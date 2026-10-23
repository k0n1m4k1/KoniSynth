// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// SoundFont 2 (especificación SoundFont 2.04, E-mu/Creative): RIFF "sfbk" con LIST sdta (smpl: PCM de 16 bits) y LIST pdta
// (phdr/pbag/pgen -> presets, inst/ibag/igen -> instrumentos, shdr -> cabeceras de sample). Se resuelven las zonas de cada
// preset (zona global + zonas locales, a nivel de preset y de instrumento) hasta el sample con su rango de teclas, nota raíz,
// afinación y bucle. Lo que KoniSynth usa de una zona: el sample, su bucle y la nota raíz/afinación; las envolventes,
// filtros y moduladores del SoundFont no se aplican (los pone el motor que lo toque).
struct Sf2File
{
    struct Zone
    {
        int keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
        int sample = -1, rootKey = -1, coarse = 0, fine = 0, mode = 0;
        int startOffset = 0, endOffset = 0, loopStartOffset = 0, loopEndOffset = 0;
        juce::String instrument;
    };
    struct Preset
    {
        juce::String name;
        int bank = 0, program = 0;
        std::vector<Zone> zones;
    };
    struct SampleHeader
    {
        juce::String name;
        uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0, rate = 44100;
        int originalPitch = 60, correction = 0, type = 1;
    };

    juce::MemoryBlock data;
    size_t smplOffset = 0, smplSize = 0;
    std::vector<Preset> presets; // ordenados por banco y programa
    std::vector<SampleHeader> sampleHeaders;

    static std::unique_ptr<Sf2File> load (const juce::File& file, juce::String& error)
    {
        auto sf = std::make_unique<Sf2File>();
        error = "Not a valid SoundFont 2 file: " + file.getFileName();
        if (! file.loadFileAsData (sf->data) || sf->data.getSize() < 12)
            return nullptr;
        const auto* b = static_cast<const uint8_t*> (sf->data.getData());
        const auto size = sf->data.getSize();
        const auto u32 = [b] (size_t at) { return (uint32_t) (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | ((uint32_t) b[at + 3] << 24)); };
        const auto u16 = [b] (size_t at) { return (int) (b[at] | (b[at + 1] << 8)); };
        const auto tag = [b] (size_t at) { return juce::String::fromUTF8 (reinterpret_cast<const char*> (b + at), 4); };
        if (tag (0) != "RIFF" || tag (8) != "sfbk")
            return nullptr;

        // Subbloques de los LIST sdta y pdta
        std::map<juce::String, std::pair<size_t, size_t>> chunks;
        for (size_t at = 12; at + 8 <= size;)
        {
            const auto len = (size_t) u32 (at + 4);
            if (at + 8 + len > size)
                break;
            if (tag (at) == "LIST" && len >= 4)
                for (size_t sub = at + 12; sub + 8 <= at + 8 + len;)
                {
                    const auto subLen = (size_t) u32 (sub + 4);
                    if (sub + 8 + subLen > at + 8 + len)
                        break;
                    chunks[tag (sub)] = { sub + 8, subLen };
                    sub += 8 + subLen + (subLen & 1);
                }
            at += 8 + len + (len & 1);
        }
        for (const auto* name : { "smpl", "phdr", "pbag", "pgen", "inst", "ibag", "igen", "shdr" })
            if (chunks.count (name) == 0)
                return nullptr;
        sf->smplOffset = chunks["smpl"].first;
        sf->smplSize = chunks["smpl"].second;
        const auto records = [&chunks] (const char* name, size_t recordSize) { return chunks[name].second / recordSize; };
        const auto name20 = [b] (size_t at)
        {
            const auto* text = reinterpret_cast<const char*> (b + at);
            return juce::String::fromUTF8 (text, (int) strnlen (text, 20)).trim();
        };

        const auto numSamples = records ("shdr", 46);
        for (size_t i = 0; i + 1 < numSamples; ++i) // el último registro es el terminador EOS
        {
            const auto at = chunks["shdr"].first + i * 46;
            SampleHeader h;
            h.name = name20 (at);
            h.start = u32 (at + 20); h.end = u32 (at + 24); h.loopStart = u32 (at + 28); h.loopEnd = u32 (at + 32);
            h.rate = std::max<uint32_t> (1, u32 (at + 36));
            h.originalPitch = b[at + 40];
            h.correction = (int8_t) b[at + 41];
            h.type = u16 (at + 44);
            sf->sampleHeaders.push_back (h);
        }

        // Generadores de una lista de zonas (bags): devuelve, por zona, sus pares (operador, cantidad)
        using Gens = std::vector<std::pair<int, int>>;
        const auto zoneGens = [&] (const char* bag, const char* gen, size_t first, size_t last)
        {
            std::vector<Gens> zones;
            const auto numBags = records (bag, 4), numGens = records (gen, 4);
            for (size_t z = first; z < last && z + 1 < numBags; ++z)
            {
                const auto g0 = (size_t) u16 (chunks[bag].first + z * 4), g1 = (size_t) u16 (chunks[bag].first + (z + 1) * 4);
                Gens gens;
                for (size_t g = g0; g < g1 && g < numGens; ++g)
                {
                    const auto at = chunks[gen].first + g * 4;
                    gens.push_back ({ u16 (at), u16 (at + 2) });
                }
                zones.push_back (std::move (gens));
            }
            return zones;
        };
        const auto find = [] (const Gens& g, int op) -> std::optional<int>
        {
            for (auto it = g.rbegin(); it != g.rend(); ++it)
                if (it->first == op) return it->second;
            return std::nullopt;
        };
        enum { startOffs = 0, endOffs = 1, loopStartOffs = 2, loopEndOffs = 3, startCoarse = 4, endCoarse = 12, keyRange = 43,
               velRange = 44, loopStartCoarse = 45, loopEndCoarse = 50, coarseTune = 51, fineTune = 52, sampleId = 53,
               sampleModes = 54, instrumentId = 41, rootKey = 58 };
        const auto s16 = [] (int v) { return (int) (int16_t) (uint16_t) v; };

        const auto numInst = records ("inst", 22);
        const auto numPresets = records ("phdr", 38);
        for (size_t p = 0; p + 1 < numPresets; ++p)
        {
            const auto at = chunks["phdr"].first + p * 38;
            Preset preset;
            preset.name = name20 (at);
            preset.program = u16 (at + 20);
            preset.bank = u16 (at + 22);
            const auto pz = zoneGens ("pbag", "pgen", (size_t) u16 (at + 24), (size_t) u16 (at + 38 + 24));
            Gens pGlobal;
            for (size_t k = 0; k < pz.size(); ++k)
            {
                const auto inst = find (pz[k], instrumentId);
                if (! inst.has_value())
                {
                    if (k == 0) pGlobal = pz[k];
                    continue;
                }
                if ((size_t) *inst + 1 >= numInst)
                    continue;
                Gens pg = pGlobal;
                pg.insert (pg.end(), pz[k].begin(), pz[k].end());
                const auto iat = chunks["inst"].first + (size_t) *inst * 22;
                const auto iz = zoneGens ("ibag", "igen", (size_t) u16 (iat + 20), (size_t) u16 (iat + 22 + 20));
                Gens iGlobal;
                for (size_t j = 0; j < iz.size(); ++j)
                {
                    const auto sid = find (iz[j], sampleId);
                    if (! sid.has_value())
                    {
                        if (j == 0) iGlobal = iz[j];
                        continue;
                    }
                    if ((size_t) *sid >= sf->sampleHeaders.size() || (sf->sampleHeaders[(size_t) *sid].type & 0x8000) != 0) // ROM: sin datos
                        continue;
                    Gens ig = iGlobal;
                    ig.insert (ig.end(), iz[j].begin(), iz[j].end());
                    Zone z;
                    z.instrument = name20 (iat);
                    z.sample = *sid;
                    // Rangos: intersección de preset e instrumento
                    for (const auto* g : { &pg, &ig })
                    {
                        if (const auto r = find (*g, keyRange)) { z.keyLo = std::max (z.keyLo, *r & 0xff); z.keyHi = std::min (z.keyHi, *r >> 8); }
                        if (const auto r = find (*g, velRange)) { z.velLo = std::max (z.velLo, *r & 0xff); z.velHi = std::min (z.velHi, *r >> 8); }
                    }
                    if (z.keyLo > z.keyHi)
                        continue;
                    // Afinación: el preset suma a la del instrumento (SF 2.04, 9.4)
                    z.coarse = s16 (find (ig, coarseTune).value_or (0)) + s16 (find (pg, coarseTune).value_or (0));
                    z.fine = s16 (find (ig, fineTune).value_or (0)) + s16 (find (pg, fineTune).value_or (0));
                    z.rootKey = s16 (find (ig, rootKey).value_or (-1));
                    z.mode = find (ig, sampleModes).value_or (0) & 3;
                    z.startOffset = s16 (find (ig, startOffs).value_or (0)) + 32768 * s16 (find (ig, startCoarse).value_or (0));
                    z.endOffset = s16 (find (ig, endOffs).value_or (0)) + 32768 * s16 (find (ig, endCoarse).value_or (0));
                    z.loopStartOffset = s16 (find (ig, loopStartOffs).value_or (0)) + 32768 * s16 (find (ig, loopStartCoarse).value_or (0));
                    z.loopEndOffset = s16 (find (ig, loopEndOffs).value_or (0)) + 32768 * s16 (find (ig, loopEndCoarse).value_or (0));
                    preset.zones.push_back (z);
                }
            }
            if (! preset.zones.empty())
                sf->presets.push_back (std::move (preset));
        }
        std::stable_sort (sf->presets.begin(), sf->presets.end(), [] (const Preset& x, const Preset& y)
                          { return x.bank != y.bank ? x.bank < y.bank : x.program < y.program; });
        if (sf->presets.empty())
        {
            error = "No playable presets in " + file.getFileName();
            return nullptr;
        }
        error = {};
        return sf;
    }

    // Zona por defecto de un preset: la que contiene el Do central (nota 60) o, si ninguna, la de rango más cercano
    static int defaultZone (const Preset& p)
    {
        int best = 0, bestDistance = 1000;
        for (size_t k = 0; k < p.zones.size(); ++k)
        {
            const auto& z = p.zones[k];
            const auto distance = 60 < z.keyLo ? z.keyLo - 60 : 60 > z.keyHi ? 60 - z.keyHi : 0;
            if (distance < bestDistance) { bestDistance = distance; best = (int) k; }
        }
        return best;
    }

    static juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); }

    juce::String describe (const Zone& z) const
    {
        const auto& h = sampleHeaders[(size_t) z.sample];
        return noteName (z.keyLo) + "-" + noteName (z.keyHi) + "  " + h.name + "  (root " + noteName (z.rootKey >= 0 ? z.rootKey : h.originalPitch) + ")";
    }
};
