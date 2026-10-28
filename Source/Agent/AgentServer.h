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
//   get      {"ids": ["cutoff", ...], "detail": false}   valores actuales como texto; con detail, por id {text, value
//            (valor real, el que admite set como número), normalized (0..1), unit (unidad de value; null si no se deduce)}
//   set      {"values": {"cutoff": "2 kHz", "resonance": 0.4, "wave": {"normalized": 0.66}}, "detail": false}
//            texto = formato del parámetro (en bool y choice, una de sus opciones); número = valor real (en choice, el
//            índice; fuera de rango se recorta y, con detail, se marca "clamped"); {"normalized": x} = posición 0..1 del
//            control (F4: mutaciones). El lote se valida entero antes de cambiar nada: un id, texto, número no finito o
//            forma de objeto no válidos rechazan la orden sin tocar ningún parámetro. Todo el lote es un solo paso de
//            deshacer
//   state    {}                             tempo y reloj del último bloque: frecuencia, bloque, latencia, datos del host que
//            existan (bpm, ppq, timeInSamples, playing), tempo efectivo y su fuente, reloj propio del motor (Omnichord,
//            Drum Machine, VL-1), BPM del display, transformaciones activas de las notas y punto de captura
//   presets  {}                             lista (índice, nombre, categoría, fábrica/usuario, motor, playHint y rango
//            recomendado playLow-playHigh en notas MIDI si los tiene)
//   load_preset {"index": 12} | {"name": "Acid Bass"}
//   save_preset {"name": "...", "category": "Bass"}
//   play     {"notes": [{"note": "C4" | 60, "velocity": 100, "start": 0.0, "duration": 1.0}, ...], "bpm": 128}   (F3, S2)
//            con el reloj de audio (Agent/AgentScheduler.h): cada nota en su muestra, round(fs · t) desde el origen del
//            play (el inicio del bloque en que llega al hilo de audio). Sin bpm, start y duration en segundos; con bpm, en
//            beats (tempo congelado). Máximo 64 notas que acaban antes de 30 s (y 512 pendientes entre todos los play).
//            note es obligatoria (entero 0..127 o nombre C-1..G9 con # o b; C4 = 60); velocity entera 1..127 (100);
//            start >= 0 (0); duration > 0 (1). Cualquier nota no válida o campo desconocido rechaza la orden entera
//   stop_notes {}                           suelta todas las notas (también las del teclado en pantalla) y cancela las
//            programadas por play que aún no han sonado (cada play se suma a los anteriores; stop_notes los cancela todos)
//   listen   {"seconds": 2.0, "fromPlay": false}   vuelca a un WAV temporal propio de cada captura (se conservan las 32
//            últimas) los últimos N s de la salida (0,05..10 s) o, con fromPlay, N s desde el origen del último play
//            (instante 0 de sus start, conservando el silencio inicial; es la muestra exacta del origen del planificador,
//            sin depender del hilo de mensajes). Devuelve ruta, frecuencia, fotogramas pedidos y
//            capturados, posición de inicio, pico, "partial" si se capturó menos de lo pedido y "truncated" con el motivo
//            ("notYetWritten": aún sonando, repetir; "notEnoughAudio": la captura empezó hace menos de N s).
//            "point": "output" (por defecto: salida final, tras efectos, Gain y limitador, más el audio del transporte) o
//            "engine" (salida del motor con sus efectos propios, antes de vocoder, monitor de entrada, cadena de efectos,
//            Gain y limitador; mismas posiciones que output)
class AgentServer : private juce::Thread
{
public:
    struct Host
    {
        APVTS& apvts;
        PresetManager& presets;
        UndoHistory& undo;
        juce::String version;
        juce::MidiKeyboardState& keyboard; // el teclado en pantalla: stop_notes también suelta sus notas
        AudioTap& tap;                     // F3: salida reciente (Agent/AudioTap.h)
        AgentScheduler& scheduler;         // S2: notas de play con el reloj de audio (Agent/AgentScheduler.h)
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
        // Capturas de una sesión anterior en este puerto (listen-<puerto>-<n>.wav; antes, listen-<puerto>.wav): se borran
        for (const auto& f : captureDirectory().findChildFiles (juce::File::findFiles, false, "listen-" + juce::String (port) + "*.wav"))
            if (const auto rest = f.getFileNameWithoutExtension().fromFirstOccurrenceOf ("listen-" + juce::String (port), false, false);
                rest.isEmpty() || rest.startsWithChar ('-'))
                f.deleteFile();
        startThread();
        return true;
    }

    // Hilo de mensajes. Idempotente
    void stop()
    {
        *alive = false;              // órdenes pendientes en la cola de mensajes (y notas programadas): no se ejecutan
        host.scheduler.cancel(); // las notas que el agente dejó sonando o programadas no quedan colgadas
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

    static juce::File captureDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KoniSynth-agent");
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
        if (cmd == "get")         return get (req.getProperty ("ids", juce::var()), (bool) req.getProperty ("detail", false), error);
        if (cmd == "set")         return set (req.getProperty ("values", juce::var()), (bool) req.getProperty ("detail", false), error);
        if (cmd == "state")       return state();
        if (cmd == "presets")     return presetList();
        if (cmd == "load_preset") return loadPreset (req, error);
        if (cmd == "save_preset") return savePreset (req, error);
        if (cmd == "play")        return play (req, error);
        if (cmd == "stop_notes")  { host.scheduler.cancel(); host.keyboard.allNotesOff (0); return true; } // y lo programado
        if (cmd == "listen")      return listen (req.getProperty ("seconds", 2.0), (bool) req.getProperty ("fromPlay", false),
                                                 req.getProperty ("point", "output").toString(), error);
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

    float raw (const char* id) const
    {
        const auto* v = host.apvts.getRawParameterValue (id);
        return v != nullptr ? v->load() : 0.0f;
    }

    // Transformaciones que sufren las notas de play antes del motor, en el orden de processBlock: transposición, escala,
    // secuenciador (si está activo sustituye las notas) o arpegiador. No se desactivan aquí: el cliente decide con set
    juce::var noteTransforms() const
    {
        juce::Array<juce::var> list;
        if (const auto t = (int) raw ("keyTranspose"); t != 0)
            list.add ("transpose " + juce::String (t > 0 ? "+" : "") + juce::String (t) + " st (keyTranspose)");
        if (raw ("scaleMode") > 0.5f)
            list.add ("scale quantizer (scaleMode " + host.apvts.getParameter ("scaleMode")->getCurrentValueAsText() + ")");
        if (raw ("seqOn") > 0.5f)
            list.add ("step sequencer replaces the notes (seqOn)");
        else if (raw ("arpOn") > 0.5f)
            list.add ("arpeggiator replaces the notes (arpOn)");
        return list;
    }

    // Estado de tempo y reloj (S6): lo que el procesador decidió en el último bloque capturado (AudioTap::Clock), sin
    // recalcular el tempo efectivo. Del host solo se dan los datos que proporciona
    juce::var state()
    {
        const auto c = host.tap.clock();
        const auto sr = host.tap.sampleRate();
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("audioRunning", c.valid);
        o->setProperty ("sampleRate", sr);
        if (c.valid)
        {
            o->setProperty ("blockSize", c.blockSize);
            o->setProperty ("latencySamples", c.latencySamples);
            o->setProperty ("latencyMs", 1000.0 * c.latencySamples / sr);
            o->setProperty ("blockStartFrame", (juce::int64) c.frame); // posición en la captura de listen
        }
        auto h = std::make_unique<juce::DynamicObject>();
        if (c.hasBpm)  h->setProperty ("bpm", c.bpm);
        if (c.hasPpq)  h->setProperty ("ppq", c.ppq);
        if (c.hasTime) h->setProperty ("timeInSamples", c.timeSamples);
        h->setProperty ("playing", c.playing);
        o->setProperty ("host", juce::var (h.release()));
        // Tempo de arpegiador, secuenciador y sincronías (LFO, eco...): el del host o, sin él, Arp BPM
        auto t = std::make_unique<juce::DynamicObject>();
        t->setProperty ("bpm", c.valid ? c.tempoBpm : (double) raw ("arpBpm"));
        t->setProperty ("source", c.hasBpm ? "host" : "internal (arpBpm)");
        t->setProperty ("usedBy", "arpeggiator, step sequencer, tempo-synced LFOs and effects");
        o->setProperty ("tempo", juce::var (t.release()));
        // Reloj propio del motor: ritmos de Omnichord y Drum Machine (siguen al host solo si da BPM y PPQ:
        // Dsp/StepClock.h) y el VL-1, que nunca sigue al host
        const auto engine = (int) raw ("engine");
        if (engine == 4 || engine == 5)
        {
            const auto synced = c.hasBpm && c.hasPpq;
            auto r = std::make_unique<juce::DynamicObject>();
            r->setProperty ("bpm", synced ? c.bpm : (double) raw (engine == 5 ? "drumTempo" : "omniTempo"));
            r->setProperty ("source", synced ? juce::String ("host") : juce::String (engine == 5 ? "internal (drumTempo)" : "internal (omniTempo)"));
            o->setProperty ("rhythmClock", juce::var (r.release()));
        }
        else if (engine == 2)
        {
            auto r = std::make_unique<juce::DynamicObject>();
            r->setProperty ("bpm", (double) c.displayBpm); // fórmula del procesador (negra = 32 ticks)
            r->setProperty ("source", "VL-1 own clock (vl1Tempo); never follows the host");
            o->setProperty ("rhythmClock", juce::var (r.release()));
        }
        if (c.valid)
        {
            o->setProperty ("displayBpm", (double) c.displayBpm); // el LED de BPM de la interfaz
            o->setProperty ("displayBpmFromHost", c.displayFromHost);
        }
        o->setProperty ("noteTransforms", noteTransforms());
        auto points = std::make_unique<juce::DynamicObject>(); // los dos puntos de listen
        points->setProperty ("output", outputPointText);
        points->setProperty ("engine", enginePointText);
        o->setProperty ("capturePoints", juce::var (points.release()));
        return juce::var (o.release());
    }

    static bool isBoolParam (juce::RangedAudioParameter& p)   { return dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr; }
    static bool isChoiceParam (juce::RangedAudioParameter& p) { return dynamic_cast<juce::AudioParameterChoice*> (&p) != nullptr; }

    // Número JSON finito (entero o real; true/false no cuentan)
    static bool finiteNumber (const juce::var& v, double& out)
    {
        if (! (v.isInt() || v.isInt64() || v.isDouble()))
            return false;
        out = (double) v;
        return std::isfinite (out);
    }

    // Unidad del valor real (convertFrom0to1) de un parámetro, deducida de su propio texto: los formatos de Params/Layout.h
    // muestran el valor con otra escala (0,35 s -> "350 ms"; 2000 Hz -> "2.00 kHz"; 0,4 -> "40 %"). Se compara el número
    // mostrado con el valor real en el máximo, el centro y el mínimo del rango (tolerancia 2 %: el texto va redondeado); el 0
    // no distingue escalas y se salta. "ratio" = 0..1 mostrado como %. null si no se reconoce (texto sin número)
    static juce::var unitOf (juce::RangedAudioParameter& p)
    {
        if (isBoolParam (p))   return "bool";
        if (isChoiceParam (p)) return "index";
        for (const auto x : { 1.0f, 0.5f, 0.0f })
        {
            const auto real = (double) p.convertFrom0to1 (x);
            const auto text = p.getText (x, 64).trim();
            int i = 0;
            while (i < text.length() && (juce::CharacterFunctions::isDigit (text[i]) || text[i] == '-' || text[i] == '+' || text[i] == '.'))
                ++i;
            if (std::abs (real) < 1.0e-6 || i == 0 || ! text.substring (0, i).containsAnyOf ("0123456789"))
                continue;
            const auto shown = text.substring (0, i).getDoubleValue();
            const auto suffix = text.substring (i).trim();
            const auto near = [real] (double v) { return std::abs (v - real) <= 0.02 * std::abs (real) + 1.0e-6; };
            if (suffix == "kHz") return near (shown * 1000.0) ? juce::var ("Hz") : near (shown) ? juce::var ("kHz") : juce::var();
            if (suffix == "ms")  return near (shown) ? juce::var ("ms") : near (shown / 1000.0) ? juce::var ("s") : juce::var();
            if (suffix == "s")   return near (shown) ? juce::var ("s") : near (shown * 1000.0) ? juce::var ("ms") : juce::var();
            if (suffix == "%")   return near (shown / 100.0) ? juce::var ("ratio") : near (shown) ? juce::var ("%") : juce::var();
            return near (shown) ? juce::var (suffix) : juce::var();
        }
        return {};
    }

    static juce::var detailOf (juce::RangedAudioParameter& p)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("text", p.getCurrentValueAsText());
        o->setProperty ("value", (double) p.convertFrom0to1 (p.getValue()));
        o->setProperty ("normalized", (double) p.getValue());
        o->setProperty ("unit", unitOf (p));
        return juce::var (o.release());
    }

    // Valor pedido por set -> posición 0..1, sin tocar el parámetro. false (y el motivo en 'why') si no es válido. JUCE no
    // sirve para validar texto: en choice, un texto desconocido da la opción 0, y en bool cualquier texto da Off
    // (juce_AudioParameterChoice.cpp:61,79; juce_AudioParameterBool.cpp:50-65)
    static bool resolveValue (juce::RangedAudioParameter& p, const juce::var& v, float& norm, bool& clamped, juce::String& why)
    {
        clamped = false;
        const auto steps = p.getNumSteps();
        const auto discrete = isBoolParam (p) || isChoiceParam (p);
        if (v.isObject())
        {
            const auto* o = v.getDynamicObject();
            double x = 0.0;
            if (o == nullptr || o->getProperties().size() != 1 || ! o->hasProperty ("normalized"))
            {
                why = "an object value must be exactly {\"normalized\": 0..1}";
                return false;
            }
            if (! finiteNumber (o->getProperty ("normalized"), x) || x < 0.0 || x > 1.0)
            {
                why = "normalized must be a number in 0..1";
                return false;
            }
            norm = (float) x;
            return true;
        }
        if (v.isString())
        {
            const auto t = v.toString().trim();
            if (discrete)
            {
                juce::StringArray options;
                for (int k = 0; k < steps; ++k)
                    options.add (p.getText ((float) k / (float) (steps - 1), 64));
                auto k = options.indexOf (t, true);
                if (k < 0 && isBoolParam (p)) // alias habituales de un interruptor
                    k = juce::StringArray { "off", "no", "false", "0" }.contains (t, true) ? 0
                      : juce::StringArray { "on", "yes", "true", "1" }.contains (t, true) ? 1 : -1;
                if (k < 0)
                {
                    why = "unknown option \"" + t + "\" (options: " + options.joinIntoString (", ") + ")";
                    return false;
                }
                norm = (float) k / (float) (steps - 1);
                return true;
            }
            norm = p.getValueForText (t);
            // Sin cifras solo vale un texto que el parámetro muestra tal cual ("Off", la vocal "E"): si al leerlo y volver a
            // mostrarlo no sale lo mismo, el formato no lo ha entendido (y habría dado el mínimo sin avisar)
            if (! std::isfinite (norm) || (! t.containsAnyOf ("0123456789") && ! p.getText (norm, 64).trim().equalsIgnoreCase (t)))
            {
                why = "cannot read \"" + t + "\" (format like \"" + p.getCurrentValueAsText() + "\")";
                return false;
            }
            norm = juce::jlimit (0.0f, 1.0f, norm);
            return true;
        }
        if (v.isBool())
        {
            if (! isBoolParam (p)) { why = "true/false only for on/off parameters"; return false; }
            norm = (bool) v ? 1.0f : 0.0f;
            return true;
        }
        double x = 0.0;
        if (! finiteNumber (v, x))
        {
            why = v.isInt() || v.isInt64() || v.isDouble() ? "the number must be finite"
                                                            : "a value must be text, a number or {\"normalized\": 0..1}";
            return false;
        }
        if (discrete && (x != std::floor (x) || x < 0.0 || x > (double) (steps - 1)))
        {
            why = "must be an option index 0.." + juce::String (steps - 1) + (isBoolParam (p) ? " (or true/false)" : "");
            return false;
        }
        const auto& r = p.getNormalisableRange();
        clamped = x < (double) r.start || x > (double) r.end;
        norm = p.convertTo0to1 ((float) juce::jlimit ((double) r.start, (double) r.end, x));
        return true;
    }

    static juce::var describeOne (juce::RangedAudioParameter& p)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("id", p.getParameterID());
        o->setProperty ("name", p.getName (64));
        o->setProperty ("value", p.getCurrentValueAsText());
        o->setProperty ("normalized", p.getValue());
        o->setProperty ("unit", unitOf (p)); // unidad de los números que admite set
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

    juce::var get (const juce::var& ids, bool detail, juce::String& error)
    {
        if (! ids.isArray()) { error = "ids must be an array"; return {}; }
        auto o = std::make_unique<juce::DynamicObject>();
        for (const auto& v : *ids.getArray())
        {
            auto* p = host.apvts.getParameter (v.toString());
            if (p == nullptr) { error = "unknown parameter: " + v.toString(); return {}; }
            o->setProperty (v.toString(), detail ? detailOf (*p) : juce::var (p->getCurrentValueAsText()));
        }
        return juce::var (o.release());
    }

    juce::var set (const juce::var& values, bool detail, juce::String& error)
    {
        auto* obj = values.getDynamicObject();
        if (obj == nullptr) { error = "values must be an object"; return {}; }
        // Validación previa del lote entero: si un id o un valor no es válido no se cambia nada
        struct Change { juce::RangedAudioParameter* p; juce::Identifier id; float norm; bool clamped; };
        std::vector<Change> changes;
        for (const auto& kv : obj->getProperties())
        {
            auto* p = host.apvts.getParameter (kv.name.toString());
            if (p == nullptr) { error = "unknown parameter: " + kv.name.toString(); return {}; }
            Change c { p, kv.name, 0.0f, false };
            juce::String why;
            if (! resolveValue (*p, kv.value, c.norm, c.clamped, why)) { error = kv.name.toString() + ": " + why + " (nothing changed)"; return {}; }
            changes.push_back (c);
        }
        host.undo.beginGroup();
        auto applied = std::make_unique<juce::DynamicObject>();
        for (const auto& c : changes)
        {
            c.p->beginChangeGesture();
            c.p->setValueNotifyingHost (c.norm);
            c.p->endChangeGesture();
            auto d = detail ? detailOf (*c.p) : juce::var (c.p->getCurrentValueAsText());
            if (detail && c.clamped)
                d.getDynamicObject()->setProperty ("clamped", true); // el número pedido estaba fuera del rango
            applied->setProperty (c.id, d);
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

    // Nota para play, estricta: entero 0..127 o nombre (letra, # o b, octava -1..9; C4 = 60). -1 si no es válida
    // (midiNoteFromText, la de los parámetros, es tolerante: ante un error daría C4)
    static int strictMidiNote (const juce::var& v)
    {
        double x = 0.0;
        if (finiteNumber (v, x))
            return x == std::floor (x) && x >= 0.0 && x <= 127.0 ? (int) x : -1;
        if (! v.isString())
            return -1;
        const auto t = v.toString().trim();
        if (t.isNotEmpty() && t.length() <= 3 && t.containsOnly ("0123456789"))
            return t.getIntValue() <= 127 ? t.getIntValue() : -1;
        static const int pcs[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        const auto letter = juce::CharacterFunctions::toUpperCase (t[0]);
        if (t.length() < 2 || letter < 'A' || letter > 'G')
            return -1;
        auto pc = pcs[letter - 'A'];
        int i = 1;
        if (t[i] == '#') { ++pc; ++i; }
        else if (t[i] == 'b') { --pc; ++i; }
        const auto octave = t.substring (i);
        const auto digits = octave.startsWithChar ('-') ? octave.substring (1) : octave;
        if (digits.isEmpty() || digits.length() > 1 || ! digits.containsOnly ("0123456789"))
            return -1;
        const auto n = (octave.getIntValue() + 1) * 12 + pc;
        return n >= 0 && n <= 127 ? n : -1;
    }

    // Notas al planificador del hilo de audio (Agent/AgentScheduler.h): cada una en su muestra exacta desde el origen del
    // play. Con "bpm", start y duration van en beats (negra = 1) y se pasan a segundos aquí, una vez: t = 60 / bpm · beat,
    // desde la posición absoluta (sin acumular intervalos); el tempo queda congelado para este play
    juce::var play (const juce::var& req, juce::String& error)
    {
        const auto notes = req.getProperty ("notes", juce::var());
        if (! notes.isArray() || notes.size() == 0 || notes.size() > 64) { error = "notes must be an array of 1..64 notes"; return {}; }
        double bpm = 0.0;
        if (req.hasProperty ("bpm") && (! finiteNumber (req.getProperty ("bpm", {}), bpm) || bpm < 20.0 || bpm > 999.0))
        {
            error = "bpm must be a number in 20..999 (nothing played)";
            return {};
        }
        const auto toSeconds = bpm > 0.0 ? 60.0 / bpm : 1.0;
        const juce::String unit = bpm > 0.0 ? "beats" : "seconds";
        double end = 0.0;
        std::vector<AgentScheduler::Note> list;
        // Validación previa de todas las notas: con una no válida no suena ninguna
        for (int i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            const auto bad = [&error, i] (const juce::String& why) { error = "note " + juce::String (i) + ": " + why + " (nothing played)"; return juce::var(); };
            const auto* o = n.getDynamicObject();
            if (o == nullptr)
                return bad ("must be an object {note, velocity, start, duration}");
            for (const auto& kv : o->getProperties())
                if (! juce::StringArray { "note", "velocity", "start", "duration" }.contains (kv.name.toString()))
                    return bad ("unknown field \"" + kv.name.toString() + "\"");
            if (! o->hasProperty ("note"))
                return bad ("missing note");
            const auto note = strictMidiNote (o->getProperty ("note"));
            if (note < 0)
                return bad ("invalid note \"" + o->getProperty ("note").toString() + "\" (C-1..G9, e.g. C4 = 60, or 0..127)");
            double vel = 100.0, start = 0.0, dur = 1.0;
            if (o->hasProperty ("velocity") && ! finiteNumber (o->getProperty ("velocity"), vel)) vel = -1.0;
            if (vel != std::floor (vel) || vel < 1.0 || vel > 127.0)
                return bad ("velocity must be an integer 1..127");
            if (o->hasProperty ("start") && ! finiteNumber (o->getProperty ("start"), start)) start = -1.0;
            if (o->hasProperty ("duration") && ! finiteNumber (o->getProperty ("duration"), dur)) dur = 0.0;
            if (start < 0.0 || dur <= 0.0)
                return bad ("start must be a number >= 0 and duration a number > 0 (" + unit + ")");
            start *= toSeconds;
            dur *= toSeconds;
            if (start + dur > maxPlaySeconds)
                return bad ("ends at " + juce::String (start + dur, 3) + " s; the limit is " + juce::String (maxPlaySeconds, 0) + " s");
            list.push_back ({ note, (int) vel, start, dur });
            end = juce::jmax (end, start + dur);
        }
        // Capacidad del planificador (512 notas pendientes a la vez entre todos los play): si no cabe, no suena nada
        const auto seq = playSeq + 1;
        if (! host.scheduler.schedule (seq, list))
        {
            error = "too many notes scheduled; wait for them to finish or call stop_notes (nothing played)";
            return {};
        }
        playSeq = seq;
        playEpoch = host.tap.epochNumber();
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("scheduled", (int) list.size());
        o->setProperty ("endsAt", end); // segundos
        if (bpm > 0.0)
            o->setProperty ("bpm", bpm);
        if (const auto tr = noteTransforms(); tr.size() > 0)
            o->setProperty ("transforms", tr); // lo que cambiará estas notas antes de sonar (ver state)
        return juce::var (o.release());
    }

    static constexpr const char* outputPointText = "final output: after all effects, Gain and the limiter, plus audio played by the transport";
    static constexpr const char* enginePointText = "engine output with its own built-in effects (e.g. SNES echo, MT-32 reverb), before the "
                                                   "vocoder, input monitor, effect chain, Gain and limiter";

    juce::var listen (const juce::var& secondsVar, bool fromPlay, const juce::String& point, juce::String& error)
    {
        if (point != "output" && point != "engine")
        {
            error = "point must be \"output\" or \"engine\"";
            return {};
        }
        double seconds = 0.0;
        if (! finiteNumber (secondsVar, seconds) || seconds < 0.05 || seconds > maxListenSeconds)
        {
            error = "seconds must be a number in 0.05.." + juce::String (maxListenSeconds, 0);
            return {};
        }
        auto& tap = host.tap;
        const auto sr = tap.sampleRate();
        const auto requested = juce::roundToInt (seconds * sr);
        const auto written = tap.framesWritten();
        juce::uint64 start = 0;
        juce::String truncated;
        if (fromPlay)
        {
            const auto origin = host.scheduler.originOf (playSeq);
            if (origin < 0) { error = "fromPlay: the last play has not started yet"; return {}; }
            if (playEpoch != tap.epochNumber()) { error = "fromPlay: the capture was reset (sample rate changed) after the last play"; return {}; }
            start = (juce::uint64) origin;
            if (written > start + (juce::uint64) tap.capacityFrames())
            {
                error = "fromPlay: the last play started more than " + juce::String (AudioTap::secondsKept) + " s ago and is no longer in the capture";
                return {};
            }
        }
        else
            start = written > (juce::uint64) requested ? written - (juce::uint64) requested : 0;
        auto audio = tap.range (start, requested, point == "engine");
        if (audio.getNumSamples() == 0) { error = "no audio captured yet (is the audio device running?)"; return {}; }
        if (audio.getNumSamples() < requested)
            truncated = fromPlay ? "notYetWritten" : "notEnoughAudio";
        // Un fichero por captura: las anteriores siguen valiendo para compare (se conservan las keptCaptures últimas)
        const auto dir = captureDirectory();
        dir.createDirectory();
        const auto n = ++captureCount;
        const auto name = [this] (int k) { return "listen-" + juce::String (port) + "-" + juce::String (k) + ".wav"; };
        if (n > keptCaptures)
            dir.getChildFile (name (n - keptCaptures)).deleteFile();
        const auto file = dir.getChildFile (name (n));
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
        o->setProperty ("point", point);
        o->setProperty ("sampleRate", sr);
        o->setProperty ("seconds", (double) audio.getNumSamples() / sr);
        o->setProperty ("requestedFrames", requested);
        o->setProperty ("capturedFrames", audio.getNumSamples());
        o->setProperty ("startFrame", (juce::int64) start); // posición absoluta en la captura (fromPlay: origen del play)
        o->setProperty ("partial", audio.getNumSamples() < requested);
        if (truncated.isNotEmpty())
            o->setProperty ("truncated", truncated);
        const auto peak = juce::jmax (audio.getMagnitude (0, 0, audio.getNumSamples()), audio.getMagnitude (1, 0, audio.getNumSamples()));
        o->setProperty ("peakDb", juce::Decibels::gainToDecibels (peak, -120.0f));
        return juce::var (o.release());
    }

    static constexpr double maxPlaySeconds = 30.0, maxListenSeconds = 10.0;
    static constexpr int keptCaptures = 32; // WAV de captura conservados por instancia (10 s a 48 kHz = 3,84 MB cada uno)

    Host host;
    // Último play (hilo de mensajes): su número (el planificador da su origen, AgentScheduler::originOf) y la época del tap
    // en que se programó
    juce::uint32 playSeq = 0, playEpoch = 0;
    int captureCount = 0;
    std::unique_ptr<juce::StreamingSocket> listener;
    int port = 0;
    std::atomic<int> activeClients { 0 };
    juce::String token;
    juce::File announce;
    juce::OwnedArray<ClientThread> clients;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
