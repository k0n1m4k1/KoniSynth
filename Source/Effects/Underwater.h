// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Media: imprime la mezcla en un soporte. Va al final de la cadena (antes de Gain).
// Especificaciones por soporte (origen en el comentario de cada fila):
//==============================================================================
// Media "Underwater": escuchar con la cabeza bajo el agua. Burbujas con la frecuencia de resonancia de Minnaert (1933):
// f·R ≈ 3,26 m/s (f ≈ 3,26 kHz / R en mm) para burbujas de aire en agua a presión atmosférica; radios de 0,5 a 5 mm (log-uniforme)
// -> 6,5 kHz a 650 Hz; cada burbuja es una senoide amortiguada con Q ≈ 10 que sube un poco de tono al ascender (Q y subida
// estimados; en la línea de los modelos de líquidos de van den Doel, 2005). Sin cifra única publicada para la sensación de
// "oído tapado" (conducción ósea, oído medio lleno de agua): paso bajo resonante cuyo corte baja con la profundidad (Age:
// 1500 -> 225 Hz, estimado). Oleaje (Wow): modulación lenta de tono (±15 cents) y amplitud a ~0,3 Hz (estimada).
struct Underwater
{
    // Aleatoriedad reproducible (Dsp/RandomClock.h): el procesador la resiembra en cada anclaje del transporte
    void reseed (juce::int64 seed) { rng.setSeed (seed); }

    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& l : lines) l.assign ((size_t) std::ceil (0.02 * sr) + 4, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& c : lp) for (auto& b : c) b.clear();
        bubbles = {};
        write = 0;
        wave = 0.0;
    }

    void process (juce::AudioBuffer<float>& buffer, float age, float noiseKnob, float wowKnob, float driveKnob)
    {
        const auto fc = 1500.0 * (1.0 - 0.85 * age);
        const auto numCh = std::min (2, buffer.getNumChannels());
        for (int c = 0; c < numCh; ++c)
        {
            lp[(size_t) c][0].set (1, fc, 1.2, sampleRate);
            lp[(size_t) c][1].set (1, fc * 1.6, 0.6, sampleRate);
        }
        const auto rate = 40.0 * noiseKnob;                       // burbujas por segundo
        const auto g = juce::Decibels::decibelsToGain (18.0f * driveKnob);
        const auto size = (int) lines[0].size();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // Nueva burbuja (proceso de Poisson)
            if (rng.nextDouble() < rate / sampleRate)
                for (auto& b : bubbles)
                    if (b.amp < 1.0e-4)
                    {
                        const auto radiusMm = 0.5 * std::pow (10.0, rng.nextDouble()); // 0,5..5 mm
                        b.hz = 3260.0 / radiusMm;
                        b.amp = 0.05 + 0.1 * rng.nextDouble();
                        b.decay = std::exp (-juce::MathConstants<double>::pi * b.hz / (10.0 * sampleRate)); // Q = 10
                        b.phase = 0.0;
                        b.pan = rng.nextFloat();
                        break;
                    }
            double bubbleL = 0.0, bubbleR = 0.0;
            for (auto& b : bubbles)
                if (b.amp >= 1.0e-4)
                {
                    b.phase += b.hz / sampleRate;
                    b.hz *= 1.0 + 2.0 / sampleRate;                     // sube de tono al ascender (estimado: +2 %/s)
                    const auto y = b.amp * std::sin (juce::MathConstants<double>::twoPi * b.phase);
                    bubbleL += y * (1.0 - b.pan);
                    bubbleR += y * b.pan;
                    b.amp *= b.decay;
                }
            // Oleaje: tono por retardo modulado y amplitud
            wave += 0.3 / sampleRate;
            if (wave >= 1.0) wave -= 1.0;
            const auto sway = std::sin (juce::MathConstants<double>::twoPi * wave);
            // ±15 cents a 0,3 Hz con Wow 100 %: amplitud del retardo = Δf/f / (2π·f_mod) = 0,0087/(2π·0,3) ≈ 4,6 ms
            const auto delay = (0.006 + 0.0046 * wowKnob * sway) * sampleRate;
            const auto gain = 1.0 - 0.15 * wowKnob * (0.5 + 0.5 * sway);
            for (int c = 0; c < numCh; ++c)
            {
                auto& l = lines[(size_t) c];
                l[(size_t) write] = buffer.getSample (c, i);
                auto pos = write - delay;
                while (pos < 0.0) pos += size;
                const auto i0 = (int) pos;
                const auto f = (float) (pos - i0);
                const auto x = l[(size_t) (i0 % size)] + f * (l[(size_t) ((i0 + 1) % size)] - l[(size_t) (i0 % size)]);
                auto y = lp[(size_t) c][1].process (lp[(size_t) c][0].process (x)) * gain;
                y += c == 0 ? bubbleL : bubbleR;
                buffer.setSample (c, i, std::tanh (0.3f * g * (float) y) / (0.3f * g)); // saturación con ganancia 1 en pequeña señal
            }
            write = (write + 1) % size;
        }
    }

private:
    struct Bubble { double hz = 1000.0, amp = 0.0, decay = 0.0, phase = 0.0; float pan = 0.5f; };
    double sampleRate = 44100.0, wave = 0.0;
    std::array<std::vector<float>, 2> lines;
    std::array<std::array<Biquad, 2>, 2> lp;
    std::array<Bubble, 16> bubbles {};
    int write = 0;
    juce::Random rng;
};
