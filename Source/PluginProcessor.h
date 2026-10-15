#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LoudnessMeter.h"
#include "SharedBus.h"
#include "LatencyMeasurer.h"

// IDs de los parámetros (así los encuentra Logic para automatizar y guardar).
namespace ParamIDs
{
    inline constexpr auto mode     = "mode";      // Send / Receive
    inline constexpr auto link     = "link";      // Link ID 1..16
    inline constexpr auto pre      = "pre";       // botón PRE FX (on) / POST FX (off)
    inline constexpr auto delta    = "delta";     // escuchar solo la diferencia
    inline constexpr auto measure  = "measure";   // RMS / LUFS
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

    // Osciloscopio: últimos segundos de PRE FX (original alineado) y POST FX
    // (procesado y nivelado), en mono. Los escribe el audio, los lee la interfaz.
    std::vector<float> scopePre, scopePost;
    std::atomic<juce::int64> scopeWritePos { 0 };
    int scopeMask = 0;

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
    void readDryFractional (const LinkSlot& slot, juce::int64 end, int n, int numCh, double latency);
    void writeScope (const float* preBlock, const float* postBlock, int n);

    // Punteros rápidos a los parámetros (leer esto en el audio es seguro).
    std::atomic<float>* pMode    = nullptr;
    std::atomic<float>* pLink    = nullptr;
    std::atomic<float>* pPre     = nullptr;
    std::atomic<float>* pDelta   = nullptr;
    std::atomic<float>* pMeasure = nullptr;
    std::atomic<float>* pWindowMs   = nullptr;
    std::atomic<float>* pSync       = nullptr;
    std::atomic<float>* pWindowSync = nullptr;
    std::atomic<float>* pLatency = nullptr;
    std::atomic<float>* pPolarity = nullptr;
    std::atomic<float>* pHold    = nullptr;

    LoudnessMeter preMeter, postMeter;
    juce::AudioBuffer<float> dryBuffer;               // original alineado (muestras enteras, para PRE FX)
    juce::AudioBuffer<float> deltaDryBuffer;          // original alineado con decimales (para DELTA)
    std::vector<float> dryMono, postMono;             // para medir la latencia
    std::vector<float> scopePreBlock, scopePostBlock; // un bloque para el osciloscopio
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmoother;

    float autoGainDb = 0.0f;
    int   registeredSendLink = -1;                    // -1 = no registrado
    juce::uint32 lastHeartbeat = 0;
    int   blocksWithoutHeartbeat = 0;
    int   lastLink = -1;
    double currentSampleRate = 48000.0;
    bool  syncStateHandled = false;                   // último modo ya convertido

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteringABProcessor)
};
