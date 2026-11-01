# Motor Gravis UltraSound (Engines/Gus.h). Los patches de Gravis no se distribuyen: el test genera .PAT sintéticos (formato de
# PATCH.H del GUS SDK v2.01: cabecera de 129 bytes, instrumento de 63, capa de 47, muestra de 96 + datos) con un seno en bucle y
# su timidity.cfg, y comprueba con ellos el lector, el tono del GF1 (cálculo del paso de la biblioteca del SDK), la frecuencia
# de salida según las voces activas y el vibrato (Timer 2 a 45,29 Hz, cuartos de ciclo, 21 unidades por semitono).
# Sin carpeta de patches, silencio y sin fallo.
import os, struct, sys, tempfile
import numpy as np
from common import render, db, Checker

c = Checker('gus')

# Sin patches: silencio
empty = tempfile.mkdtemp(prefix='koni_gus_empty_')
os.environ['KONI_GUS_PATCHES'] = empty
x, fs = render([], 0.5, ['69:0.05:0.4:100'], 'gus.wav', engine='Gravis UltraSound')
c.check(np.max(np.abs(x)) == 0.0, 'sin patches: silencio (pico %.1e) y sin fallo' % np.max(np.abs(x)))

# Seno de 441 Hz grabado a 44 100 Hz (100 muestras por periodo), 16 bits con signo, bucle de 44 periodos completos, sin
# envolvente (modes: 16 bits + bucle = 0x05), raíz 441 Hz, rango 20 Hz-20 kHz
SR, ROOT = 44100, 441.0
n = 100 * 50
wave = np.round(16000 * np.sin(2 * np.pi * np.arange(n) / 100)).astype('<i2')


def make_patch(path, vib=(0, 0, 0)):
    """Escribe un .PAT de una muestra; vib = (sweep, rate, depth) del vibrato."""
    header = b'GF1PATCH110\x00' + b'ID#000002\x00' + b'synthetic sine'.ljust(60, b'\x00') + bytes([1, 14, 0]) \
        + struct.pack('<HHI', 1, 127, wave.nbytes) + bytes(36)
    instrument = struct.pack('<H', 0) + b'sine'.ljust(16, b'\x00') + struct.pack('<i', 96 + wave.nbytes) + bytes([1]) + bytes(40)
    layer = bytes([0, 0]) + struct.pack('<i', 96 + wave.nbytes) + bytes([1]) + bytes(40)
    sample = b'sine\x00\x00\x00' + bytes([0]) + struct.pack('<iii', wave.nbytes, 0, 2 * 4400) + struct.pack('<H', SR) \
        + struct.pack('<iii', 20000, 20000000, int(ROOT * 1000)) + struct.pack('<h', 0) + bytes([7]) \
        + bytes([63] * 6) + bytes([255] * 6) + bytes(3) + bytes(vib) + bytes([0x05]) + struct.pack('<hH', 60, 1024) + bytes(36)
    assert len(header) == 129 and len(instrument) == 63 and len(layer) == 47 and len(sample) == 96
    open(path, 'wb').write(header + instrument + layer + sample + wave.tobytes())


folder = tempfile.mkdtemp(prefix='koni_gus_')
make_patch(os.path.join(folder, 'sine.pat'))
VIB_SWEEP, VIB_RATE, VIB_DEPTH = 0, 100, 42
make_patch(os.path.join(folder, 'vib.pat'), (VIB_SWEEP, VIB_RATE, VIB_DEPTH))
open(os.path.join(folder, 'timidity.cfg'), 'w').write('bank 0\n0 sine.pat\n1 vib.pat\ndrumset 0\n36 sine.pat\n')
os.environ['KONI_GUS_PATCHES'] = folder


def fc_for(milli, voices):
    """_GF1_CALC_FC de la biblioteca del SDK con fs entera = 617 400 / voces."""
    chip = 617400 // voices
    ratio = ((chip << 9) + (SR >> 1)) // SR
    root = int(ROOT * 1000)
    t = ((milli << 9) + (root >> 1)) // root
    return (((t << 9) + (ratio >> 1)) // ratio) & 0x7FFF


def crossings(x, fs):
    y = x[int(0.15 * fs):]
    idx = np.where((y[:-1] < 0) & (y[1:] >= 0))[0]
    return idx + (-y[idx]) / (y[idx + 1] - y[idx])


def pitch(x, fs):
    """Frecuencia media por cruces por cero ascendentes interpolados (parte estable)."""
    t = crossings(x[:int(0.85 * fs)], fs)
    return (len(t) - 1) * fs / (t[-1] - t[0])


for voices in (14, 32):
    chip = 617400.0 / voices                                 # §1.5 del SDK: 1e6 / (1,619695497 · voces)
    expected = fc_for(440000, voices) / 512 * chip / 100     # 100 muestras por periodo del seno
    x, fs = render(['--param=gusVoices=%d' % voices], 1.0, ['69:0.05:0.9:127'], 'gus.wav', engine='Gravis UltraSound')
    f = pitch(x, fs)
    c.check(np.max(np.abs(x)) > 0.05 and abs(f - expected) < 0.1,
            '%d voces (GF1 a %.0f Hz): La 4 a %.3f Hz (esperado %.3f Hz por el paso de la biblioteca; pico %.1f dBFS)'
            % (voices, chip, f, expected, db(np.max(np.abs(x)))))

# Vibrato (calculate_vibrato y el Timer 2): rate 100 → f = (100·2334 + 5000)/100 = 2384 mHz; ticks por cuarto =
# round(45 289 / 9536) = 5; LFO = 45,2899 Hz / 20 = 2,264 Hz. Profundidad 42 = 2 semitonos: desviación ±(1,122 − 1)/2 = ±6,1 %
x, fs = render(['--param=gusProgram=1', '--param=gusVoices=14'], 4.6, ['69:0.05:4.5:127'], 'gus.wav',
               engine='Gravis UltraSound')
t = crossings(x[:int(4.5 * fs)], fs)
inst = fs / np.diff(t)                                       # tono por periodo del seno
tm = (t[1:] + t[:-1]) / 2 / fs
grid = np.arange(tm[0], tm[-1], 0.005)
track = np.interp(grid, tm, inst) - np.mean(inst)
spec = np.abs(np.fft.rfft(track * np.hanning(len(track)), 1 << 18))
lfo = np.fft.rfftfreq(1 << 18, 0.005)[np.argmax(spec[1:]) + 1]
lfo_expected = 1 / (69 * 320e-6) / (4 * 5)
swing = 100 * (np.percentile(inst, 99.5) / np.percentile(inst, 0.5) - 1) / 2
c.check(abs(lfo - lfo_expected) < 0.03 * lfo_expected and 4.5 < swing < 7.5,
        'vibrato rate 100, depth 42: LFO %.3f Hz (esperado %.3f Hz), desviación ±%.1f %% (esperado ±6,1 %%)'
        % (lfo, lfo_expected, swing))

# Percusión (Keys = Rhythm): la tecla 36 del drumset suena
x, fs = render(['GUS Keys=Rhythm'], 0.6, ['36:0.05:0.4:127'], 'gus.wav', engine='Gravis UltraSound')
c.check(np.max(np.abs(x)) > 0.05, 'Rhythm, tecla 36 del drumset: suena (pico %.1f dBFS)' % db(np.max(np.abs(x))))
sys.exit(c.summary())
