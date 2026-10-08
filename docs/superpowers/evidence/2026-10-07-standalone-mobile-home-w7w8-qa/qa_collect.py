#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 QA (2026-10-07): read every raw log of the QA chain and reconcile it into one result record — native (shared
and cold), corpus, boards, ABI, both probes with their accounting, each mutation battery entry by entry (RED, match
count 1, every worker baseline at the native floor), discovery and the inventory — plus the SHA-256 of every raw
log. Read-only. Usage: qa_collect.py <qa_artifacts_dir> <out.json>."""
import hashlib, json, re, sys
from pathlib import Path

Q = Path(sys.argv[1])
FLOOR = (3123, 203168, 0)


def text(name):
    return (Q / name).read_bytes().decode("utf-8", "backslashreplace")


out, bad = {}, []

runs = [l.split("\t") for l in text("runs.tsv").splitlines() if l.strip()]
out["runs"] = runs
out["chain_done"] = runs[-1] == ["DONE"]
# ★ The first chain stopped at `mut-model` (exit 9) on ENOSPC and qa-chain-2.sh resumed after a RESUME marker. That
#   failed row is SUPERSEDED only if (a) its preserved log shows Errno 28 and that no worker derived a clean baseline
#   (nothing was measured), and (b) the same step ran again after the marker with exit 0. Anything else stays a problem.
resume = next((i for i, r in enumerate(runs) if r[0] == "RESUME"), None)
superseded = []
for i, r in enumerate(runs[:-1]):
    if r[0] == "RESUME" or r[0].startswith("STOP:"):
        continue
    if len(r) >= 2 and r[1] != "0" and not (len(r) > 3 and r[3] == "observed"):
        later_ok = resume is not None and i < resume and any(x[0] == r[0] and x[1] == "0" for x in runs[resume + 1:])
        kept = text("enospc-mut-model.log") if r[0] == "mut-model" and (Q / "enospc-mut-model.log").exists() else ""
        if later_ok and "Errno 28" in kept and "no worker derived a clean baseline" in kept:
            superseded.append({"step": r[0], "exit": r[1], "cause": "ENOSPC (Errno 28); no verdicts",
                               "kept_log": "enospc-mut-model.log", "rerun_exit": "0"})
        else:
            bad.append(f"step {r[0]} exit {r[1]}")
out["superseded_steps"] = superseded
out["stop_lines"] = [r[0] for r in runs if r[0].startswith("STOP:")]


def doctest(name):
    t = text(name)
    c = re.search(r"test cases:\s+(\d+) \|\s+(\d+) passed \| (\d+) failed \| (\d+) skipped", t)
    a = re.search(r"assertions:\s+(\d+) \|\s+(\d+) passed \| (\d+) failed", t)
    return {"cases": int(c[1]), "cases_failed": int(c[3]), "skipped": int(c[4]), "assertions": int(a[1]),
            "assertions_failed": int(a[3]), "status_success": "Status: SUCCESS" in t}


for n in ("native-binary.log", "native-cold-binary.log"):
    d = doctest(n)
    out[n] = d
    if (d["cases"], d["assertions"], d["cases_failed"]) != FLOOR or d["skipped"] or d["assertions_failed"]:
        bad.append(f"{n} {d}")
out["native_cold_build_compiled"] = "warning:" in text("native-cold-build.log") and "Library Manager: Installing" in text("native-cold-build.log")
out["native_shared_build_incremental"] = "Testing..." in text("native-build.log")

for n in ("corpus-vs-base.json", "corpus-vs-final.json", "board-fields.json", "board-provenance.json"):
    v = json.loads((Q / n).read_text())["verdict"]
    out[n] = v
    if v != "PASS":
        bad.append(f"{n} {v}")
out["lus_sha256"] = text("lus-sha.log").split()[0]

for n in ("abi.log", "abi-supplemental.log"):
    m = re.search(r"PASS: board ABI \((\d+) checks, (\d+)/(\d+) controls RED, (\d+) unusable\)", text(n))
    out[n] = {"checks": int(m[1]), "controls_red": int(m[2]), "controls": int(m[3]), "unusable": int(m[4])} if m else None
    if not m or m[2] != m[3] or m[4] != "0":
        bad.append(f"{n} not PASS")

t = text("firmware-ui.log")
arms = [tuple(map(int, x)) for x in re.findall(r"(\d+) passed / (\d+) failed / (\d+) total", t)]
cv = re.search(r"controls: (\d+) verified / (\d+) unusable", t)
ca = re.search(r"controls accounted: (\d+) declared, (\d+) exactly once with the accepted outcome, (\d+) guard failure", t)
out["firmware_ui"] = {"arms": arms, "controls_verified": int(cv[1]), "controls_unusable": int(cv[2]),
                      "declared": int(ca[1]), "accounted_once": int(ca[2]), "guard_failures": int(ca[3]),
                      "pass_line": t.rstrip().endswith("PASS")}
if (any(f for _, f, _ in arms) or cv[2] != "0" or ca[1] != ca[2] or ca[3] != "0" or not out["firmware_ui"]["pass_line"]):
    bad.append("firmware-ui")

t = text("board-ui.log")
ia = re.search(r"identities accounted: (\d+) declared, (\d+) exactly once with the accepted outcome, (\d+) guard failure", t)
cats = dict((k, (int(a), int(b))) for k, a, b in re.findall(r"^\s+(\S+)\s+(\d+) expected,\s+(\d+) observed", t, re.M))
we = re.search(r"wiring predicate exits: live (\d+) x exit 0; mutant (\d+) x exit 1", t)
out["board_ui"] = {"declared": int(ia[1]), "accounted_once": int(ia[2]), "guard_failures": int(ia[3]),
                   "categories": cats, "live_exit0": int(we[1]), "mutant_exit1": int(we[2]),
                   "pass_line": t.rstrip().endswith("PASS")}
if ia[1] != ia[2] or ia[3] != "0" or any(a != b for a, b in cats.values()) or not out["board_ui"]["pass_line"]:
    bad.append("board-ui")

muts = {}
for p in sorted(Q.glob("mut-*.log")):
    t = p.read_bytes().decode("utf-8", "backslashreplace")
    tgt = re.search(r"^-- target (\S+): \S+ \((\d+) entries\)", t, re.M)
    sel = re.search(r"^-- selection: (.*)$", t, re.M)
    fin = re.search(r"^mutations: (\d+) RED / (\d+) unusable", t, re.M)
    reds = re.findall(r"^  ok   (.+?) -> RED \((\d+) assertion\(s\) failed, match count (\d+)\)", t, re.M)
    base = re.findall(r"clean baseline (\d+) / (\d+) / (\d+)\s+\(DERIVED from this tree\)", t)
    merged = re.search(r"clean baseline (\d+) / (\d+) / (\d+)\s+\(DERIVED per worker tree; (\d+) tree\(s\) agree\)", t)
    banners = len(re.findall(r"B217", t))
    untouched = "real tree untouched" in t
    rec = {"target": tgt[1] if tgt else None, "configured": int(tgt[2]) if tgt else None,
           "selection": sel[1] if sel else None, "red": int(fin[1]) if fin else None,
           "unusable": int(fin[2]) if fin else None, "red_lines": len(reds),
           "match_counts_all_1": all(m == "1" for _, _, m in reds),
           "labels": [l.split(" ")[0] for l, _, _ in reds],
           "worker_baselines": len(base), "worker_baselines_at_floor": all(tuple(map(int, b)) == FLOOR for b in base),
           "merged_baseline": tuple(map(int, merged.groups()[:3])) if merged else None,
           "trees_agree": int(merged[4]) if merged else None, "b217_mentions": banners, "real_tree_untouched": untouched}
    muts[p.stem] = rec
    single = p.stem.startswith("mut-uipresets-")
    want = 1 if single else rec["configured"]
    if (rec["red"] != want or rec["unusable"] != 0 or rec["red_lines"] != want or not rec["match_counts_all_1"]
            or not rec["worker_baselines_at_floor"] or rec["merged_baseline"] != FLOOR or not untouched):
        bad.append(f"{p.stem} {rec['red']}/{want} unusable {rec['unusable']}")
out["mutation"] = muts
out["mutation_selector_a_plus_uieditor"] = sum(muts[k]["red"] for k in muts if not k.startswith("mut-uipresets-"))

t = text("discovery.log")
d = re.search(r"Ran (\d+) tests? in", t)
out["discovery"] = {"ran": int(d[1]) if d else None, "ok": bool(re.search(r"^OK$", t, re.M)), "skipped": "skipped" in t}
if not d or not out["discovery"]["ok"] or out["discovery"]["skipped"]:
    bad.append("discovery")
out["inventory"] = text("inventory.log").strip().splitlines()[-1:]
for n in ("stability.json",):
    v = json.loads((Q / n).read_text())["verdict"]
    out[n] = v
    if v != "PASS":
        bad.append(n)

out["raw_logs"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(Q.glob("*.log"))}
out["problems"] = bad
out["verdict"] = "PASS" if not bad and out["chain_done"] else "FAIL"
json.dump(out, open(sys.argv[2], "w"), indent=1, sort_keys=True)
print(json.dumps({k: out[k] for k in ("verdict", "problems", "mutation_selector_a_plus_uieditor", "discovery",
                                      "firmware_ui", "board_ui")}, indent=1))
sys.exit(0 if out["verdict"] == "PASS" else 1)
