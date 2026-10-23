# Solo con run_all.py --live: usa el standalone real y la configuración del usuario (con copia y restauración byte a byte).
# Versionado de presets contra el standalone real (servidor de agente). Los presets de prueba (zzKoniTest*) se borran.
import os, json, socket, subprocess, time, shutil, filecmp, glob, re, sys, tempfile
sys.stdout.reconfigure(encoding='utf-8')
S = tempfile.mkdtemp(prefix='koni_live_')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'build', 'KoniSynth_artefacts', 'Release', 'Standalone', 'KoniSynth.exe')
K = os.path.join(os.environ['APPDATA'], 'KoniSynth')
CFG, SET, AGENT, PRE = os.path.join(K, 'config.settings'), os.path.join(K, 'KoniSynth.settings'), os.path.join(K, 'agent'), os.path.join(K, 'Presets')
bak = {f: os.path.join(S, os.path.basename(f) + '.pvbak') for f in (CFG, SET)}
for f, b in bak.items(): shutil.copy2(f, b)
os.makedirs(PRE, exist_ok=True)
before = sorted(os.listdir(PRE))
fav = os.path.join(K, 'favorites.txt'); fav_before = open(fav, encoding='utf-8').read() if os.path.exists(fav) else None
results = []
def check(ok, what): results.append(ok); print(('ok    ' if ok else 'FALLO ') + what)
v1 = os.path.join(PRE, 'zzKoniTest v1.xml'); v3 = os.path.join(PRE, 'zzKoniTest v3.xml'); saved = os.path.join(PRE, 'zzKoniTest Saved.xml')
open(v1, 'w', encoding='utf-8').write('<?xml version="1.0" encoding="UTF-8"?>\n<KoniSynthPreset presetCategory="Lead" thereminOn="1" cutoff="1000"/>\n')
open(v3, 'w', encoding='utf-8').write('<?xml version="1.0" encoding="UTF-8"?>\n<KoniSynthPreset formatVersion="3" pluginVersion="9.9.0" presetCategory="Pad" engine="0" cutoff="2000" futureKnob="0.5"/>\n')
p = None
try:
    txt = open(CFG, encoding='utf-8').read()
    if 'agentControl' in txt:
        txt = re.sub(r'<VALUE name="agentControl" val="[^"]*"/>', '<VALUE name="agentControl" val="1"/>', txt)
    else:
        txt = txt.replace('</PROPERTIES>', '  <VALUE name="agentControl" val="1"/>\n</PROPERTIES>')
    open(CFG, 'w', encoding='utf-8').write(txt)
    for a in glob.glob(os.path.join(AGENT, '*.json')): os.remove(a)
    p = subprocess.Popen([APP]); time.sleep(5)
    info = json.load(open(glob.glob(os.path.join(AGENT, '*.json'))[0], encoding='utf-8'))
    s = socket.create_connection(('127.0.0.1', info['port']), timeout=10); f = s.makefile('rwb'); n = [0]
    def call(cmd, **kw):
        n[0] += 1
        f.write((json.dumps(dict(token=info['token'], id=n[0], cmd=cmd, **kw)) + '\n').encode()); f.flush()
        return json.loads(f.readline())
    r = call('load_preset', name='zzKoniTest v1')
    g = call('get', ids=['cutoff'])
    check(r['ok'] and r['result']['engine'] == 'Theremin' and 'note' not in r['result'] and g['result']['cutoff'] in ('1000 Hz', '1.00 kHz'),
          'formato 1 (sin formatVersion, thereminOn=1): motor %s, cutoff %s, sin aviso' % (r.get('result', {}).get('engine'), g['result']['cutoff']))
    r = call('load_preset', name='zzKoniTest v3')
    note = r.get('result', {}).get('note', '')
    g = call('get', ids=['cutoff'])
    check(r['ok'] and r['result']['engine'] == 'Synth' and 'newer KoniSynth (9.9.0' in note and 'futureKnob' in note and g['result']['cutoff'].startswith('2.00 k'),
          'formato 3 (posterior): carga lo conocido (cutoff %s) y avisa: "%s"' % (g['result']['cutoff'], note))
    r = call('save_preset', name='zzKoniTest Saved', category='Lead')
    head = open(saved, encoding='utf-8').read()[:300] if os.path.exists(saved) else ''
    m1 = re.search(r'formatVersion="(\d+)"', head); m2 = re.search(r'pluginVersion="([^"]+)"', head)
    check(r['ok'] and m1 and m1.group(1) == '2' and m2 is not None, 'guardado: formatVersion=%s pluginVersion=%s' % (m1 and m1.group(1), m2 and m2.group(1)))
    r = call('presets')
    names = [x['name'] if isinstance(x, dict) else x for x in r['result']]
    check(r['ok'] and sum(1 for x in names if str(x).startswith('zzKoniTest')) == 3, 'presets: %d en total, 3 de prueba' % len(names))
    f.close(); s.close()
finally:
    if p: p.kill(); p.wait()
    for x in (v1, v3, saved):
        if os.path.exists(x): os.remove(x)
    for fl, b in bak.items(): shutil.copy2(b, fl)
    for a in glob.glob(os.path.join(AGENT, '*.json')): os.remove(a)
    print('config y settings restaurados, idénticos:', all(filecmp.cmp(b, fl, shallow=False) for fl, b in bak.items()))
    print('carpeta de presets como estaba:', sorted(os.listdir(PRE)) == before, '| favoritos intactos:', (open(fav, encoding='utf-8').read() if os.path.exists(fav) else None) == fav_before)
    print('%d/%d comprobaciones' % (sum(results), len(results)))
sys.exit(0 if results and all(results) else 1)
