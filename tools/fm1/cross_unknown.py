#!/usr/bin/env python3
"""Instrucciones que Ghidra (ghidra-jieli) decodifica y la tabla de fm1-emu no: qué dice el objdump de JieLi en
la misma dirección (objdump: desplazamiento desde 0; Ghidra: 0x02000000 + desplazamiento) y si esa dirección es
inicio de instrucción en el objdump (código) o cae dentro de otra / en una zona que el objdump no lista.
Uso: cross_unknown.py <fm1-isa/src> <objdump.txt> <ghidra_linear.asm>"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import isa_table  # noqa: E402
from check_isa import read_listing  # noqa: E402

src, objdump, ghidra = sys.argv[1:4]
isa, fpu, fb = isa_table.load(src)
isa_table.prepare(isa)
obj = {a: (b, t) for a, b, t in read_listing(objdump)}
starts = sorted(obj)
gh = read_listing(ghidra)
mem = {}
for a, b, _ in gh:
    for i, x in enumerate(b):
        mem.setdefault(a + i, x)
kinds = collections.Counter()
for a, b, t in gh:
    win = sum(mem.get(a + i, 0) << (8 * i) for i in range(6))
    if isa_table.decode(win, isa, fpu, fb) is not None:
        continue
    off = a - 0x02000000
    if off in obj:
        kinds['inicio de instrucción en objdump'] += 1
        print('%08x  %-6s %-28s | objdump: %s  %s' % (a, b.hex(), t[:28], obj[off][0].hex(), obj[off][1][:40]))
    else:
        import bisect
        i = bisect.bisect_right(starts, off) - 1
        inside = i >= 0 and starts[i] + len(obj[starts[i]][0]) > off
        kinds['dentro de otra instrucción del objdump' if inside else 'fuera de lo que lista el objdump'] += 1
for k, v in kinds.items():
    print('%5d  %s' % (v, k))
