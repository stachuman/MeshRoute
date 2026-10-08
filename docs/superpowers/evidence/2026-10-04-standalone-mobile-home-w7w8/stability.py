#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 §4.1 final step 13: the frozen inputs at the START and the END of the chain must be byte-identical — every
fence and new-fence file, every read-only input, every preparation input, the brief, HEAD, the simulator, the staged
set and the inventory's changed/new hashes. Usage: python3 -B stability.py <scope-start.json> <scope-end.json> <out.json>."""
import json, sys

a, b = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
keys = ["head", "simulator", "staged", "brief", "fence", "new_fence", "readonly", "preparation", "git_diff_check"]
diff = [k for k in keys if a.get(k) != b.get(k)]
inv = ("changed", "new_hashes", "unexplained_changed", "unexplained_new", "missing")
for side in ("meshroute", "simulator"):
    for k in inv:
        if a["inventory"][side].get(k) != b["inventory"][side].get(k):
            diff.append(f"inventory.{side}.{k}")
out = {"compared": keys + [f"inventory.*.{k}" for k in inv], "differences": diff,
       "start_verdict": a["verdict"], "end_verdict": b["verdict"],
       "fence": {p: v["sha256"] for p, v in b["fence"].items()},
       "new_fence": {p: (v or {}).get("sha256") for p, v in b["new_fence"].items()},
       "verdict": "PASS" if not diff and a["verdict"] == b["verdict"] == "PASS" else "FAIL"}
json.dump(out, open(sys.argv[3], "w"), indent=1, sort_keys=True)
print(f"stability: {len(out['compared'])} groups compared; differences {diff}; scope start {a['verdict']} / end "
      f"{b['verdict']} -> {out['verdict']}")
sys.exit(0 if out["verdict"] == "PASS" else 1)
