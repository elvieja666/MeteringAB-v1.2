#pragma once
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
//  ScopeBands: para Contorno / Solo líneas ("graves sin onda + agudos prolijos").
//
//  Separa cada onda en dos (corte en 150 Hz, filtros Butterworth de 4º orden):
//   - Graves: se cortan en medios ciclos (positivo / negativo) y de cada uno se
//     guarda solo su pico. Así el borde une pico con pico y no baja a cero en
//     cada ciclo: un "pozo" aparece solo si el volumen del grave baja de verdad.
//     Un medio ciclo termina cuando la onda cruza al otro lado con convicción
//     (más del 25% del nivel reciente), así las ondulaciones chicas no cuentan.
//   - Agudos: van tal cual (pico por columna) y encima del grave.
// ---------------------------------------------------------------------------
class ScopeBands
{
public:
    static constexpr double cutoffHz = 150.0;
    static constexpr double hysteresis = 0.25;          // cruce "de verdad": 25% del nivel
    static constexpr double levelReleaseMs = 300.0;     // memoria del nivel para ese umbral

    struct Result
    {
        float high = 0.0f;                              // agudos de esta muestra
        bool  peakDone = false;                         // terminó un medio ciclo del grave...
        float peak = 0.0f;                              // ...con este pico (+ arriba, - abajo)
        int   peakBin = -1;                             // ...en esta columna (-1 = no se estaba dibujando)
    };

    void prepare (double sampleRate)
    {
        // Butterworth de 4º orden = dos biquads con Q 0,5412 y 1,3066 (RBJ).
        const double qs[2] = { 0.5411961001461970, 1.3065629648763766 };
        for (int s = 0; s < 2; ++s)
        {
            lp[s].design (sampleRate, qs[s], false);
            hp[s].design (sampleRate, qs[s], true);
        }
        levelRelease = std::exp (-1.0 / (levelReleaseMs * 0.001 * sampleRate));
        reset();
    }

    void reset()
    {
        for (auto& f : lp) f.z1 = f.z2 = 0.0;
        for (auto& f : hp) f.z1 = f.z2 = 0.0;
        level = 0.0;
        positive = true;
        curPeak = 0.0;
        curBin = -1;
    }

    // x: la muestra; bin: la columna donde se está dibujando (-1 = no se dibuja).
    Result process (float x, int bin)
    {
        const double low  = lp[1].process (lp[0].process (x));
        Result r;
        r.high = (float) hp[1].process (hp[0].process (x));

        level = std::max (std::abs (low), level * levelRelease);
        const double threshold = hysteresis * level;

        if (positive ? low > curPeak : low < curPeak)   // el pico de este medio ciclo
        {
            curPeak = low;
            curBin = bin;
        }

        if (positive ? low < -threshold : low > threshold)   // cruzó al otro lado: cerrar el medio ciclo
        {
            r.peakDone = true;
            r.peak = (float) curPeak;
            r.peakBin = curBin;
            positive = ! positive;
            curPeak = low;
            curBin = bin;
        }

        return r;
    }

    // El medio ciclo que está en curso (su pico hasta ahora y en qué columna).
    float pendingPeak() const { return (float) curPeak; }
    int   pendingBin() const  { return curBin; }

private:
    struct Biquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void design (double sampleRate, double q, bool highPass)
        {
            const double w0 = 2.0 * 3.14159265358979323846 * cutoffHz / sampleRate;
            const double cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            const double k = highPass ? (1.0 + cosw) * 0.5 : (1.0 - cosw) * 0.5;
            b0 = k / a0;
            b1 = (highPass ? -2.0 * k : 2.0 * k) / a0;
            b2 = b0;
            a1 = -2.0 * cosw / a0;
            a2 = (1.0 - alpha) / a0;
        }

        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    Biquad lp[2], hp[2];
    double levelRelease = 0.99, level = 0.0, curPeak = 0.0;
    bool positive = true;
    int curBin = -1;
};
