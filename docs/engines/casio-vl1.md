# Casio VL-1

The Casio VL-1 engine emulates the Casio VL-1 (VL-Tone, 1979): a monophonic pocket keyboard with a digital 8-digit
"ADSR" sound code and a built-in rhythm box. Waveforms, the digital envelope, the output stage, the clock and the rhythm
sounds come from the PolyValens VL-1 emulator (2006, CC0/MIT, github.com/linuxmao-org/VL1-emulator). Use it for
toy-keyboard melodies, the "Da Da Da" sound, and lo-fi monophonic lines with a rhythm underneath.

As with every emulated engine in KoniSynth, the defaults reproduce the original behaviour, quirks included. Anything
that makes it "nicer" than the hardware is an option and is off by default; this engine has no such options.

## What you need

Nothing extra.

## Controls

Select **Casio VL-1** in the Engine menu. Choosing it from that menu loads the preset **VL-1 Piano**. The engine shows
three sections: **Casio VL-1**, **VL-1 Rhythm** and **Display**.

### Casio VL-1

The digits A to T follow the VL-1 sound code (sound, A, D, SL, ST, R, V, T). The defaults are the Piano code
`0 0 4 5 3 2 0 0`.

| Control | What it does | Range / default |
|---|---|---|
| Sound menu (header) | VL-1 sound. Electro 1-3 alternate octaves in time with the tempo. | Piano, Fantasy, Violin, Flute, Guitar 1, Guitar 2, Horn, Electro 1, Electro 2, Electro 3 / Piano |
| Octave | VL-1 octave. | -1, 0, +1 / 0 |
| A | Attack, like the digit in the VL-1 code. | 0-9 / 0 |
| D | Decay to the sustain level. | 0-9 / 4 |
| SL | Sustain level. | 0-9 / 5 |
| ST | How long the sustain lasts; 9 = forever. | 0-9 / 3 |
| R | Release after you let go. | 0-9 / 2 |
| V | Vibrato: 0 = off; 1 fast and subtle ... 9 slow and deep. | 0-9 / 0 |
| T | Tremolo: 0 = off; 1 fast ... 9 slow. | 0-9 / 0 |

### VL-1 Rhythm

| Control | What it does | Range / default |
|---|---|---|
| Edit Rhythms... (header) | Opens the rhythm editor for your own rhythms, User 1-8, with the three VL-1 sounds (see [User rhythms](../user-rhythms.md)). | - |
| Rhythm menu (header) | VL-1 rhythm box (Rock 1 is the one from "Da Da Da"). Off stops it. User 1-8 are your own rhythms (an empty slot plays nothing). | Off, March, Waltz, 4 Beat, Swing, Rock 1, Rock 2, Bossanova, Samba, Rhumba, Beguine, User 1-8 / Off |
| Tempo | VL-1 tempo; 0 = about 131 BPM. | -9 to +9 / 0 |
| Balance | Balance between melody and rhythm. | 0-100 % / 50 % |

### Display

The VL-1's 7-segment LCD, drawn after the figures of its manual (8 digits plus one position on the left):

- The **last three notes** you played, the last one on the right (manual p. 18). Each note is a number 1-7 (C to B),
  with a small sharp sign above it for a black key and an octave bar: below the number for the low octave (MIDI notes
  below 60), above it for the high octave (from MIDI 72), none for the middle octave (60-71).
- The **tempo**, as `[-2]`, for 2 s whenever you change it (manual p. 14).
- The mark on the far left is lit, and **blinks with the rhythm** while one is running.

The display only shows; it does not change the sound. Its colours do not follow the skin. Estimated, not shown in the
manual: a positive tempo is drawn without a sign (`[ 2]`).

### User rhythms

User 1-8 are MIDI files in the KoniSynth settings folder, `rhythms/Casio VL-1/User 1.mid` to `User 8.mid`: channel
10, notes 36 (Pock), 37 (Peek) and 42 (Noise), one sound per sixteenth (no triplet grid, no accent levels). On an empty
step the previous sound keeps ringing, as in the factory rhythms. They run on the VL-1's own clock, so Tempo applies.
Draw them with **Edit Rhythms...** or in any sequencer; KoniSynth reloads a changed file by itself. Details:
[User rhythms](../user-rhythms.md).

## Playing it

- **Monophonic, last note wins.** Releasing the last key returns to the previous key still held, without resetting the
  waveform phase; the envelope restarts from its attack.
- **Rhythm.** It starts as soon as a rhythm is selected (no key needed) and restarts from the first step when you change
  it. It runs on the VL-1's own clock: one tick = 5.5 ms + (9 - Tempo) x 0.98 ms, and one beat = 32 ticks. Calculated
  from that formula: Tempo -9 = 81 BPM, 0 = 131 BPM, +9 = 341 BPM. The engine does not read the host tempo.
- **Electro 1-3** are Piano, Fantasy and Violin with the pitch dropping one octave and coming back every 8 ticks
  (about 115 ms at Tempo 0, calculated), so the effect speeds up and slows down with Tempo.
- **Vibrato** runs at 15/V Hz and **tremolo** at 35/T Hz (full depth). Both restart with each note.
- **Pitch bend** works (smoothed with a 5 ms pole).

Recipe, "Da Da Da":
1. Load **Da Da Da (VL-1)** (Piano, code 00453200, Rhythm Rock 1, Tempo 0).
2. Play a simple riff in the middle of the keyboard; set Balance to taste.

Recipe, writing your own code:
1. Read the 8 digits of a VL-1 code: sound, A, D, SL, ST, R, V, T.
2. Pick the sound in the menu and enter the other seven digits on A, D, SL, ST, R, V and T.
   Example from the factory codes: Fantasy `1 0 0 9 9 6 3 0`, Violin `2 3 0 9 9 1 3 0`.

## Faithful details

- **Waveforms** as on the VL-1: Piano is an 11/16 pulse, Fantasy a square one octave up, Horn a 1/7 pulse, Guitar 1 and
  2 play one octave down (`VL1.h`, data from the emulator's `Wave.cpp`).
- **Stepped digital envelope.** 15 levels and a time unit of 12 ms (`VL1Defs.h`); the level only changes every 12 ms
  (`LineInTime.cpp`).
- **The attack always finishes**, even if you release the key during it (`ADSR.cpp`).
- **Phase resets only when the voice was silent** (`Voice.cpp`).
- **Output stage.** RC with 600 ohm charge / 33 kohm discharge and 3.3 uF, plus the measured quadratic curve
  (`EnvelopeShaper.cpp`); output response: 800 Hz low-pass and 65 Hz high-pass (`PluginVL1.cpp`).
- **Rhythm sounds** (`Rhythm.cpp`, `Noise.cpp`): Pock, a 780 Hz square of 30 periods falling one level every 2 periods;
  Peek, the same at 1560 Hz; Noise, a 16-bit LFSR at 50 kHz with 8 steps of 5 ms and 6 of 20 ms.
- **Levels.** Piano A4 sustained = 0.6 Vpp; Pock = 55/63 of the melody (ratio 55 mVpp / 63 mVpp cited in the emulator).

Differences from the emulator, stated in the code: waves 0/1 use PolyBLEP per edge (the emulator oversamples 16x with a
box average); the 65 Hz high-pass is applied to melody + rhythm (output coupling); the noise LFSR runs freely between
hits (the emulator repeats a fixed table; the hardware is not documented).

Optional "nicer" settings: none.

## Factory presets

| Preset | Category | Code / rhythm from the preset values |
|---|---|---|
| Da Da Da (VL-1) | Machines | Piano 00453200, Rock 1, Tempo 0 |
| VL-1 Fantasy | Machines | Fantasy, A0 D0 SL9 ST9 R6 V3 T0 |
| VL-1 Violin Bossa | Machines | Violin, A3 D0 SL9 ST9 R1 V3 T0, Bossanova, Tempo +2 |
| VL-1 Piano | Machines | Piano 00453200, no rhythm (loaded by the Engine menu) |
| VL-1 Flute | Machines | Flute, A3 D0 SL9 ST9 R1 V3 T0 |
| VL-1 Guitar | Machines | Guitar 1, A1 D0 SL7 ST1 R2 V0 T0 |
| VL-1 Manual ADSR | Machines | Electro 3, A0 D0 SL9 ST9 R9 V1 T4 (code 90099914, the example from the VL-1 manual per the code comment) |

## Limitations

- Monophonic.
- The rhythm tempo is the VL-1's own -9..+9 control; it does not follow the host.
- The noise generator's behaviour between hits is not documented for the hardware (see above).
