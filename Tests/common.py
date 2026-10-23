# Utilidades comunes de las pruebas: rutas del repositorio, render con KoniBench y comprobaciones con tolerancia.
# Requisitos: compilar antes KoniSynth_VST3 y KoniBench (Release); Python con numpy.
import os, sys, subprocess
sys.stdout.reconfigure(encoding='utf-8')
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'build')
EXE = os.path.join(BUILD, 'KoniBench_artefacts', 'Release', 'KoniBench.exe')
VST = os.path.join(BUILD, 'KoniSynth_artefacts', 'Release', 'VST3', 'KoniSynth.vst3')
RENDERS = os.path.join(ROOT, 'Tests', 'renders')
os.makedirs(RENDERS, exist_ok=True)
sys.path.insert(0, os.path.join(ROOT, 'Agent', 'KoniSynth-mcp'))
import analysis  # noqa: E402  (read_wav)

SEED = '--prop=randomSeed=5EED'  # aleatoriedad reproducible (Dsp/RandomClock.h)


def render(args, secs, notes, name='t.wav', engine='Synth'):
    """Render con KoniBench; devuelve (canal izquierdo, fs). Falla si algún parámetro no existe."""
    path = os.path.join(RENDERS, name)
    cmd = [EXE, VST, 'render', path, str(secs)] + (['Engine=' + engine] if engine else []) + [SEED] + list(args) + list(notes)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or 'aviso' in r.stdout:
        raise RuntimeError('render falló: %s\n%s' % (' '.join(cmd[4:]), r.stdout + r.stderr))
    x, fs = analysis.read_wav(path)
    return (x[:, 0] if x.ndim > 1 else x), fs


def db(v):
    return 20 * np.log10(np.maximum(np.abs(v), 1e-12))


class Checker:
    """Acumula comprobaciones: check(cond, texto). summary() imprime el total y devuelve el código de salida."""

    def __init__(self, name):
        self.name, self.ok, self.total = name, 0, 0

    def check(self, cond, text):
        self.total += 1
        self.ok += bool(cond)
        print(('  OK    ' if cond else '  FALLO ') + text, flush=True)
        return cond

    def summary(self):
        print('%s: %d/%d' % (self.name, self.ok, self.total))
        return 0 if self.ok == self.total else 1
