#!/usr/bin/env python3
# Board attribution (brief §4.1 step 8): read two measurement roots' manifests FIELD BY FIELD and attribute every
# measurements.* / payload difference, down to symbol level for RAM. usage: attribute.py <base-root> <final-root>
import json, sys
from pathlib import Path
base, final = Path(sys.argv[1]), Path(sys.argv[2])
RAM_SECTIONS = ('.data', '.bss', '.dram0.data', '.dram0.bss', '.noinit', '.rtc.data', '.rtc.bss')
def flat(d, p=''):
    out = {}
    if isinstance(d, dict):
        for k, v in d.items(): out.update(flat(v, f'{p}.{k}' if p else k))
    else: out[p] = d
    return out
def syms(root):
    t = {}
    for l in (root / 'symbols.txt').read_text().splitlines():
        f = l.split('\t')
        if len(f) >= 6 and f[1] == 'OBJECT': t[(f[0], f[4])] = int(f[5])
    return t
for env in sorted(p.name for p in base.iterdir() if p.is_dir()):
    b = json.loads((base / env / 'manifest.json').read_text()); c = json.loads((final / env / 'manifest.json').read_text())
    fb, fc = flat(b), flat(c)
    print(f'== {env}')
    for grp in ('measurements', 'artifacts', 'toolchain', 'fixed_identity', 'paths', 'source', 'environment'):
        diffs = [k for k in sorted(set(fb) | set(fc)) if k.startswith(grp + '.') and fb.get(k) != fc.get(k)]
        print(f'  {grp}: {len(diffs)} field(s) differ')
        if grp in ('measurements', 'artifacts'):
            for k in diffs: print(f'    {k}: {fb.get(k)} -> {fc.get(k)}')
    sb, sc = syms(base / env), syms(final / env)
    ram = [(k, sb.get(k, 0), sc.get(k, 0)) for k in sorted(set(sb) | set(sc))
           if sb.get(k) != sc.get(k) and k[1] in RAM_SECTIONS]
    tot = sum(n - o for _, o, n in ram)
    print(f'  RAM-section OBJECT symbols that changed: {len(ram)}, symbol-size sum {tot:+d}')
    for (name, sec), o, n in ram: print(f'    {sec:12s} {name:60s} {o:6d} -> {n:6d} ({n - o:+d})')
