# Motor Roland MT-32 (Munt): sin ROM de Roland válidas no suena ni falla. Con KONI_MT32_ROMS_REAL=<carpeta con las ROM del
# usuario> comprueba además que suena (Part 1, programa 1), que el modo Rhythm también y que no se pierden notas al repetir
# rápido o con acordes que agotan los parciales. Las ROM no se distribuyen
import os, sys, tempfile
import numpy as np
from common import render, db, Checker

c = Checker('mt32')
junk = tempfile.mkdtemp(prefix='koni_mt32_')
rng = np.random.default_rng(1)
for name, size in (('ctrl.rom', 65536), ('pcm.rom', 524288)):  # tamaños de ROM reales, contenido aleatorio: SHA-1 desconocido
    open(os.path.join(junk, name), 'wb').write(rng.integers(0, 256, size, dtype=np.uint8).tobytes())
os.environ['KONI_MT32_ROMS'] = junk
x, fs = render([], 1.0, ['60:0.1:0.8:100'], 'mt32.wav', engine='Roland MT-32')
c.check(np.max(np.abs(x)) == 0.0, 'sin ROM válidas: silencio (pico %.1e) y sin fallo' % np.max(np.abs(x)))
real = os.environ.get('KONI_MT32_ROMS_REAL')
if real:
    os.environ['KONI_MT32_ROMS'] = real
    for mode, note in (('Part 1', 60), ('Rhythm', 36)):
        x, fs = render(['MT-32 Keys=' + mode], 1.5, ['%d:0.1:1.0:100' % note], 'mt32.wav', engine='Roland MT-32')
        c.check(np.max(np.abs(x)) > 0.01, 'ROM reales, %s: suena (pico %.1f dBFS)' % (mode, db(np.max(np.abs(x)))))
    # Ninguna nota perdida: si una nota obliga a abortar otra (single-assign o sin parciales libres), Munt sale sin tocarla
    # (Part::playPoly) y solo la cola de Munt la reintenta. Nota tocada: nivel en la segunda mitad de su duración a menos de
    # 8 dB del de la primera (con playMsgNow las perdidas quedaban de -11 a -25 dB: solo el aborto de la anterior)
    def level(x, fs, t0, t1):
        return db(np.sqrt(np.mean(x[int(t0 * fs):int(t1 * fs)] ** 2)) + 1e-12)
    cases = (('20 x C4 cada 60 ms', [(60, 0.2 + 0.06 * i, 0.05) for i in range(20)]),
             ('C4/E4 alternos cada 60 ms', [(60 + 4 * (i & 1), 0.2 + 0.06 * i, 0.05) for i in range(20)]),
             ('8 acordes de 8 notas cada 150 ms', [(60 + k, 0.2 + 0.15 * i, 0.12) for i in range(8) for k in (0, 4, 7, 11, 12, 16, 19, 23)]))
    for program in (1, 49):
        for name, notes in cases:
            x, fs = render(['MT-32 Keys=Part 1', 'MT-32 Program=%d' % program], 2.5,
                           ['%d:%.3f:%.3f:100' % n for n in notes], 'mt32.wav', engine='Roland MT-32')
            onsets = sorted({(t, d) for _, t, d in notes})
            lv = [level(x, fs, t + 0.5 * d, t + 0.95 * d) for t, d in onsets]
            played = sum(v > lv[0] - 8.0 for v in lv)
            c.check(played == len(onsets), 'ROM reales, programa %d, %s: %d de %d notas tocadas (peor %.1f dB)'
                    % (program, name, played, len(onsets), min(lv) - lv[0]))
else:
    print('  (sin KONI_MT32_ROMS_REAL: no se prueba el sonido con ROM reales)')
sys.exit(c.summary())
