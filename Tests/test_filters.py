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

# Ladder (SynthData.h) frente al modelo analógico 1/((1+s)^4 + k) con prewarp en el corte: s = j·tan(πf/fs)/tan(πfc/fs),
# k = 4,4·Reso y la compensación de entrada 1 + k/2 (Synth.h:304). LP 12 = (1+s)²/D, BP 12 = 2s(1+s)²/D, HP 12 = s²(1+s)²/D
def ladder_theory(mode, f, fc, reso):
    s = 1j * np.tan(np.pi * f / fs) / np.tan(np.pi * fc / fs); k = 4.4 * reso
    num = {'LP 24': 1, 'LP 12': (1 + s) ** 2, 'BP 12': 2 * s * (1 + s) ** 2, 'HP 12': s ** 2 * (1 + s) ** 2}[mode]
    return np.abs(num / ((1 + s) ** 4 + k)) * (1 + 0.5 * k)


for mode in ('LP 24', 'LP 12', 'BP 12', 'HP 12'):
    for reso in (0.0, 0.5):
        f, h = H(['Filter Mode=' + mode, 'Cutoff=1000 Hz', 'Resonance=%d%%' % (reso * 100)])
        th = ladder_theory(mode, f, 1000, reso); sel = (f > 100) & (f < 8000) & (db(th) > -60)
        err = np.abs(db(h[sel]) - db(th[sel])).max()
        c.check(err < 0.2, 'Ladder %s Reso %d %% frente a la teoría (100 Hz-8 kHz): error máximo %.2f dB (< 0,2)' % (mode, reso * 100, err))
# Reso 90 % (k = 3,96, pico de +40 dB): con ruido al 1 % el pico casi lineal; al 5 % la tanh del lazo lo comprime ~5 dB
NB1 = [a for a in BASE if not a.startswith('Noise')] + ['Noise Level=1%']
r90, _ = render(NB1 + ['Filter On=Off'], 12.0, ['69:0.05:11.9:127'], 'flt_ref90.wav')
y90, _ = render(NB1 + ['Filter On=On', 'Filter Mode=LP 24', 'Cutoff=1000 Hz', 'Resonance=90%'], 12.0, ['69:0.05:11.9:127'], 'flt90.wav')
n = 32768; w = np.hanning(n); sxy = 0; sxx = 0
for i in range(int(0.5 * fs), len(r90) - n, n // 2):
    X = np.fft.rfft(r90[i:i + n] * w); Y = np.fft.rfft(y90[i:i + n] * w); sxy = sxy + Y * np.conj(X); sxx = sxx + np.abs(X) ** 2
f = np.fft.rfftfreq(n, 1 / fs); h = np.abs(sxy / sxx); th = ladder_theory('LP 24', f, 1000, 0.9)
pk = db(h.max()) - db(th.max()); sel = (f > 100) & (f < 8000) & (np.abs(f - 1000) > 50)
off = np.abs(db(h[sel]) - db(th[sel])).max()
c.check(abs(pk) < 1.5 and off < 0.5, 'Ladder LP 24 Reso 90 %%: pico %+.2f dB frente a la teoría (±1,5), fuera de ±50 Hz %.2f dB (< 0,5)' % (pk, off))


# Autooscilación (Osc 1 apagado, ruido 1 % de excitación): afinación del pico frente al corte
def peak_freq(x):
    seg = x[int(1.5 * fs):int(2.5 * fs)]; N = 1 << 20
    X = np.abs(np.fft.rfft(seg * np.blackman(len(seg)), N)); i = int(np.argmax(X[1:])) + 1
    a, b, cc = np.log(X[i - 1:i + 2]); return (i + 0.5 * (a - cc) / (a - 2 * b + cc)) * fs / N, np.sqrt(np.mean(seg ** 2))


OSC = [a for a in NB1 if not a.startswith('Osc1')] + ['Osc1 Level=0%', 'Filter Mode=LP 24']
for reso, tol in ((91, 4.0), (92, 1.0), (100, 1.0)):
    for cut in (100, 1000, 5000, 15000):
        x, _ = render(OSC + ['Cutoff=%d Hz' % cut, 'Resonance=%d%%' % reso], 3.0, ['69:0.05:2.9:127'], 'flt_osc.wav')
        fp, _ = peak_freq(x); ct = 1200 * np.log2(fp / cut)
        c.check(abs(ct) < tol, 'Ladder autooscila, Reso %d %%, corte %d Hz: %+.2f ct (±%.0f)' % (reso, cut, ct, tol))
# Umbral: con Reso 90 % (k = 3,96 < 4) no oscila, solo resuena el ruido
rms = [peak_freq(render(OSC + ['Cutoff=1000 Hz', 'Resonance=%d%%' % r], 3.0, ['69:0.05:2.9:127'], 'flt_osc.wav')[0])[1] for r in (90, 92)]
c.check(rms[0] < 0.25 * rms[1], 'Ladder umbral de autooscilación: RMS Reso 90 %% / 92 %% = %.2f (< 0,25)' % (rms[0] / rms[1]))

# Estabilidad con valores extremos: salida finita, acotada y sin continua
STAB = [('Saw, Drive y Reso 100 %, corte máx.', ['Wave=Saw', 'Filter Mode=LP 24', 'Cutoff=20000 Hz', 'Resonance=100%', 'Filter Drive=100%'],
         ['36:0.05:1.9:127', '48:0.05:1.9:127', '60:0.05:1.9:127', '96:0.05:1.9:127']),
        ('barrido de 5 oct en 0,2 ms, Reso 100 %', ['Wave=Saw', 'Filter Mode=LP 24', 'Cutoff=20 Hz', 'Resonance=100%', 'Filter Env=100%',
                                                   'Filter Attack=0.2 ms', 'Filter Decay=50 ms'], ['%d:%.2f:0.08:127' % (40 + 3 * i, 0.1 * i) for i in range(18)]),
        ('HP 12, Drive y Reso 100 %', ['Wave=Square', 'Filter Mode=HP 12', 'Cutoff=20000 Hz', 'Resonance=100%', 'Filter Drive=100%'], ['48:0.05:1.9:127']),
        ('BP 12 a 20 Hz, Drive y Reso 100 %', ['Wave=Saw', 'Filter Mode=BP 12', 'Cutoff=20 Hz', 'Resonance=100%', 'Filter Drive=100%'], ['24:0.05:1.9:127'])]
for name, args, notes in STAB:
    x, _ = render(['Drift=0%', 'Sustain=100%', 'Attack=1 ms', 'Filter Env=0%', 'Filter Key=0%', 'Vel To Filter=0%'] + args, 2.0, notes, 'flt_stab.wav')
    ok = bool(np.all(np.isfinite(x))) and np.abs(x).max() < 0.5 and abs(x.mean()) < 0.02
    c.check(ok, 'Ladder estable, %s: pico %.3f (< 0,5), continua %+.4f (< 0,02)' % (name, np.abs(x).max(), x.mean()))
sys.exit(c.summary())
