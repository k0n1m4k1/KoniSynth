// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// MIDI learn: CC (0–119) -> índice de parámetro. La UI escribe "learning" y borra asignaciones; el hilo de audio
// lee y asigna. Todo atómico, sin bloqueos. Los CC 120–127 son mensajes de modo de canal (All Sound Off,
// Reset All Controllers, All Notes Off...): no se pueden asignar.
struct MidiLearn
{
    MidiLearn()
    {
        for (auto& p : ccToParam)
            p = -1;
    }

    int ccFor (int paramIndex) const
    {
        for (int cc = 0; cc < 120; ++cc)
            if (ccToParam[(size_t) cc].load() == paramIndex)
                return cc;
        return -1;
    }

    void clear (int paramIndex)
    {
        for (auto& p : ccToParam)
            if (p.load() == paramIndex)
                p = -1;
    }

    void clearAll()
    {
        for (auto& p : ccToParam)
            p = -1;
    }

    void assign (int cc, int paramIndex)
    {
        clear (paramIndex); // un parámetro, un CC
        ccToParam[(size_t) cc] = paramIndex;
    }

    // Hilo de audio. setValueNotifyingHost desde el hilo de audio es el patrón habitual en JUCE (el wrapper encola el aviso).
    void handle (const juce::MidiMessage& m, const juce::Array<juce::AudioProcessorParameter*>& params)
    {
        if (! m.isController())
            return;
        const auto cc = m.getControllerNumber();
        if (cc >= 120)
        {
            if (learning.load() >= 0)
                rejectedCc.store (cc); // la UI avisa; el parámetro sigue esperando un CC válido
            return;
        }

        if (const auto target = learning.exchange (-1); target >= 0)
            assign (cc, target);

        const auto index = ccToParam[(size_t) cc].load();
        if (index >= actionBase && index < actionBase + numActions) // botón de acción: se dispara al cruzar 64 hacia arriba
        {
            const auto k = (size_t) (index - actionBase);
            const auto v = m.getControllerValue();
            if (v >= 64 && actionLast[k] < 64)
                actionPending[k].store (true);
            actionLast[k] = v;
        }
        else if (index >= 0 && index < params.size())
            params[index]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
    }

    // Acciones sin parámetro (PANIC y transporte): ocupan los índices actionBase + k de ccToParam (los parámetros nunca
    // llegan ahí). El hilo de audio marca actionPending; PANIC lo consume el propio processBlock y el transporte, el hilo
    // de mensajes (KoniSynthProcessor::pollActions, 50 Hz)
    // actionRevoke: Take back control de la sesión del agente (Agent/AgentSession.h). Se guardan por su id: solo al final
    enum Action { actionPanic = 0, actionRecord, actionStop, actionPlay, actionRevoke, numActions };
    static constexpr int actionBase = 1 << 20;
    static const char* actionId (int k) { static const char* ids[numActions] { "panic", "record", "stop", "play", "revoke" }; return ids[k]; }
    bool takeAction (int k) { return actionPending[(size_t) k].exchange (false); }
    std::array<std::atomic<bool>, numActions> actionPending {};
    std::array<int, numActions> actionLast {}; // último valor del CC (solo hilo de audio)

    std::array<std::atomic<int>, 120> ccToParam;
    std::atomic<int> learning { -1 }; // índice del parámetro esperando un CC; -1 = ninguno
    std::atomic<int> rejectedCc { -1 }; // último CC 120-127 recibido durante MIDI Learn (lo consume la UI)
};
