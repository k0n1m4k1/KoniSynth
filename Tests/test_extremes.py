# Extremos del Synth y estado retenido del MIDI:
# 1) Fases en [0, 1) con más de un ciclo por muestra: nota 127 con Pitch Env +48 st (sierra) y el modulador del modo complejo
#    con Ratio 16 y AM 100 %. Antes, la fase crecía sin límite y la voz daba picos de 10^3-10^5.
# 2) Sync de Osc 2 sobre wavetable por encima de fs (Octave +2, Semi +12, Pitch Env +48 st): la fase tras el reinicio pasaba
#    de 1 y la lectura salía de la tabla.
# 3) CC 123 (All Notes Off) con arpegiador o secuenciador y una tecla pulsada (y con Latch): silencio tras el release.
#    Antes, el estado retenido volvía a generar notas.
import os, subprocess, sys
import numpy as np
from common import EXE, VST, RENDERS, SEED, Checker, analysis

c = Checker('extremes')
FS = 48000


def render(args, secs, notes):
    path = os.path.join(RENDERS, 'extremes.wav')
    r = subprocess.run([EXE, VST, 'render', path, str(secs), '--stereo', SEED] + args + notes, capture_output=True, text=True)
    if r.returncode != 0 or 'aviso' in r.stdout:
        raise RuntimeError('render falló: %s\n%s' % (' '.join(args + notes), r.stdout + r.stderr))
    x, _ = analysis.read_wav(path)
    return np.abs(x).max(axis=1) if x.ndim > 1 else np.abs(x)


def db(v):
    return 20 * np.log10(max(float(v), 1e-9))


SYNTH = ['--param=engine=0', '--param=filterOn=0']
# 1 y 2: el pico de salida debe ser finito y del orden de una nota normal (< 0,5 = -6 dBFS; la nota de control A4 da
# -22 dBFS). La salida se recorta a 0 dBFS: con la fase desbocada el pico llega a ese techo.
# La lectura fuera de la wavetable (caso 3) no se ve en la salida (lee floats válidos de otro nivel): ahí la prueba solo
# vigila que siga finito y acotado
control = render(SYNTH, 1.0, ['69:0.0:0.8:100']).max()
for name, args in (('Sierra, nota 127, Pitch Env +48 st 2 s', ['--param=wave=2', '--param=pitchEnvAmount=48', '--param=pitchEnvDecay=2000']),
                   ('Complejo: Saw Ratio 16, AM 100 %', ['--param=complexOn=1', '--param=modWave=2', '--param=modRatio=16',
                                                        '--param=fmIndex=0', '--param=amDepth=1']),
                   ('Osc 2 wavetable con sync > fs', ['--param=osc2WtTable=0', '--param=osc2Octave=2',
                                                     '--param=osc2Semi=12', '--param=osc2Sync=1', '--param=osc2WtLevel=1',
                                                     '--param=pitchEnvAmount=48', '--param=pitchEnvDecay=2000'])):
    x = render(SYNTH + args, 1.0, ['127:0.0:0.8:100'])
    p = x.max()
    c.check(np.isfinite(x).all() and p < 0.5, '%-40s pico %6.1f dBFS (control A4 %6.1f dBFS)' % (name, db(p), db(control)))

# 3: tecla pulsada de 0,1 a 3,0 s (o soltada a 0,3 s con Latch) y CC 123 a 0,6 s; de 1,6 a 3,0 s debe haber silencio
for name, args, note in (('Arpegiador', ['--param=arpOn=1'], '60:0.1:2.9:100'),
                         ('Arpegiador con Latch', ['--param=arpOn=1', '--param=arpLatch=1'], '60:0.1:0.2:100'),
                         ('Secuenciador', ['--param=seqOn=1'], '60:0.1:2.9:100'),
                         ('Secuenciador con Latch', ['--param=seqOn=1', '--param=seqLatch=1'], '60:0.1:0.2:100')):
    x = render(['--param=engine=0'] + args, 3.0, [note, 'cc:0.6:123:0:1'])
    before, after = x[int(0.15 * FS):int(0.6 * FS)].max(), x[int(1.6 * FS):].max()
    c.check(before > 1e-3 and after < 1e-4, '%-28s antes del CC 123 %6.1f dBFS; de 1,6 a 3,0 s %6.1f dBFS' % (name, db(before), db(after)))
sys.exit(c.summary())
