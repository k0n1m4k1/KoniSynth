"""Prueba del cliente del puente (KoniLink) contra un servidor falso del protocolo de KoniSynth: no hace falta KoniSynth.

Comprueba que una orden no idempotente (play) no se repite si la conexión cae después de enviarla (KoniSynth pudo
ejecutarla: el error lo dice) y que una idempotente (get) sí se reintenta y responde.
Uso: .venv\\Scripts\\python test_link.py
"""
import json, socket, sys, threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import konisynth_mcp as k  # noqa: E402

received = []
drop = {"play": 1, "get": 1}  # la primera de cada una: el servidor cuelga sin responder

srv = socket.socket()
srv.bind(("127.0.0.1", 0))
srv.listen(5)
port = srv.getsockname()[1]


def serve() -> None:
    while True:
        try:
            c, _ = srv.accept()
        except OSError:
            return
        f = c.makefile("rwb")
        while line := f.readline():
            req = json.loads(line)
            received.append(req["cmd"])
            if drop.get(req["cmd"], 0) > 0:
                drop[req["cmd"]] -= 1
                c.close()
                break
            f.write((json.dumps({"id": req["id"], "ok": True, "result": {"cmd": req["cmd"]}}) + "\n").encode())
            f.flush()


def main() -> int:
    threading.Thread(target=serve, daemon=True).start()
    k.KoniLink.announces = staticmethod(lambda: [{"port": port, "token": "t", "started": "1"}])
    link = k.KoniLink()
    ok = True

    def check(cond: bool, text: str) -> None:
        nonlocal ok
        ok &= cond
        print(("  OK    " if cond else "  FALLO ") + text)

    try:
        link.call("play", notes=[{"note": 60}])
        check(False, "play tras una caída: debía dar error")
    except k.ToolError as e:
        check("may have been executed" in str(e) and received.count("play") == 1,
              f"play: un solo envío ({received.count('play')}) y error explícito")
    check(link.call("get", ids=["cutoff"]) == {"cmd": "get"} and received.count("get") == 2,
          f"get: reintentado tras la caída ({received.count('get')} envíos) y respondido")
    print("órdenes recibidas:", received)
    srv.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
