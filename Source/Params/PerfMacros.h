// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, APVTS). No es una cabecera independiente.
#pragma once

// Macros de interpretación: 8 parámetros "Macro 1..8" (0..1) que mueven a la vez hasta 8 parámetros cada uno, en línea
// recta entre dos valores (From/To, normalizados). Distintas de las macros por frame de las consolas (Dsp/FrameMacro.h).
// Asignaciones en la propiedad de estado "perfMacros" (una línea "macro|id|from|to", macro 1..8) y nombres en
// "perfMacroNames" ("n1|n2|...|n8"): viajan con el estado del plugin y con los presets.
// Un parámetro pertenece como mucho a una macro (asignarlo a otra lo mueve). Los destinos solo se escriben cuando la
// macro cambia de valor (o cambian las asignaciones): mover a mano un destino no lo "pelea" la macro hasta que esta se mueva.
// Hilos: update() y la edición en el de mensajes; apply() en el de audio, con try-lock (si el de mensajes está publicando
// una tabla nueva, ese bloque no aplica y el siguiente sí: ningún bloqueo en el hilo de audio).
class PerfMacros
{
public:
    static constexpr int numMacros = 8, maxTargets = 8;
    static constexpr const char* mapProp = "perfMacros";
    static constexpr const char* namesProp = "perfMacroNames";

    struct Target { juce::String id; float from = 0.0f, to = 1.0f; };

    void attach (APVTS& s)
    {
        apvts = &s;
        for (int m = 0; m < numMacros; ++m)
            raw[(size_t) m] = s.getRawParameterValue (paramId (m));
    }

    static juce::String paramId (int m) { return "macro" + juce::String (m + 1); }
    static bool isMacroParam (const juce::String& id) { return id.startsWith ("macro") && id.substring (5).containsOnly ("0123456789"); }

    // Hilo de mensajes: interpreta el texto si ha cambiado y publica la tabla para el hilo de audio
    void update()
    {
        const auto text = apvts->state.getProperty (mapProp).toString();
        const auto h = text.hashCode64();
        if (h == hash)
            return;
        hash = h;
        Table t {};
        for (const auto& target : parse (text))
            if (auto* p = apvts->getParameter (target.second.id); p != nullptr && ! isMacroParam (target.second.id))
            {
                auto& n = t.count[(size_t) target.first];
                if (n < maxTargets)
                    t.slots[(size_t) target.first][(size_t) n++] = { p, target.second.from, target.second.to };
            }
        const juce::SpinLock::ScopedLockType lock (tableLock);
        table = t;
        dirty = true;
    }

    // Hilo de audio, al principio del bloque (tras el MIDI learn: un CC asignado a una macro actúa en este mismo bloque)
    void apply()
    {
        const juce::SpinLock::ScopedTryLockType lock (tableLock);
        if (! lock.isLocked())
            return;
        const auto force = dirty;
        dirty = false;
        for (int m = 0; m < numMacros; ++m)
        {
            const auto v = raw[(size_t) m]->load (std::memory_order_relaxed);
            if (! force && v == last[(size_t) m])
                continue;
            last[(size_t) m] = v;
            for (int i = 0; i < table.count[(size_t) m]; ++i)
            {
                const auto& s = table.slots[(size_t) m][(size_t) i];
                const auto target = juce::jlimit (0.0f, 1.0f, s.from + (s.to - s.from) * v);
                if (std::abs (s.param->getValue() - target) > 1.0e-6f)
                    s.param->setValueNotifyingHost (target);
            }
        }
    }

    // --- Edición (hilo de mensajes) ---
    std::vector<Target> targets (int m) const
    {
        std::vector<Target> out;
        for (const auto& t : parse (apvts->state.getProperty (mapProp).toString()))
            if (t.first == m) out.push_back (t.second);
        return out;
    }

    void setTargets (int m, const std::vector<Target>& list)
    {
        auto all = parse (apvts->state.getProperty (mapProp).toString());
        all.erase (std::remove_if (all.begin(), all.end(), [m] (const auto& t) { return t.first == m; }), all.end());
        for (const auto& t : list)
            if ((int) std::count_if (all.begin(), all.end(), [m] (const auto& x) { return x.first == m; }) < maxTargets)
                all.push_back ({ m, t });
        write (all);
    }

    // Macro de un parámetro (-1 = ninguna)
    int macroOf (const juce::String& id) const
    {
        for (const auto& t : parse (apvts->state.getProperty (mapProp).toString()))
            if (t.second.id == id) return t.first;
        return -1;
    }

    // Asigna el parámetro a la macro m (lo quita de otra si estaba). From = su valor actual; To = el extremo más lejano,
    // de modo que la macro en 0 no lo mueve y al subir lo lleva hacia ese extremo. Sin sitio (8 destinos): false
    bool assign (int m, const juce::String& id)
    {
        auto* p = apvts->getParameter (id);
        if (p == nullptr || isMacroParam (id))
            return false;
        auto all = parse (apvts->state.getProperty (mapProp).toString());
        all.erase (std::remove_if (all.begin(), all.end(), [&id] (const auto& t) { return t.second.id == id; }), all.end());
        if ((int) std::count_if (all.begin(), all.end(), [m] (const auto& x) { return x.first == m; }) >= maxTargets)
            return false;
        const auto now = p->getValue();
        all.push_back ({ m, { id, now, now < 0.5f ? 1.0f : 0.0f } });
        write (all);
        return true;
    }

    void remove (const juce::String& id)
    {
        auto all = parse (apvts->state.getProperty (mapProp).toString());
        all.erase (std::remove_if (all.begin(), all.end(), [&id] (const auto& t) { return t.second.id == id; }), all.end());
        write (all);
    }

    juce::String name (int m) const
    {
        const auto n = juce::StringArray::fromTokens (apvts->state.getProperty (namesProp).toString(), "|", {})[m].trim();
        return n.isNotEmpty() ? n : "Macro " + juce::String (m + 1);
    }

    void setName (int m, const juce::String& text)
    {
        auto names = juce::StringArray::fromTokens (apvts->state.getProperty (namesProp).toString(), "|", {});
        while (names.size() < numMacros) names.add ({});
        names.set (m, text.removeCharacters ("|\n\r").trim().substring (0, 24));
        bool any = false;
        for (const auto& n : names) any = any || n.isNotEmpty();
        apvts->state.setProperty (namesProp, any ? names.joinIntoString ("|") : juce::String(), nullptr);
    }

private:
    struct Slot { juce::RangedAudioParameter* param = nullptr; float from = 0.0f, to = 1.0f; };
    struct Table { std::array<std::array<Slot, maxTargets>, numMacros> slots {}; std::array<int, numMacros> count {}; };

    // Líneas válidas "m|id|from|to" (m 1..8, from/to 0..1); las demás se ignoran
    static std::vector<std::pair<int, Target>> parse (const juce::String& text)
    {
        std::vector<std::pair<int, Target>> out;
        for (const auto& line : juce::StringArray::fromLines (text))
        {
            const auto f = juce::StringArray::fromTokens (line, "|", {});
            if (f.size() != 4) continue;
            const auto m = f[0].getIntValue() - 1;
            if (m < 0 || m >= numMacros || f[1].isEmpty()) continue;
            out.push_back ({ m, { f[1], juce::jlimit (0.0f, 1.0f, f[2].getFloatValue()), juce::jlimit (0.0f, 1.0f, f[3].getFloatValue()) } });
        }
        return out;
    }

    void write (std::vector<std::pair<int, Target>> all)
    {
        std::stable_sort (all.begin(), all.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        juce::StringArray lines;
        for (const auto& t : all)
            lines.add (juce::String (t.first + 1) + "|" + t.second.id + "|" + juce::String (t.second.from, 4) + "|" + juce::String (t.second.to, 4));
        apvts->state.setProperty (mapProp, lines.joinIntoString ("\n"), nullptr);
        update(); // efecto inmediato (sin esperar al temporizador del procesador)
    }

    APVTS* apvts = nullptr;
    std::array<std::atomic<float>*, numMacros> raw {};
    std::array<float, numMacros> last { -1, -1, -1, -1, -1, -1, -1, -1 };
    juce::int64 hash = 0;
    juce::SpinLock tableLock;
    Table table {};
    bool dirty = false; // protegido por tableLock
};
