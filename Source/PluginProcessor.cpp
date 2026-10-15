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

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::delta, 1 }, "Delta", false));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::measure, 1 }, "Medición"_u,
                                                        StringArray { "RMS", "LUFS (filtro K)" }, 1));

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

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::hold, 1 }, "Congelar", false));

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
    pDelta   = apvts.getRawParameterValue (ParamIDs::delta);
    pMeasure = apvts.getRawParameterValue (ParamIDs::measure);
    pWindowMs   = apvts.getRawParameterValue (ParamIDs::windowMs);
    pSync       = apvts.getRawParameterValue (ParamIDs::sync);
    pWindowSync = apvts.getRawParameterValue (ParamIDs::windowSync);
    pLatency = apvts.getRawParameterValue (ParamIDs::latency);
    pPolarity = apvts.getRawParameterValue (ParamIDs::polarity);
    pHold    = apvts.getRawParameterValue (ParamIDs::hold);

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
    meter.setMode ((LoudnessMeter::Mode) (int) pMeasure->load());

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
    deltaDryBuffer.setSize (2, juce::jmax (samplesPerBlock, 4096));
    dryMono.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
    postMono.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
    latencyMeasurer.prepare();
    lastScopeLength = -1;                            // el osciloscopio arranca limpio
    gainSmoother.reset (sampleRate, 0.05);           // 50 ms de suavizado
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

    postMeter.process (buffer.getArrayOfReadPointers(), numCh, n);

    if (! connected)
    {
        // Sin Send: el audio pasa sin cambios.
        if (latencyMeasurer.isMeasuring())
            latencyMeasurer.setState (LatencyMeasurer::State::noSend);

        gainSmoother.setCurrentAndTargetValue (1.0f);
        displayPre  = -120.0f;
        displayPost = (float) postMeter.getDecibels();
        displayGain = 0.0f;
        return;
    }

    // 1) Traer el audio original, retrasado según la latencia de tus FX.
    if (dryBuffer.getNumSamples() < n)
    {
        dryBuffer.setSize (2, n, false, false, true);  // caso raro: bloque más grande de lo esperado
        deltaDryBuffer.setSize (2, n, false, false, true);
    }

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

        if (pHold->load() < 0.5f && preMs > gate && postMs > 1.0e-12)
            autoGainDb = juce::jlimit (-autoLimitDb, autoLimitDb, (float) (10.0 * std::log10 (preMs / postMs)));
    }

    const float targetDb = autoGainDb;
    gainSmoother.setTargetValue (juce::Decibels::decibelsToGain (targetDb));

    // 3) Aplicar según lo que quieras escuchar.
    //    DELTA manda sobre todo; si no, el botón elige PRE FX u POST FX.
    const bool delta = pDelta->load() > 0.5f;
    const bool pre   = pPre->load() > 0.5f;
    const float polaritySign = pPolarity->load() > 0.5f ? -1.0f : 1.0f;

    // Para DELTA el original se alinea con decimales (ej. 64,5 muestras),
    // así se anula exacto lo que tus efectos no cambiaron.
    const juce::AudioBuffer<float>* deltaDry = &dryBuffer;

    if (delta && std::abs (latencyExact - latency) > 0.001)
    {
        readDryFractional (slot, end, n, numCh, latencyExact);
        deltaDry = &deltaDryBuffer;
    }

    // Osciloscopio: dónde arranca este bloque dentro del loop y cuánto avanza por muestra.
    double scopeQuarters = scopeBlockStartQuarters (n);
    const double loopQuarters = getScopeLengthBars() * timeSigNum.load() * 4.0 / timeSigDen.load();
    const double quartersPerSample = hostBpm.load() / 60.0 / currentSampleRate;

    for (int i = 0; i < n; ++i)
    {
        const float g = gainSmoother.getNextValue();
        float preSum = 0.0f, postSum = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* out = buffer.getWritePointer (ch);
            const float dry = dryBuffer.getSample (ch, i);
            preSum  += dry;
            postSum += out[i] * g;                       // POST nivelado, escuches lo que escuches

            if (delta)      out[i] = out[i] * g - polaritySign * deltaDry->getSample (ch, i);   // solo lo que cambiaron tus FX
            else if (pre)   out[i] = dry;                // PRE FX: el original
            else            out[i] = out[i] * g;         // POST FX: procesado y nivelado
        }

        // Osciloscopio: columna que corresponde a este momento del compás.
        {
            const float preValue  = preSum / (float) numCh;
            const float postValue = postSum / (float) numCh;
            const int bin = juce::jlimit (0, scopeBins - 1, (int) (scopeQuarters / loopQuarters * scopeBins));

            if (bin != lastScopeBin)                    // columna nueva: reemplaza la del loop anterior
            {
                scopePreMin[(size_t) bin].store (preValue, std::memory_order_relaxed);
                scopePreMax[(size_t) bin].store (preValue, std::memory_order_relaxed);
                scopePostMin[(size_t) bin].store (postValue, std::memory_order_relaxed);
                scopePostMax[(size_t) bin].store (postValue, std::memory_order_relaxed);
                lastScopeBin = bin;
            }
            else
            {
                auto& a = scopePreMin[(size_t) bin];   if (preValue  < a.load (std::memory_order_relaxed)) a.store (preValue,  std::memory_order_relaxed);
                auto& b = scopePreMax[(size_t) bin];   if (preValue  > b.load (std::memory_order_relaxed)) b.store (preValue,  std::memory_order_relaxed);
                auto& c = scopePostMin[(size_t) bin];  if (postValue < c.load (std::memory_order_relaxed)) c.store (postValue, std::memory_order_relaxed);
                auto& d = scopePostMax[(size_t) bin];  if (postValue > d.load (std::memory_order_relaxed)) d.store (postValue, std::memory_order_relaxed);
            }

            scopeQuarters += quartersPerSample;
            if (scopeQuarters >= loopQuarters)
                scopeQuarters -= loopQuarters;
        }
    }

    scopePhaseQuarters = scopeQuarters;
    scopeCurrentBin.store (juce::jlimit (0, scopeBins - 1, (int) (scopeQuarters / loopQuarters * scopeBins)), std::memory_order_relaxed);

    displayPre  = (float) preMeter.getDecibels();
    displayPost = (float) postMeter.getDecibels();
    displayGain = targetDb;
}

// Dónde arranca el bloque dentro del loop del osciloscopio (en negras).
// Con la canción sonando sigue la posición de Logic (alineada a los compases);
// detenida, el barrido sigue corriendo solo al tempo del proyecto.
double MeteringABProcessor::scopeBlockStartQuarters (int n)
{
    const int lengthIndex = scopeLengthIndex.load();
    const double lengthBars = getScopeLengthBars();
    const double barQuarters = timeSigNum.load() * 4.0 / timeSigDen.load();
    const double loopQuarters = lengthBars * barQuarters;

    if (lengthIndex != lastScopeLength)                 // cambió el largo: empezar limpio
    {
        for (int b = 0; b < scopeBins; ++b)
        {
            scopePreMin[(size_t) b] = 0.0f;  scopePreMax[(size_t) b] = 0.0f;
            scopePostMin[(size_t) b] = 0.0f; scopePostMax[(size_t) b] = 0.0f;
        }
        lastScopeLength = lengthIndex;
        lastScopeBin = -1;
    }

    double phase = scopePhaseQuarters;

    if (hostPlaying && hostHasPpq)
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

// ---------------------------------------------------------------------------
//  Lee el audio original retrasado un número de muestras CON DECIMALES.
//  Usa interpolación "sinc con ventana": reconstruye el valor exacto entre
//  dos muestras, como haría la onda continua original.
// ---------------------------------------------------------------------------
void MeteringABProcessor::readDryFractional (const LinkSlot& slot, juce::int64 end, int n, int numCh, double latencyExact)
{
    const int whole = (int) std::floor (latencyExact);
    const double frac = latencyExact - whole;

    // Cuántas muestras a cada lado se pueden usar: no podemos leer audio
    // "del futuro", así que con latencias muy chicas se usan menos.
    const int half = juce::jlimit (1, maxInterpolationHalfLength, whole + 1);

    // Pesos del interpolador (iguales para todo el bloque).
    std::array<double, 2 * maxInterpolationHalfLength> weights {};
    double sum = 0.0;

    for (int k = -(half - 1); k <= half; ++k)
    {
        const double x = k - frac;                                    // distancia a la muestra
        const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
        const double u = x / (double) half;                           // -1..1
        const double window = std::abs (u) >= 1.0 ? 0.0
                            : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * u) + 0.08 * std::cos (2.0 * juce::MathConstants<double>::pi * u);
        weights[(size_t) (k + half - 1)] = sinc * window;
        sum += sinc * window;
    }

    for (int k = 0; k < 2 * half; ++k)
        weights[(size_t) k] /= sum;                                    // sin cambio de volumen

    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto& ring = slot.ring[(size_t) ch];
        float* dst = deltaDryBuffer.getWritePointer (ch);

        for (int i = 0; i < n; ++i)
        {
            const juce::int64 base = end - n + i - whole;              // muestra entera más cercana
            double acc = 0.0;

            for (int k = -(half - 1); k <= half; ++k)
            {
                const auto pos = base - k;
                if (pos >= 0)
                    acc += weights[(size_t) (k + half - 1)] * ring[(size_t) (pos & LinkSlot::ringMask)];
            }

            dst[i] = (float) acc;
        }
    }
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
