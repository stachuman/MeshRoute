#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Invocation + anti-vacuity contract for `tools/probe_ble_line/run.sh` (remote-admin v2 Slice 0f).

WHY A `test_*.py` BESIDE A SHELL PROBE, when the probe already gates itself. Two reasons, both measured
elsewhere in this repo rather than assumed:

  1. **DISCOVERY.** `python3 -m unittest discover -s tools -p "test_*.py"` is the standing sweep this project
     runs; a probe that lives only in a slice's evidence file is a probe the next slice does not run. This file
     is what puts the BLE line-capacity gate INSIDE that sweep.
  2. **THE PROBE'S OWN HONESTY.** The sibling probes' history is a list of runners that reported PASS while
     measuring nothing: controls documented as "not optional" that the default command skipped; a `|| true`
     that replaced `PIPESTATUS`; a summary line printing a count nobody compared. So the contract asserted here
     is not "the probe passes" — it is *"the probe cannot report success without having measured"*.

⛔ ONE ARM DELIBERATELY BREAKS THE TOOLCHAIN (`CXX=/bin/false`), because *"a build failure is FAILING, never
   measured"* ([[B237]]) is exactly the property that cannot be proved by reading a script.

RUN:  python3 -m unittest discover -s tools -p "test_*.py"
      python3 tools/test_probe_ble_line.py
"""

from __future__ import annotations

import os
import subprocess
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
RUN = HERE / "probe_ble_line" / "run.sh"
MAIN = HERE / "probe_ble_line" / "probe_main.cpp"
HDR = ROOT / "src" / "device_ble.h"


def run(*args: str, env_extra: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    env = dict(os.environ)
    if env_extra:
        env.update(env_extra)
    return subprocess.run([str(RUN), *args], capture_output=True, text=True, env=env, timeout=1800)


class ProbeFilesExist(unittest.TestCase):
    def test_the_runner_and_its_tu_are_present_and_executable(self) -> None:
        self.assertTrue(RUN.is_file(), f"{RUN} is missing — the gate cannot be run")
        self.assertTrue(MAIN.is_file(), f"{MAIN} is missing")
        self.assertTrue(os.access(RUN, os.X_OK), f"{RUN} is not executable")

    def test_it_reuses_the_one_arduino_fake_and_forks_none(self) -> None:
        """U1: `tools/probe_console_sink/fakes/Arduino.h` is the ONE Arduino fake. A second one is how two probes
        end up measuring two different Arduinos. This probe's own `fakes/` may hold ONLY the platform surface no
        existing fake provides — Bluefruit and the two Nordic stubs."""
        text = RUN.read_text(encoding="utf-8")
        self.assertIn("probe_console_sink/fakes", text)
        own = {p.name for p in (HERE / "probe_ble_line" / "fakes").iterdir()}
        self.assertEqual(own, {"bluefruit.h", "nrf.h", "nrf_soc.h"}, f"unexpected fake(s): {own}")

    def test_the_fakes_hold_no_intake_decision(self) -> None:
        """The probe is a gate only because the LINE MACHINE is the real header's. A fake that grew a line
        buffer, a newline rule or a length limit would make it a model of itself."""
        for name in ("bluefruit.h", "nrf.h", "nrf_soc.h"):
            text = (HERE / "probe_ble_line" / "fakes" / name).read_text(encoding="utf-8")
            for forbidden in ("g_line", "g_pos", "g_overflow", "line_too_long"):
                self.assertNotIn(forbidden, text, f"{name} must not know about {forbidden}")


class ItCompilesTheRealProductionHeader(unittest.TestCase):
    def test_the_tu_includes_the_live_header_not_a_copy(self) -> None:
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "device_ble.h"', text)
        self.assertIn("mrble::service_rx()", text)
        self.assertIn("mrble::begin(", text)

    def test_the_runner_points_the_include_path_at_src(self) -> None:
        text = RUN.read_text(encoding="utf-8")
        self.assertIn('HDR="$ROOT/src/device_ble.h"', text)
        self.assertIn('-I"$ROOT/src"', text)

    def test_the_header_under_test_is_the_one_this_slice_changed(self) -> None:
        self.assertTrue(HDR.is_file())
        self.assertIn("kLineStorageBytes", HDR.read_text(encoding="utf-8"))


class PinsAreEnforcedNotPrinted(unittest.TestCase):
    """A pinned count that is printed but never compared is the exact defect the pins exist to prevent."""

    def setUp(self) -> None:
        self.text = RUN.read_text(encoding="utf-8")

    def test_both_pins_are_declared_with_a_written_derivation(self) -> None:
        self.assertRegex(self.text, r"(?m)^PIN_CHECKS=\d+$")
        self.assertRegex(self.text, r"(?m)^PIN_CONTROLS=\d+$")
        self.assertIn("derived by running the clean probe", self.text)

    def test_both_pins_are_compared_and_set_rc(self) -> None:
        self.assertIn('[ "$n_checks" -eq "$PIN_CHECKS" ]', self.text)
        self.assertIn('[ "$n_ctl" -eq "$PIN_CONTROLS" ]', self.text)
        self.assertIn('[ "$n_probe_fail" -eq 0 ]', self.text)
        self.assertIn('[ "$probe_rc" -eq 0 ]', self.text)

    def test_the_controls_run_by_default(self) -> None:
        """The sibling probes' documented trap: controls described as not-optional while the default command
        skipped them. The guard must therefore be `!= --no-neg`, i.e. opt-OUT."""
        self.assertIn('if [ "${1:-}" != "--no-neg" ]; then', self.text)

    def test_pipestatus_is_read_bare(self) -> None:
        """`... | tee ... || true` REPLACES PIPESTATUS with (0), which reads a failing probe as a passing one."""
        self.assertIn("probe_rc=${PIPESTATUS[0]}", self.text)
        self.assertNotIn('tee "$OUT/probe.out" || true', self.text)


class TheGateRuns(unittest.TestCase):
    """The executed half. One default run (the real gate) and one `--no-neg` run."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.gate = run()
        cls.probe_only = run("--no-neg")

    def test_the_default_run_is_the_gate_and_passes(self) -> None:
        self.assertEqual(self.gate.returncode, 0, self.gate.stdout[-4000:])
        self.assertIn("\nPASS", self.gate.stdout)

    def test_the_default_run_actually_executed_its_pinned_checks_and_controls(self) -> None:
        self.assertRegex(self.gate.stdout, r"probe: (\d+) checks .*0 failed \(pin \1\)")
        self.assertRegex(self.gate.stdout, r"controls: (\d+) verified / 0 unusable \(pin \1\)")
        reds = [ln for ln in self.gate.stdout.splitlines() if "-> RED (" in ln]
        self.assertGreaterEqual(len(reds), 7, self.gate.stdout[-4000:])

    def test_the_b237_control_of_the_controls_fired(self) -> None:
        self.assertIn("a real SIGSEGV classifies as UNUSABLE", self.gate.stdout)

    def test_the_derived_capacity_is_reported_not_assumed(self) -> None:
        """The probe must PRINT the derivation it measured, so the evidence quotes a measurement."""
        self.assertRegex(self.gate.stdout, r"derived: send=\d+\s+send_layer=\d+.*=>\s+storage=\d+")

    def test_the_source_integrity_pin_can_actually_fail(self) -> None:
        """N7 is the only thing standing between this probe and 'compile a copy, inspect the live tree'."""
        self.assertRegex(self.gate.stdout, r"N7 .*-> RED \(1 check\(s\) failed: A1")

    def test_the_tree_is_not_modified_by_a_run(self) -> None:
        self.assertIn("tree unchanged:", self.gate.stdout)

    def test_no_neg_never_reports_pass(self) -> None:
        self.assertEqual(self.probe_only.returncode, 0, self.probe_only.stdout[-4000:])
        self.assertIn("PROBE-ONLY — NOT A GATE", self.probe_only.stdout)
        self.assertNotIn("\nPASS", self.probe_only.stdout)
        self.assertNotIn("negative controls", self.probe_only.stdout)


class ItCannotReportSuccessWithoutMeasuring(unittest.TestCase):
    """⛔ THE ANTI-VACUITY ARM, EXECUTED. With a compiler that cannot build anything, the runner must FAIL and
    must never print PASS — a probe that says "0 failed" over zero executed checks is the defect."""

    def test_a_broken_toolchain_fails_loudly(self) -> None:
        res = run(env_extra={"CXX": "/bin/false"})
        self.assertNotEqual(res.returncode, 0, res.stdout[-2000:])
        self.assertNotIn("\nPASS", res.stdout)
        self.assertIn("BUILD FAILED", res.stdout)


if __name__ == "__main__":
    unittest.main()
