// MeshRoute — src/firmware_ui.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The board-UI FEATURE layer (plan Task 6 = spec slice UI-6). It owns the model, the two send trackers, ALL render
// policy, the battery cache and the correlation of node-wide pushes into UI outcomes — and it is where the three
// `mr_ui_*` hooks LIVE, which is what let UI-5's TEMPORARY copies in variants/heltec_v3/board_ui.cpp be deleted.
// Adds no new core API: every read is an accessor that already existed (spec §6).
//
// ★ THE TWO BOUNDARIES THIS FILE SITS BETWEEN, both of them load-bearing rather than tidy:
//   above it  `variants/heltec_v3/board_ui.h` — a display-INDEPENDENT canvas. Nothing here names U8g2, I2C or a pin;
//             nothing there knows what a "screen" is. That is what makes the V4 port a pin table, not a rewrite.
//   below it  `firmware_ui_model.h` / `firmware_ui_send.h` — pure, board-free, natively tested. Every gesture meaning,
//             every state transition and the whole two-tracker glue live THERE so the native suite can drive them;
//             this file holds only what genuinely needs `g_node` / `g_hal` / the panel.
//
// ★ WHAT IS DONE AND WHAT IS NOT — in source, because docs rot and code is read
//   ([[meshroute-mark-done-vs-missing-in-code]]):
//   DONE      the snapshot builder, the DRAWING of STATUS / TEAM / INBOX / SEND / both compose lists / the emergency
//             overlay, the battery cache, and §B91's dead-panel report line.
//   ★ MOVED OUT 2026-08-05 (the UI-6 QA fix slice) — and this is the point, not a tidy-up. WHEN to paint (`FrameGate`),
//             what an arriving push MEANS (`ui_route_recv_push`) and the unread counters all lived here, in a TU that
//             NEITHER the native suite NOR the simulator compiles, and all four of §B101/§B102/§B107/§B108 shipped
//             green because of it. They are now pure code in firmware_ui_model.h / firmware_ui_send.h, driven by the
//             native suite, and `tools/probe_board_ui/run.sh`'s W1-W4 pin that this file still CALLS them.
//   MISSING   THE SEND ITSELF. `ui_perform_send` is a LOUD REFUSAL stub — see it. Task 7 owns `mrfw::exec_command`,
//             which is the one approved firmware addition this plan makes, and it is explicitly not this task's.
//             ⇒ on this build a long-press reaches SENDING... and then FAILED, by construction and visibly.
//   MISSING   inbox ROWS. `Inbox::pull()` adaptation is Task 7 (spec §6.1, with the per-kind row budget); the INBOX
//             screen renders its live counts and says so, rather than showing an empty list that looks like "no mail".
//   MISSING   a real battery reading. `mrui::battery_sample_mv()` is Task 9's; until then it answers "unavailable" and
//             the status bar renders `--` (the console_json.h:126 rule), never a plausible wrong number.
#include "mr_features.h"

#if MR_FEAT_OLED

#include <cstdio>            // snprintf — every panel string is formatted here, never in the board TU
#include "firmware_ui_model.h"
#include "firmware_ui_send.h"
#include "board_ui.h"        // resolved by `-I variants/heltec_v3` — ★ THIS is the task that makes that flag
                             //   load-bearing; §A0 predicted Task 5 and UI-5 measured it dead there three ways.
#include "mr_ui.h"           // the three hook DECLARATIONS we define below (fw_main calls them unconditionally)
#include "fw_context.h"      // g_node / g_hal / g_iradio
#include "console_sink.h"    // mrcon — the guarded sink; §B91's dead-panel line is the only thing this file prints

#ifndef MR_UI_TEAM_CHANNEL_ID
// C2, fail loud: the channel the alarm and the canned posts go to is an OWNER-RULED BUILD CONSTANT with no cfg key, no
// NV field and no console verb. Defaulting it here would silently point a distress call at somebody else's channel.
#  error "MR_UI_TEAM_CHANNEL_ID is not defined — the board env must supply it (platformio.ini, [env:heltec_v3])"
#endif

namespace {

// ---- state ------------------------------------------------------------------------------------------------------
mrui::UiModel     s_model;
mrui::InputFsm    s_input;
// ★ TWO trackers: an alarm must never queue behind a DM waiting on its e2e ack (spec §2.1). Normal work never touches
//   the emergency slot, in either direction.
mrui::SendTracker s_tracker_emg, s_tracker_normal;

// Unread counts + "newest received" stamps are UI-LOCAL and session-scoped (spec §6): `Inbox` exposes no read cursor,
// and counting here needs no new core API. ★ The six loose statics they used to be MOVED into `mrui::UiInboxCounters`
// so the two things that move them are natively driven — see firmware_ui_model.h and §B103/§B108.
mrui::UiInboxCounters s_counters;

int32_t  s_batt_mv        = -1;      // last GOOD reading; <0 = never had one -> render `--`
uint32_t s_batt_next_ms   = 0;
bool     s_batt_attempted = false;

// ★ THE FRAME IS FROZEN AT begin_frame(). A frame spans several ticks and U8g2 re-clips the WHOLE scene once per page,
//   so anything the renderer reads must be a COPY — live state changing mid-frame tears the image across page
//   boundaries (spec §5). ⚠ The plan's Task-6 block froze `UiState` + `UiSnapshot` but then had the emergency overlay
//   read `s_model` LIVE, which reintroduces exactly that tear on the one screen where it matters most. Hence EmgView.
struct EmgView {
    mrui::Emergency    st         = mrui::Emergency::idle;
    mrui::RefuseReason refuse     = mrui::RefuseReason::other;
    // ⚠ WRITTEN, NOT YET READ — and stated rather than left to be discovered, the same discipline
    //   firmware_ui_send.h's `_chan` and UiModel's `_last_try_ms` get:
    //     `dm`   — the DM outcome sub-view is TASK 7's render (spec §3.4.1's seven states). Frozen here because the
    //              freeze point is this function and Task 7 must not have to re-plumb it to get a tear-free read.
    //     `fail` — the CORE reason verbatim (spec §2.1 rule 6). The panel shows `refuse`'s compact code today; the
    //              verbatim reason becomes renderable when Task 7's real send path can produce one that is not
    //              `none`. ⛔ Do not "use" it by printing a raw enum number to make this comment go away.
    mrui::DmState      dm         = mrui::DmState::idle;
    mrui::FailReason   fail       = mrui::FailReason::none;
    uint8_t            arm_secs   = 0;
    uint8_t            tries      = 0;
    uint32_t           retry_in_s = 0;
    char               who[mrui::kLabelCap + 1] = {};
    char               text[21]                 = {};
};
mrui::UiState    s_frame_state{};
mrui::UiSnapshot s_frame_snap{};
EmgView          s_frame_emg{};
// ★ WHEN to paint (§B107). The frozen copies above are WHAT to paint; this owns the lifecycle that decides when they
//   are refreshed — including the `dirty` consumption, which belongs to the FREEZE and not to the final page.
mrui::FrameGate  s_gate;

// ---- §5 rule 1: paint only when the MAC is idle ------------------------------------------------------------------
// The SAME predicate fw_main.cpp:1406 uses to decide it may sleep (U1 — do not invent a second one). A full 1024 B
// frame is ~25 ms of blocking I2C against a `cts_to_data_gap_ms` of 5, so this gate is a correctness constraint: it is
// what stops the panel from breaking an in-flight RTS/CTS/DATA exchange.
bool mac_idle() { return !g_iradio.tx_busy() && g_hal.txq_depth() == 0; }

// ---- battery cache (spec §7) -------------------------------------------------------------------------------------
// Sampled at boot and every 30 s, only when the MAC is idle. An earlier draft sampled eight ADC reads on EVERY service
// pass for a value that changes over minutes.
// ★ The cadence gates on ATTEMPTED, not on SUCCEEDED. Gating on `s_batt_mv >= 0` meant a board whose reader returns the
//   documented unavailable value was re-read on every idle pass, for ever.
constexpr uint32_t kBattPeriodMs = 30000;
void battery_maybe_sample(uint32_t now_ms) {
    if (!mac_idle()) return;
    if (s_batt_attempted && uint32_t(now_ms - s_batt_next_ms) >= (1u << 31)) return;   // wrap-safe "not due yet"
    const int32_t mv = mrui::battery_sample_mv();
    if (mv >= 0) s_batt_mv = mv;                 // keep the last GOOD value; an unavailable read never erases it
    s_batt_attempted = true;
    s_batt_next_ms   = now_ms + kBattPeriodMs;
}

// ---- labels (spec §6: team_key_of_id -> peer_name_find -> 0x<hash> -> bare id) ------------------------------------
void label_from_hash(uint32_t hash, char* out, uint8_t cap) {
    if (g_node.peer_name_find(hash, out, cap) == 0) snprintf(out, cap, "0x%08lx", (unsigned long)hash);
}
void label_for_team_id(uint8_t id, char* out, uint8_t cap) {
    uint32_t hash = 0;
    // ⓘ Inert on a !MR_FEAT_TEAM build: `team_key_of_id` stubs to false there, so the label falls straight through to
    //   the bare id. No #if needed — the stub IS the fallback.
    if (g_node.team_key_of_id(id, hash) && hash != 0) { label_from_hash(hash, out, cap); return; }
    snprintf(out, cap, "id %u", unsigned(id));
}
void label_for_origin(const MESHROUTE_NS::Push& pu, char* out, uint8_t cap) {
    // §chan-crypt CL2c: a channel_recv carries the sender's stable key_hash32 here too, and `origin` on a team post is
    // only a DAD-assigned team_local_id — so prefer the hash when the post named one.
    if (pu.sender_hash != 0) { label_from_hash(pu.sender_hash, out, cap); return; }
    snprintf(out, cap, "id %u", unsigned(pu.origin));
}

// ---- small formatters (ALL text formatting lives in this file, never in the board TU) ----------------------------
void fmt_age(char* out, size_t cap, uint32_t s) {
    if (s == UINT32_MAX)  { snprintf(out, cap, "--"); return; }
    if (s < 60)           { snprintf(out, cap, "%us", unsigned(s)); return; }
    if (s < 3600)         { snprintf(out, cap, "%um", unsigned(s / 60)); return; }
    if (s < 86400)        { snprintf(out, cap, "%uh%02u", unsigned(s / 3600), unsigned((s % 3600) / 60)); return; }
    snprintf(out, cap, "%ud", unsigned(s / 86400));
}
// Volts, never a percentage: a percentage needs a chemistry and a discharge curve nobody has approved (spec §3.3).
void fmt_volts(char* out, size_t cap, int32_t mv) {
    if (mv < 0) { snprintf(out, cap, "--"); return; }
    snprintf(out, cap, "%u.%uV", unsigned(mv / 1000), unsigned((mv % 1000) / 100));
}
const char* refuse_text(mrui::RefuseReason r) {
    switch (r) {
        case mrui::RefuseReason::parser:      return "BAD CMD";
        case mrui::RefuseReason::unsealable:  return "NO CRYPTO";
        case mrui::RefuseReason::no_location: return "NO FIX";
        case mrui::RefuseReason::queue_full:  return "QUEUE FULL";
        case mrui::RefuseReason::other:       return "REFUSED";
    }
    return "REFUSED";   // -Wswitch covers the enum; this satisfies -Wreturn-type
}

// ---- the send path: NOT BUILT IN THIS TASK, and it refuses LOUDLY ------------------------------------------------
// ★★ The plan's Task-6 tick calls `ui_perform_send()`, but that function is Task 7 Step 1 — it needs
//    `mrfw::exec_command`, an ADDITION to src/firmware_commands.{h,cpp} that the plan approves for Task 7 and that C1
//    forbids folding in here. So Task 6 cannot have a working send, and the only question is how it fails.
// ⇒ It fails LOUD (C2). The alternative — drop the request on the floor — leaves the model in `firing`/`submitting`
//   and the panel on `SENDING...` FOR EVER, which is the precise defect §B72/§B79 were raised about, and it would make
//   the H6 bench group unable to distinguish "not built" from "the radio path is broken".
// ⓘ `on_send_refused` lands the alarm in `Emergency::failed`, which §B78 made a RETAINED outcome and §B71 makes
//   dismissable with one short press — so the bench operator is not trapped on the failure screen.
constexpr bool kSendPathBuilt = false;   // ★ Task 7 flips this to true when ui_perform_send does real work.
void ui_perform_send(const mrui::SendReq& req, uint32_t now_ms) {
    mrui::SendTracker& tr = (req.kind == mrui::SendKind::emergency) ? s_tracker_emg : s_tracker_normal;
    tr.refuse();
    // §B78: `now_ms` is REQUIRED and is the REFUSAL's own time — a gesture-anchored deadline is already partly spent
    // by the time a refusal lands, which is the defect spec §4.3 exists to kill.
    s_model.on_send_refused(req.kind, mrui::RefuseReason::other, now_ms);
    mrcon.println(F("!! UI send path not built (plan Task 7 / slice UI-7)"));
}

// ---- snapshot ----------------------------------------------------------------------------------------------------
uint32_t age_s_from(uint32_t now_ms, uint32_t then_ms) { return uint32_t(now_ms - then_ms) / 1000u; }

mrui::UiSnapshot build_snapshot(uint32_t now_ms) {
    mrui::UiSnapshot s{};
    s.now_ms       = now_ms;
    // ★★ §B108 round 2: ONE call (U2), never two assignments — it publishes the CAPPED display counts and the
    //    UNCAPPED arrival serials they were derived from together, which is what lets `FrameGate` freeze a serial
    //    that provably matches the number this frame will draw.
    s_counters.publish(s);
    s.last_dm_age_s = s_counters.have_dm ? age_s_from(now_ms, s_counters.last_dm_ms) : UINT32_MAX;
    s.last_ch_age_s = s_counters.have_ch ? age_s_from(now_ms, s_counters.last_ch_ms) : UINT32_MAX;
    // The TEAM/SEND slots are gated on MR_FEAT_OLED && MR_FEAT_TEAM (spec §9): `gateway_heltec` is a REAL build with
    // OLED=1 and TEAM=0, so this is not hypothetical. `team_build` is what makes the model's cycle skip those slots.
    s.team_build = (MR_FEAT_TEAM != 0);
#if MR_FEAT_TEAM
    // ⚠ `rt_team_at` has NO !MR_FEAT_TEAM stub, by deliberate core design (there is no `_rt_team` to read), so this
    //   whole block must be guarded — the two counters around it stub to 0 and would compile either way.
    const uint8_t total = g_node.rt_team_count();
    s.team_total = total;
    s.team_shown = (total > mrui::kMaxTeamRows) ? mrui::kMaxTeamRows : total;
    const uint64_t now64 = g_hal.now();
    for (uint8_t i = 0; i < s.team_shown; ++i) {
        const MESHROUTE_NS::RtEntry& e = g_node.rt_team_at(i);
        mrui::TeamRow& r = s.team[i];
        r.id = e.dest;
        if (e.n > 0) {
            const MESHROUTE_NS::RtCandidate& c = e.candidates[0];   // the PRIMARY candidate (node_carriers.h:296)
            r.score_q4    = c.score;
            r.hops        = c.hops;
            r.last_heard_s = (c.last_seen_ms == 0 || now64 < c.last_seen_ms)
                           ? UINT32_MAX : uint32_t((now64 - c.last_seen_ms) / 1000u);
        } else {
            r.last_heard_s = UINT32_MAX;
        }
        label_for_team_id(r.id, r.label, uint8_t(sizeof r.label));
    }
#endif
    s.my_team_id = g_node.team_local_id();
    s.team_id    = g_node.config().team_id;
    s.batt_mv    = s_batt_mv;
    // inbox_shown stays 0: the `Inbox::pull()` adapter is Task 7 (spec §6.1). The INBOX screen says so rather than
    // rendering an empty list a user would read as "no messages".
    return s;
}

EmgView freeze_emg(const mrui::UiSnapshot& s) {
    EmgView v{};
    v.st       = s_model.emergency();
    v.dm       = s_model.dm_state();
    v.refuse   = s_model.refuse_reason();
    v.fail     = s_model.fail_reason();
    v.tries    = s_model.attempts();
    v.arm_secs = s_model.arming_secs_left(s);
    // ⚠ `retry_at_ms()` is meaningful ONLY while `blocked` (the STATE is the predicate — §B74 removed the sentinel),
    //   so it is read only there, and wrap-safely.
    if (v.st == mrui::Emergency::blocked) {
        const uint32_t left = s_model.retry_at_ms() - s.now_ms;
        v.retry_in_s = (left >= (1u << 31)) ? 0 : (left + 999) / 1000;
    }
    if (v.st == mrui::Emergency::reply) {
        snprintf(v.who,  sizeof v.who,  "%s", s_model.reply_who());
        snprintf(v.text, sizeof v.text, "%s", s_model.reply_text());
    }
    return v;
}

// ---- render policy (spec §3.3 layout) ---------------------------------------------------------------------------
// 128x64, two fonts only (spec §11: do not link the full font set). 6x10 gives 21 columns and a 10 px line pitch;
// 10x20 gives 12 columns and is used for the emergency headline alone.
constexpr int kBarBaseline = 7;    // 6x10 baseline inside the 8 px status bar
constexpr int kBarRuleY    = 9;
constexpr int kBodyY0      = 19;
constexpr int kBodyDy      = 10;
constexpr int kBodyRows    = 5;    // 19, 29, 39, 49, 59 — all inside 64
constexpr int kEmgHeadY    = 34;   // 10x20 headline
constexpr int kEmgDetailY  = 52;   // 6x10 detail beneath it
// ⚠ DELIBERATELY OVERSIZED vs the 21 visible columns, and it is NOT slack for its own sake — do not shrink it back.
//   Every line here is built with snprintf, and `-Wformat-truncation=` (on by default under -Wall in this toolchain and
//   GATE-BLOCKING in this project) fires whenever GCC cannot PROVE the widest expansion fits. It measured 10 such
//   warnings at kLineCap 24 — all benign truncations, all still ten new warnings against a pinned census. The widest
//   provable line is the status bar (two uint16_t counts at 5 digits, two uint8_t at 3, plus an 11-char volts field
//   because `int32_t/1000` can be 7 digits) at 37 bytes, and the REPLY detail (`who` 14 + `text` 20) at 37. 48 proves
//   both. The panel clips at 21 columns regardless — this buffer bounds the FORMATTER, not the display.
constexpr int kLineCap     = 48;

int body_y(int row) { return kBodyY0 + row * kBodyDy; }

// Which slice of a longer list is on screen. A cursor may address up to kMaxTeamRows entries while only kBodyRows fit.
uint8_t list_first(uint8_t cursor, uint8_t n, uint8_t rows) {
    if (n <= rows || cursor < rows) return 0;
    const uint8_t first = uint8_t(cursor - rows + 1);
    return uint8_t((first + rows > n) ? (n - rows) : first);
}

void draw_status_bar(const mrui::UiSnapshot& s) {
    char volts[12]; fmt_volts(volts, sizeof volts, s.batt_mv);   // 7-digit volts + ".xV" + NUL — see kLineCap
    char bar[kLineCap];
    if (s.team_build)
        snprintf(bar, sizeof bar, "DM%u CH%u T%u/%u %s", unsigned(s.unread_dm), unsigned(s.unread_ch),
                 unsigned(s.team_shown), unsigned(s.team_total), volts);
    else
        snprintf(bar, sizeof bar, "DM%u CH%u %s", unsigned(s.unread_dm), unsigned(s.unread_ch), volts);
    mrui::draw_text(0, kBarBaseline, bar);
    mrui::draw_hline(0, kBarRuleY, 128);
}

void draw_status_screen(const mrui::UiSnapshot& s) {
    char l[kLineCap], age[10];
    mrui::draw_text(0, body_y(0), "STATUS");
    snprintf(l, sizeof l, "me T%u  team %08lx", unsigned(s.my_team_id), (unsigned long)s.team_id);
    mrui::draw_text(0, body_y(1), l);
    fmt_age(age, sizeof age, s.last_dm_age_s);
    snprintf(l, sizeof l, "DM %u, newest %s", unsigned(s.unread_dm), age);
    mrui::draw_text(0, body_y(2), l);
    fmt_age(age, sizeof age, s.last_ch_age_s);
    snprintf(l, sizeof l, "CH %u, newest %s", unsigned(s.unread_ch), age);
    mrui::draw_text(0, body_y(3), l);
    if (s.batt_mv >= 0) snprintf(l, sizeof l, "batt %ldmV", (long)s.batt_mv);
    else                snprintf(l, sizeof l, "batt --");
    mrui::draw_text(0, body_y(4), l);
}

void draw_team_screen(const mrui::UiState& st, const mrui::UiSnapshot& s) {
    if (s.team_shown == 0) {
        mrui::draw_text(0, body_y(0), "TEAM");
        mrui::draw_text(0, body_y(1), "no teammates heard");
        return;
    }
    const uint8_t first = list_first(st.cursor, s.team_shown, kBodyRows);
    for (uint8_t row = 0; row < kBodyRows && first + row < s.team_shown; ++row) {
        const mrui::TeamRow& t = s.team[first + row];
        char age[10]; fmt_age(age, sizeof age, t.last_heard_s);
        char l[kLineCap];
        snprintf(l, sizeof l, "%c%-10s %4s %uh", (first + row == st.cursor) ? '>' : ' ', t.label, age,
                 unsigned(t.hops));
        mrui::draw_text(0, body_y(row), l);
    }
}

void draw_inbox_screen(const mrui::UiSnapshot& s) {
    char l[kLineCap], age[10];
    mrui::draw_text(0, body_y(0), "INBOX");
    fmt_age(age, sizeof age, s.last_dm_age_s);
    snprintf(l, sizeof l, "DM %u  newest %s", unsigned(s.unread_dm), age);
    mrui::draw_text(0, body_y(1), l);
    fmt_age(age, sizeof age, s.last_ch_age_s);
    snprintf(l, sizeof l, "CH %u  newest %s", unsigned(s.unread_ch), age);
    mrui::draw_text(0, body_y(2), l);
    // Honest absence, not an empty list: `Inbox::pull()` rows are Task 7 (spec §6.1). A blank body here would read as
    // "you have no messages", which is a different and wrong statement.
    mrui::draw_text(0, body_y(4), "rows: slice UI-7");
}

void draw_send_screen() {
    mrui::draw_text(0, body_y(0), "SEND to team");
    mrui::draw_text(0, body_y(2), "double = pick a text");
    mrui::draw_text(0, body_y(3), "long   = EMERGENCY");
}

// ★ §B66 IS PAID HERE, and it is the reason these two tables sit beside a static_assert. `back` is identified
//   POSITIONALLY by the model (`cursor + 1 == n`), with the COUNT in firmware_ui_model.h and the STRINGS here — so a
//   text added in one place without the other silently turns "back without sending" into a SEND. Binding them with an
//   assert makes that a BUILD failure instead of a mis-send. ⓘ The last row is ONE row that is both `back` and
//   don't-send, not two (spec §3.2.2).
const char* const kDmTexts[]      = { "Are you OK?",      "I'm OK",   "back, don't send" };
const char* const kChannelTexts[] = { "Got your message", "All good", "back, don't send" };
static_assert(sizeof kDmTexts / sizeof kDmTexts[0] == mrui::kDmTextCount,
              "§B66: kDmTexts and mrui::kDmTextCount must agree, or `back` becomes a send");
static_assert(sizeof kChannelTexts / sizeof kChannelTexts[0] == mrui::kChannelTextCount,
              "§B66: kChannelTexts and mrui::kChannelTextCount must agree, or `back` becomes a send");

void draw_compose(const mrui::UiState& st, const mrui::UiSnapshot& s) {
    const bool dm = (st.compose == mrui::Compose::dm);
    const char* const* texts = dm ? kDmTexts : kChannelTexts;
    const uint8_t n = dm ? mrui::kDmTextCount : mrui::kChannelTextCount;
    char head[kLineCap];
    if (dm) {
        // The peer was bound at ENTRY (`compose_peer`), so a roster that reorders under an open modal cannot retarget
        // the label — or the send. Resolve the label from that bound id, never from the cursor.
        char label[mrui::kLabelCap + 1]; label_for_team_id(st.compose_peer, label, uint8_t(sizeof label));
        snprintf(head, sizeof head, "to: %s", label);
    } else {
        snprintf(head, sizeof head, "to: team ch %u", unsigned(MR_UI_TEAM_CHANNEL_ID));
    }
    mrui::draw_text(0, body_y(0), head);
    const uint8_t first = list_first(st.cursor, n, kBodyRows - 1);
    for (uint8_t row = 0; row + 1 < kBodyRows && first + row < n; ++row) {
        char l[kLineCap];
        snprintf(l, sizeof l, "%c%s", (first + row == st.cursor) ? '>' : ' ', texts[first + row]);
        mrui::draw_text(0, body_y(row + 1), l);
    }
    (void)s;
}

// The emergency overlay REPLACES the body (never the status bar — spec §3.3 keeps that always). Font::large for the
// headline, so it is readable at arm's length under stress; Font::small for the detail line.
void draw_emergency(const EmgView& v) {
    const char* head = "";
    char detail[kLineCap] = {};
    switch (v.st) {
        case mrui::Emergency::idle: return;                                    // caller checks, this is belt-and-braces
        case mrui::Emergency::arming:
            head = "RELEASE!";
            snprintf(detail, sizeof detail, "EMERGENCY IN %u", unsigned(v.arm_secs));
            break;
        case mrui::Emergency::firing:
            head = "SENDING...";
            snprintf(detail, sizeof detail, "attempt %u of %u", unsigned(v.tries + 1), unsigned(mrui::kEmgMaxTries));
            break;
        case mrui::Emergency::blocked:
            head = "BLOCKED";
            snprintf(detail, sizeof detail, "retry in %lus", (unsigned long)v.retry_in_s);
            break;
        // ★ PICKED UP, never DELIVERED: a team channel post has NO end-to-end ack, so the only signal is that a
        //   neighbour was overheard re-flooding it (spec §4). Calling that "delivered" would be a false safety claim.
        case mrui::Emergency::picked_up:
            head = "PICKED UP";
            snprintf(detail, sizeof detail, "a relay heard it");
            break;
        // ⚠ §B38 (owner-ruled): `relayed` means FIRST RELAY ONLY, never coverage — so on a fully-1-hop team this reads
        //   NOT HEARD at 100 % delivery. That is ACCEPTED BEHAVIOUR and must not be "fixed" in the renderer. The
        //   wording therefore says what was MEASURED (no relay overheard), not what it implies about delivery.
        case mrui::Emergency::not_heard:
            head = "NOT HEARD";
            snprintf(detail, sizeof detail, "no relay after %u", unsigned(v.tries));
            break;
        case mrui::Emergency::reply:
            head = "REPLY";
            snprintf(detail, sizeof detail, "%s: %s", v.who, v.text);
            break;
        case mrui::Emergency::cancelled:
            head = "CANCELLED";
            break;
        case mrui::Emergency::failed:
            head = "FAILED";
            if (kSendPathBuilt) snprintf(detail, sizeof detail, "%s", refuse_text(v.refuse));
            // Task 6 has no send path at all, so a bench operator must not read this as a radio/crypto failure.
            else                snprintf(detail, sizeof detail, "no send path: UI-7");
            break;
    }
    mrui::set_font(mrui::Font::large);
    mrui::draw_text(0, kEmgHeadY, head);
    mrui::set_font(mrui::Font::small);
    if (detail[0]) mrui::draw_text(0, kEmgDetailY, detail);
}

// ⚠ Called ONCE PER PAGE, on the FROZEN copies. It must be pure: no state written, nothing read that a later page
//   could see differently, or the image tears across page boundaries (spec §5).
void draw_frame(const mrui::UiState& st, const mrui::UiSnapshot& s, const EmgView& v) {
    mrui::set_font(mrui::Font::small);
    draw_status_bar(s);
    if (v.st != mrui::Emergency::idle) { draw_emergency(v); return; }   // the alarm owns the body, from any screen
    if (st.compose != mrui::Compose::none) { draw_compose(st, s); return; }
    switch (st.screen) {
        case mrui::Screen::status: draw_status_screen(s);      break;
        case mrui::Screen::team:   draw_team_screen(st, s);    break;
        case mrui::Screen::inbox:  draw_inbox_screen(s);       break;
        case mrui::Screen::send:   draw_send_screen();         break;
        case mrui::Screen::count:  break;                     // not a screen; listed so -Wswitch stays useful
    }
}

}  // namespace

// ====================================================================================================== the hooks
// ★ These three are the seam `lib/hal/mr_ui.h` declares and `fw_main` calls UNCONDITIONALLY. They lived TEMPORARILY in
//   variants/heltec_v3/board_ui.cpp so UI-5 could link; Task 6 took ownership and DELETED those copies. Defining them
//   in both places is a duplicate-symbol link failure.

void mr_ui_init() {
    // ★ §B91: the canvas now REPORTS. `board_init()` probes the panel's I2C address, and THIS is the report channel a
    //   `void` return could not have — one console line, once, at boot. It is deliberately not fatal: a node with a
    //   dead panel must keep meshing, and the UI keeps running blind.
    if (!mrui::board_init()) mrcon.println(F("!! OLED panel did not ACK (check Vext / addr 0x3C / wiring)"));
    // No boot splash: the first real frame is one tick away and goes through the page-chunked path. UI-5's splash
    // existed only to prove the canvas was reachable under --gc-sections; the feature layer calls all nine entry
    // points now (§B88), so nothing is collected and nothing needs a stand-in.
}

void mr_ui_tick(uint32_t now_ms) {
    // ★★ §B84/§B79 FIRST, before any paint decision: both trackers' bounded windows must advance, the emergency slot
    //    must consume one attempt on an unattributable expiry, and the normal slot's expiry must NEVER reach the
    //    emergency model. That whole wiring is `mrui::ui_pump_trackers` — a PURE function in firmware_ui_send.h, which
    //    is what puts it under the native gate. It used to be inline here, where nothing could test it.
    mrui::ui_pump_trackers(s_tracker_emg, s_tracker_normal, s_model, now_ms);

    battery_maybe_sample(now_ms);
    const mrui::UiSnapshot s = build_snapshot(now_ms);
    s_model.on_gesture(s_input.update(mrui::button_pressed(), now_ms), s);
    s_model.on_tick(s);
    // ⓘ §B108: THE UNREAD CLEAR USED TO BE HERE, and that was the defect — `if (screen == inbox) { = 0; }` ran on
    //   EVERY pass, ahead of the blanked check and before a single page had reached the panel. It now happens exactly
    //   once, inside `FrameGate::on_page`, when a COMPLETE and VISIBLE Inbox frame has gone out, and it subtracts only
    //   the counts that frame FROZE — so a message arriving while it paged out is still unread.

    // The emergency slot is checked FIRST and is NOT gated on the normal slot: an alarm must never wait on a DM that is
    // waiting on its e2e ack (spec §2.1). If a canned channel post is still outstanding when the alarm fires, ABANDON
    // its UI tracking and take the channel — its late ctr will not match anything afterwards.
    mrui::SendReq req{};
    if (s_model.emergency_pending()) {
        if (!s_tracker_normal.idle() && s_tracker_normal.kind() != mrui::SendKind::dm) s_tracker_normal.close();
        const bool got_emg = s_model.take_send_request(req);   // ⚠ §B70: this DRAINS — ONE call, into a local
        if (got_emg) ui_perform_send(req, now_ms);
    } else if (s_tracker_normal.idle()) {
        const bool got_req = s_model.take_send_request(req);   // ⚠ §B70: distinct name, still exactly one call
        if (got_req) ui_perform_send(req, now_ms);
    }

    // ★★ ALL of the render POLICY — the §5 MAC-idle gate, the blank, the page continuation, the 2 Hz throttle and the
    //    emergency bypass — is `mrui::FrameGate::step`, a PURE class in firmware_ui_model.h. It moved there for the
    //    same reason `ui_pump_trackers` did: §B104 recorded that none of it had any behavioural probe, and §B107 (a
    //    newer UI state LOST while a frame paged out) was reachable only by human review. This file keeps exactly what
    //    genuinely needs the panel: the frozen copies and the four canvas calls.
    switch (s_gate.step(s_model, s, mac_idle())) {
        case mrui::FrameStep::mac_busy: return;                 // never start OR continue a paint mid-exchange
        case mrui::FrameStep::blank:
            mrui::set_power_save(true);                         // EDGE-triggered: latched in the board, repeats are no-ops
            return;
        case mrui::FrameStep::idle:
            mrui::set_power_save(false);
            return;
        case mrui::FrameStep::open:
            mrui::set_power_save(false);
            // ★ THE FREEZE. Everything the renderer reads is a COPY from here on, so the image cannot tear across the
            //   eight page boundaries this frame will span (spec §5).
            s_frame_state = s_model.state();
            s_frame_snap  = s;
            s_frame_emg   = freeze_emg(s);
            mrui::begin_frame();
            break;
        case mrui::FrameStep::next_page:
            mrui::set_power_save(false);
            break;
    }
    // ★ U8g2 page mode redraws the WHOLE scene per page — the draw calls are CLIPPED, not accumulated. Drawing once at
    //   frame start and then only advancing pages (an earlier draft) leaves seven of eight pages blank. `open` and
    //   `next_page` therefore share this tail, which is what makes "once per page" structural rather than a rule.
    draw_frame(s_frame_state, s_frame_snap, s_frame_emg);       // the FROZEN copies, so the image cannot tear
    s_gate.on_page(mrui::next_page(), s_model, s_counters);
}

void mr_ui_on_push(const MESHROUTE_NS::Push& pu) {
    const uint32_t now = uint32_t(g_hal.now());
    switch (pu.kind) {
        // ★★ §B103/F4: the RECEIVE half is `mrui::ui_route_recv_push` — counters, stamps, and the §4.4 reply scope.
        //    ⚠ `g_node.same_team(pu.team_id)` (node.h:274) is the clause with the SAFETY weight on it, not the channel
        //      equality: `ingest_channel_m` already drops a foreign TEAM's post, but lets a `team_id == 0` LEAF post
        //      through to everyone — so on channel 0 any passer-by used to render as a distress REPLY. See the routing
        //      function for the full argument and for why `same_team` IS the three-clause guard.
        case MESHROUTE_NS::PushKind::msg_recv:
        case MESHROUTE_NS::PushKind::channel_recv: {
            char who[mrui::kLabelCap + 1]; label_for_origin(pu, who, uint8_t(sizeof who));
            (void)mrui::ui_route_recv_push(s_counters, s_model, pu, uint8_t(MR_UI_TEAM_CHANNEL_ID),
                                           g_node.same_team(pu.team_id), who, now);
            break;
        }
        // Every branch that can move the emergency goes through a tracker first — that is what makes a false PICKED UP
        // structurally impossible rather than merely unlikely (spec §2.1). The routing itself is pure and tested.
        default:
            (void)mrui::ui_route_send_push(s_tracker_emg, s_tracker_normal, s_model, pu, now);
            break;
    }
}

#endif  // MR_FEAT_OLED
