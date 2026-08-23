// MeshRoute — src/firmware_ui_nearby.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-16 N2 (spec docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md §4-N2) — THE NEARBY
// SCAN SCREEN'S PURE UNIT, MODEL SIDE: the row carrier and its IDENTITY, the OWN-TEAM FILTER, the rows the
// operator walks, and every lexeme the screen can print. (The row's three TOKENS are one file over — see
// the include-order note at the bottom of this block.)
//
// ★★★ WHY A FILE OF ITS OWN rather than more of `firmware_ui_model.h`: a battery is per-SOURCE-FILE
//     (`tools/probe_ui_model_mutations.py`'s own header — the backup, the marker and the lock are all
//     keyed by the resolved path). The decisions below are OWNER RULINGS that must each be attacked ON
//     THEIR OWN — the own-team filter (§4-N2 pin 3), the first-observed order that may never be sorted
//     (R-5), BACK's unconditional last row, and (one file over) the tier mapping that must CALL
//     `presence_quality_tier` rather than re-derive one (R-4) plus the fingerprint that must come from THE
//     shared helper (U1). Left inside the
//     3400-line model they would have shared `model`'s ~140 entries; composed in `src/firmware_ui.cpp`
//     they would have had NO battery at all (that TU is compiled by neither the native suite nor the
//     simulator — §B115). ⓘ Same argument `firmware_ui_join.h` made one arc ago, for the same class of
//     ruled decision.
//
// ★★★★ WHAT THIS SCREEN IS, AND THE TWO THINGS IT IS NOT (spec §3 P-3/P-4, design §3.6.4 point 2).
//   It is a READ-ONLY LIST OF TEAM IDS SOMETHING NEARBY ADVERTISED. ⛔ It is NOT a membership, a route,
//   a peer binding or a key — hearing a team id proves only that a beacon carried it, and the id is
//   PUBLIC by design (P-2). ⛔ AND IT TRANSMITS NOTHING: this unit formats a frozen copy of a core-side
//   RAM cache. Nothing here queries, probes, resolves or sends, and the N2 probe arm asserts the TX
//   queue depth and the radio's start count across a full walk rather than arguing it.
//
// ★★ THE ROWS ARE FROZEN PER ENTRY (owner ruling R-10). The model captures the list ONCE, on the
//    `menu -> nearby` transition (`UiModel::load_nearby`), exactly as slice 6 captures `/mrjoin` on its
//    own transition. ⛔ NO auto-refresh: the operator leaves and re-enters to rescan. Two consequences,
//    and both are ruled rather than incidental — a team that walks into range mid-walk cannot insert a
//    row under the operator's cursor, and *"the scan never selects by itself"* (§3.6.5) is trivially true.
//
// ⓘ DONE-VS-MISSING, STATED IN CODE BECAUSE DOCS ROT ([[meshroute-mark-done-vs-missing-in-code]]):
//   N2 is the LIST and NOTHING ELSE. ⛔ There is no join here, no confirmation and no act — a `double` on
//   a team row deliberately does nothing yet, and the arm that will land it is §UI-16 N3
//   (`JOIN <fingerprint>?` over the existing `TeamRequest{ mint=false, team_id }` transaction). That is
//   [[B222]]'s rule honoured rather than waived: a transition lands WITH its flow, never one slice ahead.
//
// ⚠⚠ WHY THE SCREEN'S PURE LOGIC IS **TWO** HEADERS AND NOT ONE — REPORTED, because the spec's file list named one.
//    It is an INCLUDE-ORDER FACT of this tree, measured rather than preferred: `src/firmware_ui_chrome.h:36` includes
//    `firmware_ui_model.h`, so ⛔ NO HEADER THE MODEL ITSELF INCLUDES MAY INCLUDE CHROME — the model would be
//    mid-parse and chrome's `UiState` / `Emergency` uses would not compile. THIS file is model-included (the
//    `UiSnapshot` array and the `UiState` frozen copy need its carriers), so it is chrome-FREE; the two tokens that
//    genuinely need chrome's shared formatters live one file over in `firmware_ui_nearby_row.h`.
// ★ THAT IS THIS TREE'S OWN LAYERING, NOT AN INVENTION: `TeamRow` lives in `firmware_ui_model.h` while its row
//   formatter `ui_team_row` lives in `firmware_ui_team.h` (which includes model + chrome), and `firmware_ui_status.h`
//   / `firmware_ui_geo.h` sit the same way. ⇒ each half gets its OWN mutation target (`uinearby` / `uinearbyrow`),
//   which is strictly more coverage than one file would have had, and ⛔ no ruled decision moved into a TU that no
//   gate compiles.
#pragma once
#include <cstddef>
#include <cstdint>
#include "protocol_constants.h"    // cap_team_seen — the observation ring's own capacity (⛔ never a second literal)

namespace mrui {

// ★ THE CAPACITY IS THE RING'S OWN, ⛔ never a second literal: `protocol::cap_team_seen` is what
//   `Node::_team_seen` holds, so a re-tuning of the cache re-sizes this list with it and the published
//   array can never be smaller than the thing it publishes.
inline constexpr uint8_t kMaxNearbyRows = MESHROUTE_NS::protocol::cap_team_seen;

// ★★★ ONE OBSERVED TEAM, AS THE PANEL NEEDS IT — the projection of ONE `MESHROUTE_NS::TeamSeen`.
// ⛔ IT CARRIES THE RAW `snr_q4`, ⛔ NOT A TIER, and that is the point of the field rather than an
//    accident: the tier is `presence_quality_tier`'s answer and is computed at the RENDER, in this pure
//    unit, where a mutation can attack it. A tier computed at the publish site (`build_snapshot`) would
//    put the ruling in the ONE TU no automated gate compiles (§B115).
// ⓘ `team_id` IS THE ROW'S IDENTITY (§B66, spec §4-N2 pin 6) — ⛔ never its index. It is also the value
//   N3's confirmation will carry, which is why it rides the row WHOLE (U2) rather than being re-derived
//   from the six-character fingerprint (spec §3 P-7: the token is a human selection aid, never an
//   authority — [[B48]]'s class).
struct NearbyRow {
    // ★★ `uint64_t`, TAKEN WHOLE, for the reason `UiSnapshot::home_confirm_age_ms` is: the ONE bucketing
    //    of a millisecond age in this tree is `ui_fmt_home_age`, and it takes `uint64_t` all the way into
    //    its divisions. A `uint32_t` here would invite the ~49.7-day wrap this project has already fixed
    //    once. ⓘ The value is bounded by `team_seen_retain_ms` in practice (a row past the window is not
    //    returned by the accessor at all) — but the TYPE is not where a bound belongs.
    uint64_t age_ms = 0;
    uint32_t team_id = 0;
    int16_t  snr_q4 = 0;
    // ⛔ FALSE MEANS THE AGE IS MEANINGLESS AND THE ROW SAYS SO (`--`), ⛔ never a fabricated `0s`. It is
    //    `ui_fmt_home_age`'s own `ever` parameter, published rather than re-invented: the publish site
    //    cannot date an observation stamped AFTER the instant it read (a clock that stepped backwards),
    //    and "I cannot date this" is a state that formatter already has a token for.
    bool     age_valid = false;
    // ★ NAMED, ⛔ never implicit tail padding — the `PeerLoc::reserved` / `TeamSeen::reserved` rule:
    //   implicit padding is INDETERMINATE after `NearbyRow{}`, which would make any whole-record compare
    //   (or a `memcmp` in a future test) unsound.
    uint8_t  reserved = 0;
};

// The FROZEN, OWN-TEAM-FILTERED list the screen walks. ⛔ A fixed array + a count, never a container:
// this is embedded code and the count is what the cursor is bounded by, so the two come from ONE
// construction (`ProvRowList`'s rule, three menus up).
struct NearbyList {
    NearbyRow row[kMaxNearbyRows] = {};
    uint8_t   n = 0;
};

// ★★★★ THE OWN-TEAM FILTER, AND IT LIVES **HERE** BECAUSE THAT IS THE RULING (spec §4-N1 pin 9 /
//      §4-N2 pin 3): N1 records our own team id like any other, so *"which teams are audible"* and
//      *"which of them are worth offering"* each have exactly ONE authority. ⛔ Filtering at the write
//      site would give the second question two.
// ⓘ A teamless joiner (`own_team_id == 0`) loses NOTHING to this: N1's write gate is `peer_team != 0`,
//   so no recorded row can carry 0 and the comparison simply never matches. ⛔ It is written as a plain
//   equality rather than guarded on `own_team_id != 0` — a guard would be a second rule about the same
//   decision, and a row that DID carry 0 is not a team anybody may join either.
// ⓘ ORDER IS THE SOURCE'S, PRESERVED (owner ruling R-5): the ring's own FIRST-OBSERVED order is
//   STRUCTURAL (`lib/core/team_seen_ring.h` refreshes in place and, on overflow, shifts the stalest out
//   and appends), so this copy sorts NOTHING — ⛔ least of all by signal, which would re-order the list
//   under the operator's cursor between two entries.
inline NearbyList nearby_capture(const NearbyRow* src, uint8_t n, uint32_t own_team_id) {
    NearbyList out{};
    if (!src) return out;                                  // ⛔ FAILS CLOSED: no source is an EMPTY list, never a guess
    if (n > kMaxNearbyRows) n = kMaxNearbyRows;            // the publisher's bound, restated where the copy happens
    for (uint8_t i = 0; i < n; ++i) {
        if (src[i].team_id == own_team_id) continue;       // ★ the team we are ALREADY in is not a candidate
        out.row[out.n++] = src[i];                         // ⛔ the WHOLE row (U2), never rebuilt field by field
    }
    return out;
}

// ------------------------------------------------------------------------- the rows, AS IDENTITIES
// ★★ §B66's rule, a fourth menu deep: the visible list is the filtered one, so a row's meaning may not
//    be derived from its position — the own-team row being dropped puts every later team one row up.
// ⛔ AND THE ROW CARRIES THE WHOLE `NearbyRow`, ⛔ not an index into the list: an index is the second
//    authority §B66 exists to forbid, and the identity a later slice acts on (the full 32-bit `team_id`)
//    must be the identity the panel drew.
struct NearbySelRow {
    NearbyRow team{};        // MEANINGFUL ONLY while `!back`
    bool      back = false;
};
struct NearbySelList {
    NearbySelRow row[kMaxNearbyRows + 1] = {};   // every retained team at most, plus the UNCONDITIONAL BACK
    uint8_t      n = 0;
    // ⛔ FAILS CLOSED (C2), exactly as `ProvRowList::at` and `JoinSelList::at` do: an out-of-range index
    //    names NO row and the caller must do nothing rather than being handed a plausible one.
    bool at(uint8_t i, NearbySelRow& out) const { if (i >= n) return false; out = row[i]; return true; }
};
// ⓘ BACK IS UNCONDITIONAL — the same rule `provision_rows` and `join_sel_rows` state: leaving must never
//   depend on a store, a build flag or, here, on whether anything was heard. ⇒ an EMPTY scan still opens
//   a list the operator can leave; it just has nothing to offer, which is the honest answer.
inline NearbySelList nearby_sel_rows(const NearbyList& l) {
    NearbySelList out{};
    for (uint8_t i = 0; i < l.n && i < kMaxNearbyRows; ++i) {
        out.row[out.n].team = l.row[i];
        out.row[out.n].back = false;
        ++out.n;
    }
    out.row[out.n].back = true;
    ++out.n;
    return out;
}

// ---------------------------------------------------------------------------------- the lexemes
// ★ DECLARED ONCE, IN THIS PURE UNIT, so an owner ruling changes each in exactly one place and a native
//   case can pin the exact bytes (§B115: a string built in `src/firmware_ui.cpp` is a string no
//   automated gate can read). ⚠ WIDTH IS A CONSTRAINT: the rail leaves a 19-column body.
inline constexpr const char* kNearbyTitle = "NEARBY";              // spec S-2 — the design's own word (§3.6.4 :797)
// ★★ TWO LINES, AND THE SECOND ONE IS **F-1's HONEST COMPLETION OF THE FIRST** (spec §1.7 F-1, §8 S-4).
//    §3.6.4 says the joiner listens on its *"current effective PHY"* — which is TRUE and INCOMPLETE:
//    `lib/core/node_beacon.cpp`'s teamless arm drops a beacon whose LEAF NIBBLE differs BEFORE parse
//    (`flags_of(bytes[0]) != _cfg.leaf_id && _cfg.team_id == 0`), so a joiner on the right
//    frequency/BW/SF but a different `leaf_id` hears nothing at all. ⛔ This spec does not relax that
//    gate (relaxing a pre-parse drop is a routing-plane change with corpus consequences and is not
//    onboarding) ⇒ the panel says so instead of letting the operator conclude the other node is off.
inline constexpr const char* kNearbyPhyLine  = "CURRENT PHY ONLY";  // spec S-3 — the design's own line (:829-830)
inline constexpr const char* kNearbyLeafLine = "SAME RADIO + LEAF";  // spec S-4 — F-1's second line
inline constexpr const char* kNearbyEmpty    = "NO TEAMS NEARBY";    // spec S-5
// The screen's one NOTE row: the empty state, or nothing at all. ⓘ It is a FUNCTION rather than a
// renderer-side `if` for the §B115 reason — the condition is a decision, and decisions do not live in
// the one TU no gate compiles. ⛔ "Nothing heard" is not an error and says nothing about the radio: it
// is the ordinary answer on a quiet band, so there is no remedy line beside it.
inline const char* nearby_note(const NearbyList& l) { return l.n == 0 ? kNearbyEmpty : ""; }

// ⓘ THE ROW'S THREE TOKENS (the shared fingerprint · the `n/3` tier · the reused age) ARE ONE FILE OVER,
//   in `firmware_ui_nearby_row.h` — see this header's opening note for the include-order fact that puts
//   them there, and ⛔ do not re-spell any of them here to "keep the unit together".

}  // namespace mrui
