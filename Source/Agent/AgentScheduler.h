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

    // ¿Cabe un play de n notas? Cota conservadora: el hilo de audio solo puede haber vaciado más desde la lectura
    bool canSchedule (int notes) const
    {
        return fifo.getFreeSpace() >= notes + 1 && pendingCount.load() + 2 * (fifo.getNumReady() + notes) <= maxPending;
    }

    // Programa un play: 'seq' lo identifica (originOf) y 'notes' van en segundos desde su origen. Llamar antes a canSchedule
    struct Note { int note, velocity; double start, duration; };
    template <typename Notes>
    bool schedule (juce::uint32 seq, const Notes& notes)
    {
        if (! canSchedule ((int) notes.size()))
            return false;
        push ({ Command::kStart, seq, 0, 0, 0.0, 0.0 });
        for (const auto& n : notes)
            push ({ Command::kNote, seq, n.note, n.velocity, n.start, n.duration });
        return true;
    }

    void cancel() { push ({ Command::kCancel, 0, 0, 0, 0.0, 0.0 }); }

    // Origen (posición en la captura de audio) del play 'seq', o -1 si el hilo de audio aún no lo ha recibido
    juce::int64 originOf (juce::uint32 seq) const
    {
        const auto v = originPacked.load (std::memory_order_acquire); // play y origen en un solo atómico: siempre coherentes
        return v != noOrigin && (v >> frameBits) == (seq & seqMask) ? (juce::int64) (v & frameMask) : -1;
    }

    // --- Hilo de audio ---

    // Al cambiar la frecuencia (prepareToPlay, sin process a la vez): lo pendiente se descarta y las alturas que sonaban
    // se sueltan en el siguiente bloque
    void reset() { dropAll = true; }

    // Añade a 'midi' los eventos de [ahora, ahora + numSamples). 'captureFrame' = posición en la captura de audio de la
    // primera muestra del bloque (para el origen que lee listen); fs = frecuencia actual
    void process (juce::MidiBuffer& midi, int numSamples, double fs, juce::uint64 captureFrame)
    {
        if (dropAll)
        {
            dropAll = false;
            releaseAll (midi);
        }
        drain (midi, fs, captureFrame);
        const auto end = now + numSamples;
        int i = 0;
        for (; i < count && pending[(size_t) i].at < end; ++i)
        {
            const auto& e = pending[(size_t) i];
            const auto offset = (int) (e.at - now);
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
        }
        pendingCount.store (count);
        now = end;
    }

    bool idle() const { return count == 0 && fifo.getNumReady() == 0; } // sin nada que entregar (hilo de audio)

private:
    struct Command
    {
        enum Kind { kStart, kNote, kCancel } kind;
        juce::uint32 seq;
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

    void push (const Command& c)
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)      commands[(size_t) scope.startIndex1] = c;
        else if (scope.blockSize2 > 0) commands[(size_t) scope.startIndex2] = c;
    }

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
            case Command::kCancel:
                count = 0;
                releaseAll (midi);
                break;
        }
    }

    // Inserción ordenada por (muestra, note-off antes que note-on, orden de llegada). Sin hueco (no ocurre: canSchedule)
    void insert (const Event& e)
    {
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

    void releaseAll (juce::MidiBuffer& midi)
    {
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
    juce::uint32 nextOrder = 0;
    std::atomic<bool> dropAll { false };
    // Publicado al hilo de mensajes
    std::atomic<int> pendingCount { 0 };
    // Último play recibido: número (24 bits) y origen (40 bits: 2^40 muestras = 132 días a 96 kHz)
    static constexpr int frameBits = 40;
    static constexpr juce::uint64 frameMask = (juce::uint64 (1) << frameBits) - 1, seqMask = (1u << 24) - 1, noOrigin = ~juce::uint64 (0);
    std::atomic<juce::uint64> originPacked { noOrigin };
};
