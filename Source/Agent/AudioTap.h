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
// (mínimo 48 kHz): 4,6 MB a 48 kHz estéreo. Leer N s (máximo 10) mientras el audio avanza es seguro mientras la lectura
// tarde menos que (12 s - N s), porque el escritor solo pisa lo que tiene delante.
// Cambio de frecuencia (prepare): se vacía (no se mezclan muestras de dos tasas), se vuelve a reservar si 12 s ya no caben y
// sube 'epoch' para que una posición anterior no se lea como de la captura nueva. Ocurre en prepareToPlay, sin push a la vez
// (el host no llama a processBlock mientras prepara); los lectores se excluyen con 'lock', que push nunca toma.
class AudioTap
{
public:
    static constexpr int secondsKept = 12;

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
            data.setSize (2, needed);
            capacity = needed;
        }
        data.clear();
        write.store (0, std::memory_order_release);
        rate.store (sr);
        ++epoch;
    }

    double sampleRate() const { return rate.load(); }
    int capacityFrames() const { return capacity; }
    juce::uint32 epochNumber() const { return epoch.load(); } // cambia cada vez que la captura se vacía (otra frecuencia)

    // Hilo de audio. Sin asignaciones ni bloqueos
    void push (const juce::AudioBuffer<float>& buffer)
    {
        const auto n = buffer.getNumSamples();
        const auto chans = buffer.getNumChannels();
        auto pos = write.load (std::memory_order_relaxed);
        for (int i = 0; i < n;)
        {
            const auto chunk = juce::jmin (n - i, capacity - (int) (pos % (juce::uint64) capacity));
            const auto at = (int) (pos % (juce::uint64) capacity);
            for (int ch = 0; ch < 2; ++ch)
                data.copyFrom (ch, at, buffer, juce::jmin (ch, chans - 1), i, chunk);
            pos += (juce::uint64) chunk;
            i += chunk;
        }
        write.store (pos, std::memory_order_release);
    }

    juce::uint64 framesWritten() const { return write.load (std::memory_order_acquire); }

    // Hilo de mensajes: 'frames' fotogramas desde la posición absoluta 'start'. Devuelve lo ya escrito (puede ser menos:
    // el resto aún no ha sonado); vacío si 'start' no se ha escrito todavía o ya se ha pisado (más de 12 s atrás)
    juce::AudioBuffer<float> range (juce::uint64 start, int frames) const
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
                out.copyFrom (ch, i, data, ch, at, chunk);
            pos += (juce::uint64) chunk;
            i += chunk;
        }
        return out;
    }

private:
    int capacity = 0;
    juce::AudioBuffer<float> data;
    std::atomic<juce::uint64> write { 0 };
    std::atomic<double> rate { 0.0 };
    std::atomic<juce::uint32> epoch { 0 };
    juce::CriticalSection lock; // prepare frente a los lectores; push no la toma
};
