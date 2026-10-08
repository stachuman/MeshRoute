#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 — B499's closure proof for `run-final.sh::step` (synthetic; NO firmware gate runs). The real helper is
extracted VERBATIM between its `# >>> step` / `# <<< step` markers and driven by tiny synthetic chains in a temp dir:
  · fault   — `step first false; step following true; echo DONE` must exit NONZERO, record ONLY `first` with exit 1,
              never run `following` (no log) and never print DONE;
  · healthy — `step first true; step following true; echo DONE` must exit 0, record both with exit 0 and print DONE.
The CONTROL removes the stop line from the extracted helper (the bypass) and requires the fault proof to FAIL on it —
a proof that cannot fail is not a measurement. Usage: python3 -B runner_step_proof.py <run-final.sh> <out.json>.
Exit 0 iff the real helper passes both cases AND the bypass is caught."""
import json, subprocess, sys, tempfile
from pathlib import Path

src = Path(sys.argv[1]).read_text()
a, b = src.index("# >>> step"), src.index("# <<< step")
helper = src[a:b]
STOP = '         if [ "$rc" -ne 0 ]; then'
assert helper.count(STOP) == 1, "the stop line is not where the proof expects it (exactly once)"
# the bypass: the stop line becomes a bare close — the pre-B499 helper, which records and carries on
bypassed = "\n".join(("         true; }" if l.startswith(STOP) else l) for l in helper.splitlines()) + "\n"


def drive(h, first_cmd):
    d = tempfile.mkdtemp(prefix="w7w8-b499-")
    script = f'A={d}\n{h}\nstep first {first_cmd}\nstep following true\necho DONE\n'
    r = subprocess.run(["bash", "-c", script], capture_output=True, text=True)
    tsv = (Path(d) / "final-runs.tsv").read_text() if (Path(d) / "final-runs.tsv").exists() else ""
    rows = [l.split("\t")[:2] for l in tsv.splitlines()]
    return {"exit": r.returncode, "stdout": r.stdout, "stderr": r.stderr, "tsv_rows": rows,
            "following_ran": (Path(d) / "final-following.log").exists()}


def fault_ok(x):
    return x["exit"] != 0 and x["tsv_rows"] == [["first", "1"]] and not x["following_ran"] and "DONE" not in x["stdout"]


def healthy_ok(x):
    return x["exit"] == 0 and x["tsv_rows"] == [["first", "0"], ["following", "0"]] and x["following_ran"] \
        and x["stdout"] == "DONE\n"


real_fault, real_healthy = drive(helper, "false"), drive(helper, "true")
ctl_fault = drive(bypassed, "false")
out = {"scope": "synthetic: the real step() extracted verbatim; no firmware gate runs",
       "helper_sha256_source": __import__("hashlib").sha256(src.encode()).hexdigest(),
       "real": {"fault": real_fault, "fault_ok": fault_ok(real_fault),
                "healthy": real_healthy, "healthy_ok": healthy_ok(real_healthy)},
       "control_stop_removed": {"fault": ctl_fault, "proof_fails_as_required": not fault_ok(ctl_fault)}}
# ⛔ the control must be a WORKING helper (both rows recorded, DONE printed) — a broken one would be "caught" vacuously
out["control_stop_removed"]["control_is_a_working_helper"] = (ctl_fault["tsv_rows"] == [["first", "1"], ["following", "0"]]
                                                             and "DONE" in ctl_fault["stdout"])
out["verdict"] = "PASS" if out["control_stop_removed"]["control_is_a_working_helper"] and (out["real"]["fault_ok"] and out["real"]["healthy_ok"]
                            and out["control_stop_removed"]["proof_fails_as_required"]) else "FAIL"
json.dump(out, open(sys.argv[2], "w"), indent=1)
print(f"real helper: fault exit {real_fault['exit']} rows {real_fault['tsv_rows']} following {real_fault['following_ran']} "
      f"-> {'ok' if out['real']['fault_ok'] else 'FAIL'}; healthy exit {real_healthy['exit']} rows "
      f"{real_healthy['tsv_rows']} -> {'ok' if out['real']['healthy_ok'] else 'FAIL'}")
print(f"control (stop removed): fault exit {ctl_fault['exit']} rows {ctl_fault['tsv_rows']} following "
      f"{ctl_fault['following_ran']} -> {'RED (caught)' if out['control_stop_removed']['proof_fails_as_required'] else 'NOT CAUGHT'}")
print("B499 PROOF", out["verdict"])
sys.exit(0 if out["verdict"] == "PASS" else 1)
