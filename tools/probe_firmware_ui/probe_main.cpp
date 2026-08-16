// MeshRoute — tools/probe_firmware_ui/probe_main.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §B105 PROBE — host-compiles the REAL `src/firmware_ui.cpp` and MEASURES the four behaviours [[B104]] named as having
// no behavioural cover at all: RENDER POLICY (the caller half of once-per-page), the §5 MAC-IDLE GATE, the 2 Hz
// THROTTLE, and the BATTERY CADENCE — plus, since §UI-9, what the cadence PUTS ON THE PANEL (P5: `--` vs volts).
//
// ★★ WHY IT COULD NOT EXIST BEFORE, in one line: `firmware_ui.cpp` included `fw_context.h` -> `<RadioLib.h>` -> not
//    host-compilable. [[B105]] replaced that with `fw_context_pure.h` + `DeviceHal::radio()`, and this file is the
//    whole point of that refactor. ⇒ if someone puts `fw_context.h` back, THIS BUILD BREAKS. That is intended.
//
// ★ WHAT IS FAKED AND WHAT IS REAL, because the split is what makes the measurements mean anything:
//     REAL   `src/firmware_ui.cpp` itself · `meshroute::Node` · `meshroute::DeviceHal` (so `txq_depth()` is the real
//            outbound queue, moved by the real `tx()`) · `mrui::UiModel` / `FrameGate` / the trackers.
//     FAKED  the CANVAS (`board_ui.h`'s nine entry points) — counting stand-ins, so a page loop and a battery sample
//            are observable; the RADIO (`IRadio`) — scriptable, so `tx_busy()` can be driven; and `mrfw::exec_command`.
//   ⓘ The canvas is faked rather than linked from `variants/heltec_v3/board_ui.cpp` DELIBERATELY: that TU already has
//     its own probe (`tools/probe_board_ui/`), which measures the panel side — including, since §UI-9, the real ADC
//     reader's polarity/enable/disable/plausibility behaviour. Faking it here keeps this probe pointed at the FEATURE
//     layer and lets `battery_sample_mv()` answer differently per case, which is what makes the "a GOOD reading
//     arrives, then the reader goes unavailable" arm reachable at all.
//     ⛔ CORRECTED IN PLACE 2026-08-06: this note used to say the real reader "is hardcoded `-1` until Task 9". Task 9
//        has landed; the real one now reads hardware and can answer either way.
//
// ★★★ EVERY CHECK MUST BE ABLE TO FAIL. `run.sh` re-runs this binary against mutated COPIES of `firmware_ui.cpp` (the
//     tempting WRONG fixes, not just deletions) and requires each to turn it RED. A check no mutation can break is
//     recorded as vacuous, not as a pass — this arc has already shipped seven instruments that could not fail.
//   ★ THE RATIO IS NO LONGER WRITTEN DOWN HERE. It used to read "20 of the 25 checks are reddened by … 13 controls",
//     and it went stale the moment a slice added checks — a hand-maintained coverage claim in a comment is the same
//     defect class as a bench doc restating a constant's value ([[B120]]). `run.sh` now MEASURES it and prints
//     `coverage: N of M checks are reddened by at least one control`, NAMING every exception (`PROBE_LIST=1` makes
//     each CHK announce itself, which supplies the denominator).
//   ⓘ The standing exceptions are the five P2b lines, and they are exceptions ON PURPOSE: they are HARNESS
//     PRECONDITIONS asserting `DeviceHal`/`IRadio` behaviour, not `firmware_ui.cpp`'s — "the frame was accepted",
//     "the queue is non-empty", "the radio is still idle", "the queue drained", "the frame really went to the radio".
//     No mutation of the file under test can move them, and that is correct: their job is to fail if the QUEUE ever
//     stops being non-empty at the moment P2b measures suppression, because then P2b's real check would be passing
//     over an empty queue. They are this probe's own vacuity guard, in the W10b sense, and they can fail: a
//     `DeviceHal::tx` that sent immediately instead of enqueuing would trip them.
//   ⓘ §UI-7D slice B ADDS FIVE MORE STANDING EXCEPTIONS, and they are the same shape — HARNESS PRECONDITIONS about
//     `meshroute::Inbox`, not about this file: "the probe's real inbox is wired", "a DM is recorded", "a channel post is
//     recorded", "six live records to browse", and the negative-space "P6f ...and deletes nothing else" (a REFUSED
//     activation never reaches the store at all, so no mutation of the served path can move it). They can still fail —
//     a `record_*` that stopped returning the assigned seq, or an `erase` that took a bystander, would trip them, and
//     that is exactly their job: without them the P6 phases could be passing over an EMPTY store.
//   ⓘ §UI-14 ADDS TEN MORE, AND EVERY ONE OF THEM IS NEGATIVE SPACE — the shape that no mutation of the file under
//     test can move, because it asserts that something did NOT happen: "opening it wrote NOTHING" / "applied NOTHING
//     live" / "the persisted record is untouched" / "the NON-covered fields carried through" / "changed nothing" /
//     "the marker is GONE once it is durable" / "STATUS is clean again" / "it is NOT reported as unsaved" / "no
//     RESTART is claimed for a live field" / "NOT the word `dirty` in any form". ★ They are the checks that make the
//     POSITIVE ones mean something — "SAVED" is only evidence if a REFUSAL does not also say it — and they can still
//     fail: a renderer that showed the effective value, or a save path that wrote twice, trips them. ⚠ The one that
//     LOOKS breakable and is not is "the marker is GONE once it is durable": the marker comes straight from
//     `config_unsaved()`, so the mutation that would wrongly keep it lives in the SERVICE, where the native battery's
//     `C05` (the marker cleared before the write returns) already reddens it.
//   ⓘ §notify-every-save ([[B194]]) ADDS FOUR MORE, all negative space and all of P8f/P8g: "P8f ...with zero writes",
//     "P8f DISCARD clears it, onto the record leave left", "P8g a JOIN-shaped write moves no covered field, raises
//     nothing" and "P8g ...and no unsaved marker either". ★ The one worth naming is P8f's ZERO WRITES, because it
//     looks like it should redden and MUST NOT: unlike P8b's reverted write, the `leave`-shaped change is STANDING at
//     save time, so `save()`'s gate 2b re-reads and refuses it with zero writes even if the notification never
//     arrived. That is the backstop working, and it is precisely why the IMMEDIATE half needs its own positive check
//     ("shows CFG! RELOAD", reddened by C37/C38) rather than being inferred from the refusal.
//   ⓘ §CHROME-4 ADDS ONE MORE, AND IT IS THE SAME HARNESS-PRECONDITION SHAPE: "P14d precondition: a DM compose modal
//     is open over the TEAM screen". No mutation of the rail can move it — it asserts that the WALK reached the modal
//     at all, which is what makes the rail assertion beside it mean something. It can still fail: a compose modal that
//     stopped opening from TEAM trips it, and then the rail check below it would have been measuring an empty screen.
//   ⚠ AND THE 64-CHARACTER LABEL BOUND BIT FOR THE THIRD SLICE RUNNING (registered as [[B203]]): two §CHROME-4 labels
//     were written at 67 and 68 BYTES and dropped out of the reddened roll-up while their controls were turning them
//     red. Both were shortened. ⓘ It is BYTES, not characters — a `§` costs two.
//   ⚠ THE ORIGINAL RECORD OF THE SAME DEFECT: two of §UI-14's labels were written at
//     67 and 69 characters and DROPPED OUT of `run.sh`'s reddened roll-up (it parses the `%-64s` field), so both read
//     as "no control reddens" while C37/C38 were in fact turning one of them red. Both were shortened. ⇒ the bound is
//     a real constraint on the label, not a style note — §UI-14 recorded the same defect one slice earlier.

#include "mr_features.h"
#include "board_ui.h"          // the mrui:: canvas contract — IMPLEMENTED below as counting fakes
#include "mr_ui.h"             // mr_ui_init / mr_ui_tick / mr_ui_on_push — the seam under test
#include "fw_context_pure.h"   // §B105: g_hal / g_node — DEFINED here (fw_main.cpp is not in this link)
#include "firmware_commands.h" // mrfw::exec_command — faked below
#include "firmware_config.h"   // §UI-14: mrfw::device_cfg_store / device_cfg_live — the two seams, faked below
#include "iclock.h"
#include "iradio.h"
#include "command.h"
#include "firmware_ui_icons.h"  // ★ §CHROME-3: the strip's glyphs, so "the RIGHT icon" is POINTER IDENTITY rather
                                //   than "a bitmap appeared". ⓘ Pure and Arduino-free, which is why a probe may
                                //   include it without dragging the model in.
#include "inbox.h"              // §UI-7D slice B: the REAL Inbox is what these cases delete out of
#include "fixed_inbox_store.h"  //   ...backed by the same heap-free RAM ring the ESP32 board itself runs ([[B134]])
#include <Arduino.h>           // the shim: millis / Print / F() / Serial  (tools/probe_board_ui/fakes)
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>          // getenv — PROBE_LIST, the coverage roll-up switch

// ==================================================================================================================
// the scriptable device under the feature layer
// ==================================================================================================================
namespace {

// A minimal IRadio. `tx_busy` is the half of `mac_idle()` that used to force `<RadioLib.h>` into the TU — driving it
// from here is the measurement [[B105]] bought.
struct ProbeRadio : meshroute::IRadio {
    bool busy_tx = false;                 // what tx_busy() answers
    int  starts  = 0;
    meshroute::TxResult start_transmit(const uint8_t*, size_t, int16_t, int32_t, int8_t, int8_t, int16_t) override {
        ++starts; return meshroute::TxResult::ok;
    }
    bool poll_tx_done() override { return false; }
    bool tx_busy() const override { return busy_tx; }
    void abort_tx() override {}
    void set_rx_sf(int) override {}
    bool channel_busy() override { return false; }
    bool poll_rx(uint8_t*, size_t, size_t&, float&, float&) override { return false; }
};

// The canvas counters. One struct so a case can snapshot, act, and diff.
struct Canvas {
    int  init = 0, begin_frame = 0, next_page = 0, set_font = 0, draw_text = 0, draw_hline = 0;
    int  power_save = 0, button = 0, battery = 0;
    int  pages_left = 0;                  // mirrors U8g2's 8-page loop (board_ui.cpp:121-126)
    int  last_power_save = -1;
    int  draws_at_page_start = 0;         // draw_text count when the current page began
    int  min_draws_per_page = 1 << 30;    // the SMALLEST scene any page of the last frame got
    int  pages_this_frame = 0;
    // ---- §UI-9: WHAT THE TEXT ACTUALLY SAYS, not just how many draws happened ---------------------------------
    // ★ [[B104]]'s standing residue is that this probe counts draw CALLS, so it can prove a page was painted and
    //   never that the right text was on it. These two fields dent that for ONE field and no more: `first_text` is
    //   the STATUS BAR (draw_frame draws it first, on EVERY screen and even under the emergency overlay), and
    //   `page_text` is everything the frame drew. ⛔ The snapshot BUILDER and every other `draw_*` stay uncovered.
    char first_text[64] = {};             // the first string of the CURRENT frame = the status bar
    bool have_first = false;
    char page_text[2048] = {};            // every string of the current frame, '|'-separated
    size_t n_page_text = 0;
    bool init_answer  = true;             // what board_init() reports (§B91)
    // ---- §B197/§B200: the button wake, armed PER SLEEP and always disarmed --------------------------------------
    // ★ `arm_answer` is what the BOARD reports back from its ESP-IDF calls, so every arm of the caller's mapping —
    //   including the FAIL-CLOSED one — is reachable from a host. `disarm_answer` is the same for the teardown.
    // ⛔ `arm_calls` starting at 0 and STAYING 0 across `mr_ui_init()` is [[B200]]'s check: nothing may arm at boot.
    int  arm_calls    = 0;
    int  disarm_calls = 0;
    mrui::WakeArm arm_answer = mrui::WakeArm::armed;
    bool disarm_answer = true;
    bool button_down  = false;
    int32_t batt_answer = -1;             // what battery_sample_mv() hands back; <0 = unavailable (the real V3 today)
    int  bus_ops() const { return init + begin_frame + next_page + power_save; }
    // ★★★ §CHROME-3 — THE PANEL'S LATCH, MODELLED, because `bus_ops()` above counts CALLS and the real board counts
    //   COMMANDS. `variants/heltec_v3/board_ui.cpp`'s `set_power_save` returns immediately when the value has not
    //   changed ("repeat calls are GENUINE no-ops"), so the tick's per-blanked-tick `set_power_save(true)` reaches
    //   the SSD1306 exactly once, on the edge. A fake that counted every call would make §8.3.1's "zero ADDITIONAL
    //   bus calls" fail against a correct implementation — and, far worse, invite somebody to "fix" it by suppressing
    //   the edge itself. ⇒ `power_cmds` counts EDGES, which is what the panel sees.
    int  power_cmds = 0;
    int  bus_cmds() const { return init + begin_frame + next_page + power_cmds; }
    // ---- §CHROME-3: WHERE each thing was drawn, and on WHICH page --------------------------------------------------
    // ★ [[B104]]'s standing residue is that this probe counted draw CALLS. The strip is a GEOMETRY, so counting is
    //   structurally unable to measure it: a slot at the wrong x, an icon selected for the wrong state, or a battery
    //   token that moved the icons before it all leave every count identical. ⇒ every draw is recorded with its
    //   coordinates, its bytes' IDENTITY (the exact `mrui::icons::` pointer) and the page it landed on.
    static constexpr int kMaxRec = 512;
    struct Rec { int page; bool is_text; int x, y, w, h; const uint8_t* bits; char s[24]; };
    Rec rec[kMaxRec] = {};
    int n_rec = 0;
    int cur_page = 0;
    // ★★ §CHROME-4: `draw_rect` HAS A CALLER AT LAST — the navigation rail's selection frame, which §CHROME-2 and
    //    §CHROME-3 both recorded as the one thing keeping the primitive out of every shipped image. ⛔ It was `== 0`
    //    in §CHROME-3 and is now a COUNT with a required value per frame: exactly ONE on an ordinary or modal view,
    //    exactly ZERO on an emergency one (§5.3, §11.2).
    int draw_rect_calls = 0;
};
Canvas g_c;

// ---- readers over the recorded draws -------------------------------------------------------------------------------
// ⚠ `Font::small` is a 6x10 FIXED font (u8g2_font_6x10_tf), so a string's pixel width is exactly 6 columns per
//   character. That is the same arithmetic `src/firmware_ui.cpp`'s layout table derives its slots from, written out
//   here independently rather than shared with it — a bound computed by the code under test would agree with a
//   layout that had drifted.
int text_px(const char* s) { return int(strlen(s)) * 6; }

// The text drawn AT an exact slot, on a given page. `nullptr` = nothing was drawn there, which is itself an answer
// (the home and key slots are legitimately empty in some states).
const char* text_at(int x, int y, int page = 0) {
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.is_text && r.page == page && r.x == x && r.y == y) return r.s;
    }
    return nullptr;
}
// The bitmap drawn at an exact x on the strip row, by POINTER IDENTITY — so "the right glyph" is a measurement and
// not "some bitmap appeared".
const uint8_t* bitmap_at(int x, int y, int page = 0) {
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (!r.is_text && r.page == page && r.x == x && r.y == y) return r.bits;
    }
    return nullptr;
}
int bitmaps_on_page(int page = 0) {
    int n = 0;
    for (int i = 0; i < g_c.n_rec; ++i) if (!g_c.rec[i].is_text && g_c.rec[i].page == page) ++n;
    return n;
}
// ⛔ SCOPED TO THE STRIP SINCE §CHROME-4: the rail draws five more glyphs below the y = 9 rule, so a page-wide count
//   would no longer say anything about the strip's own budget. Everything at or above the rule is the strip's.
int strip_glyphs_on_page(int page = 0) {
    int n = 0;
    for (int i = 0; i < g_c.n_rec; ++i)
        if (!g_c.rec[i].is_text && g_c.rec[i].page == page && g_c.rec[i].y <= 9) ++n;
    return n;
}
// The rightmost pixel column any STRIP draw claimed (the strip is everything at or above the y = 9 rule).
// ⚠ SCOPED TO THE STRIP DELIBERATELY. ⓘ HISTORY, KEPT VISIBLE: this note used to say the 21-column BODY
//   *"legitimately over-runs 128 px today — `DELIVERED to <14-char label>` is 27 columns = 162 px and u8g2 clips it"*
//   and deferred the fix to slice 4. §CHROME-4 HAS DONE IT: the body is 19 columns at `x = 12`, that exact line was
//   split across two rows, and P14f now ASSERTS the body's extent instead of merely reporting it. The strip keeps a
//   reader of its own because it is the one region that is still 128 px wide.
int strip_max_x() {
    int m = -1;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.y > 9) continue;
        const int right = r.is_text ? (r.x + text_px(r.s) - 1) : (r.x + r.w - 1);
        if (right > m) m = right;
    }
    return m;
}
int strip_max_y() {
    int m = -1;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.y > 9) continue;
        // ⓘ A bitmap's rows run y .. y+h-1. For TEXT the bottom is the BASELINE itself, and that is a MEASUREMENT of
        //   the strip's alphabet rather than a convenience: the only characters any strip token can contain are
        //   `0-9 + - . V s m h d o l`, and not one of them descends below the baseline in `u8g2_font_6x10_tf`. ⛔ A
        //   token that ever gained a descender (`g`, `p`, `y`, `q`, `j`) would sit two rows lower and this bound
        //   would have to be re-derived rather than nudged.
        const int bottom = r.is_text ? r.y : (r.y + r.h - 1);
        if (bottom > m) m = bottom;
    }
    return m;
}
int body_max_x() {
    int m = -1;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.y <= 9) continue;
        const int right = r.is_text ? (r.x + text_px(r.s) - 1) : (r.x + r.w - 1);
        if (right > m) m = right;
    }
    return m;
}

// ================================================================================ §CHROME-4 — the rail's own readers
// ★★ THE GEOMETRY IS STATED HERE INDEPENDENTLY of the renderer's table, exactly as P13's slot coordinates are: a
//    bound imported from the code under test agrees with a layout that has drifted. Design §3.2: rail `x = 0..9`,
//    `y = 10..59`, five 10-px slots aligned to the body baselines 19/29/39/49/59.
constexpr int kRailX = 0, kRailW = 10, kRailH = 10;
constexpr int kRailSlotY[5] = { 10, 20, 30, 40, 50 };     // STATUS, TEAM, INBOX, SEND, SETTINGS — §3.2's order
constexpr int kRailIconX = 1;                              // (10 - 7) / 2
inline int rail_icon_y(int slot) { return kRailSlotY[slot] + 1; }

// Which glyph is in a rail slot, by POINTER IDENTITY. `nullptr` = the slot drew nothing, which is an ANSWER (§3.2's
// unavailable slots) rather than a failure.
const uint8_t* rail_glyph_at(int slot, int page = 0) { return bitmap_at(kRailIconX, rail_icon_y(slot), page); }

// Which slot carries the selection frame on a page: 0..4, -1 if none, -2 if MORE THAN ONE (§11.2 requires exactly
// one — a reader that returned the first would pass over a rail that boxed everything).
int rail_boxed_slot(int page = 0) {
    int found = -1;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.is_text || r.page != page || r.bits != nullptr) continue;   // `[rect]` records carry no bytes
        if (strcmp(r.s, "[rect]") != 0) continue;
        for (int sl = 0; sl < 5; ++sl)
            if (r.x == kRailX && r.y == kRailSlotY[sl] && r.w == kRailW && r.h == kRailH)
                { if (found >= 0) return -2; found = sl; }
    }
    return found;
}
int rail_frames_on_page(int page = 0) {
    int n = 0;
    for (int i = 0; i < g_c.n_rec; ++i)
        if (!g_c.rec[i].is_text && g_c.rec[i].page == page && strcmp(g_c.rec[i].s, "[rect]") == 0) ++n;
    return n;
}
// How many rail glyphs a page drew, counting only bitmaps inside the rail's column band.
int rail_glyphs_on_page(int page = 0) {
    int n = 0;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.is_text || r.page != page || r.y <= 9) continue;
        if (r.bits != nullptr && r.x == kRailIconX) ++n;
    }
    return n;
}

// ★★★ §7.1's BODY BOUNDS, MEASURED OVER THE **TEXT** RECORDS ONLY. The rail draws bitmaps and a frame at `x = 0..9`
//     with `y > 9`, so a bound taken over every record below the rule would report the rail's own x and prove
//     nothing about the body. Design §3.2: normal body origin `x = 12`, width 116 px ⇒ last usable column 127.
int body_text_min_x() {
    int m = 1 << 30;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (!r.is_text || r.y <= 9) continue;
        if (r.x < m) m = r.x;
    }
    return (m == (1 << 30)) ? -1 : m;
}
int body_text_max_x() {
    int m = -1;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (!r.is_text || r.y <= 9) continue;
        const int right = r.x + text_px(r.s) - 1;
        if (right > m) m = right;
    }
    return m;
}
// The widest body line, in COLUMNS — the figure design §7.3's audit is expressed in.
int body_max_cols() {
    int m = 0;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (!r.is_text || r.y <= 9) continue;
        const int n = int(strlen(r.s));
        if (n > m) m = n;
    }
    return m;
}
// Every page of the CURRENT frame drew the same strip — the property a FROZEN chrome delivers and a live one cannot.
// ⚠ The comparison is over the strip's records IN ORDER, including each glyph's byte POINTER: a page that drew the
//   same number of things in the same places from a newer projection would still differ in a token or a glyph.
int strip_recs_of_page(int page, const Canvas::Rec** out, int cap) {
    int n = 0;
    for (int i = 0; i < g_c.n_rec && n < cap; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.page == page && r.y <= 9) out[n++] = &r;
    }
    return n;
}
bool strip_identical_on_every_page(int pages) {
    const Canvas::Rec* a[32];
    const Canvas::Rec* b[32];
    const int na = strip_recs_of_page(0, a, 32);
    if (na == 0) return false;                       // a page with no strip at all proves nothing
    for (int p = 1; p < pages; ++p) {
        const int nb = strip_recs_of_page(p, b, 32);
        if (nb != na) return false;
        for (int i = 0; i < na; ++i)
            if (a[i]->is_text != b[i]->is_text || a[i]->x != b[i]->x || a[i]->y != b[i]->y ||
                a[i]->bits != b[i]->bits || strcmp(a[i]->s, b[i]->s) != 0) return false;
    }
    return true;
}

// The recording executor. `exec_command` is the ONE device dependency of the send path.
struct ExecLog {
    int  calls = 0;
    char last[256] = {};
    bool ok = true;
    MESHROUTE_NS::CmdCode code = MESHROUTE_NS::CmdCode::queued;
    uint16_t ctr = 1;
};
ExecLog g_exec;

}  // namespace

// ---- the canvas fakes (namespace mrui, exactly the nine `board_ui.h` declares) ------------------------------------
namespace mrui {
bool board_init()  { ++g_c.init; return g_c.init_answer; }
void begin_frame() { ++g_c.begin_frame; g_c.pages_left = 8; g_c.pages_this_frame = 0;
                     g_c.min_draws_per_page = 1 << 30; g_c.draws_at_page_start = g_c.draw_text;
                     // a new frame's text replaces the old one — never accumulates across frames, or a stale value
                     // from an earlier frame would satisfy a "the panel says X" check for ever.
                     g_c.have_first = false; g_c.first_text[0] = '\0';
                     g_c.n_page_text = 0;    g_c.page_text[0]  = '\0';
                     g_c.n_rec = 0;          g_c.cur_page      = 0; }
bool next_page()   {
    ++g_c.next_page;
    ++g_c.cur_page;   // §CHROME-3: everything recorded from here on belongs to the NEXT page's replay
    // The scene drawn since the last page boundary is this page's content. A caller that draws only at frame start
    // leaves every later page with ZERO — which is the seven-blank-pages defect (spec §5).
    const int drew = g_c.draw_text - g_c.draws_at_page_start;
    if (drew < g_c.min_draws_per_page) g_c.min_draws_per_page = drew;
    g_c.draws_at_page_start = g_c.draw_text;
    ++g_c.pages_this_frame;
    if (g_c.pages_left > 0) --g_c.pages_left;
    return g_c.pages_left > 0;
}
void set_font(Font)                    { ++g_c.set_font; }
// §CHROME-3: record the placement of every draw, on its page, before the existing text bookkeeping.
void record(bool is_text, int x, int y, int w, int h, const uint8_t* bits, const char* s) {
    if (g_c.n_rec >= Canvas::kMaxRec) return;
    Canvas::Rec& r = g_c.rec[g_c.n_rec++];
    r.page = g_c.cur_page; r.is_text = is_text;
    r.x = x; r.y = y; r.w = w; r.h = h; r.bits = bits;
    snprintf(r.s, sizeof r.s, "%s", s ? s : "");
}
void draw_text(int x, int y, const char* s) {
    record(/*is_text=*/true, x, y, 0, 0, nullptr, s);
    ++g_c.draw_text;
    if (!s) return;
    if (!g_c.have_first) { snprintf(g_c.first_text, sizeof g_c.first_text, "%s", s); g_c.have_first = true; }
    const size_t n = strlen(s);
    if (g_c.n_page_text + n + 2 < sizeof g_c.page_text) {
        memcpy(g_c.page_text + g_c.n_page_text, s, n); g_c.n_page_text += n;
        g_c.page_text[g_c.n_page_text++] = '|';
        g_c.page_text[g_c.n_page_text]   = '\0';
    }
}
void draw_hline(int, int, int)         { ++g_c.draw_hline; }
// ★★ §CHROME-3 — THE TWO §CHROME-2 PRIMITIVES, faked here for the first time because THIS SLICE IS THEIR FIRST
//    CALLER. Both are compose-only on the real board (`variants/heltec_v3/board_ui.cpp`, pure forwards to U8g2's
//    `drawXBM` / `drawFrame`) and `tools/probe_board_ui` measures that against the real TU; here they only record.
// ★★ §CHROME-4: `draw_rect` NOW HAS ITS ONLY LEGITIMATE CALLER — the rail's selection frame. The recorded
//    `[rect]` entry carries a NULL byte pointer, which is what distinguishes it from a glyph in every reader below.
void draw_bitmap(int x, int y, int w, int h, const uint8_t* bits) { record(false, x, y, w, h, bits, ""); }
void draw_rect(int x, int y, int w, int h) { ++g_c.draw_rect_calls; record(false, x, y, w, h, nullptr, "[rect]"); }
void set_power_save(bool on)           {
    ++g_c.power_save;
    // The board LATCHES (see `Canvas::power_cmds`): only a CHANGE reaches the panel.
    if (g_c.last_power_save != (on ? 1 : 0)) ++g_c.power_cmds;
    g_c.last_power_save = on ? 1 : 0;
}
bool button_pressed()                  { ++g_c.button; return g_c.button_down; }
// §B197/§B200: the REAL pair lives in variants/heltec_v3/board_ui.cpp and is measured by tools/probe_board_ui (P11 +
// its controls, including the pin re-sample and the rollback). Here they are scriptable stand-ins, because what THIS
// probe measures is what the FEATURE layer does with the answers — map all three verdicts, latch only on a HARDWARE
// failure, say each failure once, and ⛔ never arm at boot.
WakeArm arm_button_wake()              { ++g_c.arm_calls;    return g_c.arm_answer; }
bool    disarm_button_wake()           { ++g_c.disarm_calls; return g_c.disarm_answer; }
int32_t battery_sample_mv()            { ++g_c.battery; return g_c.batt_answer; }
}  // namespace mrui

// ---- §UI-14: the CONFIG-SERVICE seams' fakes ----------------------------------------------------------------------
// ★★★ THE SAME SHAPE AS `exec_command` ABOVE, AND FOR THE SAME REASON: `src/firmware_ui.cpp` constructs the ONE
//     `mrfw::ConfigService` over `mrfw::device_cfg_store()` / `device_cfg_live()`, whose real bodies live in
//     `src/firmware_config.cpp` behind `<Arduino.h>`, LittleFS/NVS and `g_ble_mode`. Faking the two ACCESSORS is what
//     lets this probe drive the feature layer's use of the service — including the failure arms, which no real store
//     on a host could produce.
// ⛔⛔ AND IT IS THE LIMIT OF WHAT THIS PROBE PROVES, stated here rather than left to be assumed: the DEVICE binding
//     ([[B193]] — the §nv-ritual load and the OFF->ON `mobile_register_current()` bridge) is NOT in this link at all.
//     Nothing here writes flash, and ⛔ no reset-during-write / power-cut behaviour is exercised. That half is a BENCH
//     check. A green run here says the SCREEN drives the service correctly, never that the storage is sound.
namespace {
struct ProbeCfgStore : mrfw::ICfgStore {
    mrnv::Blob rec{};
    bool can_load = true, can_save = true;
    int  writes = 0, loads = 0;
    ProbeCfgStore() {
        rec.magic = mrnv::kMagic; rec.version = mrnv::kVersion;
        rec.e2e_dm = 0; rec.intro_attach = 1; rec.mobile_autoregister = 0; rec.ble_mode = 0;
        rec.node_id = 42; rec.channel_ctr = 7;      // NON-covered fields: a save that dropped them is visible
    }
    bool load(mrnv::Blob& out) override { ++loads; if (!can_load) return false; out = rec; return true; }
    bool save(const mrnv::Blob& b) override { ++writes; if (!can_save) return false; rec = b; return true; }
};
struct ProbeCfgLive : mrfw::ICfgLive {
    mrfw::CfgValues eff{};
    int applies = 0;
    mrfw::CfgValues effective() const override { return eff; }
    void apply_live(const mrfw::CfgLiveFields& f) override {
        ++applies;
        eff.at(mrfw::CfgField::e2e_dm)              = f.e2e_dm ? 1 : 0;
        eff.at(mrfw::CfgField::intro_attach)        = f.intro_attach ? 1 : 0;
        eff.at(mrfw::CfgField::mobile_autoregister) = f.mobile_autoregister ? 1 : 0;
    }
};
// ⓘ Function-local statics, exactly as the device bindings are, so the OLED layer's `ConfigService` — which is
//   constructed over them at STATIC-INIT time — cannot bind a reference to an object whose construction has not run.
ProbeCfgStore& probe_store() { static ProbeCfgStore s; return s; }
ProbeCfgLive&  probe_live()  { static ProbeCfgLive  s; return s; }
}  // namespace
namespace mrfw {
ICfgStore& device_cfg_store() { return probe_store(); }
ICfgLive&  device_cfg_live()  { return probe_live(); }
}  // namespace mrfw

// ---- the executor fake --------------------------------------------------------------------------------------------
namespace mrfw {
ExecResult exec_command(const char* line, size_t len) {
    ++g_exec.calls;
    const size_t n = len < sizeof g_exec.last - 1 ? len : sizeof g_exec.last - 1;
    memcpy(g_exec.last, line, n); g_exec.last[n] = '\0';
    ExecResult r{};
    r.ok = g_exec.ok;
    r.result.code = g_exec.code;
    r.result.ctr  = g_exec.ctr;
    return r;
}
}  // namespace mrfw

// ---- the globals `firmware_ui.cpp` reads. Construction order matters: g_hal before g_node, same TU, in order. -----
namespace { meshroute::ArduinoClock g_probe_clock; ProbeRadio g_probe_radio; }
meshroute::DeviceHal g_hal(g_probe_clock, g_probe_radio);
meshroute::Node      g_node(g_hal, /*node_id=*/1, /*key_hash32=*/0x11223344u, "probe");
// §UI-7D slice B: two REAL stores, installed in P6 rather than at construction so the earlier phases keep measuring the
// unwired-inbox configuration they were written against.
namespace { meshroute::FixedInboxStore<8> g_probe_dm_store, g_probe_ch_store; }

// ==================================================================================================================
// harness
// ==================================================================================================================
namespace {
int g_pass = 0, g_fail = 0;
// PROBE_LIST=1 makes every check announce itself whether it passed or not. `run.sh` uses that as the DENOMINATOR of
// its "N of M checks are reddened by a control" roll-up, so the ratio is measured instead of restated in a comment —
// the header of this file carried a hand-maintained "20 of 25" that went stale the moment six checks were added.
const bool g_list = std::getenv("PROBE_LIST") != nullptr;
#define CHK(label, expr) do {                                                              \
    const bool ok_ = (expr);                                                               \
    if (ok_) ++g_pass; else { ++g_fail; printf("  FAIL %-64s  %s\n", (label), #expr); }    \
    if (g_list) printf("  CHECK %s\n", (label));                                           \
} while (0)

// §UI-9 text predicates. `has_voltage` looks for the shape `fmt_volts` emits for a REAL reading — digit '.' digit 'V'
// — so it can say "no voltage was invented" without knowing which one would have been. ⚠ It must not match the
// STATUS body's `batt %ldmV` (digits then "mV"), and it does not: that has no '.' before the 'V'.
bool has_voltage(const char* s) {
    for (const char* p = s; p[0] && p[1] && p[2] && p[3]; ++p)
        if (isdigit((unsigned char)p[0]) && p[1] == '.' && isdigit((unsigned char)p[2]) && p[3] == 'V') return true;
    return false;
}
// ⓘ `ends_with` LIVED HERE and is gone: its only callers were P5's three battery checks, which read the LAST FIELD of
//   the packed status bar. §CHROME-3's strip has no last field to suffix-match — the battery lives at a fixed slot —
//   so those checks now read `text_at(x, y)` and this helper had no remaining caller (-Wunused-function under the
//   -Werror this file is built with). ⛔ Removed because it became dead, never because a check was dropped.

void set_now(uint32_t ms) { g_probe_millis = ms; }
void tick(uint32_t ms)    { set_now(ms); mr_ui_tick(ms); }

// Drive whole frames: the tick paints exactly ONE page per pass, so a complete 8-page frame is 8 ticks. Time advances
// past the throttle between frames unless the caller pins it.
void run_ticks(uint32_t from_ms, int n, uint32_t step_ms) {
    for (int i = 0; i < n; ++i) tick(from_ms + uint32_t(i) * step_ms);
}

// Re-dirty the model the way the firmware itself does — an arriving push (`ui_route_recv_push` -> `mark_dirty`).
// ⚠ A DM from OUTSIDE the team, so this cannot be mistaken for the §R1 reply-wake path: it moves the unread counter
//   and nothing else, which is all these cases need.
void dirty_the_model(uint32_t now_ms) {
    set_now(now_ms);
    MESHROUTE_NS::Push pu{};
    pu.kind = MESHROUTE_NS::PushKind::msg_recv;
    pu.origin = 7; pu.sender_hash = 0xDEADBEEFu;
    pu.body[0] = 'h'; pu.body[1] = 'i'; pu.body_len = 2;
    mr_ui_on_push(pu);
}

// ★ BRING THE PANEL TO A KNOWN STATE and hand back the time it is in. Awake, no frame open, not dirty, throttle
//   expired. ⚠ This helper is NOT decoration: `UiModel::on_tick` blanks after `kBlankMs` = 15 s WITHOUT INPUT, and
//   the first version of this probe jumped 100 s between cases and then measured a dark panel — five checks failed
//   for a reason that was the harness, not the firmware. A gesture is the only thing that moves `_last_input_ms`.
// ⓘ The press is delivered as a real `short_press` through the real `InputFsm` (debounce 25 / double_gap 350), not
//   by poking the model — the whole point is that this probe drives the SHIPPED path.
// ---- §UI-7D slice B helpers -------------------------------------------------------------------------------------
// ★ Every gesture below is delivered through the REAL `InputFsm` (debounce 25, double_gap 350, arm 800), because the
//   whole point of this probe is that it drives the shipped path rather than poking the model.
uint32_t double_press(uint32_t t) {
    g_c.button_down = true;  tick(t);       tick(t + 50);       // tap 1 (well inside arm_ms, so no long_arm)
    g_c.button_down = false; tick(t + 100); tick(t + 150);
    g_c.button_down = true;  tick(t + 200); tick(t + 250);      // tap 2 -> its release is the double_press
    g_c.button_down = false; tick(t + 300); tick(t + 350);
    return t + 400;
}
// Let the panel paint one complete frame. `begin_frame` resets `page_text` and every page re-draws the WHOLE scene, so
// after this `page_text` is exactly what the panel is showing.
void paint(uint32_t t) { run_ticks(t, 10, 10); }

// A real `pull()` — the ONLY authority these cases use for "is the record still in the store". ⛔ Never the panel: a
// visual disappearance is precisely what must not be trusted as evidence of a delete.
struct LiveScan { int n = 0; bool found = false; meshroute::InboxKind kind = meshroute::InboxKind::dm; uint32_t seq = 0; };
bool live_cb(void* vctx, const meshroute::InboxEntry& e) {
    LiveScan* c = static_cast<LiveScan*>(vctx);
    ++c->n;
    if (e.kind == c->kind && e.seq == c->seq) c->found = true;
    return true;
}
int live_count() { LiveScan c{}; (void)g_node.inbox().pull(0, 0, live_cb, &c); return c.n; }
bool live_has(meshroute::InboxKind k, uint32_t seq) {
    LiveScan c{}; c.kind = k; c.seq = seq;
    (void)g_node.inbox().pull(0, 0, live_cb, &c);
    return c.found;
}

// §UI-14: press `short` until the panel SHOWS `want`, then leave it on screen. ⚠ BOUNDED and asserted by the caller,
// never assumed: if the walk never finds it, the caller's own check is what fails.
uint32_t walk_to(uint32_t t, const char* want);

uint32_t settle(uint32_t t) {
    g_c.button_down = true;  tick(t); tick(t + 50);          // stable press (debounce 25 ms)
    g_c.button_down = false; tick(t + 100);                  // release
    t += 500; tick(t);                                       // > double_gap_ms after the release -> short_press
    for (int i = 1; i <= 12; ++i) tick(t + uint32_t(i) * 10);   // let that press's frame page all the way out
    t += 700; tick(t);                                       // > kPaintThrottleMs since that paint
    return t;
}

// Walk the list until the HIGHLIGHTED row is of the wanted kind, then open it with a double press.
// ⛔⛔ THE TARGET STRING MUST NOT MATCH ANOTHER SCREEN'S ROW, and this is a MEASURED trap rather than a caution: the
//    callers used to pass `">DM"`, and §UI-14's SETTINGS menu has a row rendered `">DM crypt off"` — so the walk
//    matched the SETTINGS screen, double-pressed there, and ENTERED THE VALUE EDITOR instead of an inbox record. Every
//    later phase then measured the wrong screen. ⇒ the inbox preview row pads its kind tag to FIVE columns (§CHROME-4
//    widened it from three so `CH255` can never be truncated into a DIFFERENT channel number), so a DM row is always
//    `">DM   "` and a channel row `">CH7  "` — neither of which any other screen can produce. Pass those, never a
//    bare prefix.
// ⚠ Asserted by the caller afterwards, never assumed: if the walk never finds one, the caller's first check fails.
uint32_t walk_to(uint32_t t, const char* want) {
    for (int i = 0; i < 22; ++i) {
        paint(t);
        if (strstr(g_c.page_text, want) != nullptr) return t;
        t = settle(t + 500);
    }
    paint(t);
    return t;
}

// ★★★★ §CHROME-4 — WALK BY THE **RAIL**, because design §7.2 deleted the two titles this used to walk by.
//   `walk_to(t, "STATUS")` and `walk_to(t, "SETTINGS")` worked only while those screens carried a label-only heading;
//   the rail now names the screen and §7.2 gives the row to the content. ⇒ the screen predicate is the BOXED SLOT.
// ⚠ STATED PLAINLY: this navigates by the mechanism this slice adds, so a rail that boxed the wrong slot would send
//   the walk to the wrong screen — and every content check the caller then makes would fail. That direction is safe
//   (it makes a defect louder, never quieter); what it cannot do is stand in for a check ON the rail, which is why
//   P14 asserts the mapping directly instead of inferring it from a successful walk.
uint32_t walk_to_slot(uint32_t t, int slot) {
    for (int i = 0; i < 22; ++i) {
        paint(t);
        if (rail_boxed_slot() == slot) return t;
        t = settle(t + 500);
    }
    paint(t);
    return t;
}
constexpr int kSlotStatus = 0, kSlotTeam = 1, kSlotInbox = 2, kSlotSend = 3, kSlotSettings = 4;
// Design §3.2's normal body origin, stated here rather than imported from `src/firmware_ui.cpp`'s `kBodyX`.
constexpr int kBodyXExpected = 12;

uint32_t open_highlighted(uint32_t t, const char* want) {
    // ⚠ THE BOUND IS THE WHOLE CYCLE, WITH SLACK, AND IT IS NOT DECORATION: §UI-14 appended a fifth screen whose menu
    //   is itself list-aware, so a walk sized for the four-screen cycle stopped short and every later phase drifted
    //   onto the wrong screen (measured: 25 checks red, none of them the feature's). Bounded, so a missing row still
    //   fails the caller's check instead of looping.
    for (int i = 0; i < 28; ++i) {
        paint(t);
        if (strstr(g_c.page_text, want) != nullptr) { t = double_press(t + 500); paint(t); return t; }
        t = settle(t + 500);
    }
    return t;
}
}  // namespace

int main() {
    printf("== §B105 probe — src/firmware_ui.cpp, host-compiled ==\n");

    // ============================================================================================================ P0
    // THE BUILD ITSELF IS THE FIRST MEASUREMENT. This binary exists only because the TU stopped including
    // `fw_context.h`; `run.sh`'s first control puts that include back and requires the BUILD to fail.
    mr_ui_init();
    CHK("P0 mr_ui_init reaches the canvas exactly once", g_c.init == 1);
    // ★★★ §UI-14 follow-up, AND IT HAS TO BE MEASURED HERE — the ONLY moment in this binary when the config service
    //     has never been opened. `mr_ui_on_config_saved` guards on `is_open()` BEFORE it loads, so a `cfg set` on a
    //     node whose operator has never reached SETTINGS must cost NOTHING: no flash read, no comparison, no marker.
    //     ⚠ Once P7 opens the service it can never be closed again (that is the contract — the draft must outlive the
    //     screen), so this arm is unrepeatable later and the check would have to be deleted rather than moved.
    {
        ProbeCfgStore& st0 = probe_store();
        st0.rec.e2e_dm = 1;                       // a covered field really did move under it
        mr_ui_on_config_saved();
        CHK("P0c a config write before SETTINGS is opened reads no flash", st0.loads == 0);
        st0.rec.e2e_dm = 0;
    }
    // §B91: a panel that does not ACK is REPORTED. Nothing else in this file prints, so the sink is unambiguous.
    Serial.reset();
    g_c.init_answer = false;
    mr_ui_init();
    CHK("P0b a dead panel is reported on the console (§B91)", strstr(Serial.out, "did not ACK") != nullptr);
    g_c.init_answer = true;

    // ============================================================================================================ P1
    // RENDER POLICY — THE CALLER HALF OF ONCE-PER-PAGE. This is the exact cover [[B104]] recorded as LOST when Task 6
    // moved the hooks into an un-compilable TU: `run.sh`'s S3 could only check that a `draw_frame` call site EXISTS.
    // U8g2 CLIPS each page instead of accumulating, so a scene drawn once at frame start leaves 7 of 8 pages blank.
    uint32_t t = settle(100000);
    dirty_the_model(t);
    g_c = Canvas{};
    run_ticks(t, 8, 10);                         // 8 ticks = one complete frame
    CHK("P1 one frame opens exactly once",                 g_c.begin_frame == 1);
    CHK("P1 one frame pushes exactly 8 pages",             g_c.next_page == 8);
    CHK("P1 EVERY page got the scene re-drawn (none blank)", g_c.min_draws_per_page >= 1);
    CHK("P1 the frame completed",                          g_c.pages_this_frame == 8);
    const int per_frame_draws = g_c.draw_text;
    CHK("P1 the scene is non-trivial (>= 8 strings)",      per_frame_draws >= 8);

    // ============================================================================================================ P2
    // THE §5 MAC-IDLE GATE — the correctness constraint, not a nicety: a full frame is ~25 ms of blocking I2C against
    // a `cts_to_data_gap_ms` of 5, so painting mid-exchange DROPS RADIO FRAMES. `mac_idle()` is a TWO-clause predicate
    // and BOTH clauses are measured independently here, because dropping either one is a plausible "simplification".
    //
    // (a) the RADIO half — `g_hal.radio().tx_busy()`. Driving this is what [[B105]] made possible at all.
    t = settle(t + 2000);
    // ⛔⛔ QUIESCE FIRST, AND §CHROME-3 IS WHY — MEASURED, not defensive. `g_c = Canvas{}` resets the FAKE's page
    //    counter, so a frame still OPEN at that moment leaves the harness and the firmware disagreeing about how many
    //    pages remain, and the resume phase then measures the tail of the old frame instead of a new one. That became
    //    reachable when the strip landed: a COMPLETE, VISIBLE Inbox frame advances the read watermark, which changes
    //    the mail token, which correctly asks for ONE more paint — so `settle()` no longer reliably returns with the
    //    panel idle. ⇒ page any such frame out, then clear the 2 Hz throttle, so the phase below starts from rest.
    //    ⓘ The property P2a measures is unchanged; only the precondition is now established instead of assumed.
    paint(t);
    t += 700;
    dirty_the_model(t);
    g_c = Canvas{};
    g_probe_radio.busy_tx = true;
    run_ticks(t, 8, 10);
    CHK("P2a a TX on air suppresses EVERY canvas/bus call",  g_c.bus_ops() == 0);
    CHK("P2a ...and no drawing either",                      g_c.draw_text == 0);
    g_probe_radio.busy_tx = false;
    run_ticks(t + 700, 8, 10);
    CHK("P2a the paint RESUMES once the TX completes",       g_c.begin_frame == 1 && g_c.next_page == 8);

    // (b) the QUEUE half — `g_hal.txq_depth()`. The REAL DeviceHal queue, moved by the REAL tx(): enqueue one frame
    //     and the depth is 1 while the radio is still idle, which isolates this clause from the one above.
    t = settle(t + 2000);
    dirty_the_model(t);
    g_c = Canvas{};
    {
        const uint8_t frame[16] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
        meshroute::TxParams p; p.sf = 8;
        CHK("P2b the frame was accepted by the real queue",  g_hal.tx(frame, sizeof frame, p) == meshroute::TxResult::ok);
    }
    CHK("P2b the real DeviceHal queue is non-empty",         g_hal.txq_depth() == 1);
    CHK("P2b ...with the radio still idle",                  !g_probe_radio.busy_tx);
    run_ticks(t, 8, 10);
    CHK("P2b a queued TX alone suppresses every bus call",   g_c.bus_ops() == 0);
    g_hal.collect_tx_completion(); g_hal.pump_tx();          // §T3 §2.1: the two halves of the old service_tx()
    CHK("P2b the queue drained",                             g_hal.txq_depth() == 0);
    CHK("P2b ...and the frame really went to the radio",     g_probe_radio.starts == 1);
    run_ticks(t + 100, 8, 10);
    CHK("P2b the paint RESUMES once the queue is empty",     g_c.begin_frame == 1 && g_c.next_page == 8);

    // ============================================================================================================ P3
    // THE 2 Hz THROTTLE, as INTEGRATION. The decision itself is `FrameGate::step`, pure and natively driven; what no
    // native case can see is whether this file routes through it at all — a tick that painted unconditionally would
    // keep every native case green. `kPaintThrottleMs` = 500 ms.
    t = settle(t + 2000);
    dirty_the_model(t);
    g_c = Canvas{};
    run_ticks(t, 8, 1);                                      // frame 1 completes inside 8 ms
    CHK("P3 the first frame paints",                         g_c.begin_frame == 1);
    dirty_the_model(t + 100);                                // an arrival re-dirties -> a repaint is now WANTED
    run_ticks(t + 100, 300, 1);                              // 300 more ticks, all inside the 500 ms window
    CHK("P3 no SECOND frame opens inside the throttle",      g_c.begin_frame == 1);
    run_ticks(t + 600, 8, 1);                                // now past 500 ms since frame 1's paint
    CHK("P3 a frame opens again once the throttle expires",  g_c.begin_frame == 2);

    // ============================================================================================================ P4
    // THE BATTERY CADENCE — sampled at boot and every 30 s, ONLY while the MAC is idle, and the cadence gates on
    // ATTEMPTED rather than on SUCCEEDED. That last clause is the whole reason the code has a `s_batt_attempted` flag:
    // gating on `s_batt_mv >= 0` re-reads the ADC on EVERY idle pass for ever on a board whose reader answers
    // "unavailable" — which is every V3 today, since `battery_sample_mv()` is hardcoded `-1` until Task 9.
    // ⓘ `t + 60000` rather than a literal: it is unconditionally past whatever deadline the cases above left behind
    //   (each sample arms `now + 30 s`), so this phase does not depend on how many frames the ones above painted.
    // ⓘ The panel is dark by now (no input for a minute) and that is CORRECT and irrelevant: `battery_maybe_sample`
    //   runs BEFORE the frame gate, so the cadence is a property of the tick, not of the paint.
    t += 60000;
    g_c = Canvas{};
    g_c.batt_answer = -1;                                    // the SHIPPED V3 answer today: unavailable
    run_ticks(t, 8, 10);
    CHK("P4 sampled once on the first due pass",             g_c.battery == 1);
    // ★★ THE ATTEMPTED-vs-SUCCEEDED CLAUSE, and it is the whole reason `s_batt_attempted` exists. 300 idle ticks with
    //    the reader answering "unavailable" every time: a cadence gated on `s_batt_mv >= 0` re-reads the ADC on EVERY
    //    one of them, for ever, on every V3 built today.
    run_ticks(t + 100, 300, 10);                             // 3 s of ticks, far inside the 30 s period
    CHK("P4 NOT re-sampled inside 30 s, though every read failed", g_c.battery == 1);
    run_ticks(t + 30100, 4, 10);                             // now > 30 s after the first sample
    CHK("P4 re-sampled once the 30 s period elapses",        g_c.battery == 2);
    // The §5 MAC-idle gate applies to the ADC too — a multi-read burst must not land mid-exchange.
    const int before = g_c.battery;
    g_probe_radio.busy_tx = true;
    run_ticks(t + 90000, 8, 10);                             // long overdue, but the MAC is busy
    CHK("P4 a due sample is SUPPRESSED while the MAC is busy", g_c.battery == before);
    g_probe_radio.busy_tx = false;
    run_ticks(t + 95000, 2, 10);
    CHK("P4 ...and taken as soon as the MAC goes idle",      g_c.battery == before + 1);

    // ============================================================================================================ P5
    // ★★ WHAT THE CADENCE PUTS ON THE PANEL (plan Task 9 / slice UI-9). P4 proves the ADC is READ at the right
    //    moments; it says nothing about what the operator sees, and [[B104]]'s residue is exactly that — this probe
    //    counts draw CALLS. These checks read the STATUS STRIP's battery TOKEN, which `draw_frame` emits on every
    //    screen and under the overlay.
    // ★ THE RULED RENDER POLICY (plan Task 9 Step 3, spec §3.3, design §4.5): `3.9V` or `--`, NEVER a percentage. A
    //   percentage needs a chemistry and a discharge curve nobody has approved.
    // ⛔⛔ RETARGETED BY §CHROME-3, NOT WEAKENED — and the retarget is the point: these three used to read
    //    `first_text`, i.e. *"the first string of the frame"*, which was the packed `DM… CH… T…/… …V` bar. The strip
    //    that replaced it draws the battery token LAST and at a FIXED SLOT, so `ends_with(first_text, "3.9V")` would
    //    now be asserting the MAIL count and would pass or fail for reasons nothing to do with the battery. ⇒ they
    //    read the exact slot instead, which is strictly stronger: it pins the value AND where it landed.
    //    ⓘ §6.1's rule, applied one section over: a check is retargeted when its string moves, never deleted.
    // ⚠ ONLY the strip is asserted, deliberately: `settle()` delivers a real short press, which CYCLES the screen, so
    //   which BODY is drawn is not deterministic here.
    // ⛔ This closes ONE field of B104's residue. The snapshot BUILDER and every other `draw_*` remain uncovered.
    // §3.1's battery slot, stated INDEPENDENTLY of the renderer's own table (a bound taken from the code under test
    // would agree with a layout that had drifted): icon at x = 91, token at x = 104, both on the y = 7 baseline.
    const int kBattTextX = 104, kStripBaseY = 7;

    // (a) THE READER HAS NEVER SUCCEEDED -> `--`, and nothing may be invented in its place.
    g_c.batt_answer = -1;
    t = settle(t + 100000);
    dirty_the_model(t);
    run_ticks(t, 8, 10);
    CHK("P5 an unavailable reading renders the strip's `--`",
        text_at(kBattTextX, kStripBaseY) != nullptr && strcmp(text_at(kBattTextX, kStripBaseY), "--") == 0);
    CHK("P5 ... and NO voltage is invented anywhere",        !has_voltage(g_c.page_text));

    // (b) ONE GOOD READING REACHES THE PANEL — as volts, to one decimal, never a percentage.
    g_c.batt_answer = 3912;                                  // 3.912 V
    t += 31000; run_ticks(t, 2, 10);                         // the 30 s period has elapsed -> one (successful) sample
    t = settle(t + 1000);
    dirty_the_model(t);
    run_ticks(t, 8, 10);
    CHK("P5 a successful reading renders as volts",
        text_at(kBattTextX, kStripBaseY) != nullptr && strcmp(text_at(kBattTextX, kStripBaseY), "3.9V") == 0);
    CHK("P5 ... and never as a percentage",                  strchr(g_c.page_text, '%') == nullptr);

    // (c) ★ SPEC §7's LAST-GOOD RULE, MEASURED. A later UNAVAILABLE read must NOT erase the value already displayed
    //     (`if (mv >= 0) s_batt_mv = mv;`). ⚠ THE CONSEQUENCE IS REAL AND IS REPORTED RATHER THAN SMOOTHED: after one
    //     good sample, a reader that dies keeps a STALE voltage on the panel indefinitely. That is what §7 says
    //     ("keeps the last good value") and what the shipped code does; whether it should is the owner's call, not
    //     this slice's. Until the FIRST success the field is `--`, which (a) pins.
    g_c.batt_answer = -1;
    const int b4 = g_c.battery;
    t += 31000; run_ticks(t, 2, 10);
    CHK("P5 the cadence DID re-attempt after the good one",  g_c.battery > b4);
    t = settle(t + 1000);
    dirty_the_model(t);
    run_ticks(t, 8, 10);
    CHK("P5 an unavailable read does not erase the last good value",
        text_at(kBattTextX, kStripBaseY) != nullptr && strcmp(text_at(kBattTextX, kStripBaseY), "3.9V") == 0);

    // ============================================================================================================ P6
    // ★★★★ §UI-7D slice B — THE INBOX DETAIL MODAL, END TO END, AGAINST A REAL `meshroute::Inbox`. This is the only
    //     instrument in the tree that exercises the whole chain: a real button press -> the real `InputFsm` -> the model's
    //     identity tracking -> `firmware_ui.cpp`'s `(kind, seq)` lookup over the real `pull()` -> the real
    //     `Inbox::erase()` -> the panel. The native suite drives the model with a hand-built snapshot; nothing there can
    //     see whether THIS file looks the record up by the right pair, copies the body while the pointer is alive, or
    //     passes the three erase outcomes through.
    // ★★ THE AUTHORITY FOR "DELETED" IS A REAL `pull()`, NEVER THE PANEL. Spec §3.5 forbids a visual disappearance
    //    without durable success, so a check that read the screen would be asserting the one thing that may not be
    //    trusted as evidence.
    // ★★★ THE FIXTURE IS BUILT FOR ONE DEFECT IN PARTICULAR: three DMs and three channel posts, so BOTH stores hold
    //     seq 1, 2 and 3. The two sequence spaces are independent, so a lookup or an erase that drops the KIND resolves
    //     to the other store's record with the same number — [[B133]] was exactly that. ⓘ And the CHANNEL record is
    //     opened FIRST, while its same-numbered DM is still live: `pull()` streams the DM block before the channel block,
    //     so a `seq`-only lookup is INDISTINGUISHABLE from a correct one whenever the target is a DM. Ordering the
    //     phases this way is what makes that control able to fail at all.
    // ⚠ [[B134]] IS RESPECTED IN THE WORDING: the store here is the same volatile RAM ring the ESP32 board runs, so what
    //   is measured is that the record is gone from every future pull IN THIS RUNTIME. ⛔ No power-loss claim is made or
    //   available — and ⛔⛔ a cross-reboot check would be VACUOUS rather than merely absent: on that store a reboot
    //   destroys the record, its tombstone and the whole history alike, so "still deleted" would pass for the wrong
    //   reason. That is why nothing here simulates one.
    g_probe_dm_store.set_epoch(1); g_probe_ch_store.set_epoch(1);
    g_node.inbox().on_init(&g_probe_dm_store, &g_probe_ch_store);
    CHK("P6 the probe's real inbox is wired", g_node.inbox().enabled());
    {
        const uint8_t d1[] = { 'd', 'm', '-', 'o', 'n', 'e' };
        const uint8_t c1[] = { 'c', 'h', '-', 'o', 'n', 'e' };
        for (uint16_t i = 1; i <= 3; ++i) {
            CHK("P6 a DM is recorded",      g_node.inbox().record_dm(48, 0, i, 0, d1, sizeof d1, 1000) == i);
            CHK("P6 a channel post is recorded",
                g_node.inbox().record_channel(7, 0x01020300u + i, 0, c1, sizeof c1, 1000) == i);
        }
    }
    CHK("P6 six live records to browse, seq 1..3 in BOTH stores", live_count() == 6);

    // Walk to the INBOX screen with real presses. Asserted rather than counted, so a screen-order change cannot
    // silently retarget everything below it.
    // ⚠ THE WALK IS THE SHARED BOUNDED HELPER, not a hand-sized loop. It used to be `for (i < 6)`, which was exactly
    //   the four-screen cycle plus slack — and §UI-14's fifth screen made it stop short, so P6a failed and every later
    //   phase drifted onto the wrong screen (MEASURED: 25 red checks, none of them about the inbox). A walk sized by
    //   hand to today's cycle is a walk that breaks on the next slice.
    t = walk_to(t + 2000, "INBOX");
    CHK("P6a the INBOX screen is reachable by pressing",   strstr(g_c.page_text, "INBOX") != nullptr);
    CHK("P6a ...and it lists both kinds",                  strstr(g_c.page_text, "DM ") != nullptr &&
                                                           strstr(g_c.page_text, "CH7") != nullptr);

    // ---- (a) A CHANNEL record, opened while its same-numbered DM is still live -------------------------------------
    t = open_highlighted(t + 500, ">CH7 ");
    CHK("P6b a double opens the CHANNEL record's modal",    strstr(g_c.page_text, "CH7 from") != nullptr);
    CHK("P6b ...showing that record's own body",           strstr(g_c.page_text, "ch-one") != nullptr);
    CHK("P6b ...with `back` selected, never `delete`",     strstr(g_c.page_text, ">back") != nullptr &&
                                                           strstr(g_c.page_text, ">delete") == nullptr);
    CHK("P6b ...and the page indicator reads 1/1",         strstr(g_c.page_text, "1/1") != nullptr);
    CHK("P6b opening DELETED NOTHING",                     live_count() == 6);

    // ---- (b) `back` CHANGES NOTHING IN STORAGE — asserted at the STORE, not on the screen --------------------------
    t = double_press(t + 500); paint(t);
    CHK("P6c `back` closes the modal",                     strstr(g_c.page_text, ">back") == nullptr &&
                                                           strstr(g_c.page_text, "INBOX") != nullptr);
    CHK("P6c ...and left all six records in the store",    live_count() == 6);
    CHK("P6c ...including the one that was open",          live_has(meshroute::InboxKind::channel, 1));

    // ---- (c) THE DELIBERATE SEQUENCE on a channel record: open, short, double -------------------------------------
    t = open_highlighted(t + 500, ">CH7 ");
    CHK("P6d the channel modal is open again",             strstr(g_c.page_text, "CH7 from") != nullptr);
    t = settle(t + 500); paint(t);                         // one SHORT press -> the action toggles
    CHK("P6d a short press selects `delete`",              strstr(g_c.page_text, ">delete") != nullptr &&
                                                           strstr(g_c.page_text, ">back") == nullptr);
    CHK("P6d ...and still nothing has been deleted",       live_count() == 6);
    t = double_press(t + 500); paint(t);
    CHK("P6d the channel record is GONE from a real pull", !live_has(meshroute::InboxKind::channel, 1));
    CHK("P6d ...exactly one record was removed",           live_count() == 5);
    CHK("P6d ★ the DM with the SAME seq survived",         live_has(meshroute::InboxKind::dm, 1));
    CHK("P6d ...and so did the other channel posts",       live_has(meshroute::InboxKind::channel, 2) &&
                                                           live_has(meshroute::InboxKind::channel, 3));
    CHK("P6d the modal closed back to the list",           strstr(g_c.page_text, "INBOX") != nullptr &&
                                                           strstr(g_c.page_text, ">delete") == nullptr);

    // ---- (d) THE SAME on a DM, so neither store is assumed symmetric with the other -------------------------------
    t = open_highlighted(t + 500, ">DM  ");
    CHK("P6e a DM record opens with the DM header",        strstr(g_c.page_text, "DM from 48") != nullptr);
    CHK("P6e ...and its own body",                         strstr(g_c.page_text, "dm-one") != nullptr);
    t = settle(t + 500); paint(t);
    t = double_press(t + 500); paint(t);
    CHK("P6e the DM is GONE from a real pull",             !live_has(meshroute::InboxKind::dm, 1));
    CHK("P6e ...exactly one record was removed",           live_count() == 4);
    CHK("P6e ★ the channel posts are untouched",           live_has(meshroute::InboxKind::channel, 2) &&
                                                           live_has(meshroute::InboxKind::channel, 3));

    // ---- (e) A RECORD REMOVED BEHIND THE UI'S BACK. The console verb `del_msg` does exactly this between two frames.
    //          The list is rebuilt from the store every tick, so the selection is dropped and the activation REFUSED.
    CHK("P6f removing a record out of band succeeds",
        g_node.inbox().erase(meshroute::InboxKind::dm, 2) == meshroute::InboxEraseResult::erased);
    const int live_after_oob = live_count();
    t = double_press(t + 500); paint(t);
    CHK("P6f a vanished record REFUSES with MESSAGE GONE", strstr(g_c.page_text, "MESSAGE GONE") != nullptr);
    CHK("P6f ...opens no modal",                           strstr(g_c.page_text, ">back") == nullptr);
    // ★ THE SAFETY HALF (§B64's rule, one plane over): while the refusal stands the `>` marker is SUPPRESSED. A
    //   highlight beside a record the model has already refused to act on is the same wrong in display form — and it is
    //   two presses from a Delete.
    CHK("P6f ...and the list's highlight is suppressed",   strstr(g_c.page_text, ">DM  ") == nullptr &&
                                                           strstr(g_c.page_text, ">CH7 ") == nullptr);
    CHK("P6f ...and deletes nothing else",                 live_count() == live_after_oob);

    // ---- (f) THE `not_found` DELETE OUTCOME, END TO END: the record is evicted WHILE THE MODAL IS OPEN, so the erase
    //          the user then confirms comes back `not_found`. ⛔ The modal must say MESSAGE GONE and must NOT read as a
    //          success — "a visual disappearance without durable success is forbidden" is precisely this path.
    t = open_highlighted(t + 500, ">CH7 ");
    CHK("P6g a channel record is open",                    strstr(g_c.page_text, "CH7 from") != nullptr);
    {
        // remove whichever channel record is open, out of band, then confirm the delete from the modal
        const bool had2 = live_has(meshroute::InboxKind::channel, 2);
        const uint32_t victim = had2 ? 2u : 3u;
        CHK("P6g the open record is removed out of band",
            g_node.inbox().erase(meshroute::InboxKind::channel, victim) == meshroute::InboxEraseResult::erased);
    }
    const int live_before_confirm = live_count();
    t = settle(t + 500); paint(t);                         // select `delete`
    t = double_press(t + 500); paint(t);                   // ...and confirm it
    CHK("P6g the modal reports MESSAGE GONE",              strstr(g_c.page_text, "MESSAGE GONE") != nullptr);
    CHK("P6g ...and says why, rather than implying success", strstr(g_c.page_text, "evicted or deleted") != nullptr);
    CHK("P6g ...and NOTHING further was deleted",          live_count() == live_before_confirm);
    t = double_press(t + 500); paint(t);                   // either press returns to the rebuilt list
    // ⓘ MEASURED, and it is the right behaviour rather than a leak: the rebuilt LIST also carries `MESSAGE GONE`, because
    //   the selection it was tracking is likewise gone from the store. What distinguishes the list from the modal is the
    //   modal's own second line — so THAT is what must have disappeared.
    CHK("P6g a press returns to the rebuilt INBOX",        strstr(g_c.page_text, "INBOX") != nullptr &&
                                                           strstr(g_c.page_text, "evicted or deleted") == nullptr &&
                                                           strstr(g_c.page_text, ">back") == nullptr);

    // ============================================================================================================ P7
    // ★★★★ §UI-14 — THE SETTINGS SCREEN, END TO END, THROUGH THE SHIPPED PATH. The native suite drives the pure model
    //     against the pure service; what NOTHING there can see is whether THIS file renders the row it is highlighting,
    //     shows the DRAFT's value rather than the effective one, puts the draft marker on STATUS, and freezes the
    //     service's three facts at the frame instead of reading them live.
    // ★★ THE AUTHORITY FOR "SAVED" IS THE FAKE STORE, NEVER THE PANEL — the §UI-7D rule one screen over: a visual
    //    claim is exactly what may not be trusted as evidence that something durable happened.
    // ⛔ AND THE STORE IS A FAKE. No flash, no wear, no power-cut ([[B193]]); that half is a bench check.
    {
        ProbeCfgStore& st = probe_store();
        ProbeCfgLive&  lv = probe_live();
        lv.eff = mrfw::cfg_values_from_blob(st.rec);          // a freshly booted node: effective == persisted
        t = walk_to_slot(t + 2000, kSlotSettings);
        CHK("P7 the SETTINGS screen is reachable by pressing",  rail_boxed_slot() == kSlotSettings);
        CHK("P7 ...and it lists a covered field with its value", strstr(g_c.page_text, "DM crypt") != nullptr);
        CHK("P7 ...opening it wrote NOTHING",                    st.writes == 0);
        CHK("P7 ...and applied NOTHING live",                    lv.applies == 0);
    // ★★ SPEC §3.6.2's CONDITIONAL ROW, MEASURED IN BOTH ARMS — and the same source file asserts both, so neither arm
    //    can rot unnoticed. `run.sh` builds this file AND `firmware_ui.cpp` a second time with `-DMR_UI_BLE_ROW=1`.
#if MR_UI_BLE_ROW
        t = walk_to(t + 500, ">BLE");
        CHK("P7 the BLE row IS rendered when the transport condition is met",
            strstr(g_c.page_text, "BLE") != nullptr);
#else
        // Walk the WHOLE menu once and require the row to appear on none of its frames — a single frame shows only
        // three rows, so checking one would prove nothing.
        {
            bool seen_ble = false;
            for (int i = 0; i < 10; ++i) { paint(t); if (strstr(g_c.page_text, "BLE")) seen_ble = true; t = settle(t + 500); }
            // ⚠ THE LABEL IS UNDER 64 CHARACTERS ON PURPOSE: `run.sh`'s coverage roll-up parses `%-64s`, so a longer
            //   one silently drops out of the "N of M reddened" denominator — measured on this very check.
            CHK("P7 the BLE row is ABSENT (no UI-12 transport in any env)", !seen_ble);
        }
        t = walk_to_slot(t + 500, kSlotSettings);
#endif
        // ---- the EDITOR: `double` enters, `short` cycles the DRAFT, `double` accepts -------------------------------
        t = walk_to(t + 500, ">DM crypt");
        CHK("P7a the value row can be highlighted",             strstr(g_c.page_text, ">DM crypt") != nullptr);
        printf("DBG PAGE=[%s]\n", g_c.page_text);
        CHK("P7a ...and shows the persisted value",             strstr(g_c.page_text, "DM crypt off") != nullptr);
        t = double_press(t + 500); paint(t);
        CHK("P7a a double ENTERS the editor (the value is bracketed)",
            strstr(g_c.page_text, "[off]") != nullptr);
        t = settle(t + 500); paint(t);
        CHK("P7a a short press CYCLES the value while editing", strstr(g_c.page_text, "[on]") != nullptr);
        CHK("P7a ...in the RAM DRAFT ONLY — no durable write",  st.writes == 0);
        CHK("P7a ...and no live apply",                         lv.applies == 0);
        CHK("P7a ...the persisted record is untouched",         st.rec.e2e_dm == 0);
        t = double_press(t + 500); paint(t);
        CHK("P7a a double ACCEPTS and leaves the editor",       strstr(g_c.page_text, "[on]") == nullptr &&
                                                                strstr(g_c.page_text, "DM crypt on") != nullptr);
        // ---- THE DRAFT STATE, NOW ON THE RAIL'S SETTINGS BADGE ------------------------------------------------
        // ⛔⛔ RETARGETED BY §CHROME-4 / design §6.1, AND THE RETARGETING IS THE POINT. These checks read
        //   `CFG* UNSAVED` off the STATUS TITLE, which design §6 removes: *"the redundant `CFG* UNSAVED` /
        //   `CFG! RELOAD` decoration is removed from the STATUS title. The rail makes the state visible from every
        //   ordinary screen."* ⛔ THE FACT IS NOT DROPPED — it MOVED, so the coverage moves with it, onto the
        //   SETTINGS rail icon's BADGE. ⓘ The ACTIONABLE text is still measured, on the screen §6 requires it on:
        //   see P8b/P8c/P8f, which read `CFG! RELOAD` off SETTINGS, and P14g's `UNSAVED` check.
        // ★ AND THE TRANSITION IS PROVABLE IN BOTH DIRECTIONS (§6.1 rule 4): the badge check below requires the new
        //   presentation, and the companion check requires the OLD one to be ABSENT from the STATUS body — so a
        //   renderer cannot satisfy both the old and the new answer.
        t = walk_to_slot(t + 500, kSlotStatus);
        CHK("P7b the SETTINGS rail badge carries the unsaved state",
            rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsUnsaved);
        CHK("P7b ...and the STATUS body no longer carries the withdrawn marker TEXT",
            strstr(g_c.page_text, "CFG* UNSAVED") == nullptr && strstr(g_c.page_text, "CFG! RELOAD") == nullptr);
        CHK("P7b ...and it is NOT the word `dirty` in any form", strstr(g_c.page_text, "dirty") == nullptr);
        // ---- SAVE ------------------------------------------------------------------------------------------------
        t = walk_to(t + 500, ">SAVE");
        CHK("P7c the SAVE row can be highlighted",              strstr(g_c.page_text, ">SAVE") != nullptr);
        t = double_press(t + 500); paint(t);
        CHK("P7c the panel says SAVED",                         strstr(g_c.page_text, "SAVED") != nullptr);
        CHK("P7c ...and the STORE says so too: EXACTLY one write", st.writes == 1);
        CHK("P7c ...with the covered field written",            st.rec.e2e_dm == 1);
        CHK("P7c ...and the NON-covered fields carried through", st.rec.node_id == 42 && st.rec.channel_ctr == 7u);
        CHK("P7c ...the live half applied, once",               lv.applies == 1);
        CHK("P7c ...and it applied the SAVED value",            lv.eff.at(mrfw::CfgField::e2e_dm) == 1);
        t = walk_to_slot(t + 500, kSlotStatus);
        CHK("P7c the unsaved badge is GONE once it is durable",  rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettings);
        CHK("P7c ...and no RESTART is claimed for a live field", strstr(g_c.page_text, "RESTART NEEDED") == nullptr);
        // ---- A FAILED WRITE: the panel must say so, and the marker must SURVIVE -----------------------------------
        t = walk_to(t + 500, ">key attach");
        t = double_press(t + 500); paint(t);                    // enter
        t = settle(t + 500);                                    // cycle 1 -> 0
        t = double_press(t + 500); paint(t);                    // accept
        st.can_save = false;
        const int writes_before = st.writes;
        t = walk_to(t + 500, ">SAVE");
        t = double_press(t + 500); paint(t);
        CHK("P7d a failed durable write says SAVE FAILED",      strstr(g_c.page_text, "SAVE FAILED") != nullptr);
        CHK("P7d ...it was ATTEMPTED",                          st.writes == writes_before + 1);
        CHK("P7d ...and changed nothing",                       st.rec.intro_attach == 1);
        CHK("P7d ...nothing was applied live",                  lv.applies == 1);
        CHK("P7d ...and the DRAFT BADGE SURVIVES the failure",  (t = walk_to_slot(t + 500, kSlotStatus),
                                                                 rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsUnsaved));
        // ---- BACK preserves it, and a REBOOT-CLASS save shows the third literal -----------------------------------
        st.can_save = true;
        t = walk_to(t + 500, ">DISCARD");
        t = double_press(t + 500); paint(t);
        CHK("P7e DISCARD clears the marker without writing",    st.writes == writes_before + 1);
        t = walk_to_slot(t + 500, kSlotStatus);
        CHK("P7e ...and the badge is clean again",              rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettings);
        // A reboot-class difference is produced the way a real one is: the EFFECTIVE `ble_mode` differs from what is
        // persisted, which is exactly the state a saved-but-not-rebooted node is in.
        // ⓘ MEASURED AND STATED, because the first version of this check failed for the right reason: poking the LIVE
        //   sink from outside changes a fact the MODEL never saw, so nothing marked the frame dirty and the panel kept
        //   the previous image. On device that cannot happen — `reboot_required` only becomes true at a SAVE (which
        //   marks dirty) or at boot — so the press below is what a real operator supplies, not a workaround.
        lv.eff.at(mrfw::CfgField::ble_mode) = 1;
        t = settle(t + 500);
        t = walk_to_slot(t + 500, kSlotStatus);
        CHK("P7e a reboot-class difference renders RESTART NEEDED",
            strstr(g_c.page_text, "RESTART NEEDED") != nullptr);
        // ★★ §6's PRIORITY, THROUGH THE SHIPPED PATH: a durable save that needs a reboot is NO LONGER unsaved, so the
        //    badge must be the RESTART one — ⛔ not the unsaved one, and not the clean gear either.
        CHK("P7e ...and the badge is RESTART, not unsaved",     rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsRestart);
        lv.eff.at(mrfw::CfgField::ble_mode) = 0;

        // ======================================================================================================= P8
        // ★★★★ §UI-14 follow-up — THE IMMEDIATE EXTERNAL-WRITE NOTIFICATION (spec §3.6.1), which is `mr_ui_on_config_saved`.
        //     `handle_cfg_set` calls it after a successful persisted `/mrcfg` write; here the probe plays that part,
        //     because `firmware_config.cpp` is not in this link. ⇒ what IS measured here is everything the hook's own
        //     body must do; what is NOT is the CALL SITE, which is `tools/probe_board_ui/`'s W12/W13 (four + one
        //     controls) because no host build compiles `handle_cfg_set`.
        // ★★ THE REPAINT IS PART OF THE PROPERTY, NOT A DETAIL: `FrameGate::step` returns `idle` while the model is
        //    clean, so a latch raised without `mark_dirty()` would be TRUE AND INVISIBLE. Every check below therefore
        //    reads the PANEL after the hook and WITHOUT any button press — a press would repaint anyway and the check
        //    would pass on the broken code.
        {
            // (a) IMMEDIATE — a covered field moves under an open draft, and the panel says so with no input at all.
            const int loads_before = st.loads;
            st.rec.intro_attach = 0;                       // the companion's write (the record is already updated)
            mr_ui_on_config_saved();
            CHK("P8a the hook re-read the record",              st.loads > loads_before);
            t += 700; paint(t);                            // NO gesture: the repaint must come from the hook alone
            // ⓘ THE PANEL IS ON **STATUS** HERE (P7e left it there and no gesture has been made since), which is
            //   exactly why this one reads the BADGE: §6 moved the compact indicator to the rail so it is visible
            //   from every ordinary screen. The ACTIONABLE text is asserted on SETTINGS by P8b/P8f below.
            CHK("P8a a covered external write shows the conflict badge at once",
                rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsConflict);
            CHK("P8a ...and the RELOAD row is offered",         (t = walk_to(t + 500, ">RELOAD"),
                                                                strstr(g_c.page_text, ">RELOAD") != nullptr));
            // (b) the CHANGE -> REVERT case the SAVE-time byte comparison cannot catch: the record goes back, so the
            //     bytes match the baseline again — and the latch must SURVIVE that, or the save proceeds.
            st.rec.intro_attach = 1;
            mr_ui_on_config_saved();
            const int writes_before2 = st.writes;
            t = walk_to(t + 500, ">SAVE");
            t = double_press(t + 500); paint(t);
            CHK("P8b a reverted external write STILL refuses the save",
                strstr(g_c.page_text, "CFG! RELOAD") != nullptr);
            CHK("P8b ...with zero writes",                      st.writes == writes_before2);
            t = walk_to(t + 500, ">DISCARD");
            t = double_press(t + 500); paint(t);           // the ruled way out
            t += 700; paint(t);
            CHK("P8b DISCARD clears it",                        strstr(g_c.page_text, "CFG! RELOAD") == nullptr);
            // (c) a NON-COVERED write raises NOTHING — the negative half, and it is structural: the hook extracts
            //     only the four covered fields, so a leased counter or an identity cannot reach the marker.
            st.rec.channel_ctr = 999;
            st.rec.node_id     = 123;
            mr_ui_on_config_saved();
            t += 700; paint(t);
            CHK("P8c a NON-covered external write raises no conflict",
                strstr(g_c.page_text, "CFG! RELOAD") == nullptr);
            CHK("P8c ...and no unsaved marker either",          strstr(g_c.page_text, "CFG* UNSAVED") == nullptr);
            // (d) NOTHING CHANGED -> nothing claimed. The hook must not invent a conflict from being called.
            mr_ui_on_config_saved();
            mr_ui_on_config_saved();
            t += 700; paint(t);
            CHK("P8d repeated notifications with no change claim nothing",
                strstr(g_c.page_text, "CFG! RELOAD") == nullptr);
            // (e) an UNREADABLE record is not a conflict either — it says nothing about whether the fields moved, and
            //     the SAVE-time gate still re-reads. ⛔ Inventing a latch here would refuse a legitimate save.
            st.can_load = false;
            mr_ui_on_config_saved();
            st.can_load = true;
            t += 700; paint(t);
            CHK("P8e an unreadable record is not treated as a conflict",
                strstr(g_c.page_text, "CFG! RELOAD") == nullptr);
            // (f) ★★★ §notify-every-save / [[B194]] — THE `leave` SHAPE, which is the largest covered-field change any
            //     verb makes: `handle_leave` rebuilds the record from a zeroed `mrnv::Blob` and persists it, so ALL
            //     FOUR covered fields land at 0 under whatever draft is open. Before this slice that write notified
            //     nothing. ⓘ The CALL SITE is `tools/probe_board_ui/`'s W18 — `firmware_config.cpp` is not in this link.
            t = walk_to(t + 500, ">key attach");
            t = double_press(t + 500); paint(t);           // enter the editor
            t = settle(t + 500);                           // cycle the DRAFT (intro_attach 1 -> 0)
            t = double_press(t + 500); paint(t);           // accept
            t = walk_to_slot(t + 500, kSlotStatus);
            CHK("P8f a covered field is edited, so the draft badge stands",
                rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsUnsaved);
            t = walk_to_slot(t + 500, kSlotSettings);
            const int writes_before3 = st.writes;
            st.rec.e2e_dm = 0; st.rec.intro_attach = 0; st.rec.mobile_autoregister = 0; st.rec.ble_mode = 0;
            mr_ui_on_config_saved();
            t += 700; paint(t);                            // NO gesture: the repaint must come from the hook alone
            CHK("P8f a LEAVE-shaped write (all four reset) shows CFG! RELOAD",
                strstr(g_c.page_text, "CFG! RELOAD") != nullptr);
            t = walk_to(t + 500, ">SAVE");
            t = double_press(t + 500); paint(t);
            CHK("P8f ...and the SAVE over the wiped record is REFUSED",
                strstr(g_c.page_text, "CFG! RELOAD") != nullptr);
            CHK("P8f ...with zero writes",                  st.writes == writes_before3);
            t = walk_to(t + 500, ">DISCARD");
            t = double_press(t + 500); paint(t);
            t += 700; paint(t);
            CHK("P8f DISCARD clears it, onto the record leave left",
                strstr(g_c.page_text, "CFG! RELOAD") == nullptr);
            // (g) ⛔ THE NEGATIVE HALF OF THE SYSTEMATIC RULE, and it is what makes "notify on EVERY user-initiated
            //     save" defensible rather than merely loud: `join` persists `/mrcfg` and now notifies too, but it
            //     assigns NONE of the four covered fields — so the notification must raise NOTHING AT ALL.
            st.rec.freq_mhz = 869.525; st.rec.bw_hz = 125000; st.rec.routing_sf = 9;
            st.rec.leaf_id = 3; st.rec.layer0_id = 3;
            st.rec.node_id = 0; st.rec.joined = 0; st.rec.lineage_id = 0; st.rec.config_epoch = 0;
            st.rec.leaf_name_len = 0;
            mr_ui_on_config_saved();
            t += 700; paint(t);
            CHK("P8g a JOIN-shaped write moves no covered field, raises nothing",
                strstr(g_c.page_text, "CFG! RELOAD") == nullptr);
            t = walk_to_slot(t + 500, kSlotStatus);
            CHK("P8g ...and no unsaved badge either",       rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettings);
        }
    }

    // ============================================================================================================ P9
    // ★★★★ §T3 — WHAT THE OPERATOR ACTUALLY READS, END TO END THROUGH `mr_ui_on_push`.
    // The panel is the whole point of this slice: until now `SENT, waiting` appeared at CORE ADMISSION, which is five
    // measured gaps short of the air. These cases drive the SHIPPED path — the real `firmware_ui.cpp`, the real
    // `SendTracker`, the real `UiModel`, and `mr_ui_on_push` (the exact seam `fw_main` calls) — and read the panel's
    // own bytes. ⓘ The design numbers these P1-P7; this file's P-slots are already taken, so they land as P9a-P9f and
    // the mapping is stated here rather than left to be guessed: P9a=P1(first half), P9b=P1(second half), P9c=P7,
    // P9d=P1 for the DM plane, P9e=P6, P9f=P4.
    // ⛔ WHAT THIS DOES **NOT** PROVE, stated rather than implied: the CORE's production of the push (its ownership
    //    predicate, the `flood` clause, the 16-bit handle) is native-only cover — `mrfw::exec_command` is faked here,
    //    so no real origination happens in this binary. §T3's N14a-e are those assertions; these measure the app half.
    {
        auto aired_push = [](uint8_t dst, uint16_t ctr) {
            MESHROUTE_NS::Push pu{}; pu.kind = MESHROUTE_NS::PushKind::send_aired;
            pu.dst = dst; pu.ctr = ctr; return pu;
        };
        // ---- ★ A CANNED TEAM POST, sent through the real SEND screen with the real gestures.
        g_exec = ExecLog{}; g_exec.ok = true;
        g_exec.code = MESHROUTE_NS::CmdCode::queued;
        g_exec.ctr  = 300;                                   // ★ ABOVE 255 on purpose — §b40's 16-bit handle
        uint32_t t9 = settle(400000);
        t9 = open_highlighted(t9, "SEND to team");            // the SEND screen -> the canned CHANNEL list
        t9 = double_press(t9 + 500); paint(t9);               // ...and send its first text
        CHK("P9a the canned post really reached the executor", g_exec.calls == 1);
        CHK("P9a an ACCEPTED post reads QUEUED, never SENT",
            strstr(g_c.page_text, "QUEUED") != nullptr && strstr(g_c.page_text, "SENT, waiting") == nullptr);

        // ---- ★★★★ THE COMPLETION. This is the fact §T3 exists to deliver, arriving through the core's own app
        //      channel exactly as `fw_main`'s push drain delivers it.
        mr_ui_on_push(aired_push(/*dst=*/0, /*ctr=*/300));
        t9 += 700; paint(t9);
        CHK("P9b the correlated airing turns QUEUED into SENT, waiting",
            strstr(g_c.page_text, "SENT, waiting") != nullptr && strstr(g_c.page_text, "QUEUED") == nullptr);

        // ---- ⛔ P9c (design P7): an UNCORRELATED airing moves NOTHING. Re-send so a fresh QUEUED is on screen.
        g_exec = ExecLog{}; g_exec.ok = true; g_exec.code = MESHROUTE_NS::CmdCode::queued; g_exec.ctr = 301;
        t9 = settle(t9 + 1000);
        t9 = open_highlighted(t9, "SEND to team");
        t9 = double_press(t9 + 500); paint(t9);
        CHK("P9c precondition: the new post is QUEUED",  strstr(g_c.page_text, "QUEUED") != nullptr);
        mr_ui_on_push(aired_push(/*dst=*/0, /*ctr=*/45));     // ⛔ 301 & 0xff == 45: the TRUNCATED handle
        t9 += 700; paint(t9);
        CHK("P9c a truncated/foreign handle leaves the panel on QUEUED",
            strstr(g_c.page_text, "QUEUED") != nullptr && strstr(g_c.page_text, "SENT, waiting") == nullptr);
        // ---- ⛔ P9f (design P4): a push of an UNRELATED kind moves neither new state.
        MESHROUTE_NS::Push other{}; other.kind = MESHROUTE_NS::PushKind::send_acked; other.dst = 0; other.ctr = 301;
        mr_ui_on_push(other);
        t9 += 700; paint(t9);
        CHK("P9f an unrelated push kind moves neither new state",
            strstr(g_c.page_text, "QUEUED") != nullptr && strstr(g_c.page_text, "SENT, waiting") == nullptr);
        // ...and the CORRECT handle still works, so the two refusals above are the correlation and not an inert panel.
        mr_ui_on_push(aired_push(0, 301));
        t9 += 700; paint(t9);
        CHK("P9c ...and the exact 16-bit handle still promotes it",
            strstr(g_c.page_text, "SENT, waiting") != nullptr);

        // ---- ★ P9e (design P6): the renamed no-relay string, ON THE PANEL. The retired wording kept the word SENT
        //      on a state reached with no airing evidence at all — the same contradiction the two lines above remove.
        // ⚠ THE NEEDLE IS ASSEMBLED AT RUNTIME ON PURPOSE: `run.sh`'s P6 grep asserts the retired literal appears in
        //   NO `src/` or `tools/` source, and a check that spelled it out here would match ITSELF and make that gate
        //   permanently red for the wrong reason.
        char retired[16]; snprintf(retired, sizeof retired, "SENT, %s relay", "no");
        MESHROUTE_NS::Push nr{}; nr.kind = MESHROUTE_NS::PushKind::channel_sent; nr.ctr = 301; nr.relayed = false;
        mr_ui_on_push(nr);
        t9 += 700; paint(t9);
        CHK("P9e the no-relay outcome renders NO RELAY HEARD",
            strstr(g_c.page_text, "NO RELAY HEARD") != nullptr);
        CHK("P9e ...and the retired no-relay wording is nowhere on the panel",
            strstr(g_c.page_text, retired) == nullptr);

        // ---- ★★ P9d — THE DM PLANE, on the panel, through the real TEAM roster. `aired_waiting` is a SEPARATE
        //      state from `ChanState::aired` and is rendered by a separate arm, so the channel checks above say
        //      nothing about it. A teammate is installed on the real team routing plane so the TEAM screen has a row
        //      to open — the same seam the native suite uses, not a poked snapshot.
        {
            MESHROUTE_NS::NodeConfig cfg{};
            cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
            cfg.team_id = 0xABCD1234u;
            g_node.on_init(cfg);
            g_node.set_team_local_id(50);
            g_node.test_learn_route(/*dest=*/60, /*via=*/60, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            g_exec = ExecLog{}; g_exec.ok = true; g_exec.code = MESHROUTE_NS::CmdCode::queued; g_exec.ctr = 42;
            t9 = settle(t9 + 1000);
            t9 = open_highlighted(t9, ">id 60");           // the teammate row -> the DM compose list
            t9 = double_press(t9 + 500); paint(t9);        // ...and send its first canned text
            CHK("P9d the DM really reached the executor", g_exec.calls == 1);
            CHK("P9d an ACCEPTED DM reads QUEUED, never SENT",
                strstr(g_c.page_text, "QUEUED") != nullptr && strstr(g_c.page_text, "SENT, waiting") == nullptr);
            mr_ui_on_push(aired_push(/*dst=*/60, /*ctr=*/42));
            t9 += 700; paint(t9);
            CHK("P9d the DM airing turns QUEUED into SENT, waiting",
                strstr(g_c.page_text, "SENT, waiting") != nullptr && strstr(g_c.page_text, "QUEUED") == nullptr);
        }
    }

    // ============================================================================================================ P10
    // ★★★ §B197/§B198/§B200 — THE DEVICE SLEEP POLICY, THROUGH THE REAL `mr_ui_allows_sleep()`. The PURE predicate is
    //   under the native gate (`ui-sleep:` cases, nine mutations); what NO native case can reach is this file's jobs:
    //   mapping the board's three arm verdicts, latching sleep off for the boot on a HARDWARE failure, and ⛔ NEVER
    //   arming at boot. `src/fw_main.cpp`'s gate — the one consumer — is outside every build a host can make, and is
    //   pinned structurally by `probe_board_ui`'s W23/W29/W30/W31.
    // ⛔⛔ P10a WAS RETARGETED, NOT EXTENDED. It used to assert *"mr_ui_init arms the button wake exactly once"* — it
    //   REQUIRED [[B200]], and would have gone green against the panicking image. It now asserts the opposite.
    {
        // ---- (i) ⛔ mr_ui_init ARMS NOTHING. The [[B200]] fix, measured rather than grepped ----------------------
        g_c.arm_calls = 0; g_c.disarm_calls = 0;
        g_c.arm_answer = mrui::WakeArm::armed; g_c.disarm_answer = true;
        Serial.reset();
        mr_ui_init();
        CHK("P10a mr_ui_init ARMS NOTHING at boot (B200)",
            g_c.arm_calls == 0 && g_c.disarm_calls == 0);
        CHK("P10a2 ...and says nothing about a wake source at boot",
            strstr(Serial.out, "button wake") == nullptr);

        // ---- (ii) a blank, idle, frame-free node PERMITS sleep --------------------------------------------------
        // ⚠ This is the POSITIVE arm and it comes first deliberately: every "refuses" check below is only evidence
        //   because this state exists and answers true. Without it a hook stuck at `false` would satisfy them all.
        uint32_t t10 = settle(600000);
        paint(t10);
        t10 += 20000; tick(t10);                    // > kBlankMs since the last input -> the panel blanks
        CHK("P10b a blank, idle node with no open frame PERMITS sleep", mr_ui_allows_sleep() == true);

        // ---- (iii) a LIT panel refuses — the bounded 15 s attention window --------------------------------------
        g_c.button_down = true;  tick(t10 + 10); tick(t10 + 60);
        g_c.button_down = false; tick(t10 + 110);
        // ★ THE B197 DISCRIMINATOR, AND IT IS MEASURED BEFORE THE PANEL EVER LIGHTS: the press is still UNDEBOUNCED
        //   here, so the model has not yet been woken by any gesture — the ONLY thing forbidding sleep at this
        //   instant is that a gesture is being classified. That is exactly the ≤1 s window a sleeping node used to
        //   spend asleep, which is why a tap did nothing and only a long hold ever got through.
        g_c.button_down = true;  tick(t10 + 200);
        CHK("P10c a press being CLASSIFIED refuses sleep (B197)", mr_ui_allows_sleep() == false);
        g_c.button_down = false;
        t10 = settle(t10 + 300);                    // complete the gesture; the panel is now LIT
        CHK("P10d a LIT panel refuses sleep", mr_ui_allows_sleep() == false);

        // ---- (iv) an OPEN page-buffer frame refuses on EVERY page pass (B198) -----------------------------------
        // ⛔ THE HARM WAS ~8 SECONDS PER FRAME ON THE EMERGENCY SCREEN: one 128 B page per service pass × a ≤1 s
        //   sleep between passes. So the property is "false on every pass of the frame", not "false at some point".
        dirty_the_model(t10 + 100);
        g_c = Canvas{};
        int refused_mid_frame = 0;
        for (int i = 0; i < 8; ++i) {
            tick(t10 + 200 + uint32_t(i) * 10);
            if (!mr_ui_allows_sleep()) ++refused_mid_frame;
        }
        CHK("P10e sleep is refused on EVERY page pass of the frame (B198)", refused_mid_frame == 8);
        CHK("P10e ...and the frame really did page out (8 pages, none blank)",
            g_c.next_page == 8 && g_c.min_draws_per_page >= 1);
        // ⓘ WHAT THIS ARM CANNOT ISOLATE, stated rather than implied: a frame can only be OPEN on a LIT panel, so
        //   `blanked` is false here too and this cannot prove `frame_open` is the term doing the work. The native
        //   `ui-sleep:` matrix drives all eight term combinations and does prove it; this measures the shipped path.
        t10 += 400;
        t10 += 20000; tick(t10);                    // blank again, frame long since complete
        CHK("P10f ...and permitted again once the frame is out and blank",
            mr_ui_allows_sleep() == true);

        // ---- (v) ★★★ THE ARM VERDICT MAPPING, AND `button_down` IS THE ONE THAT MUST NOT LATCH ------------------
        // The node is in the blank/idle/frame-free state that P10f just proved PERMITS sleep, so every answer below
        // is attributable to the arm alone.
        // ⛔⛔ A HELD BUTTON IS NOT A FAULT. It is the most ordinary reason in the world not to sleep — and latching
        //   on it would disable light-sleep for the whole boot on the first press of the day, on a battery-powered
        //   safety device, while looking exactly like a working fix.
        g_c.arm_calls = 0; g_c.disarm_calls = 0;
        Serial.reset();
        g_c.arm_answer = mrui::WakeArm::armed;
        CHK("P10g an armed board answers ok", mr_ui_arm_button_wake() == MrUiWakeArm::ok);
        g_c.arm_answer = mrui::WakeArm::button_down;
        CHK("P10g2 a HELD button answers button_down", mr_ui_arm_button_wake() == MrUiWakeArm::button_down);
        CHK("P10g3 ...and does NOT latch sleep off, and says nothing",
            mr_ui_allows_sleep() == true && Serial.out[0] == '\0');
        // ★ TWO arms so far, one per `mr_ui_arm_button_wake()` call — and `mr_ui_allows_sleep()` in P10g3 contributed
        //   NONE. That is a property, not bookkeeping: the policy hook runs every service pass and must never arm
        //   anything; an arm hidden inside it would be a per-pass arm on a possibly-held button.
        CHK("P10g4 ...and the POLICY hook itself armed nothing", g_c.arm_calls == 2);

        // ---- (vi) ★★★ THE FAIL-CLOSED PATH. The single most important behaviour in this slice -------------------
        // ⛔ A node that light-sleeps with its button unarmed is [[B197]] made PERMANENT AND INVISIBLE: the only
        //   remaining wake sources are a LoRa RxDone and the ≤1 s deadline timer, neither of which the operator can
        //   reach. ⇒ a HARDWARE failure must disable sleep for the WHOLE BOOT, and must SAY SO once, exactly.
        Serial.reset();
        g_c.arm_answer = mrui::WakeArm::failed;
        CHK("P10h a platform failure answers failed", mr_ui_arm_button_wake() == MrUiWakeArm::failed);
        CHK("P10h2 ...reported with the exact boot line",
            strstr(Serial.out, "!! OLED button wake unavailable; sleep disabled") != nullptr);
        CHK("P10h3 ...and that state now REFUSES sleep (fail closed)",
            mr_ui_allows_sleep() == false);
        // ★ SAID ONCE. The arm runs on every idle service pass, so an unlatched print would flood the USB-CDC sink
        //   this firmware has already been wedged by — and a flood is not something a later reader would attribute
        //   to a missing edge check. A second failure must add nothing.
        Serial.reset();
        (void)mr_ui_arm_button_wake();
        CHK("P10h4 ...and it is said ONCE, not on every pass", Serial.out[0] == '\0');
    }

    // ============================================================================================================ P12
    // ★★★ §B200 — THE DISARM SEAM, AND ITS FAILURE IS THE SERIOUS ONE. A refused disarm means the level interrupt is
    //   still on the pin with the CPU RUNNING, which is [[B200]]'s exact precondition. This layer cannot fix the
    //   hardware; the one thing it can do is stop the node ever arming it again.
    // ⛔⛔ WHAT THIS BLOCK CANNOT ISOLATE, STATED RATHER THAN LEFT TO BE ASSUMED. The lockout latch is BOOT-SCOPED and
    //   is deliberately never cleared — there is no reset entry point and adding a test-only one would be inventing
    //   API for the instrument. P10h already latched it, so *"the disarm failure is what disabled sleep"* is NOT
    //   attributable here, and neither is its own console line (it is said once, and the once is spent). ⇒ ONE of
    //   the two lockout paths can be proved end-to-end per process, and the ARM path was given that slot because it
    //   is the common one and its exact line is what bench Part 23.2 reads.
    //   ★ The disarm path's remaining obligations are covered STRUCTURALLY, with mutation controls, by
    //     `probe_board_ui`'s W27 (its `latch_sleep_off()` call and its exact string) — weaker, and named as such.
    {
        Serial.reset();
        g_c.disarm_calls = 0; g_c.disarm_answer = true;
        CHK("P12a a successful disarm reports true and says nothing",
            mr_ui_disarm_button_wake() == true && Serial.out[0] == '\0');
        CHK("P12a2 ...having actually asked the board", g_c.disarm_calls == 1);
        g_c.disarm_answer = false;
        CHK("P12b a refused disarm reports false", mr_ui_disarm_button_wake() == false);
        CHK("P12b2 ...and asked the board that time too", g_c.disarm_calls == 2);
    }

    // ============================================================================================================ P13
    // ★★★★ §CHROME-3 — THE STATUS STRIP (design §3.1/§4) AND §8.3's REPAINT INVALIDATION, AS AMENDED BY §8.3.1.
    //   The projection, its formatters and its equality are PURE and are driven by `test/test_firmware_ui_chrome.cpp`
    //   with a mutation battery. What NOTHING there can see is what this file does with them: which glyph lands at
    //   which x, whether the strip is drawn from the FROZEN chrome or read live mid-frame, and whether a snapshot-only
    //   change asks for a repaint at all.
    // ★★ THE SLOTS ARE STATED HERE INDEPENDENTLY of the renderer's layout table. A bound imported from the code under
    //    test would agree with a layout that had drifted — the "instrument that cannot fail" shape this arc keeps
    //    finding. `Font::small` is 6 px per column; every glyph but the battery is 7 px wide.
    {
        struct Slot { int icon_x, text_x; };
        const Slot kMail = {  0,   8 }, kHome = { 28, 36 }, kTeam = { 56, 64 };
        const Slot kKey  = { 79,  -1 }, kBatt = { 91, 104 };
        const int  kIconY = 0, kBaseY = 7;

        // ---- the fixture. The team plane already carries P9d's `team_id` + one route to id 60; the CONTENT key is
        //      loaded here through the core's own boot-restore path, which is the only public way to move §4.4's fact.
        uint8_t pub[32], priv[32];
        for (int i = 0; i < 32; ++i) { pub[i] = uint8_t(0xA0 + i); priv[i] = uint8_t(0x40 + i); }
        g_node.team_channel_key_load(pub, priv, /*present=*/true);
        g_c.batt_answer = 4123;                                  // 4.123 V -> the token `4.1V`
        uint32_t t13 = settle(900000);
        run_ticks(t13, 4, 10);                                   // > 30 s since P5's last sample -> one good read
        t13 = walk_to(t13 + 500, "INBOX");                       // a COMPLETE, VISIBLE inbox frame zeroes the unread
        paint(t13);
        t13 = settle(t13 + 500);
        paint(t13);

        // ---- (a) THE FIXED SLOTS, and every glyph by POINTER IDENTITY --------------------------------------------
        CHK("P13a the mail envelope is drawn at the strip's first slot",
            bitmap_at(kMail.icon_x, kIconY) == mrui::icons::kIconMail);
        CHK("P13a ...with its count in the slot's own text column",
            text_at(kMail.text_x, kBaseY) != nullptr && strcmp(text_at(kMail.text_x, kBaseY), "0") == 0);
        // §4.2 — this build HAS the mobile plane (MR_FEAT_MOBILE defaults to 1 here), nothing was ever confirmed, so
        // the house is the EMPTY one and the age reads `--`. ⛔ `--` is NOT `0s`: they are different silences.
        // ⚠ STATED LIMIT: only `unknown` is reachable from a host — `confirmed`/`checking`/`lost` are set by the
        //   mobile FSM's own RF paths, which this probe does not run. The four-state icon TABLE is pinned natively
        //   (`chrome-home:` cases); what is measured here is that the renderer selects from it at all, which the
        //   control that hardcodes one glyph reddens.
        CHK("P13a the home slot draws the never-confirmed house",
            bitmap_at(kHome.icon_x, kIconY) == mrui::icons::kIconHomeUnknown);
        CHK("P13a ...and its compact age is `--`, never `0s`",
            text_at(kHome.text_x, kBaseY) != nullptr && strcmp(text_at(kHome.text_x, kBaseY), "--") == 0);
        CHK("P13a the people slot draws the people glyph",
            bitmap_at(kTeam.icon_x, kIconY) == mrui::icons::kIconPeople);
        CHK("P13a ...counting the ONE team route this node knows",
            text_at(kTeam.text_x, kBaseY) != nullptr && strcmp(text_at(kTeam.text_x, kBaseY), "1") == 0);
        CHK("P13a a held team CONTENT key draws the normal key",
            bitmap_at(kKey.icon_x, kIconY) == mrui::icons::kIconKey);
        CHK("P13a the battery outline is the 11-px asset",
            bitmap_at(kBatt.icon_x, kIconY) == mrui::icons::kIconBattery);
        // ★ THE DIMENSIONS TRAVEL WITH THE POINTER, and this is not pedantry: the battery is the ONE asset whose rows
        //   are TWO bytes (`stride_of(11) == 2`), so a call site that passed the shared 7-px width would decode the
        //   same bytes as a 7x14 smear — an error a pointer-identity check alone cannot see.
        {
            bool dims_ok = true;
            for (int i = 0; i < g_c.n_rec; ++i) {
                const Canvas::Rec& r = g_c.rec[i];
                if (r.is_text || r.page != 0) continue;
                if (r.bits == nullptr) continue;      // §CHROME-4: the rail's `[rect]` frame carries no bytes
                const bool batt = (r.bits == mrui::icons::kIconBattery);
                const int  w    = batt ? int(mrui::icons::kBatteryW) : int(mrui::icons::kIconW);
                if (r.w != w || r.h != int(mrui::icons::kIconH)) dims_ok = false;
            }
            CHK("P13a each glyph is drawn at its OWN width (battery 11, others 7)", dims_ok);
        }
        CHK("P13a ...voltage in the right-anchored token column",
            text_at(kBatt.text_x, kBaseY) != nullptr && strcmp(text_at(kBatt.text_x, kBaseY), "4.1V") == 0);
        CHK("P13a the y=9 rule is still drawn under the strip", g_c.draw_hline > 0);
        CHK("P13a exactly five glyphs on the strip (no sixth)", strip_glyphs_on_page(0) == 5);
        // ★ §CHROME-4: the rail lives BELOW the rule and is measured in full by P14; what this pins here is that the
        //   two regions are disjoint — the strip kept its five and the rail drew its own five plus one frame.
        CHK("P13a ...and the rail drew five glyphs and ONE frame below it",
            rail_glyphs_on_page(0) == 5 && rail_frames_on_page(0) == 1);

        // ---- (b) GEOMETRY — §11.2's bound ------------------------------------------------------------------------
        // ⓘ Still scoped to the strip HERE, because the strip's own budget is what P13 is about; the BODY's bound is
        //   ASSERTED by P14f now that §CHROME-4 has migrated it to 19 columns at `x = 12`.
        CHK("P13b no strip draw exceeds x=127",  strip_max_x() >= 0 && strip_max_x() <= 127);
        CHK("P13b no strip draw exceeds y=63",   strip_max_y() >= 0 && strip_max_y() <= 63);
        CHK("P13b ...and the strip stays inside its own y=0..8 band", strip_max_y() <= 8);
        printf("  INFO strip right edge x=%d, bottom y=%d; BODY right edge x=%d\n",
               strip_max_x(), strip_max_y(), body_max_x());

        // ---- (c) THE BATTERY FIELD IS ANCHORED, so a shorter token moves NO earlier icon ---------------------------
        // ★ THE SHORTER TOKEN IS PRODUCED THE HONEST WAY: a reading too wide to render as `d.dV` is UNAVAILABLE
        //   (§CHROME-1 R2.2's geometric guard), so 12.0 V renders `--` — ⛔ never a plausible-looking clamp. That
        //   makes this check ALSO the end-to-end proof of the guard, through the shipped renderer.
        // ⓘ It cannot be produced by an unavailable READ: spec §7's last-good rule keeps the previous voltage for
        //   ever, and P5 already took a good sample in this process.
        const int mail_x = kMail.icon_x, home_x = kHome.icon_x, team_x = kTeam.icon_x, key_x = kKey.icon_x;
        g_c.batt_answer = 12000;                                 // 12.0 V — outside the four-column slot
        t13 += 31000; run_ticks(t13, 2, 10);
        t13 = settle(t13 + 1000);
        paint(t13);
        CHK("P13c an unrenderable voltage renders `--`, never a clamp",
            text_at(kBatt.text_x, kBaseY) != nullptr && strcmp(text_at(kBatt.text_x, kBaseY), "--") == 0);
        CHK("P13c ...and the battery ICON did not move with it",
            bitmap_at(kBatt.icon_x, kIconY) == mrui::icons::kIconBattery);
        CHK("P13c ...nor did any icon before it (anchored, not flowed)",
            bitmap_at(mail_x, kIconY) == mrui::icons::kIconMail &&
            bitmap_at(home_x, kIconY) == mrui::icons::kIconHomeUnknown &&
            bitmap_at(team_x, kIconY) == mrui::icons::kIconPeople &&
            bitmap_at(key_x,  kIconY) == mrui::icons::kIconKey);
        CHK("P13c ...and the strip still fits 128 px", strip_max_x() <= 127);

        // ---- (d) ★★★ THE FROZEN CHROME, ACROSS ALL EIGHT PAGE REPLAYS ---------------------------------------------
        // U8g2 re-clips the WHOLE scene once per page, so a frame spans eight ticks. A renderer that read the LIVE
        // projection would tear the strip the moment a value moved mid-frame — and this is the ONLY place that can be
        // measured, which is why the change is injected BETWEEN two pages of one frame.
        // ★ THE DRIVER IS THE **TEAM ROUTE COUNT**, deliberately: it is MONOTONIC and settable at any instant
        //   (`test_learn_route`), so unlike the session-unread mail value it cannot be reset underneath the case by a
        //   complete INBOX frame that a screen cycle happened to land on. An instrument whose fixture another
        //   mechanism can undo is one that fails for the wrong reason.
        t13 = settle(t13 + 1000);
        paint(t13);
        t13 += 1000;
        dirty_the_model(t13);                                     // an arrival, so a frame is owed
        run_ticks(t13 + 700, 3, 10);                              // open it and push three pages
        const char* p0 = text_at(kTeam.text_x, kBaseY, 0);
        char frozen_tok[8]; snprintf(frozen_tok, sizeof frozen_tok, "%s", p0 ? p0 : "?");
        g_node.test_learn_route(/*dest=*/62, /*via=*/62, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
        run_ticks(t13 + 740, 6, 10);                              // ⚡ ...and the remaining pages replay
        CHK("P13d every page of one frame drew the SAME strip", strip_identical_on_every_page(8));
        CHK("P13d ...including pages drawn after the value moved",
            text_at(kTeam.text_x, kBaseY, 7) != nullptr &&
            strcmp(text_at(kTeam.text_x, kBaseY, 7), frozen_tok) == 0);
        CHK("P13d the fixture really did move it under the open frame",
            strcmp(frozen_tok, "1") == 0);
        // ★ AND THE MID-FRAME CHANGE IS NOT LOST (§8.3 rule 5 / §B107): ONE follow-up frame renders it.
        t13 += 3000; paint(t13);
        CHK("P13d ...and the NEXT frame renders the newer projection",
            text_at(kTeam.text_x, kBaseY) != nullptr &&
            strcmp(text_at(kTeam.text_x, kBaseY), "2") == 0);

        // ---- (e) ★★★ §8.3.1 BEHAVIOUR 4 — LIT + CLEAN + A VISIBLE CHROME CHANGE ⇒ DIRTY --------------------------
        // ★★ THE POSITIVE HALF, AND THE WHOLE POINT OF §8.3: a rule that never invalidates is as wrong as one that
        //    always does. The change is a SNAPSHOT-ONLY fact with NO gesture and NO push — exactly the class §8.3
        //    names (a team route arriving on a beacon, the home link changing state) — so nothing else can mark the
        //    model dirty, and `FrameGate::step` answers `idle` for ever on a clean model.
        t13 = settle(t13 + 1000);
        paint(t13);                                              // ...and let that press's frame page out
        t13 += 1000; tick(t13);                                  // past the 500 ms throttle, panel LIT and CLEAN
        {
            const int frames_before = g_c.begin_frame;
            run_ticks(t13 + 10, 4, 10);
            CHK("P13e precondition: a clean lit panel opens NO frame itself",
                g_c.begin_frame == frames_before);
            g_node.team_channel_key_load(pub, priv, /*present=*/false);   // the CONTENT key is gone — §4.4 state moves
            run_ticks(t13 + 100, 10, 10);
            CHK("P13e a snapshot-only chrome change opens a frame, no gesture",
                g_c.begin_frame > frames_before);
            CHK("P13e ...and the strip now draws the CROSSED key",
                bitmap_at(kKey.icon_x, kIconY) == mrui::icons::kIconKeyCrossed);
            t13 += 300;
        }

        // ---- (f) ★★★★ §8.3.1 BEHAVIOURS 1 AND 2 — THE BLANKED PANEL, IN THE FIVE PINNED STEPS --------------------
        // ⛔⛔ THE SEQUENCE BEGINS AFTER THE BLANKING EDGE HAS COMPLETED, because the first `set_power_save(true)`
        //    LEGITIMATELY ISSUES ONE PANEL COMMAND. Counting it would fail a correct implementation — or, far worse,
        //    invite somebody to "fix" it by suppressing the edge itself.
        // ⓘ WHAT IS MEASURED HERE AND WHAT IS NOT, STATED RATHER THAN IMPLIED. `dirty` is private to the model and
        //   unreachable from this binary, so its PRESERVATION is pinned where it can be READ: the `chrome-invalidate:`
        //   cases in `test/test_firmware_ui_chrome.cpp` (mutations X27-X30), plus `probe_board_ui`'s W3, which forbids
        //   `firmware_ui.cpp` from naming `clear_dirty` at all. ⓘ `mr_ui_allows_sleep()` is ALSO unusable from here
        //   and it is not a gap in this slice: P10h has already spent the BOOT-SCOPED lockout, so the hook answers
        //   false whatever the UI state (P12's header records that ordering limit). The sleep permission itself is
        //   measured by P10b/P10f. ⇒ what this block measures is every OTHER observable consequence of behaviours 1
        //   and 2: no unblank, no frame, no page, and not one additional panel command.
        {
            t13 += 16000;                                        // (1) blank: > kBlankMs (15 s) with no input at all
            tick(t13);
            tick(t13 + 10);                                      //     ...and let the power-save edge complete
            CHK("P13f precondition: the panel is dark", g_c.last_power_save == 1);
            const int bus0    = g_c.bus_cmds();                  // (2) record the bus-COMMAND count, after the edge
            const int frames0 = g_c.begin_frame;
            const int pages0  = g_c.next_page;
            const int pwr0    = g_c.power_cmds;
            // (3) change the chrome while still blanked — the key, the team count and the battery together. ⛔ NO
            //     push and NO gesture: either would mark the model dirty on its own and the measurement would then be
            //     about that instead.
            g_node.team_channel_key_load(pub, priv, /*present=*/true);
            g_node.test_learn_route(/*dest=*/61, /*via=*/61, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            g_c.batt_answer = 3555;
            run_ticks(t13 + 100, 40, 100);                       // (4) four seconds of subsequent UI ticks
            // (5) ZERO ADDITIONAL bus commands, NO frame opened, and the panel still dark.
            CHK("P13f a blanked chrome change issues ZERO extra bus commands",
                g_c.bus_cmds() == bus0 && g_c.power_cmds == pwr0);
            CHK("P13f ...opens no frame and pushes no page",
                g_c.begin_frame == frames0 && g_c.next_page == pages0);
            CHK("P13f ...and does not unblank the panel", g_c.last_power_save == 1);
            // ---- §8.3.1 BEHAVIOUR 3 — after the WAKE, the first frame freezes the CURRENT chrome ------------------
            // ⛔ Not the projection captured while dark: the two now differ in the key AND the team count, which is
            //   what makes this check able to come out otherwise.
            t13 = settle(t13 + 5000);                            // a real press wakes the panel
            paint(t13);
            CHK("P13g the first frame after a wake freezes the CURRENT chrome",
                bitmap_at(kKey.icon_x, kIconY) == mrui::icons::kIconKey &&
                text_at(kTeam.text_x, kBaseY) != nullptr &&
                strcmp(text_at(kTeam.text_x, kBaseY), "3") == 0);
        }

        // ---- (h) ★★★ THE ATTENTION CLOCK IS NOT TOUCHED (§8.3.1 behaviour 1's last clause) ------------------------
        // ★★ A chrome change is NOT an input. The tempting wrong edit — "keep the panel awake while things are
        //    changing" — would postpone the bounded 15 s attention window on every tick, so a node whose home age
        //    turns once a second WOULD NEVER BLANK, and therefore (via `ui_allows_sleep`) would never light-sleep
        //    again. That is a power regression with no panic and nothing visible on the panel.
        {
            uint32_t t14 = settle(t13 + 2000);                   // ⓘ `settle` returns 1200 ms after its press, so the
            const int pwr_before = g_c.power_cmds;               //   attention deadline is t14 + ~14.3 s
            bool key_on = true;
            for (int i = 0; i < 40; ++i) {                       // 10 s of ticks, a chrome change on EVERY one
                key_on = !key_on;
                g_node.team_channel_key_load(pub, priv, key_on);
                tick(t14 + 100 + uint32_t(i) * 250);
            }
            CHK("P13h 40 chrome changes do NOT blank the panel early",
                g_c.last_power_save != 1);
            CHK("P13h ...and none of them touched the panel's power latch", g_c.power_cmds == pwr_before);
            tick(t14 + 100 + 16000);                             // now past kBlankMs since the LAST INPUT
            CHK("P13h ...and it still blanks on the unmoved deadline",
                g_c.last_power_save == 1 && g_c.power_cmds == pwr_before + 1);
            t13 = t14 + 20000;
            g_node.team_channel_key_load(pub, priv, /*present=*/true);
        }

        // ---- (i) NO BUS TRAFFIC FROM A DRAW. One frame = one begin_frame + eight next_page, whatever it drew ------
        // ⓘ The board half of this is `tools/probe_board_ui` (§CHROME-2's checks against the REAL `board_ui.cpp`:
        //   `draw_bitmap` / `draw_rect` are pure forwards to U8g2's COMPOSE-ONLY calls and add nothing to its
        //   `bus_ops()`). What THIS measures is the caller: the strip's ten-odd draws must not add a bus command of
        //   their own — an icon renderer that "helpfully" flushed a page would be invisible to a draw COUNT.
        {
            uint32_t t15 = settle(t13 + 2000);
            dirty_the_model(t15);
            const int bf0 = g_c.begin_frame, np0 = g_c.next_page, pwr0 = g_c.power_cmds;
            run_ticks(t15 + 700, 8, 10);
            CHK("P13i one frame costs one begin + eight pages, whatever it drew",
                g_c.begin_frame - bf0 == 1 && g_c.next_page - np0 == 8);
            CHK("P13i ...and the strip's draws issued no panel command",
                g_c.power_cmds == pwr0);
            CHK("P13i ...having actually drawn the strip on that frame",
                strip_glyphs_on_page(0) == 5);
        }
    }

    // ============================================================================================================ P14
    // ★★★★ §CHROME-4 — THE NAVIGATION RAIL (design §3.2/§5.2/§5.3), THE CONFIGURATION BADGE (§6) AND THE 19-COLUMN
    //   BODY MIGRATION (§7). The mapping, the badge priority and the slot mask are PURE and are driven by
    //   `test/test_firmware_ui_chrome.cpp` with a mutation battery. What NOTHING there can see is what THIS file does
    //   with them: which glyph lands in which slot, whether exactly ONE slot is boxed, whether the selection survives
    //   all eight page replays, whether an emergency frame issues a rail call at all, and where the BODY is drawn.
    // ★★ THE GEOMETRY IS STATED IN THIS FILE (see `kRailSlotY` above), never imported from the renderer's table.
    {
        uint32_t t16 = settle(1200000);
        paint(t16);

        // ---- (a) THE FIVE SLOTS, each glyph by POINTER IDENTITY, each at its canonical y ------------------------
        CHK("P14a the STATUS slot draws the information disc",
            rail_glyph_at(kSlotStatus) == mrui::icons::kIconStatus);
        CHK("P14a the TEAM slot reuses the people glyph",
            rail_glyph_at(kSlotTeam) == mrui::icons::kIconPeople);
        CHK("P14a the INBOX slot reuses the envelope",
            rail_glyph_at(kSlotInbox) == mrui::icons::kIconMail);
        CHK("P14a the SEND slot draws the outgoing arrow",
            rail_glyph_at(kSlotSend) == mrui::icons::kIconSend);
        CHK("P14a the SETTINGS slot draws a badge variant of the gear",
            rail_glyph_at(kSlotSettings) != nullptr);
        // ★ EVERY rail glyph is 7x7 and sits in the rail's column — a glyph drawn at the body's x would still be "a
        //   bitmap below the rule" to a count, and would land on top of the text.
        {
            bool geom_ok = true;
            for (int i = 0; i < g_c.n_rec; ++i) {
                const Canvas::Rec& r = g_c.rec[i];
                if (r.is_text || r.page != 0 || r.y <= 9) continue;
                if (r.bits == nullptr) {                        // the selection frame
                    if (r.x != kRailX || r.w != kRailW || r.h != kRailH) geom_ok = false;
                } else {                                        // a slot glyph
                    if (r.x != kRailIconX || r.w != int(mrui::icons::kIconW) ||
                        r.h != int(mrui::icons::kIconH)) geom_ok = false;
                    if (r.y + r.h - 1 > 59) geom_ok = false;    // §3.2: the rail ends at y = 59
                }
            }
            CHK("P14a every rail draw is inside x=0..9, y=10..59", geom_ok);
        }
        CHK("P14a no draw of the whole frame exceeds x=127 or y=63",
            strip_max_x() <= 127 && body_max_x() <= 127 && strip_max_y() <= 63);
        // ★ THE WHOLE FRAME'S NON-TEXT TALLY, which is what a sixth rail glyph or a second selection frame moves and
        //   neither of the two scoped counters above would: 5 strip glyphs + 5 rail glyphs + 1 selection frame.
        CHK("P14a the frame draws exactly 5 + 5 glyphs and 1 frame", bitmaps_on_page(0) == 11);

        // ---- (b) EXACTLY ONE FRAME, AND IT NAMES THE SCREEN ------------------------------------------------------
        // ⛔ `rail_boxed_slot` answers -2 for MORE THAN ONE, so "the right slot is boxed" cannot be satisfied by a
        //    rail that boxes everything — the reader that returned the first match would have passed over exactly that.
        {
            struct { int slot; const char* name; } order[5] = {
                { kSlotStatus, "STATUS" }, { kSlotTeam, "TEAM" }, { kSlotInbox, "INBOX" },
                { kSlotSend, "SEND" }, { kSlotSettings, "SETTINGS" },
            };
            bool every_screen_ok = true, exactly_one = true;
            for (int k = 0; k < 5; ++k) {
                t16 = walk_to_slot(t16 + 500, order[k].slot);
                if (rail_boxed_slot() != order[k].slot) every_screen_ok = false;
                if (rail_frames_on_page(0) != 1) exactly_one = false;
            }
            CHK("P14b cycling the five screens boxes each one's own slot", every_screen_ok);
            CHK("P14b ...and EXACTLY one navigation frame is drawn each time", exactly_one);
        }

        // ---- (c) THE SELECTION SURVIVES ALL EIGHT PAGE REPLAYS ---------------------------------------------------
        // ★★ U8g2 re-clips the WHOLE scene once per page, so a rail read from a live authority — or from a
        //    renderer-local cursor advanced per page — would move under an open frame. This is the only venue that
        //    can see it.
        {
            t16 = walk_to_slot(t16 + 500, kSlotInbox);
            t16 += 1000;
            dirty_the_model(t16);
            run_ticks(t16 + 700, 9, 10);                        // one whole frame, page by page
            bool same_every_page = true;
            for (int p = 0; p < 8; ++p) {
                if (rail_boxed_slot(p) != kSlotInbox) same_every_page = false;
                if (rail_glyph_at(kSlotSend, p) != mrui::icons::kIconSend) same_every_page = false;
            }
            CHK("P14c the correct slot stays boxed on all EIGHT page replays", same_every_page);
        }

        // ---- (d) §5.2's MODAL MAPPING, THROUGH THE SHIPPED PATH --------------------------------------------------
        // ★★ THE RAIL MUST DESCRIBE THE BODY ACTUALLY BEING SHOWN. The inbox DETAIL modal replaces the body while
        //    `Screen::inbox` is underneath it; the DM compose modal is opened from the TEAM screen, so a rail that
        //    followed the screen alone would say TEAM over a send.
        {
            t16 = walk_to_slot(t16 + 500, kSlotInbox);
            t16 = open_highlighted(t16 + 500, ">DM  ");
            CHK("P14d precondition: the inbox DETAIL modal is open",
                strstr(g_c.page_text, ">back") != nullptr);
            CHK("P14d the detail modal keeps INBOX selected",   rail_boxed_slot() == kSlotInbox);
            t16 = double_press(t16 + 500); paint(t16);          // `back` closes it
            t16 = walk_to_slot(t16 + 500, kSlotTeam);
            t16 = open_highlighted(t16 + 500, ">id 60");        // a teammate row -> the DM compose modal
            CHK("P14d precondition: a DM compose modal is open over the TEAM screen",
                strstr(g_c.page_text, "to: ") != nullptr);
            CHK("P14d a compose modal opened from TEAM selects SEND, not TEAM",
                rail_boxed_slot() == kSlotSend);
            t16 = double_press(t16 + 500); paint(t16);          // send the first canned text -> the RESULT phase
            CHK("P14d ...and the send RESULT keeps SEND selected", rail_boxed_slot() == kSlotSend);
            t16 = double_press(t16 + 500); paint(t16);          // acknowledge and close
        }

        // ---- (e) §5.3's EMERGENCY EXCEPTION — NO RAIL AT ALL, AND THE BODY KEEPS x = 0 ---------------------------
        // ⛔⛔ THIS IS THE SAFETY HALF OF THE SLICE. `NO RELAY HRD` / `NOT RELAYED` are `Font::large` = 10 px per
        //    column on a 128-px panel, i.e. TWELVE columns at x = 0. Shifting that body to `kBodyX` would leave 11
        //    and CLIP A DISTRESS HEADLINE — which is why §5.3 makes the exception and why it is measured here rather
        //    than trusted to the projection.
        {
            t16 = settle(t16 + 2000);
            g_c.button_down = true;                             // hold past arm_ms -> the alarm ARMS
            for (int i = 0; i < 20; ++i) tick(t16 + 100 + uint32_t(i) * 100);
            t16 += 2200;
            paint(t16);
            CHK("P14e precondition: the emergency overlay owns the body",
                strstr(g_c.page_text, "RELEASE!") != nullptr || strstr(g_c.page_text, "EMERGENCY IN") != nullptr);
            CHK("P14e an emergency frame draws NO rail glyph and NO frame",
                rail_glyphs_on_page(0) == 0 && rail_frames_on_page(0) == 0);
            CHK("P14e ...and the STRIP is still there (§5.3 keeps it)", strip_glyphs_on_page(0) == 5);
            // ⛔⛔ EVERY emergency body draw, not just the leftmost. A check on `body_text_min_x()` alone PASSED over a
            //   mutant that moved the `Font::large` HEADLINE to `kBodyX` and left the small-font detail line at 0 —
            //   measured, on this very control (C82). The headline is the string that clips, so it is the one that
            //   must be asserted individually.
            {
                bool all_at_zero = true;
                for (int i = 0; i < g_c.n_rec; ++i) {
                    const Canvas::Rec& r = g_c.rec[i];
                    if (!r.is_text || r.page != 0 || r.y <= 9) continue;
                    if (r.x != 0) all_at_zero = false;
                }
                // ⚠ THE LABEL IS UNDER 64 BYTES ON PURPOSE — `run.sh`'s coverage roll-up parses `%-64s`, so a
                //   longer one silently drops out of the "N of M reddened" denominator. Measured here: the first
                //   wording was 67 bytes and read as "no control reddens" while C82 was turning it red.
                CHK("P14e ...and EVERY emergency body draw keeps x=0", all_at_zero && body_text_min_x() == 0);
            }
            g_c.button_down = false;
            for (int i = 0; i < 10; ++i) tick(t16 + 100 + uint32_t(i) * 100);
            t16 = settle(t16 + 3000);
            t16 = settle(t16 + 1000);                           // acknowledge the outcome, back to the normal cycle
            t16 = settle(t16 + 1000);
        }

        // ---- (f) §7's BODY MIGRATION, MEASURED ON EVERY ORDINARY SCREEN -------------------------------------------
        // ★★★ §7.1 rule 3: *"every rendered normal line is proven at or below 116 pixels"*. ⛔ AND rule 1's other
        //     half is measured with it: every ordinary body draw starts at `kBodyX`, so no text can land under the
        //     rail. A renderer that moved only SOME sites would satisfy neither.
        // ⚠ THE WALK COVERS THE MODAL BODIES TOO, because those are the widest lines in the tree (the inbox preview
        //   row and the detail header).
        {
            bool x_ok = true, w_ok = true;
            int  widest = 0, widest_right = 0;
            for (int k = 0; k < 5; ++k) {
                t16 = walk_to_slot(t16 + 500, k);
                if (body_text_min_x() >= 0 && body_text_min_x() != kBodyXExpected) x_ok = false;
                if (body_text_max_x() > 127) w_ok = false;
                if (body_max_cols() > widest) widest = body_max_cols();
                if (body_text_max_x() > widest_right) widest_right = body_text_max_x();
            }
            // ...and the two body-REPLACING views, which the screen walk cannot reach
            t16 = walk_to_slot(t16 + 500, kSlotInbox);
            t16 = open_highlighted(t16 + 500, ">CH7 ");
            if (body_text_min_x() >= 0 && body_text_min_x() != kBodyXExpected) x_ok = false;
            if (body_text_max_x() > 127) w_ok = false;
            if (body_max_cols() > widest) widest = body_max_cols();
            if (body_text_max_x() > widest_right) widest_right = body_text_max_x();
            t16 = double_press(t16 + 500); paint(t16);
            CHK("P14f every ordinary body draw starts at x=12 (the one kBodyX)", x_ok);
            CHK("P14f ...and no ordinary body line exceeds 116 px", w_ok);
            printf("  INFO §7.3 audit: widest ordinary body line = %d columns, right edge x = %d (bound 19 / 127)\n",
                   widest, widest_right);
            CHK("P14f ...and the widest line is at most 19 columns", widest <= 19);
            // ⛔ VACUITY GUARD: a walk that drew nothing would satisfy both bounds. Require the body to have been
            //    genuinely wide — the inbox preview row is 19 columns by construction.
            CHK("P14f ...and the walk really did draw a full-width line", widest >= 15);
        }

        // ---- (g) §6/§6.1 — THE BADGE PRIORITY TABLE, INCLUDING BOTH OVERLAPPING PAIRS -----------------------------
        // ★★ THE FOUR STATES ARE DRIVEN THROUGH THE REAL `ConfigService` over the fake store, so this measures the
        //    SHIPPED selection (`ChromeCfg::from` -> `ui_cfg_badge` -> `rail_badge_glyph`), not a table lookup.
        // ⛔ AND SETTINGS MUST STILL SAY IT IN WORDS (§6: *"the icon may replace the STATUS decoration; it may never
        //    replace the instruction"*) — every arm below asserts the badge AND, where the state is one §6 names,
        //    the actionable text on the SETTINGS screen itself.
        {
            ProbeCfgStore& st = probe_store();
            ProbeCfgLive&  lv = probe_live();
            st.can_save = true; st.can_load = true;
            t16 = walk_to_slot(t16 + 500, kSlotSettings);
            t16 = walk_to(t16 + 500, ">DISCARD");
            t16 = double_press(t16 + 500); paint(t16);           // a clean draft over the current record
            lv.eff = mrfw::cfg_values_from_blob(st.rec);
            t16 += 700; paint(t16);
            CHK("P14g clean  -> the plain gear",     rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettings);
            CHK("P14g ...and SETTINGS says nothing it cannot act on",
                strstr(g_c.page_text, "CFG* UNSAVED") == nullptr &&
                strstr(g_c.page_text, "CFG! RELOAD")  == nullptr);
            // unsaved: edit a covered field in the DRAFT only
            t16 = walk_to(t16 + 500, ">DM crypt");
            t16 = double_press(t16 + 500); paint(t16);
            t16 = settle(t16 + 500);
            t16 = double_press(t16 + 500); paint(t16);
            CHK("P14g unsaved -> the gear with the dot",
                rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsUnsaved);
            CHK("P14g ...and SETTINGS still SAYS `CFG* UNSAVED` in words",
                strstr(g_c.page_text, "CFG* UNSAVED") != nullptr);
            // unsaved + RESTART-REQUIRED: §6's priority puts UNSAVED above restart
            lv.eff.at(mrfw::CfgField::ble_mode) = 1;
            t16 = settle(t16 + 500);
            CHK("P14g unsaved + restart -> UNSAVED wins (§6's priority)",
                rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsUnsaved);
            CHK("P14g ...and RESTART NEEDED is still stated in words",
                strstr(g_c.page_text, "RESTART NEEDED") != nullptr);
            // conflict + unsaved: CONFLICT outranks everything
            st.rec.mobile_autoregister = st.rec.mobile_autoregister ? 0 : 1;
            mr_ui_on_config_saved();
            t16 += 700; paint(t16);
            CHK("P14g conflict + unsaved -> CONFLICT wins",
                rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsConflict);
            CHK("P14g ...and SETTINGS SAYS `CFG! RELOAD`, the remedy",
                strstr(g_c.page_text, "CFG! RELOAD") != nullptr);
            // ...clear the conflict and the draft; only the reboot fact is left
            t16 = walk_to(t16 + 500, ">DISCARD");
            t16 = double_press(t16 + 500); paint(t16);
            t16 += 700; paint(t16);
            CHK("P14g restart alone -> the gear with the restart marker",
                rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsRestart);
            CHK("P14g ...and it is NOT the unsaved or the conflict glyph",
                rail_glyph_at(kSlotSettings) != mrui::icons::kIconSettingsUnsaved &&
                rail_glyph_at(kSlotSettings) != mrui::icons::kIconSettingsConflict);
            lv.eff.at(mrfw::CfgField::ble_mode) = 0;
        }
    }

    printf("\n%d passed / %d failed / %d total\n", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
