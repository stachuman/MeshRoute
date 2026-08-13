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

#include "mr_features.h"
#include "board_ui.h"          // the mrui:: canvas contract — IMPLEMENTED below as counting fakes
#include "mr_ui.h"             // mr_ui_init / mr_ui_tick / mr_ui_on_push — the seam under test
#include "fw_context_pure.h"   // §B105: g_hal / g_node — DEFINED here (fw_main.cpp is not in this link)
#include "firmware_commands.h" // mrfw::exec_command — faked below
#include "iclock.h"
#include "iradio.h"
#include "command.h"
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
    bool button_down  = false;
    int32_t batt_answer = -1;             // what battery_sample_mv() hands back; <0 = unavailable (the real V3 today)
    int  bus_ops() const { return init + begin_frame + next_page + power_save; }
};
Canvas g_c;

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
                     g_c.n_page_text = 0;    g_c.page_text[0]  = '\0'; }
bool next_page()   {
    ++g_c.next_page;
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
void draw_text(int, int, const char* s) {
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
void set_power_save(bool on)           { ++g_c.power_save; g_c.last_power_save = on ? 1 : 0; }
bool button_pressed()                  { ++g_c.button; return g_c.button_down; }
int32_t battery_sample_mv()            { ++g_c.battery; return g_c.batt_answer; }
}  // namespace mrui

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
bool ends_with(const char* s, const char* suffix) {
    const size_t n = strlen(s), m = strlen(suffix);
    return m <= n && strcmp(s + (n - m), suffix) == 0;
}

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

uint32_t settle(uint32_t t) {
    g_c.button_down = true;  tick(t); tick(t + 50);          // stable press (debounce 25 ms)
    g_c.button_down = false; tick(t + 100);                  // release
    t += 500; tick(t);                                       // > double_gap_ms after the release -> short_press
    for (int i = 1; i <= 12; ++i) tick(t + uint32_t(i) * 10);   // let that press's frame page all the way out
    t += 700; tick(t);                                       // > kPaintThrottleMs since that paint
    return t;
}

// Walk the list until the HIGHLIGHTED row is of the wanted kind (`>DM` / `>CH`), then open it with a double press.
// ⚠ Asserted by the caller afterwards, never assumed: if the walk never finds one, the caller's first check fails.
uint32_t open_highlighted(uint32_t t, const char* want) {
    for (int i = 0; i < 14; ++i) {
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
    dirty_the_model(t);
    g_c = Canvas{};
    g_probe_radio.busy_tx = true;
    run_ticks(t, 8, 10);
    CHK("P2a a TX on air suppresses EVERY canvas/bus call",  g_c.bus_ops() == 0);
    CHK("P2a ...and no drawing either",                      g_c.draw_text == 0);
    g_probe_radio.busy_tx = false;
    run_ticks(t + 100, 8, 10);
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
    g_hal.service_tx();                                      // hand it to the radio -> the queue drains
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
    //    counts draw CALLS. These checks read the STATUS BAR's TEXT, which `draw_frame` emits first on every screen.
    // ★ THE RULED RENDER POLICY (plan Task 9 Step 3, spec §3.3): `3.9V` or `--`, NEVER a percentage. A percentage
    //   needs a chemistry and a discharge curve nobody has approved.
    // ⚠ ONLY the bar is asserted, deliberately: `settle()` delivers a real short press, which CYCLES the screen, so
    //   which BODY is drawn is not deterministic here. The bar is drawn on every screen and under the overlay.
    // ⛔ This closes ONE field of B104's residue. The snapshot BUILDER and every other `draw_*` remain uncovered.

    // (a) THE READER HAS NEVER SUCCEEDED -> `--`, and nothing may be invented in its place.
    g_c.batt_answer = -1;
    t = settle(t + 100000);
    dirty_the_model(t);
    run_ticks(t, 8, 10);
    CHK("P5 an unavailable reading renders the bar's `--`",  ends_with(g_c.first_text, "--"));
    CHK("P5 ... and NO voltage is invented anywhere",        !has_voltage(g_c.page_text));

    // (b) ONE GOOD READING REACHES THE PANEL — as volts, to one decimal, never a percentage.
    g_c.batt_answer = 3912;                                  // 3.912 V
    t += 31000; run_ticks(t, 2, 10);                         // the 30 s period has elapsed -> one (successful) sample
    t = settle(t + 1000);
    dirty_the_model(t);
    run_ticks(t, 8, 10);
    CHK("P5 a successful reading renders as volts",          ends_with(g_c.first_text, "3.9V"));
    CHK("P5 ... and never as a percentage",                  strchr(g_c.first_text, '%') == nullptr);

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
    CHK("P5 an unavailable read does not erase the last good value", ends_with(g_c.first_text, "3.9V"));

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
    t = settle(t + 2000);
    for (int i = 0; i < 6 && strstr(g_c.page_text, "INBOX") == nullptr; ++i) { t = settle(t + 1000); paint(t); }
    CHK("P6a the INBOX screen is reachable by pressing",   strstr(g_c.page_text, "INBOX") != nullptr);
    CHK("P6a ...and it lists both kinds",                  strstr(g_c.page_text, "DM ") != nullptr &&
                                                           strstr(g_c.page_text, "CH7") != nullptr);

    // ---- (a) A CHANNEL record, opened while its same-numbered DM is still live -------------------------------------
    t = open_highlighted(t + 500, ">CH");
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
    t = open_highlighted(t + 500, ">CH");
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
    t = open_highlighted(t + 500, ">DM");
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
    CHK("P6f ...and the list's highlight is suppressed",   strstr(g_c.page_text, ">DM") == nullptr &&
                                                           strstr(g_c.page_text, ">CH") == nullptr);
    CHK("P6f ...and deletes nothing else",                 live_count() == live_after_oob);

    // ---- (f) THE `not_found` DELETE OUTCOME, END TO END: the record is evicted WHILE THE MODAL IS OPEN, so the erase
    //          the user then confirms comes back `not_found`. ⛔ The modal must say MESSAGE GONE and must NOT read as a
    //          success — "a visual disappearance without durable success is forbidden" is precisely this path.
    t = open_highlighted(t + 500, ">CH");
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

    printf("\n%d passed / %d failed / %d total\n", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
