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

namespace Colours
{
    const juce::Colour background { 0xff16181d };
    const juce::Colour panel      { 0xff20232b };
    const juce::Colour outline    { 0xff2e323c };
    const juce::Colour text       { 0xffe8e6e3 };
    const juce::Colour dim        { 0xff8a8f98 };
    const juce::Colour accent     { 0xfff2b544 };
    const juce::Colour good       { 0xff6fcf97 };
    const juce::Colour bad        { 0xffeb5757 };
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
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, Colours::background);
    setColour (juce::Slider::textBoxOutlineColourId, Colours::outline);
    setColour (juce::Slider::textBoxHighlightColourId, Colours::accent.withAlpha (0.4f));
    setColour (juce::TextEditor::backgroundColourId, Colours::background);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    setColour (juce::CaretComponent::caretColourId, Colours::accent);
    setColour (juce::ComboBox::backgroundColourId, Colours::panel);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::arrowColourId, Colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::background);
    setColour (juce::TextButton::buttonColourId, Colours::panel);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::background);
    setColour (juce::ToggleButton::textColourId, Colours::text);
    setColour (juce::ToggleButton::tickColourId, Colours::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Colours::dim);
    setColour (juce::Label::textColourId, Colours::text);
}

juce::Font MeteringABLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (button.getProperties().contains ("big"))
        return juce::FontOptions (19.0f, juce::Font::bold);

    return juce::FontOptions (juce::jmin (15.0f, (float) buttonHeight * 0.6f));
}

void MeteringABLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = startAngle + pos * (endAngle - startAngle);
    const float track = 4.0f;

    juce::Path back;
    back.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::outline);
    g.strokePath (back, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
    g.setColour (Colours::accent);
    g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto knobRadius = radius - track * 3.0f;
    g.setColour (Colours::panel);
    g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2, knobRadius * 2);

    const auto tip = centre.getPointOnCircumference (knobRadius - 4.0f, angle);
    g.setColour (Colours::text);
    g.drawLine ({ centre.getPointOnCircumference (knobRadius * 0.35f, angle), tip }, 2.5f);
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
    addCombo (measureBox, ParamIDs::measure, measureAtt);

    measureLabel.setColour (juce::Label::textColourId, Colours::dim);
    measureLabel.setFont (juce::FontOptions (12.0f));
    measureLabel.setText ("Medición"_u, juce::dontSendNotification);
    addAndMakeVisible (measureLabel);

    // Link ID: flechitas + número editable.
    linkSlider.setSliderStyle (juce::Slider::IncDecButtons);
    linkSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 26);
    linkSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    linkLabel.setText ("Link ID", juce::dontSendNotification);
    linkLabel.setColour (juce::Label::textColourId, Colours::dim);
    linkLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (linkSlider);
    addAndMakeVisible (linkLabel);
    linkAtt = std::make_unique<SliderAttachment> (apvts, ParamIDs::link, linkSlider);

    setupKnob (windowSlider,  windowLabel,  "Ventana");
    // Latencia: sin perilla, solo el botón Medir y el valor medido.
    latencyLabel.setText ("Latencia", juce::dontSendNotification);
    latencyLabel.setJustificationType (juce::Justification::centred);
    latencyLabel.setColour (juce::Label::textColourId, Colours::dim);
    latencyLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (latencyLabel);

    latencyValueLabel.setJustificationType (juce::Justification::centred);
    latencyValueLabel.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    latencyValueLabel.setColour (juce::Label::textColourId, Colours::text);
    addAndMakeVisible (latencyValueLabel);

    latencyMsLabel.setJustificationType (juce::Justification::centred);
    latencyMsLabel.setFont (juce::FontOptions (12.0f));
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
        syncButtons[i].setClickingTogglesState (false);
        syncButtons[i].onClick = [this, i] { setSync (i == 1); };
        addAndMakeVisible (syncButtons[i]);
    }

    windowInfoLabel.setJustificationType (juce::Justification::centred);
    windowInfoLabel.setColour (juce::Label::textColourId, Colours::dim);
    windowInfoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (windowInfoLabel);

    // Botón único PRE FX / POST FX (cada clic alterna).
    abButton.getProperties().set ("big", true);
    abButton.setClickingTogglesState (false);
    abButton.onClick = [this] { toggleAB(); };
    addAndMakeVisible (abButton);

    deltaButton.setButtonText ("DELTA");
    deltaButton.setClickingTogglesState (false);
    deltaButton.onClick = [this]
    {
        setBoolParam (ParamIDs::delta, processor.apvts.getRawParameterValue (ParamIDs::delta)->load() < 0.5f);
    };
    addAndMakeVisible (deltaButton);

    listenLabel.setText ("Escuchar  ·  clic para alternar"_u, juce::dontSendNotification);
    listenLabel.setColour (juce::Label::textColourId, Colours::dim);
    listenLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (listenLabel);

    // Medir latencia
    measureButton.setButtonText ("Medir");
    measureButton.setColour (juce::TextButton::textColourOffId, Colours::accent);
    measureButton.onClick = [this] { processor.startLatencyMeasurement(); };
    addAndMakeVisible (measureButton);

    // Ø: compensar un plugin que invierte la polaridad (lo pone solo "Medir").
    polarityButton.setClickingTogglesState (true);
    polarityButton.setTooltip ("Polaridad invertida");
    polarityButton.setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    addAndMakeVisible (polarityButton);
    polarityAtt = std::make_unique<ButtonAttachment> (apvts, ParamIDs::polarity, polarityButton);

    latencyInfoLabel.setJustificationType (juce::Justification::centred);
    latencyInfoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (latencyInfoLabel);

    addAndMakeVisible (holdButton);
    holdAtt = std::make_unique<ButtonAttachment> (apvts, ParamIDs::hold, holdButton);

    // Osciloscopio sincronizado + largo en compases (se guarda con el proyecto).
    addAndMakeVisible (scope);
    scopeLabel.setText ({}, juce::dontSendNotification);
    scope.setTooltip ("Clic para congelar / descongelar");
    scopeLabel.setColour (juce::Label::textColourId, Colours::dim);
    scopeLabel.setFont (juce::FontOptions (12.0f));
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

    updateWaveButtons();

    // ▾ Osciloscopio: mostrar u ocultar toda la sección (se guarda con el proyecto).
    scopeToggleButton.setClickingTogglesState (false);
    scopeToggleButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    scopeToggleButton.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    scopeToggleButton.setColour (juce::TextButton::textColourOffId, Colours::text);
    scopeToggleButton.setTooltip ("Mostrar u ocultar el osciloscopio");
    scopeToggleButton.onClick = [this] { setScopeOpen (! scopeOpen); };
    addAndMakeVisible (scopeToggleButton);
    scopeOpen = (bool) apvts.state.getProperty ("scopeOpen", true);

    // Estilo: forma de dibujar, colores y grilla (se guardan con el proyecto).
    scopeStyleButton.setTooltip ("Forma, colores y grilla del osciloscopio");
    scopeStyleButton.onClick = [this] { showStyleMenu(); };
    addAndMakeVisible (scopeStyleButton);
    applyScopeLook();

    statusLabel.setFont (juce::FontOptions (13.0f));
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
    setLookAndFeel (nullptr);
}

void MeteringABEditor::setupKnob (PreciseSlider& s, juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, Colours::dim);
    label.setFont (juce::FontOptions (12.0f));
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

    juce::CallOutBox::launchAsynchronously (std::move (selector), scopeStyleButton.getBounds(), this);
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
    const bool showBody = showSection && scopeOpen;

    scopeToggleButton.setVisible (showSection);
    scopeToggleButton.setButtonText (juce::String::fromUTF8 (scopeOpen ? "\xe2\x96\xbe" : "\xe2\x96\xb8") + "  Osciloscopio");

    for (juce::Component* c : std::initializer_list<juce::Component*> { &scope, &scopeLabel, &zoomBox, &showPreButton, &showPostButton, &linkButton, &scopeStyleButton })
        c->setVisible (showBody);

    const int height = ! showSection ? mainHeight
                     : scopeOpen     ? mainHeight + scopeHeaderHeight + scopeBodyHeight
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
        b.setColour (juce::TextButton::buttonColourId, on ? colour : Colours::panel);
        b.setColour (juce::TextButton::textColourOffId, on ? Colours::background : Colours::dim);
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

    // Si está en DELTA, el primer clic sale de DELTA y vuelve a la última versión.
    if (apvts.getRawParameterValue (ParamIDs::delta)->load() > 0.5f)
        setBoolParam (ParamIDs::delta, false);
    else
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
                 &measureBox, &measureLabel, &windowSlider, &windowLabel, &latencyLabel, &polarityButton, &latencyValueLabel, &latencyMsLabel,
                 &abButton, &deltaButton, &listenLabel, &measureButton, &latencyInfoLabel, &holdButton,

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

    // Botón PRE/POST y DELTA
    const bool pre   = processor.apvts.getRawParameterValue (ParamIDs::pre)->load() > 0.5f;
    const bool delta = processor.apvts.getRawParameterValue (ParamIDs::delta)->load() > 0.5f;
    const juce::Colour preColour { 0xff5dcaa5 }, preText { 0xff04342c };

    abButton.setButtonText (pre ? "PRE FX" : "POST FX");

    if (delta)
    {
        abButton.setColour (juce::TextButton::buttonColourId, Colours::panel);
        abButton.setColour (juce::TextButton::textColourOffId, Colours::dim);
    }
    else
    {
        abButton.setColour (juce::TextButton::buttonColourId, pre ? preColour : Colours::accent);
        abButton.setColour (juce::TextButton::textColourOffId, pre ? preText : Colours::background);
    }

    deltaButton.setColour (juce::TextButton::buttonColourId, delta ? Colours::text : Colours::panel);
    deltaButton.setColour (juce::TextButton::textColourOffId, delta ? Colours::background : Colours::text);

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

        latencyValueLabel.setText (samplesText + " muestras", juce::dontSendNotification);
        latencyMsLabel.setText ("= " + msText, juce::dontSendNotification);

        const bool busy = m.getState() == State::measuring || m.getState() == State::analysing;
        measureButton.setButtonText (busy ? "..." : "Medir");
        measureButton.setEnabled (! busy);
    }

    // Mensaje de estado.
    const int link = (int) processor.apvts.getRawParameterValue (ParamIDs::link)->load();

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

    if (! isSendMode && scopeOpen && ! scope.isFrozen())
        scope.repaint();
}

void MeteringABEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Título
    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("METERING A/B", 20, 10, 210, 32, juce::Justification::centredLeft);

    // Panel de medidores
    auto meterArea = juce::Rectangle<int> (20, 80, getWidth() - 40, 70);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (meterArea.toFloat(), 8.0f);

    const auto unit = (int) processor.apvts.getRawParameterValue (ParamIDs::measure)->load() == 1 ? " LUFS" : " dB";

    auto formatLevel = [unit] (float db)
    {
        return db <= -119.0f ? juce::String ("—"_u) : juce::String (db, 1) + unit;
    };

    struct Readout { juce::String title, value; juce::Colour colour; };
    std::vector<Readout> readouts;

    if (isSendMode)
    {
        readouts.push_back ({ "Nivel original", formatLevel (shownPre), Colours::text });
    }
    else
    {
        const auto gainText = (shownGain >= 0 ? "+" : "") + juce::String (shownGain, 2) + " dB";
        readouts.push_back ({ "Original (Send)",   formatLevel (shownPre),  Colours::text });
        readouts.push_back ({ "Ganancia aplicada", gainText,                Colours::accent });
        readouts.push_back ({ "Procesado",         formatLevel (shownPost), Colours::text });
    }

    const int colWidth = meterArea.getWidth() / (int) readouts.size();

    for (size_t i = 0; i < readouts.size(); ++i)
    {
        auto col = meterArea.withX (meterArea.getX() + (int) i * colWidth).withWidth (colWidth).reduced (10, 8);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (readouts[i].title, col.removeFromTop (16), juce::Justification::centred);
        g.setColour (readouts[i].colour);
        g.setFont (juce::FontOptions (23.0f, juce::Font::bold));
        g.drawText (readouts[i].value, col, juce::Justification::centred);
    }

    if (isSendMode)
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Este Metering A/B va PRIMERO en la cadena de efectos.\n"
                          "Poné otro Metering A/B en modo Receive al FINAL, con el mismo Link ID.\n"_u
                          "El A/B, el DELTA y el osciloscopio están en el Receive."_u,
                          juce::Rectangle<int> (20, 180, getWidth() - 40, 80), juce::Justification::centred, 4);
    }
    else
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Clic en un número para escribir el valor exacto  ·  Shift + arrastrar = ajuste fino  ·  Doble clic = valor por defecto"_u,
                    20, mainHeight - 24, getWidth() - 40, 16, juce::Justification::centred);

        // Línea que separa la sección del osciloscopio
        g.setColour (Colours::outline);
        g.fillRect (0, mainHeight, getWidth(), 1);
    }
}

void MeteringABEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 0);

    // Fila superior: título | modo | link ID
    auto top = area.removeFromTop (52).withTrimmedTop (10).withHeight (32);
    top.removeFromLeft (210);
    linkSlider.setBounds (top.removeFromRight (110));
    linkLabel.setBounds (top.removeFromRight (60));
    top.removeFromRight (12);
    modeBox.setBounds (top);

    statusLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);
    area.removeFromTop (70);   // medidores (se dibujan en paint)
    area.removeFromTop (12);

    // Fila de escucha: PRE/POST | DELTA | Medición | Congelar
    auto row = area.removeFromTop (50);
    auto abArea = row.removeFromLeft (200);
    listenLabel.setBounds (abArea.removeFromTop (16).withWidth (260));
    abButton.setBounds (abArea.withHeight (34));
    row.removeFromLeft (10);
    deltaButton.setBounds (row.removeFromLeft (90).withTrimmedTop (16).withHeight (34));

    row.removeFromLeft (20);
    auto measureArea = row.removeFromLeft (140);
    measureLabel.setBounds (measureArea.removeFromTop (16));
    measureBox.setBounds (measureArea.withHeight (30).translated (0, 2));

    row.removeFromLeft (16);
    holdButton.setBounds (row.withTrimmedTop (18).withHeight (30));

    // Perillas: Ventana y Latencia, más bajas.
    area.removeFromTop (10);
    auto knobs = area.removeFromTop (126).reduced (40, 0);
    const int kw = 240;

    // Ventana: botones ms|Sync arriba, perilla, equivalencia abajo.
    {
        auto k = knobs.removeFromLeft (kw);
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
        auto k = knobs.removeFromRight (kw);
        latencyLabel.setBounds (k.removeFromTop (20));
        latencyValueLabel.setBounds (k.removeFromTop (26));
        latencyMsLabel.setBounds (k.removeFromTop (16));
        k.removeFromTop (8);
        auto buttons = k.removeFromTop (34).withSizeKeepingCentre (176, 34);
        measureButton.setBounds (buttons.removeFromLeft (132));
        buttons.removeFromLeft (8);
        polarityButton.setBounds (buttons);
        k.removeFromTop (6);
        latencyInfoLabel.setBounds (k.removeFromTop (16));
    }

    // ---- Sección del osciloscopio (debajo de la línea) ----
    auto section = getLocalBounds().withTrimmedTop (mainHeight + 1).reduced (20, 0);

    auto scopeHeader = section.removeFromTop (scopeHeaderHeight - 1).withSizeKeepingCentre (section.getWidth(), 28);
    scopeToggleButton.setBounds (scopeHeader.removeFromLeft (128).translated (-8, 0));
    zoomBox.setBounds (scopeHeader.removeFromRight (100));
    scopeHeader.removeFromRight (10);
    showPostButton.setBounds (scopeHeader.removeFromRight (74));
    scopeHeader.removeFromRight (6);
    showPreButton.setBounds (scopeHeader.removeFromRight (66));
    scopeHeader.removeFromRight (6);
    linkButton.setBounds (scopeHeader.removeFromRight (64));
    scopeHeader.removeFromRight (6);
    scopeStyleButton.setBounds (scopeHeader.removeFromRight (64));
    scopeLabel.setBounds (scopeHeader);

    scope.setBounds (section.removeFromTop (scopeBodyHeight - 16));
}
