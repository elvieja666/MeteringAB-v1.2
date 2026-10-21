#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
//  LoudnessMeter: mide qué tan fuerte suena una señal.
//
//  Dos modos de filtro:
//   - RMS: energía promedio "plana".
//   - LUFS: aplica el filtro K de la norma ITU-R BS.1770 (el que usan los
//     medidores LUFS), que se parece más a cómo percibe el oído.
//
//  Dos formas de promediar en el tiempo:
//   - Exponencial (modo ms): lo reciente pesa más y lo viejo se desvanece.
//   - Rectangular (modo Sync): promedia exactamente los últimos X segundos,
//     todos con el mismo peso. Con X = compases exactos, la lectura de un
//     loop queda quieta porque siempre entran ciclos musicales completos.
//  Las dos se calculan siempre, así cambiar de una a otra no produce saltos.
// ---------------------------------------------------------------------------
class LoudnessMeter
{
public:
    enum class Mode { rms = 0, lufs = 1 };
    enum class Averaging { exponential = 0, rectangular = 1 };

    static constexpr int binSize = 128;              // muestras por "casillero"
    static constexpr double maxWindowSeconds = 120.0;

    // Reserva memoria: llamar fuera del audio (prepareToPlay).
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        designKWeighting();
        setWindowSeconds (windowSeconds);
        bins.assign ((size_t) std::ceil (maxWindowSeconds * sampleRate / binSize) + 2, 0.0);
        setRectangularSeconds (rectSeconds);
        reset();
    }

    void reset()
    {
        for (auto& c : channels)
        {
            c.stage1.z1 = c.stage1.z2 = 0.0;
            c.stage2.z1 = c.stage2.z2 = 0.0;
        }

        meanSquare = 0.0;
        // Solo lo usado (en pausa se llama en cada bloque: llenar los ~45000 era un gasto).
        std::fill (bins.begin(), bins.begin() + juce::jmin ((int) bins.size(), binsFilled), 0.0);
        binAccumulator = 0.0;
        binFill = 0;
        binWrite = 0;
        binsFilled = 0;
        runningSum = 0.0;
        binsSinceRecompute = 0;
    }

    void setMode (Mode m)              { mode = m; }
    void setAveraging (Averaging a)    { averaging = a; }

    // Ventana del promedio exponencial.
    void setWindowSeconds (double seconds)
    {
        windowSeconds = juce::jmax (0.01, seconds);
        alpha = std::exp (-1.0 / (windowSeconds * sampleRate));
    }

    // Ventana del promedio rectangular (ej: duración exacta de 1 compás).
    void setRectangularSeconds (double seconds)
    {
        rectSeconds = juce::jlimit (0.01, maxWindowSeconds, seconds);

        if (bins.empty())
            return;

        const int newCount = juce::jlimit (1, (int) bins.size() - 1,
                                           (int) std::lround (rectSeconds * sampleRate / binSize));

        if (newCount != windowBins)
        {
            windowBins = newCount;
            recomputeRunningSum();
        }
    }

    // Procesa un bloque. numChannels puede ser 1 o 2.
    void process (const float* const* data, int numChannels, int numSamples)
    {
        numChannels = juce::jlimit (1, 2, numChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            double energy = 0.0;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                double x = data[ch][i];

                if (mode == Mode::lufs)
                    x = channels[(size_t) ch].stage2.process (channels[(size_t) ch].stage1.process (x));

                energy += x * x;
            }

            // RMS: promedio entre canales. LUFS: suma (como pide BS.1770).
            if (mode == Mode::rms)
                energy /= numChannels;

            meanSquare = alpha * meanSquare + (1.0 - alpha) * energy;

            binAccumulator += energy;

            if (++binFill == binSize)
                pushBin();
        }

        // Evita números "denormales" que hacen lento al CPU en silencio.
        if (meanSquare < 1.0e-20)
            meanSquare = 0.0;
    }

    double getMeanSquare() const
    {
        if (averaging == Averaging::exponential)
            return meanSquare;

        const int used = juce::jmin (binsFilled, windowBins);
        return used > 0 ? juce::jmax (0.0, runningSum) / ((double) used * binSize) : 0.0;
    }

    // Valor en dB para mostrar (RMS en dBFS, LUFS con su offset estándar).
    double getDecibels() const
    {
        const auto ms = getMeanSquare();

        if (ms <= 1.0e-12)
            return -120.0;

        auto db = 10.0 * std::log10 (ms);
        return mode == Mode::lufs ? db - 0.691 : db;
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        double process (double x)
        {
            auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct ChannelState
    {
        Biquad stage1, stage2;
    };

    void designKWeighting()
    {
        // Coeficientes del filtro K calculados para cualquier sample rate
        // (mismas fórmulas que la librería libebur128).
        Biquad shelf, highPass;

        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }

        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            highPass.b0 = 1.0;
            highPass.b1 = -2.0;
            highPass.b2 = 1.0;
            highPass.a1 = 2.0 * (K * K - 1.0) / a0;
            highPass.a2 = (1.0 - K / Q + K * K) / a0;
        }

        for (auto& c : channels)
        {
            c.stage1 = shelf;
            c.stage2 = highPass;
        }
    }

    // Guarda un casillero completo y actualiza la suma de la ventana.
    void pushBin()
    {
        const int size = (int) bins.size();
        bins[(size_t) binWrite] = binAccumulator;
        runningSum += binAccumulator;

        // El casillero que sale de la ventana.
        if (binsFilled >= windowBins)
            runningSum -= bins[(size_t) ((binWrite - windowBins + size) % size)];

        binWrite = (binWrite + 1) % size;
        binsFilled = juce::jmin (binsFilled + 1, size);
        binAccumulator = 0.0;
        binFill = 0;

        // De vez en cuando se recalcula de cero para que no se acumule error.
        if (++binsSinceRecompute >= juce::jmax (windowBins, 64))
            recomputeRunningSum();
    }

    void recomputeRunningSum()
    {
        const int size = (int) bins.size();
        const int used = juce::jmin (binsFilled, windowBins);
        double sum = 0.0;

        for (int k = 1; k <= used; ++k)
            sum += bins[(size_t) ((binWrite - k + size) % size)];

        runningSum = sum;
        binsSinceRecompute = 0;
    }

    double sampleRate = 48000.0;
    double windowSeconds = 1.0;
    double rectSeconds = 2.0;
    Averaging averaging = Averaging::exponential;

    std::vector<double> bins;
    double binAccumulator = 0.0;
    int binFill = 0, binWrite = 0, binsFilled = 0, windowBins = 1, binsSinceRecompute = 0;
    double runningSum = 0.0;
    double alpha = 0.0;
    double meanSquare = 0.0;
    Mode mode = Mode::rms;
    std::array<ChannelState, 2> channels;
};
