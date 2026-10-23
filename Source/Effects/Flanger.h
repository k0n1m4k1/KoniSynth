// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Flanger de BBD tipo Electro-Harmonix Electric Mistress: SAD1024 como 2×512 etapas (Metzger + DAFx-16), retardo = 512/(2·f);
// el periodo del reloj es proporcional a la tensión del LFO -> retardo lineal con el LFO (Metzger + PedalPCB). Retardo medido:
// 0,8-6 ms en flange y 1-10 ms en Filter Matrix (medida en un Deluxe v2, Fractal; contradice los 7,3 ms calculados con el reloj
// mínimo: se usa la medida). LFO triangular de 0,07 a 10 Hz, con las crestas redondeadas. Filter Matrix: LFO parado, Range fija
// el retardo (filtro peine estático). Color = realimentación (polaridad no verificada: positiva). La salida directa va a 180°
// de la flangeada (Fractal; una sola fuente). Filtros del BBD: paso bajo de 2 polos a 17 kHz (una sola fuente). El ruido y el
// aliasing del muestreo del BBD no se modelan.
struct BbdFlanger
{
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& l : lines) l.assign ((size_t) std::ceil (0.012 * sr) + 4, 0.0f);
        for (auto& b : lp) b.set (1, 17000.0, 0.70710678, sr);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& b : lp) b.clear();
        fb.fill (0.0f);
        write = 0;
        lfoPhase = 0.0;
    }

    void process (juce::AudioBuffer<float>& buffer, float rateHz, float range, float color, bool matrix, float mix)
    {
        const auto numCh = std::min (2, buffer.getNumChannels());
        const auto size = (int) lines[0].size();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            lfoPhase += rateHz / sampleRate;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            for (int c = 0; c < numCh; ++c)
            {
                // Triángulo con crestas redondeadas ("hipertriangular"); el canal derecho desfasado 90°
                auto ph = lfoPhase + 0.25 * c;
                ph -= std::floor (ph);
                const auto tri = ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;
                const auto lfo = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::halfPi * tri); // 0..1
                const auto delayMs = matrix ? 1.0 + 9.0 * range : 0.8 + (0.8 + 5.2 * range - 0.8) * lfo;
                const auto x = buffer.getSample (c, i);
                auto& line = lines[(size_t) c];
                line[(size_t) write] = x + color * fb[(size_t) c];
                auto pos = write - delayMs * 0.001 * sampleRate;
                while (pos < 0.0) pos += size;
                const auto i0 = (int) pos;
                const auto f = (float) (pos - i0);
                const auto a = line[(size_t) (i0 % size)], b = line[(size_t) ((i0 + 1) % size)];
                const auto wet = (float) lp[(size_t) c].process (a + f * (b - a));
                fb[(size_t) c] = wet;
                const auto out = 0.5f * (wet - x); // directa a 180° de la flangeada
                buffer.setSample (c, i, x + mix * (out - x));
            }
            write = (write + 1) % size;
        }
    }

private:
    double sampleRate = 44100.0, lfoPhase = 0.0;
    std::array<std::vector<float>, 2> lines;
    std::array<Biquad, 2> lp;
    std::array<float, 2> fb {};
    int write = 0;
};
