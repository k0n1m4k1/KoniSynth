// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, Params). No es una cabecera independiente.
#pragma once

//==============================================================================
// Ecualizador paramétrico de 6 etapas (tras la válvula, antes de la modulación: el sitio del EQ de un ampli; la reverb y el
// eco reciben el tono ya ecualizado). Etapas, en este orden:
//   Low Cut (paso alto Butterworth de 12/24/48 dB/oct) · Low (estante, pendiente S = 1) · Mid 1 y Mid 2 (picos con Q) ·
//   High (estante, S = 1) · High Cut (paso bajo Butterworth 12/24/48 dB/oct) · Output (dB).
// Coeficientes del "Audio EQ Cookbook" de R. Bristow-Johnson (fórmulas estándar; transformada bilineal con frecuencia
// prewarpeada en cada etapa, así la frecuencia central es exacta). Butterworth de orden 2N: N secciones con
// Q_k = 1 / (2·cos((2k-1)·π / (4N))) (12 dB: 0,7071; 24 dB: 0,5412 y 1,3066).
// Sin zipper: cada parámetro se suaviza (frecuencias en escala logarítmica, ganancias en dB) con constante de 20 ms y los
// coeficientes se recalculan cada 32 muestras. Estructura transpuesta de forma directa II (estable con cambios lentos).
// Coste: 10 biquads × 2 canales como máximo = ~200 operaciones por muestra estéreo. Latencia: 0.
namespace Eq
{
struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

inline Coeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline double w0Of (double f, double fs) { return juce::MathConstants<double>::twoPi * juce::jlimit (5.0, 0.49 * fs, f) / fs; }

inline Coeffs peak (double f, double gainDb, double q, double fs)
{
    const auto a = std::pow (10.0, gainDb / 40.0), w = w0Of (f, fs), alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
    return normalise (1 + alpha * a, -2 * c, 1 - alpha * a, 1 + alpha / a, -2 * c, 1 - alpha / a);
}

inline Coeffs lowShelf (double f, double gainDb, double fs)
{
    const auto a = std::pow (10.0, gainDb / 40.0), w = w0Of (f, fs), c = std::cos (w);
    const auto beta = 2.0 * std::sqrt (a) * std::sin (w) / 2.0 * std::sqrt (2.0); // 2·√A·α con S = 1
    return normalise (a * ((a + 1) - (a - 1) * c + beta), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - beta),
                      (a + 1) + (a - 1) * c + beta, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - beta);
}

inline Coeffs highShelf (double f, double gainDb, double fs)
{
    const auto a = std::pow (10.0, gainDb / 40.0), w = w0Of (f, fs), c = std::cos (w);
    const auto beta = 2.0 * std::sqrt (a) * std::sin (w) / 2.0 * std::sqrt (2.0);
    return normalise (a * ((a + 1) + (a - 1) * c + beta), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - beta),
                      (a + 1) - (a - 1) * c + beta, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - beta);
}

inline Coeffs highPass (double f, double q, double fs)
{
    const auto w = w0Of (f, fs), alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
    return normalise ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

inline Coeffs lowPass (double f, double q, double fs)
{
    const auto w = w0Of (f, fs), alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
    return normalise ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

// Q de la sección k (0..n-1) de un Butterworth de orden 2n
inline double butterQ (int k, int n) { return 1.0 / (2.0 * std::cos ((2.0 * k + 1.0) * juce::MathConstants<double>::pi / (4.0 * n))); }

// Ajustes de un instante (valores reales de los parámetros)
struct Settings
{
    double lowCut = 20, highCut = 20000, lowFreq = 100, lowGain = 0, mid1Freq = 400, mid1Gain = 0, mid1Q = 1,
           mid2Freq = 2500, mid2Gain = 0, mid2Q = 1, highFreq = 8000, highGain = 0, outputDb = 0;
    int lowCutOrder = 1, highCutOrder = 1; // secciones de 2.º orden: 1 = 12 dB/oct, 2 = 24, 4 = 48
};

// Secciones en 12 posiciones fijas (0-3 Low Cut, 4 Low, 5 Mid 1, 6 Mid 2, 7 High, 8-11 High Cut). Las inactivas (ganancia
// 0 dB, corte en el extremo o sección de un orden menor) son la identidad: en posiciones fijas el estado de cada filtro
// no cambia de sitio al activarse otra etapa, así que no hay que vaciarlo (vaciarlo provocaría un clic)
inline bool identity (const Coeffs& c) { return c.b0 == 1.0 && c.b1 == 0.0 && c.b2 == 0.0 && c.a1 == 0.0 && c.a2 == 0.0; }

inline int sections (const Settings& s, double fs, std::array<Coeffs, 12>& out)
{
    out.fill (Coeffs {});
    if (s.lowCut > 20.5)
        for (int k = 0; k < s.lowCutOrder; ++k) out[(size_t) k] = highPass (s.lowCut, butterQ (k, s.lowCutOrder), fs);
    if (std::abs (s.lowGain) > 0.01)  out[4] = lowShelf (s.lowFreq, s.lowGain, fs);
    if (std::abs (s.mid1Gain) > 0.01) out[5] = peak (s.mid1Freq, s.mid1Gain, s.mid1Q, fs);
    if (std::abs (s.mid2Gain) > 0.01) out[6] = peak (s.mid2Freq, s.mid2Gain, s.mid2Q, fs);
    if (std::abs (s.highGain) > 0.01) out[7] = highShelf (s.highFreq, s.highGain, fs);
    if (s.highCut < 19900.0 && s.highCut < 0.49 * fs)
        for (int k = 0; k < s.highCutOrder; ++k) out[(size_t) (8 + k)] = lowPass (s.highCut, butterQ (k, s.highCutOrder), fs);
    return 12;
}

// Respuesta en dB a la frecuencia f (para la curva de la UI): producto de |H(e^{jw})| de cada sección
inline double responseDb (const Settings& s, double fs, double f)
{
    std::array<Coeffs, 12> c;
    const auto n = sections (s, fs, c);
    const std::complex<double> z1 = std::polar (1.0, -juce::MathConstants<double>::twoPi * f / fs), z2 = z1 * z1;
    double mag = std::pow (10.0, s.outputDb / 20.0);
    for (int i = 0; i < n; ++i)
    {
        const auto& k = c[(size_t) i];
        mag *= std::abs ((k.b0 + k.b1 * z1 + k.b2 * z2) / (1.0 + k.a1 * z1 + k.a2 * z2));
    }
    return 20.0 * std::log10 (std::max (mag, 1e-9));
}
} // namespace Eq

struct Equalizer
{
    void prepare (double sr)
    {
        fs = sr;
        reset();
    }

    void reset()
    {
        for (auto& ch : state) for (auto& z : ch) z = { 0.0, 0.0 };
        primed = false;
    }

    void process (juce::AudioBuffer<float>& buffer, const Eq::Settings& target)
    {
        if (! primed) { current = target; primed = true; }
        const auto chans = juce::jmin (2, buffer.getNumChannels());
        const auto n = buffer.getNumSamples();
        constexpr int chunk = 32;
        const auto k = 1.0 - std::exp (-(double) chunk / (0.020 * fs)); // constante de 20 ms por tramo de 32 muestras
        for (int start = 0; start < n; start += chunk)
        {
            smooth (target, k);
            std::array<Eq::Coeffs, 12> c;
            const auto ns = Eq::sections (current, fs, c);
            std::array<bool, 12> active {};
            for (int sct = 0; sct < ns; ++sct) active[(size_t) sct] = ! Eq::identity (c[(size_t) sct]);
            const auto gain = std::pow (10.0, current.outputDb / 20.0);
            const auto len = juce::jmin (chunk, n - start);
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* x = buffer.getWritePointer (ch, start);
                auto& st = state[(size_t) ch];
                for (int i = 0; i < len; ++i)
                {
                    double v = x[i];
                    for (int s = 0; s < ns; ++s)
                    {
                        if (! active[(size_t) s]) continue;
                        const auto& q = c[(size_t) s];
                        auto& z = st[(size_t) s];
                        const auto y = q.b0 * v + z.first;
                        z.first = q.b1 * v - q.a1 * y + z.second;
                        z.second = q.b2 * v - q.a2 * y;
                        v = y;
                    }
                    x[i] = (float) (v * gain);
                }
            }
        }
    }

private:
    void smooth (const Eq::Settings& t, double k)
    {
        auto lin = [k] (double& c, double v) { c += (v - c) * k; };
        auto logf = [k] (double& c, double v) { c = std::exp (std::log (c) + (std::log (v) - std::log (c)) * k); };
        logf (current.lowCut, t.lowCut); logf (current.highCut, t.highCut); logf (current.lowFreq, t.lowFreq);
        logf (current.mid1Freq, t.mid1Freq); logf (current.mid2Freq, t.mid2Freq); logf (current.highFreq, t.highFreq);
        logf (current.mid1Q, t.mid1Q); logf (current.mid2Q, t.mid2Q);
        lin (current.lowGain, t.lowGain); lin (current.mid1Gain, t.mid1Gain); lin (current.mid2Gain, t.mid2Gain);
        lin (current.highGain, t.highGain); lin (current.outputDb, t.outputDb);
        current.lowCutOrder = t.lowCutOrder; current.highCutOrder = t.highCutOrder; // discretos: al instante
    }

    double fs = 48000.0;
    Eq::Settings current;
    bool primed = false;
    std::array<std::array<std::pair<double, double>, 12>, 2> state {};
};
