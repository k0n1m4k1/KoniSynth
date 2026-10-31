// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación, tras Dsp/OscillatorCore.h (usa Wave).
// No es una cabecera independiente.
#pragma once

//==============================================================================
// Escalón de banda limitada de fase mínima (minBLEP; E. Brandt, "Hard Sync Without Aliasing", ICMC 2001) para los
// osciladores del motor Synth. Latencia 0: la fase mínima no tiene preeco, así que cada salto solo corrige las muestras
// posteriores. Núcleo: sinc con corte a 0,9·Nyquist (21,6 kHz a 48 kHz), 16 cruces por lado, ventana de Kaiser β 9;
// fase mínima por cepstro real plegado; integral normalizada = escalón. Se guarda el residuo (escalón suave - escalón
// ideal) en 32 muestras × 64 subfases con interpolación lineal.
// Réplica (scratchpad/minblep_design.py, misma métrica que sync_alias2.py): sierra 440 Hz -106,6 dB fuera de la rejilla
// armónica en 20 Hz-20 kHz (PolyBLEP de 2 puntos medido en C++: -38,6 dB); sierra con sync duro -105,4 dB.
// La corrección es lineal: con unísono, una sola cola estéreo por voz suma delta·ganancia de cada carril.
// Dos tablas: a fs (corte 0,9 del Nyquist, 32 muestras) y para el oscilador complejo a 2·fs (corte 0,45 del Nyquist de
// 2·fs = el mismo 21,6 kHz, 64 muestras = la misma duración): a 2·fs con corte 0,9 quedaba energía entre 24 y 43 kHz que el
// FIR de diezmado no quita del todo (medido: -61 dB fuera de rejilla con el complejo sin sync).
namespace MinBlep
{
constexpr int kOversample = 64;

// FFT compleja radix 2 en doble (solo para construir la tabla una vez)
inline void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const auto n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto ang = juce::MathConstants<double>::twoPi / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k, w *= wl)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double) n;
}

struct Table
{
    int samples = 32;
    std::vector<float> res;

    // Residuo a 'tau' muestras del salto (tau >= 0)
    float at (double tau) const
    {
        const auto p = std::max (0.0, tau) * kOversample;
        const auto i = (int) p;
        if (i >= samples * kOversample) return 0.0f;
        const auto a = (float) (p - i);
        return res[(size_t) i] + a * (res[(size_t) i + 1] - res[(size_t) i]);
    }
};

// cutoff: fracción del Nyquist de la tasa a la que trabaja la tabla; samples: longitud guardada del residuo
inline Table makeTable (double cutoff, int samples)
{
    {
        constexpr double zeros = 16.0, beta = 9.0;
        const auto half = zeros / cutoff;
        const auto n = (int) (2.0 * half * kOversample) + 1;
        auto i0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 50; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        size_t nfft = 1;
        while (nfft < (size_t) n * 8) nfft <<= 1;
        std::vector<std::complex<double>> h (nfft);
        for (int i = 0; i < n; ++i)
        {
            const auto x = -half + 2.0 * half * i / (n - 1);
            const auto arg = juce::MathConstants<double>::pi * cutoff * x;
            const auto sinc = std::abs (arg) < 1.0e-12 ? 1.0 : std::sin (arg) / arg;
            const auto r = 2.0 * i / (n - 1) - 1.0;
            h[(size_t) i] = cutoff * sinc * i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
        }
        // Cepstro real: c = IFFT(log|H|); plegado (c0, 2·c1..c(N/2-1), c(N/2), 0...); fase mínima = IFFT(exp(FFT(plegado)))
        fft (h, false);
        for (auto& x : h) x = std::log (std::max (std::abs (x), 1.0e-12));
        fft (h, true);
        for (size_t i = 1; i < nfft / 2; ++i) h[i] *= 2.0;
        for (size_t i = nfft / 2 + 1; i < nfft; ++i) h[i] = 0.0;
        fft (h, false);
        for (auto& x : h) x = std::exp (x);
        fft (h, true);
        std::vector<double> step ((size_t) n);
        double acc = 0.0;
        for (int i = 0; i < n; ++i) { acc += h[(size_t) i].real(); step[(size_t) i] = acc; }
        Table t;
        t.samples = samples;
        t.res.assign ((size_t) (samples * kOversample + 2), 0.0f);
        for (size_t i = 0; i < t.res.size() && i < step.size(); ++i)
            t.res[i] = (float) (step[i] / acc - 1.0);
        t.res.back() = 0.0f; // la cola acaba en 0 (|residuo| en la última muestra: 2,2e-5 en la réplica)
        return t;
    }
}

inline const Table& table1x() { static const auto t = makeTable (0.9, 32); return t; }
inline const Table& table2x() { static const auto t = makeTable (0.45, 64); return t; }

// Cola de correcciones de una fuente (estéreo). Cada muestra: pop() suma y limpia la posición actual; add() registra un
// salto ocurrido 'tau' (0 < tau <= 1) muestras después de la última muestra entregada
struct Ring
{
    static constexpr int kSize = 128, kMask = kSize - 1; // >= la tabla más larga (64)

    explicit Ring (const Table& t) : table (&t) {}

    void add (double tau, float deltaL, float deltaR)
    {
        for (int k = 1; k <= table->samples; ++k)
        {
            const auto v = table->at ((double) k - tau);
            const auto idx = (size_t) ((pos + k - 1) & kMask);
            l[idx] += deltaL * v;
            r[idx] += deltaR * v;
        }
    }

    void pop (float& left, float& right)
    {
        left += l[(size_t) pos];
        right += r[(size_t) pos];
        l[(size_t) pos] = r[(size_t) pos] = 0.0f;
        pos = (pos + 1) & kMask;
    }

    void reset() { l.fill (0.0f); r.fill (0.0f); pos = 0; }

    const Table* table;
    std::array<float, kSize> l {}, r {};
    int pos = 0;
};

// Forma ingenua (sin corrección), mismos valores que oscillator(): sierra 2t - 1; pulso 0,7·(±1 - (2d - 1)); triángulo; seno;
// mezclas (Tri-Saw y combinaciones de sierra, triángulo y pulso)
inline float naive (int wave, double t, double pw)
{
    if (isMixWave (wave))
        return oscillator (wave, t, 0.0, (float) pw, false);
    switch (wave)
    {
        case triangle: return (float) (1.0 - 4.0 * std::abs (t - 0.5));
        case saw:      return (float) (2.0 * t - 1.0);
        case square:   return 0.7f * ((t < pw ? 1.0f : -1.0f) - (float) (2.0 * pw - 1.0));
        default:       return (float) std::sin (juce::MathConstants<double>::twoPi * t);
    }
}

// Saltos de valor de la forma ingenua mientras la fase recorre linealmente de t0 (instante f0) a t0 + inc·(f1 - f0)
// (instante f1); instantes en fracciones de muestra tras la última muestra entregada. inc puede ser negativo (FM que
// atraviesa cero). Sierra: salto -2 al cruzar un entero subiendo. Pulso: +1,4 en el entero y -1,4 en el ancho d.
// Mezclas: -2·(sierra + pulso) en el entero y +2·pulso en 1 - d (pesos de konisimd::kWaveMix); las esquinas de su parte de
// triángulo, como las del triángulo, sin corregir.
// Seno y triángulo no tienen saltos de valor (el triángulo tiene saltos de pendiente: su aliasing cae 12 dB/oct, sin corregir)
inline void segment (Ring& ring, int wave, double t0, double inc, double f0, double f1, double pw, float gL, float gR)
{
    if ((wave != saw && wave != square && ! isMixWave (wave)) || inc == 0.0) return;
    // Un ciclo o más por muestra (por encima de fs): ningún BLEP evita el aliasing y cada ciclo costaría una corrección;
    // sin tope, Pitch Env +48 st en notas altas pide decenas por muestra
    if (std::abs (inc) >= 1.0) return;
    const auto t1 = t0 + inc * (f1 - f0);
    auto edge = [&] (double c, float deltaUp)
    {
        if (inc > 0.0)
            for (auto x = std::floor (t0 - c) + 1.0 + c; x <= t1; x += 1.0)
                ring.add (f0 + (x - t0) / inc, deltaUp * gL, deltaUp * gR);
        else
            for (auto x = std::ceil (t1 - c) + c; x < t0; x += 1.0)
                ring.add (f0 + (x - t0) / inc, -deltaUp * gL, -deltaUp * gR);
    };
    if (wave == saw)
        edge (0.0, -2.0f);
    else if (isMixWave (wave))
    {
        const auto& m = waveMix (wave);
        edge (0.0, (float) (-2.0 * (m.saw + m.pulse)));
        if (m.pulse != 0.0)
            edge (1.0 - pw, (float) (2.0 * m.pulse));
    }
    else
    {
        edge (0.0, 1.4f);
        edge (pw, -1.4f);
    }
}

// Paso completo de un oscilador esclavo con sync duro: hasta el reinicio (instante f del paso, si lo hay) avanza con inc;
// en el reinicio salta de su valor a naive(0); después avanza con |inc| desde 0 (como el reinicio de los osciladores)
inline void syncedStep (Ring& ring, int wave, double t0, double inc, bool reset, double f, double pw, float gL, float gR)
{
    if (! reset) { segment (ring, wave, t0, inc, 0.0, 1.0, pw, gL, gR); return; }
    segment (ring, wave, t0, inc, 0.0, f, pw, gL, gR);
    auto ta = t0 + inc * f;
    ta -= std::floor (ta);
    const auto jump = naive (wave, 0.0, pw) - naive (wave, ta, pw);
    ring.add (f, jump * gL, jump * gR);
    segment (ring, wave, 0.0, std::abs (inc), f, 1.0, pw, gL, gR);
}
} // namespace MinBlep
