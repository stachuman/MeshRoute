// MeshRoute — lib/hal/timer_wheel.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// H1 — the device-backend timer wheel behind Hal::after()/cancel(). Honors the contract
// (lib/core/hal.h): caller-allocated one-shot ids, (re)arm-by-id, bounded. The Node owns
// the timer ids: 1..63 DENSE (singletons + slot-ring ranges, e.g. SyncResponse [32..47],
// flood [61..63]); 64..79 = the band reserved for the Slice-3 per-layer gateway scheduler
// (window open/close + per-leaf beacon). All < kCap.
//
// DRIFT (vs MeshCore's absolute-timestamp priority queue): a flat array INDEXED BY timer_id
// — heap-free, O(kCap) per tick, and re-arm-by-id is a single store. Simpler + native-testable
// for fire-ordering / cancel / re-arm without millis() or RadioLib.
//
// The device_hal owns the wheel + the Node and PUMPS it each loop:
//     while ((int id = wheel.pop_due(clock.now_ms())) >= 0) node.on_timer((uint32_t)id);
// pop_due returns the earliest-due id (tie: smallest id) and deactivates it; a fired timer may
// re-arm itself (positive delay -> not due this pump), so the drain loop terminates.
#pragma once
#include <cstdint>

namespace meshroute {

class TimerWheel {
public:
    static constexpr uint32_t kCap = 82;   // matches the Hal "cap" caller-id contract (64->80 in Slice 3b for the dual-layer
                                           // gateway band; 80->82 in §S6 for the presence probe/roster [78,79] + the mobile
                                           // OFFER-backoff de-storm [80]; ids 1..63 are dense)

    // false ONLY if timer_id is out of range (>= kCap); otherwise (re)arms id to fire at now+delay.
    bool after(uint32_t delay_ms, uint32_t timer_id, uint64_t now_ms);
    void cancel(uint32_t timer_id);                  // idempotent; no-op for out-of-range / inactive

    // The next id whose deadline has elapsed (<= now), earliest-first then smallest-id; deactivates
    // it and returns it. -1 when nothing is due. Call in a while-loop to drain a tick.
    int  pop_due(uint64_t now_ms);

    bool     active(uint32_t timer_id) const;        // test/introspection
    uint64_t due_at(uint32_t timer_id) const;        // test/introspection (undefined if inactive)

    // The earliest active deadline (min _due over active ids), or UINT64_MAX if nothing is armed. Bounds an
    // idle sleep: fw_main halts the CPU until this time (or a radio/console IRQ wakes it sooner). O(kCap).
    uint64_t earliest_due() const;

private:
    bool     _active[kCap] = {};
    uint64_t _due[kCap]    = {};
};

}  // namespace meshroute
