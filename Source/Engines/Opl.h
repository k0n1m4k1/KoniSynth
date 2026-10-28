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
// Modelos OPLL (YM2413) y VRC7: Engines/Opll.h (Nuked-OPLL).
struct OplEngine
{
    static constexpr double kChipRate = 14318180.0 / 288.0;
    enum Model { opl2, opl3, opllYm2413, opllVrc7 };

    void prepare (double sr, int)
    {
        sampleRate = sr;
        resampler.prepare (kChipRate, sr);
        opll.prepare (sr);
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v = {};
        timer = 0; egTimer = 0; egState = 0; egAdd = 0; egTimerLo = 0;
        tremoloPos = 0; tremolo = 0; vibPos = 0;
        age = 0;
        resampler.reset();
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
        for (auto& v : voices)
            if (v.note >= 0) setPitch (v);

        auto it = midi.cbegin();
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
                handleMidi ((*it).getMessage());
            auto lr = resampler.nextStereo ([this] { return chipSample(); });
            if (model == opl2 || monoOutput)
                lr[0] = lr[1] = 0.5f * (lr[0] + lr[1]);
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
        const auto newFour = newModel == opl3 && p.oplFourOp->load() > 0.5f;
        if (newModel != model || newFour != fourOp)
        {
            for (auto& v : voices) v = {};
            model = newModel;
            fourOp = newFour;
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

    int numVoices() const { return model == opl2 ? 9 : fourOp ? 6 : 18; }

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
            int acc = 0;
            int prevOut = 0;
            for (int j = 0; j < opCount; ++j)
            {
                auto& s = v.slot[(size_t) j];
                const auto& o = ops[(size_t) j];
                // Entrada de modulación: el primero, su realimentación; los demás, el anterior si el algoritmo lo encadena
                int mod = 0;
                if (j == 0)
                    mod = feedback ? (s.prout + s.out) >> (9 - feedback) : 0;
                else if (chains (j))
                    mod = prevOut;
                s.prout = s.out;
                envelope (s, o, v, ((carrierMask >> j) & 1) ? v.velocityTl : 0);
                phase (s, o, v);
                s.out = waveform (o.wf, s.phaseOut + mod, s.egOut);
                prevOut = s.out;
                if ((carrierMask >> j) & 1) acc += s.out;
            }
            acc = (int) (int16_t) acc;
            // Paneo (registro C0h, bits 4-5: A = izquierda, B = derecha), según el modo del driver de KoniSynth
            const auto pan = panMode == 1 ? 1 : panMode == 2 ? 2 : panMode == 3 ? 1 + (k & 1) : 0;
            const auto onL = model == opl2 || pan != 2, onR = model == opl2 || pan != 1;
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

    static constexpr float kGain = 2.0f / 32768.0f; // un canal a fondo de escala (±4084) ≈ ±0,25

    double sampleRate = 44100.0;
    SincResampler resampler;
    OpllEngine opll;
    std::array<Voice, 18> voices {};
    std::array<OpRegs, 4> ops {};
    int model = 0, alg = 0, feedback = 0, tremoloShift = 4, vibShift = 1, panMode = 0;
    bool fourOp = false, velocityOn = true, monoOutput = false, pgReset = false;
    uint32_t timer = 0;
    uint64_t egTimer = 0;
    int egState = 0, egAdd = 0, egTimerLo = 0, tremoloPos = 0, tremolo = 0, vibPos = 0;
    double tuneCents = 0.0;
    float bendSmoothed = 0.0f;
    long long age = 0;
};
