// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Help text for every parameter (UI tooltip, in the app's language: English; the default value is appended).
static juce::String paramHelp (const juce::String& id)
{
    static const std::map<juce::String, const char*> help {
        { "gain", "Master output volume, at the very end of the chain." },
        { "n64Source", "Sound source: KoniSynth's own samples (Square, Saw, Sine: 32-sample loops; Strings, Choir: 3-voice loops; Bass) made at the chosen sample rate, or User Sample (its own file: Sample... button in this section)." },
        { "n64SourceRate", "Rate of the sample (8 to 32 kHz). N64 games stored most samples at low rates to save cartridge space: the dull, fuzzy N64 sound." },
        { "n64OutputRate", "Output rate of the console's audio interface (games used 22.05, 32 or 44.1 kHz). Notes can go at most one octave above the sample rate, as on the console (libultra MAX_RATIO 1.99996)." },
        { "n64Dac", "How the AI output samples become sound. Hold (default, as a non-oversampling DAC): each sample is held until "
                    "the next, images included. Smooth: a low-pass resampler removes them (cleaner than the console)." },
        { "n64Adpcm", "Store the samples as VADPCM (4 bits per sample, 16-sample frames, order-2 predictor), the N64 compression with its own grit. Off: raw 16-bit samples, for comparison." },
        { "n64Attack", "libultra envelope: time to reach Attack Volume (exponential ramp, as the console's mixer)." },
        { "n64Decay", "libultra envelope: time to go from Attack Volume to Decay Volume, which then holds while the key is down." },
        { "n64Release", "libultra envelope: time to fade out after the key is released." },
        { "n64AttackVolume", "libultra envelope: level reached at the end of the attack (0-127)." },
        { "n64DecayVolume", "libultra envelope: level held after the decay (0-127)." },
        { "n64Volume", "Channel volume (0-127); velocity scales it when Velocity is on." },
        { "n64Pan", "Channel pan (0 = left, 64 = center, 127 = right), with libultra's equal-power table." },
        { "n64Spread", "Spreads successive notes to opposite sides." },
        { "n64Fx", "libultra effect with Nintendo's own settings: Small Room, Big Room (the default of the music player), Echo (179 ms), Chorus or Flange. The effect returns in mono, as on the console." },
        { "n64FxMix", "How much of each voice goes to the effect (0-127, equal-power dry/wet as libultra's fxMix)." },
        { "n64Velocity", "Key velocity scales the voice volume (KoniSynth's driver)." },
        { "osc1Mute", "Silences Oscillator 1 without changing its level (tab right-click: Mute / Solo)." },
        { "wtMute", "Silences the wavetable without changing its level." },
        { "sampleMute", "Silences the sample without changing its level." },
        { "stringMute", "Silences the plucked string without changing its level." },
        { "osc2Mute", "Silences Oscillator 2 without changing its level (tab right-click: Mute / Solo)." },
        { "osc3Mute", "Silences Oscillator 3 without changing its level (tab right-click: Mute / Solo)." },
        { "noiseMute", "Silences the noise without changing its level (tab right-click: Mute / Solo)." },
        { "monoOut", "Sums left and right to mono at the very end, to check how the sound holds up in mono." },
        { "mdOutput", "Mega Drive output. Stereo: Model 1 headphone jack or Model 2 AV. Mono: Model 1 AV and RF outputs, as most people heard it on a TV." },
        { "snesOutput", "SNES output. Stereo: AV (multi-out). Mono: RF to the TV aerial input." },
        { "chipOutput", "Game Boy output. Headphones: stereo (NR51 panning). Speaker: the built-in mono speaker (left + right; its frequency response is not modelled). No effect on the NES, which is mono." },
        { "engine", "Sound engine: Synth (polyphonic), Theremin (XY pad), Casio VL-1 (monophonic + rhythm box), Stylophone (monophonic, 20 keys), Omnichord (chords + strum plate + rhythm), Drum Machine (TR-808 / CR-78 / Mini Pops kits), FM 6-Op (DX7-style FM), 8-Bit (NES / Famicom and Game Boy sound chips), Polivoks (Soviet duophonic synth), ANS (photoelectronic synthesizer: 720 sine tones drawn on a glass plate), Mega Drive (Sega YM2612 FM chip, 6 voices), SNES (Sony S-DSP: 8 BRR sample voices with Gaussian interpolation and FIR echo), N64 (libultra: VADPCM samples, RSP resampler, Nintendo's reverbs), OPL (AdLib / Sound Blaster: YM3812, YMF262, YM2413 and VRC7) or Roland MT-32 (LA synthesis via Munt, with your own ROMs). Choosing one from this menu loads its most representative preset." },
        { "pvVoice", "Polivoks voice mode: Mono, or Duo (lowest key to VCO2, highest to VCO1, as on the original)." },
        { "pvWave1", "Polivoks VCO1 waveform." },
        { "pvRange1", "VCO1 range, 32' (lowest) to 2', one octave per step. Glide acts only on VCO1, as on the original." },
        { "pvWave2", "Polivoks VCO2 waveform." },
        { "pvRange2", "VCO2 range, 32' to 2'." },
        { "pvDetune", "VCO2 detune in semitones. A little detune (0.1 st) gives the classic thick Polivoks bass." },
        { "pvFm", "VCO2 modulates the frequency of VCO1 (cross-modulation). Works best with VCO2 lower than VCO1." },
        { "pvGlide", "Portamento of VCO1 (exponential, like an RC circuit)." },
        { "pvDrift", "Oscillator drift, up to the factory spec of +/-0.5 % (about 9 cents)." },
        { "pvMix1", "VCO1 level. The mixer has no limiter: high levels overdrive the filter, part of the Polivoks sound." },
        { "pvMix2", "VCO2 level (also drives the filter)." },
        { "pvNoise", "White noise level." },
        { "pvFilterMode", "Filter output: low-pass (12 dB/oct) or band-pass." },
        { "pvCutoff", "Filter cutoff (no keyboard tracking, as on the original). The mod wheel works like the original cutoff pedal." },
        { "pvReso", "Filter resonance: rough and unstable, self-oscillates near the top." },
        { "pvEnvAmount", "How far the filter envelope moves the cutoff, in Hz (the original filter responds linearly in Hz)." },
        { "pvFAttack", "Filter envelope attack." }, { "pvFDecay", "Filter envelope decay." },
        { "pvFSustain", "Filter envelope sustain." }, { "pvFRelease", "Filter envelope release." },
        { "pvFLoop", "Repeat mode: the filter envelope repeats attack-decay while the key is held." },
        { "pvAAttack", "Amp envelope attack." }, { "pvADecay", "Amp envelope decay." },
        { "pvASustain", "Amp envelope sustain." }, { "pvARelease", "Amp envelope release." },
        { "pvALoop", "Repeat mode: the amp envelope repeats attack-decay while the key is held." },
        { "pvLfoWave", "Polivoks LFO waveform: triangle, square, noise (random pulses) or sample & hold." },
        { "pvLfoRate", "Polivoks LFO rate." },
        { "pvLfoVco1", "LFO to VCO1 pitch." }, { "pvLfoVco2", "LFO to VCO2 pitch." },
        { "pvLfoVcf", "LFO to filter cutoff." }, { "pvLfoVca", "LFO to volume (tremolo)." },
        { "ansScore", "ANS score (the glass plate): factory scores or User (draw your own with Plate...)." },
        { "ansSpeed", "Plate speed in mm per second (up to 24, as on the ANS). 1 mm is one column of the score." },
        { "ansHold", "Hold: the plate keeps playing without a key. Off: it plays while a key is held. In both modes the last key "
                    "pressed transposes the score." },
        { "ansEmulsion", "Slow irregularity of each optical track, which made the ANS sound alive (amount not documented: estimate)." },
        { "flangerOn", "Flanger in the style of the Electric Mistress (bucket-brigade delay, 0.8-6 ms)." },
        { "flangerRate", "Flanger sweep rate (0.07-10 Hz, triangular)." },
        { "flangerRange", "Sweep depth; in Filter Matrix mode it sets the fixed delay (1-10 ms)." },
        { "flangerColor", "Feedback: more metallic and resonant." },
        { "flangerMatrix", "Filter Matrix: stops the sweep and leaves a fixed comb filter, set by Range." },
        { "flangerMix", "Flanger amount." },
        { "fshiftOn", "Frequency shifter (Bode / Moog 1630): moves every frequency by the same amount, so harmonics become inharmonic." },
        { "fshiftHz", "Shift in Hz (-5000 to +5000). Small values give slow phasing and beating; large ones, metallic and bell-like tones." },
        { "fshiftBalance", "Upper output (+1, frequencies go up), lower output (-1, they go down), or a mix of both." },
        { "fshiftMix", "Frequency shifter amount." },
        { "leslieOn", "Rotating speaker (Leslie 122/147): horn and drum spinning, with Doppler and tremolo." },
        { "leslieSpeed", "Stop, Chorale (slow: horn 48 rpm) or Tremolo (fast: horn 400 rpm). Changing speed ramps up or down like the real motors." },
        { "leslieDrive", "Tube preamp drive of the Leslie amplifier." },
        { "leslieMix", "Leslie amount." },
        { "springOn", "Spring reverb (Fender-style 2-spring tank, 33 and 41 ms), with the typical dispersion chirps." },
        { "springDecay", "Spring decay time (1.2-4 s; the long-decay tank is 2.75-4 s)." },
        { "springMix", "Spring reverb amount." },
        { "chipModel", "Sound chip: NES NTSC (2A03), NES PAL (2A07), Famicom (2A03, different output filter), Game Boy (DMG), Game Boy Color (CGB, same sound channels) or PAL Unadapted (NTSC game on a PAL NES: ~1.3 semitones lower, slower driver). Choosing one loads its most representative preset." },
        { "chipVoice", "Which chip channel your keys play: one pulse, the triangle (NES) or wave channel (Game Boy), the noise, Poly (2 pulses + triangle/wave, 3 notes), MIDI Channels (1-4 = pulse 1, pulse 2, triangle/wave, noise; 5 = DPCM) or DPCM (NES sample channel). VRC6 Pulse / VRC6 Saw / Poly + VRC6: the VRC6 expansion channels (Famicom with Expansion = VRC6)." },
        { "chipDpcm", "NES DPCM sample: Kit plays kick, snare, toms, timpani and slap bass from the drum keys (C2 kick, D2 snare...); the others play one sample, with the key choosing one of the 16 hardware playback rates (C4 = fastest). User Sample converts its own file (Sample... button in this section) to 1-bit DPCM; a .dmc file plays as it is." },
        { "chipDpcmLoop", "Loop the DPCM sample while the key is held (hardware loop flag)." },
        { "chipDuty", "Pulse width of the pulse channels. 25 % and 75 % sound the same." },
        { "chipVolume", "Channel volume (0-15, 4 bits like the hardware)." },
        { "chipEnvelope", "Constant: fixed volume. Decay: the hardware envelope falls 15 to 0. Loop / Rise: NES repeats the decay; Game Boy rises instead." },
        { "chipEnvRate", "Hardware envelope period: NES 0-15 (about 240 Hz divided by rate+1 per step), Game Boy 1-7 (64 Hz divided by rate)." },
        { "chipRelease", "Release after you let go, in video frames (60 per second). 0 = cut at once, like most game music." },
        { "chipSweep", "Hardware pitch sweep: shift amount, negative = upwards. 0 = off." },
        { "chipSweepRate", "Hardware sweep period (1 fast ... 7 slow; 0 = off)." },
        { "chipArp", "Hold a chord on one channel and it is played as a fast arpeggio, the classic chiptune trick." },
        { "chipArpSpeed", "Arpeggio speed in video frames per note (1 = 60 notes per second)." },
        { "chipVibDepth", "Vibrato depth in cents, applied once per frame and rounded to the chip's period register (as game music drivers do)." },
        { "chipVibRate", "Vibrato speed." },
        { "chipVibDelay", "Frames before the vibrato starts." },
        { "chipNoiseMode", "Noise channel: Long (hiss) or Short (metallic, pitched). Keys choose the noise pitch." },
        { "chipWave", "Game Boy wave channel table (32 samples of 4 bits). Boot RAM is what the chip holds at power-on (Game Boy and Game Boy Color differ). On the Game Boy, retriggering the wave channel at the wrong instant corrupts the table, as on the real console." },
        { "chipWaveLevel", "Game Boy wave channel volume: 100, 50 or 25 %." },
        { "chipVelocity", "Velocity sets the 4-bit volume (as many drivers do). Off: every note at Volume." },
        { "chipFilter", "Console output filter: NES high-passes at 90 and 440 Hz and low-passes at 14 kHz; Famicom 37 Hz; Game Boy its output capacitor. Off: raw chip." },
        { "fmMode", "DX7: the original chip arithmetic (log-sine and exponential ROMs, 12-bit phase), its 49 kHz rate and its floating-point 12-bit DAC, with the grit of the real instrument. Clean: smooth floating-point FM." },
        { "fmAlgorithm", "FM algorithm (1-32, as on the DX7): how the 6 operators modulate each other. Carriers are heard; modulators shape their tone." },
        { "fmFeedback", "Feedback (0-7) of the operator marked fb in the algorithm: from sine towards saw and then noise." },
        { "fmLfoWave", "FM LFO waveform." },
        { "fmLfoSpeed", "FM LFO speed (0-99; about 0.06 to 49 Hz, from the DX7 table)." },
        { "fmLfoDelay", "Time before the LFO fades in after you play (0 = at once)." },
        { "fmLfoPmd", "LFO pitch modulation depth (vibrato), scaled by Pitch Mod Sens." },
        { "fmLfoAmd", "LFO amplitude modulation depth (tremolo / wah), for operators with Amp Mod Sens above 0." },
        { "fmLfoPms", "Pitch modulation sensitivity (0-7): with depth 99, 7 is about one octave." },
        { "fmTranspose", "Transposes the whole voice in semitones (-24 to +24), as the DX7 voice setting. Loaded .syx voices bring theirs." },
        { "fmOscSync", "Oscillator key sync: on, every note starts the operators from the same point of the wave; off, they keep "
                       "running freely, so each attack sounds slightly different (many DX7 pianos and guitars)." },
        { "fmLfoSync", "LFO key sync: restarts the LFO cycle when you play a note after all keys were released. The DX7 INIT VOICE has it on." },
        { "fmPR1", "Pitch envelope rate 1 (0-99): speed towards level 1 when you play." },
        { "fmPR2", "Pitch envelope rate 2 (0-99): speed from level 1 to level 2." },
        { "fmPR3", "Pitch envelope rate 3 (0-99): speed from level 2 to level 3, held while the key is down." },
        { "fmPR4", "Pitch envelope rate 4 (0-99): speed back to level 4 after you release the key." },
        { "fmPL1", "Pitch envelope level 1 (0-99; 50 = no change, 0 / 99 = about -4 / +4 octaves)." },
        { "fmPL2", "Pitch envelope level 2 (0-99; 50 = no change)." },
        { "fmPL3", "Pitch envelope level 3, the sustain (0-99; 50 = no change)." },
        { "fmPL4", "Pitch envelope level 4: where the note starts and returns after release (0-99; 50 = no change)." },
        { "omniSustain", "Omnichord strum plate sustain: how long each plucked string rings (about 0.4 to 3 s)." },
        { "omniHarpLevel", "Volume of the strum plate (Sonic Strings)." },
        { "omniChordLevel", "Volume of the held chord voice, heard while a chord key is down." },
        { "omniBassLevel", "Volume of the auto bass (root, fifth and third follow the rhythm)." },
        { "omniHarpOctave", "Octave of the strum plate: its 13 zones span 4 octaves from the chord." },
        { "omniSplit", "Keys below this note are chord buttons (one key = major; add the minor third for minor, the minor seventh for 7th, both for m7, the major seventh for maj7). From this note up: the 13 strum zones. The mod wheel also strums." },
        { "omniRhythm", "Omnichord rhythm with auto bass. The bass plays only while a chord key is held. Patterns are KoniSynth's own, in each style. "
                        "User 1-8: your own rhythms (Edit Rhythms...), one MIDI file each in the KoniSynth settings folder, rhythms/Omnichord/"
                        "User 1.mid ... User 8.mid: drums on channel 10 (General MIDI notes), bass on channel 2 written in C (C3 root, "
                        "C2 low root, G3 fifth, E3 third). Default: Off." },
        { "omniTempo", "Omnichord rhythm tempo (follows the host tempo when the host is playing)." },
        { "omniRhythmLevel", "Volume of the Omnichord drums." },
        { "drumPattern2", "Second pattern played at the same time, like holding two rhythm buttons on the Mini Pops 7: hits add up, and waltz or triplet rhythms make the counter skip steps (the trick behind the Oxygene rhythms)." },
        { "drumKit", "Drum kit: Korg Mini Pops 7 (1966), CR-78 (1978) or TR-808 (1980), synthesized from their circuit tunings. Keys follow the General MIDI drum map (C2 kick, D2 snare, F#2 closed hat, A#2 open hat...)." },
        { "drumPattern", "Pattern played by the drum machine. Off: play the kit from the keys only. Patterns are KoniSynth's own, in each style. "
                         "User 1-8: your own rhythms, one MIDI file each (drums on channel 10, General MIDI notes) in the KoniSynth "
                         "settings folder, rhythms/Drum Machine/User 1.mid ... User 8.mid; edit them in any sequencer and they reload by "
                         "themselves. An empty slot plays nothing." },
        { "drumTempo", "Pattern tempo (follows the host tempo and position when the host is playing)." },
        { "drumSwing", "Delays every second 16th note. 0 = straight; 33 % = triplet feel." },
        { "drumTune", "Tunes all drums together, in semitones." },
        { "drumDecay", "Scales all decay times (x0.25 to x4; x1 = the factory decay of each voice)." },
        { "drumTone", "Brightness: kick click, snare tone and noise filters." },
        { "drumSnappy", "Amount of snare wires (noise) in the snare." },
        { "drumAccent", "Level of accented steps and of notes played at full velocity." },
        { "styloModel", "1968: the original Stylophone (relaxation oscillator, vintage tuning, vibrato switch). Gen X-2: Dubreq's 2025 analogue synth (pulse, sub-octaves, resonant filter, envelope, LFO, delay), EXPERIMENTAL: controls from the official manual, ranges estimated." },
        { "styloRegister", "1968 model: which of the three originals (each with 20 keys): Standard A2-E4, Bass A1-E3, Treble A3-E5. With Range = Original it sets the playable range; it always sets the attack (Treble fastest, from its smaller capacitor)." },
        { "genxPwm", "Gen X-2 'X' button: the LFO sweeps the pulse width (official manual). EXPERIMENTAL: amounts estimated." },
        { "genxSub1", "Gen X-2 '-1' button: adds a square one octave below." },
        { "genxSub2", "Gen X-2 '-2' button: adds a square two octaves below." },
        { "genxAttack", "Gen X-2 envelope attack: how fast the filter (and the pitch, if Env Pitch is up) opens. Range estimated." },
        { "genxDecay", "Gen X-2 envelope decay: how fast the filter closes again. Off = the filter stays open (sustain), as the manual describes. Range estimated." },
        { "genxPitch", "Gen X-2 envelope PITCH dial: at zero the envelope only moves the filter; turned up, it also moves the pitch (up to +24 semitones here, estimated)." },
        { "genxLfoWave", "Gen X-2 LFO wave: triangle or square." },
        { "genxLfoRate", "Gen X-2 LFO rate, waves per second. Range estimated." },
        { "genxLfoDepth", "Gen X-2 LFO depth: modulates the pitch (vibrato) and the filter cutoff at the same time, as on the original." },
        { "genxCutoff", "Gen X-2 resonant low-pass filter cutoff." },
        { "genxRes", "Gen X-2 filter resonance (feedback)." },
        { "genxDelay", "Gen X-2 analogue delay on/off." },
        { "genxTime", "Gen X-2 delay time between the sound and its echo. Range estimated." },
        { "genxRepeats", "Gen X-2 delay repeats (feedback). Each repeat gets darker, like a bucket-brigade delay (estimated)." },
        { "genxLevel", "Gen X-2 delay level: amount of echo." },
        { "genxStrip", "Gen X-2 expression strip (here, the pitch-bend wheel): Filter = wah from the centre, Osc = pitch bend, LFO = vibrato amount (only the upper half, as the strip's right side)." },
        { "genxOctave", "Gen X-2 OCTAVE switch: transposes by -1, 0 or +1 octave." },
        { "drumVelocity", "Off (default, as the drum machines): a MIDI note plays at the normal level, or accented from velocity "
                          "100. On: the velocity sets the level continuously." },
        { "styloRange", "Full keyboard: every key plays its own note (each keeps the vintage tuning of the matching original key). Original (20 keys): as on the real Stylophone, notes outside the model's 20 keys are moved by octaves into its range." },
        { "styloVibrato", "Stylophone vibrato switch: about 7 Hz, as measured on the original." },
        { "styloVibDepth", "Vibrato depth in cents (not documented for the original; 30 ct is an estimate)." },
        { "styloTuning", "Vintage: the original resistor chain, about 33 cents stretched over the 20 keys (low keys flat). Equal: in tune." },
        { "styloOutput", "Speaker: the small built-in speaker (bright, no bass). Line: the line output, darkened by its 154 Hz low-pass." },
        { "styloClick", "Stylus click when it touches a key." },
        { "wave", "Oscillator 1 waveform. Sine: pure; triangle: soft; saw: bright; square: hollow; Tri-Saw: the Minimoog triangle-sawtooth (mostly triangle with a little saw): softer than saw, with a bit more edge than triangle. "
                  "Saw+Pulse, Saw+Tri, Tri+Pulse, Saw+Tri+Pulse: waveforms switched on together in one Prophet-5 oscillator, mixed in its "
                  "proportions (the pulse follows Pulse Width). Saw+Pulse (Juno): the Juno-106 saw and pulse together, at equal level. Default: Sine." },
        { "attack", "Time for the volume to rise when you press a key." },
        { "decay", "Time for the volume to fall from its peak to the Sustain level." },
        { "sustain", "Volume level while you hold the key." },
        { "release", "Time for the sound to fade out after you release the key." },
        { "drift", "Analog instability: slow random detuning, like a vintage synth warming up." },
        { "osc1Level", "Oscillator 1 volume in the mixer." },
        { "pulseWidth", "Square wave pulse width (also the pulse in the Saw+Pulse, Tri+Pulse and Saw+Tri+Pulse waveforms of Osc 1, Osc 2 "
                        "and the modulator): 50 % sounds hollow; extreme values sound thin and nasal." },
        { "pwmDepth", "How much LFO 1 moves the pulse width (PWM): adds motion and body to the square wave and the pulse mixes. "
                      "It is the attenuator of the LFO 1 -> PWM cable in Mod Patch (0 % = no cable). Default: 0 %." },
        { "osc2Wave", "Oscillator 2 waveform. Tri-Saw: the Minimoog triangle-sawtooth. Saw+Pulse, Saw+Tri, Tri+Pulse, "
                      "Saw+Tri+Pulse: Prophet-5 waveform mixes; Saw+Pulse (Juno): the Juno-106 mix (see Wave). Wavetable: plays its "
                      "own table (Osc 2 Table... in the header), independent of the Wavetable tab, at its own position (WT Pos), "
                      "with its own tuning and phase; Sync works too. Sample: plays its own audio file (Osc 2 Sample... in the header: "
                      "Load, Root, Start, Length, Loop), a different one from the Sample tab, with the tuning, drift, level and mute "
                      "of Osc 2; Sync restarts it from Start every cycle of Osc 1. Default: Saw." },
        { "osc2WtPosition", "Position of oscillator 2 in its own wavetable (Wavetable waveform only; the table is chosen in "
                            "Osc 2 Table...). Default 0 %." },
        { "osc2WtTable", "Wavetable of oscillator 2 (Wavetable waveform only), independent of the Wavetable tab: built-in "
                         "tables or your own file (Osc 2 Table... > Load: WAV with 2048 samples per frame, the Serum format, "
                         "or a single cycle). Default: Basic Shapes." },
        { "osc2Octave", "Oscillator 2 octave relative to oscillator 1." },
        { "osc2Semi", "Oscillator 2 offset in semitones (7 = fifth, 12 = octave)." },
        { "osc2Fine", "Oscillator 2 fine detune in cents: a few cents give a thick beating sound." },
        { "osc2Level", "Oscillator 2 volume in the mixer." },
        { "osc2Sync", "Hard-syncs oscillator 2 to oscillator 1: raise its tuning for the classic metallic sync lead. Patch it in "
                      "the PATCH panel of the Complex Oscillator (OSC 1 -> SYNC 2). Default: Off." },
        { "subLevel", "Oscillator 3 volume. With its defaults (Square, Oct -1) it is the classic sub-oscillator that adds weight "
                      "to the bass; its wave and tuning (Oct, Semi, Fine) make it a full third oscillator, like Osc 2." },
        { "osc3Octave", "Oscillator 3 octave relative to oscillator 1, -2 to +2. Default -1 (one octave down: the classic "
                        "sub-oscillator). Above 0 it suits Sync and the Complex modulator (OSC 3 -> MOD)." },
        { "osc3Wave", "Oscillator 3 waveform. Square (default, 50 % pulse) is the classic sub-oscillator; Sine gives a clean "
                      "fundamental (the Init preset uses it); Tri-Saw is the Minimoog triangle-sawtooth; the Saw/Tri/Pulse mixes are "
                      "the Prophet-5 and Juno-106 ones (see Wave), with the pulse at 50 %. Wavetable: plays its own table (Osc 3 "
                      "Table... in the header) at its own position (WT Pos). Sample: plays its own audio file (Osc 3 Sample... in the "
                      "header: Load, Root, Start, Length, Loop) with the tuning, level and mute of Osc 3. Sync works with all of them." },
        { "osc3Sync", "Hard-syncs oscillator 3 to oscillator 1: every cycle of Osc 1 restarts it (its table from the start of the "
                      "cycle, its sample from Start); raise its tuning for the metallic sync sound. Patch it in the PATCH panel of "
                      "the Complex Oscillator (OSC 1 -> SYNC 3). Default: Off." },
        { "osc3WtPosition", "Position of oscillator 3 in its own wavetable (Wavetable waveform only; the table is chosen in "
                            "Osc 3 Table...). Matrix destination: Osc 3 WT Position. Default 0 %." },
        { "osc3WtTable", "Wavetable of oscillator 3 (Wavetable waveform only), independent of the Wavetable tab and of Osc 2: "
                         "built-in tables or your own file (Osc 3 Table... > Load: WAV with 2048 samples per frame, the Serum "
                         "format, or a single cycle). Default: Basic Shapes." },
        { "osc3SampleRoot", "Osc 3 Sample: the note at which its file plays at its original pitch (before the Oct, Semi "
                            "and Fine of Osc 3: at Oct -1 it sounds one octave lower). Default: C4." },
        { "osc3SampleStart", "Osc 3 Sample: playback start point, also where Sync restarts it. Default: 0 %." },
        { "osc3SampleLength", "Osc 3 Sample: cuts the file after this time with a short fade (Full = the whole file). Default: Full." },
        { "osc3SampleLoop", "Osc 3 Sample: loops the file from Start to the end (or its own loop, if the file has one) while "
                            "the note is held. Default: Off." },
        { "osc3Semi", "Oscillator 3 offset in semitones (7 = fifth, 12 = octave), -12 to +12. Default 0." },
        { "osc3Fine", "Oscillator 3 fine detune in cents, -50 to +50. Default 0." },
        { "osc1TriSym", "Triangle symmetry of Oscillator 1 (Triangle waveform only): adds the even harmonics of an analog triangle "
                        "that is slightly lopsided. About 50-60 % matches a Minimoog Model D. Default 0 % (ideal triangle)." },
        { "osc1TriRound", "Rounds the tips of the Oscillator 1 triangle (Triangle waveform only): softer, with weaker odd harmonics; "
                          "100 % is a sine. The filter already rounds them a little: about 0-20 % matches a Minimoog Model D (30-50 % with Filter off). Default 0 % (sharp tips)." },
        { "osc2TriSym", "Triangle symmetry of Oscillator 2 (Triangle waveform only): adds the even harmonics of an analog triangle "
                        "that is slightly lopsided. About 50-60 % matches a Minimoog Model D. Default 0 % (ideal triangle)." },
        { "osc2TriRound", "Rounds the tips of the Oscillator 2 triangle (Triangle waveform only): softer, with weaker odd harmonics; "
                          "100 % is a sine. The filter already rounds them a little: about 0-20 % matches a Minimoog Model D (30-50 % with Filter off). Default 0 % (sharp tips)." },
        { "osc3TriSym", "Triangle symmetry of Oscillator 3 (Triangle waveform only): adds the even harmonics of an analog triangle "
                        "that is slightly lopsided. About 50-60 % matches a Minimoog Model D. Default 0 % (ideal triangle)." },
        { "osc3TriRound", "Rounds the tips of the Oscillator 3 triangle (Triangle waveform only): softer, with weaker odd harmonics; "
                          "100 % is a sine. The filter already rounds them a little: about 0-20 % matches a Minimoog Model D (30-50 % with Filter off). Default 0 % (sharp tips)." },
        { "noiseLevel", "Noise volume in the mixer (wind, percussion, breath)." },
        { "noiseColor", "Noise colour: white (bright hiss), pink (darker, like rain) or LFSR digital noise from 8-bit game chips (Short = metallic, tonal noise that follows the key)." },
        { "wtLevel", "Wavetable oscillator volume in the mixer: a stack of waveforms you can sweep through (Position)." },
        { "wtTable", "Wavetable: built-in tables or your own file (WAV with 2048 samples per frame, the Serum format, or a single cycle)." },
        { "wtPosition", "Position inside the wavetable: sweeps from the first to the last waveform. LFO 2 can move it." },
        { "stringLevel", "Math Models volume in the mixer: a sound computed in real time from a formula (not a sample): a string "
                         "plucked, bowed, struck or driven, or a set of fading modes (Modal)." },
        { "stringDecay", "How long the string rings once it is free (seconds to fall by 60 dB): after a pluck or a hammer, or after "
                         "you release the key with Bow and E-Bow. With Modal it scales every decay of the recipe (x1 at 3 s)." },
        { "stringBright", "Brightness: with Noise Burst and Pick, the pluck (soft finger to hard plectrum); with Bow, Hammer and "
                          "E-Bow, the high-frequency loss of the string at the bridge." },
        { "stringPick", "Where the string is plucked, bowed, struck or driven: near the end (low %) is bright and thin; the "
                        "middle (50 %) is hollow and round." },
        { "stringBowPressure", "Bow pressure (with Bow, and with Banded when bowed): low = airy, high = rough; too little or too much stops the steady tone, "
                               "as with a real bow. Default: 50 %." },
        { "stringHammerFelt", "Felt hardness of the hammer (with Hammer): soft piano to hard cimbalom; a harder felt gives a "
                              "shorter, brighter contact. Default: 50 %." },
        { "stringEbowDrive", "Drive strength of the electromagnet (with E-Bow): how fast the note swells and how loud it "
                             "settles. Default: 50 %." },
        { "stringBuzz", "Sitar bridge (with Noise Burst and Pick): the string grazes a curved bridge and buzzes (jawari), as on "
                        "the sitar and tanpura; more Buzz = the bridge closer to the string. 0 % = no bridge. Adapted from "
                        "Siddiq (2012): only the part of the string next to the bridge is computed his way." },
        { "stringHarmonic", "Harmonic (flageolet): a finger lightly touching the string at 1/2, 1/3, 1/4 or 1/5 of its length, so "
                            "only the partials with a node there remain: the note sounds 1, 1.58, 2 or 2.32 octaves up with a "
                            "glassy tone. Off = open string." },
        { "sampleLevel", "Loaded sample volume in the mixer; it goes through the filter and envelopes like an oscillator." },
        { "sampleRoot", "The note at which the sample plays at its original pitch." },
        { "sampleStart", "Sample playback start point." },
        { "sampleLength", "Cuts the sample after this time with a short fade (0 = full sample). Short values give LA-style synthesis: a sampled attack transient over a synthesized sustain, as on the Roland MT-32 and D-50." },
        { "sampleLoop", "Loops the sample from Start to the end while the note is held." },
        { "osc2SampleRoot", "Osc 2 Sample: the note at which its file plays at its original pitch (before the Oct, Semi and Fine "
                            "of Osc 2). Default: C4." },
        { "osc2SampleStart", "Osc 2 Sample: playback start point, also where Sync restarts it. Default: 0 %." },
        { "osc2SampleLength", "Osc 2 Sample: cuts the file after this time with a short fade (Full = the whole file). Default: Full." },
        { "osc2SampleLoop", "Osc 2 Sample: loops the file from Start to the end (or its own loop, if the file has one) while "
                            "the note is held. Default: Off." },
        { "mdSampleRoot", "Mega Drive DAC sample: the note at which its file plays at the stored rate (its original pitch). Default: C4." },
        { "mdSampleStart", "Mega Drive DAC sample: playback start point within its file. Default: 0 %." },
        { "mdSampleLength", "Mega Drive DAC sample: cuts the file after this time (Full = the whole file). Default: Full." },
        { "mdSampleLoop", "Mega Drive DAC sample: loops it from Start while the note is held. Default: Off." },
        { "snesSampleRoot", "SNES sample: the note at which its file plays at the stored rate (its original pitch). Default: C4." },
        { "snesSampleStart", "SNES sample: playback start point within its file. Default: 0 %." },
        { "snesSampleLength", "SNES sample: cuts the file after this time (Full = the whole file). Default: Full." },
        { "snesSampleLoop", "SNES sample: loops it from Start while the note is held. Default: Off." },
        { "n64SampleRoot", "N64 sample: the note at which its file plays at the stored rate (its original pitch). Default: C4." },
        { "n64SampleStart", "N64 sample: playback start point within its file. Default: 0 %." },
        { "n64SampleLength", "N64 sample: cuts the file after this time (Full = the whole file). Default: Full." },
        { "n64SampleLoop", "N64 sample: loops it from Start while the note is held. Default: Off." },
        { "dpcmSampleStart", "8-Bit/DPCM user sample: start point within its file before converting it to DPCM (not used with a .dmc file, which plays as it is). Default: 0 %." },
        { "dpcmSampleLength", "8-Bit/DPCM user sample: part of the file converted to DPCM (Full = up to the 4081-byte limit, about 1 s). Default: Full." },
        { "inputGain", "Audio input gain (mic or line). Raise it if your mic is quiet." },
        { "inputMonitor", "Sends the input into the effects chain: your voice or instrument with echo, reverb, tape..." },
        { "vocoderOn", "Turns the vocoder on: your voice (input) shapes the synth sound. Play notes while you speak." },
        { "vocoderBank", "Filter bank: VP-330 (10 bands, Roland), 16 bands (EMS style) or 32 (most intelligible)." },
        { "vocoderMix", "Balance between the unprocessed sound and the vocoder." },
        { "vocoderAttack", "How fast the vocoder follows the voice as it gets louder." },
        { "vocoderRelease", "How fast the vocoder lets go as the voice gets quieter: high = smoother, low = more articulate." },
        { "vocoderFormant", "Shifts the synth bands: deeper voice (negative) or higher voice (positive)." },
        { "vocoderSibilance", "Lets the voice's unvoiced consonants (s, t, f) through so words are clearer." },
        { "unisonVoices", "Oscillators per note (unison): more = wider, thicker sound, more CPU." },
        { "unisonDetune", "Detune between the unison oscillators, in cents." },
        { "unisonSpread", "Stereo width of the unison oscillators." },
        { "voicePan", "Spreads the notes across the stereo field: each new note gets its own position (whole voice: oscillators, sample, wavetable). 0 = all centered." },
        { "voicePanMode", "How Voice Pan places each note: Alternate (left, right, left...), Keyboard (low notes left, high notes right, C2..C6) or Random." },
        { "mediaOn", "Records the mix onto a simulated medium (cassette, vinyl, reel-to-reel tape)." },
        { "mediaType", "Medium: cassette, vinyl or reel-to-reel tape at 38, 19 or 9.5 cm/s, or Underwater (head under water: Age = depth, Noise = bubbles, Wow = waves)." },
        { "mediaAge", "Wear of the medium: duller highs and more imperfections." },
        { "mediaNoise", "Level of the medium's own noise (tape hiss, vinyl crackle)." },
        { "mediaWow", "Speed instability of the medium (wow and flutter): wavering pitch." },
        { "mediaDrive", "Recording saturation: tape compression and warmth." },
        { "chorusOn", "Turns the chorus on: doubles the sound with small variations to widen it." },
        { "chorusMode", "Juno-60 Chorus I / II / I+II, or string machine Ensemble (Solina)." },
        { "chorusMix", "Chorus level against the original signal." },
        { "chorusDepth", "Chorus modulation depth." },
        { "filterOn", "Turns the 4-pole ladder filter (Moog style) on." },
        { "filterMode", "Filter model and response. LP 24 / LP 12 / BP 12 / HP 12: Moog-style ladder. SVF: state-variable "
                        "(Oberheim SEM style), smooth, with notch. MS-20: Korg35 Sallen-Key, aggressive resonance that screams. Comb: "
                        "tuned resonator (Cutoff = pitch; with Key at 100 % it follows the notes), Reso = feedback. Dual: two SVF "
                        "filters spaced by Spread (BP in parallel: vowel-like peaks; LP in series)." },
        { "stringPickup", "Magnetic pickup position, as a fraction of the string length from the bridge (Off: the string itself). "
                          "Each harmonic k is shaped by 2·|sin(π·k·position)|: at 25 % harmonics 2 and 3 stand out and the 4th "
                          "vanishes, like the bridge pickup of an electric bass." },
        { "stringExcite", "The model. All are published models adapted to KoniSynth (Formula... shows each one). "
                          "Noise Burst (Karplus 1983): a burst of filtered noise, classic and a bit grainy. Pick (Jaffe 1983): "
                          "the shape of a plucked string, a triangle with its tip at Pick Position. Bow (McIntyre 1983): a bow "
                          "rubs the string while the key is held (stick-slip friction). Hammer (Hall 1992): a felt hammer strikes "
                          "it, as in a piano. E-Bow (Heet 1978): an electromagnet drives the string while the key is held and "
                          "the note swells (own model of the device). Modal (Adrien 1991): a sum of fading sines with their own "
                          "ratios and decays, edited in Formula... (Soft Key, Metal or your own). Reed (Guillemain 2005): a "
                          "clarinet, a reed on a cylindrical bore, blown while the key is held; velocity is the blowing "
                          "pressure (soft and round to bright). Flute (Karjalainen 1991): an "
                          "air jet across a tube, breathy, it overblows with Jet. Brass (Cook 1991): buzzing lips on a tube, "
                          "a trumpet or trombone. Banded (Essl 1999): a bar, a glass or a bowl, struck or bowed, one "
                          "loop per mode. Shaker (Cook 1997): maracas, tambourine and other shakers, many small random "
                          "collisions through a few resonances. Mallet (Avanzini 2001): a mallet with a nonlinear felt that strikes the "
                          "modes of the Modal recipe; the harder and faster the strike, the shorter the contact and the "
                          "brighter the tone. The panel shows only the controls of the chosen model." },
        { "malletHardness", "Hardness of the Mallet model's head, as its contact time on a rigid surface: from 10 ms (soft "
                            "felt, only the low modes) to 0.3 ms (hard plastic, every mode). Faster strikes are shorter and "
                            "brighter. Default: 50 % (1.7 ms)." },
        { "malletMass", "Weight of the Mallet model's head, from 0.01 to 10 times the mass of the struck object: light = one "
                        "clean bounce; heavy = it bounces several times and damps the sound. Default: 33 % (0.1)." },
        { "malletDamp", "Losses of the Mallet model's felt: low = an elastic, ringing bounce; high = a dull, padded one "
                        "(less energy goes back into the mallet). Default: 20 %." },
        { "bandedShape", "Object of the Banded model: Uniform Bar (inharmonic, short), Tuned Bar (a vibraphone bar), Glass "
                         "Harmonica (a rubbed glass) or Tibetan Bowl (12 measured modes in close pairs that beat). Decay sets "
                         "how long it rings. Default: Tuned Bar." },
        { "bandedExcite", "How the Banded model is played: Strike (a mallet, it rings on its own) or Bow (rubbed while the key "
                          "is held; it builds up in about a second and Press sets the bow pressure: too much chokes it). "
                          "Default: Strike." },
        { "shakerType", "Instrument of the Shaker model: Maraca, Cabasa, Sekere, Tambourine, Sleigh Bells or Bamboo Chimes. "
                        "Each note is one shake, stronger with velocity; releasing the key stops it. At C4 the resonances are "
                        "those of the instrument; other notes move them. Default: Maraca." },
        { "shakerObjects", "Number of beans, beads or jingles in the Shaker model, from a quarter to four times the "
                           "instrument's: few = separate clicks, many = a smooth hiss. Default: 50 % (the instrument's)." },
        { "shakerShake", "How long one shake of the Shaker model lasts: low = a short flick, high = a long rattle. Default: "
                         "50 % (the instrument's)." },
        { "shakerTone", "Pitch of the Shaker model's resonances, from 1 octave down to 1 octave up (smaller or larger "
                        "shells and jingles), on top of the note. Default: 50 %." },
        { "reedEmbouchure", "Embouchure of the Reed model (how the lips hold the reed, the maximum airflow): low = a pinched, "
                            "thinner tone; high = a loose, fuller one that squeaks more easily. Default: 33 %." },
        { "reedNoise", "Breath noise of the Reed model: small, fast changes of the blowing pressure. Default: 50 %." },
        { "fluteJet", "Jet length of the Flute model, as a ratio of the tube: short jets overblow to the upper modes (a higher, "
                      "brighter note), long ones give a soft, unstable tone. At the default the flute is in tune from C4 to "
                      "C7, its real range. Default: 50 % (the STK flute)." },
        { "fluteNoise", "Breath noise of the Flute model: the air hiss of the jet. Default: 38 %." },
        { "brassLip", "Lip tension of the Brass model, from 2 octaves below to 2 octaves above the note: the lips buzz at "
                      "their own pitch and the tube locks to the nearest harmonic, so turning it jumps between notes like a "
                      "real player. Default: 50 % (the lips at the played note)." },
        { "brassSlide", "Tube length of the Brass model, from 0.5 to 1.5 times its length (a trombone slide): with the lips "
                        "fixed, the pitch bends and jumps. Default: 50 % (x1)." },
        { "stringStiff", "Stiffness of the string (Noise Burst, Pick, Bow, Hammer, E-Bow): a stiff string pushes its upper "
                         "partials sharp, f_k = k f0 sqrt(1 + B k^2), as in a piano, a bass string or a bell-like wire. "
                         "Off at 0; B from 0.00001 to 0.001. Default: Off." },
        { "stringTension", "Tension modulation of the string (Noise Burst, Pick, Bow, Hammer, E-Bow): a string that vibrates "
                           "widely is stretched and sounds sharper, then settles to its pitch as it fades, and it creates "
                           "partials the pluck had left out. Stronger with velocity. Off at 0; at 100 % a strong pluck starts "
                           "about a semitone sharp. Default: Off." },
        { "stringDamping", "High-frequency loss inside the string: upper partials die out sooner than the fundamental, as in a real "
                           "wound string. The fundamental keeps its Decay time and its tuning (compensated; with long Decay times the damping "
                           "is limited to what can be compensated)." },
        { "filterSpread", "Distance between the two filters of the Dual modes, in octaves (centred on Cutoff)." },
        { "cutoff", "Cutoff frequency: lower (LP) sounds darker; higher sounds brighter." },
        { "resonance", "Emphasis at the cutoff frequency. At maximum the filter whistles on its own (self-oscillation)." },
        { "filterDrive", "Saturation at the filter input: more growl and harmonics." },
        { "filterEnv", "How far the filter envelope opens the filter on each note." },
        { "filterAttack", "Filter envelope attack." },
        { "filterDecay", "Filter envelope decay." },
        { "filterSustain", "Filter envelope sustain level." },
        { "envCurve", "Shape of the amp and filter envelopes. Linear: straight ramps (digital). Analog RC: the curves of the classic "
                      "envelope chips (CEM3310): the attack rushes up and bends into the peak, decay and release fall fast then slow. "
                      "Snappier and more natural at the same times." },
        { "filterRelease", "Filter envelope release after you let go of the key." },
        { "filterKey", "Keyboard tracking: higher notes open the filter more (at 100 % it follows note for note)." },
        { "velToFilter", "Playing harder opens the filter more." },
        { "thereminRange", "Theremin span in octaves across the pad." },
        { "thereminGlide", "Hand inertia: how long the pitch takes to follow the mouse." },
        { "thereminTone", "Timbre: from a pure sine to a wave richer in harmonics." },
        { "thereminModel", "Physical: physical model (non-linear field, locking); Ideal: linear, stable tuning." },
        { "thereminField", "Antenna field linearity: low = strongly compressed towards the antenna." },
        { "thereminLock", "Oscillator locking: a silent zone and low growl near zero beat." },
        { "thereminZero", "Hand distance at which the theremin goes silent (zero beat)." },
        { "thereminPitch", "Pitch hand position (pad X axis)." },
        { "thereminVolume", "Volume hand position (pad Y axis)." },
        { "vl1Sound", "Casio VL-1 sound. Electro 1-3 alternate octaves in time with the tempo." },
        { "vl1Octave", "VL-1 octave (-1, 0, +1)." },
        { "vl1Attack", "A: attack (0-9), like the digit in the VL-1 ADSR code." },
        { "vl1Decay", "D: decay to the sustain level (0-9)." },
        { "vl1SustainLevel", "SL: sustain level (0-9)." },
        { "vl1SustainTime", "ST: how long the sustain lasts (0-9; 9 = forever)." },
        { "vl1Release", "R: release after you let go (0-9)." },
        { "vl1Vibrato", "V: vibrato (0 = off; 1 fast and subtle ... 9 slow and deep)." },
        { "vl1Tremolo", "T: tremolo (0 = off; 1 fast ... 9 slow)." },
        { "vl1Rhythm", "VL-1 rhythm box (Rock 1 is the one from \"Da Da Da\"). Off stops it. User 1-8: your own rhythms (Edit Rhythms...), one MIDI "
                       "file each in the KoniSynth settings folder, rhythms/Casio VL-1/User 1.mid ... User 8.mid: channel 10, notes 36 "
                       "(Pock), 37 (Peek) and 42 (Noise), one sound per sixteenth. Default: Off." },
        { "vl1Tempo", "VL-1 tempo, from -9 to +9 (0 = about 131 BPM)." },
        { "vl1Balance", "Balance between melody and rhythm." },
        { "crushOn", "Turns the bit crusher on: lowers resolution and sample rate for 8-bit game, early sampler and lo-fi textures." },
        { "crushBits", "Resolution in bits: 16 = clean, 8 = classic sampler grit, 4 = 8-bit game chip, 1-2 = harsh digital noise." },
        { "crushRate", "Sample rate of the crushed signal: low values add metallic aliasing (no anti-alias filter, like the old machines)." },
        { "crushMix", "Balance between the clean and the crushed signal." },
        { "crushDrive", "Level going into the crusher (compensated afterwards): higher = the quantization steps cover more of the signal, coarser 8-bit grain." },
        { "phaserOn", "Turns the 4-stage phaser on: the sweeping, swirling sound of 70s string machines and electric pianos." },
        { "phaserRate", "Phaser sweep speed." },
        { "phaserDepth", "How wide the phaser sweeps." },
        { "phaserFeedback", "Color: feeds the phaser output back into itself for a stronger, more vocal sweep." },
        { "phaserMix", "Phaser level against the original signal (50 % = classic deepest notches)." },
        { "tubeOn", "Turns the tube saturation on." },
        { "tubeDrive", "How hard the tube is driven: from subtle warmth to distortion." },
        { "ampOn", "Turns the tube amp and cabinet on (after the color effects, before dynamics). Adds a few samples of latency (4x oversampling)." },
        { "ampModel", "Amp: each one has the tone stack of the original circuit (component values) and its number of gain stages. Bassman 59 and Twin 69: clean to crunch; JCM800: classic rock; Mark, SLO-100 and VL501: high gain." },
        { "ampGain", "Preamp gain: how hard the tube stages are driven. Low = clean, high = distortion." },
        { "ampBass", "Bass control of the tone stack (log taper, like the real pot). Interacts with Mid and Treble as in the circuit." },
        { "ampMid", "Mid control of the tone stack. At 0 the classic mid scoop." },
        { "ampTreble", "Treble control of the tone stack." },
        { "ampPresence", "Presence: upper treble boost in the power amp feedback loop (up to +8 dB around 3 kHz and above)." },
        { "ampMaster", "Power amp drive: low = clean power stage, high = power tube saturation and compression." },
        { "ampSag", "Power supply sag: the headroom drops on loud passages (spongy feel, more with the tube rectifier amps like the Bassman)." },
        { "ampOutput", "Output level of the amp, to match the level with the amp off." },
        { "cabType", "Speaker cabinet: built-in approximations of typical cabinets (filters, not measured from a specific one), or User IR to load your own impulse response (Load IR...). Off = amp only (direct out)." },
        { "cabMix", "Mix between the cabinet and the direct amp signal." },
        { "transOn", "Turns the transient shaper on (after the EQ, before the compressor). Level-independent: no threshold to set." },
        { "transAttack", "Attack: positive = punchier, sharper note starts (up to +12 dB on the onset); negative = softer, rounder starts." },
        { "transSustain", "Sustain: positive = longer tails, more body and room; negative = drier, shorter notes (up to 12 dB)." },
        { "compOn", "Turns the compressor on (after the transient shaper, before the modulation effects). Linked stereo." },
        { "compMode", "Character. VCA: clean and precise, times as set. Opto: smooth RMS detection and soft knee; release slows down after long compression (LA-2A style). FET: feedback detection (the detector hears the compressed output, 1176 style): very fast attack possible and the attack gets quicker as the ratio rises; aggressive. Same static ratio in the three modes." },
        { "compThreshold", "Level above which the compressor starts reducing gain." },
        { "compRatio", "How much the output rises above the threshold: at 4:1, 4 dB more input gives 1 dB more output. 2:1 gentle, 4:1 firm, 10:1 and up is limiting. Default 4:1." },
        { "compAttack", "How fast the compressor reacts. Short = catches the transients (flatter); longer = lets the attack through (punchier)." },
        { "compRelease", "How fast the gain comes back after the level drops. Short = more pumping and loudness; long = smoother." },
        { "compKnee", "Width of the transition around the threshold: 0 dB = hard knee, wider = gradual, more transparent. Opto uses at least 12 dB." },
        { "compMakeup", "Gain added after compression to recover the lost level." },
        { "compMix", "Dry/compressed mix: below 100 % is parallel (New York) compression, keeps the attacks and adds density." },
        { "compScHpf", "High-pass filter on the detector only (not on the sound): the lows stop driving the compressor, less pumping on bass. Off at 20 Hz." },
        { "compSource", "What drives the compressor. Internal: the synth itself. Audio In: the audio input (ducking: a voice or a kick lowers the synth)." },
        { "limOn", "Turns the output limiter on (the very last stage, after Gain and Mono): the sample peaks never exceed the ceiling." },
        { "limCeiling", "Maximum output sample peak. -0.3 dB leaves a little room for converters; inter-sample peaks can still go slightly above." },
        { "limRelease", "How fast the gain returns after a peak: short = louder but can distort the bass; long = cleaner." },
        { "limLookahead", "Off = zero latency (gain drops on the peak itself, may add slight distortion on sharp peaks). 1-5 ms = transparent, but adds that latency (reported to the host)." },
        { "eqOn", "Turns the parametric EQ on (after the tube, before modulation and space: shapes the tone like an amp EQ)." },
        { "eqLowCut", "Low cut (high-pass) frequency: removes rumble and mud below it. At 20 Hz it is off." },
        { "eqLowCutSlope", "Steepness of the low cut: 6 dB/oct very gentle (one pole, like the Juno-60 HPF: its positions 1-3 sit at about 235, 516 and 1098 Hz), 12 gentle, 24 standard, 48 brick-wall. Default 24." },
        { "eqLowFreq", "Low shelf corner frequency (bass band)." },
        { "eqLowGain", "Low shelf boost or cut: weight and body of the sound." },
        { "eqMid1Freq", "Centre frequency of mid band 1 (typical: 200-800 Hz for body or mud, a scoop around 500 Hz for metal)." },
        { "eqMid1Gain", "Boost or cut of mid band 1." },
        { "eqMid1Q", "Width of mid band 1: low Q = broad and musical, high Q = narrow (surgical cuts, resonances)." },
        { "eqMid2Freq", "Centre frequency of mid band 2 (typical: 1-5 kHz for presence, attack, pick clank)." },
        { "eqMid2Gain", "Boost or cut of mid band 2." },
        { "eqMid2Q", "Width of mid band 2." },
        { "eqHighFreq", "High shelf corner frequency (treble band)." },
        { "eqHighGain", "High shelf boost or cut: air and brightness, or darker and warmer." },
        { "eqHighCut", "High cut (low-pass) frequency: tames fizz and hiss above it. At 20 kHz it is off." },
        { "eqHighCutSlope", "Steepness of the high cut: 6 dB/oct very gentle (one pole), 12 gentle, 24 standard, 48 brick-wall. Default 24." },
        { "eqOutput", "EQ output level, to compensate boosts or cuts." },
        { "eqLowCutOn", "Low Cut stage on/off (bypass without losing its settings; the change glides in 20 ms). Default on." },
        { "eqLowOn", "Low shelf on/off (bypass without losing its settings; the change glides in 20 ms). Default on." },
        { "eqMid1On", "Mid 1 band on/off (bypass without losing its settings; the change glides in 20 ms). Default on." },
        { "eqMid2On", "Mid 2 band on/off (bypass without losing its settings; the change glides in 20 ms). Default on." },
        { "eqHighOn", "High shelf on/off (bypass without losing its settings; the change glides in 20 ms). Default on." },
        { "eqHighCutOn", "High Cut stage on/off (bypass without losing its settings; the change glides in 20 ms). Default on." },
        { "tubeBias", "Tube asymmetry: adds even harmonics (warmer sound)." },
        { "lfoOn", "Turns the LFO (slow modulation oscillator) on." },
        { "lfoTarget", "What the LFO modulates: pitch (vibrato), volume (tremolo) or filter (wah)." },
        { "lfoRate", "LFO speed (up to 100 Hz: audio-rate wobble and buzz). Also sets the speed of the mod wheel vibrato." },
        { "lfoShape", "LFO waveform. S&H jumps to a new random value every cycle; Random glides smoothly between random values." },
        { "lfoSync", "Locks the LFO to the tempo as a note value (Free = use Rate in Hz). Uses the host tempo, or Arp BPM in the standalone." },
        { "lfo2On", "Turns the second LFO on." },
        { "lfo2Target", "What LFO 2 modulates: pitch, volume, filter, pulse width, the pitch of oscillator 2 only, or the wavetable position." },
        { "lfo2Shape", "LFO 2 waveform." },
        { "lfo2Rate", "LFO 2 speed." },
        { "lfo2Sync", "Locks LFO 2 to the tempo as a note value (Free = use Rate in Hz)." },
        { "lfo2Depth", "LFO 2 amount." },
        { "fineTune", "Fine tuning of the whole synth in cents." },
        { "lfoDepth", "LFO amount." },
        { "seqOn", "Turns the step sequencer on: holding a key plays the pattern transposed to that key (overrides the arpeggiator)." },
        { "seqSteps", "Number of steps in the pattern (1-16): odd lengths give shifting, irregular cycles." },
        { "seqRate", "Step length, synced to the host tempo when it is playing (T = triplets); otherwise Arp BPM sets the tempo." },
        { "seqGate", "How long each step sounds, as a fraction of the step." },
        { "seqLatch", "Keeps the sequence running after you release the key; pressing a new key transposes it." },
        { "keyTranspose", "Transposes every played note by this many semitones, before the scale, the sequencer and the arpeggiator "
                          "(not the Drum Machine nor the MT-32 rhythm part)." },
        { "scaleMode", "Keeps the notes in a scale. Round: a note outside the scale plays the nearest one in it (see Rounding). "
                       "Filter: notes outside the scale do not sound. Off: every note as played." },
        { "scaleRoot", "Root note of the scale (the keys of the scale are marked on the keyboard)." },
        { "scaleType", "The scale: Major, Minor (natural), Harmonic and Melodic Minor, the church modes (Dorian, Phrygian, Lydian, "
                       "Mixolydian, Locrian), the two pentatonics, Blues, Whole Tone and Phrygian Dominant." },
        { "scaleRound", "Where a note outside the scale goes in Round mode: Nearest (the closer scale note; half way between two, "
                        "the lower one), Down or Up." },
        { "arpOn", "Turns the arpeggiator on: held notes are played one after another in a pattern." },
        { "arpMode", "Arpeggio pattern: up, down, up and down, random or in the order you played the notes." },
        { "arpRate", "Arpeggio step length, synced to the host tempo when it is playing (T = triplets)." },
        { "arpOctaves", "How many octaves the arpeggio spans." },
        { "arpGate", "How long each arpeggio note sounds, as a fraction of the step: low = staccato, high = legato." },
        { "arpLatch", "Keeps the arpeggio running after you release the keys; a new chord replaces it." },
        { "arpOneShot", "Plays the arpeggio pattern once per key press instead of looping (game sound effects such as coins and power-ups)." },
        { "arpBpm", "Internal tempo (Master > Tempo for engines without a clock of their own): arpeggiator, sequencer, synced "
                    "echo and LFOs when the host does not provide one (e.g. the standalone app). Default 120 BPM." },
        { "voiceMode", "Poly: chords. Mono: one note at a time, last key wins (classic monosynth). Legato: like Mono, but overlapping notes slide without restarting the envelopes." },
        { "glide", "Portamento: time to slide from the previous note to the new one (0 = off)." },
        { "pitchEnvAmount", "Pitch drop or rise at the start of each note, in semitones: the key to synthesized kicks, toms and laser zaps." },
        { "pitchEnvDecay", "How fast the pitch envelope returns to the note's pitch." },
        { "atFilter", "Aftertouch to filter: pressing harder on held keys opens the filter (up to +3 octaves)." },
        { "atVibrato", "Aftertouch to vibrato: pressing harder adds vibrato (up to +/-1 semitone at the LFO 1 rate)." },
        { "mtxSrc1", "Modulation matrix slot 1: the source (LFOs, envelopes, velocity, key, wheel, aftertouch, MPE timbre, a random value per note, pitch bend)." },
        { "mtxDst1", "Modulation matrix slot 1: what the source moves, for each voice separately (pitch, cutoff, resonance, levels, pan...)." },
        { "mtxAmt1", "Modulation matrix slot 1: how much and in which direction (negative inverts). At 100 %: pitch ±24 semitones, cutoff ±5 octaves, full range for the rest." },
        { "mtxSrc2", "Modulation matrix slot 2: the source (LFOs, envelopes, velocity, key, wheel, aftertouch, MPE timbre, a random value per note, pitch bend)." },
        { "mtxDst2", "Modulation matrix slot 2: what the source moves, for each voice separately (pitch, cutoff, resonance, levels, pan...)." },
        { "mtxAmt2", "Modulation matrix slot 2: how much and in which direction (negative inverts). At 100 %: pitch ±24 semitones, cutoff ±5 octaves, full range for the rest." },
        { "mtxSrc3", "Modulation matrix slot 3: the source (LFOs, envelopes, velocity, key, wheel, aftertouch, MPE timbre, a random value per note, pitch bend)." },
        { "mtxDst3", "Modulation matrix slot 3: what the source moves, for each voice separately (pitch, cutoff, resonance, levels, pan...)." },
        { "mtxAmt3", "Modulation matrix slot 3: how much and in which direction (negative inverts). At 100 %: pitch ±24 semitones, cutoff ±5 octaves, full range for the rest." },
        { "mtxSrc4", "Modulation matrix slot 4: the source (LFOs, envelopes, velocity, key, wheel, aftertouch, MPE timbre, a random value per note, pitch bend)." },
        { "mtxDst4", "Modulation matrix slot 4: what the source moves, for each voice separately (pitch, cutoff, resonance, levels, pan...)." },
        { "mtxAmt4", "Modulation matrix slot 4: how much and in which direction (negative inverts). At 100 %: pitch ±24 semitones, cutoff ±5 octaves, full range for the rest." },
        { "mtxSrc5", "Modulation matrix slot 5: the source (LFOs, envelopes, velocity, key, wheel, aftertouch, MPE timbre, a random value per note, pitch bend)." },
        { "mtxDst5", "Modulation matrix slot 5: what the source moves, for each voice separately (pitch, cutoff, resonance, levels, pan...)." },
        { "mtxAmt5", "Modulation matrix slot 5: how much and in which direction (negative inverts). At 100 %: pitch ±24 semitones, cutoff ±5 octaves, full range for the rest." },
        { "mtxSrc6", "Modulation matrix slot 6: the source (LFOs, envelopes, velocity, key, wheel, aftertouch, MPE timbre, a random value per note, pitch bend)." },
        { "mtxDst6", "Modulation matrix slot 6: what the source moves, for each voice separately (pitch, cutoff, resonance, levels, pan...)." },
        { "mtxAmt6", "Modulation matrix slot 6: how much and in which direction (negative inverts). At 100 %: pitch ±24 semitones, cutoff ±5 octaves, full range for the rest." },
        { "mtxSrc7", "Modulation matrix slot 7: the source (LFOs, envelopes, velocity, key, wheel, aftertouch, MPE timbre, a random value per note, pitch bend)." },
        { "mtxDst7", "Modulation matrix slot 7: what the source moves, for each voice separately (pitch, cutoff, resonance, levels, pan...)." },
        { "mtxAmt7", "Modulation matrix slot 7: how much and in which direction (negative inverts). At 100 %: pitch ±24 semitones, cutoff ±5 octaves, full range for the rest." },
        { "mtxSrc8", "Modulation matrix slot 8: the source (LFOs, envelopes, velocity, key, wheel, aftertouch, MPE timbre, a random value per note, pitch bend)." },
        { "mtxDst8", "Modulation matrix slot 8: what the source moves, for each voice separately (pitch, cutoff, resonance, levels, pan...)." },
        { "mtxAmt8", "Modulation matrix slot 8: how much and in which direction (negative inverts). At 100 %: pitch ±24 semitones, cutoff ±5 octaves, full range for the rest." },
        { "mtxRandomFade", "Random Fade: how much the matrix Random source fades out with the filter envelope. 0 % = each note keeps its random offset; 100 % = the offset follows the filter envelope, so with sustain 0 every note starts off target and settles on it during the decay (random pitch that converges, varied attacks). Default 0 %." },
        { "mpeOn", "MPE (MIDI Polyphonic Expression, lower zone): channel 1 is the master (its bend moves every note); channels 2-16 carry one note each, with its own pitch bend, pressure and CC74 timbre (use them in the modulation matrix: Aftertouch, MPE Timbre). For Roli, Linnstrument, Osmose, Continuum..." },
        { "voiceAssign", "Which voice plays each new note. Lowest Free: the first idle voice (repeated single notes use the same voice). "
                         "Reuse Same Note: a repeated key retriggers its own voice, like the Prophet-5. Round Robin: every note moves on to the "
                         "next voice, like the Prophet-5 Rev 4 option (with Per Voice Card tolerances each note sounds a bit different)." },
        { "oscPhase", "Oscillator phase when a note starts. Auto: random if Drift is up, else from zero. Reset: always from zero (same "
                      "attack every time, punchy basses). Free Running: the oscillators keep running between notes like analog VCOs. "
                      "Random: a random phase per note." },
        { "voiceTolerance", "Where the Drift tolerances (detune, cutoff spread, thermal coefficient) live. Per Note: new values for each note. "
                            "Per Voice Card: fixed per physical voice, like the voice cards of a vintage polysynth." },
        { "filterCleanDrive", "Clean filter input. Off (default): the input stage always saturates a little, even at Drive 0 % "
                              "(analog character, 3rd harmonic of a full-scale sine at about -29 dB). On: Drive 0 % is linear and the "
                              "saturation fades in over the first 25 % of Drive. SVF, comb and dual modes become linear; the ladder and MS-20 "
                              "keep their own core saturation (3rd harmonic about -34 dB)." },
        { "voiceSatMode", "Curve of the per-voice saturation (after or before the filter, see Position; each note on its own: chords stay clean of "
                          "intermodulation). Soft: tanh, like an overdriven OTA. Diode: asymmetric, adds even harmonics. Hard: clipping. "
                          "Fold: sine wavefolder (West Coast); more drive = more folds. Antialiased (ADAA), no added latency." },
        { "voiceSatDrive", "Per-voice saturation drive. Off at 0 dB; fades in over the first 3 dB. Level is compensated for Soft, Diode "
                           "and Hard (a full-scale oscillator keeps its peak level); Fold is not. Curve: Saturation... in the Filter header." },
        { "voiceSatPos", "Where the per-voice saturation sits. Post-filter: between filter and amp, it adds harmonics to the "
                         "filtered sound. Pre-filter: the filter smooths the harmonics it adds (drive into the filter). Default: Post-filter." },
        { "vcaClick", "Analog VCA control feedthrough: the amp envelope leaks into the audio and the output coupling turns it into a "
                      "thump at each note (strongest with fast attacks). 4 %: worst case of a modern AS3360 (10 mV in 10 Vpp); 100 %: an "
                      "untrimmed vintage VCA. Polarity and amount vary per voice (per voice card with Tolerances = Per Voice Card)." },
        { "vcaLeak", "Analog VCA bleed: the VCA never closes completely, so after the release each voice keeps sounding at this level "
                     "until it is reused (like the oscillator bleed of a Minimoog). Off: silence after the release. Uses CPU for the "
                     "voices that keep running." },
        { "mpeBendRange", "Pitch bend range of each note in MPE mode (semitones). The MPE standard default is 48." },
        { "atAmp", "Aftertouch to volume: pressing harder makes the note louder (up to +6 dB), like the CS-80." },
        { "formantOn", "Turns the formant filter on: makes any sound speak or sing a vowel." },
        { "formantVoice", "Formant set: Male (bass singer) or Female (soprano singer)." },
        { "formantVowel", "Vowel: A, E, I, O, U; values in between morph smoothly." },
        { "formantShift", "Shifts all formants up or down (smaller or larger 'throat')." },
        { "formantRate", "Speed of the automatic vowel morph." },
        { "formantDepth", "How many vowels the automatic morph sweeps (0 = off)." },
        { "formantMix", "Balance between the original and the vowel-filtered sound." },
        { "bendRange", "Pitch bend wheel range of your controller, in semitones." },
        { "macro1", "Performance macro 1: moves all the parameters assigned to it at once, each between its From and To values. Assign a parameter: right-click it > Assign to macro. Edit ranges and names: Macros > Assign..." },
        { "macro2", "Performance macro 2: moves all the parameters assigned to it at once, each between its From and To values. Assign a parameter: right-click it > Assign to macro. Edit ranges and names: Macros > Assign..." },
        { "macro3", "Performance macro 3: moves all the parameters assigned to it at once, each between its From and To values. Assign a parameter: right-click it > Assign to macro. Edit ranges and names: Macros > Assign..." },
        { "macro4", "Performance macro 4: moves all the parameters assigned to it at once, each between its From and To values. Assign a parameter: right-click it > Assign to macro. Edit ranges and names: Macros > Assign..." },
        { "macro5", "Performance macro 5: moves all the parameters assigned to it at once, each between its From and To values. Assign a parameter: right-click it > Assign to macro. Edit ranges and names: Macros > Assign..." },
        { "macro6", "Performance macro 6: moves all the parameters assigned to it at once, each between its From and To values. Assign a parameter: right-click it > Assign to macro. Edit ranges and names: Macros > Assign..." },
        { "macro7", "Performance macro 7: moves all the parameters assigned to it at once, each between its From and To values. Assign a parameter: right-click it > Assign to macro. Edit ranges and names: Macros > Assign..." },
        { "macro8", "Performance macro 8: moves all the parameters assigned to it at once, each between its From and To values. Assign a parameter: right-click it > Assign to macro. Edit ranges and names: Macros > Assign..." },
        { "modWheelDepth", "Mod wheel depth (CC 1): vibrato up to +/-1 semitone." },
        { "echoOn", "Turns the tape echo on." },
        { "echoTime", "Time between repeats." },
        { "echoFeedback", "Number of repeats. Above 100 % the echo feeds itself (self-oscillation)." },
        { "echoMix", "Volume of the repeats." },
        { "echoTone", "Brightness of the repeats: low = each repeat darker, like worn tape." },
        { "echoSync", "Locks the echo time to the tempo as a note value (D = dotted, T = triplet). Free = use Time in ms." },
        { "echoWow", "Echo tape instability: wavering pitch on the repeats." },
        { "echoPingPong", "Ping-pong: the repeats bounce left, right, left... (the input is summed to mono into the left side). Off: each side echoes on its own." },
        { "ambientOn", "Turns the plate reverb on." },
        { "ambientSize", "Size of the space." },
        { "ambientMix", "Reverb level against the original signal." },
        { "reverbDecay", "Length of the reverb tail (seconds to fall by 60 dB)." },
        { "reverbPredelay", "Gap before the reverb starts: separates the dry sound from the tail." },
        { "reverbDiffusion", "Density of the early echoes: low = distinct echoes; high = smooth tail." },
        { "reverbDamp", "Frequency above which the tail gets progressively darker." },
        { "reverbTone", "Brightness of the sound going into the reverb." },
        { "reverbMod", "Internal tail modulation: avoids metallic ringing." },
        { "reverbWidth", "Stereo width of the reverb." },
        { "reverbFreeze", "Freezes the reverb tail indefinitely (infinite pad)." },
        { "complexOn", "Turns the complex oscillator (Buchla 259 style) on for oscillator 1." },
        { "modWave", "Modulator oscillator waveform (Tri-Saw: the Minimoog triangle-sawtooth; the Saw/Tri/Pulse mixes: see Wave). "
                     "Default: Sine." },
        { "modRatio", "Modulator/main frequency ratio: whole numbers = harmonic; others = bell-like." },
        { "fmIndex", "FM amount: more = richer, more metallic timbre." },
        { "amDepth", "Amplitude modulation; at 100 % it is ring modulation (robot or bell sound)." },
        { "timbre", "Wavefolding: progressively adds harmonics." },
        { "symmetry", "Wavefolder asymmetry." },
        { "timbreMod", "The modulator moves the wavefolder." },
        { "modLp", "Low-pass filter on the Complex modulator (internal, Osc 2 or Osc 3) before the FM and the AM: a saw or square "
                   "modulator becomes rounder, for softer FM. One pole (6 dB/oct). Default: Off (fully up)." },
        { "snesSource", "Sound source (BRR sample). Square, Saw, Sine: 32-sample loops; Strings, Choir: 4400-sample loops (3 detuned voices); Bass: 64-sample loop. All are KoniSynth's own samples, BRR-encoded. User Sample: its own file (Sample... button in this section), re-encoded to BRR; a .brr or .spc file plays as it is." },
        { "snesUserRate", "Rate at which the user sample is stored in BRR (32, 16 or 8 kHz). Lower = more of the classic muffled SNES sound and longer samples within the 64 KB of audio RAM." },
        { "snesInterp", "Sample interpolation. Gaussian: the S-DSP's own 4-point filter with its 512-entry table (the muffled SNES sound). Linear and None are only for comparison: the console can't do them." },
        { "snesEnvMode", "Envelope: ADSR (hardware attack/decay/sustain, release -8 per sample) or one of the GAIN modes: direct level, linear down, exponential down, linear up, bent up." },
        { "snesAttack", "ADSR attack (0-15). 15 = instant (+1024 per step); 0 = about 4.0 s (63 steps of 2048 samples)." },
        { "snesDecay", "ADSR decay rate (0-7), exponential, from full level to the sustain level." },
        { "snesSustainLevel", "ADSR sustain level (0-7): where the decay stops, (N+1)/8 of full level." },
        { "snesSustainRate", "ADSR sustain rate (0-31): exponential decay while the key is held. 0 = holds." },
        { "snesGainRate", "GAIN rate (0-31) for the linear, exponential and bent modes (the same 32-rate table as the envelope)." },
        { "snesDirectGain", "Direct GAIN level (0-127), used in Direct Gain mode." },
        { "snesNoise", "Replace the sample with the S-DSP noise generator (15-bit LFSR, not interpolated)." },
        { "snesNoiseRate", "Noise clock (0-31): 1 = 16 Hz ... 30 = 16 kHz, 31 = 32 kHz." },
        { "snesVolume", "Voice volume register (0-127); velocity scales it when Velocity is on." },
        { "snesMainVolume", "Main volume register MVOL (0-127)." },
        { "snesVelocity", "Key velocity scales the voice volume (KoniSynth's driver; the S-DSP itself has no velocity)." },
        { "snesEcho", "Send the voices to the echo (EON)." },
        { "snesEdl", "Echo delay EDL (0-15): 16 ms per step, up to 240 ms (0 = 1 sample)." },
        { "snesEfb", "Echo feedback EFB (-128..127). 64 = normal decaying echo, 127 = almost endless." },
        { "snesEvol", "Echo volume EVOL (-128..127)." },
        { "snesFir", "Echo FIR filter: Flat (7F 00 00 00 00 00 00 00) or the low-pass set from the official specs (FF 08 17 24 24 17 08 FF)." },
        { "snesDac", "How the 32 kHz samples become sound. Hold (default, as a non-oversampling DAC of the time): each sample is "
                     "held until the next, with its gentle top-octave droop and images above 16 kHz. Smooth: a low-pass resampler "
                     "removes the images and rolls off from about 12 kHz (cleaner than the console)." },
        { "snesFilter", "Analog output stage (blargg's model: gentle low-pass, -6 dB at 16 kHz, and a 20 Hz high-pass). Off = raw DSP output." },
        { "chipExpansion", "Cartridge expansion sound. VRC6 (Konami, Akumajou Densetsu): 2 extra pulses and a sawtooth, mixed linearly with the 2A03. Famicom only, as on the real console (the NES has no expansion audio without a mod). Play it with Voice = VRC6 Pulse, VRC6 Saw, Poly + VRC6 (6 voices) or MIDI Channels (6-8)." },
        { "chipVrc6Duty", "VRC6 pulse duty: 1/16 to 8/16 (the 2A03 only has 1/8, 1/4, 1/2, 3/4)." },
        { "chipVrc6Overdrive", "VRC6 sawtooth: lets the volume push the accumulator rate above 42, where the original's 8-bit accumulator overflows and the saw breaks into a harsher wave (a real quirk of the chip)." },
        { "midiInChannel", "MIDI channel KoniSynth listens to. Omni = all. Set it to your keyboard's channel when it also sends accompaniment or metronome on other channels (Casio CT-S400: 1). The on-screen keyboard always plays." },
        { "opllInstrument", "OPLL / VRC7 instrument from the chip's ROM (dumped from the silicon). 0 = User: built from OP1 (modulator) and OP2 (carrier) of the OPL editor (Waveform 1 = half sine). The names are Yamaha's for the YM2413; the VRC7 has its own, different set (no official names)." },
        { "opllVolume", "OPLL channel volume register: attenuation in 3 dB steps (0 = loudest). Velocity adds to it." },
        { "opllSustain", "OPLL SUS bit: after the key is released, the note fades with a slow fixed release instead of the instrument's own." },
        { "opllRhythm", "YM2413 rhythm mode: channels 7-9 become bass drum, snare, tom, cymbal and hi-hat, played on MIDI channel 10 (General MIDI drum notes); 6 melodic voices remain. The VRC7 has no rhythm output." },
        { "oplModel", "OPL2 = Yamaha YM3812 (AdLib, Sound Blaster 1-2): 9 two-operator voices, 4 waveforms, mono, and the YM3014 floating-point DAC that drops the low bits of loud signals (its typical grit). OPL3 = YMF262 (Sound Blaster Pro 2, 16): 18 voices or 6 four-operator voices, 8 waveforms, stereo. OPLL = YM2413 (Japanese Master System FM, MSX-MUSIC): 9 voices, 15 ROM instruments + 1 user, optional drums. VRC7 = Konami's OPLL in Lagrange Point: 6 voices, its own ROM set. OPLL and VRC7 are cycle-accurate (Nuked-OPLL)." },
        { "oplFourOp", "OPL3 only: pairs of channels become 4-operator voices (6 voices) with 4 algorithms." },
        { "oplAlgorithm", "How the operators connect. 2 operators: 0 = FM (1 modulates 2), 1 = AM (1 + 2 added). 4 operators (OPL3): 0 = 1->2->3->4, 1 = (1->2)+(3->4), 2 = 1+(2->3->4), 3 = 1+(2->3)+4." },
        { "oplFeedback", "Self-modulation of OP1 (register C0h): 0 = off, 7 = most (noisy)." },
        { "oplDeepTremolo", "Register BDh bit 7 (DAM): tremolo depth 4.8 dB instead of 1 dB, for operators with Tremolo on." },
        { "oplDeepVibrato", "Register BDh bit 6 (DVB): vibrato depth 14 cents instead of 7, for operators with Vibrato on." },
        { "oplVelocity", "Velocity lowers the carriers' Total Level (0.75 dB steps), as a sound driver would; the chip itself has no velocity." },
        { "oplPan", "OPL3 stereo (register C0h bits 4-5: left, right or both). Alternate: voices go left, right, left... The OPL2 is mono." },
        { "oplOutput", "Stereo (OPL3) or both sides summed to mono." },
        { "mt32Model", "Which Roland ROM set to use from your ROM folder: MT-32 (first-generation control ROM preferred, the one most "
                       "late-80s PC games were written for) or CM-32L (more sound effects, the later DAC). Falls back to the other "
                       "if only that one is present." },
        { "mt32Mode", "What the keyboard plays. Part 1: MIDI channel 2 of the MT-32 with the Program next to it. Rhythm: channel 10, "
                      "the drum kit. Not used with Multi on." },
        { "mt32Multi", "MIDI channels as they come instead of the keyboard routing, for MT-32 .mid files (parts on channels 2-9, "
                       "rhythm on 10; import them with the transport). SysEx always passes (custom patches and LCD messages)." },
        { "mt32NiceAmp", "Munt beautification, off = as the real unit: amp ramps may jump to their target (small clicks). On: "
                         "always gradual." },
        { "mt32NicePan", "Munt beautification, off = as the real unit: the LA32 only gets 3-bit panning (no exact centre for a "
                         "single partial). On: 4-bit, smoother." },
        { "mt32NiceMix", "Munt beautification, off = as the real unit: partials may mix in counter-phase and cancel out. On: "
                         "always in phase." },
        { "mt32Program", "Patch of Part 1 (1-128, the MT-32 preset bank). The list shows each patch's sound name as stored in "
                         "your ROMs (or as changed by SysEx)." },
        { "mt32Reverb", "The MT-32's own digital reverb (room, hall, plate, tap delay), as set by the patch or by SysEx." },
        { "mt32Dac", "How the LA32 output reaches the DAC. Auto: as the unit does (Gen 1 for MT-32 v1.x: the famous digital "
                     "overflow distortion on loud notes; Gen 2 for later units). Nice: double volume, clean clipping. Pure: the "
                     "LA32 bits as they are, half volume. Gen 1 / Gen 2: force the bit order of the early or the later units "
                     "whatever ROM is loaded." },
        { "opl1Am", "OP1: Tremolo (AM): this operator follows the chip LFO, 3.7 Hz, depth set by Deep Tremolo." },
        { "opl1Vib", "OP1: Vibrato (VIB): this operator follows the chip vibrato, 6.1 Hz, depth set by Deep Vibrato." },
        { "opl1Egt", "OP1: EGT: on = the envelope holds at Sustain Level while the key is down; off = it keeps decaying at the Release Rate (percussive)." },
        { "opl1Ksr", "OP1: KSR: envelopes get faster in higher octaves." },
        { "opl1Mult", "OP1: Frequency multiple: 0 = x0.5, 1-10, 12, 15 (11 and 13 give 10 and 12, as on the chip)." },
        { "opl1Ksl", "OP1: Key scale level: attenuation that grows with pitch (0, 1.5, 3 or 6 dB per octave)." },
        { "opl1Tl", "OP1: Total level: attenuation in 0.75 dB steps (63 = -47 dB). On a modulator it sets the brightness." },
        { "opl1Ar", "OP1: Attack rate (15 = instant)." },
        { "opl1Dr", "OP1: Decay rate from full level down to the Sustain Level." },
        { "opl1Sl", "OP1: Sustain level in 3 dB steps (15 = -93 dB)." },
        { "opl1Rr", "OP1: Release rate after the key is released (and the decay after Sustain when EGT is off)." },
        { "opl1Wave", "OP1: Waveform (register E0h). The OPL2 has the first 4 (when the program enabled them); the OPL3 all 8." },
        { "opl2Am", "OP2: Tremolo (AM): this operator follows the chip LFO, 3.7 Hz, depth set by Deep Tremolo." },
        { "opl2Vib", "OP2: Vibrato (VIB): this operator follows the chip vibrato, 6.1 Hz, depth set by Deep Vibrato." },
        { "opl2Egt", "OP2: EGT: on = the envelope holds at Sustain Level while the key is down; off = it keeps decaying at the Release Rate (percussive)." },
        { "opl2Ksr", "OP2: KSR: envelopes get faster in higher octaves." },
        { "opl2Mult", "OP2: Frequency multiple: 0 = x0.5, 1-10, 12, 15 (11 and 13 give 10 and 12, as on the chip)." },
        { "opl2Ksl", "OP2: Key scale level: attenuation that grows with pitch (0, 1.5, 3 or 6 dB per octave)." },
        { "opl2Tl", "OP2: Total level: attenuation in 0.75 dB steps (63 = -47 dB). On a modulator it sets the brightness." },
        { "opl2Ar", "OP2: Attack rate (15 = instant)." },
        { "opl2Dr", "OP2: Decay rate from full level down to the Sustain Level." },
        { "opl2Sl", "OP2: Sustain level in 3 dB steps (15 = -93 dB)." },
        { "opl2Rr", "OP2: Release rate after the key is released (and the decay after Sustain when EGT is off)." },
        { "opl2Wave", "OP2: Waveform (register E0h). The OPL2 has the first 4 (when the program enabled them); the OPL3 all 8." },
        { "opl3Am", "OP3: Tremolo (AM): this operator follows the chip LFO, 3.7 Hz, depth set by Deep Tremolo." },
        { "opl3Vib", "OP3: Vibrato (VIB): this operator follows the chip vibrato, 6.1 Hz, depth set by Deep Vibrato." },
        { "opl3Egt", "OP3: EGT: on = the envelope holds at Sustain Level while the key is down; off = it keeps decaying at the Release Rate (percussive)." },
        { "opl3Ksr", "OP3: KSR: envelopes get faster in higher octaves." },
        { "opl3Mult", "OP3: Frequency multiple: 0 = x0.5, 1-10, 12, 15 (11 and 13 give 10 and 12, as on the chip)." },
        { "opl3Ksl", "OP3: Key scale level: attenuation that grows with pitch (0, 1.5, 3 or 6 dB per octave)." },
        { "opl3Tl", "OP3: Total level: attenuation in 0.75 dB steps (63 = -47 dB). On a modulator it sets the brightness." },
        { "opl3Ar", "OP3: Attack rate (15 = instant)." },
        { "opl3Dr", "OP3: Decay rate from full level down to the Sustain Level." },
        { "opl3Sl", "OP3: Sustain level in 3 dB steps (15 = -93 dB)." },
        { "opl3Rr", "OP3: Release rate after the key is released (and the decay after Sustain when EGT is off)." },
        { "opl3Wave", "OP3: Waveform (register E0h). The OPL2 has the first 4 (when the program enabled them); the OPL3 all 8." },
        { "opl4Am", "OP4: Tremolo (AM): this operator follows the chip LFO, 3.7 Hz, depth set by Deep Tremolo." },
        { "opl4Vib", "OP4: Vibrato (VIB): this operator follows the chip vibrato, 6.1 Hz, depth set by Deep Vibrato." },
        { "opl4Egt", "OP4: EGT: on = the envelope holds at Sustain Level while the key is down; off = it keeps decaying at the Release Rate (percussive)." },
        { "opl4Ksr", "OP4: KSR: envelopes get faster in higher octaves." },
        { "opl4Mult", "OP4: Frequency multiple: 0 = x0.5, 1-10, 12, 15 (11 and 13 give 10 and 12, as on the chip)." },
        { "opl4Ksl", "OP4: Key scale level: attenuation that grows with pitch (0, 1.5, 3 or 6 dB per octave)." },
        { "opl4Tl", "OP4: Total level: attenuation in 0.75 dB steps (63 = -47 dB). On a modulator it sets the brightness." },
        { "opl4Ar", "OP4: Attack rate (15 = instant)." },
        { "opl4Dr", "OP4: Decay rate from full level down to the Sustain Level." },
        { "opl4Sl", "OP4: Sustain level in 3 dB steps (15 = -93 dB)." },
        { "opl4Rr", "OP4: Release rate after the key is released (and the decay after Sustain when EGT is off)." },
        { "opl4Wave", "OP4: Waveform (register E0h). The OPL2 has the first 4 (when the program enabled them); the OPL3 all 8." },
        { "mdVoice", "What the keys play. FM: the 6 YM2612 channels. FM + PSG: each note also on a square-wave channel of the SN76489 PSG. PSG Tone: only the 3 PSG square channels (lowest note ~109 Hz, as on the console). PSG Noise: the PSG noise channel (mono). DAC Sample: the Sample panel's sound through channel 6's 8-bit DAC (mono; Root plays at the DAC Rate). FM Ch3 Special: channel 3 with its own frequency for OP1-OP3 (mono)." },
        { "mdPsgNoise", "PSG noise type. White: 16-bit shift register with taps on bits 0 and 3 (Sega's PSG). Periodic: a 16-step buzz whose pitch is the noise rate / 16." },
        { "mdPsgNoiseRate", "PSG noise clock. Key: follows the note through tone channel 3's divider, as drivers did for pitched noise and periodic bass. Fixed: 3,579,545 Hz / 512, 1024 or 2048 shifts per second." },
        { "mdPsgAttack", "PSG attack, set by the sound driver (the chip has no envelope): frames per 2 dB step from silence to full volume, at 59.92 frames per second. 0 = instant." },
        { "mdPsgDecay", "PSG decay: frames per 2 dB step from full volume down to Sustain. 0 = instant." },
        { "mdPsgSustain", "PSG sustain: attenuation held while the key is down, in 2 dB steps (0 = full volume, 15 = silent)." },
        { "mdPsgRelease", "PSG release: frames per 2 dB step after the key is released (15 steps to silence). 0 = instant." },
        { "mdDacRate", "Rate at which the sound driver writes the sample into channel 6's DAC (register 2Ah). The Root note of the Sample panel plays at this rate; the DAC holds each 8-bit value until the next write. With Loop off the sample plays to the end, like drum samples in games." },
        { "mdCh3Op1", "Channel 3 Special mode: OP1 frequency in semitones from the played note (registers A9h/ADh). OP4 always plays the note." },
        { "mdCh3Op2", "Channel 3 Special mode: OP2 frequency in semitones from the played note (registers AAh/AEh). OP4 always plays the note." },
        { "mdCh3Op3", "Channel 3 Special mode: OP3 frequency in semitones from the played note (registers A8h/ACh). OP4 always plays the note." },
        { "mdPan", "Stereo of the 6 FM channels (YM2612 register B4h: hard left, right or both). Alternate: voices go left, right, left... Spread: left, center, right... The real chip had no in-between positions." },
        { "snesPan", "Stereo balance of the voices (left/right volume registers of each voice)." },
        { "snesSpread", "Spreads successive notes to opposite sides (left/right volume registers), as a sound driver placing its channels." },
        { "chipPan1", "Game Boy NR51: where Pulse 1 goes (center, left, right or off). Stereo only on the Game Boy headphone jack; the NES and Famicom are mono, so it has no effect there." },
        { "chipPan2", "Game Boy NR51: where Pulse 2 goes. No effect on the NES (mono)." },
        { "chipPan3", "Game Boy NR51: where the Wave channel goes. No effect on the NES (mono)." },
        { "chipPan4", "Game Boy NR51: where the Noise channel goes. No effect on the NES (mono)." },
        { "mdModel", "Mega Drive model. Model 1: discrete YM2612 with its 'ladder effect' (crossover distortion in the 9-bit DAC) and the 3.4 kHz output low-pass. Model 2: YM3438 in the ASIC, no ladder, same low-pass. Clean: YM3438 without the analog filter." },
        { "mdAlgorithm", "How the 4 operators connect (0-7), as on the YM2612. 0: 1->2->3->4 (one carrier) ... 7: all four heard. Carriers are highlighted in Operators..." },
        { "mdFeedback", "OP1 feedback (0-7): from pure sine to buzzy and, at 7, noisy." },
        { "mdLfo", "Chip LFO speed (the 8 YM2612 rates) or Off." },
        { "mdPms", "Vibrato depth from the LFO (0-7: 0 to 80 cents)." },
        { "mdAms", "Tremolo depth from the LFO on operators with AM on (0-3: 0, 1.4, 5.9, 11.8 dB)." },
        { "mdVelocity", "Key velocity lowers the carriers' level (KoniSynth's driver; the chip itself has no velocity)." },
        { "modSync", "Hard-syncs the main oscillator to the modulator." },
        { "complexModSource", "Which source modulates oscillator 1 in the complex oscillator. Internal (default): its own "
                              "modulator (Mod Wave and Ratio). Osc 2 or Osc 3: that oscillator with its tuning, wave, "
                              "wavetable position or sample (Mod Wave and Ratio do nothing); it modulates even at level 0 or "
                              "muted, so turn its level down to hear only the modulation. Noise: the voice's noise with its "
                              "colour (FM and AM by noise: rough, gritty tones), also at Noise level 0 or muted." },
    };
    const auto it = help.find (id);
    if (it != help.end())
        return juce::String::fromUTF8 (it->second);
    // Pasos del secuenciador (stepNote1..16, stepOn1..16, stepTie1..16)
    // Operadores FM: fm<N><campo>
    if (id.startsWith ("fm") && id.length() > 3 && juce::CharacterFunctions::isDigit (id[2]))
    {
        const auto op = "OP" + id.substring (2, 3) + ": ";
        const auto f = id.substring (3);
        if (f == "Coarse")    return op + "frequency ratio (0 = 0.5, 1, 2 ... 31), or in Fixed mode 1 / 10 / 100 / 1000 Hz.";
        if (f == "Fine")      return op + "fine ratio: multiplies by 1 + Fine/100 (in Fixed mode, by 10^(Fine/100)).";
        if (f == "Detune")    return op + "slight detune (-7..+7, about +/-7 cents around A4).";
        if (f == "Fixed")     return op + "fixed frequency: ignores the key (drum and noise effects).";
        if (f == "Level")     return op + "output level (0-99). On a carrier it is volume; on a modulator, brightness (99 = about 12.6 rad of modulation).";
        if (f.startsWith ("R")) return op + "envelope rate " + f.substring (1) + " (0 slow ... 99 instant). 1 attack, 2-3 decay, 4 release.";
        if (f.startsWith ("L") && f.length() == 2) return op + "envelope level " + f.substring (1) + " (0-99). Level 3 is the sustain; level 4 the end of the release.";
        if (f == "Vel")       return op + "velocity sensitivity (0-7).";
        if (f == "RateScale") return op + "rate scaling (0-7): higher notes have faster envelopes, like a real string.";
        if (f == "Ams")       return op + "amplitude modulation sensitivity to the FM LFO (0-3).";
        if (f == "Break")     return op + "keyboard level scaling break point: the note where the left and right curves start.";
        if (f == "LDepth")    return op + "level scaling depth below the break point.";
        if (f == "RDepth")    return op + "level scaling depth above the break point.";
        if (f == "LCurve")    return op + "level scaling curve below the break point (-/+ linear or exponential).";
        if (f == "RCurve")    return op + "level scaling curve above the break point (-/+ linear or exponential).";
    }
    // Operadores Mega Drive: md<N><campo>, rangos nativos del YM2612
    if (id.startsWith ("md") && id.length() > 3 && juce::CharacterFunctions::isDigit (id[2]))
    {
        const auto op = "OP" + id.substring (2, 3) + ": ";
        const auto f = id.substring (3);
        if (f == "Tl")  return op + "total level, attenuation in 0.75 dB steps (0 = loudest, 127 = -95 dB). On a carrier it is volume; on a modulator, brightness.";
        if (f == "Ar")  return op + "attack rate (0 = never, 31 = instant). Exponential attack, like the chip.";
        if (f == "Dr")  return op + "first decay rate, from full level down to the sustain level (0 = none, 31 = fastest).";
        if (f == "Sr")  return op + "second decay rate (sustain rate): keeps decaying while the key is held (0 = holds).";
        if (f == "Sl")  return op + "sustain level: where the first decay ends, 3 dB per step (15 = -93 dB).";
        if (f == "Rr")  return op + "release rate after the key is released (0 slow ... 15 fastest).";
        if (f == "Mul") return op + "frequency multiple: 0 = x0.5, 1 = x1 ... 15 = x15.";
        if (f == "Dt")  return op + "detune: -3..+3 small pitch offsets that grow with the note, as on the chip.";
        if (f == "Ks")  return op + "key scale (0-3): higher notes get faster envelopes.";
        if (f == "Am")  return op + "amplitude modulation by the LFO (depth set by LFO Amp Sens). Note: with the LFO off, AM still attenuates, as on the real chip.";
        if (f == "Ssg") return op + "SSG-EG: the envelope repeats, alternates or holds (uses the decay rates). Off = normal envelope.";
    }
    if (id.startsWith ("ansReg")) return "ANS register " + id.substring (6) + ": half-octave band " + id.substring (6)
                                          + " of 20 (the original had 20 photocells, each with its own half-octave filter).";
    if (id.startsWith ("stepNote")) return "Step " + id.fromFirstOccurrenceOf ("stepNote", false, false) + " pitch, in semitones from the key you hold (0 = that key).";
    if (id.startsWith ("stepOn"))   return "Step " + id.fromFirstOccurrenceOf ("stepOn", false, false) + " plays (on) or rests (off).";
    if (id.startsWith ("stepTie"))  return "Step " + id.fromFirstOccurrenceOf ("stepTie", false, false) + " ties into the next step: no new attack (with Legato voice mode it slides, like a TB-303).";
    return {};
}
