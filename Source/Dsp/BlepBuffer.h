// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Escalones de banda limitada (estilo Blip_Buffer de blargg): cada cambio de nivel del chip, en su instante exacto con
// fracción de muestra, suma un impulso sinc con ventana de Kaiser (32 coeficientes, β 9,5, 1024 subfases interpoladas)
// a un buffer de deltas; la salida es su integral. Medido en réplica (scratchpad/blep_exact.py, sin ventana de análisis):
// alias de una cuadrada ≤ -92 dB hasta 10 kHz a 48 kHz (muestreo directo: -9,5 a -36 dB); -0,13 dB a 20 kHz.
// Latencia: 15,5 muestras (0,32 ms).
struct BlepBuffer
{
    static constexpr int kTaps = 32, kPhases = 1024;

    void prepare (int maxBlock)
    {
        deltas.assign ((size_t) (maxBlock + kTaps + 1), 0.0);
        integrator = 0.0;
    }

    void reset()
    {
        std::fill (deltas.begin(), deltas.end(), 0.0);
        integrator = 0.0;
    }

    // Cambio de nivel 'delta' en el instante t (muestras desde el inicio del bloque actual, 0 <= t < tamaño del bloque)
    void addDelta (double t, double delta)
    {
        const auto i = (int) t;
        const auto pf = (t - i) * kPhases;
        const auto p = std::min ((int) pf, kPhases - 1);
        const auto a = pf - p;
        const auto& k = kernel();
        const auto* k0 = &k[(size_t) p * kTaps];
        const auto* k1 = k0 + kTaps;
        auto* d = &deltas[(size_t) i];
        for (int j = 0; j < kTaps; ++j)
            d[j] += delta * (k0[j] + a * (k1[j] - k0[j]));
    }

    // Integra n muestras y desplaza el buffer
    void read (int n, float* out)
    {
        for (int i = 0; i < n; ++i)
        {
            integrator += deltas[(size_t) i];
            out[i] = (float) integrator;
        }
        std::move (deltas.begin() + n, deltas.begin() + n + kTaps, deltas.begin());
        std::fill (deltas.begin() + kTaps, deltas.begin() + n + kTaps, 0.0);
    }

private:
    // Tabla (kPhases + 1) × kTaps: sinc a Nyquist con ventana de Kaiser evaluada en la posición continua; cada fase suma 1
    static const std::vector<double>& kernel()
    {
        static const auto table = []
        {
            std::vector<double> t ((size_t) (kPhases + 1) * kTaps);
            const auto beta = 9.5;
            const auto i0 = [] (double x) // Bessel modificada de orden 0 (serie)
            {
                double s = 1.0, term = 1.0;
                for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); s += term; }
                return s;
            };
            for (int p = 0; p <= kPhases; ++p)
            {
                const auto frac = (double) p / kPhases;
                double sum = 0.0;
                for (int j = 0; j < kTaps; ++j)
                {
                    const auto n = j - kTaps / 2 + 1 - frac;                      // posición respecto al escalón
                    const auto x = juce::MathConstants<double>::pi * n;
                    const auto sinc = std::abs (n) < 1.0e-12 ? 1.0 : std::sin (x) / x;
                    const auto r = (n + kTaps / 2) / (kTaps / 2) - 1.0;           // -1..1 a lo largo de la ventana
                    const auto w = std::abs (r) <= 1.0 ? i0 (beta * std::sqrt (1.0 - r * r)) / i0 (beta) : 0.0;
                    t[(size_t) p * kTaps + (size_t) j] = sinc * w;
                    sum += sinc * w;
                }
                for (int j = 0; j < kTaps; ++j)
                    t[(size_t) p * kTaps + (size_t) j] /= sum;
            }
            return t;
        }();
        return table;
    }

    std::vector<double> deltas;
    double integrator = 0.0;
};

//==============================================================================
// FM de 6 operadores tipo Yamaha DX7 (1983). Implementación propia en coma flotante de las fórmulas de msfa
// (Google 2012 / Pascal Gauthier, Apache 2.0; github.com/asb2m10/dexed, source/msfa: env.cc, dx7note.cc, fm_core.cc, lfo.cc):
//  - nivel en unidades de 2^-8 octava (0,0235 dB); amplitud = 2^(nivel/256 - 14) ciclos de desviación (nivel máximo: 2 ciclos = 4π rad);
//  - EG: qrate = R·41/64 + rate scaling; incremento por muestra (4 + qrate%4)·2^(qrate/4)·2^-14·44100/fs; decay lineal en dB;
//    ataque con salto a 1716 y paso ∝ (17 - octavas); objetivo (scaleout(L)/2)·64 + outlevel - 4256, mínimo 16;
//  - output level: scaleoutlevel (tabla levellut < 20, 28 + OL después) · 32; velocidad: tabla velocity_data y sensibilidad 0..7;
//  - level scaling (4 curvas, punto de ruptura) y rate scaling de dx7note.cc; detune empírico de Dexed; fixed = 10^(c%4 + fine/100) Hz;
//  - feedback: media de las 2 últimas salidas · 2^(fb - 8); LFO: tabla de 100 velocidades medidas, retardo de dos fases, PMD/PMS.
// Algoritmos: tabla de banderas de fm_core.cc (bus de entrada/salida por operador, de OP6 a OP1). En los algoritmos 4 y 6 la
// realimentación es un bucle (OP4 -> OP6 y OP5 -> OP6), como en el DX7; msfa lo simplifica a autorrealimentación de OP6.
// No incluido: pitch EG, AMS con la curva exacta (msfa la marca como provisional: aquí atenuación lineal en dB, estimada), portamento.
// Modo DX7 (por defecto): la aritmética del chip OPS según Ken Shirriff (righto.com, partes II-V, 2021-22), contrastada con
// el wiki de msfa (R. Levien) y Dexed Mark I (solo como referencia: GPLv3, no copiado):
//  - fase de 12 bits por ciclo (2 de cuadrante + 10 de índice); ROM log-sin de 1/4 de onda, 1024 entradas:
//    round(-1024·log2(sin((n+0,5)/1024·π/2))); la envolvente se suma en ese dominio (1/1024 de octava);
//  - ROM exp de 1024 entradas, mantisa de 12 bits y desplazamiento: magnitud de 14 bits por operador; suma entera;
//  - frecuencia interna 49 096 Hz; DAC BA9221 de 12 bits con escalador ÷1/2/4/8 (signo + 11 bits + exponente de 2 bits;
//    escala según los ceros iniciales: 5 -> ÷8, 4 -> ÷4, 3 -> ÷2); salida retenida (ZOH), aquí reconstruida con BlepBuffer;
//  - filtro de salida Sallen-Key: 14 kHz (foro de Cantabile) o 16 kHz (Shirriff, "I believe"); orden no publicado:
//    2.º orden a 15 kHz (supuesto).
//  - compensación de nivel (COM) por número de portadoras: documentada por Shirriff pero sin tabla; aquí log2(portadoras)
//    octavas (estimado). Modo Clean: el seno en coma flotante de antes.
