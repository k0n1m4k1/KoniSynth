// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// OPLL: Yamaha YM2413 (Sega Master System japonesa / Mark III FM Unit, MSX-MUSIC) y Konami VRC7 (DS1001, Lagrange Point),
// con Nuked-OPLL 1.0.2 (Nuke.YKT, GPL-2.0+; Source/ThirdParty/NukedOPLL): emulación a nivel de ciclo del silicio, con las
// ROM de instrumentos volcadas de cada chip. Al integrarlo, el binario de KoniSynth es GPL-2.0-or-later.
// Rasgos del original que conserva: el DAC multiplexado (cada canal sale en su ciclo; aquí se suman los 18 ciclos de cada
// muestra, como integra la salida analógica), el bit de signo que se cuela por el DAC con el canal en silencio (YM2413), la
// percusión que sale dos veces por la salida de ritmo (más fuerte) y el VRC7 de 6 canales sin percusión.
// Driver de KoniSynth: escribe los registros por una cola con los tiempos del chip (dirección, 4 ciclos, dato y 32 ciclos
// hasta la siguiente: 2 muestras por registro, 40 µs), como haría la CPU del juego.
extern "C"
{
#include "../ThirdParty/NukedOPLL/opll.h"
}

struct OpllEngine
{
    static constexpr double kChipRate = 3579545.0 / 72.0; // 49 716 Hz

    void prepare (double sr)
    {
        resampler.prepare (kChipRate, sr);
        dcCoef = std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sr);
        reset();
    }

    void reset()
    {
        OPLL_Reset (&chip, vrc7 ? opll_type_ds1001 : opll_type_ym2413);
        for (auto& v : voices) v = {};
        queueRead = queueWrite = 0;
        writeStage = 0;
        userPatch.fill (-1);
        lastRhythm = -1;
        drumKeys = 0;
        age = 0;
        dcIn = dcOut = 0.0;
        dcPrimed = false;
        resampler.reset();
    }

    // vrc7Model: false = YM2413, true = VRC7. Reinicia el chip si cambia
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p, bool vrc7Model)
    {
        if (vrc7Model != vrc7) { vrc7 = vrc7Model; reset(); }
        readParams (p);
        bendSmoothed += 0.2f * (p.pitchBend.load() - bendSmoothed);
        tuneCents = (double) p.fineTune->load() + 100.0 * bendSmoothed;
        for (int k = 0; k < numVoices(); ++k)
            if (voices[(size_t) k].note >= 0) writePitch (k, false);

        auto it = midi.cbegin();
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
                handleMidi ((*it).getMessage());
            const auto lr = resampler.nextStereo ([this] { const auto s = chipSample(); return std::array<float, 2> { s, s }; });
            // Bloqueo de continua (5 Hz): el bit de signo que se cuela por el DAC deja un offset, como en la salida analógica
            if (! dcPrimed) { dcIn = lr[0]; dcPrimed = true; } // el offset en reposo no produce un transitorio al arrancar
            const auto y = lr[0] - dcIn + dcCoef * dcOut;
            dcIn = lr[0];
            dcOut = y;
            for (int c = 0; c < out.getNumChannels(); ++c) out.addSample (c, i, (float) y);
        }
    }

    // Instrumentos de la ROM del YM2413 (nombres del datasheet de Yamaha). Los del VRC7 no tienen nombres oficiales
    static const char* ym2413Name (int i)
    {
        static const char* n[16] = { "User", "Violin", "Guitar", "Piano", "Flute", "Clarinet", "Oboe", "Trumpet", "Organ", "Horn",
                                     "Synthesizer", "Harpsichord", "Vibraphone", "Synthesizer Bass", "Acoustic Bass", "Electric Guitar" };
        return n[juce::jlimit (0, 15, i)];
    }

private:
    struct Voice { int note = -1, fnum = 0, block = 0, vol = 0; bool gate = false; long long age = 0; };

    int numVoices() const { return vrc7 ? 6 : rhythm ? 6 : 9; }

    void readParams (const Params& p)
    {
        instrument = juce::jlimit (0, 15, (int) p.opllInstrument->load());
        baseVolume = juce::jlimit (0, 15, (int) p.opllVolume->load());
        sustain = p.opllSustain->load() > 0.5f;
        velocityOn = p.oplVelocity->load() > 0.5f;
        const auto wantRhythm = ! vrc7 && p.opllRhythm->load() > 0.5f;
        if (wantRhythm != rhythm)
        {
            for (int k = 6; k < 9; ++k) { keyOff (k); voices[(size_t) k] = {}; }
            rhythm = wantRhythm;
        }
        if (! vrc7 && (int) rhythm != lastRhythm)
        {
            lastRhythm = rhythm;
            if (rhythm)
            {
                // Frecuencias y volúmenes de percusión de los canales 7-9 (valores habituales de los drivers MSX/SMS)
                for (const auto& [r, v] : { std::pair<int, int> { 0x16, 0x20 }, { 0x17, 0x50 }, { 0x18, 0xc0 }, { 0x26, 0x05 },
                                            { 0x27, 0x05 }, { 0x28, 0x01 }, { 0x36, 0x00 }, { 0x37, 0x00 }, { 0x38, 0x00 } })
                    queue (r, v);
            }
            drumKeys = 0;
            queue (0x0e, rhythm ? 0x20 : 0x00);
        }
        // Instrumento de usuario (registros 00h-07h) con los mandos de OP1 (modulador) y OP2 (portadora) del motor OPL
        std::array<int, 8> reg {};
        for (int k = 0; k < 2; ++k)
        {
            const auto& o = p.oplOp[(size_t) k];
            reg[(size_t) k] = (o.am->load() > 0.5f ? 0x80 : 0) | (o.vib->load() > 0.5f ? 0x40 : 0) | (o.egt->load() > 0.5f ? 0x20 : 0)
                              | (o.ksr->load() > 0.5f ? 0x10 : 0) | ((int) o.mult->load() & 15);
            reg[(size_t) (4 + k)] = (((int) o.ar->load() & 15) << 4) | ((int) o.dr->load() & 15);
            reg[(size_t) (6 + k)] = (((int) o.sl->load() & 15) << 4) | ((int) o.rr->load() & 15);
        }
        const auto& m = p.oplOp[0];
        const auto& c = p.oplOp[1];
        reg[2] = (((int) m.ksl->load() & 3) << 6) | ((int) m.tl->load() & 63);
        reg[3] = (((int) c.ksl->load() & 3) << 6) | (((int) c.wave->load() & 1) << 4) | (((int) m.wave->load() & 1) << 3)
                 | ((int) p.oplFeedback->load() & 7);
        for (int r = 0; r < 8; ++r)
            if (reg[(size_t) r] != userPatch[(size_t) r]) { userPatch[(size_t) r] = reg[(size_t) r]; queue (r, reg[(size_t) r]); }
        // Instrumento y volumen de las voces que suenan
        for (int k = 0; k < numVoices(); ++k)
            if (voices[(size_t) k].note >= 0 && (instrument != shownInstrument || baseVolume != shownVolume))
                queue (0x30 + k, (instrument << 4) | voiceVolume (voices[(size_t) k]));
        shownInstrument = instrument;
        shownVolume = baseVolume;
    }

    int voiceVolume (const Voice& v) const { return juce::jlimit (0, 15, baseVolume + v.vol); }

    // fnum de 9 bits y bloque: f = fnum · fs · 2^(bloque - 19), bloque mínimo con fnum < 512
    void writePitch (int k, bool force)
    {
        auto& v = voices[(size_t) k];
        const auto f = 440.0 * std::exp2 ((v.note - 69) / 12.0 + tuneCents / 1200.0) * KoniTuning::ratio (v.note);
        int block = 0;
        double fn = f * 524288.0 / kChipRate;
        while (fn >= 511.5 && block < 7) { fn *= 0.5; ++block; }
        const auto fnum = juce::jlimit (0, 511, (int) std::lround (fn));
        if (! force && fnum == v.fnum && block == v.block) return;
        v.fnum = fnum; v.block = block;
        queue (0x10 + k, fnum & 0xff);
        queue (0x20 + k, (sustain ? 0x20 : 0) | (v.gate ? 0x10 : 0) | (block << 1) | (fnum >> 8));
    }

    void keyOff (int k)
    {
        auto& v = voices[(size_t) k];
        v.gate = false;
        queue (0x20 + k, (sustain ? 0x20 : 0) | (v.block << 1) | (v.fnum >> 8));
    }

    // Percusión del YM2413 (canal MIDI 10, mapa GM): bombo, caja, tom, platillo, charles
    static int drumBit (int note)
    {
        switch (note)
        {
            case 35: case 36: return 0x10;                                   // BD
            case 38: case 40: case 37: case 39: return 0x08;                 // SD
            case 41: case 43: case 45: case 47: case 48: case 50: return 0x04; // TOM
            case 49: case 51: case 52: case 55: case 57: case 59: return 0x02; // CYM
            case 42: case 44: case 46: return 0x01;                          // HH
            default: return 0;
        }
    }

    void handleMidi (const juce::MidiMessage& m)
    {
        if (rhythm && m.getChannel() == 10 && (m.isNoteOn() || m.isNoteOff()))
        {
            const auto bit = drumBit (m.getNoteNumber());
            if (bit == 0) return;
            if (m.isNoteOn())
            {
                drumKeys &= ~bit;
                queue (0x0e, 0x20 | drumKeys); // flanco: apagar y volver a encender
                drumKeys |= bit;
                queue (0x0e, 0x20 | drumKeys);
            }
            else
            {
                drumKeys &= ~bit;
                queue (0x0e, 0x20 | drumKeys);
            }
            return;
        }
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
            if (v.gate) keyOff (target); // redisparo: el chip necesita ver el key off
            v.note = m.getNoteNumber();
            v.age = ++age;
            // Velocidad (driver): volumen del canal, pasos de 3 dB
            v.vol = velocityOn ? juce::jlimit (0, 15, (int) std::lround (-20.0 * std::log10 (std::max (0.01f, m.getFloatVelocity())) / 3.0)) : 0;
            queue (0x30 + target, (instrument << 4) | voiceVolume (v));
            v.gate = true;
            writePitch (target, true);
        }
        else if (m.isNoteOff())
        {
            for (int k = 0; k < n; ++k)
                if (voices[(size_t) k].note == m.getNoteNumber() && voices[(size_t) k].gate) keyOff (k);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (int k = 0; k < n; ++k) if (voices[(size_t) k].gate) keyOff (k);
            if (m.isAllSoundOff()) { reset(); }
        }
    }

    void queue (int reg, int value)
    {
        const auto next = (queueWrite + 1) % (int) writes.size();
        if (next == queueRead) return; // cola llena (no ocurre en uso normal: 512 registros)
        writes[(size_t) queueWrite] = { (uint8_t) reg, (uint8_t) value };
        queueWrite = next;
    }

    // Una muestra: 18 ciclos del chip; una escritura de registro cada 2 muestras (dirección en el ciclo 0, dato en el 4)
    float chipSample()
    {
        int32_t sum = 0;
        for (int c = 0; c < 18; ++c)
        {
            if (writeStage == 0 && c == 0 && queueRead != queueWrite)
            {
                OPLL_Write (&chip, 0, writes[(size_t) queueRead].reg);
                writeStage = 1;
            }
            else if (writeStage == 1 && c == 4)
            {
                OPLL_Write (&chip, 1, writes[(size_t) queueRead].value);
                queueRead = (queueRead + 1) % (int) writes.size();
                writeStage = 2;
            }
            int32_t buf[2];
            OPLL_Clock (&chip, buf);
            sum += buf[0] + buf[1];
        }
        if (writeStage == 2) writeStage = 3;      // una muestra entera de espera tras el dato
        else if (writeStage == 3) writeStage = 0;
        return (float) sum * kGain;
    }

    struct Write { uint8_t reg = 0, value = 0; };
    static constexpr float kGain = 1.0f / 600.0f; // escala medida: una voz del YM2413 con pico ≈ 0,24 (scratchpad/opll_check.py)

    opll_t chip {};
    SincResampler resampler;
    std::array<Voice, 9> voices {};
    std::array<Write, 512> writes {};
    std::array<int, 8> userPatch {};
    int queueRead = 0, queueWrite = 0, writeStage = 0, instrument = 0, baseVolume = 0, drumKeys = 0, lastRhythm = -1;
    int shownInstrument = -1, shownVolume = -1;
    bool vrc7 = false, rhythm = false, sustain = false, velocityOn = true, dcPrimed = false;
    double tuneCents = 0.0, dcIn = 0.0, dcOut = 0.0, dcCoef = 0.0;
    float bendSmoothed = 0.0f;
    long long age = 0;
};
