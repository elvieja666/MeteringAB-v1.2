#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
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
//  Eficiencia (mismo resultado, ~3 veces menos CPU):
//   - Los filtros de las 20 bandas se calculan juntos (el procesador hace
//     varias bandas a la vez).
//   - El "sostener y recortar" de cada banda trabaja de a tramos cortos (el
//     pico de cada tramo): en el grave, de a 1/64 de ciclo; en los agudos,
//     muestra por muestra. El tope (pico real), de a 2 muestras.
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

        double edges[numBands + 1];
        for (int k = 0; k <= numBands; ++k)
            edges[k] = 20.0 * std::pow (2.0, k * 0.5);

        const double nyquistLimit = 0.45 * sr;
        int maxDelay = 0;
        activeBands = 0;

        for (int s = 0; s < stages; ++s)
            for (int k = 0; k < numBands; ++k)
                setCoefs (s, k, Coefs {});                     // sin filtro: pasa igual

        for (int k = 0; k < numBands; ++k)
        {
            const double lo = edges[k], hi = edges[k + 1];
            if (k > 0 && lo >= nyquistLimit)
                break;                                         // fuera de rango (muestreo bajo)

            std::vector<Coefs> filters;
            const bool first = k == 0, last = k == numBands - 1 || hi >= nyquistLimit;
            if (! first) addButterworth (filters, lo, true);   // pasa-altos en el borde de abajo
            if (! last)  addButterworth (filters, hi, false);  // pasa-bajos en el borde de arriba
            for (size_t s = 0; s < filters.size(); ++s)
                setCoefs ((int) s, k, filters[s]);

            auto& b = bands[(size_t) k];
            const double lowest = first ? 18.0 : lo;
            const int window = std::clamp ((int) std::lround (sr / lowest), 8, std::max (8, (int) std::lround (2700.0 * scale)));
            const int radius = window / 2;

            // De a cuántas muestras: 1/64 de ciclo (potencia de 2), muestra por muestra en los agudos.
            b.step = window >= 16 ? 2 : 1;
            while (b.step * 2 <= window / 64) b.step *= 2;
            const int blocks = (radius + b.step - 1) / b.step;
            b.hold.setup (2 * blocks + 1);
            b.tight.setup (2 * blocks + 1);

            const double centre = first ? 22.0 : last ? std::min (lo * 1.5, nyquistLimit) : std::sqrt (lo * hi);
            b.delay = onsetDelay (filters, centre) + 2 * blocks * b.step + b.step - 1;
            maxDelay = std::max (maxDelay, b.delay);
            activeBands = k + 1;
        }

        // Tope: el pico real, tapando los huecos entre ciclos (±28 ms, hasta 18 Hz) pero
        // sin ensanchar los bordes; así la suma nunca pasa la onda ni se adelanta a un golpe.
        const int capRadius = std::max (1, (int) std::lround (1350.0 * scale));
        const int capBlocks = (capRadius + capStep - 1) / capStep;
        for (auto* c : { &capPosHold, &capNegHold }) c->setup (2 * capBlocks + 1);
        for (auto* c : { &capPosTight, &capNegTight }) c->setup (2 * capBlocks + 1);
        const int capDelay = 2 * capBlocks * capStep + capStep - 1;
        latency = std::max (maxDelay, capDelay);

        for (int k = 0; k < activeBands; ++k)
            bands[(size_t) k].align.setup (latency - bands[(size_t) k].delay);
        capPosAlign.setup (latency - capDelay);
        capNegAlign.setup (latency - capDelay);
        reset();
    }

    void reset()
    {
        for (int s = 0; s < stages; ++s)
            for (int k = 0; k < numBands; ++k)
                z1[s][k] = z2[s][k] = 0.0;
        for (auto& b : bands)
        {
            b.hold.reset(); b.tight.reset(); b.align.reset();
            b.peak = 0.0f; b.count = 0; b.value = 0.0f;
        }
        for (auto* c : { &capPosHold, &capNegHold }) c->reset();
        for (auto* c : { &capPosTight, &capNegTight }) c->reset();
        capPosAlign.reset(); capNegAlign.reset();
        capPosPeak = capNegPeak = capPosValue = capNegValue = 0.0f;
        capCount = 0;
    }

    // Cuántas muestras atrasa el resultado (el mismo para todas las bandas).
    int getLatency() const { return latency; }

    // Una muestra. Devuelve el borde de arriba y de abajo (positivo) de la
    // muestra de hace getLatency() muestras.
    void process (float x, float& up, float& down)
    {
        // 1) Filtros: todas las bandas juntas (cada etapa, las 20 bandas a la vez).
        double y[numBands];
        for (int k = 0; k < numBands; ++k)
            y[k] = x;
        for (int s = 0; s < stages; ++s)
        {
            const double* __restrict cb0 = fb0[s]; const double* __restrict cb1 = fb1[s]; const double* __restrict cb2 = fb2[s];
            const double* __restrict ca1 = fa1[s]; const double* __restrict ca2 = fa2[s];
            double* __restrict a = z1[s];
            double* __restrict b = z2[s];
            for (int k = 0; k < numBands; ++k)
            {
                const double in = y[k];
                const double out = cb0[k] * in + a[k];
                a[k] = cb1[k] * in - ca1[k] * out + b[k];
                b[k] = cb2[k] * in - ca2[k] * out;
                y[k] = out;
            }
        }

        // 2) Cada banda: el pico de cada tramo, "sostener y recortar", alinear y sumar.
        float sum = 0.0f;
        for (int k = 0; k < activeBands; ++k)
        {
            auto& b = bands[(size_t) k];
            b.peak = std::max (b.peak, (float) std::abs (y[k]));
            if (++b.count == b.step)
            {
                b.value = b.tight.push (b.hold.push (b.peak));
                b.peak = 0.0f;
                b.count = 0;
            }
            sum += b.align.push (b.value);
        }

        // 3) Tope: el pico real.
        capPosPeak = std::max (capPosPeak, std::max (0.0f, x));
        capNegPeak = std::max (capNegPeak, std::max (0.0f, -x));
        if (++capCount == capStep)
        {
            capPosValue = capPosTight.push (capPosHold.push (capPosPeak));
            capNegValue = capNegTight.push (capNegHold.push (capNegPeak));
            capPosPeak = capNegPeak = 0.0f;
            capCount = 0;
        }
        const float cp = capPosAlign.push (capPosValue);
        const float cn = capNegAlign.push (capNegValue);
        up = std::min (sum, cp);
        down = std::min (sum, cn);
    }

private:
    struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    // Máximo (o mínimo) de las últimas N muestras, una muestra por vez (sin pedir memoria).
    // Cola de candidatos: al frente el mejor, al final el más nuevo (cada valor entra y
    // sale una sola vez, así que cuesta lo mismo con ventanas cortas o largas).
    template <bool wantMax>
    struct Sliding
    {
        struct Item { float v; std::uint32_t t; };
        std::vector<Item> items;
        std::uint32_t head = 0, tail = 0, t = 0, mask = 0, length = 1;

        void setup (int n)
        {
            length = (std::uint32_t) std::max (1, n);
            std::uint32_t cap = 1;
            while (cap < length + 1) cap <<= 1;
            mask = cap - 1;
            items.assign (cap, Item { 0.0f, 0 });
            reset();
        }
        void reset() { head = tail = t = 0; }

        float push (float v)
        {
            if constexpr (wantMax) { while (tail != head && v >= items[(tail - 1) & mask].v) --tail; }
            else                   { while (tail != head && v <= items[(tail - 1) & mask].v) --tail; }
            items[tail & mask] = Item { v, t };
            ++tail;
            if (t - items[head & mask].t >= length) ++head;   // el del frente salió de la ventana
            ++t;
            return items[head & mask].v;
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
        Sliding<true> hold;
        Sliding<false> tight;
        Delay align;
        int step = 1, delay = 0, count = 0;
        float peak = 0.0f, value = 0.0f;
    };

    // Butterworth de 4º orden (dos secciones de 2º orden).
    void addButterworth (std::vector<Coefs>& out, double freq, bool highPass) const
    {
        const double w = std::tan (3.14159265358979323846 * freq / sr);
        for (const double q : { 0.54119610, 1.30656296 })
        {
            Coefs f;
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
    int onsetDelay (const std::vector<Coefs>& filters, double freq) const
    {
        const int length = std::max ((int) (0.25 * sr), (int) (30.0 * sr / freq));
        std::vector<float> out ((size_t) length);
        std::vector<double> s1 (filters.size(), 0.0), s2 (filters.size(), 0.0);
        for (int i = 0; i < length; ++i)
        {
            double y = std::sin (2.0 * 3.14159265358979323846 * freq * i / sr);
            for (size_t f = 0; f < filters.size(); ++f)
            {
                const auto& c = filters[f];
                const double o = c.b0 * y + s1[f];
                s1[f] = c.b1 * y - c.a1 * o + s2[f];
                s2[f] = c.b2 * y - c.a2 * o;
                y = o;
            }
            out[(size_t) i] = (float) std::abs (y);
        }
        float steady = 0.0f;
        for (int i = length / 2; i < length; ++i) steady = std::max (steady, out[(size_t) i]);
        for (int i = 0; i < length; ++i)
            if (out[(size_t) i] >= 0.5f * steady)
                return i;
        return 0;
    }

    static constexpr int stages = 4;      // pasa-altos (2) + pasa-bajos (2)
    static constexpr int capStep = 2;

    double sr = 48000.0;
    // Coeficientes por etapa, las 20 bandas seguidas (así se calculan juntas).
    alignas (32) double fb0[stages][numBands], fb1[stages][numBands], fb2[stages][numBands],
                        fa1[stages][numBands], fa2[stages][numBands];
    void setCoefs (int s, int k, const Coefs& c)
    {
        fb0[s][k] = c.b0; fb1[s][k] = c.b1; fb2[s][k] = c.b2; fa1[s][k] = c.a1; fa2[s][k] = c.a2;
    }
    alignas (32) double z1[stages][numBands] {}, z2[stages][numBands] {};
    Band bands[numBands];
    int activeBands = 0;
    Sliding<true> capPosHold, capNegHold;
    Sliding<false> capPosTight, capNegTight;
    Delay capPosAlign, capNegAlign;
    float capPosPeak = 0.0f, capNegPeak = 0.0f, capPosValue = 0.0f, capNegValue = 0.0f;
    int capCount = 0;
    int latency = 0;
};
