# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W2 — the old -> new control map, base (HEAD) runner vs final runner, both read from SOURCE by declared.py.
# Proves: every check outside W49/W51/W54 is byte-identical (label, file, predicate name, every control script); the
# only new check is W54-help; the 15 W49/W51/W54/W54-help controls carry exactly §2.6's dispositions.
# Usage: python3 ordinal_map.py <out.tsv> <out.json>
import json, os, subprocess, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from declared import declared_wiring
R = '/home/staszek/MeshRoute'; RUN = 'tools/probe_board_ui/run.sh'
base = declared_wiring(subprocess.check_output(['git', 'show', 'HEAD:' + RUN], cwd=R).decode().split('\n'))
final = declared_wiring(open(f'{R}/{RUN}', encoding='utf-8').read().split('\n'))
B = {d['id']: d for d in base}; F = {d['id']: d for d in final}
TOUCHED = ('W49', 'W51', 'W54', 'W54-help')
# §2.6, by meaning: (check, new ordinal) -> (disposition, old check/ordinal or None, must-fail clause)
LEDGER = {
    ('W49', 1): ('kept', ('W49', 1), 'the arm\'s file-wide presence'),
    ('W49', 2): ('kept', ('W49', 2), 'the exact line + 2 / len - 2 arm'),
    ('W49', 3): ('kept', ('W49', 3), 'the handler\'s presence'),
    ('W49', 4): ('kept', ('W49', 4), 'the exact guarded arm'),
    ('W49', 5): ('re-anchored (all-or-nothing)', ('W49', 5), 'ONLY the in-dispatch() clause; one arm remains in the file'),
    ('W49', 6): ('kept', ('W49', 6), 'the guarded arm'),
    ('W51', 1): ('re-anchored', ('W51', 1), 'the three-token ban'),
    ('W51', 2): ('re-anchored', ('W51', 2), 'the three-token ban'),
    ('W51', 3): ('replaced', ('W51', 3), 'the seam-call clause'),
    ('W51', 4): ('new', None, 'the flush clause'),
    ('W54', 1): ('kept', ('W54', 1), 'the include outside a region'),
    ('W54', 2): ('kept', ('W54', 2), 'the binding\'s tokens outside a region'),
    ('W54', 3): ('kept', ('W54', 3), 'the status token outside a region'),
    ('W54', 4): ('kept', ('W54', 5), 'the instance\'s presence'),
    ('W54-help', 1): ('re-anchored from W54', ('W54', 4), 'ONLY the region clause; the literal remains once'),
}
rows, problems = [], []
for d in final:
    for n, s in enumerate(d['scripts'], 1):
        if d['id'] in TOUCHED:
            disp, old, clause = LEDGER[(d['id'], n)]
            old_s = B[old[0]]['scripts'][old[1] - 1] if old else None
            same = old_s == s
            if disp == 'kept' and not same: problems.append(f'{d["id"]}#{n} declared kept but changed')
            if disp != 'kept' and same: problems.append(f'{d["id"]}#{n} declared {disp} but identical')
            rows.append({'check': d['id'], 'new': n, 'old': f'{old[0]}#{old[1]}' if old else '-', 'disposition': disp,
                         'script_identical_to_old': same, 'must_fail': clause})
        else:
            b = B.get(d['id'])
            same = b is not None and n <= len(b['scripts']) and b['scripts'][n - 1] == s
            if not same: problems.append(f'{d["id"]}#{n} changed outside the ledger')
            rows.append({'check': d['id'], 'new': n, 'old': f'{d["id"]}#{n}', 'disposition': 'unchanged',
                         'script_identical_to_old': same, 'must_fail': '(its own check, unchanged)'})
for i, d in F.items():
    if i in TOUCHED: continue
    b = B.get(i)
    if b is None: problems.append(f'{i} is new outside the ledger'); continue
    for k in ('label', 'file', 'pred'):
        if b[k] != d[k]: problems.append(f'{i} {k} changed')
    if len(b['scripts']) != len(d['scripts']): problems.append(f'{i} control count changed')
gone = sorted(set(B) - set(F))
if gone: problems.append(f'checks removed: {gone}')
retired_old = [f'{c}#{k}' for c in ('W49', 'W51', 'W54') for k in range(1, len(B[c]['scripts']) + 1)
               if (c, k) not in {v[1] for v in LEDGER.values() if v[1]}]
summary = {'base_checks': len(base), 'final_checks': len(final), 'new_checks': sorted(set(F) - set(B)),
           'base_declared_controls': sum(len(d['scripts']) for d in base),
           'base_dormant_controls_W49_W51_W54': sum(len(B[c]['scripts']) for c in ('W49', 'W51', 'W54')),
           'final_declared_controls': sum(len(d['scripts']) for d in final),
           'touched_controls': sum(1 for r in rows if r['check'] in TOUCHED),
           'old_controls_without_successor': retired_old,
           'label_changes': {c: [B[c]['label'], F[c]['label']] for c in ('W49', 'W51', 'W54') if B[c]['label'] != F[c]['label']},
           'problems': problems, 'verdict': 'PASS' if not problems else 'FAIL'}
with open(sys.argv[1], 'w') as f:
    f.write('check\tnew_ordinal\told\tdisposition\tscript_identical_to_old\tmust_fail\n')
    for r in rows: f.write(f"{r['check']}\t{r['new']}\t{r['old']}\t{r['disposition']}\t{r['script_identical_to_old']}\t{r['must_fail']}\n")
json.dump({'summary': summary, 'rows': rows}, open(sys.argv[2], 'w'), indent=1, ensure_ascii=False)
print(json.dumps(summary, indent=1, ensure_ascii=False))
