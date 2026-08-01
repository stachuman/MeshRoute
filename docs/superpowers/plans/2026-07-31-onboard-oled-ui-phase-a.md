# On-device OLED + one-button UI — Phase A (Heltec V3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give a Heltec V3 team mobile a usable no-phone interface — status, teammates, inbox, canned messages, teammate DMs, and a long-press emergency — driven by one button on the on-board SSD1306.

**Architecture:** Two **pure headers** in `src/` hold all logic (gesture classification; screens, compose modal, emergency and DM outcome machines) and are unit-tested natively. `src/firmware_ui.cpp` builds a plain-data snapshot, owns all render policy, performs sends and **correlates their outcomes**. `src/board_ui.cpp` owns only U8g2, I²C, GPIO and the ADC, behind a display-independent canvas.

**Tech Stack:** C++20, PlatformIO, doctest (native), U8g2 (page-buffer mode), Arduino-ESP32 (`heltec_v3` / `heltec_mobile`).

**Spec:** `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`. Read §2, §2.1, §4 and §5 before Task 4.

*Revision 2026-08-01: rewritten against the review findings (now archived at `docs/archive/2026-08-01-onboard-oled-ui-review.md`). The material changes are the new send-attribution task, the corrected retry arithmetic, long-gesture pre-emption, the DM outcome machine, the canvas boundary, edge-triggered blanking and cached battery sampling.*

---

## Global Constraints

- **Never `git commit`.** Project rule D4: the owner makes every commit. Each task ends with "report ready", not a commit.
- **The gate (D1):** `pio test -e native`, then **run** `./.pio/build/native/program` — the wrapper falsely reports "0 test cases"; the binary prints the real count and must show 0 failed. Then s18 md5 **exact** against the keystone in `simulation/BASELINE.md` (read it there; never hardcode). Then the board envs.
- **Board envs:** `gateway`, `xiao_sx1262`, `xiao_esp32s3` (the standing 3-env rule) **plus** `heltec_v3` and `heltec_mobile`, which this changes.
- **s18 must not move.** Everything here is `src/`-only, so the stream is inert by construction.
- **Warnings are gate-blocking.** Zero `-Wswitch`, no new warnings vs the pio baseline.
- **Author header:** every new source file gets `// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>` as line 2.
- **`sizeof(Node)` must not change.** No UI state enters `lib/core`.
- **Reuse, do not add.** The only new firmware surfaces are the UI TUs, the U8g2 dependency, and the V3 battery reader. If a task appears to need a new config key, console verb, NV field or wire change — **stop and ask**.
- **`dispatch` is `mrfw::dispatch`** (`src/firmware_commands.h:23,41`). Qualify every call.
- **Bench-only behaviour goes in `docs/2026-07-31-bench-test-script.md`** (rule M2) — the panel, the button and the ADC are unreachable by native tests and the sim, so their checks belong there with exact expected console lines.

### Prerequisites — discharged, verified 2026-08-01

`send_channel … -t -l -e` is **built and honoured**: the parser accepts `-e`/`-l` (`lib/console/console_parse.cpp:250,267`), and `Node::on_command` enforces the refusal matrix — `loc_unsealed`, `no_team`, `global_clear_copy`, `no_key`, `no_identity`, **`no_fix`** (`lib/core/node.cpp:1402-1526`). `team_channel_crypt` defaults **true** (`lib/core/node_carriers.h:184`). Nothing in Phase A waits on protocol work.

### Constants fixed by the owner

- `MR_UI_TEAM_CHANNEL_ID` — build constant, **default 0**.
- Emergency text `"I'm in danger"`; channel canned `"Got your message"` / `"All good"`; DM canned `"Are you OK?"` / `"I'm OK"`. Every compose list ends with `back, don't send`.

---

## File Structure

| file | responsibility |
|---|---|
| `src/firmware_ui_input.h` *(new, pure)* | debounce + gesture classification |
| `src/firmware_ui_model.h` *(new, pure)* | screens, list-aware cursor, compose modal, emergency machine, DM outcome machine. Owns `UiSnapshot`, `UiState`, `SendReq`, `SendOutcome` |
| `src/firmware_ui_send.h` *(new)* | the send tracker: typed result, `ctr`/peer/channel correlation, outcome window |
| `src/firmware_ui.cpp` *(new)* | snapshot building, **all render policy**, send execution, push correlation, battery cache, the three `mr_ui_*` hooks |
| `src/board_ui.cpp` *(modify — currently an empty seam)* | U8g2, I²C, button GPIO, battery ADC, panel power latch. **Nothing else.** |
| `src/board_ui.h` *(new)* | the display-independent canvas. **Must not include `firmware_ui_model.h`.** |
| `test/test_firmware_ui_input.cpp` *(new)* | classifier tests |
| `test/test_firmware_ui_model.cpp` *(new)* | screens, compose, emergency, DM outcome tests |
| `test/test_firmware_ui_send.cpp` *(new)* | attribution/correlation tests |
| `platformio.ini` *(modify)* | U8g2 pinned; pins and constants for `heltec_v3` |

`lib/hal/mr_ui.h` is **not modified**; the three hooks are implemented in `firmware_ui.cpp`, not `board_ui.cpp`.

### Task ↔ spec-slice map

| plan task | spec slice |
|---|---|
| 1 | UI-1 |
| 2 | UI-2 |
| 3 | UI-3 |
| 4 | UI-4 |
| 5 | UI-5 |
| 6 | UI-6 |
| 7 | UI-7 |
| 8 | UI-8 |
| 9 | UI-9 |

---

### Task 1: Gesture classifier

**Files:** Create `src/firmware_ui_input.h`; test `test/test_firmware_ui_input.cpp`.

**Interfaces:**
- Consumes: nothing.
- Produces: `mrui::Gesture`, `mrui::InputCfg`, `mrui::InputFsm` with `Gesture update(bool pressed, uint32_t now_ms)` and `uint32_t hold_ms(uint32_t now_ms) const`.

A `short_press` is emitted only after the double window expires — a tap is single only in hindsight. So `update()` must be called every poll, pressed or not.

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_firmware_ui_input.cpp
#include <doctest.h>
#include "firmware_ui_input.h"
using namespace mrui;

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
    CHECK(run_until(f, true,  0,   60)  == Gesture::none);
    CHECK(run_until(f, false, 65,  200) == Gesture::none);
    CHECK(run_until(f, false, 205, 500) == Gesture::short_press);
}
TEST_CASE("two taps inside the window yield double_press") {
    InputFsm f;
    run_until(f, true, 0, 60); run_until(f, false, 65, 120); run_until(f, true, 125, 180);
    CHECK(run_until(f, false, 185, 400) == Gesture::double_press);
}
TEST_CASE("hold yields long_arm then long_fire") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900)    == Gesture::long_arm);
    CHECK(run_until(f, true, 905, 3600) == Gesture::long_fire);
}
TEST_CASE("release between arm and fire cancels and never fires") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900)      == Gesture::long_arm);
    CHECK(run_until(f, false, 905, 1200)  == Gesture::long_cancel);
    CHECK(run_until(f, false, 1205, 5000) != Gesture::long_fire);
}
TEST_CASE("bounce shorter than debounce_ms is ignored") {
    InputFsm f; f.update(true, 0); f.update(false, 10);
    CHECK(run_until(f, false, 15, 600) == Gesture::none);
}
TEST_CASE("hold_ms reports countdown progress") {
    InputFsm f; run_until(f, true, 0, 1000);
    CHECK(f.hold_ms(1000) >= 950);
}
```

- [ ] **Step 2: Run and verify it fails**

Run: `pio test -e native` then `./.pio/build/native/program` — FAIL, header missing.

- [ ] **Step 3: Write the implementation**

```cpp
// MeshRoute — src/firmware_ui_input.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure button-gesture classifier for the one-button board UI. No Arduino, no globals — the board samples the GPIO and
// feeds (pressed, now_ms); this decides what the press MEANT. Pure so the native suite can drive the timing table
// directly; reachable from tests via `-I src` (platformio.ini native env).
#pragma once
#include <cstdint>

namespace mrui {

enum class Gesture : uint8_t { none = 0, short_press, double_press, long_arm, long_fire, long_cancel };

struct InputCfg {
    uint16_t debounce_ms = 25, double_gap_ms = 350, arm_ms = 800, fire_ms = 3500;   // fire_ms: bench-tunable
};

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
```

- [ ] **Step 4: Run and verify all six cases pass**
- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 2: Screens, list-aware cursor, compose modal

**Files:** Create `src/firmware_ui_model.h`; test `test/test_firmware_ui_model.cpp`.

**Interfaces:**
- Consumes: `mrui::Gesture`.
- Produces: `Screen`, `Compose`, `TeamRow`, `InboxRow`, `UiSnapshot`, `UiState`, `SendKind`, `SendReq`, `UiModel` with `on_gesture`, `on_tick`, `state()`, `clear_dirty()`, `take_send_request()`. Task 3 extends the same class; Task 4 feeds it typed outcomes; Task 6 fills `UiSnapshot`.

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_firmware_ui_model.cpp
#include <doctest.h>
#include "firmware_ui_model.h"
using namespace mrui;

static UiSnapshot snap(uint32_t now_ms = 1000) {
    UiSnapshot s{};
    s.now_ms = now_ms; s.team_shown = 3; s.team_total = 3; s.unread_dm = 2; s.unread_ch = 5; s.batt_mv = 3900;
    for (uint8_t i = 0; i < 3; ++i) { s.team[i].id = uint8_t(10 + i); s.team[i].last_heard_s = 60; }
    return s;
}

TEST_CASE("short press is LIST-AWARE: it walks TEAM before leaving it") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::send);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::status);
}
TEST_CASE("an empty TEAM list is passed through, not a dead end") {
    UiModel m; auto s = snap(); s.team_shown = 0; s.team_total = 0;
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
}
TEST_CASE("double on TEAM opens the DM sub-view bound to the highlighted peer") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // cursor 1
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_peer == s.team[1].id);
    CHECK(m.state().cursor == 0);
}
TEST_CASE("sub-view: `back` leaves without sending") {
    UiModel m; const auto s = snap(); SendReq req{};
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);    // -> back
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);
    CHECK(m.state().screen  == Screen::team);
    CHECK(m.take_send_request(req) == false);
}
TEST_CASE("sub-view: double on a message emits a DM request for the bound peer") {
    UiModel m; const auto s = snap(); SendReq req{};
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::double_press, s);
    REQUIRE(m.take_send_request(req) == true);
    CHECK(req.kind == SendKind::dm); CHECK(req.peer_id == s.team[0].id); CHECK(req.text_index == 0);
    CHECK(m.state().compose == Compose::none);
}
TEST_CASE("sub-view auto-exits on inactivity WITHOUT sending") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press, snap(1000)); m.on_gesture(Gesture::double_press, snap(1100));
    m.on_tick(snap(1100 + kBlankMs + 1));
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);
}
TEST_CASE("panel blanks and the waking SHORT press is consumed") {
    UiModel m;
    m.on_tick(snap(1000)); m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    m.on_gesture(Gesture::short_press, snap(1000 + kBlankMs + 10));
    CHECK(m.state().blanked == false);
    CHECK(m.state().screen  == Screen::status);
}
```

- [ ] **Step 2: Run and verify it fails** (`pio test -e native`, then run the binary)

- [ ] **Step 3: Write the implementation**

```cpp
// MeshRoute — src/firmware_ui_model.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure screen/state model for the one-button board UI. Consumes a gesture plus a plain-data snapshot and produces what
// to draw. Knows nothing of g_node, Arduino or the display — that is what keeps it native-testable and every hardware
// concern in board_ui.cpp. See docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md §2-§5.
#pragma once
#include <cstdint>
#include "firmware_ui_input.h"

namespace mrui {

inline constexpr uint32_t kBlankMs      = 15000;
inline constexpr uint8_t  kMaxTeamRows  = 8;    // spec §11: a 3-10 member group; the snapshot reports the TRUE total too
inline constexpr uint8_t  kMaxInboxRows = 8;
inline constexpr uint8_t  kLabelCap     = 14;   // display-clamped teammate label

enum class Screen  : uint8_t { status = 0, team, inbox, send, count };
enum class Compose : uint8_t { none = 0, dm, channel };

inline constexpr uint8_t kDmTextCount      = 3;   // "Are you OK?", "I'm OK", back
inline constexpr uint8_t kChannelTextCount = 3;   // "Got your message", "All good", back

// The model NEVER sends — it ASKS. firmware_ui.cpp drains the request, performs the send and feeds back a typed outcome.
enum class SendKind : uint8_t { emergency = 0, dm, channel_canned };
struct SendReq { SendKind kind = SendKind::emergency; uint8_t peer_id = 0; uint8_t text_index = 0; };

struct TeamRow {
    uint8_t  id = 0; uint32_t last_heard_s = 0; int16_t score_q4 = 0; uint8_t hops = 0;
    char     label[kLabelCap + 1] = {};   // resolved name / 0xhash / bare id, already clamped (spec §3.3)
};
struct InboxRow {
    bool     is_dm = false; uint8_t channel_id = 0; uint32_t rx_age_s = 0;
    char     text[21] = {};               // clamped to the panel width
};

struct UiSnapshot {
    uint32_t now_ms = 0;
    uint16_t unread_dm = 0, unread_ch = 0;
    uint32_t last_dm_age_s = UINT32_MAX, last_ch_age_s = UINT32_MAX;
    uint8_t  team_shown = 0, team_total = 0;      // shown <= kMaxTeamRows; total = rt_team_count() (spec §3.3)
    TeamRow  team[kMaxTeamRows] = {};
    uint8_t  inbox_shown = 0; uint16_t inbox_total = 0;
    InboxRow inbox[kMaxInboxRows] = {};
    uint8_t  my_team_id = 0; uint32_t team_id = 0;
    int32_t  batt_mv = -1;                        // <0 = unavailable -> render "--", never a guess
    bool     team_build = true;
};

struct UiState {
    Screen  screen = Screen::status;
    uint8_t cursor = 0;
    Compose compose = Compose::none;
    uint8_t compose_peer = 0;   // bound at ENTRY: the roster can reorder under an open modal, which would retarget it
    bool    blanked = false;
    bool    dirty   = true;
};

class UiModel {
public:
    void on_gesture(Gesture g, const UiSnapshot& s) {
        if (g == Gesture::none) return;
        _last_input_ms = s.now_ms;
        // ★ spec §4.2: emergency gestures pre-empt EVERYTHING — blank-wake and the compose modal both.
        if (g == Gesture::long_arm || g == Gesture::long_fire || g == Gesture::long_cancel) {
            _st.blanked = false; emergency_gesture(g, s); _st.dirty = true; return;
        }
        if (_st.blanked) { _st.blanked = false; _st.dirty = true; return; }   // the waking press is CONSUMED
        if (_st.compose != Compose::none) { compose_gesture(g); return; }
        if (g == Gesture::short_press)  { advance_or_next(s); _st.dirty = true; }
        else if (g == Gesture::double_press) { activate(s);   _st.dirty = true; }
    }

    void on_tick(const UiSnapshot& s) {
        tick_emergency(s);                                        // Task 3
        if (_st.compose != Compose::none && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) {
            _st.compose = Compose::none; _st.cursor = 0; _st.dirty = true;   // never outlive attention; sends nothing
        }
        if (!_st.blanked && elapsed(s.now_ms, _last_input_ms) >= blank_limit()) { _st.blanked = true; _st.dirty = true; }
    }

    const UiState& state() const { return _st; }
    void clear_dirty() { _st.dirty = false; }
    bool take_send_request(SendReq& out) {
        if (!_req_pending) return false;
        _req_pending = false; out = _req; return true;
    }

protected:
    // Wrap-safe elapsed time. millis() wraps at ~49.7 days; `a >= b` would break across it, this does not.
    static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
    void queue(SendKind k, uint8_t peer, uint8_t idx) { _req = {k, peer, idx}; _req_pending = true; }

    UiState  _st{};
    uint32_t _last_input_ms = 0;
    SendReq  _req{};
    bool     _req_pending = false;

private:
    void advance_or_next(const UiSnapshot& s) {
        const uint8_t n = list_len(s);
        if (n > 1 && _st.cursor + 1 < n) { ++_st.cursor; return; }
        _st.screen = next_screen(_st.screen, s); _st.cursor = 0;
    }
    void activate(const UiSnapshot& s) {
        if (_st.screen == Screen::team && s.team_shown > 0) {
            _st.compose = Compose::dm; _st.compose_peer = s.team[_st.cursor % s.team_shown].id; _st.cursor = 0;
        } else if (_st.screen == Screen::send) {
            _st.compose = Compose::channel; _st.compose_peer = 0; _st.cursor = 0;
        }
    }
    void compose_gesture(Gesture g) {
        const uint8_t n = (_st.compose == Compose::dm) ? kDmTextCount : kChannelTextCount;
        if (g == Gesture::short_press) { _st.cursor = uint8_t((_st.cursor + 1) % n); _st.dirty = true; return; }
        if (g != Gesture::double_press) return;
        if (_st.cursor + 1 == n) { _st.compose = Compose::none; _st.cursor = 0; _st.dirty = true; return; }  // `back`
        queue(_st.compose == Compose::dm ? SendKind::dm : SendKind::channel_canned, _st.compose_peer, _st.cursor);
        _st.compose = Compose::none; _st.cursor = 0; _st.dirty = true;
    }
    uint8_t list_len(const UiSnapshot& s) const {
        if (_st.screen == Screen::team)  return s.team_shown;
        if (_st.screen == Screen::inbox) return s.inbox_shown;
        return 1;
    }
    static Screen next_screen(Screen cur, const UiSnapshot& s) {
        for (uint8_t i = 1; i <= uint8_t(Screen::count); ++i) {
            const Screen cand = Screen((uint8_t(cur) + i) % uint8_t(Screen::count));
            if (s.team_build || cand == Screen::status || cand == Screen::inbox) return cand;
        }
        return Screen::status;
    }
    // Task 3 supplies these; declared here so on_gesture/on_tick compile in task order.
    void emergency_gesture(Gesture g, const UiSnapshot& s);
    void tick_emergency(const UiSnapshot& s);
    uint32_t blank_limit() const;
};

}  // namespace mrui
```

Task 3 defines the three declared members **inline in this same header**, immediately after the class. Do not leave them undefined at the end of Task 2 — instead add temporary inline definitions (`emergency_gesture` empty, `tick_emergency` empty, `blank_limit` returning `kBlankMs`) so Task 2 links and its tests run; Task 3 replaces them.

- [ ] **Step 4: Run and verify all seven cases pass**
- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 3: Emergency and DM outcome machines

**Files:** Modify `src/firmware_ui_model.h`, `test/test_firmware_ui_model.cpp`.

**Interfaces:**
- Produces: `Emergency`, `DmState`, `SendOutcome`, and on `UiModel`: `on_send_accepted(SendKind, uint32_t now_ms)`, `on_send_refused(SendKind, Reason)`, `on_outcome(const SendOutcome&, uint32_t now_ms)`, `emergency()`, `dm_state()`, `arming_secs_left()`, `retry_at_ms()`. Task 4 calls all of these.

Read spec §4 and §4.1-§4.4 first. **Three rules are non-negotiable and each fixes a bug found in review:**

1. An attempt is counted on **acceptance**, never on request — a refusal or a pre-TX block must not consume one of the three alarms.
2. The retry deadline is `now_ms + next_ms` computed **when the block arrives**, not from the originating gesture.
3. `next_ms == 0` means "floor passed, cap/duty still blocking" — it must **not** retry immediately (that spins every tick and burns all three alarms in milliseconds). Use a UI backoff: 2 s, doubling, capped at 30 s, consuming no attempt.

- [ ] **Step 1: Write the failing tests (append)**

```cpp
TEST_CASE("arm then cancel never emits a send") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));   CHECK(m.emergency() == Emergency::arming);
    m.on_gesture(Gesture::long_cancel, snap(2000));CHECK(m.emergency() == Emergency::cancelled);
    CHECK(m.take_send_request(req) == false);
}
TEST_CASE("cancelled auto-returns to idle after its window") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_cancel, snap(2000));
    m.on_tick(snap(2000 + kCancelledMs + 1));
    CHECK(m.emergency() == Emergency::idle);
}
TEST_CASE("arming countdown is visible and decreases") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000));
    const uint8_t a = m.arming_secs_left(snap(1200));
    const uint8_t b = m.arming_secs_left(snap(2400));
    CHECK(b < a);
}
TEST_CASE("attempts are counted on ACCEPTANCE, not on request") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    REQUIRE(m.take_send_request(req) == true);
    m.on_send_refused(SendKind::emergency, RefuseReason::parser);      // put nothing on air
    CHECK(m.emergency() == Emergency::failed);
    CHECK(m.attempts() == 0);                                          // no alarm consumed
}
TEST_CASE("exactly THREE accepted transmissions, then sticky NOT HEARD") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    for (int i = 1; i <= 3; ++i) {
        REQUIRE(m.take_send_request(req) == true);
        m.on_send_accepted(SendKind::emergency, 5000u * uint32_t(i));
        CHECK(m.attempts() == i);
        m.on_outcome(SendOutcome::channel_no_relay(), 5000u * uint32_t(i) + 100);
    }
    CHECK(m.emergency() == Emergency::not_heard);
    CHECK(m.take_send_request(req) == false);
}
TEST_CASE("blocked computes the deadline from the OUTCOME time, not the gesture") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::blocked(10000), /*now_ms=*/60000);
    CHECK(m.emergency() == Emergency::blocked);
    CHECK(m.retry_at_ms() == 70000);                    // 60000 + 10000, NOT 4500 + 10000
    m.on_tick(snap(69000)); CHECK(m.take_send_request(req) == false);
    m.on_tick(snap(70001)); CHECK(m.take_send_request(req) == true);
}
TEST_CASE("next_ms == 0 backs off instead of spinning, and consumes no attempt") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::blocked(0), 5100);
    CHECK(m.emergency() == Emergency::blocked);
    CHECK(m.retry_at_ms() == 5100 + kBlockedBackoffMinMs);
    m.on_tick(snap(5100 + kBlockedBackoffMinMs - 1)); CHECK(m.take_send_request(req) == false);
    m.on_tick(snap(5100 + kBlockedBackoffMinMs + 1)); CHECK(m.take_send_request(req) == true);
    CHECK(m.attempts() == 1);                            // the block did not consume an alarm
}
TEST_CASE("retry deadline is wrap-safe") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 0xFFFFF000u);
    m.on_outcome(SendOutcome::blocked(0x2000), 0xFFFFF000u);   // deadline wraps past 2^32
    m.on_tick(snap(0x00001001u));
    CHECK(m.take_send_request(req) == true);
}
TEST_CASE("long gestures work from inside a compose sub-view") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
    REQUIRE(m.state().compose == Compose::dm);
    m.on_gesture(Gesture::long_arm, s);
    CHECK(m.emergency() == Emergency::arming);
}
TEST_CASE("long gestures work from a blanked panel") {
    UiModel m;
    m.on_tick(snap(1000)); m.on_tick(snap(1000 + kBlankMs + 1));
    REQUIRE(m.state().blanked == true);
    m.on_gesture(Gesture::long_arm, snap(1000 + kBlankMs + 10));
    CHECK(m.emergency() == Emergency::arming);
    CHECK(m.state().blanked == false);
}
TEST_CASE("a matching teammate reply becomes sticky human confirmation") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_relayed(), 5100);
    CHECK(m.emergency() == Emergency::picked_up);
    m.on_reply("Ann", "on my way", 6000);
    CHECK(m.emergency() == Emergency::reply);
}
TEST_CASE("DM outcomes are independent of the emergency machine") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_send_accepted(SendKind::dm, 5100);                       // a DM in flight alongside
    m.on_outcome(SendOutcome::dm_no_key(), 5200);
    CHECK(m.dm_state()  == DmState::no_key);
    CHECK(m.emergency() == Emergency::firing);                    // UNTOUCHED
    m.on_outcome(SendOutcome::channel_relayed(), 5300);
    CHECK(m.emergency() == Emergency::picked_up);
}
```

- [ ] **Step 2: Run and verify it fails**

- [ ] **Step 3: Implement**

Add above `class UiModel`:

```cpp
inline constexpr uint32_t kEmgHoldMs            = 120000;
inline constexpr uint32_t kCancelledMs          = 1000;
inline constexpr uint8_t  kEmgMaxTries          = 3;      // THREE TRANSMISSIONS, counted on acceptance
inline constexpr uint32_t kBlockedBackoffMinMs  = 2000;   // next_ms==0 policy: 2s, doubling, capped
inline constexpr uint32_t kBlockedBackoffMaxMs  = 30000;

enum class Emergency : uint8_t { idle = 0, arming, firing, blocked, picked_up, not_heard, reply, cancelled, failed };
enum class DmState   : uint8_t { idle = 0, submitting, waiting_ack, delivered, no_key, not_confirmed, failed };
enum class RefuseReason : uint8_t { parser = 0, unsealable, no_location, queue_full, other };

// A correlated outcome. Built ONLY by the send tracker (Task 4) after it has matched ctr/peer/channel — the model never
// sees a raw Push, which is what makes a false PICKED UP structurally impossible (spec §2.1).
struct SendOutcome {
    enum class Kind : uint8_t { channel_relayed, channel_no_relay, blocked, dm_acked, dm_no_key, dm_failed, dm_timeout };
    Kind     kind = Kind::channel_no_relay;
    uint32_t next_ms = 0;
    static SendOutcome channel_relayed()   { return {Kind::channel_relayed, 0}; }
    static SendOutcome channel_no_relay()  { return {Kind::channel_no_relay, 0}; }
    static SendOutcome blocked(uint32_t n) { return {Kind::blocked, n}; }
    static SendOutcome dm_acked()          { return {Kind::dm_acked, 0}; }
    static SendOutcome dm_no_key()         { return {Kind::dm_no_key, 0}; }
    static SendOutcome dm_failed()         { return {Kind::dm_failed, 0}; }
    static SendOutcome dm_timeout()        { return {Kind::dm_timeout, 0}; }
};
```

Add to `UiModel`'s public section:

```cpp
    Emergency emergency() const { return _emg; }
    DmState   dm_state()  const { return _dm; }
    uint8_t   attempts()  const { return _tries; }
    uint32_t  retry_at_ms() const { return _retry_at_ms; }
    uint8_t   arming_secs_left(const UiSnapshot& s) const {
        if (_emg != Emergency::arming) return 0;
        const uint32_t left = _arm_fire_at_ms - s.now_ms;
        return (left > 60000u) ? 0 : uint8_t((left + 999) / 1000);        // wrap-safe: a huge value means past-due
    }

    void on_send_accepted(SendKind k, uint32_t now_ms) {
        if (k == SendKind::emergency) { ++_tries; _last_try_ms = now_ms; }
        else if (k == SendKind::dm)   { _dm = DmState::waiting_ack; }
        _st.dirty = true;
    }
    void on_send_refused(SendKind k, RefuseReason r) {
        _refuse = r;
        if (k == SendKind::emergency) _emg = Emergency::failed;   // terminal + actionable, never a stuck SENDING...
        else if (k == SendKind::dm)   _dm  = DmState::failed;
        _st.dirty = true;
    }
    void on_outcome(const SendOutcome& o, uint32_t now_ms) {
        using K = SendOutcome::Kind;
        switch (o.kind) {
            case K::dm_acked:   _dm = DmState::delivered;     _st.dirty = true; return;
            case K::dm_no_key:  _dm = DmState::no_key;        _st.dirty = true; return;
            case K::dm_timeout: _dm = DmState::not_confirmed; _st.dirty = true; return;
            case K::dm_failed:  _dm = DmState::failed;        _st.dirty = true; return;
            default: break;
        }
        if (_emg != Emergency::firing && _emg != Emergency::blocked) return;
        if (o.kind == K::blocked) {
            _emg = Emergency::blocked;
            const uint32_t d = (o.next_ms > 0) ? o.next_ms : next_backoff();
            _retry_at_ms = now_ms + d;                        // ★ from the OUTCOME time, not the gesture
            _st.dirty = true; return;
        }
        if (o.kind == K::channel_relayed) { _emg = Emergency::picked_up; _st.dirty = true; return; }
        if (_tries >= kEmgMaxTries) { _emg = Emergency::not_heard; _st.dirty = true; return; }
        _emg = Emergency::firing; queue(SendKind::emergency, 0, 0); _st.dirty = true;
    }
    void on_reply(const char* who, const char* text, uint32_t now_ms) {
        if (_emg == Emergency::idle) return;                  // only while an emergency is live or sticky
        copy_clamped(_reply_who,  who,  sizeof _reply_who);
        copy_clamped(_reply_text, text, sizeof _reply_text);
        _emg = Emergency::reply; _emg_hold_until_ms = now_ms + kEmgHoldMs; _st.dirty = true;
    }
    const char* reply_who()  const { return _reply_who; }
    const char* reply_text() const { return _reply_text; }
```

And define the three members Task 2 declared:

```cpp
inline void UiModel::emergency_gesture(Gesture g, const UiSnapshot& s) {
    if (g == Gesture::long_arm)    { _emg = Emergency::arming; _arm_fire_at_ms = s.now_ms + kArmToFireMs; return; }
    if (g == Gesture::long_cancel) { _emg = Emergency::cancelled; _cancelled_until_ms = s.now_ms + kCancelledMs; return; }
    // long_fire
    _emg = Emergency::firing; _tries = 0; _backoff_ms = 0;
    _emg_hold_until_ms = s.now_ms + kEmgHoldMs;
    queue(SendKind::emergency, 0, 0);
}
inline void UiModel::tick_emergency(const UiSnapshot& s) {
    if (_emg == Emergency::cancelled && elapsed(s.now_ms, _cancelled_until_ms) < (1u << 31)) { _emg = Emergency::idle; _st.dirty = true; }
    if (_emg == Emergency::blocked && _retry_at_ms != _no_deadline &&
        elapsed(s.now_ms, _retry_at_ms) < (1u << 31)) {                 // wrap-safe "now >= deadline"
        _retry_at_ms = _no_deadline; _emg = Emergency::firing; queue(SendKind::emergency, 0, 0); _st.dirty = true;
    }
    if (_emg == Emergency::arming) {                                     // dirty ONLY when the visible digit changes
        const uint8_t d = arming_secs_left(s);
        if (d != _last_countdown) { _last_countdown = d; _st.dirty = true; }
    }
}
inline uint32_t UiModel::blank_limit() const {
    const bool live = (_emg == Emergency::arming || _emg == Emergency::firing ||
                       _emg == Emergency::blocked || _emg == Emergency::not_heard || _emg == Emergency::reply);
    return live ? kEmgHoldMs : kBlankMs;
}
```

with the private members `_emg`, `_dm`, `_refuse`, `_tries`, `_retry_at_ms` (init `_no_deadline`), `_last_try_ms`, `_arm_fire_at_ms`, `_cancelled_until_ms`, `_emg_hold_until_ms`, `_backoff_ms`, `_last_countdown`, `_reply_who[kLabelCap+1]`, `_reply_text[21]`, a `static constexpr uint32_t _no_deadline = 0xFFFFFFFFu`, `kArmToFireMs = 3500` matching `InputCfg::fire_ms`, plus:

```cpp
    uint32_t next_backoff() {
        _backoff_ms = (_backoff_ms == 0) ? kBlockedBackoffMinMs
                                         : ((_backoff_ms * 2 > kBlockedBackoffMaxMs) ? kBlockedBackoffMaxMs : _backoff_ms * 2);
        return _backoff_ms;
    }
    static void copy_clamped(char* dst, const char* src, size_t cap) {
        size_t i = 0; for (; src && src[i] && i + 1 < cap; ++i) dst[i] = src[i]; dst[i] = '\0';
    }
```

- [ ] **Step 4: Run and verify every case passes** (Tasks 1-3 together)
- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 4: Send tracker — attribution

**Files:** Create `src/firmware_ui_send.h`, `test/test_firmware_ui_send.cpp`.

**Interfaces:**
- Produces: `mrui::SendTracker` with `void submit(SendKind, uint8_t peer_id, uint8_t channel_id, uint32_t now_ms)`, `void accept(uint16_t ctr, uint32_t now_ms)`, `void refuse()`, `bool match_channel_sent(uint16_t ctr, bool relayed, SendOutcome& out)`, `bool match_blocked(bool blocked_channel, uint32_t next_ms, uint32_t now_ms, SendOutcome& out)`, `bool match_dm(uint16_t ctr, uint8_t dst, bool acked, bool no_pubkey, SendOutcome& out)`, `bool idle() const`.

★ **This is the task that prevents a false safety confirmation.** Pushes are node-wide: a console post, a BLE post or a canned message all raise `channel_sent`. Without correlation, any of them completes an emergency that was never transmitted. Read spec §2.1.

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_firmware_ui_send.cpp
#include <doctest.h>
#include "firmware_ui_send.h"
using namespace mrui;

TEST_CASE("an unrelated channel_sent cannot complete the emergency") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(/*ctr=*/77, 1010);
    CHECK(t.match_channel_sent(/*ctr=*/12, /*relayed=*/true, o) == false);   // someone else's post
    CHECK(t.match_channel_sent(/*ctr=*/77, /*relayed=*/true, o) == true);
    CHECK(o.kind == SendOutcome::Kind::channel_relayed);
}
TEST_CASE("a blocked DM cannot block the emergency") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(77, 1010);
    CHECK(t.match_blocked(/*blocked_channel=*/false, 5000, 1020, o) == false);
    CHECK(t.match_blocked(/*blocked_channel=*/true,  5000, 1020, o) == true);
}
TEST_CASE("a blocked event outside the outcome window is ignored") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(77, 1010);
    CHECK(t.match_blocked(true, 5000, 1010 + kOutcomeWindowMs + 1, o) == false);
}
TEST_CASE("a DM outcome must match ctr AND peer") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, /*peer=*/174, 0, 1000); t.accept(/*ctr=*/900, 1010);
    CHECK(t.match_dm(900, /*dst=*/99,  true, false, o) == false);   // right ctr, wrong peer
    CHECK(t.match_dm(901, /*dst=*/174, true, false, o) == false);   // right peer, wrong ctr
    CHECK(t.match_dm(900, /*dst=*/174, true, false, o) == true);
    CHECK(o.kind == SendOutcome::Kind::dm_acked);
}
TEST_CASE("no_pubkey maps to dm_no_key") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, 174, 0, 1000); t.accept(900, 1010);
    REQUIRE(t.match_dm(900, 174, false, /*no_pubkey=*/true, o) == true);
    CHECK(o.kind == SendOutcome::Kind::dm_no_key);
}
TEST_CASE("a refused submit leaves nothing to match") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.refuse();
    CHECK(t.idle() == true);
    CHECK(t.match_channel_sent(77, true, o) == false);
}
```

- [ ] **Step 2: Run and verify it fails**

- [ ] **Step 3: Implement**

```cpp
// MeshRoute — src/firmware_ui_send.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★ The attribution layer. Pushes are NODE-WIDE: channel_sent / send_blocked fire for every origination, including
// console, BLE and canned sends. Feeding them straight to the UI model lets an unrelated post complete an emergency
// that was never transmitted — a FALSE SAFETY CONFIRMATION. Nothing reaches the model until it has matched here.
// One in-flight UI send at a time, which is what makes the ctr-less send_blocked correlatable at all. Spec §2.1.
#pragma once
#include <cstdint>
#include "firmware_ui_model.h"

namespace mrui {

inline constexpr uint32_t kOutcomeWindowMs = 8000;   // how long an accepted send may still claim a ctr-less outcome

class SendTracker {
public:
    void submit(SendKind k, uint8_t peer_id, uint8_t channel_id, uint32_t now_ms) {
        _k = k; _peer = peer_id; _chan = channel_id; _state = State::submitted; _submit_ms = now_ms; _ctr = 0;
    }
    void accept(uint16_t ctr, uint32_t now_ms) { _ctr = ctr; _accept_ms = now_ms; _state = State::accepted; }
    void refuse()                              { _state = State::idle; }
    void close()                               { _state = State::idle; }
    bool idle() const                          { return _state == State::idle; }
    SendKind kind() const                      { return _k; }

    bool match_channel_sent(uint16_t ctr, bool relayed, SendOutcome& out) {
        if (_state != State::accepted) return false;
        if (_k == SendKind::dm) return false;
        if (ctr != _ctr) return false;                    // ★ the only reliable correlator
        out = relayed ? SendOutcome::channel_relayed() : SendOutcome::channel_no_relay();
        _state = State::idle; return true;
    }
    // send_blocked carries NO ctr (command.h). Scope by channel-ness + the bounded window; serialisation does the rest.
    bool match_blocked(bool blocked_channel, uint32_t next_ms, uint32_t now_ms, SendOutcome& out) {
        if (_state != State::accepted) return false;
        if (_k == SendKind::dm) return false;
        if (!blocked_channel) return false;
        if (uint32_t(now_ms - _accept_ms) > kOutcomeWindowMs) return false;
        out = SendOutcome::blocked(next_ms);
        _state = State::idle; return true;
    }
    bool match_dm(uint16_t ctr, uint8_t dst, bool acked, bool no_pubkey, SendOutcome& out) {
        if (_state != State::accepted || _k != SendKind::dm) return false;
        if (ctr != _ctr || dst != _peer) return false;    // ★ ctr AND peer
        out = acked ? SendOutcome::dm_acked() : (no_pubkey ? SendOutcome::dm_no_key() : SendOutcome::dm_failed());
        _state = State::idle; return true;
    }

private:
    enum class State : uint8_t { idle = 0, submitted, accepted };
    State    _state = State::idle;
    SendKind _k = SendKind::emergency;
    uint8_t  _peer = 0, _chan = 0;
    uint16_t _ctr = 0;
    uint32_t _submit_ms = 0, _accept_ms = 0;
};

}  // namespace mrui
```

- [ ] **Step 4: Run and verify all six cases pass**
- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 5: Board canvas port

**Files:** Modify `platformio.ini` (`[env:heltec_v3]`), create `src/board_ui.h`, modify `src/board_ui.cpp`.

**Interfaces:**
- Produces: `mrui::board_init()`, `begin_frame()`, `next_page()`, `set_font(Font)`, `draw_text(x,y,const char*)`, `draw_hline(x,y,w)`, `set_power_save(bool)`, `button_pressed()`, `battery_sample_mv()`.

**Read spec §5 first.** A full 1024 B frame at 400 kHz is ~25 ms of blocking I²C; `cts_to_data_gap_ms` is 5 and turnarounds are 5-8 ms. Page-chunking is why the `_1_` (page-buffer) constructor is specified — do not substitute a full-buffer driver.

- [ ] **Step 1: Add the dependency, pinned exactly**

In `[env:heltec_v3]` `lib_deps`:

```ini
  olikraus/U8g2 @ 2.35.30      ; PINNED EXACTLY (same rule as the RadioLib pin on this env): a caret lets different
                               ; checkouts resolve different versions and silently skews the board RAM/Flash baseline.
```

- [ ] **Step 2: Build and record the pre-UI baseline**

Run: `pio run -e heltec_v3` — SUCCESS. Record flash/RAM.

- [ ] **Step 3: Write the canvas header**

```cpp
// MeshRoute — src/board_ui.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The display-INDEPENDENT canvas the UI feature layer draws through. Nothing above this line knows U8g2 exists;
// nothing below it knows what a "screen" is (spec §2 hard boundary). ★ This header must NOT include
// firmware_ui_model.h — an earlier plan draft did, and that inverted the boundary the spec promises.
#pragma once
#include <cstdint>

namespace mrui {

enum class Font : uint8_t { small = 0, large };   // 6x10 / 10x20

void board_init();
void begin_frame();                 // compose a new frame; does NOT touch the bus
bool next_page();                   // push ONE page (~3 ms); true while pages remain
void set_font(Font f);
void draw_text(int x, int y, const char* s);
void draw_hline(int x, int y, int w);
void set_power_save(bool on);       // panel off/on WITHOUT clearing display RAM; latched, repeat calls are no-ops
bool button_pressed();
int32_t battery_sample_mv();        // one sample; <0 = unavailable. Caller decides WHEN (spec §7)

}  // namespace mrui
```

- [ ] **Step 4: Implement the board TU**

```cpp
#include "mr_features.h"
#if MR_FEAT_OLED
#include <U8g2lib.h>
#include "board_ui.h"

static U8G2_SSD1306_128X64_NONAME_1_HW_I2C s_u8g2(U8G2_R0, /*reset=*/21, /*scl=*/18, /*sda=*/17);
static bool s_painting = false, s_asleep = false;

namespace mrui {
void board_init() { pinMode(MR_UI_BTN_PIN, INPUT_PULLUP); battery_init(); s_u8g2.begin(); set_font(Font::small); }
void begin_frame()               { s_u8g2.firstPage(); s_painting = true; }
bool next_page()                 { if (!s_painting) return false; if (s_u8g2.nextPage()) return true; s_painting = false; return false; }
void set_font(Font f)            { s_u8g2.setFont(f == Font::large ? u8g2_font_10x20_tf : u8g2_font_6x10_tf); }
void draw_text(int x,int y,const char* s) { s_u8g2.drawStr(x, y, s); }
void draw_hline(int x,int y,int w)        { s_u8g2.drawHLine(x, y, w); }
// ★ EDGE-triggered. An earlier draft called clearDisplay() every tick while blanked — that is a full-frame I2C
// transfer, on every service pass, defeating the page-chunking rule entirely. setPowerSave keeps display RAM.
void set_power_save(bool on) {
    if (on == s_asleep) return;
    s_u8g2.setPowerSave(on ? 1 : 0); s_asleep = on; if (on) s_painting = false;
}
bool button_pressed() { return digitalRead(MR_UI_BTN_PIN) == LOW; }
}  // namespace mrui
#endif
```

The `mr_ui_*` hooks are **not** defined here — they live in `firmware_ui.cpp` (Task 6). `battery_init` / `battery_sample_mv` land in Task 9; until then provide `int32_t battery_sample_mv() { return -1; }` and an empty `battery_init()` so the TU links and the panel renders `--`.

- [ ] **Step 5: Build and flash; the panel lights**

Run: `pio run -e heltec_v3 -t upload`. If the panel stays dark, suspect the reset pin (spec §14 Q1) before the driver.

- [ ] **Step 6: Report ready — do NOT commit**

---

### Task 6: Feature layer — snapshot, render policy, tick

**Files:** Create `src/firmware_ui.cpp`; modify `platformio.ini` (`build_src_filter`, pins).

**Interfaces:** implements `mr_ui_init` / `mr_ui_tick` / `mr_ui_on_push`; owns `build_snapshot()`, `draw_frame()`, the battery cache and the push correlation.

- [ ] **Step 1: Add pins and constants**

```ini
  -DMR_UI_BTN_PIN=0             ; Heltec V3/V4 user button. Active LOW, INPUT_PULLUP.
                                ; NB GPIO0 is the ESP32-S3 boot strap: held across a reset it enters download mode.
  -DMR_UI_TEAM_CHANNEL_ID=0     ; owner ruling: build constant, no cfg key / NV field / console verb
  -DMR_UI_ADC_CTRL=37
  -DMR_UI_VBAT_READ=1
```

and `+<firmware_ui.cpp>` in `build_src_filter`.

- [ ] **Step 2: Write the snapshot builder and battery cache**

```cpp
// MeshRoute — src/firmware_ui.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The board-UI FEATURE layer (U3). Owns the model, the send tracker, ALL render policy, and the correlation of
// node-wide pushes into UI outcomes. Adds no new core API — every read is an accessor that already existed (spec §6).
#include "mr_features.h"
#if MR_FEAT_OLED
#include "firmware_ui_model.h"
#include "firmware_ui_send.h"
#include "board_ui.h"
#include "fw_context.h"
#include "firmware_commands.h"   // mrfw::dispatch
#include "dispatch_sink.h"

static mrui::UiModel    s_model;
static mrui::InputFsm   s_input;
static mrui::SendTracker s_tracker;
static uint32_t s_last_dm_ms = 0, s_last_ch_ms = 0;
static uint16_t s_unread_dm = 0, s_unread_ch = 0;
static int32_t  s_batt_mv = -1;
static uint32_t s_batt_next_ms = 0;

static bool mac_idle() {   // spec §5 rule 1 — the same predicate fw_main.cpp uses to decide it may sleep
    return !g_iradio.tx_busy() && g_hal.txq_depth() == 0;
}

// Battery: sampled at boot and every 30 s, only when the MAC is idle. An earlier draft sampled 8 ADC reads on EVERY
// service pass for a value that changes over minutes (spec §7).
static void battery_maybe_sample(uint32_t now_ms) {
    if (!mac_idle()) return;
    if (s_batt_mv >= 0 && uint32_t(now_ms - s_batt_next_ms) >= (1u << 31)) return;   // wrap-safe "not due yet"
    const int32_t mv = mrui::battery_sample_mv();
    if (mv >= 0) s_batt_mv = mv;
    s_batt_next_ms = now_ms + 30000;
}
```

`build_snapshot(now_ms)` fills `UiSnapshot` from: `s_unread_dm/ch`; `s_last_dm_ms/ch` for ages; `g_node.rt_team_count()` into `team_total` and the first `kMaxTeamRows` of `rt_team_at(i)` into `team[]` with `team_shown`; each row's label resolved `team_key_of_id()` → `peer_name_find()` → `0x<hash>` → bare id, clamped to `kLabelCap`; `g_node.team_local_id()`; `g_node.config().team_id`; `s_batt_mv`. **No BLE field** — `mrble::connected()` is inert on ESP32 (`device_ble.h:47`), so V3 shows nothing rather than a permanently-false indicator.

- [ ] **Step 3: Write the render policy (in THIS file, not the board TU)**

`draw_frame(const UiState&, const UiSnapshot&)` draws the status bar (`DM<n> CH<n> T<shown>/<total> <volts>`), then dispatches on `compose` first, then `screen`, then the emergency overlay when `emergency() != idle`. All text formatting lives here; only `mrui::draw_text` / `set_font` / `draw_hline` cross the boundary. Emergency states use `Font::large`; everything else `Font::small`. Battery renders `3.9V` or `--`, never a percentage.

- [ ] **Step 4: Write the tick**

```cpp
void mr_ui_tick(uint32_t now_ms) {
    static uint32_t s_last_paint_ms = 0;
    static bool     s_frame_open = false;
    battery_maybe_sample(now_ms);
    const mrui::UiSnapshot s = build_snapshot(now_ms);
    s_model.on_gesture(s_input.update(mrui::button_pressed(), now_ms), s);
    s_model.on_tick(s);
    if (s_model.state().screen == mrui::Screen::inbox) { s_unread_dm = 0; s_unread_ch = 0; }

    mrui::SendReq req{};
    if (s_tracker.idle() && s_model.take_send_request(req)) ui_perform_send(req, now_ms);   // one in flight at a time

    if (!mac_idle()) return;                                  // never start OR continue a paint mid-exchange
    if (s_frame_open) { s_frame_open = mrui::next_page(); return; }   // one page, then yield the bus
    if (s_model.state().blanked) { mrui::set_power_save(true); s_model.clear_dirty(); return; }
    mrui::set_power_save(false);
    const bool emg = s_model.emergency() != mrui::Emergency::idle;
    if (s_model.state().dirty && (emg || uint32_t(now_ms - s_last_paint_ms) >= 500)) {
        mrui::begin_frame(); draw_frame(s_model.state(), s);
        s_frame_open = mrui::next_page(); s_model.clear_dirty(); s_last_paint_ms = now_ms;
    }
}
void mr_ui_init() { mrui::board_init(); }
```

Emergency bypasses the 2 Hz throttle but **not** the MAC-idle gate, and the model marks itself dirty only when the countdown digit changes (Task 3), so this does not repaint at tick rate.

- [ ] **Step 5: Write the push correlation**

```cpp
void mr_ui_on_push(const meshroute::Push& pu) {
    using PK = meshroute::PushKind;
    mrui::SendOutcome o{};
    const uint32_t now = uint32_t(g_hal.now());
    switch (pu.kind) {
        case PK::msg_recv:     s_last_dm_ms = now; if (s_unread_dm < 999) ++s_unread_dm; break;
        case PK::channel_recv:
            s_last_ch_ms = now; if (s_unread_ch < 999) ++s_unread_ch;
            if (pu.channel_id == MR_UI_TEAM_CHANNEL_ID)                      // spec §4.4: ONLY our channel qualifies
                s_model.on_reply(label_for_origin(pu), reinterpret_cast<const char*>(pu.body), now);
            break;
        case PK::channel_sent:
            if (s_tracker.match_channel_sent(pu.ctr, pu.relayed, o)) s_model.on_outcome(o, now);
            break;
        case PK::send_blocked:
            if (s_tracker.match_blocked(pu.blocked_channel, pu.next_ms, now, o)) s_model.on_outcome(o, now);
            break;
        case PK::send_e2e_acked:
            if (s_tracker.match_dm(pu.ctr, pu.dst, /*acked=*/true, false, o)) s_model.on_outcome(o, now);
            break;
        case PK::send_failed:
            if (s_tracker.match_dm(pu.ctr, pu.dst, false, pu.reason == meshroute::SendFailReason::no_pubkey, o))
                s_model.on_outcome(o, now);
            break;
        default: break;
    }
}
```

★ Every branch that can move the emergency goes through the tracker. Verify the exact `Push` field names against `lib/core/command.h` before writing — `relayed` and `next_ms` are confirmed, `blocked_channel`, `ctr`, `dst`, `channel_id` and the `SendFailReason` enumerator spelling must be re-checked (V2: specs are point-in-time).

- [ ] **Step 6: Build, flash, verify the cycle**

Short presses walk TEAM then move on; counts and battery render; the panel sleeps after 15 s and the waking press does not change screen.

- [ ] **Step 7: Report ready — do NOT commit**

---

### Task 7: Sends, compose sub-views, inbox adapter

**Files:** Modify `src/firmware_ui.cpp`.

- [ ] **Step 1: Implement `ui_perform_send` with a typed result**

```cpp
static const char* const kDmTexts[]      = { "Are you OK?", "I'm OK" };
static const char* const kChannelTexts[] = { "Got your message", "All good" };

static void ui_perform_send(const mrui::SendReq& req, uint32_t now_ms) {
    char line[96]; int n = 0;
    if (req.kind == mrui::SendKind::dm) {
        if (req.text_index >= 2) return;
        // §3.4 cleartext DM by team_local_id. -t = TEAM plane, -a = end-to-end ack (the confirmation a channel post
        // can never give). NO -e: the parser gates it allow_e=by_hash and rejects it on an id target; crypt stays
        // `def` = the node's e2e_dm. We do NOT force plaintext — CryptIntent::off was deliberately removed.
        n = snprintf(line, sizeof line, "send %u \"%s\" -t -a", unsigned(req.peer_id), kDmTexts[req.text_index]);
        s_tracker.submit(mrui::SendKind::dm, req.peer_id, 0, now_ms);
    } else {
        const bool emergency = (req.kind == mrui::SendKind::emergency);
        const char* body = emergency ? "I'm in danger"
                                     : (req.text_index < 2 ? kChannelTexts[req.text_index] : nullptr);
        if (!body) return;
        // ★★ §4.1: -l is CONDITIONAL. on_command REFUSES `-t -l` with no_fix when lat_e7==0 && lon_e7==0
        // (node.cpp:1526). Sending it unconditionally would turn "no fix" into NO ALARM AT ALL.
        const meshroute::NodeConfig& cfg = g_node.config();
        const bool have_fix = emergency && (cfg.lat_e7 != 0 || cfg.lon_e7 != 0);
        n = have_fix
            ? snprintf(line, sizeof line, "send_channel %u \"%s\" -t -l -e", unsigned(MR_UI_TEAM_CHANNEL_ID), body)
            : snprintf(line, sizeof line, "send_channel %u \"%s\" -t -e",    unsigned(MR_UI_TEAM_CHANNEL_ID), body);
        s_tracker.submit(req.kind, 0, MR_UI_TEAM_CHANNEL_ID, now_ms);
    }
    if (n <= 0 || size_t(n) >= sizeof line) { s_tracker.refuse(); s_model.on_send_refused(req.kind, mrui::RefuseReason::other); return; }

    // ★ The synchronous result must reach the model TYPED — never a discarded BufferSink, or a parser refusal leaves
    // the panel on SENDING... forever (spec §2.1).
    BufferSink sink;
    const meshroute::CmdResult r = mrfw::dispatch_typed(line, size_t(n), sink);
    if (r.code == meshroute::CmdCode::queued) { s_tracker.accept(r.ctr, now_ms); s_model.on_send_accepted(req.kind, now_ms); }
    else { s_tracker.refuse(); s_model.on_send_refused(req.kind, refuse_reason_of(r.code)); }
}
```

**`mrfw::dispatch_typed` does not exist yet.** `dispatch()` returns `bool` and writes text. Add a small typed sibling in `firmware_commands.{h,cpp}` that runs the same parse → `on_command` path and returns the `CmdResult`, with `dispatch()` implemented in terms of it so there is **one** parser path (U1). This is the one place this plan touches an existing firmware file; it adds no verb, key or wire surface. If the reviewer prefers otherwise, **stop and ask** rather than parsing `sink` text.

- [ ] **Step 2: Implement the compose sub-view render** — header line (`to: <label>` / `to: team`), then the item list with a `>` marker on `cursor`, `back, don't send` last. Outcome states replace the list: `SENDING...`, `SENT, waiting`, `DELIVERED to <label>`, `NO KEY`, `NO CONFIRM`, or the refusal reason.

- [ ] **Step 3: Implement the inbox adapter over `Inbox::pull()`**

Call `g_node.inbox().pull(dm_since, chan_since, cb, ctx)` directly — **never** dispatch textual `pull_inbox` into the 512 B `BufferSink`. Keep the newest `kMaxInboxRows`; prefix each `DM` or `CH<n>`; clamp body text to 20 chars. `pull()` returns the DM block oldest-first then the channel block oldest-first (`inbox.h:107-109`) — render in that block order and do **not** imply chronological interleaving. Viewing does **not** advance the durable `mark_read` cursor.

- [ ] **Step 4: Build, flash, verify all four paths**

1. **Channel** — SEND → `double` → `double` on "Got your message": the second node receives it.
2. **`back`** — open either sub-view, walk to the last item, `double`: closes, **nothing transmitted** (confirm no dispatched line on the console).
3. **DM** — TEAM → walk to a teammate → `double` → `double` on "Are you OK?": the second node receives it and the sender shows `DELIVERED`.
4. **Inbox** — rows appear labelled `DM`/`CH`, bounded and readable.

- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 8: Emergency end-to-end on hardware

**Files:** Modify `src/firmware_ui.cpp` (render only — the machine is built and tested in Task 3).

- [ ] **Step 1: Render the emergency states** (`Font::large`)

`arming` → `RELEASE TO CANCEL` + `arming_secs_left()`; `firing` → `SENDING...`; `blocked` → `BLOCKED` + `retry in Ns` from `retry_at_ms()`; `picked_up` → **`PICKED UP`** (never `DELIVERED` — it means a neighbour re-flooded it); `not_heard` → `NOT HEARD` + `hold=retry`; `reply` → `REPLY <who>` + clamped text; `cancelled` → `CANCELLED`; `failed` → the refusal reason.

- [ ] **Step 2: Bench matrix (two nodes, same team)** — add each line to `docs/2026-07-31-bench-test-script.md` per M2

1. Long-press from every screen **and from inside both compose sub-views** reaches SENDING.
2. Release at ~3.0 s cancels and auto-returns after ~1 s; release past 3.5 s fires.
3. Second node off → `NOT HEARD` after exactly **3 accepted transmissions**, then sticky.
4. Second node on → `PICKED UP`; a reply on channel 0 → `REPLY`.
5. Fire twice inside 10 s → `BLOCKED` with a live countdown, then auto-fires (`channel_min_interval_ms` 10000).
6. Emergency on a blanked panel works and wakes it.
7. **Conditional `-l`, both halves:** with `cfg set lat/lon` set → the receiver's record carries a position. With `lat 0` / `lon 0` → **the alarm still goes out**, positionless, with no `no_location` refusal. If this fails, the conditional is inverted and the feature is broken where it matters.
8. **Attribution:** while the emergency is `firing`, post a canned channel message from the *console* of the same node — the emergency state must **not** move.
9. **Blanked panel produces no repeated I²C** — confirm by trace counter or scope.

- [ ] **Step 3: Report ready — do NOT commit**

---

### Task 9: V3 battery reader

**Files:** Modify `src/board_ui.cpp`.

Spec §7 is the authority. **V3 polarity is auto-detected** (boards past rev 3.2 inverted it) — do not hardcode LOW. V3 has **no** settling delay; do not import V4's `delay(10)`.

- [ ] **Step 1: Implement**

```cpp
static bool s_adc_active_high = false;

static void battery_init() {
    pinMode(MR_UI_ADC_CTRL, INPUT);
    s_adc_active_high = (digitalRead(MR_UI_ADC_CTRL) == LOW);      // probe idle, invert: MeshCore HeltecV3Board::begin()
    pinMode(MR_UI_ADC_CTRL, OUTPUT);
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);  // park inactive
}

namespace mrui {
int32_t battery_sample_mv() {
    analogReadResolution(10);
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? HIGH : LOW);
    uint32_t raw = 0;
    for (int i = 0; i < 8; ++i) raw += analogRead(MR_UI_VBAT_READ);
    raw /= 8;
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);
    return int32_t(5.42f * (3.3f / 1024.0f) * float(raw) * 1000.0f);
}
}
```

- [ ] **Step 2: Verify against a multimeter** — within ~50 mV. A consistent ratio error means this revision's divider differs; record the measured value, do not tune to taste. Add the check to the bench script (M2).

- [ ] **Step 3: Confirm the cadence** — instrument or trace that sampling happens ~every 30 s and never while the MAC is busy.

- [ ] **Step 4: Report ready — do NOT commit**

---

## Final gate

- [ ] `pio test -e native` then **run** `./.pio/build/native/program` — real count printed, 0 failed
- [ ] s18 md5 **exact** vs the current `simulation/BASELINE.md` keystone
- [ ] The mandatory mobile/team scenarios per `BASELINE.md` §2 — 0 assertion failures
- [ ] `pio run` for `gateway`, `xiao_sx1262`, `xiao_esp32s3`, `heltec_v3`, `heltec_mobile` — green, no new warnings
- [ ] Flash/RAM delta recorded for the Heltec envs vs the Task 5 Step 2 baseline
- [ ] Bench-only checks added to `docs/2026-07-31-bench-test-script.md` (M2)
- [ ] Report ready with the numbers. **The owner commits.**

## Open items this plan does not decide

- **Arm duration 3.5 s** — a bench opinion. Tune `InputCfg::fire_ms` (and the matching `kArmToFireMs`) after Task 8.
- **V3 panel reset pin** — 21 per `board_ui.cpp:14`; MeshCore's V3 variant defines none. Confirm during Task 5 Step 5.
- **`mrfw::dispatch_typed`** — Task 7 Step 1 proposes factoring the typed result out of the existing dispatch path. It adds no verb, key or wire surface, but it does touch an existing firmware file; confirm before implementing.
