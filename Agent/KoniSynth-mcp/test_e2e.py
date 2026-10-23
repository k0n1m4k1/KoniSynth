"""Prueba de extremo a extremo de KoniSynth-mcp: cliente MCP (stdio) -> konisynth_mcp.py -> KoniSynth standalone.

Arranca el standalone con Agent control activado (copia y restaura config.settings y KoniSynth.settings, con
comparación byte a byte), llama a cada herramienta y comprueba los resultados.
Uso: .venv\\Scripts\\python test_e2e.py
"""
import asyncio, filecmp, glob, json, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

HERE = Path(__file__).resolve().parent
APP = HERE.parents[1] / "build" / "KoniSynth_artefacts" / "Release" / "Standalone" / "KoniSynth.exe"
K = Path(os.environ["APPDATA"]) / "KoniSynth"
CFG, SET, AGENT = K / "config.settings", K / "KoniSynth.settings", K / "agent"
results = []


def check(ok, what):
    results.append(bool(ok))
    print(("ok    " if ok else "FALLO ") + what)


def payload(r):
    """Contenido de un CallToolResult: structured si lo hay, si no el JSON del primer texto."""
    if getattr(r, "structured_content", None) is not None:
        sc = r.structured_content
        return sc.get("result", sc) if isinstance(sc, dict) and set(sc) == {"result"} else sc
    return json.loads(r.content[0].text)


async def run():
    params = StdioServerParameters(command=sys.executable, args=[str(HERE / "konisynth_mcp.py")])
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as s:
            init = await s.initialize()
            check(init.server_info.name == "KoniSynth-mcp", "initialize: %s %s" % (init.server_info.name, init.server_info.version))
            tools = sorted(t.name for t in (await s.list_tools()).tools)
            check(tools == sorted(["list_instances", "status", "describe_parameters", "get_parameters", "set_parameters",
                                   "list_presets", "load_preset", "save_preset", "play_notes", "stop_notes", "audition", "listen", "compare", "generate", "use_candidate", "mutate"]), "herramientas: %s" % tools)
            t0 = time.perf_counter()
            st = payload(await s.call_tool("status", {}))
            check(st.get("product") == "KoniSynth", "status: %s (%.1f ms ida y vuelta MCP+TCP)" % (st, (time.perf_counter() - t0) * 1000))
            inst = payload(await s.call_tool("list_instances", {}))
            check(len(inst) == 1 and inst[0]["alive"], "list_instances: %s" % inst)
            groups = payload(await s.call_tool("describe_parameters", {}))
            check(groups["total"] >= 680 and "fm" in groups["groups"], "describe sin match: %d parámetros, %d grupos" % (groups["total"], len(groups["groups"])))
            cut = payload(await s.call_tool("describe_parameters", {"match": "cutoff"}))
            check(any(p["id"] == "cutoff" and p.get("help") for p in cut), "describe match=cutoff: %s" % [p["id"] for p in cut])
            res = payload(await s.call_tool("set_parameters", {"values": {"cutoff": "1.2 kHz", "resonance": 0.55, "filterMode": "BP 12"}}))
            check(res == {"cutoff": "1.20 kHz", "resonance": "55 %", "filterMode": "BP 12"}, "set_parameters: %s" % res)
            got = payload(await s.call_tool("get_parameters", {"ids": ["cutoff", "resonance"]}))
            check(got["cutoff"] == "1.20 kHz", "get_parameters: %s" % got)
            bad = await s.call_tool("set_parameters", {"values": {"noExiste": 1}})
            check(bad.is_error and "noExiste" in bad.content[0].text, "set con id inexistente -> error visible para el agente: %s" % bad.content[0].text[:80])
            pads = payload(await s.call_tool("list_presets", {"category": "Pad"}))
            check(len(pads) > 3, "list_presets category=Pad: %d (%s...)" % (len(pads), [p["name"] for p in pads[:3]]))
            ld = payload(await s.call_tool("load_preset", {"name": pads[0]["name"]}))
            check(ld["preset"] == pads[0]["name"], "load_preset: %s" % ld)
            name = "MCP Test %d" % int(time.time())
            sv = payload(await s.call_tool("save_preset", {"name": name, "category": "Pad"}))
            check(sv["preset"] == name, "save_preset: %s" % sv)
            user = Path(os.environ["APPDATA"]) / "KoniSynth" / "Presets"
            files = list(user.glob(name + "*.xml"))
            check(len(files) == 1, "fichero del preset de usuario: %s" % [f.name for f in files])
            for f in files:
                f.unlink()  # la prueba no deja presets


def main():
    bak = {f: Path(tempfile.gettempdir()) / (f.name + ".e2ebak") for f in (CFG, SET)}
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
