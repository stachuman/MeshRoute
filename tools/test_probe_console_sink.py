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
#      1. the PINS line's counts — an anti-reduction pin, exact in BOTH directions (see PIN_* below), AND that the
#         runner now applies the same judgment itself before PASS ([[B294]]);
#      2. that every negative control actually produced a RED result (no STAYED GREEN / INSTRUMENT FAILURE / NOT
#         APPLIED anywhere in the output);
#      3. that `--no-neg` is visibly probe-only and NEVER prints PASS; and
#      4. that the md5s the binary printed are the md5s of the files on disk right now — i.e. the run measured the
#         checkout, not a copy.
#    Plus the anti-vacuity behaviour of the §0g PROJECTION ORACLE (`gen_command_inventory.primary_names`), which is
#    what the no-command-lost proof now rests on since [[B291]] retired the frozen `help_baseline.json`.
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
sys.path.insert(0, HERE)
import ble_guard as BG              # noqa: E402
import gen_command_inventory as GEN  # noqa: E402

# ★ THE DERIVED PINS. Every one of these was COUNTED from a real run, never chosen; each is exact in both directions
#   so that adding a check is a deliberate act and removing one cannot pass silently.
# ★★ §0g/[[B294]]: THE RUNNER NOW ENFORCES THE SAME PINS ITSELF, before it prints PASS. This file is deliberately
#   kept as an INDEPENDENT SECOND READER — it re-derives the numbers from the runner's stdout and additionally pins
#   that the runner performs that judgment (test_the_runner_enforces_its_own_pins).
#     profiles   — len(gen_command_inventory.PROFILES); the six real board macro sets.
#     checks     — the summed `N total` the probe binary prints per profile: 120 on EVERY profile (52 §B95 sink rows
#                  + 68 §0g help rows), so 120*6 = 720. §0a's 972 is retired with the per-topic rows that scaled it.
#     structural — the row count structural.py reports (S1..S29; §0b/[[B279]] added S21, the boot sink owner;
#                  §RADMIN-0c added S22..S29 — the two one-call `fw_main.cpp` adapters, the seam's ROUTER-FIRST
#                  single fork, its refusal to re-choose a sink, the no-borrowed-body/no-static rule, the HEX send
#                  handle no executed check can see, and the two [[B298]] comment-census rows).
#     ble_guard  — the executed BLE help-refusal rows: 53 corpus lines x 4 assertions (B1..B4) = 212.
#     ownership  — §RADMIN-0c: one ownership.py row per REAL product profile (6), each requiring the router-owned
#                  and parser-owned primary-form sets to be DISJOINT, non-empty, complete and at their counts.
#     own_ctl    — ownership.py's own controls (3): a synthetic collision, a deleted router form, an emptied
#                  parser surface — each REFUSED.
#     controls   — negctl's own CONTROLS-TOTAL: 8 sink + 16 source + 6 B214 + 13 help rendered-index + 2 help
#                  structural + 3 router + 5 oracle + 3 BLE executed + 2 BLE structural = 58. (§0b/[[B279]] took
#                  the source family 5 -> 6 with X12, the control that reddens S21; §RADMIN-0c took it 6 -> 16 with
#                  X13..X22, the controls that redden S22..S29.)
PIN_PROFILES = 6
PIN_CHECKS = 720
PIN_STRUCTURAL = 29
PIN_BLE_GUARD = 212
PIN_OWNERSHIP = 6
PIN_OWN_CTL = 3
PIN_CONTROLS = 58

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
        m = re.search(r"PINS profiles=(\d+) checks=(\d+) structural=(\d+) ble_guard=(\d+) ownership=(\d+) "
                      r"ownership_controls=(\d+) controls=(\d+) unusable_controls=(\d+)", self.full.stdout)
        self.assertIsNotNone(m, "the runner must print its derived pins")
        (profiles, checks, structural, ble_guard, ownership, own_ctl,
         controls, unusable) = (int(g) for g in m.groups())
        self.assertEqual(PIN_OWNERSHIP, ownership,
                         "the router/parser ownership gate lost or gained a product profile")
        self.assertEqual(PIN_OWN_CTL, own_ctl,
                         "an ownership control (collision / missing form / emptied surface) was dropped")
        self.assertEqual(PIN_BLE_GUARD, ble_guard,
                         "the executed BLE help-refusal check gained or lost rows")
        self.assertEqual(PIN_PROFILES, profiles, "a product profile was added or dropped from the probe matrix")
        self.assertEqual(PIN_CHECKS, checks,
                         "the probe's total check count moved — a reduction must never still report PASS; "
                         "if checks were added deliberately, re-derive PIN_CHECKS from the run")
        self.assertEqual(PIN_STRUCTURAL, structural, "structural.py gained or lost a row")
        self.assertEqual(PIN_CONTROLS, controls, "a negative control was added or removed")
        self.assertEqual(0, unusable)

    def test_the_probe_matrix_matches_the_generator_matrix(self):
        """One profile list, not two: the runner's PROFILES and the generator's must name the same builds."""
        with open(os.path.join(PROBE_DIR, "run.sh"), encoding="utf-8") as fh:
            run_sh = fh.read()
        block = run_sh.split("PROFILES=(", 1)[1].split(")\n", 1)[0]
        named = set(re.findall(r'"([a-z_]+)\|', block))
        self.assertEqual(set(GEN.PROFILES), named)

    def test_every_profile_was_compared_against_the_source_projection(self):
        """§0g: the oracle is the GENERATED INVENTORY, and every profile must actually have been compared to it."""
        rows = re.findall(r"primary names (\w+): (\d+) rendered == (\d+) projected", self.full.stdout)
        self.assertEqual(PIN_PROFILES, len(rows), "every profile must be compared against the inventory projection")
        for prof, rendered, projected in rows:
            self.assertEqual(rendered, projected, f"{prof}: rendered and projected name counts differ")
            self.assertGreater(int(projected), 0,
                               f"{prof}: an EMPTY projection would compare clean against anything")
        # ...and the numbers the runner reported must be the ones the generator produces right now, independently.
        for prof, rendered, _projected in rows:
            rows_now, _n, _v, _r = GEN.build_rows(ROOT)
            self.assertEqual(int(rendered), len(GEN.primary_names(rows_now, GEN.PROFILES[prof])),
                             f"{prof}: the runner's count disagrees with a fresh projection")

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

    def test_the_runner_enforces_its_own_pins(self):
        """[[B294]]: running the gate DIRECTLY must refuse a shrunken measurement instead of printing PASS.

        The selftest drops one profile, so every probe binary the child launched still prints `0 failed` while the
        observed profile/check counts fall — the exact hole where a green probe used to carry a green gate.
        """
        r = subprocess.run(["bash", os.path.join(PROBE_DIR, "run.sh"), "--pin-selftest"],
                           capture_output=True, text=True, cwd=ROOT)
        self.assertEqual(0, r.returncode, r.stdout[-4000:] + r.stderr[-2000:])
        self.assertIn("SELFTEST OK", r.stdout)
        self.assertNotIn("PASS: probe + structural + controls all green", r.stdout,
                         "the selftest must never share the gate's PASS wording")
        self.assertIn("STILL GREEN, runner refused", r.stdout,
                      "the F4 shape — green probe binaries, shrunken coverage — must be demonstrated")

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
        for t in BG.RETIRED_TOPIC_WORDS:
            for shape in (f"help {t}", f"help  {t}", f"help {t} x", f"help {t}x"):
                self.assertTrue(rows[shape], f"{shape} must be refused on BLE")
        for through in ("helpful", "hel", "status", ""):
            self.assertFalse(rows[through], f"{through} must NOT be swallowed by the help guard")


class TestPrimaryProjectionOracle(unittest.TestCase):
    """§0g: the completeness proof now rests on the GENERATED projection, so a vacuous oracle would void the gate.

    (This class replaces TestHelpManifest, retired with `help_manifest.py`/`help_baseline.json` under [[B291]].)
    """

    @classmethod
    def setUpClass(cls):
        cls.rows, _n, _v, _r = GEN.build_rows(ROOT)

    def test_every_profile_projects_a_non_empty_sorted_unique_name_set(self):
        for prof, macros in GEN.PROFILES.items():
            names = GEN.primary_names(self.rows, macros)
            self.assertGreater(len(names), 40, f"{prof}: an almost-empty projection proves nothing")
            self.assertEqual(sorted(set(names), key=lambda n: n.encode()), names,
                             f"{prof}: the projection must be bytewise sorted and unique")

    def test_an_empty_row_set_REFUSES_instead_of_projecting_nothing(self):
        with self.assertRaises(GEN.GeneratorError):
            GEN.primary_projection([], GEN.PROFILES["full_headless"])

    def test_the_gate_evaluator_refuses_a_macro_that_is_not_a_profile_axis(self):
        with self.assertRaises(GEN.GeneratorError):
            GEN.eval_gate("MR_FEAT_NOT_A_PROFILE_AXIS", GEN.PROFILES["gateway"])

    def test_the_gate_evaluator_agrees_with_the_real_gates(self):
        self.assertTrue(GEN.eval_gate("MR_N_LAYERS < 2", GEN.PROFILES["mobile"]))
        self.assertFalse(GEN.eval_gate("MR_N_LAYERS < 2", GEN.PROFILES["gateway"]))
        self.assertFalse(GEN.eval_gate("MR_FEAT_REMOTE_MGMT", GEN.PROFILES["mobile"]))
        self.assertTrue(GEN.eval_gate("MR_FEAT_OLED", GEN.PROFILES["full_oled"]))

    def test_the_feature_gates_really_separate_the_profiles(self):
        """A projection identical on every profile would silently stop testing availability at all."""
        full = set(GEN.primary_names(self.rows, GEN.PROFILES["full_oled"]))
        gw = set(GEN.primary_names(self.rows, GEN.PROFILES["gateway"]))
        mob = set(GEN.primary_names(self.rows, GEN.PROFILES["mobile"]))
        self.assertEqual({"mobile", "team", "ui"}, full - gw)
        self.assertEqual({"lock", "password", "unlock", "ui"}, full - mob)

    def test_the_punctuation_alias_is_not_a_second_command(self):
        names = GEN.primary_names(self.rows, GEN.PROFILES["full_headless"])
        self.assertIn("help", names)
        self.assertNotIn("?", names)

    def test_the_console_token_parser_surface_is_included(self):
        """Surface 3's seven names never reach dispatch(); dropping that surface loses them silently (42 vs 49)."""
        names = set(GEN.primary_names(self.rows, GEN.PROFILES["full_headless"]))
        for n in ("send", "send_channel", "send_layer", "peerkey", "peername", "reqpubkey", "resolve"):
            self.assertIn(n, names)


if __name__ == "__main__":
    unittest.main()
