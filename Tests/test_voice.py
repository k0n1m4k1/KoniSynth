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

# --- Osc 2 en modo Wavetable: misma tabla que la fuente Wavetable, fase y posición propias ---
WT = ['Filter On=Off', 'Attack=1 ms', 'Sustain=100%', 'Release=5 ms', 'Drift=0%', 'Osc1 Level=0%', 'Wavetable=Harmonic Sweep']
OSC2 = WT + ['Osc2 Wave=Wavetable', 'Osc2 Level=100%']
fa = 440 * 2 ** ((57 - 69) / 12)


def harmonics(x, fs, f0, n=30):
    seg = x[int(0.3 * fs):int(1.3 * fs)]; k = np.arange(len(seg)); w = np.kaiser(len(seg), 20)
    return np.array([abs(np.sum(seg * w * np.exp(-2j * np.pi * h * f0 * k / fs))) for h in range(1, n + 1)])


def same_harmonics(a, b):
    m = db(a) > db(a.max()) - 60
    return np.abs(db(b[m]) - db(a[m])).max()


src, fs = render(WT + ['Wavetable Level=100%', 'Wavetable Position=50%'], 1.5, ['57:0.05:1.4:127'], 'osc2wt.wav')
o2, _ = render(OSC2 + ['Osc2 WT Position=50%'], 1.5, ['57:0.05:1.4:127'], 'osc2wt.wav')
d = same_harmonics(harmonics(src, fs, fa), harmonics(o2, fs, fa))
c.check(d < 0.01, 'Osc 2 Wavetable = fuente Wavetable a la misma posición (50 %%): armónicos a %.3f dB como máximo (< 0,01)' % d)
h0 = harmonics(render(OSC2 + ['Osc2 WT Position=0%'], 1.5, ['57:0.05:1.4:127'], 'osc2wt.wav')[0], fs, fa)
h1 = harmonics(render(OSC2 + ['Osc2 WT Position=100%'], 1.5, ['57:0.05:1.4:127'], 'osc2wt.wav')[0], fs, fa)
c.check(db(h0[1] / h0[0]) < -100 and db(h1[1] / h1[0]) > -10,
        'Osc 2 WT Pos mueve la tabla: h2 %.0f dB a 0 %% (seno) y %.1f dB a 100 %%' % (db(h0[1] / h0[0]), db(h1[1] / h1[0])))
x, _ = render(OSC2 + ['Osc2 Octave=1', 'Osc2 Semi=7', 'Osc2 Fine=10.0 ct'], 1.5, ['69:0.05:1.4:127'], 'osc2wt.wav')
want = 880 * 2 ** (7.1 / 12); ct = 1200 * np.log2(pitch(x, fs) / want)
c.check(abs(ct) < 0.05, 'Osc 2 Wavetable sigue Oct +1, Semi +7, Fine +10 ct: %.3f Hz (esperado %.3f, %+.3f ct)' % (pitch(x, fs), want, ct))
hm = harmonics(render(OSC2 + ['Osc2 WT Position=0%', 'Matrix 1 Source=Velocity', 'Matrix 1 Dest=Osc 2 WT Position', 'Matrix 1 Amount=100%'],
                      1.5, ['57:0.05:1.4:127'], 'osc2wt.wav')[0], fs, fa)
d = same_harmonics(h1, hm)
c.check(d < 0.01, 'matriz Velocity -> Osc 2 WT Position 100 %%, velocidad 127 = posición 100 %%: %.3f dB (< 0,01)' % d)
# Sync: el salto del reinicio va corregido con minBLEP (con la tabla en un seno, igual que el Sine clásico); el salto de
# pendiente queda sin corregir, como en el Sine y el Triangle clásicos (MinBlep.h). Afecta sobre todo a posiciones brillantes:
# referencia para un futuro BLAMP (Harmonic Sweep, posición 30 %, Oct +1, Semi +5): C3 -75,4, C4 -52,9, C5 -43,6 dB
# (sierra clásica con sync: -118,3 / -111,8 / -108,5 dB)
SYNC = ['Osc2 Level=100%', 'Osc2 Sync=On', 'Osc2 Octave=1', 'Osc2 Semi=5']
fc4 = 440 * 2 ** ((60 - 69) / 12)
a_sine = alias_ratio(render(WT + SYNC + ['Osc2 Wave=Sine'], 1.5, ['60:0.05:1.4:127'], 'osc2wt.wav')[0], fs, fc4)
a_wt0 = alias_ratio(render(WT + SYNC + ['Osc2 Wave=Wavetable', 'Osc2 WT Position=0%'], 1.5, ['60:0.05:1.4:127'], 'osc2wt.wav')[0], fs, fc4)
c.check(abs(a_wt0 - a_sine) < 0.5, 'Osc 2 Wavetable con sync, tabla en seno: aliasing %.1f dB = Sine clásico %.1f dB (±0,5)' % (a_wt0, a_sine))
a_wt30 = alias_ratio(render(WT + SYNC + ['Osc2 Wave=Wavetable', 'Osc2 WT Position=30%'], 1.5, ['60:0.05:1.4:127'], 'osc2wt.wav')[0], fs, fc4)
c.check(a_wt30 < -50, 'Osc 2 Wavetable con sync, posición 30 %%, C4: aliasing %.1f dB (< -50)' % a_wt30)

# --- Osc 3 (el Sub ampliado): f3 = f1 · 2^(octava + (semi + fine/100)/12); por defecto, la cuadrada de siempre (bit a bit:
# comprobado contra el binario anterior en 5 configuraciones del Sub al integrarlo) ---
O3 = ['Filter On=Off', 'Attack=1 ms', 'Sustain=100%', 'Release=5 ms', 'Drift=0%', 'Osc1 Level=0%', 'Sub Level=100%']
SHAPE = {'Sine': [None, None, None, None], 'Triangle': [None, -19.08, None, -27.96], 'Saw': [-6.02, -9.54, -12.04, -13.98],
         'Square': [None, -9.54, None, -13.98]}  # h2..h5 respecto a h1 (dB); None = ausente (< -100)
for wave, shape in SHAPE.items():
    x, fs = render(O3 + ['Osc3 Wave=' + wave, 'Osc3 Semi=12', 'Osc3 Fine=10.0 ct'], 1.5, ['57:0.05:1.4:127'], 'osc3.wav')
    want = 220 * 2 ** (10 / 1200); ct = 1200 * np.log2(pitch(x, fs) / want)
    h = harmonics(x, fs, want, 5); h = db(h[1:] / h[0])
    ok = all((v < -100) if s is None else abs(v - s) < 0.1 for v, s in zip(h, shape))
    c.check(abs(ct) < 0.05 and ok, 'Osc 3 %s, -1 oct + 12 st + 10 ct: %+.3f ct; h2..h5 %s dB' % (wave, ct, ' '.join('%.1f' % v for v in h)))
x, _ = render(O3 + ['Sub Octave=-2 oct', 'Osc3 Wave=Sine', 'Osc3 Semi=-24'], 1.5, ['81:0.05:1.4:127'], 'osc3.wav')
ct = 1200 * np.log2(pitch(x, fs) / 55.0)
c.check(abs(ct) < 0.05, 'Osc 3 en el extremo grave (-2 oct - 24 st = -4 oct de A5): %.3f Hz (55, %+.3f ct)' % (pitch(x, fs), ct))
# Agudo (+1 oct de C7 = 4186 Hz): sierra y cuadrada con minBLEP; el triángulo, sin corregir su pendiente, igual que el de Osc 2
f8 = 440 * 2 ** (39 / 12)  # C8, una octava sobre la nota C7 tocada
for wave in ('Saw', 'Square', 'Triangle'):
    a3 = alias_ratio(render(O3 + ['Osc3 Wave=' + wave, 'Osc3 Semi=24'], 1.5, ['96:0.05:1.4:127'], 'osc3.wav')[0], fs, f8)
    if wave != 'Triangle':
        c.check(a3 < -90, 'Osc 3 %s a %.0f Hz: aliasing %.1f dB (< -90, minBLEP)' % (wave, f8, a3))
    else:
        a2 = alias_ratio(render(O3[:-1] + ['Osc2 Level=100%', 'Osc2 Wave=Triangle', 'Osc2 Octave=1'], 1.5, ['96:0.05:1.4:127'], 'osc3.wav')[0], fs, f8)
        c.check(abs(a3 - a2) < 0.5, 'Osc 3 Triangle a %.0f Hz: aliasing %.1f dB = Osc 2 Triangle %.1f dB (±0,5)' % (f8, a3, a2))

# --- Mute por fuente (bit a bit): silencia la contribución de su fuente, no el recurso compartido (la tabla) ---
import hashlib
sha = lambda x: hashlib.sha256(x.tobytes()).hexdigest()
MN = ['57:0.05:1.0:127', '64:0.3:0.8:100']
WTS = ['Wavetable Level=100%', 'Wavetable Position=60%']
O2W = ['Osc2 Wave=Wavetable', 'Osc2 Level=100%', 'Osc2 WT Position=20%']
c.check(sha(render(WT + WTS + O2W + ['Wavetable Mute=On'], 1.2, MN, 'mute.wav')[0]) == sha(render(WT + O2W, 1.2, MN, 'mute.wav')[0]),
        'Mute de Wavetable con Osc 2 en la misma tabla: suena solo Osc 2 (bit a bit)')
c.check(sha(render(WT + WTS + O2W + ['Osc2 Mute=On'], 1.2, MN, 'mute.wav')[0]) == sha(render(WT + WTS, 1.2, MN, 'mute.wav')[0]),
        'Mute de Osc 2: suena solo la fuente Wavetable (bit a bit)')
O3S = ['Sub Level=100%', 'Osc3 Wave=Saw']
c.check(sha(render(WT + O3S + ['Noise Level=50%', 'Noise Mute=On', 'Osc2 Level=60%', 'Osc2 Mute=On'], 1.2, MN, 'mute.wav')[0])
        == sha(render(WT + O3S, 1.2, MN, 'mute.wav')[0]), 'Mute de Osc 2 y Noise: igual que sin ellos (bit a bit)')
# La matriz no devuelve el nivel a una fuente en Mute (antes de los Mute de Osc 2 / Osc 3 / Noise, Osc 1 sí sonaba: RMS 0,054)
for src in ('Osc 1', 'Osc 2', 'Noise'):
    pid = src.replace(' ', '')
    x, _ = render(WT + ['%s Level=0%%' % pid, '%s Mute=On' % pid, 'Matrix 1 Source=Velocity', 'Matrix 1 Dest=%s Level' % src, 'Matrix 1 Amount=100%'],
                  1.2, MN, 'mute.wav')
    c.check(np.abs(x).max() == 0.0, 'Mute de %s con la matriz Velocity -> %s Level al 100 %%: pico %.2e (silencio)' % (src, src, np.abs(x).max()))
sys.exit(c.summary())
