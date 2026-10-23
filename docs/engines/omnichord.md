# Omnichord

The Omnichord engine emulates the Suzuki Omnichord OM-84 (1984): chord buttons, a 13-zone strum plate ("Sonic
Strings"), a held chord voice, an auto bass and a rhythm section. There is no public service manual or schematic; the
figures marked [EMU] in the code come from the Chordian emulator (github.com/Jan125/pb.chordian), a third-party model,
not hardware measurements. Use it for strummed harp-like arpeggios over chords, and for the classic rhythm + bass
accompaniment.

As with every emulated engine in KoniSynth, the defaults reproduce the original behaviour, quirks included. Anything
that makes it "nicer" than the hardware is an option and is off by default; this engine has no such options.

## What you need

Nothing extra.

## Controls

Select **Omnichord** in the Engine menu. Choosing it from that menu loads the preset **Omnichord Strum**. The engine
shows two sections: **Omnichord** and **Omnichord Rhythm**.

### Omnichord

| Control | What it does | Range / default |
|---|---|---|
| Split | Keys below this note are chord buttons; from this note up, the 13 strum zones. | C2-C6 (MIDI 36-84) / C4 (60) |
| Sustain | How long each plucked string of the strum plate rings. | 0-100 % (about 0.4 to 3 s) / 50 % |
| Oct | Octave of the strum plate; its 13 zones span 4 octaves from the chord. | -2 to +1 / 0 |
| Harp | Volume of the strum plate (Sonic Strings). | 0-100 % / 80 % |
| Chord | Volume of the held chord voice, heard while a chord key is down. | 0-100 % / 50 % |
| Bass | Volume of the auto bass (root, fifth and third follow the rhythm). | 0-100 % / 60 % |

### Omnichord Rhythm

| Control | What it does | Range / default |
|---|---|---|
| Rhythm menu (header) | Rhythm with auto bass. The bass plays only while a chord key is held. Patterns are KoniSynth's own, in each style. | Off, Rock 1, Rock 2, Slow Rock, Swing, Waltz, Latin, Foxtrot, Country, Disco, March, Blues, Tango / Off |
| Tempo | Rhythm tempo; follows the host tempo when the host is playing. | 60-240 BPM / 100 BPM |
| Level | Volume of the drums. | 0-100 % / 70 % |

## Playing it

**Chord buttons (below Split).** The chord is recognised from the keys you hold: the lowest key is the root, and the
type comes from the other intervals above it (any octave):

| Keys held (example on C) | Chord |
|---|---|
| C | major |
| C + Eb | minor |
| C + Bb | 7 (played without the fifth: root, 3rd, b7) |
| C + Eb + Bb | m7 |
| C + B | maj7 |
| C + Eb + F# | dim (voiced 0, 3, 9 as on the OM-84 [EMU]) |
| C + E + G# (no G) | aug |

The chord is voiced as 3 notes folded into MIDI 54-65 [EMU]. A new chord, or a chord pressed after silence, restarts
the chord voice; releasing one key while others are held re-reads the chord without restarting it.

**Strum plate (from Split up).** The 13 zones play the notes of the current chord, rising through 4 octaves
(note = chord note + 12 x (zone / 3) + 12 x Oct). Key velocity sets the pluck level. Keys more than 12 semitones above
Split wrap around to zone 0 again. The **mod wheel** also strums: each zone it crosses sounds once.

**Bass and rhythm.** With a rhythm running, the bass follows the pattern (root, root an octave down, fifth or third)
only while a chord key is held. With Rhythm = Off, the bass sounds once when you press a chord. The drums use the CR-78
voices of KoniSynth's drum machine. **Pitch bend** bends harp, chord and bass.

Recipe, strum over chords:
1. Load **Omnichord Strum** (long Sustain 90 %, quiet chord voice, plate reverb).
2. Hold a chord key below C4 with the left hand.
3. Sweep the mod wheel up and down, or play keys from C4 up, to strum.

Recipe, accompaniment:
1. Load **Omnichord Rock 1** (Rock 1, 84 BPM).
2. Hold chord keys in turn: the drums run continuously and the bass follows each chord while a key is down.

## Faithful details

- **Chord voicing and harp layout** [EMU]: 3-note voicing in MIDI 54-65; 7th chords without the fifth (0, 4, 10);
  diminished as 0, 3, 9; harp = chord notes + 12·(n/3), 13 zones = 4 octaves.
- **Harp decay** [EMU]: exponential with τ = 0.366 + 2.734·Sustain s; no release (Sustain alone sets the length).
- **Bass** [EMU]: root in MIDI 42-53, linear fall over 1.22 s.
- **Chord voice** [EMU]: decays with τ 0.333 s while held (down to a floor) and releases in 0.18 s.
- **13 sensors and a 4-octave range**: Wikipedia and forum.midibox.org.
- **Timbre**: divider square waves (an 8253 timer according to a forum, not verified); "high notes sound almost
  triangular", hence a fixed low-pass. Cutoffs: harp 1500 Hz (estimated, 2 poles), chord voice 2000 Hz (fitted to two
  recordings of a major and a minor chord, Freesound 723983 / 723985), bass 350 Hz (estimated).
- **Rhythm names** are those of the OM-84 (Acadia Recording and Wikipedia); the patterns themselves are KoniSynth's own,
  because the Omnichord ROM patterns have not been found.

Optional "nicer" settings: none.

## Factory presets

| Preset | Category | Notes from the preset values |
|---|---|---|
| Omnichord Rock 1 | Machines | Rock 1, 84 BPM, Sustain 60 % |
| Omnichord Strum | Machines | No rhythm, Sustain 90 %, Chord 20 %, plate reverb (loaded by the Engine menu) |
| Omnichord Latin | Machines | Latin, 112 BPM |
| Omnichord Disco | Machines | Disco |
| Omnichord Country | Machines | Country |
| Omnichord Deep Jukebox | Machines | Oct -1, Sustain 100 %, large plate reverb |

## Limitations

- No service manual or schematic is public: the [EMU] figures come from a third-party emulator, and the filter cutoffs
  are estimated or fitted to two recordings.
- The rhythm patterns are not transcriptions of the OM-84 ROM.
- The strum plate has 13 zones; higher keys wrap around.
