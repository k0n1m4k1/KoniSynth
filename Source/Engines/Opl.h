// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// Motor OPL: Yamaha YM3812 (OPL2: AdLib, Sound Blaster) e YMF262 (OPL3: Sound Blaster Pro 2 / 16). Implementación propia que
// sigue el comportamiento del silicio documentado por Nuked-OPL3 1.8 (Nuke.YKT, LGPL-2.1+; análisis del chip):
//  - fs = 49 716 Hz (14,318 MHz / 288 en el OPL3; 3,58 MHz / 72 en el OPL2); fase de 19 bits: += ((fnum << block) >> 1)·MULT2 >> 1
//    (MULT ×0,5..15, sin 11 ni 13), salida de fase de 10 bits; la modulación suma la salida del modulador a la fase.
//  - Operador: tablas logsin y exp de la ROM (256 entradas cada una, valores de Nuked); formas de onda 0-3 (OPL2) y 0-7 (OPL3).
//  - Envolvente de 9 bits con el temporizador del chip (un paso cada 2 muestras, desplazamiento = bit bajo del contador);
//    ataque exponencial; EGT = sostiene en SL; KSR; KSL por tabla (0 / 1,5 / 3 / 6 dB por octava).
//  - LFO: trémolo de 210 pasos (1 o 4,8 dB, DAM) y vibrato de 8 pasos (7 o 14 cents, DVB), del contador global.
//  - Conexiones: 2 operadores (FM, AM) y, en el OPL3, 4 operadores (4 algoritmos); realimentación del primer operador.
// Del hardware original, además de lo que modela Nuked:
//  - OPL2: el DAC externo YM3014 es de coma flotante, 3 bits de exponente y 10 de mantisa: los bits bajos de las señales
//    grandes se pierden (ymfm de A. Giles, BSD-3, roundtrip_fp). Es el grano característico del AdLib.
//  - OPL3: algunos canales salen una muestra más tarde por un lado (canales 6-8 a la izquierda, 15-17 a la derecha), un
//    efecto del orden de proceso del YMF262 (Nuked, OPL_QUIRK_CHANNELSAMPLEDELAY).
//  - OPL2: solo 4 formas de onda (en el chip, con el bit WSE del registro de test, que activaban los juegos). Mono.
// No verificado: la escala de la mezcla del YM3812 (ymfm la marca como "pendiente de verificar"); aquí, la de Nuked.
// Sin modo de percusión (rítmico) ni CSM en esta versión. Remuestreo a la tasa del host con SincResampler.
// Modo GM (OPL3): el chip tocado por el driver DMX (Doom, Heretic, Hexen, Raptor...), multitímbrico, con el banco DMXOPL3
// (Engines/OplGmBank.h): ver la sección "Modo GM" más abajo.
// Modelos OPLL (YM2413) y VRC7: Engines/Opll.h (Nuked-OPLL).
struct OplEngine
{
    static constexpr double kChipRate = 14318180.0 / 288.0;
    enum Model { opl2, opl3, opllYm2413, opllVrc7 };

    void prepare (double sr, int)
    {
        sampleRate = sr;
        resampler.prepare (kChipRate, sr);
        gold.prepare (kChipRate, sr, kGain);
        opll.prepare (sr);
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v = {};
        timer = 0; egTimer = 0; egState = 0; egAdd = 0; egTimerLo = 0;
        tremoloPos = 0; tremolo = 0; vibPos = 0;
        age = 0;
        gmInit();
        resampler.reset();
        gold.reset();
        opll.reset();
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p)
    {
        const auto m = juce::jlimit (0, 3, (int) p.oplModel->load());
        if (m >= opllYm2413)
        {
            if (model < opllYm2413) for (auto& v : voices) v = {};
            model = m;
            opll.render (out, midi, p, m == opllVrc7);
            return;
        }
        readParams (p);
        bendSmoothed += 0.2f * (p.pitchBend.load() - bendSmoothed);
        tuneCents = (double) p.fineTune->load() + 100.0 * bendSmoothed;
        if (! gm) // en GM la frecuencia la pone el driver (gmUpdateFrequency)
            for (auto& v : voices)
                if (v.note >= 0) setPitch (v);

        // AdLib Gold (Engines/OplGold.h): solo con el OPL3, la única variante que montaba la tarjeta
        const auto goldOn = model == opl3 && p.oplGold->load() > 0.5f;
        if (goldOn != goldWasOn) { goldWasOn = goldOn; gold.reset(); }
        if (goldOn)
            gold.apply (juce::roundToInt (p.oplGoldBass->load()), juce::roundToInt (p.oplGoldTreble->load()),
                        juce::roundToInt (p.oplGoldStereo->load()), juce::roundToInt (p.oplGoldSurround->load()),
                        juce::roundToInt (p.oplGoldLevel->load()));
        const auto surround = goldOn && gold.surroundOn();

        auto it = midi.cbegin();
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
                gm ? gmHandleMidi ((*it).getMessage()) : handleMidi ((*it).getMessage());
            auto lr = resampler.nextStereo ([this, surround]
            {
                const auto s = chipSample();
                if (surround) gold.push (s[0], s[1]); // entrada del YM7128B, a la tasa del chip
                return s;
            });
            if (model == opl2 || monoOutput)
                lr[0] = lr[1] = 0.5f * (lr[0] + lr[1]);
            if (goldOn)
                lr = gold.process (lr);
            if (out.getNumChannels() >= 2) { out.addSample (0, i, (float) lr[0]); out.addSample (1, i, (float) lr[1]); }
            else if (out.getNumChannels() == 1) out.addSample (0, i, (float) (0.5 * (lr[0] + lr[1])));
        }
    }

    // Algoritmos (operadores 1-4 en el orden del chip; "->" modula, "+" suma)
    static const char* describe (bool fourOp, int alg)
    {
        static const char* two[2] = { "1 -> 2", "1 + 2" };
        static const char* four[4] = { "1 -> 2 -> 3 -> 4", "(1 -> 2) + (3 -> 4)", "1 + (2 -> 3 -> 4)", "1 + (2 -> 3) + 4" };
        return fourOp ? four[juce::jlimit (0, 3, alg)] : two[alg & 1];
    }

    // Portadoras: bit k = operador k
    static int carrierMask (bool four, int a)
    {
        if (! four) return (a & 1) == 0 ? 0b0010 : 0b0011;
        static const int m[4] = { 0b1000, 0b1010, 0b1001, 0b1101 };
        return m[juce::jlimit (0, 3, a)];
    }

private:
    enum EgGen { attack, decay, sustain, release };

    struct OpRegs { bool am = false, vib = false, egt = true, ksr = false; int mult = 1, ksl = 0, tl = 63, ar = 15, dr = 0, sl = 0, rr = 7, wf = 0; };

    struct Slot
    {
        uint32_t phase = 0;
        int phaseOut = 0, egRout = 0x1ff, egOut = 0x1ff, gen = release, out = 0, prout = 0;
        bool key = false;
    };

    struct Voice
    {
        std::array<Slot, 4> slot {};
        int fnum = 0, block = 0, ksv = 0, kslBase = 0;
        int note = -1, velocityTl = 0, channel = 0, prevAccLeft = 0, prevAccRight = 0;
        bool gate = false;
        long long age = 0;
        // Modo GM: el instrumento cargado en el canal del chip (registros ya decodificados), el canal MIDI del driver (numeración
        // MUS: la percusión es el 15), la tecla, la voz del instrumento (0 o 1), el volumen de la nota y el registro de frecuencia
        std::array<OpRegs, 2> gmOp {};
        int gmFeedback = 0, gmChannel = -1, gmKey = 0, gmInstrVoice = 0, gmNoteVolume = 0, gmFreq = 0, gmPan = 0x30;
        bool gmAdditive = false;
        const OplGmBank::Instrument* gmInstr = nullptr;
    };

    // ---- Tablas de la ROM (Nuked-OPL3)
    static const uint16_t* logsin()
    {
        static const uint16_t t[256] = {
            0x859, 0x6c3, 0x607, 0x58b, 0x52e, 0x4e4, 0x4a6, 0x471, 0x443, 0x41a, 0x3f5, 0x3d3, 0x3b5, 0x398, 0x37e, 0x365,
            0x34e, 0x339, 0x324, 0x311, 0x2ff, 0x2ed, 0x2dc, 0x2cd, 0x2bd, 0x2af, 0x2a0, 0x293, 0x286, 0x279, 0x26d, 0x261,
            0x256, 0x24b, 0x240, 0x236, 0x22c, 0x222, 0x218, 0x20f, 0x206, 0x1fd, 0x1f5, 0x1ec, 0x1e4, 0x1dc, 0x1d4, 0x1cd,
            0x1c5, 0x1be, 0x1b7, 0x1b0, 0x1a9, 0x1a2, 0x19b, 0x195, 0x18f, 0x188, 0x182, 0x17c, 0x177, 0x171, 0x16b, 0x166,
            0x160, 0x15b, 0x155, 0x150, 0x14b, 0x146, 0x141, 0x13c, 0x137, 0x133, 0x12e, 0x129, 0x125, 0x121, 0x11c, 0x118,
            0x114, 0x10f, 0x10b, 0x107, 0x103, 0x0ff, 0x0fb, 0x0f8, 0x0f4, 0x0f0, 0x0ec, 0x0e9, 0x0e5, 0x0e2, 0x0de, 0x0db,
            0x0d7, 0x0d4, 0x0d1, 0x0cd, 0x0ca, 0x0c7, 0x0c4, 0x0c1, 0x0be, 0x0bb, 0x0b8, 0x0b5, 0x0b2, 0x0af, 0x0ac, 0x0a9,
            0x0a7, 0x0a4, 0x0a1, 0x09f, 0x09c, 0x099, 0x097, 0x094, 0x092, 0x08f, 0x08d, 0x08a, 0x088, 0x086, 0x083, 0x081,
            0x07f, 0x07d, 0x07a, 0x078, 0x076, 0x074, 0x072, 0x070, 0x06e, 0x06c, 0x06a, 0x068, 0x066, 0x064, 0x062, 0x060,
            0x05e, 0x05c, 0x05b, 0x059, 0x057, 0x055, 0x053, 0x052, 0x050, 0x04e, 0x04d, 0x04b, 0x04a, 0x048, 0x046, 0x045,
            0x043, 0x042, 0x040, 0x03f, 0x03e, 0x03c, 0x03b, 0x039, 0x038, 0x037, 0x035, 0x034, 0x033, 0x031, 0x030, 0x02f,
            0x02e, 0x02d, 0x02b, 0x02a, 0x029, 0x028, 0x027, 0x026, 0x025, 0x024, 0x023, 0x022, 0x021, 0x020, 0x01f, 0x01e,
            0x01d, 0x01c, 0x01b, 0x01a, 0x019, 0x018, 0x017, 0x017, 0x016, 0x015, 0x014, 0x014, 0x013, 0x012, 0x011, 0x011,
            0x010, 0x00f, 0x00f, 0x00e, 0x00d, 0x00d, 0x00c, 0x00c, 0x00b, 0x00a, 0x00a, 0x009, 0x009, 0x008, 0x008, 0x007,
            0x007, 0x007, 0x006, 0x006, 0x005, 0x005, 0x005, 0x004, 0x004, 0x004, 0x003, 0x003, 0x003, 0x002, 0x002, 0x002,
            0x002, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000 };
        return t;
    }

    static const uint16_t* expTable()
    {
        static const uint16_t t[256] = {
            0x7fa, 0x7f5, 0x7ef, 0x7ea, 0x7e4, 0x7df, 0x7da, 0x7d4, 0x7cf, 0x7c9, 0x7c4, 0x7bf, 0x7b9, 0x7b4, 0x7ae, 0x7a9,
            0x7a4, 0x79f, 0x799, 0x794, 0x78f, 0x78a, 0x784, 0x77f, 0x77a, 0x775, 0x770, 0x76a, 0x765, 0x760, 0x75b, 0x756,
            0x751, 0x74c, 0x747, 0x742, 0x73d, 0x738, 0x733, 0x72e, 0x729, 0x724, 0x71f, 0x71a, 0x715, 0x710, 0x70b, 0x706,
            0x702, 0x6fd, 0x6f8, 0x6f3, 0x6ee, 0x6e9, 0x6e5, 0x6e0, 0x6db, 0x6d6, 0x6d2, 0x6cd, 0x6c8, 0x6c4, 0x6bf, 0x6ba,
            0x6b5, 0x6b1, 0x6ac, 0x6a8, 0x6a3, 0x69e, 0x69a, 0x695, 0x691, 0x68c, 0x688, 0x683, 0x67f, 0x67a, 0x676, 0x671,
            0x66d, 0x668, 0x664, 0x65f, 0x65b, 0x657, 0x652, 0x64e, 0x649, 0x645, 0x641, 0x63c, 0x638, 0x634, 0x630, 0x62b,
            0x627, 0x623, 0x61e, 0x61a, 0x616, 0x612, 0x60e, 0x609, 0x605, 0x601, 0x5fd, 0x5f9, 0x5f5, 0x5f0, 0x5ec, 0x5e8,
            0x5e4, 0x5e0, 0x5dc, 0x5d8, 0x5d4, 0x5d0, 0x5cc, 0x5c8, 0x5c4, 0x5c0, 0x5bc, 0x5b8, 0x5b4, 0x5b0, 0x5ac, 0x5a8,
            0x5a4, 0x5a0, 0x59c, 0x599, 0x595, 0x591, 0x58d, 0x589, 0x585, 0x581, 0x57e, 0x57a, 0x576, 0x572, 0x56f, 0x56b,
            0x567, 0x563, 0x560, 0x55c, 0x558, 0x554, 0x551, 0x54d, 0x549, 0x546, 0x542, 0x53e, 0x53b, 0x537, 0x534, 0x530,
            0x52c, 0x529, 0x525, 0x522, 0x51e, 0x51b, 0x517, 0x514, 0x510, 0x50c, 0x509, 0x506, 0x502, 0x4ff, 0x4fb, 0x4f8,
            0x4f4, 0x4f1, 0x4ed, 0x4ea, 0x4e7, 0x4e3, 0x4e0, 0x4dc, 0x4d9, 0x4d6, 0x4d2, 0x4cf, 0x4cc, 0x4c8, 0x4c5, 0x4c2,
            0x4be, 0x4bb, 0x4b8, 0x4b5, 0x4b1, 0x4ae, 0x4ab, 0x4a8, 0x4a4, 0x4a1, 0x49e, 0x49b, 0x498, 0x494, 0x491, 0x48e,
            0x48b, 0x488, 0x485, 0x482, 0x47e, 0x47b, 0x478, 0x475, 0x472, 0x46f, 0x46c, 0x469, 0x466, 0x463, 0x460, 0x45d,
            0x45a, 0x457, 0x454, 0x451, 0x44e, 0x44b, 0x448, 0x445, 0x442, 0x43f, 0x43c, 0x439, 0x436, 0x433, 0x430, 0x42d,
            0x42a, 0x428, 0x425, 0x422, 0x41f, 0x41c, 0x419, 0x416, 0x414, 0x411, 0x40e, 0x40b, 0x408, 0x406, 0x403, 0x400 };
        return t;
    }

    static int expOut (uint32_t level)
    {
        if (level > 0x1fff) level = 0x1fff;
        return (expTable()[level & 0xff] << 1) >> (level >> 8);
    }

    // Las 8 formas de onda del OPL3 (las 4 primeras, también OPL2), como OPL3_EnvelopeCalcSin0..7
    static int waveform (int wf, int phase, int env)
    {
        const auto* ls = logsin();
        phase &= 0x3ff;
        uint32_t out = 0;
        bool neg = false;
        const auto envShift = (uint32_t) env << 3;
        switch (wf)
        {
            case 0: neg = (phase & 0x200) != 0; out = (phase & 0x100) ? ls[(phase & 0xff) ^ 0xff] : ls[phase & 0xff]; break;
            case 1: out = (phase & 0x200) ? 0x1000 : (phase & 0x100) ? ls[(phase & 0xff) ^ 0xff] : ls[phase & 0xff]; break;
            case 2: out = (phase & 0x100) ? ls[(phase & 0xff) ^ 0xff] : ls[phase & 0xff]; break;
            case 3: out = (phase & 0x100) ? 0x1000 : ls[phase & 0xff]; break;
            case 4: neg = (phase & 0x300) == 0x100;
                    out = (phase & 0x200) ? 0x1000 : (phase & 0x80) ? ls[((phase ^ 0xff) << 1) & 0xff] : ls[(phase << 1) & 0xff]; break;
            case 5: out = (phase & 0x200) ? 0x1000 : (phase & 0x80) ? ls[((phase ^ 0xff) << 1) & 0xff] : ls[(phase << 1) & 0xff]; break;
            case 6: neg = (phase & 0x200) != 0; out = 0; break;
            default:
                if (phase & 0x200) { neg = true; phase = (phase & 0x1ff) ^ 0x1ff; }
                out = (uint32_t) phase << 3;
                break;
        }
        const auto v = expOut (out + envShift);
        return neg ? ~v : v; // como el XOR 0xffff del chip (complemento a uno)
    }

    void readParams (const Params& p)
    {
        const auto newModel = juce::jlimit (0, 1, (int) p.oplModel->load());
        if (model >= opllYm2413) opll.reset(); // al volver de OPLL, el chip OPLL queda en silencio
        const auto newGm = newModel == opl3 && p.oplGm->load() > 0.5f;
        const auto newFour = newModel == opl3 && ! newGm && p.oplFourOp->load() > 0.5f;
        if (newModel != model || newFour != fourOp || newGm != gm)
        {
            for (auto& v : voices) v = {};
            model = newModel;
            fourOp = newFour;
            gm = newGm;
            gmInit();
        }
        alg = juce::jlimit (0, 3, (int) p.oplAlgorithm->load());
        if (! fourOp) alg &= 1;
        feedback = juce::jlimit (0, 7, (int) p.oplFeedback->load());
        tremoloShift = p.oplDeepTremolo->load() > 0.5f ? 2 : 4;   // DAM: 4,8 dB / 1 dB
        vibShift = p.oplDeepVibrato->load() > 0.5f ? 0 : 1;       // DVB: 14 / 7 cents
        velocityOn = p.oplVelocity->load() > 0.5f;
        panMode = juce::jlimit (0, 3, (int) p.oplPan->load());
        monoOutput = p.oplOutput->load() > 0.5f;
        for (size_t k = 0; k < 4; ++k)
        {
            const auto& s = p.oplOp[k];
            auto& o = ops[k];
            o.am = s.am->load() > 0.5f; o.vib = s.vib->load() > 0.5f; o.egt = s.egt->load() > 0.5f; o.ksr = s.ksr->load() > 0.5f;
            o.mult = (int) s.mult->load(); o.ksl = (int) s.ksl->load(); o.tl = (int) s.tl->load();
            o.ar = (int) s.ar->load(); o.dr = (int) s.dr->load(); o.rr = (int) s.rr->load();
            const auto sl = (int) s.sl->load();
            o.sl = sl == 15 ? 0x1f : sl; // SL 15 = 93 dB
            o.wf = (int) s.wave->load() & (model == opl2 ? 3 : 7);
        }
    }

    int numVoices() const { return model == opl2 ? 9 : fourOp ? 6 : 18; } // GM: 18 (OPL3, 2 operadores)

    // Canal real del chip de cada voz (para el retardo de canal del OPL3): 2 operadores 0..17; 4 operadores, los pares
    // (0,3) (1,4) (2,5) (9,12) (10,13) (11,14), cuya salida sale por el segundo canal del par
    int chipChannel (int v) const
    {
        static const int four[6] = { 3, 4, 5, 12, 13, 14 };
        return fourOp ? four[v] : v;
    }

    // fnum/bloque: f = fnum · fs · 2^(bloque - 20), bloque mínimo con fnum < 1024
    void setPitch (Voice& v) const
    {
        const auto f = 440.0 * std::exp2 ((v.note - 69) / 12.0 + tuneCents / 1200.0) * KoniTuning::ratio (v.note);
        int block = 0;
        double fn = f * 1048576.0 / kChipRate;
        while (fn >= 1023.5 && block < 7) { fn *= 0.5; ++block; }
        v.fnum = juce::jlimit (0, 1023, (int) std::lround (fn));
        v.block = block;
        v.ksv = (block << 1) | ((v.fnum >> 9) & 1); // NTS = 0
        static const int kslrom[16] = { 0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64 };
        v.kslBase = std::max (0, (kslrom[v.fnum >> 6] << 2) - ((8 - block) << 5));
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        const auto n = numVoices();
        if (m.isNoteOn())
        {
            int target = -1;
            for (int k = 0; k < n; ++k)
                if (voices[(size_t) k].note == m.getNoteNumber()) target = k;
            if (target < 0)
            {
                long long best = std::numeric_limits<long long>::max();
                for (int pass = 0; pass < 2 && target < 0; ++pass)
                    for (int k = 0; k < n; ++k)
                        if ((pass == 1 || ! voices[(size_t) k].gate) && voices[(size_t) k].age < best) { best = voices[(size_t) k].age; target = k; }
            }
            auto& v = voices[(size_t) target];
            v.note = m.getNoteNumber();
            v.gate = true;
            v.age = ++age;
            v.channel = chipChannel (target);
            // Velocidad (driver de KoniSynth): TL de las portadoras en pasos de 0,75 dB
            v.velocityTl = velocityOn ? juce::jlimit (0, 63, (int) std::lround (-20.0 * std::log10 (std::max (0.01f, m.getFloatVelocity())) / 0.75)) : 0;
            setPitch (v);
            for (auto& s : v.slot) s.key = true; // si estaba sonando (key on sin key off), el chip no reinicia la envolvente
        }
        else if (m.isNoteOff())
        {
            for (int k = 0; k < n; ++k)
                if (voices[(size_t) k].note == m.getNoteNumber() && voices[(size_t) k].gate)
                {
                    voices[(size_t) k].gate = false;
                    for (auto& s : voices[(size_t) k].slot) s.key = false;
                }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices)
            {
                v.gate = false;
                for (auto& s : v.slot) { s.key = false; if (m.isAllSoundOff()) { s.egRout = 0x1ff; s.gen = release; } }
            }
        }
    }

    int carriers() const { return carrierMask (fourOp, alg); }

    // Envolvente de un operador (OPL3_EnvelopeCalc)
    void envelope (Slot& s, const OpRegs& o, const Voice& v, int extraTl)
    {
        // KSL: el parámetro va en dB/oct (0, 1,5, 3, 6: lo comparte con el OPLL, cuyo registro es monótono). En el OPL2/OPL3 el
        // registro no lo es: 1 = 3 dB/oct y 2 = 1,5 dB/oct (kslshift {8, 1, 2, 0} por valor de registro, Nuked-OPL3 opl3.c;
        // hoja de datos del YM3812). Por eso aquí se indexa por dB: 0 -> registro 0, 1,5 -> 2, 3 -> 1, 6 -> 3
        static const int kslShift[4] = { 8, 2, 1, 0 };
        static const int incStep[4][4] = { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 0, 1, 0 }, { 1, 1, 1, 0 } };
        s.egOut = s.egRout + ((std::min (63, o.tl + extraTl)) << 2) + (v.kslBase >> kslShift[o.ksl]) + (o.am ? tremolo : 0);
        int regRate = 0;
        bool reset = false;
        if (s.key && s.gen == release) { reset = true; regRate = o.ar; }
        else switch (s.gen)
        {
            case attack: regRate = o.ar; break;
            case decay: regRate = o.dr; break;
            case sustain: regRate = o.egt ? 0 : o.rr; break;
            default: regRate = o.rr; break;
        }
        pgReset = reset;
        const auto ks = v.ksv >> ((o.ksr ? 0 : 1) << 1);
        const auto rate = ks + (regRate << 2);
        auto rateHi = rate >> 2;
        const auto rateLo = rate & 3;
        if (rateHi & 0x10) rateHi = 0x0f;
        const auto egShift = rateHi + egAdd;
        int shift = 0;
        if (regRate != 0)
        {
            if (rateHi < 12)
            {
                if (egState)
                    shift = egShift == 12 ? 1 : egShift == 13 ? (rateLo >> 1) & 1 : egShift == 14 ? rateLo & 1 : 0;
            }
            else
            {
                shift = (rateHi & 3) + incStep[rateLo][egTimerLo];
                if (shift & 4) shift = 3;
                if (! shift) shift = egState;
            }
        }
        auto rout = s.egRout;
        int inc = 0;
        if (reset && rateHi == 0x0f) rout = 0;
        const auto off = (s.egRout & 0x1f8) == 0x1f8;
        if (s.gen != attack && ! reset && off) rout = 0x1ff;
        switch (s.gen)
        {
            case attack:
                if (s.egRout == 0) s.gen = decay;
                else if (s.key && shift > 0 && rateHi != 0x0f) inc = ~s.egRout >> (4 - shift);
                break;
            case decay:
                if ((s.egRout >> 4) == o.sl) s.gen = sustain;
                else if (! off && ! reset && shift > 0) inc = 1 << (shift - 1);
                break;
            default:
                if (! off && ! reset && shift > 0) inc = 1 << (shift - 1);
                break;
        }
        s.egRout = (rout + inc) & 0x1ff;
        if (reset) s.gen = attack;
        if (! s.key) s.gen = release;
    }

    // Fase de un operador (OPL3_PhaseGenerate, sin el modo rítmico)
    void phase (Slot& s, const OpRegs& o, const Voice& v) const
    {
        static const int mt[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };
        auto fnum = v.fnum;
        if (o.vib)
        {
            int range = (fnum >> 7) & 7;
            if (! (vibPos & 3)) range = 0;
            else if (vibPos & 1) range >>= 1;
            range >>= vibShift;
            if (vibPos & 4) range = -range;
            fnum += range;
        }
        const auto base = (uint32_t) ((fnum << v.block) >> 1);
        s.phaseOut = (int) ((s.phase >> 9) & 0x3ff);
        if (pgReset) s.phase = 0;
        s.phase += (base * (uint32_t) mt[o.mult]) >> 1;
    }

    // Una muestra del chip: por voz, los operadores en orden (cada uno ve la salida actual del anterior); realimentación con
    // las dos salidas anteriores del primero
    std::array<float, 2> chipSample()
    {
        const auto n = numVoices();
        const auto carrierMask = carriers();
        int mixL = 0, mixR = 0;
        for (int k = 0; k < n; ++k)
        {
            auto& v = voices[(size_t) k];
            const auto opCount = fourOp ? 4 : 2;
            // GM: cada canal del chip con su instrumento (conexión FM = solo la segunda es portadora; AM = las dos)
            const auto voiceCarriers = gm ? (v.gmAdditive ? 0b11 : 0b10) : carrierMask;
            const auto voiceFeedback = gm ? v.gmFeedback : feedback;
            int acc = 0;
            int prevOut = 0;
            for (int j = 0; j < opCount; ++j)
            {
                auto& s = v.slot[(size_t) j];
                const auto& o = gm ? v.gmOp[(size_t) j] : ops[(size_t) j];
                // Entrada de modulación: el primero, su realimentación; los demás, el anterior si el algoritmo lo encadena
                int mod = 0;
                if (j == 0)
                    mod = voiceFeedback ? (s.prout + s.out) >> (9 - voiceFeedback) : 0;
                else if (gm ? ! v.gmAdditive : chains (j))
                    mod = prevOut;
                s.prout = s.out;
                envelope (s, o, v, (! gm && ((carrierMask >> j) & 1)) ? v.velocityTl : 0); // GM: el volumen va en el TL
                phase (s, o, v);
                s.out = waveform (o.wf, s.phaseOut + mod, s.egOut);
                prevOut = s.out;
                if ((voiceCarriers >> j) & 1) acc += s.out;
            }
            acc = (int) (int16_t) acc;
            // Paneo (registro C0h, bits 4-5: A = izquierda, B = derecha), según el modo del driver de KoniSynth; en GM, el del
            // driver DMX (CC 10)
            const auto pan = panMode == 1 ? 1 : panMode == 2 ? 2 : panMode == 3 ? 1 + (k & 1) : 0;
            const auto onL = model == opl2 || (gm ? (v.gmPan & 0x10) != 0 : pan != 2);
            const auto onR = model == opl2 || (gm ? (v.gmPan & 0x20) != 0 : pan != 1);
            // Retardo de canal del OPL3: la mezcla de un lado se toma antes de procesar el último operador de esos canales
            const auto lateL = model == opl3 && v.channel >= 6 && v.channel <= 8;
            const auto lateR = model == opl3 && v.channel >= 15;
            if (onL) mixL += lateL ? v.prevAccLeft : acc;
            if (onR) mixR += lateR ? v.prevAccRight : acc;
            v.prevAccLeft = v.prevAccRight = acc;
        }
        advanceTimers();
        auto l = juce::jlimit (-32768, 32767, mixL), r = juce::jlimit (-32768, 32767, mixR);
        if (model == opl2) { l = roundtripFp (l); r = roundtripFp (r); }
        return { (float) l * kGain, (float) r * kGain };
    }

    // ¿El operador j recibe la salida del anterior? (Nuked, OPL3_ChannelSetupAlg)
    bool chains (int j) const
    {
        if (! fourOp) return alg == 0;
        switch (alg)
        {
            case 0:  return true;                       // 1 -> 2 -> 3 -> 4
            case 1:  return j != 2;                     // (1 -> 2) + (3 -> 4)
            case 2:  return j != 1;                     // 1 + (2 -> 3 -> 4)
            default: return j == 2;                     // 1 + (2 -> 3) + 4
        }
    }

    // DAC YM3014 (coma flotante 3.10): exponente mínimo 1; se pierden los bits por debajo de la mantisa (ymfm roundtrip_fp)
    static int roundtripFp (int value)
    {
        const auto scan = value ^ (value >> 31);
        int lz = 0;
        for (int b = 14; b >= 0 && ! ((scan >> b) & 1); --b) ++lz; // ceros a la izquierda desde el bit 14
        auto exponent = std::max (7 - std::min (7, lz), 1) - 1;
        return value & ~((1 << exponent) - 1);
    }

    // Contadores globales: trémolo (210 pasos), vibrato (8 pasos) y temporizador de la envolvente (OPL3_Generate4Ch)
    void advanceTimers()
    {
        if ((timer & 0x3f) == 0x3f) tremoloPos = (tremoloPos + 1) % 210;
        tremolo = (tremoloPos < 105 ? tremoloPos : 210 - tremoloPos) >> tremoloShift;
        if ((timer & 0x3ff) == 0x3ff) vibPos = (vibPos + 1) & 7;
        ++timer;
        if (egState)
        {
            int shift = 0;
            while (shift < 13 && ((egTimer >> shift) & 1) == 0) ++shift;
            egAdd = shift > 12 ? 0 : shift + 1;
            egTimerLo = (int) (egTimer & 3);
            egTimer = (egTimer + 1) & 0xfffffffffull;
        }
        egState ^= 1;
    }

    //==========================================================================================================================
    // Modo GM: el driver DMX de Digital Expressions (Paul Radek), el de Doom 1.9 y otros juegos de 1993-1996, tal como lo
    // reconstruye Chocolate Doom (src/i_oplmusic.c, GPL-2.0+; comportamiento comprobado contra el original por sus autores).
    // Banco GENMIDI de DMXOPL3. Lo que hace el driver:
    //  - 18 canales del chip en una lista libre (FIFO) y una de ocupados; una nota toma el primero libre y, al soltarse, el
    //    canal vuelve al final de la libre. Sin canales libres se roba: el último ocupado que sea segunda voz o de canal MIDI
    //    mayor o igual (ReplaceExistingVoice de la v1.9). Los instrumentos de dos voces ocupan dos canales del chip.
    //  - Canal 10 = percusión: notas 35-81 -> instrumentos 128-174, tocados en la nota 60 (o su nota fija). Para el driver,
    //    el canal de percusión es el 15 (formato MUS) y el 16 el 10: cuenta en el orden de robo.
    //  - Frecuencia por tabla propia (frequency_curve, 1/32 de semitono): índice 64 + 32·nota + bend, nota con su
    //    desplazamiento y plegada a 0-95; la segunda voz se desafina (afinación fina / 2 - 64)/32 semitonos. No sigue la
    //    afinación de KoniSynth (Scala, Fine Tune): la tabla es la del driver.
    //  - Volumen: TL de la portadora = 63 - tabla[vel] · 2·(tabla[vol CC 7] + 1) / 512 (volume_mapping_table); el nivel de la
    //    portadora del banco no se usa. En conexión AM el modulador baja con él (máximo de su nivel y el de la portadora).
    //  - Pitch bend: solo el MSB, ±64 = ±2 semitonos. CC 7 volumen (100 al empezar), CC 10 paneo en 3 posiciones con los
    //    lados cambiados (fallo del DMX que Chocolate Doom conserva por defecto), CC 123 apagar notas. Nada más (ni sustain).
    struct GmChannel { int program = 0, volume = 100, pan = 0x30, bend = 0; };

    void gmInit()
    {
        gmFreeCount = 18;
        gmAllocCount = 0;
        for (int k = 0; k < 18; ++k) gmFree[(size_t) k] = k;
        for (auto& c : gmChannels) c = {};
    }

    static int gmMusChannel (int midiChannel0) { return midiChannel0 == 9 ? 15 : midiChannel0 == 15 ? 9 : midiChannel0; }

    void gmHandleMidi (const juce::MidiMessage& m)
    {
        if (m.getChannel() < 1) return;
        const auto ch0 = m.getChannel() - 1;
        const auto mus = gmMusChannel (ch0);
        auto& chan = gmChannels[(size_t) mus];
        if (m.isNoteOn())
        {
            const auto key = m.getNoteNumber();
            int instrument = chan.program, note = key;
            if (ch0 == 9)
            {
                if (key < 35 || key > 81) return;
                instrument = OplGmBank::kMelodic + key - 35;
                note = 60;
            }
            const auto& instr = OplGmBank::get (instrument);
            if (gmFreeCount == 0)
                gmReplaceExistingVoice();
            gmVoiceKeyOn (mus, instr, 0, note, key, m.getVelocity());
            if (instr.flags & 0x04)
                gmVoiceKeyOn (mus, instr, 1, note, key, m.getVelocity());
        }
        else if (m.isNoteOff()) // también nota con velocidad 0
        {
            for (int i = 0; i < gmAllocCount; ++i)
            {
                const auto& v = voices[(size_t) gmAlloc[(size_t) i]];
                if (v.gmChannel == mus && v.gmKey == m.getNoteNumber())
                    gmReleaseVoice (i--);
            }
        }
        else if (m.isProgramChange())
            chan.program = m.getProgramChangeNumber();
        else if (m.isControllerOfType (7))
        {
            chan.volume = m.getControllerValue();
            for (int k = 0; k < 18; ++k)
                if (voices[(size_t) k].gmChannel == mus) gmSetVolume (voices[(size_t) k]);
        }
        else if (m.isControllerOfType (10))
        {
            const auto value = m.getControllerValue();
            chan.pan = value >= 96 ? 0x10 : value <= 48 ? 0x20 : 0x30; // lados cambiados, como el DMX
            for (auto& v : voices)
                if (v.gmChannel == mus) v.gmPan = chan.pan;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (int i = 0; i < gmAllocCount; ++i)
                if (m.isAllSoundOff() || voices[(size_t) gmAlloc[(size_t) i]].gmChannel == mus)
                    gmReleaseVoice (i--);
            if (m.isAllSoundOff())
                for (auto& v : voices)
                    for (auto& s : v.slot) { s.egRout = 0x1ff; s.gen = release; }
        }
        else if (m.isPitchWheel())
        {
            chan.bend = (m.getPitchWheelValue() >> 7) - 64; // solo el MSB
            // Las voces del canal se reafinan y pasan al final de la lista de ocupadas (orden de robo, como el driver)
            std::array<int, 18> updated {}, others {};
            int nu = 0, no = 0;
            for (int i = 0; i < gmAllocCount; ++i)
            {
                const auto k = gmAlloc[(size_t) i];
                if (voices[(size_t) k].gmChannel == mus) { gmUpdateFrequency (voices[(size_t) k]); updated[(size_t) nu++] = k; }
                else others[(size_t) no++] = k;
            }
            for (int i = 0; i < no; ++i) gmAlloc[(size_t) i] = others[(size_t) i];
            for (int i = 0; i < nu; ++i) gmAlloc[(size_t) (no + i)] = updated[(size_t) i];
        }
    }

    void gmVoiceKeyOn (int mus, const OplGmBank::Instrument& instr, int instrVoice, int note, int key, int velocity)
    {
        if (gmFreeCount == 0) return;
        const auto k = gmFree[0];
        for (int i = 1; i < gmFreeCount; ++i) gmFree[(size_t) (i - 1)] = gmFree[(size_t) i];
        --gmFreeCount;
        gmAlloc[(size_t) gmAllocCount++] = k;

        auto& v = voices[(size_t) k];
        v.gmChannel = mus;
        v.gmKey = key;
        v.note = (instr.flags & 0x01) ? instr.fixedNote : note;
        v.gmPan = gmChannels[(size_t) mus].pan;
        v.channel = k;
        if (v.gmInstr != &instr || v.gmInstrVoice != instrVoice) // el driver no recarga un instrumento que ya está
        {
            v.gmInstr = &instr;
            v.gmInstrVoice = instrVoice;
            const auto& d = instr.voice[instrVoice];
            v.gmAdditive = (d.feedback & 0x01) != 0;
            v.gmFeedback = (d.feedback >> 1) & 7;
            gmDecodeOp (d.mod, v.gmOp[0]);
            gmDecodeOp (d.car, v.gmOp[1]);
            v.gmOp[1].tl = 63;                                  // la portadora, al mínimo hasta fijar el volumen
            if (v.gmAdditive) v.gmOp[0].tl = 63;                // en AM, también el modulador
        }
        v.gmNoteVolume = velocity;
        gmSetVolume (v);
        v.gmFreq = 0;
        gmUpdateFrequency (v);
        v.gate = true;
        v.age = ++age;
        for (auto& s : v.slot) s.key = true;                    // bit 5 de B0h (key on) al escribir la frecuencia
    }

    void gmReleaseVoice (int index)
    {
        const auto k = gmAlloc[(size_t) index];
        auto& v = voices[(size_t) k];
        v.gate = false;
        for (auto& s : v.slot) s.key = false;
        v.gmChannel = -1;
        v.note = 0;
        for (int i = index; i < gmAllocCount - 1; ++i) gmAlloc[(size_t) i] = gmAlloc[(size_t) (i + 1)];
        --gmAllocCount;
        gmFree[(size_t) gmFreeCount++] = k; // al final de la libre, como el driver
    }

    void gmReplaceExistingVoice()
    {
        if (gmAllocCount == 0) return;
        int result = 0;
        for (int i = 0; i < gmAllocCount; ++i)
        {
            const auto& v = voices[(size_t) gmAlloc[(size_t) i]];
            if (v.gmInstrVoice != 0 || v.gmChannel >= voices[(size_t) gmAlloc[(size_t) result]].gmChannel)
                result = i;
        }
        gmReleaseVoice (result);
    }

    // Registros de un operador del banco: 20h (AM VIB EGT KSR MULT), 60h (AR DR), 80h (SL RR), E0h, KSL y nivel de 40h
    static void gmDecodeOp (const OplGmBank::Op& b, OpRegs& o)
    {
        o.am = (b.tremolo & 0x80) != 0; o.vib = (b.tremolo & 0x40) != 0; o.egt = (b.tremolo & 0x20) != 0; o.ksr = (b.tremolo & 0x10) != 0;
        o.mult = b.tremolo & 0x0f;
        o.ar = b.attack >> 4; o.dr = b.attack & 0x0f;
        const auto sl = b.sustain >> 4;
        o.sl = sl == 15 ? 0x1f : sl;
        o.rr = b.sustain & 0x0f;
        o.wf = b.waveform & 7;
        static const int kslIndex[4] = { 0, 2, 1, 3 }; // registro -> índice en dB (0, 3, 1,5, 6 dB/oct), ver envelope()
        o.ksl = kslIndex[(b.scale >> 6) & 3];
        o.tl = b.level & 0x3f;
    }

    // Volumen de la nota (SetVoiceVolume del driver)
    void gmSetVolume (Voice& v)
    {
        static const int volumeTable[128] = {
            0, 1, 3, 5, 6, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23, 25, 26, 27, 29, 30, 32, 33, 34, 36, 37, 39, 41, 43, 45, 47, 49,
            50, 52, 54, 55, 57, 59, 60, 61, 63, 64, 66, 67, 68, 69, 71, 72, 73, 74, 75, 76, 77, 79, 80, 81, 82, 83, 84, 84, 85, 86,
            87, 88, 89, 90, 91, 92, 92, 93, 94, 95, 96, 96, 97, 98, 99, 99, 100, 101, 101, 102, 103, 103, 104, 105, 105, 106, 107,
            107, 108, 109, 109, 110, 110, 111, 112, 112, 113, 113, 114, 114, 115, 115, 116, 117, 117, 118, 118, 119, 119, 120,
            120, 121, 121, 122, 122, 123, 123, 123, 124, 124, 125, 125, 126, 126, 127, 127 };
        if (v.gmInstr == nullptr || v.gmChannel < 0) return;
        const auto& d = v.gmInstr->voice[v.gmInstrVoice];
        const auto midiVolume = 2 * (volumeTable[juce::jlimit (0, 127, gmChannels[(size_t) v.gmChannel].volume)] + 1);
        const auto full = (volumeTable[juce::jlimit (0, 127, v.gmNoteVolume)] * midiVolume) >> 9;
        const auto car = 0x3f - full;
        if (car == v.gmOp[1].tl) return; // el driver solo escribe si cambia
        v.gmOp[1].tl = car;
        if (v.gmAdditive && (d.mod.level & 0x3f) != 0x3f)
            v.gmOp[0].tl = std::max ((int) (d.mod.level & 0x3f), car);
    }

    // Frecuencia de la voz (FrequencyForVoice / UpdateVoiceFrequency del driver)
    void gmUpdateFrequency (Voice& v)
    {
        if (v.gmInstr == nullptr || v.gmChannel < 0) return;
        const auto& d = v.gmInstr->voice[v.gmInstrVoice];
        auto note = v.note;
        if ((v.gmInstr->flags & 0x01) == 0) note += d.noteOffset;
        while (note < 0) note += 12;
        while (note > 95) note -= 12;
        auto index = 64 + 32 * note + gmChannels[(size_t) v.gmChannel].bend;
        if (v.gmInstrVoice != 0) index += v.gmInstr->fineTune / 2 - 64;
        index = std::max (0, index);
        int freq;
        if (index < 284) freq = gmFrequencyCurve (index);
        else freq = gmFrequencyCurve (284 + (index - 284) % 384) | (std::min (7, (index - 284) / 384) << 10);
        if (freq == v.gmFreq) return;
        v.gmFreq = freq;
        v.fnum = freq & 0x3ff;
        v.block = (freq >> 10) & 7;
        v.ksv = (v.block << 1) | ((v.fnum >> 9) & 1); // NTS = 0
        static const int kslrom[16] = { 0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64 };
        v.kslBase = std::max (0, (kslrom[v.fnum >> 6] << 2) - ((8 - v.block) << 5));
    }

    // frequency_curve del driver (Chocolate Doom): 668 valores de F-number en 1/32 de semitono; desde el 284 se repite por
    // octavas. El último es un desbordamiento del original (0x36c) y se conserva
    static int gmFrequencyCurve (int index)
    {
        static const std::uint16_t curve[668] = {
            0x133, 0x133, 0x134, 0x134, 0x135, 0x136, 0x136, 0x137, 0x137, 0x138, 0x138, 0x139, 0x139, 0x13a, 0x13b, 0x13b,
            0x13c, 0x13c, 0x13d, 0x13d, 0x13e, 0x13f, 0x13f, 0x140, 0x140, 0x141, 0x142, 0x142, 0x143, 0x143, 0x144, 0x144,
            0x145, 0x146, 0x146, 0x147, 0x147, 0x148, 0x149, 0x149, 0x14a, 0x14a, 0x14b, 0x14c, 0x14c, 0x14d, 0x14d, 0x14e,
            0x14f, 0x14f, 0x150, 0x150, 0x151, 0x152, 0x152, 0x153, 0x153, 0x154, 0x155, 0x155, 0x156, 0x157, 0x157, 0x158,
            0x158, 0x159, 0x15a, 0x15a, 0x15b, 0x15b, 0x15c, 0x15d, 0x15d, 0x15e, 0x15f, 0x15f, 0x160, 0x161, 0x161, 0x162,
            0x162, 0x163, 0x164, 0x164, 0x165, 0x166, 0x166, 0x167, 0x168, 0x168, 0x169, 0x16a, 0x16a, 0x16b, 0x16c, 0x16c,
            0x16d, 0x16e, 0x16e, 0x16f, 0x170, 0x170, 0x171, 0x172, 0x172, 0x173, 0x174, 0x174, 0x175, 0x176, 0x176, 0x177,
            0x178, 0x178, 0x179, 0x17a, 0x17a, 0x17b, 0x17c, 0x17c, 0x17d, 0x17e, 0x17e, 0x17f, 0x180, 0x181, 0x181, 0x182,
            0x183, 0x183, 0x184, 0x185, 0x185, 0x186, 0x187, 0x188, 0x188, 0x189, 0x18a, 0x18a, 0x18b, 0x18c, 0x18d, 0x18d,
            0x18e, 0x18f, 0x18f, 0x190, 0x191, 0x192, 0x192, 0x193, 0x194, 0x194, 0x195, 0x196, 0x197, 0x197, 0x198, 0x199,
            0x19a, 0x19a, 0x19b, 0x19c, 0x19d, 0x19d, 0x19e, 0x19f, 0x1a0, 0x1a0, 0x1a1, 0x1a2, 0x1a3, 0x1a3, 0x1a4, 0x1a5,
            0x1a6, 0x1a6, 0x1a7, 0x1a8, 0x1a9, 0x1a9, 0x1aa, 0x1ab, 0x1ac, 0x1ad, 0x1ad, 0x1ae, 0x1af, 0x1b0, 0x1b0, 0x1b1,
            0x1b2, 0x1b3, 0x1b4, 0x1b4, 0x1b5, 0x1b6, 0x1b7, 0x1b8, 0x1b8, 0x1b9, 0x1ba, 0x1bb, 0x1bc, 0x1bc, 0x1bd, 0x1be,
            0x1bf, 0x1c0, 0x1c0, 0x1c1, 0x1c2, 0x1c3, 0x1c4, 0x1c4, 0x1c5, 0x1c6, 0x1c7, 0x1c8, 0x1c9, 0x1c9, 0x1ca, 0x1cb,
            0x1cc, 0x1cd, 0x1ce, 0x1ce, 0x1cf, 0x1d0, 0x1d1, 0x1d2, 0x1d3, 0x1d3, 0x1d4, 0x1d5, 0x1d6, 0x1d7, 0x1d8, 0x1d8,
            0x1d9, 0x1da, 0x1db, 0x1dc, 0x1dd, 0x1de, 0x1de, 0x1df, 0x1e0, 0x1e1, 0x1e2, 0x1e3, 0x1e4, 0x1e5, 0x1e5, 0x1e6,
            0x1e7, 0x1e8, 0x1e9, 0x1ea, 0x1eb, 0x1ec, 0x1ed, 0x1ed, 0x1ee, 0x1ef, 0x1f0, 0x1f1, 0x1f2, 0x1f3, 0x1f4, 0x1f5,
            0x1f6, 0x1f6, 0x1f7, 0x1f8, 0x1f9, 0x1fa, 0x1fb, 0x1fc, 0x1fd, 0x1fe, 0x1ff, 0x200, 0x201, 0x201, 0x202, 0x203,
            0x204, 0x205, 0x206, 0x207, 0x208, 0x209, 0x20a, 0x20b, 0x20c, 0x20d, 0x20e, 0x20f, 0x210, 0x210, 0x211, 0x212,
            0x213, 0x214, 0x215, 0x216, 0x217, 0x218, 0x219, 0x21a, 0x21b, 0x21c, 0x21d, 0x21e, 0x21f, 0x220, 0x221, 0x222,
            0x223, 0x224, 0x225, 0x226, 0x227, 0x228, 0x229, 0x22a, 0x22b, 0x22c, 0x22d, 0x22e, 0x22f, 0x230, 0x231, 0x232,
            0x233, 0x234, 0x235, 0x236, 0x237, 0x238, 0x239, 0x23a, 0x23b, 0x23c, 0x23d, 0x23e, 0x23f, 0x240, 0x241, 0x242,
            0x244, 0x245, 0x246, 0x247, 0x248, 0x249, 0x24a, 0x24b, 0x24c, 0x24d, 0x24e, 0x24f, 0x250, 0x251, 0x252, 0x253,
            0x254, 0x256, 0x257, 0x258, 0x259, 0x25a, 0x25b, 0x25c, 0x25d, 0x25e, 0x25f, 0x260, 0x262, 0x263, 0x264, 0x265,
            0x266, 0x267, 0x268, 0x269, 0x26a, 0x26c, 0x26d, 0x26e, 0x26f, 0x270, 0x271, 0x272, 0x273, 0x275, 0x276, 0x277,
            0x278, 0x279, 0x27a, 0x27b, 0x27d, 0x27e, 0x27f, 0x280, 0x281, 0x282, 0x284, 0x285, 0x286, 0x287, 0x288, 0x289,
            0x28b, 0x28c, 0x28d, 0x28e, 0x28f, 0x290, 0x292, 0x293, 0x294, 0x295, 0x296, 0x298, 0x299, 0x29a, 0x29b, 0x29c,
            0x29e, 0x29f, 0x2a0, 0x2a1, 0x2a2, 0x2a4, 0x2a5, 0x2a6, 0x2a7, 0x2a9, 0x2aa, 0x2ab, 0x2ac, 0x2ae, 0x2af, 0x2b0,
            0x2b1, 0x2b2, 0x2b4, 0x2b5, 0x2b6, 0x2b7, 0x2b9, 0x2ba, 0x2bb, 0x2bd, 0x2be, 0x2bf, 0x2c0, 0x2c2, 0x2c3, 0x2c4,
            0x2c5, 0x2c7, 0x2c8, 0x2c9, 0x2cb, 0x2cc, 0x2cd, 0x2ce, 0x2d0, 0x2d1, 0x2d2, 0x2d4, 0x2d5, 0x2d6, 0x2d8, 0x2d9,
            0x2da, 0x2dc, 0x2dd, 0x2de, 0x2e0, 0x2e1, 0x2e2, 0x2e4, 0x2e5, 0x2e6, 0x2e8, 0x2e9, 0x2ea, 0x2ec, 0x2ed, 0x2ee,
            0x2f0, 0x2f1, 0x2f2, 0x2f4, 0x2f5, 0x2f6, 0x2f8, 0x2f9, 0x2fb, 0x2fc, 0x2fd, 0x2ff, 0x300, 0x302, 0x303, 0x304,
            0x306, 0x307, 0x309, 0x30a, 0x30b, 0x30d, 0x30e, 0x310, 0x311, 0x312, 0x314, 0x315, 0x317, 0x318, 0x31a, 0x31b,
            0x31c, 0x31e, 0x31f, 0x321, 0x322, 0x324, 0x325, 0x327, 0x328, 0x329, 0x32b, 0x32c, 0x32e, 0x32f, 0x331, 0x332,
            0x334, 0x335, 0x337, 0x338, 0x33a, 0x33b, 0x33d, 0x33e, 0x340, 0x341, 0x343, 0x344, 0x346, 0x347, 0x349, 0x34a,
            0x34c, 0x34d, 0x34f, 0x350, 0x352, 0x353, 0x355, 0x357, 0x358, 0x35a, 0x35b, 0x35d, 0x35e, 0x360, 0x361, 0x363,
            0x365, 0x366, 0x368, 0x369, 0x36b, 0x36c, 0x36e, 0x370, 0x371, 0x373, 0x374, 0x376, 0x378, 0x379, 0x37b, 0x37c,
            0x37e, 0x380, 0x381, 0x383, 0x384, 0x386, 0x388, 0x389, 0x38b, 0x38d, 0x38e, 0x390, 0x392, 0x393, 0x395, 0x397,
            0x398, 0x39a, 0x39c, 0x39d, 0x39f, 0x3a1, 0x3a2, 0x3a4, 0x3a6, 0x3a7, 0x3a9, 0x3ab, 0x3ac, 0x3ae, 0x3b0, 0x3b1,
            0x3b3, 0x3b5, 0x3b7, 0x3b8, 0x3ba, 0x3bc, 0x3bd, 0x3bf, 0x3c1, 0x3c3, 0x3c4, 0x3c6, 0x3c8, 0x3ca, 0x3cb, 0x3cd,
            0x3cf, 0x3d1, 0x3d2, 0x3d4, 0x3d6, 0x3d8, 0x3da, 0x3db, 0x3dd, 0x3df, 0x3e1, 0x3e3, 0x3e4, 0x3e6, 0x3e8, 0x3ea,
            0x3ec, 0x3ed, 0x3ef, 0x3f1, 0x3f3, 0x3f5, 0x3f6, 0x3f8, 0x3fa, 0x3fc, 0x3fe, 0x36c };
        return curve[juce::jlimit (0, 667, index)];
    }

    std::array<GmChannel, 16> gmChannels {};
    std::array<int, 18> gmFree {}, gmAlloc {};
    int gmFreeCount = 18, gmAllocCount = 0;

    static constexpr float kGain = 2.0f / 32768.0f; // un canal a fondo de escala (±4084) ≈ ±0,25

    double sampleRate = 44100.0;
    SincResampler resampler;
    OplGold gold;
    bool goldWasOn = false;
    OpllEngine opll;
    std::array<Voice, 18> voices {};
    std::array<OpRegs, 4> ops {};
    int model = 0, alg = 0, feedback = 0, tremoloShift = 4, vibShift = 1, panMode = 0;
    bool fourOp = false, velocityOn = true, monoOutput = false, pgReset = false, gm = false;
    uint32_t timer = 0;
    uint64_t egTimer = 0;
    int egState = 0, egAdd = 0, egTimerLo = 0, tremoloPos = 0, tremolo = 0, vibPos = 0;
    double tuneCents = 0.0;
    float bendSmoothed = 0.0f;
    long long age = 0;
};
