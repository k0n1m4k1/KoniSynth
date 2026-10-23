// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Reverb de placa: topología de J. Dattorro, "Effect Design Part 1", J. AES 45(9), 1997, Fig. 1 y Tablas 1–2
// ("plate-class reverberation topology in the style of Griesinger"). Longitudes publicadas a 29 761 Hz, escaladas a fs.
//  Entrada: suma mono -> predelay -> pasa-bajos "bandwidth" (Tone) -> 4 difusores pasa-todo (142, 107 | 379, 277;
//  coef. 0,750 | 0,625 × Diffusion) -> tanque en ocho:
//   L: pasa-todo modulado 672 (+excursión; coef. -0,70) -> 4453 -> damping -> ×decay -> pasa-todo 1800 (dd2) -> 3720 -> R
//   R: pasa-todo modulado 908 (+excursión; coef. -0,70) -> 4217 -> damping -> ×decay -> pasa-todo 2656 (dd2) -> 3163 -> L
//   dd2 = decay + 0,15 en [0,25, 0,50]. Modulación: excursión 16 muestras (×Mod), ~1 Hz, LFO en cuadratura (L sin, R cos).
//  Salida: 7 tomas por canal (Tabla 2), todo húmedo, ×0,6. Interpolación lineal en las lecturas fraccionarias
//  (el artículo prefiere pasa-todo: la lineal añade un leve filtrado variable).
//  Decay en segundos: un ciclo completo del ocho recorre 21 589 muestras @ 29 761 Hz = 0,7254 s × Size y aplica decay⁴
//  -> RT60 = 60 / (-80·log10 decay) · T  ->  decay = 10^(-0,75·T / RT60).
//  Calibración (réplica scratchpad/rt60_cal3.py, RT60 en la octava de 1 kHz, T20×3): Decay = RT60 a 1 kHz.
//   · decay ×|H_damp(1 kHz)|^(-1/2): compensa los 2 pasa-bajos de damping por vuelta (sin ello: -9 % con Damp 3 kHz).
//   · RT60 interno = Decay - 0,1 s: la difusión añadía +0,10..0,15 s casi constantes (+10 % con Decay 1 s).
//   · Colas cortas: los pasa-todo del tanque (dd1 0,70 en ~23 ms) tenían cola propia de ~0,5 s (Decay 0,3 s medía
//     0,55 s); sus coeficientes se limitan a g <= 10^(-3·T_ap/RT60), que solo actúa por debajo de ~0,6 s.
//   Resultado: error de -7..+2 % de 0,3 a 6 s (+12 % en el mínimo de 0,2 s); antes -9..+82 %.
//  Freeze: decay = 1, sin damping, sin entrada (el artículo: "the sound will remain held in the tank indefinitely").
struct PlateReverb
{
    static constexpr double refRate = 29761.0;
    static constexpr double maxSize = 1.5;

    struct Line // retardo circular; read(d): muestra escrita hace d muestras (d = 0: la última)
    {
        void init (int maxDelay)
        {
            int size = 1;
            while (size < maxDelay + 4)
                size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }
        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }
        void push (float x) { buffer[(size_t) write] = x; write = (write + 1) & mask; }
        float read (float d) const // lineal: solo mientras Size se desliza
        {
            const auto i = (int) d;
            const auto f = d - (float) i;
            const auto a = buffer[(size_t) ((write - 1 - i) & mask)];
            const auto b = buffer[(size_t) ((write - 2 - i) & mask)];
            return a + f * (b - a);
        }
        float readInt (int d) const { return buffer[(size_t) ((write - 1 - d) & mask)]; }
        float readHermite (float d) const // 4 puntos, 3er orden: mucha menos pérdida de agudos que la lineal
        {
            const auto i = (int) d;
            const auto f = d - (float) i;
            const auto xm1 = buffer[(size_t) ((write - i) & mask)];
            const auto x0  = buffer[(size_t) ((write - 1 - i) & mask)];
            const auto x1  = buffer[(size_t) ((write - 2 - i) & mask)];
            const auto x2  = buffer[(size_t) ((write - 3 - i) & mask)];
            const auto c1 = 0.5f * (x1 - xm1);
            const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }
        std::vector<float> buffer;
        int mask = 0, write = 0;
    };

    // Lectura de retardo. Con longitudes fijas, posición entera (redondeada): la interpolación lineal en un bucle
    // realimentado filtra en cada vuelta (medido en réplica: con Freeze la energía caía de 0,209 a 0,127 en 7 s).
    // Fraccionaria solo mientras Size se desliza; Hermite para el retardo modulado.
    enum class Read { integer, linear, hermite };

    static float readAt (const Line& line, float d, Read mode)
    {
        switch (mode)
        {
            case Read::integer: return line.readInt ((int) (d + 0.5f));
            case Read::hermite: return line.readHermite (d);
            default:            return line.read (d);
        }
    }

    // Pasa-todo en celosía de Dattorro. sign = +1: v = x - g·d, y = d + g·v (difusores de entrada y dd2);
    // sign = -1: v = x + g·d, y = d - g·v ("note sign", dd1). Retardo de 'length' muestras.
    static float allpass (Line& line, float x, float g, float length, float sign, Read mode = Read::integer)
    {
        const auto d = readAt (line, length - 1.0f, mode);
        const auto v = x - sign * g * d;
        line.push (v);
        return d + sign * g * v;
    }

    static float delay (Line& line, float x, float length, Read mode = Read::integer)
    {
        const auto d = readAt (line, length - 1.0f, mode);
        line.push (x);
        return d;
    }

    void prepare (double sr)
    {
        sampleRate = sr;
        scale = sr / refRate;
        const auto maxLen = [this] (double n) { return (int) std::ceil (n * scale * maxSize) + 64; };
        predelay.init ((int) std::ceil (0.25 * sr) + 4);
        for (int i = 0; i < 4; ++i)
            inputAp[(size_t) i].init (maxLen (inputLengths[i]));
        apMod[0].init (maxLen (672 + 4 * 16)); apMod[1].init (maxLen (908 + 4 * 16)); // Mod hasta 200 %: +2·32
        del1[0].init (maxLen (4453));          del1[1].init (maxLen (4217));
        ap2[0].init (maxLen (1800));           ap2[1].init (maxLen (2656));
        del2[0].init (maxLen (3720));          del2[1].init (maxLen (3163));
        sizeSmoothed.reset (sr, 0.5); // 0,5 s: cambiar Size reescala retardos -> desafinación breve, sin clics
        reset();
    }

    void reset()
    {
        predelay.clear();
        for (auto& l : inputAp) l.clear();
        for (auto* group : { &apMod, &del1, &ap2, &del2 })
            for (auto& l : *group) l.clear();
        bandLp = 0.0f;
        damp = { 0.0f, 0.0f };
        tankOut = { 0.0f, 0.0f };
        lfoPhase = 0.0;
        firstBlock = true;
    }

    void process (juce::AudioBuffer<float>& buffer, float sizeKnob, float rt60, float predelayMs, float diffusion,
                  float dampHz, float toneHz, float modDepth, float width, bool freeze, float mix)
    {
        const auto sr = (float) sampleRate;
        sizeSmoothed.setTargetValue (0.5f + sizeKnob); // Size 0..1 -> 0,5..1,5 × longitudes publicadas

        const auto loopSeconds = 21589.0 / refRate;
        const auto bandwidth = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * toneHz / sr);
        const auto damping = freeze ? 0.0f : std::exp (-juce::MathConstants<float>::twoPi * dampHz / sr);
        const auto rtInternal = juce::jmax (0.05, (double) rt60 - 0.1);
        // |H| del pasa-bajos de damping a 1 kHz: (1 - d) / |1 - d·e^(-jω)|
        const auto w1k = juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
        const auto dampMag = (1.0 - damping) / std::sqrt (1.0 - 2.0 * damping * std::cos (w1k) + (double) damping * damping);
        const auto dampComp = (float) std::pow (dampMag, -0.5);
        // Límite de los pasa-todo del tanque (cola propia <= RT60 pedido), por bloque con el Size objetivo
        const auto targetSize = 0.5 + (double) sizeKnob;
        auto limitFor = [&] (double lengthAtRef) { return (float) std::pow (10.0, -3.0 * lengthAtRef * targetSize / refRate / rtInternal); };
        const float limitDd1[2] = { limitFor (672.0), limitFor (908.0) }, limitDd2[2] = { limitFor (1800.0), limitFor (2656.0) };
        const auto inputGain = freeze ? 0.0f : 1.0f;
        const auto id1 = 0.750f * diffusion, id2 = 0.625f * diffusion;
        // Con Freeze no hay modulación: el retardo modulado se lee entero y el bucle queda sin pérdidas
        // (pasa-todos y retardos de ganancia 1, damping 0). Con Hermite seguía perdiendo 2,3 dB en 7 s (réplica).
        const auto excursion = freeze ? 0.0f : (float) (16.0 * scale) * modDepth;
        const auto modRead = freeze ? Read::integer : Read::hermite;
        const auto lfoInc = 1.0 / sampleRate; // 1 Hz
        const auto predelaySamples = juce::jlimit (1.0f, 0.25f * sr, predelayMs * 0.001f * sr);
        const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);
        const auto numChannels = buffer.getNumChannels();

        if (firstBlock) // tras reset: sin barrido desde el valor inicial del suavizado
        {
            sizeSmoothed.setCurrentAndTargetValue (0.5f + sizeKnob);
            firstBlock = false;
        }

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto sliding = sizeSmoothed.isSmoothing();
            const auto size = sizeSmoothed.getNextValue();
            const auto k = (float) scale * size;
            const auto tankRead = sliding ? Read::linear : Read::integer;
            const auto decay = freeze ? 1.0f : juce::jmin (0.9999f, (float) std::pow (10.0, -0.75 * loopSeconds * size / rtInternal) * dampComp);
            const auto dd2 = juce::jlimit (0.25f, 0.50f, decay + 0.15f);
            auto apLimit = [freeze] (float g, float limit) { return freeze ? g : juce::jmin (g, limit); };

            float in = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                in += buffer.getSample (ch, i);
            in *= inputGain / (float) numChannels;

            // Entrada
            auto x = delay (predelay, in, predelaySamples);
            bandLp += bandwidth * (x - bandLp);
            x = bandLp;
            x = allpass (inputAp[0], x, id1, (float) (142 * scale), 1.0f); // los difusores de entrada no escalan con Size
            x = allpass (inputAp[1], x, id1, (float) (107 * scale), 1.0f);
            x = allpass (inputAp[2], x, id2, (float) (379 * scale), 1.0f);
            x = allpass (inputAp[3], x, id2, (float) (277 * scale), 1.0f);

            // Tanque
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            const auto modL = excursion * (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase);
            const auto modR = excursion * (float) std::cos (juce::MathConstants<double>::twoPi * lfoPhase);

            const std::array<float, 2> feed { x + decay * tankOut[1], x + decay * tankOut[0] }; // ocho: cada lado recibe al otro
            static constexpr float modBase[2] = { 672.0f, 908.0f }, len1[2] = { 4453.0f, 4217.0f },
                                   lenAp2[2] = { 1800.0f, 2656.0f }, len2[2] = { 3720.0f, 3163.0f };
            const float mods[2] = { modL, modR };

            for (size_t s = 0; s < 2; ++s)
            {
                // Longitud 672/908 + excursión ± excursión (rango [base, base + 2·exc]); dd1 = 0,70 (Tabla 1)
                auto v = allpass (apMod[s], feed[s], apLimit (0.70f, limitDd1[s]), modBase[s] * k + excursion + mods[s], -1.0f, modRead);
                v = delay (del1[s], v, len1[s] * k, tankRead);
                damp[s] = (1.0f - damping) * v + damping * damp[s];
                v = damp[s] * decay;
                v = allpass (ap2[s], v, apLimit (dd2, limitDd2[s]), lenAp2[s] * k, 1.0f, tankRead);
                tankOut[s] = delay (del2[s], v, len2[s] * k, tankRead);
            }

            // Tomas de salida (Tabla 2): nodeA_B[n] = muestra n del retardo entre los nodos A y B
            // node24_30 = del1[0] (4453), node31_33 = ap2[0] (1800), node33_39 = del2[0] (3720),
            // node48_54 = del1[1] (4217), node55_59 = ap2[1] (2656), node59_63 = del2[1] (3163)
            auto tap = [k, tankRead] (const Line& l, float n) { return readAt (l, n * k, tankRead); };
            auto wetL = 0.6f * (  tap (del1[1], 266) + tap (del1[1], 2974) - tap (ap2[1], 1913) + tap (del2[1], 1996)
                                - tap (del1[0], 1990) - tap (ap2[0], 187)  - tap (del2[0], 1066));
            auto wetR = 0.6f * (  tap (del1[0], 353) + tap (del1[0], 3627) - tap (ap2[0], 1228) + tap (del2[0], 2673)
                                - tap (del1[1], 2111) - tap (ap2[1], 335)  - tap (del2[1], 121));

            // Anchura (M/S del húmedo)
            const auto mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * width;
            wetL = mid + side;
            wetR = mid - side;

            if (numChannels >= 2)
            {
                buffer.setSample (0, i, dryGain * buffer.getSample (0, i) + wetGain * wetL);
                buffer.setSample (1, i, dryGain * buffer.getSample (1, i) + wetGain * wetR);
            }
            else
            {
                buffer.setSample (0, i, dryGain * buffer.getSample (0, i) + wetGain * mid);
            }
        }
    }

    static constexpr double inputLengths[4] = { 142, 107, 379, 277 };
    double sampleRate = 44100.0, scale = 1.0, lfoPhase = 0.0;
    Line predelay;
    std::array<Line, 4> inputAp;
    std::array<Line, 2> apMod, del1, ap2, del2;
    float bandLp = 0.0f;
    std::array<float, 2> damp {}, tankOut {};
    juce::SmoothedValue<float> sizeSmoothed { 1.0f };
    bool firstBlock = true;
};
