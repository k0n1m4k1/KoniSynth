# Todas las pruebas de KoniSynth en un comando: python Tests/run_all.py [--live]
# Antes: compilar KoniSynth_VST3, KoniSynth_CLAP y KoniBench (Release); CLAP_VALIDATOR = ruta de clap-validator (opcional). --live añade la prueba contra el standalone real (toca la
# configuración del usuario con copia y restauración byte a byte; no ejecutar con el standalone abierto).
import os, sys, subprocess, time
sys.stdout.reconfigure(encoding='utf-8')
HERE = os.path.dirname(os.path.abspath(__file__))
tests = ['test_units.py', 'test_regression.py', 'test_filters.py', 'test_voice.py', 'test_midi_learn.py', 'test_clap.py', 'test_mt32.py', 'test_ui_reach.py', 'test_rhythms.py', 'test_panic.py']
if '--live' in sys.argv:
    tests.append('test_preset_live.py')
rows, failed = [], 0
for t in tests:
    print('==', t, flush=True)
    t0 = time.perf_counter()
    r = subprocess.run([sys.executable, os.path.join(HERE, t)], cwd=HERE)
    rows.append((t, r.returncode == 0, time.perf_counter() - t0))
    failed += r.returncode != 0
print('\nResumen')
for t, ok, secs in rows:
    print('  %-22s %-6s %5.1f s' % (t, 'OK' if ok else 'FALLO', secs))
print('%d/%d suites correctas' % (len(rows) - failed, len(rows)))
sys.exit(1 if failed else 0)
