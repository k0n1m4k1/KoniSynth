// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, APVTS, paramHelp, PresetManager, UndoHistory). No es una cabecera independiente.
#pragma once

//==============================================================================
// Control por agente (F1): servidor TCP local para KoniSynth-mcp (proceso aparte que habla MCP con Claude).
//
// Aislamiento (requisito): no conoce el procesador; solo recibe referencias a APVTS, PresetManager y UndoHistory, y
// solo usa su API pública. Desactivado (opción global "agentControl", false por defecto) no existe: el procesador no
// crea el objeto, así que no hay hilo, socket ni fichero. No toca el hilo de audio: cada orden se ejecuta en el hilo de
// mensajes, igual que un cambio hecho con el ratón.
//
// Red: solo 127.0.0.1, primer puerto libre de 47800..47899 (una instancia por puerto). Anuncio en
// %APPDATA%\KoniSynth\agent\<puerto>.json con el puerto y un token aleatorio de 128 bits que toda petición debe traer;
// se borra al parar (si el proceso muere, queda un anuncio huérfano: el cliente lo descarta si no responde a ping).
//
// Protocolo: una línea JSON por petición y otra por respuesta (UTF-8, '\n'; máximo 1 MiB por línea).
//   -> {"token": "...", "id": 1, "cmd": "ping" | "describe" | "get" | "set" | "presets" | "load_preset" | "save_preset", ...}
//   <- {"id": 1, "ok": true, "result": ...}   |   {"id": 1, "ok": false, "error": "..."}
//   describe {"match": "cutoff"}            parámetros (id, nombre, valor, rango, opciones, ayuda); match opcional
//   get      {"ids": ["cutoff", ...]}       valores actuales
//   set      {"values": {"cutoff": "2 kHz", "resonance": 0.4, "wave": {"normalized": 0.66}}}   texto = formato del
//            parámetro; número = valor real; {"normalized": x} = posición 0..1 del control (F4: mutaciones).
//            Todo el lote es un solo paso de deshacer
//   presets  {}                             lista (índice, nombre, categoría, fábrica/usuario, motor, playHint y rango
//            recomendado playLow-playHigh en notas MIDI si los tiene)
//   load_preset {"index": 12} | {"name": "Acid Bass"}
//   save_preset {"name": "...", "category": "Bass"}
//   play     {"notes": [{"note": "C4" | 60, "velocity": 100, "start": 0.0, "duration": 1.0}, ...]}   (F3) por el teclado
//            virtual, como el teclado en pantalla; máximo 64 notas, inicio y duración hasta 30 s
//   stop_notes {}                           suelta todas las notas y cancela las programadas por play que aún no han sonado
//            (cada play se suma a los anteriores; stop_notes los cancela todos)
//   listen   {"seconds": 2.0, "fromPlay": false}   vuelca a un WAV temporal los últimos N s de la salida (máximo 10) o,
//            con fromPlay, N s desde la muestra en que sonó la primera nota del último play (independiente de retrasos del
//            hilo de mensajes); devuelve ruta, frecuencia, pico y "partial" si aún no se han escrito los N s
class AgentServer : private juce::Thread
{
public:
    struct Host
    {
        APVTS& apvts;
        PresetManager& presets;
        UndoHistory& undo;
        juce::String version;
        juce::MidiKeyboardState& keyboard; // F3: tocar notas (el mismo estado que el teclado en pantalla)
        AudioTap& tap;                     // F3: salida reciente (Agent/AudioTap.h)
    };

    static constexpr int firstPort = 47800, lastPort = 47899;
    static constexpr int maxLine = 1 << 20;

    explicit AgentServer (Host h) : juce::Thread ("KoniSynth agent"), host (h) {}
    ~AgentServer() override { stop(); }

    // Hilo de mensajes. false si no hay puerto libre o no se puede escribir el anuncio
    bool start()
    {
        for (int p = firstPort; p <= lastPort && listener == nullptr; ++p)
        {
            auto s = std::make_unique<juce::StreamingSocket>();
            if (s->createListener (p, "127.0.0.1"))
            {
                listener = std::move (s);
                port = p;
            }
        }
        if (listener == nullptr)
            return false;

        token = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())
              + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64());
        auto info = std::make_unique<juce::DynamicObject>();
        info->setProperty ("port", port);
        info->setProperty ("token", token);
        info->setProperty ("product", "KoniSynth");
        info->setProperty ("version", host.version);
        info->setProperty ("started", juce::Time::getCurrentTime().toISO8601 (true));
        const auto dir = discoveryDirectory();
        dir.createDirectory();
        announce = dir.getChildFile (juce::String (port) + ".json");
        if (! announce.replaceWithText (juce::JSON::toString (juce::var (info.release()))))
        {
            listener.reset();
            return false;
        }
        startThread();
        return true;
    }

    // Hilo de mensajes. Idempotente
    void stop()
    {
        *alive = false;              // órdenes pendientes en la cola de mensajes (y notas programadas): no se ejecutan
        if (notesScheduled)
            host.keyboard.allNotesOff (0); // las notas que el agente dejó sonando no quedan colgadas
        signalThreadShouldExit();
        if (listener != nullptr) listener->close();
        stopThread (3000);               // tras parar el hilo de aceptación nadie más toca 'clients'
        for (auto* c : clients)
        {
            c->signalThreadShouldExit();
            c->socket->close();
            c->stopThread (3000);
        }
        clients.clear();
        listener.reset();
        if (announce != juce::File())
            announce.deleteFile();
        announce = juce::File();
    }

    int getPort() const { return port; }
    int connectedClients() const { return activeClients.load(); } // conexiones abiertas del puente (diálogo Agent)

    static juce::File discoveryDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("KoniSynth").getChildFile ("agent");
    }

private:
    // Un hilo por cliente (máximo maxClients a la vez): KoniSynth-mcp mantiene su conexión abierta y otro cliente
    // (otra sesión, una sonda de list_instances) no debe esperar a que la cierre
    static constexpr int maxClients = 4;
    struct ClientThread : juce::Thread
    {
        ClientThread (AgentServer& o, juce::StreamingSocket* s) : juce::Thread ("KoniSynth agent client"), owner (o), socket (s) {}
        void run() override { owner.serve (*socket, *this); }
        AgentServer& owner;
        std::unique_ptr<juce::StreamingSocket> socket;
    };

    // Hilo de aceptación
    void run() override
    {
        while (! threadShouldExit())
        {
            if (listener == nullptr || listener->waitUntilReady (true, 200) <= 0)
                continue;
            std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());
            if (client == nullptr)
                continue;
            for (int i = clients.size(); --i >= 0;) // libera los clientes que ya terminaron
                if (! clients[i]->isThreadRunning())
                    clients.remove (i);
            if (clients.size() >= maxClients)
                continue; // demasiados: se cierra al salir de ámbito
            clients.add (new ClientThread (*this, client.release()))->startThread();
        }
    }

    void serve (juce::StreamingSocket& client, juce::Thread& self)
    {
        ++activeClients;
        const struct Leave { std::atomic<int>& n; ~Leave() { --n; } } leave { activeClients };
        juce::MemoryBlock pending;
        char buffer[4096];
        while (! self.threadShouldExit() && ! threadShouldExit() && client.isConnected())
        {
            const auto ready = client.waitUntilReady (true, 200);
            if (ready < 0) return;
            if (ready == 0) continue;
            const auto n = client.read (buffer, (int) sizeof (buffer), false);
            if (n <= 0) return; // cerrada por el cliente
            pending.append (buffer, (size_t) n);
            for (;;)
            {
                const auto* data = static_cast<const char*> (pending.getData());
                const auto size = (int) pending.getSize();
                int nl = -1;
                for (int i = 0; i < size; ++i)
                    if (data[i] == '\n') { nl = i; break; }
                if (nl < 0)
                {
                    if (size > maxLine) return; // línea demasiado larga: se corta la conexión
                    break;
                }
                const auto line = juce::String::fromUTF8 (data, nl);
                const auto reply = juce::JSON::toString (handle (line), true) + "\n";
                pending.removeSection (0, (size_t) nl + 1);
                const auto utf8 = reply.toRawUTF8();
                if (client.write (utf8, (int) std::strlen (utf8)) < 0)
                    return;
            }
        }
    }

    static juce::var reply (const juce::var& id, bool ok, const juce::var& payload)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("id", id);
        o->setProperty ("ok", ok);
        o->setProperty (ok ? "result" : "error", payload);
        return juce::var (o.release());
    }

    juce::var handle (const juce::String& line)
    {
        const auto req = juce::JSON::parse (line);
        const auto id = req.getProperty ("id", juce::var());
        if (! req.isObject())
            return reply (id, false, "invalid JSON");
        if (req.getProperty ("token", juce::var()).toString() != token)
            return reply (id, false, "invalid token");
        const auto cmd = req.getProperty ("cmd", juce::var()).toString();
        juce::String error;
        auto result = onMessageThread ([this, cmd, req] (juce::String& err) { return dispatch (cmd, req, err); }, error);
        return error.isEmpty() ? reply (id, true, result) : reply (id, false, error);
    }

    // Ejecuta fn en el hilo de mensajes y espera su resultado (máximo 5 s; si el servidor se para, deja de esperar).
    // Resultado y error viven en 'call' (compartido): si se agota la espera, la orden puede terminar después sin escribir
    // en variables de este hilo que ya no existen
    juce::var onMessageThread (std::function<juce::var (juce::String&)> fn, juce::String& error)
    {
        struct Call { juce::WaitableEvent done; juce::var result; juce::String error; };
        auto call = std::make_shared<Call>();
        juce::MessageManager::callAsync ([call, fn, flag = alive]
        {
            if (*flag)
                call->result = fn (call->error);
            call->done.signal();
        });
        for (int waited = 0; ! call->done.wait (50); waited += 50)
            if (threadShouldExit() || waited > 5000)
            {
                error = threadShouldExit() ? "server stopping" : "timeout waiting for the message thread";
                return {};
            }
        error = call->error;
        return call->result;
    }

    // --- Órdenes (hilo de mensajes) ---
    juce::var dispatch (const juce::String& cmd, const juce::var& req, juce::String& error)
    {
        if (cmd == "ping")        return ping();
        if (cmd == "describe")    return describe (req.getProperty ("match", juce::var()).toString().toLowerCase());
        if (cmd == "get")         return get (req.getProperty ("ids", juce::var()), error);
        if (cmd == "set")         return set (req.getProperty ("values", juce::var()), error);
        if (cmd == "presets")     return presetList();
        if (cmd == "load_preset") return loadPreset (req, error);
        if (cmd == "save_preset") return savePreset (req, error);
        if (cmd == "play")        return play (req.getProperty ("notes", juce::var()), error);
        if (cmd == "stop_notes")  { ++*playGeneration; host.keyboard.allNotesOff (0); return true; } // cancela lo programado
        if (cmd == "listen")      return listen ((double) req.getProperty ("seconds", 2.0), (bool) req.getProperty ("fromPlay", false), error);
        error = "unknown cmd: " + cmd;
        return {};
    }

    juce::var ping()
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("product", "KoniSynth");
        o->setProperty ("version", host.version);
        if (auto* e = host.apvts.getParameter ("engine"))
            o->setProperty ("engine", e->getCurrentValueAsText());
        const auto i = host.presets.currentIndex();
        o->setProperty ("preset", i >= 0 ? host.presets.getPresets()[(size_t) i].name : juce::String());
        o->setProperty ("modified", host.presets.isDirty());
        return juce::var (o.release());
    }

    static juce::var describeOne (juce::RangedAudioParameter& p)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("id", p.getParameterID());
        o->setProperty ("name", p.getName (64));
        o->setProperty ("value", p.getCurrentValueAsText());
        o->setProperty ("normalized", p.getValue());
        o->setProperty ("min", p.getText (0.0f, 64));
        o->setProperty ("max", p.getText (1.0f, 64));
        o->setProperty ("default", p.getText (p.getDefaultValue(), 64));
        const auto n = p.getNumSteps();
        if (dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr)
            o->setProperty ("type", "bool");
        else if (dynamic_cast<juce::AudioParameterChoice*> (&p) != nullptr || (n >= 2 && n <= 64))
        {
            o->setProperty ("type", dynamic_cast<juce::AudioParameterChoice*> (&p) != nullptr ? "choice" : "stepped");
            juce::Array<juce::var> options;
            for (int i = 0; i < n; ++i)
                options.add (p.getText ((float) i / (float) (n - 1), 64));
            o->setProperty ("options", options);
        }
        else
            o->setProperty ("type", "continuous");
        if (const auto help = paramHelp (p.getParameterID()); help.isNotEmpty())
            o->setProperty ("help", help);
        return juce::var (o.release());
    }

    juce::var describe (const juce::String& match)
    {
        juce::Array<juce::var> list;
        for (auto* ap : host.apvts.processor.getParameters())
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (ap))
                if (match.isEmpty() || p->getParameterID().toLowerCase().contains (match) || p->getName (64).toLowerCase().contains (match))
                    list.add (describeOne (*p));
        return list;
    }

    juce::var get (const juce::var& ids, juce::String& error)
    {
        if (! ids.isArray()) { error = "ids must be an array"; return {}; }
        auto o = std::make_unique<juce::DynamicObject>();
        for (const auto& v : *ids.getArray())
        {
            auto* p = host.apvts.getParameter (v.toString());
            if (p == nullptr) { error = "unknown parameter: " + v.toString(); return {}; }
            o->setProperty (v.toString(), p->getCurrentValueAsText());
        }
        return juce::var (o.release());
    }

    juce::var set (const juce::var& values, juce::String& error)
    {
        auto* obj = values.getDynamicObject();
        if (obj == nullptr) { error = "values must be an object"; return {}; }
        // Validación previa: si algún ID no existe no se cambia nada
        for (const auto& kv : obj->getProperties())
            if (host.apvts.getParameter (kv.name.toString()) == nullptr) { error = "unknown parameter: " + kv.name.toString(); return {}; }
        host.undo.beginGroup();
        auto applied = std::make_unique<juce::DynamicObject>();
        for (const auto& kv : obj->getProperties())
        {
            auto* p = host.apvts.getParameter (kv.name.toString());
            const auto norm = kv.value.isString() ? p->getValueForText (kv.value.toString())
                            : kv.value.isObject() ? (float) (double) kv.value.getProperty ("normalized", p->getValue())
                                                  : p->convertTo0to1 ((float) (double) kv.value);
            p->beginChangeGesture();
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
            p->endChangeGesture();
            applied->setProperty (kv.name, p->getCurrentValueAsText());
        }
        host.undo.endGroup();
        return juce::var (applied.release());
    }

    juce::var presetList()
    {
        juce::Array<juce::var> list;
        const auto& all = host.presets.getPresets();
        const auto* engines = dynamic_cast<juce::AudioParameterChoice*> (host.apvts.getParameter ("engine"));
        for (size_t i = 0; i < all.size(); ++i)
        {
            const auto& p = all[i];
            auto o = std::make_unique<juce::DynamicObject>();
            o->setProperty ("index", (int) i);
            o->setProperty ("name", p.name);
            o->setProperty ("category", p.category);
            o->setProperty ("factory", p.factory);
            // Motor, cómo tocarlo y rango recomendado (los metadatos del navegador; sin rango, no se envía)
            o->setProperty ("engine", engines != nullptr ? engines->choices[p.engine] : juce::String (p.engine));
            if (p.playHint.isNotEmpty())
                o->setProperty ("playHint", p.playHint);
            if (p.playLow >= 0 && p.playHigh >= p.playLow)
            {
                o->setProperty ("playLow", p.playLow); // notas MIDI (60 = C4)
                o->setProperty ("playHigh", p.playHigh);
            }
            list.add (juce::var (o.release()));
        }
        return list;
    }

    juce::var loadPreset (const juce::var& req, juce::String& error)
    {
        const auto& all = host.presets.getPresets();
        int index = req.hasProperty ("index") ? (int) req.getProperty ("index", -1) : -1;
        if (index < 0 && req.hasProperty ("name"))
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].name.equalsIgnoreCase (req.getProperty ("name", {}).toString())) { index = (int) i; break; }
        if (index < 0 || index >= (int) all.size()) { error = "preset not found"; return {}; }
        if (const auto r = host.presets.load (index); r.failed()) { error = r.getErrorMessage(); return {}; }
        auto reply = ping();
        if (host.presets.lastLoadNote.isNotEmpty()) // formato posterior o ajustes desconocidos
            if (auto* o = reply.getDynamicObject()) o->setProperty ("note", host.presets.lastLoadNote);
        return reply;
    }

    juce::var savePreset (const juce::var& req, juce::String& error)
    {
        const auto name = req.getProperty ("name", {}).toString().trim();
        const auto category = req.getProperty ("category", "Init").toString();
        if (const auto v = host.presets.validateName (name); v.failed()) { error = v.getErrorMessage(); return {}; }
        if (const auto r = host.presets.saveAs (name, category); r.failed()) { error = r.getErrorMessage(); return {}; }
        return ping();
    }

    // Notas por el teclado virtual (canal 1). Note-on y note-off con Timer::callAfterDelay (hilo de mensajes): resolución
    // de un tic del temporizador (~1-16 ms según el sistema), suficiente para escuchar un sonido, no para secuenciar
    juce::var play (const juce::var& notes, juce::String& error)
    {
        if (! notes.isArray() || notes.size() == 0 || notes.size() > 64) { error = "notes must be an array of 1..64 notes"; return {}; }
        double end = 0.0;
        struct N { int note, velocity; double start, duration; };
        std::vector<N> list;
        for (const auto& n : *notes.getArray())
        {
            const auto nv = n.getProperty ("note", 60);
            const auto note = nv.isString() ? midiNoteFromText (nv.toString()) : (int) nv;
            const auto vel = (int) n.getProperty ("velocity", 100);
            const auto start = (double) n.getProperty ("start", 0.0);
            const auto dur = (double) n.getProperty ("duration", 1.0);
            if (note < 0 || note > 127 || vel < 1 || vel > 127 || start < 0.0 || start > 30.0 || dur <= 0.0 || dur > 30.0)
            {
                error = "note out of range (note 0..127, velocity 1..127, start 0..30 s, duration 0..30 s)";
                return {};
            }
            list.push_back ({ note, vel, start, dur });
            end = juce::jmax (end, start + dur);
        }
        notesScheduled = true;
        auto& kb = host.keyboard;
        auto& tap = host.tap;
        playStart = 0;
        const auto first = std::min_element (list.begin(), list.end(), [] (const N& a, const N& b) { return a.start < b.start; })->start;
        // Generación de este play: stop_notes la incrementa y los temporizadores pendientes (note-on y note-off) ya no actúan.
        // Así un note-off antiguo no corta una nota nueva de la misma altura tras un stop_notes
        const auto gen = playGeneration->load();
        for (const auto& n : list)
        {
            juce::Timer::callAfterDelay (juce::roundToInt (n.start * 1000.0), [this, &kb, &tap, n, first, flag = alive, g = playGeneration, gen]
            {
                if (! *flag || g->load() != gen) return;
                if (n.start == first && playStart == 0)
                    playStart = tap.framesWritten(); // la nota entra en el siguiente bloque de audio: ancla de 'fromPlay'
                kb.noteOn (1, n.note, (float) n.velocity / 127.0f);
            });
            juce::Timer::callAfterDelay (juce::roundToInt ((n.start + n.duration) * 1000.0), [&kb, n, flag = alive, g = playGeneration, gen]
            {
                if (*flag && g->load() == gen) kb.noteOff (1, n.note, 0.0f);
            });
        }
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("scheduled", (int) list.size());
        o->setProperty ("endsAt", end);
        return juce::var (o.release());
    }

    juce::var listen (double seconds, bool fromPlay, juce::String& error)
    {
        seconds = juce::jlimit (0.05, 10.0, seconds);
        const auto sr = host.tap.sampleRate();
        const auto frames = juce::roundToInt (seconds * sr);
        if (fromPlay && playStart == 0) { error = "fromPlay: no note has sounded since the last play"; return {}; }
        auto audio = fromPlay ? host.tap.range (playStart, frames) : host.tap.last (frames);
        if (audio.getNumSamples() == 0) { error = "no audio captured yet (is the audio device running?)"; return {}; }
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KoniSynth-agent");
        dir.createDirectory();
        const auto file = dir.getChildFile ("listen-" + juce::String (port) + ".wav");
        file.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (32)
                                                                                        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            {
                error = "could not write " + file.getFullPathName();
                return {};
            }
        }
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("path", file.getFullPathName());
        o->setProperty ("sampleRate", sr);
        o->setProperty ("seconds", (double) audio.getNumSamples() / sr);
        o->setProperty ("partial", audio.getNumSamples() < frames);
        const auto peak = juce::jmax (audio.getMagnitude (0, 0, audio.getNumSamples()), audio.getMagnitude (1, 0, audio.getNumSamples()));
        o->setProperty ("peakDb", juce::Decibels::gainToDecibels (peak, -120.0f));
        return juce::var (o.release());
    }

    Host host;
    bool notesScheduled = false;
    juce::uint64 playStart = 0; // posición del tap cuando sonó la primera nota del último play (hilo de mensajes)
    std::unique_ptr<juce::StreamingSocket> listener;
    int port = 0;
    std::atomic<int> activeClients { 0 };
    juce::String token;
    juce::File announce;
    juce::OwnedArray<ClientThread> clients;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::shared_ptr<std::atomic<std::uint32_t>> playGeneration = std::make_shared<std::atomic<std::uint32_t>> (0); // stop_notes la sube
};
