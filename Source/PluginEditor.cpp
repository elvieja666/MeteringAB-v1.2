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
    const juce::Colour accent     = Theme::accent;
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
    setColour (juce::Slider::textBoxHighlightColourId, Theme::accent.withAlpha (0.25f));
    setColour (juce::TextEditor::backgroundColourId, Theme::well);
    setColour (juce::TextEditor::textColourId, Theme::inkStrong);
    setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.25f));
    setColour (juce::TextEditor::highlightedTextColourId, Theme::inkStrong);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::accent);
    setColour (juce::CaretComponent::caretColourId, Theme::accent);
    setColour (juce::ComboBox::textColourId, Theme::ink);
    setColour (juce::ComboBox::arrowColourId, Theme::ink);
    setColour (juce::PopupMenu::backgroundColourId, Theme::well);
    setColour (juce::PopupMenu::textColourId, Theme::ink);
    setColour (juce::PopupMenu::headerTextColourId, Theme::inkDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, Theme::raisedBot);
    setColour (juce::TextButton::buttonOnColourId, Theme::raisedBot);
    setColour (juce::TextButton::textColourOffId, Theme::ink);
    setColour (juce::TextButton::textColourOnId, Theme::accent);
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

    if (button.getProperties()["kind"].toString() == "bypass")
        return Theme::font (11.5f, 500);

    if (button.getProperties()["kind"].toString() == "plain")
        return Theme::font (button.getProperties().contains ("small") ? 12.0f
                            : button.getProperties().contains ("open") ? 14.0f : 16.0f, button.getProperties().contains ("small") ? 400 : 300);

    return Theme::font (juce::jmin (14.0f, (float) buttonHeight * 0.5f), 400);
}

// Zona que ocupa el botón en relieve (deja lugar a la sombra). El texto se
// centra en esta zona, no en todo el componente.
static juce::Rectangle<float> raisedShape (juce::Rectangle<float> bounds)
{
    return bounds.reduced (5.0f, 2.0f).withTrimmedBottom (4.0f);
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
        const auto colour = juce::Colour ((juce::uint32) (juce::int64) button.getProperties().getWithDefault ("chipColour", (juce::int64) Theme::accentBright.getARGB()));

        g.setColour (on ? colour.withAlpha (0.16f).overlaidWith (juce::Colours::transparentBlack) : Theme::darkBg.brighter (over ? 0.06f : 0.0f));
        g.fillRoundedRectangle (r, radius);
        g.setColour (on ? colour.withAlpha (0.6f) : Theme::chipLine.brighter (over ? 0.25f : 0.0f));
        g.drawRoundedRectangle (r, radius, 1.0f);
        return;
    }

    if (kind == "scopetoggle")                       // Scope: pastilla con triángulo (abierto = encendido)
    {
        r = r.reduced (0.5f);
        const float radius = r.getHeight() * 0.5f;
        const bool on = button.getToggleState();
        g.setColour (on ? Theme::raisedTop : Theme::pill.brighter (over ? 0.03f : 0.0f));
        g.fillRoundedRectangle (r, radius);
        if (on)
        {
            g.setColour (Theme::accent.withAlpha (0.45f));
            g.drawRoundedRectangle (r, radius, 1.0f);
        }
        // Triángulo a la derecha: ◀ cerrar (abierto) / ▶ abrir (cerrado).
        const float cx = r.getRight() - radius - 2.0f, cy = r.getCentreY(), t = 4.5f;
        juce::Path p;
        if (on) p.addTriangle (cx + t * 0.6f, cy - t, cx + t * 0.6f, cy + t, cx - t * 0.8f, cy);
        else    p.addTriangle (cx - t * 0.6f, cy - t, cx - t * 0.6f, cy + t, cx + t * 0.8f, cy);
        g.setColour (on ? Theme::accent : Theme::inkDim);
        g.fillPath (p);
        return;
    }

    if (kind == "bypass")                            // Match Gain Bypass: pastilla; prendido = naranja
    {
        r = r.reduced (0.5f);
        const float radius = r.getHeight() * 0.5f;
        const bool on = button.getToggleState();
        g.setColour (on ? Theme::warn : Theme::pill.brighter (over ? 0.03f : 0.0f));
        g.fillRoundedRectangle (r, radius);
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
    Theme::drawRaised (g, raisedShape (r), radius, over, down || button.getToggleState());
}

void MeteringABLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto kind = button.getProperties()["kind"].toString();
    auto r = button.getLocalBounds();
    juce::Colour colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);

    if (kind == "chip")
    {
        const auto chipColour = juce::Colour ((juce::uint32) (juce::int64) button.getProperties().getWithDefault ("chipColour", (juce::int64) Theme::accentBright.getARGB()));
        colour = button.getToggleState() ? chipColour.brighter (0.25f) : Theme::chipText;
    }
    else if (kind == "scopetoggle")
    {
        colour = button.getToggleState() ? Theme::accent : Theme::inkDim;
        r = r.withTrimmedRight (r.getHeight() / 2 + 6).withTrimmedLeft (4);
    }
    else if (kind == "segment")
    {
        colour = button.getToggleState() ? Theme::accent : Theme::inkDim;
    }
    else if (kind == "bypass")
    {
        colour = button.getToggleState() ? juce::Colours::white : Theme::inkDim;
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
    const bool raised = kind.isEmpty() || kind == "raised";
    if (raised)
        r = raisedShape (r.toFloat()).toNearestInt().translated (0, button.isDown() ? 1 : 0);

    // Con ícono (ojo = Overlay, parlante = escucha): ícono + texto centrados juntos.
    const auto icon = button.getProperties()["icon"].toString();
    if (icon.isNotEmpty())
    {
        const auto font = getTextButtonFont (button, button.getHeight());
        const float textW = juce::GlyphArrangement::getStringWidth (font, button.getButtonText());
        const float iconH = button.getProperties().contains ("big") ? 24.0f : 14.0f;
        const float iconW = icon == "eye" ? iconH : iconH * 0.95f;
        const float gap = button.getProperties().contains ("big") ? 12.0f : 6.0f;
        const auto rf = r.toFloat();
        const float x0 = rf.getCentreX() - (iconW + gap + textW) * 0.5f;
        const auto iconBox = juce::Rectangle<float> (x0, rf.getCentreY() - iconH * 0.5f, iconW, iconH);
        if (icon == "eye") Theme::drawEye (g, iconBox, colour);
        else               Theme::drawSpeaker (g, iconBox, colour);
        g.setColour (colour);
        g.drawText (button.getButtonText(), juce::Rectangle<float> (x0 + iconW + gap, rf.getY(), textW + 4.0f, rf.getHeight()),
                    juce::Justification::centredLeft, false);
        return;
    }

    g.drawFittedText (button.getButtonText(), r,
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
    label->setColour (juce::Label::outlineWhenEditingColourId, Theme::accent);
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

    // Arco de fondo y arco del valor (azul).
    juce::Path back;
    back.addCentredArc (centre.x, centre.y, radius - track * 0.5f, radius - track * 0.5f, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::track);
    g.strokePath (back, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - track * 0.5f, radius - track * 0.5f, 0.0f, startAngle, angle, true);
    g.setColour (Theme::accent.brighter (0.15f));
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
MeteringABPanel::MeteringABPanel (MeteringABProcessor& p)
    : processor (p)
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

    setupKnob (windowSlider,  windowLabel,  "Window");
    // Latencia: sin perilla, solo el botón Medir y el valor medido.
    latencyLabel.setText ("Latency", juce::dontSendNotification);
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
    // (El estilo se fija en cada perilla, así el casillero sale con el diseño nuevo
    // aunque la perilla todavía no esté puesta en el plugin.)
    for (auto* ws : { &windowSlider, &windowSyncSlider })
    {
        ws->setLookAndFeel (&lnf);
        ws->setTextBoxStyle (juce::Slider::TextBoxRight, false, 104, 26);
    }

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
    abButton.getProperties().set ("icon", "speaker");          // parlante: esto es lo que suena
    abButton.setClickingTogglesState (false);
    abButton.onClick = [this] { toggleAB(); };
    addAndMakeVisible (abButton);

    matchInfoLabel.setText ("Level-matched in LUFS (K-weighted)"_u, juce::dontSendNotification);
    matchInfoLabel.setJustificationType (juce::Justification::centred);
    matchInfoLabel.setColour (juce::Label::textColourId, Colours::dim);
    matchInfoLabel.setFont (Theme::font (11.0f, 300));
    addAndMakeVisible (matchInfoLabel);

    // Match Gain Bypass: escuchar POST FX con su nivel real (sin nivelar).
    matchBypassButton.getProperties().set ("kind", "bypass");
    matchBypassButton.setClickingTogglesState (false);
    matchBypassButton.onClick = [this]
    {
        setBoolParam (ParamIDs::matchBypass, processor.apvts.getRawParameterValue (ParamIDs::matchBypass)->load() < 0.5f);
        refreshNow();
    };
    addAndMakeVisible (matchBypassButton);

    listenLabel.setText ("Listen  ·  click to switch"_u, juce::dontSendNotification);
    listenLabel.setColour (juce::Label::textColourId, Colours::dim);
    listenLabel.setFont (Theme::font (12.5f, 300));
    addAndMakeVisible (listenLabel);

    // Medir latencia
    measureButton.setButtonText ("Measure");
    measureButton.onClick = [this] { processor.startLatencyMeasurement(); };
    addAndMakeVisible (measureButton);


    latencyInfoLabel.setJustificationType (juce::Justification::centred);
    latencyInfoLabel.setFont (Theme::font (11.0f, 400));
    addAndMakeVisible (latencyInfoLabel);


    // Osciloscopio sincronizado + largo en compases (se guarda con el proyecto).
    addAndMakeVisible (scope);
    scopeLabel.setText ("Oscilloscope", juce::dontSendNotification);
    scopeLabel.setColour (juce::Label::textColourId, Theme::darkText);
    scopeLabel.setFont (Theme::font (15.0f, 300));
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

    for (juce::TextButton* b : { (juce::TextButton*) &compareButton, &freezeButton, &gainLineButton, &optionsButton })
        b->getProperties().set ("kind", "chip");

    // Freeze: congela lo que se ve (para mirarlo y hacerle zoom tranquilo).
    // Options: canales (L / R / suma) y grilla.
    optionsButton.setClickingTogglesState (false);
    optionsButton.onClick = [this] { showStyleMenu (optionsButton); };
    addAndMakeVisible (optionsButton);

    freezeButton.setClickingTogglesState (false);
    freezeButton.getProperties().set ("chipColour", (juce::int64) Theme::accentBright.getARGB());
    freezeButton.onClick = [this]
    {
        scope.setFrozen (! scope.isFrozen());
        freezeButton.setToggleState (scope.isFrozen(), juce::dontSendNotification);
    };
    addAndMakeVisible (freezeButton);

    // Gain: línea en el osciloscopio con el valor de la ganancia aplicada (se guarda).
    gainLineButton.setClickingTogglesState (false);
    gainLineButton.getProperties().set ("chipColour", (juce::int64) 0xffeceef1);
    gainLineButton.onClick = [this]
    {
        gainLineOn = ! gainLineOn;
        processor.apvts.state.setProperty ("scopeGainLine", gainLineOn, nullptr);
        gainLineButton.setToggleState (gainLineOn, juce::dontSendNotification);
        scope.setGainLine (gainLineOn && gainShown ? std::optional<float> (shownGain) : std::nullopt);
    };
    gainLineOn = (bool) apvts.state.getProperty ("scopeGainLine", false);
    gainLineButton.setToggleState (gainLineOn, juce::dontSendNotification);
    addAndMakeVisible (gainLineButton);

    // Scope: abrir / cerrar el osciloscopio. Un plugin nuevo arranca cerrado (solo
    // la columna gris); después se abre como lo dejaste (se guarda con el proyecto).
    scopeToggleButton.getProperties().set ("kind", "scopetoggle");
    scopeToggleButton.setClickingTogglesState (false);
    scopeToggleButton.onClick = [this] { setScopeOpen (! scopeOpen); };
    addAndMakeVisible (scopeToggleButton);
    scopeOpen = (bool) apvts.state.getProperty ("scopeOpenNow", scopeOpenOnStart);

    zoomBox.getProperties().set ("kind", "chip");

    // Overlay: las dos ondas juntas para comparar, solo a la vista (se guarda con el proyecto).
    compareButton.setClickingTogglesState (false);
    compareButton.getProperties().set ("icon", "eye");          // ojo: solo cambia lo que se ve
    compareButton.onClick = [this] { setCompare (! compareOn); };
    addAndMakeVisible (compareButton);

    // Botón ⇄ linkeado a la escucha: hace lo mismo que el botón PRE/POST FX
    // grande (alterna lo que suena) y el osciloscopio lo sigue.
    // LINK: prendido, el osciloscopio sigue a la escucha (muestra solo la onda
    // que suena); apagado, muestra lo que dejes con PRE FX / POST FX.
    // Al abrir: sigue a la escucha y muestra solo la onda que suena (antes quedaban
    // las dos a la vista, como en Overlay, con el botón apagado)...
    setScopeLinked (true);
    if ((bool) apvts.state.getProperty ("scopeCompare", false))
        setCompare (true);                            // ...salvo con Overlay prendido

    updateWaveButtons();

    // Columna gris: títulos a la izquierda.
    for (auto* l : { &listenLabel, &windowLabel, &latencyLabel })
        l->setJustificationType (juce::Justification::centredLeft);

    linkLabel.setJustificationType (juce::Justification::centredLeft);
    latencyMsLabel.setJustificationType (juce::Justification::centredLeft);
    latencyInfoLabel.setJustificationType (juce::Justification::centredRight);

    applyScopeLook();

    statusLabel.setFont (Theme::font (12.5f, 400));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    shownPre  = processor.displayPre.load();
    shownPost = processor.displayPost.load();
    shownGain = processor.displayGain.load();

    timerCallback();          // en modo Send oculta los controles del Receive
    updateScopeSection();     // fija el alto según el modo y si el osciloscopio está abierto
    startTimerHz (timerHz);
}

MeteringABPanel::~MeteringABPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MeteringABPanel::setupKnob (PreciseSlider& s, juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, Colours::dim);
    label.setFont (Theme::font (12.5f, 300));
    addAndMakeVisible (s);
    addAndMakeVisible (label);
}

void MeteringABPanel::applyScopeLook()
{
    auto& state = processor.apvts.state;
    // Forma de PRE y de POST por separado (si no hay, toman la forma general de antes).
    // PRE + POST: PRE en Outline y POST en Lines only. Siguiendo la escucha: Detallado (picos).
    scope.setStyles (ScopeView::Style::contour, ScopeView::Style::lines);
    scope.setLinkStyle (ScopeView::Style::detailed);
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

// Colores para elegir por onda (los de las paletas, sin repetir).
static const std::array<std::pair<const char*, juce::uint32>, 9> waveColours {{
    { "Grey",        0xffa9aeb6 }, { "Dark grey",   0xff6c7077 }, { "Light grey", 0xffb8bbc1 },
    { "White",       0xffd9d7d3 }, { "Blue",        0xff5aa9ff }, { "Pink",       0xffff6fa8 },
    { "Red",         0xffeb5757 }, { "Violet",      0xff9b8cff }, { "Lime",       0xffb5e655 },
}};

// Menú del botón Options: canales y grilla.
juce::PopupMenu MeteringABPanel::buildStyleMenu() const
{
    auto& state = processor.apvts.state;
    juce::PopupMenu menu;

    juce::PopupMenu channelsMenu;
    const int channels = (int) state.getProperty ("scopeChannels", 0);
    for (int i = 0; i < ScopeView::channelNames.size(); ++i)
        channelsMenu.addItem (500 + i, ScopeView::channelNames[i], true, channels == i);
    menu.addSubMenu ("Channels: " + ScopeView::channelNames[juce::jlimit (0, ScopeView::channelNames.size() - 1, channels)], channelsMenu);
    menu.addItem (400, "Show grid", true, scope.getShowGrid());
    return menu;
}

void MeteringABPanel::applyStyleMenuResult (int result)
{
    if (result == 0)
        return;

    auto& st = processor.apvts.state;

    // Color de una sola onda: pasa a "personalizado" conservando el de la otra.
    auto setWaveColour = [&] (bool pre, juce::Colour c)
    {
        st.setProperty ("scopeCustomPre",  (pre ? c : scope.getPreColour()).toString(),  nullptr);
        st.setProperty ("scopeCustomPost", (pre ? scope.getPostColour() : c).toString(), nullptr);
        st.setProperty ("scopePalette", ScopeView::customPalette, nullptr);
    };

    if (result >= 100 && result < 150)      st.setProperty ("scopeStylePre", result - 100, nullptr);
    else if (result >= 150 && result < 200) st.setProperty ("scopeStylePost", result - 150, nullptr);
    else if (result == 300 || result == 301) { pickCustomColour (result == 300); return; }
    else if (result == 400)                 st.setProperty ("scopeGrid", ! scope.getShowGrid(), nullptr);
    else if (result >= 500 && result < 600) st.setProperty ("scopeChannels", result - 500, nullptr);
    else if (result >= 600 && result < 700) st.setProperty ("scopeStyleLink", result - 601, nullptr);   // 600 = -1 (igual)
    else if (result >= 700 && result < 700 + (int) waveColours.size())
        setWaveColour (true,  juce::Colour (waveColours[(size_t) (result - 700)].second));
    else if (result >= 750 && result < 750 + (int) waveColours.size())
        setWaveColour (false, juce::Colour (waveColours[(size_t) (result - 750)].second));

    applyScopeLook();
}

void MeteringABPanel::showStyleMenu (juce::Component& anchor)
{
    styleMenuAnchor = &anchor;
    ++styleMenuOpenedForTests;

    if (! anchor.isShowing())                      // fuera de pantalla (tests): no hay dónde mostrarlo
        return;

    buildStyleMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                                          [safe = juce::Component::SafePointer<MeteringABPanel> (this)] (int result)
    {
        if (safe != nullptr)
            safe->applyStyleMenuResult (result);
    });
}

// Abre un selector de color; elegir un color pasa a la paleta "Personalizado".
void MeteringABPanel::pickCustomColour (bool forPre)
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
        [this, forPre, safe = juce::Component::SafePointer<MeteringABPanel> (this)] (juce::Colour c)
        {
            if (safe == nullptr)
                return;

            processor.apvts.state.setProperty (forPre ? "scopeCustomPre" : "scopeCustomPost", c.toString(), nullptr);
            applyScopeLook();
        });

    // Funciona tanto en el plugin como en la ventana aparte.
    auto* anchor = styleMenuAnchor != nullptr ? styleMenuAnchor : static_cast<juce::Component*> (&optionsButton);
    auto* host = anchor->getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::move (selector),
                                            host->getLocalArea (anchor, anchor->getLocalBounds()), host);
}


// Muestra u oculta la sección del osciloscopio y ajusta el alto de la ventana.
void MeteringABPanel::setScopeOpen (bool open)
{
    scopeOpen = open;
    processor.apvts.state.setProperty ("scopeOpenNow", open, nullptr);
    updateScopeSection();
}

void MeteringABPanel::updateScopeSection()
{
    // Receive: el osciloscopio a la derecha (si está abierto). Send: no hay.
    const bool showScope = ! isSendMode && scopeOpen;
    scopeToggleButton.setVisible (! isSendMode);
    scopeToggleButton.setToggleState (scopeOpen, juce::dontSendNotification);

    for (auto* c : scopeParts)
        c->setVisible (showScope);

    scopeLabel.setVisible (showScope);

    const int width  = isSendMode ? sendWidth : scopeOpen ? receiveWidth : sideWidth;
    const int height = isSendMode ? sendHeight : receiveHeight;

    if (getWidth() != width || getHeight() != height)
        setSize (width, height);
    else
        resized();

    repaint();
}

void MeteringABPanel::setWaves (bool pre, bool post)
{
    scope.setShowPre (pre);
    scope.setShowPost (post);
    processor.apvts.state.setProperty ("scopeShowPre",  pre,  nullptr);
    processor.apvts.state.setProperty ("scopeShowPost", post, nullptr);
    updateWaveButtons();
}

// Encendido: relleno con el color de su onda. Apagado: gris.
void MeteringABPanel::updateWaveButtons()
{
    auto style = [] (juce::TextButton& b, bool on, juce::Colour colour)
    {
        b.getProperties().set ("chipColour", (juce::int64) colour.getARGB());
        b.setToggleState (on, juce::dontSendNotification);
        b.repaint();
    };

    style (compareButton, compareOn, Theme::accentBright);

}

void MeteringABPanel::setCompare (bool on)
{
    compareOn = on;
    processor.apvts.state.setProperty ("scopeCompare", on, nullptr);

    if (on)
    {
        setScopeLinked (false);       // deja de seguir la escucha...
        setWaves (true, true);        // ...y muestra las dos (PRE Outline, POST Lines only)
    }
    else
    {
        setScopeLinked (true);        // vuelve a seguir la escucha (Detallado)
    }
}

void MeteringABPanel::setScopeLinked (bool linked)
{
    scopeLinked = linked;
    scope.setLinked (linked);
    processor.apvts.state.setProperty ("scopeLink", linked, nullptr);

    if (linked)
        syncWavesToListen();          // al prenderlo, mostrar enseguida lo que suena
    else
        updateWaveButtons();
}

void MeteringABPanel::syncWavesToListen()
{
    const bool listeningPre = processor.apvts.getRawParameterValue (ParamIDs::pre)->load() > 0.5f;
    setWaves (listeningPre, ! listeningPre);
}

void MeteringABPanel::setBoolParam (const char* paramId, bool on)
{
    if (auto* param = processor.apvts.getParameter (paramId))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

void MeteringABPanel::toggleAB()
{
    auto& apvts = processor.apvts;

    setBoolParam (ParamIDs::pre, apvts.getRawParameterValue (ParamIDs::pre)->load() < 0.5f);
}

void MeteringABPanel::setSync (bool on)
{
    if (auto* param = processor.apvts.getParameter (ParamIDs::sync))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

void MeteringABPanel::timerCallback()
{
    // Movimiento suave de los números en pantalla.
    // Si Logic deja de mandar audio (pausa), los medidores quedan en -∞ y se
    // ponen en cero para cuando vuelva el play.
    const auto sinceLastBlock = juce::Time::getMillisecondCounter() - processor.lastBlockMs.load (std::memory_order_relaxed);
    const bool audioStopped = processor.lastBlockMs.load (std::memory_order_relaxed) == 0 || sinceLastBlock > 300;

    if (audioStopped && ! audioWasStopped)
        processor.requestMeterReset();

    audioWasStopped = audioStopped;

    const float targetPre  = audioStopped ? -120.0f : processor.displayPre.load();
    const float targetPost = audioStopped ? -120.0f : processor.displayPost.load();
    const bool stopped = targetPre <= -119.0f && targetPost <= -119.0f;   // pausa (Logic parado o sin audio)

    if (stopped && ! metersFalling)
    {
        // Al parar, Original y Procesado bajan juntos: cada uno a su ritmo para
        // llegar a -∞ al mismo tiempo (el más alto baja a 65 dB/s, el otro más lento).
        const float top = juce::jmax (shownPre, shownPost);
        const float frames = juce::jmax (1.0f, (top - fallFloorDb) / fallDbPerSecond * (float) timerHz);
        fallStepPre  = shownPre  > fallFloorDb ? (shownPre  - fallFloorDb) / frames : 0.0f;
        fallStepPost = shownPost > fallFloorDb ? (shownPost - fallFloorDb) / frames : 0.0f;
    }

    metersFalling = stopped;

    auto ease = [stopped] (float& shown, float target, float stopStep)
    {
        if (target <= -119.0f)
        {
            // En pausa bajan de a poco hasta -∞ (como un medidor que cae), no de golpe.
            shown -= stopped ? stopStep : fallDbPerSecond / (float) timerHz;
            if (shown < fallFloorDb + 0.01f)
                shown = -120.0f;
        }
        else if (shown <= -119.0f) shown = target;             // arranca sin "subir" desde -∞
        else shown += (target - shown) * 0.35f;
    };
    ease (shownPre,  targetPre,  fallStepPre);
    ease (shownPost, targetPost, fallStepPost);

    // La ganancia aplicada queda a la vista (también en pausa) una vez que hubo algo medido.
    const bool wasShown = gainShown;
    gainShown = processor.gainMeasured.load();
    if (processor.displayGainValid.load())
        shownGain = wasShown ? shownGain + (processor.displayGain.load() - shownGain) * 0.35f
                             : processor.displayGain.load();   // después de un clic arranca del valor nuevo

    // Línea de la ganancia aplicada en el osciloscopio (botón Gain).
    scope.setGainLine (gainLineOn && gainShown ? std::optional<float> (std::round (shownGain * 100.0f) / 100.0f) : std::nullopt);

    // El osciloscopio sigue a la escucha: cuando cambia PRE/POST FX (botón,
    // tecla asignada o automatización), muestra solo la onda que suena.
    {
        const int listenPre = processor.apvts.getRawParameterValue (ParamIDs::pre)->load() > 0.5f ? 1 : 0;

        if (lastListenPre >= 0 && listenPre != lastListenPre)
        {
            if (compareOn)
                updateWaveButtons();      // PRE + POST: siguen las dos a la vista
            else if (scopeLinked)
                setWaves (listenPre == 1, listenPre == 0);
            else
                setScopeLinked (true);    // vuelve a seguir la escucha (y muestra la que suena)
        }

        if (listenPre != lastListenPre)
            scope.repaint();          // Overlay: el parlante de la leyenda sigue a la escucha
        lastListenPre = listenPre;
    }

    const bool sendMode = (int) processor.apvts.getRawParameterValue (ParamIDs::mode)->load() == 0;

    if (sendMode != isSendMode)
    {
        isSendMode = sendMode;

        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &windowSlider, &windowLabel, &latencyLabel, &latencyMsLabel,
                 &abButton, &listenLabel, &matchInfoLabel, &measureButton, &latencyInfoLabel, &matchBypassButton,

                 &syncButtons[0], &syncButtons[1], &windowInfoLabel })
            c->setVisible (! isSendMode);

        updateScopeSection();
    }

    // Match Gain Bypass
    {
        const bool bypassed = processor.apvts.getRawParameterValue (ParamIDs::matchBypass)->load() > 0.5f;
        matchBypassButton.setToggleState (bypassed, juce::dontSendNotification);
        matchInfoLabel.setText (bypassed ? "Match gain bypassed (real level)"_u : "Level-matched in LUFS (K-weighted)"_u, juce::dontSendNotification);
        matchInfoLabel.setColour (juce::Label::textColourId, bypassed ? Theme::warn : Colours::dim);
        if (bypassed != matchBypassShown) { matchBypassShown = bypassed; repaint(); }
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
    abButton.setColour (juce::TextButton::textColourOffId, pre ? Theme::preInk : Theme::accent);   // PRE gris, POST azul

    // Estado de la medición de latencia
    {
        using State = LatencyMeasurer::State;
        const auto& m = processor.latencyMeasurer;
        const double latency = processor.apvts.getRawParameterValue (ParamIDs::latency)->load();
        const bool whole = std::abs (latency - std::round (latency)) < 0.005;
        const auto samplesText = (whole ? juce::String (juce::roundToInt (latency)) : juce::String (latency, 2));
        const auto msText = juce::String (latency * 1000.0 / juce::jmax (1.0, processor.getCurrentSampleRate()), 2)+ " ms";
        juce::String text;
        juce::Colour colour = Colours::dim;

        switch (m.getState())
        {
            case State::measuring:
                text = "Measuring... " + juce::String (juce::roundToInt (m.getProgress() * 100.0f)) + "% (while playing)";
                colour = Colours::accent;
                break;
            case State::analysing:
                text = "Calculating...";
                colour = Colours::accent;
                break;
            case State::done:
                text = juce::String::fromUTF8 ("\xe2\x9c\x93") + " Measured";
                colour = Colours::good;
                break;
            case State::noSignal: text = "No audio: press play";          colour = Colours::bad; break;
            case State::unclear:  text = "No clear match";              colour = Colours::bad; break;
            case State::noSend:   text = "No Send connected";               colour = Colours::bad; break;
            case State::idle:
                if (latency < 0.005)
                    text = "Press play and click Measure"_u;
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
        measureButton.setButtonText (busy ? "..." : "Measure");
        measureButton.setEnabled (! busy);
    }

    // Mensaje de estado.
    const int link = (int) processor.apvts.getRawParameterValue (ParamIDs::link)->load();

    if (! linkValueLabel.isBeingEdited())
        linkValueLabel.setText (juce::String (link), juce::dontSendNotification);

    if (processor.duplicateSend.load())
    {
        statusLabel.setText ("More than one Send on Link ID "_u + juce::String (link) + ": change one."_u, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Colours::bad);
    }
    else if (isSendMode)
    {
        statusLabel.setText ("Send active on Link ID " + juce::String (link) + ".", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Colours::good);
    }
    else if (processor.sendConnected.load())
    {
        statusLabel.setText ("Connected to the Send on Link ID " + juce::String (link) + ".", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Colours::good);
    }
    else
    {
        statusLabel.setText ("No Send playing on Link ID "_u + juce::String (link) + " (press play).", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Colours::bad);
    }

    repaint (meterArea());
    repaint (statusLabel.getBounds().withX (0).withWidth (40));   // puntito del estado

    if (! isSendMode && ! scope.isFrozen())
        scope.repaint();
}

// Número grande (letra fina) con la unidad chica al lado, centrados juntos.
static void drawNumberWithUnit (juce::Graphics& g, const juce::String& number, const juce::String& unit,
                                juce::Rectangle<int> area, float size, juce::Colour colour, int align = 0)   // -1 izq, 0 centro, 1 der
{
    const auto numberFont = Theme::font (size, 300);
    const auto unitFont   = Theme::font (size * 0.44f, 400);
    const float gap = unit.isEmpty() ? 0.0f : 4.0f;
    const float wNum  = juce::GlyphArrangement::getStringWidth (numberFont, number);
    const float wUnit = unit.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (unitFont, unit);
    const float total = wNum + gap + wUnit;
    float x = align < 0 ? (float) area.getX()
            : align > 0 ? (float) area.getRight() - total
                        : (float) area.getCentreX() - total * 0.5f;
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

void MeteringABPanel::paint (juce::Graphics& g)
{
    // Receive: columna gris a la izquierda y el osciloscopio en oscuro a la derecha.
    // Send: toda la franja gris.
    const int sideW = isSendMode ? getWidth() : sideWidth;
    g.setGradientFill (juce::ColourGradient (Theme::topLight, 0.0f, 0.0f, Theme::topDark, 0.0f, (float) getHeight(), false));
    g.fillRect (0, 0, sideW, getHeight());

    if (! isSendMode)
    {
        g.setColour (Theme::darkBg);
        g.fillRect (sideW, 0, getWidth() - sideW, getHeight());
    }

    // Título
    g.setColour (juce::Colour (0xff3a3d42));
    g.setFont (Theme::font (14.0f, 600, 0.3f));
    g.drawText ("METERING A/B", 20, 5, 240, 32, juce::Justification::centredLeft);

    // Pastilla del Link ID (− 1 +)
    {
        const auto r = linkMinus.getBounds().getUnion (linkPlus.getBounds()).toFloat();
        g.setColour (Theme::pill);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    }

    // Puntito de color al lado del estado
    {
        const auto lb = statusLabel.getBounds().toFloat();
        const float dotY = lb.getCentreY();
        g.setColour (statusLabel.findColour (juce::Label::textColourId));
        g.fillEllipse (lb.getX() - 13.0f, dotY - 3.5f, 7.0f, 7.0f);
    }

    // Send: solo título, modo, Link ID y estado (ventana chica).
    if (isSendMode)
        return;

    // Números: Original | Procesado | Ganancia aplicada, juntos en un recuadro hundido (uno debajo del otro).
    const auto meters = meterArea();
    Theme::drawWell (g, meters.toFloat(), 14.0f);

    // En pausa (o después de un clic) los niveles quedan en -∞ y la ganancia sin valor.
    auto level = [] (float db) { return db <= -119.0f ? juce::String::fromUTF8 ("-\xe2\x88\x9e") : juce::String (db, 1); };
    struct Readout { juce::String title, number, unit; juce::Colour colour; };
    const Readout readouts[] = {
        { "Original (Send)",   level (shownPre),  "LUFS", Theme::inkStrong },
        { "Processed",         level (shownPost), "LUFS", Theme::inkStrong },
        { "Applied gain", gainShown ? (shownGain >= 0 ? "+" : "") + juce::String (shownGain, 2) : juce::String ("—"_u),
                               gainShown ? "dB" : "", matchBypassShown ? Theme::inkFaint : Theme::accent },
    };

    auto rows = meters.reduced (16, 4);
    const int rowHeight = rows.getHeight() / 3;

    for (int i = 0; i < 3; ++i)
    {
        auto row = rows.removeFromTop (rowHeight);
        if (i < 2)
        {
            g.setColour (juce::Colours::black.withAlpha (0.06f));
            g.fillRect (row.getX(), row.getBottom(), row.getWidth(), 1);
        }
        g.setColour (Theme::inkDim);
        g.setFont (Theme::font (12.0f, 300));
        g.drawText (readouts[i].title, row.withTrimmedTop (6), juce::Justification::centredLeft);
        drawNumberWithUnit (g, readouts[i].number, readouts[i].unit, row, 27.0f, readouts[i].colour, 1);
    }

    // Fondo de ms | Sync
    {
        const auto r = syncButtons[0].getBounds().getUnion (syncButtons[1].getBounds()).toFloat();
        g.setColour (Theme::pill);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    }

    // Latencia medida: número grande + "muestras"
    drawNumberWithUnit (g, latencyNumberText, "samples", latencyNumberArea, 28.0f, Theme::inkStrong, -1);

    // Líneas finas entre Ventana | Latencia | Escuchar
    g.setColour (juce::Colours::black.withAlpha (0.07f));
    for (int y : { latencyLabel.getY() - 10, listenLabel.getY() - 10 })
        g.fillRect (20, y, sideWidth - 40, 1);
}

// Clic sobre los números: medidores a cero (vuelven a medir desde ahí).
juce::Rectangle<int> MeteringABPanel::meterArea() const
{
    return metersBounds;
}

void MeteringABPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! isSendMode && meterArea().contains (e.getPosition()))
        resetMeters();
}

void MeteringABPanel::resetMeters()
{
    processor.requestMeterReset();
    processor.gainMeasured = false;                 // la ganancia aplicada también se borra ("—")...
    processor.displayGainValid = false;             // ...hasta que haya una medición nueva
    gainShown = false;
    shownPre = shownPost = -120.0f;                 // el clic los borra en el momento
    repaint (meterArea());
}

void MeteringABPanel::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (! isSendMode && meterArea().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                                           : juce::MouseCursor::NormalCursor);
}

void MeteringABPanel::resized()
{
    // ---- Columna gris (de arriba a abajo). En Send queda solo esta parte de arriba. ----
    auto k = juce::Rectangle<int> (20, 0, (isSendMode ? sendWidth : sideWidth) - 40, getHeight());
    k.removeFromTop (40);                                       // título (se dibuja en paint)
    modeBox.setBounds (k.removeFromTop (30));
    k.removeFromTop (6);
    {
        auto row = k.removeFromTop (30);
        linkPlus.setBounds (row.removeFromRight (32));
        linkValueLabel.setBounds (row.removeFromRight (40));
        linkMinus.setBounds (row.removeFromRight (32));
        linkLabel.setBounds (row);
        linkLabel.setJustificationType (juce::Justification::centredLeft);
    }
    k.removeFromTop (4);

    // El estado, en un renglón (igual en Send y Receive). El Send termina acá: corta
    // justo antes de donde en el Receive empiezan los números.
    statusLabel.setBounds (k.removeFromTop (22).withTrimmedLeft (14));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    if (isSendMode)
        return;

    metersBounds = { 20, k.getY() + 2, sideWidth - 40, 124 };   // medidores (se dibujan en paint)
    k.removeFromTop (metersBounds.getBottom() - k.getY());
    k.removeFromTop (12);

    // Ventana: título y ms|Sync, perilla con el valor, equivalencia abajo.
    {
        auto header = k.removeFromTop (24);
        syncButtons[1].setBounds (header.removeFromRight (46));
        syncButtons[0].setBounds (header.removeFromRight (38));
        windowLabel.setBounds (header);
        k.removeFromTop (2);
        const auto knobs = k.removeFromTop (80);
        windowSlider.setBounds (knobs);
        windowSyncSlider.setBounds (knobs);
        windowInfoLabel.setBounds (k.removeFromTop (16));
    }
    k.removeFromTop (14);

    // Latencia: número a la izquierda, Medir a la derecha, estado debajo.
    {
        latencyLabel.setBounds (k.removeFromTop (20));
        k.removeFromTop (4);
        auto row = k.removeFromTop (50);
        auto rightSide = row.removeFromRight (126);
        measureButton.setBounds (rightSide.withSizeKeepingCentre (126, 42).translated (0, -3));
        latencyNumberArea = row.removeFromTop (34);
        latencyMsLabel.setBounds (row.removeFromTop (16));
        latencyInfoLabel.setBounds (k.removeFromTop (16));
    }
    k.removeFromTop (14);

    // Escuchar: el botón grande PRE FX / POST FX y, al pie, cómo se nivela y Scope.
    {
        auto head = k.removeFromTop (22);
        matchBypassButton.setBounds (head.removeFromRight (128));
        listenLabel.setBounds (head);
        k.removeFromTop (4);
        abButton.setBounds (k.removeFromTop (60).expanded (4, 0));
        k.removeFromTop (14);
        auto row = k.removeFromTop (34);
        scopeToggleButton.setBounds (row.removeFromRight (116));   // Scope: abrir / cerrar el osciloscopio
        matchInfoLabel.setBounds (row);
        matchInfoLabel.setJustificationType (juce::Justification::centredLeft);
    }

    // ---- Osciloscopio (a la derecha, siempre) ----
    auto section = getLocalBounds().withTrimmedLeft (sideWidth).reduced (18, 0);
    auto scopeHeader = section.removeFromTop (scopeHeaderHeight).withSizeKeepingCentre (section.getWidth(), 28);
    zoomBox.setBounds (scopeHeader.removeFromRight (96));
    scopeHeader.removeFromRight (8);
    // De izquierda a derecha: Overlay, Gain, Freeze, Options (y el largo).
    optionsButton.setBounds (scopeHeader.removeFromRight (74));
    scopeHeader.removeFromRight (6);
    freezeButton.setBounds (scopeHeader.removeFromRight (64));
    scopeHeader.removeFromRight (6);
    gainLineButton.setBounds (scopeHeader.removeFromRight (58));
    scopeHeader.removeFromRight (6);
    compareButton.setBounds (scopeHeader.removeFromRight (104));
    scopeLabel.setBounds (scopeHeader);

    scope.setBounds (section.withTrimmedBottom (16));
}

// ---------------------------------------------------------------------------
//  Ventana escalable
// ---------------------------------------------------------------------------
MeteringABEditor::MeteringABEditor (MeteringABProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    addAndMakeVisible (panel);
    scale = juce::jlimit (minScale, maxScale, (float) (double) processor.apvts.state.getProperty ("uiScaleV6", (double) defaultScale));

    // Esquina para arrastrar; mantiene la proporción del plugin.
    setResizable (true, true);
    setConstrainer (&constrainer);
    fitToPanel();
}

MeteringABEditor::~MeteringABEditor() = default;

void MeteringABEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::darkBg);
}

// Proporción y límites según el alto actual del panel (cambia al abrir el osciloscopio o pasar a Send).
void MeteringABEditor::fitToPanel()
{
    const juce::ScopedValueSetter<bool> busy (fitting, true);
    const int w = juce::jmax (1, panel.getWidth()), h = juce::jmax (1, panel.getHeight());
    const bool send = panel.isSendLayout();

    // Send: la misma escala que el Receive (su columna gris queda igual), pero sin
    // esquina para escalarlo. Receive: con la esquina.
    setResizable (! send, ! send);
    if (resizableCorner != nullptr)                  // clic derecho en la esquina: menú de tamaño
    {
        resizableCorner->removeMouseListener (this);
        resizableCorner->addMouseListener (this, false);
    }
    shownScale = scale;

    constrainer.setFixedAspectRatio ((double) w / (double) h);
    constrainer.setSizeLimits (juce::roundToInt (w * minScale), juce::roundToInt (h * minScale),
                               juce::roundToInt (w * maxScale), juce::roundToInt (h * maxScale));

    if (send)
        constrainer.setSizeLimits (juce::roundToInt (w * scale), juce::roundToInt (h * scale),
                                   juce::roundToInt (w * scale), juce::roundToInt (h * scale));

    setSize (juce::roundToInt (w * shownScale), juce::roundToInt (h * shownScale));
    resized();
}

void MeteringABEditor::resized()
{
    if (! fitting && ! panel.isSendLayout())
    {
        // El usuario arrastró la esquina: la escala sale del ancho.
        scale = juce::jlimit (minScale, maxScale, (float) getWidth() / (float) juce::jmax (1, panel.getWidth()));
        shownScale = scale;
        processor.apvts.state.setProperty ("uiScaleV6", scale, nullptr);
    }

    panel.setTransform (juce::AffineTransform::scale (shownScale));

}

void MeteringABEditor::setUiScale (float newScale)
{
    scale = juce::jlimit (minScale, maxScale, newScale);
    processor.apvts.state.setProperty ("uiScaleV6", scale, nullptr);
    fitToPanel();
}

void MeteringABEditor::childBoundsChanged (juce::Component* child)
{
    if (child == &panel && ! fitting)
        fitToPanel();
}

// Clic derecho (o Ctrl + clic) en la esquina de abajo a la derecha: "Reset size".
void MeteringABEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && resizableCorner != nullptr && e.eventComponent == resizableCorner.get())
        showSizeMenu();
}

void MeteringABEditor::showSizeMenu()
{
    ++sizeMenuOpenedForTests;

    if (resizableCorner == nullptr || ! resizableCorner->isShowing())   // fuera de pantalla (tests)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Reset size", std::abs (scale - defaultScale) > 0.005f);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (resizableCorner.get()),
                        [safe = juce::Component::SafePointer<MeteringABEditor> (this)] (int result)
                        {
                            if (safe != nullptr && result == 1)
                                safe->setUiScale (defaultScale);
                        });
}
