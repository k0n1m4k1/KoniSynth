// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Suzuki Omnichord (OM-84, 1984): acordes con botones, placa de rasgueo de 13 zonas ("Sonic Strings"), voz de acordes,
// bajo automático y ritmos. No hay manual de servicio ni esquemático público; las cifras [EMU] vienen del emulador
// Chordian (github.com/Jan125/pb.chordian), modelado de un tercero, no medidas de hardware:
//  - voicing de 3 notas plegado a MIDI 54..65; 7ª sin quinta (0,4,10); arpa: notas del acorde + 12·(n/3), 13 zonas = 4 octavas;
//  - decay del arpa τ = 0,366 + 2,734·Sustain s; bajo: raíz en MIDI 42..53, caída lineal en 1,22 s;
//  - voz de acordes: caída con τ 0,333 s mientras se mantiene y release de 0,18 s.
// 13 sensores y rango de 4 octavas: Wikipedia y forum.midibox.org. Timbre: cuadradas de divisor (timer 8253 según un foro, no
// verificado); "las notas agudas suenan casi triangulares" -> paso bajo fijo (corte estimado).
// Teclado: por debajo de Split, botones de acorde (se reconoce el acorde tocado: una tecla = mayor; con 3ª menor = menor;
// con 7ª menor = 7; ...). Desde Split, las 13 zonas del arpa. La rueda de modulación rasguea las 13 zonas al moverla.
struct Omnichord
{
    // Aleatoriedad reproducible (Dsp/RandomClock.h): el ruido de la caja de ritmos interna
    void reseed (juce::int64 seed) { kit.reseed (seed); }

    void prepare (double sr)
    {
        sampleRate = sr;
        kit.prepare (sr);
        const auto onePole = [sr] (double hz) { return 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * hz / sr); };
        harpLpCoef = onePole (1500.0);  // estimado
        chordLpCoef = onePole (2000.0); // ajustado a 2 grabaciones (acorde mayor/menor, Freesound 723983/723985); antes 900 estimado
        bassLpCoef = onePole (350.0);   // estimado
        chordReleaseCoef = (float) std::exp (-1.0 / (0.18 * sr));
        chordDecayCoef = (float) std::exp (-1.0 / (0.333 * sr));
        chordAttackCoef = (float) std::exp (-1.0 / (0.005 * sr));
        bendCoef = 1.0f - (float) std::exp (-1.0 / (0.005 * sr));
        reset();
    }

    // PANIC (silencio total): el ritmo queda parado hasta la siguiente nota
    void stopRhythm() { rhythmStopped = true; }

    void reset()
    {
        kit.reset();
        clock.restart();
        rhythm = -2;
        numChordKeys = 0;
        for (auto& h : harp) h = {};
        chordEnv = 0.0f;
        bassEnv = 0.0;
        harpLp1 = harpLp2 = chordLp = bassLp = 0.0f;
        lastZone = -1;
    }

    // Pasos disparados desde el inicio del ritmo (-1 parado): el editor de ritmos marca el paso step % numSteps
    long long currentStep() const noexcept { return stepCount.load (std::memory_order_relaxed); }

    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, const StepClock::Transport& transport,
                 const UserRhythms::Bank* userBank)
    {
        split = (int) p.omniSplit->load();
        harpOctave = (int) p.omniHarpOctave->load();
        const auto tau = 0.366 + 2.734 * (double) p.omniSustain->load();
        const auto harpDecay = (float) std::exp (-1.0 / (tau * sampleRate));
        const auto harpLevel = p.omniHarpLevel->load(), chordLevel = p.omniChordLevel->load();
        const auto bassLevel = p.omniBassLevel->load(), rhythmLevel = p.omniRhythmLevel->load();
        const auto tune = (double) p.fineTune->load();

        if (rhythmStopped) // parado por PANIC: arranca de nuevo, desde el principio, con la siguiente nota
            for (const auto m : midi)
                if (m.getMessage().isNoteOn())
                {
                    rhythmStopped = false;
                    clock.restart();
                    break;
                }
        const auto newRhythm = (int) p.omniRhythm->load() - 1; // 0 = Off
        if (newRhythm != rhythm)
        {
            rhythm = newRhythm;
            clock.restart();
        }

        // Ritmos de fábrica y, a continuación, las ranuras User 1-8 (Engines/UserRhythms.h); ranura vacía: sin ritmo
        const auto factory = (int) omnichordPatterns().size();
        const RhythmPattern* active = rhythm < 0 ? nullptr
                                    : rhythm < factory ? &omnichordPatterns()[(size_t) rhythm]
                                    : userBank != nullptr ? userBank->pattern (rhythm - factory) : nullptr;
        rhythmRunning = active != nullptr;

        struct Hit { int sample; DrumSlot slot; char c; };
        std::array<Hit, 64> hits;
        int numHits = 0;
        if (active != nullptr && ! rhythmStopped)
        {
            const auto& pat = *active;
            clock.process (out.getNumSamples(), sampleRate, (double) p.omniTempo->load(), pat.stepsPerBeat, 0.0, transport,
                [&] (int sample, long long step)
                {
                    const auto idx = (size_t) (step % pat.numSteps);
                    for (const auto& [slot, lane] : pat.lanes)
                        if (lane[idx] != '.' && numHits < (int) hits.size())
                            hits[(size_t) numHits++] = { sample, slot, lane[idx] };
                });
        }
        stepCount.store (active != nullptr && clock.lastFired >= 0 ? clock.lastFired : -1, std::memory_order_relaxed);

        // Rasgueo con la rueda de modulación: cada zona que cruza suena una vez
        const auto zone = juce::jlimit (0, 12, (int) (p.modWheel.load() * 13.0f));
        auto midiIt = midi.cbegin();
        int hit = 0;
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
                handleMidi ((*midiIt).getMessage());

            if (i == 0)
            {
                if (lastZone >= 0 && zone != lastZone)
                    for (int z = lastZone; z != zone;)
                    {
                        z += zone > lastZone ? 1 : -1;
                        pluck (z, 0.8f);
                    }
                lastZone = zone;
            }

            for (; hit < numHits && hits[(size_t) hit].sample <= i; ++hit)
            {
                const auto& h = hits[(size_t) hit];
                if (h.slot == DrumSlot::Bass)
                {
                    if (numChordKeys > 0) // el bajo automático solo suena con un botón de acorde pulsado
                        startBass (h.c);
                }
                else if (const auto voice = slotVoice (1, h.slot); voice >= 0)
                {
                    kit.trigger ((DrumVoice) voice, stepVelocity (h.c), {});
                }
            }

            bendSmoothed += bendCoef * (p.pitchBend.load() - bendSmoothed);
            const auto pitchMul = std::exp2 ((tune + 100.0 * bendSmoothed) / 1200.0);

            // Arpa: 13 cuerdas con caída exponencial (sin release: la duración la fija Sustain)
            float harpSum = 0.0f;
            for (auto& h : harp)
                if (h.amp > 1.0e-4f)
                {
                    harpSum += h.amp * squareStep (h.phase, h.hz * pitchMul);
                    h.amp *= harpDecay;
                }
            harpLp1 += harpLpCoef * (harpSum - harpLp1);
            harpLp2 += harpLpCoef * (harpLp1 - harpLp2);

            // Voz de acordes: suena mientras hay botón pulsado
            const auto held = numChordKeys > 0;
            if (held)
            {
                chordTarget *= chordDecayCoef;
                chordTarget = std::max (chordTarget, 0.55f);
                chordEnv = chordTarget + chordAttackCoef * (chordEnv - chordTarget);
            }
            else
            {
                chordEnv *= chordReleaseCoef;
            }
            float chordSum = 0.0f;
            if (chordEnv > 1.0e-4f)
                for (int k = 0; k < 3; ++k)
                    chordSum += squareStep (chordPhase[(size_t) k], noteHz (chordNotes[(size_t) k]) * pitchMul);
            chordLp += chordLpCoef * (chordEnv * chordSum / 3.0f - chordLp);

            // Bajo: caída lineal en 1,22 s
            float bass = 0.0f;
            if (bassEnv > 0.0)
            {
                bass = (float) bassEnv * squareStep (bassPhase, noteHz (bassNote) * pitchMul);
                bassEnv -= 1.0 / (1.22 * sampleRate);
            }
            bassLp += bassLpCoef * (bass - bassLp);

            const auto y = 0.25f * (harpLevel * harpLp2 + chordLevel * chordLp + 1.6f * bassLevel * bassLp) + rhythmLevel * kit.render();
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, y);
        }
    }

private:
    struct HarpString { double phase = 0.0, hz = 0.0; float amp = 0.0f; };

    static double noteHz (int note) { return 440.0 * std::exp2 ((note - 69) / 12.0) * KoniTuning::ratio (note); }

    float squareStep (double& phase, double hz)
    {
        const auto dt = hz / sampleRate;
        phase += dt;
        if (phase >= 1.0) phase -= 1.0;
        return oscillator (square, phase, dt);
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            const auto n = m.getNoteNumber();
            if (n < split)
            {
                if (numChordKeys < (int) chordKeys.size())
                    chordKeys[(size_t) numChordKeys++] = n;
                updateChord (true);
            }
            else
            {
                pluck ((n - split) % 13, m.getFloatVelocity());
            }
        }
        else if (m.isNoteOff())
        {
            const auto n = m.getNoteNumber();
            if (n < split)
            {
                for (int j = 0; j < numChordKeys; ++j)
                    if (chordKeys[(size_t) j] == n)
                    {
                        chordKeys[(size_t) j] = chordKeys[(size_t) --numChordKeys];
                        break;
                    }
                if (numChordKeys > 0)
                    updateChord (false);
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            numChordKeys = 0;
            if (m.isAllSoundOff())
                reset();
        }
    }

    // Reconoce el acorde de las teclas pulsadas: raíz = la más grave; tipo por los intervalos presentes
    void updateChord (bool pressed)
    {
        int low = 128;
        for (int j = 0; j < numChordKeys; ++j)
            low = std::min (low, chordKeys[(size_t) j]);
        unsigned mask = 0;
        for (int j = 0; j < numChordKeys; ++j)
            mask |= 1u << ((chordKeys[(size_t) j] - low) % 12);
        const auto has = [mask] (int iv) { return (mask >> iv) & 1u; };

        std::array<int, 3> iv { 0, 4, 7 };                          // mayor
        if (has (3) && has (6))                    iv = { 0, 3, 9 }; // dim (OM-84: 0,3,9 [EMU])
        else if (has (4) && has (8) && ! has (7))  iv = { 0, 4, 8 }; // aug
        else if (has (3) && has (10))              iv = { 0, 3, 10 }; // m7
        else if (has (11))                         iv = { 0, 4, 11 }; // maj7
        else if (has (10))                         iv = { 0, 4, 10 }; // 7 (sin quinta)
        else if (has (3))                          iv = { 0, 3, 7 };  // menor

        const auto root = low % 12;
        const auto changed = root != chordRoot || iv != chordIntervals;
        chordRoot = root;
        chordIntervals = iv;
        for (int k = 0; k < 3; ++k)
            chordNotes[(size_t) k] = 54 + ((root + iv[(size_t) k] - 6 + 24) % 12); // ventana 54..65 [EMU]
        std::sort (chordNotes.begin(), chordNotes.end());

        if (pressed && (changed || chordEnv < 0.01f))
        {
            chordTarget = 1.0f;
            if (! rhythmRunning)
                startBass ('r'); // sin ritmo: el bajo suena al pulsar el acorde
        }
    }

    void pluck (int zone, float velocity)
    {
        auto& h = harp[(size_t) zone];
        const auto note = chordNotes[(size_t) (zone % 3)] + 12 * (zone / 3) + 12 * harpOctave;
        h.hz = noteHz (note);
        h.amp = velocity;
    }

    void startBass (char c)
    {
        const auto r = 42 + ((chordRoot - 6 + 12) % 12); // raíz en 42..53 [EMU]
        switch (c)
        {
            case 'R': bassNote = r - 12; break;
            case 'f': bassNote = r + 7; break;
            case 't': bassNote = r + chordIntervals[1]; break;
            default:  bassNote = r; break;
        }
        bassEnv = 1.0;
    }

    double sampleRate = 44100.0;
    DrumKit kit;
    StepClock clock;
    int rhythm = -2, split = 60, harpOctave = 0, lastZone = -1;
    bool rhythmRunning = false;
    bool rhythmStopped = false; // PANIC: sin ritmo hasta la siguiente nota
    std::atomic<long long> stepCount { -1 };
    std::array<int, 10> chordKeys {};
    int numChordKeys = 0, chordRoot = 0, bassNote = 48;
    std::array<int, 3> chordIntervals { 0, 4, 7 }, chordNotes { 60, 64, 67 };
    std::array<HarpString, 13> harp {};
    std::array<double, 3> chordPhase {};
    double bassPhase = 0.0, bassEnv = 0.0;
    float chordEnv = 0.0f, chordTarget = 0.0f;
    float harpLpCoef = 0.0f, chordLpCoef = 0.0f, bassLpCoef = 0.0f, chordReleaseCoef = 0.0f, chordDecayCoef = 0.0f, chordAttackCoef = 0.0f;
    float harpLp1 = 0.0f, harpLp2 = 0.0f, chordLp = 0.0f, bassLp = 0.0f, bendCoef = 1.0f, bendSmoothed = 0.0f;
};
