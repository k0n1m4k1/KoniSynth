# Regresión bit a bit: render de 12 motores con semilla fija y SHA-256 del WAV frente a Tests/regression_ref.json.
# "python test_regression.py save" regenera la referencia (solo tras un cambio de sonido intencionado y verificado).
import os, sys, hashlib, json, wave
import numpy as np
from common import EXE, VST, RENDERS, SEED, Checker
import subprocess

CASES = {
    'synth': ['Engine=Synth', 'Unison Voices=3', 'Unison Spread=0.8'],
    'theremin': ['Engine=Theremin', 'Theremin Volume=0.8', 'Theremin Pitch=0.5'],
    'vl1': ['Engine=Casio VL-1', 'VL-1 Rhythm=Rock 1'],
    'stylo': ['Engine=Stylophone'], 'omni': ['Engine=Omnichord', 'Omnichord Rhythm=Rock 1'],
    'drums': ['Engine=Drum Machine', 'Drum Pattern=Rock 1'], 'fm': ['Engine=FM 6-Op', 'FM OP2 Level=80'],
    'chip': ['Engine=8-Bit/DPCM', '8-Bit/DPCM Voice=Poly'], 'polivoks': ['Engine=Polivoks'], 'ans': ['Engine=ANS'],
    'md': ['Engine=Mega Drive', 'MD OP3 Total Level=30', 'MD Algorithm=4'], 'snes': ['Engine=SNES', 'SNES Echo=1', 'SNES Source=Strings'],
}
NOTES = ['60:0.05:0.6:100', '64:0.1:0.5:90', '67:0.15:0.4:80']
REF = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'regression_ref.json')

res = {}
for k, p in CASES.items():
    path = os.path.join(RENDERS, 'rf_' + k + '.wav')
    r = subprocess.run([EXE, VST, 'render', path, '1.2'] + p + [SEED] + NOTES, capture_output=True, text=True)
    if r.returncode != 0:
        print(k, r.stdout, r.stderr); sys.exit(1)
    w = wave.open(path); raw = w.readframes(w.getnframes())
    b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
    x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
    x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    res[k] = dict(h=hashlib.sha256(raw).hexdigest()[:16], rms=float(20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)))

if sys.argv[1:] == ['save']:
    json.dump(res, open(REF, 'w'), indent=1); print('referencia guardada')
    sys.exit(0)
old = json.load(open(REF))
c = Checker('regresión')
for k in res:
    c.check(res[k]['h'] == old[k]['h'], '%-9s %s (RMS %+.2f dB)' % (k, 'idéntico' if res[k]['h'] == old[k]['h'] else 'DISTINTO', res[k]['rms'] - old[k]['rms']))
sys.exit(c.summary())
