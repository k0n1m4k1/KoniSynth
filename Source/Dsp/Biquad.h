// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Filtro biquad RBJ (forma directa II transpuesta). Lo usan el vocoder y las cajas de ritmos.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

    double process (double x)
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // RBJ cookbook: 0 pasabanda (ganancia 0 dB en el pico), 1 pasa-bajos, 2 pasa-altos. Conserva el estado.
    void set (int type, double f, double q, double sr)
    {
        const auto w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, 0.45 * sr, f) / sr;
        const auto c = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        if (type == 0)      { b0 = alpha; b1 = 0.0; b2 = -alpha; }
        else if (type == 1) { b0 = b2 = 0.5 * (1.0 - c); b1 = 1.0 - c; }
        else                { b0 = b2 = 0.5 * (1.0 + c); b1 = -(1.0 + c); }
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 = -2.0 * c / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void clear() { z1 = z2 = 0.0; }
};
