// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Motor SNES: S-DSP (Sony SPC700 + DSP) a 32 000 Hz, 8 voces de samples BRR. Todo según fullsnes (problemkaputt.de, "SNES APU
// DSP ..."):
//  - BRR: bloques de 9 bytes (cabecera shift/filtro/loop + 16 nibbles); filtros 1-3 con sus fórmulas exactas; recorte a 16
//    bits y a 15 bits con pérdida de signo, como el chip.
//  - Tono de 14 bits (0x1000 = 1:1 a 32 kHz); interpolación gaussiana de 4 puntos con la tabla de 512 entradas del chip
//    (sumas 0x7FF..0x801) y sus desbordamientos (2.ª-3.ª suma con vuelta a 16 bits, 4.ª saturada).
//  - Envolvente de 11 bits: ADSR (ataque +32 o +1024, decay/sustain exponencial -(((L-1) SAR 8)+1), release -8 por muestra)
//    o GAIN (directo, lineal, exponencial, curvo) con la tabla de 32 tasas; 5 muestras de retardo tras el key-on.
//  - Ruido LFSR de 15 bits (sin interpolación) a una de las 32 tasas.
//  - Mezclador: muestra·vol SAR 6 con saturación a 16 bits tras cada suma; volumen maestro; eco con búfer de EDL·16 ms
//    (EDL 0 = 1 muestra), FIR de 8 taps (entrada SAR 1, productos SAR 6, 7 sumas con vuelta y la última saturada), EFB y
//    EVOL; entrada del eco AND 0xFFFE; salida XOR 0xFFFF (inversión de fase del post-amplificador).
// Salida analógica opcional: modelo de blargg (libgme Spc_Filter): FIR (x + 3·x[-1])/4 (-6 dB a 16 kHz) y paso alto con
// fuga 1 - 1/256 (19,9 Hz); no es una medida del hardware. Remuestreo a la tasa del host con SincResampler (0,25 ms).
// Fuentes: 6 samples propios de KoniSynth (sintetizados y codificados en BRR al arrancar) o el sample del usuario (panel
// Sample), recodificado en BRR a 32/16/8 kHz con un límite de 64 KB, como la RAM de audio de la consola.
struct SnesEngine
{
    static constexpr double kRate = 32000.0;
    static constexpr int kNumSources = 6;

    void prepare (double sr, int maxBlock)
    {
        resampler.prepare (kRate, sr);
        zoh.prepare (kRate, sr);
        (void) maxBlock;
        if (sources[0].brr.empty())
            buildSources();
        reset();
    }

    void reset()
    {
        for (auto& v : voice) v = {};
        for (auto& e : echoRam) e.assign (15 * 512, 0);
        echoPos = 0;
        echoLength = 1;
        for (auto& f : firBuf) f.fill (0);
        firPos = 0;
        noise = 0x4000; // -4000h en 15 bits (fullsnes: valor tras el reset)
        counter = 0;
        lpPrev = hpIn = hpOut = { 0.0, 0.0 };
        age = 0;
        resampler.reset();
        zoh.reset();
    }

    // PANIC, tras reset: 64 muestras del chip (2 ms a 32 kHz) de silencio en las que la etapa de salida (paso bajo y paso alto)
    // sigue a la entrada; después arranca en equilibrio y no deja un escalón de continua (como en Mega Drive)
    void settleOutput() { settle = 64; }

    // Macros por frame (Dsp/FrameMacro.h, propiedad "snesMacros"), EXPERIMENTAL: el driver N-SPC avanza por ticks de su timer
    // según el tempo, no por frames; aquí van a 60,10 Hz como en un driver de efectos. Volumen: escala VxVOL (macro/15);
    // arpegio; pitch: registro de tono, 8 unidades por paso (+ = más grave); "duty" = NON, ruido en esa voz (bit 0).
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, const SampleData* userSample, int rootNote,
                 const MacroSet* macroSet)
    {
        static const MacroSet none;
        macros = macroSet != nullptr ? macroSet : &none;
        readParams (p);
        prepareUserSource (userSample, p);
        userRootNote = rootNote;
        bendSmoothed += 0.2f * (p.pitchBend.load() - bendSmoothed);
        tuneCents = (double) p.fineTune->load() + 100.0 * bendSmoothed;
        for (auto& v : voice)
            if (v.note >= 0)
                v.pitch = macroPitch (v);

        const auto smooth = (int) p.snesDac->load() == 1;
        if (smooth != wasSmooth) { wasSmooth = smooth; resampler.reset(); zoh.reset(); } // el que entra empieza limpio
        auto midiIt = midi.cbegin();
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
                handleMidi ((*midiIt).getMessage());
            auto y = smooth ? resampler.nextStereo ([this] { return dspSample(); }) : zoh.nextStereo ([this] { return dspSample(); });
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

private:
    static int gauss (int i)
    {
        static const short table[512] = {
        0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
        0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x002, 0x002, 0x002, 0x002, 0x002,
        0x002, 0x002, 0x003, 0x003, 0x003, 0x003, 0x003, 0x004, 0x004, 0x004, 0x004, 0x004, 0x005, 0x005, 0x005, 0x005,
        0x006, 0x006, 0x006, 0x006, 0x007, 0x007, 0x007, 0x008, 0x008, 0x008, 0x009, 0x009, 0x009, 0x00A, 0x00A, 0x00A,
        0x00B, 0x00B, 0x00B, 0x00C, 0x00C, 0x00D, 0x00D, 0x00E, 0x00E, 0x00F, 0x00F, 0x00F, 0x010, 0x010, 0x011, 0x011,
        0x012, 0x013, 0x013, 0x014, 0x014, 0x015, 0x015, 0x016, 0x017, 0x017, 0x018, 0x018, 0x019, 0x01A, 0x01B, 0x01B,
        0x01C, 0x01D, 0x01D, 0x01E, 0x01F, 0x020, 0x020, 0x021, 0x022, 0x023, 0x024, 0x024, 0x025, 0x026, 0x027, 0x028,
        0x029, 0x02A, 0x02B, 0x02C, 0x02D, 0x02E, 0x02F, 0x030, 0x031, 0x032, 0x033, 0x034, 0x035, 0x036, 0x037, 0x038,
        0x03A, 0x03B, 0x03C, 0x03D, 0x03E, 0x040, 0x041, 0x042, 0x043, 0x045, 0x046, 0x047, 0x049, 0x04A, 0x04C, 0x04D,
        0x04E, 0x050, 0x051, 0x053, 0x054, 0x056, 0x057, 0x059, 0x05A, 0x05C, 0x05E, 0x05F, 0x061, 0x063, 0x064, 0x066,
        0x068, 0x06A, 0x06B, 0x06D, 0x06F, 0x071, 0x073, 0x075, 0x076, 0x078, 0x07A, 0x07C, 0x07E, 0x080, 0x082, 0x084,
        0x086, 0x089, 0x08B, 0x08D, 0x08F, 0x091, 0x093, 0x096, 0x098, 0x09A, 0x09C, 0x09F, 0x0A1, 0x0A3, 0x0A6, 0x0A8,
        0x0AB, 0x0AD, 0x0AF, 0x0B2, 0x0B4, 0x0B7, 0x0BA, 0x0BC, 0x0BF, 0x0C1, 0x0C4, 0x0C7, 0x0C9, 0x0CC, 0x0CF, 0x0D2,
        0x0D4, 0x0D7, 0x0DA, 0x0DD, 0x0E0, 0x0E3, 0x0E6, 0x0E9, 0x0EC, 0x0EF, 0x0F2, 0x0F5, 0x0F8, 0x0FB, 0x0FE, 0x101,
        0x104, 0x107, 0x10B, 0x10E, 0x111, 0x114, 0x118, 0x11B, 0x11E, 0x122, 0x125, 0x129, 0x12C, 0x130, 0x133, 0x137,
        0x13A, 0x13E, 0x141, 0x145, 0x148, 0x14C, 0x150, 0x153, 0x157, 0x15B, 0x15F, 0x162, 0x166, 0x16A, 0x16E, 0x172,
        0x176, 0x17A, 0x17D, 0x181, 0x185, 0x189, 0x18D, 0x191, 0x195, 0x19A, 0x19E, 0x1A2, 0x1A6, 0x1AA, 0x1AE, 0x1B2,
        0x1B7, 0x1BB, 0x1BF, 0x1C3, 0x1C8, 0x1CC, 0x1D0, 0x1D5, 0x1D9, 0x1DD, 0x1E2, 0x1E6, 0x1EB, 0x1EF, 0x1F3, 0x1F8,
        0x1FC, 0x201, 0x205, 0x20A, 0x20F, 0x213, 0x218, 0x21C, 0x221, 0x226, 0x22A, 0x22F, 0x233, 0x238, 0x23D, 0x241,
        0x246, 0x24B, 0x250, 0x254, 0x259, 0x25E, 0x263, 0x267, 0x26C, 0x271, 0x276, 0x27B, 0x280, 0x284, 0x289, 0x28E,
        0x293, 0x298, 0x29D, 0x2A2, 0x2A6, 0x2AB, 0x2B0, 0x2B5, 0x2BA, 0x2BF, 0x2C4, 0x2C9, 0x2CE, 0x2D3, 0x2D8, 0x2DC,
        0x2E1, 0x2E6, 0x2EB, 0x2F0, 0x2F5, 0x2FA, 0x2FF, 0x304, 0x309, 0x30E, 0x313, 0x318, 0x31D, 0x322, 0x326, 0x32B,
        0x330, 0x335, 0x33A, 0x33F, 0x344, 0x349, 0x34E, 0x353, 0x357, 0x35C, 0x361, 0x366, 0x36B, 0x370, 0x374, 0x379,
        0x37E, 0x383, 0x388, 0x38C, 0x391, 0x396, 0x39B, 0x39F, 0x3A4, 0x3A9, 0x3AD, 0x3B2, 0x3B7, 0x3BB, 0x3C0, 0x3C5,
        0x3C9, 0x3CE, 0x3D2, 0x3D7, 0x3DC, 0x3E0, 0x3E5, 0x3E9, 0x3ED, 0x3F2, 0x3F6, 0x3FB, 0x3FF, 0x403, 0x408, 0x40C,
        0x410, 0x415, 0x419, 0x41D, 0x421, 0x425, 0x42A, 0x42E, 0x432, 0x436, 0x43A, 0x43E, 0x442, 0x446, 0x44A, 0x44E,
        0x452, 0x455, 0x459, 0x45D, 0x461, 0x465, 0x468, 0x46C, 0x470, 0x473, 0x477, 0x47A, 0x47E, 0x481, 0x485, 0x488,
        0x48C, 0x48F, 0x492, 0x496, 0x499, 0x49C, 0x49F, 0x4A2, 0x4A6, 0x4A9, 0x4AC, 0x4AF, 0x4B2, 0x4B5, 0x4B7, 0x4BA,
        0x4BD, 0x4C0, 0x4C3, 0x4C5, 0x4C8, 0x4CB, 0x4CD, 0x4D0, 0x4D2, 0x4D5, 0x4D7, 0x4D9, 0x4DC, 0x4DE, 0x4E0, 0x4E3,
        0x4E5, 0x4E7, 0x4E9, 0x4EB, 0x4ED, 0x4EF, 0x4F1, 0x4F3, 0x4F5, 0x4F6, 0x4F8, 0x4FA, 0x4FB, 0x4FD, 0x4FF, 0x500,
        0x502, 0x503, 0x504, 0x506, 0x507, 0x508, 0x50A, 0x50B, 0x50C, 0x50D, 0x50E, 0x50F, 0x510, 0x511, 0x511, 0x512,
        0x513, 0x514, 0x514, 0x515, 0x516, 0x516, 0x517, 0x517, 0x517, 0x518, 0x518, 0x518, 0x518, 0x518, 0x519, 0x519,
        };
        return table[i & 511];
    }

    static int clamp16 (int v) { return juce::jlimit (-32768, 32767, v); }

    enum EnvState { attack, decay, sustain, release };

    struct Source
    {
        std::vector<uint8_t> brr; // bloques de 9 bytes
        int loopBlock = 0;        // -1 = sin bucle (el último bloque lleva End+Mute)
        double rootHz = 1000.0;   // frecuencia que suena con tono 0x1000 (1:1 a 32 kHz)
    };

    struct Voice
    {
        int source = 0, block = 0, nibble = 16;
        bool decoding = false;
        int old = 0, older = 0;
        std::array<int, 4> hist {}; // las 4 muestras decodificadas más recientes: [0] la más antigua
        int frac = 0;               // fracción del contador de tono (12 bits)
        int env = 0, envState = release, konDelay = 0;
        int pitch = 0, note = -1;
        std::array<MacroCursor, MacroSet::numKinds> mc {}; // macros por frame
        int pitchAcc = 0, macroVolume = 15, macroNoise = -1;
        std::array<int, 2> volume {}; // VxVOLL, VxVOLR
        bool gate = false;
        long long age = 0;
    };

    void readParams (const Params& p)
    {
        sourceIndex = juce::jlimit (0, kNumSources, (int) p.snesSource->load());
        interp = juce::jlimit (0, 2, (int) p.snesInterp->load());
        envMode = juce::jlimit (0, 5, (int) p.snesEnvMode->load());
        attackRate = (int) p.snesAttack->load(); decayRate = (int) p.snesDecay->load();
        sustainLevel = (int) p.snesSustainLevel->load(); sustainRate = (int) p.snesSustainRate->load();
        gainRate = (int) p.snesGainRate->load(); directGain = (int) p.snesDirectGain->load();
        noiseOn = p.snesNoise->load() > 0.5f; noiseRate = (int) p.snesNoiseRate->load();
        baseVolume = (int) p.snesVolume->load(); mainVolume = (int) p.snesMainVolume->load();
        velocityOn = p.snesVelocity->load() > 0.5f;
        pan = p.snesPan->load();
        spread = p.snesSpread->load();
        monoOutput = p.snesOutput->load() > 0.5f;
        echoOn = p.snesEcho->load() > 0.5f; edl = (int) p.snesEdl->load();
        efb = (int) p.snesEfb->load(); evol = (int) p.snesEvol->load();
        firPreset = juce::jlimit (0, 1, (int) p.snesFir->load());
        filterOn = p.snesFilter->load() > 0.5f;
    }

    //--------------------------------------------------------------------------------------------------------------------------
    // BRR: decodificación exacta de una muestra (fullsnes "BRR Samples") y codificador por búsqueda (filtro 0-3 × shift 0-12
    // con el mínimo error cuadrático, simulando el decodificador)
    static int brrDecode (int nib, int shift, int filter, int old, int older) { return Brr::decodeSample (nib, shift, filter, old, older); }

    static Source encodeBrr (const std::vector<int>& pcm, bool loop, double rootHz)
    {
        Source src;
        src.rootHz = rootHz;
        src.loopBlock = loop ? 0 : -1;
        const auto numBlocks = std::max<size_t> (1, (pcm.size() + 15) / 16);
        int old = 0, older = 0;
        for (size_t b = 0; b < numBlocks; ++b)
        {
            int target[16];
            for (int k = 0; k < 16; ++k)
                target[k] = b * 16 + (size_t) k < pcm.size() ? pcm[b * 16 + (size_t) k] : 0;
            double bestErr = std::numeric_limits<double>::max();
            int bestShift = 0, bestFilter = 0;
            uint8_t bestNibs[16] {};
            int bestOld = 0, bestOlder = 0;
            const auto maxFilter = b == 0 ? 0 : 3; // el primer bloque (y el del bucle) usa filtro 0 (fullsnes "BRR Notes")
            for (int filter = 0; filter <= maxFilter; ++filter)
                for (int shift = 0; shift <= 12; ++shift)
                {
                    int o = old, oo = older;
                    double err = 0.0;
                    uint8_t nibs[16];
                    for (int k = 0; k < 16 && err < bestErr; ++k)
                    {
                        const auto pred = brrDecode (0, shift, filter, o, oo);
                        const auto step = shift == 0 ? 0.5 : (double) (1 << (shift - 1));
                        auto nib = juce::jlimit (-8, 7, (int) std::lround ((target[k] - pred) / step));
                        // Prueba el vecino por si el redondeo o el recorte dan menos error
                        auto bestN = nib;
                        auto bestV = brrDecode (nib, shift, filter, o, oo);
                        for (const auto cand : { nib - 1, nib + 1 })
                            if (cand >= -8 && cand <= 7)
                                if (const auto v = brrDecode (cand, shift, filter, o, oo); std::abs (v - target[k]) < std::abs (bestV - target[k]))
                                {
                                    bestN = cand;
                                    bestV = v;
                                }
                        nibs[k] = (uint8_t) (bestN & 15);
                        err += (double) (bestV - target[k]) * (bestV - target[k]);
                        oo = o;
                        o = bestV;
                    }
                    if (err < bestErr)
                    {
                        bestErr = err;
                        bestShift = shift;
                        bestFilter = filter;
                        std::copy (nibs, nibs + 16, bestNibs);
                        bestOld = o;
                        bestOlder = oo;
                    }
                }
            old = bestOld;
            older = bestOlder;
            const auto last = b + 1 == numBlocks;
            src.brr.push_back ((uint8_t) ((bestShift << 4) | (bestFilter << 2) | (last ? (loop ? 3 : 1) : 0)));
            for (int k = 0; k < 16; k += 2)
                src.brr.push_back ((uint8_t) ((bestNibs[k] << 4) | bestNibs[k + 1]));
        }
        return src;
    }

    // Samples propios (Dsp/WaveSources.h) a 32 kHz, codificados en BRR
    void buildSources()
    {
        for (int k = 0; k < kNumSources; ++k)
        {
            const auto w = makeWaveSource (k, kRate);
            sources[(size_t) k] = encodeBrr (w.pcm, true, w.rootHz);
        }
    }

    // Sample del usuario: región del panel Sample (Start, Length, Loop), remuestreada a 32/16/8 kHz y codificada en BRR con un
    // máximo de 64 KB (7281 bloques). Solo se recodifica si cambia algo. Raíz: la nota Root del panel a esa tasa.
    void prepareUserSource (const SampleData* sample, const Params& p)
    {
        const auto rateIndex = juce::jlimit (0, 2, (int) p.snesUserRate->load());
        const auto start = p.sampleStart->load(), lengthMs = p.sampleLength->load();
        const auto loop = p.sampleLoop->load() > 0.5f;
        const auto length = sample != nullptr ? sample->length() : 0;
        if (sample == userSource && length == userSourceLength && rateIndex == userRateIndex && start == userStart
            && lengthMs == userLengthMs && loop == userLoop)
            return;
        userSource = sample; userSourceLength = length; userRateIndex = rateIndex; userStart = start; userLengthMs = lengthMs; userLoop = loop;
        sources[kNumSources] = {};
        if (sample == nullptr || length < 16)
            return;
        // BRR original (.brr, .spc): se toca tal cual, a 32 kHz, con sus propios flags de End/Loop (Start/Length/Loop no aplican)
        if (! sample->brr.empty())
        {
            userRoot = kRate;
            sources[kNumSources].brr = sample->brr;
            sources[kNumSources].loopBlock = sample->brrLoopBlock;
            return;
        }
        const auto rate = kRate / (double) (1 << rateIndex);
        const auto ratio = sample->sampleRate / rate;
        const auto level = juce::jlimit (0, SampleData::maxLevels - 1, (int) std::floor (std::log2 (std::max (1.0, ratio))));
        const auto scale = ratio / std::exp2 ((double) level);
        const auto first = (double) start * length / std::exp2 ((double) level);
        auto count = (int) ((1.0 - start) * length / ratio);
        if (lengthMs > 0.0f)
            count = std::min (count, (int) (lengthMs / 1000.0 * rate));
        count = std::min (count, 7281 * 16);
        std::vector<int> pcm ((size_t) std::max (0, count));
        for (int i = 0; i < count; ++i)
            pcm[(size_t) i] = juce::jlimit (-16000, 16000, (int) std::lround (16000.0 * sample->read (level, first + i * scale)));
        userRoot = rate;
        sources[kNumSources] = encodeBrr (pcm, loop, 0.0);
    }

    int pitchFor (int note) const
    {
        const auto& src = sources[(size_t) sourceIndex];
        // Fuente del usuario: la nota Root suena a su tasa (32/16/8 kHz) -> con tono 0x1000 suena root·32000/tasa
        const auto rootHz = sourceIndex == kNumSources ? 440.0 * std::exp2 ((userRootNote - 69) / 12.0) * kRate / userRoot : src.rootHz;
        const auto f = 440.0 * std::exp2 ((note - 69) / 12.0 + tuneCents / 1200.0) * KoniTuning::ratio (note);
        return juce::jlimit (0, 0x3fff, (int) std::lround (4096.0 * f / rootHz));
    }

    // Tono con arpegio (semitonos o @n) y pitch de las macros
    int macroPitch (const Voice& v) const
    {
        auto note = v.note;
        const auto& arp = v.mc[MacroSet::arpeggio];
        if (arp.active())
        {
            const auto& m = macros->macro[MacroSet::arpeggio];
            note = juce::jlimit (0, 127, arp.isAbsolute (m) ? arp.value (m, 0) + 12 : note + arp.value (m, 0));
        }
        return juce::jlimit (0, 0x3fff, pitchFor (note) - 8 * v.pitchAcc);
    }

    void applyMacroLevels (Voice& v) const
    {
        if (v.mc[MacroSet::volume].active()) v.macroVolume = v.mc[MacroSet::volume].value (macros->macro[MacroSet::volume], 15);
        if (v.mc[MacroSet::duty].active()) v.macroNoise = v.mc[MacroSet::duty].value (macros->macro[MacroSet::duty], 0) & 1;
    }

    // Un frame (60,10 Hz): avanza las macros de las voces que suenan
    void macroFrame()
    {
        if (macros->empty()) return;
        for (auto& v : voice)
        {
            if (v.note < 0 || (v.env == 0 && v.envState == release)) continue;
            for (size_t m = 0; m < MacroSet::numKinds; ++m)
                v.mc[m].advance (macros->macro[m]);
            if (v.mc[MacroSet::pitch].active())
                v.pitchAcc = juce::jlimit (-2047, 2047, v.pitchAcc + v.mc[MacroSet::pitch].value (macros->macro[MacroSet::pitch], 0));
            applyMacroLevels (v);
            v.pitch = macroPitch (v);
        }
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            int target = -1;
            for (int k = 0; k < 8; ++k)
                if (voice[(size_t) k].note == m.getNoteNumber()) target = k;
            if (target < 0)
            {
                long long best = std::numeric_limits<long long>::max();
                for (int pass = 0; pass < 2 && target < 0; ++pass)
                    for (int k = 0; k < 8; ++k)
                        if ((pass == 1 || ! voice[(size_t) k].gate) && voice[(size_t) k].age < best)
                        {
                            best = voice[(size_t) k].age;
                            target = k;
                        }
            }
            auto& v = voice[(size_t) target];
            // Key-on (fullsnes "KON"): envolvente a 0, ataque, el sample desde el principio; 5 muestras vacías antes de empezar
            v = Voice {};
            v.source = sourceIndex;
            v.note = m.getNoteNumber();
            v.gate = true;
            v.age = ++age;
            v.envState = attack;
            v.konDelay = 5;
            v.nibble = 99;
            v.decoding = ! sources[(size_t) v.source].brr.empty();
            for (size_t k = 0; k < MacroSet::numKinds; ++k)
                v.mc[k].start (macros->macro[k]);
            v.pitchAcc = v.mc[MacroSet::pitch].value (macros->macro[MacroSet::pitch], 0);
            applyMacroLevels (v);
            v.pitch = macroPitch (v);
            // Velocidad y paneo (driver de KoniSynth): VxVOLL/VxVOLR = volumen · velocidad · ley de balance. Spread separa las
            // voces sucesivas a un lado y a otro (como un driver que reparte los canales)
            const auto vol = velocityOn ? baseVolume * m.getFloatVelocity() : (float) baseVolume;
            const auto voicePan = juce::jlimit (-1.0f, 1.0f, pan + spread * ((age & 1) ? 1.0f : -1.0f));
            v.volume = { (int) std::lround (vol * std::min (1.0f, 1.0f - voicePan)), (int) std::lround (vol * std::min (1.0f, 1.0f + voicePan)) };
        }
        else if (m.isNoteOff())
        {
            for (auto& v : voice)
                if (v.note == m.getNoteNumber() && v.gate)
                {
                    v.gate = false;
                    v.envState = release; // KOFF
                    for (size_t k = 0; k < MacroSet::numKinds; ++k)
                        v.mc[k].noteOff (macros->macro[k]);
                }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voice)
            {
                v.gate = false;
                v.envState = release;
                if (m.isAllSoundOff()) { v.env = 0; v.decoding = false; } // como FLG.7 (soft reset)
            }
        }
    }

    bool fires (int rate) const
    {
        static const int period[32] = { 0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
                                        64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1 };
        return rate > 0 && rate < 32 && counter % (unsigned) period[rate] == 0;
    }

    // Siguiente muestra BRR de la voz (al terminar un bloque aplica sus flags: End+Loop salta al bucle; End+Mute, además,
    // pasa a release con envolvente 0)
    void decodeNext (Voice& v)
    {
        const auto& src = sources[(size_t) v.source];
        int s = 0;
        if (v.decoding)
        {
            if (v.nibble >= 16)
            {
                if (v.nibble != 99) // 99: aún no se ha leído el primer bloque
                {
                    const auto flags = src.brr[(size_t) v.block * 9] & 3;
                    if (flags & 1)
                    {
                        if (flags == 1) { v.envState = release; v.env = 0; }
                        if (src.loopBlock < 0) { v.decoding = false; }
                        else v.block = src.loopBlock;
                    }
                    else
                    {
                        ++v.block;
                    }
                }
                v.nibble = 0;
            }
            if (v.decoding && (size_t) v.block * 9 + 8 < src.brr.size())
            {
                const auto header = src.brr[(size_t) v.block * 9];
                const auto byte = src.brr[(size_t) v.block * 9 + 1 + (size_t) (v.nibble >> 1)];
                const auto raw = (v.nibble & 1) ? (byte & 15) : (byte >> 4);
                const auto nib = raw >= 8 ? raw - 16 : raw;
                s = brrDecode (nib, header >> 4, (header >> 2) & 3, v.old, v.older);
                v.older = v.old;
                v.old = s;
                ++v.nibble;
            }
        }
        v.hist = { v.hist[1], v.hist[2], v.hist[3], s };
    }

    int interpolate (const Voice& v) const
    {
        const auto i = (v.frac >> 4) & 0xff;
        if (interp == 0) // gaussiana del chip
        {
            auto out = (gauss (0xff - i) * v.hist[0]) >> 10;
            out += (gauss (0x1ff - i) * v.hist[1]) >> 10;
            out += (gauss (0x100 + i) * v.hist[2]) >> 10;
            out = (int) (int16_t) (uint16_t) out; // 2.ª y 3.ª suma sin control de desbordamiento (vuelta a 16 bits)
            out += (gauss (i) * v.hist[3]) >> 10;
            return clamp16 (out) >> 1;
        }
        if (interp == 1) // lineal (comparación; no es del chip)
            return v.hist[1] + (((v.hist[2] - v.hist[1]) * i) >> 8);
        return v.hist[1]; // sin interpolación (comparación)
    }

    void stepEnvelope (Voice& v)
    {
        if (v.envState == release)
        {
            v.env = std::max (0, v.env - 8);
            return;
        }
        const auto expStep = [&v] { return -(((v.env - 1) >> 8) + 1); };
        if (envMode == 0) // ADSR
        {
            if (v.envState == attack)
            {
                const auto rate = attackRate * 2 + 1;
                if (fires (rate))
                    v.env += rate == 31 ? 1024 : 32;
                if (v.env >= 0x7e0)
                {
                    v.envState = decay;
                    v.env = std::min (v.env, 0x7ff);
                }
            }
            else if (v.envState == decay)
            {
                if (fires (decayRate * 2 + 16))
                    v.env += expStep();
                if (v.env <= (sustainLevel + 1) * 0x100)
                    v.envState = sustain;
            }
            else if (fires (sustainRate))
            {
                v.env += expStep();
            }
        }
        else if (envMode == 1) // GAIN directo
        {
            v.env = directGain * 16;
        }
        else if (fires (gainRate)) // GAIN: lineal -32, exponencial, lineal +32, curvo (+32 hasta 0x600, luego +8)
        {
            switch (envMode)
            {
                case 2: v.env -= 32; break;
                case 3: v.env += expStep(); break;
                case 4: v.env += 32; break;
                default: v.env += v.env < 0x600 ? 32 : 8; break;
            }
        }
        v.env = juce::jlimit (0, 0x7ff, v.env);
    }

    std::array<float, 2> dspSample()
    {
        ++counter;
        if ((macroClock += 1.0) >= kRate / 60.0988)
        {
            macroClock -= kRate / 60.0988;
            macroFrame();
        }
        if (fires (noiseRate))
            noise = ((noise >> 1) & 0x3fff) | (((noise ^ (noise >> 1)) & 1) << 14);
        const auto noiseValue = (noise & 0x4000) ? (noise & 0x7fff) - 0x8000 : noise; // 15 bits con signo

        std::array<int, 2> mainSum { 0, 0 }, echoSum { 0, 0 };
        for (auto& v : voice)
        {
            if (v.konDelay > 0)
            {
                --v.konDelay;
                continue;
            }
            if (v.env == 0 && v.envState == release && ! v.decoding)
                continue;
            const auto sample = (v.macroNoise >= 0 ? v.macroNoise == 1 : noiseOn) ? noiseValue : interpolate (v);
            stepEnvelope (v);
            const auto outx = (sample * v.env) >> 11;
            // Avance del contador de tono: cada vez que pasa de 0x1000 entra una muestra BRR nueva
            v.frac += v.pitch;
            while (v.frac >= 0x1000)
            {
                v.frac -= 0x1000;
                decodeNext (v);
            }
            for (size_t side = 0; side < 2; ++side)
            {
                const auto s = (outx * (v.volume[side] * v.macroVolume / 15)) >> 6;
                mainSum[side] = clamp16 (mainSum[side] + s);
                if (echoOn)
                    echoSum[side] = clamp16 (echoSum[side] + s);
            }
        }

        // Eco: búfer circular de EDL·512 muestras; FIR de 8 taps sobre lo que sale del búfer
        static const int firTaps[2][8] = { { 0x7f, 0, 0, 0, 0, 0, 0, 0 }, { -1, 0x08, 0x17, 0x24, 0x24, 0x17, 0x08, -1 } };
        const auto& fir = firTaps[firPreset];
        // Los dos canales se filtran por separado, con los mismos coeficientes y sin diafonía (fullsnes)
        std::array<int, 2> outSample {};
        for (size_t side = 0; side < 2; ++side)
        {
            auto& fb = firBuf[side];
            auto& ram = echoRam[side];
            fb[(size_t) (firPos & 7)] = ram[(size_t) echoPos] >> 1;
            int sum = 0;
            for (int k = 0; k < 7; ++k)
                sum += (fb[(size_t) ((firPos - 7 + k) & 7)] * fir[k]) >> 6;
            sum = (int) (int16_t) (uint16_t) sum;
            sum = clamp16 (sum + ((fb[(size_t) (firPos & 7)] * fir[7]) >> 6));
            outSample[side] = clamp16 (((mainSum[side] * mainVolume) >> 7) + ((sum * evol) >> 7));
            ram[(size_t) echoPos] = clamp16 (echoSum[side] + ((sum * efb) >> 7)) & ~1;
        }
        firPos = (firPos + 1) & 7;
        if (echoPos == 0)
            echoLength = edl << 9;
        if (++echoPos >= echoLength)
            echoPos = 0;

        std::array<float, 2> result {};
        for (size_t side = 0; side < 2; ++side)
        {
            auto y = (double) ~outSample[side] / 32768.0; // XOR FFFFh (inversión del post-amplificador) = ~x
            if (filterOn)
            {
                const auto lp = 0.25 * y + 0.75 * lpPrev[side];
                lpPrev[side] = y;
                hpOut[side] = settle > 0 ? 0.0 : lp - hpIn[side] + (1.0 - 1.0 / 256.0) * hpOut[side];
                hpIn[side] = lp;
                y = hpOut[side];
            }
            result[side] = (float) y;
        }
        if (settle > 0)
            --settle;
        return result;
    }

    SincResampler resampler; // DAC "Smooth"
    ZohResampler zoh;        // DAC "Hold" (por defecto)
    bool wasSmooth = false;
    std::array<Source, kNumSources + 1> sources {};
    std::array<Voice, 8> voice {};
    std::array<std::vector<int>, 2> echoRam; // izquierda, derecha (en la consola, entradas de 4 bytes L/R)
    std::array<std::array<int, 8>, 2> firBuf {};
    int echoPos = 0, echoLength = 1, firPos = 0, noise = 0x4000;
    unsigned counter = 0;
    int sourceIndex = 0, interp = 0, envMode = 0, attackRate = 15, decayRate = 7, sustainLevel = 7, sustainRate = 0;
    int gainRate = 0, directGain = 127, noiseRate = 0, baseVolume = 80, mainVolume = 127, edl = 0, efb = 0, evol = 0, firPreset = 0;
    bool noiseOn = false, velocityOn = true, echoOn = false, filterOn = true;
    std::array<double, 2> lpPrev {}, hpIn {}, hpOut {};
    int settle = 0; // muestras del chip de asentamiento de la salida que quedan (settleOutput)
    double tuneCents = 0.0, userRoot = kRate;
    float pan = 0.0f, spread = 0.0f;
    bool monoOutput = false; // salida RF: L + R
    float bendSmoothed = 0.0f;
    const MacroSet* macros = nullptr; // del bloque (nunca nulo durante render)
    double macroClock = 0.0;
    int userRootNote = 60;
    long long age = 0;
    const SampleData* userSource = nullptr;
    int userSourceLength = -1, userRateIndex = -1;
    float userStart = -1.0f, userLengthMs = -1.0f;
    bool userLoop = false;
};
