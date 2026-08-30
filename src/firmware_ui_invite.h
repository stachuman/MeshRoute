// MeshRoute — src/firmware_ui_invite.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-16 N4/N5 (spec docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md §4-N4/N5) — THE
// `INVITE MEMBER` WINDOW'S PURE UNIT: the bounded window's duration, the **TWO** snapshot authorities and the
// diff over them, the VOLATILE per-window handled set, the candidate row (name · team-local id · member
// fingerprint), the explicit pubkey-request intent and every lexeme the screen can print.
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
//   N4 shows candidates and REJECTs them; N5 added the EXPLICIT `REQUEST PUBKEY` ceremony and the side-effect-free
//   preflight that reaches either `NEED PUBKEY` or the grant-ready confirmation; ✅ **N6 (2026-08-24) LANDS THE
//   GRANT ITSELF** — the one forward to `Node::team_key_grant_send` on `Plane::TEAM`, the EIGHT-arm outcome mapping
//   and the `{dst, ctr}` `send_aired` correlation (the bottom block of this file).
//   ⛔ WHAT IS STILL NOT HERE, by scope: the RECEIVING half. This node never learns that its grant was READ — there
//   is no e2e ack on a DATA_TYPE_TEAM_KEY_GRANT — and the joiner's own durable `TEAM KEY RECEIVED` note is K3/K4's.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>                  // snprintf — §B115: the row is composed HERE so the suite reads the BYTES
#include "node.h"                 // the grant's own PeerKeyConf floor + existing Command carrier (U1/U2)

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

// ====================================================================== N5's pure device boundary and decisions
// ★★★ THE PREFLIGHT IS THE GRANT'S OWN BAR, NOT A SECOND AVAILABILITY RULE. The device forward below calls the
//     existing `Node::peer_key_find` and discards its public-key out-buffer; this pure helper supplies the SAME
//     floor as `Node::team_key_grant_send`'s `no_pubkey` arm (`lib/core/node.cpp`, §team-ch-key T-K3). If that arm
//     moves, this floor moves with it. Only this boolean reaches the model.
// ⓘ The forward itself is `const`: asking whether a usable cache entry exists neither requests nor refreshes one.
//    `peer_key_find` also performs the grant's TTL-age check, so an expired entry is refused here exactly as there.
// ★★★★ THE FORWARD's ANSWER — **THE EXECUTOR's OWN TWO FACTS, ⛔ NEVER A VERDICT** (QG blocker, 2026-08-24). The
//      first cut returned `void`, so the model entered `WAITING FOR PUBKEY` unconditionally: with no attached seam,
//      or against a SYNCHRONOUS refusal (`err_no_identity`, a full TX queue), the panel claimed to be waiting for an
//      answer to a request that was never made. A screen may only claim what happened.
// ⚠⚠ `ok` IS ⛔ NOT REDUNDANT WITH `code`, AND THIS IS THE TRAP THAT MAKES IT LOAD-BEARING: `mrfw::ExecResult`'s
//    `result` is *"valid only when `ok`"*, and `MESHROUTE_NS::CmdResult::code`'s own default is **`queued`**
//    (`lib/core/command.h`) ⇒ a line that never parsed carries `queued` in a default-constructed result. A decision
//    that reads `code` alone therefore treats a PARSE FAILURE as a successfully started workflow. Both terms, always.
// ⓘ `accepted` is carried because the forward reports what it was given (⛔ the device TU may sanitise nothing), and
//   it is DELIBERATELY NOT IN THE DECISION — see `invite_request_started`.
struct UiInviteIssue {
    bool                  ok       = false;                          // false => nothing parsed / nothing executed
    MESHROUTE_NS::CmdCode code     = MESHROUTE_NS::CmdCode::queued;   // ⚠ MEANINGLESS unless `ok` — see above
    bool                  accepted = false;                          // the TX path took a frame; ⛔ not the gate
};

struct IUiInviteDevice {
    virtual ~IUiInviteDevice() = default;
    // ★★★★ [[B249]] — request the EXISTING core-owned triggered team announcement. The pure model owns the
    //      once-per-fresh-open decision and the ordering relative to its snapshot; the device adapter owns only
    //      this one forward. The core remains the authority for team/mobile eligibility, jitter, coalescing and
    //      the minimum announcement interval, so there is deliberately no UI-side result or retry state.
    virtual void request_team_announcement() = 0;
    virtual bool peer_key_at_least(uint32_t key_hash32, MESHROUTE_NS::Node::PeerKeyConf floor) const = 0;
    virtual UiInviteIssue issue(const MESHROUTE_NS::Command& command) = 0;
    // ★★★★ §UI-16 N6 — THE ONE GRANT FORWARD, AND IT IS `Node::team_key_grant_send`'s OWN SIGNATURE minus the two
    //      arguments THIS SCREEN FIXES (U2 — the carrier is not rebuilt, the verb is not re-implemented and no
    //      second send path is opened): the optional TEAM `name=` is never sent (F-3 — this firmware stores no team
    //      label, and inventing one here would be an unruled string), and the PLANE is supplied by the pure caller
    //      below rather than chosen in the device TU.
    // ⛔ THERE IS NO DEFAULT ANSWER, deliberately: it is pure-virtual and returns the ELEVEN-valued outcome BY VALUE,
    //    so an implementer cannot leave it unset — and `TeamKeyGrantTx::queued` is the enum's ZERO, i.e. a
    //    default-constructed carrier would have claimed a successful send (the `CmdResult::code` trap `UiInviteIssue`
    //    documents above, one screen earlier, wearing different clothes).
    // ★★★★ §UI-16 N6b (2026-08-24) — **BOTH CORRELATION TERMS COME BACK FROM THE CORE NOW, AND THAT IS THE FIX.**
    //      `out_ctr` is the origination handle of the flight the core really created, and `out_dst` is the id it
    //      RESOLVED AT SEND TIME. ⛔ The screen may not substitute the id it froze when the row was selected:
    //      `Node::send_by_hash` re-resolves the hash against the CURRENT binding, so a team-DAD between the
    //      selection and the press lands the grant on a DIFFERENT id — and a `send_aired` carrying that id would
    //      never have matched, leaving the panel at `GRANT QUEUED` for ever (QG blocker 2, 2026-08-24).
    // ⓘ BOTH ARE ZERO UNLESS THE OUTCOME IS `queued`, by the core's own contract: no other outcome has a flight.
    virtual MESHROUTE_NS::Node::TeamKeyGrantTx grant(uint32_t key_hash32, MESHROUTE_NS::Plane plane,
                                                     uint16_t* out_ctr, uint8_t* out_dst) = 0;
};

inline bool invite_grant_preflight(const IUiInviteDevice* dev, uint32_t key_hash32) {
    return dev && key_hash32 != 0 &&
           dev->peer_key_at_least(key_hash32, MESHROUTE_NS::Node::PeerKeyConf::authoritative);
}

// ★★★ THE OPERATOR-AUTHORISED REQUEST, AS THE EXISTING TYPED COMMAND CARRIER (U2). Full hash in, full hash out;
//     no display name is accepted by this function, so a mutable label cannot become the request target (P-7d).
//     TEAM is explicit — AUTO/GLOBAL would ask a different address plane and spend airtime on the wrong network.
inline MESHROUTE_NS::Command invite_reqpubkey_command(uint32_t key_hash32) {
    MESHROUTE_NS::Command cmd{};
    cmd.kind = MESHROUTE_NS::CmdKind::reqpubkey;
    cmd.u.resolve.dst_hash = key_hash32;
    cmd.u.resolve.dst_id   = 0;
    cmd.u.resolve.hard     = false;
    cmd.u.resolve.plane    = static_cast<uint8_t>(MESHROUTE_NS::Plane::TEAM);
    return cmd;
}

// ★★★ WHAT "THE REQUEST IS UNDER WAY" MEANS, IN ONE PLACE. It is a SUCCESSFULLY STARTED OR LOCALLY COMPLETED
//     workflow — the line parsed AND the command was admitted — and ⛔ nothing stronger. (⛔ Not "accepted":
//     `CmdResult::accepted` is a narrower, precise term — a frame handed to TX — and this predicate deliberately
//     spans both its values; QG wording correction 2026-08-24.)
// ⛔⛔ IT DELIBERATELY DOES **NOT** REQUIRE `accepted`, AND THAT IS A REAL PATH RATHER THAN LAXITY: `reqpubkey` has
//     one accepted outcome that legitimately hands the TX path nothing at all — the branch that answers from the
//     LOCAL key cache and reports through the `peer_key_cached` push (`lib/core/command.h`'s own note on the field).
//     It returns `queued` with `accepted == false`, and the push that completes the wait may already be queued. A
//     gate on `accepted` would strand exactly the operator whose request had ALREADY succeeded, at `NEED PUBKEY`,
//     while the answer sat waiting — so the strictest-looking reading is the wrong one.
inline bool invite_request_started(const UiInviteIssue& r) {
    return r.ok && r.code == MESHROUTE_NS::CmdCode::queued;
}

// ★★★ THE ONE CALL THE MODEL MAKES: build the typed command, hand it to the forward, and answer the ONLY question a
//     screen may act on. ⛔ A null seam and a zero hash are refused BEFORE the forward — an unattached model (a
//     `!MR_FEAT_OLED`-shaped build, a partially-wired probe) must fail CLOSED exactly as `invite_grant_preflight`
//     does one screen earlier, ⛔ never by spending a call and ⛔ never by claiming a request it could not make.
inline bool invite_issue_reqpubkey(IUiInviteDevice* dev, uint32_t key_hash32) {
    if (!dev || key_hash32 == 0) return false;
    return invite_request_started(dev->issue(invite_reqpubkey_command(key_hash32)));
}

inline constexpr std::size_t kInviteReqpubkeyLineCap = 24;   // `reqpubkey 0x12345678 -t` + NUL
inline std::size_t ui_fmt_invite_reqpubkey_line(char* out, std::size_t cap,
                                                const MESHROUTE_NS::Command& cmd) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (cmd.kind != MESHROUTE_NS::CmdKind::reqpubkey || cmd.u.resolve.dst_hash == 0 ||
        cmd.u.resolve.dst_id != 0 || cmd.u.resolve.hard ||
        cmd.u.resolve.plane != static_cast<uint8_t>(MESHROUTE_NS::Plane::TEAM)) return 0;
    const int n = snprintf(out, cap, "reqpubkey 0x%08lX -t", (unsigned long)cmd.u.resolve.dst_hash);
    return (n > 0 && static_cast<std::size_t>(n) < cap) ? static_cast<std::size_t>(n) : 0;
}

// ★ BOTH TERMS ARE LOAD-BEARING: the kind prevents an unrelated push from completing the wait, and the FULL hash
//   prevents one peer's key/name arrival from enabling or repainting another candidate's ceremony.
inline bool invite_peer_key_cached_matches(const MESHROUTE_NS::Push& pu, uint32_t key_hash32) {
    return pu.kind == MESHROUTE_NS::PushKind::peer_key_cached && pu.sender_hash == key_hash32;
}

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
    //    row and the caller must do nothing rather than being handed a plausible one. A candidate's next screen
    //    freezes this identity before it can offer the BACK-default request or the REJECT-default ready pair.
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

// ★★★ THE CACHED NAME **FOR ONE FROZEN IDENTITY**, LOOKED UP IN THE **LIVE** LIST (F-15 rule 3 / P-7d). It is a
//     RENDER INPUT and nothing else: it is keyed BY the hash, so a name can never be the thing that finds a member,
//     and `""` — the honest blank — is the answer for a hash the list does not carry or does not name.
// ⓘ IT IS LOOKED UP LIVE RATHER THAN FROZEN WITH THE SELECTION, deliberately (P-7d's own pin): a member whose name
//   changes between the row and the confirmation is still granted THE SAME KEY, and the only way that pin can be
//   DRIVEN is if the screen really does re-read the name while the act really does not.
// ⛔ `key_hash32 == 0` finds nothing: 0 means "no authoritative binding", never an identity (F-7).
inline const char* invite_name_of(const InviteMember* mem, uint8_t n, uint32_t key_hash32) {
    if (!mem || key_hash32 == 0) return "";
    if (n > kMaxInviteRows) n = kMaxInviteRows;
    for (uint8_t i = 0; i < n; ++i)
        if (mem[i].key_hash32 == key_hash32) return mem[i].name;
    return "";
}

// ★★★★ THE CONFIRMATION'S IDENTITY ROWS, DECIDED **HERE** AND NOT IN THE RENDERER (§B115): what a screen puts on
//      the panel at the moment a PRIVATE KEY is shipped is a ruled decision (P-7c), and `src/firmware_ui.cpp` is
//      compiled by neither the native suite nor the simulator — so a renderer-side `if (name) … else …` would be a
//      rule no gate in this tree can attack. The renderer draws these two strings and makes no choice.
// ★★★ THE HASH ROW IS UNCONDITIONAL — ⛔ **EVEN WHEN A NAME IS SHOWN** (spec §3 P-7c, F-15 rule 4): a mutable,
//     self-asserted label may never be the ONLY identity an operator reads before an irreversible act. The name is
//     an ADDED row, exactly as it is an ADDED column on the list (rule 2); ⛔ it never replaces anything.
struct InviteIdRows {
    char hash[kMemberHashCap] = {};    // ALWAYS the full `0x%08lX`
    char name[kInviteNameCap] = {};    // the cached name, "" when none — the row the renderer simply omits
};
inline InviteIdRows invite_id_rows(const InviteMember* mem, uint8_t n, uint32_t key_hash32) {
    InviteIdRows r{};
    ui_fmt_member_hash_full(r.hash, sizeof r.hash, key_hash32);
    const char* nm = invite_name_of(mem, n, key_hash32);
    for (std::size_t i = 0; nm[i] && i + 1 < sizeof r.name; ++i) r.name[i] = nm[i];
    return r;
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
inline constexpr const char* kInviteNeedPubkey    = "NEED PUBKEY";          // S-18
inline constexpr const char* kInviteRequestPubkey = "REQUEST PUBKEY";       // S-19
inline constexpr const char* kInviteWaitingPubkey = "WAITING FOR PUBKEY";   // S-20 — ⛔ never WAITING FOR KEY
// ⛔ CORRECTED IN PLACE 2026-08-24 (§UI-16 N6, V1): this line cited **S-22**, which is `KEY SENT` — a DIFFERENT
//    lexeme for a different fact, now declared below. `GRANT KEY` is S-17, the design's own action word.
inline constexpr const char* kInviteGrantKey      = "GRANT KEY";            // S-17 — the act itself is N6's, below

// The screen's one NOTE row: the candidate word, or the empty state. ⓘ It is a FUNCTION rather than a
// renderer-side `if` for the §B115 reason — the condition is a decision, and decisions do not live in the one TU
// no gate compiles. ⛔ "Nobody new" is not an error: it is the ORDINARY answer, and the honest one on first open
// (spec §7.3 step 2 FAILs if an already-known member appears).
inline const char* invite_note(const InviteSelList& l) {
    // ⓘ `n == 1` is the EMPTY list: BACK is unconditional, so a list of one row is BACK alone.
    return (l.n <= 1) ? kInviteEmpty : kInviteNew;
}

// ★ THE MISSING-KEY CONFIRMATION. BACK is the zero/default arm; reaching the request therefore costs the ruled
//   `short` then `double`, and merely entering a candidate cannot issue a WANT_PUBKEY.
inline const char* invite_pubkey_label(bool request) { return request ? kInviteRequestPubkey : "BACK"; }

// ★ THE READY CONFIRMATION. REJECT is selected initially and is still the local handled-set act (F-13); `GRANT KEY`
//   is the one press on this device that ships a private key, and N6 below is its act.
inline const char* invite_confirm_label(bool grant) { return grant ? kInviteGrantKey : "REJECT"; }

// ================================================================================= §UI-16 N6 — THE GRANT ACT
// ★★★★ THE OUTCOME MAPPING IS THE WHOLE SLICE AND IT IS **PURE** (spec §4-N6), so all EIGHT `TeamKeyGrantTx` arms
//      and BOTH push outcomes are driven by the native suite rather than reasoned about. ⛔ Nothing below sends
//      anything, opens a second send path, adds a payload, a frame type or a wire byte: the ACT is one forward to
//      the existing `Node::team_key_grant_send`, and everything else here is WHAT THE PANEL IS ALLOWED TO SAY.
//
// ★★★★ ✅ F-9, RULED — AND THE WITHDRAWN RULE IS KEPT VISIBLE: *"`ctr != 0` = airborne ⇒ `KEY SENT`"*. It is FALSE.
//      `ctr != 0` means the send was ADMITTED TO THE QUEUE; nothing has left the radio, and the operator standing
//      in front of a candidate reading `KEY SENT` would be reading a claim about the air that no layer made. ⇒
//        · `queued` ⇒ `GRANT QUEUED` (S-21) — admission, and the ONLY promotable state;
//        · a CORRELATED `PushKind::send_aired{dst, ctr}` ⇒ `KEY SENT` (S-22) — the SX1262 TxDone edge for THIS
//          flight (`lib/core/command.h`'s `send_aired`, `lib/core/node.cpp`'s `push_send_aired_if_owned`);
//        · a CORRELATED failure ⇒ `GRANT FAILED` (S-23);
//        · `parked` ⇒ stored behind an H resolve, in its own words (S-37, below).
// ⛔⛔ **CORRECTED 2026-08-24 (§UI-16 N6b, QG-ruled) — AND THE WITHDRAWN HALF IS KEPT VISIBLE:** this block used to
//     read *"`queued` with `ctr == 0` ⇒ PARKED"* and to split the `queued` arm on the handle. **THE HANDLE IS NOT AN
//     ADMISSION SIGNAL AND NEVER WAS**, measured at the two sites: a FULL TX QUEUE dropped the frame and still
//     returned a non-zero `ctr` (`lib/core/node_mac.cpp`), and a FULL PARKED RING stored nothing and returned 0
//     (`lib/core/node_hashlocate.cpp`) — so `GRANT QUEUED` and `GRANT PARKED` could BOTH be false. ⇒ the core now
//     returns an EXPLICIT dispatch outcome and this mapper reads it: `parked` is shown ⛔ ONLY for the explicitly
//     STORED park (S-37's own rule), and the admission refusal gets its own word, `GRANT QUEUE FULL` (S-38), which
//     ⛔ may never be collapsed into `GRANT FAILED` — that word belongs to the CORRELATED in-flight failure.
// ⛔⛔ AND THERE IS **NO E2E ACK ON A GRANT** (`lib/core/node_mac_rx.cpp` consumes the DATA_TYPE_TEAM_KEY_GRANT as control traffic and
//     answers nothing), so ⛔ NO arm here may print `JOIN COMPLETE` (S-32), `KEY RECEIVED`, or any other completion
//     word: this node cannot know that the grant was received, and the joiner's own `TEAM KEY RECEIVED` (S-25) is a
//     different node's screen, reached from its own push.
inline constexpr MESHROUTE_NS::Plane kInviteGrantPlane = MESHROUTE_NS::Plane::TEAM;
// ★★★ THE PLANE IS NAMED **HERE**, IN THE PURE UNIT, AND ⛔ NEVER IN THE DEVICE TU — the same rule and the same
//     reason as N5's `invite_reqpubkey_command`: an address-plane choice is a DECISION, it is attackable at match
//     count 1 only where it is written, and `AUTO`/`GLOBAL` would ask a different network for a member this screen
//     enumerated on the TEAM plane. ⓘ It is also what makes `delegated` UNREACHABLE on the real seam — the mobile
//     pre-check in `Node::team_key_grant_send` is gated on `plane != Plane::TEAM` — which is precisely why the
//     `delegated` arm below must still fail LOUD instead of being deleted.

// ★★★ WHAT THE PANEL HOLDS ABOUT ONE GRANT. ⓘ It is a STATE and not a string, so the words live once (below) and
//     the correlation reasons about the STATE — `sent`/`failed` are terminal, `queued` is the only thing a push may
//     upgrade, and `parked` has no handle to correlate with at all.
enum class InviteGrantState : uint8_t {
    none = 0,      // no act has run — the carrier's zero value, and ⛔ not a word (§B74: no value is reserved)
    queued,        // S-21 `GRANT QUEUED`  — admitted with a handle; the ONE promotable state
    parked,        // S-37 `GRANT PARKED`  — ⛔ ONLY from the core's EXPLICITLY-STORED park (see `kInviteGrantParked`)
    queue_full,    // S-38 `GRANT QUEUE FULL` — the ADMISSION REFUSAL. No grant DATA was stored or will air; an
                   //   unresolved-target H lookup may still air. ⛔ No push will ever be about it.
    sent,          // S-22 `KEY SENT`      — ⛔ ONLY from a CORRELATED `send_aired`
    failed,        // S-23 `GRANT FAILED`  — a CORRELATED in-flight failure, ★ or the core's own already-reported
                   //   pre-admission `send_failed` (§UI-16 N6b: the same fact, arriving synchronously)
    no_team,       // S-24 `NOT IN A TEAM`
    no_key,        // S-24 `NO TEAM KEY`
    no_identity,   // S-24 `NO IDENTITY`
    no_pubkey,     // S-18 `NEED PUBKEY` — ★ the SAME lexeme N5's landing uses, deliberately: it is the SAME fact
                   //   about the same candidate, and a second spelling of it would be a second state to reason about
    self,          // S-24 `SELF`
    wrong_plane,   // S-24 `WRONG PLANE` — `delegated`'s word; see the mapper
    name_too_long, // S-24 `NAME TOO LONG`
    // ★★★★ §UI-16 N6b (QG round 2, 2026-08-24) — **THE INVENTORY SENTINEL, AND IT EXISTS BECAUSE A HAND-WRITTEN
    //      ONE ALREADY FAILED THIS ARC.** The word sweep used to walk a hand-listed array compared against a
    //      hand-typed literal, so when `queue_full` was added the array stayed short, the literal stayed right,
    //      and the case went on calling itself exhaustive while never testing the longest word on the screen.
    //      ⇒ `count` makes the INVENTORY a property of the ENUM: the sweep iterates `0 .. count-1`, so a state
    //      added above this line is visited automatically, and `invite_grant_word`'s `default`-less switch makes
    //      forgetting to WORD it a BUILD FAILURE (-Werror=switch). ⛔ There is no pair left to keep in sync.
    // ⛔ IT IS ⛔ NOT A STATE and no carrier may ever hold it: it names how many there are, nothing more. ⛔ It
    //    must stay LAST — that is what makes it the count — and §B74's "no arithmetic value is reserved" is not
    //    violated, because `count` is not a value any screen, push or result can carry.
    count
};

// ★ THE WORDS, DECLARED ONCE (spec §8). ⚠ WIDTHS against the 19-column body, in declaration order: 12 · 8 · 12 ·
//   13 · 11 · 11 · 4 · 11 · 13, and `GRANT PARKED` below is 12 and `GRANT QUEUE FULL` is 16 — every one fits on its
//   own row, so ⛔ none of them is ever clipped or re-wrapped. (A native case sweeps the lengths rather than
//   trusting this line.)
inline constexpr const char* kInviteGrantQueued = "GRANT QUEUED";    // S-21 — ADMISSION, ⛔ not air
inline constexpr const char* kInviteKeySent     = "KEY SENT";        // S-22 — the design's own word (§3.6.4 :821)
inline constexpr const char* kInviteGrantFailed = "GRANT FAILED";    // S-23
inline constexpr const char* kInviteNotInTeam   = "NOT IN A TEAM";   // S-24 — `no_team`
inline constexpr const char* kInviteNoTeamKey   = "NO TEAM KEY";     // S-24 — `no_key`
inline constexpr const char* kInviteNoIdentity  = "NO IDENTITY";     // S-24 — `no_identity`
inline constexpr const char* kInviteSelfTarget  = "SELF";            // S-24 — `self`
inline constexpr const char* kInviteWrongPlane  = "WRONG PLANE";     // S-24 — `delegated`
inline constexpr const char* kInviteNameTooLong = "NAME TOO LONG";   // S-24 — `too_large`
// ★★★ ⓘ **REPORTED, NOT INVENTED** — §8's own rule for a ruling that settles a SEMANTIC and no lexeme: *"the wording
//     is this file cluster's house style applied to it, one line each, pinned by a native case"*. The PARKED
//     sub-state is exactly that case: F-9 rules the behaviour (`queued` with `ctr == 0` is parked behind an H
//     resolve and *"says so in its own words"*) and §8 carries no row for it, because the draft's word there was
//     `WAITING FOR KEY` — now FORBIDDEN (S-34, ambiguous between the recipient's PUBKEY and the team CONTENT key).
//   ⇒ the word is the CONSOLE's own already-shipped one — `src/firmware_config.cpp`'s
//     *"PARKED (resolving the target's node id — it flies when the binding arrives)"* — reduced to the cluster's
//     `GRANT <state>` shape, so the panel and the serial log name the same state with the same word.
//   ⛔⛔ **AMENDED 2026-08-24 — §8 NOW CARRIES IT AS S-37, AND ITS RULE IS A PROHIBITION:** it is shown ONLY for the
//     core's EXPLICITLY-STORED parked outcome (`TeamKeyGrantTx::parked`) and ⛔ NEVER inferred from `ctr == 0`,
//     because a FULL parked ring stores nothing and returns exactly the same zero.
inline constexpr const char* kInviteGrantParked = "GRANT PARKED";
// ★★★ S-38, ADDED 2026-08-24 (the N6 first-gate QG): the ADMISSION REFUSAL — the TX queue or the parked ring was
//     FULL, so the frame was dropped at the door. ⛔ IT IS ⛔ NOT `GRANT FAILED`: that word means a flight this node
//     really made came back failed, and an operator told "failed" reaches for a different remedy than one told the
//     device is momentarily too busy to accept the send. ⛔ And it is emphatically not `GRANT QUEUED`, which is the
//     word the pre-correction core laundered it into.
inline constexpr const char* kInviteGrantQueueFull = "GRANT QUEUE FULL";

// ★★★★ THE ELEVEN ARMS, EACH WITH ITS OWN WORD — ⛔ THEY MAY NOT COLLAPSE (spec §8 S-24, verbatim). An operator
//      holding a device that says `GRANT FAILED` for six different, differently-remediable states is an operator
//      who cannot act: `NO TEAM KEY` is *ask a teammate*, `NO IDENTITY` is *this node has no crypto identity*,
//      `NOT IN A TEAM` is *join first*, and they are not the same sentence.
// ⛔ NO `default:` — §B72's rule: `-Wswitch` is what makes a TWELFTH `TeamKeyGrantTx` enumerator a BUILD FAILURE
//    here instead of a screen that silently says the wrong thing. The trailing return exists only for
//    `-Wreturn-type`. ★ IT WORKED: N6b's three new core enumerators reddened this switch and the console's at
//    compile time, which is exactly why they are added rather than folded into `queued`.
// ★★★ `delegated` IS UNREACHABLE ON THE REAL SEAM AND IS MAPPED ANYWAY, LOUDLY (C2): the UI always sends
//     `kInviteGrantPlane`, and the core's delegate pre-check is gated on `plane != Plane::TEAM` — but an unreachable
//     arm that returns a PLAUSIBLE word is the arm that lies the day it becomes reachable. `WRONG PLANE` is a
//     REFUSAL: it claims no queue, no air and no delivery, and no push can ever promote it.
// ⓘ `too_large` is unreachable for the same class of reason (this screen sends no `name=` at all — F-3) and is
//   mapped for the same one.
// ★★★★ §UI-16 N6b — **THE HANDLE IS NOT A PARAMETER OF THIS FUNCTION ANY MORE, AND THAT IS THE WHOLE CORRECTION.**
//      ⛔ WITHDRAWN SIGNATURE, KEPT VISIBLE: `invite_grant_state_of(tx, ctr)`, whose `queued` arm read
//      `(ctr != 0) ? queued : parked`. The state is now the CORE's own word and nothing else, so no reading of the
//      counter can make the panel say a thing the dispatch did not report. The `ctr` survives as a CORRELATION term
//      on the result carrier — never as evidence of admission.
inline InviteGrantState invite_grant_state_of(MESHROUTE_NS::Node::TeamKeyGrantTx tx) {
    using TX = MESHROUTE_NS::Node::TeamKeyGrantTx;
    switch (tx) {
        // ★★★ THE HEADLINE (F-9): `queued` is ADMISSION — the core says the TxItem was really STORED. ⛔ It is not
        //     `sent`: nothing has left the radio until a correlated `send_aired` says so.
        case TX::queued:      return InviteGrantState::queued;
        // ★★★ THE THREE N6b ARMS — each one used to arrive here wearing `queued`'s clothes.
        case TX::parked:      return InviteGrantState::parked;       // S-37, and ⛔ ONLY from this explicit outcome
        case TX::queue_full:  return InviteGrantState::queue_full;   // S-38, and ⛔ never collapsed into `failed`
        // ⓘ `send_failed` IS THE SAME FACT `GRANT FAILED` ALREADY NAMES, arriving synchronously instead of as a
        //   push: the core reached no admission point and has ALREADY pushed `send_failed` for it (its enumerator
        //   in `lib/core/node.h` lists the paths). ⛔ It gets no new lexeme — one fact, one word (S-23).
        case TX::send_failed: return InviteGrantState::failed;
        case TX::no_team:     return InviteGrantState::no_team;
        case TX::no_key:      return InviteGrantState::no_key;
        case TX::no_identity: return InviteGrantState::no_identity;
        case TX::no_pubkey:   return InviteGrantState::no_pubkey;
        case TX::self:        return InviteGrantState::self;
        case TX::delegated:   return InviteGrantState::wrong_plane;
        case TX::too_large:   return InviteGrantState::name_too_long;
    }
    return InviteGrantState::wrong_plane;   // ⛔ unreachable (see the -Wswitch note); a REFUSAL, never a claim
}

// The one word for one state. ⓘ `none` renders `""` — the result screen is unreachable without an act, and an empty
// string is the honest answer for "nothing has happened", ⛔ never a reassuring one.
inline const char* invite_grant_word(InviteGrantState s) {
    switch (s) {
        case InviteGrantState::none:          return "";
        case InviteGrantState::queued:        return kInviteGrantQueued;
        case InviteGrantState::parked:        return kInviteGrantParked;
        case InviteGrantState::queue_full:    return kInviteGrantQueueFull;
        case InviteGrantState::sent:          return kInviteKeySent;
        case InviteGrantState::failed:        return kInviteGrantFailed;
        case InviteGrantState::no_team:       return kInviteNotInTeam;
        case InviteGrantState::no_key:        return kInviteNoTeamKey;
        case InviteGrantState::no_identity:   return kInviteNoIdentity;
        case InviteGrantState::no_pubkey:     return kInviteNeedPubkey;      // S-18, REUSED — one fact, one word
        case InviteGrantState::self:          return kInviteSelfTarget;
        case InviteGrantState::wrong_plane:   return kInviteWrongPlane;
        case InviteGrantState::name_too_long: return kInviteNameTooLong;
        // ⛔ THE SENTINEL IS ⛔ NOT A STATE, so it has NO word — and it is spelled out rather than left to a
        //    `default:` for the reason the whole switch has none: `default:` would swallow a REAL state added
        //    above it, which is precisely the miss this sentinel was introduced to make impossible.
        case InviteGrantState::count:         return "";
    }
    return "";
}

// ★★★★ THE ACT'S WHOLE RESULT, AND IT **OUTLIVES THE WINDOW** DELIBERATELY: the grant ends the window (its snapshot,
//      its handled set and its frozen selection are discarded on the way to the result screen), so the verdict must
//      carry its OWN identity or the panel could not draw the hash it is about (P-7c).
// ★★★ THE TWO CORRELATION TERMS ARE STORED TOGETHER WITH THE STATE because they are one fact: *this* flight.
// ⓘ COST, MEASURED not assumed (host, `offsetof`-proved beside its declaration in `firmware_ui_model.h`):
//   `sizeof(InviteGrantResult)` is 8 — `hash` 0, `ctr` 4, `dst` 6, `st` 7, no tail hole at alignof 4.
struct InviteGrantResult {
    uint32_t         hash = 0;                        // the target the act named — the result screen's identity
    uint16_t         ctr  = 0;                        // correlation term 1: the origination handle (0 = none exists)
    uint8_t          dst  = 0;                        // correlation term 2: the SEND-TIME RESOLVED team-local id
                                                      //   ★ §UI-16 N6b — ⛔ WITHDRAWN, KEPT VISIBLE: *"the FROZEN
                                                      //   team-local id"*. A team-DAD between the selection and the
                                                      //   press moves it, and `send_aired` carries what the core
                                                      //   really addressed — so the frozen one never matched.
    InviteGrantState st   = InviteGrantState::none;
};

// ★★★★ THE ACT. ⛔ THE TARGET IS A `key_hash32` AND THE MODEL FREEZES IT — no display name is accepted by this
//      function, so a mutable label cannot become the thing a private key is shipped to (P-7d, [[B48]]'s class).
// ⛔ A NULL SEAM OR A ZERO HASH PERFORMS NOTHING AND CLAIMS NOTHING (C2, and N5's `invite_issue_reqpubkey` shape one
//    screen earlier): it returns false, the caller stays where it was, and ⛔ no word is invented for a state §8
//    does not inventory. An unattached model (a `!MR_FEAT_OLED`-shaped build, a partially-wired probe) must fail
//    CLOSED, ⛔ never by spending a call and ⛔ never by announcing an outcome nothing produced.
// ★★★★ §UI-16 N6b — **THE FROZEN `dst_id` PARAMETER IS GONE, AND ITS ABSENCE IS THE FIX.** ⛔ WITHDRAWN SIGNATURE,
//      KEPT VISIBLE: `invite_grant_perform(dev, key_hash32, dst_id, out)`, whose body did `out.dst = dst_id`. The
//      send resolves its OWN destination from the hash inside `Node::send_by_hash` — against the binding that is
//      live AT SEND TIME — so a member that re-ran team-DAD between the row and the press was granted on the NEW
//      id while the panel waited for a `send_aired` addressed to the OLD one. It never arrived, and the screen sat
//      at `GRANT QUEUED` for ever. ⇒ the correlation's second term is the core's ANSWER, ⛔ never the UI's memory.
// ⓘ The resolved id is still a TEAM-plane local id (C3): it is a CORRELATION term only — it indexes nothing here
//   and addresses nothing here; the addressing was done by the core that reported it.
inline bool invite_grant_perform(IUiInviteDevice* dev, uint32_t key_hash32, InviteGrantResult& out) {
    // ⚠ SPELLED `dev == nullptr` RATHER THAN `!dev`, and it is ⛔ not a style drift: `invite_issue_reqpubkey`'s
    //   identical guard one screen up is a battery entry's EXACT MATCH TEXT (`uiinvite` I16 — the missing-seam
    //   control), and a second copy of that line would silently make it VACUOUS, i.e. disarm the instrument that
    //   proves an unattached model claims nothing. (Measured: the match count went to 2.)
    if (dev == nullptr || key_hash32 == 0) return false;
    uint16_t ctr = 0;
    uint8_t  dst = 0;
    const MESHROUTE_NS::Node::TeamKeyGrantTx tx = dev->grant(key_hash32, kInviteGrantPlane, &ctr, &dst);
    out.hash = key_hash32;
    out.dst  = dst;                          // ★ the core's SEND-TIME resolution (see the note above)
    out.ctr  = ctr;
    out.st   = invite_grant_state_of(tx);    // ⛔ the outcome ALONE decides the word — ⛔ never the handle
    return true;
}

// ★★★★ THE CORRELATION, AND **BOTH TERMS ARE LOAD-BEARING** (spec §4-N6): a `ctr` alone is a LOCAL handle and the
//      same value legitimately names another flight — `lib/core/command.h` says so about `channel_sent`'s own `ctr`
//      in as many words — so `{dst, ctr}` is the pair that means *this* flight and either half alone does not.
// ★★★ AND IT UPGRADES A **QUEUED** STATE ONLY, which is `send_aired`'s own documented contract (*"a consumer must
//     apply it as an UPGRADE of a queued state only, never over a terminal one"*): `sent` and `failed` are terminal,
//     every refusal is terminal — ★ including N6b's `queue_full`, for which the core stored nothing, so ⛔ NO push
//     can ever be about it — and `parked` has `ctr == 0`: no handle exists, so ⛔ nothing may match it. That
//     last term is not decoration: `ctr == 0` is the value six unrelated operations put on a push (`send_failed`'s
//     own `dst == 0` note), so a zero-handle match would be a wildcard.
inline bool invite_grant_correlates(const InviteGrantResult& r, const MESHROUTE_NS::Push& pu) {
    return r.st == InviteGrantState::queued && r.ctr != 0 && pu.ctr == r.ctr && pu.dst == r.dst;
}
// The two push outcomes, and ⛔ only these two: `send_aired` promotes to `KEY SENT`, `send_failed` to `GRANT FAILED`.
// ⓘ `send_acked` / `send_e2e_acked` are deliberately NOT here — see the no-e2e-ack note at the top of this block: a
//   link ack is not delivery of a grant, and there is no end-to-end ack to receive.
inline bool invite_grant_apply_push(InviteGrantResult& r, const MESHROUTE_NS::Push& pu) {
    const bool aired  = pu.kind == MESHROUTE_NS::PushKind::send_aired;
    const bool failed = pu.kind == MESHROUTE_NS::PushKind::send_failed;
    if (!aired && !failed) return false;
    if (!invite_grant_correlates(r, pu)) return false;
    r.st = aired ? InviteGrantState::sent : InviteGrantState::failed;
    return true;
}

}  // namespace mrui
