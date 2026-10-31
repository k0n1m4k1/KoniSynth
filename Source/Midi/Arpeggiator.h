// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Arpegiador: procesador MIDI antes del motor (Synth y VL-1; el theremin no usa MIDI de notas).
// Sustituye las notas entrantes por el arpegio; el resto de mensajes pasa igual. Modos Up, Down, Up/Down (sin repetir
// extremos), Random y As Played; 1–4 octavas; Gate = fracción del paso con la nota sonando; Latch mantiene el último
// acorde al soltar (se sustituye al pulsar tras soltar todo).
// Reloj: con el host en reproducción, rejilla del host (ppq): paso k = floor(ppq / beatsPorPaso); sin reproducción o sin
// tempo del host, reloj interno a Arp BPM (o al tempo del host si lo da) que arranca con la primera nota.
// Tiempo real: sin asignaciones en process (MidiBuffer de salida reservado en prepare, notas en arrays fijos).
struct Arpeggiator
{
    // Aleatoriedad reproducible (Dsp/RandomClock.h): el procesador la resiembra en cada anclaje del transporte
    void reseed (juce::int64 seed) { rng.setSeed (seed); }

    static constexpr int maxNotes = 32;

    void prepare (double sr)
    {
        sampleRate = sr;
        out.ensureSize (2048);
        reset();
    }

    void reset()
    {
        numHeld = numLatched = 0;
        sounding = -1;
        offCountdown = -1;
        stepCounter = 0.0;
        stepIndex = 0;
        lastGridStep = -1;
        wasOn = false;
    }

    struct HostTime { std::optional<double> bpm, ppq; bool playing = false; };

    void process (juce::MidiBuffer& midi, int numSamples, const Params& p, const HostTime& host)
    {
        const auto on = p.arpOn->load() > 0.5f;
        out.clear();

        if (! on)
        {
            if (wasOn)
                stopSounding (0); // al apagar: suelta la nota del arpegio
            wasOn = false;
            for (const auto m : midi) // registra pulsaciones aunque esté apagado (encenderlo con teclas pulsadas)
                track (m.getMessage());
            if (out.isEmpty())
                return;
            for (const auto m : midi)
                out.addEvent (m.getMessage(), m.samplePosition);
            midi.swapWith (out);
            return;
        }

        if (! wasOn) // al encender: corta las notas que sonaban por paso directo; el arpegio las toma
        {
            for (int k = 0; k < numHeld; ++k)
                out.addEvent (juce::MidiMessage::noteOff (1, held[(size_t) k].note), 0);
            stepCounter = 0.0;
            lastGridStep = -1;
        }
        wasOn = true;

        static constexpr double rateBeats[6] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        const auto stepBeats = rateBeats[juce::jlimit (0, 5, (int) p.arpRate->load())];
        const auto bpm = host.bpm.value_or ((double) p.arpBpm->load());
        const auto samplesPerStep = juce::jmax (1.0, 60.0 / bpm * stepBeats * sampleRate);
        const auto gateSamples = juce::jmax (1, (int) (p.arpGate->load() * samplesPerStep));
        const auto sync = host.playing && host.ppq.has_value() && host.bpm.has_value();
        const auto latch = p.arpLatch->load() > 0.5f;
        const auto mode = (int) p.arpMode->load();
        const auto octaves = juce::jlimit (1, 4, (int) p.arpOctaves->load());
        const auto oneShot = p.arpOneShot->load() > 0.5f;

        auto it = midi.cbegin();
        for (int i = 0; i < numSamples; ++i)
        {
            for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
            {
                const auto m = (*it).getMessage();
                if (m.isNoteOnOrOff())
                {
                    const auto hadNotes = activeCount (latch) > 0;
                    track (m, latch);
                    if (! hadNotes && activeCount (latch) > 0 && ! sync)
                        stepCounter = 0.0; // primera nota: el arpegio empieza ya
                    if (m.isNoteOn() && oneShot)
                    {
                        stepIndex = 0;     // One Shot: cada pulsación reinicia el patrón
                        stepCounter = 0.0;
                    }
                }
                else
                {
                    if (m.isAllNotesOff() || m.isAllSoundOff()) // CC 123 / 120: vacía lo pulsado y lo retenido (Latch)
                    {
                        track (m);
                        stepIndex = 0;
                        stopSounding (i);
                    }
                    out.addEvent (m, i);
                }
            }

            if (offCountdown >= 0 && --offCountdown < 0)
                stopSounding (i);

            bool trigger = false;
            if (sync)
            {
                const auto ppq = *host.ppq + (double) i * *host.bpm / (60.0 * sampleRate);
                const auto gridStep = (juce::int64) std::floor (ppq / stepBeats + 1.0e-9);
                trigger = gridStep != lastGridStep;
                lastGridStep = gridStep;
            }
            else
            {
                stepCounter -= 1.0;
                if (stepCounter <= 0.0)
                {
                    stepCounter += samplesPerStep;
                    trigger = true;
                }
            }

            if (trigger)
            {
                const auto n = activeCount (latch);
                if (n == 0)
                {
                    stepIndex = 0;
                    continue;
                }
                // One Shot: el patrón completo son n·octavas pasos (Up/Down: 2·total - 2); después, silencio hasta otra pulsación
                const auto total = n * octaves;
                if (oneShot && stepIndex >= (mode == 2 ? juce::jmax (1, 2 * total - 2) : total))
                    continue;
                stopSounding (i);
                const auto [note, velocity] = noteForStep (mode, octaves, latch);
                sounding = note;
                out.addEvent (juce::MidiMessage::noteOn (1, note, velocity), i);
                offCountdown = gateSamples;
            }
        }

        midi.swapWith (out);
    }

private:
    struct Held { int note; juce::uint8 velocity; };

    int activeCount (bool latch) const { return latch ? numLatched : numHeld; }

    void track (const juce::MidiMessage& m, bool latch = false)
    {
        if (m.isNoteOn())
        {
            if (latch && numHeld == 0)
                numLatched = 0; // nuevo acorde tras soltar todo: sustituye al retenido
            add (held, numHeld, m);
            add (latched, numLatched, m);
        }
        else if (m.isNoteOff())
        {
            remove (held, numHeld, m.getNoteNumber());
            if (! latch)
                remove (latched, numLatched, m.getNoteNumber());
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            numHeld = 0;
            numLatched = 0;
        }
    }

    static void add (std::array<Held, maxNotes>& list, int& count, const juce::MidiMessage& m)
    {
        remove (list, count, m.getNoteNumber());
        if (count < maxNotes)
            list[(size_t) count++] = { m.getNoteNumber(), m.getVelocity() };
    }

    static void remove (std::array<Held, maxNotes>& list, int& count, int note)
    {
        int j = 0;
        for (int k = 0; k < count; ++k)
            if (list[(size_t) k].note != note)
                list[(size_t) j++] = list[(size_t) k];
        count = j;
    }

    std::pair<int, juce::uint8> noteForStep (int mode, int octaves, bool latch)
    {
        const auto& src = latch ? latched : held;
        const auto n = activeCount (latch);

        std::array<Held, maxNotes> order {};
        std::copy_n (src.begin(), n, order.begin());
        if (mode != 4) // As Played conserva el orden de pulsación
            std::sort (order.begin(), order.begin() + n, [] (const Held& a, const Held& b) { return a.note < b.note; });

        const auto total = n * octaves; // nota k-ésima del patrón ascendente: order[k % n] + 12·(k / n)
        int k = 0;
        switch (mode)
        {
            case 1: k = total - 1 - (stepIndex % total); break;                       // Down
            case 2:                                                                    // Up/Down
            {
                const auto cycle = juce::jmax (1, 2 * total - 2);
                const auto pos = stepIndex % cycle;
                k = pos < total ? pos : cycle - pos;
                break;
            }
            case 3: k = rng.nextInt (total); break;                                   // Random
            default: k = stepIndex % total; break;                                    // Up, As Played
        }
        ++stepIndex;
        const auto& h = order[(size_t) (k % n)];
        return { juce::jmin (127, h.note + 12 * (k / n)), h.velocity };
    }

    void stopSounding (int sample)
    {
        if (sounding >= 0)
            out.addEvent (juce::MidiMessage::noteOff (1, sounding), sample);
        sounding = -1;
        offCountdown = -1;
    }

    double sampleRate = 44100.0, stepCounter = 0.0;
    std::array<Held, maxNotes> held {}, latched {};
    int numHeld = 0, numLatched = 0, sounding = -1, offCountdown = -1, stepIndex = 0;
    juce::int64 lastGridStep = -1;
    bool wasOn = false;
    juce::MidiBuffer out;
    juce::Random rng;
};
