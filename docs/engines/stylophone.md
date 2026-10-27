# Stylophone

The Stylophone engine emulates the Dubreq Stylophone (1968, white-module version): a monophonic instrument with 20
metal keys played with a stylus, built around a PUT relaxation oscillator. It reproduces the three original models
(Standard, Bass, Treble), the vintage tuning of the resistor chain, the vibrato switch, the built-in speaker and the
line output. Sources: Simon Inns (waitingforfriday.com/?p=334 and github.com/simoninns/stylophone-1968/wiki). A second,
**experimental** model emulates the Dubreq Gen X-2 (2025), with its controls taken from the official manual and ranges
estimated. Use it for the buzzy stylus sound of the original, or for the Gen X-2's bass, lead and bleep sounds.

As with every emulated engine in KoniSynth, the defaults reproduce the original behaviour, quirks included. Anything
that makes it "nicer" than the hardware is an option and is off by default.

## What you need

Nothing extra.

## Controls

Select **Stylophone** in the Engine menu. Choosing it from that menu loads the preset **Stylophone**. The engine shows
the **Stylophone** section; with Model = Gen X-2 two more sections appear: **Gen X-2 Osc / Envelope (experimental)**
and **Gen X-2 LFO / Filter / Delay**.

### Stylophone

| Control | What it does | Range / default |
|---|---|---|
| Model menu (header) | 1968: the original (relaxation oscillator, vintage tuning, vibrato switch). Gen X-2: Dubreq's 2025 analogue synth, EXPERIMENTAL. | 1968, Gen X-2 (experimental) / **1968** |
| Vibrato (switch) | Vibrato switch, about 7 Hz as measured on the original (1968 model). | Off / On / Off |
| Range | Original (20 keys): notes outside the model's 20 keys are moved by octaves into its range. Full keyboard: every key plays its own note, keeping the vintage tuning of the matching original key. | Original (20 keys), Full keyboard / **Original** |
| Register | 1968 model: which of the three originals. With Range = Original it sets the playable range; it always sets the attack (Treble fastest). | Standard (A2-E4), Bass (A1-E3), Treble (A3-E5) / Standard |
| Depth | Vibrato depth in cents (not documented for the original; 30 ct is an estimate). | 0-100 ct / 30 ct |
| Tuning | Vintage: the original resistor chain, about 33 cents stretched over the 20 keys (low keys flat). Equal: in tune. | Vintage, Equal / **Vintage** |
| Output | Speaker: small built-in speaker (bright, no bass). Line: line output, darkened by its 154 Hz low-pass. | Speaker, Line / **Speaker** |
| Click | Stylus click when it touches a key. | 0-100 % / 30 % |

### Gen X-2 Osc / Envelope (experimental)

The display shows the attack-decay envelope that moves the pitch and the filter, drawn with the engine's own formulas; drag its points to set the times and levels (right-click a point: MIDI Learn, Set value, Reset; double-click: default).

| Control | What it does | Range / default |
|---|---|---|
| X (PWM) (switch) | The LFO sweeps the pulse width (50 % +/- 40 %). | Off / On / Off |
| -1 | Adds a square one octave below. | Off / On / Off |
| -2 | Adds a square two octaves below. | Off / On / Off |
| Octave | OCTAVE switch: transposes by one octave. | -1, 0, +1 / 0 |
| Attack | How fast the filter (and the pitch, if Pitch is up) opens. Range estimated. | 1 ms-2 s / 1 ms |
| Pitch | Envelope PITCH dial: at zero the envelope only moves the filter; turned up, it also moves the pitch. | 0-24 st / 0 st |
| Decay | How fast the filter closes again. Off = the filter stays open (sustain). Range estimated. | Off (sustain)-4 s / Off |
| Strip | Expression strip mode: Filter = wah from the centre, Osc = pitch bend, LFO = vibrato amount (right half only). | Filter, Osc, LFO / Osc |

### Gen X-2 LFO / Filter / Delay

| Control | What it does | Range / default |
|---|---|---|
| Delay (switch) | Analogue delay on/off. | Off / On / Off |
| Wave | LFO wave. | Triangle, Square / Triangle |
| Rate | LFO rate. Range estimated. | 0.1-30 Hz / 5 Hz |
| Depth | LFO depth: modulates pitch (up to +/-1 semitone) and filter cutoff (up to +/-1 octave) at the same time. | 0-100 % / 0 % |
| Cutoff | Resonant low-pass filter cutoff. | 30 Hz-12 kHz / 12 kHz |
| Res | Filter resonance. | 0-100 % / 0 % |
| Time | Delay time. Range estimated. | 40-400 ms / 200 ms |
| Repeats | Delay feedback; each repeat gets darker (estimated). | 0-90 % / 40 % |
| Level | Amount of echo. | 0-100 % / 40 % |

## Playing it

**Pad.** Open it with the **PAD** tab (right-click: Docked, Hidden or Floating window). Play it with the mouse like the
stylus: press to touch a plate, drag to slide from plate to plate (with the click of each contact), release to lift the
stylus. 1968: one row of 20 plates from the lowest note of the register. Gen X-2: two piano-like rows (25 notes from C3
+ OCTAVE) with the expression strip above it.

**MIDI and keyboard.** Monophonic. If several keys are held, the highest sounds (the stylus shorts the chain up to the
touched tap; inferred, not documented). With Range = Original, notes outside the 20 keys of the register are folded by
octaves into it; with Full keyboard the MIDI plays the whole keyboard and the pad shows the register window. Lifting the
stylus releases with the last pitch. The **pitch-bend wheel** bends the 1968 model and acts as the Gen X-2 strip.

Ranges of the 1968 registers: Standard A2-E4, Bass A1-E3, Treble A3-E5. The Gen X-2 plays C3-C5 (25 keys, estimated),
shifted by OCTAVE.

Recipe, original sound:
1. Load **Stylophone** (1968, Standard, Speaker).
2. Drag across the plates on the pad; switch Vibrato on for the classic wobble.
3. For a recording-desk sound, load **Stylophone Line Out** (Line output, Click 15 %).

Recipe, Gen X-2 bass:
1. Load **Gen X-2 Sub Bass** (-1 and -2 on, Cutoff 600 Hz, Res 30 %, Decay 0.6 s).
2. Play low notes; set Strip = Filter and use the bend wheel as a wah.

## Faithful details

1968 model (verified in the sources named in `Stylophone.h`):
- **PUT relaxation oscillator**: exponential charge to η·Vs and instant discharge (shark-fin wave), η = 0.55, the centre
  of the trimmer range 0.48-0.62.
- **Three separate models, no octave switch**: Standard A2-E4, Bass A1-E3, Treble A3-E5.
- **Vintage tuning**: series resistor chain with steps of about 1.060 (ideal 1.0595): +33 cents accumulated from key 0
  to key 19, from -26 ct (key 0) to +6.9 ct (key 19), referred to key 12 (A3 on the Standard).
- **Vibrato** by an RC phase-shift network, measured at about 7 Hz.
- **Line output** with a 154 Hz passive low-pass.
- **Attack per register**: faster on the Treble (47 nF capacitor against 100 nF / 200 nF).

Estimated (no published data): vibrato depth, attack and release (base attack 5 ms, release 3 ms), the 75 ohm speaker
response (modelled as a soft-saturating one-transistor amp, 2-pole high-pass at 250 Hz and 1-pole low-pass at 5 kHz),
note priority, and the shape and level of the stylus click.

Gen X-2 (experimental), from the official manual ("STYLOPHONE-GENX-2-A3-Instructions.pdf"): two-octave piano keyboard,
OCTAVE -1/0/+1, X = PWM moved by the LFO, -1/-2 sub-octaves, ATTACK/DECAY on the filter and, with PITCH > 0, on the
pitch ("no attack or decay = sustained with the filter open"), square or triangle LFO on pitch and cutoff at once,
resonant low-pass, analogue delay, and the FILT/OSC (bipolar from the centre) or LFO (right half only) strip. Estimated:
the ranges of every control, the pulse oscillator, sub-octaves by division (0.8 of the level), a 2-pole SVF with input
saturation, a BBD-style delay (3.5 kHz low-pass in the loop), keys C3-C5 and the strip mapped to the pitch-bend wheel.
Each stylus contact retriggers the Gen X-2 envelope from its current level.

Optional "nicer" settings (all off by default): **Tuning = Equal**, **Range = Full keyboard**, and **Click** down to
0 %. **Output = Line** is not a beautification: it is the original's own line output.

## Factory presets

| Preset | Category | Model / notes from the preset values |
|---|---|---|
| Stylophone | Machines | 1968, defaults (speaker) |
| Stylophone Line Out | Machines | 1968, Line output, Click 15 % |
| Gen X-2 Sub Bass | Machines | Gen X-2, -1 and -2, Cutoff 600 Hz, Res 30 %, Decay 0.6 s |
| Gen X-2 PWM Lead | Machines | Gen X-2, PWM, LFO 3 Hz, Cutoff 4 kHz, delay 280 ms |
| Gen X-2 Bleeps | Machines | Gen X-2, Pitch 12 st, Decay 0.15 s, square LFO 8 Hz, delay 120 ms |

## Limitations

- Monophonic.
- The Gen X-2 model is experimental: its controls follow the official manual, but the manual gives no figures, so all
  ranges are estimated.
- Vibrato, Depth, Tuning and Register apply to the 1968 model only (the Gen X-2 uses its own oscillator and OCTAVE).
- The speaker response and several timings of the 1968 model are estimates (listed above).
