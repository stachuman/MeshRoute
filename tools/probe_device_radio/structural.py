#!/usr/bin/env python3
"""V4 port source-shape checks that complement the production-header/GPIO behavioral probes."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 6:
        print("usage: structural.py DEVICE_RADIO_H FW_MAIN_CPP FIRMWARE_CONFIG_CPP FIRMWARE_COMMANDS_CPP PLATFORMIO_INI")
        return 2

    device = Path(sys.argv[1]).read_text()
    fw = Path(sys.argv[2]).read_text()
    config = Path(sys.argv[3]).read_text()
    commands = Path(sys.argv[4]).read_text()
    platform = Path(sys.argv[5]).read_text()
    passed = 0
    failed = 0

    def check(label: str, condition: bool) -> None:
        nonlocal passed, failed
        if condition:
            passed += 1
        else:
            failed += 1
            print(f"  FAIL {label}")

    def env_section(name: str) -> str:
        marker = f"[env:{name}]"
        start = platform.find(marker)
        if start < 0:
            return ""
        end = platform.find("\n[", start + len(marker))
        return platform[start:end if end >= 0 else len(platform)]

    # One physical RadioLib authority for each direction. Names in comments do not count: these expressions name
    # the wrapped object exactly, so a bypass anywhere is visible.
    check("S1 exactly one production _radio.startReceive expression", device.count("_radio.startReceive(") == 1)
    check("S2 exactly one production _radio.startTransmit expression", device.count("_radio.startTransmit(") == 1)
    arm_at = device.find("bool arm_rx()")
    rx_call_at = device.find("_radio.startReceive(")
    check("S3 the sole startReceive expression lives in arm_rx", arm_at >= 0 and rx_call_at > arm_at)

    # The provider is bound once at construction; fw_main contains no board/FEM pin choice.
    provider_ctor = "meshroute::Sx1262Radio  g_iradio(g_radio, meshroute::board_rf_instance());"
    check("S4 fw_main binds Sx1262Radio to the optional provider", fw.count(provider_ctor) == 1)
    check("S5 fw_main contains no V4 FEM pin/mode logic", all(token not in fw for token in ("GC1109", "KCT8103", "MR_RF_FEM_")))

    # std_init gates begin, and begin's exact result becomes the public hardware truth. A discarded begin or an
    # assignment before it would recreate the false-healthy boot state this slice exists to remove.
    begin_binding = "ok = g_iradio.begin();"
    truth_binding = "g_radio_ok = ok;"
    begin_pos = fw.find(begin_binding)
    truth_pos = fw.find(truth_binding)
    check("S6 initial arm result is assigned, never discarded", fw.count(begin_binding) == 1 and fw.count("g_iradio.begin();") == 1)
    check("S7 g_radio_ok is assigned after the initial arm result", begin_pos >= 0 and truth_pos > begin_pos and fw.count(truth_binding) == 1)
    check("S8 boot output renders the combined g_radio_ok truth",
          'mrcon.println(g_radio_ok ? F("OK") : F("INIT FAILED"));' in fw)

    # T3/W21/W22 is outside every native binary: preserve the service-before-drain, exhaustive delivery, timers,
    # then pump order in the only production bridge.
    collect = "g_hal.collect_tx_completion();"
    drain = "for (meshroute::TxOutcome outcome; g_hal.pop_tx_outcome(outcome); ) g_node.on_tx_complete(outcome);"
    timers = "for (int id; (id = g_hal.pop_due_timer()) >= 0; )"
    pump = "g_hal.pump_tx();"
    positions = [fw.find(item) for item in (collect, drain, timers, pump)]
    check("S9 completion collection precedes exhaustive outcome drain", positions[0] >= 0 and positions[0] < positions[1])
    check("S10 every popped outcome reaches Node::on_tx_complete", fw.count(drain) == 1)
    check("S11 completion/drain precede timers and pump remains last",
          all(pos >= 0 for pos in positions) and positions == sorted(positions))

    # One final frequency authority: validation is before standby; all public producers route through it.
    freq_start = device.find("bool apply_frequency(double mhz, bool rearm)")
    freq_end = device.find("bool validate_frequency(double mhz)", freq_start)
    freq_block = device[freq_start:freq_end] if freq_start >= 0 and freq_end > freq_start else ""
    unsupported = freq_block.find("if (!frequency_supported(mhz))")
    standby = freq_block.find("_radio.standby()")
    set_frequency = freq_block.find("_radio.setFrequency(")
    check("S12 exactly one production SetRfFrequency expression, inside apply_frequency",
          device.count("_radio.setFrequency(") == 1 and unsupported >= 0 and unsupported < standby < set_frequency)
    check("S13 gateway set_rx_freq delegates to the shared helper",
          device.count("(void)apply_frequency(mhz, /*rearm=*/true);") == 1)
    tx_gate = "if ((_board_rf && !_board_rf_ready) || !_frequency_valid) return TxResult::radio_error;"
    check("S14 TX frequency/FEM validity gate precedes output translation and standby",
          device.find(tx_gate) >= 0 and device.find(tx_gate) < device.find("const BoardRfDrive drive = output_drive(pw);") < device.find("_radio.standby()"))

    boot_apply = "(void)g_iradio.apply_frequency(g_freq_mhz, /*rearm=*/false);"
    check("S15 boot applies requested frequency before FEM/RX begin",
          fw.count(boot_apply) == 1 and fw.find(boot_apply) < fw.find(begin_binding))
    live_apply = "if (g_iradio.apply_frequency(b.freq_mhz, /*rearm=*/false)) {"
    check("S16 grouped live config is gated by the same frequency authority",
          config.count(live_apply) == 1 and "(void)g_iradio.validate_frequency(b.freq_mhz);" in config)
    check("S17 fw_main/config contain no direct RadioLib setFrequency bypass",
          "g_radio.setFrequency(" not in fw and "g_radio.setFrequency(" not in config)

    # USB diagnostics keep hardware/config/composite truth distinct and name the runtime FEM, never a compile-time
    # V4 revision. The structured BLE/JSON status is intentionally untouched in this slice.
    diag_tokens = ("fem=", " lna=", " radiohw=", " rfcfg=", " rfok=", " rfmodefail=", " rfbandfail=", " rfout=", " rfchip=")
    check("S18 one USB RF formatter renders every required field",
          commands.count("void print_rf_diagnostics(Print& out)") == 1 and all(token in commands for token in diag_tokens))
    check("S19 rfok is the hardware/config conjunction",
          "out.print((g_radio_ok && rfcfg) ? 1 : 0);" in commands)
    check("S20 boot and status both call the same RF formatter",
          fw.count("print_rf_diagnostics(mrcon);") == 1 and commands.count("print_rf_diagnostics(out);") == 1)
    check("S21 board_name has an explicit Heltec V4 arm",
          '#elif defined(BOARD_HELTEC_V4)\n    return "heltec_v4";' in commands)

    v4_section = env_section("heltec_v4")
    v4_mobile_section = env_section("heltec_v4_mobile")
    v4_gateway_section = env_section("gateway_heltec_v4")
    check("S22 exactly one base and one of each V4 role profile",
          all(platform.count(f"[env:{name}]") == 1
              for name in ("heltec_v4", "heltec_v4_mobile", "gateway_heltec_v4")))
    required_v4 = ("board = heltec_v4", "board_build.variants_dir = arduino_variants", "-DMR_BOARD_RF_FRONTEND=1",
                   "-DLORA_TX_POWER=10", "-DMR_DEFAULT_OUTPUT_DBM=22", "-DMR_RF_OUTPUT_MIN_DBM=22",
                   "-DMR_RF_OUTPUT_MAX_DBM=22", "-DMR_RF_FREQ_MIN_MHZ=863.0", "-DMR_RF_FREQ_MAX_MHZ=928.0",
                   "-DMR_RF_STRICT_ENVELOPE=1", "-DSX126X_REGISTER_PATCH=1",
                   "+<../variants/heltec_v4/board_rf.cpp>")
    check("S23 V4 env pins the first-build RF/output/variant contract",
          bool(v4_section) and all(token in v4_section for token in required_v4))
    check("S24 V4 mobile inherits the V4 board and adds only the mobile role",
          all(token in v4_mobile_section for token in
              ("extends = env:heltec_v4", "${env:heltec_v4.build_flags}", "-DMR_PROFILE_MOBILE")) and
          all(token not in v4_mobile_section for token in
              ("${gateway_flags.build_flags}", "board =", "build_src_filter =")))
    check("S25 V4 gateway inherits the V4 board and adds only the shared gateway role",
          all(token in v4_gateway_section for token in
              ("extends = env:heltec_v4", "${env:heltec_v4.build_flags}", "${gateway_flags.build_flags}")) and
          all(token not in v4_gateway_section for token in
              ("-DMR_PROFILE_MOBILE", "board =", "build_src_filter =")))

    print(f"{passed} structural checks passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
