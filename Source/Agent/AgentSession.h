// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (tras Agent/AgentScheduler.h). Solo usa JUCE.
#pragma once

//==============================================================================
// Sesión de control por el agente: cesión temporal del transporte (grabar y parar) al agente de KoniSynth-mcp.
//
// - El agente pide el control con una tarea (request_control). Sin sesión abierta, la UI pregunta una vez y el usuario
//   elige cuánto dura la sesión (15, 30, 60 o 120 min); mientras dura, el agente toma y suelta el control para cualquier
//   tarea sin volver a preguntar. Sin ventana abierta no se puede conceder (la petición se rechaza).
// - Origen de las notas: las del agente entran por su planificador (Agent/AgentScheduler.h) después de mezclar el
//   controlador, el teclado en pantalla y el .mid del transporte; el hilo de audio cuenta las note-on del usuario antes de
//   ese punto (userNotes) solo mientras el agente tiene el control. Si el usuario toca, la UI pregunta si quiere
//   recuperarlo: Sí = revoke; No o sin respuesta en 8 s = el agente sigue y no se vuelve a preguntar en 30 s.
// - Se pierde el control (sin cerrar la sesión) si el puente deja de hablar 10 s (su hilo de keepalive manda ping cada 3 s
//   mientras tiene el control). Cierran la sesión: Take back control (botón o MIDI Learn action:revoke), PANIC y su caducidad.
// - La sesión no se guarda (ni en el estado ni en config.settings): al cerrar el plugin, la siguiente petición pregunta.
//
// Hilos: userNotes, inControl y revokeRequested se tocan desde el de audio (atómicos); heard, desde los hilos del servidor;
// todo lo demás, solo en el de mensajes (órdenes del servidor vía onMessageThread, tick a 50 Hz y la UI)
class AgentSession
{
public:
    static constexpr double keepaliveMs = 10000.0, askMs = 8000.0, quietMs = 30000.0, noticeMs = 6000.0;
    static inline const int durations[] = { 15, 30, 60, 120 }; // minutos que ofrece el diálogo (30 por defecto)

    // Acciones del procesador sobre el transporte y las notas del agente (las fija KoniSynthProcessor)
    std::function<juce::String (juce::File&)> startRecording; // error vacío si arranca; deja el fichero de la toma
    std::function<juce::File()> stopRecording;                // WAV de la toma (el .mid va al lado si hubo MIDI)
    std::function<void()> cancelAgentNotes;
    // La UI se registra para preguntar (nullptr sin ventana). Recibe la tarea; responde con answer()
    std::function<void (const juce::String& task)> askUser;

    // --- Hilo de audio ---
    std::atomic<bool> inControl { false };
    std::atomic<int> userNotes { 0 };
    std::atomic<bool> revokeRequested { false }; // PANIC desde el hilo de audio: tick() cierra la sesión

    // Hilos del servidor: cualquier petición con token válido cuenta como señal de vida
    void heard() { lastHeard.store (now()); }

    // --- Hilo de mensajes ---
    enum class Request { none, pending, granted, denied };

    // Petición del agente. Devuelve el estado de la petición o un error (sin ventana o tarea vacía)
    juce::var request (const juce::String& taskText, juce::String& error)
    {
        if (taskText.trim().isEmpty()) { error = "task is required (what the agent will do, e.g. \"record the parts of a composition\")"; return {}; }
        task = taskText.trim().substring (0, 200);
        if (sessionOpen())
        {
            grant();
            return status();
        }
        if (request_ != Request::pending)
        {
            if (askUser == nullptr) { error = "the KoniSynth window is closed: the user must open it to allow an agent session"; return {}; }
            request_ = Request::pending;
            askUser (task);
        }
        return status();
    }

    // Respuesta del diálogo: minutos de sesión (0 = Deny)
    void answer (int minutes)
    {
        if (request_ != Request::pending)
            return;
        if (minutes <= 0)
        {
            request_ = Request::denied;
            return;
        }
        sessionMinutes = minutes;
        untilMs = now() + minutes * 60000.0;
        grant();
    }

    // El agente termina su tarea: suelta el control (la sesión sigue abierta)
    juce::var release (const juce::String& summary)
    {
        if (inControl.load())
        {
            const auto take = stopAgentRecording();
            inControl.store (false);
            notify ("Task finished" + (summary.isNotEmpty() ? ": " + summary.substring (0, 200) : juce::String (".")) + take);
        }
        return status();
    }

    // Take back control, PANIC, action:revoke o caducidad: el usuario recupera el control y la sesión se cierra
    void revoke (const juce::String& why)
    {
        const auto had = inControl.load() || sessionOpen();
        if (inControl.exchange (false) && cancelAgentNotes != nullptr)
            cancelAgentNotes();
        const auto take = stopAgentRecording();
        untilMs = 0.0;
        asking = false;
        if (request_ == Request::pending) request_ = Request::denied;
        if (had)
            notify (why + take);
    }

    // Grabación pedida por el agente (solo con el control). Devuelve el WAV de la toma
    juce::var record (juce::String& error)
    {
        if (! inControl.load()) { error = "no control: call request_control first"; return {}; }
        if (agentRecording) { error = "already recording"; return {}; }
        juce::File file;
        if (startRecording == nullptr || (error = startRecording (file)).isNotEmpty())
            return {};
        agentRecording = true;
        takeStart = now();
        part = file.getFileNameWithoutExtension();
        return file.getFullPathName();
    }

    juce::var stopRecord (juce::String& error)
    {
        if (! agentRecording) { error = "not recording"; return {}; }
        const auto wav = stopRecording != nullptr ? stopRecording() : juce::File();
        agentRecording = false;
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("wav", wav.getFullPathName());
        const auto mid = wav.withFileExtension ("mid");
        o->setProperty ("mid", mid.existsAsFile() ? juce::var (mid.getFullPathName()) : juce::var());
        return juce::var (o.release());
    }

    juce::var status() const
    {
        auto o = std::make_unique<juce::DynamicObject>();
        static const char* names[] { "none", "pending", "granted", "denied" };
        o->setProperty ("request", names[(int) request_]);
        o->setProperty ("sessionOpen", sessionOpen());
        o->setProperty ("secondsLeft", sessionOpen() ? juce::roundToInt ((untilMs - now()) / 1000.0) : 0);
        o->setProperty ("inControl", inControl.load());
        o->setProperty ("recording", agentRecording);
        o->setProperty ("task", task);
        return juce::var (o.release());
    }

    // 50 Hz (KoniSynthProcessor::pollActions): caducidad, señal de vida y notas del usuario
    void tick()
    {
        const auto t = now();
        if (revokeRequested.exchange (false))
            revoke ("PANIC: control taken back, agent session closed.");
        if (untilMs > 0.0 && t >= untilMs)
            revoke ("Agent session ended (" + juce::String (sessionMinutes) + " min). The next request will ask again.");
        if (inControl.load() && t - lastHeard.load() > keepaliveMs)
        {
            if (cancelAgentNotes != nullptr) cancelAgentNotes();
            const auto take = stopAgentRecording();
            inControl.store (false);
            notify ("Agent disconnected: no message for 10 s. Its notes were cancelled." + take);
        }
        const auto played = userNotes.exchange (0);
        if (! inControl.load())
            asking = false;
        else if (asking && t >= askUntil)
            keep(); // sin respuesta: el agente sigue
        else if (played > 0 && ! asking && t >= quietUntil && askUser != nullptr)
        {
            asking = true;
            askUntil = t + askMs;
        }
    }

    // Respuesta "Let it continue" (o sin respuesta): no se vuelve a preguntar en 30 s
    void keep()
    {
        asking = false;
        quietUntil = now() + quietMs;
    }

    // Lo que dibuja la tarjeta central (UI)
    bool sessionOpen() const { return untilMs > 0.0 && now() < untilMs; }
    bool isAsking() const { return asking; }
    double askFraction() const { return asking ? juce::jlimit (0.0, 1.0, (askUntil - now()) / askMs) : 0.0; }
    bool isRecording() const { return agentRecording; }
    double takeSeconds() const { return agentRecording ? (now() - takeStart) / 1000.0 : 0.0; }
    double secondsLeft() const { return sessionOpen() ? (untilMs - now()) / 1000.0 : 0.0; }
    const juce::String& getTask() const { return task; }
    const juce::String& getPart() const { return part; }
    juce::String noticeText() const { return now() < noticeUntil ? notice : juce::String(); }
    double noticeFraction() const { return juce::jlimit (0.0, 1.0, (noticeUntil - now()) / noticeMs); }
    void dismissNotice() { noticeUntil = 0.0; }

private:
    static double now() { return juce::Time::getMillisecondCounterHiRes(); }

    void grant()
    {
        request_ = Request::granted;
        inControl.store (true);
        lastHeard.store (now());
        asking = false;
        quietUntil = 0.0;
    }

    // Para la toma del agente si la hay; devuelve el texto para el aviso ("" sin toma)
    juce::String stopAgentRecording()
    {
        if (! agentRecording)
            return {};
        const auto seconds = takeSeconds();
        const auto wav = stopRecording != nullptr ? stopRecording() : juce::File();
        agentRecording = false;
        return " Take \"" + wav.getFileNameWithoutExtension() + "\" stopped at " + juce::String (seconds, 1) + " s and kept.";
    }

    void notify (const juce::String& text)
    {
        notice = text;
        noticeUntil = now() + noticeMs;
    }

    std::atomic<double> lastHeard { 0.0 };
    Request request_ = Request::none;
    juce::String task, part, notice;
    int sessionMinutes = 30;
    double untilMs = 0.0, askUntil = 0.0, quietUntil = 0.0, noticeUntil = 0.0, takeStart = 0.0;
    bool asking = false, agentRecording = false;
};
