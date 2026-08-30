// MeshRoute — lib/core/node_join.cpp  (node_id auto-assignment: DAD + self-heal)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Allocates the 8-bit short `node_id` with no authority, over lossy links / partitions, into the
// 254-id space. Identity is the key_hash32 (stable); node_id is a disposable lease — renumbering is
// harmless (upper layers re-bind by key_hash32). Design + rationale:
//   docs/specs/2026-06-05-node-id-auto-assignment-design.md
//
// Mirrors the Lua join cluster (dv_dual_sf.lua join_choose_candidate_id / join_start_claim / handle_j
// CLAIM+DENY / forced_rejoin) with ONE deliberate, signed-off divergence: the tiebreak is KEY-ONLY
// (lower key_hash32 wins) — the Lua's lease_age-first is non-convergent under wire staleness (§6), and
// claim_epoch is now vestigial (kept on the wire/NV, no longer consulted). DISCOVER/OFFER are deferred
// (the design's join is beacon-listen + Q config-pull + DAD); this slice is the CLAIM/heal core.
#include "node.h"
#include "frame_codec.h"
#include "identity.h"  // §P2-6: key_hash32_of (LE(ed_pub[:4]) derivation)

#include <span>

namespace MESHROUTE_NS {

// §6 — the one tiebreak (KEY-ONLY, decided 2026-06-06): lower key_hash32 WINS/keeps; higher yields.
// One rule for every STATIC-plane heal — direct (§7), mediated/shared-neighbour (L2a), delivery-driven (L2c) — so
// they can never pick different losers (a third-party mediator has no epoch; key alone keeps them
// consistent). key_hash32 is a unique total order per honest node ⇒ exactly one winner, convergent.
// claim_epoch is now VESTIGIAL: still carried on the J wire + in NV (reserved), no longer consulted here.
// team-DAD (node_beacon.cpp same_team_beacon collision) NOW calls this too (§P2-5, 2026-07-21) — its keys are
//   guaranteed unequal there, so !join_tiebreak_wins == the old inline `_key_hash32 > b.key_hash32`.
// ★ ONE DELIBERATE DIVERGENCE (code is truth): hosted-mobile local-id allocation is ARRIVAL-ORDER-WINS (first key to
//   register keeps the id; find_free_mobile_id is idempotent per key), NOT a key tiebreak — the mobile plane has no global DAD.
bool Node::join_tiebreak_wins(uint8_t /*my_epoch*/, uint32_t my_key, uint8_t /*their_epoch*/, uint32_t their_key) {
    return my_key < their_key;
}

// ---- denied-id list (§13: a slot that lost a claim/heal stays denied for dad_denied_id_ttl_ms = 1 day) --
bool Node::join_id_denied(uint8_t id) const {
    return recent_ring_hit(_join_denied, _join_denied_n, DeniedId{ id, 0 }, _hal.now(), protocol::dad_denied_id_ttl_ms);
}
void Node::join_deny_id(uint8_t id) {
    recent_ring_mark(_join_denied, _join_denied_n, DeniedId{ id, _hal.now() });
}
void Node::age_out_denied_ids() {
    recent_ring_age_out(_join_denied, _join_denied_n, _hal.now(), protocol::dad_denied_id_ttl_ms);
}

// ---- L2a mediation suppression: one DENY per (id, loser-hash) per window (#1 — kill the per-beacon storm) --
bool Node::mediated_recently(uint8_t node_id, uint32_t loser_hash) const {
    return recent_ring_hit(_mediated_recent, _mediated_recent_n, MediatedRecent{ node_id, loser_hash, 0 },
                           _hal.now(), protocol::mediated_deny_suppress_ms);
}
void Node::mark_mediated(uint8_t node_id, uint32_t loser_hash) {
    recent_ring_mark(_mediated_recent, _mediated_recent_n, MediatedRecent{ node_id, loser_hash, _hal.now() });
}
void Node::age_out_mediated() {
    recent_ring_age_out(_mediated_recent, _mediated_recent_n, _hal.now(), protocol::mediated_deny_suppress_ms);
}

// §mobile 2a: host-assign a free LOCAL id (17..254) for a mobile — distinct across THIS host's registered mobiles + not
// our own id. Returns 0 if the pool is full. Idempotent: a known key_hash returns its existing id (a re-DISCOVER re-offers
// the same id). The id MAY overlap a neighbour's global id — the mobile mark disambiguates (§17 A3), no global DAD.
//
// §S0 CONVENTION (cold-boot alias fix): the two id-pickers share the 17..254 range. Hosted-mobile allocation picks
// TOP-DOWN from 254 (:107). Static DAD (join_choose_candidate_id) picks UNIFORMLY AT RANDOM from the free ids in
// 17..254 (:161) — NOT bottom-up (code is truth: the old comment claimed a bottom-up scan that the picker never did).
// So the pools are not a guaranteed disjoint split; the real protection is that the mobile allocator EXCLUDES every id
// it has evidence is a static (id_bind + _rt, same wide view as the static picker) AND biases high (top-down), making a
// mobile/static collision improbable until the pool is nearly exhausted. The metal bug was cold-boot allocation at t~8s handing a mobile local 18 (== static S2) BEFORE
// S2's beacon populated id_bind/_rt, so the picker "knew" nothing to exclude. Fix: exclude every id we have evidence
// is a static (id_bind + the routing table, same wide view as the static picker), AND allocate top-down. Two further
// backstops make this self-healing: (b) a LATER static binding for an id we already gave a mobile EVICTS the mobile
// (evict_aliased_hosted_mobile at id_bind_set) -> it re-registers onto a fresh top id via the presence plane; and
// route_uses_mobile_as_transit's static carve stops false-rejecting a route THROUGH such an aliased static meanwhile.
//
// ★★★ §MH-S2 / [[B137]] — AND THE THIRD SOURCE OF TRUTH THIS PICKER NOW CONSULTS: LIVE PENDING RESERVATIONS.
// Before this slice the picker scanned `_mobile_reg` (plus the static evidence above) and **a staged OFFER is not a
// registry row**, so every concurrently discovering mobile was offered the SAME id — measured at 254 for both in
// §S0-1. It was invisible only because the single-slot `_pending_offer` meant one OFFER ever flew: the two defects
// masked each other, and S2's ring removes the mask. ⇒ an id promised in an armed-or-recently-transmitted OFFER is
// TAKEN until its CLAIM lands or `mobile_offer_reservation_ms` elapses.
// ★ The reservation check is written against `now` (`reserve_until_ms > now`), NOT against a "was it swept yet" flag,
//   so an ELAPSED reservation stops blocking its id the instant it expires — even if `mobile_offer_fire`'s scan has
//   not run yet (a dropped/failed `_hal.after` must never be able to leak an id permanently).
uint8_t Node::find_free_mobile_id(uint32_t key_hash32) {
    // ★ §MH-S5 §9.3 — "run `mobile_reg_age_out(now)` … before allocating a local id or refusing because
    // `cap_host_mobiles` is full". Both of this function's refusals are downstream of the row set, so a row that
    // is 25 minutes dead must not be able to hold its id (or a whole host slot) for the up-to-60 s until the next
    // `kAgingTimerId` sweep. ⛔ Do NOT duplicate the age predicate below (§9.3's closing line) — the sweep is the
    // only place it lives.
    mobile_reg_age_out();
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == key_hash32) return _active->_mobile_reg[i].mobile_local_id;
    const uint64_t now = _hal.now();
    // IDEMPOTENT FOR A KEY WE ALREADY PROMISED: a re-DISCOVER from a mobile that holds a live reservation gets ITS
    // OWN id back, exactly as a registered mobile gets its row's id back one loop above. This is what makes §5.3.2's
    // "a duplicate DISCOVER retains its reservation" true even on the path where the entry already fired.
    for (uint8_t i = 0; i < protocol::cap_pending_mobile_offers; ++i) {
        const PendingMobileOffer& e = _pending_mobile_offers[i];
        if (e.reserve_until_ms > now && e.target_key_hash32 == key_hash32) return e.proposed_id;
    }
    auto id_taken = [&](uint8_t id) -> bool {
        if (id == _node_id) return true;
        if (join_id_denied(id)) return true;                                                                    // an id under active DAD denial (a static conflict) — mirror the static picker
        for (uint8_t  i = 0; i < _active->_mobile_reg_n; ++i) if (_active->_mobile_reg[i].mobile_local_id == id) return true;
        for (uint8_t  i = 0; i < protocol::cap_pending_mobile_offers; ++i)                                                     // [[B137]]: an id PROMISED to another mobile mid-handshake
            if (_pending_mobile_offers[i].reserve_until_ms > now && _pending_mobile_offers[i].proposed_id == id) return true;
        for (uint16_t i = 0; i < _active->_id_bind_n;    ++i) if (_active->_id_bind[i].node_id == id)            return true;   // a known static (direct/heard) binding
        for (uint8_t  i = 0; i < _active->_rt_count;     ++i) if (_active->_rt[i].dest == id)                    return true;   // a DV-reachable static within dv_hop_cap
        return false;
    };
    for (int id = 254; id >= protocol::normal_node_id_min; --id)   // TOP-DOWN (statics climb from 17 -> the pools stay disjoint until near-full)
        if (!id_taken(static_cast<uint8_t>(id))) return static_cast<uint8_t>(id);
    return 0;   // pool full
}

// §S0 (b): a hosted mobile's local id ALIASES a real static's node_id (the cold-boot pool collision, or a static that
// arrives AFTER we allocated the id). When an AUTHORITATIVE static binding lands for that id, evict the aliasing mobile
// from the registry so it re-registers onto a fresh (top-of-range) id — it notices its own absence from the next
// P-roster (S6 absent-from-roster rule) and re-DISCOVERs. The static keeps its own id; route_uses_mobile_as_transit's
// carve already un-poisons routes through it in the interim. No-op unless we host a mobile on that id (static-inert).
void Node::evict_aliased_hosted_mobile(uint8_t node_id, uint32_t static_key_hash32) {
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i) {
        if (_active->_mobile_reg[i].mobile_local_id != node_id)     continue;
        if (_active->_mobile_reg[i].key_hash32 == static_key_hash32) return;   // it IS this mobile (own beacon/rebind), not an alias
        MR_EMIT("mobile_evict_alias", EF_I("local_id", node_id),
                EF_I("mobile_key", static_cast<int64_t>(_active->_mobile_reg[i].key_hash32)),
                EF_I("static_key", static_cast<int64_t>(static_key_hash32)));
        mobile_reg_remove(i, "static_alias");   // §MH-S5 §9.3: the ONE compaction primitive (was open-coded here)
        presence_schedule_roster();   // the next roster omits it -> the mobile re-registers (absent-from-roster)
        return;
    }
}

// ---- §3 candidate selection: prefer our previous id, else a random free slot (-1 = leaf full) ----------
int Node::join_choose_candidate_id() {
    const int prev = id_bind_find_by_hash(_key_hash32);                 // the network/NV may remember our old id
    // R6.3/G1: a legacy/NV prev id in the gateway range 1..16 is NOT re-preferred -> re-pick a normal id (17..254).
    if (prev >= protocol::normal_node_id_min && prev <= 254 && !join_id_denied(static_cast<uint8_t>(prev))) {
        MR_EMIT("join_prefer_previous_id", EF_I("node", prev), EF_I("key_hash32", static_cast<int64_t>(_key_hash32)));
        return prev;
    }
    // "taken" = every id this node KNOWS is in use (L1, design §3): id_bind (direct neighbours + heard
    // claims) ∪ _active->_rt dest (EVERY reachable node within dv_hop_cap — DV-propagated, the wide view that makes
    // an incremental joiner leaf-unique) ∪ the no-route defer queue ∪ our own pending claim. Best-effort:
    // pre-convergence / simultaneous-cold-start gaps fall to the heal (§7.1), not to this picker.
    auto id_taken = [&](uint8_t id) -> bool {
        for (uint16_t i = 0; i < _active->_id_bind_n;  ++i) if (_active->_id_bind[i].node_id == id)     return true;
        for (uint8_t  i = 0; i < _active->_rt_count;   ++i) if (_active->_rt[i].dest == id)             return true;
        for (uint8_t  i = 0; i < _active->_deferred_n; ++i) if (_active->_deferred[i].item.dst == id)   return true;
        return _join_claim.active && _join_claim.proposed == id;
    };
    uint8_t free_list[254];                                             // 254 B stack — fine
    uint16_t nfree = 0;
    for (int id = protocol::normal_node_id_min; id <= 254; ++id)        // R6.3/G1: normal nodes pick 17..254 (1..16 = gateways)
        if (!join_id_denied(static_cast<uint8_t>(id)) && !id_taken(static_cast<uint8_t>(id)))
            free_list[nfree++] = static_cast<uint8_t>(id);
    if (nfree == 0) return -1;
    return free_list[_hal.rand_range(0, static_cast<int>(nfree))];      // uniform pick (two joiners rarely collide)
}

// ---- §4 claim -> probe -> adopt ----------------------------------------------------------------------
bool Node::join_start_claim([[maybe_unused]] const char* reason) {   // reason: telemetry-only (stripped on device)
    if (_joined || _join_claim.active) return false;
    const int cand = join_choose_candidate_id();
    if (cand < 0) {                                       // 17..254 all taken -> leaf full
        MR_EMIT("join_no_candidate", EF_S("reason", reason ? reason : "no_free_id"));
        Push pu{}; pu.kind = PushKind::join_refused; pu.join_reason = JoinRefuseReason::leaf_full; enqueue_push(pu);   // §7c: visible on metal
        return false;
    }
    // claim_epoch is NO LONGER bumped (key-only tiebreak, §6) — it stays reserved on the wire + in NV.
    const uint8_t nonce = static_cast<uint8_t>(_hal.rand_range(0, 256));
    _join_claim = { true, static_cast<uint8_t>(cand), _key_hash32, _claim_epoch, nonce, _hal.now() };

    j_claim_in in{};
    in.leaf_id = _cfg.leaf_id; in.gateway_capable = _cfg.is_gateway; in.is_mobile = _cfg.is_mobile;
    in.key_hash32 = _key_hash32; in.proposed_node_id = static_cast<uint8_t>(cand);
    in.lease_age_seconds = 0;                                           // telemetry only (§6); tiebreak ignores it
    in.claim_epoch = _claim_epoch; in.nonce = nonce;
    uint8_t buf[11];
    const size_t n = pack_j_claim(in, std::span<uint8_t>(buf, sizeof buf));
    MR_EMIT("join_claim_sent", EF_I("proposed_node_id", cand), EF_I("key_hash32", static_cast<int64_t>(_key_hash32)),
            EF_I("claim_epoch", _claim_epoch), EF_S("reason", reason ? reason : "auto"));
    if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    (void)_hal.after(protocol::dad_claim_guard_ms, kJoinClaimGuardTimerId);
    return true;
}

// Guard window elapsed: adopt if no objection surfaced (no conflicting binding for our proposed id), else
// deny that id + retry with a fresh candidate after a backoff.
void Node::join_claim_guard_fire() {
    if (!_join_claim.active) return;
    const uint8_t proposed = _join_claim.proposed;
    _join_claim.active = false;
    bool conflict = false;
    for (uint16_t i = 0; i < _active->_id_bind_n; ++i)
        if (_active->_id_bind[i].node_id == proposed && _active->_id_bind[i].key_hash32 != _key_hash32) { conflict = true; break; }
    if (conflict) {
        join_deny_id(proposed);
        MR_EMIT("join_claim_denied", EF_I("denied_node_id", proposed), EF_S("reason", "claim_guard_conflict"));
        (void)_hal.after(protocol::join_retry_backoff_ms, kJoinRetryTimerId);
        return;
    }
    join_adopt(proposed);
}

void Node::join_adopt(uint8_t node_id) {
    set_identity(node_id, _key_hash32);                                // _node_id + Hal id + authoritative self-bind
    _joined = true;
    _join_claim.active = false;
    MR_EMIT("join_adopted", EF_I("node", node_id), EF_I("key_hash32", static_cast<int64_t>(_key_hash32)), EF_I("claim_epoch", _claim_epoch));
    // Companion feedback: fires on EVERY adopt path — verb join/create, the boot DAD, and the heal re-adopt (id-change
    // staleness fix). The MOBILE adopt (node_mobile.cpp set_identity) does NOT route through here -> no mobile_reg double-push.
    Push pu{}; pu.kind = PushKind::join_adopted; pu.dst = node_id; pu.layer_id = _cfg.leaf_id; pu.ctr = _claim_epoch;
    enqueue_push(pu);
    schedule_triggered_beacon();                                       // announce the new id (peers re-bind on it)
    if (_pending_rediscover) {                                         // a verb reprovision -> the id is now stable: rebuild routes
        _pending_rediscover = false;
        restart_discovery();
    }
}

// ---- §5 receive: J dispatch + CLAIM/DENY handlers ----------------------------------------------------
void Node::handle_j(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pj = parse_j(std::span<const uint8_t>(bytes, len));
    if (!pj) return;
    const j_out& j = *pj;
    // §mobile: a mobile DISCOVER is LEAF-EXEMPT on the HOST side (2a — a mobile probes any host on the freq/sf/bw, §17);
    // a mobile OFFER is LEAF-EXEMPT on the MOBILE side (2b — the mobile hasn't adopted the host's leaf yet). Every other J
    // frame (static DISCOVER + CLAIM/DENY, + an OFFER to a non-mobile) stays leaf-filtered -> the static mesh is byte-unaffected.
    const bool mobile_exempt =
        (j.is_mobile && j.opcode == static_cast<uint8_t>(j_opcode::discover)) ||
        (j.is_mobile && j.opcode == static_cast<uint8_t>(j_opcode::offer) && _cfg.is_mobile);
    // §W2c: a TEAM-scoped mediated DENY is LEAF-EXEMPT (a mixed-leaf team spans nibbles, §P2-1) — it is team_id-gated
    // downstream, never touches the static id_bind plane. Static DENYs (team_scoped=false) stay leaf-filtered -> s18/s21-s28 unchanged.
    if (!mobile_exempt && !j.team_scoped && j.leaf_id != _cfg.leaf_id) return;   // foreign layer
    if (j.wire_version != protocol::wire_version) {                    // R6.2 §5.2: never join across a wire-version gap
        MR_EMIT("j_wire_incompatible", EF_I("src_op", j.opcode), EF_I("their_ver", j.wire_version), EF_I("my_ver", protocol::wire_version));
        return;
    }

    if (j.opcode == static_cast<uint8_t>(j_opcode::claim)) {
        if (j.is_mobile) {                                            // §mobile 2a: a mobile CLAIM = claim-stands (record/refresh — NO reply)
            if (!can_host_mobiles()) return;                          // ★ §B132: RE-CHECK eligibility on the ACCEPT path, not just on the OFFER. This gate was ABSENT: the only test was `chosen_host_id != _node_id`, so a stale CLAIM (from a mobile that adopted us before we became a gateway) or a forged one ADDRESSED at a gateway was recorded as a hosted mobile without ever asking whether we may host. Gating only the OFFER is the tempting HALF-fix — the registry entry is what makes the invalid home load-bearing.
            if (j.chosen_host_id != _node_id) return;                 // §mobile: only the host the mobile CHOSE records it — a flood-hearer (relay) is NOT a host (else it proxies for a mobile it doesn't serve)
            // ★★★ [[B147]] §MH-S2b — THE RESERVATION PLANE IS READ BEFORE ANYTHING ELSE, BECAUSE THE REGISTERED-ROW
            // COLLISION LOOP BELOW ONLY SEES REGISTERED ROWS. §MH-S2 made the OFFER reserve its proposed id ([[B137]])
            // but left the CLAIM correlating on HASH ALONE: it recorded whatever `proposed_node_id` the frame carried
            // and then released "the claimant's" slot by hash. A live reservation is a PROMISE about a (hash, id)
            // PAIR, and half of it was never read. ⇒ A is offered X and lets its reservation lapse · X is re-promised
            // to B · A's delayed CLAIM for X arrives · no REGISTERED row holds X yet, so A is recorded on B's reserved
            // id — and B's own CLAIM then walks into the collision-DENY recovery that [[B137]] exists to make
            // unnecessary.
            // ★ THE SHAPE, FOR THE RECORD: correlating on hash alone is the same category error as `LbtKind` alone
            //   ([[B142]]) and `seq` without `InboxKind` ([[B133]]/M5). The identity is the PAIR.
            // ⓘ `reserve_until_ms > now` (not `!= 0`) deliberately: an ELAPSED-but-unswept entry is NOT a live
            //   promise, exactly as `find_free_mobile_id` reads it — a dropped `_hal.after` must never be able to
            //   reject a legitimate CLAIM forever.
            const uint64_t claim_now = _hal.now();
            int own = -1, foreign = -1;
            for (uint8_t i = 0; i < protocol::cap_pending_mobile_offers; ++i) {
                const PendingMobileOffer& e = _pending_mobile_offers[i];
                if (e.reserve_until_ms <= claim_now) continue;                     // absent or elapsed -> not a promise
                if (e.target_key_hash32 == j.key_hash32)              own     = static_cast<int>(i);
                else if (e.proposed_id == j.proposed_node_id)         foreign = static_cast<int>(i);
            }
            // ★★★★ [[B147]] §MH-S2c — **THE STALENESS TEST RUNS FIRST, BEFORE ANY BRANCH ACTS ON THE FRAME'S
            // CONTENTS.** THE PRINCIPLE, STATED WHERE IT IS ENFORCED: *a stale frame must be identified as stale
            // before any branch acts on what it carries* — otherwise a check on its contents CONSUMES STATE THAT
            // BELONGS TO A NEWER TRANSACTION. Same family as [[B142]] (a stale completion consuming the newer
            // attempt's stage) and as [[B147]]'s own hash-vs-(hash,id) lesson: the ORDER of the tests is part of the
            // correlation, not an implementation detail.
            // ⛔ THE ORDERING DEFECT THIS FIXES (the §MH-S2b arrangement ran the registered-row loop first): A held
            //    id X · X is now REGISTERED to B · A re-DISCOVERs and is promised Y · A's DELAYED CLAIM for X arrives
            //    ⇒ the registered-row branch DENIED A **and released Y**, so A threw away the id we are currently
            //    promising it and the drop/retain policy below was never reached. The CLAIM was stale the whole time;
            //    only the order of the tests decided whether anyone noticed.
            // ⓘ A mismatching `own` short-circuits EVERY downstream branch — registered-row collision included —
            //   because none of them can be reasoning about the current transaction if the frame is not from it.
            if (own >= 0 && _pending_mobile_offers[own].proposed_id != j.proposed_node_id) {
                // We hold a LIVE promise to THIS mobile for a DIFFERENT id — i.e. it has already re-DISCOVERed and
                // been re-offered — so this CLAIM is a stale echo of an earlier round. ⛔ DROP, do not DENY, and
                // ⛔ RETAIN the reservation: a DENY would make it re-register and throw away the id we are currently
                // promising it. Its CLAIM for the promised id is still to come, and `find_free_mobile_id`'s
                // idempotence guarantees it is the same id.
                MR_EMIT("mobile_claim_stale_id", EF_I("claimed", j.proposed_node_id),
                        EF_I("reserved", _pending_mobile_offers[own].proposed_id),
                        EF_I("key", static_cast<int64_t>(j.key_hash32)));
                return;
            }
            // §6.4 S6: a CLAIM whose local id collides a DIFFERENTLY-keyed hosted mobile. ⚠ THE LEGACY
            // CONCURRENT-CLAIM COMPATIBILITY PATH — it is NO LONGER the primary defence, and its old rationale is
            // WITHDRAWN: that comment said "find_free_mobile_id reserves nothing until CLAIM, so two mobiles can be
            // offered the same free id", which [[B137]] made FALSE — an id is now reserved from OFFER ADMISSION and
            // `find_free_mobile_id` EXCLUDES live reservations, so four concurrent OFFERs propose four unique ids.
            // ⇒ this branch survives as the RACE BACKSTOP the owner ruled it to be (a CLAIM with no live claimant
            // reservation — aged out, or from a peer that never took one). Do NOT
            // last-write-wins (two hosted mobiles sharing one id -> the home last-miles ambiguously) and do NOT record the
            // colliding claim. TARGETED DENY the LOSER (claimant_key_hash32 = its hash) so ONLY it yields + re-registers
            // (re-DISCOVER -> a fresh id, now excluding the taken one); the RECORDED mobile ignores the DENY (hash mismatch).
            // This makes the mobile-home path recover like static/team DAD (collision -> the loser re-picks) instead of the
            // old broadcast re-OFFER, which the recorded mobile would ALSO adopt (it isn't addressed). See node_join DENY handler.
            // ⓘ Reached only for a CLAIM that is NOT stale (above): either the claimant holds no live promise, or it
            //   holds one for THIS very id — in both cases the frame really is about the id it names.
            for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
                if (_active->_mobile_reg[i].mobile_local_id == j.proposed_node_id && _active->_mobile_reg[i].key_hash32 != j.key_hash32) {
                    addr_conflict_send_deny(j.proposed_node_id, _active->_mobile_reg[i].key_hash32, j.key_hash32, J_DENY_CONFLICT);
                    MR_EMIT("mobile_id_collision_deny", EF_I("id", j.proposed_node_id), EF_I("loser", static_cast<int64_t>(j.key_hash32)));
                    // ★ §MH-S2 [[B137]]: the loser's reservation (if it still holds one) is RELEASED here, not left to
                    // age out. Its re-DISCOVER must be free to draw a DIFFERENT id, and `find_free_mobile_id` is
                    // idempotent per key — a retained reservation would hand it the SAME losing id forever.
                    // ⓘ SAFE ONLY BECAUSE OF THE ORDER ABOVE: the slot released here can no longer belong to a newer
                    //   transaction — a promise for a DIFFERENT id already returned.
                    mobile_offer_release(j.key_hash32);
                    return;   // do NOT record the colliding claim; the targeted DENY re-registers the loser
                }
            if (own < 0 && foreign >= 0) {
                // The claimed id is PROMISED TO SOMEBODY ELSE and this claimant holds no promise of its own.
                // Recording it would manufacture the very collision the reservation prevents, one CLAIM later.
                // ⇒ the SAME targeted-DENY backstop the registered-row branch uses (U1), with the reservation
                // HOLDER as the owner, so only the late claimant yields and re-DISCOVERs onto a fresh id.
                addr_conflict_send_deny(j.proposed_node_id, _pending_mobile_offers[foreign].target_key_hash32,
                                        j.key_hash32, J_DENY_CONFLICT);
                MR_EMIT("mobile_claim_reserved_elsewhere", EF_I("id", j.proposed_node_id),
                        EF_I("holder", static_cast<int64_t>(_pending_mobile_offers[foreign].target_key_hash32)),
                        EF_I("loser", static_cast<int64_t>(j.key_hash32)));
                mobile_offer_release(j.key_hash32);   // idempotent: frees an ELAPSED slot this key may still occupy
                return;
            }
            // Remaining cases both record: (a) `own >= 0` AND the ids MATCH — the normal handshake, the promise
            // kept; (b) `own < 0 && foreign < 0` — a late CLAIM whose reservation has aged out, against an id
            // nobody else is promised. (b) is the pre-[[B137]] compatibility path and is deliberately retained:
            // the reservation is an upper bound on a leak, not a licence to reject a mobile that took its time.
            int slot = -1;
            for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
                if (_active->_mobile_reg[i].key_hash32 == j.key_hash32) { slot = static_cast<int>(i); break; }
            if (slot < 0 && _active->_mobile_reg_n < protocol::cap_host_mobiles) slot = _active->_mobile_reg_n++;
            if (slot < 0) return;                                     // registry full -> drop (the mobile re-DISCOVERs elsewhere)
            _active->_mobile_reg[static_cast<uint8_t>(slot)] =
                { j.key_hash32, j.proposed_node_id, j.claim_epoch, _hal.now() };
            _active->_mobile_snr_q4[static_cast<uint8_t>(slot)] = protocol::db_to_q4(meta.snr_db);   // §S6: seed the per-mobile SNR EWMA from the CLAIM
            MR_EMIT("mobile_registered", EF_I("key", static_cast<int64_t>(j.key_hash32)),
                    EF_I("local_id", j.proposed_node_id), EF_I("epoch", j.claim_epoch));
            // ★ §MH-S2 [[B137]]: THE MATCHING CLAIM — the reservation has done its job and is released NOW rather than
            // waiting out `mobile_offer_reservation_ms`. The registry row takes over as the thing that holds the id
            // (`find_free_mobile_id`'s first loop), so the promise is never dropped, only handed on. Releasing also
            // frees the ring slot for the next discovering mobile, which is what keeps 8 slots serving 16 hosted
            // mobiles. ⓘ Keyed by HASH, matching whatever slot this mobile holds — ⛔ never by the claimed id, which
            // a race backstop may have already reassigned.
            mobile_offer_release(j.key_hash32);
            presence_notify_old_home(j.key_hash32, j.proposed_node_id, j.claim_epoch);   // §S6.4-D: NEW home -> old-home redirect breadcrumb (D10; stashed at OFFER time)
            presence_schedule_roster();                               // §S6: roster on a registry change (coalesced)
            return;                                                   // do NOT fall into the static DAD tie-break
        }
        // §mobile separation: a MOBILE (incl. an off-grid team member, node_id==_team_local_id) is NOT on the static DAD
        // plane — its id is a LOCAL id, not a global identity. It must NEVER defend/learn a STATIC claimant against that
        // local id: a DENY would leak id_bind(local_id -> mobile_hash) fleet-wide + evict the legit static node claiming
        // its own global id. Mirror the beacon self-defense guard (node_beacon.cpp `!_cfg.is_mobile`).
        if (_cfg.is_mobile) return;
        const uint8_t proposed = j.proposed_node_id;
        bool conflict = false; uint32_t owner_key = _key_hash32; uint8_t reason = J_DENY_CONFLICT;
        if (_joined && proposed == _node_id && j.key_hash32 != _key_hash32) {           // (a) my adopted id
            conflict = true;
        } else {                                                                        // (b) a known binding, other hash
            for (uint16_t i = 0; i < _active->_id_bind_n; ++i)
                if (_active->_id_bind[i].node_id == proposed && _active->_id_bind[i].key_hash32 != j.key_hash32) {
                    conflict = true; owner_key = _active->_id_bind[i].key_hash32; break;
                }
        }
        if (!conflict && _join_claim.active && _join_claim.proposed == proposed         // (c) simultaneous claim
            && j.key_hash32 != _key_hash32) {
            if (join_tiebreak_wins(_join_claim.claim_epoch, _key_hash32, j.claim_epoch, j.key_hash32)) {
                conflict = true; owner_key = _key_hash32; reason = J_DENY_PENDING_CLAIM;
            } else {                                                                    // I lose -> drop my claim, retry
                _join_claim.active = false; _hal.cancel(kJoinClaimGuardTimerId);
                join_deny_id(proposed);
                MR_EMIT("join_claim_denied", EF_I("denied_node_id", proposed), EF_S("reason", "simultaneous_claim_lost"));
                (void)_hal.after(protocol::join_retry_backoff_ms, kJoinRetryTimerId);
                return;
            }
        }
        if (conflict) addr_conflict_send_deny(proposed, owner_key, j.key_hash32, reason);
        else          id_bind_set(proposed, j.key_hash32, IdBindSource::bcn, IdBindConf::claimed);  // learn the claim
        return;
    }

    if (j.opcode == static_cast<uint8_t>(j_opcode::deny)) {
        // §W2c team-DAD L2a mediation: a TEAM-scoped mediated DENY from a shared-neighbour observer (B) — it saw our
        // _team_local_id ALSO claimed by a DIFFERENT-keyed teammate we can't hear (a hidden-terminal collision the
        // direct beacon compare, node_beacon.cpp:749, can never catch). §18 HARD SPLIT: a team-scoped DENY is NEVER
        // processed on the static id_bind plane — it returns here before the static handler, so a static node (team_id 0)
        // / an other-team member (team_id mismatch) / the WINNER (claimant != its key) all just DROP it. Only the LOSER
        // (our team, our id, claimant == our key, owner == the winner's key) re-picks via the existing tentative window.
        if (j.team_scoped) {
#if MR_FEAT_TEAM
            if (_cfg.is_mobile && _cfg.team_id != 0 && j.team_id == _cfg.team_id
                && _team_local_id != 0 && j.denied_node_id == _team_local_id
                && j.claimant_key_hash32 == _key_hash32 && j.owner_key_hash32 != _key_hash32) {
                MR_EMIT("team_dad_mediated_deny_rx", EF_I("id", _team_local_id),
                        EF_I("winner", static_cast<int64_t>(j.owner_key_hash32)));
                team_dad_fire();   // re-pick (a re-pick ignores the pin -> a fresh id) + re-arm the guard window
            }
#endif
            return;   // a team-scoped DENY never falls into the static id_bind plane (§18)
        }
#if MR_FEAT_MOBILE
        // §S6: our HOST bounced our local id (concurrent-OFFER collision) — a DENY TARGETED at us (claimant == our hash) for
        // our adopted id. Re-register (mobile_reset_registration -> re-DISCOVER -> a fresh, collision-free id). Handled BEFORE
        // id_bind_set so a mobile LOCAL id never enters the static id_bind plane (§mobile separation), and before the static
        // tiebreak (which would forced_rejoin onto the STATIC DAD plane — wrong for a host-registered mobile).
        if (_cfg.is_mobile && _my_mobile_reg.active && j.denied_node_id == _node_id && j.claimant_key_hash32 == _key_hash32) {
            MR_EMIT("mobile_id_denied", EF_I("id", _node_id), EF_I("home", _my_mobile_reg.home_id));
            // ★ §MH-S4 §7.1 step 7 — "a targeted collision DENY still immediately abandons the provisional id and
            // re-enters discovery": the pre-existing behaviour, UNCHANGED, and it deliberately does NOT spend a
            // re-CLAIM retry — a DENY is the home speaking, not silence. `MobileAttemptResult::denied` was declared
            // by §MH-S1 as "set by S4"; this is its one and only writer, so the code no longer promises a surface
            // it cannot fill (MARK DONE-VS-MISSING IN CODE).
            _mobile_last_result = MobileAttemptResult::denied;
            mobile_reset_registration("mobile_id_collision");
            (void)_hal.after(0, kMobileDiscoverTimerId);          // re-DISCOVER now -> a fresh id (find_free_mobile_id excludes the id the recorded mobile holds)
            return;
        }
#endif
        id_bind_set(j.denied_node_id, j.owner_key_hash32, IdBindSource::bcn, IdBindConf::claimed);   // learn the owner
        if (_joined && j.denied_node_id == _node_id
            && j.claimant_key_hash32 == _key_hash32 && j.owner_key_hash32 != _key_hash32) {
            const bool i_win = join_tiebreak_wins(_claim_epoch, _key_hash32, j.owner_claim_epoch, j.owner_key_hash32);
            MR_EMIT("addr_conflict_tie_break", EF_I("node", _node_id), EF_B("i_win", i_win), EF_I("my_claim_epoch", _claim_epoch),
                    EF_I("their_claim_epoch", j.owner_claim_epoch), EF_I("their_key_hash32", static_cast<int64_t>(j.owner_key_hash32)));
            if (!i_win) forced_rejoin("addr_conflict_lost");
        }
        return;
    }
    if (j.opcode == static_cast<uint8_t>(j_opcode::discover)) {       // §mobile 2a: host side of mobile registration
        if (!j.is_mobile) return;                                     // a static node never DISCOVERs -> ignore (still deferred)
        if (!can_host_mobiles()) return;                              // §B132: the ONE eligibility invariant (node.h) — a mobile never hosts, a static node can opt OUT (B3), and a GATEWAY is never a home (half its schedule is on the other leaf). Was `is_mobile || !host_mobiles`, which let a strong-SNR gateway win the home.
        if (_node_id == 0) return;                                    // §clean-join: no host OFFER while unprovisioned/mid-DAD (reset_join_for_reprovision set_identity(0)'d us; adopt restores the id right before _joined). NOT `!_joined`: an operator-pinned host (`cfg set node_id` -> b.joined=0, "won't auto-yield") has _joined==false FOREVER and must keep hosting. Bonus: kills the absurd responder_node_id=0 OFFER.
        const uint8_t local = find_free_mobile_id(j.key_hash32);
        if (local == 0) return;                                       // pool full -> stay silent (the mobile picks another host)
        j_offer_in off{}; off.leaf_id = _cfg.leaf_id; off.gateway_capable = false; off.is_mobile = true;
        off.responder_node_id = _node_id; off.responder_key_hash32 = _key_hash32;
        off.data_sf_bitmap = static_cast<uint8_t>(_cfg.allowed_sf_bitmap & 0xFF);   // F-SF-1 (2026-07-19): ADVISORY only — the mobile keeps its OWN configured sf_list, so this low byte is never consumed; it rides purely as a misconfig diagnostic (its SF>=8 truncation is therefore harmless)
        off.proposed_mobile_id = local;
        off.target_key_hash32  = j.key_hash32;                        // §S6: ADDRESS the OFFER at the discovering mobile (only its hash adopts it — a broadcast OFFER heard by another mobile is now ignored, killing the "wrong mobile adopts a foreign id" leg of the concurrent-register race)
        // §S6.4-D: if this DISCOVER carries a last_home (a re-home), stash it so a subsequent CLAIM (adopt) makes THIS
        // (new) home originate the old-home notify (D10). Dedup/refresh by mobile hash; evict-oldest on overflow.
        if (j.last_home_id != 0 && j.last_home_id != _node_id) {
            int ni = -1;
            for (uint8_t i = 0; i < _active->_notify_pending_n; ++i)
                if (_active->_notify_pending[i].mobile_hash == j.key_hash32) { ni = i; break; }
            if (ni < 0) {
                if (_active->_notify_pending_n < protocol::cap_host_mobiles) ni = _active->_notify_pending_n++;
                else {   // §3-A.6/P2-6: full -> evict the STALEST stash (min stash_ms), not slot 0
                    ni = 0;
                    for (uint8_t i = 1; i < _active->_notify_pending_n; ++i)
                        if (_active->_notify_pending[i].stash_ms < _active->_notify_pending[ni].stash_ms) ni = i;
                }
            }
            _active->_notify_pending[ni] = { j.key_hash32, j.last_home_id, j.last_home_layer, j.last_home_key_hash32, _hal.now() };   // §B4: + old-home hash for a cross-layer breadcrumb; §3-A.6: + stash_ms
        }
        uint8_t buf[13]; const size_t n = pack_j_offer(off, std::span<uint8_t>(buf, sizeof buf));
        if (n) {
            // §S6/QA-3b: DE-STORM the OFFER — stash it + fire after a random backoff so two co-located hosts don't answer
            // this DISCOVER at the SAME ms (the same-ms PHY collision that made a mobile adopt the WEAKER home). Reuses the
            // join OFFER-backoff window.
            // ★★ §MH-S2 §5.3.2 — NO LONGER A SINGLE SLOT. `mobile_offer_admit` owns the keyed ring, the coalesce, the
            // fit-before-draw guard, the id reservation and the deadline re-arm; the EMIT stays HERE and stays BEFORE
            // it, so its "the OFFER is committed" meaning is unchanged for every existing reader — but it is now
            // emitted only when a slot is actually taken, because a `full` or `duplicate` admission commits nothing.
            // ★★ §MH-S1b §6.2/§10 — RENAMED FROM `mobile_offer_tx`, WHICH IS THE POINT. §10: *"an event named
            // `mobile_offer_tx` must not continue to mean only 'copied into a stash'"*, and this site IS the
            // stash — the frame does not reach the radio for another 100..1000 ms and may never reach it at
            // all. `mobile_offer_scheduled` says what actually happened here; the honest `mobile_offer_tx` is
            // now raised in `lbt_complete` at the accepted handoff, and `mobile_offer_dropped` on a refusal.
            // Those three ARE §10's required "scheduled / transmitter-admitted / confirmed" distinction (the
            // third is S4's).
            //
            // ⚠ THE ORDER BELOW IS LOAD-BEARING FOR THE RNG STREAM, not a style choice. With ONE discovering mobile
            // the sequence is still exactly: pick id (no draw) -> pack -> emit -> ONE `rand_range` inside the admit ->
            // ONE `_hal.after`. Identical count, identical position ⇒ the single-mobile mobile plane does not
            // re-anchor here (S3 is the arc's only planned re-anchor). A `duplicate` or `full` admission draws
            // NOTHING, which is the other half of that promise.
            const MobileOfferAdmit r = mobile_offer_admit(j.key_hash32, buf, n, local);
            if (r == MobileOfferAdmit::armed) {
                MR_EMIT("mobile_offer_scheduled", EF_I("to_key", static_cast<int64_t>(j.key_hash32)), EF_I("local_id", local));
            } else if (r == MobileOfferAdmit::duplicate) {
                // §5.3.3: this mobile already has an ARMED OFFER. Coalesce — no second slot, deadline UNMOVED, id
                // RETAINED. It is a distinct event from `mobile_offer_scheduled` on purpose: nothing was scheduled.
                MR_EMIT("mobile_offer_coalesced", EF_I("to_key", static_cast<int64_t>(j.key_hash32)), EF_I("local_id", local));
            } else if (r == MobileOfferAdmit::full) {
                // §5.3.2 item 5 / gate 19: refuse EXPLICITLY and disturb nothing. The mobile's own DISCOVER retry is
                // the backstop — ⛔ evicting another mobile's armed entry to make room is exactly the §S0-1 defect.
                MR_EMIT("mobile_offer_ring_full", EF_I("to_key", static_cast<int64_t>(j.key_hash32)),
                        EF_I("pending", mobile_offers_pending_n()));
            }
        }
        return;
    }
#if MR_FEAT_MOBILE
    if (j.opcode == static_cast<uint8_t>(j_opcode::offer)) {          // §mobile 2b: mobile-side OFFER collector
        if (!_cfg.is_mobile || !j.is_mobile || _my_mobile_reg.active) return;   // only an UNREGISTERED mobile collects; a static node -> ignore (deferred)
        if (j.target_key_hash32 != _key_hash32) return;              // §S6: only collect an OFFER ADDRESSED TO US — a broadcast OFFER meant for a concurrently-registering mobile no longer gets adopted by the wrong one
        if (_mobile_offers_n < protocol::cap_mobile_offers)
            _mobile_offers[_mobile_offers_n++] = { j.responder_node_id, j.responder_key_hash32,
                                                   j.proposed_mobile_id, meta.snr_db,
                                                   j.leaf_id, j.data_sf_bitmap };   // §mobile: keep the host's leaf + sf_list to adopt on CLAIM
        return;
    }
#endif
}

// The id's owner defends it: send a J_DENY carrying our claim_epoch so the impostor runs the tiebreak (§6)
// in its DENY handler and yields if it loses. Called from handle_j (a heard claim) + the beacon collision.
void Node::addr_conflict_send_deny(uint8_t node_id, uint32_t owner_key, uint32_t claimant_key, uint8_t reason,
                                   bool team_scoped, uint32_t team_id) {
    j_deny_in in{};
    in.leaf_id = _cfg.leaf_id; in.gateway_capable = _cfg.is_gateway; in.is_mobile = _cfg.is_mobile;
    in.denied_node_id = node_id; in.owner_key_hash32 = owner_key; in.claimant_key_hash32 = claimant_key;
    in.owner_lease_age_seconds = 0;                                    // telemetry only (§6)
    in.owner_claim_epoch = _claim_epoch; in.reason = reason;
    in.team_scoped = team_scoped; in.team_id = team_id;                // §W2c: a team-mediated DENY (19-B) carries the mediator's team_id
    uint8_t buf[19];
    const size_t n = pack_j_deny(in, std::span<uint8_t>(buf, sizeof buf));
    MR_EMIT("join_deny_sent", EF_I("denied_node_id", node_id), EF_I("claimant_key_hash32", static_cast<int64_t>(claimant_key)),
            EF_I("reason", reason));
    if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// The join-FSM reset shared by forced_rejoin (heal) + the console reprovision verbs (join/create/leave, fw_main).
// CRITICAL: clears `_joined` — without it, `set_identity(0)` leaves the node "joined" and CmdKind::join is
// idempotent-once-joined (no claim, no J, node_id stuck) → a reprovision never re-DADs. On a joined node it also
// denies the prior id + drops our own (prior, key) binding so the re-DAD picks a FRESH id (not the same one).
void Node::reset_join_for_reprovision() {
    if (_joined) {                                                     // joined-only cleanup (a fresh node skips it)
        const uint8_t prior = _node_id;
        join_deny_id(prior);                                          // don't let the picker immediately re-pick it
        for (uint16_t i = 0; i < _active->_id_bind_n; ++i)            // drop our own (prior, myhash) binding
            if (_active->_id_bind[i].node_id == prior && _active->_id_bind[i].key_hash32 == _key_hash32) {
                for (uint16_t k = i; k + 1 < _active->_id_bind_n; ++k) _active->_id_bind[k] = _active->_id_bind[k + 1];
                _active->_id_bind_n--; break;
            }
    }
    _joined = false;
    _join_claim.active = false;
    _hal.cancel(kJoinClaimGuardTimerId);
    set_identity(protocol::unjoined_node_id, _key_hash32);             // 0 = unprovisioned (transient; the caller re-claims)
}

// Lost the heal tiebreak: yield the id, deny it, drop our stale self-binding, go unprovisioned, and re-run DAD.
void Node::forced_rejoin(const char* reason) {
    if (!_joined) return;
    // telemetry-only (stripped on device) — the MR_EMIT below is its ONLY consumer. ⚠ NOT the same as the
    // same-named local in reset_join_for_reprovision() above: THAT one is live code (join_deny_id + the _id_bind
    // scan compare against it), so it must NOT get this attribute.
    [[maybe_unused]] const uint8_t prior = _node_id;                  // capture BEFORE the reset zeroes _node_id
    reset_join_for_reprovision();
    MR_EMIT("addr_conflict_forced_rejoin", EF_I("prior_node_id", prior), EF_S("reason", reason ? reason : "addr_conflict_lost"));
    join_start_claim(reason);
}

// ---- L2c: delivery-driven heal + redirect (design §7.1) ----------------------------------------------
// A DM arrived addressed to OUR node_id, but its cleartext DST_HASH names a DIFFERENT key — an id
// collision misdelivered it. Two jobs, both obeying the ONE key-only tiebreak (§6) so every heal path
// agrees on the loser: (1) the DM must still reach its real owner (redirect by hash), (2) the duplicate
// id must heal (the higher-key holder yields). The suppression ring bounds a redirect loop while the
// collision is still unhealed (a poisoned binding could otherwise resolve straight back to our own id).
bool Node::l2c_redirected_recently(uint32_t want_hash) {
    return recent_ring_hit(_l2c_redirect, _l2c_redirect_n, L2cRedirect{ want_hash, 0 },
                           _hal.now(), protocol::l2c_redirect_suppress_ms);
}
void Node::l2c_mark_redirected(uint32_t want_hash) {
    recent_ring_mark(_l2c_redirect, _l2c_redirect_n, L2cRedirect{ want_hash, _hal.now() });
}
void Node::l2c_handle_misdelivery(const PostAck& pa, uint32_t want_hash) {
    MR_EMIT("l2c_misdelivery", EF_I("node", _node_id), EF_I("origin", pa.origin), EF_I("ctr", pa.ctr),
            EF_I("want_hash", static_cast<int64_t>(want_hash)));
    // REDIRECT — FORWARD the DM toward want_hash's real owner WITHOUT re-originating: the full inner (incl.
    // DST_HASH) + origin/ctr/flags ride through unchanged, so sender attribution, the E2E-ack target, and the
    // (origin,ctr) dedup all stay intact (a re-`send` would corrupt all three — the review's #1 bug). The leg
    // is re-budgeted from OUR route to the owner (l2c_enqueue_forward, originator-style), NOT inherited from the
    // inbound DM (whose remainder is irrelevant / may have arrived exhausted — the review's hop-budget bug).
    //
    // If we hold a fresh AUTHORITATIVE owner binding (and it isn't us), forward NOW — floodless, so it is NOT
    // suppression-gated (every queued DM should reach the owner). Otherwise PARK + flood a HARD H; THAT path is
    // anti-flood-gated (one flood per hash per window). The resolution decides forward-vs-heal — want_hash back
    // to OUR id is a CONFIRMED same-id collision (heal), any other id means the recipient moved (forward, no
    // renumber). The HEAL is therefore confirmation-gated in drain_parked_sends, never blind here (design §7.1).
    IdBindConf conf = IdBindConf::claimed;
    const int rid = id_bind_find_by_hash(want_hash, &conf);
    if (rid >= 0 && conf == IdBindConf::authoritative && static_cast<uint8_t>(rid) != _node_id) {
        if (l2c_enqueue_forward(static_cast<uint8_t>(rid), pa.origin, pa.ctr, pa.ctr_lo, pa.flags, pa.type, pa.inner, pa.inner_len, pa.nonce_seed)) {
            // success only (queue-full already emitted the drop)
            MR_EMIT("l2c_redirect_forward", EF_I("origin", pa.origin), EF_I("ctr", pa.ctr), EF_I("to", rid));
        }
        return;                                                           // l2c_enqueue_forward always kicks the queue (success or drop)
    }
    if (l2c_redirected_recently(want_hash)) {                            // suppress only the PARK+flood path (anti-flood)
        MR_EMIT("l2c_redirect_suppressed", EF_I("want_hash", static_cast<int64_t>(want_hash)));
        become_free();
        return;
    }
    l2c_mark_redirected(want_hash);
    l2c_park_redirect(want_hash, pa);                                     // hold the DM for forward/heal-on-resolution
    emit_hash_query(want_hash, /*hard=*/true);                           // owner-authoritative resolution = the discriminator
    MR_EMIT("l2c_redirect_query", EF_I("want_hash", static_cast<int64_t>(want_hash)));
    become_free();
}

// Build + enqueue a fresh routing leg that carries an EXISTING DM (origin/ctr/inner preserved) to `to_id`. It
// keeps FORWARDER semantics (is_forward=true): a no-route transit DM is DROPPED, NOT deferred — a relay must
// not hold (or surface a local `send_failed` for) someone else's DM. But the redirect goes to a DIFFERENT
// destination than the inbound DM, so (a) `previous_hop=0` removes the upstream-loop exclusion (a re-targeted
// leg may legitimately route back through the inbound hop) and (b) the hop budget is FRESHLY DERIVED from OUR
// route to `to_id` — never inherited from the inbound DM's remainder, which is irrelevant and (for a DM that
// arrived at us exhausted) would underflow to the 31-hop max. Identity rides in origin/ctr/inner. ALWAYS kicks
// the queue (`become_free`) so the half-duplex serializer can't stall; returns false (and emits) on queue-full.
bool Node::l2c_enqueue_forward(uint8_t to_id, uint8_t origin, uint16_t ctr, uint8_t ctr_lo, uint8_t flags,
                               uint8_t type, const uint8_t* inner, uint8_t inner_len, const uint8_t nonce_seed[8]) {
    if (_active->_tx_queue_n >= kTxQueueCap) {
        MR_EMIT("l2c_redirect_dropped_queue_full", EF_I("to", to_id), EF_I("origin", origin), EF_I("ctr", ctr));
        become_free();                                                    // keep the queue serviced even on drop (codebase contract)
        return false;
    }
    TxItem it{};
    it.origin = origin; it.dst = to_id; it.ctr = ctr; it.ctr_lo = ctr_lo; it.flags = flags; it.type = type;   // S1/M7a: a misdelivered typed frame (E2E_ACK/H_ANSWER) keeps its type on the redirect
    it.is_forward = true;                                                 // forwarder: drop (not defer/push) a no-route transit DM
    it.previous_hop = 0;                                                  // re-targeted leg: no upstream-loop exclusion (node 0 is the no-op sentinel)
    RtEntry* rte = rt_find(to_id);                                        // FRESH budget from our route to the owner
    const uint8_t rt_hops = (rte && rte->n > 0) ? rte->candidates[0].hops : 1;
    const int rem = static_cast<int>(rt_hops) + protocol::hop_budget_slack;
    it.fwd_remaining = static_cast<uint8_t>(rem > protocol::hop_budget_max_initial ? protocol::hop_budget_max_initial : rem);
    it.fwd_committed = 0;
    it.inner_len = (inner_len > protocol::max_payload_bytes_hard_cap) ? protocol::max_payload_bytes_hard_cap : inner_len;
    for (uint8_t i = 0; i < it.inner_len; ++i) it.inner[i] = inner[i];
    for (int i = 0; i < 8; ++i) it.nonce_seed[i] = nonce_seed[i];          // §1c: CRYPTED re-tx carries the originator's seed verbatim (zero for plaintext)
    it.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = it;
    become_free();
    return true;
}

// The HARD-H resolution proved want_hash's owner holds OUR node_id => a genuine same-id collision (not a
// stale sender binding). Called AFTER the drain loop (NOT mid-loop) so forced_rejoin's identity mutation can't
// corrupt a sibling parked entry. Heal by the §6 key-only tiebreak: lower key keeps + DENYs the squatter;
// higher yields. Renumber only fires for a DAD-joined node (forced_rejoin's `!_joined` guard) — a cfg/NV-
// provisioned id is operator-owned, surfaced (collision_confirmed healed=false) rather than auto-reassigned.
// The DM that exposed the collision was dropped at the drain (forwarding-to-self loops); it is recovered by
// the sender's retry once the heal converges (consistent with the in-window-drop residual, design §7.1).
void Node::l2c_confirmed_collision(uint32_t want_hash) {
    const bool i_win = join_tiebreak_wins(0, _key_hash32, 0, want_hash);
    MR_EMIT("l2c_collision_confirmed", EF_I("node", _node_id), EF_I("want_hash", static_cast<int64_t>(want_hash)),
            EF_I("my_key", static_cast<int64_t>(_key_hash32)), EF_B("i_win", i_win), EF_B("healed", i_win || _joined));
    if (i_win) addr_conflict_send_deny(_node_id, _key_hash32, want_hash, J_DENY_MEDIATED);  // squatter must yield
    else       forced_rejoin("l2c_collision_confirmed");                                    // we are the squatter -> yield
}

// ============================================================================
// §S6 presence plane — HOME side (always compiled; a home is a static node). Host-gated by _active->_mobile_reg_n /
// host_mobiles -> a non-host is a cheap type-drop + a static-only mesh is byte-identical (no probes exist).
// ============================================================================

// §S6/D6: the layer-directory version this node advertises in its roster. A gateway derives a 1-byte epoch over its
// own layer PHY set (bump on a provisioning change); a plain home has n_layers==1 -> 0 (the full type-4-TLV
// gw-epoch propagation + XOR aggregate is DEFERRED — this keeps the roster's dir_epoch stable, no spurious pulls).
uint8_t Node::presence_compute_dir_epoch() const {
    if (_n_layers < 2) return 0;
    uint8_t e = 0;
    for (uint8_t i = 0; i < _n_layers; ++i) {
        e ^= _cfg.layers[i].layer_id;
        e ^= static_cast<uint8_t>(protocol::mhz_to_khz(_cfg.layers[i].freq_mhz));
        e ^= static_cast<uint8_t>(_cfg.layers[i].routing_sf);
    }
    return e;
}

// §3-D: refresh a hosted mobile's proxy-liveness clock + step its per-mobile SNR EWMA (seed-if-zero).
// ★★ §B177-FIX (owner-ruled 2026-08-11, ledger §1.16) — **CORRECTED IN PLACE.** This header used to read *"The ONE
// updater shared by the probe path (presence_ingest_probe) and the beacon path (node_beacon.cpp) so the roster-tier feed
// can never again be present on one and skipped on the other."* **The BEACON CALLER IS GONE** (removed at
// `node_beacon.cpp`'s `if (b.is_mobile)` arm, with the reason and the withdrawn rationale recorded there): a beacon
// carries the hash but no `reg_epoch`, so it can never establish the row identity a refresh needs. ⇒ the ONE remaining
// caller is `presence_refresh_hosted_row`, i.e. **both P-probe arms**, each gated on `host_row_probe_refreshable()`.
// The CLAIM path deliberately does NOT use this — it SEEDS a fresh registry slot with a hard assign (a stale
// _mobile_snr_q4 tail slot must reset, not smooth). Caller checks bounds.
void Node::mobile_reg_touch(uint8_t slot, int16_t snr_q4) {
    _active->_mobile_reg[slot].last_heard_ms = _hal.now();
    int16_t& ew = _active->_mobile_snr_q4[slot];
    ew = protocol::snr_ewma_update(ew, snr_q4);   // seed-if-zero EWMA (canonical link-quality helper)
}

// ★★★ §MH-S5b — **THE ONE "A PROBE FROM THIS HOSTED MOBILE ARRIVED" REFRESH.** Contract at the declaration (node.h).
// It is the SELECTED-check arm's own pre-existing body, lifted verbatim so the SEARCHING arm can read the same one
// (U1) — `mobile_reg_touch` plus §S6 A.4 key custody with its `ed_pub[:4] == key_hash32` self-consistency test.
// ⛔ THE SELF-CONSISTENCY TEST IS NOT OPTIONAL AND IS NOT A FORMALITY: without it any node could push a pubkey under
//    somebody else's hash. `key_hash32_of` (identity.h) owns the LE(ed_pub[:4]) derivation — ⛔ never re-derive it.
// ⓘ BYTE-INERT FOR THE OLD CALLER, by construction: same two effects in the same order, no emit, no log. The only
//   behaviour change in this slice's host half is the NEW call site.
void Node::presence_refresh_hosted_row(uint8_t slot, const p_probe_out& p, int16_t snr_q4) {
    mobile_reg_touch(slot, snr_q4);                             // §3-D: last_heard_ms + seed-if-zero SNR EWMA (shared with the beacon path so the two can't drift)
    if (p.has_pubkey) {                                         // §S6 A.4: key custody rides the probe (RETIRES DATA_TYPE_MOBILE_PUBKEY_PUSH) — self-consistency check ed_pub[:4]==hash
        const uint32_t pk_hash = key_hash32_of(p.ed_pub);       // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
        if (pk_hash == p.key_hash32) {
            for (uint8_t k = 0; k < 32; ++k) _active->_mobile_reg[slot].ed_pub[k] = p.ed_pub[k];
            _active->_mobile_reg[slot].has_pubkey = true;
        }
    }
}

// ★★★ §MH-S5 §9.3 — **THE ONE REMOVAL PRIMITIVE.** Every hosted-row removal compacts `_mobile_reg` AND the
// parallel `_mobile_snr_q4` array, and it does so HERE, once, so a future parallel array cannot be added to one
// site and forgotten at the others. THREE call sites existed before this function and TWO of them had the loop
// open-coded (`evict_aliased_hosted_mobile`, `presence_ingest_probe`'s select-elsewhere prune); the third —
// timed expiry — did not exist at all, which is [[§S0-5]]'s defect.
// ⛔ NOT a "mark dead" flag: §9.1 requires PHYSICAL compaction, because the id must become allocatable
//    (`find_free_mobile_id` scans `_mobile_reg_n` rows) and the row must leave the roster (`presence_emit_roster`
//    iterates the same count). A tombstone would satisfy neither.
// ⓘ The caller owns the roster consequence: `evict_aliased_hosted_mobile` schedules one (the mobile learns from
//   its absence), the prune deliberately does not answer at all, and the age-out has nobody to tell. Scheduling
//   it here would put a roster on the air for a mobile that is 25 minutes dead.
//
// ⛔⛔ **THIS PRIMITIVE EMITS AND LOGS NOTHING, AND THAT IS AN ATTRIBUTION DECISION, NOT AN OVERSIGHT.** Both
// `MR_EMIT` and `_hal.log` land in the simulator's event stream (`FirmwareNode::simLog` -> `logScriptLog`), so a
// single new line here would move every corpus row that reaches either of the two PRE-EXISTING removal sites —
// making the S5 behaviour change unmeasurable against §12.2's byte-identity rule. The two old sites keep their
// own emits (`mobile_evict_alias`, `presence_prune_stale`) verbatim; only the genuinely NEW path
// (`mobile_reg_age_out`) adds an event, where a mover would be the behaviour change itself and attributable.
// ⇒ the refactor half of this slice is byte-inert BY CONSTRUCTION rather than by hope.
// ⓘ `reason` is consumed by the caller-side emits, not here; it is retained in the signature because §9.3 names
//   `mobile_reg_remove(slot, reason)` and a future administrative removal should not have to invent a channel.
void Node::mobile_reg_remove(uint8_t slot, [[maybe_unused]] const char* reason) {
    if (slot >= _active->_mobile_reg_n) return;                       // C2: never compact past the live count
    for (uint8_t k = slot; k + 1 < _active->_mobile_reg_n; ++k) {
        _active->_mobile_reg[k]    = _active->_mobile_reg[k + 1];
        _active->_mobile_snr_q4[k] = _active->_mobile_snr_q4[k + 1];
    }
    --_active->_mobile_reg_n;
    // The vacated tail is reset. ⓘ **VERIFIED INERT TODAY, kept as defence-in-depth and labelled as such (V1):**
    // the CLAIM record's aggregate init `{ hash, id, epoch, now }` re-initialises every later member from
    // `HostMobileEntry`'s DEFAULT MEMBER INITIALISERS (redirect_*, ed_pub, has_pubkey, name, name_len,
    // last_key_fwd_hash32, deleg_fail all have one), and `_mobile_snr_q4[slot]` is hard-assigned beside it — so no
    // live reader inherits a corpse and this assignment changes no behaviour. It exists so that a future PARTIAL
    // write into a recycled slot cannot silently inherit the previous mobile's pubkey, name or redirect.
    _active->_mobile_reg[_active->_mobile_reg_n]    = LayerRuntime::HostMobileEntry{};
    _active->_mobile_snr_q4[_active->_mobile_reg_n] = 0;
}

// ★★★ §MH-S5 §9.1/§9.2 — **THE 25-MINUTE PHYSICAL EXPIRY, DIRECT *AND* REDIRECT.** Until this slice
// `protocol::mobile_liveness_ms` had exactly ONE consumer — the hash-locate proxy gate — so past 25 minutes the
// home stopped answering FOR the mobile and changed nothing else: the row stayed in the registry, in every
// roster, and holding its local id for ever (§S0-5 pinned all three).
//
// ⛔⛔ **NO NEW TIMER ID, AND NONE WAS AVAILABLE.** `TimerWheel::kCap` is 91, the top allocated id is 90 and
// every id in between is taken, so this is a DEADLINE SCAN on the existing periodic `kAgingTimerId` sweep — the
// same shape §MH-S2 gave the OFFER ring on timer 80, and the `age_out_parked_sends` / `id_bind_age_out` /
// `age_out_rreq_last` idiom this sweep already hosts (U1). It is additionally called at the two points §9.3
// names where a STALE ROW WOULD CHANGE A DECISION, so the 60-second sweep granularity can never be observed by
// the two consumers that matter:
//   · `find_free_mobile_id` — before allocating an id or refusing because `cap_host_mobiles` is full;
//   · `presence_emit_roster` — before advertising the rows.
// ✅ **THE RESIDUAL THIS COMMENT USED TO CLAIM IS CLOSED BY §MH-S5-FIX ([[B173]]) — corrected in place, because a
//   reader acts on THIS text.** It said the channel-coverage readers "are `const` and are NOT hooked", so they could
//   over-cover a dead mobile for up to `rt_aging_check_period_ms` (60 s) past the boundary, "and the alternative was a
//   const_cast or a mutating call inside a const predicate". ⛔ That was a FALSE DICHOTOMY: there is a third option and
//   it is the one §9.3's *"do not duplicate age predicates at each consumer"* points at — ONE shared, non-mutating
//   `host_row_live_direct()` (node.h) that every consumer reads. Both coverage readers now consult it and stay `const`,
//   so a row past the boundary is excluded from coverage IMMEDIATELY, not one sweep later. ⓘ It also named
//   `flood_mark_direct_neighbours`, which does not exist — the function is `flood_set_my_coverage` (V1).
// ★★ §MH-S5-FIX2 (owner-ruled 2026-08-10, ledger §1.14): the same predicate now guards SIX more service paths, so the
//   sentence above ("the two consumers that matter") is about this SWEEP's two extra call sites, NOT about the reach of
//   the boundary — the full consumer list is at the predicate in `node.h`.
// ⓘ The sweep below is still what PHYSICALLY compacts; the predicate only stops a consumer ACTING on a doomed row.
//
// ★ ONE PREDICATE FOR BOTH ROW KINDS, and that is a design decision with a reason: §9.2 asks for the redirect
// lifetime to be "stamped at breadcrumb receipt" and then to "be removed under the same age-out sweep".
// `last_heard_ms` IS the row's lifetime clock and the breadcrumb handler now restamps it
// (`node_mac_rx.cpp`'s DATA_TYPE_MOBILE_BREADCRUMB arm), so the two lifetimes are the same arithmetic on the
// same field. ⛔ A SECOND `uint64_t redirect_stamp_ms` WAS CONSIDERED AND DECLINED: it would add 8 B ×
// cap_host_mobiles(16) × MR_N_LAYERS to `sizeof(Node)` (D2, the ten-env sweep) to store a value that is
// definitionally the last thing this home heard ABOUT this mobile. ⓘ Restamping is safe for the proxy gate
// because the redirect answer at `node_hashlocate.cpp` tests `redirect_home_id != 0` FIRST and is deliberately
// NOT liveness-gated, so a fresher stamp cannot resurrect a direct proxy answer for a redirected row.
void Node::mobile_reg_age_out() {
    if (_active->_mobile_reg_n == 0) return;                          // non-host / empty -> inert (static-mesh byte-identical)
    const uint64_t now = _hal.now();
    // Descending so a compaction cannot skip the row that slides into the vacated slot.
    for (uint8_t i = _active->_mobile_reg_n; i-- > 0; ) {
        if (!host_row_expired(i, now)) continue;   // §MH-S5-FIX: the boundary is spelled ONCE (node.h), shared with `host_row_live_direct`
        const bool redirect = (_active->_mobile_reg[i].redirect_home_id != 0);
        MR_EMIT("mobile_reg_expired", EF_I("local_id", _active->_mobile_reg[i].mobile_local_id),
                EF_I("key", static_cast<int64_t>(_active->_mobile_reg[i].key_hash32)),
                EF_I("age_ms", static_cast<int64_t>(now - _active->_mobile_reg[i].last_heard_ms)),
                EF_I("redirect", redirect ? 1 : 0));
        mobile_reg_remove(i, redirect ? "redirect_expiry" : "direct_expiry");
    }
}

// A probe heard (LEAF-FREE): refresh the hosted mobile's liveness + SNR EWMA + key custody, then schedule ONE
// coalesced roster. Answers ONLY for a mobile we CURRENTLY host (a `lost` probe from a hosted mobile = the
// one-way-deaf recovery). A probe from a non-hosted mobile is ignored (registration is the J plane's job, D8).
void Node::presence_ingest_probe(const uint8_t* frame, size_t len, const RxMeta& meta) {
    if (!can_host_mobiles()) return;                                 // §S6/QA-2 + ★§B132: only an ELIGIBLE HOST answers probes — the SAME invariant as the J DISCOVER->OFFER + CLAIM sides, now shared as ONE accessor (node.h) instead of re-spelled. ⚠ The prior text here asserted it was the "SAME gate" as the OFFER and it WAS — both spelled `is_mobile || !host_mobiles` and both omitted the gateway clause, so the identical defect existed at two sites.
    if (_node_id == 0) return;                                       // §S6/QA-1: mid-join/unprovisioned (reset_join_for_reprovision set_identity(0)) — do NOT re-accept registry state mid-transition. SAME predicate as the mobile-OFFER suspend (node_join DISCOVER), NOT _joined (a pinned host keeps _joined==false forever).
    auto p = parse_p_probe(std::span<const uint8_t>(frame, len));
    if (!p) return;
    const int16_t snr_q4 = protocol::db_to_q4(meta.snr_db);
    const uint8_t rx_tier = protocol::presence_quality_tier(snr_q4);
    const bool searching  = p->searching();
    const bool sel_me     = (p->selected_home_id == _node_id && p->selected_home_layer == active_layer_id());
    // find our hosted entry for this hash
    int mine = -1;
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == p->key_hash32) { mine = static_cast<int>(i); break; }
    if (mine >= 0 && !searching) {
        if (!sel_me) {                                              // §S6 rev2: the mobile selected ANOTHER home -> PRUNE my stale entry NOW (instant registry self-heal)
            const uint8_t m = static_cast<uint8_t>(mine);
            mobile_reg_remove(m, "probe_selected_elsewhere");   // §MH-S5 §9.3: the ONE compaction primitive (was open-coded here); emits nothing, so the line below stays the stream's only event
            MR_EMIT("presence_prune_stale", EF_I("was", m), EF_I("selected", p->selected_home_id));
            return;                                                 // do NOT answer (only the selected home does)
        }
        // sel_me: normal refresh + custody + SNR EWMA, then answer (ONLY the selected home answers a check probe)
        // ★★★★ §B177-FIX (owner-ruled 2026-08-11, ledger §1.16) — **THE SELECTED ARM NOW ASKS THE SAME TUPLE AS THE
        // SEARCHING ARM.** It used to refresh on `mine >= 0 && sel_me` alone, i.e. on a row found by **HASH ALONE** with
        // no `host_row_live_direct()` and no epoch term — so a REDIRECT row was restamped past §9.2's breadcrumb clock, an
        // EXPIRED row was resurrected before compaction (ledger §1.14), and a row from before a re-home was kept alive at
        // an old home. `sel_me` proves only that the probe names US as home. ⇒ ONE predicate for both arms (node.h),
        // never re-spelled. ⓘ The ANSWER is deliberately NOT gated: a probe that named us is still ingested and still
        // gets its coalesced roster, and that roster carries the row's OWN `reg_epoch` — which is exactly the evidence
        // `presence_ingest_roster` re-registers on (`node_mobile.cpp`, the epoch-mismatch arm). Refusing the refresh
        // therefore self-heals rather than going quiet (C2).
        if (host_row_probe_refreshable(static_cast<uint8_t>(mine), p->reg_epoch))
            presence_refresh_hosted_row(static_cast<uint8_t>(mine), *p, snr_q4);   // §MH-S5b: the ONE refresh, shared with the searching arm below (was inline here)
        MR_EMIT("presence_probe_rx", EF_I("m", mine), EF_I("snr_q4", _active->_mobile_snr_q4[mine]));
        presence_schedule_roster();                                 // coalesced answer (rate-limit floored)
        return;
    }
    if (searching) {                                                // §S6 rev2: EVERY home answers a searching probe (candidate canvass), incl. non-hosts — with the ECHO of how WE heard IT (D14/D15)
        // ★★★★ §MH-S5b ITEM 2 — **A SEARCHING PROBE FROM A MOBILE WE ACTUALLY HOST REFRESHES ITS ROW.** Until this
        // slice the refresh lived on the `!searching` arm ONLY, so every searching probe — the §MH-S4b claiming
        // solicitation, the home-loss recovery canvass, and now §8.3's weak/missed-home canvass (item 1) — left the
        // hosted row's `last_heard_ms` and its per-mobile SNR EWMA untouched. §9.1 says the opposite in as many
        // words: "mobile beacons/**probes** refreshing `last_heard_ms`".
        // ⛔⛔ **AND IT IS WHAT STOPS ITEM 1 FROM CREATING A DEFECT.** With §MH-S5's rows now MORTAL, a permanently
        //    weak-home mobile would probe every 1-8 minutes on a kind that never re-stamped its own row. ⓘ STATED
        //    HONESTLY BECAUSE IT WAS MEASURED (V1) RATHER THAN INHERITED: eviction at `mobile_liveness_ms` was NOT
        //    imminent, because a hosted mobile's periodic BEACON also called `mobile_reg_touch` (`node_beacon.cpp`).
        //    The real residue item 2 closes is (a) a beacon SKIPPED by the R4.3 budget tier (a live corpus path) and
        //    (b) the SNR-EWMA feed, i.e. the roster quality tier the mobile's own re-home decision reads back.
        //    ★★ §B177-FIX UPDATE (2026-08-11, ledger §1.16): **that beacon caller no longer exists** — the sentence above
        //    is kept as the audit trail of what was true when item 2 landed, and its consequence has STRENGTHENED rather
        //    than weakened: with the beacon out of the registry entirely, item 2 is no longer a redundancy over a second
        //    refresh path but **one of the only two refresh paths there are** (this arm and the SELECTED arm above).
        //
        // ★★★ THE IDENTITY IS THE TUPLE, NOT THE HASH (the [[B147]]/[[B174]] shape, twice-learned):
        //   · `host_row_live_direct` — ⛔ a REDIRECT row is a breadcrumb whose clock §9.2 gives to the BREADCRUMB, and
        //     an EXPIRED row must get no service before compaction (ledger §1.14). Refreshing either would RESURRECT
        //     a row this arc spent two slices making mortal. ONE predicate, ten-plus consumers, not re-spelled.
        //   · `epoch` — ⛔ AND THIS TERM IS LOAD-BEARING, NOT BELT-AND-BRACES. A searching probe names NO home
        //     (`selected_home_id == 0` is what makes it searching), so the `sel_me` test that protects the arm above
        //     is unavailable here. Without an epoch match, an OLD home holding a row from BEFORE a re-home would
        //     refresh it from the mobile's canvass — and item 1 removes the `presence_prune_stale` self-heal that
        //     used to reap it (a selected probe prunes; a searching probe must not, because it selects nobody). The
        //     mobile bumps `_my_mobile_reg.epoch` on every fresh CLAIM, so a stale row's epoch cannot match.
        //     ⓘ The comparison is the low byte, deliberately: that is the width the wire carries and the same
        //       arithmetic `presence_ingest_roster` already uses at the mobile end.
        // ★ §B177-FIX: those two terms are now the shared `host_row_probe_refreshable()` (node.h) — SAME two terms, same
        //   order, no behaviour change on this arm; the SELECTED arm above reads the identical predicate so the two can
        //   never again disagree about which row a probe may refresh.
        if (mine >= 0 && host_row_probe_refreshable(static_cast<uint8_t>(mine), p->reg_epoch))
            presence_refresh_hosted_row(static_cast<uint8_t>(mine), *p, snr_q4);
        if (!_active->_roster_echo_pending) {                       // first probe of the window wins the echo (D15)
            _active->_roster_echo_hash = p->key_hash32; _active->_roster_echo_q = rx_tier; _active->_roster_echo_pending = true;
        }
        MR_EMIT("presence_probe_rx", EF_I("searching", 1), EF_I("snr_q4", snr_q4));
        presence_schedule_roster();
        return;
    }
    // a check probe for a hash we don't host -> ignore
}

// ============================================================================================================
// ★★★ §MH-S2 §5.3.2/§5.3.3 — THE KEYED PENDING-OFFER RING + [[B137]]'s PENDING-ID RESERVATION.
//
// WHAT IT REPLACES: one 13-byte `LayerRuntime::_pending_offer` slot armed through `jtx_stash_arm`, i.e. "last
// DISCOVER wins" — a second mobile's DISCOVER inside the 100..1000 ms jitter destroyed the first mobile's targeted
// OFFER (§S0-1 reproduces it, and this slice rewrites that case in place per B101).
//
// ★ THE PRECEDENT IT FOLLOWS (U1, and it is deliberately the THIRD user of the idiom, not a new one):
// `park_reflood_arm`/`park_reflood_fire` (node_hashlocate.cpp) and `e2e_ack_deadline_arm_timer`/
// `e2e_ack_deadline_fire` (node_mac.cpp) each serve a whole multi-entry ring from ONE one-shot timer re-armed to
// the EARLIEST pending deadline. That is exactly what is needed here, and it is why `TimerWheel::kCap` STAYS 91:
// the highest allocated id is 90 and there are zero free ids, so a per-slot timer band was never available.
// ⛔ NOT `jtx_ring_arm`: that helper's ring is ROUND-ROBIN by cursor and its slot index IS its timer id
// (`timer_base + slot`) — both properties are precisely what this ring must not have. It must key by hash (so a
// duplicate coalesces), must prefer a genuinely free slot over any eviction, and must live on ONE timer id. The
// pieces of `jittered_tx_stash.h` that DO generalise are reused verbatim in spirit and named where they appear:
// the fit-before-draw refusal, the draw-only-on-accept rule, and `len` as the armed flag (`jtx_fire` clears it).
//
// ONE DIVERGENCE FROM BOTH PRECEDENTS, and it is required by §5.3.3: the fire transmits AT MOST ONE due entry per
// callback and re-arms for the next earliest (the other two drain everything due). Reason: this ring's payload is a
// RADIO FRAME, and firing four in one callback would put four OFFERs into the same millisecond — the exact same-ms
// collision the jitter exists to prevent.
// ============================================================================================================

// The IN-USE slot holding `target_key_hash32`, or -1. ⛔ Keyed by HASH, never by slot index — coalescing by index is
// the tempting wrong fix that would make two mobiles share an entry the moment the ring wrapped.
// "In use" is `reserve_until_ms != 0`: every armed entry also holds a reservation, but a transmitted entry keeps its
// reservation after `len` goes to 0, and that residual IS [[B137]].
int Node::mobile_offer_slot_of(uint32_t target_key_hash32) const {
    for (uint8_t i = 0; i < protocol::cap_pending_mobile_offers; ++i)
        if (_pending_mobile_offers[i].reserve_until_ms != 0 && _pending_mobile_offers[i].target_key_hash32 == target_key_hash32)
            return static_cast<int>(i);
    return -1;
}

// [[B137]]: the promise is discharged (the matching CLAIM landed, or it was refused by the race backstop) -> free the
// slot AND its reservation, then re-arm. Idempotent: a mobile with no entry is a no-op, which is the normal case for
// a CLAIM that arrives after its reservation already expired.
void Node::mobile_offer_release(uint32_t target_key_hash32) {
    const int s = mobile_offer_slot_of(target_key_hash32);
    if (s < 0) return;
    _pending_mobile_offers[s] = PendingMobileOffer{};
    mobile_offer_arm_timer();
}

// §5.3.2 — ADMISSION. Returns which of the four things happened; the caller (the DISCOVER handler) decides what to
// say about it. ★ The order of the tests below is the contract:
//   1. FIT FIRST, before anything is consumed — `jittered_tx_stash.h` invariant 1, inherited not re-invented: a frame
//      that does not fit is refused WHOLE, with no copy, no slot, no reservation and NO RNG DRAW.
//   2. DUPLICATE next, so a coalesce can never consume a slot or move a deadline (§5.3.3).
//   3. Then a genuinely FREE slot — §5.3.2 item 3. ⛔ There is no eviction branch at all: `full` is a refusal, and
//      "the ring overwrites on full" is the §S0-1 defect wearing a ring's clothes.
//   4. Only then the draw + the arm — invariant 2's "the cursor advances only for an ACCEPTED frame", restated for a
//      ring that has no cursor: NOTHING is consumed until the entry is certain.
Node::MobileOfferAdmit Node::mobile_offer_admit(uint32_t target_key_hash32, const uint8_t* frame, size_t n,
                                                uint8_t proposed_id) {
    if (n == 0 || n > sizeof(_pending_mobile_offers[0].buf) || proposed_id == 0) return MobileOfferAdmit::invalid;
    const uint64_t now = _hal.now();

    // (2) DUPLICATE — but ONLY while the entry is still ARMED. §5.3.3's "a duplicate DISCOVER coalesces and does not
    // move the deadline" is about a second DISCOVER arriving *inside the jitter window*, when an answer is already on
    // its way. Once the OFFER has been TRANSMITTED (`len == 0`, reservation still live) a fresh DISCOVER means the
    // mobile did not hear it, and the correct answer is a NEW OFFER — re-armed into the SAME slot, keeping the SAME
    // reserved id (that is `find_free_mobile_id`'s reservation-idempotence, so the caller already handed us the same
    // `proposed_id`). Treating that as a duplicate would answer a re-DISCOVER with silence forever.
    const int existing = mobile_offer_slot_of(target_key_hash32);
    if (existing >= 0 && _pending_mobile_offers[existing].len != 0) return MobileOfferAdmit::duplicate;

    // (3) a genuinely free slot — the mobile's own re-arm slot if it has one, else the first slot whose reservation
    // is absent or ELAPSED. Reading `reserve_until_ms <= now` as free (rather than requiring the scan to have swept
    // it) is what keeps the ring self-healing if an `_hal.after` was ever dropped.
    int slot = existing;
    if (slot < 0)
        for (uint8_t i = 0; i < protocol::cap_pending_mobile_offers; ++i)
            if (_pending_mobile_offers[i].reserve_until_ms <= now) { slot = static_cast<int>(i); break; }
    if (slot < 0) { ++_mobile_offer_ring_full_n; return MobileOfferAdmit::full; }   // ⛔ nothing armed is touched

    // (4) accepted: ONE draw, in the same position in the stream that `jtx_stash_arm` occupied, so a single-mobile
    // DISCOVER consumes exactly the draws it always did (§11 S2 — S3 is the arc's only planned RNG re-anchor).
    PendingMobileOffer& e = _pending_mobile_offers[slot];
    e = PendingMobileOffer{};
    for (size_t i = 0; i < n; ++i) e.buf[i] = frame[i];
    e.len               = static_cast<uint8_t>(n);
    e.target_key_hash32 = target_key_hash32;
    e.proposed_id       = proposed_id;
    const uint32_t jit  = static_cast<uint32_t>(_hal.rand_range(protocol::join_offer_backoff_min_ms,
                                                                protocol::join_offer_backoff_max_ms + 1));
    e.due_ms            = now + jit;
    e.reserve_until_ms  = now + protocol::mobile_offer_reservation_ms;   // [[B137]]: the id is PROMISED from here
    mobile_offer_arm_timer();
    return MobileOfferAdmit::armed;
}

// §5.3.3 — (re)arm the ONE timer to the EARLIEST pending deadline of EITHER kind: an armed OFFER's `due_ms` or a
// reservation's `reserve_until_ms`. Nothing pending -> cancel (idempotent per the Hal contract).
// ⚠ It must always take the MINIMUM, never "the one that just changed": `Hal::after` holds one deadline per timer
// id, so arming a LATER deadline would silently displace an EARLIER one and strand that mobile's OFFER.
//
// ★★★ [[B145]] §MH-S2b — AND AN ALREADY-ELAPSED MINIMUM IS RE-ARMED AT A POSITIVE FLOOR, NEVER AT ZERO. This line
// used to read `earliest > now ? earliest - now : 0`, and that `0` was the whole defect: `TimerWheel::pop_due` fires
// on `_due <= now`, and the production pump (`src/fw_main.cpp` `for (int id; (id = g_hal.pop_due_timer()) >= 0; )`)
// keeps draining at a clock that need not have moved ⇒ the callback re-entered inside the SAME pump and
// `mobile_offer_fire`'s "at most ONE due OFFER per callback" bought nothing: four overdue entries still reached the
// radio in one millisecond. ⛔ THE OLD TEST COULD NOT SEE IT — it invoked `on_timer(80)` by hand, one call at a time,
// modelling a pump that does not exist; the regression beside it now drives a REAL `TimerWheel` at a FIXED timestamp.
// ⓘ Only a delay that would be ZERO is substituted. A positive computed delay passes through untouched, so every
//   non-overdue path — which is every path in a mesh where the timer is not starved — is byte-identical.
// ⛔ `protocol::mobile_offer_respace_ms` is a CONSTANT, not a draw (S3 owns jitter); its sizing note is at the
//   constant, and it is deliberately NOT `join_offer_backoff_min_ms` despite sharing its value today.
void Node::mobile_offer_arm_timer() {
    uint64_t earliest = ~0ull;
    for (uint8_t i = 0; i < protocol::cap_pending_mobile_offers; ++i) {
        const PendingMobileOffer& e = _pending_mobile_offers[i];
        if (e.len != 0            && e.due_ms           < earliest) earliest = e.due_ms;
        if (e.reserve_until_ms != 0 && e.reserve_until_ms < earliest) earliest = e.reserve_until_ms;
    }
    if (earliest == ~0ull) { _hal.cancel(kMobileOfferBackoffTimerId); return; }
    const uint64_t now   = _hal.now();
    const uint64_t delay = earliest > now ? earliest - now : 0;
    (void)_hal.after(delay != 0 ? static_cast<uint32_t>(delay) : protocol::mobile_offer_respace_ms,
                     kMobileOfferBackoffTimerId);
}

// §5.3.3 — the deadline scan. Three jobs, in this order, then a re-arm.
//
// ★★ §B132b — THE TRANSMISSION BOUNDARY IS A DECISION SITE, and it is re-checked HERE as well as at admission. The
// OFFER is committed at DISCOVER time but transmitted 100..1000 ms later, and eligibility can flip inside that window
// through two LIVE console knobs needing no reboot (`cfg set mobile 1` while the registry is empty, and
// `cfg set host_mobiles off`). Without this re-check the node advertises itself as a home it may no longer be. It is
// also the only defence on the REFUSED-`on_init` path, which returns before that cleanup with `n_layers == 2` intact.
void Node::mobile_offer_fire() {
    if (!can_host_mobiles()) { mobile_host_pending_clear(); return; }   // ineligible -> DROP the ring, transmit NOTHING
    const uint64_t now = _hal.now();

    // (1) [[B137]]: expire elapsed reservations, so a mobile that is offered an id and never CLAIMs cannot leak it.
    // ⓘ An entry can only be here with `len != 0` if its OFFER never got a fire opportunity inside the whole
    // reservation window (jitter max 1000 ms vs 10 000 ms) — a starved timer. Dropping it with the reservation is
    // right: the frame is 10 s stale and the mobile's own retry is the backstop.
    for (uint8_t i = 0; i < protocol::cap_pending_mobile_offers; ++i) {
        PendingMobileOffer& e = _pending_mobile_offers[i];
        if (e.reserve_until_ms == 0 || e.reserve_until_ms > now) continue;
        MR_EMIT("mobile_offer_reservation_expired", EF_I("to_key", static_cast<int64_t>(e.target_key_hash32)),
                EF_I("local_id", e.proposed_id), EF_I("unsent", e.len != 0 ? 1 : 0));
        e = PendingMobileOffer{};
    }

    // (2) AT MOST ONE due OFFER per callback (§5.3.3), the earliest — one frame per callback keeps the host off a
    // same-millisecond burst, which is the entire reason the jitter exists.
    int due = -1;
    for (uint8_t i = 0; i < protocol::cap_pending_mobile_offers; ++i) {
        const PendingMobileOffer& e = _pending_mobile_offers[i];
        if (e.len == 0 || e.due_ms > now) continue;
        if (due < 0 || e.due_ms < _pending_mobile_offers[due].due_ms) due = static_cast<int>(i);
    }
    if (due >= 0) {
        PendingMobileOffer& e = _pending_mobile_offers[due];
        // ★ §MH-S1 §6.2 — THE ADMISSION RESULT DECIDES THE ENTRY'S FATE. `jtx_fire` clears `len` either way (so a
        // re-entrant fire is a no-op rather than a duplicate transmission) and returns whether the transmitter
        // accepted it. ⛔ A rejection must NOT disturb any other mobile's entry — vacuous under the old single slot,
        // a real obligation now, and satisfied structurally: only `e` is touched.
        // ★ THE RESERVATION IS RETAINED ON A REJECTION (§5.3.2 item 9's "retains or reschedules"). The frame is
        // dropped, but the mobile's id stays promised, so its own re-DISCOVER is answered with the SAME id rather
        // than a fresh one. That is strictly better than the drop-everything S1 had to do.
        // ★★ §MH-S3 RULED, so this is no longer a deferral: RETAIN IS THE FINAL SHAPE. §6.2 and item 9 both offer
        // reschedule **OR** report-an-explicit-drop as ALTERNATIVES, not as a sequence — and S2 took the second
        // one COMPLETELY (explicit `mobile_offer_dropped`, [[B146]]'s counter, and the reservation kept). S3 owns
        // the draw the reschedule would have needed and has DECLINED to spend it here: a reschedule would put a
        // second OFFER for the same mobile on a channel our own transmitter has just said it cannot use, while
        // §6.2's stated backstop — the source mobile's own retry, which S3 has just jittered — already covers it.
        TxAdmission adm = TxAdmission::admitted;
        if (!jtx_fire(e.buf, e.len, LbtKind::mobile_offer, &adm)) {
            // ★ [[B146]] §MH-S2b — THE COUNTER LIVES IN `mobile_offer_admission_rejected`, NOT HERE. It used to be
            // incremented at this ONE call site, so the DEFERRED rejection (node.cpp's `tx_deferred_lost` arm, the
            // other caller) reported the drop and left `mobile_offer_reject_count()` reading zero. One reporter, one
            // counter, one place — see the note at the function.
            mobile_offer_admission_rejected(adm);   // adm ∈ {defer_full, tx_rejected} — tx_initiating writes it on every path
        }
    }

    // (3) re-arm for whatever is left — the next due OFFER or the next reservation bound (park_reflood_fire's tail).
    mobile_offer_arm_timer();
}

// ★ §MH-S1 §6.2 — the staged OFFER reached its jitter deadline and OUR OWN TRANSMITTER definitively refused
// it. Before this slice `jtx_fire`'s answer was discarded, so the frame simply vanished while
// `mobile_offer_tx` — emitted 100..1000 ms earlier at DISCOVER time, i.e. meaning only *staged* — was the
// last word on the subject (§10: "an event named `mobile_offer_tx` must not continue to mean only 'copied
// into a stash'"). ★ §MH-S1b closed the naming half too: that staging emit is now `mobile_offer_scheduled`
// and `mobile_offer_tx` is raised in `lbt_complete` at the accepted handoff, so this drop report and it are
// mutually exclusive by construction — exactly one of the two fires per staged OFFER.
//
// ⛔ WHY THIS REPORTS A DROP AND DOES NOT RESCHEDULE. §6.2 permits either. Rescheduling needs a bounded NEW
// deadline, i.e. a fresh jitter draw, and **S3 is the only planned RNG re-anchor in this arc** — a draw here
// would re-anchor all 36 corpus streams under the wrong slice and destroy the attribution the whole slice
// ordering exists to protect. The reschedule is therefore owed to S2 (which introduces the keyed ring the new
// deadline would be scanned from) / S3 (which owns the draw). ⇒ **the source mobile's own retry is the
// backstop**, exactly as §6.2 says it must be when the drop branch is taken.
// ★★★ §MH-S3 CLOSED THIS, BY RULING RATHER THAN BY BUILDING IT — read the two paragraphs above as HISTORY.
// S3 re-read §6.2 and §5.3.2 item 9 against the source (V1) and found the obligation is a DISJUNCTION —
// "**reschedule** it … **or** **report an explicit drop** and bump the transmitter-rejection counter, so the
// source mobile's own retry remains the backstop" — with item 9 phrased identically ("retains **or**
// reschedules"). The drop branch is taken IN FULL here: explicit event, [[B146]]'s single-reporter counter,
// no other mobile's entry touched, and (§MH-S2) the [[B137]] reservation RETAINED so the retry is answered
// with the same id. ⇒ **NOTHING IS OWED.** S3 spent its draws on the three §5.2/§5.4 sites and deliberately
// not here: a reschedule would re-offer onto a channel this node's own transmitter has just refused, and
// §6.2's named backstop — the source mobile's retry — is precisely what S3 jittered. ⛔ Do not resurrect
// this as a "leftover"; it is a closed decision, not a gap.
//
// ★ §MH-S2 CLOSED THE TWO THINGS THIS NOTE OWED. (a) "A rejection must never disturb any other mobile's armed
// entry" was satisfied VACUOUSLY under the single slot — the entry reported was necessarily the one that just
// fired. It is now a REAL obligation and is met structurally: `mobile_offer_fire` picks exactly one due entry and
// touches only that one. (b) The ring-overflow counter §10 asked for exists (`_mobile_offer_ring_full_n`), and its
// twin `_mobile_offer_reject_n` is incremented HERE (see [[B146]] below). ⓘ STILL OWED: §6.2's *reschedule*
// alternative, which needs a bounded new jitter draw ⇒ S3. What S2 could do without a draw, it did — the entry's
// [[B137]] id RESERVATION is retained across the drop, so the mobile's own retry is answered with the same id.
// ⛔ "STILL OWED" IS WITHDRAWN by §MH-S3 — see the ruling paragraph above; the alternative was chosen, not skipped.
//
// ★★★ [[B146]] §MH-S2b — THE COUNTER IS INCREMENTED HERE, WHERE THE DROP IS REPORTED, AND NOWHERE ELSE. §MH-S2 put
// `++_mobile_offer_reject_n` at the `mobile_offer_fire` call site, which is only ONE of this function's TWO callers:
// an OFFER accepted into the LBT defer ring and then refused by the HAL arrives from `node.cpp`'s `tx_deferred_lost`
// arm instead, so it emitted `mobile_offer_dropped` while `mobile_offer_reject_count()` stayed at zero — a counter
// that under-reports exactly the failure it exists to count. ★ AND THE TEST COULD NOT SEE IT: the deferred case
// asserted the EVENT, never the counter. ⇒ ONE reporter owns the count. Every caller must therefore reach here
// exactly once per definitively-refused OFFER (no caller may increment as well), which is what makes
// `mobile_offer_reject_count()` == `mobile_offer_dropped` count, by construction.
void Node::mobile_offer_admission_rejected(TxAdmission why) {
    ++_mobile_offer_reject_n;
    MR_EMIT("mobile_offer_dropped", EF_S("result", why == TxAdmission::defer_full ? "defer_full" : "tx_rejected"));
    // §3-A.1 twin: MR_EMIT is device-stripped. The host has just failed to answer a mobile that is waiting on
    // a 2 s window, so this one IS operator-visible — but trace-gated, not `!!`: the mobile re-DISCOVERs.
    _hal.log("mobile OFFER dropped at our own transmitter — not sent; the mobile's own retry is the backstop");
}

// ★★ §B132b — the ONE cleanup for "this node must not act as a mobile HOME": drop every leaf's PENDING host
// transmission and cancel the timer that would fire it. Two callers, deliberately: `on_init`'s gateway force-off
// (C3 hygiene) and the OFFER timer's own eligibility re-check (the guarantee) — one spelling so they cannot drift.
//
// ⚠ PER-LEAF, NOT `_active`, FOR THE ROSTER HALF: the roster coalesce/echo state lives in LayerState and a GATEWAY
// OWNS TWO of them, so a window opened while the other leaf was active would otherwise survive a swap.
// ★ §MH-S2: the OFFER half is no longer per-leaf — the ring is node-global (node.h), so ONE clear covers it. That is
// a simplification the node-global scope buys, not a behaviour change: the old loop existed precisely because a frame
// staged on the inactive leaf was invisible to `_active`, and a node-global ring has no inactive copy to hide in.
// ⚠ The clear wipes RESERVATIONS as well as armed frames, deliberately: a node that must not act as a home must not
// be holding local ids promised to mobiles either.
//
// ⓘ WHAT IS *NOT* HERE, and why: `_mobile_reg` / `_notify_pending` / `_mobile_snr_q4` are cleared by `on_init` itself
// (that is registry state, not a pending transmission), and the roster has no equivalent of the OFFER's boundary hole —
// `presence_emit_roster()` already re-checks `can_host_mobiles()` AT THE EMIT, which IS its transmission boundary, so
// clearing `_roster_coalesce_pending` here is hygiene (a stale window flag) and never the thing that suppresses a
// roster. The OFFER had no such check, which is exactly the difference this function exists for.
void Node::mobile_host_pending_clear() {
    for (uint8_t i = 0; i < protocol::cap_pending_mobile_offers; ++i) _pending_mobile_offers[i] = PendingMobileOffer{};
    for (uint8_t li = 0; li < MR_N_LAYERS; ++li) {
        _layers[li]._roster_coalesce_pending = false;
        _layers[li]._roster_echo_pending     = false;
    }
    // Idempotent per the Hal contract (hal.h). ⚠ NOT load-bearing for a test: TestHal::cancel() is a NO-OP, so a
    // native case must FIRE the timer and assert the absent frame on the wire rather than trust the cancel.
    _hal.cancel(kMobileOfferBackoffTimerId);
    _hal.cancel(kPresenceRosterTimerId);
}

// Arm the coalesce timer so a burst of probes -> ONE roster; obey the rate-limit floor (spoof/burst).
void Node::presence_schedule_roster() {
    if (_active->_roster_coalesce_pending) return;                            // one window already open
    const uint64_t now = _hal.now();
    uint32_t delay = static_cast<uint32_t>(_hal.rand_range(protocol::presence_roster_coalesce_min_ms,
                                                           protocol::presence_roster_coalesce_max_ms + 1));
    const uint64_t earliest = _active->_last_roster_ms + protocol::presence_roster_min_interval_ms;
    if (now + delay < earliest) delay = static_cast<uint32_t>(earliest - now);   // rate-limit floor
    if (_hal.after(delay, kPresenceRosterTimerId)) _active->_roster_coalesce_pending = true;
}

void Node::presence_roster_fire() {
    _active->_roster_coalesce_pending = false;
    presence_emit_roster();
}

// Build + LBT-broadcast the roster from the host registry + the per-mobile quality tier + has_key + dir_epoch.
// §B2: a delegated send this home tried to route for a hosted mobile failed LOUD (no gateway / bad path). Set the
// per-entry deleg_fail bit + schedule a coalesced roster; the mobile seeing ITS bit fires send_failed{no_route} once.
// ★★★ §MH-S5-FIX2 finding C (owner-ruled 2026-08-10, ledger §1.14) — **THE BIT IS REFUSED UNLESS THE ROW IS LIVE AND
// DIRECT.** `deleg_fail` only ever reaches its mobile inside a ROSTER, and [[B172]]'s filter means a redirect/expired
// row is never in one — so setting it there produced a flag nothing could carry and a failure NOTHING WOULD REPORT
// (the *"instrument that cannot fail"* shape, in its worst direction: the absence of the mobile's
// `send_failed{no_route}` would have read as success). ⛔ §MH-S5-FIX called that *"self-clearing"*; WITHDRAWN — only a
// re-CLAIM's aggregate-initialise or expiry cleared it, i.e. possibly never.
// ⇒ Refuse, and refuse the ROSTER SCHEDULE with it: airtime for a signal that cannot be carried is airtime wasted.
// ★ The other half of the same defect lives at the breadcrumb arm (`node_mac_rx.cpp`), which CLEARS an already-set bit
//   when a row converts direct -> redirect. This refusal alone cannot close that order of events (accepted while
//   direct, breadcrumb before the failure was recorded), and that clear alone cannot stop a fresh set on an EXPIRED
//   row — both are needed.
// ⓘ `host_row_live_direct` is the ONE predicate (`node.h`); no age arithmetic is re-spelled here (§9.3).
void Node::presence_mark_deleg_fail(uint32_t mobile_hash) {
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == mobile_hash) {
            if (!host_row_live_direct(i)) return;              // §MH-S5-FIX2: not ours to signal for -> no bit, no roster
            _active->_mobile_reg[i].deleg_fail = true;
            presence_schedule_roster();
            return;
        }
}

void Node::presence_emit_roster() {
    if (!can_host_mobiles()) return;                                 // ★ §B132: a roster ADVERTISES "I am the home of these mobiles" — an ineligible node must never emit one. Gated HERE because this is the single choke point for all SIX schedule paths (evict_aliased_hosted_mobile, the CLAIM record, the two presence_ingest_probe answers, presence_mark_deleg_fail, presence_roster_fire), so no future caller can route around the invariant. Inert for an eligible host by construction.
    if (_node_id == 0) return;                                       // §S6/QA-1: never broadcast a roster with home_id=0 garbage while mid-join/unprovisioned (SAME suspend as the OFFER gate)
    // ★ §MH-S5 §9.3 — "…before emitting a roster". A roster is the home's public claim *"I host these mobiles"*, so
    // an expired row must never appear in one; the sweep runs before the count is read, not after (§S0-5 asserted
    // the opposite: a mobile 50 minutes silent was still advertised). Ordered AFTER the `_node_id`/eligibility gates
    // so an ineligible node still mutates nothing.
    mobile_reg_age_out();
    if (_active->_mobile_reg_n == 0 && !_active->_roster_echo_pending) return;   // §S6 rev2: an EMPTY home still answers a searching-probe canvass (echo only)
    PRosterEntry ents[protocol::cap_host_mobiles];
    // ★★★ §MH-S5-FIX [[B172]] — **THE SOURCE ROW OF EACH ENTRY, CARRIED.** Once the copy loop FILTERS, `n < i` and the
    // roster index stops being the registry index. The one-shot `deleg_fail` clear at the bottom of this function used
    // to key on the ENTRY index with a comment asserting the 1:1 mapping, so under a filter it would clear the WRONG
    // rows' bits — silently losing the *delegated-send-dropped* signal a mobile needs to fire `send_failed{no_route}`
    // exactly once. ⛔ Do not re-key that loop on `i`. ⓘ A FUNCTION-LOCAL array: zero `Node` bytes, so D2 does not fire
    // (a member would have cost `cap_host_mobiles × MR_N_LAYERS`). Bounded by the same cap as `ents`.
    uint8_t src_slot[protocol::cap_host_mobiles];
    uint8_t n = 0;
    for (uint8_t i = 0; i < _active->_mobile_reg_n && n < protocol::cap_host_mobiles; ++i) {
        // ★★ §MH-S5-FIX [[B172]]/[[B173]] — a roster is this home's PUBLIC CLAIM *"I host these mobiles"*, and spec
        // §9.2 is explicit: *"redirect rows never … advertise as directly hosted"* (§9.1 says the same for an expired
        // row). ⚠ The `PRosterEntry` wire entry has NO redirect marker, so a listener CANNOT tell — which is why the
        // filter has to be here, at the producer. Before this, an old home advertised a moved mobile as its own for up
        // to the full 25-minute redirect lifetime.
        if (!host_row_live_direct(i)) continue;
        src_slot[n]        = i;                                     // entry n came from registry row i
        ents[n].key_hash32 = _active->_mobile_reg[i].key_hash32;
        ents[n].local_id   = _active->_mobile_reg[i].mobile_local_id;
        ents[n].reg_epoch  = static_cast<uint8_t>(_active->_mobile_reg[i].epoch);
        ents[n].quality    = protocol::presence_quality_tier(_active->_mobile_snr_q4[i]);
        ents[n].has_key    = _active->_mobile_reg[i].has_pubkey;
        ents[n].deleg_fail = _active->_mobile_reg[i].deleg_fail;   // §B2: a delegated send this home dropped loud (one-shot; cleared below after this roster carries it)
        ++n;
    }
    p_roster_in in{}; in.home_id = _node_id; in.home_layer = active_layer_id();
    in.dir_epoch = presence_compute_dir_epoch(); in.wire_version = protocol::wire_version; in.entries = ents; in.count = n;   // §D16
    if (_active->_roster_echo_pending) { in.has_echo = true; in.echo_hash32 = _active->_roster_echo_hash; in.echo_quality = _active->_roster_echo_q; }
    uint8_t buf[protocol::lora_max_frame_bytes];
    const size_t sz = pack_p_roster(in, std::span<uint8_t>(buf, sizeof buf));
    if (sz) {
        _active->_last_roster_ms = _hal.now();
        MR_EMIT("presence_roster_tx", EF_I("count", n), EF_I("home", _node_id), EF_I("echo", _active->_roster_echo_pending ? 1 : 0));
        tx_initiating(buf, sz, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
        // §B2: one-shot — this roster carried the bit, so clear it. ★ KEYED ON `src_slot`, NOT on the entry index:
        // since [[B172]] the copy loop above filters, so entry k maps to registry row `src_slot[k]`, never to row k.
        // A probe between set and here just re-fires the roster.
        for (uint8_t k = 0; k < n; ++k) _active->_mobile_reg[src_slot[k]].deleg_fail = false;
    }
    _active->_roster_echo_pending = false;                          // one echo per window (consumed)
}

// §S6.4-D: the NEW home originates the redirect breadcrumb to the mobile's stashed last_home (D10 — home-sent
// REPLACES the old mobile-sent breadcrumb; survives a mobile that sleeps right after adopting). Payload =
// [new_home_id][new_epoch][new_home_layer], SOURCE_HASH = the mobile (so the old home attributes it to _mobile_reg[M]).
void Node::presence_notify_old_home(uint32_t mobile_hash, uint8_t /*new_local_id*/, uint16_t new_epoch) {
    for (uint8_t i = 0; i < _active->_notify_pending_n; ++i) {
        if (_active->_notify_pending[i].mobile_hash != mobile_hash) continue;
        const uint8_t old_home  = _active->_notify_pending[i].last_home_id;
        const uint8_t old_layer = _active->_notify_pending[i].last_home_layer;
        const uint32_t old_hash = _active->_notify_pending[i].last_home_hash;
        // remove the entry (swap-with-last)
        _active->_notify_pending[i] = _active->_notify_pending[--_active->_notify_pending_n];
        if (old_home == 0 || old_home == _node_id) return;           // fresh / self -> nothing to notify
        uint8_t body[3] = { _node_id, static_cast<uint8_t>(new_epoch), active_layer_id() };   // new_home_id/new_epoch/new_home_layer
        // §B4: CROSS-LAYER old home (old_layer != ours) — S1 UNBLOCKED this. Address the breadcrumb BY HASH (the mobile
        // carried its old home's hash in the +4-B j_discover block) via a bridging gateway; SOURCE_HASH = the mobile so the
        // old home's redirect machinery attributes it. Best-effort: no gateway / no hash -> drop (a dead old home can't
        // black-hole; the mobile_liveness prune backstops an alive-but-unreachable one, spec §S6.4-D). Never park an ack.
        if (old_layer != 0 && old_layer != active_layer_id()) {
            const uint8_t target_leaf = static_cast<uint8_t>(old_layer & 0x0F);
            const uint8_t gw = old_hash ? select_gateway_for_leaf(target_leaf) : 0;
            if (gw == 0) { MR_EMIT("presence_notify_xl_no_route", EF_I("old_home", old_home), EF_I("old_layer", old_layer)); return; }
            const uint8_t ids[2] = { active_layer_id(), old_layer };   // path [our_layer, old_layer], cur=1
            (void)enqueue_cross_layer(gw, old_hash, ids, /*n_layers=*/2, /*cur=*/1, body, 3, /*flags=*/0,
                                      /*out_ctr=*/nullptr, DATA_TYPE_MOBILE_BREADCRUMB, /*override_source_hash=*/mobile_hash);
            MR_EMIT("presence_notify_xl_tx", EF_I("old_home", old_home), EF_I("old_layer", old_layer), EF_I("new_home", _node_id));
            return;
        }
        // Same-layer old home (the common re-home): routed by id.
        (void)enqueue_data(old_home, body, 3, DATA_FLAG_SOURCE_HASH, "presence_notify",
                           /*app_dm=*/false, DATA_TYPE_MOBILE_BREADCRUMB, CryptIntent::off,
                           /*override_dst_hash=*/0, /*override_source_hash=*/mobile_hash);
        MR_EMIT("presence_notify_tx", EF_I("old_home", old_home), EF_I("new_home", _node_id));
        return;
    }
}

}  // namespace meshroute
