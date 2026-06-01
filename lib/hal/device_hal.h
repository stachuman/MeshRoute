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
#include "hal.h"
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
