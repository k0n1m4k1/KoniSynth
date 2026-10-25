// Ritmos de usuario en ficheros MIDI (banco por motor, 8 ranuras). Cabecera autónoma: usa JUCE (juce_audio_basics) y
// DrumSlot / RhythmPattern de Engines/RhythmPatterns.h, incluida antes; KoniBench la incluye igual (prueba rhythmtest).
#pragma once

//==============================================================================
// Formato: un fichero MIDI estándar por ritmo, editable en cualquier secuenciador.
//   <ajustes de usuario>/KoniSynth/rhythms/<motor>/User 1.mid ... User 8.mid (si falta, la ranura queda vacía)
//   Tipo 0, 96 ticks por negra. Nombre: meta "track name". Batería en el canal 10 con notas GM (las mismas que el teclado)
//   Rejilla según el compás y las posiciones de las notas:
//     4/4, semicorcheas (24 ticks): 16 pasos (un compás) o 32 (dos)
//     3/4, semicorcheas: 12 pasos (vals; el contador salta las posiciones 4-5 y 8-9, como los de fábrica)
//     4/4 con las notas en tresillo de corchea (32 ticks): 12 pasos (shuffle)
//   Niveles por velocity: suave 57, normal 95, acento 127 (los 'o', 'x', 'X' de los patrones de fábrica, ×127). Al leer se
//   cuantiza: < 76 suave, < 111 normal, el resto acento; la posición, al paso más cercano. Notas fuera del mapa o de la
//   longitud: se ignoran (se cuentan en 'ignored')
namespace UserRhythms
{
constexpr int numSlots = 8;
constexpr int ticksPerQuarter = 96;
inline const juce::String gridMark { "KoniSynth rhythm:" }; // meta de texto con la rejilla exacta (ver read)

struct Rhythm
{
    juce::String name;
    int numSteps = 16, stepsPerBeat = 4;          // 16/32 a 4 por tiempo; 12 a 4 (vals) o a 3 (tresillo)
    std::array<std::string, (size_t) DrumSlot::count> lanes; // un carácter por paso ('.', 'o', 'x', 'X'); vacío = sin carril
    int ignored = 0;                              // notas descartadas al leer

    bool isWaltz() const { return numSteps == 12 && stepsPerBeat == 4; }
    void clear (int steps, int perBeat)
    {
        numSteps = steps; stepsPerBeat = perBeat;
        for (auto& l : lanes) l.clear();
    }
    void set (DrumSlot slot, int step, char level)
    {
        auto& l = lanes[(size_t) slot];
        if (l.empty()) l.assign ((size_t) numSteps, '.');
        auto& c = l[(size_t) step];
        const auto rank = [] (char ch) { return ch == 'X' ? 3 : ch == 'x' ? 2 : ch == 'o' ? 1 : 0; };
        if (rank (level) > rank (c)) c = level; // dos notas en el mismo paso: la más fuerte
    }
};

// Carril -> nota GM al escribir (las del teclado del motor, Engines/DrumMachine.h gmVoice)
inline int gmNote (DrumSlot s)
{
    static constexpr int notes[] = { 36, 38, 37, 39, 42, 46, 49, 56, 45, 50, 64, 70, 75, 54, 73 };
    const auto i = (size_t) s;
    return i < std::size (notes) ? notes[i] : -1;
}

// Nota GM -> carril al leer (con los alias del mapa GM que el teclado ya acepta); -1 = sin carril
inline int slotForNote (int note)
{
    using S = DrumSlot;
    switch (note)
    {
        case 35: case 36: return (int) S::BD;
        case 38: case 40: return (int) S::SD;
        case 37:          return (int) S::RS;
        case 39:          return (int) S::CP;
        case 42: case 44: return (int) S::CH;
        case 46:          return (int) S::OH;
        case 49: case 51: case 52: case 55: case 57: case 59: return (int) S::CY;
        case 56:          return (int) S::CB;
        case 41: case 43: case 45: return (int) S::LT;
        case 47: case 48: case 50: return (int) S::HT;
        case 62: case 63: case 64: return (int) S::LC;
        case 58: case 70: return (int) S::MA; // 58: la quijada del Mini Pops 7 ocupa el carril de maracas
        case 75:          return (int) S::CL;
        case 54:          return (int) S::TB;
        case 73: case 74: return (int) S::GU;
        default:          return -1;
    }
}

inline char levelFor (int velocity) { return velocity < 76 ? 'o' : velocity < 111 ? 'x' : 'X'; }
inline int velocityFor (char level) { return level == 'X' ? 127 : level == 'x' ? 95 : level == 'o' ? 57 : 0; }

// Lee un ritmo de un MIDI. Devuelve false (con el motivo) si no se puede leer o no tiene ninguna nota del mapa
inline bool read (juce::InputStream& in, const juce::String& fallbackName, Rhythm& out, juce::String& error)
{
    juce::MidiFile mf;
    if (! mf.readFrom (in)) { error = "not a MIDI file"; return false; }
    const auto tpq = (int) mf.getTimeFormat();
    if (tpq <= 0) { error = "SMPTE time format is not supported"; return false; }

    // Todas las pistas juntas (un tipo 1 exportado de un DAW suele llevar el tempo aparte)
    juce::MidiMessageSequence all;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        all.addSequence (*mf.getTrack (t), 0.0);
    all.sort();

    juce::String name;
    int num = 4, den = 4, markSteps = 0, markPerBeat = 0;
    std::vector<std::pair<double, std::pair<int, int>>> notes; // (tick, (nota, velocity)) del canal 10
    for (const auto* e : all)
    {
        const auto& m = e->message;
        if (m.isTrackNameEvent() && name.isEmpty()) name = m.getTextFromTextMetaEvent().trim();
        else if (m.isTimeSignatureMetaEvent()) m.getTimeSignatureInfo (num, den);
        else if (m.isTextMetaEvent() && m.getTextFromTextMetaEvent().startsWith (gridMark))
        {
            // Rejilla declarada por KoniSynth al guardar ("KoniSynth rhythm: 12 steps, 3 per beat"): manda sobre la deducción
            const auto t = m.getTextFromTextMetaEvent().fromFirstOccurrenceOf (":", false, false);
            markSteps = t.upToFirstOccurrenceOf ("steps", false, false).trim().getIntValue();
            markPerBeat = t.fromFirstOccurrenceOf (",", false, false).trim().getIntValue();
        }
        else if (m.isNoteOn() && m.getChannel() == 10) notes.push_back ({ m.getTimeStamp(), { m.getNoteNumber(), m.getVelocity() } });
    }
    if (notes.empty()) { error = "no drum notes on MIDI channel 10"; return false; }

    // Rejilla: 3/4 = vals; si todas las notas caen en tresillo de corchea y alguna no en semicorchea, shuffle
    const auto q16 = tpq / 4.0, q12 = tpq / 3.0;
    const auto on = [] (double t, double q) { return std::abs (t / q - std::round (t / q)) < 0.1; };
    bool allTriplet = true, someOff16 = false;
    for (const auto& n : notes) { allTriplet = allTriplet && on (n.first, q12); someOff16 = someOff16 || ! on (n.first, q16); }
    const auto marked = (markSteps == 16 || markSteps == 32 || markSteps == 12) && (markPerBeat == 4 || (markPerBeat == 3 && markSteps == 12));
    const auto triplet = marked ? markPerBeat == 3 : num != 3 && allTriplet && someOff16;
    const auto stepTicks = triplet ? q12 : q16;
    if (marked)                out.clear (markSteps, markPerBeat);
    else if (num == 3 && den == 4) out.clear (12, 4);
    else if (triplet)          out.clear (12, 3);
    else
    {
        double last = 0.0;
        for (const auto& n : notes) last = std::max (last, n.first);
        out.clear (std::round (last / q16) >= 16.0 ? 32 : 16, 4);
    }

    out.ignored = 0;
    int used = 0;
    for (const auto& [tick, nv] : notes)
    {
        const auto step = (int) std::round (tick / stepTicks);
        const auto slot = slotForNote (nv.first);
        if (slot < 0 || step < 0 || step >= out.numSteps) { ++out.ignored; continue; }
        out.set ((DrumSlot) slot, step, levelFor (nv.second));
        ++used;
    }
    if (used == 0) { error = "no notes of the drum map inside the pattern length"; return false; }
    out.name = name.isNotEmpty() ? name : fallbackName;
    return true;
}

inline bool read (const juce::File& file, Rhythm& out, juce::String& error)
{
    juce::FileInputStream in (file);
    if (! in.openedOk()) { error = "cannot open " + file.getFullPathName(); return false; }
    return read (in, file.getFileNameWithoutExtension(), out, error);
}

// Escribe un ritmo como MIDI tipo 0 (nota de medio paso de duración, como un disparo de batería)
inline bool write (const Rhythm& r, juce::OutputStream& out)
{
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, r.name.isNotEmpty() ? r.name : "User Rhythm"), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (r.isWaltz() ? 3 : 4, 4), 0.0);
    seq.addEvent (juce::MidiMessage::textMetaEvent (1, gridMark + " " + juce::String (r.numSteps) + " steps, "
                                                       + juce::String (r.stepsPerBeat) + " per beat"), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0); // 120 BPM, informativo: el motor usa su propio tempo
    const auto stepTicks = r.stepsPerBeat == 3 ? ticksPerQuarter / 3.0 : ticksPerQuarter / 4.0;
    for (int s = 0; s < (int) DrumSlot::count; ++s)
    {
        const auto& lane = r.lanes[(size_t) s];
        const auto note = gmNote ((DrumSlot) s);
        if (lane.empty() || note < 0) continue;
        for (int i = 0; i < r.numSteps && i < (int) lane.size(); ++i)
            if (const auto v = velocityFor (lane[(size_t) i]); v > 0)
            {
                seq.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) v), i * stepTicks);
                seq.addEvent (juce::MidiMessage::noteOff (10, note), (i + 0.5) * stepTicks);
            }
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), r.numSteps * stepTicks);
    seq.sort();
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ticksPerQuarter);
    mf.addTrack (seq);
    return mf.writeTo (out, 0);
}

inline bool write (const Rhythm& r, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (file); // escritura atómica: el vigilante nunca lee un fichero a medias
    {
        juce::FileOutputStream out (tmp.getFile());
        if (! out.openedOk() || ! write (r, out)) return false;
    }
    return tmp.overwriteTargetFileWithTemporary();
}

// Carpeta del banco de un motor y fichero de una ranura (0..7)
inline juce::File folder (const juce::String& engine)
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("KoniSynth").getChildFile ("rhythms").getChildFile (engine);
}
inline juce::File slotFile (const juce::String& engine, int slot) { return folder (engine).getChildFile ("User " + juce::String (slot + 1) + ".mid"); }

// Banco publicado al hilo de audio (DataSlot): ritmos leídos y su vista como RhythmPattern (punteros a sus cadenas; el
// banco no cambia tras publicarse)
struct Bank
{
    std::array<Rhythm, numSlots> rhythms;
    std::array<bool, numSlots> present {};
    std::array<RhythmPattern, numSlots> views;
    std::array<juce::String, numSlots> errors;
    std::array<std::string, numSlots> names; // copia estable en std::string para RhythmPattern::name

    void buildViews()
    {
        for (int i = 0; i < numSlots; ++i)
        {
            auto& v = views[(size_t) i];
            const auto& r = rhythms[(size_t) i];
            names[(size_t) i] = r.name.toStdString();
            v.name = names[(size_t) i].c_str();
            v.numSteps = r.numSteps;
            v.stepsPerBeat = r.stepsPerBeat;
            v.lanes.clear();
            if (present[(size_t) i])
                for (int s = 0; s < (int) DrumSlot::count; ++s)
                    if (! r.lanes[(size_t) s].empty())
                        v.lanes.push_back ({ (DrumSlot) s, r.lanes[(size_t) s].c_str() });
        }
    }

    const RhythmPattern* pattern (int slot) const
    {
        return slot >= 0 && slot < numSlots && present[(size_t) slot] ? &views[(size_t) slot] : nullptr;
    }
};

inline std::unique_ptr<Bank> loadBank (const juce::String& engine)
{
    auto bank = std::make_unique<Bank>();
    for (int i = 0; i < numSlots; ++i)
    {
        const auto f = slotFile (engine, i);
        if (f.existsAsFile())
            bank->present[(size_t) i] = read (f, bank->rhythms[(size_t) i], bank->errors[(size_t) i]);
    }
    bank->buildViews();
    return bank;
}

// Firma del banco en disco (tamaño y fecha de cada ranura): si cambia, se vuelve a leer
inline juce::String signature (const juce::String& engine)
{
    juce::String s;
    for (int i = 0; i < numSlots; ++i)
    {
        const auto f = slotFile (engine, i);
        s << (f.existsAsFile() ? juce::String (f.getSize()) + "@" + juce::String (f.getLastModificationTime().toMilliseconds()) : "-") << ";";
    }
    return s;
}
} // namespace UserRhythms
