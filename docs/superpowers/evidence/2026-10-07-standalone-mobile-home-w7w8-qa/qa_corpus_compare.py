#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 QA (2026-10-07): QA's fresh corpus manifest against a reference manifest, per stream, on the union of every
per-stream field either side carries; the scenario sets must be equal, and the lus SHA-256 must be the stock one.
Usage: qa_corpus_compare.py <reference.json> <qa.json> <out.json> [<expected lus sha256>]."""
import json, sys

ref, qa = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
want_lus = sys.argv[4] if len(sys.argv) > 4 else None
r = {s["name"]: s for s in ref["scenarios"]}
q = {s["name"]: s for s in qa["scenarios"]}
fields = sorted({k for s in ref["scenarios"] + qa["scenarios"] for k in s})
rows = []
for name in sorted(set(r) | set(q)):
    if name not in r or name not in q:
        rows.append({"name": name, "identical": False, "missing_on": "reference" if name not in r else "qa"})
        continue
    d = [f for f in fields if r[name].get(f) != q[name].get(f)]
    rows.append({"name": name, "identical": not d, "differs_in": d, "events": q[name].get("events"),
                 "output_md5": q[name].get("output_md5"), "anchor_match": q[name].get("anchor_match")})
s18 = q.get("s18_meshroute", {})
out = {"fields_compared": fields, "streams": len(rows), "identical": sum(x["identical"] for x in rows),
       "scenario_set_equal": set(r) == set(q), "lus_sha256": qa.get("lus_sha256"),
       "lus_is_stock": (want_lus is None or qa.get("lus_sha256") == want_lus),
       "s18": {"events": s18.get("events"), "output_md5": s18.get("output_md5")}, "rows": rows}
out["verdict"] = "PASS" if (out["identical"] == out["streams"] == len(r) and out["scenario_set_equal"]
                            and out["lus_is_stock"]) else "FAIL"
json.dump(out, open(sys.argv[3], "w"), indent=1)
print(f"corpus: {out['identical']}/{out['streams']} identical on {len(fields)} fields; set equal "
      f"{out['scenario_set_equal']}; lus stock {out['lus_is_stock']}; s18 {out['s18']} -> {out['verdict']}")
sys.exit(0 if out["verdict"] == "PASS" else 1)
