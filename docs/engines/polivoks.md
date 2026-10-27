# Polivoks

The Polivoks engine emulates the Formanta Polivoks (1982, designed by Vladimir Kuzmin), the Soviet duophonic
synthesizer: two VCOs with five waveforms (32' to 2'), VCO2 frequency-modulating VCO1, portamento on VCO1 only, noise,
a mixer that overdrives the filter, a 2-pole state-variable filter (low-pass / band-pass), two ADSR envelopes with an
attack-decay repeat mode, and a four-waveform LFO. The filter is computed at 4x oversampling because of its
non-linearity. Use it for rough basses, screaming resonant leads and two-note duophonic lines.

As with every emulated engine in KoniSynth, the defaults reproduce the original behaviour, imperfections included
(oscillator drift is on by default, the filter has no keyboard tracking and the mixer has no limiter).

## What you need

Nothing extra.

## Controls

Select **Polivoks** in the Engine menu. Choosing it from that menu loads the preset **Polivoks Scream**. The engine
shows two rows: VCO and Mixer / VCF on top; Filter EG, Amp EG and LFO below.

### Polivoks VCO

| Control | What it does | Range / default |
|---|---|---|
| Voice menu (header) | Mono, or Duo: lowest key to VCO2, highest to VCO1, as on the original. | Mono, Duo / Mono |
| Wave 1 | VCO1 waveform. | Triangle, Saw, Square, Pulse 1, Pulse 2 / Saw |
| Range 1 | VCO1 range, one octave per step (8' = the written note). | 32', 16', 8', 4', 2' / 8' |
| Wave 2 | VCO2 waveform. | as Wave 1 / Saw |
| Range 2 | VCO2 range. | 32' to 2' / 8' |
| Detune | VCO2 detune. A little detune (0.1 st) gives the classic thick Polivoks bass. | -12..+12 st / 0 st |
| FM | VCO2 modulates the frequency of VCO1 (exponential, up to +/-2 octaves). Works best with VCO2 lower than VCO1. | 0-100 % / 0 % |
| Glide | Portamento of VCO1 only (exponential, like an RC circuit). | 0-2 s / 0 s |
| Drift | Oscillator drift, up to the factory spec of +/-0.5 % (about 9 cents) at 100 %. | 0-100 % / 30 % |

### Polivoks Mixer / VCF

| Control | What it does | Range / default |
|---|---|---|
| Mode menu (header) | Filter output: low-pass (12 dB/oct) or band-pass. | LP, BP / LP |
| VCO 1 | VCO1 level. No limiter: high levels overdrive the filter, part of the Polivoks sound. | 0-100 % / 50 % |
| VCO 2 | VCO2 level (also drives the filter). | 0-100 % / 50 % |
| Noise | White noise level. | 0-100 % / 0 % |
| Cutoff | Filter cutoff, no keyboard tracking. | 100 Hz-10 kHz / 1500 Hz |
| Reso | Resonance: rough and unstable, self-oscillates near the top. | 0-100 % / 20 % |
| Env | How far the filter envelope moves the cutoff, in Hz (the filter responds linearly in Hz). | -10000..+10000 Hz / 3000 Hz |

### Polivoks Filter EG and Polivoks Amp EG

Both sections have the same controls. Each shows its envelope (RC curves; with Repeat, two attack-decay cycles), drawn with the engine's own formulas; drag its points to set the times and levels (right-click a point: MIDI Learn, Set value, Reset; double-click: default).

| Control | What it does | Range / default |
|---|---|---|
| Repeat (switch) | The envelope repeats attack-decay while the key is held. | Off / On / Off |
| A | Attack. | 0.5 ms-3 s / 5 ms |
| D | Decay. | 0.5 ms-12 s / 0.4 s |
| S | Sustain. | 0-100 % / Filter 20 %, Amp 80 % |
| R | Release. | 0.5 ms-12 s / 0.2 s |

### Polivoks LFO

| Control | What it does | Range / default |
|---|---|---|
| Wave menu (header) | Triangle, square, noise (random pulses) or sample & hold. | Triangle, Square, Noise, S&H / Triangle |
| Rate | LFO rate. | 0.1-30 Hz / 5 Hz |
| VCO 1 | LFO to VCO1 pitch (up to +/-0.5 octave at 100 %). | 0-100 % / 0 % |
| VCO 2 | LFO to VCO2 pitch (up to +/-0.5 octave). | 0-100 % / 0 % |
| VCF | LFO to filter cutoff (up to +/-3000 Hz). | 0-100 % / 0 % |
| VCA | LFO to volume, tremolo (up to +/-50 %). | 0-100 % / 0 % |

There are no header buttons or extra dialogs in this engine.

## Playing it

- **Mono:** both VCOs play the last key held. Releasing a key returns to the previous held key.
- **Duo:** with two keys held, the highest goes to VCO1 and the lowest to VCO2. With one key, both play it.
- **Envelopes retrigger** on every new key (multiple trigger).
- **Mod wheel** works like the original cutoff pedal: it adds up to +5000 Hz to the cutoff.
- The total cutoff (knob + envelope + LFO + wheel) can go beyond the knob range, from 20 Hz to 18 kHz.

### Recipes

**Thick bass** (as "Polivoks Bass"): Range 1 and Range 2 = 16', Detune = 0.1 st, VCO 1 and VCO 2 about 55 %,
Reso about 25 %, Cutoff 600 Hz, Env 2500 Hz, Filter EG D = 0.25 s, S = 10 %.

**Duophonic lead** (as "Polivoks Duo"): Voice = Duo, Wave 1 = Pulse 1, Wave 2 = Saw, Reso 40 %, Cutoff 2500 Hz. Hold
a bass note with the left hand and play the melody above it.

**Repeating filter** (as "Polivoks Repeat"): Filter EG Repeat on, A = 2 ms, D = 0.12 s, Cutoff 400 Hz, Env 4000 Hz,
Reso 60 %. Hold a key: the filter envelope cycles attack-decay.

**Scream** (as "Polivoks Scream"): Wave 1 = Square, FM 35 %, Range 2 = 16', VCO 1 = 90 %, VCO 2 = 60 %, Mode = BP,
Reso 85 %, Cutoff 1800 Hz, Glide 0.08 s.

## Faithful details

- **Filter:** the integrators are К140УД12 (uA776) op-amps without external capacitors, modelled as OTA-C integrators
  with tanh saturation. Result: cutoff linear in Hz with the control current, no keyboard tracking, and slew-rate
  limiting at large signals.
- **Resonance without limiter:** at the top it self-oscillates with an almost square shape.
- **Mixer overdrive:** the filter input is attenuated about 1:48 from a 10 Vpp saw, so the mixer drives the filter into
  the knee of the tanh.
- **Crossover distortion** of the output stage at low current: a dead zone that grows as the cutoff falls (observed by
  Kuzmin; amount estimated).
- **Factory cutoff range:** 100 Hz-10 kHz. **VCO drift:** +/-0.5 % in 4 h, the manual's spec.
- **Duo assignment** (highest key to VCO1) and **glide on VCO1 only**, as on the original.
- Sources named in the code: the Soviet owner's manual (archive.org), Sound On Sound (Polivoks Pro), Wikipedia,
  vintagesynth, modularsynthesis.com (Kuzmin's VCF), North Coast Synthesis (SVF) and monosounds (Duovox).

There is no "beautify" switch in this engine. Setting **Drift** to 0 % removes the oscillator drift.

## Factory presets

| Preset | Category |
|---|---|
| Polivoks Bass | Bass |
| Polivoks Acid Bass | Bass |
| Polivoks Scream | Lead |
| Polivoks Duo | Lead |
| Polivoks Repeat | Sequence |
| Polivoks S&H Filter | Sequence |

## Limitations

Estimated, with no data from the original:

- envelope times (range taken from the Erica clone: attack 0-3 s, decay/release 0-12 s);
- LFO rate range;
- the widths of the two pulse waveforms (Pulse 1 = 25 %, Pulse 2 = 10 %);
- the amount of crossover distortion;
- that every new key retriggers the envelopes.
