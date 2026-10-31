// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación, antes de UI/Widgets.h (la perilla y su menú
// la usan); usa ModMatrix (Dsp/ModMatrix.h) y JUCE. No es una cabecera independiente.
#pragma once

//==============================================================================
// Ayudantes de la matriz de modulación (Dsp/ModMatrix.h) para la UI: qué mando representa cada destino, cómo se dibuja su
// alcance, el color de cada fuente y las operaciones de las ranuras (añadir una ruta al soltar una fuente sobre un mando,
// quitarla, listar las de un destino). Las ranuras son los parámetros mtxSrcN / mtxDstN / mtxAmtN (N = 1..8)
namespace MatrixUi
{
// Mando que representa cada destino y cómo se aplica el desplazamiento a su valor real: sumando en su unidad, en octavas
// (corte) o escalado (resonancia: el destino está en k = 4,4·Reso). Tono, volumen y paneo no tienen un mando propio
struct Target { const char* paramId; int kind; float scale; }; // kind 0 suma, 1 octavas
inline Target targetFor (int destination)
{
    switch (destination)
    {
        case ModMatrix::dstOsc2Pitch:  return { "osc2Semi", 0, 1.0f };
        case ModMatrix::dstCutoff:     return { "cutoff", 1, 1.0f };
        case ModMatrix::dstResonance:  return { "resonance", 0, 1.0f / 4.4f };
        case ModMatrix::dstDrive:      return { "filterDrive", 0, 1.0f };
        case ModMatrix::dstPulseWidth: return { "pulseWidth", 0, 1.0f };
        case ModMatrix::dstOsc1Level:  return { "osc1Level", 0, 1.0f };
        case ModMatrix::dstOsc2Level:  return { "osc2Level", 0, 1.0f };
        case ModMatrix::dstNoiseLevel: return { "noiseLevel", 0, 1.0f };
        case ModMatrix::dstFmIndex:    return { "fmIndex", 0, 1.0f };
        case ModMatrix::dstTimbre:     return { "timbre", 0, 1.0f };
        case ModMatrix::dstWtPosition: return { "wtPosition", 0, 1.0f };
        case ModMatrix::dstOsc2WtPosition: return { "osc2WtPosition", 0, 1.0f };
        case ModMatrix::dstOsc3Pitch:  return { "osc3Semi", 0, 1.0f };
        case ModMatrix::dstOsc3Level:  return { "subLevel", 0, 1.0f };
        case ModMatrix::dstOsc3WtPosition: return { "osc3WtPosition", 0, 1.0f };
        default:                       return { nullptr, 0, 1.0f };
    }
}

// Destino que representa un mando (0 = ninguno)
inline int destinationOf (const juce::String& paramId)
{
    for (int d = 1; d < ModMatrix::numDestinations; ++d)
        if (const auto t = targetFor (d); t.paramId != nullptr && paramId == t.paramId)
            return d;
    return 0;
}

inline bool bipolar (int source)
{
    return source == ModMatrix::srcLfo1 || source == ModMatrix::srcLfo2 || source == ModMatrix::srcKeyTrack
        || source == ModMatrix::srcRandom || source == ModMatrix::srcPitchBend;
}

// Fuentes "por nota": su valor queda fijo desde el note-on de cada nota (cada nota el suyo)
inline bool perNote (int source)
{
    return source == ModMatrix::srcVelocity || source == ModMatrix::srcKeyTrack || source == ModMatrix::srcRandom;
}

// Color de cada fuente (fichas, lámparas piloto y faders de la tira de atenuadores). Fijo en todos los skins, como los LED
inline juce::Colour sourceColour (int source)
{
    static const juce::uint32 c[] = { 0xff808080, 0xff42a2c8, 0xffd070c0, 0xffe0a040, 0xff60c080, 0xfff2e6d0, 0xff80a0ff,
                                      0xffc8c060, 0xffe07060, 0xff70d0d0, 0xffb0b0b0, 0xffff9c40 };
    return juce::Colour (c[juce::jlimit (0, (int) std::size (c) - 1, source)]);
}

// Normalizado (0..1) del parámetro desplazado 'offset' unidades del destino desde su valor actual
inline float shifted (juce::RangedAudioParameter& p, const Target& t, float offset)
{
    const auto range = p.getNormalisableRange();
    const auto base = p.convertFrom0to1 (p.getValue());
    const auto v = t.kind == 1 ? base * std::exp2 (offset) : base + offset * t.scale;
    return p.convertTo0to1 (juce::jlimit (range.start, range.end, v));
}

inline float slotValue (APVTS& s, const char* what, int slot)
{
    return s.getRawParameterValue (what + juce::String (slot + 1))->load();
}

inline void setSlot (APVTS& s, const char* what, int slot, float value)
{
    if (auto* p = s.getParameter (what + juce::String (slot + 1)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

// Ranura activa: fuente y destino elegidos (la cantidad puede ser 0: sigue ocupada, como en el diálogo Matrix)
inline bool slotUsed (APVTS& s, int slot)
{
    return (int) slotValue (s, "mtxSrc", slot) > 0 && (int) slotValue (s, "mtxDst", slot) > 0;
}

// Ranuras que apuntan a un destino, en orden
inline std::vector<int> slotsFor (APVTS& s, int destination)
{
    std::vector<int> out;
    for (int i = 0; i < ModMatrix::numSlots; ++i)
        if (slotUsed (s, i) && (int) slotValue (s, "mtxDst", i) == destination)
            out.push_back (i);
    return out;
}

// Ruta nueva fuente -> destino al +25 % en la primera ranura libre. Devuelve la ranura, -1 si ya existe esa ruta y -2 si
// las 8 están ocupadas. Tres gestos: tres pasos de deshacer
enum { routeExists = -1, routesFull = -2 };
inline int addRoute (APVTS& s, int source, int destination)
{
    int free = -1;
    for (int i = 0; i < ModMatrix::numSlots; ++i)
    {
        if (slotUsed (s, i) && (int) slotValue (s, "mtxSrc", i) == source && (int) slotValue (s, "mtxDst", i) == destination)
            return routeExists;
        if (free < 0 && ! slotUsed (s, i))
            free = i;
    }
    if (free < 0)
        return routesFull;
    setSlot (s, "mtxSrc", free, (float) source);
    setSlot (s, "mtxDst", free, (float) destination);
    setSlot (s, "mtxAmt", free, 0.25f);
    return free;
}

// Vacía una ranura (fuente None, destino None, cantidad 0)
inline void removeSlot (APVTS& s, int slot)
{
    setSlot (s, "mtxAmt", slot, 0.0f);
    setSlot (s, "mtxDst", slot, 0.0f);
    setSlot (s, "mtxSrc", slot, 0.0f);
}
} // namespace MatrixUi
