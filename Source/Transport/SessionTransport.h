// Se incluye desde KoniSynth.cpp (y desde Bench/KoniBench.cpp para su prueba unitaria). Solo usa JUCE.
#pragma once
#include <bitset>

//==============================================================================
// Transporte de sesión: REC / STOP / PLAY-PAUSE.
// - REC graba la salida final a WAV (32 bit float a la frecuencia de la sesión: sin recorte si el limitador está apagado)
//   y, a la vez, el MIDI que entra al motor (controlador, teclado en pantalla y fichero en reproducción). Al parar, el
//   MIDI se guarda como .mid junto al WAV (960 PPQ, tempo del momento de empezar).
// - PLAY reproduce la fuente cargada: un fichero de audio (se suma a la salida) o un .mid (sus eventos entran al motor
//   como si llegaran del controlador). Tras grabar, la fuente pasa a ser la toma recién grabada.
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
        mf.convertTimestampTicksToSeconds();
        std::vector<PlayEvent> events;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            for (const auto* e : *mf.getTrack (t))
                if (const auto& m = e->message; m.isNoteOnOrOff() || m.isController() || m.isPitchWheel() || m.isAftertouch()
                                                 || m.isChannelPressure() || m.isProgramChange()
                                                 || m.isSysEx()) // SysEx: patches propios y textos del LCD de los .mid del MT-32
                    events.push_back ({ m.getTimeStamp(), m });
        // Por tiempo; a igualdad, las notas apagadas primero (una nota repetida no se corta a sí misma)
        std::stable_sort (events.begin(), events.end(), [] (const PlayEvent& a, const PlayEvent& b)
                          { return a.seconds != b.seconds ? a.seconds < b.seconds : (a.message.isNoteOff() && ! b.message.isNoteOff()); });
        if (events.empty())
            return "the MIDI file has no notes or controllers: " + file.getFileName();
        const juce::ScopedLock cl (controlLock);
        stopPlaybackLocked();
        {
            const juce::ScopedLock rl (readerLock);
            reader.reset();
            const juce::SpinLock::ScopedLockType sl (playLock);
            midiEvents = std::move (events);
            midiCursor = 0;
            lengthSeconds.store (midiEvents.back().seconds);
            source.store (Source::midi);
        }
        sourceName = file.getFileName();
        return {};
    }

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
    Source getSource() const    { return source.load(); }
    juce::String getSourceName() const { const juce::ScopedLock cl (controlLock); return sourceName; }
    juce::File lastRecording() const   { const juce::ScopedLock cl (controlLock); return lastTake; }
    bool hasTakeMidi() const    { const juce::ScopedLock tl (takeLock); return take.getNumEvents() > 0 || midiFifo.getNumReady() > 0; }
    double recordedSeconds() const { return (double) recFrames.load() / deviceRate.load(); }
    double positionSeconds() const { return rewind.load() ? 0.0 : (double) playPos.load() / deviceRate.load(); }
    double getLengthSeconds() const { return lengthSeconds.load(); }
    juce::int64 getDrops() const { return droppedAudio.load() + droppedMidi.load() + underruns.load(); }

    //==========================================================================
    // Hilo de audio

    // Al principio del bloque, antes de leer el MIDI (pitch bend, CC): añade los eventos del .mid que caen en este bloque
    // y, tras PAUSE / STOP / fin, apaga las notas que dejó sonando
    void injectMidi (juce::MidiBuffer& midi, int numSamples)
    {
        if (rewind.exchange (false))
        {
            playPos.store (0);
            midiCursor = 0;
        }
        if (flushNotes.exchange (false))
            releaseHeldNotes (midi, 0);
        if (! playing.load (std::memory_order_acquire) || source.load() != Source::midi)
            return;
        const juce::SpinLock::ScopedTryLockType tl (playLock);
        if (! tl.isLocked())
            return; // el hilo de mensajes cambia la fuente ahora mismo: los eventos salen en el bloque siguiente
        const auto fs = deviceRate.load();
        const auto start = playPos.load();
        const auto end = start + numSamples;
        for (; midiCursor < midiEvents.size(); ++midiCursor)
        {
            const auto& e = midiEvents[midiCursor];
            const auto t = (juce::int64) std::llround (e.seconds * fs);
            if (t >= end)
                break;
            const auto offset = (int) juce::jlimit<juce::int64> (0, numSamples - 1, t - start);
            midi.addEvent (e.message, offset);
            track (e.message);
        }
        if (midiCursor >= midiEvents.size()) // fin del fichero: para, apaga lo que quede y vuelve al principio
        {
            playing.store (false);
            releaseHeldNotes (midi, numSamples - 1);
            rewind.store (true);
        }
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
            if (source.load() == Source::midi)
                playPos.fetch_add (n);
            else if (source.load() == Source::audio && seekGen.load() == appliedSeekGen.load())
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
    struct PlayEvent { double seconds; juce::MidiMessage message; };
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
    juce::SpinLock playLock; // fuente y FIFO de audio frente al hilo de audio
    std::atomic<Source> source { Source::none };
    std::atomic<bool> playing { false }, rewind { false }, flushNotes { false }, fileEnded { false };
    std::atomic<juce::int64> playPos { 0 }, underruns { 0 };
    std::atomic<double> lengthSeconds { 0.0 };
    std::atomic<juce::uint32> seekGen { 0 }, appliedSeekGen { 0 };
    std::vector<PlayEvent> midiEvents;
    size_t midiCursor = 0;
    std::array<std::bitset<128>, 16> held {};
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
