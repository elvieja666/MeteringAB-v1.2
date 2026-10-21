#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
//  BandEnvelope: el borde de Outline / Lines only, "por bandas".
//
//  1) El audio se separa en 20 bandas (dos por octava, de 20 Hz a 20 kHz).
//  2) Cada banda tiene su propio muestreo: tapa un ciclo de su frecuencia más
//     grave (el grave 2048 muestras, los agudos unas pocas). Así no se ven los
//     ciclos y los agudos quedan precisos. ("sostener y recortar": el pico más
//     alto de la ventana y después el más bajo, así los bordes no se ensanchan).
//  3) Se suman las 20 bandas, sin pasar nunca el pico real (tapando los huecos
//     entre ciclos del grave, sin ensanchar los bordes).
//
//  Los filtros de cada banda atrasan el audio, y no todos lo mismo (los graves
//  mucho más que los agudos). Para que golpes y bandas queden alineados, cada
//  banda se retrasa lo justo para llegar todas juntas: el resultado sale con un
//  atraso fijo (getLatency) que el osciloscopio descuenta al dibujar.
//
//  Sirve en tiempo real (una muestra por vez, sin pedir memoria) y también para
//  recalcular un tramo con zoom desde la onda guardada.
// ---------------------------------------------------------------------------
class BandEnvelope
{
public:
    static constexpr int numBands = 20;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double scale = sr / 48000.0;
        bands.clear();

        double edges[numBands + 1];
        for (int k = 0; k <= numBands; ++k)
            edges[k] = 20.0 * std::pow (2.0, k * 0.5);

        const double nyquistLimit = 0.45 * sr;
        int maxDelay = 0;

        for (int k = 0; k < numBands; ++k)
        {
            const double lo = edges[k], hi = edges[k + 1];
            if (k > 0 && lo >= nyquistLimit)
                break;                                         // fuera de rango (muestreo bajo)

            Band b;
            const bool first = k == 0, last = k == numBands - 1 || hi >= nyquistLimit;
            if (! first) addButterworth (b.filters, lo, true);   // pasa-altos en el borde de abajo
            if (! last)  addButterworth (b.filters, hi, false);  // pasa-bajos en el borde de arriba

            const double lowest = first ? 18.0 : lo;
            const int window = std::clamp ((int) std::lround (sr / lowest), 8, std::max (8, (int) std::lround (2700.0 * scale)));
            b.radius = window / 2;
            b.hold.setup (2 * b.radius + 1, true);
            b.tight.setup (2 * b.radius + 1, false);

            const double centre = first ? 22.0 : last ? std::min (lo * 1.5, nyquistLimit) : std::sqrt (lo * hi);
            b.delay = onsetDelay (b.filters, centre) + 2 * b.radius;
            maxDelay = std::max (maxDelay, b.delay);
            bands.push_back (std::move (b));
        }

        // Tope: el pico real, tapando los huecos entre ciclos (±28 ms, hasta 18 Hz) pero
        // sin ensanchar los bordes; así la suma nunca pasa la onda ni se adelanta a un golpe.
        capRadius = std::max (1, (int) std::lround (1350.0 * scale));
        for (auto* c : { &capPosHold, &capNegHold }) c->setup (2 * capRadius + 1, true);
        for (auto* c : { &capPosTight, &capNegTight }) c->setup (2 * capRadius + 1, false);
        latency = std::max (maxDelay, 2 * capRadius);

        for (auto& b : bands)
            b.align.setup (latency - b.delay);
        capPosAlign.setup (latency - 2 * capRadius);
        capNegAlign.setup (latency - 2 * capRadius);
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            for (auto& f : b.filters) f.z1 = f.z2 = 0.0;
            b.hold.reset(); b.tight.reset(); b.align.reset();
        }
        for (auto* c : { &capPosHold, &capNegHold, &capPosTight, &capNegTight }) c->reset();
        capPosAlign.reset(); capNegAlign.reset();
    }

    // Cuántas muestras atrasa el resultado (el mismo para todas las bandas).
    int getLatency() const { return latency; }

    // Una muestra. Devuelve el borde de arriba y de abajo (positivo) de la
    // muestra de hace getLatency() muestras.
    void process (float x, float& up, float& down)
    {
        float sum = 0.0f;
        for (auto& b : bands)
        {
            double y = x;
            for (auto& f : b.filters)
                y = f.process (y);
            const float held = b.hold.push ((float) std::abs (y));
            sum += b.align.push (b.tight.push (held));
        }

        const float cp = capPosAlign.push (capPosTight.push (capPosHold.push (std::max (0.0f, x))));
        const float cn = capNegAlign.push (capNegTight.push (capNegHold.push (std::max (0.0f, -x))));
        up = std::min (sum, cp);
        down = std::min (sum, cn);
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

    // Máximo (o mínimo) de las últimas N muestras, una muestra por vez (sin pedir memoria).
    struct Sliding
    {
        std::vector<float> val;
        std::vector<long long> idx;
        int head = 0, size = 0, cap = 1, length = 1;
        long long t = 0;
        bool wantMax = true;

        void setup (int n, bool max)
        {
            length = std::max (1, n); cap = length + 1; wantMax = max;
            val.assign ((size_t) cap, 0.0f); idx.assign ((size_t) cap, 0);
            reset();
        }
        void reset() { head = 0; size = 0; t = 0; }

        float push (float v)
        {
            // Cola circular sin divisiones (rápido): head = el mejor, al final el más nuevo.
            int back = head + size - 1; if (back >= cap) back -= cap;
            if (wantMax) { while (size > 0 && v >= val[(size_t) back]) { --size; if (--back < 0) back += cap; } }
            else         { while (size > 0 && v <= val[(size_t) back]) { --size; if (--back < 0) back += cap; } }
            int pos = head + size; if (pos >= cap) pos -= cap;
            val[(size_t) pos] = v; idx[(size_t) pos] = t; ++size;
            while (idx[(size_t) head] <= t - length) { if (++head >= cap) head = 0; --size; }
            ++t;
            return val[(size_t) head];
        }
    };

    struct Delay
    {
        std::vector<float> buf;
        int pos = 0;
        void setup (int n) { buf.assign ((size_t) std::max (0, n), 0.0f); pos = 0; }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        float push (float v)
        {
            if (buf.empty()) return v;
            const float out = buf[(size_t) pos];
            buf[(size_t) pos] = v;
            if (++pos >= (int) buf.size()) pos = 0;
            return out;
        }
    };

    struct Band
    {
        std::vector<Biquad> filters;
        Sliding hold, tight;
        Delay align;
        int radius = 0, delay = 0;
    };

    // Butterworth de 4º orden (dos secciones de 2º orden).
    void addButterworth (std::vector<Biquad>& out, double freq, bool highPass) const
    {
        const double w = std::tan (3.14159265358979323846 * freq / sr);
        for (const double q : { 0.54119610, 1.30656296 })
        {
            Biquad f;
            const double n = 1.0 / (1.0 + w / q + w * w);
            if (highPass) { f.b0 = n; f.b1 = -2.0 * n; f.b2 = n; }
            else          { f.b0 = w * w * n; f.b1 = 2.0 * f.b0; f.b2 = f.b0; }
            f.a1 = 2.0 * (w * w - 1.0) * n;
            f.a2 = (1.0 - w / q + w * w) * n;
            out.push_back (f);
        }
    }

    // Cuánto tarda la banda en "llegar" a un golpe: con un tono que arranca de golpe
    // en su frecuencia, las muestras hasta que la salida llega a la mitad de su nivel.
    // (Así los golpes de todas las bandas quedan alineados donde el oído los pone.)
    int onsetDelay (std::vector<Biquad> filters, double freq) const
    {
        const int length = std::max ((int) (0.25 * sr), (int) (30.0 * sr / freq));
        std::vector<float> out ((size_t) length);
        for (int i = 0; i < length; ++i)
        {
            double y = std::sin (2.0 * 3.14159265358979323846 * freq * i / sr);
            for (auto& f : filters) y = f.process (y);
            out[(size_t) i] = (float) std::abs (y);
        }
        float steady = 0.0f;
        for (int i = length / 2; i < length; ++i) steady = std::max (steady, out[(size_t) i]);
        for (int i = 0; i < length; ++i)
            if (out[(size_t) i] >= 0.5f * steady)
                return i;
        return 0;
    }

    double sr = 48000.0;
    std::vector<Band> bands;
    Sliding capPosHold, capNegHold, capPosTight, capNegTight;
    Delay capPosAlign, capNegAlign;
    int capRadius = 1350, latency = 0;
};
