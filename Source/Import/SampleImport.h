// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// Carga de samples y bancos del usuario (KoniSynth no incluye ninguno): audio (WAV/AIFF/FLAC/OGG), SoundFont 2 (.sf2),
// BRR de la SNES (.brr) e instantáneas del APU de la SNES (.spc). Los bancos guardan el elemento elegido en la ruta, tras
// '|' (carácter no válido en nombres de fichero de Windows): "banco.sf2|preset=0:24;zone=2", "juego.spc|sample=5". Así los
// presets y el estado del plugin no cambian de formato.
struct SampleImport
{
    static constexpr const char* fileFilter = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.sf2;*.brr;*.spc";

    static juce::File fileOf (const juce::String& path) { return juce::File (path.containsChar ('|') ? path.upToLastOccurrenceOf ("|", false, false) : path); }
    static juce::String itemOf (const juce::String& path) { return path.containsChar ('|') ? path.fromLastOccurrenceOf ("|", false, false) : juce::String(); }
    static juce::String pathOf (const juce::File& file, const juce::String& item) { return item.isEmpty() ? file.getFullPathName() : file.getFullPathName() + "|" + item; }

    // Formatos con varios samples dentro: hay que elegir uno (Editor::chooseSampleItem)
    static bool isBank (const juce::File& f) { return f.hasFileExtension ("sf2;spc"); }

    static int itemValue (const juce::String& item, const juce::String& key, int fallback)
    {
        for (const auto& part : juce::StringArray::fromTokens (item, ";", ""))
            if (part.upToFirstOccurrenceOf ("=", false, false).trim() == key)
                return part.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        return fallback;
    }

    static std::unique_ptr<SampleData> load (const juce::File& file, const juce::String& item, juce::String& error)
    {
        if (file.hasFileExtension ("brr")) return loadBrrFile (file, error);
        if (file.hasFileExtension ("spc")) return loadSpc (file, item, error);
        if (file.hasFileExtension ("sf2")) return loadSf2 (file, item, error);
        return SampleData::fromFile (file, error);
    }

    // BRR: bloques decodificados a 32 kHz (1:1 en el S-DSP) para los demás motores, y los originales para el motor SNES
    static std::unique_ptr<SampleData> fromBrr (const uint8_t* bytes, int blocks, int loopBlock, const juce::String& name)
    {
        auto data = std::make_unique<SampleData>();
        data->sampleRate = 32000.0;
        data->name = name;
        data->brr.assign (bytes, bytes + (size_t) blocks * 9);
        data->brr[(size_t) (blocks - 1) * 9] |= 1; // garantiza el End en el último bloque
        const auto loops = (data->brr[(size_t) (blocks - 1) * 9] & 2) != 0 && loopBlock >= 0 && loopBlock < blocks;
        data->brrLoopBlock = loops ? loopBlock : -1;
        if (loops)
        {
            data->loopStart = loopBlock * 16;
            data->loopEnd = blocks * 16;
        }
        const auto pcm = Brr::decode (data->brr.data(), blocks);
        data->levels[0].resize (pcm.size());
        for (size_t i = 0; i < pcm.size(); ++i)
            data->levels[0][i] = (float) pcm[i] / 32768.0f;
        data->buildLevels();
        return data;
    }

    // .brr: bloques de 9 bytes; si el tamaño es 9·n + 2, los 2 primeros bytes son el desplazamiento del bucle en bytes
    // (convención de los ripeadores y de los editores de SNES); si es 9·n, el bucle (si lo hay) vuelve al principio
    static std::unique_ptr<SampleData> loadBrrFile (const juce::File& file, juce::String& error)
    {
        juce::MemoryBlock block;
        if (! file.loadFileAsData (block) || block.getSize() < 9 || (block.getSize() % 9 != 0 && block.getSize() % 9 != 2))
        {
            error = "Not a BRR file (9-byte blocks, optionally a 2-byte loop header): " + file.getFileName();
            return nullptr;
        }
        const auto* b = static_cast<const uint8_t*> (block.getData());
        const auto header = block.getSize() % 9 == 2 ? 2 : 0;
        const auto loopOffset = header != 0 ? (int) (b[0] | (b[1] << 8)) : 0;
        const auto total = (block.getSize() - (size_t) header) / 9;
        auto blocks = Brr::chainLength (b + header, block.getSize() - (size_t) header, false);
        if (blocks == 0)
            blocks = (int) total;
        return fromBrr (b + header, blocks, loopOffset % 9 == 0 ? loopOffset / 9 : -1, file.getFileName());
    }

    static std::unique_ptr<SampleData> loadSpc (const juce::File& file, const juce::String& item, juce::String& error)
    {
        const auto spc = SpcFile::load (file, error);
        if (spc == nullptr)
            return nullptr;
        const auto index = itemValue (item, "sample", spc->samples.front().index);
        const auto it = std::find_if (spc->samples.begin(), spc->samples.end(), [index] (const SpcFile::Sample& s) { return s.hasIndex (index); });
        if (it == spc->samples.end())
        {
            error = "Sample " + juce::String (index) + " not found in " + file.getFileName();
            return nullptr;
        }
        const auto title = spc->game.isNotEmpty() ? spc->game + (spc->song.isNotEmpty() ? " - " + spc->song : juce::String()) : file.getFileName();
        return fromBrr (spc->ram() + it->start, it->blocks, it->loopBlock, title + ", " + SpcFile::describe (*it).upToFirstOccurrenceOf ("  ", false, false));
    }

    static std::unique_ptr<SampleData> loadSf2 (const juce::File& file, const juce::String& item, juce::String& error)
    {
        const auto sf = Sf2File::load (file, error);
        if (sf == nullptr)
            return nullptr;
        const auto key = item.isNotEmpty() ? item.fromFirstOccurrenceOf ("preset=", false, false).upToFirstOccurrenceOf (";", false, false) : juce::String();
        const auto bank = key.upToFirstOccurrenceOf (":", false, false).getIntValue(), program = key.fromFirstOccurrenceOf (":", false, false).getIntValue();
        auto it = std::find_if (sf->presets.begin(), sf->presets.end(), [&] (const Sf2File::Preset& p) { return p.bank == bank && p.program == program; });
        if (key.isEmpty())
            it = sf->presets.begin();
        if (it == sf->presets.end())
        {
            error = "Preset " + key + " not found in " + file.getFileName();
            return nullptr;
        }
        const auto zoneIndex = juce::jlimit (0, (int) it->zones.size() - 1, itemValue (item, "zone", Sf2File::defaultZone (*it)));
        const auto& z = it->zones[(size_t) zoneIndex];
        const auto& h = sf->sampleHeaders[(size_t) z.sample];
        const auto frames = (long long) (sf->smplSize / 2);
        const auto start = (long long) h.start + z.startOffset, end = (long long) h.end + z.endOffset;
        if (start < 0 || end > frames || end - start < 4)
        {
            error = "Sample \"" + h.name + "\" points outside the sample data of " + file.getFileName();
            return nullptr;
        }
        auto data = std::make_unique<SampleData>();
        // Afinación de la zona (corrección del sample + coarse/fine): equivale a reproducir el sample a otra frecuencia
        data->sampleRate = h.rate * std::exp2 ((h.correction + z.fine + 100.0 * z.coarse) / 1200.0);
        data->name = it->name + " / " + h.name;
        const auto length = std::min (end - start, (long long) (60.0 * h.rate));
        data->truncated = length < end - start;
        const auto* pcm = static_cast<const uint8_t*> (sf->data.getData()) + sf->smplOffset + (size_t) start * 2;
        data->levels[0].resize ((size_t) length);
        for (long long i = 0; i < length; ++i)
            data->levels[0][(size_t) i] = (float) (int16_t) (uint16_t) (pcm[i * 2] | (pcm[i * 2 + 1] << 8)) / 32768.0f;
        const auto loopStart = (long long) h.loopStart + z.loopStartOffset - start, loopEnd = (long long) h.loopEnd + z.loopEndOffset - start;
        if ((z.mode == 1 || z.mode == 3) && loopStart >= 0 && loopEnd <= length && loopEnd - loopStart >= 2)
        {
            data->loopStart = (int) loopStart;
            data->loopEnd = (int) loopEnd;
        }
        data->rootKey = z.rootKey >= 0 && z.rootKey <= 127 ? z.rootKey : (h.originalPitch <= 127 ? h.originalPitch : 60);
        data->buildLevels();
        return data;
    }
};
