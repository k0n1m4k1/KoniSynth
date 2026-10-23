// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Reloj de pasos para patrones de ritmo. Con transporte del host (bpm + ppq + reproduciendo) sigue su posición;
// sin él corre libre con el tempo propio. Swing: retrasa los pasos impares una fracción de paso.
struct StepClock
{
    struct Transport { std::optional<double> bpm, ppq; bool playing = false; };

    void restart() { pos = -1.0e-9; lastFired = -1; }

    // Llama a fire (muestra, índice de paso absoluto) por cada paso que empieza dentro del bloque
    template <typename Fire>
    void process (int numSamples, double sampleRate, double freeBpm, int stepsPerBeat, double swing, const Transport& t, Fire&& fire)
    {
        const auto synced = t.bpm.has_value() && t.ppq.has_value();
        if (synced && ! t.playing)
        {
            pos = -1.0e9; // al volver a reproducir se resincroniza con la posición del host
            lastFired = -1;
            return;
        }
        const auto bpm = synced ? *t.bpm : freeBpm;
        const auto stepsPerSample = bpm / 60.0 * stepsPerBeat / sampleRate;
        if (synced)
        {
            const auto hostPos = *t.ppq * stepsPerBeat;
            if (std::abs (hostPos - pos) > 0.5) // salto de posición del host (loop, recolocación): resincroniza
                lastFired = (long long) std::ceil (hostPos) - 1;
            pos = hostPos;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            const auto next = lastFired + 1;
            const auto threshold = (double) next + ((next & 1) != 0 ? swing : 0.0);
            if (pos >= threshold)
            {
                fire (i, next);
                lastFired = next;
            }
            pos += stepsPerSample;
        }
    }

    double pos = -1.0e-9;
    long long lastFired = -1;
};
