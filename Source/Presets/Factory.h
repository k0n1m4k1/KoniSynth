// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

//==============================================================================
// Presets de fábrica, en unidades reales (s, Hz, ms, índice de choice, 0/1 de bool).
// Los parámetros no listados toman su valor por defecto.
struct PresetValue
{
    juce::String id;
    float value; // agregado (no std::pair): los literales enteros convierten sin warning C4244
};

struct FactoryPreset
{
    juce::String name, category;
    std::vector<PresetValue> values;
    std::vector<std::pair<juce::String, juce::String>> properties {}; // propiedades de estado de texto (macros por frame)
};

// Categorías de presets: síntesis de las de Surge XT (carpetas de patches_factory en su repositorio: Basses, Brass,
// Chords, FX, Keys, Leads, Pads, Percussion, Plucks, Polysynths, Sequences, Vocoder, Winds...) y de Arturia Analog Lab
// (Bass, Keys, Lead, Pad, Piano, Electric Piano, Organ, Strings, Brass, Drums, Sequence, Vocal, Sound Effects).
// "Machines" agrupa los motores-instrumento (Theremin, Casio VL-1); "8-Bit" (chips de videoconsola y efectos de juego),
// "Nature" (animales y fenómenos atmosféricos) y "Vocal" (vocales y coros) son categorías propias de KoniSynth.
static const juce::StringArray& presetCategories()
{
    static const juce::StringArray list { "Init", "Bass", "Lead", "Pad", "Keys", "Brass", "Strings", "Pluck", "Sequence",
                                          "Drums", "FX", "8-Bit", "Nature", "Vocal", "Vocoder", "Machines" };
    return list;
}

static const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list {
        { "Init", "Init", { { "osc3Wave", 0 } } }, // Osc 3 en seno; los presets sin osc3Wave cargan la cuadrada (el Sub de siempre)
        { "Soft Pad", "Pad",   { { "wave", 1 }, { "attack", 0.8f }, { "decay", 0.5f }, { "sustain", 0.8f }, { "release", 2.0f }, { "drift", 0.35f },
                          { "filterMode", 1 }, { "cutoff", 1800.0f }, { "resonance", 0.15f }, { "filterEnv", 0.2f },
                          { "filterAttack", 1.0f }, { "filterDecay", 2.0f },
                          { "lfoOn", 1 }, { "lfoTarget", 1 }, { "lfoRate", 0.3f }, { "lfoDepth", 0.2f },
                          { "ambientOn", 1 }, { "ambientSize", 0.85f }, { "ambientMix", 0.45f }, { "gain", 0.6f } } },
        { "Retro Lead", "Lead", { { "wave", 3 }, { "attack", 0.005f }, { "decay", 0.15f }, { "sustain", 0.7f }, { "release", 0.15f },
                          { "cutoff", 2500.0f }, { "resonance", 0.3f }, { "filterEnv", 0.3f }, { "filterDecay", 0.4f },
                          { "tubeOn", 1 }, { "tubeDrive", 10.0f }, { "tubeBias", 0.3f },
                          { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.5f }, { "lfoDepth", 0.15f },
                          { "echoOn", 1 }, { "echoTime", 300.0f }, { "echoFeedback", 0.35f }, { "echoMix", 0.25f } } },
        { "Saw Bass", "Bass",   { { "wave", 2 }, { "attack", 0.002f }, { "decay", 0.25f }, { "sustain", 0.5f }, { "release", 0.08f },
                          { "cutoff", 250.0f }, { "resonance", 0.45f }, { "filterDrive", 0.4f }, { "filterEnv", 0.55f },
                          { "filterAttack", 0.001f }, { "filterDecay", 0.25f }, { "filterKey", 0.5f },
                          { "tubeOn", 1 }, { "tubeDrive", 6.0f }, { "gain", 0.6f } } },
        { "Echo Pluck", "Pluck", { { "wave", 1 }, { "attack", 0.001f }, { "decay", 0.3f }, { "sustain", 0.0f }, { "release", 0.3f },
                          { "cutoff", 700.0f }, { "resonance", 0.3f }, { "filterEnv", 0.6f }, { "filterDecay", 0.25f },
                          { "echoOn", 1 }, { "echoTime", 375.0f }, { "echoFeedback", 0.5f }, { "echoMix", 0.4f },
                          { "echoTone", 3000.0f }, { "echoWow", 0.4f },
                          { "ambientOn", 1 }, { "ambientSize", 0.5f }, { "ambientMix", 0.2f } } },
        { "Glass Bell", "Keys", { { "wave", 0 }, { "attack", 0.001f }, { "decay", 1.5f }, { "sustain", 0.0f }, { "release", 2.5f },
                          { "echoOn", 1 }, { "echoTime", 500.0f }, { "echoFeedback", 0.45f }, { "echoMix", 0.3f },
                          { "echoTone", 6000.0f }, { "echoWow", 0.2f },
                          { "ambientOn", 1 }, { "ambientSize", 0.9f }, { "ambientMix", 0.5f } } },
        { "Wobble", "Bass",     { { "wave", 2 }, { "attack", 0.01f }, { "decay", 0.2f }, { "sustain", 0.9f }, { "release", 0.2f },
                          { "cutoff", 600.0f }, { "resonance", 0.6f }, { "tubeOn", 1 }, { "tubeDrive", 8.0f },
                          { "lfoOn", 1 }, { "lfoTarget", 2 }, { "lfoRate", 3.0f }, { "lfoDepth", 0.7f } } },
        { "Acid Squelch", "Bass", { { "wave", 2 }, { "attack", 0.002f }, { "decay", 0.2f }, { "sustain", 0.6f }, { "release", 0.1f },
                          { "cutoff", 300.0f }, { "resonance", 0.85f }, { "filterDrive", 0.5f }, { "filterEnv", 0.8f },
                          { "filterAttack", 0.001f }, { "filterDecay", 0.2f }, { "filterKey", 0.3f },
                          { "tubeOn", 1 }, { "tubeDrive", 12.0f }, { "tubeBias", 0.4f },
                          { "echoOn", 1 }, { "echoTime", 375.0f }, { "echoFeedback", 0.3f }, { "echoMix", 0.2f } } },
        { "Space Tape", "Pad", { { "wave", 1 }, { "attack", 0.3f }, { "decay", 0.5f }, { "sustain", 0.7f }, { "release", 1.5f }, { "drift", 0.5f },
                          { "cutoff", 1500.0f }, { "resonance", 0.25f },
                          { "echoOn", 1 }, { "echoTime", 450.0f }, { "echoFeedback", 0.85f }, { "echoMix", 0.5f },
                          { "echoTone", 2200.0f }, { "echoWow", 0.8f },
                          { "ambientOn", 1 }, { "ambientSize", 0.8f }, { "ambientMix", 0.35f } } },
        { "Etheric Theremin", "Machines", { { "engine", 1 }, { "thereminRange", 3.0f }, { "thereminGlide", 80.0f }, { "thereminTone", 0.2f },
                          { "thereminModel", 0 }, { "thereminField", 10.0f }, { "thereminLock", 10.0f }, { "thereminZero", 60.0f },
                          { "drift", 0.3f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.8f }, { "lfoDepth", 0.1f },
                          { "echoOn", 1 }, { "echoTime", 420.0f }, { "echoFeedback", 0.35f }, { "echoMix", 0.25f },
                          { "echoTone", 3500.0f }, { "echoWow", 0.3f },
                          { "ambientOn", 1 }, { "ambientSize", 0.75f }, { "ambientMix", 0.35f } } },
        // Campo muy no lineal, enganche ancho (gruñido) y batido cero a 48 cm: a la izquierda el tono vuelve a subir
        { "Sci-Fi Saucer", "Machines", { { "engine", 1 }, { "thereminRange", 4.0f }, { "thereminGlide", 300.0f }, { "thereminTone", 0.6f },
                          { "thereminModel", 0 }, { "thereminField", 4.0f }, { "thereminLock", 35.0f }, { "thereminZero", 48.0f },
                          { "drift", 0.6f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 7.0f }, { "lfoDepth", 0.35f },
                          { "tubeOn", 1 }, { "tubeDrive", 10.0f }, { "tubeBias", 0.4f },
                          { "echoOn", 1 }, { "echoTime", 330.0f }, { "echoFeedback", 0.7f }, { "echoMix", 0.4f },
                          { "echoTone", 2500.0f }, { "echoWow", 0.6f },
                          { "ambientOn", 1 }, { "ambientSize", 0.85f }, { "ambientMix", 0.4f } } },
        // Electro-Theremin de Tanner ("Good Vibrations"): seno simple, deslizador con portamento, sin vibrato de mano
        // (Wikipedia "Electro-Theremin"; NPR/WWNO 2013). Modelo Ideal = afinación estable, como el original
        { "Electro-Theremin", "Machines", { { "engine", 1 }, { "thereminModel", 1 }, { "thereminTone", 0.0f }, { "thereminGlide", 150.0f } } },
        // Metales tipo CS-80: dos capas por voz -> unísono de 2 ligeramente desafinado; filtro que se abre despacio
        { "Neon Brass", "Brass", { { "wave", 2 }, { "attack", 0.12f }, { "decay", 0.6f }, { "sustain", 0.75f }, { "release", 0.9f }, { "drift", 0.3f },
                          { "unisonVoices", 2 }, { "unisonDetune", 7.0f }, { "unisonSpread", 0.6f },
                          { "cutoff", 900.0f }, { "resonance", 0.2f }, { "filterEnv", 0.35f }, { "filterAttack", 0.25f }, { "filterDecay", 1.2f },
                          { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.0f }, { "lfoDepth", 0.06f },
                          { "ambientOn", 1 }, { "ambientSize", 0.92f }, { "ambientMix", 0.45f } } },
        // Acordes con chorus II, el recomendado para recrear los acordes del Juno-106 en Discovery (reverbmachine.com)
        { "French Chords", "Keys", { { "wave", 2 }, { "attack", 0.005f }, { "decay", 0.4f }, { "sustain", 0.7f }, { "release", 0.3f },
                          { "cutoff", 2200.0f }, { "resonance", 0.15f }, { "filterEnv", 0.2f }, { "filterDecay", 0.3f },
                          { "chorusOn", 1 }, { "chorusMode", 1 }, { "chorusMix", 0.5f },
                          { "tubeOn", 1 }, { "tubeDrive", 6.0f } } },
        { "String Machine", "Strings", { { "wave", 2 }, { "attack", 0.25f }, { "decay", 0.5f }, { "sustain", 0.85f }, { "release", 1.2f },
                          { "unisonVoices", 3 }, { "unisonDetune", 6.0f }, { "unisonSpread", 0.4f },
                          { "filterMode", 1 }, { "cutoff", 4000.0f },
                          { "chorusOn", 1 }, { "chorusMode", 3 }, { "chorusMix", 0.7f },
                          { "ambientOn", 1 }, { "ambientSize", 0.8f }, { "ambientMix", 0.3f } } },
        // 7 sierras con desafinación en el rango 40–90 ct de extremo a extremo citado para el JP-8000 (foro KVR): ±25 ct
        { "Supersaw Lab", "Lead", { { "wave", 2 }, { "attack", 0.01f }, { "decay", 0.3f }, { "sustain", 0.9f }, { "release", 0.5f },
                          { "unisonVoices", 7 }, { "unisonDetune", 25.0f }, { "unisonSpread", 1.0f },
                          { "cutoff", 6000.0f }, { "chorusOn", 1 }, { "chorusMode", 0 }, { "chorusMix", 0.3f },
                          { "ambientOn", 1 }, { "ambientSize", 0.7f }, { "ambientMix", 0.25f }, { "gain", 0.45f } } },
        { "Cassette Memories", "Pad", { { "wave", 1 }, { "attack", 0.4f }, { "decay", 0.6f }, { "sustain", 0.8f }, { "release", 1.5f }, { "drift", 0.3f },
                          { "unisonVoices", 2 }, { "unisonDetune", 6.0f }, { "cutoff", 2500.0f },
                          { "chorusOn", 1 }, { "chorusMode", 1 }, { "chorusMix", 0.4f },
                          { "mediaOn", 1 }, { "mediaType", 0 }, { "mediaAge", 0.5f }, { "mediaNoise", 0.55f },
                          { "mediaWow", 0.6f }, { "mediaDrive", 0.4f } } },
        { "Dusty Vinyl", "Keys", { { "wave", 0 }, { "attack", 0.001f }, { "decay", 1.5f }, { "sustain", 0.0f }, { "release", 2.5f },
                          { "ambientOn", 1 }, { "ambientSize", 0.7f }, { "ambientMix", 0.3f },
                          { "mediaOn", 1 }, { "mediaType", 1 }, { "mediaAge", 0.6f }, { "mediaNoise", 0.6f }, { "mediaWow", 0.5f } } },
        { "Studio Reel", "Bass", { { "wave", 2 }, { "attack", 0.002f }, { "decay", 0.25f }, { "sustain", 0.5f }, { "release", 0.08f },
                          { "cutoff", 300.0f }, { "resonance", 0.4f }, { "filterEnv", 0.5f }, { "filterDecay", 0.25f },
                          { "mediaOn", 1 }, { "mediaType", 2 }, { "mediaAge", 0.1f }, { "mediaNoise", 0.4f },
                          { "mediaWow", 0.5f }, { "mediaDrive", 0.6f }, { "gain", 0.6f } } },
        { "Worn Reel 9.5", "Pad", { { "wave", 1 }, { "attack", 0.3f }, { "decay", 0.5f }, { "sustain", 0.7f }, { "release", 1.5f },
                          { "cutoff", 1500.0f }, { "echoOn", 1 }, { "echoTime", 450.0f }, { "echoFeedback", 0.6f }, { "echoMix", 0.4f },
                          { "mediaOn", 1 }, { "mediaType", 4 }, { "mediaAge", 0.7f }, { "mediaNoise", 0.6f },
                          { "mediaWow", 0.8f }, { "mediaDrive", 0.5f } } },
        // Autooscilación del ladder con Key 100 %: un silbido sinusoidal 2 octavas sobre la nota (cutoff C6 con referencia C4)
        { "Ladder Whistle", "Lead", { { "wave", 0 }, { "attack", 0.15f }, { "decay", 0.3f }, { "sustain", 0.9f }, { "release", 0.6f }, { "drift", 0.3f },
                          { "cutoff", 1046.5f }, { "resonance", 1.0f }, { "filterKey", 1.0f },
                          { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.5f }, { "lfoDepth", 0.12f },
                          { "ambientOn", 1 }, { "ambientSize", 0.7f }, { "ambientMix", 0.3f } } },
        // Trio, "Da Da Da" (1982): ritmo Rock-1 + voz Piano del VL-1 (Wikipedia "Casio VL-1"; MusicTech "Rewind").
        // Piano = código 00453200; tempo 0 = 130,9 BPM (la pista mide 132,6 BPM según cs.uwaterloo.ca, Hott Tracks 048)
        { "Da Da Da (VL-1)", "Machines", { { "engine", 2 }, { "vl1Sound", 0 }, { "vl1Attack", 0 }, { "vl1Decay", 4 }, { "vl1SustainLevel", 5 },
                          { "vl1SustainTime", 3 }, { "vl1Release", 2 }, { "vl1Vibrato", 0 }, { "vl1Tremolo", 0 },
                          { "vl1Rhythm", 5 }, { "vl1Tempo", 0 } } },
        // Códigos de fábrica del VL-1 (VL1Defs.h): Fantasy 10099630, Violin 23099130
        { "VL-1 Fantasy", "Machines", { { "engine", 2 }, { "vl1Sound", 1 }, { "vl1Attack", 0 }, { "vl1Decay", 0 }, { "vl1SustainLevel", 9 },
                          { "vl1SustainTime", 9 }, { "vl1Release", 6 }, { "vl1Vibrato", 3 }, { "vl1Tremolo", 0 } } },
        { "VL-1 Violin Bossa", "Machines", { { "engine", 2 }, { "vl1Sound", 2 }, { "vl1Attack", 3 }, { "vl1Decay", 0 }, { "vl1SustainLevel", 9 },
                          { "vl1SustainTime", 9 }, { "vl1Release", 1 }, { "vl1Vibrato", 3 }, { "vl1Tremolo", 0 },
                          { "vl1Rhythm", 7 }, { "vl1Tempo", 2 } } },
        // Sonidos de ROM con su código de fábrica (VL1Defs.h del emulador linuxmao-org; formato de 8 cifras: muzines.co.uk
        // "Casiotone VL1"). Piano sin ritmo = el que carga el selector de motor
        { "VL-1 Piano", "Machines", { { "engine", 2 }, { "vl1Sound", 0 }, { "vl1Attack", 0 }, { "vl1Decay", 4 }, { "vl1SustainLevel", 5 },
                          { "vl1SustainTime", 3 }, { "vl1Release", 2 }, { "vl1Vibrato", 0 }, { "vl1Tremolo", 0 } } },
        { "VL-1 Flute", "Machines", { { "engine", 2 }, { "vl1Sound", 3 }, { "vl1Attack", 3 }, { "vl1Decay", 0 }, { "vl1SustainLevel", 9 },
                          { "vl1SustainTime", 9 }, { "vl1Release", 1 }, { "vl1Vibrato", 3 }, { "vl1Tremolo", 0 } } },
        { "VL-1 Guitar", "Machines", { { "engine", 2 }, { "vl1Sound", 4 }, { "vl1Attack", 1 }, { "vl1Decay", 0 }, { "vl1SustainLevel", 7 },
                          { "vl1SustainTime", 1 }, { "vl1Release", 2 }, { "vl1Vibrato", 0 }, { "vl1Tremolo", 0 } } },
        // Código de ejemplo del manual: 90099914 (Wikipedia "Casio VL-1")
        { "VL-1 Manual ADSR", "Machines", { { "engine", 2 }, { "vl1Sound", 9 }, { "vl1Attack", 0 }, { "vl1Decay", 0 }, { "vl1SustainLevel", 9 },
                          { "vl1SustainTime", 9 }, { "vl1Release", 9 }, { "vl1Vibrato", 1 }, { "vl1Tremolo", 4 } } },
        // Stylophone 1968 (rango completo: un preset por sonido, no por registro; el vibrato de ≈7 Hz es un interruptor): altavoz y salida de línea
        { "Stylophone", "Machines", { { "engine", 3 }, { "styloModel", 0 } } },
        { "Stylophone Line Out", "Machines", { { "engine", 3 }, { "styloModel", 0 }, { "styloOutput", 1 }, { "styloClick", 0.15f } } },
        // Gen X-2 (experimental): sonidos que el manual oficial describe ("punchy bass", "screaming leads", "bleeps and bloops")
        { "Gen X-2 Sub Bass", "Machines", { { "engine", 3 }, { "styloModel", 1 }, { "styloOutput", 1 }, { "genxSub1", 1 }, { "genxSub2", 1 },
                          { "genxCutoff", 600.0f }, { "genxRes", 0.3f }, { "genxDecay", 0.6f } } },
        { "Gen X-2 PWM Lead", "Machines", { { "engine", 3 }, { "styloModel", 1 }, { "styloOutput", 1 }, { "genxPwm", 1 }, { "genxLfoRate", 3.0f },
                          { "genxCutoff", 4000.0f }, { "genxRes", 0.45f }, { "genxDelay", 1 }, { "genxTime", 280.0f }, { "genxRepeats", 0.45f },
                          { "genxLevel", 0.35f } } },
        { "Gen X-2 Bleeps", "Machines", { { "engine", 3 }, { "styloModel", 1 }, { "styloOutput", 1 }, { "genxPitch", 12.0f },
                          { "genxDecay", 0.15f }, { "genxCutoff", 6000.0f }, { "genxRes", 0.6f }, { "genxLfoWave", 1 }, { "genxLfoRate", 8.0f },
                          { "genxDelay", 1 }, { "genxTime", 120.0f }, { "genxRepeats", 0.5f } } },
        // Omnichord: ritmo Rock 1 con bajo (el que suena en "Clint Eastwood" según MusicTech), y arpa sola con mucho sustain
        { "Omnichord Rock 1", "Machines", { { "engine", 4 }, { "omniRhythm", 1 }, { "omniTempo", 84.0f }, { "omniSustain", 0.6f } } },
        { "Omnichord Strum", "Machines", { { "engine", 4 }, { "omniSustain", 0.9f }, { "omniChordLevel", 0.2f },
                          { "ambientOn", 1 }, { "ambientMix", 0.3f } } },
        { "Omnichord Latin", "Machines", { { "engine", 4 }, { "omniRhythm", 6 }, { "omniTempo", 112.0f } } },
        // Ritmos de fábrica del OM-84 (Perfect Circuit, matrixsynth): Disco y Country
        { "Omnichord Disco", "Machines", { { "engine", 4 }, { "omniRhythm", 9 } } },
        { "Omnichord Country", "Machines", { { "engine", 4 }, { "omniRhythm", 8 } } },
        // Omnichord grabado y ralentizado, "deep jukebox sound" (Eno/Lanois, "Deep Blue Day": Wikipedia "Omnichord"; una fuente)
        { "Omnichord Deep Jukebox", "Machines", { { "engine", 4 }, { "omniHarpOctave", -1 }, { "omniSustain", 1.0f },
                          { "ambientOn", 1 }, { "ambientSize", 0.8f }, { "ambientMix", 0.4f } } },
        // Cajas de ritmos (kits de los service notes de Roland)
        { "808 Electro", "Drums", { { "engine", 5 }, { "drumKit", 0 }, { "drumPattern", 5 }, { "drumTempo", 112.0f } } },
        { "808 Long Boom", "Drums", { { "engine", 5 }, { "drumKit", 0 }, { "drumDecay", 0.6f }, { "drumTone", 0.3f },
                          { "drumPattern", 6 }, { "drumTempo", 90.0f }, { "drumSwing", 0.12f } } },
        { "808 Synth Pop", "Drums", { { "engine", 5 }, { "drumKit", 0 }, { "drumPattern", 7 }, { "drumTempo", 118.0f } } },
        { "CR-78 Rock", "Drums", { { "engine", 5 }, { "drumKit", 1 }, { "drumPattern", 1 }, { "drumTempo", 120.0f } } },
        // Mini Pops 7: Oxygène IV = Slow Rock + Rock sujetos con cinta; Oxygène VI = Rumba + Bossa Nova (entrevista a
        // Jarre en newagemusic.guide). Solo se combinan ritmos de la misma fila (manual de Bucket Pops) y en la de Slow Rock
        // están Rock 2 y Rock 4: se usa Rock 2 (deducción). Tempos estimados; patrones propios, no los del hardware
        { "Oxygene IV Rhythm", "Drums", { { "engine", 5 }, { "drumKit", 2 }, { "drumPattern", 19 }, { "drumPattern2", 2 }, { "drumTempo", 96.0f } } },
        { "Oxygene VI Rhythm", "Drums", { { "engine", 5 }, { "drumKit", 2 }, { "drumPattern", 22 }, { "drumPattern2", 8 }, { "drumTempo", 120.0f } } },
        { "CR-78 Bossa", "Drums", { { "engine", 5 }, { "drumKit", 1 }, { "drumPattern", 8 }, { "drumTempo", 126.0f } } },
        // FM 6-Op. E.Piano: algoritmo 5 con el modulador del "tine" a ratio 14 (Paul Colbert, One Two Testing, nov. 1983)
        // y niveles/decays de DecentSampler fm6op (portadoras 0,90/0,70/0,55; moduladores 0,55/0,40/0,30, decay 0,25-0,40 s, sustain 0).
        // Conversión a escala DX7 propia: no es el patch de ROM
        { "FM E.Piano", "Keys", { { "engine", 6 }, { "fmAlgorithm", 5 },
                          { "fm1Coarse", 1 }, { "fm1Level", 99 }, { "fm1Vel", 2 }, { "fm1RateScale", 3 }, { "fm1R1", 96 }, { "fm1R2", 25 }, { "fm1L2", 75 }, { "fm1R3", 25 }, { "fm1L3", 0 }, { "fm1R4", 67 },
                          { "fm2Coarse", 14 }, { "fm2Level", 62 }, { "fm2Vel", 6 }, { "fm2RateScale", 3 }, { "fm2R1", 95 }, { "fm2R2", 50 }, { "fm2L2", 0 }, { "fm2R3", 35 }, { "fm2L3", 0 }, { "fm2R4", 78 },
                          { "fm3Coarse", 1 }, { "fm3Level", 92 }, { "fm3Vel", 2 }, { "fm3RateScale", 3 }, { "fm3R1", 96 }, { "fm3R2", 25 }, { "fm3L2", 75 }, { "fm3R3", 25 }, { "fm3L3", 0 }, { "fm3R4", 67 },
                          { "fm4Coarse", 1 }, { "fm4Level", 70 }, { "fm4Vel", 6 }, { "fm4RateScale", 3 }, { "fm4R1", 95 }, { "fm4R2", 45 }, { "fm4L2", 0 }, { "fm4R3", 35 }, { "fm4L3", 0 }, { "fm4R4", 78 },
                          { "fm5Coarse", 1 }, { "fm5Level", 86 }, { "fm5Detune", 3 }, { "fm5Vel", 2 }, { "fm5RateScale", 3 }, { "fm5R1", 96 }, { "fm5R2", 25 }, { "fm5L2", 75 }, { "fm5R3", 25 }, { "fm5L3", 0 }, { "fm5R4", 67 },
                          { "fm6Coarse", 1 }, { "fm6Level", 64 }, { "fm6Vel", 6 }, { "fm6RateScale", 3 }, { "fm6R1", 95 }, { "fm6R2", 40 }, { "fm6L2", 0 }, { "fm6R3", 35 }, { "fm6L3", 0 }, { "fm6R4", 78 } } },
        // Brass: algoritmo 1, OP1 y OP2 a ratio 1 con las envolventes en unidades DX7 de DecentSampler (OP1 R 99/86/0/60 L 99/94/94/0;
        // OP2 nivel 0,55, R 99/70/0/55 L 99/60/60/0); Chowning: trompeta 1:1 con índice que cae de 5 a 0 (Columbia CMC)
        { "FM Brass", "Brass", { { "engine", 6 }, { "fmAlgorithm", 1 },
                          { "fm1Coarse", 1 }, { "fm1Level", 99 }, { "fm1Vel", 1 }, { "fm1R2", 86 }, { "fm1L2", 94 }, { "fm1R3", 0 }, { "fm1L3", 94 }, { "fm1R4", 60 },
                          { "fm2Coarse", 1 }, { "fm2Level", 80 }, { "fm2Vel", 3 }, { "fm2R2", 70 }, { "fm2L2", 60 }, { "fm2R3", 0 }, { "fm2L3", 60 }, { "fm2R4", 55 },
                          { "fmLfoSpeed", 34 }, { "fmLfoDelay", 40 }, { "fmLfoPmd", 6 } } },
        // Campanas: pares 1:3,5 (DecentSampler: modulador con decay de 0,10-0,15 s, portadora de 2-4 s); R 43 ≈ 3 s y R 70 ≈ 0,17 s
        // para caer 90 dB (tabla calculada de env.cc)
        { "FM Bells", "Keys", { { "engine", 6 }, { "fmAlgorithm", 5 },
                          { "fm1Coarse", 1 }, { "fm1Level", 99 }, { "fm1Vel", 2 }, { "fm1RateScale", 2 }, { "fm1R2", 43 }, { "fm1L2", 0 }, { "fm1R3", 43 }, { "fm1L3", 0 }, { "fm1R4", 43 },
                          { "fm2Coarse", 3 }, { "fm2Level", 78 }, { "fm2Fine", 50 }, { "fm2Vel", 5 }, { "fm2RateScale", 2 }, { "fm2R2", 70 }, { "fm2L2", 0 }, { "fm2R3", 70 }, { "fm2L3", 0 }, { "fm2R4", 70 },
                          { "fm3Coarse", 1 }, { "fm3Level", 90 }, { "fm3Detune", 3 }, { "fm3RateScale", 2 }, { "fm3R2", 40 }, { "fm3L2", 0 }, { "fm3R3", 40 }, { "fm3L3", 0 }, { "fm3R4", 40 },
                          { "fm4Coarse", 3 }, { "fm4Level", 70 }, { "fm4Fine", 50 }, { "fm4Detune", -3 }, { "fm4Vel", 5 }, { "fm4RateScale", 2 }, { "fm4R2", 66 }, { "fm4L2", 0 }, { "fm4R3", 66 }, { "fm4L3", 0 }, { "fm4R4", 66 } } },
        // Campana tubular: ratio 3,18 (Paul Colbert, 1983)
        { "FM Tubular Bells", "Keys", { { "engine", 6 }, { "fmAlgorithm", 5 },
                          { "fm1Coarse", 1 }, { "fm1Level", 99 }, { "fm1Vel", 2 }, { "fm1RateScale", 2 }, { "fm1R2", 38 }, { "fm1L2", 0 }, { "fm1R3", 38 }, { "fm1L3", 0 }, { "fm1R4", 40 },
                          { "fm2Coarse", 3 }, { "fm2Level", 82 }, { "fm2Fine", 18 }, { "fm2Vel", 4 }, { "fm2RateScale", 2 }, { "fm2R2", 55 }, { "fm2L2", 30 }, { "fm2R3", 55 }, { "fm2L3", 0 }, { "fm2R4", 55 },
                          { "fm3Coarse", 1 }, { "fm3Level", 80 }, { "fm3Detune", 2 }, { "fm3RateScale", 2 }, { "fm3R2", 36 }, { "fm3L2", 0 }, { "fm3R3", 36 }, { "fm3L3", 0 }, { "fm3R4", 40 },
                          { "fm4Coarse", 3 }, { "fm4Level", 70 }, { "fm4Fine", 18 }, { "fm4Detune", -2 }, { "fm4Vel", 4 }, { "fm4RateScale", 2 }, { "fm4R2", 50 }, { "fm4L2", 20 }, { "fm4R3", 50 }, { "fm4L3", 0 }, { "fm4R4", 50 } } },
        // Órgano: algoritmo 32 (6 portadoras) a ratios 1, 2, 3, 4, 5 y 8 con envolvente cuadrada (Keyboard, 1985)
        { "FM Organ", "Keys", { { "engine", 6 }, { "fmAlgorithm", 32 },
                          { "fm1Coarse", 1 }, { "fm1Level", 99 }, { "fm1R4", 80 },
                          { "fm2Coarse", 2 }, { "fm2Level", 90 }, { "fm2R4", 80 },
                          { "fm3Coarse", 3 }, { "fm3Level", 84 }, { "fm3R4", 80 },
                          { "fm4Coarse", 4 }, { "fm4Level", 80 }, { "fm4R4", 80 },
                          { "fm5Coarse", 5 }, { "fm5Level", 74 }, { "fm5R4", 80 },
                          { "fm6Coarse", 8 }, { "fm6Level", 72 }, { "fm6R4", 80 },
                          { "fmLfoSpeed", 38 }, { "fmLfoPmd", 4 } } },
        // Bajo: algoritmo 1, OP2 modula OP1 con ratio 2 ("techno", MusicTech) y caída rápida del modulador; cifras de EG propias
        { "FM Bass", "Bass", { { "engine", 6 }, { "fmAlgorithm", 1 },
                          { "fm1Coarse", 0 }, { "fm1Level", 99 }, { "fm1Vel", 2 }, { "fm1R2", 60 }, { "fm1L2", 85 }, { "fm1R3", 30 }, { "fm1L3", 70 }, { "fm1R4", 75 },
                          { "fm2Coarse", 1 }, { "fm2Level", 84 }, { "fm2Vel", 5 }, { "fm2R2", 72 }, { "fm2L2", 40 }, { "fm2R3", 40 }, { "fm2L3", 0 }, { "fm2R4", 75 } } },
        // Motor Chip: el sonido sale del modelo del hardware; los ajustes son los habituales en la música de juegos
        { "NES Square Lead", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipDuty", 1 }, { "chipVolume", 12 },
                          { "chipVibDepth", 25.0f }, { "chipVibRate", 6.0f }, { "chipVibDelay", 12 }, { "chipRelease", 6 } } },
        // Mismo lead con reloj PAL (el selector de modelo carga este preset con NES (PAL))
        { "NES PAL Square Lead", "8-Bit", { { "engine", 7 }, { "chipModel", 1 }, { "chipDuty", 1 }, { "chipVolume", 12 },
                          { "chipVibDepth", 25.0f }, { "chipVibRate", 6.0f }, { "chipVibDelay", 12 }, { "chipRelease", 6 } } },
        // Juego NTSC sin adaptar en una NES PAL (AtariAge "Examining NES PAL conversions"; NESdev "Detect TV system")
        { "NES PAL Unadapted Lead", "8-Bit", { { "engine", 7 }, { "chipModel", 5 }, { "chipDuty", 1 }, { "chipVolume", 12 },
                          { "chipVibDepth", 25.0f }, { "chipVibRate", 6.0f }, { "chipVibDelay", 12 }, { "chipRelease", 6 } } },
        { "NES Thin Pulse", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipDuty", 0 }, { "chipVolume", 10 } } },
        { "NES Triangle Bass", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVoice", 1 } } },
        { "NES Arp Chords", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipDuty", 2 }, { "chipArp", 1 }, { "chipArpSpeed", 1 } } },
        { "NES Decay Pluck", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipDuty", 1 }, { "chipEnvelope", 1 }, { "chipEnvRate", 3 } } },
        { "NES Laser Sweep", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipDuty", 2 }, { "chipSweep", 3 }, { "chipSweepRate", 1 } } },
        { "NES Noise Kit", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVoice", 2 }, { "chipEnvelope", 1 }, { "chipEnvRate", 2 } } },
        // Ruido en modo corto (93 pasos, metálico: NESdev "2A03", vgmrips "NES APU")
        { "NES Noise Hi-Hat", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVoice", 2 }, { "chipNoiseMode", 1 }, { "chipEnvelope", 1 }, { "chipEnvRate", 1 } } },
        { "NES Band (MIDI 1-4)", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVoice", 4 }, { "chipDuty", 1 }, { "chipRelease", 4 } } },
        { "Famicom Poly", "8-Bit", { { "engine", 7 }, { "chipModel", 2 }, { "chipVoice", 3 }, { "chipDuty", 2 }, { "chipRelease", 8 } } },
        { "NES DPCM Kit", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVoice", 5 }, { "chipDpcm", 0 } } },
        { "NES DPCM Slap Bass", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVoice", 5 }, { "chipDpcm", 5 } } },
        { "Game Boy Square", "8-Bit", { { "engine", 7 }, { "chipModel", 3 }, { "chipDuty", 2 }, { "chipVolume", 12 },
                          { "chipVibDepth", 20.0f }, { "chipVibDelay", 15 } } },
        { "Game Boy Wave Bass", "8-Bit", { { "engine", 7 }, { "chipModel", 3 }, { "chipVoice", 1 }, { "chipWave", 0 } } },
        { "Game Boy Boot Wave", "8-Bit", { { "engine", 7 }, { "chipModel", 3 }, { "chipVoice", 1 }, { "chipWave", 6 } } },
        // Pulso 1 con sweep de hardware (gbdev "Gameboy sound hardware")
        { "Game Boy Color Lead", "8-Bit", { { "engine", 7 }, { "chipModel", 4 }, { "chipDuty", 3 }, { "chipArp", 1 }, { "chipArpSpeed", 2 } } },
        // Famicom + VRC6 (Konami): sierra con vibrato de driver y poly de 6 voces (2A03 + VRC6), diseño propio
        { "Famicom VRC6 Saw Lead", "8-Bit", { { "engine", 7 }, { "chipModel", 2 }, { "chipExpansion", 1 }, { "chipVoice", 7 },
                          { "chipVolume", 15 }, { "chipVibDepth", 20.0f }, { "chipVibRate", 5.5f }, { "chipVibDelay", 10 }, { "chipRelease", 4 } } },
        { "Famicom VRC6 Poly", "8-Bit", { { "engine", 7 }, { "chipModel", 2 }, { "chipExpansion", 1 }, { "chipVoice", 8 },
                          { "chipVolume", 12 }, { "chipVrc6Duty", 3 }, { "chipRelease", 6 } } },
        // Macros por frame (Dsp/FrameMacro.h): efectos de sonido e instrumentos de driver, diseñados para KoniSynth
        { "NES Coin Pickup", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVolume", 15 } },
                          { { "chipMacros", "vol: 15 15 15 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0; arp: 0 0 0 0 5; duty: 1" } } },
        { "NES Jump", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVolume", 15 } },
                          { { "chipMacros", "vol: 15 15 14 13 12 11 10 9 8 6 4 2 0; pitch: 0 | -8; duty: 2" } } },
        { "NES Explosion", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVoice", 2 }, { "chipVolume", 15 } },
                          { { "chipMacros", "vol: 15 15 14 13 12 11 10 9 8 7 6 5 4 3 2 2 1 1 0; arp: 0 -1 -2 -3 -4 -5 -6 -7 -8" } } },
        { "NES Echo Lead", "8-Bit", { { "engine", 7 }, { "chipModel", 0 }, { "chipVolume", 15 } },
                          { { "chipMacros", "vol: 15 12 10 9 8 7 7 6 6 6 / 4 2 1 0; duty: 2 2 1" } } },
        { "Game Boy Laser", "8-Bit", { { "engine", 7 }, { "chipModel", 3 }, { "chipVolume", 15 } },
                          { { "chipMacros", "vol: 15 15 14 12 10 8 6 4 2 0; pitch: 0 | 6; duty: 2 1 0" } } },
        // Mega Drive: registros en orden OP1..OP4 del datasheet (plutiedev "YM2612 registers"; DT con signo). Fuentes:
        // Furnace, banco instruments/OPN (README: "free of restrictions... may be used in any project"), .dmp decodificados.
        // "Common Genesis Bass" = el bajo de Sonic 1 (Green Hill Zone, voice $00, s1disasm) salvo TL de OP4 (5 frente a 0)
        // Bajo de Sonic the Hedgehog (Green Hill Zone, FM2, voice $00; s1disasm Mus81 - GHZ.asm l.466-483): mismos registros,
        // con el TL efectivo de la portadora = 0 + atenuación del canal $0B = 11
        { "MD Green Hill Bass", "Bass", { { "engine", 10 }, { "mdAlgorithm", 0 }, { "mdFeedback", 1 }, { "md1Tl", 36 }, { "md1Ar", 31 }, { "md1Dr", 18 },
                          { "md1Sr", 0 }, { "md1Sl", 2 }, { "md1Rr", 15 }, { "md1Mul", 10 }, { "md1Dt", 0 }, { "md1Ks", 0 }, { "md1Am", 0 },
                          { "md2Tl", 45 }, { "md2Ar", 31 }, { "md2Dr", 14 }, { "md2Sr", 4 }, { "md2Sl", 2 }, { "md2Rr", 15 }, { "md2Mul", 0 },
                          { "md2Dt", -3 }, { "md2Ks", 0 }, { "md2Am", 0 }, { "md3Tl", 19 }, { "md3Ar", 31 }, { "md3Dr", 10 }, { "md3Sr", 4 },
                          { "md3Sl", 2 }, { "md3Rr", 15 }, { "md3Mul", 0 }, { "md3Dt", 3 }, { "md3Ks", 1 }, { "md3Am", 0 }, { "md4Tl", 11 },
                          { "md4Ar", 31 }, { "md4Dr", 10 }, { "md4Sr", 3 }, { "md4Sl", 2 }, { "md4Rr", 15 }, { "md4Mul", 0 }, { "md4Dt", 0 },
                          { "md4Ks", 1 }, { "md4Am", 0 } } },
        // Programa de prueba "Grand Piano" del manual técnico de Genesis de Sega, sección YM2612 (SMS Power, Documents/YM2612)
        { "MD Grand Piano", "Keys", { { "engine", 10 }, { "mdAlgorithm", 2 }, { "mdFeedback", 6 }, { "md1Tl", 35 }, { "md1Ar", 31 }, { "md1Dr", 5 },
                          { "md1Sr", 2 }, { "md1Sl", 1 }, { "md1Rr", 1 }, { "md1Mul", 1 }, { "md1Dt", -3 }, { "md1Ks", 1 }, { "md1Am", 0 },
                          { "md2Tl", 38 }, { "md2Ar", 31 }, { "md2Dr", 5 }, { "md2Sr", 2 }, { "md2Sl", 1 }, { "md2Rr", 1 }, { "md2Mul", 3 },
                          { "md2Dt", 3 }, { "md2Ks", 1 }, { "md2Am", 0 }, { "md3Tl", 45 }, { "md3Ar", 25 }, { "md3Dr", 5 }, { "md3Sr", 2 },
                          { "md3Sl", 1 }, { "md3Rr", 1 }, { "md3Mul", 13 }, { "md3Dt", 0 }, { "md3Ks", 2 }, { "md3Am", 0 }, { "md4Tl", 0 },
                          { "md4Ar", 20 }, { "md4Dr", 7 }, { "md4Sr", 2 }, { "md4Sl", 10 }, { "md4Rr", 6 }, { "md4Mul", 1 }, { "md4Dt", 0 },
                          { "md4Ks", 2 }, { "md4Am", 0 } } },
        { "MD Electric Slap Bass", "Bass", { { "engine", 10 }, { "mdAlgorithm", 2 }, { "mdFeedback", 0 }, { "md1Tl", 0 }, { "md1Ar", 31 },
                          { "md1Dr", 20 }, { "md1Sr", 0 }, { "md1Sl", 15 }, { "md1Rr", 3 }, { "md1Mul", 0 }, { "md1Dt", 0 }, { "md1Ks", 1 },
                          { "md1Am", 0 }, { "md2Tl", 43 }, { "md2Ar", 31 }, { "md2Dr", 23 }, { "md2Sr", 0 }, { "md2Sl", 0 }, { "md2Rr", 4 },
                          { "md2Mul", 15 }, { "md2Dt", 3 }, { "md2Ks", 0 }, { "md2Am", 0 }, { "md3Tl", 16 }, { "md3Ar", 31 }, { "md3Dr", 17 },
                          { "md3Sr", 0 }, { "md3Sl", 2 }, { "md3Rr", 1 }, { "md3Mul", 1 }, { "md3Dt", 3 }, { "md3Ks", 1 }, { "md3Am", 0 },
                          { "md4Tl", 2 }, { "md4Ar", 31 }, { "md4Dr", 9 }, { "md4Sr", 0 }, { "md4Sl", 15 }, { "md4Rr", 10 }, { "md4Mul", 1 },
                          { "md4Dt", 3 }, { "md4Ks", 0 }, { "md4Am", 0 } } },
        { "MD Slap Bass", "Bass", { { "engine", 10 }, { "mdAlgorithm", 0 }, { "mdFeedback", 6 }, { "md1Tl", 29 }, { "md1Ar", 31 }, { "md1Dr", 12 },
                          { "md1Sr", 7 }, { "md1Sl", 1 }, { "md1Rr", 3 }, { "md1Mul", 9 }, { "md1Dt", 3 }, { "md1Ks", 0 }, { "md1Am", 0 },
                          { "md2Tl", 41 }, { "md2Ar", 31 }, { "md2Dr", 10 }, { "md2Sr", 7 }, { "md2Sl", 1 }, { "md2Rr", 3 }, { "md2Mul", 5 },
                          { "md2Dt", 3 }, { "md2Ks", 0 }, { "md2Am", 1 }, { "md3Tl", 17 }, { "md3Ar", 31 }, { "md3Dr", 7 }, { "md3Sr", 7 },
                          { "md3Sl", 1 }, { "md3Rr", 3 }, { "md3Mul", 0 }, { "md3Dt", 3 }, { "md3Ks", 0 }, { "md3Am", 1 }, { "md4Tl", 15 },
                          { "md4Ar", 31 }, { "md4Dr", 8 }, { "md4Sr", 8 }, { "md4Sl", 8 }, { "md4Rr", 5 }, { "md4Mul", 1 }, { "md4Dt", 3 },
                          { "md4Ks", 0 }, { "md4Am", 0 }, { "mdAms", 1 } } },
        { "MD Synth Brass", "Brass", { { "engine", 10 }, { "mdAlgorithm", 4 }, { "mdFeedback", 5 }, { "md1Tl", 19 }, { "md1Ar", 20 }, { "md1Dr", 6 },
                          { "md1Sr", 8 }, { "md1Sl", 8 }, { "md1Rr", 6 }, { "md1Mul", 1 }, { "md1Dt", 0 }, { "md1Ks", 0 }, { "md1Am", 0 },
                          { "md2Tl", 21 }, { "md2Ar", 20 }, { "md2Dr", 1 }, { "md2Sr", 0 }, { "md2Sl", 15 }, { "md2Rr", 8 }, { "md2Mul", 2 },
                          { "md2Dt", 0 }, { "md2Ks", 0 }, { "md2Am", 0 }, { "md3Tl", 26 }, { "md3Ar", 29 }, { "md3Dr", 1 }, { "md3Sr", 9 },
                          { "md3Sl", 15 }, { "md3Rr", 6 }, { "md3Mul", 1 }, { "md3Dt", 2 }, { "md3Ks", 0 }, { "md3Am", 0 }, { "md4Tl", 17 },
                          { "md4Ar", 23 }, { "md4Dr", 1 }, { "md4Sr", 0 }, { "md4Sl", 15 }, { "md4Rr", 8 }, { "md4Mul", 3 }, { "md4Dt", -3 },
                          { "md4Ks", 0 }, { "md4Am", 0 } } },
        { "MD E.Piano", "Keys", { { "engine", 10 }, { "mdAlgorithm", 4 }, { "mdFeedback", 7 }, { "md1Tl", 38 }, { "md1Ar", 31 }, { "md1Dr", 0 },
                          { "md1Sr", 0 }, { "md1Sl", 15 }, { "md1Rr", 3 }, { "md1Mul", 11 }, { "md1Dt", 0 }, { "md1Ks", 0 }, { "md1Am", 0 },
                          { "md2Tl", 30 }, { "md2Ar", 31 }, { "md2Dr", 15 }, { "md2Sr", 0 }, { "md2Sl", 15 }, { "md2Rr", 8 }, { "md2Mul", 12 },
                          { "md2Dt", 0 }, { "md2Ks", 0 }, { "md2Am", 0 }, { "md3Tl", 33 }, { "md3Ar", 31 }, { "md3Dr", 6 }, { "md3Sr", 0 },
                          { "md3Sl", 11 }, { "md3Rr", 1 }, { "md3Mul", 1 }, { "md3Dt", 0 }, { "md3Ks", 0 }, { "md3Am", 0 }, { "md4Tl", 0 },
                          { "md4Ar", 31 }, { "md4Dr", 8 }, { "md4Sr", 0 }, { "md4Sl", 15 }, { "md4Rr", 9 }, { "md4Mul", 1 }, { "md4Dt", 0 },
                          { "md4Ks", 0 }, { "md4Am", 0 } } },
        { "MD Tubular Bells", "Keys", { { "engine", 10 }, { "mdAlgorithm", 4 }, { "mdFeedback", 4 }, { "md1Tl", 43 }, { "md1Ar", 31 },
                          { "md1Dr", 8 }, { "md1Sr", 0 }, { "md1Sl", 15 }, { "md1Rr", 4 }, { "md1Mul", 10 }, { "md1Dt", 3 }, { "md1Ks", 0 },
                          { "md1Am", 0 }, { "md2Tl", 10 }, { "md2Ar", 31 }, { "md2Dr", 10 }, { "md2Sr", 0 }, { "md2Sl", 15 }, { "md2Rr", 6 },
                          { "md2Mul", 8 }, { "md2Dt", -1 }, { "md2Ks", 0 }, { "md2Am", 0 }, { "md3Tl", 36 }, { "md3Ar", 31 }, { "md3Dr", 4 },
                          { "md3Sr", 0 }, { "md3Sl", 11 }, { "md3Rr", 1 }, { "md3Mul", 8 }, { "md3Dt", -3 }, { "md3Ks", 0 }, { "md3Am", 0 },
                          { "md4Tl", 6 }, { "md4Ar", 31 }, { "md4Dr", 9 }, { "md4Sr", 0 }, { "md4Sl", 15 }, { "md4Rr", 6 }, { "md4Mul", 5 },
                          { "md4Dt", 2 }, { "md4Ks", 0 }, { "md4Am", 0 } } },
        // "GEMSLike Bass": imitación de la comunidad (Furnace), no un patch oficial de GEMS
        { "MD GEMS-Style Bass", "Bass", { { "engine", 10 }, { "mdAlgorithm", 2 }, { "mdFeedback", 4 }, { "md1Tl", 24 }, { "md1Ar", 31 },
                          { "md1Dr", 10 }, { "md1Sr", 6 }, { "md1Sl", 2 }, { "md1Rr", 9 }, { "md1Mul", 0 }, { "md1Dt", 0 }, { "md1Ks", 0 },
                          { "md1Am", 1 }, { "md2Tl", 19 }, { "md2Ar", 31 }, { "md2Dr", 15 }, { "md2Sr", 0 }, { "md2Sl", 8 }, { "md2Rr", 15 },
                          { "md2Mul", 7 }, { "md2Dt", 0 }, { "md2Ks", 0 }, { "md2Am", 0 }, { "md3Tl", 39 }, { "md3Ar", 31 }, { "md3Dr", 14 },
                          { "md3Sr", 11 }, { "md3Sl", 4 }, { "md3Rr", 0 }, { "md3Mul", 3 }, { "md3Dt", 0 }, { "md3Ks", 0 }, { "md3Am", 0 },
                          { "md4Tl", 5 }, { "md4Ar", 31 }, { "md4Dr", 2 }, { "md4Sr", 5 }, { "md4Sl", 1 }, { "md4Rr", 9 }, { "md4Mul", 1 },
                          { "md4Dt", 0 }, { "md4Ks", 0 }, { "md4Am", 0 }, { "mdAms", 1 } } },
        // SNES: samples propios de KoniSynth (no de juegos); eco con el FIR paso bajo del ejemplo oficial (fullsnes "Echo Registers").
        // Ajustes de KoniSynth al estilo de las bandas sonoras con eco largo (EDL 5-7 = 80-112 ms, EFB 0x40-0x60)
        { "SNES Echo Strings", "Pad", { { "engine", 11 }, { "snesSource", 3 }, { "snesAttack", 8 }, { "snesDecay", 7 }, { "snesSustainLevel", 7 },
                          { "snesEcho", 1 }, { "snesEdl", 6 }, { "snesEfb", 80 }, { "snesEvol", 48 }, { "snesFir", 1 } } },
        { "SNES Choir", "Pad", { { "engine", 11 }, { "snesSource", 4 }, { "snesAttack", 9 }, { "snesEcho", 1 }, { "snesEdl", 5 }, { "snesEfb", 64 },
                          { "snesEvol", 40 }, { "snesFir", 1 } } },
        { "SNES Square Lead", "Lead", { { "engine", 11 }, { "snesSource", 0 }, { "snesAttack", 15 }, { "snesDecay", 4 }, { "snesSustainLevel", 5 },
                          { "snesSustainRate", 12 }, { "snesEcho", 1 }, { "snesEdl", 4 }, { "snesEfb", 48 }, { "snesEvol", 32 }, { "snesFir", 1 } } },
        { "SNES Bass", "Bass", { { "engine", 11 }, { "snesSource", 5 }, { "snesAttack", 15 }, { "snesDecay", 5 }, { "snesSustainLevel", 3 },
                          { "snesSustainRate", 14 } } },
        // ---- Propuesta 2026-10-21: Pad / Strings / Bass / Keys / Pluck / Brass / Lead / Drums / FX / Sequence / Machines
        // PWM desde el LFO 1 (corre aunque lfoOn = 0: Synth.h:309, 352). Receta genérica de pad tipo Juno: pulso + PWM lento + chorus
        // (musicradar.com/how-to/how-to-create-an-80s-juno-style-pad-sound). Cifras propias
        { "PWM Analog Pad", "Pad", { { "wave", 3 }, { "pulseWidth", 0.5f }, { "pwmDepth", 0.3f }, { "lfoRate", 0.6f }, { "subLevel", 0.3f },
                          { "attack", 0.6f }, { "decay", 1.0f }, { "sustain", 0.8f }, { "release", 1.8f }, { "drift", 0.3f },
                          { "cutoff", 1600.0f }, { "resonance", 0.15f }, { "filterEnv", 0.15f }, { "filterAttack", 0.8f }, { "filterDecay", 2.0f }, { "filterSustain", 0.5f },
                          { "chorusOn", 1 }, { "chorusMode", 2 }, { "chorusMix", 0.5f },
                          { "ambientOn", 1 }, { "ambientSize", 0.8f }, { "ambientMix", 0.3f }, { "reverbDecay", 4.0f }, { "gain", 0.55f } } },
        // Ambient: 3 sierras, LP12 que deriva al azar (Random a 0,07 Hz, ±0,5 oct), placa de 18 s. Diseño propio
        { "Endless Plate Pad", "Pad", { { "wave", 2 }, { "unisonVoices", 3 }, { "unisonDetune", 9.0f }, { "unisonSpread", 0.8f },
                          { "attack", 2.5f }, { "decay", 2.0f }, { "sustain", 1.0f }, { "release", 6.0f }, { "filterMode", 1 }, { "cutoff", 1200.0f },
                          { "lfoOn", 1 }, { "lfoTarget", 2 }, { "lfoShape", 5 }, { "lfoRate", 0.07f }, { "lfoDepth", 0.25f },
                          { "ambientOn", 1 }, { "ambientSize", 1.0f }, { "ambientMix", 0.55f }, { "reverbDecay", 18.0f }, { "reverbPredelay", 60.0f },
                          { "reverbDamp", 4000.0f }, { "gain", 0.5f } } },
        // Ambient: desplazador de frecuencia de 0,4 Hz al 50 % -> batido/fase lenta (Help.h: "small values give slow phasing"). Diseño propio
        { "Barber Pole Drone", "Pad", { { "wave", 1 }, { "osc2Wave", 2 }, { "osc2Octave", -1 }, { "osc2Level", 0.35f },
                          { "attack", 3.0f }, { "sustain", 1.0f }, { "release", 8.0f }, { "cutoff", 900.0f },
                          { "fshiftOn", 1 }, { "fshiftHz", 0.4f }, { "fshiftMix", 0.5f },
                          { "echoOn", 1 }, { "echoTime", 600.0f }, { "echoFeedback", 0.6f }, { "echoMix", 0.3f }, { "echoTone", 2000.0f },
                          { "ambientOn", 1 }, { "ambientMix", 0.5f }, { "reverbDecay", 12.0f } } },
        // Tabla "PWM Sweep" (sin uso en los presets actuales) recorrida por el LFO 2 en triangular. Diseño propio
        { "PWM Sweep Pad", "Pad", { { "osc1Level", 0.0f }, { "wtLevel", 1.0f }, { "wtTable", 1 }, { "wtPosition", 0.3f },
                          { "lfo2On", 1 }, { "lfo2Target", 5 }, { "lfo2Shape", 1 }, { "lfo2Rate", 0.12f }, { "lfo2Depth", 0.6f },
                          { "attack", 1.0f }, { "sustain", 1.0f }, { "release", 2.5f }, { "voicePan", 0.5f },
                          { "chorusOn", 1 }, { "chorusMode", 1 }, { "chorusMix", 0.4f }, { "ambientOn", 1 }, { "ambientMix", 0.35f }, { "reverbDecay", 5.0f } } },
        // FM 6-Op, algoritmo 5 (3 pares): portadoras 1:1 desafinadas ±4, moduladores 3/1/1 a nivel bajo, ataque lento (R1 40-45,
        // tiempo estimado ~1 s), vibrato retardado. Diseño propio
        { "FM Glass Pad", "Pad", { { "engine", 6 }, { "fmAlgorithm", 5 },
                          { "fm1Coarse", 1 }, { "fm1Level", 95 }, { "fm1R1", 45 }, { "fm1R2", 30 }, { "fm1L2", 90 }, { "fm1R3", 20 }, { "fm1L3", 85 }, { "fm1R4", 35 },
                          { "fm2Coarse", 3 }, { "fm2Level", 55 }, { "fm2Vel", 3 }, { "fm2R1", 40 }, { "fm2R2", 25 }, { "fm2L2", 70 }, { "fm2R3", 20 }, { "fm2L3", 60 }, { "fm2R4", 35 },
                          { "fm3Coarse", 1 }, { "fm3Level", 90 }, { "fm3Detune", 4 }, { "fm3R1", 45 }, { "fm3R2", 30 }, { "fm3L2", 90 }, { "fm3R3", 20 }, { "fm3L3", 85 }, { "fm3R4", 35 },
                          { "fm4Coarse", 1 }, { "fm4Level", 60 }, { "fm4R1", 40 }, { "fm4R4", 35 },
                          { "fm5Coarse", 2 }, { "fm5Level", 80 }, { "fm5Detune", -4 }, { "fm5R1", 45 }, { "fm5R4", 35 },
                          { "fm6Coarse", 1 }, { "fm6Level", 45 }, { "fm6R1", 40 }, { "fm6R4", 35 },
                          { "fmLfoSpeed", 30 }, { "fmLfoDelay", 50 }, { "fmLfoPmd", 4 },
                          { "ambientOn", 1 }, { "ambientMix", 0.35f }, { "reverbDecay", 5.0f } } },
        // Mega Drive, algoritmo 7 (4 portadoras, MegaDrive.h:134): aditivo de senos ×1, ×2, ×4 y ×1 desafinado; AR 13-14 (ataque lento,
        // tiempo no medido), LFO 5,86 Hz PMS 2. Diseño propio
        { "MD Additive Pad", "Pad", { { "engine", 10 }, { "mdAlgorithm", 7 }, { "mdFeedback", 2 }, { "mdLfo", 3 }, { "mdPms", 2 },
                          { "md1Tl", 12 }, { "md1Ar", 14 }, { "md1Dr", 2 }, { "md1Sl", 2 }, { "md1Rr", 5 }, { "md1Mul", 1 }, { "md1Dt", 0 },
                          { "md2Tl", 24 }, { "md2Ar", 14 }, { "md2Dr", 3 }, { "md2Sl", 3 }, { "md2Rr", 5 }, { "md2Mul", 2 }, { "md2Dt", 1 },
                          { "md3Tl", 32 }, { "md3Ar", 13 }, { "md3Dr", 4 }, { "md3Sl", 4 }, { "md3Rr", 5 }, { "md3Mul", 4 }, { "md3Dt", -1 },
                          { "md4Tl", 10 }, { "md4Ar", 14 }, { "md4Dr", 2 }, { "md4Sl", 2 }, { "md4Rr", 5 }, { "md4Mul", 1 }, { "md4Dt", 3 },
                          { "ambientOn", 1 }, { "ambientMix", 0.35f }, { "reverbDecay", 4.0f } } },
        // N64: sierra propia a 22 kHz, envolvente lenta, Big Room de libultra. Diseño propio
        { "N64 Saw Pad", "Pad", { { "engine", 12 }, { "n64Source", 1 }, { "n64Attack", 600.0f }, { "n64Release", 1500.0f }, { "n64Spread", 0.6f },
                          { "n64Fx", 2 }, { "n64FxMix", 70 } } },
        // Pizzicato sintético: sierra, LP12 con envolvente de filtro de 120 ms, sin sostenido. Diseño propio
        { "Synth Pizzicato", "Strings", { { "wave", 2 }, { "unisonVoices", 2 }, { "unisonDetune", 6.0f }, { "attack", 0.002f }, { "decay", 0.25f }, { "sustain", 0.0f },
                          { "release", 0.2f }, { "filterMode", 1 }, { "cutoff", 600.0f }, { "filterEnv", 0.5f }, { "filterAttack", 0.001f }, { "filterDecay", 0.12f },
                          { "filterSustain", 0.0f }, { "filterKey", 0.6f }, { "velToFilter", 0.4f },
                          { "ambientOn", 1 }, { "ambientSize", 0.7f }, { "ambientMix", 0.3f }, { "reverbDecay", 2.5f } } },
        // FM 6-Op, algoritmo 1 (portadoras OP1 y OP3; OP5/OP6 a nivel 0 -> OP4 modula a OP3): dos pilas 1:1 desafinadas ±3 + ensemble.
        // Diseño propio
        { "FM Ensemble Strings", "Strings", { { "engine", 6 }, { "fmAlgorithm", 1 },
                          { "fm1Coarse", 1 }, { "fm1Level", 95 }, { "fm1Detune", 3 }, { "fm1R1", 50 }, { "fm1R2", 40 }, { "fm1L2", 95 }, { "fm1R3", 30 }, { "fm1L3", 90 }, { "fm1R4", 40 },
                          { "fm2Coarse", 1 }, { "fm2Level", 72 }, { "fm2Vel", 2 }, { "fm2R1", 45 }, { "fm2R2", 30 }, { "fm2L2", 85 }, { "fm2R3", 30 }, { "fm2L3", 80 }, { "fm2R4", 40 },
                          { "fm3Coarse", 1 }, { "fm3Level", 95 }, { "fm3Detune", -3 }, { "fm3R1", 50 }, { "fm3R2", 40 }, { "fm3L2", 95 }, { "fm3R3", 30 }, { "fm3L3", 90 }, { "fm3R4", 40 },
                          { "fm4Coarse", 2 }, { "fm4Level", 60 }, { "fm4R1", 45 }, { "fm4R4", 40 },
                          { "fmLfoSpeed", 33 }, { "fmLfoDelay", 60 }, { "fmLfoPmd", 5 },
                          { "chorusOn", 1 }, { "chorusMode", 3 }, { "chorusMix", 0.5f } } },
        // OPL2 2-op FM, ataque lento (AR 6) y vibrato profundo en ambos operadores. Diseño propio
        { "OPL2 Strings", "Strings", { { "engine", 13 }, { "oplModel", 0 }, { "oplAlgorithm", 0 }, { "oplFeedback", 3 }, { "oplDeepVibrato", 1 },
                          { "opl1Mult", 1 }, { "opl1Tl", 26 }, { "opl1Ar", 6 }, { "opl1Dr", 2 }, { "opl1Sl", 2 }, { "opl1Rr", 5 }, { "opl1Vib", 1 },
                          { "opl2Mult", 1 }, { "opl2Tl", 0 }, { "opl2Ar", 6 }, { "opl2Dr", 1 }, { "opl2Sl", 1 }, { "opl2Rr", 5 }, { "opl2Vib", 1 } } },
        // SNES: sample Strings con ADSR percusivo (SL 1, SR 18) y eco corto (EDL 4 = 64 ms). Diseño propio
        { "SNES Pizzicato", "Strings", { { "engine", 11 }, { "snesSource", 3 }, { "snesAttack", 15 }, { "snesDecay", 4 }, { "snesSustainLevel", 1 },
                          { "snesSustainRate", 18 }, { "snesEcho", 1 }, { "snesEdl", 4 }, { "snesEfb", 40 }, { "snesEvol", 32 }, { "snesFir", 1 } } },
        // Dos sierras a 12 ct: batido de 0,38 Hz en A1 (55 Hz · (2^(12/1200) - 1)). Técnica general de bajo de sierras desafinadas; cifras propias
        { "Phasing Saw Bass", "Bass", { { "voiceMode", 1 }, { "wave", 2 }, { "osc2Wave", 2 }, { "osc2Fine", 12.0f }, { "osc2Level", 1.0f }, { "subLevel", 0.4f },
                          { "attack", 0.002f }, { "decay", 0.3f }, { "sustain", 1.0f }, { "release", 0.12f }, { "cutoff", 420.0f }, { "resonance", 0.2f },
                          { "filterDrive", 0.3f }, { "filterEnv", 0.2f }, { "filterDecay", 0.4f }, { "filterKey", 0.3f }, { "tubeOn", 1 }, { "tubeDrive", 6.0f },
                          { "gain", 0.55f } } },
        // OPL3: portadora en forma 6 (cuadrada, solo OPL3) modulada levemente. Diseño propio
        { "OPL3 Square Bass", "Bass", { { "engine", 13 }, { "oplModel", 1 }, { "oplAlgorithm", 0 }, { "oplFeedback", 2 },
                          { "opl1Mult", 1 }, { "opl1Tl", 34 }, { "opl1Ar", 15 }, { "opl1Dr", 5 }, { "opl1Sl", 6 }, { "opl1Rr", 8 },
                          { "opl2Mult", 1 }, { "opl2Tl", 0 }, { "opl2Ar", 15 }, { "opl2Dr", 2 }, { "opl2Sl", 3 }, { "opl2Rr", 9 }, { "opl2Wave", 6 } } },
        // Polivoks: una sierra a 16', resonancia alta, envolvente de filtro corta y glide de 60 ms (bajo ácido). Diseño propio
        { "Polivoks Acid Bass", "Bass", { { "engine", 8 }, { "pvWave1", 1 }, { "pvRange1", 1 }, { "pvMix1", 0.9f }, { "pvMix2", 0.0f }, { "pvCutoff", 350.0f },
                          { "pvReso", 0.75f }, { "pvEnvAmount", 5000.0f }, { "pvFAttack", 0.001f }, { "pvFDecay", 0.18f }, { "pvFSustain", 0.0f },
                          { "pvAAttack", 0.002f }, { "pvADecay", 0.3f }, { "pvASustain", 0.7f }, { "pvARelease", 0.08f }, { "pvGlide", 0.06f } } },
        // Clavinet sintético: pulso estrecho (18 %) + BP con envolvente rápida y velocity, phaser. Diseño propio
        { "Funky Clav", "Keys", { { "wave", 3 }, { "pulseWidth", 0.18f }, { "osc2Wave", 3 }, { "osc2Octave", 1 }, { "osc2Level", 0.3f },
                          { "attack", 0.001f }, { "decay", 0.5f }, { "sustain", 0.0f }, { "release", 0.06f }, { "filterMode", 2 }, { "cutoff", 900.0f },
                          { "resonance", 0.45f }, { "filterEnv", 0.45f }, { "filterAttack", 0.001f }, { "filterDecay", 0.15f }, { "filterKey", 0.8f },
                          { "velToFilter", 0.5f }, { "phaserOn", 1 }, { "phaserRate", 0.8f }, { "phaserMix", 0.35f } } },
        // Karplus-Strong: púa al 20 % (más redondo que el clave al 10 %), brillo bajo = dedo. Posición de pulsado -> armónicos ausentes
        // (soundonsound.com/techniques/synthesizing-plucked-strings). Cifras propias
        // Bajo eléctrico con púa: excitación triangular (Pick), pastilla al 18 % y Damping, ajustados contra la media de 4
        // grabaciones reales de E2/F2 con púa (Freesound, solo medida): error de armónicos h1-h6 2,1 dB RMS (antes 18,5)
        { "Picked Electric Bass", "Bass", { { "osc1Level", 0.0f }, { "stringLevel", 1.0f }, { "stringExcite", 1 }, { "stringPickup", 0.18f },
                          { "stringDamping", 0.16f }, { "stringBright", 0.28f }, { "stringPick", 0.03f }, { "stringDecay", 1.4f },
                          { "filterOn", 0 }, { "drift", 0.05f }, { "attack", 0.001f }, { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 0.12f },
                          { "gain", 0.75f } } },
        { "Nylon Guitar", "Pluck", { { "osc1Level", 0.0f }, { "stringLevel", 1.0f }, { "stringDecay", 3.0f }, { "stringBright", 0.4f }, { "stringPick", 0.2f },
                          { "attack", 0.001f }, { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 0.4f }, { "cutoff", 6000.0f },
                          { "ambientOn", 1 }, { "ambientSize", 0.5f }, { "ambientMix", 0.2f }, { "reverbDecay", 1.8f }, { "gain", 0.6f } } },
        { "Concert Harp", "Pluck", { { "osc1Level", 0.0f }, { "stringLevel", 1.0f }, { "stringDecay", 7.0f }, { "stringBright", 0.55f }, { "stringPick", 0.35f },
                          { "attack", 0.001f }, { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 2.0f }, { "cutoff", 9000.0f },
                          { "voicePan", 0.4f }, { "voicePanMode", 1 },
                          { "ambientOn", 1 }, { "ambientSize", 0.8f }, { "ambientMix", 0.35f }, { "reverbDecay", 3.5f }, { "gain", 0.6f } } },
        // Filtros, saturación por voz y VCA analógico de la 0.7 (Dsp/Filters.h, Dsp/VoiceSaturator.h). Diseños propios
        // Dual BP: picos en 892·2^(∓0,29) = 730 y 1090 Hz, formantes F1/F2 de /ɑ/ en voz masculina (Peterson y Barney, 1952;
        // no verificado en esta sesión)
        { "Dual Formant Ah", "Vocal", { { "wave", 2 }, { "filterMode", 12 }, { "cutoff", 892.0f }, { "filterSpread", 0.58f }, { "resonance", 0.75f },
                          { "attack", 0.08f }, { "sustain", 1.0f }, { "release", 0.4f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.2f },
                          { "lfoDepth", 0.08f }, { "ambientOn", 1 }, { "ambientMix", 0.2f }, { "gain", 0.4f } } },
        // MS-20 LP con resonancia alta y envolvente rápida, saturación Diode tras el filtro; mono legato
        { "MS-20 Acid Scream", "Lead", { { "wave", 2 }, { "voiceMode", 2 }, { "glide", 40.0f }, { "filterMode", 8 }, { "cutoff", 500.0f },
                          { "resonance", 0.85f }, { "filterEnv", 0.6f }, { "filterAttack", 0.001f }, { "filterDecay", 0.3f }, { "filterSustain", 0.2f },
                          { "filterDrive", 0.5f }, { "voiceSatMode", 1 }, { "voiceSatDrive", 9.0f }, { "attack", 0.001f }, { "sustain", 1.0f },
                          { "release", 0.1f }, { "gain", 0.35f } } },
        // Comb + afinado a la nota (Cutoff C4 con Key 100 %) excitado con sierra y ruido. Drift 0: la tolerancia de corte por voz
        // (±10 % del Drift) desafina el peine respecto a la nota y apaga los armónicos altos (con Drift 30 %, h4-h10 entre 8 y 22 dB menos)
        { "Comb Bowed Metal", "Strings", { { "wave", 2 }, { "osc1Level", 0.4f }, { "noiseLevel", 1.0f }, { "filterMode", 10 }, { "cutoff", 261.63f },
                          { "filterKey", 1.0f }, { "resonance", 0.9f }, { "drift", 0.0f }, { "attack", 0.3f }, { "sustain", 1.0f }, { "release", 1.2f },
                          { "ambientOn", 1 }, { "ambientMix", 0.3f }, { "gain", 0.6f } } },
        // Sierra + cuadrada a -1 oct, ladder LP 24, golpe del VCA (Click), fuga a -70 dB y saturación Soft a 10 dB
        { "Analog Thump Bass", "Bass", { { "wave", 2 }, { "osc2Wave", 3 }, { "osc2Octave", -1 }, { "osc2Level", 0.5f }, { "cutoff", 400.0f },
                          { "resonance", 0.2f }, { "filterEnv", 0.4f }, { "filterAttack", 0.001f }, { "filterDecay", 0.25f }, { "filterSustain", 0.0f },
                          { "vcaClick", 0.5f }, { "vcaLeak", -70.0f }, { "voiceSatMode", 0 }, { "voiceSatDrive", 10.0f },
                          { "attack", 0.001f }, { "decay", 0.4f }, { "sustain", 0.7f }, { "release", 0.08f } } },
        // Senoide plegada (Fold a 18 dB: armónicos impares, estilo West Coast) y SVF LP que suaviza el extremo agudo
        { "Wavefold Lead", "Lead", { { "wave", 0 }, { "voiceSatMode", 3 }, { "voiceSatDrive", 18.0f }, { "filterMode", 4 }, { "cutoff", 5000.0f },
                          { "resonance", 0.2f }, { "attack", 0.005f }, { "sustain", 1.0f }, { "release", 0.2f }, { "lfoOn", 1 }, { "lfoTarget", 0 },
                          { "lfoRate", 5.5f }, { "lfoDepth", 0.1f } } },
        // Tres sierras en unísono por un SVF Notch que barre el LFO a 0,25 Hz
        { "Notch Sweep Pad", "Pad", { { "wave", 2 }, { "unisonVoices", 3 }, { "unisonDetune", 12.0f }, { "filterMode", 7 }, { "cutoff", 1200.0f },
                          { "resonance", 0.3f }, { "lfoOn", 1 }, { "lfoTarget", 2 }, { "lfoRate", 0.25f }, { "lfoDepth", 0.6f }, { "attack", 1.2f },
                          { "sustain", 1.0f }, { "release", 2.5f }, { "ambientOn", 1 }, { "ambientMix", 0.3f }, { "gain", 0.3f } } },
        // Instrumentos y sonidos populares de los 60 a los 90: recreaciones (cifras con fuente en la investigación de la tanda;
        // el resto, estimadas). Los artistas de referencia van en el README, no en los nombres
        // Años 60
        { "Tonewheel Organ B3", "Keys", { { "engine", 6 }, { "fmAlgorithm", 32 }, { "fm1Coarse", 0 }, { "fm1Level", 99 }, { "fm1R4", 85 }, { "fm2Coarse", 1 }, { "fm2Fine", 50 }, { "fm2Level", 99 }, { "fm2R4", 85 }, { "fm3Coarse", 1 }, { "fm3Level", 99 }, { "fm3R4", 85 }, { "fm4Coarse", 3 }, { "fm4Level", 92 }, { "fm4R2", 62 }, { "fm4L2", 0 }, { "fm4R3", 62 }, { "fm4L3", 0 }, { "fm4R4", 62 }, { "leslieOn", 1 }, { "leslieSpeed", 1 }, { "leslieDrive", 0.3f }, { "gain", 0.32f } } },
        { "Continental Combo Organ", "Keys", { { "wave", 3 }, { "osc2Wave", 3 }, { "osc2Octave", 1 }, { "osc2Level", 0.6f }, { "subLevel", 0.5f }, { "drift", 0.05f }, { "attack", 0.003f }, { "decay", 0.1f }, { "sustain", 1.0f }, { "release", 0.02f }, { "filterMode", 1 }, { "cutoff", 3500.0f }, { "filterKey", 0.5f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 6.0f }, { "lfoDepth", 0.03f }, { "ambientOn", 1 }, { "ambientMix", 0.15f }, { "gain", 0.29f } } },
        { "Compact Combo Organ", "Keys", { { "wave", 3 }, { "pulseWidth", 0.25f }, { "osc2Wave", 3 }, { "osc2Octave", 1 }, { "osc2Level", 0.5f }, { "drift", 0.05f }, { "attack", 0.003f }, { "decay", 0.1f }, { "sustain", 1.0f }, { "release", 0.02f }, { "filterMode", 3 }, { "cutoff", 350.0f }, { "resonance", 0.1f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 6.5f }, { "lfoDepth", 0.05f }, { "springOn", 1 }, { "springMix", 0.2f }, { "springDecay", 2.0f }, { "gain", 0.29f } } },
        { "Tape Flute", "Keys", { { "wave", 0 }, { "osc2Wave", 1 }, { "osc2Level", 0.3f }, { "noiseLevel", 0.05f }, { "noiseColor", 1 }, { "drift", 0.4f }, { "attack", 0.06f }, { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 0.15f }, { "filterMode", 1 }, { "cutoff", 3000.0f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.0f }, { "lfoDepth", 0.04f }, { "mediaOn", 1 }, { "mediaType", 3 }, { "mediaAge", 0.4f }, { "mediaNoise", 0.3f }, { "mediaWow", 0.4f }, { "mediaDrive", 0.2f }, { "ambientOn", 1 }, { "ambientMix", 0.2f } } },
        { "Pianet Reed Keys", "Keys", { { "osc1Level", 0.0f }, { "stringLevel", 1.0f }, { "stringExcite", 1 }, { "stringPick", 0.15f }, { "stringBright", 0.55f }, { "stringDecay", 1.5f }, { "drift", 0.05f }, { "attack", 0.001f }, { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 0.03f }, { "filterMode", 1 }, { "cutoff", 7000.0f }, { "tubeOn", 1 }, { "tubeDrive", 4.0f }, { "ambientOn", 1 }, { "ambientMix", 0.15f }, { "gain", 0.6f } } },
        { "Valve Clavioline", "Lead", { { "voiceMode", 1 }, { "wave", 3 }, { "drift", 0.3f }, { "attack", 0.01f }, { "decay", 0.2f }, { "sustain", 1.0f }, { "release", 0.08f }, { "filterMode", 2 }, { "cutoff", 1200.0f }, { "resonance", 0.4f }, { "filterKey", 0.5f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 6.5f }, { "lfoDepth", 0.08f }, { "tubeOn", 1 }, { "tubeDrive", 8.0f }, { "tubeBias", 0.4f }, { "echoOn", 1 }, { "echoTime", 120.0f }, { "echoFeedback", 0.3f }, { "echoMix", 0.2f }, { "ambientOn", 1 }, { "ambientMix", 0.3f }, { "gain", 0.26f } } },
        { "Reed EP 200A", "Keys", { { "wave", 0 }, { "complexOn", 1 }, { "modWave", 0 }, { "modRatio", 1.0f }, { "fmIndex", 0.5f }, { "mtxSrc1", 5 }, { "mtxDst1", 12 }, { "mtxAmt1", 0.3f }, { "mtxSrc2", 3 }, { "mtxDst2", 12 }, { "mtxAmt2", 0.3f }, { "filterMode", 1 }, { "cutoff", 6000.0f }, { "filterAttack", 0.001f }, { "filterDecay", 0.3f }, { "filterSustain", 0.2f }, { "attack", 0.002f }, { "decay", 3.0f }, { "sustain", 0.0f }, { "release", 0.25f }, { "drift", 0.05f }, { "voiceSatMode", 0 }, { "voiceSatDrive", 9.0f }, { "lfoOn", 1 }, { "lfoTarget", 1 }, { "lfoRate", 5.6f }, { "lfoDepth", 0.35f }, { "gain", 0.6f } } },
        { "Modular Lead 1968", "Lead", { { "voiceMode", 1 }, { "glide", 30.0f }, { "wave", 2 }, { "osc2Wave", 3 }, { "osc2Fine", 4.0f }, { "osc2Level", 0.6f }, { "drift", 0.4f }, { "envCurve", 1 }, { "attack", 0.01f }, { "decay", 0.3f }, { "sustain", 0.8f }, { "release", 0.15f }, { "cutoff", 1200.0f }, { "resonance", 0.2f }, { "filterEnv", 0.4f }, { "filterAttack", 0.01f }, { "filterDecay", 0.4f }, { "filterSustain", 0.5f }, { "filterKey", 0.5f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.5f }, { "lfoDepth", 0.03f }, { "ambientOn", 1 }, { "ambientMix", 0.15f } } },
        // Años 70
        { "Model D Bass", "Bass", { { "voiceMode", 1 }, { "wave", 2 }, { "osc2Wave", 2 }, { "osc2Fine", 6.0f }, { "osc2Level", 0.8f }, { "subLevel", 0.5f }, { "envCurve", 1 }, { "attack", 0.001f }, { "decay", 0.5f }, { "sustain", 0.9f }, { "release", 0.05f }, { "cutoff", 300.0f }, { "resonance", 0.2f }, { "filterDrive", 0.3f }, { "filterEnv", 0.5f }, { "filterAttack", 0.001f }, { "filterDecay", 0.35f }, { "filterSustain", 0.2f }, { "filterRelease", 0.05f }, { "filterKey", 0.33f }, { "gain", 0.55f } } },
        { "Model D Lead", "Lead", { { "voiceMode", 2 }, { "glide", 80.0f }, { "wave", 2 }, { "unisonVoices", 3 }, { "unisonDetune", 5.0f }, { "unisonSpread", 0.0f }, { "envCurve", 1 }, { "attack", 0.005f }, { "decay", 0.6f }, { "sustain", 0.85f }, { "release", 0.15f }, { "cutoff", 1500.0f }, { "resonance", 0.35f }, { "filterDrive", 0.4f }, { "filterEnv", 0.35f }, { "filterAttack", 0.005f }, { "filterDecay", 0.6f }, { "filterSustain", 0.4f }, { "filterKey", 0.67f }, { "lfoRate", 5.5f }, { "lfoShape", 1 }, { "modWheelDepth", 0.3f }, { "gain", 0.5f } } },
        { "Odyssey Sync Lead", "Lead", { { "voiceMode", 2 }, { "glide", 40.0f }, { "wave", 3 }, { "osc1Level", 0.3f }, { "osc2Wave", 2 }, { "osc2Sync", 1 }, { "osc2Semi", 7 }, { "osc2Level", 1.0f }, { "mtxSrc1", 1 }, { "mtxDst1", 2 }, { "mtxAmt1", 0.25f }, { "lfoRate", 0.4f }, { "lfoShape", 1 }, { "filterMode", 1 }, { "cutoff", 3000.0f }, { "resonance", 0.3f }, { "filterEnv", 0.3f }, { "filterAttack", 0.002f }, { "filterDecay", 0.5f }, { "filterSustain", 0.3f }, { "attack", 0.002f }, { "sustain", 1.0f }, { "release", 0.1f }, { "echoOn", 1 }, { "echoSync", 2 }, { "echoFeedback", 0.3f }, { "echoMix", 0.15f }, { "gain", 0.45f } } },
        { "Taurus Pedal Bass", "Bass", { { "voiceMode", 1 }, { "wave", 2 }, { "osc2Wave", 3 }, { "osc2Fine", 8.0f }, { "osc2Level", 0.8f }, { "envCurve", 1 }, { "attack", 0.01f }, { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 0.6f }, { "cutoff", 250.0f }, { "resonance", 0.3f }, { "filterEnv", 0.4f }, { "filterAttack", 0.01f }, { "filterDecay", 1.5f }, { "filterSustain", 0.3f }, { "filterRelease", 0.8f }, { "filterKey", 0.5f }, { "gain", 0.6f } } },
        { "Suitcase Tine EP", "Keys", { { "wave", 0 }, { "complexOn", 1 }, { "modWave", 0 }, { "modRatio", 1.0f }, { "fmIndex", 0.4f }, { "mtxSrc1", 5 }, { "mtxDst1", 12 }, { "mtxAmt1", 0.3f }, { "mtxSrc2", 3 }, { "mtxDst2", 12 }, { "mtxAmt2", 0.4f }, { "mtxSrc3", 2 }, { "mtxDst3", 11 }, { "mtxAmt3", 0.8f }, { "lfo2Rate", 4.5f }, { "filterMode", 1 }, { "cutoff", 8000.0f }, { "filterAttack", 0.001f }, { "filterDecay", 0.25f }, { "filterSustain", 0.1f }, { "attack", 0.001f }, { "decay", 6.0f }, { "sustain", 0.0f }, { "release", 0.3f }, { "drift", 0.05f }, { "voiceSatMode", 0 }, { "voiceSatDrive", 3.0f }, { "gain", 0.40f } } },
        { "Clavinet D6 Strings", "Keys", { { "osc1Level", 0.0f }, { "stringLevel", 1.0f }, { "stringExcite", 1 }, { "stringPick", 0.05f }, { "stringPickup", 0.1f }, { "stringBright", 0.8f }, { "stringDecay", 2.0f }, { "stringDamping", 0.2f }, { "drift", 0.05f }, { "attack", 0.001f }, { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 0.03f }, { "filterMode", 3 }, { "cutoff", 250.0f }, { "resonance", 0.1f }, { "gain", 0.7f } } },
        { "Five-Voice Poly Brass", "Brass", { { "wave", 2 }, { "osc2Wave", 2 }, { "osc2Fine", 7.0f }, { "osc2Level", 0.8f }, { "drift", 0.35f }, { "voiceTolerance", 1 }, { "envCurve", 1 }, { "attack", 0.05f }, { "decay", 0.5f }, { "sustain", 0.85f }, { "release", 0.35f }, { "cutoff", 700.0f }, { "resonance", 0.15f }, { "filterEnv", 0.45f }, { "filterAttack", 0.08f }, { "filterDecay", 0.6f }, { "filterSustain", 0.5f }, { "filterRelease", 0.4f }, { "velToFilter", 0.3f }, { "ambientOn", 1 }, { "ambientMix", 0.2f }, { "gain", 0.5f } } },
        { "Poly Vox Humana", "Vocal", { { "wave", 2 }, { "osc2Wave", 2 }, { "osc2Octave", 1 }, { "osc2Level", 0.5f }, { "unisonVoices", 2 }, { "unisonDetune", 6.0f }, { "unisonSpread", 0.5f }, { "drift", 0.1f }, { "attack", 0.15f }, { "decay", 0.5f }, { "sustain", 1.0f }, { "release", 0.8f }, { "filterMode", 1 }, { "cutoff", 3500.0f }, { "formantOn", 1 }, { "formantVoice", 0 }, { "formantVowel", 0.0f }, { "formantMix", 0.5f }, { "chorusOn", 1 }, { "chorusMode", 3 }, { "chorusMix", 0.5f }, { "ambientOn", 1 }, { "ambientMix", 0.25f }, { "gain", 0.45f } } },
        // Años 80 (DX7 E.Piano 1 y Bass 1: voces 11 y 15 de la ROM1A, decodificadas con la conversión de Dx7Syx.h;
        // Bass 1 trae TRNSP -12 y el motor FM no transpone: tocarla una octava más abajo)
        { "DX7 E.Piano 1", "Keys", { { "engine", 6 }, { "fmAlgorithm", 5 }, { "fmFeedback", 6 }, { "fmLfoWave", 4 }, { "fmLfoSpeed", 34 }, { "fmLfoDelay", 33 }, { "fmLfoPms", 3 }, { "fm1Detune", 3 }, { "fm1Level", 99 }, { "fm1R1", 96 }, { "fm1R2", 25 }, { "fm1R3", 25 }, { "fm1R4", 67 }, { "fm1L2", 75 }, { "fm1L3", 0 }, { "fm1Vel", 2 }, { "fm1RateScale", 3 }, { "fm2Coarse", 14 }, { "fm2Level", 58 }, { "fm2R1", 95 }, { "fm2R2", 50 }, { "fm2R3", 35 }, { "fm2R4", 78 }, { "fm2L2", 75 }, { "fm2L3", 0 }, { "fm2Vel", 7 }, { "fm2RateScale", 3 }, { "fm3Level", 99 }, { "fm3R1", 95 }, { "fm3R2", 20 }, { "fm3R3", 20 }, { "fm3R4", 50 }, { "fm3L2", 95 }, { "fm3L3", 0 }, { "fm3Vel", 2 }, { "fm3RateScale", 3 }, { "fm4Level", 89 }, { "fm4R1", 95 }, { "fm4R2", 29 }, { "fm4R3", 20 }, { "fm4R4", 50 }, { "fm4L2", 95 }, { "fm4L3", 0 }, { "fm4Vel", 6 }, { "fm4RateScale", 3 }, { "fm5Detune", -7 }, { "fm5Level", 99 }, { "fm5R1", 95 }, { "fm5R2", 20 }, { "fm5R3", 20 }, { "fm5R4", 50 }, { "fm5L2", 95 }, { "fm5L3", 0 }, { "fm5RateScale", 3 }, { "fm6Detune", 7 }, { "fm6Level", 79 }, { "fm6R1", 95 }, { "fm6R2", 29 }, { "fm6R3", 20 }, { "fm6R4", 50 }, { "fm6L2", 95 }, { "fm6L3", 0 }, { "fm6Vel", 6 }, { "fm6RateScale", 3 }, { "fm6Break", 62 }, { "fm6RDepth", 19 } } },
        { "DX7 Bass 1", "Bass", { { "engine", 6 }, { "fmAlgorithm", 16 }, { "fmFeedback", 7 }, { "fm1Coarse", 0 }, { "fm1Level", 99 }, { "fm1R1", 95 }, { "fm1R2", 62 }, { "fm1R3", 17 }, { "fm1R4", 58 }, { "fm1L2", 95 }, { "fm1L3", 32 }, { "fm1RateScale", 7 }, { "fm1Break", 57 }, { "fm1LDepth", 57 }, { "fm1RDepth", 14 }, { "fm1LCurve", 3 }, { "fm2Coarse", 0 }, { "fm2Level", 80 }, { "fm2R2", 20 }, { "fm2R3", 0 }, { "fm2R4", 0 }, { "fm2L2", 0 }, { "fm2L3", 0 }, { "fm2RateScale", 7 }, { "fm3Coarse", 0 }, { "fm3Level", 99 }, { "fm3R1", 88 }, { "fm3R2", 96 }, { "fm3R3", 32 }, { "fm3R4", 30 }, { "fm3L1", 79 }, { "fm3L2", 65 }, { "fm3L3", 0 }, { "fm3Vel", 3 }, { "fm3RateScale", 6 }, { "fm4Coarse", 5 }, { "fm4Level", 93 }, { "fm4R1", 90 }, { "fm4R2", 42 }, { "fm4R3", 7 }, { "fm4R4", 55 }, { "fm4L1", 90 }, { "fm4L2", 30 }, { "fm4L3", 0 }, { "fm4Vel", 5 }, { "fm4RateScale", 5 }, { "fm5Coarse", 0 }, { "fm5Level", 62 }, { "fm5R2", 0 }, { "fm5R3", 0 }, { "fm5R4", 0 }, { "fm5L2", 0 }, { "fm5L3", 0 }, { "fm5Vel", 3 }, { "fm5RateScale", 7 }, { "fm5Break", 73 }, { "fm5LDepth", 75 }, { "fm6Coarse", 9 }, { "fm6Level", 85 }, { "fm6R1", 94 }, { "fm6R2", 56 }, { "fm6R3", 24 }, { "fm6R4", 55 }, { "fm6L1", 93 }, { "fm6L2", 28 }, { "fm6L3", 0 }, { "fm6Vel", 7 }, { "fm6RateScale", 1 } } },
        { "Wavetable Pad PPG", "Pad", { { "osc1Level", 0.0f }, { "wtLevel", 1.0f }, { "wtTable", 2 }, { "wtPosition", 0.1f }, { "mtxSrc1", 3 }, { "mtxDst1", 14 }, { "mtxAmt1", 0.6f }, { "filterMode", 0 }, { "cutoff", 2500.0f }, { "resonance", 0.2f }, { "filterEnv", 0.2f }, { "filterAttack", 1.5f }, { "filterDecay", 3.0f }, { "filterSustain", 0.4f }, { "filterRelease", 2.0f }, { "attack", 0.4f }, { "decay", 1.0f }, { "sustain", 0.9f }, { "release", 1.5f }, { "drift", 0.0f }, { "crushOn", 1 }, { "crushBits", 8.0f }, { "crushDrive", 12.0f }, { "crushMix", 0.3f }, { "ambientOn", 1 }, { "ambientMix", 0.35f }, { "reverbDecay", 4.0f }, { "gain", 0.5f } } },
        { "Digital Bell Pad", "Pad", { { "wave", 0 }, { "osc1Level", 0.7f }, { "complexOn", 1 }, { "modWave", 0 }, { "modRatio", 3.5f }, { "fmIndex", 0.2f }, { "mtxSrc1", 3 }, { "mtxDst1", 12 }, { "mtxAmt1", 0.6f }, { "filterAttack", 0.001f }, { "filterDecay", 0.8f }, { "filterSustain", 0.0f }, { "osc2Wave", 2 }, { "osc2Fine", 7.0f }, { "osc2Level", 0.6f }, { "attack", 0.003f }, { "decay", 2.0f }, { "sustain", 0.8f }, { "release", 2.5f }, { "filterMode", 1 }, { "cutoff", 9000.0f }, { "chorusOn", 1 }, { "chorusMode", 1 }, { "chorusMix", 0.5f }, { "ambientOn", 1 }, { "ambientMix", 0.4f }, { "reverbDecay", 4.0f }, { "gain", 0.31f } } },
        { "Mono Bass 101", "Bass", { { "voiceMode", 1 }, { "wave", 2 }, { "osc2Wave", 3 }, { "osc2Level", 0.6f }, { "subLevel", 0.7f }, { "envCurve", 1 }, { "attack", 0.001f }, { "decay", 0.3f }, { "sustain", 0.6f }, { "release", 0.05f }, { "cutoff", 350.0f }, { "resonance", 0.3f }, { "filterEnv", 0.45f }, { "filterAttack", 0.001f }, { "filterDecay", 0.2f }, { "filterSustain", 0.0f }, { "filterKey", 0.5f }, { "gain", 0.55f } } },
        { "Orchestra Hit 8-Bit", "FX", { { "wave", 2 }, { "unisonVoices", 3 }, { "unisonDetune", 15.0f }, { "unisonSpread", 0.7f }, { "osc2Wave", 2 }, { "osc2Octave", 1 }, { "osc2Semi", 4 }, { "osc2Level", 0.5f }, { "subLevel", 0.5f }, { "noiseLevel", 0.2f }, { "attack", 0.001f }, { "decay", 0.45f }, { "sustain", 0.0f }, { "release", 0.25f }, { "filterMode", 1 }, { "cutoff", 2500.0f }, { "filterEnv", 0.3f }, { "filterAttack", 0.001f }, { "filterDecay", 0.15f }, { "filterSustain", 0.0f }, { "crushOn", 1 }, { "crushBits", 8.0f }, { "crushRate", 16000.0f }, { "crushMix", 1.0f }, { "ambientOn", 1 }, { "ambientMix", 0.3f }, { "reverbDecay", 2.0f }, { "gain", 0.5f } } },
        { "Eight-Voice Sync Lead", "Lead", { { "voiceMode", 2 }, { "glide", 30.0f }, { "wave", 3 }, { "osc1Level", 0.3f }, { "osc2Wave", 2 }, { "osc2Sync", 1 }, { "osc2Octave", 1 }, { "osc2Level", 1.0f }, { "mtxSrc1", 3 }, { "mtxDst1", 2 }, { "mtxAmt1", 0.5f }, { "filterMode", 0 }, { "cutoff", 5000.0f }, { "resonance", 0.1f }, { "filterEnv", 0.15f }, { "filterAttack", 0.001f }, { "filterDecay", 0.6f }, { "filterSustain", 0.2f }, { "attack", 0.003f }, { "sustain", 1.0f }, { "release", 0.2f }, { "echoOn", 1 }, { "echoSync", 2 }, { "echoFeedback", 0.3f }, { "echoMix", 0.2f }, { "gain", 0.45f } } },
        { "Phase Distortion Reso Lead", "Lead", { { "osc1Level", 0.0f }, { "wtLevel", 1.0f }, { "wtTable", 3 }, { "wtPosition", 0.05f }, { "mtxSrc1", 3 }, { "mtxDst1", 14 }, { "mtxAmt1", 0.8f }, { "mtxSrc2", 6 }, { "mtxDst2", 14 }, { "mtxAmt2", -0.2f }, { "filterAttack", 0.005f }, { "filterDecay", 0.6f }, { "filterSustain", 0.3f }, { "filterRelease", 0.3f }, { "attack", 0.003f }, { "decay", 0.5f }, { "sustain", 0.8f }, { "release", 0.3f }, { "drift", 0.0f }, { "echoOn", 1 }, { "echoSync", 3 }, { "echoFeedback", 0.25f }, { "echoMix", 0.15f }, { "gain", 0.45f } } },
        // Años 90
        { "Gated Supersaw Pad", "Pad", { { "wave", 2 }, { "unisonVoices", 7 }, { "unisonDetune", 20.0f }, { "unisonSpread", 1.0f }, { "attack", 0.3f }, { "decay", 0.5f }, { "sustain", 1.0f }, { "release", 1.0f }, { "filterMode", 1 }, { "cutoff", 4000.0f }, { "lfoOn", 1 }, { "lfoTarget", 1 }, { "lfoShape", 3 }, { "lfoSync", 8 }, { "lfoDepth", 1.0f }, { "arpBpm", 138.0f }, { "ambientOn", 1 }, { "ambientMix", 0.3f }, { "reverbDecay", 3.0f }, { "gain", 0.4f } } },
        { "Detuned Reese Bass", "Bass", { { "voiceMode", 1 }, { "wave", 2 }, { "osc2Wave", 2 }, { "osc2Fine", 18.0f }, { "osc2Level", 1.0f }, { "subLevel", 0.3f }, { "attack", 0.002f }, { "decay", 0.3f }, { "sustain", 1.0f }, { "release", 0.15f }, { "cutoff", 500.0f }, { "resonance", 0.15f }, { "filterDrive", 0.3f }, { "lfoOn", 1 }, { "lfoTarget", 2 }, { "lfoRate", 0.2f }, { "lfoDepth", 0.15f }, { "chorusOn", 1 }, { "chorusMode", 0 }, { "chorusMix", 0.4f }, { "gain", 0.5f } } },
        { "Trance Supersaw Pluck", "Pluck", { { "wave", 2 }, { "unisonVoices", 7 }, { "unisonDetune", 15.0f }, { "unisonSpread", 1.0f }, { "attack", 0.001f }, { "decay", 0.35f }, { "sustain", 0.0f }, { "release", 0.3f }, { "cutoff", 300.0f }, { "resonance", 0.25f }, { "filterEnv", 0.6f }, { "filterAttack", 0.001f }, { "filterDecay", 0.18f }, { "filterSustain", 0.0f }, { "filterRelease", 0.2f }, { "filterKey", 0.5f }, { "echoOn", 1 }, { "echoSync", 2 }, { "echoFeedback", 0.35f }, { "echoMix", 0.25f }, { "ambientOn", 1 }, { "ambientMix", 0.35f }, { "reverbDecay", 3.0f }, { "gain", 0.81f } } },
        { "808 Sub Bass", "Bass", { { "voiceMode", 1 }, { "glide", 60.0f }, { "wave", 0 }, { "attack", 0.001f }, { "decay", 3.0f }, { "sustain", 0.0f }, { "release", 0.5f }, { "pitchEnvAmount", 1.0f }, { "pitchEnvDecay", 133.0f }, { "filterOn", 0 }, { "tubeOn", 1 }, { "tubeDrive", 9.0f }, { "gain", 0.44f } } },
        { "Vector Wave Pad", "Pad", { { "wave", 2 }, { "osc1Level", 0.5f }, { "osc2Wave", 3 }, { "osc2Octave", 1 }, { "osc2Level", 0.5f }, { "wtLevel", 0.5f }, { "wtTable", 4 }, { "mtxSrc1", 1 }, { "mtxDst1", 7 }, { "mtxAmt1", -0.5f }, { "mtxSrc2", 1 }, { "mtxDst2", 8 }, { "mtxAmt2", 0.5f }, { "lfoRate", 0.15f }, { "lfoShape", 1 }, { "lfo2On", 1 }, { "lfo2Target", 5 }, { "lfo2Shape", 1 }, { "lfo2Rate", 0.09f }, { "lfo2Depth", 0.5f }, { "filterMode", 1 }, { "cutoff", 5000.0f }, { "attack", 1.0f }, { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 2.5f }, { "chorusOn", 1 }, { "chorusMode", 1 }, { "chorusMix", 0.3f }, { "ambientOn", 1 }, { "ambientMix", 0.4f }, { "reverbDecay", 5.0f }, { "gain", 0.31f } } },
        { "House Organ Bass", "Bass", { { "voiceMode", 1 }, { "wave", 1 }, { "osc1Level", 0.4f }, { "osc2Wave", 3 }, { "osc2Octave", 2 }, { "osc2Semi", 7 }, { "osc2Level", 0.6f }, { "attack", 0.001f }, { "decay", 1.9f }, { "sustain", 0.0f }, { "release", 0.2f }, { "filterMode", 1 }, { "cutoff", 331.0f }, { "filterEnv", 0.21f }, { "filterAttack", 0.001f }, { "filterDecay", 1.8f }, { "filterSustain", 0.0f }, { "filterRelease", 0.2f }, { "echoOn", 1 }, { "echoSync", 3 }, { "echoFeedback", 0.2f }, { "echoMix", 0.25f }, { "gain", 0.79f } } },
        { "Gabber Kick", "Drums", { { "wave", 1 }, { "attack", 0.001f }, { "decay", 0.35f }, { "sustain", 0.0f }, { "release", 0.2f }, { "pitchEnvAmount", 36.0f }, { "pitchEnvDecay", 25.0f }, { "filterOn", 0 }, { "voiceSatMode", 2 }, { "voiceSatDrive", 24.0f }, { "tubeOn", 1 }, { "tubeDrive", 18.0f }, { "gain", 0.34f } } },
        { "Dub Techno Chord", "Keys", { { "wave", 2 }, { "osc2Wave", 3 }, { "osc2Fine", 6.0f }, { "osc2Level", 0.5f }, { "attack", 0.002f }, { "decay", 0.3f }, { "sustain", 0.0f }, { "release", 0.3f }, { "filterMode", 1 }, { "cutoff", 600.0f }, { "resonance", 0.3f }, { "filterEnv", 0.35f }, { "filterAttack", 0.001f }, { "filterDecay", 0.2f }, { "filterSustain", 0.0f }, { "crushOn", 1 }, { "crushBits", 6.0f }, { "crushMix", 0.3f }, { "echoOn", 1 }, { "echoSync", 2 }, { "echoFeedback", 0.7f }, { "echoMix", 0.4f }, { "echoTone", 1800.0f }, { "echoWow", 0.4f }, { "springOn", 1 }, { "springMix", 0.35f }, { "springDecay", 3.0f }, { "gain", 0.5f } } },
        // FM 6-Op, algoritmo 5: par 1:4 (moduladora con caída rápida) + portadora OP3 a ×4 = 1er sobretono de la lámina, afinado a
        // 2 octavas (dato de la literatura de marimbas; no verificado en esta sesión). Diseño propio
        { "FM Marimba", "Pluck", { { "engine", 6 }, { "fmAlgorithm", 5 },
                          { "fm1Coarse", 1 }, { "fm1Level", 99 }, { "fm1Vel", 3 }, { "fm1RateScale", 3 }, { "fm1R2", 55 }, { "fm1L2", 0 }, { "fm1R3", 55 }, { "fm1L3", 0 }, { "fm1R4", 55 },
                          { "fm2Coarse", 4 }, { "fm2Level", 70 }, { "fm2Vel", 5 }, { "fm2RateScale", 3 }, { "fm2R2", 80 }, { "fm2L2", 0 }, { "fm2R3", 80 }, { "fm2L3", 0 }, { "fm2R4", 80 },
                          { "fm3Coarse", 4 }, { "fm3Level", 60 }, { "fm3RateScale", 3 }, { "fm3R2", 75 }, { "fm3L2", 0 }, { "fm3R3", 75 }, { "fm3L3", 0 }, { "fm3R4", 75 } } },
        // MT-32 (Munt, ROM del usuario): parte 1 con el programa 1 del banco de fábrica (el piano acústico con el que arranca)
        { "MT-32 Piano 1", "Keys", { { "engine", 14 }, { "mt32Mode", 0 }, { "mt32Program", 1 } } },
        // OPLL: instrumento 7 "Trumpet" de la ROM del YM2413 (volcado del silicio, Nuked-OPLL) con SUS
        { "OPLL Trumpet", "Brass", { { "engine", 13 }, { "oplModel", 2 }, { "opllInstrument", 7 }, { "opllSustain", 1 } } },
        // Stab de metales polifónico: 2 sierras + sub-octava del Osc 2, filtro que abre en 40 ms. Diseño propio
        { "Poly Brass Stab", "Brass", { { "wave", 2 }, { "unisonVoices", 2 }, { "unisonDetune", 8.0f }, { "osc2Wave", 2 }, { "osc2Octave", -1 }, { "osc2Level", 0.4f },
                          { "attack", 0.02f }, { "decay", 0.35f }, { "sustain", 0.55f }, { "release", 0.25f }, { "cutoff", 700.0f }, { "resonance", 0.1f },
                          { "filterEnv", 0.45f }, { "filterAttack", 0.04f }, { "filterDecay", 0.3f }, { "filterSustain", 0.3f }, { "velToFilter", 0.4f },
                          { "ambientOn", 1 }, { "ambientMix", 0.2f } } },
        // Lead mono legato: sierra + cuadrada a -7 ct + sub, drive de filtro, vibrato por aftertouch, eco 1/8D. Diseño propio
        { "Fat Mono Lead", "Lead", { { "voiceMode", 2 }, { "glide", 70.0f }, { "wave", 2 }, { "osc2Wave", 3 }, { "osc2Fine", -7.0f }, { "osc2Level", 0.8f },
                          { "subLevel", 0.3f }, { "attack", 0.003f }, { "decay", 0.4f }, { "sustain", 0.85f }, { "release", 0.15f }, { "cutoff", 1400.0f },
                          { "resonance", 0.35f }, { "filterDrive", 0.4f }, { "filterEnv", 0.3f }, { "filterDecay", 0.5f }, { "filterSustain", 0.4f },
                          { "filterKey", 0.5f }, { "lfoOn", 1 }, { "lfoRate", 5.2f }, { "lfoDepth", 0.04f }, { "atVibrato", 0.5f },
                          { "echoOn", 1 }, { "echoSync", 2 }, { "echoFeedback", 0.3f }, { "echoMix", 0.2f } } },
        // Patrones de RhythmPatterns.h sin preset: Funk (4), Reggae (14), Shuffle (18). Tempos estimados
        { "808 Funk", "Drums", { { "engine", 5 }, { "drumKit", 0 }, { "drumPattern", 4 }, { "drumTempo", 104.0f }, { "drumSwing", 0.1f } } },
        { "808 Dub Reggae", "Drums", { { "engine", 5 }, { "drumKit", 0 }, { "drumPattern", 14 }, { "drumTempo", 76.0f }, { "drumTune", -2.0f },
                          { "springOn", 1 }, { "springMix", 0.35f }, { "springDecay", 3.5f },
                          { "echoOn", 1 }, { "echoSync", 2 }, { "echoFeedback", 0.45f }, { "echoMix", 0.2f } } },
        { "CR-78 Shuffle", "Drums", { { "engine", 5 }, { "drumKit", 1 }, { "drumPattern", 18 }, { "drumTempo", 100.0f } } },
        // Tom electrónico de pad hexagonal: triangular + ruido (arquitectura del módulo SDS-V: syntherjack.net/?p=89) con caída de
        // tono de +12 st, τ 120 ms. Tocar C2-C3. Cifras propias
        { "Hex Pad Tom", "Drums", { { "wave", 1 }, { "noiseLevel", 0.25f }, { "attack", 0.001f }, { "decay", 0.6f }, { "sustain", 0.0f }, { "release", 0.6f },
                          { "pitchEnvAmount", 12.0f }, { "pitchEnvDecay", 120.0f }, { "cutoff", 3000.0f }, { "filterEnv", 0.3f }, { "filterDecay", 0.2f },
                          { "ambientOn", 1 }, { "ambientMix", 0.2f }, { "reverbDecay", 1.2f } } },
        // Subida de ruido: BP de 300 Hz a 300·2^4,5 ≈ 6,8 kHz en 4 s (filterEnv 0,9 = +4,5 oct). Diseño propio
        { "Noise Riser", "FX", { { "osc1Level", 0.0f }, { "noiseLevel", 1.0f }, { "filterMode", 2 }, { "cutoff", 300.0f }, { "resonance", 0.5f },
                          { "filterEnv", 0.9f }, { "filterAttack", 4.0f }, { "filterDecay", 1.0f }, { "filterSustain", 1.0f }, { "filterRelease", 1.0f },
                          { "attack", 3.0f }, { "sustain", 1.0f }, { "release", 1.5f }, { "ambientOn", 1 }, { "ambientMix", 0.4f }, { "reverbDecay", 4.0f } } },
        // Caída de sub: seno de +24 st a la nota con τ 600 ms (tocar C1). Diseño propio
        { "Sub Drop", "FX", { { "wave", 0 }, { "attack", 0.001f }, { "decay", 2.0f }, { "sustain", 0.0f }, { "release", 1.0f }, { "pitchEnvAmount", 24.0f },
                          { "pitchEnvDecay", 600.0f }, { "filterOn", 0 }, { "tubeOn", 1 }, { "tubeDrive", 6.0f } } },
        // Secuencia de 16 pasos en menor (0 12 7 3 / 0 12 10 7 / 0 12 7 3 / 5 12 10 7) con barrido de filtro de 20 s (LFO 0,05 Hz).
        // Pulsar una nota (Latch). Diseño propio
        { "Berlin Sequence", "Sequence", { { "wave", 2 }, { "attack", 0.002f }, { "decay", 0.2f }, { "sustain", 0.2f }, { "release", 0.15f },
                          { "cutoff", 700.0f }, { "resonance", 0.55f }, { "filterEnv", 0.45f }, { "filterDecay", 0.18f }, { "filterSustain", 0.0f },
                          { "lfoOn", 1 }, { "lfoTarget", 2 }, { "lfoRate", 0.05f }, { "lfoDepth", 0.4f },
                          { "seqOn", 1 }, { "seqSteps", 16 }, { "seqRate", 3 }, { "seqGate", 0.5f }, { "seqLatch", 1 }, { "arpBpm", 118.0f },
                          { "stepNote2", 12 }, { "stepNote3", 7 }, { "stepNote4", 3 }, { "stepNote6", 12 }, { "stepNote7", 10 }, { "stepNote8", 7 },
                          { "stepNote10", 12 }, { "stepNote11", 7 }, { "stepNote12", 3 }, { "stepNote13", 5 }, { "stepNote14", 12 }, { "stepNote15", 10 },
                          { "stepNote16", 7 }, { "echoOn", 1 }, { "echoSync", 2 }, { "echoFeedback", 0.4f }, { "echoMix", 0.3f },
                          { "ambientOn", 1 }, { "ambientMix", 0.25f }, { "reverbDecay", 4.0f } } },
        // ANS: partitura de fábrica "72-Step Scale" (índice 5), la única sin preset
        { "ANS 72-Step Scale", "Machines", { { "engine", 9 }, { "ansScore", 5 }, { "ansSpeed", 12.0f }, { "ansEmulsion", 0.3f } } },
        // OPL (YM3812 / YMF262): instrumentos diseñados para KoniSynth con los registros del chip (no son bancos de juegos)
        { "OPL2 E.Piano", "Keys", { { "engine", 13 }, { "oplModel", 0 }, { "oplAlgorithm", 0 }, { "oplFeedback", 2 },
                          { "opl1Mult", 1 }, { "opl1Tl", 30 }, { "opl1Ar", 15 }, { "opl1Dr", 5 }, { "opl1Sl", 8 }, { "opl1Rr", 5 }, { "opl1Egt", 0 }, { "opl1Wave", 1 },
                          { "opl2Mult", 1 }, { "opl2Tl", 0 }, { "opl2Ar", 15 }, { "opl2Dr", 3 }, { "opl2Sl", 12 }, { "opl2Rr", 6 }, { "opl2Egt", 0 } } },
        { "OPL2 Bass", "Bass", { { "engine", 13 }, { "oplModel", 0 }, { "oplAlgorithm", 0 }, { "oplFeedback", 5 },
                          { "opl1Mult", 1 }, { "opl1Tl", 22 }, { "opl1Ar", 15 }, { "opl1Dr", 6 }, { "opl1Sl", 4 }, { "opl1Rr", 8 },
                          { "opl2Mult", 1 }, { "opl2Tl", 0 }, { "opl2Ar", 15 }, { "opl2Dr", 2 }, { "opl2Sl", 3 }, { "opl2Rr", 8 } } },
        { "OPL2 Organ", "Keys", { { "engine", 13 }, { "oplModel", 0 }, { "oplAlgorithm", 1 }, { "oplFeedback", 0 },
                          { "opl1Mult", 2 }, { "opl1Tl", 8 }, { "opl1Vib", 1 }, { "opl1Rr", 9 },
                          { "opl2Mult", 1 }, { "opl2Tl", 0 }, { "opl2Vib", 1 }, { "opl2Rr", 9 } } },
        { "OPL3 4-Op Brass", "Brass", { { "engine", 13 }, { "oplModel", 1 }, { "oplFourOp", 1 }, { "oplAlgorithm", 1 }, { "oplFeedback", 4 },
                          { "opl1Mult", 1 }, { "opl1Tl", 24 }, { "opl1Ar", 10 }, { "opl1Dr", 2 }, { "opl1Sl", 3 },
                          { "opl2Mult", 1 }, { "opl2Tl", 0 }, { "opl2Ar", 9 }, { "opl2Dr", 1 }, { "opl2Sl", 2 }, { "opl2Rr", 6 },
                          { "opl3Mult", 2 }, { "opl3Tl", 28 }, { "opl3Ar", 11 }, { "opl3Dr", 3 }, { "opl3Sl", 4 },
                          { "opl4Mult", 1 }, { "opl4Tl", 4 }, { "opl4Ar", 9 }, { "opl4Dr", 1 }, { "opl4Sl", 2 }, { "opl4Rr", 6 }, { "opl4Wave", 0 } } },
        // OPLL / VRC7: instrumentos de la ROM de cada chip (volcados del silicio; Nuked-OPLL)
        { "OPLL Piano", "Keys", { { "engine", 13 }, { "oplModel", 2 }, { "opllInstrument", 3 } } },
        { "OPLL Synth Bass + Drums", "Bass", { { "engine", 13 }, { "oplModel", 2 }, { "opllInstrument", 13 }, { "opllRhythm", 1 } } },
        { "VRC7 Patch 1", "8-Bit", { { "engine", 13 }, { "oplModel", 3 }, { "opllInstrument", 1 } } },
        // N64: samples propios de KoniSynth a 22 kHz en VADPCM; efectos de libultra con sus ajustes de fábrica. Big Room es el
        // efecto por defecto del reproductor de música de libultra (manual de N64, "Audio Effects")
        { "N64 Big Room Choir", "Pad", { { "engine", 12 }, { "n64Source", 4 }, { "n64Attack", 40.0f }, { "n64Release", 700.0f },
                          { "n64Fx", 2 }, { "n64FxMix", 60 } } },
        { "N64 Small Room Strings", "Strings", { { "engine", 12 }, { "n64Source", 3 }, { "n64Attack", 120.0f }, { "n64Release", 500.0f },
                          { "n64Fx", 1 }, { "n64FxMix", 50 }, { "n64Spread", 0.4f } } },
        { "N64 Echo Lead", "Lead", { { "engine", 12 }, { "n64Source", 1 }, { "n64SourceRate", 2 }, { "n64Decay", 400.0f }, { "n64DecayVolume", 90 },
                          { "n64Release", 300.0f }, { "n64Fx", 3 }, { "n64FxMix", 50 } } },
        { "N64 Chorus Square", "Lead", { { "engine", 12 }, { "n64Source", 0 }, { "n64Release", 250.0f }, { "n64Fx", 4 }, { "n64FxMix", 70 } } },
        { "N64 Flange Bass", "Bass", { { "engine", 12 }, { "n64Source", 5 }, { "n64SourceRate", 1 }, { "n64Decay", 600.0f }, { "n64DecayVolume", 70 },
                          { "n64Release", 150.0f }, { "n64Fx", 5 }, { "n64FxMix", 45 } } },
        { "SNES Noise Wind", "Machines", { { "engine", 11 }, { "snesNoise", 1 }, { "snesNoiseRate", 24 }, { "snesAttack", 6 }, { "snesEcho", 1 },
                          { "snesEdl", 7 }, { "snesEfb", 96 }, { "snesEvol", 56 }, { "snesFir", 1 } } },
        // Hardbass / donk: goma elástica que se suelta y frena contra una caja de madera hueca (descripción del usuario), ajustado
        // por búsqueda en rejilla contra dos donks reales de Freesound (Chameon "Donk Bass (F)" y phoenixdk "donk.wav", CC0;
        // scratchpad/donk_fit.py, media de 4 renders, Sol2): objetivo -20 dB a 41 ms / -40 dB a 310 ms / centroide 295 Hz; preset
        // 29 / 359 ms / 325 Hz; octavas de 250 y 500 Hz a -2,0 / -6,7 dB (referencia -2,1 / -7,0); sobra algo de fundamental
        // (125 Hz: -9,7 frente a -14,9 dB). Tono que cae desde +19 st (τ 30 ms), clic de cuerda pulsada, caja = formante "o" -4 st
        { "Hardbass Donk", "Bass", { { "wave", 1 }, { "complexOn", 1 }, { "modWave", 0 }, { "modRatio", 2.0f }, { "fmIndex", 1.2f }, { "pitchEnvAmount", 19.0f }, { "pitchEnvDecay", 30.0f }, { "attack", 0.0002f }, { "decay", 0.065f }, { "sustain", 0.08f }, { "release", 0.3f }, { "filterOn", 1 }, { "filterMode", 1 }, { "cutoff", 750.0f }, { "resonance", 0.45f }, { "filterEnv", 0.7f }, { "filterAttack", 0.0002f }, { "filterDecay", 0.08f }, { "filterSustain", 0.0f }, { "stringLevel", 0.35f }, { "stringDecay", 0.1f }, { "stringBright", 0.85f }, { "stringPick", 0.08f }, { "formantOn", 1 }, { "formantVoice", 0 }, { "formantVowel", 3.0f }, { "formantShift", -4.0f }, { "formantMix", 0.7f }, { "tubeOn", 1 }, { "tubeDrive", 6.0f } } },
        // Variante más "eléctrica" del donk: sin caída de tono y con un brillo que se cierra (índice de FM por la envolvente de
        // filtro: 1,37 + 5 -> 1,37 en 0,4 s), formante más suave. Ajustada contra un donk de hardbass hecho en Serum (YouTube,
        // "serum hard bass donk", solo para análisis): serie de armónicos impares (FM 2:1) con el armónico central por potencia
        // a 8,2 / 6,8 / 5,3 en 0-60 / 60-160 / 160-300 ms (referencia 8,2 / 7,0 / 4,7; el preset anterior 6,4 / 4,7 / 4,8) y
        // -20 dB a 290 ms (referencia 240-335 ms). Elegida de oído frente a otras dos variantes
        { "Electric Donk Bass", "Bass", { { "wave", 1 }, { "complexOn", 1 }, { "modWave", 0 }, { "modRatio", 2.0f }, { "fmIndex", 1.37f }, { "mtxSrc1", 3 }, { "mtxDst1", 12 }, { "mtxAmt1", 1.0f }, { "attack", 0.0002f }, { "decay", 0.117f }, { "sustain", 0.074f }, { "release", 0.3f }, { "filterOn", 1 }, { "filterMode", 1 }, { "cutoff", 500.0f }, { "resonance", 0.45f }, { "filterEnv", 0.28f }, { "filterAttack", 0.0002f }, { "filterDecay", 0.4f }, { "filterSustain", 0.0f }, { "stringLevel", 0.35f }, { "stringDecay", 0.1f }, { "stringBright", 0.85f }, { "stringPick", 0.08f }, { "formantOn", 1 }, { "formantVoice", 0 }, { "formantVowel", 3.0f }, { "formantShift", -4.0f }, { "formantMix", 0.45f }, { "tubeOn", 1 }, { "tubeDrive", 9.0f }, { "gain", 0.6f } } },
        // El mismo donk a contratiempo: pasos 3, 7, 11 y 15 (Producer School; Bonedo), 155 BPM (hardbass: 150-175)
        { "Hardbass Donk Bounce", "Sequence", { { "wave", 1 }, { "complexOn", 1 }, { "modWave", 0 }, { "modRatio", 2.0f }, { "fmIndex", 1.2f }, { "pitchEnvAmount", 19.0f }, { "pitchEnvDecay", 30.0f }, { "attack", 0.0002f }, { "decay", 0.065f }, { "sustain", 0.08f }, { "release", 0.3f }, { "filterOn", 1 }, { "filterMode", 1 }, { "cutoff", 750.0f }, { "resonance", 0.45f }, { "filterEnv", 0.7f }, { "filterAttack", 0.0002f }, { "filterDecay", 0.08f }, { "filterSustain", 0.0f }, { "stringLevel", 0.35f }, { "stringDecay", 0.1f }, { "stringBright", 0.85f }, { "stringPick", 0.08f }, { "formantOn", 1 }, { "formantVoice", 0 }, { "formantVowel", 3.0f }, { "formantShift", -4.0f }, { "formantMix", 0.7f }, { "tubeOn", 1 }, { "tubeDrive", 6.0f }, { "seqOn", 1 }, { "seqSteps", 16 }, { "seqRate", 3 }, { "seqGate", 0.9f }, { "arpBpm", 155.0f }, { "stepOn1", 0 }, { "stepOn2", 0 }, { "stepOn3", 1 }, { "stepOn4", 0 }, { "stepOn5", 0 }, { "stepOn6", 0 }, { "stepOn7", 1 }, { "stepOn8", 0 }, { "stepOn9", 0 }, { "stepOn10", 0 }, { "stepOn11", 1 }, { "stepOn12", 0 }, { "stepOn13", 0 }, { "stepOn14", 0 }, { "stepOn15", 1 }, { "stepOn16", 0 } } },
        { "Hardbass Kit", "Drums", { { "engine", 5 }, { "drumKit", 0 }, { "drumPattern", 29 }, { "drumTempo", 155.0f }, { "drumTone", 0.7f } } },
        // Kit TR-808 sin patrón (lo carga el selector de motor): bombo de decay largo, su rasgo más conocido (Wikipedia
        // "Roland TR-808"; MusicRadar)
        { "808 Kit", "Drums", { { "engine", 5 }, { "drumKit", 0 }, { "drumDecay", 0.4f } } },
        // CR-78, preset Disco a 96 BPM (Phil Collins, "In the Air Tonight": Wikipedia "Roland CR-78" y "In the Air Tonight"; Reverb)
        { "CR-78 Disco", "Drums", { { "engine", 5 }, { "drumKit", 1 }, { "drumPattern", 3 }, { "drumTempo", 96.0f } } },
        // CR-78, Cha-Cha + Rumba a la vez (Blondie, "Heart of Glass": Roland Cloud; happymag). Tempo no publicado: el por defecto
        { "CR-78 Cha-Cha + Rumba", "Drums", { { "engine", 5 }, { "drumKit", 1 }, { "drumPattern", 10 }, { "drumPattern2", 22 } } },
        // Hoover de los 90: sierras desafinadas ±40 cents, una octava abajo, entra alta y cae (MusicRadar; musicproductionwiki)
        { "Hoover 90s", "Lead", { { "wave", 2 }, { "unisonVoices", 5 }, { "unisonDetune", 40.0f }, { "osc2Wave", 2 }, { "osc2Octave", -1 },
                          { "osc2Level", 0.7f }, { "pitchEnvAmount", 4.0f }, { "pitchEnvDecay", 150.0f }, { "chorusOn", 1 }, { "chorusMode", 2 },
                          { "chorusDepth", 0.8f } } },
        // Polivoks: bajo de 2 sierras a 16', VCO2 +0,1 st, mezclador al 40-60 %, resonancia 2-3 de 10 (monosounds)
        { "Polivoks Bass", "Bass", { { "engine", 8 }, { "pvRange1", 1 }, { "pvRange2", 1 }, { "pvDetune", 0.1f }, { "pvMix1", 0.55f },
                          { "pvMix2", 0.55f }, { "pvReso", 0.25f }, { "pvCutoff", 600.0f }, { "pvEnvAmount", 2500.0f }, { "pvFDecay", 0.25f },
                          { "pvFSustain", 0.1f } } },
        { "Polivoks Scream", "Lead", { { "engine", 8 }, { "pvWave1", 2 }, { "pvFm", 0.35f }, { "pvRange2", 1 }, { "pvMix1", 0.9f },
                          { "pvMix2", 0.6f }, { "pvReso", 0.85f }, { "pvCutoff", 1800.0f }, { "pvFilterMode", 1 }, { "pvGlide", 0.08f } } },
        { "Polivoks Duo", "Lead", { { "engine", 8 }, { "pvVoice", 1 }, { "pvWave1", 3 }, { "pvWave2", 1 }, { "pvReso", 0.4f }, { "pvCutoff", 2500.0f } } },
        { "Polivoks Repeat", "Sequence", { { "engine", 8 }, { "pvFLoop", 1 }, { "pvAAttack", 0.002f }, { "pvFAttack", 0.002f },
                          { "pvFDecay", 0.12f }, { "pvReso", 0.6f }, { "pvCutoff", 400.0f }, { "pvEnvAmount", 4000.0f } } },
        // LFO S&H sobre el filtro (formas del LFO: Wikipedia "Polivoks", sequencer.de)
        { "Polivoks S&H Filter", "Sequence", { { "engine", 8 }, { "pvLfoWave", 3 }, { "pvLfoRate", 8.0f }, { "pvLfoVcf", 0.5f }, { "pvReso", 0.6f } } },
        // ANS: partituras de fábrica (composición propia inspirada en sus usos: Artemiev, Coil)
        { "ANS Cluster", "Machines", { { "engine", 9 }, { "ansScore", 0 }, { "ansSpeed", 12.0f } } },
        { "ANS Glissandi", "Machines", { { "engine", 9 }, { "ansScore", 1 }, { "ansSpeed", 24.0f }, { "ambientOn", 1 }, { "ambientMix", 0.3f } } },
        { "ANS Drone", "Machines", { { "engine", 9 }, { "ansScore", 2 }, { "ansSpeed", 6.0f }, { "ansHold", 1 } } },
        { "ANS Bells", "Machines", { { "engine", 9 }, { "ansScore", 3 }, { "ansSpeed", 24.0f } } },
        { "ANS Noise Bands", "Machines", { { "engine", 9 }, { "ansScore", 4 }, { "ansSpeed", 18.0f } } },
        // Efectos nuevos
        { "Leslie Organ", "Keys", { { "engine", 6 }, { "fmMode", 0 }, { "fmAlgorithm", 32 }, { "fm1Level", 99 }, { "fm2Coarse", 2 }, { "fm2Level", 90 },
                          { "fm3Coarse", 3 }, { "fm3Level", 84 }, { "fm4Coarse", 4 }, { "fm4Level", 80 }, { "leslieOn", 1 }, { "leslieSpeed", 1 } } },
        { "Spring Dub Stab", "Keys", { { "wave", 3 }, { "cutoff", 1500.0f }, { "decay", 0.25f }, { "sustain", 0.0f }, { "springOn", 1 },
                          { "springMix", 0.5f }, { "springDecay", 3.5f } } },
        { "Mistress Pad", "Pad", { { "wave", 2 }, { "unisonVoices", 3 }, { "attack", 0.8f }, { "release", 1.5f }, { "flangerOn", 1 },
                          { "flangerRate", 0.15f }, { "flangerColor", 0.7f } } },
        { "Underwater Pad", "FX", { { "wave", 2 }, { "unisonVoices", 3 }, { "attack", 0.6f }, { "release", 1.2f }, { "mediaOn", 1 }, { "mediaType", 5 },
                          { "mediaAge", 0.5f }, { "mediaNoise", 0.4f }, { "mediaWow", 0.6f }, { "mediaDrive", 0.1f } } },
        { "Bode Bells", "FX", { { "wave", 0 }, { "attack", 0.005f }, { "decay", 1.5f }, { "sustain", 0.0f }, { "fshiftOn", 1 }, { "fshiftHz", 377.0f } } },
        // Clave barroco: solo cuerda pulsada (púa dura cerca del extremo), envolvente abierta: la cuerda decae sola
        { "Baroque Harpsichord", "Keys", { { "osc1Level", 0.0f }, { "stringLevel", 1.0f }, { "stringDecay", 4.0f }, { "stringBright", 0.9f },
                          { "stringPick", 0.1f }, { "attack", 0.001f }, { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 0.25f },
                          { "cutoff", 12000.0f }, { "resonance", 0.0f },
                          { "ambientOn", 1 }, { "ambientSize", 0.7f }, { "ambientMix", 0.25f }, { "gain", 0.6f } } },
        // Secuencia arpegiada a semicorcheas en dos octavas con eco (inspirada en el synth-pop alemán y británico de 1978–81)
        { "Robot Arp", "Sequence", { { "wave", 3 }, { "attack", 0.002f }, { "decay", 0.18f }, { "sustain", 0.3f }, { "release", 0.1f },
                          { "cutoff", 1500.0f }, { "resonance", 0.4f }, { "filterEnv", 0.4f }, { "filterDecay", 0.2f },
                          { "arpOn", 1 }, { "arpMode", 0 }, { "arpRate", 3 }, { "arpOctaves", 2 }, { "arpGate", 0.4f },
                          { "echoOn", 1 }, { "echoTime", 375.0f }, { "echoFeedback", 0.35f }, { "echoMix", 0.25f } } },
        // Vocoder: portadora de sierras desafinadas (rica en armónicos: las bandas tienen qué modular). Habla al micro y toca
        { "Robot Vocoder", "Vocoder", { { "wave", 2 }, { "attack", 0.005f }, { "sustain", 1.0f }, { "release", 0.15f },
                          { "unisonVoices", 3 }, { "unisonDetune", 8.0f }, { "cutoff", 9000.0f },
                          { "vocoderOn", 1 }, { "vocoderBank", 1 }, { "vocoderMix", 1.0f }, { "vocoderSibilance", 0.4f },
                          { "inputGain", 12.0f } } },
        // ---- Bass / Lead (investigación 2026-10-20). Valores de las "preset recipes" de Syntorial (remakes aproximados,
        // no volcados del panel original); instrumento original verificado aparte cuando ha sido posible.
        // Conversiones estimadas: cutoff % -> Hz con Hz ≈ 7,86·e^(0,0748·p) (calibrada con 2 pares de Vital; ±1 octava),
        // filterEnv = p·128/60, tiempos de nota a 120 BPM. Afinación de oído pendiente contra los temas originales.
        { "Blue Monday 88 Bass", "Bass", { { "wave", 2 }, { "osc1Level", 0.7f }, { "osc2Wave", 3 }, { "osc2Level", 0.3f }, { "unisonVoices", 4 }, { "unisonDetune", 11.0f }, { "unisonSpread", 1.0f },
                          { "attack", 0.001f }, { "decay", 0.25f }, { "sustain", 0.2f }, { "release", 0.16f }, { "cutoff", 481.0f }, { "filterKey", 0.5f }, { "filterEnv", 0.53f },
                          { "filterAttack", 0.001f }, { "filterDecay", 0.3f }, { "filterSustain", 0.0f }, { "filterRelease", 10.0f }, { "chorusOn", 1 }, { "chorusMix", 0.5f } } },
            // syntorial.com/preset-recipe/new-order-blue-monday-88-bass ; original 1983: Moog Source (Wikipedia)
        { "Everything Counts Bass", "Bass", { { "voiceMode", 1 }, { "wave", 2 }, { "unisonVoices", 4 }, { "unisonDetune", 12.0f }, { "unisonSpread", 1.0f }, { "attack", 0.02f },
                          { "sustain", 1.0f }, { "release", 0.02f }, { "cutoff", 228.0f }, { "resonance", 0.5f }, { "filterEnv", 0.53f }, { "filterAttack", 0.001f },
                          { "filterDecay", 0.35f }, { "filterSustain", 0.0f }, { "echoOn", 1 }, { "echoTime", 62.0f }, { "echoFeedback", 0.0f }, { "echoMix", 0.1f } } },
            // syntorial.com/preset-recipe/depeche-mode-everything-counts-bass (mono; delay 1/32 a 120 BPM supuesto)
        { "The Model Bass", "Bass", { { "voiceMode", 1 }, { "wave", 3 }, { "attack", 0.001f }, { "decay", 0.8f }, { "sustain", 0.2f }, { "release", 0.4f },
                          { "cutoff", 108.0f }, { "filterEnv", 0.11f }, { "filterAttack", 0.001f }, { "filterDecay", 0.9f }, { "filterSustain", 0.0f }, { "filterRelease", 10.0f } } },
            // syntorial.com/preset-recipe/kraftwerk-the-model-bass ; sinte original incierto (MusicRadar)
        { "Europe Endless Bass", "Bass", { { "voiceMode", 1 }, { "wave", 3 }, { "unisonVoices", 2 }, { "unisonDetune", 8.0f }, { "attack", 0.001f }, { "decay", 0.45f },
                          { "sustain", 0.0f }, { "release", 0.16f }, { "cutoff", 700.0f }, { "filterKey", 0.5f }, { "filterEnv", 0.64f }, { "filterAttack", 0.001f },
                          { "filterDecay", 0.01f }, { "filterSustain", 0.0f }, { "filterRelease", 0.25f }, { "tubeOn", 1 }, { "tubeDrive", 3.8f }, { "chorusOn", 1 }, { "chorusMix", 0.3f } } },
            // syntorial.com/preset-recipe/kraftwerk-europe-endless-bass ; Minimoog "likely"
        { "Around The World Bass", "Bass", { { "voiceMode", 1 }, { "glide", 30.0f }, { "wave", 3 }, { "attack", 0.001f }, { "sustain", 1.0f }, { "release", 0.02f },
                          { "cutoff", 228.0f } } },
            // syntorial.com/preset-recipe/daft-punk-around-the-world-bass (mono, portamento 30 ms) ; reverbmachine.com (sinte sin identificar)
        { "Acid 303 Bass", "Bass", { { "voiceMode", 2 }, { "glide", 60.0f }, { "wave", 2 }, { "attack", 0.001f }, { "sustain", 1.0f }, { "release", 0.01f },
                          { "cutoff", 1000.0f }, { "resonance", 0.5f }, { "filterEnv", 0.4f }, { "filterAttack", 0.001f }, { "filterDecay", 0.3f }, { "filterSustain", 0.0f },
                          { "tubeOn", 1 }, { "tubeDrive", 20.0f } } },
            // TB-303: LP 24 dB sin autooscilación (Wikipedia) -> resonancia < 0,91; valores por defecto de NT-303/Open303 (plugin, no
            // hardware), slide 60 ms ≈ Legato + Glide; distorsión casi al máximo como en "Da Funk" (reverbmachine.com). Falta accent.
        { "Numbers Lead", "Lead", { { "voiceMode", 1 }, { "wave", 2 }, { "osc1Level", 0.5f }, { "osc2Wave", 2 }, { "osc2Level", 0.5f }, { "osc2Fine", -4.0f },
                          { "attack", 0.01f }, { "decay", 0.13f }, { "sustain", 0.0f }, { "release", 0.12f }, { "cutoff", 481.0f }, { "filterKey", 0.5f }, { "filterEnv", 0.43f },
                          { "filterAttack", 0.03f }, { "filterSustain", 1.0f }, { "filterRelease", 10.0f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 0.5f },
                          { "lfoDepth", 0.5f }, { "echoOn", 1 }, { "echoTime", 250.0f }, { "echoFeedback", 0.5f }, { "echoMix", 0.45f } } },
            // syntorial.com/preset-recipe/kraftwerk-numbers-lead (2 sierras ±2 ct, LFO 1 st a 1 compás, delay 1/8 a 120 BPM supuesto)
        { "Just Cant Get Enough Lead", "Lead", { { "wave", 2 }, { "subLevel", 0.15f }, { "unisonVoices", 4 }, { "unisonDetune", 9.0f }, { "unisonSpread", 0.6f },
                          { "attack", 0.01f }, { "decay", 0.55f }, { "sustain", 0.0f }, { "release", 0.22f }, { "cutoff", 4537.0f },
                          { "ambientOn", 1 }, { "ambientSize", 0.25f }, { "ambientMix", 0.1f } } },
            // syntorial.com/preset-recipe/depeche-mode-just-cant-get-enough-lead ; original Roland Jupiter-4 (thomann.de/blog)
        { "New Life Lead", "Lead", { { "voiceMode", 2 }, { "wave", 2 }, { "osc1Level", 0.6f }, { "osc2Wave", 2 }, { "osc2Octave", 1 }, { "osc2Level", 0.4f },
                          { "unisonVoices", 4 }, { "unisonDetune", 8.0f }, { "attack", 0.12f }, { "sustain", 1.0f }, { "release", 1.7f }, { "filterMode", 2 },
                          { "cutoff", 1478.0f }, { "resonance", 0.4f }, { "ambientOn", 1 }, { "ambientSize", 0.8f }, { "ambientMix", 0.3f } } },
            // syntorial.com/preset-recipe/depeche-mode-new-life-lead (mono legato, BP, res 40 %)
        { "Never Let Me Down Lead", "Lead", { { "wave", 2 }, { "osc1Level", 0.71f }, { "osc2Wave", 2 }, { "osc2Level", 0.69f }, { "unisonVoices", 5 }, { "unisonDetune", 10.0f },
                          { "unisonSpread", 1.0f }, { "attack", 0.001f }, { "decay", 0.346f }, { "sustain", 0.71f }, { "release", 0.203f }, { "cutoff", 447.0f },
                          { "resonance", 0.66f }, { "filterEnv", 0.94f }, { "filterAttack", 0.001f }, { "filterDecay", 0.322f }, { "filterSustain", 0.21f },
                          { "filterRelease", 0.372f }, { "chorusOn", 1 }, { "chorusMix", 1.0f }, { "ambientOn", 1 }, { "ambientMix", 0.54f } } },
            // syntorial.com/preset-recipe/depeche-mode-never-let-me-down-again-lead ; original E-mu Emulator II (tocar 1 octava abajo;
            // faltan el morph de sync y el LFO solo en osc2)
        { "Oxygene IV Lead", "Lead", { { "wave", 3 }, { "pulseWidth", 0.3f }, { "unisonVoices", 3 }, { "unisonDetune", 11.0f }, { "unisonSpread", 0.5f },
                          { "attack", 0.001f }, { "sustain", 1.0f }, { "release", 1.59f }, { "filterMode", 3 }, { "cutoff", 2493.0f }, { "resonance", 0.76f },
                          { "filterEnv", 0.6f }, { "filterAttack", 0.001f }, { "filterDecay", 0.078f }, { "filterSustain", 0.0f }, { "filterRelease", 0.09f } } },
            // syntorial.com/preset-recipe/jean-michel-jarre-oxygene-4-lead (HP 12 dB 2493 Hz, res 76 %; falta flanger) ; equipo del álbum:
            // VCS3, Eminent 310, RMI, ARP 2600 (musicradar.com). PW 0,3 estimado ("medium pulse")
        { "Blade Runner Brass", "Brass", { { "voiceMode", 1 }, { "filterMode", 1 }, { "wave", 3 }, { "osc1Level", 0.6f }, { "osc2Wave", 3 }, { "osc2Octave", 1 }, { "osc2Fine", -6.0f },
                          { "osc2Level", 0.4f }, { "pwmDepth", 0.36f }, { "lfoOn", 1 }, { "lfoRate", 4.0f }, { "lfoDepth", 0.0f }, { "attack", 0.25f }, { "decay", 1.9f },
                          { "sustain", 0.4f }, { "release", 0.75f }, { "cutoff", 2147.0f }, { "resonance", 0.15f }, { "filterEnv", 0.32f }, { "filterAttack", 0.65f },
                          { "filterDecay", 10.0f }, { "filterSustain", 0.0f }, { "filterRelease", 10.0f }, { "ambientOn", 1 }, { "ambientSize", 0.9f }, { "ambientMix", 0.1f } } },
            // syntorial.com/preset-recipe/vangelis-blade-runner-brass ; reverbmachine.com/blog/vangelis-blade-runner-synth-sounds (CS-80).
            // Filtro LP 12 dB/oct del CS-80 (hollowsun.com/nostalgia/contributors/cs80). Faltan aftertouch y ribbon
        { "Da Funk Lead", "Lead", { { "wave", 3 }, { "osc2Wave", 3 }, { "osc2Semi", 5 }, { "osc2Level", 0.8f }, { "attack", 0.001f }, { "sustain", 1.0f },
                          { "release", 0.05f }, { "filterMode", 2 }, { "cutoff", 228.0f }, { "resonance", 0.65f }, { "filterKey", 0.5f }, { "filterEnv", 0.64f },
                          { "filterAttack", 0.09f }, { "filterDecay", 4.0f }, { "filterSustain", 0.0f }, { "filterRelease", 0.11f }, { "tubeOn", 1 }, { "tubeDrive", 16.8f } } },
            // syntorial.com/preset-recipe/daft-punk-da-funk-lead ; reverbmachine.com (cuartas paralelas -> osc2 +5 st, "likely")
        // ---- Pink Floyd (investigación 2026-10-20)
        { "Shine On Lead", "Lead", { { "voiceMode", 1 }, { "wave", 2 }, { "cutoff", 700.0f }, { "resonance", 0.5f }, { "attack", 1.05f }, { "decay", 10.0f },
                          { "sustain", 0.0f }, { "release", 0.08f }, { "pitchEnvAmount", 18.0f }, { "pitchEnvDecay", 3.3f }, { "lfoOn", 1 }, { "lfoTarget", 0 },
                          { "lfoRate", 4.0f }, { "lfoDepth", 0.21f }, { "chorusOn", 1 }, { "chorusMode", 0 }, { "chorusMix", 0.5f }, { "ambientOn", 1 },
                          { "ambientSize", 0.5f }, { "ambientMix", 0.2f } } },
            // syntorial.com/preset-recipe/pink-floyd-shine-on-your-crazy-diamond-lead: EMS VCS3, sierra mono, LP 60 % res 50 %, amp A 1050 ms
            // D 10 s S 0 R 80 ms, envolvente de tono +18 st D 10 ms (τ ≈ 3,3 ms),
            // vibrato 42 ct a 1/8 (4 Hz a 120 BPM supuesto), chorus 50 % lento, reverb 20 %. Minimoog en "Wish You Were Here" (musicradar.com)
        { "On the Run Sequence", "Sequence", { { "voiceMode", 2 }, { "wave", 3 }, { "cutoff", 481.0f }, { "resonance", 0.75f }, { "filterKey", 0.5f },
                          { "attack", 0.001f }, { "sustain", 1.0f }, { "release", 0.05f }, { "seqOn", 1 }, { "seqSteps", 8 }, { "seqRate", 3 },
                          { "seqGate", 0.9f }, { "seqLatch", 1 }, { "arpBpm", 165.0f }, { "stepNote1", 12 }, { "stepNote2", 0 }, { "stepNote3", 3 },
                          { "stepNote4", 7 }, { "stepNote5", 5 }, { "stepNote6", 3 }, { "stepNote7", 5 }, { "stepNote8", 7 }, { "lfoOn", 1 },
                          { "lfoTarget", 2 }, { "lfoRate", 0.3f }, { "lfoDepth", 0.3f }, { "ambientOn", 1 }, { "ambientSize", 0.25f }, { "ambientMix", 0.1f } } },
            // syntorial.com/preset-recipe/pink-floyd-on-the-run-bass: EMS Synthi AKS, pulso 50 %, mono legato, LP 55 % res 75 %, key 50 %,
            // A 0 S 100 % R 50 ms, reverb 10 % pequeña. Secuencia de 8 notas a 165 BPM con corte y resonancia modulados (matrixsynth.com,
            // comentario sin contrastar; LFO de filtro estimado). Secuenciador: E4 E3 G3 B3 A3 G3 A3 B3 = +12 0 +3 +7 +5 +3 +5 +7 sobre E3
            // (pulsar E3; Latch lo mantiene), 1/16 a 165 BPM
        { "Shine On Intro Strings", "Strings", { { "wave", 2 }, { "osc2Wave", 2 }, { "osc2Octave", 1 }, { "osc2Level", 0.4f }, { "filterOn", 0 }, { "attack", 1.5f },
                          { "sustain", 1.0f }, { "release", 2.0f }, { "chorusOn", 1 }, { "chorusMode", 3 }, { "chorusMix", 0.7f }, { "chorusDepth", 0.6f },
                          { "ambientOn", 1 }, { "ambientSize", 0.8f }, { "ambientMix", 0.35f }, { "reverbDecay", 5.0f } } },
            // arquitectura: el acorde inicial (Sol menor) entra con VCS3, ARP Solina, Hammond y copas de cristal (Wikipedia vía forums.steinberg.net);
            // Solina = divisor de octava sin filtro + ensemble. Ataque lento por el fundido de entrada. Cifras estimadas
        // ---- Pad / Strings / Brass / Keys / Vocoder (investigación 2026-10-20). Recetas de Syntorial y Reverb Machine; cutoff % ->
        // Hz con la misma curva estimada que los bajos (Hz ≈ 7,86·e^(0,0748·p)); tiempos de 10 s respetados (rangos ampliados a 20 s).
        // Los presets marcados "arquitectura" reproducen el instrumento documentado; sus cifras son estimaciones.
        { "Replicant Brass Swell", "Brass", { { "wave", 2 }, { "osc1Level", 0.6f }, { "osc2Wave", 2 }, { "osc2Fine", 3.0f }, { "osc2Level", 0.4f }, { "drift", 0.1f },
                          { "filterMode", 1 }, { "filterDrive", 0.3f }, { "cutoff", 1000.0f }, { "filterEnv", 0.33f }, { "filterAttack", 1.0f }, { "filterDecay", 3.0f },
                          { "filterSustain", 0.6f }, { "filterRelease", 3.0f }, { "attack", 0.3f }, { "sustain", 0.85f }, { "release", 2.5f }, { "echoOn", 1 },
                          { "echoTime", 375.0f }, { "echoFeedback", 0.45f }, { "echoMix", 0.3f }, { "echoWow", 0.2f }, { "ambientOn", 1 }, { "ambientSize", 0.9f },
                          { "ambientMix", 0.35f }, { "reverbDecay", 10.0f } } },
            // musictech.com "how to emulate sounds of Blade Runner": 2 sierras 60/40 a pocos cents, filtro 12 dB, env +33 %, attack de filtro
            // ≈ 1 s, S&H lento al tono de osc2 (aquí: Drift), eco de cinta, reverb ≈ 10 s. Cutoff, delay y ADSR de amplitud estimados
        { "Tears in Rain Strings", "Strings", { { "wave", 2 }, { "osc2Wave", 2 }, { "osc2Octave", 1 }, { "osc2Level", 0.5f }, { "filterMode", 1 }, { "cutoff", 4500.0f },
                          { "attack", 0.35f }, { "sustain", 1.0f }, { "release", 1.8f }, { "chorusOn", 1 }, { "chorusMode", 3 }, { "chorusMix", 0.6f },
                          { "chorusDepth", 0.7f }, { "echoOn", 1 }, { "echoTime", 420.0f }, { "echoFeedback", 0.35f }, { "echoMix", 0.2f }, { "ambientOn", 1 },
                          { "ambientSize", 0.9f }, { "ambientMix", 0.4f }, { "reverbDecay", 8.0f } } },
            // arquitectura: cuerdas del Roland VP-330 con ensemble BBD, attack/release únicos, delay tipo Space Echo y Lexicon 224
            // (reverbmachine.com/blog/vangelis-tears-in-rain-blade-runner, Wikipedia). Cifras estimadas
        { "Oxygene Phased Strings", "Strings", { { "phaserOn", 1 }, { "phaserRate", 0.3f }, { "phaserDepth", 0.6f }, { "phaserMix", 0.5f }, { "wave", 2 }, { "osc1Level", 0.6f }, { "osc2Wave", 2 }, { "osc2Octave", 1 }, { "osc2Level", 0.5f }, { "filterOn", 0 },
                          { "attack", 0.6f }, { "sustain", 1.0f }, { "release", 2.0f }, { "chorusOn", 1 }, { "chorusMode", 3 }, { "chorusMix", 0.6f },
                          { "chorusDepth", 0.6f }, { "echoOn", 1 }, { "echoTime", 300.0f }, { "echoFeedback", 0.3f }, { "echoMix", 0.2f }, { "ambientOn", 1 },
                          { "ambientSize", 0.7f }, { "ambientMix", 0.35f }, { "reverbDecay", 4.0f } } },
            // arquitectura: Eminent 310 a 8' + 4' por un phaser EHX Small Stone, placa EMT y eco Revox (hollowsun.com, Wikipedia).
            // Phaser de 4 etapas (velocidad y profundidad estimadas; la del pedal original no está documentada). Cifras estimadas
        { "Incantations Ensemble", "Strings", { { "wave", 2 }, { "osc2Wave", 2 }, { "osc2Octave", 1 }, { "osc2Level", 0.4f }, { "filterOn", 0 }, { "attack", 0.2f },
                          { "sustain", 1.0f }, { "release", 1.2f }, { "chorusOn", 1 }, { "chorusMode", 3 }, { "chorusMix", 0.7f }, { "chorusDepth", 0.6f },
                          { "ambientOn", 1 }, { "ambientSize", 0.6f }, { "ambientMix", 0.25f }, { "reverbDecay", 3.0f } } },
            // arquitectura: ARP/Eminent Solina en Ommadawn, Incantations, Platinum, Crises (tubular.net): divisor de octava, sin filtro,
            // envolvente AR, ensemble BBD triple. Tiempos estimados
        { "Space Oddity Tape Strings", "Strings", { { "wave", 2 }, { "unisonVoices", 3 }, { "unisonDetune", 8.0f }, { "unisonSpread", 0.4f }, { "filterMode", 1 },
                          { "cutoff", 3500.0f }, { "attack", 0.12f }, { "decay", 2.0f }, { "sustain", 0.9f }, { "release", 0.25f }, { "mediaOn", 1 }, { "mediaType", 3 },
                          { "mediaAge", 0.5f }, { "mediaWow", 0.5f }, { "mediaNoise", 0.2f }, { "ambientOn", 1 }, { "ambientSize", 0.5f }, { "ambientMix", 0.2f },
                          { "reverbDecay", 2.0f } } },
            // arquitectura: Mellotron (cinta de ≈ 8 s, "3 violins") tocado por Rick Wakeman, Trident 20-06-1969 (bowiebible.com, Wikipedia).
            // Falta el límite de 8 s por nota. Cifras estimadas
        { "Warszawa Tape Cello", "Strings", { { "wave", 2 }, { "osc2Wave", 1 }, { "osc2Octave", 1 }, { "osc2Level", 0.3f }, { "filterMode", 1 }, { "cutoff", 2200.0f },
                          { "attack", 0.15f }, { "decay", 2.0f }, { "sustain", 0.85f }, { "release", 0.4f }, { "mediaOn", 1 }, { "mediaType", 2 }, { "mediaAge", 0.3f },
                          { "mediaWow", 0.2f }, { "mediaNoise", 0.15f }, { "ambientOn", 1 }, { "ambientSize", 0.7f }, { "ambientMix", 0.3f }, { "reverbDecay", 4.0f } } },
            // arquitectura: melodía en Chamberlin con cello y flauta (Eno), Wikipedia "Warszawa". Cifras estimadas
        { "Robot Voice 10-Band", "Vocoder", { { "wave", 2 }, { "osc2Wave", 3 }, { "osc2Octave", -1 }, { "osc2Level", 0.4f }, { "filterOn", 0 }, { "attack", 0.005f },
                          { "sustain", 1.0f }, { "release", 0.15f }, { "vocoderOn", 1 }, { "vocoderBank", 0 }, { "vocoderMix", 1.0f }, { "vocoderAttack", 5.0f },
                          { "vocoderRelease", 40.0f }, { "vocoderSibilance", 0.3f }, { "inputGain", 12.0f } } },
            // banco VP-330 de 10 bandas verificado (smemmusic.ch); el vocoder concreto de "The Robots" no está confirmado. Cifras estimadas
        { "Autobahn Vocoder", "Vocoder", { { "wave", 2 }, { "filterOn", 0 }, { "attack", 0.01f }, { "sustain", 1.0f }, { "release", 0.2f }, { "vocoderOn", 1 },
                          { "vocoderBank", 1 }, { "vocoderMix", 1.0f }, { "vocoderAttack", 8.0f }, { "vocoderRelease", 60.0f }, { "vocoderSibilance", 0.2f },
                          { "inputGain", 12.0f }, { "echoOn", 1 }, { "echoTime", 250.0f }, { "echoFeedback", 0.3f }, { "echoMix", 0.15f } } },
            // vocoder hecho a medida por un ingeniero de telefonía (attackmagazine.com, resumen de búsqueda). Todo estimado
        { "Voyager Chords", "Pad", { { "wave", 2 }, { "cutoff", 1000.0f }, { "resonance", 0.1f }, { "filterEnv", 0.27f }, { "filterAttack", 0.3f }, { "filterDecay", 2.0f },
                          { "filterSustain", 0.0f }, { "attack", 0.3f }, { "decay", 2.0f }, { "sustain", 0.0f }, { "release", 0.6f }, { "chorusOn", 1 },
                          { "chorusMode", 1 }, { "chorusMix", 0.5f } } },
            // reverbmachine.com/blog/daft-punk-discovery-synth-sounds: Juno-106 "probable", sierra, faders env 2,7 A 4 D 7 S 0, Chorus II.
            // Conversión de faders a s/Hz estimada (Juno-106: attack 1,5 ms–3 s, decay 1,5 ms–12 s, support.roland.com); falta HPF en serie
        { "Short Circuit Swell", "Pad", { { "wave", 2 }, { "cutoff", 500.0f }, { "resonance", 0.38f }, { "filterEnv", 0.5f }, { "filterAttack", 1.0f }, { "filterDecay", 2.0f },
                          { "filterSustain", 0.5f }, { "attack", 1.0f }, { "sustain", 1.0f }, { "release", 1.0f }, { "chorusOn", 1 }, { "chorusMode", 1 },
                          { "chorusMix", 0.5f } } },
            // reverbmachine.com (Discovery): sierra, faders freq 3,5 res 3,8 env 5 attack 7,2. Conversión estimada
        { "Neon Lights Keys", "Keys", { { "wave", 3 }, { "pulseWidth", 0.1f }, { "osc2Wave", 3 }, { "osc2Octave", 1 }, { "osc1Level", 0.3f }, { "osc2Level", 0.8f },
                          { "filterMode", 1 }, { "cutoff", 20000.0f }, { "resonance", 0.8f }, { "attack", 0.002f }, { "sustain", 1.0f }, { "release", 3.28f },
                          { "lfoOn", 1 }, { "lfoTarget", 1 }, { "lfoRate", 8.0f }, { "lfoDepth", 0.85f }, { "chorusOn", 1 }, { "chorusMode", 1 }, { "chorusMix", 0.19f },
                          { "ambientOn", 1 }, { "ambientSize", 0.5f }, { "ambientMix", 0.19f } } },
            // syntorial.com/preset-recipe/kraftwerk-neon-lights-keys: 2 pulsos finos (osc2 +12 st), LP 12 dB a 21 kHz res 80 %, A 2 ms R 3,28 s,
            // trémolo 1/16 (120 BPM supuesto). Falta el segundo LFO (barrido de filtro a 4/1)
        { "In The Air Tonight Pad", "Pad", { { "wave", 3 }, { "cutoff", 331.0f }, { "resonance", 0.45f }, { "attack", 0.001f }, { "sustain", 1.0f }, { "release", 0.02f },
                          { "chorusOn", 1 }, { "chorusMode", 0 }, { "chorusMix", 0.5f }, { "echoOn", 1 }, { "echoTime", 125.0f }, { "echoFeedback", 0.0f },
                          { "echoMix", 0.5f } } },
            // syntorial.com/preset-recipe/phil-collins-in-the-air-tonight-pad (delay 1/16 a 120 BPM supuesto)
        { "Fade To Grey Pad", "Pad", { { "wave", 2 }, { "unisonVoices", 4 }, { "unisonDetune", 6.0f }, { "unisonSpread", 0.8f }, { "cutoff", 4537.0f }, { "attack", 0.001f },
                          { "sustain", 1.0f }, { "release", 0.02f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 4.0f }, { "lfoDepth", 0.28f },
                          { "ambientOn", 1 }, { "ambientSize", 0.5f }, { "ambientMix", 0.3f } } },
            // syntorial.com/preset-recipe/visage-fade-to-grey-pad (vibrato 56 ct a 1/8, 120 BPM supuesto)
        { "Stranger Things Pad", "Pad", { { "wave", 2 }, { "unisonVoices", 2 }, { "unisonDetune", 8.0f }, { "unisonSpread", 0.3f }, { "cutoff", 156.0f }, { "resonance", 0.2f },
                          { "filterEnv", 0.43f }, { "filterAttack", 2.7f }, { "filterSustain", 1.0f }, { "filterRelease", 10.0f }, { "attack", 1.55f }, { "sustain", 1.0f },
                          { "release", 3.65f }, { "chorusOn", 1 }, { "chorusMode", 0 }, { "chorusMix", 0.45f } } },
            // syntorial.com/preset-recipe/survive-stranger-things-theme-pad (fenv 20 % A 2,7 s, R 10 s)
        { "Africa Brass", "Brass", { { "wave", 2 }, { "cutoff", 481.0f }, { "filterEnv", 0.21f }, { "filterAttack", 0.08f }, { "filterDecay", 5.3f }, { "filterSustain", 0.2f },
                          { "filterRelease", 10.0f }, { "attack", 0.001f }, { "decay", 10.0f }, { "sustain", 0.0f }, { "release", 0.3f }, { "chorusOn", 1 },
                          { "chorusMode", 0 }, { "chorusMix", 0.4f }, { "ambientOn", 1 }, { "ambientSize", 0.5f }, { "ambientMix", 0.2f } } },
            // syntorial.com/preset-recipe/toto-africa-brass (decay 10 s, filterDecay 5,3 s, filterRelease 10 s)
        { "Jump Brass", "Brass", { { "wave", 2 }, { "unisonVoices", 4 }, { "unisonDetune", 9.0f }, { "cutoff", 4537.0f }, { "resonance", 0.25f }, { "attack", 0.001f },
                          { "decay", 10.0f }, { "sustain", 0.0f }, { "release", 0.08f }, { "ambientOn", 1 }, { "ambientSize", 0.5f }, { "ambientMix", 0.3f } } },
            // syntorial.com/preset-recipe/van-halen-jump-brass (decay 10 s)
        // ---- 8-Bit (investigación 2026-10-20). Chips: nesdev.org (APU Pulse/Triangle/Noise/Envelope), gbdev.io Pan Docs, SID en
        // waitingforfriday.com/?p=661. Efectos de juego: generadores de sfxr (github.com/grimfang4/sfxr, MIT) con el punto medio de sus
        // rangos aleatorios; conversiones calc: f = 3528·(b² + 0,001) Hz, etapa = 2,268·x² s. Los slides lineales de sfxr (D st en T s)
        // se aproximan con la envolvente de tono exponencial: Amount = D, τ = T/2 (falta un 13 % del recorrido al final de T).
        // Crush a 4 bits (Drive +12 dB por defecto) = 16 niveles de volumen de NES/Game Boy.
            // pulsar F#6 + B6 ~125 ms. sfxr pickup: base 1494 Hz ≈ F#6, salto +2,7 st (rango 0,6–6,8) tras 73 ms; intervalo +5 st elegido
            // dentro del rango. El arp repite mientras se mantiene (falta arp de un solo disparo)
            // tocar G#6. sfxr jump: duty 0,35, base 718 Hz, subida +61 st/s durante 0,233 s = +14,2 st
            // tocar C#5. sfxr laserShoot: base 1988 Hz, bajada -119 st/s en 181 ms = -21,6 st, duty 0,375
            // cualquier nota. sfxr explosion: ruido, base 32,1 Hz × 32 valores por periodo = 1027 Hz de retención; NES: 2349 Hz (periodo $C)
        { "8-Bit Power-Up", "8-Bit", { { "wave", 3 }, { "pulseWidth", 0.35f }, { "attack", 0.001f }, { "decay", 0.2f }, { "sustain", 0.5f }, { "release", 0.1f },
                          { "pitchEnvAmount", -15.0f }, { "pitchEnvDecay", 37.0f }, { "arpOn", 1 }, { "arpMode", 0 }, { "arpRate", 3 }, { "arpBpm", 205.0f },
                          { "arpOctaves", 2 }, { "arpGate", 1.0f } } },
            // pulsar una tríada en A4 ~300 ms. sfxr powerup: base 436 Hz, +206 st/s, repetición 73 ms = +15 st por repetición; la tríada
            // arpegiada sustituye a la repetición (cada paso redispara la envolvente de tono)
            // pulsar una tríada mayor ~1 s. Sin generador de sfxr para "1-up": receta estimada (solo los 4 bits son verificados)
        { "8-Bit Hit", "8-Bit", { { "wave", 3 }, { "pulseWidth", 0.35f }, { "attack", 0.001f }, { "decay", 0.1f }, { "sustain", 0.0f }, { "release", 0.02f },
                          { "pitchEnvAmount", 48.0f }, { "pitchEnvDecay", 25.0f } } },
            // tocar A1 (empieza en A5). sfxr hitHurt: base 886 Hz, -954 st/s; sfxr baja hasta -92 st, KoniSynth llega a 48
        { "8-Bit Menu Blip", "8-Bit", { { "wave", 3 }, { "pulseWidth", 0.35f }, { "attack", 0.001f }, { "decay", 0.07f }, { "sustain", 0.0f }, { "release", 0.02f } } },
            // tocar C#5. sfxr blipSelect: base 568 Hz, 51 + 23 ms, duty 0,35
            // C5–C6. NES pulso: duty 12,5 %, modo de volumen constante, 54,6 Hz–12,4 kHz
            // C4–C6. Game Boy: duty 25 %, envolvente pace 3 = 15·3/64 = 703 ms (en el hardware la caída es lineal en 16 escalones)
        { "Chip Square Bass", "8-Bit", { { "voiceMode", 1 }, { "wave", 3 }, { "attack", 0.001f }, { "decay", 0.15f }, { "sustain", 0.7f }, { "release", 0.03f },
                          { "crushOn", 1 }, { "crushBits", 4.0f } } },
            // A1–A3. NES pulso 50 % (no baja de 54,6 Hz ≈ A1); ADSR estimado
            // A0–A3. NES triángulo: 32 escalones de 4 bits, sin control de volumen (solo puerta), desde 27,3 Hz. Drive +14 dB (×5) lleva
            // ±0,2 a ±1: 16 niveles = 32 escalones por ciclo
            // pulsar una tríada. Acorde arpegiado de tracker: 1/32 a 240 BPM = 31 ms (los trackers lo hacen por frame de 16,7 ms)
        { "SID-Style Sync Lead", "8-Bit", { { "voiceMode", 2 }, { "glide", 60.0f }, { "wave", 2 }, { "osc1Level", 0.0f }, { "osc2Wave", 2 }, { "osc2Level", 1.0f },
                          { "osc2Sync", 1 }, { "osc2Octave", 1 }, { "osc2Semi", 7 }, { "attack", 0.002f }, { "decay", 0.204f }, { "sustain", 0.67f },
                          { "release", 0.114f }, { "filterMode", 1 }, { "cutoff", 2000.0f }, { "resonance", 0.5f }, { "filterEnv", 0.3f } } },
            // C4–C5. SID: sync, LP 12 dB/oct, tabla ADSR A0 = 2 ms, D6 = 204 ms, S 10/15, R4 = 114 ms; corte, resonancia y glide estimados
        { "SID-Style PWM Bass", "8-Bit", { { "voiceMode", 1 }, { "wave", 3 }, { "pwmDepth", 0.3f }, { "lfoRate", 0.8f }, { "attack", 0.002f }, { "decay", 0.168f },
                          { "sustain", 0.5f }, { "release", 0.072f }, { "filterMode", 1 }, { "cutoff", 800.0f }, { "resonance", 0.6f }, { "filterEnv", 0.4f } } },
            // C1–C3. SID: PW de 12 bits, LP 12 dB, A0 = 2 ms, D5 = 168 ms, R3 = 72 ms; velocidad de PWM y filtro estimados
        { "SID-Style Ring Bell", "8-Bit", { { "wave", 1 }, { "complexOn", 1 }, { "amDepth", 1.0f }, { "modRatio", 3.5f }, { "fmIndex", 0.0f }, { "attack", 0.002f },
                          { "decay", 1.5f }, { "sustain", 0.0f }, { "release", 1.5f } } },
            // C5–C6. SID: modulación en anillo sobre la triangular, A0 = 2 ms, D = R = 10 -> 1,5 s; relación 3,5 elegida (inarmónica)
        // ---- Nature / Vocal (investigación 2026-10-20). Parches Pd de A. Farnell, "Designing Sound" (github.com/IgorxOcarina/
        // Farnell_designing_sound_pd_patches, leídos objeto a objeto), bioacústica publicada y formantes de Csound. Resonancia -> Q del
        // BP12 calculada con el modelo lineal del ladder (no medida). Muchos se tocan manteniendo una nota: el arpegiador repite la llamada.
        { "Field Cricket", "Nature", { { "wave", 0 }, { "osc2Wave", 0 }, { "osc2Octave", 1 }, { "osc2Level", 0.3f }, { "filterOn", 0 }, { "fineTune", 25.0f },
                          { "attack", 0.002f }, { "decay", 0.001f }, { "sustain", 1.0f }, { "release", 0.005f }, { "lfoOn", 1 }, { "lfoTarget", 1 },
                          { "lfoShape", 3 }, { "lfoRate", 58.1f }, { "lfoDepth", 1.0f }, { "arpOn", 1 }, { "arpMode", 0 }, { "arpRate", 0 }, { "arpGate", 0.172f },
                          { "arpBpm", 86.0f } } },
            // mantener C#8 (+25 ct = 4500 Hz). Farnell cricket1: portadora 4500 Hz + 2º armónico ×0,3, pulsos de 58,1 Hz en ventanas de 120 ms
            // cada 0,699 s (1/4 a 86 BPM = 0,698 s; gate 0,172 = 120 ms). Falta el crescendo de los 7 pulsos de cricket2
        { "Dolbear Cricket 25C", "Nature", { { "wave", 0 }, { "osc2Wave", 0 }, { "osc2Octave", 1 }, { "osc2Level", 0.2f }, { "fineTune", 8.0f }, { "attack", 0.002f },
                          { "decay", 0.001f }, { "sustain", 1.0f }, { "release", 0.004f }, { "lfoOn", 1 }, { "lfoTarget", 1 }, { "lfoShape", 3 },
                          { "lfoRate", 30.3f }, { "lfoDepth", 1.0f }, { "arpOn", 1 }, { "arpMode", 0 }, { "arpRate", 1 }, { "arpGate", 0.32f }, { "arpBpm", 73.0f } } },
            // mantener D#8 (+8 ct ≈ 5 kHz). Ley de Dolbear (Wikipedia): T_C = (N + 30)/7; 1/8 a 73 BPM = 146 chirps/min -> 25,1 °C.
            // Regla: arpBpm = (7·T_C - 30)/2. Sílabas de ~30 Hz, 4 por chirp (resumen de Springer, G. campestris; no leído entero)
        { "Cicada Buzz", "Nature", { { "osc1Level", 0.0f }, { "noiseLevel", 1.0f }, { "filterMode", 2 }, { "cutoff", 6383.0f }, { "resonance", 0.69f },
                          { "attack", 0.05f }, { "decay", 0.1f }, { "sustain", 1.0f }, { "release", 0.3f }, { "lfoOn", 1 }, { "lfoTarget", 1 }, { "lfoRate", 8.0f },
                          { "lfoDepth", 1.0f } } },
            // mantener cualquier nota. Farnell cicada1 (BP 6000 Hz Q 7 -> res 0,69, pico a 0,94·cutoff) y cicada3 (llamada a 8 Hz).
            // Falta el ring mod a 500 Hz y la 2ª banda
        { "Spring Peeper", "Nature", { { "wave", 0 }, { "fineTune", 43.0f }, { "attack", 0.01f }, { "decay", 0.15f }, { "sustain", 0.0f }, { "release", 0.02f },
                          { "pitchEnvAmount", -2.0f }, { "pitchEnvDecay", 50.0f }, { "arpOn", 1 }, { "arpMode", 0 }, { "arpRate", 0 }, { "arpGate", 0.2f },
                          { "arpBpm", 60.0f }, { "ambientOn", 1 }, { "ambientMix", 0.25f }, { "reverbDecay", 1.5f } } },
            // mantener F7 (+43 ct = 2864 Hz). Hanna et al. 2014 (cs.mun.ca): pico 2864 ± 20 Hz, llamada de 157 ms. Subida y ritmo estimados
        { "Bullfrog Croak", "Nature", { { "wave", 2 }, { "filterMode", 2 }, { "cutoff", 632.0f }, { "resonance", 0.0f }, { "attack", 0.02f }, { "decay", 0.8f },
                          { "sustain", 0.0f }, { "release", 0.1f }, { "lfoOn", 1 }, { "lfoTarget", 1 }, { "lfoRate", 10.0f }, { "lfoDepth", 0.5f }, { "tubeOn", 1 },
                          { "tubeDrive", 6.0f } } },
            // tocar G2 (98 Hz). PMC1249523: armónicos de 200 a 2000 Hz con fundamental ~100 Hz débil, modulaciones < 12 Hz
        { "Chickadee Fee-Bee", "Nature", { { "wave", 0 }, { "attack", 0.02f }, { "decay", 0.35f }, { "sustain", 0.0f }, { "release", 0.03f },
                          { "pitchEnvAmount", 0.7f }, { "pitchEnvDecay", 200.0f }, { "arpOn", 1 }, { "arpMode", 1 }, { "arpRate", 1 }, { "arpGate", 0.74f },
                          { "arpBpm", 60.0f }, { "ambientOn", 1 }, { "ambientMix", 0.2f }, { "reverbDecay", 1.2f } } },
            // mantener A7 + G7 (Down: "fee" y luego "bee"). PMC7595340: dos notas puras de 3–4 kHz; 0,37 s + 0,13 s (Ficken 1978, resumen)
        { "Syrinx Tweet", "Nature", { { "wave", 0 }, { "filterMode", 2 }, { "cutoff", 3534.0f }, { "resonance", 0.5f }, { "pitchEnvAmount", 12.0f },
                          { "pitchEnvDecay", 40.0f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 20.0f }, { "lfoDepth", 0.3f }, { "attack", 0.005f },
                          { "decay", 0.12f }, { "sustain", 0.0f }, { "release", 0.03f }, { "arpOn", 1 }, { "arpMode", 3 }, { "arpRate", 3 }, { "arpGate", 0.4f },
                          { "arpBpm", 150.0f }, { "ambientOn", 1 }, { "ambientMix", 0.2f } } },
            // mantener 3–5 notas entre C7 y C8. Farnell bird2/trachea: formante traqueal ≈ 3138 Hz (Q 3 -> res 0,5). Chirp FM aproximado
        { "Tawny Owl Hoot", "Nature", { { "wave", 0 }, { "osc2Wave", 0 }, { "osc2Octave", 1 }, { "osc2Level", 0.3f }, { "attack", 0.05f }, { "decay", 0.5f },
                          { "sustain", 0.5f }, { "release", 0.3f }, { "pitchEnvAmount", -1.77f }, { "pitchEnvDecay", 150.0f }, { "ambientOn", 1 },
                          { "ambientMix", 0.35f }, { "reverbDecay", 2.5f } } },
            // tocar G#5 ~0,6 s, pausa de 3–4 s. wildechoes.org: ≈ 800 Hz, 1ª nota 750 -> 850 Hz, armónico a 1600 Hz
        { "Wolf Howl", "Nature", { { "voiceMode", 2 }, { "glide", 400.0f }, { "wave", 2 }, { "cutoff", 1200.0f }, { "resonance", 0.1f }, { "filterKey", 1.0f },
                          { "pitchEnvAmount", -7.0f }, { "pitchEnvDecay", 500.0f }, { "attack", 0.35f }, { "decay", 1.0f }, { "sustain", 0.8f }, { "release", 1.5f },
                          { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.0f }, { "lfoDepth", 0.05f }, { "ambientOn", 1 }, { "ambientMix", 0.35f },
                          { "reverbDecay", 4.0f } } },
            // mantener E5 de 1 a 13 s, legato entre 270 y 720 Hz. Palacios et al. 2007 (uv.es): F0 media 270–720 Hz, 1,1–12,8 s
        { "Cat Meow", "Nature", { { "wave", 2 }, { "filterMode", 2 }, { "cutoff", 1500.0f }, { "resonance", 0.5f }, { "filterEnv", 0.4f }, { "filterAttack", 0.2f },
                          { "filterDecay", 0.5f }, { "filterSustain", 0.2f }, { "filterRelease", 0.3f }, { "pitchEnvAmount", -2.0f }, { "pitchEnvDecay", 150.0f },
                          { "attack", 0.05f }, { "decay", 0.6f }, { "sustain", 0.3f }, { "release", 0.25f } } },
            // tocar D#5 ~0,8 s. Nicastro y Owren 2003: F0 400–1200 Hz, duración media 0,78 s. Contorno estimado
        { "Cat Purr", "Nature", { { "osc1Level", 0.0f }, { "osc2Wave", 2 }, { "osc2Octave", -2 }, { "osc2Level", 1.0f }, { "noiseLevel", 0.15f }, { "noiseColor", 1 },
                          { "cutoff", 400.0f }, { "resonance", 0.2f }, { "attack", 0.3f }, { "decay", 0.3f }, { "sustain", 1.0f }, { "release", 0.5f },
                          { "lfoOn", 1 }, { "lfoTarget", 1 }, { "lfoRate", 0.8f }, { "lfoDepth", 0.4f } } },
            // tocar G2 (osc2 = 24,5 Hz). Wikipedia "Purr" (Schötz y Eklund): 20,9–27,2 Hz. Ciclo respiratorio de 0,8 Hz estimado
        { "Humpback Moan", "Nature", { { "voiceMode", 2 }, { "glide", 900.0f }, { "wave", 1 }, { "osc2Wave", 0 }, { "osc2Octave", -1 }, { "osc2Level", 0.5f },
                          { "cutoff", 1500.0f }, { "filterKey", 0.5f }, { "pitchEnvAmount", -5.0f }, { "pitchEnvDecay", 800.0f }, { "attack", 0.5f },
                          { "decay", 1.5f }, { "sustain", 0.8f }, { "release", 2.5f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 0.3f }, { "lfoDepth", 0.15f },
                          { "echoOn", 1 }, { "echoTime", 700.0f }, { "echoFeedback", 0.35f }, { "echoMix", 0.25f }, { "ambientOn", 1 }, { "ambientMix", 0.5f },
                          { "reverbDecay", 8.0f } } },
            // unidades de 1–3 s en legato entre C3 y C5. Wikipedia "Whale vocalization": unidades de 20 Hz a > 24 kHz y de pocos segundos
        { "Gull Long Call", "Nature", { { "wave", 2 }, { "filterMode", 2 }, { "cutoff", 3000.0f }, { "resonance", 0.5f }, { "pitchEnvAmount", 2.0f },
                          { "pitchEnvDecay", 60.0f }, { "attack", 0.01f }, { "decay", 0.12f }, { "sustain", 0.0f }, { "release", 0.05f }, { "arpOn", 1 },
                          { "arpMode", 0 }, { "arpRate", 1 }, { "arpGate", 0.5f }, { "arpBpm", 126.0f }, { "tubeOn", 1 }, { "tubeDrive", 6.0f },
                          { "ambientOn", 1 }, { "ambientMix", 0.2f }, { "reverbDecay", 1.5f } } },
            // mantener E6 (nota alta) o C6 ("miau"). gull-research.org: 1,31–1,47 kHz; 8 elementos en 1,7–2,1 s (0,238 s/elemento)
        { "Rain Drops", "Nature", { { "osc1Level", 0.0f }, { "noiseLevel", 1.0f }, { "filterMode", 3 }, { "cutoff", 900.0f }, { "filterKey", 1.0f },
                          { "attack", 0.0002f }, { "decay", 0.0003f }, { "sustain", 0.0f }, { "release", 0.0002f }, { "arpOn", 1 }, { "arpMode", 3 },
                          { "arpRate", 3 }, { "arpGate", 0.05f }, { "arpBpm", 134.0f }, { "ambientOn", 1 }, { "ambientMix", 0.3f }, { "reverbDecay", 1.2f } } },
            // mantener las blancas de C4 a C6 (HP aleatorio 0,9–3,6 kHz). Farnell rain1/cpulse: gota de 0,2–0,31 ms, hip 900 Hz,
            // 8,95 gotas/s (1/16 a 134 BPM = 8,93). Rejilla regular en lugar de intervalos aleatorios
        { "Campfire Crackle", "Nature", { { "osc1Level", 0.0f }, { "noiseLevel", 1.0f }, { "filterMode", 2 }, { "cutoff", 2400.0f }, { "resonance", 0.235f },
                          { "filterKey", 1.0f }, { "attack", 0.0005f }, { "decay", 0.015f }, { "sustain", 0.0f }, { "release", 0.01f }, { "arpOn", 1 },
                          { "arpMode", 3 }, { "arpRate", 4 }, { "arpGate", 0.1f }, { "arpBpm", 100.0f }, { "ambientOn", 1 }, { "ambientMix", 0.15f },
                          { "reverbDecay", 0.8f } } },
            // mantener notas de C4 a C7 (centros 2–16 kHz). Farnell fire_crackling3: BP Q 1, decay U[0, 29] ms, centros 1,5–16 kHz.
            // Faltan siseo y lamido (capas paralelas)
        { "Wind Whistle", "Nature", { { "osc1Level", 0.0f }, { "noiseLevel", 0.3f }, { "filterMode", 2 }, { "cutoff", 781.0f }, { "resonance", 0.88f },
                          { "attack", 2.0f }, { "decay", 2.0f }, { "sustain", 1.0f }, { "release", 4.0f }, { "lfoOn", 1 }, { "lfoTarget", 2 }, { "lfoShape", 5 },
                          { "lfoRate", 0.1f }, { "lfoDepth", 0.184f }, { "ambientOn", 1 }, { "ambientMix", 0.3f }, { "reverbDecay", 3.0f } } },
            // mantener cualquier nota. Farnell wind4: VCF Q 60 (res 0,88) a 600 + 400·velocidad (601–1000 Hz); velocidad lenta y
            // aleatoria (LFO Random a 0,1 Hz)
        { "Thunder Strike", "Nature", { { "osc1Level", 0.0f }, { "noiseLevel", 1.0f }, { "filterMode", 1 }, { "cutoff", 80.0f }, { "resonance", 0.0f },
                          { "filterEnv", 0.8f }, { "filterAttack", 0.001f }, { "filterDecay", 0.6f }, { "filterSustain", 0.0f }, { "filterRelease", 2.0f },
                          { "attack", 0.05f }, { "decay", 8.0f }, { "sustain", 0.0f }, { "release", 4.0f }, { "tubeOn", 1 }, { "tubeDrive", 21.6f },
                          { "echoOn", 1 }, { "echoTime", 1000.0f }, { "echoFeedback", 0.5f }, { "echoMix", 0.3f }, { "ambientOn", 1 }, { "ambientMix", 0.6f },
                          { "reverbDecay", 20.0f } } },
            // nota corta. Farnell thunder4: LP 80 Hz ×2, golpe ≈ 100–1300 Hz, ataque 50 ms, retumbo de 8 s, clip ×12 (21,6 dB), cola de 20 s
        { "Choir Aah", "Vocal", { { "wave", 2 }, { "unisonVoices", 5 }, { "unisonDetune", 10.0f }, { "cutoff", 6000.0f }, { "attack", 0.4f }, { "decay", 0.5f },
                          { "sustain", 0.9f }, { "release", 1.2f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.5f }, { "lfoDepth", 0.15f },
                          { "formantOn", 1 }, { "formantVoice", 0 }, { "formantVowel", 0.0f }, { "formantMix", 1.0f }, { "ambientOn", 1 }, { "ambientMix", 0.35f },
                          { "reverbDecay", 3.5f } } },
            // acordes de C2 a C4. Formantes Csound "bass" A: 600/1040/2250 Hz, BW 60/70/110. Detune y vibrato (5,5 Hz, ±0,3 st) estimados
        { "Choir Ooh", "Vocal", { { "wave", 2 }, { "unisonVoices", 5 }, { "unisonDetune", 10.0f }, { "cutoff", 6000.0f }, { "attack", 0.5f }, { "decay", 0.5f },
                          { "sustain", 0.9f }, { "release", 1.5f }, { "lfoOn", 1 }, { "lfoTarget", 0 }, { "lfoRate", 5.5f }, { "lfoDepth", 0.12f },
                          { "formantOn", 1 }, { "formantVoice", 1 }, { "formantVowel", 4.0f }, { "formantMix", 1.0f }, { "ambientOn", 1 }, { "ambientMix", 0.4f },
                          { "reverbDecay", 4.0f } } },
            // C4–C6. Formantes Csound "soprano" U: 325/700/2700 Hz (tabla femenina propia en vez de desplazar la masculina)
        { "Vowel Morph Pad", "Vocal", { { "wave", 2 }, { "unisonVoices", 3 }, { "unisonDetune", 8.0f }, { "attack", 0.6f }, { "sustain", 1.0f }, { "release", 1.5f },
                          { "formantOn", 1 }, { "formantVoice", 0 }, { "formantVowel", 2.0f }, { "formantRate", 0.15f }, { "formantDepth", 2.0f },
                          { "formantMix", 1.0f }, { "chorusOn", 1 }, { "chorusMode", 1 }, { "chorusMix", 0.4f }, { "ambientOn", 1 }, { "ambientMix", 0.35f },
                          { "reverbDecay", 4.0f } } },
            // acordes. Barrido lento A–E–I–O–U (formantes Csound "bass"). Velocidad estimada
        { "Talk Box Lead", "Vocal", { { "voiceMode", 2 }, { "glide", 60.0f }, { "wave", 2 }, { "osc2Wave", 3 }, { "osc2Level", 0.5f }, { "tubeOn", 1 },
                          { "tubeDrive", 12.0f }, { "filterMode", 1 }, { "cutoff", 800.0f }, { "filterEnv", 0.5f }, { "filterAttack", 0.08f },
                          { "filterDecay", 0.4f }, { "filterSustain", 0.3f }, { "formantOn", 1 }, { "formantVowel", 3.0f }, { "formantMix", 0.8f },
                          { "attack", 0.005f }, { "sustain", 1.0f }, { "release", 0.15f } } },
            // línea monofónica en legato, C3–C5. csoundjournal.com (1999): fuente saturada rica en armónicos + 3 pasa-banda. Vocal O y "wah" estimados
        // ---- Wavetable / Sequencer (presets propios de KoniSynth, sin referencia externa)
        { "Harmonic Sweep Pad", "Pad", { { "osc1Level", 0.0f }, { "wtLevel", 1.0f }, { "wtTable", 2 }, { "wtPosition", 0.5f }, { "lfo2On", 1 },
                          { "lfo2Target", 5 }, { "lfo2Rate", 0.08f }, { "lfo2Depth", 0.8f }, { "attack", 0.8f }, { "sustain", 1.0f }, { "release", 2.0f },
                          { "unisonVoices", 1 }, { "chorusOn", 1 }, { "chorusMode", 1 }, { "chorusMix", 0.4f }, { "ambientOn", 1 }, { "ambientMix", 0.35f },
                          { "reverbDecay", 4.0f } } },
            // tabla "Harmonic Sweep" (armónicos 1..k) recorrida por el LFO 2
        { "Digital Sync Lead", "Lead", { { "voiceMode", 1 }, { "glide", 40.0f }, { "osc1Level", 0.0f }, { "wtLevel", 1.0f }, { "wtTable", 3 },
                          { "wtPosition", 0.3f }, { "lfo2On", 1 }, { "lfo2Target", 5 }, { "lfo2Rate", 0.5f }, { "lfo2Depth", 0.5f }, { "attack", 0.005f },
                          { "sustain", 1.0f }, { "release", 0.2f }, { "cutoff", 6000.0f }, { "echoOn", 1 }, { "echoSync", 3 }, { "echoMix", 0.25f } } },
            // tabla "Sync Sweep" (sierra sincronizada, relación 1–8) barrida por el LFO 2; eco a 1/8
        { "Formant Sweep Keys", "Keys", { { "osc1Level", 0.0f }, { "wtLevel", 1.0f }, { "wtTable", 4 }, { "wtPosition", 0.2f }, { "attack", 0.005f },
                          { "decay", 1.2f }, { "sustain", 0.3f }, { "release", 0.6f }, { "lfo2On", 1 }, { "lfo2Target", 5 }, { "lfo2Shape", 5 },
                          { "lfo2Rate", 1.5f }, { "lfo2Depth", 0.3f }, { "ambientOn", 1 }, { "ambientMix", 0.25f } } },
            // tabla "Formant Sweep" con movimiento aleatorio suave del LFO 2
        { "Acid Step Line", "Sequence", { { "voiceMode", 2 }, { "glide", 60.0f }, { "wave", 2 }, { "cutoff", 600.0f }, { "resonance", 0.8f }, { "filterEnv", 0.5f },
                          { "filterDecay", 0.25f }, { "filterSustain", 0.0f }, { "attack", 0.001f }, { "sustain", 1.0f }, { "release", 0.05f }, { "tubeOn", 1 },
                          { "tubeDrive", 15.0f }, { "seqOn", 1 }, { "seqSteps", 16 }, { "seqRate", 3 }, { "seqGate", 0.5f }, { "arpBpm", 128.0f },
                          { "stepNote3", 12 }, { "stepTie4", 1 }, { "stepNote5", 3 }, { "stepNote7", 0 }, { "stepOn8", 0 }, { "stepNote9", 7 }, { "stepTie9", 1 },
                          { "stepNote10", 10 }, { "stepNote12", -12 }, { "stepNote14", 12 }, { "stepTie14", 1 }, { "stepNote15", 5 }, { "stepOn16", 0 } } },
            // patrón propio de 16 pasos con ties en legato (slides tipo TB-303); pulsar una nota grave (C2–C3)
        { "Irregular 7-Step", "Sequence", { { "wave", 3 }, { "pulseWidth", 0.3f }, { "attack", 0.002f }, { "decay", 0.15f }, { "sustain", 0.2f }, { "release", 0.1f },
                          { "cutoff", 1800.0f }, { "resonance", 0.35f }, { "filterEnv", 0.35f }, { "filterDecay", 0.2f }, { "seqOn", 1 }, { "seqSteps", 7 },
                          { "seqRate", 3 }, { "seqGate", 0.6f }, { "seqLatch", 1 }, { "arpBpm", 120.0f }, { "stepNote2", 7 }, { "stepNote3", 12 }, { "stepNote4", 3 },
                          { "stepNote5", 10 }, { "stepNote6", 7 }, { "stepNote7", 15 }, { "echoOn", 1 }, { "echoSync", 2 }, { "echoFeedback", 0.45f },
                          { "echoMix", 0.3f } } },
            // compás irregular: 7 pasos contra 4/4 (el patrón se desplaza cada compás); eco a corchea con puntillo
        // ---- Drums / FX (investigación 2026-10-20). Una voz por preset: tocar en la nota indicada.
        // Frecuencias del TR-808: DAFx-14 (bombo, dafx14.fau.de), ICMC-SMC-2014 (hats/cowbell, speech.di.uoa.gr), scrapbox.io/0b5vr/TR-808
        // (toms, caja, rimshot, claves; dice proceder del service manual, no leído). Recetas de Sound On Sound "Synth Secrets".
        // pitchEnvDecay es una constante de tiempo τ (63 %): "salto en ≈6 ms" -> τ ≈ 2 ms. Tiempos de decay sin medida publicada.
        { "808-Style Kick", "Drums", { { "wave", 0 }, { "attack", 0.001f }, { "decay", 0.8f }, { "sustain", 0.0f }, { "release", 0.8f },
                          { "pitchEnvAmount", 12.0f }, { "pitchEnvDecay", 2.0f }, { "filterOn", 0 } } },
            // tocar G1 (49,0 Hz; centro del bridged-T ≈ 49,5 Hz, +18 ct). Verificado: oscilador casi senoidal, sube > 1 octava ≈ 6 ms al golpe
        { "808-Style Long Boom", "Drums", { { "wave", 0 }, { "attack", 0.001f }, { "decay", 2.0f }, { "sustain", 0.0f }, { "release", 2.0f },
                          { "pitchEnvAmount", 1.0f }, { "pitchEnvDecay", 133.0f }, { "tubeOn", 1 }, { "tubeDrive", 6.0f }, { "filterOn", 0 } } },
            // tocar G1. arxiv.org/html/2502.07524: f0 49,48 Hz, barrido inicial ≈ 1 st, ataque ≈ 0,4 s y cola de segundos. tubeDrive estimado
        { "909-Style Kick", "Drums", { { "wave", 1 }, { "noiseLevel", 0.15f }, { "attack", 0.001f }, { "decay", 0.5f }, { "sustain", 0.0f }, { "release", 0.4f },
                          { "pitchEnvAmount", 24.0f }, { "pitchEnvDecay", 13.0f }, { "filterOn", 0 } } },
            // tocar A1. Verificado: un oscilador con barrido descendente (perfectcircuit.com, clon Hexinverter Mutant BD9); cifras sin medir
        { "808-Style Snare", "Drums", { { "wave", 0 }, { "osc2Wave", 0 }, { "osc2Octave", 1 }, { "osc1Level", 0.8f }, { "osc2Level", 0.5f }, { "noiseLevel", 0.7f },
                          { "filterMode", 3 }, { "cutoff", 200.0f }, { "attack", 0.001f }, { "decay", 0.18f }, { "sustain", 0.0f }, { "release", 0.18f },
                          { "pitchEnvAmount", 2.0f }, { "pitchEnvDecay", 3.3f } } },
            // tocar A#3. Verificado: tonos 238 + 476 Hz y ruido blanco por HPF. El HPF afecta también a los tonos (falta filtro solo para ruido)
        { "808-Style Tom", "Drums", { { "wave", 0 }, { "noiseLevel", 0.1f }, { "noiseColor", 1 }, { "attack", 0.001f }, { "decay", 0.4f }, { "sustain", 0.0f },
                          { "release", 0.4f }, { "pitchEnvAmount", 2.0f }, { "pitchEnvDecay", 10.0f } } },
            // tocar F#2 / C#3 / F#3 (90 / 135 / 185 Hz verificados); ruido rosa y leve bend
        { "808-Style Conga", "Drums", { { "wave", 0 }, { "attack", 0.001f }, { "decay", 0.2f }, { "sustain", 0.0f }, { "release", 0.2f },
                          { "pitchEnvAmount", 2.0f }, { "pitchEnvDecay", 5.0f } } },
            // tocar F#3 / B3 / G4 (185 / 250 / 400 Hz verificados); mismo circuito que los toms sin ruido
        { "808-Style Rimshot", "Drums", { { "wave", 0 }, { "osc2Wave", 0 }, { "osc2Octave", 1 }, { "osc2Semi", 10 }, { "osc2Fine", 48.0f }, { "osc1Level", 0.7f },
                          { "osc2Level", 0.7f }, { "filterMode", 3 }, { "cutoff", 300.0f }, { "attack", 0.001f }, { "decay", 0.03f }, { "sustain", 0.0f },
                          { "release", 0.03f }, { "tubeOn", 1 }, { "tubeDrive", 6.0f } } },
            // tocar A#4. Verificado: 455 + 1667 Hz (+22,48 st = 12 + 10 + 0,48) por HPF. Corte, decay y saturación estimados
        { "808-Style Claves", "Drums", { { "wave", 0 }, { "attack", 0.001f }, { "decay", 0.04f }, { "sustain", 0.0f }, { "release", 0.04f }, { "filterOn", 0 } } },
            // tocar D#7 (2489 Hz; 2500 Hz verificado). SOS: decay más corto que el cowbell (40 ms estimado)
        { "808-Style Cowbell", "Drums", { { "wave", 3 }, { "osc2Wave", 3 }, { "osc2Semi", 7 }, { "osc2Fine", -20.0f }, { "osc1Level", 0.8f }, { "osc2Level", 0.8f },
                          { "filterMode", 2 }, { "cutoff", 2640.0f }, { "resonance", 0.3f }, { "attack", 0.001f }, { "decay", 0.3f }, { "sustain", 0.0f }, { "release", 0.3f } } },
            // tocar C#5. Verificado: cuadradas 540 + 800 Hz (6,80 st), pasa-banda; SOS: BP 12 dB/oct a 2,64 kHz con algo de resonancia
        { "808-Style Closed Hat", "Drums", { { "wave", 3 }, { "osc2Wave", 3 }, { "osc2Semi", 7 }, { "osc2Fine", -20.0f }, { "osc1Level", 0.7f }, { "osc2Level", 0.7f },
                          { "complexOn", 1 }, { "modRatio", 0.38f }, { "fmIndex", 4.0f }, { "amDepth", 1.0f }, { "noiseLevel", 0.3f }, { "filterMode", 3 },
                          { "cutoff", 7100.0f }, { "resonance", 0.2f }, { "attack", 0.001f }, { "decay", 0.05f }, { "sustain", 0.0f }, { "release", 0.05f } } },
            // tocar C#5. Verificado: 6 cuadradas (205,3/369,6/304,4/522,7/540/800 Hz), BPF 3440 y 7100 Hz + HPF. Aquí: aproximación
            // con 2 osciladores, ring/FM y ruido (el 808 no usa ruido). Falta el banco de 6 cuadradas
        { "808-Style Open Hat", "Drums", { { "wave", 3 }, { "osc2Wave", 3 }, { "osc2Semi", 7 }, { "osc2Fine", -20.0f }, { "osc1Level", 0.7f }, { "osc2Level", 0.7f },
                          { "complexOn", 1 }, { "modRatio", 0.38f }, { "fmIndex", 4.0f }, { "amDepth", 1.0f }, { "noiseLevel", 0.3f }, { "filterMode", 3 },
                          { "cutoff", 7100.0f }, { "resonance", 0.2f }, { "attack", 0.001f }, { "decay", 0.45f }, { "sustain", 0.0f }, { "release", 0.45f } } },
            // tocar C#5. Igual que el closed hat con decay largo (0,45 s estimado)
        { "FM Ride Cymbal", "Drums", { { "wave", 3 }, { "complexOn", 1 }, { "modRatio", 0.4f }, { "fmIndex", 10.0f }, { "filterMode", 3 }, { "cutoff", 2640.0f },
                          { "filterEnv", 0.58f }, { "filterAttack", 0.2f }, { "filterDecay", 3.7f }, { "filterSustain", 0.0f }, { "attack", 0.001f },
                          { "decay", 0.75f }, { "sustain", 0.0f }, { "release", 0.75f } } },
            // tocar D#7. SOS "Synthesizing realistic cymbals": portadora cuadrada ≈ 2,5 kHz, moduladora ≈ 1 kHz (0,4) a FM máxima, HP 12 dB
            // a 2,64 kHz que abre en 200 ms y cierra en 3,7 s, decay ≈ 0,75 s. filterEnv = log2(20000/2640)/5 = 0,58. Moduladora seno, no pulso
        { "Analog Hand Clap", "Drums", { { "osc1Level", 0.0f }, { "noiseLevel", 1.0f }, { "filterMode", 2 }, { "cutoff", 1000.0f }, { "resonance", 0.3f },
                          { "attack", 0.001f }, { "decay", 0.12f }, { "sustain", 0.0f }, { "release", 0.3f }, { "echoOn", 1 }, { "echoTime", 11.0f },
                          { "echoFeedback", 0.35f }, { "echoMix", 0.4f }, { "ambientOn", 1 }, { "ambientMix", 0.25f }, { "reverbDecay", 0.8f } } },
            // tocar cualquier nota. Verificado (attackmagazine.com, sweetwater.com): ruido blanco por pasa-banda (~1 kHz) con ráfagas rápidas y
            // cola larga. El eco de 11 ms sustituye a las ráfagas (falta envolvente multi-ráfaga)
        { "Ocean Surf", "Nature", { { "osc1Level", 0.0f }, { "noiseLevel", 1.0f }, { "cutoff", 400.0f }, { "resonance", 0.2f }, { "filterEnv", 0.5f },
                          { "filterAttack", 2.0f }, { "filterDecay", 4.0f }, { "filterSustain", 0.2f }, { "attack", 2.0f }, { "decay", 2.0f }, { "sustain", 0.6f },
                          { "release", 4.0f }, { "echoOn", 1 }, { "echoTime", 350.0f }, { "echoFeedback", 0.3f }, { "echoMix", 0.3f }, { "ambientOn", 1 },
                          { "ambientMix", 0.4f }, { "reverbDecay", 3.0f } } },
            // mantener una nota = una ola. Oxygène II: olas con EMS Synthi AKS y delay de cinta Revox (Wikipedia, newagemusic.guide). Cifras estimadas
        { "Gusting Wind", "Nature", { { "osc1Level", 0.0f }, { "noiseLevel", 1.0f }, { "noiseColor", 1 }, { "filterMode", 2 }, { "cutoff", 800.0f }, { "resonance", 0.7f },
                          { "filterKey", 1.0f }, { "filterEnv", 0.3f }, { "filterAttack", 1.5f }, { "filterDecay", 3.0f }, { "filterSustain", 0.5f }, { "attack", 1.5f },
                          { "decay", 1.0f }, { "sustain", 1.0f }, { "release", 3.0f }, { "lfoOn", 1 }, { "lfoTarget", 2 }, { "lfoRate", 0.2f }, { "lfoDepth", 0.4f },
                          { "ambientOn", 1 }, { "ambientMix", 0.3f } } },
            // C3–C6: la tecla mueve el filtro. Receta: ruido rosa por filtro resonante con ráfagas (LFO lento aquí; falta S&H). Cifras estimadas
        { "Sci-Fi Laser Zap", "FX", { { "wave", 0 }, { "attack", 0.001f }, { "decay", 0.25f }, { "sustain", 0.0f }, { "release", 0.1f }, { "pitchEnvAmount", 36.0f },
                          { "pitchEnvDecay", 40.0f }, { "echoOn", 1 }, { "echoTime", 120.0f }, { "echoFeedback", 0.4f }, { "echoMix", 0.3f } } },
            // tocar C5. Cualitativo verificado (seno, ataque rápido, barrido descendente); +36 st y τ 40 ms estimados
        { "Distant Thunder", "Nature", { { "osc1Level", 0.0f }, { "noiseLevel", 1.0f }, { "noiseColor", 1 }, { "cutoff", 120.0f }, { "resonance", 0.1f }, { "filterEnv", 0.6f },
                          { "filterAttack", 0.005f }, { "filterDecay", 1.5f }, { "filterSustain", 0.0f }, { "attack", 0.005f }, { "decay", 2.0f }, { "sustain", 0.0f },
                          { "release", 4.0f }, { "tubeOn", 1 }, { "tubeDrive", 9.0f }, { "ambientOn", 1 }, { "ambientMix", 0.5f }, { "reverbDecay", 4.0f } } },
            // cualquier nota. Ninguna cifra publicada (deepai.org: ninguna síntesis de trueno iguala a la grabación). Todo estimado
    };
    return list;
}
