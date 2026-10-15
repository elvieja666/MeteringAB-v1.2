#include "PluginEditor.h"
#include "Text.h"

// Selector de color que avisa en vivo mientras lo movés.
class LiveColourSelector : public juce::ColourSelector, private juce::ChangeListener
{
public:
    explicit LiveColourSelector (juce::Colour initial, std::function<void (juce::Colour)> callback)
        : juce::ColourSelector (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders),
          onColour (std::move (callback))
    {
        setCurrentColour (initial);
        setSize (300, 260);
        addChangeListener (this);
    }

    ~LiveColourSelector() override { removeChangeListener (this); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { if (onColour) onColour (getCurrentColour()); }
    std::function<void (juce::Colour)> onColour;
};

// Colores de la parte de arriba (clara). El osciloscopio usa los de Theme oscuros.
namespace Colours
{
    const juce::Colour background = Theme::topLight;
    const juce::Colour text       = Theme::ink;
    const juce::Colour dim        = Theme::inkDim;
    const juce::Colour accent     = Theme::teal;
    const juce::Colour good       = Theme::good;
    const juce::Colour bad        = Theme::bad;
}

// ---------------------------------------------------------------------------
//  PreciseSlider
// ---------------------------------------------------------------------------
PreciseSlider::PreciseSlider()
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::TextBoxBelow, false, 124, 22);
    setTextBoxIsEditable (true);                 // <- clic en el número para escribir
    setWantsKeyboardFocus (true);
}

void PreciseSlider::mouseDown (const juce::MouseEvent& e)
{
    // Con Shift, hace falta arrastrar 10 veces más: ajuste fino.
    setMouseDragSensitivity (e.mods.isShiftDown() ? 2500 : 250);
    juce::Slider::mouseDown (e);
}

// ---------------------------------------------------------------------------
//  Look & feel
// ---------------------------------------------------------------------------
MeteringABLookAndFeel::MeteringABLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Theme::topLight);
    setColour (juce::Slider::textBoxTextColourId, Theme::inkStrong);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Theme::teal.withAlpha (0.25f));
    setColour (juce::TextEditor::backgroundColourId, Theme::well);
    setColour (juce::TextEditor::textColourId, Theme::inkStrong);
    setColour (juce::TextEditor::highlightColourId, Theme::teal.withAlpha (0.25f));
    setColour (juce::TextEditor::highlightedTextColourId, Theme::inkStrong);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::teal);
    setColour (juce::CaretComponent::caretColourId, Theme::teal);
    setColour (juce::ComboBox::textColourId, Theme::ink);
    setColour (juce::ComboBox::arrowColourId, Theme::ink);
    setColour (juce::PopupMenu::backgroundColourId, Theme::well);
    setColour (juce::PopupMenu::textColourId, Theme::ink);
    setColour (juce::PopupMenu::headerTextColourId, Theme::inkDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::teal);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, Theme::raisedBot);
    setColour (juce::TextButton::buttonOnColourId, Theme::raisedBot);
    setColour (juce::TextButton::textColourOffId, Theme::ink);
    setColour (juce::TextButton::textColourOnId, Theme::teal);
    setColour (juce::Label::textColourId, Theme::ink);
    setColour (juce::TooltipWindow::backgroundColourId, Theme::ink);
    setColour (juce::TooltipWindow::textColourId, Theme::well);
    setColour (juce::TooltipWindow::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::BubbleComponent::backgroundColourId, Theme::well);
    setColour (juce::BubbleComponent::outlineColourId, Theme::track);
}

juce::Font MeteringABLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (button.getProperties().contains ("big"))
        return Theme::font (22.0f, 600, 0.05f);

    if (button.getProperties()["kind"].toString() == "plain")
        return Theme::font (button.getProperties().contains ("open") ? 14.0f : 16.0f, 300);

    return Theme::font (juce::jmin (14.0f, (float) buttonHeight * 0.5f), 400);
}

void MeteringABLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down)
{
    const auto kind = button.getProperties()["kind"].toString();
    auto r = button.getLocalBounds().toFloat();

    if (kind == "plain")
        return;

    if (kind == "chip")                              // pastilla oscura del osciloscopio
    {
        r = r.reduced (0.5f);
        const float radius = r.getHeight() * 0.5f;
        const bool on = button.getToggleState();
        const auto colour = juce::Colour ((juce::uint32) (juce::int64) button.getProperties().getWithDefault ("chipColour", (juce::int64) Theme::tealBright.getARGB()));

        g.setColour (on ? colour.withAlpha (0.16f).overlaidWith (juce::Colours::transparentBlack) : Theme::darkBg.brighter (over ? 0.06f : 0.0f));
        g.fillRoundedRectangle (r, radius);
        g.setColour (on ? colour.withAlpha (0.6f) : Theme::chipLine.brighter (over ? 0.25f : 0.0f));
        g.drawRoundedRectangle (r, radius, 1.0f);
        return;
    }

    if (kind == "segment")                           // ms | Sync: la elegida en relieve
    {
        if (button.getToggleState())
            Theme::drawRaised (g, r.reduced (2.0f), (r.getHeight() - 4.0f) * 0.5f, false, false);
        return;
    }

    // raised (por defecto)
    const float radius = button.getProperties().contains ("big") ? 18.0f : (r.getHeight() - 6.0f) * 0.5f;
    Theme::drawRaised (g, r.reduced (5.0f, 2.0f).withTrimmedBottom (4.0f), radius, over, down || button.getToggleState());
}

void MeteringABLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto kind = button.getProperties()["kind"].toString();
    auto r = button.getLocalBounds();
    juce::Colour colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);

    if (kind == "chip")
    {
        const auto chipColour = juce::Colour ((juce::uint32) (juce::int64) button.getProperties().getWithDefault ("chipColour", (juce::int64) Theme::tealBright.getARGB()));
        colour = button.getToggleState() ? chipColour.brighter (0.25f) : Theme::chipText;
    }
    else if (kind == "segment")
    {
        colour = button.getToggleState() ? Theme::teal : Theme::inkDim;
    }
    else if (kind == "plain" && button.getProperties().contains ("open"))
    {
        // Triángulo ▸ / ▾ dibujado (más prolijo que un carácter).
        const bool open = button.getProperties()["open"];
        auto tri = r.removeFromLeft (22).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
        juce::Path p;
        if (open) p.addTriangle (tri.getX(), tri.getY() + 1.5f, tri.getRight(), tri.getY() + 1.5f, tri.getCentreX(), tri.getBottom() - 0.5f);
        else      p.addTriangle (tri.getX() + 1.5f, tri.getY(), tri.getX() + 1.5f, tri.getBottom(), tri.getRight() - 0.5f, tri.getCentreY());
        g.setColour (colour);
        g.fillPath (p);
    }

    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.5f);

    g.setColour (colour);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    const bool down = button.isDown() && kind == "raised";
    g.drawFittedText (button.getButtonText(), r.translated (0, down ? 1 : 0),
                      kind == "plain" && button.getProperties().contains ("open") ? juce::Justification::centredLeft
                                                                                  : juce::Justification::centred, 1);
}

void MeteringABLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const bool dark = box.getProperties()["kind"].toString() == "chip";

    if (dark)
    {
        g.setColour (Theme::darkBg.brighter (box.isMouseOver (true) ? 0.06f : 0.0f));
        g.fillRoundedRectangle (r, radius);
        g.setColour (Theme::chipLine.brighter (box.isMouseOver (true) ? 0.25f : 0.0f));
        g.drawRoundedRectangle (r, radius, 1.0f);
    }
    else
    {
        g.setColour (Theme::pill.darker (box.isMouseOver (true) ? 0.03f : 0.0f));
        g.fillRoundedRectangle (r, radius);
    }

    // Flechita ⌄
    const float cx = (float) w - radius - 4.0f, cy = (float) h * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.5f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.5f, cy - 2.0f);
    g.setColour (dark ? Theme::chipText : Theme::ink);
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font MeteringABLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return Theme::font (box.getProperties()["kind"].toString() == "chip" ? 12.5f : 14.0f, 400);
}

void MeteringABLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const int pad = box.getHeight() / 2 + 2;
    label.setBounds (pad - 4, 1, box.getWidth() - pad - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, box.getProperties()["kind"].toString() == "chip" ? Theme::chipText : Theme::ink);
}

juce::Font MeteringABLookAndFeel::getPopupMenuFont()
{
    return Theme::font (14.0f, 400);
}

juce::Label* MeteringABLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->getProperties().set ("well", true);
    label->setFont (Theme::font (14.5f, 400));
    label->setColour (juce::Label::textColourId, Theme::inkStrong);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundWhenEditingColourId, Theme::well);
    label->setColour (juce::Label::textWhenEditingColourId, Theme::inkStrong);
    label->setColour (juce::Label::outlineWhenEditingColourId, Theme::teal);
    return label;
}

void MeteringABLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (label.getProperties()["well"] && ! label.isBeingEdited())
    {
        // Casillero hundido con el valor (se puede hacer clic para escribir).
        auto r = label.getLocalBounds().toFloat().reduced (1.0f, 0.0f);
        Theme::drawWell (g, r, r.getHeight() * 0.5f);
        g.setColour (label.findColour (juce::Label::textColourId));
        g.setFont (getLabelFont (label));
        g.drawFittedText (label.getText(), label.getLocalBounds(), juce::Justification::centred, 1);
        return;
    }

    LookAndFeel_V4::drawLabel (g, label);
}

juce::Font MeteringABLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

void MeteringABLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = startAngle + pos * (endAngle - startAngle);
    const float track = 5.0f;

    // Arco de fondo y arco del valor (turquesa).
    juce::Path back;
    back.addCentredArc (centre.x, centre.y, radius - track * 0.5f, radius - track * 0.5f, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::track);
    g.strokePath (back, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - track * 0.5f, radius - track * 0.5f, 0.0f, startAngle, angle, true);
    g.setColour (Theme::teal.brighter (0.15f));
    g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Perilla clara en relieve.
    const auto knobRadius = radius - track - 6.0f;
    juce::Path knob;
    knob.addEllipse (centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2, knobRadius * 2);
    juce::DropShadow (juce::Colours::black.withAlpha (0.25f), 7, { 0, 2 }).drawForPath (g, knob);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffafbfb), centre.x, centre.y - knobRadius,
                                             juce::Colour (0xffe0e3e6), centre.x, centre.y + knobRadius, false));
    g.fillPath (knob);

    // Rayita que marca la posición.
    g.setColour (Theme::ink);
    g.drawLine ({ centre.getPointOnCircumference (knobRadius * 0.45f, angle), centre.getPointOnCircumference (knobRadius * 0.85f, angle) }, 2.6f);
}

// ---------------------------------------------------------------------------
//  Editor
// ---------------------------------------------------------------------------
MeteringABEditor::MeteringABEditor (MeteringABProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);
    auto& apvts = processor.apvts;

    auto addCombo = [&] (juce::ComboBox& box, const char* paramId, std::unique_ptr<ComboAttachment>& att)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramId)))
            box.addItemList (choice->choices, 1);

        addAndMakeVisible (box);
        att = std::make_unique<ComboAttachment> (apvts, paramId, box);
    };

    addCombo (modeBox, ParamIDs::mode, modeAtt);


    // Link ID: pastilla "−  1  +" (el número se puede escribir con un clic).
    linkSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    linkSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addChildComponent (linkSlider);                   // invisible: solo guarda el valor
    linkAtt = std::make_unique<SliderAttachment> (apvts, ParamIDs::link, linkSlider);

    linkLabel.setText ("Link ID", juce::dontSendNotification);
    linkLabel.setColour (juce::Label::textColourId, Colours::dim);
    linkLabel.setFont (Theme::font (13.0f, 300));
    linkLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (linkLabel);

    auto stepLink = [this] (int delta)
    {
        linkSlider.setValue (juce::jlimit (linkSlider.getMinimum(), linkSlider.getMaximum(), linkSlider.getValue() + delta),
                             juce::sendNotificationSync);
    };

    for (auto* b : { &linkMinus, &linkPlus })
    {
        b->getProperties().set ("kind", "plain");
        b->setColour (juce::TextButton::textColourOffId, Theme::ink);
        addAndMakeVisible (*b);
    }

    linkMinus.onClick = [stepLink] { stepLink (-1); };
    linkPlus.onClick  = [stepLink] { stepLink (+1); };

    linkValueLabel.setEditable (true);
    linkValueLabel.setJustificationType (juce::Justification::centred);
    linkValueLabel.setFont (Theme::font (14.0f, 500));
    linkValueLabel.setColour (juce::Label::textColourId, Theme::ink);
    linkValueLabel.onTextChange = [this]
    {
        const int typed = linkValueLabel.getText().retainCharacters ("0123456789").getIntValue();
        if (typed > 0)
            linkSlider.setValue (juce::jlimit (linkSlider.getMinimum(), linkSlider.getMaximum(), (double) typed), juce::sendNotificationSync);
    };
    addAndMakeVisible (linkValueLabel);

    setupKnob (windowSlider,  windowLabel,  "Ventana");
    // Latencia: sin perilla, solo el botón Medir y el valor medido.
    latencyLabel.setText ("Latencia", juce::dontSendNotification);
    latencyLabel.setJustificationType (juce::Justification::centred);
    latencyLabel.setColour (juce::Label::textColourId, Colours::dim);
    latencyLabel.setFont (Theme::font (12.5f, 300));
    addAndMakeVisible (latencyLabel);

    // El número de latencia se dibuja en paint (número grande + "muestras" chico).

    latencyMsLabel.setJustificationType (juce::Justification::centred);
    latencyMsLabel.setFont (Theme::font (11.5f, 300));
    latencyMsLabel.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (latencyMsLabel);

    windowAtt  = std::make_unique<SliderAttachment> (apvts, ParamIDs::windowMs, windowSlider);

    // Doble clic vuelve al valor por defecto.
    windowSlider.setDoubleClickReturnValue (true, 3000.0);

    // Ventana: el valor va a la derecha de la perilla (ocupa menos alto).
    for (auto* ws : { &windowSlider, &windowSyncSlider })
        ws->setTextBoxStyle (juce::Slider::TextBoxRight, false, 104, 26);

    // Ventana en compases (modo Sync): misma perilla, valores musicales.
    addAndMakeVisible (windowSyncSlider);
    windowSyncAtt = std::make_unique<SliderAttachment> (apvts, ParamIDs::windowSync, windowSyncSlider);
    windowSyncSlider.setDoubleClickReturnValue (true, (double) SyncDivisions::defaultIndex);

    // Botones ms | Sync
    syncButtons[0].setButtonText ("ms");
    syncButtons[1].setButtonText ("Sync");
    syncButtons[0].setConnectedEdges (juce::Button::ConnectedOnRight);
    syncButtons[1].setConnectedEdges (juce::Button::ConnectedOnLeft);

    for (int i = 0; i < 2; ++i)
    {
        syncButtons[i].getProperties().set ("kind", "segment");
        syncButtons[i].setClickingTogglesState (false);
        syncButtons[i].onClick = [this, i] { setSync (i == 1); };
        addAndMakeVisible (syncButtons[i]);
    }

    windowInfoLabel.setJustificationType (juce::Justification::centred);
    windowInfoLabel.setColour (juce::Label::textColourId, Colours::dim);
    windowInfoLabel.setFont (Theme::font (11.0f, 300));
    addAndMakeVisible (windowInfoLabel);

    // Botón único PRE FX / POST FX (cada clic alterna).
    abButton.getProperties().set ("big", true);
    abButton.setClickingTogglesState (false);
    abButton.onClick = [this] { toggleAB(); };
    addAndMakeVisible (abButton);

    matchInfoLabel.setText ("Nivelado en LUFS (filtro K)"_u, juce::dontSendNotification);
    matchInfoLabel.setJustificationType (juce::Justification::centred);
    matchInfoLabel.setColour (juce::Label::textColourId, Colours::dim);
    matchInfoLabel.setFont (Theme::font (11.0f, 300));
    addAndMakeVisible (matchInfoLabel);

    listenLabel.setText ("Escuchar  ·  clic para alternar"_u, juce::dontSendNotification);
    listenLabel.setColour (juce::Label::textColourId, Colours::dim);
    listenLabel.setFont (Theme::font (12.5f, 300));
    addAndMakeVisible (listenLabel);

    // Medir latencia
    measureButton.setButtonText ("Medir");
    measureButton.onClick = [this] { processor.startLatencyMeasurement(); };
    addAndMakeVisible (measureButton);

    // Ø: compensar un plugin que invierte la polaridad (lo pone solo "Medir").
    polarityButton.setClickingTogglesState (true);
    polarityButton.setTooltip ("Polaridad invertida");
    polarityButton.setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    addAndMakeVisible (polarityButton);
    polarityAtt = std::make_unique<ButtonAttachment> (apvts, ParamIDs::polarity, polarityButton);

    latencyInfoLabel.setJustificationType (juce::Justification::centred);
    latencyInfoLabel.setFont (Theme::font (11.0f, 400));
    addAndMakeVisible (latencyInfoLabel);


    // Osciloscopio sincronizado + largo en compases (se guarda con el proyecto).
    addAndMakeVisible (scope);
    scopeLabel.setText ({}, juce::dontSendNotification);
    scope.setTooltip ("Clic para congelar / descongelar");
    scopeLabel.setColour (juce::Label::textColourId, Theme::darkDim);
    scopeLabel.setFont (Theme::font (12.0f, 300));
    addAndMakeVisible (scopeLabel);

    zoomBox.addItemList (ScopeLengths::names, 1);
    const int savedLength = juce::jlimit (0, (int) ScopeLengths::bars.size() - 1,
                                          (int) apvts.state.getProperty ("scopeLength", ScopeLengths::defaultIndex));
    processor.scopeLengthIndex = savedLength;
    zoomBox.setSelectedId (savedLength + 1, juce::dontSendNotification);
    zoomBox.onChange = [this]
    {
        const int index = zoomBox.getSelectedId() - 1;
        processor.scopeLengthIndex = index;
        processor.apvts.state.setProperty ("scopeLength", index, nullptr);
    };
    addAndMakeVisible (zoomBox);

    // Botones para mostrar u ocultar cada onda (se guardan con el proyecto).
    scope.setShowPre  ((bool) apvts.state.getProperty ("scopeShowPre",  true));
    scope.setShowPost ((bool) apvts.state.getProperty ("scopeShowPost", true));

    for (auto* b : { &showPreButton, &showPostButton, &linkButton, &scopeStyleButton })
        b->getProperties().set ("kind", "chip");

    zoomBox.getProperties().set ("kind", "chip");

    for (auto* b : { &showPreButton, &showPostButton })
    {
        b->setClickingTogglesState (false);
        b->setTooltip ("Mostrar u ocultar esta onda en el osciloscopio");
        addAndMakeVisible (*b);
    }

    showPreButton.onClick  = [this] { setWaves (! scope.getShowPre(), scope.getShowPost()); };
    showPostButton.onClick = [this] { setWaves (scope.getShowPre(), ! scope.getShowPost()); };

    // Botón ⇄ linkeado a la escucha: hace lo mismo que el botón PRE/POST FX
    // grande (alterna lo que suena) y el osciloscopio lo sigue.
    // LINK: prendido, el osciloscopio sigue a la escucha (muestra solo la onda
    // que suena); apagado, muestra lo que dejes con PRE FX / POST FX.
    linkButton.setClickingTogglesState (false);
    linkButton.setTooltip ("Prendido: el osciloscopio sigue a la escucha PRE/POST FX");
    linkButton.onClick = [this] { setScopeLinked (! scopeLinked); };
    addAndMakeVisible (linkButton);
    scopeLinked = (bool) apvts.state.getProperty ("scopeLink", true);
    scope.setLinked (scopeLinked);

    updateWaveButtons();

    // ▾ Osciloscopio: mostrar u ocultar toda la sección (se guarda con el proyecto).
    scopeToggleButton.setClickingTogglesState (false);
    scopeToggleButton.getProperties().set ("kind", "plain");
    scopeToggleButton.setColour (juce::TextButton::textColourOffId, Theme::darkText);
    scopeToggleButton.setTooltip ("Mostrar u ocultar el osciloscopio");
    scopeToggleButton.onClick = [this]
    {
        if (scopeDetached)
            setScopeDetached (false);              // estaba en la ventana aparte: traerlo de vuelta
        else
            setScopeOpen (! scopeOpen);
    };
    addAndMakeVisible (scopeToggleButton);
    scopeOpen = (bool) apvts.state.getProperty ("scopeOpen", false);   // arranca cerrado

    // ⧉ Abrir el osciloscopio en una ventana aparte, grande.
    detachButton.setTooltip ("Abrir el osciloscopio en una ventana aparte");
    detachButton.onClick = [this] { setScopeDetached (! scopeDetached); };
    addAndMakeVisible (detachButton);
    detachedContent.setLookAndFeel (&lnf);
    detachedContent.layout = [this] (juce::Rectangle<int> area) { layoutDetached (area); };

    // Estilo: forma de dibujar, colores y grilla (se guardan con el proyecto).
    scopeStyleButton.setTooltip ("Forma, colores y grilla del osciloscopio");
    scopeStyleButton.onClick = [this] { showStyleMenu(); };
    addAndMakeVisible (scopeStyleButton);
    applyScopeLook();

    statusLabel.setFont (Theme::font (12.5f, 400));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    shownPre  = processor.displayPre.load();
    shownPost = processor.displayPost.load();
    shownGain = processor.displayGain.load();

    timerCallback();          // en modo Send oculta los controles del Receive
    updateScopeSection();     // fija el alto según el modo y si el osciloscopio está abierto
    startTimerHz (30);
}

MeteringABEditor::~MeteringABEditor()
{
    stopTimer();

    if (scopeWindow != nullptr)
    {
        processor.apvts.state.setProperty ("scopeWindowBounds", scopeWindow->getBounds().toString(), nullptr);
        scopeWindow->clearContentComponent();
        scopeWindow.reset();
    }

    detachedContent.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void DetachButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    g.setColour (detached ? Theme::tealBright.withAlpha (0.16f) : Theme::darkBg.brighter (isDown ? 0.1f : isOver ? 0.06f : 0.0f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (detached ? Theme::tealBright.withAlpha (0.6f) : Theme::chipLine.brighter (isOver ? 0.25f : 0.0f));
    g.drawRoundedRectangle (r, radius, 1.0f);

    // Ícono: cuadro abierto arriba a la derecha con una flecha saliendo.
    const auto ink = detached ? Theme::tealBright.brighter (0.25f) : Theme::chipText;
    auto icon = r.withSizeKeepingCentre (12.0f, 12.0f);
    juce::Path box;
    box.startNewSubPath (icon.getX() + 6.0f, icon.getY());
    box.lineTo (icon.getX(), icon.getY());
    box.lineTo (icon.getX(), icon.getBottom());
    box.lineTo (icon.getRight(), icon.getBottom());
    box.lineTo (icon.getRight(), icon.getY() + 8.0f);
    g.setColour (ink);
    g.strokePath (box, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path arrow;
    arrow.startNewSubPath (icon.getX() + 6.0f, icon.getBottom() - 6.0f);
    arrow.lineTo (icon.getRight() + 1.0f, icon.getY() - 1.0f);
    arrow.startNewSubPath (icon.getRight() - 5.0f, icon.getY() - 1.0f);
    arrow.lineTo (icon.getRight() + 1.0f, icon.getY() - 1.0f);
    arrow.lineTo (icon.getRight() + 1.0f, icon.getY() + 5.0f);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Lleva el osciloscopio (y sus botones) a una ventana aparte, o lo trae de vuelta.
void MeteringABEditor::setScopeDetached (bool detached)
{
    if (detached == scopeDetached)
        return;

    scopeDetached = detached;
    detachButton.detached = detached;
    detachButton.setTooltip (detached ? "Traer el osciloscopio de vuelta al plugin" : "Abrir el osciloscopio en una ventana aparte");
    auto& state = processor.apvts.state;

    if (detached)
    {
        for (auto* c : scopeParts)
            detachedContent.addAndMakeVisible (c);

        const auto saved = juce::Rectangle<int>::fromString (state.getProperty ("scopeWindowBounds", "").toString());
        detachedContent.setSize (1000, 520);

        if (createSeparateWindows)
        {
            const int link = (int) processor.apvts.getRawParameterValue (ParamIDs::link)->load();
            scopeWindow = std::make_unique<ScopeWindow> ("Metering A/B  ·  Osciloscopio  ·  Link ID "_u + juce::String (link));
            scopeWindow->setUsingNativeTitleBar (true);
            scopeWindow->setContentNonOwned (&detachedContent, true);
            scopeWindow->setResizable (true, false);
            scopeWindow->setResizeLimits (600, 320, 4000, 2500);
           #if JUCE_MAC || JUCE_WINDOWS
            scopeWindow->setAlwaysOnTop (true);             // que no quede detrás de Logic
           #endif

            const bool savedFits = ! saved.isEmpty()
                && juce::Desktop::getInstance().getDisplays().getDisplayForRect (saved) != nullptr;

            if (savedFits)
                scopeWindow->setBounds (saved);
            else
                scopeWindow->centreAroundComponent (this, scopeWindow->getWidth(), scopeWindow->getHeight());

            scopeWindow->onClose = [safe = juce::Component::SafePointer<MeteringABEditor> (this)]
            {
                juce::MessageManager::callAsync ([safe]
                {
                    if (safe != nullptr)
                        safe->setScopeDetached (false);
                });
            };

            scopeWindow->setVisible (true);
            scopeWindow->toFront (true);
        }
        else
        {
            detachedContent.resized();
        }
    }
    else
    {
        if (scopeWindow != nullptr)
        {
            state.setProperty ("scopeWindowBounds", scopeWindow->getBounds().toString(), nullptr);
            scopeWindow->clearContentComponent();
            scopeWindow.reset();
        }

        for (auto* c : scopeParts)
            addChildComponent (c);

        scopeOpen = true;                                   // vuelve abierto al plugin
        state.setProperty ("scopeOpen", true, nullptr);
    }

    updateScopeSection();
}

// Ventana grande: controles arriba (en el mismo orden que en el plugin) y el osciloscopio debajo.
void MeteringABEditor::layoutDetached (juce::Rectangle<int> area)
{
    auto bar = area.removeFromTop (52).reduced (14, 10);
    scope.setBounds (area.reduced (12, 0).withTrimmedBottom (12));

    zoomBox.setBounds (bar.removeFromRight (96));
    bar.removeFromRight (8);
    showPostButton.setBounds (bar.removeFromRight (74));
    bar.removeFromRight (6);
    showPreButton.setBounds (bar.removeFromRight (66));
    bar.removeFromRight (6);
    linkButton.setBounds (bar.removeFromRight (60));
    bar.removeFromRight (6);
    scopeStyleButton.setBounds (bar.removeFromRight (60));
}

void MeteringABEditor::setupKnob (PreciseSlider& s, juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, Colours::dim);
    label.setFont (Theme::font (12.5f, 300));
    addAndMakeVisible (s);
    addAndMakeVisible (label);
}

void MeteringABEditor::applyScopeLook()
{
    auto& state = processor.apvts.state;
    // Forma de PRE y de POST por separado (si no hay, toman la forma general de antes).
    const int legacyStyle = (int) state.getProperty ("scopeStyle", 0);
    scope.setStyles ((ScopeView::Style) juce::jlimit (0, 2, (int) state.getProperty ("scopeStylePre",  legacyStyle)),
                     (ScopeView::Style) juce::jlimit (0, 2, (int) state.getProperty ("scopeStylePost", legacyStyle)));
    scope.setShowGrid ((bool) state.getProperty ("scopeGrid", true));
    scope.setChannels ((ScopeView::Channels) juce::jlimit (0, 3, (int) state.getProperty ("scopeChannels", 0)));

    const int palette = juce::jlimit (0, ScopeView::customPalette, (int) state.getProperty ("scopePalette", 0));

    if (palette == ScopeView::customPalette)
        scope.setColours (juce::Colour::fromString (state.getProperty ("scopeCustomPre",  "ff5dcaa5").toString()),
                          juce::Colour::fromString (state.getProperty ("scopeCustomPost", "fff2b544").toString()));
    else
        scope.setColours (juce::Colour (ScopeView::palettes[(size_t) palette].pre),
                          juce::Colour (ScopeView::palettes[(size_t) palette].post));

    updateWaveButtons();
}

void MeteringABEditor::showStyleMenu()
{
    auto& state = processor.apvts.state;
    const int stylePre  = (int) scope.getStylePre();
    const int stylePost = (int) scope.getStylePost();
    const int palette   = (int) state.getProperty ("scopePalette", 0);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Forma PRE FX");
    for (int i = 0; i < ScopeView::styleNames.size(); ++i)
        menu.addItem (100 + i, ScopeView::styleNames[i], true, stylePre == i);

    menu.addSectionHeader ("Forma POST FX");
    for (int i = 0; i < ScopeView::styleNames.size(); ++i)
        menu.addItem (150 + i, ScopeView::styleNames[i], true, stylePost == i);

    menu.addSectionHeader ("Canales");
    const int channels = (int) state.getProperty ("scopeChannels", 0);
    for (int i = 0; i < ScopeView::channelNames.size(); ++i)
        menu.addItem (500 + i, ScopeView::channelNames[i], true, channels == i);

    menu.addSectionHeader ("Colores (PRE / POST)");
    for (int i = 0; i < (int) ScopeView::palettes.size(); ++i)
        menu.addItem (200 + i, ScopeView::palettes[(size_t) i].name, true, palette == i);

    menu.addItem (200 + ScopeView::customPalette, "Personalizado", true, palette == ScopeView::customPalette);
    menu.addItem (300, juce::String::fromUTF8 ("  Elegir color PRE\xe2\x80\xa6"));
    menu.addItem (301, juce::String::fromUTF8 ("  Elegir color POST\xe2\x80\xa6"));

    menu.addSeparator();
    menu.addItem (400, "Mostrar grilla", true, scope.getShowGrid());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scopeStyleButton),
                        [this, safe = juce::Component::SafePointer<MeteringABEditor> (this)] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& st = processor.apvts.state;

        if (result >= 100 && result < 150)      st.setProperty ("scopeStylePre", result - 100, nullptr);
        else if (result >= 150 && result < 200) st.setProperty ("scopeStylePost", result - 150, nullptr);
        else if (result >= 200 && result < 300) st.setProperty ("scopePalette", result - 200, nullptr);
        else if (result == 300 || result == 301) { pickCustomColour (result == 300); return; }
        else if (result == 400)                 st.setProperty ("scopeGrid", ! scope.getShowGrid(), nullptr);
        else if (result >= 500 && result < 600) st.setProperty ("scopeChannels", result - 500, nullptr);

        applyScopeLook();
    });
}

// Abre un selector de color; elegir un color pasa a la paleta "Personalizado".
void MeteringABEditor::pickCustomColour (bool forPre)
{
    auto& state = processor.apvts.state;

    if ((int) state.getProperty ("scopePalette", 0) != ScopeView::customPalette)
    {
        // Arrancar el personalizado desde los colores que se ven ahora.
        state.setProperty ("scopeCustomPre",  scope.getPreColour().toString(),  nullptr);
        state.setProperty ("scopeCustomPost", scope.getPostColour().toString(), nullptr);
        state.setProperty ("scopePalette", ScopeView::customPalette, nullptr);
        applyScopeLook();
    }

    const auto initial = forPre ? scope.getPreColour() : scope.getPostColour();
    auto selector = std::make_unique<LiveColourSelector> (initial,
        [this, forPre, safe = juce::Component::SafePointer<MeteringABEditor> (this)] (juce::Colour c)
        {
            if (safe == nullptr)
                return;

            processor.apvts.state.setProperty (forPre ? "scopeCustomPre" : "scopeCustomPost", c.toString(), nullptr);
            applyScopeLook();
        });

    // Funciona tanto en el plugin como en la ventana aparte.
    auto* host = scopeStyleButton.getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::move (selector),
                                            host->getLocalArea (&scopeStyleButton, scopeStyleButton.getLocalBounds()), host);
}

void MeteringABEditor::setScopeOpen (bool open)
{
    scopeOpen = open;
    processor.apvts.state.setProperty ("scopeOpen", open, nullptr);
    updateScopeSection();
}

// Muestra u oculta la sección del osciloscopio y ajusta el alto de la ventana.
void MeteringABEditor::updateScopeSection()
{
    const bool showSection = ! isSendMode;

    if (! showSection && scopeDetached)
    {
        setScopeDetached (false);                  // en Send no hay osciloscopio: cerrar la ventana
        return;
    }

    const bool showBody = showSection && scopeOpen && ! scopeDetached;
    const bool open = scopeOpen && ! scopeDetached;

    scopeToggleButton.setVisible (showSection);
    scopeToggleButton.setButtonText ("Osciloscopio");
    scopeToggleButton.getProperties().set ("open", open);     // triángulo ▾ / ▸
    scopeToggleButton.repaint();
    detachButton.setVisible (showSection);

    if (! scopeDetached)
        for (auto* c : scopeParts)
            c->setVisible (showBody);

    scopeLabel.setText (scopeDetached ? "En una ventana aparte" : "", juce::dontSendNotification);
    scopeLabel.setVisible (showSection && scopeDetached);

    const int height = ! showSection ? sendHeight
                     : showBody      ? mainHeight + scopeHeaderHeight + scopeBodyHeight
                                     : mainHeight + scopeHeaderHeight;

    if (getWidth() != editorWidth || getHeight() != height)
        setSize (editorWidth, height);
    else
        resized();

    repaint();
}

void MeteringABEditor::setWaves (bool pre, bool post)
{
    scope.setShowPre (pre);
    scope.setShowPost (post);
    processor.apvts.state.setProperty ("scopeShowPre",  pre,  nullptr);
    processor.apvts.state.setProperty ("scopeShowPost", post, nullptr);
    updateWaveButtons();
}

// Encendido: relleno con el color de su onda. Apagado: gris.
void MeteringABEditor::updateWaveButtons()
{
    auto style = [] (juce::TextButton& b, bool on, juce::Colour colour)
    {
        b.getProperties().set ("chipColour", (juce::int64) colour.getARGB());
        b.setToggleState (on, juce::dontSendNotification);
        b.repaint();
    };

    style (showPreButton,  scope.getShowPre(),  scope.getPreColour());
    style (showPostButton, scope.getShowPost(), scope.getPostColour());

    // LINK: prendido toma el color de la onda que estás escuchando.
    const bool listeningPre = processor.apvts.getRawParameterValue (ParamIDs::pre)->load() > 0.5f;
    style (linkButton, scopeLinked, listeningPre ? scope.getPreColour() : scope.getPostColour());
}

void MeteringABEditor::setScopeLinked (bool linked)
{
    scopeLinked = linked;
    scope.setLinked (linked);
    processor.apvts.state.setProperty ("scopeLink", linked, nullptr);

    if (linked)
        syncWavesToListen();          // al prenderlo, mostrar enseguida lo que suena
    else
        updateWaveButtons();
}

void MeteringABEditor::syncWavesToListen()
{
    const bool listeningPre = processor.apvts.getRawParameterValue (ParamIDs::pre)->load() > 0.5f;
    setWaves (listeningPre, ! listeningPre);
}

void MeteringABEditor::setBoolParam (const char* paramId, bool on)
{
    if (auto* param = processor.apvts.getParameter (paramId))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

void MeteringABEditor::toggleAB()
{
    auto& apvts = processor.apvts;

    setBoolParam (ParamIDs::pre, apvts.getRawParameterValue (ParamIDs::pre)->load() < 0.5f);
}

void MeteringABEditor::setSync (bool on)
{
    if (auto* param = processor.apvts.getParameter (ParamIDs::sync))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

void MeteringABEditor::timerCallback()
{
    // Movimiento suave de los números en pantalla.
    auto ease = [] (float& shown, float target) { shown += (target - shown) * 0.35f; };
    ease (shownPre,  processor.displayPre.load());
    ease (shownPost, processor.displayPost.load());
    ease (shownGain, processor.displayGain.load());

    // El osciloscopio sigue a la escucha: cuando cambia PRE/POST FX (botón,
    // tecla asignada o automatización), muestra solo la onda que suena.
    {
        const int listenPre = processor.apvts.getRawParameterValue (ParamIDs::pre)->load() > 0.5f ? 1 : 0;

        if (lastListenPre >= 0 && listenPre != lastListenPre)
        {
            if (scopeLinked)
                setWaves (listenPre == 1, listenPre == 0);
            else
                updateWaveButtons();      // solo el color del botón LINK
        }

        lastListenPre = listenPre;
    }

    const bool sendMode = (int) processor.apvts.getRawParameterValue (ParamIDs::mode)->load() == 0;

    if (sendMode != isSendMode)
    {
        isSendMode = sendMode;

        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &windowSlider, &windowLabel, &latencyLabel, &polarityButton, &latencyMsLabel,
                 &abButton, &listenLabel, &matchInfoLabel, &measureButton, &latencyInfoLabel,

                 &syncButtons[0], &syncButtons[1], &windowInfoLabel })
            c->setVisible (! isSendMode);

        updateScopeSection();
    }

    // Ventana: ms o Sync
    const bool syncOn = processor.apvts.getRawParameterValue (ParamIDs::sync)->load() > 0.5f;
    windowSlider.setVisible (! isSendMode && ! syncOn);
    windowSyncSlider.setVisible (! isSendMode && syncOn);
    syncButtons[0].setToggleState (! syncOn, juce::dontSendNotification);
    syncButtons[1].setToggleState (syncOn, juce::dontSendNotification);

    const auto tempoText = juce::String (processor.hostBpm.load(), 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                           + " BPM " + juce::String (processor.timeSigNum.load()) + "/" + juce::String (processor.timeSigDen.load());
    const double windowSeconds = processor.getWindowSeconds();

    if (syncOn)
    {
        windowInfoLabel.setText ("= " + juce::String (juce::roundToInt (windowSeconds * 1000.0)) + " ms  ·  " + tempoText,
                                 juce::dontSendNotification);
    }
    else
    {
        const int nearest = MeteringABProcessor::nearestDivision (windowSeconds, processor.getBarSeconds());
        windowInfoLabel.setText ("~ " + SyncDivisions::names[nearest] + "  ·  " + tempoText, juce::dontSendNotification);
    }

    // Botón PRE FX / POST FX
    const bool pre = processor.apvts.getRawParameterValue (ParamIDs::pre)->load() > 0.5f;
    abButton.setButtonText (pre ? "PRE FX" : "POST FX");
    abButton.setColour (juce::TextButton::textColourOffId, pre ? Theme::preInk : Theme::teal);   // PRE gris, POST turquesa

    // Estado de la medición de latencia
    {
        using State = LatencyMeasurer::State;
        const auto& m = processor.latencyMeasurer;
        const double latency = processor.apvts.getRawParameterValue (ParamIDs::latency)->load();
        const bool whole = std::abs (latency - std::round (latency)) < 0.005;
        const auto samplesText = (whole ? juce::String (juce::roundToInt (latency)) : juce::String (latency, 2)).replaceCharacter ('.', ',');
        const auto msText = juce::String (latency * 1000.0 / juce::jmax (1.0, processor.getCurrentSampleRate()), 2).replaceCharacter ('.', ',') + " ms";
        const bool inverted = processor.apvts.getRawParameterValue (ParamIDs::polarity)->load() > 0.5f;
        const auto polarityText = inverted ? "  ·  "_u + juce::String::fromUTF8 ("\xc3\x98") + " invertida" : juce::String();
        juce::String text;
        juce::Colour colour = Colours::dim;

        switch (m.getState())
        {
            case State::measuring:
                text = "Midiendo... " + juce::String (juce::roundToInt (m.getProgress() * 100.0f)) + "% (con play)";
                colour = Colours::accent;
                break;
            case State::analysing:
                text = "Calculando...";
                colour = Colours::accent;
                break;
            case State::done:
                text = juce::String::fromUTF8 ("\xe2\x9c\x93") + " Medido" + polarityText;
                colour = Colours::good;
                break;
            case State::noSignal: text = "No llega audio: dale play";          colour = Colours::bad; break;
            case State::unclear:  text = "Sin coincidencia clara";              colour = Colours::bad; break;
            case State::noSend:   text = "No hay Send conectado";               colour = Colours::bad; break;
            case State::idle:
                if (inverted)
                    text = juce::String::fromUTF8 ("\xc3\x98") + " invertida";
                else if (latency < 0.005)
                    text = "Dale play y tocá Medir"_u;
                break;
        }

        latencyInfoLabel.setText (text, juce::dontSendNotification);
        latencyInfoLabel.setColour (juce::Label::textColourId, colour);

        if (latencyNumberText != samplesText)
        {
            latencyNumberText = samplesText;
            repaint (latencyNumberArea);
        }
        latencyMsLabel.setText ("= " + msText, juce::dontSendNotification);

        const bool busy = m.getState() == State::measuring || m.getState() == State::analysing;
        measureButton.setButtonText (busy ? "..." : "Medir");
        measureButton.setEnabled (! busy);
    }

    // Mensaje de estado.
    const int link = (int) processor.apvts.getRawParameterValue (ParamIDs::link)->load();

    if (! linkValueLabel.isBeingEdited())
        linkValueLabel.setText (juce::String (link), juce::dontSendNotification);

    if (processor.duplicateSend.load())
    {
        statusLabel.setText ("Hay más de un Send con el Link ID "_u + juce::String (link) + ": cambiá uno."_u, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Colours::bad);
    }
    else if (isSendMode)
    {
        statusLabel.setText ("Send activo en Link ID " + juce::String (link) + ". Poné un Metering A/B en Receive al final de la cadena."_u, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Colours::good);
    }
    else if (processor.sendConnected.load())
    {
        statusLabel.setText ("Conectado al Send del Link ID " + juce::String (link) + ".", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Colours::good);
    }
    else
    {
        statusLabel.setText ("No hay ningún Send sonando en el Link ID "_u + juce::String (link) + " (dale play).", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Colours::bad);
    }

    repaint (0, 76, getWidth(), 80);
    repaint (statusLabel.getBounds().withX (0).withWidth (40));   // puntito del estado

    if (scopeWindow != nullptr)                    // título con el Link ID actual
    {
        const auto title = "Metering A/B  ·  Osciloscopio  ·  Link ID "_u
                         + juce::String ((int) processor.apvts.getRawParameterValue (ParamIDs::link)->load());
        if (scopeWindow->getName() != title)
            scopeWindow->setName (title);
    }

    if (! isSendMode && (scopeOpen || scopeDetached) && ! scope.isFrozen())
        scope.repaint();
}

// Número grande (letra fina) con la unidad chica al lado, centrados juntos.
static void drawNumberWithUnit (juce::Graphics& g, const juce::String& number, const juce::String& unit,
                                juce::Rectangle<int> area, float size, juce::Colour colour)
{
    const auto numberFont = Theme::font (size, 300);
    const auto unitFont   = Theme::font (size * 0.44f, 400);
    const float gap = unit.isEmpty() ? 0.0f : 4.0f;
    const float wNum  = juce::GlyphArrangement::getStringWidth (numberFont, number);
    const float wUnit = unit.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (unitFont, unit);
    float x = (float) area.getCentreX() - (wNum + gap + wUnit) * 0.5f;
    const float baseline = (float) area.getCentreY() + size * 0.36f;

    g.setColour (colour);
    g.setFont (numberFont);
    g.drawSingleLineText (number, juce::roundToInt (x), juce::roundToInt (baseline));

    if (unit.isNotEmpty())
    {
        g.setColour (Theme::inkDim);
        g.setFont (unitFont);
        g.drawSingleLineText (unit, juce::roundToInt (x + wNum + gap), juce::roundToInt (baseline));
    }
}

void MeteringABEditor::paint (juce::Graphics& g)
{
    const int topHeight = isSendMode ? getHeight() : mainHeight;

    // Parte de arriba en gris claro; el osciloscopio, abajo, en oscuro.
    g.setGradientFill (juce::ColourGradient (Theme::topLight, 0.0f, 0.0f, Theme::topDark, 0.0f, (float) topHeight, false));
    g.fillRect (0, 0, getWidth(), topHeight);

    if (! isSendMode)
    {
        g.setColour (Theme::darkBg);
        g.fillRect (0, topHeight, getWidth(), getHeight() - topHeight);
    }

    // Título
    g.setColour (juce::Colour (0xff3a3d42));
    g.setFont (Theme::font (16.0f, 600, 0.16f));
    g.drawText ("METERING A/B", 20, 10, 220, 32, juce::Justification::centredLeft);

    // Pastilla del Link ID (− 1 +)
    {
        const auto r = linkMinus.getBounds().getUnion (linkPlus.getBounds()).toFloat();
        g.setColour (Theme::pill);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    }

    // Puntito de color al lado del estado
    {
        const auto lb = statusLabel.getBounds().toFloat();
        g.setColour (statusLabel.findColour (juce::Label::textColourId));
        g.fillEllipse (lb.getX() - 13.0f, lb.getCentreY() - 3.5f, 7.0f, 7.0f);
    }

    // Send: solo título, modo, Link ID y estado (ventana chica).
    if (isSendMode)
        return;

    // Números: Original | Ganancia | Procesado, en un recuadro hundido.
    auto meterArea = juce::Rectangle<int> (20, 80, getWidth() - 40, 70);
    Theme::drawWell (g, meterArea.toFloat(), 14.0f);

    auto level = [] (float db) { return db <= -119.0f ? juce::String ("—"_u) : juce::String (db, 1); };
    struct Readout { juce::String title, number, unit; juce::Colour colour; };
    const bool hasPre = shownPre > -119.0f, hasPost = shownPost > -119.0f;
    const Readout readouts[] = {
        { "Original (Send)",   level (shownPre),  hasPre  ? "LUFS" : "", Theme::inkStrong },
        { "Ganancia aplicada", (shownGain >= 0 ? "+" : "") + juce::String (shownGain, 2), "dB", Theme::teal },
        { "Procesado",         level (shownPost), hasPost ? "LUFS" : "", Theme::inkStrong },
    };

    const int colWidth = meterArea.getWidth() / 3;

    for (int i = 0; i < 3; ++i)
    {
        auto col = meterArea.withX (meterArea.getX() + i * colWidth).withWidth (colWidth).reduced (8, 7);
        g.setColour (Theme::inkDim);
        g.setFont (Theme::font (12.0f, 300));
        g.drawText (readouts[i].title, col.removeFromTop (16), juce::Justification::centred);
        drawNumberWithUnit (g, readouts[i].number, readouts[i].unit, col, 29.0f, readouts[i].colour);
    }

    // Fondo de ms | Sync
    {
        const auto r = syncButtons[0].getBounds().getUnion (syncButtons[1].getBounds()).toFloat();
        g.setColour (Theme::pill);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    }

    // Latencia medida: número grande + "muestras"
    drawNumberWithUnit (g, latencyNumberText, "muestras", latencyNumberArea, 26.0f, Theme::inkStrong);

    // Ayuda
    g.setColour (Theme::inkFaint);
    g.setFont (Theme::font (10.5f, 300));
    g.drawText ("Clic en un número para escribir el valor exacto  ·  Shift + arrastrar = ajuste fino  ·  Doble clic = valor por defecto"_u,
                20, mainHeight - 24, getWidth() - 40, 16, juce::Justification::centred);
}

void MeteringABEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 0);

    // Fila superior: título | modo | Link ID (− 1 +)
    auto top = area.removeFromTop (52).withTrimmedTop (10).withHeight (32);
    top.removeFromLeft (200);
    linkPlus.setBounds (top.removeFromRight (32));
    linkValueLabel.setBounds (top.removeFromRight (40));
    linkMinus.setBounds (top.removeFromRight (32));
    linkLabel.setBounds (top.removeFromRight (62));
    top.removeFromRight (10);
    modeBox.setBounds (top);

    statusLabel.setBounds (area.removeFromTop (22).withTrimmedLeft (14));
    area.removeFromTop (6);
    area.removeFromTop (70);   // medidores (se dibujan en paint)
    area.removeFromTop (12);

    // Tres columnas con los títulos alineados: Escuchar | Ventana | Latencia
    auto columns = area.removeFromTop (columnsHeight);
    const int sideWidth = 190;
    auto left  = columns.removeFromLeft (sideWidth);
    auto right = columns.removeFromRight (sideWidth);
    auto middle = columns.reduced (10, 0);

    // Escuchar: el botón grande PRE FX / POST FX y, debajo, cómo se nivela.
    {
        auto k = left;
        listenLabel.setBounds (k.removeFromTop (22));
        k.removeFromTop (4);
        abButton.setBounds (k.removeFromTop (88).expanded (4, 0));
        k.removeFromTop (4);
        matchInfoLabel.setBounds (k.removeFromTop (16));
    }

    // Ventana: botones ms|Sync arriba, perilla con el valor, equivalencia abajo.
    {
        auto k = middle;
        auto header = k.removeFromTop (22).withSizeKeepingCentre (138, 22);
        windowLabel.setBounds (header.removeFromLeft (54));
        syncButtons[0].setBounds (header.removeFromLeft (38));
        syncButtons[1].setBounds (header.removeFromLeft (46));
        k.removeFromTop (4);
        windowInfoLabel.setBounds (k.removeFromBottom (16));
        windowSlider.setBounds (k.withSizeKeepingCentre (200, k.getHeight()));
        windowSyncSlider.setBounds (k.withSizeKeepingCentre (200, k.getHeight()));
    }

    // Latencia: título, valor medido, Medir + Ø y estado.
    {
        auto k = right;
        latencyLabel.setBounds (k.removeFromTop (22));
        latencyNumberArea = k.removeFromTop (30);
        latencyMsLabel.setBounds (k.removeFromTop (16));
        k.removeFromTop (6);
        auto buttons = k.removeFromTop (40).withSizeKeepingCentre (186, 40);
        measureButton.setBounds (buttons.removeFromLeft (136));
        buttons.removeFromLeft (4);
        polarityButton.setBounds (buttons);
        k.removeFromTop (2);
        latencyInfoLabel.setBounds (k.removeFromTop (16));
    }

    // ---- Sección del osciloscopio (debajo de la línea) ----
    auto section = getLocalBounds().withTrimmedTop (mainHeight + 1).reduced (20, 0);

    auto scopeHeader = section.removeFromTop (scopeHeaderHeight - 1).withSizeKeepingCentre (section.getWidth(), 28);
    scopeToggleButton.setBounds (scopeHeader.removeFromLeft (128).translated (-8, 0));
    detachButton.setBounds (scopeHeader.removeFromRight (34));
    scopeHeader.removeFromRight (8);

    if (scopeDetached)                             // los controles están en la ventana aparte
    {
        scopeLabel.setBounds (scopeHeader);
        return;
    }

    zoomBox.setBounds (scopeHeader.removeFromRight (96));
    scopeHeader.removeFromRight (8);
    showPostButton.setBounds (scopeHeader.removeFromRight (74));
    scopeHeader.removeFromRight (6);
    showPreButton.setBounds (scopeHeader.removeFromRight (66));
    scopeHeader.removeFromRight (6);
    linkButton.setBounds (scopeHeader.removeFromRight (60));
    scopeHeader.removeFromRight (6);
    scopeStyleButton.setBounds (scopeHeader.removeFromRight (60));
    scopeLabel.setBounds (scopeHeader);

    scope.setBounds (section.removeFromTop (scopeBodyHeight - 16));
}
