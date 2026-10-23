// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Desplazador de frecuencia (Bode Frequency Shifter / Moog 1630; artículos de H. Bode de 1972 y 1976, ZKM): ±5000 Hz, salidas
// superior e inferior y mezcla continua entre ellas. Señal analítica con el Hilbert IIR de Olli Niemitalo: dos cadenas de 4
// all-pass en z^-2, (a² - z^-2)/(1 - a²·z^-2), una con z^-1 adicional; 90 ± 0,7° en el 99,8 % de la banda -> rechazo de la
// banda lateral ≈ 44 dB (el Bode especifica ≥ 40 dB). Coeficientes publicados (a): rama 1 con z^-1 y rama 2.
struct FrequencyShifter
{
    void prepare (double sr) { sampleRate = sr; reset(); }

    void reset()
    {
        for (auto& c : chans) c = {};
        phase = 0.0;
    }

    void process (juce::AudioBuffer<float>& buffer, float shiftHz, float balance, float mix)
    {
        const auto inc = (double) shiftHz / sampleRate;
        const auto numCh = std::min (2, buffer.getNumChannels());
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto c0 = std::cos (juce::MathConstants<double>::twoPi * phase);
            const auto s0 = std::sin (juce::MathConstants<double>::twoPi * phase);
            phase += inc;
            phase -= std::floor (phase);
            for (int c = 0; c < numCh; ++c)
            {
                auto& st = chans[(size_t) c];
                const auto x = buffer.getSample (c, i);
                const auto re = allpassChain (st.b2, kB, x);
                const auto im = st.delayed;               // z^-1 de la rama 1
                st.delayed = allpassChain (st.b1, kA, x);
                const auto upper = re * c0 - im * s0;      // banda lateral superior (desplazada +shift)
                const auto lower = re * c0 + im * s0;
                const auto wet = (float) (0.5 * (1.0 + balance) * upper + 0.5 * (1.0 - balance) * lower);
                buffer.setSample (c, i, x + mix * (wet - x));
            }
        }
    }

private:
    static constexpr double kA[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    static constexpr double kB[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };

    struct Section { double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0; };
    struct Chan { std::array<Section, 4> b1, b2; double delayed = 0.0; };

    // out(t) = a²·(in(t) + out(t-2)) - in(t-2)
    static double allpassChain (std::array<Section, 4>& s, const double (&a)[4], double x)
    {
        for (int k = 0; k < 4; ++k)
        {
            auto& q = s[(size_t) k];
            const auto a2 = a[k] * a[k];
            const auto y = a2 * (x + q.y2) - q.x2;
            q.x2 = q.x1; q.x1 = x;
            q.y2 = q.y1; q.y1 = y;
            x = y;
        }
        return x;
    }

    double sampleRate = 44100.0, phase = 0.0;
    std::array<Chan, 2> chans;
};
