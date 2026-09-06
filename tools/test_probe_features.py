#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Invocation + anti-vacuity contract for `tools/probe_features/run.sh` (remote-admin v2 slice 1).

WHY A `test_*.py` BESIDE A SHELL PROBE, when the probe already gates itself. Two reasons, both measured
elsewhere in this repository rather than assumed:

  1. **DISCOVERY.** `python3 -m unittest discover -s tools -p "test_*.py"` is the standing sweep this project
     runs; a probe that lives only in a slice's evidence file is a probe the next slice does not run. This file
     is what puts the `mr_features.h` configuration matrix INSIDE that sweep.
  2. **THE PROBE'S OWN HONESTY.** The sibling probes' history is a list of runners that reported PASS while
     measuring nothing: controls documented as "not optional" that the default command skipped; a `|| true`
     that replaced `PIPESTATUS`; a summary line printing a count nobody compared. So the contract asserted here
     is not "the probe passes" — it is *"the probe cannot report success without having measured"*.

⛔ FOUR ARMS DELIBERATELY SABOTAGE THE RUNNER — `CXX=/bin/false` plus `MR_PROBE_DROP=cell|check|control` —
   because *"dropping a required configuration, check or control cannot preserve PASS"* is exactly the property
   that cannot be proved by reading a script.

★ THE SLICE-1 FENCE IS ASSERTED HERE, not merely promised in a brief: `MR_FEAT_RADMIN_*` must be named by
  EXACTLY ONE production file. ⓘ WHEN THE FIRST CONSUMER LANDS (a later slice), `test_the_pair_has_no_consumer`
  is the assertion that must be DELIBERATELY updated — that is its job, not an obstacle to route around.

RUN:  python3 -m unittest discover -s tools -p "test_*.py"
      python3 tools/test_probe_features.py
"""

from __future__ import annotations

import os
import re
import subprocess
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PROBE = HERE / "probe_features"
RUN = PROBE / "run.sh"
MAIN = PROBE / "probe_main.cpp"
ENVMAP = PROBE / "envmap.py"
MUTATE = PROBE / "mutate.py"
HDR = ROOT / "lib" / "core" / "mr_features.h"


def run(*args: str, env_extra: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    env = dict(os.environ)
    if env_extra:
        env.update(env_extra)
    return subprocess.run([str(RUN), *args], capture_output=True, text=True, env=env, timeout=1800)


class ProbeFilesExist(unittest.TestCase):
    def test_the_runner_and_its_support_files_are_present_and_executable(self) -> None:
        for path in (RUN, MAIN, ENVMAP, MUTATE):
            self.assertTrue(path.is_file(), f"{path} is missing — the gate cannot be run")
        self.assertTrue(os.access(RUN, os.X_OK), f"{RUN} is not executable")

    def test_no_arduino_fake_was_forked(self) -> None:
        """U1. The header consumes DEFINES, not a device API, so this probe needs no Arduino fake at all — and
        must not grow one: a second fake is how two probes end up measuring two different Arduinos."""
        self.assertFalse((PROBE / "fakes").exists(), "probe_features must not carry fakes; it compiles defines")
        self.assertNotIn("fakes", RUN.read_text(encoding="utf-8"))


class ItCompilesTheRealProductionHeader(unittest.TestCase):
    def test_the_driver_includes_the_live_header_not_a_copy(self) -> None:
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "mr_features.h"', text)
        self.assertIn("MR_FEAT_RADMIN_CLIENT", text)
        self.assertIn("MR_FEAT_RADMIN_ACCEPT", text)

    def test_the_runner_points_the_include_path_at_lib_core(self) -> None:
        text = RUN.read_text(encoding="utf-8")
        self.assertIn('HDR="$ROOT/lib/core/mr_features.h"', text)
        self.assertIn('-I"$ROOT/lib/core"', text)

    def test_the_driver_derives_no_expectation_from_the_header(self) -> None:
        """⛔ The one way this probe could become a model of itself: computing the expected value from the
        measured one. Every expectation must arrive as an EXP_* macro from the runner's ruled table, and the
        driver must hold NO copy of the derivation it is checking."""
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn("EXP_RADMIN_CLIENT", text)
        self.assertIn("EXP_RADMIN_ACCEPT", text)
        self.assertNotRegex(text, r"#\s*define\s+MR_FEAT_", "the driver must not define any MR_FEAT_* itself")
        self.assertNotIn("defined(ARDUINO)", text, "the driver must not re-implement the board/host discriminator")
        self.assertNotRegex(text, r"#\s*define\s+EXP_", "expectations come from the runner, never from this file")

    def test_the_header_under_test_is_the_one_this_slice_changed(self) -> None:
        text = HDR.read_text(encoding="utf-8")
        self.assertIn("MR_FEAT_RADMIN_CLIENT", text)
        self.assertIn("MR_FEAT_RADMIN_ACCEPT", text)
        self.assertIn("defined(ARDUINO)", text)


class TheSliceOneFence(unittest.TestCase):
    """The scaffold's whole claim is that it is inert. These two assertions are what make that checkable."""

    def test_the_pair_has_no_consumer(self) -> None:
        hits = sorted(
            str(p.relative_to(ROOT))
            for base in ("lib", "src", "test")
            for p in (ROOT / base).rglob("*")
            if p.is_file() and "MR_FEAT_RADMIN" in p.read_text(encoding="utf-8", errors="ignore")
        )
        self.assertEqual(hits, ["lib/core/mr_features.h"],
                         "slice 1 is a pure scaffold: exactly one production file may name the pair")

    def test_the_pair_has_no_command_line_override_surface(self) -> None:
        """R-RA-26 forbids a configuration override: an invalid pair must be underivable, not merely unusual."""
        self.assertNotRegex(HDR.read_text(encoding="utf-8"), r"(?m)^#ifndef\s+MR_FEAT_RADMIN")


class PinsAreEnforcedNotPrinted(unittest.TestCase):
    """A pinned count that is printed but never compared is the exact defect the pins exist to prevent."""

    def setUp(self) -> None:
        self.text = RUN.read_text(encoding="utf-8")

    def test_all_three_pins_are_declared_with_a_written_derivation(self) -> None:
        for pin in ("PIN_CELLS", "PIN_CHECKS", "PIN_CONTROLS"):
            self.assertRegex(self.text, rf"(?m)^{pin}=\d+$")
        self.assertIn("derived by running the clean matrix", self.text)

    def test_all_three_pins_are_compared_and_set_rc(self) -> None:
        self.assertIn('[ "$n_cells" -eq "$PIN_CELLS" ]', self.text)
        self.assertIn('[ "$n_checks" -eq "$PIN_CHECKS" ]', self.text)
        self.assertIn('[ "$n_ctl" -eq "$PIN_CONTROLS" ]', self.text)
        self.assertIn('[ "$n_fail" -eq 0 ]', self.text)
        self.assertIn('[ "$envmap_rc" -eq 0 ]', self.text)

    def test_the_controls_run_by_default(self) -> None:
        """The sibling probes' documented trap: controls described as not-optional while the default command
        skipped them. The guard must therefore be `!= --no-neg`, i.e. opt-OUT."""
        self.assertIn('if [ "${1:-}" != "--no-neg" ]; then', self.text)


class TheGateRuns(unittest.TestCase):
    """The executed half: one default run (the real gate) and one `--no-neg` run."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.gate = run()
        cls.probe_only = run("--no-neg")

    def test_the_default_run_is_the_gate_and_passes(self) -> None:
        self.assertEqual(self.gate.returncode, 0, self.gate.stdout[-4000:])
        self.assertIn("\nPASS", self.gate.stdout)

    def test_it_executed_its_pinned_cells_checks_and_controls(self) -> None:
        self.assertRegex(self.gate.stdout,
                         r"matrix: (\d+) configuration cells \(pin \1\), (\d+) checks .*0 failed \(pin \2\)")
        self.assertRegex(self.gate.stdout, r"controls: (\d+) verified / 0 unusable \(pin \1\)")

    def test_the_classification_is_declared_before_any_control_runs(self) -> None:
        """A classification decided after seeing the outcome is not a classification."""
        out = self.gate.stdout
        self.assertIn("control classification (declared up-front", out)
        self.assertLess(out.index("control classification (declared up-front"),
                        out.index("== class A —"), "the classification must precede the first control")

    def test_every_declared_control_class_actually_fired(self) -> None:
        out = self.gate.stdout
        for prefix, minimum in (("A", 4), ("B", 6), ("C", 5), ("X", 4)):
            fired = len(re.findall(rf"(?m)^  ok   {prefix}\d ", out))
            self.assertGreaterEqual(fired, minimum, f"class {prefix}: {fired} controls fired\n{out[-4000:]}")

    def test_the_refusals_are_attributed_to_the_headers_own_diagnostics(self) -> None:
        """⛔ A compile failure is the declared RED for class A ONLY because it is matched to a diagnostic
        EXTRACTED from the production header. If that extraction stopped working the controls would be blind."""
        self.assertIn("3 distinct board-only #error diagnostics (extracted, not typed here)", self.gate.stdout)
        self.assertRegex(self.gate.stdout, r"A1 .*-> REFUSED at `both`")
        self.assertRegex(self.gate.stdout, r"A2 .*-> REFUSED at `neither`")
        self.assertRegex(self.gate.stdout, r"A3 .*-> REFUSED at `consistency`")
        self.assertRegex(self.gate.stdout, r"A4 .*-> REFUSED at `consistency`")

    def test_the_simulator_gateway_keeps_both_endpoints_and_that_is_measured(self) -> None:
        """The one cell no profile-only reading of R-RA-17 would have got right."""
        self.assertIn("derived: host_lus_gateway TEAM=1 MOBILE=1 MOBILE_HOST=1 GATEWAY=1 OLED=0 "
                      "REMOTE_MGMT=1 RADMIN_CLIENT=1 RADMIN_ACCEPT=1", self.gate.stdout)
        self.assertRegex(self.gate.stdout, r"B3 .*-> RED on host_lus_gateway")

    def test_the_environment_mapping_is_derived_not_typed(self) -> None:
        self.assertIn("all 14 envs map into the probe matrix", self.gate.stdout)
        self.assertIn("no-profile STATIC PRODUCT envs = 5", self.gate.stdout)

    def test_the_tree_is_not_modified_by_a_run(self) -> None:
        self.assertIn("tree unchanged:", self.gate.stdout)

    def test_no_neg_never_reports_pass(self) -> None:
        self.assertEqual(self.probe_only.returncode, 0, self.probe_only.stdout[-4000:])
        self.assertIn("PROBE-ONLY — NOT A GATE", self.probe_only.stdout)
        self.assertNotIn("\nPASS", self.probe_only.stdout)
        self.assertNotIn("== class A —", self.probe_only.stdout)


class ItCannotReportSuccessWithoutMeasuring(unittest.TestCase):
    """⛔ THE ANTI-VACUITY ARMS, EXECUTED."""

    def test_a_broken_toolchain_fails_loudly(self) -> None:
        res = run(env_extra={"CXX": "/bin/false"})
        self.assertNotEqual(res.returncode, 0, res.stdout[-2000:])
        self.assertNotIn("\nPASS", res.stdout)
        self.assertIn("MATRIX BUILD FAILED", res.stdout)

    def test_dropping_a_configuration_cannot_preserve_pass(self) -> None:
        res = run(env_extra={"MR_PROBE_DROP": "cell"})
        self.assertNotEqual(res.returncode, 0)
        self.assertNotIn("\nPASS", res.stdout)
        self.assertIn("CELL COUNT MOVED", res.stdout)

    def test_dropping_a_check_cannot_preserve_pass(self) -> None:
        res = run(env_extra={"MR_PROBE_DROP": "check"})
        self.assertNotEqual(res.returncode, 0)
        self.assertNotIn("\nPASS", res.stdout)
        self.assertIn("CHECK COUNT MOVED", res.stdout)

    def test_dropping_a_control_cannot_preserve_pass(self) -> None:
        res = run(env_extra={"MR_PROBE_DROP": "control"})
        self.assertNotEqual(res.returncode, 0)
        self.assertNotIn("\nPASS", res.stdout)
        self.assertIn("CONTROL COUNT MOVED", res.stdout)


class TheMutatorIsSingleMatch(unittest.TestCase):
    """Every control injects EXACTLY ONE change. The production derivation deliberately spells the same line
    twice, so an unverified `sed` would silently hit the wrong arm."""

    def _mutate(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(["python3", str(MUTATE), *args], capture_output=True, text=True, timeout=120)

    def test_a_multi_match_find_is_refused(self) -> None:
        out = ROOT / ".probe_features_should_never_exist.h"
        res = self._mutate("--in", str(HDR), "--out", str(out),
                           "--find", "#  define MR_FEAT_RADMIN_CLIENT 1",
                           "--replace", "#  define MR_FEAT_RADMIN_CLIENT 0", "--expect", "1")
        self.assertNotEqual(res.returncode, 0, res.stdout)
        self.assertIn("matched 2 line(s), expected 1", res.stderr)
        self.assertFalse(out.exists(), "a refused mutation must write nothing")

    def test_a_vacuous_find_is_refused(self) -> None:
        out = ROOT / ".probe_features_should_never_exist.h"
        res = self._mutate("--in", str(HDR), "--out", str(out),
                           "--find", "#  define MR_FEAT_NO_SUCH_THING 7",
                           "--replace", "x", "--expect", "1")
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("matched 0 line(s), expected 1", res.stderr)
        self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
