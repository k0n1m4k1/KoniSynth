// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación, tras Engines/SynthData.h (Ladder).
#pragma once

//==============================================================================
// Filtros del motor Synth además del ladder (SynthData.h). Todos con la misma entrada por muestra: x ya saturada por el
// Drive, g = tan(π·fc/fs), r = resonancia 0..1, fc en Hz (el peine lo necesita en muestras de retardo).
namespace VoiceFilters
{
// Modos del parámetro filterMode (los 4 primeros, el ladder de siempre: los presets guardan el índice)
enum Mode { ladderLp24 = 0, ladderLp12, ladderBp12, ladderHp12, svfLp, svfBp, svfHp, svfNotch, ms20Lp, ms20Hp, combPlus, combMinus,
            dualBp, dualLp, numModes };

// State-variable TPT (Zavalishin, "The Art of VA Filter Design", 4.4): h = 1/(1 + 2R·g + g²);
//   hp = (x - (2R + g)·s1 - s2)·h, bp = g·hp + s1, lp = g·bp + s2. R = amortiguamiento (Q = 1/(2R)).
// La memoria del integrador de banda se acota con 1,5·tanh(s/1,5): con señales pequeñas es lineal (error < 0,3 % para
// |s| < 0,1) y con la resonancia al máximo la autooscilación no crece sin límite (como la saturación del SEM)
struct Svf
{
    float s1 = 0.0f, s2 = 0.0f, lp = 0.0f, bp = 0.0f, hp = 0.0f;
    void reset() { s1 = s2 = lp = bp = hp = 0.0f; }
    void process (float x, float g, float R)
    {
        const auto h = 1.0f / (1.0f + 2.0f * R * g + g * g);
        hp = (x - (2.0f * R + g) * s1 - s2) * h;
        const auto v1 = g * hp;
        bp = v1 + s1;
        s1 = 1.5f * std::tanh ((bp + v1) / 1.5f);
        const auto v2 = g * bp;
        lp = v2 + s2;
        s2 = lp + v2;
    }
    static float damping (float r) { return juce::jmax (0.006f, 1.0f - 0.994f * r); } // r = 1: Q ≈ 83 (autooscila)
};

// Filtro de un polo TPT (LP y HP) con la salida de realimentación del Korg35 (beta · estado)
struct OnePole
{
    float z = 0.0f;
    float process (float x, float G, bool hp)
    {
        const auto v = (x - z) * G;
        const auto lp = v + z;
        z = lp + v;
        return hp ? x - lp : lp;
    }
};

// Korg35 del MS-20 (Sallen-Key de 2 polos con el lazo saturado), forma VA de W. Pirkle ("Designing Software Synthesizer
// Plug-Ins in C++", cap. 7, Korg35): K = 0,01..1,98 (autooscila cerca de 2); u = tanh (α0·(y1 + S35)) en el lazo.
// Pendientes como el original (Wikipedia, Korg MS-20): paso bajo 12 dB/oct, paso alto 6 dB/oct (medido: -13 y -6 dB/oct)
struct Korg35
{
    OnePole a, b, c; // LP: LPF1, LPF2, HPF1 / HP: HPF1, HPF2, LPF1
    void reset() { a = b = c = {}; }
    float process (float x, float g, float r, bool highPass)
    {
        const auto G = g / (1.0f + g);
        const auto K = 0.01f + 1.97f * r;
        const auto alpha0 = 1.0f / (1.0f - K * G + K * G * G);
        if (! highPass)
        {
            const auto y1 = a.process (x, G, false);
            const auto s35 = c.z * (-1.0f / (1.0f + g)) + b.z * ((K - K * G) / (1.0f + g));
            const auto u = std::tanh (alpha0 * (y1 + s35));
            const auto y = K * b.process (u, G, false);
            c.process (y, G, true);
            return y / K;
        }
        const auto y1 = a.process (x, G, true);
        const auto s35 = b.z * (-G / (1.0f + g)) + c.z * (1.0f / (1.0f + g));
        const auto y = std::tanh (K * alpha0 * (y1 + s35)); // como en el LP: la realimentación entra dentro de α0
        c.process (b.process (y, G, true), G, false);
        return y / K;
    }
};

// Peine realimentado afinado: v = x + fb · LP(v[n - D]); la salida se escala por (1 - |fb|) (armónicos resonantes a
// ganancia ~1). Comb +: D = fs/fc, armónicos de fc; Comb -: realimentación negativa y D = fs/(2·fc): solo armónicos
// impares de fc (hueco, como un tubo cerrado). Paso bajo de un polo en el lazo (a = 0,2) y su retardo de fase en DC,
// a/(1-a) muestras, descontado de D. Interpolación de Hermite (4 puntos). Memoria: 16384 muestras (20 Hz a 192 kHz = 9600)
struct Comb
{
    static constexpr int size = 16384;
    std::vector<float> buf = std::vector<float> ((size_t) size, 0.0f);
    int w = 0;
    float lpState = 0.0f;
    bool used = false;
    void reset() { if (used) std::fill (buf.begin(), buf.end(), 0.0f); used = false; lpState = 0.0f; }
    float process (float x, float fcHz, float fs, float r, bool negative)
    {
        constexpr float a = 0.2f;
        used = true;
        const auto fb = (negative ? -1.0f : 1.0f) * 0.985f * r;
        const auto delay = juce::jlimit (2.0f, (float) size - 4.0f, fs / (negative ? 2.0f * fcHz : fcHz) - a / (1.0f - a));
        const auto pos = (float) w - delay;
        const auto i = (int) std::floor (pos);
        const auto t = pos - (float) i;
        const auto at = [this] (int k) { return buf[(size_t) (k & (size - 1))]; };
        const auto y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const auto c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        const auto delayed = ((c3 * t + c2) * t + c1) * t + y1;
        lpState = a * lpState + (1.0f - a) * delayed;
        const auto v = x + fb * lpState;
        buf[(size_t) w] = v;
        w = (w + 1) & (size - 1);
        return v * (1.0f - std::abs (fb));
    }
};

// Todos los filtros de una voz y un canal; el ladder es el de SynthData.h
struct VoiceFilter
{
    Ladder ladder;
    Svf a, b;
    Korg35 k35;
    Comb comb;

    void reset() { ladder.reset(); a.reset(); b.reset(); k35.reset(); comb.reset(); }

    // spreadOct: separación de los dos filtros de los modos Dual (octavas)
    float process (float x, float g, float k, int mode, float fcHz, float fs, float spreadOct)
    {
        if (mode < svfLp)
            return ladder.process (x, g, k, mode);
        const auto r = juce::jlimit (0.0f, 1.0f, k / 4.4f);
        switch (mode)
        {
            case svfLp: case svfBp: case svfHp: case svfNotch:
            {
                const auto R = Svf::damping (r);
                a.process (x, g, R);
                return mode == svfLp ? a.lp : mode == svfBp ? 2.0f * R * a.bp : mode == svfHp ? a.hp : x - 2.0f * R * a.bp;
            }
            case ms20Lp: return k35.process (x, g, r, false);
            case ms20Hp: return k35.process (x, g, r, true);
            case combPlus: return comb.process (x, fcHz, fs, r, false);
            case combMinus: return comb.process (x, fcHz, fs, r, true);
            default: // Dual: dos SVF a fc·2^(∓spread/2)
            {
                const auto R = Svf::damping (r);
                const auto half = 0.5f * spreadOct;
                const auto gLo = std::tan (juce::MathConstants<float>::pi * juce::jlimit (10.0f, 0.45f * fs, fcHz * std::exp2 (-half)) / fs);
                const auto gHi = std::tan (juce::MathConstants<float>::pi * juce::jlimit (10.0f, 0.45f * fs, fcHz * std::exp2 (half)) / fs);
                if (mode == dualBp) // en paralelo: dos picos (formantes)
                {
                    a.process (x, gLo, R);
                    b.process (x, gHi, R);
                    return 2.0f * R * (a.bp + b.bp);
                }
                a.process (x, gLo, R); // en serie: dos paso bajo de 12 dB con las esquinas separadas
                b.process (a.lp, gHi, R);
                return b.lp;
            }
        }
    }
};
} // namespace VoiceFilters
