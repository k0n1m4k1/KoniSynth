// Samples propios de KoniSynth para los motores de samples (SNES, N64): bucles con un número entero de ciclos, generados a la
// tasa de la consola. N muestras con M ciclos: raíz = tasa·M/N. PCM de 16 bits a ±12000 (margen para los códecs).
// Se incluye desde KoniSynth.cpp (mismo TU). A 32 kHz da exactamente los mismos valores que la versión original del motor SNES.
#pragma once

struct WaveSource
{
    std::vector<int> pcm;
    double rootHz = 1000.0; // frecuencia del bucle reproducido a su tasa
};

static constexpr int kNumWaveSources = 6; // Square, Saw, Sine, Strings, Choir, Bass

static WaveSource makeWaveSource (int index, double rate)
{
    const auto twoPi = juce::MathConstants<double>::twoPi;
    const auto limit = std::min (15000.0, 0.47 * rate); // armónicos por debajo de 15 kHz y de Nyquist
    const auto simple = [&] (int n, auto&& wave) // forma de onda de un ciclo
    {
        WaveSource s;
        s.pcm.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            s.pcm[(size_t) i] = (int) std::lround (12000.0 * wave ((double) i / n));
        s.rootHz = rate / n;
        return s;
    };
    // Conjunto de 3 voces de banda limitada (71, 72, 73 ciclos en 4400 muestras a 32 kHz: ±24 ct)
    const auto ensemble = [&] (auto&& harmonicGain)
    {
        const auto n = rate == 32000.0 ? 4400 : (int) std::lround (4400.0 * rate / 32000.0);
        std::vector<double> acc ((size_t) n, 0.0);
        for (const auto cycles : { 71, 72, 73 })
        {
            const auto f0 = rate * cycles / n;
            for (int h = 1; h * f0 < limit; ++h)
            {
                const auto g = harmonicGain (h, h * f0) / 3.0;
                const auto ph = 0.37 * h * cycles; // fases fijas distintas por voz
                for (int i = 0; i < n; ++i)
                    acc[(size_t) i] += g * std::sin (twoPi * h * cycles * i / n + ph);
            }
        }
        // Pico = máximo valor absoluto (el extremo puede ser negativo)
        const auto peak = std::max (1.0e-9, std::abs (*std::max_element (acc.begin(), acc.end(), [] (double a, double b) { return std::abs (a) < std::abs (b); })));
        WaveSource s;
        s.pcm.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            s.pcm[(size_t) i] = (int) std::lround (12000.0 * acc[(size_t) i] / std::abs (peak));
        s.rootHz = rate * 72 / n;
        return s;
    };
    switch (index)
    {
        case 0: return simple (32, [] (double x) { return x < 0.5 ? 1.0 : -1.0; });          // cuadrada de 32 muestras
        case 1: return simple (32, [] (double x) { return 1.0 - 2.0 * x; });                 // sierra de 32 muestras
        case 2: return simple (32, [twoPi] (double x) { return std::sin (twoPi * x); });    // seno de 32 muestras
        // Cuerdas: sierras con caída por encima de 4 kHz
        case 3: return ensemble ([] (int h, double f) { return 1.0 / h / (1.0 + std::pow (f / 4000.0, 2.0)); });
        // Coro "aah": formantes fijos 700 / 1220 / 2600 Hz (al transponer se desplazan, como en las consolas)
        case 4: return ensemble ([] (int h, double f)
        {
            const auto bump = [f] (double fc, double bw) { return std::exp (-0.5 * std::pow ((f - fc) / bw, 2.0)); };
            return (bump (700.0, 130.0) + 0.6 * bump (1220.0, 160.0) + 0.25 * bump (2600.0, 250.0) + 0.02) / std::sqrt ((double) h);
        });
        default: // Bajo: sierra de banda limitada de 64 muestras con caída por encima de 2 kHz
        {
            const auto root = rate / 64.0;
            std::vector<double> acc (64, 0.0);
            for (int h = 1; h * root < limit; ++h)
                for (int i = 0; i < 64; ++i)
                    acc[(size_t) i] += std::sin (twoPi * h * i / 64.0) / h / (1.0 + std::pow (h * root / 2000.0, 2.0));
            const auto peak = std::abs (*std::max_element (acc.begin(), acc.end(), [] (double a, double b) { return std::abs (a) < std::abs (b); }));
            WaveSource s;
            s.pcm.resize (64);
            for (int i = 0; i < 64; ++i)
                s.pcm[(size_t) i] = (int) std::lround (12000.0 * acc[(size_t) i] / peak);
            s.rootHz = root;
            return s;
        }
    }
}
