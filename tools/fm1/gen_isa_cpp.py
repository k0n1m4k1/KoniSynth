#!/usr/bin/env python3
"""Escribe Source/Fm1/Fm1IsaTable.inc (la tabla de instrucciones pi32v2 en C++) a partir de fm1-emu.

Uso: gen_isa_cpp.py <fm1-emu/crates/fm1-isa/src> <salida.inc> <commit de fm1-emu>
La tabla es la de fm1-emu (MIT, Joakim Eriksson) tal cual: mismo orden de prioridad, máscaras, muestras y ranuras.
Validación previa: tools/fm1/check_isa.py (longitudes frente al objdump de JieLi y a Ghidra).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import isa_table  # noqa: E402

KIND = {'Imm': 0, 'Rel': 1, 'Adj': 2, 'Blk': 3, 'Scaled': 4, 'Const': 5, 'Zero32': 6, 'Bit': 7, 'NBit': 8,
        'Composed': 9, 'NComposed': 10}


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def canon(s):
    # canon_syntax de fm1-core: el signo de -#i va en la ranura y el " #" final (par de emisión doble) se quita
    s = s.replace('-#i', '#i').replace('-#h', '#h')
    while s.endswith(' #'):
        s = s[:-2]
    return s.rstrip()


def main(src, out, commit):
    isa, fpu, fb = isa_table.load(src)
    samples, slots, alts, rows = [], [], [], {}
    for table, entries in (('kIsa', isa), ('kFpu', fpu), ('kFallback', fb)):
        rows[table] = []
        for e in entries:
            s0, l0, a0 = len(samples), len(slots), len(alts)
            samples += e['samples']
            alts += e['alt']
            for s in e['slots']:
                p = s['pieces'] + [(0, 0)] * (6 - len(s['pieces']))
                slots.append('{ %d, %d, { %s }, %d, %d, %d, %d }' % (
                    KIND[s['kind']], len(s['pieces']), ', '.join('{ %d, %d }' % q for q in p), s['mult'], s['adj'],
                    s['hi'], s['lo']))
            rows[table].append('{ %s, %s, %s, %d, %du, 0x%016xull, 0x%016xull, kAlts + %d, %d, kSamples + %d, %d, kSlots + %d, %d }' % (
                cstr(e['name']), cstr(e['syntax']), cstr(canon(e['syntax'])), e['len'], e['count'], e['mask'], e['match'],
                a0, len(e['alt']), s0, len(e['samples']), l0, len(e['slots'])))
    L = []
    L.append('// GENERADO por tools/fm1/gen_isa_cpp.py: no editar a mano.')
    L.append('// Tabla de instrucciones pi32v2 (CPU del JieLi AC791N del M-VAVE FM-1), tomada de fm1-emu (Joakim Eriksson,')
    L.append('// licencia MIT, https://github.com/joakimeriksson/fm1-emu, commit %s): crates/fm1-isa/src/generated_isa.rs y' % commit)
    L.append('// fpu.rs, con el mismo orden de prioridad, máscaras, muestras del corpus y ranuras de operandos.')
    L.append('// Validada antes de generarla (tools/fm1/check_isa.py): misma longitud en las 199 931 instrucciones del objdump')
    L.append('// de JieLi de V13 y las 200 435 de V14, y ninguna longitud distinta frente a Ghidra con ghidra-jieli.')
    L.append('// Se incluye solo desde Fm1Isa.cpp.')
    L.append('')
    L.append('static const uint64_t kSamples[] = {')
    for i in range(0, len(samples), 6):
        L.append('    ' + ', '.join('0x%xull' % v for v in samples[i:i + 6]) + ',')
    L.append('};')
    L.append('')
    L.append('static const char* const kAlts[] = {')
    L += ['    %s,' % cstr(a) for a in alts] or ['    nullptr,']
    L.append('};')
    L.append('')
    L.append('static const Fm1SlotSpec kSlots[] = {')
    L += ['    %s,' % s for s in slots]
    L.append('};')
    for table in ('kIsa', 'kFpu', 'kFallback'):
        L.append('')
        L.append('static const Fm1IsaEntry %s[] = {' % table)
        L += ['    %s,' % r for r in rows[table]]
        L.append('};')
    L.append('')
    with open(out, 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('\n'.join(L))
    print('%s: %d clases, %d FPU, %d respaldo, %d muestras, %d ranuras, %d alias, %.2f MB'
          % (out, len(isa), len(fpu), len(fb), len(samples), len(slots), len(alts), os.path.getsize(out) / 1e6))


if __name__ == '__main__':
    main(*sys.argv[1:4])
