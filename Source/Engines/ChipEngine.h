// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Motor Chip: APU del NES (Ricoh 2A03 / 2A07) y de la Game Boy, emulados en ciclos de su reloj con los cambios de nivel
// pasados por BlepBuffer.
// NES (nesdev wiki: APU, APU_Pulse, APU_Triangle, APU_Noise, APU_Envelope, APU_Sweep, APU_Frame_Counter, APU_Mixer):
//  - CPU 1 789 773 Hz (NTSC) / 1 662 607 Hz (PAL); pulso f = CPU/(16·(t+1)), secuencia de 8 pasos que decrementa y se
//    reinicia al escribir $4003/$4007 (el clic de cada nota); silencio con t < 8 o periodo objetivo del sweep > $7FF;
//  - triángulo f = CPU/(32·(t+1)), 32 pasos 15..0..15, se congela al detenerse (DC); ruido LFSR de 15 bits (taps 0^1 o 0^6),
//    16 periodos NTSC/PAL; envolvente en cuartos de frame (≈240/200 Hz) y sweep en medios frames;
//  - mezclador no lineal: 95,88/(8128/(p1+p2)+100) y 159,79/(1/(t/8227 + n/12241)+100);
//  - salida NES: paso altos de 1.er orden a 90 y 440 Hz y paso bajo a 14 kHz; Famicom: paso alto a 37 Hz.
// Game Boy (Pan Docs Audio_details/Audio_Registers; gbdev wiki "Gameboy sound hardware"; SameBoy apu.c):
//  - reloj 4 194 304 Hz; pulsos f = 131072/(2048-x) (8 pasos, sin reinicio de fase al disparar); onda f = 65536/(2048-x),
//    32 muestras de 4 bits con volumen 100/50/25 %; ruido LFSR de 15/7 bits, periodo = divisor[r] << s;
//  - secuenciador de 512 Hz: sweep a 128 Hz, envolvente a 64 Hz; DAC lineal invertido (0 -> +1, 15 -> -1), apagado = 0;
//  - condensador de salida: k = 0,999958 por ciclo (≈28 Hz), en todos los modelos como SameBoy. El 0,998943 que el borrador de
//    blargg da para CGB (≈706 Hz) no tiene medidas y SameBoy no lo usa: descartado;
//  - errores originales reproducidos: corrupción de la wave RAM al redisparar el canal 3 en DMG justo antes de que lea un byte
//    (SameBoy apu.c y Gambatte channel3.cpp: byte 0-3 -> se copia en wave[0]; 4-15 -> su bloque de 4 bytes sobre wave[0..3]);
//    el canal 3 arranca 6 ciclos tarde y su primera muestra es la vieja del buffer; los pulsos no reinician la fase.
// NES: filtros 90/440 Hz + 14 kHz según nesdev (medidas de blargg y componentes); NSFPlay usa otros ("~ my NES": 58,5 Hz y
// 10,8 kHz) y Mesen2 no filtra: contradicción documentada. Driver: escribe $08 en el sweep (FamiTracker, FamiStudio) y solo
// reescribe el byte alto del periodo si cambia (de ahí el clic del vibrato al cruzar un múltiplo de 256).
// Capa de "driver" a la frecuencia de vídeo: 60,0988 Hz (NTSC, 29 780,5 ciclos), 50,0070 Hz (PAL, 33 247,5) y 59,7275 Hz
// (Game Boy, 70 224 ciclos), contrastadas con nesdev/NSFPlay y SameBoy/Pan Docs. Vibrato, arpegio y release se aplican por
// frame y se cuantizan a los registros enteros del chip (de ahí la desafinación y los saltos auténticos).
// DMC del NES (nesdev APU_DMC): registro de 8 bits que se vacía bit a bit a una de 16 tasas (ciclos de CPU), nivel de 7 bits
// con pasos de ±2 (sin salir de 0..127); entra en el mezclador no lineal con peso 1/22638 y su nivel, que se queda donde acaba
// el sample, baja el volumen de triángulo y ruido (como en el hardware). Samples de fábrica sintetizados por KoniSynth y
// codificados en DPCM (los de los juegos tienen copyright); "User Sample" codifica el fichero del panel Sample.
// VRC6 de Konami (expansión de cartucho, solo Famicom: el NES no tiene las patillas de audio de expansión sin modificar), según
// el wiki de nesdev ("VRC6 audio", espejo nesdev-wiki.nes.science) contrastado con Mesen2: reloj de la CPU (M2); 2 pulsos con
// periodo de 12 bits (f = CPU/(16(t+1)), hasta 27,3 Hz, una octava por debajo del 2A03), duty (D+1)/16 y volumen de 4 bits;
// "sierra" de acumulador de 8 bits: suma A cada 2 pasos del divisor, se pone a 0 en el 7.º (f = CPU/(14(t+1))) y saca los 5
// bits altos: con A > 42 el acumulador desborda y la onda se rompe (defecto original; aquí con "Saw Overdrive"). Al desactivar
// un pulso su secuencia se reinicia. DAC lineal de escalera externo, mezclado fuera del mezclador no lineal del 2A03: cada
// paso vale lo mismo que un paso de un pulso del APU a volumen 15 (95,88/(8128/15+100)/15 = 0,00996; nesdev, VRC6_SCALE).
// Medidas: el nivel relativo varía de -1,1 a +0,2 dB según el cartucho y la consola (dos medidas publicadas); aquí 0 dB.
struct ChipEngine
{
    // palPort: juego NTSC sin adaptar en una NES PAL (tabla de periodos NTSC con el reloj PAL)
    enum Model { nesNtsc, nesPal, famicom, gbDmg, gbCgb, palPort };
    enum Channel { pulse1, pulse2, tri, noise, numChannels }; // en Game Boy, 'tri' es el canal de onda

    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        for (auto& b : blep) b.prepare (kMaxChunk);
        (void) maxBlock;
        reset();
    }

    void reset()
    {
        for (auto& b : blep) b.reset();
        for (auto& c : ch) c = {};
        for (auto& d : drv) d = {};
        lastMix = { 0.0, 0.0 };
        frameClock = 0.0;
        driverClock = 0.0;
        hp1 = hp2 = lp = hpPrev1 = hpPrev2 = dc = { 0.0, 0.0 };
        dmc = {};
        vrcPulse = {}; vrcSaw = {}; vrcVoice = {}; nextVrcVoice = 0;
        loadedWave = loadedWaveModel = -1;
        nextVoice = 0;
        lastModel = -1;
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, const SampleData* userSample, const MacroSet* macroSet)
    {
        static const MacroSet none;
        macros = macroSet != nullptr ? macroSet : &none;
        prepareUserDpcm (userSample, p);
        const auto model = juce::jlimit (0, 5, (int) p.chipModel->load());
        if (model != lastModel)
        {
            reset();
            lastModel = model;
            setupModel (model);
        }
        readParams (p);

        auto midiIt = midi.cbegin();
        const auto numSamples = out.getNumSamples();
        float block[2][kMaxChunk];
        for (int start = 0; start < numSamples; start += kMaxChunk)
        {
            const auto n = std::min (kMaxChunk, numSamples - start);
            for (int i = 0; i < n; ++i)
            {
                for (; midiIt != midi.cend() && (*midiIt).samplePosition <= start + i; ++midiIt)
                    handleMidi ((*midiIt).getMessage(), p);
                stepSample (i, p);
            }
            blep[0].read (n, block[0]);
            blep[1].read (n, block[1]);
            for (int i = 0; i < n; ++i)
            {
                auto l = (float) outputFilter (block[0][i], 0), r = (float) outputFilter (block[1][i], 1);
                if (speakerOutput)
                    l = r = 0.5f * (l + r);
                if (out.getNumChannels() >= 2)
                {
                    out.addSample (0, start + i, l);
                    out.addSample (1, start + i, r);
                }
                else if (out.getNumChannels() == 1)
                {
                    out.addSample (0, start + i, 0.5f * (l + r));
                }
            }
        }
    }

private:
    static constexpr int kMaxChunk = 256;

    struct ChipChannel
    {
        // Registros
        int period = 0;              // t (NES) o x (GB), 11 bits
        int duty = 2, volume = 0;
        bool constantVolume = true, envLoop = false, envUp = false;
        int envPeriod = 0;
        bool running = false;        // NES triángulo: contadores > 0; resto: canal activo / DAC encendido
        bool dacOn = false;
        int sweepShift = 0, sweepPeriod = 0;
        bool sweepNegate = true, sweepEnabled = false;
        bool shortNoise = false;
        // Estado interno
        double countdown = 0.0;      // ciclos de reloj hasta el siguiente paso
        int step = 0;
        unsigned lfsr = 1;
        int envDivider = 0, envDecay = 0;
        bool envStart = false;
        int sweepDivider = 0;
        bool sweepReload = false;
        int waveSample = 0;
        int level = 0;               // salida digital actual 0..15
    };

    struct Driver // un canal del chip visto por el "driver" de música
    {
        std::array<int, 16> held {};
        int numHeld = 0, note = -1, velocity = 100;
        bool gate = false, releasing = false;
        int frames = 0, releaseFrame = 0, arpIndex = 0, arpCounter = 0;
        // Macros por frame: posición en cada una, pitch acumulado (unidades de periodo), release de macro en curso,
        // último volumen escrito en la Game Boy (el driver solo reescribe si cambia) y nivel de NR32 forzado (-1 = el del panel)
        std::array<MacroCursor, MacroSet::numKinds> mc {};
        int pitchAcc = 0, lastGbVolume = -1, waveShift = -1;
        bool macroRelease = false;
    };

    // ---- Configuración por modelo
    void setupModel (int m)
    {
        nes = m <= famicom || m == palPort;
        pal = m == nesPal || m == palPort;
        clock = nes ? (pal ? 1662607.03 : 1789772.73) : 4194304.0;
        // El juego sin adaptar escribe los periodos calculados para el reloj NTSC: todo suena ×1662607/1789773 = −127,6 ct
        // y su driver va a 50,01 Hz en vez de 60,10 Hz (16,8 % más lento). Ruido y DPCM usan las tablas PAL del chip
        periodClock = m == palPort ? 1789772.73 : clock;
        frameLength = nes ? (pal ? 33254.0 : 29830.0) : 8192.0;      // NES: frame counter de 4 pasos; GB: 512 Hz
        driverRate = nes ? (pal ? 1662607.03 / 33247.5 : 1789772.73 / 29780.5) : 4194304.0 / 70224.0;
        // Filtros de salida a la frecuencia del host
        const auto onePole = [this] (double hz) { return std::exp (-juce::MathConstants<double>::twoPi * hz / sampleRate); };
        if (m == nesNtsc || m == nesPal || m == palPort) { hpA = onePole (90.0); hpB = onePole (440.0); lpA = onePole (14000.0); useLp = true; }
        else if (m == famicom)           { hpA = onePole (37.0); hpB = 1.0; useLp = false; }
        else                             { hpA = std::pow (0.999958, 4194304.0 / sampleRate); hpB = 1.0; useLp = false; }
        for (auto& c : ch) { c.lfsr = nes ? 1u : 0x7fffu; c.countdown = 0.0; }
        // Lista de periodos de ruido de la Game Boy (divisor << shift), de mayor a menor periodo
        gbNoisePeriods.clear();
        for (int s = 0; s < 14; ++s)
            for (int r = 0; r < 8; ++r)
                gbNoisePeriods.push_back ((r == 0 ? 8 : 16 * r) << s);
        std::sort (gbNoisePeriods.begin(), gbNoisePeriods.end(), std::greater<int>());
        gbNoisePeriods.erase (std::unique (gbNoisePeriods.begin(), gbNoisePeriods.end()), gbNoisePeriods.end());
    }

    void readParams (const Params& p)
    {
        voiceMode = (int) p.chipVoice->load();
        duty = (int) p.chipDuty->load();
        baseVolume = (int) p.chipVolume->load();
        envMode = (int) p.chipEnvelope->load();
        envRate = (int) p.chipEnvRate->load();
        releaseFrames = (int) p.chipRelease->load();
        sweep = (int) p.chipSweep->load();
        sweepRate = (int) p.chipSweepRate->load();
        arpMode = (int) p.chipArp->load();
        arpSpeed = (int) p.chipArpSpeed->load();
        vibDepth = p.chipVibDepth->load();
        vibRate = p.chipVibRate->load();
        vibDelay = (int) p.chipVibDelay->load();
        shortNoiseParam = (int) p.chipNoiseMode->load() == 1;
        waveIndex = (int) p.chipWave->load();
        waveShift = (int) p.chipWaveLevel->load(); // 0 = 100 %, 1 = 50 %, 2 = 25 %
        velocityToVolume = p.chipVelocity->load() > 0.5f;
        filterOn = p.chipFilter->load() > 0.5f;
        // Game Boy: NR51 (bits 7-4 izquierda, 3-0 derecha, por canal): Center, Left, Right u Off (Pan Docs "Audio Registers").
        // El NES y la Famicom son mono en todos sus modelos: allí se ignora
        for (int c = 0; c < numChannels; ++c)
        {
            const auto pan = (int) p.chipPan[(size_t) c]->load();
            panL[(size_t) c] = pan == 0 || pan == 1;
            panR[(size_t) c] = pan == 0 || pan == 2;
        }
        speakerOutput = ! nes && p.chipOutput->load() > 0.5f;
        tuneCents = (double) p.fineTune->load();
        bendSemis = (double) p.pitchBend.load();
        vrc6Expansion = (int) p.chipExpansion->load() == 1;
        vrc6Duty = juce::jlimit (0, 7, (int) p.chipVrc6Duty->load());
        vrc6Overdrive = p.chipVrc6Overdrive->load() > 0.5f;
        dpcmSource = (int) p.chipDpcm->load();
        dpcmLoop = p.chipDpcmLoop->load() > 0.5f;
    }

    // ---- MIDI -> canales del chip
    int channelFor (const juce::MidiMessage& m, bool noteOn)
    {
        switch (voiceMode)
        {
            case 0: return pulse1;
            case 1: return tri;
            case 2: return noise;
            case 8: // Poly + VRC6: lo que no va al VRC6, como Poly
            case 3: // Poly: pulso 1, pulso 2 y triángulo/onda
            {
                if (! noteOn)
                {
                    for (int c : { pulse1, pulse2, tri })
                        for (int k = 0; k < drv[(size_t) c].numHeld; ++k)
                            if (drv[(size_t) c].held[(size_t) k] == m.getNoteNumber()) return c;
                    return -1;
                }
                for (int c : { pulse1, pulse2, tri })
                    if (drv[(size_t) c].numHeld == 0) return c;
                const auto c = nextVoice;
                nextVoice = (nextVoice + 1) % 3;
                return c;
            }
            default: // MIDI: canal 1..4 -> pulso 1, pulso 2, triángulo/onda, ruido
            {
                const auto c = m.getChannel() - 1;
                return c >= 0 && c < numChannels ? c : -1;
            }
        }
    }

    void handleMidi (const juce::MidiMessage& m, const Params& p)
    {
        juce::ignoreUnused (p);
        if (vrc6HandleMidi (m))
            return;
        if (voiceMode == 6 || voiceMode == 7) // modos solo VRC6 (sin la expansión activa, no suenan)
            if (m.isNoteOn() || m.isNoteOff()) return;
        const auto toDmc = nes && (voiceMode == 5 || (voiceMode == 4 && m.getChannel() == 5));
        if (toDmc && (m.isNoteOn() || m.isNoteOff()))
        {
            if (m.isNoteOn()) dmcNoteOn (m.getNoteNumber());
            else if (dmc.note == m.getNoteNumber() && dmc.loop) dmc.bytesRemaining = 0; // suelta: termina el byte actual
            return;
        }
        if (m.isNoteOn())
        {
            const auto c = channelFor (m, true);
            if (c < 0) return;
            auto& d = drv[(size_t) c];
            if (voiceMode == 3 && d.numHeld > 0) d.numHeld = 0; // Poly con robo: el canal pasa a la nota nueva
            if (d.numHeld < (int) d.held.size())
                d.held[(size_t) d.numHeld++] = m.getNoteNumber();
            d.velocity = m.getVelocity();
            const auto retrigger = ! d.gate || d.releasing || arpMode == 0;
            d.note = m.getNoteNumber();
            d.gate = true;
            d.releasing = false;
            if (retrigger)
            {
                d.frames = 0;
                d.arpIndex = d.arpCounter = 0;
                keyOn (c);
            }
        }
        else if (m.isNoteOff())
        {
            for (int c = 0; c < numChannels; ++c)
            {
                auto& d = drv[(size_t) c];
                int j = 0;
                for (int k = 0; k < d.numHeld; ++k)
                    if (d.held[(size_t) k] != m.getNoteNumber()) d.held[(size_t) j++] = d.held[(size_t) k];
                if (j == d.numHeld) continue;
                d.numHeld = j;
                if (d.numHeld > 0)
                {
                    d.note = d.held[(size_t) d.numHeld - 1]; // vuelve a la última nota pulsada (sin reataque)
                    writePeriod (c, false);
                }
                else
                {
                    keyOff (c);
                }
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (int c = 0; c < numChannels; ++c)
            {
                drv[(size_t) c].numHeld = 0;
                keyOff (c);
            }
            for (int k = 0; k < 3; ++k) { vrcVoice[(size_t) k].numHeld = 0; if (vrcVoice[(size_t) k].gate) vrc6KeyOff (k); }
            if (m.isAllSoundOff())
            {
                const auto model = lastModel;
                reset();
                lastModel = model;
                setupModel (model);
            }
        }
    }

    // ---- DMC
    struct Dmc
    {
        const std::uint8_t* data = nullptr;
        int length = 0, address = 0, bytesRemaining = 0;
        int buffer = -1, shift = 0, bitsRemaining = 8;
        bool silence = true, loop = false, active = false;
        int level = 0, rate = 15, note = -1;
        double countdown = 0.0;
    };

    struct DpcmSample { std::array<std::uint8_t, 4081> bytes {}; int length = 0; };

    static int dmcPeriod (bool palClock, int rate)
    {
        static constexpr int ntsc[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };
        static constexpr int palT[16] = { 398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50 };
        return palClock ? palT[rate] : ntsc[rate];
    }

    // Codificador DPCM (como los importadores de FamiTracker): a cada bit, sube 2 si el nivel previsto queda por debajo de la
    // señal y baja 2 si no. pcm: muestras en [-1, 1] ya a la tasa de bits del DMC; máximo 4081 bytes (32 648 bits)
    template <typename Fn>
    static void encodeDpcm (Fn&& pcm, int numBits, DpcmSample& out)
    {
        numBits = std::min (numBits, 4081 * 8);
        out.bytes.fill (0);
        int level = 64;
        for (int i = 0; i < numBits; ++i)
        {
            const auto target = 64.0f + 60.0f * juce::jlimit (-1.0f, 1.0f, pcm (i));
            const auto up = target >= (float) level; // FamiTracker (PCMImport.cpp): Sample >= Delta -> 1
            if (up) { out.bytes[(size_t) (i >> 3)] |= (std::uint8_t) (1u << (i & 7)); if (level <= 125) level += 2; }
            else if (level >= 2) level -= 2;
        }
        out.length = (numBits + 7) / 8;
    }

    // Samples de fábrica: síntesis propia (determinista) a la tasa 15 NTSC (1 789 773 / 54 ≈ 33 144 bits/s)
    static const std::array<DpcmSample, 5>& factoryDpcm()
    {
        static const auto set = []
        {
            std::array<DpcmSample, 5> kit;
            const auto fs = 1789772.73 / 54.0;
            const auto twoPi = juce::MathConstants<double>::twoPi;
            unsigned seed = 12345;
            auto noise = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 9) & 0x7fff) / 16384.0f - 1.0f; };
            double ph = 0.0;
            auto make = [&] (int index, double seconds, std::function<float (double)> gen)
            {
                ph = 0.0;
                encodeDpcm ([&] (int i) { return gen (i / fs); }, (int) (seconds * fs), kit[(size_t) index]);
            };
            make (0, 0.25, [&] (double t) { ph += (50.0 + 110.0 * std::exp (-t / 0.03)) / fs;              // bombo
                                            return (float) (std::sin (twoPi * ph) * std::exp (-t / 0.12)); });
            make (1, 0.22, [&] (double t) { ph += 180.0 / fs;                                                 // caja
                                            return (float) (0.5 * std::sin (twoPi * ph) * std::exp (-t / 0.05)) + 0.7f * noise() * (float) std::exp (-t / 0.09); });
            make (2, 0.35, [&] (double t) { ph += (140.0 + 60.0 * std::exp (-t / 0.05)) / fs;                // tom
                                            return (float) (std::sin (twoPi * ph) * std::exp (-t / 0.18)); });
            make (3, 0.9,  [&] (double t) { ph += 90.0 / fs;                                                  // timbal
                                            return (float) ((0.7 * std::sin (twoPi * ph) + 0.3 * std::sin (twoPi * 1.5 * ph)) * std::exp (-t / 0.6)); });
            make (4, 0.6,  [&] (double t) { ph += 110.0 / fs; double y = 0.0;                                 // bajo slap
                                            for (int h = 1; h <= 6; ++h) y += std::sin (twoPi * h * ph) / h * std::exp (-t * (3.0 + 4.0 * h));
                                            return (float) (0.9 * y); });
            return kit;
        }();
        return set;
    }

    // User Sample (su propio fichero): se recodifica cuando cambia el fichero o su región Start/Length (buffer fijo de 4081
    // bytes: sin reservar memoria en audio). Un .dmc se toca tal cual, sin recodificar (Start y Length no aplican)
    void prepareUserDpcm (const SampleData* sample, const Params& p)
    {
        const auto length = sample != nullptr ? sample->length() : 0;
        const auto start = p.dpcmSampleStart->load(), lengthMs = p.dpcmSampleLength->load();
        if (sample == userSource && length == userSourceLength && start == userStart && lengthMs == userLengthMs)
            return;
        userSource = sample;
        userSourceLength = length;
        userStart = start;
        userLengthMs = lengthMs;
        userDpcm.length = 0;
        if (sample == nullptr)
            return;
        if (! sample->dpcm.empty())
        {
            userDpcm.length = (int) std::min (sample->dpcm.size(), userDpcm.bytes.size());
            std::copy_n (sample->dpcm.begin(), userDpcm.length, userDpcm.bytes.begin());
            return;
        }
        if (sample->length() < 2)
            return;
        // Remuestreo a 33 144 Hz: nivel de mipmap (media banda) más alto que no baje de esa tasa, leído con Hermite
        constexpr auto bitRate = 1789772.73 / 54.0;
        const auto ratio = sample->sampleRate / bitRate;
        const auto level = juce::jlimit (0, SampleData::maxLevels - 1, (int) std::floor (std::log2 (std::max (1.0, ratio))));
        const auto scale = ratio / std::exp2 ((double) level);
        const auto first = (double) start * length / std::exp2 ((double) level);
        auto bits = (int) ((1.0 - start) * length / ratio);
        if (lengthMs > 0.0f)
            bits = std::min (bits, (int) (lengthMs / 1000.0 * bitRate));
        encodeDpcm ([sample, level, scale, first] (int i) { return sample->read (level, first + i * scale); },
                    std::min (4081 * 8, bits), userDpcm);
    }

    void dmcNoteOn (int note)
    {
        int source = dpcmSource, rate = 15;
        if (source == 0) // kit: notas de percusión GM
        {
            switch (note)
            {
                case 35: case 36: source = 1; break;
                case 38: case 40: source = 2; break;
                case 41: case 43: source = 3; rate = 12; break;
                case 45: case 47: source = 3; rate = 14; break;
                case 48: case 50: source = 3; rate = 15; break;
                case 52: case 53: source = 4; break;
                case 55: case 57: source = 5; break;
                default: return;
            }
        }
        else
        {
            rate = juce::jlimit (0, 15, 15 - (60 - note)); // C4 = tasa 15 (la más rápida); cada semitono abajo, una tasa
        }
        const auto* sample = source == 6 ? &userDpcm : &factoryDpcm()[(size_t) juce::jlimit (0, 4, source - 1)];
        if (sample->length == 0)
            return;
        dmc.data = sample->bytes.data();
        dmc.length = sample->length;
        dmc.address = 0;
        dmc.bytesRemaining = sample->length;
        dmc.loop = dpcmLoop;
        dmc.rate = rate;
        dmc.note = note;
        dmc.active = true;
        if (dmc.buffer < 0) dmcFetch();
    }

    void dmcFetch()
    {
        if (dmc.bytesRemaining <= 0 || dmc.data == nullptr)
            return;
        dmc.buffer = dmc.data[dmc.address];
        dmc.address = (dmc.address + 1) % juce::jmax (1, dmc.length);
        if (--dmc.bytesRemaining == 0 && dmc.loop)
        {
            dmc.address = 0;
            dmc.bytesRemaining = dmc.length;
        }
    }

    // Ciclo de salida del DMC (nesdev APU_DMC)
    void dmcClock()
    {
        if (! dmc.silence)
        {
            if (dmc.shift & 1) { if (dmc.level <= 125) dmc.level += 2; }
            else               { if (dmc.level >= 2)   dmc.level -= 2; }
        }
        dmc.shift >>= 1;
        if (--dmc.bitsRemaining == 0)
        {
            dmc.bitsRemaining = 8;
            if (dmc.buffer < 0) dmc.silence = true;
            else
            {
                dmc.silence = false;
                dmc.shift = dmc.buffer;
                dmc.buffer = -1;
                dmcFetch();
            }
        }
    }

    // ---- Escrituras de registros como las haría el driver
    void keyOn (int c)
    {
        auto& k = ch[(size_t) c];
        auto& d = drv[(size_t) c];
        const auto vel = velocityToVolume ? d.velocity / 127.0 : 1.0;
        for (int m = 0; m < MacroSet::numKinds; ++m)
            d.mc[(size_t) m].start (macros->macro[(size_t) m]);
        d.pitchAcc = macros->macro[MacroSet::pitch].empty() ? 0 : d.mc[MacroSet::pitch].value (macros->macro[MacroSet::pitch], 0);
        d.macroRelease = false;
        d.waveShift = -1;
        k.duty = duty;
        k.shortNoise = shortNoiseParam;
        if (! nes && c == tri)
            loadWaveRam (false);
        if (nes)
        {
            k.constantVolume = envMode == 0;
            k.envLoop = envMode == 2;
            k.envPeriod = envRate;
            k.volume = k.constantVolume ? juce::jlimit (0, 15, (int) std::lround (baseVolume * vel)) : envRate;
            k.envStart = true;                                    // escritura en $4003/$4007/$400F
            k.sweepEnabled = sweep != 0 && sweepRate > 0;
            k.sweepNegate = sweep <= 0;                           // el driver pone negate con el sweep apagado (evita el silencio)
            k.sweepShift = std::abs (sweep);
            k.sweepPeriod = std::max (0, sweepRate - 1);
            k.sweepReload = true;
            if (c == pulse1 || c == pulse2)
                k.step = 0;                                       // reinicio de fase de la secuencia
            k.running = true;
        }
        else
        {
            k.volume = juce::jlimit (0, 15, (int) std::lround (baseVolume * vel));
            k.envUp = envMode == 2;
            k.envPeriod = envMode == 0 ? 0 : juce::jlimit (1, 7, envRate);
            k.envDivider = k.envPeriod == 0 ? 8 : k.envPeriod;
            k.dacOn = c == tri ? true : (k.volume > 0 || k.envUp);  // NRx2 & $F8 != 0
            k.sweepEnabled = c == pulse1 && sweep != 0 && sweepRate > 0;
            k.sweepNegate = sweep < 0;
            k.sweepShift = std::abs (sweep);
            k.sweepPeriod = sweepRate;
            k.sweepDivider = sweepRate == 0 ? 8 : sweepRate;
            if (c == noise) k.lfsr = 0x7fff;
            if (c == tri)
            {
                // Bug del DMG: redisparo con el canal sonando, 1 tick (2 ciclos) antes de que lea el siguiente byte
                if (lastModel == gbDmg && k.running && k.countdown < 2.0)
                {
                    const auto byte = ((k.step + 1) >> 1) & 15;
                    if (byte < 4)
                    {
                        waveRam[0] = waveRam[(size_t) (2 * byte)];
                        waveRam[1] = waveRam[(size_t) (2 * byte + 1)];
                    }
                    else
                    {
                        const auto block = (byte & ~3) * 2;
                        for (int q = 0; q < 8; ++q)
                            waveRam[(size_t) q] = waveRam[(size_t) (block + q)];
                    }
                }
                k.step = 0; // posición 0 sin rellenar el buffer: suena antes la muestra vieja
            }
            k.running = true;
        }
        writePeriod (c, true);
        if (! nes)
            k.countdown = stepCycles (c) + (c == tri ? 6.0 : 0.0); // recarga del temporizador; el canal 3 arranca 6 ciclos tarde
        d.lastGbVolume = k.volume;
        applyMacroLevels (c, true);
        updateLevel (c);
    }

    // Volumen y duty de las macros, como los escribe el driver cada frame.
    //  Volumen: v = techo(macro · volumen del canal / 15) si ninguno es 0 (FamiTracker). NES pulsos/ruido: volumen constante
    //  en $4000/$4004/$400C; triángulo: macro 0 lo detiene (contador lineal), > 0 lo activa. Game Boy pulsos/ruido: NRx2 y
    //  disparo, solo si cambia (en el ruido el disparo reinicia el LFSR; en los pulsos recarga el temporizador: el chip lo
    //  hace así); canal de onda: NR32 (100/50/25 % o mudo según la macro: < 2 mudo, < 6 25 %, < 11 50 %, si no 100 %).
    //  Duty: pulsos, el ciclo de trabajo (sin reiniciar la fase); ruido, el modo corto (bit de modo / LFSR de 7 bits).
    void applyMacroLevels (int c, bool atTrigger)
    {
        auto& k = ch[(size_t) c];
        auto& d = drv[(size_t) c];
        const auto& dutyMacro = macros->macro[MacroSet::duty];
        if (d.mc[MacroSet::duty].active())
        {
            const auto v = d.mc[MacroSet::duty].value (dutyMacro, duty);
            if (c == noise) k.shortNoise = (v & 1) != 0;
            else if (c != tri) k.duty = v & 3;
        }
        const auto& volMacro = macros->macro[MacroSet::volume];
        if (! d.mc[MacroSet::volume].active())
            return;
        const auto mv = d.mc[MacroSet::volume].value (volMacro, 15);
        const auto vel = velocityToVolume ? d.velocity / 127.0 : 1.0;
        const auto channelVolume = juce::jlimit (0, 15, (int) std::lround (baseVolume * vel));
        const auto v = mv == 0 || channelVolume == 0 ? 0 : std::max (1, (mv * channelVolume + 14) / 15);
        if (nes)
        {
            if (c == tri) k.running = mv > 0;
            else { k.constantVolume = true; k.volume = v; }
        }
        else if (c == tri)
        {
            d.waveShift = mv < 2 ? 4 : mv < 6 ? 2 : mv < 11 ? 1 : 0;
        }
        else if (atTrigger || v != d.lastGbVolume)
        {
            k.volume = v;
            k.envPeriod = 0;
            k.envUp = false;
            k.dacOn = v > 0;
            k.running = true;
            if (! atTrigger)
            {
                if (c == noise) k.lfsr = 0x7fff;
                k.countdown = stepCycles (c);
            }
            d.lastGbVolume = v;
        }
    }

    void keyOff (int c)
    {
        auto& d = drv[(size_t) c];
        d.gate = false;
        if (std::any_of (macros->macro.begin(), macros->macro.end(), [] (const FrameMacro& m) { return m.release >= 0; }))
        {
            for (int m = 0; m < MacroSet::numKinds; ++m)
                d.mc[(size_t) m].noteOff (macros->macro[(size_t) m]);
            d.macroRelease = true;
            return;
        }
        if (releaseFrames > 0 && ! nes && c != tri)
        {
            // Envolvente descendente con periodo p: llega a 0 en vol·p/64 s ≈ release (redisparo: el pulso no reinicia fase)
            auto& k = ch[(size_t) c];
            const auto p = juce::jlimit (1, 7, (int) std::lround (64.0 * releaseFrames / (driverRate * std::max (1, k.volume))));
            k.envUp = false;
            k.envPeriod = p;
            k.envDivider = p;
            k.countdown = stepCycles (c);
            d.releasing = false;
            return;
        }
        if (releaseFrames > 0)
        {
            d.releasing = true;
            d.releaseFrame = 0;
            return;
        }
        silence (c);
    }

    void silence (int c)
    {
        auto& k = ch[(size_t) c];
        drv[(size_t) c].releasing = false;
        drv[(size_t) c].macroRelease = false;
        if (nes)
        {
            if (c == tri) k.running = false; // el triángulo se congela en su paso actual
            else          { k.constantVolume = true; k.volume = 0; }
        }
        else
        {
            k.running = false;
            k.dacOn = false; // NRx2 = 0: DAC apagado (salida analógica 0)
        }
        updateLevel (c);
    }

    // Nota (con arpegio, vibrato y pitch bend) -> periodo entero del chip
    void writePeriod (int c, bool noteStart)
    {
        auto& d = drv[(size_t) c];
        auto& k = ch[(size_t) c];
        if (d.note < 0) return;
        auto note = d.note;
        if (arpMode > 0 && d.numHeld > 1)
        {
            std::array<int, 16> sorted {};
            std::copy (d.held.begin(), d.held.begin() + d.numHeld, sorted.begin());
            std::sort (sorted.begin(), sorted.begin() + d.numHeld);
            const auto idx = d.arpIndex % d.numHeld;
            note = sorted[(size_t) (arpMode == 1 ? idx : d.numHeld - 1 - idx)];
        }
        if (d.mc[MacroSet::arpeggio].active()) // macro de arpegio: semitonos sobre la nota, o nota absoluta (@n, desde C-0)
        {
            const auto& am = macros->macro[MacroSet::arpeggio];
            const auto v = d.mc[MacroSet::arpeggio].value (am, 0);
            note = juce::jlimit (0, 127, d.mc[MacroSet::arpeggio].isAbsolute (am) ? v + 12 : note + v);
        }
        if (c == noise)
        {
            if (nes) k.period = noisePeriod (juce::jlimit (0, 15, 15 - ((note - 36) % 16 + 16) % 16));
            else
            {
                const auto idx = juce::jlimit (0, (int) gbNoisePeriods.size() - 1, note - 24);
                k.period = gbNoisePeriods[(size_t) idx];
            }
            return;
        }
        auto cents = tuneCents + 100.0 * bendSemis;
        if (vibDepth > 0.0f && d.frames >= vibDelay)
            cents += vibDepth * std::sin (juce::MathConstants<double>::twoPi * vibRate * (d.frames - vibDelay) / driverRate);
        const auto f = 440.0 * std::exp2 ((note - 69) / 12.0 + cents / 1200.0) * KoniTuning::ratio (note);
        int t;
        if (nes) t = (int) std::lround (periodClock / ((c == tri ? 32.0 : 16.0) * f) - 1.0);
        else     t = (int) std::lround (2048.0 - (c == tri ? 65536.0 : 131072.0) / f);
        // Macro de pitch: desplazamiento acumulado del registro de periodo; + = periodo más largo (más grave) en los dos chips
        t += nes ? d.pitchAcc : -d.pitchAcc;
        t = juce::jlimit (0, 2047, t);
        // NES: escribir el byte alto del periodo ($4003/$4007) reinicia la secuencia del pulso; el driver solo lo
        // escribe si cambia (por eso el vibrato que cruza un límite de 256 hace clic)
        if (nes && ! noteStart && (c == pulse1 || c == pulse2) && (t >> 8) != (k.period >> 8))
            k.step = 0;
        k.period = t;
    }

    int noisePeriod (int index) const
    {
        static constexpr int ntsc[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
        static constexpr int palT[16] = { 4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778 };
        return pal ? palT[index] : ntsc[index];
    }

    // ---- Duración de un paso de cada canal, en ciclos de reloj
    double stepCycles (int c) const
    {
        const auto& k = ch[(size_t) c];
        if (nes)
        {
            if (c == noise) return k.period;
            if (c == tri)   return k.period + 1.0;
            return 2.0 * (k.period + 1.0);
        }
        if (c == noise) return k.period;
        if (c == tri)   return 2.0 * (2048 - k.period);
        return 4.0 * (2048 - k.period);
    }

    bool clocks (int c) const
    {
        const auto& k = ch[(size_t) c];
        if (nes) return c != tri || k.running;     // pulsos y ruido corren siempre (su salida la decide el volumen)
        return k.running;
    }

    // Avanza la secuencia del canal un paso
    void advance (int c)
    {
        auto& k = ch[(size_t) c];
        if (c == noise)
        {
            if (nes)
            {
                const auto fb = (k.lfsr ^ (k.lfsr >> (k.shortNoise ? 6 : 1))) & 1u;
                k.lfsr = (k.lfsr >> 1) | (fb << 14);
            }
            else
            {
                const auto fb = (k.lfsr ^ (k.lfsr >> 1)) & 1u;
                k.lfsr = (k.lfsr >> 1) | (fb << 14);
                if (k.shortNoise) k.lfsr = (k.lfsr & ~(1u << 6)) | (fb << 6);
            }
        }
        else if (c == tri)
        {
            k.step = (k.step + 1) & 31;
            if (! nes)
                k.waveSample = waveRam[(size_t) k.step];
        }
        else
        {
            k.step = nes ? (k.step + 7) & 7 : (k.step + 1) & 7; // NES: el contador decrementa
        }
        updateLevel (c);
    }

    int envelopeLevel (const ChipChannel& k) const
    {
        if (nes) return k.constantVolume ? k.volume : k.envDecay;
        return k.volume;
    }

    void updateLevel (int c)
    {
        auto& k = ch[(size_t) c];
        static constexpr std::uint8_t nesDuty[4] = { 0b00000001, 0b00000011, 0b00001111, 0b11111100 };
        static constexpr std::uint8_t gbDuty[4]  = { 0b00000001, 0b10000001, 0b10000111, 0b01111110 };
        static constexpr int triSeq[32] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
                                            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        if (nes)
        {
            if (c == tri) { k.level = triSeq[k.step]; return; }
            const auto vol = envelopeLevel (k);
            if (c == noise) { k.level = (k.lfsr & 1u) ? 0 : vol; return; }
            const auto target = k.period + (k.sweepNegate ? -(k.period >> k.sweepShift) - (c == pulse1 ? 1 : 0)
                                                          : (k.period >> k.sweepShift));
            const auto muted = k.period < 8 || target > 0x7ff;
            const auto bit = (nesDuty[k.duty] >> (7 - k.step)) & 1;  // bits leídos en el orden 0, 7, 6 ... 1 del contador
            k.level = muted ? 0 : bit * vol;
            return;
        }
        if (c == tri)
        {
            const auto shift = drv[(size_t) c].waveShift >= 0 ? drv[(size_t) c].waveShift : waveShift;
            k.level = k.running ? (k.waveSample >> std::min (4, shift)) : 0;
            return;
        }
        if (c == noise) { k.level = (k.lfsr & 1u) ? 0 : k.volume; return; }
        k.level = ((gbDuty[k.duty] >> (7 - k.step)) & 1) * k.volume;
    }

    // Mezclador de la consola sobre las salidas digitales actuales
    // Mezcla de salida (izquierda, derecha). NES: mono (la misma en los dos lados)
    std::array<double, 2> mix() const
    {
        if (nes)
        {
            const auto p = ch[pulse1].level + ch[pulse2].level;
            const auto pulseOut = p == 0 ? 0.0 : 95.88 / (8128.0 / p + 100.0);
            const auto t = ch[tri].level, n = ch[noise].level, d = dmc.level;
            const auto tnd = (t == 0 && n == 0 && d == 0) ? 0.0 : 159.79 / (1.0 / (t / 8227.0 + n / 12241.0 + d / 22638.0) + 100.0);
            // VRC6: DAC lineal propio, sumado fuera del mezclador no lineal
            const auto vrc = vrc6Active() ? 0.009959 * (vrc6Output (0) + vrc6Output (1) + vrc6Output (2)) : 0.0;
            return { pulseOut + tnd + vrc, pulseOut + tnd + vrc };
        }
        // Game Boy: DAC lineal invertido por canal (0 -> +1, 15 -> -1), apagado = 0; cada lado suma los canales que NR51 le
        // envía (estéreo por los auriculares; el altavoz de la consola es mono)
        std::array<double, 2> s { 0.0, 0.0 };
        for (int c = 0; c < numChannels; ++c)
        {
            const auto& k = ch[(size_t) c];
            const auto on = c == tri ? k.running : (k.dacOn && k.running);
            if (! on) continue;
            const auto v = 1.0 - 2.0 * k.level / 15.0;
            if (panL[(size_t) c]) s[0] += v;
            if (panR[(size_t) c]) s[1] += v;
        }
        return { 0.075 * s[0], 0.075 * s[1] }; // nivel parecido al de un pulso del NES a volumen 15
    }

    // Escalones de la mezcla en la posición pos (en muestras) hacia los dos búferes BLEP
    void emitMix (double pos)
    {
        const auto m = mix();
        for (size_t side = 0; side < 2; ++side)
            if (m[side] != lastMix[side])
            {
                blep[side].addDelta (pos, m[side] - lastMix[side]);
                lastMix[side] = m[side];
            }
    }

    // ---- Una muestra de salida: eventos de los 4 canales en orden temporal, más secuenciador y driver
    void stepSample (int i, const Params& p)
    {
        juce::ignoreUnused (p);
        const auto cps = clock / sampleRate;

        // Secuenciador de frames del chip (NES: 4 pasos de cuarto de frame; GB: 512 Hz, 8 pasos)
        frameClock += cps;
        const auto quarter = nes ? frameLength / 4.0 : frameLength;
        while (frameClock >= quarter)
        {
            frameClock -= quarter;
            frameStep = (frameStep + 1) & (nes ? 3 : 7);
            if (nes) nesFrameStep (frameStep);
            else     gbFrameStep (frameStep);
        }
        // Driver de música a la frecuencia de vídeo
        driverClock += driverRate / sampleRate;
        if (driverClock >= 1.0)
        {
            driverClock -= 1.0;
            driverFrame();
        }
        emitMix (i);

        // Pasos de secuencia dentro de esta muestra, fusionando los canales (el DMC, índice numChannels, y los 3 del VRC6,
        // numChannels + 1..3) en orden de tiempo
        const auto dmcRuns = nes && dmc.active;
        const auto vrcRuns = vrc6Active();
        for (;;)
        {
            int next = -1;
            double t = cps;
            for (int c = 0; c < numChannels; ++c)
                if (clocks (c) && ch[(size_t) c].countdown < t)
                {
                    t = ch[(size_t) c].countdown;
                    next = c;
                }
            if (dmcRuns && dmc.countdown < t)
            {
                t = dmc.countdown;
                next = numChannels;
            }
            if (vrcRuns)
                for (int k = 0; k < 3; ++k)
                    if (vrc6Running (k) && vrc6Countdown (k) < t) { t = vrc6Countdown (k); next = numChannels + 1 + k; }
            if (next < 0) break;
            if (next > numChannels)
            {
                const auto k = next - numChannels - 1;
                vrc6Advance (k);
                vrc6Countdown (k) += vrc6Period (k) + 1.0;
                emitMix (i + juce::jlimit (0.0, 0.999999, t / cps));
                continue;
            }
            if (next == numChannels)
            {
                dmcClock();
                dmc.countdown += dmcPeriod (pal, dmc.rate);
                emitMix (i + juce::jlimit (0.0, 0.999999, t / cps));
                continue;
            }
            advance (next);
            ch[(size_t) next].countdown += std::max (1.0, stepCycles (next));
            emitMix (i + juce::jlimit (0.0, 0.999999, t / cps));
        }
        for (int c = 0; c < numChannels; ++c)
            if (clocks (c))
                ch[(size_t) c].countdown -= cps;
        if (dmcRuns)
            dmc.countdown -= cps;
        if (vrcRuns)
            for (int k = 0; k < 3; ++k)
                if (vrc6Running (k)) vrc6Countdown (k) -= cps;
    }

    void nesFrameStep (int s)
    {
        // Cuarto de frame: envolventes (y contador lineal del triángulo, que aquí sigue al gate)
        for (int c : { pulse1, pulse2, noise })
        {
            auto& k = ch[(size_t) c];
            if (k.envStart)
            {
                k.envStart = false;
                k.envDecay = 15;
                k.envDivider = k.envPeriod;
            }
            else if (k.envDivider-- == 0)
            {
                k.envDivider = k.envPeriod;
                if (k.envDecay > 0) --k.envDecay;
                else if (k.envLoop) k.envDecay = 15;
            }
            updateLevel (c);
        }
        // Medio frame (pasos 1 y 3 del ciclo de 4): sweep
        if (s == 1 || s == 3)
            for (int c : { pulse1, pulse2 })
            {
                auto& k = ch[(size_t) c];
                const auto change = k.period >> k.sweepShift;
                const auto target = k.sweepNegate ? k.period - change - (c == pulse1 ? 1 : 0) : k.period + change;
                if (k.sweepDivider == 0 && k.sweepEnabled && k.sweepShift > 0 && k.period >= 8 && target <= 0x7ff)
                    k.period = std::max (0, target);
                if (k.sweepDivider == 0 || k.sweepReload) { k.sweepDivider = k.sweepPeriod; k.sweepReload = false; }
                else --k.sweepDivider;
                updateLevel (c);
            }
    }

    void gbFrameStep (int s)
    {
        if (s == 2 || s == 6) // sweep del canal 1 a 128 Hz
        {
            auto& k = ch[pulse1];
            if (--k.sweepDivider <= 0)
            {
                k.sweepDivider = k.sweepPeriod == 0 ? 8 : k.sweepPeriod;
                if (k.sweepEnabled && k.sweepPeriod > 0 && k.running)
                {
                    const auto change = k.period >> k.sweepShift;
                    const auto next = k.sweepNegate ? k.period - change : k.period + change;
                    if (next > 2047) k.running = false;          // desbordamiento: el canal se apaga
                    else if (k.sweepShift > 0) k.period = std::max (0, next);
                    updateLevel (pulse1);
                }
            }
        }
        if (s == 7) // envolventes a 64 Hz
            for (int c : { pulse1, pulse2, noise })
            {
                auto& k = ch[(size_t) c];
                if (k.envPeriod == 0) continue;
                if (--k.envDivider <= 0)
                {
                    k.envDivider = k.envPeriod;
                    const auto v = k.volume + (k.envUp ? 1 : -1);
                    if (v >= 0 && v <= 15) k.volume = v;
                    updateLevel (c);
                }
            }
    }

    // Driver: arpegio, vibrato y release, una vez por frame de vídeo
    void driverFrame()
    {
        if (vrc6Active()) vrc6Frame();
        for (int c = 0; c < numChannels; ++c)
        {
            auto& d = drv[(size_t) c];
            if (! d.gate && ! d.releasing && ! d.macroRelease) continue;
            ++d.frames;
            for (int m = 0; m < MacroSet::numKinds; ++m)
                d.mc[(size_t) m].advance (macros->macro[(size_t) m]);
            if (d.mc[MacroSet::pitch].active())
                d.pitchAcc = juce::jlimit (-2047, 2047, d.pitchAcc + d.mc[MacroSet::pitch].value (macros->macro[MacroSet::pitch], 0));
            applyMacroLevels (c, false);
            if (d.macroRelease && d.mc[MacroSet::volume].finished (macros->macro[MacroSet::volume])
                && d.mc[MacroSet::volume].value (macros->macro[MacroSet::volume], 0) == 0)
            {
                silence (c);
                continue;
            }
            if (d.macroRelease && ! d.mc[MacroSet::volume].active()) // release sin macro de volumen: corta al acabar las demás
            {
                const auto done = std::all_of (d.mc.begin(), d.mc.end(), [this, &d] (const MacroCursor& mc)
                                               { return ! mc.active() || mc.finished (macros->macro[(size_t) (&mc - d.mc.data())]); });
                if (done) { silence (c); continue; }
            }
            updateLevel (c);
            if (arpMode > 0 && d.numHeld > 1 && ++d.arpCounter >= arpSpeed)
            {
                d.arpCounter = 0;
                ++d.arpIndex;
            }
            if (d.releasing)
            {
                auto& k = ch[(size_t) c];
                ++d.releaseFrame;
                const auto v = juce::jlimit (0, 15, (int) std::lround (baseVolume * (1.0 - (double) d.releaseFrame / releaseFrames)));
                if (nes && c != tri) { k.constantVolume = true; k.volume = v; }
                else if (! nes && c != tri) k.volume = v;
                if (d.releaseFrame >= releaseFrames) { silence (c); continue; }
                updateLevel (c);
            }
            writePeriod (c, false);
        }
    }

    // Salida analógica de la consola (opcional)
    double outputFilter (double x, int side)
    {
        const auto c = (size_t) side;
        if (! filterOn)
            return x - dcBlock (x, side);
        // paso altos de 1.er orden: y = a·(y + x - x1)
        hp1[c] = hpA * (hp1[c] + x - hpPrev1[c]); hpPrev1[c] = x;
        auto y = hp1[c];
        if (hpB < 1.0) { hp2[c] = hpB * (hp2[c] + y - hpPrev2[c]); hpPrev2[c] = y; y = hp2[c]; }
        if (useLp) { lp[c] = y + lpA * (lp[c] - y); y = lp[c]; }
        return y;
    }

    // Sin filtro de consola: solo se quita la componente continua (paso alto a 5 Hz)
    double dcBlock (double x, int side)
    {
        auto& d = dc[(size_t) side];
        d += (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sampleRate)) * (x - d);
        return d;
    }

    // Carga la tabla elegida en la wave RAM (cuando cambia la tabla o el modelo, o al reiniciar)
    void loadWaveRam (bool force)
    {
        if (! force && waveIndex == loadedWave && lastModel == loadedWaveModel)
            return;
        loadedWave = waveIndex;
        loadedWaveModel = lastModel;
        if (waveIndex == 6 && lastModel == gbCgb)
        {
            for (int i = 0; i < 32; ++i) // CGB al encender: 00 FF repetido (gbdev wiki)
                waveRam[(size_t) i] = (i & 2) ? 15 : 0;
            return;
        }
        waveRam = waveTable()[(size_t) waveIndex];
    }

    // Ondas del canal 3 de la Game Boy (32 muestras de 4 bits). La última es el contenido inicial de la wave RAM de una DMG
    // (84 40 43 AA 2D 78 92 3C 60 59 59 B0 34 B8 2E DA; gbdev wiki); el resto son formas propias
    static const std::array<std::array<int, 32>, 7>& waveTable()
    {
        static const auto table = []
        {
            std::array<std::array<int, 32>, 7> w {};
            for (int i = 0; i < 32; ++i)
            {
                w[0][(size_t) i] = i < 16 ? i : 31 - i;                                                     // triángulo
                w[1][(size_t) i] = i / 2;                                                                    // sierra
                w[2][(size_t) i] = juce::jlimit (0, 15, (int) std::lround (7.5 + 7.5 * std::sin (juce::MathConstants<double>::twoPi * i / 32.0))); // seno
                w[3][(size_t) i] = i < 16 ? 15 : 0;                                                          // cuadrada
                w[4][(size_t) i] = i < 8 ? 15 : 0;                                                           // pulso 25 %
                w[5][(size_t) i] = juce::jlimit (0, 15, (int) std::lround (7.5 + 4.0 * std::sin (juce::MathConstants<double>::twoPi * i / 32.0)
                                                                              + 3.5 * std::sin (2.0 * juce::MathConstants<double>::twoPi * i / 32.0))); // órgano
            }
            static constexpr std::uint8_t dmg[16] = { 0x84, 0x40, 0x43, 0xAA, 0x2D, 0x78, 0x92, 0x3C, 0x60, 0x59, 0x59, 0xB0, 0x34, 0xB8, 0x2E, 0xDA };
            for (int i = 0; i < 16; ++i)
            {
                w[6][(size_t) (2 * i)] = dmg[i] >> 4;       // nibble alto primero
                w[6][(size_t) (2 * i + 1)] = dmg[i] & 15;
            }
            return w;
        }();
        return table;
    }

    double sampleRate = 44100.0, clock = 1789772.73, frameLength = 29830.0, driverRate = 60.099;
    double frameClock = 0.0, driverClock = 0.0;
    std::array<double, 2> lastMix { 0.0, 0.0 };
    std::array<bool, 4> panL { true, true, true, true }, panR { true, true, true, true }; // NR51 (Game Boy)
    bool speakerOutput = false; // Game Boy por el altavoz: mono
    int frameStep = 0;
    bool nes = true, pal = false;
    double periodClock = 1789772.73; // reloj para el que se calculan los periodos (≠ clock solo en palPort)
    double hpA = 1.0, hpB = 1.0, lpA = 0.0;
    std::array<double, 2> hp1 {}, hp2 {}, lp {}, hpPrev1 {}, hpPrev2 {}, dc {};
    bool useLp = false, filterOn = true;
    std::array<ChipChannel, numChannels> ch {};
    std::array<Driver, numChannels> drv {};
    std::vector<int> gbNoisePeriods;
    std::array<int, 32> waveRam {};
    int loadedWave = -1, loadedWaveModel = -1;
    std::array<BlepBuffer, 2> blep; // izquierda, derecha
    Dmc dmc;
    DpcmSample userDpcm;
    const SampleData* userSource = nullptr;
    int userSourceLength = 0, dpcmSource = 0;
    float userStart = -1.0f, userLengthMs = -1.0f;
    bool dpcmLoop = false;
    int lastModel = -1, nextVoice = 0;
    // Parámetros del bloque
    int voiceMode = 0, duty = 2, baseVolume = 15, envMode = 0, envRate = 0, releaseFrames = 0, sweep = 0, sweepRate = 0;
    int arpMode = 0, arpSpeed = 1, vibDelay = 0, waveIndex = 0, waveShift = 0;
    float vibDepth = 0.0f, vibRate = 0.0f;
    bool shortNoiseParam = false, velocityToVolume = true;
    double tuneCents = 0.0, bendSemis = 0.0;
    const MacroSet* macros = nullptr; // del bloque (nunca nulo durante render)

    // ---- VRC6
    struct Vrc6Pulse { int period = 0, duty = 7, volume = 0, step = 15; bool enabled = false; double countdown = 0.0; };
    struct Vrc6Saw { int period = 0, rate = 0, acc = 0, tick = 0; bool enabled = false; double countdown = 0.0; };
    struct Vrc6Voice
    {
        std::array<int, 16> held {};
        int numHeld = 0, note = -1, velocity = 100, frames = 0, releaseFrame = 0;
        bool gate = false, releasing = false;
    };
    std::array<Vrc6Pulse, 2> vrcPulse {};
    Vrc6Saw vrcSaw {};
    std::array<Vrc6Voice, 3> vrcVoice {}; // 0, 1 = pulsos; 2 = sierra
    bool vrc6Expansion = false, vrc6Overdrive = false;
    int vrc6Duty = 7, nextVrcVoice = 0;

    bool vrc6Active() const { return vrc6Expansion && lastModel == famicom; }

    int vrc6Output (int k) const
    {
        if (k < 2)
        {
            const auto& v = vrcPulse[(size_t) k];
            return v.enabled && v.step <= v.duty ? v.volume : 0;
        }
        return vrcSaw.enabled ? vrcSaw.acc >> 3 : 0;
    }

    // Paso del divisor de un canal del VRC6
    void vrc6Advance (int k)
    {
        if (k < 2) { auto& v = vrcPulse[(size_t) k]; v.step = (v.step - 1) & 15; return; }
        auto& s = vrcSaw;
        if (++s.tick >= 14) { s.tick = 0; s.acc = 0; }
        else if ((s.tick & 1) == 0) s.acc = (s.acc + s.rate) & 0xff; // 8 bits: con A > 42 desborda
    }

    double& vrc6Countdown (int k) { return k < 2 ? vrcPulse[(size_t) k].countdown : vrcSaw.countdown; }
    bool vrc6Running (int k) const { return k < 2 ? vrcPulse[(size_t) k].enabled : vrcSaw.enabled; }
    int vrc6Period (int k) const { return k < 2 ? vrcPulse[(size_t) k].period : vrcSaw.period; }

    // Nota -> periodo de 12 bits, con vibrato y pitch bend del driver
    void vrc6WritePeriod (int k)
    {
        const auto& d = vrcVoice[(size_t) k];
        if (d.note < 0) return;
        auto cents = tuneCents + 100.0 * bendSemis;
        if (vibDepth > 0.0f && d.frames >= vibDelay)
            cents += vibDepth * std::sin (juce::MathConstants<double>::twoPi * vibRate * (d.frames - vibDelay) / driverRate);
        const auto f = 440.0 * std::exp2 ((d.note - 69) / 12.0 + cents / 1200.0) * KoniTuning::ratio (d.note);
        const auto t = juce::jlimit (0, 4095, (int) std::lround (clock / ((k < 2 ? 16.0 : 14.0) * f) - 1.0));
        if (k < 2) vrcPulse[(size_t) k].period = t; else vrcSaw.period = t;
    }

    void vrc6SetLevel (int k, int vol15)
    {
        if (k < 2) vrcPulse[(size_t) k].volume = juce::jlimit (0, 15, vol15);
        else vrcSaw.rate = juce::jlimit (0, 63, (int) std::lround (vol15 * (vrc6Overdrive ? 63.0 : 42.0) / 15.0));
    }

    void vrc6KeyOn (int k)
    {
        auto& d = vrcVoice[(size_t) k];
        d.frames = 0;
        d.releasing = false;
        const auto vel = velocityToVolume ? d.velocity / 127.0 : 1.0;
        vrc6SetLevel (k, (int) std::lround (baseVolume * vel));
        if (k < 2)
        {
            auto& v = vrcPulse[(size_t) k];
            v.duty = vrc6Duty;
            if (! v.enabled) { v.step = 15; v.countdown = 0.0; } // activar: la secuencia empieza de cero
            v.enabled = true;
        }
        else if (! vrcSaw.enabled)
        {
            vrcSaw.enabled = true; // el divisor no se reinicia (el primer paso puede salir corto, como en el chip)
        }
        vrc6WritePeriod (k);
    }

    void vrc6Silence (int k)
    {
        vrcVoice[(size_t) k].releasing = false;
        if (k < 2) { vrcPulse[(size_t) k].enabled = false; vrcPulse[(size_t) k].step = 15; }
        else { vrcSaw.enabled = false; vrcSaw.acc = 0; }
    }

    void vrc6KeyOff (int k)
    {
        auto& d = vrcVoice[(size_t) k];
        d.gate = false;
        if (releaseFrames > 0) { d.releasing = true; d.releaseFrame = 0; return; }
        vrc6Silence (k);
    }

    void vrc6Frame()
    {
        for (int k = 0; k < 3; ++k)
        {
            auto& d = vrcVoice[(size_t) k];
            if (! d.gate && ! d.releasing) continue;
            ++d.frames;
            if (d.releasing)
            {
                ++d.releaseFrame;
                const auto vel = velocityToVolume ? d.velocity / 127.0 : 1.0;
                vrc6SetLevel (k, (int) std::lround (baseVolume * vel * (1.0 - (double) d.releaseFrame / releaseFrames)));
                if (d.releaseFrame >= releaseFrames) { vrc6Silence (k); continue; }
            }
            vrc6WritePeriod (k);
        }
    }

    // Canal del VRC6 para un mensaje de nota (-1 = no va al VRC6)
    int vrc6ChannelFor (const juce::MidiMessage& m, bool noteOn)
    {
        if (! vrc6Active()) return -1;
        if (voiceMode == 6) return 0;
        if (voiceMode == 7) return 2;
        if (voiceMode == 4) { const auto c = m.getChannel(); return c >= 6 && c <= 8 ? c - 6 : -1; }
        if (voiceMode == 8)
        {
            if (! noteOn)
            {
                for (int k = 0; k < 3; ++k)
                    for (int j = 0; j < vrcVoice[(size_t) k].numHeld; ++j)
                        if (vrcVoice[(size_t) k].held[(size_t) j] == m.getNoteNumber()) return k;
                return -1;
            }
            // Poly + VRC6: primero los canales libres del 2A03 (pulsos y triángulo), luego los del VRC6, luego por turno
            for (int c : { pulse1, pulse2, tri })
                if (drv[(size_t) c].numHeld == 0) return -1;
            for (int k = 0; k < 3; ++k)
                if (vrcVoice[(size_t) k].numHeld == 0) return k;
            const auto k = nextVrcVoice;
            nextVrcVoice = (nextVrcVoice + 1) % 3;
            return k;
        }
        return -1;
    }

    // true si el mensaje fue al VRC6
    bool vrc6HandleMidi (const juce::MidiMessage& m)
    {
        if (! (m.isNoteOn() || m.isNoteOff())) return false;
        const auto k = vrc6ChannelFor (m, m.isNoteOn());
        if (k < 0) return false;
        auto& d = vrcVoice[(size_t) k];
        if (m.isNoteOn())
        {
            if (voiceMode == 8 && d.numHeld > 0) d.numHeld = 0;
            if (d.numHeld < (int) d.held.size()) d.held[(size_t) d.numHeld++] = m.getNoteNumber();
            d.velocity = m.getVelocity();
            d.note = m.getNoteNumber();
            d.gate = true;
            vrc6KeyOn (k);
            return true;
        }
        int j = 0;
        for (int r = 0; r < d.numHeld; ++r)
            if (d.held[(size_t) r] != m.getNoteNumber()) d.held[(size_t) j++] = d.held[(size_t) r];
        if (j == d.numHeld) return true;
        d.numHeld = j;
        if (d.numHeld > 0) { d.note = d.held[(size_t) d.numHeld - 1]; vrc6WritePeriod (k); }
        else vrc6KeyOff (k);
        return true;
    }
};
