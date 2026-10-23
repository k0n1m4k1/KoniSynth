# ANS

The ANS engine emulates the ANS photoelectronic synthesizer of Evgeny Murzin (1958; 1964 model). The original
generated 720 sine tones, 72 per octave (16.67 cents apart) over 10 octaves, from five rotating glass discs. The score
was a glass plate covered with black mastic that the composer scratched away: vertical position = pitch (1 mm per
tone), horizontal = time, width of the scratch = volume. The plate moved at 0 to 24 mm/s past 20 photocells, each with
a half-octave filter, which acted as a 20-band equalizer. Use it for microtonal clusters, glissandi, drones and
spectral textures.

As with every emulated engine in KoniSynth, the defaults follow the original, including the slow irregularity of the
optical tracks (Emulsion is on by default).

## What you need

Nothing extra.

## Controls

Select **ANS** in the Engine menu. Choosing it from that menu loads the preset **ANS Cluster**.

### ANS

| Control | What it does | Range / default |
|---|---|---|
| Score menu (header) | The glass plate: a factory score, or User (draw your own with Edit...). | Cluster, Glissandi, Drone, Bells, Noise Bands, 72-Step Scale, User / Cluster |
| Hold (switch) | On: the plate keeps playing without a key. Off: it starts when you press a key. | Off / On / Off |
| Speed | Plate speed in mm per second (up to 24, as on the ANS). 1 mm is one column of the score. | 0-24 mm/s / 12 mm/s |
| Emulsion | Slow irregularity of each optical track, which made the ANS sound alive (amount not documented: estimate). | 0-100 % / 20 % |

Header button **Edit...** opens the ANS plate editor.

### ANS plate editor (Edit...)

- The header repeats Score, Hold, Speed and Emulsion.
- The plate: 720 tones (vertical) x 512 mm (horizontal). Horizontal lines mark each octave (every 72 tones). A red
  vertical line is the reading cursor.
- **Left drag:** scratch (sound). **Right drag:** fill (silence). The brush is soft-edged: repeated strokes over the
  same spot make it louder, up to the maximum.
- Below the plate, the 20 **registers** (knobs 1-20).

| Control | What it does | Range / default |
|---|---|---|
| 1 ... 20 | ANS register n: gain of half-octave band n of 20 (the original had 20 photocells, each with its own half-octave filter). | 0-100 % / 100 % |

## Playing it

- **Tuning:** tone k sounds at 21.09375 x 2^(k/72) Hz. Tone 0 = 21.09 Hz; one octave = 72 rows.
- **Transposition:** the key transposes the whole score in steps of 1/72 octave, 6 steps per semitone. C4 (MIDI 60)
  = no transposition. With several keys held, the last one sets the transposition. This applies with Hold on or off.
- **Hold off:** pressing the first key starts the plate from the beginning (column 0); releasing all keys stops it.
- **Hold on:** the plate runs continuously, with or without keys.
- The plate is 512 mm long (21.3 s at 24 mm/s) and loops at the end.
- Pitch bend shifts all tones together.

### Drawing your own score

1. Click **Edit...**.
2. Left-drag on the plate to scratch tones. A horizontal stroke is a sustained tone; a slanted stroke is a glissando; a
   wide vertical band of tones sounds like noise.
3. Right-drag to erase.
4. When you release the mouse, the drawing becomes the **User** score and the Score menu switches to User. If you
   started from a factory score, it is copied into User first, so the factory score is not changed.
5. Turn on **Hold**, or press C4, to hear it. Shape the overall spectrum with registers 1-20.

### Factory scores

| Score | Content |
|---|---|
| Cluster | Microtonal cluster (Solaris style): 9 tones 1/72 octave apart around A 220 Hz, entering and leaving. |
| Glissandi | Crossed glissandi. |
| Drone | A drone with harmonics that turn on in turn. |
| Bells | Short strikes of inharmonic partials that decay. |
| Noise Bands | Wide vertical bands (all tracks at once = white noise). |
| 72-Step Scale | The 72-tone-per-octave scale from A 220 to A 440 Hz, step by step. |

## Faithful details

- 720 sine tones, 72 per octave, 10 octaves; 20 half-octave registers (from at least two sources each: the 1964 VDNKh
  leaflet on theremin.ru, Wikipedia, 120years.net, an interview with S. Kreichi in InRock, habr).
- Plate speed 0-24 mm/s (leaflet; a single source).
- Lowest tone 21.09375 Hz, calculated from the disc speeds published by journalpmn.ru (the slow disc turns at 6/64 rps
  with 225 cycles per turn on its inner track); a single source, consistent with the "21-21600 Hz" given by habr.
- Fastest attack of the ANS: 1 ms (leaflet); each tone's amplitude follows the plate with that time constant.
- **Emulsion:** a slow random amplitude fluctuation per track (estimated).

There is no "beautify" switch in this engine. Setting **Emulsion** to 0 % removes the track irregularity.

## Factory presets

| Preset | Category |
|---|---|
| ANS Cluster | Machines |
| ANS Glissandi | Machines |
| ANS Drone | Machines |
| ANS Bells | Machines |
| ANS Noise Bands | Machines |
| ANS 72-Step Scale | Machines |

The factory scores are KoniSynth's own compositions, inspired by known uses of the ANS (Artemiev, Coil).

## Limitations

- Not published, so chosen or omitted: the law from scratch width to amplitude (linear here), the amount of emulsion
  irregularity (estimated), and detuning between discs (not added).
- Tones above the Nyquist limit are silenced (in the code, above 0.45 x the sample rate), so the top tones of the
  10 octaves may not sound at low sample rates.
