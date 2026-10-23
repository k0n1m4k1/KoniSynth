# KoniSynth-mcp

MCP server (stdio) that lets an agent such as Claude control KoniSynth: read and set parameters, browse, load and
save presets. It talks to KoniSynth's local control server (`Source/Agent/AgentServer.h`).

## Requirements

- KoniSynth 0.6+ with **Agent control** enabled: right-click the *KoniSynth* title (or *Options* in the standalone)
  > *Agent control (localhost)*. It is off by default; while off, KoniSynth opens no port.
- Python 3.10+ (tested with 3.14.3), the official MCP SDK (`mcp`, MIT) and `numpy` (BSD-3-Clause), pinned in `requirements.txt`.

```
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
```

## Register it in Claude Code

The easiest way: in KoniSynth open *Agent (MCP)...* (Options, or right-click the title) and use *Copy install command* and
*Copy Claude Code command*: they contain the paths of your machine. By hand (user scope, every project), with `<dir>` the
folder of this README:

```
claude mcp add KoniSynth --scope user -- "<dir>\.venv\Scripts\python.exe" "<dir>\konisynth_mcp.py"     # Windows
claude mcp add KoniSynth --scope user -- "<dir>/.venv/bin/python" "<dir>/konisynth_mcp.py"               # Linux
```

## Tools

| Tool | What it does |
|---|---|
| `status` | Engine, preset and unsaved-changes flag |
| `list_instances` | Running KoniSynth instances with Agent control (one port each, 47800-47899) |
| `describe_parameters(match)` | Id, name, value, range, options and help of matching parameters (without `match`: groups and counts) |
| `get_parameters(ids)` | Current displayed values |
| `set_parameters(values)` | Batch change; text in the parameter's format ("2 kHz", "Saw") or real numbers; one undo step |
| `list_presets(category, match)` / `load_preset` / `save_preset` | Presets |
| `play_notes(notes)` / `stop_notes` | Play a note, a chord ("C4 E4 G4") or a timed list through the virtual keyboard |
| `audition(notes, duration, tail)` | Play and measure: pitch and pitch curve (glide, vibrato, drift), level, envelope, spectrum (centroid, rolloff, flatness, octave bands, harmonics), stereo |
| `listen(seconds)` | Measure the last seconds of output, whatever is sounding (also the user playing) |
| `compare(reference_wav)` | Differences to a reference WAV supplied by the user (only audio you own or with a free license) |
| `generate(style, engine, count, amount, seed, reference_wav)` | New sounds with knowledge: factory preset of the style + mutation of the parameters that matter for its engine and the effects + style rules + listening check; ranked by similarity with a reference. Reproducible with `seed` |
| `use_candidate(n)` / `mutate(amount)` | Load a generated candidate / vary the current sound |

## How it connects

Each KoniSynth instance with Agent control writes `%APPDATA%\KoniSynth\agent\<port>.json` with its port and a random
128-bit token, listens only on `127.0.0.1` and removes the file when it stops. Every request carries the token. The
server keeps one connection open and reconnects if KoniSynth restarts.

## Test

`test_e2e.py` starts the standalone with Agent control enabled (backing up and restoring the settings files), drives
every tool through a real MCP client and checks the results; `test_f3.py` checks the listening tools against known
values (A4 = 440 Hz, cutoff vs brightness, a 440 Hz sine reference, no stuck notes):

```
.venv\Scripts\python test_e2e.py
.venv\Scripts\python test_f3.py
.venv\Scripts\python test_f4.py
```

The analysis (`analysis.py`) needs the audio device running in the standalone (or the host playing).
