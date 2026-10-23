// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Remuestreador de una tasa fija de entrada (la del chip) a la del host: sinc enventanado (Blackman) de 16 taps con corte en
// 0,92·min(entrada, salida)/2 y 512 fases (sin interpolar: error de fase ≤ 1/1024 de muestra). Latencia: 8 muestras de
// entrada (0,15 ms a 53,3 kHz). Cada fase normalizada a ganancia 1 en continua.
struct SincResampler
{
    static constexpr int kTaps = 16, kPhases = 512;

    void prepare (double inRate, double outRate)
    {
        step = inRate / outRate;
        const auto wc = 0.92 * 0.5 * std::min (inRate, outRate) / inRate; // corte normalizado a la tasa de entrada
        for (int ph = 0; ph < kPhases; ++ph)
        {
            const auto frac = (double) ph / kPhases;
            double sum = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
                const auto x = (double) (k - kTaps / 2 + 1) - frac; // distancia a la muestra pedida
                const auto s = std::abs (x) < 1.0e-9 ? 2.0 * wc : std::sin (juce::MathConstants<double>::twoPi * wc * x) / (juce::MathConstants<double>::pi * x);
                const auto w = 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * x / (kTaps / 2)) + 0.08 * std::cos (juce::MathConstants<double>::twoPi * x / (kTaps / 2));
                table[(size_t) ph][(size_t) k] = (float) (s * w);
                sum += s * w;
            }
            for (auto& v : table[(size_t) ph])
                v = (float) (v / sum);
        }
        reset();
    }

    void reset()
    {
        history.fill (0.0f);
        historyR.fill (0.0f);
        position = 0.0;
        available = 0;
    }

    // Salida de una muestra; 'produce' genera muestras de entrada nuevas cuando hacen falta
    template <typename Produce>
    float next (Produce&& produce)
    {
        // Hace falta la entrada hasta floor(position) + kTaps/2
        while (available <= (long long) position + kTaps / 2)
        {
            history[(size_t) (available & kMask)] = produce();
            ++available;
        }
        const auto base = (long long) position;
        const auto& h = table[(size_t) ((position - (double) base) * kPhases)];
        lastBase = base;
        lastKernel = &h;
        float y = 0.0f;
        for (int k = 0; k < kTaps; ++k)
            y += h[(size_t) k] * history[(size_t) ((base + k - kTaps / 2 + 1) & kMask)];
        position += step;
        // Mantiene los índices pequeños: desplaza el origen
        if (position > 1.0e6)
        {
            const auto shift = (((long long) position - kTaps) / 64) * 64; // múltiplo del tamaño del búfer circular
            position -= (double) shift;
            available -= shift;
            lastBase -= shift;
        }
        return y;
    }

    // Versión estéreo: 'produce' devuelve el par (izquierda, derecha); mismo kernel y misma posición para los dos lados
    template <typename Produce>
    std::array<float, 2> nextStereo (Produce&& produce)
    {
        float right = 0.0f;
        const auto left = next ([&]
        {
            const auto s = produce();
            historyR[(size_t) (available & kMask)] = s[1];
            return s[0];
        });
        // Mismo origen y mismo kernel que acaba de usar next() para la izquierda
        for (int k = 0; k < kTaps; ++k)
            right += (*lastKernel)[(size_t) k] * historyR[(size_t) ((lastBase + k - kTaps / 2 + 1) & kMask)];
        return { left, right };
    }

private:
    static constexpr int kMask = 63;
    std::array<std::array<float, kTaps>, kPhases> table {};
    std::array<float, 64> history {}, historyR {};
    long long available = 0; // muestras de entrada producidas (índice de la siguiente)
    long long lastBase = 0;
    const std::array<float, kTaps>* lastKernel = nullptr;
    double position = 0.0, step = 1.0;
};
