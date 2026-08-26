// V4-3 radio sequencing probe. This compiles the REAL lib/hal/device_radio.h against counting silicon/FEM fakes.
#include "device_radio.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

using meshroute::BoardRfDrive;
using meshroute::BoardRfKind;
using meshroute::BoardRfLnaState;
using meshroute::IBoardRf;
using meshroute::Sx1262Radio;
using meshroute::TxResult;

namespace {

int g_pass = 0;
int g_fail = 0;

#define CHK(label, expr) do {                                                               \
    const bool ok_ = static_cast<bool>(expr);                                               \
    if (ok_) ++g_pass;                                                                      \
    else { ++g_fail; std::printf("  FAIL %-62s %s\n", (label), #expr); }                  \
} while (0)

struct CountingRf final : IBoardRf {
    explicit CountingRf(std::vector<std::string>& log) : events(log) {}

    std::vector<std::string>& events;
    bool begin_result = true;
    bool tx_result = true;
    bool rx_result = true;
    BoardRfDrive drive{true, 10};

    bool begin() override { events.emplace_back("fem.begin"); return begin_result; }
    bool tx_mode() override { events.emplace_back("fem.tx"); return tx_result; }
    bool rx_mode() override { events.emplace_back("fem.rx"); return rx_result; }
    BoardRfKind kind() const override { return BoardRfKind::gc1109; }
    BoardRfLnaState lna_state() const override { return BoardRfLnaState::on; }
    bool frequency_supported(double mhz) const override { return mhz >= 863.0 && mhz <= 928.0; }
    BoardRfDrive drive_for_output(int8_t output_dbm) const override {
        events.emplace_back("fem.drive:" + std::to_string(static_cast<int>(output_dbm)));
        return output_dbm == 22 ? drive : BoardRfDrive{false, 0};
    }
};

bool exact(const std::vector<std::string>& got, std::initializer_list<const char*> want) {
    if (got.size() != want.size()) return false;
    size_t i = 0;
    for (const char* item : want) if (got[i++] != item) return false;
    return true;
}

bool ends_with(const std::vector<std::string>& got, std::initializer_list<const char*> want) {
    if (got.size() < want.size()) return false;
    size_t i = got.size() - want.size();
    for (const char* item : want) if (got[i++] != item) return false;
    return true;
}

void clear(CustomSX1262& chip) { chip.events.clear(); }

void probe_names() {
    CHK("N1 BoardRfKind::none name", std::string(meshroute::board_rf_kind_name(BoardRfKind::none)) == "none");
    CHK("N2 BoardRfKind::gc1109 name", std::string(meshroute::board_rf_kind_name(BoardRfKind::gc1109)) == "gc1109");
    CHK("N3 BoardRfKind::kct8103l name", std::string(meshroute::board_rf_kind_name(BoardRfKind::kct8103l)) == "kct8103l");
    CHK("N4 BoardRfKind::unknown name", std::string(meshroute::board_rf_kind_name(BoardRfKind::unknown)) == "unknown");
    CHK("N5 LNA n/a name", std::string(meshroute::board_rf_lna_state_name(BoardRfLnaState::not_applicable)) == "n/a");
    CHK("N6 LNA on name", std::string(meshroute::board_rf_lna_state_name(BoardRfLnaState::on)) == "on");
    CHK("N7 LNA bypass name", std::string(meshroute::board_rf_lna_state_name(BoardRfLnaState::bypass)) == "bypass");
}

void probe_begin() {
    CustomSX1262 chip;
    CountingRf rf(chip.events);
    Sx1262Radio radio(chip, &rf);

    CHK("B1 FEM begin + initial arm succeeds", radio.begin());
    CHK("B2 FEM begin precedes rx_mode and sole startReceive",
        exact(chip.events, {"radio.set_action", "fem.begin", "fem.rx", "radio.start_rx"}));
    CHK("B3 begin exposes detected FEM kind", radio.board_rf_kind() == BoardRfKind::gc1109);
    CHK("B4 begin exposes LNA state", radio.board_rf_lna_state() == BoardRfLnaState::on);
    CHK("B5 successful begin counts no transition failure", radio.rf_mode_failures() == 0);

    CustomSX1262 begin_fail_chip;
    CountingRf begin_fail_rf(begin_fail_chip.events);
    begin_fail_rf.begin_result = false;
    Sx1262Radio begin_fail(begin_fail_chip, &begin_fail_rf);
    CHK("B6 failed FEM begin makes begin false", !begin_fail.begin());
    CHK("B7 failed FEM begin emits no rx_mode/startReceive",
        exact(begin_fail_chip.events, {"radio.set_action", "fem.begin"}));
    CHK("B8 failed FEM begin is counted", begin_fail.rf_mode_failures() == 1);

    CustomSX1262 rx_fail_chip;
    CountingRf rx_fail_rf(rx_fail_chip.events);
    rx_fail_rf.rx_result = false;
    Sx1262Radio rx_fail(rx_fail_chip, &rx_fail_rf);
    CHK("B9 failed initial rx_mode makes begin false", !rx_fail.begin());
    CHK("B10 failed initial rx_mode emits no startReceive",
        exact(rx_fail_chip.events, {"radio.set_action", "fem.begin", "fem.rx"}));
    CHK("B11 failed initial rx_mode is counted", rx_fail.rf_mode_failures() == 1);

    CustomSX1262 arm_fail_chip;
    arm_fail_chip.start_receive_result = -7;
    CountingRf arm_fail_rf(arm_fail_chip.events);
    Sx1262Radio arm_fail(arm_fail_chip, &arm_fail_rf);
    CHK("B12 failed initial RadioLib arm makes begin false", !arm_fail.begin());
    CHK("B13 RadioLib arm failure follows successful FEM RX",
        ends_with(arm_fail_chip.events, {"fem.rx", "radio.start_rx"}));
    CHK("B14 RadioLib arm failure preserves its existing counter", arm_fail.rx_arm_failures() == 1);
}

void probe_tx_and_recovery() {
    const uint8_t frame[3] = {0x10, 0x20, 0x30};
    CustomSX1262 chip;
    CountingRf rf(chip.events);
    Sx1262Radio radio(chip, &rf);
    CHK("T-1 boot carrier applies before begin", radio.apply_frequency(869.4625, /*rearm=*/false));
    CHK("T0 setup begin succeeds", radio.begin());
    clear(chip);

    CHK("T1 translated FEM transmit arms", radio.start_transmit(frame, sizeof frame, 10, 125000, 5, 22, 16) == TxResult::ok);
    CHK("T2 translation is the first TX preflight operation", !chip.events.empty() && chip.events.front() == "fem.drive:22");
    CHK("T3 translated chip drive, then tx_mode, then sole startTransmit",
        exact(chip.events, {"fem.drive:22", "radio.standby", "radio.sf:10", "radio.bw", "radio.cr:5",
                            "radio.power:10", "radio.pre:16", "fem.tx", "radio.start_tx"}));
    CHK("T4 requested 22 dBm never reaches setOutputPower directly", chip.last_power == 10);
    CHK("T5 successful arm is in flight", radio.tx_busy());

    clear(chip);
    meshroute::g_dio1_fired = true;
    CHK("C1 TxDone completes exactly once", radio.poll_tx_done());
    CHK("C2 completion finishes TX then restores FEM RX + RadioLib RX",
        exact(chip.events, {"radio.finish_tx", "fem.rx", "radio.start_rx"}));
    CHK("C3 completed TX is no longer busy", !radio.tx_busy());
    CHK("C4 completion is drained exactly once", !radio.poll_tx_done());

    clear(chip);
    rf.drive.valid = false;
    CHK("T6 invalid translation is refused", radio.start_transmit(frame, sizeof frame, 8, 125000, 5, 22, 16) == TxResult::radio_error);
    CHK("T7 invalid translation leaves the SX1262/RX untouched", exact(chip.events, {"fem.drive:22"}));
    rf.drive = BoardRfDrive{true, 10};

    clear(chip);
    rf.tx_result = false;
    CHK("T8 failed tx_mode is refused", radio.start_transmit(frame, sizeof frame, 8, 125000, 5, 22, 16) == TxResult::radio_error);
    CHK("T9 failed tx_mode emits no startTransmit and restores RX",
        ends_with(chip.events, {"fem.tx", "fem.rx", "radio.start_rx"}) &&
        std::find(chip.events.begin(), chip.events.end(), "radio.start_tx") == chip.events.end());
    CHK("T10 failed tx_mode is counted", radio.rf_mode_failures() == 1);
    rf.tx_result = true;

    clear(chip);
    chip.start_transmit_result = -9;
    CHK("T11 failed RadioLib startTransmit is refused", radio.start_transmit(frame, sizeof frame, 8, 125000, 5, 22, 16) == TxResult::radio_error);
    CHK("T12 failed startTransmit restores FEM RX + RadioLib RX",
        ends_with(chip.events, {"fem.tx", "radio.start_tx", "fem.rx", "radio.start_rx"}));
    CHK("T13 a failed arm is not in flight", !radio.tx_busy());
    chip.start_transmit_result = RADIOLIB_ERR_NONE;

    clear(chip);
    CHK("A1 abort setup transmit arms", radio.start_transmit(frame, sizeof frame, 9, 125000, 5, 22, 16) == TxResult::ok);
    clear(chip);
    radio.abort_tx();
    CHK("A2 abort stops TX then restores FEM RX + RadioLib RX",
        exact(chip.events, {"radio.standby", "fem.rx", "radio.start_rx"}));
    CHK("A3 abort clears in-flight state", !radio.tx_busy());
}

void probe_retunes_and_rx_drain() {
    CustomSX1262 chip;
    CountingRf rf(chip.events);
    Sx1262Radio radio(chip, &rf);
    CHK("R0 setup begin succeeds", radio.begin());

    clear(chip);
    radio.set_rx_sf(9);
    CHK("R1 SF retune restores FEM RX before RadioLib RX",
        exact(chip.events, {"radio.standby", "radio.sf:9", "fem.rx", "radio.start_rx"}));

    clear(chip);
    radio.set_rx_freq(869.4625);
    CHK("R2 frequency retune restores FEM RX before RadioLib RX",
        exact(chip.events, {"radio.standby", "radio.freq", "fem.rx", "radio.start_rx"}));

    clear(chip);
    radio.set_rx_bw(125000);
    CHK("R3 bandwidth retune restores FEM RX before RadioLib RX",
        exact(chip.events, {"radio.standby", "radio.bw", "fem.rx", "radio.start_rx"}));

    clear(chip);
    radio.set_rx_cr(5);
    CHK("R4 coding-rate retune restores FEM RX before RadioLib RX",
        exact(chip.events, {"radio.standby", "radio.cr:5", "fem.rx", "radio.start_rx"}));

    clear(chip);
    chip.irq_flags = RADIOLIB_SX126X_IRQ_RX_DONE;
    meshroute::g_dio1_fired = true;
    uint8_t buf[8]{};
    size_t out_len = 0;
    float snr = 0.0f, rssi = 0.0f;
    CHK("R5 packet drain succeeds", radio.poll_rx(buf, sizeof buf, out_len, snr, rssi));
    CHK("R6 packet drain restores FEM RX before RadioLib RX", ends_with(chip.events, {"fem.rx", "radio.start_rx"}));
    CHK("R7 packet drain preserves decoded length", out_len == 3);
}

void probe_frequency_and_config_truth() {
    const uint8_t frame[2] = {0x10, 0x20};
    CustomSX1262 chip;
    CountingRf rf(chip.events);
    Sx1262Radio radio(chip, &rf);

    CHK("F1 external-FEM carrier starts invalid", !radio.frequency_valid());
    CHK("F2 hardware begin is independent of an invalid config latch", radio.begin());
    CHK("F3 valid output alone cannot make rfcfg true", !radio.rf_config_valid(22));

    clear(chip);
    CHK("F4 lower boundary is accepted", radio.apply_frequency(863.0, /*rearm=*/false));
    CHK("F5 accepted carrier uses standby then the sole setFrequency", exact(chip.events, {"radio.standby", "radio.freq"}));
    CHK("F6 lower boundary becomes current", chip.last_frequency == 863.0f && radio.frequency_valid());
    clear(chip);
    CHK("F7 upper boundary is accepted", radio.apply_frequency(928.0, /*rearm=*/false));
    CHK("F8 upper boundary becomes current", chip.last_frequency == 928.0f && radio.frequency_valid());

    clear(chip);
    CHK("F9 below-band carrier is refused", !radio.apply_frequency(862.999, /*rearm=*/false));
    CHK("F10 invalid validation precedes standby and leaves RX untouched", chip.events.empty());
    CHK("F11 below-band refusal marks config invalid and counts once",
        !radio.frequency_valid() && radio.rf_band_failures() == 1);
    CHK("F12 TX is refused before FEM/radio work while carrier is invalid",
        radio.start_transmit(frame, sizeof frame, 8, 125000, 5, 22, 16) == TxResult::radio_error && chip.events.empty());

    clear(chip);
    CHK("F13 later valid carrier clears the latch without reboot", radio.apply_frequency(869.4625, /*rearm=*/false));
    clear(chip);
    CHK("F14 singleton nominal output maps to chip drive 10",
        radio.output_drive_for(22).valid && radio.output_drive_for(22).chip_dbm == 10);
    CHK("F15 other requested output is unsupported", !radio.output_supported(21));
    CHK("F16 valid carrier plus 22 dBm makes rfcfg true", radio.rf_config_valid(22));
    CHK("F17 invalid output makes rfcfg false without changing carrier", !radio.rf_config_valid(21) && radio.frequency_valid());

    uint8_t buf[8]{};
    size_t out_len = 0;
    float snr = 0.0f, rssi = 0.0f;
    chip.irq_flags = SX126X_IRQ_PREAMBLE_DETECTED;
    meshroute::g_dio1_fired = false;
    CHK("F18 precondition latches one preamble window",
        !radio.poll_rx(buf, sizeof buf, out_len, snr, rssi) && radio.take_preamble());

    clear(chip);
    meshroute::g_dio1_fired = true;
    chip.set_frequency_result = -11;
    CHK("F19a RadioLib frequency failure is surfaced", !radio.apply_frequency(870.0, /*rearm=*/true));
    CHK("F19b failed live apply restores FEM RX and continuous RX",
        exact(chip.events, {"radio.standby", "radio.freq", "fem.rx", "radio.start_rx"}));
    CHK("F19c failed live apply clears the stale DIO1 edge", !meshroute::g_dio1_fired);
    clear(chip);
    CHK("F19d stale DIO1 cannot drain a phantom packet",
        !radio.poll_rx(buf, sizeof buf, out_len, snr, rssi));
    CHK("F19e phantom-packet check never reaches readData",
        std::find(chip.events.begin(), chip.events.end(), "radio.read") == chip.events.end());
    CHK("F19f failed live apply starts a fresh preamble window", radio.take_preamble());
    CHK("F20 RadioLib frequency failure invalidates config and increments the same counter",
        !radio.frequency_valid() && radio.rf_band_failures() == 2);

    chip.set_frequency_result = RADIOLIB_ERR_NONE;
    chip.start_receive_result = -12;
    clear(chip);
    CHK("F21 successful frequency apply surfaces a failed RX rearm",
        !radio.apply_frequency(869.4625, /*rearm=*/true));
    CHK("F22 failed RX rearm still follows the FEM/RX sequence",
        exact(chip.events, {"radio.standby", "radio.freq", "fem.rx", "radio.start_rx"}));
    chip.start_receive_result = RADIOLIB_ERR_NONE;
    clear(chip);
    CHK("F23 failed RX rearm also starts a fresh preamble window",
        !radio.poll_rx(buf, sizeof buf, out_len, snr, rssi) && radio.take_preamble());

    clear(chip);
    CHK("F24 valid correction after the failed rearm recovers immediately",
        radio.apply_frequency(869.4625, /*rearm=*/true));
    CHK("F25 valid live correction re-arms FEM then RadioLib RX",
        exact(chip.events, {"radio.standby", "radio.freq", "fem.rx", "radio.start_rx"}));

    CustomSX1262 failed_chip;
    CountingRf failed_rf(failed_chip.events);
    failed_rf.begin_result = false;
    Sx1262Radio failed_hw(failed_chip, &failed_rf);
    CHK("F26 valid config can be established before failed hardware begin",
        failed_hw.apply_frequency(869.4625, /*rearm=*/false) && failed_hw.rf_config_valid(22));
    CHK("F27 failed hardware begin does not erase independent rfcfg truth", !failed_hw.begin() && failed_hw.rf_config_valid(22));

    clear(chip);
    radio.set_rx_freq(929.0);
    CHK("F28 gateway/adopted out-of-band retune has the same no-standby refusal", chip.events.empty());
    CHK("F29 gateway/adopted refusal also blocks TX", !radio.frequency_valid());
    clear(chip);
    radio.set_rx_freq(928.0);
    CHK("F30 valid gateway retune clears the latch and restores RX",
        radio.frequency_valid() && exact(chip.events, {"radio.standby", "radio.freq", "fem.rx", "radio.start_rx"}));
}

void probe_null_fem() {
    const uint8_t frame[2] = {0x10, 0x20};
    CustomSX1262 chip;
    Sx1262Radio radio(chip, nullptr);

    CHK("Z1 null-FEM begin succeeds", radio.begin());
    CHK("Z2 null-FEM begin is the direct existing-board order",
        exact(chip.events, {"radio.set_action", "radio.start_rx"}));
    CHK("Z3 null-FEM kind is none", radio.board_rf_kind() == BoardRfKind::none);
    CHK("Z4 null-FEM LNA is n/a", radio.board_rf_lna_state() == BoardRfLnaState::not_applicable);
    CHK("Z5 null-FEM transition count stays zero", radio.rf_mode_failures() == 0);

    clear(chip);
    CHK("Z6 null-FEM transmit preserves return behavior",
        radio.start_transmit(frame, sizeof frame, 8, 125000, 5, 7, 16) == TxResult::ok);
    CHK("Z7 null-FEM power mapping is identity", chip.last_power == 7);
    CHK("Z8 null-FEM path emits no FEM calls",
        exact(chip.events, {"radio.standby", "radio.sf:8", "radio.bw", "radio.cr:5", "radio.power:7",
                            "radio.pre:16", "radio.start_tx"}));
    meshroute::g_dio1_fired = true;
    (void)radio.poll_tx_done();

    clear(chip);
    chip.start_transmit_result = -3;
    CHK("Z9 null-FEM failed startTransmit preserves radio_error",
        radio.start_transmit(frame, sizeof frame, 8, 125000, 5, 7, 16) == TxResult::radio_error);
    CHK("Z10 null-FEM failed startTransmit restores direct continuous RX",
        ends_with(chip.events, {"radio.start_tx", "radio.start_rx"}));
}

}  // namespace

int main() {
    probe_names();
    probe_begin();
    probe_tx_and_recovery();
    probe_retunes_and_rx_drain();
    probe_frequency_and_config_truth();
    probe_null_fem();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
