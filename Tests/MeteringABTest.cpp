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
    bool highpass = false;                                     // EQ que deja solo agudos
    float hpState[2][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    bool saturate = false, pulsing = false;                    // para las capturas del osciloscopio
    long long sampleCount = 0;
    float lpState[2] = { 0.0f, 0.0f };

    int block = 512;                                           // tamaño de bloque del "host"
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
        juce::AudioBuffer<float> buf (2, block), dry (2, block);
        const int blocks = (int) (seconds * 48000 / block);
        size_t w = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
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
            head.ppq = (double) (sampleCount - block) * 120.0 / 60.0 / 48000.0;
            send.processBlock (buf, midi);
            // "Efecto": ganancia + latencia
            for (int i = 0; i < block; ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& d = ch == 0 ? fxDelayL : fxDelayR;
                    size_t idx = (fxWrite + (size_t) i) % d.size();
                    float in = buf.getSample (ch, i);
                    float out = d[idx];
                    d[idx] = in;
                    float y = fxLatency == 0 ? in * fxGain : out * fxGain;
                    if (lowpass) { lpState[ch] += 0.15f * (y - lpState[ch]); y = lpState[ch]; }   // "EQ" que quita agudos
                    if (highpass)                                                               // EQ que saca el grave (queda casi solo el "aire")
                        for (int k = 0; k < 2; ++k) { hpState[ch][k] += 0.3f * (y - hpState[ch][k]); y -= hpState[ch][k]; }
                    if (unrelated) y = 0.2f * (otherRng.nextFloat() * 2 - 1);                   // sonido sin relación
                    if (saturate) y = std::tanh (3.0f * y) / 3.0f * 2.0f;                      // saturador que aplasta picos
                    buf.setSample (ch, i, y);
                }
            fxWrite = (fxWrite + block) % fxDelayL.size();
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

    if (const char* dir = std::getenv ("CURSOR_SHOT"))   // cómo se ve la mano cerrada (grande, sobre gris y sobre oscuro)
    {
        const auto hand = ClosedHand::image (16.0f);
        juce::Image img (juce::Image::ARGB, hand.getWidth() * 2 + 30, hand.getHeight() + 20, true);
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xffd8dade));
        g.setColour (juce::Colour (0xff121418)); g.fillRect (hand.getWidth() + 20, 0, hand.getWidth() + 10, img.getHeight());
        g.drawImageAt (hand, 10, 10); g.drawImageAt (hand, hand.getWidth() + 20, 10);
        const auto small = ClosedHand::image (3.0f);
        g.drawImageAt (small, 4, 4);
        juce::File f (juce::String (dir) + "/mano-cerrada.png");
        f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }
    if (std::getenv ("BENCH") != nullptr)
    {
        // Costo de la pantalla y del audio: tocando, con zoom (ms por cuadro, % de un núcleo).
        Chain c (33, -3.0f, 0); c.pulsing = true;
        c.recv.scopeLengthIndex = std::atoi (std::getenv ("BENCH"));
        c.recv.apvts.state.setProperty ("scopeChannels", std::getenv ("SPLIT") ? 3 : 0, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (c.recv.createEditor());
        auto& sc = panelOf (ed)->getScopeForTests();
        c.run (9.0);
        {
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            c.run (20.0);
            std::printf ("BENCH audio: %.2f%% de un núcleo (Send + Receive)\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 20000.0 * 100.0);
        }
        for (const double span : { 1.0, 0.5, 0.25, 0.1, 0.03, 0.01, 0.0 })
        {
            double total = 0.0; const int frames = 10;
            for (int f = 0; f < frames; ++f)
            {
                c.run (1.0 / 30.0);
                const double ph = c.recv.scopeCurrentBin.load() / (double) MeteringABProcessor::scopeBins;
                sc.setView (juce::jlimit (0.0, 1.0, ph - span * 0.5), span);
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                juce::Image img (juce::Image::ARGB, sc.getWidth(), sc.getHeight(), true);
                juce::Graphics g (img); sc.paintEntireComponent (g, true);
                total += juce::Time::getMillisecondCounterHiRes() - t0;
            }
            std::printf ("BENCH span %.2f: %.1f ms por cuadro\n", sc.getViewSpan(), total / frames);
        }
        c.recv.setPlayHead (nullptr); c.send.setPlayHead (nullptr);
        return 0;
    }
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

        {
            MeteringABProcessor send, recv; juce::MidiBuffer m;
            setParam (send, ParamIDs::link, 15); setParam (recv, ParamIDs::mode, 1); setParam (recv, ParamIDs::link, 15);
            setParam (recv, ParamIDs::pre, 0);
            recv.apvts.state.setProperty ("scopeOpen", true, nullptr);
            recv.apvts.state.setProperty ("scopeLength", 3, nullptr); recv.scopeLengthIndex = 3;     // 2 bars
            recv.apvts.state.setProperty ("scopeShowPre", false, nullptr);
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
            juce::File f (juce::String (argv[1]) + "/audio-detallado.png");
            f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);

            // Freeze + zoom (x8 y x40) sobre la misma parte.
            auto& sc = panelOf (ed)->getScopeForTests();
            panelOf (ed)->getFreezeButtonForTests().onClick();
            const double zooms[][2] = { { 0.36, 0.125 }, { 0.392, 0.025 }, { 0.3995, 0.0 } };   // x8, x40 y el máximo
            for (int z = 0; z < 3; ++z)
            {
                sc.setView (zooms[z][0], zooms[z][1]);
                auto zimg = sc.createComponentSnapshot (sc.getLocalBounds(), true, 2.0f);
                juce::File zf (juce::String (argv[1]) + "/audio-detallado" + (z == 0 ? "-zoom8.png" : z == 1 ? "-zoom40.png" : "-zoommax.png"));
                zf.deleteFile(); juce::FileOutputStream zos (zf); juce::PNGImageFormat().writeImageToStream (zimg, zos);
            }
            {
                // Con zoom y el mouse sobre "Show all" (manito, subrayado).
                sc.setView (zooms[0][0], zooms[0][1]);
                (void) sc.createComponentSnapshot (sc.getLocalBounds(), true, 1.0f);
                const auto pl = sc.plotArea();
                for (float x = pl.getX() + 8.0f; x < pl.getX() + 300.0f; x += 2.0f)
                    if (sc.isShowAllAt ({ x, pl.getBottom() - 16.0f }))
                    {
                        const juce::Point<float> p { x + 20.0f, pl.getBottom() - 16.0f };
                        sc.mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                        &sc, &sc, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 0, false));
                        break;
                    }
                auto himg = sc.createComponentSnapshot (sc.getLocalBounds(), true, 2.0f);
                juce::File hf (juce::String (argv[1]) + "/audio-showall-hover.png");
                hf.deleteFile(); juce::FileOutputStream hos (hf); juce::PNGImageFormat().writeImageToStream (himg, hos);
            }
            {                                           // regla de nivel: línea fija en -6 dB y el mouse en -14 dB
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
            {                                           // la interfaz entera, con Freeze y zoom
                sc.setView (0.36, 0.125);
                auto* panel = panelOf (ed);
                auto pimg = panel->createComponentSnapshot (panel->getLocalBounds(), true, 2.0f);
                juce::File pf (juce::String (argv[1]) + "/audio-panel-freeze.png");
                pf.deleteFile(); juce::FileOutputStream pos (pf); juce::PNGImageFormat().writeImageToStream (pimg, pos);

                sc.resetZoom();
                if (std::getenv ("MENU_SHOT"))          // cómo se ve el menú de Options (dibujado con el estilo del plugin)
                {
                    auto& lf = panel->getLookAndFeel();
                    auto drawMenu = [&] (const juce::PopupMenu& m, int highlight, const char* file)
                    {
                        struct Row { juce::String text; bool sep, ticked, sub; };
                        std::vector<Row> rows;
                        for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                        {
                            const auto& item = it.getItem();
                            rows.push_back ({ item.text, item.isSeparator, item.isTicked, item.subMenu != nullptr });
                        }
                        int w = 0, h = 4;
                        std::vector<int> heights;
                        for (auto& r : rows)
                        {
                            int iw = 0, ih = 0;
                            lf.getIdealPopupMenuItemSize (r.text, r.sep, -1, iw, ih);
                            w = juce::jmax (w, iw + 30); heights.push_back (ih); h += ih;
                        }
                        h += 4;
                        juce::Image img (juce::Image::ARGB, w * 2, h * 2, true);
                        juce::Graphics g (img);
                        g.addTransform (juce::AffineTransform::scale (2.0f));
                        lf.drawPopupMenuBackground (g, w, h);
                        int y = 4;
                        for (size_t i = 0; i < rows.size(); ++i)
                        {
                            const juce::Rectangle<int> area (0, y, w, heights[i]);
                            lf.drawPopupMenuItem (g, area, rows[i].sep, true, (int) i == highlight, rows[i].ticked, rows[i].sub,
                                                  rows[i].text, {}, nullptr, nullptr);
                            y += heights[i];
                        }
                        juce::File mf (juce::String (argv[1]) + file);
                        mf.deleteFile(); juce::FileOutputStream mos (mf); juce::PNGImageFormat().writeImageToStream (img, mos);
                    };
                    sc.setTripletGrid (true);
                    panel->getProcessorStateForTests().setProperty ("scopeGridTriplets", true, nullptr);
                    const auto menu = panel->buildStyleMenu();
                    drawMenu (menu, 2, "/menu-options.png");
                    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                        if (it.getItem().subMenu != nullptr)
                            drawMenu (*it.getItem().subMenu, -1, "/menu-channels.png");
                    panel->getProcessorStateForTests().setProperty ("scopeGridTriplets", false, nullptr);
                    sc.setTripletGrid (false);
                }
                {
                    // Grilla en tresillos: todo el loop y un compás.
                    sc.setTripletGrid (true);
                    sc.resetZoom();
                    auto t1 = sc.createComponentSnapshot (sc.getLocalBounds(), true, 2.0f);
                    juce::File tf (juce::String (argv[1]) + "/audio-tresillos.png");
                    tf.deleteFile(); juce::FileOutputStream tos (tf); juce::PNGImageFormat().writeImageToStream (t1, tos);
                    sc.setView (0.0, 0.5);
                    auto t2 = sc.createComponentSnapshot (sc.getLocalBounds(), true, 2.0f);
                    juce::File tf2 (juce::String (argv[1]) + "/audio-tresillos-1bar.png");
                    tf2.deleteFile(); juce::FileOutputStream tos2 (tf2); juce::PNGImageFormat().writeImageToStream (t2, tos2);
                    sc.setView (0.0, 0.125);
                    auto t3 = sc.createComponentSnapshot (sc.getLocalBounds(), true, 2.0f);
                    juce::File tf3 (juce::String (argv[1]) + "/audio-tresillos-zoom.png");
                    tf3.deleteFile(); juce::FileOutputStream tos3 (tf3); juce::PNGImageFormat().writeImageToStream (t3, tos3);
                    sc.setTripletGrid (false);
                }
                sc.resetZoom();
            }
            send.setPlayHead (nullptr); recv.setPlayHead (nullptr);
        }
        return 0;
    }

    if (argc > 1) // Metering A/BTests <carpeta>: guarda capturas de la interfaz
    {
        const char* names[] = { "/send.png", "/receive.png", "/receive-long.png", "/receive-closed.png",
                                "/paleta-greys.png", "/paleta-blue-pink.png", "/canales-split.png", "/link-cambio.png" };

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
                if (shot >= 4)   // paletas y canales, en 1/2 bar para ver el detalle
                {
                    a->apvts.state.setProperty ("scopeLength", 1, nullptr); a->scopeLengthIndex = 1;
                    a->apvts.state.setProperty ("scopePalette", shot == 4 ? 1 : shot == 5 ? 2 : 0, nullptr);
                    if (shot == 6) a->apvts.state.setProperty ("scopeChannels", 3, nullptr);   // L y R separados
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
                setParam (*a, ParamIDs::matchBypass, 1);
                panelOf (ed3)->refreshNow();
                save (*ed3, "/receive-bypass.png");
                setParam (*a, ParamIDs::matchBypass, 0);
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
        {
            MeteringABProcessor rcv; setParam (rcv, ParamIDs::mode, 1);
            std::unique_ptr<juce::AudioProcessorEditor> er (rcv.createEditor());
            check (MeteringABPanel::sendHeight == MeteringABPanel::sectionTop (1)
                       && panelOf (er)->getMeterAreaForTests().getY() > MeteringABPanel::sendHeight,
                   "Send: corta justo donde en el Receive empieza la parte de los números", MeteringABPanel::sendHeight);
            {
                // La columna en 5 partes.
                using MP = MeteringABPanel;
                const bool ok = MP::sectionTop (MP::numSections) == MP::receiveHeight
                                && MP::sectionTop (1) == 135 && MP::sectionTop (2) == 270 && MP::sectionTop (3) == 360
                                && MP::sectionTop (4) == 450 && MP::sendHeight == MP::sectionTop (1);
                check (ok, "Receive: partes de 135, 135, 90, 90 y 225 (título, números, Window, Latency, escucha)", MP::sectionTop (4));
                const auto knob = panelOf (er)->getWindowKnobForTests();
                check (knob.getCentreX() == 127 && knob.getCentreY() == (MP::sectionTop (2) + MP::sectionTop (3)) / 2 && knob.getWidth() == 76,
                       "Window: perilla en el medio de su parte, entre el primer y el segundo tercio (x 127)", knob.getCentreX());
                const auto info = panelOf (er)->getWindowInfoForTests();
                const int third = (MP::sectionTop (3) - MP::sectionTop (2)) / 3;
                const auto measured = panelOf (er)->getLatencyInfoBoundsForTests();
                check (info.first.getRight() == MP::sideWidth - 20 && info.second == juce::Justification::centredRight
                           && MP::sectionTop (3) - info.first.getBottom() == MP::sectionTop (4) - measured.getBottom()
                           && info.first.getHeight() == measured.getHeight(),
                       "Window: la equivalencia termina donde las burbujas y está a la altura de \"Measured\" (en su sección)", info.first.getY());
                (void) third;
                const auto bubbles = panelOf (er)->getWindowBubblesForTests();   // ms|Sync arriba, el valor abajo
                check (info.first.getY() - bubbles.second.getBottom() == measured.getY() - panelOf (er)->getMeasureButtonBoundsForTests().getBottom()
                           && bubbles.second.getRight() == MP::sideWidth - 20,
                       "Window: del valor a la equivalencia, lo mismo que de Measure a \"Measured\"", info.first.getY() - bubbles.second.getBottom());
                const auto measure = panelOf (er)->getMeasureButtonBoundsForTests();
                check (MP::windowValueWidth == measure.getWidth() && info.first.getRight() == measure.getRight(),
                       "Window: ms|Sync y el valor igual de largos que Measure (y alineados)", measure.getWidth());
                {
                    const auto number = panelOf (er)->getLatencyNumberAreaForTests();
                    const auto kb = panelOf (er)->getWindowKnobForTests();
                    const int knobLeft = kb.getX() + 4;               // el dibujo de la perilla deja 4 de margen
                    check (number.first.getX() == knobLeft && number.second.getX() == knobLeft
                               && number.first.getCentreY() == panelOf (er)->getMeasureButtonBoundsForTests().getCentreY()
                               && number.second.getY() == panelOf (er)->getLatencyInfoBoundsForTests().getY(),
                           "Latency: \"64 samples\" en la línea de Measure; \"= 1.33 ms\" en el renglón de \"Measured\"", number.first.getCentreY());
                }
                check (measure.getCentreY() == (MP::sectionTop (3) + MP::sectionTop (4)) / 2,
                       "Latency: Measure justo en el medio de la sección (de arriba abajo)", measure.getCentreY());
            }
        }
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
        // Botón Scope (al pie de la columna gris): un plugin nuevo arranca cerrado; después recuerda.
        MeteringABProcessor a; setParam (a, ParamIDs::mode, 1);
        MeteringABPanel::scopeOpenOnStart = false;
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        auto* p = panelOf (ed);
        check (! p->isScopeOpenForTests() && ! p->getScopeForTests().isVisible() && p->getWidth() == MeteringABPanel::sideWidth
               && ! p->getScopeToggleForTests().getToggleState(),
               "Scope: un plugin nuevo se abre con el osciloscopio cerrado (solo la columna gris)", p->getWidth());
        p->getScopeToggleForTests().onClick();
        check (p->isScopeOpenForTests() && p->getScopeForTests().isVisible() && p->getWidth() == MeteringABPanel::receiveWidth,
               "Scope: al abrirlo vuelve el osciloscopio (ancho)", p->getWidth());
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed2 (a.createEditor());
            check (panelOf (ed2)->isScopeOpenForTests() && panelOf (ed2)->getWidth() == MeteringABPanel::receiveWidth,
                   "Scope: al cerrar y reabrir el plugin, sigue abierto como lo dejaste", panelOf (ed2)->getWidth());
        }
        juce::MemoryBlock saved; a.getStateInformation (saved);
        MeteringABProcessor b2; b2.setStateInformation (saved.getData(), (int) saved.getSize());
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed3 (b2.createEditor());
            check (panelOf (ed3)->isScopeOpenForTests(), "Scope: abierto se guarda con el proyecto", 1);
        }
        p->getScopeToggleForTests().onClick();
        check (! p->isScopeOpenForTests() && p->getWidth() == MeteringABPanel::sideWidth,
               "Scope: al cerrarlo queda solo la columna gris (ancho)", p->getWidth());
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed4 (a.createEditor());
            check (! panelOf (ed4)->isScopeOpenForTests(), "Scope: cerrado, al reabrir sigue cerrado", 1);
        }
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
        check (e->getWidth() - MeteringABPanel::sideWidth == 3 * MeteringABPanel::sideWidth,
               "La sección del osciloscopio mide 3 Receive cerrados (1140 px)", e->getWidth() - MeteringABPanel::sideWidth);
    }
    {
        // Osciloscopio linkeado a la escucha: al alternar PRE/POST FX muestra solo esa onda.
        MeteringABProcessor a; setParam (a, ParamIDs::mode, 1); setParam (a, ParamIDs::pre, 0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
        auto* editor = panelOf (ed);
        auto pump = [editor] { editor->refreshNow(); };     // lo mismo que hace el timer de la interfaz
        pump();
        const bool postAtStart = ! (bool) a.apvts.state.getProperty ("scopeShowPre", true) && (bool) a.apvts.state.getProperty ("scopeShowPost", false);
        setParam (a, ParamIDs::pre, 1); pump();
        const bool onlyPre = (bool) a.apvts.state.getProperty ("scopeShowPre", false) && ! (bool) a.apvts.state.getProperty ("scopeShowPost", true);
        setParam (a, ParamIDs::pre, 0); pump();
        const bool onlyPost = ! (bool) a.apvts.state.getProperty ("scopeShowPre", true) && (bool) a.apvts.state.getProperty ("scopeShowPost", false);
        check (postAtStart && onlyPre && onlyPost, "LINK prendido: el osciloscopio sigue a la escucha (desde que se abre)", 1);

        // Sin botón LINK: aunque un proyecto viejo lo tenga apagado, siempre sigue a la escucha.
        MeteringABProcessor c; setParam (c, ParamIDs::mode, 1); setParam (c, ParamIDs::pre, 0);
        c.apvts.state.setProperty ("scopeLink", false, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (c.createEditor());
        auto* e2 = panelOf (ed2);
        e2->refreshNow(); setParam (c, ParamIDs::pre, 1); e2->refreshNow();
        const bool followsPre = (bool) c.apvts.state.getProperty ("scopeShowPre", false) && ! (bool) c.apvts.state.getProperty ("scopeShowPost", true);
        check (followsPre, "Siempre sigue a la escucha (aunque el proyecto tuviera LINK apagado)", 1);
    }
    {
        // Al dar play de nuevo el detalle de muestra ya no se borra en el audio (era pesado):
        // la pantalla muestra vacío lo que todavía no se volvió a dibujar.
        Chain c (31, -3.0f, 0);
        c.run (2.5);                                            // 1 compás a 120 = 2 s: todo dibujado
        const int slots = c.recv.scopeRawSlots.load();
        const int late = slots * 3 / 4;                         // a los 3/4 del compás
        const bool hadData = c.recv.scopeRaw[1][(size_t) late].load() != 0u;
        c.head.playing = false; c.run (0.3);
        c.head.playing = true; c.sampleCount = 0; c.run (0.2);   // play desde el principio
        std::unique_ptr<juce::AudioProcessorEditor> ed (c.recv.createEditor());
        auto& sc = panelOf (ed)->getScopeForTests();
        sc.setFrozen (true);
        const bool stillInProcessor = c.recv.scopeRaw[1][(size_t) late].load() != 0u;
        const bool earlyShown = sc.rawCopyForTests (1, slots / 40) != 0u;
        check (hadData && stillInProcessor && sc.rawCopyForTests (1, late) == 0u && earlyShown,
               "Play de nuevo: lo no redibujado se ve vacío (sin borrar 3 M valores en el audio)", late);
        c.recv.setPlayHead (nullptr); c.send.setPlayHead (nullptr);
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
        // (Antes 1,85 : 1; ahora manda el ancho = 3 Receive cerrados y el alto = 5 partes de 135.)
        check (scopeBox.getWidth() == MeteringABPanel::scopeWidth && scopeBox.getHeight() == MeteringABPanel::scopeHeight,
               "Osciloscopio: 1104 x 607 (3 columnas de ancho, todo el alto)", (double) scopeBox.getWidth() / scopeBox.getHeight());
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
        // Al abrir el Receive: una sola onda, la que suena (aunque un proyecto viejo tuviera Overlay).
        {
            MeteringABProcessor fresh; setParam (fresh, ParamIDs::mode, 1); setParam (fresh, ParamIDs::pre, 0);
            fresh.apvts.state.setProperty ("scopeCompare", true, nullptr);   // de un proyecto viejo: ya no existe
            std::unique_ptr<juce::AudioProcessorEditor> fe (fresh.createEditor());
            auto* fp = panelOf (fe);
            const auto& fs = fp->getScopeForTests();
            check (! fs.getShowPre() && fs.getShowPost(),
                   "Al abrir: se ve solo la onda que suena (POST), aunque el proyecto tuviera Overlay", (double) fs.getShowPre());
            setParam (fresh, ParamIDs::pre, 1);
            std::unique_ptr<juce::AudioProcessorEditor> fe2 (fresh.createEditor());
            const auto& fs2 = panelOf (fe2)->getScopeForTests();
            check (fs2.getShowPre() && ! fs2.getShowPost(), "Al abrir escuchando PRE: se ve solo PRE", (double) fs2.getShowPost());
        }
        // Match Gain Bypass: POST FX suena con su nivel real; la ganancia se sigue midiendo.
        Chain c (19, -6.0f, 0);
        c.run (6.0);
        auto matched = c.run (0.5);
        setParam (c.recv, ParamIDs::matchBypass, 1);
        auto raw = c.run (0.5);
        auto rmsOf = [] (const juce::AudioBuffer<float>& b) { return b.getRMSLevel (0, 0, b.getNumSamples()); };
        const float diffDb = juce::Decibels::gainToDecibels (rmsOf (matched)) - juce::Decibels::gainToDecibels (rmsOf (raw));
        check (std::abs (diffDb - 6.0f) < 0.6f && std::abs (c.recv.displayGain.load() - 6.0f) < 0.3f,
               "Match Gain Bypass: POST suena 6 dB más bajo (su nivel real) y la ganancia se sigue mostrando", diffDb);
        setParam (c.recv, ParamIDs::matchBypass, 0);
        auto back = c.run (0.5);
        check (std::abs (juce::Decibels::gainToDecibels (rmsOf (back)) - juce::Decibels::gainToDecibels (rmsOf (matched))) < 0.6f,
               "Match Gain Bypass apagado: vuelve a sonar nivelado", juce::Decibels::gainToDecibels (rmsOf (back)));

        std::unique_ptr<juce::AudioProcessorEditor> ed (c.recv.createEditor());
        auto* p = panelOf (ed);
        p->getMatchBypassButtonForTests().onClick();
        check (c.recv.apvts.getRawParameterValue (ParamIDs::matchBypass)->load() > 0.5f && p->getMatchBypassButtonForTests().getToggleState()
                   && p->getMatchBypassButtonForTests().isVisible(),
               "Botón Match Gain Bypass: prende el bypass", 1);
        p->getMatchBypassButtonForTests().onClick();
        check (c.recv.apvts.getRawParameterValue (ParamIDs::matchBypass)->load() < 0.5f, "Botón Match Gain Bypass: otro clic lo apaga", 1);
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
        {
            Chain h (17, -3.0f, 900);
            h.highpass = true;
            h.run (0.3); h.recv.startLatencyMeasurement(); h.run (3.5); h.recv.processPendingConversion();
            check (h.recv.latencyMeasurer.getState() == State::done && std::abs (plain (h.recv, ParamIDs::latency) - 900.0f) < 1.0f,
                   "Medir: EQ que deja solo agudos + 900 muestras -> 900 (antes: No clear match)", plain (h.recv, ParamIDs::latency));
        }
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

        // Bloques más grandes de lo avisado (8192 > 4096): antes la medición se trababa.
        Chain big (12, -3.0f, 777);
        big.block = 8192;
        big.run (0.5); big.recv.startLatencyMeasurement(); big.run (4.0); big.recv.processPendingConversion();
        check (big.recv.latencyMeasurer.getState() == State::done && std::abs (plain (big.recv, ParamIDs::latency) - 777.0f) < 0.01f,
               "Medir con bloques de 8192 muestras: 777 -> 777", plain (big.recv, ParamIDs::latency));
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
        sc.mouseUp (mouse (200.0f));

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
            {
                // Con el mouse encima: manito de clic (se entiende que se puede clickear).
                sc.mouseMove (mouse (showAll.x).withNewPosition (showAll));
                const bool hand = sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::PointingHandCursor);
                const bool noRuler = ! sc.isHoverLineShownForTests();
                const juce::Point<float> mid { pl.getCentreX(), pl.getCentreY() - 40.0f };
                sc.mouseMove (mouse (mid.x).withNewPosition (mid));
                const bool dragHand = sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::DraggingHandCursor);
                check (hand && noRuler && dragHand, "Show all: con el mouse encima, manito de clic (y sin la regla)", hand);
                // Agarrar para moverse: mano cerrada mientras está apretado; al soltar, abierta.
                sc.mouseDown (mouse (mid.x).withNewPosition (mid));
                const bool closedOnPress = sc.isGrabbingForTests() && sc.getMouseCursor() == closedHandCursor();
                const juce::Point<float> moved { mid.x - 60.0f, mid.y };
                sc.mouseDrag (mouse (moved.x).withNewPosition (moved));
                const bool closedWhileDragging = sc.getMouseCursor() == closedHandCursor();
                sc.mouseUp (mouse (moved.x).withNewPosition (moved));
                const bool openAfter = ! sc.isGrabbingForTests()
                                       && sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::DraggingHandCursor);
                {
                    // La mano cerrada tiene que verse (antes, en algunas Mac, quedaba invisible).
                    const auto hand = ClosedHand::image (3.0f);
                    int white = 0, black = 0;
                    for (int y = 0; y < hand.getHeight(); ++y)
                        for (int x = 0; x < hand.getWidth(); ++x)
                        {
                            const auto c = hand.getPixelAt (x, y);
                            if (c.getAlpha() > 200) (c.getBrightness() > 0.8f ? white : black)++;
                        }
                    check (hand.getWidth() == 54 && white > 600 && black > 150, "Mano cerrada: se dibuja (blanca con borde negro)", white);
                }
                check (closedOnPress && closedWhileDragging && openAfter,
                       "Zoom: al agarrar y arrastrar, mano cerrada; al soltar, mano abierta", closedOnPress * 100 + closedWhileDragging * 10 + openAfter);
                // Sobre la cajita de la regla: el dedo, al pasar y al clickear (no la mano cerrada).
                sc.setMarkerDb (-6.0f);
                const auto tag = sc.getMarkerTagAreaForTests().getCentre();
                sc.mouseMove (mouse (tag.x).withNewPosition (tag));
                const bool fingerOver = sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::PointingHandCursor);
                sc.mouseDown (mouse (tag.x).withNewPosition (tag));
                const bool fingerPressed = ! sc.isGrabbingForTests()
                                           && sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::PointingHandCursor);
                sc.mouseUp (mouse (tag.x).withNewPosition (tag));
                const bool tagRemoves = ! sc.isMarkerEditorOpen() && ! sc.getMarkerDb().has_value();
                sc.mouseDoubleClick (mouse (tag.x).withNewPosition (tag));     // doble clic: vuelve y abre la caja
                const bool doubleOpens = sc.isMarkerEditorOpen() && sc.getMarkerDb().has_value() && std::abs (*sc.getMarkerDb() + 6.0f) < 0.01f;
                sc.typeAndEscapeMarkerForTests ("");
                check (tagRemoves && doubleOpens && ! sc.isMarkerEditorOpen(),
                       "Regla: un clic en la cajita la saca; doble clic abre la caja (con el mismo valor)", tagRemoves * 10 + doubleOpens);
                // Sobre "Show all", también al clickear (apretado).
                sc.mouseMove (mouse (showAll.x).withNewPosition (showAll));
                sc.mouseDown (mouse (showAll.x).withNewPosition (showAll));
                const bool fingerOnShowAll = ! sc.isGrabbingForTests()
                                             && sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::PointingHandCursor);
                sc.mouseUp (mouse (showAll.x).withNewPosition (showAll));            // Show all: sin zoom
                sc.setMarkerDb (-6.0f);
                const auto tag2 = sc.getMarkerTagAreaForTests().getCentre();
                sc.mouseMove (mouse (tag2.x).withNewPosition (tag2));
                const bool arrowWithoutZoom = ! sc.isZoomed() && sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::NormalCursor);
                check (arrowWithoutZoom, "Sin zoom: siempre la flecha (también sobre la cajita de la regla)", arrowWithoutZoom);
                sc.setMarkerDb (std::nullopt);
                sc.setView (0.3, 0.1);
                (void) sc.createComponentSnapshot (sc.getLocalBounds());
                check (fingerOver && fingerPressed && fingerOnShowAll,
                       "Zoom: el dedo sobre Show all y la cajita de la regla (al pasar y al apretar)", fingerOver * 100 + fingerPressed * 10 + fingerOnShowAll);
                {
                    // Sobre la cajita gris oscura que sigue al mouse: también el dedo (al pasar y al apretar).
                    const juce::Point<float> onHover { sc.getHoverTagAreaForTests (0).getCentreX(), pl.getCentreY() - 60.0f };
                    sc.mouseMove (mouse (onHover.x).withNewPosition (onHover));
                    const bool fingerHover = sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::PointingHandCursor);
                    sc.mouseDown (mouse (onHover.x).withNewPosition (onHover));
                    const bool fingerHoverPressed = ! sc.isGrabbingForTests()
                                                    && sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::PointingHandCursor);
                    sc.mouseUp (mouse (onHover.x).withNewPosition (onHover));
                    const bool placed = sc.getMarkerDb().has_value();
                    sc.setMarkerDb (std::nullopt);
                    check (fingerHover && fingerHoverPressed && placed,
                           "Zoom: el dedo sobre la cajita gris que sigue al mouse (y el clic deja la regla)", fingerHover * 10 + fingerHoverPressed);
                    (void) sc.createComponentSnapshot (sc.getLocalBounds());
                }
                sc.mouseMove (mouse (showAll.x).withNewPosition (showAll));
            }
            sc.mouseDown (mouse (showAll.x).withNewPosition (showAll)); sc.mouseUp (mouse (showAll.x).withNewPosition (showAll));
            check (! sc.isZoomed(), "Show all: vuelve a ver todo", sc.getViewSpan());
            check (sc.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::NormalCursor), "Show all: después vuelve la flecha", 1);
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
            sc.showMarkerEditor();
            sc.typeAndEscapeMarkerForTests ("-20");
            check (std::abs (*sc.getMarkerDb() + 12.5f) < 0.01f, "Regla: escribir y Escape no cambia el valor", *sc.getMarkerDb());
            {
                // Caja abierta sin escribir nada: un clic en el gráfico la cierra (y no mueve la regla).
                sc.showMarkerEditor();
                const juce::Point<float> other { pl.getCentreX() - 80.0f, pl.getCentreY() - 30.0f };
                sc.mouseDown (mouse (other.x).withNewPosition (other)); sc.mouseUp (mouse (other.x).withNewPosition (other));
                const bool closedByPlot = ! sc.isMarkerEditorOpen() && std::abs (*sc.getMarkerDb() + 12.5f) < 0.01f;
                // ...y un clic en otra parte del plugin (ej. el botón Freeze) también.
                sc.showMarkerEditor();
                sc.clickElsewhereForTests (panel->getFreezeButtonForTests());
                const bool closedByButton = ! sc.isMarkerEditorOpen() && std::abs (*sc.getMarkerDb() + 12.5f) < 0.01f;
                check (closedByPlot && closedByButton, "Regla: la caja vacía se cierra al hacer clic en otra parte", closedByPlot * 10 + closedByButton);
            }
            sc.setMarkerDb (8.0f);
            check (std::abs (*sc.getMarkerDb() + 8.0f) < 0.01f, "Regla: un valor positivo se toma como negativo", *sc.getMarkerDb());
            {
                const auto tagC = sc.getMarkerTagAreaForTests().getCentre();
                sc.mouseMove (mouse (tagC.x).withNewPosition (tagC));
                const bool hiddenOnTag = ! sc.isHoverLineShownForTests();
                const juce::Point<float> away { pl.getCentreX(), pl.getY() + 20.0f };
                sc.mouseMove (mouse (away.x).withNewPosition (away));
                check (hiddenOnTag && sc.isHoverLineShownForTests(),
                       "Regla: sobre el cartelito de dB no se dibuja la línea del mouse (afuera sí)", hiddenOnTag);
            }
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

        check (panel->getABButtonForTests().getProperties()["icon"].toString() == "speaker",
               "La escucha lleva el parlante (lo que suena)", 1);
        panel->getOptionsButtonForTests().onClick();
        check (panel->getStyleMenuOpenedForTests() == 1 && panel->getOptionsButtonForTests().isVisible(),
               "Options: abre el menú de canales y grilla", panel->getStyleMenuOpenedForTests());
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
        check (ids.count (400) && ids.count (500) && ids.count (503) && ! ids.count (700) && ! ids.count (750) && ! ids.count (300),
               "Options: solo canales y grilla (sin colores)", (double) ids.size());
    }
    {
        // Casos raros: muestreos de 22 a 192 kHz, mono, bloques de 1 / impares / enormes,
        // cambios de tempo y de compás tocando.
        struct MovingHead : juce::AudioPlayHead
        {
            double ppq = 0.0, bpm = 120.0; int num = 4, den = 4; bool playing = true;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p; p.setBpm (bpm); p.setTimeSignature (TimeSignature { num, den }); p.setIsPlaying (playing);
                p.setPpqPosition (ppq); p.setPpqPositionOfLastBarStart (std::floor (ppq / (num * 4.0 / den)) * (num * 4.0 / den));
                return p;
            }
        };
        bool allFinite = true, envSeen = true;
        int cases = 0;
        for (const double sr : { 22050.0, 44100.0, 96000.0, 192000.0 })
            for (const int block : { 1, 37, 1000, 6000 })
                for (const int chans : { 1, 2 })
                {
                    const int link = 40 + (cases % 8);
                    MovingHead head;
                    MeteringABProcessor s, r;
                    setParam (s, ParamIDs::mode, 0); setParam (s, ParamIDs::link, (float) link);
                    setParam (r, ParamIDs::mode, 1); setParam (r, ParamIDs::link, (float) link);
                    s.setPlayHead (&head); r.setPlayHead (&head);
                    s.prepareToPlay (sr, 512); r.prepareToPlay (sr, 512);
                    juce::AudioBuffer<float> buf (chans, block); juce::MidiBuffer m;
                    const int total = (int) (sr * (block == 1 ? 0.4 : 2.5));
                    double phase = 0.0;
                    for (int done = 0; done < total; done += block)
                    {
                        const double t = done / sr;
                        head.bpm = t < 1.0 ? 120.0 : 77.0;                    // cambio de tempo
                        if (t > 1.6) { head.num = 7; head.den = 8; }          // y de compás
                        head.playing = ! (t > 1.9 && t < 2.1);                // un stop en el medio
                        for (int ch = 0; ch < chans; ++ch)
                            for (int i = 0; i < block; ++i)
                                buf.setSample (ch, i, 0.5f * (float) std::sin (phase + i * 2.0 * juce::MathConstants<double>::pi * 110.0 / sr));
                        phase += block * 2.0 * juce::MathConstants<double>::pi * 110.0 / sr;
                        s.processBlock (buf, m);
                        r.processBlock (buf, m);
                        for (int ch = 0; ch < chans; ++ch)
                            for (int i = 0; i < block; ++i)
                                allFinite = allFinite && std::isfinite (buf.getSample (ch, i));
                        if (head.playing) head.ppq += block / sr * head.bpm / 60.0;
                    }
                    bool any = false;
                    for (int b = 0; b < MeteringABProcessor::scopeBins; ++b)
                    {
                        const float u = r.scopeTraces[1].max[(size_t) b].load();
                        allFinite = allFinite && std::isfinite (u);
                        any = any || u > 0.1f;
                    }
                    envSeen = envSeen && (block == 1 || any);
                    s.setPlayHead (nullptr); r.setPlayHead (nullptr);
                    ++cases;
                }
        check (allFinite && envSeen, "Casos raros (22-192 kHz, mono, bloques 1-6000, tempo/compás/stop): sin NaN y dibuja", cases);
    }
    std::printf ("\n%s (%d fallas)\n", failures == 0 ? "TODO OK" : "HAY FALLAS", failures);
    return failures;
}
