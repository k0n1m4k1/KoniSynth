# SNES

The SNES engine emulates the Super Nintendo sound unit: the Sony SPC700 + S-DSP running at 32,000 Hz, with 8 voices that
play BRR-compressed samples through the chip's Gaussian interpolation, ADSR/GAIN envelopes, noise generator and 8-tap FIR
echo. Everything follows fullsnes (problemkaputt.de, "SNES APU DSP"). Use it for the muffled, echoing sample sound of
16-bit Nintendo music, with KoniSynth's own samples or with yours.

## What you need

Nothing extra. The six built-in sources are KoniSynth's own samples, BRR-encoded at start-up. To play your own sound,
load it in the Sample panel (WAV, AIFF, FLAC, OGG, SoundFont .sf2, SNES .brr or .spc); KoniSynth includes no game samples.

## Controls

Three sections on one row: **SNES** (source) -> **SNES Envelope** -> **SNES Echo / Output**.

### SNES

| Control | What it does | Range / default |
|---|---|---|
| Source (header menu) | BRR sample. Square, Saw, Sine: 32-sample loops; Strings, Choir: 4400-sample loops (3 detuned voices); Bass: 64-sample loop. User Sample: the Sample panel's file, re-encoded to BRR. | Square / Saw / Sine / Strings / Choir / Bass / User Sample; Square |
| Noise | Replace the sample with the S-DSP noise generator (15-bit LFSR, not interpolated). | off |
| Frame Macros... (header button) | Experimental per-frame macros (60.10 Hz). See "Playing it". | |
| Noise Rate | Noise clock: 1 = 16 Hz ... 30 = 16 kHz, 31 = 32 kHz. | 0-31; 28 |
| Interp | Gaussian: the S-DSP's own 4-point filter with its 512-entry table. Linear and None are only for comparison: the console can't do them. | Gaussian / Linear / None; Gaussian |
| User Rate | Rate at which the user sample is stored in BRR. Lower = more muffled and longer samples within the 64 KB of audio RAM. | 32 / 16 / 8 kHz; 32 kHz |
| Volume | Voice volume register; velocity scales it when Velocity is on. | 0-127; 80 |

### SNES Envelope

| Control | What it does | Range / default |
|---|---|---|
| Mode (header menu) | ADSR (hardware attack/decay/sustain, release -8 per sample) or a GAIN mode: direct level, linear down, exponential down, linear up, bent up. | ADSR / Direct Gain / Linear Down / Exp Down / Linear Up / Bent Up; ADSR |
| Velocity | Key velocity scales the voice volume (KoniSynth's driver; the S-DSP has no velocity). | on |
| A | ADSR attack. 15 = instant (+1024 per step); 0 = about 4.0 s. | 0-15; 15 |
| D | ADSR decay rate, exponential, from full level to the sustain level. | 0-7; 7 |
| SL | Sustain level: where the decay stops, (N+1)/8 of full level. | 0-7; 7 |
| SR | Sustain rate: exponential decay while the key is held. 0 = holds. | 0-31; 0 |
| Gain Rate | GAIN rate for the linear, exponential and bent modes (same 32-rate table). | 0-31; 16 |
| Direct | Direct GAIN level, used in Direct Gain mode. | 0-127; 127 |

Default envelope: A15 D7 SL7 SR0 (organ-like).

### SNES Echo / Output

| Control | What it does | Range / default |
|---|---|---|
| On (header toggle) | Send the voices to the echo (EON). | off |
| FIR (header menu) | Flat (`7F 00 00 00 00 00 00 00`) or the low-pass set from the official specs (`FF 08 17 24 24 17 08 FF`). | Flat / Low-pass; Low-pass |
| Filter | Analog output stage (blargg's model: gentle low-pass, -6 dB at 16 kHz, 20 Hz high-pass). Off = raw DSP output. | on |
| EDL | Echo delay, 16 ms per step, up to 240 ms (0 = 1 sample). | 0-15; 5 |
| EFB | Echo feedback. 64 = normal decaying echo, 127 = almost endless. | -128..127; 64 |
| EVOL | Echo volume. | -128..127; 48 |
| Main | Main volume register MVOL. | 0-127; 127 |
| Pan | Stereo balance of the voices (left/right volume registers). | -1..+1; 0 |
| Spread | Spreads successive notes to opposite sides, as a sound driver placing its channels. | 0-100 %; 0 % |
| Output | Stereo: AV (multi-out). Mono: RF to the TV aerial input. | Stereo (AV) / Mono (RF); Stereo |
| DAC | Hold: each 32 kHz sample is held until the next, with its gentle top-octave droop and images above 16 kHz. Smooth: a low-pass resampler removes the images and rolls off from about 12 kHz (cleaner than the console). | Hold / Smooth; Hold |

## Playing it

- 8 voices. Key-on resets the envelope and starts the sample from the beginning, after 5 empty samples, as on the chip.
- **Your own sample:**
  1. Switch to the Synth engine, open the Sample tab, press Edit... and Load a file. For .sf2 and .spc files a menu
     lets you pick the preset/zone or the sample inside.
  2. Set Start, Length, Loop and Root in the Sample panel.
  3. Switch back to SNES and set Source = User Sample. Choose User Rate (32, 16 or 8 kHz).
  - Audio files are resampled to the User Rate and BRR-encoded, up to 64 KB (7281 blocks). The Root note plays at that rate.
  - Original BRR data (.brr, or a sample taken from an .spc) is played as it is, at 32 kHz, with its own End/Loop flags;
    Start, Length and Loop do not apply. A .brr file of size 9·n + 2 starts with a 2-byte loop offset; size 9·n loops
    (if at all) to the start.
- **Frame Macros... (experimental):** Furnace syntax (`|` loop, `/` release, `@n` absolute note in the arpeggio, max 256
  steps). Volume 0-15 scales the voice volume; Arpeggio in semitones; Pitch moves the pitch register 8 units per step
  (+ = lower); Noise (NON) 1 = that voice plays the noise generator. Nintendo's N-SPC driver stepped by tempo ticks, not
  video frames: here the macros run at 60.10 Hz, like a sound-effect driver.

## Faithful details

- **BRR:** 9-byte blocks, filters 1-3 with their exact formulas, clipping to 16 bits and to 15 bits with sign loss.
- **Pitch:** 14-bit (0x1000 = 1:1 at 32 kHz).
- **Gaussian interpolation:** the chip's 512-entry table (sums 0x7FF..0x801) and its overflows (2nd-3rd sums wrap to
  16 bits, the 4th saturates).
- **Envelope:** 11 bits; ADSR attack +32 or +1024, exponential decay/sustain -(((L-1) SAR 8)+1), release -8 per sample;
  GAIN modes with the 32-rate table.
- **Mixer:** sample·vol SAR 6 with 16-bit saturation after each sum; echo buffer of EDL·16 ms; FIR of 8 taps (input
  SAR 1, products SAR 6, 7 sums with wrap-around and the last one saturated); echo input AND 0xFFFE; output XOR 0xFFFF
  (phase inversion of the post-amplifier). Left and right are filtered separately with no crosstalk.
- **Noise:** 15-bit LFSR, value -4000h after reset, not interpolated.
- **User samples** obey the 64 KB audio RAM limit.

Optional "nicer" settings, all off by default:

- DAC = Smooth (default Hold).
- Interp = Linear or None exist only for comparison.

Note: the Filter (on by default) is blargg's model from libgme (Spc_Filter); the code states that it is not a
measurement of the hardware.

## Factory presets

| Preset | Category |
|---|---|
| SNES Echo Strings | Pad |
| SNES Choir | Pad |
| SNES Square Lead | Lead |
| SNES Bass | Bass |
| SNES Pizzicato | Strings |
| SNES Noise Wind | Machines |

Choosing SNES from the engine menu loads SNES Echo Strings.

## Limitations

- 8 voices.
- User samples are limited to 64 KB of BRR (7281 blocks).
- The frame macros are experimental: the original N-SPC driver did not run on video frames.
- Velocity, Pan and Spread are KoniSynth's driver behaviour (voice volume registers), not S-DSP features.
