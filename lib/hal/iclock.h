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

#if defined(ARDUINO)
// Device clock. NB: millis() is 32-bit and wraps ~49 days; long-run wrap handling
// is deferred (H-track follow-up) — fine for bring-up / bench tests.
class ArduinoClock : public IClock {
public:
    uint64_t now_ms() override { return static_cast<uint64_t>(::millis()); }
};
#endif

}  // namespace meshroute
