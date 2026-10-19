#include <set>
#include "PluginProcessor.h"
#include "PluginEditor.h"
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

// El contenido del plugin (lo que antes era el editor entero).
static MeteringABPanel* panelOf (std::unique_ptr<juce::AudioProcessorEditor>& ed)
{
    return &dynamic_cast<MeteringABEditor*> (ed.get())->getPanel();
}

int failures = 0;
static void check (bool ok, const char* what, double value)
{
    std::printf ("%s  %-55s %.3f\n", ok ? "OK  " : "FAIL", what, value);
    if (! ok) ++failures;
}

// Simula a Logic en play: tempo 120, 4/4, la posición avanza con cada bloque.
struct ChainHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (120.0); p.setTimeSignature (TimeSignature { 4, 4 }); p.setIsPlaying (playing);
        p.setPpqPosition (ppq); p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        p.setBarCount ((juce::int64) std::floor (ppq / 4.0));
        return p;
    }
};

struct Chain
{
    ChainHead head;
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
        send.setPlayHead (&head); recv.setPlayHead (&head);
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
            head.ppq = (double) (sampleCount - 512) * 120.0 / 60.0 / 48000.0;
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

// ---- Efectos simulados para medir la latencia ----
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

struct Result { double measured; bool inverted; };

// Mide la latencia de un "efecto" con audio tipo música.
static Result runMeasureCase (int link, std::unique_ptr<Fx> fx)
{
    MeteringABProcessor send, recv; juce::MidiBuffer m;
    setParam (send, ParamIDs::mode, 0); setParam (send, ParamIDs::link, (float) link);
    setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, (float) link);
    send.prepareToPlay (48000, 512); recv.prepareToPlay (48000, 512);

    juce::Random rng (3); float brown = 0;
    Fir bandLimit (lowpassTaps());
    juce::AudioBuffer<float> buf (2, 512);

    auto block = [&]
    {
        for (int i = 0; i < 512; ++i)
        {
            const float white = rng.nextFloat() * 2 - 1;
            brown = 0.97f * brown + 0.03f * white;
            const float x = bandLimit.process (0, 0.3f * white + 2.0f * brown);   // ruido con graves y agudos (tipo música)
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        send.processBlock (buf, m);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, fx->process (ch, buf.getSample (ch, i)));
        recv.processBlock (buf, m);
    };

    for (int b = 0; b < 30; ++b) block();
    recv.startLatencyMeasurement();
    for (int b = 0; b < 300; ++b) block();
    recv.processPendingConversion();
    const double measured = recv.apvts.getRawParameterValue (ParamIDs::latency)->load();
    const bool inv = recv.latencyMeasurer.isInverted();
    return { measured, inv };
}


int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    MeteringABPanel::scopeOpenOnStart = true;   // casi todas las pruebas miran el osciloscopio

    if (argc > 2) // MeteringABTests <carpeta> <audio.raw>: tu audio (estéreo, float, 48 kHz) por el plugin real, 2 bars a 70 BPM
    {
        juce::MemoryBlock data;
        juce::File (argv[2]).loadFileAsData (data);
        const auto* samples = static_cast<const float*> (data.getData());
        int frames = (int) (data.getSize() / (2 * sizeof (float)));

        // Opcional: <procesado.raw> = lo que sale de los FX (si no, el mismo audio).
        juce::MemoryBlock processedData;
        if (argc > 3) juce::File (argv[3]).loadFileAsData (processedData);
        const auto* processed = argc > 3 ? static_cast<const float*> (processedData.getData()) : samples;
        if (argc > 3) frames = juce::jmin (frames, (int) (processedData.getSize() / (2 * sizeof (float))));

        struct Head70 : juce::AudioPlayHead
        {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p; p.setBpm (70.0); p.setTimeSignature (TimeSignature { 4, 4 }); p.setIsPlaying (true);
                p.setPpqPosition (ppq); p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                p.setBarCount ((juce::int64) std::floor (ppq / 4.0));
                return p;
            }
        } head;

        for (int style = 0; style < 3; ++style)
        {
            MeteringABProcessor send, recv; juce::MidiBuffer m;
            setParam (send, ParamIDs::link, 15); setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, 15);
            setParam (recv, ParamIDs::pre, 0);
            recv.apvts.state.setProperty ("scopeOpen", true, nullptr);
            recv.apvts.state.setProperty ("scopeLength", 3, nullptr); recv.scopeLengthIndex = 3;     // 2 bars
            recv.apvts.state.setProperty ("scopeShowPre", false, nullptr);
            recv.apvts.state.setProperty ("scopeLink", false, nullptr);
            recv.apvts.state.setProperty ("scopeStylePost", style == 0 ? 0 : style == 1 ? 2 : 1, nullptr);
            send.setPlayHead (&head); recv.setPlayHead (&head);
            send.prepareToPlay (48000, 512); recv.prepareToPlay (48000, 512);
            head.ppq = 0.0;
            juce::AudioBuffer<float> b (2, 512);

            for (int start = 0; start + 512 <= frames; start += 512)
            {
                for (int i = 0; i < 512; ++i)
                {
                    b.setSample (0, i, samples[2 * (start + i)]);
                    b.setSample (1, i, samples[2 * (start + i) + 1]);
                }
                head.ppq = start * (70.0 / 60.0) / 48000.0;
                send.processBlock (b, m);
                for (int i = 0; i < 512; ++i)
                {
                    b.setSample (0, i, processed[2 * (start + i)]);
                    b.setSample (1, i, processed[2 * (start + i) + 1]);
                }
                recv.processBlock (b, m);
            }

            std::unique_ptr<juce::AudioProcessorEditor> ed (recv.createEditor());
            panelOf (ed)->refreshNow();
            auto& scope = panelOf (ed)->getScopeForTests();
            if (const char* size = std::getenv ("SCOPE_SIZE"))   // para mockups: SCOPE_SIZE=ancho x alto
            {
                const auto parts = juce::StringArray::fromTokens (size, "x", "");
                const_cast<ScopeView&> (scope).setSize (parts[0].getIntValue(), parts[1].getIntValue());
            }
            auto img = const_cast<ScopeView&> (scope).createComponentSnapshot (scope.getLocalBounds(), true, 2.0f);
            const char* names[] = { "/audio-contorno.png", "/audio-lineas.png", "/audio-detallado.png" };
            juce::File f (juce::String (argv[1]) + names[style]);
            f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);

            // Freeze + zoom (x8 y x40) sobre la misma parte.
            auto& sc = panelOf (ed)->getScopeForTests();
            panelOf (ed)->getFreezeButtonForTests().onClick();
            const char* base[] = { "/audio-contorno", "/audio-lineas", "/audio-detallado" };
            const double zooms[][2] = { { 0.36, 0.125 }, { 0.392, 0.025 }, { 0.3995, 0.0 } };   // x8, x40 y el máximo
            for (int z = 0; z < 3; ++z)
            {
                sc.setView (zooms[z][0], zooms[z][1]);
                auto zimg = sc.createComponentSnapshot (sc.getLocalBounds(), true, 2.0f);
                juce::File zf (juce::String (argv[1]) + base[style] + (z == 0 ? "-zoom8.png" : z == 1 ? "-zoom40.png" : "-zoommax.png"));
                zf.deleteFile(); juce::FileOutputStream zos (zf); juce::PNGImageFormat().writeImageToStream (zimg, zos);
            }
            if (style == 2)                             // regla de nivel: línea fija en -6 dB y el mouse en -14 dB
            {
                sc.resetZoom();
                sc.setMarkerDb (-6.0f);
                const auto pl = sc.plotArea();
                const float y = pl.getCentreY() - juce::Decibels::decibelsToGain (-14.0f) * pl.getHeight() * 0.5f;
                juce::MouseEvent mv (juce::Desktop::getInstance().getMainMouseSource(), { pl.getCentreX(), y }, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &sc, &sc, juce::Time::getCurrentTime(), { pl.getCentreX(), y }, juce::Time::getCurrentTime(), 0, false);
                sc.mouseMove (mv);
                panelOf (ed)->getGainLineButtonForTests().onClick();   // + la línea de la ganancia aplicada
                panelOf (ed)->refreshNow();
                auto rimg = sc.createComponentSnapshot (sc.getLocalBounds(), true, 2.0f);
                juce::File rf (juce::String (argv[1]) + "/audio-regla.png");
                rf.deleteFile(); juce::FileOutputStream ros (rf); juce::PNGImageFormat().writeImageToStream (rimg, ros);
                sc.setMarkerDb (std::nullopt);
            }
            if (style == 0)                             // la interfaz entera, con Freeze y zoom
            {
                sc.setView (0.36, 0.125);
                auto* panel = panelOf (ed);
                auto pimg = panel->createComponentSnapshot (panel->getLocalBounds(), true, 2.0f);
                juce::File pf (juce::String (argv[1]) + "/audio-panel-freeze.png");
                pf.deleteFile(); juce::FileOutputStream pos (pf); juce::PNGImageFormat().writeImageToStream (pimg, pos);

                // PRE + POST: las dos juntas (PRE Outline, POST Lines only).
                sc.resetZoom();
                panel->getCompareButtonForTests().onClick();
                auto cimg = panel->createComponentSnapshot (panel->getLocalBounds(), true, 2.0f);
                juce::File cf (juce::String (argv[1]) + "/audio-compare.png");
                cf.deleteFile(); juce::FileOutputStream cos (cf); juce::PNGImageFormat().writeImageToStream (cimg, cos);
                {
                    sc.setView (0.0, 0.5);                 // un compás, como se ve en Logic
                    auto bc = sc.createComponentSnapshot (sc.getLocalBounds(), true, 2.0f);
                    juce::File bf (juce::String (argv[1]) + "/audio-compare-1bar.png");
                    bf.deleteFile(); juce::FileOutputStream bos (bf); juce::PNGImageFormat().writeImageToStream (bc, bos);
                }
                for (int z = 0; z < 2; ++z)             // ...y con zoom (x8 y x40)
                {
                    sc.setView (zooms[z][0], zooms[z][1]);
                    auto zc = sc.createComponentSnapshot (sc.getLocalBounds(), true, 2.0f);
                    juce::File zf (juce::String (argv[1]) + (z == 0 ? "/audio-compare-zoom8.png" : "/audio-compare-zoom40.png"));
                    zf.deleteFile(); juce::FileOutputStream zos (zf); juce::PNGImageFormat().writeImageToStream (zc, zos);
                }
                sc.resetZoom();
                panel->getCompareButtonForTests().onClick();
            }
            send.setPlayHead (nullptr); recv.setPlayHead (nullptr);
        }
        return 0;
    }

    if (argc > 1) // Metering A/BTests <carpeta>: guarda capturas de la interfaz
    {
        const char* names[] = { "/send.png", "/receive.png", "/receive-long.png", "/receive-closed.png",
                                "/estilo-detallado.png", "/estilo-lineas.png", "/estilo-contorno.png", "/link-cambio.png" };

        for (int shot = 0; shot < 8; ++shot)
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
                chain = std::make_unique<Chain> (40 + shot, 0.0f, 64);
                chain->recv.apvts.state.setProperty ("scopeOpen", shot != 3, nullptr);   // capturas con el osciloscopio abierto (salvo la 3)   // FX con 64 muestras de latencia
                setParam (chain->recv, ParamIDs::latency, 64.0f);
                chain->recv.latencyMeasurer.setState (LatencyMeasurer::State::done);
                chain->saturate = true; chain->pulsing = true;
                a = &chain->recv;
                a->apvts.state.setProperty ("scopeLength", shot == 2 ? 3 : 2, nullptr);   // 1 bar / 2 bars
                a->scopeLengthIndex = shot == 2 ? 3 : 2;
                if (shot == 2) a->apvts.state.setProperty ("scopeShowPre", false, nullptr);   // captura con solo POST
                if (shot >= 4)   // estilos, en 1/2 bar para ver el detalle
                {
                    a->apvts.state.setProperty ("scopeLength", 1, nullptr); a->scopeLengthIndex = 1;
                    a->apvts.state.setProperty ("scopeStyle", shot == 4 ? 1 : shot == 5 ? 2 : 0, nullptr);
                    a->apvts.state.setProperty ("scopePalette", shot == 4 ? 1 : shot == 5 ? 2 : 0, nullptr);
                    if (shot == 5) a->apvts.state.setProperty ("scopeLink", false, nullptr);   // LINK apagado en la captura
                    if (shot == 6) a->apvts.state.setProperty ("scopeChannels", 3, nullptr);   // L y R separados
                    if (shot == 6) a->apvts.state.setProperty ("scopeStylePost", 2, nullptr);  // PRE contorno, POST solo líneas
                }
                if (shot == 7)   // LINK: escuchando POST, cambio a PRE en la mitad del compás
                {
                    a->apvts.state.setProperty ("scopeLength", 2, nullptr); a->scopeLengthIndex = 2;
                    a->apvts.state.setProperty ("scopeShowPre", true, nullptr);
                    a->apvts.state.setProperty ("scopeShowPost", false, nullptr);
                    chain->run (3.0); setParam (*a, ParamIDs::pre, 1); chain->run (0.5);
                }
                else
                    chain->run (shot == 2 ? 5.3 : 3.3);
            }

            std::unique_ptr<juce::AudioProcessorEditor> ed (a->createEditor());
            if (shot == 1)   // simula tocar el botón de escucha: pasa a PRE FX y el osciloscopio lo sigue
            {
                auto* e = panelOf (ed);
                e->refreshNow(); setParam (*a, ParamIDs::pre, 1); e->refreshNow();
            }
            auto save = [&] (juce::Component& comp, const char* name)
            {
                auto img = comp.createComponentSnapshot (comp.getLocalBounds(), true, 2.0f);
                juce::File f (juce::String (argv[1]) + name);
                f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            };
            save (*ed, names[shot]);

            if (shot == 2)   // plugin escalado al 75% y al 150%
            {
                auto* outer = dynamic_cast<MeteringABEditor*> (ed.get());
                for (float sc : { 0.75f, 1.5f })
                {
                    outer->setUiScale (sc);
                    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                    juce::File f (juce::String (argv[1]) + (sc < 1.0f ? "/escala-75.png" : "/escala-150.png"));
                    f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
                }
                outer->setUiScale (1.0f);
            }

            if (shot == 7)   // LINK con su propia forma (Solo líneas), PRE/POST siguen en Contorno
            {
                a->apvts.state.setProperty ("scopeStyleLink", 2, nullptr);
                std::unique_ptr<juce::AudioProcessorEditor> ed2 (a->createEditor());
                panelOf (ed2)->refreshNow();
                save (*ed2, "/link-forma.png");
            }

            if (shot == 3)   // ventana en modo Sync (compases)
            {
                chain->head.playing = false; chain->run (0.1);          // pausa: medidores en -∞
                panelOf (ed)->refreshNow();
                save (*ed, "/receive-pausa.png");
                chain->head.playing = true;

                setParam (*a, ParamIDs::sync, 1);
                panelOf (ed)->refreshNow();
                save (*ed, "/receive-sync.png");

                MeteringABPanel::scopeOpenOnStart = false;            // así se abre el plugin
                std::unique_ptr<juce::AudioProcessorEditor> ed3 (a->createEditor());
                panelOf (ed3)->refreshNow();
                save (*ed3, "/receive-abre.png");
                MeteringABPanel::scopeOpenOnStart = true;
            }

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

        setParam (c.recv, ParamIDs::pre, 1);               // PRE FX
        out = c.run (0.5, &dry);
        check (std::abs (rmsDb (out) - rmsDb (dry)) < 0.01, "PRE FX = audio original del Send", rmsDb (out) - rmsDb (dry));

        setParam (c.recv, ParamIDs::pre, 0);               // POST FX
        c.run (0.5, &dry); out = c.run (0.02, &dry);
        check (std::abs (rmsDb (out) - rmsDb (dry)) < 0.2, "POST FX = procesado al mismo nivel", rmsDb (out) - rmsDb (dry));
        check (c.recv.apvts.getParameter (ParamIDs::pre)->getCurrentValueAsText() == "POST FX", "El parámetro se muestra como 'POST FX'", 1);
    }
    {
        // Pausa en Logic: los medidores quedan en -∞; al volver el play miden de
        // nuevo y la ganancia aplicada no salta (no se escucha un golpe de volumen).
        Chain c (31, -6.0f, 0);
        c.run (12.0);
        {
            // En la pantalla: al poner pausa, Original y Procesado bajan de a poco hasta -∞;
            // la ganancia aplicada queda como estaba.
            std::unique_ptr<juce::AudioProcessorEditor> ed (c.recv.createEditor());
            auto* e = panelOf (ed);
            for (int k = 0; k < 10; ++k) e->refreshNow();
            const float playing = e->getShownPreForTests();
            c.head.playing = false;
            c.run (0.1);
            e->refreshNow();
            const float firstFrame = e->getShownPreForTests();
            check (firstFrame < playing && firstFrame > playing - 5.0f, "Pausa: Original baja de a poco, no de golpe (dB)", firstFrame);
            // Original y Procesado llegan juntos a -∞ (aunque arranquen en valores distintos).
            int preOff = -1, postOff = -1;
            for (int k = 0; k < 90 && (preOff < 0 || postOff < 0); ++k)
            {
                e->refreshNow();
                if (preOff < 0 && e->getShownPreForTests() <= -119.0f) preOff = k;
                if (postOff < 0 && e->getShownPostForTests() <= -119.0f) postOff = k;
            }
            check (preOff >= 0 && preOff == postOff, "Pausa: Original y Procesado llegan juntos a -inf (cuadros de diferencia)", postOff - preOff);
            for (int k = 0; k < 90; ++k) e->refreshNow();            // 3 segundos
            check (e->getShownPreForTests() <= -119.0f, "Pausa: al rato queda en -inf", e->getShownPreForTests());
            check (e->isGainShownForTests(), "Pausa: la ganancia aplicada queda a la vista", 1);
        }
        c.run (0.3);
        check (c.recv.displayPre.load() <= -119.0f && c.recv.displayPost.load() <= -119.0f && std::abs (c.recv.displayGain.load() - 6.0f) < 0.3f,
               "Pausa: medidores en cero y la ganancia se mantiene (dB)", c.recv.displayGain.load());
        c.head.playing = true;
        juce::AudioBuffer<float> dry; c.run (0.05, &dry); auto out = c.run (512.0 / 48000.0, &dry);
        check (std::abs (rmsDb (out) - rmsDb (dry)) < 0.3, "Al volver el play el nivel sigue igualado (dB de diferencia)", rmsDb (out) - rmsDb (dry));
        c.run (0.5);
        check (c.recv.displayPre.load() > -60.0f && c.recv.displayGainValid.load() && std::abs (c.recv.displayGain.load() - 6.0f) < 0.3f,
               "Al volver el play los medidores miden de nuevo (ganancia dB)", c.recv.displayGain.load());

        // Clic sobre los números: medidores a cero y vuelven a medir desde ahí.
        c.run (5.0);
        const float before = c.recv.displayPre.load();
        MeteringABProcessor& r = c.recv;
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.createEditor());
        auto* e = panelOf (ed);
        e->clickMetersForTests();
        check (e->getShownPreForTests() <= -119.0f && ! e->isGainShownForTests(), "Clic en los números: se borran enseguida (también la ganancia)", e->getShownPreForTests());
        c.run (512.0 / 48000.0);
        check (c.recv.displayPre.load() < before - 10.0f, "Clic en los números: el medidor arranca de cero (dB)", c.recv.displayPre.load());
        c.run (0.3); e->refreshNow();
        check (e->isGainShownForTests(), "Clic en los números: la ganancia vuelve a aparecer al medir de nuevo", 1);
    }
    {
        Chain c (2, 4.5f, 0);
        c.run (12.0);
        check (std::abs (c.recv.displayGain.load() + 4.5f) < 0.1f, "Auto (LUFS): FX +4.5 dB -> ganancia ~ -4.5 dB", c.recv.displayGain.load());
        check (c.recv.apvts.getParameter ("measure") == nullptr && c.recv.apvts.getParameter ("hold") == nullptr
               && c.recv.apvts.getParameter ("polarity") == nullptr,
               "Sin selector RMS/LUFS ni Congelar (siempre LUFS)", 1);
    }
    {
        Chain c (3, -3.0f, 300);
        setParam (c.recv, ParamIDs::latency, 300);
        setParam (c.recv, ParamIDs::pre, 1);
        juce::AudioBuffer<float> prevDry, dry;
        c.run (2.0, &prevDry);
        auto out = c.run (512.0 / 48000.0, &dry);
        double maxErr = 0;                                 // PRE FX = lo que mandó el Send hace 300 muestras
        for (int i = 0; i < 512; ++i)
        {
            const float expected = i >= 300 ? dry.getSample (0, i - 300) : prevDry.getSample (0, 512 - 300 + i);
            maxErr = juce::jmax (maxErr, (double) std::abs (out.getSample (0, i) - expected));
        }
        check (maxErr < 1.0e-6, "Latencia 300: PRE FX alineado con el procesado (error)", maxErr);
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
        // Receive: el osciloscopio está siempre (aunque un proyecto viejo lo tenga cerrado).
        MeteringABProcessor a; a.apvts.state.setProperty ("scopeOpen", false, nullptr); setParam (a, ParamIDs::mode, 1);
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        check (panelOf (ed)->getWidth() == MeteringABPanel::receiveWidth && panelOf (ed)->getHeight() == MeteringABPanel::receiveHeight
               && panelOf (ed)->getScopeForTests().isVisible(),
               "Receive: osciloscopio siempre visible (alto px)", ed->getHeight());
        MeteringABProcessor sendP;
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (sendP.createEditor());
        check (panelOf (ed2)->getHeight() == MeteringABEditor::sendHeight && ! panelOf (ed2)->getScopeForTests().isVisible(),
               "Modo Send: sin sección de osciloscopio (alto px)", panelOf (ed2)->getHeight());
    }
    {
        // Plugin escalable: todo se agranda en proporción y el tamaño se guarda.
        MeteringABProcessor a; setParam (a, ParamIDs::mode, 1);
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        auto* outer = dynamic_cast<MeteringABEditor*> (ed.get());
        check (ed->isResizable() && std::abs (panelOf (ed)->getScopeForTests().getHeight() * outer->getUiScale() - 468.3f) < 1.0f,
               "Escalable: por defecto el osciloscopio mide 468 px de alto (plugin de 522)", panelOf (ed)->getScopeForTests().getWidth() * outer->getUiScale());
        outer->setUiScale (1.5f);
        check (ed->getWidth() == juce::roundToInt (MeteringABPanel::receiveWidth * 1.5) && ed->getHeight() == juce::roundToInt (MeteringABPanel::receiveHeight * 1.5),
               "Escalable: al 150% todo crece en proporción (alto px)", ed->getHeight());
        check (std::abs (outer->getPanel().getTransform().mat00 - 1.5f) < 1.0e-4f, "Escalable: el contenido se dibuja al 150%", outer->getPanel().getTransform().mat00);
        {
            // Clic derecho en la esquina de escalar: menú "Reset size".
            auto* corner = outer->resizableCorner.get();
            const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
            juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(), { 3.0f, 3.0f }, right, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 corner, corner, juce::Time::getCurrentTime(), { 3.0f, 3.0f }, juce::Time::getCurrentTime(), 1, false);
            if (corner != nullptr) outer->mouseDown (ev);
            check (corner != nullptr && outer->sizeMenuOpenedForTests == 1, "Clic derecho en la esquina: menú Reset size", outer->sizeMenuOpenedForTests);
            outer->setUiScale (MeteringABEditor::defaultScale);    // lo que hace "Reset size"
            check (std::abs (outer->getUiScale() - MeteringABEditor::defaultScale) < 1.0e-4f, "Reset size: vuelve al tamaño por defecto", outer->getUiScale());
        }
        outer->setUiScale (1.5f);
        setParam (a, ParamIDs::mode, 0); panelOf (ed)->refreshNow();
        check (ed->getWidth() == juce::roundToInt (MeteringABPanel::sendWidth * 1.5) && ed->getHeight() == juce::roundToInt (MeteringABEditor::sendHeight * 1.5)
               && ! ed->isResizable(),
               "Send: la misma escala que el Receive, sin esquina para escalar (alto px)", ed->getHeight());
        setParam (a, ParamIDs::mode, 1); panelOf (ed)->refreshNow();
        check (ed->isResizable() && ed->getWidth() == juce::roundToInt (MeteringABPanel::receiveWidth * 1.5),
               "Volver a Receive: recupera su escala y la esquina", ed->getWidth());
        ed->setSize (MeteringABPanel::receiveWidth, MeteringABPanel::receiveHeight);   // como arrastrar la esquina
        check (std::abs (outer->getUiScale() - 1.0f) < 1.0e-3f, "Escalable: arrastrar la esquina cambia la escala", outer->getUiScale());
        outer->setUiScale (5.0f);
        check (std::abs (outer->getUiScale() - MeteringABEditor::maxScale) < 1.0e-4f, "Escalable: no pasa del 150%", outer->getUiScale());
        outer->setUiScale (1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (a.createEditor());
        check (ed2->getWidth() == MeteringABPanel::receiveWidth, "Escalable: al reabrir el plugin recuerda el tamaño (ancho px)", ed2->getWidth());
    }
    {
        // Botón Scope (al pie de la columna gris): el plugin se abre con el osciloscopio cerrado.
        MeteringABProcessor a; setParam (a, ParamIDs::mode, 1);
        MeteringABPanel::scopeOpenOnStart = false;
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        auto* p = panelOf (ed);
        check (! p->isScopeOpenForTests() && ! p->getScopeForTests().isVisible() && p->getWidth() == MeteringABPanel::sideWidth
               && ! p->getScopeToggleForTests().getToggleState(),
               "Scope: el plugin se abre con el osciloscopio cerrado (solo la columna gris)", p->getWidth());
        p->getScopeToggleForTests().onClick();
        check (p->isScopeOpenForTests() && p->getScopeForTests().isVisible() && p->getWidth() == MeteringABPanel::receiveWidth,
               "Scope: al abrirlo vuelve el osciloscopio (ancho)", p->getWidth());
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (a.createEditor());
        check (! panelOf (ed2)->isScopeOpenForTests(), "Scope: al reabrir el plugin vuelve a estar cerrado", 1);
        p->getScopeToggleForTests().onClick();
        check (! p->isScopeOpenForTests() && p->getWidth() == MeteringABPanel::sideWidth,
               "Scope: al cerrarlo queda solo la columna gris (ancho)", p->getWidth());
        MeteringABPanel::scopeOpenOnStart = true;
    }
    {
        // Columna gris a la izquierda y el osciloscopio a la derecha (aunque un
        // proyecto viejo tenga guardado otro modo).
        MeteringABProcessor a; setParam (a, ParamIDs::mode, 1);
        a.apvts.state.setProperty ("scopeWide", true, nullptr);
        a.apvts.state.setProperty ("scopeOpen", true, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        auto* e = panelOf (ed);
        check (e->getWidth() == MeteringABPanel::receiveWidth && e->getScopeForTests().getX() >= MeteringABPanel::sideWidth
               && e->getScopeForTests().getHeight() == MeteringABPanel::scopeHeight,
               "Osciloscopio a la derecha de la columna gris, a toda altura (x px)", e->getScopeForTests().getX());
    }
    {
        // Osciloscopio linkeado a la escucha: al alternar PRE/POST FX muestra solo esa onda.
        MeteringABProcessor a; setParam (a, ParamIDs::mode, 1); setParam (a, ParamIDs::pre, 0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        auto* editor = panelOf (ed);
        auto pump = [editor] { editor->refreshNow(); };     // lo mismo que hace el timer de la interfaz
        pump();
        const bool bothAtStart = (bool) a.apvts.state.getProperty ("scopeShowPre", true) && (bool) a.apvts.state.getProperty ("scopeShowPost", true);
        setParam (a, ParamIDs::pre, 1); pump();
        const bool onlyPre = (bool) a.apvts.state.getProperty ("scopeShowPre", false) && ! (bool) a.apvts.state.getProperty ("scopeShowPost", true);
        setParam (a, ParamIDs::pre, 0); pump();
        const bool onlyPost = ! (bool) a.apvts.state.getProperty ("scopeShowPre", true) && (bool) a.apvts.state.getProperty ("scopeShowPost", false);
        check (bothAtStart && onlyPre && onlyPost, "LINK prendido: el osciloscopio sigue a la escucha", 1);

        // Sin botón LINK: aunque un proyecto viejo lo tenga apagado, siempre sigue a la escucha.
        MeteringABProcessor c; setParam (c, ParamIDs::mode, 1); setParam (c, ParamIDs::pre, 0);
        c.apvts.state.setProperty ("scopeLink", false, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (c.createEditor());
        auto* e2 = panelOf (ed2);
        e2->refreshNow(); setParam (c, ParamIDs::pre, 1); e2->refreshNow();
        const bool followsPre = (bool) c.apvts.state.getProperty ("scopeShowPre", false) && ! (bool) c.apvts.state.getProperty ("scopeShowPost", true);
        check (e2->isScopeLinkedForTests() && followsPre, "Siempre sigue a la escucha (aunque el proyecto tuviera LINK apagado)", 1);
    }
    {
        // Forma separada para PRE y POST; un proyecto viejo con una sola forma la usa para las dos.
        MeteringABProcessor a; setParam (a, ParamIDs::mode, 1);
        a.apvts.state.setProperty ("scopeStyle", 1, nullptr);
        a.apvts.state.setProperty ("scopeStylePost", 2, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        auto* e = panelOf (ed);
        check (e->getScopeForTests().getStylePre() == ScopeView::Style::contour && e->getScopeForTests().getStylePost() == ScopeView::Style::lines,
               "Formas fijas: PRE Outline y POST Lines only (aunque el proyecto tuviera otras)", 1);
    }
    {
        // Ya no hay forma aparte para LINK: cada onda usa la suya.
        MeteringABProcessor a; setParam (a, ParamIDs::mode, 1);
        a.apvts.state.setProperty ("scopeStylePre", 0, nullptr);
        a.apvts.state.setProperty ("scopeStylePost", 0, nullptr);
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
            check (panelOf (ed)->getScopeForTests().getLinkStyle() == ScopeView::Style::detailed, "Siguiendo la escucha: Detallado por defecto", 1);
        }
        a.apvts.state.setProperty ("scopeStyleLink", 2, nullptr);   // de un proyecto viejo: ya no se usa
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        const auto& sc = panelOf (ed)->getScopeForTests();
        check (sc.getLinkStyle() == ScopeView::Style::detailed && sc.getStylePre() == ScopeView::Style::contour,
               "Siguiendo la escucha: siempre Detallado; PRE / POST a mano con su forma", 1);
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
        check (p->getCurrentValueAsText() == "64.50 samples", "Latencia se muestra con decimales ('64.50 muestras')", 1);
        setParam (q, ParamIDs::latency, 128.0f);
        check (p->getCurrentValueAsText() == "128 samples", "Latencia entera se muestra sin decimales", 1);
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
        setParam (a, ParamIDs::latency, 128.37f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        b2.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b2.apvts.getRawParameterValue (ParamIDs::latency)->load() - 128.37f) < 0.006f,
               "Estado guardado y recuperado (latencia 128.37)", b2.apvts.getRawParameterValue (ParamIDs::latency)->load());
    }
    {
        // El editor se puede crear y destruir sin problemas (Receive, osciloscopio abierto).
        MeteringABProcessor a; setParam (a, ParamIDs::mode, 1);
        a.apvts.state.setProperty ("scopeOpen", true, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        check (ed != nullptr && ed->getWidth() == juce::roundToInt (MeteringABPanel::receiveWidth * MeteringABEditor::defaultScale),
               "Editor Receive: tamaño por defecto (ancho px)", ed->getWidth());
        const auto& scopeBox = panelOf (ed)->getScopeForTests();
        check (std::abs ((double) scopeBox.getWidth() / scopeBox.getHeight() - 1.85) < 0.01,
               "Osciloscopio con proporción 1,85 : 1", (double) scopeBox.getWidth() / scopeBox.getHeight());
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

        // De punta a punta con la cadena: medir 777.
        Chain c (13, -3.0f, 777);
        c.run (0.3); c.recv.startLatencyMeasurement(); c.run (3.5); c.recv.processPendingConversion();
        const double got = c.recv.apvts.getRawParameterValue (ParamIDs::latency)->load();
        check (std::abs (got - 777.0) < 0.01, "Medir en la cadena: 777 -> 777 (muestras)", got);
    }

    {
        // Medir latencias "con coma" y polaridad.
        auto r = runMeasureCase (21, std::make_unique<Delay> (64));
        check (r.measured == 64.0 && ! r.inverted, "Medir: retardo entero 64 -> 64 (muestras)", r.measured);
        r = runMeasureCase (22, std::make_unique<Fir> (fractionalDelayTaps (64.5, 129)));
        check (std::abs (r.measured - 64.5) < 0.02, "Medir: retardo 64,5 -> 64.5 (muestras)", r.measured);
        r = runMeasureCase (23, std::make_unique<Fir> (fractionalDelayTaps (64.25, 129)));
        check (std::abs (r.measured - 64.25) < 0.02, "Medir: retardo 64,25 -> 64.25 (muestras)", r.measured);
        r = runMeasureCase (24, std::make_unique<Fir> (fractionalDelayTaps (1000.37, 2001)));
        check (std::abs (r.measured - 1000.37) < 0.02, "Medir: retardo 1000,37 -> 1000.37 (muestras)", r.measured);
        r = runMeasureCase (25, std::make_unique<Invert>());
        check (r.inverted, "Medir: polaridad invertida se detecta", r.measured);
        r = runMeasureCase (26, std::make_unique<Peaking>());
        check (! r.inverted && std::abs (r.measured) < 0.5, "Medir: un EQ no se confunde con latencia (muestras)", r.measured);
    }

    {
        // Osciloscopio sincronizado: con la canción sonando, el barrido sigue la
        // posición de Logic dentro del compás (120 BPM 4/4, 1 compás = 4 negras).
        MeteringABProcessor send, recv; juce::MidiBuffer m;
        setParam (send, ParamIDs::link, 9); setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, 9);
        struct PlayingHead : juce::AudioPlayHead
        {
            double ppq = 0.0; bool playing = true;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p; p.setBpm (120.0); p.setTimeSignature (TimeSignature { 4, 4 }); p.setIsPlaying (playing);
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

        // En pausa: el cabezal no avanza, sigue al cursor y no dibuja nada nuevo.
        recv.scopeLengthIndex = 2; step (17.0);
        head.playing = false;
        const int binBefore = recv.scopeCurrentBin.load();
        const float maxBefore = recv.scopePostMax[(size_t) binBefore + 5].load();
        for (int k = 0; k < 50; ++k) { head.ppq = 17.0; b.clear(); b.setSample (0, 0, 0.9f); b.setSample (0, 300, 0.9f); send.processBlock (b, m); recv.processBlock (b, m); }
        const bool stayed = recv.scopeCurrentBin.load() == (int) (1.0 / 4.0 * MeteringABProcessor::scopeBins);
        const bool untouched = std::abs (recv.scopePostMax[(size_t) binBefore + 5].load() - maxBefore) < 1.0e-9f;
        check (stayed && untouched && ! recv.scopeIsRunning.load(), "Osciloscopio en pausa: quieto y sin dibujar", recv.scopeCurrentBin.load());
        head.ppq = 25.0; step (25.0); head.playing = false; recv.processBlock (b, m);
        check (recv.scopeCurrentBin.load() == (int) (1.0 / 4.0 * MeteringABProcessor::scopeBins), "Osciloscopio en pausa: sigue al cursor si lo movés", recv.scopeCurrentBin.load());
        send.setPlayHead (nullptr); recv.setPlayHead (nullptr);
    }

    {
        // Como en Logic (compensación de latencia): FX con 4400 muestras, Logic lee
        // el audio de la pista 4400 muestras adelantado y arranca 4400 muestras
        // antes del play; los dos plugins reciben la misma posición. Golpe en el tiempo 2.
        MeteringABProcessor send, recv; juce::MidiBuffer m;
        setParam (send, ParamIDs::link, 15); setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, 15);
        setParam (recv, ParamIDs::latency, 4400.0f);
        struct Head : juce::AudioPlayHead
        {
            double ppq = 0.0; bool playing = false;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p; p.setBpm (120.0); p.setTimeSignature (TimeSignature { 4, 4 }); p.setIsPlaying (playing);
                p.setPpqPosition (ppq); p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                p.setBarCount ((juce::int64) std::floor (ppq / 4.0));
                return p;
            }
        } head;
        send.setPlayHead (&head); recv.setPlayHead (&head);
        send.prepareToPlay (48000, 512); recv.prepareToPlay (48000, 512);
        const double qps = 120.0 / 60.0 / 48000.0;
        std::vector<float> delay (4400, 0.0f); size_t w = 0;
        juce::AudioBuffer<float> b (2, 512);
        // conectar (Logic en pausa)
        for (int k = 0; k < 20; ++k) { b.clear(); send.processBlock (b, m); recv.processBlock (b, m); }
        // play desde el compás 1 (ppq 0), Logic arranca 4400 muestras antes
        head.playing = true;
        double ppq = -4400 * qps;
        for (int blk = 0; blk < 200; ++blk)
        {
            head.ppq = ppq;
            for (int i = 0; i < 512; ++i)
            {
                const double t = ppq + (i + 4400) * qps;                          // audio leído adelantado
                const float x = (t >= 1.0 && t < 1.0 + 0.02) ? 0.9f : 0.001f;   // golpe en el tiempo 2
                b.setSample (0, i, x); b.setSample (1, i, x);
            }
            send.processBlock (b, m);
            for (int i = 0; i < 512; ++i) { const float in = b.getSample (0, i); const float out = delay[w]; delay[w] = in; w = (w + 1) % delay.size(); b.setSample (0, i, out); b.setSample (1, i, out); }
            recv.processBlock (b, m);
            ppq += 512 * qps;
            if (ppq > 3.5) break;
        }
        int peakBin = 0; float peak = 0.0f;
        for (int k = 0; k < MeteringABProcessor::scopeBins; ++k)
            if (recv.scopePreMax[(size_t) k].load() > peak) { peak = recv.scopePreMax[(size_t) k].load(); peakBin = k; }
        const int expectedBin = MeteringABProcessor::scopeBins / 4;   // tiempo 2 de 4
        check (std::abs (peakBin - expectedBin) <= 2, "Osciloscopio con latencia: el golpe se dibuja en su tiempo (columna)", peakBin);
        float tail = 0.0f;
        for (int k = MeteringABProcessor::scopeBins - 60; k < MeteringABProcessor::scopeBins; ++k)
            tail = juce::jmax (tail, recv.scopePreMax[(size_t) k].load());
        check (tail == 0.0f, "Al dar play no se dibuja nada al final del compás", tail);
        send.setPlayHead (nullptr); recv.setPlayHead (nullptr);
    }

    {
        // Crossfade de 10 ms: el peor caso es un efecto que invierte la onda
        // (PRE y POST opuestos). Sin crossfade el salto llega a 1,0.
        auto maxJumpAtSwitch = [] (float& settledError)
        {
            MeteringABProcessor send, recv; juce::MidiBuffer m;
            setParam (send, ParamIDs::link, 13); setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, 13);
            setParam (recv, ParamIDs::pre, 0);
            send.prepareToPlay (48000, 512); recv.prepareToPlay (48000, 512);
            juce::AudioBuffer<float> b (2, 512), dry (2, 512);
            double ph = 0; float last = 0.0f, maxJump = 0.0f;
            for (int blk = 0; blk < 400; ++blk)
            {
                if (blk == 300) setParam (recv, ParamIDs::pre, 1);   // tocar el botón
                for (int i = 0; i < 512; ++i) { const float x = 0.5f * (float) std::sin (ph); ph += 2 * juce::MathConstants<double>::pi * 100.0 / 48000.0; b.setSample (0, i, x); b.setSample (1, i, x); }
                dry.makeCopyOf (b);
                send.processBlock (b, m);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) b.setSample (ch, i, -b.getSample (ch, i));   // FX que invierte
                recv.processBlock (b, m);
                for (int i = 0; i < 512; ++i)
                {
                    if (blk >= 299 && blk <= 302) maxJump = juce::jmax (maxJump, std::abs (b.getSample (0, i) - last));
                    last = b.getSample (0, i);
                }
                if (blk == 399)
                {
                    settledError = 0.0f;
                    for (int i = 0; i < 512; ++i) settledError = juce::jmax (settledError, std::abs (b.getSample (0, i) - dry.getSample (0, i)));
                }
            }
            return maxJump;
        };
        float err = 0.0f;
        const float jump = maxJumpAtSwitch (err);
        check (jump < 0.02f, "Crossfade POST -> PRE: sin salto (máx. salto entre muestras)", jump);
        check (err < 1.0e-5f, "Crossfade: después de 10 ms queda exacto en PRE FX", err);
    }

    {
        // "Le doy play y no sale nada": casos reales de Logic.
        MeteringABProcessor send, recv; juce::MidiBuffer m;
        setParam (send, ParamIDs::link, 12); setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, 12);
        struct Head : juce::AudioPlayHead
        {
            double ppq = 0.0; bool playing = false;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p; p.setBpm (120.0); p.setTimeSignature (TimeSignature { 4, 4 }); p.setIsPlaying (playing);
                p.setPpqPosition (ppq); p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
                p.setBarCount ((juce::int64) std::floor (ppq / 4.0));
                return p;
            }
        } head;
        send.setPlayHead (&head); recv.setPlayHead (&head);
        send.prepareToPlay (48000, 512); recv.prepareToPlay (48000, 512);
        const double qps = 120.0 / 60.0 / 48000.0;
        juce::AudioBuffer<float> b (2, 512);
        auto block = [&] (bool sendRuns)
        {
            for (int i = 0; i < 512; ++i) { const float x = 0.5f * (float) std::sin (0.05 * i); b.setSample (0, i, x); b.setSample (1, i, x); }
            if (sendRuns) send.processBlock (b, m);
            recv.processBlock (b, m);
            if (head.playing) head.ppq += 512 * qps;
        };
        auto drawnNear = [&] (double quartersInBar)   // ¿hay algo dibujado en ese momento del compás?
        {
            const int bin = (int) (quartersInBar / 4.0 * MeteringABProcessor::scopeBins);
            float peak = 0.0f;
            for (int k = bin - 10; k <= bin + 10; ++k)
                peak = juce::jmax (peak, recv.scopePreMax[(size_t) k].load(), -recv.scopePreMin[(size_t) k].load());
            return peak > 0.1f;
        };

        // 1) Play desde el compás 3, después stop: Logic deja de procesar el Send.
        head.ppq = 8.0; head.playing = true;
        for (int k = 0; k < 60; ++k) block (true);
        head.playing = false;
        for (int k = 0; k < 60; ++k) block (false);
        // Play otra vez desde el compás 1: tiene que dibujar enseguida.
        head.ppq = 0.0; head.playing = true;
        for (int k = 0; k < 40; ++k) block (true);
        check (drawnNear (0.5), "Play de nuevo (desde antes): dibuja enseguida", 1);

        // 1b) Igual, pero en stop Logic no llama a ningún plugin (ni se entera del stop).
        head.playing = false; for (int k = 0; k < 5; ++k) block (true);
        head.ppq = 12.0; head.playing = true;
        for (int k = 0; k < 60; ++k) block (true);
        recv.clearScopeForTests();
        head.ppq = 0.0;                                            // play de nuevo desde el compás 1
        for (int k = 0; k < 40; ++k) block (true);
        check (drawnNear (0.5), "Play de nuevo sin pasar por stop: dibuja enseguida", 1);

        // 2) Modo ciclo: play en la mitad del compás 1, el ciclo vuelve al inicio.
        head.playing = false; for (int k = 0; k < 5; ++k) block (true);
        head.ppq = 2.0; head.playing = true;
        for (int k = 0; k < 180; ++k) { block (true); if (head.ppq >= 4.0) head.ppq -= 4.0; }   // ciclo de 1 compás
        check (drawnNear (0.5) && drawnNear (1.0), "Modo ciclo: sigue dibujando después de volver al inicio", 1);
        send.setPlayHead (nullptr); recv.setPlayHead (nullptr);
    }

    {
        // Canales: L y R se guardan por separado y L+R es el promedio.
        Chain c (11, 0.0f, 0);
        c.run (2.5);   // la cadena manda R = 0,8 x L
        float maxSum = 0, maxL = 0, maxR = 0;
        for (int k = 0; k < MeteringABProcessor::scopeBins; ++k)
        {
            maxSum = juce::jmax (maxSum, c.recv.scopeTraces[MeteringABProcessor::preSum].max[(size_t) k].load());
            maxL   = juce::jmax (maxL,   c.recv.scopeTraces[MeteringABProcessor::preLeft].max[(size_t) k].load());
            maxR   = juce::jmax (maxR,   c.recv.scopeTraces[MeteringABProcessor::preRight].max[(size_t) k].load());
        }
        check (std::abs (maxR / maxL - 0.8f) < 0.02f && std::abs (maxSum / maxL - 0.9f) < 0.02f,
               "Canales: R = 0,8 x L y L+R = promedio (relación R/L)", maxR / maxL);
    }

    {
        // LINK: al cambiar la escucha, la nueva onda aparece solo donde ya pasó el
        // cabezal desde el cambio; atrás y adelante quedan como estaban (120 BPM: 1 compás = 2 s).
        Chain c (10, -3.0f, 0);
        c.run (3.0);                               // 1,5 compases escuchando POST: cabezal en la mitad
        setParam (c.recv, ParamIDs::pre, 1);       // cambio a PRE
        c.run (0.5);                               // cabezal en 3/4 del compás
        auto flagAt = [&] (double barFraction) { return (int) c.recv.scopeShowPreAt[(size_t) (barFraction * MeteringABProcessor::scopeBins)].load(); };
        check (flagAt (0.25) == 0 && flagAt (0.6) == 1 && flagAt (0.9) == 0,
               "LINK: PRE solo desde el cambio hasta el cabezal; adelante sigue POST", flagAt (0.25) * 100 + flagAt (0.6) * 10 + flagAt (0.9));
    }

    {
        // Outline / Lines only: Detallado unido punta a punta.
        using V = std::vector<float>;
        using H = std::vector<std::uint8_t>;
        //          columnas: 0     1     2      3     4      5     6     7
        const V hi { 0.1f, 0.5f, -0.2f, 0.4f, 0.05f, 0.6f, 0.3f, 0.2f };
        const V lo { -0.1f, 0.1f, -0.5f, -0.3f, -0.6f, -0.1f, -0.4f, -0.2f };
        const auto [up, down] = ScopeView::tipEdges (hi, lo, H (8, 1), 100);
        check (std::abs (up[1] - 0.5f) < 1.0e-5f && std::abs (up[3] - 0.4f) < 1.0e-5f && std::abs (up[5] - 0.6f) < 1.0e-5f,
               "Outline: las puntas de arriba quedan donde están (todo el detalle)", up[3]);
        check (std::abs (up[2] - 0.45f) < 1.0e-5f && std::abs (up[4] - 0.5f) < 1.0e-5f,
               "Outline: entre punta y punta, línea recta (se llena el Detallado)", up[4]);
        check (std::abs (down[3] + 0.55f) < 1.0e-5f && std::abs (down[5] + 0.5f) < 1.0e-5f,
               "Outline: abajo, igual con las puntas negativas", down[3]);
        check (up[7] >= 0.0f && down[0] <= 0.0f, "Outline: arriba nunca baja del centro ni abajo lo pasa", up[7]);

        // Dos notas separadas por silencio: no se unen.
        V h2 (300, 0.0f), l2 (300, 0.0f);
        h2[10] = 0.5f; h2[290] = 0.5f;
        const auto [up2, down2] = ScopeView::tipEdges (h2, l2, H (300, 1), 100);
        check (up2[150] < 1.0e-5f, "Outline: no une puntas separadas por silencio", up2[150]);
    }
    {
        // Zoom: el audio guarda la onda muestra a muestra (120 BPM, 1 compás = 2 s = 96000 muestras).
        Chain c (21, -6.0f, 0);
        c.run (1.5);
        auto& r = c.recv;
        const int slots = r.scopeRawSlots.load();
        check (slots == 96000, "Zoom: un casillero por muestra (1 compás a 120 BPM)", slots);
        int filled = 0;
        for (int i = 1000; i < 60000; i += 1000)
        {
            const auto p = r.scopeRaw[MeteringABProcessor::preSum][(size_t) i].load();
            filled += (MeteringABProcessor::rawLo (p) == MeteringABProcessor::rawHi (p) && p != 0) ? 1 : 0;
        }
        check (filled >= 55, "Zoom: cada casillero es una muestra (mínimo = máximo)", filled);
        check (std::abs (MeteringABProcessor::rawHi (MeteringABProcessor::packRaw (-0.25f, 0.5f)) - 0.5f) < 1.0e-4f
               && std::abs (MeteringABProcessor::rawLo (MeteringABProcessor::packRaw (-0.25f, 0.5f)) + 0.25f) < 1.0e-4f,
               "Zoom: mínimo y máximo se guardan bien", 1);

        // Interfaz: Freeze (botón), zoom con la rueda en el mouse, arrastrar, doble clic.
        r.apvts.state.setProperty ("scopeOpen", true, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.createEditor());
        auto* panel = panelOf (ed);
        auto& sc = panel->getScopeForTests();
        check (! sc.isFrozen() && ! sc.isZoomed(), "Osciloscopio: arranca sin congelar y sin zoom", 1);

        const juce::ModifierKeys none;
        auto mouse = [&] (float x) { return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x, 100.0f }, none, 1.0f,
                                                               0.0f, 0.0f, 0.0f, 0.0f, &sc, &sc, juce::Time::getCurrentTime(), { x, 100.0f },
                                                               juce::Time::getCurrentTime(), 1, false); };
        sc.mouseDown (mouse (200.0f));
        sc.mouseUp (mouse (200.0f));
        check (! sc.isFrozen(), "Clic en el osciloscopio ya no congela (es para arrastrar)", 1);

        panel->getFreezeButtonForTests().onClick();
        check (sc.isFrozen() && panel->getFreezeButtonForTests().getToggleState(), "Freeze: el botón congela", 1);

        const auto plot = sc.getLocalBounds().toFloat().withTrimmedLeft (38.0f).withTrimmedRight (10.0f);
        const float mx = plot.getX() + plot.getWidth() * 0.25f;          // mouse en 1/4 del ancho
        juce::MouseWheelDetails w {}; w.deltaY = 0.5f;
        sc.mouseWheelMove (mouse (mx), w);
        const double anchor = sc.getViewStart() + 0.25 * sc.getViewSpan();
        check (sc.isZoomed() && sc.getViewSpan() < 0.5 && std::abs (anchor - 0.25) < 1.0e-3,
               "Rueda: zoom dejando quieto el punto bajo el mouse", sc.getViewSpan());

        const double before = sc.getViewStart();
        sc.mouseDown (mouse (300.0f));
        sc.mouseDrag (mouse (200.0f));                                     // arrastrar a la izquierda = ir hacia adelante
        check (sc.getViewStart() > before, "Arrastrar: se mueve por el loop", sc.getViewStart() - before);

        sc.mouseMagnify (mouse (mx), 1000.0f);
        check (sc.getViewSpan() * slots >= 47.0 && sc.getViewSpan() * slots < 60.0, "Zoom máximo: ~48 muestras a lo ancho", sc.getViewSpan() * slots);

        sc.setView (2.0, 0.1);
        check (sc.getViewStart() + sc.getViewSpan() <= 1.0 + 1.0e-9, "Zoom: no se va del loop", sc.getViewStart());

        {
            // Con zoom, "Show all" abajo a la izquierda vuelve a ver todo.
            auto snap = sc.createComponentSnapshot (sc.getLocalBounds());   // pinta y ubica "Show all"
            (void) snap;
            const auto pl = sc.plotArea();
            juce::Point<float> showAll { 0, 0 };
            for (float x = pl.getX() + 8.0f; x < pl.getX() + 300.0f && showAll.x == 0; x += 2.0f)
                if (sc.isShowAllAt ({ x, pl.getBottom() - 16.0f })) showAll = { x + 4.0f, pl.getBottom() - 16.0f };
            sc.mouseDown (mouse (showAll.x).withNewPosition (showAll)); sc.mouseUp (mouse (showAll.x).withNewPosition (showAll));
            check (! sc.isZoomed(), "Show all: vuelve a ver todo", sc.getViewSpan());
        }

        {
            // Regla de nivel: clic deja una línea fija en ese dB; clic sobre ella la saca.
            const auto pl = sc.plotArea();
            const float y6 = pl.getCentreY() - 0.501187f * pl.getHeight() * 0.5f;   // -6 dB
            check (std::abs (sc.dbAtY (y6) + 6.0f) < 0.05f, "Regla: la altura de -6 dB se lee -6 dB", sc.dbAtY (y6));
            const juce::Point<float> at { pl.getCentreX(), y6 };
            sc.mouseDown (mouse (at.x).withNewPosition (at)); sc.mouseUp (mouse (at.x).withNewPosition (at));
            check (sc.getMarkerDb().has_value() && std::abs (*sc.getMarkerDb() + 6.0f) < 0.05f, "Regla: clic deja la línea fija en -6 dB", sc.getMarkerDb().value_or (0));
            sc.mouseDown (mouse (at.x).withNewPosition (at)); sc.mouseUp (mouse (at.x).withNewPosition (at));
            check (! sc.getMarkerDb().has_value(), "Regla: clic sobre la línea la saca", 1);
            sc.setMarkerDb (-3.0f);
            sc.showMarkerEditor();
            sc.typeMarkerForTests ("-12.5");
            check (sc.getMarkerDb().has_value() && std::abs (*sc.getMarkerDb() + 12.5f) < 0.01f, "Regla: doble clic y escribir -12.5 dB", sc.getMarkerDb().value_or (0));
            sc.setMarkerDb (8.0f);
            check (std::abs (*sc.getMarkerDb() + 8.0f) < 0.01f, "Regla: un valor positivo se toma como negativo", *sc.getMarkerDb());
            sc.setMarkerDb (std::nullopt);
        }

        // Con zoom se dibuja la onda real (se ve distinto que sin zoom) y se pinta sin problemas.
        sc.setView (0.3, 0.01);
        auto deep = sc.createComponentSnapshot (sc.getLocalBounds(), true, 1.0f);
        int lit = 0;
        for (int x = 60; x < deep.getWidth() - 20; x += 2)
            for (int y = 30; y < deep.getHeight() - 20; y += 2)
                lit += deep.getPixelAt (x, y).getBrightness() > 0.5f ? 1 : 0;
        check (lit > 50, "Zoom x100: se dibuja la onda", lit);

        panel->getFreezeButtonForTests().onClick();
        check (! sc.isFrozen(), "Freeze: otro clic descongela", 1);

        // Gain: línea con el valor de la ganancia aplicada.
        for (int k = 0; k < 20; ++k) panel->refreshNow();
        check (! sc.getGainLine().has_value(), "Gain: apagado no dibuja la línea", 1);
        panel->getGainLineButtonForTests().onClick();
        panel->refreshNow();
        check (panel->getGainLineButtonForTests().getToggleState() && sc.getGainLine().has_value()
                   && std::abs (*sc.getGainLine() - r.displayGain.load()) < 0.3f,
               "Gain: la línea toma el valor de la ganancia aplicada (dB)", sc.getGainLine().value_or (99.0f));
        panel->getGainLineButtonForTests().onClick();
        check (! sc.getGainLine().has_value(), "Gain: otro clic la saca", 1);

        // PRE + POST: un solo botón que muestra las dos ondas juntas.
        auto& cmp = panel->getCompareButtonForTests();
        setParam (r, ParamIDs::pre, 1); panel->refreshNow();
        setParam (r, ParamIDs::pre, 0); panel->refreshNow();
        check (panel->isScopeLinkedForTests() && ! cmp.getToggleState() && ! sc.getShowPre() && sc.getShowPost(),
               "Sin PRE + POST: sigue a la escucha (solo POST, apagado)", cmp.getToggleState());
        cmp.onClick();
        check (panel->isCompareOnForTests() && cmp.getToggleState() && sc.getShowPre() && sc.getShowPost() && ! panel->isScopeLinkedForTests()
                   && sc.getStylePre() == ScopeView::Style::contour && sc.getStylePost() == ScopeView::Style::lines,
               "PRE + POST: las dos juntas, PRE en Outline y POST en Lines only", 1);
        check (cmp.getButtonText() == "Overlay" && cmp.getProperties()["icon"].toString() == "eye"
                   && panel->getABButtonForTests().getProperties()["icon"].toString() == "speaker",
               "Overlay: se llama Overlay con un ojo (se ve); la escucha lleva el parlante (suena)", 1);
        setParam (r, ParamIDs::pre, 1); panel->refreshNow();
        check (sc.getShowPre() && sc.getShowPost(), "PRE + POST: cambiar la escucha no lo apaga", 1);
        cmp.onClick();
        check (! panel->isCompareOnForTests() && panel->isScopeLinkedForTests() && sc.getShowPre() && ! sc.getShowPost(),
               "PRE + POST apagado: vuelve a seguir la escucha", 1);

        // Overlay ya no tiene clic derecho; el menú (colores, canales y grilla) está en Options.
        const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);
        juce::MouseEvent rc (juce::Desktop::getInstance().getMainMouseSource(), { 5.0f, 5.0f }, right, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &cmp, &cmp, juce::Time::getCurrentTime(), { 5.0f, 5.0f }, juce::Time::getCurrentTime(), 1, false);
        cmp.mouseDown (rc); cmp.mouseUp (rc);
        check (panel->getStyleMenuOpenedForTests() == 0 && ! panel->isCompareOnForTests(), "Overlay: el clic derecho no abre ningún menú", panel->getStyleMenuOpenedForTests());
        panel->getOptionsButtonForTests().onClick();
        check (panel->getStyleMenuOpenedForTests() == 1 && ! panel->isCompareOnForTests() && panel->getOptionsButtonForTests().isVisible(),
               "Options: abre el menú de colores, canales y grilla", panel->getStyleMenuOpenedForTests());
        std::set<int> ids;
        std::function<void (const juce::PopupMenu&)> walk = [&] (const juce::PopupMenu& m)
        {
            for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
            {
                if (it.getItem().itemID != 0) ids.insert (it.getItem().itemID);
                if (it.getItem().subMenu != nullptr) walk (*it.getItem().subMenu);
            }
        };
        walk (panel->buildStyleMenu());
        check (ids.count (700) && ids.count (750) && ids.count (400) && ids.count (500) && ! ids.count (100) && ! ids.count (150),
               "Menú: colores de PRE y POST, canales y grilla (sin formas)", (double) ids.size());
        const auto postBefore = sc.getPostColour();
        panel->applyStyleMenuResult (700 + 7);             // Violeta para PRE
        check (sc.getPreColour() == juce::Colour (0xff9b8cff) && sc.getPostColour() == postBefore, "Color de PRE: cambia solo PRE", 1);
    }
    std::printf ("\n%s (%d fallas)\n", failures == 0 ? "TODO OK" : "HAY FALLAS", failures);
    return failures;
}
