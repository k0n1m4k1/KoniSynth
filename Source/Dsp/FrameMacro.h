// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido). No es una cabecera independiente.
#pragma once

//==============================================================================
// Macros por frame: secuencias de valores que el driver de música aplica una vez por frame de vídeo (o tick del driver),
// como los instrumentos de FamiTracker, FamiStudio y Furnace. Sintaxis de Furnace (tildearrow.org/furnace/doc, "instrument
// editor"): valores separados por espacios; "|" = inicio del bucle; "/" = punto de release; en el arpegio, "@n" = nota
// absoluta contada desde C-0 (48 = C-4 = MIDI 60).
// Reproducción (definida aquí; la documentación de Furnace solo dice que la macro se detiene en el release hasta soltar):
//  - nota pulsada: avanza un valor por frame; al llegar al release se queda ahí, o vuelve al bucle si éste está antes;
//  - al soltar: sigue tras el release; al final, vuelve al bucle si está después del release (o si no hay release);
//  - sin bucle: al final se queda en el último valor.
struct FrameMacro
{
    std::vector<int> values;
    std::vector<uint8_t> absolute; // arpegio: 1 = nota absoluta
    int loop = -1, release = -1;

    bool empty() const { return values.empty(); }
    int size() const { return (int) values.size(); }

    // Devuelve el error ("" si no hay). lo/hi: rango de los valores; allowAbsolute: acepta "@n" (arpegio)
    static juce::String parse (const juce::String& text, FrameMacro& out, int lo, int hi, bool allowAbsolute)
    {
        out = {};
        auto tokens = juce::StringArray::fromTokens (text.replace ("|", " | ").replace ("/", " / "), " \t,", "");
        tokens.removeEmptyStrings();
        for (const auto& t : tokens)
        {
            if (t == "|")
            {
                if (out.loop >= 0) return "only one loop point '|'";
                out.loop = out.size();
                continue;
            }
            if (t == "/")
            {
                if (out.release >= 0) return "only one release point '/'";
                if (out.values.empty()) return "'/' needs a value before it";
                out.release = out.size() - 1;
                continue;
            }
            const auto isAbs = t.startsWithChar ('@');
            const auto body = isAbs ? t.substring (1) : t;
            if (isAbs && ! allowAbsolute) return "'@' (absolute note) only in the arpeggio";
            if (! body.containsOnly ("-+0123456789") || body.isEmpty()) return "not a number: " + t;
            const auto v = body.getIntValue();
            if (isAbs ? (v < 0 || v > 115) : (v < lo || v > hi))
                return t + " out of range (" + (isAbs ? juce::String ("@0..@115") : juce::String (lo) + ".." + juce::String (hi)) + ")";
            out.values.push_back (v);
            out.absolute.push_back (isAbs ? 1 : 0);
            if (out.size() > 256) return "more than 256 steps";
        }
        if (out.loop >= out.size()) out.loop = -1; // "|" al final: sin efecto
        return {};
    }
};

// Posición de un canal dentro de una macro
struct MacroCursor
{
    int pos = -1;
    bool released = false;

    void start (const FrameMacro& m) { pos = m.empty() ? -1 : 0; released = false; }
    bool active() const { return pos >= 0; }

    void noteOff (const FrameMacro& m)
    {
        released = true;
        if (pos >= 0 && m.release >= 0 && pos < m.release)
            pos = m.release; // soltar antes de llegar: salta al release (el siguiente frame toca lo de después)
    }

    void advance (const FrameMacro& m)
    {
        if (pos < 0 || m.empty()) return;
        const auto n = m.size();
        if (! released && m.release >= 0 && pos >= m.release)
        {
            pos = m.loop >= 0 && m.loop < m.release ? m.loop : m.release;
            return;
        }
        auto p = pos + 1;
        if (p >= n)
            p = m.loop >= 0 && (m.release < 0 || m.loop > m.release) ? m.loop : n - 1;
        pos = p;
    }

    // ¿Ha terminado? (sin bucle aplicable y en el último valor)
    bool finished (const FrameMacro& m) const
    {
        return pos >= 0 && pos == m.size() - 1 && ! (m.loop >= 0 && (m.release < 0 || m.loop > m.release)) && (released || m.release < 0);
    }

    int value (const FrameMacro& m, int fallback) const { return pos >= 0 && pos < m.size() ? m.values[(size_t) pos] : fallback; }
    bool isAbsolute (const FrameMacro& m) const { return pos >= 0 && pos < m.size() && m.absolute[(size_t) pos] != 0; }
};

// Juego de macros de un motor: volumen, arpegio, pitch y duty/ruido (los cuatro de FamiTracker). Se guarda como texto en una
// propiedad del estado ("vol: 15 12 | 8 / 0; arp: 0 12; pitch: ; duty: 2") y el procesador la publica ya interpretada
struct MacroSet
{
    enum Kind { volume, arpeggio, pitch, duty, numKinds };
    std::array<FrameMacro, numKinds> macro;

    static const char* key (int k) { static const char* keys[numKinds] = { "vol", "arp", "pitch", "duty" }; return keys[k]; }

    // Rangos: volumen 0-15 (escala del chip), arpegio ±96 semitonos, pitch ±127 unidades de periodo por frame, duty 0-3
    static void range (int k, int& lo, int& hi)
    {
        static const int r[numKinds][2] = { { 0, 15 }, { -96, 96 }, { -127, 127 }, { 0, 3 } };
        lo = r[k][0]; hi = r[k][1];
    }

    static juce::String textOf (const juce::String& all, int k)
    {
        for (const auto& part : juce::StringArray::fromTokens (all, ";", ""))
            if (part.upToFirstOccurrenceOf (":", false, false).trim() == key (k))
                return part.fromFirstOccurrenceOf (":", false, false).trim();
        return {};
    }

    static juce::String join (const std::array<juce::String, numKinds>& texts)
    {
        juce::StringArray parts;
        for (int k = 0; k < numKinds; ++k)
            if (texts[(size_t) k].trim().isNotEmpty())
                parts.add (juce::String (key (k)) + ": " + texts[(size_t) k].trim());
        return parts.joinIntoString ("; ");
    }

    // Interpreta todo; error del primero que falle (con su nombre), "" si todo bien
    static juce::String parse (const juce::String& all, MacroSet& out)
    {
        for (int k = 0; k < numKinds; ++k)
        {
            int lo, hi;
            range (k, lo, hi);
            if (const auto e = FrameMacro::parse (textOf (all, k), out.macro[(size_t) k], lo, hi, k == arpeggio); e.isNotEmpty())
                return juce::String (key (k)) + ": " + e;
        }
        return {};
    }

    bool empty() const { return std::all_of (macro.begin(), macro.end(), [] (const FrameMacro& m) { return m.empty(); }); }
};

using MacroSlot = DataSlot<MacroSet>;
