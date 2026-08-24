// MeshRoute — src/firmware_ui_invite.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-16 N4 (spec docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md §4-N4) — THE
// `INVITE MEMBER` WINDOW'S PURE UNIT: the bounded window's duration, the **TWO** snapshot authorities and the
// diff over them, the VOLATILE per-window handled set, the candidate row (name · team-local id · member
// fingerprint) and every lexeme the screen can print.
//
// ★★★ WHY A FILE OF ITS OWN rather than more of `firmware_ui_model.h`: a battery is per-SOURCE-FILE
//     (`tools/probe_ui_model_mutations.py`'s own header — the backup, the marker and the lock are all keyed by
//     the resolved path). The decisions below are OWNER RULINGS that must each be attacked ON THEIR OWN — the
//     two authorities (F-11, and each half separately), the AUTHORITATIVE floor that keeps a route-only member
//     off the grantable list (F-7/C2), the handled set that must be volatile AND must exist at all (F-13), and
//     the name lifecycle that ADDS a column instead of swapping one (F-15 rules 2-3). Left inside the
//     3700-line model they would have shared `model`'s entries; composed in `src/firmware_ui.cpp` they would
//     have had NO battery at all (that TU is compiled by neither the native suite nor the simulator — §B115).
//     ⓘ Same argument `firmware_ui_nearby.h` and `firmware_ui_join.h` made, for the same class of decision.
//
// ⚠⚠ AND IT IS **ONE** FILE, WHICH IS WORTH STATING BECAUSE N2's PURE UNIT HAD TO BECOME TWO. That split was an
//    INCLUDE-ORDER FACT — `src/firmware_ui_chrome.h:36` includes `firmware_ui_model.h`, so a header the MODEL
//    includes may not include chrome, and N2's row needed chrome's `ui_fmt_team_fingerprint` / `ui_fmt_home_age`
//    / `ui_pad_token`. THIS unit needs NONE of the three: the member fingerprint is a SEPARATE definition by
//    ruling (F-8 / S-13 — ⛔ never merged with the team-id helper), there is no age on the row, and ⛔ no
//    `ui_pad_token` is called OR forked, because the row is FIXED-WIDTH BY CONSTRUCTION — every field carries an
//    explicit width, so one `snprintf` always writes exactly `kInviteRowCap - 1` characters plus its NUL, i.e.
//    the whole buffer. (A native case asserts that length over every arm, including a blank name, id 0 and id
//    254, rather than leaving it as an argument.)
//
// ⓘ DONE-VS-MISSING, STATED IN CODE BECAUSE DOCS ROT ([[meshroute-mark-done-vs-missing-in-code]]):
//   N4 can SHOW CANDIDATES and REJECT them, and that is ALL. ⛔ There is no grant, ⛔ no pubkey request and ⛔ no
//   `GRANT KEY` word anywhere in this slice — `REJECT` is the only act S-17 reaches here (spec §4-N4's scope
//   line: *"⛔ No grant, no pubkey request — this slice can only show candidates"*). The pubkey request is
//   §UI-16 N5 (`NEED PUBKEY` / `REQUEST PUBKEY` / `WAITING FOR PUBKEY`) and the grant plus the `REJECT`-selected
//   two-action confirmation is N6 (spec §4-N6 pin 1, which is N6's to land). ⇒ this slice's confirmation offers
//   the SAFE arm `BACK` (selected initially, P-13) and the ONE act it can honestly perform, `REJECT`; ⛔ it does
//   NOT offer a `GRANT KEY` row whose flow does not exist yet, which is [[B222]]'s rule honoured rather than
//   waived: a transition lands WITH the flow behind it, never one slice ahead.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>                  // snprintf — §B115: the row is composed HERE so the suite reads the BYTES

namespace mrui {

// ★ THE LIST'S CAPACITY. It is the SNAPSHOT's team-row capacity (`mrui::kMaxTeamRows`), and the two are bound by
//   a `static_assert` in `firmware_ui_model.h` rather than by this comment — this header may not include the
//   model (the model includes IT: `UiSnapshot` publishes the member array and `UiState` freezes the window).
inline constexpr uint8_t kMaxInviteRows = 8;

// ★ THE CACHED NAME's STORAGE, and it is deliberately WIDER than the six columns the row draws: the clamp is the
//   FORMAT's (`%-6.6s`), ⛔ never the buffer's. A buffer clamped to six would make the 6-column rule unattackable
//   — the mutation that drops the clamp would render the same six characters — and it would also be a SECOND
//   truncation of one name (§UI-17 S-11 clamps the TEAM row at six from a 14-column label; one name, one
//   truncation). ⓘ It equals `mrui::kLabelCap + 1`, asserted in the model beside the other bound.
inline constexpr uint8_t kInviteNameCap = 15;

// AUTHORITY (b)'s STORAGE: one bit per team-local id, 0..255. ⓘ 32 bytes, derived from the id's own width — ⛔
// never a hand-written 32: a team-local id is a `uint8_t`, so the bitset is exactly `256 / 8`.
inline constexpr uint8_t kInviteIdBytes = 32;

// ★★★★ FIVE MINUTES — OWNER-RULED (spec §9 R-3: *"5 minutes, and it does NOT hold the panel lit"*) — AND ITS
//      DERIVATION IS THE ONE `protocol::team_seen_retain_ms` ALREADY WROTE DOWN ONE SLICE AGO: a team member's
//      STEADY-state beacon period is `NodeConfig::team_beacon_period_ms`, which defaults to **300000**
//      (`lib/core/node_carriers.h`), so this window is EXACTLY **ONE** such period — the shortest window in
//      which every member of the team is expected to be heard once. N1's retention is TWO of them (600000), the
//      smallest window that survives one MISSED beacon; a candidate the operator is standing in front of does
//      not need that second period, and a longer approval window is a longer time in which one press grants a
//      key. ⓘ THE RELATION IS PINNED BY A NATIVE CASE, not by this comment, so a re-tuned beacon period is
//      NOTICED here instead of silently drifting away from the number the owner ruled.
// ⛔ AND IT IS A CONSTANT, ⛔ never `_cfg.team_beacon_period_ms` — the R-2(i) argument, applied a second time:
//    a window read off live config would change duration when an unrelated cadence is re-tuned, and the ruling
//    is a DURATION the operator was told about, not a cadence.
inline constexpr uint32_t kInviteWindowMs = 300000;

// ================================================================================ the carriers
// ★★★ ONE TEAM MEMBER, AS THE INVITE SCREEN NEEDS IT — the projection `build_snapshot` publishes beside the
//     TEAM row, from the SAME single `team_key_of_id` resolution (spec §6: *"one `team_key_of_id` resolution per
//     row and hands it to both consumers (U1), ⛔ never two lookups for one row"*).
// ⛔ IT IS NOT `TeamRow`, and the two are deliberately different projections of one member: `TeamRow::label` is
//    a DISPLAY string with fallbacks (`peer_name_find` -> `0x<hash>` -> `id <n>`), and two of those three
//    fallbacks are FORBIDDEN here — a truncated `0x` form in a six-column field is a THIRD spelling of the hash,
//    beside the full id and the fingerprint (spec §4-N4, F-15). ⇒ `name` below is `Node::peer_name_find`'s
//    ANSWER AND NOTHING ELSE, and `""` — the blank column — is the honest state until one is cached.
// ⓘ `key_hash32 == 0` MEANS **NO AUTHORITATIVE BINDING** (F-7): `team_key_of_id`'s floor is `authoritative`, a
//   `claimed` on-air binding cannot answer it, and `_team_peer` bits are set from a keyless multi-hop DV entry
//   (`lib/core/node.cpp:645`). Such a member is a REAL member — it is recorded in authority (b) below — but it
//   has no fingerprint and no seal target, so it is ⛔ never listed as grantable (C2, fail closed).
struct InviteMember {
    uint32_t key_hash32 = 0;
    uint8_t  id = 0;                       // the TEAM-plane local id (C3: it indexes nothing here)
    char     name[kInviteNameCap] = {};    // `peer_name_find`'s answer VERBATIM; "" = no cached name
};

// ★★★★ THE WINDOW'S WHOLE STATE — the TWO snapshot authorities (F-11), the volatile handled set (F-13) and the
//      FROZEN selection — in ONE carrier, because they have ONE lifetime: they are taken when the window OPENS
//      and DISCARDED when it closes. ⛔ Four fields on `UiState` would be four things to remember to clear, and
//      "the handled set survives the window" is precisely what the ruling forbids.
// ★★ AUTHORITY (a) — THE AUTHORITATIVE HASHES AT OPENING. It survives a team-local-id change, which is what
//    makes a re-DAD'd member (same hash, new id) ⛔ not a candidate.
// ★★ AUTHORITY (b) — THE BITSET OF TEAM-LOCAL IDS PRESENT AT OPENING. It suppresses an ALREADY-PRESENT member
//    whose hash only turns authoritative later in the window — the route-only member F-11 was written about,
//    which a hash-keyed snapshot alone announces as `NEW MEMBER` although it was there all along.
// ⇒ **NEW ⟺ NEITHER the hash NOR the current id was in the opening snapshot.**
// ⚠⚠ THE ONE CASE THIS CANNOT SEPARATE IS **DOCUMENTED, ⛔ NOT ENGINEERED AWAY, AND IT IS NOT A BUG** (F-11,
//    owner-ruled): an UNKEYED member that changes its id **and** acquires a hash inside one window presents a
//    hash nobody has seen at an id nobody has seen, and therefore PROMPTS. That is a **SAFE FALSE PROMPT**: the
//    operator must still confirm, the fingerprint is on the row, and the alternative — suppressing it — would
//    mean suppressing a genuinely new member with the same evidence. ⛔ A later slice that "fixes" this is
//    removing a prompt the ruling requires; the native suite DRIVES the case so the behaviour is measured
//    rather than asserted in prose.
struct InviteWindow {
    uint32_t hash[kMaxInviteRows] = {};        // (a) the authoritative hashes at opening
    uint32_t handled[kMaxInviteRows] = {};     // the VOLATILE per-window handled set (F-13)
    uint8_t  id_bits[kInviteIdBytes] = {};     // (b) the team-local ids present at opening
    uint32_t sel_hash = 0;                     // the FROZEN selection: the confirmation's identity (P-7d)
    uint8_t  n = 0;                            // how many of `hash[]` are real
    uint8_t  handled_n = 0;                    // how many of `handled[]` are real
    uint8_t  sel_id = 0;                       // ...and its team-local id, frozen with it
    // ⛔ NO ARITHMETIC VALUE IS RESERVED FOR "NO SNAPSHOT" (§B74's discipline): `n == 0` is a REAL state — a
    //    team whose only member is us — and it may not double as "the window is not open". A window that has
    //    taken no snapshot produces NO candidates at all (see `invite_is_new`), which is C2 rather than caution:
    //    the alternative treats every member as new the moment the state is lost.
    bool     taken = false;
};

// ------------------------------------------------------------------- the two authorities, asked separately
// ⓘ TWO FUNCTIONS AND ⛔ NOT ONE `is_present`: the mutations must be able to drop EITHER authority ALONE (spec
//   §4-N4's first two entries — one mislabels the route-only member, the other mislabels the re-DAD'd one), and
//   an entry that cannot be applied to exactly one site is not an attributable control.
inline bool invite_snap_has_hash(const InviteWindow& w, uint32_t key_hash32) {
    for (uint8_t i = 0; i < w.n && i < kMaxInviteRows; ++i)
        if (w.hash[i] == key_hash32) return true;
    return false;
}
inline bool invite_snap_has_id(const InviteWindow& w, uint8_t id) {
    return (w.id_bits[uint8_t(id >> 3)] & uint8_t(1u << (id & 7u))) != 0u;
}

// ★★★ THE SNAPSHOT, TAKEN AT **WINDOW OPEN** — ⛔ never at the first render (spec §4-N4's mutation: a snapshot
//     taken at the first render is taken AFTER a member could already have arrived, so it hides exactly the
//     candidate the window exists to surface, and it re-takes itself on every repaint).
// ★ EVERY member's ID goes into authority (b), INCLUDING a route-only one; only an AUTHORITATIVE hash goes into
//   authority (a). That asymmetry IS F-11: presence is evidence even when identity is not.
// ★★★★ AND THE NULL SOURCE FAILS **CLOSED**, WHICH IS TWO DIFFERENT ANSWERS FOR TWO DIFFERENT FACTS (C2):
//      ⓘ `(nullptr, 0)` is a COHERENT claim — "there are no members" — and it is honoured: `taken = true` with an
//        empty snapshot, because "I looked and the team is just me" is a real state (see `n == 0` above).
//      ⛔⛔ `(nullptr, n > 0)` is an INCOHERENT one — a publisher claiming `n` members while handing over NO source —
//        and it is REFUSED: `taken = false`. ⚠ Recording it as a TAKEN-but-EMPTY snapshot would be FAIL-**OPEN**
//        wearing a fail-closed comment: an empty authority set makes `invite_is_new` answer *new* for EVERY member
//        the window then observes, so the whole team would be announced as candidates on a bug in the caller. The
//        refusal costs a window that lists nothing, which is the direction C2 requires the error to fall.
inline InviteWindow invite_snapshot_take(const InviteMember* mem, uint8_t n) {
    InviteWindow w{};
    if (!mem && n != 0) return w;          // ⛔ REFUSED: members claimed, no source — `taken` stays false
    w.taken = true;                        // ⛔ true even for an EMPTY team: "I looked" is the fact recorded
    if (!mem) return w;                    // `(nullptr, 0)`: an empty team, honestly looked at
    if (n > kMaxInviteRows) n = kMaxInviteRows;   // the publisher's bound, restated where the copy happens
    for (uint8_t i = 0; i < n; ++i) {
        const InviteMember& m = mem[i];
        w.id_bits[uint8_t(m.id >> 3)] |= uint8_t(1u << (m.id & 7u));      // (b) — presence, always
        if (m.key_hash32 != 0 && w.n < kMaxInviteRows) w.hash[w.n++] = m.key_hash32;   // (a) — identity, when known
    }
    return w;
}

// ★★★★ THE DIFF (F-11 / spec §3 P-6b), AND EVERY TERM IS LOAD-BEARING:
//   1. no snapshot ⇒ NOTHING is new (C2 — the window is not open, so there is nothing to announce);
//   2. no AUTHORITATIVE hash ⇒ ⛔ not listed at all (F-7: no fingerprint and no seal target, so ⛔ never a
//      grantable candidate, and ⛔ never one with a blank or invented fingerprint);
//   3. the hash was in authority (a) ⇒ present at opening, whatever its id says now (the re-DAD case);
//   4. the id was in authority (b) ⇒ present at opening, whatever its hash says now (the route-only case).
// ⛔ THE DIFF IS ⛔ NOT KEYED ON `last_seen_ms` (spec §1.3): `team_key_set` refreshes that on EVERY authoritative
//    beacon, so it means *last heard* and would announce every member of the team on their next beacon.
inline bool invite_is_new(const InviteWindow& w, const InviteMember& m) {
    if (!w.taken) return false;
    if (m.key_hash32 == 0) return false;
    if (invite_snap_has_hash(w, m.key_hash32)) return false;
    if (invite_snap_has_id(w, m.id)) return false;
    return true;
}

// ------------------------------------------------------------------------------- the handled set (F-13)
// ★★★ IT EXISTS BECAUSE THE DRAFT WAS SELF-CONTRADICTORY, and the ruling says so: `REJECT` cannot both "change
//     nothing" and "remove the candidate", because the LOCAL REFRESH re-adds it a tick later. ⇒ a VOLATILE
//     per-window set of candidate HASHES. It changes ⛔ no core, radio, membership, key or NV state — it is RAM
//     that lives and dies with the window (`UiModel::enter_provision` discards it on the way out).
// ⛔ IT IS KEYED BY THE `key_hash32`, ⛔ never by the display name (P-7d): the name is MUTABLE
//    (`lib/core/node_hashlocate.cpp:346`), so a name-keyed set would silence a DIFFERENT member the moment two
//    members shared a name.
inline bool invite_handled_has(const InviteWindow& w, uint32_t key_hash32) {
    for (uint8_t i = 0; i < w.handled_n && i < kMaxInviteRows; ++i)
        if (w.handled[i] == key_hash32) return true;
    return false;
}
// ⓘ RETURNS WHETHER THE SET NOW HOLDS IT. A FULL set REFUSES (C2) rather than evicting: an evicted entry is a
//   rejected candidate silently coming back, which is the very property F-13 exists to give. ⓘ It cannot fill in
//   practice — the set and the list share one capacity — and the bound is here because a bound belongs at the
//   write, not in an argument about reachability. ⛔ `key_hash32 == 0` is refused too: 0 is "no authoritative
//   binding" and such a member is never a candidate, so it can never be the thing being rejected.
inline bool invite_handled_add(InviteWindow& w, uint32_t key_hash32) {
    if (key_hash32 == 0) return false;
    if (invite_handled_has(w, key_hash32)) return true;
    if (w.handled_n >= kMaxInviteRows) return false;
    w.handled[w.handled_n++] = key_hash32;
    return true;
}

// ------------------------------------------------------------------------------- the rows, AS IDENTITIES
// ★★ §B66's rule, a fourth menu deep: the visible list is the DIFFED one, so a row's meaning may not be derived
//    from its position — a member being suppressed (present at opening, route-only, or already handled) puts
//    every later candidate one row up, and a REJECT between two frames does exactly that.
// ⛔ AND THE ROW CARRIES THE WHOLE `InviteMember` (U2), ⛔ not an index into anything: the identity a later
//    slice grants to is the `key_hash32` the panel drew (P-7d).
struct InviteSelRow {
    InviteMember cand{};     // MEANINGFUL ONLY while `!back`
    bool         back = false;
};
struct InviteSelList {
    InviteSelRow row[kMaxInviteRows + 1] = {};   // every candidate at most, plus the UNCONDITIONAL BACK
    uint8_t      n = 0;
    // ⛔ FAILS CLOSED (C2), exactly as `ProvRowList::at` / `NearbySelList::at` do: an out-of-range index names NO
    //    row and the caller must do nothing rather than being handed a plausible one. Here the row one press
    //    from BACK is a candidate whose confirmation is one `short` from REJECT.
    bool at(uint8_t i, InviteSelRow& out) const { if (i >= n) return false; out = row[i]; return true; }
};
// ★★★★ THE LOCAL REFRESH (F-14 / R-10), AS ONE PURE FUNCTION: it is called with the LIVE member projection every
//      time the list is walked or drawn, so a member that appears mid-window appears on the panel — ⛔ without a
//      scan, a query or a single byte transmitted (spec §3 P-4b; the publisher's two reads, `rt_team_at` and
//      `team_key_of_id`, are both `const`). The probe asserts the TX-queue depth and the radio's start count
//      across a held-open window rather than arguing it.
// ★ THE HANDLED SET IS APPLIED **HERE** rather than inside `invite_is_new`, and the separation is what makes the
//   two rulings independently attackable: F-11 decides *is this member new*, F-13 decides *has the operator
//   already dealt with it*. One function answering both would take one mutation to break both.
// ⓘ BACK IS UNCONDITIONAL — the rule `provision_rows` / `nearby_sel_rows` state: leaving must never depend on a
//   store, a build flag or, here, on whether anybody new arrived. An EMPTY window still opens a list the
//   operator can leave.
inline InviteSelList invite_sel_rows(const InviteWindow& w, const InviteMember* mem, uint8_t n) {
    InviteSelList out{};
    if (mem) {
        if (n > kMaxInviteRows) n = kMaxInviteRows;
        for (uint8_t i = 0; i < n; ++i) {
            if (!invite_is_new(w, mem[i])) continue;
            if (invite_handled_has(w, mem[i].key_hash32)) continue;
            out.row[out.n].cand = mem[i];      // ⛔ the WHOLE member (U2), never rebuilt field by field
            out.row[out.n].back = false;
            ++out.n;
        }
    }
    out.row[out.n].back = true;
    ++out.n;
    return out;
}

// ============================================================================================ the tokens
// ★★★★ THE **MEMBER** FINGERPRINT (spec §8 S-13), AND IT IS A **SECOND, SEPARATELY-NAMED** DEFINITION BY RULING
//      (F-8). §3.6.4 point 5 uses two different fingerprints in one sentence: the TEAM-id one is
//      `mrui::ui_fmt_team_fingerprint` (the low 24 bits of a `team_id`, `firmware_ui_chrome.h`), and THIS one is
//      over a `key_hash32` — a different value, in a different space, about a different entity. ⛔ They may not
//      share one helper: the shared helper's own U1 argument is that ONE token has ONE definition, and folding
//      two quantities into it would make a team and a member print through the same function by coincidence.
// ⓘ `%06lX` + `(unsigned long)` is the file-cluster's hex idiom, uppercase and zero-padded for the same two
//   reasons the team token gives: a space-padded token stops being a placeable fixed-width field, and a list
//   where one entry reads `a1b2c3` and another `A1B2C3` reads as two different people.
// ⛔ IT IS THE **LOW 24 BITS** OF THE HASH, so it is a HUMAN SELECTION AID and ⛔ NEVER AN AUTHORITY (P-7): the
//    confirmation carries the FULL `0x%08lX` (below), and every act keys on the full 32 bits ([[B48]]'s class).
inline constexpr uint32_t    kMemberFpMask  = 0x00FFFFFFu;
inline constexpr std::size_t kMemberFpCap   = 7;    // six uppercase hex characters + NUL
inline void ui_fmt_member_fingerprint(char* out, std::size_t cap, uint32_t key_hash32) {
    if (!out || cap == 0) return;
    snprintf(out, cap, "%06lX", (unsigned long)(key_hash32 & kMemberFpMask));
}

// ★★★★ THE FULL HASH, i.e. rule 4's *"the identity that survives into the irreversible act"* (spec §3 P-7c).
//      Every confirmation opened from this list draws it, ⛔ **even when a name is shown** — a mutable,
//      self-asserted label may never be the only thing an operator reads before a private key is shipped.
// ⓘ `0x%08lX` is the panel's own full-id spelling (`ui_fmt_team_id_full`'s, one screen over) and the console's,
//   deliberately: an operator reading the panel and the serial log must see the SAME string for the same peer.
inline constexpr std::size_t kMemberHashCap = 11;   // "0x" + eight uppercase hex + NUL
inline void ui_fmt_member_hash_full(char* out, std::size_t cap, uint32_t key_hash32) {
    if (!out || cap == 0) return;
    snprintf(out, cap, "0x%08lX", (unsigned long)key_hash32);
}

// ★★★★ THE CANDIDATE ROW (spec §8 S-35, F-15 rules 2-3): `%c%-6.6s T%-3u %6s` -> `>Wolfga T221 6C2971`.
//      WIDTH PROOF, DERIVED AND NOT GUESSED: `1 + 6 + 1 + 4 + 1 + 6 = 19` — exactly the rail's body budget.
// ★★★ THE NAME IS AN **ADDED COLUMN**, ⛔ NEVER A SWAPPED TOKEN. Rule 2 (*initially the member fingerprint*) and
//     rule 3 (*then prefer the cached name*) are ⛔ not a substitution: the identity aid the operator has learned
//     to read STAYS PUT and a BLANK COLUMN FILLS IN. That is strictly safer than swapping — and it is what lets
//     rule 4 hold at row level too, because the row never stops carrying a hash-derived token.
// ★★★ AND THE NAME'S SOURCE IS `Node::peer_name_find` AND NOTHING ELSE (U1) — the SAME single name source the
//     TEAM chain's second step already uses. ⛔ It is NOT `label_from_hash` and ⛔ not `label_for_team_id`
//     (`src/firmware_ui.cpp`): both fall back to `0x%08lx` (ten columns) and to a bare id, which in a six-column
//     field would render a TRUNCATED `0x` FORM — a THIRD spelling of the hash beside the full id and the
//     fingerprint. The publisher hands this carrier `""` when no name is cached, and `""` is what `%-6.6s`
//     renders as six spaces.
// ⓘ THE 6-COLUMN CLAMP MATCHES THE TEAM ROW's (§UI-17 S-11) DELIBERATELY: a member that appears on BOTH screens
//   must not render two different truncations of one name.
// ⓘ THE MARKER IS A PARAMETER, ⛔ not composed by the caller afterwards: the width proof above only closes if the
//   marker is inside the 19 columns, and a caller that prefixed its own would silently make the row 20.
inline constexpr std::size_t kInviteRowCap = 20;    // 19 columns + NUL
inline void ui_fmt_invite_row(char* out, std::size_t cap, char marker, const InviteMember& m) {
    if (!out || cap == 0) return;
    char fp[kMemberFpCap]; ui_fmt_member_fingerprint(fp, sizeof fp, m.key_hash32);
    snprintf(out, cap, "%c%-6.6s T%-3u %6s", marker, m.name, unsigned(m.id), fp);
}

// =========================================================================================== the lexemes
// ★ DECLARED ONCE, IN THIS PURE UNIT, so an owner ruling changes each in exactly one place and a native case can
//   pin the exact bytes (§B115). ⚠ WIDTH IS A CONSTRAINT: the rail leaves a 19-column body.
inline constexpr const char* kInviteTitle  = "INVITE MEMBER";   // S-12 — the design's own words (§3.6.4 :800)
// ★★ S-14, AND ITS TWIN IS **FORBIDDEN**: the design bans `KEYLESS` for a member in as many words, and the
//    absence of that word on this path is a TEST rather than a preference (spec §3 P-6, S-33). `WAITING FOR KEY`
//    (S-34) is forbidden too — it is ambiguous between the recipient's PUBKEY and the team CONTENT key, the two
//    different secrets this flow sits between.
inline constexpr const char* kInviteNew    = "NEW MEMBER";      // S-14 — the design's own word (§3.6.4 :815)
inline constexpr const char* kInviteEmpty  = "NO CANDIDATES";   // S-15
// ★ S-16 — what the panel says when the bounded window ran out. ⛔ IT IS A STATEMENT ABOUT THE **UI** AND
//   NOTHING ELSE (spec §3 P-11): expiry grants nothing, revokes nothing and rewrites nothing — the member set,
//   the membership and the content key are byte-identical across open -> expire -> re-open.
inline constexpr const char* kInviteClosed = "WINDOW CLOSED";   // S-16

// The screen's one NOTE row: the candidate word, or the empty state. ⓘ It is a FUNCTION rather than a
// renderer-side `if` for the §B115 reason — the condition is a decision, and decisions do not live in the one TU
// no gate compiles. ⛔ "Nobody new" is not an error: it is the ORDINARY answer, and the honest one on first open
// (spec §7.3 step 2 FAILs if an already-known member appears).
inline const char* invite_note(const InviteSelList& l) {
    // ⓘ `n == 1` is the EMPTY list: BACK is unconditional, so a list of one row is BACK alone.
    return (l.n <= 1) ? kInviteEmpty : kInviteNew;
}

// ★★ THE CONFIRMATION'S TWO WORDS. `BACK` is the SAFE arm and is selected initially (P-13, established by
//    `enter_provision`'s zero value), and `REJECT` is S-17's — the ONE act N4's scope reaches. ⛔ `GRANT KEY` is
//    N6's and is deliberately absent here; see this file's DONE-VS-MISSING note.
// ⓘ IT IS `join_confirm_label`'s SHAPE VERBATIM (U3) — one function, a `bool`, the safe word on `false` — so the
//   three confirmations in this cluster read the same way and none of them can identify an arm POSITIONALLY
//   (§B66). `BACK` is the shipped spelling every one of them uses.
inline const char* invite_confirm_label(bool confirm) { return confirm ? "REJECT" : "BACK"; }

}  // namespace mrui
