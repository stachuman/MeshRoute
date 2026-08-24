// MeshRoute — test_firmware_ui_invite.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK, never
//     REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §UI-16 N4 — the native suite for the `INVITE MEMBER` window's pure unit (`src/firmware_ui_invite.h`): the TWO
// snapshot authorities and the diff over them (F-11), the AUTHORITATIVE floor that keeps a route-only member off
// the grantable list (F-7), the VOLATILE per-window handled set (F-13), the candidate row's name lifecycle
// (F-15 rules 2-3, 5), the member fingerprint and the full-hash identity (S-13 / P-7c), and every lexeme.
//
// ★★★ THE EXPECTATIONS ARE INDEPENDENT OF THE IMPLEMENTATION, which is what lets them redden it:
//   · the DIFF cases drive the four transitions BY CONSTRUCTION — present-at-open, arrived-after, re-DAD'd (same
//     hash, new id) and route-only-turned-authoritative — so dropping EITHER authority moves exactly one of them
//     and cannot be hidden by the other;
//   · the ROW cases assert the EXACT NINETEEN BYTES, ⛔ never "a name appeared": a row that swapped the
//     fingerprint for the name, or lost the clamp, is a different byte string and says so;
//   · the WINDOW's five minutes is pinned by its DERIVATION (the default team-beacon period) as well as by its
//     value, so a re-tuned cadence is noticed here instead of drifting away from the number the owner ruled.
//
// ⛔ WHAT IS NOT MEASURED HERE, and said out loud rather than silently skipped: the MODEL's half (which gesture
//   opens the window, when the snapshot is taken, what the deadline does, what `REJECT` reaches) is
//   `test/test_firmware_ui_model.cpp`'s §UI-16 N4 block and the `model` battery; the RENDERER
//   (`src/firmware_ui.cpp`'s three new arms) is compiled by neither the native suite nor the simulator (§B115)
//   and its cover is `tools/probe_firmware_ui`'s INVITE phase, which drives REAL members through the REAL node.
#include "doctest.h"
#include "firmware_ui_invite.h"
#include "node.h"                 // NodeConfig — the DERIVATION pin for the window's five minutes
#include "protocol_constants.h"   // team_seen_retain_ms — N1's two periods, the other half of that derivation
#include <cstdint>
#include <cstring>

using mrui::InviteMember;
using mrui::InviteSelList;
using mrui::InviteSelRow;
using mrui::InviteWindow;
using mrui::invite_handled_add;
using mrui::invite_handled_has;
using mrui::invite_is_new;
using mrui::invite_note;
using mrui::invite_sel_rows;
using mrui::invite_snap_has_hash;
using mrui::invite_snap_has_id;
using mrui::invite_snapshot_take;
using mrui::kMaxInviteRows;
using mrui::ui_fmt_invite_row;
using mrui::ui_fmt_member_fingerprint;
using mrui::ui_fmt_member_hash_full;

namespace {

// One member, as `build_snapshot` publishes it. ⓘ `hash == 0` is the ROUTE-ONLY member: a real member of the
// team (its id is in the routing table) for which `team_key_of_id` has no AUTHORITATIVE binding.
InviteMember mem(uint8_t id, uint32_t hash, const char* name = "") {
    InviteMember m{};
    m.id = id;
    m.key_hash32 = hash;
    for (uint8_t i = 0; name[i] && i + 1 < mrui::kInviteNameCap; ++i) m.name[i] = name[i];
    return m;
}
// The candidate rows for one live member list, in one call.
InviteSelList rows(const InviteWindow& w, const InviteMember* live, uint8_t n) {
    return invite_sel_rows(w, live, n);
}
// How many CANDIDATES (i.e. rows that are not the unconditional BACK).
uint8_t cands(const InviteSelList& l) { return uint8_t(l.n - 1); }

}  // namespace

// ============================================================ pin 1's constant — FIVE MINUTES, AND ITS DERIVATION
TEST_CASE("ui16-window: the window is FIVE MINUTES, named once, and its derivation is pinned to the period") {
    CHECK(mrui::kInviteWindowMs == 300000u);
    // ★★★ THE DERIVATION (spec §9 R-3, and the discipline `team_seen_retain_ms` set one slice ago): five minutes
    //     is EXACTLY ONE default team-beacon period — the shortest window in which every member of the team is
    //     expected to be heard once. ⛔ This is not decoration: without the pin a re-tuned cadence would leave
    //     the ruled number standing on a rationale that had quietly stopped being true.
    MESHROUTE_NS::NodeConfig cfg{};
    CHECK(cfg.team_beacon_period_ms == mrui::kInviteWindowMs);
    // ...and N1's retention is TWO of them (it must survive a MISSED beacon; an operator standing in front of
    // the panel need not).
    CHECK(MESHROUTE_NS::protocol::team_seen_retain_ms == 2u * mrui::kInviteWindowMs);
}

// ================================================================== pins 2-6 — THE TWO AUTHORITIES AND THE DIFF
TEST_CASE("ui16-snap: the snapshot records EVERY id and only the AUTHORITATIVE hashes") {
    const InviteMember live[] = { mem(81, 0x00C0FFEEu), mem(82, 0u), mem(200, 0xAABBCCDDu, "Wolfgangetta") };
    const InviteWindow w = invite_snapshot_take(live, 3);
    CHECK(w.taken == true);
    // ★ AUTHORITY (a) — identity, when it is known. The route-only member contributes NOTHING here.
    CHECK(w.n == 2);
    CHECK(invite_snap_has_hash(w, 0x00C0FFEEu));
    CHECK(invite_snap_has_hash(w, 0xAABBCCDDu));
    CHECK(invite_snap_has_hash(w, 0u) == false);          // ⛔ 0 is "no binding", never a recorded identity
    CHECK(invite_snap_has_hash(w, 0x00C0FFEFu) == false);
    // ★ AUTHORITY (b) — presence, ALWAYS, including the route-only member (that asymmetry IS F-11).
    CHECK(invite_snap_has_id(w, 81));
    CHECK(invite_snap_has_id(w, 82));
    CHECK(invite_snap_has_id(w, 200));
    CHECK(invite_snap_has_id(w, 83) == false);
    CHECK(invite_snap_has_id(w, 0) == false);
    CHECK(invite_snap_has_id(w, 255) == false);
    // ...and the bitset really is 256-wide: the two ENDS of the id space are recorded like any other.
    const InviteMember edges[] = { mem(0, 0x11111111u), mem(255, 0x22222222u) };
    const InviteWindow e = invite_snapshot_take(edges, 2);
    CHECK(invite_snap_has_id(e, 0));
    CHECK(invite_snap_has_id(e, 255));
    CHECK(invite_snap_has_id(e, 1) == false);
    CHECK(invite_snap_has_id(e, 254) == false);
    // ⛔⛔ THE NULL SOURCE FAILS **CLOSED**, AND THE TWO ARMS ANSWER DIFFERENTLY BECAUSE THEY ARE TWO DIFFERENT
    //     CLAIMS (C2). ⓘ `(nullptr, 0)` is COHERENT — "the team is just me" — so it is a VALID, taken, empty
    //     snapshot: `taken` is the fact that we LOOKED, and an empty team is a real state (`n == 0` is not a
    //     sentinel).
    const InviteWindow none = invite_snapshot_take(nullptr, 0);
    CHECK(none.taken == true);
    CHECK(none.n == 0);
    // ⛔⛔ `(nullptr, n > 0)` is INCOHERENT — `n` members claimed with NO source — so it is REFUSED OUTRIGHT.
    //     ⚠ Recording it as taken-but-empty would be FAIL-**OPEN**: the authority sets would be empty, and
    //     `invite_is_new` would then answer *new* for EVERY member the window observes — the whole team announced
    //     as candidates because the caller had a bug. The refusal lists nothing instead, which is the direction
    //     the error must fall. THIS IS THE ASSERTION THAT SEPARATES THE TWO, and it is driven, not argued.
    const InviteWindow nul = invite_snapshot_take(nullptr, 8);
    CHECK(nul.taken == false);
    CHECK(nul.n == 0);
    CHECK(invite_snap_has_id(nul, 81) == false);
    CHECK(invite_is_new(nul, mem(81, 0x00C0FFEEu)) == false);   // ⛔ an unsnapshotted window announces NOBODY
    // ...and the publisher's bound is restated at the copy: a count past the ARRAY's own capacity is clamped, so
    // a publisher that overran could not make this read past the fixed array. ⚠ Driven with a REAL array of the
    // full capacity — a shorter one plus a big count would be undefined behaviour in the CASE, not a measurement.
    InviteMember many[kMaxInviteRows];
    for (uint8_t i = 0; i < kMaxInviteRows; ++i) many[i] = mem(uint8_t(10 + i), 0x30000000u + i);
    const InviteWindow over = invite_snapshot_take(many, 200);
    CHECK(over.n == kMaxInviteRows);
    CHECK(invite_snap_has_id(over, 10));
    CHECK(invite_snap_has_id(over, uint8_t(10 + kMaxInviteRows - 1)));
    CHECK(invite_snap_has_id(over, uint8_t(10 + kMaxInviteRows)) == false);
}

TEST_CASE("ui16-diff: pin 2 — a member PRESENT AT SNAPSHOT is ⛔ never a candidate, however it is re-described") {
    const InviteMember live[] = { mem(81, 0x00C0FFEEu), mem(200, 0xAABBCCDDu) };
    const InviteWindow w = invite_snapshot_take(live, 2);
    CHECK(invite_is_new(w, live[0]) == false);
    CHECK(invite_is_new(w, live[1]) == false);
    CHECK(cands(rows(w, live, 2)) == 0);
    // ...on the tenth refresh as on the first: the snapshot is the OPENING's and nothing here consumes it.
    for (int i = 0; i < 10; ++i) CHECK(cands(rows(w, live, 2)) == 0);
    // ⛔ AND A MEMBER PRESENT AT SNAPSHOT IS NOT A CANDIDATE EVEN AFTER ITS NAME ARRIVES: a name is a display
    //    fact and may never make a decision (P-7d).
    const InviteMember named[] = { mem(81, 0x00C0FFEEu, "Wolfgangetta"), mem(200, 0xAABBCCDDu, "Bo") };
    CHECK(cands(rows(w, named, 2)) == 0);
}

TEST_CASE("ui16-diff: pin 3 — a member APPEARING AFTER the snapshot is a candidate, and exactly once") {
    const InviteMember at_open[] = { mem(81, 0x00C0FFEEu) };
    const InviteWindow w = invite_snapshot_take(at_open, 1);
    const InviteMember later[] = { mem(81, 0x00C0FFEEu), mem(90, 0x12345678u) };
    const InviteSelList l = rows(w, later, 2);
    CHECK(cands(l) == 1);
    InviteSelRow r{};
    CHECK(l.at(0, r));
    CHECK(r.back == false);
    CHECK(r.cand.key_hash32 == 0x12345678u);              // ⛔ the WHOLE member, carried by identity
    CHECK(r.cand.id == 90);
    // ★ EXACTLY ONCE: it is ONE row, not one per refresh, and the row does not multiply when the list is rebuilt.
    for (int i = 0; i < 5; ++i) CHECK(cands(rows(w, later, 2)) == 1);
    // ...and the UNCONDITIONAL BACK is the last row, so the window can always be left (pin 4's rule, one screen up).
    CHECK(l.at(uint8_t(l.n - 1), r));
    CHECK(r.back == true);
    CHECK(l.at(l.n, r) == false);                          // ⛔ fails closed past the end
    CHECK(l.at(255, r) == false);
}

TEST_CASE("ui16-diff: pin 4 — a RE-DAD'd member (same hash, NEW id) is ⛔ not a candidate [authority (a)]") {
    // ★★ THE CASE AUTHORITY (a) EXISTS FOR: team-DAD re-ran and the member now answers to a different
    //    team-local id. It is the SAME peer — the hash is the identity — so announcing it as `NEW MEMBER` would
    //    ask the operator to grant a key to somebody who already has one.
    const InviteMember at_open[] = { mem(81, 0x00C0FFEEu) };
    const InviteWindow w = invite_snapshot_take(at_open, 1);
    const InviteMember redadded[] = { mem(97, 0x00C0FFEEu) };      // same hash, new id
    CHECK(invite_snap_has_id(w, 97) == false);                     // ⛔ authority (b) genuinely does NOT cover it
    CHECK(invite_is_new(w, redadded[0]) == false);                 // ...and (a) is what answers
    CHECK(cands(rows(w, redadded, 1)) == 0);
}

TEST_CASE("ui16-diff: pin 5 — a ROUTE-ONLY member whose hash turns authoritative is ⛔ not a candidate [(b)]") {
    // ★★ THE CASE AUTHORITY (b) EXISTS FOR (F-11's own falsifier): at the open this member had a `_team_peer`
    //    bit from a keyless multi-hop DV entry and NO authoritative binding, so a hash-keyed snapshot alone
    //    knows nothing about it. When the binding lands mid-window it would be announced as `NEW MEMBER`
    //    although it was there all along.
    const InviteMember at_open[] = { mem(82, 0u) };                // route-only: present, unidentified
    const InviteWindow w = invite_snapshot_take(at_open, 1);
    CHECK(w.n == 0);                                               // ⛔ (a) holds nothing for it
    CHECK(invite_snap_has_id(w, 82));                              // ...and (b) is the whole of the evidence
    const InviteMember resolved[] = { mem(82, 0x5A5A5A5Au) };      // the SAME id, now authoritative
    CHECK(invite_is_new(w, resolved[0]) == false);
    CHECK(cands(rows(w, resolved, 1)) == 0);
    // ⛔ AND WHILE IT IS STILL ROUTE-ONLY IT IS NOT LISTED EITHER (F-7 / C2): no fingerprint, no seal target, so
    //    ⛔ never a grantable candidate — and ⛔ never one with a blank or invented fingerprint.
    const InviteMember never_bound[] = { mem(120, 0u) };           // a route-only member that ARRIVED mid-window
    CHECK(invite_snap_has_id(w, 120) == false);                    // genuinely absent from both authorities...
    CHECK(invite_is_new(w, never_bound[0]) == false);              // ...and STILL not a candidate
    CHECK(cands(rows(w, never_bound, 1)) == 0);
}

TEST_CASE("ui16-diff: pin 6 — the DOUBLE-CHANGE case DOES prompt, and it is measured rather than asserted") {
    // ★★★★ THE DOCUMENTED **SAFE FALSE PROMPT** (F-11, owner-ruled), DRIVEN DIRECTLY: an UNKEYED member changes
    //      its team-local id AND acquires a hash inside one window. It then presents a hash nobody has seen at
    //      an id nobody has seen, so BOTH authorities answer "no" and it prompts.
    // ⛔ THIS IS NOT A BUG AND IT IS ⛔ NOT TO BE ENGINEERED AWAY: the operator must still confirm, the
    //    fingerprint is on the row, and suppressing it would mean suppressing a genuinely new member with
    //    exactly the same evidence. The case exists so the behaviour is MEASURED — a later slice that "fixes"
    //    it turns this case red and has to read the ruling.
    const InviteMember at_open[] = { mem(82, 0u) };                // present, unidentified
    const InviteWindow w = invite_snapshot_take(at_open, 1);
    const InviteMember both_changed[] = { mem(99, 0x7788AABBu) };  // new id AND a new hash, one window
    CHECK(invite_snap_has_id(w, 99) == false);
    CHECK(invite_snap_has_hash(w, 0x7788AABBu) == false);
    CHECK(invite_is_new(w, both_changed[0]) == true);              // ★ it PROMPTS — the ruled behaviour
    const InviteSelList l = rows(w, both_changed, 1);
    CHECK(cands(l) == 1);
    InviteSelRow r{};
    CHECK(l.at(0, r));
    CHECK(r.cand.key_hash32 == 0x7788AABBu);                       // ...carrying the fingerprint the human checks
}

TEST_CASE("ui16-diff: with NO snapshot nothing is a candidate — the window is not open (C2)") {
    // ★ THE `taken` FLAG IS THE STATE, ⛔ never `n == 0`: a team whose only member is us takes a snapshot with
    //   ZERO hashes, and that is a real, open window. A default-constructed one is a CLOSED window, and a closed
    //   window produces no candidate list at all (spec §4-N4's mutation, and P-12's native half).
    const InviteWindow closed{};
    CHECK(closed.taken == false);
    const InviteMember live[] = { mem(90, 0x12345678u), mem(91, 0x9ABCDEF0u) };
    CHECK(invite_is_new(closed, live[0]) == false);
    CHECK(cands(rows(closed, live, 2)) == 0);
    CHECK(rows(closed, live, 2).n == 1);                           // ...BACK, and nothing else
    // ...while an EMPTY but TAKEN snapshot is open and DOES announce an arrival.
    const InviteWindow empty_open = invite_snapshot_take(nullptr, 0);
    CHECK(empty_open.taken == true);
    CHECK(invite_is_new(empty_open, live[0]) == true);
    CHECK(cands(rows(empty_open, live, 2)) == 2);
    // ⛔ AND A NULL LIVE LIST IS AN EMPTY ONE, never a walk off the end.
    CHECK(rows(empty_open, nullptr, 8).n == 1);
    CHECK(rows(empty_open, live, 200).n == 3);                     // the bound, restated at the read
}

// ============================================================================ pin 8 — THE HANDLED SET (F-13)
TEST_CASE("ui16-handled: a handled candidate leaves the list, and the set is HASH-keyed and bounded") {
    const InviteWindow at_open = invite_snapshot_take(nullptr, 0);
    InviteWindow w = at_open;
    const InviteMember live[] = { mem(90, 0x12345678u, "Ann"), mem(91, 0x9ABCDEF0u, "Ann") };
    CHECK(cands(rows(w, live, 2)) == 2);
    // ★ REJECT adds the HASH. ⓘ The two candidates share a NAME, which is what makes this a P-7d case and not a
    //   bookkeeping one: a name-keyed set would silence both.
    CHECK(invite_handled_add(w, 0x12345678u) == true);
    CHECK(invite_handled_has(w, 0x12345678u) == true);
    CHECK(invite_handled_has(w, 0x9ABCDEF0u) == false);
    const InviteSelList l = rows(w, live, 2);
    CHECK(cands(l) == 1);
    InviteSelRow r{};
    CHECK(l.at(0, r));
    CHECK(r.cand.key_hash32 == 0x9ABCDEF0u);                       // ★ the OTHER one is untouched
    // ...and it stays gone across every refresh of the window.
    for (int i = 0; i < 5; ++i) CHECK(cands(rows(w, live, 2)) == 1);
    // ⛔ IT IS IDEMPOTENT (a second REJECT of the same hash is not a second entry) and ⛔ REFUSES 0, which is
    //    "no authoritative binding" and can never be a candidate in the first place.
    CHECK(invite_handled_add(w, 0x12345678u) == true);
    CHECK(w.handled_n == 1);
    CHECK(invite_handled_add(w, 0u) == false);
    CHECK(invite_handled_has(w, 0u) == false);
    CHECK(w.handled_n == 1);
    // ⛔ A FULL SET REFUSES (C2) — it ⛔ never evicts, because an evicted entry is a rejected candidate silently
    //    coming back, which is the whole property this set exists to give.
    InviteWindow full = at_open;
    for (uint8_t i = 0; i < kMaxInviteRows; ++i) CHECK(invite_handled_add(full, uint32_t(0x1000u + i)) == true);
    CHECK(full.handled_n == kMaxInviteRows);
    CHECK(invite_handled_add(full, 0xDEADBEEFu) == false);
    CHECK(full.handled_n == kMaxInviteRows);
    CHECK(invite_handled_has(full, 0x1000u) == true);               // ★ the oldest entry is STILL there
    CHECK(invite_handled_has(full, 0xDEADBEEFu) == false);
    // ★★ AND THE SET IS **VOLATILE**: it lives in the window, so a fresh window has none of it. (The MODEL owns
    //    the discard — `enter_provision` — and `test_firmware_ui_model.cpp` drives the close/re-open path.)
    const InviteWindow reopened = invite_snapshot_take(nullptr, 0);
    CHECK(reopened.handled_n == 0);
    CHECK(invite_handled_has(reopened, 0x12345678u) == false);
    CHECK(cands(rows(reopened, live, 2)) == 2);                     // ★ the rejected candidate RETURNS
}

// ================================================================== pins 14-16 — THE ROW AND ITS NAME LIFECYCLE
TEST_CASE("ui16-row: pin 14 — rule 2's INITIAL state: a BLANK name column and a populated fingerprint") {
    char out[mrui::kInviteRowCap];
    ui_fmt_invite_row(out, sizeof out, '>', mem(221, 0x006C2971u));
    CHECK(strcmp(out, ">       T221 6C2971") == 0);
    CHECK(strlen(out) == 19u);                                     // ⛔ EXACTLY the body budget, 1+6+1+4+1+6
    // ⛔ THE FINGERPRINT COLUMN IS NEVER EMPTY — that is the identity aid the operator reads, and the row exists
    //    only for members that HAVE one (the authoritative floor, F-7).
    CHECK(strstr(out, "6C2971") != nullptr);
    // ...the marker is a parameter, so an unselected row differs in exactly one byte.
    ui_fmt_invite_row(out, sizeof out, ' ', mem(221, 0x006C2971u));
    CHECK(strcmp(out, "        T221 6C2971") == 0);
    CHECK(strlen(out) == 19u);
    // ★ THE WIDTH HOLDS AT BOTH ENDS OF THE ID SPACE, which is where a `%-3u` would betray a `%u`.
    ui_fmt_invite_row(out, sizeof out, '>', mem(0, 0xFFFFFFFFu));
    CHECK(strcmp(out, ">       T0   FFFFFF") == 0);
    CHECK(strlen(out) == 19u);
    ui_fmt_invite_row(out, sizeof out, '>', mem(254, 0x00000000u));
    CHECK(strcmp(out, ">       T254 000000") == 0);
    CHECK(strlen(out) == 19u);
}

TEST_CASE("ui16-row: pin 15 — rule 3's UPGRADE: the name FILLS A COLUMN and the fingerprint is UNCHANGED") {
    char blank[mrui::kInviteRowCap], named[mrui::kInviteRowCap];
    const uint32_t hash = 0x006C2971u;
    ui_fmt_invite_row(blank, sizeof blank, '>', mem(221, hash));
    ui_fmt_invite_row(named, sizeof named, '>', mem(221, hash, "Wolfgangetta"));
    CHECK(strcmp(named, ">Wolfga T221 6C2971") == 0);
    CHECK(strlen(named) == 19u);
    // ★★★ THE NAME IS AN **ADDED COLUMN**, ⛔ NEVER A SWAP: the six fingerprint characters are byte-identical
    //     before and after the name arrives, and the row's LENGTH does not move either.
    CHECK(strcmp(blank + 7, named + 7) == 0);                      // the id and fingerprint columns, untouched
    CHECK(strcmp(blank + 13, named + 13) == 0);                    // ...and the fingerprint alone, byte for byte
    CHECK(strstr(named, "6C2971") != nullptr);
    CHECK(strlen(blank) == strlen(named));
    // ★ CLAMPED TO SIX — the TEAM row's own `%-6.6s` (§UI-17 S-11), so one name has ONE truncation on this panel.
    char row[mrui::kInviteRowCap];
    ui_fmt_invite_row(row, sizeof row, '>', mem(7, hash, "Wolfgangetta-the-longest"));
    CHECK(strcmp(row, ">Wolfga T7   6C2971") == 0);
    CHECK(strlen(row) == 19u);
    // ...and a name SHORTER than six pads rather than shifting the columns.
    ui_fmt_invite_row(row, sizeof row, '>', mem(7, hash, "Bo"));
    CHECK(strcmp(row, ">Bo     T7   6C2971") == 0);
    CHECK(strlen(row) == 19u);
    // ⛔ AND NOTHING IN THE NAME CAN REACH THE IDENTITY COLUMNS: a name that LOOKS like a hash still renders in
    //    its own six columns and changes neither the id nor the fingerprint (S-36's rule at row level).
    ui_fmt_invite_row(row, sizeof row, '>', mem(221, hash, "0x00c0ffee"));
    CHECK(strcmp(row, ">0x00c0 T221 6C2971") == 0);
}

TEST_CASE("ui16-row: pin 16 — the row's IDENTITY is the key_hash32, so two same-named candidates are two rows") {
    // ★★★ P-7d / F-15 rule 5: the name is a RENDER INPUT and never an identity. Two members with the SAME cached
    //     name and different hashes are two distinct rows, and they select — and are handled — independently.
    const InviteWindow w = invite_snapshot_take(nullptr, 0);
    const InviteMember live[] = { mem(90, 0x11111111u, "Ann"), mem(91, 0x22222222u, "Ann") };
    const InviteSelList l = rows(w, live, 2);
    CHECK(cands(l) == 2);
    InviteSelRow a{}, b{};
    CHECK(l.at(0, a));
    CHECK(l.at(1, b));
    CHECK(a.cand.key_hash32 != b.cand.key_hash32);
    CHECK(strcmp(a.cand.name, b.cand.name) == 0);                  // ...same name, and still two rows
    // ...and their ROWS differ, because the fingerprint is what tells them apart on the glass.
    char ra[mrui::kInviteRowCap], rb[mrui::kInviteRowCap];
    ui_fmt_invite_row(ra, sizeof ra, '>', a.cand);
    ui_fmt_invite_row(rb, sizeof rb, ' ', b.cand);
    CHECK(strcmp(ra, ">Ann    T90  111111") == 0);
    CHECK(strcmp(rb, " Ann    T91  222222") == 0);
    // ★ HANDLING ONE LEAVES THE OTHER: a name-keyed set would silence both (the mutation this pins).
    InviteWindow w2 = w;
    CHECK(invite_handled_add(w2, a.cand.key_hash32));
    const InviteSelList after = rows(w2, live, 2);
    CHECK(cands(after) == 1);
    InviteSelRow r{};
    CHECK(after.at(0, r));
    CHECK(r.cand.key_hash32 == 0x22222222u);
}

// =================================================================== pins 9 and 17 — THE TWO IDENTITY TOKENS
TEST_CASE("ui16-fp: the MEMBER fingerprint is its own definition, six columns of the key_hash32") {
    char out[mrui::kMemberFpCap];
    ui_fmt_member_fingerprint(out, sizeof out, 0x006C2971u);
    CHECK(strcmp(out, "6C2971") == 0);
    CHECK(strlen(out) == 6u);
    // ★ UPPERCASE AND ZERO-PADDED, both load-bearing: a space-padded token stops being a placeable field, and a
    //   list where one entry reads `a1b2c3` and another `A1B2C3` reads as two different people.
    ui_fmt_member_fingerprint(out, sizeof out, 0x00000001u);
    CHECK(strcmp(out, "000001") == 0);
    ui_fmt_member_fingerprint(out, sizeof out, 0xFFABCDEFu);
    CHECK(strcmp(out, "ABCDEF") == 0);
    // ⛔⛔ IT IS THE **LOW 24 BITS**, WHICH IS WHY IT MAY NEVER BE AN AUTHORITY (P-7): two peers one byte apart
    //     fingerprint identically, and only the FULL hash tells them apart — which is what every act keys on.
    char a[mrui::kMemberFpCap], b[mrui::kMemberFpCap];
    ui_fmt_member_fingerprint(a, sizeof a, 0x11ABCDEFu);
    ui_fmt_member_fingerprint(b, sizeof b, 0x22ABCDEFu);
    CHECK(strcmp(a, b) == 0);
    CHECK(0x11ABCDEFu != 0x22ABCDEFu);
    // ★★ AND IT IS A **SEPARATE** DEFINITION FROM THE TEAM-ID FINGERPRINT BY RULING (F-8): the same number in
    //    the two spaces is the same six characters, and that is a coincidence of both being `%06lX` — ⛔ not a
    //    licence to share one helper, because they describe different entities and either may be re-ruled.
    //    ⓘ Asserted as a VALUE here rather than by calling chrome: this unit may not include it (see its header).
    ui_fmt_member_fingerprint(out, sizeof out, 0x00D9348Au);
    CHECK(strcmp(out, "D9348A") == 0);
    // ⛔ FAILS CLOSED: no buffer means no write and no crash.
    ui_fmt_member_fingerprint(nullptr, sizeof out, 0x006C2971u);
    ui_fmt_member_fingerprint(out, 0, 0x00112233u);
    CHECK(strcmp(out, "D9348A") == 0);                             // ...and the neighbour is untouched
}

TEST_CASE("ui16-hash: pin 17 — the confirmation's identity is the FULL 0x%08lX hash, always") {
    char out[mrui::kMemberHashCap];
    ui_fmt_member_hash_full(out, sizeof out, 0x00C0FFEEu);
    CHECK(strcmp(out, "0x00C0FFEE") == 0);
    CHECK(strlen(out) == 10u);
    CHECK(strlen(out) + 1u <= 19u);                                // it fits the body with a marker to spare
    // ★★★ IT IS THE **WHOLE** 32 BITS (P-7c / F-15 rule 4): the two peers that share a fingerprint are told
    //     apart HERE, at the moment of the irreversible act — a truncated form on this screen would put the
    //     operator's confirmation on a value 255 other peers also answer to.
    char a[mrui::kMemberHashCap], b[mrui::kMemberHashCap];
    ui_fmt_member_hash_full(a, sizeof a, 0x11ABCDEFu);
    ui_fmt_member_hash_full(b, sizeof b, 0x22ABCDEFu);
    CHECK(strcmp(a, b) != 0);
    CHECK(strcmp(a, "0x11ABCDEF") == 0);
    CHECK(strcmp(b, "0x22ABCDEF") == 0);
    // ...zero-padded and uppercase, i.e. the SAME string the console prints for the same peer.
    ui_fmt_member_hash_full(out, sizeof out, 0x00000001u);
    CHECK(strcmp(out, "0x00000001") == 0);
    ui_fmt_member_hash_full(out, sizeof out, 0xFFFFFFFFu);
    CHECK(strcmp(out, "0xFFFFFFFF") == 0);
    // ⛔ FAILS CLOSED like every token here.
    ui_fmt_member_hash_full(nullptr, sizeof out, 0x00C0FFEEu);
    ui_fmt_member_hash_full(out, 0, 0x00112233u);
    CHECK(strcmp(out, "0xFFFFFFFF") == 0);
}

TEST_CASE("ui16-noident: pin 9 — no NAME-SHAPED value can reach an identity column, whatever the name is") {
    // ★★ THE SCREEN'S OWN IDENTITY IS A FINGERPRINT AND ⛔ NEVER A LABEL (F-3, S-36): there is no team-name
    //    field anywhere in this firmware, and this unit has NO name resolver of any kind — the only name it can
    //    render is the one the publisher already cached beside a VERIFIED pubkey, in its own six columns.
    // ⓘ The screen's TEAM-fingerprint row is the RENDERER's (`ui_fmt_team_fingerprint` of `s.team_id`) and is
    //   asserted by exact bytes in the probe's INVITE phase; what is measurable HERE is that no name changes an
    //   identity token, which is the property that rule would be violated by.
    char with_name[mrui::kInviteRowCap], without[mrui::kInviteRowCap];
    ui_fmt_invite_row(without,   sizeof without,   '>', mem(221, 0x006C2971u));
    ui_fmt_invite_row(with_name, sizeof with_name, '>', mem(221, 0x006C2971u, "TEAMNAME"));
    CHECK(strcmp(without + 7, with_name + 7) == 0);                // the id and fingerprint columns are identical
    char fp[mrui::kMemberFpCap], hash[mrui::kMemberHashCap];
    ui_fmt_member_fingerprint(fp, sizeof fp, 0x006C2971u);
    ui_fmt_member_hash_full(hash, sizeof hash, 0x006C2971u);
    CHECK(strcmp(fp, "6C2971") == 0);
    CHECK(strcmp(hash, "0x006C2971") == 0);
}

// ================================================================================= pin 7 — THE LEXEMES
TEST_CASE("ui16-words: every lexeme is exact, fits 19 columns, and the FORBIDDEN ones appear nowhere") {
    CHECK(strcmp(mrui::kInviteTitle,  "INVITE MEMBER") == 0);      // S-12
    CHECK(strcmp(mrui::kInviteNew,    "NEW MEMBER") == 0);         // S-14
    CHECK(strcmp(mrui::kInviteEmpty,  "NO CANDIDATES") == 0);      // S-15
    CHECK(strcmp(mrui::kInviteClosed, "WINDOW CLOSED") == 0);      // S-16
    CHECK(strcmp(mrui::invite_confirm_label(false), "BACK") == 0);   // S-17's safe arm, and the shipped spelling
    CHECK(strcmp(mrui::invite_confirm_label(true),  "REJECT") == 0); // S-17, the ONE act this slice reaches
    const char* const all[] = { mrui::kInviteTitle, mrui::kInviteNew, mrui::kInviteEmpty, mrui::kInviteClosed,
                                mrui::invite_confirm_label(false), mrui::invite_confirm_label(true) };
    for (const char* s : all) {
        CHECK(1u + strlen(s) <= 19u);                              // ...with the cursor marker
        // ⛔⛔ THE THREE FORBIDDEN WORDS, AND THEIR ABSENCE IS A TEST RATHER THAN A PREFERENCE: `KEYLESS` (S-33)
        //     is the design's own banned word for a member, `WAITING FOR KEY` (S-34) is ambiguous between the
        //     recipient's PUBKEY and the team CONTENT key, and `JOIN COMPLETE` (S-32) claims an end-to-end
        //     outcome nothing acknowledges. ⓘ `GRANT KEY` is not forbidden — it is simply N6's, and this slice
        //     performs no grant, so it may not be spelled here either.
        CHECK(strstr(s, "KEYLESS") == nullptr);
        CHECK(strstr(s, "WAITING FOR KEY") == nullptr);
        CHECK(strstr(s, "JOIN COMPLETE") == nullptr);
        CHECK(strstr(s, "GRANT KEY") == nullptr);
    }
    // The NOTE row: the candidate word when there is one, the empty state when there is not. ⓘ A list of ONE row
    // is BACK alone — the unconditional exit — which is what makes `n <= 1` the empty test rather than `n == 0`.
    const InviteWindow w = invite_snapshot_take(nullptr, 0);
    const InviteMember live[] = { mem(90, 0x12345678u) };
    CHECK(strcmp(invite_note(rows(w, nullptr, 0)), mrui::kInviteEmpty) == 0);
    CHECK(strcmp(invite_note(rows(w, live, 1)), mrui::kInviteNew) == 0);
    // ...and a window that is CLOSED says the empty thing, because it produces no candidates at all.
    const InviteWindow closed{};
    CHECK(strcmp(invite_note(rows(closed, live, 1)), mrui::kInviteEmpty) == 0);
}
