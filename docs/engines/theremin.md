# Theremin

The Theremin engine is a monophonic theremin played from an XY pad: X is the pitch hand, Y the volume hand. Its default
model, Physical, simulates the instrument rather than a synth with portamento: the pitch hand changes the capacitance of
a radio-frequency oscillator, the audible tone is the beat between that oscillator and a fixed one, and the two
oscillators lock together near zero beat. The constants are of the order of magnitude of a Moog Etherwave (estimated,
not measured). Use it for sci-fi and vintage film sounds, continuous slides and the low growl near silence; use the
Ideal model when you need stable, linear tuning.

As with every emulated engine in KoniSynth, the defaults reproduce the behaviour of the real instrument, imperfections
included. Anything that makes it "nicer" (here, the Ideal model) is an option and is off by default.

## What you need

Nothing extra.

## Controls

Select **Theremin** in the Engine menu. Choosing it from that menu loads the preset **Etheric Theremin**. The engine
shows the **Theremin** section and, next to it, **LFO** (LFO 1), which the theremin uses for vibrato or tremolo.

### Theremin

| Control | What it does | Range / default |
|---|---|---|
| Model menu (header) | Physical: physical model (non-linear field, locking). Ideal: linear, stable tuning. | Physical, Ideal / **Physical** |
| Range | Span in octaves across the pad, starting at C3. | 1-5 oct / 3.0 oct |
| Glide | Hand inertia: how long the pitch takes to follow the mouse. | 1-1000 ms / 60 ms |
| Tone | Timbre, from a pure sine to a wave richer in (odd) harmonics. | 0-100 % / 25 % |
| Field | Antenna field linearity (Physical only): low values compress the pitch strongly towards the antenna. | 2-40 cm / 10 cm |
| Lock | Oscillator locking range (Physical only): a silent zone and a low growl near zero beat. | 0-60 Hz / 12 Hz |
| Zero | Hand distance at which the theremin goes silent, zero beat (Physical only). | 20-90 cm / 60 cm |
| Drift | Same parameter as Drift in the Synth's Osc 1 tab: slow random detuning. | 0-100 % / 20 % |

The pad writes two more parameters, **Theremin Pitch** (X, default 50 %) and **Theremin Volume** (Y, default 0 %). They
are automatable; the pad sends them as gestures, so the DAW can record them as automation.

### LFO (LFO 1)

| Control | What it does in this engine | Range / default |
|---|---|---|
| On (header) | Turns the LFO on. | Off / On / Off |
| Target (cable in Mod Patch) | PITCH (vibrato) or AMP (tremolo); the theremin has no filter, so CUTOFF is not shown. | PITCH, AMP / PITCH |
| Rate | LFO speed. | 0.02-100 Hz / 5.00 Hz |
| Depth | Physical: vibrato moves the pitch hand by up to +/-1.5 cm. Ideal: up to +/-2 semitones. Tremolo: up to full depth. | 0-100 % / 30 % |

Shape and Sync are shown in the section, but the theremin's LFO is a sine at Rate in Hz (`Theremin.h`, `nextLfo`).

## Playing it

**Pad.** Open it with the **PAD** tab (right-click: Docked, Hidden or Floating window). Click and drag to play; release
the mouse and the volume goes to 0, as when the hand leaves the volume antenna. X runs from 60 cm from the pitch
antenna (left edge) to 2 cm (right edge). The grid marks where each semitone sounds with the active model, including the
pitch drop caused by locking; the shaded zone is the locking zone (silence).

- Ideal: frequency = C3 x 2^(Range x X), with Glide applied in octaves; volume = Y squared.
- Physical: the top of the pad (2 cm) is calibrated to C3 x 2^Range; the rest follows the field, so semitones get
  narrower towards the antenna. Volume follows a saturating curve with 20 ms of inertia.

**MIDI and the on-screen keyboard.** Notes also play the theremin: monophonic, last note wins, velocity sets the volume.
Notes outside C3 .. C3 + Range octaves are moved by octaves into that range. The hand keeps its inertia (Glide), and in
Physical mode the note is played at the distance that gives that beat, locking included, so the sound does not change:
only the source of pitch and volume does. Touching the pad gives control back to the pad.

**Pitch bend** acts only in the Ideal model.

Recipe, classic vibrato lead:
1. Load **Etheric Theremin** (LFO 1 on, Vibrato, 5.8 Hz, depth 10 %).
2. Drag on the pad with the volume half way up; move X slowly and let Glide smooth the steps.
3. Raise Lock to hear the growl as you approach the zero-beat side of the pad.

Recipe, keyboard-playable theremin:
1. Set Model to **Ideal** and Tone to 0 % (as the **Electro-Theremin** preset does).
2. Play from a MIDI keyboard; Glide sets the slide between notes.

## Faithful details

- **Non-linear field.** The variable oscillator follows f0·sqrt(C0 / (C0 + Ch(d))) with the hand capacitance
  Ch(d) = Ck / (d + d0); Field is d0. f0 = 260 kHz, C0 = 20 pF (Etherwave order of magnitude, estimated).
- **Locking (Adler equation).** When the beat is smaller than Lock, the oscillators lock and the output is silent (DC,
  removed by a 20 Hz high-pass). Just above, the pitch falls to sqrt(beat² - Lock²) and the wave becomes pulsing: the
  low growl.
- **Past zero beat.** If Zero is inside the pad range (less than 60 cm), moving further left than the zero-beat point
  makes the pitch rise again (preset comment for **Sci-Fi Saucer** in `Factory.h`). With Zero above 60 cm the zero-beat
  point lies beyond the left edge of the pad.
- **Drift.** Physical: the reference oscillator wanders by +/-20 ppm rms at Drift 100 %. Ideal: up to 4 cents.
- **Tone** is a tanh waveshaper, tanh(d·s)/tanh(d) with d = 0.1 to 6.

Optional "nicer" setting: **Model = Ideal** (linear tuning, no locking, no field compression).

## Factory presets

| Preset | Category | Notes from the preset values |
|---|---|---|
| Etheric Theremin | Machines | Physical, Range 3, Lock 10 Hz, LFO vibrato, tape echo and plate reverb |
| Sci-Fi Saucer | Machines | Physical, very non-linear field (4 cm), wide lock (35 Hz), zero beat at 48 cm, tube, long echo |
| Electro-Theremin | Machines | Ideal model, pure sine (Tone 0 %), Glide 150 ms |

## Limitations

- Monophonic.
- The field constants are estimated, not measured; the volume response is phenomenological, not a circuit model.
- The LFO Filter target does not apply, and the LFO is always a sine (Shape and Sync are not read by this engine).
- Pitch bend works only in the Ideal model.
