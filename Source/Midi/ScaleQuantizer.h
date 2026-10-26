// Escala de las notas de entrada (pie del teclado): Transpose en semitonos y, con Mode Round, la nota fuera de la escala va
// a una de la escala (Rounding: Nearest, con empate hacia abajo; Down; Up); con Filter, no suena. Va tras el teclado en
// pantalla y la grabación (REC guarda lo tocado tal cual) y antes de Scala/MTS-ESP, el secuenciador y el arpegiador.
// Cada nota de entrada recuerda la nota que emitió (por canal: MPE): su note-off y su aftertouch van a esa nota aunque los
// ajustes cambien con la tecla pulsada. Dos teclas que caen en la misma nota (Round) la suenan una vez: note-on con la
// primera, note-off con la última que se suelta (contador por nota de salida). Sin asignaciones en el audio: el búfer de
// salida se reserva en prepare(). Se incluye desde KoniSynth.cpp (mismo TU).
#pragma once

struct ScaleQuantizer
{
    enum Mode { off = 0, round, filter };
    enum Rounding { nearest = 0, down, up };

    // Grados de cada escala (bit k = k semitonos sobre la raíz), en el orden de scaleType (Params/Layout.h). Intervalos de la
    // teoría estándar: mayor 0 2 4 5 7 9 11; menor natural 0 2 3 5 7 8 10; menor armónica 0 2 3 5 7 8 11; menor melódica
    // (ascendente) 0 2 3 5 7 9 11; dórico 0 2 3 5 7 9 10; frigio 0 1 3 5 7 8 10; lidio 0 2 4 6 7 9 11; mixolidio 0 2 4 5 7 9 10;
    // locrio 0 1 3 5 6 8 10; pentatónica mayor 0 2 4 7 9; menor 0 3 5 7 10; blues 0 3 5 6 7 10; tonos enteros 0 2 4 6 8 10;
    // frigio dominante 0 1 4 5 7 8 10
    static juce::uint16 mask (int scale)
    {
        const auto bits = [] (std::initializer_list<int> degrees) { juce::uint16 m = 0; for (auto d : degrees) m |= (juce::uint16) (1u << d); return m; };
        static const juce::uint16 masks[] = {
            bits ({ 0, 2, 4, 5, 7, 9, 11 }), bits ({ 0, 2, 3, 5, 7, 8, 10 }), bits ({ 0, 2, 3, 5, 7, 8, 11 }), bits ({ 0, 2, 3, 5, 7, 9, 11 }),
            bits ({ 0, 2, 3, 5, 7, 9, 10 }), bits ({ 0, 1, 3, 5, 7, 8, 10 }), bits ({ 0, 2, 4, 6, 7, 9, 11 }), bits ({ 0, 2, 4, 5, 7, 9, 10 }),
            bits ({ 0, 1, 3, 5, 6, 8, 10 }), bits ({ 0, 2, 4, 7, 9 }), bits ({ 0, 3, 5, 7, 10 }), bits ({ 0, 3, 5, 6, 7, 10 }),
            bits ({ 0, 2, 4, 6, 8, 10 }), bits ({ 0, 1, 4, 5, 7, 8, 10 }) };
        return masks[juce::jlimit (0, (int) std::size (masks) - 1, scale)];
    }

    static bool inScale (int note, int root, juce::uint16 m) { return ((m >> (((note - root) % 12 + 12) % 12)) & 1u) != 0; }

    // Nota de la escala para 'note' (0..127; la escala tiene grados en cada octava, así que hay una a menos de 12 semitonos)
    static int quantize (int note, int root, juce::uint16 m, int rounding)
    {
        if (inScale (note, root, m)) return note;
        int below = -1, above = -1;
        for (int d = 1; d < 12 && below < 0; ++d) if (note - d >= 0 && inScale (note - d, root, m)) below = note - d;
        for (int d = 1; d < 12 && above < 0; ++d) if (note + d <= 127 && inScale (note + d, root, m)) above = note + d;
        if (below < 0) return above;
        if (above < 0) return below;
        if (rounding == down) return below;
        if (rounding == up) return above;
        return above - note < note - below ? above : below; // empate: abajo
    }

    ScaleQuantizer() { reset(); }
    void prepare() { out.ensureSize (8192); reset(); }

    void reset()
    {
        for (auto& ch : held) ch.fill (-1);
        for (auto& ch : count) ch.fill (0);
    }

    // bypass: el motor usa la nota como sonido (Drum Machine, parte de ritmo del MT-32): sin transponer ni escala
    void process (juce::MidiBuffer& midi, int transpose, int mode, int root, int scale, int rounding, bool bypass)
    {
        if (midi.isEmpty()) return;
        out.clear();
        const auto m = mask (scale);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const auto pos = meta.samplePosition;
            const auto ch = juce::jlimit (1, 16, msg.getChannel()) - 1;
            if (msg.isNoteOn())
            {
                const auto in = msg.getNoteNumber();
                if (held[(size_t) ch][(size_t) in] >= 0) // note-on repetido sin note-off: se suelta la anterior
                    release (ch, in, pos);
                auto note = bypass ? in : in + transpose;
                if (note < 0 || note > 127) continue;
                if (! bypass && mode != off)
                {
                    if (mode == filter && ! inScale (note, root, m)) continue;
                    note = quantize (note, root, m, rounding);
                    if (note < 0) continue;
                }
                held[(size_t) ch][(size_t) in] = (juce::int16) note;
                if (count[(size_t) ch][(size_t) note]++ == 0)
                    out.addEvent (juce::MidiMessage::noteOn (ch + 1, note, msg.getVelocity()), pos);
            }
            else if (msg.isNoteOff())
            {
                release (ch, msg.getNoteNumber(), pos, msg.getVelocity());
            }
            else if (msg.isAftertouch())
            {
                if (const auto note = held[(size_t) ch][(size_t) msg.getNoteNumber()]; note >= 0)
                    out.addEvent (juce::MidiMessage::aftertouchChange (ch + 1, note, msg.getAfterTouchValue()), pos);
            }
            else
            {
                if (msg.isAllNotesOff() || msg.isAllSoundOff())
                {
                    held[(size_t) ch].fill (-1);
                    count[(size_t) ch].fill (0);
                }
                out.addEvent (msg, pos);
            }
        }
        midi.swapWith (out);
    }

private:
    void release (int ch, int in, int pos, juce::uint8 velocity = 0)
    {
        const auto note = held[(size_t) ch][(size_t) in];
        if (note < 0) return; // filtrada o nunca vista: no sonaba
        held[(size_t) ch][(size_t) in] = -1;
        if (--count[(size_t) ch][(size_t) note] <= 0)
        {
            count[(size_t) ch][(size_t) note] = 0;
            out.addEvent (juce::MidiMessage::noteOff (ch + 1, note, velocity), pos);
        }
    }

    std::array<std::array<juce::int16, 128>, 16> held {};  // nota emitida por cada nota de entrada (-1: no suena)
    std::array<std::array<juce::uint8, 128>, 16> count {};  // teclas que sostienen cada nota de salida
    juce::MidiBuffer out;
};
