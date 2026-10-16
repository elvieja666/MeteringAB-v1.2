#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LoudnessMeter.h"
#include "BassTracker.h"
#include "SharedBus.h"
#include "LatencyMeasurer.h"

// IDs de los parámetros (así los encuentra Logic para automatizar y guardar).
namespace ParamIDs
{
    inline constexpr auto mode     = "mode";      // Send / Receive
    inline constexpr auto link     = "link";      // Link ID 1..16
    inline constexpr auto pre      = "pre";       // botón PRE FX (on) / POST FX (off)
    inline constexpr auto windowMs   = "windowMs";   // ventana en milisegundos
    inline constexpr auto sync       = "sync";       // ventana: ms (off) / Sync (on)
    inline constexpr auto windowSync = "windowSync"; // ventana en compases
    inline constexpr auto latency  = "latency";   // compensación (muestras, con decimales)
    inline constexpr auto polarity = "polarity";  // compensar polaridad invertida
    inline constexpr auto hold     = "hold";      // congelar el auto gain
}

// Valores musicales del modo Sync, en compases.
namespace SyncDivisions
{
    inline constexpr std::array<double, 6> bars { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
    inline const juce::StringArray names { "1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars" };
    inline constexpr int defaultIndex = 2;   // 1 bar
}

// Largos del osciloscopio, en compases.
namespace ScopeLengths
{
    inline constexpr std::array<double, 5> bars { 0.25, 0.5, 1.0, 2.0, 4.0 };
    inline const juce::StringArray names { "1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars" };
    inline constexpr int defaultIndex = 2;   // 1 bar
}

class MeteringABProcessor : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::AsyncUpdater
{
public:
    enum class Mode   { send = 0, receive = 1 };

    MeteringABProcessor();
    ~MeteringABProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                             { return 1; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override          { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Valores para la pantalla (los escribe el audio, los lee la interfaz).
    std::atomic<float> displayPre    { -120.0f };
    std::atomic<float> displayPost   { -120.0f };
    std::atomic<float> displayGain   { 0.0f };
    std::atomic<bool>  displayGainValid { false };   // false: medidores en cero (pausa o clic), sin ganancia que mostrar

    // Pone los medidores en cero (clic sobre los números). Lo aplica el audio en el próximo bloque.
    void requestMeterReset()                         { meterResetRequested = true; }
    std::atomic<juce::uint32> lastBlockMs { 0 };      // cuándo llegó el último bloque de audio
    std::atomic<bool>  sendConnected { false };
    std::atomic<bool>  duplicateSend { false };

    // Tempo y compás del proyecto (los informa Logic en cada bloque).
    std::atomic<double> hostBpm      { 120.0 };
    std::atomic<int>    timeSigNum   { 4 };
    std::atomic<int>    timeSigDen   { 4 };

    // Duración de un compás en segundos, según el tempo y compás actuales.
    double getBarSeconds() const;
    // Duración de la ventana que se está usando ahora (ms o Sync), en segundos.
    double getWindowSeconds() const;
    // Índice del valor musical más cercano a una duración (en escala musical).
    static int nearestDivision (double seconds, double barSeconds);
    // Para los tests: aplica ya una conversión ms <-> Sync pendiente.
    void processPendingConversion() { handleUpdateNowIfNeeded(); }

    // Osciloscopio sincronizado al tempo: el ancho es un largo musical (ej. 1
    // compás) dividido en "columnas". Cada columna guarda el mínimo y máximo
    // de PRE FX y POST FX en ese momento del compás. Se va sobrescribiendo a
    // medida que avanza la canción, así cada compás reemplaza al anterior.
    static constexpr int scopeBins = 2048;            // resolución horizontal (columnas por loop)
    // Una "traza" por señal y canal: mínimo, máximo y suma de cuadrados (RMS).
    // hold: cuánto sostener el borde en Contorno / Solo líneas (ms), según el grave de esa columna.
    struct ScopeTrace { std::array<std::atomic<float>, scopeBins> min, max, sq, hold; };

    // Borde "adaptativo fino": sin un grave fuerte sostiene 3 ms (mucha definición);
    // si domina un grave, sostiene un ciclo de ese grave (hasta 50 ms) para tapar sus ondas.
    static constexpr float scopeMinHoldMs = 3.0f, scopeMaxHoldMs = 50.0f, scopeHoldCycles = 1.05f;
    static float scopeHoldFor (float fullPeak, float lowPeak, double periodMs)
    {
        const float dominance = fullPeak > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (lowPeak / fullPeak - 0.12f) / 0.25f) : 0.0f;
        const float cycle = juce::jlimit (scopeMinHoldMs, scopeMaxHoldMs, (float) (scopeHoldCycles * periodMs));
        return scopeMinHoldMs + (cycle - scopeMinHoldMs) * dominance;
    }
    enum ScopeTraceIndex { preSum = 0, postSum, preLeft, postLeft, preRight, postRight, numScopeTraces };
    std::array<ScopeTrace, numScopeTraces> scopeTraces;                  // L+R (promedio), solo L, solo R
    std::array<std::atomic<int>, scopeBins> scopeCount;                  // muestras en cada columna

    // Con LINK: qué onda mostrar en cada columna (1 = PRE, 0 = POST). Cada
    // columna guarda lo que sonaba cuando el cabezal pasó por ella: al alternar
    // la escucha, el cambio aparece a medida que avanza el cabezal y lo de
    // adelante sigue como estaba hasta que el cabezal llegue.
    std::array<std::atomic<std::uint8_t>, scopeBins> scopeShowPreAt;

    // Atajos a la suma L+R (los usan las pruebas).
    std::array<std::atomic<float>, scopeBins>& scopePreMin  = scopeTraces[preSum].min;
    std::array<std::atomic<float>, scopeBins>& scopePreMax  = scopeTraces[preSum].max;
    std::array<std::atomic<float>, scopeBins>& scopePostMin = scopeTraces[postSum].min;
    std::array<std::atomic<float>, scopeBins>& scopePostMax = scopeTraces[postSum].max;
    std::atomic<int> scopeCurrentBin { 0 };
    std::atomic<bool> scopeIsRunning { false };       // Logic en play
    std::atomic<int> scopeLengthIndex { ScopeLengths::defaultIndex };
    double getScopeLengthBars() const { return ScopeLengths::bars[(size_t) juce::jlimit (0, (int) ScopeLengths::bars.size() - 1, scopeLengthIndex.load())]; }

    // Medición automática de latencia (botón "Medir").
    LatencyMeasurer latencyMeasurer;
    void startLatencyMeasurement();
    double getCurrentSampleRate() const { return currentSampleRate; }

    static constexpr float minWindowMs = 100.0f, maxWindowMs = 60000.0f;
    static constexpr float autoLimitDb = 24.0f;      // corrección máxima del matcheo automático
    static constexpr int   maxLatencySamples = 32000;
    static constexpr int   maxInterpolationHalfLength = 32;   // 64 muestras de interpolación

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void readHostTempo();
    void configureMeter (LoudnessMeter& meter) const;
    void updateSenderRegistration (bool isSend, int linkIndex);
    void processSend (juce::AudioBuffer<float>& buffer, LinkSlot& slot);
    void processReceive (juce::AudioBuffer<float>& buffer, LinkSlot& slot);
    double scopeBlockStartQuarters (int n);          // posición en el loop del osciloscopio

    // Punteros rápidos a los parámetros (leer esto en el audio es seguro).
    std::atomic<float>* pMode    = nullptr;
    std::atomic<float>* pLink    = nullptr;
    std::atomic<float>* pPre     = nullptr;
    std::atomic<float>* pWindowMs   = nullptr;
    std::atomic<float>* pSync       = nullptr;
    std::atomic<float>* pWindowSync = nullptr;
    std::atomic<float>* pLatency = nullptr;
    std::atomic<float>* pPolarity = nullptr;

    LoudnessMeter preMeter, postMeter;
    juce::AudioBuffer<float> dryBuffer;               // original alineado (muestras enteras, para PRE FX)
    std::vector<float> dryMono, postMono;             // para medir la latencia
    // Posición de la canción (la informa Logic en cada bloque).
    std::atomic<bool> meterResetRequested { false };
    bool   hostKnowsTransport = false;                // el host informa play / pausa
    bool   hostPlaying = false, hostHasPpq = false, hostHasBarStart = false, hostHasBarCount = false;
    double hostPpq = 0.0, hostBarStart = 0.0;
    juce::int64 hostBarCount = 0;
    double scopePhaseQuarters = 0.0;                  // dónde va el barrido (en negras)
    int    lastScopeBin = -1, lastScopeLength = -1;
    std::array<BassTracker, numScopeTraces> bassTrackers;     // graves de cada onda (para el borde)
    std::array<float, numScopeTraces> binLowPeak {}, binPeriodMax {};
    bool   scopeWasPlaying = false;                   // para detectar cuándo se da play
    bool   scopeGateOpen = true;                      // false solo durante el tramo previo al play
    double scopeLastBlockPpq = 0.0;                   // para detectar saltos hacia atrás (ciclo)
    double scopePlayStartQuarters = 0.0;              // dónde se dio play (en negras)
    void   clearScope();
public:
    void   clearScopeForTests() { clearScope(); }
private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmoother;

    // Crossfade de 10 ms al cambiar PRE FX / POST FX (evita clicks).
    // Los dos pesos se mueven juntos en línea recta y siempre suman 1.
    static constexpr double crossfadeSeconds = 0.010;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixPost, mixPre;
    void setMixTargets (bool pre, bool immediately);

    float autoGainDb = 0.0f;
    int   registeredSendLink = -1;                    // -1 = no registrado
    juce::uint32 lastHeartbeat = 0;
    int   blocksWithoutHeartbeat = 0;
    int   lastLink = -1;
    double currentSampleRate = 48000.0;
    bool  syncStateHandled = false;                   // último modo ya convertido

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteringABProcessor)
};
