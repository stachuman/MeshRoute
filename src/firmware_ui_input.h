// MeshRoute — src/firmware_ui_input.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure button-gesture classifier for the one-button board UI (UI-1). No Arduino, no globals — the board samples the
// GPIO and feeds (pressed, now_ms); this decides what the press MEANT. Pure so the native suite can drive the timing
// table directly; reachable from tests via `-I src` (platformio.ini:83, native env, test_build_src=no).
//
// DONE here: debounce, short vs double, long arm/fire/cancel, wrap-safe timing, per-instance InputCfg.
// NOT here (deliberate, by unit boundary — [[meshroute-mark-done-vs-missing-in-code]]):
//   - reading the GPIO and its active level: variants/heltec_common/board_ui.cpp (UI-5/UI-6)
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

    // ★★★ §B197/§B198 — "a gesture is still being CLASSIFIED", read-only. The device sleep gate needs this: an
    //   ESP32 light-sleep pass can be up to MR_MAX_SLEEP_MS (1000 ms) long, which is longer than debounce_ms (25),
    //   double_gap_ms (350) and arm_ms (800), so a node that slept between the edge and the decision would classify
    //   a real gesture from samples a second apart — or drop it. ⇒ while this is true the CPU must stay awake.
    // ★ THE THREE TERMS ARE THE THREE UNDECIDED STATES, and each covers a window the others do not:
    //     `_raw`         a level change has been SEEN but not yet debounced (press debounce AND release debounce —
    //                    `_raw` is the raw level, so it is true through a press and false through a release; the
    //                    release window is covered by `_stable`, which is still true until the debounce completes);
    //     `_stable`      a debounced press is being HELD — the arm/fire clock is running;
    //     `_pending_tap` released, and the single-vs-double decision has not expired yet (`double_gap_ms`).
    // ⛔ `_armed` / `_fired` NEED NO TERM, and adding one would be wrong rather than merely redundant: WHILE HELD they
    //   are strictly implied by `_stable` (both are only ever set with `_stable` true, and `on_release` is the only
    //   other writer), and AFTER the release they are HISTORICAL — `_fired` in particular stays true until the next
    //   debounced press, so a term on it would hold the CPU awake for ever after one emergency fire.
    bool active() const { return _raw || _stable || _pending_tap; }

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
