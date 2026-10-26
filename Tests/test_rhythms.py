# Ritmos de usuario de punta a punta (Engines/UserRhythms.h): un MIDI escrito aquí con el mismo patrón que uno de fábrica
# suena bit a bit igual que el de fábrica en la caja de ritmos, el Omnichord (con su bajo en el canal 2) y el VL-1. Usa una
# carpeta temporal (KONISYNTH_RHYTHMS_DIR) y no toca la del usuario.
import os, shutil, struct
from common import RENDERS, Checker, render
import numpy as np

TMP = os.path.join(RENDERS, 'rhythms_tmp')
shutil.rmtree(TMP, ignore_errors=True)
os.environ['KONISYNTH_RHYTHMS_DIR'] = TMP  # lo heredan los procesos de KoniBench

TPQ = 96


def varlen(n):
    out = [n & 0x7f]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7f) | 0x80)
        n >>= 7
    return bytes(out)


def meta(kind, data):
    return b'\xff' + bytes([kind]) + varlen(len(data)) + data


def write_midi(path, name, steps, per_beat, notes):
    """MIDI tipo 0 como el que guarda KoniSynth: notes = [(paso, canal 1-16, nota, velocity)]."""
    step = TPQ // per_beat
    ev = [(0, meta(0x03, name.encode())), (0, meta(0x58, bytes([4, 2, 24, 8]))),
          (0, meta(0x01, ('KoniSynth rhythm: %d steps, %d per beat' % (steps, per_beat)).encode()))]
    for s, ch, n, v in notes:
        ev.append((s * step, bytes([0x90 | (ch - 1), n, v])))
        ev.append((s * step + step // 2, bytes([0x80 | (ch - 1), n, 0])))
    ev.sort(key=lambda e: (e[0], e[1][0] == 0x90))  # a igual tiempo, note off antes que note on
    body, now = b'', 0
    for t, e in ev:
        body += varlen(t - now) + e
        now = t
    body += varlen(steps * step - now) + meta(0x2f, b'')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, TPQ) + b'MTrk' + struct.pack('>I', len(body)) + body)


def lane(pattern, ch, note):
    return [(i, ch, note, 127 if c == 'X' else 95 if c == 'x' else 57) for i, c in enumerate(pattern) if c != '.']


c = Checker('ritmos de usuario')

# Caja de ritmos: Rock 1 de fábrica (Engines/RhythmPatterns.h) = índice 1; User 1 = 1 + 29 de fábrica
write_midi(os.path.join(TMP, 'Drum Machine', 'User 1.mid'), 'Rock copy', 16, 4,
           lane('x.......x.x.....', 10, 36) + lane('....X.......X...', 10, 38) + lane('x.x.x.x.x.x.x.x.', 10, 42))
a, _ = render(['--param=drumPattern=1'], 3.0, [], 'rh_dm_fac.wav', 'Drum Machine')
b, _ = render(['--param=drumPattern=30'], 3.0, [], 'rh_dm_user.wav', 'Drum Machine')
c.check(np.abs(a).max() > 0.01 and np.array_equal(a, b), 'caja de ritmos: User 1 = Rock 1 de fábrica, bit a bit (pico %.3f)' % np.abs(a).max())
e, _ = render(['--param=drumPattern=31'], 1.0, [], 'rh_dm_empty.wav', 'Drum Machine')
c.check(np.abs(e).max() == 0.0, 'caja de ritmos: ranura vacía (User 2) en silencio')

# Omnichord: Rock 1 de fábrica (índice 1) con su bajo; User 1 = 1 + 12. Acorde de Do mantenido (C3, por debajo de Split)
write_midi(os.path.join(TMP, 'Omnichord', 'User 1.mid'), 'Rock copy', 16, 4,
           lane('x.......x.x.....', 10, 36) + lane('....X.......X...', 10, 38) + lane('x.x.x.x.x.x.x.x.', 10, 42)
           + [(0, 2, 48, 95), (8, 2, 48, 95), (10, 2, 55, 95)])
a, _ = render(['--param=omniRhythm=1'], 3.0, ['48:0:3:100'], 'rh_om_fac.wav', 'Omnichord')
b, _ = render(['--param=omniRhythm=13'], 3.0, ['48:0:3:100'], 'rh_om_user.wav', 'Omnichord')
c.check(np.abs(a).max() > 0.01 and np.array_equal(a, b), 'Omnichord: User 1 = Rock 1 de fábrica con el bajo del canal 2, bit a bit')

# VL-1: Rock 1 de fábrica (índice 5: Pock Peek Noise Noise Pock Peek Noise Peek, corcheas) en semicorcheas; User 1 = 1 + 10
write_midi(os.path.join(TMP, 'Casio VL-1', 'User 1.mid'), 'Rock copy', 16, 4,
           lane('x.......x.......', 10, 36) + lane('..x.......x...x.', 10, 37) + lane('....x.x.....x...', 10, 42))
a, _ = render(['--param=vl1Rhythm=5'], 3.0, [], 'rh_vl_fac.wav', 'Casio VL-1')
b, _ = render(['--param=vl1Rhythm=11'], 3.0, [], 'rh_vl_user.wav', 'Casio VL-1')
c.check(np.abs(a).max() > 0.01 and np.array_equal(a, b), 'VL-1: User 1 = Rock 1 de fábrica, bit a bit (pico %.3f)' % np.abs(a).max())

shutil.rmtree(TMP, ignore_errors=True)
raise SystemExit(c.summary())
