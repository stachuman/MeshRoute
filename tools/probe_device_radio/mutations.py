#!/usr/bin/env python3
"""Negative controls for the V4-2 device-radio behavioral and fw_main structural probes."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
from typing import Callable


@dataclass(frozen=True)
class Mutation:
    label: str
    apply: Callable[[str], str]


def replace_once(old: str, new: str) -> Callable[[str], str]:
    def apply(text: str) -> str:
        if text.count(old) != 1:
            raise ValueError(f"expected one match, found {text.count(old)}: {old[:70]!r}")
        return text.replace(old, new, 1)
    return apply


def replace_nth(old: str, new: str, occurrence: int) -> Callable[[str], str]:
    def apply(text: str) -> str:
        starts = []
        at = 0
        while True:
            at = text.find(old, at)
            if at < 0:
                break
            starts.append(at)
            at += len(old)
        if len(starts) < occurrence:
            raise ValueError(f"expected occurrence {occurrence}, found {len(starts)}: {old[:70]!r}")
        at = starts[occurrence - 1]
        return text[:at] + new + text[at + len(old):]
    return apply


def after_marker(marker: str, old: str, new: str) -> Callable[[str], str]:
    def apply(text: str) -> str:
        start = text.find(marker)
        if start < 0:
            raise ValueError(f"marker missing: {marker!r}")
        at = text.find(old, start)
        if at < 0:
            raise ValueError(f"target after marker missing: {old!r}")
        return text[:at] + new + text[at + len(old):]
    return apply


def reverse_tx_mode(text: str) -> str:
    gate = "        if (!rf_tx_mode()) {"
    start = "        const int16_t st = _radio.startTransmit(const_cast<uint8_t*>(b), n);"
    if text.count(gate) != 1 or text.count(start) != 1:
        raise ValueError("tx-mode/startTransmit anchors drifted")
    text = text.replace(gate, "        if (false && !rf_tx_mode()) {", 1)
    return text.replace(start, start + "\n        if (!rf_tx_mode()) { arm_rx(); return TxResult::radio_error; }", 1)


def reverse_completion_block(text: str) -> str:
    block = ("    g_hal.collect_tx_completion();\n"
             "    for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); ) g_node.on_tx_complete(outcome);\n")
    timer = ("    for (int id; (id = g_hal.pop_due_timer()) >= 0; ) { g_node.on_timer((uint32_t)id); "
             "canary_timer((uint32_t)id); }   // ADDENDUM: per-timer-id fine canary -> names the exact handler that corrupts the HAL\n")
    if text.count(block) != 1 or text.count(timer) != 1:
        raise ValueError("completion/timer anchors drifted")
    text = text.replace(block, "", 1)
    return text.replace(timer, timer + block, 1)


def main() -> int:
    if len(sys.argv) != 8:
        print("usage: mutations.py ROOT OUT CXX DEVICE FW HARNESS SUPPORT_OBJECTS_CSV")
        return 2

    root = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve()
    cxx = sys.argv[3]
    device_path = Path(sys.argv[4]).resolve()
    fw_path = Path(sys.argv[5]).resolve()
    harness = Path(sys.argv[6]).resolve()
    support = [Path(item).resolve() for item in sys.argv[7].split(",") if item]
    fakes = root / "tools/probe_device_radio/fakes"
    structural = root / "tools/probe_device_radio/structural.py"
    device_live = device_path.read_text()
    fw_live = fw_path.read_text()

    direct_arm = ("    bool arm_rx() {\n"
                  "        if (!rf_rx_mode()) return false;\n"
                  "        if (_radio.startReceive() == RADIOLIB_ERR_NONE) return true;\n"
                  "        ++_rx_arm_failures;\n"
                  "        return false;\n"
                  "    }")
    reverse_arm = ("    bool arm_rx() {\n"
                   "        if (_radio.startReceive() != RADIOLIB_ERR_NONE) { ++_rx_arm_failures; return false; }\n"
                   "        return rf_rx_mode();\n"
                   "    }")
    no_rf_arm = ("    bool arm_rx() {\n"
                   "        if (_radio.startReceive() == RADIOLIB_ERR_NONE) return true;\n"
                   "        ++_rx_arm_failures;\n"
                   "        return false;\n"
                   "    }")
    failed_tx_mode = ("        if (!rf_tx_mode()) {\n"
                      "            arm_rx();                                                              // restore FEM RX + continuous RX after the failed transition\n"
                      "            return TxResult::radio_error;\n"
                      "        }")
    failed_tx_mode_no_recovery = ("        if (!rf_tx_mode()) {\n"
                                  "            (void)0;                                                    // MUTANT: recovery deleted\n"
                                  "            return TxResult::radio_error;\n"
                                  "        }")
    retune_arm = "        arm_rx();\n        _pre_seen = false;"

    device_mutations = [
        Mutation("D1 delete FEM begin", replace_once("const bool ok = rf_begin() && arm_rx();", "const bool ok = arm_rx();")),
        Mutation("D2 reverse FEM begin and initial arm", replace_once("const bool ok = rf_begin() && arm_rx();", "const bool ok = arm_rx() && rf_begin();")),
        Mutation("D3 bypass arm_rx at boot", replace_once(
            "const bool ok = rf_begin() && arm_rx();",
            "const bool ok = rf_begin() && (_radio.startReceive() == RADIOLIB_ERR_NONE);")),
        Mutation("D4 reverse rx_mode/startReceive", replace_once(direct_arm, reverse_arm)),
        Mutation("D5 delete rx_mode from arm_rx", replace_once(direct_arm, no_rf_arm)),
        Mutation("D6 bypass output translation", replace_once(
            "const BoardRfDrive drive = output_drive(pw);", "const BoardRfDrive drive{ true, pw };")),
        Mutation("D7 ignore a refused output translation", replace_once(
            "if (!drive.valid) return TxResult::radio_error;", "if (false && !drive.valid) return TxResult::radio_error;")),
        Mutation("D8 send requested output instead of translated chip drive", replace_once(
            "_radio.setOutputPower(drive.chip_dbm);", "_radio.setOutputPower(pw);")),
        Mutation("D9 delete tx_mode", replace_once("if (!rf_tx_mode()) {", "if (false && !rf_tx_mode()) {")),
        Mutation("D9b delete failed-tx_mode RX recovery", replace_once(
            failed_tx_mode, failed_tx_mode_no_recovery)),
        Mutation("D10 reverse tx_mode/startTransmit", reverse_tx_mode),
        Mutation("D11 delete failed-startTransmit RX recovery", after_marker(
            "if (st != RADIOLIB_ERR_NONE)", "            arm_rx();", "            (void)0;")),
        Mutation("D12 delete completion RX recovery", replace_once(
            "        arm_rx();                                                    // re-arm continuous RX on the listening SF",
            "        (void)0;                                                     // MUTANT: recovery deleted")),
        Mutation("D13 delete abort RX recovery", replace_once(
            "        arm_rx();                                                    // re-arm continuous RX\n",
            "        (void)0;                                                     // MUTANT: recovery deleted\n")),
        Mutation("D14 delete SF-retune RX recovery", replace_nth(retune_arm, "        (void)0;\n        _pre_seen = false;", 1)),
        Mutation("D15 delete frequency-retune RX recovery", replace_nth(retune_arm, "        (void)0;\n        _pre_seen = false;", 2)),
        Mutation("D16 delete bandwidth-retune RX recovery", replace_nth(retune_arm, "        (void)0;\n        _pre_seen = false;", 3)),
        Mutation("D17 delete coding-rate-retune RX recovery", replace_nth(retune_arm, "        (void)0;\n        _pre_seen = false;", 4)),
        Mutation("D18 delete packet-drain RX recovery", replace_once(
            "        arm_rx();                                          // re-arm RX (MeshCore discipline: startReceive after every read)",
            "        (void)0;                                           // MUTANT: recovery deleted")),
        Mutation("D19 break the null-FEM identity mapping", replace_once(
            ": BoardRfDrive{ true, requested_dbm };", ": BoardRfDrive{ false, requested_dbm };")),
    ]

    fw_mutations = [
        Mutation("W1 discard initial-arm result", replace_once("ok = g_iradio.begin();", "(void)g_iradio.begin();")),
        Mutation("W2 omit the provider binding", replace_once(
            "g_iradio(g_radio, meshroute::board_rf_instance())", "g_iradio(g_radio, nullptr)")),
        Mutation("W3 delete completion collection", replace_once("    g_hal.collect_tx_completion();", "    (void)0;")),
        Mutation("W4 reverse completion/timer ordering", reverse_completion_block),
        Mutation("W5 replace exhaustive drain with one pop", replace_once(
            "for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); )",
            "if (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome))")),
        Mutation("W6 deliver outcomes to the wrong Node method", replace_once(
            "g_node.on_tx_complete(outcome);", "g_node.on_timer(outcome);")),
    ]

    flags = [
        cxx, "-std=gnu++20", "-Wall", "-Wextra", "-Werror", "-Wno-volatile",
        "-fno-exceptions", "-fno-rtti", "-DARDUINO=100", "-DMR_RADIO_CANARY=0",
        f"-I{fakes}", f"-I{root / 'lib/meshcore'}", f"-I{root / 'lib/monocypher/src'}",
    ]

    failures = 0
    print("== device-radio behavioral mutation controls ==")
    for index, mutation in enumerate(device_mutations, 1):
        try:
            mutant = mutation.apply(device_live)
        except ValueError as exc:
            print(f"  UNUSABLE {mutation.label}: {exc}")
            failures += 1
            continue
        if mutant == device_live:
            print(f"  UNUSABLE {mutation.label}: mutation changed nothing")
            failures += 1
            continue

        case = out / f"mut-device-{index}"
        hal_dir = case / "lib/hal"
        hal_dir.mkdir(parents=True)
        (hal_dir / "device_radio.h").write_text(mutant)
        for name in ("iboard_rf.h", "iradio.h", "radio_canary.h"):
            os.symlink(root / "lib/hal" / name, hal_dir / name)
        os.symlink(root / "lib/core", case / "lib/core")
        binary = case / "probe"
        cmd = flags + [f"-I{hal_dir}", str(harness)] + [str(obj) for obj in support] + ["-o", str(binary)]
        built = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if built.returncode != 0:
            print(f"  UNUSABLE {mutation.label}: mutant did not compile")
            print("\n".join("    " + line for line in built.stdout.splitlines()[:8]))
            failures += 1
            continue
        ran = subprocess.run([str(binary)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if ran.returncode == 1 and "  FAIL " in ran.stdout:
            fail_count = ran.stdout.count("  FAIL ")
            print(f"  RED  {mutation.label} ({fail_count} failed checks)")
        else:
            print(f"  UNUSABLE {mutation.label}: exit={ran.returncode}, failed-check-lines={ran.stdout.count('  FAIL ')}")
            failures += 1

    print("== fw_main structural mutation controls ==")
    for index, mutation in enumerate(fw_mutations, 1):
        try:
            mutant = mutation.apply(fw_live)
        except ValueError as exc:
            print(f"  UNUSABLE {mutation.label}: {exc}")
            failures += 1
            continue
        fw_mutant = out / f"mut-fw-{index}.cpp"
        fw_mutant.write_text(mutant)
        ran = subprocess.run(
            [sys.executable, str(structural), str(device_path), str(fw_mutant)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        if ran.returncode == 1 and "  FAIL " in ran.stdout:
            print(f"  RED  {mutation.label} ({ran.stdout.count('  FAIL ')} failed checks)")
        else:
            print(f"  UNUSABLE {mutation.label}: exit={ran.returncode}, failed-check-lines={ran.stdout.count('  FAIL ')}")
            failures += 1

    print(f"{len(device_mutations) + len(fw_mutations)} controls checked, {failures} unusable")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
