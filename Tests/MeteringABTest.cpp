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
    bool saturate = false, pulsing = false;                    // para las capturas del osciloscopio
    long long sampleCount = 0;
    float lpState[2] = { 0.0f, 0.0f };

    Chain (int link, float gainDb, int latency) : fxLatency (latency), fxGain (juce::Decibels::decibelsToGain (gainDb))
    {
        setParam (send, ParamIDs::mode, 0); setParam (send, ParamIDs::link, (float) link);
        setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, (float) link);
        setParam (recv, ParamIDs::pre, 0);   // los tests escuchan POST FX salvo que digan otra cosa
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
                if (pulsing)   // golpes tipo batería cada 0,5 s
                {
                    const double tb = std::fmod ((double) sampleCount / 48000.0, 0.5);
                    x *= (float) (0.25 + 2.4 * std::exp (-tb / 0.09));
                }
                ++sampleCount;
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
                    if (saturate) y = std::tanh (3.0f * y) / 3.0f * 2.0f;                      // saturador que aplasta picos
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

// ---- Simulación del DELTA con distintos tipos de efecto ----
// "Efecto": procesa una muestra mono-por-canal con estado propio.
struct Fx
{
    virtual ~Fx() = default;
    virtual float process (int ch, float x) = 0;
};

struct Delay : Fx
{
    std::vector<float> buf[2]; size_t w[2] {};
    explicit Delay (int n) { buf[0].assign ((size_t) n + 1, 0); buf[1].assign ((size_t) n + 1, 0); }
    float process (int ch, float x) override
    {
        auto& b = buf[ch]; b[w[ch]] = x; size_t r = (w[ch] + 1) % b.size(); w[ch] = r; return b[r];
    }
};

// FIR genérico (para retardo fraccionario o EQ de fase lineal).
struct Fir : Fx
{
    std::vector<float> h, hist[2]; size_t pos[2] {};
    explicit Fir (std::vector<float> taps) : h (std::move (taps)) { hist[0].assign (h.size(), 0); hist[1].assign (h.size(), 0); }
    float process (int ch, float x) override
    {
        auto& s = hist[ch]; s[pos[ch]] = x; double y = 0; size_t idx = pos[ch];
        for (size_t k = 0; k < h.size(); ++k) { y += h[k] * s[idx]; idx = idx == 0 ? s.size() - 1 : idx - 1; }
        pos[ch] = (pos[ch] + 1) % s.size(); return (float) y;
    }
};

static std::vector<float> fractionalDelayTaps (double delay, int length)   // sinc con ventana centrada en el retardo
{
    std::vector<float> h ((size_t) length);
    const double halfSpan = std::min (delay, length - 1 - delay);
    for (int n = 0; n < length; ++n)
    {
        const double t = n - delay;
        const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
        const double u = t / (halfSpan + 1.0);
        const double win = std::abs (u) >= 1.0 ? 0.0 : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * u) + 0.08 * std::cos (2 * juce::MathConstants<double>::pi * u);
        h[(size_t) n] = (float) (sinc * win);
    }
    return h;
}

// Filtro pasa-bajos en 16 kHz para la señal de prueba: la música casi no
// tiene energía arriba de eso, y cualquier retardo "con coma" es impreciso
// pegado a la frecuencia máxima (24 kHz a 48 kHz de muestreo).
static std::vector<float> lowpassTaps()
{
    const int length = 101; const double fc = 16000.0 / 48000.0;
    std::vector<float> h ((size_t) length);
    for (int n = 0; n < length; ++n)
    {
        const double t = n - (length - 1) / 2.0;
        const double sinc = std::abs (t) < 1e-9 ? 2 * fc : std::sin (2 * juce::MathConstants<double>::pi * fc * t) / (juce::MathConstants<double>::pi * t);
        const double win = 0.42 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * n / (length - 1)) + 0.08 * std::cos (4 * juce::MathConstants<double>::pi * n / (length - 1));
        h[(size_t) n] = (float) (sinc * win);
    }
    return h;
}

struct Peaking : Fx   // EQ "común" (fase mínima): +4 dB en 3 kHz
{
    double b0, b1, b2, a1, a2, z1[2] {}, z2[2] {};
    Peaking()
    {
        const double fs = 48000, f0 = 3000, Q = 1.0, A = std::pow (10.0, 4.0 / 40.0);
        const double w = 2 * juce::MathConstants<double>::pi * f0 / fs, al = std::sin (w) / (2 * Q), c = std::cos (w);
        const double a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0;
    }
    float process (int ch, float x) override
    {
        const double y = b0 * x + z1[ch]; z1[ch] = b1 * x - a1 * y + z2[ch]; z2[ch] = b2 * x - a2 * y; return (float) y;
    }
};

struct Invert : Fx { float process (int, float x) override { return -x; } };

struct Result { double measured; bool inverted; double deltaDb; double idealDb; };

// idealFx: lo que el efecto "realmente cambia" alineado perfecto (o nullptr = no cambia nada).
static Result runDeltaCase (int link, std::unique_ptr<Fx> fx, std::unique_ptr<Fx> idealFx)
{
    MeteringABProcessor send, recv; juce::MidiBuffer m;
    setParam (send, ParamIDs::mode, 0); setParam (send, ParamIDs::link, (float) link);
    setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, (float) link);
    send.prepareToPlay (48000, 512); recv.prepareToPlay (48000, 512);

    juce::Random rng (3); float brown = 0;
    Fir bandLimit (lowpassTaps());
    juce::AudioBuffer<float> buf (2, 512), dry (2, 512);
    double sumDelta = 0, sumDry = 0, sumIdeal = 0;

    auto block = [&] (bool accumulate)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float white = rng.nextFloat() * 2 - 1;
            brown = 0.97f * brown + 0.03f * white;
            const float x = bandLimit.process (0, 0.3f * white + 2.0f * brown);   // ruido con graves y agudos (tipo música)
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        dry.makeCopyOf (buf);
        send.processBlock (buf, m);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, fx->process (ch, buf.getSample (ch, i)));
        recv.processBlock (buf, m);

        if (accumulate)
            for (int i = 0; i < 512; ++i)
            {
                const float d = dry.getSample (0, i);
                const float ideal = idealFx ? idealFx->process (0, d) - d : 0.0f;
                sumDelta += (double) buf.getSample (0, i) * buf.getSample (0, i);
                sumDry   += (double) d * d;
                sumIdeal += (double) ideal * ideal;
            }
        else if (idealFx)
            for (int i = 0; i < 512; ++i) idealFx->process (0, dry.getSample (0, i));
    };

    for (int b = 0; b < 30; ++b) block (false);
    recv.startLatencyMeasurement();
    for (int b = 0; b < 300; ++b) block (false);
    recv.processPendingConversion();
    const double measured = recv.apvts.getRawParameterValue (ParamIDs::latency)->load();
    const bool inv = recv.apvts.getRawParameterValue (ParamIDs::polarity)->load() > 0.5f;
    setParam (recv, ParamIDs::delta, 1);
    for (int b = 0; b < 600; ++b) block (false);       // que se asiente el matcheo
    for (int b = 0; b < 200; ++b) block (true);

    auto db = [] (double a, double b) { return a <= 0 ? -200.0 : 10 * std::log10 (a / b); };
    return { measured, inv, db (sumDelta, sumDry), db (sumIdeal, sumDry) };
}


int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc > 1) // Metering A/BTests <carpeta>: guarda capturas de la interfaz
    {
        const char* names[] = { "/send.png", "/receive.png", "/receive-long.png" };

        for (int shot = 0; shot < 3; ++shot)
        {
            std::unique_ptr<Chain> chain;
            std::unique_ptr<MeteringABProcessor> lone;
            MeteringABProcessor* a = nullptr;

            if (shot == 0)
            {
                lone = std::make_unique<MeteringABProcessor>(); a = lone.get();
                a->prepareToPlay (48000, 512);
                a->displayPre = -14.2f;
            }
            else
            {
                chain = std::make_unique<Chain> (40 + shot, 0.0f, 64);   // FX con 64 muestras de latencia
                setParam (chain->recv, ParamIDs::latency, 64.0f);
                chain->recv.latencyMeasurer.setState (LatencyMeasurer::State::done);
                chain->saturate = true; chain->pulsing = true;
                a = &chain->recv;
                a->apvts.state.setProperty ("scopeLength", shot == 1 ? 2 : 3, nullptr);   // 1 bar / 2 bars
                a->scopeLengthIndex = shot == 1 ? 2 : 3;
                if (shot == 2) a->apvts.state.setProperty ("scopeShowPre", false, nullptr);   // captura con PRE oculto
                chain->run (shot == 1 ? 3.3 : 5.3);
            }

            std::unique_ptr<juce::AudioProcessorEditor> ed (a->createEditor());
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            juce::File f (juce::String (argv[1]) + names[shot]);
            f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
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
        // Mostrar/ocultar ondas: se guarda con el proyecto.
        MeteringABProcessor a;
        a.apvts.state.setProperty ("scopeShowPost", false, nullptr);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        MeteringABProcessor b2; b2.setStateInformation (mb.getData(), (int) mb.getSize());
        check (! (bool) b2.apvts.state.getProperty ("scopeShowPost", true) && (bool) b2.apvts.state.getProperty ("scopeShowPre", true),
               "Osciloscopio: ocultar POST FX se guarda con el proyecto", 1);
    }
    {
        MeteringABProcessor fresh;
        check (fresh.apvts.getParameter (ParamIDs::pre)->getCurrentValueAsText() == "PRE FX", "Por defecto arranca en PRE FX", 1);
    }
    {
        // Latencia con decimales escrita a mano.
        MeteringABProcessor q; auto* p = q.apvts.getParameter (ParamIDs::latency);
        const auto r = p->getNormalisableRange();
        check (std::abs (r.convertFrom0to1 (p->getValueForText ("64,25")) - 64.25f) < 0.006f, "Escribir '64,25' en Latencia = 64.25 muestras", r.convertFrom0to1 (p->getValueForText ("64,25")));
        setParam (q, ParamIDs::latency, 64.5f);
        check (p->getCurrentValueAsText() == "64.50 muestras", "Latencia se muestra con decimales ('64.50 muestras')", 1);
        setParam (q, ParamIDs::latency, 128.0f);
        check (p->getCurrentValueAsText() == "128 muestras", "Latencia entera se muestra sin decimales", 1);
    }
    {
        // El matcheo es siempre automático, con un tope fijo de 24 dB.
        Chain c (5, -30.0f, 0);
        c.run (12.0);
        check (std::abs (c.recv.displayGain.load() - 24.0f) < 0.01f, "Matcheo siempre Auto: tope fijo de 24 dB", c.recv.displayGain.load());
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
        setParam (a, ParamIDs::latency, 128.37f); setParam (a, ParamIDs::polarity, 1);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        b2.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b2.apvts.getRawParameterValue (ParamIDs::latency)->load() - 128.37f) < 0.006f
                   && b2.apvts.getRawParameterValue (ParamIDs::polarity)->load() > 0.5f,
               "Estado guardado y recuperado (latencia 128.37 y polaridad)", b2.apvts.getRawParameterValue (ParamIDs::latency)->load());
    }
    {
        // El editor se puede crear y destruir sin problemas.
        MeteringABProcessor a;
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        check (ed != nullptr && ed->getWidth() == 640 && ed->getHeight() == 710, "Editor se crea (ancho px)", ed->getWidth());
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

    {
        // DELTA con latencias "con coma", polaridad y EQ común.
        auto r = runDeltaCase (21, std::make_unique<Delay> (64), nullptr);
        check (r.measured == 64.0 && r.deltaDb < -90, "DELTA: retardo entero 64 -> silencio (dB)", r.deltaDb);
        r = runDeltaCase (22, std::make_unique<Fir> (fractionalDelayTaps (64.5, 129)), nullptr);
        check (std::abs (r.measured - 64.5) < 0.02, "Medir: retardo 64,5 -> 64.5 (muestras)", r.measured);
        check (r.deltaDb < -40, "DELTA: retardo 64,5 -> silencio (dB)", r.deltaDb);
        r = runDeltaCase (23, std::make_unique<Fir> (fractionalDelayTaps (64.25, 129)), nullptr);
        check (std::abs (r.measured - 64.25) < 0.02, "Medir: retardo 64,25 -> 64.25 (muestras)", r.measured);
        check (r.deltaDb < -40, "DELTA: retardo 64,25 -> silencio (dB)", r.deltaDb);
        r = runDeltaCase (24, std::make_unique<Fir> (fractionalDelayTaps (1000.37, 2001)), nullptr);
        check (std::abs (r.measured - 1000.37) < 0.02 && r.deltaDb < -40, "DELTA: retardo 1000,37 -> medido y en silencio (dB)", r.deltaDb);
        r = runDeltaCase (25, std::make_unique<Invert>(), nullptr);
        check (r.inverted && r.deltaDb < -90, "Polaridad invertida: se detecta y DELTA da silencio (dB)", r.deltaDb);
        r = runDeltaCase (26, std::make_unique<Peaking>(), std::make_unique<Peaking>());
        check (! r.inverted && std::abs (r.deltaDb - r.idealDb) < 2.0, "EQ común: DELTA muestra lo que cambió el EQ (dB vs ideal)", r.deltaDb - r.idealDb);
    }

    {
        // Osciloscopio sincronizado: con la canción sonando, el barrido sigue la
        // posición de Logic dentro del compás (120 BPM 4/4, 1 compás = 4 negras).
        MeteringABProcessor send, recv; juce::MidiBuffer m;
        setParam (send, ParamIDs::link, 9); setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, 9);
        struct PlayingHead : juce::AudioPlayHead
        {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p; p.setBpm (120.0); p.setTimeSignature (TimeSignature { 4, 4 }); p.setIsPlaying (true);
                p.setPpqPosition (ppq); p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                p.setBarCount ((juce::int64) std::floor (ppq / 4.0));
                return p;
            }
        } head;
        send.setPlayHead (&head); recv.setPlayHead (&head);
        send.prepareToPlay (48000, 512); recv.prepareToPlay (48000, 512);
        juce::AudioBuffer<float> b (2, 512);
        auto step = [&] (double ppq) { head.ppq = ppq; b.clear(); b.setSample (0, 0, 0.5f); send.processBlock (b, m); recv.processBlock (b, m); };
        for (int k = 0; k < 20; ++k) step (k * 0.0533);              // conectar
        step (17.0);                                               // compás 5, tiempo 2 (1/4 del compás)
        const double expected = (1.0 + 512.0 * 120.0 / 60.0 / 48000.0) / 4.0 * MeteringABProcessor::scopeBins;
        check (std::abs (recv.scopeCurrentBin.load() - expected) <= 1.0, "Osciloscopio: el barrido sigue la canción (columna)", recv.scopeCurrentBin.load());
        recv.scopeLengthIndex = 3;                                 // 2 bars: el compás 6 (índice 5, impar) va en la 2a mitad
        step (21.0);
        const double expected2 = (4.0 + 1.0 + 512.0 * 120.0 / 60.0 / 48000.0) / 8.0 * MeteringABProcessor::scopeBins;
        check (std::abs (recv.scopeCurrentBin.load() - expected2) <= 1.0, "Osciloscopio 2 bars: el compás 6 va en la segunda mitad", recv.scopeCurrentBin.load());
        send.setPlayHead (nullptr); recv.setPlayHead (nullptr);
    }
    std::printf ("\n%s (%d fallas)\n", failures == 0 ? "TODO OK" : "HAY FALLAS", failures);
    return failures;
}
