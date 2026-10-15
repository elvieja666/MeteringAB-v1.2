#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Scope.h"

// ---------------------------------------------------------------------------
//  Perilla con valor exacto:
//   - Hacé clic en el número de abajo y escribí el valor (ej: -3.25).
//   - Shift + arrastrar = ajuste fino.
//   - Doble clic en la perilla = vuelve al valor por defecto.
// ---------------------------------------------------------------------------
class PreciseSlider : public juce::Slider
{
public:
    PreciseSlider();
    void mouseDown (const juce::MouseEvent& e) override;
};

class MeteringABLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MeteringABLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

class MeteringABEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MeteringABEditor (MeteringABProcessor&);
    ~MeteringABEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Para las pruebas: ejecuta una actualización de la interfaz (lo que hace el timer).
    void refreshNow() { timerCallback(); }
    const ScopeView& getScopeForTests() const { return scope; }

private:
    void timerCallback() override;
    void setBoolParam (const char* paramId, bool on);
    void toggleAB();
    void setSync (bool on);
    void setupKnob (PreciseSlider& s, juce::Label& label, const juce::String& text);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    MeteringABProcessor& processor;
    MeteringABLookAndFeel lnf;

    juce::ComboBox modeBox, measureBox;
    PreciseSlider linkSlider, windowSlider, windowSyncSlider;
    juce::Label linkLabel, windowLabel, latencyLabel, measureLabel;
    juce::TextButton abButton, deltaButton, measureButton;
    juce::TextButton polarityButton { juce::String::fromUTF8 ("\xc3\x98") };   // "Ø": polaridad invertida
    juce::Label listenLabel, latencyInfoLabel;
    juce::Label latencyValueLabel, latencyMsLabel;    // valor medido, en grande (sin perilla)
    juce::TextButton syncButtons[2];                  // [0] = ms, [1] = Sync
    juce::Label windowInfoLabel;                      // equivalencia ms <-> compases
    juce::ToggleButton holdButton { "Congelar" };
    juce::Label statusLabel;

    juce::TooltipWindow tooltipWindow { this, 700 };
    ScopeView scope { processor };
    juce::Label scopeLabel;
    juce::ComboBox zoomBox;
    juce::TextButton showPreButton { "PRE FX" }, showPostButton { "POST FX" };
    juce::TextButton linkButton { "LINK" };           // prendido: el osciloscopio sigue a la escucha
    bool scopeLinked = true;
    void setScopeLinked (bool linked);
    void syncWavesToListen();
    juce::TextButton scopeToggleButton;               // ▾ / ▸ mostrar u ocultar el osciloscopio
    juce::TextButton scopeStyleButton { "Estilo" };   // menú de opciones gráficas
    void showStyleMenu();
    void applyScopeLook();                            // lee estilo y colores guardados
    void pickCustomColour (bool forPre);
    bool scopeOpen = false;
    void setScopeOpen (bool open);
    void updateScopeSection();

    // Medidas de la ventana: parte principal + sección del osciloscopio.
    static constexpr int editorWidth = 640;
    static constexpr int mainHeight = 378;            // hasta la línea que separa el osciloscopio
    static constexpr int scopeHeaderHeight = 44;      // barra "Osciloscopio" (siempre visible)
    static constexpr int scopeBodyHeight = 316;       // el osciloscopio en sí, más margen
    void setWaves (bool pre, bool post);
    void updateWaveButtons();

    std::unique_ptr<ComboAttachment>  modeAtt, measureAtt;
    std::unique_ptr<SliderAttachment> linkAtt, windowAtt, windowSyncAtt;
    std::unique_ptr<ButtonAttachment> holdAtt, polarityAtt;

    float shownPre = -120.0f, shownPost = -120.0f, shownGain = 0.0f;
    bool isSendMode = false;
    int lastListenPre = -1;                           // para seguir al botón de escucha

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteringABEditor)
};
