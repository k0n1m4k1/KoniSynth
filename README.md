# KoniSynth

**Version 1.0.0** · by **@k0n1m4k1** 🇪🇸 · GNU AGPLv3


[![M8ven Score](https://m8ven.ai/badge/mcp/k0n1m4k1-konisynth-knlbk4?v=1c9d31871080c40a85dd776478b0d476)](https://m8ven.ai/mcp/k0n1m4k1-konisynth-knlbk4?s=readme)

**Open-source synth lab with firmware-accurate emulations of the Yamaha DX7, Gravis UltraSound, Roland MT-32 & others.** Sixteen engines for the sounds of the 60s to the 90s in one free plugin: **VST3**, **CLAP** and **standalone**, on
**Windows** and **Linux**.

https://github.com/user-attachments/assets/bc25f214-fb35-41e4-b6d6-8f8946c0178c

## Why KoniSynth

- **The real machines, bugs included.** The DX7 runs its OPS chip arithmetic and firmware pitch bend; the MT-32 overflows
  like a first-generation unit; the YM2612 has its ladder effect. Anything nicer than the original is an option, off by default.
- **Sound cards and consoles of the 90s.** AdLib, Sound Blaster and AdLib Gold (OPL), Gravis UltraSound, Roland MT-32,
  Mega Drive, SNES, N64, NES and Game Boy, each with the chip's own limits.
- **A synth of its own.** The Synth engine is not an emulation: a Moog-style ladder, 14 filters, unison, wavetable, samples,
  physical models, analog drift, an 8-slot matrix and MPE.
- **Play any MIDI file, and render it.** A built-in MIDI player with a play-along mode, and **Render Stems**: one WAV per
  track, each with its own preset, faster than real time.
- **Measured, not guessed.** Hardware figures come from datasheets, decaps and reference emulators, cited in the code;
  sounds are checked against recordings and by ear.
- **464 factory presets**, an AI agent interface (MCP), and no added latency.

## Engines

| Engine | What it is | Manual |
|---|---|---|
| Synth | KoniSynth's own analog-style synth: oscillator, wavetable, sample and Math Models (12 physical and stochastic models), Osc 2 and Osc 3, noise, complex oscillator, 14 filters, matrix | [synth.md](docs/engines/synth.md), [maths.md](docs/engines/maths.md) |
| Theremin | Physical model of the theremin (pitch and volume antennas) with its own pad; also playable from MIDI | [theremin.md](docs/engines/theremin.md) |
| Casio VL-1 | The 1979 VL-Tone: its sounds, ADSR digits, rhythms and LCD | [casio-vl1.md](docs/engines/casio-vl1.md) |
| Stylophone | The 1968 stylus organ (20 keys) and the experimental Gen X-2, with a gold-plate pad | [stylophone.md](docs/engines/stylophone.md) |
| Omnichord | Suzuki Omnichord: chord buttons, strum plate and rhythms | [omnichord.md](docs/engines/omnichord.md) |
| Drum Machine | Mini Pops 7, CR-78 and TR-808 voices with patterns and step lights | [drum-machine.md](docs/engines/drum-machine.md) |
| FM 6-Op (DX7) | Yamaha DX7 (OPS chip arithmetic): 32 algorithms, pitch envelope, firmware pitch bend, display; loads DX7 SysEx banks | [fm-6op.md](docs/engines/fm-6op.md) |
| 8-Bit/DPCM | 2A03 (NTSC/PAL), Famicom with VRC6, Game Boy / Color, with frame macros | [chip-8bit.md](docs/engines/chip-8bit.md) |
| Polivoks | The Soviet Polivoks synthesizer | [polivoks.md](docs/engines/polivoks.md) |
| ANS | The photoelectronic ANS synthesizer: draw the score | [ans.md](docs/engines/ans.md) |
| Mega Drive | YM2612 / YM3438 FM + SN76489 PSG + DAC | [mega-drive.md](docs/engines/mega-drive.md) |
| SNES | S-DSP: BRR samples, Gaussian interpolation, echo with FIR | [snes.md](docs/engines/snes.md) |
| N64 | RSP + libultra: VADPCM, resampling and its effects | [n64.md](docs/engines/n64.md) |
| OPL | YM3526 (OPL), YM3812 (OPL2), YMF262 (OPL3, with the AdLib Gold surround and tone stage), YM2413 (OPLL) and Konami VRC7; rhythm mode and General MIDI | [opl.md](docs/engines/opl.md) |
| Roland MT-32 | MT-32 / CM-32L through Munt, with **your own ROMs**; its LCD and named patch list | [mt32.md](docs/engines/mt32.md) |
| Gravis UltraSound | GF1 wavetable chip (Classic, MAX, Extreme) with **your own patches**: output rate set by the active voices, logarithmic volume ramps, card memory | [gravis-ultrasound.md](docs/engines/gravis-ultrasound.md) |

## Features

**Synth engine**
- Four summed sources (oscillator, wavetable in the Serum format, sample, Math Models), plus Osc 2, Osc 3 and Noise.
- Minimoog Tri-Saw, Prophet-5 and Juno-106 wave mixes, and analog triangles measured on two Model D units.
- Ladder filter with three anti-aliasing qualities, 14 filter models, per-voice saturation before or after the filter.
- Analog RC envelopes, 1/f drift, two tempo-synced LFOs, an 8-slot matrix, MPE, polyphonic aftertouch, a mixer per source.
- Editable displays: waveform, filter response, envelopes, LFOs. Details: [synth.md](docs/engines/synth.md).

**Effects** (seven groups; drag the tabs to reorder the first six, saved with the preset)
- Color: bit crusher, formant filter, tube, parametric EQ. Amp and cabinet (built-in or your impulse response).
- Dynamics: transient shaper, compressor. Modulation: phaser, flanger, frequency shifter. Ensemble: chorus, Leslie.
- Space: tape echo, plate and spring reverb. Output: cassette, vinyl, reel tape, underwater, master and limiter.

**MIDI files: player, play along and stems** ([midi-player.md](docs/midi-player.md))
- A small 90s-style player window: A-B loop, tempo 25-150 %, mute and solo per track, the last 8 files.
- Play along: pick a track as your part and follow it as a scrolling staff, in Play, Wait or Follow mode.
- MT-32, Gravis UltraSound and OPL3 play whole General MIDI files with their own instruments (Multitimbral).
- **Render Stems:** one stereo WAV per track, each with its own preset and engine, faster than real time, plus an
  optional mix of all of them.

**Performance**
- Arpeggiator, step sequencer, 8 macros, MIDI Learn on every knob and on the transport buttons, undo/redo.
- Transpose and a scale quantizer (14 scales) under the on-screen keyboard; the computer keyboard plays too.
- Fine tune, Scala `.scl`/`.kbm` microtuning and MTS-ESP.
- Audio input through the effects, or as the modulator of a vocoder (VP-330 with 10 bands, 16 or 32 bands).
- Transport: record the session to WAV, play audio files, import and export MIDI (SysEx too).

**Presets**
- 464 factory presets, including recreations of well-known instruments from the 60s to the 90s.
- A searchable browser with filters by category and engine, favorites on keys 1-9, export and import of user presets.
- Every preset says how to play it, and the keyboard greys out the keys outside its range.

**Display and interface**
- Oscilloscope, spectrum, spectrogram, goniometer, VU meters, an Arcade game played by the music and a 90s Demo mode,
  in modern, green or amber phosphor style.
- Collapsible blocks, floating windows, tooltips on every control, and Reset / Set value / MIDI Learn on right-click.
- Skins: Vintage 60s (default), Modern, Phosphor Green, Phosphor Amber and Brushed Metal.

**Your own content**
- Samples (WAV, AIFF, FLAC), SoundFont zones, SNES BRR/SPC, DX7 SysEx banks, impulse responses, MT-32 ROMs, GUS patches.
  KoniSynth includes none: you supply them.
- 8 user rhythms for the Drum Machine, the Omnichord and the VL-1, as MIDI files ([user-rhythms.md](docs/user-rhythms.md)).
- Controller profiles (Oxygen Pro Mini, Casio CT-S400, Yamaha PSR-EW425, Yamaha reface YC) and a setup assistant.

## Philosophy

- **Faithful by default.** Emulated engines sound like the original hardware, imperfections included; smoother
  behaviour is an option, off by default.
- **Measure, then listen.** Changes are measured (spectrum, level, pitch, timing) and, when unclear, compared with a
  recording; recreated presets were chosen in A/B/C listening tests.
- **Reproducible.** Randomness (drift, noise, tolerances) is seeded per project: a bounce sounds like what you heard.
- **Low latency.** No added latency by default: the limiter's look-ahead is optional and off.

## Installation

Download the zip for your system from the releases.

### Windows (x64)

| File | Copy to |
|---|---|
| `KoniSynth.vst3` (folder) | `C:\Program Files\Common Files\VST3\` (copy the whole folder) |
| `KoniSynth.clap` | `C:\Program Files\Common Files\CLAP\` (create it if needed) |
| `KoniSynth.exe` | anywhere (standalone application) |

No Visual C++ Redistributable is needed. If your DAW does not find the plugin:
1. Check the folder and rescan.
2. If the zip came from a browser or e-mail, right-click the zip **before extracting** → *Properties* → **Unblock**.
3. Windows 11 **Smart App Control** blocks unsigned DLLs (a `.vst3` is a DLL). The binaries are not code-signed.

### Linux (x86-64)

| File | Copy to |
|---|---|
| `KoniSynth.vst3` (folder) | `~/.vst3/` |
| `KoniSynth.clap` | `~/.clap/` |
| `KoniSynth` | anywhere (standalone application; `chmod +x KoniSynth`) |

Built on Ubuntu 24.04; needs glibc 2.38 or newer. Runtime libraries: `libasound2`, `libfreetype6`, `libfontconfig1`,
`libx11-6`, `libxext6`, `libxrandr2`, `libxinerama1`, `libxcursor1`; JACK is optional.

### macOS

Not available yet: building requires a Mac with Xcode. See [docs/BUILDING.md](docs/BUILDING.md).

## Getting started

1. **Engine** (top bar): pick one; it loads its most representative preset.
2. **Play:** the on-screen keyboard (KEYS), the PAD tab (Theremin, Stylophone) or any MIDI keyboard. In the standalone,
   *Options → Audio/MIDI config...* selects the devices. Computer keyboard: A row = white keys, Q row = black keys.
3. **Presets:** ◀ ▶ to step, the list to choose, the magnifier for the browser, the disk icons to save.
4. **Tweak:** hover any control for its help; right-click it for MIDI Learn, Set value and Reset.
5. **Menu** (*Options* in the standalone, right-click the *KoniSynth* title in the plugin): MIDI, Tuning, Randomness,
   Skin, Agent (MCP) and About.

The preset browser: search by the start of words (`bass nes`), filter by source, category and engine, click to listen.
`(s)` marks a preset that plays your own sample, `(π)` one that uses Math Models. Each engine manual in
[docs/engines](docs/engines) explains its controls.

## Agent control (MCP)

An AI agent can read and set parameters, browse and save presets, play and listen to KoniSynth through the
[Model Context Protocol](https://modelcontextprotocol.io).

1. **Once:** open *Agent (MCP)...*, get the bridge (*Copy install command*, or the `KoniSynth-mcp` folder of the release
   zip) and register it in your agent (*Register in agent...*). Python 3.10+ is required.
2. **Every time:** turn *Agent control* on and ask your agent to connect. The **MCP** LED in the top bar shows it is on.

KoniSynth listens only on `127.0.0.1` while Agent control is on, and never connects to the internet. Details and tools:
[Agent/KoniSynth-mcp/README.md](Agent/KoniSynth-mcp/README.md).

## Building from source

CMake 3.22+ and a C++20 compiler (MSVC 2022+ or GCC 13+); CMake fetches JUCE 9.0.4 and clap-juce-extensions.
Steps for Windows, Linux and macOS: [docs/BUILDING.md](docs/BUILDING.md).

## Tests

`python Tests/run_all.py` needs the Release build of `KoniSynth_VST3` and `KoniBench`, and Python with numpy.

| Suite | What it checks |
|---|---|
| `test_units.py` | Undo/redo, transport (WAV bit-exact, MIDI timing), play along, Scala tuning, saturation |
| `test_regression.py` | Bit-exact renders of 12 engines with a fixed seed against stored hashes |
| `test_filters.py` | Filter responses and slopes, ladder self-oscillation, stability and quality modes, Clean Drive |
| `test_voice.py` | Per-voice saturation, analog VCA, microtuning, plucked string, Osc 2 wavetable and sample, effects order |
| `test_midi_learn.py` | MIDI Learn of parameters and actions stored in the state |
| `test_clap.py` | clap-validator (when `CLAP_VALIDATOR` points to it) |
| `test_mt32.py` | MT-32 without valid ROMs is silent and safe; with `KONI_MT32_ROMS_REAL` it checks that it sounds |
| `test_gus.py` | Gravis UltraSound with synthetic patches: patch reader, GF1 pitch, output rate by active voices, vibrato |
| `test_ui_reach.py` | Every parameter and every engine is reachable from the interface |
| `test_rhythms.py` | A user rhythm with the same pattern as a factory one sounds the same |
| `test_panic.py` | PANIC silences every engine and effect tail |
| `test_extremes.py` | Synth phases at extreme pitch and modulation, and no stuck notes after All Notes Off |

With `--live`, two more suites drive the real standalone: preset versioning (`test_preset_live.py`) and Render Stems
(`test_stems_live.py`: per-track filtering, faster than real time, the mix is the exact sum, reproducible bytes).

The VST3 passes [pluginval](https://github.com/Tracktion/pluginval) at strictness level 10, and the CLAP passes
[clap-validator](https://github.com/free-audio/clap-validator) 0.4.1 with no failures. `KoniBench` (in `Bench/`) renders
the plugin offline from the command line, for measurements.

## License

KoniSynth is free software under the **GNU Affero General Public License v3.0** ([LICENSE](LICENSE)). Copyright (C) 2026
@k0n1m4k1. Third-party code (JUCE, Nuked-OPLL, Munt, msfa, MTS-ESP, TDA8425_emu, YM7128B_emu...) and its licences are
listed in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

Trademarks (Roland, Yamaha, Casio, Nintendo, Sega, Dubreq, Suzuki...) belong to their owners and only describe what each
engine emulates. No ROM, sample or recording from those products is included.

## Credits

Design and direction: **@k0n1m4k1**. Developed with the help of Claude (Anthropic). Hardware research credits (Ken Shirriff's
DX7 analysis, blargg's SNES notes, Nuke.YKT, the Munt team, PolyValens' VL-1 emulator, Chordian's Omnichord data and many
others) are cited in the source and in THIRD-PARTY-NOTICES.txt.
