#!/usr/bin/env python3
"""Tabla de instrucciones pi32v2 (CPU del JieLi AC791N del M-VAVE FM-1) leída de fm1-emu.

Fuente: fm1-emu de Joakim Eriksson (MIT), crates/fm1-isa/src/generated_isa.rs (la tabla que usa su CPU: máscaras,
prioridad, muestras del corpus y ranuras de operandos resueltas por su solver) y fpu.rs (la FPU y la familia de
respaldo, que no salen en el objdump del fabricante). Este módulo las lee tal cual y ofrece:
  - load(ruta_a_fm1-isa/src) -> (isa, fpu, fallback): listas de entradas (dict) en el orden de prioridad de fm1-emu;
  - decode(win, isa, fpu, fallback) -> entrada o None: el mismo algoritmo que fm1_isa::decode_win.
Lo usan gen_isa_cpp.py (escribe la tabla en C++) y check_isa.py (valida contra los listados de V13/V14).
"""
import os
import re

_ENTRY = re.compile(r'IsaEntry \{(.*?)\n    \},', re.S)
_SLOT = re.compile(r'SlotSpec \{ kind: SlotKind::(\w+), pieces: &\[([^\]]*)\], mult: (-?\d+), adj: (-?\d+), hi: (\d+), lo: (\d+) \}')
_FPU = re.compile(r'e!\("(\w+)", "((?:[^"\\]|\\.)*)", (0x[0-9a-f_]+), (0x[0-9a-f_]+)(?:, (\d+))?\)')


def _num(s):
    return int(s.replace('_u64', '').replace('_', ''), 16 if s.startswith('0x') else 10)


def _rust_str(s):
    # Las cadenas de la tabla solo llevan escapes \" y \\
    return s.replace('\\"', '"').replace('\\\\', '\\')


def _parse_generated(text):
    out = []
    for m in _ENTRY.finditer(text):
        body = m.group(1)
        get = lambda key: re.search(r'\n\s*%s: (.*?),\n' % key, body).group(1)
        name = _rust_str(get('name').strip('"'))
        syntax = _rust_str(re.search(r'\n\s*syntax: "((?:[^"\\]|\\.)*)",', body).group(1))
        alt_block = re.search(r'alt: &\[(.*?)\],', body, re.S).group(1)
        alt = [_rust_str(a) for a in re.findall(r'"((?:[^"\\]|\\.)*)"', alt_block)]
        samples_block = re.search(r'samples: &\[(.*?)\],', body, re.S).group(1)
        samples = [_num(s) for s in re.findall(r'0x[0-9a-f]+_u64', samples_block)]
        slots_block = re.search(r'slots: &\[(.*?)\],?\s*$', body, re.S).group(1)
        slots = []
        for s in _SLOT.finditer(slots_block):
            pieces = [tuple(int(x) for x in p) for p in re.findall(r'\((\d+), (\d+)\)', s.group(2))]
            slots.append(dict(kind=s.group(1), pieces=pieces, mult=int(s.group(3)), adj=int(s.group(4)),
                              hi=int(s.group(5)), lo=int(s.group(6))))
        out.append(dict(name=name, syntax=syntax, len=int(get('len')), count=int(get('count')),
                        mask=_num(get('mask')), match=_num(get('match_')), group=get('group').strip('"'),
                        alt=alt, samples=samples, slots=slots))
    return out


def _parse_fpu(text):
    fpu_part, _, fb_part = text.partition('pub static FALLBACK')
    def entries(part):
        return [dict(name=m.group(1), syntax=_rust_str(m.group(2)), len=int(m.group(5) or 4), count=0,
                     mask=_num(m.group(3)), match=_num(m.group(4)), group='fpu', alt=[], samples=[], slots=[])
                for m in _FPU.finditer(part)]
    return entries(fpu_part), entries(fb_part)


def load(src_dir):
    isa = _parse_generated(open(os.path.join(src_dir, 'generated_isa.rs'), encoding='utf-8').read())
    fpu, fallback = _parse_fpu(open(os.path.join(src_dir, 'fpu.rs'), encoding='utf-8').read())
    return isa, fpu, fallback


def decode(win, isa, fpu, fallback):
    """fm1_isa::decode_win: la FPU primero; luego la tabla en orden, y una entrada cuyas muestras contienen los bytes
    observados (solo los de su longitud) gana a sus hermanas; si nada coincide, la familia de respaldo."""
    for e in fpu:
        if win & e['mask'] == e['match']:
            return e
    best = None
    for e in isa:
        if win & e['mask'] == e['match']:
            obs = win & ((1 << (e['len'] * 8)) - 1)
            if obs in e['_sampleset']:
                return e
            if best is None:
                best = e
    if best is None:
        best = next((e for e in fallback if win & e['mask'] == e['match']), None)
    return best


def prepare(isa):
    for e in isa:
        e['_sampleset'] = set(e['samples'])
    return isa
