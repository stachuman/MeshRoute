#!/usr/bin/env python3
"""Independent control-completeness count for tools/probe_firmware_ui/run.sh (NOT the runner's own guard).
usage: completeness.py <run.sh> <log>. Declared labels = B227's own extractor regex over the runner's source;
observed = terminal verdict lines in the log (`  ok   <label> -> RED …`, `  ok   <label> (build fails…)`, `  FAIL <label> — …`)."""
import re, sys, collections
src, log = sys.argv[1], sys.argv[2]
declared = [m.group(1) for m in (re.match(r'^[ \t]*ctl "(.*)" (?:yes|no) \\$', l) for l in open(src, encoding='utf-8')) if m]
# the label as the SHELL delivers it: inside double quotes, `\\"`, `\\\\`, `\\$` and `\\`` lose their backslash
declared = [re.sub(r'\\(["\\$`])', r'\1', l) for l in declared]
calls = sum(1 for l in open(src, encoding='utf-8') if re.match(r'^[ \t]*ctl "', l))
obs = collections.Counter(); unknown = []
lines = open(log, encoding='utf-8', errors='replace').read().splitlines()
dset = set(declared)
for l in lines:
    m = re.match(r'^  (ok|FAIL) +(.*)$', l)
    if not m: continue
    body = m.group(2)
    hit = None
    for lab in dset:
        if body == lab or body.startswith(lab + ' -> RED (') or body.startswith(lab + ' (build fails') or body.startswith(lab + ' — '):
            if hit is None or len(lab) > len(hit): hit = lab
    if hit: obs[hit] += 1
dup_decl = [k for k, v in collections.Counter(declared).items() if v > 1]
missing = [l for l in declared if obs[l] == 0]
multi = [l for l in declared if obs[l] > 1]
fails = [l for l in lines if l.startswith('  FAIL ')]
nf = sum(1 for l in lines if 'command not found' in l)
print(f"declared labels={len(declared)} unique={len(dset)} ctl-call-lines={calls} duplicates-in-source={dup_decl}")
print(f"observed exactly once={sum(1 for l in declared if obs[l]==1)} missing={len(missing)} more-than-once={len(multi)}")
print(f"FAIL lines={len(fails)} command-not-found={nf}")
for l in missing: print("  MISSING", l)
for l in multi: print("  MULTI", obs[l], l)
for l in fails[:20]: print("  ", l[:200])
ok = len(declared) == calls and not dup_decl and not missing and not multi and nf == 0
print("COMPLETE" if ok else "INCOMPLETE")
sys.exit(0 if ok else 1)
