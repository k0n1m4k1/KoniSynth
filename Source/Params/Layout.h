// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

static int midiNoteFromText (const juce::String& text)
{
    const auto t = text.trim().toUpperCase();
    if (t.isEmpty())
        return 60;
    if (juce::CharacterFunctions::isDigit (t[0]))
        return juce::jlimit (0, 127, t.getIntValue());
    static const int pcs[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    if (t[0] < 'A' || t[0] > 'G')
        return 60;
    auto pc = pcs[t[0] - 'A'];
    int i = 1;
    if (i < t.length() && t[i] == '#') { ++pc; ++i; }
    else if (i < t.length() && t[i] == 'B' && t.length() > 2) { --pc; ++i; }
    return juce::jlimit (0, 127, (t.substring (i).getIntValue() + 1) * 12 + pc);
}

static APVTS::ParameterLayout createLayout()
{
    using namespace juce;

    auto skewed = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };

    auto seconds = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 0.01f ? String (v * 1000.0f, 1) + " ms" : v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; }), true);
    auto percent = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (roundToInt (v * 100.0f)) + " %"; }));
    auto hertz = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (v, 2) + " Hz"; }));
    auto millis = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (roundToInt (v)) + " ms"; }));
    auto freq = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; }));
    auto decibels = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (v, 1) + " dB"; }));

    APVTS::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    // Suma a mono al final de la cadena (comprobar la compatibilidad mono de la mezcla)
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "monoOut", 1 }, "Mono Output", false));
    // Mute de las fuentes del Synth (pestañas Osc 1 / Wavetable / Sample / String): apagados por defecto, los presets no cambian
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "osc1Mute", 1 },   "Osc1 Mute", false));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "wtMute", 1 },     "Wavetable Mute", false));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "sampleMute", 1 }, "Sample Mute", false));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "stringMute", 1 }, "String Mute", false));
    // N64 (libultra + microcódigo de audio): fuente, tasas, VADPCM, envolvente de libultra (tiempos en ms, volúmenes 0-127),
    // canal (volumen, paneo 0-127), efecto de libultra y su envío (fxMix 0-127)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "n64Source", 1 }, "N64 Source", StringArray { "Square", "Saw", "Sine", "Strings", "Choir", "Bass", "User Sample" }, 3));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "n64SourceRate", 1 }, "N64 Sample Rate", StringArray { "8 kHz", "11 kHz", "16 kHz", "22 kHz", "32 kHz" }, 3));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "n64OutputRate", 1 }, "N64 Output Rate", StringArray { "22.05 kHz", "32 kHz", "44.1 kHz" }, 1));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "n64Adpcm", 1 }, "N64 VADPCM", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "n64Dac", 1 }, "N64 DAC", StringArray { "Hold", "Smooth" }, 0)); // como snesDac
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "n64Attack", 1 },  "N64 Attack Time", NormalisableRange<float> (0.0f, 5000.0f, 0.0f, 0.3f), 5.0f, millis));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "n64Decay", 1 },   "N64 Decay Time", NormalisableRange<float> (0.0f, 5000.0f, 0.0f, 0.3f), 0.0f, millis));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "n64Release", 1 }, "N64 Release Time", NormalisableRange<float> (0.0f, 5000.0f, 0.0f, 0.3f), 200.0f, millis));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "n64AttackVolume", 1 }, "N64 Attack Volume", 0, 127, 127));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "n64DecayVolume", 1 },  "N64 Decay Volume", 0, 127, 127));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "n64Volume", 1 }, "N64 Volume", 0, 127, 100));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "n64Pan", 1 },    "N64 Pan", 0, 127, 64));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "n64Spread", 1 }, "N64 Spread", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "n64Fx", 1 },     "N64 Effect", StringArray { "None", "Small Room", "Big Room", "Echo", "Chorus", "Flange" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "n64FxMix", 1 },  "N64 Effect Mix", 0, 127, 40));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "n64Velocity", 1 }, "N64 Velocity", true));
    // Salidas reales de las consolas estéreo (verificadas: Wikipedia "Sega Genesis", Pan Docs, fullsnes): la mono es L+R
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mdOutput", 1 },   "MD Output", StringArray { "Stereo (headphones / Model 2 AV)", "Mono (Model 1 AV, RF)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "snesOutput", 1 }, "SNES Output", StringArray { "Stereo (AV)", "Mono (RF)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipOutput", 1 }, "GB Output", StringArray { "Headphones (stereo)", "Speaker (mono)" }, 0));

    // Motor de sonido (excluyentes). Sustituye a thereminOn: estados (setStateInformation) y presets de usuario
    // (PresetManager::targetValues) antiguos se migran
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "engine", 1 }, "Engine", StringArray { "Synth", "Theremin", "Casio VL-1", "Stylophone", "Omnichord", "Drum Machine", "FM 6-Op", "8-Bit (NES / Game Boy)", "Polivoks", "ANS", "Mega Drive", "SNES", "N64", "OPL (AdLib / Sound Blaster)", "Roland MT-32" }, 0));

    // Oscilador + envolvente. Drift: deriva 1/f por voz, común y de Osc 2 (modelo y cifras: Synth.h, comentario de updateDrift)
    // Tri-Saw (triángulo/sierra del Minimoog) y después las mezclas Saw+Pulse, Saw+Tri, Tri+Pulse, Saw+Tri+Pulse (Prophet-5) y
    // Saw+Pulse (Juno) añadidas al final de wave, osc2Wave, osc3Wave y modWave en la 1.0.0-alpha: los presets guardan el
    // índice y no cambian; las automatizaciones grabadas sí (JUCE normaliza como índice / (n - 1))
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "wave", 1 }, "Wave", StringArray { "Sine", "Triangle", "Saw", "Square", "Tri-Saw", "Saw+Pulse", "Saw+Tri", "Tri+Pulse", "Saw+Tri+Pulse", "Saw+Pulse (Juno)" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 },  "Attack",  skewed (0.0002f, 5.0f, 0.2f), 0.01f, seconds));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "decay", 1 },   "Decay",   skewed (0.0002f, 20.0f, 0.2f), 0.1f,  seconds));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", 1 }, "Sustain", NormalisableRange<float> (0.0f, 1.0f), 0.8f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release", skewed (0.0002f, 20.0f, 0.4f), 0.3f,  seconds));
    // Curva de las envolventes del Synth (amplitud y filtro): Linear = la de siempre (presets sin cambios); Analog RC = CEM3310
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "envCurve", 1 }, "Env Curve", StringArray { "Linear", "Analog RC" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drift", 1 },   "Drift",   NormalisableRange<float> (0.0f, 1.0f), 0.2f, percent));

    // Sustractivo: PW/PWM (ondas cuadradas de Osc 1 y Osc 2; PWM desde el LFO), Osc 2, sub-oscilador, ruido y mezclador
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "osc1Level", 1 },   "Osc1 Level",  NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pulseWidth", 1 },  "Pulse Width", NormalisableRange<float> (0.05f, 0.95f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pwmDepth", 1 },    "PWM Depth",   NormalisableRange<float> (0.0f, 0.45f), 0.0f, percent));
    // Las formas en el orden de wave y, al final, Wavetable (tabla propia, osc2WtTable) y Sample (fichero propio, osc2SamplePath).
    // Hasta el formato de preset 3, Wavetable y Sample iban 4.º y 5.º: PresetManager::migrateValues y setStateInformation
    // remapean. El motor usa sus códigos (osc2WaveFromChoice, Dsp/OscillatorCore.h)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "osc2Wave", 1 },    "Osc2 Wave",   StringArray { "Sine", "Triangle", "Saw", "Square", "Tri-Saw", "Saw+Pulse", "Saw+Tri", "Tri+Pulse", "Saw+Tri+Pulse", "Saw+Pulse (Juno)", "Wavetable", "Sample" }, 2));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "osc2Octave", 1 },  "Osc2 Octave", -2, 2, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "osc2Semi", 1 },    "Osc2 Semi",   -12, 12, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "osc2Fine", 1 },    "Osc2 Fine",   NormalisableRange<float> (-50.0f, 50.0f), 0.0f,
                                                   parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ct"; }))));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "osc2Level", 1 },   "Osc2 Level",  NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "osc2Sync", 1 },    "Osc2 Sync", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "subLevel", 1 },    "Sub Level",   NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "subOctave", 1 },   "Sub Octave",  StringArray { "-1 oct", "-2 oct" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "noiseLevel", 1 },  "Noise Level", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    // LFSR: ruido digital de 15 bits de los chips de 8 bits (NES 2A03, nesdev.org/wiki/APU_Noise): realimentación bit0 XOR bit1
    // (modo largo, 32767 pasos) o bit0 XOR bit6 (modo corto, 93 pasos: tonal). Reloj ligado a la nota (diseño propio:
    // largo = f·8; corto = f·93, para que el tono del modo corto sea la nota tocada)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "noiseColor", 1 },  "Noise Color", StringArray { "White", "Pink", "LFSR", "LFSR Short" }, 0));

    // Fuente Sample (mezclador del motor Synth). El fichero no es un parámetro: va en el estado (propiedad samplePath)
    auto noteName = AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        { return MidiMessage::getMidiNoteName (v, true, true, 4); })
        .withValueFromStringFunction ([] (const String& t) { return midiNoteFromText (t); });
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "sampleLevel", 1 }, "Sample Level", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "sampleRoot", 1 },  "Sample Root", 0, 127, 60, noteName));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "sampleStart", 1 }, "Sample Start", NormalisableRange<float> (0.0f, 0.99f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "sampleLoop", 1 },  "Sample Loop", false));
    // Modo LA (síntesis "Linear Arithmetic" del Roland MT-32/D-50: transitorio PCM corto + parciales sintetizados en el sostenido):
    // Length > 0 corta el sample a ese tiempo con un fundido de 10 ms; 0 = sample completo
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "sampleLength", 1 }, "Sample Length", skewed (0.0f, 2000.0f, 150.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v < 0.5f ? String ("Full") : String (roundToInt (v)) + " ms"; }))));

    // Oscilador de wavetable (mezclador del motor Synth): tabla incluida o fichero del usuario (ruta en el estado, wtPath)
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "wtLevel", 1 },    "Wavetable Level", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "wtTable", 1 },    "Wavetable",
        StringArray { "Basic Shapes", "PWM Sweep", "Harmonic Sweep", "Sync Sweep", "Formant Sweep", "User File" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "wtPosition", 1 }, "Wavetable Position", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));

    // Cuerda pulsada (Karplus-Strong extendido) en el mezclador del motor Synth: clave, guitarra, arpa
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "stringLevel", 1 },  "String Level", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "stringDecay", 1 },  "String Decay", skewed (0.1f, 20.0f, 2.0f), 3.0f, seconds));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "stringBright", 1 }, "String Brightness", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
    // Pastilla magnética (posición desde el puente; 0 = sin pastilla) y pérdida de agudos en el lazo (0 = la de siempre)
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "stringPickup", 1 }, "String Pickup", NormalisableRange<float> (0.0f, 0.5f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v < 0.005f ? String ("Off") : String (roundToInt (v * 100.0f)) + " %"; }))));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "stringExcite", 1 }, "String Excite", StringArray { "Noise", "Pick" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "stringDamping", 1 }, "String Damping", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "stringPick", 1 },   "String Pick Position", NormalisableRange<float> (0.02f, 0.5f), 0.13f, percent));

    // Entrada de audio (micro/línea): ganancia, monitor (a la cadena de efectos) y vocoder de canales
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "inputGain", 1 },    "Input Gain", NormalisableRange<float> (-12.0f, 36.0f), 0.0f, decibels));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "inputMonitor", 1 }, "Input Monitor", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "vocoderOn", 1 },    "Vocoder On", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "vocoderBank", 1 },  "Vocoder Bank", StringArray { "VP-330 10", "16 Band", "32 Band" }, 1));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "vocoderMix", 1 },   "Vocoder Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "vocoderAttack", 1 }, "Vocoder Attack", skewed (0.5f, 50.0f, 5.0f), 5.0f, millis));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "vocoderRelease", 1 }, "Vocoder Release", skewed (5.0f, 500.0f, 50.0f), 50.0f, millis));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "vocoderFormant", 1 }, "Vocoder Formant", NormalisableRange<float> (-12.0f, 12.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; }))));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "vocoderSibilance", 1 }, "Vocoder Sibilance", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));

    // Unísono: N osciladores por voz, desafinados ±Detune cents y abiertos en estéreo ±Spread
    auto cents = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (v, 1) + " ct"; }));
    l.add (std::make_unique<AudioParameterInt>   (ParameterID { "unisonVoices", 1 }, "Unison Voices", 1, 7, 1));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "unisonDetune", 1 }, "Unison Detune", NormalisableRange<float> (0.0f, 50.0f), 12.0f, cents));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "unisonSpread", 1 }, "Unison Spread", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    // Paneo por voz (Synth): cada nota recibe una posición en ±Voice Pan al empezar: alterna L/R, sigue al teclado
    // (C2 izquierda .. C6 derecha) o al azar. Ley equal-power con ganancia 1 en el centro; 0 = todas al centro (como antes)
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "voicePan", 1 }, "Voice Pan", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voicePanMode", 1 }, "Voice Pan Mode", StringArray { "Alternate", "Keyboard", "Random" }, 0));

    // Media: soporte de grabación al final de la cadena (ver struct Media)
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mediaOn", 1 },    "Media On", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mediaType", 1 },  "Media Type", StringArray { "Cassette", "Vinyl", "Reel 38", "Reel 19", "Reel 9.5", "Underwater" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "mediaAge", 1 },   "Media Age",   NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "mediaNoise", 1 }, "Media Noise", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "mediaWow", 1 },   "Media Wow",   NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "mediaDrive", 1 }, "Media Drive", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));

    // Chorus / ensemble (ver struct Chorus para el origen de cada cifra)
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "chorusOn", 1 },    "Chorus On", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chorusMode", 1 },  "Chorus Mode", StringArray { "Chorus I", "Chorus II", "Chorus I+II", "Ensemble" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "chorusMix", 1 },   "Chorus Mix",   NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "chorusDepth", 1 }, "Chorus Depth", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));

    // Filtro ladder. Env: ±5 octavas a ±100 %; Key: seguimiento de teclado (100 % = 1 oct/oct, referencia C4)
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "filterOn", 1 },     "Filter On", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "filterMode", 1 },   "Filter Mode", StringArray { "LP 24", "LP 12", "BP 12", "HP 12",
        "SVF LP", "SVF BP", "SVF HP", "SVF Notch", "MS-20 LP", "MS-20 HP", "Comb +", "Comb -", "Dual BP", "Dual LP" }, 0)); // ladder + Dsp/Filters.h
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "filterSpread", 1 }, "Filter Spread", NormalisableRange<float> (0.0f, 4.0f), 1.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " oct"; }))));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "cutoff", 1 },       "Cutoff",    skewed (20.0f, 20000.0f, 1000.0f), 20000.0f, freq));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "resonance", 1 },    "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "filterDrive", 1 },  "Filter Drive", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "filterEnv", 1 },    "Filter Env",   NormalisableRange<float> (-1.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "filterAttack", 1 }, "Filter Attack", skewed (0.0002f, 5.0f, 0.2f), 0.005f, seconds));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "filterDecay", 1 },  "Filter Decay",  skewed (0.0002f, 20.0f, 0.4f), 0.3f, seconds));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "filterKey", 1 },    "Filter Key",    NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    // Envolvente de filtro ADSR completa (antes AD: sustain 0, release = decay) y velocity -> cantidad de envolvente
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "filterSustain", 1 }, "Filter Sustain", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "filterRelease", 1 }, "Filter Release", skewed (0.0002f, 20.0f, 0.4f), 0.3f, seconds));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "velToFilter", 1 },   "Vel To Filter",  NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));

    // Theremin: sustituye a las voces del teclado. Pitch/Volume los escribe el pad XY (automatizables, fuera de los presets)
    auto octaves = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (v, 1) + " oct"; }));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "thereminRange", 1 },  "Theremin Range", NormalisableRange<float> (1.0f, 5.0f), 3.0f, octaves));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "thereminGlide", 1 },  "Theremin Glide", skewed (1.0f, 1000.0f, 100.0f), 60.0f, millis));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "thereminTone", 1 },   "Theremin Tone",  NormalisableRange<float> (0.0f, 1.0f), 0.25f, percent));
    // Modelo físico: Field = d0 (más alto = campo más lineal), Lock = rango de enganche de Adler, Zero = distancia de batido cero
    auto centimetres = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (v, 1) + " cm"; }));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "thereminModel", 1 }, "Theremin Model", StringArray { "Physical", "Ideal" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "thereminField", 1 }, "Theremin Field", NormalisableRange<float> (2.0f, 40.0f), 10.0f, centimetres));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "thereminLock", 1 },  "Theremin Lock",  NormalisableRange<float> (0.0f, 60.0f), 12.0f, hertz));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "thereminZero", 1 },  "Theremin Zero",  NormalisableRange<float> (20.0f, 90.0f), 60.0f, centimetres));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "thereminPitch", 1 },  "Theremin Pitch", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "thereminVolume", 1 }, "Theremin Volume", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));

    // Casio VL-1: sustituye a las voces (monofónico). Dígitos 0..9 como el código ADSR de 8 cifras del VL-1
    // (sonido, A, D, SL, ST, R, V, T). Tempo -9..9 del VL-1: tick = 5,5 ms + (9 - T)·0,98 ms
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "vl1Sound", 1 },  "VL-1 Sound",
        StringArray { "Piano", "Fantasy", "Violin", "Flute", "Guitar 1", "Guitar 2", "Horn", "Electro 1", "Electro 2", "Electro 3" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "vl1Octave", 1 }, "VL-1 Octave", -1, 1, 0));
    struct Digit { const char* id; const char* name; int def; }; // valores por defecto: Piano 00453200
    for (const auto& d : { Digit { "vl1Attack", "VL-1 Attack", 0 }, Digit { "vl1Decay", "VL-1 Decay", 4 },
                           Digit { "vl1SustainLevel", "VL-1 Sustain Level", 5 }, Digit { "vl1SustainTime", "VL-1 Sustain Time", 3 },
                           Digit { "vl1Release", "VL-1 Release", 2 }, Digit { "vl1Vibrato", "VL-1 Vibrato", 0 },
                           Digit { "vl1Tremolo", "VL-1 Tremolo", 0 } })
        l.add (std::make_unique<AudioParameterInt> (ParameterID { d.id, 1 }, d.name, 0, 9, d.def));
    {
        // Ranuras del banco de usuario (Engines/UserRhythms.h) tras los 10 de fábrica (VL1::patterns, en este orden)
        StringArray rhythms { "Off", "March", "Waltz", "4 Beat", "Swing", "Rock 1", "Rock 2", "Bossanova", "Samba", "Rhumba", "Beguine" };
        for (int i = 1; i <= UserRhythms::numSlots; ++i) rhythms.add ("User " + String (i));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "vl1Rhythm", 1 }, "VL-1 Rhythm", rhythms, 0));
    }
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "vl1Tempo", 1 },   "VL-1 Tempo", -9, 9, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "vl1Balance", 1 }, "VL-1 Balance", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));

    // Stylophone: sustituye a las voces (monofónico). Modelos: el de 1968 (registro = rango de 20 teclas de cada versión:
    // Standard, Bass, Treble) y el Gen X-2 (experimental)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "styloModel", 1 },   "Stylophone Model", StringArray { "1968", "Gen X-2 (experimental)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "styloRegister", 1 }, "Stylophone Register", StringArray { "Standard (A2-E4)", "Bass (A1-E3)", "Treble (A3-E5)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "styloRange", 1 },   "Stylophone Range", StringArray { "Original (20 keys)", "Full keyboard" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "styloVibrato", 1 }, "Stylophone Vibrato", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "styloVibDepth", 1 }, "Stylophone Vibrato Depth", NormalisableRange<float> (0.0f, 100.0f), 30.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ct"; }))));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "styloTuning", 1 },  "Stylophone Tuning", StringArray { "Vintage", "Equal" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "styloOutput", 1 },  "Stylophone Output", StringArray { "Speaker", "Line" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "styloClick", 1 },   "Stylophone Click", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    // Gen X-2 (2025, experimental): mandos del manual oficial; rangos estimados (el manual no da cifras)
    auto decaySeconds = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 0.0005f ? String ("Off (sustain)") : v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; }), true);
    auto genxSemis = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; }));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "genxPwm", 1 },     "Gen X-2 PWM", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "genxSub1", 1 },    "Gen X-2 Sub -1", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "genxSub2", 1 },    "Gen X-2 Sub -2", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxAttack", 1 },  "Gen X-2 Attack", skewed (0.001f, 2.0f, 0.1f), 0.001f, seconds));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxDecay", 1 },   "Gen X-2 Decay", skewed (0.0f, 4.0f, 0.5f), 0.0f, decaySeconds));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxPitch", 1 },   "Gen X-2 Env Pitch", NormalisableRange<float> (0.0f, 24.0f), 0.0f, genxSemis));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "genxLfoWave", 1 }, "Gen X-2 LFO Wave", StringArray { "Triangle", "Square" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxLfoRate", 1 }, "Gen X-2 LFO Rate", skewed (0.1f, 30.0f, 4.0f), 5.0f, hertz));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxLfoDepth", 1 }, "Gen X-2 LFO Depth", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxCutoff", 1 },  "Gen X-2 Cutoff", skewed (30.0f, 12000.0f, 1200.0f), 12000.0f, hertz));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxRes", 1 },     "Gen X-2 Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "genxDelay", 1 },   "Gen X-2 Delay On", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxTime", 1 },    "Gen X-2 Delay Time", skewed (40.0f, 400.0f, 150.0f), 200.0f, millis));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxRepeats", 1 }, "Gen X-2 Delay Repeats", NormalisableRange<float> (0.0f, 0.9f), 0.4f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "genxLevel", 1 },   "Gen X-2 Delay Level", NormalisableRange<float> (0.0f, 1.0f), 0.4f, percent));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "genxStrip", 1 },   "Gen X-2 Strip", StringArray { "Filter", "Osc", "LFO" }, 1));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "genxOctave", 1 },  "Gen X-2 Octave", -1, 1, 0));

    // Omnichord: por debajo de Split, botones de acorde; desde Split, las 13 zonas del arpa
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "omniSustain", 1 },    "Omnichord Sustain", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "omniHarpLevel", 1 },  "Omnichord Harp", NormalisableRange<float> (0.0f, 1.0f), 0.8f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "omniChordLevel", 1 }, "Omnichord Chord", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "omniBassLevel", 1 },  "Omnichord Bass", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "omniHarpOctave", 1 }, "Omnichord Harp Octave", -2, 1, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "omniSplit", 1 },      "Omnichord Split", 36, 84, 60, noteName));
    {
        StringArray rhythms { "Off" };
        for (const auto& r : omnichordPatterns()) rhythms.add (r.name);
        for (int i = 1; i <= UserRhythms::numSlots; ++i) rhythms.add ("User " + String (i)); // banco de usuario, al final
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "omniRhythm", 1 }, "Omnichord Rhythm", rhythms, 0));
    }
    auto bpm = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " BPM"; }));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "omniTempo", 1 },       "Omnichord Tempo", NormalisableRange<float> (60.0f, 240.0f, 1.0f), 100.0f, bpm));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "omniRhythmLevel", 1 }, "Omnichord Rhythm Level", NormalisableRange<float> (0.0f, 1.0f), 0.7f, percent));

    // Caja de ritmos: kit TR-808 / CR-78 desde el teclado (mapa GM) y patrones
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "drumKit", 1 }, "Drum Kit", StringArray { "Mini Pops 7 (1966)", "CR-78 (1978)", "TR-808 (1980)" }, 2)); // por año (DrumMachine.h kitFromParam)
    {
        StringArray patterns { "Off" };
        for (const auto& r : drumPatterns()) patterns.add (r.name);
        // Ranuras del banco de usuario (ficheros MIDI, Engines/UserRhythms.h), al final: los índices de fábrica no cambian
        for (int i = 1; i <= UserRhythms::numSlots; ++i) patterns.add ("User " + String (i));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "drumPattern", 1 }, "Drum Pattern", patterns, 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "drumPattern2", 1 }, "Drum Pattern 2", patterns, 0));
    }
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "drumTempo", 1 },  "Drum Tempo", NormalisableRange<float> (60.0f, 240.0f, 1.0f), 120.0f, bpm));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "drumSwing", 1 },  "Drum Swing", NormalisableRange<float> (0.0f, 0.5f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "drumTune", 1 },   "Drum Tune", NormalisableRange<float> (-12.0f, 12.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; }))));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "drumDecay", 1 },  "Drum Decay", NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "x" + String (std::exp2 (2.0f * v), 2); })
                                       .withValueFromStringFunction ([] (const String& t) // "x2.00" -> log2(2)/2 = 0,5
                                           { const auto m = t.retainCharacters ("0123456789.").getFloatValue();
                                             return m > 0.0f ? jlimit (-1.0f, 1.0f, 0.5f * std::log2 (m)) : 0.0f; })));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "drumTone", 1 },   "Drum Tone", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "drumSnappy", 1 }, "Drum Snappy", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "drumAccent", 1 }, "Drum Accent", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    // Velocidad MIDI continua (apagado = como el aparato: nivel normal o acento)
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "drumVelocity", 1 }, "Drum Velocity", false));

    // Efectos nuevos: flanger de BBD, desplazador de frecuencia, Leslie y muelles
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "flangerOn", 1 },    "Flanger On", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "flangerRate", 1 },  "Flanger Rate", skewed (0.07f, 10.0f, 1.0f), 0.3f, hertz));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "flangerRange", 1 }, "Flanger Range", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "flangerColor", 1 }, "Flanger Color", NormalisableRange<float> (0.0f, 0.95f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "flangerMatrix", 1 }, "Flanger Filter Matrix", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "flangerMix", 1 },   "Flanger Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "fshiftOn", 1 },     "Freq Shift On", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "fshiftHz", 1 },     "Freq Shift Amount",
        NormalisableRange<float> (-5000.0f, 5000.0f,
            [] (float, float, float x) { const auto u = 2.0f * x - 1.0f; return 5000.0f * u * std::abs (u); },          // cuadrática: fino cerca de 0
            [] (float, float, float v) { return 0.5f + 0.5f * std::copysign (std::sqrt (std::abs (v) / 5000.0f), v); }), 0.0f, hertz));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "fshiftBalance", 1 }, "Freq Shift Up/Down", NormalisableRange<float> (-1.0f, 1.0f), 1.0f));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "fshiftMix", 1 },    "Freq Shift Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "leslieOn", 1 },     "Leslie On", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "leslieSpeed", 1 },  "Leslie Speed", StringArray { "Stop", "Chorale", "Tremolo" }, 1));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "leslieDrive", 1 },  "Leslie Drive", NormalisableRange<float> (0.0f, 1.0f), 0.2f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "leslieMix", 1 },    "Leslie Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "springOn", 1 },     "Spring On", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "springDecay", 1 },  "Spring Decay", NormalisableRange<float> (1.2f, 4.0f), 3.0f, seconds));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "springMix", 1 },    "Spring Mix", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));

    // Válvula (post-mezcla, 2x oversampling). Bias: asimetría -> armónicos pares
    // Crush: reducción de resolución (bits) y de frecuencia de muestreo (sample & hold), sin filtro antialias: carácter de
    // chips de 8 bits y samplers antiguos. Antes de la válvula
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "crushOn", 1 },   "Crush On", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "crushBits", 1 }, "Crush Bits", NormalisableRange<float> (1.0f, 16.0f, 1.0f), 8.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " bit"; }))));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "crushRate", 1 }, "Crush Rate", skewed (500.0f, 48000.0f, 8000.0f), 22050.0f, freq));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "crushMix", 1 },  "Crush Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    // Drive: ganancia antes de cuantizar, compensada después. Las voces salen a ±0,2 (0,2 × velocity): sin ganancia, 4 bits
    // usarían 3 de los 16 niveles; con +12 dB (×4) la señal llega a ±0,8
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "crushDrive", 1 }, "Crush Drive", NormalisableRange<float> (0.0f, 24.0f), 12.0f, decibels));

    // Phaser de 4 etapas (arquitectura del EHX Small Stone: 4 etapas OTA; ehx.com, rockboard.de). Antes del chorus
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "phaserOn", 1 },       "Phaser On", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "phaserRate", 1 },     "Phaser Rate", skewed (0.05f, 10.0f, 1.0f), 0.5f, hertz));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "phaserDepth", 1 },    "Phaser Depth", NormalisableRange<float> (0.0f, 1.0f), 0.7f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "phaserFeedback", 1 }, "Phaser Color", NormalisableRange<float> (0.0f, 0.9f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "phaserMix", 1 },      "Phaser Mix", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));

    // Filtro de formantes (vocales cantadas): 3 pasa-banda en paralelo con F, ancho de banda y nivel de la tabla de formantes del
    // manual de Csound (csound.com/docs/manual/MiscFormants.html): voz Male = "bass", Female = "soprano"
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "formantOn", 1 },    "Formant On", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "formantVoice", 1 }, "Formant Voice", StringArray { "Male", "Female" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "formantVowel", 1 }, "Formant Vowel", NormalisableRange<float> (0.0f, 4.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            static const char* names[] = { "A", "E", "I", "O", "U" };
            const auto i = juce::jlimit (0, 4, roundToInt (v));
            return std::abs (v - (float) i) < 0.05f ? String (names[i]) : String (v, 2);
        }).withValueFromStringFunction ([] (const String& t)
        {
            const auto i = String ("AEIOU").indexOfChar (t.trim().toUpperCase()[0]);
            return i >= 0 ? (float) i : t.getFloatValue();
        })));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "formantShift", 1 }, "Formant Shift", NormalisableRange<float> (-12.0f, 12.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; }))));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "formantRate", 1 },  "Formant Morph Rate", skewed (0.02f, 10.0f, 1.0f), 0.5f, hertz));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "formantDepth", 1 }, "Formant Morph Depth", NormalisableRange<float> (0.0f, 2.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " v"; }))));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "formantMix", 1 },   "Formant Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));

    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "tubeOn", 1 },    "Tube On", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tubeDrive", 1 }, "Tube Drive", NormalisableRange<float> (0.0f, 24.0f), 9.0f, decibels));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tubeBias", 1 },  "Tube Bias",  NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    // Ecualizador paramétrico (Effects/Equalizer.h), tras la válvula: cortes Butterworth, estantes y dos picos
    {
        const StringArray slopes { "12 dB/oct", "24 dB/oct", "48 dB/oct", "6 dB/oct" }; // 6 dB/oct al final (un polo; índices guardados)
        auto q = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "Q " + String (v, 2); }));
        auto gainRange = NormalisableRange<float> (-18.0f, 18.0f);
        l.add (std::make_unique<AudioParameterBool>   (ParameterID { "eqOn", 1 },           "EQ On", false));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqLowCut", 1 },       "EQ Low Cut", skewed (20.0f, 1000.0f, 100.0f), 20.0f, freq));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "eqLowCutSlope", 1 },  "EQ Low Cut Slope", slopes, 1));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqLowFreq", 1 },      "EQ Low Freq", skewed (30.0f, 600.0f, 120.0f), 100.0f, freq));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqLowGain", 1 },      "EQ Low Gain", gainRange, 0.0f, decibels));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqMid1Freq", 1 },     "EQ Mid 1 Freq", skewed (80.0f, 4000.0f, 500.0f), 400.0f, freq));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqMid1Gain", 1 },     "EQ Mid 1 Gain", gainRange, 0.0f, decibels));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqMid1Q", 1 },        "EQ Mid 1 Q", skewed (0.3f, 10.0f, 1.5f), 1.0f, q));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqMid2Freq", 1 },     "EQ Mid 2 Freq", skewed (300.0f, 12000.0f, 2500.0f), 2500.0f, freq));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqMid2Gain", 1 },     "EQ Mid 2 Gain", gainRange, 0.0f, decibels));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqMid2Q", 1 },        "EQ Mid 2 Q", skewed (0.3f, 10.0f, 1.5f), 1.0f, q));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqHighFreq", 1 },     "EQ High Freq", skewed (1000.0f, 18000.0f, 6000.0f), 8000.0f, freq));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqHighGain", 1 },     "EQ High Gain", gainRange, 0.0f, decibels));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqHighCut", 1 },      "EQ High Cut", skewed (1000.0f, 20000.0f, 8000.0f), 20000.0f, freq));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "eqHighCutSlope", 1 }, "EQ High Cut Slope", slopes, 1));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "eqOutput", 1 },       "EQ Output", gainRange, 0.0f, decibels));
    }
    // Ampli + pantalla (Effects/AmpSim.h): tras COLOR, antes de la dinámica. Los mandos del tone stack, 0-1 como los potenciómetros
    {
        l.add (std::make_unique<AudioParameterBool>   (ParameterID { "ampOn", 1 },       "Amp On", false));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "ampModel", 1 },    "Amp Model", StringArray { "Bassman 59", "Twin 69", "JCM800", "Mark", "SLO-100", "VL501" }, 2));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ampGain", 1 },     "Amp Gain", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ampBass", 1 },     "Amp Bass", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ampMid", 1 },      "Amp Mid", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ampTreble", 1 },   "Amp Treble", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ampPresence", 1 }, "Amp Presence", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ampMaster", 1 },   "Amp Master", NormalisableRange<float> (0.0f, 1.0f), 0.4f, percent));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ampSag", 1 },      "Amp Sag", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ampOutput", 1 },   "Amp Output", NormalisableRange<float> (-24.0f, 12.0f), 0.0f, decibels));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "cabType", 1 },     "Cabinet", StringArray { "Off", "1x12 Open", "2x12 Open", "4x12 Closed", "1x15 Bass", "8x10 Bass", "User IR" }, 3));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "cabMix", 1 },      "Cabinet Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    }
    // Dinámica (Effects/Dynamics.h): modelador de transitorios y compresor tras el EQ; limitador al final, tras el Gain
    {
        auto ms2 = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v < 1.0f ? String (v, 2) + " ms" : v < 100.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; }));
        auto ratio = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return String (v, v < 10.0f ? 1 : 0) + ":1"; }));
        auto hpf = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v < 20.5f ? String ("Off") : String (roundToInt (v)) + " Hz"; }));
        l.add (std::make_unique<AudioParameterBool>   (ParameterID { "transOn", 1 },       "Transient On", false));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "transAttack", 1 },   "Transient Attack", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, percent));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "transSustain", 1 },  "Transient Sustain", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, percent));
        l.add (std::make_unique<AudioParameterBool>   (ParameterID { "compOn", 1 },        "Comp On", false));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "compMode", 1 },      "Comp Mode", StringArray { "VCA", "Opto", "FET" }, 0));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "compThreshold", 1 }, "Comp Threshold", NormalisableRange<float> (-60.0f, 0.0f), -18.0f, decibels));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "compRatio", 1 },     "Comp Ratio", skewed (1.0f, 20.0f, 4.0f), 4.0f, ratio));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "compAttack", 1 },    "Comp Attack", skewed (0.02f, 200.0f, 10.0f), 10.0f, ms2));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "compRelease", 1 },   "Comp Release", skewed (10.0f, 3000.0f, 200.0f), 150.0f, ms2));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "compKnee", 1 },      "Comp Knee", NormalisableRange<float> (0.0f, 24.0f), 6.0f, decibels));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "compMakeup", 1 },    "Comp Makeup", NormalisableRange<float> (0.0f, 24.0f), 0.0f, decibels));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "compMix", 1 },       "Comp Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "compScHpf", 1 },     "Comp SC HPF", skewed (20.0f, 500.0f, 100.0f), 20.0f, hpf));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "compSource", 1 },    "Comp Key", StringArray { "Internal", "Audio In" }, 0));
        l.add (std::make_unique<AudioParameterBool>   (ParameterID { "limOn", 1 },         "Limiter On", false));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "limCeiling", 1 },    "Limiter Ceiling", NormalisableRange<float> (-12.0f, 0.0f), -0.3f, decibels));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "limRelease", 1 },    "Limiter Release", skewed (1.0f, 1000.0f, 80.0f), 60.0f, ms2));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "limLookahead", 1 },  "Limiter Lookahead", StringArray { "Off", "1 ms", "2 ms", "5 ms" }, 0));
    }

    // LFO (vibrato: ±2 semitonos a depth 100 %; tremolo: amplitud mínima 1 - depth; filter: ±2 octavas de cutoff)
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "lfoOn", 1 },     "LFO On", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoTarget", 1 }, "LFO Target", StringArray { "Vibrato", "Tremolo", "Filter" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "lfoRate", 1 },   "LFO Rate",  skewed (0.02f, 100.0f, 4.0f), 5.0f, hertz));
    // Formas: S&H = valor aleatorio nuevo por ciclo; Random = aleatorio con transición suave (coseno) entre valores.
    // Sync: el periodo es una figura musical al tempo del host (o Arp BPM sin host); Free = Rate en Hz
    const StringArray lfoShapes { "Sine", "Triangle", "Saw", "Square", "S&H", "Random" };
    const StringArray syncDivisions { "Free", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoShape", 1 },  "LFO Shape", lfoShapes, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoSync", 1 },   "LFO Sync", syncDivisions, 0));
    // LFO 2: destinos propios (PWM y tono solo del Osc 2, como piden varias recetas)
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "lfo2On", 1 },     "LFO 2 On", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfo2Target", 1 }, "LFO 2 Target", StringArray { "Vibrato", "Tremolo", "Filter", "PWM", "Osc 2 Pitch", "WT Position" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfo2Shape", 1 },  "LFO 2 Shape", lfoShapes, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "lfo2Rate", 1 },   "LFO 2 Rate", skewed (0.02f, 100.0f, 4.0f), 0.5f, hertz));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfo2Sync", 1 },   "LFO 2 Sync", syncDivisions, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "lfo2Depth", 1 },  "LFO 2 Depth", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "fineTune", 1 },   "Fine Tune", NormalisableRange<float> (-100.0f, 100.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ct"; }))));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "lfoDepth", 1 },  "LFO Depth", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));

    // Escala de las notas de entrada (pie del teclado; Midi/ScaleQuantizer.h): transposición en semitonos y, con Mode Round,
    // la nota fuera de la escala va a la más cercana de la escala (Rounding: Nearest con empate hacia abajo, Down o Up); con
    // Filter, no suena. Escalas como grados en semitonos sobre Root (ScaleQuantizer::masks). Antes del secuenciador y el arpegiador
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "keyTranspose", 1 }, "Key Transpose", -24, 24, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scaleMode", 1 },  "Scale Mode", StringArray { "Off", "Round", "Filter" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scaleRoot", 1 },  "Scale Root",
        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scaleType", 1 },  "Scale Type",
        StringArray { "Major", "Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
                      "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone", "Phrygian Dominant" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scaleRound", 1 }, "Scale Rounding", StringArray { "Nearest", "Down", "Up" }, 0));

    // Arpegiador (antes del motor). Rate en fracciones de negra; Arp BPM solo sin tempo del host
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "arpOn", 1 },      "Arp On", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpMode", 1 },    "Arp Mode", StringArray { "Up", "Down", "Up/Down", "Random", "As Played" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpRate", 1 },    "Arp Rate", StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "arpOctaves", 1 }, "Arp Octaves", 1, 4, 1));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "arpGate", 1 },    "Arp Gate", NormalisableRange<float> (0.05f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "arpLatch", 1 },   "Arp Latch", false));
    // One Shot: el patrón suena una vez por pulsación (efectos de juego: moneda, power-up) en lugar de repetirse
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "arpOneShot", 1 }, "Arp One Shot", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "arpBpm", 1 },     "Arp BPM", NormalisableRange<float> (40.0f, 240.0f, 1.0f), 120.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " BPM"; }))));

    // Voz: modo (Poly / Mono con prioridad a la última nota / Legato sin reinicio de envolventes), glide desde la última
    // nota (constante de tiempo exponencial en el dominio de semitonos) y envolvente de tono (baterías, zaps)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voiceMode", 1 },   "Voice Mode", StringArray { "Poly", "Mono", "Legato" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "glide", 1 },       "Glide", skewed (0.0f, 2000.0f, 200.0f), 0.0f, millis));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pitchEnvAmount", 1 }, "Pitch Env Amount", NormalisableRange<float> (-48.0f, 48.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; }))));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pitchEnvDecay", 1 }, "Pitch Env Decay", skewed (1.0f, 2000.0f, 100.0f), 50.0f, millis));

    // Secuenciador por pasos (antes del motor; si está activo sustituye al arpegiador). Transpone el patrón con la tecla pulsada:
    // el paso con tono 0 suena en la nota tocada. Tie liga el paso con el siguiente (con Voice Mode Legato: slide tipo TB-303)
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "seqOn", 1 },    "Seq On", false));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "seqSteps", 1 }, "Seq Steps", 1, 16, 16));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "seqRate", 1 },  "Seq Rate", StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "seqGate", 1 },  "Seq Gate", NormalisableRange<float> (0.05f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "seqLatch", 1 }, "Seq Latch", false));
    // Chip: APU del NES / Famicom y de la Game Boy. Rangos de los registros del hardware (volumen y envolvente de 4 bits...)
    // Paneo NR51 de la Game Boy por canal (Pulse 1, Pulse 2, Wave, Noise); el NES es mono
    {
        const char* names[] = { "Pulse 1", "Pulse 2", "Wave", "Noise" };
        for (int k = 0; k < 4; ++k)
            l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipPan" + String (k + 1), 1 }, String ("Chip Pan ") + names[k],
                                                           StringArray { "Center", "Left", "Right", "Off" }, 0));
    }
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipModel", 1 },  "Chip Model",
        StringArray { "NES NTSC (2A03)", "NES PAL (2A07)", "Famicom (2A03)", "Game Boy (DMG)", "GB Color (CGB)", "PAL Unadapted" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipVoice", 1 },  "Chip Voice",
        StringArray { "Pulse", "Triangle / Wave", "Noise", "Poly", "MIDI Channels", "DPCM (NES)", "VRC6 Pulse", "VRC6 Saw", "Poly + VRC6" }, 0));
    // Expansión VRC6 (solo Famicom): duty de sus pulsos (1/16 a 8/16) y sierra con desbordamiento del acumulador
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipExpansion", 1 }, "Chip Expansion", StringArray { "None", "VRC6 (Famicom)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipVrc6Duty", 1 }, "VRC6 Pulse Duty",
        StringArray { "1/16", "2/16", "3/16", "4/16", "5/16", "6/16", "7/16", "8/16" }, 7));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "chipVrc6Overdrive", 1 }, "VRC6 Saw Overdrive", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipDuty", 1 },   "Chip Duty", StringArray { "12.5 %", "25 %", "50 %", "75 %" }, 2));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "chipVolume", 1 }, "Chip Volume", 0, 15, 12));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipEnvelope", 1 }, "Chip Envelope", StringArray { "Constant", "Decay", "Loop / Rise" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "chipEnvRate", 1 }, "Chip Envelope Rate", 0, 15, 4));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "chipRelease", 1 }, "Chip Release", 0, 60, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "chipSweep", 1 },   "Chip Sweep", -7, 7, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "chipSweepRate", 1 }, "Chip Sweep Rate", 0, 7, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipArp", 1 },     "Chip Arpeggio", StringArray { "Off", "Up", "Down" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "chipArpSpeed", 1 }, "Chip Arpeggio Speed", 1, 8, 1));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "chipVibDepth", 1 }, "Chip Vibrato Depth", NormalisableRange<float> (0.0f, 100.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ct"; }))));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "chipVibRate", 1 }, "Chip Vibrato Rate", NormalisableRange<float> (0.5f, 15.0f), 6.0f, hertz));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "chipVibDelay", 1 }, "Chip Vibrato Delay", 0, 60, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipNoiseMode", 1 }, "Chip Noise Mode", StringArray { "Long", "Short" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipWave", 1 },    "Chip Wave",
        StringArray { "Triangle", "Saw", "Sine", "Square", "Pulse 25 %", "Organ", "Boot RAM" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipWaveLevel", 1 }, "Chip Wave Level", StringArray { "100 %", "50 %", "25 %" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chipDpcm", 1 },    "Chip DPCM Sample",
        StringArray { "Kit (keys)", "Kick", "Snare", "Tom", "Timpani", "Slap Bass", "User Sample" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "chipDpcmLoop", 1 }, "Chip DPCM Loop", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "chipVelocity", 1 }, "Chip Velocity", true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "chipFilter", 1 },  "Chip Console Filter", true));

    // Polivoks: rangos del original donde se conocen (32'..2', corte 100 Hz - 10 kHz); tiempos y LFO estimados (ver el motor)
    {
        const StringArray waves { "Triangle", "Saw", "Square", "Pulse 1", "Pulse 2" }, ranges { "32'", "16'", "8'", "4'", "2'" };
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pvVoice", 1 },  "Polivoks Voice", StringArray { "Mono", "Duo" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pvWave1", 1 },  "Polivoks VCO1 Wave", waves, 1));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pvRange1", 1 }, "Polivoks VCO1 Range", ranges, 2));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pvWave2", 1 },  "Polivoks VCO2 Wave", waves, 1));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pvRange2", 1 }, "Polivoks VCO2 Range", ranges, 2));
    }
    auto semis = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " st"; }));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvDetune", 1 }, "Polivoks VCO2 Detune", NormalisableRange<float> (-12.0f, 12.0f), 0.0f, semis));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvFm", 1 },     "Polivoks FM", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvGlide", 1 },  "Polivoks Glide", skewed (0.0f, 2.0f, 0.2f), 0.0f, seconds));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvDrift", 1 },  "Polivoks Drift", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvMix1", 1 },   "Polivoks Mix VCO1", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvMix2", 1 },   "Polivoks Mix VCO2", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvNoise", 1 },  "Polivoks Noise", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pvFilterMode", 1 }, "Polivoks Filter Mode", StringArray { "LP", "BP" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvCutoff", 1 }, "Polivoks Cutoff", skewed (100.0f, 10000.0f, 1000.0f), 1500.0f, hertz));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvReso", 1 },   "Polivoks Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.2f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvEnvAmount", 1 }, "Polivoks Env Amount", NormalisableRange<float> (-10000.0f, 10000.0f), 3000.0f, hertz));
    for (const auto* pre : { "pvF", "pvA" })
    {
        const String id (pre), nm (id == "pvF" ? "Polivoks Filter " : "Polivoks Amp ");
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "Attack", 1 },  nm + "Attack",  skewed (0.0005f, 3.0f, 0.2f), 0.005f, seconds));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "Decay", 1 },   nm + "Decay",   skewed (0.0005f, 12.0f, 0.5f), 0.4f, seconds));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "Sustain", 1 }, nm + "Sustain", NormalisableRange<float> (0.0f, 1.0f), id == "pvF" ? 0.2f : 0.8f, percent));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "Release", 1 }, nm + "Release", skewed (0.0005f, 12.0f, 0.5f), 0.2f, seconds));
        l.add (std::make_unique<AudioParameterBool>  (ParameterID { id + "Loop", 1 },    nm + "Repeat", false));
    }
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "pvLfoWave", 1 }, "Polivoks LFO Wave", StringArray { "Triangle", "Square", "Noise", "S&H" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pvLfoRate", 1 }, "Polivoks LFO Rate", skewed (0.1f, 30.0f, 4.0f), 5.0f, hertz));
    for (const auto* t : { "Vco1", "Vco2", "Vcf", "Vca" })
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { String ("pvLfo") + t, 1 }, String ("Polivoks LFO to ") + String (t).toUpperCase(),
                                                      NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));

    // ANS: partitura (de fábrica o pintada), velocidad de la placa 0-24 mm/s, 20 registros de media octava
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "ansScore", 1 }, "ANS Score",
        StringArray { "Cluster", "Glissandi", "Drone", "Bells", "Noise Bands", "72-Step Scale", "User" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ansSpeed", 1 }, "ANS Speed", NormalisableRange<float> (0.0f, 24.0f), 12.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " mm/s"; }))));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "ansHold", 1 },  "ANS Hold", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ansEmulsion", 1 }, "ANS Emulsion", NormalisableRange<float> (0.0f, 1.0f), 0.2f, percent));
    for (int b = 1; b <= 20; ++b)
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ansReg" + String (b), 1 }, "ANS Register " + String (b),
                                                      NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));

    // FM 6-Op (tipo DX7): rangos del DX7 (0..99, detune -7..7, curvas de level scaling). Por defecto, INIT VOICE: algoritmo 1,
    // OP1 a nivel 99 y el resto a 0, EG 99/99/99/99 - 99/99/99/0, ratio 1, break point C3 (Yamaha) = MIDI 60
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "fmMode", 1 },      "FM Mode", StringArray { "DX7", "Clean" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "fmAlgorithm", 1 }, "FM Algorithm", 1, 32, 1));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "fmFeedback", 1 },  "FM Feedback", 0, 7, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "fmLfoWave", 1 },  "FM LFO Wave", StringArray { "Triangle", "Saw Down", "Saw Up", "Square", "Sine", "S&H" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "fmLfoSpeed", 1 }, "FM LFO Speed", 0, 99, 35));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "fmLfoDelay", 1 }, "FM LFO Delay", 0, 99, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "fmLfoPmd", 1 },   "FM LFO Pitch Depth", 0, 99, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "fmLfoAmd", 1 },   "FM LFO Amp Depth", 0, 99, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "fmLfoPms", 1 },   "FM Pitch Mod Sens", 0, 7, 3));
    // Resto de la voz del DX7 (VCED 126-133 pitch EG, 136 osc key sync, 141 LFO key sync, 144 transpose). Por defecto, el
    // INIT VOICE del DX7: pitch EG plano (L = 50, sin efecto), osc sync on, LFO sync on (manual p. 43) y transpose 0
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "fmTranspose", 1 }, "FM Transpose", -24, 24, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "fmOscSync", 1 },   "FM Osc Key Sync", true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "fmLfoSync", 1 },   "FM LFO Key Sync", true));
    for (int k = 1; k <= 4; ++k)
    {
        l.add (std::make_unique<AudioParameterInt> (ParameterID { "fmPR" + String (k), 1 }, "FM Pitch EG Rate " + String (k), 0, 99, 99));
        l.add (std::make_unique<AudioParameterInt> (ParameterID { "fmPL" + String (k), 1 }, "FM Pitch EG Level " + String (k), 0, 99, 50));
    }
    for (int op = 1; op <= 6; ++op)
    {
        const auto id = [op] (const char* name) { return ParameterID { "fm" + String (op) + name, 1 }; };
        const auto nm = [op] (const char* name) { return "FM OP" + String (op) + " " + name; };
        l.add (std::make_unique<AudioParameterInt> (id ("Coarse"), nm ("Coarse"), 0, 31, 1));
        l.add (std::make_unique<AudioParameterInt> (id ("Fine"),   nm ("Fine"), 0, 99, 0));
        l.add (std::make_unique<AudioParameterInt> (id ("Detune"), nm ("Detune"), -7, 7, 0));
        l.add (std::make_unique<AudioParameterBool> (id ("Fixed"), nm ("Fixed"), false));
        l.add (std::make_unique<AudioParameterInt> (id ("Level"),  nm ("Level"), 0, 99, op == 1 ? 99 : 0));
        for (int k = 1; k <= 4; ++k)
        {
            l.add (std::make_unique<AudioParameterInt> (ParameterID { "fm" + String (op) + "R" + String (k), 1 }, nm ("Rate ") + String (k), 0, 99, 99));
            l.add (std::make_unique<AudioParameterInt> (ParameterID { "fm" + String (op) + "L" + String (k), 1 }, nm ("Level ") + String (k), 0, 99, k == 4 ? 0 : 99));
        }
        l.add (std::make_unique<AudioParameterInt> (id ("Vel"),       nm ("Velocity Sens"), 0, 7, 0));
        l.add (std::make_unique<AudioParameterInt> (id ("RateScale"), nm ("Rate Scaling"), 0, 7, 0));
        l.add (std::make_unique<AudioParameterInt> (id ("Ams"),       nm ("Amp Mod Sens"), 0, 3, 0));
        l.add (std::make_unique<AudioParameterInt> (id ("Break"),     nm ("Break Point"), 21, 120, 60, noteName));
        l.add (std::make_unique<AudioParameterInt> (id ("LDepth"),    nm ("Left Depth"), 0, 99, 0));
        l.add (std::make_unique<AudioParameterInt> (id ("RDepth"),    nm ("Right Depth"), 0, 99, 0));
        l.add (std::make_unique<AudioParameterChoice> (id ("LCurve"), nm ("Left Curve"), StringArray { "-LIN", "-EXP", "+EXP", "+LIN" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (id ("RCurve"), nm ("Right Curve"), StringArray { "-LIN", "-EXP", "+EXP", "+LIN" }, 0));
    }

    // Mega Drive (YM2612 / YM3438): rangos nativos de los registros (plutiedev.com "YM2612 registers"). Por defecto, el
    // equivalente al INIT VOICE: algoritmo 0 con OP1-OP3 en silencio (TL 127) y OP4 = seno, AR 31, RR 15
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mdModel", 1 },     "MD Model", StringArray { "Model 1 (YM2612)", "Model 2 (YM3438)", "Clean (YM3438)" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mdAlgorithm", 1 }, "MD Algorithm", 0, 7, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mdFeedback", 1 },  "MD Feedback", 0, 7, 0));
    // LFO: 53 267 Hz / (128 pasos · 108, 77, 71, 67, 62, 44, 8, 5 muestras)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mdLfo", 1 },       "MD LFO",
        StringArray { "Off", "3.85 Hz", "5.40 Hz", "5.86 Hz", "6.21 Hz", "6.71 Hz", "9.46 Hz", "52.0 Hz", "83.2 Hz" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mdPms", 1 },       "MD LFO Pitch Sens", 0, 7, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mdAms", 1 },       "MD LFO Amp Sens", 0, 3, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mdVelocity", 1 },  "MD Velocity", true));
    // Paneo del YM2612: registro B4h (L/R por canal, solo duro). El driver de KoniSynth lo reparte entre las 6 voces
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mdPan", 1 },       "MD Pan", StringArray { "Center", "Left", "Right", "Alternate L/R", "Spread L/C/R" }, 0));
    // Voces: FM, PSG SN76489 (tono, ruido), DAC del canal 6 y canal 3 especial (Engines/MegaDrive.h)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mdVoice", 1 },     "MD Voice",
        StringArray { "FM (6 ch)", "FM + PSG", "PSG Tone (3 ch)", "PSG Noise", "DAC Sample (ch 6)", "FM Ch3 Special" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mdPsgNoise", 1 },  "MD PSG Noise", StringArray { "White", "Periodic" }, 0));
    // Ruido: desplazamientos del LFSR a 3 579 545 Hz / 512, 1024, 2048, o al ritmo del tono 3 (sigue la tecla)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mdPsgNoiseRate", 1 }, "MD PSG Noise Rate", StringArray { "Key (tone 3)", "6991 Hz", "3496 Hz", "1748 Hz" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mdPsgAttack", 1 }, "MD PSG Attack", 0, 15, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mdPsgDecay", 1 },  "MD PSG Decay", 0, 15, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mdPsgSustain", 1 }, "MD PSG Sustain", 0, 15, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mdPsgRelease", 1 }, "MD PSG Release", 0, 15, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mdDacRate", 1 },   "MD DAC Rate", StringArray { "6.5 kHz", "8 kHz", "11 kHz", "16 kHz", "22 kHz" }, 1));
    for (int op = 1; op <= 3; ++op)
        l.add (std::make_unique<AudioParameterInt> (ParameterID { "mdCh3Op" + String (op), 1 }, "MD Ch3 OP" + String (op) + " Semitones", -24, 24, 0));
    for (int op = 1; op <= 4; ++op)
    {
        const auto id = [op] (const char* name) { return ParameterID { "md" + String (op) + name, 1 }; };
        const auto nm = [op] (const char* name) { return "MD OP" + String (op) + " " + name; };
        l.add (std::make_unique<AudioParameterInt>  (id ("Tl"),  nm ("Total Level"), 0, 127, op == 4 ? 0 : 127));
        l.add (std::make_unique<AudioParameterInt>  (id ("Ar"),  nm ("Attack Rate"), 0, 31, 31));
        l.add (std::make_unique<AudioParameterInt>  (id ("Dr"),  nm ("Decay Rate"), 0, 31, 0));
        l.add (std::make_unique<AudioParameterInt>  (id ("Sr"),  nm ("Sustain Rate"), 0, 31, 0));
        l.add (std::make_unique<AudioParameterInt>  (id ("Sl"),  nm ("Sustain Level"), 0, 15, 0));
        l.add (std::make_unique<AudioParameterInt>  (id ("Rr"),  nm ("Release Rate"), 0, 15, 15));
        l.add (std::make_unique<AudioParameterInt>  (id ("Mul"), nm ("Multiple"), 0, 15, 1));
        l.add (std::make_unique<AudioParameterInt>  (id ("Dt"),  nm ("Detune"), -3, 3, 0));
        l.add (std::make_unique<AudioParameterInt>  (id ("Ks"),  nm ("Key Scale"), 0, 3, 0));
        l.add (std::make_unique<AudioParameterBool> (id ("Am"),  nm ("AM"), false));
        l.add (std::make_unique<AudioParameterChoice> (id ("Ssg"), nm ("SSG-EG"),
            StringArray { "Off", "0 Repeat", "1 Once", "2 Alternate", "3 Once, High", "4 Repeat Inv", "5 Once Inv, High", "6 Alternate Inv", "7 Once Inv" }, 0));
    }

    // OPL (YM3812 / YMF262): registros nativos (Nuked-OPL3, datasheets). Por defecto: 2 operadores en FM con el modulador en
    // silencio (TL 63) y la portadora en seno, AR 15, RR 7, EGT (sostiene)
    // Canal MIDI de entrada: Omni o 1-16 (los teclados con acompañamiento lo mandan por otros canales: Casio CT-S400)
    {
        StringArray channels { "Omni" };
        for (int c = 1; c <= 16; ++c) channels.add (String (c));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "midiInChannel", 1 }, "MIDI Input Channel", channels, 0));
    }
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "oplModel", 1 },     "OPL Model", StringArray { "OPL2 (YM3812)", "OPL3 (YMF262)", "OPLL (YM2413)", "VRC7 (Konami)" }, 0));
    // OPLL / VRC7: instrumento de la ROM (0 = el de usuario, con los mandos de OP1/OP2), volumen del canal (pasos de 3 dB),
    // SUS (release lento al soltar) y percusión del YM2413 (canal MIDI 10)
    {
        StringArray inst { "0 User" };
        static const char* names[15] = { "Violin", "Guitar", "Piano", "Flute", "Clarinet", "Oboe", "Trumpet", "Organ", "Horn",
                                         "Synthesizer", "Harpsichord", "Vibraphone", "Synth Bass", "Acoustic Bass", "Electric Guitar" };
        for (int i = 1; i <= 15; ++i) inst.add (String (i) + " " + names[i - 1]);
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "opllInstrument", 1 }, "OPLL Instrument", inst, 3));
    }
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "opllVolume", 1 },  "OPLL Volume Attenuation", 0, 15, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "opllSustain", 1 }, "OPLL Sustain (SUS)", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "opllRhythm", 1 },  "OPLL Rhythm (YM2413)", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "oplFourOp", 1 },    "OPL 4-Op", false));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "oplAlgorithm", 1 }, "OPL Algorithm", 0, 3, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "oplFeedback", 1 },  "OPL Feedback", 0, 7, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "oplDeepTremolo", 1 }, "OPL Deep Tremolo", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "oplDeepVibrato", 1 }, "OPL Deep Vibrato", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "oplVelocity", 1 },  "OPL Velocity", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "oplPan", 1 },       "OPL Pan", StringArray { "Center", "Left", "Right", "Alternate L/R" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "oplOutput", 1 },    "OPL Output", StringArray { "Stereo (OPL3)", "Mono" }, 0));

    // Roland MT-32 / CM-32L (Engines/Mt32.h, Munt): modelo de ROM, destino del teclado, programa, reverb del aparato y DAC
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mt32Model", 1 },   "MT-32 Model", StringArray { "MT-32", "CM-32L" }, 0));
    // Teclado -> parte 1 o ritmo; Multi (enrutado aparte): canales MIDI tal cual, para ficheros .mid
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mt32Mode", 1 },    "MT-32 Keys", StringArray { "Part 1", "Rhythm" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mt32Multi", 1 },   "MT-32 Multi", false));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mt32Program", 1 }, "MT-32 Program", 1, 128, 1));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mt32Reverb", 1 },  "MT-32 Reverb", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mt32Dac", 1 },     "MT-32 DAC", StringArray { "Auto", "Nice", "Pure", "Gen 1", "Gen 2" }, 0));
    // Embellecimientos de Munt, apagados: por defecto, el aparato tal cual (regla de KoniSynth: fiel; quien quiera, configura)
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mt32NiceAmp", 1 }, "MT-32 Nice Amp Ramp", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mt32NicePan", 1 }, "MT-32 Nice Panning", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mt32NiceMix", 1 }, "MT-32 Nice Partial Mixing", false));
    for (int op = 1; op <= 4; ++op)
    {
        const auto id = [op] (const char* name) { return ParameterID { "opl" + String (op) + name, 1 }; };
        const auto nm = [op] (const char* name) { return "OPL OP" + String (op) + " " + name; };
        l.add (std::make_unique<AudioParameterBool> (id ("Am"),   nm ("Tremolo"), false));
        l.add (std::make_unique<AudioParameterBool> (id ("Vib"),  nm ("Vibrato"), false));
        l.add (std::make_unique<AudioParameterBool> (id ("Egt"),  nm ("Sustain (EGT)"), true));
        l.add (std::make_unique<AudioParameterBool> (id ("Ksr"),  nm ("Key Scale Rate"), false));
        l.add (std::make_unique<AudioParameterInt>  (id ("Mult"), nm ("Multiple"), 0, 15, 1));
        l.add (std::make_unique<AudioParameterChoice> (id ("Ksl"), nm ("Key Scale Level"), StringArray { "0", "1.5 dB/oct", "3 dB/oct", "6 dB/oct" }, 0));
        l.add (std::make_unique<AudioParameterInt>  (id ("Tl"),   nm ("Total Level"), 0, 63, (op % 2 == 1) ? 63 : 0));
        l.add (std::make_unique<AudioParameterInt>  (id ("Ar"),   nm ("Attack Rate"), 0, 15, 15));
        l.add (std::make_unique<AudioParameterInt>  (id ("Dr"),   nm ("Decay Rate"), 0, 15, 0));
        l.add (std::make_unique<AudioParameterInt>  (id ("Sl"),   nm ("Sustain Level"), 0, 15, 0));
        l.add (std::make_unique<AudioParameterInt>  (id ("Rr"),   nm ("Release Rate"), 0, 15, 7));
        l.add (std::make_unique<AudioParameterChoice> (id ("Wave"), nm ("Waveform"),
            StringArray { "0 Sine", "1 Half sine", "2 Abs sine", "3 Quarter sine", "4 Alternating (OPL3)", "5 Camel (OPL3)", "6 Square (OPL3)", "7 Log saw (OPL3)" }, 0));
    }

    // SNES (S-DSP): registros nativos (fullsnes). Por defecto: cuadrada, gaussiana, ADSR A15 D7 SL7 SR0 (órgano), sin eco
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "snesSource", 1 },  "SNES Source", StringArray { "Square", "Saw", "Sine", "Strings", "Choir", "Bass", "User Sample" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "snesUserRate", 1 }, "SNES User Rate", StringArray { "32 kHz", "16 kHz", "8 kHz" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "snesInterp", 1 },  "SNES Interpolation", StringArray { "Gaussian", "Linear", "None" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "snesEnvMode", 1 }, "SNES Envelope Mode", StringArray { "ADSR", "Direct Gain", "Linear Down", "Exp Down", "Linear Up", "Bent Up" }, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesAttack", 1 },       "SNES Attack", 0, 15, 15));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesDecay", 1 },        "SNES Decay", 0, 7, 7));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesSustainLevel", 1 }, "SNES Sustain Level", 0, 7, 7));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesSustainRate", 1 },  "SNES Sustain Rate", 0, 31, 0));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesGainRate", 1 },     "SNES Gain Rate", 0, 31, 16));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesDirectGain", 1 },   "SNES Direct Gain", 0, 127, 127));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "snesNoise", 1 },        "SNES Noise", false));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesNoiseRate", 1 },    "SNES Noise Rate", 0, 31, 28));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesVolume", 1 },       "SNES Voice Volume", 0, 127, 80));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesMainVolume", 1 },   "SNES Main Volume", 0, 127, 127));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "snesVelocity", 1 },     "SNES Velocity", true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "snesEcho", 1 },         "SNES Echo", false));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesEdl", 1 },          "SNES Echo Delay", 0, 15, 5));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesEfb", 1 },          "SNES Echo Feedback", -128, 127, 64));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "snesEvol", 1 },         "SNES Echo Volume", -128, 127, 48));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "snesFir", 1 },          "SNES Echo FIR", StringArray { "Flat", "Low-pass" }, 1));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "snesFilter", 1 },       "SNES Output Filter", true));
    // DAC: Hold = retención como un DAC sin sobremuestreo (por defecto, fiel); Smooth = sinc de bajada (quita las imágenes y
    // recorta desde ~12 kHz): embellecimiento opcional
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "snesDac", 1 },          "SNES DAC", StringArray { "Hold", "Smooth" }, 0));
    // Volumen L/R por voz (VxVOLL/VxVOLR): balance y separación entre voces sucesivas (driver de KoniSynth)
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "snesPan", 1 },    "SNES Pan", NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "snesSpread", 1 }, "SNES Spread", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));

    for (int i = 1; i <= 16; ++i)
    {
        l.add (std::make_unique<AudioParameterInt>  (ParameterID { "stepNote" + String (i), 1 }, "Step " + String (i) + " Note", -24, 24, 0));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "stepOn" + String (i), 1 },   "Step " + String (i) + " On", true));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "stepTie" + String (i), 1 },  "Step " + String (i) + " Tie", false));
    }

    // Controlador MIDI: rango del pitch bend (semitonos) y profundidad de la rueda de modulación (CC 1 -> vibrato a la
    // velocidad del LFO, ±1 semitono con rueda y Mod Wheel al 100 %; funciona aunque el LFO esté apagado)
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "bendRange", 1 },     "Bend Range", 0, 24, 2));
    // MPE (zona inferior: canal 1 maestro, 2-16 miembros): bend, presión y CC74 por nota en el motor Synth
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mpeOn", 1 },         "MPE", false));
    // Voces del motor Synth: asignación, fase de los osciladores al pulsar y tolerancias (por nota o por tarjeta de voz)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voiceAssign", 1 },   "Voice Assign", StringArray { "Lowest Free", "Reuse Same Note", "Round Robin" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "oscPhase", 1 },      "Osc Phase", StringArray { "Auto", "Reset", "Free Running", "Random" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voiceTolerance", 1 }, "Tolerances", StringArray { "Per Note", "Per Voice Card" }, 0));
    // Entrada del filtro limpia: con Drive 0 % la etapa tanh de entrada desaparece (por defecto apagado: carácter de siempre)
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "filterCleanDrive", 1 }, "Filter Clean Drive", false));
    // Saturación por voz (motor Synth, entre filtro y VCA): curva y drive (0 dB = apagada)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voiceSatMode", 1 },  "Voice Sat Mode", StringArray { "Soft", "Diode", "Hard", "Fold" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "voiceSatDrive", 1 }, "Voice Sat Drive", NormalisableRange<float> (0.0f, 36.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v < 0.05f ? String ("Off") : String (v, 1) + " dB"; }))));
    // Posición de la saturación por voz: tras el filtro (la de siempre) o antes (el filtro suaviza los armónicos que añade)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voiceSatPos", 1 },   "Voice Sat Position", StringArray { "Post-filter", "Pre-filter" }, 0));
    // VCA analógico (motor Synth): feedthrough del control ("click"/thump) y fuga con el VCA cerrado (bleed). Off por defecto
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "vcaClick", 1 },      "VCA Click", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "vcaLeak", 1 },       "VCA Leak", NormalisableRange<float> (-100.0f, -30.0f), -100.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v <= -99.5f ? String ("Off") : String (v, 1) + " dB"; }))));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "mpeBendRange", 1 },  "MPE Bend Range", 0, 96, 48));
    // Aftertouch (presión de canal o polifónica, la mayor de las dos): filtro +3 oct, vibrato ±1 st (a la velocidad del LFO 1)
    // y volumen hasta +6 dB, como los faders "After" del Yamaha CS-80
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "atFilter", 1 },  "Aftertouch Filter", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "atVibrato", 1 }, "Aftertouch Vibrato", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "atAmp", 1 },     "Aftertouch Volume", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    // Matriz de modulación del motor Synth (Dsp/ModMatrix.h): 8 ranuras fuente -> destino × cantidad, por voz
    for (int m = 1; m <= ModMatrix::numSlots; ++m)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mtxSrc" + String (m), 1 }, "Matrix " + String (m) + " Source", ModMatrix::sourceNames(), 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mtxDst" + String (m), 1 }, "Matrix " + String (m) + " Dest", ModMatrix::destinationNames(), 0));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "mtxAmt" + String (m), 1 }, "Matrix " + String (m) + " Amount", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, percent));
    }
    // Macros de interpretación (Params/PerfMacros.h): 8 mandos que mueven a la vez los parámetros asignados
    for (int m = 1; m <= 8; ++m)
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "macro" + String (m), 1 }, "Macro " + String (m),
                                                      NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "modWheelDepth", 1 }, "Mod Wheel Depth", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));

    // Echo de cinta. Feedback > 100 % = autooscilación acotada por la saturación del lazo.
    // Tone: pasa-bajos en el lazo; Wow: modulación del tiempo (wow 0,5 Hz ±2 ms + flutter 6,3 Hz ±0,15 ms a 100 %)
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "echoOn", 1 },       "Echo On", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "echoTime", 1 },     "Echo Time",     skewed (10.0f, 1000.0f, 250.0f), 350.0f, millis));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "echoFeedback", 1 }, "Echo Feedback", NormalisableRange<float> (0.0f, 1.1f), 0.4f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "echoMix", 1 },      "Echo Mix",      NormalisableRange<float> (0.0f, 1.0f), 0.35f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "echoTone", 1 },     "Echo Tone",     skewed (800.0f, 12000.0f, 3000.0f), 4500.0f, freq));
    // Sync del eco a figuras musicales (D = con puntillo, T = tresillo); Free = Echo Time en ms. Máximo 1000 ms
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "echoSync", 1 },     "Echo Sync",
        StringArray { "Free", "1/4", "1/8D", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "echoWow", 1 },      "Echo Wow",      NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    // Ping-Pong: las repeticiones alternan entre izquierda y derecha (entrada sumada a mono a la línea izquierda; cada línea
    // realimenta a la otra). Apagado por defecto: los presets anteriores suenan igual
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "echoPingPong", 1 }, "Echo Ping-Pong", false));

    // Reverb de placa (Dattorro 1997, ver struct PlateReverb). Se conservan los ID ambient* por compatibilidad de presets.
    auto rt60 = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (v, v < 10.0f ? 2 : 1) + " s"; }));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "ambientOn", 1 },       "Reverb On", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ambientSize", 1 },     "Reverb Size", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ambientMix", 1 },      "Reverb Mix",  NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbDecay", 1 },     "Reverb Decay", skewed (0.2f, 30.0f, 3.0f), 2.5f, rt60));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbPredelay", 1 },  "Reverb Predelay", skewed (0.0f, 250.0f, 40.0f), 10.0f, millis));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbDiffusion", 1 }, "Reverb Diffusion", NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbDamp", 1 },      "Reverb Damp", skewed (500.0f, 20000.0f, 4000.0f), 9000.0f, freq));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbTone", 1 },      "Reverb Tone", skewed (500.0f, 20000.0f, 4000.0f), 16000.0f, freq));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbMod", 1 },       "Reverb Mod",  NormalisableRange<float> (0.0f, 2.0f), 1.0f, percent));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbWidth", 1 },     "Reverb Width", NormalisableRange<float> (0.0f, 1.5f), 1.0f, percent));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { "reverbFreeze", 1 },    "Reverb Freeze", false));

    // Oscilador complejo (estilo Buchla 259): oscilador modulador -> FM lineal (atraviesa cero), AM/ring y Timbre
    // del principal; wavefolder (Timbre) con Symmetry; sync duro del principal al modulador.
    auto ratio = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (v, 3) + "x"; }));
    auto index = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return "I " + String (v, 2); }));
    auto bipolar = parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (roundToInt (v * 100.0f)) + " %"; }));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "complexOn", 1 },  "Complex On", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "modWave", 1 },    "Mod Wave", StringArray { "Sine", "Triangle", "Saw", "Square", "Tri-Saw", "Saw+Pulse", "Saw+Tri", "Tri+Pulse", "Saw+Tri+Pulse", "Saw+Pulse (Juno)" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "modRatio", 1 },   "Mod Ratio", skewed (0.125f, 16.0f, 1.0f), 1.0f, ratio));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "fmIndex", 1 },    "FM Index",  skewed (0.0f, 10.0f, 2.0f), 0.0f, index));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "amDepth", 1 },    "AM Depth",  NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "timbre", 1 },     "Timbre",    NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "symmetry", 1 },   "Symmetry",  NormalisableRange<float> (-1.0f, 1.0f), 0.0f, bipolar));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "timbreMod", 1 },  "Timbre Mod", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "modSync", 1 },    "Mod Sync", false));
    // Pasa-bajos de un polo sobre el modulador del Complex (interno, Osc 2 u Osc 3) antes de la FM y la AM: arriba del todo,
    // apagado (sin efecto, bit a bit)
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "modLp", 1 },      "Mod LP", skewed (20.0f, 20000.0f, 1000.0f), 20000.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v >= 19999.0f ? String ("Off") : v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; }))));

    // Osc 2 en modo Wavetable (osc2Wave = Wavetable): su propia tabla (las incluidas o un fichero, ruta osc2WtPath en el
    // estado), independiente de la de la fuente Wavetable, como su sample, y su posición en ella
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "osc2WtPosition", 1 }, "Osc2 WT Position", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "osc2WtTable", 1 },    "Osc2 Wavetable",
        StringArray { "Basic Shapes", "PWM Sweep", "Harmonic Sweep", "Sync Sweep", "Formant Sweep", "User File" }, 0));

    // Osc 3: el Sub ampliado (subLevel y subOctave siguen siendo su nivel y su octava base). Por defecto, la cuadrada al 50 % de
    // siempre; f3 = f1 · 2^(octava + (semi + fine/100)/12): con -1 oct, Semi -24..+24 cubre de -3 a +1 octavas
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "osc3Wave", 1 }, "Osc3 Wave", StringArray { "Sine", "Triangle", "Saw", "Square", "Tri-Saw", "Saw+Pulse", "Saw+Tri", "Tri+Pulse", "Saw+Tri+Pulse", "Saw+Pulse (Juno)" }, 3));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "osc3Semi", 1 }, "Osc3 Semi", -24, 24, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "osc3Fine", 1 }, "Osc3 Fine", NormalisableRange<float> (-50.0f, 50.0f), 0.0f,
                                                   parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ct"; }))));

    // Forma del triángulo por oscilador (Dsp/OscillatorCore.h, shapeTriangle): asimetría (armónicos pares) y redondeo de las
    // puntas (impares más bajos). 0 % = triángulo ideal de siempre; Sym 50-60 % y Round 0-20 % = el medido en dos Minimoog Model D a través del filtro
    // (su etapa tanh ya redondea las puntas; con el filtro apagado, Round 30-50 %)
    for (const auto* o : { "osc1", "osc2", "osc3" })
    {
        const auto n = String (o).replace ("osc", "Osc");
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { String (o) + "TriSym", 1 },   n + " Tri Symmetry", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { String (o) + "TriRound", 1 }, n + " Tri Round",    NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    }

    // Mute de las fuentes del segundo grupo de pestañas (Osc 2 | Osc 3 | Noise), como osc1Mute, wtMute, sampleMute y stringMute
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "osc2Mute", 1 },  "Osc2 Mute", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "osc3Mute", 1 },  "Osc3 Mute", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "noiseMute", 1 }, "Noise Mute", false));

    // Modulador del Complex Oscillator: el interno (Mod Wave, Ratio) o Osc 2 / Osc 3 tal como suenan (afinación, forma, tabla)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "complexModSource", 1 }, "Complex Mod Source", StringArray { "Internal", "Osc 2", "Osc 3" }, 0));

    // Osc 2 en modo Sample (osc2Wave = Sample): mismos controles que la fuente Sample, con su propio fichero (osc2SamplePath) y
    // la afinación, la deriva, el nivel y el Mute de Osc 2
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "osc2SampleRoot", 1 },  "Osc2 Sample Root", 0, 127, 60, noteName));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "osc2SampleStart", 1 }, "Osc2 Sample Start", NormalisableRange<float> (0.0f, 0.99f), 0.0f, percent));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "osc2SampleLoop", 1 },  "Osc2 Sample Loop", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "osc2SampleLength", 1 }, "Osc2 Sample Length", skewed (0.0f, 2000.0f, 150.0f), 0.0f,
        parsing (AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v < 0.5f ? String ("Full") : String (roundToInt (v)) + " ms"; }))));
    return l;
}
