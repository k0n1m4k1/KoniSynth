# Voz del Synth: saturación por voz (intermodulación, aliasing, nivel), VCA analógico (Click, Leak) y microafinación Scala
import sys
import numpy as np
from common import render, db, Checker

c = Checker('voz')
TONE = ['Wave=Sine', 'Filter On=Off', 'Attack=1 ms', 'Sustain=100%', 'Release=5 ms', 'Drift=0%']


def spectrum(x, fs, t0=0.3, t1=1.3):
    seg = x[int(t0 * fs): int(t1 * fs)]
    S = np.abs(np.fft.rfft(seg * np.kaiser(len(seg), 20))) ** 2
    return np.fft.rfftfreq(len(seg), 1 / fs), S


def band(f, S, fc, tol=4.0):
    return S[(f > fc - tol) & (f < fc + tol)].sum()


def alias_ratio(x, fs, f0):
    f, S = spectrum(x, fs)
    sel = (f > 20) & (f < 20000); on = np.abs(f - np.round(f / f0) * f0) < 12
    return 10 * np.log10(S[sel & ~on].sum() / S[sel].sum())


# --- Saturación por voz ---
f1, f2 = 440 * 2 ** (-9 / 12), 440 * 2 ** (-5 / 12)
x, fs = render(TONE + ['Voice Sat Mode=Soft', 'Voice Sat Drive=24 dB'], 1.5, ['60:0.1:1.4:127', '64:0.1:1.4:127'], 'sat.wav')
f, S = spectrum(x, fs)
imd = max(10 * np.log10(band(f, S, v) / band(f, S, f1)) for v in (f2 - f1, 2 * f1 - f2, 2 * f2 - f1, f1 + f2))
c.check(imd < -120, 'saturación por voz, acorde C4+E4 a 24 dB: intermodulación máxima %.1f dB (< -120)' % imd)
for mode, note, d, lim in (('Hard', 60, 24, -75), ('Hard', 84, 24, -55), ('Soft', 84, 24, -80)):
    x, fs = render(TONE + ['Voice Sat Mode=' + mode, 'Voice Sat Drive=%d dB' % d], 1.5, ['%d:0.1:1.4:127' % note], 'sat.wav')
    a = alias_ratio(x, fs, 440 * 2 ** ((note - 69) / 12))
    c.check(a < lim, 'aliasing %s %d dB nota %d: %.1f dB (< %d)' % (mode, d, note, a, lim))
for mode in ('Soft', 'Diode', 'Hard'):
    x, fs = render(TONE + ['Voice Sat Mode=' + mode, 'Voice Sat Drive=12 dB'], 1.0, ['69:0.1:0.8:127'], 'sat.wav')
    seg = x[int(0.3 * fs):int(0.7 * fs)]; pp = (seg.max() - seg.min()) / 0.2
    c.check(abs(db(pp)) < 1.0, 'nivel %s 12 dB: pico a pico %.2f dB respecto a sin saturar (< 1)' % (mode, db(pp)))

# --- VCA analógico ---
SILENT = ['Osc1 Level=0%', 'Osc2 Level=0%', 'Sub Level=0%', 'Noise Level=0%', 'Filter On=Off', 'Attack=1 ms', 'Sustain=100%', 'Release=1 ms', 'Drift=0%']
x, fs = render(SILENT + ['VCA Click=100%'], 1.0, ['60:0.1:0.5:127'], 'vca.wav')
k = int(0.1 * fs) + int(np.argmax(np.abs(x[int(0.1 * fs):int(0.15 * fs)])))
seg = np.abs(x[k:k + int(0.2 * fs)]) / abs(x[k])
tau = (np.argmax(seg < 0.9 / np.e) - np.argmax(seg < 0.9)) / fs * 1000
c.check(-47 < db(x[k]) < -39 and abs(tau - 15.9) < 0.5, 'VCA Click 100 %%: pico %.1f dBFS, tau %.1f ms (15,9: acoplamiento de 10 Hz)' % (db(x[k]), tau))
c.check(np.max(np.abs(np.diff(x[int(0.6 * fs):]))) < 2e-4, 'VCA Click: sin saltos al acabar la cola (máx %.1e por muestra)' % np.max(np.abs(np.diff(x[int(0.6 * fs):]))))
for leak in (-40, -60):
    x, fs = render(TONE[:-2] + ['Release=10 ms', 'Drift=0%', 'VCA Leak=%d dB' % leak], 1.6, ['69:0.1:0.3:127'], 'vca.wav')
    r = db(np.sqrt(np.mean(x[int(1.0 * fs):int(1.5 * fs)] ** 2))) - db(np.sqrt(np.mean(x[int(0.2 * fs):int(0.35 * fs)] ** 2)))
    c.check(abs(r - leak) < 0.1, 'VCA Leak %d dB: %.2f dB respecto al sostenido' % (leak, r))

# --- Microafinación (Scala en el estado) ---
mean = ('1/4-comma meantone\n12\n76.04900\n193.15686\n310.26303\n5/4\n503.42157\n579.47057\n696.57843\n25/16\n889.73529\n1006.84314\n'
        '1082.89214\n2/1\n')
dia = 'Ptolemy diatonic\n7\n9/8\n5/4\n4/3\n3/2\n5/3\n15/8\n2/1\n'
kbm = '12\n0\n127\n60\n69\n432.0\n7\n0\nx\n1\nx\n2\n3\nx\n4\nx\n5\nx\n6\n'
c4 = 440 * 2 ** (-9 / 12)


def pitch(x, fs):
    seg = x[int(0.4 * fs): int(1.2 * fs)]
    if np.sqrt(np.mean(seg ** 2)) < 1e-4:
        return 0.0
    n = len(seg) * 8; S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)); k = int(np.argmax(S[20:])) + 20
    a, b, cc = np.log(S[k - 1:k + 2] + 1e-30)
    return (k + 0.5 * (a - cc) / (a - 2 * b + cc)) * fs / n


SYN = ['Wave=Sine', 'Drift=0%', 'Filter On=Off', 'Sustain=100%']
for name, note, props, want in (('mesotónico E4 = 5/4 de C4', 64, {'tuningScl': mean}, c4 * 1.25), ('.kbm A4 = 432 Hz', 69, {'tuningScl': dia, 'tuningKbm': kbm}, 432.0),
                                ('.kbm C#4 sin mapear (x)', 61, {'tuningScl': dia, 'tuningKbm': kbm}, 0.0)):
    x, fs = render(SYN + ['--prop=%s=%s' % kv for kv in props.items()], 1.5, ['%d:0.05:1.3:100' % note], 'tun.wav')
    f0 = pitch(x, fs)
    ok = (f0 == 0.0) if want == 0 else abs(1200 * np.log2(f0 / want)) < 0.05
    c.check(ok, 'afinación %s: %.3f Hz (esperado %.3f)' % (name, f0, want))
# --- Cuerda física: pastilla (nulo en h4 a 25 %) y Damping (no cambia afinación ni Decay de la fundamental) ---
STR = ['Osc1 Level=0%', 'String Level=100%', 'Filter On=Off', 'Attack=1 ms', 'Decay=4000 ms', 'Sustain=0%', 'Drift=0%', 'String Excite=Pick',
       'String Pick Position=10%', 'String Decay=3.0 s']


def partial(x, fs, f, t0, t1):
    n = np.arange(int(t0 * fs), int(t1 * fs)); seg = x[n] * np.hanning(len(n))
    return abs(np.sum(seg * np.exp(-2j * np.pi * f * n / fs))) / len(n)


f0 = 440 * 2 ** ((40 - 69) / 12)
x, fs = render(STR + ['String Pickup=25%'], 2.0, ['40:0.0:1.9:100'], 'str.wav')
h4 = 20 * np.log10(partial(x, fs, 4 * f0, 0.1, 0.6) / partial(x, fs, f0, 0.1, 0.6))
c.check(h4 < -25, 'cuerda, pastilla 25 %%: h4 a %.1f dB de h1 (nulo de 2·sin(4π/4); < -25)' % h4)
dec = {}
for d in (0, 80):
    x, fs = render(STR + ['String Damping=%d%%' % d], 2.0, ['40:0.0:1.9:100'], 'str.wav')
    a1, a2 = partial(x, fs, f0, 0.2, 0.5), partial(x, fs, f0, 1.2, 1.5)
    dec[d] = 20 * np.log10(a2 / a1)  # caída de la fundamental en 1 s
    dec[str(d)] = pitch(x, fs)
c.check(abs(dec[80] - dec[0]) < 0.5, 'cuerda, Damping 80 %%: caída de la fundamental en 1 s %.2f dB frente a %.2f sin Damping (< 0,5 dB)' % (dec[80], dec[0]))
c.check(abs(1200 * np.log2(dec['80'] / dec['0'])) < 1.0, 'cuerda, Damping 80 %%: afinación %.3f Hz frente a %.3f Hz (< 1 cent)' % (dec['80'], dec['0']))
sys.exit(c.summary())
