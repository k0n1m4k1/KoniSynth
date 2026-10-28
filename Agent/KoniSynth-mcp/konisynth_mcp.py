"""KoniSynth-mcp: servidor MCP (stdio) que conecta un agente con KoniSynth.

Habla con el servidor de control local del plugin (Source/Agent/AgentServer.h, fase F1): TCP en 127.0.0.1, una línea
JSON por petición, autenticada con el token del fichero de anuncio %APPDATA%\\KoniSynth\\agent\\<puerto>.json.
El control por agente debe estar activado en KoniSynth (clic derecho en el título > Agent control).

Uso: python konisynth_mcp.py   (lo lanza el cliente MCP; ver README.md)
"""
from __future__ import annotations

import json
import os
import socket
import threading
import time
from pathlib import Path
from typing import Any

from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError  # su mensaje llega al agente (RuntimeError se oculta)
from mcp.types import ToolAnnotations

import analysis
import generator

# Anotaciones MCP de las tools (pistas para el cliente: qué puede ejecutar sin pedir confirmación). Todas son de mundo
# cerrado: el puente solo habla con KoniSynth en 127.0.0.1 y lee ficheros locales.
#   READ: no cambia el sonido ni los presets (listen escribe solo su WAV de captura temporal).
#   REPLACE: sustituye valores o el sonido actual (deshacible); repetirla igual no cambia más. WRITE: idem, pero cada
#   llamada cambia de nuevo (aleatoria o relativa al valor actual). ADD: solo añade (save_preset falla si el nombre
#   existe). PLAY: suena, sin cambiar nada guardado. STOP: suelta todas las notas (también las del usuario).
READ = ToolAnnotations(read_only_hint=True, open_world_hint=False)
REPLACE = ToolAnnotations(read_only_hint=False, destructive_hint=True, idempotent_hint=True, open_world_hint=False)
WRITE = ToolAnnotations(read_only_hint=False, destructive_hint=True, idempotent_hint=False, open_world_hint=False)
ADD = ToolAnnotations(read_only_hint=False, destructive_hint=False, idempotent_hint=True, open_world_hint=False)
PLAY = ToolAnnotations(read_only_hint=False, destructive_hint=False, idempotent_hint=False, open_world_hint=False)
STOP = ToolAnnotations(read_only_hint=False, destructive_hint=False, idempotent_hint=True, open_world_hint=False)

def _app_data() -> Path:
    """Carpeta de datos de la aplicación como la resuelve JUCE (userApplicationDataDirectory)."""
    if os.name == "nt":
        return Path(os.environ.get("APPDATA", str(Path.home())))
    if os.uname().sysname == "Darwin":
        return Path.home() / "Library"
    return Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))


ANNOUNCE_DIR = _app_data() / "KoniSynth" / "agent"
TIMEOUT_S = 10.0

INSTRUCTIONS = """\
KoniSynth is an experimental 70s/80s synthesizer (VST3/CLAP/standalone) with several engines selected by the
parameter `engine` (Synth, Theremin, Casio VL-1, Stylophone, Omnichord, Drum Machine, FM 6-Op, 8-Bit/DPCM (NES/Game Boy),
Polivoks, ANS, Mega Drive, SNES, N64, OPL, Roland MT-32 with the user's ROMs). It values fidelity to the original hardware, quirks included.

How to work with it:
- Call `status` first. If it fails, ask the user to enable Agent control in KoniSynth (right-click the title).
- Parameters are many (about 680). Use `describe_parameters` with a `match` (id or name fragment: "cutoff", "lfo",
  "fm", "chip", "reverb"...) to see ids, current values, ranges, options and a help text for each. Calling it without
  `match` returns only the id groups and counts.
- `set_parameters` takes {id: value}. A string value uses the parameter's own format ("2 kHz", "350 ms", "40 %",
  "Saw", "C4"); a number is the real value in the parameter's unit (Hz, s, dB, 0..1 for percentages). Each call is
  one undo step for the user (Ctrl+Z), so group related changes in one call.
- Factory presets (about 240, by category) are good starting points and examples of each engine: `list_presets`,
  then `load_preset`. Save results with `save_preset` (user presets).
- Engine-specific parameters only sound when that engine is selected; change `engine` first.
- You cannot hear, but you can measure: `audition` plays notes and returns the measured sound (pitch, level,
  attack/decay, spectral centroid and octave-band profile, harmonics, stereo); `listen` measures whatever is sounding
  now (also the user playing); `compare` measures the distance to a reference WAV supplied by the user. Iterate:
  change parameters, audition, read the numbers, adjust. Rough guide: centroid up = brighter; flatness near 1 =
  noise-like, near 0 = tonal; attack under 10 ms = percussive.
- `generate` creates new sounds with knowledge: it starts from a factory preset of the requested style, mutates the
  parameters that matter for its engine and the effects, applies the style rules and keeps only candidates that pass a
  listening check; with a reference WAV they are ranked by similarity. `use_candidate` loads one of them, `save_preset`
  keeps it. `mutate` varies the current sound (small amount = subtle). Every step can be undone by the user.
- For requests like "the synth from <film/song>": recall or look up what is documented about that sound, say what is
  known and what is guessed, pick the closest engine and preset, set parameters, audition and iterate with the
  measurements; ask the user for a reference recording they own if available (`compare`).
"""

mcp = MCPServer("KoniSynth-mcp", instructions=INSTRUCTIONS, version="0.1.0")


class KoniLink:
    """Conexión persistente con una instancia de KoniSynth (reconecta si se pierde)."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._sock: socket.socket | None = None
        self._file = None
        self._port: int | None = None
        self._token = ""
        self._next_id = 0

    @staticmethod
    def announces() -> list[dict[str, Any]]:
        found = []
        for f in sorted(ANNOUNCE_DIR.glob("*.json")):
            try:
                info = json.loads(f.read_text(encoding="utf-8"))
                if isinstance(info.get("port"), int) and info.get("token"):
                    found.append(info)
            except (OSError, ValueError):
                continue
        return found

    def _close(self) -> None:
        for c in (self._file, self._sock):
            try:
                if c is not None:
                    c.close()
            except OSError:
                pass
        self._sock = self._file = None
        self._port = None

    def _connect(self, port: int | None) -> None:
        candidates = self.announces()
        if port is not None:
            candidates = [a for a in candidates if a["port"] == port]
        else:  # la más reciente primero
            candidates.sort(key=lambda a: a.get("started", ""), reverse=True)
        if not candidates:
            raise ToolError("No KoniSynth instance with Agent control enabled was found "
                               "(enable it: right-click the KoniSynth title > Agent control).")
        errors = []
        for a in candidates:
            try:
                s = socket.create_connection(("127.0.0.1", a["port"]), timeout=TIMEOUT_S)
                self._sock, self._file = s, s.makefile("rwb")
                self._port, self._token = a["port"], a["token"]
                return
            except OSError as e:  # anuncio huérfano (proceso cerrado a la fuerza)
                errors.append(f"{a['port']}: {e}")
        raise ToolError("KoniSynth announces found but none answered: " + "; ".join(errors))

    # Órdenes que no deben repetirse si se pierde la respuesta: KoniSynth pudo ejecutarlas (play sonaría dos veces; save_preset
    # fallaría la segunda vez con "el nombre existe"). Antes de enviarlas se comprueba la conexión con ping, que sí se reintenta:
    # una conexión caducada (KoniSynth reiniciado) se renueva sin riesgo de duplicar la orden
    NOT_IDEMPOTENT = frozenset({"play", "save_preset"})

    def call(self, cmd: str, port: int | None = None, **args: Any) -> Any:
        with self._lock:
            once = cmd in self.NOT_IDEMPOTENT
            if once:
                self._send("ping", port, {}, retry=True)
            reply = self._send(cmd, port, args, retry=not once)
            if not reply.get("ok"):
                raise ToolError(f"KoniSynth: {reply.get('error')}")
            return reply.get("result")

    def _send(self, cmd: str, port: int | None, args: dict[str, Any], retry: bool) -> dict[str, Any]:
        try:  # antes de conectar: NaN o Infinity no son JSON (Python los escribiría igualmente)
            json.dumps(args, allow_nan=False)
        except ValueError as e:
            raise ToolError(f"invalid value for '{cmd}': numbers must be finite (no NaN or Infinity)") from e
        for attempt in (0, 1) if retry else (1,):
            if self._sock is None or (port is not None and port != self._port):
                self._close()
                self._connect(port)
            self._next_id += 1
            request = dict(token=self._token, id=self._next_id, cmd=cmd, **args)
            try:
                self._file.write((json.dumps(request) + "\n").encode("utf-8"))
                self._file.flush()
                line = self._file.readline()
                if not line:
                    raise ConnectionError("connection closed")
                return json.loads(line)
            except (OSError, ValueError) as e:
                self._close()
                if attempt == 1:
                    if not retry:
                        raise ToolError(f"KoniSynth connection lost after sending '{cmd}': it may have been executed. "
                                        "Check with status (or listen) before sending it again.") from e
                    raise ToolError(f"KoniSynth connection failed: {e}") from e
        raise ToolError("KoniSynth connection failed")


link = KoniLink()
MAX_LISTEN_S = 10.0  # captura máxima de listen en KoniSynth (AgentServer.h, maxListenSeconds)


def _group(param_id: str) -> str:
    """Prefijo de grupo de un id ('fm3Rate' -> 'fm', 'chipVoice' -> 'chip', 'cutoff' -> 'cutoff')."""
    head = ""
    for ch in param_id:
        if ch.isupper() or ch.isdigit():
            break
        head += ch
    return head or param_id


@mcp.tool(annotations=READ)
def list_instances() -> list[dict[str, Any]]:
    """List the running KoniSynth instances with Agent control enabled (port, engine, current preset)."""
    out = []
    for a in KoniLink.announces():
        item = {"port": a["port"], "version": a.get("version"), "started": a.get("started")}
        try:
            probe = KoniLink()
            item.update(probe.call("ping", port=a["port"]))
            probe._close()
            item["alive"] = True
        except ToolError:
            item["alive"] = False
        out.append(item)
    return out


@mcp.tool(annotations=READ)
def status(port: int | None = None) -> dict[str, Any]:
    """Current engine, preset and whether it has unsaved changes, plus `clock`: sample rate, block size, latency, the
    host data that exists (bpm, ppq, timeInSamples, playing; nothing is invented when the host gives none), the
    effective tempo of arpeggiator/sequencer/synced LFOs and effects with its source (host or Arp BPM), the engine's
    own rhythm clock (Omnichord, Drum Machine, VL-1), the UI's BPM display, the active note transforms (transpose,
    scale, sequencer, arpeggiator: they change the notes you play) and where captures are taken. Call this first.
    `port` selects an instance (default: the most recently started one)."""
    out = link.call("ping", port=port)
    out["clock"] = link.call("state", port=port)
    return out


@mcp.tool(annotations=READ)
def describe_parameters(match: str = "", port: int | None = None) -> list[dict[str, Any]] | dict[str, Any]:
    """Describe parameters whose id or name contains `match` (case-insensitive): id, name, current value, min, max,
    default, type (continuous / stepped / choice / bool), options and help text. Without `match`, returns only the id
    groups with their counts (about 680 parameters in total) so you can narrow down."""
    if match.strip():
        return link.call("describe", port=port, match=match.strip())
    params = link.call("describe", port=port)
    groups: dict[str, int] = {}
    for p in params:
        groups[_group(p["id"])] = groups.get(_group(p["id"]), 0) + 1
    return {"total": len(params), "groups": dict(sorted(groups.items())),
            "hint": "call again with match='<group or name fragment>' for details"}


@mcp.tool(annotations=READ)
def get_parameters(ids: list[str], port: int | None = None) -> dict[str, dict[str, Any]]:
    """Current values of the given parameter ids: per id {text (as displayed), value (exact real value, the number
    set_parameters accepts), normalized (0..1 control position), unit (unit of value: Hz, s, ms, dB, st...; "ratio" =
    0..1 shown as %; "index" = choice option number; "bool"; "" = plain number; null if unknown)}."""
    return link.call("get", port=port, ids=ids, detail=True)


@mcp.tool(annotations=REPLACE)
def set_parameters(values: dict[str, str | float | bool | dict[str, float]],
                   port: int | None = None) -> dict[str, dict[str, Any]]:
    """Set several parameters at once. Each value is one of: text in the parameter's format ("2 kHz", "350 ms",
    "40 %", "C4"; for switches and choices one of its options, e.g. "Saw", "On"); a number = real value in the unit
    given by get_parameters/describe_parameters (for choices the option index; out-of-range numbers are clamped and
    marked "clamped"); true/false for switches; or {"normalized": 0..1} = control position. The whole call is validated
    first: an unknown id, unreadable text, non-finite number or other object shape changes nothing. One undo step in
    KoniSynth. Returns the same detail as get_parameters for each id."""
    return link.call("set", port=port, values=values, detail=True)


@mcp.tool(annotations=READ)
def list_presets(category: str = "", match: str = "", port: int | None = None) -> list[dict[str, Any]]:
    """List presets (index, name, category, factory or user, engine, and when the preset has them: playHint = how to play it,
    playLow/playHigh = recommended range as MIDI notes, 60 = C4), optionally filtered by category and/or a name fragment."""
    presets = link.call("presets", port=port)
    cat, m = category.lower().strip(), match.lower().strip()
    return [p for p in presets if (not cat or p["category"].lower() == cat) and (not m or m in p["name"].lower())]


@mcp.tool(annotations=REPLACE)
def load_preset(name: str = "", index: int = -1, port: int | None = None) -> dict[str, Any]:
    """Load a preset by exact name (case-insensitive) or by index from list_presets. One undo step."""
    if index >= 0:
        return link.call("load_preset", port=port, index=index)
    if not name:
        raise ToolError("give a preset name or index")
    return link.call("load_preset", port=port, name=name)


@mcp.tool(annotations=ADD)
def save_preset(name: str, category: str = "Init", port: int | None = None) -> dict[str, Any]:
    """Save the current sound as a user preset with this name and category (fails if the name exists)."""
    return link.call("save_preset", port=port, name=name, category=category)


def _notes(notes: str | list[dict[str, Any]], duration: float, velocity: int) -> list[dict[str, Any]]:
    """"C4" o "C4 E4 G4" (acorde, todas a la vez) o lista de {note, velocity, start, duration}."""
    if isinstance(notes, str):
        names = notes.split()
        if not names:
            raise ToolError("no notes given")
        return [{"note": n, "velocity": velocity, "start": 0.0, "duration": duration} for n in names]
    # Los campos que falten toman los valores por defecto; note es obligatoria y KoniSynth rechaza campos desconocidos
    if not isinstance(notes, list) or not all(isinstance(n, dict) for n in notes):
        raise ToolError("notes must be a string of note names or a list of {note, velocity, start, duration}")
    for i, n in enumerate(notes):
        if "note" not in n:
            raise ToolError(f"note {i}: missing note")
    return [{"velocity": velocity, "start": 0.0, "duration": duration, **n} for n in notes]


def _tempo(bpm: float | None) -> dict[str, float]:
    """Argumento bpm de play (KoniSynth valida 20..999); sin bpm, tiempos en segundos."""
    if bpm is None:
        return {}
    if not 20.0 <= float(bpm) <= 999.0:
        raise ToolError("bpm must be 20..999")
    return {"bpm": float(bpm)}


_last_capture: dict[str, Any] = {}


def _measured(cap: dict[str, Any]) -> dict[str, Any]:
    """Medida de una captura de listen, con lo que se pidió y lo que se obtuvo."""
    _last_capture.clear()
    _last_capture.update(cap)
    sr = float(cap["sampleRate"])
    capture = {"requested_s": round(cap["requestedFrames"] / sr, 4), "captured_s": round(cap["capturedFrames"] / sr, 4),
               "sample_rate": sr, "partial": bool(cap.get("partial")), "point": cap.get("point", "output")}
    if cap.get("truncated"):
        capture["truncated"] = cap["truncated"]
    return {"wav": cap["path"], "capture": capture, **analysis.analyze(cap["path"])}


def _point(point: str) -> str:
    if point not in ("output", "engine"):
        raise ToolError('point must be "output" or "engine"')
    return point


def _listen(seconds: float, port: int | None, point: str = "output") -> dict[str, Any]:
    if not 0.05 <= seconds <= MAX_LISTEN_S:
        raise ToolError(f"seconds must be 0.05..{MAX_LISTEN_S:g}")
    return _measured(link.call("listen", port=port, seconds=seconds, point=_point(point)))


@mcp.tool(annotations=PLAY)
def play_notes(notes: str | list[dict[str, Any]], duration: float = 1.0, velocity: int = 100, bpm: float | None = None,
               port: int | None = None) -> dict[str, Any]:
    """Play notes without measuring, sample-accurate (KoniSynth schedules them on its audio clock). `notes`: "C4",
    "C4 E4 G4" (chord) or a list of {note ("C4", "F#3", "Bb2" or MIDI 0-127; required), velocity (integer 1-127), start,
    duration} for melodies (max 64 notes, all ending within 30 s). Times are seconds; with `bpm` (20-999) they are beats
    (quarter note = 1, e.g. 0.25 = sixteenth) at that fixed tempo. Any invalid note rejects the whole call and nothing
    sounds. If transpose, scale, sequencer or arpeggiator are active, the notes go through them: the reply lists them
    under `transforms` (turn them off with set_parameters if you need the exact notes)."""
    return link.call("play", port=port, notes=_notes(notes, duration, velocity), **_tempo(bpm))


@mcp.tool(annotations=STOP)
def stop_notes(port: int | None = None) -> bool:
    """Release all notes and cancel the notes scheduled by play_notes or audition that have not sounded yet."""
    return link.call("stop_notes", port=port)


@mcp.tool(annotations=PLAY)
def audition(notes: str | list[dict[str, Any]] = "C4", duration: float = 1.0, velocity: int = 100, tail: float = 0.6,
             bpm: float | None = None, point: str = "output", port: int | None = None) -> dict[str, Any]:
    """Play notes, wait for them to finish plus `tail` seconds (release, echo, reverb) and measure the result: pitch
    (Hz, note, cents), level (dBFS), envelope (attack, decay), spectrum (centroid, rolloff, flatness, octave bands,
    harmonics) and stereo. The capture starts at the notes' time 0 (a late first note keeps its leading silence) and
    lasts until the last note ends plus `tail`; that total must be at most 10 s (longer requests are rejected before
    playing). With `bpm`, start and duration are beats as in play_notes (`tail` stays in seconds). Also returns the WAV
    path and `capture` (requested and captured seconds, partial/truncated). `point`: "output" (default, what you hear:
    after effects, Gain and limiter) or "engine" (the engine with its own built-in effects such as SNES echo or MT-32
    reverb, before the vocoder, input monitor, added effects, Gain and limiter: to judge the raw sound)."""
    point = _point(point)
    seq = _notes(notes, duration, velocity)
    tempo = _tempo(bpm)
    try:
        ends = max(float(n["start"]) + float(n["duration"]) for n in seq) * (60.0 / bpm if bpm else 1.0)
    except (TypeError, ValueError) as e:
        raise ToolError("start and duration must be numbers") from e
    total = ends + max(0.0, tail)
    if total > MAX_LISTEN_S:
        raise ToolError(f"audition captures at most {MAX_LISTEN_S:g} s: the notes end at {ends:g} s plus tail {tail:g} s "
                        f"= {total:g} s. Shorten them, or use play_notes and listen.")
    link.call("stop_notes", port=port)
    played = link.call("play", port=port, notes=seq, **tempo)
    time.sleep(total + 0.15)
    # Captura desde el origen del play (la muestra en que el planificador de KoniSynth lo recibió, no el reloj de aquí):
    # si aún no se han escrito los segundos pedidos, se espera
    for _ in range(20):
        cap = link.call("listen", port=port, seconds=max(0.05, total), fromPlay=True, point=point)
        if cap.get("truncated") != "notYetWritten":
            break
        time.sleep(0.1)
    out = _measured(cap)
    if played.get("transforms"):
        out["transforms"] = played["transforms"]  # las notas no sonaron tal cual (ver status)
    return out


@mcp.tool(annotations=READ)
def listen(seconds: float = 2.0, point: str = "output", port: int | None = None) -> dict[str, Any]:
    """Measure the last `seconds` (0.05-10) of KoniSynth's output, whatever is sounding (e.g. the user playing).
    `point` as in audition ("output" or "engine": before the added effects). `capture` says how much was captured
    (less than requested if the audio started less than `seconds` ago) and from which point."""
    return _listen(seconds, port, point)


@mcp.tool(annotations=READ)
def compare(reference_wav: str, candidate_wav: str = "") -> dict[str, Any]:
    """Compare a reference WAV supplied by the user (path) with the last capture (or another WAV): differences in level,
    pitch (cents), brightness (centroid and rolloff ratios), noisiness, attack/decay, octave-band profile and stereo,
    plus a single spectral-profile distance in dB (0 = same tonal balance). Only use audio the user owns or that has a
    free license."""
    cand = candidate_wav or _last_capture.get("path", "")
    if not cand:
        raise ToolError("no capture yet: call audition or listen first, or give candidate_wav")
    try:
        ref, can = analysis.analyze(reference_wav), analysis.analyze(cand)
    except (OSError, ValueError) as e:
        raise ToolError(f"cannot read WAV: {e}") from e
    return {"reference": reference_wav, "candidate": cand, "difference": analysis.compare(ref, can),
            "reference_features": ref, "candidate_features": can}


_last_generation: dict[str, Any] = {}


def _audition_for_generator(notes: str, duration: float, velocity: int, tail: float, port: int | None) -> dict[str, Any]:
    return audition(notes, duration, velocity, tail, port=port)


@mcp.tool(annotations=WRITE)
def generate(style: str, engine: str = "", count: int = 3, amount: float = 0.35, seed: int | None = None,
             reference_wav: str = "", max_tries: int = 8, port: int | None = None) -> dict[str, Any]:
    """Generate new sounds with knowledge. `style`: bass, lead, pad, pluck, keys, strings, brass, fx, drums, chip,
    sequence, vocal, nature, machines. Starts from
    a random factory preset of that style (optionally only of `engine`, e.g. "FM", "Mega Drive", "8-Bit/DPCM"), mutates the
    relevant parameters (`amount` 0..1: 0.15 subtle, 0.35 varied, 0.7 wild), auditions each candidate and keeps those
    that pass (not silent, no clipping, pitched if the style needs it, envelope fits the style). With `reference_wav`
    (audio the user owns), accepted candidates are ranked by similarity. Same seed = same results. The last candidate
    tried stays loaded; use `use_candidate(n)` to load an accepted one and `save_preset` to keep it. About 2-4 s per try."""
    ref = None
    if reference_wav:
        try:
            ref = analysis.analyze(reference_wav)
        except (OSError, ValueError) as e:
            raise ToolError(f"cannot read reference WAV: {e}") from e
    try:
        result = generator.generate(link.call, _audition_for_generator, style.lower().strip(), engine, max(1, min(count, 8)),
                                    max(0.0, min(amount, 1.0)), seed, ref, max(1, min(max_tries, 20)), port)
    except ValueError as e:
        raise ToolError(str(e)) from e
    _last_generation.clear()
    _last_generation.update(result)
    shown = [{k: v for k, v in c.items() if k != "values"} | {"n": i} for i, c in enumerate(result["accepted"])]
    return {"style": result["style"], "seed": result["seed"], "accepted": shown, "rejected": result["rejected"],
            "next": "use_candidate(n) to load one, then save_preset to keep it" if shown else "try a higher max_tries or another amount"}


@mcp.tool(annotations=REPLACE)
def use_candidate(n: int = 0, port: int | None = None) -> dict[str, Any]:
    """Load accepted candidate number `n` from the last `generate` (its base preset plus the same values)."""
    acc = _last_generation.get("accepted") or []
    if not 0 <= n < len(acc):
        raise ToolError(f"no candidate {n}; the last generate accepted {len(acc)}")
    return generator.apply_candidate(link.call, acc[n], port)


@mcp.tool(annotations=WRITE)
def mutate(amount: float = 0.2, seed: int | None = None, include_fx: bool = True, port: int | None = None) -> dict[str, Any]:
    """Vary the current sound around its present values (only parameters that matter for the selected engine, plus
    effects if `include_fx`). `amount` 0..1. One undo step. Returns the changed parameters with their new values."""
    import random as _r
    seed = seed if seed is not None else _r.randrange(1 << 30)
    params = link.call("describe", port=port)
    eng_param = next(p for p in params if p["id"] == "engine")
    engine = eng_param["options"].index(eng_param["value"])
    if not include_fx:
        params = [p for p in params if generator.eligible(p["id"], engine) == "engine"]
    values = generator.mutate(params, engine, max(0.0, min(amount, 1.0)), _r.Random(seed))
    if not values:
        return {"seed": seed, "changed": {}}
    return {"seed": seed, "changed": link.call("set", port=port, values=values)}


if __name__ == "__main__":
    mcp.run("stdio")
