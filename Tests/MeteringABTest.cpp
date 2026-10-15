#include "PluginProcessor.h"
#include <cstdio>

static void setParam (MeteringABProcessor& p, const char* id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

static double rmsDb (const juce::AudioBuffer<float>& b)
{
    double s = 0; int n = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) { s += b.getSample (ch, i) * b.getSample (ch, i); ++n; }
    return 10.0 * std::log10 (s / n + 1e-30);
}

struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 120.0; int num = 4, den = 4;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { num, den });
        return p;
    }
};

static float plain (MeteringABProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

static void oneBlock (MeteringABProcessor& p)
{
    juce::AudioBuffer<float> b (2, 512); b.clear(); juce::MidiBuffer m;
    p.processBlock (b, m);
}

int failures = 0;
static void check (bool ok, const char* what, double value)
{
    std::printf ("%s  %-55s %.3f\n", ok ? "OK  " : "FAIL", what, value);
    if (! ok) ++failures;
}

struct Chain
{
    MeteringABProcessor send, recv;
    juce::MidiBuffer midi;
    std::vector<float> fxDelayL, fxDelayR;
    int fxLatency;
    float fxGain;
    double phase = 0;
    juce::Random rng { 42 }, otherRng { 7 };
    bool lowpass = false, unrelated = false, silent = false;   // tipos de "efecto"
    float lpState[2] = { 0.0f, 0.0f };

    Chain (int link, float gainDb, int latency) : fxLatency (latency), fxGain (juce::Decibels::decibelsToGain (gainDb))
    {
        setParam (send, ParamIDs::mode, 0); setParam (send, ParamIDs::link, (float) link);
        setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, (float) link);
        send.prepareToPlay (48000, 512); recv.prepareToPlay (48000, 512);
        fxDelayL.assign ((size_t) juce::jmax (1, latency), 0); fxDelayR.assign ((size_t) juce::jmax (1, latency), 0);
    }

    // Procesa "seconds" de ruido rosado-ish/sinusoide y devuelve el último bloque.
    juce::AudioBuffer<float> run (double seconds, juce::AudioBuffer<float>* lastDry = nullptr)
    {
        juce::AudioBuffer<float> buf (2, 512), dry (2, 512);
        const int blocks = (int) (seconds * 48000 / 512);
        size_t w = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                float x = silent ? 0.0f : 0.25f * (float) std::sin (phase) + 0.05f * (rng.nextFloat() * 2 - 1);
                phase += 2 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
                buf.setSample (0, i, x); buf.setSample (1, i, x * 0.8f);
            }
            dry.makeCopyOf (buf);
            send.processBlock (buf, midi);
            // "Efecto": ganancia + latencia
            for (int i = 0; i < 512; ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& d = ch == 0 ? fxDelayL : fxDelayR;
                    size_t idx = (fxWrite + (size_t) i) % d.size();
                    float in = buf.getSample (ch, i);
                    float out = d[idx];
                    d[idx] = in;
                    float y = fxLatency == 0 ? in * fxGain : out * fxGain;
                    if (lowpass) { lpState[ch] += 0.15f * (y - lpState[ch]); y = lpState[ch]; }   // "EQ" que quita agudos
                    if (unrelated) y = 0.2f * (otherRng.nextFloat() * 2 - 1);                   // sonido sin relación
                    buf.setSample (ch, i, y);
                }
            fxWrite = (fxWrite + 512) % fxDelayL.size();
            recv.processBlock (buf, midi);
            if (lastDry) lastDry->makeCopyOf (dry);
        }
        (void) w;
        return buf;
    }
    size_t fxWrite = 0;
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc > 1) // Metering A/BTests <carpeta>: guarda capturas de la interfaz
    {
        for (int shot = 0; shot < 3; ++shot)
        {
            MeteringABProcessor a; FakePlayHead ph; a.setPlayHead (&ph); a.prepareToPlay (48000, 512);
            setParam (a, ParamIDs::mode, shot == 0 ? 0.0f : 1.0f);
            if (shot == 1) { setParam (a, ParamIDs::latency, 64); a.latencyMeasurer.setState (LatencyMeasurer::State::done); }
            if (shot == 2) { oneBlock (a); setParam (a, ParamIDs::sync, 1); a.processPendingConversion(); setParam (a, ParamIDs::windowSync, 2); setParam (a, ParamIDs::pre, 1); }
            a.displayPre = -14.2f; a.displayPost = -9.8f; a.displayGain = -4.4f; a.sendConnected = true;
            std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            const char* names[] = { "/send.png", "/receive.png", "/receive-sync.png" };
            juce::File f (juce::String (argv[1]) + names[shot]);
            f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            ed.reset(); a.setPlayHead (nullptr);
        }
        return 0;
    }

    {
        Chain c (1, -6.0f, 0);
        auto out = c.run (12.0);
        check (std::abs (c.recv.displayGain.load() - 6.0f) < 0.1f, "Auto (LUFS): FX -6 dB -> ganancia ~ +6 dB", c.recv.displayGain.load());
        check (c.recv.sendConnected.load(), "Receive detecta el Send", 1);
        juce::AudioBuffer<float> dry; c.run (0.5, &dry); out = c.run (0.02, &dry);
        check (std::abs (rmsDb (out) - rmsDb (dry)) < 0.2, "Salida matcheada = nivel original (dB de diferencia)", rmsDb (out) - rmsDb (dry));

        setParam (c.recv, ParamIDs::delta, 1);
        out = c.run (1.0);
        check (rmsDb (out) < -60, "Delta con FX de pura ganancia ~ silencio (dB)", rmsDb (out));

        setParam (c.recv, ParamIDs::pre, 1);               // PRE FX con DELTA activo: manda DELTA
        out = c.run (0.5);
        check (rmsDb (out) < -60, "DELTA manda aunque esté en PRE FX (dB)", rmsDb (out));

        setParam (c.recv, ParamIDs::delta, 0);             // PRE FX
        out = c.run (0.5, &dry);
        check (std::abs (rmsDb (out) - rmsDb (dry)) < 0.01, "PRE FX = audio original del Send", rmsDb (out) - rmsDb (dry));

        setParam (c.recv, ParamIDs::pre, 0);               // POST FX
        c.run (0.5, &dry); out = c.run (0.02, &dry);
        check (std::abs (rmsDb (out) - rmsDb (dry)) < 0.2, "POST FX = procesado al mismo nivel", rmsDb (out) - rmsDb (dry));
        check (c.recv.apvts.getParameter (ParamIDs::pre)->getCurrentValueAsText() == "POST FX", "El parámetro se muestra como 'POST FX'", 1);
    }
    {
        Chain c (2, 4.5f, 0);
        setParam (c.recv, ParamIDs::measure, 0);
        c.run (12.0);
        check (std::abs (c.recv.displayGain.load() + 4.5f) < 0.1f, "Auto (RMS): FX +4.5 dB -> ganancia ~ -4.5 dB", c.recv.displayGain.load());
    }
    {
        Chain c (3, -3.0f, 300);
        setParam (c.recv, ParamIDs::latency, 300);
        setParam (c.recv, ParamIDs::delta, 1);
        c.run (12.0);
        auto out = c.run (1.0);
        check (rmsDb (out) < -60, "Latencia 300 compensada: Delta ~ silencio (dB)", rmsDb (out));
        setParam (c.recv, ParamIDs::latency, 0);
        out = c.run (1.0);
        check (rmsDb (out) > -40, "Latencia sin compensar: Delta NO es silencio (dB)", rmsDb (out));
    }
    {
        Chain c (4, -6.0f, 0);
        setParam (c.recv, ParamIDs::match, 1);
        setParam (c.recv, ParamIDs::manual, 2.37f);
        c.run (3.0);
        check (std::abs (c.recv.displayGain.load() - 2.37f) < 0.001f, "Manual: valor exacto 2.37 dB", c.recv.displayGain.load());
        auto* p = c.recv.apvts.getParameter (ParamIDs::manual);
        check (std::abs (p->getValueForText ("-3,25 dB") * 48 - 24 + 3.25f) < 0.01f, "Escribir '-3,25 dB' se interpreta como -3.25", p->getValueForText ("-3,25 dB") * 48 - 24);
    }
    {
        Chain c (5, -20.0f, 0);
        setParam (c.recv, ParamIDs::limit, 12.0f);
        c.run (12.0);
        check (std::abs (c.recv.displayGain.load() - 12.0f) < 0.01f, "Límite auto 12 dB respeta el tope", c.recv.displayGain.load());
    }
    {
        MeteringABProcessor lonely; juce::MidiBuffer m;
        setParam (lonely, ParamIDs::mode, 1); setParam (lonely, ParamIDs::link, 9);
        lonely.prepareToPlay (48000, 512);
        juce::AudioBuffer<float> b (2, 512);
        for (int k = 0; k < 100; ++k) { for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.3f); b.setSample (1, i, 0.3f); } lonely.processBlock (b, m); }
        check (! lonely.sendConnected.load() && std::abs (b.getSample (0, 100) - 0.3f) < 1e-6f, "Sin Send: avisa y deja pasar el audio igual", b.getSample (0, 100));
    }
    {
        MeteringABProcessor a, b2; juce::MidiBuffer m;
        setParam (a, ParamIDs::link, 10); setParam (b2, ParamIDs::link, 10);
        a.prepareToPlay (48000, 512); b2.prepareToPlay (48000, 512);
        juce::AudioBuffer<float> buf (2, 512);
        a.processBlock (buf, m); b2.processBlock (buf, m); a.processBlock (buf, m);
        check (a.duplicateSend.load() && b2.duplicateSend.load(), "Dos Send en el mismo ID: avisan los dos", 1);
    }
    {
        // Guardar/recuperar estado (como cuando abrís el proyecto de Logic).
        MeteringABProcessor a, b2;
        setParam (a, ParamIDs::manual, -7.13f); setParam (a, ParamIDs::latency, 128);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        b2.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b2.apvts.getRawParameterValue (ParamIDs::manual)->load() + 7.13f) < 0.005f, "Estado guardado y recuperado (manual)", b2.apvts.getRawParameterValue (ParamIDs::manual)->load());
    }
    {
        // El editor se puede crear y destruir sin problemas.
        MeteringABProcessor a;
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        check (ed != nullptr && ed->getWidth() == 640 && ed->getHeight() == 478, "Editor se crea (ancho px)", ed->getWidth());
    }


    {
        // Filtro K: 1 kHz estéreo a 0 dBFS = ~0 LUFS; 30 Hz se lee mucho más bajo. En RMS, iguales.
        auto measure = [] (double freq, LoudnessMeter::Mode mode)
        {
            LoudnessMeter m; m.prepare (48000); m.setMode (mode);
            m.setAveraging (LoudnessMeter::Averaging::rectangular); m.setRectangularSeconds (1.0);
            juce::AudioBuffer<float> b (2, 48000);
            for (int i = 0; i < 48000; ++i) { auto v = (float) std::sin (2 * juce::MathConstants<double>::pi * freq * i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
            m.process (b.getArrayOfReadPointers(), 2, 48000); m.process (b.getArrayOfReadPointers(), 2, 48000);
            return m.getDecibels();
        };
        check (std::abs (measure (1000, LoudnessMeter::Mode::lufs)) < 0.15, "Filtro K: 1 kHz estéreo 0 dBFS = ~0 LUFS", measure (1000, LoudnessMeter::Mode::lufs));
        check (measure (30, LoudnessMeter::Mode::lufs) < -4.0, "Filtro K: 30 Hz se lee bastante más bajo (LUFS)", measure (30, LoudnessMeter::Mode::lufs));
        check (std::abs (measure (30, LoudnessMeter::Mode::rms) - measure (1000, LoudnessMeter::Mode::rms)) < 0.05, "RMS: 30 Hz y 1 kHz se leen igual (diferencia dB)", measure (30, LoudnessMeter::Mode::rms) - measure (1000, LoudnessMeter::Mode::rms));
    }
    {
        // Conversión ms <-> Sync a 120 BPM 4/4 (1 compás = 2000 ms).
        MeteringABProcessor p; FakePlayHead ph; p.setPlayHead (&ph); p.prepareToPlay (48000, 512); oneBlock (p);
        check (std::abs (p.getBarSeconds() - 2.0) < 1e-9, "120 BPM 4/4: 1 compás = 2 s", p.getBarSeconds());
        setParam (p, ParamIDs::sync, 1); p.processPendingConversion();
        check ((int) plain (p, ParamIDs::windowSync) == 3, "ms->Sync: 3000 ms -> el más cercano es 2 bars (índice 3)", plain (p, ParamIDs::windowSync));
        setParam (p, ParamIDs::windowSync, 2);  // 1 bar
        setParam (p, ParamIDs::sync, 0); p.processPendingConversion();
        check (std::abs (plain (p, ParamIDs::windowMs) - 2000.0f) < 0.5f, "Sync->ms: 1 bar -> 2000 ms", plain (p, ParamIDs::windowMs));
        setParam (p, ParamIDs::windowMs, 1850.0f);
        setParam (p, ParamIDs::sync, 1); p.processPendingConversion();
        check ((int) plain (p, ParamIDs::windowSync) == 2, "ms->Sync: 1850 ms -> 1 bar (el más cercano)", plain (p, ParamIDs::windowSync));
        setParam (p, ParamIDs::windowSync, 1);  // 1/2 bar
        setParam (p, ParamIDs::sync, 0); p.processPendingConversion();
        check (std::abs (plain (p, ParamIDs::windowMs) - 1000.0f) < 0.5f, "Sync->ms: 1/2 bar -> 1000 ms", plain (p, ParamIDs::windowMs));
        p.setPlayHead (nullptr);
    }
    {
        // Usa el compás del proyecto: 3/4 a 100 BPM -> 1 compás = 1800 ms.
        MeteringABProcessor p; FakePlayHead ph; ph.bpm = 100; ph.num = 3; ph.den = 4;
        p.setPlayHead (&ph); p.prepareToPlay (48000, 512); oneBlock (p);
        setParam (p, ParamIDs::sync, 1); p.processPendingConversion();
        setParam (p, ParamIDs::windowSync, 2);
        setParam (p, ParamIDs::sync, 0); p.processPendingConversion();
        check (std::abs (plain (p, ParamIDs::windowMs) - 1800.0f) < 0.5f, "3/4 a 100 BPM: 1 bar -> 1800 ms", plain (p, ParamIDs::windowMs));
        ph.num = 6; ph.den = 8; ph.bpm = 120; oneBlock (p);
        check (std::abs (p.getBarSeconds() - 1.5) < 1e-9, "6/8 a 120 BPM: 1 compás = 1,5 s", p.getBarSeconds());
        p.setPlayHead (nullptr);
    }
    {
        // Lectura con acentos una vez por compás (120 BPM): Sync 1 bar queda quieta.
        auto ripple = [] (bool sync)
        {
            MeteringABProcessor p; FakePlayHead ph; p.setPlayHead (&ph); juce::MidiBuffer m;
            setParam (p, ParamIDs::link, 12);
            if (sync) { setParam (p, ParamIDs::sync, 1); p.processPendingConversion(); setParam (p, ParamIDs::windowSync, 2); }
            else setParam (p, ParamIDs::windowMs, 2000.0f);
            p.prepareToPlay (48000, 512);
            juce::AudioBuffer<float> b (2, 512); long long t = 0; float lo = 1e9f, hi = -1e9f;
            for (int blk = 0; blk < (int) (20.0 * 48000 / 512); ++blk)
            {
                for (int i = 0; i < 512; ++i, ++t)
                {
                    const double posInBar = std::fmod ((double) t / 48000.0, 2.0);
                    const float env = posInBar < 0.15 ? 0.9f : 0.05f;    // "bombo" al principio del compás
                    const float v = env * (float) std::sin (2 * juce::MathConstants<double>::pi * 300.0 * (double) t / 48000.0);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
                p.processBlock (b, m);
                if (t > 10 * 48000) { lo = juce::jmin (lo, p.displayPre.load()); hi = juce::jmax (hi, p.displayPre.load()); }
            }
            p.setPlayHead (nullptr);
            return hi - lo;
        };
        const float rs = ripple (true), re = ripple (false);
        check (rs < 0.2f, "Sync 1 bar: la lectura casi no se mueve (dB de variación)", rs);
        check (re > 1.0f, "ms 2000 exponencial: la lectura oscila (dB de variación)", re);
    }
    {
        // El matcheo funciona igual en modo Sync.
        Chain c (13, -6.0f, 0);
        setParam (c.recv, ParamIDs::sync, 1); c.recv.processPendingConversion();
        setParam (c.recv, ParamIDs::windowSync, 2);
        c.run (8.0);
        check (std::abs (c.recv.displayGain.load() - 6.0f) < 0.1f, "Sync: FX -6 dB -> ganancia ~ +6 dB", c.recv.displayGain.load());
    }
    {
        // Abrir un proyecto guardado no convierte nada: respeta lo guardado.
        MeteringABProcessor a; FakePlayHead ph; a.setPlayHead (&ph); a.prepareToPlay (48000, 512); oneBlock (a);
        setParam (a, ParamIDs::sync, 1); a.processPendingConversion();
        setParam (a, ParamIDs::windowSync, 4);           // 4 bars
        setParam (a, ParamIDs::windowMs, 1234.0f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        MeteringABProcessor b2; FakePlayHead ph2; ph2.bpm = 87; b2.setPlayHead (&ph2); b2.prepareToPlay (48000, 512); oneBlock (b2);
        b2.setStateInformation (mb.getData(), (int) mb.getSize()); b2.processPendingConversion();
        check (plain (b2, ParamIDs::sync) > 0.5f && (int) plain (b2, ParamIDs::windowSync) == 4 && std::abs (plain (b2, ParamIDs::windowMs) - 1234.0f) < 0.5f,
               "Abrir proyecto: Sync, 4 bars y 1234 ms se respetan", plain (b2, ParamIDs::windowMs));
        a.setPlayHead (nullptr); b2.setPlayHead (nullptr);
    }
    {
        MeteringABProcessor q; auto* w = q.apvts.getParameter (ParamIDs::windowMs);
        const auto r = w->getNormalisableRange();
        check (std::abs (r.convertFrom0to1 (w->getValueForText ("1,85 s")) - 1850.0f) < 1.0f, "Escribir '1,85 s' en la ventana = 1850 ms", r.convertFrom0to1 (w->getValueForText ("1,85 s")));
        check (std::abs (r.convertFrom0to1 (w->getValueForText ("2500 ms")) - 2500.0f) < 1.0f, "Escribir '2500 ms' en la ventana = 2500 ms", r.convertFrom0to1 (w->getValueForText ("2500 ms")));
    }

    {
        // Medición automática de latencia.
        using State = LatencyMeasurer::State;
        auto measure = [] (int link, int latency, bool lowpass, bool unrelated, bool silent, double& similarity)
        {
            Chain c (link, -3.0f, latency);
            c.lowpass = lowpass; c.unrelated = unrelated; c.silent = silent;
            c.run (0.3);
            c.recv.startLatencyMeasurement();
            c.run (3.5);
            c.recv.processPendingConversion();
            similarity = c.recv.latencyMeasurer.getLastSimilarity();
            return std::make_pair (c.recv.latencyMeasurer.getState(), (int) plain (c.recv, ParamIDs::latency));
        };
        double sim = 0;
        auto r = measure (14, 300, false, false, false, sim);
        check (r.first == State::done && r.second == 300, "Medir: FX con 300 muestras -> 300", r.second);
        r = measure (15, 0, false, false, false, sim);
        check (r.first == State::done && r.second == 0, "Medir: FX sin latencia -> 0", r.second);
        r = measure (16, 1234, true, false, false, sim);
        check (r.first == State::done && r.second == 1234, "Medir: EQ que quita agudos + 1234 muestras -> 1234", r.second);
        r = measure (16, 20000, false, false, false, sim);
        check (r.first == State::done && r.second == 20000, "Medir: latencia grande 20000 -> 20000", r.second);
        r = measure (14, 300, false, true, false, sim);
        check (r.first == State::unclear && r.second == 0, "Medir: sonido sin relación -> avisa, no inventa", sim);
        r = measure (15, 300, false, false, true, sim);
        check (r.first == State::noSignal, "Medir: sin audio -> pide play", (double) (int) r.first);

        MeteringABProcessor lonely; setParam (lonely, ParamIDs::mode, 1); setParam (lonely, ParamIDs::link, 11);
        lonely.prepareToPlay (48000, 512); for (int k = 0; k < 100; ++k) oneBlock (lonely);
        lonely.startLatencyMeasurement();
        check (lonely.latencyMeasurer.getState() == State::noSend, "Medir sin Send -> avisa", 1);

        // De punta a punta: medir y después DELTA da silencio.
        Chain c (13, -3.0f, 777);
        c.run (0.3); c.recv.startLatencyMeasurement(); c.run (3.5); c.recv.processPendingConversion();
        setParam (c.recv, ParamIDs::delta, 1);
        c.run (8.0); auto out = c.run (1.0);
        check (rmsDb (out) < -60, "Medir 777 y DELTA -> silencio (dB)", rmsDb (out));
    }
    std::printf ("\n%s (%d fallas)\n", failures == 0 ? "TODO OK" : "HAY FALLAS", failures);
    return failures;
}
