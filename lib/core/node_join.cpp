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
    schedule_triggered_beacon();                                       // announce the new id (peers re-bind on it)
    if (_pending_rediscover) {                                         // a verb reprovision -> the id is now stable: rebuild routes
        _pending_rediscover = false;
        restart_discovery();
    }
}

// ---- §5 receive: J dispatch + CLAIM/DENY handlers ----------------------------------------------------
void Node::handle_j(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    (void)meta;
    auto pj = parse_j(std::span<const uint8_t>(bytes, len));
    if (!pj) return;
    const j_out& j = *pj;
    if (j.leaf_id != _cfg.leaf_id) return;                             // foreign layer
    if (j.wire_version != protocol::wire_version) {                    // R6.2 §5.2: never join across a wire-version gap
        MR_EMIT("j_wire_incompatible", EF_I("src_op", j.opcode), EF_I("their_ver", j.wire_version), EF_I("my_ver", protocol::wire_version));
        return;
    }

    if (j.opcode == static_cast<uint8_t>(j_opcode::claim)) {
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
    // DISCOVER / OFFER: deferred (beacon-listen + Q-pull + DAD model) — ignored this slice.
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

}  // namespace meshroute
