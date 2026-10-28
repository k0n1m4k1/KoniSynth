# KoniSynth-mcp

MCP server (stdio) that lets an agent such as Claude control KoniSynth: read and set parameters, browse, load and
save presets. It talks to KoniSynth's local control server (`Source/Agent/AgentServer.h`).

## Requirements

- KoniSynth 0.6+ with **Agent control** enabled: right-click the *KoniSynth* title (or *Options* in the standalone)
  > *Agent control (localhost)*. It is off by default; while off, KoniSynth opens no port.
- Python 3.10+ (tested with 3.14.3; on Debian/Ubuntu also the `python3-venv` package), the official MCP SDK (`mcp`, MIT)
  and `numpy` (BSD-3-Clause), pinned in `requirements.txt`.

## Setup

KoniSynth keeps the bridge in one folder of your user profile:

| System | Bridge folder |
|---|---|
| Windows | `%APPDATA%\KoniSynth\KoniSynth-mcp` |
| Linux | `~/.config/KoniSynth/KoniSynth-mcp` |

Everything below is in KoniSynth's *Agent (MCP)...* dialog: click the **MCP** LED in the top bar (next to AVX2; lit while
Agent control is on), or use Options / right-click the title. It shows which bridge it found and whether its Python
environment is ready.

### 1. Get the bridge (once), in one of two ways

- **Download it automatically:** click *Copy install command* and paste it in a terminal (cmd, PowerShell or bash). It is
  a single Python line that downloads this folder from the release tag of your KoniSynth version (for example
  `v1.0.0-beta`) to the bridge folder, creates its Python environment (`.venv`), installs `requirements.txt` and writes the
  version to `version.txt`. KoniSynth itself never connects to the internet: the command you run does.
- **Copy it:** copy the `KoniSynth-mcp` folder of the release zip (the same version as your KoniSynth) into the bridge
  folder above, for example to use it offline. KoniSynth detects it there. Then run *Copy install command* as well: with
  the bridge already in place it downloads nothing and only creates the Python environment.

If you run KoniSynth from the unzipped release folder, the `KoniSynth-mcp` folder next to it is found too. For a bridge in
any other folder, use *Locate bridge...*.

### 2. Register it in your agent (once)

*Register in agent...* copies, with the paths of your machine:

- **Claude Code** or **Codex CLI:** a command; paste it in a terminal.
- **Other MCP clients** (Claude Desktop, Cursor, VS Code...): a JSON entry for their `mcpServers` configuration.

By hand, with `<dir>` the bridge folder:

```
claude mcp add KoniSynth --scope user -- "<dir>\.venv\Scripts\python.exe" "<dir>\konisynth_mcp.py"     # Claude Code, Windows
claude mcp add KoniSynth --scope user -- "<dir>/.venv/bin/python" "<dir>/konisynth_mcp.py"               # Claude Code, Linux
codex mcp add KoniSynth -- "<dir>\.venv\Scripts\python.exe" "<dir>\konisynth_mcp.py"                     # Codex CLI, Windows
```

```json
{ "mcpServers": { "KoniSynth": { "command": "<dir>\\.venv\\Scripts\\python.exe", "args": ["<dir>\\konisynth_mcp.py"] } } }
```

The environment by hand, if you prefer it to the install command:

```
cd <dir>
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt     # Windows (Linux: .venv/bin/python)
```

### 3. Every time

Turn *Agent control* on in the dialog and ask your agent to connect: the agent starts the bridge itself.

### After updating KoniSynth

Get the bridge of the new version: run *Copy install command* again (the dialog warns when the downloaded bridge is from
another version and the command then downloads the new one), or copy the new `KoniSynth-mcp` folder over the old one. The
registration in your agent stays valid: the paths do not change.

## Tools

| Tool | What it does |
|---|---|
| `status` | Engine, preset and unsaved-changes flag, plus the clock: sample rate, block, latency, host tempo/position when the host gives them, effective tempo and its source, engine rhythm clock, active note transforms (transpose, scale, sequencer, arpeggiator) |
| `list_instances` | Running KoniSynth instances with Agent control (one port each, 47800-47899) |
| `describe_parameters(match)` | Id, name, value, range, options and help of matching parameters (without `match`: groups and counts) |
| `get_parameters(ids)` | Current values: displayed text, exact real value, normalized position (0-1) and unit |
| `set_parameters(values)` | Batch change; text in the parameter's format ("2 kHz", "Saw"), real numbers, true/false or `{"normalized": x}`. The whole batch is validated first (an invalid value changes nothing); one undo step |
| `list_presets(category, match)` / `load_preset` / `save_preset` | Presets (the list gives the engine, how to play it and the recommended range) |
| `play_notes(notes)` / `stop_notes` | Play a note, a chord ("C4 E4 G4") or a timed list, sample-accurate on the audio clock; times in seconds or, with `bpm`, in beats (an invalid note rejects the whole list) / release all notes and cancel the scheduled ones |
| `audition(notes, duration, tail, bpm)` | Play and measure (up to 10 s, from the notes' time 0): pitch and pitch curve (glide, vibrato, drift), level, envelope, spectrum (centroid, rolloff, flatness, octave bands, harmonics), stereo |
| `listen(seconds)` | Measure the last 0.05-10 s of output, whatever is sounding (also the user playing). Each capture is its own WAV; the result says how much was captured |
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
values (A4 = 440 Hz, cutoff vs brightness, a 440 Hz sine reference, no stuck notes); `test_exact.py` checks exact
values and units, batch validation, invalid notes and the capture timing; `test_state.py` checks the clock and tempo
state:

```
.venv\Scripts\python test_e2e.py
.venv\Scripts\python test_f3.py
.venv\Scripts\python test_f4.py
```

The analysis (`analysis.py`) needs the audio device running in the standalone (or the host playing).
