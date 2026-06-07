// MeshRoute — lib/hal/device_hal.h  (H3)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The DEVICE backend of meshroute::Hal — the second concrete Hal (beside the sim's FirmwareNode).
// Implements all 18 Hal methods over: IClock (now), TimerWheel (after/cancel), AirtimeLedger (the
// duty-cycle log, SAME algorithm as the sim so device==sim==Lua), an internal IRadio seam (tx /
// set_rx_sf / LBT-CAD), and a small device RNG. fw_main owns a Node + a DeviceHal and PUMPS the
// loop: service RX (radio.poll_rx -> node.on_recv) + drain timers (pop_due_timer -> node.on_timer)
// + drain pushes (node.next_push -> console).
//
// NATIVE-TESTABLE: device_hal.cpp has NO RadioLib/Arduino — only IClock + IRadio. The real
// CustomSX1262-backed IRadio is the device-only TU; native tests inject a FakeClock + MockRadio.
#pragma once
#include "../core/hal.h"   // relative: a bare "hal.h" collides with a framework header on some
                           // search orders (Windows nRF52); the ../ is unambiguous. See iradio.h.
#include "iclock.h"
#include "iradio.h"
#include "timer_wheel.h"
#include "airtime_ledger.h"

namespace meshroute {

class DeviceHal : public Hal {
public:
    DeviceHal(IClock& clock, IRadio& radio) : _clock(clock), _radio(radio) {}

    // ---- Hal radio ----
    TxResult tx(const uint8_t* bytes, size_t len, const TxParams& p) override;
    void     set_rx_sf(int sf) override;
    uint64_t channel_busy_until() override;             // CAD busy -> now + busy_hold; else 0
    uint64_t airtime_used_ms(uint64_t window_ms) override;
    uint64_t oldest_tx_end_ms() override;
    // Metal RX-window slop (bench-measured, SX1262): ~1-symbol RX_DONE demod lag (scales w/ SF) + ~30 ms
    // SPI reconfig/mode-switch (SF-flat) + ~20 ms safety. Sizes the data-SF window to the real DATA
    // RX_DONE across SF5..SF12 — airtime_ms alone falls ~1 symbol + ~30 ms short on hardware.
    uint32_t rx_window_slop_ms(int sf) const override {
        return ((1u << sf) * 1000u) / static_cast<uint32_t>(_def_bw) + 1 /*~1 symbol*/ + 50 /*~30ms reconfig + ~20ms safety*/;
    }

    // ---- Hal time/timers ----
    uint64_t now() override { return _clock.now_ms(); }
    bool     after(uint32_t delay_ms, uint32_t timer_id) override { return _wheel.after(delay_ms, timer_id, _clock.now_ms()); }
    void     cancel(uint32_t timer_id) override { _wheel.cancel(timer_id); }

    // ---- Hal identity / rng / telemetry ----
    void     set_protocol_id(int id) override { _short_id = id < 0 ? 0 : (id > 255 ? 255 : id); }
    int      rand_range(int lo, int hi) override;       // [lo,hi); xorshift32 (device determinism is irrelevant)
    void     emit(const char* type, const EventField* fields, size_t n) override { (void)type; (void)fields; (void)n; }
    void     log(const char* msg) override { (void)msg; }   // device routes telemetry over the console in fw_main, not here
    void     panic(const char* why) override { _panicked = true; _panic_why = why; }

    // ---- device-loop glue (called by fw_main, not part of the Hal contract) ----
    int      pop_due_timer() { return _wheel.pop_due(_clock.now_ms()); }   // -1 if none due
    // Async-TX pump (Step 2): drain the in-flight TX completion (-> radio re-arms RX) + start the next
    // queued frame when the radio is idle. Call every loop, after RX + the timer drain (both enqueue TX).
    void     service_tx();
    uint32_t txq_drops() const { return _txq_drops; }     // # frames dropped on outbound-queue overflow (status diagnostic)
    uint8_t  txq_depth() const { return _txq_count; }     // current outbound-queue depth (status diagnostic)
    uint32_t tx_timeouts() const { return _tx_timeouts; } // # in-flight TXs force-recovered by the watchdog (should stay 0)
    void     seed_rng(uint32_t seed) { _rng = seed ? seed : 0xA5A5A5A5u; }
    int      short_id() const { return _short_id; }
    bool     panicked() const { return _panicked; }
    const char* panic_why() const { return _panic_why; }

    // Operating-point params for airtime accounting (set from NodeConfig at boot so the device ledger
    // matches the Node's own airtime math). busy_hold = how long a CAD-busy channel reads as occupied.
    void     configure(int16_t def_sf, int32_t bw_hz, int8_t cr, int16_t preamble_sym,
                       int8_t power_dbm, uint32_t channel_busy_hold_ms) {
        _def_sf = def_sf; _def_bw = bw_hz; _def_cr = cr; _def_preamble = preamble_sym;
        _def_power = power_dbm; _busy_hold_ms = channel_busy_hold_ms;
    }

private:
    void pump_tx();                 // internal: start the head queued frame if the radio is idle (+ debit the ledger)

    // Outbound TX queue (Step 2 async TX). Half-duplex: the radio sends ONE frame at a time. tx() enqueues
    // (mirrors the sim's _pending_txs + MeshCore's `outbound`); service_tx()/pump_tx() start the next when
    // the radio is idle + debit the ledger at the actual on-air send. Bounded ring; overflow drops + counts
    // (the MAC's own timeouts recover the frame — at lbt=false this matches the sim, which never busies tx).
    struct TxQEntry { uint8_t buf[255]; uint16_t len; int16_t sf; int32_t bw; int8_t cr; int16_t pre; int8_t pw; };
    static constexpr uint8_t kTxQCap = 8;
    TxQEntry _txq[kTxQCap];
    uint8_t  _txq_head  = 0;        // ring read index
    uint8_t  _txq_count = 0;        // entries in flight in the ring
    uint32_t _txq_drops = 0;        // overflow drops (diagnostic)

    // TX-completion watchdog (mirrors MeshCore's outbound_expiry). pump_tx() sets the deadline at the
    // on-air send; service_tx() force-recovers the radio if a TxDone never arrives by then (else a missed
    // edge leaves the node deaf + mute). Generous (1.5x airtime + slop) so a normal TX never trips it.
    uint64_t _tx_deadline_ms = 0;
    uint32_t _tx_timeouts    = 0;   // # watchdog recoveries (diagnostic; should stay 0)
    // NB: device-level CSMA lives in the Node's LBT (tx_initiating/tx_flood -> channel_busy_until), which
    // correctly defers ONLY unsolicited frames. There is intentionally NO pump-level CSMA guard — it would
    // wrongly hold the time-critical CTS/DATA/ACK responses. The radio's channel_busy() (noise-floor +
    // is_receiving) is the signal the Node's LBT consumes.

    IClock&       _clock;
    IRadio&       _radio;
    TimerWheel    _wheel;
    AirtimeLedger _ledger;
    uint32_t      _rng          = 0x12345678u;
    int           _short_id     = -1;
    bool          _panicked     = false;
    const char*   _panic_why    = nullptr;
    // operating point (RF plan defaults: SF8 / BW125 / CR4-5 / 16-sym preamble / 14 dBm; CAD hold 100 ms)
    int16_t  _def_sf       = 8;
    int32_t  _def_bw       = 125000;
    int8_t   _def_cr       = 5;
    int16_t  _def_preamble = 16;
    int8_t   _def_power    = 14;
    uint32_t _busy_hold_ms = 100;
};

}  // namespace meshroute
