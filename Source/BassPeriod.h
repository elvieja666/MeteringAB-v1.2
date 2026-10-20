#pragma once
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
//  BassPeriod: cuánto dura el ciclo del grave en este momento (en segundos).
//
//  Outline / Lines only tapan los huecos entre ciclos del grave; para eso
//  necesitan saber cuánto dura cada ciclo (un sub de 20 Hz dura 50 ms, un
//  bajo de 50 Hz 20 ms). Se filtra el grave (< 150 Hz) y se mide la distancia
//  entre dos cruces hacia arriba (con histéresis, así los agudos no cuentan).
//  Si el grave no es una parte importante del sonido, el ciclo es "corto"
//  (minPeriod): no hay nada que tapar y el dibujo queda bien preciso.
// ---------------------------------------------------------------------------
class BassPeriod
{
public:
    static constexpr double minPeriod = 0.003;     // 3 ms: sin grave
    static constexpr double maxPeriod = 0.055;     // 55 ms: ~18 Hz

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double w = std::tan (3.14159265358979323846 * cutoff / sr);
        const double q[2] = { 0.54119610, 1.30656296 };   // Butterworth de 4º orden
        for (int k = 0; k < 2; ++k)
        {
            const double n = 1.0 / (1.0 + w / q[k] + w * w);
            lp[k].b0 = w * w * n; lp[k].b1 = 2.0 * lp[k].b0; lp[k].b2 = lp[k].b0;
            lp[k].a1 = 2.0 * (w * w - 1.0) * n; lp[k].a2 = (1.0 - w / q[k] + w * w) * n;
        }
        release = std::exp (-1.0 / (0.3 * sr));
        reset();
    }

    void reset()
    {
        for (auto& f : lp) f.z1 = f.z2 = 0.0;
        levelLow = levelFull = 0.0;
        positive = true;
        sinceUp = -1;
        current = minPeriod;
    }

    // Una muestra; devuelve el ciclo del grave en segundos.
    float process (float x)
    {
        const double low = lp[1].process (lp[0].process (x));
        levelLow  = std::max (std::abs (low), levelLow * release);
        levelFull = std::max ((double) std::abs (x), levelFull * release);

        if (sinceUp >= 0)
            ++sinceUp;

        const double threshold = 0.25 * levelLow;
        if (positive && low < -threshold)
            positive = false;
        else if (! positive && low > threshold)
        {
            positive = true;
            if (sinceUp > 0)
            {
                const double p = sinceUp / sr;
                if (p >= minPeriod && p <= maxPeriod)
                    current = p;
            }
            sinceUp = 0;
        }

        const bool bassMatters = levelLow > 1.0e-4 && levelLow >= 0.25 * levelFull;
        return (float) (bassMatters ? current : minPeriod);
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static constexpr double cutoff = 150.0;
    double sr = 48000.0, release = 0.99993;
    Biquad lp[2];
    double levelLow = 0.0, levelFull = 0.0, current = minPeriod;
    bool positive = true;
    long long sinceUp = -1;
};
