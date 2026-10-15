#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Text.h"

// ---------------------------------------------------------------------------
//  Parámetros: acá se define cada control, su rango y cómo se muestra.
// ---------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout MeteringABProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::mode, 1 }, "Modo",
                                                        StringArray { "Send (antes de FX)"_u, "Receive (después de FX)"_u }, 0));

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::link, 1 }, "Link ID", 1, SharedBus::numLinks, 1));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::pre, 1 }, "PRE/POST FX", true,   // arranca en PRE FX
                                                      AudioParameterBoolAttributes()
                                                          .withStringFromValueFunction ([] (bool on, int) { return on ? String ("PRE FX") : String ("POST FX"); })));



    // Ventana en ms: acepta "1850", "1850 ms" o "1,85 s".
    NormalisableRange<float> windowRange (minWindowMs, maxWindowMs, 1.0f);
    windowRange.setSkewForCentre (3000.0f);

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::windowMs, 1 }, "Ventana (ms)",
                                                       windowRange, 3000.0f,
                                                       AudioParameterFloatAttributes()
                                                           .withLabel ("ms")
                                                           .withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)) + " ms"; })
                                                           .withValueFromStringFunction ([] (const String& t)
                                                           {
                                                               const auto lower = t.toLowerCase().trim();
                                                               const auto number = lower.retainCharacters (".,0123456789").replaceCharacter (',', '.').getFloatValue();
                                                               const bool seconds = lower.endsWith ("s") && ! lower.endsWith ("ms");
                                                               return seconds ? number * 1000.0f : number;
                                                           })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::sync, 1 }, "Ventana Sync", false,
                                                      AudioParameterBoolAttributes()
                                                          .withStringFromValueFunction ([] (bool on, int) { return on ? String ("Sync") : String ("ms"); })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::windowSync, 1 }, "Ventana (compases)",
                                                        SyncDivisions::names, SyncDivisions::defaultIndex));

    // Latencia con decimales: muchos plugins (oversampling, fase lineal)
    // retrasan el audio un número "con coma" de muestras, ej. 64,5.
    NormalisableRange<float> latencyRange (0.0f, (float) maxLatencySamples, 0.01f);
    latencyRange.setSkewForCentre (2000.0f);

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::latency, 1 }, "Latencia", latencyRange, 0.0f,
                                                       AudioParameterFloatAttributes()
                                                           .withLabel ("muestras")
                                                           .withStringFromValueFunction ([] (float v, int)
                                                           {
                                                               const bool whole = std::abs (v - std::round (v)) < 0.005f;
                                                               return (whole ? String (juce::roundToInt (v)) : String (v, 2)) + " muestras";
                                                           })
                                                           .withValueFromStringFunction ([] (const String& t)
                                                           {
                                                               return t.retainCharacters (".,0123456789").replaceCharacter (',', '.').getFloatValue();
                                                           })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::polarity, 1 }, "Polaridad invertida", false));


    return layout;
}

MeteringABProcessor::MeteringABProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pMode    = apvts.getRawParameterValue (ParamIDs::mode);
    pLink    = apvts.getRawParameterValue (ParamIDs::link);
    pPre     = apvts.getRawParameterValue (ParamIDs::pre);
    pWindowMs   = apvts.getRawParameterValue (ParamIDs::windowMs);
    pSync       = apvts.getRawParameterValue (ParamIDs::sync);
    pWindowSync = apvts.getRawParameterValue (ParamIDs::windowSync);
    pLatency = apvts.getRawParameterValue (ParamIDs::latency);
    pPolarity = apvts.getRawParameterValue (ParamIDs::polarity);

    syncStateHandled = pSync->load() > 0.5f;
    apvts.addParameterListener (ParamIDs::sync, this);
}

MeteringABProcessor::~MeteringABProcessor()
{
    apvts.removeParameterListener (ParamIDs::sync, this);
    cancelPendingUpdate();
    updateSenderRegistration (false, -1);
}

// ---------------------------------------------------------------------------
//  Tempo, compás y ventana
// ---------------------------------------------------------------------------
double MeteringABProcessor::getBarSeconds() const
{
    const double bpm = juce::jlimit (20.0, 999.0, hostBpm.load());
    const int num = juce::jlimit (1, 32, timeSigNum.load());
    const int den = juce::jlimit (1, 32, timeSigDen.load());

    // Un compás = numerador x (duración de una negra x 4 / denominador).
    // Ej: 4/4 a 120 BPM = 4 x 0,5 s = 2 s.  6/8 a 120 BPM = 6 x 0,25 s = 1,5 s.
    return num * (4.0 / den) * 60.0 / bpm;
}

double MeteringABProcessor::getWindowSeconds() const
{
    if (pSync->load() > 0.5f)
    {
        const int index = juce::jlimit (0, (int) SyncDivisions::bars.size() - 1, (int) pWindowSync->load());
        return juce::jmin (LoudnessMeter::maxWindowSeconds, SyncDivisions::bars[(size_t) index] * getBarSeconds());
    }

    return pWindowMs->load() / 1000.0;
}

int MeteringABProcessor::nearestDivision (double seconds, double barSeconds)
{
    // Se compara en escala "musical" (proporciones): 1850 ms está más cerca de
    // 1 bar (2000 ms) que de 1/2 bar (1000 ms).
    int best = 0;
    double bestDistance = 1.0e9;

    for (size_t i = 0; i < SyncDivisions::bars.size(); ++i)
    {
        const double distance = std::abs (std::log (seconds / (SyncDivisions::bars[i] * barSeconds)));

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = (int) i;
        }
    }

    return best;
}

void MeteringABProcessor::readHostTempo()
{
    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm())
                if (*bpm > 0.0)
                    hostBpm = *bpm;

            hostPlaying = position->getIsPlaying();
            hostKnowsTransport = true;
            const auto ppq = position->getPpqPosition();
            const auto barStart = position->getPpqPositionOfLastBarStart();
            const auto barCount = position->getBarCount();
            hostHasPpq = ppq.hasValue();             if (ppq)      hostPpq = *ppq;
            hostHasBarStart = barStart.hasValue();   if (barStart) hostBarStart = *barStart;
            hostHasBarCount = barCount.hasValue();   if (barCount) hostBarCount = *barCount;

            if (const auto sig = position->getTimeSignature())
            {
                if (sig->numerator > 0 && sig->denominator > 0)
                {
                    timeSigNum = sig->numerator;
                    timeSigDen = sig->denominator;
                }
            }
        }
    }
}

void MeteringABProcessor::configureMeter (LoudnessMeter& meter) const
{
    meter.setMode (LoudnessMeter::Mode::lufs);       // siempre LUFS con filtro K

    if (pSync->load() > 0.5f)
    {
        meter.setAveraging (LoudnessMeter::Averaging::rectangular);
        meter.setRectangularSeconds (getWindowSeconds());
    }
    else
    {
        meter.setAveraging (LoudnessMeter::Averaging::exponential);
        meter.setWindowSeconds (pWindowMs->load() / 1000.0);
    }
}

// Cuando cambia el botón ms/Sync (desde la interfaz, automatización o el
// host), la conversión se hace un instante después en el hilo de la interfaz.
void MeteringABProcessor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();
}

void MeteringABProcessor::startLatencyMeasurement()
{
    if (! sendConnected.load())
    {
        latencyMeasurer.setState (LatencyMeasurer::State::noSend);
        return;
    }

    latencyMeasurer.start();
}

void MeteringABProcessor::handleAsyncUpdate()
{
    // 1) ¿Terminó de grabar la medición de latencia? Calcularla.
    if (latencyMeasurer.getState() == LatencyMeasurer::State::analysing)
    {
        if (latencyMeasurer.analyse (maxLatencySamples) == LatencyMeasurer::State::done)
        {
            auto setPlainNow = [this] (const char* id, float plainValue)
            {
                if (auto* param = apvts.getParameter (id))
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
                    param->endChangeGesture();
                }
            };

            setPlainNow (ParamIDs::latency, (float) latencyMeasurer.getResult());
            setPlainNow (ParamIDs::polarity, latencyMeasurer.isInverted() ? 1.0f : 0.0f);
        }
    }

    // 2) ¿Cambió el botón ms / Sync? Convertir la ventana.
    const bool syncOn = pSync->load() > 0.5f;

    if (syncOn == syncStateHandled)
        return;

    syncStateHandled = syncOn;

    auto setPlain = [this] (const char* id, float plainValue)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
            param->endChangeGesture();
        }
    };

    if (syncOn)
    {
        // ms -> Sync: el valor musical más cercano.
        const auto index = nearestDivision (pWindowMs->load() / 1000.0, getBarSeconds());
        setPlain (ParamIDs::windowSync, (float) index);
    }
    else
    {
        // Sync -> ms: la misma duración, en milisegundos.
        const int index = juce::jlimit (0, (int) SyncDivisions::bars.size() - 1, (int) pWindowSync->load());
        const double ms = SyncDivisions::bars[(size_t) index] * getBarSeconds() * 1000.0;
        setPlain (ParamIDs::windowMs, (float) juce::jlimit ((double) minWindowMs, (double) maxWindowMs, ms));
    }
}

void MeteringABProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    preMeter.prepare (sampleRate);
    postMeter.prepare (sampleRate);
    dryBuffer.setSize (2, juce::jmax (samplesPerBlock, 4096));
    dryMono.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
    postMono.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
    latencyMeasurer.prepare();
    lastScopeLength = -1;                            // el osciloscopio arranca limpio
    gainSmoother.reset (sampleRate, 0.05);           // 50 ms de suavizado

    for (auto* m : { &mixPost, &mixPre })
        m->reset (sampleRate, crossfadeSeconds);

    setMixTargets (pPre->load() > 0.5f, true);
    gainSmoother.setCurrentAndTargetValue (1.0f);
    blocksWithoutHeartbeat = 0;
}

bool MeteringABProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

// Lleva la cuenta de cuántos Send hay en cada Link ID (para avisar si hay dos).
void MeteringABProcessor::updateSenderRegistration (bool isSend, int linkIndex)
{
    const int wanted = isSend ? linkIndex : -1;

    if (wanted == registeredSendLink)
        return;

    if (registeredSendLink >= 0)
        SharedBus::get().slot (registeredSendLink).senderCount.fetch_sub (1);

    if (wanted >= 0)
        SharedBus::get().slot (wanted).senderCount.fetch_add (1);

    registeredSendLink = wanted;
}

void MeteringABProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    lastBlockMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);   // la interfaz ve si Logic dejó de procesar

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    readHostTempo();

    const bool isSend = (int) pMode->load() == (int) Mode::send;
    const int link = (int) pLink->load() - 1;

    updateSenderRegistration (isSend, link);
    auto& slot = SharedBus::get().slot (link);

    if (link != lastLink)
    {
        lastLink = link;
        lastHeartbeat = slot.heartbeat.load();
        blocksWithoutHeartbeat = 0;
        preMeter.reset();
        postMeter.reset();
    }

    duplicateSend = slot.senderCount.load() > 1;

    if (isSend)
        processSend (buffer, slot);
    else
        processReceive (buffer, slot);
}

// ---------------------------------------------------------------------------
//  SEND: no toca el audio. Solo copia el original al bus compartido.
// ---------------------------------------------------------------------------
void MeteringABProcessor::processSend (juce::AudioBuffer<float>& buffer, LinkSlot& slot)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    if (numCh == 0 || n == 0)
        return;

    const auto start = slot.writePos.load (std::memory_order_relaxed);

    for (int ch = 0; ch < 2; ++ch)
    {
        // Si la pista es mono, se copia el mismo canal en ambos lados.
        const float* src = buffer.getReadPointer (juce::jmin (ch, numCh - 1));
        auto& ring = slot.ring[(size_t) ch];

        for (int i = 0; i < n; ++i)
            ring[(size_t) ((start + i) & LinkSlot::ringMask)] = src[i];
    }

    slot.numChannels.store (numCh);
    slot.writePos.store (start + n, std::memory_order_release);
    slot.heartbeat.fetch_add (1, std::memory_order_release);

    configureMeter (preMeter);
    preMeter.process (buffer.getArrayOfReadPointers(), numCh, n);

    displayPre    = (float) preMeter.getDecibels();
    displayPost   = displayPre.load();
    displayGain   = 0.0f;
    sendConnected = true;
}

// ---------------------------------------------------------------------------
//  RECEIVE: compara el original (del Send) con lo procesado y empareja.
// ---------------------------------------------------------------------------
void MeteringABProcessor::processReceive (juce::AudioBuffer<float>& buffer, LinkSlot& slot)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    if (numCh == 0 || n == 0)
        return;

    // ¿Está vivo el Send? Si su contador no se mueve, no hay Send en este ID.
    const auto hb = slot.heartbeat.load (std::memory_order_acquire);

    if (hb != lastHeartbeat)
    {
        lastHeartbeat = hb;
        blocksWithoutHeartbeat = 0;
    }
    else
    {
        ++blocksWithoutHeartbeat;
    }

    const int timeoutBlocks = juce::jmax (4, (int) (0.3 * currentSampleRate / n));
    const bool connected = blocksWithoutHeartbeat <= timeoutBlocks && n < LinkSlot::ringSize / 2;
    sendConnected = connected;

    configureMeter (preMeter);
    configureMeter (postMeter);

    // Medidores en cero: con Logic en pausa, o al hacer clic sobre los números.
    // (La ganancia aplicada se mantiene: al volver el audio se recalcula enseguida.)
    const bool paused = hostKnowsTransport && ! hostPlaying;

    if (meterResetRequested.exchange (false) || paused)
    {
        preMeter.reset();
        postMeter.reset();
    }

    postMeter.process (buffer.getArrayOfReadPointers(), numCh, n);

    if (! connected)
    {
        // Sin Send: el audio pasa sin cambios.
        if (latencyMeasurer.isMeasuring())
            latencyMeasurer.setState (LatencyMeasurer::State::noSend);

        gainSmoother.setCurrentAndTargetValue (1.0f);
        scopeWasPlaying = false;                        // el próximo play arranca de cero
        scopeIsRunning.store (false, std::memory_order_relaxed);
        displayPre  = -120.0f;
        displayPost = paused ? -120.0f : (float) postMeter.getDecibels();
        displayGain = 0.0f;
        displayGainValid = false;
        return;
    }

    // 1) Traer el audio original, retrasado según la latencia de tus FX.
    if (dryBuffer.getNumSamples() < n)
        dryBuffer.setSize (2, n, false, false, true);  // caso raro: bloque más grande de lo esperado

    const double latencyExact = juce::jlimit (0.0, (double) (LinkSlot::ringSize - n - 2 * maxInterpolationHalfLength),
                                              (double) pLatency->load());
    const int latency = (int) std::lround (latencyExact);   // PRE FX: el original intacto, sin filtrar
    const auto end = slot.writePos.load (std::memory_order_acquire);
    const auto start = end - n - latency;

    // Medición de latencia: graba el original SIN compensar y lo procesado.
    if (latencyMeasurer.isMeasuring() && (int) dryMono.size() >= n)
    {
        const auto rawStart = end - n;

        for (int i = 0; i < n; ++i)
        {
            const auto pos = rawStart + i;
            float d = 0.0f, p = 0.0f;

            for (int ch = 0; ch < numCh; ++ch)
            {
                d += pos >= 0 ? slot.ring[(size_t) ch][(size_t) (pos & LinkSlot::ringMask)] : 0.0f;
                p += buffer.getSample (ch, i);
            }

            dryMono[(size_t) i]  = d / (float) numCh;
            postMono[(size_t) i] = p / (float) numCh;
        }

        if (latencyMeasurer.push (dryMono.data(), postMono.data(), n))
            triggerAsyncUpdate();                       // el cálculo se hace fuera del audio
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto& ring = slot.ring[(size_t) ch];
        float* dst = dryBuffer.getWritePointer (ch);

        for (int i = 0; i < n; ++i)
        {
            const auto pos = start + i;
            dst[i] = pos >= 0 ? ring[(size_t) (pos & LinkSlot::ringMask)] : 0.0f;
        }
    }

    preMeter.process (dryBuffer.getArrayOfReadPointers(), numCh, n);

    // 2) Matcheo automático: cuánta ganancia hace falta para igualar el nivel.
    {
        const double preMs = preMeter.getMeanSquare();
        const double postMs = postMeter.getMeanSquare();
        constexpr double gate = 1.0e-6;                  // -60 dB: no matchear silencio

        if (preMs > gate && postMs > 1.0e-12)
            autoGainDb = juce::jlimit (-autoLimitDb, autoLimitDb, (float) (10.0 * std::log10 (preMs / postMs)));
    }

    const float targetDb = autoGainDb;
    gainSmoother.setTargetValue (juce::Decibels::decibelsToGain (targetDb));

    // 3) Aplicar según lo que quieras escuchar: PRE FX u POST FX.
    const bool pre = pPre->load() > 0.5f;

    // Crossfade: los pesos van hacia lo que elegiste en 10 ms.
    setMixTargets (pre, false);

    // Osciloscopio: dónde arranca este bloque dentro del loop y cuánto avanza por muestra.
    // Solo se dibuja con Logic en play; en pausa queda quieto.
    double scopeQuarters = scopeBlockStartQuarters (n);
    const bool scopeRunning = hostPlaying;
    const double loopQuarters = getScopeLengthBars() * timeSigNum.load() * 4.0 / timeSigDen.load();
    const double quartersPerSample = hostBpm.load() / 60.0 / currentSampleRate;

    // Logic ya compensa la latencia de los efectos (lee el audio adelantado),
    // así que la posición que informa coincide con el audio que llega acá:
    // no hay que restar nada.

    // Al dar play: limpiar y dibujar solo desde el punto de play (Logic
    // empieza a procesar un poco antes para compensar la latencia).
    if (scopeRunning && ! scopeWasPlaying)
    {
        clearScope();
        scopePlayStartQuarters = hostHasPpq ? hostPpq : 0.0;

        // Si Logic arrancó apenas antes de un tiempo (lo que adelanta para
        // compensar la latencia), el punto de play real es ese tiempo.
        const double nextBeat = std::ceil (scopePlayStartQuarters - 1.0e-9);
        const double preRollWindow = latencyExact * quartersPerSample + 0.05;

        if (nextBeat - scopePlayStartQuarters <= preRollWindow)
            scopePlayStartQuarters = nextBeat;

        scopeGateOpen = false;                          // esperar a llegar al punto de play
    }
    else if (scopeRunning && hostHasPpq && hostPpq < scopeLastBlockPpq - 1.0e-6)
    {
        // Logic saltó hacia atrás (modo ciclo, o un play nuevo sin pasar por
        // stop): seguir dibujando sin esperar al punto de play anterior.
        scopeGateOpen = true;
    }

    scopeWasPlaying = scopeRunning;
    scopeLastBlockPpq = hostHasPpq ? hostPpq : 0.0;
    double songQuarters = hostHasPpq ? hostPpq : scopePlayStartQuarters;   // posición del audio que llega

    for (int i = 0; i < n; ++i)
    {
        const float g = gainSmoother.getNextValue();
        const float wPost = mixPost.getNextValue(), wPre = mixPre.getNextValue();
        float preSumValue = 0.0f, postSumValue = 0.0f;
        float preCh[2] {}, postCh[2] {};

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* out = buffer.getWritePointer (ch);
            const float dry = dryBuffer.getSample (ch, i);
            preSumValue  += dry;
            postSumValue += out[i] * g;                  // POST nivelado, escuches lo que escuches
            preCh[ch]  = dry;
            postCh[ch] = out[i] * g;

            const float post = out[i] * g;                                           // POST FX: procesado y nivelado
            out[i] = wPost * post + wPre * dry;                                       // PRE FX = dry
        }

        // Osciloscopio: columna que corresponde a este momento del compás.
        if (scopeRunning && ! scopeGateOpen && songQuarters >= scopePlayStartQuarters - 1.0e-9)
            scopeGateOpen = true;

        if (scopeRunning && scopeGateOpen)
        {
            if (numCh == 1)                             // pista mono: L y R iguales
            {
                preCh[1] = preCh[0];
                postCh[1] = postCh[0];
            }

            const float values[numScopeTraces] = { preSumValue / (float) numCh, postSumValue / (float) numCh,
                                                   preCh[0], postCh[0], preCh[1], postCh[1] };
            const int bin = juce::jlimit (0, scopeBins - 1, (int) (scopeQuarters / loopQuarters * scopeBins));
            const auto k = (size_t) bin;
            const bool newBin = bin != lastScopeBin;    // columna nueva: reemplaza la del loop anterior

            for (int t = 0; t < numScopeTraces; ++t)
            {
                auto& trace = scopeTraces[(size_t) t];
                const float v = values[t];

                if (newBin)
                {
                    trace.min[k].store (v, std::memory_order_relaxed);
                    trace.max[k].store (v, std::memory_order_relaxed);
                    trace.sq[k].store (v * v, std::memory_order_relaxed);
                }
                else
                {
                    if (v < trace.min[k].load (std::memory_order_relaxed)) trace.min[k].store (v, std::memory_order_relaxed);
                    if (v > trace.max[k].load (std::memory_order_relaxed)) trace.max[k].store (v, std::memory_order_relaxed);
                    trace.sq[k].store (trace.sq[k].load (std::memory_order_relaxed) + v * v, std::memory_order_relaxed);
                }
            }

            scopeCount[k].store (newBin ? 1 : scopeCount[k].load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            scopeShowPreAt[k].store (pre ? 1 : 0, std::memory_order_relaxed);   // lo que sonaba al dibujarla
            lastScopeBin = bin;
        }

        if (scopeRunning)
        {
            songQuarters += quartersPerSample;
            scopeQuarters += quartersPerSample;
            if (scopeQuarters >= loopQuarters)
                scopeQuarters -= loopQuarters;
        }
    }

    scopePhaseQuarters = scopeQuarters;
    scopeIsRunning.store (scopeRunning, std::memory_order_relaxed);
    scopeCurrentBin.store (juce::jlimit (0, scopeBins - 1, (int) (scopeQuarters / loopQuarters * scopeBins)), std::memory_order_relaxed);

    if (paused)
    {
        preMeter.reset();                              // en pausa quedan en -∞
        postMeter.reset();
        displayPre  = -120.0f;
        displayPost = -120.0f;
        displayGainValid = false;
        return;
    }

    displayPre  = (float) preMeter.getDecibels();
    displayPost = (float) postMeter.getDecibels();
    displayGain = targetDb;
    displayGainValid = preMeter.getMeanSquare() > 1.0e-6;   // hay audio medido
}

void MeteringABProcessor::setMixTargets (bool pre, bool immediately)
{
    const float tPre  = pre ? 1.0f : 0.0f;
    const float tPost = 1.0f - tPre;

    if (immediately)
    {
        mixPre.setCurrentAndTargetValue (tPre);
        mixPost.setCurrentAndTargetValue (tPost);
    }
    else if (! juce::exactlyEqual (mixPre.getTargetValue(), tPre))
    {
        mixPre.setTargetValue (tPre);
        mixPost.setTargetValue (tPost);
    }
}

void MeteringABProcessor::clearScope()
{
    for (int b = 0; b < scopeBins; ++b)
    {
        for (auto& trace : scopeTraces)
        {
            trace.min[(size_t) b] = 0.0f;
            trace.max[(size_t) b] = 0.0f;
            trace.sq[(size_t) b] = 0.0f;
        }

        scopeCount[(size_t) b] = 0;
        scopeShowPreAt[(size_t) b] = (std::uint8_t) (pPre->load() > 0.5f ? 1 : 0);
    }

    lastScopeBin = -1;
}

// Dónde arranca el bloque dentro del loop del osciloscopio (en negras).
// Sigue siempre la posición del cursor de Logic (alineada a los compases).
// Con Logic en pausa el cabezal queda quieto y no se dibuja nada nuevo.
double MeteringABProcessor::scopeBlockStartQuarters (int n)
{
    const int lengthIndex = scopeLengthIndex.load();
    const double lengthBars = getScopeLengthBars();
    const double barQuarters = timeSigNum.load() * 4.0 / timeSigDen.load();
    const double loopQuarters = lengthBars * barQuarters;

    if (lengthIndex != lastScopeLength)                 // cambió el largo: empezar limpio
    {
        clearScope();
        lastScopeLength = lengthIndex;
    }

    double phase = scopePhaseQuarters;

    // Con play o en pausa, el cabezal va donde está el cursor de Logic
    // (en pausa se queda quieto ahí; si movés el cursor, lo sigue).
    if (hostHasPpq)
    {
        if (hostHasBarStart)
        {
            const double intoBar = juce::jmax (0.0, hostPpq - hostBarStart);

            if (lengthBars >= 1.0)
            {
                const auto barIndex = hostHasBarCount ? hostBarCount
                                                      : (juce::int64) std::floor (hostBarStart / barQuarters + 0.5);
                const auto barsInLoop = (juce::int64) lengthBars;
                phase = (double) (((barIndex % barsInLoop) + barsInLoop) % barsInLoop) * barQuarters + intoBar;
            }
            else
            {
                phase = std::fmod (intoBar, loopQuarters);
            }
        }
        else
        {
            phase = std::fmod (hostPpq, loopQuarters);
        }
    }

    phase = std::fmod (phase, loopQuarters);
    if (phase < 0.0)
        phase += loopQuarters;

    juce::ignoreUnused (n);
    return phase;
}

void MeteringABProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MeteringABProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            // Al abrir un proyecto se respetan los valores guardados tal cual
            // (no es un cambio de ms/Sync hecho por el usuario).
            cancelPendingUpdate();
            syncStateHandled = pSync->load() > 0.5f;

            if (latencyMeasurer.getState() == LatencyMeasurer::State::analysing)
                triggerAsyncUpdate();                   // no perder una medición en curso
        }
}

juce::AudioProcessorEditor* MeteringABProcessor::createEditor()
{
    return new MeteringABEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MeteringABProcessor();
}
