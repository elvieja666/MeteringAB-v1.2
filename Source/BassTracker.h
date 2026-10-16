#pragma once
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
//  BassTracker: para el osciloscopio. Mira solo los graves (filtro pasa-bajos
//  de 150 Hz) y mide cuánto dura su ciclo contando los cruces por cero.
//  Con eso Contorno / Solo líneas saben cuánto sostener el borde: lo justo
//  para tapar las ondas del grave, y casi nada cuando no hay un grave fuerte.
// ---------------------------------------------------------------------------
class BassTracker
{
public:
    static constexpr double cutoffHz = 150.0;

    void prepare (double sampleRate)
    {
        // Pasa-bajos Butterworth de 2º orden (biquad RBJ, Q = 1/√2).
        const double w0 = 2.0 * 3.14159265358979323846 * cutoffHz / sampleRate;
        const double cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cosw) * 0.5 / a0;
        b1 = (1.0 - cosw) / a0;
        b2 = b0;
        a1 = -2.0 * cosw / a0;
        a2 = (1.0 - alpha) / a0;
        maxHalf = (int) (0.1 * sampleRate);       // más de 100 ms: no hay grave que tapar
        reset();
    }

    void reset()
    {
        z1 = z2 = 0.0;
        sinceCrossing = lastHalf = maxHalf;
        wasNegative = false;
    }

    // Procesa una muestra y devuelve el grave (para comparar su nivel con el total).
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;

        const bool negative = y < 0.0;
        if (negative != wasNegative)
        {
            lastHalf = sinceCrossing;
            sinceCrossing = 0;
            wasNegative = negative;
        }
        else if (sinceCrossing < maxHalf)
        {
            ++sinceCrossing;
        }

        return (float) y;
    }

    // Duración de un ciclo del grave, en muestras (estimación prudente: la más larga
    // entre el último medio ciclo completo y el que está en curso).
    double periodSamples() const { return 2.0 * std::max (lastHalf, sinceCrossing); }

private:
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    int sinceCrossing = 0, lastHalf = 0, maxHalf = 4800;
    bool wasNegative = false;
};
