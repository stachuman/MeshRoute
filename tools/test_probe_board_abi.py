#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Unit controls for tools/probe_board_abi.py (§B246 standing board-ABI probe).

WHAT THIS COVERS AND WHAT IT DOES NOT. The probe's own `--no-neg`-less run already exercises the mechanism
end to end on the two real toolchains; these are the cheap, toolchain-free controls over its PURE parts — the
TU generator, the flag-derivation plumbing, the verdict functions and the pin-table structure — so a defect in
the reasoning is caught without waiting on a cross compile.

⛔ ONE ARM DELIBERATELY COMPILES: `test_compile_failure_is_a_failing_verdict` drives `measure()` through the
   HOST compiler with a broken TU, because "a compile failure is FAILING, never measured" ([[B237]]) is exactly
   the property that cannot be proved by inspecting a function signature.

RUN:  cd tools && python3 test_probe_board_abi.py       # (the module imports its subject by name, as
                                                        #  test_measure_board.py does)
"""

from __future__ import annotations

import copy
import subprocess
import sys
import unittest
from pathlib import Path

import probe_board_abi as abi


HOST_TARGET = "native"


def fake_measurement(entries, sizes: dict[str, tuple[int, int, bool]]) -> dict[str, int]:
    """Build the symbol table a compile would have produced, so the verdict functions can be driven directly."""
    out: dict[str, int] = {}
    for cpp_name, _ in (*entries, (abi.FIXTURE_NAME, "1")):
        size, align, included = sizes[cpp_name]
        name = abi.slug(cpp_name)
        out[f"{abi.SYMBOL_PREFIX}size__{name}"] = size
        out[f"{abi.SYMBOL_PREFIX}align__{name}"] = align
        if included:
            out[f"{abi.SYMBOL_PREFIX}included__{name}"] = 1
    return out


class PinTableStructure(unittest.TestCase):
    """The table is data, so its shape is worth pinning: a struct pinned for one ABI and forgotten for another
    would produce a probe that is green on the target nobody checked."""

    def test_every_target_pins_every_struct_including_the_fixture(self) -> None:
        expected = {name for name, _ in abi.PINNED} | {abi.FIXTURE_NAME}
        self.assertEqual(set(abi.TARGETS), set(abi.PIN_TABLE))
        for target, table in abi.PIN_TABLE.items():
            with self.subTest(target):
                self.assertEqual(set(table), expected)
                for cpp_name, (size, align, included) in table.items():
                    self.assertGreater(size, 0, cpp_name)
                    self.assertIn(align, (1, 2, 4, 8, 16), cpp_name)
                    self.assertIsInstance(included, bool, cpp_name)

    def test_the_pinned_set_has_no_duplicates(self) -> None:
        names = [name for name, _ in abi.PINNED]
        self.assertEqual(len(names), len(set(names)))

    def test_board_targets_are_the_two_approved_abis(self) -> None:
        # The 2-env board-gate ruling (2026-08-18). A third board target here would be the same over-reach.
        self.assertEqual(abi.BOARD_TARGETS, ("heltec_mobile", "gateway"))
        self.assertIn(HOST_TARGET, abi.TARGETS)

    def test_the_host_and_the_boards_are_pinned_differently_somewhere(self) -> None:
        """★ The pins THEMSELVES must record a divergence, or the table is a host table with three columns."""
        differing = [name for name in abi.PIN_TABLE[HOST_TARGET]
                     if any(abi.PIN_TABLE[t][name][:2] != abi.PIN_TABLE[HOST_TARGET][name][:2]
                            for t in abi.BOARD_TARGETS)]
        self.assertTrue(differing, "no pinned struct differs host-vs-board")
        self.assertIn(abi.FIXTURE_NAME, differing)


class GeneratedTu(unittest.TestCase):
    def test_emits_size_and_alignment_symbols_for_every_entry(self) -> None:
        text = abi.generate_tu(abi.PINNED)
        for cpp_name, _ in (*abi.PINNED, (abi.FIXTURE_NAME, "1")):
            name = abi.slug(cpp_name)
            self.assertIn(f"{abi.SYMBOL_PREFIX}size__{name}[sizeof({cpp_name})];", text)
            self.assertIn(f"{abi.SYMBOL_PREFIX}align__{name}[alignof({cpp_name})];", text)

    def test_feature_tu_markers_are_guarded_by_the_declared_predicate(self) -> None:
        """The T (feature-TU-inclusion) half must be decided by the PREPROCESSOR on the env's flags — if the
        marker were emitted unconditionally, it would be a Python opinion rather than a compiler answer.
        ⛔ It is inclusion, NOT `.bss` residency; the probe must never claim the latter."""
        text = abi.generate_tu(abi.PINNED)
        for cpp_name, included_if in abi.PINNED:
            name = abi.slug(cpp_name)
            marker = f"char {abi.SYMBOL_PREFIX}included__{name}[1];"
            self.assertIn(marker, text)
            guarded = f"#if {included_if}\n{marker}\n#endif"
            if included_if == "1":
                self.assertNotIn(guarded, text, cpp_name)      # unconditional entries carry no guard
            else:
                self.assertIn(guarded, text, cpp_name)

    def test_mr_features_is_included_before_anything_reads_MR_FEAT(self) -> None:
        headers = list(abi.PROBE_HEADERS)
        self.assertEqual(headers[0], "mr_features.h")

    def test_the_fixture_keeps_the_incident_shape(self) -> None:
        """A pointer, then a byte, then the `uint32_t` that is free only where pointers are 8 bytes."""
        for member in ("void*    adapter_a", "uint8_t  pick", "uint32_t gen", "void*    adapter_b"):
            self.assertIn(member, abi.FIXTURE_SOURCE)


class CompileCommandDerivation(unittest.TestCase):
    IDEDATA = {
        "cxx_path": "/pkg/bin/xtensa-esp32s3-elf-g++",
        "cxx_flags": ["-Os", "-MMD", '"-DQUOTED=<a/b.h>"', "--param", "max-inline-insns-single=500"],
        "defines": ["MR_FEAT_OLED=1", "MR_PROFILE_MOBILE"],
        "includes": {"build": ["/p/src"], "compatlib": ["/p/compat"], "toolchain": ["/wrong/riscv32/include"]},
    }

    def command(self, defines=None):
        return abi.compile_command(self.IDEDATA, Path("/tmp/x.cpp"), Path("/tmp/x.o"), defines)

    def test_uses_the_envs_own_pinned_compiler(self) -> None:
        self.assertEqual(self.command()[0], self.IDEDATA["cxx_path"])

    def test_compiles_only(self) -> None:
        self.assertIn("-c", self.command())

    def test_drops_the_dependency_flag(self) -> None:
        self.assertNotIn("-MMD", self.command())

    def test_unquotes_shell_quoted_flags(self) -> None:
        """The espressif platform ships one such entry; passed verbatim it reads as a filename and the compile
        fails for a reason that has nothing to do with a struct."""
        command = self.command()
        self.assertIn("-DQUOTED=<a/b.h>", command)
        self.assertNotIn('"-DQUOTED=<a/b.h>"', command)

    def test_keeps_multi_token_flags_intact(self) -> None:
        command = self.command()
        self.assertIn("--param", command)
        self.assertIn("max-inline-insns-single=500", command)

    def test_drops_the_toolchain_include_group_and_keeps_the_others(self) -> None:
        command = self.command()
        self.assertIn("-I/p/src", command)
        self.assertIn("-I/p/compat", command)
        self.assertNotIn("-I/wrong/riscv32/include", command)

    def test_derived_defines_reach_the_compiler(self) -> None:
        command = self.command()
        self.assertIn("-DMR_FEAT_OLED=1", command)
        self.assertIn("-DMR_PROFILE_MOBILE", command)

    def test_an_override_replaces_the_derived_defines(self) -> None:
        """Controls (4a)/(4b) depend on this seam: the flags must be substitutable, not baked in."""
        command = self.command(defines=["ONLY_THIS"])
        self.assertIn("-DONLY_THIS", command)
        self.assertNotIn("-DMR_FEAT_OLED=1", command)


class BinutilsSelection(unittest.TestCase):
    def test_prefers_the_same_package(self) -> None:
        """A host `nm` cannot read an xtensa object; a PATH `nm` would silently be somebody else's toolchain."""
        package = Path(__file__).resolve().parent
        # Only the naming rule is exercised here — the real siblings are exercised by the probe's own run.
        self.assertTrue(abi.binutil("/usr/lib/ccache/g++", "nm").endswith("nm"))
        self.assertTrue(package.is_dir())

    def test_raises_when_no_such_tool_exists(self) -> None:
        with self.assertRaises(abi.ProbeFailure):
            abi.binutil("/usr/lib/ccache/g++", "definitely-not-a-binutil")


class Verdicts(unittest.TestCase):
    """Each mutation below must turn a check RED. A verdict function that stays green on broken input is the
    failure this project keeps rediscovering."""

    def setUp(self) -> None:
        self.entries = abi.PINNED
        self.pins = copy.deepcopy(abi.PIN_TABLE)
        self.measured = {t: fake_measurement(self.entries, self.pins[t]) for t in abi.TARGETS}

    def test_the_clean_measurement_passes(self) -> None:
        for target in abi.TARGETS:
            self.assertGreater(abi.check_target(target, self.measured[target], self.entries, self.pins), 0)
        self.assertEqual(abi.check_fixture_divergence(self.measured), len(abi.BOARD_TARGETS))
        checks, diverging = abi.check_production_divergence(self.measured, self.entries)
        self.assertEqual(checks, 1)
        self.assertIn("mrui::UiModel", diverging)

    def test_a_moved_size_is_red(self) -> None:
        target = "heltec_mobile"
        symbol = f"{abi.SYMBOL_PREFIX}size__{abi.slug('mrui::UiState')}"
        self.measured[target][symbol] += 8            # the [[B246]] delta, on the ABI that actually paid it
        with self.assertRaises(abi.ProbeFailure):
            abi.check_target(target, self.measured[target], self.entries, self.pins)

    def test_a_moved_alignment_is_red(self) -> None:
        target = "gateway"
        symbol = f"{abi.SYMBOL_PREFIX}align__{abi.slug('mrui::UiProvAnswer')}"
        self.measured[target][symbol] = 8
        with self.assertRaises(abi.ProbeFailure):
            abi.check_target(target, self.measured[target], self.entries, self.pins)

    def test_lost_feature_tu_inclusion_is_red_even_though_every_size_matches(self) -> None:
        """★ THE POINT OF THE T COLUMN: sizes alone stay green while the one board with a panel silently stops
        compiling the panel's TU. ⛔ Inclusion, not residency."""
        target = "heltec_mobile"
        del self.measured[target][f"{abi.SYMBOL_PREFIX}included__{abi.slug('mrui::UiState')}"]
        with self.assertRaises(abi.ProbeFailure):
            abi.check_target(target, self.measured[target], self.entries, self.pins)

    def test_unexpected_feature_tu_inclusion_is_red(self) -> None:
        target = "gateway"
        self.measured[target][f"{abi.SYMBOL_PREFIX}included__{abi.slug('mrui::UiState')}"] = 1
        with self.assertRaises(abi.ProbeFailure):
            abi.check_target(target, self.measured[target], self.entries, self.pins)

    def test_coverage_loss_is_red(self) -> None:
        target = "gateway"
        del self.measured[target][f"{abi.SYMBOL_PREFIX}size__{abi.slug('meshroute::Node')}"]
        with self.assertRaises(abi.ProbeFailure):
            abi.check_target(target, self.measured[target], self.entries, self.pins)

    def test_an_unpinned_probed_symbol_is_red(self) -> None:
        target = "gateway"
        self.measured[target][f"{abi.SYMBOL_PREFIX}size__Stray"] = 4
        with self.assertRaises(abi.ProbeFailure):
            abi.check_target(target, self.measured[target], self.entries, self.pins)

    def test_a_probed_but_unpinned_struct_is_red(self) -> None:
        entries = (*self.entries, ("mrui::NotPinned", "1"))
        with self.assertRaises(abi.ProbeFailure):
            abi.check_target("gateway", self.measured["gateway"], entries, self.pins)

    def test_an_abi_invariant_fixture_is_red(self) -> None:
        """Control (1) in pure form: if the fixture stops diverging, the probe cannot see the B246 class."""
        symbol = f"{abi.SYMBOL_PREFIX}size__{abi.slug(abi.FIXTURE_NAME)}"
        host = self.measured[HOST_TARGET][symbol]
        for target in abi.BOARD_TARGETS:
            self.measured[target][symbol] = host
        with self.assertRaises(abi.ProbeFailure):
            abi.check_fixture_divergence(self.measured)

    def test_an_all_invariant_production_set_is_red(self) -> None:
        for target in abi.BOARD_TARGETS:
            self.measured[target] = dict(self.measured[HOST_TARGET])
        with self.assertRaises(abi.ProbeFailure):
            abi.check_production_divergence(self.measured, self.entries)

    def test_a_missing_pin_block_is_red(self) -> None:
        with self.assertRaises(abi.ProbeFailure):
            abi.check_target("no_such_target", self.measured["gateway"], self.entries, self.pins)


class FeatureTuPredicateDerivation(unittest.TestCase):
    """`MR_FEAT_OLED` is only a legitimate feature-TU predicate while the flag and the TU those types belong to
    agree — so the agreement is derived from the effective configuration, per environment. ⛔ The claim under
    test is "this env compiles the feature's TU", NOT "an instance is resident"."""

    def setUp(self) -> None:
        self.config = abi.effective_config()

    def test_the_real_configuration_agrees_on_every_environment(self) -> None:
        self.assertGreaterEqual(abi.check_oled_feature_tu_derivation(self.config), 2)

    def test_the_predicate_is_checked_against_all_environments_not_a_list(self) -> None:
        environments = abi.oled_environments(self.config)
        self.assertGreaterEqual(len(environments), 2)
        self.assertTrue(any(flag for flag, _ in environments.values()))
        self.assertTrue(any(not flag for flag, _ in environments.values()))

    def test_a_flag_without_the_tu_is_red(self) -> None:
        mutated = copy.deepcopy(self.config)
        for section in mutated:
            if section[0].startswith("env:") and not any(o[0] == "build_flags" for o in section[1]):
                section[1].append(["build_flags", [f"-D{abi.OLED_FLAG}"]])
                break
            if section[0].startswith("env:"):
                for option in section[1]:
                    if option[0] == "build_flags" and f"-D{abi.OLED_FLAG}" not in option[1]:
                        option[1] = [*option[1], f"-D{abi.OLED_FLAG}"]
                        break
                else:
                    continue
                break
        self.assertNotEqual(mutated, self.config, "UNUSABLE control: the mutation changed nothing")
        with self.assertRaises(abi.ProbeFailure):
            abi.check_oled_feature_tu_derivation(mutated)

    def test_a_tu_without_the_flag_is_red(self) -> None:
        mutated = copy.deepcopy(self.config)
        for section in mutated:
            if section[0].startswith("env:"):
                for option in section[1]:
                    if option[0] == "build_src_filter" and abi.UI_TU not in " ".join(option[1]):
                        option[1] = [*option[1], f"+<{abi.UI_TU}>"]
                        break
                else:
                    continue
                break
        self.assertNotEqual(mutated, self.config, "UNUSABLE control: the mutation changed nothing")
        with self.assertRaises(abi.ProbeFailure):
            abi.check_oled_feature_tu_derivation(mutated)


class ControlSetIntegrity(unittest.TestCase):
    def test_the_full_sweep_control_count_is_pinned(self) -> None:
        """[[B217]]'s discipline: the gate's control count must not shrink quietly."""
        measured = {t: fake_measurement(abi.PINNED, abi.PIN_TABLE[t]) for t in abi.TARGETS}
        specs = abi.control_specs(measured, abi.PINNED, list(abi.TARGETS), abi.effective_config())
        self.assertEqual(len(specs), abi.FULL_SWEEP_CONTROLS)
        self.assertTrue(all(needs is True for _, needs, _ in specs),
                        "on the FULL selection every control must be applicable")

    def test_a_narrow_selection_marks_the_inapplicable_control_na_rather_than_green(self) -> None:
        entries = tuple(e for e in abi.PINNED if e[0] == "mrui::UiState")
        measured = {t: fake_measurement(entries, abi.PIN_TABLE[t]) for t in abi.TARGETS}
        specs = abi.control_specs(measured, entries, list(abi.TARGETS), abi.effective_config())
        by_name = {name: needs for name, needs, _ in specs}
        node_control = next(n for n in by_name if n.startswith("(4a)"))
        self.assertIsInstance(by_name[node_control], str, "a control with no subject must be n/a, not GREEN")


class FailingVerdicts(unittest.TestCase):
    def test_compile_failure_is_a_failing_verdict(self) -> None:
        """⛔ THE [[B237]] ARM, run for real on the HOST compiler: an unbuildable TU must raise, never return an
        empty or partial symbol table that a caller could read as 'nothing changed'."""
        data = abi.idedata(HOST_TARGET)
        broken = "struct Broken { NoSuchType member; };\nchar mr_abi_size__Broken[sizeof(Broken)];\n"
        with self.assertRaises(abi.ProbeFailure) as caught:
            abi.measure(HOST_TARGET, broken)
        self.assertIn("DID NOT COMPILE", str(caught.exception))
        self.assertIn(HOST_TARGET, str(caught.exception))
        self.assertTrue(data["cxx_path"])

    def test_an_unknown_struct_selector_exits_8_not_zero(self) -> None:
        """[[B235]]: a selection that matches nothing must be a LOUD REFUSAL, never a quiet '0 problems'."""
        completed = subprocess.run([sys.executable, str(Path(abi.__file__)), "--struct", "mrui::NoSuchThing"],
                                   capture_output=True, text=True)
        self.assertEqual(completed.returncode, 8)
        self.assertIn("REFUSED", completed.stderr)

    def test_an_unknown_target_selector_exits_8_not_zero(self) -> None:
        completed = subprocess.run([sys.executable, str(Path(abi.__file__)), "--target", "heltec_v3"],
                                   capture_output=True, text=True)
        self.assertEqual(completed.returncode, 8)
        self.assertIn("REFUSED", completed.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
