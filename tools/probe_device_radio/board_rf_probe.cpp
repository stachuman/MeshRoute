// V4-3 Heltec FEM probe. Compiles the real variants/heltec_v4/board_rf.cpp against counting GPIO fakes.
#include "board_rf.h"
#include "board_rf_provider.h"
#include <driver/gpio.h>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using meshroute::BoardRfKind;
using meshroute::BoardRfLnaState;
using meshroute::IBoardRf;

namespace {
int g_pass = 0;
int g_fail = 0;

#define CHK(label, expr) do {                                                               \
    const bool ok_ = static_cast<bool>(expr);                                               \
    if (ok_) ++g_pass;                                                                      \
    else { ++g_fail; std::printf("  FAIL %-62s %s\n", (label), #expr); }                  \
} while (0)

void reset_gpio(std::array<int, 2> up, std::array<int, 2> down) {
    probe_gpio::reset();
    probe_gpio::reset_holds();
    probe_gpio::pullup_reads = up;
    probe_gpio::pulldown_reads = down;
}

size_t event_at(const char* item) {
    const auto it = std::find(probe_gpio::events.begin(), probe_gpio::events.end(), item);
    return it == probe_gpio::events.end() ? probe_gpio::events.size() : static_cast<size_t>(it - probe_gpio::events.begin());
}

void probe_classifier() {
    using meshroute::classify_heltec_v4_fem;
    CHK("K1 four stable LOW samples classify GC1109",
        classify_heltec_v4_fem(false, false, false, false) == BoardRfKind::gc1109);
    CHK("K2 four stable HIGH samples classify KCT8103L",
        classify_heltec_v4_fem(true, true, true, true) == BoardRfKind::kct8103l);
    CHK("K3 floating pull-following input is unknown",
        classify_heltec_v4_fem(true, true, false, false) == BoardRfKind::unknown);
    CHK("K4 unstable pull-up samples are unknown",
        classify_heltec_v4_fem(false, true, false, false) == BoardRfKind::unknown);
    CHK("K5 unstable pull-down samples are unknown",
        classify_heltec_v4_fem(true, true, false, true) == BoardRfKind::unknown);
    CHK("K6 stable but disagreeing biases are unknown",
        classify_heltec_v4_fem(false, false, true, true) == BoardRfKind::unknown);
}

void probe_gc1109(IBoardRf& rf) {
    reset_gpio({LOW, LOW}, {LOW, LOW});
    CHK("G1 stable external pull-down initializes", rf.begin());
    CHK("G2 runtime kind is GC1109", rf.kind() == BoardRfKind::gc1109);
    CHK("G3 GC1109 has no configurable LNA state", rf.lna_state() == BoardRfLnaState::not_applicable);
    CHK("G4 LDO is driven high before its hold is released",
        event_at("write:7:1") < event_at("rtc_hold:7"));
    CHK("G5 RTC and digital stale holds use their matching APIs before sampling",
        event_at("rtc_hold:7") < event_at("mode:2:2") &&
        event_at("rtc_hold:2") < event_at("mode:2:2") &&
        event_at("gpio_hold:46") < event_at("mode:2:2") &&
        event_at("rtc_hold:5") < event_at("mode:2:2"));
    CHK("G6 both pulls are sampled twice",
        std::count(probe_gpio::events.begin(), probe_gpio::events.end(), "read:2") == 4);
    CHK("G7 detected FEM is parked disabled until arm_rx",
        probe_gpio::levels[7] == HIGH && probe_gpio::levels[2] == LOW && probe_gpio::levels[46] == LOW);

    probe_gpio::events.clear();
    CHK("G8 GC1109 RX transition succeeds", rf.rx_mode());
    CHK("G9 GC1109 RX table is LDO=1 EN=1 TX_EN=0",
        probe_gpio::levels[7] == HIGH && probe_gpio::levels[2] == HIGH && probe_gpio::levels[46] == LOW &&
        event_at("write:7:1") < event_at("write:2:1") && event_at("write:2:1") < event_at("write:46:0"));

    probe_gpio::events.clear();
    CHK("G10 GC1109 TX transition succeeds", rf.tx_mode());
    CHK("G11 GC1109 TX table is LDO=1 EN=1 TX_EN=1",
        probe_gpio::levels[7] == HIGH && probe_gpio::levels[2] == HIGH && probe_gpio::levels[46] == HIGH &&
        event_at("write:7:1") < event_at("write:2:1") && event_at("write:2:1") < event_at("write:46:1"));
}

void probe_kct8103l(IBoardRf& rf) {
    reset_gpio({HIGH, HIGH}, {HIGH, HIGH});
    CHK("L1 stable external pull-up initializes", rf.begin());
    CHK("L2 runtime kind is KCT8103L", rf.kind() == BoardRfKind::kct8103l);
    CHK("L3 approved V4.3 LNA state is on", rf.lna_state() == BoardRfLnaState::on);
    CHK("L4 detected KCT is parked disabled until arm_rx",
        probe_gpio::levels[7] == HIGH && probe_gpio::levels[2] == LOW && probe_gpio::levels[5] == LOW);

    probe_gpio::events.clear();
    CHK("L5 KCT8103L RX transition succeeds", rf.rx_mode());
    CHK("L6 KCT8103L RX/LNA-on table is LDO=1 CSD=1 CTX=0",
        probe_gpio::levels[7] == HIGH && probe_gpio::levels[2] == HIGH && probe_gpio::levels[5] == LOW &&
        event_at("write:7:1") < event_at("write:2:1") && event_at("write:2:1") < event_at("write:5:0"));

    probe_gpio::events.clear();
    CHK("L7 KCT8103L TX transition succeeds", rf.tx_mode());
    CHK("L8 KCT8103L TX table is LDO=1 CSD=1 CTX=1",
        probe_gpio::levels[7] == HIGH && probe_gpio::levels[2] == HIGH && probe_gpio::levels[5] == HIGH &&
        event_at("write:7:1") < event_at("write:2:1") && event_at("write:2:1") < event_at("write:5:1"));
}

void probe_refusals(IBoardRf& rf) {
    reset_gpio({HIGH, HIGH}, {LOW, LOW});
    CHK("U1 floating discriminator refuses initialization", !rf.begin());
    CHK("U2 floating discriminator reports unknown", rf.kind() == BoardRfKind::unknown);
    CHK("U3 unknown FEM is parked fail-closed",
        probe_gpio::levels[2] == LOW && probe_gpio::levels[46] == LOW && probe_gpio::levels[5] == LOW);
    CHK("U4 unknown FEM refuses RX and TX", !rf.rx_mode() && !rf.tx_mode());

    reset_gpio({LOW, HIGH}, {LOW, LOW});
    CHK("U5 unstable repeated samples refuse initialization", !rf.begin() && rf.kind() == BoardRfKind::unknown);

    reset_gpio({LOW, LOW}, {LOW, LOW});
    probe_gpio::hold_release_ok[46] = false;
    CHK("U6 stale-hold release failure refuses initialization", !rf.begin() && rf.kind() == BoardRfKind::unknown);
    CHK("U7 one hold failure does not skip the remaining release attempts",
        event_at("rtc_hold:7") < probe_gpio::events.size() && event_at("rtc_hold:2") < probe_gpio::events.size() &&
        event_at("gpio_hold:46") < probe_gpio::events.size() && event_at("rtc_hold:5") < probe_gpio::events.size());

    reset_gpio({LOW, LOW}, {LOW, LOW});
    probe_gpio::hold_release_ok[2] = false;
    CHK("U8 an RTC-domain stale-hold release failure also refuses initialization",
        !rf.begin() && rf.kind() == BoardRfKind::unknown);
}

void probe_capabilities(IBoardRf& rf) {
    int valid_outputs = 0;
    for (int value = -128; value <= 127; ++value) {
        const auto drive = rf.drive_for_output(static_cast<int8_t>(value));
        if (drive.valid) {
            ++valid_outputs;
            CHK("P1 sole valid requested output maps to chip drive 10", value == 22 && drive.chip_dbm == 10);
        }
    }
    CHK("P2 output capability is the singleton {22}", valid_outputs == 1);
    CHK("P3 frequency lower boundary accepted", rf.frequency_supported(863.0));
    CHK("P4 frequency upper boundary accepted", rf.frequency_supported(928.0));
    CHK("P5 below-band frequency refused", !rf.frequency_supported(862.999));
    CHK("P6 above-band frequency refused", !rf.frequency_supported(928.001));
    CHK("P7 NaN is not inside the strict V4 envelope",
        !rf.frequency_supported(std::numeric_limits<double>::quiet_NaN()));
}
}  // namespace

int main() {
    probe_classifier();
    IBoardRf* rf = meshroute::board_rf_instance();
    CHK("S1 V4 provider supplies one concrete FEM instance", rf != nullptr);
    if (rf) {
        probe_gc1109(*rf);
        probe_kct8103l(*rf);
        probe_refusals(*rf);
        probe_capabilities(*rf);
    }
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
