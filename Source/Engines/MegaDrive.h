// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Motor Mega Drive / Genesis: YM2612 (Model 1, chip discreto) o YM3438 (Model 2 ASIC y Clean); 6 canales FM de 4 operadores
// tocados como polifonía de 6 voces. Modelo a nivel de muestra del chip, a fs = 53 693 175 Hz / 1008 = 53 267 Hz (NTSC;
// relación del reloj en Genesis Plus GX, sound.c), con el comportamiento documentado por Nuked-OPN2 (análisis del silicio)
// y plutiedev.com "YM2612 registers":
//  - Fase de 20 bits; incremento = ((2·fnum ± PM) << block >> 2 ± detune)·MUL2 >> 1 (MUL 0 = ×0,5); detune por tabla.
//  - Operador: log-seno de cuarto de onda (256) y exponencial (256) generadas por fórmula (coinciden con las 256 entradas de
//    las ROM de Nuked: -log2(sin((i+0,5)·π/512))·256 y (2^(i/256)-1)·1024); atenuación = logsin + 4·EG; salida 14 bits.
//  - EG de 10 bits: timer de 12 bits que avanza cada 3 muestras; incremento por tasa 2·R + KSV con el patrón del chip;
//    ataque exponencial (nivel += ~nivel << inc >> 5), SL 15 = nivel 31 (93 dB); SSG-EG.
//  - Algoritmos con los retardos de una muestra del chip (OP3 recibe OP1/OP2 de la muestra anterior; OP2 y OP4, la actual).
//  - LFO de 7 bits: un paso cada 108, 77, 71, 67, 62, 44, 8 o 5 muestras = 3,85 ... 83,2 Hz (el datasheet da 3,82 ... 69,2 Hz
//    escalado a 7,67 MHz; las dos más rápidas no cuadran con el chip según Nuked y GX); PM y AM del chip.
//  - DAC de 9 bits por canal. YM2612: "ladder effect" (salida activa 1 de cada 4 ciclos y el resto el signo, ×3: salto de
//    ~1,6 % del fondo de escala en el cruce por cero). YM3438: sin él.
// Salida: paso bajo analógico de 1 polo a 3,39 kHz en Model 1/2 (Genesis Plus GX, ajuste Mega Sg), bloqueo de continua y
// remuestreo a la tasa del host (SincResampler, 0,15 ms).
// Voces (mdVoice), además de los 6 canales FM:
//  - PSG SN76489 (dentro del VDP): reloj 53 693 175 / 15 = 3 579 545 Hz; contador a reloj/16 (21 ciclos por cada 5 muestras
//    del FM, exacto: 1008 / (16·15) = 4,2); tono f = reloj/(32·N), N de 10 bits (mínimo 109,3 Hz con N = 1023; N = 0/1 =
//    salida fija); ruido con LFSR de 16 bits (blanco: taps en los bits 0 y 3; periódico: bit 0, periodo de 16 pasos) a
//    reloj/512, /1024, /2048 o al ritmo del tono 3; 16 volúmenes en pasos de 2 dB (smspower.org SN76489). Salida unipolar,
//    mono. Envolvente de driver (el chip no tiene): pasos de 2 dB por frame de vídeo (59,92 Hz).
//    Mezcla: amplitud de un canal PSG = 0,513 de la de un canal FM en Model 1 (Genesis Plus GX: PSG ±2800 con preamp 150 %
//    frente a ±8192 del FM, "ajustado al balance PSG/FM de un MD1 VA4"); el PSG pasa por el mismo paso bajo. Model 2 y
//    Clean: ×20/32 (relación entre los ajustes M2 100:20 y M1 100:32 del Mega Sg, firebrandx) y sin filtro.
//  - DAC del canal 6 ($2B bit 7): el sample del usuario cuantizado a 8 bits ($2A) y retenido entre escrituras, a la tasa
//    del driver; sustituye a la salida FM del canal 6 en el acumulador (Nuked: (dato ^ 0x80) << 1) con su paneo y ladder.
//  - Canal 3 especial ($27 bits 7-6 = 01): OP1-OP3 con frecuencia propia ($A9/$AD, $AA/$AE, $A8/$AC); OP4 con la del
//    canal. Monofónico (último pulsado), como lo usaban los drivers.
// Macros por frame (Dsp/FrameMacro.h, propiedad "mdMacros"), a 59,92 Hz como los drivers SMPS/GEMS: volumen (FM: TL de las
// portadoras, 2 dB por paso = 8/3 de TL; PSG: atenuación de 2 dB por paso, sustituye a su envolvente), arpegio, pitch (FM:
// F-number, PSG: N; + = más grave) y "duty" = tipo de ruido del PSG (0 blanco, 1 periódico). El DAC no usa macros.
struct MegaDriveEngine
{
    static constexpr double kChipRate = 53693175.0 / 1008.0;
    static constexpr double kPsgClock = 53693175.0 / 15.0;
    enum Model { model1, model2, clean };
    enum Voice { voiceFm, voiceFmPsg, voicePsgTone, voicePsgNoise, voiceDac, voiceCh3 };

    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        resampler.prepare (kChipRate, sr);
        dcCoef = std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sr);
        lpCoef = std::exp (-juce::MathConstants<double>::twoPi * 3390.0 / kChipRate);
        (void) maxBlock;
        reset();
    }

    void reset()
    {
        for (auto& c : ch) c = {};
        lfoCount = lfoQuot = 0;
        egTimer = egQuot = egShiftLock = egTimerLow = 0;
        lp = dcIn = dcOut = { 0.0, 0.0 };
        age = 0;
        for (auto& v : psg) v = {};
        lfsr = 0x8000;
        psgPhase = 0;
        psgOut = frameAcc = 0.0;
        heldCount = 0;
        dacPlaying = false;
        dacValue = 0x80;
        resampler.reset();
    }

    // PANIC, tras reset: el chip en reposo da continua (escalera del DAC) y el bloqueo de continua, recién puesto a cero, la
    // dejaría pasar como un escalón que tarda ~0,2 s en irse (medido: +0,0185). Durante 64 muestras (1,3 ms a 48 kHz, más
    // que el remuestreador) la salida calla y el filtro sigue a la entrada: después arranca en equilibrio
    void settleOutput() { settle = 64; }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, const SampleData* userSample, int rootNote,
                 const MacroSet* macroSet)
    {
        static const MacroSet none;
        macros = macroSet != nullptr ? macroSet : &none;
        readParams (p);
        dacRoot = rootNote;
        if (voice == voiceDac)
            prepareDac (userSample, p);
        bendSmoothed += 0.2f * (p.pitchBend.load() - bendSmoothed);
        tuneCents = (double) p.fineTune->load() + 100.0 * bendSmoothed;
        for (auto& c : ch)
            if (c.note >= 0)
                setPitch (c);
        for (size_t k = 0; k < 3; ++k)
            if (psg[k].note >= 0)
                psg[k].n = psgPeriod (psg[k], false);
        if (psg[3].note >= 0)
            psg[2].n = psgPeriod (psg[3], true);
        if (dacPlaying)
            dacStep = dacStepFor (dacNote);

        auto midiIt = midi.cbegin();
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
                handleMidi ((*midiIt).getMessage());
            const auto lr = resampler.nextStereo ([this] { return chipSample(); });
            // Bloqueo de continua (5 Hz) por lado: el ladder del YM2612 deja un offset por canal
            std::array<float, 2> y {};
            for (size_t side = 0; side < 2; ++side)
            {
                const auto dc = settle > 0 ? 0.0 : lr[side] - dcIn[side] + dcCoef * dcOut[side];
                dcIn[side] = lr[side];
                dcOut[side] = dc;
                y[side] = (float) dc;
            }
            if (settle > 0)
                --settle;
            if (monoOutput)
                y[0] = y[1] = 0.5f * (y[0] + y[1]);
            if (out.getNumChannels() >= 2)
            {
                out.addSample (0, i, y[0]);
                out.addSample (1, i, y[1]);
            }
            else if (out.getNumChannels() == 1)
            {
                out.addSample (0, i, 0.5f * (y[0] + y[1]));
            }
        }
    }

    // Descripción de un algoritmo (OP1..OP4 como en el datasheet; "+" = suma, "->" = modula)
    static const char* describe (int alg)
    {
        static const char* text[8] = { "1 -> 2 -> 3 -> 4", "(1 + 2) -> 3 -> 4", "(1 + (2 -> 3)) -> 4", "((1 -> 2) + 3) -> 4",
                                       "(1 -> 2) + (3 -> 4)", "1 -> 2, 1 -> 3, 1 -> 4  (2 + 3 + 4)", "(1 -> 2) + 3 + 4", "1 + 2 + 3 + 4" };
        return text[juce::jlimit (0, 7, alg)];
    }

    // Portadoras de cada algoritmo, bit k = OP(k+1)
    static int carriers (int alg)
    {
        static const int mask[8] = { 0b1000, 0b1000, 0b1000, 0b1000, 0b1010, 0b1110, 0b1110, 0b1111 };
        return mask[juce::jlimit (0, 7, alg)];
    }

private:
    enum EgState { attack, decay, sustain, release };

    struct OpParams { int tl = 127, ar = 31, dr = 0, sr = 0, sl = 0, rr = 15, mul2 = 2, dt = 0, ks = 0, ssg = 0; bool am = false; };

    struct Op
    {
        uint32_t phase = 0;
        int level = 0x3ff, state = release;
        bool kon = false, konLatch = false, ssgInv = false;
        int ssgDir = 0;
        int egOut = 0x3ff;
    };

    struct Channel
    {
        std::array<Op, 4> op {}; // OP1..OP4 (numeración del datasheet)
        int fnum = 0, block = 0, kcode = 0;
        std::array<int, 4> opFnum {}, opBlock {}, opKcode {}; // por operador: distintos solo en el canal 3 especial
        std::array<MacroCursor, MacroSet::numKinds> mc {};    // macros por frame
        int macroTl = 0, pitchAcc = 0;
        bool macroRelease = false;
        int op1Prev0 = 0, op1Prev1 = 0, op2Prev = 0;
        int note = -1, velocityTl = 0;
        long long age = 0;
        bool gate = false;
    };

    // Canal del PSG (0-2 tono, 3 ruido). env/velAtten: atenuación en pasos de 2 dB (15 = apagado)
    enum PsgStage { psgAttackStage, psgDecayStage, psgSustainStage, psgReleaseStage, psgOffStage };
    struct Psg
    {
        int n = 1023, counter = 1, env = 15, velAtten = 0, stage = psgOffStage, frameCount = 0, note = -1;
        bool out = false, gate = false;
        std::array<MacroCursor, MacroSet::numKinds> mc {};
        int pitchAcc = 0;
        bool macroRelease = false;
        long long age = 0;
    };

    void readParams (const Params& p)
    {
        const auto newVoice = juce::jlimit (0, 5, (int) p.mdVoice->load());
        if (newVoice != voice)
        {
            silenceAll();
            voice = newVoice;
        }
        psgWhite = p.mdPsgNoise->load() < 0.5f;
        psgNoiseRate = juce::jlimit (0, 3, (int) p.mdPsgNoiseRate->load());
        psgA = (int) p.mdPsgAttack->load(); psgD = (int) p.mdPsgDecay->load();
        psgS = (int) p.mdPsgSustain->load(); psgR = (int) p.mdPsgRelease->load();
        static const double dacRates[5] = { 6500.0, 8000.0, 11025.0, 16000.0, 22050.0 };
        dacRateIndex = juce::jlimit (0, 4, (int) p.mdDacRate->load());
        dacRate = dacRates[dacRateIndex];
        for (int k = 0; k < 3; ++k)
            ch3Offset[(size_t) k] = (int) p.mdCh3Op[(size_t) k]->load();
        model = juce::jlimit (0, 2, (int) p.mdModel->load());
        algorithm = juce::jlimit (0, 7, (int) p.mdAlgorithm->load());
        feedback = juce::jlimit (0, 7, (int) p.mdFeedback->load());
        lfoFreq = (int) p.mdLfo->load() - 1; // 0 = Off
        pms = juce::jlimit (0, 7, (int) p.mdPms->load());
        ams = juce::jlimit (0, 3, (int) p.mdAms->load());
        velocityOn = p.mdVelocity->load() > 0.5f;
        panMode = juce::jlimit (0, 4, (int) p.mdPan->load());
        monoOutput = p.mdOutput->load() > 0.5f;
        for (int k = 0; k < 4; ++k)
        {
            const auto& src = p.mdOp[(size_t) k];
            auto& o = opParams[(size_t) k];
            o.tl = (int) src.tl->load(); o.ar = (int) src.ar->load(); o.dr = (int) src.dr->load(); o.sr = (int) src.sr->load();
            const auto sl = (int) src.sl->load();
            o.sl = sl | ((sl + 1) & 0x10); // SL 15 -> 31
            o.rr = (int) src.rr->load();
            const auto mul = (int) src.mul->load();
            o.mul2 = mul == 0 ? 1 : 2 * mul;
            o.dt = (int) src.dt->load(); o.ks = (int) src.ks->load(); o.am = src.am->load() > 0.5f;
            o.ssg = (int) src.ssg->load(); // 0 = off, 1..8 = modo 0..7
        }
    }

    // fnum/block para la nota: bloque mínimo con fnum < 1288 (rango de una octava 644..1288, como las tablas de los
    // drivers: A4 = fnum 1083 bloque 4; plutiedev da 1081). f = fnum·2^block·fs/2^21
    double noteHz (int note) const { return 440.0 * std::exp2 ((note - 69) / 12.0 + tuneCents / 1200.0) * KoniTuning::ratio (note); }

    static void fnumFor (double f, int& fnum, int& block, int& kcode)
    {
        block = 0;
        double x = f * 2097152.0 / kChipRate;
        while (x >= 1288.0 && block < 7)
        {
            x *= 0.5;
            ++block;
        }
        fnum = juce::jlimit (0, 2047, (int) std::lround (x));
        static const int fnNote[16] = { 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3 };
        kcode = (block << 2) | fnNote[fnum >> 7];
    }

    // Nota con la macro de arpegio (semitonos, o nota absoluta @n desde C-0)
    int macroNote (const std::array<MacroCursor, MacroSet::numKinds>& mc, int note) const
    {
        const auto& cur = mc[MacroSet::arpeggio];
        if (! cur.active()) return note;
        const auto& m = macros->macro[MacroSet::arpeggio];
        return juce::jlimit (0, 127, cur.isAbsolute (m) ? cur.value (m, 0) + 12 : note + cur.value (m, 0));
    }

    void setPitch (Channel& c) const
    {
        const auto f = noteHz (macroNote (c.mc, c.note));
        fnumFor (f, c.fnum, c.block, c.kcode);
        c.fnum = juce::jlimit (0, 2047, c.fnum - c.pitchAcc); // macro de pitch: + = F-number menor (más grave)
        const auto special = voice == voiceCh3 && &c == &ch[2];
        for (size_t k = 0; k < 4; ++k)
        {
            if (special && k < 3)
            {
                fnumFor (f * std::exp2 (ch3Offset[k] / 12.0), c.opFnum[k], c.opBlock[k], c.opKcode[k]);
                c.opFnum[k] = juce::jlimit (0, 2047, c.opFnum[k] - c.pitchAcc);
            }
            else
            {
                c.opFnum[k] = c.fnum;
                c.opBlock[k] = c.block;
                c.opKcode[k] = c.kcode;
            }
        }
    }

    // N del PSG con arpegio y pitch de macro (+ = N mayor, más grave). Ruido siguiendo la tecla: N del tono 3
    int psgPeriod (const Psg& v, bool noiseKey) const
    {
        const auto note = macroNote (v.mc, v.note);
        return juce::jlimit (1, 1023, (noiseKey ? noiseN (note) : toneN (noteHz (note))) + v.pitchAcc);
    }

    // Macros al empezar una nota (valor del frame 0) y en cada frame
    template <typename Voice>
    void startMacros (Voice& v) const
    {
        for (size_t m = 0; m < MacroSet::numKinds; ++m)
            v.mc[m].start (macros->macro[m]);
        v.pitchAcc = v.mc[MacroSet::pitch].value (macros->macro[MacroSet::pitch], 0);
        v.macroRelease = false;
    }

    template <typename Voice>
    void advanceMacros (Voice& v) const
    {
        for (size_t m = 0; m < MacroSet::numKinds; ++m)
            v.mc[m].advance (macros->macro[m]);
        if (v.mc[MacroSet::pitch].active())
            v.pitchAcc = juce::jlimit (-1023, 1023, v.pitchAcc + v.mc[MacroSet::pitch].value (macros->macro[MacroSet::pitch], 0));
    }

    template <typename Voice>
    bool macrosFinished (const Voice& v) const
    {
        for (size_t m = 0; m < MacroSet::numKinds; ++m)
            if (v.mc[m].active() && ! v.mc[m].finished (macros->macro[m])) return false;
        return true;
    }

    bool anyReleasePoint() const
    {
        return std::any_of (macros->macro.begin(), macros->macro.end(), [] (const FrameMacro& m) { return m.release >= 0; });
    }

    // Volumen de macro -> TL extra de las portadoras (FM): 2 dB por paso = 8/3 de TL (0,75 dB); 0 = silencio (TL 127)
    void updateMacroTl (Channel& c) const
    {
        const auto& cur = c.mc[MacroSet::volume];
        if (! cur.active()) { c.macroTl = 0; return; }
        const auto v = cur.value (macros->macro[MacroSet::volume], 15);
        c.macroTl = v == 0 ? 127 : (int) std::lround ((15 - v) * 8.0 / 3.0);
    }

    // Volumen de macro del PSG: atenuación 15 - v (pasos de 2 dB), en lugar de la envolvente
    void updateMacroVolume (Psg& v) const
    {
        if (v.mc[MacroSet::volume].active())
            v.env = 15 - v.mc[MacroSet::volume].value (macros->macro[MacroSet::volume], 15);
    }

    // Un frame del driver (59,92 Hz): avanza las macros de cada canal que suena
    void macroFrame()
    {
        if (macros->empty()) return;
        for (auto& c : ch)
        {
            if (c.note < 0 || ! (c.gate || c.macroRelease)) continue;
            advanceMacros (c);
            updateMacroTl (c);
            setPitch (c);
            if (c.macroRelease && macrosFinished (c)) c.macroRelease = false;
        }
        for (size_t k = 0; k < 4; ++k)
        {
            auto& v = psg[k];
            if (v.note < 0 || ! (v.gate || v.macroRelease)) continue;
            advanceMacros (v);
            updateMacroVolume (v);
            if (v.macroRelease && v.mc[MacroSet::volume].finished (macros->macro[MacroSet::volume]) && v.env >= 15)
            {
                v.macroRelease = false;
                v.note = -1;
                v.stage = psgOffStage;
            }
        }
    }

    // PSG: N del tono (f = reloj/(32·N)); ruido siguiendo la tecla con N del tono 3: el periódico suena a reloj/(32·16·N)
    static int toneN (double f) { return juce::jlimit (1, 1023, (int) std::lround (kPsgClock / (32.0 * f))); }
    int noiseN (int note) const { return juce::jlimit (1, 1023, (int) std::lround (kPsgClock / (512.0 * noteHz (note)))); }
    int velAttenFor (float vel, double stepDb) const
    {
        return velocityOn ? (int) std::lround (-20.0 * std::log10 (std::max (0.01f, vel)) / stepDb) : 0;
    }

    void silenceAll()
    {
        for (auto& c : ch)
        {
            c.gate = false;
            c.note = -1;
            for (auto& o : c.op) { o.konLatch = false; o.kon = false; o.level = 0x3ff; o.state = release; }
        }
        for (auto& v : psg) { v.gate = false; v.note = -1; v.env = 15; v.stage = psgOffStage; }
        heldCount = 0;
        dacPlaying = false;
        dacValue = 0x80;
    }

    // Key on de un canal FM. Reataque como un driver: key off de una muestra antes del key on (el chip necesita el flanco)
    void fmStart (Channel& c, int note, float vel)
    {
        if (c.gate)
            for (size_t k = 0; k < 4; ++k) { c.op[k].konLatch = false; egStep (c.op[k], opParams[k], c.opKcode[k], false); }
        c.note = note;
        c.gate = true;
        c.age = ++age;
        // Velocidad (driver de KoniSynth, no del chip): atenuación de las portadoras = 20·log10(vel) en pasos de 0,75 dB
        c.velocityTl = juce::jlimit (0, 127, velAttenFor (vel, 0.75));
        startMacros (c);
        updateMacroTl (c);
        setPitch (c);
        for (auto& o : c.op) o.konLatch = true;
    }

    void fmStop (Channel& c) const
    {
        c.gate = false;
        for (auto& o : c.op) o.konLatch = false;
        if (anyReleasePoint()) // las macros siguen tras su release mientras suena la caída del FM
        {
            for (size_t m = 0; m < MacroSet::numKinds; ++m)
                c.mc[m].noteOff (macros->macro[m]);
            c.macroRelease = true;
        }
    }

    // Asignación polifónica: misma nota -> su canal; si no, el libre (sin gate) más antiguo; si no hay, el más antiguo
    template <typename Slot, size_t N>
    int allocate (const std::array<Slot, N>& slots, int count, int note) const
    {
        for (int k = 0; k < count; ++k)
            if (slots[(size_t) k].note == note) return k;
        int target = -1;
        long long best = std::numeric_limits<long long>::max();
        for (int pass = 0; pass < 2 && target < 0; ++pass)
            for (int k = 0; k < count; ++k)
                if ((pass == 1 || ! slots[(size_t) k].gate) && slots[(size_t) k].age < best)
                {
                    best = slots[(size_t) k].age;
                    target = k;
                }
        return target;
    }

    void psgKeyOn (Psg& v, int note, float vel)
    {
        v.note = note;
        v.gate = true;
        v.age = ++age;
        v.velAtten = juce::jlimit (0, 15, velAttenFor (vel, 2.0));
        v.frameCount = 0;
        // Ataque 0 = volumen máximo ya en el key on (sin esperar al frame siguiente: 16,7 ms de latencia)
        v.env = psgA == 0 ? 0 : 15;
        v.stage = psgA == 0 ? psgDecayStage : psgAttackStage;
        startMacros (v);
        updateMacroVolume (v);
    }

    void psgKeyOff (Psg& v) const
    {
        v.gate = false;
        if (v.mc[MacroSet::volume].active() && anyReleasePoint()) // la macro de volumen hace el release
        {
            for (size_t m = 0; m < MacroSet::numKinds; ++m)
                v.mc[m].noteOff (macros->macro[m]);
            v.macroRelease = true;
            return;
        }
        v.frameCount = 0;
        v.stage = psgR == 0 ? psgOffStage : psgReleaseStage;
        if (psgR == 0) v.env = 15;
    }

    // Envolvente de driver, una vez por frame de vídeo: un paso de 2 dB cada 'rate' frames (0 = inmediato)
    void psgEnvFrame (Psg& v) const
    {
        if (v.mc[MacroSet::volume].active())
            return;
        const auto due = [&v] (int rate) { if (++v.frameCount < rate) return false; v.frameCount = 0; return true; };
        switch (v.stage)
        {
            case psgAttackStage:
                if (psgA == 0 || due (psgA)) v.env = psgA == 0 ? 0 : v.env - 1;
                if (v.env <= 0) { v.env = 0; v.stage = psgDecayStage; v.frameCount = 0; }
                break;
            case psgDecayStage:
                if (v.env >= psgS) { v.stage = psgSustainStage; break; }
                if (psgD == 0 || due (psgD)) v.env = psgD == 0 ? psgS : v.env + 1;
                break;
            case psgReleaseStage:
                if (psgR == 0 || due (psgR)) v.env = psgR == 0 ? 15 : v.env + 1;
                if (v.env >= 15) { v.env = 15; v.stage = psgOffStage; v.note = -1; }
                break;
            default:
                break;
        }
    }

    // DAC: tasa de lectura para la nota (la Root del panel Sample suena a la tasa del driver)
    double dacStepFor (int note) const { return dacRate * std::exp2 ((note - dacRoot) / 12.0 + tuneCents / 1200.0) / kChipRate; }

    // Sample del usuario (región Start/Length del panel Sample) remuestreado a la tasa del driver y cuantizado a 8 bits sin
    // signo, como los datos del registro $2A. Solo se recalcula si cambia algo.
    void prepareDac (const SampleData* sample, const Params& p)
    {
        const auto start = p.sampleStart->load(), lengthMs = p.sampleLength->load();
        dacLoop = p.sampleLoop->load() > 0.5f;
        const auto length = sample != nullptr ? sample->length() : 0;
        if (sample == dacSource && length == dacSourceLength && dacRateIndex == dacPreparedRate && start == dacStart && lengthMs == dacLengthMs)
            return;
        dacSource = sample; dacSourceLength = length; dacPreparedRate = dacRateIndex; dacStart = start; dacLengthMs = lengthMs;
        dacPcm.clear();
        dacPlaying = false;
        dacValue = 0x80;
        if (sample == nullptr || length < 16)
            return;
        const auto ratio = sample->sampleRate / dacRate;
        const auto level = juce::jlimit (0, SampleData::maxLevels - 1, (int) std::floor (std::log2 (std::max (1.0, ratio))));
        const auto scale = ratio / std::exp2 ((double) level);
        const auto first = (double) start * length / std::exp2 ((double) level);
        auto count = (int) ((1.0 - start) * length / ratio);
        if (lengthMs > 0.0f)
            count = std::min (count, (int) (lengthMs / 1000.0 * dacRate));
        dacPcm.resize ((size_t) std::max (0, count));
        for (int i = 0; i < count; ++i)
            dacPcm[(size_t) i] = (uint8_t) juce::jlimit (0, 255, (int) std::lround (128.0 + 127.0 * sample->read (level, first + i * scale)));
    }

    // Voces monofónicas (ruido del PSG, DAC, canal 3 especial): prioridad a la última nota, con pila de notas pulsadas
    void monoStart (int note, float vel)
    {
        monoVel = vel;
        if (voice == voicePsgNoise)
        {
            psg[2].n = noiseN (note);
            lfsr = 0x8000; // el driver escribe el registro de ruido: el LFSR se reinicia
            psgKeyOn (psg[3], note, vel);
        }
        else if (voice == voiceDac)
        {
            dacNote = note;
            dacPos = 0.0;
            dacStep = dacStepFor (note);
            dacPlaying = ! dacPcm.empty();
        }
        else if (voice == voiceCh3)
        {
            fmStart (ch[2], note, vel);
        }
    }

    void monoStop()
    {
        if (voice == voicePsgNoise)
            psgKeyOff (psg[3]);
        else if (voice == voiceDac && dacLoop) // sin lazo, el driver toca el sample hasta el final
        {
            dacPlaying = false;
            dacValue = 0x80;
        }
        else if (voice == voiceCh3)
            fmStop (ch[2]);
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        const auto mono = voice == voicePsgNoise || voice == voiceDac || voice == voiceCh3;
        if (m.isNoteOn())
        {
            const auto note = m.getNoteNumber();
            const auto vel = m.getFloatVelocity();
            if (mono)
            {
                int w = 0;
                for (int r = 0; r < heldCount; ++r)
                    if (held[(size_t) r] != note) held[(size_t) w++] = held[(size_t) r];
                heldCount = w;
                if (heldCount == (int) held.size())
                {
                    std::copy (held.begin() + 1, held.end(), held.begin());
                    --heldCount;
                }
                held[(size_t) heldCount++] = note;
                monoStart (note, vel);
                return;
            }
            if (voice == voiceFm || voice == voiceFmPsg)
                fmStart (ch[(size_t) allocate (ch, 6, note)], note, vel);
            if (voice == voiceFmPsg || voice == voicePsgTone)
                psgKeyOn (psg[(size_t) allocate (psg, 3, note)], note, vel);
        }
        else if (m.isNoteOff())
        {
            const auto note = m.getNoteNumber();
            if (mono)
            {
                const auto wasTop = heldCount > 0 && held[(size_t) (heldCount - 1)] == note;
                int w = 0;
                for (int r = 0; r < heldCount; ++r)
                    if (held[(size_t) r] != note) held[(size_t) w++] = held[(size_t) r];
                heldCount = w;
                if (wasTop)
                {
                    if (heldCount > 0) monoStart (held[(size_t) (heldCount - 1)], monoVel);
                    else monoStop();
                }
                return;
            }
            for (auto& c : ch)
                if (c.note == note && c.gate) fmStop (c);
            for (size_t k = 0; k < 3; ++k)
                if (psg[k].note == note && psg[k].gate) psgKeyOff (psg[k]);
        }
        else if (m.isAllSoundOff())
        {
            silenceAll();
        }
        else if (m.isAllNotesOff())
        {
            heldCount = 0;
            for (auto& c : ch) if (c.gate) fmStop (c);
            for (auto& v : psg) if (v.gate) psgKeyOff (v);
            if (dacLoop) { dacPlaying = false; dacValue = 0x80; }
        }
    }

    // Un ciclo del contador del PSG (reloj/16): tonos con medio periodo de N ciclos; el ruido desplaza el LFSR en el flanco
    // de subida de su divisor (N = 16, 32, 64 o el del tono 3)
    void psgTick()
    {
        for (size_t k = 0; k < 3; ++k)
        {
            auto& v = psg[k];
            if (--v.counter <= 0)
            {
                v.counter = v.n;
                v.out = v.n <= 1 ? true : ! v.out;
            }
        }
        auto& z = psg[3];
        if (--z.counter <= 0)
        {
            z.counter = psgNoiseRate == 0 ? psg[2].n : (0x10 << (psgNoiseRate - 1));
            z.out = ! z.out;
            if (z.out)
            {
                const auto white = z.mc[MacroSet::duty].active() ? (z.mc[MacroSet::duty].value (macros->macro[MacroSet::duty], 0) & 1) == 0 : psgWhite;
                const auto fb = white ? ((lfsr ^ (lfsr >> 3)) & 1) : (lfsr & 1);
                lfsr = (lfsr >> 1) | (fb << 15);
            }
        }
    }

    double psgLevel() const
    {
        static const int volume[16] = { 32767, 26028, 20675, 16422, 13045, 10362, 8231, 6568, 5193, 4125, 3277, 2603, 2067, 1642, 1304, 0 };
        double s = 0.0;
        for (size_t k = 0; k < 4; ++k)
        {
            const auto& v = psg[k];
            const auto bit = k < 3 ? v.out : (lfsr & 1) != 0;
            if (bit && v.note >= 0)
                s += volume[std::min (15, v.env + v.velAtten)];
        }
        return s / 32767.0;
    }

    // Incremento de fase de un operador (Nuked-OPN2, PhaseCalcIncrement)
    uint32_t phaseIncrement (const Channel& c, size_t opIndex, const OpParams& o, int lfoPm) const
    {
        static const int sh1[8][8] = { { 7, 7, 7, 7, 7, 7, 7, 7 }, { 7, 7, 7, 7, 7, 7, 7, 7 }, { 7, 7, 7, 7, 7, 7, 1, 1 }, { 7, 7, 7, 7, 1, 1, 1, 1 },
                                       { 7, 7, 7, 1, 1, 1, 1, 0 }, { 7, 7, 1, 1, 0, 0, 0, 0 }, { 7, 7, 1, 1, 0, 0, 0, 0 }, { 7, 7, 1, 1, 0, 0, 0, 0 } };
        static const int sh2[8][8] = { { 7, 7, 7, 7, 7, 7, 7, 7 }, { 7, 7, 7, 7, 2, 2, 2, 2 }, { 7, 7, 7, 2, 2, 2, 7, 7 }, { 7, 7, 2, 2, 7, 7, 2, 2 },
                                       { 7, 7, 2, 7, 7, 7, 2, 7 }, { 7, 7, 7, 2, 7, 7, 2, 1 }, { 7, 7, 7, 2, 7, 7, 2, 1 }, { 7, 7, 7, 2, 7, 7, 2, 1 } };
        static const int detuneTable[8] = { 16, 17, 19, 20, 22, 24, 27, 29 };
        int fnum = c.opFnum[opIndex] << 1;
        int lfoL = lfoPm & 0x0f;
        if (lfoL & 0x08)
            lfoL ^= 0x0f;
        const auto fnumH = c.opFnum[opIndex] >> 4;
        int fm = (fnumH >> sh1[pms][lfoL]) + (fnumH >> sh2[pms][lfoL]);
        if (pms > 5)
            fm <<= pms - 5;
        fm >>= 2;
        fnum = ((lfoPm & 0x10) ? fnum - fm : fnum + fm) & 0xfff;
        int base = (fnum << c.opBlock[opIndex]) >> 2;
        if (const auto dtL = std::abs (o.dt); dtL != 0)
        {
            const auto kcode = std::min (c.opKcode[opIndex], 0x1c);
            const auto sum = (kcode >> 2) + 9 + ((dtL == 3) | (dtL & 0x02));
            const auto detune = detuneTable[((sum & 1) << 2) | (kcode & 3)] >> (9 - (sum >> 1));
            base += o.dt < 0 ? -detune : detune;
        }
        base &= 0x1ffff;
        return (uint32_t) ((base * o.mul2) >> 1) & 0xfffffu;
    }

    // Un paso de la envolvente de un operador (Nuked-OPN2: EnvelopeSSGEG, EnvelopePrepare, EnvelopeGenerate, EnvelopeADSR).
    // Devuelve true si hay que reiniciar la fase.
    bool egStep (Op& o, const OpParams& prm, int kcode, bool tick)
    {
        static const int stepHi[4][4] = { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 0, 1, 0 }, { 1, 1, 1, 0 } };
        const auto ssgOn = prm.ssg > 0;
        const auto mode = prm.ssg - 1;
        // SSG-EG: latches a partir del nivel actual
        bool pgrst = false, repeat = false, holdUp = false;
        int dir = 0;
        if (ssgOn)
        {
            dir = o.ssgDir;
            if (o.level & 0x200)
            {
                if ((mode & 3) == 0) pgrst = true;
                if ((mode & 1) == 0) repeat = true;
                if ((mode & 3) == 2) dir ^= 1;
                if ((mode & 3) == 3) dir = 1;
            }
            if (o.konLatch && ((mode & 7) == 5 || (mode & 7) == 3))
                holdUp = true;
            dir &= o.kon ? 1 : 0;
        }
        o.ssgDir = dir;
        o.ssgInv = ssgOn && ((dir ^ ((mode >> 2) & 1)) & (o.kon ? 1 : 0));

        // Tasa e incremento
        auto state = o.state;
        if ((o.kon && repeat) || (! o.kon && o.konLatch))
            state = attack;
        const auto rateReg = state == attack ? prm.ar : state == decay ? prm.dr : state == sustain ? prm.sr : (prm.rr << 1) | 1;
        const auto rate = std::min (63, (rateReg << 1) + (kcode >> (prm.ks ^ 3)));
        int inc = 0;
        if (rateReg != 0 && tick)
        {
            if (rate < 48)
            {
                const auto sum = ((rate >> 2) + egShiftLock) & 0x0f;
                inc = sum == 12 ? 1 : sum == 13 ? (rate >> 1) & 1 : sum == 14 ? rate & 1 : 0;
            }
            else
            {
                inc = std::min (4, stepHi[rate & 3][egTimerLow] + (rate >> 2) - 11);
            }
        }
        const auto rateMax = (rate >> 1) == 0x1f;

        // Salida de la envolvente (con el nivel anterior): inversión SSG, AM y TL
        {
            int lvl = (o.ssgInv ? 512 - o.level : o.level) & 0x3ff;
            if (prm.am)
            {
                static const int amShift[4] = { 7, 3, 1, 0 };
                lvl += lfoAm >> amShift[ams];
            }
            lvl += prm.tl << 3;
            o.egOut = std::min (lvl, 0x3ff);
        }

        // ADSR
        const auto nkon = o.konLatch, okon = o.kon;
        const auto pgReset = (nkon && ! okon) || pgrst;
        const auto konEvent = (nkon && ! okon) || (okon && repeat);
        const auto koffEvent = okon && ! nkon;
        auto level = o.level;
        if (koffEvent && o.ssgInv)
            level = (512 - level) & 0x3ff;
        const auto egOff = ssgOn ? (level >> 9) != 0 : (level & 0x3f0) == 0x3f0;
        auto next = level;
        auto nextState = o.state;
        int step = 0;
        if (konEvent)
        {
            nextState = attack;
            if (rateMax)
                next = 0;
            else if (o.state == attack && level != 0 && inc != 0 && nkon)
                step = (~level * (1 << inc)) >> 5;
        }
        else
        {
            switch (o.state)
            {
                case attack:
                    if (level == 0) nextState = decay;
                    else if (inc != 0 && ! rateMax && nkon) step = (~level * (1 << inc)) >> 5;
                    break;
                case decay:
                    if ((level >> 4) == (prm.sl << 1)) nextState = sustain;
                    else if (! egOff && inc != 0) step = (1 << (inc - 1)) << (ssgOn ? 2 : 0);
                    break;
                default: // sustain, release
                    if (! egOff && inc != 0) step = (1 << (inc - 1)) << (ssgOn ? 2 : 0);
                    break;
            }
            if (! nkon)
                nextState = release;
        }
        if (! konEvent && ! holdUp && o.state != attack && egOff)
        {
            nextState = release;
            next = 0x3ff;
        }
        o.kon = nkon;
        o.level = (next + step) & 0x3ff;
        o.state = nextState;
        return pgReset;
    }

    static int opOutput (const Op& o, int mod)
    {
        static const auto tables = []
        {
            std::pair<std::array<int, 256>, std::array<int, 256>> t;
            for (int i = 0; i < 256; ++i)
            {
                t.first[(size_t) i] = (int) std::lround (-std::log2 (std::sin ((i + 0.5) * juce::MathConstants<double>::pi / 512.0)) * 256.0);
                t.second[(size_t) i] = (int) std::lround ((std::exp2 (i / 256.0) - 1.0) * 1024.0);
            }
            return t;
        }();
        const auto phase = (mod + (int) (o.phase >> 10)) & 0x3ff;
        const auto quarter = (phase & 0x100) ? ((phase ^ 0xff) & 0xff) : (phase & 0xff);
        const auto level = std::min (0x1fff, tables.first[(size_t) quarter] + (o.egOut << 2));
        const auto out = ((tables.second[(size_t) ((level & 0xff) ^ 0xff)] | 0x400) << 2) >> (level >> 8);
        return (phase & 0x200) ? -out : out;
    }

    std::array<float, 2> chipSample()
    {
        // LFO: contador de 7 bits; un paso cuando el cociente alcanza el divisor (lfo_cycles de Nuked)
        static const int lfoCycles[8] = { 108, 77, 71, 67, 62, 44, 8, 5 };
        if (lfoFreq >= 0)
        {
            const auto cyc = lfoCycles[juce::jlimit (0, 7, lfoFreq)];
            // Nuked comprueba la condición en cada ciclo de reloj y suma 1 una vez por muestra: un paso cada 'cyc' muestras
            // (Genesis Plus GX: "one level will last for 108, 77, 71, 67, 62, 44, 8 or 5 samples")
            if ((++lfoQuot & cyc) == cyc) { lfoQuot = 0; lfoCount = (lfoCount + 1) & 0x7f; }
        }
        else
        {
            lfoCount = 0;
        }
        const auto lfoPm = lfoCount >> 2;
        lfoAm = ((lfoCount & 0x40) ? (lfoCount & 0x3f) : (lfoCount ^ 0x3f)) << 1;

        // Timer de la envolvente: avanza cada 3 muestras; desplazamiento = posición del bit bajo + 1
        egQuot = (egQuot + 1) % 3;
        const auto tick = egQuot == 2;
        if (tick)
        {
            egTimer = (egTimer + 1) & 0xfff;
            egShiftLock = 0;
            for (int b = 0; b < 12; ++b)
                if ((egTimer >> b) & 1) { egShiftLock = b + 1; break; }
            egTimerLow = egTimer & 3;
        }

        // DAC: el driver escribe la muestra siguiente a su tasa; el registro retiene el valor entre escrituras
        if (dacPlaying)
        {
            auto idx = (size_t) dacPos;
            if (idx >= dacPcm.size() && dacLoop && ! dacPcm.empty())
            {
                dacPos = std::fmod (dacPos, (double) dacPcm.size());
                idx = (size_t) dacPos;
            }
            if (idx < dacPcm.size())
            {
                dacValue = dacPcm[idx];
                dacPos += dacStep;
            }
            else
            {
                dacPlaying = false;
                dacValue = 0x80;
            }
        }

        // PSG: 21 ciclos de reloj/16 cada 5 muestras del FM; la salida de la muestra es la media de sus ciclos (4 o 5)
        const auto psgOn = voice == voiceFmPsg || voice == voicePsgTone || voice == voicePsgNoise;
        if ((frameAcc += 1.0) >= kChipRate / 59.922743)
        {
            frameAcc -= kChipRate / 59.922743;
            if (psgOn)
                for (auto& v : psg) psgEnvFrame (v);
            macroFrame();
            for (size_t k = 0; k < 3; ++k)
                if (psg[k].note >= 0) psg[k].n = psgPeriod (psg[k], false);
            if (psg[3].note >= 0) psg[2].n = psgPeriod (psg[3], true);
        }
        double psgMix = 0.0;
        if (psgOn)
        {
            double sum = 0.0;
            int count = 0;
            for (psgPhase += 21; psgPhase >= 5; psgPhase -= 5, ++count)
            {
                psgTick();
                sum += psgLevel();
            }
            psgOut = count > 0 ? sum / count : psgOut;
            // En las unidades del acumulador: un canal FM a fondo de escala = ±0,75·256 = ±192; el PSG unipolar 0..A
            // equivale a una cuadrada de ±A/2 (el bloqueo de continua quita el resto)
            psgMix = psgOut * 2.0 * 192.0 * (model == model1 ? 0.513 : 0.513 * 20.0 / 32.0);
        }

        const auto alg = algorithm;
        const auto carrierMask = carriers (alg);
        std::array<double, 2> mix { psgMix * (model == model1 ? 1.0 : 0.0), psgMix * (model == model1 ? 1.0 : 0.0) };
        for (auto& c : ch)
        {
            // Envolventes y fase
            std::array<uint32_t, 4> inc {};
            for (int k = 0; k < 4; ++k)
            {
                auto prm = opParams[(size_t) k];
                if ((carrierMask >> k) & 1)
                    prm.tl = std::min (127, prm.tl + c.velocityTl + c.macroTl);
                const auto reset = egStep (c.op[(size_t) k], prm, c.opKcode[(size_t) k], tick);
                inc[(size_t) k] = phaseIncrement (c, (size_t) k, prm, lfoPm);
                if (reset)
                {
                    c.op[(size_t) k].phase = 0;
                    inc[(size_t) k] = 0;
                }
            }

            // Operadores en el orden del chip (OP1, OP3, OP2, OP4) con sus fuentes de modulación
            const auto fbMod = feedback > 0 ? (c.op1Prev0 + c.op1Prev1) >> (10 - feedback) : 0;
            const auto o1 = opOutput (c.op[0], fbMod);
            const auto in3 = ((alg == 1 || alg == 5) ? c.op1Prev0 : 0) + (alg <= 2 ? c.op2Prev : 0);
            const auto o3 = opOutput (c.op[2], in3 >> 1);
            const auto in2 = (alg == 0 || (alg >= 3 && alg <= 6)) ? o1 : 0;
            const auto o2 = opOutput (c.op[1], in2 >> 1);
            const auto in4 = ((alg == 2 || alg == 5) ? o1 : 0) + (alg == 3 ? c.op2Prev : 0) + ((alg <= 4 && alg != 5) ? o3 : 0);
            const auto o4 = opOutput (c.op[3], in4 >> 1);
            c.op1Prev1 = c.op1Prev0;
            c.op1Prev0 = o1;
            c.op2Prev = o2;
            for (int k = 0; k < 4; ++k)
                c.op[(size_t) k].phase = (c.op[(size_t) k].phase + inc[(size_t) k]) & 0xfffffu;

            // Acumulador del canal (9 bits, saturado en cada suma, en el orden del chip)
            int acc = 0;
            const auto add = [&acc] (int v) { acc = juce::jlimit (-256, 255, acc + (v >> 5)); };
            if (carrierMask & 1) add (o1);
            if (carrierMask & 4) add (o3);
            if (carrierMask & 2) add (o2);
            if (carrierMask & 8) add (o4);
            // DAC activo: el dato de 8 bits sustituye a la salida FM del canal 6 (Nuked: (dacdata ^ 0x80) << 1, 9 bits)
            if (voice == voiceDac && &c == &ch[5])
                acc = (int) (int8_t) (uint8_t) (dacValue ^ 0x80) * 2;

            // Paneo del canal (registro B4h: bit 7 = L, bit 6 = R; solo duro), según el modo del driver de KoniSynth
            const auto k = (int) (&c - ch.data());
            const auto pan = panMode == 1 ? 1 : panMode == 2 ? 2 : panMode == 3 ? 1 + (k & 1) : panMode == 4 ? (k % 3 == 1 ? 0 : 1 + (k % 3) / 2) : 0;
            const bool onL = pan != 2, onR = pan != 1; // 0 = centro, 1 = izquierda, 2 = derecha
            // DAC: YM2612 con ladder (1 ciclo de 4 con la salida, 3 con el signo; ×3). En un lado apagado sale solo el signo
            // los 4 ciclos (Nuked-OPN2, ChOutput). YM3438: 3 de 4 ciclos, y nada en un lado apagado
            if (model == model1)
            {
                int outv = acc, sign = acc >> 8;
                if (outv >= 0) { ++outv; ++sign; }
                const auto active = 3.0 * (outv + 3 * sign) / 4.0, muted = 3.0 * sign;
                mix[0] += onL ? active : muted;
                mix[1] += onR ? active : muted;
            }
            else
            {
                mix[0] += onL ? 0.75 * acc : 0.0;
                mix[1] += onR ? 0.75 * acc : 0.0;
            }
        }
        std::array<float, 2> y {};
        for (size_t side = 0; side < 2; ++side)
        {
            auto v = mix[side] / 384.0; // un canal a fondo de escala (0,75·256) = 0,5
            if (model != clean)
                v = lp[side] = v + lpCoef * (lp[side] - v);
            if (model != model1) // Model 2 / Clean: el PSG no pasa por el filtro del FM
                v += psgMix / 384.0;
            y[side] = (float) v;
        }
        return y;
    }

    double sampleRate = 44100.0;
    SincResampler resampler;
    std::array<Channel, 6> ch {};
    std::array<OpParams, 4> opParams {};
    int model = 0, algorithm = 0, feedback = 0, lfoFreq = -1, pms = 0, ams = 0;
    bool velocityOn = true;
    int lfoCount = 0, lfoQuot = 0, lfoAm = 0;
    int egTimer = 0, egQuot = 0, egShiftLock = 0, egTimerLow = 0;
    std::array<double, 2> lp {}, dcIn {}, dcOut {};
    int settle = 0; // muestras de asentamiento de la salida que quedan (settleOutput)
    double lpCoef = 0.0, dcCoef = 0.0, tuneCents = 0.0;
    bool monoOutput = false; // salida AV/RF del Model 1: L + R
    int panMode = 0; // 0 Center, 1 Left, 2 Right, 3 Alternate (canales pares L, impares R), 4 Spread (L, C, R...)
    float bendSmoothed = 0.0f;
    long long age = 0;

    int voice = -1; // -1: la primera lectura de parámetros fija el modo
    const MacroSet* macros = nullptr; // del bloque (nunca nulo durante render)
    std::array<int, 3> ch3Offset {}; // semitonos de OP1-OP3 en el canal 3 especial
    std::array<Psg, 4> psg {};
    int lfsr = 0x8000, psgPhase = 0, psgNoiseRate = 0, psgA = 0, psgD = 0, psgS = 0, psgR = 0;
    bool psgWhite = true;
    double psgOut = 0.0, frameAcc = 0.0;
    std::array<int, 16> held {};
    int heldCount = 0;
    float monoVel = 1.0f;
    std::vector<uint8_t> dacPcm;
    const SampleData* dacSource = nullptr;
    int dacSourceLength = 0, dacPreparedRate = -1, dacRateIndex = 0, dacRoot = 60, dacNote = 60;
    float dacStart = -1.0f, dacLengthMs = -1.0f;
    double dacRate = 8000.0, dacPos = 0.0, dacStep = 0.0;
    bool dacPlaying = false, dacLoop = false;
    int dacValue = 0x80;
};
