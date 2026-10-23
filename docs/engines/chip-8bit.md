# 8-Bit (NES / Game Boy)

The 8-Bit engine emulates the sound chips of the Nintendo Entertainment System / Famicom (Ricoh 2A03 NTSC and 2A07 PAL)
and of the Game Boy (DMG) and Game Boy Color (CGB), cycle by cycle at their own clocks, plus the Konami VRC6 expansion of
the Famicom. On top of the chip there is a "driver" layer running at the video frame rate, as in game music: arpeggio,
vibrato, release and per-frame macros are applied once per frame and rounded to the chip's integer registers. Use it
for chiptune leads, basses, drums and game sound effects.

As with every emulated engine in KoniSynth, the defaults reproduce the original hardware, quirks included.

## What you need

Nothing extra. Optional: for **DPCM = User Sample**, a sound file loaded in the Sample panel (the **Edit...** button of
the Sample tab, in the Synth engine's sources). It is converted to 1-bit DPCM.

## Controls

Select **8-Bit (NES / Game Boy)** in the Engine menu. Choosing it loads the representative preset of the current chip
model. The engine shows four sections in one row.

### Chip

| Control | What it does | Range / default |
|---|---|---|
| Model menu (header) | Sound chip. Choosing one loads its representative preset (see Factory presets). Each item has a tooltip with its clock and filters. | NES NTSC (2A03), NES PAL (2A07), Famicom (2A03), Game Boy (DMG), GB Color (CGB), PAL Unadapted / NES NTSC |
| Filter (switch) | Console output filter: NES high-pass 90 and 440 Hz + low-pass 14 kHz; Famicom 37 Hz; Game Boy its output capacitor. Off: raw chip. | Off / On / **On** |
| Voice | Which chip channel your keys play (see Playing it). | Pulse, Triangle / Wave, Noise, Poly, MIDI Channels, DPCM (NES), VRC6 Pulse, VRC6 Saw, Poly + VRC6 / Pulse |
| Duty | Pulse width of the pulse channels. 25 % and 75 % sound the same. | 12.5, 25, 50, 75 % / 50 % |
| Volume | Channel volume, 4 bits like the hardware. | 0-15 / 12 |
| Env | Constant: fixed volume. Decay: the hardware envelope falls 15 to 0. Loop / Rise: NES repeats the decay; Game Boy rises instead. | Constant, Decay, Loop / Rise / Constant |
| Rate | Hardware envelope period: NES 0-15 (about 240 Hz / (rate+1) per step), Game Boy 1-7 (64 Hz / rate). | 0-15 / 4 |
| Release | Release after the key is let go, in video frames. 0 = cut at once, like most game music. | 0-60 / 0 |
| Sweep | Hardware pitch sweep shift; negative = upwards; 0 = off. | -7..+7 / 0 |
| S.Rate | Hardware sweep period (1 fast ... 7 slow; 0 = off). | 0-7 / 0 |
| Noise | Noise channel: Long (hiss) or Short (metallic, pitched). Keys choose the noise pitch. | Long, Short / Long |
| Wave | Game Boy wave table (32 samples of 4 bits). Boot RAM = what the chip holds at power-on (DMG and CGB differ). | Triangle, Saw, Sine, Square, Pulse 25 %, Organ, Boot RAM / Triangle |
| W.Level | Game Boy wave channel volume. | 100, 50, 25 % / 100 % |
| DPCM | NES DPCM sample: Kit plays drums from the drum keys; the others play one sample with the key choosing the playback rate. | Kit (keys), Kick, Snare, Tom, Timpani, Slap Bass, User Sample / Kit |

### Chip Driver

| Control | What it does | Range / default |
|---|---|---|
| Arp menu (header) | Hold a chord on one channel and it plays as a fast arpeggio. | Off, Up, Down / Off |
| Velocity (switch) | Velocity sets the 4-bit volume (as many drivers do). Off: every note at Volume. | Off / On / **On** |
| Arp Spd | Arpeggio speed in frames per note (1 = 60 notes per second). | 1-8 / 1 |
| Vib | Vibrato depth in cents, applied once per frame and rounded to the period register. | 0-100 ct / 0 |
| Vib Rate | Vibrato speed. | 0.5-15 Hz / 6 Hz |
| Vib Delay | Frames before the vibrato starts. | 0-60 / 0 |
| DPCM Loop | Loop the DPCM sample while the key is held (hardware loop flag). | Off / On / Off |

Header button **Frame Macros...** opens the macro editor (see Playing it).

### Famicom Expansion

| Control | What it does | Range / default |
|---|---|---|
| Expansion menu (header) | VRC6 (Konami, Akumajou Densetsu): 2 extra pulses and a sawtooth, mixed linearly with the 2A03. Famicom model only. | None, VRC6 (Famicom) / None |
| Overdrive (switch) | Lets the volume push the saw accumulator rate above 42, where the 8-bit accumulator overflows and the saw breaks into a harsher wave. | Off / On / Off |
| Duty | VRC6 pulse duty (the 2A03 only has 1/8, 1/4, 1/2, 3/4). | 1/16 to 8/16 / 8/16 |

### GB Stereo

| Control | What it does | Range / default |
|---|---|---|
| P1, P2, Wave, Noise | Game Boy NR51 routing of Pulse 1, Pulse 2, Wave and Noise. No effect on NES / Famicom (mono). | Center, Left, Right, Off / Center |
| Output | Headphones: stereo (NR51). Speaker: the built-in mono speaker (left + right; its frequency response is not modelled). | Headphones (stereo), Speaker (mono) / Headphones |

## Playing it

### Voice modes

| Voice | Keys play |
|---|---|
| Pulse | Pulse 1 (the one with the hardware sweep). |
| Triangle / Wave | NES triangle, or Game Boy wave channel. |
| Noise | Noise channel; the key picks the noise period. |
| Poly | Pulse 1, pulse 2 and triangle/wave: 3 notes; a free channel first, then the next one in turn. |
| MIDI Channels | MIDI ch 1-4 = pulse 1, pulse 2, triangle/wave, noise; ch 5 = DPCM; ch 6-8 = VRC6 (with the expansion on). |
| DPCM (NES) | The NES sample channel (NES and Famicom models only). |
| VRC6 Pulse / VRC6 Saw | A VRC6 channel. Silent unless Model = Famicom and Expansion = VRC6. |
| Poly + VRC6 | 6 voices: free 2A03 channels first, then the VRC6 channels. |

On a single channel, holding several keys returns to the last held note without a new attack when you release one.
With **Arp** on, a held chord on that channel is played as an arpeggio at **Arp Spd** frames per note.

### DPCM kit (DPCM = Kit)

| Keys (C4 = MIDI 60) | Sample |
|---|---|
| B1, C2 (35, 36) | Kick |
| D2, E2 (38, 40) | Snare |
| F2, G2 / A2, B2 / C3, D3 | Tom at three rates |
| E3, F3 (52, 53) | Timpani |
| G3, A3 (55, 57) | Slap bass |

With a single sample selected, C4 plays at rate 15 (the fastest) and each semitone below is one rate slower, down to
rate 0. The factory samples are synthesized by KoniSynth and encoded to DPCM (game samples are copyrighted).

### Frame Macros...

The editor has four rows: **Volume**, **Arpeggio**, **Pitch** and **Duty / Noise**. Each row takes values separated by
spaces, one per frame (60.10 Hz on NES NTSC and Famicom, 50.01 Hz on PAL, 59.73 Hz on Game Boy), and shows a bar graph.

- `|` marks the loop start; `/` marks the release point (the macro waits there while the key is held).
- Volume 0-15 (scaled by channel volume and velocity). Arpeggio in semitones (range +/-96), or `@n` for an absolute
  note from C-0 (`@48` = C4). Pitch: change of the period register per frame, accumulated (+ = lower; range +/-127).
  Duty 0-3 (12.5 / 25 / 50 / 75 %); on the noise channel 0 = long, 1 = short.
- An empty row is not used. Text with errors is not applied and the error is shown. **Clear** removes all macros.

Recipe (coin pickup, as in the "NES Coin Pickup" preset): Volume `15 15 15 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0`,
Arpeggio `0 0 0 0 5`, Duty `1`.

## Faithful details

- **NES** (nesdev wiki): CPU 1,789,773 Hz (NTSC) / 1,662,607 Hz (PAL); writing the high period byte restarts the
  pulse sequence (the click of each note); silence when the period is below 8 or the sweep target exceeds $7FF; the
  triangle freezes in place when stopped; non-linear mixer; the DPCM level stays where the sample ends and lowers the
  triangle and noise volume, as on the hardware.
- **Driver clicks:** the driver rewrites the high period byte only when it changes, so vibrato crossing a multiple of
  256 clicks.
- **PAL Unadapted:** an NTSC game on a PAL NES: everything sounds 127.6 cents lower and the 50.01 Hz driver runs about
  17 % slower.
- **Game Boy** (Pan Docs, gbdev, SameBoy): pulses do not reset phase on trigger; wave channel 3 starts 6 cycles late and
  first plays the old buffered sample; inverted linear DAC.
- **DMG wave RAM corruption:** retriggering channel 3 just before it reads a byte corrupts the table (SameBoy and
  Gambatte). DMG only.
- **Boot RAM** wave: the DMG power-on contents (from gbdev); on CGB, 00 FF repeated.
- **VRC6** (nesdev, checked against Mesen2): 12-bit pulse periods (down to 27.3 Hz), duty (D+1)/16, 8-bit saw
  accumulator; mixed outside the 2A03's non-linear mixer at 0 dB.

Optional settings that change the original behaviour: **Filter** off (raw chip, no console output filter) and
**Velocity** off. The VRC6 overflow quirk is opt-in through **Overdrive**.

## Factory presets

All in category **8-Bit**. The Model menu loads the preset in brackets.

NES Square Lead (NES NTSC), NES PAL Square Lead (NES PAL), NES PAL Unadapted Lead (PAL Unadapted), Famicom Poly
(Famicom), Game Boy Square (Game Boy DMG), Game Boy Color Lead (GB Color), NES Thin Pulse, NES Triangle Bass, NES Arp
Chords, NES Decay Pluck, NES Laser Sweep, NES Noise Kit, NES Noise Hi-Hat, NES Band (MIDI 1-4), NES DPCM Kit, NES DPCM
Slap Bass, Game Boy Wave Bass, Game Boy Boot Wave, Famicom VRC6 Saw Lead, Famicom VRC6 Poly, and the frame-macro effects
NES Coin Pickup, NES Jump, NES Explosion, NES Echo Lead, Game Boy Laser.

## Limitations

- The NES / Famicom are mono: GB Stereo has no effect there.
- The Game Boy speaker's frequency response is not modelled.
- VRC6 works only with Model = Famicom. Other expansion chips (VRC7, N163, FDS) are not emulated.
- The NES output filters follow nesdev (blargg's measurements); NSFPlay and Mesen2 use different values or none: the
  code documents this contradiction.
- The Game Boy output capacitor uses the same value (0.999958 per cycle, about 28 Hz) on DMG and CGB, as SameBoy does.
  The stronger CGB value shown in the Model tooltip (0.998943) has no measurements and is not used.
- VRC6 relative level: published measurements vary from -1.1 to +0.2 dB by cartridge and console; 0 dB is used.
