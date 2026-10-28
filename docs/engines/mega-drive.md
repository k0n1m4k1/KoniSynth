# Mega Drive

The Mega Drive engine emulates the sound hardware of the Sega Mega Drive / Genesis: the Yamaha YM2612 FM chip of the
Model 1 boards, or the YM3438 (OPN2C) core of the later ASIC, plus the SN76489 PSG inside the video chip. The six
4-operator FM channels play as a 6-voice polyphonic synth; the PSG, the channel 6 DAC and the channel 3 special mode are
available as separate voice modes. The chip is modelled sample by sample at 53,267 Hz (53,693,175 Hz / 1008, NTSC),
following Nuked-OPN2 (silicon analysis) and plutiedev.com "YM2612 registers". Use it for 16-bit Sega FM basses, brass,
electric pianos and PSG blips.

## What you need

Nothing extra. The optional DAC Sample voice plays a sample that you load yourself (see "Playing it"); KoniSynth ships
no samples.

## Controls

The engine shows four sections: **Mega Drive** and **MD LFO** on the first row, **MD Voice** and **MD PSG (SN76489)** on
the second.

### Mega Drive

| Control | What it does | Range / default |
|---|---|---|
| Model (header menu) | Model 1: discrete YM2612 with the "ladder effect" and the ~3.4 kHz output low-pass. Model 2: YM3438, no ladder, same low-pass. Clean: YM3438 without the analog filter, brighter than any real console. | Model 1 (YM2612) / Model 2 (YM3438) / Clean (YM3438); default Model 1 |
| Operators... (header button) | Opens the Mega Drive editor (see below). | |
| Algorithm | How the 4 operators connect, as on the YM2612. 0: 1->2->3->4 (one carrier) ... 7: all four heard. | 0-7; 0 |
| Feedback | OP1 feedback: from pure sine to buzzy and, at 7, noisy. | 0-7; 0 |
| OP1 TL ... OP4 TL | Total level of each operator, 0.75 dB steps (0 = loudest, 127 = -95 dB). On a carrier it is volume; on a modulator, brightness. | 0-127; OP1-OP3 127, OP4 0 |

The default patch is the equivalent of an INIT VOICE: algorithm 0 with OP1-OP3 silent (TL 127) and OP4 a sine with AR 31,
RR 15.

**Mega Drive editor (Operators...).** One column per operator (OP1-OP4), carriers highlighted, and the current algorithm
written out (for example `(1 -> 2) + (3 -> 4)`). Per operator:

| Control | What it does | Range / default |
|---|---|---|
| TL | Total level (as above). | 0-127 |
| MUL | Frequency multiple: 0 = x0.5, 1 = x1 ... 15 = x15. | 0-15; 1 |
| DT | Detune: small pitch offsets that grow with the note, as on the chip. | -3..+3; 0 |
| AR | Attack rate (0 = never, 31 = instant). Exponential attack, like the chip. | 0-31; 31 |
| DR | First decay rate, from full level to the sustain level (0 = none). | 0-31; 0 |
| SR | Second decay (sustain) rate while the key is held (0 = holds). | 0-31; 0 |
| SL | Sustain level, 3 dB per step (15 = -93 dB). | 0-15; 0 |
| RR | Release rate (0 slow ... 15 fastest). | 0-15; 15 |
| KS | Key scale: higher notes get faster envelopes. | 0-3; 0 |
| SSG-EG | The envelope repeats, alternates or holds (uses the decay rates). | Off, 0 Repeat ... 7 Once Inv; Off |
| AM | Amplitude modulation by the LFO (depth set by AMS). With the LFO off, AM still attenuates, as on the real chip. | off |

### MD LFO

| Control | What it does | Range / default |
|---|---|---|
| LFO (header menu) | Chip LFO speed (the 8 YM2612 rates) or Off. | Off, 3.85, 5.40, 5.86, 6.21, 6.71, 9.46, 52.0, 83.2 Hz; Off |
| Velocity | Key velocity lowers the carriers' level (KoniSynth's driver; the chip has no velocity). | on |
| PMS | Vibrato depth from the LFO (0 to 80 cents). | 0-7; 0 |
| AMS | Tremolo depth on operators with AM on (0, 1.4, 5.9, 11.8 dB). | 0-3; 0 |
| Pan | Stereo of the 6 FM channels (register B4h: hard left, right or both). Alternate: left, right, left... Spread: left, center, right... | Center / Left / Right / Alternate L/R / Spread L/C/R; Center |
| Output | Stereo: Model 1 headphone jack or Model 2 AV. Mono: Model 1 AV and RF outputs, as most people heard it on a TV. | Stereo / Mono; Stereo |

### MD Voice

| Control | What it does | Range / default |
|---|---|---|
| Voice (header menu) | What the keys play: FM (6 ch); FM + PSG (each note also on a PSG square channel); PSG Tone (3 ch, lowest note ~109 Hz); PSG Noise (mono); DAC Sample (ch 6) (mono); FM Ch3 Special (mono). | default FM (6 ch) |
| Frame Macros... (header button) | Per-frame macros of the sound driver (59.92 Hz). See "Playing it". | |
| DAC Rate | Rate at which the driver writes the sample into channel 6's DAC (register 2Ah). The Root note of its own sample (Sample... button) plays at this rate; the DAC holds each 8-bit value until the next write. | 6.5 / 8 / 11 / 16 / 22 kHz; 8 kHz |
| Ch3 OP1, Ch3 OP2, Ch3 OP3 | Channel 3 Special mode: frequency of OP1-OP3 in semitones from the played note. OP4 always plays the note. | -24..+24; 0 |

### MD PSG (SN76489)

The display shows the driver envelope (2 dB steps, one every A/D/R video frames), drawn with the engine's own formulas; drag its points to set the times and levels (right-click a point: MIDI Learn, Set value, Reset; double-click: default).

| Control | What it does | Range / default |
|---|---|---|
| Noise type (header menu) | White: 16-bit shift register, taps on bits 0 and 3 (Sega's PSG). Periodic: 16-step buzz at noise rate / 16. | White / Periodic; White |
| N.Rate | Noise clock. Key: follows the note through tone channel 3's divider. Fixed: 3,579,545 Hz / 512, 1024 or 2048. | Key (tone 3) / 6991 / 3496 / 1748 Hz; Key |
| A | Driver attack: frames per 2 dB step from silence to full volume, at 59.92 frames/s. 0 = instant. | 0-15; 0 |
| D | Frames per 2 dB step from full volume down to S. 0 = instant. | 0-15; 0 |
| S | Attenuation held while the key is down, 2 dB steps (15 = silent). | 0-15; 0 |
| R | Frames per 2 dB step after release (15 steps to silence). 0 = instant. | 0-15; 2 |

The PSG has no envelope of its own; A/D/S/R are a sound-driver envelope.

## Playing it

- **FM (6 ch):** 6-voice polyphony. A repeated note reuses its channel; otherwise the oldest free channel, else the oldest.
  Retrigger works like a driver: key off one sample before key on.
- **FM + PSG:** the FM channels plus 3 PSG square channels, allocated separately.
- **PSG Noise, DAC Sample, FM Ch3 Special:** monophonic, last-note priority with a stack of held notes.
- **DAC Sample:** plays this engine's own sample (**Sample...** button of the MD Voice section, separate from the Synth's)
  through channel 6, quantized to 8 bits. An 8-bit WAV at the DAC Rate is the chip's own format and is not altered. With Loop off the
  sample plays to the end, like drum samples in games.
  1. Press **Sample...** in the MD Voice section and Load a file (Start, Length, Loop, Root apply).
  2. Set Voice = DAC Sample (ch 6).
  3. Set DAC Rate; the Root note plays at that rate.
- **Frame Macros...:** one line of text per macro, Furnace syntax: values separated by spaces, one per frame; `|` = loop
  start, `/` = release point; in the arpeggio `@n` = absolute note from C-0 (48 = C-4 = MIDI 60). Max 256 steps.
  Text with errors is not applied and the error is shown.
  - Volume (0-15): FM = extra TL on the carriers, 2 dB per step; PSG = attenuation, replacing its envelope.
  - Arpeggio (semitones or `@n`), Pitch (FM: F-number, PSG: period N; + = lower), PSG Noise (0 white, 1 periodic).
  - The DAC does not use macros.
  - Example, falling volume with a release: `15 13 11 9 / 6 3 0`.

## Faithful details

- **Ladder effect (Model 1):** the YM2612's 9-bit DAC outputs each channel 1 cycle in 4 and the sign the rest, a jump of
  ~1.6 % of full scale at every zero crossing that adds grit to quiet notes (Nuked-OPN2). The YM3438 models do not have it.
- **Analog low-pass:** 1 pole at 3.39 kHz on Model 1 and Model 2 (Genesis Plus GX, Mega Sg setting).
- **Operator maths:** 20-bit phase, log-sine and exponential tables matching Nuked's ROMs, 10-bit envelope with the chip's
  timer (one step every 3 samples), exponential attack, SSG-EG, and the chip's one-sample delays inside the algorithms.
- **LFO rates:** one step every 108, 77, 71, 67, 62, 44, 8 or 5 samples (3.85 ... 83.2 Hz). The datasheet gives
  3.82 ... 69.2 Hz; the two fastest follow Nuked and Genesis Plus GX instead.
- **AM with the LFO off** still attenuates, as on the real chip.
- **PSG:** clock 3,579,545 Hz, tone f = clock / (32·N) with a 10-bit N (lowest 109.3 Hz), 16 volumes in 2 dB steps,
  unipolar mono output (smspower.org SN76489). Level against FM: 0.513 of an FM channel on Model 1 (Genesis Plus GX,
  "adjusted to the PSG/FM balance of an MD1 VA4"); x20/32 on Model 2 and Clean (Mega Sg M1/M2 settings).
- **Hard panning only:** register B4h has no in-between positions.
- **Pitch tables:** F-numbers in a 644..1288 octave, A4 = F-number 1083 block 4, as driver tables (plutiedev gives 1081).

Optional "nicer" setting: Model = Clean (no analog filter). Defaults are Model 1, stereo output.

## Factory presets

| Preset | Category |
|---|---|
| MD Green Hill Bass | Bass |
| MD Electric Slap Bass | Bass |
| MD Slap Bass | Bass |
| MD GEMS-Style Bass | Bass |
| MD Grand Piano | Keys |
| MD E.Piano | Keys |
| MD Tubular Bells | Keys |
| MD Synth Brass | Brass |
| MD Additive Pad | Pad |

Choosing Mega Drive from the engine menu loads MD Green Hill Bass.

## Limitations

- PSG Noise, DAC Sample and FM Ch3 Special are monophonic.
- The DAC Sample voice and the PSG Noise voice are mono.
- Velocity is KoniSynth's driver behaviour, not a chip feature.
- The chip is clocked at the NTSC rate (53,267 Hz); no PAL clock option appears in the code.
