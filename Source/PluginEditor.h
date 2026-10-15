#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Scope.h"
#include "Theme.h"

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

// Estilo de los controles. Cada botón dice qué tipo es con la propiedad "kind":
//   raised  = en relieve sobre el gris (Medir, Ø, el botón grande)
//   segment = mitad de ms | Sync (la elegida queda en relieve)
//   chip    = pastilla oscura del osciloscopio (color en "chipColour")
//   plain   = solo texto (▸ Osciloscopio)
class MeteringABLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MeteringABLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;
};

// Botón para agrandar el plugin: osciloscopio grande a la derecha del panel gris.
class WideButton : public juce::Button
{
public:
    WideButton() : juce::Button ("Osciloscopio grande") {}
    bool on = false;                                  // resaltado mientras está grande
    void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
};

class MeteringABEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    // Medidas de la ventana: parte principal + sección del osciloscopio.
    static constexpr int columnsHeight = 152;         // Escuchar | Ventana | Latencia
    static constexpr int mainHeight = 162 + columnsHeight + 36;   // hasta la línea que separa el osciloscopio
    static constexpr int sendHeight = 86;             // Send: solo título, modo, Link ID y estado

    explicit MeteringABEditor (MeteringABProcessor&);
    ~MeteringABEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Para las pruebas: ejecuta una actualización de la interfaz (lo que hace el timer).
    void refreshNow() { timerCallback(); }
    const ScopeView& getScopeForTests() const { return scope; }
    void setScopeWideForTests (bool on)           { setScopeWide (on); }
    bool isScopeWide() const                      { return scopeWide; }

    // Modo ancho: el panel gris igual que siempre y, a su derecha, el osciloscopio
    // estirado hasta el otro borde de la pantalla de una MacBook Pro de 14" (1512 puntos).
    static constexpr int wideWidth = 1500, wideHeight = mainHeight;

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

    juce::ComboBox modeBox;
    PreciseSlider linkSlider, windowSlider, windowSyncSlider;
    juce::Label linkLabel, windowLabel, latencyLabel;
    juce::TextButton abButton, measureButton;
    juce::TextButton polarityButton { juce::String::fromUTF8 ("\xc3\x98") };   // "Ø": polaridad invertida
    juce::Label listenLabel, latencyInfoLabel, matchInfoLabel;
    juce::Label latencyMsLabel;                       // "= 1,33 ms" debajo del valor medido
    juce::TextButton syncButtons[2];                  // [0] = ms, [1] = Sync
    juce::Label windowInfoLabel;                      // equivalencia ms <-> compases
    juce::Label statusLabel;
    juce::TextButton linkMinus { juce::String::fromUTF8 ("\xe2\x88\x92") }, linkPlus { "+" };   // − / +
    juce::Label linkValueLabel;
    juce::String latencyNumberText;                   // número de latencia (se dibuja en paint)
    juce::Rectangle<int> latencyNumberArea;

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

    // Osciloscopio grande, a la derecha del panel gris.
    WideButton wideButton;
    bool scopeWide = false;
    void setScopeWide (bool wide);
    bool isWideLayout() const                         { return scopeWide && ! isSendMode; }
    int contentOffsetY() const;                       // (0: el panel gris queda arriba)
    juce::Component* scopeParts[6] = { &scope, &zoomBox, &scopeStyleButton, &linkButton, &showPreButton, &showPostButton };

    static constexpr int editorWidth = 640;
    static constexpr int scopeHeaderHeight = 44;      // barra "Osciloscopio" (siempre visible)
    static constexpr int scopeBodyHeight = 316;       // el osciloscopio en sí, más margen
    void setWaves (bool pre, bool post);
    void updateWaveButtons();

    std::unique_ptr<ComboAttachment>  modeAtt;
    std::unique_ptr<SliderAttachment> linkAtt, windowAtt, windowSyncAtt;
    std::unique_ptr<ButtonAttachment> polarityAtt;

    float shownPre = -120.0f, shownPost = -120.0f, shownGain = 0.0f;
    bool isSendMode = false;
    int lastListenPre = -1;                           // para seguir al botón de escucha

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteringABEditor)
};
