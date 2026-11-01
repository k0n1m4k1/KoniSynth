// Se incluye desde Transport/SessionTransport.h (y con él desde KoniSynth.cpp y Bench/KoniBench.cpp). Solo usa JUCE.
#pragma once
#include "../Midi/GeneralMidi.h"
#include <array>
#include <map>
#include <set>

//==============================================================================
// Play-along (práctica sobre un .mid): análisis del fichero para el reproductor del transporte y para la vista del display.
// - Pistas lógicas: cada par (pista del fichero, canal) con notas. Así un fichero de tipo 0 (una pista, varios canales) y uno
//   de tipo 1 (una pista por instrumento) se ven igual: una entrada por instrumento.
// - De cada pista: nombre (meta "track name"; si la pista lleva varios canales, " · ch N"), canal, primer Program Change,
//   notas (inicio y fin en segundos, tono, velocidad), tesitura y tono medio. El canal 10 es percusión (GM).
// - Melodía sugerida: la pista no percusiva de tono medio más agudo entre las que tienen al menos el 25 % de las notas de la
//   más poblada (descarta adornos de pocas notas).
// - Compases y tiempos (s) con el mapa de tempo y los compases del fichero (por defecto 120 BPM y 4/4). Con división SMPTE
//   (sin negras), rejilla de 120 BPM y 4/4 sobre los segundos.
// - Estado de cada nota de cada pista (pendiente, acertada, fallada), atómico: lo escribe el hilo de audio y lo lee la UI.
struct PlayAlongSong
{
    struct Note { double start = 0.0, end = 0.0; int pitch = 60, velocity = 100; };
    struct Track
    {
        juce::String name;
        int fileTrack = 0, channel = 1, program = -1, low = 127, high = 0;
        double meanPitch = 0.0;
        bool drums = false;
    };
    enum Status : juce::uint8 { pending = 0, hit = 1, missed = 2 };

    std::vector<Track> tracks;
    std::vector<std::vector<Note>> notes;  // por pista, ordenadas por inicio
    std::vector<double> bars, beats;       // inicios (s) de compás y de tiempo, hasta pasado el final
    double length = 0.0;                   // última nota apagada o último evento (s)
    double firstBarSeconds = 2.0;          // duración del primer compás: la cuenta previa
    std::vector<std::pair<double, double>> tempoMap { { 0.0, 120.0 } }; // (s, BPM de negra) desde cada cambio de tempo
    int suggested = -1;
    bool hasMt32Sysex = false;             // trae SysEx de Roland para el MT-32 (41 dd 16): música hecha para él, no GM
    std::vector<std::unique_ptr<std::atomic<juce::uint8>[]>> status; // [pista][nota]

    // Tempo del fichero (BPM de negra) en el instante sec
    double bpmAt (double sec) const
    {
        auto bpm = tempoMap.front().second;
        for (const auto& [at, b] : tempoMap)
        {
            if (at > sec) break;
            bpm = b;
        }
        return bpm;
    }

    int numNotes (int t) const { return t >= 0 && t < (int) notes.size() ? (int) notes[(size_t) t].size() : 0; }
    juce::uint8 statusOf (int t, int i) const { return status[(size_t) t][(size_t) i].load (std::memory_order_relaxed); }
    void setStatus (int t, int i, Status s) const { status[(size_t) t][(size_t) i].store (s, std::memory_order_relaxed); }
    void resetStatus (int t, double from = -1.0e9, double to = 1.0e9) const
    {
        for (int i = 0; i < numNotes (t); ++i)
            if (const auto& n = notes[(size_t) t][(size_t) i]; n.start >= from && n.start < to)
                setStatus (t, i, pending);
    }

    // Nombres de los 128 programas de General MIDI (Midi/GeneralMidi.h): solo para la lista de pistas
    static const char* gmProgramName (int program) { return GeneralMidi::programName (program); }

    // Texto de la lista de pistas: "Melody · ch 4 · Flute · 312 notes · C4-A5"
    juce::String describe (int t) const
    {
        const auto& k = tracks[(size_t) t];
        juce::String s = k.name + " · ch " + juce::String (k.channel);
        if (k.drums) s << " · drums";
        else if (k.program >= 0) s << " · " << gmProgramName (k.program);
        s << " · " << numNotes (t) << " notes";
        if (! k.drums && numNotes (t) > 0)
            s << " · " << juce::MidiMessage::getMidiNoteName (k.low, true, true, 4) << "-" << juce::MidiMessage::getMidiNoteName (k.high, true, true, 4);
        return s;
    }

    // Pista lógica de cada evento del fichero: el índice en 'tracks' (o -1 si su par pista/canal no tiene notas, o es SysEx).
    // 'mf' en ticks (antes de convertir a segundos). Rellena todo menos 'status' (lo crea prepareStatus)
    static std::shared_ptr<PlayAlongSong> analyse (const juce::MidiFile& mf)
    {
        auto song = std::make_shared<PlayAlongSong>();
        const auto division = mf.getTimeFormat();
        const auto ppq = division > 0 ? (double) division : 480.0; // SMPTE: rejilla de 120 BPM sobre una base de 480
        // Mapa de tempo (tick -> µs por negra) y compases (tick, numerador, denominador) de todas las pistas
        std::vector<std::pair<double, double>> tempos { { 0.0, 500000.0 } };
        std::vector<std::array<double, 3>> signatures { { 0.0, 4.0, 4.0 } };
        double lastTick = 0.0;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            for (const auto* e : *mf.getTrack (t))
            {
                const auto& m = e->message;
                lastTick = std::max (lastTick, m.getTimeStamp());
                if (m.isSysEx() && m.getSysExDataSize() >= 3 && m.getSysExData()[0] == 0x41 && m.getSysExData()[2] == 0x16)
                    song->hasMt32Sysex = true;
                if (division > 0 && m.isTempoMetaEvent())
                    tempos.push_back ({ m.getTimeStamp(), m.getTempoSecondsPerQuarterNote() * 1.0e6 });
                else if (m.isTimeSignatureMetaEvent())
                {
                    int num = 4, den = 4;
                    m.getTimeSignatureInfo (num, den);
                    if (num > 0 && den > 0) signatures.push_back ({ m.getTimeStamp(), (double) num, (double) den });
                }
            }
        const auto byTick = [] (const auto& a, const auto& b) { return a[0] < b[0]; };
        std::stable_sort (tempos.begin(), tempos.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        std::stable_sort (signatures.begin(), signatures.end(), byTick);
        // tick -> segundos con el mapa de tempo (SMPTE: los ticks ya son tiempo real, a la división del fichero)
        const auto seconds = [&] (double tick)
        {
            if (division <= 0)
            {
                const auto fps = (double) (-(division >> 8)), sub = (double) (division & 0xff);
                return tick / juce::jmax (1.0, fps * sub);
            }
            double s = 0.0, prevTick = 0.0, uspq = 500000.0;
            for (const auto& [t, u] : tempos)
            {
                if (t >= tick) break;
                s += (t - prevTick) / ppq * uspq * 1.0e-6;
                prevTick = t; uspq = u;
            }
            return s + (tick - prevTick) / ppq * uspq * 1.0e-6;
        };

        // Mapa de tempo en segundos (SMPTE: sin negras, 120 BPM fijos)
        if (division > 0)
        {
            song->tempoMap.clear();
            for (const auto& [tick, uspq] : tempos)
                if (uspq > 0.0) song->tempoMap.push_back ({ seconds (tick), 60.0e6 / uspq });
            if (song->tempoMap.empty()) song->tempoMap.push_back ({ 0.0, 120.0 });
        }

        // Pistas lógicas, nombres y notas
        std::map<std::pair<int, int>, int> index;
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            juce::String name;
            std::set<int> channels;
            for (const auto* e : *mf.getTrack (t))
            {
                const auto& m = e->message;
                if (name.isEmpty() && m.isTrackNameEvent()) name = m.getTextFromTextMetaEvent().trim();
                if (m.isNoteOn()) channels.insert (m.getChannel());
            }
            for (const auto ch : channels)
            {
                Track k;
                k.fileTrack = t;
                k.channel = ch;
                k.drums = ch == 10;
                k.name = name.isNotEmpty() ? name : "Track " + juce::String (t + 1);
                if (channels.size() > 1) k.name << " · ch " << ch;
                index[{ t, ch }] = (int) song->tracks.size();
                song->tracks.push_back (k);
                song->notes.emplace_back();
            }
            // Notas: cada note-on se cierra con el siguiente note-off del mismo tono y canal (o con el final de la pista)
            std::array<std::vector<std::pair<double, int>>, 16 * 128> open; // por canal y tono: (inicio en ticks, velocidad)
            double trackEnd = 0.0;
            for (const auto* e : *mf.getTrack (t))
            {
                const auto& m = e->message;
                trackEnd = std::max (trackEnd, m.getTimeStamp());
                if (! m.isNoteOnOrOff()) continue;
                const auto slot = (size_t) ((m.getChannel() - 1) * 128 + m.getNoteNumber());
                if (m.isNoteOn())
                    open[slot].push_back ({ m.getTimeStamp(), m.getVelocity() });
                else if (! open[slot].empty())
                {
                    const auto [on, vel] = open[slot].front();
                    open[slot].erase (open[slot].begin());
                    const auto k = index[{ t, m.getChannel() }];
                    song->notes[(size_t) k].push_back ({ seconds (on), seconds (m.getTimeStamp()), m.getNoteNumber(), vel });
                }
            }
            for (size_t slot = 0; slot < open.size(); ++slot)
                for (const auto& [on, vel] : open[slot])
                    song->notes[(size_t) index[{ t, (int) (slot / 128) + 1 }]].push_back ({ seconds (on), seconds (trackEnd), (int) (slot % 128), vel });
            // Primer Program Change de cada canal de la pista
            for (const auto* e : *mf.getTrack (t))
                if (const auto& m = e->message; m.isProgramChange())
                    if (const auto it = index.find ({ t, m.getChannel() }); it != index.end() && song->tracks[(size_t) it->second].program < 0)
                        song->tracks[(size_t) it->second].program = m.getProgramChangeNumber();
        }
        // Tesitura, tono medio, duración y sugerencia
        size_t most = 0;
        for (size_t k = 0; k < song->tracks.size(); ++k)
        {
            auto& n = song->notes[k];
            std::stable_sort (n.begin(), n.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
            double sum = 0.0;
            for (const auto& x : n)
            {
                song->tracks[k].low = std::min (song->tracks[k].low, x.pitch);
                song->tracks[k].high = std::max (song->tracks[k].high, x.pitch);
                sum += x.pitch;
                song->length = std::max (song->length, x.end);
            }
            song->tracks[k].meanPitch = n.empty() ? 0.0 : sum / (double) n.size();
            most = std::max (most, n.size());
        }
        song->length = std::max (song->length, seconds (lastTick));
        double best = -1.0;
        for (size_t k = 0; k < song->tracks.size(); ++k)
            if (! song->tracks[k].drums && song->notes[k].size() * 4 >= most && song->tracks[k].meanPitch > best)
            {
                best = song->tracks[k].meanPitch;
                song->suggested = (int) k;
            }
        // Compases y tiempos hasta un compás después del final
        const auto endTick = division > 0 ? lastTick : song->length * 2.0 * ppq; // SMPTE: 120 BPM = 2 negras por segundo
        const auto tickSeconds = [&] (double tick) { return division > 0 ? seconds (tick) : tick / (2.0 * ppq); };
        double tick = 0.0;
        size_t sig = 0;
        while (tick <= endTick + 1.0 && song->bars.size() < 100000)
        {
            while (sig + 1 < signatures.size() && signatures[sig + 1][0] <= tick + 0.5) ++sig;
            const auto beat = ppq * 4.0 / signatures[sig][2];
            const auto barTicks = beat * signatures[sig][1];
            song->bars.push_back (tickSeconds (tick));
            for (int b = 0; b < (int) signatures[sig][1]; ++b)
                song->beats.push_back (tickSeconds (tick + b * beat));
            tick += barTicks;
        }
        song->bars.push_back (tickSeconds (tick));
        if (song->bars.size() >= 2)
            song->firstBarSeconds = juce::jlimit (1.0, 6.0, song->bars[1] - song->bars[0]);
        return song;
    }

    void prepareStatus()
    {
        status.clear();
        for (const auto& n : notes)
        {
            status.push_back (std::make_unique<std::atomic<juce::uint8>[]> (std::max<size_t> (1, n.size())));
            for (size_t i = 0; i < n.size(); ++i) status.back()[i].store (pending);
        }
    }

    // Pista lógica de un evento de la pista 'fileTrack' del fichero (-1: ninguna)
    int trackOf (int fileTrack, const juce::MidiMessage& m) const
    {
        if (m.isSysEx() || m.isMetaEvent()) return -1;
        for (size_t k = 0; k < tracks.size(); ++k)
            if (tracks[k].fileTrack == fileTrack && tracks[k].channel == m.getChannel())
                return (int) k;
        return -1;
    }
};
