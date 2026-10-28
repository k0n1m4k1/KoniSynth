# Solo con run_all.py --live: usa el standalone real y la configuración del usuario (con copia y restauración byte a byte).
# Versionado de presets contra el standalone real (servidor de agente). Los presets de prueba (zzKoniTest*) se borran.
import os, json, socket, subprocess, time, shutil, filecmp, glob, re, sys, tempfile
sys.stdout.reconfigure(encoding='utf-8')
S = tempfile.mkdtemp(prefix='koni_live_')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'build', 'KoniSynth_artefacts', 'Release', 'Standalone', 'KoniSynth.exe')
K = os.path.join(os.environ['APPDATA'], 'KoniSynth')
CFG, SET, AGENT, PRE = os.path.join(K, 'config.settings'), os.path.join(K, 'KoniSynth.settings'), os.path.join(K, 'agent'), os.path.join(K, 'Presets')
# config.settings, KoniSynth.settings y los .txt de ajustes de versiones anteriores que existan: el standalone los migra a
# config.settings y los borra, así que se guardan y se restauran todos
LEGACY = [os.path.join(K, n) for n in ('favorites.txt', 'mt32-roms.txt', 'mcp-bridge.txt', 'simd-selftest.txt')]
bak = {f: os.path.join(S, os.path.basename(f) + '.pvbak') for f in [CFG, SET] + [x for x in LEGACY if os.path.exists(x)]}
for f, b in bak.items(): shutil.copy2(f, b)
os.makedirs(PRE, exist_ok=True)
before = sorted(os.listdir(PRE))
results = []
def check(ok, what): results.append(ok); print(('ok    ' if ok else 'FALLO ') + what)
v1 = os.path.join(PRE, 'zzKoniTest v1.xml'); v3 = os.path.join(PRE, 'zzKoniTest v3.xml'); saved = os.path.join(PRE, 'zzKoniTest Saved.xml')
# Formato 4: KSL del OPL como valor de registro (OPL2: 1 = 3 dB/oct); en OPLL el registro ya iba en dB (1 = 1,5 dB/oct)
k4 = os.path.join(PRE, 'zzKoniTest ksl4.xml'); k4l = os.path.join(PRE, 'zzKoniTest ksl4 opll.xml')
open(k4, 'w', encoding='utf-8').write('<?xml version="1.0" encoding="UTF-8"?>\n<KoniSynthPreset formatVersion="4" pluginVersion="1.0.0" presetCategory="Keys" engine="13" oplModel="0" opl2Ksl="1" opl1Ksl="2" opl3Ksl="3"/>\n')
# Formato 5: el SNES usaba la fuente Sample (fichero y región); se pasa a su sample propio
k5s = os.path.join(PRE, 'zzKoniTest snes5.xml')
open(k5s, 'w', encoding='utf-8').write('<?xml version="1.0" encoding="UTF-8"?>\n<KoniSynthPreset formatVersion="5" pluginVersion="1.0.0" presetCategory="8-Bit" engine="11" snesSource="6" sampleStart="0.25" sampleRoot="67" samplePath=""/>\n')
open(k4l, 'w', encoding='utf-8').write('<?xml version="1.0" encoding="UTF-8"?>\n<KoniSynthPreset formatVersion="4" pluginVersion="1.0.0" presetCategory="Keys" engine="13" oplModel="2" opl2Ksl="1"/>\n')
open(v1, 'w', encoding='utf-8').write('<?xml version="1.0" encoding="UTF-8"?>\n<KoniSynthPreset presetCategory="Lead" thereminOn="1" cutoff="1000"/>\n')
open(v3, 'w', encoding='utf-8').write('<?xml version="1.0" encoding="UTF-8"?>\n<KoniSynthPreset formatVersion="99" pluginVersion="9.9.0" presetCategory="Pad" engine="0" cutoff="2000" futureKnob="0.5"/>\n')
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
          'formato 99 (posterior): carga lo conocido (cutoff %s) y avisa: "%s"' % (g['result']['cutoff'], note))
    r = call('save_preset', name='zzKoniTest Saved', category='Lead')
    head = open(saved, encoding='utf-8').read()[:300] if os.path.exists(saved) else ''
    m1 = re.search(r'formatVersion="(\d+)"', head); m2 = re.search(r'pluginVersion="([^"]+)"', head)
    check(r['ok'] and m1 and m1.group(1) == '6' and m2 is not None, 'guardado: formatVersion=%s pluginVersion=%s' % (m1 and m1.group(1), m2 and m2.group(1)))
    r = call('load_preset', name='zzKoniTest ksl4')
    g = call('get', ids=['opl1Ksl', 'opl2Ksl', 'opl3Ksl'])['result']
    check(r['ok'] and g == {'opl1Ksl': '1.5 dB/oct', 'opl2Ksl': '3 dB/oct', 'opl3Ksl': '6 dB/oct'},
          'formato 4, OPL2: KSL de registro 2/1/3 -> %s (mismo sonido)' % g)
    r = call('load_preset', name='zzKoniTest ksl4 opll')
    g = call('get', ids=['opl2Ksl'])['result']
    check(r['ok'] and g == {'opl2Ksl': '1.5 dB/oct'}, 'formato 4, OPLL: KSL 1 sigue en %s (su registro ya iba en dB)' % g['opl2Ksl'])
    r = call('load_preset', name='zzKoniTest snes5')
    g = call('get', ids=['snesSampleStart', 'snesSampleRoot', 'sampleStart'])['result']
    check(r['ok'] and g['snesSampleStart'] == '25 %' and g['snesSampleRoot'] == 'G4', 'formato 5, SNES: región de la fuente Sample -> la suya (%s)' % g)
    r = call('presets')
    names = [x['name'] if isinstance(x, dict) else x for x in r['result']]
    check(r['ok'] and sum(1 for x in names if str(x).startswith('zzKoniTest')) == 6, 'presets: %d en total, 6 de prueba' % len(names))
    f.close(); s.close()
finally:
    if p: p.kill(); p.wait()
    for x in (v1, v3, saved, k4, k4l, k5s):
        if os.path.exists(x): os.remove(x)
    for fl, b in bak.items(): shutil.copy2(b, fl)
    for a in glob.glob(os.path.join(AGENT, '*.json')): os.remove(a)
    print('ajustes restaurados, idénticos (%d ficheros):' % len(bak), all(filecmp.cmp(b, fl, shallow=False) for fl, b in bak.items()))
    print('carpeta de presets como estaba:', sorted(os.listdir(PRE)) == before)
    print('%d/%d comprobaciones' % (sum(results), len(results)))
sys.exit(0 if results and all(results) else 1)
