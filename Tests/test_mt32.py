# Motor Roland MT-32 (Munt): sin ROM de Roland válidas no suena ni falla. Con KONI_MT32_ROMS_REAL=<carpeta con las ROM del
# usuario> comprueba además que suena (Part 1, programa 1) y que el modo Rhythm también. Las ROM no se distribuyen
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
        x, fs = render(['MT-32 Mode=' + mode], 1.5, ['%d:0.1:1.0:100' % note], 'mt32.wav', engine='Roland MT-32')
        c.check(np.max(np.abs(x)) > 0.01, 'ROM reales, %s: suena (pico %.1f dBFS)' % (mode, db(np.max(np.abs(x)))))
else:
    print('  (sin KONI_MT32_ROMS_REAL: no se prueba el sonido con ROM reales)')
sys.exit(c.summary())
