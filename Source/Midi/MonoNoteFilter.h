// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Modo Mono / Legato del motor Synth: convierte el MIDI en una línea monofónica con prioridad a la última nota.
// Pulsar con otra tecla pulsada -> note-on de la nueva (el sintetizador de una voz la roba; Legato no reinicia
// envolventes). Soltar la nota que suena -> vuelve a la anterior aún pulsada (note-on) o note-off si no queda ninguna.
// Soltar una tecla que no suena -> se quita de la pila, sin mensaje. All Notes Off (CC 123) y All Sound Off (CC 120) vacían
// la pila y pasan: sin eso, soltar después una tecla que seguía en la pila volvía a disparar la anterior. Sin asignaciones
// (pila fija, buffer reservado).
struct MonoNoteFilter
{
    void prepare() { out.ensureSize (1024); reset(); }
    void reset() { count = 0; }

    void process (juce::MidiBuffer& midi)
    {
        out.clear();
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
            {
                remove (msg.getNoteNumber());
                if (count < (int) stack.size())
                    stack[(size_t) count++] = { msg.getNoteNumber(), msg.getVelocity() };
                out.addEvent (msg, m.samplePosition);
            }
            else if (msg.isNoteOff())
            {
                const auto wasTop = count > 0 && stack[(size_t) count - 1].note == msg.getNoteNumber();
                remove (msg.getNoteNumber());
                if (! wasTop)
                    continue;
                if (count > 0)
                    out.addEvent (juce::MidiMessage::noteOn (msg.getChannel(), stack[(size_t) count - 1].note,
                                                             stack[(size_t) count - 1].velocity), m.samplePosition);
                else
                    out.addEvent (msg, m.samplePosition);
            }
            else
            {
                if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    count = 0;
                out.addEvent (msg, m.samplePosition);
            }
        }
        midi.swapWith (out);
    }

private:
    struct Held { int note; juce::uint8 velocity; };

    void remove (int note)
    {
        int j = 0;
        for (int k = 0; k < count; ++k)
            if (stack[(size_t) k].note != note)
                stack[(size_t) j++] = stack[(size_t) k];
        count = j;
    }

    std::array<Held, 32> stack {};
    int count = 0;
    juce::MidiBuffer out;
};
