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
//   raised  = en relieve sobre el gris (Medir, el botón grande)
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

// Todo el contenido del plugin, a tamaño "normal". Receive: columna gris a la
// izquierda (medidores, Ventana, Latencia, Escuchar) y el osciloscopio a la
// derecha, siempre visible. Send: una franja chica. La ventana del plugin
// (MeteringABEditor, más abajo) lo muestra agrandado o achicado.
class MeteringABPanel : public juce::Component, private juce::Timer
{
public:
    // Medidas de la ventana: parte principal + sección del osciloscopio.
    // Receive: columna + osciloscopio. El osciloscopio mantiene la proporción
    // 2,178 : 1 (ancho : alto) de la referencia; su alto = 780 - 52 - 16 = 712.
    static constexpr int receiveHeight = 663;          // columna gris compacta (más grande en pantalla)
    static constexpr int scopeHeight = receiveHeight - 52 - 16;   // = 595
    static constexpr int scopeWidth = 1101;            // 595 * 1,85
    static constexpr int sideWidth = 380;             // la columna gris (Receive)
    static constexpr int receiveWidth = sideWidth + 18 + scopeWidth + 18;   // = 1967 (al 100%)
    static constexpr int sendWidth = sideWidth, sendHeight = 134;    // Send: corta justo donde en el Receive empieza el recuadro de los números
    static constexpr int mainHeight = receiveHeight;  // (compatibilidad)

    explicit MeteringABPanel (MeteringABProcessor&);
    ~MeteringABPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // clic en los números: medidores a cero
    void mouseMove (const juce::MouseEvent&) override;

    // Para las pruebas: ejecuta una actualización de la interfaz (lo que hace el timer).
    void refreshNow() { timerCallback(); }
    const ScopeView& getScopeForTests() const { return scope; }
    ScopeView& getScopeForTests()             { return scope; }
    juce::TextButton& getFreezeButtonForTests() { return freezeButton; }
    juce::TextButton& getOptionsButtonForTests() { return optionsButton; }
    juce::ValueTree& getProcessorStateForTests() { return processor.apvts.state; }
    juce::TextButton& getGainLineButtonForTests() { return gainLineButton; }
    bool isSendLayout() const                   { return isSendMode; }
    bool isScopeOpenForTests() const            { return scopeOpen; }
    juce::TextButton& getABButtonForTests()      { return abButton; }
    juce::TextButton& getMatchBypassButtonForTests() { return matchBypassButton; }
    juce::Rectangle<int> getMeterAreaForTests() const { return meterArea(); }
    static inline bool scopeOpenOnStart = false;   // pruebas: abrir con el osciloscopio a la vista
    juce::TextButton& getScopeToggleForTests()  { return scopeToggleButton; }

    int getStyleMenuOpenedForTests() const      { return styleMenuOpenedForTests; }
    float getShownPreForTests() const             { return shownPre; }
    float getShownPostForTests() const            { return shownPost; }
    bool isGainShownForTests() const              { return gainShown; }
    void clickMetersForTests()                    { resetMeters(); }

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
    juce::TextButton matchBypassButton { "Match Gain Bypass" };   // POST sin la ganancia de matcheo
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
    bool matchBypassShown = false;                    // Match Gain Bypass prendido (ganancia en gris)
    juce::TextButton optionsButton { "Options" };     // menú: canales (L / R / suma) y grilla
    juce::TextButton freezeButton { "Freeze" };       // congelar el osciloscopio (para hacer zoom quieto)
    juce::TextButton gainLineButton { "Gain" };       // línea con el valor de la ganancia aplicada
    bool gainLineOn = false;
    juce::TextButton scopeToggleButton { "Scope" };   // abrir / cerrar el osciloscopio (al pie de la columna gris)
    bool scopeOpen = true;
    void setScopeOpen (bool open);
    void syncWavesToListen();                         // el osciloscopio muestra la onda que suena
    void showStyleMenu (juce::Component& anchor);     // menú de opciones gráficas (botón Options)
public:
    juce::PopupMenu buildStyleMenu() const;          // canales y grilla
    void applyStyleMenuResult (int result);
private:
    juce::Component* styleMenuAnchor = nullptr;
    int styleMenuOpenedForTests = 0;
    void applyScopeLook();                            // lee estilo y colores guardados
    void pickCustomColour (bool forPre);
    void updateScopeSection();

    juce::Component* scopeParts[5] = { &scope, &zoomBox, &freezeButton, &gainLineButton, &optionsButton };

    static constexpr int scopeHeaderHeight = 52;      // barra "Osciloscopio" arriba del gráfico
    void setWaves (bool pre, bool post);

    std::unique_ptr<ComboAttachment>  modeAtt;
    std::unique_ptr<SliderAttachment> linkAtt, windowAtt, windowSyncAtt;

    float shownPre = -120.0f, shownPost = -120.0f, shownGain = 0.0f;
    bool gainShown = false;                           // false: ganancia "—" (todavía no se midió nada)
    static constexpr int   timerHz = 30;
    static constexpr float fallDbPerSecond = 65.0f;   // en pausa, Original y Procesado caen a este ritmo...
    static constexpr float fallFloorDb = -70.0f;      // ...y por debajo de esto pasan a -∞
    float fallStepPre = 0.0f, fallStepPost = 0.0f;    // al parar: cuánto baja cada uno por cuadro (llegan juntos)
    bool metersFalling = false;
    bool audioWasStopped = false;
    juce::Rectangle<int> meterArea() const;
    juce::Rectangle<int> metersBounds { 20, 136, 340, 124 };
    void resetMeters();
    bool isSendMode = false;
    int lastListenPre = -1;                           // para seguir al botón de escucha
    juce::String lastScopeSignature;                  // estado del osciloscopio (para no redibujarlo si no cambió)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteringABPanel)
};

// ---------------------------------------------------------------------------
//  Ventana del plugin, escalable: arrastrando la esquina de abajo a la derecha
//  todo se agranda o achica en proporción (de 75% a 200%). El tamaño elegido se
//  guarda con el proyecto.
// ---------------------------------------------------------------------------
class MeteringABEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int mainHeight = MeteringABPanel::receiveHeight;
    static constexpr int sendHeight = MeteringABPanel::sendHeight;
    static constexpr int baseWidth  = MeteringABPanel::receiveWidth;
    static constexpr float minScale = 0.4f, maxScale = 1.5f;
    // Por defecto el osciloscopio mide 781 x 359: la referencia (1041 x 478) un 25% más
    // chica. Todo lo demás (la columna gris) se escala junto, en proporción (≈ 950 x 393).
    static constexpr float defaultScale = 1.05f * 497.0f / (float) MeteringABPanel::receiveHeight;   // 497 px de alto, + 5%

    explicit MeteringABEditor (MeteringABProcessor&);
    ~MeteringABEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void childBoundsChanged (juce::Component*) override;   // el panel cambió de alto (osciloscopio, Send)

    MeteringABPanel& getPanel()                       { return panel; }
    float getUiScale() const                          { return scale; }
    // Clic derecho en la esquina de escalar: menú con "Reset size".
    void showSizeMenu();
    void mouseDown (const juce::MouseEvent&) override;
    int sizeMenuOpenedForTests = 0;
    void setUiScale (float newScale);

private:
    void fitToPanel();                                // tamaño de la ventana = panel x escala

    MeteringABProcessor& processor;
    MeteringABPanel panel;
    juce::ComponentBoundsConstrainer constrainer;
    float scale = 1.0f;                               // escala del Receive (la que elegís con la esquina)
    float shownScale = 1.0f;                          // la que se usa ahora (Send: la misma que el Receive)
    bool fitting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteringABEditor)
};
