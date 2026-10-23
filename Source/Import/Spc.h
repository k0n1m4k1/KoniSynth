// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// SPC: instantánea del APU de la SNES (formato "SNES-SPC700 Sound File Data v0.30"): cabecera de 256 bytes (título del juego
// en 4Eh, ID666), 64 KB de RAM del SPC700 en 100h y los 128 registros del S-DSP en 10100h. Los samples están en la RAM
// como BRR; el directorio está en DIR·100h (registro DIR = 5Dh), 4 bytes por entrada: inicio y punto de bucle (fullsnes
// "SNES APU DSP BRR Directory"). Aquí se listan las entradas que son cadenas BRR válidas.
struct SpcFile
{
    struct Sample
    {
        int index = 0, start = 0, blocks = 0, loopBlock = -1;
        int voice = -1; // voz (0-7) que lo tenía asignado (registro SRCN) en la instantánea, -1 si ninguna
    };

    juce::MemoryBlock data;
    juce::String game, song;
    std::vector<Sample> samples;

    static constexpr size_t kRam = 0x100, kDsp = 0x10100, kMinSize = 0x10180;

    const uint8_t* ram() const { return static_cast<const uint8_t*> (data.getData()) + kRam; }

    static std::unique_ptr<SpcFile> load (const juce::File& file, juce::String& error)
    {
        auto spc = std::make_unique<SpcFile>();
        if (! file.loadFileAsData (spc->data) || spc->data.getSize() < kMinSize
            || ! juce::String::fromUTF8 (static_cast<const char*> (spc->data.getData()), 27).startsWith ("SNES-SPC700 Sound File Data"))
        {
            error = "Not an SPC file (SNES-SPC700 Sound File Data): " + file.getFileName();
            return nullptr;
        }
        const auto* bytes = static_cast<const uint8_t*> (spc->data.getData());
        const auto text = [bytes] (size_t at, int len) { return juce::String::fromUTF8 (reinterpret_cast<const char*> (bytes + at), len).trim(); };
        if (bytes[0x23] == 26) // con etiqueta ID666
        {
            spc->song = text (0x2e, 32).upToFirstOccurrenceOf (juce::String::charToString (0), false, false);
            spc->game = text (0x4e, 32).upToFirstOccurrenceOf (juce::String::charToString (0), false, false);
        }
        const auto* dsp = bytes + kDsp;
        const auto dir = (size_t) dsp[0x5d] << 8;
        const auto* ram = spc->ram();
        for (int i = 0; i < 256; ++i)
        {
            const auto entry = dir + (size_t) i * 4;
            if (entry + 4 > 0x10000)
                break;
            const auto start = (int) (ram[entry] | (ram[entry + 1] << 8));
            const auto loop = (int) (ram[entry + 2] | (ram[entry + 3] << 8));
            if (start < 0x200) // páginas 0 y 1: registros y pila, nunca samples
                continue;
            const auto blocks = Brr::chainLength (ram + start, 0x10000 - (size_t) start, true);
            if (blocks == 0)
                continue;
            if (std::any_of (spc->samples.begin(), spc->samples.end(), [start] (const Sample& s) { return s.start == start; }))
                continue;
            Sample s;
            s.index = i;
            s.start = start;
            s.blocks = blocks;
            const auto endHeader = ram[start + (blocks - 1) * 9];
            if ((endHeader & 2) && loop >= start && (loop - start) % 9 == 0 && (loop - start) / 9 < blocks)
                s.loopBlock = (loop - start) / 9;
            for (int v = 0; v < 8; ++v)
                if (dsp[v * 0x10 + 4] == i) { s.voice = v; break; }
            spc->samples.push_back (s);
        }
        if (spc->samples.empty())
        {
            error = "No BRR samples found in " + file.getFileName();
            return nullptr;
        }
        return spc;
    }

    static juce::String describe (const Sample& s)
    {
        return "Sample " + juce::String::toHexString (s.index).paddedLeft ('0', 2).toUpperCase() + "  ("
               + juce::String (s.blocks * 16 / 32.0, 0) + " ms" + (s.loopBlock >= 0 ? ", loop" : "")
               + (s.voice >= 0 ? ", voice " + juce::String (s.voice) : "") + ")";
    }
};
