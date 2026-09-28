# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 §4.1 final step 6 — THE INDEPENDENT RECONCILIATION, once. Neither side uses the implementation's census or its
# comparator:
#   · EXPECTED: the pre-change identity set (derive_prechange.py: the pre-check's reconciliation + the BASE sources,
#     P4a's ten executions keyed P4a/0…/9) — and, separately, the checked-in manifest, which must equal it;
#   · OBSERVED (a): every terminal observation the runner RECORDED, read from an xtrace of the stock default run
#     (PS4='+${FUNCNAME[0]:-main}@${LINENO}|', BASH_XTRACEFD) at the recorder's own printf;
#   · OBSERVED (b): what EXECUTED, read from the same trace and the run's output by W2's method, never through the
#     recorder: each wiring check's invocation and each control's sed + mutant predicate; each trait / missing-trait /
#     structural call; each negctl label printed with its `->` line in its arm's section; and the two canvas tallies.
# It shows: (a) == manifest == pre-change set; (b) agrees with (a) layer by layer; every outcome is the accepted one;
# and brief §2.2.4 — every live wiring predicate exited 0, every mutant evaluation exited 1.
# Usage: python3 reconcile_b459.py <trace> <run.log> <expected.tsv> <prechange.json> <out.json>
import json, re, shlex, sys
TRACE, LOG, MAN, PRE, OUT = sys.argv[1:6]
trace = open(TRACE, encoding='utf-8', errors='surrogateescape').read().split('\n')
log = open(LOG, encoding='utf-8', errors='surrogateescape').read().split('\n')
problems = []
manifest = [tuple(l.split('\t')[:2]) for l in open(MAN, encoding='utf-8').read().split('\n') if l and not l.startswith('#')]
pre = [tuple(x) for x in json.load(open(PRE))['identities']]

# ---- (a) the recorded observations, at the recorder's printf
recorded = []
for t in trace:
    m = re.match(r"^\+pa_record@\d+\|printf '%s\\t%s(?:\\t%s)?\\n' (.*)$", t)
    if m:
        a = shlex.split(m.group(1))
        recorded.append((a[0], a[1], a[2] if len(a) > 2 else ''))
# only the run's observation ledger is written by pa_record; the census and the comparator never call it
rec_ids = [r[0] for r in recorded]
dups = sorted({i for i in rec_ids if rec_ids.count(i) > 1})
if dups: problems.append(f'recorded twice: {dups[:10]}')

# ---- (b) execution evidence, not the recorder
ex = {'wiring': [], 'wiring/control': [], 'trait': [], 'missing': [], 'struct': [], 'negctl/v3': [], 'negctl/v4': []}
cur, n = None, 0
for t in trace:
    m = re.match(r'^\+wchk_in@\d+\|local file=(.*)$', t)
    if m:
        kv = dict(x.split('=', 1) for x in shlex.split('file=' + m.group(1)))
        cur = 'wiring/' + kv['label'].split(' ', 1)[0]; n = 0; ex['wiring'].append(cur); continue
    if cur and re.match(r'^\+wchk_in@\d+\|sed ', t):
        n += 1; ex['wiring/control'].append(f'{cur}/control/{n}'); continue
    m = re.match(r'^\+main@\d+\|trait_control (.*)$', t) or re.match(r'^\+\w+@\d+\|trait_control (.*)$', t)
    if m and not t.startswith('+trait_control@'):
        ex['trait'].append('trait/' + shlex.split(m.group(1))[0].split(' ', 1)[0]); continue
    m = re.match(r'^\+main@\d+\|missing_trait_control (\S+)$', t)
    if m: ex['missing'].append('missing/' + m.group(1)); continue
    m = re.match(r'^\+main@\d+\|schk (.*)$', t)
    if m: ex['struct'].append('struct/' + shlex.split(m.group(1))[0]); continue
for arm, hdr in (('v3', '== negative controls (each MUST fail) =='), ('v4', '== V4 fixed-ADC source controls (each MUST fail) ==')):
    i0 = log.index(hdr); i1 = log.index('== V4 fixed-ADC source controls (each MUST fail) ==') if arm == 'v3' else len(log)
    sec = log[i0:i1]
    for k, l in enumerate(sec[:-1]):
        if re.match(r'^C\d+[a-z0-9]* ', l) and re.match(r'^\s+(->|!!)', sec[k + 1]):   # C9c2 / C9l2 carry digits too
            ex['negctl/' + arm].append(f'negctl/{arm}/' + l.split(' ', 1)[0])
tally = {a: re.search(rf'^Heltec {a.upper()} board_ui probe: (\d+) passed / (\d+) failed / (\d+) total$', '\n'.join(log), re.M)
         for a in ('v3', 'v4')}
# a structural failure branch (S1 could not compile) records without calling schk; include it from the log
if any('FAIL S1 could not compile' in l for l in log): ex['struct'].append('struct/S1')

def ns(i):
    p = i.split('/')
    if p[0] == 'wiring': return 'wiring/control' if '/control/' in i else 'wiring'
    return f'{p[0]}/{p[1]}' if p[0] in ('canvas', 'negctl') else p[0]
rec_by_ns = {}
for i, o, d in recorded: rec_by_ns.setdefault(ns(i), []).append(i)
layer = {}
for k, lst in ex.items():
    same = sorted(lst) == sorted(rec_by_ns.get(k, []))
    layer[k] = {'executed': len(lst), 'recorded': len(rec_by_ns.get(k, [])), 'same_set': same}
    if not same: problems.append(f'{k}: executed and recorded identities differ')
for a in ('v3', 'v4'):
    t = tally[a]; r = len(rec_by_ns.get(f'canvas/{a}', []))
    layer[f'canvas/{a}'] = {'tally_total': int(t.group(3)) if t else None, 'tally_failed': int(t.group(2)) if t else None, 'recorded': r,
                            'same_count': bool(t) and int(t.group(3)) == r}
    if not layer[f'canvas/{a}']['same_count']: problems.append(f'canvas/{a}: tally and records differ')

# ---- the three sets
man_ids, pre_ids, rec_set = [i for i, _ in manifest], [i for i, _ in pre], sorted(set(rec_ids))
res = {'manifest': len(man_ids), 'prechange': len(pre_ids), 'recorded': len(recorded),
       'manifest_equals_prechange': sorted(manifest) == sorted(pre),
       'recorded_equals_manifest': rec_set == sorted(man_ids) and not dups,
       'missing_from_record': sorted(set(man_ids) - set(rec_ids)), 'unknown_in_record': sorted(set(rec_ids) - set(man_ids))}
if not res['manifest_equals_prechange']: problems.append('manifest != pre-change identity set')
if not res['recorded_equals_manifest']: problems.append('recorded observations != manifest')
accepted = dict(manifest)
wrong = [(i, o) for i, o, d in recorded if accepted.get(i) != o]
res['wrong_outcomes'] = wrong
if wrong: problems.append(f'{len(wrong)} observation(s) with an outcome other than the accepted one')
# ---- brief §2.2.4: exact predicate exits
live = [d for i, o, d in recorded if ns(i) == 'wiring']; mut = [d for i, o, d in recorded if ns(i) == 'wiring/control']
res['exits'] = {'live': {x: live.count(x) for x in sorted(set(live))}, 'mutant': {x: mut.count(x) for x in sorted(set(mut))}}
if set(live) != {'pred_exit=0'} or set(mut) != {'pred_exit=1'}: problems.append('a predicate exit other than live 0 / mutant 1')
negkinds = {}
for i, o, d in recorded:
    if i.startswith('negctl/'): negkinds[f"{i.split('/')[1]} {d}"] = negkinds.get(f"{i.split('/')[1]} {d}", 0) + 1
res.update(layers=layer, negctl_kinds=negkinds, problems=problems, verdict='PASS' if not problems else 'FAIL')
json.dump(res, open(OUT, 'w'), indent=1)
counts = {}
for i in rec_ids: counts[ns(i)] = counts.get(ns(i), 0) + 1
print('recorded per namespace:', counts)
print(f"manifest {res['manifest']} == pre-change {res['prechange']}: {res['manifest_equals_prechange']}; recorded {res['recorded']} == manifest: {res['recorded_equals_manifest']}")
print('layers (executed vs recorded):', {k: (v.get('executed', v.get('tally_total')), v['recorded']) for k, v in layer.items()})
print('exits:', res['exits'], '| negctl kinds:', negkinds)
print('RECONCILIATION', res['verdict'], problems)
