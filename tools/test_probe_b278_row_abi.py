#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Unit controls for tools/probe_b278_row_abi.py (§B278 S0 synthetic-mirror ABI mode).

WHAT THIS COVERS AND WHAT IT DOES NOT — the same split `test_probe_board_abi.py` uses. The probe's own
control-bearing run already exercises the mechanism end to end on the two real cross toolchains; these are
the cheap, toolchain-free controls over its PURE parts: the TU generator, the symbol reader, the verdict
functions and the full-sweep refusal. A defect in the reasoning is then caught without waiting on a cross
compile.

⛔ ONE ARM DELIBERATELY COMPILES (host only): `test_a_dropped_field_is_a_failing_verdict` drives the real
   `measure` path through the HOST compiler, because "a MISSING FIELD is a FAILING verdict, never a smaller
   struct" is exactly the property that cannot be proved by inspecting a function signature.

RUN:  cd tools && python3 test_probe_b278_row_abi.py
"""

from __future__ import annotations

import unittest

import probe_b278_row_abi as mirror
import probe_board_abi as abi


def fake_sizes(name: str, fields, size: int, align: int, offsets: dict[str, int]) -> dict[str, int]:
    """Build the symbol table a compile would have produced (offsets are stored +1, as the TU emits them)."""
    out = {f"{mirror.SYMBOL_PREFIX}size__{name}": size,
           f"{mirror.SYMBOL_PREFIX}align__{name}": align}
    for _ty, member in fields:
        out[f"{mirror.SYMBOL_PREFIX}off__{name}__{member}"] = offsets[member] + 1
    return out


CANDIDATE_OK = {"ts_ms": 0, "mobile_hash": 8, "target": 12, "return_peer": 16, "ctr_h": 20, "ctr_m": 22,
                "layer": 24, "outward_type": 25, "target_kind": 26, "return_kind": 27, "state": 28,
                "obligations": 29}
CURRENT_OK = {"ts_ms": 0, "mobile_hash": 8, "peer": 12, "ctr_h": 16, "ctr_m": 18, "layer": 20,
              "peer_kind": 21, "state": 22}


def clean_measurement() -> dict[str, dict[str, object]]:
    return {
        mirror.CANDIDATE_NAME: {"size": 32, "align": 8, "offsets": dict(CANDIDATE_OK)},
        mirror.CURRENT_NAME: {"size": 24, "align": 8, "offsets": dict(CURRENT_OK)},
    }


class TestSymbolPrefix(unittest.TestCase):
    def test_the_prefix_survives_the_shared_symbol_filter(self) -> None:
        """`probe_board_abi.read_sizes` keeps ONLY `mr_abi_*`; a private prefix reads back as an empty table."""
        self.assertTrue(mirror.SYMBOL_PREFIX.startswith(abi.SYMBOL_PREFIX))


class TestGeneratedTu(unittest.TestCase):
    def test_every_declared_field_gets_an_offset_symbol(self) -> None:
        tu = mirror.generate_tu()
        for _ty, member in mirror.CANDIDATE_FIELDS:
            self.assertIn(f"{mirror.SYMBOL_PREFIX}off__{mirror.CANDIDATE_NAME}__{member}", tu)
        self.assertIn("node.h", tu)          # the production 24-byte static_assert rides in this TU

    def test_offsets_are_emitted_plus_one_and_read_back_minus_one(self) -> None:
        """offsetof can be 0 and a zero-length array is no symbol; the +1/-1 must be applied in ONE place."""
        self.assertIn("offsetof(MrB278CandidateRow, ts_ms) + 1", mirror.generate_tu())
        sizes = fake_sizes(mirror.CANDIDATE_NAME, mirror.CANDIDATE_FIELDS, 32, 8, CANDIDATE_OK)
        read = mirror.read_mirror(sizes, mirror.CANDIDATE_NAME, mirror.CANDIDATE_FIELDS)
        self.assertEqual(read["offsets"]["ts_ms"], 0)
        self.assertEqual(read["offsets"]["obligations"], 29)

    def test_the_candidate_sequence_is_the_spec_sequence(self) -> None:
        """§4.1's field order, transcribed. A silent re-order here would make the whole probe meaningless."""
        self.assertEqual([m for _t, m in mirror.CANDIDATE_FIELDS],
                         ["ts_ms", "mobile_hash", "target", "return_peer", "ctr_h", "ctr_m", "layer",
                          "outward_type", "target_kind", "return_kind", "state", "obligations"])


class TestReadMirrorIsFailLoud(unittest.TestCase):
    def test_a_missing_size_symbol_RAISES(self) -> None:
        sizes = fake_sizes(mirror.CANDIDATE_NAME, mirror.CANDIDATE_FIELDS, 32, 8, CANDIDATE_OK)
        del sizes[f"{mirror.SYMBOL_PREFIX}size__{mirror.CANDIDATE_NAME}"]
        with self.assertRaises(mirror.ProbeFailure):
            mirror.read_mirror(sizes, mirror.CANDIDATE_NAME, mirror.CANDIDATE_FIELDS)

    def test_a_missing_offset_symbol_RAISES_rather_than_reporting_a_smaller_struct(self) -> None:
        sizes = fake_sizes(mirror.CANDIDATE_NAME, mirror.CANDIDATE_FIELDS, 32, 8, CANDIDATE_OK)
        del sizes[f"{mirror.SYMBOL_PREFIX}off__{mirror.CANDIDATE_NAME}__obligations"]
        with self.assertRaises(mirror.ProbeFailure):
            mirror.read_mirror(sizes, mirror.CANDIDATE_NAME, mirror.CANDIDATE_FIELDS)


class TestVerdicts(unittest.TestCase):
    def test_the_clean_measurement_has_no_problems(self) -> None:
        self.assertEqual(mirror.check_target("native", clean_measurement()), [])

    def test_a_non_32_byte_candidate_is_a_problem(self) -> None:
        measured = clean_measurement()
        measured[mirror.CANDIDATE_NAME]["size"] = 40
        problems = mirror.check_target("native", measured)
        self.assertTrue(any("spec §12-S0 requires 32" in p for p in problems))

    def test_a_non_24_byte_control_is_a_problem(self) -> None:
        measured = clean_measurement()
        measured[mirror.CURRENT_NAME]["size"] = 32
        problems = mirror.check_target("native", measured)
        self.assertTrue(any("node.h's live static_assert says 24" in p for p in problems))

    def test_a_reordered_layout_of_the_SAME_size_is_still_a_problem(self) -> None:
        """The reorder control's whole point: 32 bytes is reachable by a WRONG packing, so size alone is blind."""
        measured = clean_measurement()
        reordered = {"ts_ms": 24, "mobile_hash": 0, "target": 4, "return_peer": 8, "ctr_h": 12, "ctr_m": 14,
                     "layer": 16, "outward_type": 17, "target_kind": 18, "return_kind": 19, "state": 20,
                     "obligations": 21}
        measured[mirror.CANDIDATE_NAME]["offsets"] = reordered
        self.assertEqual(measured[mirror.CANDIDATE_NAME]["size"], 32)      # unchanged
        self.assertTrue(mirror.check_target("native", measured))

    def test_two_fields_sharing_an_offset_is_a_problem(self) -> None:
        measured = clean_measurement()
        measured[mirror.CANDIDATE_NAME]["offsets"]["obligations"] = 28
        self.assertTrue(any("share offset" in p for p in mirror.check_target("native", measured)))

    def test_an_offset_outside_sizeof_is_a_problem(self) -> None:
        measured = clean_measurement()
        measured[mirror.CANDIDATE_NAME]["offsets"]["obligations"] = 99
        self.assertTrue(any("outside sizeof" in p for p in mirror.check_target("native", measured)))


class TestFullSweepRefusal(unittest.TestCase):
    def test_a_complete_target_set_is_accepted(self) -> None:
        mirror.require_full_sweep(tuple(abi.TARGETS), filtered=False)

    def test_a_silently_dropped_target_REFUSES(self) -> None:
        reduced = tuple(t for t in abi.TARGETS if t != "gateway")
        with self.assertRaises(mirror.ProbeRefusal):
            mirror.require_full_sweep(reduced, filtered=False)

    def test_an_explicit_filter_is_allowed_but_is_banner_marked_not_a_gate(self) -> None:
        mirror.require_full_sweep(("native",), filtered=True)             # allowed
        self.assertIn("NOT A GATE", mirror.__doc__ or "")

    def test_the_control_count_is_pinned(self) -> None:
        self.assertEqual(mirror.FULL_SWEEP_CONTROLS, 6)


class TestHostCompileArms(unittest.TestCase):
    """⛔ These COMPILE (host toolchain only) — the properties cannot be proved by inspection."""

    def test_a_dropped_field_is_a_failing_verdict(self) -> None:
        detail = mirror.control_field_missing(("native",))
        self.assertIn("obligations", detail)
        self.assertIn("FAILING verdict", detail)

    def test_an_uncompilable_tu_is_a_failing_verdict_never_a_measurement(self) -> None:
        detail = mirror.control_tu_uncompilable(("native",))
        self.assertIn("DID NOT COMPILE", detail)


if __name__ == "__main__":
    unittest.main(verbosity=2)
