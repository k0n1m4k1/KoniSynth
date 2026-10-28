# OPL (AdLib / Sound Blaster)

The OPL engine emulates four Yamaha FM chips. OPL2 is the YM3812 of the AdLib and Sound Blaster 1-2: 9 two-operator
voices, 4 waveforms, mono, through the YM3014 floating-point DAC. OPL3 is the YMF262 of the Sound Blaster Pro 2 and 16:
18 voices or 6 four-operator voices, 8 waveforms, stereo. OPLL is the YM2413 (Japanese Master System FM, MSX-MUSIC):
9 voices, 15 ROM instruments + 1 user instrument, optional drums. VRC7 is Konami's OPLL variant used in Lagrange Point:
6 voices and its own ROM set. OPL2/OPL3 are KoniSynth's own implementation following Nuked-OPL3 1.8; OPLL and VRC7 run
on Nuked-OPLL 1.0.2, cycle-accurate, with instrument ROMs dumped from each chip. Use it for DOS game music, AdLib-era FM,
and MSX / Master System FM.

## What you need

Nothing extra.

## Controls

Sections: **OPL** and **OPL LFO / Output**; with Model = OPLL or VRC7, an **OPLL / VRC7** section appears between them.

### OPL

| Control | What it does | Range / default |
|---|---|---|
| Model (header menu) | OPL2 (YM3812), OPL3 (YMF262), OPLL (YM2413) or VRC7 (Konami), as described above. | default OPL2 (YM3812) |
| 4-Op | OPL3 only: pairs of channels become 4-operator voices (6 voices) with 4 algorithms. | off |
| Operators... (header button) | Opens the OPL editor (see below). | |
| Algorithm | 2 operators: 0 = FM (1 modulates 2), 1 = AM (1 + 2 added). 4 operators (OPL3): 0 = 1->2->3->4, 1 = (1->2)+(3->4), 2 = 1+(2->3->4), 3 = 1+(2->3)+4. | 0-3; 0 |
| Feedback | Self-modulation of OP1 (register C0h): 0 = off, 7 = most (noisy). | 0-7; 0 |
| OP1 TL ... OP4 TL | Total level, 0.75 dB steps (63 = -47 dB). On a modulator it sets the brightness. | 0-63; OP1/OP3 63, OP2/OP4 0 |

Default patch: 2 operators in FM, the modulator silent (TL 63) and the carrier a sine with AR 15, RR 7, EGT on.

In the OPL editor (Operators...), each operator column shows its envelope with the chip's times (at C4, with KSR), drawn
from the chip's own envelope generator; drag its points for AR, DR and SL, and RR.

**OPL editor (Operators...).** Columns OP1-OP4 with the chip's own registers, carriers highlighted, algorithm written out;
OP3/OP4 are used only in OPL3 4-operator mode. Per operator:

| Control | What it does | Range / default |
|---|---|---|
| TL | Total level (as above). | 0-63 |
| MULT | Frequency multiple: 0 = x0.5, 1-10, 12, 15 (11 and 13 give 10 and 12, as on the chip). | 0-15; 1 |
| KSL | Key scale level: attenuation that grows with pitch. The control is in dB/oct on every chip (on the OPL2/OPL3 register, value 1 is 3 dB/oct and 2 is 1.5 dB/oct; KoniSynth writes the right one). | 0 / 1.5 / 3 / 6 dB/oct; 0 |
| AR | Attack rate (15 = instant). | 0-15; 15 |
| DR | Decay rate from full level to the sustain level. | 0-15; 0 |
| SL | Sustain level in 3 dB steps (15 = -93 dB). | 0-15; 0 |
| RR | Release rate (and the decay after sustain when EGT is off). | 0-15; 7 |
| WAVE | Waveform (register E0h). OPL2 has the first 4; OPL3 all 8. | 0 Sine, 1 Half sine, 2 Abs sine, 3 Quarter sine, 4 Alternating, 5 Camel, 6 Square, 7 Log saw; 0 |
| AM | Tremolo: follows the chip LFO, 3.7 Hz, depth set by Deep AM. | off |
| VIB | Vibrato: follows the chip vibrato, 6.1 Hz, depth set by Deep VIB. | off |
| EGT | On = the envelope holds at SL while the key is down; off = keeps decaying at RR (percussive). | on |
| KSR | Envelopes get faster in higher octaves. | off |

### OPLL / VRC7 (OPLL and VRC7 models only)

| Control | What it does | Range / default |
|---|---|---|
| Instrument (header menu) | Instrument from the chip's ROM. 0 = User: built from OP1 (modulator) and OP2 (carrier) of the OPL editor (Waveform 1 = half sine). The names are Yamaha's for the YM2413; the VRC7 has its own, different set (no official names). | 0 User, 1 Violin ... 15 Electric Guitar; 3 Piano |
| Rhythm | YM2413 rhythm mode: channels 7-9 become bass drum, snare, tom, cymbal and hi-hat on MIDI channel 10; 6 melodic voices remain. The VRC7 has no rhythm output. | off |
| Volume | Channel volume register: attenuation in 3 dB steps (0 = loudest). Velocity adds to it. | 0-15; 0 |
| SUS | SUS bit: after release the note fades with a slow fixed release instead of the instrument's own. | off |

### OPL LFO / Output

| Control | What it does | Range / default |
|---|---|---|
| Velocity | Velocity lowers the carriers' TL (0.75 dB steps), as a sound driver would; the chip has no velocity. | on |
| Deep AM | Register BDh bit 7 (DAM): tremolo depth 4.8 dB instead of 1 dB. | off |
| Deep VIB | Register BDh bit 6 (DVB): vibrato depth 14 cents instead of 7. | off |
| Pan | OPL3 stereo (register C0h bits 4-5). Alternate: voices go left, right, left... The OPL2 is mono. | Center / Left / Right / Alternate L/R; Center |
| Output | Stereo (OPL3) or both sides summed to mono. | Stereo (OPL3) / Mono; Stereo |

## Playing it

- **Voices:** OPL2 9; OPL3 18, or 6 with 4-Op; OPLL 9, or 6 with Rhythm on; VRC7 6. A repeated note reuses its voice;
  otherwise the oldest free voice, else the oldest one.
- **OPLL user instrument:** set Instrument = 0 User and shape it in Operators... with OP1 (modulator) and OP2 (carrier): AM,
  VIB, EGT, KSR, MULT, AR, DR, SL, RR of both; KSL of both; TL of OP1; WAVE 0 or 1 of both; Feedback.
- **OPLL drums (channel 10):** set Model = OPLL (YM2413), turn Rhythm on, and send notes on MIDI channel 10 (General MIDI
  drum notes). Other channels keep playing the 6 melodic voices. Note map in the code:

| Drum | MIDI notes |
|---|---|
| Bass drum | 35, 36 |
| Snare | 37, 38, 39, 40 |
| Tom | 41, 43, 45, 47, 48, 50 |
| Cymbal | 49, 51, 52, 55, 57, 59 |
| Hi-hat | 42, 44, 46 |

  If KoniSynth's global MIDI Input Channel is set to one channel, channel messages on other channels are dropped: keep it
  at Omni to reach channel 10 from a controller. The drum tunings and levels are values commonly used by MSX/SMS drivers.

## Faithful details

- **OPL2/OPL3 core** (after Nuked-OPL3): 49,716 Hz, 19-bit phase, log-sine and exponential ROM tables, 9-bit envelope with
  the chip's timer, exponential attack, KSL table, 210-step tremolo and 8-step vibrato.
- **YM3014 DAC (OPL2):** floating point with 3 exponent and 10 mantissa bits; the low bits of loud signals are lost, the
  typical AdLib grain (ymfm, `roundtrip_fp`).
- **OPL3 channel delay:** some channels come out one sample later on one side (channels 6-8 left, 15-17 right), an
  effect of the YMF262's processing order (Nuked, `OPL_QUIRK_CHANNELSAMPLEDELAY`).
- **OPL2:** only 4 waveforms (on the chip, enabled by the WSE test bit that games set) and mono output.
- **OPLL/VRC7** (Nuked-OPLL): multiplexed DAC (the 18 cycles of each sample are summed, as the analog output integrates
  them); on the YM2413 the sign bit leaks through the DAC on a silent channel; percussion comes out twice through the
  rhythm output (louder); the VRC7 has 6 channels and no percussion.
- **Register timing:** KoniSynth's driver writes OPLL registers through a queue with the chip's timing (address, 4
  cycles, data, 32 cycles: 2 samples per register, 40 µs), as a game CPU would.

Optional "nicer" settings: none. The code defines no beautification switch for this engine; the quirks above are always on.

## Factory presets

| Preset | Category |
|---|---|
| OPL2 E.Piano | Keys |
| OPL2 Organ | Keys |
| OPL2 Bass | Bass |
| OPL2 Strings | Strings |
| OPL3 Square Bass | Bass |
| OPL3 4-Op Brass | Brass |
| OPLL Piano | Keys |
| OPLL Trumpet | Brass |
| OPLL Synth Bass + Drums | Bass |
| VRC7 Patch 1 | 8-Bit |

Choosing OPL from the engine menu loads OPL2 E.Piano.

## Limitations

- OPL2/OPL3: no percussion (rhythm) mode and no CSM in this version.
- Not verified: the mix scale of the YM3812 (ymfm marks it as pending verification); the engine uses Nuked's.
- OPLL/VRC7 output is mono; Pan applies to OPL3.
- The VRC7 has no rhythm output; its ROM instruments have no official names.
- Velocity is KoniSynth's driver behaviour, not a chip feature.
- Nuked-OPLL is GPL-2.0+; the code notes that integrating it makes the KoniSynth binary GPL-2.0-or-later.
