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
        // LFO: el estado estable de un aparato encendido (fade al máximo; con retardo, cada nota lo reinicia)
        lfoPh = 0;
        lfoDelayAcc = 0xFFFF;
        lfoFade = 255;
        lfoS = 0;
        ampA = 0;
        pitchReg = 0;
        // VOICE_RESET_EVENT_AND_PITCH_BUFFERS: buffers de tono a 0x2EA8 (nota 36, ya en Voice) y estado mono a cero
        lastPitch = 0x2EA8;
        monoCount = 0;
        legatoUp = false;
        lastKey = 0;
        portaTickPhase = 0.0;
        portaHalf = false;
        pitchEgToggle = false;
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

        // LFO y modulación: los calcula la CPU del DX7 en cada tick del temporizador (fwTick); aquí, sus parámetros.
        // PATCH_ACTIVATE_SCALE_VALUE: sc(v) = (660·v) >> 8 (99 -> 255). Velocidad del LFO (PATCH_ACTIVATE_SCALE_LFO_SPEED):
        // A = sc(velocidad), incremento por tick de la fase de 16 bits = A·11, o A·(((A - 160) >> 2) + 11) desde A = 160
        // (velocidad 0 -> 11). Retardo (PATCH_ACTIVATE_SCALE_LFO_DELAY): x = 99 - delay, incremento
        // (16 + (x & 15)) << (2 + (x >> 4)). PMS por TABLE_PITCH_MOD_SENS
        static constexpr int pmsTab[8] = { 0, 10, 20, 33, 55, 92, 153, 255 };
        const auto sc = [] (int v) { return (660 * juce::jlimit (0, 99, v)) >> 8; };
        {
            const auto speed = juce::roundToInt (p.fmLfoSpeed->load());
            const auto a = sc (speed);
            lfoIncFw = speed == 0 ? 11 : a * (a < 160 ? 11 : ((a - 160) >> 2) + 11);
            lfoDelayParam = juce::jlimit (0, 99, juce::roundToInt (p.fmLfoDelay->load()));
            const auto x = 99 - lfoDelayParam;
            lfoDelayIncFw = (16 + (x & 15)) << (2 + (x >> 4));
        }
        lfoWave = (int) p.fmLfoWave->load();
        pmdSc = sc (juce::roundToInt (p.fmLfoPmd->load()));
        amdSc = sc (juce::roundToInt (p.fmLfoAmd->load()));
        pmsFw = pmsTab[juce::jlimit (0, 7, (int) p.fmLfoPms->load())];
        // Modo Function (MOD_PROCESS_INPUT_SOURCES): cada fuente da q = (sc(rango) · 2·valor) >> 8 (el firmware guarda los
        // controladores duplicados) y las fuentes asignadas a un mismo destino se SUMAN con saturación a 255. EG BIAS:
        // egR = suma saturada de sc(rango) de las fuentes asignadas; bias = 255 - sat(suma de sus q + 255 - egR), es decir,
        // con el controlador a 0 la atenuación es sc(rango) y se abre al subirlo
        {
            int ampSum = 0, pitSum = 0, egRange = 0, egSum = 0;
            for (size_t c = 0; c < 4; ++c)
            {
                const auto r = sc (juce::roundToInt (p.fmCtl[c].range->load()));
                const auto q = (r * 2 * ctlValue[c]) >> 8;
                if (p.fmCtl[c].amp->load() > 0.5f)   ampSum = std::min (255, ampSum + q);
                if (p.fmCtl[c].pitch->load() > 0.5f) pitSum = std::min (255, pitSum + q);
                if (p.fmCtl[c].eg->load() > 0.5f)   { egRange = std::min (255, egRange + r); egSum = std::min (255, egSum + q); }
            }
            ampFactor = ampSum;
            pitchFactor = pitSum;
            egBias = 255 - std::min (255, egSum + (255 - egRange));
        }        const auto tune = (double) p.fineTune->load();
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

        // Control común avanzado n muestras de operador: el registro de tono del EGS (1/16384 de octava; ver fwTick) más el
        // pitch bend y la afinación de KoniSynth; la profundidad de AM A (0-255) que prepareVoice convierte en ganancia
        auto control = [&] (int n, double& pitchMul, double& ampDepth)
        {
            ampDepth = ampA / 256.0;
            bendSmoothed += (1.0f - std::pow (1.0f - bendCoef, (float) n)) * (p.pitchBend.load() - bendSmoothed);
            pitchMul = std::exp2 (pitchReg / 16384.0 + (tune + 100.0 * bendSmoothed) / 1200.0);
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
                fwTicks (n);
                control (n, pitchMul, ampDepth);
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
                fwTicks (k);
                control (k, pitchMul, ampDepth);
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
        // PITCH_EG_PROCESS: nivel de 16 bits en 1/4096 de octava (0x4000 = sin desviación), etapa 0-2 hacia L1-L3, 3 = sostiene
        // L3 con la tecla pulsada, 4 = hacia L4 tras soltarla, 5 = fin. loaded: el nivel que tiene el EGS (se carga en la
        // pasada de PORTA_PROCESS de la voz)
        int level = 0x4000, step = 0, loaded = 0x4000;
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
        lfoKeyOn(); // en poly, en cada nota (msfa/Dexed: solo si no había otra tecla pulsada)
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
        // Envolvente de tono (VOICE_ADD): empieza en L4, etapa 0
        v.pitchEnv = {};
        v.pitchEnv.level = v.pitchEnv.loaded = pitchEgLevel (3);
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
            lfoKeyOn();
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

    // HANDLER_OCF, en cada tick del temporizador: LFO_GET_AMPLITUDE, MOD_AMP_LOAD_TO_EGS, PORTA_PROCESS (alternando las
    // voces 0-7 y 8-15: cada voz a kTickHz / 2) y MOD_PITCH_LOAD_TO_EGS. kTickHz = 369,7 Hz: el temporizador cuenta 3140
    // ciclos (SYSTEM_TICK_PERIOD) y su periodo real se ha medido por la velocidad del LFO, que avanza un paso por tick, en
    // demos de tres DX7 (10 velocidades, 368,8-371,2 Hz)
    static constexpr double kTickHz = 369.7;

    void fwTicks (int n)
    {
        portaTickPhase += n * kTickHz / opRate;
        while (portaTickPhase >= 1.0)
        {
            portaTickPhase -= 1.0;
            lfoTick();
            // MOD_AMP_LOAD_TO_EGS: x = sat(fade·sc(AMD)/256 + controladores AMP); el EG bias satura x por arriba;
            // A = sat(x·(127 - s)/256 + bias). El EGS recibe TABLE_AMP_MOD[255 - A] (≈ -32·log2(1 - A/256)); cómo lo
            // aplica cada AMS lo decide el EGS (ver prepareVoice)
            auto x = std::min (255, ((lfoFade * amdSc) >> 8) + ampFactor);
            x = std::min (255, x + egBias) - egBias;
            ampA = std::min (255, ((x * (127 - lfoS)) >> 8) + egBias);
            const auto first = portaHalf ? 8 : 0;
            portaHalf = ! portaHalf;
            for (int i = first; i < first + 8; ++i)
            {
                auto& v = voices[(size_t) i];
                portaStep (v);
                v.pitchEnv.loaded = v.pitchEnv.level; // PORTA_PROCESS suma el nivel de la envolvente de tono al cargar el EGS
            }
            // PITCH_EG_PROCESS: en ticks alternos (M_PITCH_EG_UPDATE_TOGGLE), las 16 voces
            pitchEgToggle = ! pitchEgToggle;
            if (pitchEgToggle)
                for (auto& v : voices)
                    pitchEgTick (v.pitchEnv);
            // MOD_PITCH_LOAD_TO_EGS: T = sat(controladores PITCH + fade·sc(PMD)/256); l = s·PMS/256 (truncado hacia 0);
            // registro = (l·T) >> 1, en 1/16384 de octava (el pitch bend de 12 semitonos llega a 0x3FFF)
            const auto t = std::min (255, pitchFactor + ((lfoFade * pmdSc) >> 8));
            const auto l = lfoS >= 0 ? (lfoS * pmsFw) >> 8 : -(((-lfoS) * pmsFw) >> 8);
            pitchReg = (l * t) >> 1;
        }
    }

    // LFO_GET_AMPLITUDE: retardo (acumulador de 16 bits; al desbordar, el fade sube max(1, incremento >> 8) por tick hasta
    // 255), fase de 16 bits y la forma de onda como byte con signo s (-128..127)
    void lfoTick()
    {
        if (const auto d = lfoDelayAcc + lfoDelayIncFw; d > 0xFFFF)
        {
            lfoFade = std::min (255, lfoFade + std::max (1, lfoDelayIncFw >> 8));
            lfoDelayAcc = 0xFFFF;
        }
        else
        {
            lfoDelayAcc = d;
        }
        const auto old = lfoPh;
        lfoPh = (lfoPh + lfoIncFw) & 0xFFFF;
        const auto shNew = old < 0x8000 && lfoPh >= 0x8000; // desbordamiento con signo: el S&H toma muestra a mitad de ciclo
        const auto msb = lfoPh >> 8;
        const auto toS8 = [] (int b) { b &= 0xFF; return b > 127 ? b - 256 : b; };
        switch (lfoWave)
        {
            case 0: // triángulo: -128 en fase 0, +127 en fase 0,5 (el key sync deja la fase en 0x7FFF: el pico)
            {
                auto t = ((lfoPh << 1) & 0xFFFF) >> 8;
                if (lfoPh & 0x8000) t = ~t & 0xFF;
                lfoS = toS8 (t + 128);
                break;
            }
            case 1: lfoS = toS8 (~msb); break; // sierra descendente
            case 2: lfoS = toS8 (msb); break;  // sierra ascendente
            case 3: lfoS = (lfoPh & 0x8000) ? -128 : 127; break;
            case 4: // seno: TABLE_LFO_SIN de un cuarto de onda (64 valores)
            {
                static constexpr std::uint8_t sinTab[64] = {
                    2, 5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38, 41, 44, 47, 50, 53, 56, 58, 61, 64, 67, 69, 72, 74,
                    77, 79, 82, 84, 86, 89, 91, 93, 95, 97, 99, 101, 103, 105, 106, 108, 110, 111, 113, 114, 115, 117, 118,
                    119, 120, 121, 122, 123, 124, 124, 125, 125, 126, 126, 127, 127, 127, 127 };
                auto idx = msb & 63;
                if (msb & 64) idx ^= 63;
                auto v = (int) sinTab[idx];
                if (msb & 0x80) v = ~v & 0xFF;
                lfoS = toS8 (v);
                break;
            }
            default: // S&H: generador congruente 179·s + 0x11 (8 bits)
                if (shNew)
                {
                    lfoShAcc = (lfoShAcc * 179 + 0x11) & 0xFF;
                    lfoS = toS8 (lfoShAcc);
                }
                break;
        }
    }

    // Nota nueva (VOICE_ADD_POLY en cada nota, VOICE_ADD_MONO solo con la primera tecla): con retardo, el LFO vuelve a
    // esperar y a entrar desde 0; con LFO key sync, la fase va a 0x7FFF
    void lfoKeyOn()
    {
        if (lfoDelayParam != 0)
        {
            lfoDelayAcc = 0;
            lfoFade = 0;
        }
        if (lfoSync)
            lfoPh = 0x7FFF;
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

    // Tablas de pitchenv.cc (msfa), idénticas a TABLE_PITCH_EG_RATE y TABLE_PITCH_EG_LEVEL - 128 del firmware (comprobado entrada a entrada)
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

    // Nivel de la tabla TABLE_PITCH_EG_LEVEL (= pitchEnvTab + 128) en las unidades de PITCH_EG_PROCESS: valor · 128
    int pitchEgLevel (int stage) const { return (pitchEnvTab[pitchLevels[(size_t) stage]] + 128) * 128; }

    // PITCH_EG_PROCESS para una voz (cada 2 ticks): suma o resta la velocidad de TABLE_PITCH_EG_RATE (= pitchEnvRate);
    // la etapa acaba cuando el byte alto alcanza el del objetivo (o al bajar de 0), y entonces el nivel salta al objetivo.
    // Velocidad: R · 184,85 / 4096 = R / 22,16 octavas por segundo, en escalones de 5,41 ms
    void pitchEgTick (PitchEnv& e) const
    {
        if (e.step == 3 || e.step >= 5)
            return;
        const auto stage = std::min (e.step, 3);
        const auto inc = (int) pitchEnvRate[pitchRates[(size_t) stage]];
        const auto next = pitchEgLevel (stage);
        bool finished = e.level == next;
        if (! finished && e.level < next)
        {
            const auto d = (e.level + inc) & 0xFFFF;
            if ((d >> 8) < (next >> 8)) e.level = d;
            else                        finished = true;
        }
        else if (! finished)
        {
            const auto d = (e.level - inc) & 0xFFFF;
            if ((d & 0x8000) == 0 && (d >> 8) > (next >> 8)) e.level = d;
            else                                             finished = true;
        }
        if (finished)
        {
            e.level = next;
            e.step = e.step + 1 == 6 ? 0 : e.step + 1;
        }
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
        v.pitchEnv.step = 4; // la envolvente de tono va hacia L4
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
        static constexpr double amModDepth = 0.9612;                                    // A = 253 -> -26 dB (ver abajo)
        const auto down = v.keyDown || v.sustained;
        // Envolvente de tono del subbloque: solo en los operadores en modo razón (dx7note.cc: los de frecuencia fija solo
        // siguen el pitch bend)
        // El portamento desplaza el tono respecto a la nota de la voz (en mono, la primera tecla del legato); como la envolvente
        // de tono, solo en los operadores en modo razón
        const auto portaOct = (v.portaOut - dxPitch (v.playNote)) / 4096.0;
        const auto pitchMul = (float) std::exp2 ((v.pitchEnv.loaded - 0x4000) / 4096.0 + portaOct);
        for (int k = 0; k < 6; ++k)
            lanesOf (vi).inc[k][laneOf (vi)] = patch[(size_t) k].fixed ? v.baseInc[(size_t) k] : v.baseInc[(size_t) k] * pitchMul;
        bool anyAudible = false;
        std::array<float, 6> target {};
        for (int k = 0; k < 6; ++k)
        {
            auto& op = v.ops[(size_t) k];
            const auto& pk = patch[(size_t) k];
            auto level = envStep (op, pk, down, n);
            // Modulación de amplitud: el firmware da la profundidad A (0-255, fwTicks) y el EGS la aplica según el AMS del
            // operador; esa conversión no está en el firmware y se ha medido: g = 1 - amModDepth·(A/256)·AMS. En grabaciones
            // de tres DX7 (voz TRAIN de la ROM 1A, AMD 99 -> A = 253, AMS 3), con el LFO del firmware simulado, la forma
            // lineal en amplitud con valle de 26 dB ajusta mejor (error rms 1,00 y 1,24 dB) que aplicar el registro
            // logarítmico TABLE_AMP_MOD tal cual (1,42 y 2,74 dB). Grabaciones: YouTube FRghocE4pPA (872-896 s) y
            // W_xLKRBeuCU (2402-2478 s). AMS 1 y 2 con la tabla de msfa: supuesto.
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

    double sampleRate = 44100.0, srScale = 1.0;
    // LFO y registros de modulación del firmware (ver fwTicks / lfoTick)
    int lfoPh = 0, lfoDelayAcc = 0xFFFF, lfoFade = 255, lfoS = 0, lfoShAcc = 0, lfoIncFw = 11, lfoDelayIncFw = 0,
        lfoDelayParam = 0, lfoWave = 0, pmdSc = 0, amdSc = 0, pmsFw = 0, ampFactor = 0, pitchFactor = 0, egBias = 0,
        ampA = 0, pitchReg = 0;
    int transpose = 0;
    bool oscSync = true, lfoSync = true;
    std::array<int, 4> pitchRates { 99, 99, 99, 99 }, pitchLevels { 50, 50, 50, 50 };
    bool sustainPedal = false;
    std::array<int, 4> ctlValue {}; // últimos valores (0-127) de rueda, foot, breath y aftertouch; persisten tras reset()
    // Mono y portamento (ver monoNoteOn / portaStep)
    bool mono = false, legatoUp = false, glissando = false, portaPedalParam = false, portaPedalCc = false, portaHalf = false,
         pitchEgToggle = false;
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
