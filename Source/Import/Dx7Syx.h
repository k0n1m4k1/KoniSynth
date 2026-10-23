// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// Volcados SysEx del Yamaha DX7 (DX7 Operating Manual, "MIDI Data Format"): banco de 32 voces (F0 43 0n 09 20 00, 4096 bytes
// en formato empaquetado VMEM de 128 bytes por voz, checksum, F7 = 4104 bytes; también el bloque de 4096 bytes sin
// cabecera) o voz única (F0 43 0n 00 01 1B, 155 bytes en formato VCED, checksum, F7 = 163 bytes). Las voces se pasan a
// VCED y de ahí a los parámetros del motor FM 6-Op (que usa los mismos rangos del DX7).
struct Dx7Syx
{
    struct Voice
    {
        juce::String name;
        std::array<uint8_t, 155> vced {}; // por operador (OP6 primero) 21 bytes; después los 29 de la voz
    };

    static juce::String loadVoices (const juce::File& file, std::vector<Voice>& voices)
    {
        juce::MemoryBlock block;
        if (! file.loadFileAsData (block))
            return "Can't read " + file.getFileName();
        const auto* b = static_cast<const uint8_t*> (block.getData());
        const auto size = block.getSize();
        const auto checksumOk = [b] (size_t from, size_t count, uint8_t sum)
        {
            int s = 0;
            for (size_t i = 0; i < count; ++i) s += b[from + i];
            return ((-s) & 0x7f) == sum;
        };
        voices.clear();
        if (size >= 4104 && b[0] == 0xf0 && b[1] == 0x43 && b[3] == 0x09 && b[4] == 0x20 && b[5] == 0x00)
        {
            if (! checksumOk (6, 4096, b[4102]))
                return "Bad checksum in DX7 bank " + file.getFileName() + " (the file is damaged)";
            for (int v = 0; v < 32; ++v) voices.push_back (unpack (b + 6 + v * 128));
        }
        else if (size == 4096)
        {
            for (int v = 0; v < 32; ++v) voices.push_back (unpack (b + v * 128));
        }
        else if (size >= 163 && b[0] == 0xf0 && b[1] == 0x43 && b[3] == 0x00 && b[4] == 0x01 && b[5] == 0x1b)
        {
            if (! checksumOk (6, 155, b[161]))
                return "Bad checksum in DX7 voice " + file.getFileName() + " (the file is damaged)";
            Voice voice;
            std::copy (b + 6, b + 6 + 155, voice.vced.begin());
            voice.name = nameOf (voice.vced.data() + 145);
            voices.push_back (voice);
        }
        else
        {
            return file.getFileName() + " is not a DX7 voice or 32-voice bank (SysEx 163 or 4104 bytes)";
        }
        return {};
    }

    // VMEM (128 bytes empaquetados) -> VCED (155)
    static Voice unpack (const uint8_t* m)
    {
        Voice v;
        auto& e = v.vced;
        for (int op = 0; op < 6; ++op)
        {
            const auto* s = m + op * 17;
            auto* d = e.data() + op * 21;
            for (int k = 0; k < 11; ++k) d[k] = s[k] & 0x7f;    // R1-4, L1-4, BP, LD, RD
            d[11] = s[11] & 3;          d[12] = (s[11] >> 2) & 3; // LC, RC
            d[13] = s[12] & 7;          d[20] = (s[12] >> 3) & 15; // RS, DET
            d[14] = s[13] & 3;          d[15] = (s[13] >> 2) & 7; // AMS, KVS
            d[16] = s[14] & 0x7f;                                 // OL
            d[17] = s[15] & 1;          d[18] = (s[15] >> 1) & 31; // M, FC
            d[19] = s[16] & 0x7f;                                 // FF
        }
        for (int k = 0; k < 8; ++k) e[126 + (size_t) k] = m[102 + k] & 0x7f; // PR1-4, PL1-4
        e[134] = m[110] & 31;                                                 // ALG
        e[135] = m[111] & 7;   e[136] = (m[111] >> 3) & 1;                    // FB, OKS
        for (int k = 0; k < 4; ++k) e[137 + (size_t) k] = m[112 + k] & 0x7f; // LFS, LFD, LPMD, LAMD
        e[141] = m[116] & 1;   e[142] = (m[116] >> 1) & 7; e[143] = (m[116] >> 4) & 7; // LKS, LFW, LPMS
        e[144] = m[117] & 0x7f;                                               // TRNSP
        for (int k = 0; k < 10; ++k) e[145 + (size_t) k] = m[118 + k] & 0x7f;
        v.name = nameOf (e.data() + 145);
        return v;
    }

    static juce::String nameOf (const uint8_t* text)
    {
        juce::String s;
        for (int k = 0; k < 10; ++k)
            s += juce::String::charToString ((juce::juce_wchar) (text[k] >= 32 && text[k] < 127 ? text[k] : ' '));
        return s.trimEnd();
    }

    // Escribe la voz en los parámetros del motor FM. Devuelve lo que el motor no modela (vacío si nada)
    static juce::String apply (const Voice& v, juce::AudioProcessorValueTreeState& apvts)
    {
        const auto set = [&apvts] (const juce::String& id, int value)
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
            {
                const auto& range = p->getNormalisableRange();
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (range.start, range.end, (float) value)));
                p->endChangeGesture();
            }
        };
        const auto& e = v.vced;
        for (int k = 0; k < 6; ++k)
        {
            const auto* d = e.data() + k * 21;
            const auto op = "fm" + juce::String (6 - k); // VCED empieza por OP6
            for (int r = 0; r < 4; ++r)
            {
                set (op + "R" + juce::String (r + 1), d[r]);
                set (op + "L" + juce::String (r + 1), d[4 + r]);
            }
            set (op + "Break", d[8] + 21); // BP 0 = A-1 (MIDI 21), 39 = C3 (MIDI 60)
            set (op + "LDepth", d[9]);   set (op + "RDepth", d[10]);
            set (op + "LCurve", d[11]);  set (op + "RCurve", d[12]);
            set (op + "RateScale", d[13]); set (op + "Ams", d[14]); set (op + "Vel", d[15]);
            set (op + "Level", d[16]);   set (op + "Fixed", d[17]);
            set (op + "Coarse", d[18]);  set (op + "Fine", d[19]);
            set (op + "Detune", d[20] - 7);
        }
        set ("fmAlgorithm", e[134] + 1);
        set ("fmFeedback", e[135]);
        set ("fmLfoSpeed", e[137]); set ("fmLfoDelay", e[138]); set ("fmLfoPmd", e[139]); set ("fmLfoAmd", e[140]);
        set ("fmLfoWave", e[142]);  set ("fmLfoPms", e[143]);

        juce::StringArray ignored;
        if (e[130] != 50 || e[131] != 50 || e[132] != 50 || e[133] != 50)
            ignored.add ("pitch envelope");
        if (e[144] != 24)
            ignored.add ("transpose " + juce::String ((int) e[144] - 24) + " st");
        if (e[136] == 0)
            ignored.add ("oscillator key sync off");
        if (e[141] != 0)
            ignored.add ("LFO key sync");
        return ignored.joinIntoString (", ");
    }
};
