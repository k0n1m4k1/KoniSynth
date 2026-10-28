# KoniSynth

**Version 1.0.1-beta** · by **@k0n1m4k1** 🇪🇸 · GNU AGPLv3

[![M8ven Score](https://m8ven.ai/badge/mcp/k0n1m4k1-konisynth-knlbk4?v=1c9d31871080c40a85dd776478b0d476)](https://m8ven.ai/mcp/k0n1m4k1-konisynth-knlbk4?s=readme)

KoniSynth is an experimental synthesizer laboratory for the sounds of the 60s to the 90s: fifteen engines, from an analog-style
synth of its own to faithful emulations of the Casio VL-1, the Stylophone, the Omnichord, classic drum machines, the Yamaha
DX7, the NES and Game Boy, the Polivoks, the ANS, the Mega Drive, the SNES, the N64, the AdLib/Sound Blaster OPL chips and
the Roland MT-32. It runs as a **VST3** and **CLAP** plugin and as a **standalone** application on **Windows** and **Linux**.

![KoniSynth](assets/cover.png)

## Philosophy

- **Faithful by default.** Every emulated engine aims to sound like the original hardware, *bugs and imperfections included*:
  the DX7's 12-bit phase and its floating DAC, the first-generation MT-32 digital overflow, the YM2612 "ladder" effect, the
  NES triangle that freezes on its last step, a non-oversampling DAC that holds each sample... Anything that makes an engine *nicer* than the
  original (smoother ramps, cleaner resampling, continuous velocity where the machine had two levels) exists only as an option,
  and it is **off by default**.
- **A synth with its own personality.** The *Synth* engine is not an emulation: it is KoniSynth's own eight-voice polyphonic
  design (Mono and Legato too) with Moog traits: a 4-pole ladder low-pass with three qualities against aliasing, the Minimoog
  Tri-Saw and an analog triangle measured on two Model D units. Around them: minBLEP oscillators with up to 7-voice unison,
  the Prophet-5 and Juno-106 waveform mixes, a Buchla 259-style complex oscillator (through-zero FM, ring, wavefolder; Osc 2
  or Osc 3 can be its modulator), a wavetable (Serum format) and a sample player for Osc 2 as well, a Karplus-Strong plucked
  string, 14 filter models (ladder, SVF, MS-20, comb, dual), per-voice saturation before or after the filter, analog RC
  envelopes, 1/f drift with per-note or per-voice-card tolerances, an analog VCA (click and bleed), two tempo-synced LFOs,
  an 8-slot modulation matrix, MPE and polyphonic aftertouch, and AVX2 for the unison.
- **Measure, don't guess.** Behaviour is checked against measurements (spectra, levels, pitch, timing) and, where it was
  unclear, against reference recordings. Sources for the hardware data are cited in the code comments.
- **Listen, too.** Presets recreated from a reference recording went through A/B/C listening tests: two or three candidates
  rendered with the same notes and at the same level as the reference, chosen by ear. A recipe that met every measured
  tolerance but did not sound right was discarded; the measurements of the chosen one are noted next to it in the code.
- **Reproducible.** Randomness (drift, noise, analog tolerances) is seeded per project: a bounce sounds like what you heard.
- **Low latency.** No added latency by default: the limiter's look-ahead is optional and off.

## Engines

| Engine | What it is | Manual |
|---|---|---|
| Synth | KoniSynth's own analog-style synth: 4 sources (oscillator, wavetable, sample, plucked string), Osc 2 (classic and mixed waves, wavetable or its own sample), Osc 3, noise, complex oscillator, 14 filters, matrix | [synth.md](docs/engines/synth.md) |
| Theremin | Physical model of the theremin (pitch and volume antennas) with its own pad; also playable from MIDI | [theremin.md](docs/engines/theremin.md) |
| Casio VL-1 | The 1979 VL-Tone: its sounds, ADSR digits, rhythms and LCD | [casio-vl1.md](docs/engines/casio-vl1.md) |
| Stylophone | The 1968 stylus organ (20 keys) and the experimental Gen X-2, with a gold-plate pad | [stylophone.md](docs/engines/stylophone.md) |
| Omnichord | Suzuki Omnichord: chord buttons, strum plate and rhythms | [omnichord.md](docs/engines/omnichord.md) |
| Drum Machine | Mini Pops 7, CR-78 and TR-808 voices with patterns and step lights | [drum-machine.md](docs/engines/drum-machine.md) |
| FM 6-Op | Yamaha DX7 (OPS chip arithmetic) with its 32 algorithms, pitch envelope and display; loads DX7 SysEx banks | [fm-6op.md](docs/engines/fm-6op.md) |
| 8-Bit/DPCM | 2A03 (NTSC/PAL), Famicom with VRC6, Game Boy / Color, with frame macros | [chip-8bit.md](docs/engines/chip-8bit.md) |
| Polivoks | The Soviet Polivoks synthesizer | [polivoks.md](docs/engines/polivoks.md) |
| ANS | The photoelectronic ANS synthesizer: draw the score | [ans.md](docs/engines/ans.md) |
| Mega Drive | YM2612 / YM3438 FM + SN76489 PSG + DAC | [mega-drive.md](docs/engines/mega-drive.md) |
| SNES | S-DSP: BRR samples, Gaussian interpolation, echo with FIR | [snes.md](docs/engines/snes.md) |
| N64 | RSP + libultra: VADPCM, resampling and its effects | [n64.md](docs/engines/n64.md) |
| OPL | YM3812 (OPL2), YMF262 (OPL3), YM2413 (OPLL) and Konami VRC7 | [opl.md](docs/engines/opl.md) |
| Roland MT-32 | MT-32 / CM-32L through Munt, with **your own ROMs**; its LCD and named patch list | [mt32.md](docs/engines/mt32.md) |

## Features

- **Synth engine:** besides the four summed sources, a second group of tabs (Osc 2, Osc 3, Noise), with Mute and Solo on
  every source; Osc 2 can play its own wavetable or its own sample file, and Osc 2 or Osc 3 can be the modulator of the
  complex oscillator, through a low-pass filter if you want softer FM. Wavetables in the Serum format (2048 samples per
  frame, or the frame size its `clm` chunk declares). Waveforms include the Minimoog Tri-Saw and the mixed waves of a Prophet-5 oscillator
  and of the Juno-106 (Saw+Pulse), and every triangle can be made lopsided and rounded like an analog one (Tri Sym, Tri Rnd).
  The ladder filter has three qualities against aliasing with drive (Classic, Balanced 2x, High Quality 4x), and the
  per-voice saturation can sit after or before it. A strip above the sources shows the current signal path, and its
  displays are editable: the wave display (each source, the mix and the result after the filter, with handles for pulse
  width, wavetable position, sample start and loop...; with the Complex on, the complex oscillator's output), the filter
  response (cutoff and resonance), the envelopes, the pitch envelope and the LFOs. The emulated engines show their own
  envelopes the same way, with their real curves and steps (Polivoks, Mega Drive PSG, N64, Casio VL-1, Gen X-2 and each
  OPL operator); the voice saturation, Crush and Tube show their transfer curves, and the compressor's curve is editable
  (threshold, makeup, ratio) with the limiter ceiling on it.
- **Effects chain** (each group can be folded or opened in its own window), by default in this order; drag a group's
  tab onto another (or use its right-click menu) to reorder the first six, saved with the preset; output stays last:
  color (bit crusher, formant filter, tube, parametric EQ with 6 to 48 dB/oct cuts) → amp and cabinet (built-in or your
  impulse response) → dynamics (transient shaper, compressor) → modulation (phaser, flanger, frequency shifter) → ensemble
  (chorus, Leslie) → space (tape echo, plate reverb, spring reverb) → output (media: cassette, vinyl, reel tape at three
  speeds, underwater; master; limiter).
- **Performance:** arpeggiator, step sequencer, 8 performance macros, MIDI Learn on every knob and on the transport and PANIC
  buttons, MPE and polyphonic aftertouch (Synth), undo/redo.
- **Keyboard and scales:** under the on-screen keyboard, Transpose (±24 semitones) and a scale quantizer: 14 scales (major,
  minor, modes, pentatonics, blues, whole tone...) on any key, with out-of-scale notes rounded to the scale (nearest, down
  or up) or left out; the keyboard marks the notes of the scale. The computer keyboard plays it too (octave and velocity
  keys, favorites on 1-9).
- **Tuning:** fine tune (±100 cents), Scala `.scl`/`.kbm` microtuning (saved with the project; a LED shows it is active) and
  MTS-ESP, in every engine.
- **Audio input and vocoder:** a microphone or line input can go through the effects chain, or be the modulator of a channel
  vocoder (VP-330 with 10 bands, 16 or 32 bands) with any engine as the carrier.
- **Transport:** record the session to WAV, play audio files, import (also SysEx) and export MIDI.
- **Presets:** 355 factory presets (including recreations of popular instruments from the 60s to the 90s), user presets,
  favorites in your own order (keys 1-9 load the first nine) and a searchable browser with filters by category and engine.
  Presets say how to play them (on the display and in the browser) and grey out the keys outside their recommended range.
  Presets that use your own files tell you when one is missing. User presets can be exported (one as `.xml`, all as a
  `.zip` bank) and imported from the browser.
- **Your own content:** samples (WAV, AIFF, FLAC), SoundFont zones, SNES BRR/SPC, DX7 SysEx banks, impulse responses, Roland
  MT-32 ROMs. None of them is included; you supply them.
- **User rhythms:** 8 rhythms of your own for the Drum Machine, the Omnichord (with its auto bass) and the Casio VL-1,
  drawn in a rhythm editor or in any sequencer: each one is a MIDI file (see [User rhythms](docs/user-rhythms.md)).
- **MIDI controllers:** setup profiles for the Akai Oxygen Pro Mini, Casio CT-S400, Yamaha PSR-EW425 and Yamaha reface YC;
  a Controller Setup assistant learns the knobs of your unit one by one and suggests the settings for it.
- **Display:** oscilloscope, spectrum, spectrogram, goniometer, VU meters and **Arcade** (a pixel-art shooter played by the
  music), in modern, green or amber phosphor style. Its bezel follows the skin and style (graphite, terminal casing or
  brushed steel), or is brass, or none; with a bezel the screen has CRT-style glass. BPM display and transport next to it. The VL-1, DX7 and MT-32
  engines also show their own front-panel display, and the Drum Machine its step lights.
- **Interface:** collapsible blocks, floating windows (handy on multi-monitor setups; a pin in the title bar keeps one on
  top), resizable window, tooltips on every control, right-click menu with Reset / Set value / MIDI Learn, skins (Default,
  Phosphor Green, Phosphor Amber, Brushed Metal; the choice is global and remembered). Knobs turn like real ones; choices of
  a few options are stepped switches, on/off choices are keys, and waveform menus and switches show the waveform. The
  transpose and scale bar goes with the on-screen keyboard. Red LEDs keep their colour with every skin.
- **Agent control (MCP):** an AI agent such as Claude can read and set parameters, browse and save presets, play and listen to
  KoniSynth (see [Agent control](#agent-control-mcp)).

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
3. Windows 11 **Smart App Control** blocks unsigned DLLs (a `.vst3` is a DLL): the standalone may run while the plugin does
   not load. The binaries are not code-signed.

### Linux (x86-64)

| File | Copy to |
|---|---|
| `KoniSynth.vst3` (folder) | `~/.vst3/` |
| `KoniSynth.clap` | `~/.clap/` |
| `KoniSynth` | anywhere (standalone application; `chmod +x KoniSynth`) |

Built on Ubuntu 24.04; needs glibc 2.38 or newer (most distributions from 2023 on). Runtime libraries: ALSA, FreeType, Fontconfig and X11 (`libasound2`, `libfreetype6`,
`libfontconfig1`, `libx11-6`, `libxext6`, `libxrandr2`, `libxinerama1`, `libxcursor1`); JACK is optional. The standalone
application and the VST3 have been tested by hand on Ubuntu.

### macOS

Not available yet: building requires a Mac with Xcode. See [docs/BUILDING.md](docs/BUILDING.md).

## Getting started

1. **Engine** (top bar): pick an engine; it loads its most representative preset (for example *808 Kit* for the Drum
   Machine, *FM E.Piano* for FM 6-Op). The Synth only switches engine; on 8-Bit, the Model menu loads its own preset.
2. **Play:** the KEYS tab (on-screen keyboard), the PAD tab (Theremin and Stylophone), or any MIDI keyboard.
   In the standalone, *Options → Audio/MIDI config...* selects the audio device and MIDI inputs. The computer keyboard
   plays the on-screen keyboard too: A row = white keys, Q row = black keys, Z / X = octave, C / V = velocity.
3. **Presets:** ◀ ▶ to step, the list to choose, the magnifier for the browser, the disk icons to save.
4. **Preset browser** (magnifier):
   - **Search** by the start of words in the name, category or engine (`bass nes`); case and accents do not matter.
   - **Filters** on the left: Source (All, ★ Favorites, Factory, User), Category and Engine, each with the number of
     presets it would leave.
   - Click a row or use the arrow keys to **listen**; double-click or Enter loads it and closes the browser.
   - **Favorites:** click the star. The first nine show their number next to the star, and keys **1-9** load them (with
     the KoniSynth window focused, outside text fields). To reorder them: drag the rows while the table is sorted by the
     star column, or right-click a favorite (Move to top / up / down / to end), or Alt + Up / Down.
   - **Import... / Export...** (next to the search): export the selected user preset (`.xml`, also from its right-click
     menu) or all your user presets as a `.zip` bank; import a `.xml` or a `.zip`. If a user preset with the same name
     exists you choose Replace, Skip or Cancel. The samples, impulse responses and wavetables a preset uses are not
     included: copy them too, or use Locate... when the preset asks for them.
5. **How to play each preset:** when a preset loads, the display shows its name and a hint for 8 seconds
   ("Bass line, C1-C3", "Slide on the PAD, or play C3-C6 legato"...); the browser shows the same hint when you hover a
   row and when you select it. Most presets also have a recommended range: the on-screen keyboard greys out the keys
   outside it, and it names the key under the mouse (C4 = MIDI 60). Right-click a key to change the range or edit the
   hint; Save or Save As stores them with the preset.
6. **Tweak:** hover any control for its help (every control has a tooltip). Right-click a control for MIDI Learn,
   Set value and Reset to default. Controls that have no effect with the current settings are dimmed, and still respond.
   Blocks fold with their tab.
7. **Menu** (*Options* in the standalone, right-click the *KoniSynth* title in the plugin): MIDI (channel, bend range,
   controller profiles), Tuning, Randomness, Skin, Agent (MCP) and About.

Each engine manual in [docs/engines](docs/engines) explains its controls and how to play it.

## Agent control (MCP)

KoniSynth can be driven by an AI agent through the [Model Context Protocol](https://modelcontextprotocol.io):

1. **Once:** in KoniSynth open *Agent (MCP)...* and get the bridge into `%APPDATA%\KoniSynth\KoniSynth-mcp` (Linux:
   `~/.config/KoniSynth/KoniSynth-mcp`): either run *Copy install command* in a terminal, which downloads the bridge of your
   KoniSynth version from this repository (its release tag) and installs its Python packages, or copy there the
   `KoniSynth-mcp` folder of the release zip and run the same command, which then only installs the packages. Then
   *Register in agent...* copies the command for Claude Code or Codex (run it too) or the JSON entry for any other MCP
   client. Python 3.10+ is required (on Debian/Ubuntu also `python3-venv`). KoniSynth itself never connects to the
   internet.
2. **Every time:** turn *Agent control* on in that dialog and ask your agent to connect. The agent starts the bridge itself.
   The **MCP** LED in the top bar, next to AVX2, is lit while Agent control is on; click it to open the dialog.

KoniSynth listens only on `127.0.0.1` (first free port in 47800-47899, announced with a random token); while it is off, no port
is open. Details and the list of tools: [Agent/KoniSynth-mcp/README.md](Agent/KoniSynth-mcp/README.md).

## Building from source

CMake 3.22+, a C++20 compiler (MSVC 2022+ or GCC 13+); JUCE 9.0.4 and clap-juce-extensions are fetched by CMake. Full steps for
Windows, Linux and macOS: [docs/BUILDING.md](docs/BUILDING.md).

## Tests

`python Tests/run_all.py` (needs the Release build of `KoniSynth_VST3` and `KoniBench`, and Python with numpy) runs:

| Suite | What it checks |
|---|---|
| `test_units.py` | Undo/redo, transport (WAV bit-exact, MIDI timing), Scala tuning, saturation |
| `test_regression.py` | Bit-exact renders of 12 engines with a fixed seed against stored hashes |
| `test_filters.py` | Filter responses and slopes (ladder, SVF, MS-20, comb, dual), ladder self-oscillation, stability and quality modes, Clean Drive |
| `test_voice.py` | Per-voice saturation (intermodulation, aliasing, level, position), analog VCA, microtuning, plucked string, Osc 2 wavetable and sample, Complex Mod LP, effects order, Serum `clm` frame size |
| `test_midi_learn.py` | MIDI Learn of parameters and actions stored in the state |
| `test_clap.py` | clap-validator (when `CLAP_VALIDATOR` points to it) |
| `test_mt32.py` | MT-32 without valid ROMs is silent and safe; with `KONI_MT32_ROMS_REAL` it checks that it sounds |
| `test_ui_reach.py` | Every parameter and every engine is reachable from the interface; one dropdown per section |
| `test_rhythms.py` | A user rhythm (MIDI file) with the same pattern as a factory one sounds the same on the Drum Machine, Omnichord (with its bass) and VL-1 |

The VST3 also passes [pluginval](https://github.com/Tracktion/pluginval) at strictness level 10. `KoniBench` (in `Bench/`)
renders the plugin offline from the command line, for measurements.

## License

KoniSynth is free software under the **GNU Affero General Public License v3.0** ([LICENSE](LICENSE)). Copyright (C) 2026
@k0n1m4k1. It uses JUCE (under the AGPLv3), Nuked-OPLL (GPL-2.0+), Munt mt32emu (LGPL-2.1+), formulas from msfa (Apache-2.0)
and the MTS-ESP client (0BSD); their notices and licence texts are in [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).
Trademarks (Roland, Yamaha, Casio, Nintendo, Sega, Dubreq, Suzuki...) belong to their owners and are used only to describe what
each engine emulates. No ROM, sample or recording from those products is included.

## Credits

Design and direction: **@k0n1m4k1**. Developed with the help of Claude (Anthropic). Hardware research credits (Ken Shirriff's
DX7 analysis, blargg's SNES notes, Nuke.YKT, the Munt team, PolyValens' VL-1 emulator, Chordian's Omnichord data and many
others) are cited in the source and in THIRD-PARTY-NOTICES.txt.
