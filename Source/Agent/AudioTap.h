// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE). No es una cabecera independiente.
#pragma once

//==============================================================================
// Control por agente (F3): copia circular de la salida final del plugin para que el agente "escuche" (Agent/AgentServer.h
// la vuelca a WAV y KoniSynth-mcp la analiza).
//
// Aislamiento: el procesador solo la crea al activar el control por agente y la publica con un puntero atómico; con el
// control desactivado ese puntero es nulo y processBlock no hace más que leerlo (una carga atómica por bloque). El objeto,
// una vez creado, no se destruye hasta destruir el procesador.
// Un escritor (hilo de audio) sin bloqueos; lectores en el hilo de mensajes. Capacidad: 12 s a la frecuencia de trabajo
// (mínimo 48 kHz), dos señales estéreo: 9,2 MB a 48 kHz. Leer N s (máximo 10) mientras el audio avanza es seguro mientras
// la lectura tarde menos que (12 s - N s), porque el escritor solo pisa lo que tiene delante.
// Dos puntos de captura con las mismas posiciones: "output", la salida final (canales 0-1, push), y "engine", la salida del
// motor con sus efectos propios (eco del SNES, reverb del MT-32...) antes de vocoder, monitor de entrada, cadena de efectos,
// Gain y limitador (canales 2-3, pushEngine, en la posición del bloque que push cierra después).
// Cambio de frecuencia (prepare): se vacía (no se mezclan muestras de dos tasas), se vuelve a reservar si 12 s ya no caben y
// sube 'epoch' para que una posición anterior no se lea como de la captura nueva. Ocurre en prepareToPlay, sin push a la vez
// (el host no llama a processBlock mientras prepara); los lectores se excluyen con 'lock', que push nunca toma.
class AudioTap
{
public:
    static constexpr int secondsKept = 12;

    // Reloj del último bloque capturado (estado de tempo para el agente). Lo rellena processBlock con lo que ya decidió: el
    // host (solo los datos que da; nada inventado) y el tempo efectivo del arpegiador/secuenciador/sync (Params::tempoBpm)
    struct Clock
    {
        bool hasBpm = false, hasPpq = false, hasTime = false, playing = false;
        double bpm = 0.0, ppq = 0.0;     // del host, al inicio del bloque
        juce::int64 timeSamples = 0;     // del host, al inicio del bloque
        double tempoBpm = 0.0;           // efectivo: el del host o, sin él, Arp BPM
        float displayBpm = 0.0f;         // el del LED de BPM de la interfaz
        bool displayFromHost = false;
        int blockSize = 0, latencySamples = 0;
        juce::uint64 frame = 0;          // posición en la captura del primer fotograma de ese bloque
        bool valid = false;              // false hasta el primer bloque
    };

    explicit AudioTap (double sampleRate) { prepare (sampleRate); }

    // prepareToPlay (o el hilo de mensajes al crearla). Misma frecuencia: no toca nada (hay hosts que preparan a menudo)
    void prepare (double sr)
    {
        sr = sr > 0.0 ? sr : 48000.0;
        const juce::ScopedLock sl (lock);
        if (capacity > 0 && sr == rate.load())
            return;
        if (const auto needed = secondsKept * juce::jmax (48000, juce::roundToInt (sr)); needed > capacity)
        {
            data.setSize (4, needed);
            capacity = needed;
        }
        data.clear();
        engineWritten = false;
        write.store (0, std::memory_order_release);
        rate.store (sr);
        ++epoch;
    }

    double sampleRate() const { return rate.load(); }
    int capacityFrames() const { return capacity; }
    juce::uint32 epochNumber() const { return epoch.load(); } // cambia cada vez que la captura se vacía (otra frecuencia)

    // Hilo de audio, antes de push en el mismo bloque: la salida del motor (canales 2-3) en las posiciones de este bloque
    void pushEngine (const juce::AudioBuffer<float>& buffer)
    {
        write2 (buffer, 2);
        engineWritten = true;
    }

    // Hilo de audio. Sin asignaciones ni bloqueos. Cierra el bloque: la salida final (canales 0-1) y avanza la posición
    void push (const juce::AudioBuffer<float>& buffer, Clock c)
    {
        const auto n = buffer.getNumSamples();
        auto pos = write.load (std::memory_order_relaxed);
        if (! engineWritten) // un bloque sin pushEngine: silencio en vez de lo que hubiera hace 12 s
            for (int i = 0; i < n;)
            {
                const auto at = (int) ((pos + (juce::uint64) i) % (juce::uint64) capacity);
                const auto chunk = juce::jmin (n - i, capacity - at);
                data.clear (2, at, chunk);
                data.clear (3, at, chunk);
                i += chunk;
            }
        engineWritten = false;
        write2 (buffer, 0);
        // Reloj con seqlock: el contador es impar mientras se escribe y el lector repite si cambia (el escritor no espera)
        c.frame = pos;
        c.blockSize = n;
        c.valid = true;
        clockSeq.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);
        std::memcpy (&clockData, &c, sizeof (Clock));
        clockSeq.fetch_add (1, std::memory_order_release);
        write.store (pos + (juce::uint64) n, std::memory_order_release);
    }

    juce::uint64 framesWritten() const { return write.load (std::memory_order_acquire); }

    // Hilo de mensajes: copia coherente del reloj del último bloque
    Clock clock() const
    {
        Clock c;
        for (int tries = 0; tries < 1000; ++tries)
        {
            const auto s0 = clockSeq.load (std::memory_order_acquire);
            if ((s0 & 1u) != 0)
                continue;
            std::memcpy (&c, &clockData, sizeof (Clock));
            std::atomic_thread_fence (std::memory_order_acquire);
            if (clockSeq.load (std::memory_order_relaxed) == s0)
                return c;
        }
        return {}; // sin copia estable (no ocurre a ritmo de bloque): valid = false
    }

    // Hilo de mensajes: 'frames' fotogramas desde la posición absoluta 'start', de la salida final o (engine) de la del motor.
    // Devuelve lo ya escrito (puede ser menos: el resto aún no ha sonado); vacío si 'start' no se ha escrito todavía o ya se
    // ha pisado (más de 12 s atrás)
    juce::AudioBuffer<float> range (juce::uint64 start, int frames, bool engine = false) const
    {
        const juce::ScopedLock sl (lock);
        const auto end = write.load (std::memory_order_acquire);
        if (frames <= 0 || start >= end || end - start > (juce::uint64) capacity)
            return {};
        const auto n = (int) std::min ((juce::uint64) frames, end - start);
        juce::AudioBuffer<float> out (2, n);
        auto pos = start;
        for (int i = 0; i < n;)
        {
            const auto at = (int) (pos % (juce::uint64) capacity);
            const auto chunk = juce::jmin (n - i, capacity - at);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, i, data, (engine ? 2 : 0) + ch, at, chunk);
            pos += (juce::uint64) chunk;
            i += chunk;
        }
        return out;
    }

private:
    // Copia el bloque en dos canales del anillo desde 'first', en las posiciones del bloque actual (sin avanzar). Mono -> L=R
    void write2 (const juce::AudioBuffer<float>& buffer, int first)
    {
        const auto n = buffer.getNumSamples();
        const auto chans = buffer.getNumChannels();
        const auto pos = write.load (std::memory_order_relaxed);
        for (int i = 0; i < n;)
        {
            const auto at = (int) ((pos + (juce::uint64) i) % (juce::uint64) capacity);
            const auto chunk = juce::jmin (n - i, capacity - at);
            for (int ch = 0; ch < 2; ++ch)
                data.copyFrom (first + ch, at, buffer, juce::jmin (ch, chans - 1), i, chunk);
            i += chunk;
        }
    }

    int capacity = 0;
    bool engineWritten = false; // hilo de audio: pushEngine ya escribió este bloque
    juce::AudioBuffer<float> data;
    std::atomic<juce::uint64> write { 0 };
    std::atomic<double> rate { 0.0 };
    std::atomic<juce::uint32> epoch { 0 };
    Clock clockData;                             // protegido por clockSeq (seqlock)
    std::atomic<juce::uint32> clockSeq { 0 };
    juce::CriticalSection lock; // prepare frente a los lectores; push no la toma
};
