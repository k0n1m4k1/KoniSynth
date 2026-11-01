# Gravis UltraSound

The Gravis UltraSound engine emulates the GF1 wavetable chip of the Gravis UltraSound sound cards (Classic 1992, MAX 1994,
Extreme 1996): 32 sample voices mixed in hardware, playing the instrument patches loaded into the card's memory. It is
built from the figures in Gravis' own developer kit (UltraSound SDK v2.01, "Low Level Toolkit", 1993). Use it for
early-90s PC music played through the UltraSound's General MIDI driver, and for its sampled instruments.

## What you need

**A patch set, which you must supply.** The original Gravis patches are copyrighted and are not included.

1. Put the `.PAT` files and their map file in `Documents/KoniSynth/Patches/GUS` (the default folder), or press
   **Patches... > Choose patch folder...** and pick another folder. The choice is global for all KoniSynth instances.
2. The map file can be, in this order of preference:
   - Gravis' `ULTRAMID.INI` (in `ULTRASND\MIDI` of an original installation): the map the ULTRAMID driver used, with the
     patch each program plays for every card memory size. With it, the RAM control works as on the card.
   - A `timidity.cfg` (lines `<program> <file.pat>` under `bank 0`, and `<key> <file.pat>` under `drumset 0`).
   - Gravis' `DEFAULT.CFG` (programs 1-128; the drums in its `begin_multipatch` block).

   Patches are searched in the folder and its subfolders.
3. Without patches, the section header says `no patches: see Patches...`.

Sources of patches: the `ULTRASND\MIDI` folder of an original installation, or the free FreePats set (GPL-3.0 or later
with an exception; it sounds different from the Gravis set).

**Patches... menu:**

| Item | What it does |
|---|---|
| (status line) | Result of the last read: how many patches and how much memory they take, or what is missing. |
| Choose patch folder... | Select the folder with your patches. |
| Open patch folder | Opens the current folder (created if missing). |
| Rescan | Reads the folder again. |

## Controls

| Control | What it does | Default |
|---|---|---|
| Model | Classic, MAX or Extreme. All use the same GF1 chip; the model sets the memory: Classic 256 KB to 1 MB, MAX 512 KB to 1 MB, Extreme 1 MB soldered (RAM is dimmed). | Classic |
| Multi | On: the 16 MIDI channels play as a General MIDI module (drums on channel 10), for .mid files and DAW tracks. Off: your keyboard plays the Keys setting. | off |
| Keys | Program: your keyboard plays the program in the list. Rhythm: the drum kit (one sound per key). | Program |
| Program | General MIDI program for Keys = Program (the sound comes from your patch set). | 1 - Acoustic Grand Piano |
| RAM | Memory on the card: 256 KB, 512 KB, 768 KB or 1 MB. With `ULTRAMID.INI`, each program plays the patch that file gives for this size (for example, with 256 KB the organs play the patch of program 3, Electric Grand Piano). Without it, patches load as they are used and one that does not fit is replaced by a loaded patch of the same family. | 1 MB |
| Voices | Active voices of the GF1, 14 to 32. More voices lower the output rate: 44.1 kHz with 14, 30.9 kHz with 20, 19.3 kHz with 32. | 24 |
| Bits | Sample depth of the loaded patches. 8-bit: ULTRAMID's default, which keeps the high byte of each 16-bit sample (more noise, half the memory). 16-bit: its `-16` option. | 8-bit |

## Display

The card had no display; this one shows what was invisible on it. Each of the 32 GF1 voices shows its note, patch, volume
level, envelope point (0-5; S held at the sustain point, R released, E playing out to the end of the sample) and pan (the thin
line under the level). A stolen voice flashes, and voices beyond the active count are faded. The footer gives the active
voices and the chip rate they give, the card memory in use with a mark every 256 KB, the resident patches, how many programs
play a substitute, the sample depth, and whether `ULTRAMID.INI` is in use.

## How it works

What comes from the Gravis SDK (verified):

| Feature | Value |
|---|---|
| Voice service | One voice every 1.6 us, looping over the active voices: each voice is updated at 1,000,000 / (1.619695497 x voices) Hz (44,100 Hz with 14, 19,293 Hz with 32). Fewer than 14 voices count as 14. |
| Pitch | A 6.9 fixed-point step per update (6 integer bits, 9 fraction bits), computed as round(f x 512 / rate): pitch is quantized to 1/512 of a sample per update. |
| Interpolation | Linear between the two neighbouring samples, at 16 bits also for 8-bit data. Steps above 1 skip samples without filtering, which the SDK warns can alias. |
| Volume | 12 bits, logarithmic: 4-bit exponent and 8-bit mantissa (about 0.0235 dB per step, 96 dB range). |
| Volume ramps | Hardware ramps for the envelopes: a step of 1-63 applied every 1, 8, 64 or 512 updates. Ramps take longer with more voices (2.3 times with 32 than with 14). |
| Panning | 16 positions, 0 = left, 15 = right (no exact centre). |
| Loops | Forward and bidirectional; samples marked "backward" play reversed. All active voices are summed, stopped ones too. |
| Patch format | GF1PATCH110 .PAT files: per sample, its rate, key range, root pitch, loop, balance, a 6-point envelope and key scaling. |

What the driver does, read from the machine code of the SDK's own library (the copy linked into the SDK's patch editor,
`PATCH.EXE`, which keeps its debug symbols). Whether Gravis' resident MIDI driver (ULTRAMID) uses the same library has not
been verified.

| Feature | Rule |
|---|---|
| Note pitch | A note table of 440 x 2^((n - 69) / 12) Hz in thousandths, truncated. Note 127 is 0 Hz in that table, so it is silent. |
| Step | Computed in three rounded integer steps: rate ratio = 512 x chip rate / sample rate; t = 512 x f / root; step = 512 x t / ratio. The 32-bit arithmetic overflows above 8,388 Hz, so notes 121-126 play out of tune, as on the card. |
| Key scaling | When a sample's scale factor is not 1024, its note is scaled in 1/1024 semitone steps (a 16-bit product) and interpolated between table notes. From note 95 up the library mixes a value divided by 32 with an undivided one; that is kept. |
| Velocity and volume | Level factor = (127 + 64)(velocity + 128)(channel volume + 128) / 48,705, where channel volume = CC 7 x CC 11 / 127. Every envelope level is multiplied by it / 255 (minimum 8). Defaults: CC 7 = 127, CC 11 = 127. |
| Envelope | Point 0 ramps up from 8. Each later point ramps toward its offset; the direction comes from comparing it with the previous offset. With sustain the note holds at point 2's level until released, then goes to point 3 with rate 3. Bit 7 of the modes jumps to point 5. After point 5 the loop is switched off and the sample plays to its end. Without an envelope, note-off does nothing. |
| CC 7 / CC 11 | A ramp to the new level at rate 0x44, then the envelope resumes. Without sustain the envelope stops after that ramp (a library quirk, kept). |
| Pan | CC 10 / 8 (16 positions). It also moves notes already released. Until a CC 10 arrives, each sample's own balance is used. |
| Pitch bend | Default range 2 semitones (RPN 0). Data entry also changes it when no RPN has been selected. Up = semitone table with linear interpolation; down = 1 / up. |
| Vibrato | A software LFO on the GF1's Timer 2 (69 counts of 320 us = 45.29 Hz). Rate: f = 0.05 + 0.02334 x rate Hz. Each quarter of the triangle lasts round(45.29 / (4 f)) ticks, so 5.66 Hz is the top. Depth: 21 units per semitone. Sweep: the depth grows from 0 over sweep x 22.08 ms. |
| Tremolo | A looping volume ramp around the sustain level. Depth: (depth + 1) / 8 steps of the 8-bit volume (about 0.38 dB each, up to about 12 dB). Speed from the same rate formula and the number of active voices. It only sounds while a note is held at its sustain point. The patch's tremolo sweep is not used by the library. |
| Voice allocation | Round robin from the voice after the last one used. If none is free, the stolen voice is the released one, else one held by the pedal, else a sounding one (priorities 30 / 15 / 10); the oldest wins ties. The same key twice gets two voices. |
| Other controllers | CC 64 holds released notes until the pedal is up. CC 121 resets bend, expression and pan only. CC 123-127 release the notes on all channels. CC 1 and the others are ignored. |
| 8-bit loading (ULTRAMID v1.12) | Without `-16`, ULTRAMID copies only the high byte of each 16-bit sample (truncation, no rounding) and uploads it as 8-bit data; unsigned data is made signed by the card's DMA. |
| Memory sizes (ULTRAMID v1.12) | `ULTRAMID.INI` lists, for every program and drum (128 + key), the number that plays with 256, 512, 768 and 1024 KB; number 128 is a silent "blank" patch. |

What is an estimate (neither the SDK nor its library settles it):

| Feature | Choice |
|---|---|
| Pan law | Constant power (as in DOSBox Staging). |
| Output stage | A first-order low-pass at 8 kHz (DOSBox Staging's fit to recordings of the Classic) and a 16-bit mixer that clips. |
| Sample choice | The library keeps the voice's current sample while the note lies strictly inside its range. Otherwise it scans the first 16 samples and takes the first one that holds the note, unless the next one holds it too (with overlapping ranges the last one wins). With no match, the note is silent. Which sample counts as "current" when a note starts depends on the driver; here it is the last one. |
| Patches without envelope | They play at the velocity level; the library leaves the previous volume. |
| Stolen voice | It restarts at once; the library first ramps it down to the minimum. |
| Memory without `ULTRAMID.INI` | Patches load in order of use; one that does not fit is replaced by a loaded patch of the same General MIDI family. |
| Voices default | 24. |

Not implemented yet: the Extreme's FM chip (ESS ES1688, for Sound Blaster compatibility), planned as a second phase.

The output goes to the host rate through a sample-and-hold DAC rebuilt with band-limited steps, as in the SNES and N64
engines. Microtuning is not applied.

## Sources

- Advanced Gravis / Forte, *Low Level Toolkit/Source for UltraSound*, UltraSound SDK v2.01 (1993): chapter 1 (theory of
  operation, volume ramping), chapter 2 (voice registers, timers), appendix F (patch header).
- The same SDK's library, as linked into its patch editor `ULTRA3D\PATCH.EXE` (driver behaviour: functions such as
  `calculate_vibrato`, `calculate_tremolo`, `_GF1_HERTZ_ON/OFF`, `_GF1_MIDI_PARAMETER`).
- Advanced Gravis, UltraSound software 4.11 for DOS (1995): `ULTRAMID.EXE` v1.12 (patch loading) and `ULTRAMID.INI`.
- UltraSound FAQ v1.56 (the voices / playback rate table).
- Wikipedia, "Gravis Ultrasound" (models, memory).
- DOSBox Staging, `gus.cpp` (pan law and output filter used as estimates).
