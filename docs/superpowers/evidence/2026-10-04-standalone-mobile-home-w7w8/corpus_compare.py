#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 §4.1 final step 3: per-stream identity of the fresh corpus manifest against baseline step 3's
(`receipts/corpus-manifest-base.json`). Every stream must match on every per-stream field; the scenario set must be
identical. Usage: python3 -B corpus_compare.py <base.json> <final.json> <out.json>. Exit 1 on any difference."""
import json, sys

base, new = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
b = {s["name"]: s for s in base["scenarios"]}
n = {s["name"]: s for s in new["scenarios"]}
fields = sorted({k for s in base["scenarios"] for k in s} | {k for s in new["scenarios"] for k in s})
rows, bad = [], 0
if set(b) != set(n):
    bad += 1
for name in sorted(set(b) & set(n)):
    diffs = [f for f in fields if b[name].get(f) != n[name].get(f)]
    rows.append({"name": name, "identical": not diffs, "differs_in": diffs, "events": n[name].get("events"),
                 "output_md5": n[name].get("output_md5")})
    bad += bool(diffs)
out = {"fields_compared": fields, "streams": len(rows), "identical": sum(r["identical"] for r in rows),
       "scenario_set_equal": set(b) == set(n), "rows": rows, "verdict": "PASS" if not bad else "FAIL"}
json.dump(out, open(sys.argv[3], "w"), indent=1)
print(f"corpus: {out['identical']}/{out['streams']} streams identical on {len(fields)} per-stream fields; "
      f"scenario set equal: {out['scenario_set_equal']} -> {out['verdict']}")
sys.exit(1 if bad else 0)
