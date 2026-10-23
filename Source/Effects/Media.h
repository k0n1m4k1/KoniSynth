// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

struct MediaSpec
{
    double speed;      // m/s de cinta (0 = vinilo)
    double eqTau;      // s, constante de agudos de la ecualización de reproducción (0 = vinilo: RIAA)
    float wowFlutter;  // fracción rms de desviación de velocidad
    float rotationHz;  // componente periódica del wow
    float snrDb;       // relación señal/ruido respecto a la referencia (RMS 0,25)
    float f3Hz;        // -3 dB de agudos con el soporte nuevo
    float lowHz;       // extremo grave
    bool tape;
};

static constexpr MediaSpec mediaSpecs[] = {
    // Cassette Type I: 4,76 cm/s; 120 µs (IASA); W&F 0,08 % (típico, Wikipedia); S/N 55–60 dB sin NR; 30–14000 Hz (TEAC 375, normal)
    { 0.0476, 120.0e-6, 0.0008f,  0.7f,    58.0f, 14000.0f, 30.0f, true },
    // Vinilo: W&F 0,025 % WRMS (Technics SL-1200); 33⅓ rpm = 0,556 Hz; S/N 55–60 dB; 20 kHz estimado; polo IEC de 20 Hz
    { 0.0,      0.0,      0.00025f, 0.5556f, 58.0f, 20000.0f, 20.0f, false },
    // Magnetófono 38 cm/s: 35 µs (IEC); W&F estimado; S/N 60–70 dB; 30–30000 Hz (Denon DH-510)
    { 0.38,     35.0e-6,  0.0003f,  0.5f,    68.0f, 30000.0f, 30.0f, true },
    // Magnetófono 19 cm/s: 70 µs (IEC); W&F estimado; 20–25000 Hz (Braun)
    { 0.19,     70.0e-6,  0.0005f,  0.5f,    65.0f, 25000.0f, 20.0f, true },
    // Magnetófono 9,5 cm/s: 90 µs (IASA); W&F estimado; 12,5 kHz derivado por Wallace (misma d que a 19 cm/s)
    { 0.095,    90.0e-6,  0.001f,   0.5f,    60.0f, 12500.0f, 30.0f, true },
};

// Modelo:
//  1) Saturación (solo cinta): tanh simétrica (armónicos impares) con compensación: pico 0,3 entra y sale a ~0,3.
//  2) Wow & flutter: velocidad (1 + δ) -> retardo modulado, dτ/dt = -δ (integrador con fuga de 2 s, base 6 ms).
//     δ = senoide de rotación (50 % de la potencia) + ruido 0,5–6 Hz (30 %) + ruido 6–100 Hz (20 %): bandas de wow y flutter
//     de IEC 60386 / DIN 45507; el reparto es estimado. δ_rms = W&F × (1 + 9·Age) × 2·Wow (Wow 50 % = especificación).
//  3) Agudos: pérdida por separación (Wallace, BSTJ 1951): 54,6·d/λ dB, λ = v/f -> f(-3 dB) = 3·v / (54,6·d).
//     d0 se deduce de la especificación; Age suma hasta 1 µm (estimado). Se aproxima con 2 polos a la misma -3 dB.
//     Vinilo: f(-3 dB) / (1 + 2,3·Age) por desgaste de aguja (estimado).
//  4) Ruido. Cinta: blanco con la forma de la ecualización de reproducción (plano bajo f_eq = 1/(2πτ), +6 dB/oct encima
//     hasta +18 dB) y la misma pérdida de agudos. Vinilo: blanco por la curva RIAA (3180/318/75 µs) + rumble 10–30 Hz a
//     -65 dB (especificaciones de -55 a -78 dB) + crepitar Poisson + rayas que repiten cada vuelta. Nivel del ruido:
//     referencia - S/N, ±20 dB con Noise (50 % = especificación), +12 dB a Age 100 %. Las S/N publicadas suelen ir
//     ponderadas: el nivel es aproximado.
struct Media
{
    // Aleatoriedad reproducible (Dsp/RandomClock.h): el procesador la resiembra en cada anclaje del transporte
    void reseed (juce::int64 seed) { rng.setSeed (seed); }

    void prepare (double sr, int maxBlock, int numChannels)
    {
        sampleRate = sr;
        line.prepare ({ sr, (juce::uint32) maxBlock, (juce::uint32) juce::jmax (1, numChannels) });
        line.setMaximumDelayInSamples ((int) std::ceil (0.045 * sr)); // base hasta 10,5 ms (Age y Wow al máximo) + 30 ms
        baseDelay.reset (sr, 2.0);
        rng.setSeedRandomly();

        // Normalización de las bandas de wow/flutter (fijas por frecuencia de muestreo)
        wowBand.set (sr, 0.5, 6.0);
        flutterBand.set (sr, 6.0, 100.0);
        rumbleBand.set (sr, 10.0, 30.0);
        cachedType = -1;
        reset();
    }

    void reset()
    {
        line.reset();
        tauOffset = 0.0;
        firstBlock = true;
        rotation = 0.0;
        wowBand.clear(); flutterBand.clear(); rumbleBand.clear();
        for (auto& c : channels)
            c = {};
    }

    void process (juce::AudioBuffer<float>& buffer, int type, float age, float noiseKnob, float wowKnob, float driveKnob)
    {
        const auto& spec = mediaSpecs[juce::jlimit (0, 4, type)];
        configure (type, spec, age);

        const auto numChannels = juce::jmin (buffer.getNumChannels(), 2);
        const auto sr = (float) sampleRate;

        const auto deltaRms = spec.wowFlutter * (1.0f + 9.0f * age) * 2.0f * wowKnob;

        // Retardo base = latencia de Media. Solo tiene que cubrir la excursión negativa de τ: el modelo es lineal en
        // δ_rms y en réplica (120 s) el pico de |τ| fue 0,36–0,48 s × δ_rms según el soporte. Base = 0,5 ms + 0,625 s·δ_rms
        // (1,3× el peor caso). Cassette por defecto: 2,35 ms (antes 6 ms fijos); vinilo nuevo: 0,66 ms.
        // Se desliza en 2 s al girar Age/Wow: ≤ 0,1 % de desviación de tono transitoria, por debajo del propio wow.
        const auto targetBase = (0.0005 + 0.625 * (double) deltaRms) * sampleRate;
        if (firstBlock)
        {
            baseDelay.setCurrentAndTargetValue (targetBase);
            firstBlock = false;
        }
        baseDelay.setTargetValue (targetBase);
        const auto noiseDb  = -12.04f - spec.snrDb + 40.0f * (noiseKnob - 0.5f) + 12.0f * age; // -12,04 dB = RMS 0,25
        const auto noiseRms = juce::Decibels::decibelsToGain (noiseDb);
        const auto rumbleRms = juce::Decibels::decibelsToGain (-12.04f - 65.0f + 40.0f * (noiseKnob - 0.5f) + 10.0f * age);

        const auto g    = juce::Decibels::decibelsToGain (18.0f * driveKnob);
        const auto comp = 0.3f / std::tanh (0.3f * g);

        // Vinilo: crepitar (tasa y amplitud estimadas; picos de ruido de un LP nuevo y silencioso ≈ -40 dB, Stereo Lab)
        const auto crackleProb = (30.0f + 600.0f * age) / sr;
        const auto crackleAmp  = 0.01f * (1.0f + 4.0f * age) * juce::Decibels::decibelsToGain (40.0f * (noiseKnob - 0.5f));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // Wow & flutter
            rotation += spec.rotationHz / sampleRate;
            const auto crossedRevolution = rotation >= 1.0;
            if (crossedRevolution)
                rotation -= 1.0;

            const auto delta = deltaRms * (std::sin (juce::MathConstants<double>::twoPi * rotation)            // senoide: potencia 0,5
                                           + std::sqrt (0.3) * wowBand.next (white())
                                           + std::sqrt (0.2) * flutterBand.next (white()));
            const auto base = baseDelay.getNextValue();
            tauOffset = juce::jlimit (-(base - 0.00025 * sampleRate), 0.030 * sampleRate,
                                      tauOffset - delta - tauOffset / (2.0 * sampleRate));
            const auto delay = (float) (base + tauOffset);

            // Vinilo: eventos de crepitar y rayas (comunes a ambos canales: el surco es uno)
            float click = 0.0f;
            if (! spec.tape)
            {
                if (rng.nextFloat() < crackleProb)
                    click += crackleAmp * -std::log (juce::jmax (1.0e-6f, rng.nextFloat())) * (rng.nextBool() ? 1.0f : -1.0f);
                for (int s = 0; s < numScratches; ++s)
                    if (crossesPosition (scratchPos[(size_t) s], spec.rotationHz))
                        click += scratchAmp[(size_t) s];
            }

            const auto common = white(); // parte común del ruido de superficie del vinilo
            const auto rumble = spec.tape ? 0.0f : rumbleRms * rumbleBand.next (white()); // mecánico: común a ambos canales

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& c = channels[(size_t) ch];
                auto x = buffer.getSample (ch, i);

                if (spec.tape)
                    x = std::tanh (g * x) * comp;

                line.pushSample (ch, x);
                auto y = line.popSample (ch, delay);

                // Respuesta del soporte: 2 polos de agudos + 1 polo de graves
                const auto lowpassed = tpt (tpt (y, lpG, c.lp1), lpG, c.lp2);
                c.hp = lowpassed - c.hpPrev + hpCoef * c.hp;
                c.hpPrev = lowpassed;
                y = c.hp;

                // Ruido
                float n;
                if (spec.tape)
                {
                    n = noiseRms * noiseNorm * shapeTapeNoise (c, white());
                }
                else
                {
                    const auto w = 0.7071f * (common + white()); // 50 % de potencia común entre canales
                    n = noiseRms * noiseNorm * shapeVinylNoise (c, w) + rumble + clickFilter (c, click);
                }

                buffer.setSample (ch, i, y + n);
            }
        }
    }

private:
    struct Band // ruido blanco -> pasa-altos 1 polo -> pasa-bajos 1 polo, normalizado a RMS 1
    {
        void set (double sr, double lo, double hi)
        {
            hpC = (float) (1.0 - juce::MathConstants<double>::twoPi * lo / sr);
            lpC = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hi / sr));
            // Potencia de la respuesta al impulso (8 constantes de tiempo del polo más lento) para normalizar
            clear();
            double power = 0.0;
            const auto n = (int) (8.0 * sr / (juce::MathConstants<double>::twoPi * lo));
            for (int i = 0; i < n; ++i)
            {
                const auto v = step (i == 0 ? 1.0f : 0.0f);
                power += (double) v * v;
            }
            norm = (float) (1.0 / std::sqrt (power)); // white() tiene varianza 1
            clear();
        }
        float step (float x) { hp = x - prev + hpC * hp; prev = x; lp += lpC * (hp - lp); return lp; }
        float next (float x) { return norm * step (x); }
        void clear() { hp = prev = lp = 0.0f; }
        float hpC = 0.0f, lpC = 0.0f, norm = 1.0f, hp = 0.0f, prev = 0.0f, lp = 0.0f;
    };

    struct ChannelState
    {
        float lp1 = 0.0f, lp2 = 0.0f, hp = 0.0f, hpPrev = 0.0f;           // respuesta del soporte (lp1/lp2: estados TPT)
        float nShelfHp = 0.0f, nShelfPrev = 0.0f, nLp1 = 0.0f, nLp2 = 0.0f; // forma del ruido de cinta
        float riaa1 = 0.0f, riaa3 = 0.0f;                                 // RIAA
        float clickLp = 0.0f;
    };

    // Pasa-bajos de 1 polo TPT: s es el estado
    static float tpt (float x, float G, float& s)
    {
        const auto v = G * (x - s);
        const auto y = v + s;
        s = y + v;
        return y;
    }

    // Ruido blanco uniforme de varianza 1
    float white() { return std::sqrt (3.0f) * (2.0f * rng.nextFloat() - 1.0f); }

    float shapeTapeNoise (ChannelState& c, float x) const
    {
        c.nShelfHp = x - c.nShelfPrev + shelfCoef * c.nShelfHp; // pasa-altos en f_eq
        c.nShelfPrev = x;
        const auto shelved = x + 7.0f * c.nShelfHp;             // +18 dB por encima de f_eq
        return tpt (tpt (shelved, lpG, c.nLp1), lpG, c.nLp2);
    }

    float shapeVinylNoise (ChannelState& c, float x) const
    {
        // RIAA reproducción: (1 + sT2) / ((1 + sT1)(1 + sT3)) = [r + (1 - r)/(1 + sT1)] · 1/(1 + sT3), r = T2/T1 = 0,1
        c.riaa1 += riaaC1 * (x - c.riaa1);
        const auto shelf = 0.1f * x + 0.9f * c.riaa1;
        c.riaa3 += riaaC3 * (shelf - c.riaa3);
        return tpt (tpt (c.riaa3, lpG, c.nLp1), lpG, c.nLp2);
    }

    float clickFilter (ChannelState& c, float impulse) const
    {
        c.clickLp += riaaC3 * (impulse - c.clickLp); // el polo de 75 µs (2122 Hz) de la RIAA redondea el clic
        return c.clickLp;
    }

    bool crossesPosition (double pos, float rotationHz) const
    {
        const auto prev = rotation - rotationHz / sampleRate;
        return prev < 0.0 ? (pos >= prev + 1.0 || pos < rotation) : (pos >= prev && pos < rotation);
    }

    void configure (int type, const MediaSpec& spec, float age)
    {
        const auto ageQ = std::round (age * 100.0f) / 100.0f; // recalcula solo si cambia en pasos de 1 %
        if (type == cachedType && ageQ == cachedAge)
            return;
        cachedType = type;
        cachedAge = ageQ;

        double f3;
        if (spec.tape)
        {
            const auto d0 = 3.0 * spec.speed / (54.6 * spec.f3Hz); // m, separación equivalente del soporte nuevo
            f3 = 3.0 * spec.speed / (54.6 * (d0 + ageQ * 1.0e-6));
        }
        else
        {
            f3 = spec.f3Hz / (1.0 + 2.3 * ageQ);
        }

        // 2 polos iguales TPT (bilineal, cero en Nyquist). En el dominio prewarped la cascada cae -3 dB donde
        // tan(π·f/fs) = 0,6436·g, así que g = tan(π·f3/fs) / 0,6436 da -3 dB exactos en f3. Con el polo
        // "y += a·(x - y)" se medía -3 dB a 21,6 kHz pidiendo 14 kHz (solo ~1 dB de atenuación cerca de Nyquist).
        const auto g = std::tan (juce::MathConstants<double>::pi * juce::jmin (f3, 0.49 * sampleRate) / sampleRate) / 0.6436;
        lpG       = (float) (g / (1.0 + g));
        hpCoef    = (float) (1.0 - juce::MathConstants<double>::twoPi * spec.lowHz / sampleRate);
        shelfCoef = spec.tape ? (float) (1.0 - 1.0 / (spec.eqTau * sampleRate)) : 0.0f;
        riaaC1    = (float) (1.0 - std::exp (-1.0 / (3180.0e-6 * sampleRate)));
        riaaC3    = (float) (1.0 - std::exp (-1.0 / (75.0e-6 * sampleRate)));

        // Rayas: 0–4 según Age, en posiciones fijas de la vuelta (amplitud estimada)
        numScratches = (int) (ageQ * 4.0f);
        for (int s = 0; s < 4; ++s)
        {
            scratchPos[(size_t) s] = rng.nextDouble();
            scratchAmp[(size_t) s] = (0.04f + 0.08f * rng.nextFloat()) * (rng.nextBool() ? 1.0f : -1.0f);
        }

        // Normaliza el ruido a RMS 1: potencia de la respuesta al impulso de la cadena de forma
        ChannelState probe;
        double power = 0.0;
        for (int i = 0; i < 8192; ++i)
        {
            const auto x = i == 0 ? 1.0f : 0.0f;
            const auto v = spec.tape ? shapeTapeNoise (probe, x) : shapeVinylNoise (probe, x);
            power += (double) v * v;
        }
        noiseNorm = (float) (1.0 / std::sqrt (juce::jmax (power, 1.0e-12)));
    }

    double sampleRate = 44100.0, tauOffset = 0.0, rotation = 0.0;
    juce::SmoothedValue<double> baseDelay { 0.0 };
    bool firstBlock = true;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> line;
    juce::Random rng;
    Band wowBand, flutterBand, rumbleBand;
    std::array<ChannelState, 2> channels {};
    std::array<double, 4> scratchPos {};
    std::array<float, 4> scratchAmp {};
    int numScratches = 0, cachedType = -1;
    float cachedAge = -1.0f, lpG = 0.5f, hpCoef = 1.0f, shelfCoef = 0.0f, riaaC1 = 1.0f, riaaC3 = 1.0f, noiseNorm = 1.0f;
};
