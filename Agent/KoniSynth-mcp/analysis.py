"""Rasgos de audio para el agente ("oídos" de KoniSynth-mcp). Solo numpy.

analyze(path) devuelve medidas con unidad en el nombre (dB, Hz, ms, cents). compare(a, b) da las diferencias de b
respecto a a. Todo es aproximado y está pensado para iterar un sonido, no para medir con rigor de laboratorio:
- tono por NSDF (McLeod: autocorrelación normalizada) en tramos de 40 ms cada 20 ms, 40..2000 Hz, con interpolación
  parabólica (~1 cent); f0 = mediana de los tramos claros (NSDF >= 0,8) y curva de tono en cents respecto a ella,
  con vibrato (frecuencia y profundidad), glide inicial y deriva;
- envolvente RMS en ventanas de 10 ms (ataque 10 % -> 90 % del máximo; caída hasta -20 dB bajo el máximo);
- espectro medio (Welch, Hann, 4096 puntos) de la parte con señal: centroide y rolloff 85 % sobre la magnitud (como es
  habitual: sobre la potencia, la fundamental domina y una sierra parece oscura), planitud y bandas de octava (energía).
"""
from __future__ import annotations

import struct
from pathlib import Path

import numpy as np

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
OCTAVE_BANDS_HZ = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]


def read_wav(path: str | Path) -> tuple[np.ndarray, int]:
    """WAV PCM 16/24/32 bits o float 32/64 (también WAVE_FORMAT_EXTENSIBLE) -> (muestras [n, canales] float64, fs)."""
    data = Path(path).read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, chans, fs, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if tag == 0xFFFE and len(body) >= 26:  # EXTENSIBLE: el formato real está en el subformato
                tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, chans, fs, bits)
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    if fmt is None or raw is None:
        raise ValueError("missing fmt or data chunk")
    tag, chans, fs, bits = fmt
    if tag == 3:
        x = np.frombuffer(raw, dtype="<f4" if bits == 32 else "<f8").astype(np.float64)
    elif tag == 1 and bits == 16:
        x = np.frombuffer(raw, dtype="<i2") / 32768.0
    elif tag == 1 and bits == 24:
        b = np.frombuffer(raw[: len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(v >= 1 << 23, v - (1 << 24), v) / float(1 << 23)
    elif tag == 1 and bits == 32:
        x = np.frombuffer(raw, dtype="<i4") / 2147483648.0
    else:
        raise ValueError(f"unsupported WAV format (tag {tag}, {bits} bits)")
    n = len(x) // chans
    return x[: n * chans].reshape(n, chans), fs


def _db(v: float, floor: float = -120.0) -> float:
    return float(max(floor, 20.0 * np.log10(max(v, 1e-12))))


def note_name(f0: float) -> tuple[str, float]:
    midi = 69.0 + 12.0 * np.log2(f0 / 440.0)
    n = int(round(midi))
    return f"{NOTE_NAMES[n % 12]}{n // 12 - 1}", float((midi - n) * 100.0)


def _nsdf_pitch(frame: np.ndarray, fs: int, fmin: float = 40.0, fmax: float = 2000.0) -> tuple[float | None, float]:
    """f0 de un tramo por NSDF (McLeod & Wyvill 2005): primer máximo que alcanza el 90 % del mayor; (f0, claridad 0..1)."""
    n = len(frame)
    x = frame - np.mean(frame)
    if np.max(np.abs(x)) < 1e-6:
        return None, 0.0
    size = 1 << int(np.ceil(np.log2(2 * n)))
    spec = np.fft.rfft(x, size)
    acf = np.fft.irfft(spec * np.conj(spec), size)[:n]
    sq = x * x
    m = 2 * acf[0] - np.concatenate(([0.0], np.cumsum(sq)[:-1])) - np.concatenate(([0.0], np.cumsum(sq[::-1])[:-1]))
    nsdf = 2 * acf / np.maximum(m, 1e-12)
    lo, hi = max(2, int(fs / fmax)), min(n - 2, int(fs / fmin))
    if hi <= lo:
        return None, 0.0
    seg = nsdf[lo:hi]
    peaks = np.nonzero((seg[1:-1] > seg[:-2]) & (seg[1:-1] >= seg[2:]) & (seg[1:-1] > 0))[0] + 1
    if len(peaks) == 0:
        return None, 0.0
    best = float(np.max(seg[peaks]))
    k = int(peaks[np.nonzero(seg[peaks] >= 0.9 * best)[0][0]]) + lo
    a, b, c = nsdf[k - 1], nsdf[k], nsdf[k + 1]
    delta = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    return float(fs / (k + delta)), float(min(1.0, b))


def _pitch_track(mono: np.ndarray, fs: int, start: int, end: int) -> dict:
    """Curva de tono de mono[start:end] y su resumen."""
    win, hop = int(0.04 * fs), int(0.02 * fs)
    times, f0s, clar = [], [], []
    for s0 in range(start, max(start + 1, end - win), hop):
        f, c = _nsdf_pitch(mono[s0: s0 + win], fs)
        times.append((s0 - start) / fs)
        f0s.append(f)
        clar.append(c)
    good = [f for f, c in zip(f0s, clar) if f and c >= 0.8]
    if len(good) < 2:
        return {"f0_hz": None, "note": None, "clarity": round(float(np.median(clar)) if clar else 0.0, 2),
                "comment": "no clear pitch (noise, chord, percussion or too short)"}
    f0 = float(np.median(good))
    name, cents = note_name(f0)
    curve = [float(round(1200 * np.log2(f / f0), 1)) if f and c >= 0.8 else None for f, c in zip(f0s, clar)]
    out = {"f0_hz": round(f0, 2), "note": name, "cents": round(cents, 1),
           "clarity": round(float(np.median([c for c in clar if c > 0])), 2),
           "curve_step_ms": 20, "curve_cents": curve[:100]}  # 2 s como mucho
    valid = np.array([v for v in curve if v is not None])
    first = next((v for v in curve if v is not None), None)
    steady = valid[len(valid) // 4:] if len(valid) >= 8 else valid
    out["glide_start_cents"] = first
    out["drift_cents"] = round(float(steady[-1] - steady[0]), 1) if len(steady) > 1 else 0.0
    if len(steady) >= 16:  # vibrato: pico del espectro de la curva sin tendencia entre 2 y 12 Hz
        y = steady - np.polyval(np.polyfit(np.arange(len(steady)), steady, 1), np.arange(len(steady)))
        spec = np.abs(np.fft.rfft(y * np.hanning(len(y)), 256))
        fr = np.fft.rfftfreq(256, 0.02)
        band = (fr >= 2) & (fr <= 12)
        k = int(np.argmax(np.where(band, spec, 0)))
        depth = float(np.percentile(y, 95) - np.percentile(y, 5)) / 2
        out["vibrato"] = {"rate_hz": round(float(fr[k]), 1), "depth_cents": round(depth, 1)} if depth > 3 else None
    return out


def analyze(path: str | Path) -> dict:
    x, fs = read_wav(path)
    left = x[:, 0]
    right = x[:, 1] if x.shape[1] > 1 else x[:, 0]
    mono = 0.5 * (left + right)
    out: dict = {"duration_s": round(len(mono) / fs, 3), "sample_rate_hz": fs,
                 "peak_dbfs": round(_db(float(np.max(np.abs(x))) if len(x) else 0.0), 1),
                 "rms_dbfs": round(_db(float(np.sqrt(np.mean(mono ** 2))) if len(mono) else 0.0), 1)}
    if out["peak_dbfs"] < -70.0:
        out["silent"] = True
        return out

    # Envolvente RMS cada 10 ms
    hop = max(1, fs // 100)
    frames = len(mono) // hop
    env = np.sqrt(np.mean(mono[: frames * hop].reshape(frames, hop) ** 2, axis=1)) if frames else np.zeros(1)
    peak_i = int(np.argmax(env))
    peak = float(env[peak_i])
    active = np.nonzero(env > peak * 10 ** (-60 / 20))[0]
    onset_i = int(active[0]) if len(active) else 0
    above10 = np.nonzero(env[onset_i: peak_i + 1] >= 0.1 * peak)[0]
    above90 = np.nonzero(env[onset_i: peak_i + 1] >= 0.9 * peak)[0]
    attack_ms = (above90[0] - above10[0]) * 10.0 if len(above10) and len(above90) else 0.0
    below20 = np.nonzero(env[peak_i:] < peak * 0.1)[0]
    out["envelope"] = {
        "onset_ms": onset_i * 10.0,
        "attack_ms": round(float(attack_ms), 1),
        "decay_to_minus20db_ms": round(float(below20[0] * 10.0), 1) if len(below20) else None,
        "level_at_50pct_db": round(_db(float(env[min(len(env) - 1, onset_i + (len(env) - onset_i) // 2)])) - _db(peak), 1),
        "rms_10ms_db_first_second": [round(_db(float(v)), 1) for v in env[onset_i: onset_i + 100: 5]],
    }

    # Parte con señal (desde el ataque hasta que cae 40 dB) para tono y espectro
    end_i = len(env)
    quiet = np.nonzero(env[peak_i:] < peak * 0.01)[0]
    if len(quiet):
        end_i = peak_i + int(quiet[0])
    seg = mono[onset_i * hop: max(onset_i + 5, end_i) * hop]
    out["pitch"] = _pitch_track(mono, fs, onset_i * hop, max(onset_i + 5, end_i) * hop)

    # Notas graves: con ventanas de 10 ms el RMS de un periodo largo (E1 = 24 ms) fluctúa y falsea ataque y caída. Con la
    # f0 medida, la envolvente se rehace con una ventana de 2 periodos (mínimo 10 ms), cada 5 ms
    f0 = out["pitch"].get("f0_hz")
    if f0 and 2.0 / f0 > 0.010:
        w, h = int(round(2.0 * fs / f0)), max(1, fs // 200)
        c = np.concatenate(([0.0], np.cumsum(mono ** 2)))
        idx = np.arange(0, max(1, len(mono) - w), h)
        env2 = np.sqrt((c[idx + w] - c[idx]) / w)
        pk = int(np.argmax(env2))
        on = int(np.nonzero(env2 > env2[pk] * 10 ** (-60 / 20))[0][0])
        a10 = np.nonzero(env2[on: pk + 1] >= 0.1 * env2[pk])[0]
        a90 = np.nonzero(env2[on: pk + 1] >= 0.9 * env2[pk])[0]
        d20 = np.nonzero(env2[pk:] < env2[pk] * 0.1)[0]
        out["envelope"]["attack_ms"] = round(float((a90[0] - a10[0]) * 1000.0 * h / fs), 1) if len(a10) and len(a90) else 0.0
        out["envelope"]["decay_to_minus20db_ms"] = round(float(d20[0] * 1000.0 * h / fs), 1) if len(d20) else None
        out["envelope"]["attack_resolution_ms"] = round(1000.0 * w / fs, 1)  # un ataque más corto que esto no se distingue

    # Espectro medio (Welch)
    nfft = 4096
    if len(seg) < nfft:
        seg = np.pad(seg, (0, nfft - len(seg)))
    win = np.hanning(nfft)
    starts = range(0, len(seg) - nfft + 1, nfft // 2)
    psd = np.mean([np.abs(np.fft.rfft(seg[s: s + nfft] * win)) ** 2 for s in starts], axis=0)
    freqs = np.fft.rfftfreq(nfft, 1.0 / fs)
    total = float(np.sum(psd)) + 1e-30
    mag = np.sqrt(psd)
    cum = np.cumsum(mag) / (float(np.sum(mag)) + 1e-30)
    out["spectrum"] = {
        "centroid_hz": round(float(np.sum(freqs * mag) / (np.sum(mag) + 1e-30)), 1),
        "rolloff85_hz": round(float(freqs[int(np.searchsorted(cum, 0.85))]), 1),
        "flatness": round(float(np.exp(np.mean(np.log(psd[1:] + 1e-30))) / (np.mean(psd[1:]) + 1e-30)), 4),
        "octave_bands_db": {},
    }
    for fc in OCTAVE_BANDS_HZ:
        band = (freqs >= fc / np.sqrt(2)) & (freqs < fc * np.sqrt(2))
        out["spectrum"]["octave_bands_db"][str(fc)] = round(10 * np.log10(float(np.sum(psd[band])) / total + 1e-12), 1)
    if out["pitch"]["f0_hz"]:
        f = out["pitch"]["f0_hz"]
        h1 = None
        harm = []
        for h in range(1, 9):
            k = int(round(h * f * nfft / fs))
            if k + 2 >= len(psd):
                break
            a = float(np.max(psd[k - 2: k + 3]))
            h1 = h1 or a
            harm.append(round(10 * np.log10(a / h1 + 1e-12), 1))
        out["spectrum"]["harmonics_db_rel_h1"] = harm

    # Estéreo
    mid, side = 0.5 * (left + right), 0.5 * (left - right)
    corr = float(np.corrcoef(left, right)[0, 1]) if np.std(left) > 0 and np.std(right) > 0 else 1.0
    out["stereo"] = {"correlation": round(corr, 3),
                     "side_to_mid_db": round(_db(float(np.sqrt(np.mean(side ** 2)))) - _db(float(np.sqrt(np.mean(mid ** 2)))), 1)}
    return out


def compare(reference: dict, candidate: dict) -> dict:
    """Diferencias candidate - reference de los rasgos principales y distancia de perfil espectral (dB RMS entre bandas
    de octava, normalizadas por energía total y limitadas a -60 dB)."""
    if reference.get("silent") or candidate.get("silent"):
        return {"comment": "one of the sounds is silent", "reference_silent": bool(reference.get("silent")),
                "candidate_silent": bool(candidate.get("silent"))}
    d: dict = {"rms_db": round(candidate["rms_dbfs"] - reference["rms_dbfs"], 1)}
    fa, fb = reference["pitch"].get("f0_hz"), candidate["pitch"].get("f0_hz")
    d["pitch_cents"] = round(1200 * np.log2(fb / fa), 1) if fa and fb else None
    for k in ("centroid_hz", "rolloff85_hz"):
        d[k + "_ratio"] = round(candidate["spectrum"][k] / max(reference["spectrum"][k], 1e-9), 3)
    d["flatness"] = round(candidate["spectrum"]["flatness"] - reference["spectrum"]["flatness"], 4)
    ea, eb = reference["envelope"], candidate["envelope"]
    d["attack_ms"] = round(eb["attack_ms"] - ea["attack_ms"], 1)
    if ea["decay_to_minus20db_ms"] is not None and eb["decay_to_minus20db_ms"] is not None:
        d["decay_to_minus20db_ms"] = round(eb["decay_to_minus20db_ms"] - ea["decay_to_minus20db_ms"], 1)
    ba, bb = reference["spectrum"]["octave_bands_db"], candidate["spectrum"]["octave_bands_db"]
    floor = -60.0  # bandas casi vacías (fondo de ruido frente a silencio digital) no deben dominar la distancia
    diff = {k: round(max(bb[k], floor) - max(ba[k], floor), 1) for k in ba}
    d["octave_bands_db"] = diff
    # Distancia solo en bandas audibles del sonido: por encima de max(40 Hz, 0,7·f0 más grave). Lo de debajo de la
    # fundamental es rumor de la grabación (una referencia real lo trae; un synth no) y no debe dirigir el ajuste
    lowest = min([f for f in (fa, fb) if f] or [57.0])
    used = [v for k, v in diff.items() if float(k) >= max(40.0, 0.7 * lowest)]
    d["spectral_profile_distance_db"] = round(float(np.sqrt(np.mean([v * v for v in used]))), 1)
    d["distance_bands_from_hz"] = round(max(40.0, 0.7 * lowest), 1)
    d["stereo_side_to_mid_db"] = round(candidate["stereo"]["side_to_mid_db"] - reference["stereo"]["side_to_mid_db"], 1)
    return d
