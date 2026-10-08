# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W7+W8 — static AST census of the mutation union (brief §4.1 baseline step 10; final step 9's tables). The harness
# is PARSED, never imported (its top level starts a battery). For every battery in the two selectors: its entry
# count, its target file, and whether each entry's pattern matches that target exactly once; plus every table's
# AST fingerprint and the native pins. Usage: python3 -B union_census.py <out.json> [reference.json]
import ast, hashlib, json, os, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
SEL_A = ["model", "chrome", "uisend", "sliceCbudget", "sliceCsend", "w4aident", "w4bhome"]     # changed product files
SEL_B = ["uistatus", "uiteam", "uiinvite", "uiprov", "uijoin", "uipresets", "config", "consoleline"]  # dependencies
NEW = ["uieditor"]                                                                               # added by this package
src = open(os.path.join(ROOT, "tools/probe_ui_model_mutations.py"), "rb").read()
tree = ast.parse(src)
named = {}
for n in tree.body:
    if isinstance(n, ast.Assign):
        for t in n.targets:
            names = [t] if isinstance(t, ast.Name) else list(getattr(t, "elts", []))
            for i, nm in enumerate(names):
                if isinstance(nm, ast.Name):
                    named.setdefault(nm.id, n.value if isinstance(t, ast.Name) else n.value.elts[i])
by_target = {ast.literal_eval(k): v.id for k, v in zip(named["MUTS_BY_TARGET"].keys, named["MUTS_BY_TARGET"].values)}
target_src = ast.literal_eval(named["TARGET_SRC"])
fp = lambda node: hashlib.sha256(ast.dump(node).encode()).hexdigest()
def battery(t):
    if t not in by_target:
        return None
    entries = ast.literal_eval(named[by_target[t]])
    text = open(os.path.join(ROOT, target_src[t]), encoding="utf-8").read()
    hits = [text.count(p) for _l, p, _r in entries]
    return {"list": by_target[t], "target": target_src[t], "entries": len(entries),
            "match_exactly_once": sum(h == 1 for h in hits),
            "not_once": [(entries[i][0].split()[0], h) for i, h in enumerate(hits) if h != 1],
            "ids": [e[0].split()[0] for e in entries], "fingerprint": fp(named[by_target[t]])}
out = {"selector_a": {t: battery(t) for t in SEL_A}, "selector_b": {t: battery(t) for t in SEL_B},
       "new": {t: battery(t) for t in NEW},
       "pins": [ast.unparse(named["PIN_CASES"]), ast.unparse(named["PIN_ASSERTS"])],
       "all_batteries": len(by_target), "all_entries": sum(len(ast.literal_eval(named[l])) for l in by_target.values()),
       "fingerprints": {k: fp(named[v]) for k, v in sorted(by_target.items())}}
a = sum(b["entries"] for b in out["selector_a"].values() if b)
bb = sum(b["entries"] for b in out["selector_b"].values() if b)
nw = sum(b["entries"] for b in out["new"].values() if b)
out["totals"] = {"selector_a": a, "selector_b": bb, "new": nw,
                 "union_batteries": sum(1 for s in ("selector_a", "selector_b", "new") for b in out[s].values() if b),
                 "union_entries": a + bb + nw}
json.dump(out, open(sys.argv[1], "w"), indent=1)
print(f"selector (a) {a}: " + ", ".join(f"{t} {b['entries']}" for t, b in out['selector_a'].items() if b))
print(f"selector (b) {bb}: " + ", ".join(f"{t} {b['entries']}" for t, b in out['selector_b'].items() if b))
print(f"new: {', '.join(f'{t} {b[chr(101)+chr(110)+chr(116)+chr(114)+chr(105)+chr(101)+chr(115)]}' for t, b in out['new'].items() if b) or 'none yet'}")
print(f"union {out['totals']['union_batteries']} batteries / {out['totals']['union_entries']} entries; "
      f"not-exactly-once: {[(t, b['not_once']) for s in ('selector_a', 'selector_b', 'new') for t, b in out[s].items() if b and b['not_once']]}; "
      f"pins {out['pins']}; whole harness {out['all_batteries']} batteries / {out['all_entries']} entries")
