# On-device OLED + one-button UI — Phase A (Heltec V3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give a Heltec V3 team mobile a usable no-phone interface — status, teammates, inbox, two canned messages, and a long-press emergency — driven by one button on the on-board SSD1306.

**Architecture:** Two **pure headers** in `src/` hold all the logic (gesture classification, screen/emergency state) and are unit-tested natively. `src/firmware_ui.cpp` builds a plain-data snapshot from the live node and drives the model. `src/board_ui.cpp` owns the panel and the GPIO. Sends go out as **console command strings through the existing `dispatch()` sink**, so no new command plumbing exists anywhere.

**Tech Stack:** C++20, PlatformIO, doctest (native), U8g2 (page-buffer mode), Arduino-ESP32 (`heltec_v3` / `heltec_mobile`).

**Spec:** `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`. Read §2, §4 and §5 before Task 4.

---

## Global Constraints

- **Never `git commit`.** Project rule D4: the owner makes every commit. Each task ends with "report ready", not a commit. This overrides the commit steps the writing-plans skill would normally emit.
- **The gate (D1), run before declaring any task done:** `pio test -e native`, then **run the binary** `./.pio/build/native/program` — the wrapper falsely reports "0 test cases"; the binary prints the real count and must show 0 failed. Then s18 md5 **exact** against the keystone in `simulation/BASELINE.md` (never hardcode the value — read it there). Then the board envs.
- **Board envs for this work:** `gateway`, `xiao_sx1262`, `xiao_esp32s3` (the standing 3-env rule) **plus** `heltec_v3` and `heltec_mobile`, which this changes.
- **s18 must not move.** Every file here lives in `src/` or is a new TU; `lib/core` is untouched, so the stream is inert by construction. If s18 moves, something was edited that should not have been.
- **Warnings are gate-blocking.** Zero `-Wswitch`, no new warnings versus the pio baseline.
- **Author header:** every new source file gets `// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>` as line 2.
- **`sizeof(Node)` must not change.** No UI state enters `lib/core`; the assert at `node.h:1976` is not touched by this plan.
- **Reuse, do not add.** The only new firmware surfaces this plan introduces are the two UI TUs, the display library dependency, and the V3 battery reader (owner-approved). Everything else routes through existing APIs. If a task appears to need a new config key, console verb, NV field or wire change — **stop and ask**; that is out of scope by owner instruction.

### External dependency

**Encrypted channel posts + location (`send_channel … -t -l -e`) land BEFORE this plan.** Owner statement, 2026-07-31; specified in `docs/superpowers/specs/2026-07-30-channel-crypt-and-location-privacy-design.md` (CL2 / T-K2). Today `send_channel` is documented "no ack/enc" (`lib/console/console_parse.cpp:197`), channel records are cleartext (`lib/core/inbox.h:100`), and `send_channel` has no `-l` (`lib/core/command.h:39`).

Because this plan sends through `dispatch()` with a **command string**, both flags need **no UI code change** beyond composing the right line. If either is unavailable when Task 7 runs, the send fails loud at the parser rather than going out in clear — the correct failure, and it must not be "fixed" by dropping a flag.

★★ **The one thing the UI must get right itself:** that spec's matrix (§2.2.1) **refuses `-t -l` with `no_location`** when `lat_e7 == 0 && lon_e7 == 0`. So `-l` is **conditional on a fix existing** — see Task 7. Sending it unconditionally converts "no fix" into **no alarm at all**, which is the single worst failure mode in this plan. It is covered by a bench case in Task 8.

### Constants fixed by the owner

- `MR_UI_TEAM_CHANNEL_ID` — build constant, **default 0**. No cfg key, no NV field, no console verb.
- Canned messages: emergency `"I'm in danger"`, plus `"Got your message"` and `"All good"`.

---

## File Structure

| file | responsibility |
|---|---|
| `src/firmware_ui_input.h` *(new, pure)* | debounce + classify button samples into gestures. No Arduino. |
| `src/firmware_ui_model.h` *(new, pure)* | screen cycle, cursors, emergency state machine, blanking. No Arduino, no `g_node`. Owns `UiSnapshot`, `UiState`, `SendReq`. |
| `src/firmware_ui.cpp` *(new)* | builds `UiSnapshot` from the live node; drives model; issues sends via `dispatch()`; calls render primitives. |
| `src/board_ui.cpp` *(modify — currently an empty seam)* | U8g2 panel, page-chunked paint, button GPIO sampling, V3 battery ADC. Implements the `mr_ui_*` hooks. |
| `test/test_firmware_ui_input.cpp` *(new)* | native tests for the classifier. |
| `test/test_firmware_ui_model.cpp` *(new)* | native tests for screens + emergency. |
| `platformio.ini` *(modify)* | U8g2 dependency, pinned exactly, on `heltec_v3` only. |

`lib/hal/mr_ui.h` is **not modified** — its three hooks are already the right seam.

### Task ↔ spec-slice map

The spec numbers its slices `UI-1…UI-7`; this plan splits some of them for TDD granularity. Use this when reviewing a task against the spec:

| plan task | spec slice |
|---|---|
| 1 | UI-1 |
| 2 | UI-2 |
| 3 | UI-6 (model half) |
| 4 | UI-3 |
| 5 | UI-4 |
| 6 | UI-3/UI-4 integration (the snapshot builder, implicit in the spec) |
| 7 | UI-5 |
| 8 | UI-6 (hardware half) |
| 9 | UI-7 |

---

### Task 1: Gesture classifier

**Files:**
- Create: `src/firmware_ui_input.h`
- Test: `test/test_firmware_ui_input.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `mrui::Gesture` (enum), `mrui::InputCfg`, `mrui::InputFsm` with `Gesture update(bool pressed, uint32_t now_ms)` and `uint32_t hold_ms(uint32_t now_ms) const`.

Design note the implementer must understand: a `short_press` cannot be emitted at release, because it might turn out to be the first half of a double. It is emitted only once the double window has expired, which is why `update()` must be called on every poll even when the button is idle.

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_firmware_ui_input.cpp
#include <doctest.h>
#include "firmware_ui_input.h"
using namespace mrui;

// Drive the FSM from t=0 to t=until_ms in 5 ms polls, returning the first non-none gesture.
static Gesture run_until(InputFsm& f, bool pressed, uint32_t from_ms, uint32_t until_ms) {
    Gesture got = Gesture::none;
    for (uint32_t t = from_ms; t <= until_ms; t += 5) {
        const Gesture g = f.update(pressed, t);
        if (g != Gesture::none && got == Gesture::none) got = g;
    }
    return got;
}

TEST_CASE("single tap yields short_press after the double window") {
    InputFsm f;
    CHECK(run_until(f, true,  0,   60) == Gesture::none);        // still held, too short for arm
    CHECK(run_until(f, false, 65,  200) == Gesture::none);       // released, double window still open
    CHECK(run_until(f, false, 205, 500) == Gesture::short_press);// window expired -> short
}

TEST_CASE("two taps inside the window yield double_press, not two shorts") {
    InputFsm f;
    run_until(f, true, 0, 60); run_until(f, false, 65, 120);
    run_until(f, true, 125, 180);
    CHECK(run_until(f, false, 185, 400) == Gesture::double_press);
}

TEST_CASE("hold past arm_ms yields long_arm, then long_fire past fire_ms") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900)  == Gesture::long_arm);
    CHECK(run_until(f, true, 905, 3600) == Gesture::long_fire);
}

TEST_CASE("release between arm and fire yields long_cancel and never fires") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900) == Gesture::long_arm);
    CHECK(run_until(f, false, 905, 1200) == Gesture::long_cancel);
    CHECK(run_until(f, false, 1205, 5000) != Gesture::long_fire);
}

TEST_CASE("bounce shorter than debounce_ms is ignored") {
    InputFsm f;
    f.update(true, 0); f.update(false, 10);       // 10 ms glitch < 25 ms debounce
    CHECK(run_until(f, false, 15, 600) == Gesture::none);
}

TEST_CASE("hold_ms reports progress for the countdown") {
    InputFsm f;
    run_until(f, true, 0, 1000);
    CHECK(f.hold_ms(1000) >= 950);
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `pio test -e native` then `./.pio/build/native/program`
Expected: FAIL — `firmware_ui_input.h` does not exist.

- [ ] **Step 3: Write the implementation**

```cpp
// MeshRoute — src/firmware_ui_input.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure button-gesture classifier for the one-button board UI. No Arduino, no globals — the board port samples the
// GPIO and feeds (pressed, now_ms); this decides what the press MEANT. Pure so the native suite can drive the timing
// table directly (see test_firmware_ui_input.cpp); reachable from tests via `-I src` (platformio.ini native env).
#pragma once
#include <cstdint>

namespace mrui {

enum class Gesture : uint8_t {
    none = 0,
    short_press,    // emitted AFTER the double window expires — a tap is only known to be single in hindsight
    double_press,
    long_arm,       // crossed arm_ms while held: the UI starts the emergency countdown
    long_fire,      // crossed fire_ms while held: commit
    long_cancel,    // released between arm_ms and fire_ms: abort
};

struct InputCfg {
    uint16_t debounce_ms   = 25;
    uint16_t double_gap_ms = 350;    // max release->press gap that still counts as a double
    uint16_t arm_ms        = 800;
    uint16_t fire_ms       = 3500;   // spec §14 Q2: bench-tunable
};

class InputFsm {
public:
    explicit InputFsm(InputCfg cfg = {}) : _cfg(cfg) {}

    // Call every poll, pressed or not — a pending single tap needs the idle ticks to time out its double window.
    Gesture update(bool pressed, uint32_t now_ms) {
        if (pressed != _raw) { _raw = pressed; _edge_ms = now_ms; }          // raw edge: start the debounce
        if (now_ms - _edge_ms >= _cfg.debounce_ms && _stable != _raw) {
            _stable = _raw;
            if (_stable) return on_press(now_ms);
            return on_release(now_ms);
        }
        if (_stable && !_armed && now_ms - _press_ms >= _cfg.arm_ms) {        // held past arm
            _armed = true; return Gesture::long_arm;
        }
        if (_stable && _armed && !_fired && now_ms - _press_ms >= _cfg.fire_ms) {
            _fired = true; return Gesture::long_fire;
        }
        if (!_stable && _pending_tap && now_ms - _release_ms >= _cfg.double_gap_ms) {
            _pending_tap = false; return Gesture::short_press;               // window closed -> it was single
        }
        return Gesture::none;
    }

    uint32_t hold_ms(uint32_t now_ms) const { return _stable ? now_ms - _press_ms : 0; }

private:
    Gesture on_press(uint32_t now_ms) {
        _press_ms = now_ms; _armed = false; _fired = false;
        return Gesture::none;                                                 // meaning is decided at release/hold
    }
    Gesture on_release(uint32_t now_ms) {
        _release_ms = now_ms;
        if (_fired)  { _pending_tap = false; return Gesture::none; }          // already committed
        if (_armed)  { _pending_tap = false; return Gesture::long_cancel; }
        if (_pending_tap) { _pending_tap = false; return Gesture::double_press; }
        _pending_tap = true; return Gesture::none;                            // maybe single, maybe first of a double
    }

    InputCfg _cfg;
    bool     _raw = false, _stable = false, _armed = false, _fired = false, _pending_tap = false;
    uint32_t _edge_ms = 0, _press_ms = 0, _release_ms = 0;
};

}  // namespace mrui
```

- [ ] **Step 4: Run the test and verify it passes**

Run: `pio test -e native` then `./.pio/build/native/program`
Expected: all six cases pass, 0 failed.

- [ ] **Step 5: Report ready — do NOT commit**

State the native pass count and stop. The owner commits (D4).

---

### Task 2: Screen model — cycle, cursors, blanking

**Files:**
- Create: `src/firmware_ui_model.h`
- Test: `test/test_firmware_ui_model.cpp`

**Interfaces:**
- Consumes: `mrui::Gesture` from Task 1.
- Produces: `mrui::Screen`, `mrui::TeamRow`, `mrui::UiSnapshot`, `mrui::UiState`, `mrui::UiModel` with `void on_gesture(Gesture, const UiSnapshot&)`, `void on_tick(const UiSnapshot&)`, `const UiState& state() const`. Task 3 extends the same class with the emergency machine; Task 6 fills `UiSnapshot`; Task 4 renders `UiState`.

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_firmware_ui_model.cpp
#include <doctest.h>
#include "firmware_ui_model.h"
using namespace mrui;

static UiSnapshot snap(uint32_t now_ms = 1000) {
    UiSnapshot s{};
    s.now_ms = now_ms; s.team_n = 3; s.unread_dm = 2; s.unread_ch = 5; s.batt_mv = 3900;
    for (uint8_t i = 0; i < 3; ++i) { s.team[i].id = uint8_t(10 + i); s.team[i].last_heard_s = 60; }
    return s;
}

TEST_CASE("short press cycles screens and wraps") {
    UiModel m; const auto s = snap();
    CHECK(m.state().screen == Screen::status);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::send_got);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::send_ok);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::status);
}

TEST_CASE("double press advances the TEAM cursor and wraps on team_n") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s);                 // -> team
    CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::double_press, s); CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::double_press, s); CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::double_press, s); CHECK(m.state().cursor == 0);
}

TEST_CASE("cursor resets when the screen changes") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s);
    m.on_gesture(Gesture::double_press, s); CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::short_press, s);  CHECK(m.state().cursor == 0);
}

TEST_CASE("panel blanks after the idle timeout and the waking press is consumed") {
    UiModel m;
    m.on_tick(snap(1000));
    m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    m.on_gesture(Gesture::short_press, snap(1000 + kBlankMs + 10));
    CHECK(m.state().blanked == false);
    CHECK(m.state().screen == Screen::status);            // consumed: it woke, it did not cycle
}

TEST_CASE("a screen change marks the state dirty and a repaint clears it") {
    UiModel m; const auto s = snap();
    m.clear_dirty();
    m.on_gesture(Gesture::short_press, s);
    CHECK(m.state().dirty == true);
    m.clear_dirty();
    CHECK(m.state().dirty == false);
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `pio test -e native` then `./.pio/build/native/program`
Expected: FAIL — `firmware_ui_model.h` does not exist.

- [ ] **Step 3: Write the implementation**

```cpp
// MeshRoute — src/firmware_ui_model.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure screen/state model for the one-button board UI. Consumes a gesture plus a plain-data snapshot of the node and
// produces what to draw. Deliberately knows nothing about g_node, Arduino or the display: that keeps it native-testable
// (test_firmware_ui_model.cpp) and keeps every hardware concern in board_ui.cpp. See
// docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md §2-§5.
#pragma once
#include <cstdint>
#include "firmware_ui_input.h"

namespace mrui {

inline constexpr uint32_t kBlankMs        = 15000;   // spec §5: panel blanks after this much input silence
inline constexpr uint8_t  kMaxTeamRows    = 8;       // a hiking group is 3-10; the UI shows the first 8

// Screen slots. team/send_* compile out of the CYCLE on a non-team build (see next_screen), but the enum keeps its
// values so a native test exercises both orderings without a second build.
enum class Screen : uint8_t { status = 0, team, inbox, send_got, send_ok, count };

struct TeamRow {
    uint8_t  id           = 0;    // team_local_id
    uint32_t last_heard_s = 0;
    int16_t  score_q4     = 0;
    uint8_t  hops         = 0;
};

struct UiSnapshot {
    uint32_t now_ms       = 0;
    uint16_t unread_dm    = 0, unread_ch = 0;
    uint32_t last_dm_age_s = UINT32_MAX, last_ch_age_s = UINT32_MAX;   // UINT32_MAX = unknown (no push since boot)
    uint8_t  team_n       = 0;
    TeamRow  team[kMaxTeamRows] = {};
    uint8_t  my_team_id   = 0;
    int32_t  batt_mv      = -1;   // <0 = unavailable; render "--", never a guess (console_json.h:126 rule)
    bool     ble_connected = false;
    bool     team_build    = true;  // MR_FEAT_TEAM; false shortens the cycle to status/inbox
};

struct UiState {
    Screen   screen  = Screen::status;
    uint8_t  cursor  = 0;
    bool     blanked = false;
    bool     dirty   = true;
};

class UiModel {
public:
    void on_gesture(Gesture g, const UiSnapshot& s) {
        if (g == Gesture::none) return;
        _last_input_ms = s.now_ms;
        if (_st.blanked) { _st.blanked = false; _st.dirty = true; return; }   // spec §5: the waking press is CONSUMED
        switch (g) {
            case Gesture::short_press:  _st.screen = next_screen(_st.screen, s); _st.cursor = 0; _st.dirty = true; break;
            case Gesture::double_press: advance_cursor(s); _st.dirty = true; break;
            default: break;                                                    // long_* belongs to the emergency machine (Task 3)
        }
    }

    void on_tick(const UiSnapshot& s) {
        if (!_st.blanked && s.now_ms - _last_input_ms >= kBlankMs) { _st.blanked = true; _st.dirty = true; }
    }

    const UiState& state() const { return _st; }
    void clear_dirty() { _st.dirty = false; }

protected:
    UiState  _st{};
    uint32_t _last_input_ms = 0;

private:
    static Screen next_screen(Screen cur, const UiSnapshot& s) {
        for (uint8_t i = 1; i <= uint8_t(Screen::count); ++i) {
            const Screen cand = Screen((uint8_t(cur) + i) % uint8_t(Screen::count));
            if (s.team_build || cand == Screen::status || cand == Screen::inbox) return cand;
        }
        return Screen::status;
    }
    void advance_cursor(const UiSnapshot& s) {
        const uint8_t n = (_st.screen == Screen::team) ? s.team_n : 0;
        _st.cursor = (n > 0) ? uint8_t((_st.cursor + 1) % n) : 0;
    }
};

}  // namespace mrui
```

Note for the implementer: `advance_cursor` returns 0 for INBOX in this task — the inbox cursor needs the real store and is wired in Task 6. Do not invent a placeholder count here.

- [ ] **Step 4: Run the test and verify it passes**

Run: `pio test -e native` then `./.pio/build/native/program`
Expected: all five cases pass, 0 failed. Task 1's cases still pass.

- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 3: Emergency state machine

**Files:**
- Modify: `src/firmware_ui_model.h`
- Modify: `test/test_firmware_ui_model.cpp`

**Interfaces:**
- Consumes: `UiModel` from Task 2, `Gesture` from Task 1.
- Produces: `mrui::Emergency`, `mrui::SendReq`, and on `UiModel`: `void on_send_outcome(bool blocked, uint32_t next_ms, bool relayed)`, `bool take_send_request(SendReq& out)`, `Emergency emergency() const`, `uint32_t emg_retry_at_ms() const`. Task 8 consumes `take_send_request` and calls `on_send_outcome`.

Read spec §4 before writing this. The retry bound is 3 and is not negotiable — unbounded retry burns the duty budget the rest of the team needs to answer.

- [ ] **Step 1: Write the failing test (append to the existing file)**

```cpp
TEST_CASE("arm then cancel never emits a send") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));
    CHECK(m.emergency() == Emergency::arming);
    m.on_gesture(Gesture::long_cancel, snap(2000));
    CHECK(m.emergency() == Emergency::cancelled);
    CHECK(m.take_send_request(req) == false);
}

TEST_CASE("arm then fire emits exactly one emergency send request") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));
    m.on_gesture(Gesture::long_fire, snap(4500));
    CHECK(m.emergency() == Emergency::firing);
    REQUIRE(m.take_send_request(req) == true);
    CHECK(req.kind == SendKind::emergency);
    CHECK(m.take_send_request(req) == false);          // consumed, not repeated
}

TEST_CASE("relayed outcome reports PICKED UP") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));
    m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req);
    m.on_send_outcome(/*blocked=*/false, /*next_ms=*/0, /*relayed=*/true);
    CHECK(m.emergency() == Emergency::picked_up);
}

TEST_CASE("no_relay retries three times then goes sticky NOT HEARD") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));
    m.on_gesture(Gesture::long_fire, snap(4500));
    CHECK(m.take_send_request(req) == true);           // attempt 1
    for (int attempt = 2; attempt <= 3; ++attempt) {
        m.on_send_outcome(false, 0, /*relayed=*/false);
        CHECK(m.emergency() == Emergency::firing);
        CHECK(m.take_send_request(req) == true);       // attempts 2 and 3
    }
    m.on_send_outcome(false, 0, false);
    CHECK(m.emergency() == Emergency::not_heard);
    CHECK(m.take_send_request(req) == false);          // bounded: no fourth attempt
}

TEST_CASE("blocked surfaces the retry deadline and re-requests when it passes") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));
    m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req);
    m.on_send_outcome(/*blocked=*/true, /*next_ms=*/10000, /*relayed=*/false);
    CHECK(m.emergency() == Emergency::blocked);
    CHECK(m.take_send_request(req) == false);          // must WAIT, not spin
    m.on_tick(snap(4500 + 10000 + 1));
    CHECK(m.take_send_request(req) == true);           // deadline passed -> retry
}

TEST_CASE("double press acknowledges a sticky NOT HEARD and re-fires") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));
    m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req);
    for (int i = 0; i < 3; ++i) { m.on_send_outcome(false, 0, false); m.take_send_request(req); }
    REQUIRE(m.emergency() == Emergency::not_heard);
    m.on_gesture(Gesture::double_press, snap(20000));
    CHECK(m.take_send_request(req) == true);           // user-driven retry
}

TEST_CASE("emergency holds the panel awake past the blank timeout") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000));
    m.on_gesture(Gesture::long_fire, snap(4500));
    m.on_tick(snap(4500 + kBlankMs + 1));
    CHECK(m.state().blanked == false);
    m.on_tick(snap(4500 + kEmgHoldMs + 1));
    CHECK(m.state().blanked == true);                  // capped hold, state retained
    CHECK(m.emergency() != Emergency::idle);
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `pio test -e native` then `./.pio/build/native/program`
Expected: FAIL — `Emergency`, `SendReq`, `emergency()` undefined.

- [ ] **Step 3: Write the implementation (extend `firmware_ui_model.h`)**

Add above `class UiModel`:

```cpp
inline constexpr uint32_t kEmgHoldMs   = 120000;  // spec §5: emergency holds the panel on, capped
inline constexpr uint8_t  kEmgMaxTries = 3;       // spec §4: BOUNDED. Unbounded retry burns the team's duty budget.

enum class Emergency : uint8_t { idle = 0, arming, firing, blocked, picked_up, not_heard, cancelled };
enum class SendKind  : uint8_t { emergency = 0, got_message, all_ok };
struct SendReq { SendKind kind = SendKind::emergency; };
```

Add to `UiModel`'s public section:

```cpp
    Emergency emergency()      const { return _emg; }
    uint32_t  emg_retry_at_ms() const { return _retry_at_ms; }

    // The model NEVER sends — it asks. Returns true once per attempt; the host (firmware_ui.cpp) performs the send
    // and reports back via on_send_outcome. Keeps this header free of dispatch()/Arduino and therefore native-testable.
    bool take_send_request(SendReq& out) {
        if (!_req_pending) return false;
        _req_pending = false; out = _req; return true;
    }

    void on_send_outcome(bool blocked, uint32_t next_ms, bool relayed) {
        if (_emg != Emergency::firing && _emg != Emergency::blocked) return;   // outcome for a canned send: ignore
        if (blocked) { _emg = Emergency::blocked; _retry_at_ms = _last_try_ms + next_ms; return; }
        if (relayed) { _emg = Emergency::picked_up; return; }
        if (_tries >= kEmgMaxTries) { _emg = Emergency::not_heard; return; }
        _emg = Emergency::firing; request(SendKind::emergency);                 // retry within the bound
    }
```

Extend `on_gesture`'s switch, before `default`:

```cpp
            case Gesture::long_arm:    _emg = Emergency::arming;   _st.dirty = true; break;
            case Gesture::long_cancel: _emg = Emergency::cancelled; _st.dirty = true; break;
            case Gesture::long_fire:
                _emg = Emergency::firing; _tries = 0; request(SendKind::emergency); _st.dirty = true; break;
```

and make `double_press` context-sensitive by replacing its case body with:

```cpp
            case Gesture::double_press:
                if (_emg == Emergency::not_heard || _emg == Emergency::picked_up) {
                    if (_emg == Emergency::not_heard) { _emg = Emergency::firing; _tries = 0; request(SendKind::emergency); }
                    else                              { _emg = Emergency::idle; }
                } else if (_st.screen == Screen::send_got) { request(SendKind::got_message); }
                else if (_st.screen == Screen::send_ok)    { request(SendKind::all_ok); }
                else                                       { advance_cursor(s); }
                _st.dirty = true; break;
```

Note the long_* cases must run **before** the `_st.blanked` early-return is applied to them — move the blank check so it guards only `short_press`/`double_press`; an emergency must work on a blanked panel. Rewrite the head of `on_gesture` as:

```cpp
        if (g == Gesture::none) return;
        _last_input_ms = s.now_ms;
        const bool is_long = (g == Gesture::long_arm || g == Gesture::long_fire || g == Gesture::long_cancel);
        if (_st.blanked && !is_long) { _st.blanked = false; _st.dirty = true; return; }
        if (_st.blanked) _st.blanked = false;   // an emergency wakes the panel AND acts
```

Extend `on_tick`:

```cpp
    void on_tick(const UiSnapshot& s) {
        if (_emg == Emergency::blocked && _retry_at_ms != 0 && s.now_ms >= _retry_at_ms) {
            _emg = Emergency::firing; _retry_at_ms = 0; request(SendKind::emergency);
        }
        const bool emg_live = (_emg == Emergency::firing || _emg == Emergency::blocked ||
                               _emg == Emergency::not_heard || _emg == Emergency::arming);
        const uint32_t idle_limit = emg_live ? kEmgHoldMs : kBlankMs;
        if (!_st.blanked && s.now_ms - _last_input_ms >= idle_limit) { _st.blanked = true; _st.dirty = true; }
    }
```

Add to the private section:

```cpp
    void request(SendKind k) {
        _req.kind = k; _req_pending = true;
        if (k == SendKind::emergency) { ++_tries; _last_try_ms = _last_input_ms; }
    }
    Emergency _emg = Emergency::idle;
    SendReq   _req{};
    bool      _req_pending = false;
    uint8_t   _tries = 0;
    uint32_t  _retry_at_ms = 0, _last_try_ms = 0;
```

- [ ] **Step 4: Run the test and verify it passes**

Run: `pio test -e native` then `./.pio/build/native/program`
Expected: all Task 1-3 cases pass, 0 failed.

- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 4: Panel port — U8g2, page-chunked paint, blanking

**Files:**
- Modify: `platformio.ini` (`[env:heltec_v3]` `lib_deps`)
- Modify: `src/board_ui.cpp`

**Interfaces:**
- Consumes: `UiState`, `UiSnapshot` (Tasks 2-3).
- Produces: `mrui_board_paint_begin()`, `bool mrui_board_paint_step()` (returns true while more pages remain), `mrui_board_blank()`, declared in `src/board_ui.h` *(new)* so `firmware_ui.cpp` can call them without knowing U8g2 exists.

**Read `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §5 before writing a line of this.** A full 1024-byte frame at 400 kHz I²C blocks for ~25 ms; `cts_to_data_gap_ms` is 5 and measured turnarounds are 5-8 ms, so a naive repaint breaks an in-flight RTS/CTS/DATA exchange. Page-chunking is the whole reason U8g2's `_1_` (page-buffer) constructor is specified — do not substitute a full-buffer driver.

- [ ] **Step 1: Add the dependency, pinned exactly**

In `platformio.ini`, `[env:heltec_v3]`, append to `lib_deps`:

```ini
  olikraus/U8g2 @ 2.35.30      ; PINNED EXACTLY (same rule as the RadioLib pin above): a caret lets different
                               ; checkouts resolve different versions and silently skews the board RAM/Flash baseline.
```

- [ ] **Step 2: Build to confirm the dependency resolves and links**

Run: `pio run -e heltec_v3`
Expected: SUCCESS. Record the flash/RAM figures — they are the pre-UI baseline for this env.

- [ ] **Step 3: Write the board header**

```cpp
// MeshRoute — src/board_ui.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The board-side render/input primitives the UI feature layer calls. Deliberately display-library-agnostic: nothing
// above this line knows U8g2 exists, and nothing below it knows what a "screen" is (spec §2, hard boundary).
#pragma once
#include <cstdint>
#include "firmware_ui_model.h"

namespace mrui {
void board_init();
void paint_begin(const UiState& st, const UiSnapshot& s);   // compose the frame; does NOT touch the bus
bool paint_step();                                          // push ONE page (~3 ms); true while pages remain
void blank();
bool button_pressed();                                      // debounced downstream by InputFsm
int32_t battery_mv();                                       // <0 = unavailable
}
```

- [ ] **Step 4: Implement the panel half of `board_ui.cpp`**

Replace the `TODO(board-ui)` bodies. Key points, all from spec §10.1: SSD1306 at 0x3C on SDA 17 / SCL 18, reset 21 (**confirm on hardware before trusting** — MeshCore's V3 variant defines no `PIN_OLED_RESET`).

```cpp
#include "mr_features.h"
#if MR_FEAT_OLED
#include <U8g2lib.h>
#include "board_ui.h"
#include "mr_ui.h"

static U8G2_SSD1306_128X64_NONAME_1_HW_I2C s_u8g2(U8G2_R0, /*reset=*/21, /*scl=*/18, /*sda=*/17);
static bool             s_painting = false;
static mrui::UiState    s_render_state{};   // snapshotted at paint_begin: the page loop runs across several ticks,
static mrui::UiSnapshot s_render_snap{};    // so it must NOT read live state that changes underneath it mid-frame.

// Draws the whole frame into whichever page U8g2 currently has selected. Called once per page; U8g2 clips.
static void draw_current_screen(const mrui::UiState& st, const mrui::UiSnapshot& s) {
    s_u8g2.setFont(u8g2_font_6x10_tf);
    char bar[32];
    snprintf(bar, sizeof bar, "DM%u CH%u T%u %s %s",
             unsigned(s.unread_dm), unsigned(s.unread_ch), unsigned(s.team_n),
             s.ble_connected ? "BLE*" : "ble",
             s.batt_mv < 0 ? "--" : "OK");          // batt_mv<0 => "--", never a guessed percentage
    s_u8g2.drawStr(0, 8, bar);
    s_u8g2.drawHLine(0, 10, 128);
    switch (st.screen) {
        case mrui::Screen::status:   draw_status(s);            break;
        case mrui::Screen::team:     draw_team(s, st.cursor);   break;
        case mrui::Screen::inbox:    draw_inbox(s, st.cursor);  break;
        case mrui::Screen::send_got: s_u8g2.drawStr(0, 30, "SEND:"); s_u8g2.drawStr(0, 44, "Got your message"); break;
        case mrui::Screen::send_ok:  s_u8g2.drawStr(0, 30, "SEND:"); s_u8g2.drawStr(0, 44, "All good");         break;
        default: break;
    }
}

namespace mrui {

void board_init() { s_u8g2.begin(); s_u8g2.setFont(u8g2_font_6x10_tf); }

void paint_begin(const UiState& st, const UiSnapshot& s) {
    s_render_state = st; s_render_snap = s;      // freeze what this frame draws
    s_u8g2.firstPage(); s_painting = true;
}

// ONE page per call (~3 ms of bus). The caller only invokes this while the MAC is idle (spec §5 rule 1), so the bus
// is never held across an RTS/CTS turnaround.
bool paint_step() {
    if (!s_painting) return false;
    draw_current_screen(s_render_state, s_render_snap);   // draws into the page buffer
    if (s_u8g2.nextPage()) return true;
    s_painting = false; return false;
}

void blank() { s_u8g2.clearDisplay(); s_painting = false; }

}  // namespace mrui
#endif
```

The three body helpers are file-statics with the same shape — each draws at most four 10 px lines starting at y=22, using `u8g2_font_6x10_tf`:

```cpp
static void draw_status(const mrui::UiSnapshot& s) {
    char l[32];
    snprintf(l, sizeof l, "me: team id %u", unsigned(s.my_team_id));            s_u8g2.drawStr(0, 22, l);
    if (s.last_dm_age_s == UINT32_MAX) snprintf(l, sizeof l, "DM %u  (age --)", unsigned(s.unread_dm));
    else                               snprintf(l, sizeof l, "DM %u  %lum ago", unsigned(s.unread_dm), (unsigned long)(s.last_dm_age_s / 60));
    s_u8g2.drawStr(0, 34, l);
    if (s.batt_mv < 0) snprintf(l, sizeof l, "batt --");
    else               snprintf(l, sizeof l, "batt %ld mV", (long)s.batt_mv);
    s_u8g2.drawStr(0, 46, l);
}

static void draw_team(const mrui::UiSnapshot& s, uint8_t cursor) {
    if (s.team_n == 0) { s_u8g2.drawStr(0, 22, "no teammates yet"); return; }
    const mrui::TeamRow& r = s.team[cursor % s.team_n];
    char l[32];
    snprintf(l, sizeof l, "%u/%u  id %u", unsigned(cursor + 1), unsigned(s.team_n), unsigned(r.id));
    s_u8g2.drawStr(0, 22, l);
    snprintf(l, sizeof l, "heard %lus ago", (unsigned long)r.last_heard_s);      s_u8g2.drawStr(0, 34, l);
    snprintf(l, sizeof l, "snr %d  hops %u", int(r.score_q4 / 16), unsigned(r.hops));
    s_u8g2.drawStr(0, 46, l);
}

static void draw_inbox(const mrui::UiSnapshot& s, uint8_t /*cursor*/) {
    char l[32];
    snprintf(l, sizeof l, "DM %u   CH %u", unsigned(s.unread_dm), unsigned(s.unread_ch));
    s_u8g2.drawStr(0, 22, l);
    s_u8g2.drawStr(0, 34, "use the app to read");    // Phase A: counts + ages only; message text is Task 6+ scope
}
```

`score_q4` is Q4 dB, hence the `/16` to reach whole dB (`node_carriers.h:267`). The emergency view (Task 8) is the one place that switches to `u8g2_font_10x20_tf`.

- [ ] **Step 5: Build and flash; verify the panel lights and shows a static frame**

Run: `pio run -e heltec_v3 -t upload`
Expected: the panel shows the status bar. If it stays dark, suspect the reset pin (spec §14 Q1) before suspecting the driver.

- [ ] **Step 6: Report ready — do NOT commit**

---

### Task 5: Button GPIO into the classifier

**Files:**
- Modify: `src/board_ui.cpp`
- Modify: `platformio.ini` (`[env:heltec_v3]` build flag)

**Interfaces:**
- Consumes: nothing new.
- Produces: `mrui::button_pressed()` (declared in Task 4's header).

- [ ] **Step 1: Add the pin as a build flag**

In `[env:heltec_v3]` `build_flags`:

```ini
  -DMR_UI_BTN_PIN=0             ; Heltec V3/V4 user button (MeshCore PIN_USER_BTN). Active LOW, needs INPUT_PULLUP.
                                ; NB GPIO0 is the ESP32-S3 boot strap: holding it across a reset enters download mode.
```

- [ ] **Step 2: Implement the sampler**

```cpp
void board_init() {
    pinMode(MR_UI_BTN_PIN, INPUT_PULLUP);      // active LOW
    s_u8g2.begin(); s_u8g2.setFont(u8g2_font_6x10_tf);
}
bool button_pressed() { return digitalRead(MR_UI_BTN_PIN) == LOW; }
```

- [ ] **Step 3: Build, flash, verify by trace**

Run: `pio run -e heltec_v3 -t upload`, then press the button while watching the serial console with `debug on`.
Expected: presses register. There is no UI reaction yet — Task 6 connects the model.

- [ ] **Step 4: Report ready — do NOT commit**

---

### Task 6: Snapshot builder and the tick loop

**Files:**
- Create: `src/firmware_ui.cpp`
- Modify: `src/board_ui.cpp` (delegate the `mr_ui_*` hooks into the feature layer)
- Modify: `platformio.ini` (add `+<firmware_ui.cpp>` to `[env:heltec_v3]` `build_src_filter`)

**Interfaces:**
- Consumes: `UiModel`, `UiSnapshot`, `board_*` primitives.
- Produces: the wired `mr_ui_init()` / `mr_ui_tick()` / `mr_ui_on_push()` behaviour.

Every field below comes from an **existing** accessor — spec §6 lists them. Add no new core API. If a field seems to need one, stop and ask.

- [ ] **Step 1: Write the snapshot builder**

```cpp
// MeshRoute — src/firmware_ui.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The board-UI FEATURE layer (U3: feature logic lives in a firmware_* module; board_ui.cpp stays board glue). Owns the
// model, builds its snapshot from the live node each tick, and issues sends through the EXISTING console sink. Adds no
// new core API — every read below is an accessor that already existed (spec §6).
#include "mr_features.h"
#if MR_FEAT_OLED
#include "firmware_ui_model.h"
#include "board_ui.h"
#include "fw_context.h"          // g_node, g_iradio, g_hal
#include "firmware_commands.h"   // dispatch()
#include "dispatch_sink.h"       // BufferSink
#include "device_ble.h"          // mrble::connected()

static mrui::UiModel   s_model;
static mrui::InputFsm  s_input;
static uint32_t        s_last_dm_ms = 0, s_last_ch_ms = 0;   // stamped in mr_ui_on_push (spec §6)

static uint16_t s_unread_dm = 0, s_unread_ch = 0;   // UI-LOCAL counters — see the note below this block

static mrui::UiSnapshot build_snapshot(uint32_t now_ms) {
    mrui::UiSnapshot s{};
    s.now_ms = now_ms;
    s.unread_dm = s_unread_dm;
    s.unread_ch = s_unread_ch;
    s.last_dm_age_s = s_last_dm_ms ? (now_ms - s_last_dm_ms) / 1000 : UINT32_MAX;
    s.last_ch_age_s = s_last_ch_ms ? (now_ms - s_last_ch_ms) / 1000 : UINT32_MAX;
#if MR_FEAT_TEAM
    s.team_build = true;
    s.my_team_id = g_node.team_local_id();
    const uint8_t n = g_node.rt_team_count();
    for (uint8_t i = 0; i < n && s.team_n < mrui::kMaxTeamRows; ++i) {
        const meshroute::RtEntry& e = g_node.rt_team_at(i);
        if (e.n == 0) continue;
        mrui::TeamRow& r = s.team[s.team_n++];
        r.id = e.dest; r.score_q4 = e.candidates[0].score; r.hops = e.candidates[0].hops;
        r.last_heard_s = uint32_t((g_hal.now() - e.candidates[0].last_seen_ms) / 1000);
    }
#else
    s.team_build = false;
#endif
    s.batt_mv = mrui::battery_mv();
    s.ble_connected = mrble::connected();
    return s;
}
#endif
```

**Why the unread counts are UI-local, verified 2026-07-31.** `Inbox` (`lib/core/inbox.h:111-116`) exposes `dm_newest_seq()`, `chan_newest_seq()` and `mark_read()` but **no read-cursor getter** — `read_cursor()` exists only on `InboxStore`, and `src/firmware_inbox.h:11` states the convention that verbs operate on `g_node.inbox()`, *not* the `g_inbox_dm` / `g_inbox_ch` stores directly. Computing `newest - cursor` would therefore mean either breaking that boundary or adding a new `lib/core` accessor — both excluded by the reuse constraint. Counting in `mr_ui_on_push` instead needs **zero new API** and matches the age-stamp decision the spec already made (§6).

Consequence, accepted: the counters are **session-scoped** — a reboot resets them to 0 while the durable inbox is untouched. For a glanceable status bar "since you last looked" is arguably the more useful meaning anyway. `g_node.inbox()` remains the authority for anything durable; do not add a wrapper around it.

- [ ] **Step 2: Write the tick, honouring the MAC-idle paint rule**

```cpp
static bool mac_idle() {   // spec §5 rule 1 — the SAME predicate fw_main.cpp:1274 uses to decide it may sleep
    return !g_iradio.tx_busy() && g_hal.txq_depth() == 0;
}

void mr_ui_tick(uint32_t now_ms) {
    static uint32_t s_last_paint_ms = 0;
    const mrui::UiSnapshot s = build_snapshot(now_ms);
    s_model.on_gesture(s_input.update(mrui::button_pressed(), now_ms), s);
    s_model.on_tick(s);

    mrui::SendReq req{};
    if (s_model.take_send_request(req)) ui_perform_send(req);   // Task 8

    if (!mac_idle()) return;                                    // never start or continue a paint mid-exchange
    if (mrui::paint_step()) return;                             // a frame is in flight: push one more page
    if (s_model.state().blanked) { mrui::blank(); s_model.clear_dirty(); return; }
    if (s_model.state().dirty && now_ms - s_last_paint_ms >= 500) {   // <=2 Hz, as mr_ui.h instructs
        mrui::paint_begin(s_model.state(), s); s_model.clear_dirty(); s_last_paint_ms = now_ms;
    }
}

void mr_ui_init() { mrui::board_init(); }

void mr_ui_on_push(const meshroute::Push& pu) {
    switch (pu.kind) {
        case meshroute::PushKind::msg_recv:
            s_last_dm_ms = uint32_t(g_hal.now()); if (s_unread_dm < 999) ++s_unread_dm; break;
        case meshroute::PushKind::channel_recv:
            s_last_ch_ms = uint32_t(g_hal.now()); if (s_unread_ch < 999) ++s_unread_ch; break;
        case meshroute::PushKind::channel_sent: s_model.on_send_outcome(false, 0, pu.relayed); break;
        case meshroute::PushKind::send_blocked: s_model.on_send_outcome(true, pu.next_ms, false); break;
        default: break;
    }
}
```

Field names verified against `lib/core/command.h`: `Push::relayed` at `:188`, `Push::next_ms` at `:203`. `g_hal.txq_depth()` is `lib/hal/device_hal.h:70`.

Clear the counter for a screen when the user views it — in `mr_ui_tick`, after `on_gesture`:

```cpp
    if (s_model.state().screen == mrui::Screen::inbox) { s_unread_dm = 0; s_unread_ch = 0; }
```

- [ ] **Step 3: Move the `mr_ui_*` bodies out of `board_ui.cpp`**

`board_ui.cpp` keeps only `board_init` / `paint_*` / `blank` / `button_pressed` / `battery_mv`. The three `mr_ui_*` hooks now live in `firmware_ui.cpp`. This is the §2 hard boundary; do not leave duplicate definitions or the link will fail.

- [ ] **Step 4: Build, flash, verify the cycle**

Run: `pio run -e heltec_v3 -t upload`
Expected: short presses cycle STATUS → TEAM → INBOX → SEND → SEND → STATUS; counts and battery render; the panel blanks after 15 s and the waking press does not change screen.

- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 7: Canned messages through the existing command sink

**Files:**
- Modify: `src/firmware_ui.cpp`
- Modify: `platformio.ini` (`[env:heltec_v3]` build flag)

**Interfaces:**
- Consumes: `SendReq`/`SendKind` (Task 3), `dispatch()` + `BufferSink` (existing).
- Produces: `ui_perform_send(const mrui::SendReq&)`.

This is the task that discharges the "reuse, do not add" constraint. The UI becomes a **fourth transport into the existing console sink**, exactly like BLE and rcmd already are (`src/dispatch_sink.h` header comment). No new command struct, no new verb, no new plumbing — and when `send_channel … -e` lands, the flag works here with no code change.

- [ ] **Step 1: Add the channel-id constant**

In `[env:heltec_v3]` `build_flags`:

```ini
  -DMR_UI_TEAM_CHANNEL_ID=0     ; owner ruling 2026-07-31: build constant, no cfg key / NV field / console verb
```

- [ ] **Step 2: Implement the send**

```cpp
static void ui_perform_send(const mrui::SendReq& req) {
    const bool emergency = (req.kind == mrui::SendKind::emergency);
    const char* body = emergency                                  ? "I'm in danger"    :
                       (req.kind == mrui::SendKind::got_message)  ? "Got your message" : "All good";

    // §4.1 — the DISTRESS call carries a position when one exists; the canned messages never do.
    // ★★ `-l` MUST be conditional. The channel-crypt spec (2026-07-30 §2.2.1) REFUSES `-t -l` with `no_location`
    // when lat_e7==0 && lon_e7==0. Sending `-l` unconditionally would therefore turn "no fix" into NO ALARM AT ALL,
    // which is the worst failure this feature can have. Check the fix, then choose the form.
    const meshroute::NodeConfig& cfg = g_node.config();
    const bool have_fix = emergency && (cfg.lat_e7 != 0 || cfg.lon_e7 != 0);

    char line[96];
    // -t = team plane, -e = encrypted, -l = attach position. `-e` and `-l` on send_channel are delivered by the
    // encrypted-channel + location slice, which lands BEFORE this work (owner, 2026-07-31). If either is absent the
    // parser rejects the line and the send fails LOUD — which is correct.
    // NEVER "fix" that by dropping -e: it would put "I'm in danger" on the air in clear.
    const int n = have_fix
        ? snprintf(line, sizeof line, "send_channel %u \"%s\" -t -l -e", unsigned(MR_UI_TEAM_CHANNEL_ID), body)
        : snprintf(line, sizeof line, "send_channel %u \"%s\" -t -e",    unsigned(MR_UI_TEAM_CHANNEL_ID), body);
    if (n <= 0 || size_t(n) >= sizeof line) return;
    BufferSink sink;                       // the response is not shown on the panel; outcome arrives via Push
    dispatch(line, size_t(n), sink);
}
```

- [ ] **Step 3: Build, flash, verify against a second node**

Run: `pio run -e heltec_v3 -t upload`
Expected: on SEND "Got your message", a double press posts the message; a second node in the same team receives it. Confirm on the sender's serial console that the line was accepted (no `parse error`).

- [ ] **Step 4: Report ready — do NOT commit**

---

### Task 8: Emergency end-to-end on hardware

**Files:**
- Modify: `src/firmware_ui.cpp` (render hooks only — the machine is already built and tested in Task 3)

**Interfaces:**
- Consumes: everything above. No new symbols.

- [ ] **Step 1: Render the emergency states**

In `draw_current_screen`, when `model.emergency() != Emergency::idle`, draw the emergency view instead of the current screen, using the large font:

- `arming` — `RELEASE TO CANCEL` and a countdown from `(fire_ms - hold_ms)/1000`
- `firing` — `SENDING...`
- `blocked` — `BLOCKED` and `retry in Ns` from `emg_retry_at_ms()`
- `picked_up` — **`PICKED UP`**. Never the word `DELIVERED`: `channel_sent{relayed}` means a neighbour re-flooded it, not that a human read it (spec §4).
- `not_heard` — `NOT HEARD` plus `hold=retry`
- `cancelled` — `CANCELLED` for ~1 s, then back to the cycle

- [ ] **Step 2: Bench-verify the full matrix (two nodes, same team)**

1. Long-press from each screen — reaches SENDING without reading the panel.
2. Release at ~3.0 s cancels; release past 3.5 s fires.
3. Second node powered **off** → `NOT HEARD` after exactly 3 attempts, then sticky.
4. Second node **on** → `PICKED UP`; reply from it appears.
5. Fire twice inside 10 s → second shows `BLOCKED` with a live countdown, then auto-fires (`channel_min_interval_ms` is 10000).
6. Emergency on a blanked panel works and wakes it.
7. ★★ **The conditional-`-l` pair, and do not skip either half:**
   - `cfg set lat 52.2297` + `cfg set lon 21.0122`, then fire → the receiving node's record carries the position.
   - `cfg set lat 0` + `cfg set lon 0` (no fix), then fire → **the alarm still goes out**, without a position and without a `no_location` refusal. If this case fails, the conditional in Task 7 is inverted or missing, and the feature is broken in exactly the situation it exists for.
8. Canned messages ("Got your message", "All good") carry **no** position in either state — location rides the distress call only.

- [ ] **Step 3: The paint-vs-radio check — the one most likely to fail**

Run a sustained DM load between the two nodes while cycling screens continuously for several minutes. Compare CTS-timeout and retry counts against the same load with the UI idle.
Expected: no regression. If timeouts rise, the MAC-idle gate or the page chunking is wrong — fix that, do not lengthen the timeouts.

- [ ] **Step 4: Report ready — do NOT commit**

---

### Task 9: V3 battery reader

**Files:**
- Modify: `src/board_ui.cpp`
- Modify: `platformio.ini` (`[env:heltec_v3]` build flags)

**Interfaces:**
- Produces: `mrui::battery_mv()` — millivolts, `<0` = unavailable.

Spec §7 is the authority. **The V3 polarity is auto-detected, not fixed** — boards past rev 3.2 inverted it, which is why `HeltecV3Board::begin()` probes the idle level. Do not hardcode LOW. V3 has **no settling delay**; do not import V4's `delay(10)`.

- [ ] **Step 1: Add the pins**

```ini
  -DMR_UI_ADC_CTRL=37           ; Heltec V3/V4 battery divider enable — POLARITY AUTO-DETECTED at boot on V3
  -DMR_UI_VBAT_READ=1
```

- [ ] **Step 2: Implement**

```cpp
static bool s_adc_active_high = false;

static void battery_init() {                 // call from board_init()
    pinMode(MR_UI_ADC_CTRL, INPUT);
    s_adc_active_high = (digitalRead(MR_UI_ADC_CTRL) == LOW);   // probe idle, invert: MeshCore HeltecV3Board::begin()
    pinMode(MR_UI_ADC_CTRL, OUTPUT);
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);   // park inactive
}

int32_t battery_mv() {
    analogReadResolution(10);
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? HIGH : LOW);
    uint32_t raw = 0;
    for (int i = 0; i < 8; ++i) raw += analogRead(MR_UI_VBAT_READ);
    raw /= 8;
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);
    return int32_t(5.42f * (3.3f / 1024.0f) * float(raw) * 1000.0f);
}
```

- [ ] **Step 3: Verify against a multimeter**

Measure the cell directly and compare with the status bar. Expected: within ~50 mV. If it is consistently off by a ratio, the divider constant differs on this board revision — record the measured value; do not tune it to taste.

- [ ] **Step 4: Report ready — do NOT commit**

---

## Final gate before handing back

- [ ] `pio test -e native` then **run** `./.pio/build/native/program` — real count printed, 0 failed
- [ ] s18 md5 **exact** vs the current `simulation/BASELINE.md` keystone (read it there; never assume)
- [ ] The mandatory mobile/team scenarios per `BASELINE.md` §2 — 0 assertion failures
- [ ] `pio run` for `gateway`, `xiao_sx1262`, `xiao_esp32s3`, `heltec_v3`, `heltec_mobile` — all green, no new warnings
- [ ] Flash/RAM delta recorded for `heltec_v3` and `heltec_mobile` against the Task 4 Step 2 baseline
- [ ] Report ready with the numbers. **The owner commits.**

## Open items this plan does not decide

- **Arm duration 3.5 s** (spec §14 Q2) — a bench opinion, not a code review. Tune the `InputCfg::fire_ms` default after Task 8.
- **V3 panel reset pin** (spec §14 Q1) — 21 per `board_ui.cpp:14`, but MeshCore's V3 variant defines none. Confirm during Task 4 Step 5.
- ~~Location in the emergency message~~ — **RESOLVED 2026-07-31: include it when a fix exists** (spec §4.1). Implemented as the conditional `-l` in Task 7 and bench-checked in Task 8 case 7. Phase A's coordinate is hand-typed and may be stale; the owner weighed that and ruled to send it anyway, and Phase B's live fix retires the concern.
