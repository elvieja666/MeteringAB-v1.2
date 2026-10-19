#pragma once
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

// ---------------------------------------------------------------------------
//  LatencyMeasurer: averigua cuánto retrasan tus efectos al audio.
//
//  1) Mientras suena el tema, graba unos segundos del original (lo que manda
//     el Send) y de lo procesado (lo que llega al Receive).
//  2) Busca cuántas muestras hay que correr el original para que "encaje"
//     con lo procesado (correlación cruzada con FFT).
//  3) Revisa que la coincidencia sea clara antes de dar un resultado.
//
//  La grabación ocurre en el audio; el cálculo, en el hilo de la interfaz.
// ---------------------------------------------------------------------------
class LatencyMeasurer
{
public:
    enum class State { idle = 0, measuring, analysing, done, noSignal, unclear, noSend };

    static constexpr int captureOrder = 17;
    static constexpr int captureSize  = 1 << captureOrder;   // ~2,7 s a 48 kHz

    void prepare()
    {
        dry.assign ((size_t) captureSize, 0.0f);
        post.assign ((size_t) captureSize, 0.0f);
    }

    // Desde la interfaz: empezar una medición.
    void start()
    {
        writePos = 0;
        progress = 0.0f;
        state = (int) State::measuring;
    }

    State getState() const    { return (State) state.load(); }
    void setState (State s)   { state = (int) s; }
    bool isMeasuring() const  { return getState() == State::measuring; }
    float getProgress() const { return progress.load(); }
    double getResult() const  { return result.load(); }   // muestras, con decimales
    bool isInverted() const   { return inverted.load(); }

    // Desde el audio: agrega un bloque (original y procesado, en mono).
    // Devuelve true cuando la grabación se completó y hay que analizar.
    bool push (const float* dryMono, const float* postMono, int numSamples)
    {
        if (! isMeasuring() || dry.empty())
            return false;

        int pos = writePos;
        const int count = juce::jmin (numSamples, captureSize - pos);

        std::copy (dryMono, dryMono + count, dry.begin() + pos);
        std::copy (postMono, postMono + count, post.begin() + pos);
        pos += count;
        writePos = pos;
        progress = (float) pos / (float) captureSize;

        if (pos >= captureSize)
        {
            state = (int) State::analysing;
            return true;
        }

        return false;
    }

    // Desde la interfaz: calcula la latencia. Devuelve el estado final.
    State analyse (int maxLag)
    {
        double dryEnergy = 0.0, postEnergy = 0.0;

        for (int i = 0; i < captureSize; ++i)
        {
            dryEnergy  += (double) dry[(size_t) i] * dry[(size_t) i];
            postEnergy += (double) post[(size_t) i] * post[(size_t) i];
        }

        // Sin audio (por debajo de -70 dBFS en promedio) no se puede medir.
        if (dryEnergy / captureSize < 1.0e-7 || postEnergy / captureSize < 1.0e-7)
            return finish (State::noSignal, 0.0, false);

        maxLag = juce::jlimit (0, captureSize / 2, maxLag);

        // Correlación cruzada con FFT (el doble de largo para que no "dé la vuelta").
        constexpr int fftOrder = captureOrder + 1;
        constexpr int fftSize  = 1 << fftOrder;
        juce::dsp::FFT fft (fftOrder);

        std::vector<std::complex<float>> a ((size_t) fftSize), b ((size_t) fftSize), spectrumA ((size_t) fftSize), spectrumB ((size_t) fftSize);

        for (int i = 0; i < captureSize; ++i)
        {
            a[(size_t) i] = { post[(size_t) i], 0.0f };
            b[(size_t) i] = { dry[(size_t) i], 0.0f };
        }

        fft.perform (a.data(), spectrumA.data(), false);
        fft.perform (b.data(), spectrumB.data(), false);

        // "PHAT": iguala todas las frecuencias, así el pico queda bien marcado
        // aunque la música tenga notas que se repiten o tus efectos ecualicen.
        // (Se guarda también el espectro cruzado original para el ajuste fino.)
        // Las frecuencias donde casi no hay nada (por ejemplo, lo que un EQ cortó del
        // todo) no se usan: su fase es ruido y podía hacer que el pico apareciera en
        // cualquier lado.
        float maxMagnitude = 0.0f;
        for (int i = 0; i < fftSize; ++i)
        {
            auto cross = spectrumA[(size_t) i] * std::conj (spectrumB[(size_t) i]);
            spectrumB[(size_t) i] = cross;
            maxMagnitude = std::max (maxMagnitude, std::abs (cross));
        }

        const float floorMagnitude = std::max (1.0e-12f, maxMagnitude * 1.0e-8f);   // -80 dB
        for (int i = 0; i < fftSize; ++i)
        {
            const auto cross = spectrumB[(size_t) i];
            const float magnitude = std::abs (cross);
            spectrumA[(size_t) i] = magnitude > floorMagnitude ? cross / magnitude : std::complex<float> {};
        }

        fft.perform (spectrumA.data(), a.data(), true);

        int bestLag = 0;
        float bestValue = 0.0f;

        for (int lag = 0; lag <= maxLag; ++lag)
        {
            const float v = std::abs (a[(size_t) lag].real());

            if (v > bestValue)
            {
                bestValue = v;
                bestLag = lag;
            }
        }

        // ¿Qué tan claro es el pico? (cuántas veces más alto que lo típico de la curva)
        {
            std::vector<float> values ((size_t) maxLag + 1);
            for (int lag = 0; lag <= maxLag; ++lag)
                values[(size_t) lag] = std::abs (a[(size_t) lag].real());
            auto middle = values.begin() + (std::ptrdiff_t) (values.size() / 2);
            std::nth_element (values.begin(), middle, values.end());
            lastProminence = *middle > 0.0f ? (double) bestValue / (double) *middle : 0.0;
        }

        // Afinar con decimales: se reconstruye la curva entre muestras
        // (interpolación sinc) y se busca el punto más alto.
        auto correlationAt = [&] (double offset)
        {
            constexpr int K = 24;
            double acc = 0.0;

            for (int k = -K; k <= K; ++k)
            {
                const double x = offset - k;
                const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double u = x / (K + 1.0);
                const double window = 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * u) + 0.08 * std::cos (2.0 * juce::MathConstants<double>::pi * u);
                const int index = (bestLag + k + fftSize) % fftSize;   // lags negativos están al final
                acc += a[(size_t) index].real() * sinc * window;
            }

            return acc;
        };

        double bestOffset = 0.0, bestAbs = std::abs (correlationAt (0.0));

        for (int pass = 0; pass < 2; ++pass)
        {
            const double step  = pass == 0 ? 0.01 : 0.0005;
            const double range = pass == 0 ? 1.0 : 0.01;
            const double from = bestOffset - range;
            const double to   = bestOffset + range;

            for (double o = from; o <= to + 1.0e-12; o += step)
            {
                const double v = std::abs (correlationAt (o));

                if (v > bestAbs)
                {
                    bestAbs = v;
                    bestOffset = o;
                }
            }
        }

        const bool polarityInverted = correlationAt (bestOffset) < 0.0;
        double estimate = bestLag + bestOffset;

        // Ajuste fino con la fase: un retardo de τ muestras gira la fase de
        // cada frecuencia en proporción a τ. Se mide ese giro (pesado por la
        // energía de cada frecuencia) y se corrige la estimación.
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            double numerator = 0.0, denominator = 0.0;
            const int maxBin = (int) (0.45 * fftSize);

            for (int k = 1; k < maxBin; ++k)
            {
                const double omega = 2.0 * juce::MathConstants<double>::pi * k / fftSize;
                std::complex<double> c (spectrumB[(size_t) k].real(), spectrumB[(size_t) k].imag());

                if (polarityInverted)
                    c = -c;

                const double weight = std::abs (c);
                if (weight <= 0.0)
                    continue;

                const double phase = std::arg (c * std::polar (1.0, omega * estimate));   // lo que sobra
                numerator   += weight * omega * phase;
                denominator += weight * omega * omega;
            }

            if (denominator <= 0.0)
                break;

            const double correction = -numerator / denominator;

            if (std::abs (correction) > 1.0)                              // algo raro: no tocar
                break;

            estimate += correction;
        }

        const double exactLag = juce::jlimit (0.0, (double) maxLag, estimate);

        // ¿Qué tan parecidas son las dos señales con ese corrimiento? (0 a 1)
        const int checkLag = (int) std::lround (exactLag);
        double cross = 0.0, e1 = 0.0, e2 = 0.0;

        for (int t = checkLag; t < captureSize; ++t)
        {
            const double p = post[(size_t) t];
            const double d = dry[(size_t) (t - checkLag)];
            cross += p * d;
            e1 += p * p;
            e2 += d * d;
        }

        const double similarity = (e1 > 0.0 && e2 > 0.0) ? std::abs (cross) / std::sqrt (e1 * e2) : 0.0;
        lastSimilarity = similarity;

        // Hace falta que se parezcan las ondas O que el pico sea muy claro. Lo segundo
        // cubre efectos que cambian mucho la onda pero no el tiempo (un EQ que deja
        // solo los agudos, saturación fuerte), que antes daban "No clear match".
        if (similarity < minSimilarity && lastProminence < minProminence)
            return finish (State::unclear, 0.0, false);

        // Si es casi entero, se redondea (evita "64,0003").
        const double rounded = std::abs (exactLag - std::round (exactLag)) < 0.005 ? std::round (exactLag) : exactLag;
        return finish (State::done, rounded, polarityInverted);
    }

    double getLastSimilarity() const { return lastSimilarity; }
    double getLastProminence() const { return lastProminence; }

    static constexpr double minSimilarity = 0.25;
    static constexpr double minProminence = 20.0;

private:
    State finish (State s, double lag, bool isPolarityInverted)
    {
        result = lag;
        inverted = isPolarityInverted;
        state = (int) s;
        return s;
    }

    std::vector<float> dry, post;
    std::atomic<int> state { (int) State::idle };
    std::atomic<int> writePos { 0 };
    std::atomic<double> result { 0.0 };
    std::atomic<bool> inverted { false };
    std::atomic<float> progress { 0.0f };
    double lastSimilarity = 0.0, lastProminence = 0.0;
};
