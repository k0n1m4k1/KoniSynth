# Synth preset bank (1.0.1-beta, 2026-10-28)

87 Synth-engine presets, prepared as user presets during the 1.0.1 beta, were curated into the factory bank: **77 new factory presets**, **2 updates** of existing ones and **8 not shipped** (duplicates or superseded versions, listed below as aliases of the preset that replaces them). All of them use the Synth engine.

## How they were integrated

- **Values:** each factory entry stores only the parameters that differ from their default value (`Source/Presets/Factory.h`, end of the list), rounded to 6 significant digits. Sub-parameters of blocks that are switched off (for example amp settings with Amp off) were dropped: rendering every preset with and without them gave identical audio, sample for sample.
- **No personal paths:** the source files pointed wavetable slots to a local file, but no preset selects the "User File" table, so those paths had no effect and were removed.
- **Level:** loudness, not peak, for the whole factory bank except Drums, Machines, FX and Nature. Each preset is played at velocity 100 for 4 s on the lowest, middle and highest note of its recommended range; its loudness is the maximum K-weighted power (ITU-R BS.1770, which accounts for frequency) over 200 ms windows, averaged in power over the three notes. `gain` (linear, last in the chain) is scaled to reach -26.8 LUFS (the median of the bank), with a tolerance of ±0.5 dB, a ceiling of gain = 1 and peaks at or below -6 dBFS; 38 presets that need more than gain = 1 stay at the ceiling. The spread between presets (10th to 90th percentile) went from 10.9 dB to 1.5 dB.
- **Check:** each factory entry was rendered against its source preset; the output matches the source scaled by the gain change (largest relative difference 4.8e-6, from the rounding).
- **Patterns:** sequencer and arpeggiator presets have Latch off: releasing the key stops the pattern.
- **Markers:** in the preset list and the browser, `(π)` follows the name of a preset that uses the Math Models source (any model, Modal included). No factory preset uses a sample (`(s)` only marks user presets with a sample of their own).

## New categories

Organ, Mallets, Woodwind, Guitar, World, Cinematic and Hybrid join the existing ones. Category prefixes used in the source names ("Guitar - ", "Ethnic - ", "Mallets - "...) were removed or turned into a suffix. Existing factory presets moved to a new category (only the label changes):

| Preset | Before | Now |
|---|---|---|
| Continental Combo Organ | Keys | Organ |
| Compact Combo Organ | Keys | Organ |
| Tonewheel Organ B3 | Keys | Organ |
| FM Organ | Keys | Organ |
| OPL2 Organ | Keys | Organ |
| Leslie Organ | Keys | Organ |
| Nylon Guitar | Pluck | Guitar |
| Children Acoustic Guitar | Pluck | Guitar |
| Tape Flute | Keys | Woodwind |
| FM Marimba | Pluck | Mallets |
| FM Tubular Bells | Keys | Mallets |
| MD Tubular Bells | Keys | Mallets |

A user preset whose category this version does not know (one saved by a later version) is listed under Init and keeps its category when saved again.

## Other changes to existing factory presets

| Preset | Change | Reason |
|---|---|---|
| Blue Monday 88 Bass | values of the revised version; hint "Tight mono eighth notes E1-G3; velocity adds brightness." | Revision of the factory preset: mono, drift 0, shorter release, velocity to brightness |
| On the Run Sequence | values of the revised version; hint "Hold E3 for the pattern at 165 BPM; releasing the key stops it." | Same sound; only seqLatch 1 -> 0, so releasing the key stops the pattern |
| House Piano Stab | recommended range C3-C5 (48-72) instead of C3-C6 | the hint already said C3-C5 |
| House Piano Stab FM | recommended range C3-C5 (48-72) instead of C3-C6 | the hint already said C3-C5 |
| House Piano Stab Pluck | recommended range C3-C5 (48-72) instead of C3-C6 | the hint already said C3-C5 |

## New factory presets

| Factory name | Marker | Category | Source preset | Peak before | Gain before → after | Note |
|---|---|---|---|---|---|---|
| Vowel Aurora |  | Pad | Ambient - Vowel Aurora | 0.054 | 0.350 → 0.514 | Pad category replaces the "Ambient -" prefix |
| Berlin Time Resonance |  | Keys | = | 0.064 | 0.900 → 0.900 | Unique FM bell + comb voice |
| Monolith Cluster |  | Cinematic | Cinema - Monolith Cluster | 0.057 | 0.900 → 1.000 | Cinematic category replaces the prefix |
| Clean Electric Guitar | (π) | Guitar | Clean Electric Guitar 2 | 0.037 | 0.400 → 0.876 | Canonical: the amp version measured 0.0068 peak (-43 dBFS); this one bypasses the amp |
| Crystal Hybrid 1987 |  | Pad | = | 0.048 | 0.400 → 0.672 | Unique FM/wavetable pad |
| Funk Filter Bass |  | Bass | Daft Filter Motion | 0.118 | 0.900 → 0.612 | Artist name removed |
| Octave Sync Solo |  | Lead | Daft Sync Solo 2 | 0.089 | 0.900 → 0.900 | Canonical of the pair (exact octave, stronger fundamental); artist name removed |
| Metal Memory |  | Keys | Depeche Metal Memory | 0.120 | 0.900 → 0.598 | Artist name removed; attack by Math Models > Modal (Metal recipe) |
| Electro Hybrid Bass | (π) | Bass | Electric Bass - Electro Hybrid | 0.112 | 0.950 → 0.679 | Prefix moved to a suffix |
| Fingerstyle Bass | (π) | Bass | Electric Bass - Fingerstyle | 0.131 | 0.950 → 0.581 | Prefix moved to a suffix |
| Fretless Bass | (π) | Bass | Electric Bass - Fretless | 0.052 | 0.950 → 1.000 | Prefix moved to a suffix |
| Hardcore Grind Bass | (π) | Bass | Electric Bass - Hardcore Grind | 0.140 | 0.918 → 0.524 | Prefix moved to a suffix |
| Pick Drive Bass | (π) | Bass | Electric Bass - Pick Drive | 0.086 | 0.950 → 0.950 | Prefix moved to a suffix |
| Slap Pop Bass | (π) | Bass | Electric Bass - Slap Pop | 0.127 | 0.950 → 0.600 | Prefix moved to a suffix |
| Enjoy Bell Pluck |  | Pluck | = | 0.102 | 0.900 → 0.708 | Unique |
| Equinox Orbit Sequence |  | Sequence | = | 0.074 | 0.900 → 0.900 | Unique; latch off |
| Banjo Pick | (π) | World | Ethnic - Banjo Pick | 0.038 | 0.400 → 0.832 | World category replaces the "Ethnic -" prefix |
| Hammered Dulcimer | (π) | World | Ethnic - Hammered Dulcimer | 0.041 | 0.400 → 0.773 | World category replaces the prefix |
| Koto Silk | (π) | World | Ethnic - Koto Silk | 0.032 | 0.400 → 0.999 | World category replaces the prefix |
| Sitar Resonance | (π) | World | Ethnic - Sitar Resonance | 0.031 | 0.400 → 1.000 | World category replaces the prefix |
| Bode Drift |  | FX | Experimental - Bode Drift | 0.069 | 0.731 → 0.731 | Prefix removed |
| EBM Drive Sequence |  | Sequence | Frontline EBM Sequence | 0.113 | 0.900 → 0.636 | Name alluding to a band replaced |
| Ambient Spells Guitar | (π) | Guitar | Guitar - Ambient Spells | 0.054 | 0.500 → 0.739 | Guitar category; prefix moved to a suffix |
| Darkwave Razor Guitar | (π) | Guitar | Guitar - Darkwave Razor | 0.174 | 0.063 → 0.029 | Kept apart from Industrial Drive: 26 values differ (comp, chorus, decay) |
| Disco Mute Guitar | (π) | Guitar | Guitar - Disco Mute | 0.122 | 0.950 → 0.623 | Guitar category |
| French Filter Guitar | (π) | Guitar | Guitar - French Filter | 0.092 | 0.950 → 0.950 | Guitar category |
| Glass Chorus Guitar | (π) | Guitar | Guitar - Glass Chorus | 0.110 | 0.950 → 0.689 | Guitar category |
| Industrial Drive Guitar | (π) | Guitar | Guitar - Industrial Drive | 0.175 | 0.050 → 0.023 | Guitar category |
| New Age Harmonics Guitar | (π) | Guitar | Guitar - New Age Harmonics | 0.093 | 0.950 → 0.950 | Guitar category |
| Personal Twang Guitar | (π) | Guitar | Guitar - Personal Twang | 0.211 | 0.350 → 0.132 | Guitar category |
| Hardcore Fold Impact |  | Bass | = | 0.089 | 0.900 → 0.900 | Unique |
| Harder Robot Carrier |  | Vocoder | = | 0.000 | 0.350 → 0.350 | Vocoder preset (needs audio input; level not measurable offline) |
| Bowed Reed |  | Hybrid | Hybrid - Bowed Reed | 0.107 | 0.100 → 0.075 | Canonical of the Heavy Bow 2 duplicate; honest hybrid name |
| Bowed Resonator |  | Hybrid | Hybrid - Bowed Resonator | 0.104 | 0.900 → 0.691 | Hybrid category |
| Celesta |  | Mallets | Mallets - Celesta | 0.043 | 0.400 → 0.747 | Mallets category |
| Kalimba |  | Mallets | Mallets - Kalimba | 0.042 | 0.400 → 0.760 | Mallets category |
| Tubular Bells |  | Mallets | Mallets - Tubular Bells | 0.032 | 0.300 → 0.752 | Mallets category |
| Velvet Vibraphone |  | Mallets | Mallets - Velvet Vibraphone | 0.051 | 0.400 → 0.633 | Mallets category |
| Dusk Chamber |  | Pad | Moby Dusk Chamber | 0.061 | 0.350 → 0.350 | Artist name removed |
| Soft Chord Memory |  | Keys | Moby Soft Chord Memory | 0.035 | 0.350 → 0.792 | Artist name removed; attack by Math Models > Modal (Soft Key recipe) |
| Muted Trumpet Noir |  | Brass | = | 0.056 | 0.400 → 0.576 | Unique |
| Numbers Metal Perc |  | Drums | = | 0.115 | 0.900 → 0.626 | Unique |
| Forest Ocarina |  | Woodwind | Ocarina - Forest Breath | 0.109 | 0.900 → 0.662 | Woodwind category |
| Breath Rotor Organ |  | Organ | Organ - Floyd Breath Rotor | 0.049 | 0.350 → 0.566 | Organ category; artist name removed |
| Stellar Air Organ |  | Organ | Organ - Stellar Air | 0.105 | 0.350 → 0.267 | Organ category |
| Tonewheel Soul Organ |  | Organ | Organ - Tonewheel Soul | 0.055 | 0.300 → 0.439 | Organ category |
| Oxygene Ensemble Strings |  | Strings | Oxygene Phased Strings 2 | 0.018 | 0.350 → 1.000 | Not a revision of the factory Oxygene Phased Strings (32 values differ): a separate sound |
| Pocket Computer Sequence |  | Sequence | = | 0.094 | 0.900 → 0.900 | Unique; latch off |
| Progressive Prism Pluck |  | Sequence | = | 0.118 | 0.900 → 0.613 | Arpeggio: Sequence category |
| Blues Harmonica |  | Woodwind | Reed - Blues Harmonica | 0.042 | 0.400 → 0.759 | Woodwind category (free reed) |
| Concert Accordion |  | Organ | Reed - Concert Accordion | 0.035 | 0.400 → 0.924 | Organ category (keyboard free reed) |
| Smoky Tenor Sax |  | Woodwind | Reed - Smoky Tenor | 0.046 | 0.400 → 0.690 | Woodwind category |
| Reload Prism Lead |  | Lead | = | 0.098 | 0.570 → 0.570 | Unique |
| Formant Circuit Robot |  | Vocal | Robot - Formant Circuit | 0.090 | 0.757 → 0.757 | Vocal category (synthetic formants, no vocoder) |
| Binary Resonator |  | Sequence | Sequence - Binary Resonator | 0.076 | 0.400 → 0.400 | Prefix removed (Sequence category) |
| Bode Conveyor |  | Sequence | Sequence - Bode Conveyor | 0.092 | 0.900 → 0.900 | Prefix removed |
| Comb Machine |  | Sequence | Sequence - Comb Machine | 0.132 | 0.900 → 0.547 | Prefix removed |
| Cross Sync Orbit |  | Sequence | Sequence - Cross Sync Orbit | 0.054 | 0.900 → 1.000 | Prefix removed; the octave-up is documented in the hint |
| Odd Ring Laboratory |  | Sequence | Sequence - Odd Ring Laboratory | 0.094 | 0.900 → 0.900 | Prefix removed |
| Phase Transmission |  | Sequence | Sequence - Phase Transmission | 0.040 | 0.300 → 0.602 | Prefix removed |
| Sample Hold Workshop |  | Sequence | Sequence - Sample Hold Workshop | 0.038 | 0.900 → 1.000 | Prefix removed |
| Solo Cello Legato |  | Strings | Solo Cello Legato 2 | 0.119 | 0.950 → 0.641 | Canonical: glide 35 ms (the first version had 0.035 ms) |
| Solo Viola Warm |  | Strings | = | 0.127 | 0.950 → 0.599 | Unique |
| Solo Violin Cantabile |  | Strings | Solo Violin Cantabile 2 | 0.111 | 0.950 → 0.683 | Canonical legato version |
| Violin Cantabile Poly |  | Strings | Solo Violin Cantabile | 0.047 | 0.400 → 0.685 | Kept: polyphonic (voiceMode Poly) vs the legato canonical |
| Synth Chamber Clarinet |  | Woodwind | = | 0.024 | 0.350 → 1.000 | Woodwind category |
| Chapel Pipes |  | Organ | Synth Chapel Pipes 2 | 0.055 | 0.280 → 0.407 | Organ register without the sub |
| Chapel Pipes Full |  | Organ | Synth Chapel Pipes | 0.063 | 0.280 → 0.280 | Kept as a second register (subLevel 0.18); the pitch-detector reading did not prove detuning |
| Synth Concert Flute |  | Woodwind | = | 0.050 | 0.350 → 0.556 | Woodwind category |
| Synth Harmonic Marimba |  | Mallets | = | 0.049 | 0.350 → 0.574 | Mallets category |
| Synth Lyric Oboe |  | Woodwind | = | 0.035 | 0.350 → 0.803 | Woodwind category |
| Synth Soft Piano |  | Keys | Synth Soft Piano 2 | 0.046 | 0.400 → 0.693 | Canonical: stable tuning |
| Venetian Chamber Synth |  | Strings | = | 0.062 | 0.900 → 0.900 | Unique |
| Warm Trombone |  | Brass | = | 0.038 | 0.400 → 0.836 | Unique |
| Why Heart Gospel Keys |  | Keys | = | 0.041 | 0.350 → 0.677 | Attack by Math Models > Modal (Soft Key recipe) |
| Wrong Pulse Drive |  | Bass | Wrong Pulse Drive 2 | 0.099 | 0.900 → 0.900 | Canonical: the darker, smoother revision |

## Not shipped (aliases)

Loading an old project keeps its sound (projects store every value); only the preset name shown is the old one.

| Source preset | Replaced by | Reason |
|---|---|---|
| Clean Electric Guitar | Clean Electric Guitar | Only ampOn differs from the canonical; output 14.6 dB lower (0.0068 vs 0.0365 peak) |
| Daft Sync Solo | Octave Sync Solo | Superseded by revision 2 (4 values differ: osc levels, osc2Semi 7 -> 0) |
| Electric Cello - Heavy Bow 2 | Bowed Reed | Exact duplicate of Hybrid - Bowed Reed (0 serialized differences) |
| Electric Cello - Heavy Bow | Bowed Resonator | Harsh/pulsing for a bow; the aggressive bowed texture is covered by Bowed Resonator |
| Floyd Breath Rotor | Breath Rotor Organ | Exact duplicate of Organ - Floyd Breath Rotor (0 differences; left by a failed rename) |
| Solo Cello Legato | Solo Cello Legato | Preset error: glide 0.035 ms instead of 35 ms |
| Synth Soft Piano | Synth Soft Piano | Only drift 5 % and osc2Fine +1.5 ct differ from the canonical |
| Wrong Pulse Drive | Wrong Pulse Drive | Brighter, harsher first version; the dark revision replaces it |

Renamed to remove artist names: Moby Soft Chord Memory → Soft Chord Memory, Moby Dusk Chamber → Dusk Chamber, Daft Sync Solo 2 → Octave Sync Solo, Daft Filter Motion → Funk Filter Bass, Depeche Metal Memory → Metal Memory, Floyd Breath Rotor → Breath Rotor Organ, Frontline EBM Sequence → EBM Drive Sequence.

## Limits

- Levels were measured on one note; chords and pads played as chords sound louder. Harder Robot Carrier is a vocoder preset and is silent without audio input, so its level was not measured.
- Fretless Bass, Monolith Cluster, Cross Sync Orbit, Sample Hold Workshop, Synth Chamber Clarinet and Oxygene Ensemble Strings stay below 0.06 with gain at its maximum (peaks 0.043-0.067).
- Names of acoustic instruments describe the role, not a model of them: the String source is a plucked-string model, and bowed, reed and wind presets are subtractive or hybrid sounds.
- Cross Sync Orbit sounds one octave above the played key (oscillator sync); its hint says so.
- No commercial recording was used to compare these presets: the references guide the musical role and character, not an exact emulation.
- Init keeps its analog character (Drift 20 %, filter Clean Drive off). Ambient Echo Arp Dark keeps its tuning: the -32 cent reading came from very short notes and was not confirmed.
