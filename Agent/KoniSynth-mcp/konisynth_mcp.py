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
parameter `engine` (Synth, Theremin, Casio VL-1, Stylophone, Omnichord, Drum Machine, FM 6-Op, 8-Bit NES/Game Boy,
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

    def call(self, cmd: str, port: int | None = None, **args: Any) -> Any:
        with self._lock:
            for attempt in (0, 1):
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
                    reply = json.loads(line)
                    break
                except (OSError, ValueError) as e:
                    self._close()
                    if attempt == 1:
                        raise ToolError(f"KoniSynth connection failed: {e}") from e
            if not reply.get("ok"):
                raise ToolError(f"KoniSynth: {reply.get('error')}")
            return reply.get("result")


link = KoniLink()


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
    """Current engine, preset and whether it has unsaved changes. Call this first. `port` selects an instance
    (default: the most recently started one)."""
    return link.call("ping", port=port)


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
def get_parameters(ids: list[str], port: int | None = None) -> dict[str, str]:
    """Current values (as displayed in KoniSynth) of the given parameter ids."""
    return link.call("get", port=port, ids=ids)


@mcp.tool(annotations=REPLACE)
def set_parameters(values: dict[str, str | float], port: int | None = None) -> dict[str, str]:
    """Set several parameters at once; returns the resulting displayed values. Strings use the parameter's format
    ("2 kHz", "350 ms", "40 %", "Saw", "C4"); numbers are real values in the parameter's unit. The whole call is one
    undo step in KoniSynth. If any id is unknown, nothing is changed."""
    return link.call("set", port=port, values=values)


@mcp.tool(annotations=READ)
def list_presets(category: str = "", match: str = "", port: int | None = None) -> list[dict[str, Any]]:
    """List presets (index, name, category, factory or user), optionally filtered by category and/or a name fragment."""
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
    return [{"note": n.get("note", 60), "velocity": n.get("velocity", velocity), "start": n.get("start", 0.0),
             "duration": n.get("duration", duration)} for n in notes]


_last_capture: dict[str, Any] = {}


def _listen(seconds: float, port: int | None) -> dict[str, Any]:
    cap = link.call("listen", port=port, seconds=seconds)
    _last_capture.clear()
    _last_capture.update(cap)
    return {"wav": cap["path"], **analysis.analyze(cap["path"])}


@mcp.tool(annotations=PLAY)
def play_notes(notes: str | list[dict[str, Any]], duration: float = 1.0, velocity: int = 100,
               port: int | None = None) -> dict[str, Any]:
    """Play notes through KoniSynth's virtual keyboard without measuring. `notes`: "C4", "C4 E4 G4" (chord) or a list
    of {note, velocity (1-127), start (s), duration (s)} for melodies (max 64 notes, up to 30 s)."""
    return link.call("play", port=port, notes=_notes(notes, duration, velocity))


@mcp.tool(annotations=STOP)
def stop_notes(port: int | None = None) -> bool:
    """Release all notes."""
    return link.call("stop_notes", port=port)


@mcp.tool(annotations=PLAY)
def audition(notes: str | list[dict[str, Any]] = "C4", duration: float = 1.0, velocity: int = 100, tail: float = 0.6,
             port: int | None = None) -> dict[str, Any]:
    """Play notes, wait for them to finish plus `tail` seconds (release, echo, reverb) and measure the result: pitch
    (Hz, note, cents), level (dBFS), envelope (attack, decay), spectrum (centroid, rolloff, flatness, octave bands,
    harmonics) and stereo. Total length is capped at 10 s. Also returns the WAV path of the capture."""
    link.call("stop_notes", port=port)
    played = link.call("play", port=port, notes=_notes(notes, duration, velocity))
    total = min(10.0, float(played["endsAt"]) + max(0.0, tail))
    time.sleep(total + 0.15)
    # Captura anclada a la muestra en que sonó la primera nota (no al reloj): si el hilo de mensajes de KoniSynth retrasó
    # la nota, se espera a que estén escritos los segundos pedidos
    for _ in range(20):
        cap = link.call("listen", port=port, seconds=total, fromPlay=True)
        if not cap.get("partial"):
            break
        time.sleep(0.1)
    _last_capture.clear()
    _last_capture.update(cap)
    return {"wav": cap["path"], **analysis.analyze(cap["path"])}


@mcp.tool(annotations=READ)
def listen(seconds: float = 2.0, port: int | None = None) -> dict[str, Any]:
    """Measure the last `seconds` (max 10) of KoniSynth's output, whatever is sounding (e.g. the user playing)."""
    return _listen(seconds, port)


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
    return audition(notes, duration, velocity, tail, port)


@mcp.tool(annotations=WRITE)
def generate(style: str, engine: str = "", count: int = 3, amount: float = 0.35, seed: int | None = None,
             reference_wav: str = "", max_tries: int = 8, port: int | None = None) -> dict[str, Any]:
    """Generate new sounds with knowledge. `style`: bass, lead, pad, pluck, keys, strings, brass, fx, drums, chip,
    sequence, vocal, nature, machines. Starts from
    a random factory preset of that style (optionally only of `engine`, e.g. "FM", "Mega Drive", "8-Bit"), mutates the
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
