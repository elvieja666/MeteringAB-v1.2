#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include "PluginProcessor.h"
#include "Text.h"
#include "Theme.h"

// ---------------------------------------------------------------------------
//  Osciloscopio sincronizado al tempo del proyecto.
//
//  - El ancho es un largo musical (1/4 bar a 4 bars). Arriba, la grilla de
//    tiempos (0, 1/4, 2/4, 3/4, 4/4...) con subdivisiones.
//  - Barre de izquierda a derecha siguiendo la canción y, al llegar al final,
//    vuelve a empezar reemplazando lo del compás anterior.
//  - Escala vertical fija en dB: 0 arriba y abajo, -∞ en el centro.
//  - PRE FX y encima POST FX (ya nivelado), con colores y estilo a elección.
//  - Clic: congelar / descongelar.
// ---------------------------------------------------------------------------
class ScopeView : public juce::Component, public juce::SettableTooltipClient
{
public:
    // Formas de dibujar la onda.
    enum class Style { contour = 0, detailed = 1, lines = 2 };
    static inline const juce::StringArray styleNames { "Contorno", "Detallado (picos)", juce::String::fromUTF8 ("Solo l\xc3\xadneas") };

    // Paletas de colores: { nombre, PRE, POST }.
    struct Palette { const char* name; juce::uint32 pre, post; };
    static constexpr std::array<Palette, 5> palettes {{
        { "Gris / Azul",         0xffa9aeb6, 0xff5aa9ff },
        { "Grises (clasico)",    0xff6c7077, 0xffb8bbc1 },
        { "Azul / Rosa",         0xff5aa9ff, 0xffff6fa8 },
        { "Blanco / Rojo",       0xffd9d7d3, 0xffeb5757 },
        { "Violeta / Lima",      0xff9b8cff, 0xffb5e655 },
    }};
    static constexpr int customPalette = (int) palettes.size();   // "Personalizado"

    explicit ScopeView (MeteringABProcessor& p) : processor (p) { capture(); }

    // Forma de cada onda por separado (ej. PRE en Contorno y POST en Solo líneas).
    void setStyles (Style pre, Style post)     { stylePre = pre; stylePost = post; repaint(); }
    Style getStylePre() const                  { return stylePre; }
    Style getStylePost() const                 { return stylePost; }
    // Forma cuando LINK sigue a la escucha (vacío = la de PRE y la de POST).
    void setLinkStyle (std::optional<Style> s) { linkStyle = s; repaint(); }
    std::optional<Style> getLinkStyle() const  { return linkStyle; }
    void setColours (juce::Colour pre, juce::Colour post) { preColour = pre; postColour = post; repaint(); }
    juce::Colour getPreColour() const          { return preColour; }
    juce::Colour getPostColour() const         { return postColour; }
    // Canales a mostrar.
    enum class Channels { sum = 0, left = 1, right = 2, split = 3 };
    static inline const juce::StringArray channelNames { "L + R (suma)", "Solo L", "Solo R", "L y R separados" };
    void setChannels (Channels c)              { channels = c; repaint(); }
    Channels getChannels() const               { return channels; }

    void setShowGrid (bool shouldShow)         { showGrid = shouldShow; repaint(); }

    // LINK: cada columna muestra la onda que sonaba cuando se dibujó.
    void setLinked (bool shouldLink)           { linked = shouldLink; repaint(); }
    bool getShowGrid() const                   { return showGrid; }

    bool isFrozen() const { return frozen; }

    // Mostrar u ocultar cada onda (botones PRE FX / POST FX).
    void setShowPre (bool shouldShow)  { showPre = shouldShow;  repaint(); }
    void setShowPost (bool shouldShow) { showPost = shouldShow; repaint(); }
    bool getShowPre() const            { return showPre; }
    bool getShowPost() const           { return showPost; }

    void mouseDown (const juce::MouseEvent&) override
    {
        frozen = ! frozen;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (! frozen)
            capture();

        const auto bounds = getLocalBounds().toFloat();
        g.setColour (Theme::darkPanel);
        g.fillRoundedRectangle (bounds, 12.0f);

        const auto plot = bounds.withTrimmedLeft (38.0f).withTrimmedTop (20.0f).withTrimmedRight (10.0f).withTrimmedBottom (8.0f);
        drawTimeGrid (g, plot);

        // Una o dos "franjas": L y R separados usa dos (L arriba, R abajo).
        const bool split = channels == Channels::split;
        const int lanes = split ? 2 : 1;
        const double binSecs = binSeconds();

        for (int lane = 0; lane < lanes; ++lane)
        {
            auto area = plot;
            if (split)
                area = lane == 0 ? plot.withHeight (plot.getHeight() * 0.5f - 4.0f)
                                 : plot.withTrimmedTop (plot.getHeight() * 0.5f + 4.0f);

            const float midY = area.getCentreY();
            const float half = area.getHeight() * 0.5f;
            drawLevelGrid (g, area, midY, half);

            auto yFor = [&] (float v) { return midY - juce::jlimit (-1.0f, 1.0f, v) * half; };
            const auto& pre  = traces[(size_t) (lane * 2)];
            const auto& post = traces[(size_t) (lane * 2 + 1)];

            auto drawSignal = [&] (const TraceCopy& t, juce::Colour colour, Style style, Mask mask)
            {
                switch (style)
                {
                    case Style::contour:  drawEnvelope (g, area, t.min, t.max, t.hold, yFor, colour, true, mask, binSecs);  break;
                    case Style::lines:    drawEnvelope (g, area, t.min, t.max, t.hold, yFor, colour, false, mask, binSecs); break;
                    case Style::detailed: drawDetailed (g, area, t.min, t.max, yFor, colour, mask); break;
                }
            };

            // Con LINK (y una sola onda elegida), cada columna muestra lo que
            // sonaba: PRE donde escuchabas PRE y POST donde escuchabas POST.
            const bool followListen = linked && showPre != showPost;

            if (followListen)
            {
                drawSignal (pre,  preColour,  linkStyle.value_or (stylePre),  Mask { &showPreAt, 1 });
                drawSignal (post, postColour, linkStyle.value_or (stylePost), Mask { &showPreAt, 0 });
            }
            else
            {
                if (showPre)
                    drawSignal (pre, preColour, stylePre, {});

                if (showPost)
                    drawSignal (post, postColour, stylePost, {});    // POST encima
            }

            // Etiqueta del canal (L / R) cuando no es la suma.
            if (channels != Channels::sum)
            {
                const char* name = channels == Channels::left ? "L" : channels == Channels::right ? "R" : (lane == 0 ? "L" : "R");
                g.setColour (juce::Colour (0xffe8e6e3).withAlpha (0.8f));
                g.setFont (Theme::font (13.0f, 600));
                g.drawText (name, juce::Rectangle<float> (area.getX() + 6.0f, area.getY() + 4.0f, 20.0f, 16.0f).toNearestInt(),
                            juce::Justification::centredLeft);
            }
        }

        if (split)
        {
            g.setColour (juce::Colour (0xff2e323c));
            g.fillRect (plot.getX(), plot.getCentreY() - 1.0f, plot.getWidth(), 2.0f);
        }

        // Cabezal del barrido
        if (! frozen)
        {
            const float x = plot.getX() + (float) currentBin / (float) MeteringABProcessor::scopeBins * plot.getWidth();
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        }

        // Estado (la leyenda ahora son los botones PRE FX / POST FX de arriba)
        g.setFont (Theme::font (11.0f, 400));

        if (! showPre && ! showPost)
        {
            g.setColour (juce::Colour (0xff8a8f98));
            g.drawText ("Ambas ondas ocultas", plot.toNearestInt(), juce::Justification::centred);
        }

        if (frozen)
        {
            g.setColour (juce::Colour (0xffe8e6e3));
            g.drawText ("CONGELADO", juce::Rectangle<float> (plot.getRight() - 110.0f, plot.getY() + 6.0f, 104.0f, 14.0f).toNearestInt(),
                        juce::Justification::centredRight);
        }
    }

private:
    void capture()
    {
        // Qué trazas copiar según los canales elegidos (pares PRE/POST).
        using P = MeteringABProcessor;
        int sources[4] = { P::preSum, P::postSum, P::preSum, P::postSum };

        switch (channels)
        {
            case Channels::sum:   break;
            case Channels::left:  sources[0] = P::preLeft;  sources[1] = P::postLeft;  break;
            case Channels::right: sources[0] = P::preRight; sources[1] = P::postRight; break;
            case Channels::split: sources[0] = P::preLeft;  sources[1] = P::postLeft;
                                  sources[2] = P::preRight; sources[3] = P::postRight; break;
        }

        const int used = channels == Channels::split ? 4 : 2;

        for (int b = 0; b < MeteringABProcessor::scopeBins; ++b)
        {
            showPreAt[(size_t) b] = processor.scopeShowPreAt[(size_t) b].load (std::memory_order_relaxed);
            const float count = (float) juce::jmax (1, processor.scopeCount[(size_t) b].load (std::memory_order_relaxed));

            for (int t = 0; t < used; ++t)
            {
                const auto& src = processor.scopeTraces[(size_t) sources[t]];
                auto& dst = traces[(size_t) t];
                dst.min[(size_t) b] = src.min[(size_t) b].load (std::memory_order_relaxed);
                dst.max[(size_t) b] = src.max[(size_t) b].load (std::memory_order_relaxed);
                dst.sq[(size_t) b]  = src.sq[(size_t) b].load (std::memory_order_relaxed) / count;
                dst.hold[(size_t) b] = src.hold[(size_t) b].load (std::memory_order_relaxed);
            }
        }

        currentBin = processor.scopeCurrentBin.load (std::memory_order_relaxed);
        lengthBars = processor.getScopeLengthBars();
        beatsPerBar = juce::jlimit (1, 32, processor.timeSigNum.load());
    }

    // Grilla de tiempos: líneas por tiempo y subdivisiones, con etiquetas k/n.
    void drawTimeGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        const double totalBeats = lengthBars * beatsPerBar;
        const int subdivisions = totalBeats <= 8.0 ? 4 : 1;             // 16avos si hay lugar
        const int totalLines = juce::roundToInt (totalBeats * subdivisions);
        const bool labelSubdivisions = totalBeats < 2.0;
        const float minLabelGap = 34.0f;
        float lastLabelX = -1000.0f;

        g.setFont (Theme::font (11.0f, 400));

        for (int k = 0; k <= totalLines; ++k)
        {
            const float x = plot.getX() + (float) k / (float) totalLines * plot.getWidth();
            const bool isBeat = k % subdivisions == 0;
            const bool isBar  = isBeat && (k / subdivisions) % beatsPerBar == 0;

            if (showGrid || isBar)
            {
                g.setColour (juce::Colour (isBar ? 0xff4a4f5a : isBeat ? 0xff383c45 : 0xff24272e));
                g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            }

            const bool wantsLabel = labelSubdivisions ? true : isBeat;

            if (wantsLabel && x - lastLabelX >= minLabelGap)
            {
                juce::String text;
                if (k == 0)                 text = "0";
                else if (labelSubdivisions) text = juce::String (k) + "/" + juce::String (beatsPerBar * subdivisions);
                else                        text = juce::String (k / subdivisions) + "/" + juce::String (beatsPerBar);

                g.setColour (juce::Colour (0xff8a8f98));
                const auto labelArea = k == 0          ? juce::Rectangle<float> (x - 2.0f,  plot.getY() - 18.0f, 44.0f, 14.0f)
                                     : k == totalLines ? juce::Rectangle<float> (x - 42.0f, plot.getY() - 18.0f, 44.0f, 14.0f)
                                                       : juce::Rectangle<float> (x - 22.0f, plot.getY() - 18.0f, 44.0f, 14.0f);
                g.drawText (text, labelArea.toNearestInt(),
                            k == 0 ? juce::Justification::centredLeft
                                   : k == totalLines ? juce::Justification::centredRight : juce::Justification::centred);
                lastLabelX = x;
            }
        }
    }

    // Escala en dB: 0, -6, -12, -18 arriba y abajo, -∞ en el centro.
    void drawLevelGrid (juce::Graphics& g, juce::Rectangle<float> plot, float midY, float half) const
    {
        g.setFont (Theme::font (11.0f, 400));
        float lastLabelOffset = 1.0e9f;                   // distancia al centro de la última etiqueta

        for (int db : { 0, -6, -12, -18 })
        {
            const float amplitude = juce::Decibels::decibelsToGain ((float) db);
            const float offset = amplitude * half;
            const bool roomForLabel = lastLabelOffset - offset >= 13.0f && offset >= 13.0f;   // que no se pisen

            for (int side : { -1, 1 })
            {
                const float y = midY + (float) side * offset;

                if (showGrid)
                {
                    g.setColour (juce::Colour (0xff24272e));
                    g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
                }

                if (roomForLabel)
                {
                    g.setColour (juce::Colour (0xff8a8f98));
                    g.drawText (juce::String (db), juce::Rectangle<float> (plot.getX() - 36.0f, y - 7.0f, 32.0f, 14.0f).toNearestInt(),
                                juce::Justification::centredRight);
                }
            }

            if (roomForLabel)
                lastLabelOffset = offset;
        }

        g.setColour (juce::Colour (0xff6b707a));
        g.drawHorizontalLine (juce::roundToInt (midY), plot.getX(), plot.getRight());
        g.setColour (juce::Colour (0xff8a8f98));
        g.drawText ("-" + juce::String::fromUTF8 ("\xe2\x88\x9e"),
                    juce::Rectangle<float> (plot.getX() - 36.0f, midY - 7.0f, 32.0f, 14.0f).toNearestInt(), juce::Justification::centredRight);
    }

    using Bins = std::array<float, MeteringABProcessor::scopeBins>;

    // Máscara opcional: con LINK, qué columnas dibujar de cada onda.
    using Flags = std::array<std::uint8_t, MeteringABProcessor::scopeBins>;
    struct Mask { const Flags* flags = nullptr; std::uint8_t wanted = 0;
                  bool includes (int b) const { return flags == nullptr || (*flags)[(size_t) b] == wanted; } };

public:
    // Borde de Contorno / Solo líneas, "adaptativo fino": sube al instante en el
    // golpe y se sostiene lo que diga cada columna (holdMs, lo calcula el audio
    // según el grave: 3 ms sin grave fuerte = mucha definición; un ciclo del grave
    // = sin ondas). Después baja rápido y se suaviza apenas.
    // Esquinas redondeadas ("intermedio 2"): la bajada y el suavizado crecen con
    // lo que se sostiene (más suave con graves, más nítido con agudos).
    static constexpr double envelopeReleaseMs     = 5.0;    // bajada mínima
    static constexpr double envelopeReleaseFactor = 0.5;    // ...o la mitad de lo que se sostiene
    static constexpr double envelopeSmoothMs      = 1.8;    // suavizado mínimo
    static constexpr double envelopeSmoothFactor  = 0.1;    // ...o el 10% de lo que se sostiene
    static constexpr double envelopeSpreadMs  = 20.0;   // el "sostener" no cambia de golpe entre columnas
    static constexpr double envelopeNotchMs   = 14.0;   // pozos más angostos que esto se tapan

    // Un punto por columna del osciloscopio (centro en bins) con su pico de arriba y de abajo.
    struct EnvelopePoint { float bin, hi, lo; };

    // holdMs: cuánto sostener en cada columna (nullptr = 20 ms para todas).
    static std::vector<EnvelopePoint> smoothEnvelope (const float* mins, const float* maxs, const float* holdMs,
                                                      int from, int to, double binSeconds)
    {
        std::vector<EnvelopePoint> points;
        const int n = to - from;
        if (n <= 0)
            return points;

        const double secs = binSeconds > 0.0 ? binSeconds : 1.0e-3;
        auto toBins = [secs] (double ms) { return juce::jmax (1, (int) std::lround (ms * 0.001 / secs)); };

        // 0) Cuánto sostener en cada columna: el mayor de las vecinas (±20 ms), así no salta.
        std::vector<int> holdBins ((size_t) n);
        std::vector<float> holdSpread ((size_t) n);
        const int spread = toBins (envelopeSpreadMs);
        for (int i = 0; i < n; ++i)
        {
            float ms = holdMs == nullptr ? 20.0f : 0.0f;
            if (holdMs != nullptr)
                for (int j = juce::jmax (0, i - spread); j <= juce::jmin (n - 1, i + spread); ++j)
                    ms = juce::jmax (ms, holdMs[from + j]);
            holdBins[(size_t) i] = toBins (ms);
            holdSpread[(size_t) i] = ms;
        }

        // 1) Sostener el pico ese tiempo y después bajar (5 ms, o la mitad de lo sostenido).
        std::vector<float> up ((size_t) n), down ((size_t) n);
        float eu = 0.0f, el = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float hi = -1.0f, lo = 1.0f;
            for (int j = juce::jmax (0, i - holdBins[(size_t) i] + 1); j <= i; ++j)
            {
                hi = juce::jmax (hi, maxs[from + j]);
                lo = juce::jmin (lo, mins[from + j]);
            }

            const double releaseMs = juce::jmax (envelopeReleaseMs, envelopeReleaseFactor * holdSpread[(size_t) i]);
            const auto a = (float) std::exp (-secs / (releaseMs * 0.001));
            eu = juce::jmax (hi, eu * a);
            el = juce::jmin (lo, el * a);
            up[(size_t) i] = eu;
            down[(size_t) i] = el;
        }

        // 1b) Tapar "pozos" angostos (menos de 14 ms) que quedan cuando cambia cuánto
        //     sostener justo en un golpe: primero ensancha los picos y después los
        //     vuelve a su ancho (los escalones de verdad quedan igual de nítidos).
        {
            const int r = toBins (envelopeNotchMs) / 2;
            if (r > 0)
            {
                auto close = [n, r] (std::vector<float>& v, bool upper)
                {
                    std::vector<float> wide ((size_t) n), out ((size_t) n);
                    for (int i = 0; i < n; ++i)
                    {
                        float m = v[(size_t) i];
                        for (int j = juce::jmax (0, i - r); j <= juce::jmin (n - 1, i + r); ++j)
                            m = upper ? juce::jmax (m, v[(size_t) j]) : juce::jmin (m, v[(size_t) j]);
                        wide[(size_t) i] = m;
                    }
                    for (int i = 0; i < n; ++i)
                    {
                        float m = wide[(size_t) i];
                        for (int j = juce::jmax (0, i - r); j <= juce::jmin (n - 1, i + r); ++j)
                            m = upper ? juce::jmin (m, wide[(size_t) j]) : juce::jmax (m, wide[(size_t) j]);
                        out[(size_t) i] = m;
                    }
                    v.swap (out);
                };
                close (up, true);
                close (down, false);
            }
        }

        // 2) Suavizado que se adapta: gaussiano de 1,8 ms, o el 10% de lo sostenido
        //    (redondea las esquinas sin perder los detalles chicos).
        points.reserve ((size_t) n + 2);
        points.push_back ({ (float) from, 0.0f, 0.0f });       // punta izquierda (se completa abajo)

        for (int i = 0; i < n; ++i)
        {
            const double smoothMs = juce::jmax (envelopeSmoothMs, envelopeSmoothFactor * holdSpread[(size_t) i]);
            const double sigma = juce::jmax (0.5, smoothMs * 0.001 / secs);
            const int radius = juce::jmin (96, (int) std::ceil (3.0 * sigma));
            double hi = 0.0, lo = 0.0, sum = 0.0;

            for (int k = -radius; k <= radius; ++k)
            {
                const auto j = (size_t) juce::jlimit (0, n - 1, i + k);
                const double w = std::exp (-0.5 * (k / sigma) * (k / sigma));
                hi += w * up[j];
                lo += w * down[j];
                sum += w;
            }

            points.push_back ({ (float) (from + i) + 0.5f, (float) (hi / sum), (float) (lo / sum) });
        }

        // Puntas del tramo, para que cubra de borde a borde.
        points.front().hi = points[1].hi;
        points.front().lo = points[1].lo;
        points.push_back ({ (float) to, points.back().hi, points.back().lo });
        return points;
    }

private:
    // Segundos que dura cada columna del osciloscopio (según tempo y largo).
    double binSeconds() const
    {
        const double bpm = juce::jmax (1.0, processor.hostBpm.load());
        const double quartersPerBar = beatsPerBar * 4.0 / juce::jmax (1, processor.timeSigDen.load());
        return lengthBars * quartersPerBar * 60.0 / bpm / MeteringABProcessor::scopeBins;
    }

    // Contorno (con relleno) o solo líneas: borde definido (ver smoothEnvelope).
    // Las columnas fuera de la máscara quedan vacías (cada tramo se dibuja aparte).
    template <typename YFunc>
    static void drawEnvelope (juce::Graphics& g, juce::Rectangle<float> plot, const Bins& mins, const Bins& maxs,
                              const Bins& holdMs, YFunc yFor, juce::Colour colour, bool filled, Mask mask, double binSeconds)
    {
        const int bins = MeteringABProcessor::scopeBins;
        juce::Path fill, lines;
        auto xFor = [&] (float b) { return plot.getX() + b / (float) bins * plot.getWidth(); };

        int b = 0;
        while (b < bins)
        {
            if (! mask.includes (b)) { ++b; continue; }

            int end = b;                                   // tramo [b, end)
            while (end < bins && mask.includes (end))
                ++end;

            const auto points = smoothEnvelope (mins.data(), maxs.data(), holdMs.data(), b, end, binSeconds);
            juce::Path top, bottom;

            for (size_t i = 0; i < points.size(); ++i)
            {
                const float x = xFor (points[i].bin), yHi = yFor (points[i].hi);
                if (i == 0) { fill.startNewSubPath (x, yHi); top.startNewSubPath (x, yHi); }
                else        { fill.lineTo (x, yHi);          top.lineTo (x, yHi); }
            }

            for (size_t i = points.size(); i-- > 0;)
            {
                const float x = xFor (points[i].bin), yLo = yFor (points[i].lo);
                fill.lineTo (x, yLo);
                if (i == points.size() - 1) bottom.startNewSubPath (x, yLo);
                else                        bottom.lineTo (x, yLo);
            }

            fill.closeSubPath();
            lines.addPath (top);
            lines.addPath (bottom);
            b = end;
        }

        // Contorno: color plano, sin línea de borde. Solo líneas: solo el borde.
        if (filled)
        {
            g.setColour (colour.withAlpha (0.6f));
            g.fillPath (fill);
            return;
        }

        g.setColour (colour);
        g.strokePath (lines, juce::PathStrokeType (1.5f));
    }

    // Detallado: cada medio píxel muestra el pico real (sin suavizar, se ven
    // los ciclos de los graves), todo de un mismo color.
    template <typename YFunc>
    static void drawDetailed (juce::Graphics& g, juce::Rectangle<float> plot, const Bins& mins, const Bins& maxs,
                              YFunc yFor, juce::Colour colour, Mask mask)
    {
        const int bins = MeteringABProcessor::scopeBins;
        const int columns = juce::jmax (1, (int) plot.getWidth() * 2);
        const float columnWidth = plot.getWidth() / (float) columns;
        juce::RectangleList<float> peaks;

        for (int c = 0; c < columns; ++c)
        {
            const int from = c * bins / columns;
            const int to   = juce::jmax (from + 1, (c + 1) * bins / columns);
            float lo = 1.0f, hi = -1.0f;
            bool any = false;

            for (int b = from; b < to; ++b)
            {
                if (! mask.includes (b))
                    continue;

                lo = juce::jmin (lo, mins[(size_t) b]);
                hi = juce::jmax (hi, maxs[(size_t) b]);
                any = true;
            }

            if (! any || hi < lo)
                continue;

            const float x = plot.getX() + (float) c * columnWidth;
            const float yHi = yFor (hi), yLo = yFor (lo);
            peaks.addWithoutMerging ({ x, yHi, columnWidth + 0.35f, juce::jmax (0.6f, yLo - yHi) });
        }

        g.setColour (colour);
        g.fillRectList (peaks);
    }

    MeteringABProcessor& processor;
    struct TraceCopy { Bins min {}, max {}, sq {}, hold {}; };
    std::array<TraceCopy, 4> traces;                  // [PRE, POST] de la franja de arriba, [PRE, POST] de abajo
    Channels channels = Channels::sum;
    Flags showPreAt {};
    bool linked = true;
    Style stylePre = Style::contour, stylePost = Style::contour;
    std::optional<Style> linkStyle;
    juce::Colour preColour { palettes[0].pre }, postColour { palettes[0].post };
    bool showGrid = true;
    int currentBin = 0;
    double lengthBars = 1.0;
    int beatsPerBar = 4;
    bool frozen = false;
    bool showPre = true, showPost = true;
};
