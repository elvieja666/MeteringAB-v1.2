#include "PluginEditor.h"
#include "Text.h"

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
    setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, 22);
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
    addCombo (matchBox, ParamIDs::match, matchAtt);
    addCombo (measureBox, ParamIDs::measure, measureAtt);

    for (auto* l : { &matchLabel, &measureLabel })
    {
        l->setColour (juce::Label::textColourId, Colours::dim);
        l->setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (*l);
    }
    matchLabel.setText ("Matcheo", juce::dontSendNotification);
    measureLabel.setText ("Medición"_u, juce::dontSendNotification);

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

    setupKnob (manualSlider,  manualLabel,  "Ganancia manual");
    setupKnob (windowSlider,  windowLabel,  "Ventana");
    setupKnob (limitSlider,   limitLabel,   "Límite auto"_u);
    setupKnob (latencySlider, latencyLabel, "Latencia");

    manualAtt  = std::make_unique<SliderAttachment> (apvts, ParamIDs::manual,  manualSlider);
    windowAtt  = std::make_unique<SliderAttachment> (apvts, ParamIDs::windowMs, windowSlider);
    limitAtt   = std::make_unique<SliderAttachment> (apvts, ParamIDs::limit,   limitSlider);
    latencyAtt = std::make_unique<SliderAttachment> (apvts, ParamIDs::latency, latencySlider);

    // Doble clic vuelve al valor por defecto.
    manualSlider.setDoubleClickReturnValue (true, 0.0);
    windowSlider.setDoubleClickReturnValue (true, 3000.0);

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
    limitSlider.setDoubleClickReturnValue (true, 18.0);
    latencySlider.setDoubleClickReturnValue (true, 0.0);

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

    latencyInfoLabel.setJustificationType (juce::Justification::centred);
    latencyInfoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (latencyInfoLabel);

    addAndMakeVisible (holdButton);
    holdAtt = std::make_unique<ButtonAttachment> (apvts, ParamIDs::hold, holdButton);

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    shownPre  = processor.displayPre.load();
    shownPost = processor.displayPost.load();
    shownGain = processor.displayGain.load();

    setSize (640, 478);
    timerCallback();
    startTimerHz (20);
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

    const bool sendMode = (int) processor.apvts.getRawParameterValue (ParamIDs::mode)->load() == 0;

    if (sendMode != isSendMode)
    {
        isSendMode = sendMode;

        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &matchBox, &measureBox, &matchLabel, &measureLabel, &manualSlider, &windowSlider,
                 &limitSlider, &latencySlider, &manualLabel, &windowLabel, &limitLabel, &latencyLabel,
                 &abButton, &deltaButton, &listenLabel, &measureButton, &latencyInfoLabel, &holdButton,
                 &syncButtons[0], &syncButtons[1], &windowInfoLabel })
            c->setVisible (! isSendMode);
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
        const int latency = (int) processor.apvts.getRawParameterValue (ParamIDs::latency)->load();
        const auto msText = juce::String (latency * 1000.0 / juce::jmax (1.0, processor.getCurrentSampleRate()), 1).replaceCharacter ('.', ',') + " ms";
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
                text = "Medido: " + juce::String (latency) + " = " + msText;
                colour = Colours::good;
                break;
            case State::noSignal: text = "No llega audio: dale play";          colour = Colours::bad; break;
            case State::unclear:  text = "Sin coincidencia clara";              colour = Colours::bad; break;
            case State::noSend:   text = "No hay Send conectado";               colour = Colours::bad; break;
            case State::idle:     text = "= " + msText;                          break;
        }

        latencyInfoLabel.setText (text, juce::dontSendNotification);
        latencyInfoLabel.setColour (juce::Label::textColourId, colour);

        const bool busy = m.getState() == State::measuring || m.getState() == State::analysing;
        measureButton.setButtonText (busy ? "..." : "Medir");
        measureButton.setEnabled (! busy);
    }

    const bool manual = (int) processor.apvts.getRawParameterValue (ParamIDs::match)->load() == 1;
    manualSlider.setEnabled (manual);
    manualSlider.setAlpha (manual ? 1.0f : 0.4f);

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

    repaint (0, 90, getWidth(), 100);
}

void MeteringABEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Título
    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("METERING A/B", 20, 14, 210, 32, juce::Justification::centredLeft);

    // Panel de medidores
    auto meterArea = juce::Rectangle<int> (20, 94, getWidth() - 40, 90);
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
        readouts.push_back ({ "Original (Send)", formatLevel (shownPre),  Colours::text });
        readouts.push_back ({ "Procesado",       formatLevel (shownPost), Colours::text });
        readouts.push_back ({ "Ganancia aplicada", gainText,              Colours::accent });
    }

    const int colWidth = meterArea.getWidth() / (int) readouts.size();

    for (size_t i = 0; i < readouts.size(); ++i)
    {
        auto col = meterArea.withX (meterArea.getX() + (int) i * colWidth).withWidth (colWidth).reduced (10, 12);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (readouts[i].title, col.removeFromTop (18), juce::Justification::centred);
        g.setColour (readouts[i].colour);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText (readouts[i].value, col, juce::Justification::centred);
    }

    if (isSendMode)
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Este Metering A/B va PRIMERO en la cadena de efectos.\n"
                          "Poné otro Metering A/B en modo Receive al FINAL, con el mismo Link ID.\n"_u
                          "Los controles de matcheo y el A/B están en el Receive."_u,
                          juce::Rectangle<int> (20, 210, getWidth() - 40, 80), juce::Justification::centred, 4);
    }
    else
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Clic en un número para escribir el valor exacto  ·  Shift + arrastrar = ajuste fino  ·  Doble clic = valor por defecto"_u,
                    20, getHeight() - 24, getWidth() - 40, 18, juce::Justification::centred);
    }
}

void MeteringABEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 0);

    // Fila superior: título | modo | link ID
    auto top = area.removeFromTop (60).withTrimmedTop (14).withHeight (32);
    top.removeFromLeft (210);
    linkSlider.setBounds (top.removeFromRight (110));
    linkLabel.setBounds (top.removeFromRight (60));
    top.removeFromRight (12);
    modeBox.setBounds (top);

    statusLabel.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);
    area.removeFromTop (90);   // medidores (se dibujan en paint)
    area.removeFromTop (14);

    // Fila de A/B y opciones
    auto row = area.removeFromTop (56);
    auto abArea = row.removeFromLeft (160);
    listenLabel.setBounds (abArea.removeFromTop (16).withWidth (240));
    abButton.setBounds (abArea.withHeight (40).translated (0, -2));
    row.removeFromLeft (8);
    deltaButton.setBounds (row.removeFromLeft (72).withTrimmedTop (14).withHeight (40));
    row.removeFromLeft (12);
    auto matchArea = row.removeFromLeft (96);
    matchLabel.setBounds (matchArea.removeFromTop (16));
    matchBox.setBounds (matchArea.withHeight (30));

    row.removeFromLeft (10);
    auto measureArea = row.removeFromLeft (118);
    measureLabel.setBounds (measureArea.removeFromTop (16));
    measureBox.setBounds (measureArea.withHeight (30));

    row.removeFromLeft (10);
    holdButton.setBounds (row.withTrimmedTop (16).withHeight (30));

    // Perillas
    area.removeFromTop (12);
    auto knobs = area.removeFromTop (172);
    const int kw = knobs.getWidth() / 4;

    auto placeKnob = [&] (PreciseSlider& s, juce::Label& l)
    {
        auto k = knobs.removeFromLeft (kw);
        l.setBounds (k.removeFromTop (22));
        k.removeFromBottom (18);
        s.setBounds (k.reduced (8, 0));
    };

    placeKnob (manualSlider, manualLabel);

    // Columna de Ventana: botones ms|Sync arriba, perilla, equivalencia abajo.
    {
        auto k = knobs.removeFromLeft (kw);
        auto header = k.removeFromTop (22).withSizeKeepingCentre (138, 22);
        windowLabel.setBounds (header.removeFromLeft (54));
        syncButtons[0].setBounds (header.removeFromLeft (38));
        syncButtons[1].setBounds (header.removeFromLeft (46));
        windowInfoLabel.setBounds (k.removeFromBottom (18).expanded (12, 0));
        windowSlider.setBounds (k.reduced (8, 0));
        windowSyncSlider.setBounds (k.reduced (8, 0));
    }

    placeKnob (limitSlider, limitLabel);
    // Columna de Latencia: botón Medir arriba, estado de la medición abajo.
    {
        auto k = knobs.removeFromLeft (kw);
        auto header = k.removeFromTop (22).withSizeKeepingCentre (128, 22);
        latencyLabel.setBounds (header.removeFromLeft (62));
        measureButton.setBounds (header.removeFromLeft (60));
        latencyInfoLabel.setBounds (k.removeFromBottom (18).expanded (14, 0));
        latencySlider.setBounds (k.reduced (8, 0));
    }
}
