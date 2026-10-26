# Drum Machine

The Drum Machine engine synthesizes three analog rhythm machines from their circuit tunings: the Roland TR-808 (1980),
the Roland CR-78 (1978) and the Korg Mini Pops 7 (1966). Nothing is sampled: frequencies and decay times come from the
tuning tables of the TR-808 and CR-78 service notes, and the noise filters and envelopes were calculated from the
schematics. You can play the kit from the keyboard (General MIDI drum map), run one of 29 built-in patterns or one of
your own 8 user rhythms, or both at once. Use it for classic analog drum parts, or for the combined-rhythm trick of the Mini Pops 7.

As with every emulated engine in KoniSynth, the defaults reproduce the original behaviour, quirks included. Anything that
makes it "nicer" than the hardware is an option and is off by default.

## What you need

Nothing extra.

## Controls

Select **Drum Machine** in the Engine menu. Choosing it from that menu loads the preset **808 Kit**. The engine shows
two sections side by side.

### Drum Kit

| Control | What it does | Range / default |
|---|---|---|
| Kit menu (header) | Drum kit, by year: Mini Pops 7 (1966), CR-78 (1978) or TR-808 (1980). Keys follow the General MIDI drum map. | Mini Pops 7, CR-78, TR-808 / TR-808 |
| Velocity (switch) | Off: a MIDI note plays at the normal level, or accented from velocity 100. On: velocity sets the level continuously. | Off / On / **Off** |
| Tune | Tunes all drums together. | -12 to +12 st / 0 st |
| Decay | Scales all decay times; x1 is the factory decay of each voice. | x0.25 to x4 / x1.00 |
| Tone | Brightness: kick click, snare tone and noise filters. | 0-100 % / 50 % |
| Snappy | Amount of snare wires (noise) in the snare. | 0-100 % / 50 % |
| Accent | Level of accented steps and of notes played at full velocity. | 0-100 % / 50 % |

### Pattern

| Control | What it does | Range / default |
|---|---|---|
| Edit... (header) | Opens the rhythm editor for your own rhythms, User 1-8 (see [User rhythms](../user-rhythms.md)). | - |
| Pattern menu (header) | Pattern played by the machine. Off: play the kit from the keys only. Factory patterns listed alphabetically, then User 1-8 (an empty user slot plays nothing). | Off + 29 patterns + User 1-8 / Off |
| + Pattern | A second pattern played at the same time, like holding two rhythm buttons on the Mini Pops 7. | Off + 29 patterns + User 1-8 / Off |
| Tempo | Pattern tempo. Follows the host tempo and position when the host is playing. | 60-240 BPM / 120 BPM |
| Swing | Delays every second 16th note. 0 = straight; 33 % = triplet feel. | 0-50 % / 0 % |

Patterns: Rock 1, Rock 2, Rock 4, Disco, Funk, Electro, Hip Hop, Synth Pop, Bossa Nova, Samba, Cha-Cha, Mambo, Beguine,
Tango, Reggae, March, March 6/8, Foxtrot, Waltz, Jazz Waltz, Shuffle, Slow Rock, Swing, Boogie, Rumba, Merengue,
Habanera, Latin Swing, Hardbass.

Above the knobs, a row of 16 red step lights works like the LEDs of the TR-808 STEP buttons (TR-808 Owner's Manual,
p. 11): while a pattern runs, the light moves across the steps of the measure from left to right. A 12-step measure
uses lights 1-12; with no pattern running, all are off. The lights do not change colour with the skin.

**User rhythms.** User 1-8 are MIDI files (channel 10, General MIDI drum notes, three levels: soft, normal, accent) in
the KoniSynth settings folder, `rhythms/Drum Machine/User 1.mid` to `User 8.mid`. Draw them with **Edit...** (15
instrument lanes, 12, 16 or 32 steps; lanes the selected kit does not have are dimmed) or in any sequencer: KoniSynth
reloads a changed file by itself. Presets store only the slot number. Folder locations, the editor and the file format:
[User rhythms](../user-rhythms.md).

## Playing it

### Keyboard map (C4 = MIDI 60)

| Note | TR-808 | CR-78 | Mini Pops 7 |
|---|---|---|---|
| B1, C2 (35, 36) | Bass drum | Bass drum | Bass drum |
| C#2 (37) | Rim shot | Rim shot | Rim shot |
| D2 (38) | Snare | Snare | Snare 1 |
| D#2 (39) | Hand clap | - | Snare 2 |
| E2 (40) | Snare | Snare | Snare 2 |
| F2, G2, A2 (41, 43, 45) | Low tom | Low conga | - |
| F#2, G#2 (42, 44) | Closed hat | Hi-hat | Hi-hat |
| A#2 (46) | Open hat | Hi-hat | Cymbal 1 |
| B2, C3 (47, 48) | Mid tom | - | - |
| D3 (50) | High tom | - | - |
| C#3, D#3, E3, G3, A3, B3 (49, 51, 52, 55, 57, 59) | Cymbal | Cymbal | Cymbal 2 |
| F#3 (54) | - | Tambourine | Tambourine |
| G#3 (56) | Cowbell | Cowbell | Cowbell |
| A#3 (58) | - | - | Quijada (jawbone) |
| C4 (60) | - | High bongo | Bongo (600 Hz) |
| C#4 (61) | - | Low bongo | Bongo (400 Hz) |
| D4 (62) | High conga | High bongo | Conga |
| D#4 (63) | Mid conga | Low bongo | Conga |
| E4 (64) | Low conga | Low conga | Conga |
| A#4 (70) | Maracas | Maracas | - |
| C#5 (73) | - | Guiro (short) | Guiro |
| D5 (74) | - | Guiro (long) | Guiro |
| D#5 (75) | Claves | Claves | Claves |
| A5 (81) | - | Metal beat | - |

Notes outside this map are ignored.

### Velocity and accent

- **Velocity off (default, as the machines):** velocity 1-99 plays at the normal level (0.75, the same as a normal
  pattern step); velocity 100-127 plays accented. The TR-808 has a single global accent level.
- **Velocity on:** the level follows velocity continuously; velocity 127 plays at the accent level.
- Accent level = 0.75 + 0.5 x Accent, so with Accent at 50 % an accent is level 1.0.

### Running a pattern

1. Pick a kit in the Drum Kit menu.
2. Pick a pattern in the Pattern menu. It starts at once with its own clock at **Tempo**.
3. In a DAW, start the host transport: the pattern follows the host tempo and position.
4. Play the keyboard on top; pattern hits and keys share the same voices.

A pattern only plays the instruments the selected kit has: for example, the TR-808 has no tambourine or guiro, and the
Mini Pops 7 plays the clap lane on Snare 2 and the maracas lane on the quijada. Swing applies only to patterns with 4
steps per beat; triplet patterns ignore it.

### Combining two patterns (Mini Pops 7 trick)

The step counter has 16 positions per bar. Waltz patterns (12 steps, 4 per beat) skip positions 4-5 and 8-9; triplet
patterns (12 steps, 3 per beat) skip one position in every 4 and run at 3 steps per beat. With **+ Pattern** set, the
hits of both patterns add up (like the diode matrix of the original) and their skips add up too. Result: Slow Rock +
Rock is heard as the Rock pattern walked through in jumps, the effect behind the Oxygene rhythms.

Recipe: Kit = Mini Pops 7, Pattern = Slow Rock, + Pattern = Rock 2, Tempo = 96 BPM (this is the factory preset
"Oxygene IV Rhythm").

## Faithful details

- **Closed hat chokes open hat** on the TR-808 (transistor Q23 in the original).
- **TR-808 bass drum:** 56 Hz with a click of about 6 ms in which the pitch rises more than an octave, and a slow
  "pitch sigh" (Werner et al., DAFx-14).
- **TR-808 cymbal and hats:** six square oscillators at 205.3 / 304.4 / 369.6 / 522.7 / 540 / 800 Hz, band-pass at
  3440 and 7100 Hz and high-pass at 10.5 kHz (Werner, Abel and Smith, ICMC-SMC 2014). Tune moves the six together.
- **Filters calculated from the service-note schematics** (nodal analysis with the real R and C values): snare
  high-pass 2750 Hz Q 0.73, clap band-pass 1271 Hz Q 1.48 with four "teeth" at 0, 11, 22 and 30 ms plus a reverb tail,
  maracas, rim shot, cowbell and hat filters. Validated against the published cymbal band-passes (1-1.3 % error).
- **Decay times** from the TR-808 (p. 14) and CR-78 (p. 15) service-note tuning tables; some were checked against
  recordings of real units.
- **TR-808 toms and congas:** at high amplitude the pitch rises and returns, as the diodes do (curve estimated).
- **CR-78:** hats, cymbal and maracas through a 9.10 kHz LC tank; guiro is a train of clicks at 125 / 77 Hz on a
  6.13 kHz tank, with no noise (VG-11A schematic).
- **Mini Pops 7:** LC resonators and filtered noise, no integrated circuits; resonator frequencies read from the
  "MP-7 over-all circuit diagram" (Tubbutec). Bass drum, snare 1 and cymbal 1 decays measured on a Mini Pops 45
  (sister circuit).
- **No continuous velocity by default:** see Velocity above.

Optional setting that goes beyond the original: **Velocity** (continuous velocity), off by default.

## Factory presets

| Preset | Category |
|---|---|
| 808 Kit | Drums |
| 808 Electro | Drums |
| 808 Long Boom | Drums |
| 808 Synth Pop | Drums |
| 808 Funk | Drums |
| 808 Dub Reggae | Drums |
| Hardbass Kit | Drums |
| CR-78 Rock | Drums |
| CR-78 Bossa | Drums |
| CR-78 Shuffle | Drums |
| CR-78 Disco | Drums |
| CR-78 Cha-Cha + Rumba | Drums |
| Oxygene IV Rhythm | Drums |
| Oxygene VI Rhythm | Drums |
| Children 808 Kick | Drums |
| Children 808 Open Hat | Drums |

## Limitations

- The patterns are KoniSynth's own, written in the conventions of each style. They are **not** transcriptions of the
  Roland or Korg ROMs.
- Estimated, not published: the pitch curves of the toms and the bass drum, the relative level of the voices, and the
  Q of the CR-78 tanks.
- Mini Pops 7: decays are estimated (except the three measured voices above); the 860 / 4600 Hz cowbell assignment is
  not verified; guiro and quijada pulse rates and durations are estimated.
- When two patterns are combined, adding their skips together is an assumption (not published).
- The Oxygene presets use estimated tempos.
