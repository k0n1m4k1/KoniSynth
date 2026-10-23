# Alcance de la interfaz (estático, sin compilar): todo parámetro de Params/Layout.h tiene un control en Source/UI y todo
# motor del parámetro "engine" aparece en los grupos del selector de motor (Editor.h). Evita funcionalidad sin forma de usarla
import os, re, sys, glob
from common import ROOT, Checker

c = Checker('alcance UI')
src = os.path.join(ROOT, 'Source')
lay = open(os.path.join(src, 'Params', 'Layout.h'), encoding='utf-8').read()
ui = ''.join(open(p, encoding='utf-8').read() for p in glob.glob(os.path.join(src, 'UI', '*.h')))

ids = set(re.findall(r'ParameterID \{ "(\w+)"', lay))
# IDs construidos en bucle en Layout.h: se expanden igual que allí
for prefix, n in (('chipPan', 4), ('mdCh3Op', 3), ('macro', 8)):
    if prefix in ids:
        ids.discard(prefix)
        ids.update('%s%d' % (prefix, k) for k in range(1, n + 1))
for d in re.findall(r'Digit \{ "(\w+)"', lay):
    ids.add(d)
for pre in re.findall(r'for \(const auto\* pre : \{ ([^}]*) \}', lay):
    for p in re.findall(r'"(\w+)"', pre):
        for suf in re.findall(r'ParameterID \{ id \+ "(\w+)"', lay):
            ids.add(p + suf)
dynamic_ok = {'macro%d' % k for k in range(1, 9)}  # la UI los crea con PerfMacros::paramId (m)
missing = sorted(i for i in ids if '"%s"' % i not in ui and i not in dynamic_ok)
c.check(not missing, 'parámetros con control en la UI: %d de %d%s' % (len(ids) - len(missing), len(ids), (' (faltan: ' + ' '.join(missing) + ')') if missing else ''))

engines = re.search(r'ParameterID \{ "engine", 1 \}, "Engine", StringArray \{ ([^}]*) \}', lay).group(1)
names = re.findall(r'"([^"]+)"', engines)
groups = re.search(r'groups\[\] = \{(.*?)\};', ui, re.S).group(1)
listed = {int(v) for v in re.findall(r'\d+', re.sub(r'"[^"]*"', '', groups))}
absent = [names[k] for k in range(len(names)) if k not in listed]
c.check(not absent, 'motores en el selector: %d de %d%s' % (len(names) - len(absent), len(names), (' (faltan: ' + ', '.join(absent) + ')') if absent else ''))
# Un solo desplegable de cabecera por sección (withCombo / withSortedCombo): un segundo mezcla sus opciones en el mismo select
def multi_combo_chains(text):
    """Sentencias 'x.withA (...).withB (...)...;' con más de un withCombo/withSortedCombo en el nivel superior de la cadena.
    Recorre por profundidad de paréntesis y llaves: las lambdas de withHeaderButton (con sus propias llamadas) no cuentan."""
    out = []
    for m in re.finditer(r'\b\w+\s*\.\s*with\w+\s*\(', text):
        if m.start() > 0 and text[m.start() - 1] in '.>':  # a mitad de otra cadena
            continue
        depth, i, combos = 0, m.start(), 0
        while i < len(text):
            ch = text[i]
            if ch in '({':
                depth += 1
            elif ch in ')}':
                depth -= 1
            elif ch == ';' and depth == 0:
                break
            elif ch == '.' and depth == 0 and re.match(r'\.\s*with(?:Sorted)?Combo\s*\(', text[i:i + 40]):
                combos += 1
            i += 1
        if combos > 1:
            out.append(text[:m.start()].count('\n') + 1)
    return out


ed = open(os.path.join(src, 'UI', 'Editor.h'), encoding='utf-8').read()
bad = ['Editor.h:%d' % n for n in multi_combo_chains(ed)]
c.check(not bad, 'secciones con más de un desplegable: %d%s' % (len(bad), (' (' + ', '.join(bad) + ')') if bad else ''))
sys.exit(c.summary())
