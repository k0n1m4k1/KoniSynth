// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Vocoder de canales. Moduladora = entrada de audio (mono), portadora = salida del motor (estéreo).
// Bancos:
//   VP-330 10: centros del Roland VP-330 (150 Hz – 5,2 kHz; smemmusic.ch).
//   16 Band: arquitectura del EMS Vocoder 2000 (14 pasabanda de 6º orden + pasa-bajos + pasa-altos; vintagesynth.com).
//            Frecuencias del EMS no encontradas: 14 centros logarítmicos de 200 Hz a 5 kHz (estimación).
//   32 Band: 100 Hz – 10 kHz logarítmicos.
// Cada banda = 3 biquads idénticos (6º orden). Bordes en la media geométrica entre centros vecinos; Q de cada biquad
// = fc/BW · √(2^(1/3) - 1) para que la cascada caiga 3 dB en los bordes; pasa-bajos/altos con 3 Butterworth de
// 2º orden a borde/0,714 y borde·0,714. Bandas alternas con polaridad invertida (y LP/HP invertidos respecto a la
// alternancia en el de 16). Medido en réplica (scratchpad/vocoder_check.py): cruces -3,0..-4,6 dB; suma compleja de
// bandas -1,8..+3,6 dB (sin alternar: muescas de -30 a -45 dB).
// Envolvente por banda: rectificador de onda completa + un polo con ataque/release. Formant: desplaza las bandas de la
// portadora ±12 semitonos. Sibilance: pasa-altos de 2º orden a 6 kHz de la moduladora (consonantes sordas).
// Compensación por banco con portadora sierra 110 Hz (pico 0,2) y moduladora rosa a -20 dBFS RMS: salida RMS = portadora.
struct Vocoder
{
    static constexpr int maxBands = 32;

    void prepare (double sr)
    {
        sampleRate = sr;
        configuredBank = -1;
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            for (auto* chain : { &b.mod, &b.carL, &b.carR })
                for (auto& q : *chain)
                    q.z1 = q.z2 = 0.0;
            b.env = 0.0;
        }
        sibilance.z1 = sibilance.z2 = 0.0;
    }

    void process (juce::AudioBuffer<float>& carrier, const float* modulator, const Params& p)
    {
        const auto bank = juce::jlimit (0, 2, (int) p.vocoderBank->load());
        const auto formant = p.vocoderFormant->load();
        if (bank != configuredBank || formant != configuredFormant)
            configure (bank, formant);

        const auto ka = 1.0 - std::exp (-1.0 / (0.001 * p.vocoderAttack->load() * sampleRate));
        const auto kr = 1.0 - std::exp (-1.0 / (0.001 * p.vocoderRelease->load() * sampleRate));
        const auto sib = (double) p.vocoderSibilance->load();
        const auto mix = p.vocoderMix->load();
        const auto dry = std::cos (mix * juce::MathConstants<float>::halfPi);
        const auto wet = std::sin (mix * juce::MathConstants<float>::halfPi);
        constexpr double makeup[3] = { 69.5, 13.8, 197.7 };
        const auto gain = makeup[bank];
        const auto stereo = carrier.getNumChannels() > 1;

        for (int i = 0; i < carrier.getNumSamples(); ++i)
        {
            const auto m = (double) modulator[i];
            const auto cl = (double) carrier.getSample (0, i);
            const auto cr = stereo ? (double) carrier.getSample (1, i) : cl;
            double outL = 0.0, outR = 0.0;

            for (int b = 0; b < numBands; ++b)
            {
                auto& band = bands[(size_t) b];
                const auto level = std::abs (run (band.mod, m));
                band.env += (level > band.env ? ka : kr) * (level - band.env);
                const auto e = band.polarity * band.env;
                outL += e * run (band.carL, cl);
                if (stereo)
                    outR += e * run (band.carR, cr);
            }

            const auto s = sib * sibilance.process (m);
            carrier.setSample (0, i, (float) (dry * cl + wet * (gain * outL + s)));
            if (stereo)
                carrier.setSample (1, i, (float) (dry * cr + wet * (gain * outR + s)));
        }
    }

private:

    using Chain = std::array<Biquad, 3>;

    struct Band
    {
        Chain mod, carL, carR;
        double env = 0.0, polarity = 1.0;
    };

    static double run (Chain& chain, double x)
    {
        for (auto& q : chain)
            x = q.process (x);
        return x;
    }

    void configure (int bank, float formant)
    {
        configuredBank = bank;
        configuredFormant = formant;

        std::array<double, maxBands> centres {};
        int n = 0;
        if (bank == 0)
        {
            for (auto f : { 150.0, 220.0, 350.0, 500.0, 760.0, 1100.0, 1600.0, 2200.0, 3600.0, 5200.0 })
                centres[(size_t) n++] = f;
        }
        else
        {
            const auto count = bank == 1 ? 14 : 32;
            const auto lo = bank == 1 ? 200.0 : 100.0, hi = bank == 1 ? 5000.0 : 10000.0;
            for (int i = 0; i < count; ++i)
                centres[(size_t) n++] = lo * std::pow (hi / lo, i / (double) (count - 1));
        }

        const auto shift = std::exp2 (formant / 12.0);
        const auto cascadeBw = std::sqrt (std::cbrt (2.0) - 1.0);       // 0,5098
        const auto cascadeLp = std::pow (std::cbrt (2.0) - 1.0, 0.25);   // 0,7140
        auto edgeLow  = [&] (int i) { return i > 0 ? std::sqrt (centres[(size_t) i] * centres[(size_t) i - 1]) : centres[0] / std::sqrt (centres[1] / centres[0]); };
        auto edgeHigh = [&] (int i) { return i < n - 1 ? std::sqrt (centres[(size_t) i] * centres[(size_t) i + 1])
                                                       : centres[(size_t) n - 1] * std::sqrt (centres[(size_t) n - 1] / centres[(size_t) n - 2]); };

        auto setChain = [&] (Band& band, int type, double f, double q)
        {
            for (auto& s : band.mod)  s.set (type, f, q, sampleRate);
            for (auto& s : band.carL) s.set (type, f * shift, q, sampleRate);
            for (auto& s : band.carR) s.set (type, f * shift, q, sampleRate);
        };

        numBands = 0;
        if (bank == 1) // pasa-bajos por debajo del primer borde, con polaridad opuesta a la alternancia
        {
            setChain (bands[0], 1, edgeLow (0) / cascadeLp, juce::MathConstants<double>::sqrt2 * 0.5);
            bands[0].polarity = -1.0;
            numBands = 1;
        }
        for (int i = 0; i < n; ++i)
        {
            const auto lo = edgeLow (i), hi = edgeHigh (i), fc = std::sqrt (lo * hi);
            auto& band = bands[(size_t) numBands];
            setChain (band, 0, fc, fc / (hi - lo) * cascadeBw);
            band.polarity = (numBands % 2 == 0) ? 1.0 : -1.0;
            ++numBands;
        }
        if (bank == 1)
        {
            auto& band = bands[(size_t) numBands];
            setChain (band, 2, edgeHigh (n - 1) * cascadeLp, juce::MathConstants<double>::sqrt2 * 0.5);
            band.polarity = (numBands % 2 == 0) ? -1.0 : 1.0;
            ++numBands;
        }

        sibilance.set (2, 6000.0, juce::MathConstants<double>::sqrt2 * 0.5, sampleRate);
    }

    double sampleRate = 44100.0;
    int configuredBank = -1, numBands = 0;
    float configuredFormant = 0.0f;
    std::array<Band, maxBands> bands {};
    Biquad sibilance;
};
