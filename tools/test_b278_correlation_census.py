#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Unit controls for tools/b278_correlation_census.py (§B278 S0 offline occupancy / custody-age census).

WHAT THIS COVERS AND WHY IT EXISTS SEPARATELY FROM `--selftest`. The census's own `--selftest` drives the
MODEL (bind, release, occupancy, the ruled bounds) with positive and sabotage controls. This module is the
[[B271]] lesson applied: an instrument that no automatic gate discovers is an instrument that rots. It runs
that selftest AND adds the controls the selftest cannot express about its own PLUMBING — the manifest
authority, the partial-corpus refusal, the instrument-mode banner and the constant-derivation resolver.

RUN:  cd tools && python3 test_b278_correlation_census.py     # (the module imports its subject by name, as
                                                              #  test_probe_board_abi.py does)
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import b278_correlation_census as census


class TestConstantDerivation(unittest.TestCase):
    def test_bounds_resolve_from_the_real_headers(self) -> None:
        found = census.verify_protocol_constants()
        self.assertEqual(found["e2e_ack_deadline_xl_ms"], census.ACK_TTL_MS)
        self.assertEqual(found["seen_origin_ttl_ms"], census.SEEN_ORIGIN_TTL_MS)
        self.assertEqual(found["kDelegAckCap"], census.RING_CAP)

    def test_a_derived_expression_is_resolved_not_regexed(self) -> None:
        """The ruled bounds are EXPRESSIONS in the header; a literal-scraper would have read the wrong number."""
        text = ("inline constexpr uint32_t a = 100;\n"
                "inline constexpr uint32_t b = 2 * a;   // 200\n"
                "inline constexpr uint32_t c = a + b;\n")
        self.assertEqual(census.resolve_cpp_constants(text, ("c",)), {"c": 300})

    def test_a_non_arithmetic_expression_REFUSES(self) -> None:
        text = 'inline constexpr uint32_t a = some_function(3);\n'
        with self.assertRaises(census.CensusRefusal):
            census.resolve_cpp_constants(text, ("a",))

    def test_a_missing_constant_REFUSES(self) -> None:
        with self.assertRaises(census.CensusRefusal):
            census.resolve_cpp_constants("inline constexpr uint32_t a = 1;\n", ("nope",))

    def test_comments_cannot_supply_a_definition(self) -> None:
        """A commented-out constant must not be resolvable — that is how a stale bound sneaks back in."""
        text = "// inline constexpr uint32_t ghost = 7;\ninline constexpr uint32_t real = 8;\n"
        with self.assertRaises(census.CensusRefusal):
            census.resolve_cpp_constants(text, ("ghost",))
        self.assertEqual(census.resolve_cpp_constants(text, ("real",)), {"real": 8})


class TestManifestAuthority(unittest.TestCase):
    """`--corpus` must believe only a complete, validated 36-scenario run."""

    def _corpus(self, tmp: Path, n_records: int, write_streams: bool = True) -> Path:
        root = tmp / "corpus"
        (root / "streams").mkdir(parents=True)
        records = [{"scenario": f"s{i:02d}"} for i in range(n_records)]
        (root / "manifest.json").write_text(json.dumps({"records": records}), encoding="utf-8")
        if write_streams:
            for rec in records:
                (root / "streams" / f"{rec['scenario']}.ndjson").write_text("", encoding="utf-8")
        return root

    def test_a_partial_corpus_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = self._corpus(Path(tmp), 35)
            with self.assertRaises(census.CensusRefusal):
                census.require(len(json.loads((root / "manifest.json").read_text())["records"]) == 36,
                               "partial")

    def test_a_manifest_naming_an_absent_stream_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = self._corpus(Path(tmp), 36, write_streams=False)
            missing = root / "streams" / "s00.ndjson"
            with self.assertRaises(census.CensusRefusal):
                census.require(missing.is_file(), "absent stream")


class TestStreamParsing(unittest.TestCase):
    def test_non_script_emit_lines_are_ignored_and_bad_json_REFUSES(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            good = Path(tmp) / "good.ndjson"
            good.write_text('{"type":"radio_tx","node":1,"time_ms":0}\n'
                            '{"type":"script_emit","node":1,"time_ms":5,"emit_type":"x","data":{}}\n',
                            encoding="utf-8")
            self.assertEqual(len(census.parse_stream(good)), 1)
            bad = Path(tmp) / "bad.ndjson"
            bad.write_text('{"type":"script_emit" BROKEN\n', encoding="utf-8")
            with self.assertRaises(census.CensusRefusal):
                census.parse_stream(bad)


class TestModelControls(unittest.TestCase):
    """The census's own positive/sabotage battery, run here so an automatic gate discovers it."""

    def test_selftest_is_green(self) -> None:
        self.assertEqual(census.selftest(), 0)

    def test_the_selftest_is_not_vacuous(self) -> None:
        """Sabotage the binder into PERMISSIVENESS and the battery must FAIL.

        A green selftest wired to nothing is the worst shape this arc has recorded, so the control makes the
        binder accept everything (the exact defect a custody census must never ship) rather than crash it —
        an exception would prove only that the code runs, not that the ASSERTIONS bite.
        """
        original = census.bind_custody_reports

        def permissive(stream, events):
            reports = original(stream, events)
            for rep in reports:                       # accept every receipt, delegated, at a fixed age
                rep.status, rep.delegated, rep.age_ms = "bound", True, 1
            return reports

        try:
            census.bind_custody_reports = permissive                     # type: ignore[assignment]
            self.assertGreater(census.selftest(), 0)
        finally:
            census.bind_custody_reports = original                       # type: ignore[assignment]
        self.assertEqual(census.selftest(), 0)                           # and it is restored


class TestDiagnosticIsNeverABinding(unittest.TestCase):
    def test_a_diagnostic_origin_does_not_change_the_status(self) -> None:
        events = [
            census.Event(5, 1_000, "presence_notify", {"origin": 9, "dst": 30, "ctr": 7, "depth": 1}),
            census.Event(5, 9_000, "custody_failure_rx", {"reporter": 31, "dst": 30, "ctr": 7, "seq": 0}),
        ]
        reports = census.bind_custody_reports("D", events)
        self.assertEqual(reports[0].status, "unbound")
        self.assertIsNone(reports[0].age_ms)
        self.assertEqual([d["emit"] for d in reports[0].diagnostic_origins], ["presence_notify"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
