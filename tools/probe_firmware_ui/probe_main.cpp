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
//   ⓘ §UI-15 slice 5 / [[B225]] ADDS ITS OWN, AND THEY ARE THE SAME TWO SHAPES. The HARNESS PRECONDITIONS: "P15a the
//     PROVISION row can be highlighted" and the two "precondition:" lines that state which team the node is in — they
//     assert the walk ARRIVED and which arm of §3.6.3's conditional is being measured, which is what makes the
//     assertion beside each mean something. The NEGATIVE SPACE: every "zero durable writes" / "the adapter was not
//     entered at all" / "nothing was applied live" / "no retune happened" on the BACK, the PHY refusal and the two
//     failure arms. ★ Each has a POSITIVE ARM IN THE SAME PHASE — the identical screen with the divergence removed
//     really does create a team, write once and move the live node — so the zeros are evidence, not a fixture that
//     could never do anything.
//   ⓘ §UI-17 S2 ADDS SIX CHECKS — the `P6b2` blank-and-wake phase plus P9d's close — AND EXACTLY **ONE** OF THEM IS A
//     STANDING EXCEPTION. ★ MEASURED off the roll-up rather than predicted, because the prediction was WRONG: this
//     note first claimed no control could redden any of them. Five ARE reddened (a renderer stuck not painting fails
//     them along with everything else, C10's shape), and only "P6b2 ...and the store came through it untouched" is
//     not — the familiar NEGATIVE SPACE shape, since no mutation of a renderer can make a real `pull()` lose a
//     record. ⛔ WHAT NO CONTROL IN THIS FILE CAN DO, and this is the division of labour rather than a gap, is INVERT
//     THE RETENTION ITSELF: it lives in `src/firmware_ui_model.h` and every control in `run.sh` mutates
//     `src/firmware_ui.cpp`. That half is named rather than assumed, and the inventory is COMPLETE — **SEVEN**
//     entries in `tools/probe_ui_model_mutations.py --target=model`, every one of which turns the native suite RED:
//       **S01** the compose sub-view's `kBlankMs` auto-exit re-instated · **S02** the detail modal's re-instated ·
//       **S03** the compose sub-view no longer closed by a COMMITTED alarm (§B101's close dropped) ·
//       **S04** the blank deadline made conditional on a modal being open ·
//       **S05** the dark-page suspension removed (the cadence runs on a panel nobody can see) ·
//       **S06** the wake-time cadence restart removed (the wake pass itself turns the page) ·
//       **S07** the page turn no longer outranked by a blank due on the SAME tick.
//     ⛔ CORRECTED IN PLACE 2026-08-21 (QG): this sentence named only S01/S02/S04/S05/S06 and then said *"all six of
//     which"* — a count that matched neither the five it listed nor the seven that exist. A miscounted inventory is
//     the same class of defect as a stale coverage ratio ([[B120]]), which is why it is corrected rather than tidied.
//     ⇒ being reddened by a broad control proves these checks are WIRED; it does not prove they measure the ruling,
//     and the model battery is where that is proved.
//   ★ WHAT THIS PHASE ADDS OVER THE NATIVE CASES is the panel: that the power latch really goes down on the unmoved
//     deadline with a modal up, and that the frame the consumed wake press produces draws the SAME record's modal
//     with `back` still selected — none of which any native case compiles.
//   ⓘ P9d's "acknowledging the result closes the sub-view" is a HARNESS PRECONDITION: it asserts the phase LEFT THE
//     PANEL AS IT FOUND IT, which the deleted `kBlankMs` auto-exit used to do by accident. Without it P13's screen
//     walk starts inside a retained sub-view and measures the wrong screen (MEASURED: P13a red on a mail count the
//     INBOX frame it never reached could not clear).
//   ★★ AND THERE ARE NOW **TWO ARMS OF THIS FILE**, on the `MR_N_LAYERS` axis, for the reason the `MR_UI_BLE_ROW` arm
//     exists one paragraph up: with `-DMR_N_LAYERS=2` BOTH provisioning children hide, so `draw_provision_screen` is
//     structurally unreachable and §UI-15 slice 5's three screens were measured by NOTHING ([[B225]]). `run.sh`
//     therefore builds this file and `firmware_ui.cpp` a second time against `[env:heltec_v3]`'s own `-D` set; P7's
//     parent-row check flips direction there and P15 exists only there. ⛔ Anything used by one arm alone must be
//     inside the same `#if`: this file is built with `-Werror`, so an unused static on the other arm is a build
//     failure ([[B169]]'s shape).
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
#include "firmware_ui_prov.h"   // ★★★★ §UI-15 slice 5 / [[B225]]: the REAL team-create adapter's three DEVICE seams
                                //   (`prov_service` / `prov_device_facts` / `prov_note_persisted_team_local_id`) are
                                //   FAKED below, so what the child-enabled arm drives is the SHIPPED adapter over a
                                //   scripted device — ⛔ never a stand-in for the adapter itself.
#include "firmware_join_service.h"   // ★★ §UI-15 slice 6: slice 1's typed join transaction — the probe supplies the
                                     //    ONE instance `mrfw::join_service()` returns (its real body is in
                                     //    `src/firmware_config.cpp`, which is not in this link).
#include "firmware_join_profiles.h"  // ★★ ...and slice 2's `/mrjoin` store service, the same way. Both are PURE, so
                                     //    what the P16 phase drives is the SHIPPED logic over a scripted store.
#include "firmware_ui_join.h"        // ★ the SELECT/CONFIRM/WAITING strings and the four-term rule — the P16 checks
                                     //   compute their EXPECTATION with them, so a panel row is a VALUE RELATION.
#include "firmware_ui_chrome.h" // ★ the SHARED id / fingerprint / REPLACES formatters. The P15 checks compute their
                                //   EXPECTATION with them, so `the fingerprint on the panel` is a VALUE RELATION to
                                //   `ui_fmt_team_fingerprint(the created id)` rather than "six hex characters appeared".
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
// ⛔⛔ SCOPED TO THE RAIL's OWN COLUMN SINCE §UI-17 S3, AND IT IS A STRENGTHENING RATHER THAN A RELAXATION. This
//   counted EVERY `[rect]` on the page, which was sound while the rail's selection frame was `draw_rect`'s only
//   caller in the tree (§CHROME-4 said so in as many words). S3 gives it a second one — the STATUS body's reserved
//   24x24 mark — so a page-wide tally would now answer 2 on STATUS and P14b's *"EXACTLY one navigation frame"*
//   would fail against a CORRECT renderer. ⇒ the reader is scoped to what it is named for: a frame in the rail's
//   column, at the rail's width. ⓘ The BODY's own rects have their own counter directly below, so nothing became
//   invisible — the two regions are counted separately and both are asserted.
int rail_frames_on_page(int page = 0) {
    int n = 0;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.is_text || r.page != page || strcmp(r.s, "[rect]") != 0) continue;
        if (r.x == kRailX && r.w == kRailW) ++n;
    }
    return n;
}
// The rects the BODY drew — everything `[rect]` that is NOT the rail's selection frame. ⓘ §UI-17 S3's STATUS mark
// placeholder was the only one; S6 REPLACED IT WITH A REAL BITMAP, so this now answers 0 on EVERY screen, STATUS
// included. ⛔ It is kept, not deleted: a body that starts drawing frames of its own is exactly what it exists to
// catch (C99's "put the mark in the chrome" edit is the shipped example), and P14a asserts the 0 on both arms.
int body_rects_on_page(int page = 0) {
    int n = 0;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.is_text || r.page != page || strcmp(r.s, "[rect]") != 0) continue;
        if (!(r.x == kRailX && r.w == kRailW)) ++n;
    }
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
// ★★★ §UI-17 S3 — THE BODY NOW HAS **TWO** ORIGINS ON ONE SCREEN, so `body_text_min_x()` alone can no longer say
//     what P14f used to ask it. Spec §2.1: STATUS reserves `x = 12..35, y = 12..35` for a 24x24 mark and draws its
//     first three rows at `x = 40` (88 px = 14 columns); rows 3-4 stay at `kBodyX` with the full 19. Every other
//     screen keeps ONE origin. ⇒ the three readers below let P14f express *"a per-screen expected origin SET, plus
//     positive terms so it can still fail"* — ⛔ NEVER the `min_x >= 12` relaxation, which is the
//     instrument-that-cannot-fail shape this project has registered twenty-one times.
// ⚠ THE GEOMETRY IS STATED HERE, independently of `src/firmware_ui.cpp`'s own table, exactly as `kBodyXExpected`
//   and `kRailSlotY` are: a bound imported from the code under test agrees with a layout that has drifted.
constexpr int kStatusTextXExpected  = 40;   // spec §2.1: past the 24x24 slot plus a 4-px gutter
constexpr int kStatusNarrowColsExp  = 14;   // (128 - 40) / 6
constexpr int kStatusMarkXExpected  = 12, kStatusMarkYExpected = 12;
constexpr int kStatusMarkWExpected  = 24, kStatusMarkHExpected = 24;

// Every body text draw's x is one of the allowed origins for this screen. ⛔ An EMPTY body answers true, which is
// why P14f pairs this with the positive terms below rather than trusting it alone.
bool body_x_only_in(const int* allowed, int n_allowed, int page = 0) {
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (!r.is_text || r.page != page || r.y <= 9) continue;
        bool ok = false;
        for (int k = 0; k < n_allowed; ++k) if (r.x == allowed[k]) ok = true;
        if (!ok) return false;
    }
    return true;
}
// How many body rows a page drew AT an exact origin, and how wide the widest of them is — the two positive terms.
int body_rows_at_x(int x, int page = 0) {
    int n = 0;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (r.is_text && r.page == page && r.y > 9 && r.x == x) ++n;
    }
    return n;
}
int body_max_cols_at_x(int x, int page = 0) {
    int m = 0;
    for (int i = 0; i < g_c.n_rec; ++i) {
        const Canvas::Rec& r = g_c.rec[i];
        if (!r.is_text || r.page != page || r.y <= 9 || r.x != x) continue;
        const int n = int(strlen(r.s));
        if (n > m) m = n;
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

// ---- ★★★★ §UI-15 slice 5 / [[B225]]: THE PROVISIONING SEAMS' FAKES — the CHILD-ENABLED ARM ONLY -------------------
// ★★★ WHY THEY ARE *DEVICE* FAKES AND NOT AN `IUiProvision` STAND-IN, stated because the correction brief names
//     `UiModel::attach_provision` as the natural seam and this is one layer BELOW it: `s_model` is a file-static in an
//     anonymous namespace inside `src/firmware_ui.cpp`, so nothing outside that TU can call `attach_provision` — and
//     the ONE thing this arm exists to measure is that THE SHIPPED FILE wires the real adapter into it. ⇒ the probe
//     scripts what the adapter READS (`src/firmware_config.cpp`'s three device forwards, which are not in this link)
//     and lets `mr_ui_init()` perform the attach itself. What runs is therefore the REAL `UiProvisionAdapter`, the
//     REAL `ui_prov_create_team` precondition and the REAL `ProvisioningService` transaction — so every answer the
//     renderer draws is the adapter's own, ⛔ never a shape invented here. A control drops the attach and this whole
//     phase goes red, which is what proves the seam is wired rather than assumed.
// ⛔ GUARDED BY `MR_N_LAYERS < 2` because that is the guard the DECLARATIONS carry (`src/firmware_config.h:60`): on
//    the layered arm these three functions do not exist to define, and `firmware_ui.cpp` references none of them.
// ⛔ AND THE LIMIT, in the shape §UI-14's own fakes state it: no flash, no wear, no power-cut. A green run here says
//    the SCREEN drives the adapter and the transaction correctly, never that the storage is sound ([[B193]]).
#if MR_N_LAYERS < 2
namespace {
// The live sink. ★ `set_team` REALLY MOVES THE NODE, and that is not decoration: `DeviceProvLive::set_team`
// (`src/firmware_config.cpp:1422`) forwards to `g_node.set_team_id`, and `build_snapshot` draws the confirmation's
// `REPLACES <fp>` line from `g_node.config().team_id` — so without this forward the "already in a team" arm of that
// conditional would be unreachable and the probe would measure one half of it (the §W10b lesson).
// ⛔ `fire_dad` IS COUNTED AND NOT PERFORMED: it is the transaction's one AIRTIME operation and this probe spends
//    none. `apply_phy` is counted for the same reason it exists in the native suite — it must stay at ZERO, because an
//    OLED create is a MEMBERSHIP operation ([[B209]]).
struct ProbeProvLive : mrfw::IProvLive {
    int set_team_calls = 0, install_calls = 0, phy_calls = 0, dad_calls = 0;
    uint32_t last_team_id = 0;
    void set_team(uint32_t team_id) override {
        ++set_team_calls; last_team_id = team_id; (void)g_node.set_team_id(team_id);
    }
    void install_key(const uint8_t pub[32], const uint8_t priv[32]) override {
        ++install_calls; g_node.team_channel_key_load(pub, priv, /*present=*/true);
    }
    void apply_phy(const mrfw::ProvPhy&) override { ++phy_calls; }
    void fire_dad() override { ++dad_calls; }
    int total() const { return set_team_calls + install_calls + phy_calls + dad_calls; }
};
// Deterministic, and it MOVES between creates: `project_team` resamples until the minted id is neither 0 nor the
// current team, so a constant seed would make the second create walk the whole `kTeamIdMintTries` loop and refuse.
struct ProbeEntropy : mrfw::IEntropy {
    uint8_t seed = 0x51;
    void fill(uint8_t* out, size_t n) override {
        for (size_t i = 0; i < n; ++i) out[i] = uint8_t(seed + i);
        seed = uint8_t(seed + 0x13);
    }
};
// ⓘ The transaction runs over the SAME `ProbeCfgStore` the SETTINGS phases use (U1 — one durable seam, exactly as the
//   device has one), so `writes` is a single authority for "did anything durable happen" across both features.
struct ProvSeams {
    ProbeProvLive      live;
    ProbeEntropy       ent;
    mrfw::ProvSnapshot snap{};
    mrfw::ProvPhyFloor floor{};
    int     facts_calls = 0;              // = how many times the ADAPTER was entered (its first act is this read)
    int     noted_calls = 0;
    uint8_t noted_team_local_id = 0xFF;
    mrfw::ProvisioningService svc{probe_store(), live, ent};
};
ProvSeams& prov_seams() { static ProvSeams s; return s; }
}  // namespace
namespace mrfw {
ProvisioningService& prov_service() { return prov_seams().svc; }
void prov_device_facts(ProvSnapshot& out, ProvPhyFloor& floor) {
    ProvSeams& s = prov_seams();
    ++s.facts_calls;
    out   = s.snap;
    floor = s.floor;
}
void prov_note_persisted_team_local_id(uint8_t v) {
    ProvSeams& s = prov_seams();
    ++s.noted_calls; s.noted_team_local_id = v;
}
}  // namespace mrfw

// ---- ★★★★ §UI-15 slice 6: THE STATIC-JOIN SEAMS' FAKES — the CHILD-ENABLED ARM ONLY ------------------------------
// ★★★ THE SAME ARGUMENT AS THE PROVISIONING FAKES ABOVE, one feature over: `src/firmware_ui.cpp`'s
//     `DeviceJoinProvision` forwards to `mrfw::join_service()` and `mrfw::join_profile_service()`, whose real bodies
//     live in `src/firmware_config.cpp` (not in this link). Faking the two ACCESSORS is what lets this probe drive
//     the SHIPPED renderer, the SHIPPED model and the SHIPPED `UiProvisionAdapter` against the REAL slice-1
//     transaction and the REAL slice-2 store service — so every string the panel draws is the feature's own.
// ⛔ THE `/mrcfg` SIDE IS THE **SAME** `ProbeCfgStore` the SETTINGS and team-create phases use (U1 — one durable
//    seam, exactly as the device has one), so `writes` stays a single authority for "did anything durable happen".
// ⛔ AND THE LIMIT, unchanged: no flash, no wear, no power-cut ([[B193]]). A green run says the SCREENS drive the
//    transaction correctly, never that the storage is sound.
namespace {
// The `/mrjoin` record, with a settable four-state read so the panel's whole store matrix is reachable.
struct ProbeJoinStore : mrfw::IJoinStore {
    mrnv::JoinBlob rec{};
    mrnv::JoinRead answer = mrnv::JoinRead::ok;
    int loads = 0, writes = 0;
    ProbeJoinStore() { mrnv::join_blob_init(rec); }
    mrnv::JoinRead load(mrnv::JoinBlob& out) override {
        ++loads;
        if (answer == mrnv::JoinRead::ok) out = rec;
        return answer;
    }
    bool save(const mrnv::JoinBlob& b) override { ++writes; rec = b; return true; }
};
// ⛔ THE LIVE SEAM IS COUNTED AND NOT PERFORMED: `provision_apply_live` retunes a radio and starts DAD, and this
//    probe spends no airtime. ★ Leaving `g_node` untouched is also what keeps `canonical_node_id()` a STABLE fact
//    the correlation phase can aim a synthesized push at.
struct ProbeJoinLive : mrfw::IJoinLive {
    int        calls = 0;
    mrnv::Blob last{};
    void apply_and_start(const mrnv::Blob& b) override { ++calls; last = b; }
};
struct JoinSeams {
    ProbeJoinStore           presets;
    ProbeJoinLive            live;
    mrfw::JoinProfileService psvc{presets};
    mrfw::JoinService        jsvc{probe_store(), live};
};
JoinSeams& join_seams() { static JoinSeams s; return s; }
}  // namespace
namespace mrfw {
JoinService&        join_service()         { return join_seams().jsvc; }
JoinProfileService& join_profile_service() { return join_seams().psvc; }
}  // namespace mrfw
#endif  // MR_N_LAYERS < 2

// ==================================================================================================================
// harness
// ==================================================================================================================
namespace {
int g_pass = 0, g_fail = 0;
// PROBE_LIST=1 makes every check announce itself whether it passed or not. `run.sh` uses that as the DENOMINATOR of
// its "N of M checks are reddened by a control" roll-up, so the ratio is measured instead of restated in a comment —
// the header of this file carried a hand-maintained "20 of 25" that went stale the moment six checks were added.
// ⛔⛔ CORRECTED IN PLACE 2026-08-21 (§UI-17 S4, [[B229]]'s residue CLOSED), AND THE WITHDRAWN RULE IS KEPT VISIBLE.
//   This block read: *"⚠ ★ KEEP A LABEL AT **64 CHARACTERS OR FEWER**, and it is not a style rule: `run.sh`
//   attributes a reddening by re-reading the FAIL line with `s/^  FAIL \(.\{1,64\}\)  .*$/\1/`, which needs the
//   `%-64s` PADDING to find the label's end. A longer label overflows the field, the extraction misses, and the
//   roll-up then reports a check that a control DOES redden as "(no control reddens)" — an under-count, i.e. the
//   instrument lying in the quiet direction. ⓘ MEASURED 2026-08-20 ([[B226]]/[[B228]]): three new labels did exactly
//   that and were shortened."*
// ★ THE LENGTH RULE IS GONE BECAUSE THE MECHANISM IS: `run.sh`'s `attribute()` now matches a FAIL line against the
//   arm's KNOWN label list (the same `PROBE_LIST=1` output that supplies the denominator) and takes the LONGEST
//   label that prefixes it, so attribution no longer depends on the padding at all. Its own vacuity guard runs the
//   WITHDRAWN parse over a synthetic 90-character label and requires it to MISS. ⓘ The `%-64s` padding stays — it is
//   what keeps the FAIL lines readable — and a SHORT label is still better prose. It is no longer a correctness bound.
// ⚠ WHAT IS STILL TRUE: a label must be UNIQUE and must not be a prefix of another label whose control it does not
//   share, which longest-wins matching makes exact rather than approximate.
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

// ★★★★ §UI-17 S8 — THE PushKind ROSTER'S SIZE, **ASKED OF THE COMPILER RATHER THAN TYPED IN** (QG, 2026-08-22).
//      ⛔⛔ THE CLAIM THIS REPLACES WAS FALSE AS WRITTEN AND IS KEPT VISIBLE: P20d's bound was the enum's last member
//      (`send_aired`) with the note *"a kind appended later cannot quietly fall outside what this arm drives"* — a
//      hard-coded bound sweeps only the kinds that existed the day it was typed, and an 18th kind sails past it
//      UNSWEPT while the prose says it cannot.
// ★★★ THE MECHANISM IS A BUILD FAILURE, NOT A CONVENTION: the switch below has ⛔ **NO `default:`**, so a kind
//      appended to `lib/core/command.h` makes it non-exhaustive — and `run.sh` compiles THIS FILE with
//      `-Wall -Wextra -Werror`. ⇒ the probe harness **STOPS COMPILING, HERE**, on both arms, and the only way past it
//      is to add the new kind's `case` label, which is the very act that grows the sweep. ⓘ Every arm is the SAME
//      expression — "one past my own value" — so there is no ordinal to type wrong, and the maximum over the
//      underlying type IS the count. ⓘ A non-enumerator value is well defined for a scoped enum with a fixed
//      underlying type, so the 0..255 probe is legal; such values match no label and take the final `return 0`.
// ⛔ `test/test_firmware_ui_send.cpp` carries its OWN copy, deliberately: the two instruments state their
//    expectations independently (this file's standing rule), and a shared helper could only live in `src/`, i.e.
//    production code no product needs. Each copy fails its own build on a new kind.
uint8_t push_kind_after(MESHROUTE_NS::PushKind k) {
    using PK = MESHROUTE_NS::PushKind;
    switch (k) {
        case PK::msg_recv:       case PK::channel_recv:   case PK::send_acked:      case PK::send_failed:
        case PK::send_e2e_acked: case PK::hash_resolved:  case PK::peer_key_cached: case PK::config_adopted:
        case PK::join_refused:   case PK::send_blocked:   case PK::channel_sent:    case PK::mobile_reg:
        case PK::team_reg:       case PK::join_adopted:   case PK::team_key_received:
        case PK::team_channel_no_key: case PK::send_aired:
            return uint8_t(uint8_t(k) + 1u);
    }
    return 0;
}
uint8_t push_kind_count() {
    uint8_t n = 0;
    for (uint16_t v = 0; v <= 0xFF; ++v) {
        const uint8_t a = push_kind_after(MESHROUTE_NS::PushKind(uint8_t(v)));
        if (a > n) n = a;
    }
    return n;
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
// ★★★★ WHICH RECORD DID THE PANEL ACTUALLY DELETE — DERIVED FROM THE STORE, NEVER ASSUMED FROM THE ROW ORDER.
//      [[B231]] (the owner's newest-at-top ruling) moved which record the cursor starts on, and every hardcoded
//      `live_has(kind, 1)` here went red for a reason that was not a defect. ⇒ the cases below name the vanished
//      record by MEASURING it, exactly as P6g already picks its victim, and then assert the properties that are
//      really theirs: exactly one record left, of the right KIND, and [[B133]]'s "the same seq in the OTHER store
//      survived". ⓘ The fixture's seqs are 1..3 in both stores; nothing outside that range exists to look for.
constexpr uint32_t kFixtureSeqs = 3;
struct KindSet { bool live[kFixtureSeqs + 1] = {}; };
KindSet live_set(meshroute::InboxKind k) {
    KindSet s;
    for (uint32_t q = 1; q <= kFixtureSeqs; ++q) s.live[q] = live_has(k, q);
    return s;
}
// The ONE seq that was live in `before` and is not live now. ⛔ 0 when none or more than one vanished — a sentinel the
// caller must check, so "exactly one record was removed" cannot be mistaken for "some record was removed".
uint32_t vanished_since(meshroute::InboxKind k, const KindSet& before) {
    uint32_t gone = 0; int n = 0;
    for (uint32_t q = 1; q <= kFixtureSeqs; ++q) if (before.live[q] && !live_has(k, q)) { gone = q; ++n; }
    return n == 1 ? gone : 0;
}
// Every OTHER seq of that kind that was live before is still live. (`gone == 0` makes this trivially true, which is
// why the caller asserts `gone != 0` first.)
bool others_survived(meshroute::InboxKind k, const KindSet& before, uint32_t gone) {
    for (uint32_t q = 1; q <= kFixtureSeqs; ++q) if (before.live[q] && q != gone && !live_has(k, q)) return false;
    return true;
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
// ★★★★ §UI-17 S1 — AND IT LEAVES AN ENTERED TEAM/INBOX LIST FIRST, because `short` can no longer walk out of one:
//      the walk off the last row returns to the FIRST row (the contained-`BACK` rule), so a screen walk that started
//      inside an interactive list would press against a wall for 22 iterations and then measure the wrong screen.
uint32_t leave_list(uint32_t t);
uint32_t walk_to_slot(uint32_t t, int slot) {
    t = leave_list(t);
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
// ★★ §UI-15 slice 5 — THE BODY'S FIVE ROW BASELINES, stated here INDEPENDENTLY of the renderer's own table (P13's and
//    P14's rule: a bound imported from the code under test agrees with a layout that has drifted). Design §3.2 puts
//    them at 19/29/39/49/59.
// ⓘ MOVED OUT OF THE `MR_N_LAYERS < 2` GUARD BY §UI-17 S3: `status_row` below reads the SAME baselines at the
//   narrowed origin and runs on BOTH arms, so the table can no longer belong to one of them.
constexpr int kBodyY0Expected = 19, kBodyDyExpected = 10;
int body_y_expected(int row) { return kBodyY0Expected + row * kBodyDyExpected; }
// ★ §UI-17 S3's SIBLING ROW READER, at `x = 40`. `text_at` is EXACT-COORDINATE, so reading a STATUS row 0-2 through
//   `body_row` (x = 12) answers `nullptr` — the failure direction is loud, which is why the two readers are
//   separate rather than one reader with a tolerance.
const char* status_row(int row) { return text_at(kStatusTextXExpected, body_y_expected(row)); }
bool status_row_is(int row, const char* want) {
    const char* s = status_row(row);
    return s != nullptr && strcmp(s, want) == 0;
}
// ⛔⛔ AND THE ROW IS WHY THIS READER EXISTS AT ALL, rather than a `strstr` over `page_text`: the success screen draws
//    the FULL id `0x12A1B2C3` and the fingerprint `A1B2C3`, and the fingerprint is by definition the LAST SIX
//    CHARACTERS OF THE ID — so a substring search for it matches inside the id token and would pass on a renderer
//    that never drew the fingerprint row at all. The token is therefore asserted AS THE STRING AT ITS OWN ROW.
// ⓘ IT USED TO BE GUARDED BY `#if MR_N_LAYERS < 2` (its only caller was P15/P16, and an unused static under
//   `-Werror` is a build failure — [[B169]]'s shape). §UI-17 S3's P17 reads the WIDE STATUS rows through it on
//   BOTH arms, so the guard is gone and the pair sits beside its narrowed sibling above.
const char* body_row(int row) { return text_at(kBodyXExpected, body_y_expected(row)); }
bool body_row_is(int row, const char* want) {
    const char* s = body_row(row);
    return s != nullptr && strcmp(s, want) == 0;
}
// ★★★★ [[B237]] — A ROW READER THAT IS **FAIL-CLOSED ON `nullptr`**, and it exists because a check wrote
//      `strchr(r[0], '>')` on a row that a MUTANT had moved elsewhere: `text_at` answered `nullptr`, the mutant
//      SEGFAULTED, and `run.sh` counted the crash as a successful reddening (the B237 blocker — the runner's rule
//      is fixed there too, because a null-safe check is not a substitute for a runner that cannot be fooled).
// ⛔ THE ANSWER FOR A MISSING ROW IS **false**, NOT "well, it carries no marker": a row that is not on the panel
//    cannot satisfy a claim ABOUT the panel. Every negative-space check here must read that way — the failure
//    direction is loud, which is the same rule `body_row_is` follows one line up.
bool body_row_unmarked(int row) {
    const char* s = body_row(row);
    return s != nullptr && strchr(s, '>') == nullptr;
}

// ★★★★ §UI-17 S1 — ESCAPE AN ENTERED TEAM/INBOX LIST, and it is a NO-OP everywhere else by two independent tests:
//      the boxed slot must be TEAM or INBOX (so the SETTINGS menu, a compose modal — which boxes SEND by the §5.2
//      ruling — and every other screen are left alone), and a DETAIL modal is recognised by its own lowercase
//      `>back` and never operated on here.
// ⓘ ON A PASSIVE LIST IT COSTS AT MOST TWO PRESSES and they are ordinary navigation: `short` moves TEAM -> INBOX ->
//   SEND, the slot test then stops it, and `walk_to_slot`'s own loop puts the caller where it asked to be.
uint32_t leave_list(uint32_t t) {
    for (int i = 0; i < 12; ++i) {
        paint(t);
        const int slot = rail_boxed_slot();
        if (slot != kSlotTeam && slot != kSlotInbox) return t;         // not on a list screen at all
        if (strstr(g_c.page_text, ">back") != nullptr) return t;       // the DETAIL modal owns the body — leave it
        if (strstr(g_c.page_text, ">BACK") != nullptr) { t = double_press(t + 500); paint(t); return t; }
        t = settle(t + 500);
    }
    return t;
}

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

// ★★★★ §UI-17 S1 — REACH AN **ENTERED** TEAM/INBOX LIST AT ITS FIRST ROW, which is `to_cfg_menu`'s shape one screen
//      over: walk to the slot (which leaves whatever list the previous phase left behind), then `double`. ⇒ every
//      caller starts on ROW 0 of the list, exactly as an operator arriving and pressing `double` does.
// ⛔ IT MUST WORK FROM WHATEVER STATE THE PREVIOUS PHASE LEFT, which is why the leave is inside `walk_to_slot` and
//    not written at each call site: a `double` issued inside an already-entered list ACTIVATES the highlighted row.
uint32_t enter_list(uint32_t t, int slot) {
    t = walk_to_slot(t, slot);
    t = double_press(t + 500); paint(t);
    return t;
}
// ...and the row walk on top of it. ⓘ ONE function (U1): entering is part of reaching a row now, so a phase that
// forgot it would walk the whole cycle and measure another screen — the trap `to_cfg_menu` documents next door.
uint32_t open_in_list(uint32_t t, int slot, const char* want) {
    return open_highlighted(enter_list(t, slot), want);
}

// ★★★★ [[B232]] — REACH THE SETTINGS **MENU**, WHICH IS NOW A PLACE YOU HAVE TO ENTER. The screen LANDS on a CLOSED
//      single-entry view: `short` passes it in ONE press and `double` opens the menu (the PROVISION-child idiom).
// ⛔⛔ AND IT MUST WORK FROM WHATEVER STATE THE PREVIOUS PHASE LEFT BEHIND, which is why it walks to the closed view
//     FIRST rather than double-pressing wherever it happens to be: a `double` in the menu ACTIVATES the highlighted
//     row (`>DISCARD` is one of them), and the ordinary `settle` cycle can no longer wander back into the menu on its
//     own — walking off the last row lands on the closed view and the next press leaves the screen entirely.
// ⇒ every caller starts at ROW 0 of the menu, exactly as a fresh arrival plus one `double` does.
uint32_t to_cfg_menu(uint32_t t) {
    t = walk_to_slot(t, kSlotSettings);
    t = walk_to(t, mrui::kSettingsEnterText);   // already in the menu? walk the rows round to the closed view
    t = double_press(t + 500); paint(t);
    return t;
}
// The menu-row walk every SETTINGS phase used to spell as a bare `walk_to`. ⓘ ONE function (U1): the entry press is
// part of reaching a row now, so a phase that forgot it would walk the whole cycle and measure another screen.
uint32_t cfg_walk_to(uint32_t t, const char* want) { return walk_to(to_cfg_menu(t), want); }
// ...and its counterpart: land on the CLOSED single-entry view — the view SETTINGS now shows on ARRIVAL, and
// therefore the one design §6's *"the icon may replace the STATUS decoration; it may NEVER replace the instruction"*
// has to be satisfied from. ⚠ It PAINTS, so the caller reads the frame this walk produced and not an earlier one.
uint32_t to_cfg_closed(uint32_t t) {
    t = walk_to_slot(t, kSlotSettings);
    t = walk_to(t, mrui::kSettingsEnterText);
    paint(t);
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
    // ⚠ §UI-17 S1: BY THE RAIL, not by the body text. A `walk_to` over `page_text` presses `short` — which no
    //   longer leaves an ENTERED list at all — so a screen walk must go through `walk_to_slot`, whose leave is
    //   what makes it bounded (§CHROME-4 already made the rail the screen predicate).
    t = walk_to_slot(t + 2000, kSlotInbox);
    CHK("P6a the INBOX screen is reachable by pressing",   strstr(g_c.page_text, "INBOX") != nullptr);
    CHK("P6a ...and it lists both kinds",                  strstr(g_c.page_text, "DM ") != nullptr &&
                                                           strstr(g_c.page_text, "CH7") != nullptr);

    // ---- ★★★★ §UI-17 S1 — THE NAVIGATION CONTRACT, ON THE GLASS -----------------------------------------------
    // ★★ WHAT ONLY THIS PROBE CAN SEE: the model's arms are under the native gate (`ui17-` cases, seven mutations);
    //    what NOTHING there compiles is whether THIS file suppresses the marker while the list is passive, draws the
    //    `BACK` row at all, and keeps the rail still while the walk is contained. The rail is the screen predicate
    //    here (§CHROME-4), so "the press moved a ROW, not the screen" is measurable rather than inferred.
    CHK("P6h INBOX lands PASSIVE: no row carries the marker",
        strstr(g_c.page_text, ">DM  ") == nullptr && strstr(g_c.page_text, ">CH7 ") == nullptr);
    // ⓘ THE "no BACK row while passive" HALF IS PINNED AT **P14d**, NOT HERE, AND THE REASON IS MEASURED: this
    //   fixture holds six records, so the exit row (index 6) sits BELOW the four-row window at cursor 0 — a
    //   control that drew it anyway could not be seen from this screen, and a check no control can redden is
    //   the thing the roll-up exists to name. The TEAM roster there is ONE row, so its exit row is on screen.
    t = settle(t + 500); paint(t);
    CHK("P6h a short press on a passive list moves the RAIL, never a row",
        rail_boxed_slot() == kSlotSend);
    // ...and a `double` ENTERS it: the marker appears on the FIRST row and the list grows its exit row.
    t = walk_to_slot(t + 500, kSlotInbox);
    t = double_press(t + 500); paint(t);
    CHK("P6i a double ENTERS the list: a row is marked",   strstr(g_c.page_text, ">") != nullptr);
    CHK("P6i ...and it is the FIRST row of the list",      strstr(g_c.page_text, ">DM  ") != nullptr);
    CHK("P6i ...and the entering press did not move the rail", rail_boxed_slot() == kSlotInbox);
    // ★ THE CONTAINED WALK: `short` reaches BACK and the press past it comes HOME — ⛔ it never leaves the screen.
    t = walk_to(t + 500, ">BACK");
    CHK("P6j the walk reaches the BACK row without leaving the screen",
        strstr(g_c.page_text, ">BACK") != nullptr && rail_boxed_slot() == kSlotInbox);
    t = settle(t + 500); paint(t);
    CHK("P6j one more short comes HOME to row 0, never off the screen",
        rail_boxed_slot() == kSlotInbox && strstr(g_c.page_text, ">BACK") == nullptr &&
        strstr(g_c.page_text, ">") != nullptr);
    // ★ AND `BACK` RETURNS TO THE PASSIVE FORM OF THE SAME SCREEN, which one further `short` then passes.
    t = walk_to(t + 500, ">BACK");
    t = double_press(t + 500); paint(t);
    CHK("P6k a double on BACK returns to the PASSIVE list, not elsewhere",
        rail_boxed_slot() == kSlotInbox && strstr(g_c.page_text, "BACK") == nullptr &&
        strstr(g_c.page_text, ">") == nullptr);
    t = settle(t + 500); paint(t);
    CHK("P6k ...and one further short then passes the screen", rail_boxed_slot() == kSlotSend);

    // ---- (a) A CHANNEL record, opened while its same-numbered DM is still live -------------------------------------
    t = open_in_list(t + 500, kSlotInbox, ">CH7 ");
    CHK("P6b a double opens the CHANNEL record's modal",    strstr(g_c.page_text, "CH7 from") != nullptr);
    CHK("P6b ...showing that record's own body",           strstr(g_c.page_text, "ch-one") != nullptr);
    CHK("P6b ...with `back` selected, never `delete`",     strstr(g_c.page_text, ">back") != nullptr &&
                                                           strstr(g_c.page_text, ">delete") == nullptr);
    CHK("P6b ...and the page indicator reads 1/1",         strstr(g_c.page_text, "1/1") != nullptr);
    CHK("P6b opening DELETED NOTHING",                     live_count() == 6);

    // ---- ★★★★ §UI-17 S2 — BLANK AND WAKE **OVER AN OPEN MODAL** (spec S2 pins 1/2/5, on the glass) -----------------
    // ★★ WHAT ONLY THIS PROBE CAN SEE. The model's half is under the native gate (`ui17-hold:` cases, and the SEVEN
    //    entries S01-S07 — the complete inventory is at the top of this file);
    //    what NOTHING there compiles is the panel itself — that the power latch really goes down on the unmoved
    //    `kBlankMs` deadline WITH a modal up, and that the frame the WAKE press produces draws the SAME record's
    //    modal with `back` still selected rather than the list underneath it.
    // ⛔ THE AUTHORITY FOR "NOTHING WAS DELETED" IS A REAL `pull()`, as everywhere else in P6: the whole point of
    //    §3.3 is that a power event may not change anything durable, and the panel is exactly the wrong witness.
    // ⓘ WHAT REDDENS THESE, MEASURED OFF THE ROLL-UP — ⛔ AND THE FIRST VERSION OF THIS NOTE WAS WRONG, so it is
    //   corrected in place rather than quietly rewritten. It claimed *"NO CONTROL IN `run.sh` REDDENS THESE FOUR"*.
    //   FIVE of the six checks this slice adds ARE reddened by `run.sh` controls (a renderer that stops painting
    //   fails them along with everything else, C10's shape); only "P6b2 ...and the store came through it untouched"
    //   is not, and that one is NEGATIVE SPACE — no mutation of a renderer can make a real `pull()` lose a record.
    // ⛔ WHAT NO CONTROL HERE CAN DO is invert the RETENTION itself: it lives in `firmware_ui_model.h` while every
    //   control in `run.sh` mutates `src/firmware_ui.cpp`. ⇒ being reddened by a broad control proves these checks
    //   are WIRED; it does not prove they measure the ruling. That half is the SEVEN entries S01-S07 in
    //   `tools/probe_ui_model_mutations.py --target=model`, each of which reddens the native suite; the complete
    //   inventory, with what each one attacks, is at the top of this file.
    {
        const int live_before_blank = live_count();
        CHK("P6b2 precondition: the panel is lit with the modal open",
            g_c.last_power_save != 1 && strstr(g_c.page_text, ">back") != nullptr);
        t += 16000;                                        // > kBlankMs (15 s) with NO input at all
        tick(t);
        tick(t + 10);                                      // ...and let the power-save edge complete
        CHK("P6b2 the panel blanks on time with the modal still open",
            g_c.last_power_save == 1 && live_count() == live_before_blank);
        // ONE short press: the model CONSUMES it as the wake (`on_gesture`'s blanked arm returns early), so what the
        // next frame draws is what the operator walked away from — ⛔ never the list, and ⛔ never `delete`.
        t = settle(t + 5000);
        paint(t);
        CHK("P6b2 the wake press restores the SAME record's modal",
            g_c.last_power_save != 1 && strstr(g_c.page_text, "CH7 from") != nullptr &&
            strstr(g_c.page_text, "ch-one") != nullptr);
        CHK("P6b2 ...with `back` still selected, never `delete`",
            strstr(g_c.page_text, ">back") != nullptr && strstr(g_c.page_text, ">delete") == nullptr);
        CHK("P6b2 ...and the store came through it untouched", live_count() == live_before_blank);
    }

    // ---- (b) `back` CHANGES NOTHING IN STORAGE — asserted at the STORE, not on the screen --------------------------
    t = double_press(t + 500); paint(t);
    CHK("P6c `back` closes the modal",                     strstr(g_c.page_text, ">back") == nullptr &&
                                                           strstr(g_c.page_text, "INBOX") != nullptr);
    CHK("P6c ...and left all six records in the store",    live_count() == 6);
    CHK("P6c ...including the one that was open",          live_has(meshroute::InboxKind::channel, 1));

    // ---- (c) THE DELIBERATE SEQUENCE on a channel record: open, short, double -------------------------------------
    t = open_in_list(t + 500, kSlotInbox, ">CH7 ");
    CHK("P6d the channel modal is open again",             strstr(g_c.page_text, "CH7 from") != nullptr);
    t = settle(t + 500); paint(t);                         // one SHORT press -> the action toggles
    CHK("P6d a short press selects `delete`",              strstr(g_c.page_text, ">delete") != nullptr &&
                                                           strstr(g_c.page_text, ">back") == nullptr);
    CHK("P6d ...and still nothing has been deleted",       live_count() == 6);
    const KindSet ch_before = live_set(meshroute::InboxKind::channel);
    t = double_press(t + 500); paint(t);
    const uint32_t ch_gone = vanished_since(meshroute::InboxKind::channel, ch_before);
    CHK("P6d a channel record is GONE from a real pull",   ch_gone != 0);
    CHK("P6d ...exactly one record was removed",           live_count() == 5);
    // ★★ [[B231]] ON THE GLASS, END TO END: the highlight starts on the FIRST row of the channel block, and the owner's
    //    ruling is that that row is the NEWEST post. So the record the shipped path just deleted names the order — and
    //    it is measured through a real pull rather than read off the panel.
    CHK("P6d ★ ...and it is the NEWEST channel post ([[B231]] newest-at-top)", ch_gone == 3u);
    CHK("P6d ★ the DM with the SAME seq survived",         live_has(meshroute::InboxKind::dm, ch_gone));
    CHK("P6d ...and so did the other channel posts",
        others_survived(meshroute::InboxKind::channel, ch_before, ch_gone));
    CHK("P6d the modal closed back to the list",           strstr(g_c.page_text, "INBOX") != nullptr &&
                                                           strstr(g_c.page_text, ">delete") == nullptr);
    // ★★★★ [[B233]] ON THE GLASS, AND THIS IS THE ONLY INSTRUMENT THAT CAN SEE IT: the frame that closed the modal
    //      froze the snapshot built at the TOP of the delete's own tick, i.e. the PRE-erase rows — so the header still
    //      says 6/6 while the store holds 5. The fix owes ONE more repaint, from the next tick's fresh pull.
    // ⛔ NO PRESS, deliberately: a gesture repaints for an unrelated reason and this check would pass on the broken
    //    code. Time alone is advanced, past `kPaintThrottleMs`.
    // ⓘ The COUNT is what the panel can distinguish here — every fixture record renders the same preview text — and it
    //   is exactly the quantity the stale frame gets wrong.
    t += 700; paint(t);
    CHK("P6d ★★ the list itself catches up with NO further press ([[B233]])",
        strstr(g_c.page_text, "INBOX 5/5") != nullptr);

    // ---- (d) THE SAME on a DM, so neither store is assumed symmetric with the other -------------------------------
    t = open_in_list(t + 500, kSlotInbox, ">DM  ");
    CHK("P6e a DM record opens with the DM header",        strstr(g_c.page_text, "DM from 48") != nullptr);
    CHK("P6e ...and its own body",                         strstr(g_c.page_text, "dm-one") != nullptr);
    const KindSet dm_before = live_set(meshroute::InboxKind::dm);
    const KindSet ch_before_dm_delete = live_set(meshroute::InboxKind::channel);
    t = settle(t + 500); paint(t);
    t = double_press(t + 500); paint(t);
    const uint32_t dm_gone = vanished_since(meshroute::InboxKind::dm, dm_before);
    CHK("P6e a DM is GONE from a real pull",               dm_gone != 0);
    CHK("P6e ...exactly one record was removed",           live_count() == 4);
    CHK("P6e ★ the channel posts are untouched",
        others_survived(meshroute::InboxKind::channel, ch_before_dm_delete, 0));

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
    t = open_in_list(t + 500, kSlotInbox, ">CH7 ");
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
        // ★★★★ [[B232]] — THE LANDING, ON THE PANEL. The native suite drives the MODEL's arm; what only this file can
        //      say is what the operator actually SEES on arrival: the single entry row, and ⛔ NOT the menu's rows.
        CHK("P7 [[B232]] SETTINGS lands on the single entry row",
            strstr(g_c.page_text, mrui::kSettingsEnterText) != nullptr);
        CHK("P7 [[B232]] ...and the menu's rows are NOT on the panel", strstr(g_c.page_text, "DM crypt") == nullptr &&
                                                                       strstr(g_c.page_text, "DISCARD") == nullptr);
        CHK("P7 ...opening it wrote NOTHING",                    st.writes == 0);
        CHK("P7 ...and applied NOTHING live",                    lv.applies == 0);
        // ⇒ and a `double` opens the menu, which is what every phase below walks.
        t = to_cfg_menu(t + 500);
        CHK("P7 ...and it lists a covered field with its value", strstr(g_c.page_text, "DM crypt") != nullptr);
    // ★★ SPEC §3.6.2's CONDITIONAL ROW, MEASURED IN BOTH ARMS — and the same source file asserts both, so neither arm
    //    can rot unnoticed. `run.sh` builds this file AND `firmware_ui.cpp` a second time with `-DMR_UI_BLE_ROW=1`.
#if MR_UI_BLE_ROW
        t = cfg_walk_to(t + 500, ">BLE");
        CHK("P7 the BLE row IS rendered when the transport condition is met",
            strstr(g_c.page_text, "BLE") != nullptr);
#else
        // Walk the WHOLE menu once and require the row to appear on none of its frames — a single frame shows only
        // three rows, so checking one would prove nothing.
        // ⚠ [[B232]]: the walk STARTS INSIDE THE MENU (`to_cfg_menu`) and is bounded by the menu's own length — the
        //   `settle` cycle no longer re-enters it, so a loop that ran past the last row would look at other screens.
        {
            bool seen_ble = false;
            t = to_cfg_menu(t + 500);
            for (int i = 0; i < 8; ++i) { paint(t); if (strstr(g_c.page_text, "BLE")) seen_ble = true; t = settle(t + 500); }
            // ⚠ THE LABEL IS UNDER 64 CHARACTERS ON PURPOSE: `run.sh`'s coverage roll-up parses `%-64s`, so a longer
            //   one silently drops out of the "N of M reddened" denominator — measured on this very check.
            CHK("P7 the BLE row is ABSENT (no UI-12 transport in any env)", !seen_ble);
        }
        t = walk_to_slot(t + 500, kSlotSettings);
#endif
        // ★★★★ §UI-15 slice 5 / OWNER RULING 2026-08-19 — **THE PARENT `PROVISION` ROW FOLLOWS THE CHILD PREDICATES**,
        //      and BOTH ARMS OF THAT CONDITION ARE ASSERTED FROM THIS ONE SOURCE — the `MR_UI_BLE_ROW` shape directly
        //      above (U3), and [[B225]]'s whole correction: `run.sh` builds this file and `firmware_ui.cpp` twice, once
        //      layered (`-DMR_N_LAYERS=2`, both children hidden) and once mirroring `[env:heltec_v3]` (both children
        //      present), so neither arm of the ruling can rot unnoticed.
        // ⛔ The native suite cannot see either arm — it drives `settings_rows` directly, while what is measured here is
        //    that THIS FILE publishes the predicates and passes them to the row builder.
        // ⚠ THE WHOLE MENU IS WALKED, not one frame: a frame shows three rows, so a single look would prove nothing.
        {
            bool seen_prov = false;
            t = to_cfg_menu(t + 500);       // [[B232]]: the rows exist only inside the menu — see `to_cfg_menu`
            for (int i = 0; i < 9; ++i) { paint(t); if (strstr(g_c.page_text, "PROVISION")) seen_prov = true; t = settle(t + 500); }
            // ⚠ THE LABEL IS UNDER 64 CHARACTERS ON PURPOSE — `run.sh`'s coverage roll-up parses `%-64s`, so a longer
            //   one silently drops out of the "N of M reddened" denominator (measured on this very check).
#if MR_N_LAYERS < 2
            CHK("P7 the PROVISION row IS offered when a child exists", seen_prov);
#else
            CHK("P7 the PROVISION row is HIDDEN with no child (owner ruling)", !seen_prov);
#endif
            t = walk_to_slot(t + 500, kSlotSettings);
        }
        // ---- the EDITOR: `double` enters, `short` cycles the DRAFT, `double` accepts -------------------------------
        t = cfg_walk_to(t + 500, ">DM crypt");
        CHK("P7a the value row can be highlighted",             strstr(g_c.page_text, ">DM crypt") != nullptr);
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
        t = cfg_walk_to(t + 500, ">SAVE");
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
        t = cfg_walk_to(t + 500, ">key attach");
        t = double_press(t + 500); paint(t);                    // enter
        t = settle(t + 500);                                    // cycle 1 -> 0
        t = double_press(t + 500); paint(t);                    // accept
        st.can_save = false;
        const int writes_before = st.writes;
        t = cfg_walk_to(t + 500, ">SAVE");
        t = double_press(t + 500); paint(t);
        CHK("P7d a failed durable write says SAVE FAILED",      strstr(g_c.page_text, "SAVE FAILED") != nullptr);
        CHK("P7d ...it was ATTEMPTED",                          st.writes == writes_before + 1);
        CHK("P7d ...and changed nothing",                       st.rec.intro_attach == 1);
        CHK("P7d ...nothing was applied live",                  lv.applies == 1);
        CHK("P7d ...and the DRAFT BADGE SURVIVES the failure",  (t = walk_to_slot(t + 500, kSlotStatus),
                                                                 rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsUnsaved));
        // ---- BACK preserves it, and a REBOOT-CLASS save shows the third literal -----------------------------------
        st.can_save = true;
        t = cfg_walk_to(t + 500, ">DISCARD");
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
            CHK("P8a ...and the RELOAD row is offered",         (t = cfg_walk_to(t + 500, ">RELOAD"),
                                                                strstr(g_c.page_text, ">RELOAD") != nullptr));
            // (b) the CHANGE -> REVERT case the SAVE-time byte comparison cannot catch: the record goes back, so the
            //     bytes match the baseline again — and the latch must SURVIVE that, or the save proceeds.
            st.rec.intro_attach = 1;
            mr_ui_on_config_saved();
            const int writes_before2 = st.writes;
            t = cfg_walk_to(t + 500, ">SAVE");
            t = double_press(t + 500); paint(t);
            CHK("P8b a reverted external write STILL refuses the save",
                strstr(g_c.page_text, "CFG! RELOAD") != nullptr);
            CHK("P8b ...with zero writes",                      st.writes == writes_before2);
            t = cfg_walk_to(t + 500, ">DISCARD");
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
            t = cfg_walk_to(t + 500, ">key attach");
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
            t = cfg_walk_to(t + 500, ">SAVE");
            t = double_press(t + 500); paint(t);
            CHK("P8f ...and the SAVE over the wiped record is REFUSED",
                strstr(g_c.page_text, "CFG! RELOAD") != nullptr);
            CHK("P8f ...with zero writes",                  st.writes == writes_before3);
            t = cfg_walk_to(t + 500, ">DISCARD");
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
            t9 = open_in_list(t9, kSlotTeam, ">id 60");    // the teammate row -> the DM compose list
            t9 = double_press(t9 + 500);                   // ...and send its first canned text
            // ⚠⚠ §UI-17 S4 — THE HARNESS PAYS FOR A REAL BEHAVIOUR CHANGE, and it is the S2 note two lines below
            //    wearing the other hat. ⛔ MEASURED, not anticipated: this phase's teammate row carries a
            //    SECOND-SCALE route age, so its token now turns every second and `ui_team_invalidate` asks for the
            //    repaint §1.9 F-8 requires. One of those frames lands between the activation and the paint below and
            //    SPENDS the 2 Hz window, so the RESULT's own frame is refused and `paint()` re-reads the compose
            //    list. ⇒ advance past the throttle before reading the panel, exactly as P9e already does one block
            //    up. ⛔ NOTHING IS WEAKENED: the same two checks assert the same bytes — the harness merely gives the
            //    panel the window a repainting screen now costs. (Removing the invalidation makes this line
            //    unnecessary again, which is how the cause was measured.)
            t9 += 700; paint(t9);
            CHK("P9d the DM really reached the executor", g_exec.calls == 1);
            CHK("P9d an ACCEPTED DM reads QUEUED, never SENT",
                strstr(g_c.page_text, "QUEUED") != nullptr && strstr(g_c.page_text, "SENT, waiting") == nullptr);
            mr_ui_on_push(aired_push(/*dst=*/60, /*ctr=*/42));
            t9 += 700; paint(t9);
            CHK("P9d the DM airing turns QUEUED into SENT, waiting",
                strstr(g_c.page_text, "SENT, waiting") != nullptr && strstr(g_c.page_text, "QUEUED") == nullptr);
            // ---- ⚠⚠ §UI-17 S2 — THE PHASE THAT OPENS A MODAL NOW CLOSES IT, and this line is the harness paying for
            //      the ruling rather than a new property. ⛔ MEASURED, not anticipated: until §9 R-1 the `kBlankMs`
            //      auto-exit tidied this RESULT phase away during the long jump to P10, so every later phase started
            //      on a top-level screen BY ACCIDENT. With the modal retained (which is the whole point), P13's
            //      `walk_to_slot` met a sub-view whose rail boxes SEND, `leave_list` correctly answered "not on a
            //      list screen", and the walk then spent all 22 presses inside the TEAM list it landed in — P13a
            //      failed on a mail count the never-visible INBOX frame never cleared.
            //      ⇒ acknowledge the outcome, exactly as an operator does. ⛔ The RETENTION itself is not measured
            //      here — it is P6b2's and the native `ui17-hold:` cases' — this is the harness leaving the panel as
            //      it found it.
            t9 = settle(t9 + 1000); paint(t9);
            CHK("P9d acknowledging the result closes the sub-view",
                strstr(g_c.page_text, "SENT, waiting") == nullptr && strstr(g_c.page_text, "to: ") == nullptr);
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
        t13 = walk_to_slot(t13 + 500, kSlotInbox);               // a COMPLETE, VISIBLE inbox frame zeroes the unread
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
        // ★★ §UI-17 S3 — THE CENSUS BELOW IS TAKEN ON A **NAMED** SCREEN, and that is a re-point rather than a
        //    convenience. The non-text tally was `== 11` on whatever screen the walk happened to leave up; S3 gives
        //    the STATUS body a 24x24 `draw_rect` mark, so the expectation is now SCREEN-DEPENDENT (STATUS 12, every
        //    other screen 11) and a census taken on an unnamed screen would measure one of two right answers at
        //    random. ⇒ INBOX first, for the rail's own five-plus-one; the STATUS arm follows and pins the extra
        //    record at its own coordinates.
        t16 = walk_to_slot(t16, kSlotInbox);
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
        // ⛔⛔ RE-POINTED BY §UI-17 S3, ⛔ NOT WEAKENED. `bitmaps_on_page` counts EVERY non-text record, `draw_rect`
        //   included, and the STATUS body draws one more than the rest. ⇒ the expectation is SCREEN-DEPENDENT: an
        //   ordinary screen still owes exactly 11 (asserted here, on INBOX) and STATUS owes exactly 12, with the
        //   twelfth pinned at its own `12,12,24,24` immediately below. ⛔ A relaxation to `>= 11` would be the
        //   instrument-that-cannot-fail shape; the number stays EXACT on both arms of the split.
        CHK("P14a the frame draws exactly 5 + 5 glyphs and 1 frame", bitmaps_on_page(0) == 11);
        CHK("P14a ...and an ordinary screen's body draws no rect of its own", body_rects_on_page(0) == 0);
        // ---- (a2) §UI-17 S6 — THE STATUS BODY's 24x24 MARK, NOW THE REAL ASSET -------------------------------------
        // ★★★★ RE-POINTED BY §UI-17 S6, ⛔ NOT WEAKENED, and the re-point is the MEASUREMENT of the slice: S3 drew a
        //      `draw_rect` placeholder in this slot and S6 draws `icons::kMarkMeshRoute` there instead. ⇒ the census
        //      MOVES in exactly two ways and both are asserted: `body_rects_on_page(0)` **1 -> 0** (the placeholder
        //      rect is gone from every screen, STATUS included) while `bitmaps_on_page(0)` **stays 12** — that
        //      counter tallies EVERY non-text record, rect or glyph, so the twelfth record changed KIND, not count.
        // ★★ THE MARK'S EXACT RECT IS STILL ASSERTED, through the bitmap record instead of the rect record, and with
        //    the asset's POINTER IDENTITY added on top — the same standard `bitmap_at` holds the strip's glyphs to.
        //    ⛔ "a 24x24 bitmap appeared at 12,12" would pass against any icon in the header drawn at the wrong size.
        // ⓘ The mark is a BODY draw and must never be mistaken for the rail's selection frame — hence the geometry,
        //   and hence `rail_glyphs_on_page`'s x-band scoping, which the mark at x=12 stays outside of.
        {
            t16 = walk_to_slot(t16 + 500, kSlotStatus);
            CHK("P14a STATUS draws exactly ONE more non-text record, and NO body rect at all",
                bitmaps_on_page(0) == 12 && body_rects_on_page(0) == 0);
            bool mark_ok = false;
            for (int i = 0; i < g_c.n_rec; ++i) {
                const Canvas::Rec& r = g_c.rec[i];
                if (r.is_text || r.page != 0 || r.bits != mrui::icons::kMarkMeshRoute) continue;
                if (r.x == kStatusMarkXExpected && r.y == kStatusMarkYExpected &&
                    r.w == kStatusMarkWExpected && r.h == kStatusMarkHExpected) mark_ok = true;
            }
            CHK("P14a ...and it is the MeshRoute mark ASSET, at 24x24 on 12,12", mark_ok);
            CHK("P14a ...and STATUS still boxes exactly ONE rail slot",
                rail_frames_on_page(0) == 1 && rail_boxed_slot() == kSlotStatus);
            // ⛔ THE POSITIVE TERM FOR THE NARROWED GEOMETRY, read through the sibling row reader at `x = 40`: row 0
            //    always draws (`TEAM …` or `NO TEAM`), so a renderer that left every row at `kBodyX` reads nullptr.
            CHK("P14a ...and STATUS row 0 is drawn at the narrowed x=40 origin", status_row(0) != nullptr);
        }

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
            t16 = open_in_list(t16 + 500, kSlotInbox, ">DM  ");
            CHK("P14d precondition: the inbox DETAIL modal is open",
                strstr(g_c.page_text, ">back") != nullptr);
            CHK("P14d the detail modal keeps INBOX selected",   rail_boxed_slot() == kSlotInbox);
            t16 = double_press(t16 + 500); paint(t16);          // `back` closes it
            // ★ §UI-17 S1: TEAM lands PASSIVE, so the roster is on the panel with NO row marked — and the walk to
            //   a row is `enter_list`'s `double` plus the row walk, never a bare `short` walk.
            t16 = walk_to_slot(t16 + 500, kSlotTeam);
            CHK("P14d TEAM lands passive: no teammate row is marked",
                strstr(g_c.page_text, ">id 60") == nullptr);
            CHK("P14d ...and its passive form offers no BACK row",
                strstr(g_c.page_text, "BACK") == nullptr);
            t16 = settle(t16 + 500); paint(t16);
            CHK("P14d a short on passive TEAM moves the RAIL, never a row",
                rail_boxed_slot() == kSlotInbox);
            t16 = open_in_list(t16 + 500, kSlotTeam, ">id 60");   // a teammate row -> the DM compose modal
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
                // ⓘ CORRECTED IN PLACE 2026-08-21 ([[B229]] closed): this note read *"THE LABEL IS UNDER 64 BYTES
                //   ON PURPOSE — `run.sh`'s coverage roll-up parses `%-64s`, so a longer one silently drops out of
                //   the 'N of M reddened' denominator"*. The measurement it records is real (this wording was 67
                //   bytes and read as "no control reddens" while C82 was turning it red) — but the roll-up no
                //   longer parses the padding at all, so the LENGTH is no longer the reason. See the `CHK` macro.
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
        // ⛔⛔ RE-POINTED BY §UI-17 S3, ⛔ NOT WEAKENED, AND THE SHAPE OF THE RE-POINT IS THE WHOLE POINT. Spec §2.1
        //   reserves `x = 12..35, y = 12..35` of the STATUS body for a 24x24 mark and moves that screen's first
        //   three rows to `x = 40`; every other screen keeps the one `kBodyX`. ⇒ the assertion becomes a PER-SCREEN
        //   EXPECTED ORIGIN **SET** — STATUS `{12, 40}`, everything else `{12}` — over EVERY body text record, plus
        //   THREE POSITIVE TERMS so it can still fail: STATUS drew at least one row at 40, at least one at 12, and
        //   no `x = 40` row exceeds 14 columns (88 px).
        // ⛔ NEVER `min_x >= 12`. That was the tempting one-line "fix" and it is the instrument-that-cannot-fail
        //   shape this project has registered twenty-one times: it would pass for a body drawn anywhere to the
        //   right of the rail, including one whose STATUS rows had silently lost five columns of meaning.
        {
            bool x_ok = true, w_ok = true, narrow_ok = true;
            int  widest = 0, widest_right = 0;
            static const int kOneOrigin[1]    = { kBodyXExpected };
            static const int kStatusOrigins[2] = { kBodyXExpected, kStatusTextXExpected };
            for (int k = 0; k < 5; ++k) {
                t16 = walk_to_slot(t16 + 500, k);
                const bool status = (k == kSlotStatus);
                if (!body_x_only_in(status ? kStatusOrigins : kOneOrigin, status ? 2 : 1)) x_ok = false;
                if (status) {
                    // the two positive terms + the narrowed budget, all three measured on the real renderer
                    if (body_rows_at_x(kStatusTextXExpected) < 1) narrow_ok = false;
                    if (body_rows_at_x(kBodyXExpected)       < 1) narrow_ok = false;
                    if (body_max_cols_at_x(kStatusTextXExpected) > kStatusNarrowColsExp) narrow_ok = false;
                    // ...and the ROW-LEVEL split, which the set alone cannot see: rows 0-2 belong to the narrowed
                    // origin and rows 3-4 to the wide one. A single row that slipped back under the mark is a row
                    // drawn ON TOP of the artwork S6 is about to land there.
                    for (int row = 0; row <= 2; ++row)
                        if (text_at(kBodyXExpected, body_y_expected(row)) != nullptr) narrow_ok = false;
                    for (int row = 3; row <= 4; ++row)
                        if (text_at(kStatusTextXExpected, body_y_expected(row)) != nullptr) narrow_ok = false;
                }
                if (body_text_max_x() > 127) w_ok = false;
                if (body_max_cols() > widest) widest = body_max_cols();
                if (body_text_max_x() > widest_right) widest_right = body_text_max_x();
            }
            // ...and the two body-REPLACING views, which the screen walk cannot reach
            t16 = open_in_list(t16 + 500, kSlotInbox, ">CH7 ");
            if (!body_x_only_in(kOneOrigin, 1)) x_ok = false;
            if (body_text_max_x() > 127) w_ok = false;
            if (body_max_cols() > widest) widest = body_max_cols();
            if (body_text_max_x() > widest_right) widest_right = body_text_max_x();
            t16 = double_press(t16 + 500); paint(t16);
            CHK("P14f every body draw starts at its screen's own origin", x_ok);
            CHK("P14f ...STATUS uses BOTH origins, rows 0-2 at 40 in 14 cols", narrow_ok);
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
            t16 = cfg_walk_to(t16 + 500, ">DISCARD");
            t16 = double_press(t16 + 500); paint(t16);           // a clean draft over the current record
            lv.eff = mrfw::cfg_values_from_blob(st.rec);
            t16 += 700; paint(t16);
            CHK("P14g clean  -> the plain gear",     rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettings);
            CHK("P14g ...and SETTINGS says nothing it cannot act on",
                strstr(g_c.page_text, "CFG* UNSAVED") == nullptr &&
                strstr(g_c.page_text, "CFG! RELOAD")  == nullptr);
            // unsaved: edit a covered field in the DRAFT only
            t16 = cfg_walk_to(t16 + 500, ">DM crypt");
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
            t16 = cfg_walk_to(t16 + 500, ">DISCARD");
            t16 = double_press(t16 + 500); paint(t16);
            t16 += 700; paint(t16);
            CHK("P14g restart alone -> the gear with the restart marker",
                rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsRestart);
            CHK("P14g ...and it is NOT the unsaved or the conflict glyph",
                rail_glyph_at(kSlotSettings) != mrui::icons::kIconSettingsUnsaved &&
                rail_glyph_at(kSlotSettings) != mrui::icons::kIconSettingsConflict);
            // ★★★★ [[B232]] — AND EVERY CELL AGAIN FROM THE **CLOSED SINGLE-ENTRY VIEW**, which is what SETTINGS now
            //      shows on ARRIVAL. The checks above read the MENU, and after the ruling an operator can see the
            //      badge and never open the menu at all — so a renderer that gave the closed view a body of its own
            //      would satisfy every one of them while leaving §6's forbidden ICON-ONLY ERROR on the panel.
            // ⚠ Driven state by state through the same real service, in §6's own priority order.
            t16 = to_cfg_closed(t16 + 500);
            CHK("P14g [[B232]] restart alone SAYS RESTART NEEDED when closed",
                strstr(g_c.page_text, "RESTART NEEDED") != nullptr &&
                strstr(g_c.page_text, mrui::kSettingsEnterText) != nullptr);
            CHK("P14g [[B232]] ...and claims no draft state it has not got",
                strstr(g_c.page_text, "CFG* UNSAVED") == nullptr &&
                strstr(g_c.page_text, "CFG! RELOAD")  == nullptr);
            lv.eff.at(mrfw::CfgField::ble_mode) = 0;
            // unsaved, read from the closed view
            t16 = cfg_walk_to(t16 + 500, ">DM crypt");
            t16 = double_press(t16 + 500); paint(t16);
            t16 = settle(t16 + 500);
            t16 = double_press(t16 + 500); paint(t16);
            t16 = to_cfg_closed(t16 + 500);
            CHK("P14g [[B232]] unsaved SAYS `CFG* UNSAVED` from the closed view",
                strstr(g_c.page_text, "CFG* UNSAVED") != nullptr &&
                strstr(g_c.page_text, mrui::kSettingsEnterText) != nullptr);
            CHK("P14g [[B232]] ...and the badge is there too (it never replaces)",
                rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsUnsaved);
            // conflict, read from the closed view — the remedy the operator has to be able to READ
            st.rec.mobile_autoregister = st.rec.mobile_autoregister ? 0 : 1;
            mr_ui_on_config_saved();
            t16 = to_cfg_closed(t16 + 500);
            CHK("P14g [[B232]] conflict SAYS `CFG! RELOAD` from the closed view",
                strstr(g_c.page_text, "CFG! RELOAD") != nullptr &&
                strstr(g_c.page_text, mrui::kSettingsEnterText) != nullptr);
            CHK("P14g [[B232]] ...and CONFLICT still outranks UNSAVED there",
                strstr(g_c.page_text, "CFG* UNSAVED") == nullptr &&
                rail_glyph_at(kSlotSettings) == mrui::icons::kIconSettingsConflict);
            // ...and the CLEAN cell: back to a clean draft, and the closed view invents nothing
            t16 = cfg_walk_to(t16 + 500, ">DISCARD");
            t16 = double_press(t16 + 500); paint(t16);
            t16 = to_cfg_closed(t16 + 500);
            CHK("P14g [[B232]] clean says nothing it cannot act on, when closed",
                strstr(g_c.page_text, "CFG* UNSAVED") == nullptr &&
                strstr(g_c.page_text, "CFG! RELOAD")  == nullptr &&
                strstr(g_c.page_text, mrui::kSettingsEnterText) != nullptr);
        }
    }

    // ============================================================================================================ P17
    // ★★★★ §UI-17 S3 — THE **PRODUCTION HANDOFF**, AND IT IS THE ONE THING THE PURE SUITE STRUCTURALLY CANNOT SEE.
    //      `test/test_firmware_ui_status.cpp` proves what `mrui::ui_status_*` RETURNS and `--target=uistatus` proves
    //      each substitution is load-bearing — but BOTH call the formatters directly. Delete `status_text(2, l)`
    //      from `draw_status_screen`, or point it at row 1, and every native case stays green, all THIRTEEN
    //      `uistatus` mutations stay RED, and P14's geometry stays green: the panel simply loses a row.
    //      ⇒ this phase drives DISTINCTIVE facts through the REAL node and asserts EVERY row's EXACT BYTES AT ITS
    //      EXACT COORDINATE. That is the [[B226]] discipline (a token the pure suite proves and the production
    //      renderer never shows is not proven), applied at the HANDOFF SEAM.
    // ★★ THE VALUES ARE CHOSEN SO NO TWO ROWS CAN BE CONFUSED FOR ONE ANOTHER: `TEAM 3D9348A5` · `ME T220` ·
    //    `3 KNOWN` · the unread/home line · `52.123,21.456`. No row's text is a substring of another's, so a
    //    misroute cannot pass by coincidence.
    {
        // ---- the fixture, through the core's own public seams (⛔ never a poked snapshot) ----------------------
        uint8_t s_pub[32], s_priv[32];
        for (int i = 0; i < 32; ++i) { s_pub[i] = uint8_t(0xA0 + i); s_priv[i] = uint8_t(0x40 + i); }
        MESHROUTE_NS::NodeConfig scfg{};
        scfg.routing_sf = 7; scfg.allowed_sf_bitmap = (1u << 7); scfg.leaf_id = 0;
        scfg.team_id = 0x3D9348A5u;      // -> `TEAM 3D9348A5`
        scfg.lat_e7  = 521234567;        // ->  52.123
        scfg.lon_e7  = 214567890;        // ->  21.456
        g_node.on_init(scfg);
        g_node.set_team_local_id(220);   // -> `ME T220`
        g_node.team_channel_key_load(s_pub, s_priv, /*present=*/true);
        g_node.test_learn_route(/*dest=*/70, /*via=*/70, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
        g_node.test_learn_route(/*dest=*/71, /*via=*/71, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
        g_node.test_learn_route(/*dest=*/72, /*via=*/72, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);

        uint32_t t18 = settle(1400000);
        t18 = walk_to_slot(t18 + 500, kSlotStatus);
        paint(t18);

        // ---- (a) EVERY ROW, EXACT BYTES AT ITS EXACT COORDINATE ------------------------------------------------
        {
            const char* r[5] = { status_row(0), status_row(1), status_row(2), body_row(3), body_row(4) };
            printf("  INFO §UI-17 STATUS body: [%s] [%s] [%s] [%s] [%s]\n",
                   r[0] ? r[0] : "-", r[1] ? r[1] : "-", r[2] ? r[2] : "-",
                   r[3] ? r[3] : "-", r[4] ? r[4] : "-");
        }
        CHK("P17a STATUS row 0 is `TEAM 3D9348A5` at x=40",  status_row_is(0, "TEAM 3D9348A5"));
        CHK("P17a row 1 is `ME T220` at x=40",               status_row_is(1, "ME T220"));
        // ★★★ ROWS 2 AND 3 ARE ASSERTED AGAINST THE **STRIP's OWN TOKENS**, not against literals, and that is
        //     STRONGER rather than weaker. Spec §2.2 notes d and e both rule that these two rows state the SAME two
        //     facts the strip's people-count and envelope already draw, through the SAME `ui_fmt_team` /
        //     `ui_fmt_mail` tokens, *"so the two surfaces cannot disagree"* (U1). Comparing the surfaces measures
        //     exactly that rule. ⛔ It is not a tautology: the two tokens are produced at DIFFERENT call sites from
        //     DIFFERENT structs (`UiChrome` vs `UiSnapshot`), and a body that stopped agreeing with the strip is
        //     precisely the defect the rule exists to forbid. ⓘ And it is immune to how many routes an earlier
        //     phase happened to leave behind, which a literal would not be.
        // ⓘ The strip's slot coordinates are stated here independently of the renderer's table, as P13's are.
        {
            const char* team = text_at(/*people text_x=*/64, /*strip baseline=*/7);
            const char* mail = text_at(/*mail   text_x=*/ 8, /*strip baseline=*/7);
            char want2[32], want3[40];
            snprintf(want2, sizeof want2, "%s KNOWN", team ? team : "?");
            snprintf(want3, sizeof want3, "%s NEW / HOME --", mail ? mail : "?");
            CHK("P17a row 2 is the STRIP's own team token + KNOWN, at x=40",
                team != nullptr && status_row_is(2, want2));
            CHK("P17a row 3 is the STRIP's own unread token + HOME, at x=12",
                mail != nullptr && body_row_is(3, want3));
        }
        CHK("P17a row 4 is the configured position at x=12", body_row_is(4, "52.123,21.456"));
        // ⛔ ...AND NO ROW IS DRAWN AT THE OTHER SCREEN'S ORIGIN. Without this a renderer that drew row 2 at BOTH
        //    origins would satisfy every check above while overprinting the reserved mark.
        {
            bool split_ok = true;
            for (int row = 0; row <= 2; ++row)
                if (text_at(kBodyXExpected, body_y_expected(row)) != nullptr) split_ok = false;
            for (int row = 3; row <= 4; ++row)
                if (text_at(kStatusTextXExpected, body_y_expected(row)) != nullptr) split_ok = false;
            CHK("P17a ...and no STATUS row is drawn at the other origin", split_ok);
        }

        // ---- (b) ★★★ THE FROZEN FRAME: A POSITION THAT MOVES **BETWEEN PAGES** MAY NOT TEAR THE ROW -------------
        // ⛔⛔ THIS IS THE CONTROL FOR A DEFECT THAT SHIPPED IN S3's FIRST CUT: `draw_status_screen` read
        //     `g_node.config()` LIVE, and `draw_frame` runs ONCE PER OLED PAGE. U8g2 re-clips the WHOLE scene per
        //     page, so a `cfg set lat` landing between two of the eight replays drew half the coordinate row from
        //     each fix. The cure is the snapshot (`own_lat_e7` / `own_lon_e7` / `own_fix`), and THIS is the only
        //     venue in the tree that can see it — the same shape P13d uses for the strip's frozen chrome.
        // ⚠ `mutable_config()` is the core's OWN live-tweak seam — exactly what a device `cfg set lat` writes.
        // ⛔ NO `settle()` ANYWHERE BELOW, AND THAT IS LOAD-BEARING RATHER THAN TIDY: a `short` on STATUS is
        //    ordinary NAVIGATION — it moves the rail to TEAM — so a press between these steps would measure the
        //    wrong screen. The repaints are driven by a PUSH (`dirty_the_model`) plus time past the 2 Hz throttle,
        //    which is exactly how a snapshot-only change reaches the panel on device. ⚠ The whole block stays well
        //    inside `kBlankMs` (15 s) of the walk's last real press, or the panel would blank underneath it.
        {
            t18 += 1000;                                      // past the 500 ms paint throttle
            dirty_the_model(t18);
            run_ticks(t18 + 100, 3, 10);                      // open the frame and push three pages
            const char* p0 = text_at(kBodyXExpected, body_y_expected(4), 0);
            char frozen[24];
            snprintf(frozen, sizeof frozen, "%s", p0 ? p0 : "?");
            CHK("P17b precondition: page 0 drew the ORIGINAL position",
                strcmp(frozen, "52.123,21.456") == 0);
            g_node.mutable_config().lat_e7 = -891234567;      // ⚡ the fix MOVES under the open frame
            g_node.mutable_config().lon_e7 = -1791234567;
            run_ticks(t18 + 140, 6, 10);                      // ...and the remaining pages replay
            bool same_every_page = true;
            for (int p = 0; p < 8; ++p) {
                const char* r = text_at(kBodyXExpected, body_y_expected(4), p);
                if (r == nullptr || strcmp(r, frozen) != 0) same_every_page = false;
            }
            CHK("P17b every page of that frame drew the SAME position row", same_every_page);
            CHK("P17b ...including the pages drawn AFTER the fix moved",
                text_at(kBodyXExpected, body_y_expected(4), 7) != nullptr &&
                strcmp(text_at(kBodyXExpected, body_y_expected(4), 7), "52.123,21.456") == 0);
            // ★ AND THE MOVE IS NOT LOST (§8.3 rule 5 / §B107): the NEXT frame renders the newer position.
            t18 += 1000; dirty_the_model(t18); paint(t18 + 100); t18 += 200;
            CHK("P17b ...and the NEXT frame renders the new position",
                body_row_is(4, "-89.123,-179.123"));
        }

        // ---- (c) THE PUBLISH SITE's OWN TWO ANSWERS, through the real renderer ---------------------------------
        // ⛔ `(0,0)` IS `NO LOCATION`, NEVER `0.000,0.000` — the core refuses a located send there, so the panel
        //    must not claim the Gulf of Guinea. And ONE non-zero coordinate IS a fix: the predicate is an OR,
        //    because that is what the refusal is keyed on. Both are decided at `build_snapshot`'s publish site,
        //    which no native case compiles.
        {
            g_node.mutable_config().lat_e7 = 0;
            g_node.mutable_config().lon_e7 = 0;
            t18 += 1000; dirty_the_model(t18); paint(t18 + 100); t18 += 200;
            CHK("P17c no fix at all renders NO LOCATION, never 0.000,0.000",
                body_row_is(4, "NO LOCATION"));
            g_node.mutable_config().lon_e7 = 214567890;       // on the equator: lat 0, lon set -> STILL a fix
            t18 += 1000; dirty_the_model(t18); paint(t18 + 100); t18 += 200;
            CHK("P17c one non-zero coordinate IS a fix (the predicate is an OR)",
                body_row_is(4, "0.000,21.456"));
        }

        // ---- restore the fixture P15/P16 inherit (P9d's team + P13's content key) ------------------------------
        {
            MESHROUTE_NS::NodeConfig back{};
            back.routing_sf = 7; back.allowed_sf_bitmap = (1u << 7); back.leaf_id = 0;
            back.team_id = 0xABCD1234u;
            g_node.on_init(back);
            g_node.set_team_local_id(50);
            g_node.team_channel_key_load(s_pub, s_priv, /*present=*/true);
            g_node.test_learn_route(/*dest=*/60, /*via=*/60, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            g_node.test_learn_route(/*dest=*/61, /*via=*/61, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            g_node.test_learn_route(/*dest=*/62, /*via=*/62, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            t18 = settle(t18 + 1000);
        }
    }

    // ============================================================================================================ P18
    // ★★★★ §UI-17 S4 — THE **PRODUCTION HANDOFF** FOR THE TEAM ROW, and it is the same seam P17 exists for one screen
    //      over. `test/test_firmware_ui_team.cpp` proves what `mrui::ui_team_row` RETURNS and `--target=uiteam` proves
    //      each field is load-bearing — but BOTH call the formatter directly. Point `draw_team_screen` at the wrong
    //      snapshot row, drop the call, or hand it the wrong marker, and every native case stays green, all SIXTEEN
    //      `uiteam` mutations stay RED, and P14's geometry stays green: the panel simply shows the wrong people.
    //      ⇒ this phase drives THREE DISTINCTIVE teammates through the REAL node — the real label resolver, the real
    //      route ages — and asserts EVERY row's EXACT BYTES AT ITS EXACT COORDINATE ([[B226]]'s discipline).
    // ★★ THE THREE ROWS ARE THE THREE ANSWERS THE LABEL RESOLVER CAN GIVE (`label_for_team_id`), so a misroute cannot
    //    pass by coincidence: a cached NAME (`Wolfgangetta` -> `Wolfga`), a key with NO name (`0x00c0ffee` ->
    //    `0x00c0`) and no key at all (`id 83`). No row's text is a substring of another's, and each carries its own
    //    route age (`3m` / `2m` / `1m`).
    // ⛔ THE AGES ARE EXACT, NOT APPROXIMATE: the probe's clock is deterministic, so each route is STAMPED at a fixed
    //    offset before the frame that reads it (`stamp_min` / the second-scale stamps below). ⚠ `set_now` alone moves
    //    only the HAL clock the core stamps with — no `mr_ui_tick` runs at those instants, so the UI never sees the
    //    backwards step.
    {
        // ---- the fixture, through the core's own public seams (⛔ never a poked snapshot) ----------------------
        // ★ `set_team_id` is the core's OWN team-switch entry point and it drops the previous team's `_rt_team`,
        //   `_team_peer` and `_team_keys` (`clear_team_routing_state`) — which is what makes the roster below
        //   EXACTLY three rows rather than three added to whatever earlier phases left.
        (void)g_node.set_team_id(0x51CE0004u);
        g_node.set_team_local_id(90);

        uint8_t pub81[32], pub82[32];
        for (int i = 0; i < 32; ++i) { pub81[i] = uint8_t(0x10 + i); pub82[i] = uint8_t(0x60 + i); }
        pub82[0] = 0xEE; pub82[1] = 0xFF; pub82[2] = 0xC0; pub82[3] = 0x00;   // -> LE hash 0x00C0FFEE
        const uint32_t hash81 = MESHROUTE_NS::key_hash32_of(pub81);
        const uint32_t hash82 = MESHROUTE_NS::key_hash32_of(pub82);
        const bool named_ok = g_node.peer_key_set(hash81, pub81,
                                                  MESHROUTE_NS::Node::PeerKeyConf::authoritative, "Wolfgangetta", 12);
        CHK("P18 precondition: the named teammate's key and name are cached", named_ok);
        CHK("P18 precondition: the unnamed teammate's hash is 0x00c0ffee", hash82 == 0x00C0FFEEu);

        // ⚠⚠ A HARNESS TRAP, MEASURED RATHER THAN GUESSED, AND IT IS WHY THE ID→HASH BINDINGS ARE RE-STAMPED WITH THE
        //    ROUTES BELOW. `DeviceHal::now()` extends the 32-bit `millis()` into a MONOTONIC 64-bit value, so every
        //    deliberate BACKWARDS `set_now` here reads as a millis WRAP and advances that clock by 2^32 ms (~49.7
        //    days). A `team_key_set` stamped before such a jump then falls past `team_key_of_id`'s 48 h freshness
        //    gate, the resolver drops to the bare id, and the rows would quietly read `id 81` — a green-looking
        //    fixture measuring the WRONG label. ⇒ `stamp` refreshes both bindings after every jump it makes.
        // ⓘ The UI never sees the backwards step: no `mr_ui_tick` runs at those instants, and the model's own clock
        //   is the `now_ms` argument, not the HAL's.
        auto bind_keys = [&]() {
            g_node.team_key_set(81, hash81, MESHROUTE_NS::Node::IdBindSource::bcn,
                                MESHROUTE_NS::Node::IdBindConf::authoritative);
            g_node.team_key_set(82, hash82, MESHROUTE_NS::Node::IdBindSource::bcn,
                                MESHROUTE_NS::Node::IdBindConf::authoritative);
        };
        // stamp(when, a0, a1, a2) — put the three routes at EXACTLY those ages (ms) as of `when`, and leave the HAL
        // clock at `when`. ⓘ `rt_merge` refreshes `last_seen_ms` on a same-next_hop merge whichever direction it
        // moves (`node_routing.cpp`'s metadata-only arm), so a re-stamp is a re-stamp and not a "best wins".
        // ⚠ THE THREE ARE STAMPED **OLDEST FIRST**, for the same clock reason: each BACKWARDS step is read as a wrap,
        //   so the helper takes exactly ONE step back and then walks forward through the rest. Stamping them in
        //   argument order would put a 2^32 ms gap between two rows and the first one would render `old`.
        auto stamp = [&](uint32_t when, uint32_t a0, uint32_t a1, uint32_t a2) {
            struct S { uint8_t id; uint32_t age; } r[3] = { {81, a0}, {82, a1}, {83, a2} };
            for (int i = 0; i < 3; ++i)
                for (int j = i + 1; j < 3; ++j)
                    if (r[j].age > r[i].age) { const S sw = r[i]; r[i] = r[j]; r[j] = sw; }
            for (const S& e : r) {
                set_now(when - e.age);
                g_node.test_learn_route(e.id, e.id, 1, 144, /*team_plane=*/true);
            }
            set_now(when);
            bind_keys();
        };
        // The minute-scale fixture: 200 / 130 / 90 s, each chosen CLEAR of its token's boundary so a stray tick
        // cannot move one.
        auto stamp_min = [&](uint32_t when) { stamp(when, 200000, 130000, 90000); };

        uint32_t t19 = settle(1600000);
        t19 = walk_to_slot(t19 + 500, kSlotTeam);

        // ---- (a) EVERY ROW, EXACT BYTES AT ITS EXACT COORDINATE, on the PASSIVE screen -------------------------
        {
            const uint32_t at = t19 + 1000;
            stamp_min(at);
            dirty_the_model(at);
            paint(at + 100);
            const char* r[4] = { body_row(0), body_row(1), body_row(2), body_row(3) };
            printf("  INFO §UI-17 TEAM rows: [%s] [%s] [%s] [%s]\n",
                   r[0] ? r[0] : "-", r[1] ? r[1] : "-", r[2] ? r[2] : "-", r[3] ? r[3] : "-");
            CHK("P18a row 0 is the NAMED teammate, clamped to six columns",
                body_row_is(0, " Wolfga  3m        "));
            CHK("P18a row 1 is the 0x<hash> label, clamped the same way",
                body_row_is(1, " 0x00c0  2m        "));
            CHK("P18a row 2 is the bare-id fallback, and its own age",
                body_row_is(2, " id 83   1m        "));
            // ⛔ THE TWO RESERVED COLUMNS ARE PART OF EVERY ASSERTION ABOVE — each row is its WHOLE 19 characters,
            //    trailing blanks included, which is what makes S5 a token substitution rather than a re-layout.
            CHK("P18a every drawn row is exactly 19 columns wide",
                r[0] != nullptr && strlen(r[0]) == 19 && r[1] != nullptr && strlen(r[1]) == 19 &&
                r[2] != nullptr && strlen(r[2]) == 19);
            // ⛔ ...AND NO ROW IS DRAWN AT THE **STATUS** ORIGIN. Without this a renderer that drew the roster at
            //    `x = 40` — under S3's reserved mark — would satisfy nothing above and still look plausible.
            bool only_body = true;
            for (int row = 0; row <= 4; ++row)
                if (text_at(kStatusTextXExpected, body_y_expected(row)) != nullptr) only_body = false;
            CHK("P18a ...and no TEAM row is drawn at the STATUS origin", only_body);
            // §UI-17 S1's rule, re-measured through the new format: a PASSIVE preview marks nothing and offers no
            // exit row, so a fourth row must not exist at all.
            // ⛔ READ THROUGH `body_row_unmarked`, ⛔ NEVER `strchr(r[i], …)` ([[B237]]): a mutant that MOVES the body
            //    leaves `text_at` answering `nullptr`, and this check dereferenced it — a crash, which the runner
            //    then scored as a successful control. The helper is fail-closed: no row means no claim.
            CHK("P18a a passive TEAM screen marks no row and draws no BACK",
                body_row_unmarked(0) && body_row_unmarked(1) && body_row_unmarked(2) && r[3] == nullptr);
        }

        // ---- (b) THE ENTERED LIST: the marker moves, and ⛔ nothing else on the line does -----------------------
        {
            uint32_t tb = t19 + 2000;
            stamp_min(tb);
            tb = double_press(tb); paint(tb);
            CHK("P18b entering marks row 0 and moves no other column",
                body_row_is(0, ">Wolfga  3m        "));
            CHK("P18b ...and the last row is the shared BACK row", body_row_is(3, " BACK"));
            // One `short` walks the list — ⛔ it does not leave the screen (the contained-`BACK` rule) — and the
            // marker moves ONE row. ⚠ The rows are re-stamped first so the walk's own ~1.2 s cannot move a token.
            stamp_min(tb + 400);
            tb = settle(tb + 500);
            CHK("P18b a short walks to row 1, which is now the marked one",
                body_row_is(1, ">0x00c0  2m        ") && body_row_is(0, " Wolfga  3m        "));
        }

        // ---- (c) ★★★ THE FROZEN FRAME: A ROUTE AGE THAT MOVES **BETWEEN PAGES** MAY NOT TEAR THE ROW ------------
        // ⛔ The S3 defect's shape, one screen over: `draw_frame` runs ONCE PER OLED PAGE and u8g2 re-clips the whole
        //    scene each time, so a row read live would draw half of one age and half of another. The cure is the
        //    frozen snapshot, and this is the only venue in the tree that can see it.
        // ⛔ NO `settle()` INSIDE THIS BLOCK: a `short` here would walk the list or pass the screen, so the repaint
        //    is driven by a PUSH plus time — exactly how a snapshot-only change reaches the panel on device.
        {
            uint32_t tc = walk_to_slot(t19 + 6000, kSlotTeam);
            stamp_min(tc + 1000);
            dirty_the_model(tc + 1000);
            run_ticks(tc + 1100, 3, 10);                      // open the frame and push three pages
            char frozen[24];
            const char* p0 = text_at(kBodyXExpected, body_y_expected(0), 0);
            snprintf(frozen, sizeof frozen, "%s", p0 ? p0 : "?");
            CHK("P18c precondition: page 0 drew the row the frame froze",
                strcmp(frozen, " Wolfga  3m        ") == 0);
            stamp(tc + 1100, 500000, 130000, 90000);          // ⚡ the route age JUMPS under the open frame
            run_ticks(tc + 1140, 6, 10);                      // ...and the remaining pages replay
            bool same_every_page = true;
            for (int p = 0; p < 8; ++p) {
                const char* r = text_at(kBodyXExpected, body_y_expected(0), p);
                if (r == nullptr || strcmp(r, frozen) != 0) same_every_page = false;
            }
            CHK("P18c every page of that frame drew the SAME team row", same_every_page);
            // ★ AND THE MOVE IS NOT LOST (§8.3 rule 5 / §B107): the NEXT frame renders the newer age.
            uint32_t tn = tc + 2200;
            dirty_the_model(tn); paint(tn + 100);
            CHK("P18c ...and the NEXT frame renders the newer route age",
                body_row_is(0, " Wolfga  8m        "));
        }

        // ---- (d) ★★★★ §1.9 F-8 — A LIT TEAM SCREEN'S AGES **TURN**, WITH NO PRESS AND NO PUSH ------------------
        // ⛔⛔ THIS IS THE PRE-EXISTING GAP THE SLICE CLOSES, AND IT IS COUNTED RATHER THAN ARGUED: before S4 the only
        //    invalidation in the tree compared the CHROME projection, which carries no per-row body token, so
        //    `FrameGate::step` answered `idle` for ever and the panel sat on a stale age. Every check below runs
        //    with ⛔ NO gesture and ⛔ NO push — only the clock.
        {
            uint32_t td = walk_to_slot(t19 + 12000, kSlotTeam) + 1000;
            // Row 0 is stamped in SECONDS (its token turns every second); rows 1 and 2 stay in minutes.
            stamp(td, 12000, 130000, 90000);
            dirty_the_model(td); paint(td + 100);
            CHK("P18d precondition: the lit TEAM screen shows a 12s route age",
                body_row_is(0, " Wolfga 12s        "));
            const int frames0 = g_c.begin_frame;
            run_ticks(td + 1200, 10, 10);                     // one second on, and NOTHING else has happened
            CHK("P18d ★ the age turns on a LIT panel with no press and no push",
                g_c.begin_frame == frames0 + 1 && body_row_is(0, " Wolfga 13s        "));
            // ⓘ A reference paint whose NEXT token turn lands inside the 2 Hz window ...
            run_ticks(td + 2900, 10, 10);
            CHK("P18d ...a second turn repaints too (the rule is not one-shot)",
                g_c.begin_frame == frames0 + 2 && body_row_is(0, " Wolfga 14s        "));
            // ⛔ ...AND THE THROTTLE IS STILL FREE TO REFUSE IT. The invalidation only ever ASKS for a paint; the
            //    MAC-idle gate and the 2 Hz throttle inside `FrameGate::step` decide.
            const int frames2 = g_c.begin_frame;
            run_ticks(td + 3050, 5, 10);                      // the token HAS turned, ~150 ms after that paint
            CHK("P18d ⛔ the 2 Hz throttle still REFUSES a turn inside its window",
                g_c.begin_frame == frames2 && body_row_is(0, " Wolfga 14s        "));
            run_ticks(td + 3600, 10, 10);                     // past the throttle — the request was not lost
            CHK("P18d ...and the refused request is not lost, it paints next",
                g_c.begin_frame == frames2 + 1 && body_row_is(0, " Wolfga 15s        "));
        }

        // ---- (d2) ⛔ AND A RAW AGE THAT MOVES INSIDE ITS BUCKET ASKS FOR NOTHING ---------------------------------
        // ★ Without this the fix above would be indistinguishable from "repaint every tick", which is the tempting
        //   wrong implementation and would cost the panel its 2 Hz ceiling for a screen that did not change.
        {
            uint32_t te = walk_to_slot(t19 + 20000, kSlotTeam) + 1000;
            stamp_min(te);                                    // 3m / 2m / 1m — every token far from its boundary
            dirty_the_model(te); paint(te + 100);
            CHK("P18d2 precondition: the panel is lit on the three minute-aged rows",
                body_row_is(0, " Wolfga  3m        ") && body_row_is(2, " id 83   1m        "));
            const int frames = g_c.begin_frame;
            const int cmds   = g_c.bus_cmds();
            run_ticks(te + 1200, 200, 10);                    // 2 s of ticks, no press, no push, no token turn
            CHK("P18d2 ⛔ while every token holds, no frame opens at all", g_c.begin_frame == frames);
            CHK("P18d2 ...and the panel bus is not touched either", g_c.bus_cmds() == cmds);
        }

        // ---- (e) ⛔ AND A **DARK** PANEL IS NOT WOKEN BY ANY OF IT, SO SLEEP IS UNAFFECTED ------------------------
        // ★★ `FrameGate::step` tests `blanked` FIRST and never examines `dirty`, so an invalidation raised while dark
        //    changes nothing observable — but that is a claim about a code path, and this measures it: no frame, no
        //    bus command, and `mr_ui_allows_sleep()` true across a window in which the token turns every second.
        {
            uint32_t tk = walk_to_slot(t19 + 26000, kSlotTeam) + 1000;
            stamp(tk, 12000, 130000, 90000);
            dirty_the_model(tk); paint(tk + 100);
            CHK("P18e precondition: the panel is lit on TEAM", body_row_is(0, " Wolfga 12s        "));
            run_ticks(tk + 200, 170, 100);                    // 17 s with NO press: the attention window expires
            CHK("P18e the panel blanked with no press at all", g_c.last_power_save == 1);
            const int frames = g_c.begin_frame, cmds = g_c.bus_cmds();
            for (int i = 0; i < 60; ++i) tick(tk + 17400 + uint32_t(i) * 100);   // 6 s: six token turns, in the dark
            CHK("P18e ⛔ a dark TEAM screen opens no frame however many tokens turn",
                g_c.begin_frame == frames && g_c.bus_cmds() == cmds);
            // ⓘ THE SLEEP HALF IS MEASURED IN THE NATIVE SUITE, NOT HERE, AND THE REASON IS THIS BINARY's OWN STATE:
            //   P10h deliberately latches `mr_ui_allows_sleep()` OFF for the whole boot (the fail-closed hardware
            //   case), so every later phase would read `false` whatever the UI did — an instrument that cannot fail.
            //   `test/test_firmware_ui_team.cpp`'s blanked case drives `ui_allows_sleep` directly and requires TRUE
            //   across the same token turns. What IS attributable here is the two counters above: no frame opened, so
            //   `frame_open()` — the term that would have taken sleep away — never became true.
            // ★ THE WAKE PRESS IS CONSUMED and the frame it opens shows the CURRENT age — ⛔ never the one the panel
            //   went dark on. The route is re-stamped so the expected token is exact rather than approximate.
            // ⚠ STAMPED **BEFORE** `tw`, deliberately: `settle` ticks at `tw` and the stamp helper must not leave
            //   the HAL clock ahead of that, or the backwards step would advance the monotonic epoch again (see the
            //   clock note at the top of this phase) and every row would read `49d` from a bare id.
            const uint32_t tw = tk + 24000;
            stamp(tw - 1000, 200000, 130000, 90000);          // -> a MINUTE-scale age, so the expected token
            //   cannot depend on WHICH tick of the wake opens the frame: `3m` spans 180..239 s and the frame opens
            //   inside one second of `tw`. ⛔ And it is a token the dark frame could not have been showing — that
            //   one froze on `12s` — so "the CURRENT age" is what this assertion actually distinguishes.
            t19 = settle(tw);
            CHK("P18e the wake paints the CURRENT route age, not the dark one",
                body_row_is(0, " Wolfga  3m        "));
        }

        // ---- restore the fixture the later phases inherit (P17's own restore, verbatim) -------------------------
        // ⚠ THE TEAM PLANE IS DROPPED FIRST, through the core's own verb, or this phase's three teammates would
        //   linger in the roster every later phase reads.
        {
            uint8_t s_pub[32], s_priv[32];
            for (int i = 0; i < 32; ++i) { s_pub[i] = uint8_t(0xA0 + i); s_priv[i] = uint8_t(0x40 + i); }
            (void)g_node.set_team_id(0);
            MESHROUTE_NS::NodeConfig back{};
            back.routing_sf = 7; back.allowed_sf_bitmap = (1u << 7); back.leaf_id = 0;
            back.team_id = 0xABCD1234u;
            g_node.on_init(back);
            g_node.set_team_local_id(50);
            g_node.team_channel_key_load(s_pub, s_priv, /*present=*/true);
            g_node.test_learn_route(/*dest=*/60, /*via=*/60, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            g_node.test_learn_route(/*dest=*/61, /*via=*/61, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            g_node.test_learn_route(/*dest=*/62, /*via=*/62, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            t19 = settle(t19 + 1000);
        }
        (void)t19;
    }

    // ============================================================================================================ P19
    // ★★★★ §UI-17 S5 — THE **PRODUCTION HANDOFF** FOR THE LOCATION COLUMNS, and it is the [[B226]] seam again:
    //      `test/test_firmware_ui_geo.cpp` proves what `mrui::ui_geo_*` DECIDES and `--target=uigeo` proves each term
    //      is load-bearing — but every one of those instruments calls the pure unit DIRECTLY. Stop publishing
    //      `peer_loc_find`'s answer, look it up under the wrong identity, hand the row a "fresh" age or a hard-wired
    //      own fix, and every native case stays green, all 16 `uigeo` + 20 `uiteam` mutations stay RED, and the panel
    //      shows a distance that is nobody's. `src/firmware_ui.cpp` is compiled by neither the native suite nor the
    //      simulator (§B115), so this phase is the only venue that can see any of it.
    // ★★ FOUR TEAMMATES, FOUR DIFFERENT ANSWERS, AND NO TWO ROWS CAN BE CONFUSED FOR ONE ANOTHER:
    //      · `Alpha` — a FRESH cached position ~2 km north  ⇒ `2.0k  N`
    //      · `Bravo` — a position **601 s** old             ⇒ ⛔ BOTH COLUMNS BLANK (⛔ never a plausible number)
    //      · `Cleo`  — no cached position at all            ⇒ BLANK (⛔ never `0m`)
    //      · `Delta` — a position IDENTICAL to ours         ⇒ `0m` and a BLANK direction (the owner's ruling, on glass)
    // ⛔ THE POSITIONS ARE SEEDED THROUGH `Node::peer_loc_set` — THE CORE's OWN SEAM, the same one the authenticated
    //    receive paths call — ⛔ never by poking a snapshot. So what this phase measures is the WHOLE chain: the
    //    cache, the id->hash resolution, the publish site, the frozen frame and the renderer.
    //
    // ⚠⚠⚠ A HARNESS TRAP, MEASURED RATHER THAN GUESSED, AND IT IS WHY EVERY SUB-PHASE RE-STAMPS AFTER ITS WALK.
    //     `walk_to_slot` PAINTS AT A TIME THE CLOCK HAS ALREADY PASSED: `leave_list` returns straight after its own
    //     `paint(t)` (which ticks t .. t+90) and the caller then paints at that same `t` — a 90 ms BACKWARDS step,
    //     which `ArduinoClock` reads as a millis WRAP and answers by adding 2^32 ms (~49.7 days) to the monotonic
    //     epoch (`lib/hal/iclock.h`'s `accumulate_millis_wrap`). ⓘ It is PRE-EXISTING and harmless to the shipped
    //     firmware (a real `millis()` never steps back), and this run crosses **137** such epochs before reaching
    //     here. ⛔ BUT ANY NODE-SIDE STAMP TAKEN BEFORE A WALK IS 49 DAYS OLD AFTER IT: the route ages render `49d`,
    //     `team_key_of_id` falls past its 48 h gate so every label drops to the bare id, and every cached position is
    //     past its freshness bound. **MEASURED: that is exactly how this phase failed first.**
    //  ⇒ THE RULE THIS PHASE FOLLOWS, and P18's `stamp` helper follows the same one for the same reason:
    //     **walk FIRST, stamp SECOND, and never walk between a stamp and the frame that reads it.**
    //     Waking a blanked panel is safe (a wake press paints where it stood — §B107) and is how each sub-phase gets
    //     its lit frame without navigating.
    {
        // ---- the fixture, through the core's own public seams --------------------------------------------------
        (void)g_node.set_team_id(0x51CE0005u);
        g_node.set_team_local_id(91);
        g_node.mutable_config().lat_e7 = 520000000;      // 52.0000000 N — OUR fix, the one `own_fix` is derived from
        g_node.mutable_config().lon_e7 = 210000000;      // 21.0000000 E

        uint8_t pub[4][32];
        uint32_t hash[4];
        const char* names[4] = { "Alpha", "Bravo", "Cleo", "Delta" };
        bool keys_ok = true;
        for (int k = 0; k < 4; ++k) {
            for (int i = 0; i < 32; ++i) pub[k][i] = uint8_t(0x20 + k * 0x30 + i);
            hash[k] = MESHROUTE_NS::key_hash32_of(pub[k]);
            if (!g_node.peer_key_set(hash[k], pub[k], MESHROUTE_NS::Node::PeerKeyConf::authoritative,
                                     names[k], uint8_t(strlen(names[k])))) keys_ok = false;
        }
        CHK("P19 precondition: four teammate keys and names are cached", keys_ok);

        // ★★★ THE FIXTURE'S WHOLE TIMELINE, AND EVERY OFFSET IN IT IS LOAD-BEARING (⛔ forward only, see the trap):
        //       base + 0        the four routes and the four id->hash bindings
        //       base + 3600000  `Bravo`'s position                       ⇒ 601 s old at the frame — ⛔ ONE SECOND
        //                                                                  past `kPeerLocMaxAgeS`, the ruled edge
        //       base + 4201000  `Alpha`'s and `Delta`'s positions, and THE FRAME
        //     ⇒ every row's ROUTE age is 4201 s, which the ruled table draws `1h` — and it stays `1h` until 7200 s,
        //     so no expected row below can depend on how long a walk took. ⛔ The alternative, a second-scale route
        //     age, is a fixture measuring itself. The AGE column is S4's and is pinned second-by-second at P18.
        // ⓘ `Cleo` (hash[2]) is DELIBERATELY never given a position — the cache-miss row.
        auto stamp_all = [&](uint32_t base) -> uint32_t {
            set_now(base);
            for (int k = 0; k < 4; ++k) {
                g_node.test_learn_route(uint8_t(84 + k), uint8_t(84 + k), 1, 144, /*team_plane=*/true);
                g_node.team_key_set(uint8_t(84 + k), hash[k], MESHROUTE_NS::Node::IdBindSource::bcn,
                                    MESHROUTE_NS::Node::IdBindConf::authoritative);
            }
            set_now(base + 3600000);
            (void)g_node.peer_loc_set(hash[1], 520000000 + 184000, 210000000,
                                      MESHROUTE_NS::Node::PeerLocSrc::peer);        // Bravo — will be 601 s old
            set_now(base + 4201000);
            (void)g_node.peer_loc_set(hash[0], 520000000 + 184000, 210000000,
                                      MESHROUTE_NS::Node::PeerLocSrc::peer);        // Alpha — ~2 km north, fresh
            (void)g_node.peer_loc_set(hash[3], 520000000, 210000000,
                                      MESHROUTE_NS::Node::PeerLocSrc::team);        // Delta — exactly where we are
            return base + 4201000;
        };

        uint32_t t20 = settle(1700000);
        t20 = walk_to_slot(t20 + 500, kSlotTeam);

        // ---- (a) EVERY ROW, EXACT BYTES AT ITS EXACT COORDINATE --------------------------------------------------
        {
            // ⛔ THE WAKE PRESS IS THE ONLY GESTURE HERE, and it is what makes the frame LIT without navigating: the
            //    4 201 s the fixture jumps blanks the panel, and §B107's wake paints where the operator left it.
            const uint32_t at = stamp_all(t20 + 1000);
            t20 = settle(at);
            paint(t20 + 100);
            const char* r[4] = { body_row(0), body_row(1), body_row(2), body_row(3) };
            printf("  INFO §UI-17 S5 TEAM rows: [%s] [%s] [%s] [%s]\n",
                   r[0] ? r[0] : "-", r[1] ? r[1] : "-", r[2] ? r[2] : "-", r[3] ? r[3] : "-");
            CHK("P19a a FRESH cached position draws the distance and the octant",
                body_row_is(0, " Alpha   1h 2.0k  N"));
            // ⛔⛔ THE ONE THAT MATTERS MOST, and it is ONE SECOND past the bound: the position is still in the
            //     cache, the peer is still on the roster, and the panel says NOTHING about where they are. A stale
            //     fix rendered as a current one is worse than no fix, because the operator acts on it (C2).
            CHK("P19a ⛔ a 601 s old position renders BLANK, never a number",
                body_row_is(1, " Bravo   1h        "));
            CHK("P19a a peer with NO cached position blanks too (⛔ never 0m)",
                body_row_is(2, " Cleo    1h        "));
            // ★★★★ THE OWNER'S COINCIDENT RULING, ON GLASS: a zero-length vector has no bearing, so the direction
            //      column is blank beside a perfectly valid `0m`. ⛔ FAIL on `N` — a fabricated cardinal.
            CHK("P19a ★ a COINCIDENT teammate draws `0m` and a BLANK direction",
                body_row_is(3, " Delta   1h   0m   "));
            CHK("P19a every drawn row is still exactly 19 columns wide",
                r[0] != nullptr && strlen(r[0]) == 19 && r[1] != nullptr && strlen(r[1]) == 19 &&
                r[2] != nullptr && strlen(r[2]) == 19 && r[3] != nullptr && strlen(r[3]) == 19);
            t20 += 2000;
        }

        // ---- (b) ⛔⛔ RENDERING TEAM CREATES **NO TRAFFIC OF ANY KIND** (spec §3.4) -------------------------------
        // ★★★ COUNTED, NOT ARGUED, and both counters are the REAL ones: `g_hal.txq_depth()` is the real DeviceHal
        //     queue P2b moves, and `g_probe_radio.starts` is the real radio's start count. A full TEAM walk —
        //     entering the interactive list, walking every row, leaving it and coming back — must move NEITHER.
        // ⓘ This is the automated half of bench §7.3 step 6, and it is the stronger half: on metal a scheduled beacon
        //   can never be told apart from a panel-driven send without a five-minute baseline.
        {
            const int s0 = g_probe_radio.starts;
            // ⛔ THE BEFORE-CHECK IS NOT CEREMONY: sub-phase (a) has ALREADY drawn this roster several times, so a
            //    renderer that asks for a position would have filled the queue before the walk even begins — and a
            //    bare "the depth did not CHANGE" would then be satisfied by a queue that is merely still full.
            CHK("P19b precondition: nothing is queued before the TEAM walk", g_hal.txq_depth() == 0);
            uint32_t tb = t20 + 1000;
            tb = double_press(tb); paint(tb);                 // enter the interactive list
            for (int i = 0; i < 5; ++i) tb = settle(tb + 500); // walk every row, including BACK
            tb = walk_to_slot(tb + 500, kSlotTeam);           // ...and back onto the screen from the rail
            paint(tb + 500);
            CHK("P19b ⛔ a full TEAM walk enqueues NOTHING (the real queue)",
                g_hal.txq_depth() == 0);
            // ★★ AND THE SECOND COUNTER IS NOT A DUPLICATE OF THE FIRST: a queue read alone cannot see a frame that
            //    was queued AND already drained. ⇒ the two halves of the real service path are run — exactly as P2b
            //    runs them — and the radio must STILL never have started. ⓘ On a clean tree there is nothing to
            //    pump, which is the point.
            g_hal.collect_tx_completion(); g_hal.pump_tx();
            CHK("P19b ⛔ ...and pumping the queue starts NO transmission",
                g_probe_radio.starts == s0);
            t20 = tb + 1000;
        }

        // ---- (c) ★★★ THE FROZEN FRAME: OUR OWN FIX MOVING **BETWEEN PAGES** MAY NOT TEAR THE ROWS ----------------
        // ⛔ THE S3 DEFECT'S SHAPE, ONE COLUMN OVER, and it is why the own fix is read ONCE from the frozen snapshot:
        //    `draw_team_screen` runs ONCE PER OLED PAGE and u8g2 re-clips the whole scene each time, so a `cfg set
        //    lat` landing between two of the eight replays would draw half the roster from one position and half
        //    from another. ⛔ NO `settle()` INSIDE the block below: a press would walk the list; the repaint is
        //    driven by a PUSH plus time, exactly as it is on device.
        {
            uint32_t tc = walk_to_slot(t20, kSlotTeam);
            tc = settle(stamp_all(tc + 1000));                // ...walk FIRST, stamp SECOND (the trap above)
            paint(tc + 100);
            char frozen[24];
            tc += 1000;
            dirty_the_model(tc);
            run_ticks(tc + 100, 3, 10);                       // open the frame and push three pages
            const char* p0 = text_at(kBodyXExpected, body_y_expected(0), 0);
            snprintf(frozen, sizeof frozen, "%s", p0 ? p0 : "?");
            CHK("P19c precondition: page 0 drew the row the frame froze",
                strcmp(frozen, " Alpha   1h 2.0k  N") == 0);
            g_node.mutable_config().lat_e7 = 520000000 + 900000;   // ⚡ OUR fix MOVES under the open frame (~10 km)
            run_ticks(tc + 140, 6, 10);                            // ...and the remaining pages replay
            bool same_every_page = true;
            for (int p = 0; p < 8; ++p) {
                const char* r = text_at(kBodyXExpected, body_y_expected(0), p);
                if (r == nullptr || strcmp(r, frozen) != 0) same_every_page = false;
            }
            CHK("P19c every page of that frame drew the SAME distance", same_every_page);
            // ★ AND THE MOVE IS NOT LOST (§8.3 rule 5 / §B107): the NEXT frame renders the new distance — Alpha is
            //   now ~8 km SOUTH of us rather than 2 km north.
            const uint32_t tn = tc + 1400;
            dirty_the_model(tn); paint(tn + 100);
            CHK("P19c ...and the NEXT frame renders the distance from the NEW fix",
                body_row_is(0, " Alpha   1h 7.9k  S"));
            g_node.mutable_config().lat_e7 = 520000000;
            t20 = tn + 1000;
        }

        // ---- (d) ★★★★ THE POSITION GOING STALE REPAINTS THE PANEL BY ITSELF (§1.9 F-8, extended by S5) -----------
        // ⛔ NO PRESS AND NO PUSH once the panel is lit: the only thing that changes is the CLOCK crossing the 600 s
        //    bound, and the columns must go blank on their own. Without the geo half of `ui_team_invalidate` the
        //    panel would keep showing a distance that is no longer true until something unrelated repainted it —
        //    which is the whole reason the bound exists.
        // ⚠ THE ROUTE AGE IS DELIBERATELY PARKED MID-BUCKET (`1h` spans 3 600..7 199 s) SO IT CANNOT TURN INSIDE THIS
        //   WINDOW: if it could, the repaint would be attributable to the AGE bucket and this check would prove
        //   nothing about the location columns.
        {
            uint32_t td = walk_to_slot(t20, kSlotTeam);
            const uint32_t base = td + 1000;
            set_now(base);
            for (int k = 0; k < 4; ++k) {
                g_node.test_learn_route(uint8_t(84 + k), uint8_t(84 + k), 1, 144, /*team_plane=*/true);
                g_node.team_key_set(uint8_t(84 + k), hash[k], MESHROUTE_NS::Node::IdBindSource::bcn,
                                    MESHROUTE_NS::Node::IdBindConf::authoritative);
            }
            set_now(base + 3600000);
            CHK("P19d precondition: Alpha's position is re-stamped exactly",
                g_node.peer_loc_set(hash[0], 520000000 + 184000, 210000000,
                                    MESHROUTE_NS::Node::PeerLocSrc::peer));
            td = settle(base + 4194000);                      // the position is 594 s old — still inside the bound
            paint(td + 100);
            CHK("P19d precondition: the LIT screen shows Alpha's distance",
                body_row_is(0, " Alpha   1h 2.0k  N"));
            const int frames0 = g_c.begin_frame;
            for (uint32_t at = td + 300; at <= base + 4201500; at += 200) tick(at);   // ⛔ the clock, and nothing else
            CHK("P19d ★ the columns go BLANK on the clock alone, no press",
                g_c.begin_frame > frames0 && body_row_is(0, " Alpha   1h        "));
            t20 = base + 4203000;
        }

        // ---- restore the fixture the later phases inherit (P17's own restore, verbatim) -------------------------
        {
            uint8_t s_pub[32], s_priv[32];
            for (int i = 0; i < 32; ++i) { s_pub[i] = uint8_t(0xA0 + i); s_priv[i] = uint8_t(0x40 + i); }
            (void)g_node.set_team_id(0);
            MESHROUTE_NS::NodeConfig back{};
            back.routing_sf = 7; back.allowed_sf_bitmap = (1u << 7); back.leaf_id = 0;
            back.team_id = 0xABCD1234u;
            g_node.on_init(back);
            g_node.set_team_local_id(50);
            g_node.team_channel_key_load(s_pub, s_priv, /*present=*/true);
            g_node.test_learn_route(/*dest=*/60, /*via=*/60, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            g_node.test_learn_route(/*dest=*/61, /*via=*/61, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            g_node.test_learn_route(/*dest=*/62, /*via=*/62, /*hops=*/1, /*snr_q4=*/144, /*team_plane=*/true);
            t20 = settle(t20 + 1000);
        }
        (void)t20;
    }

    // ============================================================================================================ P20
    // ★★★★ §UI-17 S8 — **WAKE ON RECEIVE, THROUGH THE PRODUCTION SEAM**, and it is the [[B226]] handoff again:
    //      `test_firmware_ui_send.cpp` proves which push the pure router lets through and `--target=uisend` proves the
    //      `enc` gate is load-bearing, but every one of those instruments calls the pure unit DIRECTLY. `mr_ui_on_push`
    //      (`src/firmware_ui.cpp`) is compiled by neither the native suite nor the simulator (§B115), so a wake wired
    //      in THERE — or a recv arm that stopped reaching the router at all — is invisible to all of them. This phase
    //      is the only venue that can see the PANEL actually light, and the only one that can see it NOT light.
    // ★★ THE FOUR ARMS, and the negative pair is the half that would be forgotten:
    //      · a DM (`msg_recv`)                         ⇒ the panel LIGHTS, on the screen that was current
    //      · a SEALED team post (`channel_recv`+`enc`) ⇒ the panel LIGHTS
    //      · ⛔ a CLEARTEXT post (the SAME push, `enc` false) ⇒ it stays DARK, in the P13f zero-bus shape
    //      · ⛔ every OTHER push kind                        ⇒ it stays DARK, same shape
    // ⓘ THE ZERO-BUS MEASUREMENT IS `bus_cmds()`, NOT `bus_ops()` (§CHROME-3): the board LATCHES `set_power_save`, so
    //   the per-blanked-tick call is a genuine no-op and counting CALLS would fail a correct implementation.
    // ⛔ THE SEQUENCE ALWAYS BEGINS AFTER THE BLANKING EDGE HAS COMPLETED, exactly as P13f's does — the first
    //   `set_power_save(true)` legitimately issues one panel command, and counting it would measure the harness.
    {
        uint32_t t22 = settle(1800000);
        t22 = walk_to_slot(t22 + 500, kSlotTeam);            // ⛔ NOT the landing screen: "no navigation" is only
        paint(t22);                                          //   measurable from a screen a push could move us off
        CHK("P20 precondition: the panel is LIT on TEAM",
            g_c.last_power_save != 1 && rail_boxed_slot() == kSlotTeam);

        // Take the panel dark WITHOUT a press, and let the power-save edge complete. Returns the time it left behind.
        auto go_dark = [&](uint32_t at) -> uint32_t {
            tick(at + 16000);                                // > kBlankMs (15 s) since the last input OR message
            tick(at + 16010);
            return at + 16020;
        };
        // A push as `fw_main`'s drain delivers it. ⓘ `team` is `Push::team_id` (node_channel.cpp:413); the restored
        //   fixture's team is `0xABCD1234`, so a post tagged with it is OUR team's — which is what the operator's
        //   sealed traffic looks like. ⛔ The WAKE does not read it: the gate is `pu.enc` and the kind, nothing else.
        auto post = [](bool enc) {
            MESHROUTE_NS::Push pu{};
            pu.kind = MESHROUTE_NS::PushKind::channel_recv;
            pu.channel_id = uint8_t(MR_UI_TEAM_CHANNEL_ID); pu.team_id = 0xABCD1234u; pu.origin = 61;
            pu.enc = enc;
            pu.body[0] = 'w'; pu.body[1] = 'a'; pu.body[2] = 'k'; pu.body[3] = 'e'; pu.body_len = 4;
            return pu;
        };
        auto dm = [](bool enc) {
            MESHROUTE_NS::Push pu{};
            pu.kind = MESHROUTE_NS::PushKind::msg_recv;
            pu.origin = 61; pu.sender_hash = 0xC0FFEE01u; pu.enc = enc;
            pu.body[0] = 'h'; pu.body[1] = 'i'; pu.body_len = 2;
            return pu;
        };

        // ---- (a) A DM LIGHTS THE DARK PANEL, ON THE SCREEN THAT WAS CURRENT ------------------------------------
        {
            t22 = go_dark(t22);
            CHK("P20a precondition: the panel went dark with no press", g_c.last_power_save == 1);
            const int pwr0 = g_c.power_cmds, frames0 = g_c.begin_frame;
            set_now(t22 + 100);
            mr_ui_on_push(dm(/*enc=*/false));                // ⛔ UNSEALED, and it must still wake: it is ours
            run_ticks(t22 + 200, 12, 10);
            CHK("P20a a DM lights the panel by itself, no press",
                g_c.last_power_save == 0 && g_c.power_cmds == pwr0 + 1 && g_c.begin_frame > frames0);
            CHK("P20a ...on the SCREEN THAT WAS CURRENT — a push navigates nothing",
                rail_boxed_slot() == kSlotTeam && body_row(0) != nullptr);
            t22 += 1000;
        }

        // ---- (b) A SEALED TEAM POST DOES THE SAME ----------------------------------------------------------------
        {
            t22 = go_dark(t22);
            CHK("P20b precondition: the panel is dark again", g_c.last_power_save == 1);
            const int pwr0 = g_c.power_cmds;
            set_now(t22 + 100);
            mr_ui_on_push(post(/*enc=*/true));
            run_ticks(t22 + 200, 12, 10);
            CHK("P20b a SEALED team post lights the panel, still on TEAM",
                g_c.last_power_save == 0 && g_c.power_cmds == pwr0 + 1 && rail_boxed_slot() == kSlotTeam);
            t22 += 1000;
        }

        // ---- (c) ★★★ THE DISCRIMINATOR — THE SAME POST, CLEARTEXT, LEAVES THE PANEL DARK -------------------------
        // ⛔⛔ THIS IS THE ARM THAT PROVES THE GATE, and it is the one a hurried slice omits. Byte-for-byte the same
        //     push as (b) with `enc` false — the case §R1/[[B109]]'s *"a stranger's post does not light a dark panel"*
        //     (bench §8.15) is about, and the reason S8 reverses nothing.
        {
            t22 = go_dark(t22);
            CHK("P20c precondition: the panel is dark before the cleartext post", g_c.last_power_save == 1);
            const int bus0 = g_c.bus_cmds(), pwr0 = g_c.power_cmds;
            const int frames0 = g_c.begin_frame, pages0 = g_c.next_page;
            set_now(t22 + 100);
            mr_ui_on_push(post(/*enc=*/false));
            run_ticks(t22 + 200, 40, 100);                   // four seconds of ticks: no wake may appear late either
            CHK("P20c ⛔ a CLEARTEXT post does NOT light the panel",
                g_c.last_power_save == 1 && g_c.power_cmds == pwr0);
            CHK("P20c ...issuing ZERO bus commands, opening no frame and paging nothing",
                g_c.bus_cmds() == bus0 && g_c.begin_frame == frames0 && g_c.next_page == pages0);
            t22 += 5000;
        }

        // ---- (d) ⛔ NOTHING ELSE WAKES IT — THE FULL PushKind ENUM, MINUS THE ONE KIND THAT MAY -------------------
        // ★ THE BOUND IS `push_kind_count()`, i.e. the COMPILER'S answer rather than a literal — see that function for
        //   the whole argument and for the withdrawn claim it replaces. A kind appended to `command.h` breaks THIS
        //   BINARY'S BUILD at the roster switch, so it cannot fall outside this sweep unnoticed.
        // ⓘ Every push is the DEFAULT one, i.e. `enc == false`, so `channel_recv` appears here as the cleartext case
        //   a second time — deliberately. ⓘ The `>= 17` is a FLOOR (the enum only ever appends), there to prove the
        //   sweep is not vacuous; ⛔ it pins no roster and a new kind must never need it edited.
        {
            t22 = go_dark(t22);
            CHK("P20d precondition: the panel is dark before the other push kinds", g_c.last_power_save == 1);
            const int bus0 = g_c.bus_cmds(), pwr0 = g_c.power_cmds;
            const int frames0 = g_c.begin_frame, pages0 = g_c.next_page;
            const uint8_t n_kinds = push_kind_count();
            int driven = 0;
            for (uint8_t k = 0; k < n_kinds; ++k) {
                if (MESHROUTE_NS::PushKind(k) == MESHROUTE_NS::PushKind::msg_recv) continue;
                MESHROUTE_NS::Push pu{};
                pu.kind = MESHROUTE_NS::PushKind(k);
                set_now(t22 + 100 + uint32_t(driven) * 10);
                mr_ui_on_push(pu);
                ++driven;
            }
            run_ticks(t22 + 1000, 40, 100);
            CHK("P20d the whole enum was driven, one kind excepted",
                driven == int(n_kinds) - 1 && n_kinds >= 17);
            CHK("P20d ⛔ no other push kind wakes the panel",
                g_c.last_power_save == 1 && g_c.power_cmds == pwr0);
            CHK("P20d ...and none of them touched the bus, a frame or a page",
                g_c.bus_cmds() == bus0 && g_c.begin_frame == frames0 && g_c.next_page == pages0);
            t22 += 6000;
        }

        // ---- (e) THE WOKEN PANEL RE-BLANKS ON THE **MESSAGE's** OWN DEADLINE, with no press anywhere in the phase --
        // ★ Spec pin 9 on glass: the wake buys ONE attention window measured from the message, and then the node goes
        //   back to exactly what it was doing. ⛔ A wake that never re-blanked would be the F-10 power regression.
        {
            t22 = go_dark(t22);
            const uint32_t msg_at = t22 + 100;
            set_now(msg_at);
            mr_ui_on_push(dm(/*enc=*/true));
            run_ticks(msg_at + 100, 12, 10);
            CHK("P20e precondition: a sealed DM woke it too", g_c.last_power_save == 0);
            for (uint32_t at = msg_at + 1000; at <= msg_at + 14000; at += 500) tick(at);
            CHK("P20e ...and it is STILL lit most of a window later", g_c.last_power_save == 0);
            tick(msg_at + 15000); tick(msg_at + 15010);      // kBlankMs after the MESSAGE
            CHK("P20e ...then blanks by itself, on the message's own deadline", g_c.last_power_save == 1);
            t22 = msg_at + 16000;
        }
        (void)t22;
    }

    // ============================================================================================================ P15
    // ★★★★ §UI-15 slice 5 / [[B225]] — THE PROVISIONING SUB-VIEW, DRIVEN THROUGH THE REAL `draw_provision_screen`.
    //      ⛔ IT RUNS ON THE CHILD-ENABLED ARM ONLY, and that is the whole of the correction: with `-DMR_N_LAYERS=2`
    //      both children hide (`build_snapshot`'s two predicates), so `draw_provision_screen`
    //      (`src/firmware_ui.cpp:1143`) — real shipped rendering — is STRUCTURALLY UNREACHABLE and the slice's screens
    //      were measured by nothing. `run.sh` therefore compiles this file and `firmware_ui.cpp` a SECOND time with the
    //      exact `-D` set of `[env:heltec_v3]`, which is a leaf env with the panel (`MR_FEAT_OLED=1`) and the team
    //      plane (no `MR_PROFILE_*` ⇒ `MR_FEAT_TEAM=1`), and runs this phase there.
    // ★★ WHAT IS REAL HERE AND WHAT IS SCRIPTED: the RENDERER, the model, the gesture path (a real `InputFsm`), the
    //    `UiProvisionAdapter`, `ui_prov_create_team`'s PHY precondition and the `ProvisioningService` transaction are
    //    all the shipped ones. Scripted are only the three DEVICE forwards that live in `src/firmware_config.cpp`
    //    (which is not in this link) — see the `ProvSeams` block above for why that, and not an `IUiProvision`
    //    stand-in, is the seam.
    // ⛔ AND THE AUTHORITY FOR "IT HAPPENED" IS NEVER THE PANEL: every act below is asserted on the store's write
    //    count, the live sink's four counters and the adapter's own entry count. A visual claim is exactly what may
    //    not be trusted as evidence that something durable happened (§UI-7D's rule, three screens over).
#if MR_N_LAYERS < 2
    {
        ProbeCfgStore& st = probe_store();
        ProvSeams&     ps = prov_seams();
        st.can_load = true; st.can_save = true;

        // ---- THE CONVERGED NODE, arranged exactly as the native suite's `DevFake::converge()` does (U1): the record
        //      holds the PHY the radio flies, so the owner's precondition PASSES and the create is reachable at all.
        //      ⚠ The two ends are seeded from the SAME four values on purpose — comparing live-against-live would make
        //        the predicate trivially true and the refusal unreachable (the adapter's own header says so).
        ps.snap.mobile_reg_count       = g_node.mobile_reg_count();     // the DEVICE's own reads (firmware_config.cpp)
        ps.snap.key_hash32             = g_node.key_hash32();
        ps.snap.live_freq_mhz          = 869.4625;                      // ⚠ not representable in integral kHz
        ps.snap.live_bw_hz             = 125000;
        ps.snap.live_routing_sf        = 7;
        ps.snap.live_allowed_sf_bitmap = uint16_t((1u << 6) | (1u << 7));   // TWO data SFs: the [[B211]] shape
        ps.floor.freq_mhz              = 869.4625;
        ps.floor.bw_hz                 = 125000;
        st.rec.freq_mhz          = ps.snap.live_freq_mhz;
        st.rec.bw_hz             = ps.snap.live_bw_hz;
        st.rec.routing_sf        = ps.snap.live_routing_sf;
        st.rec.allowed_sf_bitmap = ps.snap.live_allowed_sf_bitmap;
        // The live KEY is a pair of POINTERS INTO THE NODE (`ProvSnapshot`'s own contract: both null while no key is
        // held). Re-read before every attempt, because a create installs one and a leave destroys it.
        auto arm_live_key = [&]() {
            ps.snap.live_key_pub  = g_node.team_channel_pub();
            ps.snap.live_key_priv = g_node.team_channel_priv();
        };
        arm_live_key();

        // ★★★ A PRESS, AND THEN A FRAME THAT IS CERTAINLY **NEWER THAN THE PRESS**. ⚠ MEASURED, not defensive: a
        //     frame that opened on the SAME tick as the gesture froze the PREVIOUS state, and U8g2's eight page
        //     replays then outlive the press — so a bare `paint()` read the pre-press scene and four checks failed
        //     against a renderer that was correct. The second paint is past the 2 Hz throttle, and it opens a frame
        //     ONLY if the model is still dirty, i.e. exactly in the case where the first one was stale.
        auto see = [&](uint32_t at) { paint(at); paint(at + 700); return at + 800; };

        uint32_t t17 = settle(1400000);

        // ---- (a) THE PARENT ROW OPENS THE CHILD MENU ---------------------------------------------------------------
        t17 = walk_to_slot(t17 + 2000, kSlotSettings);
        t17 = cfg_walk_to(t17 + 500, ">PROVISION");
        CHK("P15a the PROVISION row can be highlighted",  strstr(g_c.page_text, ">PROVISION") != nullptr);
        t17 = see(double_press(t17 + 500));
        CHK("P15a a double OPENS the child menu, on CREATE TEAM", body_row_is(0, ">CREATE TEAM"));
        CHK("P15a ...which also offers JOIN NETWORK and BACK",
            body_row_is(1, " JOIN NETWORK") && body_row_is(2, " BACK"));

        // ---- (b) THE CONFIRMATION: ITS BACK DEFAULT AND ITS `REPLACES` WARNING ---------------------------------------
        // ⛔ §3.6.3: reaching CREATE costs a deliberate `short` THEN `double`. A confirmation opening on CREATE would
        //    make one press mint a team — the destructive-by-default shape the inbox delete modal is also built against.
        // ⓘ THE NODE IS ALREADY IN A TEAM HERE, and that is P9d's doing rather than this phase's: it put `team_id =
        //   0xABCD1234` on the real node so the TEAM screen had a roster. ⇒ the warning's POSITIVE arm is the state
        //   this probe arrives in, and (d) below LEAVES the team to reach the negative one.
        const int w0 = st.writes, f0 = ps.facts_calls, lv0 = ps.live.total(), l0 = st.loads;
        char rep_tok[mrui::kProvReplacesCap];
        const bool rep_owed = mrui::ui_fmt_prov_replaces(rep_tok, sizeof rep_tok, g_node.config().team_id);
        CHK("P15b precondition: the node is in a team already", g_node.config().team_id != 0u && rep_owed);
        t17 = see(double_press(t17 + 500));
        CHK("P15b the confirmation opens with its title",      body_row_is(0, mrui::kProvCreateTitle));
        CHK("P15b ...with BACK selected, never CREATE",
            body_row_is(3, ">BACK") && body_row_is(4, " CREATE"));
        // ★ Design §3.6.3's *"if already in a team, the screen says the current membership will be replaced"* — and the
        //   token is the FORMATTER's output for THIS node's team, not "a REPLACES line appeared".
        CHK("P15b ...and warns that the CURRENT membership is replaced", body_row_is(1, rep_tok));
        // ⛔ THE ZEROS ARE THE POINT: merely OPENING the confirmation must run no transaction, spend no write and
        //    touch nothing live. Each has a positive arm in (e), so a zero here is evidence and not a dead fixture.
        CHK("P15b ...the adapter was not entered at all",      ps.facts_calls == f0);
        CHK("P15b ...zero durable writes and zero record loads",
            st.writes == w0 && st.loads == l0);
        CHK("P15b ...and nothing was applied live",            ps.live.total() == lv0);

        // ---- (c) BACK PERFORMS NOTHING ------------------------------------------------------------------------------
        t17 = see(double_press(t17 + 500));
        CHK("P15c a double on BACK returns to the child menu", body_row_is(0, ">CREATE TEAM"));
        CHK("P15c ...having entered no transaction",           ps.facts_calls == f0);
        CHK("P15c ...spent no write and applied nothing",
            st.writes == w0 && ps.live.total() == lv0);

        // ---- (d) THE WARNING'S OTHER ARM: A TEAMLESS NODE IS NOT WARNED ----------------------------------------------
        // ⚠ THE LEAVE IS THE HARNESS's, through the node's own verb — `team_id == 0` is the core's "not in a team"
        //   (node.h:261) and is the exact state the formatter answers `false` for.
        (void)g_node.set_team_id(0);
        arm_live_key();                                        // ...the leave destroyed the team channel key
        t17 = see(double_press(t17 + 500));
        CHK("P15d precondition: the node left its team",       g_node.config().team_id == 0u);
        CHK("P15d a teamless node gets NO replace warning",
            body_row(1) == nullptr && strstr(g_c.page_text, "REPLACES") == nullptr);
        CHK("P15d ...and the confirmation is otherwise unchanged",
            body_row_is(0, mrui::kProvCreateTitle) && body_row_is(3, ">BACK") && body_row_is(4, " CREATE"));

        // ---- (e) CONFIRM -> THE `created` RESULT ----------------------------------------------------------------------
        t17 = see(settle(t17 + 500));                          // `short` TOGGLES
        CHK("P15e a short press moves the selection to CREATE",
            body_row_is(4, ">CREATE") && body_row_is(3, " BACK"));
        t17 = see(double_press(t17 + 500));                    // `double` PERFORMS
        const uint32_t created = ps.live.last_team_id;
        char id_tok[mrui::kTeamIdTokenCap]; mrui::ui_fmt_team_id_full(id_tok, sizeof id_tok, created);
        char fp_tok[mrui::kTeamFpTokenCap]; mrui::ui_fmt_team_fingerprint(fp_tok, sizeof fp_tok, created);
        CHK("P15e the panel says TEAM CREATED",                body_row_is(0, "TEAM CREATED"));
        CHK("P15e the FULL 8-hex team id is on the panel",     created != 0u && body_row_is(1, id_tok));
        // ★★ THE VALUE RELATION, NOT MERE PRESENCE: the token at the fingerprint's own row must equal
        //    `ui_fmt_team_fingerprint` OF THE ID THE TRANSACTION MINTED. ⛔ A `strstr` for it would match INSIDE the id
        //    (the fingerprint is the id's low 24 bits, i.e. its last six characters) and would pass on a renderer that
        //    drew no fingerprint at all.
        CHK("P15e ...and the SHARED fingerprint of that same id", body_row_is(2, fp_tok));
        // ⛔ AND THE TWO ROWS REALLY ARE TWO TOKENS. ⚠ It compares the DRAWN rows, not the two locally formatted
        //    strings: `strcmp(id_tok, fp_tok)` is a fact about this file's own arithmetic that no mutation of the
        //    renderer could ever move — an unbreakable check, which this probe records as vacuous rather than as a pass.
        CHK("P15e ...the two are DIFFERENT tokens, both drawn",
            body_row(1) != nullptr && body_row(2) != nullptr && strcmp(body_row(1), body_row(2)) != 0);
        CHK("P15e ...and the way out is stated",               body_row_is(4, "press = back"));
        // ⛔ THE DURABLE + LIVE AUTHORITIES, never the panel.
        CHK("P15e the adapter ran EXACTLY once",               ps.facts_calls == f0 + 1);
        CHK("P15e ...and spent EXACTLY one durable write",     st.writes == w0 + 1);
        CHK("P15e ...the record holds that same team id",      st.rec.team_id == created);
        CHK("P15e ...the membership was applied live, DAD last",
            ps.live.set_team_calls == 1 && ps.live.install_calls == 1 && ps.live.dad_calls == 1);
        CHK("P15e ...and NO retune happened ([[B209]])",       ps.live.phy_calls == 0);
        CHK("P15e ...the post-save bookkeeping ran once",      ps.noted_calls == 1);

        // ---- (f) THE OWNER's PHY PRECONDITION, ON THE PANEL -------------------------------------------------------------
        // The divergence is the real one: `mobile register sf=…` retunes the radio and moves `_cfg.layers[0]` WITHOUT
        // persisting, so the record and the radio genuinely disagree.
        const int w1 = st.writes, lv1 = ps.live.total(), f1 = ps.facts_calls;
        ps.snap.live_routing_sf = 9;
        arm_live_key();
        t17 = see(double_press(t17 + 500));                    // leave the result -> the child menu
        CHK("P15f a press leaves the result for the child menu", body_row_is(0, ">CREATE TEAM"));
        t17 = see(double_press(t17 + 500));                    // -> the confirmation
        t17 = see(settle(t17 + 500));                          // -> CREATE
        t17 = see(double_press(t17 + 500));
        CHK("P15f a live/persisted PHY divergence says PHY DIFFERS", body_row_is(0, "PHY DIFFERS"));
        CHK("P15f ...and names the remedy: USE SERIAL",        body_row_is(1, "USE SERIAL"));
        CHK("P15f ...the adapter refused BEFORE the transaction",
            ps.facts_calls == f1 + 1 && st.writes == w1);
        CHK("P15f ...nothing applied live, no id claimed",
            ps.live.total() == lv1 && body_row(2) == nullptr);
        ps.snap.live_routing_sf = 7;                           // converged again

        // ---- (g) A FAILED DURABLE WRITE --------------------------------------------------------------------------------
        const int w2 = st.writes, lv2 = ps.live.total();
        st.can_save = false;
        t17 = see(double_press(t17 + 500));                    // leave the result
        t17 = see(double_press(t17 + 500));                    // the confirmation
        t17 = see(settle(t17 + 500));                          // -> CREATE
        t17 = see(double_press(t17 + 500));
        CHK("P15g a failed durable write says SAVE FAILED",    body_row_is(0, "SAVE FAILED"));
        CHK("P15g ...and NOTHING CHANGED",                     body_row_is(1, "NOTHING CHANGED"));
        CHK("P15g ...the write was ATTEMPTED exactly once",    st.writes == w2 + 1);
        CHK("P15g ...but nothing reached the live node",       ps.live.total() == lv2);
        CHK("P15g ...and no team id is claimed for it",        body_row(2) == nullptr);
        st.can_save = true;

        // ---- (h) AN UNREADABLE RECORD — A PRECONDITION THAT COULD NOT BE ESTABLISHED --------------------------------------
        const int w3 = st.writes, lv3 = ps.live.total(), f3 = ps.facts_calls;
        st.can_load = false;
        t17 = see(double_press(t17 + 500));                    // leave the result
        t17 = see(double_press(t17 + 500));                    // the confirmation
        t17 = see(settle(t17 + 500));                          // -> CREATE
        t17 = see(double_press(t17 + 500));
        CHK("P15h an unreadable record says CREATE REFUSED",   body_row_is(0, "CREATE REFUSED"));
        CHK("P15h ...with the SERVICE's own typed reason",
            body_row_is(1, mrfw::prov_err_name(mrfw::ProvErr::nv_load_failed)));
        CHK("P15h ...the adapter was entered and refused closed",
            ps.facts_calls == f3 + 1 && st.writes == w3);
        CHK("P15h ...applying nothing at all",                 ps.live.total() == lv3);
        st.can_load = true;

        // ---- (i) THE WAY OUT --------------------------------------------------------------------------------------------
        t17 = see(double_press(t17 + 500));
        CHK("P15i a press leaves the result, rebuilding the menu", body_row_is(0, ">CREATE TEAM"));
        t17 = walk_to(t17 + 500, ">BACK");
        t17 = see(double_press(t17 + 500));
        CHK("P15i BACK leaves the sub-view for the SETTINGS menu",
            strstr(g_c.page_text, "PROVISION") != nullptr && !body_row_is(0, ">CREATE TEAM"));

        // ======================================================================================================= P16
        // ★★★★ §UI-15 slice 6 — THE WHOLE STATIC-JOIN FLOW, THROUGH THE REAL RENDERER: select (including the store
        //      states) -> confirm -> waiting -> the 60 s WORD CHANGE ACROSS A BLANK -> a SYNTHESIZED CORRELATED
        //      ADOPT -> result, plus the two negative arms the plan's traps are named after (an UNCORRELATED adopt
        //      and a REFUSED push).
        // ★★ WHAT IS REAL AND WHAT IS SCRIPTED: the renderer, the model, the gesture path, `UiProvisionAdapter`'s
        //    join half, the slice-1 `JoinService` and the slice-2 `JoinProfileService` are all the shipped ones, and
        //    the push arrives through `mr_ui_on_push` — the EXACT seam `fw_main` calls. Scripted are only the two
        //    accessors `src/firmware_config.cpp` would have supplied.
        // ⛔ AND THE AUTHORITY FOR "IT HAPPENED" IS NEVER THE PANEL: every act is asserted on the `/mrcfg` write
        //    count, the `/mrjoin` write count and the join live seam's counter.
        // ⓘ WHICH P16 CHECKS THE ROLL-UP REPORTS AS UN-REDDENED, AND WHY — justified here rather than assumed
        //   covered (the roll-up's own rule):
        //     · the ZERO-WRITE lines (`...spent nothing`) are NEGATIVE SPACE, exactly as P15b's are: they have their
        //       positive arm in (d), where the same counters move by exactly one;
        //     · `869.4625 MHz renders EXACTLY` and `a STORAGE FAILURE never reads as empty/corrupt` are facts about
        //       the PURE formatters, not about this file — no mutation of `firmware_ui.cpp` can move them, and they
        //       are mutation-covered where they live (`--target=uijoin`, entries J21 and J09/J10);
        //     · (f)'s first three lines are HARNESS PRECONDITIONS in P15a's sense — the panel really did blank, the
        //       session really is still alive, the press really did wake it. They exist so the ONE line that carries
        //       the property (`shows STILL JOINING`, reddened by L21 and L16) cannot pass or fail for a reason that
        //       is about the harness. ⛔ A control that reddened them would be measuring the probe, not the file.
        {
            JoinSeams& js = join_seams();
            const uint8_t me = g_node.canonical_node_id();          // the id a correlated adopt must name
            // Two presets, in slots 1 and 3, so a row's SLOT NUMBER and its POSITION differ (§B66).
            // ⚠ 869.4625 MHz is 869462500 Hz EXACTLY — the value no integral kHz can hold.
            mrnv::join_blob_init(js.presets.rec);
            js.presets.rec.prof[0].present = 1; js.presets.rec.prof[0].layer = 4;
            js.presets.rec.prof[0].routing_sf = 9;
            js.presets.rec.prof[0].freq_hz = 869462500u; js.presets.rec.prof[0].bw_hz = 125000u;
            memcpy(js.presets.rec.prof[0].name, "hut", 3); js.presets.rec.prof[0].name_len = 3;
            js.presets.rec.prof[2].present = 1; js.presets.rec.prof[2].layer = 17;   // ★ ABOVE 15 — trap 2's value
            js.presets.rec.prof[2].routing_sf = 7;
            js.presets.rec.prof[2].freq_hz = 868000000u; js.presets.rec.prof[2].bw_hz = 62500u;
            js.presets.answer = mrnv::JoinRead::ok;

            // ---- (a0) THE PUSH TAP COSTS NOTHING WHILE NO JOIN IS IN FLIGHT --------------------------------------
            // ★★ `join_adopted` FIRES AT EVERY BOOT ON EVERY PROVISIONED NODE, so the tap's guard is not a nicety: a
            //    `/mrcfg` read per push would be a flash read for an event that can never complete anything. ⛔ The
            //    counter is the store's own, so this is a measurement rather than a reading of the source.
            {
                const int l0 = probe_store().loads;
                MESHROUTE_NS::Push boot{};
                boot.kind = MESHROUTE_NS::PushKind::join_adopted;
                boot.layer_id = 1; boot.dst = me;
                mr_ui_on_push(boot);
                mr_ui_on_push(boot);
                CHK("P16a0 a push with no join session reads no record",
                    probe_store().loads == l0);
            }

            // ---- (a) THE SLOT LIST ------------------------------------------------------------------------------
            t17 = see(double_press(t17 + 500));                     // SETTINGS menu -> the child menu
            CHK("P16a the child menu is up again", body_row_is(0, ">CREATE TEAM"));
            t17 = walk_to(t17 + 500, ">JOIN NETWORK");
            const int jw0 = js.presets.writes, cw0 = probe_store().writes, lv0 = js.live.calls;
            t17 = see(double_press(t17 + 500));
            CHK("P16a JOIN NETWORK opens the SLOT LIST, by its label",
                body_row_is(0, ">hut"));
            // ★ THE SECOND ROW IS SLOT **3**, not row 1: an unnamed preset renders plan §11's `PROFILE n` default,
            //   and the number in it is the SLOT's. A renderer using the row index would draw `PROFILE 2`.
            CHK("P16a ...and an unnamed preset shows its SLOT number, not its row",
                body_row_is(1, " PROFILE 3") && body_row_is(2, " BACK"));
            // ⛔ THE ZEROS: opening a LIST reads flash and writes nothing, anywhere.
            CHK("P16a ...and opening it spent no write of either record",
                js.presets.writes == jw0 && probe_store().writes == cw0 && js.live.calls == lv0);

            // ---- (b) THE CONFIRMATION: ALL FOUR VALUES, AND ITS BACK DEFAULT --------------------------------------
            t17 = see(double_press(t17 + 500));
            CHK("P16b a preset opens the CONFIRMATION, by its label", body_row_is(0, "hut"));
            {   // ★ THE VALUES ARE A **VALUE RELATION** to the formatters, ⛔ never "some numbers appeared".
                char phy[mrui::kJoinPhyLineCap], frq[mrui::kJoinFreqLineCap];
                mrui::join_fmt_phy(phy, sizeof phy, js.presets.rec.prof[0]);
                mrui::join_fmt_freq(frq, sizeof frq, js.presets.rec.prof[0]);
                CHK("P16b ...showing layer, SF, BW and the carrier",
                    body_row_is(1, phy) && body_row_is(2, frq));
                CHK("P16b ...and 869.4625 MHz renders EXACTLY",
                    strcmp(frq, "869.4625 MHz") == 0);
            }
            CHK("P16b ...with BACK selected, never JOIN", body_row_is(3, ">BACK") && body_row_is(4, " JOIN"));
            CHK("P16b ...and it has still spent nothing",
                js.presets.writes == jw0 && probe_store().writes == cw0 && js.live.calls == lv0);
            // BACK returns to the LIST — ⛔ not to the child menu: that is the screen he was choosing on.
            t17 = see(double_press(t17 + 500));
            CHK("P16c a double on BACK returns to the SLOT LIST", body_row_is(0, ">hut"));
            CHK("P16c ...having spent nothing at all",
                js.presets.writes == jw0 && probe_store().writes == cw0 && js.live.calls == lv0);

            // ---- (d) CONFIRM -> `JOINING`, and EXACTLY ONE durable write ------------------------------------------
            // ⚠ SLOT 3 IS CHOSEN, i.e. LAYER 17 — the trap-2 value — so the correlation below is exercised where
            //   plan v3's rule was unsatisfiable.
            t17 = settle(t17 + 500);                                // `short`: the cursor moves to PROFILE 3
            t17 = see(double_press(t17 + 500));
            CHK("P16d the second preset's confirmation is up", body_row_is(0, "PROFILE 3"));
            t17 = see(settle(t17 + 500));                           // `short` TOGGLES to JOIN
            CHK("P16d a short press moves the selection to JOIN",
                body_row_is(4, ">JOIN") && body_row_is(3, " BACK"));
            t17 = see(double_press(t17 + 500));                     // `double` PERFORMS
            // ⛔⛔ `JOINING`, ⛔ NEVER `JOINED`: the transaction has written once and DAD has only BEGUN.
            CHK("P16d the panel says JOINING", body_row_is(0, "JOINING"));
            CHK("P16d ...⛔ and no JOINED-shaped word is anywhere on the panel",
                strstr(g_c.page_text, "JOINED") == nullptr);
            CHK("P16d ...the way out is stated", body_row_is(4, "press = back"));
            CHK("P16d EXACTLY ONE `/mrcfg` write, and ⛔ ZERO `/mrjoin` writes",
                probe_store().writes == cw0 + 1 && js.presets.writes == jw0);
            CHK("P16d ...the live apply ran ONCE, after the save", js.live.calls == lv0 + 1);
            // ★★ THE RECORD KEEPS THE FULL BYTE AND THE NIBBLE APART — trap 2's writing end.
            CHK("P16d ...the record holds layer 17 with leaf 1",
                probe_store().rec.layer0_id == 17 && probe_store().rec.leaf_id == 1);
            CHK("P16d ...the profile's Hz reached it as MHz",
                probe_store().rec.freq_mhz == 868.0 && probe_store().rec.bw_hz == 62500u);

            // ---- (e) THE NEGATIVE ARMS: an UNCORRELATED adopt and a REFUSED push -----------------------------------
            // ⛔ EACH ARRIVES THROUGH `mr_ui_on_push`, the same seam a real adoption would, and each must change
            //    NOTHING. A screen completed by one of these is the *"a success that isn't"* class the plan names.
            // ⚠ THE PUSH SEQUENCE BELOW ADVANCES THE CLOCK IN **SMALL** STEPS AND THAT IS DELIBERATE, not tidiness:
            //   `kBlankMs` is 15 s from the LAST PRESS, and a push is not a press (§8.3.1 rule 1 — a device event must
            //   never touch the attention clock). Nine `see()`s at the ordinary +500 spacing would come within ~3 s of
            //   blanking the panel, and a blanked panel draws NOTHING — every `body_row_is` below would then fail for
            //   a reason that has nothing to do with the property. ⛔ A press to refresh the clock is not available
            //   here: it would LEAVE the waiting screen, which is exactly what these checks are standing on.
            {
                MESHROUTE_NS::Push wrong{};
                wrong.kind = MESHROUTE_NS::PushKind::join_adopted;
                wrong.layer_id = 17; wrong.dst = me;                // ⛔ the FULL byte as a leaf — plan v3's rule
                mr_ui_on_push(wrong);
                t17 = see(t17 + 100);
                CHK("P16e a FULL-layer-as-leaf adopt does NOT complete it",
                    body_row_is(0, "JOINING"));
                wrong.layer_id = 1; wrong.dst = uint8_t(me + 1);    // ⛔ somebody ELSE's adoption
                mr_ui_on_push(wrong);
                t17 = see(t17 + 100);
                CHK("P16e a FOREIGN dst does not complete it either", body_row_is(0, "JOINING"));
                wrong.dst = 0;                                      // ⛔ adopted NOTHING
                mr_ui_on_push(wrong);
                t17 = see(t17 + 100);
                CHK("P16e ...nor a ZERO dst", body_row_is(0, "JOINING"));
                // ⛔⛔ AND NO `join_refused` REASON FAILS IT (plan §2.3 rule 6): all four arms, otherwise perfectly
                //    correlated. A wire-version OBSERVATION ABOUT ANOTHER PEER rides this very kind.
                bool refused_ok = true;
                for (int r = 0; r <= int(MESHROUTE_NS::JoinRefuseReason::sf_list_mismatch); ++r) {
                    MESHROUTE_NS::Push rf{};
                    rf.kind = MESHROUTE_NS::PushKind::join_refused;
                    rf.join_reason = MESHROUTE_NS::JoinRefuseReason(r);
                    rf.layer_id = 1; rf.dst = me;
                    mr_ui_on_push(rf);
                    t17 = see(t17 + 100);
                    refused_ok = refused_ok && body_row_is(0, "JOINING");
                }
                CHK("P16e every JoinRefuseReason is IGNORED: still JOINING", refused_ok);
                CHK("P16e ...and none of them spent a write or a live apply",
                    probe_store().writes == cw0 + 1 && js.live.calls == lv0 + 1);
                // ---- ★★★★ [[B228]] — AND NONE OF THEM READS THE RECORD, EITHER ---------------------------------
                // ⛔ THE SESSION IS ACTIVE THROUGHOUT, which is the whole point: `join_session_active()` is TRUE, so
                //    the only thing standing between an ordinary push and a `/mrcfg` read is the KIND PREFILTER in
                //    `ui_join_note_push`. The session is not a brief state (BACK, the blank and the 60 s deadline all
                //    leave it running), so without the prefilter a node whose join never completes pays a flash read
                //    for every push it receives for the rest of its uptime.
                // ⚠ NO TICKS INSIDE THE BRACKET, DELIBERATELY: the SETTINGS service reads `/mrcfg` on its own account,
                //   so a `see()` between the pushes would put a legitimate load inside the window and this count would
                //   be measuring the harness. The pushes go in back to back and the panel is asserted afterwards.
                {
                    const int ld = probe_store().loads;
                    for (int r = 0; r <= int(MESHROUTE_NS::JoinRefuseReason::sf_list_mismatch); ++r) {
                        MESHROUTE_NS::Push rf{};
                        rf.kind = MESHROUTE_NS::PushKind::join_refused;
                        rf.join_reason = MESHROUTE_NS::JoinRefuseReason(r);
                        rf.layer_id = 1; rf.dst = me;                  // otherwise PERFECTLY correlated
                        mr_ui_on_push(rf);
                    }
                    // ...and the ordinary traffic of a live node, every kind of which reaches the same default arm.
                    const MESHROUTE_NS::PushKind others[] = {
                        MESHROUTE_NS::PushKind::send_acked,   MESHROUTE_NS::PushKind::send_failed,
                        MESHROUTE_NS::PushKind::send_aired,   MESHROUTE_NS::PushKind::hash_resolved,
                        MESHROUTE_NS::PushKind::config_adopted, MESHROUTE_NS::PushKind::team_reg,
                    };
                    for (MESHROUTE_NS::PushKind k : others) {
                        MESHROUTE_NS::Push un{};
                        un.kind = k; un.layer_id = 1; un.dst = me;
                        mr_ui_on_push(un);
                    }
                    CHK("P16e ⛔ [[B228]] no non-adopt push reads the record",
                        probe_store().loads == ld);
                    t17 = see(t17 + 100);
                    CHK("P16e ...and the screen is untouched by all ten of them", body_row_is(0, "JOINING"));
                }
            }

            // ---- (f) ★★★★ [[B226]] — THE 60 s `STILL JOINING`, THROUGH THE **REAL RENDERER**, ACROSS A BLANK -----
            // ⛔⛔ WHY THIS EXISTS. Plan §2.3 rule 5's word change is a MODEL latch (`UiState::join_still`) with its
            //    own native cases and its own mutation entry — but the only thing that puts it ON A PANEL is one
            //    argument in this file (`join_wait_head(st.join_still)`), and NOTHING else in the tree compiles this
            //    file. Until this block existed, `join_wait_head(false)` could be hard-wired in production and every
            //    gate — native, the corpus, both probe arms — stayed green: the vacuous-coverage class. Control L21
            //    applies exactly that mutation and requires this sequence to redden.
            // ★★ AND IT IS DRIVEN ACROSS THE BLANK BECAUSE THAT IS THE REAL SHAPE OF THE EVENT: 60 s with no press is
            //    45 s past `kBlankMs`, so the operator who comes back to a dark panel is the ONLY one who ever sees
            //    this word. A sequence that kept pressing to stay awake would be measuring a screen no user is at.
            // ⓘ THE STEPS ARE THE QG's, IN ORDER: past 60 s -> the panel is dark and the session is nonetheless
            //   ALIVE -> ONE short press, consumed as the WAKE -> the renderer says `STILL JOINING` -> nothing was
            //   written or applied to earn it.
            {
                const int cw_f = probe_store().writes, lv_f = js.live.calls, jw_f = js.presets.writes;
                // (1) PAST THE DEADLINE WITH NO INPUT AT ALL. `kJoinStillMs` is 60 s from the transaction, and the
                //     latch rides the SESSION's clock — not `_last_input_ms`, which is what the blank below uses.
                t17 += mrui::kJoinStillMs + 5000;
                tick(t17);
                tick(t17 + 10);                                 // ...and let the power-save edge complete
                // (2) THE PANEL IS DARK — and the session is nonetheless ALIVE.
                CHK("P16f the panel blanks while the join is still in flight", g_c.last_power_save == 1);
                {
                    // ⛔ THE AUTHORITY FOR "THE SESSION IS STILL ACTIVE" IS THE STORE, ⛔ NEVER THE PANEL — which is
                    //    dark and therefore says nothing at all. An UNCORRELATED adopt passes the session guard and
                    //    the kind prefilter and reaches the `/mrcfg` load, so the load counter answers the question
                    //    from outside the model. (P16a0 is the same measurement with the session CLOSED: no load.)
                    const int ld = probe_store().loads;
                    MESHROUTE_NS::Push probe_pu{};
                    probe_pu.kind = MESHROUTE_NS::PushKind::join_adopted;
                    probe_pu.layer_id = 1; probe_pu.dst = uint8_t(me + 1);   // ⛔ somebody ELSE's adoption
                    mr_ui_on_push(probe_pu);
                    CHK("P16f ...and the SESSION is still alive — measured on the store",
                        probe_store().loads == ld + 1);
                }
                // (3) ONE SHORT PRESS, AND IT IS CONSUMED AS THE WAKE (`on_gesture`'s blanked arm returns early).
                //     ⛔ If it were DELIVERED instead, `join_waiting`'s gesture arm would leave for the child menu —
                //     which is what check (4)'s negative half asserts, so the two cannot both pass on a wrong model.
                t17 = see(settle(t17 + 100));
                CHK("P16f a single short press wakes the panel", g_c.last_power_save != 1);
                CHK("P16f ...and was CONSUMED as the wake — the screen did not move",
                    !body_row_is(0, ">CREATE TEAM"));
                // (4) ★★★★ THE REAL RENDERER, SAYING THE 60 s WORD.
                CHK("P16f ★★ the OLED renderer shows STILL JOINING once past 60 s",
                    body_row_is(0, "STILL JOINING"));
                CHK("P16f ...with the way out still stated", body_row_is(4, "press = back"));
                // (5) ⛔ AND NOTHING WAS SPENT TO EARN IT: rule 5 is a WORD CHANGE and *"nothing else happens"* — no
                //     re-run transaction, no second save, no live apply.
                CHK("P16f ...having written nothing and applied nothing",
                    probe_store().writes == cw_f && js.live.calls == lv_f && js.presets.writes == jw_f);
            }

            // ---- (g) THE CORRELATED ADOPT COMPLETES, showing the resulting node id ---------------------------------
            {
                MESHROUTE_NS::Push ok{};
                ok.kind = MESHROUTE_NS::PushKind::join_adopted;
                ok.layer_id = 1;                                    // ★ the NIBBLE of the requested 17
                ok.dst = me;                                        // ★ ...and OUR canonical id, non-zero
                mr_ui_on_push(ok);
                t17 = see(t17 + 100);
                char node[mrui::kJoinNodeLineCap];
                mrui::join_fmt_node(node, sizeof node, me);
                CHK("P16g ★★ a CORRELATED adopt completes the screen", body_row_is(0, "ADOPTED"));
                CHK("P16g ...showing the resulting node id", body_row_is(1, node));
                CHK("P16g ...and the way out", body_row_is(4, "press = back"));
                CHK("P16g ...having spent NO further write and NO further live apply",
                    probe_store().writes == cw0 + 1 && js.live.calls == lv0 + 1);
                // ⛔ AND IT IS NOT RE-RUN BY A SECOND ADOPT: the session ended, so the screen is terminal.
                mr_ui_on_push(ok);
                t17 = see(t17 + 100);
                CHK("P16g a SECOND adopt changes nothing",
                    body_row_is(0, "ADOPTED") && probe_store().writes == cw0 + 1);
            }
            t17 = see(double_press(t17 + 500));
            CHK("P16g a press leaves the result for the child menu", body_row_is(0, ">CREATE TEAM"));

            // ---- (h) THE STORE MATRIX ON THE PANEL ----------------------------------------------------------------
            // ★★★★ ALL FOUR STATES, EACH ITS OWN TEXT, AND ⛔ `io_failed` NEVER READING AS ABSENT OR AS INVALID —
            //      the distinction [[B218]] bought. Each also offers NO SLOT, so a corrupt store cannot be joined from.
            {
                struct Arm { mrnv::JoinRead st; const char* head; const char* detail; };
                const Arm arms[] = {
                    { mrnv::JoinRead::absent,    "NO PROFILES",     nullptr },
                    { mrnv::JoinRead::invalid,   "PROFILE STORE",   "INVALID" },
                    { mrnv::JoinRead::io_failed, "STORAGE FAILURE", "CHECK faults" },
                };
                bool all_ok = true, io_distinct = true;
                for (const Arm& a : arms) {
                    js.presets.answer = a.st;
                    t17 = walk_to(t17 + 500, ">JOIN NETWORK");
                    t17 = see(double_press(t17 + 500));
                    all_ok = all_ok && body_row_is(0, a.head);
                    if (a.detail) all_ok = all_ok && body_row_is(1, a.detail);
                    // ⛔ THE ONLY ROW IS BACK — under the note when there is one.
                    all_ok = all_ok && body_row_is(a.detail ? 2 : 1, ">BACK");
                    if (a.st == mrnv::JoinRead::io_failed)
                        io_distinct = strstr(g_c.page_text, "NO PROFILES") == nullptr &&
                                      strstr(g_c.page_text, "INVALID") == nullptr;
                    t17 = see(double_press(t17 + 500));            // BACK -> the child menu
                }
                CHK("P16h every store state names itself, offering only BACK", all_ok);
                CHK("P16h a STORAGE FAILURE never reads as empty/corrupt", io_distinct);
                CHK("P16h ...and no store state spent a write",
                    js.presets.writes == jw0 && probe_store().writes == cw0 + 1);
                js.presets.answer = mrnv::JoinRead::ok;
            }
        }
    }
#endif  // MR_N_LAYERS < 2

    printf("\n%d passed / %d failed / %d total\n", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
