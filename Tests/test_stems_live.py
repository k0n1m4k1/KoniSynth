# Solo con run_all.py --live: usa el standalone real y la configuración del usuario (con copia y restauración byte a byte).
# Render de pistas (Transport/StemRenderer.h) por las órdenes render_stems / render_status del agente:
#  - un .mid de prueba de tipo 1 (pista de director con tempo y un CC 7 = 0 en el canal 1, pista A en el canal 1 con C4,
#    pista B en el canal 2 con G5), 4 s a 120 BPM;
#  - dos stems con presets distintos: existen, misma longitud, más rápido que el tiempo real, cada uno solo con su nota
#    (pico del espectro) y el evento de director de su canal aplicado (pitch bend al máximo en el canal 1 a 3,0 s: la
#    pista A sube; la B no, aunque su motor atiende el pitch bend de cualquier canal);
#  - con mix, un tercer WAV que es la suma exacta de los dos;
#  - el mismo render dos veces da los mismos bytes (reproducible).
import os, json, socket, subprocess, time, shutil, filecmp, glob, re, sys, tempfile, struct, hashlib
import numpy as np
sys.stdout.reconfigure(encoding='utf-8')
S = tempfile.mkdtemp(prefix='koni_stems_')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'build', 'KoniSynth_artefacts', 'Release', 'Standalone', 'KoniSynth.exe')
K = os.path.join(os.environ['APPDATA'], 'KoniSynth')
CFG, SET, AGENT = os.path.join(K, 'config.settings'), os.path.join(K, 'KoniSynth.settings'), os.path.join(K, 'agent')
bak = {f: os.path.join(S, os.path.basename(f) + '.bak') for f in (CFG, SET)}
for f, b in bak.items(): shutil.copy2(f, b)
results = []
def check(ok, what): results.append(bool(ok)); print(('ok    ' if ok else 'FALLO ') + what)

# --- .mid de prueba (SMF tipo 1, 480 PPQ): director (tempo 120 BPM; pitch bend máximo en el canal 1 a 3,0 s), A (canal 1, C4 cada
# 0,5 s) y B (canal 2, G5 cada 0,5 s, desplazado 0,25 s)
def vlq(n):
    out = [n & 0x7F]; n >>= 7
    while n: out.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(out))
def track(events):  # events: (tick, bytes)
    data, last = b'', 0
    for tick, ev in sorted(events, key=lambda e: e[0]):
        data += vlq(tick - last) + ev; last = tick
    data += vlq(0) + b'\xFF\x2F\x00'
    return b'MTrk' + struct.pack('>I', len(data)) + data
PPQ = 480
conductor = track([(0, b'\xFF\x51\x03\x07\xA1\x20'), (6 * PPQ, b'\xE0\x7F\x7F')])  # 500000 us/negra; bend máximo a 3,0 s
a = track([(0, b'\xFF\x03\x01A')] + [ev for k in range(8) for ev in ((k * PPQ, bytes([0x90, 60, 100])), (k * PPQ + PPQ * 3 // 4, bytes([0x80, 60, 0])))])
b = track([(0, b'\xFF\x03\x01B')] + [ev for k in range(8) for ev in ((k * PPQ + PPQ // 2, bytes([0x91, 79, 100])), (k * PPQ + PPQ // 2 + PPQ * 3 // 8, bytes([0x81, 79, 0])))])
MID = os.path.join(S, 'stemtest.mid')
open(MID, 'wb').write(b'MThd' + struct.pack('>IHHH', 6, 1, 3, PPQ) + conductor + a + b)

def read_float_wav(path):  # float de 32 bits: el módulo wave no lo lee; a mano tras la cabecera
    d = open(path, 'rb').read()
    i = d.find(b'data'); n = struct.unpack('<I', d[i + 4:i + 8])[0]
    fmt = d.find(b'fmt '); fs = struct.unpack('<I', d[fmt + 12:fmt + 16])[0]
    return np.frombuffer(d[i + 8:i + 8 + n], '<f4').reshape(-1, 2), fs

def peak_hz(x, fs, t0, t1):
    s = x[int(t0 * fs):int(t1 * fs)].mean(axis=1)
    spec = np.abs(np.fft.rfft(s * np.hanning(len(s)), 1 << 17)); f = np.fft.rfftfreq(1 << 17, 1 / fs)
    m = (f > 100) & (f < 2000)
    return f[m][np.argmax(spec[m])]

def rms_db(x, fs, t0, t1):
    s = x[int(t0 * fs):int(t1 * fs)]
    return 20 * np.log10(max(1e-12, float(np.sqrt(np.mean(s ** 2)))))

p = None
try:
    txt = open(CFG, encoding='utf-8').read()
    if 'agentControl' in txt:
        txt = re.sub(r'<VALUE name="agentControl" val="[^"]*"/>', '<VALUE name="agentControl" val="1"/>', txt)
    else:
        txt = txt.replace('</PROPERTIES>', '  <VALUE name="agentControl" val="1"/>\n</PROPERTIES>')
    open(CFG, 'w', encoding='utf-8').write(txt)
    for x in glob.glob(os.path.join(AGENT, '*.json')): os.remove(x)
    p = subprocess.Popen([APP]); time.sleep(5)
    info = json.load(open(glob.glob(os.path.join(AGENT, '*.json'))[0], encoding='utf-8'))
    s = socket.create_connection(('127.0.0.1', info['port']), timeout=10); f = s.makefile('rwb'); n = [0]
    def call(cmd, **kw):
        n[0] += 1
        f.write((json.dumps(dict(token=info['token'], id=n[0], cmd=cmd, **kw)) + '\n').encode()); f.flush()
        return json.loads(f.readline())

    def render(out):
        r = call('render_stems', mid=MID, outDir=out, sampleRate=48000, mix=True,
                 tracks=[{'track': 0, 'preset': 'OPL2 E.Piano'}, {'track': 1, 'preset': 'DX7 E.Piano 1'}])
        if not r['ok']:
            return r, None
        t0 = time.time()
        while time.time() - t0 < 60:
            st = call('render_status')['result']
            if not st['running'] and st['result']:
                return r, (st['result'], time.time() - t0)
            time.sleep(0.05)
        return r, None

    out1 = os.path.join(S, 'out1')
    r, done = render(out1)
    check(r['ok'] and len(r['result']['files']) == 3, 'render_stems: arranca con 2 pistas + la mezcla (%s)' % r.get('error', ''))
    res, wall = done if done else ({}, 0)
    check(res.get('ok'), 'render terminado: %s' % res.get('message'))
    files = res.get('files', [])
    if len(files) == 3:
        (xa, fs), (xb, _), (xm, _) = read_float_wav(files[0]), read_float_wav(files[1]), read_float_wav(files[2])
        check(files[2].endswith(' - Mix.wav') and xm.shape == xa.shape and np.max(np.abs(xm - (xa + xb))) < 1e-6,
              'mezcla = suma de las pistas: error máximo %.1e' % (np.max(np.abs(xm - (xa + xb))) if xm.shape == xa.shape else -1))
        check(len(xa) == len(xb), 'misma longitud: %d y %d muestras (%.2f s)' % (len(xa), len(xb), len(xa) / fs))
        check(4.0 <= len(xa) / fs <= 9.5, 'longitud = canción (4 s) + cola: %.2f s' % (len(xa) / fs))
        check(wall < 4.0, 'más rápido que el tiempo real: %.2f s de reloj para 4 s de canción (%.0f×)' % (wall, 4.0 / max(wall, 1e-3)))
        fa, fb = peak_hz(xa, fs, 0.05, 2.9), peak_hz(xb, fs, 0.3, 2.9)
        check(abs(1200 * np.log2(fa / 261.63)) < 50 or abs(1200 * np.log2(fa / 523.25)) < 50, 'pista A: pico %.1f Hz (C4 = 261,6 Hz o su octava)' % fa)
        check(abs(1200 * np.log2(fb / 783.99)) < 50 or abs(1200 * np.log2(fb / 1567.98)) < 50, 'pista B: pico %.1f Hz (G5 = 784 Hz o su octava)' % fb)
        ca = 1200 * np.log2(peak_hz(xa, fs, 3.05, 3.45) / peak_hz(xa, fs, 2.55, 2.95))
        cb = 1200 * np.log2(peak_hz(xb, fs, 3.3, 3.65) / peak_hz(xb, fs, 2.8, 3.15))
        check(ca > 150 and abs(cb) < 10, 'pitch bend de la pista de director (canal 1, 3,0 s): A %+.0f cents, B %+.0f cents' % (ca, cb))
        out2 = os.path.join(S, 'out2')
        r2, done2 = render(out2)
        files2 = done2[0].get('files', []) if done2 else []
        same = len(files2) == 3 and all(hashlib.sha256(open(x, 'rb').read()).hexdigest() == hashlib.sha256(open(y, 'rb').read()).hexdigest()
                                        for x, y in zip(files, files2))
        check(same, 'el mismo render dos veces da los mismos bytes')
    r = call('render_stems', mid=MID, outDir=os.path.join(S, 'bad'), tracks=[{'track': 9}])
    check(not r['ok'] and 'track 9' in r.get('error', ''), 'pista inexistente rechazada: %s' % r.get('error'))
    f.close(); s.close()
finally:
    if p: p.kill(); p.wait()
    for fl, bk in bak.items(): shutil.copy2(bk, fl)
    for x in glob.glob(os.path.join(AGENT, '*.json')): os.remove(x)
    print('ajustes restaurados, idénticos:', all(filecmp.cmp(bk, fl, shallow=False) for fl, bk in bak.items()))
    print('%d/%d comprobaciones' % (sum(results), len(results)))
sys.exit(0 if results and all(results) else 1)
