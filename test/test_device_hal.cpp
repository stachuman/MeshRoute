// MeshRoute — test_device_hal.cpp  (H3)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native verification of the device Hal facade (lib/hal/device_hal) over a FakeClock + a MockRadio —
// no RadioLib/Arduino. Proves the 18-method Hal logic (the async-TX outbound queue + airtime ledger,
// LBT busy-until hold, SF clamp, timers, rng, id clamp) AND that a real meshroute::Node runs end-to-end
// over the device backend (a beacon timer fires -> a frame goes out the mock radio). CHECK-only; main in
// test_airtime.cpp.
#include "doctest.h"

#include <vector>

#include "device_hal.h"
#include "iradio.h"
#include "iclock.h"
#include "airtime.h"
#include "node.h"

using namespace meshroute;

namespace {
// Scriptable IRadio for native tests: async TX (start_transmit + poll_tx_done + tx_busy, with a
// complete_tx() test hook to simulate the TxDone edge), scriptable CAD-busy + RX queue.
struct MockRadio : IRadio {
    struct Tx { std::vector<uint8_t> bytes; int16_t sf; int32_t bw; int8_t cr; int8_t power; int16_t pre; };
    std::vector<Tx> txs;                       // frames handed to start_transmit, in send order
    TxResult start_result = TxResult::ok;      // what start_transmit returns
    bool     busy         = false;             // channel_busy (CAD)
    int      last_rx_sf   = -1;
    bool     in_flight    = false;             // a start_transmit is on air
    bool     done_pending = false;             // complete_tx() armed a TxDone
    int      done_count   = 0;                 // # TXs completed (re-arm count)
    struct Rx { std::vector<uint8_t> bytes; float snr; float rssi; };
    std::vector<Rx> rx_queue;
    size_t          rx_head = 0;

    TxResult start_transmit(const uint8_t* b, size_t n, int16_t sf, int32_t bw, int8_t cr, int8_t pw, int16_t pre) override {
        if (start_result != TxResult::ok) return start_result;
        txs.push_back(Tx{ std::vector<uint8_t>(b, b + n), sf, bw, cr, pw, pre });
        in_flight = true; done_pending = false;
        return TxResult::ok;
    }
    bool poll_tx_done() override {
        if (in_flight && done_pending) { in_flight = false; done_pending = false; ++done_count; return true; }
        return false;
    }
    bool tx_busy() const override { return in_flight; }
    int  abort_count = 0;
    void abort_tx() override { in_flight = false; done_pending = false; ++abort_count; }   // watchdog recovery
    void complete_tx() { if (in_flight) done_pending = true; }   // test hook: simulate the DIO1 TxDone edge

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

TEST_CASE("DeviceHal::tx — enqueues; service_tx sends + records airtime at the on-air send (not at enqueue)") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(/*sf=*/8, /*bw=*/125000, /*cr=*/5, /*preamble=*/16, /*power=*/14, /*hold=*/100);
    clk.now = 1000;
    const uint8_t frame[10] = {1,2,3,4,5,6,7,8,9,10};
    TxParams p; p.sf = 7;                                   // bw/cr/preamble use the configured defaults
    CHECK(hal.tx(frame, sizeof(frame), p) == TxResult::ok); // enqueued
    CHECK(radio.txs.empty());                               // NOT sent yet (async)
    CHECK(hal.airtime_used_ms(3600000) == 0);              // ledger debited at the send, not at enqueue
    CHECK(hal.txq_depth() == 1);

    hal.service_tx();                                       // radio idle -> start_transmit
    CHECK(radio.txs.size() == 1);
    if (radio.txs.size() == 1) {
        CHECK(radio.txs[0].sf == 7);
        CHECK(radio.txs[0].bw == 125000);
        CHECK(radio.txs[0].bytes.size() == 10);
    }
    CHECK(hal.txq_depth() == 0);
    const uint32_t expect_air = airtime_ms(7, 125000, 5, 16, 10);
    CHECK(hal.airtime_used_ms(3600000) == expect_air);     // the ledger now holds exactly that TX's airtime
    CHECK(hal.oldest_tx_end_ms() == clk.now + expect_air);
}

TEST_CASE("DeviceHal — half-duplex: queued frames send ONE at a time, in FIFO order") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(8, 125000, 5, 16, 14, 100); clk.now = 0;
    const uint8_t a[3] = {0xA,1,2}; const uint8_t b[3] = {0xB,3,4};
    TxParams p;
    CHECK(hal.tx(a, 3, p) == TxResult::ok);
    CHECK(hal.tx(b, 3, p) == TxResult::ok);                 // both queued
    CHECK(hal.txq_depth() == 2);

    hal.service_tx();                                       // A starts
    CHECK(radio.txs.size() == 1);
    if (radio.txs.size() == 1) CHECK(radio.txs[0].bytes[0] == 0xA);
    hal.service_tx();                                       // B BLOCKED while A is in flight
    CHECK(radio.txs.size() == 1);
    CHECK(radio.tx_busy());

    radio.complete_tx(); hal.service_tx();                  // A done -> B starts
    CHECK(radio.txs.size() == 2);
    if (radio.txs.size() == 2) CHECK(radio.txs[1].bytes[0] == 0xB);
    CHECK(radio.done_count == 1);                           // exactly one completion drained
}

TEST_CASE("DeviceHal — outbound queue overflow drops + counts (the MAC's own timeouts recover the frame)") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(8, 125000, 5, 16, 14, 100);
    const uint8_t f[2] = {1,2}; TxParams p;
    int ok = 0;
    for (int i = 0; i < 64; ++i) if (hal.tx(f, 2, p) == TxResult::ok) ok++;   // fill the ring without pumping
    CHECK(ok > 0);
    CHECK(hal.txq_drops() > 0);                             // past the cap -> dropped
    CHECK(ok + static_cast<int>(hal.txq_drops()) == 64);   // every call either queued or counted as a drop
}

TEST_CASE("DeviceHal::tx — len > 255 rejected as too_long (SX1262 length register); nothing queued") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(8, 125000, 5, 16, 14, 100);
    std::vector<uint8_t> big(256, 0xEE); TxParams p;
    CHECK(hal.tx(big.data(), big.size(), p) == TxResult::too_long);
    CHECK(hal.txq_depth() == 0);
    hal.service_tx(); CHECK(radio.txs.empty());
}

TEST_CASE("DeviceHal — a radio that refuses start_transmit: frame dropped, airtime NOT recorded, queue not stuck") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(8, 125000, 5, 16, 14, 100); clk.now = 1000;
    radio.start_result = TxResult::radio_error;
    const uint8_t f[4] = {1,2,3,4}; TxParams p; p.sf = 7;
    CHECK(hal.tx(f, 4, p) == TxResult::ok);                 // enqueue still succeeds
    hal.service_tx();                                       // start_transmit -> radio_error
    CHECK(radio.txs.empty());                               // not recorded by the mock
    CHECK(hal.airtime_used_ms(3600000) == 0);              // ledger NOT debited
    CHECK(hal.txq_depth() == 0);                            // the bad frame was popped, not stuck
}

TEST_CASE("DeviceHal — TX watchdog: a missed TxDone is force-recovered past the deadline; the queue resumes") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(/*sf=*/12, 125000, 5, 16, 14, 100);      // SF12 -> long airtime (a generous deadline)
    clk.now = 0;
    const uint8_t a[20] = {0xA}; const uint8_t b[3] = {0xB,1,2};
    TxParams p; p.sf = 12;
    CHECK(hal.tx(a, 20, p) == TxResult::ok);
    CHECK(hal.tx(b, 3,  p) == TxResult::ok);
    hal.service_tx();                                       // A starts (in flight)
    CHECK(radio.tx_busy());
    CHECK(hal.tx_timeouts() == 0);

    // Simulate a LOST TxDone (never complete_tx). Before the deadline -> no recovery, B stays queued.
    clk.now = 100; hal.service_tx();
    CHECK(radio.tx_busy());
    CHECK(hal.tx_timeouts() == 0);
    CHECK(radio.txs.size() == 1);                           // B not started while A is "in flight"

    // Well past 1.5x any SF12 airtime -> the watchdog recovers the radio + the queue resumes with B.
    clk.now = 60000; hal.service_tx();
    CHECK(hal.tx_timeouts() == 1);
    CHECK(radio.abort_count == 1);
    CHECK(radio.tx_busy());                                 // B is now the in-flight TX (radio busy again)
    CHECK(radio.txs.size() == 2);
    if (radio.txs.size() == 2) CHECK(radio.txs[1].bytes[0] == 0xB);
}

TEST_CASE("DeviceHal — TX watchdog does NOT fire on a timely completion") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(12, 125000, 5, 16, 14, 100); clk.now = 0;
    const uint8_t f[20] = {1}; TxParams p; p.sf = 12;
    CHECK(hal.tx(f, 20, p) == TxResult::ok);
    hal.service_tx();                                       // starts
    clk.now = 100; radio.complete_tx(); hal.service_tx();   // completes promptly (before the deadline)
    CHECK(!radio.tx_busy());
    CHECK(hal.tx_timeouts() == 0);                          // no false recovery
    CHECK(radio.abort_count == 0);
}

TEST_CASE("DeviceHal CSMA — LBT on: a busy channel holds the frame; it sends once the channel clears") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(8, 125000, 5, 16, 14, 100); hal.set_lbt(true); clk.now = 0;
    const uint8_t f[3] = {1,2,3}; TxParams p;
    radio.busy = true;                                     // channel busy (carrier sensed)
    CHECK(hal.tx(f, 3, p) == TxResult::ok);
    hal.service_tx();  CHECK(radio.txs.empty());           // held off the busy channel
    clk.now = 50; hal.service_tx(); CHECK(radio.txs.empty()); // still busy, within the max defer
    CHECK(hal.csma_defers() == 1);                         // one hold episode counted
    radio.busy = false; hal.service_tx();                  // channel clear -> sends
    CHECK(radio.txs.size() == 1);
}

TEST_CASE("DeviceHal CSMA — LBT on: a persistently busy channel force-sends past the max defer (no starvation)") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(8, 125000, 5, 16, 14, 100); hal.set_lbt(true); clk.now = 0;
    const uint8_t f[3] = {1,2,3}; TxParams p;
    radio.busy = true;                                     // never clears
    CHECK(hal.tx(f, 3, p) == TxResult::ok);
    hal.service_tx(); CHECK(radio.txs.empty());            // held
    clk.now = 100000; hal.service_tx();                    // well past kCsmaMaxDeferMs -> force-send
    CHECK(radio.txs.size() == 1);
}

TEST_CASE("DeviceHal CSMA — LBT off: a busy channel does NOT hold (sim parity / Step-2 behaviour)") {
    FakeClock clk; MockRadio radio; DeviceHal hal(clk, radio);
    hal.configure(8, 125000, 5, 16, 14, 100); /* set_lbt defaults off */ clk.now = 0;
    const uint8_t f[3] = {1,2,3}; TxParams p;
    radio.busy = true;
    CHECK(hal.tx(f, 3, p) == TxResult::ok);
    hal.service_tx(); CHECK(radio.txs.size() == 1);        // sent regardless of the busy channel
    CHECK(hal.csma_defers() == 0);
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
    // Pump the device loop a while: advance time + drain due timers into the Node, then service the
    // async-TX queue (start the beacon, complete it, drain the completion) — the Step-2 loop shape.
    bool beaconed = false;
    for (uint64_t t = 0; t <= 200000 && !beaconed; t += 1000) {
        clk.now = t;
        for (int guard = 0; guard < 256; ++guard) {
            int id = hal.pop_due_timer();
            if (id < 0) break;
            node.on_timer(static_cast<uint32_t>(id));
        }
        hal.service_tx();                              // start a queued beacon
        radio.complete_tx();                           // simulate its TxDone
        hal.service_tx();                              // drain the completion (re-arm)
        if (!radio.txs.empty()) beaconed = true;
    }
    CHECK(beaconed);                                    // the Node emitted a frame THROUGH the device Hal
    CHECK(!radio.txs.empty());
    if (!radio.txs.empty()) CHECK(radio.txs[0].sf == 7);   // on the routing SF
    CHECK(hal.airtime_used_ms(3600000) > 0);            // and the device ledger recorded its airtime
}
