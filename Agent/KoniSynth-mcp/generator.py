"""Generación aleatoria con conocimiento (F4) para KoniSynth-mcp.

Idea (estilo Synerator, pero con criterio): partir de un preset de fábrica del estilo pedido (sonido curado), mutar
alrededor de él solo los parámetros que tienen sentido para su motor y la cadena de efectos, ajustar las reglas del
estilo (p. ej. ataque lento en un pad) y escuchar el resultado: se descartan los candidatos en silencio, saturados, sin
tono si el estilo lo pide o con una envolvente que no encaja. Con un WAV de referencia, se ordenan por parecido.
Todo es reproducible con la semilla: mismo preset base + misma semilla = mismos valores.
"""
from __future__ import annotations

import random
from typing import Any, Callable

import analysis

# Prefijos de id por motor (índice del parámetro `engine`); comprobados contra los ids de Params/Layout.h
ENGINE_PREFIXES: dict[int, tuple[str, ...]] = {
    0: ("osc", "wave", "pulseWidth", "pwm", "drift", "sub", "noise", "wt", "string", "unison", "filter", "cutoff",
        "resonance", "attack", "decay", "sustain", "release", "velToFilter", "complex", "modWave", "modRatio", "modSync",
        "amDepth", "timbre", "symmetry", "pitchEnv"),
    1: ("theremin",), 2: ("vl1",), 3: ("stylo", "genx"), 4: ("omni",), 5: ("drum",), 6: ("fm",), 7: ("chip",),
    8: ("pv",), 9: ("ans",), 10: ("md",), 11: ("snes",), 12: ("n64",), 13: ("opl",),
}
FX_PREFIXES = ("crush", "formant", "tube", "eq", "ampOn", "ampModel", "ampGain", "ampBass", "ampMid", "ampTreble",
               "ampPresence", "ampMaster", "ampSag", "cabMix", "trans", "compOn", "compMode", "compThreshold", "compRatio",
               "compAttack", "compRelease", "compKnee", "compMakeup", "compMix", "compScHpf", "phaser", "flanger", "fshift", "chorus", "leslie", "echo", "ambient",
               "reverb", "spring", "media")
# Nunca al azar: salida, MIDI y expresión, macros, interpretación, entrada, fuentes de fichero, paneo de voces, mutes
EXCLUDE_EXACT = {"engine", "gain", "monoOut", "midiInChannel", "bendRange", "fineTune", "modWheelDepth", "voiceMode",
                 "glide", "osc1Mute", "wtMute", "sampleMute", "stringMute", "wtTable", "chipModel", "mdModel", "oplModel",
                 "styloModel", "styloRange", "reverbFreeze", "voicePan", "voicePanMode", "unisonVoices",
                 "atFilter", "atVibrato", "atAmp"}  # aftertouch: ids exactos (un prefijo "at" se comería "attack")
EXCLUDE_PREFIXES = ("macro", "arp", "seq", "step", "input", "vocoder", "sample", "ansScore", "ansUser", "drumPattern",
                    "omniRhythm", "vl1Rhythm", "n64Source")

# Estilo -> categoría de los presets de fábrica, nota de escucha y comprobaciones
STYLES: dict[str, dict[str, Any]] = {
    "bass":    {"category": "Bass", "notes": "C2", "duration": 0.8, "tonal": True},
    "lead":    {"category": "Lead", "notes": "C4", "duration": 1.0, "tonal": True},
    "pad":     {"category": "Pad", "notes": "C3", "duration": 2.0, "tail": 1.2, "tonal": True, "min_attack_ms": 60},
    "pluck":   {"category": "Pluck", "notes": "C4", "duration": 0.4, "tail": 0.8, "tonal": True, "max_decay_ms": 900},
    "keys":    {"category": "Keys", "notes": "C4", "duration": 1.0, "tonal": True},
    "strings": {"category": "Strings", "notes": "C3", "duration": 1.6, "tail": 0.8, "tonal": True},
    "brass":   {"category": "Brass", "notes": "C3", "duration": 1.2, "tonal": True},
    "fx":      {"category": "FX", "notes": "C4", "duration": 1.5, "tonal": False},
    "drums":   {"category": "Drums", "notes": "C2", "duration": 0.3, "tail": 0.7, "tonal": False},
    "chip":    {"category": "8-Bit", "notes": "C4", "duration": 0.8, "tonal": True},
    "sequence": {"category": "Sequence", "notes": "C3", "duration": 2.0, "tonal": False},
    "vocal":   {"category": "Vocal", "notes": "C3", "duration": 1.5, "tonal": True},
    "nature":  {"category": "Nature", "notes": "C4", "duration": 2.0, "tonal": False},
    "machines": {"category": "Machines", "notes": "C4", "duration": 1.5, "tonal": False},
}  # Vocoder no: necesita audio de entrada
# Reglas de estilo para el motor Synth (0): valores en el formato de cada parámetro, elegidos al azar en el rango
SYNTH_RULES: dict[str, dict[str, tuple[float, float, str]]] = {
    "pad":   {"attack": (0.3, 1.5, "s"), "release": (1.2, 4.0, "s")},
    "pluck": {"attack": (0.0005, 0.005, "s"), "sustain": (0.0, 0.15, "%"), "decay": (0.15, 0.6, "s")},
    "bass":  {"attack": (0.0005, 0.02, "s")},
    "lead":  {"attack": (0.001, 0.05, "s")},
}


def eligible(param_id: str, engine: int) -> str | None:
    """'engine' / 'fx' si el parámetro puede mutar con ese motor; None si no."""
    if param_id in EXCLUDE_EXACT or any(param_id.startswith(p) for p in EXCLUDE_PREFIXES):
        return None
    if any(param_id.startswith(p) for p in ENGINE_PREFIXES.get(engine, ())):
        return "engine"
    if any(param_id.startswith(p) for p in FX_PREFIXES):
        return "fx"
    return None


def mutate(params: list[dict[str, Any]], engine: int, amount: float, rng: random.Random) -> dict[str, Any]:
    """Valores nuevos ({id: {"normalized": x}}) alrededor de los actuales. amount 0..1: probabilidad de tocar cada
    parámetro (25 % + 50 %·amount) y tamaño del cambio (desviación 0,25·amount en 0..1). Los interruptores de efectos
    cambian poco (10 %·amount) y las mezclas de efectos no pasan del 60 %."""
    out: dict[str, Any] = {}
    for p in params:
        kind = eligible(p["id"], engine)
        if kind is None or rng.random() > 0.25 + 0.5 * amount:
            continue
        v = float(p["normalized"])
        t = p.get("type")
        if t == "bool":
            if rng.random() < (0.1 if kind == "fx" else 0.2) * amount:
                out[p["id"]] = {"normalized": 0.0 if v > 0.5 else 1.0}
            continue
        if t in ("choice", "stepped"):
            n = len(p.get("options") or [])
            if n >= 2 and rng.random() < 0.4 * amount:
                k = round(v * (n - 1))
                k = rng.randrange(n) if t == "choice" else max(0, min(n - 1, k + rng.choice((-1, 1))))
                out[p["id"]] = {"normalized": k / (n - 1)}
            continue
        nv = min(1.0, max(0.0, rng.gauss(v, 0.25 * amount)))
        if kind == "fx" and p["id"].lower().endswith("mix"):
            nv = min(nv, 0.6)
        if p["id"].lower().endswith(("level", "volume")) and v > 0.2:
            nv = max(nv, 0.6 * v)  # una fuente que sonaba no se apaga por azar
        out[p["id"]] = {"normalized": round(nv, 4)}
    return out


def style_rules(style: str, engine: int, rng: random.Random) -> dict[str, str]:
    out = {}
    if engine != 0:
        return out
    for pid, (lo, hi, unit) in SYNTH_RULES.get(style, {}).items():
        x = lo * (hi / lo) ** rng.random() if lo > 0 else rng.uniform(lo, hi)
        out[pid] = f"{x * 100:.0f} %" if unit == "%" else (f"{x * 1000:.1f} ms" if x < 1 else f"{x:.2f} s")
    return out


def judge(style: str, m: dict[str, Any]) -> list[str]:
    """Motivos de descarte (vacío = válido)."""
    rules = STYLES[style]
    why = []
    if m.get("silent") or m["peak_dbfs"] < -45:
        return [f"too quiet ({m['peak_dbfs']} dBFS)"]
    if m["peak_dbfs"] > -0.3:
        why.append(f"clipping ({m['peak_dbfs']} dBFS)")
    if rules.get("tonal") and not m.get("pitch", {}).get("f0_hz"):
        why.append("no clear pitch")
    env = m.get("envelope", {})
    if "min_attack_ms" in rules and env.get("attack_ms", 0) < rules["min_attack_ms"]:
        why.append(f"attack {env.get('attack_ms')} ms < {rules['min_attack_ms']} ms")
    if "max_decay_ms" in rules and (env.get("decay_to_minus20db_ms") is None or env["decay_to_minus20db_ms"] > rules["max_decay_ms"]):
        why.append(f"decay {env.get('decay_to_minus20db_ms')} ms > {rules['max_decay_ms']} ms")
    return why


def summary(m: dict[str, Any]) -> dict[str, Any]:
    p, e, s = m.get("pitch", {}), m.get("envelope", {}), m.get("spectrum", {})
    return {"peak_dbfs": m.get("peak_dbfs"), "note": p.get("note"), "cents": p.get("cents"),
            "vibrato": p.get("vibrato"), "attack_ms": e.get("attack_ms"), "decay_to_minus20db_ms": e.get("decay_to_minus20db_ms"),
            "centroid_hz": s.get("centroid_hz"), "flatness": s.get("flatness")}


def generate(call: Callable[..., Any], audition: Callable[..., dict[str, Any]], style: str, engine: str = "",
             count: int = 3, amount: float = 0.35, seed: int | None = None, reference: dict[str, Any] | None = None,
             max_tries: int = 10, port: int | None = None) -> dict[str, Any]:
    if style not in STYLES:
        raise ValueError(f"unknown style '{style}'; use one of {sorted(STYLES)}")
    rules = STYLES[style]
    seed = seed if seed is not None else random.randrange(1 << 30)
    engine_param = next(p for p in call("describe", port=port, match="engine") if p["id"] == "engine")
    engine_names = engine_param["options"]
    want = None
    if engine:
        hits = [i for i, n in enumerate(engine_names) if engine.lower() in n.lower()]
        if not hits:
            raise ValueError(f"unknown engine '{engine}'; options: {engine_names}")
        want = hits[0]
    bases = [p for p in call("presets", port=port) if p["factory"] and p["category"].lower() == rules["category"].lower()]
    if not bases:
        raise ValueError(f"no factory presets in category {rules['category']}")
    accepted, rejected = [], []
    for attempt in range(max_tries):
        if len(accepted) >= count:
            break
        rng = random.Random(seed * 1000 + attempt)
        order = bases[:]
        rng.shuffle(order)
        base = None
        for b in order:  # el primero del motor pedido (si se pidió)
            st = call("load_preset", port=port, index=b["index"])
            eng = engine_names.index(st["engine"]) if st["engine"] in engine_names else -1
            if want is None or eng == want:
                base = (b, eng)
                break
        if base is None:
            raise ValueError(f"no {rules['category']} factory preset uses engine {engine_names[want]}")
        b, eng = base
        params = call("describe", port=port)
        values: dict[str, Any] = mutate(params, eng, amount, rng)
        values.update(style_rules(style, eng, rng))
        if values:
            call("set", port=port, values=values)
        m = audition(rules["notes"], rules["duration"], 100, rules.get("tail", 0.6), port)
        level_note = None
        if not m.get("silent") and m["peak_dbfs"] < -20:  # compensación con el Master (lineal 0..1, 100 % = ganancia unidad)
            gain = float(call("get", port=port, ids=["gain"])["gain"].split()[0]) / 100.0
            new = min(1.0, gain * 10 ** ((-10.0 - m["peak_dbfs"]) / 20.0))
            if new > gain + 1e-3:
                values["gain"] = f"{new * 100:.0f} %"
                call("set", port=port, values={"gain": values["gain"]})
                m = audition(rules["notes"], rules["duration"], 100, rules.get("tail", 0.6), port)
            if m["peak_dbfs"] < -20:
                level_note = f"quiet ({m['peak_dbfs']} dBFS with Master at {min(new, 1.0) * 100:.0f} %): raise the source level"
        why = judge(style, m)
        cand = {"attempt": attempt, "seed": seed, "base_preset": b["name"], "engine": engine_names[eng],
                "changed": len(values), "measured": summary(m), "values": values, "wav": m.get("wav")}
        if level_note:
            cand["level_note"] = level_note
        if reference is not None and not why:
            d = analysis.compare(reference, m)
            cand["distance_db"] = d.get("spectral_profile_distance_db")
            cand["pitch_cents_vs_reference"] = d.get("pitch_cents")
        (rejected if why else accepted).append(cand if not why else {**cand, "rejected": why})
    if reference is not None:
        accepted.sort(key=lambda c: c.get("distance_db", 1e9))
    return {"style": style, "seed": seed, "accepted": accepted,
            "rejected": [{k: c[k] for k in ("attempt", "base_preset", "rejected", "measured")} for c in rejected]}


def apply_candidate(call: Callable[..., Any], cand: dict[str, Any], port: int | None = None) -> dict[str, Any]:
    """Vuelve a poner un candidato: su preset base y los mismos valores (dos pasos de deshacer)."""
    st = call("load_preset", port=port, name=cand["base_preset"])
    if cand.get("values"):
        call("set", port=port, values=cand["values"])
    return st
