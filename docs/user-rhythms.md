# User rhythms

Three engines have a bank of **8 user rhythms** next to their factory patterns: **Drum Machine**, **Omnichord** and
**Casio VL-1**. The slots appear as **User 1** to **User 8** at the end of the engine's rhythm menu. Each slot is a
standard MIDI file: you draw it in the rhythm editor (**Edit Rhythms...** in the header of the rhythm section) or write it in any
sequencer, and KoniSynth plays it with the engine's own sounds and tempo.

A preset or a project stores only which slot is selected (for example *User 3*), not the rhythm itself: the slot plays
whatever its file contains at that moment. An empty slot plays nothing.

## Where the files are

Each engine has its own folder inside the KoniSynth settings folder; the files are named `User 1.mid` to `User 8.mid`.

| System | Folder |
|---|---|
| Windows | `%APPDATA%\KoniSynth\rhythms\<engine>\` |
| Linux | `~/.config/KoniSynth/rhythms/<engine>/` (or `$XDG_CONFIG_HOME/KoniSynth/rhythms/<engine>/` if that variable is set) |
| macOS (no build yet) | `~/Library/KoniSynth/rhythms/<engine>/` |

`<engine>` is `Drum Machine`, `Omnichord` or `Casio VL-1`. The editor's **Open folder** button opens it (and creates it if
needed).

KoniSynth checks the files once per second (size and modification time) and reloads a bank when one of its files
changes, so a rhythm saved from another program is picked up while it plays. The rhythm editor also rereads a file
changed by another program, as long as it has no unsaved edits of its own.

## Rhythm editor (Edit Rhythms...)

The editor shows one row (lane) per sound and one column per step; the step numbers mark the beats, and a 32-step rhythm
shows a line between its two bars.

| Control | What it does |
|---|---|
| Slot menu | Which user rhythm you edit (User 1-8). It opens on the slot that is playing, or User 1. |
| Name | Name of the rhythm, stored in the MIDI file (track name). |
| Length menu | 16 steps (4/4), 32 steps (2 bars), 12 steps (waltz 3/4) and, on the Drum Machine and Omnichord, 12 steps (shuffle: triplet eighths). Changing it keeps the hits that still fit. |
| Copy from... | Replaces the grid with one of the engine's factory rhythms, as a starting point. |
| Clear | Empties the grid. |
| Play it / Stop | Selects this slot in the engine's rhythm menu, or sets the rhythm back to Off. |
| Open folder | Opens the folder with the rhythm files. |

- **Click** a cell to step through its values: normal, accent, soft, empty. On the VL-1 a cell is only on or off. On
  the Omnichord **Bass** lane: Root, Low (the root one octave down), 5th, 3rd, empty.
- **Right-click** empties a cell; **dragging** paints the value of the first click.
- Every change is saved to the slot's file 300 ms after the last edit (and when you switch slot or close the editor);
  the status line shows the file path, or how many notes of a loaded file the engine cannot play.
- While the edited slot is the one playing, its current step is highlighted (on the Drum Machine, only when + Pattern
  is Off).

Lanes per engine:

| Engine | Lanes |
|---|---|
| Drum Machine | Bass Drum, Snare, Rim Shot, Hand Clap, Closed Hat, Open Hat, Cymbal, Cowbell, Low Tom, High Tom, Low Conga, Maracas, Claves, Tambourine, Guiro. Lanes the selected kit does not have are dimmed (they stay in the file and play with another kit). |
| Omnichord | The drum lanes of its rhythm section (the CR-78 voices: all of the above except Hand Clap) and **Bass**. |
| Casio VL-1 | Pock, Peek and Noise. One sound per step: setting one clears the others on that step. |

## File format

KoniSynth writes a type 0 MIDI file at 96 ticks per quarter note. When reading, it accepts type 0 or type 1 (all tracks
are merged) at any resolution; SMPTE time code is not supported. The tempo stored in the file is ignored: the engine
plays at its own tempo.

**Drums: MIDI channel 10**, General MIDI drum notes, the same as the engine's keyboard map. Each note-on is moved to the
nearest step.

| Lane | Note written | Also accepted |
|---|---|---|
| Bass Drum | 36 (C2) | 35 |
| Snare | 38 (D2) | 40 |
| Rim Shot | 37 (C#2) | - |
| Hand Clap | 39 (D#2) | - |
| Closed Hat | 42 (F#2) | 44 |
| Open Hat | 46 (A#2) | - |
| Cymbal | 49 (C#3) | 51, 52, 55, 57, 59 |
| Cowbell | 56 (G#3) | - |
| Low Tom | 45 (A2) | 41, 43 |
| High Tom | 50 (D3) | 47, 48 |
| Low Conga | 64 (E4) | 62, 63 |
| Maracas | 70 (A#4) | 58 (the Mini Pops 7 plays this lane on its quijada) |
| Claves | 75 (D#5) | - |
| Tambourine | 54 (F#3) | - |
| Guiro | 73 (C#5) | 74 |

**Levels** come from the velocity: 1-75 soft, 76-110 normal, 111-127 accent (KoniSynth writes 57, 95 and 127). If two
notes fall on the same step of a lane, the louder one is kept. The VL-1 has no levels: every hit plays the same.

**Grid.** KoniSynth stores the exact grid in a text event (`KoniSynth rhythm: 12 steps, 3 per beat`), which takes
precedence. Without it, the grid is deduced:

| File | Grid |
|---|---|
| Time signature 3/4 | 12 sixteenths (waltz) |
| Every note on a triplet eighth, and at least one off the sixteenth grid | 12 triplet eighths (shuffle; not on the VL-1) |
| Anything else | 16 sixteenths, or 32 if a note falls on step 17 or later |

**Omnichord bass: MIDI channel 2**, written in C and transposed by the engine to the chord you hold:

| Note | Bass |
|---|---|
| C3 (48) | root |
| C2 (36) | root one octave down |
| G3 (55) | fifth |
| E3 (52) | third |

Other notes on channel 2 are ignored. As with the factory rhythms, the bass plays only while a chord key is held; a user
rhythm without bass notes leaves the bass silent while it runs.

**Casio VL-1:** channel 10, notes 36 (Pock), 37 (Peek) and 42 (Noise), one sound per sixteenth (35 and 44 are
read as Pock and Noise too). If a step has more than
one, the first of Pock, Peek, Noise is kept. On an empty step the previous sound keeps ringing, as with the factory
rhythms. A step lasts 8 ticks of the VL-1 clock (one beat = 32 ticks), so the rhythm follows the VL-1 Tempo.

Notes outside the map, beyond the rhythm's length or on lanes the engine does not have are ignored. A file with no
playable note leaves the slot empty; the editor's status line says why.

## How each engine plays them

- **Drum Machine:** User 1-8 are in both **Pattern** and **+ Pattern**, after the factory patterns (the menus are
  alphabetical; the user slots stay at the end, in order). They follow Tempo, Swing (4 steps per beat only), the host
  tempo and the two-pattern combination exactly like the factory patterns.
- **Omnichord:** User 1-8 are at the end of the **Rhythm** menu; the drums use the CR-78 voices and the bass lane drives
  the auto bass.
- **Casio VL-1:** User 1-8 follow Beguine in the **Rhythm** menu and run on the VL-1's own clock (Tempo -9 to +9).
