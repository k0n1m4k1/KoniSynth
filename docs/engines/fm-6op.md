# FM 6-Op

FM 6-Op is a six-operator FM engine modelled on the Yamaha DX7, with the DX7's 32 algorithms and its parameter ranges
(0-99 levels and rates, detune -7..+7, keyboard level scaling curves). In the default **DX7** mode it reproduces the
original chip arithmetic: log-sine and exponential ROMs, 12-bit phase, the 49,096 Hz internal rate and the
floating-point 12-bit DAC. It loads DX7 SysEx voices and banks. Use it for electric pianos, bells, FM basses, brass and
organs, or to play your own DX7 patches.

As with every emulated engine in KoniSynth, the default reproduces the original behaviour, grit included. The smooth
**Clean** mode is optional.

## What you need

Nothing extra. Optional: Yamaha DX7 SysEx files (`.syx`) if you want to load original voices or banks.

## Controls

Select **FM 6-Op** in the Engine menu. Choosing it from that menu loads the preset **FM E.Piano**. The engine shows three
sections side by side: **FM 6-Op**, **FM LFO** and **Display**.

### FM 6-Op

| Control | What it does | Range / default |
|---|---|---|
| Mode | DX7: the original chip arithmetic, its 49 kHz rate and floating-point 12-bit DAC. Clean: smooth floating-point FM. | DX7, Clean / DX7 |
| Algorithm | How the 6 operators modulate each other (as on the DX7). Carriers are heard; modulators shape their tone. | 1-32 / 1 |
| Feedback | Feedback of the operator marked fb in the algorithm: from sine towards saw and then noise. | 0-7 / 0 |
| OP1 ... OP6 | Output level of each operator. On a carrier it is volume; on a modulator, brightness (99 = about 12.6 rad of modulation). | 0-99 / OP1 99, others 0 |

Header buttons:

- **Edit...** opens the FM editor (see below).
- **Load .syx...** loads a DX7 voice or a 32-voice bank into the 6 operators.

### FM LFO

| Control | What it does | Range / default |
|---|---|---|
| Wave menu (header) | LFO waveform. | Triangle, Saw Down, Saw Up, Square, Sine, S&H / Triangle |
| Sync (switch) | LFO key sync: restarts the LFO cycle when you play a note after all keys were released. | Off / On / **On** (DX7 INIT VOICE) |
| Speed | LFO speed, about 0.06 to 49 Hz (from the DX7 table). | 0-99 / 35 |
| Delay | Time before the LFO fades in after you play (0 = at once). | 0-99 / 0 |
| PMD | Pitch modulation depth (vibrato), scaled by PMS. | 0-99 / 0 |
| AMD | Amplitude modulation depth (tremolo / wah), for operators with AMS above 0. | 0-99 / 0 |
| PMS | Pitch modulation sensitivity; with depth 99, 7 is about one octave. | 0-7 / 3 |

### Display

The DX7's front panel display, after the figures of its operating manual: a 16-character x 2-line LCD without
backlight and, to its left, the red 2-digit LED with the voice number.

| What you do | What it shows |
|---|---|
| Load a voice from a .syx bank | `CARTRIDGE VOICE` / `CRT 5 BRASSHORNS`; the LED shows the voice's position in the bank (none for a single voice) |
| Load a KoniSynth preset | `INTERNAL VOICE` / `INT` and the preset name; the LED is off |
| Change any voice parameter | A dot after the LED number: the voice has been edited |
| Move an FM control | For 2 s, the DX7 EDIT mode: line 1 `ALG1 111111 OP2` (algorithm, operators, the operator edited), line 2 the parameter and its value (`OUTPUT LEVEL =69`) |

The labels come from the manual's figures; those it does not show (feedback, detune, fixed mode, oscillator key sync,
transpose, pitch envelope and most LFO waveform names) are estimated. The voice loaded from a .syx is saved with the
project and with user presets. The display only shows; its colours do not follow the skin.

### FM editor (Edit...)

The header repeats **Algorithm** and **Feedback** and shows the algorithm as text, next to two blocks for the whole
voice:

| Block | Control | What it does | Range / default |
|---|---|---|---|
| Voice | Osc Sync | Oscillator key sync. On: every note starts the operators from the same point of the wave. Off: they keep running freely, so each attack sounds slightly different (many DX7 pianos and guitars). | Off / On / **On** |
| | Transpose | Transposes the whole voice; the transposed note also drives keyboard level and rate scaling. | -24 to +24 st / 0 |
| Pitch EG | R1 ... R4 | Pitch envelope rates: 1 when you play, 2 and 3 towards the sustain (L3), 4 after you release the key. | 0-99 / 99 |
| | L1 ... L4 | Pitch envelope levels; 50 = no change, 0 / 99 = about -4 / +4 octaves. A note starts at L4 and returns to it after release. | 0-99 / 50 |

The pitch envelope affects only operators in ratio mode (Fixed operators keep their frequency), as on the DX7.

Below are six columns, OP1 to OP6; the columns of the carriers (the operators you hear with the current algorithm) are
highlighted.

| Control | What it does | Range / default |
|---|---|---|
| Coarse | Frequency ratio (0 = 0.5, 1, 2 ... 31), or in Fixed mode 1 / 10 / 100 / 1000 Hz. | 0-31 / 1 |
| Fine | Multiplies the ratio by 1 + Fine/100 (in Fixed mode, by 10^(Fine/100)). | 0-99 / 0 |
| Detune | Slight detune, about +/-7 cents around A4. | -7..+7 / 0 |
| Level | Operator output level (same as the OP knobs). | 0-99 / OP1 99, others 0 |
| R1 ... R4 | Envelope rates (0 slow ... 99 instant). 1 attack, 2-3 decay, 4 release. | 0-99 / 99 |
| L1 ... L4 | Envelope levels. L3 is the sustain; L4 the end of the release. | 0-99 / L1-L3 99, L4 0 |
| Vel | Velocity sensitivity. | 0-7 / 0 |
| Rate Sc | Rate scaling: higher notes have faster envelopes. | 0-7 / 0 |
| AMS | Amplitude modulation sensitivity to the FM LFO. | 0-3 / 0 |
| Break | Keyboard level scaling break point: the note where the left and right curves start. | A-1 (MIDI 21) to MIDI 120 / MIDI 60 (C3 in Yamaha naming) |
| L Depth / R Depth | Level scaling depth below / above the break point. | 0-99 / 0 |
| L Curve / R Curve | Level scaling curve below / above the break point. | -LIN, -EXP, +EXP, +LIN / -LIN |
| Fixed | Fixed frequency: the operator ignores the key (drum and noise effects). | Off / On / Off |

The defaults form the DX7 **INIT VOICE**: algorithm 1, OP1 at level 99 and the rest at 0, envelope 99/99/99/99 -
99/99/99/0, ratio 1, break point C3 (Yamaha) = MIDI 60, a flat pitch envelope (levels 50), transpose 0 and both key
syncs on.

## Playing it

- **Polyphony:** 16 voices, as on the DX7. Playing a note that is already sounding reuses its voice; with all 16 busy,
  the oldest note is replaced.
- **Sustain pedal** (CC 64) holds released notes until the pedal is lifted.
- **Oscillator key sync** (Osc Sync, on by default): every operator's phase is reset to 0 on each note; off, the
  operators run freely.
- **LFO delay** restarts only when you press a key with no other key held; with LFO **Sync** on, the LFO cycle
  restarts too.
- **Switching Mode** (DX7 / Clean) cuts the sounding notes.

### Loading a DX7 voice or bank

1. Click **Load .syx...** and choose a `.syx` file.
2. Accepted formats: a 32-voice bank (4104 bytes with SysEx header, or the bare 4096-byte block) or a single voice
   (163 bytes).
3. A single voice is applied at once. For a bank, a menu lists the 32 voices by number and name; pick one.
4. The voice writes all operator parameters, Algorithm, Feedback, the LFO settings (with LFO key sync), the pitch
   envelope, Transpose and oscillator key sync.
5. The display shows it as a cartridge voice, with its number in the bank.

### Building a sound from INIT

1. Choose an **Algorithm**; open **Edit...** to see which operators are carriers (highlighted).
2. Raise the **Level** of a modulator to add brightness; set its **Coarse** ratio for the harmonic colour.
3. Shape each operator with **R1-R4 / L1-L4**; a modulator with a fast decay gives a percussive attack.
4. Add **Vel** on modulators so harder playing is brighter.

## Faithful details

- **DX7 mode** computes 16 voices x 6 operators in the integer arithmetic of the OPS chip: quarter-wave log-sine ROM
  (14 bits) and 12-bit exponential ROM (Shirriff), 12-bit phase, at 49,096 Hz.
- **Floating-point DAC** of the DX7: 15-bit signed samples reduced to a 12-bit mantissa with a /1, /2, /4 or /8 scale
  depending on the leading zeros.
- Each DAC step is reconstructed band-limited (zero-order hold, as when recording the analog output) and then passes
  an output low-pass.
- Envelopes run in the chip's logarithmic domain; LFO speed, PMD/AMD and PMS follow the msfa tables; operator frequency
  follows `dx7note.cc` with Dexed's empirical detune.
- Feedback loops in algorithms 4 and 6 are reproduced.

Optional setting that goes beyond the original: **Mode = Clean** (smooth floating-point FM at the host sample rate),
off by default.

## Factory presets

| Preset | Category |
|---|---|
| FM E.Piano | Keys |
| FM Bells | Keys |
| FM Tubular Bells | Keys |
| FM Organ | Keys |
| Tonewheel Organ B3 | Keys |
| DX7 E.Piano 1 | Keys |
| Leslie Organ | Keys |
| FM Brass | Brass |
| FM Bass | Bass |
| DX7 Bass 1 | Bass |
| FM Glass Pad | Pad |
| FM Ensemble Strings | Strings |
| FM Marimba | Pluck |
| FM Bleep | Lead |
| Industrial Metal Hit | Drums |

The FM E.Piano preset is KoniSynth's own conversion to the DX7 scale, not the ROM patch. FM Bleep is an original design
after a 1990 bleep sound, not a copy of it.

## Limitations

- The amplitude modulation scale is estimated (0 to 24 dB with AMD 99 and AMS 3).
