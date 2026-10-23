# MIDI learn de acciones: un CC asignado a PANIC silencia; sin flanco (127 -> 100) no repite; tras 0 -> 127 sí
import sys
import numpy as np
from common import render, db, Checker

c = Checker('MIDI learn')
NOTES = ['60:0:2.0:100', '64:1.0:1.0:100']
CCS = ['cc:0.5:20:127:1', 'cc:1.3:20:100:1', 'cc:1.5:20:0:1', 'cc:1.7:20:127:1']
BASE = ['Sustain=100%', 'Release=3000 ms']


def rms(x, fs, a, b):
    return float(db(np.sqrt(np.mean(x[int(a * fs):int(b * fs)] ** 2))))


x, fs = render(BASE + CCS + ['--learn=20:action:panic'], 2.2, NOTES, 'learn.wav')
c.check(rms(x, fs, 0.3, 0.5) > -40, 'antes del CC suena (%.1f dB)' % rms(x, fs, 0.3, 0.5))
c.check(rms(x, fs, 0.6, 0.9) < -120, 'CC 20 = 127 -> PANIC: silencio (%.1f dB)' % rms(x, fs, 0.6, 0.9))
c.check(rms(x, fs, 1.35, 1.5) > -40, 'CC 127 -> 100 sin flanco: no repite (%.1f dB)' % rms(x, fs, 1.35, 1.5))
c.check(rms(x, fs, 1.8, 2.0) < -120, 'CC 0 -> 127: PANIC otra vez (%.1f dB)' % rms(x, fs, 1.8, 2.0))
y, _ = render(BASE + CCS, 2.2, NOTES, 'learn.wav')
c.check(rms(y, fs, 0.6, 0.9) > -40, 'sin asignar, el CC no hace nada (%.1f dB)' % rms(y, fs, 0.6, 0.9))
sys.exit(c.summary())
