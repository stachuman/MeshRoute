# Author: coder (B478/B487/B488/B490 package) — compact summary of one `--target=w4aident` battery log
"""Usage: python3 -B w4aident-summary.py <battery.log> <exit-code> <out.json> [reference.json f07-count]

Reads the MERGED REPORT of a battery log (raw log kept under artifacts/) and writes the per-entry verdicts, the
failed-assertion counts, the worker lines and the final tallies. With a reference summary, exits 1 unless the
entry identities, verdicts and counts are identical."""
import json, re, sys

log, rc, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
text = open(log, "rb").read().decode("utf-8", "backslashreplace")
merged = text[text.index("-- MERGED REPORT"):]
entries = []
for m in re.finditer(r"^  (ok  |FAIL|UNUSABLE|VACUOUS|MISSING) +(F\d\d)\b(.*)$", merged, re.M):
    kind, ident, rest = m.group(1).strip(), m.group(2), m.group(3)
    red = re.search(r"-> RED \((\d+) assertion\(s\) failed, match count 1\)$", rest)
    entries.append({"id": ident, "verdict": "RED" if kind == "ok" and red else kind,
                    "failed_assertions": int(red.group(1)) if red else None})
base = re.search(r"^  ok   clean baseline (\d+) / (\d+) / (\d+)\s+\(DERIVED per worker tree; (\d+) tree\(s\) agree\)", merged, re.M)
workers = re.findall(r"^  worker (\d+): (\d+)/(\d+) entries, (\d+) RED / (\d+) worthless, wall [\d.]+s, rc (\d+), "
                     r"source restored: md5 (\w+) \((MATCHES|DIFFERS — FAIL)\)", merged, re.M)
tally = re.search(r"^mutations: (\d+) RED / (\d+) unusable(.*)$", merged, re.M)
summary = {
    "log": log, "exit": rc,
    "clean_baseline": [int(base.group(i)) for i in (1, 2, 3)] if base else None,
    "trees_agree": int(base.group(4)) if base else None,
    "entries": entries,
    "workers": [{"worker": int(w[0]), "entries": f"{w[1]}/{w[2]}", "red": int(w[3]), "worthless": int(w[4]),
                 "rc": int(w[5]), "md5_after": w[6], "restored": w[7]} for w in workers],
    "tally": {"red": int(tally.group(1)), "unusable": int(tally.group(2)), "note": tally.group(3).strip()} if tally else None,
    "integrity_block": "RUN INTEGRITY FAILURE" in merged,
    "real_tree_untouched": "real tree untouched" in merged,
}
open(out, "w").write(json.dumps(summary, indent=1) + "\n")
print(f"exit {rc}; baseline {summary['clean_baseline']} x{summary['trees_agree']}; "
      + " ".join(f"{e['id']}:{e['verdict']}({e['failed_assertions']})" for e in entries)
      + f"; workers {len(summary['workers'])}; tally {summary['tally']}")
if len(sys.argv) > 4:
    # RETURN (brief r4 §4.1 steps 4-5): F01-F06, F08 and F09 keep the round-1 baseline counts; F07 has A10's stable
    # count (argv[5]) — its old 44 is retired (B492). Every entry RED, the clean baseline equal, exit 0.
    ref = json.load(open(sys.argv[4]))
    f07 = int(sys.argv[5])
    want = [dict(e, failed_assertions=f07) if e["id"] == "F07" else e for e in ref["entries"]]
    same = (want == summary["entries"] and ref["clean_baseline"] == summary["clean_baseline"] and rc == 0
            and all(e["verdict"] == "RED" for e in summary["entries"]))
    print(("MATCHES: eight baseline counts + F07 = %d" % f07) if same else "DIFFERS from the expected counts")
    sys.exit(0 if same else 1)
