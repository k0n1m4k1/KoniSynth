// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; solo usa la biblioteca estándar.
#pragma once

//==============================================================================
// Saturación por voz del motor Synth (entre el filtro y el VCA): cada nota satura por separado, así un acorde no se
// intermodula (lo que sí hace una saturación en la salida, como Tube). Antialiasing por antiderivada de primer orden
// (ADAA1, Parker et al., DAFx-16): y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]), a 2x con un filtro de media banda IIR
// polifásico (sin latencia que comunicar: retardo de grupo ~3,6 muestras a 48 kHz entre subir y bajar). En double: la
// diferencia de antiderivadas pierde precisión en float con ganancias altas.
namespace VoiceSat
{
enum Mode { soft = 0, diode, hard, fold };
constexpr double diodeBias = 0.4; // punto de trabajo desplazado: armónicos pares (tanh(x + b) - tanh(b))

inline double logCosh (double x) { const auto a = std::abs (x); return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531; }

// Curva y su antiderivada (F' = f, continua)
inline double curve (int m, double x)
{
    switch (m)
    {
        case diode: return std::tanh (x + diodeBias) - std::tanh (diodeBias);
        case hard:  return juce::jlimit (-1.0, 1.0, x);
        case fold:  return std::sin (x);
        default:    return std::tanh (x);
    }
}

inline double antiderivative (int m, double x)
{
    switch (m)
    {
        case diode: return logCosh (x + diodeBias) - x * std::tanh (diodeBias);
        case hard:  return std::abs (x) <= 1.0 ? 0.5 * x * x : std::abs (x) - 0.5;
        case fold:  return -std::cos (x);
        default:    return logCosh (x);
    }
}

// Compensación de nivel: una señal de pico 1 (un oscilador al 100 %) sale con el mismo pico a pico a cualquier drive
// (Soft, Diode, Hard). Fold no se compensa (su salida está acotada a ±1 y la ganancia llega a 0 en cada pliegue)
inline double makeup (int m, double g)
{
    if (m == fold) return 1.0;
    const auto c = 0.5 * (curve (m, g) - curve (m, -g)); // pico a pico / 2: Diode es asimétrica
    return c > 1.0e-6 ? 1.0 / c : 1.0;
}

// Media banda IIR polifásico: H(z) = 0,5 · (A0(z²) + z⁻¹ · A1(z²)), Ai = producto de pasatodos de 1.er orden; coeficientes
// por el método elíptico de hiir (L. de Soras), 8 coeficientes, transición 0,04: plano hasta 0,21·fs2 (20,16 kHz a 96 kHz),
// rechazo -99,3 dB desde 0,29·fs2 (diseño y respuesta verificados en Python: scratchpad/hb_design.py)
constexpr std::array<double, 8> halfbandCoefs { 0.04063346092419326, 0.1505051290226746, 0.30075705599187408, 0.46077450496145061,
                                                0.6095243148961883, 0.73850384111885725, 0.84922381039206607, 0.9497427837050002 };

// Cadena de 4 pasatodos y[n] = c·x[n] + x[n-1] - c·y[n-1] (la rama Ai(z²) vista a la frecuencia baja)
struct AllpassChain
{
    std::array<double, 4> x1 {}, y1 {};
    double process (double x, int first)
    {
        for (int k = 0; k < 4; ++k)
        {
            const auto c = halfbandCoefs[(size_t) (first + 2 * k)];
            const auto y = c * (x - y1[(size_t) k]) + x1[(size_t) k];
            x1[(size_t) k] = x; y1[(size_t) k] = y;
            x = y;
        }
        return x;
    }
};

// Interpolador 2x: x -> (A0(x), A1(x)) = muestras pares e impares a 2·fs
struct Upsampler2x
{
    AllpassChain a0, a1;
    std::pair<double, double> process (double x) { return { a0.process (x, 0), a1.process (x, 1) }; }
};

// Diezmador 2x: (par, impar) -> 0,5 · (A0(par) + A1(impar anterior))
struct Downsampler2x
{
    AllpassChain a0, a1;
    double oddPrev = 0.0;
    double process (double even, double odd)
    {
        const auto y = 0.5 * (a0.process (even, 0) + a1.process (oddPrev, 1));
        oddPrev = odd;
        return y;
    }
};

struct Channel
{
    double x1 = 0.0, f1 = 0.0;
    int mode = -1;
    float dcX = 0.0f, dcY = 0.0f;

    Upsampler2x up;
    Downsampler2x down;

    void reset (int m) { x1 = 0.0; f1 = antiderivative (m, 0.0); mode = m; dcX = dcY = 0.0f; up = {}; down = {}; }

    double adaa (int m, double x)
    {
        const auto f = antiderivative (m, x);
        const auto dx = x - x1;
        const auto y = std::abs (dx) > 1.0e-5 ? (f - f1) / dx : curve (m, 0.5 * (x + x1));
        x1 = x; f1 = f;
        return y;
    }

    // in: muestra de la voz; g: ganancia de entrada; out: compensación; wet: 0..1 (fundido de entrada en los primeros 3 dB)
    float process (int m, float in, double g, double out, float wet, float dcCoef)
    {
        if (m != mode) { f1 = antiderivative (m, x1); mode = m; } // cambio de curva: la antiderivada previa con la nueva
        const auto [even, odd] = up.process (g * (double) in);
        const auto yEven = adaa (m, even); // en orden: el estado del ADAA avanza par -> impar (el orden de evaluación de
        const auto yOdd = adaa (m, odd);   // los argumentos de una llamada no está definido en C++)
        const auto y = down.process (yEven, yOdd);
        auto s = (float) (y * out);
        if (m == diode) // la asimetría deja continua: bloqueador de un polo a 10 Hz
        {
            const auto yDc = s - dcX + dcCoef * dcY;
            dcX = s; dcY = yDc; s = yDc;
        }
        return in + wet * (s - in);
    }
};
} // namespace VoiceSat
