"""Prueba del estado de tempo y reloj (status -> clock): cliente MCP -> konisynth_mcp.py -> KoniSynth standalone con audio.

Sin host (standalone) no debe inventar datos del host; tempo efectivo = Arp BPM; reloj propio de Drum Machine y VL-1
(fórmula del procesador); posición de la captura que avanza con el audio; transformaciones activas de las notas en
status, play_notes y audition. Copia y restaura los ficheros de configuración (como test_e2e.py).
Uso: .venv\\Scripts\\python test_state.py
"""
import asyncio, filecmp, glob, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

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

            async def clock():
                time.sleep(0.15)  # al menos un bloque con los valores nuevos
                return (await tool("status", {}))["clock"]

            await s.call_tool("load_preset", {"name": "Init"})
            await s.call_tool("set_parameters", {"values": {"arpBpm": 97}})
            c = await clock()
            print("      clock:", c)
            check(c["audioRunning"] and c["blockSize"] > 0 and c["sampleRate"] > 0,
                  "audio: %.0f Hz, bloque %d, latencia %d muestras = %.3f ms" % (c["sampleRate"], c["blockSize"], c["latencySamples"], c["latencyMs"]))
            check(abs(c["latencyMs"] - 1000 * c["latencySamples"] / c["sampleRate"]) < 1e-9, "latencia en ms coherente con las muestras")
            # El reproductor de JUCE del standalone da solo la posición en muestras (juce_AudioProcessorPlayer.cpp:285-291)
            check("bpm" not in c["host"] and "ppq" not in c["host"] and c["host"]["playing"] is False and "timeInSamples" in c["host"],
                  "standalone: el host no da BPM ni PPQ y no se inventan (%s)" % c["host"])
            check(c["tempo"]["bpm"] == 97 and c["tempo"]["source"] == "internal (arpBpm)", "tempo efectivo: %s BPM, fuente %s" % (c["tempo"]["bpm"], c["tempo"]["source"]))
            check("rhythmClock" not in c and c["displayBpm"] == 97, "Synth: sin reloj propio; display %s BPM" % c["displayBpm"])
            t0, f0 = time.perf_counter(), c["blockStartFrame"]
            c2 = await clock()
            dt = time.perf_counter() - t0
            adv = (c2["blockStartFrame"] - f0) / c2["sampleRate"]
            check(abs(adv - dt) < 0.1, "la posición de captura avanza con el audio: %.3f s en %.3f s de reloj" % (adv, dt))

            await s.call_tool("set_parameters", {"values": {"engine": "Drum Machine", "drumTempo": 133}})
            c = await clock()
            check(c["rhythmClock"] == {"bpm": 133, "source": "internal (drumTempo)"} and c["displayBpm"] == 133 and c["tempo"]["bpm"] == 97,
                  "Drum Machine: reloj %s; display %s; tempo del arpegiador sigue en %s" % (c["rhythmClock"], c["displayBpm"], c["tempo"]["bpm"]))
            await s.call_tool("set_parameters", {"values": {"engine": "Casio VL-1", "vl1Tempo": 0}})
            c = await clock()
            want = 60.0 / (32.0 * (0.0055 + 9 * 0.00098))  # Source/KoniSynth.cpp: negra = 32 ticks de (5,5 + (9 - tempo)·0,98) ms
            check(abs(c["rhythmClock"]["bpm"] - want) < 0.01 and "never follows" in c["rhythmClock"]["source"],
                  "VL-1 tempo 0: %.3f BPM (calculado %.3f)" % (c["rhythmClock"]["bpm"], want))

            await s.call_tool("load_preset", {"name": "Init"})
            c = await clock()
            check(c["noteTransforms"] == [], "Init: sin transformaciones de notas")
            await s.call_tool("set_parameters", {"values": {"keyTranspose": 2, "arpOn": True}})
            p = await tool("play_notes", {"notes": "C4", "duration": 0.1})
            tr = p.get("transforms", [])
            check(len(tr) == 2 and "transpose +2" in tr[0] and "arpeggiator" in tr[1], "play_notes avisa: %s" % tr)
            await s.call_tool("set_parameters", {"values": {"seqOn": True}})
            c = await clock()
            check(any("sequencer" in t for t in c["noteTransforms"]) and not any("arpeggiator" in t for t in c["noteTransforms"]),
                  "con seqOn el secuenciador sustituye al arpegiador: %s" % c["noteTransforms"])
            a = await tool("audition", {"notes": "C4", "duration": 0.2, "tail": 0.1})
            check(len(a.get("transforms", [])) == 2, "audition también lo avisa: %s" % a.get("transforms"))
            check(c["capturePoints"]["output"].startswith("final output") and "engine" in c["capturePoints"], "puntos de captura: %s" % c["capturePoints"])
            await s.call_tool("stop_notes", {})
            await s.call_tool("load_preset", {"name": "Init"})


def main():
    bak = {f: Path(tempfile.gettempdir()) / (f.name + ".stbak") for f in (CFG, SET)}
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
