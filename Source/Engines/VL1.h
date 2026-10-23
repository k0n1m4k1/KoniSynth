// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Casio VL-1 (VL-Tone, 1979): voz monofónica + caja de ritmos. Datos del emulador de PolyValens (2006, CC0/MIT,
// github.com/linuxmao-org/VL1-emulator): ondas (Wave.cpp), envolvente digital (VL1Defs.h, ADSR.cpp, LineInTime.cpp),
// etapa de salida (EnvelopeShaper.cpp: RC de carga 600 Ω / descarga 33 kΩ con 3,3 µF y curva cuadrática medida),
// respuesta de la salida (pasa-bajos 800 Hz y pasa-altos 65 Hz, PluginVL1.cpp), reloj (Clock.cpp) y ritmos
// (Rhythm.cpp, Noise.cpp). Diferencias: las ondas 0/1 llevan PolyBLEP por flanco (el emulador sobremuestrea 16× con
// media de caja); el pasa-altos de 65 Hz se aplica a melodía + ritmo (acoplamiento de salida; el ritmo 0/1 tiene DC).
// Ganancias calibradas con scratchpad/vl1_check.py: Piano A4 sostenido = 0,6 Vpp; Pock = 55/63 de la melodía
// (relación medida 55 mVpp / 63 mVpp citada en el emulador).
struct VL1
{
    // Ondas: flancos alternos empezando por subida en 0 (fase en ciclos); pitchScale en múltiplos de la nota
    struct WaveSpec { float pitchScale; int numEdges; double edges[10]; };
    static constexpr WaveSpec waves[7] = {
        { 1.0f, 2,  { 0.0, 11.0 / 16.0 } },                                                     // Piano: pulso 11/16
        { 2.0f, 2,  { 0.0, 0.5 } },                                                             // Fantasy: cuadrada, octava alta
        { 1.0f, 10, { 0.0, 4 / 16.0, 5 / 16.0, 8 / 16.0, 9 / 16.0, 11 / 16.0, 12 / 16.0, 13 / 16.0, 14 / 16.0, 15 / 16.0 } }, // Violin
        { 1.0f, 2,  { 0.0, 0.5 } },                                                             // Flute
        { 0.5f, 4,  { 0.0, 1 / 16.0, 7 / 16.0, 8 / 16.0 } },                                    // Guitar 1
        { 0.5f, 6,  { 0.0, 1 / 16.0, 2 / 16.0, 6 / 16.0, 8 / 16.0, 10 / 16.0 } },               // Guitar 2
        { 0.5f, 2,  { 0.0, 1.0 / 7.0 } },                                                       // English Horn: pulso 1/7
    };

    // Envolvente: 15 niveles, unidad de tiempo 2400 / 200 kHz = 12 ms (VL1Defs.h). Índice = dígito 0..9.
    static constexpr int attackBegin[10] = { 15, 11, 7, 5, 4, 4, 3, 3, 3, 3 };
    static constexpr int attackUnits[10] = { 0, 1, 4, 7, 10, 13, 16, 19, 22, 25 };
    static constexpr int decayUnits[10]  = { 0, 9, 18, 27, 39, 48, 58, 67, 78, 87 };
    static constexpr int sustainLevel[10] = { 1, 2, 4, 6, 8, 10, 12, 13, 14, 15 };
    static constexpr int sustainUnits[10] = { 0, 79, 154, 231, 312, 389, 467, 543, 615, -1 }; // -1: infinito
    static constexpr int releaseUnits[10] = { 0, 26, 48, 73, 96, 116, 142, 167, 193, 217 };

    // Ritmos: (sonido, duración en ticks). kHalf = 16 ticks.
    enum Drum { Pock, Peek, Noise };
    struct Step { Drum drum; int ticks; };
    struct Pattern { int numSteps; Step steps[18]; };
    static constexpr int H = 16, Q = 8, W = 32;
    static constexpr Pattern patterns[10] = {
        { 2,  { { Pock, W }, { Noise, W } } },                                                            // March
        { 3,  { { Pock, W }, { Noise, W }, { Noise, W } } },                                              // Waltz
        { 4,  { { Pock, W }, { Noise, W }, { Noise, W }, { Noise, W } } },                                // 4 Beat
        { 3,  { { Peek, W }, { Noise, 3 * Q }, { Peek, Q } } },                                           // Swing
        { 8,  { { Pock, H }, { Peek, H }, { Noise, H }, { Noise, H }, { Pock, H }, { Peek, H }, { Noise, H }, { Peek, H } } }, // Rock 1
        { 9,  { { Pock, H }, { Peek, H }, { Noise, H }, { Peek, Q }, { Pock, Q }, { Pock, H }, { Pock, H }, { Noise, H }, { Peek, H } } }, // Rock 2
        { 16, { { Peek, H }, { Noise, H }, { Noise, H }, { Peek, H }, { Pock, H }, { Noise, H }, { Peek, H }, { Noise, H },
                { Pock, H }, { Noise, H }, { Peek, H }, { Pock, H }, { Pock, H }, { Peek, H }, { Noise, H }, { Pock, H } } }, // Bossanova
        { 16, { { Peek, H }, { Noise, H }, { Peek, H }, { Noise, H }, { Pock, H }, { Pock, H }, { Noise, H }, { Peek, H },
                { Noise, H }, { Peek, H }, { Noise, H }, { Peek, H }, { Pock, H }, { Pock, H }, { Pock, H }, { Noise, H } } }, // Samba
        { 18, { { Peek, H }, { Noise, Q }, { Noise, Q }, { Noise, H }, { Peek, H }, { Noise, H }, { Noise, H }, { Peek, H }, { Noise, H },
                { Noise, H }, { Noise, Q }, { Noise, Q }, { Peek, H }, { Noise, H }, { Peek, H }, { Noise, H }, { Pock, H }, { Pock, H } } }, // Rhumba
        { 9,  { { Pock, H }, { Noise, Q }, { Noise, Q }, { Noise, H }, { Noise, H }, { Peek, H }, { Noise, H }, { Pock, H }, { Noise, H } } }, // Beguine
    };

    void prepare (double sr)
    {
        sampleRate = sr;
        unitSamples = 0.012 * sr;
        chargeCoef    = std::exp (-1.0 / (sr * 600.0 * 3.3e-6));   // τ = 1,98 ms
        dischargeCoef = std::exp (-1.0 / (sr * 33000.0 * 3.3e-6)); // τ = 108,9 ms
        lp800Coef = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 800.0 / sr);
        lp65Coef  = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 65.0 / sr);
        bendCoef  = 1.0f - (float) std::exp (-1.0 / (0.005 * sr));
        reset();
    }

    void reset()
    {
        numHeld = 0;
        stage = Idle;
        level = 0;
        phase = 0.0;
        cap = 0.0;
        lp800 = lp65 = 0.0f;
        tickAcc = 0.0;
        electroCounter = 8;
        electroHalf = false;
        rhythm = -1;
        drumTime = -1;
        lfsr = 0;
        noisePhase = 0.0;
    }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p)
    {
        const auto sound   = juce::jlimit (0, 9, (int) p.vl1Sound->load());
        const auto& wave   = waves[sound < 7 ? sound : sound - 7]; // Electro 1..3 = Piano, Fantasy, Violin modulados
        const auto electro = sound >= 7;
        if (! electro)
            electroHalf = false;
        digits = { (int) p.vl1Attack->load(), (int) p.vl1Decay->load(), (int) p.vl1SustainLevel->load(),
                   (int) p.vl1SustainTime->load(), (int) p.vl1Release->load() };
        const auto octave  = std::exp2 ((double) p.vl1Octave->load());
        const auto vib     = (int) p.vl1Vibrato->load();
        const auto trem    = (int) p.vl1Tremolo->load();
        const auto vibHz   = vib > 0 ? 15.0 / vib : 0.0;               // Voice.cpp: 15/V Hz
        // Vibrato del emulador: integra el LFO sobre el incremento -> Δf/f = 0,7056/(2π·f_vib) (±38 ct con V = 3)
        const auto vibDepth = vib > 0 ? 0.7056 / (juce::MathConstants<double>::twoPi * vibHz) : 0.0;
        const auto tremHz  = trem > 0 ? 35.0 / trem : 0.0;              // 35/T Hz, profundidad total
        const auto tempo   = (int) p.vl1Tempo->load();
        const auto tickPeriod = (0.0055 + (9 - tempo) * 0.00098) * sampleRate; // Clock.cpp, en muestras
        const auto balance = 0.5f * (p.vl1Balance->load() - 0.5f);
        const auto melodyGain = 2.0f * (0.5f + balance) * 0.1954f;
        const auto rhythmGain = 2.0f * (0.5f - balance) * 0.3571f;

        const auto newRhythm = (int) p.vl1Rhythm->load() - 1; // 0 = Off
        if (newRhythm != rhythm)
        {
            rhythm = newRhythm;
            beat = 0;
            stepCounter = 1;           // el primer tick dispara el paso 0
            tickAcc = tickPeriod;      // primer tick en la primera muestra
            drumTime = -1;
        }

        auto midiIt = midi.cbegin();
        const auto twoPi = juce::MathConstants<double>::twoPi;

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
                handleMidi ((*midiIt).getMessage(), wave.pitchScale * octave);

            // Reloj de ticks: modulación Electro (cada 8 ticks alterna ×1 / ×0,5) y caja de ritmos
            tickAcc += 1.0;
            if (tickAcc >= tickPeriod)
            {
                tickAcc -= tickPeriod;
                if (--electroCounter == 0)
                {
                    electroCounter = 8;
                    electroHalf = electro && ! electroHalf;
                }
                if (rhythm >= 0 && --stepCounter == 0)
                {
                    const auto& pat = patterns[rhythm];
                    drum = pat.steps[beat].drum;
                    stepCounter = pat.steps[beat].ticks;
                    beat = (beat + 1) % pat.numSteps;
                    drumTime = 0;
                }
            }

            // Melodía
            float y = 0.0f;
            const auto env = (float) clockEnvelope() / 15.0f;
            if (stage != Idle)
            {
                noteTime += 1.0 / sampleRate;
                const auto tremGain = tremHz > 0.0 ? 0.5 + 0.5 * std::cos (twoPi * tremHz * noteTime) : 1.0;
                bendSmoothed += bendCoef * (p.pitchBend.load() - bendSmoothed);
                auto dt = baseInc * (1.0 + vibDepth * std::sin (twoPi * vibHz * noteTime)) * std::exp2 (bendSmoothed / 12.0);
                if (electroHalf) dt *= 0.5;

                int below = 0;
                for (int e = 0; e < wave.numEdges; ++e)
                    below += wave.edges[e] <= phase ? 1 : 0;
                const auto high = (below & 1) == 1;

                const auto drive = tremGain * env;
                const auto vIn = high ? 5.0 * drive - 0.6 : -0.6;
                cap = vIn > cap ? vIn + (cap - vIn) * chargeCoef : cap * dischargeCoef;
                const auto h = drive > 1.0e-15 ? std::max (0.0, 0.131033183 * cap * cap + 0.246632173 * cap - 0.219399434) : 0.0;

                auto s = high ? h : 0.0;
                for (int e = 0; e < wave.numEdges; ++e)
                {
                    auto t = phase - wave.edges[e];
                    if (t < 0.0) t += 1.0;
                    s += ((e & 1) == 0 ? 0.5 : -0.5) * h * polyBlep (t, dt);
                }
                y = (float) s;

                phase += dt;
                if (phase >= 1.0) phase -= 1.0;
            }
            lp800 += lp800Coef * (melodyGain * y - lp800);

            const auto sum = lp800 + rhythmGain * nextDrum();
            lp65 += lp65Coef * (sum - lp65);
            const auto o = sum - lp65;

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, o);
        }
    }

private:
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    struct Digits { int a = 0, d = 0, sl = 9, st = 9, r = 0; };

    // Monofónico con prioridad a la última nota; al soltarla vuelve a la anterior aún pulsada (legato, sin reinicio de fase)
    void handleMidi (const juce::MidiMessage& m, double pitchMul)
    {
        if (m.isNoteOn())
        {
            removeHeld (m.getNoteNumber());
            if (numHeld < (int) held.size())
                held[(size_t) numHeld++] = m.getNoteNumber();
            startNote (m.getNoteNumber(), pitchMul);
        }
        else if (m.isNoteOff())
        {
            const auto wasLast = numHeld > 0 && held[(size_t) numHeld - 1] == m.getNoteNumber();
            removeHeld (m.getNoteNumber());
            if (wasLast)
            {
                if (numHeld > 0) startNote (held[(size_t) numHeld - 1], pitchMul);
                else gateOff();
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            numHeld = 0;
            gateOff();
        }
    }

    void removeHeld (int note)
    {
        int j = 0;
        for (int k = 0; k < numHeld; ++k)
            if (held[(size_t) k] != note)
                held[(size_t) j++] = held[(size_t) k];
        numHeld = j;
    }

    void startNote (int note, double pitchMul)
    {
        baseInc = 440.0 * std::exp2 ((note - 69) / 12.0) * KoniTuning::ratio (note) * pitchMul / sampleRate;
        if (stage == Idle)
        {
            phase = 0.0; // Voice.cpp: la fase solo se reinicia si la voz estaba en silencio
            cap = 0.0;
        }
        noteTime = 0.0; // LFOs de vibrato y tremolo reinician con cada nota
        releasePending = false;
        level = attackBegin[digits.a];
        startLine (Attack, 15, 15 - attackBegin[digits.a], attackUnits[digits.a]);
    }

    void gateOff()
    {
        if (stage == Attack)
            releasePending = true; // ADSR.cpp: el ataque siempre termina
        else if (stage != Idle)
            startLine (Release, 0, 14, releaseUnits[digits.r]);
    }

    // Recta desde el nivel actual hacia target con la pendiente de la tabla (span niveles en units·12 ms).
    // units = 0: salto inmediato; units < 0: plana (sustain infinito).
    void startLine (Stage newStage, int target, int span, int units)
    {
        stage = newStage;
        lineTarget = target;
        lineY = level;
        stepCountdown = unitSamples;
        lineDone = false;
        lineRate = 0.0;
        if (units == 0)
        {
            level = target;
            lineDone = true;
        }
        else if (units > 0)
        {
            lineRate = (target > level ? span : -span) / (units * unitSamples);
        }
    }

    void startSustain() { startLine (Sustain, 0, 15, sustainUnits[digits.st]); }

    // LineInTime.cpp: la recta continua avanza cada muestra; el nivel entero solo se actualiza cada 12 ms
    // (subiendo: techo de la recta; bajando: suelo). Devuelve el nivel 0..15.
    int clockEnvelope()
    {
        if (stage == Idle)
            return 0;

        if (! lineDone && lineRate != 0.0)
        {
            lineY += lineRate;
            const auto up = lineRate > 0.0;
            if (up ? lineY >= lineTarget : lineY <= lineTarget)
            {
                level = lineTarget;
                lineDone = true;
            }
            else if ((stepCountdown -= 1.0) <= 0.0)
            {
                stepCountdown += unitSamples;
                if (up && level < lineY)    level = (int) std::ceil (lineY);
                if (! up && level > lineY)  level = (int) std::floor (lineY);
            }
        }

        switch (stage)
        {
            case Attack:
                if (lineDone)
                {
                    if (releasePending)          startLine (Release, 0, 14, releaseUnits[digits.r]);
                    else if (digits.d > 0)       startLine (Decay, 0, 15, decayUnits[digits.d]);
                    else                         { level = sustainLevel[digits.sl]; startSustain(); }
                }
                break;
            case Decay:
                if (level <= sustainLevel[digits.sl])
                    startSustain();
                break;
            case Sustain:
            case Release:
                if (level <= 0)
                    stage = Idle;
                break;
            case Idle:
                break;
        }
        return level;
    }

    // Pock: cuadrada 0/1 de 780 Hz, 30 periodos, nivel -1/15 cada 2 periodos; Peek: igual al doble de velocidad.
    // Noise: LFSR de 16 bits a 50 kHz (tomas 15, 14, 12, 3, realimentación invertida), 8 escalones de 5 ms y 6 de 20 ms.
    // El LFSR corre libre entre golpes (el emulador repite una tabla fija; el hardware no está documentado).
    float nextDrum()
    {
        if (drumTime < 0)
            return 0.0f;

        const auto tau = (double) drumTime++ / sampleRate;
        if (drum == Noise)
        {
            int steps;
            if (tau < 0.040)      steps = (int) (tau / 0.005);
            else if (tau < 0.160) steps = 8 + (int) ((tau - 0.040) / 0.020);
            else                  { drumTime = -1; return 0.0f; }

            noisePhase += 50000.0 / sampleRate;
            while (noisePhase >= 1.0)
            {
                noisePhase -= 1.0;
                const auto bit = ((lfsr >> 15) ^ (lfsr >> 14) ^ (lfsr >> 12) ^ (lfsr >> 3) ^ 1) & 1;
                lfsr = ((lfsr << 1) | bit) & 0xffff;
            }
            return (float) (1.0 - steps / 15.0) * (float) ((lfsr >> 15) & 1);
        }

        const auto f  = drum == Pock ? 780.0 : 1560.0;
        const auto dt = f / sampleRate;
        const auto periods = tau * f;
        if (periods >= 30.0)
        {
            drumTime = -1;
            return 0.0f;
        }
        const auto lvl = 1.0 - std::floor (periods / 2.0) / 15.0;
        const auto t = periods - std::floor (periods);
        auto s = t < 0.5 ? lvl : 0.0;
        if (periods >= dt) // el primer flanco arranca desde silencio: sin corrección
        {
            auto t2 = t - 0.5;
            if (t2 < 0.0) t2 += 1.0;
            s += 0.5 * lvl * (polyBlep (t, dt) - polyBlep (t2, dt));
        }
        return (float) s;
    }

    double sampleRate = 44100.0, unitSamples = 529.2, chargeCoef = 0.0, dischargeCoef = 0.0;
    float bendSmoothed = 0.0f, bendCoef = 1.0f; // pitch bend (semitonos), un polo de 5 ms
    float lp800Coef = 0.0f, lp65Coef = 0.0f, lp800 = 0.0f, lp65 = 0.0f;
    std::array<int, 16> held {};
    int numHeld = 0;
    Digits digits;
    Stage stage = Idle;
    int level = 0, lineTarget = 0;
    double lineY = 0.0, lineRate = 0.0, stepCountdown = 0.0;
    bool lineDone = true, releasePending = false;
    double phase = 0.0, baseInc = 0.0, noteTime = 0.0, cap = 0.0;
    double tickAcc = 0.0;
    int electroCounter = 8;
    bool electroHalf = false;
    int rhythm = -1, beat = 0, stepCounter = 1, drumTime = -1;
    Drum drum = Pock;
    unsigned lfsr = 0;
    double noisePhase = 0.0;
};
