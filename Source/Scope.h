#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Text.h"

// ---------------------------------------------------------------------------
//  Osciloscopio: PRE FX (verde agua) y encima POST FX (ámbar, ya nivelado).
//
//  - Con zoom corto (hasta 100 ms) se ve la forma de onda; el dibujo se
//    "engancha" en un cruce por cero del PRE para que quede quieto.
//  - Con zoom largo se ve el contorno (picos) de los últimos segundos:
//    ideal para ver qué hace un compresor con los transientes.
//  - La altura se ajusta sola al nivel de la pista (indicador "x4", etc.).
//  - Clic: congelar / descongelar.
// ---------------------------------------------------------------------------
class ScopeView : public juce::Component
{
public:
    static inline const juce::Colour preColour  { 0xff5dcaa5 };
    static inline const juce::Colour postColour { 0xfff2b544 };

    static constexpr std::array<int, 9> zoomMs { 10, 25, 50, 100, 250, 500, 1000, 2000, 5000 };

    explicit ScopeView (MeteringABProcessor& p) : processor (p) {}

    void setZoomIndex (int index)  { zoomIndex = juce::jlimit (0, (int) zoomMs.size() - 1, index); repaint(); }
    int getZoomIndex() const       { return zoomIndex; }
    bool isFrozen() const          { return frozen; }

    void mouseDown (const juce::MouseEvent&) override
    {
        frozen = ! frozen;
        frozenEnd = processor.scopeWritePos.load (std::memory_order_acquire);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff20232b));
        g.fillRoundedRectangle (bounds, 8.0f);

        auto plot = bounds.reduced (10.0f, 8.0f);
        const float midY = plot.getCentreY();

        // Línea del cero
        g.setColour (juce::Colour (0xff2e323c));
        g.drawHorizontalLine ((int) midY, plot.getX(), plot.getRight());

        const auto& pre  = processor.scopePre;
        const auto& post = processor.scopePost;

        if (pre.empty())
        {
            drawLabels (g, plot, 1.0f);
            return;
        }

        const int mask = processor.scopeMask;
        const double sampleRate = juce::jmax (1.0, processor.getCurrentSampleRate());
        const int span = juce::jlimit (16, mask - 1, (int) (zoomMs[(size_t) zoomIndex] * sampleRate / 1000.0));
        juce::int64 end = frozen ? frozenEnd : processor.scopeWritePos.load (std::memory_order_acquire);

        // Disparo: con zoom corto, empezar en un cruce por cero (subiendo) del PRE.
        juce::int64 start = end - span;

        if (zoomMs[(size_t) zoomIndex] <= 100)
        {
            for (juce::int64 t = end - span; t > end - 2 * span && t > 1; --t)
            {
                const float a = pre[(size_t) ((t - 1) & mask)];
                const float b = pre[(size_t) (t & mask)];

                if (a < 0.0f && b >= 0.0f)
                {
                    start = t;
                    break;
                }
            }
        }

        if (start < 0)
            start = 0;

        // Autoescala: que el pico más alto ocupe ~90% de la altura (máx. x32).
        float peak = 1.0e-6f;
        for (juce::int64 t = start; t < start + span; ++t)
        {
            peak = juce::jmax (peak, std::abs (pre[(size_t) (t & mask)]));
            peak = juce::jmax (peak, std::abs (post[(size_t) (t & mask)]));
        }

        const float targetScale = juce::jlimit (1.0f, 32.0f, 0.9f / peak);
        scale = frozen ? scale : scale + (targetScale - scale) * (targetScale < scale ? 0.6f : 0.15f);

        const float halfHeight = plot.getHeight() * 0.5f;
        auto yFor = [&] (float v) { return midY - juce::jlimit (-1.0f, 1.0f, v * scale) * halfHeight; };

        drawTrace (g, pre,  mask, start, span, plot, yFor, preColour,  0.45f);
        drawTrace (g, post, mask, start, span, plot, yFor, postColour, 0.45f);   // POST encima

        drawLabels (g, plot, scale);
    }

private:
    template <typename YFunc>
    static void drawTrace (juce::Graphics& g, const std::vector<float>& data, int mask, juce::int64 start, int span,
                           juce::Rectangle<float> plot, YFunc yFor, juce::Colour colour, float fillAlpha)
    {
        const int columns = juce::jmax (1, (int) plot.getWidth());
        const double samplesPerColumn = (double) span / columns;

        if (samplesPerColumn <= 3.0)
        {
            // Pocas muestras por píxel: se dibuja la onda como línea.
            juce::Path line;
            for (int i = 0; i < span; ++i)
            {
                const float x = plot.getX() + (float) i / (float) (span - 1) * plot.getWidth();
                const float y = yFor (data[(size_t) ((start + i) & mask)]);
                if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            g.setColour (colour);
            g.strokePath (line, juce::PathStrokeType (1.6f));
            return;
        }

        // Muchas muestras por píxel: contorno (mínimo y máximo de cada columna).
        juce::Path fill, top, bottom;
        std::vector<juce::Point<float>> lows ((size_t) columns);

        for (int c = 0; c < columns; ++c)
        {
            const auto from = start + (juce::int64) (c * samplesPerColumn);
            const auto to   = start + (juce::int64) ((c + 1) * samplesPerColumn);
            float lo = 1.0e9f, hi = -1.0e9f;

            for (auto t = from; t < juce::jmax (from + 1, to); ++t)
            {
                const float v = data[(size_t) (t & mask)];
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }

            const float x = plot.getX() + (float) c;
            const float yHi = yFor (hi), yLo = yFor (lo);

            if (c == 0) { fill.startNewSubPath (x, yHi); top.startNewSubPath (x, yHi); bottom.startNewSubPath (x, yLo); }
            else        { fill.lineTo (x, yHi);          top.lineTo (x, yHi);          bottom.lineTo (x, yLo); }

            lows[(size_t) c] = { x, yLo };
        }

        for (int c = columns - 1; c >= 0; --c)
            fill.lineTo (lows[(size_t) c]);

        fill.closeSubPath();
        g.setColour (colour.withAlpha (fillAlpha));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (top, juce::PathStrokeType (1.2f));
        g.strokePath (bottom, juce::PathStrokeType (1.2f));
    }

    void drawLabels (juce::Graphics& g, juce::Rectangle<float> plot, float currentScale) const
    {
        g.setFont (juce::FontOptions (11.0f));

        // Leyenda
        auto legend = plot.withHeight (14.0f).toNearestInt();
        g.setColour (preColour);
        g.fillEllipse ((float) legend.getX(), (float) legend.getY() + 3.0f, 8.0f, 8.0f);
        g.drawText ("PRE FX", legend.withTrimmedLeft (12).withWidth (50), juce::Justification::centredLeft);
        g.setColour (postColour);
        g.fillEllipse ((float) legend.getX() + 64.0f, (float) legend.getY() + 3.0f, 8.0f, 8.0f);
        g.drawText ("POST FX", legend.withTrimmedLeft (76).withWidth (60), juce::Justification::centredLeft);

        // Escala, zoom y estado
        g.setColour (juce::Colour (0xff8a8f98));
        juce::String info = "x" + juce::String (currentScale, currentScale < 10.0f ? 1 : 0);
        if (frozen)
            info = "CONGELADO  ·  "_u + info;
        g.drawText (info, plot.withHeight (14.0f).toNearestInt(), juce::Justification::centredRight);

        const int ms = zoomMs[(size_t) zoomIndex];
        g.drawText (ms >= 1000 ? juce::String (ms / 1000.0, ms % 1000 == 0 ? 0 : 1) + " s" : juce::String (ms) + " ms",
                    plot.withTrimmedTop (plot.getHeight() - 14.0f).toNearestInt(), juce::Justification::centredRight);
    }

    MeteringABProcessor& processor;
    int zoomIndex = 3;                  // 100 ms
    bool frozen = false;
    juce::int64 frozenEnd = 0;
    float scale = 1.0f;
};
