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
//  - Freeze (botón de arriba): congela lo que se ve.
//  - Zoom: rueda del mouse / pellizco en el trackpad (centrado en el mouse),
//    arrastrar para moverse, doble clic para volver a ver todo. Con zoom se ve
//    la onda real, muestra por muestra.
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
    void setFrozen (bool shouldFreeze)
    {
        if (shouldFreeze == frozen)
            return;

        if (shouldFreeze)
        {
            capture();                                  // lo último que se vio...
            captureAllRaw();                            // ...con todo el detalle, para hacer zoom después
        }

        frozen = shouldFreeze;
        repaint();
    }

    // Mostrar u ocultar cada onda (botones PRE FX / POST FX).
    void setShowPre (bool shouldShow)  { showPre = shouldShow;  repaint(); }
    void setShowPost (bool shouldShow) { showPost = shouldShow; repaint(); }
    bool getShowPre() const            { return showPre; }
    bool getShowPost() const           { return showPost; }

    // Zoom: qué parte del loop se ve (0 = comienzo, 1 = final).
    double getViewStart() const { return viewStart; }
    double getViewSpan() const  { return viewSpan; }
    bool isZoomed() const       { return viewSpan < 0.9999; }
    void resetZoom()            { setView (0.0, 1.0); }

    void setView (double start, double span)
    {
        viewSpan = juce::jlimit (minViewSpan(), 1.0, span);
        viewStart = juce::jlimit (0.0, 1.0 - viewSpan, start);
        setMouseCursor (isZoomed() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    // Zoom (factor > 1 acerca) dejando quieto el punto que está bajo el mouse.
    void zoomAt (float x, double factor)
    {
        const auto plot = plotArea();
        const double rel = juce::jlimit (0.0, 1.0, (double) (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        const double anchor = viewStart + rel * viewSpan;
        const double span = juce::jlimit (minViewSpan(), 1.0, viewSpan / factor);
        setView (anchor - rel * span, span);
    }

    // Mover lo que se ve (en píxeles: positivo = la onda va a la derecha).
    void panBy (float pixels)
    {
        setView (viewStart - (double) pixels / juce::jmax (1.0f, plotArea().getWidth()) * viewSpan, viewSpan);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartView = viewStart;
        dragStartX = e.position.x;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! isZoomed())
            return;
        setView (dragStartView - (double) (e.position.x - dragStartX) / juce::jmax (1.0f, plotArea().getWidth()) * viewSpan, viewSpan);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { resetZoom(); }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (std::abs (w.deltaX) > std::abs (w.deltaY))   // trackpad de costado: moverse
            panBy (w.deltaX * 400.0f);
        else if (! juce::exactlyEqual (w.deltaY, 0.0f))   // rueda / trackpad arriba-abajo: zoom
            zoomAt (e.position.x, std::exp ((double) w.deltaY * 2.5));
    }

    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override
    {
        if (scaleFactor > 0.0f)
            zoomAt (e.position.x, (double) scaleFactor);
    }

    void paint (juce::Graphics& g) override
    {
        if (! frozen)
            capture();

        const auto bounds = getLocalBounds().toFloat();
        g.setColour (Theme::darkPanel);
        g.fillRoundedRectangle (bounds, 12.0f);

        const auto plot = plotArea();
        drawTimeGrid (g, plot);

        // Una o dos "franjas": L y R separados usa dos (L arriba, R abajo).
        const bool split = channels == Channels::split;
        const int lanes = split ? 2 : 1;
        const double binSecs = binSeconds();
        const View view { viewStart, viewSpan, plot.getX(), plot.getWidth() };

        // Con zoom: ¿cuánto detalle hay? (casilleros visibles por píxel)
        const double visibleSlots = viewSpan * rawSlots;
        const bool rawUseful = isZoomed() && rawLoaded && visibleSlots <= 256.0 * plot.getWidth();
        const bool waveForEnvelope = rawUseful && viewSpan * MeteringABProcessor::scopeBins <= plot.getWidth() / 6.0f;

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
            const int preIndex = lane * 2, postIndex = lane * 2 + 1;

            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (area.expanded (0.0f, 2.0f).toNearestInt());

            auto drawSignal = [&] (int index, juce::Colour colour, Style style, Mask mask)
            {
                const auto& t = traces[(size_t) index];
                const auto& raw = rawCopy[(size_t) traceSource[(size_t) index]];

                if (rawUseful && (style == Style::detailed || waveForEnvelope))
                {
                    drawWave (g, area, raw, rawSlots, view, yFor, colour, mask);
                    return;
                }

                switch (style)
                {
                    case Style::contour:  drawEnvelope (g, area, t, view, yFor, colour, true, mask, binSecs);  break;
                    case Style::lines:    drawEnvelope (g, area, t, view, yFor, colour, false, mask, binSecs); break;
                    case Style::detailed: drawDetailed (g, area, t.min, t.max, view, yFor, colour, mask); break;
                }
            };

            // Con LINK (y una sola onda elegida), cada columna muestra lo que
            // sonaba: PRE donde escuchabas PRE y POST donde escuchabas POST.
            const bool followListen = linked && showPre != showPost;

            if (followListen)
            {
                drawSignal (preIndex,  preColour,  linkStyle.value_or (stylePre),  Mask { &showPreAt, 1 });
                drawSignal (postIndex, postColour, linkStyle.value_or (stylePost), Mask { &showPreAt, 0 });
            }
            else
            {
                if (showPre)
                    drawSignal (preIndex, preColour, stylePre, {});

                if (showPost)
                    drawSignal (postIndex, postColour, stylePost, {});    // POST encima
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
            const float x = view.x ((double) currentBin / (double) MeteringABProcessor::scopeBins);
            if (x >= plot.getX() && x <= plot.getRight())
            {
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            }
        }

        // Estado (la leyenda ahora son los botones PRE FX / POST FX de arriba)
        g.setFont (Theme::font (11.0f, 400));

        if (! showPre && ! showPost)
        {
            g.setColour (juce::Colour (0xff8a8f98));
            g.drawText ("Ambas ondas ocultas", plot.toNearestInt(), juce::Justification::centred);
        }

        // Con zoom: qué parte del loop se ve (barrita abajo) y cuánto zoom hay.
        if (isZoomed())
        {
            const auto track = juce::Rectangle<float> (plot.getX(), plot.getBottom() - 4.0f, plot.getWidth(), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (track, 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.fillRoundedRectangle (track.withX (track.getX() + (float) viewStart * track.getWidth())
                                         .withWidth (juce::jmax (4.0f, (float) viewSpan * track.getWidth())), 1.5f);

            const double factor = 1.0 / viewSpan;
            const auto text = "Zoom x" + juce::String (factor, factor < 10.0 ? 1 : 0) + juce::String::fromUTF8 ("  \xc2\xb7  doble clic: ver todo");
            g.setColour (juce::Colour (0xffe8e6e3).withAlpha (0.75f));
            g.drawText (text, juce::Rectangle<float> (plot.getX() + 8.0f, plot.getBottom() - 22.0f, 260.0f, 14.0f).toNearestInt(),
                        juce::Justification::centredLeft);
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

        for (int t = 0; t < 4; ++t)
            traceSource[(size_t) t] = sources[t];

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
                dst.hiMin[(size_t) b]  = src.hiMin[(size_t) b].load (std::memory_order_relaxed);
                dst.hiMax[(size_t) b]  = src.hiMax[(size_t) b].load (std::memory_order_relaxed);
                dst.lowPos[(size_t) b] = src.lowPos[(size_t) b].load (std::memory_order_relaxed);
                dst.lowNeg[(size_t) b] = src.lowNeg[(size_t) b].load (std::memory_order_relaxed);
            }
        }

        currentBin = processor.scopeCurrentBin.load (std::memory_order_relaxed);
        lengthBars = processor.getScopeLengthBars();
        beatsPerBar = juce::jlimit (1, 32, processor.timeSigNum.load());
        rawSlots = juce::jlimit (1, MeteringABProcessor::scopeRawCapacity, processor.scopeRawSlots.load (std::memory_order_relaxed));

        // Con zoom: copiar el detalle de muestra solo de lo que se ve (si no es demasiado).
        rawLoaded = false;
        const double visibleSlots = viewSpan * rawSlots;
        if (isZoomed() && visibleSlots <= 256.0 * juce::jmax (1.0f, plotArea().getWidth()))
        {
            const int from = juce::jmax (0, (int) std::floor (viewStart * rawSlots) - 2);
            const int to   = juce::jmin (rawSlots, (int) std::ceil ((viewStart + viewSpan) * rawSlots) + 2);
            for (int t = 0; t < used; ++t)
                copyRaw (sources[t], from, to);
            rawLoaded = true;
        }
    }

    // Freeze: guardar todo el detalle de muestra (de todas las ondas) para hacer zoom quieto.
    void captureAllRaw()
    {
        rawSlots = juce::jlimit (1, MeteringABProcessor::scopeRawCapacity, processor.scopeRawSlots.load (std::memory_order_relaxed));
        for (int t = 0; t < MeteringABProcessor::numScopeTraces; ++t)
            copyRaw (t, 0, rawSlots);
        rawLoaded = true;
    }

    void copyRaw (int source, int from, int to)
    {
        const auto& src = processor.scopeRaw[(size_t) source];
        auto& dst = rawCopy[(size_t) source];
        if (src == nullptr)
            return;
        if ((int) dst.size() < MeteringABProcessor::scopeRawCapacity)
            dst.assign ((size_t) MeteringABProcessor::scopeRawCapacity, 0u);
        for (int i = from; i < to; ++i)
            dst[(size_t) i] = src[(size_t) i].load (std::memory_order_relaxed);
    }

    // Dónde se dibuja la onda (sin los márgenes de las escalas).
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (38.0f).withTrimmedTop (20.0f).withTrimmedRight (10.0f).withTrimmedBottom (8.0f);
    }

    // Zoom máximo: que se vean al menos ~48 muestras.
    double minViewSpan() const
    {
        const int slots = juce::jlimit (1, MeteringABProcessor::scopeRawCapacity, processor.scopeRawSlots.load (std::memory_order_relaxed));
        return juce::jlimit (1.0e-5, 1.0, 48.0 / (double) slots);
    }

    // Grilla de tiempos: líneas por tiempo y subdivisiones, con etiquetas k/n.
    // Con zoom las subdivisiones se van achicando (corcheas, semis, fusas...).
    void drawTimeGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        const double totalBeats = lengthBars * beatsPerBar;
        int subdivisions = totalBeats <= 8.0 ? 4 : 1;                   // 16avos si hay lugar
        const double pixelsPerBeat = plot.getWidth() / (totalBeats * viewSpan);
        while (subdivisions < 256 && pixelsPerBeat / (subdivisions * 2) >= 14.0 && isZoomed())
            subdivisions *= 2;

        const int totalLines = juce::roundToInt (totalBeats * subdivisions);
        const bool labelSubdivisions = totalBeats * viewSpan < 2.0;
        float lastLabelX = -1000.0f;
        const View view { viewStart, viewSpan, plot.getX(), plot.getWidth() };

        const int firstLine = juce::jmax (0, (int) std::floor (viewStart * totalLines));
        const int lastLine  = juce::jmin (totalLines, (int) std::ceil ((viewStart + viewSpan) * totalLines));

        g.setFont (Theme::font (11.0f, 400));

        for (int k = firstLine; k <= lastLine; ++k)
        {
            const float x = view.x ((double) k / (double) totalLines);
            if (x < plot.getX() - 0.5f || x > plot.getRight() + 0.5f)
                continue;

            const bool isBeat = k % subdivisions == 0;
            const bool isBar  = isBeat && (k / subdivisions) % beatsPerBar == 0;

            if (showGrid || isBar)
            {
                g.setColour (juce::Colour (isBar ? 0xff4a4f5a : isBeat ? 0xff383c45 : 0xff24272e));
                g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            }

            const bool wantsLabel = labelSubdivisions ? true : isBeat;
            juce::String text;
            if (k == 0)        text = "0";
            else if (isBeat)   text = juce::String (k / subdivisions) + "/" + juce::String (beatsPerBar);
            else
            {
                int num = k, den = beatsPerBar * subdivisions;               // fracción simplificada del compás
                while (num % 2 == 0 && den % 2 == 0) { num /= 2; den /= 2; }
                if (den <= beatsPerBar * 4)                                   // hasta semicorcheas (más chico no se lee)
                    text = juce::String (num) + "/" + juce::String (den);
            }

            const float textWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
            if (wantsLabel && text.isNotEmpty() && x - textWidth * 0.5f - lastLabelX >= 10.0f)
            {
                const bool atLeft = x - plot.getX() < 22.0f, atRight = plot.getRight() - x < 22.0f;
                g.setColour (juce::Colour (0xff8a8f98));
                const auto labelArea = atLeft  ? juce::Rectangle<float> (x - 2.0f,  plot.getY() - 18.0f, 44.0f, 14.0f)
                                     : atRight ? juce::Rectangle<float> (x - 42.0f, plot.getY() - 18.0f, 44.0f, 14.0f)
                                               : juce::Rectangle<float> (x - 22.0f, plot.getY() - 18.0f, 44.0f, 14.0f);
                g.drawText (text, labelArea.toNearestInt(),
                            atLeft ? juce::Justification::centredLeft
                                   : atRight ? juce::Justification::centredRight : juce::Justification::centred);
                lastLabelX = atLeft ? x + textWidth : x + textWidth * 0.5f;
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

    // Zoom: qué parte del loop se ve y dónde cae en la pantalla.
    struct View
    {
        double start, span;
        float left, width;
        float x (double loopFraction) const { return left + (float) ((loopFraction - start) / span) * width; }
        double fractionAt (float px) const  { return start + (double) (px - left) / width * span; }
    };
    struct TraceCopy { Bins min {}, max {}, sq {}, hiMin {}, hiMax {}, lowPos {}, lowNeg {}; };

    // Máscara opcional: con LINK, qué columnas dibujar de cada onda.
    using Flags = std::array<std::uint8_t, MeteringABProcessor::scopeBins>;
    struct Mask { const Flags* flags = nullptr; std::uint8_t wanted = 0;
                  bool includes (int b) const { return flags == nullptr || (*flags)[(size_t) b] == wanted; } };

public:
    // Borde de Contorno / Solo líneas: "graves sin onda + agudos prolijos" (H2).
    //  - Graves (< 150 Hz): une los picos de cada medio ciclo (el audio los marca en
    //    lowPos / lowNeg). Un pico que baja solo, entre dos vecinos más altos, es el
    //    vaivén del ciclo y se ignora; si bajan dos o más seguidos es un cambio de
    //    volumen de verdad y se ve. Pozos de menos de 20 ms se tapan.
    //  - Agudos: su pico se sostiene 8 ms (menos "ruido") y se suma encima del grave.
    //  - Al final, un suavizado gaussiano de 2,5 ms.
    static constexpr double lowMergeMs  = 3.0;     // picos del grave a menos de esto: vale el mayor
    static constexpr float  lowDipRatio = 0.85f;   // un pico solo más bajo que esto (x vecinos) se ignora
    static constexpr double lowGapMs    = 120.0;   // picos más separados no se unen (silencio entre notas)
    static constexpr double lowFadeMs   = 10.0;    // ...y el grave baja / sube en este tiempo
    static constexpr double lowCloseMs  = 20.0;    // pozos más angostos que esto se tapan
    static constexpr double highHoldMs  = 8.0;     // cuánto se sostiene el pico de los agudos
    static constexpr double smoothMs    = 2.5;     // suavizado final

    // Lo que guarda el audio de cada onda, columna por columna.
    struct EnvelopeData { const float* lowPos; const float* lowNeg; const float* hiMin; const float* hiMax; };

    // Un punto por columna del osciloscopio (centro en bins) con su pico de arriba y de abajo.
    struct EnvelopePoint { float bin, hi, lo; };

    static std::vector<EnvelopePoint> smoothEnvelope (EnvelopeData d, int from, int to, double binSeconds)
    {
        std::vector<EnvelopePoint> points;
        const int n = to - from;
        if (n <= 0)
            return points;

        const double secs = binSeconds > 0.0 ? binSeconds : 1.0e-3;
        auto toBins = [secs] (double ms) { return (int) std::lround (ms * 0.001 / secs); };

        // --- Graves: borde de arriba (picos positivos) y de abajo (negativos, en positivo).
        auto lowEdge = [&] (const float* peaks, float sign)
        {
            struct Peak { int bin; float v; };
            std::vector<Peak> list;
            for (int i = 0; i < n; ++i)
            {
                const float v = peaks[from + i] * sign;
                if (v > 0.0f)
                    list.push_back ({ i, v });
            }

            const int gap = juce::jmax (1, toBins (lowGapMs));

            // Pozo de un solo medio ciclo (entre dos vecinos más altos): es el ciclo, no el volumen.
            for (bool changed = true; changed && list.size() > 2;)
            {
                changed = false;
                std::vector<Peak> kept { list.front() };
                for (size_t i = 1; i + 1 < list.size(); ++i)
                {
                    const auto& prev = kept.back();
                    const auto& next = list[i + 1];
                    const bool between = list[i].bin - prev.bin <= gap && next.bin - list[i].bin <= gap;
                    if (between && list[i].v < lowDipRatio * juce::jmin (prev.v, next.v))
                    {
                        changed = true;
                        continue;
                    }
                    kept.push_back (list[i]);
                }
                kept.push_back (list.back());
                list.swap (kept);
            }

            // Picos muy juntos: cada uno vale el mayor de los que tiene cerca.
            const int merge = toBins (lowMergeMs);
            std::vector<float> merged (list.size());
            for (size_t i = 0; i < list.size(); ++i)
            {
                float m = list[i].v;
                for (size_t j = i; j-- > 0 && list[i].bin - list[j].bin <= merge;)
                    m = juce::jmax (m, list[j].v);
                for (size_t j = i + 1; j < list.size() && list[j].bin - list[i].bin <= merge; ++j)
                    m = juce::jmax (m, list[j].v);
                merged[i] = m;
            }

            // Unir pico con pico (si están cerca); si no, bajar / subir rápido a cero.
            std::vector<float> edge ((size_t) n, 0.0f);
            const int fade = juce::jmax (1, toBins (lowFadeMs));
            auto fadeFrom = [&] (int bin, float v, int dir)
            {
                for (int k = 0; k <= fade; ++k)
                {
                    const int b = bin + dir * k;
                    if (b < 0 || b >= n) break;
                    edge[(size_t) b] = juce::jmax (edge[(size_t) b], v * (1.0f - (float) k / (float) (fade + 1)));
                }
            };

            for (size_t i = 0; i < list.size(); ++i)
            {
                const int b0 = list[i].bin;
                edge[(size_t) b0] = juce::jmax (edge[(size_t) b0], merged[i]);

                if (i + 1 < list.size() && list[i + 1].bin - b0 <= gap)
                {
                    const int b1 = list[i + 1].bin;
                    for (int b = b0 + 1; b < b1; ++b)
                    {
                        const float t = (float) (b - b0) / (float) (b1 - b0);
                        edge[(size_t) b] = merged[i] + (merged[i + 1] - merged[i]) * t;
                    }
                }
                else if (i + 1 == list.size() && n - 1 - b0 <= gap)
                {
                    for (int b = b0 + 1; b < n; ++b)    // el último pico, cerca del final del tramo: sigue igual
                        edge[(size_t) b] = merged[i];
                }
                else
                {
                    fadeFrom (b0, merged[i], 1);
                }

                if (i == 0 && b0 <= gap)
                {
                    for (int b = 0; b < b0; ++b)        // el primero, cerca del comienzo del tramo: igual
                        edge[(size_t) b] = merged[i];
                }
                else if (i == 0 || b0 - list[i - 1].bin > gap)
                {
                    fadeFrom (b0, merged[i], -1);
                }
            }

            // Tapar pozos angostos: ensanchar y volver al ancho (cierre).
            const int r = toBins (lowCloseMs) / 2;
            if (r > 0)
            {
                std::vector<float> wide ((size_t) n);
                for (int i = 0; i < n; ++i)
                {
                    float m = edge[(size_t) i];
                    for (int j = juce::jmax (0, i - r); j <= juce::jmin (n - 1, i + r); ++j)
                        m = juce::jmax (m, edge[(size_t) j]);
                    wide[(size_t) i] = m;
                }
                for (int i = 0; i < n; ++i)
                {
                    float m = wide[(size_t) i];
                    for (int j = juce::jmax (0, i - r); j <= juce::jmin (n - 1, i + r); ++j)
                        m = juce::jmin (m, wide[(size_t) j]);
                    edge[(size_t) i] = m;
                }
            }
            return edge;
        };

        const auto lowUp = lowEdge (d.lowPos, 1.0f);
        const auto lowDown = lowEdge (d.lowNeg, -1.0f);

        // --- Agudos: pico sostenido 8 ms, encima del grave.
        const int hold = juce::jmax (1, toBins (highHoldMs));
        std::vector<float> up ((size_t) n), down ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            float hi = 0.0f, lo = 0.0f;
            for (int j = juce::jmax (0, i - hold + 1); j <= i; ++j)
            {
                hi = juce::jmax (hi, d.hiMax[from + j]);
                lo = juce::jmin (lo, d.hiMin[from + j]);
            }
            up[(size_t) i]   = lowUp[(size_t) i] + hi;
            down[(size_t) i] = -lowDown[(size_t) i] + lo;
        }

        // --- Suavizado final (gaussiano de 2,5 ms).
        points.reserve ((size_t) n + 2);
        points.push_back ({ (float) from, 0.0f, 0.0f });       // punta izquierda (se completa abajo)

        const double sigma = juce::jmax (0.5, smoothMs * 0.001 / secs);
        const int radius = juce::jmin (96, (int) std::ceil (3.0 * sigma));
        std::vector<double> weights ((size_t) radius + 1);
        for (int k = 0; k <= radius; ++k)
            weights[(size_t) k] = std::exp (-0.5 * (k / sigma) * (k / sigma));

        for (int i = 0; i < n; ++i)
        {
            double hi = 0.0, lo = 0.0, sum = 0.0;
            for (int k = -radius; k <= radius; ++k)
            {
                const auto j = (size_t) juce::jlimit (0, n - 1, i + k);
                const double w = weights[(size_t) std::abs (k)];
                hi += w * up[j];
                lo += w * down[j];
                sum += w;
            }
            points.push_back ({ (float) (from + i) + 0.5f, (float) juce::jmin (1.0, hi / sum), (float) juce::jmax (-1.0, lo / sum) });
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
    static void drawEnvelope (juce::Graphics& g, juce::Rectangle<float>, const TraceCopy& t, View view, YFunc yFor,
                              juce::Colour colour, bool filled, Mask mask, double binSeconds)
    {
        const int bins = MeteringABProcessor::scopeBins;
        juce::Path fill, lines;
        auto xFor = [&] (float b) { return view.x ((double) b / (double) bins); };

        int b = 0;
        while (b < bins)
        {
            if (! mask.includes (b)) { ++b; continue; }

            int end = b;                                   // tramo [b, end)
            while (end < bins && mask.includes (end))
                ++end;

            const auto points = smoothEnvelope ({ t.lowPos.data(), t.lowNeg.data(), t.hiMin.data(), t.hiMax.data() }, b, end, binSeconds);
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
                              View view, YFunc yFor, juce::Colour colour, Mask mask)
    {
        const int bins = MeteringABProcessor::scopeBins;
        const int columns = juce::jmax (1, (int) plot.getWidth() * 2);
        const float columnWidth = plot.getWidth() / (float) columns;
        juce::RectangleList<float> peaks;

        for (int c = 0; c < columns; ++c)
        {
            const double f0 = view.start + view.span * c / columns, f1 = view.start + view.span * (c + 1) / columns;
            const int from = juce::jlimit (0, bins - 1, (int) std::floor (f0 * bins));
            const int to   = juce::jlimit (from + 1, bins, (int) std::ceil (f1 * bins));
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

    // Con zoom: la onda real. Si en cada medio píxel entran varias muestras, sus
    // picos (como Detallado); si se acerca más, una línea que une muestra a muestra.
    template <typename YFunc>
    static void drawWave (juce::Graphics& g, juce::Rectangle<float> plot, const std::vector<std::uint32_t>& raw, int slots,
                          View view, YFunc yFor, juce::Colour colour, Mask mask)
    {
        if ((int) raw.size() < slots)
            return;

        const int bins = MeteringABProcessor::scopeBins;
        auto binOf = [&] (int slot) { return juce::jlimit (0, bins - 1, (int) ((long long) slot * bins / slots)); };
        const int columns = juce::jmax (1, (int) plot.getWidth() * 2);
        const double slotsPerColumn = view.span * slots / columns;
        g.setColour (colour);

        if (slotsPerColumn > 1.0)
        {
            juce::RectangleList<float> peaks;
            const float columnWidth = plot.getWidth() / (float) columns;

            for (int c = 0; c < columns; ++c)
            {
                const int from = juce::jlimit (0, slots - 1, (int) std::floor ((view.start + view.span * c / columns) * slots));
                const int to   = juce::jlimit (from + 1, slots, (int) std::ceil ((view.start + view.span * (c + 1) / columns) * slots));
                float lo = 1.0f, hi = -1.0f;
                bool any = false;

                for (int i = from; i < to; ++i)
                {
                    if (! mask.includes (binOf (i)))
                        continue;
                    lo = juce::jmin (lo, MeteringABProcessor::rawLo (raw[(size_t) i]));
                    hi = juce::jmax (hi, MeteringABProcessor::rawHi (raw[(size_t) i]));
                    any = true;
                }

                if (! any)
                    continue;

                const float x = plot.getX() + (float) c * columnWidth;
                const float yHi = yFor (hi), yLo = yFor (lo);
                peaks.addWithoutMerging ({ x, yHi, columnWidth + 0.35f, juce::jmax (0.8f, yLo - yHi) });
            }

            g.fillRectList (peaks);
            return;
        }

        // Muestra a muestra: una línea (se corta donde la máscara de LINK no la incluye).
        const int from = juce::jmax (0, (int) std::floor (view.start * slots) - 1);
        const int to   = juce::jmin (slots, (int) std::ceil ((view.start + view.span) * slots) + 1);
        juce::Path path;
        bool drawing = false;

        for (int i = from; i < to; ++i)
        {
            if (! mask.includes (binOf (i)))
            {
                drawing = false;
                continue;
            }

            const float x = view.x (((double) i + 0.5) / slots);
            const float lo = MeteringABProcessor::rawLo (raw[(size_t) i]), hi = MeteringABProcessor::rawHi (raw[(size_t) i]);
            const float first = yFor (lo), second = yFor (hi);

            if (! drawing) path.startNewSubPath (x, first);
            else           path.lineTo (x, first);
            if (! juce::exactlyEqual (lo, hi))
                path.lineTo (x, second);
            drawing = true;
        }

        g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    MeteringABProcessor& processor;
    std::array<TraceCopy, 4> traces;                  // [PRE, POST] de la franja de arriba, [PRE, POST] de abajo
    std::array<int, 4> traceSource { 0, 1, 0, 1 };      // de qué traza del audio sale cada una
    std::array<std::vector<std::uint32_t>, 6> rawCopy;  // detalle de muestra (para el zoom)
    int rawSlots = 1;
    bool rawLoaded = false;
    double viewStart = 0.0, viewSpan = 1.0;             // zoom: qué parte del loop se ve
    double dragStartView = 0.0;
    float dragStartX = 0.0f;
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
