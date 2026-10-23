// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE). No es una cabecera independiente.
#pragma once

//==============================================================================
// Control por agente (F3): copia circular de la salida final del plugin para que el agente "escuche" (Agent/AgentServer.h
// la vuelca a WAV y KoniSynth-mcp la analiza).
//
// Aislamiento: el procesador solo la crea al activar el control por agente y la publica con un puntero atómico; con el
// control desactivado ese puntero es nulo y processBlock no hace más que leerlo (una carga atómica por bloque). La
// memoria, una vez creada, no se libera hasta destruir el procesador: así el hilo de audio nunca escribe en memoria
// liberada (desactivar solo deja de publicarla).
// Un escritor (hilo de audio) sin bloqueos; lectores en el hilo de mensajes. Capacidad: 12 s a la frecuencia con que se
// creó (mínimo 48 kHz): 4,6 MB a 48 kHz estéreo. Leer los últimos N s mientras el audio avanza es seguro mientras la
// lectura tarde menos que (12 s - N s), porque el escritor solo pisa lo que tiene delante.
class AudioTap
{
public:
    explicit AudioTap (double sampleRate)
        : capacity (12 * juce::jmax (48000, juce::roundToInt (sampleRate))), data (2, capacity)
    {
        data.clear();
        rate.store (sampleRate > 0.0 ? sampleRate : 48000.0);
    }

    void setSampleRate (double sr) { if (sr > 0.0) rate.store (sr); }
    double sampleRate() const { return rate.load(); }
    int capacityFrames() const { return capacity; }

    // Hilo de audio. Sin asignaciones ni bloqueos
    void push (const juce::AudioBuffer<float>& buffer)
    {
        const auto n = buffer.getNumSamples();
        const auto chans = buffer.getNumChannels();
        auto pos = write.load (std::memory_order_relaxed);
        for (int i = 0; i < n;)
        {
            const auto chunk = juce::jmin (n - i, capacity - (int) (pos % capacity));
            const auto at = (int) (pos % capacity);
            for (int ch = 0; ch < 2; ++ch)
                data.copyFrom (ch, at, buffer, juce::jmin (ch, chans - 1), i, chunk);
            pos += (juce::uint64) chunk;
            i += chunk;
        }
        write.store (pos, std::memory_order_release);
    }

    // Hilo de mensajes: los últimos 'frames' fotogramas (como mucho la capacidad), del más antiguo al más reciente
    juce::AudioBuffer<float> last (int frames) const
    {
        const auto end = write.load (std::memory_order_acquire);
        frames = (int) juce::jmin ((juce::uint64) juce::jmin (frames, capacity), end);
        juce::AudioBuffer<float> out (2, frames);
        auto pos = end - (juce::uint64) frames;
        for (int i = 0; i < frames;)
        {
            const auto at = (int) (pos % capacity);
            const auto chunk = juce::jmin (frames - i, capacity - at);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, i, data, ch, at, chunk);
            pos += (juce::uint64) chunk;
            i += chunk;
        }
        return out;
    }

    juce::uint64 framesWritten() const { return write.load (std::memory_order_acquire); }

    // Hilo de mensajes: 'frames' fotogramas desde la posición absoluta 'start' (la parte aún no escrita o ya pisada se
    // recorta). Para escuchar exactamente desde que sonó una nota
    juce::AudioBuffer<float> range (juce::uint64 start, int frames) const
    {
        const auto end = write.load (std::memory_order_acquire);
        if (start >= end || end - start > (juce::uint64) capacity)
            return {};
        const auto n = (int) juce::jmin ((juce::uint64) frames, end - start);
        juce::AudioBuffer<float> out (2, n);
        auto pos = start;
        for (int i = 0; i < n;)
        {
            const auto at = (int) (pos % capacity);
            const auto chunk = juce::jmin (n - i, capacity - at);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, i, data, ch, at, chunk);
            pos += (juce::uint64) chunk;
            i += chunk;
        }
        return out;
    }

private:
    const int capacity;
    juce::AudioBuffer<float> data;
    std::atomic<juce::uint64> write { 0 };
    std::atomic<double> rate { 48000.0 };
};
