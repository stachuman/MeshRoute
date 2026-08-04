// MeshRoute — src/firmware_ui_input.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure button-gesture classifier for the one-button board UI (UI-1). No Arduino, no globals — the board samples the
// GPIO and feeds (pressed, now_ms); this decides what the press MEANT. Pure so the native suite can drive the timing
// table directly; reachable from tests via `-I src` (platformio.ini:83, native env, test_build_src=no).
//
// DONE here: debounce, short vs double, long arm/fire/cancel, wrap-safe timing, per-instance InputCfg.
// NOT here (deliberate, by unit boundary — [[meshroute-mark-done-vs-missing-in-code]]):
//   - reading the GPIO and its active level: variants/heltec_v3/board_ui.cpp (UI-5/UI-6)
//   - what a gesture MEANS on screen: firmware_ui_model.h (UI-2/UI-3)
// Spec: docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md §3.2, §4.
#pragma once
#include <cstdint>

namespace mrui {

enum class Gesture : uint8_t { none = 0, short_press, double_press, long_arm, long_fire, long_cancel };

struct InputCfg {
    uint16_t debounce_ms = 25, double_gap_ms = 350, arm_ms = 800, fire_ms = 3500;   // fire_ms: bench-tunable
};

// A short_press is emitted only after the double window expires — a tap is single only in hindsight. So update()
// must be called on EVERY poll pass, pressed or not, or a tap is never reported. All time comparisons are unsigned
// differences, so they hold across the ~49.7-day millis() wrap.
class InputFsm {
public:
    explicit InputFsm(InputCfg cfg = {}) : _cfg(cfg) {}

    Gesture update(bool pressed, uint32_t now_ms) {
        if (pressed != _raw) { _raw = pressed; _edge_ms = now_ms; }
        if (now_ms - _edge_ms >= _cfg.debounce_ms && _stable != _raw) {
            _stable = _raw;
            if (_stable) { _press_ms = now_ms; _armed = _fired = false; return Gesture::none; }
            return on_release();
        }
        if (_stable && !_armed && now_ms - _press_ms >= _cfg.arm_ms)             { _armed = true; return Gesture::long_arm; }
        if (_stable && _armed && !_fired && now_ms - _press_ms >= _cfg.fire_ms)  { _fired = true; return Gesture::long_fire; }
        if (!_stable && _pending_tap && now_ms - _release_ms >= _cfg.double_gap_ms) {
            _pending_tap = false; return Gesture::short_press;
        }
        return Gesture::none;
    }

    uint32_t hold_ms(uint32_t now_ms) const { return _stable ? now_ms - _press_ms : 0; }

private:
    Gesture on_release() {
        _release_ms = _edge_ms;
        if (_fired)       { _pending_tap = false; return Gesture::none; }
        if (_armed)       { _pending_tap = false; return Gesture::long_cancel; }
        if (_pending_tap) { _pending_tap = false; return Gesture::double_press; }
        _pending_tap = true; return Gesture::none;
    }
    InputCfg _cfg;
    bool     _raw = false, _stable = false, _armed = false, _fired = false, _pending_tap = false;
    uint32_t _edge_ms = 0, _press_ms = 0, _release_ms = 0;
};

}  // namespace mrui
