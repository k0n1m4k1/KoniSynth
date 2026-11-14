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
        if (index >= fm1Base && index < fm1Base + fm1Count)
            handleFm1 (index - fm1Base, m.getControllerValue());
        else if (index >= actionBase && index < actionBase + numActions) // botón de acción: se dispara al cruzar 64 hacia arriba
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

    // Mandos del panel del FM-1 (Engines/Fm1.h; no son parámetros): índices fm1Base + k. k 0-13 botones (tecla de la
    // matriz: pulsado mientras el CC >= 64), 16-22 encoders (el CC absoluto se toma como diferencia: kFm1EncSpan clics de 0
    // a 127), 24 MASTER (0-127 -> ADC 0-1023). El hilo de audio deja lo pendiente; el de mensajes lo manda al aparato
    // (KoniSynthProcessor::pollActions). Se guardan por su id ("fm1:key:N", "fm1:enc:N", "fm1:master")
    static constexpr int fm1Base = 1 << 21, fm1Count = 25, fm1EncFirst = 16, fm1Master = 24;
    static constexpr float kFm1EncSpan = 48.0f;
    static juce::String fm1Id (int k)
    {
        return k < 14 ? "fm1:key:" + juce::String (k) : k < fm1Master ? "fm1:enc:" + juce::String (k - fm1EncFirst) : juce::String ("fm1:master");
    }
    static int fm1FromId (const juce::String& id)
    {
        if (id == "fm1:master") return fm1Master;
        if (id.startsWith ("fm1:key:")) return juce::jlimit (0, 13, id.getTrailingIntValue());
        if (id.startsWith ("fm1:enc:")) return fm1EncFirst + juce::jlimit (0, 6, id.getTrailingIntValue());
        return -1;
    }
    std::array<std::atomic<int>, 14> fm1KeyState = [] { std::array<std::atomic<int>, 14> a; for (auto& x : a) x = -1; return a; }(); // -1 nada
    std::array<std::atomic<int>, 7> fm1Clicks {};          // clics pendientes por encoder
    std::atomic<int> fm1MasterValue { -1 };                // ADC pendiente; -1 nada
    std::array<int, 7> fm1EncLast = [] { std::array<int, 7> a; a.fill (-1); return a; }(); // (solo hilo de audio)
    std::array<float, 7> fm1EncAcc {};
    std::array<int, 14> fm1KeyLast {};

    void handleFm1 (int k, int v)
    {
        if (k < 14)
        {
            const auto down = v >= 64 ? 1 : 0;
            if (down != fm1KeyLast[(size_t) k])
            {
                fm1KeyLast[(size_t) k] = down;
                fm1KeyState[(size_t) k].store (down);
            }
        }
        else if (k < fm1Master)
        {
            const auto e = (size_t) (k - fm1EncFirst);
            if (fm1EncLast[e] >= 0)
            {
                fm1EncAcc[e] += (float) (v - fm1EncLast[e]) * kFm1EncSpan / 127.0f;
                const auto d = (int) fm1EncAcc[e]; // (hacia cero: el resto queda para el siguiente)
                if (d != 0)
                {
                    fm1EncAcc[e] -= (float) d;
                    fm1Clicks[e].fetch_add (d);
                }
            }
            fm1EncLast[e] = v; // (el primer valor solo sitúa el mando)
        }
        else
            fm1MasterValue.store (juce::roundToInt ((float) v * 1023.0f / 127.0f));
    }

    std::array<std::atomic<int>, 120> ccToParam;
    std::atomic<int> learning { -1 }; // índice del parámetro esperando un CC; -1 = ninguno
    std::atomic<int> rejectedCc { -1 }; // último CC 120-127 recibido durante MIDI Learn (lo consume la UI)
};
