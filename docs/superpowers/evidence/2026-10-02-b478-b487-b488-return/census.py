# Author: coder (B478/B487/B488/B490 package) — static AST census of the mutation harness's tables (never imports it)
"""Usage: python3 -B census.py <harness.py> <out.json> [compare.json]

Counts MUTS_BY_TARGET batteries, every MUTS_* list's entries, TARGET_SRC's distinct target files, and fingerprints
(SHA-256 of ast.dump) the MUTS_* lists, MUTS_BY_TARGET, TARGET_SRC, PIN_CASES/PIN_ASSERTS and the worker constants.
With a third argument, exits 1 unless every figure and fingerprint equals that earlier census."""
import ast, hashlib, json, sys

def census(path):
    tree = ast.parse(open(path, "rb").read())
    named = {}
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for tgt in node.targets:
                names = [tgt] if isinstance(tgt, ast.Name) else list(getattr(tgt, "elts", []))
                for i, nm in enumerate(names):
                    if isinstance(nm, ast.Name):
                        val = node.value if isinstance(tgt, ast.Name) else node.value.elts[i]
                        watched = nm.id.startswith(("MUTS_", "PIN_", "_WORKERS")) or nm.id == "TARGET_SRC"
                        if watched and nm.id in named:
                            raise SystemExit(f"census: {nm.id} assigned twice at top level")
                        named[nm.id] = val
        if isinstance(node, ast.AugAssign) and isinstance(node.target, ast.Name) and node.target.id.startswith("MUTS_"):
            raise SystemExit(f"census: augmented assignment to {node.target.id} — the static count would be wrong")
    lists = {k: v for k, v in named.items() if k.startswith("MUTS_") and k != "MUTS_BY_TARGET"}
    for k, v in lists.items():
        if not isinstance(v, ast.List):
            raise SystemExit(f"census: {k} is not a list literal")
    by_target = named["MUTS_BY_TARGET"]
    target_src = named["TARGET_SRC"]
    bt = {ast.literal_eval(k): v.id for k, v in zip(by_target.keys, by_target.values)}
    ts = {ast.literal_eval(k): ast.literal_eval(v) for k, v in zip(target_src.keys, target_src.values)}
    if set(bt) != set(ts):
        raise SystemExit(f"census: MUTS_BY_TARGET and TARGET_SRC keys differ: {sorted(set(bt) ^ set(ts))}")
    counts = {t: len(lists[bt[t]].elts) for t in bt}
    unreferenced = sorted(set(lists) - set(bt.values()))
    fp = lambda node: hashlib.sha256(ast.dump(node).encode()).hexdigest()
    worker = {k: ast.unparse(named[k]) for k in ("_WORKERS_CAP", "_WORKERS_RESERVED_CORES", "_WORKERS_DEFAULT", "_WORKERS")}
    funcs = {n.name: fp(n) for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in ("_workers_default_formula", "_usable_cores")}
    return {
        "scope": "AST census only; the harness is parsed, never imported or run",
        "harness_sha256": hashlib.sha256(open(path, "rb").read()).hexdigest(),
        "batteries": len(bt),
        "entries": sum(counts.values()),
        "target_files": len(set(ts.values())),
        "muts_lists": len(lists),
        "unreferenced_muts_lists": unreferenced,
        "pins": [ast.unparse(named["PIN_CASES"]), ast.unparse(named["PIN_ASSERTS"])],
        "worker_constants": worker,
        "counts": counts,
        "target_src": ts,
        "fingerprints": {
            "muts_lists": {k: fp(v) for k, v in sorted(lists.items())},
            "MUTS_BY_TARGET": fp(by_target),
            "TARGET_SRC": fp(target_src),
            "PIN_CASES": fp(named["PIN_CASES"]),
            "PIN_ASSERTS": fp(named["PIN_ASSERTS"]),
            "worker_constants": {k: fp(named[k]) for k in worker},
            "worker_functions": funcs,
        },
    }

if __name__ == "__main__":
    got = census(sys.argv[1])
    open(sys.argv[2], "w").write(json.dumps(got, indent=1, sort_keys=True, ensure_ascii=False) + "\n")
    print(f"batteries={got['batteries']} entries={got['entries']} target_files={got['target_files']} "
          f"muts_lists={got['muts_lists']} unreferenced={got['unreferenced_muts_lists']} pins={got['pins']} "
          f"worker={got['worker_constants']}")
    if len(sys.argv) > 3:
        ref = json.load(open(sys.argv[3]))
        keys = ("batteries", "entries", "target_files", "muts_lists", "unreferenced_muts_lists", "pins",
                "worker_constants", "counts", "target_src", "fingerprints")
        diff = [k for k in keys if ref.get(k) != got.get(k)]
        print("census IDENTICAL to " + sys.argv[3] if not diff else f"census DIFFERS in {diff}")
        sys.exit(1 if diff else 0)
