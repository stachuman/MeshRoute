// MeshRoute — test_device_hal.cpp  (H3)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native verification of the device Hal facade (lib/hal/device_hal) over a FakeClock + a MockRadio —
// no RadioLib/Arduino. Proves the 18-method Hal logic (tx airtime ledger, LBT busy-until hold, SF
// clamp, timers, rng, id clamp) AND that a real meshroute::Node runs end-to-end over the device
// backend (a beacon timer fires -> a frame goes out the mock radio). CHECK-only; main in test_airtime.cpp.
#include "doctest.h"

#include <vector>

#include "device_hal.h"
#include "iradio.h"
#include "iclock.h"
#include "airtime.h"
#include "node.h"

using namespace meshroute;

namespace {
// Scriptable IRadio for native tests: captures TX, scriptable CAD-busy + RX queue.
struct MockRadio : IRadio {
    struct Tx { std::vector<uint8_t> bytes; int16_t sf; int32_t bw; int8_t cr; int8_t power; int16_t pre; };
    std::vector<Tx> txs;
    TxResult result = TxResult::ok;
    bool     busy = false;
    int      last_rx_sf = -1;
    struct Rx { std::vector<uint8_t> bytes; float snr; float rssi; };
    std::vector<Rx> rx_queue;
    size_t          rx_head = 0;

    TxResult transmit(const uint8_t* b, size_t n, int16_t sf, int32_t bw, int8_t cr, int8_t pw, int16_t pre) override {
        if (result == TxResult::ok) txs.push_back(Tx{ std::vector<uint8_t>(b, b + n), sf, bw, cr, pw, pre });
        return result;
    }
    void set_rx_sf(int sf) override { last_rx_sf = sf; }
    bool channel_busy() override { return busy; }
    bool poll_rx(uint8_t* buf, size_t cap, size_t& out_len, float& snr, float& rssi) override {
        if (rx_head >= rx_queue.size()) return false;
        const Rx& r = rx_queue[rx_head++];
        out_len = r.bytes.size() < cap ? r.bytes.size() : cap;
        for (size_t i = 0; i < out_len; ++i) buf[i] = r.bytes[i];
        snr = r.snr; rssi = r.rssi; return true;
    }
};
}  // namespace

TEST_CASE("DeviceHal::tx — forwards to the radio + records the on-air airtime into the duty ledger") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(/*sf=*/8, /*bw=*/125000, /*cr=*/5, /*preamble=*/16, /*power=*/14, /*hold=*/100);
    clk.now = 1000;
    const uint8_t frame[10] = {1,2,3,4,5,6,7,8,9,10};
    TxParams p; p.sf = 7;                                  // bw/cr/preamble use the configured defaults
    CHECK(hal.tx(frame, sizeof(frame), p) == TxResult::ok);
    CHECK(radio.txs.size() == 1);
    if (radio.txs.size() == 1) {
        CHECK(radio.txs[0].sf == 7);
        CHECK(radio.txs[0].bw == 125000);
        CHECK(radio.txs[0].bytes.size() == 10);
    }
    const uint32_t expect_air = airtime_ms(7, 125000, 5, 16, 10);
    CHECK(hal.airtime_used_ms(3600000) == expect_air);     // the ledger holds exactly that TX's airtime
    CHECK(hal.oldest_tx_end_ms() == clk.now + expect_air);
    // a failed TX is NOT recorded
    radio.result = TxResult::radio_error;
    CHECK(hal.tx(frame, sizeof(frame), p) == TxResult::radio_error);
    CHECK(hal.airtime_used_ms(3600000) == expect_air);     // unchanged — only on-air TX counts
}

TEST_CASE("DeviceHal::channel_busy_until — CAD busy -> now+hold; clear -> 0") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(8, 125000, 5, 16, 14, /*hold=*/100);
    clk.now = 5000;
    radio.busy = false; CHECK(hal.channel_busy_until() == 0);
    radio.busy = true;  CHECK(hal.channel_busy_until() == 5100);   // now + hold
}

TEST_CASE("DeviceHal::set_rx_sf — clamps to 5..12 + forwards to the radio") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.set_rx_sf(7);  CHECK(radio.last_rx_sf == 7);
    hal.set_rx_sf(3);  CHECK(radio.last_rx_sf == 5);    // clamp low
    hal.set_rx_sf(15); CHECK(radio.last_rx_sf == 12);   // clamp high
}

TEST_CASE("DeviceHal — after/cancel/pop_due_timer delegate to the wheel over the clock") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    clk.now = 1000;
    CHECK(hal.after(50, /*id=*/5));
    clk.now = 1049; CHECK(hal.pop_due_timer() == -1);
    clk.now = 1050; CHECK(hal.pop_due_timer() == 5);
    CHECK(hal.after(20, /*id=*/9));
    hal.cancel(9);
    clk.now = 5000; CHECK(hal.pop_due_timer() == -1);   // cancelled
}

TEST_CASE("DeviceHal — rand_range is [lo,hi), degenerate -> lo; set_protocol_id clamps [0,255]") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.seed_rng(12345);
    for (int i = 0; i < 200; ++i) { int r = hal.rand_range(3, 9); CHECK(r >= 3); CHECK(r < 9); }
    CHECK(hal.rand_range(5, 5) == 5);                   // empty range
    CHECK(hal.rand_range(8, 2) == 8);                   // hi<=lo
    hal.set_protocol_id(300); CHECK(hal.short_id() == 255);
    hal.set_protocol_id(-7);  CHECK(hal.short_id() == 0);
    hal.set_protocol_id(42);  CHECK(hal.short_id() == 42);
}

TEST_CASE("DeviceHal — a real Node runs over the device backend: a beacon timer fires -> a frame goes out the radio") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(/*sf=*/7, 125000, 5, 16, 14, 100);
    hal.seed_rng(1);
    Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);                                  // schedules the first beacon (after -> the wheel)
    // Pump the device loop a while: advance time + drain due timers into the Node.
    bool beaconed = false;
    for (uint64_t t = 0; t <= 200000 && !beaconed; t += 1000) {
        clk.now = t;
        for (int guard = 0; guard < 256; ++guard) {
            int id = hal.pop_due_timer();
            if (id < 0) break;
            node.on_timer(static_cast<uint32_t>(id));
            if (!radio.txs.empty()) beaconed = true;
        }
    }
    CHECK(beaconed);                                    // the Node emitted a frame THROUGH the device Hal
    CHECK(!radio.txs.empty());
    if (!radio.txs.empty()) CHECK(radio.txs[0].sf == 7);   // on the routing SF
    CHECK(hal.airtime_used_ms(3600000) > 0);            // and the device ledger recorded its airtime
}
