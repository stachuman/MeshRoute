// MeshRoute — lib/hal/airtime_ledger.cpp
#include "airtime_ledger.h"

namespace meshroute {

void AirtimeLedger::record(uint64_t end_ms, uint32_t airtime_ms) {
    if (_count >= kCap) {                                    // full -> drop oldest
        _head = static_cast<uint8_t>((_head + 1) % kCap);
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
