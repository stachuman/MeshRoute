# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 — an INDEPENDENT label-completeness count for a firmware-UI default run: the `ctl` labels DECLARED in the runner
# source (the B456 test's own strict call regex, not the runner's extractor) against the per-control verdict lines
# the run printed (`  ok   <label> -> RED (…)` or `  ok   <label> (build fails, as required)`).
# Usage: python3 fwui_labels.py <run.sh> <run.log> <out.json>
import json, re, sys
src = open(sys.argv[1], encoding='utf-8').read().splitlines()
calls = sum(1 for l in src if re.match(r'^[ \t]*ctl "', l))
decl = [m.group(1) for m in (re.match(r'^[ \t]*ctl "(.*)" (?:yes|no) \\$', l) for l in src) if m]
decl = [re.sub(r'\\([\\"$`])', r'\1', d) for d in decl]          # the label as the shell delivers it
log = open(sys.argv[2], encoding='utf-8', errors='surrogateescape').read().splitlines()
seen = []
for l in log:
    m = re.match(r'^  ok   (.*) -> RED \(\d+ check\(s\) failed\)$', l) or re.match(r'^  ok   (.*) \(build fails, as required\)$', l)
    if m: seen.append(m.group(1))
fails = [l for l in log if re.match(r'^  FAIL ', l)]
d, s = set(decl), set(seen)
res = {'declared_calls': calls, 'declared_labels': len(decl), 'distinct_declared': len(d), 'verdict_lines': len(seen),
       'distinct_seen': len(s), 'missing': sorted(d - s), 'unknown': sorted(s - d),
       'duplicates': sorted({x for x in seen if seen.count(x) > 1}), 'fail_lines': len(fails)}
res['verdict'] = 'PASS' if (calls == len(decl) == len(d) == len(seen) == len(s) and not res['missing'] and not res['unknown']
                            and not res['duplicates']) else 'FAIL'
json.dump(res, open(sys.argv[3], 'w'), indent=1)
print(f"declared ctl calls {calls}, labels {len(decl)} ({len(d)} distinct); verdict lines {len(seen)} ({len(s)} distinct); "
      f"missing {len(res['missing'])}, unknown {len(res['unknown'])}, duplicates {len(res['duplicates'])}; FAIL lines {len(fails)} -> {res['verdict']}")
