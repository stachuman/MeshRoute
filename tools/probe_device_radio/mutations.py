#!/usr/bin/env python3
"""Negative controls for the V4 device-radio, Heltec FEM, profile and production-wiring probes."""

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


def reverse_boot_frequency(text: str) -> str:
    apply = "        (void)g_iradio.apply_frequency(g_freq_mhz, /*rearm=*/false);\n"
    begin = "        ok = g_iradio.begin();                                  // FEM init + truthful initial continuous-RX arm result\n"
    if text.count(apply) != 1 or text.count(begin) != 1:
        raise ValueError("boot frequency/begin anchors drifted")
    text = text.replace(apply, "", 1)
    return text.replace(begin, begin + apply, 1)


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
    config_path = root / "src/firmware_config.cpp"
    commands_path = root / "src/firmware_commands.cpp"
    platform_path = root / "platformio.ini"
    device_live = device_path.read_text()
    fw_live = fw_path.read_text()
    source_live = {
        "config": config_path.read_text(),
        "commands": commands_path.read_text(),
        "platform": platform_path.read_text(),
    }

    board_path = root / "variants/heltec_v4/board_rf.cpp"
    board_header_path = root / "variants/heltec_v4/board_rf.h"
    board_harness = root / "tools/probe_device_radio/board_rf_probe.cpp"
    board_live = board_path.read_text()
    board_header_live = board_header_path.read_text()

    begin_sequence = ("        _board_rf_ready = rf_begin();\n"
                      "        const bool ok = _board_rf_ready && arm_rx();")
    reverse_begin_sequence = ("        _board_rf_ready = true;\n"
                              "        const bool armed = arm_rx();\n"
                              "        _board_rf_ready = rf_begin();\n"
                              "        const bool ok = _board_rf_ready && armed;")

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
    device_mutations = [
        Mutation("D1 delete FEM begin", replace_once(
            "        _board_rf_ready = rf_begin();", "        _board_rf_ready = true;")),
        Mutation("D2 reverse FEM begin and initial arm", replace_once(begin_sequence, reverse_begin_sequence)),
        Mutation("D3 bypass arm_rx at boot", replace_once(
            "const bool ok = _board_rf_ready && arm_rx();",
            "const bool ok = _board_rf_ready && (_radio.startReceive() == RADIOLIB_ERR_NONE);")),
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
        Mutation("D14 delete SF-retune RX recovery", after_marker(
            "void set_rx_sf", "        arm_rx();", "        (void)0;")),
        Mutation("D15 delete frequency-retune RX recovery", replace_once(
            "(void)apply_frequency(mhz, /*rearm=*/true);",
            "(void)apply_frequency(mhz, /*rearm=*/false);")),
        Mutation("D16 delete bandwidth-retune RX recovery", after_marker(
            "void set_rx_bw", "        arm_rx();", "        (void)0;")),
        Mutation("D17 delete coding-rate-retune RX recovery", after_marker(
            "void set_rx_cr", "        arm_rx();", "        (void)0;")),
        Mutation("D18 delete packet-drain RX recovery", replace_once(
            "        arm_rx();                                          // re-arm RX (MeshCore discipline: startReceive after every read)",
            "        (void)0;                                           // MUTANT: recovery deleted")),
        Mutation("D19 break the null-FEM identity mapping", replace_once(
            ": BoardRfDrive{ true, requested_dbm };", ": BoardRfDrive{ false, requested_dbm };")),
        Mutation("D20 accept an unsupported carrier", replace_once(
            "if (!frequency_supported(mhz)) {", "if (false && !frequency_supported(mhz)) {")),
        Mutation("D21 do not invalidate an unsupported carrier", after_marker(
            "bool apply_frequency", "            _frequency_valid = false;", "            _frequency_valid = true;")),
        Mutation("D22 do not count an unsupported carrier", after_marker(
            "bool apply_frequency", "            ++_rf_band_failures;", "            (void)0;")),
        Mutation("D23 let TX bypass invalid carrier state", replace_once(
            "if ((_board_rf && !_board_rf_ready) || !_frequency_valid) return TxResult::radio_error;",
            "if ((_board_rf && !_board_rf_ready) || false) return TxResult::radio_error;")),
        Mutation("D24 do not clear the invalid latch after a valid carrier", replace_once(
            "        _frequency_valid = true;", "        _frequency_valid = false;")),
        Mutation("D25 ignore a RadioLib setFrequency failure", after_marker(
            "bool apply_frequency", "if (st != RADIOLIB_ERR_NONE) {", "if (false && st != RADIOLIB_ERR_NONE) {")),
        Mutation("D26 delete RX recovery after failed setFrequency", after_marker(
            "if (st != RADIOLIB_ERR_NONE)", "            if (_begun) (void)rearm_frequency_rx();", "            (void)_begun;")),
        Mutation("D26a retain stale DIO1 across a frequency rearm", after_marker(
            "bool rearm_frequency_rx()", "        g_dio1_fired = false;", "        (void)g_dio1_fired;")),
        Mutation("D26b retain stale preamble state across a frequency rearm", after_marker(
            "bool rearm_frequency_rx()", "        _pre_seen = false;", "        (void)_pre_seen;")),
        Mutation("D26c clean preamble state only after a successful frequency rearm", replace_once(
            "        const bool armed = arm_rx();\n"
            "        _pre_seen = false;\n"
            "        return armed;",
            "        if (!arm_rx()) return false;\n"
            "        _pre_seen = false;\n"
            "        return true;")),
        Mutation("D27 bypass the shared frequency helper in set_rx_freq", replace_once(
            "        (void)apply_frequency(mhz, /*rearm=*/true);",
            "        _radio.standby(); _radio.setFrequency(static_cast<float>(mhz)); arm_rx();")),
        Mutation("D28 make rfcfg ignore output validity", replace_once(
            "return _frequency_valid && output_supported(requested_dbm);",
            "return _frequency_valid || output_supported(requested_dbm);")),
        Mutation("D29 start an external-FEM instance frequency-valid", replace_once(
            "_frequency_valid(board_rf == nullptr)", "_frequency_valid(true)")),
    ]

    board_header_mutations = [
        Mutation("B1 guess KCT for floating/unstable detection", replace_once(
            "    return BoardRfKind::unknown;", "    return BoardRfKind::kct8103l;")),
        Mutation("B2 ignore the repeated pull-up sample", replace_once(
            "if (!pullup_a && !pullup_b && !pulldown_a && !pulldown_b)",
            "if (!pullup_a && !pulldown_a && !pulldown_b)")),
        Mutation("B3 swap the stable pull-down classification", replace_once(
            "return BoardRfKind::gc1109;", "return BoardRfKind::kct8103l;")),
    ]
    board_mutations = [
        Mutation("B4 delete FEM LDO power-on", after_marker(
            "bool begin()",
            "        digitalWrite(MR_V4_FEM_LDO_PIN, HIGH);",
            "        digitalWrite(MR_V4_FEM_LDO_PIN, LOW);")),
        Mutation("B5 skip one stale-hold release", replace_once(
            "        holds_released &= rtc_gpio_hold_dis(static_cast<gpio_num_t>(MR_V4_KCT_CTX_PIN)) == ESP_OK;",
            "        (void)MR_V4_KCT_CTX_PIN;")),
        Mutation("B6 ignore a stale-hold release failure", replace_once(
            "        if (!holds_released) return park_unknown();",
            "        if (false && !holds_released) return park_unknown();")),
        Mutation("B7 GC1109 RX leaves TX_EN high", after_marker(
            "bool rx_mode()", "            digitalWrite(MR_V4_GC_TX_EN_PIN, LOW);",
            "            digitalWrite(MR_V4_GC_TX_EN_PIN, HIGH);")),
        Mutation("B8 GC1109 TX leaves TX_EN low", after_marker(
            "bool tx_mode()", "            digitalWrite(MR_V4_GC_TX_EN_PIN, HIGH);",
            "            digitalWrite(MR_V4_GC_TX_EN_PIN, LOW);")),
        Mutation("B9 KCT8103L RX bypasses its LNA", after_marker(
            "bool rx_mode()", "            digitalWrite(MR_V4_KCT_CTX_PIN, LOW);",
            "            digitalWrite(MR_V4_KCT_CTX_PIN, HIGH);")),
        Mutation("B10 KCT8103L TX leaves CTX low", after_marker(
            "bool tx_mode()", "            digitalWrite(MR_V4_KCT_CTX_PIN, HIGH);",
            "            digitalWrite(MR_V4_KCT_CTX_PIN, LOW);")),
        Mutation("B11 advertise LNA bypass instead of on", replace_once(
            "BoardRfLnaState::on", "BoardRfLnaState::bypass")),
        Mutation("B12 accept a second nominal output", replace_once(
            "return output_dbm == kNominalOutputDbm ?", "return output_dbm >= 21 && output_dbm <= kNominalOutputDbm ?")),
        Mutation("B13 use requested nominal output as chip drive", replace_once(
            "BoardRfDrive{true, kChipDriveDbm}", "BoardRfDrive{true, output_dbm}")),
        Mutation("B14 bypass the V4 frequency envelope", replace_once(
            "        return configured_frequency_supported(mhz);", "        (void)mhz; return true;")),
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
        Mutation("W7 delete the boot frequency apply", replace_once(
            "(void)g_iradio.apply_frequency(g_freq_mhz, /*rearm=*/false);", "(void)g_freq_mhz;")),
        Mutation("W8 apply the requested carrier after FEM/RX begin", reverse_boot_frequency),
        Mutation("W9 bypass the frequency authority at boot", replace_once(
            "(void)g_iradio.apply_frequency(g_freq_mhz, /*rearm=*/false);",
            "(void)g_radio.setFrequency(static_cast<float>(g_freq_mhz));")),
    ]

    source_mutations = [
        ("S1 bypass the frequency authority in grouped live config", "config", replace_once(
            "if (g_iradio.apply_frequency(b.freq_mhz, /*rearm=*/false)) {",
            "if (g_radio.setFrequency((float)b.freq_mhz) == RADIOLIB_ERR_NONE) {")),
        ("S2 half-apply modulation after a refused live carrier", "config", replace_once(
            "if (g_iradio.apply_frequency(b.freq_mhz, /*rearm=*/false)) {",
            "(void)g_iradio.apply_frequency(b.freq_mhz, /*rearm=*/false); if (true) {")),
        ("S3 discard config validation when hardware is failed", "config", replace_once(
            "(void)g_iradio.validate_frequency(b.freq_mhz);", "(void)b.freq_mhz;")),
        ("S4 make rfok an OR instead of the conjunction", "commands", replace_once(
            "out.print((g_radio_ok && rfcfg) ? 1 : 0);", "out.print((g_radio_ok || rfcfg) ? 1 : 0);")),
        ("S5 omit runtime FEM identity from USB diagnostics", "commands", replace_once(
            "out.print(F(\"fem=\"));", "out.print(F(\"front_end=\"));")),
        ("S6 label Heltec V4 as V3", "commands", replace_once(
            '#elif defined(BOARD_HELTEC_V4)\n    return "heltec_v4";',
            '#elif defined(BOARD_HELTEC_V4)\n    return "heltec_v3";')),
        ("S7 omit the production board-RF TU", "platform", replace_once(
            " +<../variants/heltec_v4/board_rf.cpp>", "")),
        ("S8 omit the strict V4 frequency envelope", "platform", replace_once(
            "  -DMR_RF_STRICT_ENVELOPE=1\n", "")),
        ("S9 delete the V4 mobile profile", "platform", replace_once(
            "[env:heltec_v4_mobile]", "[env:heltec_v4_mobile_deleted]")),
        ("S10 derive V4 mobile from the V3 board", "platform", after_marker(
            "[env:heltec_v4_mobile]", "extends = env:heltec_v4", "extends = env:heltec_v3")),
        ("S11 omit V4 board flags from the mobile profile", "platform", after_marker(
            "[env:heltec_v4_mobile]", "  ${env:heltec_v4.build_flags}\n", "")),
        ("S12 omit the mobile role from the V4 mobile profile", "platform", after_marker(
            "[env:heltec_v4_mobile]", "  -DMR_PROFILE_MOBILE\n", "")),
        ("S13 delete the V4 gateway profile", "platform", replace_once(
            "[env:gateway_heltec_v4]", "[env:gateway_heltec_v4_deleted]")),
        ("S14 derive the V4 gateway from the V3 board", "platform", after_marker(
            "[env:gateway_heltec_v4]", "extends = env:heltec_v4", "extends = env:heltec_v3")),
        ("S15 omit V4 board flags from the V4 gateway", "platform", after_marker(
            "[env:gateway_heltec_v4]", "  ${env:heltec_v4.build_flags}\n", "")),
        ("S16 omit the shared gateway role flags from the V4 gateway", "platform", after_marker(
            "[env:gateway_heltec_v4]", "  ${gateway_flags.build_flags}\n", "")),
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
        for name in ("iboard_rf.h", "iradio.h", "radio_canary.h", "rf_capabilities.h"):
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

    board_flags = [
        "-DMR_BOARD_RF_FRONTEND=1", "-DMR_V4_FEM_LDO_PIN=7", "-DMR_V4_FEM_CSD_PIN=2",
        "-DMR_V4_GC_TX_EN_PIN=46", "-DMR_V4_KCT_CTX_PIN=5", "-DLORA_TX_POWER=10",
        "-DMR_DEFAULT_OUTPUT_DBM=22", "-DMR_RF_OUTPUT_MIN_DBM=22", "-DMR_RF_OUTPUT_MAX_DBM=22",
        "-DMR_RF_FREQ_MIN_MHZ=863.0", "-DMR_RF_FREQ_MAX_MHZ=928.0", "-DMR_RF_STRICT_ENVELOPE=1",
    ]

    print("== Heltec V4 board-RF behavioral mutation controls ==")
    targeted_board_mutations = [(m, True) for m in board_header_mutations] + [(m, False) for m in board_mutations]
    for index, (mutation, targets_header) in enumerate(targeted_board_mutations, 1):
        try:
            mutant = mutation.apply(board_header_live if targets_header else board_live)
        except ValueError as exc:
            print(f"  UNUSABLE {mutation.label}: {exc}")
            failures += 1
            continue

        case = out / f"mut-board-{index}"
        case.mkdir(parents=True)
        (case / "board_rf.h").write_text(mutant if targets_header else board_header_live)
        (case / "board_rf.cpp").write_text(board_live if targets_header else mutant)
        binary = case / "probe"
        cmd = flags + board_flags + [f"-I{case}", f"-I{root / 'lib/hal'}",
                                     str(board_harness), str(case / "board_rf.cpp"), "-o", str(binary)]
        built = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if built.returncode != 0:
            print(f"  UNUSABLE {mutation.label}: mutant did not compile")
            print("\n".join("    " + line for line in built.stdout.splitlines()[:8]))
            failures += 1
            continue
        ran = subprocess.run([str(binary)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if ran.returncode == 1 and "  FAIL " in ran.stdout:
            print(f"  RED  {mutation.label} ({ran.stdout.count('  FAIL ')} failed checks)")
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
            [sys.executable, str(structural), str(device_path), str(fw_mutant), str(config_path),
             str(commands_path), str(platform_path)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        if ran.returncode == 1 and "  FAIL " in ran.stdout:
            print(f"  RED  {mutation.label} ({ran.stdout.count('  FAIL ')} failed checks)")
        else:
            print(f"  UNUSABLE {mutation.label}: exit={ran.returncode}, failed-check-lines={ran.stdout.count('  FAIL ')}")
            failures += 1

    print("== V4 cross-file structural mutation controls ==")
    for index, (label, target, apply) in enumerate(source_mutations, 1):
        try:
            mutant = apply(source_live[target])
        except ValueError as exc:
            print(f"  UNUSABLE {label}: {exc}")
            failures += 1
            continue
        mutant_path = out / f"mut-source-{index}.txt"
        mutant_path.write_text(mutant)
        paths = {
            "config": config_path,
            "commands": commands_path,
            "platform": platform_path,
        }
        paths[target] = mutant_path
        ran = subprocess.run(
            [sys.executable, str(structural), str(device_path), str(fw_path), str(paths["config"]),
             str(paths["commands"]), str(paths["platform"])],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        if ran.returncode == 1 and "  FAIL " in ran.stdout:
            print(f"  RED  {label} ({ran.stdout.count('  FAIL ')} failed checks)")
        else:
            print(f"  UNUSABLE {label}: exit={ran.returncode}, failed-check-lines={ran.stdout.count('  FAIL ')}")
            failures += 1

    total = (len(device_mutations) + len(board_header_mutations) + len(board_mutations) +
             len(fw_mutations) + len(source_mutations))
    print(f"{total} controls checked, {failures} unusable")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
