#pragma once
#include <juce_dsp/juce_dsp.h>
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
    int getResult() const     { return result.load(); }

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
            return finish (State::noSignal, 0);

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
        for (int i = 0; i < fftSize; ++i)
        {
            auto cross = spectrumA[(size_t) i] * std::conj (spectrumB[(size_t) i]);
            const float magnitude = std::abs (cross);
            spectrumA[(size_t) i] = magnitude > 1.0e-12f ? cross / magnitude : std::complex<float> {};
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

        // ¿Qué tan parecidas son las dos señales con ese corrimiento? (0 a 1)
        double cross = 0.0, e1 = 0.0, e2 = 0.0;

        for (int t = bestLag; t < captureSize; ++t)
        {
            const double p = post[(size_t) t];
            const double d = dry[(size_t) (t - bestLag)];
            cross += p * d;
            e1 += p * p;
            e2 += d * d;
        }

        const double similarity = (e1 > 0.0 && e2 > 0.0) ? std::abs (cross) / std::sqrt (e1 * e2) : 0.0;
        lastSimilarity = similarity;

        if (similarity < minSimilarity)
            return finish (State::unclear, 0);

        return finish (State::done, bestLag);
    }

    double getLastSimilarity() const { return lastSimilarity; }

    static constexpr double minSimilarity = 0.25;

private:
    State finish (State s, int lag)
    {
        result = lag;
        state = (int) s;
        return s;
    }

    std::vector<float> dry, post;
    std::atomic<int> state { (int) State::idle };
    std::atomic<int> writePos { 0 };
    std::atomic<int> result { 0 };
    std::atomic<float> progress { 0.0f };
    double lastSimilarity = 0.0;
};
