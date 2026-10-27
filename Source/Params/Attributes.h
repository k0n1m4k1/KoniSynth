// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Parámetros
// Texto -> valor de un parámetro: número tras descartar prefijos no numéricos ("I 2.00"); "%" divide entre 100,
// "kHz" multiplica por 1000, "ms" pasa a segundos si el parámetro está en segundos y "Off" es el mínimo (offIsMax: el máximo,
// p. ej. Mod LP abierto del todo). Hace reversible el formato de cada parámetro (valor escrito en el host; la prueba
// param-conversions de clap-validator lo exige).
static juce::AudioParameterFloatAttributes parsing (juce::AudioParameterFloatAttributes a, bool inSeconds = false, bool offIsMax = false)
{
    return a.withValueFromStringFunction ([inSeconds, offIsMax] (const juce::String& text)
    {
        const auto t = text.trim();
        if (t.equalsIgnoreCase ("Off"))
            return offIsMax ? 1.0e30f : -1.0e30f; // el extremo del rango (convertTo0to1 recorta antes de aplicar la curva)
        int i = 0;
        while (i < t.length() && ! (juce::CharacterFunctions::isDigit (t[i]) || t[i] == '-' || t[i] == '+' || t[i] == '.'))
            ++i;
        auto v = t.substring (i).getDoubleValue();
        if (t.contains ("%"))                              v /= 100.0;
        else if (t.containsIgnoreCase ("kHz"))             v *= 1000.0;
        else if (inSeconds && t.containsIgnoreCase ("ms")) v /= 1000.0;
        return (float) v;
    });
}
