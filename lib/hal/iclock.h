// MeshRoute — lib/hal/iclock.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The testability seam for the DEVICE Hal backend. The device Hal's now() reads
// an IClock; on hardware that is ArduinoClock(millis()), in a native unit test
// it is FakeClock(settable now) — so the timer wheel / airtime ledger / Hal
// facade run deterministically on the host without real millis() or RadioLib.
// (The SIM backend does NOT use IClock — FirmwareNode drives now() from the
// simulator's VirtualClock; IClock is device/native-test only.)
#pragma once
#include <cstdint>
#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace meshroute {

struct IClock {
    virtual ~IClock() = default;
    virtual uint64_t now_ms() = 0;
};

// Native / host-test clock with a directly settable now.
class FakeClock : public IClock {
public:
    uint64_t now = 0;
    uint64_t now_ms() override { return now; }
};

// Accumulate a 32-bit millis() into a MONOTONIC 64-bit epoch, handling the ~49-day wrap (H-track fix).
// `last` = previous raw millis, `high` = accumulated wraps (multiples of 2^32). Pure + native-testable.
// Must be called often enough to catch each wrap (the device loop runs every few ms — trivially satisfied).
// Without this, at wrap now() jumps back to 0: the airtime ledger evicts everything (duty resets) and every
// armed after() deadline (now+delay) becomes far-future -> the MAC freezes for ~49 days on an always-on node.
inline uint64_t accumulate_millis_wrap(uint32_t m, uint32_t& last, uint64_t& high) {
    if (m < last) high += (static_cast<uint64_t>(1) << 32);   // millis() wrapped past 2^32
    last = m;
    return high + m;
}

#if defined(ARDUINO)
// Device clock — millis() over the 64-bit wrap-accumulating epoch (the sim's VirtualClock is already true 64-bit,
// so this brings device == sim time semantics).
class ArduinoClock : public IClock {
public:
    uint64_t now_ms() override { return accumulate_millis_wrap(static_cast<uint32_t>(::millis()), _last, _high); }
private:
    uint32_t _last = 0;
    uint64_t _high = 0;
};
#endif

}  // namespace meshroute
