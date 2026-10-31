# Pruebas unitarias compiladas en KoniBench (sin el plugin): historial de deshacer, transporte, afinación Scala y media
# banda de la saturación, ritmos de usuario, planificador de notas del agente y filtro Mono. Cada una imprime "N fallos" al final.
import subprocess, sys, re
from common import EXE, Checker

c = Checker('unitarias')
for name in ('undotest', 'transporttest', 'tuningtest', 'sattest', 'rhythmtest', 'schedtest', 'monotest', 'taptest', 'eqtest', 'comptest'):
    r = subprocess.run([EXE, name], capture_output=True, text=True, timeout=300)
    m = re.search(r'(\d+) fallos', r.stdout)
    lines = [l for l in r.stdout.splitlines() if l.strip()]
    okcount = sum(1 for l in lines if l.startswith('OK'))
    c.check(m is not None and m.group(1) == '0' and r.returncode == 0, '%-13s %d comprobaciones, %s fallos' % (name, okcount, m.group(1) if m else '?'))
    if m is None or m.group(1) != '0':
        print('\n'.join('    ' + l for l in lines if not l.startswith('OK')))
sys.exit(c.summary())
