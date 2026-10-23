// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Chorus / ensemble estéreo (tras la válvula). Entrada: suma mono; salida: cos(Mix·π/2)·seco + sin(Mix·π/2)·húmedo.
//  Chorus I / II / I+II: según la medición publicada del chorus del Juno-60 (github.com/pendragon-andyh/Juno60):
//    un LFO triangular; retardo 1,66–5,35 ms (I y II) o 3,3–3,7 ms (I+II); 0,513 / 0,863 / 9,75 Hz;
//    canal derecho con la modulación invertida (180°); I+II sale mono. Antes del BBD hay un pasa-bajos de 12 dB
//    cuya frecuencia no se publica: 10 kHz estimado.
//  Ensemble (tipo string machine, Solina): 3 líneas moduladas por un LFO lento de 0,6 Hz y uno rápido de 6 Hz,
//    trifásicos (0/120/240°), según descripciones en foros. La profundidad no está publicada ("shallow"):
//    ±1,5 ms lento y ±0,1 ms rápido sobre 6 ms, estimado (desviación de tono ≈ 2π·f·A: 0,57 % y 0,38 %).
//    Estéreo: L = línea 1 + ½ línea 2, R = línea 3 + ½ línea 2 (el original es mono).
//  Depth escala la excursión: 100 % = la medida / estimada.
struct Chorus
{
    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        lines.prepare ({ sr, (juce::uint32) maxBlock, 3 });
        lines.setMaximumDelayInSamples ((int) std::ceil (0.010 * sr) + 4); // 10 ms > 7,6 ms (máximo del ensemble)
        lpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 10000.0f / (float) sr);
        reset();
    }

    void reset()
    {
        lines.reset();
        lfoPhase = slowPhase = fastPhase = 0.0f;
        lp1 = lp2 = 0.0f;
    }

    void process (juce::AudioBuffer<float>& buffer, int mode, float depth, float mix)
    {
        setMix (mix);
        constexpr auto twoPi = juce::MathConstants<float>::twoPi;
        const auto sr = (float) sampleRate;
        const auto numChannels = buffer.getNumChannels();

        struct JunoMode { float rate, minDelay, maxDelay; bool mono; };
        static constexpr JunoMode junoModes[] = { { 0.513f, 0.00166f, 0.00535f, false },
                                                  { 0.863f, 0.00166f, 0.00535f, false },
                                                  { 9.75f,  0.0033f,  0.0037f,  true } };

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float in = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                in += buffer.getSample (ch, i);
            in /= (float) numChannels;

            lp1 += lpCoef * (in - lp1); // 2 polos de 1er orden en cascada = 12 dB/oct
            lp2 += lpCoef * (lp1 - lp2);
            for (int l = 0; l < 3; ++l)
                lines.pushSample (l, lp2);

            float wetL, wetR;
            if (mode < 3)
            {
                const auto& m = junoModes[mode];
                lfoPhase += m.rate / sr;
                if (lfoPhase >= 1.0f)
                    lfoPhase -= 1.0f;

                const auto tri    = 1.0f - 4.0f * std::abs (lfoPhase - 0.5f); // -1..1
                const auto centre = 0.5f * (m.minDelay + m.maxDelay);
                const auto half   = 0.5f * (m.maxDelay - m.minDelay) * depth;
                const auto dL     = (centre + half * tri) * sr;
                const auto dR     = m.mono ? dL : (centre - half * tri) * sr;

                wetL = lines.popSample (0, dL);
                wetR = lines.popSample (1, dR);
                lines.popSample (2, dL); // mantiene sincronizado el puntero de lectura de la línea 3
            }
            else
            {
                slowPhase += 0.6f / sr;
                fastPhase += 6.0f / sr;
                if (slowPhase >= 1.0f) slowPhase -= 1.0f;
                if (fastPhase >= 1.0f) fastPhase -= 1.0f;

                float v[3];
                for (int l = 0; l < 3; ++l)
                {
                    const auto offset = (float) l / 3.0f; // 0°, 120°, 240°
                    const auto mod = 0.0015f * std::sin (twoPi * (slowPhase + offset))
                                   + 0.0001f * std::sin (twoPi * (fastPhase + offset));
                    v[l] = lines.popSample (l, (0.006f + depth * mod) * sr);
                }
                wetL = (v[0] + 0.5f * v[1]) / 1.5f;
                wetR = (v[2] + 0.5f * v[1]) / 1.5f;
            }

            if (numChannels >= 2)
            {
                buffer.setSample (0, i, dryGain * buffer.getSample (0, i) + wetGain * wetL);
                buffer.setSample (1, i, dryGain * buffer.getSample (1, i) + wetGain * wetR);
            }
            else
            {
                buffer.setSample (0, i, dryGain * buffer.getSample (0, i) + wetGain * 0.5f * (wetL + wetR));
            }
        }
    }

    // Mezcla a potencia constante: seco y húmedo están poco correlados (retardo modulado), se suman en potencia.
    // Medido en réplica numérica (sierra 220 Hz, Mix 50 %): fundido lineal -2,9 dB / mono -4,2 dB (Chorus I);
    // potencia constante +0,1 dB / mono -1,2 dB. El mono importa: juce::Reverb suma L+R a su entrada.
    void setMix (float mix)
    {
        dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);
    }

    float dryGain = 1.0f, wetGain = 0.0f;

    double sampleRate = 44100.0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> lines;
    float lfoPhase = 0.0f, slowPhase = 0.0f, fastPhase = 0.0f, lp1 = 0.0f, lp2 = 0.0f, lpCoef = 1.0f;
};
