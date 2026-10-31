// Cabecera independiente (solo JUCE): la incluye KoniSynth.cpp y también Bench/KoniBench.cpp (schedtest), sin el plugin.
#pragma once

#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
// Control por agente (S2): planificador de las notas de play con el reloj de audio. Sustituye a los temporizadores del hilo
// de mensajes (resolución de 1-16 ms y deriva entre notas): cada nota se entrega en su muestra exacta, como evento MIDI con
// offset dentro del bloque, en el mismo punto de processBlock que el teclado en pantalla (antes de escala, secuenciador y
// arpegiador).
//
// Hilos: un productor (hilo de mensajes: AgentServer) y un consumidor (hilo de audio: process). Los comandos pasan por una
// cola SPSC de capacidad fija (AbstractFifo); el hilo de audio no asigna ni bloquea. Capacidades explícitas: un play que no
// quepa se rechaza antes de sonar (canSchedule).
//
// Créditos: cada nota aceptada reserva 2 eventos (on y off) en 'reserved' antes de publicarse en la cola, y los conserva en
// todo el recorrido cola → pendientes; el hilo de audio los devuelve al entregar o descartar cada evento. Invariante:
// eventos en la cola + eventos pendientes <= reserved <= maxPending, así que la inserción nunca se queda sin hueco (un
// note-off no se puede perder). Solo el productor suma créditos: comprobar y sumar no compite con otro productor.
//
// Cortes (cancel desde el hilo de mensajes, reset desde el de audio): suben 'generation' (atómico, sin pasar por la cola:
// funciona con la cola llena). Cada comando lleva la generación vigente al publicarse; el hilo de audio, al ver una
// generación nueva, suelta las alturas que sonaban y descarta lo pendiente, y descarta de la cola los comandos de una
// generación anterior. Un play publicado después del corte lleva la generación nueva y suena.
//
// Tiempo: un play fija su origen en la primera muestra del bloque en que el hilo de audio lo recibe. Cada evento va a
// origen + round(fs · t), con t absoluto en segundos desde el origen (no se acumulan intervalos redondeados). El tempo de
// un play en beats se convierte una vez al recibirlo (tempo congelado: no sigue un cambio de tempo del host posterior).
// El contador de muestras es propio y monótono (no salta con un seek del host).
//
// Semántica MIDI (canal 1, una nota por altura): en la misma muestra, note-off antes que note-on. Dos notas solapadas de
// la misma altura: la segunda redispara (note-off + note-on) y la altura suena hasta el final de la última (contador por
// altura); no hay identificadores de voz. Cancelar (stop_notes) descarta lo pendiente y suelta las alturas que sonaban.
class AgentScheduler
{
public:
    static constexpr int maxPending = 1024; // eventos programados a la vez (512 notas)
    static constexpr int fifoSize = 1024;   // comandos en tránsito

    // --- Hilo de mensajes ---

    // ¿Cabe un play de n notas? Cota conservadora: el hilo de audio solo puede haber devuelto créditos o vaciado la cola
    // desde la lectura
    bool canSchedule (int notes) const
    {
        return notes >= 0 && fifo.getFreeSpace() >= notes + 1 && reserved.load (std::memory_order_acquire) + 2 * notes <= maxPending;
    }

    // Programa un play: 'seq' lo identifica (originOf) y 'notes' van en segundos desde su origen. Llamar antes a canSchedule
    struct Note { int note, velocity; double start, duration; };
    template <typename Notes>
    bool schedule (juce::uint32 seq, const Notes& notes)
    {
        if (! canSchedule ((int) notes.size()))
            return false;
        reserved.fetch_add (2 * (int) notes.size(), std::memory_order_acq_rel); // antes de publicar: el consumidor los devuelve
        const auto gen = generation.load (std::memory_order_acquire);
        // El play entero (inicio y notas) en una sola escritura: AbstractFifo lo publica al cerrar el ámbito, todo a la vez.
        // Con una escritura por comando, el hilo de audio podía leer el inicio en un bloque (origen = ese bloque) y las notas
        // en el siguiente: llegaban con offset negativo (-tamaño de bloque) y juce::Synthesiser las descartaba
        const auto total = 1 + (int) notes.size();
        const auto scope = fifo.write (total);
        jassert (scope.blockSize1 + scope.blockSize2 == total); // canSchedule comprobó el hueco y solo este hilo escribe
        int k = 0;
        const auto put = [&] (const Command& c)
        {
            const auto index = k < scope.blockSize1 ? scope.startIndex1 + k : scope.startIndex2 + (k - scope.blockSize1);
            commands[(size_t) index] = c;
            ++k;
        };
        put ({ Command::kStart, seq, gen, 0, 0, 0.0, 0.0 });
        for (const auto& n : notes)
            put ({ Command::kNote, seq, gen, n.note, n.velocity, n.start, n.duration });
        return true;
    }

    // Descarta lo programado hasta ahora y suelta lo que sonaba (en el siguiente bloque). No usa la cola
    void cancel() { generation.fetch_add (1, std::memory_order_acq_rel); }

    // Origen (posición en la captura de audio) del play 'seq', o -1 si el hilo de audio aún no lo ha recibido
    juce::int64 originOf (juce::uint32 seq) const
    {
        const auto v = originPacked.load (std::memory_order_acquire); // play y origen en un solo atómico: siempre coherentes
        return v != noOrigin && (v >> frameBits) == (seq & seqMask) ? (juce::int64) (v & frameMask) : -1;
    }

    // --- Hilo de audio ---

    // PANIC o cambio de frecuencia (sin process a la vez): lo pendiente y lo que aún está en la cola se descarta, y las
    // alturas que sonaban se sueltan en el siguiente bloque
    void reset() { generation.fetch_add (1, std::memory_order_acq_rel); }

    // Añade a 'midi' los eventos de [ahora, ahora + numSamples). 'captureFrame' = posición en la captura de audio de la
    // primera muestra del bloque (para el origen que lee listen); fs = frecuencia actual
    void process (juce::MidiBuffer& midi, int numSamples, double fs, juce::uint64 captureFrame)
    {
        if (const auto g = generation.load (std::memory_order_acquire); g != seenGen)
            cut (midi, g);
        drain (midi, fs, captureFrame);
        const auto end = now + numSamples;
        int i = 0;
        for (; i < count && pending[(size_t) i].at < end; ++i)
        {
            const auto& e = pending[(size_t) i];
            // Un evento anterior al bloque (no debe ocurrir: el play se publica entero) se entrega al principio en vez de
            // con offset negativo, que juce::Synthesiser descartaría; se cuenta para las pruebas
            if (e.at < now)
                lateEvents.fetch_add (1, std::memory_order_relaxed);
            const auto offset = (int) juce::jmax ((juce::int64) 0, e.at - now);
            auto& a = active[(size_t) e.note];
            if (e.on)
            {
                if (a > 0) // la misma altura ya suena (de un play): redisparo
                    midi.addEvent (juce::MidiMessage::noteOff (1, e.note), offset);
                ++a;
                midi.addEvent (juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.velocity), offset);
            }
            else if (a > 0 && --a == 0)
                midi.addEvent (juce::MidiMessage::noteOff (1, e.note), offset);
        }
        if (i > 0) // quita los entregados (desplazamiento: como mucho maxPending por bloque, sin asignar)
        {
            for (int k = i; k < count; ++k)
                pending[(size_t) (k - i)] = pending[(size_t) k];
            count -= i;
            reserved.fetch_sub (i, std::memory_order_acq_rel);
        }
        now = end;
    }

    bool idle() const { return count == 0 && fifo.getNumReady() == 0; } // sin nada que entregar (hilo de audio)
    int reservedEvents() const { return reserved.load(); }             // créditos en uso (pruebas)
    int lateEventCount() const { return lateEvents.load(); }           // eventos que llegaron tarde a su bloque (pruebas)

private:
    struct Command
    {
        enum Kind { kStart, kNote } kind;
        juce::uint32 seq, gen;
        int note, velocity;
        double start, duration;
    };
    struct Event
    {
        juce::int64 at;
        juce::uint32 order; // orden de llegada: desempata eventos iguales de forma estable
        juce::uint8 note, velocity;
        bool on;
    };

    void drain (juce::MidiBuffer& midi, double fs, juce::uint64 captureFrame)
    {
        auto n = fifo.getNumReady();
        while (n > 0)
        {
            const auto scope = fifo.read (n);
            for (int part = 0; part < 2; ++part)
            {
                const auto from = part == 0 ? scope.startIndex1 : scope.startIndex2;
                const auto size = part == 0 ? scope.blockSize1 : scope.blockSize2;
                for (int k = 0; k < size; ++k)
                    apply (commands[(size_t) (from + k)], midi, fs, captureFrame);
            }
            n = fifo.getNumReady();
        }
    }

    void apply (const Command& c, juce::MidiBuffer& midi, double fs, juce::uint64 captureFrame)
    {
        const auto age = (juce::int32) (c.gen - seenGen); // diferencia con signo: vale aunque el contador dé la vuelta
        if (age < 0) // de antes de un corte: fuera, y devuelve sus créditos
        {
            if (c.kind == Command::kNote)
                reserved.fetch_sub (2, std::memory_order_acq_rel);
            return;
        }
        if (age > 0) // publicado tras un corte que este bloque aún no había visto (leyó 'generation' antes): el corte primero
            cut (midi, c.gen);
        switch (c.kind)
        {
            case Command::kStart:
                origin = now;
                originPacked.store (((juce::uint64) (c.seq & seqMask) << frameBits) | (captureFrame & frameMask), std::memory_order_release);
                break;
            case Command::kNote:
            {
                const auto on = origin + (juce::int64) std::llround (fs * c.start);
                const auto off = juce::jmax (on + 1, origin + (juce::int64) std::llround (fs * (c.start + c.duration)));
                insert ({ on, nextOrder++, (juce::uint8) c.note, (juce::uint8) c.velocity, true });
                insert ({ off, nextOrder++, (juce::uint8) c.note, 0, false });
                break;
            }
        }
    }

    // Inserción ordenada por (muestra, note-off antes que note-on, orden de llegada). Siempre hay hueco: los créditos lo
    // reservaron al aceptar el play (invariante de la cabecera)
    void insert (const Event& e)
    {
        jassert (count < maxPending);
        if (count >= maxPending)
            return;
        const auto before = [] (const Event& a, const Event& b)
        {
            if (a.at != b.at) return a.at < b.at;
            if (a.on != b.on) return ! a.on;
            return a.order < b.order;
        };
        auto i = count;
        while (i > 0 && before (e, pending[(size_t) (i - 1)]))
        {
            pending[(size_t) i] = pending[(size_t) (i - 1)];
            --i;
        }
        pending[(size_t) i] = e;
        ++count;
    }

    // Corte: adopta la generación 'g', descarta lo pendiente (devuelve sus créditos) y suelta lo que sonaba
    void cut (juce::MidiBuffer& midi, juce::uint32 g)
    {
        seenGen = g;
        reserved.fetch_sub (count, std::memory_order_acq_rel);
        count = 0;
        for (int n = 0; n < 128; ++n)
            if (active[(size_t) n] > 0)
            {
                active[(size_t) n] = 0;
                midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            }
    }

    // Cola SPSC
    juce::AbstractFifo fifo { fifoSize };
    std::array<Command, (size_t) fifoSize> commands {};
    // Estado del hilo de audio
    std::array<Event, (size_t) maxPending> pending {};
    int count = 0;
    std::array<int, 128> active {};
    juce::int64 now = 0, origin = 0;
    juce::uint32 nextOrder = 0, seenGen = 0;
    // Compartido entre los dos hilos
    std::atomic<int> reserved { 0 };            // eventos aceptados aún no entregados ni descartados (cola + pendientes)
    std::atomic<juce::uint32> generation { 0 }; // sube con cada corte (cancel, reset)
    std::atomic<int> lateEvents { 0 };          // eventos con su muestra ya pasada al entregarlos (debe quedar en 0)
    // Último play recibido: número (24 bits) y origen (40 bits: 2^40 muestras = 132 días a 96 kHz)
    static constexpr int frameBits = 40;
    static constexpr juce::uint64 frameMask = (juce::uint64 (1) << frameBits) - 1, seqMask = (1u << 24) - 1, noOrigin = ~juce::uint64 (0);
    std::atomic<juce::uint64> originPacked { noOrigin };
};
