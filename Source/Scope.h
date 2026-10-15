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
//  - PRE FX (verde agua) y encima POST FX (ámbar, ya nivelado).
//  - Clic: congelar / descongelar.
// ---------------------------------------------------------------------------
class ScopeView : public juce::Component
{
public:
    static inline const juce::Colour preColour  { 0xff5dcaa5 };
    static inline const juce::Colour postColour { 0xfff2b544 };

    explicit ScopeView (MeteringABProcessor& p) : processor (p) { capture(); }

    bool isFrozen() const { return frozen; }

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
        const float midY = plot.getCentreY();
        const float half = plot.getHeight() * 0.5f;

        drawTimeGrid (g, plot);
        drawLevelGrid (g, plot, midY, half);

        auto yFor = [&] (float v) { return midY - juce::jlimit (-1.0f, 1.0f, v) * half; };

        drawEnvelope (g, plot, preMin, preMax, yFor, preColour);
        drawEnvelope (g, plot, postMin, postMax, yFor, postColour);    // POST encima

        // Cabezal del barrido
        if (! frozen)
        {
            const float x = plot.getX() + (float) currentBin / (float) MeteringABProcessor::scopeBins * plot.getWidth();
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        }

        // Leyenda y estado
        g.setFont (juce::FontOptions (11.0f));
        auto legend = juce::Rectangle<float> (plot.getX() + 8.0f, plot.getY() + 6.0f, 200.0f, 14.0f).toNearestInt();
        g.setColour (preColour);
        g.fillEllipse ((float) legend.getX(), (float) legend.getY() + 3.0f, 8.0f, 8.0f);
        g.drawText ("PRE FX", legend.withTrimmedLeft (12).withWidth (50), juce::Justification::centredLeft);
        g.setColour (postColour);
        g.fillEllipse ((float) legend.getX() + 64.0f, (float) legend.getY() + 3.0f, 8.0f, 8.0f);
        g.drawText ("POST FX", legend.withTrimmedLeft (76).withWidth (60), juce::Justification::centredLeft);

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
        for (int b = 0; b < MeteringABProcessor::scopeBins; ++b)
        {
            preMin[(size_t) b]  = processor.scopePreMin[(size_t) b].load (std::memory_order_relaxed);
            preMax[(size_t) b]  = processor.scopePreMax[(size_t) b].load (std::memory_order_relaxed);
            postMin[(size_t) b] = processor.scopePostMin[(size_t) b].load (std::memory_order_relaxed);
            postMax[(size_t) b] = processor.scopePostMax[(size_t) b].load (std::memory_order_relaxed);
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

            g.setColour (juce::Colour (isBar ? 0xff4a4f5a : isBeat ? 0xff383c45 : 0xff24272e));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());

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
    static void drawLevelGrid (juce::Graphics& g, juce::Rectangle<float> plot, float midY, float half)
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
                g.setColour (juce::Colour (0xff24272e));
                g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());

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

    template <typename YFunc>
    static void drawEnvelope (juce::Graphics& g, juce::Rectangle<float> plot,
                              const std::array<float, MeteringABProcessor::scopeBins>& mins,
                              const std::array<float, MeteringABProcessor::scopeBins>& maxs,
                              YFunc yFor, juce::Colour colour)
    {
        const int bins = MeteringABProcessor::scopeBins;
        juce::Path fill, top, bottom;

        // Cada columna toma el pico de sus vecinas: así el contorno no se ve
        // "dentado" cuando una columna dura menos que un ciclo de la onda.
        constexpr int radius = 2;
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
        g.setColour (colour.withAlpha (0.42f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (top, juce::PathStrokeType (1.2f));
        g.strokePath (bottom, juce::PathStrokeType (1.2f));
    }

    MeteringABProcessor& processor;
    std::array<float, MeteringABProcessor::scopeBins> preMin {}, preMax {}, postMin {}, postMax {};
    int currentBin = 0;
    double lengthBars = 1.0;
    int beatsPerBar = 4;
    bool frozen = false;
};
