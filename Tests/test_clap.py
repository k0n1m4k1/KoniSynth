# clap-validator sobre el .clap compilado (opcional: necesita la variable CLAP_VALIDATOR con la ruta de clap-validator.exe;
# sin ella se omite). Exige 0 pruebas fallidas
import os, sys, subprocess, re
from common import ROOT, Checker
exe = os.environ.get('CLAP_VALIDATOR', '')
clap = os.path.join(ROOT, 'build', 'KoniSynth_artefacts', 'Release', 'CLAP', 'KoniSynth.clap')
if not exe or not os.path.exists(exe):
    print('clap-validator: omitido (define CLAP_VALIDATOR con la ruta del ejecutable)')
    sys.exit(0)
r = subprocess.run([exe, 'validate', clap], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=600)
text = re.sub(r'\x1b\[[0-9;]*m', '', r.stdout + r.stderr)
m = re.search(r'(\d+) tests run, (\d+) passed, (\d+) failed, (\d+) warnings?, (\d+) skipped', text)
c = Checker('clap-validator')
c.check(m is not None and m.group(3) == '0', m.group(0) if m else 'sin resumen: ' + text[-300:])
sys.exit(c.summary())
