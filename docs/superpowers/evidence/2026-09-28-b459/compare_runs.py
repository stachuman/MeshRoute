# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 §2.1 — the refactor's proof, two comparisons:
#   fwui  <baseline.log> <candidate.log> <out.json> — the firmware-UI stock default run, line by line. The only
#         normalisations are temporary paths (`/tmp/tmp.XXXXXXXXXX`) and the shell's `real/user/sys` timing lines.
#   b456  <baseline.json> <candidate.json> <out.json> — every B456 self-test case (capture_b456.py): same cases, same
#         exit status, byte-identical output.
import difflib, json, re, sys
mode, a, b, out = sys.argv[1:5]
res = {}
if mode == 'fwui':
    def norm(p):
        lines = open(p, encoding='utf-8', errors='surrogateescape').read().split('\n')
        lines = [l for l in lines if not re.match(r'^(real|user|sys)\t', l)]
        return [re.sub(r'/tmp/tmp\.[A-Za-z0-9]+', '/tmp/tmp.X', l) for l in lines]
    x, y = norm(a), norm(b)
    diff = list(difflib.unified_diff(x, y, 'baseline', 'candidate', lineterm='', n=1))
    res = {'baseline_lines': len(x), 'candidate_lines': len(y), 'identical_after_normalisation': x == y,
           'diff': diff[:200], 'tmp_paths_normalised': sum(1 for l in open(a, errors='surrogateescape') if '/tmp/tmp.' in l)}
    print(f"fw-UI default: {len(x)} vs {len(y)} lines; identical after normalising temp paths and timings: {x == y}")
elif mode == 'b456':
    x, y = json.load(open(a)), json.load(open(b))
    key = lambda c: (c['test'], c['call'])
    xa, ya = {key(c): c for c in x['calls']}, {key(c): c for c in y['calls']}
    cases = []
    for k in sorted(set(xa) | set(ya)):
        c0, c1 = xa.get(k), ya.get(k)
        same = bool(c0 and c1) and all(c0[f] == c1[f] for f in ('runner', 'no_neg', 'rc', 'out'))
        cases.append({'test': k[0], 'call': k[1], 'rc': [c0 and c0['rc'], c1 and c1['rc']], 'identical': same})
    res = {'baseline': {f: x[f] for f in ('tests_run', 'failures', 'errors', 'skipped', 'ok', 'n_calls')},
           'candidate': {f: y[f] for f in ('tests_run', 'failures', 'errors', 'skipped', 'ok', 'n_calls')},
           'cases': cases, 'all_identical': all(c['identical'] for c in cases) and len(xa) == len(ya)}
    print(f"B456: baseline {res['baseline']}, candidate {res['candidate']}; {len(cases)} captured calls, "
          f"all exit statuses and outputs byte-identical: {res['all_identical']}")
json.dump(res, open(out, 'w'), indent=1)
