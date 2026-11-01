// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

struct FmEngine
{
    static constexpr int kVoices = 16; // como el DX7 (manual: 16 notas; con todas ocupadas suelta la primera)
    static constexpr int kGroups = kVoices / konisimd::kLanes; // grupos de 8 carriles AVX2

    void prepare (double sr)
    {
        sampleRate = sr;
        opRate = sr;
        srScale = 44100.0 / sr;
        bendCoef = 1.0f - (float) std::exp (-1.0 / (0.005 * sr));
        hwBlep.prepare (kHwChunk);
        hwLpf.set (1, 15000.0, 0.70710678, sr);
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v = {};
        laneGroups = {};
        hwBlep.reset();
        hwLpf.clear();
        hwLast = 0.0;
        hwNext = 0.0;
        for (int g = 0; g < kGroups; ++g)
        {
            for (auto& a : hwAttG[(size_t) g]) a.fill (16383.0f);
            for (auto& a : hwAttStepG[(size_t) g]) a.fill (0.0f);
            for (auto& a : hwY1G[(size_t) g]) a.fill (0);
            for (auto& a : hwY2G[(size_t) g]) a.fill (0);
        }
        sustainPedal = false;
        lfoPhase = 0.0;
        lfoDelay = 0.0;
        // VOICE_RESET_EVENT_AND_PITCH_BUFFERS: buffers de tono a 0x2EA8 (nota 36, ya en Voice) y estado mono a cero
        lastPitch = 0x2EA8;
        monoCount = 0;
        legatoUp = false;
        lastKey = 0;
        portaTickPhase = 0.0;
        portaHalf = false;
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p)
    {
        // Modo: DX7 calcula a 49 096 Hz; Clean, a la frecuencia del host. Al cambiar se cortan las notas
        const auto hw = (int) p.fmMode->load() == 0;
        if (hw != hwMode)
        {
            reset();
            hwMode = hw;
        }
        opRate = hwMode ? kDx7Rate : sampleRate;
        srScale = 44100.0 / opRate;
        bendCoef = 1.0f - (float) std::exp (-1.0 / (0.005 * opRate));
        readPatch (p);
        // POLY/MONO: al cambiar, el DX7 apaga todas las voces y reinicia sus buffers (MIDI_RX_CC_126/127 -> VOICE_RESET)
        if (const auto m = p.fmMono->load() > 0.5f; m != mono)
        {
            reset();
            mono = m;
        }
        // Portamento: la velocidad sale de TABLE_PITCH_EG_RATE[99 - tiempo] (PORTA_COMPUTE_RATE_VALUE), la misma tabla que
        // pitchEnvRate; tiempo 0 -> 255 = sin portamento. El CC 5 fija el tiempo hasta que se mueve el parámetro
        const auto timeParam = juce::jlimit (0, 99, juce::roundToInt (p.fmPortaTime->load()));
        if (timeParam != portaTimeParam)
        {
            portaTimeParam = timeParam;
            midiPortaTime = -1;
        }
        portaRate = pitchEnvRate[99 - (midiPortaTime >= 0 ? midiPortaTime : timeParam)];
        portaMode = (int) p.fmPortaMode->load();
        glissando = p.fmGlissando->load() > 0.5f;
        portaPedalParam = p.fmPortaPedal->load() > 0.5f;
        // Redondeo, no truncado: el valor por defecto llega desnormalizado de 50/99 como 49,9999… y (int) lo dejaría en 49
        // (medido: pitch EG desafinado 1/32 de octava)
        transpose = juce::jlimit (-24, 24, juce::roundToInt (p.fmTranspose->load()));
        oscSync = p.fmOscSync->load() > 0.5f;
        lfoSync = p.fmLfoSync->load() > 0.5f;
        for (int k = 0; k < 4; ++k)
        {
            pitchRates[(size_t) k] = juce::jlimit (0, 99, juce::roundToInt (p.fmPitchRate[(size_t) k]->load()));
            pitchLevels[(size_t) k] = juce::jlimit (0, 99, juce::roundToInt (p.fmPitchLevel[(size_t) k]->load()));
        }
        const auto alg = juce::jlimit (0, 31, (int) p.fmAlgorithm->load() - 1);
        const auto fb = (int) p.fmFeedback->load();
        const auto fbScale = fb > 0 ? std::exp2 ((double) fb - 8.0) : 0.0;
        // Fuente y destino de la realimentación (bucle en los algoritmos 4 y 6)
        int fbDest = -1, fbSrc = -1;
        for (int i = 0; i < 6; ++i)
        {
            if (FmAlgorithms::flags[alg][i] & 0x40) fbDest = i;
            if (FmAlgorithms::flags[alg][i] & 0x80) fbSrc = i;
        }

        // LFO (lfo.cc): velocidad 0..99 -> Hz por tabla medida; PMD/AMD ·165/64; PMS por tabla
        static constexpr double lfoHz[100] = {
            0.062541, 0.125031, 0.312393, 0.437120, 0.624610, 0.750694, 0.936330, 1.125302, 1.249609, 1.436782,
            1.560915, 1.752081, 1.875117, 2.062494, 2.247191, 2.374451, 2.560492, 2.686728, 2.873976, 2.998950,
            3.188013, 3.369840, 3.500175, 3.682224, 3.812065, 4.000800, 4.186202, 4.310716, 4.501260, 4.623209,
            4.814636, 4.930480, 5.121901, 5.315191, 5.434783, 5.617346, 5.750431, 5.946717, 6.062811, 6.248438,
            6.431695, 6.564264, 6.749460, 6.868132, 7.052186, 7.250580, 7.375719, 7.556294, 7.687577, 7.877738,
            7.993605, 8.181967, 8.372405, 8.504848, 8.685079, 8.810573, 8.986341, 9.122423, 9.300595, 9.500285,
            9.607994, 9.798158, 9.950249, 10.117361, 11.251125, 11.384335, 12.562814, 13.676149, 13.904338, 15.092062,
            16.366612, 16.638935, 17.869907, 19.193858, 19.425019, 20.833333, 21.034918, 22.502250, 24.003841, 24.260068,
            25.746653, 27.173913, 27.578599, 29.052876, 30.693677, 31.191516, 32.658393, 34.317090, 34.674064, 36.416606,
            38.197097, 38.550501, 40.387722, 40.749796, 42.625746, 44.326241, 44.883303, 46.772685, 48.590865, 49.261084 };
        static constexpr int pmsTab[8] = { 0, 10, 20, 33, 55, 92, 153, 255 };
        const auto lfoInc = lfoHz[juce::jlimit (0, 99, (int) p.fmLfoSpeed->load())] / opRate;
        const auto lfoWave = (int) p.fmLfoWave->load();
        const auto pmd = ((int) p.fmLfoPmd->load() * 165) >> 6;
        const auto amd = ((int) p.fmLfoAmd->load() * 165) >> 6;
        const auto pms = pmsTab[juce::jlimit (0, 7, (int) p.fmLfoPms->load())];
        // Retardo: estado de 32 bits; primera mitad sin modulación, segunda en rampa (getdelay)
        const auto delayParam = 99 - juce::jlimit (0, 99, (int) p.fmLfoDelay->load());
        double delayInc1 = 1.0e12, delayInc2 = 1.0e12;
        if (delayParam != 99)
        {
            const auto unit = 25190424.0 / opRate;
            auto a = (16 + (delayParam & 15)) << (1 + (delayParam >> 4));
            delayInc1 = unit * a;
            a &= 0xff80;
            delayInc2 = unit * std::max (0x80, a);
        }
        // Modo Function (msfa controllers.h): total = valor del controlador (0-127) · rango / 100, truncado; varios
        // controladores sobre el mismo destino se combinan por máximo. Sin ningún EG BIAS asignado, eg = 127 (sin efecto)
        std::array<int, 4> ctlRange {};
        std::array<bool, 4> ctlPitch {}, ctlAmp {}, ctlEg {};
        for (size_t c = 0; c < 4; ++c)
        {
            ctlRange[c] = juce::jlimit (0, 99, juce::roundToInt (p.fmCtl[c].range->load()));
            ctlPitch[c] = p.fmCtl[c].pitch->load() > 0.5f;
            ctlAmp[c] = p.fmCtl[c].amp->load() > 0.5f;
            ctlEg[c] = p.fmCtl[c].eg->load() > 0.5f;
        }
        const auto anyEg = ctlEg[0] || ctlEg[1] || ctlEg[2] || ctlEg[3];
        const auto tune = (double) p.fineTune->load();
        for (auto& v : voices)
            if (v.active)
                updateFrequencies (v);

        konisimd::FmArgs args {};
        for (int k = 0; k < 6; ++k)
        {
            args.flags[k] = FmAlgorithms::flags[alg][k];
            args.fixed[k] = patch[(size_t) k].fixed;
        }
        args.fbSrc = std::max (0, fbSrc);
        args.fbDest = fbDest;
        args.fbScale = (float) fbScale;
        const auto useAvx2 = simdActive();

        // Control común (LFO, su retardo y pitch bend) avanzado n muestras de operador
        auto control = [&] (int n, double& pitchMul, double& ampDepth)
        {
            lfoPhase += lfoInc * n;
            if (lfoPhase >= 1.0)
            {
                lfoPhase -= std::floor (lfoPhase);
                shState = (shState * 179 + 17) & 0xff;
            }
            double lfo; // 0..1
            switch (lfoWave)
            {
                case 0:  lfo = lfoPhase < 0.5 ? 2.0 * lfoPhase : 2.0 - 2.0 * lfoPhase; break; // triángulo
                case 1:  lfo = 1.0 - lfoPhase; break;                                         // sierra descendente
                case 2:  lfo = lfoPhase; break;                                               // sierra ascendente
                case 3:  lfo = lfoPhase < 0.5 ? 1.0 : 0.0; break;                             // cuadrada
                case 4:  lfo = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * lfoPhase); break;
                default: lfo = ((shState ^ 0x80) + 1) / 256.0; break;                        // S&H
            }
            if (lfoDelay < 4294967296.0)
                lfoDelay += (lfoDelay < 2147483648.0 ? delayInc1 : delayInc2) * n;
            const auto delayGain = lfoDelay < 2147483648.0 ? 0.0 : std::min (1.0, (lfoDelay - 2147483648.0) / 2147483648.0);
            int pitchCtl = 0, ampCtl = 0, egCtl = anyEg ? 0 : 127;
            for (size_t c = 0; c < 4; ++c)
            {
                const auto total = (int) (ctlValue[c] * (0.01f * (float) ctlRange[c]));
                if (ctlPitch[c]) pitchCtl = std::max (pitchCtl, total);
                if (ctlAmp[c])   ampCtl = std::max (ampCtl, total);
                if (ctlEg[c])    egCtl = std::max (egCtl, total);
            }
            // Tono: Δoct = PMD·PMS·retardo·(lfo - 0,5)/2^15 (máximo ≈ 0,99 oct). Los controladores PITCH suben la profundidad
            // sin pasar por el retardo: equivalen a 2·total (dx7note.cc: pmod_2 = pitch_mod·senslfo >> 14, frente a pmod_1
            // >> 39 con PMD en Q32), y gana la mayor
            const auto pitchOct = std::max (pmd * delayGain, 2.0 * pitchCtl) * pms * (lfo - 0.5) / 32768.0;
            // Amplitud: profundidad 0..1 (AMD·retardo·LFO); prepareVoice la convierte en ganancia lineal (ver amModDepth).
            // Controladores AMP: total/128 por el LFO, sin retardo; EG BIAS: atenuación fija 1 - (total + 1)/128 (con el
            // controlador a 0, casi completa). Gana la mayor (dx7note.cc: amod_1, amod_2, amod_3)
            ampDepth = std::max ({ amd / 255.0 * delayGain * (1.0 - lfo), ampCtl / 128.0 * (1.0 - lfo),
                                   1.0 - (egCtl + 1) / 128.0 });
            bendSmoothed += (1.0f - std::pow (1.0f - bendCoef, (float) n)) * (p.pitchBend.load() - bendSmoothed);
            pitchMul = std::exp2 (pitchOct + (tune + 100.0 * bendSmoothed) / 1200.0);
        };

        auto midiIt = midi.cbegin();
        const auto numSamples = out.getNumSamples();
        if (! hwMode)
        {
            // Clean: subbloques de hasta 16 muestras, cortados en cada evento MIDI. Como msfa (bloques de 64), EG, LFO y
            // ganancias se calculan por subbloque y la ganancia se interpola en rampa dentro de él
            float block[kSubBlock];
            for (int i = 0; i < numSamples;)
            {
                for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
                    handleMidi ((*midiIt).getMessage());
                auto n = std::min (kSubBlock, numSamples - i);
                if (midiIt != midi.cend())
                    n = std::min (n, (*midiIt).samplePosition - i);
                double pitchMul, ampDepth;
                control (n, pitchMul, ampDepth);
                portaTicks (n);
                args.pitchMul = (float) pitchMul;
                for (size_t vi = 0; vi < voices.size(); ++vi)
                    prepareVoice (voices[vi], vi, alg, ampDepth, n);
                for (int g = 0; g < kGroups; ++g)
                {
                    if (g > 0 && ! groupActive (g)) continue; // el segundo grupo solo cuesta con más de 8 voces sonando
                    float part[kSubBlock];
                    auto* dst = g == 0 ? block : part;
                    if (useAvx2) konisimd::fmRenderAvx2 (laneGroups[(size_t) g], args, n, dst);
                    else         fmRenderScalar (laneGroups[(size_t) g], args, n, dst);
                    if (g > 0) for (int k = 0; k < n; ++k) block[k] += part[k];
                }
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    for (int k = 0; k < n; ++k)
                        out.addSample (ch, i + k, 0.1f * block[k]);
                i += n;
            }
            return;
        }

        // DX7: muestras internas a 49 096 Hz en instantes no enteros de la salida (paso fs/49 096). Cada cambio del DAC entra
        // como escalón en hwBlep (la retención ZOH reconstruida con banda limitada, como al grabar la salida analógica) y
        // después pasa por el paso bajo de salida
        int carriers = 0;
        for (int k = 0; k < 6; ++k)
            carriers += (args.flags[k] & 3) == 0 ? 1 : 0;
        const auto com = (int) std::lround (1024.0 * std::log2 ((double) std::max (1, carriers)));
        const auto step = sampleRate / kDx7Rate;
        for (int start = 0; start < numSamples; start += kHwChunk)
        {
            const auto n = std::min (kHwChunk, numSamples - start);
            while (hwNext < n)
            {
                for (; midiIt != midi.cend() && (*midiIt).samplePosition <= start + (int) hwNext; ++midiIt)
                    handleMidi ((*midiIt).getMessage());
                auto k = std::max (1, std::min (kSubBlock, (int) std::ceil ((n - hwNext) / step)));
                if (midiIt != midi.cend() && (*midiIt).samplePosition < start + n)
                    k = juce::jlimit (1, k, (int) std::ceil (((*midiIt).samplePosition - start - hwNext) / step));
                double pitchMul, ampDepth;
                control (k, pitchMul, ampDepth);
                portaTicks (k);
                args.pitchMul = (float) pitchMul;
                for (size_t vi = 0; vi < voices.size(); ++vi)
                    prepareVoice (voices[vi], vi, alg, ampDepth, k);
                prepareAttenuation (k);
                for (int j = 0; j < k; ++j)
                {
                    const auto v = 0.1 * dx7Sample (args, com) / 8192.0;
                    if (v != hwLast)
                    {
                        hwBlep.addDelta (juce::jlimit (0.0, (double) n - 1.0e-6, hwNext), v - hwLast);
                        hwLast = v;
                    }
                    hwNext += step;
                }
            }
            hwNext -= n;
            float block[kHwChunk];
            hwBlep.read (n, block);
            for (int k = 0; k < n; ++k)
            {
                const auto y = (float) hwLpf.process (block[k]);
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, start + k, y);
            }
        }
    }

    // Rampa de la envolvente en el dominio logarítmico del chip (1/1024 de octava) para el subbloque de k muestras
    void prepareAttenuation (int k)
    {
        const auto toAtt = [] (float amp) // amplitud (2 = máximo) -> atenuación en 1/1024 de octava
        {
            return amp <= 1.0e-9f ? 16383.0f : juce::jlimit (0.0f, 16383.0f, -1024.0f * std::log2 (amp * 0.5f));
        };
        for (int g = 0; g < kGroups; ++g)
        {
        auto& lanes = laneGroups[(size_t) g];
        auto& hwAtt = hwAttG[(size_t) g];
        auto& hwAttStep = hwAttStepG[(size_t) g];
        for (int op = 0; op < 6; ++op)
            for (int u = 0; u < konisimd::kLanes; ++u)
            {
                const auto a0 = lanes.amp[op][u];
                const auto a1 = std::max (0.0f, a0 + lanes.ampStep[op][u] * (float) k);
                const auto att0 = toAtt (a0), att1 = toAtt (a1);
                hwAtt[(size_t) op][(size_t) u] = att0;
                hwAttStep[(size_t) op][(size_t) u] = (att1 - att0) / (float) k;
                lanes.amp[op][u] = a1; // el estado lineal avanza aquí (el núcleo DX7 trabaja con la atenuación)
            }
        }
    }

    // ROMs del OPS (Shirriff): log-sin de 1/4 de onda (14 bits) y exp de 12 bits (2048..4093)
    static const std::array<int, 1024>& logSinRom()
    {
        static const auto rom = []
        {
            std::array<int, 1024> t {};
            for (int n = 0; n < 1024; ++n)
                t[(size_t) n] = (int) std::lround (-1024.0 * std::log2 (std::sin ((n + 0.5) / 1024.0 * juce::MathConstants<double>::halfPi)));
            return t;
        }();
        return rom;
    }

    static const std::array<int, 1024>& expRom()
    {
        static const auto rom = []
        {
            std::array<int, 1024> t {};
            for (int n = 0; n < 1024; ++n)
                t[(size_t) n] = (int) std::lround (2048.0 * std::exp2 ((1023 - n) / 1024.0));
            return t;
        }();
        return rom;
    }

    // DAC flotante del DX7: 15 bits con signo -> 12 bits de mantisa con escala ÷1/2/4/8 según los ceros iniciales
    static int dacQuantize (int v)
    {
        v = juce::jlimit (-16384, 16383, v);
        const auto m = v < 0 ? ~v : v;
        const auto shift = m < 512 ? 0 : m < 1024 ? 1 : m < 2048 ? 2 : 3;
        return (v >> shift) * (1 << shift);
    }

    // Una muestra interna: 16 voces × 6 operadores en aritmética entera del OPS; devuelve la suma de las voces tras el DAC
    int dx7Sample (const konisimd::FmArgs& a, int com)
    {
        const auto& logSin = logSinRom();
        const auto& expTab = expRom();
        int total = 0;
        for (int g = 0; g < kGroups; ++g)
        {
        auto& lanes = laneGroups[(size_t) g];
        auto& hwAtt = hwAttG[(size_t) g];
        auto& hwAttStep = hwAttStepG[(size_t) g];
        auto& hwY1 = hwY1G[(size_t) g];
        auto& hwY2 = hwY2G[(size_t) g];
        for (int u = 0; u < konisimd::kLanes; ++u)
        {
            bool live = false;
            for (int k = 0; k < 6; ++k)
                live = live || hwAtt[(size_t) k][(size_t) u] < 16383.0f || hwAttStep[(size_t) k][(size_t) u] < 0.0f;
            if (! live)
                continue;
            int bus[3] = { 0, 0, 0 };
            for (int k = 0; k < 6; ++k)
            {
                const int f = a.flags[k], in = (f >> 4) & 3, ob = f & 3;
                auto mod = in != 0 ? bus[in] : 0;
                if (k == a.fbDest && a.fbScale > 0.0f)
                    mod += (int) ((hwY1[(size_t) a.fbSrc][(size_t) u] + hwY2[(size_t) a.fbSrc][(size_t) u]) * 0.5f * a.fbScale);
                // Fase de 12 bits: la modulación (8192 = 1 ciclo) se suma a la fase antes de truncar
                const auto ph = (double) lanes.phase[k][u] + mod / 8192.0;
                const auto p12 = (int) std::floor (ph * 4096.0) & 4095;
                auto idx = p12 & 1023;
                if (p12 & 1024) idx = 1023 - idx;
                auto att = logSin[(size_t) idx] + (int) hwAtt[(size_t) k][(size_t) u] + (ob == 0 ? com : 0);
                const auto mag = att >= 16384 ? 0 : ((expTab[(size_t) (att & 1023)] << 2) >> (att >> 10));
                const auto y = (p12 & 2048) ? -mag : mag;
                hwY2[(size_t) k][(size_t) u] = hwY1[(size_t) k][(size_t) u];
                hwY1[(size_t) k][(size_t) u] = y;
                const auto next = lanes.phase[k][u] + lanes.inc[k][u] * (a.fixed[k] ? 1.0f : a.pitchMul);
                lanes.phase[k][u] = next - std::floor (next);
                hwAtt[(size_t) k][(size_t) u] += hwAttStep[(size_t) k][(size_t) u];
                bus[ob] = (ob == 0 || (f & 4) != 0) ? bus[ob] + y : y;
            }
            total += dacQuantize (bus[0]);
        }
        }
        return total;
    }

private:
    static constexpr int kSubBlock = 16, kHwChunk = 256;
    static constexpr double kDx7Rate = 49096.0;

    struct OpPatch
    {
        int rates[4] {}, levels[4] {}, outLevel = 99, velSens = 0, rateScale = 0, ams = 0;
        int coarse = 1, fine = 0, detune = 0, breakPoint = 39, leftDepth = 0, rightDepth = 0, leftCurve = 0, rightCurve = 0;
        bool fixed = false;
    };

    struct Env
    {
        double level = 0.0, target = 0.0, inc = 0.0; // unidades de 2^-8 octava
        int ix = 0;
        bool rising = false;
    };

    // Estado de control de un operador; fase, ganancia y salidas viven en 'lanes' (un carril por voz)
    struct Op
    {
        Env env;
        int outlevel = 0, rateScaling = 0;
    };

    // Envolvente de tono del DX7 (pitchenv.cc de msfa): nivel en unidades de 2^-24 octava, 4 etapas como la EG de amplitud
    struct PitchEnv
    {
        double level = 0.0, target = 0.0, inc = 0.0;
        int ix = 0;
        bool rising = false, down = true;
    };

    struct Voice
    {
        bool active = false, keyDown = false, sustained = false;
        int note = 60, velocity = 100;
        int playNote = 60; // nota que suena: la MIDI más el transpose de la voz (note sigue siendo la MIDI, para el note off)
        std::uint64_t age = 0;
        std::array<Op, 6> ops; // índice 0 = OP6 ... 5 = OP1, como en fm_core.cc
        PitchEnv pitchEnv;
        std::array<float, 6> baseInc {}; // incremento de fase sin envolvente de tono
        // Portamento (unidades de 1/4096 oct del firmware): tono actual, del glissando, destino y el que suena. 0x2EA8 = nota
        // 36, el valor con que el firmware llena los buffers al arrancar (VOICE_RESET_EVENT_AND_PITCH_BUFFERS)
        int portaPitch = 0x2EA8, glissPitch = 0x2EA8, targetPitch = 0x2EA8, portaOut = 0x2EA8;
    };

    static int scaleOutLevel (int ol)
    {
        static constexpr int lut[20] = { 0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46 };
        return ol >= 20 ? 28 + ol : lut[juce::jlimit (0, 19, ol)];
    }

    static int scaleVelocity (int velocity, int sensitivity)
    {
        static constexpr std::uint8_t data[64] = {
            0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
            166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202,
            205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232,
            233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251,
            252, 253, 254 };
        const auto v = (int) data[juce::jlimit (0, 127, velocity) >> 1] - 239;
        return ((sensitivity * v + 7) >> 3) << 4;
    }

    static int scaleCurve (int group, int depth, int curve)
    {
        static constexpr std::uint8_t expData[33] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
                                                       80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250 };
        auto scale = (curve == 0 || curve == 3) ? (group * depth * 329) >> 12                          // lineal
                                                : (expData[std::min (group, 32)] * depth * 329) >> 15; // exponencial
        return curve < 2 ? -scale : scale; // 0 -LIN, 1 -EXP, 2 +EXP, 3 +LIN
    }

    static int scaleLevel (int note, const OpPatch& op)
    {
        const auto offset = note - op.breakPoint - 17;
        return offset >= 0 ? scaleCurve ((offset + 1) / 3, op.rightDepth, op.rightCurve)
                           : scaleCurve (-(offset - 1) / 3, op.leftDepth, op.leftCurve);
    }

    void readPatch (const Params& p)
    {
        for (int k = 0; k < 6; ++k)
        {
            const auto& src = p.fmOp[(size_t) k]; // k = 0 -> OP1
            auto& op = patch[(size_t) (5 - k)];   // índice de motor: 0 = OP6
            for (int s = 0; s < 4; ++s)
            {
                op.rates[s] = (int) src.rate[(size_t) s]->load();
                op.levels[s] = (int) src.level[(size_t) s]->load();
            }
            op.outLevel = (int) src.output->load();
            op.velSens = (int) src.velSens->load();
            op.rateScale = (int) src.rateScale->load();
            op.ams = (int) src.ams->load();
            op.coarse = (int) src.coarse->load();
            op.fine = (int) src.fine->load();
            op.detune = (int) src.detune->load();
            op.fixed = src.fixed->load() > 0.5f;
            op.breakPoint = (int) src.breakPoint->load() - 21; // parámetro en nota MIDI; DX7: 0 = A-1 (MIDI 21)
            op.leftDepth = (int) src.leftDepth->load();
            op.rightDepth = (int) src.rightDepth->load();
            op.leftCurve = (int) src.leftCurve->load();
            op.rightCurve = (int) src.rightCurve->load();
        }
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            if (mono) monoNoteOn (m.getNoteNumber(), m.getVelocity());
            else      noteOn (m.getNoteNumber(), m.getVelocity());
        }
        else if (m.isNoteOff() && mono)
        {
            monoNoteOff (m.getNoteNumber());
        }
        else if (m.isController() && m.getControllerNumber() == 5)
        {
            // MIDI_RX_CC_5_PORTAMENTO_TIME: el valor llega duplicado (ASLA) y se escala ·100/256 -> 0-99
            midiPortaTime = (2 * m.getControllerValue() * 100) >> 8;
        }
        else if (m.isController() && m.getControllerNumber() == 65)
        {
            // MIDI_RX_CC_65_PORTAMENO: cualquier valor distinto de 0 pisa el pedal (no >= 64 como en la norma MIDI)
            portaPedalCc = m.getControllerValue() != 0;
        }
        else if (m.isNoteOff())
        {
            for (auto& v : voices)
                if (v.active && v.keyDown && v.note == m.getNoteNumber())
                {
                    v.keyDown = false;
                    if (sustainPedal) v.sustained = true;
                    else              keyUp (v);
                }
        }
        else if (m.isController() && (m.getControllerNumber() == 1 || m.getControllerNumber() == 2 || m.getControllerNumber() == 4))
        {
            // Controladores del modo Function: rueda (CC 1), breath (CC 2), foot (CC 4)
            const auto cc = m.getControllerNumber();
            ctlValue[cc == 1 ? 0 : cc == 4 ? 1 : 2] = m.getControllerValue();
        }
        else if (m.isChannelPressure())
        {
            ctlValue[3] = m.getChannelPressureValue();
        }
        else if (m.isSustainPedalOn())
        {
            sustainPedal = true;
        }
        else if (m.isSustainPedalOff())
        {
            sustainPedal = false;
            for (auto& v : voices)
                if (v.active && v.sustained)
                {
                    v.sustained = false;
                    keyUp (v);
                }
        }
        else if (m.isAllNotesOff())
        {
            for (auto& v : voices)
                if (v.active && v.keyDown) { v.keyDown = false; keyUp (v); }
            monoCount = 0;
        }
        else if (m.isAllSoundOff())
        {
            reset();
        }
    }

    void noteOn (int note, int velocity)
    {
        // Voz libre, o la misma nota, o la más antigua
        Voice* target = nullptr;
        for (auto& v : voices)
            if (v.active && v.note == note) target = &v;
        if (target == nullptr)
            for (auto& v : voices)
                if (! v.active) { target = &v; break; }
        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.age < target->age) target = &v;
        }
        auto& v = *target;
        startVoice (v, note, velocity);
        // Portamento en poly (VOICE_ADD_POLY): solo con el pedal pisado y tiempo > 0. Retain: la nota nueva arranca en el tono
        // de la última tocada. Follow: todas las voces sin tecla pulsada (sostenidas o en release) toman la nota nueva como
        // destino; la nueva sigue desde el tono que tenía esa voz, y la "última nota" no cambia
        const auto targetPitch = dxPitch (v.playNote);
        const auto glide = portaPedal() && portaRate != 0xFF;
        if (glide && portaMode != 0)
        {
            for (auto& o : voices)
                if (! o.keyDown || &o == &v)
                    o.targetPitch = targetPitch;
            return;
        }
        if (! glide)
            lastPitch = targetPitch;
        v.portaPitch = v.glissPitch = v.portaOut = lastPitch;
        v.targetPitch = targetPitch;
        lastPitch = targetPitch;
    }

    // Arranque de una nota en la voz v (EG, pitch EG, LFO, frecuencias), común a poly y mono
    void startVoice (Voice& v, int note, int velocity)
    {
        const auto vi = (size_t) (&v - voices.data());
        const auto wasActive = v.active;
        // LFO (lfo.cc keydown, llamado si no había otra tecla pulsada): el retardo vuelve a empezar y, con LFO key sync, la
        // fase va a (2^31 - 1) / 2^32, la mitad del ciclo
        if (std::none_of (voices.begin(), voices.end(), [] (const Voice& o) { return o.active && o.keyDown; }))
        {
            lfoDelay = 0.0;
            if (lfoSync)
                lfoPhase = 2147483647.0 / 4294967296.0;
        }
        v.active = v.keyDown = true;
        v.sustained = false;
        v.note = note;
        // Transpose de la voz (VCED 144, 24 = sin cambio): la nota transpuesta es la que usan frecuencia, level scaling y rate
        // scaling (Dexed lo suma a la nota antes de Dx7Note::init)
        v.playNote = juce::jlimit (0, 127, note + transpose);
        v.velocity = velocity;
        v.age = ++ageCounter;
        for (int k = 0; k < 6; ++k)
        {
            auto& op = v.ops[(size_t) k];
            const auto& pk = patch[(size_t) k];
            auto ol = std::min (127, scaleOutLevel (pk.outLevel) + scaleLevel (v.playNote, pk));
            op.outlevel = std::max (0, (ol << 5) + scaleVelocity (velocity, pk.velSens));
            op.rateScaling = (pk.rateScale * juce::jlimit (0, 31, v.playNote / 3 - 7)) >> 3;
            if (! wasActive)
            {
                op.env = {};
                lanesOf (vi).amp[k][laneOf (vi)] = lanesOf (vi).y1[k][laneOf (vi)] = lanesOf (vi).y2[k][laneOf (vi)] = 0.0f;
            }
            if (oscSync) // OSC KEY SYNC: fase a 0 en cada nota; sin él, los osciladores siguen libres
                lanesOf (vi).phase[k][laneOf (vi)] = 0.0f;
            advance (op, pk, 0);
        }
        // Envolvente de tono: empieza en L4 y va hacia L1 (PitchEnv::set en cada Dx7Note::init)
        v.pitchEnv = {};
        v.pitchEnv.level = pitchEnvTab[pitchLevels[3]] * 524288.0; // << 19
        pitchAdvance (v.pitchEnv, 0);
        updateFrequencies (v);
    }

    // ---- Mono y portamento: firmware del DX7 (desensamblado anotado de la v1.8 de ajxs; las mismas instrucciones y tablas
    // están en la mask ROM original YA2138, comprobado byte a byte). El tono interno va en unidades de 1/4096 de octava ----

    // VOICE_CONVERT_NOTE_TO_LOG_FREQ: MSB de TABLE_MIDI_KEY_TO_LOG_F = (n - 1) + (n - 1)/3 (los MSB ≡ 3 mod 4 no se usan:
    // 16 por octava para 12 semitonos), y el LSB repite los 2 bits bajos del MSB. Semitonos de 340, 340 y 344 unidades
    // (desviación máxima 0,4 cents frente a la escala igual)
    static int dxPitchFromMsb (int msb)
    {
        const auto a = msb & 3;
        int lsb = a;
        for (int i = 0; i < 3; ++i)
            lsb = ((lsb | a) << 2) & 0xFF;
        return ((msb & 0xFF) << 8) | (lsb & 0xFC);
    }

    static int dxPitch (int note)
    {
        const auto n = juce::jlimit (0, 127, note);
        return dxPitchFromMsb (n == 0 ? 0 : (n - 1) + (n - 1) / 3);
    }

    bool portaPedal() const { return portaPedalParam || portaPedalCc; }

    // VOICE_ADD_MONO: la voz 0 hace de única voz. La primera tecla reinicia la nota; con más teclas pulsadas no hay nuevo
    // ataque (legato): solo cambia el tono destino. Con 3 o más teclas, solo cuenta una tecla que siga la dirección del
    // legato (más aguda si subía, más grave si bajaba)
    void monoNoteOn (int note, int velocity)
    {
        if (monoCount >= kVoices)
            return;
        monoKeys[(size_t) monoCount++] = note;
        auto& v = voices[0];
        if (monoCount == 1)
        {
            startVoice (v, note, velocity);
            lastKey = note;
            v.targetPitch = dxPitch (v.playNote);
            // Full Time con el pedal: desliza desde el tono anterior. Fingered nunca desliza con una sola tecla
            if (! (portaRate != 0xFF && portaMode != 0 && portaPedal()))
                v.portaPitch = v.glissPitch = v.targetPitch;
            v.portaOut = v.targetPitch; // el EGS recibe la nota nueva; PORTA_PROCESS lo corrige en su siguiente pasada
            return;
        }
        if (monoCount == 2)
            legatoUp = note >= lastKey;
        else if (legatoUp ? note < lastKey : note >= lastKey)
            return;
        monoGlideTo (note);
    }

    // VOICE_REMOVE_MONO: si quedan teclas, el destino pasa a la más aguda (legato hacia arriba) o a la más grave (hacia
    // abajo); con la última suelta, key off salvo que el sustain esté pisado
    void monoNoteOff (int note)
    {
        int i = 0;
        while (i < monoCount && monoKeys[(size_t) i] != note) ++i;
        if (i == monoCount)
            return;
        for (; i < monoCount - 1; ++i)
            monoKeys[(size_t) i] = monoKeys[(size_t) i + 1];
        --monoCount;
        auto& v = voices[0];
        if (monoCount == 0)
        {
            v.keyDown = false;
            if (sustainPedal) v.sustained = true;
            else              keyUp (v);
            return;
        }
        auto k = monoKeys[0];
        for (int j = 1; j < monoCount; ++j)
            if (legatoUp ? monoKeys[(size_t) j] >= k : monoKeys[(size_t) j] < k)
                k = monoKeys[(size_t) j];
        monoGlideTo (k);
    }

    // Nuevo destino de la voz mono. Fingered desliza sin pedal; Full Time, solo con el pedal; tiempo 0, nunca
    void monoGlideTo (int key)
    {
        auto& v = voices[0];
        lastKey = key;
        v.targetPitch = dxPitch (key + transpose);
        if (portaRate == 0xFF || (portaMode != 0 && ! portaPedal()))
            v.portaPitch = v.glissPitch = v.targetPitch; // VOICE_ADD_MONO_CLEAR_PORTA_FREQ
    }

    // HANDLER_OCF llama a PORTA_PROCESS en cada tick del temporizador, alternando las voces 0-7 y 8-15: cada voz se actualiza
    // a kTickHz / 2. kTickHz = 369,7 Hz: el temporizador cuenta 3140 ciclos (SYSTEM_TICK_PERIOD) y su periodo real se ha
    // medido por la velocidad del LFO, que avanza un paso por tick, en demos de tres DX7 (10 velocidades, 368,8-371,2 Hz)
    static constexpr double kTickHz = 369.7;

    void portaTicks (int n)
    {
        portaTickPhase += n * kTickHz / opRate;
        while (portaTickPhase >= 1.0)
        {
            portaTickPhase -= 1.0;
            const auto first = portaHalf ? 8 : 0;
            portaHalf = ! portaHalf;
            for (int i = first; i < first + 8; ++i)
                portaStep (voices[(size_t) i]);
        }
    }

    // PORTA_PROCESS para una voz, con su aritmética de 16 bits: paso = velocidad · ((MSB de la distancia >> 2) + 1), es decir,
    // velocidad · (distancia/1024 + 1): más rápido en saltos grandes, constante en los últimos 3 semitonos. Con glissando el
    // paso es velocidad · 3 y lo que suena salta de semitono en semitono (umbral 0x154)
    void portaStep (Voice& v) const
    {
        const auto rate = (unsigned) portaRate;
        auto fin = (unsigned) v.targetPitch & 0xFFFF;
        const auto cur = (unsigned) v.portaPitch & 0xFFFF;
        const auto diff = (cur - fin) & 0xFFFF;
        const auto above = (diff & 0x8000) == 0;
        if (above)
        {
            const auto step = rate * (glissando ? 3u : ((diff >> 8) >> 2) + 1u);
            auto nw = (cur - step) & 0xFFFF;
            if (nw < fin) nw = fin;
            fin = nw;
        }
        else
        {
            const auto negMsb = (0x100u - (diff >> 8)) & 0xFF;
            const auto step = rate * (glissando ? 3u : (negMsb >> 2) + 1u);
            auto nw = (cur + step) & 0xFFFF;
            if (nw >= fin) nw = fin;
            fin = nw;
        }
        v.portaPitch = (int) fin;
        if (! glissando)
        {
            v.portaOut = v.portaPitch;
            return;
        }
        // Glissando: el tono que suena (glissPitch) da un semitono cuando el portamento se aleja de él 0x154 o más
        auto g = (unsigned) v.glissPitch & 0xFFFF;
        const auto next = [] (unsigned x)
        {
            auto msb = (int) (x >> 8) & 0xFF;
            if (((msb + 1) & 3) == 0) msb = (msb + 1) & 0xFF; // salta los MSB ≡ 3 mod 4
            return (unsigned) dxPitchFromMsb (msb);
        };
        const auto gDiff = (g - fin) & 0xFFFF;
        if (above && (gDiff & 0x8000) == 0)
        {
            if ((((gDiff - 0x154) & 0xFFFF) & 0x8000) == 0)
                g = next ((g - 0x154) & 0xFFFF);
        }
        else
        {
            const auto up = (fin - g) & 0xFFFF;
            if ((((up - 0x154) & 0xFFFF) & 0x8000) == 0)
                g = next ((g + 0x155) & 0xFFFF);
        }
        v.glissPitch = (int) g;
        v.portaOut = v.glissPitch;
    }

    // Tablas de pitchenv.cc (msfa): velocidad por rate 0..99 y nivel por level 0..99 (en 2^-5 octava; 50 = 0)
    static constexpr std::uint8_t pitchEnvRate[100] = {
        1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20,
        21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56, 58, 60, 62, 64,
        66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141, 147, 153, 159, 165,
        171, 178, 185, 193, 202, 211, 232, 243, 254, 255 };
    static constexpr std::int8_t pitchEnvTab[100] = {
        -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31, -30, -29, -28,
        -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3,
        -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
        30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127 };

    // PitchEnv::advance: objetivo y velocidad de la etapa ix (0..3; 4 = fin). Velocidad por muestra de operador:
    // pitchenv_rate · 2^24 / (21,3 · fs) (msfa la aplica cada 64 muestras con unit_ = 64 · 2^24 / (21,3 · fs))
    void pitchAdvance (PitchEnv& e, int ix) const
    {
        e.ix = ix;
        if (ix >= 4)
            return;
        e.target = pitchEnvTab[pitchLevels[(size_t) ix]] * 524288.0;
        e.rising = e.target > e.level;
        e.inc = pitchEnvRate[pitchRates[(size_t) ix]] * 16777216.0 / (21.3 * opRate);
    }

    // PitchEnv::getsample avanzando n muestras; devuelve octavas
    double pitchStep (PitchEnv& e, int n) const
    {
        if (e.ix < 3 || (e.ix < 4 && ! e.down))
        {
            if (e.rising)
            {
                e.level += e.inc * n;
                if (e.level >= e.target) { e.level = e.target; pitchAdvance (e, e.ix + 1); }
            }
            else
            {
                e.level -= e.inc * n;
                if (e.level <= e.target) { e.level = e.target; pitchAdvance (e, e.ix + 1); }
            }
        }
        return e.level / 16777216.0;
    }

    // Frecuencia de cada operador (dx7note.cc osc_freq): ratio = coarse (0 = 0,5) · (1 + fine/100) con detune empírico de Dexed;
    // fixed = 10^(coarse%4 + fine/100) Hz. Por bloque y en cada nota: el patch puede cambiar mientras suena
    void updateFrequencies (Voice& v)
    {
        const auto base = 440.0 * std::exp2 ((v.playNote - 69) / 12.0) * KoniTuning::ratio (v.playNote);
        const auto l = std::log2 (base);
        const auto detuneStep = 0.0209 * std::exp (-0.396 * l) / 7.0 * l;
        for (int k = 0; k < 6; ++k)
        {
            const auto& pk = patch[(size_t) k];
            const auto hz = pk.fixed ? std::pow (10.0, (pk.coarse & 3) + pk.fine / 100.0) * std::exp2 (std::max (0, pk.detune) * 13457.0 / 16777216.0)
                                     : base * std::exp2 (detuneStep * pk.detune) * (pk.coarse == 0 ? 0.5 : (double) pk.coarse) * (1.0 + pk.fine / 100.0);
            const auto vi = (size_t) (&v - voices.data());
            v.baseInc[(size_t) k] = (float) (hz / opRate);
            lanesOf (vi).inc[k][laneOf (vi)] = v.baseInc[(size_t) k];
        }
    }

    void keyUp (Voice& v)
    {
        for (int k = 0; k < 6; ++k)
            advance (v.ops[(size_t) k], patch[(size_t) k], 3);
        if (v.pitchEnv.down) // PitchEnv::keydown (false): a la etapa 4
        {
            v.pitchEnv.down = false;
            pitchAdvance (v.pitchEnv, 3);
        }
    }

    // env.cc advance(): objetivo y velocidad de la etapa ix (0..3); ix 4 = fin
    void advance (Op& op, const OpPatch& pk, int ix)
    {
        auto& e = op.env;
        e.ix = ix;
        if (ix >= 4)
            return;
        auto actual = ((scaleOutLevel (pk.levels[ix]) >> 1) << 6) + op.outlevel - 4256;
        e.target = (double) std::max (16, actual);
        e.rising = e.target > e.level;
        const auto qrate = std::min (63, ((pk.rates[ix] * 41) >> 6) + op.rateScaling);
        e.inc = (4 + (qrate & 3)) * std::exp2 ((double) (qrate >> 2) - 14.0) * srScale;
    }

    // env.cc getsample(), avanzando n muestras de una vez (msfa avanza 64): etapas 0..2 con la tecla pulsada (mantiene L3),
    // 3 tras soltarla
    double envStep (Op& op, const OpPatch& pk, bool down, int n)
    {
        auto& e = op.env;
        if (e.ix < 3 || (e.ix < 4 && ! down))
        {
            if (e.rising)
            {
                e.level = std::max (e.level, 1716.0);
                e.level += std::floor (17.0 - e.level / 256.0) * e.inc * n;
                if (e.level >= e.target) { e.level = e.target; advance (op, pk, e.ix + 1); }
            }
            else
            {
                e.level -= e.inc * n;
                if (e.level <= e.target) { e.level = e.target; advance (op, pk, e.ix + 1); }
            }
        }
        return e.level;
    }

    // EG del subbloque y rampa de ganancia de cada operador de la voz vi; voz libre: ganancia 0 (el núcleo escalar la salta)
    void prepareVoice (Voice& v, size_t vi, int alg, double ampDepth, int n)
    {
        if (! v.active)
        {
            for (int k = 0; k < 6; ++k)
                lanesOf (vi).amp[k][laneOf (vi)] = lanesOf (vi).ampStep[k][laneOf (vi)] = 0.0f;
            return;
        }
        static constexpr double amsTab[4] = { 0.0, 66.0 / 255.0, 109.0 / 255.0, 1.0 }; // ampmodsenstab de msfa
        static constexpr double amModDepth = 0.94988;                                   // 1 - 10^(-26/20)
        const auto down = v.keyDown || v.sustained;
        // Envolvente de tono del subbloque: solo en los operadores en modo razón (dx7note.cc: los de frecuencia fija solo
        // siguen el pitch bend)
        // El portamento desplaza el tono respecto a la nota de la voz (en mono, la primera tecla del legato); como la envolvente
        // de tono, solo en los operadores en modo razón
        const auto portaOct = (v.portaOut - dxPitch (v.playNote)) / 4096.0;
        const auto pitchMul = (float) std::exp2 (pitchStep (v.pitchEnv, n) + portaOct);
        for (int k = 0; k < 6; ++k)
            lanesOf (vi).inc[k][laneOf (vi)] = patch[(size_t) k].fixed ? v.baseInc[(size_t) k] : v.baseInc[(size_t) k] * pitchMul;
        bool anyAudible = false;
        std::array<float, 6> target {};
        for (int k = 0; k < 6; ++k)
        {
            auto& op = v.ops[(size_t) k];
            const auto& pk = patch[(size_t) k];
            auto level = envStep (op, pk, down, n);
            // Modulación de amplitud lineal en amplitud: g = 1 - amModDepth·profundidad·AMS. Medido en tres DX7 (voz TRAIN de
            // la ROM 1A, AMD 99, AMS 3, LFO triangular): la forma lineal en amplitud ajusta mejor que la lineal en dB en los
            // tres (error rms 0,79-1,10 dB frente a 1,23-2,54 dB) y el valle sin efectos es de 23 y 30 dB: se toma 26 dB,
            // amModDepth = 1 - 10^(-26/20). Grabaciones: YouTube FRghocE4pPA (872-896 s), W_xLKRBeuCU (2402-2478 s) y
            // F3rrjQtQe5A (15:06, con reverb). Solo AMD 99 / AMS 3 está medido; la escala para otros valores (proporcional
            // en amplitud con la tabla de msfa) es un supuesto.
            if (pk.ams > 0 && ampDepth > 0.0)
                level += 256.0 * std::log2 (1.0 - amModDepth * ampDepth * amsTab[pk.ams & 3]); // ganancia -> unidades de 2^-8 oct
            target[(size_t) k] = (float) std::exp2 (level / 256.0 - 14.0);
            if ((FmAlgorithms::flags[alg][k] & 3) == 0 && (op.env.ix < 4 || level > 16.5))
                anyAudible = true;
        }
        if (! anyAudible)
        {
            v.active = false; // última rampa hasta 0
            target.fill (0.0f);
        }
        for (int k = 0; k < 6; ++k)
            lanesOf (vi).ampStep[k][laneOf (vi)] = (target[(size_t) k] - lanesOf (vi).amp[k][laneOf (vi)]) / (float) n;
    }

    double sampleRate = 44100.0, srScale = 1.0, lfoPhase = 0.0, lfoDelay = 0.0;
    int shState = 0;
    int transpose = 0;
    bool oscSync = true, lfoSync = true;
    std::array<int, 4> pitchRates { 99, 99, 99, 99 }, pitchLevels { 50, 50, 50, 50 };
    bool sustainPedal = false;
    std::array<int, 4> ctlValue {}; // últimos valores (0-127) de rueda, foot, breath y aftertouch; persisten tras reset()
    // Mono y portamento (ver monoNoteOn / portaStep)
    bool mono = false, legatoUp = false, glissando = false, portaPedalParam = false, portaPedalCc = false, portaHalf = false;
    int monoCount = 0, lastKey = 0, lastPitch = 0x2EA8, portaRate = 0xFF, portaMode = 0, portaTimeParam = -1, midiPortaTime = -1;
    std::array<int, kVoices> monoKeys {};
    double portaTickPhase = 0.0;
    std::uint64_t ageCounter = 0;
    std::array<Voice, kVoices> voices;
    std::array<OpPatch, 6> patch;
    float bendCoef = 1.0f, bendSmoothed = 0.0f;
    std::array<konisimd::FmLanes, kGroups> laneGroups {}; // alineado a 32: FmEngine vive dentro del procesador (new con alineación de C++17)
    konisimd::FmLanes& lanesOf (size_t vi) { return laneGroups[vi / konisimd::kLanes]; }
    static size_t laneOf (size_t vi) { return vi % konisimd::kLanes; }
    bool groupActive (int g) const
    {
        for (int u = 0; u < konisimd::kLanes; ++u)
            if (voices[(size_t) (g * konisimd::kLanes + u)].active) return true;
        return false;
    }
    // Modo DX7
    bool hwMode = false;
    double opRate = 44100.0, hwNext = 0.0, hwLast = 0.0;
    BlepBuffer hwBlep;
    Biquad hwLpf;
    std::array<std::array<std::array<float, konisimd::kLanes>, 6>, kGroups> hwAttG {}, hwAttStepG {};
    std::array<std::array<std::array<int, konisimd::kLanes>, 6>, kGroups> hwY1G {}, hwY2G {};
};
