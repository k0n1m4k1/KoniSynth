"""Prueba de valores exactos y capturas: cliente MCP (stdio) -> konisynth_mcp.py -> KoniSynth standalone con audio.

get/set con valor real, normalizado y unidad; validación del lote entero antes de cambiar nada; notas no válidas en play;
captura desde el origen del play (silencio inicial conservado), metadatos de la captura y un WAV por captura.
Copia y restaura los ficheros de configuración (como test_e2e.py).
Uso: .venv\\Scripts\\python test_exact.py
"""
import asyncio, filecmp, glob, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

import numpy as np
from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

from analysis import read_wav
from test_e2e import APP, CFG, SET, AGENT, payload  # mismas rutas y lectura de resultados

HERE = Path(__file__).resolve().parent
results = []


def check(ok, what):
    results.append(bool(ok))
    print(("ok    " if ok else "FALLO ") + what)


async def run():
    params = StdioServerParameters(command=sys.executable, args=[str(HERE / "konisynth_mcp.py")])
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as s:
            await s.initialize()

            async def tool(name, args):
                return payload(await s.call_tool(name, args))

            async def error(name, args):
                r = await s.call_tool(name, args)
                return r.content[0].text if r.is_error else None

            await s.call_tool("load_preset", {"name": "Init"})

            # --- Valores exactos y unidad ---
            d = await tool("set_parameters", {"values": {"cutoff": "2 kHz", "attack": "350 ms", "resonance": "40 %", "wave": "Saw"}})
            c, a, r, w = d["cutoff"], d["attack"], d["resonance"], d["wave"]
            check(c["unit"] == "Hz" and abs(c["value"] - 2000) < 1, "cutoff \"2 kHz\": %s = %.3f %s, normalizado %.4f" % (c["text"], c["value"], c["unit"], c["normalized"]))
            check(a["unit"] == "s" and abs(a["value"] - 0.35) < 1e-3, "attack \"350 ms\": %s = %.5f %s" % (a["text"], a["value"], a["unit"]))
            check(r["unit"] == "ratio" and abs(r["value"] - 0.4) < 1e-4, "resonance \"40 %%\": %s = %.4f %s" % (r["text"], r["value"], r["unit"]))
            check(w["unit"] == "index" and w["value"] == 2 and w["text"] == "Saw", "wave \"Saw\": índice %s (%s)" % (w["value"], w["unit"]))
            g = await tool("get_parameters", {"ids": ["cutoff"]})
            check(g["cutoff"] == c, "get_parameters devuelve el mismo detalle que set")
            n = await tool("set_parameters", {"values": {"cutoff": 1234.5}})
            check(abs(n["cutoff"]["value"] - 1234.5) < 0.01 and "clamped" not in n["cutoff"], "número = valor real: 1234,5 Hz -> %.3f (%s)" % (n["cutoff"]["value"], n["cutoff"]["text"]))
            n = await tool("set_parameters", {"values": {"cutoff": 50000, "wave": 3, "formantOn": True, "formantVowel": "E"}})
            check(n["cutoff"].get("clamped") is True and n["cutoff"]["value"] == 20000, "fuera de rango: 50000 -> %s, clamped" % n["cutoff"]["text"])
            check(n["wave"]["text"] == "Square" and n["formantOn"]["text"] == "On" and n["formantVowel"]["value"] == 1,
                  "índice 3 -> %s; true -> %s; \"E\" (texto sin cifras) -> %s" % (n["wave"]["text"], n["formantOn"]["text"], n["formantVowel"]["value"]))
            n = await tool("set_parameters", {"values": {"modLp": "Off", "cutoff": {"normalized": 0.5}}})
            check(n["modLp"]["text"] == "Off" and abs(n["cutoff"]["normalized"] - 0.5) < 1e-6, "\"Off\" (máximo) -> %s; {normalized: 0.5} -> %s" % (n["modLp"]["text"], n["cutoff"]["text"]))

            # Cobertura de la unidad en todos los parámetros
            from konisynth_mcp import link  # conexión directa (sin MCP): describe completo y peticiones a mano
            allp = link.call("describe")
            unknown = [p["id"] for p in allp if p["unit"] is None]
            units = {}
            for p in allp:
                units[str(p["unit"])] = units.get(str(p["unit"]), 0) + 1
            print("      unidades:", dict(sorted(units.items(), key=lambda kv: -kv[1])))
            print("      sin unidad (%d): %s" % (len(unknown), ", ".join(unknown)))
            check(len(allp) > 300, "describe: %d parámetros, %d sin unidad deducida" % (len(allp), len(unknown)))

            # --- Validación del lote entero ---
            before = (await tool("get_parameters", {"ids": ["cutoff", "wave"]}))
            bad = [
                ({"cutoff": "1 kHz", "wave": "Sawtooth"}, "opción desconocida"),
                ({"cutoff": "1 kHz", "formantOn": "maybe"}, "bool con texto desconocido"),
                ({"cutoff": "1 kHz", "formantVowel": "abc"}, "texto sin cifras ilegible"),
                ({"cutoff": "1 kHz", "resonance": {"normalized": 1.5}}, "normalized fuera de 0..1"),
                ({"cutoff": "1 kHz", "resonance": {"value": 0.5}}, "objeto de forma desconocida"),
                ({"cutoff": "1 kHz", "resonance": {"normalized": 0.5, "x": 1}}, "objeto con campo extra"),
                ({"cutoff": "1 kHz", "wave": 2.5}, "índice no entero"),
                ({"cutoff": "1 kHz", "wave": 99}, "índice fuera de rango"),
                ({"cutoff": "1 kHz", "resonance": True}, "true en un parámetro continuo"),
                ({"cutoff": "1 kHz", "noSuchParam": 1}, "id desconocido"),
            ]
            for values, what in bad:
                e = await error("set_parameters", {"values": values})
                check(e is not None, "rechazado (%s): %s" % (what, (e or "ACEPTADO").replace("\n", " ")[:110]))
            check(await tool("get_parameters", {"ids": ["cutoff", "wave"]}) == before, "tras los rechazos, cutoff y wave sin cambios (%s, %s)" % (before["cutoff"]["text"], before["wave"]["text"]))
            try:
                link.call("set", values={"cutoff": float("nan")})
                check(False, "NaN aceptado")
            except Exception as ex:  # noqa: BLE001
                check("finite" in str(ex), "NaN rechazado en el puente: %s" % ex)
            # NaN a mano por el socket (sin el puente): KoniSynth no lo acepta como JSON
            link._file.write(b'{"token": "%s", "id": 1, "cmd": "set", "values": {"cutoff": NaN}}\n' % link._token.encode())
            link._file.flush()
            import json
            raw = json.loads(link._file.readline())
            check(raw.get("ok") is False, "NaN directo al servidor: %s" % raw.get("error"))

            # --- play estricto ---
            await s.call_tool("load_preset", {"name": "Init"})
            badnotes = [
                ([{"note": "H4"}], "nombre no válido"), ([{"note": 128}], "nota 128"), ([{"note": "G#9"}], "G#9 > 127"),
                ([{"note": 60.5}], "nota no entera"), ([{"note": "C4", "velocity": 0}], "velocity 0"),
                ([{"note": "C4", "velocity": 64.5}], "velocity no entera"), ([{"note": "C4", "vel": 90}], "campo desconocido"),
                ([{"note": "C4", "start": 29.5, "duration": 1.0}], "acaba a 30,5 s"), ([{"velocity": 90}], "sin note"),
                ([{"note": "C4"}, {"note": "X9"}], "segunda nota no válida"), ([{"note": "C4", "start": -0.1}], "start negativo"),
            ]
            for notes, what in badnotes:
                e = await error("play_notes", {"notes": notes})
                check(e is not None, "play rechazado (%s): %s" % (what, (e or "ACEPTADO")[:100]))
            time.sleep(0.4)
            sil = await tool("listen", {"seconds": 0.3})
            check(sil.get("silent") is True, "tras los rechazos no ha sonado nada (pico %.1f dBFS)" % sil["peak_dbfs"])
            ok = link.call("play", notes=[{"note": n, "duration": 0.05} for n in ("C-1", "G9", "Bb3", "c#4", 0, 127, "60")])
            check(ok["scheduled"] == 7, "nombres válidos C-1, G9, Bb3, c#4, 0, 127, \"60\": %d notas" % ok["scheduled"])
            time.sleep(1.5)  # que acabe el release de esas notas (Init: 300 ms, cola exponencial) antes de medir el silencio

            # --- Captura desde el origen ---
            au = await tool("audition", {"notes": [{"note": "A4", "start": 0.5, "duration": 0.5}], "tail": 0.3})
            cap = au["capture"]
            check(abs(cap["requested_s"] - 1.3) < 1e-3 and cap["captured_s"] == cap["requested_s"] and not cap["partial"],
                  "audition con la nota en 0,5 s: pedido %.3f s, capturado %.3f s, partial %s" % (cap["requested_s"], cap["captured_s"], cap["partial"]))
            x, fs = read_wav(au["wav"])
            env = np.abs(x).max(axis=1) if x.ndim == 2 else np.abs(x)
            onset = int(np.argmax(env > 10 ** (-40 / 20))) / fs
            lead = 20 * np.log10(max(env[: int(0.45 * fs)].max(), 1e-9))
            check(abs(onset - 0.5) < 0.03 and lead < -80, "silencio inicial conservado: pico %.1f dBFS en 0-0,45 s; ataque (-40 dBFS) en %.4f s (esperado 0,5)" % (lead, onset))
            check(au["pitch"].get("note") == "A4", "tono %s Hz (%s)" % (au["pitch"].get("f0_hz"), au["pitch"].get("note")))
            au2 = await tool("audition", {"notes": "A3", "duration": 0.3, "tail": 0.1})
            check(au2["wav"] != au["wav"] and Path(au["wav"]).exists() and Path(au2["wav"]).exists(),
                  "un WAV por captura: %s, %s (los dos existen)" % (Path(au["wav"]).name, Path(au2["wav"]).name))
            e = await error("audition", {"notes": [{"note": "C4", "start": 9.0, "duration": 1.0}], "tail": 0.6})
            check(e is not None and "10" in e, "audition de 10,6 s rechazado antes de tocar: %s" % (e or "")[:100])
            time.sleep(0.3)
            sil = await tool("listen", {"seconds": 0.25})
            check(sil.get("silent") is True, "y no ha sonado (pico %.1f dBFS)" % sil["peak_dbfs"])
            for secs in (11, 0.01):
                e = await error("listen", {"seconds": secs})
                check(e is not None, "listen %s s rechazado: %s" % (secs, (e or "ACEPTADO")[:80]))
            for secs in (11, 0.01):
                try:
                    link.call("listen", seconds=secs)
                    check(False, "servidor: listen %s s aceptado" % secs)
                except Exception as ex:  # noqa: BLE001
                    check("0.05" in str(ex), "servidor: listen %s s rechazado: %s" % (secs, ex))
            await s.call_tool("load_preset", {"name": "Init"})


def main():
    bak = {f: Path(tempfile.gettempdir()) / (f.name + ".exbak") for f in (CFG, SET)}
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
