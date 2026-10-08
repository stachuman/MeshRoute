#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 QA (2026-10-07): after the landing, compare the whole tree with QA's start inventory. The only changed paths
may be the five landing documents; the only added paths the QA receipt and its evidence folder; nothing may be missing;
the simulator, HEAD and the staged set must be unchanged. Usage: qa_preservation.py <start.json> <now.json> <out.json>."""
import json, sys

a, b = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
LANDING = {"docs/2026-07-30-open-bug-register.md", "docs/2026-09-20-metal-test-plan.md", "tracker.md", "MEMORY.md",
           "docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md"}
RECEIPT = "docs/superpowers/evidence/2026-10-07-standalone-mobile-home-w7w8-qa"
fa, fb = a["meshroute"]["files"], b["meshroute"]["files"]
changed = sorted(k for k in set(fa) & set(fb) if fa[k] != fb[k])
added = sorted(set(fb) - set(fa))
missing = sorted(set(fa) - set(fb))
sim_same = a["simulator"]["files"] == b["simulator"]["files"] and a["simulator"]["head"] == b["simulator"]["head"]
out = {"changed": changed, "added": added, "missing": missing,
       "unexplained_changed": [k for k in changed if k not in LANDING],
       "unexplained_added": [k for k in added if not (k == RECEIPT + ".md" or k.startswith(RECEIPT + "/"))],
       "landing_documents_changed": sorted(set(changed) & LANDING),
       "head_same": a["meshroute"]["head"] == b["meshroute"]["head"], "staged": b["meshroute"]["staged"],
       "simulator_unchanged": sim_same}
ok = (not out["unexplained_changed"] and not out["unexplained_added"] and not missing and out["head_same"]
      and not out["staged"] and sim_same)
out["verdict"] = "PASS" if ok else "FAIL"
json.dump(out, open(sys.argv[3], "w"), indent=1)
print(f"changed {len(changed)} (landing {len(out['landing_documents_changed'])}), added {len(added)}, missing "
      f"{len(missing)}, unexplained {out['unexplained_changed'] + out['unexplained_added']} -> {out['verdict']}")
sys.exit(0 if ok else 1)
