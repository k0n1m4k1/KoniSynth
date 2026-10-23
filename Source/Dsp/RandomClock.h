// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación; usa las declaraciones previas de
// KoniSynth.cpp (JUCE). No es una cabecera independiente.
#pragma once

//==============================================================================
// Aleatoriedad reproducible (modo Locked, por defecto) o libre (modo Free, como el hardware).
//  Locked: todo generador aleatorio se resiembra en cada anclaje (arranque del transporte, salto de posición, prepareToPlay =
//   inicio de un render offline) con hash(semilla de instancia, flujo, posición): el mismo proyecto reproducido desde el mismo
//   punto da exactamente la misma señal, y el bounce es lo que se oyó. Las notas del Synth se siembran además por la posición
//   de su note-on (la misma nota en el mismo sitio suena igual aunque se empiece en otro compás). Sin transporte (directo,
//   standalone) la clave es un contador local: cada nota distinta, reproducible dentro de la sesión.
//  Free: semilla de sesión aleatoria y sin anclajes (cada pasada distinta, como un aparato analógico).
// La semilla de instancia vive en el estado del proyecto (no en los presets): dos pistas con el mismo preset no quedan
// correladas; al duplicar una pista se copia y "New random seed" (menú) la cambia.
namespace KoniRandom
{
// SplitMix64 (S. Vigna): mezcla de 64 bits de alta calidad, sin estado
inline juce::uint64 mix (juce::uint64 x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
inline juce::uint64 key (juce::uint64 a, juce::uint64 b) { return mix (a ^ mix (b)); }
inline juce::uint64 key (juce::uint64 a, juce::uint64 b, juce::uint64 c) { return key (key (a, b), c); }
inline double unit (juce::uint64 h) { return (double) (h >> 11) * (2.0 / 9007199254740992.0) - 1.0; } // [-1, 1)

// Ruido de valor 1D: valores uniformes en [-1, 1] en los enteros, interpolados con smoothstep.
// σ = √(0,7429 / 3) = 0,4976 (E[(1-s)² + s²] = 0,7429 para s = 3u² - 2u³ con u uniforme; derivado, no medido)
inline double valueNoise (juce::uint64 seed, double x)
{
    const auto i = std::floor (x);
    const auto u = x - i;
    const auto s = u * u * (3.0 - 2.0 * u);
    const auto n = (juce::int64) i;
    const auto a = unit (key (seed, (juce::uint64) n)), b = unit (key (seed, (juce::uint64) (n + 1)));
    return a + (b - a) * s;
}

// Ruido 1/f (rosa) determinista en el tiempo t (s): 'bands' bandas desde f0, una por octava, con la misma amplitud (misma
// energía por octava = espectro ∝ 1/f), cada una con su semilla y su desfase; normalizado a σ = 1. Semillas y desfases
// precalculados en init
struct Pink
{
    static constexpr int maxBands = 10;
    void init (juce::uint64 seed, double lowestHz, int numBands)
    {
        bands = juce::jmin (numBands, maxBands);
        f0 = lowestHz;
        norm = 1.0 / (0.4976 * std::sqrt ((double) bands));
        for (int k = 0; k < bands; ++k)
        {
            bandSeed[(size_t) k] = key (seed, (juce::uint64) k + 1u);
            offset[(size_t) k] = 1000.0 * (0.5 + 0.5 * unit (mix (bandSeed[(size_t) k])));
        }
    }
    double at (double t) const
    {
        double sum = 0.0, f = f0;
        for (int k = 0; k < bands; ++k, f *= 2.0)
            sum += valueNoise (bandSeed[(size_t) k], t * f + offset[(size_t) k]);
        return sum * norm;
    }

    std::array<juce::uint64, maxBands> bandSeed {};
    std::array<double, maxBands> offset {};
    double f0 = 1.0, norm = 1.0;
    int bands = 0;
};
} // namespace KoniRandom

// Reloj de la línea de tiempo para la aleatoriedad. Lo escribe processBlock al inicio de cada bloque y TimelineSynth tras
// cada tramo renderizado; lo leen las voces. Todo en el hilo de audio
struct RandomClock
{
    double fs = 48000.0;
    juce::uint64 seed = 1;
    bool locked = true, playing = false;
    juce::int64 timeline = 0;   // muestra del inicio del bloque: la del host si reproduce, si no un contador local
    int eventOffset = 0;        // muestras del bloque ya renderizadas por el sintetizador (= posición del evento MIDI en curso)
    juce::uint64 liveCounter = 0;
    // Deriva común de la instancia (motor Synth), evaluada por el procesador al inicio y al final de cada bloque; las voces
    // interpolan (bandas de 0,64 Hz como mucho: el error lineal en un bloque de 10 ms es despreciable)
    double commonA = 0.0, commonB = 0.0, temperatureA = 0.0, temperatureB = 0.0;
    int blockLength = 1;
    double common (int offsetInBlock) const      { return commonA + (commonB - commonA) * offsetInBlock / blockLength; }
    double temperature (int offsetInBlock) const { return temperatureA + (temperatureB - temperatureA) * offsetInBlock / blockLength; }

    double seconds (int offsetInBlock) const { return (double) (timeline + offsetInBlock) / fs; }

    // Clave de una nota del Synth: posición del note-on si el transporte avanza; contador local si no
    juce::uint64 noteKey (int note)
    {
        return playing ? KoniRandom::key (seed, (juce::uint64) (timeline + eventOffset), (juce::uint64) note + 0x4E4F5445ull)
                       : KoniRandom::key (seed ^ 0x4C495645ull, liveCounter++, (juce::uint64) note);
    }
};

// Synthesiser que anota hasta qué muestra del bloque ha renderizado: en handleMidiEvent (justo después) es la posición del
// evento, así startNote conoce el instante exacto de su note-on sin cambiar el reparto del MIDI
struct TimelineSynth : juce::Synthesiser
{
    explicit TimelineSynth (RandomClock& c) : clock (c) {}

    void renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        juce::Synthesiser::renderVoices (buffer, startSample, numSamples);
        clock.eventOffset = startSample + numSamples;
    }
    using juce::Synthesiser::renderVoices;

    // Voice Assign: 0 primera voz libre (JUCE); 1 la misma tecla redispara su voz aunque esté en release (Prophet-5); 2 round
    // robin (Prophet-5 Rev 4): cada nota a la voz libre siguiente. Sin voces libres, el robo de JUCE (la más antigua)
    juce::SynthesiserVoice* findFreeVoice (juce::SynthesiserSound* sound, int channel, int note, bool steal) const override
    {
        const auto mode = assignMode != nullptr ? (int) assignMode->load() : 0;
        if (mode == 0)
            return juce::Synthesiser::findFreeVoice (sound, channel, note, steal);
        const juce::ScopedLock sl (lock);
        const auto n = getNumVoices();
        if (mode == 1)
            for (int i = 0; i < n; ++i)
                if (auto* v = getVoice (i); v->getCurrentlyPlayingNote() == note && v->canPlaySound (sound))
                    return v;
        for (int k = 0; k < n; ++k)
        {
            const auto i = mode == 2 ? (robin + k) % n : k;
            if (auto* v = getVoice (i); ! v->isVoiceActive() && v->canPlaySound (sound))
            {
                if (mode == 2) robin = (i + 1) % n;
                return v;
            }
        }
        return steal ? findVoiceToSteal (sound, channel, note) : nullptr;
    }

    RandomClock& clock;
    const std::atomic<float>* assignMode = nullptr;
    mutable int robin = 0;
};
