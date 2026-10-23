// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Reverb de muelles: modelo paramétrico de Välimäki, Parker y Abel (JAES 58(7/8), 2010; valores de la Tabla 1 de Gamper et al.,
// DAFx-11, contrastados con Parker, EURASIP 2011, y la implementación de Gajarsky). Por muelle: lazo de baja frecuencia con
// retardo y cascada de M = 100 all-pass estirados A(z) = (a + z^-K)/(1 + a·z^-K), K = fs/(2·Fc), a = 0,62, que crean los
// "chirps" por dispersión bajo Fc = 4300 Hz; tras él un paso bajo (elíptico de orden 10 a 4750 Hz en Gajarsky; aquí Butterworth
// de orden 8, aproximación). Lazo de alta frecuencia: 200 all-pass de primer orden (a = -0,6) y g = -0,77 (nivel relativo y
// retardo estimados). Dos muelles como el tanque Accutronics 4AB3C1B de Fender: 33 y 41 ms (izquierda / derecha). Decay:
// g = 10^(-3·T/T60). Paso alto de 40 Hz a la entrada (golpes de baja frecuencia, como en la hoja de Accutronics).
// K se redondea al entero más próximo (el modelo usa un all-pass fraccionario adicional): Fc efectiva = fs/(2·K).
struct SpringReverb
{
    void prepare (double sr)
    {
        sampleRate = sr;
        for (int s = 0; s < 2; ++s)
        {
            auto& sp = springs[(size_t) s];
            const auto td = s == 0 ? 0.033 : 0.041;
            sp.k = std::max (1, (int) std::lround (sr / (2.0 * kFc)));
            const auto apDelay = kLowStages * sp.k * (1.0 - kA) / (1.0 + kA); // retardo de grupo del all-pass en continua
            sp.delay = std::max (1, (int) std::lround (td * sr - apDelay));
            sp.line.assign ((size_t) sp.delay + 1, 0.0f);
            sp.hfLine.assign ((size_t) std::max (1, (int) std::lround (td * sr * 0.5)) + 1, 0.0f);
            sp.apBuf.assign ((size_t) (kLowStages * sp.k), 0.0f);
            sp.apOut.assign ((size_t) (kLowStages * sp.k), 0.0f);
            static constexpr double q8[4] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 }; // Butterworth de orden 8
            for (int i = 0; i < 4; ++i) sp.lp[(size_t) i].set (1, 4750.0, q8[i], sr);
            sp.td = td;
        }
        hpCoef = std::exp (-juce::MathConstants<double>::twoPi * 40.0 / sr);
        reset();
    }

    void reset()
    {
        for (auto& sp : springs)
        {
            std::fill (sp.line.begin(), sp.line.end(), 0.0f);
            std::fill (sp.hfLine.begin(), sp.hfLine.end(), 0.0f);
            std::fill (sp.apBuf.begin(), sp.apBuf.end(), 0.0f);
            std::fill (sp.apOut.begin(), sp.apOut.end(), 0.0f);
            sp.hfState.fill (0.0f);
            for (auto& b : sp.lp) b.clear();
            sp.pos = sp.hfPos = sp.apPos = 0;
            sp.fbLow = sp.fbHigh = 0.0f;
        }
        hpX = hpY = 0.0;
    }

    void process (juce::AudioBuffer<float>& buffer, float t60, float mix)
    {
        const auto numCh = buffer.getNumChannels();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < numCh; ++c) mono += buffer.getSample (c, i);
            mono /= (float) numCh;
            hpY = hpCoef * (hpY + mono - hpX); hpX = mono; // paso alto de 40 Hz
            float wet[2];
            for (int s = 0; s < 2; ++s)
                wet[s] = springSample (springs[(size_t) s], (float) hpY, t60);
            for (int c = 0; c < numCh; ++c)
            {
                const auto w = numCh == 1 ? 0.5f * (wet[0] + wet[1]) : wet[c & 1];
                const auto x = buffer.getSample (c, i);
                buffer.setSample (c, i, x + mix * (w - x));
            }
        }
    }

private:
    static constexpr int kLowStages = 100, kHighStages = 200;
    static constexpr double kFc = 4300.0, kA = 0.62, kAHigh = -0.6;

    struct Spring
    {
        std::vector<float> line, hfLine, apBuf, apOut;
        std::array<float, kHighStages> hfState {};
        std::array<Biquad, 4> lp;
        int delay = 1, k = 5, pos = 0, hfPos = 0, apPos = 0;
        float fbLow = 0.0f, fbHigh = 0.0f;
        double td = 0.033;
    };

    float springSample (Spring& sp, float x, float t60)
    {
        const auto gLow = -(float) std::pow (10.0, -3.0 * sp.td / std::max (0.2f, t60));        // g_lf (signo de la tabla: -0,8)
        const auto gHigh = -(float) std::pow (10.0, -3.0 * sp.td * 0.5 / std::max (0.2f, t60 * 0.6f));
        // Lazo de baja frecuencia: entrada + realimentación -> 100 all-pass estirados (buffers circulares de K muestras)
        auto v = x + gLow * sp.fbLow;
        const auto k = sp.k;
        for (int m = 0; m < kLowStages; ++m)
        {
            auto* in = &sp.apBuf[(size_t) (m * k)];
            auto* out = &sp.apOut[(size_t) (m * k)];
            const auto idx = sp.apPos;            // misma posición circular para todas las etapas
            const auto xK = in[idx], yK = out[idx];
            const auto y = (float) kA * (v - yK) + xK; // y = a·x + x[n-K] - a·y[n-K]
            in[idx] = v;
            out[idx] = y;
            v = y;
        }
        sp.apPos = (sp.apPos + 1) % k;
        // Retardo del lazo
        const auto delayed = sp.line[(size_t) sp.pos];
        sp.line[(size_t) sp.pos] = v;
        sp.pos = (sp.pos + 1) % (int) sp.line.size();
        sp.fbLow = delayed;
        auto low = (double) delayed;
        for (auto& b : sp.lp) low = b.process (low);

        // Lazo de alta frecuencia: 200 all-pass de primer orden (a = -0,6) + retardo de medio lazo
        auto h = x + gHigh * sp.fbHigh;
        for (int m = 0; m < kHighStages; ++m)
        {
            const auto y = (float) kAHigh * h + sp.hfState[(size_t) m];
            sp.hfState[(size_t) m] = h - (float) kAHigh * y;
            h = y;
        }
        const auto hDelayed = sp.hfLine[(size_t) sp.hfPos];
        sp.hfLine[(size_t) sp.hfPos] = h;
        sp.hfPos = (sp.hfPos + 1) % (int) sp.hfLine.size();
        sp.fbHigh = hDelayed;
        return (float) low + 0.2f * hDelayed; // nivel del lazo de alta: estimado
    }

    double sampleRate = 44100.0, hpCoef = 0.0, hpX = 0.0, hpY = 0.0;
    std::array<Spring, 2> springs;
};
