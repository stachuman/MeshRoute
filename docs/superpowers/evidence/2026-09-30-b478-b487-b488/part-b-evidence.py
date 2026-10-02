# Author: coder (B478/B487/B488/B490 package) — Part B evidence: stock runs, builder commands, records, controls
"""Usage: python3 -B part-b-evidence.py <evidence-dir> <artifact-dir>

1. Final-chain step 6: the STOCK CLI on `--profile full_headless` and `--profile mobile`, each twice into fresh
   directories under <artifact-dir>; every run's exit and row count, and each pair's `--compare` exit. The first ledger
   of each profile is copied into <evidence-dir> (ASCII text).
2. The builder commands the tool generated: the prefix (run.sh before the boundary, by SHA-256) and the lines it
   APPENDED, per run.
3. The expected-record reconciliation against the pre-check's `transcript-results.json` (array_live examples).
4. B10.2's validity refusals (the review's two counterexamples included) and the classified controls B-C0..B-C6, each
   with its observed outcome, driven through tools/test_probe_inbox_transcript.py's own helpers."""
import hashlib, json, re, shutil, subprocess, sys, unittest
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
import test_probe_inbox_transcript as P  # noqa: E402 — the B10 module (the tool is loaded through it)

ev, art = Path(sys.argv[1]), Path(sys.argv[2])
art.mkdir(parents=True, exist_ok=True)
t = P.load()
record = {"tool_sha256": hashlib.sha256(P.TOOL.read_bytes()).hexdigest(),
          "driver_sha256": hashlib.sha256(P.DRIVER.read_bytes()).hexdigest(),
          "runner_sha256": hashlib.sha256((P.HERE / "run.sh").read_bytes()).hexdigest()}
prefix = t.builder_prefix((P.HERE / "run.sh").read_text(encoding="utf-8"))
record["builder_prefix"] = {"sha256": hashlib.sha256(prefix.encode()).hexdigest(), "lines": prefix.count("\n"),
                            "boundary": t.BUILDER_BOUNDARY}

# ---- 1 + 2: the stock CLI, twice per profile ----------------------------------------------------------------------
runs, builders = {}, {}
for profile in ("full_headless", "mobile"):
    for n in (1, 2):
        out = art / "transcript" / f"{profile}-{n}"
        if out.exists():
            shutil.rmtree(out)               # this script's own previous artifact run, under ignored artifacts/
        r = P.cli("--profile", profile, "--out", out, "--emit", out / "ledger.txt")
        ledger = t.validate_ledger(r.stdout, f"{profile}-{n}") if r.returncode == 0 else None
        script = (out / "build.sh").read_text(encoding="utf-8")
        builders[f"{profile}-{n}"] = {"prefix_is_runner_prefix": script.startswith(prefix + "\n"),
                                      "appended": script[len(prefix) + 1:].rstrip("\n").split("\n"),
                                      "arm": t.PROFILE_ARMS[profile]}
        runs[f"{profile}-{n}"] = {"exit": r.returncode, "rows": len(ledger.rows) if ledger else None,
                                  "profile_line": r.stdout.split("\n")[2] if ledger else None,
                                  "ledger_sha256": hashlib.sha256(r.stdout.encode()).hexdigest(),
                                  "stderr": r.stderr.strip().splitlines(), "out": str(out)}
    a, b = art / "transcript" / f"{profile}-1" / "ledger.txt", art / "transcript" / f"{profile}-2" / "ledger.txt"
    c = P.cli("--compare", a, b)
    runs[f"{profile}-compare"] = {"exit": c.returncode, "summary": c.stdout.strip().splitlines()[-1:]}
    shutil.copyfile(a, ev / f"ledger-{profile}.txt")
record["stock_runs"] = runs
record["builder_commands"] = builders

# ---- 3: records against the pre-check ------------------------------------------------------------------------------
pre = json.loads((ROOT / "docs/superpowers/evidence/2026-09-30-b478-b487-b488-precheck/transcript-results.json")
                 .read_text())
recon = []
for entry in pre:
    mine = P.rows_of(t, (ev / f"ledger-{entry['profile']}.txt").read_text(encoding="ascii"))
    for ex in entry["variants"]["array_live"]["examples"]:
        row = mine[ex["id"]]
        ser = "  SER usb=[%s] ble=[%s] fp=%s" % row.ser
        ble = "  BLE ret=%s buf=[%s] nus=[%s] usb=[%s] fp=%s" % row.ble
        pinned = P.PINNED[entry["profile"]].get(ex["id"])
        recon.append({"profile": entry["profile"], "id": ex["id"], "command": ex["command"][:24] +
                      ("…" if len(ex["command"]) > 24 else ""), "len": row.length,
                      "SER_equal_precheck": ser == ex["SER"], "BLE_equal_precheck": ble == ex["BLE"],
                      "pinned_in_suite": pinned is not None and pinned[1] == (ser, ble)})
record["record_reconciliation"] = recon
too_long = {p: sum(1 for r in P.rows_of(t, (ev / f"ledger-{p}.txt").read_text(encoding="ascii")).values()
                   if P.TOO_LONG in r.ser[0]) for p in ("full_headless", "mobile")}
record["too_long_on_usb_after"] = too_long

# ---- 4a: B10.2 validity refusals --------------------------------------------------------------------------------------
P.TranscriptValidityTests.setUpClass()
vt = P.TranscriptValidityTests("test_invalid_ledgers_are_refused_even_when_identical")
validity = {}
for why, ledger in vt.invalid_ledgers(t).items():
    code, out, err = vt.compare(t, ledger)
    validity[why] = {"exit": code, "first_violation": (err or out).strip().splitlines()[0][:300]}
code, out, _ = vt.compare(t, vt.full)
validity["(positive control) a full valid synthetic ledger against itself"] = {"exit": code, "summary": out.strip().splitlines()[-1]}
record["validity_refusals"] = validity
b0 = P.load()
b0.match_expected = lambda *a, **k: None
record["B-C0"] = {"class": "validator", "required": "the empty and truncated pairs compare with 0, so the B10.2 "
                  "regression fails",
                  "review_pairs": {n: P.run_main(b0, "--compare", P.REVIEW / n, P.REVIEW / n)[0]
                                   for n in ("b8-empty-ledger.txt", "b8-one-row-ledger.txt")},
                  "regression_failures": [w for w, _c, _o in vt.validity_failures(b0)]}
P.TranscriptValidityTests.tearDownClass()

# ---- 4b: the build controls, through the suite's own setUpClass ----------------------------------------------------------
P.TranscriptEndToEndTests.setUpClass()
E = P.TranscriptEndToEndTests
final = E.runs["full_headless-1"]


def summary(run):
    return {"exit": run["code"], "child_rc": run.get("child_rc"), "built": run.get("built") is not None
            if "built" in run else True, "ledger_rows": len(P.rows_of(t, run["ledger"])) if run["ledger"] else 0,
            "messages": [ln[:300] for ln in run["stderr"].splitlines() if ln.strip()][-6:]}


e = E("test_control_b_c1_pointer_parameter_refuses_every_row_longer_than_seven")
c1 = E.runs["B-C1"]
rows = list(P.rows_of(t, c1["ledger"]).values())
failing_records = []
for cid in ("L003", "L133", "L166", "L201", "L202"):
    try:
        e.check_records("full_headless", c1["ledger"], only={cid})
    except AssertionError:
        failing_records.append(cid)
record["B-C1"] = dict(summary(c1), **{"class": "behavioural: builds and runs",
                                       "too_long_rows": sum(P.TOO_LONG in r.ser[0] for r in rows),
                                       "rows_longer_than_7": sum(r.length > 7 for r in rows),
                                       "record_assertions_failing": failing_records,
                                       "compare_with_final": e.compare_with_final(c1)[0]})
c2 = E.runs["B-C2"]
code, out, _ = e.compare_with_final(c2)
commands = dict(t.expected_matrix("full_headless"))
record["B-C2"] = dict(summary(c2), **{"class": "behavioural: builds and runs", "compare_with_final": code,
                                       "differing": [f"{m.group(1)} {m.group(2)} [{commands[m.group(1)].decode()}]"
                                                     for m in re.finditer(r"^DIFF (L\d+) (\w+) ", out, re.M)]})
record["B-C3"] = dict(summary(E.runs["B-C3"]), **{"class": "profile refusal: builds and runs"})
# B-C4 on the real validated ledger, as the suite does
tmp = E.tmp / "b-c4-evidence"
tmp.mkdir()
fin = (final["out"] / "emitted.txt").read_text(encoding="ascii")
lines = fin[:-1].split("\n")
at = {ln.split()[1]: i for i, ln in enumerate(lines) if ln.startswith("LINE ")}
usb = re.search(r"usb=\[(.*)\] ble=\[\]", lines[at["L133"] + 1]).group(1)
cases = {"one output byte (L003 SER count=0->1)": (at["L003"] + 1, "count=0", "count=1"),
         "one sink field moved (L133 SER usb->ble)": (at["L133"] + 1, "usb=[%s] ble=[]" % usb, "usb=[] ble=[%s]" % usb),
         "one fingerprint field (L166 BLE kh)": (at["L166"] + 2, "kh=b12d4983", "kh=b12d4984")}
b4 = {}
(tmp / "final.txt").write_text(fin, encoding="ascii")
for why, (i, old, new) in cases.items():
    mutated = list(lines)
    mutated[i] = P.edit(mutated[i], old, new)
    (tmp / "x.txt").write_text("\n".join(mutated) + "\n", encoding="ascii")
    code, out, err = P.run_main(t, "--compare", tmp / "final.txt", tmp / "x.txt")
    b4[why] = {"exit": code, "summary": out.strip().splitlines()[-1]}
for why, body in (("one row removed (L050)", lines[:at["L050"]] + lines[at["L050"] + 3:]),
                  ("one row duplicated (L050)", lines[:at["L050"]] + lines[at["L050"]:at["L050"] + 3] + lines[at["L050"]:])):
    (tmp / "x.txt").write_text("\n".join(body) + "\n", encoding="ascii")
    code, out, err = P.run_main(t, "--compare", tmp / "final.txt", tmp / "x.txt")
    b4[why] = {"exit": code, "refusal": err.strip()[:300]}
record["B-C4"] = {"class": "comparison", "cases": b4}
record["B-C5"] = dict(summary(E.runs["B-C5"]), **{"class": "driver refusal: builds and runs"})
record["B-C6"] = dict(summary(E.runs["B-C6"]), **{"class": "build refusal: must not compile",
                                                   "diagnostic_present": "'mrble' has not been declared"
                                                   in E.runs["B-C6"]["stderr"]})
for name in ("B-C1", "B-C2", "B-C3", "B-C5", "B-C6"):
    src = E.runs[name]["out"]
    dst = art / "controls" / name
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns("*.bin"))
P.TranscriptEndToEndTests.tearDownClass()

(ev / "part-b-evidence.json").write_text(json.dumps(record, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
for k, v in runs.items():
    print(k, {x: v.get(x) for x in ("exit", "rows", "summary")})
print("builders:", {k: (v["prefix_is_runner_prefix"], len(v["appended"]), v["arm"]) for k, v in builders.items()})
print("records equal pre-check:", all(r["SER_equal_precheck"] and r["BLE_equal_precheck"] and r["pinned_in_suite"]
                                      for r in recon), len(recon))
print("too_long after:", too_long)
print("validity exits:", sorted({v["exit"] for k, v in validity.items() if not k.startswith("(positive")}),
      "positive:", validity["(positive control) a full valid synthetic ledger against itself"]["exit"])
for k in ("B-C0", "B-C1", "B-C2", "B-C3", "B-C5", "B-C6"):
    print(k, {x: record[k].get(x) for x in record[k] if x not in ("messages", "class", "required")})
print("B-C4", {k: v["exit"] for k, v in b4.items()})
