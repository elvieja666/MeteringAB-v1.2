#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Text.h"

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
    static inline const juce::StringArray styleNames { "Contorno", "Detallado (picos + RMS)", juce::String::fromUTF8 ("Solo l\xc3\xadneas") };

    // Paletas de colores: { nombre, PRE, POST }.
    struct Palette { const char* name; juce::uint32 pre, post; };
    static constexpr std::array<Palette, 5> palettes {{
        { "Verde agua / Ambar",  0xff5dcaa5, 0xfff2b544 },
        { "Grises (clasico)",    0xff6c7077, 0xffb8bbc1 },
        { "Azul / Rosa",         0xff5aa9ff, 0xffff6fa8 },
        { "Blanco / Rojo",       0xffd9d7d3, 0xffeb5757 },
        { "Violeta / Lima",      0xff9b8cff, 0xffb5e655 },
    }};
    static constexpr int customPalette = (int) palettes.size();   // "Personalizado"

    explicit ScopeView (MeteringABProcessor& p) : processor (p) { capture(); }

    void setStyle (Style s)                    { style = s; repaint(); }
    Style getStyle() const                     { return style; }
    void setColours (juce::Colour pre, juce::Colour post) { preColour = pre; postColour = post; repaint(); }
    juce::Colour getPreColour() const          { return preColour; }
    juce::Colour getPostColour() const         { return postColour; }
    // Canales a mostrar.
    enum class Channels { sum = 0, left = 1, right = 2, split = 3 };
    static inline const juce::StringArray channelNames { "L + R (suma)", "Solo L", "Solo R", "L y R separados" };
    void setChannels (Channels c)              { channels = c; repaint(); }
    Channels getChannels() const               { return channels; }

    void setShowGrid (bool shouldShow)         { showGrid = shouldShow; repaint(); }
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
        g.setColour (juce::Colour (0xff121316));
        g.fillRoundedRectangle (bounds, 8.0f);

        const auto plot = bounds.withTrimmedLeft (38.0f).withTrimmedTop (20.0f).withTrimmedRight (10.0f).withTrimmedBottom (8.0f);
        drawTimeGrid (g, plot);

        // Una o dos "franjas": L y R separados usa dos (L arriba, R abajo).
        const bool split = channels == Channels::split;
        const int lanes = split ? 2 : 1;

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

            auto drawSignal = [&] (const TraceCopy& t, juce::Colour colour)
            {
                switch (style)
                {
                    case Style::contour:  drawEnvelope (g, area, t.min, t.max, yFor, colour, true);  break;
                    case Style::lines:    drawEnvelope (g, area, t.min, t.max, yFor, colour, false); break;
                    case Style::detailed: drawDetailed (g, area, t.min, t.max, t.sq, yFor, midY, colour); break;
                }
            };

            if (showPre)
                drawSignal (pre, preColour);

            if (showPost)
                drawSignal (post, postColour);    // POST encima

            // Etiqueta del canal (L / R) cuando no es la suma.
            if (channels != Channels::sum)
            {
                const char* name = channels == Channels::left ? "L" : channels == Channels::right ? "R" : (lane == 0 ? "L" : "R");
                g.setColour (juce::Colour (0xffe8e6e3).withAlpha (0.8f));
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
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
        g.setFont (juce::FontOptions (11.0f));

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
            const float count = (float) juce::jmax (1, processor.scopeCount[(size_t) b].load (std::memory_order_relaxed));

            for (int t = 0; t < used; ++t)
            {
                const auto& src = processor.scopeTraces[(size_t) sources[t]];
                auto& dst = traces[(size_t) t];
                dst.min[(size_t) b] = src.min[(size_t) b].load (std::memory_order_relaxed);
                dst.max[(size_t) b] = src.max[(size_t) b].load (std::memory_order_relaxed);
                dst.sq[(size_t) b]  = src.sq[(size_t) b].load (std::memory_order_relaxed) / count;
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

        g.setFont (juce::FontOptions (11.0f));

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
        g.setFont (juce::FontOptions (11.0f));
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

    // Contorno (con relleno) o solo líneas: el pico de cada columna, suavizado.
    template <typename YFunc>
    static void drawEnvelope (juce::Graphics& g, juce::Rectangle<float> plot, const Bins& mins, const Bins& maxs,
                              YFunc yFor, juce::Colour colour, bool filled)
    {
        const int bins = MeteringABProcessor::scopeBins;
        juce::Path fill, top, bottom;

        // Cada columna toma el pico de sus vecinas: así el contorno no se ve
        // "dentado" cuando una columna dura menos que un ciclo de la onda.
        constexpr int radius = 4;
        auto peakMax = [&] (int b) { float v = -1.0f; for (int j = juce::jmax (0, b - radius); j <= juce::jmin (bins - 1, b + radius); ++j) v = juce::jmax (v, maxs[(size_t) j]); return v; };
        auto peakMin = [&] (int b) { float v =  1.0f; for (int j = juce::jmax (0, b - radius); j <= juce::jmin (bins - 1, b + radius); ++j) v = juce::jmin (v, mins[(size_t) j]); return v; };

        for (int b = 0; b < bins; ++b)
        {
            const float x = plot.getX() + ((float) b + 0.5f) / (float) bins * plot.getWidth();
            const float yHi = yFor (peakMax (b));

            if (b == 0) { fill.startNewSubPath (x, yHi); top.startNewSubPath (x, yHi); }
            else        { fill.lineTo (x, yHi);          top.lineTo (x, yHi); }
        }

        for (int b = bins - 1; b >= 0; --b)
        {
            const float x = plot.getX() + ((float) b + 0.5f) / (float) bins * plot.getWidth();
            const float yLo = yFor (peakMin (b));
            fill.lineTo (x, yLo);

            if (b == bins - 1) bottom.startNewSubPath (x, yLo);
            else               bottom.lineTo (x, yLo);
        }

        fill.closeSubPath();

        if (filled)
        {
            g.setColour (colour.withAlpha (0.42f));
            g.fillPath (fill);
        }

        g.setColour (colour);
        g.strokePath (top, juce::PathStrokeType (filled ? 1.2f : 1.5f));
        g.strokePath (bottom, juce::PathStrokeType (filled ? 1.2f : 1.5f));
    }

    // Detallado: cada medio píxel muestra el pico real (sin suavizar, se ven
    // los ciclos de los graves) y, encima, el RMS en un tono más claro.
    template <typename YFunc>
    static void drawDetailed (juce::Graphics& g, juce::Rectangle<float> plot, const Bins& mins, const Bins& maxs,
                              const Bins& meanSquares, YFunc yFor, float midY, juce::Colour colour)
    {
        const int bins = MeteringABProcessor::scopeBins;
        const int columns = juce::jmax (1, (int) plot.getWidth() * 2);
        const float columnWidth = plot.getWidth() / (float) columns;
        juce::RectangleList<float> peaks, rms;

        for (int c = 0; c < columns; ++c)
        {
            const int from = c * bins / columns;
            const int to   = juce::jmax (from + 1, (c + 1) * bins / columns);
            float lo = 1.0f, hi = -1.0f;
            double sq = 0.0;

            for (int b = from; b < to; ++b)
            {
                lo = juce::jmin (lo, mins[(size_t) b]);
                hi = juce::jmax (hi, maxs[(size_t) b]);
                sq += meanSquares[(size_t) b];
            }

            if (hi < lo)
                continue;

            const float x = plot.getX() + (float) c * columnWidth;
            const float yHi = yFor (hi), yLo = yFor (lo);
            peaks.addWithoutMerging ({ x, yHi, columnWidth + 0.35f, juce::jmax (0.6f, yLo - yHi) });

            const float r = (float) std::sqrt (sq / (to - from));
            const float rTop = yFor (juce::jmin (r, hi > 0.0f ? hi : r)), rBottom = yFor (-juce::jmin (r, lo < 0.0f ? -lo : r));
            if (rBottom > rTop)
                rms.addWithoutMerging ({ x, rTop, columnWidth + 0.35f, rBottom - rTop });
        }

        g.setColour (colour.withMultipliedBrightness (0.62f).withAlpha (0.9f));
        g.fillRectList (peaks);
        g.setColour (colour.brighter (0.25f));
        g.fillRectList (rms);
        juce::ignoreUnused (midY);
    }

    MeteringABProcessor& processor;
    struct TraceCopy { Bins min {}, max {}, sq {}; };
    std::array<TraceCopy, 4> traces;                  // [PRE, POST] de la franja de arriba, [PRE, POST] de abajo
    Channels channels = Channels::sum;
    Style style = Style::contour;
    juce::Colour preColour { palettes[0].pre }, postColour { palettes[0].post };
    bool showGrid = true;
    int currentBin = 0;
    double lengthBars = 1.0;
    int beatsPerBar = 4;
    bool frozen = false;
    bool showPre = true, showPost = true;
};
