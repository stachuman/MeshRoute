# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W2 §4.1 step 4 — the DECLARED-VERSUS-EXECUTED reconciliation, independent of the runner's own summary lines (B459 is
# open: the runner counts only what happens to run). The DECLARED side is read from the runner SOURCE (and negctl.py's
# two control lists); the EXECUTED side from an xtrace of the STOCK runner (PS4='+${FUNCNAME[0]:-main}@${LINENO}|',
# BASH_XTRACEFD) plus that run's own output. Every declared item must match exactly ONE executed result; nothing may be
# missing, duplicated or unknown.
# Usage: python3 reconcile.py <run.sh> <negctl.py> <trace.txt> <traced-run.log> <out.json> <out.tsv>
import ast, json, os, re, shlex, subprocess, sys
sys.dont_write_bytecode = True   # no __pycache__ inside the evidence folder
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from declared import declared_wiring
RUN, NEG, TRACE, LOG, OUTJ, OUTT = sys.argv[1:7]
src = open(RUN, encoding='utf-8').read().split('\n')
trace = open(TRACE, encoding='utf-8', errors='surrogateescape').read().split('\n')
log = open(LOG, encoding='utf-8', errors='surrogateescape').read().split('\n')
problems = []

# ---------------------------------------------------------------- declared: the runner source's wiring calls
declared = declared_wiring(src)
ids = [d['id'] for d in declared]
dups = sorted({i for i in ids if ids.count(i) > 1})
if dups: problems.append(f'declared duplicate wiring IDs {dups}')

# ---------------------------------------------------------------- executed: wchk_in invocations in the trace
execd, cur = [], None
for t in trace:
    m = re.match(r'^\+wchk_in@\d+\|(.*)$', t)
    if not m: continue
    cmd = m.group(1)
    if cmd.startswith('local file='):
        toks = shlex.split(cmd[len('local '):])
        kv = dict(x.split('=', 1) for x in toks)
        cur = {'id': kv['label'].split(' ', 1)[0], 'label': kv['label'], 'pred': kv['pred'],
               'file': kv['file'], 'controls': [], 'passed': False, 'failed': False}
        execd.append(cur)
    elif cur is None:
        continue
    elif cmd.startswith('sed '):
        toks = shlex.split(cmd)
        cur['controls'].append({'script': toks[1], 'red': False})
    elif cmd.startswith('w_ctl='):
        cur['controls'][-1]['red'] = True
    elif cmd.startswith('w_pass='):
        cur['passed'] = True
    elif cmd.startswith('w_fail='):
        cur['failed'] = True
if any("$'" in t for t in trace if t.startswith('+wchk_in@')):
    problems.append("ANSI-C quoting in a wchk_in trace line (shlex cannot read it)")

rows = []
by_id = {}
for e in execd: by_id.setdefault(e['id'], []).append(e)
for d in declared:
    hits = by_id.get(d['id'], [])
    row = {'kind': 'wiring', 'id': d['id'], 'source_line': d['line'], 'declared_controls': len(d['scripts']),
           'executed': len(hits)}
    if len(hits) != 1:
        problems.append(f"wiring {d['id']}: executed {len(hits)} times"); row['verdict'] = 'MISMATCH'; rows.append(row); continue
    e = hits[0]
    same_scripts = [c['script'] for c in e['controls']] == d['scripts']
    red = sum(c['red'] for c in e['controls'])
    row.update({'executed_controls': len(e['controls']), 'controls_red': red, 'scripts_identical': same_scripts,
                'pred': e['pred'], 'check_passed': e['passed'] and not e['failed']})
    ok = same_scripts and red == len(d['scripts']) and e['passed'] and not e['failed'] and e['pred'] == d['pred']
    if not ok: problems.append(f"wiring {d['id']}: {row}")
    row['verdict'] = 'OK' if ok else 'MISMATCH'
    rows.append(row)
unknown = sorted(set(by_id) - set(ids))
if unknown: problems.append(f'executed wiring IDs never declared {unknown}')
last_ctl = max((int(t.split('w_ctl=')[1]) for t in trace if t.startswith('+wchk_in@') and '|w_ctl=' in t), default=0)
last_pass = max((int(t.split('w_pass=')[1]) for t in trace if t.startswith('+wchk_in@') and '|w_pass=' in t), default=0)

# ---------------------------------------------------------------- structural + trait + missing-trait call sites
def loop_words(site_line):
    """-> the word count of the literal `for X in ...; do` enclosing a call site (None if not in a loop)."""
    depth_open = None
    for k in range(site_line - 1, -1, -1):
        if re.match(r'^done\b', src[k]): return None
        if re.match(r'^for \w+ in ', src[k]): depth_open = k; break
    if depth_open is None: return None
    hdr, k = src[depth_open], depth_open
    while not re.search(r';\s*do\s*$', hdr):
        k += 1; hdr = hdr.rstrip('\\') + ' ' + src[k]
    words = re.sub(r'^for \w+ in ', '', hdr); words = re.sub(r';\s*do\s*$', '', words)
    assert '$' not in words, words
    r = subprocess.run(['bash', '--norc', '-c', 'set -f; set -- ' + words + '; echo $#'], capture_output=True, text=True)
    return int(r.stdout)
for fn, kind in (('schk', 'structural'), ('trait_control', 'trait'), ('missing_trait_control', 'missing')):
    sites = [i + 1 for i, l in enumerate(src) if re.match(r'^\s*%s ' % fn, l)]
    for s in sites:
        want = loop_words(s) or 1
        got = [t for t in trace if re.match(r'^\+main@%d\|%s ' % (s, fn), t)]
        labels = [shlex.split(t.split('|', 1)[1])[1] for t in got]
        ok = len(got) == want and len(set(labels)) == len(labels)
        rows.append({'kind': kind, 'id': labels[0].split(' ', 1)[0] if labels else '?', 'source_line': s,
                     'declared': want, 'executed': len(got), 'distinct_labels': len(set(labels)), 'verdict': 'OK' if ok else 'MISMATCH'})
        if not ok: problems.append(f'{kind} site {s}: declared {want}, executed {len(got)}')
s_pass = sum(1 for t in trace if re.match(r'^\+schk@\d+\|s_pass=', t)); s_fail = sum(1 for t in trace if re.match(r'^\+schk@\d+\|s_fail=', t))
if s_fail: problems.append(f'structural failures {s_fail}')

# ---------------------------------------------------------------- negctl: MUT_V3 / MUT_V4 labels vs the run's output
nt = ast.parse(open(NEG, encoding='utf-8').read())
decl_neg = {}
for n in ast.walk(nt):
    if isinstance(n, ast.Assign) and len(n.targets) == 1 and getattr(n.targets[0], 'id', '') in ('MUT_V3', 'MUT_V4'):
        decl_neg[n.targets[0].id] = [ast.literal_eval(e.elts[0]) for e in n.value.elts]
i3 = log.index('== negative controls (each MUST fail) ==')
i4 = log.index('== V4 fixed-ADC source controls (each MUST fail) ==')
sections = {'MUT_V3': log[i3:i4], 'MUT_V4': log[i4:]}
for arm, labels in decl_neg.items():
    sec = sections[arm]
    for lab in labels:
        pos = [k for k, l in enumerate(sec) if l == lab]
        red = len(pos) == 1 and pos[0] + 1 < len(sec) and re.match(r'^\s+-> \d+ check\(s\) fail', sec[pos[0] + 1]) is not None
        rows.append({'kind': 'negctl-' + arm[-2:].lower(), 'id': lab.split(' ', 1)[0], 'declared': 1, 'executed': len(pos),
                     'red': red, 'verdict': 'OK' if red else 'MISMATCH'})
        if not red: problems.append(f'negctl {arm} {lab!r}: executed {len(pos)}, red {red}')

summary = {
    'wiring': {'declared_checks': len(declared), 'executed_checks': len(execd), 'passed': sum(r.get('check_passed', False) for r in rows if r['kind'] == 'wiring'),
               'declared_controls': sum(len(d['scripts']) for d in declared), 'executed_controls': sum(len(e['controls']) for e in execd),
               'controls_red': sum(sum(c['red'] for c in e['controls']) for e in execd),
               'trace_final_w_ctl': last_ctl, 'trace_final_w_pass': last_pass},
    'structural': {'declared': sum(r['declared'] for r in rows if r['kind'] == 'structural'), 'executed': sum(r['executed'] for r in rows if r['kind'] == 'structural'), 's_pass': s_pass, 's_fail': s_fail},
    'trait': {'declared': sum(r['declared'] for r in rows if r['kind'] == 'trait'), 'executed': sum(r['executed'] for r in rows if r['kind'] == 'trait')},
    'missing': {'declared': sum(r['declared'] for r in rows if r['kind'] == 'missing'), 'executed': sum(r['executed'] for r in rows if r['kind'] == 'missing')},
    'negctl_v3': {'declared': len(decl_neg['MUT_V3']), 'executed_red': sum(r['red'] for r in rows if r['kind'] == 'negctl-v3')},
    'negctl_v4': {'declared': len(decl_neg['MUT_V4']), 'executed_red': sum(r['red'] for r in rows if r['kind'] == 'negctl-v4')},
    'problems': problems, 'verdict': 'PASS' if not problems else 'FAIL'}
json.dump({'summary': summary, 'rows': rows}, open(OUTJ, 'w'), indent=1, ensure_ascii=False)
with open(OUTT, 'w') as f:
    f.write('kind\tid\tsource_line\tdeclared\texecuted\tdeclared_controls\texecuted_controls\tcontrols_red\tscripts_identical\tverdict\n')
    for r in rows:
        f.write('\t'.join(str(r.get(k, '')) for k in ('kind', 'id', 'source_line', 'declared', 'executed', 'declared_controls',
                                                        'executed_controls', 'controls_red', 'scripts_identical', 'verdict')) + '\n')
print(json.dumps(summary, indent=1))
