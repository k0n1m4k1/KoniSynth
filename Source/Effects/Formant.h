// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Filtro de formantes: 3 pasa-banda RBJ (pico 0 dB) en paralelo con F, Q = F/BW y nivel A de la tabla de formantes cantados del
// manual de Csound (csound.com/docs/manual/MiscFormants.html; comprobada contra la página): Male = "bass", Female = "soprano".
// Vocal continua 0..4 (A E I O U): F se interpola en log2, A en dB y BW linealmente entre las dos vocales vecinas. Shift
// multiplica todas las F por 2^(st/12). Morph: LFO seno que barre ±Depth vocales alrededor de Vowel.
// Compensación ×5,6 (+15 dB): pérdida media de las 3 bandas con una sierra (réplica scratchpad/formant_check.py: -8 a -19 dB
// según vocal, ±4 dB entre vocales). Coeficientes recalculados cada 32 muestras.
struct FormantFilter
{
    struct Vowel { float f[3], a[3], bw[3]; };
    static constexpr Vowel table[2][5] = {
        { { { 600, 1040, 2250 }, { 0, -7, -9 },   { 60, 70, 110 } },    // bass A
          { { 400, 1620, 2400 }, { 0, -12, -9 },  { 40, 80, 100 } },    // E
          { { 250, 1750, 2600 }, { 0, -30, -16 }, { 60, 90, 100 } },    // I
          { { 400, 750, 2400 },  { 0, -11, -21 }, { 40, 80, 100 } },    // O
          { { 350, 600, 2400 },  { 0, -20, -32 }, { 40, 80, 100 } } },  // U
        { { { 800, 1150, 2900 }, { 0, -6, -32 },  { 80, 90, 120 } },    // soprano A
          { { 350, 2000, 2800 }, { 0, -20, -15 }, { 60, 100, 120 } },   // E
          { { 270, 2140, 2950 }, { 0, -12, -26 }, { 60, 90, 100 } },    // I
          { { 450, 800, 2830 },  { 0, -11, -22 }, { 40, 80, 100 } },    // O
          { { 325, 700, 2700 },  { 0, -16, -35 }, { 50, 60, 170 } } },  // U
    };

    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() { state = {}; lfoPhase = 0.0; countdown = 0; }

    void process (juce::AudioBuffer<float>& buffer, const Params& p)
    {
        const auto voice = juce::jlimit (0, 1, (int) p.formantVoice->load());
        const auto shift = std::exp2 (p.formantShift->load() / 12.0f);
        const auto mix = p.formantMix->load();
        const auto lfoInc = p.formantRate->load() / sampleRate;
        const auto depth = p.formantDepth->load();
        const auto channels = juce::jmin (2, buffer.getNumChannels());

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (--countdown <= 0)
            {
                countdown = 32;
                const auto vowel = juce::jlimit (0.0f, 4.0f, p.formantVowel->load() + depth * (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase));
                setVowel (table[voice], vowel, shift);
            }
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;

            for (int ch = 0; ch < channels; ++ch)
            {
                const auto x = buffer.getSample (ch, i);
                float wet = 0.0f;
                for (int b = 0; b < 3; ++b)
                {
                    auto& z = state[(size_t) ch][(size_t) b];
                    const auto& c = coef[(size_t) b];
                    const auto y = c.b0 * x + z.z1;       // forma directa II transpuesta; b1 = 0 en el pasa-banda
                    z.z1 = -c.a1 * y + z.z2;
                    z.z2 = -c.b0 * x - c.a2 * y;          // b2 = -b0
                    wet += c.gain * y;
                }
                buffer.setSample (ch, i, x + mix * (5.6f * wet - x));
            }
        }
    }

private:
    struct Coef { float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f, gain = 0.0f; };
    struct Z { float z1 = 0.0f, z2 = 0.0f; };

    void setVowel (const Vowel (&set)[5], float v, float shift)
    {
        const auto i = juce::jmin (3, (int) v);
        const auto t = v - (float) i;
        const auto& lo = set[i];
        const auto& hi = set[i + 1];
        for (int b = 0; b < 3; ++b)
        {
            const auto f = juce::jlimit (30.0, 0.45 * sampleRate,
                                         (double) shift * std::exp2 ((1.0f - t) * std::log2 (lo.f[b]) + t * std::log2 (hi.f[b])));
            const auto bw = (double) shift * ((1.0f - t) * lo.bw[b] + t * hi.bw[b]);
            const auto aDb = (1.0f - t) * lo.a[b] + t * hi.a[b];
            const auto w = juce::MathConstants<double>::twoPi * f / sampleRate;
            const auto alpha = std::sin (w) / (2.0 * (f / bw));
            const auto a0 = 1.0 + alpha;
            auto& c = coef[(size_t) b];
            c.b0 = (float) (alpha / a0);
            c.a1 = (float) (-2.0 * std::cos (w) / a0);
            c.a2 = (float) ((1.0 - alpha) / a0);
            c.gain = juce::Decibels::decibelsToGain (aDb);
        }
    }

    double sampleRate = 44100.0, lfoPhase = 0.0;
    int countdown = 0;
    std::array<Coef, 3> coef {};
    std::array<std::array<Z, 3>, 2> state {};
};
