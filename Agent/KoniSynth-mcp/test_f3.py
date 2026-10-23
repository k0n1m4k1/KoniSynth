"""Prueba de F3 (escuchar): cliente MCP -> KoniSynth-mcp -> KoniSynth standalone con el dispositivo de audio en marcha.

Comprueba con medidas conocidas: silencio, tono de A4/A2 (cents), brillo según el cutoff, comparación con un seno de
referencia generado aquí y que no quedan notas colgadas. Copia y restaura los ficheros de configuración.
Uso: .venv\\Scripts\\python test_f3.py
"""
import asyncio, filecmp, glob, os, shutil, struct, subprocess, sys, tempfile, time
from pathlib import Path

import numpy as np
from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

from test_e2e import APP, CFG, SET, AGENT, payload  # mismas rutas y lectura de resultados

HERE = Path(__file__).resolve().parent
results = []


def check(ok, what):
    results.append(bool(ok))
    print(("ok    " if ok else "FALLO ") + what)


def write_sine(path, f=440.0, seconds=1.5, fs=48000, amp=0.3):
    t = np.arange(int(seconds * fs)) / fs
    x = (amp * np.sin(2 * np.pi * f * t)).astype("<f4")
    st = np.repeat(x, 2)
    data = st.tobytes()
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 3, 2, fs, fs * 8, 8, 32)
    Path(path).write_bytes(hdr + b"data" + struct.pack("<I", len(data)) + data)


async def run():
    params = StdioServerParameters(command=sys.executable, args=[str(HERE / "konisynth_mcp.py")])
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as s:
            await s.initialize()
            tools = {t.name for t in (await s.list_tools()).tools}
            check({"play_notes", "stop_notes", "audition", "listen", "compare"} <= tools, "herramientas F3 presentes")
            await s.call_tool("load_preset", {"name": "Init"})
            sil = payload(await s.call_tool("listen", {"seconds": 0.5}))
            check(sil.get("silent") is True, "listen sin tocar: silencio (pico %.1f dBFS)" % sil["peak_dbfs"])
            t0 = time.perf_counter()
            a4 = payload(await s.call_tool("audition", {"notes": "A4", "duration": 0.8}))
            dt = time.perf_counter() - t0
            p = a4.get("pitch", {})
            check(p.get("note") == "A4" and abs(p.get("cents") or 99) < 15,
                  "audition A4 (Init): %s Hz, %s %+.1f cents; pico %.1f dBFS; %.2f s de herramienta" % (p.get("f0_hz"), p.get("note"), p.get("cents") or 0, a4["peak_dbfs"], dt))
            a2 = payload(await s.call_tool("audition", {"notes": "A2", "duration": 0.8}))
            check(a2["pitch"].get("note") == "A2", "audition A2: %s Hz (%s)" % (a2["pitch"].get("f0_hz"), a2["pitch"].get("note")))
            await s.call_tool("set_parameters", {"values": {"wave": "Saw", "cutoff": "300 Hz"}})
            dark = payload(await s.call_tool("audition", {"notes": "A3", "duration": 0.8}))
            await s.call_tool("set_parameters", {"values": {"cutoff": "8 kHz"}})
            bright = payload(await s.call_tool("audition", {"notes": "A3", "duration": 0.8}))
            cd, cb = dark["spectrum"]["centroid_hz"], bright["spectrum"]["centroid_hz"]
            check(cb > 2 * cd, "brillo: centroide %.0f Hz (cutoff 300 Hz) -> %.0f Hz (8 kHz)" % (cd, cb))
            pc = bright["pitch"]
            check(pc.get("curve_cents") and pc.get("note") == "A3", "curva de tono A3: %d puntos, glide inicial %s ct, deriva %s ct, vibrato %s" % (len(pc.get("curve_cents") or []), pc.get("glide_start_cents"), pc.get("drift_cents"), pc.get("vibrato")))
            await s.call_tool("load_preset", {"name": "Init"})
            await s.call_tool("audition", {"notes": "A4", "duration": 1.2, "tail": 0.3})
            ref = Path(tempfile.gettempdir()) / "koni_ref_sine440.wav"
            write_sine(ref)
            cmp = payload(await s.call_tool("compare", {"reference_wav": str(ref)}))
            d = cmp["difference"]
            check(d["pitch_cents"] is not None and abs(d["pitch_cents"]) < 15,
                  "compare con seno 440 Hz: %+.1f cents, distancia de perfil %.1f dB, centroide x%.2f" % (d["pitch_cents"], d["spectral_profile_distance_db"], d["centroid_hz_ratio"]))
            print("      bandas (dB, captura - seno):", d["octave_bands_db"], "armónicos captura:", cmp["candidate_features"]["spectrum"].get("harmonics_db_rel_h1"))
            bad = await s.call_tool("compare", {"reference_wav": "C:/no/existe.wav"})
            check(bad.is_error, "compare con fichero inexistente -> error: %s" % bad.content[0].text[:70])
            await s.call_tool("play_notes", {"notes": "C4 E4 G4", "duration": 5.0})
            time.sleep(0.5)
            await s.call_tool("stop_notes", {})
            time.sleep(1.0)  # release del Init: 300 ms
            after = payload(await s.call_tool("listen", {"seconds": 0.3}))
            check(after.get("silent") is True, "stop_notes: sin notas colgadas (pico %.1f dBFS)" % after["peak_dbfs"])
            ref.unlink()


def main():
    bak = {f: Path(tempfile.gettempdir()) / (f.name + ".f3bak") for f in (CFG, SET)}
    for f, b in bak.items():
        shutil.copy2(f, b)
    p = None
    try:
        for a in glob.glob(str(AGENT / "*.json")):
            os.remove(a)
        CFG.write_text(CFG.read_text(encoding="utf-8").replace("</PROPERTIES>", '  <VALUE name="agentControl" val="1"/>\n</PROPERTIES>'), encoding="utf-8")
        p = subprocess.Popen([str(APP)])
        time.sleep(5)
        asyncio.run(run())
    finally:
        if p is not None:
            p.kill(); p.wait()
        for f, b in bak.items():
            shutil.copy2(b, f)
        for a in glob.glob(str(AGENT / "*.json")):
            os.remove(a)
        print("config y settings restaurados, idénticos:", all(filecmp.cmp(b, f, shallow=False) for f, b in bak.items()))
        print("%d/%d comprobaciones" % (sum(results), len(results)))


if __name__ == "__main__":
    main()
