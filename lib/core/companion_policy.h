// MeshRoute — lib/core/companion_policy.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The BLE companion advertising policy (spec §A.1): decides WHEN BLE advertising should be active, given the
// persisted `ble_mode` (off/on/periodic) + the periodic period. Pure logic — no device I/O, no SoftDevice
// symbols — so it is fully native-testable with a fake clock. The device backend (Step 5) calls on_tick(now)
// each loop and starts/stops Bluefruit advertising per `should_advertise`; `request_window()` (press-to-
// advertise / a connected client extending) opens an immediate window. We window the ADVERTISING, never the
// stack itself (the SoftDevice stays enabled; only advertising toggles) — spec §A.1.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include <cstdint>

namespace MESHROUTE_NS {

enum class BleMode : uint8_t { off = 0, on = 1, periodic = 2 };   // == the NV `ble_mode` values

class CompanionPolicy {
public:
    struct Tick {
        bool     should_advertise;   // advertise this instant?
        uint64_t next_change_ms;     // when the answer next changes (a sleep/re-evaluate hint); UINT64_MAX = never
    };

    // From cfg at boot: `mode` + the periodic `period_min`; `window_ms` = how long each advertising window lasts.
    // Resets the schedule. (period_min/window_ms are validated/fixed by the caller — see the no-fallback rule;
    // this is pure logic, it does not substitute defaults for bad input.)
    void configure(BleMode mode, uint16_t period_min, uint32_t window_ms) {
        _mode = mode;
        _period_ms = static_cast<uint64_t>(period_min) * 60000ull;
        _window_ms = window_ms;
        _win_until = 0; _next_win = 0; _started = false;
    }

    // Open an advertising window NOW, held for window_ms (press-to-advertise / extend a live session). Only
    // meaningful in `periodic` — `on` already advertises always, `off` never brings the radio up.
    void request_window(uint64_t now_ms) { _win_until = now_ms + _window_ms; }

    Tick on_tick(uint64_t now_ms) {
        if (_mode == BleMode::off) return { false, UINT64_MAX };   // never advertise (the SD is never even enabled)
        if (_mode == BleMode::on)  return { true,  UINT64_MAX };   // always advertise
        // periodic — the only remaining valid mode. A window at boot (so a phone can connect immediately on
        // first power-up), then one window every period.
        if (!_started) { _started = true; _win_until = now_ms + _window_ms; _next_win = now_ms + _period_ms; }
        if (now_ms >= _next_win && now_ms >= _win_until) {         // a scheduled window is due AND we're not mid-window
            _win_until = now_ms + _window_ms;
            _next_win  = now_ms + _period_ms;
        }
        if (now_ms < _win_until) return { true,  _win_until };     // inside a window -> advertise until it closes
        return { false, _next_win };                              // idle -> next advertise at the next scheduled window
    }

    BleMode mode() const { return _mode; }

private:
    BleMode  _mode = BleMode::off;
    uint64_t _period_ms = 0;     // periodic period (ms)
    uint64_t _window_ms = 0;     // advertising-window duration (ms)
    uint64_t _win_until = 0;     // advertising until this ms (0 = not in a window)
    uint64_t _next_win  = 0;     // next scheduled window opens at this ms
    bool     _started   = false; // has the boot window been seeded?
};

}  // namespace meshroute
