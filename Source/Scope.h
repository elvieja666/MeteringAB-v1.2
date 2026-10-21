#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <map>
#include "PluginProcessor.h"
#include "Text.h"
#include "Theme.h"
#include "ClosedHandCursor.h"

// ---------------------------------------------------------------------------
//  Osciloscopio sincronizado al tempo del proyecto.
//
//  - El ancho es un largo musical (1/4 bar a 4 bars). Arriba, la grilla de
//    tiempos (0, 1/4, 2/4, 3/4, 4/4...) con subdivisiones.
//  - Barre de izquierda a derecha siguiendo la canción y, al llegar al final,
//    vuelve a empezar reemplazando lo del compás anterior.
//  - Escala vertical fija en dB: 0 arriba y abajo, -∞ en el centro.
//  - Sigue a la escucha: cada columna muestra la onda que sonaba (PRE FX o
//    POST FX, ya nivelado) cuando pasó el cabezal.
//  - Freeze (botón de arriba): congela lo que se ve.
//  - Zoom: rueda del mouse / pellizco en el trackpad (centrado en el mouse),
//    arrastrar para moverse, "Show all" (abajo) para volver a ver todo. Con zoom se ve
//    la onda real, muestra por muestra.
// ---------------------------------------------------------------------------
class ScopeView : public juce::Component, public juce::SettableTooltipClient
{
public:
    // Paletas de colores: { nombre, PRE, POST }.
    struct Palette { const char* name; juce::uint32 pre, post; };
    static constexpr std::array<Palette, 5> palettes {{
        { "Grey / Blue",         0xffa9aeb6, 0xff5aa9ff },
        { "Greys (classic)",     0xff6c7077, 0xffb8bbc1 },
        { "Blue / Pink",         0xff5aa9ff, 0xffff6fa8 },
        { "White / Red",         0xffd9d7d3, 0xffeb5757 },
        { "Violet / Lime",       0xff9b8cff, 0xffb5e655 },
    }};
    static constexpr int customPalette = (int) palettes.size();   // "Personalizado"

    explicit ScopeView (MeteringABProcessor& p) : processor (p) { capture(); }
    ~ScopeView() override { juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClick); }

    void setColours (juce::Colour pre, juce::Colour post) { preColour = pre; postColour = post; repaint(); }
    juce::Colour getPreColour() const          { return preColour; }
    juce::Colour getPostColour() const         { return postColour; }
    // Canales a mostrar.
    enum class Channels { sum = 0, left = 1, right = 2, split = 3 };
    static inline const juce::StringArray channelNames { "L + R (sum)", "L only", "R only", "L and R split" };
    void setChannels (Channels c)              { channels = c; repaint(); }
    Channels getChannels() const               { return channels; }

    void setShowGrid (bool shouldShow)         { showGrid = shouldShow; repaint(); }
    void setTripletGrid (bool on)              { tripletGrid = on; repaint(); }
    bool getTripletGrid() const                { return tripletGrid; }

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
        updateCursor();
        repaint();
    }

    // Puntero, solo con zoom: el dedo sobre lo que se clickea ("Show all" y la cajita con
    // el valor de la regla), al pasar y al clickear; mano abierta para arrastrar (cerrada
    // mientras está apretado). Sin zoom, siempre la flecha.
    bool isOverShowAll() const { return isZoomed() && zoomLabelArea.contains (hoverPoint); }
    bool isOverClickable() const { return isZoomed() && (isOverShowAll() || isOnMarkerTag()); }   // solo con zoom
    void updateCursor()
    {
        if (grabbing && isZoomed())
            setMouseCursor (closedHandCursor());
        else
            setMouseCursor (isOverClickable() ? juce::MouseCursor::PointingHandCursor
                            : isZoomed()      ? juce::MouseCursor::DraggingHandCursor
                                              : juce::MouseCursor::NormalCursor);
    }
    bool isGrabbingForTests() const { return grabbing; }

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

    // ---- Regla de nivel: línea horizontal con su valor en dB ----
    // Pasando el mouse se ve una línea con el nivel; un clic la deja fija (otro
    // clic sobre ella la saca); doble clic sobre la fija para escribir el valor.
    std::optional<float> getMarkerDb() const   { return markerDb; }

    // Línea de la ganancia aplicada (botón Gain): su nivel en dB (vacío = no se dibuja).
    void setGainLine (std::optional<float> gainDb)
    {
        if (gainDb != gainLineDb) { gainLineDb = gainDb; repaint(); }
    }
    std::optional<float> getGainLine() const   { return gainLineDb; }
    void setMarkerDb (std::optional<float> db)
    {
        if (db.has_value())
            db = juce::jlimit (-60.0f, 0.0f, -std::abs (*db));
        markerDb = db;
        updateCursor();                                 // la cajita apareció o se fue
        repaint();
    }

    // dB que corresponde a una altura del osciloscopio (según la franja: suma o L / R).
    float dbAtY (float y) const
    {
        const auto area = laneAt (y);
        const float v = std::abs (area.getCentreY() - y) / juce::jmax (1.0f, area.getHeight() * 0.5f);
        return v <= 1.0e-4f ? -120.0f : juce::jlimit (-120.0f, 0.0f, juce::Decibels::gainToDecibels (v));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverY = plotArea().contains (e.position) ? e.position.y : -1.0f;
        hoverX = e.position.x;
        hoverPoint = e.position;
        updateCursor();
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverY = -1.0f;
        hoverPoint = { -1.0f, -1.0f };
        updateCursor();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Con la caja de la regla abierta, un clic en el gráfico solo la cierra
        // (aplica lo escrito; vacía o igual, no cambia nada).
        swallowClick = isMarkerEditorOpen();
        if (swallowClick)
        {
            commitMarkerEditor (true);
            return;
        }

        dragStartView = viewStart;
        dragStartX = e.position.x;
        dragged = false;
        hoverPoint = e.position;
        hoverX = e.position.x;
        hoverY = plotArea().contains (e.position) ? e.position.y : -1.0f;
        grabbing = isZoomed() && ! isOverClickable() && ! e.mods.isPopupMenu();   // agarrar: mano cerrada
        updateCursor();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (swallowClick)
            return;
        if (e.getDistanceFromDragStart() > 3)
            dragged = true;
        if (! grabbing || ! isZoomed())                     // solo si agarró la onda (no "Show all" ni la cajita)
            return;
        setView (dragStartView - (double) (e.position.x - dragStartX) / juce::jmax (1.0f, plotArea().getWidth()) * viewSpan, viewSpan);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (std::exchange (swallowClick, false))
            return;

        if (grabbing)
        {
            grabbing = false;                               // soltar: vuelve la mano abierta
        }
        hoverPoint = e.position;
        hoverX = e.position.x;
        hoverY = plotArea().contains (e.position) ? e.position.y : -1.0f;
        updateCursor();
        if (dragged || e.getNumberOfClicks() > 1 || e.mods.isPopupMenu())
            return;

        if (zoomLabelArea.contains (e.position))           // "Show all" (con zoom)
        {
            resetZoom();
            return;
        }

        if (! plotArea().contains (e.position))
            return;

        markerBeforeClick = markerDb;
        if (isOnMarkerTag() || isOnMarker (e.position.y))   // clic sobre la línea o su cajita: la saca
                                                            // (doble clic: vuelve y abre la caja para el valor)
            setMarkerDb (std::nullopt);                     // clic sobre la línea fija: la saca
        else
            setMarkerDb (dbAtY (e.position.y));              // clic: deja la línea fija ahí
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! plotArea().contains (e.position))
            return;
        if (! markerDb.has_value())                          // el primer clic la había sacado: vuelve
            markerDb = markerBeforeClick.has_value() ? markerBeforeClick : std::optional<float> (dbAtY (e.position.y));
        showMarkerEditor();
    }

    // Escribir el valor exacto (ej. -6, -6.5 dB).
    void showMarkerEditor()
    {
        if (! markerDb.has_value() || isMarkerEditorOpen())
            return;

        markerEditor = std::make_unique<juce::TextEditor>();
        auto& ed = *markerEditor;
        ed.setFont (Theme::font (12.0f, 500));
        ed.setJustification (juce::Justification::centred);
        ed.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1e23));
        ed.setColour (juce::TextEditor::textColourId, markerColour);
        ed.setColour (juce::TextEditor::outlineColourId, markerColour.withAlpha (0.6f));
        ed.setColour (juce::TextEditor::focusedOutlineColourId, markerColour);
        ed.setText (juce::String (*markerDb, 1), false);
        ed.selectAll();
        ed.onReturnKey = [this] { commitMarkerEditor (true); };
        ed.onEscapeKey = [this] { commitMarkerEditor (false); };
        ed.onFocusLost = [this] { commitMarkerEditor (true); };
        addAndMakeVisible (ed);
        ed.setBounds (markerTagArea().toNearestInt().expanded (6, 3));
        ed.grabKeyboardFocus();
        // Clic en cualquier otro lado (el gráfico, un botón...): se cierra la caja.
        juce::Desktop::getInstance().addGlobalMouseListener (&outsideClick);
        repaint();
    }

    bool isMarkerEditorOpen() const { return markerEditor != nullptr && markerEditor->isVisible(); }
    void clickElsewhereForTests (juce::Component& other)       // clic en otra parte del plugin
    {
        const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &other, &other, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false);
        outsideClick.mouseDown (e);
    }

    bool isHoverLineShownForTests() const
    {
        const auto plot = plotArea();
        return hoverY >= plot.getY() && hoverY <= plot.getBottom() && ! isOnMarker (hoverY) && ! isOnMarkerTag() && ! isOverShowAll();
    }
    juce::Rectangle<float> getMarkerTagAreaForTests() const { return markerTagArea(); }

    bool isShowAllAt (juce::Point<float> p) const { return zoomLabelArea.contains (p); }
    void typeMarkerForTests (const juce::String& text)
    {
        if (markerEditor != nullptr) { markerEditor->setText (text); commitMarkerEditor (true); }
    }

    std::uint32_t rawCopyForTests (int source, int slot) const
    {
        const auto& r = rawCopy[(size_t) source];
        return slot >= 0 && slot < (int) r.size() ? r[(size_t) slot] : 0u;
    }

    // Escribir y apretar Escape (y lo que pasa después: el cuadro pierde el foco).
    void typeAndEscapeMarkerForTests (const juce::String& text)
    {
        if (markerEditor == nullptr) return;
        auto* ed = markerEditor.get();
        ed->setText (text);
        if (ed->onEscapeKey) ed->onEscapeKey();
        if (ed->onFocusLost) ed->onFocusLost();
    }

    void commitMarkerEditor (bool apply)
    {
        if (markerEditor == nullptr)
            return;
        const auto text = markerEditor->getText();
        // Al ocultarlo pierde el foco: sin esto, Escape terminaba aplicando lo escrito.
        markerEditor->onReturnKey = nullptr;
        markerEditor->onEscapeKey = nullptr;
        markerEditor->onFocusLost = nullptr;
        juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClick);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ScopeView> (this)]
                                         { if (safe != nullptr) safe->markerEditor.reset(); });
        markerEditor->setVisible (false);
        if (apply)
        {
            const auto number = text.retainCharacters ("-.,0123456789").replaceCharacter (',', '.');
            if (number.containsAnyOf ("0123456789"))
                setMarkerDb (number.getFloatValue());
        }
        repaint();
    }

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
        const View view { viewStart, viewSpan, plot.getX(), plot.getWidth() };

        // Con zoom: ¿cuánto detalle hay? (casilleros visibles por píxel)
        const double visibleSlots = viewSpan * rawSlots;
        const bool rawUseful = isZoomed() && rawLoaded && visibleSlots <= 256.0 * plot.getWidth();

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

            // Con zoom, la onda real muestra por muestra; si no, el pico de cada columna.
            auto drawSignal = [&] (int index, juce::Colour colour, Mask mask)
            {
                const auto& t = traces[(size_t) index];
                const auto& raw = rawCopy[(size_t) traceSource[(size_t) index]];

                if (rawUseful)
                    drawWave (g, area, raw, rawSlots, view, yFor, colour, mask);
                else
                    drawDetailed (g, area, t.min, t.max, view, yFor, colour, mask);
            };

            // Sigue a la escucha: cada columna muestra lo que sonaba (PRE donde
            // escuchabas PRE y POST donde escuchabas POST).
            if (showPre != showPost)
            {
                drawSignal (preIndex,  preColour,  Mask { &showPreAt, 1 });
                drawSignal (postIndex, postColour, Mask { &showPreAt, 0 });
            }
            else
            {
                if (showPre)
                    drawSignal (preIndex, preColour, {});

                if (showPost)
                    drawSignal (postIndex, postColour, {});    // POST encima
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

        drawLevelRuler (g, plot);

        // Estado (la leyenda ahora son los botones PRE FX / POST FX de arriba)
        g.setFont (Theme::font (11.0f, 400));

        if (! showPre && ! showPost)
        {
            g.setColour (juce::Colour (0xff8a8f98));
            g.drawText ("Both waves hidden", plot.toNearestInt(), juce::Justification::centred);
        }

        // Con zoom: qué parte del loop se ve (barrita abajo) y cuánto zoom hay.
        zoomLabelArea = {};
        if (isZoomed())
        {
            const auto track = juce::Rectangle<float> (plot.getX(), plot.getBottom() - 4.0f, plot.getWidth(), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (track, 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.fillRoundedRectangle (track.withX (track.getX() + (float) viewStart * track.getWidth())
                                         .withWidth (juce::jmax (4.0f, (float) viewSpan * track.getWidth())), 1.5f);

            const double factor = 1.0 / viewSpan;
            const auto text = "Zoom x" + (factor < 10.0 ? juce::String (factor, 1) : juce::String (juce::roundToInt (factor))) + juce::String::fromUTF8 ("  \xc2\xb7  ");
            const auto area = juce::Rectangle<float> (plot.getX() + 8.0f, plot.getBottom() - 24.0f, 280.0f, 16.0f);
            g.setColour (juce::Colour (0xffe8e6e3).withAlpha (0.75f));
            g.drawText (text, area.toNearestInt(), juce::Justification::centredLeft);
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
            // "Show all": se puede clickear (manito); con el mouse encima, más claro y subrayado.
            const juce::String showAll ("Show all");
            const float sw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), showAll);
            zoomLabelArea = area.withTrimmedLeft (w).withWidth (sw + 2.0f).expanded (4.0f, 4.0f);
            const bool hot = zoomLabelArea.contains (hoverPoint);
            const auto textArea = area.withTrimmedLeft (w).withWidth (sw + 4.0f);
            g.setColour (hot ? Theme::accentBright.brighter (0.4f) : Theme::accentBright);
            g.drawText (showAll, textArea.toNearestInt(), juce::Justification::centredLeft);   // clic: ver todo
            if (hot)
                g.fillRect (textArea.getX(), textArea.getCentreY() + 7.0f, sw, 1.0f);
        }

        if (frozen)
        {
            g.setColour (juce::Colour (0xffe8e6e3));
            g.drawText ("FROZEN", juce::Rectangle<float> (plot.getRight() - 110.0f, plot.getY() + 6.0f, 104.0f, 14.0f).toNearestInt(),
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
            const int margin = 2;
            const int from = juce::jmax (0, (int) std::floor (viewStart * rawSlots) - margin);
            const int to   = juce::jmin (rawSlots, (int) std::ceil ((viewStart + viewSpan) * rawSlots) + margin);
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
        using P = MeteringABProcessor;
        const auto& src = processor.scopeRaw[(size_t) source];
        auto& dst = rawCopy[(size_t) source];
        if (src == nullptr)
            return;
        if ((int) dst.size() < P::scopeRawCapacity)
            dst.assign ((size_t) P::scopeRawCapacity, 0u);

        // Casilleros de columnas sin datos (recién dado play, o cambió el largo): vacíos.
        const int slots = juce::jmax (1, rawSlots);
        constexpr int bins = P::scopeBins;
        int lastBin = -1;
        bool hasData = false;
        for (int i = from; i < to; ++i)
        {
            const int bin = juce::jlimit (0, bins - 1, (int) ((long long) i * bins / slots));
            if (bin != lastBin)
            {
                lastBin = bin;
                hasData = processor.scopeCount[(size_t) bin].load (std::memory_order_relaxed) > 0;
            }
            dst[(size_t) i] = hasData ? src[(size_t) i].load (std::memory_order_relaxed) : 0u;
        }
    }

    // Franja (suma, o L arriba / R abajo) que corresponde a una altura.
    juce::Rectangle<float> laneArea (int lane) const
    {
        const auto plot = plotArea();
        if (channels != Channels::split)
            return plot;
        return lane == 0 ? plot.withHeight (plot.getHeight() * 0.5f - 4.0f)
                         : plot.withTrimmedTop (plot.getHeight() * 0.5f + 4.0f);
    }
    juce::Rectangle<float> laneAt (float y) const
    {
        return channels == Channels::split && y > plotArea().getCentreY() ? laneArea (1) : laneArea (0);
    }

    // Alturas (arriba y abajo de -∞, en cada franja) de un nivel en dB.
    std::vector<float> yForDb (float db) const
    {
        std::vector<float> ys;
        const float v = juce::Decibels::decibelsToGain (db);
        for (int lane = 0; lane < (channels == Channels::split ? 2 : 1); ++lane)
        {
            const auto a = laneArea (lane);
            ys.push_back (a.getCentreY() - v * a.getHeight() * 0.5f);
            ys.push_back (a.getCentreY() + v * a.getHeight() * 0.5f);
        }
        return ys;
    }

    bool isOnMarker (float y) const
    {
        if (! markerDb.has_value())
            return false;
        for (float my : yForDb (*markerDb))
            if (std::abs (my - y) <= 5.0f)
                return true;
        return false;
    }

    // Dónde va el cartelito con el valor de la línea fija (a la derecha, sobre la línea de arriba).
    juce::Rectangle<float> markerTagArea() const
    {
        const auto plot = plotArea();
        const float y = markerDb.has_value() ? yForDb (*markerDb).front() : plot.getY();
        return juce::Rectangle<float> (plot.getRight() - 78.0f, y - 9.0f, 72.0f, 18.0f)
                   .withY (juce::jlimit (plot.getY(), plot.getBottom() - 18.0f, y - 9.0f));
    }

    // ¿El mouse está sobre el cartelito de la línea fija? (ahí no se dibuja la del mouse)
    bool isOnMarkerTag() const
    {
        return markerDb.has_value() && hoverY >= 0.0f && markerTagArea().expanded (3.0f).contains (hoverX, hoverY);
    }

    static juce::String dbText (float db)
    {
        return db <= -119.0f ? juce::String::fromUTF8 ("-\xe2\x88\x9e dB") : juce::String (db, 1) + " dB";
    }

    // Línea del mouse y línea fija, con su valor.
    void drawLevelRuler (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.expanded (0.0f, 1.0f).toNearestInt());
        g.setFont (Theme::font (11.0f, 600));

        auto tag = [&] (float y, const juce::String& text, juce::Colour fill, juce::Colour ink)
        {
            const auto r = juce::Rectangle<float> (plot.getRight() - 78.0f, y - 9.0f, 72.0f, 18.0f)
                               .withY (juce::jlimit (plot.getY(), plot.getBottom() - 18.0f, y - 9.0f));
            g.setColour (fill);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (ink);
            g.drawText (text, r.toNearestInt(), juce::Justification::centred);
        };

        if (gainLineDb.has_value())                       // ganancia aplicada: solo la línea blanca punteada, a -|ganancia| dB
        {
            const float level = juce::jlimit (-60.0f, 0.0f, -std::abs (*gainLineDb));
            const auto ys = yForDb (level);
            g.setColour (gainColour.withAlpha (0.9f));
            const float dash[] = { 8.0f, 4.0f };
            for (float y : ys)
                g.drawDashedLine (juce::Line<float> (plot.getX(), y, plot.getRight(), y), dash, 2, 1.3f);
        }

        if (markerDb.has_value())
        {
            const auto ys = yForDb (*markerDb);
            g.setColour (markerColour.withAlpha (0.9f));         // gris claro (lleno; la de Gain es punteada)
            for (float y : ys)
                g.fillRect (plot.getX(), y - 0.5f, plot.getWidth(), 1.2f);
            if (markerEditor == nullptr || ! markerEditor->isVisible())
                tag (ys.front(), dbText (*markerDb), markerColour, juce::Colour (0xff0b0c0e));
        }

        if (hoverY >= plot.getY() && hoverY <= plot.getBottom() && ! isOnMarker (hoverY) && ! isOnMarkerTag() && ! isOverShowAll())
        {
            const float db = dbAtY (hoverY);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            const float dash[] = { 4.0f, 3.0f };
            g.drawDashedLine (juce::Line<float> (plot.getX(), hoverY, plot.getRight(), hoverY), dash, 2, 1.0f);
            tag (hoverY, dbText (db), juce::Colour (0xff2a2e35), juce::Colour (0xffe8e6e3));
        }
    }

public:
    // Dónde se dibuja la onda (sin los márgenes de las escalas).
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (38.0f).withTrimmedTop (20.0f).withTrimmedRight (10.0f).withTrimmedBottom (8.0f);
    }

    // Zoom máximo: que se vean al menos ~48 muestras.
    double minViewSpan() const
    {
        // Congelado: lo capturado (si cambia el tempo mientras tanto, no el actual).
        const int live = processor.scopeRawSlots.load (std::memory_order_relaxed);
        const int slots = juce::jlimit (1, MeteringABProcessor::scopeRawCapacity, frozen ? rawSlots : live);
        return juce::jlimit (1.0e-5, 1.0, 48.0 / (double) slots);
    }

    // Grilla de tiempos: líneas por tiempo y subdivisiones, con etiquetas k/n.
    // Con zoom las subdivisiones se van achicando (corcheas, semis, fusas...).
    void drawTimeGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        const double totalBeats = lengthBars * beatsPerBar;
        // Normal: 16avos (4 por tiempo). Tresillos: 3 por tiempo (corcheas en tresillo).
        int subdivisions = tripletGrid ? (totalBeats <= 16.0 ? 3 : 1) : (totalBeats <= 8.0 ? 4 : 1);
        const double pixelsPerBeat = plot.getWidth() / (totalBeats * viewSpan);
        const int maxSubdivisions = tripletGrid ? 6 : 256;               // tresillos: hasta tresillos de semicorchea
        while (subdivisions * 2 <= maxSubdivisions && pixelsPerBeat / (subdivisions * 2) >= 14.0 && isZoomed())
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
                for (int f : { 2, 3 })
                    while (num % f == 0 && den % f == 0) { num /= f; den /= f; }
                const bool fits = tripletGrid ? (den % 3 == 0 && den <= beatsPerBar * 6)   // solo posiciones de tresillo
                                              : den <= beatsPerBar * 4;                     // hasta semicorcheas
                if (fits)
                    text = juce::String (num) + "/" + juce::String (den);
            }

            if (! wantsLabel || text.isEmpty())
                continue;
            const float textWidth = labelWidth (g.getCurrentFont(), text);
            if (x - textWidth * 0.5f - lastLabelX >= 10.0f)
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

    // Ancho de una etiqueta de la grilla (medir texto es caro: se guarda; son pocas y se repiten).
    float labelWidth (const juce::Font& font, const juce::String& text) const
    {
        if (auto it = labelWidths.find (text); it != labelWidths.end())
            return it->second;
        if (labelWidths.size() > 512)
            labelWidths.clear();
        return labelWidths[text] = juce::GlyphArrangement::getStringWidth (font, text);
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
    struct TraceCopy { Bins min {}, max {}, sq {}; };

    // Máscara opcional: con LINK, qué columnas dibujar de cada onda.
    using Flags = std::array<std::uint8_t, MeteringABProcessor::scopeBins>;
    struct Mask { const Flags* flags = nullptr; std::uint8_t wanted = 0;
                  bool includes (int b) const { return flags == nullptr || (*flags)[(size_t) b] == wanted; } };

private:
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
    std::array<std::vector<std::uint32_t>, 6> rawCopy;
  // detalle de muestra (para el zoom)
    int rawSlots = 1;
    bool rawLoaded = false;
    double viewStart = 0.0, viewSpan = 1.0;             // zoom: qué parte del loop se ve
    double dragStartView = 0.0;
    float dragStartX = 0.0f;
    bool dragged = false;
    float hoverY = -1.0f;                               // regla: altura del mouse (-1 = afuera)
    float hoverX = -1.0f;
    std::optional<float> markerDb, markerBeforeClick;   // regla: línea fija (en dB)
    std::unique_ptr<juce::TextEditor> markerEditor;
    static inline const juce::Colour markerColour { 0xffd3d6db };   // gris claro
    std::optional<float> gainLineDb;                    // línea de la ganancia aplicada
    static inline const juce::Colour gainColour { 0xffeceef1 };   // blanco
    juce::Rectangle<float> zoomLabelArea;               // "Show all" (con zoom)
    mutable std::map<juce::String, float> labelWidths;  // anchos de las etiquetas de la grilla (misma letra siempre)
    juce::Point<float> hoverPoint { -1.0f, -1.0f };     // dónde está el mouse (para "Show all")
    bool grabbing = false;                              // apretado con zoom (mano cerrada)
    bool swallowClick = false;                          // el clic que cerró la caja de la regla

    // Clic fuera de la caja de la regla (en otra parte del plugin): la cierra.
    struct OutsideClick : juce::MouseListener
    {
        explicit OutsideClick (ScopeView& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* ed = owner.markerEditor.get();
            if (! owner.isMarkerEditorOpen() || e.eventComponent == ed || (ed != nullptr && ed->isParentOf (e.eventComponent)))
                return;
            owner.commitMarkerEditor (true);
        }
        ScopeView& owner;
    } outsideClick { *this };
    Channels channels = Channels::sum;
    Flags showPreAt {};
    juce::Colour preColour { palettes[0].pre }, postColour { palettes[0].post };
    bool showGrid = true;
    bool tripletGrid = false;                           // grilla en tresillos (3 por tiempo)
    int currentBin = 0;
    double lengthBars = 1.0;
    int beatsPerBar = 4;
    bool frozen = false;
    bool showPre = true, showPost = true;
};
