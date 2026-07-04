// MeshRoute — lib/hal/airtime_ledger.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The sliding-window airtime log behind the device Hal's airtime_used_ms() /
// oldest_tx_end_ms(). THE algorithm must be identical to the sim's
// FirmwareNode::airtimeUsedInWindow (cutoff = now - window; evict end_ms <=
// cutoff; sum the rest) so device == sim == Lua on the duty-cycle decision the
// protocol owns. Fixed-size ring (no heap).
//
// H5 (2026-07-04, REGULATORY): on overflow the ring COALESCES its two oldest
// records (sum airtime, keep the newer end_ms) instead of DROPPING the oldest.
// Dropping discarded in-window airtime -> used_in_window() UNDER-reported ->
// the node kept transmitting past the legal EU868 1% duty (worse the busier the
// relay). Coalescing never loses in-window airtime, so used_in_window() can only
// OVER-report by at most the age-slack of a merged record's oldest member — the
// SAFE direction (throttle a hair early, stay legal). The sim's deque is
// unbounded (exact); the device is now slightly CONSERVATIVE vs it. Acceptable.
#pragma once
#include <cstdint>

namespace meshroute {

class AirtimeLedger {
public:
    void     record(uint64_t end_ms, uint32_t airtime_ms);   // on TX completion
    uint64_t used_in_window(uint64_t now_ms, uint64_t window_ms);  // evicts expired + sums
    uint64_t oldest_tx_end_ms() const;                       // 0 if empty

private:
    static constexpr uint8_t kCap = 64;                      // ample at LoRa duty rates
    struct Rec { uint64_t end_ms; uint32_t airtime_ms; };
    Rec     _ring[kCap] = {};
    uint8_t _head = 0, _count = 0;
    void    evict_before(uint64_t cutoff);
};

}  // namespace meshroute
