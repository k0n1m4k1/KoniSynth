// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE, Params). No es una cabecera independiente.
#pragma once

//==============================================================================
// Matriz de modulación polifónica del motor Synth: 8 ranuras fuente -> destino × cantidad (±100 %), por voz.
// Las fuentes son las que ya calcula la voz (sin avanzar nada dos veces): LFO 1 y 2 (±1), envolventes de filtro y de amplitud
// (0..1), velocidad (0..1), seguimiento de teclado (±1 = ±5 octavas desde C4), rueda (0..1), aftertouch (máximo de poly y
// canal, 0..1), timbre MPE (CC74 del canal de la nota, 0..1), aleatorio por nota (±1, fijo desde el note-on y sembrado:
// Dsp/RandomClock.h) y pitch bend (±1).
// Cada destino suma la contribución de todas las ranuras que lo apuntan; el alcance a cantidad 100 % y fuente 1 está en
// destinationRange. Las ranuras vacías (fuente o destino None, o cantidad 0) no cuestan nada.
namespace ModMatrix
{
constexpr int numSlots = 8;

enum Source { srcNone = 0, srcLfo1, srcLfo2, srcFilterEnv, srcAmpEnv, srcVelocity, srcKeyTrack, srcModWheel, srcAftertouch,
              srcTimbre, srcRandom, srcPitchBend, numSources };

enum Destination { dstNone = 0, dstPitch, dstOsc2Pitch, dstCutoff, dstResonance, dstDrive, dstPulseWidth, dstOsc1Level,
                   dstOsc2Level, dstNoiseLevel, dstVolume, dstPan, dstFmIndex, dstTimbre, dstWtPosition, dstOsc2WtPosition,
                   dstOsc3Pitch, dstOsc3Level, dstOsc3WtPosition, // 1.0.1
                   numDestinations }; // los presets guardan el índice: solo se añade al final

inline const juce::StringArray& sourceNames()
{
    static const juce::StringArray n { "None", "LFO 1", "LFO 2", "Filter Env", "Amp Env", "Velocity", "Key Track", "Mod Wheel",
                                       "Aftertouch", "MPE Timbre", "Random", "Pitch Bend" };
    return n;
}

inline const juce::StringArray& destinationNames()
{
    static const juce::StringArray n { "None", "Pitch", "Osc 2 Pitch", "Cutoff", "Resonance", "Drive", "Pulse Width", "Osc 1 Level",
                                       "Osc 2 Level", "Noise Level", "Volume", "Pan", "FM Index", "Timbre", "WT Position",
                                       "Osc 2 WT Position", "Osc 3 Pitch", "Osc 3 Level", "Osc 3 WT Position" };
    return n;
}

// Alcance de cada destino a cantidad 100 % y fuente 1 (unidad del destino; ver applyTo en la voz)
inline float destinationRange (int d)
{
    switch (d)
    {
        case dstPitch: case dstOsc2Pitch: case dstOsc3Pitch: return 24.0f; // semitonos
        case dstCutoff:                   return 5.0f;  // octavas
        case dstResonance:                return 4.4f;  // k del ladder (rango completo de Reso)
        case dstPulseWidth:               return 0.45f; // ancho de pulso
        case dstFmIndex:                  return 5.0f;  // índice de FM
        default:                          return 1.0f;  // niveles, volumen (relativo), pan, drive, timbre, posición
    }
}

struct Slot { int source = srcNone, destination = dstNone; float amount = 0.0f; };

// Ranuras activas del bloque y qué fuentes hacen falta (para calcular un LFO apagado solo si alguna ranura lo usa)
struct Block
{
    std::array<Slot, numSlots> slots {};
    int count = 0;
    std::array<bool, numSources> uses {};
    std::array<bool, numDestinations> targets {};

    template <typename Params>
    void load (const Params& p)
    {
        count = 0;
        uses.fill (false);
        targets.fill (false);
        for (int i = 0; i < numSlots; ++i)
        {
            Slot s { (int) p.mtxSrc[(size_t) i]->load(), (int) p.mtxDst[(size_t) i]->load(), p.mtxAmt[(size_t) i]->load() };
            if (s.source <= srcNone || s.source >= numSources || s.destination <= dstNone || s.destination >= numDestinations
                || std::abs (s.amount) < 1.0e-5f)
                continue;
            slots[(size_t) count++] = s;
            uses[(size_t) s.source] = true;
            targets[(size_t) s.destination] = true;
        }
    }

    bool active() const { return count > 0; }

    // src: valores de las fuentes; out: suma por destino ya escalada a su unidad
    void compute (const std::array<float, numSources>& src, std::array<float, numDestinations>& out) const
    {
        out.fill (0.0f);
        for (int i = 0; i < count; ++i)
        {
            const auto& s = slots[(size_t) i];
            out[(size_t) s.destination] += s.amount * src[(size_t) s.source] * destinationRange (s.destination);
        }
    }
};
} // namespace ModMatrix
