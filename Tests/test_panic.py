# PANIC (aquí con CC 120, el mismo panic() que el botón y la acción de MIDI learn): silencio total desde el bloque en que llega.
# Por motor, una nota sostenida con reverb, muelles, flanger, eco y Leslie: tras el panic, el pico no pasa del de ese motor en
# reposo (sin notas; algunos chips dan continua en reposo). Los ritmos que suenan solos (Drum Machine, Omnichord) y ANS con
# Hold quedan parados hasta la siguiente nota y vuelven con ella.
import os, subprocess, sys
import numpy as np
from common import EXE, VST, RENDERS, SEED, Checker, analysis

c = Checker('panic')
FS, BS = 48000, 256  # render de KoniBench: 48 kHz, bloques de 256
FX = ['--param=ambientOn=1', '--param=springOn=1', '--param=flangerOn=1', '--param=echoOn=1', '--param=leslieOn=1']
START = (FS // BS) * BS  # bloque que contiene el CC 120 de t = 1,0 s: el panic se aplica al principio del bloque


def peak(args, secs, notes):
    """Pico por muestra (máximo de los dos canales) de un render estéreo."""
    path = os.path.join(RENDERS, 'panic.wav')
    r = subprocess.run([EXE, VST, 'render', path, str(secs), '--stereo', SEED] + args + notes, capture_output=True, text=True)
    if r.returncode != 0 or 'aviso' in r.stdout:
        raise RuntimeError('render falló: %s\n%s' % (' '.join(args + notes), r.stdout + r.stderr))
    x, _ = analysis.read_wav(path)
    return np.abs(x).max(axis=1) if x.ndim > 1 else np.abs(x)


def db(v):
    return 20 * np.log10(max(float(v), 1e-9))


ENGINES = ['Synth', 'Theremin', 'Casio VL-1', 'Stylophone', 'Omnichord', 'Drum Machine', 'FM 6-Op', '8-Bit/DPCM',
           'Polivoks', 'ANS', 'Mega Drive', 'SNES', 'N64', 'OPL (AdLib / Sound Blaster)', 'Roland MT-32']
for k, name in enumerate(ENGINES):
    base = ['--param=engine=%d' % k] + FX
    held = peak(base, 1.6, ['60:0.1:1.5:100', 'cc:1.0:120:0:1'])
    idle = peak(base, 1.6, [])
    before, after, rest = held[FS // 2:FS].max(), held[START:].max(), idle[START:].max()
    c.check(after <= rest * 1.0001 + 1e-9, '%-28s antes %6.1f dBFS; tras el panic %6.1f dBFS (reposo %6.1f)' % (name, db(before), db(after), db(rest)))

for name, args in (('Drum Machine, patrón 5', ['--param=engine=5', '--param=drumPattern=5']),
                   ('Omnichord, ritmo 1', ['--param=engine=4', '--param=omniRhythm=1']),
                   ('ANS con Hold', ['--param=engine=9', '--param=ansHold=1', '--param=ansScore=1'])):
    x = peak(args, 3.2, ['cc:1.0:120:0:1', '60:2.5:0.5:100'])
    note_block = (int(2.5 * FS) // BS) * BS  # vuelve en el bloque de la nota (desde su principio: hasta 5,3 ms antes)
    before, stopped, again = x[FS // 5:FS].max(), x[START + BS:note_block].max(), x[note_block:].max()
    c.check(before > 1e-3 and stopped == 0 and again > 1e-3,
            '%-28s antes %6.1f dBFS; parado %6.1f dBFS hasta la nota; con ella %6.1f dBFS' % (name, db(before), db(stopped), db(again)))
sys.exit(c.summary())
