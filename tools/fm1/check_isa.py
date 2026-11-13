#!/usr/bin/env python3
"""Valida la tabla pi32v2 (tools/fm1/isa_table.py, leída de fm1-emu) contra listados de desensamblado de terceros.

Uso: check_isa.py <fm1-isa/src> <listado> [<listado> ...]
Formatos que entiende:
  - objdump de JieLi:   "     27c:    40 f0 10 27       \tr0 = 10000"   (dirección relativa, bytes, texto)
  - Ghidra (FM-1-RE):   "0200027c  40f01027                  mov r0,#0x2710"
Por cada instrucción listada se forma la ventana de 6 bytes con los bytes del propio listado (lo que sigue a la
instrucción sale de las líneas siguientes) y se decodifica; se compara la longitud. Informa coincidencias,
discrepancias (con ejemplos agrupados por clase) e instrucciones que la tabla no conoce.
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # con python -I, la carpeta del script no está en la ruta
import isa_table  # noqa: E402

OBJ = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t+(.*)$')
GHI = re.compile(r'^([0-9a-f]{8})  ([0-9a-f]+)\s+(.*)$')


def read_listing(path):
    rows = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = OBJ.match(line)
            if m:
                rows.append((int(m.group(1), 16), bytes.fromhex(m.group(2).replace(' ', '')), m.group(3).strip()))
                continue
            m = GHI.match(line)
            if m and len(m.group(2)) % 2 == 0:
                rows.append((int(m.group(1), 16), bytes.fromhex(m.group(2)), m.group(3).strip()))
    return rows


def main(src, listings):
    isa, fpu, fallback = isa_table.load(src)
    isa_table.prepare(isa)
    print('tabla: %d clases, FPU %d, respaldo %d, %d con ranuras resueltas' %
          (len(isa), len(fpu), len(fallback), sum(1 for e in isa if e['slots'])))
    ok_all = True
    for path in listings:
        rows = read_listing(path)
        mem = {}
        for addr, b, _ in rows:
            for i, x in enumerate(b):
                mem.setdefault(addr + i, x)
        same = diff = unknown = 0
        by_class = collections.defaultdict(list)
        unknown_ex = collections.Counter()
        for addr, b, text in rows:
            win = sum(mem.get(addr + i, 0) << (8 * i) for i in range(6))
            e = isa_table.decode(win, isa, fpu, fallback)
            if e is None:
                unknown += 1
                unknown_ex[b.hex()[:8] + '  ' + text[:40]] += 1
            elif e['len'] == len(b):
                same += 1
            else:
                diff += 1
                by_class[(e['name'], e['len'], len(b))].append('%08x %s  %s' % (addr, b.hex(), text[:50]))
        n = len(rows)
        print('\n%s\n  %d instrucciones: %d misma longitud (%.3f %%), %d distinta, %d desconocidas'
              % (path, n, same, 100.0 * same / max(1, n), diff, unknown))
        for (name, l1, l2), ex in sorted(by_class.items(), key=lambda kv: -len(kv[1]))[:15]:
            print('  %5d  tabla %-34s %d B, listado %d B   p. ej. %s' % (len(ex), name, l1, l2, ex[0]))
        for k, c in unknown_ex.most_common(8):
            print('  %5d  desconocida: %s' % (c, k))
        ok_all &= diff == 0 and unknown == 0
    return 0 if ok_all else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2:]))
