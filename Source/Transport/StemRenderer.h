// Se incluye desde KoniSynth.cpp tras KoniSynthProcessor (usa la clase completa). No es una cabecera independiente.
#pragma once

//==============================================================================
// Render de pistas ("stems") de un .mid, más rápido que el tiempo real: una instancia nueva del procesador por pista, con su
// estado (motor + preset), su propio reproductor (SessionTransport) filtrado a esa pista lógica y un WAV float de 32 bits
// escrito por bloques. El audio en vivo no se toca: es otra instancia.
// - Mismo reparto de eventos que al reproducir (SessionTransport::injectMidi: cada evento en llround(s·fs)), sin reloj del
//   host (como el standalone en vivo): el tempo y la colocación de las notas son los mismos. La aleatoriedad (deriva,
//   ruido) empieza en prepareToPlay, como al abrir el dispositivo: el render es reproducible, pero no igual bit a bit a una
//   escucha en vivo concreta (allí empieza al abrir el dispositivo, antes de pulsar PLAY).
// - Bloques fijos de 256 muestras (KoniBench usa el mismo tamaño: comparables).
// - Final: cuando el reproductor llega al final (y suelta sus notas), la cola sigue hasta 0,5 s seguidos por debajo de
//   -90 dBFS, con el tope de getTailLengthSeconds() (5 s; 30 s con Freeze). Todas las pistas acaban con la misma longitud
//   (se rellenan con silencio) y empiezan en 0: se alinean al importarlas en un DAW.
// - Hilos: las instancias se crean y se destruyen en el hilo de mensajes (llevan Timer); cada pista se renderiza en un hilo
//   de un ThreadPool de min(pistas, núcleos - 1). El último en acabar iguala las longitudes y cierra los ficheros.
// - Cancelar: cada bloque mira la bandera; los ficheros a medias se borran.
class StemRenderer
{
public:
    struct Stem
    {
        int track = -1;              // pista lógica del .mid (PlayAlongSong); -1 = todas (una instancia multitímbrica)
        juce::MemoryBlock baseState; // estado de partida (el de la instancia en vivo: afinación, semilla, ajustes)
        int presetIndex = -1;        // preset (PresetManager::getPresets) que se carga encima; -1 = el estado tal cual
        juce::File file;             // WAV de salida
    };

    // Resultado para la UI (hilo de mensajes)
    struct Result { bool ok = false, cancelled = false; juce::String message; juce::Array<juce::File> files; double seconds = 0.0; };

    StemRenderer() : pool (juce::ThreadPoolOptions{}.withThreadName ("KoniSynth stems")
                               .withNumberOfThreads (std::max (1, juce::SystemStats::getNumCpus() - 1))) {}

    ~StemRenderer()
    {
        alive->store (false); // un aviso pendiente de finish() ya no toca este objeto
        cancel();
        pool.removeAllJobs (true, 60000); // espera a que los hilos suelten las instancias
    }

    // Hilo de mensajes. Prepara las instancias y arranca. onDone llega en el hilo de mensajes. Devuelve un error vacío si arranca
    juce::String start (const juce::File& midiFile, std::vector<Stem> stems, double sampleRate, std::function<void (const Result&)> onDone)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (running.load())
            return "a render is already running";
        if (stems.empty())
            return "no tracks to render";
        jobs.clear();
        auto fail = [this] (const juce::String& e) { jobs.clear(); return e; };
        cancelled.store (false);
        failed.store (false);
        done = std::move (onDone);
        rate = sampleRate;
        for (auto& s : stems)
        {
            auto job = std::make_unique<Job> (*this);
            job->stem = std::move (s);
            auto& p = job->processor;
            p = std::make_unique<KoniSynthProcessor> (true);
            if (job->stem.baseState.getSize() > 0)
                p->setStateInformation (job->stem.baseState.getData(), (int) job->stem.baseState.getSize());
            if (job->stem.presetIndex >= 0 && job->stem.presetIndex < (int) p->presetManager.getPresets().size())
                p->presetManager.load (job->stem.presetIndex);
            if (const auto error = p->sessionTransport.loadMidi (midiFile); error.isNotEmpty())
                return fail (error);
            p->sessionTransport.setStemFilter (job->stem.track);
            p->setNonRealtime (true);
            if (! job->stem.file.getParentDirectory().createDirectory())
                return fail ("cannot create " + job->stem.file.getParentDirectory().getFullPathName());
            jobs.push_back (std::move (job));
        }
        if (const auto song = jobs.front()->processor->sessionTransport.getSong())
            songSeconds = song->length;
        remaining.store ((int) jobs.size());
        running.store (true);
        startedAt = juce::Time::getMillisecondCounterHiRes();
        for (auto& j : jobs)
            pool.addJob (j.get(), false);
        return {};
    }

    void cancel() { cancelled.store (true); }
    bool isRunning() const { return running.load(); }

    // Progreso de 0 a 1 (media de las pistas; la cola cuenta como 5 s)
    double progress() const
    {
        if (jobs.empty()) return 0.0;
        double sum = 0.0;
        for (const auto& j : jobs) sum += j->progress.load();
        return sum / (double) jobs.size();
    }

private:
    static constexpr int kBlock = 256;
    static constexpr double kSilenceDb = -90.0, kSilenceSeconds = 0.5;

    struct Job : juce::ThreadPoolJob
    {
        explicit Job (StemRenderer& r) : juce::ThreadPoolJob ("KoniSynth stem"), owner (r) {}

        JobStatus runJob() override
        {
            if (! render())
                owner.failed.store (true);
            if (owner.remaining.fetch_sub (1) == 1)
                owner.finish();
            return jobHasFinished;
        }

        bool render()
        {
            auto& p = *processor;
            const auto fs = owner.rate;
            p.setPlayConfigDetails (2, 2, fs, kBlock);
            p.prepareToPlay (fs, kBlock);
            p.sessionTransport.play();
            stem.file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream (stem.file.createOutputStream());
            if (stream == nullptr)
            {
                error = "cannot create " + stem.file.getFullPathName();
                return false;
            }
            juce::WavAudioFormat wav;
            writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (2).withBitsPerSample (32)
                                                    .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            if (writer == nullptr)
            {
                error = "cannot write " + stem.file.getFullPathName();
                return false;
            }
            juce::AudioBuffer<float> buffer (2, kBlock);
            juce::MidiBuffer midi;
            const auto threshold = juce::Decibels::decibelsToGain ((float) kSilenceDb);
            const auto silenceNeeded = (juce::int64) std::llround (kSilenceSeconds * fs);
            const auto tailMax = (juce::int64) std::llround (std::max (5.0, p.getTailLengthSeconds()) * fs);
            const auto expected = std::max (1.0, (owner.songSeconds + 5.0) * fs);
            juce::int64 tailFrames = 0, quiet = 0;
            bool ended = false;
            while (! owner.cancelled.load (std::memory_order_relaxed))
            {
                buffer.clear();
                midi.clear();
                p.processBlock (buffer, midi);
                writer->writeFromAudioSampleBuffer (buffer, 0, kBlock);
                frames += kBlock;
                progress.store (std::min (1.0, (double) frames / expected));
                if (! ended)
                {
                    ended = ! p.sessionTransport.isPlaying();
                    continue;
                }
                // Cola: hasta 0,5 s seguidos por debajo de -90 dBFS, o el tope
                tailFrames += kBlock;
                quiet = buffer.getMagnitude (0, kBlock) < threshold ? quiet + kBlock : 0;
                if (quiet >= silenceNeeded || tailFrames >= tailMax)
                    break;
            }
            p.releaseResources();
            progress.store (1.0);
            return ! owner.cancelled.load();
        }

        StemRenderer& owner;
        Stem stem;
        std::unique_ptr<KoniSynthProcessor> processor;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        juce::int64 frames = 0;
        std::atomic<double> progress { 0.0 };
        juce::String error;
    };

    // Hilo del último trabajo: misma longitud para todas, cierre y aviso a la UI (que destruye las instancias en su hilo)
    void finish()
    {
        Result r;
        r.cancelled = cancelled.load();
        r.ok = ! r.cancelled && ! failed.load();
        juce::int64 longest = 0;
        for (const auto& j : jobs) longest = std::max (longest, j->frames);
        juce::AudioBuffer<float> zeros (2, kBlock);
        zeros.clear();
        for (auto& j : jobs)
        {
            if (j->writer != nullptr && r.ok)
                for (auto left = longest - j->frames; left > 0; left -= kBlock)
                    j->writer->writeFromAudioSampleBuffer (zeros, 0, (int) std::min<juce::int64> (kBlock, left));
            j->writer.reset(); // cierra el fichero (cabecera WAV con la longitud)
            if (r.ok)
                r.files.add (j->stem.file);
            else
                j->stem.file.deleteFile();
            if (j->error.isNotEmpty() && r.message.isEmpty())
                r.message = j->error;
        }
        r.seconds = (juce::Time::getMillisecondCounterHiRes() - startedAt) / 1000.0;
        if (r.ok)
            r.message = juce::String (jobs.size()) + " track" + (jobs.size() == 1 ? "" : "s") + ", " + juce::String ((double) longest / rate, 1)
                        + " s of audio rendered in " + juce::String (r.seconds, 1) + " s";
        else if (r.cancelled)
            r.message = "render cancelled";
        juce::MessageManager::callAsync ([this, r, alive = alive]
        {
            if (! alive->load()) return;
            jobs.clear(); // las instancias (con Timer) se destruyen en el hilo de mensajes
            running.store (false);
            if (done) done (r);
        });
    }

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    juce::ThreadPool pool;
    std::vector<std::unique_ptr<Job>> jobs;
    std::function<void (const Result&)> done;
    std::atomic<bool> cancelled { false }, failed { false }, running { false };
    std::atomic<int> remaining { 0 };
    double rate = 48000.0, songSeconds = 0.0, startedAt = 0.0;
};
