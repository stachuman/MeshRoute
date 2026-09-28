#!/usr/bin/env python3
# Corpus comparison (brief §4.1 step 3): every scenario of the new manifest against the pre-check manifest, FIELD BY
# FIELD over the stream-identity fields; run-specific fields (timing, host, paths, lus hash) are reported, not compared.
import json, sys
ref = json.load(open(sys.argv[1])); new = json.load(open(sys.argv[2]))
FIELDS = ('anchor_assertion_failures', 'anchor_events', 'anchor_match', 'anchor_md5_prefix', 'assertion_failures',
          'events', 'exit_status', 'output_bytes', 'output_md5', 'output_sha256', 'promoted', 'scenario_path',
          'scenario_sha256')
r = {s['name']: s for s in ref['scenarios']}; n = {s['name']: s for s in new['scenarios']}
bad = 0
for name in sorted(set(r) | set(n)):
    if name not in r or name not in n: print(f'MISSING {name}'); bad += 1; continue
    d = [f for f in FIELDS if r[name].get(f) != n[name].get(f)]
    extra = sorted(set(n[name]) - set(FIELDS) - {'name'})
    if d: bad += 1; print(f'DIFF {name}: ' + ', '.join(f'{f} {r[name].get(f)} -> {n[name].get(f)}' for f in d))
print(f'scenarios: ref {len(r)}, new {len(n)}; identical on {len(FIELDS)} fields: {len(r) - bad if bad <= len(r) else 0}; differing: {bad}')
print(f'scenario_count {ref.get("scenario_count")} -> {new.get("scenario_count")}; baseline_sha256 equal: {ref.get("baseline_sha256") == new.get("baseline_sha256")}; inputs_stable {new.get("inputs_stable")}')
sys.exit(1 if bad else 0)
