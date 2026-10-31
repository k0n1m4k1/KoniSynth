// Se incluye desde KoniSynth.cpp (y desde Bench/KoniBench.cpp para su prueba unitaria). Solo usa JUCE.
#pragma once
#include <bitset>
#include "PlayAlong.h"

//==============================================================================
// Transporte de sesión: REC / STOP / PLAY-PAUSE.
// - REC graba la salida final a WAV (32 bit float a la frecuencia de la sesión: sin recorte si el limitador está apagado)
//   y, a la vez, el MIDI que entra al motor (controlador, teclado en pantalla y fichero en reproducción). Al parar, el
//   MIDI se guarda como .mid junto al WAV (960 PPQ, tempo del momento de empezar).
// - PLAY reproduce la fuente cargada: un fichero de audio (se suma a la salida) o un .mid (sus eventos entran al motor
//   como si llegaran del controlador). Tras grabar, la fuente pasa a ser la toma recién grabada.
// - Práctica sobre un .mid (play-along, Transport/PlayAlong.h): eliges una pista ("tu parte"); sus notas no suenan, tus
//   mensajes de canal van a su canal (suenas con su instrumento) y tus notas se comparan con las suyas. Modos: Play (el
//   .mid sigue a su tempo; acierto si tocas el tono a ±150 ms), Wait (se para en cada nota o acorde tuyo hasta que lo tocas)
//   y Follow (como Wait y, si te adelantas, la banda salta a tu nota: tu ritmo la acelera). Tempo del .mid de 25 a 150 %,
//   bucle A-B y, practicando, cuenta previa de un compás. Sin pista elegida y al 100 %, el reloj es el de siempre: las
//   mismas muestras enteras (bit a bit)
//
// Hilos: el de audio no asigna memoria propia, no bloquea ni toca el disco. El WAV lo escribe un TimeSliceThread propio
// (ThreadedWriter, FIFO de 4 s); la lectura del audio va por delante en ese mismo hilo hacia una FIFO de 1 s, y el MIDI
// grabado sale por otra FIFO que ese hilo vacía. Lo compartido con el hilo de audio va tras SpinLock: el de audio solo
// hace try-lock (si falla, ese bloque no graba / no reproduce y se cuenta en drops), los demás bloquean lo que dura una
// copia de memoria. Inactivo, el coste por bloque es leer dos atómicos.
class SessionTransport : private juce::TimeSliceClient
{
public:
    enum class Source { none, audio, midi };

    ~SessionTransport() override
    {
        stopRecording();
        if (thread != nullptr)
        {
            thread->removeTimeSliceClient (this);
            thread->stopThread (2000);
        }
    }

    //==========================================================================
    // Hilo de mensajes (o prepareToPlay)

    void prepare (double sampleRate)
    {
        const juce::ScopedLock cl (controlLock);
        if (sampleRate <= 0.0 || sampleRate == deviceRate.load())
            return;
        stopLocked(); // otra frecuencia: la toma en curso y la posición ya no valen
        deviceRate.store (sampleRate);
    }

    static juce::File recordingsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KoniSynth").getChildFile ("Recordings");
    }

    static juce::File newRecordingFile()
    {
        return recordingsFolder().getNonexistentChildFile ("KoniSynth_" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S"), ".wav", false);
    }

    // Empieza a grabar en 'file' (WAV). bpm: tempo de referencia del .mid. Devuelve un error vacío si arranca
    juce::String startRecording (const juce::File& file, double bpm)
    {
        const juce::ScopedLock cl (controlLock);
        if (recording.load())
            return {};
        ensureThread();
        if (! file.getParentDirectory().createDirectory() || (file.existsAsFile() && ! file.deleteFile()))
            return "cannot write to " + file.getParentDirectory().getFullPathName();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
            return "cannot create " + file.getFullPathName();
        const auto fs = deviceRate.load();
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (2).withBitsPerSample (32)
                                                  .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (w == nullptr)
            return "cannot create a WAV writer for " + file.getFullPathName();
        auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), *thread, juce::roundToInt (fs * 4.0));
        threaded->setFlushInterval (juce::roundToInt (fs * 10.0)); // cabecera al día cada 10 s: un cierre brusco conserva casi todo
        {
            const juce::ScopedLock tl (takeLock);
            take.clear();
            midiFifo.reset(); // nadie escribe: recording es false
        }
        recFrames.store (0);
        droppedAudio.store (0);
        droppedMidi.store (0);
        recBpm = bpm > 0.0 ? bpm : 120.0;
        recFile = file;
        {
            const juce::SpinLock::ScopedLockType sl (recordLock);
            writer = std::move (threaded);
        }
        recording.store (true, std::memory_order_release);
        return {};
    }

    // Para la grabación: cierra el WAV, guarda el .mid hermano (si hubo MIDI) y deja la toma como fuente. Devuelve el WAV
    juce::File stopRecording()
    {
        const juce::ScopedLock cl (controlLock);
        return stopRecordingLocked();
    }

    void play()
    {
        const juce::ScopedLock cl (controlLock);
        if (source.load() != Source::none)
            playing.store (true, std::memory_order_release);
    }

    void pause()
    {
        const juce::ScopedLock cl (controlLock);
        if (playing.exchange (false))
            flushNotes.store (true);
    }

    // PANIC (hilo de audio, sin bloqueos): pausa la reproducción como pause() y suelta las notas del .mid; REC sigue
    void pauseFromAudioThread()
    {
        if (playing.exchange (false))
            flushNotes.store (true);
    }

    // STOP: para la grabación y la reproducción y vuelve al principio
    void stop()
    {
        const juce::ScopedLock cl (controlLock);
        stopLocked();
    }

    juce::String loadAudio (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
        if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0.0)
            return "unsupported or empty audio file: " + file.getFileName();
        const juce::ScopedLock cl (controlLock);
        stopPlaybackLocked();
        ensureThread();
        {
            const juce::ScopedLock rl (readerLock);
            reader = std::move (r);
            readerPos = 0;
            producedFrames = 0;
            for (auto& i : interpolators) i.reset();
            fileEnded.store (false);
            appliedSeekGen.store (seekGen.load());
            const juce::SpinLock::ScopedLockType sl (playLock);
            audioFifo.reset();
            midiEvents.clear();
            lengthSeconds.store ((double) reader->lengthInSamples / reader->sampleRate);
            source.store (Source::audio);
        }
        sourceName = file.getFileName();
        return {};
    }

    juce::String loadMidi (const juce::File& file)
    {
        juce::FileInputStream in (file);
        juce::MidiFile mf;
        if (! in.openedOk() || ! mf.readFrom (in, true))
            return "not a valid MIDI file: " + file.getFileName();
        auto analysed = PlayAlongSong::analyse (mf); // en ticks: compases y tempo
        analysed->prepareStatus();
        mf.convertTimestampTicksToSeconds();
        std::vector<PlayEvent> events;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            for (const auto* e : *mf.getTrack (t))
                if (const auto& m = e->message; m.isNoteOnOrOff() || m.isController() || m.isPitchWheel() || m.isAftertouch()
                                                 || m.isChannelPressure() || m.isProgramChange()
                                                 || m.isSysEx()) // SysEx: patches propios y textos del LCD de los .mid del MT-32
                    events.push_back ({ m.getTimeStamp(), m, analysed->trackOf (t, m) });
        // Por tiempo; a igualdad, las notas apagadas primero (una nota repetida no se corta a sí misma)
        std::stable_sort (events.begin(), events.end(), [] (const PlayEvent& a, const PlayEvent& b)
                          { return a.seconds != b.seconds ? a.seconds < b.seconds : (a.message.isNoteOff() && ! b.message.isNoteOff()); });
        if (events.empty())
            return "the MIDI file has no notes or controllers: " + file.getFileName();
        const juce::ScopedLock cl (controlLock);
        stopPlaybackLocked();
        std::shared_ptr<PlayAlongSong> old;
        {
            const juce::ScopedLock rl (readerLock);
            reader.reset();
            const juce::SpinLock::ScopedLockType sl (playLock);
            midiEvents = std::move (events);
            midiCursor = 0;
            lengthSeconds.store (midiEvents.back().seconds);
            old = std::exchange (song, analysed); // el anterior se libera fuera del SpinLock
            selectedTrack.store (-1);
            for (auto& m : muted) m.store (false);
            for (auto& s : soloed) s.store (false);
            soloCount.store (0);
            loopA.store (-1.0);
            loopB.store (-1.0);
            leadIn.store (analysed->firstBarSeconds);
            source.store (Source::midi);
        }
        sourceName = file.getFileName();
        return {};
    }

    //==========================================================================
    // Práctica sobre el .mid (hilo de mensajes)

    // Análisis del .mid cargado (pistas, notas, compases, estado de cada nota); nullptr sin .mid
    std::shared_ptr<const PlayAlongSong> getSong() const
    {
        const juce::SpinLock::ScopedLockType sl (playLock);
        return source.load() == Source::midi ? song : nullptr;
    }

    // Tu parte: índice de pista lógica, o -1 (suenan todas). Sueltan las notas del .mid y el estado empieza aquí
    void selectTrack (int t)
    {
        const juce::ScopedLock cl (controlLock);
        if (selectedTrack.exchange (t) == t)
            return;
        flushNotes.store (true);
        reseek.store (true);
        if (! playing.load() && midiPos.load() <= 0.0)
            rewind.store (true); // aún en el principio: con la pista elegida, empieza con la cuenta previa
    }
    int getSelectedTrack() const { return selectedTrack.load(); }

    // Mute y solo por pista lógica (máximo maxTracks). Con algún solo, solo suenan las pistas en solo. Solo se descartan los
    // note-on de las silenciadas (los note-off pasan: ninguna nota se queda colgada; la que sonaba acaba a su hora)
    static constexpr int maxTracks = 256;
    void setTrackMute (int t, bool m) { if (juce::isPositiveAndBelow (t, maxTracks)) muted[(size_t) t].store (m); }
    void setTrackSolo (int t, bool s)
    {
        if (juce::isPositiveAndBelow (t, maxTracks) && soloed[(size_t) t].exchange (s) != s)
            soloCount.fetch_add (s ? 1 : -1);
    }
    bool isTrackMuted (int t) const  { return juce::isPositiveAndBelow (t, maxTracks) && muted[(size_t) t].load(); }
    bool isTrackSoloed (int t) const { return juce::isPositiveAndBelow (t, maxTracks) && soloed[(size_t) t].load(); }
    bool isTrackSilenced (int t) const
    {
        return juce::isPositiveAndBelow (t, maxTracks) && (muted[(size_t) t].load() || (soloCount.load() > 0 && ! soloed[(size_t) t].load()));
    }

    enum PracticeMode { practicePlay = 0, practiceWait, practiceFollow };
    void setPracticeMode (int m) { practiceMode.store (juce::jlimit (0, 2, m)); reseek.store (true); }
    int getPracticeMode() const { return practiceMode.load(); }

    // Tempo del .mid (1 = el del fichero), de 0,25 a 1,5
    void setTempoScale (float s) { tempoScale.store (juce::jlimit (0.25f, 1.5f, s)); }
    float getTempoScale() const { return tempoScale.load(); }

    // Bucle A-B en segundos del fichero (b <= a: sin bucle)
    void setLoop (double a, double b) { loopA.store (juce::jmax (0.0, a)); loopB.store (b); }
    void clearLoop() { loopA.store (-1.0); loopB.store (-1.0); }
    double getLoopA() const { return loopA.load(); }
    double getLoopB() const { return loopB.load(); }

    // Salto a un instante del .mid (s, sin cuenta previa), reproduciendo o en pausa; se aplica en el siguiente bloque de audio
    void seekMidi (double seconds) { seekTarget.store (juce::jmax (0.0, seconds)); }

    // Posición en el .mid en segundos (negativa durante la cuenta previa) y si espera tu nota (Wait / Follow)
    double practicePosition() const { return midiPos.load() / deviceRate.load(); }
    bool isWaiting() const { return waiting.load(); }

    // Tus notas recientes para la vista (tiempos en segundos del .mid; off < on: aún pulsada; ok: acertó una nota de tu parte)
    struct UserNote { std::atomic<double> on { -1.0e9 }, off { -1.0e9 }; std::atomic<int> pitch { -1 }; std::atomic<bool> ok { false }; };
    static constexpr int numUserNotes = 64;
    const std::array<UserNote, numUserNotes>& getUserNotes() const { return userNotes; }

    // Exporta el MIDI de la última toma (grabada o en curso)
    juce::String exportMidi (const juce::File& file)
    {
        const juce::ScopedLock cl (controlLock);
        drainMidi();
        const juce::ScopedLock tl (takeLock);
        if (take.getNumEvents() == 0)
            return "the last recording has no MIDI";
        return writeMidiFile (take, deviceRate.load(), recBpm, file) ? juce::String() : "cannot write " + file.getFullPathName();
    }

    // Estado para la UI (hilo de mensajes)
    bool isRecording() const    { return recording.load(); }
    bool isPlaying() const      { return playing.load(); }
    bool isPracticing() const   { return source.load() == Source::midi && selectedTrack.load() >= 0; }
    Source getSource() const    { return source.load(); }
    juce::String getSourceName() const { const juce::ScopedLock cl (controlLock); return sourceName; }
    juce::File lastRecording() const   { const juce::ScopedLock cl (controlLock); return lastTake; }
    bool hasTakeMidi() const    { const juce::ScopedLock tl (takeLock); return take.getNumEvents() > 0 || midiFifo.getNumReady() > 0; }
    double recordedSeconds() const { return (double) recFrames.load() / deviceRate.load(); }
    double positionSeconds() const
    {
        if (rewind.load()) return 0.0;
        return source.load() == Source::midi ? juce::jmax (0.0, midiPos.load() / deviceRate.load()) : (double) playPos.load() / deviceRate.load();
    }
    double getLengthSeconds() const { return lengthSeconds.load(); }
    juce::int64 getDrops() const { return droppedAudio.load() + droppedMidi.load() + underruns.load(); }

    //==========================================================================
    // Hilo de audio

    // Al principio del bloque, antes de leer el MIDI (pitch bend, CC): añade los eventos del .mid que caen en este bloque
    // y, tras PAUSE / STOP / fin, apaga las notas que dejó sonando. Reloj: midiPos, en muestras del fichero a la frecuencia
    // del dispositivo (double: entero mientras el tempo sea el 100 %, como el contador entero de antes); avanza
    // numSamples · tempo por bloque, menos en Wait / Follow si llega a una nota tuya sin tocar
    void injectMidi (juce::MidiBuffer& midi, int numSamples)
    {
        const auto fs = deviceRate.load();
        if (rewind.exchange (false))
        {
            playPos.store (0);
            midiCursor = 0;
            midiPos.store (selectedTrack.load() >= 0 ? -std::round (leadIn.load() * fs) : 0.0); // practicando: cuenta previa
            reseek.store (true);
        }
        if (flushNotes.exchange (false))
            releaseHeldNotes (midi, 0);
        if (source.load() == Source::midi && seekTarget.load() >= 0.0)
            applySeek (midi, fs);
        if (! playing.load (std::memory_order_acquire) || source.load() != Source::midi)
            return;
        const juce::SpinLock::ScopedTryLockType tl (playLock);
        if (! tl.isLocked())
            return; // el hilo de mensajes cambia la fuente ahora mismo: los eventos salen en el bloque siguiente
        const auto* s = song.get();
        const auto sel = selectedTrack.load();
        const auto practice = s != nullptr && sel >= 0 && sel < (int) s->tracks.size();
        const auto mode = practice ? practiceMode.load() : (int) practicePlay;
        const auto rate = (double) tempoScale.load();
        auto start = midiPos.load();
        if (practice && reseek.exchange (false))
            seekPractice (*s, sel, start / fs);

        // Wait / Follow: los grupos (nota o acorde) ya tocados quedan atrás; en Follow, si los tocaste antes de su hora, el
        // .mid salta hasta ellos (sin las notas que habría tocado entre medias)
        if (practice && mode != practicePlay)
        {
            const auto& notes = s->notes[(size_t) sel];
            auto jumpTo = start;
            while (nextNote < notes.size() && groupDone (*s, sel, nextNote))
            {
                const auto g = std::round (notes[nextNote].start * fs);
                if (mode == practiceFollow && g > jumpTo) jumpTo = g;
                nextNote = groupEnd (notes, nextNote);
            }
            if (jumpTo > start)
            {
                skipTo (midi, jumpTo, fs, sel);
                start = jumpTo;
            }
        }
        auto end = start + numSamples * rate;
        auto waitingNow = false;
        if (practice && mode != practicePlay && nextNote < s->notes[(size_t) sel].size())
        {
            const auto g = std::round (s->notes[(size_t) sel][nextNote].start * fs);
            if (end > g) { end = juce::jmax (start, g); waitingNow = true; }
        }
        waiting.store (waitingNow);
        // Bucle A-B: el bloque acaba en B y el siguiente empieza en A
        const auto a = std::round (loopA.load() * fs), b = std::round (loopB.load() * fs);
        const auto wrap = loopB.load() > loopA.load() && loopA.load() >= 0.0 && start < b && end >= b;
        if (wrap) end = b;

        for (; midiCursor < midiEvents.size(); ++midiCursor)
        {
            const auto& e = midiEvents[midiCursor];
            const auto t = (double) std::llround (e.seconds * fs);
            if (t >= end)
                break;
            if (practice && e.track == sel && e.message.isNoteOnOrOff())
                continue; // tu parte: la tocas tú (sus CC y Program Change sí entran: tu instrumento y tu volumen)
            if (e.message.isNoteOn() && isTrackSilenced (e.track))
                continue; // pista en mute (o fuera del solo)
            const auto offset = (int) juce::jlimit (0.0, (double) (numSamples - 1), std::floor ((t - start) / rate));
            midi.addEvent (e.message, offset);
            track (e.message);
        }
        // Play: las notas tuyas que pasan de ±hitWindow sin tocar, falladas
        if (practice && mode == practicePlay)
        {
            const auto& notes = s->notes[(size_t) sel];
            while (missNote < notes.size() && (notes[missNote].start + hitWindow) * fs < end)
            {
                if (s->statusOf (sel, (int) missNote) == PlayAlongSong::pending)
                    s->setStatus (sel, (int) missNote, PlayAlongSong::missed);
                ++missNote;
            }
        }
        if (wrap)
        {
            releaseHeldNotes (midi, numSamples - 1);
            midiPos.store (a);
            midiCursor = 0;
            while (midiCursor < midiEvents.size() && (double) std::llround (midiEvents[midiCursor].seconds * fs) < a)
                ++midiCursor;
            if (practice)
                seekPractice (*s, sel, a / fs);
            return;
        }
        midiPos.store (end);
        const auto practiceDone = ! practice || mode == practicePlay || nextNote >= s->notes[(size_t) sel].size();
        if (midiCursor >= midiEvents.size() && practiceDone) // fin del fichero: para, apaga lo que quede y vuelve al principio
        {
            playing.store (false);
            releaseHeldNotes (midi, numSamples - 1);
            rewind.store (true);
        }
    }

    // Hilo de audio, antes de injectMidi (el controlador) y con el teclado en pantalla aparte: practicando, tus mensajes de
    // canal pasan al canal de tu pista y tus notas se comparan con las suyas. Sin pista elegida, no toca nada
    void routeUserMidi (juce::MidiBuffer& midi)
    {
        const auto sel = selectedTrack.load();
        if (sel < 0 || source.load() != Source::midi || midi.isEmpty())
            return;
        const juce::SpinLock::ScopedTryLockType tl (playLock);
        if (! tl.isLocked())
            return;
        const auto* s = song.get();
        if (s == nullptr || sel >= (int) s->tracks.size())
            return;
        const auto channel = s->tracks[(size_t) sel].channel;
        const auto fs = deviceRate.load();
        const auto running = playing.load();
        const auto rate = (double) tempoScale.load();
        userScratch.clear();
        for (const auto metadata : midi)
        {
            if (metadata.numBytes < 1 || metadata.numBytes > 3 || metadata.data[0] < 0x80 || metadata.data[0] >= 0xF0)
            {
                userScratch.addEvent (metadata.data, metadata.numBytes, metadata.samplePosition);
                continue;
            }
            juce::uint8 bytes[3] {};
            std::copy (metadata.data, metadata.data + metadata.numBytes, bytes);
            bytes[0] = (juce::uint8) ((bytes[0] & 0xF0) | (channel - 1));
            userScratch.addEvent (bytes, metadata.numBytes, metadata.samplePosition);
            const auto kind = bytes[0] & 0xF0;
            const auto when = (midiPos.load() + metadata.samplePosition * rate) / fs;
            if (metadata.numBytes == 3 && kind == 0x90 && bytes[2] > 0)
                userNoteOn (*s, sel, bytes[1], when, running);
            else if (metadata.numBytes == 3 && (kind == 0x80 || kind == 0x90))
                userNoteOff (bytes[1], when);
        }
        midi.clear(); // misma cantidad de datos: cabe sin reservar memoria
        midi.addEvents (userScratch, 0, -1, 0);
    }

    // Tras mezclar el teclado en pantalla: copia a la toma todo lo que entra al motor
    void captureMidi (const juce::MidiBuffer& midi)
    {
        if (! recording.load (std::memory_order_acquire))
            return;
        const auto base = recFrames.load();
        for (const auto metadata : midi)
        {
            if (metadata.numBytes > 3 || metadata.numBytes <= 0)
                continue; // SysEx y meta: fuera
            const auto scope = midiFifo.write (1);
            if (scope.blockSize1 + scope.blockSize2 == 0) { droppedMidi.fetch_add (1); continue; }
            auto& e = captured[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
            e.time = base + metadata.samplePosition;
            e.size = (juce::uint8) metadata.numBytes;
            std::copy (metadata.data, metadata.data + metadata.numBytes, e.bytes);
        }
    }

    // Al final de la cadena: suma el audio en reproducción y graba lo que sale
    void processAudio (juce::AudioBuffer<float>& buffer)
    {
        const auto n = buffer.getNumSamples();
        const auto chans = buffer.getNumChannels();
        if (playing.load (std::memory_order_acquire))
        {
            if (source.load() == Source::audio && seekGen.load() == appliedSeekGen.load()) // el .mid avanza en injectMidi
            {
                const juce::SpinLock::ScopedTryLockType tl (playLock);
                int got = 0;
                if (tl.isLocked())
                {
                    const auto scope = audioFifo.read (juce::jmin (n, audioFifo.getNumReady()));
                    const auto mix = [&] (int from, int count, int at)
                    {
                        for (int ch = 0; ch < chans; ++ch)
                            if (chans == 1)
                                for (int i = 0; i < count; ++i)
                                    buffer.addSample (0, at + i, 0.5f * (fifoData.getSample (0, from + i) + fifoData.getSample (1, from + i)));
                            else
                                buffer.addFrom (ch, at, fifoData, juce::jmin (ch, 1), from, count);
                    };
                    if (scope.blockSize1 > 0) mix (scope.startIndex1, scope.blockSize1, 0);
                    if (scope.blockSize2 > 0) mix (scope.startIndex2, scope.blockSize2, scope.blockSize1);
                    got = scope.blockSize1 + scope.blockSize2;
                }
                playPos.fetch_add (got);
                if (got < n)
                {
                    if (fileEnded.load() && audioFifo.getNumReady() == 0) // fin del fichero
                    {
                        playing.store (false);
                        rewind.store (true);
                        seekGen.fetch_add (1);
                    }
                    else
                        underruns.fetch_add (1);
                }
            }
        }
        if (recording.load (std::memory_order_acquire))
        {
            const juce::SpinLock::ScopedTryLockType tl (recordLock);
            const float* channels[2] = { buffer.getReadPointer (0), buffer.getReadPointer (juce::jmin (1, chans - 1)) };
            if (tl.isLocked() && writer != nullptr && writer->write (channels, n))
                recFrames.fetch_add (n);
            else
                droppedAudio.fetch_add (n);
        }
    }

    // Escribe una secuencia con marcas de tiempo en muestras como .mid tipo 1 (960 PPQ, tempo bpm, 4/4). Error de
    // redondeo ≤ medio tick: 0,26 ms a 120 BPM
    static bool writeMidiFile (const juce::MidiMessageSequence& samples, double fs, double bpm, const juce::File& file)
    {
        constexpr int ppq = 960;
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)), 0.0);
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
        double last = 0.0;
        for (const auto* e : samples)
        {
            auto m = e->message;
            last = std::round (m.getTimeStamp() / fs * bpm / 60.0 * ppq);
            m.setTimeStamp (last);
            track.addEvent (m);
        }
        track.updateMatchedPairs();
        auto eot = juce::MidiMessage::endOfTrack();
        eot.setTimeStamp (last);
        track.addEvent (eot);
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (ppq);
        mf.addTrack (track);
        if (file.existsAsFile() && ! file.deleteFile())
            return false;
        juce::FileOutputStream out (file);
        return out.openedOk() && mf.writeTo (out, 1);
    }

private:
    struct PlayEvent { double seconds; juce::MidiMessage message; int track = -1; }; // track: pista lógica (PlayAlongSong)

    // Práctica: margen de acierto en Play (s) y de acorde (notas que empiezan a la vez, s)
    static constexpr double hitWindow = 0.15, chordWindow = 0.08;

    static size_t groupEnd (const std::vector<PlayAlongSong::Note>& notes, size_t i)
    {
        auto j = i;
        while (j < notes.size() && notes[j].start <= notes[i].start + chordWindow) ++j;
        return j;
    }

    static bool groupDone (const PlayAlongSong& s, int t, size_t i)
    {
        for (auto j = i; j < groupEnd (s.notes[(size_t) t], i); ++j)
            if (s.statusOf (t, (int) j) != PlayAlongSong::hit) return false;
        return true;
    }

    // Nueva posición (inicio, pista elegida, bucle, cambio de modo): las notas tuyas desde ahí vuelven a pendientes
    void seekPractice (const PlayAlongSong& s, int t, double seconds)
    {
        const auto& notes = s.notes[(size_t) t];
        nextNote = 0;
        while (nextNote < notes.size() && notes[nextNote].start < seconds - hitWindow) ++nextNote;
        missNote = nextNote;
        for (auto i = nextNote; i < notes.size(); ++i)
            s.setStatus (t, (int) i, PlayAlongSong::pending);
    }

    // Follow: salto hacia delante. Entran los eventos que no son notas (programas, CC, pitch bend) y las notas apagadas
    // de lo que sonaba; las que habrían empezado entre medias, no
    void skipTo (juce::MidiBuffer& midi, double target, double fs, int sel)
    {
        for (; midiCursor < midiEvents.size(); ++midiCursor)
        {
            const auto& e = midiEvents[midiCursor];
            if ((double) std::llround (e.seconds * fs) >= target) break;
            const auto& m = e.message;
            if (m.isNoteOn() || (e.track == sel && m.isNoteOnOrOff())) continue;
            if (m.isNoteOff() && ! held[(size_t) juce::jlimit (1, 16, m.getChannel()) - 1].test ((size_t) m.getNoteNumber())) continue;
            midi.addEvent (m, 0);
            track (m);
        }
    }

    // Salto pedido con seekMidi: suelta lo que suena, coloca el reloj y el cursor, y persigue el estado de cada canal hasta
    // ese instante (último Program Change, controladores 0-119 y pitch bend), para que cada parte suene con su instrumento,
    // volumen y paneo. Coste: un recorrido de los eventos anteriores (≈ 10^4 en un .mid de juego, del orden de 0,1 ms)
    void applySeek (juce::MidiBuffer& midi, double fs)
    {
        const juce::SpinLock::ScopedTryLockType tl (playLock);
        if (! tl.isLocked())
            return; // se reintenta en el bloque siguiente
        const auto target = seekTarget.exchange (-1.0);
        releaseHeldNotes (midi, 0);
        chaseProgram.fill (-1);
        chaseBend.fill (-1);
        for (auto& c : chaseCc) c.fill (-1);
        const auto at = std::round (target * fs);
        midiCursor = 0;
        for (; midiCursor < midiEvents.size() && (double) std::llround (midiEvents[midiCursor].seconds * fs) < at; ++midiCursor)
        {
            const auto& m = midiEvents[midiCursor].message;
            const auto ch = (size_t) juce::jlimit (1, 16, m.getChannel()) - 1;
            if (m.isProgramChange()) chaseProgram[ch] = m.getProgramChangeNumber();
            else if (m.isController() && m.getControllerNumber() < 120) chaseCc[ch][(size_t) m.getControllerNumber()] = (juce::int16) m.getControllerValue();
            else if (m.isPitchWheel()) chaseBend[ch] = m.getPitchWheelValue();
        }
        for (int ch = 0; ch < 16; ++ch)
        {
            if (chaseProgram[(size_t) ch] >= 0) midi.addEvent (juce::MidiMessage::programChange (ch + 1, chaseProgram[(size_t) ch]), 0);
            for (int cc = 0; cc < 120; ++cc)
                if (cc != 64 && chaseCc[(size_t) ch][(size_t) cc] >= 0) // el pedal no: soltaría o sostendría notas que no suenan
                    midi.addEvent (juce::MidiMessage::controllerEvent (ch + 1, cc, chaseCc[(size_t) ch][(size_t) cc]), 0);
            if (chaseBend[(size_t) ch] >= 0) midi.addEvent (juce::MidiMessage::pitchWheel (ch + 1, chaseBend[(size_t) ch]), 0);
        }
        midiPos.store (at);
        playPos.store ((juce::int64) at);
        reseek.store (true);
    }

    void userNoteOn (const PlayAlongSong& s, int t, int pitch, double when, bool running)
    {
        auto match = -1;
        if (running)
        {
            const auto& notes = s.notes[(size_t) t];
            if (practiceMode.load() == practicePlay) // la pendiente de ese tono a ±hitWindow de tu instante
            {
                for (auto i = missNote; i < notes.size() && notes[i].start <= when + hitWindow; ++i)
                    if (notes[i].pitch == pitch && s.statusOf (t, (int) i) == PlayAlongSong::pending && std::abs (notes[i].start - when) <= hitWindow)
                    { match = (int) i; break; }
            }
            else // Wait / Follow: una nota del grupo que toca ahora
                for (auto i = nextNote; i < groupEnd (notes, nextNote); ++i)
                    if (notes[i].pitch == pitch && s.statusOf (t, (int) i) == PlayAlongSong::pending)
                    { match = (int) i; break; }
            if (match >= 0)
                s.setStatus (t, match, PlayAlongSong::hit);
        }
        auto& u = userNotes[(size_t) (userHead.fetch_add (1) % numUserNotes)];
        u.off.store (-1.0e9);
        u.on.store (when);
        u.pitch.store (pitch);
        u.ok.store (match >= 0);
    }

    void userNoteOff (int pitch, double when)
    {
        const auto head = userHead.load();
        for (int k = 1; k <= numUserNotes; ++k)
        {
            auto& u = userNotes[(size_t) ((head - k + numUserNotes * 4) % numUserNotes)];
            if (u.pitch.load() == pitch && u.off.load() < u.on.load()) { u.off.store (when); return; }
        }
    }
    struct CapturedEvent { juce::int64 time = 0; juce::uint8 bytes[3] {}; juce::uint8 size = 0; };

    void ensureThread()
    {
        if (thread != nullptr)
            return;
        thread = std::make_unique<juce::TimeSliceThread> ("KoniSynth transport");
        thread->addTimeSliceClient (this);
        thread->startThread (juce::Thread::Priority::normal);
    }

    juce::File stopRecordingLocked()
    {
        if (! recording.exchange (false))
            return {};
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
        {
            const juce::SpinLock::ScopedLockType sl (recordLock);
            old = std::move (writer);
        }
        old.reset(); // vuelca lo que queda en la FIFO y cierra el WAV (bloquea este hilo lo que tarde el disco)
        drainMidi();
        {
            const juce::ScopedLock tl (takeLock);
            if (take.getNumEvents() > 0)
                writeMidiFile (take, deviceRate.load(), recBpm, recFile.withFileExtension ("mid"));
        }
        lastTake = recFile;
        loadAudio (recFile); // la toma pasa a ser la fuente (controlLock es reentrante)
        return recFile;
    }

    void stopPlaybackLocked()
    {
        if (playing.exchange (false))
            flushNotes.store (true);
        rewind.store (true);
        seekGen.fetch_add (1);
    }

    void stopLocked()
    {
        stopRecordingLocked();
        stopPlaybackLocked();
    }

    // Notas que el .mid dejó encendidas (por canal) y sostenido: se apagan al parar
    void track (const juce::MidiMessage& m)
    {
        const auto ch = (size_t) juce::jlimit (1, 16, m.getChannel()) - 1;
        if (m.isNoteOn())
            held[ch].set ((size_t) m.getNoteNumber());
        else if (m.isNoteOff())
            held[ch].reset ((size_t) m.getNoteNumber());
        else if (m.isControllerOfType (64))
            sustained[ch] = m.getControllerValue() >= 64;
    }

    void releaseHeldNotes (juce::MidiBuffer& midi, int offset)
    {
        for (int ch = 0; ch < 16; ++ch)
        {
            for (int note = 0; note < 128; ++note)
                if (held[(size_t) ch].test ((size_t) note))
                    midi.addEvent (juce::MidiMessage::noteOff (ch + 1, note), offset);
            if (sustained[(size_t) ch])
                midi.addEvent (juce::MidiMessage::controllerEvent (ch + 1, 64, 0), offset);
            held[(size_t) ch].reset();
            sustained[(size_t) ch] = false;
        }
    }

    // Hilo del transporte: vacía la FIFO del MIDI grabado y mantiene llena la del audio en reproducción
    int useTimeSlice() override
    {
        {
            const juce::ScopedLock tl (takeLock);
            drainMidiLocked();
        }
        fillAudio();
        return playing.load() ? 5 : 20;
    }

    void drainMidi()
    {
        const juce::ScopedLock tl (takeLock);
        drainMidiLocked();
    }

    void drainMidiLocked()
    {
        const auto ready = midiFifo.getNumReady();
        if (ready == 0)
            return;
        const auto scope = midiFifo.read (ready);
        const auto copy = [this] (int from, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto& e = captured[(size_t) (from + i)];
                take.addEvent (juce::MidiMessage (e.bytes, e.size, (double) e.time));
            }
        };
        copy (scope.startIndex1, scope.blockSize1);
        copy (scope.startIndex2, scope.blockSize2);
    }

    // Lee del fichero fuera del SpinLock (el disco puede tardar milisegundos) y solo copia a la FIFO dentro de él
    void fillAudio()
    {
        if (source.load() != Source::audio)
            return;
        const juce::ScopedLock rl (readerLock);
        if (reader == nullptr)
            return;
        if (const auto gen = seekGen.load(); gen != appliedSeekGen.load())
        {
            readerPos = 0;
            producedFrames = 0;
            for (auto& i : interpolators) i.reset();
            fileEnded.store (false);
            {
                const juce::SpinLock::ScopedLockType sl (playLock);
                audioFifo.reset();
            }
            appliedSeekGen.store (gen);
        }
        const auto ratio = reader->sampleRate / deviceRate.load(); // muestras del fichero por muestra de salida
        const auto total = (juce::int64) std::floor ((double) reader->lengthInSamples / ratio);
        while (! fileEnded.load())
        {
            const auto want = (int) std::min<juce::int64> ({ (juce::int64) chunk, total - producedFrames, (juce::int64) audioFifo.getFreeSpace() });
            if (want <= 0)
            {
                if (producedFrames >= total) fileEnded.store (true);
                break;
            }
            const auto exact = std::abs (ratio - 1.0) < 1.0e-9;
            const auto inFrames = exact ? want : (int) std::ceil (want * ratio) + 4;
            inBuffer.setSize (2, inFrames, false, false, true);
            reader->read (&inBuffer, 0, inFrames, readerPos, true, true);
            if (reader->numChannels == 1)
                inBuffer.copyFrom (1, 0, inBuffer, 0, 0, inFrames);
            outBuffer.setSize (2, want, false, false, true);
            int used = want;
            if (exact)
                for (int ch = 0; ch < 2; ++ch)
                    outBuffer.copyFrom (ch, 0, inBuffer, ch, 0, want);
            else
                for (int ch = 0; ch < 2; ++ch)
                    used = interpolators[(size_t) ch].process (ratio, inBuffer.getReadPointer (ch), outBuffer.getWritePointer (ch), want);
            readerPos += used;
            producedFrames += want;
            {
                const juce::SpinLock::ScopedLockType sl (playLock);
                const auto scope = audioFifo.write (want);
                for (int ch = 0; ch < 2; ++ch)
                {
                    if (scope.blockSize1 > 0) fifoData.copyFrom (ch, scope.startIndex1, outBuffer, ch, 0, scope.blockSize1);
                    if (scope.blockSize2 > 0) fifoData.copyFrom (ch, scope.startIndex2, outBuffer, ch, scope.blockSize1, scope.blockSize2);
                }
            }
            if (producedFrames >= total)
                fileEnded.store (true);
        }
    }

    static constexpr int fifoFrames = 48000 * 2, chunk = 4096, midiCapacity = 8192;

    mutable juce::CriticalSection controlLock;            // órdenes del hilo de mensajes (y prepareToPlay)
    std::unique_ptr<juce::TimeSliceThread> thread; // se crea al primer uso; antes que writer: se destruye después
    std::atomic<double> deviceRate { 48000.0 };

    // Grabación
    juce::SpinLock recordLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> recFrames { 0 }, droppedAudio { 0 }, droppedMidi { 0 };
    juce::AbstractFifo midiFifo { midiCapacity };
    std::array<CapturedEvent, midiCapacity> captured {};
    mutable juce::CriticalSection takeLock;
    juce::MidiMessageSequence take; // marcas de tiempo en muestras desde el inicio de la toma
    double recBpm = 120.0;
    juce::File recFile, lastTake;

    // Reproducción
    mutable juce::SpinLock playLock; // fuente y FIFO de audio frente al hilo de audio
    std::atomic<Source> source { Source::none };
    std::atomic<bool> playing { false }, rewind { false }, flushNotes { false }, fileEnded { false };
    std::atomic<juce::int64> playPos { 0 }, underruns { 0 };
    std::atomic<double> lengthSeconds { 0.0 };
    std::atomic<juce::uint32> seekGen { 0 }, appliedSeekGen { 0 };
    std::vector<PlayEvent> midiEvents;
    size_t midiCursor = 0;
    // Práctica: análisis del .mid (se cambia bajo playLock), tu pista, modo, tempo, bucle y reloj del .mid
    std::shared_ptr<PlayAlongSong> song;
    std::atomic<int> selectedTrack { -1 }, practiceMode { practicePlay }, userHead { 0 };
    std::atomic<float> tempoScale { 1.0f };
    std::atomic<double> midiPos { 0.0 }, loopA { -1.0 }, loopB { -1.0 }, leadIn { 2.0 };
    std::atomic<bool> waiting { false }, reseek { false };
    size_t nextNote = 0, missNote = 0; // hilo de audio: siguiente grupo tuyo (Wait / Follow) y siguiente a dar por fallado (Play)
    std::array<UserNote, numUserNotes> userNotes;
    juce::MidiBuffer userScratch = [] { juce::MidiBuffer b; b.ensureSize (16384); return b; }();
    std::array<std::bitset<128>, 16> held {};
    std::atomic<double> seekTarget { -1.0 };                 // seekMidi: instante pedido (s), o < 0
    std::array<std::atomic<bool>, maxTracks> muted {}, soloed {}; // mute y solo por pista lógica
    std::atomic<int> soloCount { 0 };
    std::array<int, 16> chaseProgram {}, chaseBend {};       // applySeek: estado perseguido por canal (-1: sin dato)
    std::array<std::array<juce::int16, 120>, 16> chaseCc {};
    std::array<bool, 16> sustained {};
    juce::String sourceName;

    juce::CriticalSection readerLock; // lector del fichero: hilo del transporte frente al de mensajes
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::int64 readerPos = 0, producedFrames = 0;
    std::array<juce::LagrangeInterpolator, 2> interpolators;
    juce::AudioBuffer<float> inBuffer, outBuffer;
    juce::AbstractFifo audioFifo { fifoFrames };
    juce::AudioBuffer<float> fifoData { 2, fifoFrames };
};
