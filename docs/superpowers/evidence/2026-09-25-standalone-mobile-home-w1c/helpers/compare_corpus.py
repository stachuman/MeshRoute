#!/usr/bin/env python3
"""W1: per-stream byte-identity of a fresh corpus manifest against the QA pre-check's corpus-manifest.json
(brief §2.6 / §4.1 step 3). Every stream must match on full MD5, full SHA-256, byte count, event count, exit status and
assertion failures; the scenario set must be identical. Exit 1 on any difference."""
import json, sys

base = json.load(open(sys.argv[1]))
new = json.load(open(sys.argv[2]))
fields = ("output_md5", "output_sha256", "output_bytes", "events", "exit_status", "assertion_failures",
          "anchor_match", "scenario_sha256")
b = {s["name"]: s for s in base["scenarios"]}
n = {s["name"]: s for s in new["scenarios"]}
bad = 0
if set(b) != set(n):
    print("SCENARIO SET DIFFERS:", sorted(set(b) ^ set(n))); bad += 1
for name in sorted(set(b) & set(n)):
    diffs = [f for f in fields if b[name].get(f) != n[name].get(f)]
    print(f"{'IDENTICAL' if not diffs else 'DELTA    '} {name:48s} md5={n[name].get('output_md5')} "
          f"sha256={n[name].get('output_sha256')[:16]}… events={n[name].get('events')}"
          + ("" if not diffs else f"  differs in {diffs}"))
    bad += bool(diffs)
print(f"streams compared: {len(set(b) & set(n))}; identical: {len(set(b) & set(n)) - bad}; deltas: {bad}")
print(f"new manifest: scenario_count={new.get('scenario_count')} inputs_stable={new.get('inputs_stable')} "
      f"source.git_head={new.get('source', {}).get('git_head')}")
sys.exit(1 if bad else 0)
