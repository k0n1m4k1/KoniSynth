# Filtros del Synth (Dsp/Filters.h): respuesta en frecuencia con el mismo ruido filtrado y sin filtrar, H = Sxy/Sxx
import sys
import numpy as np
from common import render, db, Checker

BASE = ['Osc1 Level=0%', 'Noise Level=5%', 'Drift=0%', 'Sustain=100%', 'Attack=1 ms', 'Filter Env=0%', 'Filter Key=0%', 'Vel To Filter=0%']
NOTE = ['69:0.05:3.9:127']


def welch_h(x, y, fs, n=8192):
    w = np.hanning(n); sxy = 0; sxx = 0
    for i in range(int(0.5 * fs), int(3.5 * fs) - n, n // 2):
        X = np.fft.rfft(x[i:i + n] * w); Y = np.fft.rfft(y[i:i + n] * w)
        sxy = sxy + Y * np.conj(X); sxx = sxx + np.abs(X) ** 2
    return np.fft.rfftfreq(n, 1 / fs), np.abs(sxy / sxx)


ref, fs = render(BASE + ['Filter On=Off'], 4.0, NOTE, 'flt_ref.wav')


def H(args):
    y, _ = render(BASE + ['Filter On=On'] + args, 4.0, NOTE, 'flt.wav')
    return welch_h(ref, y, fs)


def at(f, h, fq):
    return float(db(np.interp(fq, f, h)))


def peaks(f, h, rel=0.5, fmax=1e9):
    return [f[i] for i in range(1, len(h) - 1) if h[i] > h[i - 1] and h[i] >= h[i + 1] and h[i] > rel * h.max() and f[i] < fmax]


c = Checker('filtros')
R = 1.0 - 0.994 * 0.5  # Svf::damping (0,5)
theory = {'SVF LP': lambda w: 1 / np.sqrt((1 - w * w) ** 2 + (2 * R * w) ** 2),
          'SVF HP': lambda w: w * w / np.sqrt((1 - w * w) ** 2 + (2 * R * w) ** 2),
          'SVF BP': lambda w: 2 * R * w / np.sqrt((1 - w * w) ** 2 + (2 * R * w) ** 2)}
for mode, fn in theory.items():
    f, h = H(['Filter Mode=' + mode, 'Cutoff=1000 Hz', 'Resonance=50%'])
    err = max(abs(at(f, h, fq) - float(db(fn(fq / 1000)))) for fq in (250, 500, 1000, 2000, 4000))
    c.check(err < 0.5, '%s frente a la teoría (250 Hz-4 kHz): error máximo %.2f dB (< 0,5)' % (mode, err))
f, h = H(['Filter Mode=SVF Notch', 'Cutoff=1000 Hz', 'Resonance=50%'])
c.check(at(f, h, 1000) < -30, 'SVF Notch a 1 kHz: %.1f dB (< -30)' % at(f, h, 1000))
f, h = H(['Filter Mode=MS-20 LP', 'Cutoff=1000 Hz', 'Resonance=0%'])
s = at(f, h, 8000) - at(f, h, 4000)
c.check(-15 < s < -11, 'MS-20 LP: pendiente 4-8 kHz %.1f dB/oct (12 como el original)' % s)
f, h = H(['Filter Mode=MS-20 LP', 'Cutoff=1000 Hz', 'Resonance=90%'])
sel = (f > 200) & (f < 5000); k = int(np.argmax(h[sel]))
c.check(db(h[sel][k]) > 10 and abs(f[sel][k] - 1000) < 60, 'MS-20 LP Reso 90 %%: pico %.1f dB en %.0f Hz' % (db(h[sel][k]), f[sel][k]))
f, h = H(['Filter Mode=MS-20 HP', 'Cutoff=1000 Hz', 'Resonance=0%'])
s = at(f, h, 125) - at(f, h, 250)
c.check(-8 < s < -4.5, 'MS-20 HP: pendiente 125-250 Hz %.1f dB/oct (6 como el original)' % s)
f, h = H(['Filter Mode=Comb +', 'Cutoff=500 Hz', 'Resonance=90%'])
p = peaks(f, h, fmax=3100)
c.check(len(p) == 6 and all(abs(pk - 500 * (i + 1)) < 12 for i, pk in enumerate(p)), 'Comb + 500 Hz: picos %s (múltiplos de 500)' % ' '.join('%.0f' % x for x in p))
f, h = H(['Filter Mode=Comb -', 'Cutoff=500 Hz', 'Resonance=90%'])
p = peaks(f, h, fmax=3100)
c.check(len(p) == 3 and all(abs(pk - 500 * (2 * i + 1)) < 12 for i, pk in enumerate(p)), 'Comb - 500 Hz: picos %s (impares)' % ' '.join('%.0f' % x for x in p))
f, h = H(['Filter Mode=Dual LP', 'Cutoff=1000 Hz', 'Resonance=0%', 'Filter Spread=1.00 oct'])
s = at(f, h, 8000) - at(f, h, 4000)
c.check(-28 < s < -22, 'Dual LP: pendiente 4-8 kHz %.1f dB/oct (24)' % s)
# Clean Drive: con Drive 0 % la entrada del filtro es lineal (SVF sin armónicos); apagado, la etapa tanh de siempre
for clean, mode, lim in ((1, 'SVF LP', -120), (0, 'SVF LP', None)):
    x, fs = render(['Wave=Sine', 'Sustain=100%', 'Drift=0%', 'Cutoff=20000 Hz', 'Filter Mode=' + mode, 'Filter Clean Drive=' + ('On' if clean else 'Off')],
                   1.0, ['60:0.05:0.9:127'], 'clean.wav')
    seg = x[int(0.3 * fs):int(0.8 * fs)]; n = np.arange(len(seg)); w = seg * np.hanning(len(seg)); f0 = 440 * 2 ** (-9 / 12)
    h = [abs(np.sum(w * np.exp(-2j * np.pi * k * f0 * n / fs))) for k in (1, 3)]
    h3 = 20 * np.log10(h[1] / h[0])
    c.check(h3 < -120 if lim else -37 < h3 < -31, 'Clean Drive %s, %s, Drive 0 %%: h3 %.1f dB (%s)' % ('On' if clean else 'Off', mode, h3, '< -120' if lim else 'etapa tanh: -34'))
sys.exit(c.summary())
