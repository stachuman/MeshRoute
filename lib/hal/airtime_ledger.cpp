// MeshRoute — lib/hal/airtime_ledger.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "airtime_ledger.h"

namespace meshroute {

void AirtimeLedger::record(uint64_t end_ms, uint32_t airtime_ms) {
    if (_count >= kCap) {                                    // full -> COALESCE the two oldest (H5, regulatory)
        // Dropping the oldest record would DISCARD its airtime even while it is still inside the 1-hour duty
        // window -> used_in_window() UNDER-reports -> the node transmits PAST the legal EU868 1% duty. Instead
        // merge the two oldest into one: sum the airtime (nothing lost) and keep the NEWER end_ms so the merged
        // record ages out no earlier than its youngest member (conservative — it may linger a hair longer than
        // the exact sim's unbounded deque, which OVER-reports slightly = the SAFE direction). O(1), RAM-frugal.
        const uint8_t o0 = _head;                            // oldest
        const uint8_t o1 = static_cast<uint8_t>((_head + 1) % kCap);   // second-oldest
        const uint64_t newer_end = (_ring[o1].end_ms > _ring[o0].end_ms) ? _ring[o1].end_ms : _ring[o0].end_ms;
        _ring[o1] = Rec{ newer_end, _ring[o0].airtime_ms + _ring[o1].airtime_ms };   // merged record replaces the second-oldest
        _head = o1;                                           // drop the now-vacated oldest slot
        --_count;
    }
    const uint8_t tail = static_cast<uint8_t>((_head + _count) % kCap);
    _ring[tail] = Rec{ end_ms, airtime_ms };
    ++_count;
}

void AirtimeLedger::evict_before(uint64_t cutoff) {
    while (_count > 0 && _ring[_head].end_ms <= cutoff) {
        _head = static_cast<uint8_t>((_head + 1) % kCap);
        --_count;
    }
}

uint64_t AirtimeLedger::used_in_window(uint64_t now_ms, uint64_t window_ms) {
    if (window_ms == 0) return 0;
    const uint64_t cutoff = (now_ms > window_ms) ? (now_ms - window_ms) : 0;
    evict_before(cutoff);                                   // matches FirmwareNode::airtimeUsedInWindow
    uint64_t sum = 0;
    for (uint8_t i = 0; i < _count; ++i)
        sum += _ring[(_head + i) % kCap].airtime_ms;
    return sum;
}

uint64_t AirtimeLedger::oldest_tx_end_ms() const {
    return _count ? _ring[_head].end_ms : 0;
}

}  // namespace meshroute
