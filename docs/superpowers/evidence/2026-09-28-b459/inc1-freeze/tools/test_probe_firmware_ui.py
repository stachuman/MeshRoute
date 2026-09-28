#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""[[B456]] regression for tools/probe_firmware_ui/run.sh — the control-completeness accounting.

WHAT THIS COVERS. The runner's `--selftest-accounting <dir> [--no-neg]` mode replays a SYNTHETIC label/verdict stream
through the runner's OWN accounting functions and the SAME final accounting call a real run ends with
(`controls_final`), without building a probe or compiling a single mutant. Each case below asserts the runner's exit
status, so the shell plumbing itself is what is tested — never a second, Python-only model of it.

THE DEFECT (measured, W4a 2026-09-26): a guard that failed as a COMMAND (`once: command not found`, exit 127) skipped
its `&& ctl`; 229 of 236 controls ran and the runner printed PASS, because nothing compared the declared controls with
the verdicts that were recorded.

⛔ LOAD-BEARING, NOT DECORATIVE: `test_bypassing_the_final_accounting_lets_a_missing_control_through` removes the one
   accounting statement from a COPY of the runner and shows the missing-control case then exits 0 — so the stock
   runner's failure on that case is the accounting call's doing, and nothing else's.

RUN:  python3 -m unittest discover -s tools -p "test_*.py"     (or: cd tools && python3 test_probe_firmware_ui.py)
"""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

RUNNER = Path(__file__).resolve().parent / "probe_firmware_ui" / "run.sh"
LIBRARY = Path(__file__).resolve().parent / "probe_accounting.sh"   # [[B459]] the shared comparator the runner sources
ACCOUNTING_STATEMENT = 'account_controls "$1" "$2" "$CTL_VERDICTS" "$CTL_GUARDS" "$3" || rc=1'

# Five synthetic controls, C-labels in the runner's own shape; `no` is C0's must_build=no build-failure control.
LABELS = [
    ("S0  a synthetic build control", "no"),
    ("S1 the first synthetic control", "yes"),
    ("S2 the middle synthetic control", "yes"),
    ("S3 a synthetic \"quoted\" control", "yes"),
    ("S4 the last synthetic control", "yes"),
]


def accepted(must_build: str) -> str:
    return "build_fail_ok" if must_build == "no" else "red"


def complete_events() -> list[tuple[str, ...]]:
    return [("verdict", label, accepted(mb)) for label, mb in LABELS]


class AccountingSelftest(unittest.TestCase):
    def run_case(self, events, expected=None, calls=None, no_neg=False, runner=RUNNER):
        expected = LABELS if expected is None else expected
        with tempfile.TemporaryDirectory(prefix="b456-") as d:
            dd = Path(d)
            (dd / "expected.tsv").write_text("".join(f"{l}\t{mb}\n" for l, mb in expected))
            (dd / "calls").write_text(f"{len(expected) if calls is None else calls}\n")
            (dd / "events").write_text("".join("\t".join(e) + "\n" for e in events))
            argv = ["bash", str(runner), "--selftest-accounting", str(dd)] + (["--no-neg"] if no_neg else [])
            p = subprocess.run(argv, capture_output=True, text=True, timeout=60)
        return p.returncode, p.stdout + p.stderr

    def assertPasses(self, events, **kw):
        rc, out = self.run_case(events, **kw)
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS", out.splitlines()[-1], out)

    def assertFails(self, events, why, **kw):
        rc, out = self.run_case(events, **kw)
        self.assertEqual(rc, 1, out)
        self.assertEqual(out.splitlines()[-1], "FAIL", out)
        self.assertIn(why, out)

    # ---- the complete set -------------------------------------------------------------------------------------------
    def test_complete_set_passes(self):
        self.assertPasses(complete_events())

    # ---- missing first / middle / last ------------------------------------------------------------------------------
    def test_missing_first_fails(self):
        self.assertFails(complete_events()[1:], "MISSING verdict")

    def test_missing_middle_fails(self):
        ev = complete_events(); del ev[2]
        self.assertFails(ev, "MISSING verdict")

    def test_missing_last_fails(self):
        self.assertFails(complete_events()[:-1], "MISSING verdict")

    # ---- duplicate, unknown, and a duplicate that compensates a missing label at the same count ---------------------
    def test_duplicate_fails(self):
        self.assertFails(complete_events() + [complete_events()[1]], "DUPLICATE verdict")

    def test_unknown_label_fails(self):
        self.assertFails(complete_events() + [("verdict", "S9 a control nobody declared", "red")], "UNKNOWN label")

    def test_duplicate_compensating_a_missing_label_at_equal_count_fails(self):
        ev = complete_events(); ev[3] = ev[1]            # S1 twice, S3 never — the TOTAL is unchanged
        self.assertEqual(len(ev), len(LABELS))
        rc, out = self.run_case(ev)
        self.assertEqual(rc, 1, out)
        self.assertIn("DUPLICATE verdict", out)
        self.assertIn("MISSING verdict", out)

    # ---- zero expected, extraction mismatch -------------------------------------------------------------------------
    def test_zero_expected_fails(self):
        self.assertFails([], "no control is declared", expected=[], calls=0)

    def test_extraction_mismatch_fails(self):
        self.assertFails(complete_events(), "the extractor read", calls=len(LABELS) + 1)

    # ---- guards: nonzero, exit 127 (an undefined helper), and `once`'s recorded failure ------------------------------
    def test_guard_nonzero_with_no_verdict_fails(self):
        ev = complete_events(); ev[2] = ("guard_nonzero", LABELS[2][0])
        self.assertFails(ev, "MISSING verdict")

    def test_guard_127_with_no_verdict_fails(self):
        ev = complete_events(); ev[2] = ("guard_127", LABELS[2][0])
        self.assertFails(ev, "MISSING verdict")

    def test_recorded_guard_failure_fails_even_with_every_verdict(self):
        self.assertFails(complete_events() + [("guard_record", "once: an anchor that matched twice")],
                         "a control guard failed")

    # ---- C0's accepted build failure versus an ordinary must-build failure -----------------------------------------
    def test_build_control_accepts_its_build_failure(self):
        ev = complete_events()
        self.assertEqual(ev[0], ("verdict", LABELS[0][0], "build_fail_ok"))
        self.assertPasses(ev)

    def test_ordinary_control_that_does_not_compile_fails(self):
        ev = complete_events(); ev[1] = ("verdict", LABELS[1][0], "no_compile")
        self.assertFails(ev, "is not the accepted red")

    def test_build_control_that_still_builds_fails(self):
        ev = complete_events(); ev[0] = ("verdict", LABELS[0][0], "still_builds")
        self.assertFails(ev, "is not the accepted build_fail_ok")

    # ---- --no-neg is not a gate --------------------------------------------------------------------------------------
    def test_no_neg_says_it_is_not_a_gate_and_never_passes(self):
        rc, out = self.run_case([], no_neg=True)
        self.assertEqual(rc, 0, out)
        self.assertIn("NOT RUN (--no-neg)", out)
        self.assertIn("NOT a gate", out)
        self.assertNotIn("PASS", out)
        self.assertTrue(out.splitlines()[-1].startswith("NOT A GATE"), out)

    def test_no_neg_with_recorded_verdicts_fails(self):
        rc, out = self.run_case(complete_events(), no_neg=True)
        self.assertEqual(rc, 1, out)

    # ---- load-bearing: the ONE accounting statement is what fails a missing control ---------------------------------
    def test_bypassing_the_final_accounting_lets_a_missing_control_through(self):
        src = RUNNER.read_text(encoding="utf-8")
        self.assertEqual(src.count(ACCOUNTING_STATEMENT), 1, "the accounting statement must exist exactly once")
        ev = complete_events(); del ev[2]
        stock_rc, stock_out = self.run_case(ev)
        self.assertEqual(stock_rc, 1, stock_out)
        with tempfile.TemporaryDirectory(prefix="b456-bypass-") as d:
            copy_dir = Path(d) / "tools" / "probe_firmware_ui"; copy_dir.mkdir(parents=True)
            # [[B459]] the copy's `$ROOT/tools/probe_accounting.sh` is the REAL shared comparator, by an explicit link
            (copy_dir.parent / "probe_accounting.sh").symlink_to(LIBRARY)
            bypassed = copy_dir / "run.sh"
            bypassed.write_text(src.replace(ACCOUNTING_STATEMENT, ": # [[B456]] bypassed by the regression"),
                                encoding="utf-8")
            rc, out = self.run_case(ev, runner=bypassed)
        self.assertEqual(rc, 0, "with the accounting bypassed the missing control must go undetected — " + out)

    # ---- the real extractor on the real runner agrees with its declared call count --------------------------------
    def test_real_extractor_reads_every_declared_call(self):
        src = RUNNER.read_text(encoding="utf-8").splitlines()
        calls = sum(1 for l in src if re.match(r'^[ \t]*ctl "', l))
        labels = [m.group(1) for m in (re.match(r'^[ \t]*ctl "(.*)" (?:yes|no) \\$', l) for l in src) if m]
        self.assertGreater(calls, 0)
        self.assertEqual(len(labels), calls)
        self.assertEqual(len(set(labels)), len(labels), "a label declared twice could not be told apart")
        self.assertIsNotNone(shutil.which("bash"))


if __name__ == "__main__":
    unittest.main()
