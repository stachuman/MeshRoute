// MeshRoute — test_firmware_ui_invite.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK, never
//     REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §UI-16 N4/N5 — the native suite for the `INVITE MEMBER` window's pure unit (`src/firmware_ui_invite.h`): the TWO
// snapshot authorities and the diff over them (F-11), the AUTHORITATIVE floor that keeps a route-only member off
// the grantable list (F-7), the VOLATILE per-window handled set (F-13), the candidate row's name lifecycle
// (F-15 rules 2-3, 5), the member fingerprint and full-hash identity (S-13 / P-7c), N5's grant-bar preflight and
// exact typed TEAM request, the cached-key correlation, and every lexeme.
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
//   `test/test_firmware_ui_model.cpp`'s §UI-16 N4/N5 blocks and the `model` battery; the RENDERER
//   (`src/firmware_ui.cpp`'s five invite arms) is compiled by neither the native suite nor the simulator (§B115)
//   and its cover is `tools/probe_firmware_ui`'s INVITE phase, which drives REAL members through the REAL node.
#include "doctest.h"
#include "firmware_ui_invite.h"
#include "node.h"                 // NodeConfig — the DERIVATION pin for the window's five minutes
#include "protocol_constants.h"   // team_seen_retain_ms — N1's two periods, the other half of that derivation
#include "identity.h"             // ★ §UI-16 N6 pin 12: Identity / identity_from_seed — the equivalence fixture's
                                  //   two real ends, so the grant's pubkey bar is exercised with REAL key material
#include "support/test_hal.h"     // ...over a REAL Node (the shared fixture — ⛔ never a ninth hand-rolled Hal)
#include "frame_codec.h"          // ★ §UI-16 N6b (QG round 2): parse_h — the H-query DECODER the air pin uses, so
                                  //   the frame is CLASSIFIED by the shipped codec (U1), ⛔ not by a local nibble read
#include <cstddef>                // offsetof — the verdict carrier's measured placement
#include <span>                   // std::span — parse_h's own parameter type
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

struct InviteDeviceFake : mrui::IUiInviteDevice {
    void request_team_announcement() override {}
    bool present = false;
    MESHROUTE_NS::Node::PeerKeyConf conf = MESHROUTE_NS::Node::PeerKeyConf::overheard;
    mutable int reads = 0;
    mutable uint32_t read_hash = 0;
    mutable MESHROUTE_NS::Node::PeerKeyConf read_floor = MESHROUTE_NS::Node::PeerKeyConf::overheard;
    int issues = 0;
    MESHROUTE_NS::Command last{};
    // The executor's answer, scripted. The default is the ordinary on-air acceptance.
    mrui::UiInviteIssue answer{ true, MESHROUTE_NS::CmdCode::queued, true };
    bool peer_key_at_least(uint32_t hash, MESHROUTE_NS::Node::PeerKeyConf floor) const override {
        ++reads; read_hash = hash; read_floor = floor;
        return present && static_cast<uint8_t>(conf) >= static_cast<uint8_t>(floor);
    }
    mrui::UiInviteIssue issue(const MESHROUTE_NS::Command& command) override {
        ++issues; last = command; return answer;
    }
    // ---- §UI-16 N6: THE GRANT SEAM, SCRIPTED. ★ The FAKE is what supplies the outcome, so every expected panel
    //      word below is computed by the PURE mapper from the outcome the fake returned — ⛔ never a literal that
    //      would keep agreeing with a mapping that had been edited underneath it.
    MESHROUTE_NS::Node::TeamKeyGrantTx tx = MESHROUTE_NS::Node::TeamKeyGrantTx::queued;
    uint16_t tx_ctr = 0;                      // what the core would have written into `out_ctr`
    uint8_t  tx_dst = 0;                      // §UI-16 N6b: ...and into `out_dst` — the SEND-TIME resolved id
    int      grants = 0;
    uint32_t grant_hash = 0;
    MESHROUTE_NS::Plane grant_plane = MESHROUTE_NS::Plane::AUTO;   // ⛔ NOT the expected value — a default that
                                                                   //    would pass the plane check is worthless
    MESHROUTE_NS::Node::TeamKeyGrantTx grant(uint32_t key_hash32, MESHROUTE_NS::Plane plane,
                                             uint16_t* out_ctr, uint8_t* out_dst) override {
        ++grants; grant_hash = key_hash32; grant_plane = plane;
        if (out_ctr) *out_ctr = tx_ctr;
        if (out_dst) *out_dst = tx_dst;
        return tx;
    }
};

// The ELEVEN `TeamKeyGrantTx` arms, once, so every sweep below walks the WHOLE enum rather than a sample.
// ★ §UI-16 N6b RE-ANCHORED THIS FROM EIGHT: `parked`, `queue_full` and `send_failed` are the three outcomes the
//   core used to return as `queued`, and a sweep that still walked eight would prove nothing about them.
constexpr MESHROUTE_NS::Node::TeamKeyGrantTx kAllTx[11] = {
    MESHROUTE_NS::Node::TeamKeyGrantTx::queued,   MESHROUTE_NS::Node::TeamKeyGrantTx::no_team,
    MESHROUTE_NS::Node::TeamKeyGrantTx::no_key,   MESHROUTE_NS::Node::TeamKeyGrantTx::no_identity,
    MESHROUTE_NS::Node::TeamKeyGrantTx::no_pubkey, MESHROUTE_NS::Node::TeamKeyGrantTx::self,
    MESHROUTE_NS::Node::TeamKeyGrantTx::delegated, MESHROUTE_NS::Node::TeamKeyGrantTx::too_large,
    MESHROUTE_NS::Node::TeamKeyGrantTx::parked,   MESHROUTE_NS::Node::TeamKeyGrantTx::queue_full,
    MESHROUTE_NS::Node::TeamKeyGrantTx::send_failed };

// One `send_aired` / `send_failed` push, as `Node::push_send_aired_if_owned` and `push_send_failed` build them.
MESHROUTE_NS::Push aired_push(uint8_t dst, uint16_t ctr) {
    MESHROUTE_NS::Push pu{};
    pu.kind = MESHROUTE_NS::PushKind::send_aired; pu.dst = dst; pu.ctr = ctr;
    return pu;
}
MESHROUTE_NS::Push failed_push(uint8_t dst, uint16_t ctr) {
    MESHROUTE_NS::Push pu{};
    pu.kind = MESHROUTE_NS::PushKind::send_failed; pu.dst = dst; pu.ctr = ctr;
    pu.reason = MESHROUTE_NS::SendFailReason::no_route;
    return pu;
}

}  // namespace

// ========================================================================== N5 — the grant bar and typed request
TEST_CASE("ui16-pubkey-preflight: the grant's authoritative floor is reused exactly, including pinned") {
    InviteDeviceFake d;
    CHECK(mrui::invite_grant_preflight(nullptr, 0xAABBCCDDu) == false);
    CHECK(mrui::invite_grant_preflight(&d, 0) == false);
    CHECK(d.reads == 0);

    d.present = false;
    CHECK(mrui::invite_grant_preflight(&d, 0xAABBCCDDu) == false);
    CHECK(d.read_hash == 0xAABBCCDDu);
    CHECK(d.read_floor == MESHROUTE_NS::Node::PeerKeyConf::authoritative);

    d.present = true; d.conf = MESHROUTE_NS::Node::PeerKeyConf::overheard;
    CHECK(mrui::invite_grant_preflight(&d, 0xAABBCCDDu) == false);
    CHECK(d.read_floor == MESHROUTE_NS::Node::PeerKeyConf::authoritative);
    d.conf = MESHROUTE_NS::Node::PeerKeyConf::authoritative;
    CHECK(mrui::invite_grant_preflight(&d, 0xAABBCCDDu) == true);
    d.conf = MESHROUTE_NS::Node::PeerKeyConf::pinned;
    CHECK(mrui::invite_grant_preflight(&d, 0xAABBCCDDu) == true);
}

TEST_CASE("ui16-reqpubkey-command: one existing typed command carries TEAM and the frozen full hash") {
    const MESHROUTE_NS::Command c = mrui::invite_reqpubkey_command(0xFEDCBA98u);
    CHECK(c.kind == MESHROUTE_NS::CmdKind::reqpubkey);
    CHECK(c.u.resolve.dst_hash == 0xFEDCBA98u);
    CHECK(c.u.resolve.dst_id == 0);
    CHECK(c.u.resolve.hard == false);
    CHECK(c.u.resolve.plane == static_cast<uint8_t>(MESHROUTE_NS::Plane::TEAM));
    CHECK(c.body == nullptr);
    CHECK(c.body_len == 0);
    CHECK(c.no_intro == false);
    char line[mrui::kInviteReqpubkeyLineCap];
    CHECK(mrui::ui_fmt_invite_reqpubkey_line(line, sizeof line, c) == 23u);
    CHECK(strcmp(line, "reqpubkey 0xFEDCBA98 -t") == 0);
    MESHROUTE_NS::Command wrong = c;
    wrong.u.resolve.plane = static_cast<uint8_t>(MESHROUTE_NS::Plane::GLOBAL);
    CHECK(mrui::ui_fmt_invite_reqpubkey_line(line, sizeof line, wrong) == 0u);
}

// ★★★★ THE QG BLOCKER's OWN CASE (2026-08-24): `WAITING FOR PUBKEY` is a claim, and only an ACCEPTED request may
//      make it. Every arm below is a way the first cut claimed it falsely — plus the ONE arm that looks like a
//      failure and is a success.
TEST_CASE("ui16-reqpubkey-started: only a successfully started workflow enters the wait — and a LOCAL completion is one") {
    InviteDeviceFake d;
    // ⛔ NO SEAM AND NO TARGET FAIL CLOSED, AND WITHOUT SPENDING A CALL.
    CHECK(mrui::invite_issue_reqpubkey(nullptr, 0xAABBCCDDu) == false);
    CHECK(mrui::invite_issue_reqpubkey(&d, 0) == false);
    CHECK(d.issues == 0);

    // The ordinary acceptance: the line parsed, the command was admitted, a frame was taken.
    CHECK(mrui::invite_issue_reqpubkey(&d, 0xAABBCCDDu) == true);
    CHECK(d.issues == 1);
    CHECK(d.last.kind == MESHROUTE_NS::CmdKind::reqpubkey);
    CHECK(d.last.u.resolve.dst_hash == 0xAABBCCDDu);
    CHECK(d.last.u.resolve.plane == static_cast<uint8_t>(MESHROUTE_NS::Plane::TEAM));

    // ★★ THE PARSE/FORMAT FAILURE, IN ITS REAL SHAPE — and it is why `ok` is not redundant: `CmdResult::code`
    //    DEFAULTS to `queued`, so a result that never parsed carries the success code. A gate reading `code`
    //    alone would call this a started request.
    d.answer = mrui::UiInviteIssue{};
    CHECK(d.answer.ok == false);
    CHECK(d.answer.code == MESHROUTE_NS::CmdCode::queued);      // the trap, asserted rather than described
    CHECK(mrui::invite_request_started(d.answer) == false);
    CHECK(mrui::invite_issue_reqpubkey(&d, 0xAABBCCDDu) == false);
    CHECK(d.issues == 2);                                       // the attempt happened; the CLAIM did not follow

    // ★★ EVERY SYNCHRONOUS REFUSAL, LOUDLY NOT A REQUEST. `err_no_identity` is this path's real one (no Ed25519
    //    identity ⇒ the mutual WANT_PUBKEY exchange is impossible) and `err_tx_queue_full` is the transient one.
    const MESHROUTE_NS::CmdCode refusals[] = {
        MESHROUTE_NS::CmdCode::err_no_identity, MESHROUTE_NS::CmdCode::err_unsupported,
        MESHROUTE_NS::CmdCode::err_no_binding,  MESHROUTE_NS::CmdCode::err_ambiguous_plane,
        MESHROUTE_NS::CmdCode::err_unprovisioned, MESHROUTE_NS::CmdCode::err_tx_queue_full,
    };
    for (const MESHROUTE_NS::CmdCode c : refusals) {
        d.answer = mrui::UiInviteIssue{ true, c, false };
        CHECK(mrui::invite_request_started(d.answer) == false);
        CHECK(mrui::invite_issue_reqpubkey(&d, 0xAABBCCDDu) == false);
    }

    // ★★★ THE RACE THAT IS **NOT** A FAILURE (`lib/core/command.h`'s own note on `accepted`): `reqpubkey` has one
    //     accepted outcome that hands the TX path nothing — the branch answering from the LOCAL key cache, which
    //     reports through the `peer_key_cached` push. ⛔ It is `queued` with `accepted == false`, and it MUST count
    //     as started, or the operator whose request already succeeded is stranded at NEED PUBKEY.
    d.answer = mrui::UiInviteIssue{ true, MESHROUTE_NS::CmdCode::queued, false };
    CHECK(mrui::invite_request_started(d.answer) == true);
    CHECK(mrui::invite_issue_reqpubkey(&d, 0xAABBCCDDu) == true);
    // ...and `accepted` alone never promotes a refusal either.
    d.answer = mrui::UiInviteIssue{ true, MESHROUTE_NS::CmdCode::err_no_identity, true };
    CHECK(mrui::invite_request_started(d.answer) == false);
}

TEST_CASE("ui16-pubkey-push: both the kind and the full candidate hash must match") {
    MESHROUTE_NS::Push pu{};
    pu.kind = MESHROUTE_NS::PushKind::peer_key_cached;
    pu.sender_hash = 0xAABBCCDDu;
    CHECK(mrui::invite_peer_key_cached_matches(pu, 0xAABBCCDDu) == true);
    CHECK(mrui::invite_peer_key_cached_matches(pu, 0xAABBCCDEu) == false);
    pu.kind = MESHROUTE_NS::PushKind::hash_resolved;
    CHECK(mrui::invite_peer_key_cached_matches(pu, 0xAABBCCDDu) == false);
}

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
    CHECK(strcmp(mrui::kInviteNeedPubkey,    "NEED PUBKEY") == 0);          // S-18
    CHECK(strcmp(mrui::kInviteRequestPubkey, "REQUEST PUBKEY") == 0);       // S-19
    CHECK(strcmp(mrui::kInviteWaitingPubkey, "WAITING FOR PUBKEY") == 0);   // S-20
    CHECK(strcmp(mrui::invite_pubkey_label(false), "BACK") == 0);
    CHECK(strcmp(mrui::invite_pubkey_label(true),  "REQUEST PUBKEY") == 0);
    CHECK(strcmp(mrui::invite_confirm_label(false), "REJECT") == 0);        // S-17, selected initially
    CHECK(strcmp(mrui::invite_confirm_label(true),  "GRANT KEY") == 0);     // S-17's other half (⛔ not S-22)
    const char* const all[] = { mrui::kInviteTitle, mrui::kInviteNew, mrui::kInviteEmpty, mrui::kInviteClosed,
                                mrui::kInviteNeedPubkey, mrui::kInviteRequestPubkey,
                                mrui::kInviteWaitingPubkey, mrui::invite_pubkey_label(false),
                                mrui::invite_confirm_label(false), mrui::invite_confirm_label(true) };
    for (const char* s : all) {
        CHECK(1u + strlen(s) <= 19u);                              // ...with the cursor marker
        // ⛔⛔ THE THREE FORBIDDEN WORDS, AND THEIR ABSENCE IS A TEST RATHER THAN A PREFERENCE: `KEYLESS` (S-33)
        //     is the design's own banned word for a member, `WAITING FOR KEY` (S-34) is ambiguous between the
        //     recipient's PUBKEY and the team CONTENT key, and `JOIN COMPLETE` (S-32) claims an end-to-end
        //     outcome nothing acknowledges. `GRANT KEY` is enabled at N5's boundary; N6 still owns its act.
        CHECK(strstr(s, "KEYLESS") == nullptr);
        CHECK(strstr(s, "WAITING FOR KEY") == nullptr);
        CHECK(strstr(s, "JOIN COMPLETE") == nullptr);
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

// ============================================================== §UI-16 N6 — THE GRANT ACT, ITS EIGHT ARMS AND
//                                                                THE `{dst, ctr}` `send_aired` CORRELATION
// ★★★ THE WHOLE SLICE IS DRIVEN HERE BECAUSE THE WHOLE SLICE IS PURE: the fake below supplies the OUTCOME and the
//     handle, so every expected word is computed by the mapper from what the fake returned — ⛔ never a literal a
//     later edit could make agree with itself.

TEST_CASE("ui16-grant-plane: the grant flies on Plane::TEAM, named ONCE in the pure unit") {
    // ★★★ MUTATION TARGET (spec §4-N6): the plane changed away from TEAM ⇒ a member enumerated on the team plane is
    //     addressed on another one — and `delegated`, which the real seam can never return while this is TEAM,
    //     becomes reachable.
    CHECK(mrui::kInviteGrantPlane == MESHROUTE_NS::Plane::TEAM);
    InviteDeviceFake d;
    mrui::InviteGrantResult r{};
    d.tx = MESHROUTE_NS::Node::TeamKeyGrantTx::queued; d.tx_ctr = 7; d.tx_dst = 221;
    CHECK(mrui::invite_grant_perform(&d, 0x00BEDEADu, r) == true);
    // The seam is handed the plane the PURE unit chose, not one the device TU picked.
    CHECK(d.grants == 1);
    CHECK(d.grant_plane == MESHROUTE_NS::Plane::TEAM);
    CHECK(d.grant_plane == mrui::kInviteGrantPlane);
    // ...and the request is N5's plane too: one member, one address plane, both ceremonies.
    CHECK(mrui::invite_reqpubkey_command(0x00BEDEADu).u.resolve.plane ==
          static_cast<uint8_t>(mrui::kInviteGrantPlane));
}

TEST_CASE("ui16-grant-arms: ALL ELEVEN outcomes map to their OWN word — and NONE of them reads the handle") {
    using TX = MESHROUTE_NS::Node::TeamKeyGrantTx;
    // ★★★★ PIN 4, THE HEADLINE CONTROL (✅ F-9): `queued` is ADMISSION — `GRANT QUEUED` — and it is ⛔ NOT
    //      `KEY SENT`. Nothing has left the radio at this point; only a correlated TxDone edge may say that.
    CHECK(mrui::invite_grant_state_of(TX::queued) == mrui::InviteGrantState::queued);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::queued), "GRANT QUEUED") == 0);   // S-21
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::queued),
                 mrui::invite_grant_word(mrui::InviteGrantState::sent)) != 0);
    // ★★★★ §UI-16 N6b — THE THREE OUTCOMES THAT USED TO ARRIVE WEARING `queued`, each with its own word now.
    CHECK(mrui::invite_grant_state_of(TX::parked)      == mrui::InviteGrantState::parked);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::parked), "GRANT PARKED") == 0);        // S-37
    CHECK(mrui::invite_grant_state_of(TX::queue_full)  == mrui::InviteGrantState::queue_full);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::queue_full), "GRANT QUEUE FULL") == 0);// S-38
    // ⛔ AND S-38's OWN PROHIBITION: the admission refusal is ⛔ NOT the in-flight failure's word, and ⛔ not the
    //    admission word either. Three distinct states, three distinct strings.
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::queue_full),
                 mrui::invite_grant_word(mrui::InviteGrantState::failed)) != 0);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::queue_full),
                 mrui::invite_grant_word(mrui::InviteGrantState::queued)) != 0);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::queue_full),
                 mrui::invite_grant_word(mrui::InviteGrantState::parked)) != 0);
    // ⓘ `send_failed` IS the failure fact, arriving synchronously — one fact, ONE word (⛔ no new lexeme).
    CHECK(mrui::invite_grant_state_of(TX::send_failed) == mrui::InviteGrantState::failed);
    // ★★★★ THE WITHDRAWN INFERENCE, ATTACKED DIRECTLY: the state comes from the OUTCOME and the outcome ONLY, so a
    //      seam answering `queued` with NO handle is still `GRANT QUEUED` — ⛔ never re-worded as PARKED by this
    //      unit. (The real core cannot produce that pair; a fake can, and that is exactly what makes it a control:
    //      it fails the moment anybody re-derives the state from the counter.)
    {
        InviteDeviceFake q; q.tx = TX::queued; q.tx_ctr = 0; q.tx_dst = 8;
        mrui::InviteGrantResult qr{};
        CHECK(mrui::invite_grant_perform(&q, 0x00BEDEADu, qr) == true);
        CHECK(qr.st == mrui::InviteGrantState::queued);
        CHECK(strcmp(mrui::invite_grant_word(qr.st), "GRANT QUEUED") == 0);
    }
    // ★★★ THE SIX REFUSALS, ONE LINE EACH — ⛔ THEY MAY NOT COLLAPSE (S-24). Each remedy is a different sentence.
    CHECK(mrui::invite_grant_state_of(TX::no_team)     == mrui::InviteGrantState::no_team);
    CHECK(mrui::invite_grant_state_of(TX::no_key)      == mrui::InviteGrantState::no_key);
    CHECK(mrui::invite_grant_state_of(TX::no_identity) == mrui::InviteGrantState::no_identity);
    CHECK(mrui::invite_grant_state_of(TX::no_pubkey)   == mrui::InviteGrantState::no_pubkey);
    CHECK(mrui::invite_grant_state_of(TX::self)        == mrui::InviteGrantState::self);
    CHECK(mrui::invite_grant_state_of(TX::too_large)   == mrui::InviteGrantState::name_too_long);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::no_team),       "NOT IN A TEAM") == 0);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::no_key),        "NO TEAM KEY") == 0);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::no_identity),   "NO IDENTITY") == 0);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::self),          "SELF") == 0);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::name_too_long), "NAME TOO LONG") == 0);
    // ...and `no_pubkey` reuses N5's landing word DELIBERATELY: same fact, same candidate, ONE spelling.
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::no_pubkey), mrui::kInviteNeedPubkey) == 0);
    // ★★★★ `delegated` IS UNREACHABLE ON THE REAL SEAM AND FAILS **LOUD** FROM A FAKE (C2): it is a REFUSAL word,
    //      ⛔ never a plausible one, and no push can ever promote it (see the correlation case).
    CHECK(mrui::invite_grant_state_of(TX::delegated)  == mrui::InviteGrantState::wrong_plane);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::wrong_plane), "WRONG PLANE") == 0);
    // ⛔ EVERY ARM IS DRIVEN THROUGH THE REAL PERFORM PATH TOO, at BOTH handle values, and every one of the ELEVEN
    //    resulting words is DISTINCT — the collapse this suite exists to catch would make two of them equal.
    // ⓘ ELEVEN, and the arithmetic is worth stating: N6b's three arms each brought a word, but only `queue_full`
    //   brought a NEW lexeme — `parked` already had one, and `send_failed` SHARES S-23 with the correlated
    //   failure (one fact, one word). The correlated failure is not in this sweep (it needs a push), so the
    //   eleven seam outcomes still produce eleven distinct strings.
    const char* seen[11] = {};
    uint8_t n_seen = 0;
    for (const TX t : kAllTx) {
        for (const uint16_t ctr : { uint16_t(0), uint16_t(31) }) {
            InviteDeviceFake d; d.tx = t; d.tx_ctr = ctr;
            mrui::InviteGrantResult r{};
            CHECK(mrui::invite_grant_perform(&d, 0x00BEDEADu, r) == true);
            CHECK(r.st != mrui::InviteGrantState::none);
            CHECK(r.st != mrui::InviteGrantState::sent);      // ⛔ NOTHING the seam returns can say KEY SENT
            // ⛔ ...and only the core's OWN `send_failed` may say GRANT FAILED without a push.
            if (t != TX::send_failed) CHECK(r.st != mrui::InviteGrantState::failed);
            const char* w = mrui::invite_grant_word(r.st);
            CHECK(w[0] != '\0');                              // every arm has a word of its own
            bool dup = false;
            for (uint8_t i = 0; i < n_seen; ++i) if (strcmp(seen[i], w) == 0) dup = true;
            if (!dup && n_seen < 11) seen[n_seen++] = w;
        }
    }
    CHECK(n_seen == 11);       // eleven arms, eleven distinct words on the seam path
}

TEST_CASE("ui16-grant-perform: the target is the FROZEN hash, and nothing runs without a seam") {
    // ⛔ FAILS CLOSED, exactly as N5's request does: no seam and no identity perform NOTHING and claim NOTHING.
    mrui::InviteGrantResult r{};
    CHECK(mrui::invite_grant_perform(nullptr, 0x00BEDEADu, r) == false);
    CHECK(r.st == mrui::InviteGrantState::none);
    CHECK(strcmp(mrui::invite_grant_word(r.st), "") == 0);      // ⛔ and `none` invents no reassuring word
    InviteDeviceFake d;
    CHECK(mrui::invite_grant_perform(&d, 0, r) == false);
    CHECK(d.grants == 0);                                       // ⛔ a zero hash spends no call at all
    // ★★★ P-7d: the hash goes to the seam VERBATIM, and §UI-16 N6b — BOTH correlation terms come BACK from the
    //     seam. Nothing this function was told is substituted for what the core answered.
    d.tx = MESHROUTE_NS::Node::TeamKeyGrantTx::queued; d.tx_ctr = 4242; d.tx_dst = 221;
    CHECK(mrui::invite_grant_perform(&d, 0x00BEDEADu, r) == true);
    CHECK(d.grant_hash == 0x00BEDEADu);
    CHECK(r.hash == 0x00BEDEADu);
    CHECK(r.dst  == 221);
    CHECK(r.ctr  == 4242);
    CHECK(r.st   == mrui::InviteGrantState::queued);
    // A refusal still records the identity the act named, so the result screen can draw it (P-7c).
    InviteDeviceFake e; e.tx = MESHROUTE_NS::Node::TeamKeyGrantTx::no_key; e.tx_ctr = 0;
    mrui::InviteGrantResult r2{};
    CHECK(mrui::invite_grant_perform(&e, 0x006C2971u, r2) == true);
    CHECK(r2.hash == 0x006C2971u);
    CHECK(r2.ctr == 0);
    CHECK(r2.st == mrui::InviteGrantState::no_key);
}

TEST_CASE("ui16-grant-correlate: KEY SENT needs a CORRELATED push — both terms, and only over a queued state") {
    InviteDeviceFake d; d.tx = MESHROUTE_NS::Node::TeamKeyGrantTx::queued; d.tx_ctr = 4242; d.tx_dst = 221;
    mrui::InviteGrantResult r{};
    CHECK(mrui::invite_grant_perform(&d, 0x00BEDEADu, r) == true);
    // ⛔ PIN 5, TERM BY TERM: a DIFFERENT dst does not promote; a DIFFERENT ctr does not promote.
    CHECK(mrui::invite_grant_apply_push(r, aired_push(90,  4242)) == false);
    CHECK(r.st == mrui::InviteGrantState::queued);
    CHECK(mrui::invite_grant_apply_push(r, aired_push(221, 4243)) == false);
    CHECK(r.st == mrui::InviteGrantState::queued);
    CHECK(mrui::invite_grant_apply_push(r, aired_push(0, 0)) == false);
    CHECK(r.st == mrui::InviteGrantState::queued);
    // ...and an unrelated KIND never promotes, whatever it carries.
    {
        MESHROUTE_NS::Push acked{};
        acked.kind = MESHROUTE_NS::PushKind::send_acked; acked.dst = 221; acked.ctr = 4242;
        CHECK(mrui::invite_grant_apply_push(r, acked) == false);
        MESHROUTE_NS::Push e2e{};
        e2e.kind = MESHROUTE_NS::PushKind::send_e2e_acked; e2e.dst = 221; e2e.ctr = 4242;
        CHECK(mrui::invite_grant_apply_push(r, e2e) == false);     // ⛔ there IS no e2e ack on a grant
        CHECK(r.st == mrui::InviteGrantState::queued);
    }
    // ★★★ THE CORRELATED EDGE, AND ONLY IT, SAYS `KEY SENT`.
    CHECK(mrui::invite_grant_correlates(r, aired_push(221, 4242)) == true);
    CHECK(mrui::invite_grant_apply_push(r, aired_push(221, 4242)) == true);
    CHECK(r.st == mrui::InviteGrantState::sent);
    CHECK(strcmp(mrui::invite_grant_word(r.st), "KEY SENT") == 0);         // S-22
    // ★★ TERMINAL: `send_aired` is documented as an UPGRADE OF A QUEUED STATE ONLY — a later failure for the same
    //    flight may ⛔ not rewrite a verdict the operator has already read.
    CHECK(mrui::invite_grant_apply_push(r, failed_push(221, 4242)) == false);
    CHECK(r.st == mrui::InviteGrantState::sent);

    // The failure half: a CORRELATED failure says `GRANT FAILED`, and it is terminal in the same way.
    InviteDeviceFake d2; d2.tx = MESHROUTE_NS::Node::TeamKeyGrantTx::queued; d2.tx_ctr = 77; d2.tx_dst = 12;
    mrui::InviteGrantResult f{};
    CHECK(mrui::invite_grant_perform(&d2, 0x00BEDEADu, f) == true);
    CHECK(mrui::invite_grant_apply_push(f, failed_push(13, 77)) == false);      // wrong dst
    CHECK(mrui::invite_grant_apply_push(f, failed_push(12, 78)) == false);      // wrong ctr
    CHECK(mrui::invite_grant_apply_push(f, failed_push(12, 77)) == true);
    CHECK(f.st == mrui::InviteGrantState::failed);
    CHECK(strcmp(mrui::invite_grant_word(f.st), "GRANT FAILED") == 0);          // S-23
    CHECK(mrui::invite_grant_apply_push(f, aired_push(12, 77)) == false);       // ⛔ no resurrection
    CHECK(f.st == mrui::InviteGrantState::failed);

    // ★★★★ THE PARKED STATE HAS NO HANDLE, SO ⛔ NOTHING MAY MATCH IT: `ctr == 0` is the value six unrelated
    //      operations put on a push, so correlating it would be a wildcard that promotes a foreign flight.
    InviteDeviceFake p; p.tx = MESHROUTE_NS::Node::TeamKeyGrantTx::parked; p.tx_ctr = 0;
    mrui::InviteGrantResult parked{};
    CHECK(mrui::invite_grant_perform(&p, 0x00BEDEADu, parked) == true);
    CHECK(parked.st == mrui::InviteGrantState::parked);
    CHECK(mrui::invite_grant_apply_push(parked, aired_push(0, 0)) == false);
    CHECK(parked.st == mrui::InviteGrantState::parked);
    // ...and neither may a REFUSAL be promoted by a push that happens to carry its (zeroed) handle.
    for (const MESHROUTE_NS::Node::TeamKeyGrantTx t : kAllTx) {
        if (t == MESHROUTE_NS::Node::TeamKeyGrantTx::queued) continue;
        InviteDeviceFake x; x.tx = t; x.tx_ctr = 0;
        mrui::InviteGrantResult ref{};
        CHECK(mrui::invite_grant_perform(&x, 0x00BEDEADu, ref) == true);
        const mrui::InviteGrantState before = ref.st;
        CHECK(mrui::invite_grant_apply_push(ref, aired_push(221, 0)) == false);
        CHECK(mrui::invite_grant_apply_push(ref, failed_push(221, 0)) == false);
        CHECK(ref.st == before);
    }
    // A state that has NOT acted at all is likewise unpromotable.
    mrui::InviteGrantResult none{};
    CHECK(mrui::invite_grant_apply_push(none, aired_push(0, 0)) == false);
    CHECK(none.st == mrui::InviteGrantState::none);
}

TEST_CASE("ui16-grant-idrows: the FULL hash is on the confirmation EVEN WHEN a name is cached (P-7c)") {
    const InviteMember live[] = { mem(221, 0x00BEDEADu, "Wolfgangetta"), mem(90, 0x006C2971u) };
    // ★★★ RULE 4: the name is an ADDED row; the hash row is unconditional and ⛔ never replaced by it.
    const mrui::InviteIdRows named = mrui::invite_id_rows(live, 2, 0x00BEDEADu);
    CHECK(strcmp(named.hash, "0x00BEDEAD") == 0);
    CHECK(strcmp(named.name, "Wolfgangetta") == 0);
    // A member with NO cached name: the same hash row, and a BLANK name row the renderer simply omits.
    const mrui::InviteIdRows bare = mrui::invite_id_rows(live, 2, 0x006C2971u);
    CHECK(strcmp(bare.hash, "0x006C2971") == 0);
    CHECK(bare.name[0] == '\0');
    // ⛔ A HASH THE LIST DOES NOT CARRY still draws its identity — the window may already be gone (the act ends it).
    const mrui::InviteIdRows gone = mrui::invite_id_rows(live, 2, 0x11223344u);
    CHECK(strcmp(gone.hash, "0x11223344") == 0);
    CHECK(gone.name[0] == '\0');
    const mrui::InviteIdRows nolist = mrui::invite_id_rows(nullptr, 0, 0x11223344u);
    CHECK(strcmp(nolist.hash, "0x11223344") == 0);
    CHECK(nolist.name[0] == '\0');
    // ★★ THE NAME IS FOUND **BY HASH** AND ⛔ NEVER THE OTHER WAY ROUND (P-7d): two members sharing one name are
    //    two identities, and the lookup key is the one that cannot be re-typed by its owner.
    const InviteMember twins[] = { mem(11, 0x0000AAAAu, "Sam"), mem(12, 0x0000BBBBu, "Sam") };
    CHECK(strcmp(mrui::invite_name_of(twins, 2, 0x0000AAAAu), "Sam") == 0);
    CHECK(strcmp(mrui::invite_name_of(twins, 2, 0x0000BBBBu), "Sam") == 0);
    CHECK(strcmp(mrui::invite_name_of(twins, 2, 0x0000CCCCu), "") == 0);
    CHECK(strcmp(mrui::invite_name_of(twins, 2, 0), "") == 0);           // ⛔ 0 is "no binding", never an identity
    CHECK(strcmp(mrui::invite_name_of(nullptr, 3, 0x0000AAAAu), "") == 0);
    // ⚠ THE ROW's SIX-COLUMN CLAMP IS THE **FORMAT's**, ⛔ not the buffer's: the confirmation carries the WHOLE
    //   cached name, so the two screens' truncations cannot silently become one.
    CHECK(strlen(named.name) == 12u);
}

TEST_CASE("ui16-grant-words: no arm prints a COMPLETION word, and every one fits the 19-column body") {
    // ⛔⛔⛔ **THE INVENTORY IS THE ENUM's, ⛔ NOT A LIST KEPT BESIDE IT** (QG round 2, 2026-08-24). ⚠ THE HISTORY
    //      IS THE ARGUMENT: this case used to walk a HAND-LISTED array checked against a HAND-TYPED count, and
    //      both were wrong at once — `queue_full` was missing from the array while the literal still matched, so
    //      the sweep called itself exhaustive having never applied the 19-column bound or the forbidden-word
    //      check to `GRANT QUEUE FULL`, the LONGEST word this screen owns. A hand-written pair cannot detect the
    //      thing it is written from.
    // ★★★★ SO THE COUPLING IS MECHANICAL NOW, ON TWO INDEPENDENT AXES, AND NEITHER IS A LITERAL:
    //      (1) the sweep walks `0 .. InviteGrantState::count - 1`, so a state added to the enum is visited
    //          BY CONSTRUCTION — there is no array to forget;
    //      (2) `invite_grant_word`'s switch has ⛔ no `default:`, so a state added and NOT worded is a
    //          **BUILD FAILURE** (-Werror=switch), not a green test.
    //      ⇒ adding a state without extending this case's coverage is impossible: it either compiles and is
    //      swept, or it does not compile.
    for (uint8_t v = 0; v < static_cast<uint8_t>(mrui::InviteGrantState::count); ++v) {
        const mrui::InviteGrantState s = static_cast<mrui::InviteGrantState>(v);
        const char* w = mrui::invite_grant_word(s);
        // ★ EVERY state has a word EXCEPT `none`, whose honest answer is the empty string — asserted in BOTH
        //   directions here, so a future state that forgets its word (or a `none` that grows a reassuring one)
        //   reddens on the spot rather than slipping through the length bound below.
        if (s == mrui::InviteGrantState::none) CHECK(w[0] == '\0');
        else                                   CHECK(w[0] != '\0');
        CHECK(strlen(w) <= 19u);
        // ⛔⛔ PIN 6 — THERE IS NO E2E ACK ON A GRANT, so ⛔ NO arm may claim one. `JOIN COMPLETE` (S-32) is the
        //     design's own named refusal; the other three are the shapes an author reaches for instead.
        CHECK(strstr(w, "JOIN COMPLETE") == nullptr);
        CHECK(strstr(w, "COMPLETE") == nullptr);
        CHECK(strstr(w, "RECEIVED") == nullptr);
        CHECK(strstr(w, "DELIVERED") == nullptr);
        CHECK(strstr(w, "KEYLESS") == nullptr);              // S-33
        CHECK(strstr(w, "WAITING FOR KEY") == nullptr);      // S-34
    }
    // ⛔ AND `KEY SENT` IS REACHABLE FROM EXACTLY ONE STATE — the one a correlated `send_aired` produces. Same
    //    enum-driven walk, so a new state that accidentally spells it is caught here too.
    for (uint8_t v = 0; v < static_cast<uint8_t>(mrui::InviteGrantState::count); ++v) {
        const mrui::InviteGrantState s = static_cast<mrui::InviteGrantState>(v);
        if (strcmp(mrui::invite_grant_word(s), "KEY SENT") == 0) CHECK(s == mrui::InviteGrantState::sent);
    }
    // ★★ AND THE TWO N6b WORDS ARE NAMED EXPLICITLY, because the sweep above proves shape, not identity.
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::queue_full), "GRANT QUEUE FULL") == 0);
    CHECK(strcmp(mrui::invite_grant_word(mrui::InviteGrantState::parked), "GRANT PARKED") == 0);
    // ⓘ THE SENTINEL IS NOT SWEPT AND MUST NOT BE: it is not a state. Pinned so it cannot quietly become one.
    CHECK(mrui::invite_grant_word(mrui::InviteGrantState::count)[0] == '\0');
    CHECK(static_cast<uint8_t>(mrui::InviteGrantState::count) >
          static_cast<uint8_t>(mrui::InviteGrantState::name_too_long));
}

TEST_CASE("ui16-grant-size: the verdict carrier costs 8 bytes, offsetof-proved") {
    // ⓘ HOST ABI ONLY. The BOARD half of these pins lives in `tools/probe_board_abi.py` ([[B246]] standing
    //   check): run it with `--struct <T>` when a slice quotes a struct SIZE here, and in full at the gate.
    //   ⛔ It measures `sizeof`/`alignof` per ABI ONLY — a RAM figure still needs an instance count, whose
    //   authority is the per-board `RAM_used` diff (D2), never a `sizeof` from either side.
    // ⚠ MEASURED, not reasoned (D2's standing warning about native alignment applies to the BOARD figure; what is
    //   pinned here is that no padding hole opens inside the carrier).
    CHECK(sizeof(mrui::InviteGrantResult) == 8u);
    CHECK(offsetof(mrui::InviteGrantResult, hash) == 0u);
    CHECK(offsetof(mrui::InviteGrantResult, ctr)  == 4u);
    CHECK(offsetof(mrui::InviteGrantResult, dst)  == 6u);
    CHECK(offsetof(mrui::InviteGrantResult, st)   == 7u);
}

// ========================================== PIN 12 — THE EQUIVALENCE CASE, OWED FROM THE N5 PREFLIGHT RULING
// ★★★★ THE OBLIGATION, VERBATIM (spec §4-N5's clarification blockquote): *"When N6 lands, one equivalence case
//      drives the preflight and the real grant against one fixture at `authoritative` and one notch below it, so
//      the two sites can never silently disagree."*
// ★★★ WHY IT MATTERS: `invite_grant_preflight` decides whether `GRANT KEY` is even OFFERED, and
//     `Node::team_key_grant_send`'s `no_pubkey` arm decides whether the private key may actually be sealed. If the
//     preflight's floor were LOWER, the panel would offer an act the core refuses (a dead button); if it were
//     HIGHER, the operator could never reach a grant the core would have performed. Both are silent, and only ONE
//     fixture driving BOTH sites at the SAME boundary can show they agree.
// ⓘ THE SEAM UNDER TEST IS THE PRODUCTION ONE: `RealGrantSeam::peer_key_at_least` is `DeviceInvite`'s body verbatim
//   (`src/firmware_ui.cpp`) and its `grant` is `mrfw::device_team_grant`'s. Nothing is scripted here.
namespace {
class GrantHal : public mrtest::TestHalBase {
public:
    uint8_t _fill = 0x31;                       // a non-degenerate CSPRNG stream: the team-key mint REFUSES zeros
    void rand_bytes(uint8_t* o, size_t n) override {
        for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(_fill + i);
    }
    void emit(const char*, const MESHROUTE_NS::EventField*, size_t) override {}
    // ★★★★ §UI-16 N6b (QG correction, 2026-08-24) — **THE RADIO IS COUNTED, so "aired" stops being a word this
    //      suite uses loosely.** The park arm fires an `emit_hash_query` H lookup AFTER `park_send` returns, and it
    //      does so UNCONDITIONALLY — including when the ring was full and the send was dropped. That is EXISTING,
    //      DELIBERATELY UNCHANGED behaviour (N6b moved no branch), but a case asserting *"nothing aired"* on the
    //      TX-queue depth alone was measuring the DM queue and claiming the AIR. ⇒ the transmissions are counted
    //      here, and the parked-ring case PINS what really leaves: ⛔ no grant DATA, and the H lookup that the
    //      unchanged arm still emits. ⓘ `TestHalBase::tx` is inert-and-discarding; this override only tallies.
    // ★★★ AND THE FRAME IS **CLASSIFIED, ⛔ NOT MERELY COUNTED** (QG round 2): "one transmission occurred" is a
    //     weaker claim than "the H query went out", and only the second one pins WHICH branch aired. The decode is
    //     the codec's OWN `parse_h` (U1 — ⛔ no second reader of the cmd nibble here): it returns `nullopt` for a
    //     wrong cmd, so a non-H frame cannot be counted as one.
    int tx_calls = 0;                  // every transmission, whatever it is
    int tx_h_queries = 0;              // ...of which: frames `parse_h` accepts as an H query
    uint32_t last_h_query_hash = 0;    // ...and the hash the last one was looking for
    MESHROUTE_NS::TxResult tx(const uint8_t* b, size_t n, const MESHROUTE_NS::TxParams& p) override {
        ++tx_calls;
        if (b && n) {
            if (const auto h = MESHROUTE_NS::parse_h(std::span<const uint8_t>(b, n))) {
                ++tx_h_queries;
                last_h_query_hash = h->query_hash();
            }
        }
        return mrtest::TestHalBase::tx(b, n, p);
    }
};
// The two production bodies, side by side, over ONE real node.
struct RealGrantSeam : mrui::IUiInviteDevice {
    MESHROUTE_NS::Node& n;
    explicit RealGrantSeam(MESHROUTE_NS::Node& node) : n(node) {}
    void request_team_announcement() override { n.schedule_triggered_beacon(); }
    bool peer_key_at_least(uint32_t key_hash32, MESHROUTE_NS::Node::PeerKeyConf floor) const override {
        uint8_t ed[32];
        MESHROUTE_NS::Node::PeerKeyConf conf = MESHROUTE_NS::Node::PeerKeyConf::overheard;
        return n.peer_key_find(key_hash32, ed, &conf) &&
               static_cast<uint8_t>(conf) >= static_cast<uint8_t>(floor);
    }
    mrui::UiInviteIssue issue(const MESHROUTE_NS::Command&) override { return mrui::UiInviteIssue{}; }
    MESHROUTE_NS::Node::TeamKeyGrantTx grant(uint32_t key_hash32, MESHROUTE_NS::Plane plane,
                                             uint16_t* out_ctr, uint8_t* out_dst) override {
        return n.team_key_grant_send(key_hash32, /*name=*/nullptr, /*name_len=*/0, plane, out_ctr, out_dst);
    }
};
}  // namespace

TEST_CASE("ui16-grant-equiv: ONE fixture — the PREFLIGHT and the REAL grant answer identically at the floor") {
    using PKC = MESHROUTE_NS::Node::PeerKeyConf;
    using TX  = MESHROUTE_NS::Node::TeamKeyGrantTx;
    uint8_t sa[32], sb[32];
    for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
    MESHROUTE_NS::Identity A{}, B{};
    MESHROUTE_NS::identity_from_seed(A, sa);
    MESHROUTE_NS::identity_from_seed(B, sb);

    GrantHal hal;
    MESHROUTE_NS::Node node(hal, /*id=*/2, A.key_hash32);
    MESHROUTE_NS::NodeConfig cfg;
    cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    cfg.is_mobile = true; cfg.team_id = 0xAAAAAAAAu;
    node.on_init(cfg);
    node.set_crypto_identity(A.x_secret, A.ed_pub);
    CHECK(node.team_channel_key_mint());                      // in a team, holding its key, with an identity
    // ★★ §UI-16 N6b — ADOPTED TEAM-LOCAL ID, ADDED DELIBERATELY: without one, `send_by_hash`'s TEAM arm has no
    //    ROUTABLE team origin, so it pushes `send_failed` and parks NOTHING. Before N6b that path answered
    //    `queued` with `ctr == 0` and the panel called it `GRANT PARKED` — a state the node was never in. The
    //    fixture adopts an id so case (3) below exercises the REAL park; the failing variant is its own case.
    node.set_team_local_id(40);
    RealGrantSeam seam(node);

    // ---- (1) NOTHING CACHED: both sites refuse, and the grant's own word is N5's landing.
    {
        uint16_t ctr = 0; uint8_t dst = 0;
        CHECK(mrui::invite_grant_preflight(&seam, B.key_hash32) == false);
        const TX tx = seam.grant(B.key_hash32, mrui::kInviteGrantPlane, &ctr, &dst);
        CHECK(tx == TX::no_pubkey);
        CHECK(mrui::invite_grant_state_of(tx) == mrui::InviteGrantState::no_pubkey);
    }
    // ---- (2) ★★★ ONE NOTCH BELOW THE FLOOR (`overheard` — a key learned on the air, i.e. spoofable): the
    //          preflight REFUSES, and the real grant refuses with the SAME reason. ⛔ Both, or neither.
    {
        node.peer_key_set(B.key_hash32, B.ed_pub, PKC::overheard);
        uint16_t ctr = 0; uint8_t dst = 0;
        CHECK(seam.peer_key_at_least(B.key_hash32, PKC::overheard) == true);   // the key IS there...
        CHECK(mrui::invite_grant_preflight(&seam, B.key_hash32) == false);     // ...and is STILL not good enough
        CHECK(seam.grant(B.key_hash32, mrui::kInviteGrantPlane, &ctr, &dst) == TX::no_pubkey);
    }
    // ---- (3) ★★★ AT THE FLOOR (`authoritative`): the preflight ENABLES, and the real grant ⛔ does NOT refuse
    //          for want of a pubkey. The two sites move together, which is the whole obligation.
    {
        node.peer_key_set(B.key_hash32, B.ed_pub, PKC::authoritative);
        uint16_t ctr = 0; uint8_t dst = 0;
        CHECK(mrui::invite_grant_preflight(&seam, B.key_hash32) == true);
        const TX tx = seam.grant(B.key_hash32, mrui::kInviteGrantPlane, &ctr, &dst);
        CHECK(tx != TX::no_pubkey);
        // ★★★★ §UI-16 N6b — THE CORE NAMES THE OUTCOME AND THE PANEL REPEATS IT. This fixture has no team-key
        //      binding for the target, so `send_by_hash`'s TEAM arm PARKS the send behind an H resolve and says
        //      **`parked`** — ⛔ it no longer answers `queued` and leaves the screen to guess from a zero handle.
        CHECK(tx == TX::parked);
        CHECK(ctr == 0);                                      // ⛔ a parked send has NO flight and therefore no handle
        CHECK(dst == 0);                                      // ⛔ ...and no resolved destination either
        CHECK(mrui::invite_grant_state_of(tx) == mrui::InviteGrantState::parked);
        CHECK(mrui::invite_grant_state_of(tx) != mrui::InviteGrantState::sent);
    }
    // ---- (4) AND ABOVE THE FLOOR (`pinned`, the QR ceremony): both sites agree there too.
    {
        node.peer_key_set(B.key_hash32, B.ed_pub, PKC::pinned);
        uint16_t ctr = 0; uint8_t dst = 0;
        CHECK(mrui::invite_grant_preflight(&seam, B.key_hash32) == true);
        CHECK(seam.grant(B.key_hash32, mrui::kInviteGrantPlane, &ctr, &dst) != TX::no_pubkey);
    }
    // ---- (5) THE FLOOR ITSELF IS THE GRANT'S OWN, stated as a value rather than as a comment.
    CHECK(static_cast<uint8_t>(PKC::authoritative) > static_cast<uint8_t>(PKC::overheard));
    CHECK(static_cast<uint8_t>(PKC::pinned) >= static_cast<uint8_t>(PKC::authoritative));
}

// ================================================ §UI-16 N6b — THE THREE DISPATCH FACTS, AGAINST THE **REAL CORE**
// ★★★★ WHY THESE THREE AND WHY NOT AGAINST A FAKE. Each one is a state in which the pre-correction seam returned
//      `queued` and the panel then read the COUNTER to decide what to say — so a scripted fake could not have found
//      them: the defect was in what the core reported, not in what the mapper did with it. ⇒ one real `Node` per
//      case, driven to the exact edge, and the PANEL's word computed from what that node really answered.
// ⓘ The fixture is `ui16-grant-equiv`'s, reused verbatim in a helper (U1): in a team, keyed, an identity, an
//   adopted team-local id, and B's pubkey at the grant's own AUTHORITATIVE floor.
namespace {
struct GrantFixture {
    GrantHal hal;
    MESHROUTE_NS::Identity A{}, B{};
    MESHROUTE_NS::Node* node = nullptr;
    // `adopt_id = false` leaves the node WITHOUT a routable team origin, which is the shape that reaches
    // `send_by_hash`'s loud TEAM refusal — see `ui16-grant-noroute`.
    explicit GrantFixture(bool adopt_id = true) {
        uint8_t sa[32], sb[32];
        for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
        MESHROUTE_NS::identity_from_seed(A, sa);
        MESHROUTE_NS::identity_from_seed(B, sb);
        node = new MESHROUTE_NS::Node(hal, /*id=*/2, A.key_hash32);
        MESHROUTE_NS::NodeConfig cfg;
        cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
        cfg.is_mobile = true; cfg.team_id = 0xAAAAAAAAu;
        node->on_init(cfg);
        node->set_crypto_identity(A.x_secret, A.ed_pub);
        node->team_channel_key_mint();
        if (adopt_id) node->set_team_local_id(40);
        node->peer_key_set(B.key_hash32, B.ed_pub, MESHROUTE_NS::Node::PeerKeyConf::authoritative);
    }
    ~GrantFixture() { delete node; }
    // Bind B's hash to a TEAM-plane local id, so `send_by_hash`'s TEAM arm RESOLVES instead of parking.
    void bind(uint8_t team_id, uint64_t at_ms) {
        hal._now = at_ms;
        node->test_learn_route(team_id, team_id, 1, 40, /*team_plane=*/true);
        node->team_key_set(team_id, B.key_hash32, MESHROUTE_NS::Node::IdBindSource::bcn,
                           MESHROUTE_NS::Node::IdBindConf::authoritative);
    }
};
}  // namespace

TEST_CASE("ui16-grant-queuefull: a FULL TX QUEUE is an ADMISSION REFUSAL — ⛔ not GRANT QUEUED, no grant DATA queued") {
    // ★★★★ THE MEASURED DEFECT (QG, 2026-08-24): `enqueue_data` mints the counter ABOVE every bail and then drops
    //      the frame when `_tx_queue_n == kTxQueueCap`, so the pre-correction seam answered `queued` with a
    //      perfectly ordinary NON-ZERO handle for a frame that had already been thrown away. The operator read
    //      `GRANT QUEUED`, waited for a `send_aired` that could never be minted, and the private key never left.
    GrantFixture f;
    RealGrantSeam seam(*f.node);
    f.bind(/*team_id=*/86, /*at_ms=*/100000);
    f.node->test_suspend_tx_drain(true);            // hold every frame IN the queue — this is what fills it
    // Fill the queue to its cap with REAL grants: each one is admitted and each one says so.
    const uint8_t cap = f.node->test_tx_queue_n() == 0 ? 8 : 0;
    CHECK(cap == 8);                                 // kTxQueueCap, and the queue starts empty
    for (uint8_t i = 0; i < cap; ++i) {
        mrui::InviteGrantResult r{};
        CHECK(mrui::invite_grant_perform(&seam, f.B.key_hash32, r) == true);
        CHECK(r.st  == mrui::InviteGrantState::queued);
        CHECK(r.ctr != 0);                           // a real flight, with a real handle
        CHECK(r.dst == 86);                          // ...addressed to the id the core RESOLVED
        CHECK(strcmp(mrui::invite_grant_word(r.st), "GRANT QUEUED") == 0);
    }
    CHECK(f.node->test_tx_queue_n() == 8);
    // ★★★★ THE ONE MORE THAT DOES NOT FIT. ⛔ It is NOT `GRANT QUEUED` (no grant DATA was queued), ⛔ NOT
    //      `GRANT PARKED` (nothing was stored) and ⛔ NOT `GRANT FAILED` (no flight exists to fail) — it is S-38.
    mrui::InviteGrantResult full{};
    CHECK(mrui::invite_grant_perform(&seam, f.B.key_hash32, full) == true);
    CHECK(full.st == mrui::InviteGrantState::queue_full);
    CHECK(strcmp(mrui::invite_grant_word(full.st), "GRANT QUEUE FULL") == 0);
    CHECK(strcmp(mrui::invite_grant_word(full.st), "GRANT QUEUED") != 0);
    CHECK(strcmp(mrui::invite_grant_word(full.st), "GRANT FAILED") != 0);
    // ⛔ **NO GRANT DATA WAS QUEUED OR STORED** — the queue is still exactly at its cap, i.e. the refused frame
    //    never became a TxItem. ⚠ THE CLAIM IS SCOPED TO THE GRANT DATA ON PURPOSE (QG correction, 2026-08-24):
    //    "nothing aired" would be a claim about the RADIO, and this depth reading cannot make it. ⓘ On THIS arm
    //    the send resolves and never reaches the park, so no H lookup is fired either — the arm that does fire one
    //    is the parked-ring refusal, and it has its own measured case (`ui16-grant-parkfull-air`).
    CHECK(f.node->test_tx_queue_n() == 8);
    // ⛔ AND NO HANDLE IS OFFERED FOR A FLIGHT THAT DOES NOT EXIST, so no push can ever promote this verdict.
    CHECK(full.ctr == 0);
    CHECK(full.dst == 0);
    CHECK(mrui::invite_grant_correlates(full, aired_push(86, 1)) == false);
    CHECK(mrui::invite_grant_apply_push(full, aired_push(0, 0)) == false);
    CHECK(full.st == mrui::InviteGrantState::queue_full);
}

TEST_CASE("ui16-grant-parkfull: a FULL PARKED RING is the same refusal — ⛔ not GRANT PARKED, and nothing is stored") {
    // ★★★★ THE OTHER HALF OF THE MEASURED DEFECT: `park_send` returns without storing when the ring is at
    //      `cap_parked_sends`, and the arm above it returned 0 either way — so "no handle" was read as "parked"
    //      whether the send had been stored or dropped. `GRANT PARKED` (S-37) may now be shown ONLY for the
    //      EXPLICITLY-STORED outcome, which is exactly what this case separates.
    GrantFixture f;
    RealGrantSeam seam(*f.node);
    // ⛔ NO binding for B: the TEAM arm cannot resolve, so every send below PARKS behind an H resolve.
    for (uint8_t i = 0; i < 8; ++i) {               // protocol::cap_parked_sends
        mrui::InviteGrantResult r{};
        CHECK(mrui::invite_grant_perform(&seam, f.B.key_hash32, r) == true);
        CHECK(r.st == mrui::InviteGrantState::parked);
        CHECK(strcmp(mrui::invite_grant_word(r.st), "GRANT PARKED") == 0);
        CHECK(r.ctr == 0);                          // ⛔ a park has no flight and therefore no handle
    }
    // ★★★★ THE NINTH. The ring is full, `park_send` stores NOTHING — and the panel must not say it parked.
    mrui::InviteGrantResult full{};
    CHECK(mrui::invite_grant_perform(&seam, f.B.key_hash32, full) == true);
    CHECK(full.st == mrui::InviteGrantState::queue_full);
    CHECK(full.st != mrui::InviteGrantState::parked);
    CHECK(strcmp(mrui::invite_grant_word(full.st), "GRANT QUEUE FULL") == 0);
    CHECK(strcmp(mrui::invite_grant_word(full.st), "GRANT PARKED") != 0);
    // ⛔⛔ **NO GRANT DATA WAS STORED OR QUEUED** — and that is the exact claim, ⛔ not "nothing aired" (QG
    //     correction, 2026-08-24). The TX queue depth answers ONLY the DM question: it is 0 because the sealed
    //     TEAM_KEY_GRANT was never built into a TxItem, which is what the refusal means.
    CHECK(f.node->test_tx_queue_n() == 0);
}

TEST_CASE("ui16-grant-parkfull-air: what a REFUSED park really puts on the air, MEASURED and PINNED") {
    // ★★★★ THE HONEST SECOND HALF OF `ui16-grant-parkfull`, AND IT EXISTS BECAUSE THE FIRST HALF OVERCLAIMED.
    //      `send_by_hash`'s TEAM park arm calls `park_send` and then `emit_hash_query` — and the H lookup is
    //      fired **UNCONDITIONALLY**, i.e. also when the ring was full and the send was DROPPED. ⛔ That is
    //      PRE-EXISTING behaviour that §UI-16 N6b deliberately did NOT move (C1: reporting a fact may not
    //      relocate a branch), so the correct response is to MEASURE it, ⛔ never to word around it.
    // ⇒ what this case pins: a refused park stores no grant DATA, and the transmissions that DO occur are the
    //   unchanged H lookups — so a future slice that makes the H conditional will redden here and be forced to
    //   say so, rather than silently changing what the radio does.
    GrantFixture f;
    RealGrantSeam seam(*f.node);
    for (uint8_t i = 0; i < 8; ++i) {                       // protocol::cap_parked_sends — fill the ring
        mrui::InviteGrantResult r{};
        CHECK(mrui::invite_grant_perform(&seam, f.B.key_hash32, r) == true);
        CHECK(r.st == mrui::InviteGrantState::parked);
    }
    const int tx_before = f.hal.tx_calls;                   // MEASURED, ⛔ not assumed
    const int h_before  = f.hal.tx_h_queries;
    mrui::InviteGrantResult full{};
    CHECK(mrui::invite_grant_perform(&seam, f.B.key_hash32, full) == true);
    CHECK(full.st == mrui::InviteGrantState::queue_full);
    const int aired_by_the_refusal = f.hal.tx_calls - tx_before;
    const int h_by_the_refusal     = f.hal.tx_h_queries - h_before;
    // ⛔ NO GRANT DATA: the DM queue never received a TxItem for the refused send.
    CHECK(f.node->test_tx_queue_n() == 0);
    // ★★★ AND THE PIN, STATED AS THE MEASUREMENT IT IS: the refusal is ⛔ NOT radio-silent — the arm's own
    //     `emit_hash_query` still runs. ⓘ EXACTLY ONE frame per attempt (the flood originates once); ⛔ zero
    //     would mean the H had been made conditional, and more than one would mean the refusal had grown a
    //     retry. Both are behaviour changes this slice forbids itself.
    CHECK(aired_by_the_refusal == 1);
    // ★★★★ ...AND IT IS AN **H QUERY**, DECODED — ⛔ not "a transmission from a branch we reasoned about". The
    //      frame `parse_h` accepted is looking for EXACTLY the hash the grant could not resolve, which is what
    //      makes it this send's locate rather than some unrelated flood.
    CHECK(h_by_the_refusal == 1);
    CHECK(aired_by_the_refusal == h_by_the_refusal);          // ⛔ nothing ELSE aired: every frame is that H
    CHECK(f.hal.last_h_query_hash == f.B.key_hash32);
    // ⓘ ...and the CONTROL that keeps those numbers meaningful: a SUCCESSFUL park airs the same one H for the
    //   same hash, so the refusal is INDISTINGUISHABLE on the air — which is precisely why the PANEL's word had
    //   to come from the dispatch result instead of from anything observable downstream.
    GrantFixture g;
    RealGrantSeam gs(*g.node);
    const int g_before   = g.hal.tx_calls;
    const int g_h_before = g.hal.tx_h_queries;
    mrui::InviteGrantResult ok{};
    CHECK(mrui::invite_grant_perform(&gs, g.B.key_hash32, ok) == true);
    CHECK(ok.st == mrui::InviteGrantState::parked);
    CHECK(g.hal.tx_calls - g_before == aired_by_the_refusal);
    CHECK(g.hal.tx_h_queries - g_h_before == h_by_the_refusal);
    CHECK(g.hal.last_h_query_hash == g.B.key_hash32);
}

TEST_CASE("ui16-grant-redad: a re-DAD between selection and send — the grant airs to the NEW id and PROMOTES") {
    // ★★★★ QG BLOCKER 2, AS A SCENARIO. The window freezes the row's team-local id when the confirmation opens;
    //      the core resolves the hash against the binding that is live AT SEND TIME. A member that re-runs team-DAD
    //      in between is granted on its NEW id — and a panel correlating on the FROZEN one waits for a push that
    //      will never carry it. The verdict now takes both terms from the core, so the TxDone edge lands.
    GrantFixture f;
    RealGrantSeam seam(*f.node);
    f.bind(/*team_id=*/86, /*at_ms=*/100000);       // what the roster said when the row was drawn
    const uint8_t frozen = 86;                       // ...and what the UI would have frozen with it
    f.bind(/*team_id=*/77, /*at_ms=*/200000);       // ★ the RE-DAD: same hash, newer authoritative id
    f.node->test_suspend_tx_drain(true);
    mrui::InviteGrantResult r{};
    CHECK(mrui::invite_grant_perform(&seam, f.B.key_hash32, r) == true);
    CHECK(r.st == mrui::InviteGrantState::queued);
    CHECK(r.ctr != 0);
    // ★★★ THE SEND WENT TO THE **NEW** ID, and the verdict carries that — ⛔ not the frozen one.
    CHECK(r.dst == 77);
    CHECK(r.dst != frozen);
    CHECK(f.node->test_tx_queue_n() == 1);
    CHECK(f.node->test_tx_dst(0) == 77);             // ...measured on the frame itself, not only on the report
    // ⛔ THE FROZEN ID DOES NOT CORRELATE — which is precisely why storing it left the screen stuck for ever.
    CHECK(mrui::invite_grant_apply_push(r, aired_push(frozen, r.ctr)) == false);
    CHECK(r.st == mrui::InviteGrantState::queued);
    // ★★★★ AND THE REAL TxDone EDGE — the one `push_send_aired_if_owned` builds from the flight's own `pt.dst` —
    //      PROMOTES IT TO `KEY SENT`. This is the blocker's scenario, green.
    CHECK(mrui::invite_grant_apply_push(r, aired_push(77, r.ctr)) == true);
    CHECK(r.st == mrui::InviteGrantState::sent);
    CHECK(strcmp(mrui::invite_grant_word(r.st), "KEY SENT") == 0);
}

TEST_CASE("ui16-grant-noroute: a send that reaches NO admission point says GRANT FAILED — ⛔ never GRANT PARKED") {
    // ★★★★ THE THIRD FACE OF THE SAME DEFECT, AND IT IS THE ONE THAT LOOKED HARMLESS. With no adopted team-local
    //      id there is no ROUTABLE team origin, so `send_by_hash`'s TEAM arm refuses LOUD: it pushes
    //      `send_failed{mobile_no_home}`, parks NOTHING and returns 0. The pre-correction seam answered `queued`
    //      with a zero handle — and the panel, splitting on that handle, said **`GRANT PARKED`**: a state the node
    //      was never in, for a send that had already failed, on the screen that ships a private key.
    GrantFixture f(/*adopt_id=*/false);
    RealGrantSeam seam(*f.node);
    CHECK(f.node->team_local_id() == 0);                     // the precondition, stated rather than assumed
    uint16_t ctr = 0; uint8_t dst = 0;
    const MESHROUTE_NS::Node::TeamKeyGrantTx tx =
        seam.grant(f.B.key_hash32, mrui::kInviteGrantPlane, &ctr, &dst);
    CHECK(tx == MESHROUTE_NS::Node::TeamKeyGrantTx::send_failed);
    CHECK(tx != MESHROUTE_NS::Node::TeamKeyGrantTx::queued);
    CHECK(tx != MESHROUTE_NS::Node::TeamKeyGrantTx::parked);
    CHECK(ctr == 0);
    CHECK(dst == 0);
    // ⓘ ONE FACT, ONE WORD: this IS the failure `GRANT FAILED` names (S-23) — it arrives synchronously instead of
    //   as a push, and the core has ALREADY pushed `send_failed` for it. ⛔ No new lexeme is invented for it.
    CHECK(mrui::invite_grant_state_of(tx) == mrui::InviteGrantState::failed);
    CHECK(strcmp(mrui::invite_grant_word(mrui::invite_grant_state_of(tx)), "GRANT FAILED") == 0);
    CHECK(strcmp(mrui::invite_grant_word(mrui::invite_grant_state_of(tx)), "GRANT PARKED") != 0);
    CHECK(strcmp(mrui::invite_grant_word(mrui::invite_grant_state_of(tx)), "GRANT QUEUED") != 0);
    // ⛔ AND NOTHING WAS SENT OR STORED — the refusal is a refusal on every axis.
    CHECK(f.node->test_tx_queue_n() == 0);
    // ★ THE CORE REALLY REPORTED IT: a `send_failed` push is waiting, which is why the word is not an invention.
    bool pushed_fail = false;
    MESHROUTE_NS::Push pu{};
    while (f.node->next_push(pu))
        if (pu.kind == MESHROUTE_NS::PushKind::send_failed) pushed_fail = true;
    CHECK(pushed_fail);
    // ...and the verdict carrier built from it is terminal: ⛔ no push may promote a grant that never flew.
    mrui::InviteGrantResult r{};
    CHECK(mrui::invite_grant_perform(&seam, f.B.key_hash32, r) == true);
    CHECK(r.st == mrui::InviteGrantState::failed);
    CHECK(mrui::invite_grant_apply_push(r, aired_push(0, 0)) == false);
    CHECK(r.st == mrui::InviteGrantState::failed);
}
