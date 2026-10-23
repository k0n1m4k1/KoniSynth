"""Prueba de F4 (generar con conocimiento): cliente MCP -> KoniSynth-mcp -> KoniSynth standalone.

Comprueba: candidatos válidos que cumplen el estilo, reproducibilidad con la semilla, orden por parecido a una
referencia, use_candidate y mutate. Copia y restaura los ficheros de configuración.
Uso: .venv\\Scripts\\python test_f4.py
"""
import asyncio, filecmp, glob, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

from test_e2e import APP, CFG, SET, AGENT, payload
from test_f3 import write_sine
import generator

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
            tools = {t.name for t in (await s.list_tools()).tools}
            check({"generate", "use_candidate", "mutate"} <= tools, "herramientas F4 presentes")
            presets = payload(await s.call_tool("list_presets", {}))
            cats = sorted({p["category"] for p in presets if p["factory"]})
            missing = [st for st, r in generator.STYLES.items() if r["category"] not in cats]
            check(not missing, "estilos con presets de fábrica (categorías: %s); sin presets: %s" % (cats, missing))

            t0 = time.perf_counter()
            g1 = payload(await s.call_tool("generate", {"style": "pad", "count": 2, "seed": 42, "max_tries": 6}))
            dt = time.perf_counter() - t0
            acc = g1["accepted"]
            check(len(acc) >= 1 and all(c["measured"]["attack_ms"] >= 60 and c["measured"]["note"] for c in acc),
                  "generate pad seed 42: %d aceptados, %d descartados en %.1f s; %s" % (len(acc), len(g1["rejected"]), dt,
                  [(c["base_preset"], c["engine"], c["changed"], c["measured"]["note"], c["measured"]["attack_ms"]) for c in acc]))
            for r in g1["rejected"]:
                print("      descartado:", r["base_preset"], r["rejected"])
            g2 = payload(await s.call_tool("generate", {"style": "pad", "count": 2, "seed": 42, "max_tries": 6}))
            print("      run1:", [(a["attempt"], a["base_preset"], a["changed"], a["measured"]["centroid_hz"], a["measured"]["peak_dbfs"]) for a in g1["accepted"]])
            print("      run2:", [(a["attempt"], a["base_preset"], a["changed"], a["measured"]["centroid_hz"], a["measured"]["peak_dbfs"]) for a in g2["accepted"]])
            same = [(a["base_preset"], a["changed"]) for a in g1["accepted"]] == [(a["base_preset"], a["changed"]) for a in g2["accepted"]]
            cents = [abs((a["measured"]["centroid_hz"] or 0) / max(b["measured"]["centroid_hz"] or 1, 1) - 1) for a, b in zip(g1["accepted"], g2["accepted"])]
            check(same and all(c < 0.05 for c in cents), "misma semilla -> mismos candidatos (centroide difiere %s)" % ["%.1f %%" % (100 * c) for c in cents])

            ref = Path(tempfile.gettempdir()) / "koni_ref_sine65.wav"
            write_sine(ref, f=65.41, seconds=1.0)  # C2
            g3 = payload(await s.call_tool("generate", {"style": "bass", "count": 3, "seed": 7, "max_tries": 8, "reference_wav": str(ref)}))
            d = [c.get("distance_db") for c in g3["accepted"]]
            check(len(d) >= 2 and d == sorted(d), "generate bass con referencia (seno C2): distancias ordenadas %s" % d)
            ref.unlink()

            if g3["accepted"]:
                st = payload(await s.call_tool("use_candidate", {"n": 0}))
                check(st["preset"] == g3["accepted"][0]["base_preset"], "use_candidate 0: %s" % st)
            mu = payload(await s.call_tool("mutate", {"amount": 0.2, "seed": 5}))
            check(len(mu["changed"]) > 3, "mutate 0.2 seed 5: %d parámetros cambiados (p. ej. %s)" % (len(mu["changed"]), list(mu["changed"].items())[:3]))
            bad = await s.call_tool("generate", {"style": "polka"})
            check(bad.is_error and "unknown style" in bad.content[0].text, "estilo desconocido -> error: %s" % bad.content[0].text[:90])


def main():
    bak = {f: Path(tempfile.gettempdir()) / (f.name + ".f4bak") for f in (CFG, SET)}
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
