#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

// ---------------------------------------------------------------------------
//  SharedBus: el "cable invisible" entre la instancia Send y la Receive.
//
//  Todas las instancias de Metering A/B que carga Logic viven en el mismo proceso,
//  así que pueden compartir este objeto. Hay 16 "canales de enlace" (Link ID).
//  El Send copia acá el audio original (antes de tus efectos) y el Receive lo
//  lee para medirlo, compararlo y poder escucharlo en el A/B.
// ---------------------------------------------------------------------------
struct LinkSlot
{
    static constexpr int ringSize = 1 << 16;      // 65536 muestras (~1.4 s a 48 kHz)
    static constexpr int ringMask = ringSize - 1;

    std::array<std::vector<float>, 2> ring;       // audio original, máx. estéreo
    std::atomic<juce::int64> writePos { 0 };      // cuántas muestras escribió el Send
    std::atomic<int> numChannels { 0 };           // canales del Send
    std::atomic<juce::uint32> heartbeat { 0 };    // sube en cada bloque del Send
    std::atomic<int> senderCount { 0 };           // cuántos Send usan este ID

    LinkSlot()
    {
        for (auto& ch : ring)
            ch.assign ((size_t) ringSize, 0.0f);
    }
};

class SharedBus
{
public:
    static constexpr int numLinks = 16;

    static SharedBus& get()
    {
        static SharedBus instance;
        return instance;
    }

    LinkSlot& slot (int linkIndex)
    {
        return *slots[(size_t) juce::jlimit (0, numLinks - 1, linkIndex)];
    }

private:
    SharedBus()
    {
        for (auto& s : slots)
            s = std::make_unique<LinkSlot>();
    }

    std::array<std::unique_ptr<LinkSlot>, numLinks> slots;
};
