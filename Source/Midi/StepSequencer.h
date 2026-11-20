// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Secuenciador por pasos: procesador MIDI antes del motor (como el arpegiador, al que sustituye si ambos están activos).
// Transposición: la tecla pulsada más reciente es la raíz; paso k suena en raíz + stepNote[k]. Reloj: rejilla del host (ppq) si
// reproduce, si no reloj interno a Arp BPM (o al tempo del host) que arranca con la primera tecla. Paso con Tie: no se suelta;
// la nota del paso siguiente llega antes del note-off (legato). Sin asignaciones en process.
struct StepSequencer
{
    void prepare (double sr) { sampleRate = sr; out.ensureSize (2048); reset(); }
    void reset() { numHeld = 0; root = -1; sounding = -1; offCountdown = -1; step = 0; stepCounter = 0.0; lastGridStep = -1; wasOn = false; uiStep = -1; }

    // Teclas pulsadas con el secuenciador apagado (nota, velocidad, canal): las suelta y las toma al encenderlo
    struct OffHeld { int note; juce::uint8 velocity; int channel; };
    std::array<OffHeld, 16> offHeld {};
    int numOffHeld = 0;
    void trackOff (const juce::MidiMessage& m)
    {
        if (m.isNoteOn() || m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff())
        {
            int j = 0; // (quita la nota, o todas con CC 123 / 120)
            for (int k = 0; k < numOffHeld; ++k)
                if (! (m.isAllNotesOff() || m.isAllSoundOff()) && offHeld[(size_t) k].note != m.getNoteNumber())
                    offHeld[(size_t) j++] = offHeld[(size_t) k];
            numOffHeld = j;
        }
        if (m.isNoteOn() && numOffHeld < (int) offHeld.size())
            offHeld[(size_t) numOffHeld++] = { m.getNoteNumber(), m.getVelocity(), juce::jlimit (1, 16, m.getChannel()) };
    }

    void process (juce::MidiBuffer& midi, int numSamples, const Params& p, const Arpeggiator::HostTime& host)
    {
        if (p.seqOn->load() < 0.5f)
        {
            if (wasOn)
            {
                out.clear();
                if (sounding >= 0)
                    out.addEvent (juce::MidiMessage::noteOff (1, sounding), 0);
                for (const auto m : midi)
                    out.addEvent (m.data, m.numBytes, m.samplePosition); // (sin copiar a un MidiMessage: SysEx)
                midi.swapWith (out);
                reset();
            }
            // Apagado: registra las teclas pulsadas (como el arpegiador), para encenderlo con teclas abajo
            for (const auto m : midi)
                if (m.numBytes <= 3)
                    trackOff (m.getMessage());
            return;
        }
        out.clear();
        if (! wasOn)
        {
            stepCounter = 0.0;
            lastGridStep = -1;
            // Al encender: corta las notas que sonaban por paso directo (en su canal) y el patrón toma la última tecla; sin
            // esto, sus note-off se los quedaba el secuenciador y la voz seguía sonando hasta PANIC
            for (int k = 0; k < numOffHeld; ++k)
                out.addEvent (juce::MidiMessage::noteOff (offHeld[(size_t) k].channel, offHeld[(size_t) k].note), 0);
            if (numOffHeld > 0)
            {
                numHeld = numOffHeld;
                root = offHeld[(size_t) numOffHeld - 1].note;
                velocity = offHeld[(size_t) numOffHeld - 1].velocity;
                step = 0;
            }
            numOffHeld = 0;
        }
        wasOn = true;

        static constexpr double rateBeats[6] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        const auto stepBeats = rateBeats[juce::jlimit (0, 5, (int) p.seqRate->load())];
        const auto bpm = host.bpm.value_or ((double) p.arpBpm->load());
        const auto samplesPerStep = juce::jmax (1.0, 60.0 / bpm * stepBeats * sampleRate);
        const auto gateSamples = juce::jmax (1, (int) (p.seqGate->load() * samplesPerStep));
        const auto sync = host.playing && host.ppq.has_value() && host.bpm.has_value();
        const auto latch = p.seqLatch->load() > 0.5f;
        const auto steps = juce::jlimit (1, 16, (int) p.seqSteps->load());

        auto it = midi.cbegin();
        for (int i = 0; i < numSamples; ++i)
        {
            for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
            {
                if ((*it).numBytes > 3)
                {
                    out.addEvent ((*it).data, (*it).numBytes, i); // SysEx: pasa sin copiarlo
                    continue;
                }
                const auto m = (*it).getMessage();
                if (m.isNoteOn())
                {
                    if (numHeld == 0 && (root < 0 || ! latch))
                    {
                        step = 0;            // primera tecla: el patrón empieza ya
                        if (! sync) stepCounter = 0.0;
                    }
                    root = m.getNoteNumber();
                    velocity = m.getVelocity();
                    ++numHeld;
                }
                else if (m.isNoteOff())
                {
                    numHeld = juce::jmax (0, numHeld - 1);
                    if (numHeld == 0 && ! latch)
                    {
                        root = -1;
                        uiStep = -1;
                        stopSounding (i);
                    }
                }
                else
                {
                    if (m.isAllNotesOff() || m.isAllSoundOff()) // CC 123 / 120: suelta la raíz, también la retenida (Latch)
                    {
                        numHeld = 0;
                        root = -1;
                        uiStep = -1;
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
                if (trigger && lastGridStep >= 0 && root >= 0)
                    step = (int) stepMod (gridStep, steps); // en sync el paso sigue a la posición del host (también negativa)
                lastGridStep = gridStep;
            }
            else if ((stepCounter -= 1.0) <= 0.0)
            {
                stepCounter += samplesPerStep;
                trigger = true;
            }

            if (trigger && root >= 0)
            {
                const auto k = step % steps;
                uiStep = k;
                step = (k + 1) % steps;
                const auto previous = sounding;
                const auto previousTied = previous >= 0 && tieHeld;
                if (p.stepOn[(size_t) k]->load() > 0.5f)
                {
                    const auto note = juce::jlimit (0, 127, root + (int) p.stepNote[(size_t) k]->load());
                    if (! (previousTied && previous == note)) // misma nota tras un paso ligado: se prolonga sin nuevo ataque
                    {
                        out.addEvent (juce::MidiMessage::noteOn (1, note, velocity), i);
                        if (previous >= 0 && previous != note)
                            out.addEvent (juce::MidiMessage::noteOff (1, previous), i); // tras el note-on: legato si estaba ligada
                    }
                    sounding = note;
                    tieHeld = p.stepTie[(size_t) k]->load() > 0.5f;
                    offCountdown = tieHeld ? -1 : gateSamples; // ligado: suena hasta el paso siguiente
                }
                else
                {
                    stopSounding (i); // silencio
                }
            }
        }
        midi.swapWith (out);
    }

private:
    void stopSounding (int sample)
    {
        if (sounding >= 0)
            out.addEvent (juce::MidiMessage::noteOff (1, sounding), sample);
        sounding = -1;
        offCountdown = -1;
        tieHeld = false;
    }

    double sampleRate = 44100.0, stepCounter = 0.0;
    int numHeld = 0, root = -1, sounding = -1, offCountdown = -1, step = 0;
    juce::uint8 velocity = 100;
    juce::int64 lastGridStep = -1;
    bool wasOn = false, tieHeld = false;

public:
    std::atomic<int> uiStep { -1 }; // paso que suena (UI); -1 = parado
private:
    juce::MidiBuffer out;
};
