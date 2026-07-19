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

#include <span>

namespace MESHROUTE_NS {

// §6 — the one tiebreak (KEY-ONLY, decided 2026-06-06): lower key_hash32 WINS/keeps; higher yields.
// One rule for EVERY heal — direct (§7), mediated/shared-neighbour (L2a), delivery-driven (L2c) — so
// they can never pick different losers (a third-party mediator has no epoch; key alone keeps them
// consistent). key_hash32 is a unique total order per honest node ⇒ exactly one winner, convergent.
// claim_epoch is now VESTIGIAL: still carried on the J wire + in NV (reserved), no longer consulted here.
bool Node::join_tiebreak_wins(uint8_t /*my_epoch*/, uint32_t my_key, uint8_t /*their_epoch*/, uint32_t their_key) {
    return my_key < their_key;
}

// ---- denied-id list (§13: a slot that lost a claim/heal stays denied for dad_denied_id_ttl_ms = 1 day) --
bool Node::join_id_denied(uint8_t id) const {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _join_denied_n; ++i)
        if (_join_denied[i].id == id && (now - _join_denied[i].denied_at_ms) < protocol::dad_denied_id_ttl_ms)
            return true;
    return false;
}
void Node::join_deny_id(uint8_t id) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _join_denied_n; ++i)
        if (_join_denied[i].id == id) { _join_denied[i].denied_at_ms = now; return; }   // refresh
    if (_join_denied_n < protocol::cap_join_denied) { _join_denied[_join_denied_n++] = { id, now }; return; }
    uint8_t o = 0;                                                                       // full -> evict the oldest
    for (uint8_t i = 1; i < _join_denied_n; ++i) if (_join_denied[i].denied_at_ms < _join_denied[o].denied_at_ms) o = i;
    _join_denied[o] = { id, now };
}
void Node::age_out_denied_ids() {
    const uint64_t now = _hal.now();
    uint8_t w = 0;
    for (uint8_t r = 0; r < _join_denied_n; ++r)
        if ((now - _join_denied[r].denied_at_ms) < protocol::dad_denied_id_ttl_ms) _join_denied[w++] = _join_denied[r];
    _join_denied_n = w;
}

// ---- L2a mediation suppression: one DENY per (id, loser-hash) per window (#1 — kill the per-beacon storm) --
bool Node::mediated_recently(uint8_t node_id, uint32_t loser_hash) const {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _mediated_recent_n; ++i)
        if (_mediated_recent[i].node_id == node_id && _mediated_recent[i].loser_hash == loser_hash
            && (now - _mediated_recent[i].t_ms) < protocol::mediated_deny_suppress_ms)
            return true;
    return false;
}
void Node::mark_mediated(uint8_t node_id, uint32_t loser_hash) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _mediated_recent_n; ++i)
        if (_mediated_recent[i].node_id == node_id && _mediated_recent[i].loser_hash == loser_hash) {
            _mediated_recent[i].t_ms = now; return;                                       // refresh the window
        }
    if (_mediated_recent_n < protocol::cap_mediated_recent) { _mediated_recent[_mediated_recent_n++] = { node_id, loser_hash, now }; return; }
    uint8_t o = 0;                                                                        // full -> evict the oldest
    for (uint8_t i = 1; i < _mediated_recent_n; ++i) if (_mediated_recent[i].t_ms < _mediated_recent[o].t_ms) o = i;
    _mediated_recent[o] = { node_id, loser_hash, now };
}
void Node::age_out_mediated() {
    const uint64_t now = _hal.now();
    uint8_t w = 0;
    for (uint8_t r = 0; r < _mediated_recent_n; ++r)
        if ((now - _mediated_recent[r].t_ms) < protocol::mediated_deny_suppress_ms) _mediated_recent[w++] = _mediated_recent[r];
    _mediated_recent_n = w;
}

// §mobile 2a: host-assign a free LOCAL id (17..254) for a mobile — distinct across THIS host's registered mobiles + not
// our own id. Returns 0 if the pool is full. Idempotent: a known key_hash returns its existing id (a re-DISCOVER re-offers
// the same id). The id MAY overlap a neighbour's global id — the mobile mark disambiguates (§17 A3), no global DAD.
//
// §S0 CONVENTION (cold-boot alias fix): the two id-pickers share the 17..254 range but grow from OPPOSITE ends —
// static DAD (join_choose_candidate_id) picks BOTTOM-UP from 17, hosted-mobile allocation picks TOP-DOWN from 254.
// Keeping them disjoint in spirit makes a mobile/static collision improbable until the pool is nearly exhausted
// (>238 combined). The metal bug was cold-boot allocation at t~8s handing a mobile local 18 (== static S2) BEFORE
// S2's beacon populated id_bind/_rt, so the picker "knew" nothing to exclude. Fix: exclude every id we have evidence
// is a static (id_bind + the routing table, same wide view as the static picker), AND allocate top-down. Two further
// backstops make this self-healing: (b) a LATER static binding for an id we already gave a mobile EVICTS the mobile
// (evict_aliased_hosted_mobile at id_bind_set) -> it re-registers onto a fresh top id via the presence plane; and
// route_uses_mobile_as_transit's static carve stops false-rejecting a route THROUGH such an aliased static meanwhile.
uint8_t Node::find_free_mobile_id(uint32_t key_hash32) {
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == key_hash32) return _active->_mobile_reg[i].mobile_local_id;
    auto id_taken = [&](uint8_t id) -> bool {
        if (id == _node_id) return true;
        if (join_id_denied(id)) return true;                                                                    // an id under active DAD denial (a static conflict) — mirror the static picker
        for (uint8_t  i = 0; i < _active->_mobile_reg_n; ++i) if (_active->_mobile_reg[i].mobile_local_id == id) return true;
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
        for (uint8_t k = i; k + 1 < _active->_mobile_reg_n; ++k) {             // compact out the evicted slot (parallel arrays)
            _active->_mobile_reg[k]    = _active->_mobile_reg[k + 1];
            _active->_mobile_snr_q4[k] = _active->_mobile_snr_q4[k + 1];
        }
        --_active->_mobile_reg_n;
        presence_schedule_roster();   // the next roster omits it -> the mobile re-registers (absent-from-roster)
        return;
    }
}

// ---- §3 candidate selection: prefer our previous id, else a random free slot (-1 = leaf full) ----------
int Node::join_choose_candidate_id() {
    const int prev = id_bind_find_by_hash(_key_hash32);                 // the network/NV may remember our old id
    // R6.3/G1: a legacy/NV prev id in the gateway range 1..16 is NOT re-preferred -> re-pick a normal id (17..254).
    if (prev >= protocol::normal_node_id_min && prev <= 254 && !join_id_denied(static_cast<uint8_t>(prev))) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "node",       .type = EventField::T::i64, .i = prev },
                               { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(_key_hash32) } };
            _hal.emit("join_prefer_previous_id", f, 2); );
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
        MR_TELEMETRY(
            EventField f[] = { { .key = "reason", .type = EventField::T::str, .s = reason ? reason : "no_free_id" } };
            _hal.emit("join_no_candidate", f, 1); );
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
    MR_TELEMETRY(
        EventField f[] = { { .key = "proposed_node_id", .type = EventField::T::i64, .i = cand },
                           { .key = "key_hash32",       .type = EventField::T::i64, .i = static_cast<int64_t>(_key_hash32) },
                           { .key = "claim_epoch",      .type = EventField::T::i64, .i = _claim_epoch },
                           { .key = "reason",           .type = EventField::T::str, .s = reason ? reason : "auto" } };
        _hal.emit("join_claim_sent", f, 4); );
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
        MR_TELEMETRY(
            EventField f[] = { { .key = "denied_node_id", .type = EventField::T::i64, .i = proposed },
                               { .key = "reason",         .type = EventField::T::str, .s = "claim_guard_conflict" } };
            _hal.emit("join_claim_denied", f, 2); );
        (void)_hal.after(protocol::join_retry_backoff_ms, kJoinRetryTimerId);
        return;
    }
    join_adopt(proposed);
}

void Node::join_adopt(uint8_t node_id) {
    set_identity(node_id, _key_hash32);                                // _node_id + Hal id + authoritative self-bind
    _joined = true;
    _join_claim.active = false;
    MR_TELEMETRY(
        EventField f[] = { { .key = "node",        .type = EventField::T::i64, .i = node_id },
                           { .key = "key_hash32",  .type = EventField::T::i64, .i = static_cast<int64_t>(_key_hash32) },
                           { .key = "claim_epoch", .type = EventField::T::i64, .i = _claim_epoch } };
        _hal.emit("join_adopted", f, 3); );
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
    if (!mobile_exempt && j.leaf_id != _cfg.leaf_id) return;           // foreign layer
    if (j.wire_version != protocol::wire_version) {                    // R6.2 §5.2: never join across a wire-version gap
        MR_EMIT("j_wire_incompatible", EF_I("src_op", j.opcode), EF_I("their_ver", j.wire_version), EF_I("my_ver", protocol::wire_version));
        return;
    }

    if (j.opcode == static_cast<uint8_t>(j_opcode::claim)) {
        if (j.is_mobile) {                                            // §mobile 2a: a mobile CLAIM = claim-stands (record/refresh — NO reply)
            if (j.chosen_host_id != _node_id) return;                 // §mobile: only the host the mobile CHOSE records it — a flood-hearer (relay) is NOT a host (else it proxies for a mobile it doesn't serve)
            // §6.4 S6: a CLAIM whose local id collides a DIFFERENTLY-keyed hosted mobile (the concurrent-OFFER race —
            // find_free_mobile_id reserves nothing until CLAIM, so two mobiles can be offered the same free id). Do NOT
            // last-write-wins (two hosted mobiles sharing one id -> the home last-miles ambiguously) and do NOT record the
            // colliding claim. TARGETED DENY the LOSER (claimant_key_hash32 = its hash) so ONLY it yields + re-registers
            // (re-DISCOVER -> a fresh id, now excluding the taken one); the RECORDED mobile ignores the DENY (hash mismatch).
            // This makes the mobile-home path recover like static/team DAD (collision -> the loser re-picks) instead of the
            // old broadcast re-OFFER, which the recorded mobile would ALSO adopt (it isn't addressed). See node_join DENY handler.
            for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
                if (_active->_mobile_reg[i].mobile_local_id == j.proposed_node_id && _active->_mobile_reg[i].key_hash32 != j.key_hash32) {
                    addr_conflict_send_deny(j.proposed_node_id, _active->_mobile_reg[i].key_hash32, j.key_hash32, J_DENY_CONFLICT);
                    MR_EMIT("mobile_id_collision_deny", EF_I("id", j.proposed_node_id), EF_I("loser", static_cast<int64_t>(j.key_hash32)));
                    return;   // do NOT record the colliding claim; the targeted DENY re-registers the loser
                }
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
                MR_TELEMETRY(
                    EventField f[] = { { .key = "denied_node_id", .type = EventField::T::i64, .i = proposed },
                                       { .key = "reason",         .type = EventField::T::str, .s = "simultaneous_claim_lost" } };
                    _hal.emit("join_claim_denied", f, 2); );
                (void)_hal.after(protocol::join_retry_backoff_ms, kJoinRetryTimerId);
                return;
            }
        }
        if (conflict) addr_conflict_send_deny(proposed, owner_key, j.key_hash32, reason);
        else          id_bind_set(proposed, j.key_hash32, IdBindSource::bcn, IdBindConf::claimed);  // learn the claim
        return;
    }

    if (j.opcode == static_cast<uint8_t>(j_opcode::deny)) {
#if MR_FEAT_MOBILE
        // §S6: our HOST bounced our local id (concurrent-OFFER collision) — a DENY TARGETED at us (claimant == our hash) for
        // our adopted id. Re-register (mobile_reset_registration -> re-DISCOVER -> a fresh, collision-free id). Handled BEFORE
        // id_bind_set so a mobile LOCAL id never enters the static id_bind plane (§mobile separation), and before the static
        // tiebreak (which would forced_rejoin onto the STATIC DAD plane — wrong for a host-registered mobile).
        if (_cfg.is_mobile && _my_mobile_reg.active && j.denied_node_id == _node_id && j.claimant_key_hash32 == _key_hash32) {
            MR_EMIT("mobile_id_denied", EF_I("id", _node_id), EF_I("home", _my_mobile_reg.home_id));
            mobile_reset_registration("mobile_id_collision");
            (void)_hal.after(0, kMobileDiscoverTimerId);          // re-DISCOVER now -> a fresh id (find_free_mobile_id excludes the id the recorded mobile holds)
            return;
        }
#endif
        id_bind_set(j.denied_node_id, j.owner_key_hash32, IdBindSource::bcn, IdBindConf::claimed);   // learn the owner
        if (_joined && j.denied_node_id == _node_id
            && j.claimant_key_hash32 == _key_hash32 && j.owner_key_hash32 != _key_hash32) {
            const bool i_win = join_tiebreak_wins(_claim_epoch, _key_hash32, j.owner_claim_epoch, j.owner_key_hash32);
            MR_TELEMETRY(
                EventField f[] = { { .key = "node",            .type = EventField::T::i64,     .i = _node_id },
                                   { .key = "i_win",           .type = EventField::T::boolean, .b = i_win },
                                   { .key = "my_claim_epoch",  .type = EventField::T::i64,     .i = _claim_epoch },
                                   { .key = "their_claim_epoch", .type = EventField::T::i64,   .i = j.owner_claim_epoch },
                                   { .key = "their_key_hash32", .type = EventField::T::i64,    .i = static_cast<int64_t>(j.owner_key_hash32) } };
                _hal.emit("addr_conflict_tie_break", f, 5); );
            if (!i_win) forced_rejoin("addr_conflict_lost");
        }
        return;
    }
    if (j.opcode == static_cast<uint8_t>(j_opcode::discover)) {       // §mobile 2a: host side of mobile registration
        if (!j.is_mobile) return;                                     // a static node never DISCOVERs -> ignore (still deferred)
        if (_cfg.is_mobile || !_cfg.host_mobiles) return;             // a mobile never hosts; a static node can opt OUT (B3)
        if (_node_id == 0) return;                                    // §clean-join: no host OFFER while unprovisioned/mid-DAD (reset_join_for_reprovision set_identity(0)'d us; adopt restores the id right before _joined). NOT `!_joined`: an operator-pinned host (`cfg set node_id` -> b.joined=0, "won't auto-yield") has _joined==false FOREVER and must keep hosting. Bonus: kills the absurd responder_node_id=0 OFFER.
        const uint8_t local = find_free_mobile_id(j.key_hash32);
        if (local == 0) return;                                       // pool full -> stay silent (the mobile picks another host)
        j_offer_in off{}; off.leaf_id = _cfg.leaf_id; off.gateway_capable = false; off.is_mobile = true;
        off.responder_node_id = _node_id; off.responder_key_hash32 = _key_hash32;
        off.data_sf_bitmap = static_cast<uint8_t>(_cfg.allowed_sf_bitmap & 0xFF);   // low byte (Slice 2b defines how the mobile consumes it)
        off.proposed_mobile_id = local;
        off.target_key_hash32  = j.key_hash32;                        // §S6: ADDRESS the OFFER at the discovering mobile (only its hash adopts it — a broadcast OFFER heard by another mobile is now ignored, killing the "wrong mobile adopts a foreign id" leg of the concurrent-register race)
        // §S6.4-D: if this DISCOVER carries a last_home (a re-home), stash it so a subsequent CLAIM (adopt) makes THIS
        // (new) home originate the old-home notify (D10). Dedup/refresh by mobile hash; evict-oldest on overflow.
        if (j.last_home_id != 0 && j.last_home_id != _node_id) {
            int ni = -1;
            for (uint8_t i = 0; i < _active->_notify_pending_n; ++i)
                if (_active->_notify_pending[i].mobile_hash == j.key_hash32) { ni = i; break; }
            if (ni < 0) { ni = (_active->_notify_pending_n < protocol::cap_host_mobiles) ? _active->_notify_pending_n++ : 0; }
            _active->_notify_pending[ni] = { j.key_hash32, j.last_home_id, j.last_home_layer, j.last_home_key_hash32 };   // §B4: + old-home hash for a cross-layer breadcrumb
        }
        uint8_t buf[13]; const size_t n = pack_j_offer(off, std::span<uint8_t>(buf, sizeof buf));
        if (n) {
            // §S6/QA-3b: DE-STORM the OFFER — stash it + fire after a random backoff so two co-located hosts don't answer
            // this DISCOVER at the SAME ms (the same-ms PHY collision that made a mobile adopt the WEAKER home). Reuses the
            // join OFFER-backoff window. Single-slot (last DISCOVER wins). The EMIT stays here (the OFFER is committed).
            MR_EMIT("mobile_offer_tx", EF_I("to_key", static_cast<int64_t>(j.key_hash32)), EF_I("local_id", local));
            _active->_pending_offer_len = static_cast<uint8_t>(n);
            for (size_t b = 0; b < n; ++b) _active->_pending_offer[b] = buf[b];
            const uint32_t jit = static_cast<uint32_t>(_hal.rand_range(protocol::join_offer_backoff_min_ms, protocol::join_offer_backoff_max_ms + 1));
            (void)_hal.after(jit, kMobileOfferBackoffTimerId);
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
void Node::addr_conflict_send_deny(uint8_t node_id, uint32_t owner_key, uint32_t claimant_key, uint8_t reason) {
    j_deny_in in{};
    in.leaf_id = _cfg.leaf_id; in.gateway_capable = _cfg.is_gateway; in.is_mobile = _cfg.is_mobile;
    in.denied_node_id = node_id; in.owner_key_hash32 = owner_key; in.claimant_key_hash32 = claimant_key;
    in.owner_lease_age_seconds = 0;                                    // telemetry only (§6)
    in.owner_claim_epoch = _claim_epoch; in.reason = reason;
    uint8_t buf[15];
    const size_t n = pack_j_deny(in, std::span<uint8_t>(buf, sizeof buf));
    MR_TELEMETRY(
        EventField f[] = { { .key = "denied_node_id",      .type = EventField::T::i64, .i = node_id },
                           { .key = "claimant_key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(claimant_key) },
                           { .key = "reason",              .type = EventField::T::i64, .i = reason } };
        _hal.emit("join_deny_sent", f, 3); );
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
    const uint8_t prior = _node_id;                                   // capture BEFORE the reset zeroes _node_id (telemetry)
    reset_join_for_reprovision();
    MR_TELEMETRY(
        EventField f[] = { { .key = "prior_node_id", .type = EventField::T::i64, .i = prior },
                           { .key = "reason",        .type = EventField::T::str, .s = reason ? reason : "addr_conflict_lost" } };
        _hal.emit("addr_conflict_forced_rejoin", f, 2); );
    join_start_claim(reason);
}

// ---- L2c: delivery-driven heal + redirect (design §7.1) ----------------------------------------------
// A DM arrived addressed to OUR node_id, but its cleartext DST_HASH names a DIFFERENT key — an id
// collision misdelivered it. Two jobs, both obeying the ONE key-only tiebreak (§6) so every heal path
// agrees on the loser: (1) the DM must still reach its real owner (redirect by hash), (2) the duplicate
// id must heal (the higher-key holder yields). The suppression ring bounds a redirect loop while the
// collision is still unhealed (a poisoned binding could otherwise resolve straight back to our own id).
bool Node::l2c_redirected_recently(uint32_t want_hash) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _l2c_redirect_n; ++i)
        if (_l2c_redirect[i].key_hash32 == want_hash
            && (now - _l2c_redirect[i].t_ms) < protocol::l2c_redirect_suppress_ms) return true;
    return false;
}
void Node::l2c_mark_redirected(uint32_t want_hash) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _l2c_redirect_n; ++i)
        if (_l2c_redirect[i].key_hash32 == want_hash) { _l2c_redirect[i].t_ms = now; return; }
    if (_l2c_redirect_n < protocol::cap_l2c_redirect) { _l2c_redirect[_l2c_redirect_n++] = { want_hash, now }; return; }
    uint8_t o = 0;                                                     // full -> evict the oldest
    for (uint8_t i = 1; i < _l2c_redirect_n; ++i) if (_l2c_redirect[i].t_ms < _l2c_redirect[o].t_ms) o = i;
    _l2c_redirect[o] = { want_hash, now };
}
void Node::l2c_handle_misdelivery(const PostAck& pa, uint32_t want_hash) {
    MR_TELEMETRY(
        EventField f[] = { { .key = "node",       .type = EventField::T::i64, .i = _node_id },
                           { .key = "origin",     .type = EventField::T::i64, .i = pa.origin },
                           { .key = "ctr",        .type = EventField::T::i64, .i = pa.ctr },
                           { .key = "want_hash",  .type = EventField::T::i64, .i = static_cast<int64_t>(want_hash) } };
        _hal.emit("l2c_misdelivery", f, 4); );
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
            MR_TELEMETRY(
                EventField f[] = { { .key = "origin", .type = EventField::T::i64, .i = pa.origin },
                                   { .key = "ctr",    .type = EventField::T::i64, .i = pa.ctr },
                                   { .key = "to",     .type = EventField::T::i64, .i = rid } };
                _hal.emit("l2c_redirect_forward", f, 3); );              // success only (queue-full already emitted the drop)
        }
        return;                                                           // l2c_enqueue_forward always kicks the queue (success or drop)
    }
    if (l2c_redirected_recently(want_hash)) {                            // suppress only the PARK+flood path (anti-flood)
        MR_TELEMETRY(
            EventField f[] = { { .key = "want_hash", .type = EventField::T::i64, .i = static_cast<int64_t>(want_hash) } };
            _hal.emit("l2c_redirect_suppressed", f, 1); );
        become_free();
        return;
    }
    l2c_mark_redirected(want_hash);
    l2c_park_redirect(want_hash, pa);                                     // hold the DM for forward/heal-on-resolution
    emit_hash_query(want_hash, /*hard=*/true);                           // owner-authoritative resolution = the discriminator
    MR_TELEMETRY(
        EventField f[] = { { .key = "want_hash", .type = EventField::T::i64, .i = static_cast<int64_t>(want_hash) } };
        _hal.emit("l2c_redirect_query", f, 1); );
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
        MR_TELEMETRY(
            EventField f[] = { { .key = "to",     .type = EventField::T::i64, .i = to_id },
                               { .key = "origin", .type = EventField::T::i64, .i = origin },
                               { .key = "ctr",    .type = EventField::T::i64, .i = ctr } };
            _hal.emit("l2c_redirect_dropped_queue_full", f, 3); );
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
    MR_TELEMETRY(
        EventField f[] = { { .key = "node",      .type = EventField::T::i64,     .i = _node_id },
                           { .key = "want_hash", .type = EventField::T::i64,     .i = static_cast<int64_t>(want_hash) },
                           { .key = "my_key",    .type = EventField::T::i64,     .i = static_cast<int64_t>(_key_hash32) },
                           { .key = "i_win",     .type = EventField::T::boolean, .b = i_win },
                           { .key = "healed",    .type = EventField::T::boolean, .b = i_win || _joined } };
        _hal.emit("l2c_collision_confirmed", f, 5); );
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
        e ^= static_cast<uint8_t>(static_cast<uint32_t>(_cfg.layers[i].freq_mhz * 1000.0 + 0.5));
        e ^= static_cast<uint8_t>(_cfg.layers[i].routing_sf);
    }
    return e;
}

// A probe heard (LEAF-FREE): refresh the hosted mobile's liveness + SNR EWMA + key custody, then schedule ONE
// coalesced roster. Answers ONLY for a mobile we CURRENTLY host (a `lost` probe from a hosted mobile = the
// one-way-deaf recovery). A probe from a non-hosted mobile is ignored (registration is the J plane's job, D8).
void Node::presence_ingest_probe(const uint8_t* frame, size_t len, const RxMeta& meta) {
    if (_cfg.is_mobile || !_cfg.host_mobiles) return;                // §S6/QA-2: only a HOST answers probes — SAME gate as the J DISCOVER->OFFER host side (a mobile never hosts; host_mobiles=0 opts out)
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
            for (uint8_t k = m; k + 1 < _active->_mobile_reg_n; ++k) { _active->_mobile_reg[k] = _active->_mobile_reg[k+1]; _active->_mobile_snr_q4[k] = _active->_mobile_snr_q4[k+1]; }
            _active->_mobile_reg_n--;
            MR_EMIT("presence_prune_stale", EF_I("was", m), EF_I("selected", p->selected_home_id));
            return;                                                 // do NOT answer (only the selected home does)
        }
        // sel_me: normal refresh + custody + SNR EWMA, then answer (ONLY the selected home answers a check probe)
        _active->_mobile_reg[mine].last_heard_ms = _hal.now();      // liveness refresh (kills the 25-min black hole via the probe cadence)
        int16_t& ew = _active->_mobile_snr_q4[mine];
        ew = (ew == 0) ? snr_q4 : static_cast<int16_t>(ew + (((snr_q4 - ew) * protocol::snr_ewma_alpha_q4) >> 4));
        if (p->has_pubkey) {                                        // §S6 A.4: key custody rides the probe (RETIRES TYPE-12) — self-consistency check ed_pub[:4]==hash
            const uint32_t pk_hash = uint32_t(p->ed_pub[0]) | (uint32_t(p->ed_pub[1]) << 8)
                                   | (uint32_t(p->ed_pub[2]) << 16) | (uint32_t(p->ed_pub[3]) << 24);
            if (pk_hash == p->key_hash32) {
                for (uint8_t k = 0; k < 32; ++k) _active->_mobile_reg[mine].ed_pub[k] = p->ed_pub[k];
                _active->_mobile_reg[mine].has_pubkey = true;
            }
        }
        MR_EMIT("presence_probe_rx", EF_I("m", mine), EF_I("snr_q4", ew));
        presence_schedule_roster();                                 // coalesced answer (rate-limit floored)
        return;
    }
    if (searching) {                                                // §S6 rev2: EVERY home answers a searching probe (candidate canvass), incl. non-hosts — with the ECHO of how WE heard IT (D14/D15)
        if (!_active->_roster_echo_pending) {                       // first probe of the window wins the echo (D15)
            _active->_roster_echo_hash = p->key_hash32; _active->_roster_echo_q = rx_tier; _active->_roster_echo_pending = true;
        }
        MR_EMIT("presence_probe_rx", EF_I("searching", 1), EF_I("snr_q4", snr_q4));
        presence_schedule_roster();
        return;
    }
    // a check probe for a hash we don't host -> ignore
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
void Node::presence_mark_deleg_fail(uint32_t mobile_hash) {
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == mobile_hash) {
            _active->_mobile_reg[i].deleg_fail = true;
            presence_schedule_roster();
            return;
        }
}

void Node::presence_emit_roster() {
    if (_node_id == 0) return;                                       // §S6/QA-1: never broadcast a roster with home_id=0 garbage while mid-join/unprovisioned (SAME suspend as the OFFER gate)
    if (_active->_mobile_reg_n == 0 && !_active->_roster_echo_pending) return;   // §S6 rev2: an EMPTY home still answers a searching-probe canvass (echo only)
    PRosterEntry ents[protocol::cap_host_mobiles];
    uint8_t n = 0;
    for (uint8_t i = 0; i < _active->_mobile_reg_n && n < protocol::cap_host_mobiles; ++i) {
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
        for (uint8_t i = 0; i < n; ++i) _active->_mobile_reg[i].deleg_fail = false;   // §B2: one-shot — this roster carried the bit (entry i maps 1:1 to _mobile_reg[i]); a probe between set and here just re-fires the roster
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
