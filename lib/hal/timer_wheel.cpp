// MeshRoute — lib/hal/timer_wheel.cpp  (H1)
#include "timer_wheel.h"

namespace meshroute {

bool TimerWheel::after(uint32_t delay_ms, uint32_t timer_id, uint64_t now_ms) {
    if (timer_id >= kCap) return false;              // out-of-range caller id (the Node owns 1..79; 64..79 = Slice-3 gateway band)
    _active[timer_id] = true;                         // re-arm-by-id: replaces any pending deadline for this id
    _due[timer_id]    = now_ms + delay_ms;
    return true;
}

void TimerWheel::cancel(uint32_t timer_id) {
    if (timer_id < kCap) _active[timer_id] = false;
}

int TimerWheel::pop_due(uint64_t now_ms) {
    int      best     = -1;
    uint64_t best_due = 0;
    for (uint32_t i = 0; i < kCap; ++i) {
        if (!_active[i] || _due[i] > now_ms) continue;
        if (best < 0 || _due[i] < best_due) { best = static_cast<int>(i); best_due = _due[i]; }
    }
    if (best >= 0) _active[best] = false;             // one-shot: fire once
    return best;
}

bool     TimerWheel::active(uint32_t timer_id) const { return timer_id < kCap && _active[timer_id]; }
uint64_t TimerWheel::due_at(uint32_t timer_id) const { return timer_id < kCap ? _due[timer_id] : 0; }

uint64_t TimerWheel::earliest_due() const {
    uint64_t m = UINT64_MAX;
    for (uint32_t i = 0; i < kCap; ++i) if (_active[i] && _due[i] < m) m = _due[i];
    return m;
}

}  // namespace meshroute
