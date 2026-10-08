#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 §4.1 final step 9: verify the union's battery logs — for every battery of selectors (a) and (b) and the new
`uieditor`: the battery ran IN FULL, every entry RED (`mutations: N RED / 0 unusable` with N = the census's entry
count), no `FAIL` verdict line, no STALE-pin banner, every worker's clean baseline at the expected native floor, the
real tree untouched and every worker's source restored. Reads `artifacts/…/<prefix>-union-<target>.log`.
Usage: python3 -B union_verify.py <prefix> <census.json> <cases> <asserts> <out.json>."""
import json, re, sys
from pathlib import Path

A = Path("/home/staszek/MeshRoute/artifacts/2026-10-04-standalone-mobile-home-w7w8")
prefix, census, cases, asserts, out_path = sys.argv[1], json.load(open(sys.argv[2])), sys.argv[3], sys.argv[4], sys.argv[5]
rows, bad = [], []
for sel in ("selector_a", "selector_b", "new"):
    for t, b in census[sel].items():
        if not b:
            continue
        p = A / f"{prefix}-union-{t}.log"
        if not p.exists():
            bad.append(f"{t}: no log"); continue
        s = p.read_text(errors="replace")
        m = re.findall(r"^mutations: (\d+) RED / (\d+) unusable$", s, re.M)
        full = re.search(rf"^-- selection: IN FULL \(all {b['entries']} entries\)$", s, re.M) is not None
        bases = re.findall(r"clean baseline (\d+) / (\d+) / (\d+)", s)
        fails = [l for l in s.splitlines() if re.match(r"^\s*(\[w\d+\]\s+)?FAIL ", l)]
        stale = "STALE CROSS-CHECK PIN" in s
        untouched = "real tree untouched" in s
        restored = len(re.findall(r"source restored: md5 \w+ \(MATCHES\)", s))
        red, unusable = (int(m[-1][0]), int(m[-1][1])) if m else (None, None)
        ok = (full and red == b["entries"] and unusable == 0 and not fails and not stale and untouched and bases and
              all(x == (cases, asserts, "0") for x in bases) and restored > 0)
        rows.append({"selector": sel, "battery": t, "entries": b["entries"], "red": red, "unusable": unusable,
                     "in_full": full, "fail_lines": fails[:5], "stale_pin": stale, "worker_baselines": sorted(set(bases)),
                     "tree_untouched": untouched, "workers_restored": restored, "ok": ok})
        if not ok:
            bad.append(t)
out = {"prefix": prefix, "expected_floor": [cases, asserts, "0"], "batteries": len(rows),
       "entries": sum(r["entries"] for r in rows), "red": sum(r["red"] or 0 for r in rows), "rows": rows,
       "failures": bad, "verdict": "PASS" if not bad else "FAIL"}
json.dump(out, open(out_path, "w"), indent=1)
for r in rows:
    print(f"  {'ok  ' if r['ok'] else 'FAIL'} {r['battery']:13s} {r['red']}/{r['entries']} RED, {r['unusable']} unusable, "
          f"baselines {r['worker_baselines']}")
print(f"union: {out['batteries']} batteries / {out['entries']} entries, {out['red']} RED -> {out['verdict']} {bad}")
sys.exit(0 if not bad else 1)
