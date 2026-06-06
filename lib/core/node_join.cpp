// MeshRoute — lib/core/node_join.cpp  (node_id auto-assignment: DAD + self-heal)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Allocates the 8-bit short `node_id` with no authority, over lossy links / partitions, into the
// 254-id space. Identity is the key_hash32 (stable); node_id is a disposable lease — renumbering is
// harmless (upper layers re-bind by key_hash32). Design + rationale:
//   docs/specs/2026-06-05-node-id-auto-assignment-design.md
//
// Mirrors the Lua join cluster (dv_dual_sf.lua join_choose_candidate_id / join_start_claim / handle_j
// CLAIM+DENY / forced_rejoin) with ONE deliberate, signed-off divergence: the TIEBREAK is static
// claim_epoch -> key_hash32 (NOT the Lua's lease_age-first, which is provably non-convergent under wire
// staleness — spec §6). lease_age stays on the wire as telemetry. DISCOVER/OFFER are deferred (the
// design's join is beacon-listen + Q config-pull + DAD); this slice is the CLAIM/heal core.
#include "node.h"
#include "frame_codec.h"

#include <span>

namespace meshroute {

// §6 — the one tiebreak. Higher claim_epoch wins; on a tie the lower key_hash32 wins. Both inputs are
// STATIC + wire-carried, so both sides compute the same total order from the same values -> exactly one
// yields -> convergent, no ping-pong. (key_hash32 is unique per honest node, so the order is total.)
bool Node::join_tiebreak_wins(uint8_t my_epoch, uint32_t my_key, uint8_t their_epoch, uint32_t their_key) {
    if (my_epoch != their_epoch) return my_epoch > their_epoch;
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

// ---- §3 candidate selection: prefer our previous id, else a random free slot (-1 = leaf full) ----------
int Node::join_choose_candidate_id() {
    const int prev = id_bind_find_by_hash(_key_hash32);                 // the network/NV may remember our old id
    if (prev >= 1 && prev <= 254 && !join_id_denied(static_cast<uint8_t>(prev))) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "node",       .type = EventField::T::i64, .i = prev },
                               { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(_key_hash32) } };
            _hal.emit("join_prefer_previous_id", f, 2); );
        return prev;
    }
    uint8_t free_list[254];                                             // 254 B stack — fine
    uint16_t nfree = 0;
    for (int id = 1; id <= 254; ++id) {
        if (join_id_denied(static_cast<uint8_t>(id))) continue;
        bool taken = false;
        for (uint16_t i = 0; i < _id_bind_n; ++i) if (_id_bind[i].node_id == id) { taken = true; break; }
        if (!taken) free_list[nfree++] = static_cast<uint8_t>(id);
    }
    if (nfree == 0) return -1;
    return free_list[_hal.rand_range(0, static_cast<int>(nfree))];      // uniform pick (two joiners rarely collide)
}

// ---- §4 claim -> probe -> adopt ----------------------------------------------------------------------
bool Node::join_start_claim([[maybe_unused]] const char* reason) {   // reason: telemetry-only (stripped on device)
    if (_joined || _join_claim.active) return false;
    const int cand = join_choose_candidate_id();
    if (cand < 0) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "reason", .type = EventField::T::str, .s = reason ? reason : "no_free_id" } };
            _hal.emit("join_no_candidate", f, 1); );
        return false;
    }
    _claim_epoch = static_cast<uint8_t>((_claim_epoch + 1) & 0xFF);     // bump per claim (the static seniority key)
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
    for (uint16_t i = 0; i < _id_bind_n; ++i)
        if (_id_bind[i].node_id == proposed && _id_bind[i].key_hash32 != _key_hash32) { conflict = true; break; }
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
}

// ---- §5 receive: J dispatch + CLAIM/DENY handlers ----------------------------------------------------
void Node::handle_j(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    (void)meta;
    auto pj = parse_j(std::span<const uint8_t>(bytes, len));
    if (!pj) return;
    const j_out& j = *pj;
    if (j.leaf_id != _cfg.leaf_id) return;                             // foreign layer

    if (j.opcode == static_cast<uint8_t>(j_opcode::claim)) {
        const uint8_t proposed = j.proposed_node_id;
        bool conflict = false; uint32_t owner_key = _key_hash32; uint8_t reason = J_DENY_CONFLICT;
        if (_joined && proposed == _node_id && j.key_hash32 != _key_hash32) {           // (a) my adopted id
            conflict = true;
        } else {                                                                        // (b) a known binding, other hash
            for (uint16_t i = 0; i < _id_bind_n; ++i)
                if (_id_bind[i].node_id == proposed && _id_bind[i].key_hash32 != j.key_hash32) {
                    conflict = true; owner_key = _id_bind[i].key_hash32; break;
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

// Lost the heal tiebreak: yield the id, deny it (so the picker won't immediately re-pick it), drop our own
// stale self-binding, go unprovisioned, and re-run DAD with a fresh candidate.
void Node::forced_rejoin(const char* reason) {
    if (!_joined) return;
    const uint8_t prior = _node_id;
    _joined = false;
    _join_claim.active = false;
    _hal.cancel(kJoinClaimGuardTimerId);
    join_deny_id(prior);
    for (uint16_t i = 0; i < _id_bind_n; ++i)                          // drop our own (prior, myhash) binding
        if (_id_bind[i].node_id == prior && _id_bind[i].key_hash32 == _key_hash32) {
            for (uint16_t k = i; k + 1 < _id_bind_n; ++k) _id_bind[k] = _id_bind[k + 1];
            _id_bind_n--; break;
        }
    set_identity(protocol::unjoined_node_id, _key_hash32);             // 0 = unprovisioned (transient; re-claim below)
    MR_TELEMETRY(
        EventField f[] = { { .key = "prior_node_id", .type = EventField::T::i64, .i = prior },
                           { .key = "reason",        .type = EventField::T::str, .s = reason ? reason : "addr_conflict_lost" } };
        _hal.emit("addr_conflict_forced_rejoin", f, 2); );
    join_start_claim(reason);
}

}  // namespace meshroute
