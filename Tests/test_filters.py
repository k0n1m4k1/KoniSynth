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

# Ladder (SynthData.h) frente al modelo analógico 1/((1+s)^4 + k) discretizado por la bilineal a la tasa interna N·fs (N = 1
# Classic, 2 Balanced, 4 High Quality): s = j·tan(πf/N·fs)/tan(πfc/N·fs), k = 4,4·Reso y la compensación de entrada 1 + k/2
# (Synth.h:304). LP 12 = (1+s)²/D, BP 12 = 2s(1+s)²/D, HP 12 = s²(1+s)²/D. Balanced añade la ADAA de entrada: cos(πf/N·fs)
def ladder_theory(mode, f, fc, reso, n=1, adaa=False):
    rate = n * fs
    s = 1j * np.tan(np.pi * f / rate) / np.tan(np.pi * fc / rate); k = 4.4 * reso
    num = {'LP 24': 1, 'LP 12': (1 + s) ** 2, 'BP 12': 2 * s * (1 + s) ** 2, 'HP 12': s ** 2 * (1 + s) ** 2}[mode]
    return np.abs(num / ((1 + s) ** 4 + k)) * (1 + 0.5 * k) * (np.cos(np.pi * f / rate) if adaa else 1)


def peak_freq(x):
    seg = x[int(1.5 * fs):int(2.5 * fs)]; N = 1 << 20
    X = np.abs(np.fft.rfft(seg * np.blackman(len(seg)), N)); i = int(np.argmax(X[1:])) + 1
    a, b, cc = np.log(X[i - 1:i + 2]); return (i + 0.5 * (a - cc) / (a - 2 * b + cc)) * fs / N, np.sqrt(np.mean(seg ** 2))


def welch_c(x, y, n):
    w = np.hanning(n); sxy = 0; sxx = 0
    for i in range(int(0.5 * fs), len(x) - n, n // 2):
        X = np.fft.rfft(x[i:i + n] * w); Y = np.fft.rfft(y[i:i + n] * w); sxy = sxy + Y * np.conj(X); sxx = sxx + np.abs(X) ** 2
    return np.fft.rfftfreq(n, 1 / fs), sxy / sxx


def alias(x, f0):
    seg = x[int(0.5 * fs):int(1.5 * fs)]; t = 2 * np.pi * np.arange(len(seg)) / (len(seg) - 1)
    win = 0.35875 - 0.48829 * np.cos(t) + 0.14128 * np.cos(2 * t) - 0.01168 * np.cos(3 * t)  # Blackman-Harris
    P = np.abs(np.fft.rfft(seg * win)) ** 2; f = np.fft.rfftfreq(len(seg), 1 / fs)
    harm = np.zeros_like(f, bool)
    for kk in range(1, int(20000 / f0) + 2): harm |= np.abs(f - kk * f0) < 6
    band = (f > 20) & (f < 20000)
    return 10 * np.log10(P[band & ~harm].sum() / P[band & harm].sum())


NB1 = [a for a in BASE if not a.startswith('Noise')] + ['Noise Level=1%']
OSC = [a for a in NB1 if not a.startswith('Osc1')] + ['Osc1 Level=0%', 'Filter Mode=LP 24']
SAT = ['Drift=0%', 'Sustain=100%', 'Attack=1 ms', 'Filter Env=0%', 'Filter Key=0%', 'Vel To Filter=0%']
STAB = [('Saw, Drive y Reso 100 %, corte máx.', ['Wave=Saw', 'Filter Mode=LP 24', 'Cutoff=20000 Hz', 'Resonance=100%', 'Filter Drive=100%'],
         ['36:0.05:1.9:127', '48:0.05:1.9:127', '60:0.05:1.9:127', '96:0.05:1.9:127']),
        ('barrido de 5 oct en 0,2 ms, Reso 100 %', ['Wave=Saw', 'Filter Mode=LP 24', 'Cutoff=20 Hz', 'Resonance=100%', 'Filter Env=100%',
                                                   'Filter Attack=0.2 ms', 'Filter Decay=50 ms'], ['%d:%.2f:0.08:127' % (40 + 3 * i, 0.1 * i) for i in range(18)]),
        ('HP 12, Drive y Reso 100 %', ['Wave=Square', 'Filter Mode=HP 12', 'Cutoff=20000 Hz', 'Resonance=100%', 'Filter Drive=100%'], ['48:0.05:1.9:127']),
        ('BP 12 a 20 Hz, Drive y Reso 100 %', ['Wave=Saw', 'Filter Mode=BP 12', 'Cutoff=20 Hz', 'Resonance=100%', 'Filter Drive=100%'], ['24:0.05:1.9:127'])]
# Calidad del ladder (propiedad filterQuality del estado): nombre, N, ADAA, argumentos y aliasing máximo (dB, global no armónico /
# armónico) de un seno con Drive 100 %, Reso 80 %, corte 20 kHz en E7 y A7. Medido: Balanced -43,3 / -51,0; High Quality -87,6 / -62,2
QUALITY = [('Classic', 1, False, [], None),
           ('Balanced', 2, True, ['--prop=filterQuality=1'], (-40.0, -48.0)),
           ('High Quality', 4, False, ['--prop=filterQuality=2'], (-84.0, -58.0))]
for qname, n, adaa, q, alias_max in QUALITY:
    for mode in ('LP 24', 'LP 12', 'BP 12', 'HP 12'):
        for reso in (0.0, 0.5):
            f, h = H(q + ['Filter Mode=' + mode, 'Cutoff=1000 Hz', 'Resonance=%d%%' % (reso * 100)])
            th = ladder_theory(mode, f, 1000, reso, n, adaa); sel = (f > 100) & (f < 8000) & (db(th) > -60)
            err = np.abs(db(h[sel]) - db(th[sel])).max()
            c.check(err < 0.2, '%s: Ladder %s Reso %d %% frente a la teoría (100 Hz-8 kHz): error máximo %.2f dB (< 0,2)' % (qname, mode, reso * 100, err))
    # Reso 90 % (k = 3,96, pico de +40 dB): con ruido al 1 % el pico casi lineal; al 5 % la tanh del lazo lo comprime ~5 dB
    r90, _ = render(NB1 + q + ['Filter On=Off'], 12.0, ['69:0.05:11.9:127'], 'flt_ref90.wav')
    y90, _ = render(NB1 + q + ['Filter On=On', 'Filter Mode=LP 24', 'Cutoff=1000 Hz', 'Resonance=90%'], 12.0, ['69:0.05:11.9:127'], 'flt90.wav')
    f, hc = welch_c(r90, y90, 32768); h = np.abs(hc); th = ladder_theory('LP 24', f, 1000, 0.9, n, adaa)
    pk = db(h.max()) - db(th.max()); sel = (f > 100) & (f < 8000) & (np.abs(f - 1000) > 50)
    off = np.abs(db(h[sel]) - db(th[sel])).max()
    c.check(abs(pk) < 1.5 and off < 0.5, '%s: Ladder LP 24 Reso 90 %%: pico %+.2f dB frente a la teoría (±1,5), fuera de ±50 Hz %.2f dB (< 0,5)' % (qname, pk, off))
    # Autooscilación (Osc 1 apagado, ruido 1 % de excitación): afinación del pico frente al corte
    grid = ((91, 4.0, (100, 1000, 5000, 15000)), (92, 1.0, (100, 1000, 5000, 15000)), (100, 1.0, (100, 1000, 5000, 15000))) if n == 1 \
        else ((92, 1.0, (1000, 15000)), (100, 1.0, (1000, 15000)))
    for reso, tol, cuts in grid:
        for cut in cuts:
            x, _ = render(OSC + q + ['Cutoff=%d Hz' % cut, 'Resonance=%d%%' % reso], 3.0, ['69:0.05:2.9:127'], 'flt_osc.wav')
            fp, _ = peak_freq(x); ct = 1200 * np.log2(fp / cut)
            c.check(abs(ct) < tol, '%s: Ladder autooscila, Reso %d %%, corte %d Hz: %+.2f ct (±%.0f)' % (qname, reso, cut, ct, tol))
    # Umbral: con Reso 90 % (k = 3,96 < 4) no oscila, solo resuena el ruido
    rms = [peak_freq(render(OSC + q + ['Cutoff=1000 Hz', 'Resonance=%d%%' % r], 3.0, ['69:0.05:2.9:127'], 'flt_osc.wav')[0])[1] for r in (90, 92)]
    c.check(rms[0] < 0.25 * rms[1], '%s: Ladder umbral de autooscilación: RMS Reso 90 %% / 92 %% = %.2f (< 0,25)' % (qname, rms[0] / rms[1]))
    # Estabilidad con valores extremos: salida finita, acotada y sin continua
    for name, args, notes in STAB:
        x, _ = render(SAT + q + args, 2.0, notes, 'flt_stab.wav')
        ok = bool(np.all(np.isfinite(x))) and np.abs(x).max() < 0.5 and abs(x.mean()) < 0.02
        c.check(ok, '%s: Ladder estable, %s: pico %.3f (< 0,5), continua %+.4f (< 0,02)' % (qname, name, np.abs(x).max(), x.mean()))
    if alias_max is not None:
        for note, lim in zip((100, 105), alias_max):
            f0 = 440 * 2 ** ((note - 69) / 12)
            x, _ = render(SAT + q + ['Wave=Sine', 'Filter Mode=LP 24', 'Cutoff=20000 Hz', 'Resonance=80%', 'Filter Drive=100%'], 2.0,
                          ['%d:0.05:1.9:127' % note], 'flt_alias.wav')
            a = alias(x, f0)
            c.check(a < lim, '%s: aliasing de un seno de %.0f Hz, Drive 100 %%, Reso 80 %%: %.1f dB (< %.0f; Classic: -16,0 / -17,9)' % (qname, f0, a, lim))

# Balanced, Clean Drive: la rama lineal va alineada con la ADAA (media de dos muestras a 2·fs). La razón compleja
# H(Drive 10 %) / H(Drive 0 %) cancela ladder y sobremuestreo: debe ser plana, 0,6 + 0,4·sqrt(10^(2,6/20)) = 1,0646, fase 0.
# Sin alinear las ramas: -0,81 dB y -12° a 16 kHz (medido)
CLEAN = NB1 + ['--prop=filterQuality=1', 'Filter Mode=LP 24', 'Cutoff=20000 Hz', 'Resonance=0%', 'Filter Clean Drive=On']
cref, _ = render(CLEAN + ['Filter On=Off'], 4.0, ['69:0.05:3.9:127'], 'flt_cref.wav')
f, h0 = welch_c(cref, render(CLEAN + ['Filter Drive=0%'], 4.0, ['69:0.05:3.9:127'], 'flt_c0.wav')[0], 8192)
_, h1 = welch_c(cref, render(CLEAN + ['Filter Drive=10%'], 4.0, ['69:0.05:3.9:127'], 'flt_c1.wav')[0], 8192)
r = h1 / h0; sel = (f > 100) & (f < 16000)
mag = db(np.abs(r[sel]) / 1.0646); ph = np.degrees(np.angle(r[sel]))
c.check(np.abs(mag).max() < 0.05 and np.abs(ph).max() < 0.5,
        'Balanced, Clean Drive 10 %%: mezcla lineal + ADAA %.3f dB y %.2f° como máximo (100 Hz-16 kHz; < 0,05 dB y < 0,5°)' % (np.abs(mag).max(), np.abs(ph).max()))
sys.exit(c.summary())
