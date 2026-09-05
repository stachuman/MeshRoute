#!/usr/bin/env python3
# MeshRoute — tools/test_probe_console_sink.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# ★★ THE AUTO-DISCOVERED WRAPPER FOR tools/probe_console_sink/ (remote-admin v2 slice 0a, [[B208]]).
#
# WHY IT EXISTS. `tools/probe_console_sink/run.sh` has been a committed, hand-run instrument since §B95: nothing in
# `python3 -m unittest discover -s tools` executed it, so a reduction inside it — a dropped profile, a control that
# quietly stopped applying, a check count that fell — could not fail any sweep. Slice 0a made that gap material by
# putting the whole console help behind it. ⇒ this file runs the probe FOR REAL and asserts its DERIVED pins.
#
# ⛔ WHAT IT IS NOT: it does not re-implement a single check. It runs the real runner and asserts four things the
#    runner cannot assert about itself:
#      1. the PINS line's counts — an anti-reduction pin, exact in BOTH directions (see PIN_* below);
#      2. that every negative control actually produced a RED result (no STAYED GREEN / INSTRUMENT FAILURE / NOT
#         APPLIED anywhere in the output);
#      3. that `--no-neg` is visibly probe-only and NEVER prints PASS; and
#      4. that the md5s the binary printed are the md5s of the files on disk right now — i.e. the run measured the
#         checkout, not a copy.
#    Plus the anti-vacuity behaviour of `help_manifest.py`, the module the no-line-lost proof rests on.
"""Wrapper selftests for the §B95/§0a console-sink + help probe."""

from __future__ import annotations

import hashlib
import os
import re
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PROBE_DIR = os.path.join(HERE, "probe_console_sink")

sys.path.insert(0, PROBE_DIR)
import ble_guard as BG       # noqa: E402
import help_manifest as HM   # noqa: E402

# ★ THE DERIVED PINS. Every one of these was COUNTED from a real run, never chosen; each is exact in both directions
#   so that adding a check is a deliberate act and removing one cannot pass silently.
#     profiles   — len(help_manifest.PROFILES); the six real board macro sets.
#     checks     — the summed `N total` the probe binary prints per profile: 166 on the two builds that compile the
#                  OLED preset topic, 160 on the four that do not (6 fewer rows: one topic's H3b..H3e + H7a..H7d
#                  minus its absent-topic H4a). 166*2 + 160*4 = 972.
#     structural — the row count structural.py reports (S1..S20).
#     ble_guard  — the executed BLE help-refusal rows: 53 corpus lines x 4 assertions (B1..B4) = 212.
#     controls   — negctl's own CONTROLS-TOTAL: 8 sink + 5 source + 6 B214 + 10 help behavioural + 8 help
#                  structural/baseline + 3 BLE executed + 2 BLE structural = 42.
PIN_PROFILES = 6
PIN_CHECKS = 972
PIN_STRUCTURAL = 20
PIN_BLE_GUARD = 212
PIN_CONTROLS = 42

UNUSABLE = ("STAYED GREEN", "INSTRUMENT FAILURE", "CONTROL NOT APPLIED", "PROBE BUILD FAILED")


def _md5_8(path):
    with open(path, "rb") as fh:
        return hashlib.md5(fh.read()).hexdigest()[:8]


class TestProbeRunner(unittest.TestCase):
    """The real runner, run for real. ~25 s: the two invocations are shared by every test in the class."""

    full = None
    noneg = None

    @classmethod
    def setUpClass(cls):
        cls.full = subprocess.run(["bash", os.path.join(PROBE_DIR, "run.sh")],
                                  capture_output=True, text=True, cwd=ROOT)
        cls.noneg = subprocess.run(["bash", os.path.join(PROBE_DIR, "run.sh"), "--no-neg"],
                                   capture_output=True, text=True, cwd=ROOT)

    def test_the_default_gate_passes(self):
        self.assertEqual(0, self.full.returncode, self.full.stdout[-4000:] + self.full.stderr[-2000:])
        self.assertIn("PASS: probe + structural + controls all green", self.full.stdout)

    def test_the_derived_pins(self):
        m = re.search(r"PINS profiles=(\d+) checks=(\d+) structural=(\d+) ble_guard=(\d+) controls=(\d+) "
                      r"unusable_controls=(\d+)", self.full.stdout)
        self.assertIsNotNone(m, "the runner must print its derived pins")
        profiles, checks, structural, ble_guard, controls, unusable = (int(g) for g in m.groups())
        self.assertEqual(PIN_BLE_GUARD, ble_guard,
                         "the executed BLE help-refusal check gained or lost rows")
        self.assertEqual(PIN_PROFILES, profiles, "a product profile was added or dropped from the probe matrix")
        self.assertEqual(PIN_CHECKS, checks,
                         "the probe's total check count moved — a reduction must never still report PASS; "
                         "if checks were added deliberately, re-derive PIN_CHECKS from the run")
        self.assertEqual(PIN_STRUCTURAL, structural, "structural.py gained or lost a row")
        self.assertEqual(PIN_CONTROLS, controls, "a negative control was added or removed")
        self.assertEqual(0, unusable)

    def test_the_probe_matrix_matches_the_manifest_matrix(self):
        """One profile list, not two: the runner's PROFILES and help_manifest's must name the same builds."""
        with open(os.path.join(PROBE_DIR, "run.sh"), encoding="utf-8") as fh:
            run_sh = fh.read()
        block = run_sh.split("PROFILES=(", 1)[1].split(")\n", 1)[0]
        named = set(re.findall(r'"([a-z_]+)\|', block))
        self.assertEqual(set(HM.PROFILES), named)

    def test_every_profile_reported_a_clean_content_multiset(self):
        rows = re.findall(r"content multiset (\w+): expected (\d+) / actual (\d+) / (\d+) problem", self.full.stdout)
        self.assertEqual(PIN_PROFILES, len(rows), "every profile must be compared against the frozen baseline")
        for prof, exp, act, prob in rows:
            self.assertEqual("0", prob, f"{prof}: the help content multiset moved")
            self.assertEqual(exp, act, f"{prof}: line count differs")
            self.assertGreater(int(exp), 0, f"{prof}: an EMPTY expectation would compare clean against anything")

    def test_no_control_was_unusable(self):
        for line in self.full.stdout.split("\n"):
            for bad in UNUSABLE:
                self.assertNotIn(bad, line, f"unusable control: {line.strip()}")

    def test_every_response_fits_the_stage_with_headroom_reported(self):
        sizes = re.findall(r"HELP-LARGEST (\S+(?: \S+)?) (\d+) B \(stage (\d+) B\)", self.full.stdout)
        self.assertEqual(PIN_PROFILES, len(sizes))
        for name, n, stage in sizes:
            self.assertLess(int(n), int(stage), f"the largest response ({name}) does not fit the console stage")

    def test_the_run_measured_the_checkout(self):
        want_sink = _md5_8(os.path.join(ROOT, "src", "console_sink.h"))
        want_help = _md5_8(os.path.join(ROOT, "src", "firmware_help.h"))
        self.assertIn(f"sink md5 = {want_sink}", self.full.stdout)
        self.assertIn(f"help md5 = {want_help}", self.full.stdout)
        # PIN_PROFILES per-profile banners + the one the runner prints up front, before any build.
        self.assertEqual(PIN_PROFILES + 1, self.full.stdout.count(f"help md5 = {want_help}"),
                         "every profile build must report the checked-out help header")

    def test_the_ble_refusal_was_measured_not_asserted(self):
        """§0a owner ruling 2026-09-04: help must not be transferred by BLE — and it is EXECUTED, not grepped."""
        self.assertIn("BLE help-refusal (EXECUTED", self.full.stdout)
        m = re.search(r"BLE-GUARD rows=(\d+) checks=(\d+) failed=(\d+)", self.full.stdout)
        self.assertIsNotNone(m, "the BLE guard check must report its own denominators")
        rows, checks, failed = (int(g) for g in m.groups())
        self.assertEqual(0, failed)
        self.assertEqual(PIN_BLE_GUARD, checks)
        self.assertGreaterEqual(rows, 40, "an almost-empty corpus would prove nothing")
        self.assertIn("extracted guard: ", self.full.stdout,
                      "the run must state which condition text it measured")

    def test_no_neg_is_visibly_probe_only_and_never_claims_pass(self):
        self.assertEqual(0, self.noneg.returncode)
        self.assertIn("PROBE-ONLY", self.noneg.stdout)
        self.assertNotIn("PASS:", self.noneg.stdout,
                         "--no-neg must never print a gate result — the controls did not run")
        self.assertNotIn("negative controls (each MUST fail)", self.noneg.stdout)


class TestBleGuard(unittest.TestCase):
    """The BLE refusal's extractor. It is the one link in that proof that is text, so it must be fail-loud."""

    FW = os.path.join(ROOT, "src", "fw_main.cpp")

    def test_the_real_guard_extracts_and_covers_the_whole_help_family(self):
        with open(self.FW, encoding="utf-8") as fh:
            cond = BG.extract_guard(fh.read())
        self.assertIn('"help"', cond)
        self.assertIn("'?'", cond)
        self.assertRegex(cond, r"len\s*>\s*4\s*&&\s*line\s*\[\s*4\s*\]\s*==\s*' '")

    def test_extraction_refuses_when_the_refusal_is_gone(self):
        with open(self.FW, encoding="utf-8") as fh:
            text = fh.read()
        with self.assertRaises(BG.GuardError):
            BG.extract_guard(text.replace(BG.REFUSAL_CALL, "write_err(out, cap, \"help\", \"gone\")"))

    def test_extraction_refuses_a_DUPLICATED_refusal(self):
        with open(self.FW, encoding="utf-8") as fh:
            text = fh.read()
        with self.assertRaises(BG.GuardError):
            BG.extract_guard(text.replace('        return ' + BG.REFUSAL_CALL + ';',
                                          '        return ' + BG.REFUSAL_CALL + ';\n        (void)'
                                          + BG.REFUSAL_CALL + ';', 1))

    def test_a_commented_out_refusal_is_not_extracted(self):
        with open(self.FW, encoding="utf-8") as fh:
            text = fh.read()
        with self.assertRaises(BG.GuardError):
            BG.extract_guard(text.replace('        return ' + BG.REFUSAL_CALL + ';',
                                          '        // return ' + BG.REFUSAL_CALL + ';', 1))

    def test_the_corpus_covers_every_owner_topic_in_the_leaky_shapes(self):
        rows = dict(BG.corpus())
        for t in BG.TOPICS:
            for shape in (f"help {t}", f"help  {t}", f"help {t} x", f"help {t}x"):
                self.assertTrue(rows[shape], f"{shape} must be refused on BLE")
        for through in ("helpful", "hel", "status", ""):
            self.assertFalse(rows[through], f"{through} must NOT be swallowed by the help guard")


class TestHelpManifest(unittest.TestCase):
    """The no-line-lost proof rests on this module; a vacuous comparison here would void the whole gate."""

    BASELINE = os.path.join(PROBE_DIR, "help_baseline.json")

    def test_the_frozen_baseline_covers_every_profile_with_content(self):
        base = HM.load_baseline(self.BASELINE)["profiles"]
        self.assertEqual(set(HM.PROFILES), set(base))
        for name, row in base.items():
            self.assertEqual(HM.PROFILES[name], row["macros"], f"{name}: the frozen macro set drifted")
            self.assertGreater(len(row["content"]), 40, f"{name}: an almost-empty baseline proves nothing")
            self.assertGreater(row["total_bytes"], 4000)

    def test_the_comparison_preserves_MULTIPLICITY(self):
        """A `set` comparison would hide one of two identical lines being dropped. It must be a multiset."""
        self.assertEqual([], HM.compare_content(["a", "a", "b"], ["b", "a", "a"]))
        self.assertEqual(1, len(HM.compare_content(["a", "a", "b"], ["a", "b"])))
        self.assertEqual(1, len(HM.compare_content(["a", "b"], ["a", "a", "b"])))

    def test_an_empty_side_never_compares_clean(self):
        self.assertTrue(HM.compare_content(["x"], []))
        self.assertTrue(HM.compare_content([], ["x"]))

    def test_content_block_refuses_a_missing_marker(self):
        with self.assertRaises(HM.ManifestError):
            HM.content_block("nothing here\n", "gateway")

    def test_content_block_refuses_a_count_mismatch(self):
        out = "HELP-CONTENT-BEGIN gateway 3\n  a\n  b\nHELP-CONTENT-END\n"
        with self.assertRaises(HM.ManifestError):
            HM.content_block(out, "gateway")

    def test_content_block_returns_byte_faithful_lines(self):
        """The probe's stdout is UTF-8; the baseline is byte-per-char. The bridge must not widen a glyph."""
        out = "HELP-CONTENT-BEGIN gateway 1\n  — dash\nHELP-CONTENT-END\n"
        self.assertEqual(["  â dash"], HM.content_block(out, "gateway"))

    def test_the_preprocessor_evaluator_refuses_an_unknown_macro(self):
        with self.assertRaises(HM.ManifestError):
            HM.eval_cond("MR_FEAT_NOT_A_PROFILE_AXIS", HM.PROFILES["gateway"])

    def test_the_preprocessor_evaluator_agrees_with_the_real_gates(self):
        self.assertTrue(HM.eval_cond("MR_N_LAYERS < 2", HM.PROFILES["mobile"]))
        self.assertFalse(HM.eval_cond("MR_N_LAYERS < 2", HM.PROFILES["gateway"]))
        self.assertFalse(HM.eval_cond("MR_FEAT_REMOTE_MGMT", HM.PROFILES["mobile"]))
        self.assertTrue(HM.eval_cond("MR_FEAT_OLED", HM.PROFILES["full_oled"]))

    def test_the_classifier_is_derived_from_the_line_shape(self):
        self.assertEqual("separator", HM.classify(""))
        self.assertEqual("heading", HM.classify("MESSAGING"))
        self.assertEqual("content", HM.classify("  send <id>"))

    def test_rendered_bytes_counts_bytes_not_code_points(self):
        self.assertEqual(5, HM.rendered_bytes("abc"))                 # 3 + CRLF
        self.assertEqual(3, HM.rendered_bytes("abc", terminated=False))
        self.assertEqual(5, HM.rendered_bytes("â "))  # the ⚠ glyph is THREE wire bytes


if __name__ == "__main__":
    unittest.main()
