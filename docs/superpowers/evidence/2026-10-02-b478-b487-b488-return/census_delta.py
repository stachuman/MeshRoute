# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# §4.1 step 10 (return): the static census is identical to the base EXCEPT F07's replacement string — compared ENTRY BY
# ENTRY on the AST literals (the harness is parsed, never imported) of the base (10f3332) and the working tree; and
# F07's pattern still matches the real target exactly once. Usage: python3 -B census_delta.py <out.json>
import ast, json, subprocess, sys, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
def tables(source):
    out = {}
    for n in ast.parse(source).body:
        if isinstance(n, ast.Assign):
            for t in n.targets:
                names = [t] if isinstance(t, ast.Name) else list(getattr(t, "elts", []))
                for i, nm in enumerate(names):
                    if isinstance(nm, ast.Name) and (nm.id.startswith(("MUTS_", "PIN_", "_WORKERS")) or nm.id == "TARGET_SRC"):
                        val = n.value if isinstance(t, ast.Name) else n.value.elts[i]
                        try:
                            out[nm.id] = ast.literal_eval(val)
                        except ValueError:
                            out[nm.id] = ast.dump(val)          # MUTS_BY_TARGET / worker formulas: compared structurally
    return out
base = tables(subprocess.check_output(["git", "show", "10f333298ffad1098daeec37e756ed79ab0fd97f:tools/probe_ui_model_mutations.py"], cwd=ROOT))
now = tables(open(os.path.join(ROOT, "tools/probe_ui_model_mutations.py"), "rb").read())
diffs = []
for k in sorted(set(base) | set(now)):
    a, b = base.get(k), now.get(k)
    if a == b:
        continue
    if isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                for j, (u, v) in enumerate(zip(x, y)):
                    if u != v:
                        diffs.append({"table": k, "index": i, "id": x[0].split()[0], "field": ["label", "pattern", "replacement"][j],
                                      "base": u, "now": v})
    else:
        diffs.append({"table": k, "whole": True})
target = open(os.path.join(ROOT, "src/firmware_ui_model.h"), encoding="utf-8").read()
f07 = [r for r in now["MUTS_W4AIDENT"] if r[0].startswith("F07 ")][0]
ok = (len(diffs) == 1 and diffs[0].get("table") == "MUTS_W4AIDENT" and diffs[0].get("id") == "F07"
      and diffs[0].get("field") == "replacement" and target.count(f07[1]) == 1)
res = {"differences": diffs, "f07_pattern_matches_target": target.count(f07[1]),
       "verdict": "IDENTICAL except F07's replacement string" if ok else "DIFFERS beyond A10 — STOP"}
open(sys.argv[1], "w").write(json.dumps(res, indent=1, ensure_ascii=False) + "\n")
print(res["verdict"], "|", [(d.get("table"), d.get("id"), d.get("field")) for d in diffs], "| pattern matches", res["f07_pattern_matches_target"])
sys.exit(0 if ok else 1)
