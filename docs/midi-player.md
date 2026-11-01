# MIDI Music Player, play along and Render Stems

Open the player with the **...** button next to PLAY in the transport (right of the BPM display). It plays any MIDI file
through the current engine, lets you play one of its parts yourself, and renders its tracks to WAV files.

## The player window

- A small 90s-style window in the colours of the skin. Double-click its title to shrink it to one line.
- **On top** (the arrow box in the title bar): lit, the player stays above other windows while KoniSynth is in front;
  off, other windows can cover it. The choice is remembered.
- Drag it next to the main window's edge to dock it: it then moves with the main window.
- It shows time, title, the current BPM of the file and a mini spectrum.
- Volume, tempo (25-150 %, sticks at 100 %) and a seek bar. Seeking keeps each channel's program and controllers.
- A-B loop: SET A, SET B, CLEAR. **RECENT**: the last 8 files.

## Tracks

The list shows one row per track: number, name, channel, the General MIDI instrument the file asks for, the number of
notes and their range. Each row has mute (**M**) and solo (**S**).

- Without Multitimbral, every track plays with the current sound.
- **Roland MT-32**, **Gravis UltraSound** and **OPL3** with Multitimbral on play each track with its own instrument; the
  Drum Machine with Ch 10 on plays the drum track. When that switch is off, a chip in the player offers to turn it on.
- **GM>MT-32**: General MIDI files are converted for the MT-32 on the fly (program mapping, flipped panning and a GM-like
  set-up of the unit). It turns on by itself when a file has no MT-32 SysEx.

## Play along

Pick one track in the list as your part. It is muted, your notes go to its channel, and the display shows it as a staff
scrolling from right to left, with the next note to play in large letters.

| Mode | What the file does |
|---|---|
| Play | Keeps its tempo |
| Wait | Stops at each of your notes until you play it |
| Follow | Jumps ahead with you if you play early |

A one-bar count-in starts the file.

## Render Stems

**STEMS** (in the track list header) opens a dialog that renders the loaded file to WAV files, faster than real time.
You do not have to listen to the song while it renders.

| Part of the dialog | What it does |
|---|---|
| One row per track | Name, channel and instrument, notes and range, and the sound for that track |
| Sound | **Skip** (default: not rendered), **Current sound**, or any preset, grouped by category with its engine |
| Rate | 44.1, 48, 88.2 or 96 kHz |
| Folder | Where the files go (default `Documents/KoniSynth/Stems`); remembered |
| One multitimbral instance (GM) | The whole file through one instance with the current sound: one WAV with every part |
| Also mix all parts into one WAV | When the tracks are done, adds them together into `<file> - Mix.wav` |

How the files come out:
- One stereo WAV per track, 32-bit floating point: no clipping above 0 dBFS.
- Named `<file> - 01 <track>.wav`, `<file> - 02 <track>.wav`...
- All start at 0 and have the same length (shorter ones are padded with silence), so they line up in a DAW.
- Each track ends after its last note plus its tail: until 0.5 s below -90 dBFS, at most 5 s (30 s with Freeze).
- The mix adds the tracks at their own level, without normalizing.

How it works: each track gets its own KoniSynth instance, with your current settings and the chosen preset, playing only
that track. The tracks render in parallel, one per processor core minus one. The sound you are playing live is not
affected. Randomness is seeded, so the same render gives the same bytes.

Tempo-synced effects (echo, LFO, arpeggiator) follow the tempo set in the preset, not the tempo of the MIDI file.

The agent protocol has the same render as the `render_stems`, `render_status` and `render_cancel` commands (see
`Source/Agent/AgentServer.h`); the MCP bridge does not offer it as a tool yet.
