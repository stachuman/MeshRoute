#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Unit controls for tools/probe_b278_row_abi.py (§B278 correlation-row ABI mirror, PRODUCTION mode since S1a).

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


PRODUCTION_OK = {"ts_ms": 0, "mobile_hash": 8, "target": 12, "return_peer": 16, "ctr_h": 20, "ctr_m": 22,
                 "layer": 24, "outward_type": 25, "target_kind": 26, "return_kind": 27, "state": 28,
                 "custody_state": 29}


def clean_measurement() -> dict[str, dict[str, object]]:
    return {mirror.PRODUCTION_NAME: {"size": 32, "align": 8, "offsets": dict(PRODUCTION_OK)}}


class TestSymbolPrefix(unittest.TestCase):
    def test_the_prefix_survives_the_shared_symbol_filter(self) -> None:
        """`probe_board_abi.read_sizes` keeps ONLY `mr_abi_*`; a private prefix reads back as an empty table."""
        self.assertTrue(mirror.SYMBOL_PREFIX.startswith(abi.SYMBOL_PREFIX))


class TestGeneratedTu(unittest.TestCase):
    def test_every_declared_field_gets_an_offset_symbol(self) -> None:
        tu = mirror.generate_tu()
        for _ty, member in mirror.PRODUCTION_FIELDS:
            self.assertIn(f"{mirror.SYMBOL_PREFIX}off__{mirror.PRODUCTION_NAME}__{member}", tu)
        self.assertIn("node.h", tu)          # the production 32-byte static_assert rides in this TU

    def test_dropping_the_bridge_header_REFUSES_rather_than_generating_a_detached_mirror(self) -> None:
        """★ The property S0's retired 24-byte control used to carry: the mirror must stay TIED TO THE TREE."""
        reduced = tuple(h for h in abi.PROBE_HEADERS if h != mirror.BRIDGE_HEADER)
        with self.assertRaises(mirror.ProbeFailure):
            mirror.generate_tu(headers=reduced)

    def test_offsets_are_emitted_plus_one_and_read_back_minus_one(self) -> None:
        """offsetof can be 0 and a zero-length array is no symbol; the +1/-1 must be applied in ONE place."""
        self.assertIn("offsetof(MrB278ProductionRowMirror, ts_ms) + 1", mirror.generate_tu())
        sizes = fake_sizes(mirror.PRODUCTION_NAME, mirror.PRODUCTION_FIELDS, 32, 8, PRODUCTION_OK)
        read = mirror.read_mirror(sizes, mirror.PRODUCTION_NAME, mirror.PRODUCTION_FIELDS)
        self.assertEqual(read["offsets"]["ts_ms"], 0)
        self.assertEqual(read["offsets"]["custody_state"], 29)

    def test_the_mirror_sequence_is_the_production_sequence(self) -> None:
        """`lib/core/node.h` `struct DelegAck`, transcribed (V1). A silent re-order makes the probe meaningless."""
        self.assertEqual([m for _t, m in mirror.PRODUCTION_FIELDS],
                         ["ts_ms", "mobile_hash", "target", "return_peer", "ctr_h", "ctr_m", "layer",
                          "outward_type", "target_kind", "return_kind", "state", "custody_state"])


class TestReadMirrorIsFailLoud(unittest.TestCase):
    def test_a_missing_size_symbol_RAISES(self) -> None:
        sizes = fake_sizes(mirror.PRODUCTION_NAME, mirror.PRODUCTION_FIELDS, 32, 8, PRODUCTION_OK)
        del sizes[f"{mirror.SYMBOL_PREFIX}size__{mirror.PRODUCTION_NAME}"]
        with self.assertRaises(mirror.ProbeFailure):
            mirror.read_mirror(sizes, mirror.PRODUCTION_NAME, mirror.PRODUCTION_FIELDS)

    def test_a_missing_offset_symbol_RAISES_rather_than_reporting_a_smaller_struct(self) -> None:
        sizes = fake_sizes(mirror.PRODUCTION_NAME, mirror.PRODUCTION_FIELDS, 32, 8, PRODUCTION_OK)
        del sizes[f"{mirror.SYMBOL_PREFIX}off__{mirror.PRODUCTION_NAME}__custody_state"]
        with self.assertRaises(mirror.ProbeFailure):
            mirror.read_mirror(sizes, mirror.PRODUCTION_NAME, mirror.PRODUCTION_FIELDS)


class TestVerdicts(unittest.TestCase):
    def test_the_clean_measurement_has_no_problems(self) -> None:
        self.assertEqual(mirror.check_target("native", clean_measurement()), [])

    def test_a_non_32_byte_production_row_is_a_problem(self) -> None:
        measured = clean_measurement()
        measured[mirror.PRODUCTION_NAME]["size"] = 40
        problems = mirror.check_target("native", measured)
        self.assertTrue(any("node.h's live static_assert says 32" in p for p in problems))

    def test_a_reordered_layout_of_the_SAME_size_is_still_a_problem(self) -> None:
        """The reorder control's whole point: 32 bytes is reachable by a WRONG packing, so size alone is blind."""
        measured = clean_measurement()
        reordered = {"ts_ms": 24, "mobile_hash": 0, "target": 4, "return_peer": 8, "ctr_h": 12, "ctr_m": 14,
                     "layer": 16, "outward_type": 17, "target_kind": 18, "return_kind": 19, "state": 20,
                     "custody_state": 21}
        measured[mirror.PRODUCTION_NAME]["offsets"] = reordered
        self.assertEqual(measured[mirror.PRODUCTION_NAME]["size"], 32)      # unchanged
        self.assertTrue(mirror.check_target("native", measured))

    def test_two_fields_sharing_an_offset_is_a_problem(self) -> None:
        measured = clean_measurement()
        measured[mirror.PRODUCTION_NAME]["offsets"]["custody_state"] = 28
        self.assertTrue(any("share offset" in p for p in mirror.check_target("native", measured)))

    def test_an_offset_outside_sizeof_is_a_problem(self) -> None:
        measured = clean_measurement()
        measured[mirror.PRODUCTION_NAME]["offsets"]["custody_state"] = 99
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
        self.assertIn("custody_state", detail)
        self.assertIn("FAILING verdict", detail)

    def test_an_uncompilable_tu_is_a_failing_verdict_never_a_measurement(self) -> None:
        detail = mirror.control_tu_uncompilable(("native",))
        self.assertIn("DID NOT COMPILE", detail)


if __name__ == "__main__":
    unittest.main(verbosity=2)
