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

    auto dbAttributes = AudioParameterFloatAttributes()
        .withLabel ("dB")
        .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " dB"; })
        .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("-+.,0123456789").replaceCharacter (',', '.').getFloatValue(); });

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::mode, 1 }, "Modo",
                                                        StringArray { "Send (antes de FX)"_u, "Receive (después de FX)"_u }, 0));

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::link, 1 }, "Link ID", 1, SharedBus::numLinks, 1));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::match, 1 }, "Matcheo",
                                                        StringArray { "Auto", "Manual", "Apagado" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::manual, 1 }, "Ganancia manual",
                                                       NormalisableRange<float> (minGainDb, maxGainDb, 0.01f), 0.0f, dbAttributes));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::pre, 1 }, "PRE/POST FX", false,
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

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::latency, 1 }, "Latencia", 0, maxLatencySamples, 0,
                                                     AudioParameterIntAttributes()
                                                         .withLabel ("muestras")
                                                         .withStringFromValueFunction ([] (int v, int) { return String (v) + " muestras"; })
                                                         .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("0123456789").getIntValue(); })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::limit, 1 }, "Límite auto"_u,
                                                       NormalisableRange<float> (0.0f, maxGainDb, 0.1f), 18.0f, dbAttributes));

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
    pMatch   = apvts.getRawParameterValue (ParamIDs::match);
    pManual  = apvts.getRawParameterValue (ParamIDs::manual);
    pPre     = apvts.getRawParameterValue (ParamIDs::pre);
    pDelta   = apvts.getRawParameterValue (ParamIDs::delta);
    pMeasure = apvts.getRawParameterValue (ParamIDs::measure);
    pWindowMs   = apvts.getRawParameterValue (ParamIDs::windowMs);
    pSync       = apvts.getRawParameterValue (ParamIDs::sync);
    pWindowSync = apvts.getRawParameterValue (ParamIDs::windowSync);
    pLatency = apvts.getRawParameterValue (ParamIDs::latency);
    pLimit   = apvts.getRawParameterValue (ParamIDs::limit);
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
            if (auto* param = apvts.getParameter (ParamIDs::latency))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 ((float) latencyMeasurer.getResult()));
                param->endChangeGesture();
            }
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
        dryBuffer.setSize (2, n, false, false, true);  // caso raro: bloque más grande de lo esperado

    const int latency = juce::jlimit (0, LinkSlot::ringSize - n - 1, (int) pLatency->load());
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

    // 2) Calcular cuánta ganancia hace falta.
    float targetDb = 0.0f;

    switch ((Match) (int) pMatch->load())
    {
        case Match::automatic:
        {
            const double preMs = preMeter.getMeanSquare();
            const double postMs = postMeter.getMeanSquare();
            constexpr double gate = 1.0e-6;              // -60 dB: no matchear silencio

            if (pHold->load() < 0.5f && preMs > gate && postMs > 1.0e-12)
            {
                const auto limit = pLimit->load();
                autoGainDb = juce::jlimit (-limit, limit, (float) (10.0 * std::log10 (preMs / postMs)));
            }

            targetDb = autoGainDb;
            break;
        }

        case Match::manual: targetDb = pManual->load(); break;
        case Match::off:    targetDb = 0.0f;            break;
    }

    gainSmoother.setTargetValue (juce::Decibels::decibelsToGain (targetDb));

    // 3) Aplicar según lo que quieras escuchar.
    //    DELTA manda sobre todo; si no, el botón elige PRE FX u POST FX.
    const bool delta = pDelta->load() > 0.5f;
    const bool pre   = pPre->load() > 0.5f;

    for (int i = 0; i < n; ++i)
    {
        const float g = gainSmoother.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* out = buffer.getWritePointer (ch);
            const float dry = dryBuffer.getSample (ch, i);

            if (delta)      out[i] = out[i] * g - dry;   // solo lo que cambiaron tus FX
            else if (pre)   out[i] = dry;                // PRE FX: el original
            else            out[i] = out[i] * g;         // POST FX: procesado y nivelado
        }
    }

    displayPre  = (float) preMeter.getDecibels();
    displayPost = (float) postMeter.getDecibels();
    displayGain = targetDb;
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
