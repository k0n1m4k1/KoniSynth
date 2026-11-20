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

# Nivel de las pruebas: el de antes de outputGain (gain lineal 0,5 = -6,02 dB, sin nivel fijo por motor), para que los umbrales
# absolutos y las medidas guardadas sigan valiendo. outputGain = -6,02 dB menos el nivel del motor (engineTrimDb, Layout.h,
# leído de allí); una prueba que fija Gain / outputGain decide ella
ENGINES = ['KoniSynth', 'Theremin', 'Casio VL-1', 'Stylophone', 'Omnichord', 'Drum Machine', 'FM 6-Op (DX7)', '8-Bit/DPCM', 'Polivoks',
           'ANS', 'Mega Drive', 'SNES', 'N64', 'OPL (AdLib / Sound Blaster)', 'Roland MT-32', 'Gravis UltraSound', 'M-VAVE FM-1']


def _engine_trims():
    import re
    text = open(os.path.join(ROOT, 'Source', 'Params', 'Layout.h'), encoding='utf-8').read()
    return [float(v) for v in re.search(r'engineTrimDb\[17\] = \{([^}]*)\}', text).group(1).split(',')]


TRIMS = _engine_trims()


def legacy_level(engine):
    """--param de outputGain con el nivel de antes para ese motor (nombre del selector o índice)."""
    k = engine if isinstance(engine, int) else (ENGINES.index(engine) if engine in ENGINES else 0)
    return '--param=outputGain=%.4f' % (-6.0206 - TRIMS[k])


def render(args, secs, notes, name='t.wav', engine='KoniSynth'):
    """Render con KoniBench; devuelve (canal izquierdo, fs). Falla si algún parámetro no existe."""
    path = os.path.join(RENDERS, name)
    args = list(args)
    if not any(str(x).startswith(('Gain=', '--param=outputGain=')) for x in args):
        named = [str(x)[len('Engine='):] for x in args if str(x).startswith('Engine=')]
        args = [legacy_level(named[-1] if named else (engine or 'KoniSynth'))] + args
    cmd = [EXE, VST, 'render', path, str(secs)] + (['Engine=' + engine] if engine else []) + [SEED] + args + list(notes)
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
