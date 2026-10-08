#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 QA (2026-10-07): the two inventories taken at the start and end of the QA chain must be identical — every
path, every hash, both HEADs and both status digests. Usage: qa_stability.py <start.json> <end.json> <out.json>."""
import json, sys

a, b = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
diff = {}
for repo in ("meshroute", "simulator"):
    fa, fb = a[repo]["files"], b[repo]["files"]
    diff[repo] = {
        "changed": sorted(k for k in set(fa) & set(fb) if fa[k] != fb[k]),
        "added": sorted(set(fb) - set(fa)),
        "removed": sorted(set(fa) - set(fb)),
        "head_equal": a[repo]["head"] == b[repo]["head"],
        "status_equal": a[repo]["status_sha256"] == b[repo]["status_sha256"],
        "files": len(fb),
    }
ok = all(not d["changed"] and not d["added"] and not d["removed"] and d["head_equal"] and d["status_equal"]
         for d in diff.values())
out = {"diff": diff, "verdict": "PASS" if ok else "FAIL"}
json.dump(out, open(sys.argv[3], "w"), indent=1)
print(f"stability: meshroute {diff['meshroute']['files']} files, simulator {diff['simulator']['files']} files -> "
      f"{out['verdict']}")
sys.exit(0 if ok else 1)
