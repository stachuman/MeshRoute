// MeshRoute — src/firmware_ui_model.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure screen/state model for the one-button board UI (UI-2). Consumes a gesture plus a plain-data snapshot and
// produces what to draw. Knows nothing of g_node, Arduino or the display — that is what keeps it native-testable and
// every hardware concern in variants/heltec_v3/board_ui.cpp. See
// docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md §2-§5.
//
// DONE here (UI-2): screens + the list-aware cursor, the compose modal and its two canned lists, the send REQUEST
// slots, blanking, the dirty flag.
// DONE here (UI-3): the emergency state machine (arm/cancel/fire, three accepted transmissions, the blocked deadline
// and its next_ms==0 backoff, PICKED UP / NOT HEARD, the sticky REPLY whitelist, the kEmgHoldMs panel hold) and the DM
// outcome machine incl. the late-ack upgrade.
// DONE here (UI-3 QA fixes, §B72-§B75): the retry deadline no longer reserves an arithmetic value, `channel_failed`
// exists and is TERMINAL, `DmState::submitting` is written on hand-off, and an async failure carries its
// `SendFailReason` all the way to the panel.
// DONE here (§B78, owner-ruled 2026-08-04): a terminal `failed` alarm is RETAINED and holds the panel for a full
// `kEmgHoldMs` window from the failure's OWN arrival time — both the synchronous refusal and the async seal failure.
// DONE here (§B71, owner-ruled 2026-08-04, landed by UI-6): the emergency screen's EXIT — a SHORT press on a RETAINED
// outcome returns `_emg` to `idle`. It lives in this pure unit and not in firmware_ui.cpp on purpose: it is gesture
// SEMANTICS, the one thing this header owns, and putting it here is what makes it natively testable (the render layer
// has no automated cover at all). `emg_outcome_retained()` is the derived predicate; see it for the vacuous fifth state.
// DONE here (2026-08-05, the UI-6 QA fix slice — §B101/§B102/§B107/§B108): `FrameGate` (the whole render-policy
// lifecycle: the §5 MAC-idle gate, the blank, page continuation, the 2 Hz throttle and the emergency bypass), the
// `dirty` consumption moved to the FREEZE, `UiInboxCounters` + the unread clear driven by a COMPLETE and VISIBLE Inbox
// frame, `mark_dirty()`, the retained-outcome PRESENTED latch behind B71's exit, and `long_fire` closing the compose
// modal. ★ All five lived in `src/firmware_ui.cpp`, which neither the native suite nor the simulator compiles — which
// is exactly why all five shipped green. See `FrameGate`'s own block for the argument.
// DONE here (2026-08-05, the UI-7 QA fix slice — §B113 + §B64, the two behavioural blockers independent QA raised):
// §B113 `on_send_accepted` gains its THIRD arm, so `ChanState::waiting` is no longer a state nothing could reach and an
// accepted canned post finally reads `SENT, waiting` instead of a `SENDING...` that never resolved; §B64 (OWNER-RULED)
// the TEAM cursor tracks the TEAMMATE by team-plane identity (`_team_sel_id` + `sync_team_cursor`/`note_team_cursor`),
// and a teammate that has left the roster REFUSES the activation loudly (`UiState::team_pick_gone`) instead of
// retargeting the DM to whatever row the stale index happened to land on.
// DONE here (2026-08-05, the two OWNER RULINGS that closed UI-6's open decisions — §R1/§R2, register B109/B110):
// §R1 an accepted REPLY un-blanks the panel (`on_reply`, one line, past both scope guards — it is a TRANSITION, and
// the "not wake-on-any-push" half is the placement); §R2 the emergency overlay ABSORBS a `double` entirely
// (`on_gesture`, its OWN arm — see the ⚠⚠ there for why it must never be folded into §B102's latched short-press arm).
// NOT here yet (UI-4, [[meshroute-mark-done-vs-missing-in-code]]): NOTHING correlates outcomes. Every `on_outcome` /
// `on_send_accepted` / `on_send_refused` call must come from the Task-4 send tracker, which matches ctr/peer/channel
// FIRST — feeding this model a raw Push would let an unrelated channel post complete an emergency (spec §2.1).
// ⚠ `_last_try_ms` (UI-4's outcome window) is the ONE field still written-but-unread here. §B75: the claim that
// `DmState::submitting` was also written-but-unread was FALSE — nothing assigned it at all; `take_send_request` does
// now, and `dm_state()` reads it.
// NOT here at all, by unit boundary: what a screen LOOKS like and the send itself live in src/firmware_ui.cpp
// (UI-6/UI-7); the model only ever emits an index and asks. ⓘ V1 comment repair 2026-08-05: this line also claimed the
// canned message TEXTS were over there — they are not, and have not been since §B66 moved `kDmTexts`/`kChannelTexts`
// HERE (lines 80-89) so `back`'s positional identity has a single `sizeof`-derived declaration.
// DONE here (2026-08-05, §B115): the emergency DISPLAY ORDINAL and the one string that renders it
// (`emg_attempt_ordinal` / `emg_attempt_line`) — presentation split cleanly from `_tries`, which stays the limit's only
// truth. The string is formatted in this pure unit precisely so a native test can assert the VISIBLE bytes.
#pragma once
#include <cstddef>   // std::size_t (UI-3's copy_clamped) — do NOT rely on <cstdint> to drag it in transitively
#include <cstdint>
#include <cstdio>    // snprintf — §B115: the ONE panel string this pure unit formats, so the native suite can assert
                     // the VISIBLE BYTES (see `emg_attempt_line`). Free: every TU that includes this header already
                     // pulls <cstdio> through firmware_ui_send.h or firmware_ui.cpp.
// ★ §B73: the ONE lib/core dependency, and it is deliberate. Spec §2.1 rule 6 requires the WHOLE `SendFailReason` to
// reach the UI ("others -> a compact reason"), so the alternative was a parallel 18-value UI mirror of a core enum
// that command.h documents as APPEND-ONLY — the exact fork U1 forbids. `command.h` is the app seam: typed PODs, no
// Arduino, no heap, no `Node` (it is what fw_main and the sim both parse INTO), so the unit stays native-testable and
// board-free. Precedent: src/firmware_config_parse.h includes protocol_constants.h for the same reason.
#include "command.h"
#include "firmware_ui_input.h"

namespace mrui {

inline constexpr uint32_t kBlankMs      = 15000;
inline constexpr uint8_t  kMaxTeamRows  = 8;    // spec §11: a 3-10 member group; the snapshot reports the TRUE total too
inline constexpr uint8_t  kMaxInboxRows = 8;
inline constexpr uint8_t  kLabelCap     = 14;   // display-clamped teammate label

enum class Screen  : uint8_t { status = 0, team, inbox, send, count };
enum class Compose : uint8_t { none = 0, dm, channel };

// ★★ §B66 CLOSED HERE 2026-08-05 (UI-7) — THE COUNT IS NOW DERIVED FROM THE TABLE, so the two cannot disagree.
// The LAST row of a compose list is `back, don't send` and the model identifies it POSITIONALLY (`cursor + 1 == n`),
// so a table that grows without its count turns `back` into a SEND. UI-6 bound them with a `static_assert` across a TU
// boundary — a build failure instead of a mis-send, which was the right interim but still TWO declarations. UI-7 needs
// the strings in a PURE header anyway (`ui_compose_send_line` composes the console line and the native suite asserts
// it byte-for-byte), so the tables moved here and the counts are `sizeof`-derived. ⇒ one declaration, no assert to
// keep in step, and B66's own "durable cure: one table with the count derived from it" verbatim.
// ★ Owner-fixed strings (plan §"Constants fixed by the owner"). ⛔ The emergency body is NOT a compose row — it has no
//   list and no cursor — so it lives beside them rather than inside either table.
inline const char* const kDmTexts[]      = { "Are you OK?",      "I'm OK",   "back, don't send" };
inline const char* const kChannelTexts[] = { "Got your message", "All good", "back, don't send" };
inline constexpr uint8_t kDmTextCount      = uint8_t(sizeof kDmTexts      / sizeof kDmTexts[0]);
inline constexpr uint8_t kChannelTextCount = uint8_t(sizeof kChannelTexts / sizeof kChannelTexts[0]);
// ★ The SENDABLE prefix of each table — everything but the trailing `back` row. Derived, never restated, for exactly
//   the B66 reason: this is the bound `ui_compose_send_line` refuses on, so a hand-written `2` here would be the same
//   positional coupling one level down. An index at or past it names `back` (or nothing) and must REFUSE, not clamp.
inline constexpr uint8_t kDmSendableTexts      = uint8_t(kDmTextCount - 1);
inline constexpr uint8_t kChannelSendableTexts = uint8_t(kChannelTextCount - 1);
inline constexpr const char* kEmergencyText = "I'm in danger";

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
    // ★★ §B108 round 2 — TWO THINGS, PUBLISHED TOGETHER, and that togetherness is the point. `unread_*` is the
    // DISPLAY count, clamped to `kUnreadCap` because the bar has three digits; `arr_*` is the uncapped ARRIVAL SERIAL
    // those digits were derived from. `UiInboxCounters::publish` writes all four in one call, so a frame can never
    // freeze a serial that its own rendered number did not reflect. Never assign one without the other.
    uint16_t unread_dm = 0, unread_ch = 0;
    uint32_t arr_dm = 0, arr_ch = 0;
    uint32_t last_dm_age_s = UINT32_MAX, last_ch_age_s = UINT32_MAX;
    uint8_t  team_shown = 0, team_total = 0;      // shown <= kMaxTeamRows; total = rt_team_count() (spec §3.3)
    TeamRow  team[kMaxTeamRows] = {};
    uint8_t  inbox_shown = 0; uint16_t inbox_total = 0;
    InboxRow inbox[kMaxInboxRows] = {};
    uint8_t  my_team_id = 0; uint32_t team_id = 0;
    int32_t  batt_mv = -1;                        // <0 = unavailable -> render "--", never a guess
    bool     team_build = true;
};

// ★ THE UI-LOCAL UNREAD / RECENCY COUNTERS (spec §6). They were six file-static variables in firmware_ui.cpp, and
// they moved here for the §UI-6 GLUE reason: BOTH things that move them — a push arriving (`ui_route_recv_push`) and
// an Inbox frame actually reaching the panel (`FrameGate`) — are decided by code the native suite drives, and a
// counter no test can reach is exactly where §B108 hid. `have_*` keeps "never received" distinct from "received at
// t = 0"; without it a fresh boot renders "0s ago". Session-scoped by design: `Inbox` exposes no read cursor, and a
// reboot resetting them reads, for a glanceable bar, as "since you last looked".
inline constexpr uint16_t kUnreadCap = 999;   // the bar renders 3 digits — ★ A DISPLAY LIMIT ONLY, see below
struct UiInboxCounters {
    // ★★★ §B108 ROUND 2 — ARRIVAL IDENTITY IS SEPARATE FROM THE DISPLAY CAP, and conflating them re-created the very
    // harm B108 exists to prevent. The first fix stored a CAPPED `unread_*` and had a completed frame SUBTRACT the
    // count it had frozen. At saturation that loses a message: the frame freezes 999, an arrival during the eight
    // paging ticks cannot raise 999 (`if (unread < kUnreadCap) ++unread`), and the completion subtracts 999 -> 0. The
    // message is marked read having NEVER been on the panel. ⚠ The old code half-knew this — its clamp's own comment
    // named the saturation case. A CLAMP HIDES IT; IT DOES NOT FIX IT.
    // ⇒ `arr_*` counts EVERY arrival, monotonically and uncapped. `read_*` is a watermark that a COMPLETE and VISIBLE
    //   Inbox frame advances to the serial that frame FROZE. `unread_* = arr_* - read_*`, and the cap is applied
    //   nowhere but `publish`, on its way to the pixels.
    // ★ WRAPAROUND, chosen rather than inherited: `uint32_t` with UNSIGNED MODULAR subtraction. Unsigned overflow is
    //   defined in C++, so the serials themselves wrapping is harmless — the ONLY invariant is that the TRUE unread
    //   count stays below 2^32 between two reads. `uint16_t` would have cost 8 B less and wrapped at 65 536: a device
    //   left unattended for a week on a channel carrying one post per 10 s reaches ~60 000, the SAME ORDER, so it
    //   would have been "probably fine" — the reasoning class that produced this bug. 2^32 is 136 years at one
    //   arrival per second: unreachable, not merely unlikely.
    uint32_t arr_dm  = 0, arr_ch  = 0;    // monotonic arrival serials — NEVER capped, NEVER reset
    uint32_t read_dm = 0, read_ch = 0;    // read watermarks — moved ONLY by FrameGate::on_page
    uint32_t last_dm_ms = 0, last_ch_ms = 0;
    bool     have_dm = false, have_ch = false;

    uint32_t unread_dm() const { return arr_dm - read_dm; }   // modular; see the wraparound note above
    uint32_t unread_ch() const { return arr_ch - read_ch; }
    static uint16_t capped(uint32_t n) { return n < uint32_t(kUnreadCap) ? uint16_t(n) : kUnreadCap; }
    // ★ THE ONE CONVERSION PATH into a frame's snapshot (U2) — never rebuild these four at a call site, because the
    //   whole correctness argument is that the display count and the serial come from the SAME instant.
    void publish(UiSnapshot& s) const {
        s.unread_dm = capped(unread_dm()); s.arr_dm = arr_dm;
        s.unread_ch = capped(unread_ch()); s.arr_ch = arr_ch;
    }
};

// ★★★ THE INBOX ROW BUDGET (UI-7, spec §6.1), AND IT IS PURE FOR ONE MEASURABLE REASON: `Inbox::pull()` visits the DM
// block FIRST and the channel block SECOND, both oldest-first, with NO limit parameter of any kind (inbox.h:106-109
// — the only flow control is returning false from the callback). So "keep the newest 8" over one shared pool lets a
// chatty channel evict EVERY DM row, on a screen whose entire purpose is showing both. The spec calls that out and
// the plan repeats it; it is also exactly the kind of rule that reads as obviously-satisfied and is not.
// ⇒ TWO independent rings, `kMaxInboxRows / 2` each, filled newest-wins, and the whole thing is host-testable with a
//   handful of pushes. `firmware_ui.cpp` owns only the `pull()` trampoline and the text clamping.
// ⓘ The panel order stays BLOCK order (all DM rows, then all channel rows), never chronological: the two seq spaces
//   are independent and there is no shared clock to interleave on — spec §6.1 says adopting interleaving needs a
//   stated reboot/uptime rule first.
inline constexpr uint8_t kInboxRowsPerKind = uint8_t(kMaxInboxRows / 2);
class InboxRowBudget {
public:
    void reset() { _n_dm = 0; _n_ch = 0; }
    // Newest-wins: `pull` hands rows oldest-first, so once a ring is full each further row displaces the OLDEST it
    // holds. Shifting `kInboxRowsPerKind - 1` small structs is bounded and happens only past the cap.
    void add(const InboxRow& r) {
        InboxRow* buf = r.is_dm ? _dm : _ch;
        uint8_t&  n   = r.is_dm ? _n_dm : _n_ch;
        if (n < kInboxRowsPerKind) { buf[n++] = r; return; }
        for (uint8_t i = 1; i < kInboxRowsPerKind; ++i) buf[i - 1] = buf[i];
        buf[kInboxRowsPerKind - 1] = r;
    }
    // ★ THE ONE CONVERSION PATH into the snapshot (U2), like `UiInboxCounters::publish`. `total` is what `pull`
    //   VISITED, so the screen can say the list is truncated instead of implying it is complete (spec §6.1).
    void publish(UiSnapshot& s, uint16_t total) const {
        uint8_t k = 0;
        for (uint8_t i = 0; i < _n_dm && k < kMaxInboxRows; ++i) s.inbox[k++] = _dm[i];
        for (uint8_t i = 0; i < _n_ch && k < kMaxInboxRows; ++i) s.inbox[k++] = _ch[i];
        s.inbox_shown = k;
        s.inbox_total = total;
    }
    uint8_t dm_count() const { return _n_dm; }
    uint8_t ch_count() const { return _n_ch; }
private:
    InboxRow _dm[kInboxRowsPerKind] = {};
    InboxRow _ch[kInboxRowsPerKind] = {};
    uint8_t  _n_dm = 0, _n_ch = 0;
};

// ---------------------------------------------------------------------------------------------------- UI-3
// ★★ OWNER RE-RULED 2026-08-04: 120000 -> 30000. ⚠ THIS LINE AND THE CONSTANTS TEST ARE THE ONLY TWO PLACES THE
// NUMBER MAY APPEAR. Nothing else — no comment, no test, no doc line — restates it: the first §B78 write-up hardcoded
// "120 s" in prose and went stale the instant the owner re-ruled, so every other reference derives from `kEmgHoldMs`.
inline constexpr uint32_t kEmgHoldMs            = 30000;
inline constexpr uint32_t kCancelledMs          = 1000;
inline constexpr uint8_t  kEmgMaxTries          = 3;      // THREE TRANSMISSIONS, counted on acceptance
inline constexpr uint32_t kBlockedBackoffMinMs  = 2000;   // next_ms==0 policy: 2s, doubling, capped
inline constexpr uint32_t kBlockedBackoffMaxMs  = 30000;
inline constexpr uint32_t kArmToFireMs          = 3500;   // MUST match InputCfg::fire_ms (pinned by a test)

// ★★★ §B115 — THE TWO NUMBERS THE ALARM CARRIES, AND WHICH ONE IS THE TRUTH. Stated here because the shipped defect
// was EXACTLY a drift between them: the panel rendered `attempts() + 1` UNCONDITIONALLY while the airtime bound read
// `_tries`, so three posts on the wire displayed `2 of 3` -> `3 of 3` -> `4 of 3` and **`1 of 3` was NEVER SHOWN**
// (owner-measured on metal; the bound itself HELD — exactly three `M` ids went out, which is correct).
//   `_tries` — ★ THE SINGLE SOURCE OF TRUTH FOR THE LIMIT. It counts ACCEPTED transmissions, moves ONLY in
//              `on_send_accepted` (§B84's unbounded-airtime argument rests on that), and it is the ONLY value
//              `>= kEmgMaxTries` may ever be evaluated on. It is also what `NOT HEARD`'s detail line reports, because
//              there the number IS the measurement ("we transmitted three times and overheard nothing").
//   the ORDINAL (`UiModel::emg_attempt_ordinal`) — ★ PRESENTATION ONLY, and it may NEVER gate a send. It answers
//              "which of the three attempts is in flight right now", which is NOT `_tries`: an attempt that came back
//              `ctr == 0` IS in flight and is DELIBERATELY uncounted (spec §2.1 rule 2 — the bounded expiry spends it
//              later), so the in-flight ordinal is `_tries + 1` there and plain `_tries` once the attempt has been
//              ACCEPTED. An unconditional `+1` is the shipped bug; an unconditional `+0` prints `0 of 3` on the
//              `ctr == 0` first attempt. Both wrong answers have their own native control.
// ⚠ DELIBERATELY NOT CLAMPED to `kEmgMaxTries`. The raw render is the ONLY reason this defect was ever visible; a
//   clamp would have shown `2 -> 3 -> 3` — still wrong on every attempt — and hidden it permanently ([[B108]]'s
//   rejected "clamp instead of fix"). The ordinal is bounded by CONSTRUCTION instead: `on_outcome` refuses to queue a
//   fourth attempt once `_tries >= kEmgMaxTries`. ⇒ a number above `kEmgMaxTries` on the panel is a REAL accounting
//   defect and must stay visible.
//
// ★ THE ONE PLACE THE FIRING DETAIL LINE IS FORMATTED (U1), and it lives in this pure header rather than in the
// renderer for the §UI-6-GLUE reason: `src/firmware_ui.cpp` includes `fw_context.h` (RadioLib, the whole device
// stack), so NOTHING in it is host-compilable and no automated gate can read a string it builds. Here the native
// suite asserts the VISIBLE BYTES. ⚠ That matters specifically for §B115: its first two readings — `2 of 3` and
// `3 of 3` — are individually PLAUSIBLE, so a check asking "does it say N of 3" passes on the bug. The test asserts
// the exact text of the FIRST attempt. `firmware_ui.cpp` must CALL this; `tools/probe_board_ui/run.sh`'s W10 pins it.
inline void emg_attempt_line(char* out, std::size_t cap, uint8_t ordinal) {
    snprintf(out, cap, "attempt %u of %u", unsigned(ordinal), unsigned(kEmgMaxTries));
}

enum class Emergency : uint8_t { idle = 0, arming, firing, blocked, picked_up, not_heard, reply, cancelled, failed };
enum class DmState   : uint8_t { idle = 0, submitting, waiting_ack, delivered, no_key, not_confirmed, failed };
// ★★★ §B69's CARRIER, HALF ONE (UI-7) — THE CANNED-CHANNEL OUTCOME MACHINE, and it is the DmState of the channel path.
// Until now the canned channel post had NO model state at all: `ui_pump_trackers` had to CONSUME the normal tracker's
// expiry and throw it away, with `⛔ Do not "fix" this by calling on_outcome` beside it, because `on_outcome` is the
// EMERGENCY entry point and a canned post's outcome would have moved a live alarm. This enum is the missing entry
// point, and it is what lets B69's two kinds be told apart at the only place that can say them out loud.
// ★ EVERY MEMBER IS REACHABLE ONLY FROM A PATH THAT ESTABLISHED IT — the §2.1 rule this whole arc exists for:
//   `waiting`     — accepted with OUR ctr; the `channel_sent` verdict has not come back yet (it can take ~36 s).
//   `no_relay`    — a `channel_sent` came back for our ctr with `relayed == false`: we transmitted and OVERHEARD NOTHING.
//   `unconfirmed` — §B69: `ctr == 0`, so NO LOCAL HANDLE ever existed. We never listened, so we may not say "no relay";
//                   we cannot establish transmission either, so we may not say SENT. See the ★★ correction below.
//   `relayed`     — a neighbour was overheard re-flooding it. The only member that may say PICKED UP.
enum class ChanState : uint8_t { idle = 0, submitting, waiting, relayed, no_relay, unconfirmed, blocked, failed };
// ★★★ §B69's CARRIER, HALF TWO — THE EMERGENCY'S EVIDENCE, because the alarm's two channel outcomes collapse into ONE
// `Emergency` state and the renderer cannot ask which happened. `on_outcome` maps `channel_no_relay` AND
// `channel_remote_mint` down the SAME path (neither carries relay evidence ⇒ neither may claim PICKED UP ⇒ bounded
// retry), and after the third attempt BOTH land in `Emergency::not_heard`.
// ⇒ `NOT HEARD` is a CLAIM ABOUT A MEASUREMENT — "we transmitted and overheard no relay". An alarm that never held a
//   handle never listened, so on that path the claim is unfounded and the DETAIL LINE must not make it.
//
// ★★★★ B69's PREMISE IS CORRECTED HERE, MEASURED IN SOURCE 2026-08-05, AND THE CORRECTION IS THE OPPOSITE OF THE
//      OBLIGATION AS WRITTEN. B69 (and spec §2.1 rule 2, and this file's own §B68 block) rule the kind must render as
//      **SENT**, on the strength of B39's producer (3): a registered mobile's DELEGATED GLOBAL post, where the HOME
//      mints the ctr and a real MOBILE_SEND DM flies — a genuine SUCCESS. ⛔ **THAT PRODUCER IS STRUCTURALLY DEAD ON
//      THE LINE THIS UI SENDS.** `node.cpp:1401` computes `want_global = c.u.channel.global || !c.u.channel.team`, and
//      every UI channel post carries `-t` with no `-g` ⇒ `want_global == false` ⇒ the `do_send_channel_delegated`
//      branch (`node.cpp:1591-1601`) is never entered. On `-t -e` exactly TWO producers of `queued`/`ctr == 0` remain,
//      and NEITHER is a success: a pre-TX self-gate (`node_channel.cpp:650`, which also pushes `send_blocked`) and a
//      post-mint SEAL FAILURE (`node_channel.cpp:744`). The first normally resolves through `match_blocked` inside the
//      window; the second is the one that reaches expiry.
// ⇒ **RENDERING IT AS "SENT" WOULD BE THE §2.1 FALSE CONFIRMATION THE OBLIGATION WAS WRITTEN TO PREVENT.** The kind
//   stays a SUCCESS SHAPE inside the tracker — §B68's argument is untouched, "a delivered message called failed" is
//   still the error to avoid, and the tracker is GENERIC (a future plain/`-g` UI post would revive producer 3). What
//   changes is only what the PANEL says: **UNCONFIRMED**, never SENT and never "no relay". Reported to the owner as a
//   design change, not edited into the plan.
// ★ STICKY AND ORDERED, `local_tx` > `no_handle` > `none`, and the ordering is the correctness argument: ONE
//   locally-originated attempt whose `channel_sent` came back makes "we listened and heard nothing" TRUE for the alarm
//   as a whole, so a later handle-less attempt must not erase it. Reset only by a NEW alarm (`long_fire`), beside
//   `_tries` — the budget and the evidence describe the same alarm and must start together.
// ⓘ Deliberately NOT a ninth `Emergency` state: the distinction is orthogonal to the machine (it says what the
//   evidence WAS, not where the alarm IS), and a ninth state would have to be threaded through `hold_active`,
//   `emg_outcome_retained` and B71's exit set for no behavioural gain. A flag was B69's own first-named option.
enum class EmgEvidence : uint8_t { none = 0, no_handle, local_tx };
// The COMPACT panel reason. Deliberately NOT a mirror of SendFailReason: `parser` has no core equivalent (the line
// never became a Command), and the three that do are the ones whose remedy differs — encrypt / get a fix / retry
// later. Everything else is `other`, and §B73's `fail_reason()` carries the core reason verbatim beside it, so
// nothing is discarded. Spec §2.1 rule 6, §3.4.1.
enum class RefuseReason : uint8_t { parser = 0, unsealable, no_location, queue_full, other };
// ⚠ An ALIAS, not a UI enum — it IS `MESHROUTE_NS::SendFailReason`, so `mrui::FailReason::x` and
// `meshroute::SendFailReason::x` are the same value of the same type. It exists so this header names its one
// cross-namespace dependency in exactly one place; never redeclare or renumber it here.
using FailReason = MESHROUTE_NS::SendFailReason;

// A correlated outcome. Built ONLY by the send tracker (UI-4) after it has matched ctr/peer/channel — the model never
// sees a raw Push, which is what makes a false PICKED UP structurally impossible (spec §2.1).
struct SendOutcome {
    // ★★ §B68: `channel_remote_mint` is the EIGHTH kind and it is a SUCCESS. Without it Task 4 must call a DELIVERED
    // message failed: on a registered mobile a plain/`-g` GLOBAL post goes through `do_send_channel_delegated` — a real
    // MOBILE_SEND DM flies — but the HOME mints the channel ctr, so `ctr` stays 0 (`lib/core/node.cpp:1565-1573`,
    // register B39). ⇒ `ctr != 0` = we own the handle, exact correlation valid; `ctr == 0` = **no LOCAL handle exists
    // and whether anything flew is not answerable synchronously.**
    // ⓘ Unreachable on the team-plane alarm path (`MR_UI_TEAM_CHANNEL_ID`), so this is type correctness, not a live
    // safety hole. ⚠ Render it as SENT, never as PICKED UP — see register B69: the model has no state that carries
    // that distinction, so the obligation currently rests entirely on UI-6/UI-7's channel path.
    // ★★ §B72: `channel_failed` is the NINTH kind and it is the pre-enqueue failure — a SEAL failure returns
    // `queued` with ctr == 0, so `match_channel_sent` can never fire for it. Without this kind the alarm sits on
    // `SENDING...` for ever after a seal failure: a safety-path defect, not a typing nicety. It is TERMINAL, never a
    // retry — `unsealable` / `no_location` are documented PERMANENT in command.h, so retrying burns the three-alarm
    // budget for nothing.
    // ★★ §B73: both failure kinds CARRY their `SendFailReason`. A reasonless `dm_failed()` left `refuse_reason()`
    // pinned at `other`, so the panel could not say WHY — spec §2.1 rule 6 exists to prevent exactly that. The
    // parameter is REQUIRED (no defaulted `none`): a caller that has a reason must not be able to drop it silently.
    enum class Kind : uint8_t { channel_relayed, channel_no_relay, channel_remote_mint, channel_failed,
                                blocked, dm_acked, dm_no_key, dm_failed, dm_timeout };
    Kind       kind    = Kind::channel_no_relay;
    uint32_t   next_ms = 0;
    FailReason reason  = FailReason::none;   // meaningful for channel_failed / dm_failed ONLY
    static SendOutcome channel_relayed()     { return {Kind::channel_relayed, 0}; }
    static SendOutcome channel_no_relay()    { return {Kind::channel_no_relay, 0}; }
    static SendOutcome channel_remote_mint() { return {Kind::channel_remote_mint, 0}; }   // §B68: accepted, ctr minted elsewhere
    static SendOutcome channel_failed(FailReason r) { return {Kind::channel_failed, 0, r}; }   // §B72: pre-enqueue, terminal
    static SendOutcome blocked(uint32_t n)   { return {Kind::blocked, n}; }
    static SendOutcome dm_acked()            { return {Kind::dm_acked, 0}; }
    static SendOutcome dm_no_key()           { return {Kind::dm_no_key, 0}; }
    static SendOutcome dm_failed(FailReason r) { return {Kind::dm_failed, 0, r}; }             // §B73: reason REQUIRED
    static SendOutcome dm_timeout()          { return {Kind::dm_timeout, 0}; }
};

struct UiState {
    Screen  screen = Screen::status;
    uint8_t cursor = 0;
    Compose compose = Compose::none;
    uint8_t compose_peer = 0;   // bound at ENTRY: the roster can reorder under an open modal, which would retarget it
    // ★★ UI-7: THE SUB-VIEW'S SECOND PHASE. Spec §3.2.1/§3.4.1 require the OUTCOME to replace the canned list *in the
    //    sub-view* ("`SENDING...`", "`DELIVERED to <label>`", "`NO KEY`"), and UI-2 shipped `compose_gesture` CLOSING
    //    the modal as it queued the send — so every state `DmState` can reach had no renderer at all and the one thing
    //    `-a` buys over a channel post ("delivered to that PERSON") was invisible. ⇒ a send switches the same modal
    //    from `list` to `result`; it does not close it.
    // ⓘ A separate flag rather than two more `Compose` members: `Compose` says WHICH list (and therefore which peer and
    //   which text table), and that stays true in the result phase — `draw_compose` still needs it for the header.
    bool    compose_result = false;
    // ★★★ §B64 (OWNER-RULED 2026-08-05): the teammate the TEAM cursor was on has LEFT the roster, so the activation was
    //    REFUSED. It rides `UiState` — the frozen display struct — because it is a thing the panel must SAY (C2: the
    //    refusal is loud, never silent) and because that puts it on the existing freeze path (U2), with no second
    //    plumbing for the renderer to keep in step. The renderer also SUPPRESSES the `>` marker while it is set: a
    //    highlight beside somebody the user did not pick is the same mis-send in display form.
    // ⛔ NOT derived from `cursor >= team_shown`. That predicate happens to be equivalent today, and it is exactly the
    //    positional coupling §B66 exists to warn about — one row added or one clamp changed and it silently means
    //    something else. The flag says what it means.
    bool    team_pick_gone = false;
    bool    blanked = false;
    bool    dirty   = true;
};

class UiModel {
public:
    void on_gesture(Gesture g, const UiSnapshot& s) {
        if (g == Gesture::none) return;
        _last_input_ms = s.now_ms; _seeded = true;
        // ★ spec §4.2: emergency gestures pre-empt EVERYTHING — blank-wake and the compose modal both.
        if (g == Gesture::long_arm || g == Gesture::long_fire || g == Gesture::long_cancel) {
            _st.blanked = false; emergency_gesture(g, s); _st.dirty = true; return;
        }
        if (_st.blanked) { _st.blanked = false; _st.dirty = true; return; }   // the waking press is CONSUMED
        // ★★★ §B71 (OWNER-RULED 2026-08-04, implemented by UI-6): once the alarm has been sent AND ITS RESULT SEEN,
        // the next SHORT press acknowledges it and restores the normal cycle. Before this there was NO exit at all —
        // `_emg` had no path back to `idle`, so a fired alarm owned the panel until reboot.
        // ★ IT IS SAFE BECAUSE THREE RULES COMPOSE, and the ORDER of the two lines above is two of them:
        //   1. long gestures are handled first, so `long` still re-fires from a sticky outcome;
        //   2. a blanked panel consumes its waking press ABOVE, so a retained outcome is ALWAYS displayed before any
        //      press can dismiss it — that is what makes a SHORT press (not a compound gesture) acceptable here;
        //   3. only RETAINED outcomes qualify: an alarm still in flight (`arming`/`firing`/`blocked`-with-a-live-retry)
        //      is sticky, because an outcome the hiker never saw is the failure SAFETY-FIRST exists to prevent.
        // ★ It cannot trap the user: retries are BOUNDED, so the machine always terminates in a retained outcome.
        // ⚠ It is placed BEFORE the compose branch on purpose: the press must act on what the user is LOOKING AT,
        //   which is the alarm overlay, never the list underneath it.
        // ⓘ CORRECTED 2026-08-05 by §B101/F5. This comment used to say a long press "does NOT close" the compose
        //   sub-view, so the overlay rendered over a still-open modal. `long_fire` now closes it and resets the cursor
        //   — see `emergency_gesture`. `long_arm` still does not, because arming is cancellable.
        // ⓘ `double` deliberately gets NO emergency job (spec §4's "double acknowledges" AND "double re-fires" are both
        //   withdrawn — they were the contradiction B71 resolved). ⓘ CORRECTED 2026-08-05 by §R2: this line used to
        //   end "so it falls through to `activate()` as usual", and that fall-through WAS the hidden-mis-send hazard.
        //   The overlay now absorbs the double outright — see the R2 arm below.
        // ★★ §B102/F3: while the overlay is up it OWNS the body (draw_frame returns straight after draw_emergency),
        //    so a short press must NEVER operate the screen underneath — the user cannot see what they would change.
        //    It is CONSUMED either way; it DISMISSES only once the outcome has actually been presented.
        if (g == Gesture::short_press && _emg != Emergency::idle) {
            if (emg_outcome_retained()) { _emg = Emergency::idle; _st.dirty = true; }
            return;
        }
        // ★★★ R2 (OWNER-RULED 2026-08-05) — THE OVERLAY ABSORBS A DOUBLE, ENTIRELY. No emergency action, no operation
        // of the screen underneath, no dismiss, no re-fire. It is a `return`, and nothing else, on purpose.
        // ★ THE HAZARD IT CLOSES IS A HIDDEN MIS-SEND DURING AN ALARM. The overlay OWNS the body (`draw_frame` returns
        //   straight after `draw_emergency`), but a `double` used to fall through to `activate()` / `compose_gesture()`
        //   below — so TWO doubles opened a compose view the user cannot see and then SENT from it, and with a modal
        //   left open under ARMING (which §B101 deliberately does not close, because arming is cancellable) ONE was
        //   enough. That completes what §B102/F3 did for the SHORT press: the overlay is now opaque to BOTH.
        // ⚠⚠ IT IS ITS OWN ARM AND MUST STAY ONE — do NOT merge it into the branch above. That branch is gated on
        //    §B102's presented-latch (`emg_outcome_retained()`), which is F3's answer to a PREMATURE SHORT press.
        //    Sharing it would give `double` the latch, and a double would then DISMISS a presented outcome — the
        //    duty §B71 explicitly WITHDREW ("double gets no emergency job"). A test distinguishes the two arms
        //    directly (`ui-frame: R2 vs F3 …`); it is what fails if a later reader folds them.
        // ⓘ Truthfully NOT "no effect at all": the press still refreshed `_last_input_ms` at the top of this function,
        //   because the user genuinely did act. That is the input-liveness layer, not the gesture contract, and the
        //   hold deadline (§4.3) is what governs the overlay's panel time regardless.
        if (g == Gesture::double_press && _emg != Emergency::idle) return;
        if (_st.compose != Compose::none) { compose_gesture(g); return; }
        // ★★★ §B64: re-anchor the TEAM cursor onto the TEAMMATE it was placed on, BEFORE the gesture acts on it — the
        //    roster is rebuilt every tick and can have reordered since the last one. See `sync_team_cursor`.
        sync_team_cursor(s);
        if (g == Gesture::short_press)  { advance_or_next(s); note_team_cursor(s); _st.dirty = true; }
        else if (g == Gesture::double_press) { activate(s);   _st.dirty = true; }
    }

    void on_tick(const UiSnapshot& s) {
        // ★ B65 (ruled 2026-08-03): the blank timer measures "time since the user last acted", and before the first
        // tick there is no such time. Seeding from 0 blanked the panel on its FIRST tick whenever mr_ui_init() ran
        // >kBlankMs after boot — reachable because NV format-on-corrupt is a shipped path that delays boot by design —
        // leaving a safety device dark the first time it is looked at. A gesture seeds it too, so an early press keeps
        // ownership of the window rather than being overwritten by a late first tick.
        if (!_seeded) { _last_input_ms = s.now_ms; _seeded = true; }
        tick_emergency(s);
        if (_st.compose != Compose::none && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) {
            close_compose();                                                 // never outlive attention; sends nothing
        }
        // ★★★ §B64, AND THE PLACEMENT IS THE POINT: `FrameGate::step` FREEZES the state immediately after this call
        //    (`mr_ui_tick`: on_gesture -> on_tick -> step), so the highlight must already name the remembered teammate
        //    IN THIS SNAPSHOT. Re-anchoring only on a gesture would leave the panel showing `>` beside one teammate
        //    while `activate()` addressed another — the mis-send this ruling closes, arriving from the other side.
        //    ⓘ After the auto-exit above, deliberately: a modal that just closed gets its team cursor back the same tick.
        sync_team_cursor(s);
        if (!_st.blanked && !hold_active(s.now_ms) &&
            elapsed(s.now_ms, _last_input_ms) >= kBlankMs) { _st.blanked = true; _st.dirty = true; }
    }

    const UiState& state() const { return _st; }
    void clear_dirty() { _st.dirty = false; }
    // ★★ §B108: AN ARRIVAL IS A REASON TO REPAINT. `mr_ui_on_push` moved the unread counters and the recency stamps
    // and then asked for nothing, so a new message sat unshown until some UNRELATED gesture or timer happened to
    // invalidate the panel. The counts ride the STATUS BAR, which every screen draws, so this is not Inbox-specific.
    void mark_dirty() { _st.dirty = true; }
    // ★ TWO independent slots, emergency first. One shared slot would let a normal compose action OVERWRITE a queued
    // alarm, and (with the tick's in-flight gate) serialise the emergency behind a DM awaiting its e2e ack — which
    // defeats "long press fires from any screen". Normal work never touches the emergency slot. Spec §2.1.
    // ⚠ THIS CALL DRAINS (register B70) — call it ONCE into a local, never twice in an assertion plus its guard.
    // ★ §B75: draining a DM request enters `DmState::submitting` (spec §3.4.1: "the command was handed to dispatch"
    // -> `SENDING...`). This IS the hand-off point — firmware_ui.cpp performs the send inside the same service pass,
    // so there is no observable gap — and putting it here is what makes the state reachable at all: the enumerator
    // existed but nothing assigned it, so a DM showed `idle` until its result came back. Emergency and canned-channel
    // requests leave `_dm` alone, exactly as on_send_accepted / on_send_refused do.
    bool take_send_request(SendReq& out) {
        if (_emg_req_pending) { _emg_req_pending = false; out = SendReq{SendKind::emergency, 0, 0}; return true; }
        if (!_req_pending) return false;
        _req_pending = false; out = _req;
        if (out.kind == SendKind::dm) { _dm = DmState::submitting; _st.dirty = true; }
        // ★ UI-7: the canned-channel twin, and it also CLEARS a previous transaction's terminal state. Without the
        //   reset a second post would open its result phase still showing the FIRST one's verdict for the instant
        //   before `ui_perform_send` returns — a stale outcome attributed to a message that has not been sent yet.
        else if (out.kind == SendKind::channel_canned) { _chan = ChanState::submitting; _st.dirty = true; }
        return true;
    }
    bool emergency_pending() const { return _emg_req_pending; }

    // ---------------------------------------------------------------------------------- UI-3: emergency + DM
    Emergency emergency() const { return _emg; }
    DmState   dm_state()  const { return _dm; }
    ChanState chan_state() const { return _chan; }
    // ★★ §B69: WHICH of the two collapsed channel outcomes the LIVE alarm actually got. Read it beside
    //    `emergency()`; `not_heard` means two different things depending on it (see EmgEvidence).
    EmgEvidence emg_evidence() const { return _emg_evidence; }
    uint8_t   attempts()  const { return _tries; }
    // ★★★ §B115's DISPLAY ORDINAL — "which of the three attempts is in flight" — and it is PRESENTATION ONLY. See the
    // two-numbers block above `kEmgMaxTries` for the whole argument; the short form is: `_tries` is the LIMIT's single
    // source of truth and this is not a second copy of it, it is a different question. ⛔ Never test it against
    // `kEmgMaxTries`, and never clamp it.
    // ⚠ CONTRACT, the same discipline as `retry_at_ms()`: meaningful only while `emergency() == Emergency::firing`. It
    //   describes an attempt IN FLIGHT, and in every other state there is none — no arithmetic value is reserved to
    //   say so (§B74), the STATE is the predicate.
    uint8_t   emg_attempt_ordinal() const { return uint8_t(_tries + (_emg_attempt_counted ? 0 : 1)); }
    // ★ The compose sub-view's lifetime, which UI-7 needs OUTSIDE the model: it is what bounds a `late_ack` tracker
    //   slot (spec §3.4.1 upgrades NO CONFIRM -> DELIVERED only "while the sub-view is still showing") and, with it,
    //   the ONE normal send slot. See `ui_pump_trackers` — the obligation is discharged there, as a gate, not here.
    bool compose_open() const { return _st.compose != Compose::none; }
    // ⚠ Meaningful ONLY while `emergency() == Emergency::blocked`; after the retry fires the value is the spent
    // deadline. §B74: it is no longer sentinel-encoded, so there is no "no deadline" value to test for — the STATE is
    // the predicate. Any 32-bit value, `0xFFFFFFFF` included, is a legitimate deadline.
    uint32_t  retry_at_ms() const { return _retry_at_ms; }
    // The read side of the reason, in both alphabets. Spec §3.4.1/§4 require the panel to show WHICH refusal it was —
    // a generic failure is one the user cannot act on. `refuse_reason()` is the compact panel code (and the only one a
    // parser refusal has); §B73's `fail_reason()` is the core reason verbatim, for the async failures RefuseReason has
    // no code for.
    // ⚠ CONTRACT, same discipline as retry_at_ms(): both are written only when the model ENTERS a failure state, so
    // read them only while `dm_state() == DmState::failed` or `emergency() == Emergency::failed`. `no_key` /
    // `not_confirmed` need neither — those states ARE their reason (`no_pubkey` / `e2e_ack_timeout`), which is exactly
    // why the tracker gives them their own `Kind` instead of a generic failure plus a reason.
    RefuseReason refuse_reason() const { return _refuse; }
    FailReason   fail_reason()   const { return _fail; }
    // ★ UI-7: the SYNCHRONOUS refusal's `CmdCode`, verbatim. Meaningful only while `refuse_reason() != parser` — see
    //   `on_send_refused`. It is the third alphabet beside the compact reason and §B73's core reason, and it exists
    //   because `err_unsupported` covers five different walls that the panel must at least be able to NAME.
    MESHROUTE_NS::CmdCode refuse_code() const { return _refuse_code; }
    uint8_t   arming_secs_left(const UiSnapshot& s) const {
        if (_emg != Emergency::arming) return 0;
        const uint32_t left = _arm_fire_at_ms - s.now_ms;
        return (left > 60000u) ? 0 : uint8_t((left + 999) / 1000);        // wrap-safe: a huge value means past-due
    }

    // ★★★ §B113 (found by independent QA on UI-7, FIXED 2026-08-05) — THE THIRD ARM, AND WITHOUT IT
    // `ChanState::waiting` WAS A DEAD STATE: assigned zero times in the whole tree, referenced once, by
    // `firmware_ui.cpp`'s `"SENT, waiting"` arm. An ACCEPTED canned post therefore stayed on `submitting`, so the panel
    // read `SENDING...` until either the `channel_sent` verdict (up to ~36 s on a team post) or — first, on the common
    // path — the sub-view's own 15 s auto-exit. ⇒ a SUCCESSFUL send whose only feedback was a spinner that never
    // resolved, contradicting the bench guide's required `SENDING... -> SENT, waiting` verbatim.
    // ★ `waiting` MEANS WE HOLD A HANDLE, and that is the whole reason it may be reached only here: `ui_perform_send`
    //   calls this ONLY after `tr.accept(r.ctr)` with a non-zero ctr (§B39 — a `ctr == 0` result is parked in
    //   `awaiting` and never reaches acceptance), so the state cannot claim a transmission we do not own.
    // ⚠ THE `else` IS THE THIRD ARM OF A THREE-MEMBER ENUM, matching `on_send_refused` line for line (U3) — the two
    //   functions are the accept/refuse twins for the same three kinds and read as such. It must NOT become an
    //   unconditional write: an alarm's acceptance would then relabel a coincident canned post `SENT, waiting`, which
    //   is exactly the §2.1 crossover the two slots exist to prevent. A control pins that directly.
    void on_send_accepted(SendKind k, uint32_t now_ms) {
        // ★ §B115: `++_tries` COUNTS the attempt now in flight, so the flag is SET here (`= true`) and the display
        //   ordinal stops adding one for an attempt `_tries` already includes. This is the only place the flag is SET,
        //   exactly as `queue()` is the only place it is CLEARED (a freshly requested attempt is not yet counted) —
        //   one accept, one request, and the ordinal can never drift from the attempt it names.
        // ⚠ THE SEPARATION IS THE WHOLE FIX, so keep it: `_tries` stays THE LIMIT'S SINGLE SOURCE OF TRUTH and this
        //   remains its ONLY writer (§B84's unbounded-airtime argument rests on that single writer), while the ordinal
        //   is PRESENTATION ONLY and may never gate a send. Full argument: the two-numbers block above `kEmgMaxTries`.
        if (k == SendKind::emergency) { ++_tries; _last_try_ms = now_ms; _emg_attempt_counted = true; }
        else if (k == SendKind::dm)   { _dm = DmState::waiting_ack; }
        else                          { _chan = ChanState::waiting; }   // §B113: the canned-channel twin of waiting_ack
        _st.dirty = true;
    }
    // The SYNCHRONOUS refusal path (a parser reject or an immediate `err_*`) — it never became a core send, so there
    // is no `SendFailReason` for it and `_fail` is cleared to `none` rather than left describing an older failure.
    // ★★ §B78 (owner-ruled 2026-08-04): a terminal FAILED alarm is RETAINED and holds the panel like every other
    // emergency outcome. ⚠ `now_ms` is a PARAMETER and not `_last_input_ms` on purpose: the refusal can arrive well
    // after the gesture that caused it, and anchoring the window on the gesture is the same defect §4.3 was written to
    // kill (the outcome inherits a leftover window and the panel blanks seconds after the news). Only the EMERGENCY
    // branch retains — a DM refusal must not extend the alarm's window.
    // ★ UI-7: `code` is the SYNCHRONOUS `CmdCode` verbatim, and it is REQUIRED — no default (the §B73 precedent: a
    //   caller that has a reason must not be able to drop it silently). It is here because `CmdCode` CANNOT be mapped
    //   onto `RefuseReason` without inventing distinctions the core does not make: `no_key`, `no_identity`, `no_fix`,
    //   `empty` and `unsealable` ALL return `err_unsupported` (node.cpp:1530/1543/1553/1568) and differ only in a
    //   telemetry string the UI never sees. The plan's instruction is exact — "show the generic refusal AND THE CODE;
    //   do not invent a specific reason" — so the compact reason stays generic and the code rides beside it, rendered
    //   through `cmdcode_name` (U1: fw_main.cpp:905 already calls it "the ONE mapper, no second switch").
    // ⚠ CONTRACT, like `retry_at_ms()`: `refuse_code()` is meaningful only while `refuse_reason() != parser`. A line
    //   that never became a `Command` has no `CmdCode` at all, and `RefuseReason::parser` IS that predicate — no
    //   arithmetic value is reserved to mean "none" (§B74's discipline).
    void on_send_refused(SendKind k, RefuseReason r, MESHROUTE_NS::CmdCode code, uint32_t now_ms) {
        _refuse = r; _refuse_code = code; _fail = FailReason::none;
        if (k == SendKind::emergency) { _emg = Emergency::failed; retain(now_ms); }   // terminal + actionable, never a stuck SENDING...
        else if (k == SendKind::dm)   { _dm  = DmState::failed; }
        // ★ UI-7: the canned-channel arm was MISSING, and it was not a cosmetic gap — a refused canned post left
        //   `_chan` on `submitting`, i.e. the sub-view sat on `SENDING...` for ever for a send that never happened.
        //   That is §B72's defect on the non-alarm path, and the same C2 argument applies: fail LOUD, terminally.
        else                          { _chan = ChanState::failed; }
        _st.dirty = true;
    }
    // ★★★ THE CANNED-CHANNEL OUTCOME ENTRY POINT (UI-7), AND IT EXISTS BECAUSE `on_outcome` MUST NOT BE USED FOR THIS.
    // `on_outcome` is the EMERGENCY-capable path: any channel kind it receives may move a LIVE alarm, so routing a
    // canned post's outcome (or its expiry) through it lets an unrelated compose action alter a distress call — the
    // §2.1 false-confirmation class, reached from the one direction the tracker cannot filter (both are channel kinds
    // and both are correctly correlated; only the SLOT distinguishes them). `ui_pump_trackers` therefore had to drain
    // the normal expiry and DISCARD it, with the gap named in-source as Task 7's. This is that entry point.
    // ★ §B69 IS PAID HERE. `channel_no_relay` and `channel_remote_mint` land in DIFFERENT states, because they are
    //   different claims: one is "we transmitted and overheard nothing", the other is "we never held a handle and
    //   never listened". The renderer can finally distinguish them instead of printing one reading for both.
    // ⚠⚠ CORRECTED 2026-08-05 (V1). This paragraph used to end *"can finally say SENT for the second without saying it
    //   for the first"* — B69's obligation as written — AND THE CODE BELOW DELIBERATELY DOES NOT DO THAT. The comment
    //   was the stale half: `channel_remote_mint` maps to `unconfirmed`, which renders `NOT CONFIRMED`, never SENT.
    //   ★ THE MEASUREMENT THAT INVERTED THE OBLIGATION: B69 justified SENT with B39's producer (3), a registered
    //   mobile's DELEGATED GLOBAL post — a genuine success. `node.cpp:1401` computes
    //       want_global = c.u.channel.global || !c.u.channel.team
    //   and every channel line this UI sends carries `-t` with no `-g` (`ui_compose_send_line`), so `want_global` is
    //   FALSE and `do_send_channel_delegated` (node.cpp:1591-1601) is unreachable. On `-t -e` the only surviving
    //   `ctr == 0` producers are a pre-TX self-gate (node_channel.cpp:650) and a post-mint SEAL FAILURE (:744) —
    //   NEITHER a success. ⇒ SENT here would be the §2.1 false confirmation the obligation was written to prevent.
    //   ⓘ The `SendOutcome` kind stays a SUCCESS SHAPE inside the tracker (§B68 is untouched) and the tracker is
    //   generic, so a future plain/`-g` UI post would legitimately revive producer (3). Only the RENDERING differs.
    // ⓘ The DM kinds are REFUSED here rather than handled: a DM outcome belongs to `on_outcome`'s DM arms, which are
    //   already independent of the emergency. Two entry points writing `_dm` would be the fork U1 forbids.
    void on_channel_outcome(const SendOutcome& o, uint32_t now_ms) {
        using K = SendOutcome::Kind;
        (void)now_ms;   // no deadline here: the sub-view's own kBlankMs auto-exit is the display window (spec §3.2.1)
        switch (o.kind) {
            case K::channel_relayed:     _chan = ChanState::relayed;     break;
            case K::channel_no_relay:    _chan = ChanState::no_relay;    break;
            case K::channel_remote_mint: _chan = ChanState::unconfirmed; break;   // ★ §B69: never "no relay", never SENT
            case K::channel_failed:      _chan = ChanState::failed; note_failure(o.reason); break;
            case K::blocked:             _chan = ChanState::blocked;    break;
            // A DM outcome must never reach here — `_dm` has exactly one writer set (on_outcome). Listed explicitly,
            // with no `default:`, so a tenth SendOutcome::Kind fails the build instead of landing silently (§B72).
            case K::dm_acked: case K::dm_no_key: case K::dm_failed: case K::dm_timeout: return;
        }
        _st.dirty = true;
    }
    void on_outcome(const SendOutcome& o, uint32_t now_ms) {
        using K = SendOutcome::Kind;
        switch (o.kind) {   // DM outcomes are independent of the emergency and are handled first
            case K::dm_acked:   _dm = DmState::delivered;     _st.dirty = true; return;   // incl. the LATE-ack upgrade
            case K::dm_no_key:  _dm = DmState::no_key;        _st.dirty = true; return;
            case K::dm_timeout: _dm = DmState::not_confirmed; _st.dirty = true; return;
            case K::dm_failed:  _dm = DmState::failed;        note_failure(o.reason); _st.dirty = true; return;
            // ★ The channel kinds fall through to the emergency section below. They are listed EXPLICITLY and this
            // switch has NO `default:` — §B72 was a kind the type did not carry, and a `default:` is precisely what
            // would let a tenth kind land silently instead of failing the build on -Werror=switch.
            case K::channel_relayed: case K::channel_no_relay: case K::channel_remote_mint:
            case K::channel_failed:  case K::blocked: break;
        }
        // Only a LIVE alarm may be moved by a channel outcome. Anything else — idle, arming, cancelled, failed, and the
        // sticky picked_up / not_heard / reply — is left alone, so coincident channel traffic cannot manufacture or
        // resurrect an emergency (spec §2.1, second line of defence behind the tracker's ctr match). ⓘ That includes
        // channel_failed: a seal failure belonging to no live alarm is dropped whole, reason included.
        if (_emg != Emergency::firing && _emg != Emergency::blocked) return;
        // ★★★ §B69's CARRIER, WRITTEN HERE AND ONLY HERE. It is recorded AFTER the live-alarm guard on purpose: an
        // outcome that may not move the alarm may not describe its evidence either, or a coincident canned post's
        // verdict would relabel a distress result it had no part in (§2.1, the same argument as the guard itself).
        // ★ Monotone, never downgraded — `local_tx` is a fact about the alarm as a whole, and one locally-originated
        //   attempt that came back is what makes "we listened and heard nothing" TRUE. See EmgEvidence.
        if (o.kind == K::channel_relayed || o.kind == K::channel_no_relay) {
            _emg_evidence = EmgEvidence::local_tx;
        } else if (o.kind == K::channel_remote_mint && _emg_evidence == EmgEvidence::none) {
            _emg_evidence = EmgEvidence::no_handle;
        }
        if (o.kind == K::blocked) {
            _emg = Emergency::blocked;
            const uint32_t d = (o.next_ms > 0) ? o.next_ms : next_backoff();
            _retry_at_ms = now_ms + d; _retry_armed = true;    // ★ from the OUTCOME time, not the gesture
            retain(now_ms); _st.dirty = true; return;
        }
        // ★ §B72: pre-enqueue failure. TERMINAL and actionable — never one of the three alarms, and never a retry:
        // `unsealable` / `no_location` are PERMANENT for this route (command.h), so a retry would burn the budget and
        // still fail. Same landing state as the synchronous refusal, because it is the same event arriving late.
        // ★★ §B78 (owner-ruled 2026-08-04): it RETAINS, from the OUTCOME time. This lands identically to
        // on_send_refused because it is the same event arriving late, and `failed` is now in `hold_active()`'s set —
        // which is also what lets UI-6's short press acknowledge it (B71), so the hiker is never trapped on a failure
        // screen they were never shown.
        if (o.kind == K::channel_failed) {
            _emg = Emergency::failed; note_failure(o.reason); retain(now_ms); _st.dirty = true; return;
        }
        if (o.kind == K::channel_relayed) { _emg = Emergency::picked_up; retain(now_ms); _st.dirty = true; return; }
        // ★ channel_no_relay and channel_remote_mint (§B68) share this path deliberately: NEITHER carries relay
        // evidence, so neither may claim PICKED UP, and both leave the alarm unconfirmed ⇒ bounded retry. They differ
        // only in what the RENDERER should say (SENT vs NOT HEARD), and the model has no state for that — register B69.
        if (_tries >= kEmgMaxTries) { _emg = Emergency::not_heard; retain(now_ms); _st.dirty = true; return; }
        _emg = Emergency::firing; queue(SendKind::emergency, 0, 0); _st.dirty = true;
    }
    // ★ Whitelist + "an alarm actually went out". Accepting every non-idle state would let a coincident channel-0 post
    // become REPLY during `arming` (before the user even committed), or after `cancelled`/`failed` — manufacturing
    // confirmation of a message that was never sent. Spec §4.4.
    void on_reply(const char* who, const char* text, uint32_t now_ms) {
        const bool ok = (_emg == Emergency::firing || _emg == Emergency::blocked ||
                         _emg == Emergency::picked_up || _emg == Emergency::not_heard || _emg == Emergency::reply);
        if (!ok || _tries == 0) return;
        copy_clamped(_reply_who,  who,  sizeof _reply_who);
        copy_clamped(_reply_text, text, sizeof _reply_text);
        _emg = Emergency::reply; retain(now_ms); _st.dirty = true;
        // ★★★ R1 (OWNER-RULED 2026-08-05) — AN ARRIVING REPLY UN-BLANKS THE PANEL, and this line is the whole fix.
        // Before it NOTHING un-blanked on an incoming push: `dirty` was set, but `blanked` stayed true, so
        // `FrameGate::step` kept answering `blank` and the answer to a distress call waited behind a panel that is OFF
        // until the hiker happened to press the button. `dirty` alone was never enough — the blank is tested FIRST.
        // ★ IT IS HERE, PAST BOTH GUARDS, AND THAT PLACEMENT IS THE RULING'S "NOT WAKE-ON-ANY-PUSH" HALF:
        //   ① the caller (`ui_route_recv_push`) has already applied §4.4's team scope — a stranger's channel-0 post
        //      never reaches this function at all (§B103); and
        //   ② the whitelist + `_tries == 0` above have already refused everything that is not an answer to an alarm we
        //      actually transmitted. ⇒ what wakes the panel is a REPLY, not team chatter. Both are pinned by their own
        //      controls in test_firmware_ui_send.cpp; without them this fix is indistinguishable from wake-on-any-push.
        // ★ EDGE-TRIGGERED, still (spec §5): this is a STATE TRANSITION, not a per-tick write. The board latches
        //   `set_power_save`, and the ONE resulting DISPLAYON is asserted as a command SEQUENCE, not as a flag.
        // ⓘ NO SECOND TIMER (U1/C2): `retain()` on the line above already gives §4.3's kEmgHoldMs deadline, measured
        //   from THIS reply's own arrival — so the woken panel stays lit for a full window and then blanks with the
        //   state retained. `_last_input_ms` is deliberately untouched, and it is inert either way because
        //   kEmgHoldMs > kBlankMs (asserted in the test, not argued here).
        _st.blanked = false;
    }
    const char* reply_who()  const { return _reply_who; }
    const char* reply_text() const { return _reply_text; }

    // ★★ §B71's exit PREDICATE — "an alarm has reached a terminal, readable answer". DERIVED as a set, not named from
    // the ruling's prose, and one member of that prose WAS VACUOUS: the ruling as first recorded listed "final
    // `blocked`", but `blocked` is never final in this model — `on_outcome`'s `K::blocked` arm ALWAYS sets
    // `_retry_armed`, and `tick_emergency` always re-fires from it, so a `blocked` alarm is by construction still in
    // flight. Including it would have made the exit fire mid-retry, which is precisely what the ruling's first row
    // forbids. ⇒ four states, not five.
    // ✅ §B100, OWNER-AGREED 2026-08-05: the phantom member is now TRIMMED FROM THE RULING ITSELF (the plan's B71
    // table), so document and code finally enumerate the same four. ⛔ Nothing here changed — the trim removed a phantom
    // obligation from a doc, never a behaviour from this predicate; the vacuity stays ASSERTED by the "an IN-FLIGHT
    // alarm does not exit" test, which drives a real `blocked` outcome and checks this returns false.
    // ⓘ `cancelled` is excluded deliberately: nothing was sent, and it self-clears after kCancelledMs, so there is no
    // outcome to acknowledge. `arming` / `firing` are the in-flight rows.
    bool emg_outcome_retained() const {
        const bool terminal = _emg == Emergency::picked_up || _emg == Emergency::not_heard ||
                              _emg == Emergency::reply     || _emg == Emergency::failed;
        return terminal && _emg_seen == _emg_news;   // ★★ §B102: terminal AND ACTUALLY PRESENTED
    }

    // ★★★ §B102/F3 — "AN OUTCOME THE HIKER ACTUALLY SAW", made TRUE rather than ASSUMED.
    // B71's ruling permits a SHORT press to acknowledge an alarm "once its result has been seen", and the argument for
    // safety rested on "a retained outcome is ALWAYS displayed before any press can dismiss it". That was an
    // assumption about TIMING: a frame takes eight ticks, `InputFsm` delivers a gesture that was already in progress,
    // and the MAC-idle gate can hold every one of those ticks — so a press could dismiss a distress result before its
    // FIRST page reached the panel. The user then sees the alarm vanish and never learns the answer.
    // ⇒ `retain()` — which EVERY retained outcome goes through, and which a NEW reply re-enters with new text — bumps
    //   `_emg_news`. `FrameGate` reports back the news value a COMPLETED frame actually put in front of the user. The
    //   exit opens only when the two agree, so "seen" is a measurement.
    // ⓘ NO ARITHMETIC VALUE IS RESERVED (§B74's discipline): both counters start at 0 and the FIRST `retain()` makes
    //   `_emg_news` 1, while `terminal` is false for `idle` — so the initial 0 == 0 can never open the exit.
    uint32_t emg_news() const { return _emg_news; }
    void mark_outcome_presented(Emergency shown, uint32_t news) {
        if (shown == _emg && news == _emg_news) _emg_seen = news;   // the frame must match the CURRENT news, not an older one
    }

protected:
    // Wrap-safe elapsed time. millis() wraps at ~49.7 days; `a >= b` would break across it, this does not.
    static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
    void queue(SendKind k, uint8_t peer, uint8_t idx) {
        // ★★ §B115: THE ORDINAL'S ONE WRITE POINT ON THE REQUEST SIDE, and `queue()` is chosen because it is the ONE
        // choke point all three alarm requests go through — `long_fire`, `on_outcome`'s bounded retry and
        // `tick_emergency`'s blocked retry. A new attempt is requested and `_tries` has not counted it yet, so the
        // ordinal is `_tries + 1` from here until `on_send_accepted` (or the §B84 expiry that stands in for it) counts
        // it. ⛔ Do not also reset it in `long_fire`: `long_fire` ends by calling this, and a second writer is how the
        // two numbers drift apart again.
        if (k == SendKind::emergency) { _emg_req_pending = true; _emg_attempt_counted = false; return; }   // its own slot; never overwritten
        _req = {k, peer, idx}; _req_pending = true;
    }

    // ★ Spec §4.3: every retained emergency state refreshes the `kEmgHoldMs` panel-on DEADLINE — long_fire, then
    // blocked / picked_up / not_heard / reply, and (§B78) `failed`. Anchoring it only at long_fire (an earlier draft)
    // meant an outcome or a reply arriving a whole window later inherited the leftover time and the panel blanked
    // seconds after the news arrived.
    void retain(uint32_t now_ms) { _emg_hold_until_ms = now_ms + kEmgHoldMs; ++_emg_news; }

    UiState  _st{};
    uint32_t _last_input_ms = 0;
    bool     _seeded = false;            // B65: _last_input_ms is meaningless until the first tick/gesture
    SendReq  _req{};
    bool     _req_pending = false;
    bool     _emg_req_pending = false;   // separate slot: normal work can never clobber a queued alarm

    // UI-3 state. Every deadline comparison is a wrap-safe unsigned difference, never `now >= then`.
    // ★★ §B74: `_retry_armed` replaces a `0xFFFFFFFF` "no deadline" SENTINEL, and the bug it fixes was on the alarm
    // path. `_retry_at_ms = now_ms + next_ms` is unbounded, so it can land exactly on any 32-bit value — `now =
    // 0xFFFFF000`, `next_ms = 0xFFF` produces `0xFFFFFFFF` from perfectly ordinary inputs. tick_emergency then refused
    // to even examine the deadline and the emergency stayed BLOCKED FOR EVER. ⇒ a separate flag, so NO arithmetic
    // value is reserved. Never reintroduce a magic deadline value here.
    Emergency    _emg    = Emergency::idle;
    DmState      _dm     = DmState::idle;
    ChanState    _chan   = ChanState::idle;    // UI-7: the canned-channel twin of _dm; §B69's carrier for that path
    RefuseReason _refuse = RefuseReason::other;
    FailReason   _fail   = FailReason::none;   // §B73: the core reason, verbatim, beside the compact one
    // UI-7: the SYNCHRONOUS refusal's CmdCode, verbatim. Read only while `_refuse != parser` — see on_send_refused.
    MESHROUTE_NS::CmdCode _refuse_code = MESHROUTE_NS::CmdCode::queued;
    // ★★ §B69: which of the two collapsed channel outcomes THIS alarm actually got. Sticky, monotone, reset by a new
    //    alarm. It is what stops `NOT HEARD`'s detail line from claiming a measurement the alarm never took.
    EmgEvidence  _emg_evidence = EmgEvidence::none;
    uint8_t  _tries = 0;                 // ACCEPTED transmissions, never requests (spec §4) — ★ THE LIMIT'S ONLY TRUTH
    // ★★ §B115: has `_tries` counted the attempt CURRENTLY IN FLIGHT? Set false by `queue()` (a new attempt is asked
    // for), true by `on_send_accepted` (it has been counted). Read ONLY by `emg_attempt_ordinal()`, i.e. by the panel —
    // ⛔ it is not an input to any airtime, retry or terminal decision, and must never become one.
    bool     _emg_attempt_counted = false;
    bool     _retry_armed       = false; // §B74: the blocked-retry deadline is live (NOT encoded in _retry_at_ms)
    uint32_t _retry_at_ms       = 0;
    uint32_t _last_try_ms       = 0;     // UI-4's outcome window; written here, unread until then
    uint32_t _arm_fire_at_ms    = 0;
    uint32_t _cancelled_until_ms = 0;
    uint32_t _emg_hold_until_ms = 0;
    uint32_t _backoff_ms        = 0;     // the next_ms==0 UI backoff, doubling to kBlockedBackoffMaxMs
    uint8_t  _last_countdown    = 0;     // so ARMING repaints only when the visible digit changes (spec §4.3)
    uint32_t _emg_news = 0, _emg_seen = 0;   // §B102: retained-outcome news vs. what a COMPLETED frame presented
    // ★★ §B64: the TEAM cursor's selection, held by team-plane IDENTITY rather than by row index. See
    //    `sync_team_cursor` for the whole argument, the C3 plane note and why no arithmetic value is reserved.
    uint8_t  _team_sel_id    = 0;
    bool     _team_sel_valid = false;
    char     _reply_who[kLabelCap + 1] = {};
    char     _reply_text[21]           = {};

    uint32_t next_backoff() {
        _backoff_ms = (_backoff_ms == 0) ? kBlockedBackoffMinMs
                                         : ((_backoff_ms * 2 > kBlockedBackoffMaxMs) ? kBlockedBackoffMaxMs : _backoff_ms * 2);
        return _backoff_ms;
    }
    static void copy_clamped(char* dst, const char* src, std::size_t cap) {
        std::size_t i = 0; for (; src && src[i] && i + 1 < cap; ++i) dst[i] = src[i]; dst[i] = '\0';
    }
    // §B73: record an ASYNC failure in both alphabets — the core reason verbatim, plus the compact code the panel
    // reads. `default:` is deliberate and is NOT the -Wswitch hole §B72 was: command.h's `SendFailReason` is
    // documented APPEND-ONLY and grows on core's schedule, a new reason is legitimately "generic" to this panel, and
    // `_fail` carries it losslessly regardless. The three mapped reasons are the ones whose remedy differs.
    void note_failure(FailReason r) {
        _fail = r;
        switch (r) {
            case FailReason::unsealable:  _refuse = RefuseReason::unsealable;  break;
            case FailReason::no_location: _refuse = RefuseReason::no_location; break;
            case FailReason::queue_full:  _refuse = RefuseReason::queue_full;  break;
            default:                      _refuse = RefuseReason::other;       break;
        }
    }

private:
    void advance_or_next(const UiSnapshot& s) {
        const uint8_t n = list_len(s);
        if (n > 1 && _st.cursor + 1 < n) { ++_st.cursor; return; }
        _st.screen = next_screen(_st.screen, s); _st.cursor = 0;
    }
    // ★★★★ §B64 IS PAID HERE (OWNER-RULED 2026-08-05) — THE SEND TARGET IS THE REMEMBERED TEAMMATE, NEVER A ROW INDEX.
    // ⛔ WHAT THIS LINE USED TO BE: `_st.compose_peer = s.team[_st.cursor % s.team_shown].id`. The modulo kept the read
    //    in range when a later snapshot carried fewer rows than the cursor the previous tick left behind — but its
    //    EFFECT was that a cursor on row 2 meeting a 2-row roster opened the DM modal bound to ROW 0. That is a
    //    MIS-SEND, not a display glitch: "Are you OK?" went to a teammate the user never highlighted. Plan `:135`
    //    deferred it to Tasks 6/7 with *"that is a MIS-SEND … it needs a ruling before Task 7 wires real sends"*, and
    //    Task 7 wired them without resolving it.
    // ★ THE RULING, VERBATIM: *preserve the selection by teammate IDENTITY across roster refreshes; the cursor tracks
    //   the teammate, not the row index; if that teammate has disappeared from the roster, REFUSE activation and
    //   repaint — never silently select another row.*
    // ⛔ AND IT IS NOT A CLAMP. Clamping to `shown - 1` or to `0` is the tempting near-miss and it is the SAME class of
    //   defect one index over: it still SENDS, just to a different wrong teammate. The refusal is what makes the
    //   difference measurable — every clamp queues a request; the ruling queues nothing.
    void activate(const UiSnapshot& s) {
        if (_st.screen == Screen::team) {
            if (!_team_sel_valid) {
                // C2 — FAIL LOUD. `sync_team_cursor` has already announced a pick that vanished; announce it here too
                // so the refusal is self-contained rather than relying on which call ran first.
                // ⓘ An EMPTY roster is left alone: that screen already says "no teammates heard", which IS the reason.
                if (s.team_shown > 0) _st.team_pick_gone = true;
                _st.dirty = true;                        // "and repaint" — stated here, not inherited from the caller
                return;                                  // ⇒ NOTHING is queued. That is the whole assertion.
            }
            _st.compose = Compose::dm; _st.compose_peer = _team_sel_id; _st.cursor = 0;
        } else if (_st.screen == Screen::send) {
            _st.compose = Compose::channel; _st.compose_peer = 0; _st.cursor = 0;
        }
    }
    // ★★★★ §B64 — THE TEAMMATE THE CURSOR IS ON, HELD BY IDENTITY, AND RE-FOUND IN EVERY SNAPSHOT.
    // ★ THE IDENTITY IS THE ROW'S OWN `id`, DERIVED AND NOT INVENTED (U1): it is the team-plane id the snapshot already
    //   carries, the id `compose_peer` already stores, and the id `ui_compose_send_line` already puts on the wire
    //   (`send <id> "<text>" -t -a`). ⇒ the thing tracked and the thing addressed are the SAME value, so they cannot
    //   drift. No new snapshot field was added.
    // ⛔ NOT the row's `label`. That is a DISPLAY string (a resolved name, else `0x<hash>`, else the bare id) and a
    //   display-shaped field must never make an addressing decision — the same rule that killed B48.
    // ⚠ C3, PLANE DISCIPLINE: `_team_sel_id` is a TEAM-plane local id. It is only ever COMPARED against a snapshot
    //   row's `id` and copied into `compose_peer`; it indexes nothing and never reaches a static `node_id`-keyed array.
    //   There is no write path here at all, and on a `!MR_FEAT_TEAM` build `team_build` is false, so `Screen::team` is
    //   unreachable and every line below is inert.
    // ⓘ NO ARITHMETIC VALUE IS RESERVED (§B74's discipline): `_team_sel_valid` is a separate flag, so id 0 — which
    //   `Node::team_local_id()` documents as "not team-DAD'd" — needs no special case and can never be confused with
    //   "nothing is selected".
    void sync_team_cursor(const UiSnapshot& s) {
        // The selection belongs to the TEAM list alone. ⚠ THE COMPOSE GUARD IS LOAD-BEARING: while the sub-view is open
        // `_st.cursor` is the MODAL's list index, not a team row, so touching it here would walk the message selection
        // under the user's fingers — turning "I'm OK" into "Are you OK?", or into `back`. A control pins it directly.
        if (_st.screen != Screen::team || _st.compose != Compose::none) return;
        if (!_team_sel_valid) return;                    // nothing picked yet, or a pick already lost (and announced)
        for (uint8_t i = 0; i < s.team_shown; ++i) {
            if (s.team[i].id != _team_sel_id) continue;
            if (_st.cursor != i) { _st.cursor = i; _st.dirty = true; }   // the teammate MOVED -> the highlight follows
            return;
        }
        // GONE. The cursor may not silently come to rest on somebody else, so the selection is DROPPED and the loss is
        // announced. ★ EDGE-TRIGGERED (spec §5): clearing `_team_sel_valid` is what stops this from re-firing, and
        // therefore from marking the frame dirty, on every subsequent tick.
        _team_sel_valid = false; _st.team_pick_gone = true; _st.dirty = true;
    }
    // The WRITE side: whatever row the cursor has just come to rest on IS the new selection. Called after every cursor
    // or screen move, so "the pick" is always something the user's last press actually pointed at.
    void note_team_cursor(const UiSnapshot& s) {
        if (_st.screen == Screen::team && _st.cursor < s.team_shown) {
            _team_sel_id = s.team[_st.cursor].id; _team_sel_valid = true; _st.team_pick_gone = false;
            return;
        }
        _team_sel_valid = false;                         // an empty roster, or a screen that has no teammates at all
        if (_st.screen != Screen::team) _st.team_pick_gone = false;   // leaving the screen retires its message
    }
    void compose_gesture(Gesture g) {
        // ★★ UI-7: THE RESULT PHASE. Once a send has been issued the modal shows its OUTCOME instead of the list
        //    (spec §3.2.1/§3.4.1), so there is nothing to walk and nothing to activate — the only thing either gesture
        //    can mean is "I have read it".
        // ★ `double` closes because the spec says so verbatim ("the sub-view closes to its parent on an explicit
        //   `double`, or after a bounded display window" — the window is `on_tick`'s kBlankMs auto-exit).
        // ★ `short` closes too, and that is DERIVED from the shipped gesture contract rather than invented: §3.2's
        //   `short` is "advance within the current list; AT THE END, move to the next screen". The result phase has no
        //   list, so every position is the end. ⛔ The alternative — ignore it — would let a user tapping `short`
        //   hold a modal open indefinitely, since every gesture refreshes `_last_input_ms` and so postpones the very
        //   auto-exit that is supposed to bound it. Neither choice can send: this branch queues nothing.
        if (_st.compose_result) {
            if (g == Gesture::short_press || g == Gesture::double_press) close_compose();
            return;
        }
        const uint8_t n = (_st.compose == Compose::dm) ? kDmTextCount : kChannelTextCount;
        if (g == Gesture::short_press) { _st.cursor = uint8_t((_st.cursor + 1) % n); _st.dirty = true; return; }
        if (g != Gesture::double_press) return;
        if (_st.cursor + 1 == n) { close_compose(); return; }                                                // `back`
        queue(_st.compose == Compose::dm ? SendKind::dm : SendKind::channel_canned, _st.compose_peer, _st.cursor);
        // ★★ UI-7: THE MODAL STAYS OPEN. UI-2 closed it here, which left every `DmState` the spec defines with NO
        //    RENDERER — `DELIVERED to <label>` (the one thing `-a` buys that a channel post can never offer),
        //    `NO KEY`, `NO CONFIRM` — all unreachable on the panel. The cursor is still reset, so a re-opened modal
        //    starts on the first message (H7-02/H7-04), and `compose_peer` is untouched so the result can name who it
        //    went to. ⓘ It cannot outlive attention: `on_tick`'s kBlankMs auto-exit applies to BOTH phases.
        _st.compose_result = true; _st.cursor = 0; _st.dirty = true;
    }
    // ★ ONE exit for the sub-view (U1/U2). Four call sites reach it — `back`, the result phase's acknowledgement,
    //   `on_tick`'s auto-exit and §B101's `long_fire` — and the phase flag MUST be cleared with the modal or a
    //   re-opened compose would render an outcome list against a stale result. It sends nothing, by construction.
    void close_compose() { _st.compose = Compose::none; _st.compose_result = false; _st.cursor = 0; _st.dirty = true; }
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
    // Defined inline below the class (UI-3): on_gesture/on_tick call them, so they are declared with the members.
    void emergency_gesture(Gesture g, const UiSnapshot& s);
    void tick_emergency(const UiSnapshot& s);
    bool hold_active(uint32_t now_ms) const;
};

// ---------------------------------------------------------------------------------------------------- UI-3 bodies

inline void UiModel::emergency_gesture(Gesture g, const UiSnapshot& s) {
    if (g == Gesture::long_arm)    { _emg = Emergency::arming; _arm_fire_at_ms = s.now_ms + kArmToFireMs; return; }
    if (g == Gesture::long_cancel) { _emg = Emergency::cancelled; _cancelled_until_ms = s.now_ms + kCancelledMs; return; }
    // long_fire — a NEW alarm: the three-transmission budget, the backoff and any armed retry all reset, so a sticky
    // NOT HEARD can always be re-fired by another long press. (§B74: clearing `_retry_armed` here is belt-and-braces —
    // `_emg` is `firing` from this line on, and only an `on_outcome` block can return it to `blocked`, which re-arms
    // the flag itself. It is written so the flag can never be read stale, not because a stale read is reachable.)
    // ★ §B69: the EVIDENCE resets with the budget. `_tries` and `_emg_evidence` describe the SAME alarm — "three
    //   attempts, and this is what came back" — so a new alarm inheriting the old one's evidence would let a previous
    //   call's locally-heard transmission justify a `NOT HEARD` this one never measured.
    _emg = Emergency::firing; _tries = 0; _backoff_ms = 0; _retry_armed = false; _emg_evidence = EmgEvidence::none;
    // ★★ §B101/F5: COMMITTING an alarm CLOSES the compose modal and resets its cursor. The overlay covers the body,
    //    so a canned message left selected underneath is invisible — and still armed: after the alarm is dismissed the
    //    next `double` sends whatever the cursor happened to be on. An avoidable later mis-send on a safety device.
    // ⓘ `long_arm` deliberately does NOT do this. Arming is cancellable, so destroying the user's list position for a
    //    press they may still cancel would be a second, smaller wrong.
    // ⓘ UI-7 routed it through `close_compose()` so the new RESULT phase is cleared with the modal (one exit, U1).
    close_compose();
    retain(s.now_ms);
    queue(SendKind::emergency, 0, 0);
}

inline void UiModel::tick_emergency(const UiSnapshot& s) {
    if (_emg == Emergency::cancelled && elapsed(s.now_ms, _cancelled_until_ms) < (1u << 31)) { _emg = Emergency::idle; _st.dirty = true; }
    if (_emg == Emergency::blocked && _retry_armed &&
        elapsed(s.now_ms, _retry_at_ms) < (1u << 31)) {                 // wrap-safe "now >= deadline"
        _retry_armed = false; _emg = Emergency::firing; queue(SendKind::emergency, 0, 0); _st.dirty = true;
    }
    if (_emg == Emergency::arming) {                                     // dirty ONLY when the visible digit changes
        const uint8_t d = arming_secs_left(s);
        if (d != _last_countdown) { _last_countdown = d; _st.dirty = true; }
    }
}

// ★ The hold is a DEADLINE, not a duration. Returning kEmgHoldMs and letting on_tick measure it from _last_input_ms
// (an earlier draft) meant a reply that set a fresh _emg_hold_until_ms never actually extended the window, and
// picked_up fell back to the ordinary 15 s blank. Read the field; compare wrap-safely. Spec §4.3.
// ⓘ `arming` is in the retained set per the plan's code, but nothing writes the deadline on long_arm — spec §4.3's
// table lists only long_fire and the retained OUTCOMES. It is inert either way: the long_arm gesture just refreshed
// _last_input_ms, and arming lasts kArmToFireMs, so the kBlankMs blank cannot fire during it. Left as the plan has
// it; do not
// "fix" it by writing the deadline on arm without a ruling, and do not rely on it.
// ★★ §B78 (owner-ruled 2026-08-04): `failed` joins the set. Every OTHER emergency outcome held the panel; the one
// that says the alarm did NOT go out fell back to the ordinary 15 s blank — the worst place to lose the message. Both
// producers now `retain()` from their own arrival time (on_send_refused, and channel_failed in on_outcome).
inline bool UiModel::hold_active(uint32_t now_ms) const {
    const bool retained = (_emg == Emergency::arming    || _emg == Emergency::firing  ||
                           _emg == Emergency::blocked   || _emg == Emergency::picked_up ||
                           _emg == Emergency::not_heard || _emg == Emergency::reply    ||
                           _emg == Emergency::failed);
    return retained && elapsed(_emg_hold_until_ms, now_ms) < (1u << 31);   // now < deadline, wrap-safe
}

// ================================================================================================ §B107 — the FRAME GATE
// ★★★ THE RENDER-POLICY LIFECYCLE, AND IT LIVES HERE FOR THE §UI-6 GLUE REASON. It was six lines of `mr_ui_tick` in
// `src/firmware_ui.cpp` — a TU neither the native suite nor the simulator compiles — and §B104 records the result:
// render policy, the MAC-idle gate and the paint throttle had NO behavioural probe at all, which is why §B107 was
// reachable only by human review. As a pure class it is driven by the native suite and turns red on a revert.
//
// ★ THE FRAME IS FROZEN WHEN IT OPENS. U8g2 page mode re-clips the WHOLE scene once per page, so a frame spans several
//   ticks and everything the renderer reads must be a COPY — live state changing mid-frame tears the image across page
//   boundaries (spec §5). This class owns WHEN; `firmware_ui.cpp` owns WHAT (it holds the frozen copies).
//
// ★★★ §B107 — THE DEFECT, AND IT IS THE ONE THIS CLASS EXISTS TO MAKE UNREPRESENTABLE. The shipped tick cleared
//   `dirty` when the LAST PAGE went out (`if (!s_frame_open) s_model.clear_dirty();`). A frame takes eight ticks, and
//   an outcome or a gesture landing DURING those eight ticks sets `dirty` — which the completing OLD frame then
//   cleared unconditionally. The new state was never painted: PICKED UP / REPLY / FAILED could be lost outright, and
//   on the emergency screen the arming countdown swallowed digits.
// ⇒ `dirty` is CONSUMED AT THE FREEZE, because the freeze is the instant the frame stops tracking the model. Anything
//   raised after it belongs to the NEXT frame. Final-page completion does PRESENTATION BOOKKEEPING ONLY.
//
// ⚠ THE BLANKED BRANCH CLEARED IT TOO, and it does not any more — but the honest measurement is that the second half
//   was INERT: both writers of `blanked = false` (on_gesture's emergency pre-empt and its waking press) also set
//   `dirty = true`, so no wake could observe the discarded invalidation. It is fixed because it is wrong by
//   construction, not because a harm was reproduced.
// ⓘ CORRECTED 2026-08-05 by §R1 (V1). This block used to end: "What IS real and is NOT this class's to fix: nothing
//   un-blanks on an incoming push, so a REPLY arriving at a dark panel waits for a button press. That is a spec
//   question." The owner has since ruled, and it is fixed — but NOT here, and the boundary is the point: the un-blank
//   belongs to the event that decides a post IS a reply (`UiModel::on_reply`), not to the class that merely observes
//   `blanked`. This class still clears nothing and wakes nothing; it reads the flag the model owns.
enum class FrameStep : uint8_t {
    mac_busy = 0,   // §5 rule 1: the MAC is mid-exchange — touch NOTHING, not even the power-save latch
    blank,          // the panel is dark: set_power_save(true) and abandon any open page loop
    idle,           // awake, nothing to paint this pass (not dirty, or inside the throttle)
    next_page,      // draw the FROZEN copies again and push one more page
    open            // freeze, begin_frame, draw, push page 0
};

// 2 Hz. An emergency BYPASSES it (but never the MAC-idle gate), and the model marks itself dirty only when the visible
// countdown digit changes, so the alarm screen does not repaint at tick rate either.
inline constexpr uint32_t kPaintThrottleMs = 500;

class FrameGate {
public:
    // ★ ONE call that DECIDES and commits the state its decision implies, so there is no "remember to tell the gate
    //   what you decided" obligation — the exact shape the §UI-6 GLUE block was created to kill. The single thing it
    //   cannot know is how many pages the panel has left, hence `on_page`.
    FrameStep step(UiModel& m, const UiSnapshot& s, bool mac_idle) {
        if (!mac_idle) return FrameStep::mac_busy;
        if (m.state().blanked) {
            // ⚠ The blank is tested BEFORE the open-frame continuation, deliberately: `set_power_save(true)` abandons
            //   the board's page loop, so holding `_open` across a blank would leave this half of the loop describing
            //   a frame the board has already dropped. Dropping both together keeps the two halves in step.
            _open = false;
            return FrameStep::blank;   // ★ §B107: nothing is CLEARED here — an invalidation raised while dark survives
        }
        if (_open) return FrameStep::next_page;
        if (!m.state().dirty) return FrameStep::idle;
        const bool emg = m.emergency() != Emergency::idle;
        if (!emg && elapsed(s.now_ms, _last_paint_ms) < kPaintThrottleMs) return FrameStep::idle;
        m.clear_dirty();               // ★★ §B107: CONSUMED AT THE FREEZE, never at the final page
        _last_paint_ms = s.now_ms;
        // ★★ §B108 — WHAT THIS FRAME WILL ACTUALLY SHOW, frozen with everything else. "Visible" mirrors `draw_frame`'s
        //   two early returns exactly: the emergency overlay REPLACES the body, and so does the compose modal — a
        //   frame showing either has not shown the Inbox, whatever `screen` says underneath it.
        const UiState& st = m.state();
        _fr_inbox = (m.emergency() == Emergency::idle && st.compose == Compose::none && st.screen == Screen::inbox);
        // ★★ THE SERIALS, not the counts (§B108 round 2). These come from the same `publish` that produced the
        //   `unread_*` this frame will render, so "what the user saw" and "what we will mark read" cannot diverge.
        _fr_arr_dm = s.arr_dm;
        _fr_arr_ch = s.arr_ch;
        _fr_emg   = m.emergency();      // §B102: which outcome these eight pages will put in front of the user...
        _fr_news  = m.emg_news();       // ...and WHICH news it is, so a newer one is not credited to this frame
        return FrameStep::open;
    }

    // The board's `next_page()` verdict: true = more pages to come, false = the frame is COMPLETE.
    // ★★ §B108 — THE ONLY PLACE UNREAD COUNTS ARE MARKED READ. The shipped tick zeroed them on EVERY pass while
    //   `screen == inbox`, ahead of the blanked check and before a single page had reached the panel — so messages
    //   were discarded unseen while blanked, while the MAC was busy, under the emergency overlay, or simply because
    //   the screen had been cycled to. ⇒ a COMPLETE and ACTUALLY VISIBLE Inbox frame is the event.
    // ★ AND IT MARKS READ ONLY WHAT THE FRAME FROZE. A bare `= 0` here would still lose a message that arrived during
    //   the eight ticks the frame took to page out: it was never on the panel, so it was never read.
    void on_page(bool more, UiModel& m, UiInboxCounters& c) {
        _open = more;
        if (more) return;
        // ★★ §B102: the frame is COMPLETE — this is the only moment anything may be called "seen".
        m.mark_outcome_presented(_fr_emg, _fr_news);
        if (!_fr_inbox) return;
        // ★★★ §B108 ROUND 2 — ADVANCE THE WATERMARK; NEVER SUBTRACT A COUNT. Assignment is exact, so there is no
        //   underflow to clamp — and the clamp is what used to hide the defect. The old form subtracted the frozen
        //   COUNT, which at `kUnreadCap` was 999 both before and after a mid-frame arrival, so the arrival was marked
        //   read having never been on the panel. A serial has no saturation, so the arithmetic simply cannot lose it.
        // ⓘ Why the watermark goes to the FULL frozen serial even when the panel rendered a clamped "999": the Inbox
        //   is a glanceable summary with no read cursor, so a complete visible frame means "the user looked" — the
        //   three digits are a rendering limit, never a statement about how many messages were accounted for.
        c.read_dm = _fr_arr_dm;
        c.read_ch = _fr_arr_ch;
        _fr_inbox = false;   // spent: a completed frame marks its OWN arrivals read exactly once
    }

    bool     frame_open()    const { return _open; }
    uint32_t last_paint_ms() const { return _last_paint_ms; }   // diagnostic; the throttle's own state

protected:
    static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }   // wrap-safe, like UiModel's
    bool     _open          = false;
    uint32_t _last_paint_ms = 0;
    // The frozen frame DESCRIPTOR (§B108): what the pages now going out will actually put in front of the user.
    bool      _fr_inbox = false;
    uint32_t  _fr_arr_dm = 0, _fr_arr_ch = 0;   // ARRIVAL SERIALS, not counts — round 2; the cap is display-only
    Emergency _fr_emg = Emergency::idle;
    uint32_t  _fr_news = 0;
};

}  // namespace mrui
