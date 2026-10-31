# Synth

The Synth is KoniSynth's own eight-voice polyphonic synthesizer (also Mono and Legato), with Moog traits: the 4-pole
ladder low-pass (LP 24) with three qualities against aliasing, the Minimoog Tri-Saw and an analog triangle measured on two
Model D units. It does not emulate a specific instrument: it is a subtractive design with its own personality, combining
four summed sources (a minBLEP oscillator with unison, a wavetable, a
sample player and Math Models, physical models such as plucked and bowed strings, winds, bars and shakers), a Buchla 259-style complex oscillator, a second oscillator
(classic and mixed waves, its own wavetable or its own sample file), a third oscillator that grows from the classic sub,
noise (white, pink and the 8-bit LFSR), 14 filter modes, per-voice saturation before or after the filter, analog RC or
linear envelopes, two tempo-synced LFOs, an 8-slot modulation matrix (11 sources, 15 destinations), MPE and polyphonic
aftertouch, and an analog-style drift model with per-note or per-voice-card tolerances. Unison runs on AVX2 where the
processor has it. Use it for basses, leads, pads, keys, plucks, sound effects and anything the emulated engines do not cover.

Unlike the emulated engines, the Synth is not bound to an original. Its analog behaviours (drift, filter input
saturation, VCA click and bleed, RC envelope curves) are part of its design; several of them are off or at a low
setting by default.

## What you need

Nothing extra. Optional: an audio file for the **Sample** source (KoniSynth includes no samples) and a wavetable file
for the **User File** wavetable.

## Controls

The Synth fills three rows: sources -> Complex Oscillator -> Osc 2 / Osc 3 / Noise; Filter, Filter Envelope, Amp Envelope;
Voice, LFO, LFO 2. Voice, LFO 2 and the matrix act only in the Synth.

The **SIGNAL** strip above the sources draws the path the sound takes right now: the sources (with **Complex** attached to
Osc 1 and its modulator when the complex oscillator is on, marked *LP* when Mod LP filters it), the mixer, filter and
saturation in their current order, the amp, the effects (their order is in the FX tooltip) and the output. LEDs show what
sounds or is on; on the right, the active modulations. Clicking a source opens its tab and its view on the wave display.

### Source tabs: Osc 1, Wavetable, Sample, Math Models

The four sources sound summed; each tab has its own level and LED. Right-click a tab for **Mute / Solo** (Mute
silences the source without changing its level; mutes are off by default and presets do not change them).

**Osc 1**

| Control | What it does | Range / default |
|---|---|---|
| Wave menu | Oscillator 1 waveform: sine pure, triangle soft, saw bright, square hollow, Tri-Saw and the Saw/Tri/Pulse mixes (see below). | Sine, Triangle, Saw, Square, Tri-Saw, Saw+Pulse, Saw+Tri, Tri+Pulse, Saw+Tri+Pulse, Saw+Pulse (Juno) / Sine |
| Level | Oscillator 1 volume in the mixer. | 0-100 % / 100 % |
| Voices | Oscillators per note (unison): wider and thicker, more CPU. | 1-7 / 1 |
| Detune | Detune between the unison oscillators. | 0-50 ct / 12 ct |
| Spread | Stereo width of the unison oscillators. | 0-100 % / 50 % |
| PW | Square pulse width: 50 % hollow, extremes thin and nasal. | 5-95 % / 50 % |
| PWM | How much the LFO moves the pulse width. | 0-45 % / 0 % |
| Drift | Analog instability: slow random detuning (see Drift below). | 0-100 % / 20 % |
| P.Env | Pitch drop or rise at the start of each note: kicks, toms, laser zaps. | -48 to +48 st / 0 st |
| P.Decay | How fast the pitch envelope returns to the note's pitch. | 1-2000 ms / 50 ms |

The small display at the left of the Osc 1 knobs draws the pitch envelope (scale ±48 st, drawn to -40 dB of its decay):
drag the first point up or down for P.Env and the end point left or right for P.Decay.
| Tri Sym | Triangle waveform only: makes the triangle slightly lopsided like an analog one (even harmonics; see Analog triangle below). | 0-100 % / 0 % |
| Tri Rnd | Triangle waveform only: rounds its tips (weaker odd harmonics); 100 % is a sine. | 0-100 % / 0 % |

**Wavetable** (Edit Table... opens the wavetable editor: the frames in perspective, the table menu, Level, Position and a
**Load** button for a WAV with 2048 samples per frame, the Serum format, or a single cycle. A WAV that declares another
frame size in the `clm ` chunk written by Serum (`<!>1024 ...`) is read with that size and each frame is resampled to 2048;
a stereo file is mixed to mono)

| Control | What it does | Range / default |
|---|---|---|
| Table menu | Built-in tables or your own file. | Basic Shapes, PWM Sweep, Harmonic Sweep, Sync Sweep, Formant Sweep, User File / Basic Shapes |
| Level | Wavetable volume in the mixer. | 0-100 % / 0 % |
| Position | Sweeps from the first to the last waveform; LFO 2 can move it. | 0-100 % / 0 % |

**Sample** (Edit Sample... opens the sample editor: waveform with a draggable Start, the end of Length and the loop zone;
Loop, Level, Root, Start, Length and a **Load** button. The file chooser accepts WAV, AIFF, FLAC, OGG, SoundFont 2
(.sf2), SNES BRR (.brr) and SNES APU snapshots (.spc); for .sf2 and .spc you choose one item inside the bank)

| Control | What it does | Range / default |
|---|---|---|
| Level | Sample volume; it goes through the filter and envelopes like an oscillator. | 0-100 % / 0 % |
| Root | Note at which the sample plays at its original pitch. | C-1 to G9 / C4 |
| Start | Playback start point. | 0-99 % / 0 % |
| Length | Cuts the sample after this time with a short fade; short values give LA-style synthesis. | Full, up to 2000 ms / Full |
| Loop | Loops from Start to the end while the note is held. | Off / On / Off |

**Math Models** computes a physical or stochastic model for every note (strings, modes, winds, bars, shakers and a
mallet); the panel shows only the chosen model's controls, and Formula... shows its formula.

| Model | Published model | Own controls |
|---|---|---|
| Noise Burst | Karplus & Strong 1983; Jaffe & Smith 1983 | Decay, Bright, Position, Damping, Pickup, Buzz, Harmonic, Stiff, Tension |
| Pick | Jaffe & Smith 1983 | as Noise Burst |
| Bow | McIntyre, Schumacher & Woodhouse 1983; Smith 1986 | Decay, Bright, Position, Pickup, Press, Harmonic, Stiff, Tension |
| Hammer | Hall 1992 | as Bow, with Felt |
| E-Bow | after Heet's patent, 1978 (own model) | as Bow, with Drive |
| Modal | Adrien 1991 | Decay |
| Reed | Guillemain, Kergomard & Voinier 2005 | Embouchure, Noise |
| Flute | Karjalainen et al. 1991; Välimäki et al. 1992; STK Flute | Jet, Noise |
| Brass | Cook 1991 (TBone); STK Brass | Lip, Slide |
| Banded | Essl & Cook 1999; STK BandedWG | Decay, Shape, Excite, Press (with Bow) |
| Shaker | Cook 1997 (PhISEM); STK Shakers | Type, Objects, Shake, Tone |
| Mallet | Avanzini & Rocchesso 2001 | Decay, Hardness, Mass, Damp |

Full reference, formulas and history: [Math Models](maths.md)

**Tri-Saw** (all four waveform menus) is the triangle-sawtooth of the Minimoog oscillators 1 and 2: the saw and triangle
mixed passively at the waveform switch, 10/57 saw and 47/57 triangle (47 kΩ and 10 kΩ resistors). Checked against a recorded
Model D: harmonics 2 to 10 within 1.05 dB RMS. Softer than Saw, with a little more edge than Triangle; it aliases like the
triangle (its saw jump is corrected, its corners are not).

**Analog triangle** (Tri Sym and Tri Rnd, one pair per oscillator, Triangle waveform only). Measured on two Minimoog Model D
(raw waveforms at C3): their triangles carry even harmonics (h2 -24/-27 dB, h4 -41/-42 dB) that an ideal triangle lacks,
and their odd harmonics sit 3 to 7 dB lower (h3 -22/-26 dB against -19.1 dB). Tri Rnd blends the triangle with a sine
(rounded tips); Tri Sym adds a squared term (lopsided shape) with no DC offset. The filter input stage already rounds the
tips a little (h3 -22 dB with the ideal triangle), so through the filter Tri Sym 50-60 % and Tri Rnd 0-20 % match harmonics
2 to 8 of the two instruments within 1.4 to 3.0 dB RMS (Sym 60 % / Rnd 0 %: 1.4 dB against one of them). With Filter off,
Tri Rnd 30-50 % does that job. At 0 % both, the triangle is the ideal one.

**Saw+Pulse, Saw+Tri, Tri+Pulse, Saw+Tri+Pulse** (all four menus) are the waveforms a Prophet-5 oscillator plays when
several of its waveform switches are on, in the proportions of its circuit (rev 1/2 service manual, voice schematic):
saw 10 V and pulse 7.5 V peak to peak, averaged through equal 10 kΩ resistors; on oscillator B the triangle (10 V, its
peak at the saw reset) joins through 3.3 kΩ against 10 kΩ for the buffered saw/pulse, so it dominates the mixes that
include it. The pulse is high at the end of the ramp and follows Pulse Width and PWM (Osc 3: 50 %).

| Waveform | Saw | Triangle | Pulse |
|---|---|---|---|
| Saw+Pulse | 0.571 | - | 0.429 |
| Saw+Tri | 0.239 | 0.761 | - |
| Tri+Pulse | - | 0.809 | 0.191 |
| Saw+Tri+Pulse | 0.126 | 0.780 | 0.094 |
| Saw+Pulse (Juno) | 0.5 | - | 0.5 |

**Saw+Pulse (Juno)** is the Juno-106 saw and pulse switched on together. Its mix happens inside the voice chip, so it was
measured on a recording (C1, chorus off): pulse and saw at equal peak level (0.99 ± 0.02), in phase, which lifts the odd
harmonics 9.5 dB above the even ones (8.0 dB in the Prophet-5 mix). The jumps of all the mixes are corrected with minBLEP;
the ones with triangle alias like the triangle (its corners are not corrected).

### Complex Oscillator

Buchla 259-style modulation of Osc 1 by a modulator oscillator. Runs at 2x oversampling. With the Complex on, the OSC 1 view
of the wave display draws its output (the first two cycles of a note: FM, AM / ring, sync and the wavefolder), with the
modulator faint underneath; an Osc 2 modulator in Wavetable or Sample mode is not drawn.

| Control | What it does | Range / default |
|---|---|---|
| On (header) | Turns the complex oscillator on for oscillator 1. | Off / On / Off |
| Mod Src | Which oscillator is the modulator: the internal one (Mod Wave and Ratio) or Osc 2 / Osc 3 exactly as tuned in their tabs (wave, Oct, Semi, Fine, drift, WT Pos, and the sync of Osc 2). With Osc 2 or Osc 3, Mod Wave and Ratio are dimmed. See the notes below. | Internal, Osc 2, Osc 3 / Internal |
| Mod Wave menu | Modulator waveform. | Sine, Triangle, Saw, Square, Tri-Saw, Saw+Pulse, Saw+Tri, Tri+Pulse, Saw+Tri+Pulse, Saw+Pulse (Juno) / Sine |
| Sync (switch) | Hard-syncs the main oscillator to the modulator. | Off / On / Off |
| Ratio | Modulator/main frequency ratio: whole numbers harmonic, others bell-like. | 0.125x-16x / 1.000x |
| FM | Linear through-zero FM amount. | I 0-10 / 0 |
| AM/Ring | Amplitude modulation; 100 % is ring modulation. | 0-100 % / 0 % |
| Timbre | Sine wavefolder: progressively adds harmonics. | 0-100 % / 0 % |
| Symmetry | Wavefolder asymmetry. | -100 to +100 % / 0 % |
| T.Mod | The modulator moves the wavefolder. | 0-100 % / 0 % |
| Mod LP | One-pole low-pass (6 dB/oct) on the modulator (internal, Osc 2 or Osc 3) before the FM and the AM: a saw or square modulator gets rounder, for softer FM. With the internal modulator and 3 or more unison voices it runs without AVX2 (more CPU). | 20 Hz-20 kHz, Off at the top / Off |

Notes on **Mod Src** = Osc 2 or Osc 3:

- The oscillator modulates even at level 0, muted or silenced by another source's solo: turn its level down to hear only
  the modulation, or up to hear it as well. With one unison voice and Drift at 0, a sine Osc 2 one octave up gives
  exactly the same sound as the internal sine at Ratio 2.
- The sync of Osc 2 is followed inside the oversampled step, at the exact reset instant.
- The modulator is the oscillator's waveform evaluated at the doubled rate with a short polynomial correction, like the
  internal modulator; it is not the band-limited (minBLEP) signal you hear from the Osc 2 / Osc 3 output. The resets of
  Osc 2 sync are not corrected in the modulator.
- One modulator serves every unison voice, while the internal one has its own phase and detune in each voice: with
  unison the two do not sound the same. This is by design.
- Switching the source while a note sounds is immediate (no crossfade), so the modulator value jumps: with FM the
  carrier phase stays continuous but its instantaneous frequency jumps; with AM/Ring the amplitude jumps (a click); with
  T.Mod the folding depth jumps (a transient). The internal modulator pauses while Osc 2 or Osc 3 is used and, when you
  switch back to Internal, resumes from the phase where it stopped: repeatable, but not continuous.
- CPU: the same as the internal modulator with the same wave and ratio (measured with unison 7 and 8 notes: 8.68 % with
  Osc 2 saw and sync against 8.70 % with the internal saw at 1.498x).

### Source tabs: Osc 2, Osc 3, Noise

A second group of tabs with the same behaviour: all three sound together with Osc 1, Wavetable, Sample and String; each
tab has its own level, LED and **Mute / Solo** (right-click). Solo is shared by both groups: soloing any source leaves
only that one sounding, and removing the solo brings every source back to its own Mute (solo does not change the mutes
and is not saved). The LED is lit only while the source really sounds (level above 0, not muted, not silenced by
another source's solo).

**Osc 2**

| Control | What it does | Range / default |
|---|---|---|
| Wave menu | Oscillator 2 waveform. Wavetable plays its own table (Osc 2 Table...), independent of the Wavetable tab, with the tuning, drift and phase of Osc 2 and its own position (WT Pos). Sync works with it too (with bright positions it aliases more than Saw or Square: like Sine and Triangle, the reset leaves a corner that is not corrected). Sample plays its own audio file: see **Osc 2 Sample** below. | Sine, Triangle, Saw, Square, Tri-Saw, Saw+Pulse, Saw+Tri, Tri+Pulse, Saw+Tri+Pulse, Saw+Pulse (Juno), Wavetable, Sample / Saw |
| Osc 2 Sample... (header) | Opens the sample editor of Osc 2 (Sample waveform only; dimmed otherwise). | - |
| Osc 2 Table... (header) | Opens the wavetable editor of Osc 2: its own table (built-in or your file, Load), Level and Position (Wavetable waveform only; dimmed otherwise). | - |
| Sync (switch) | Hard-syncs oscillator 2 to oscillator 1: raise its tuning for the sync lead. | Off / On / Off |
| Level | Oscillator 2 level. | 0-100 % / 0 % |
| Oct | Octave relative to oscillator 1. | -2 to +2 / 0 |
| Semi | Offset in semitones (7 = fifth, 12 = octave). | -12 to +12 / 0 |
| Fine | Fine detune; a few cents give beating. | -50 to +50 ct / 0 ct |
| WT Pos | Position of Osc 2 in its own wavetable (Wavetable waveform only). | 0-100 % / 0 % |
| Tri Sym / Tri Rnd | Analog triangle of Osc 2 (Triangle waveform only), as in Oscillator 1. | 0-100 % / 0 % |

**Osc 2 Sample** (Wave = Sample). A second sample player with its own file, independent of the Sample tab: two different
files can sound at once. It uses the Level, Oct, Semi, Fine, drift and Mute of Osc 2; Osc 2 Sample... opens the same sample
editor as the Sample tab (waveform, Load, Clear, Level, Root, Start, Length, Loop), working on the Osc 2 file.

| Control | What it does | Range / default |
|---|---|---|
| Root (editor) | Note at which the file plays at its original pitch, before Oct, Semi and Fine. | C-1 to G9 / C4 |
| Start (editor) | Playback start point; also where Sync restarts it. | 0-99 % / 0 % |
| Length (editor) | Cuts the file after this time (counted from the note) with a short fade. | Full, up to 2000 ms / Full |
| Loop (editor) | Loops from Start to the end (or the file's own loop, if it has one) while the note is held. | Off / On / Off |

- **Sync** restarts the file from Start at every cycle of Osc 1, at the exact instant: a long sample may never play to
  the end, it is reread from its beginning every cycle (a pitched buzz with the colour of its first milliseconds). This
  is deliberate. It also brings back a file that has reached its end, but not one cut by Length. The jump of each
  restart is corrected; the corner left by the change of slope is not, as with the Wavetable waveform. Measured with a
  harmonic test file (Oct +1, Semi +5, loop on), energy outside the harmonics of the note: C4 -49.0 dB at 44.1 kHz and
  -53.8 dB at 48 kHz (without the correction: -33.7 and -34.4 dB; a synced Saw: -103.8 and -111.8 dB).
- As the **Complex Mod Source** (Osc 2), the file itself modulates oscillator 1, read at the doubled rate with the
  same cursor that plays it (it keeps moving at level 0 or muted, and follows the end, the loop and Sync). FM Index is
  scaled by the nominal frequency of Osc 2 (the note with Oct, Semi and Fine), not by any pitch found in the file; Mod
  Sync follows that nominal frequency too. A file without a clear pitch gives inharmonic, noisy FM: the 2x step does
  not remove every alias with such a modulator.
- The decimated level of the file (anti-aliasing for upward transposition) is chosen when the note starts, with the
  tuning of Osc 2 at that moment and a margin of +1 semitone; raising Oct or Semi during the note can alias.
- Loading another file while a note sounds silences that note's Osc 2 (it never reads a file being replaced).
- CPU (8 notes, unison 7): Osc 2 Sample 3.98 % of a core against 4.35 % for Osc 2 Saw; as the Complex modulator,
  8.97 % against 8.62 %.
- Presets save the path of the Osc 2 file (preset format 3). Presets without it (older ones and factory presets) load
  with no Osc 2 file, so a preset never inherits the file of the previous one. A missing file asks to Locate it, as
  with the Sample tab.

**Osc 3** (grows from the classic sub-oscillator and follows Osc 1)

| Control | What it does | Range / default |
|---|---|---|
| Wave menu | Waveform; the square and the pulse of the mixes stay at 50 % (they do not follow PW). By default it is the classic sub (square one octave down); the Init preset sets Sine, and presets saved before Osc 3 keep the Square. | Sine, Triangle, Saw, Square, Tri-Saw, Saw+Pulse, Saw+Tri, Tri+Pulse, Saw+Tri+Pulse, Saw+Pulse (Juno) / Square |
| Level | Oscillator 3 level. | 0-100 % / 0 % |
| Oct | Base octave below Osc 1. | -1 oct, -2 oct / -1 oct |
| Semi | Semitones on top of the base octave: from -1 oct, +12 = same octave as Osc 1 and +24 = one octave up. | -24 to +24 / 0 |
| Fine | Fine detune. | -50 to +50 ct / 0 ct |
| Tri Sym / Tri Rnd | Analog triangle of Osc 3 (Triangle waveform only), as in Oscillator 1. | 0-100 % / 0 % |

**Noise**

| Control | What it does | Range / default |
|---|---|---|
| Color menu | White, pink, or LFSR digital noise of 8-bit chips (Short = metallic, tonal, follows the key). | White, Pink, LFSR, LFSR Short / White |
| Level | Noise level. | 0-100 % / 0 % |

### Filter

The **Saturation...** header button opens the **Saturation** dialog: *Filter Input* (Clean Drive switch and Drive) and *Voice
Saturation* (curve menu, Drive and Position, with the transfer curve of the chosen curve and drive: drag its point for the
drive). The **Ladder quality** selector next to it chooses how the ladder modes are processed; see
*Ladder quality* below.

| Control | What it does | Range / default |
|---|---|---|
| On (header) | Turns the filter on. | Off / On / **On** |
| Mode menu | Filter model and response (14 modes, below). | / LP 24 |
| Cutoff | Cutoff frequency. | 20 Hz-20 kHz / 20 kHz |
| Reso | Emphasis at the cutoff; at maximum the filter self-oscillates. | 0-100 % / 0 % |
| Drive | Saturation at the filter input. | 0-100 % / 0 % |
| Env | How far the filter envelope moves the cutoff (+/-5 octaves at +/-100 %). | -100 to +100 % / 0 % |
| Key | Keyboard tracking; 100 % follows note for note (reference C4). | 0-100 % / 0 % |
| Vel | Playing harder opens the filter more. | 0-100 % / 0 % |
| Spread | Distance between the two filters of the Dual modes, centred on Cutoff. | 0-4 oct / 1.00 oct |
| Sat | Per-voice saturation drive (same parameter as in Saturation...). | Off-36 dB / Off |
| Clean Drive (Saturation...) | Off: the input stage always saturates a little, even at Drive 0 %. On: Drive 0 % is linear and saturation fades in over the first 25 % of Drive. | Off / On / **Off** |
| Voice Sat curve (Saturation...) | Soft (tanh, overdriven OTA), Diode (asymmetric, even harmonics), Hard (clipping), Fold (sine wavefolder). | Soft, Diode, Hard, Fold / Soft |
| Position (Saturation...) | Where the per-voice saturation sits: after the filter (it adds harmonics to the filtered sound) or before it (drive into the filter, which smooths what it adds). | Post-filter, Pre-filter / Post-filter |

| Mode | Description (item tooltip) |
|---|---|
| LP 24 | Ladder, 4 poles (24 dB/oct): the classic Moog low-pass. |
| LP 12 | Ladder, 2 poles (12 dB/oct). |
| BP 12 / HP 12 | Ladder band-pass / high-pass. |
| SVF LP / BP / HP / Notch | State-variable (Oberheim SEM style), 12 dB/oct, smooth; Notch removes a band. |
| MS-20 LP | Korg35 low-pass of the MS-20: biting resonance that screams. |
| MS-20 HP | Korg35 high-pass of the MS-20 (6 dB/oct, like the original). |
| Comb + | Comb resonator: all harmonics of Cutoff (metallic, plucked). Key 100 % to play in tune; Reso = feedback. |
| Comb - | Negative feedback: odd harmonics only (hollow, like a closed pipe). |
| Dual BP | Two band-pass filters in parallel, Spread apart: vowel-like formants. |
| Dual LP | Two low-pass filters in series, Spread apart. |

### Filter Envelope and Amp Envelope

| Control | What it does | Range / default |
|---|---|---|
| Filter A / D / S / R | Filter envelope attack, decay, sustain level, release. | A 0.2 ms-5 s / 5 ms; D 0.2 ms-20 s / 300 ms; S 0-100 % / 0 %; R 0.2 ms-20 s / 300 ms |
| Curve menu (Amp header) | Shape of both envelopes. Linear: straight ramps. Analog RC: CEM3310 curves (attack bends into the peak, decay and release fall fast then slow). | Linear, Analog RC / Linear |
| Amp A / D / S / R | Volume attack, decay to Sustain, sustain level, release. | A 0.2 ms-5 s / 10 ms; D 0.2 ms-20 s / 100 ms; S 0-100 % / 80 %; R 0.2 ms-20 s / 300 ms |
| V.Pan | Spreads notes across the stereo field; each new note gets its own position. 0 = all centred. | 0-100 % / 0 % |
| Pan Mode | Alternate (L, R, L...), Keyboard (C2 left .. C6 right) or Random. | Alternate, Keyboard, Random / Alternate |

### Voice

| Control | What it does | Range / default |
|---|---|---|
| Mode menu (header) | Poly: chords (8 voices). Mono: one note, last key wins. Legato: like Mono, overlapping notes slide without restarting the envelopes. | Poly, Mono, Legato / Poly |
| Glide | Portamento from the last note played (any voice); 0 = off. | 0-2000 ms / 0 ms |
| Mod Wheel | Mod wheel (CC 1) depth: vibrato up to +/-1 semitone at the LFO 1 rate; works even with LFO 1 off. | 0-100 % / 50 % |

**Voice...** opens the **Synth Voice** dialog:

| Group | Control | What it does | Range / default |
|---|---|---|---|
| Aftertouch | AT Filt | Pressure opens the filter (up to +3 octaves). | 0-100 % / 0 % |
| | AT Vib | Pressure adds vibrato (up to +/-1 st at the LFO 1 rate). | 0-100 % / 0 % |
| | AT Vol | Pressure makes the note louder (up to +6 dB), like the CS-80. | 0-100 % / 0 % |
| MPE | MPE switch | Lower zone: channel 1 master, channels 2-16 one note each with its own bend, pressure and CC74 timbre. | Off / On / Off |
| | Bend | Per-note pitch bend range in MPE mode (MPE standard default 48). | 0-96 st / 48 |
| Voice Assign | menu | Lowest Free: first idle voice. Reuse Same Note: a repeated key retriggers its own voice (Prophet-5). Round Robin: every note moves to the next voice (Prophet-5 Rev 4 option). | / Lowest Free |
| Osc Phase | menu | Auto: random if Drift is up, else from zero. Reset: always from zero. Free Running: oscillators keep running between notes. Random: random phase per note. | / Auto |
| Tolerances | menu | Where the Drift tolerances live. Per Note: new values each note. Per Voice Card: fixed per physical voice. | / Per Note |
| Analog VCA | Click | Control feedthrough: a thump at each note (strongest with fast attacks). 4 % = worst case of a modern AS3360; 100 % = untrimmed vintage VCA. | 0-100 % / 0 % |
| | Leak | The VCA never closes: after the release each voice keeps sounding at this level until reused. Uses CPU. | Off, -100 to -30 dB / Off |

Aftertouch uses channel or polyphonic pressure, whichever is larger. MIDI channel, fine tune, bend range (0-24 st,
default 2) and controller profiles are global: the MIDI button left of the engine selector.

### LFO (LFO 1) and LFO 2

Both LFOs run per voice and restart at each note.

| Control | LFO 1 | LFO 2 |
|---|---|---|
| On (header) | Off / On, default Off | Off / On, default Off |
| Target menu | Vibrato, Tremolo, Filter / Vibrato | Vibrato, Tremolo, Filter, PWM, Osc 2 Pitch, WT Position / Filter |
| Rate | 0.02-100 Hz / 5.00 Hz (also the speed of mod wheel and aftertouch vibrato) | 0.02-100 Hz / 0.50 Hz |
| Depth | 0-100 % / 30 % | 0-100 % / 30 % |
| Shape | Sine, Triangle, Saw, Square, S&H (new random value each cycle), Random (smooth glide between random values) / Sine | same / Sine |
| Sync | Free (Rate in Hz) or 4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 of the host tempo (Arp BPM in the standalone) / Free | same / Free |

Reach at Depth 100 %: vibrato +/-2 st, tremolo full depth, filter +/-2 octaves; LFO 2 also PWM +/-45 % of width and
Osc 2 Pitch +/-2 st. LFO 1 also drives the PWM knob of Osc 1.

### Modulation Matrix (Matrix... in the LFO 2 header)

8 slots, each a **source** moving a **destination** for each voice separately, by an **amount** of -100 to +100 %
(negative inverts). The number in brackets on the button counts the active routes; amber rings on the knobs show
their modulation range.

| Sources | Destinations (reach at 100 %) |
|---|---|
| LFO 1, LFO 2, Filter Env, Amp Env, Velocity, Key Track, Mod Wheel, Aftertouch, MPE Timbre, Random (one value per note; fixed during the note, or fading with Rnd Fade), Pitch Bend | Pitch and Osc 2 Pitch (+/-24 st), Cutoff (+/-5 oct), Resonance (full range), Drive, Pulse Width (+/-0.45), Osc 1 Level, Osc 2 Level, Noise Level, Volume, Pan, FM Index (+/-5), Timbre, WT Position, Osc 2 WT Position |

**Rnd Fade** (bottom of the dialog, default 0 %) makes the Random source fade with the filter envelope: the slot gets
random × (1 − fade + fade × filter envelope). At 100 % with filter sustain 0, each note starts off target and settles
on it during the filter decay. Uses: Random → Pitch at 2-5 % for a scoop into every note (leads, brass), at 30-50 % for
a chord that converges from random pitches (Convergence Swell); Random → Cutoff or Pan for plucks whose attacks vary
and settle (Restless Pluck). It applies to every slot that uses Random.

## Playing it

- **Poly** plays up to 8 voices; **Mono** and **Legato** use a single voice. Unison (Voices) multiplies oscillators
  inside each voice.
- In **Mono** and **Legato**, a note that arrives while the previous one is still in its release (notes that do not
  overlap, such as a glissando dragged across the keys) retriggers the envelopes from their current level, with filter
  and oscillators still running, as on an analog monosynth; the velocity level moves to the new note in 5 ms. There is
  no click.
- **Glide** starts from the last note played, in any voice, and slides exponentially in semitones.
- **Pitch bend** follows the global Bend Range; in MPE mode each note uses MPE Bend.

Recipe, sync lead:
1. Osc 2 tab: Level at 80 %, Sync on, Semi +7 to +12; Osc 1 Level down.
2. LFO 2 or the matrix (Mod Wheel -> Osc 2 Pitch) to sweep the sync.

Recipe, plucked harpsichord:
1. Math Models tab: Pick, Level 100 %, Decay 4 s, Bright 90 %, Position 10 %; Osc 1 Level 0 %.
2. Amp envelope: Attack 1 ms, Sustain 100 %, Release 0.25 s. These are the values of the factory preset
   **Baroque Harpsichord**; try Excite = Pick for the triangle-shaped pluck.

Recipe, LA-style attack:
1. Sample tab: load a file, Level up, Length 50-150 ms (the sampled transient).
2. Osc 1 or Wavetable for the sustained part.

## Analog behaviour of the Synth

These are the Synth's own design choices, not an emulation of a particular unit.

- **Drift** (default 20 %). At 100 %: static detune +/-3 ct and cutoff tolerance +/-10 % per voice; Osc 2 fixed offset
  +/-2 ct from Osc 1; slow 1/f drift per voice (σ 3 ct), common to all voices of the instance (σ 2 ct) and per Osc 2
  (σ 2 ct); a thermal scale error of +/-0.52 ct per octave from C4 (residual after the AS3310 exponential converter's
  compensation: estimated, not measured). At the default 20 %, voice + common drift = 0.72 ct (σ). With Drift up, the
  oscillators start at a random phase (Osc Phase = Auto).
- **Tolerances = Per Voice Card** fixes those values per physical voice, like the voice cards of a vintage polysynth;
  with Round Robin each note then sounds slightly different.
- **Filter input saturation.** By default the input stage always saturates a little (3rd harmonic of a full-scale sine
  at about -29 dB). Clean Drive makes Drive 0 % linear for the SVF, comb and dual modes; the ladder and MS-20 keep
  their core saturation (about -34 dB). The ladder self-oscillates from Reso of about 91 %.
- **Ladder quality** (Filter header > Ladder quality; LP 24, LP 12, BP 12 and HP 12 only; the other modes are not affected:
  with them the selector is dimmed but can still be set).
  With Drive, high notes fold harmonics back below 20 kHz as inharmonic tones (aliasing). Measured at 48 kHz, sine
  with Drive 100 %, Reso 80 %, energy of the folded tones relative to the harmonics:

  | Mode | E7 (2637 Hz) | A7 (3520 Hz) | CPU, busiest Synth benchmark (one core) |
  |---|---|---|---|
  | **Classic** (default): the original filter | -16 dB | -18 dB | 9.6 % |
  | **Balanced**: 2x oversampling, antialiased drive | -43 dB | -51 dB | 13.7 % |
  | **High Quality**: 4x oversampling | -88 dB | -62 dB | 20.0 % |

  Each mode has its own tone, not only less aliasing: the oversampled modes let more treble through above the cutoff
  (LP 24 at 8 kHz: about +3 dB at 12 kHz and +12 dB at 16 kHz with High Quality), and Balanced softens the very top a
  little (about -2 dB at 20 kHz for small signals). Tuning, self-oscillation and the harmonics below the aliasing stay
  the same. The oversampled modes delay the filtered sound by 0.1-0.3 ms (more in the treble); this is not reported
  to the host as latency.

  The setting belongs to the project, like the tuning: it is saved with it, and loading a preset does not change it.
  All voices always use the same mode, so a change waits until every voice is silent (the selector shows *(pending)*);
  *Apply now* in its menu applies it at once by cutting the sound, like PANIC.
- **Per-voice saturation** sits between filter and amp (or before the filter, Position), each note on its own, so chords
  stay free of intermodulation.
  Off at 0 dB, it fades in over the first 3 dB; level is compensated for Soft, Diode and Hard, not for Fold. Antialiased
  (ADAA), no added latency.
- **Analog VCA** (both off by default): Click polarity and amount vary per voice; Leak keeps finished voices sounding.
- **Reproducible randomness.** Main menu (right-click on the title; Options in the standalone) > **Randomness**:
  *Reproducible randomness (bounce = what you heard)*, on by default, reseeds every random generator at each transport
  anchor (start, jump, offline render), and seeds each Synth note by the position of its note-on, so the same project
  played from the same point gives exactly the same signal. Off: a random session seed, different on every pass, like
  analog hardware. *New random seed (detune, drift, noise...)* changes the seed. The seed is stored in the project, not
  in presets: two tracks with the same preset are not correlated.

## Factory presets

308 factory presets have no "engine" value and therefore use the Synth; none of them uses a sample. In the preset list and
the browser, `(sa)` after a name means a user preset plays a sample of its own, and `(st)` that the preset uses a string
model of Math Models (all but Modal). The 77 added in 1.0.1 and the new categories are described in [Synth preset bank](../preset-bank-2026-10-28.md). Those recreated from a reference recording went
through A/B/C listening tests (two or three candidates on the same notes and level as the reference, chosen by ear; the
measurements of the chosen one are noted in the code). Presets whose sound depends on the note or on how
they are played (nature sounds, one-voice drums, sequences, chip effects) say so: the display shows the hint for a few
seconds when the preset loads (for example "Hold E6 (or C6 for the 'meow')" for Gull Long Call), and the preset browser
shows it on hover and when the preset is selected. Most presets also carry a recommended range: the on-screen keyboard
greys out the keys outside it (a single key when the sound needs one note, such as the 808-style kick on G1). To find a
note, hover over the on-screen keyboard: it names the key under the mouse (C4 = MIDI 60, the convention of all the
manuals). Right-click a key to set the range (Range starts at / Range ends at / Only this key / Clear range) or to edit
the hint (Edit play hint...); like any other change, this marks the preset as modified, and Save or Save As stores it
with the preset.

The computer keyboard plays the on-screen keyboard (it never changes MIDI): the A row plays white keys (A to L and the
two keys after L, by position: Ñ and ´ on a Spanish keyboard, ; and ' on an English one), the Q row the black keys
between them, Z / X shift an octave down / up, C / V lower / raise the velocity by 10, and 1-9 load your favorite
presets 1-9 (mark them with the star in the browser and put them in your own order: see
[Getting started](../../README.md#getting-started)). No click is needed: pressing one of these keys gives the on-screen
keyboard the focus, unless you are typing in a text field. Loading a preset puts the A key on the
first key of its recommended range (C4 without a range); right-click a key, then "PC keyboard starts at" to choose
another white key. By category:

| Category | Presets |
|---|---|
| Init | Init |
| Bass | Saw Bass, Wobble, Acid Squelch, Studio Reel, Phasing Saw Bass, Picked Electric Bass, Analog Thump Bass, Model D Bass, Model D Air Bass, Model D Saw Stack Bass, Model D Robot Bass, Model D Bubble Bass, Model D Bounce Bass, Model D Sub Bass, Model D Soft Rez Bass, Model D Twin Square Bass, Model D The Jump, Taurus Pedal Bass, Mono Bass 101, Detuned Reese Bass, Juno Saw+Pulse Bass, 808 Sub Bass, House Organ Bass, Children Sub Bass, Children Offbeat Bass, Bleep & Bass Sub, Hardbass Donk, Electric Donk Bass, Blue Monday 88 Bass, Everything Counts Bass, The Model Bass, Europe Endless Bass, Around The World Bass, Acid 303 Bass, FM Solid Bass, Funk Filter Bass, Electro Hybrid Bass, Fingerstyle Bass, Fretless Bass, Hardcore Grind Bass, Pick Drive Bass, Slap Pop Bass, Hardcore Fold Impact, Wrong Pulse Drive |
| Lead | Retro Lead, Supersaw Lab, Ladder Whistle, MS-20 Acid Scream, Wavefold Lead, Valve Clavioline, Modular Lead 1968, Model D Lead, Model D Midnight Funk, Model D 70s Violin Lead, Model D Bright & Rude, Model D Fuzz Lead, Model D 3-Square Lead, Model D Octave Ouroboros, Model D Fusion Lead, Model D Sync Strike, Model D Nothin' Lead, Model D Looking Glass Lead, Model D Level 3, Model D Ghost Lead, Model D Outlaw, Odyssey Sync Lead, Eight-Voice Sync Lead, Phase Distortion Reso Lead, Gabber Screech, Fat Mono Lead, Hoover 90s, Numbers Lead, Just Cant Get Enough Lead, New Life Lead, Never Let Me Down Lead, Oxygene IV Lead, Da Funk Lead, Shine On Lead, Digital Sync Lead, Cloud Swell Lead, Cloud Swell Wah Lead, Octave Sync Solo, Reload Prism Lead |
| Pad | Soft Pad, Space Tape, Cassette Memories, Worn Reel 9.5, PWM Analog Pad, Endless Plate Pad, Barber Pole Drone, PWM Sweep Pad, Notch Sweep Pad, Model D Destitution, Model D Haunting, Wavetable Pad PPG, Digital Bell Pad, Gated Supersaw Pad, Vector Wave Pad, Mistress Pad, Voyager Chords, Short Circuit Swell, In The Air Tonight Pad, Fade To Grey Pad, Stranger Things Pad, Harmonic Sweep Pad, Trance Supersaw Pad, Evolving Filter Drone, Evolving Wavetable Drone, Airy Vibrato Pad, Vowel Aurora, Crystal Hybrid 1987, Dusk Chamber |
| Keys | Glass Bell, French Chords, Dusty Vinyl, Funky Clav, Pianet Reed Keys, Reed EP 200A, Model D Smooth EP, Suitcase Tine EP, Clavinet D6 Strings, Dub Techno Chord, Detroit Chord Stab, Spring Dub Stab, Baroque Harpsichord, Neon Lights Keys, Formant Sweep Keys, House Piano Stab, House Piano Stab Pluck, Deep House Chord, Berlin Time Resonance, Metal Memory, Soft Chord Memory, Synth Soft Piano, Why Heart Gospel Keys |
| Organ | Continental Combo Organ, Compact Combo Organ, Breath Rotor Organ, Stellar Air Organ, Tonewheel Soul Organ, Concert Accordion, Chapel Pipes, Chapel Pipes Full |
| Mallets | Celesta, Kalimba, Tubular Bells, Velvet Vibraphone, Synth Harmonic Marimba |
| Brass | Neon Brass, Model D Brass Drone, Five-Voice Poly Brass, Poly Brass Stab, Blade Runner Brass, Replicant Brass Swell, Africa Brass, Jump Brass, Muted Trumpet Noir, Warm Trombone |
| Woodwind | Tape Flute, Forest Ocarina, Blues Harmonica, Smoky Tenor Sax, Synth Chamber Clarinet, Synth Concert Flute, Synth Lyric Oboe |
| Strings | String Machine, Synth Pizzicato, Comb Bowed Metal, Children Strings, Shine On Intro Strings, Tears in Rain Strings, Oxygene Phased Strings, Incantations Ensemble, Space Oddity Tape Strings, Warszawa Tape Cello, Oxygene Ensemble Strings, Solo Cello Legato, Solo Viola Warm, Solo Violin Cantabile, Violin Cantabile Poly, Venetian Chamber Synth |
| Guitar | Nylon Guitar, Children Acoustic Guitar, Clean Electric Guitar, Ambient Spells Guitar, Darkwave Razor Guitar, Disco Mute Guitar, French Filter Guitar, Glass Chorus Guitar, Industrial Drive Guitar, New Age Harmonics Guitar, Personal Twang Guitar |
| Pluck | Echo Pluck, Concert Harp, Model D Steel Wound Pluck, Trance Supersaw Pluck, Children Pluck, Ambient Echo Arp, Ambient Echo Arp Glass, Ambient Echo Arp Dark, Soft Echo Pluck, Enjoy Bell Pluck |
| World | Banjo Pick, Hammered Dulcimer, Koto Silk, Sitar Resonance |
| Sequence | Berlin Sequence, Hardbass Donk Bounce, Robot Arp, On the Run Sequence, Acid Step Line, Irregular 7-Step, Equinox Orbit Sequence, EBM Drive Sequence, Pocket Computer Sequence, Progressive Prism Pluck, Binary Resonator, Bode Conveyor, Comb Machine, Cross Sync Orbit, Odd Ring Laboratory, Phase Transmission, Sample Hold Workshop |
| Drums | Model D Dark Toms, Model D Room Snare, Model D Hi-Hat, Model D Tri Kick, Model D Rez Hit, Gabber Kick, Children Boom Kick, Minimal Techno Click Perc, Hex Pad Tom, 808-Style Kick, 808-Style Long Boom, 909-Style Kick, 808-Style Snare, 808-Style Tom, 808-Style Conga, 808-Style Rimshot, 808-Style Claves, 808-Style Cowbell, 808-Style Closed Hat, 808-Style Open Hat, FM Ride Cymbal, Analog Hand Clap, Numbers Metal Perc |
| FX | Model D Vocal Bot, Model D Very Bad Place, Model D Abandoned Planet, Model D Freqy, Model D Jurassic Love, Model D Whirlwind, Model D Light Cycle, Model D 70s Sci-Fi Wobble, Model D 60s Space, Model D Singing Wind, Model D Square Meow, Model D Broken Radio, Orchestra Hit 8-Bit, Noise Riser, Sub Drop, Underwater Pad, Bode Bells, Sci-Fi Laser Zap, Resonant Whistle Swell, Bode Drift |
| Cinematic | Monolith Cluster |
| Hybrid | Bowed Reed, Bowed Resonator |
| 8-Bit | 8-Bit Power-Up, 8-Bit Hit, 8-Bit Menu Blip, Chip Square Bass, SID-Style Sync Lead, SID-Style PWM Bass, SID-Style Ring Bell |
| Nature | Field Cricket, Dolbear Cricket 25C, Cicada Buzz, Spring Peeper, Bullfrog Croak, Chickadee Fee-Bee, Syrinx Tweet, Tawny Owl Hoot, Wolf Howl, Cat Meow, Cat Purr, Humpback Moan, Gull Long Call, Rain Drops, Campfire Crackle, Wind Whistle, Thunder Strike, Ocean Surf, Gusting Wind, Distant Thunder |
| Vocal | Dual Formant Ah, Poly Vox Humana, Choir Aah, Choir Ooh, Vowel Morph Pad, Talk Box Lead, Formant Circuit Robot |
| Vocoder | Robot Vocoder, Robot Voice 10-Band, Autobahn Vocoder, Harder Robot Carrier |

## Limitations

- 8 voices in Poly; Mono and Legato are single-voice. Unison is limited to 7 oscillators per voice.
- The complex oscillator's 2x decimation filter adds 15 samples at the doubled rate (0.16 ms at 48 kHz).
- Voice Saturation with the Fold curve is not level-compensated.
- String Damping: with long Decay times the damping is limited to what can be compensated.
- VCA Leak keeps released voices running and uses CPU.
- The Sample source needs your own file; KoniSynth includes no samples.
