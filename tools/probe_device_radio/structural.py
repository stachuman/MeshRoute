#!/usr/bin/env python3
"""V4-2 source-shape checks that complement the production-header behavioral probe."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: structural.py DEVICE_RADIO_H FW_MAIN_CPP")
        return 2

    device = Path(sys.argv[1]).read_text()
    fw = Path(sys.argv[2]).read_text()
    passed = 0
    failed = 0

    def check(label: str, condition: bool) -> None:
        nonlocal passed, failed
        if condition:
            passed += 1
        else:
            failed += 1
            print(f"  FAIL {label}")

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

    print(f"{passed} structural checks passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
