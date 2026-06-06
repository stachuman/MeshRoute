// MeshRoute — lib/core/node_hashlocate.cpp  (H hash-locate plane — PROTOCOL §3.7a)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Phase A0 of the H port: the id_bind binding table (key_hash32 -> node_id) — the SUBSTRATE the H
// resolver answers from. The defining property of hash-locate: ANY node that already holds the binding
// answers an H query and stops the flood, not just the hash's owner. A node holds a binding because it
// (1) heard the owner's beacon (every BCN carries the sender's key_hash32) or (2) saw a hash-bind
// response pass through (cache-on-pass, Phase C.2). Mirrors dv_dual_sf.lua id_bind (:4677-4775).
// Bounded array (sized at the protocol max; _cfg.cap_id_bind gates additions). SINGLE-LAYER MVP — the
// join id-defense (J_DENY on conflict) and the cross-layer gateway_remote_bind are deferred.
#include "node.h"
#include "frame_codec.h"

#include <span>

namespace meshroute {

static inline const char* id_bind_conf_str(Node::IdBindConf c) {
    return (c == Node::IdBindConf::authoritative) ? "authoritative" : "claimed";
}
static inline const char* id_bind_source_str(Node::IdBindSource s) {
    switch (s) {
        case Node::IdBindSource::self:    return "self";
        case Node::IdBindSource::bcn:     return "bcn";
        case Node::IdBindSource::h_query: return "h_query";
        case Node::IdBindSource::h_relay: return "h_relay";
    }
    return "unknown";
}

// Evict any binding for `key_hash32` held by a node_id OTHER than keep_node_id — the REJOIN SELF-HEAL: a
// hash maps to exactly ONE node_id, so when a node rehomes (new id, same key) the stale id->hash entry
// must go, or id_bind_find_by_hash returns an ambiguous (often dead) id. Compaction; returns # evicted.
uint8_t Node::id_bind_evict_other_hash_holders(uint32_t key_hash32, uint8_t keep_node_id) {
    uint16_t w = 0; uint8_t evicted = 0;
    for (uint16_t r = 0; r < _id_bind_n; ++r) {
        if (_id_bind[r].key_hash32 == key_hash32 && _id_bind[r].node_id != keep_node_id) { ++evicted; continue; }
        _id_bind[w++] = _id_bind[r];
    }
    _id_bind_n = w;
    return evicted;
}

// Insert/update a binding (Lua id_bind_set dv:4677, + the rejoin/authoritative amendments). Maintains the
// (node_id <-> key_hash32) bijection: dedup-by-hash on accept (one hash -> one id), and a same-id CONFLICT
// (a different hash claims this node_id) is OVERWRITTEN by an authoritative source (self / owner-confirmed
// hash-bind) but REFUSED for a claimed one (emit addr_conflict_observed; the join-defense J_DENY stays
// deferred). NEW + table full -> table_cap_hit refuse. Only a NEW node_id emits id_bind_set (an update is
// silent — the Lua is_new gate). Returns true if the binding is now present.
bool Node::id_bind_set(uint8_t node_id, uint32_t key_hash32, IdBindSource source, IdBindConf confidence) {
    if (node_id == 0xFF) return false;                           // reserved id
    const uint64_t now = _hal.now();
    const bool authoritative = (confidence == IdBindConf::authoritative);
    for (uint16_t i = 0; i < _id_bind_n; ++i) {                  // existing entry for this node_id?
        if (_id_bind[i].node_id != node_id) continue;
        if (_id_bind[i].key_hash32 != key_hash32) {              // CONFLICT: a different hash claims this id
            if (node_id == _node_id && _id_bind[i].key_hash32 == _key_hash32) {
                // Our OWN self-binding is the root of trust — NEVER let any source (even an authoritative
                // H answer resolving a colliding hash back to our id, as the L2c redirect can trigger)
                // overwrite our id->our-key mapping. A foreign key on our id is a collision to DEFEND, not
                // to absorb (the beacon/join defense + L2c handle that); absorbing it would corrupt every
                // hash-locate answer we give for ourselves.
                MR_TELEMETRY(
                    EventField f[] = { { .key = "node",                .type = EventField::T::i64, .i = node_id },
                                       { .key = "observed_key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) },
                                       { .key = "source",              .type = EventField::T::str, .s = id_bind_source_str(source) } };
                    _hal.emit("addr_conflict_self_defended", f, 3); );
                return false;
            }
            if (!authoritative) {                               // claimed -> refuse, keep the known binding
                MR_TELEMETRY(
                    EventField f[] = { { .key = "node",                .type = EventField::T::i64, .i = node_id },
                                       { .key = "known_key_hash32",    .type = EventField::T::i64, .i = static_cast<int64_t>(_id_bind[i].key_hash32) },
                                       { .key = "observed_key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) },
                                       { .key = "source",              .type = EventField::T::str, .s = id_bind_source_str(source) } };
                    _hal.emit("addr_conflict_observed", f, 4); );
                return false;
            }
            // L2a shared-neighbour heal: a FIRST-HAND beacon (source==bcn, §5.5 confidence gate) for an id we
            // already hold bound to a DIFFERENT hash, and it is NOT our own id -> we heard two nodes use the
            // same id. Mediate (we hold both full hashes first-hand): deny the key-loser (§6 key-only) so it
            // renumbers, keep the winner. Without this the binding just flaps (and corrupts our H answers).
            // Gate (§5.5 first-hand): a genuine BEACON learn = source==bcn AND authoritative. The `&&
            // authoritative` is what makes this self-sufficient — a J-frame learn also uses source==bcn but
            // is `claimed` (refused above at !authoritative), so without this the gate would lean on that
            // upstream refuse alone (the agent's confidence-gate catch).
            if (node_id != _node_id && source == IdBindSource::bcn && authoritative
                && _id_bind[i].confidence == static_cast<uint8_t>(IdBindConf::authoritative)) {
                const uint32_t existing_key = _id_bind[i].key_hash32;
                const bool incoming_wins = join_tiebreak_wins(0, key_hash32, 0, existing_key);
                const uint32_t winner = incoming_wins ? key_hash32 : existing_key;
                const uint32_t loser  = incoming_wins ? existing_key : key_hash32;
                if (!mediated_recently(node_id, loser)) {       // #1: one DENY per (id,loser) per window, not per beacon
                    MR_TELEMETRY(
                        EventField f[] = { { .key = "node",   .type = EventField::T::i64, .i = node_id },
                                           { .key = "winner", .type = EventField::T::i64, .i = static_cast<int64_t>(winner) },
                                           { .key = "loser",  .type = EventField::T::i64, .i = static_cast<int64_t>(loser) } };
                        _hal.emit("addr_conflict_mediated", f, 3); );
                    addr_conflict_send_deny(node_id, winner, loser, J_DENY_MEDIATED);
                    mark_mediated(node_id, loser);
                }
                // We always take the incoming below (so a legitimate same-node re-key still applies + the
                // binding can't get stuck on a departed loser); the DENY drives the key-loser to renumber,
                // and the winner's next beacon re-asserts it — the flap is transient, convergence is the DENY.
            }
            _id_bind[i].key_hash32 = key_hash32;                // authoritative -> overwrite the hash (incoming wins / same-node rekey)
        }
        _id_bind[i].last_seen_ms = now;                         // refresh (silent — not new)
        _id_bind[i].source       = static_cast<uint8_t>(source);
        _id_bind[i].confidence   = static_cast<uint8_t>(confidence);
        id_bind_evict_other_hash_holders(key_hash32, node_id);  // one hash -> one id (heal a same-hash rehome)
        return true;
    }
    // NEW node_id: heal any stale holder of this hash FIRST (a pure rehome frees its slot), then cap-check.
    id_bind_evict_other_hash_holders(key_hash32, node_id);
    if (_id_bind_n >= _cfg.cap_id_bind) {                        // table full -> refuse (Lua dv:4707)
        MR_TELEMETRY(
            EventField f[] = { { .key = "table",  .type = EventField::T::str, .s = "id_bind" },
                               { .key = "cap",    .type = EventField::T::i64, .i = _cfg.cap_id_bind },
                               { .key = "size",   .type = EventField::T::i64, .i = _id_bind_n },
                               { .key = "action", .type = EventField::T::str, .s = "refuse" },
                               { .key = "node",   .type = EventField::T::i64, .i = node_id } };
            _hal.emit("table_cap_hit", f, 5); );
        return false;
    }
    _id_bind[_id_bind_n++] = { key_hash32, now, node_id, static_cast<uint8_t>(source), static_cast<uint8_t>(confidence) };
    MR_TELEMETRY(
        EventField f[] = { { .key = "node",       .type = EventField::T::i64, .i = node_id },
                           { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) },
                           { .key = "source",     .type = EventField::T::str, .s = id_bind_source_str(source) },
                           { .key = "confidence", .type = EventField::T::str, .s = id_bind_conf_str(confidence) } };
        _hal.emit("id_bind_set", f, 4); );
    return true;
}

// Find a NON-EXPIRED binding for key_hash32 -> its node_id (Lua id_bind_find_by_hash dv:4764). Skips (does
// not remove) expired entries — removal is the periodic age_out sweep. The self-binding never expires.
// This is the call that makes "any node that knows answers" work. Returns -1 on miss.
int Node::id_bind_find_by_hash(uint32_t key_hash32, IdBindConf* conf_out) {
    const uint64_t now = _hal.now();
    for (uint16_t i = 0; i < _id_bind_n; ++i) {
        if (_id_bind[i].key_hash32 != key_hash32) continue;
        const bool self_keep = (_id_bind[i].node_id == _node_id && _id_bind[i].key_hash32 == _key_hash32);
        if (!self_keep && _cfg.id_bind_ttl_ms > 0
            && (now - _id_bind[i].last_seen_ms) >= _cfg.id_bind_ttl_ms) continue;   // expired -> skip
        if (conf_out) *conf_out = static_cast<IdBindConf>(_id_bind[i].confidence);  // soft/hard for the H resolver
        return _id_bind[i].node_id;
    }
    return -1;
}

// Reverse lookup: a node_id -> its stable key_hash32 (the inverse of id_bind_find_by_hash). Used by the
// send path to stamp DST_HASH (L2c verify-on-delivery) on an app DM. ONLY an AUTHORITATIVE (owner-confirmed
// / first-hand beacon / self) binding qualifies — a CLAIMED (second-hand / relayed) binding can be stale and
// would stamp a wrong dst_hash that triggers a spurious redirect at the recipient (mirrors send_by_hash's
// trust model, which HARD-verifies a soft binding before use). Returns false (DST_HASH omitted) otherwise.
bool Node::key_hash_of_id(uint8_t id, uint32_t& out) const {
    const uint64_t now = _hal.now();
    for (uint16_t i = 0; i < _id_bind_n; ++i) {
        if (_id_bind[i].node_id != id) continue;
        if (_id_bind[i].confidence != static_cast<uint8_t>(IdBindConf::authoritative)) continue;  // confident only
        const bool self_keep = (id == _node_id && _id_bind[i].key_hash32 == _key_hash32);
        if (!self_keep && _cfg.id_bind_ttl_ms > 0
            && (now - _id_bind[i].last_seen_ms) >= _cfg.id_bind_ttl_ms) continue;     // expired
        out = _id_bind[i].key_hash32;
        return true;
    }
    return false;
}

// Drop expired bindings (TTL on last_seen_ms; Lua id_bind_age_one dv:4753). The self-binding is exempt.
// Periodic sweep (kAgingTimerId, alongside age_out_stale_routes). Compaction is load-bearing (outside
// the telemetry wrap); only the id_bind_aged emit strips on the device.
void Node::id_bind_age_out() {
    if (_cfg.id_bind_ttl_ms == 0) return;
    const uint64_t now = _hal.now();
    uint16_t w = 0;
    for (uint16_t r = 0; r < _id_bind_n; ++r) {
        const IdBind e = _id_bind[r];
        const bool self_keep = (e.node_id == _node_id && e.key_hash32 == _key_hash32);
        if (!self_keep && (now - e.last_seen_ms) >= _cfg.id_bind_ttl_ms) {
            MR_TELEMETRY(
                EventField f[] = { { .key = "node",       .type = EventField::T::i64, .i = e.node_id },
                                   { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(e.key_hash32) },
                                   { .key = "age_ms",     .type = EventField::T::i64, .i = static_cast<int64_t>(now - e.last_seen_ms) },
                                   { .key = "ttl_ms",     .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.id_bind_ttl_ms) } };
                _hal.emit("id_bind_aged", f, 4); );
            continue;                                            // drop (don't keep)
        }
        _id_bind[w++] = e;
    }
    _id_bind_n = w;
}

// =============================================================================
// Phase A — the H flood + resolve handler. The defining behaviour: ANY node that
// already holds the binding answers + STOPS the flood; the flood is the fallback.
// =============================================================================

// per-(origin, key_hash32, VARIANT) flood dedup (Lua hash_query_seen; hash_query_seen_ttl_ms window). Keying on
// `hard` is load-bearing: a HARD query (verify-on-use) must NOT be suppressed by a prior SOFT's seen-entry, or
// the escalation that reaches the owner is silently swallowed. Mirrors rreq_seen.
bool Node::hash_query_seen_recently(uint8_t origin, uint32_t key_hash32, bool hard) {
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::hash_query_seen_ttl_ms) ? now - protocol::hash_query_seen_ttl_ms : 0;
    for (uint8_t i = 0; i < _hash_query_seen_n; ++i)
        if (_hash_query_seen[i].origin == origin && _hash_query_seen[i].key_hash32 == key_hash32
            && _hash_query_seen[i].hard == hard && _hash_query_seen[i].t_ms >= cutoff) return true;
    return false;
}
void Node::mark_hash_query_seen(uint8_t origin, uint32_t key_hash32, bool hard) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _hash_query_seen_n; ++i)
        if (_hash_query_seen[i].origin == origin && _hash_query_seen[i].key_hash32 == key_hash32
            && _hash_query_seen[i].hard == hard) { _hash_query_seen[i].t_ms = now; return; }
    if (_hash_query_seen_n < protocol::cap_hash_query_seen) {
        _hash_query_seen[_hash_query_seen_n++] = { origin, key_hash32, now, hard };
    } else {                                              // ring full -> evict the oldest
        uint8_t o = 0;
        for (uint8_t i = 1; i < _hash_query_seen_n; ++i) if (_hash_query_seen[i].t_ms < _hash_query_seen[o].t_ms) o = i;
        _hash_query_seen[o] = { origin, key_hash32, now, hard };
    }
}

// H query flood handler (Lua dv:11628-11671). RESOLVE from own-hash (HARD) or a cached binding (its stored
// confidence) -> answer + SUPPRESS the forward (the flood stops at the first knowledgeable node). Else FORWARD
// with TTL-1 (deduped per origin+hash). The H frame carries no relay field, so there is no rx-source learn —
// the hash-bind response (Phase B) routes home via the existing rt[origin]. Same-layer (leaf-scoped) MVP.
void Node::handle_h(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    (void)meta;
    auto ph = parse_h(std::span<const uint8_t>(bytes, len));
    if (!ph) return;
    const h_out& h = *ph;
    if (h.leaf_id != _cfg.leaf_id) return;                 // foreign-layer (dv:11635)
    if (h.origin == _node_id) return;                      // our own query echoed back (dv:11637)
    MR_TELEMETRY(
        EventField f[] = { { .key = "origin",     .type = EventField::T::i64,     .i = h.origin },
                           { .key = "key_hash32", .type = EventField::T::i64,     .i = static_cast<int64_t>(h.key_hash32) },
                           { .key = "ttl",        .type = EventField::T::i64,     .i = h.ttl },
                           { .key = "hard",       .type = EventField::T::boolean, .b = h.hard } };
        _hal.emit("h_rx", f, 4); );                        // dv:11638

    // Resolve. SOFT query (default): own-hash OR any cached binding answers ("anyone who knows"). HARD query
    // (verify-on-use, dv §3.7a): resolve ONLY via own-hash — SKIP the cache so it reaches the OWNER for an
    // authoritative correction. A cached binding carries its own confidence (beacon = authoritative/first-hand;
    // snooped hash-bind = claimed/second-hand, Phase C).
    int node_id = -1; bool authoritative = false;
    if (h.key_hash32 == _key_hash32) { node_id = _node_id; authoritative = true; }   // own-hash: resolves either variant
    else if (!h.hard) {                                                              // HARD skips the cache -> flood to the owner
        IdBindConf conf = IdBindConf::claimed;
        const int found = id_bind_find_by_hash(h.key_hash32, &conf);
        if (found >= 0) { node_id = found; authoritative = (conf == IdBindConf::authoritative); }
    }

    if (node_id >= 0) {                                    // RESOLVER path (dv:11644) — answer + SUPPRESS the forward
        mark_hash_query_seen(h.origin, h.key_hash32, h.hard);   // mark BEFORE replying so a re-flood doesn't double-answer (dv:11647)
        MR_TELEMETRY(
            EventField f[] = { { .key = "origin",        .type = EventField::T::i64,     .i = h.origin },
                               { .key = "key_hash32",    .type = EventField::T::i64,     .i = static_cast<int64_t>(h.key_hash32) },
                               { .key = "node",          .type = EventField::T::i64,     .i = node_id },
                               { .key = "target_layer",  .type = EventField::T::i64,     .i = _cfg.leaf_id },
                               { .key = "authoritative", .type = EventField::T::boolean, .b = authoritative } };
            _hal.emit("h_resolved", f, 5); );              // dv:11649
        send_hash_bind_response(h.origin, _cfg.leaf_id, static_cast<uint8_t>(node_id), h.key_hash32, authoritative);
        return;                                            // SUPPRESS — the whole point: the flood stops here
    }

    // FORWARD path (dv:11655): we don't know it (or it's a HARD query and we're not the owner) -> re-broadcast
    // once, deduped per variant, until TTL runs out.
    if (hash_query_seen_recently(h.origin, h.key_hash32, h.hard)) return;   // flood dedup (dv:11656)
    mark_hash_query_seen(h.origin, h.key_hash32, h.hard);                   // (dv:11657)
    if (h.ttl == 0) return;                                         // TTL exhausted (dv:11658)
    MR_TELEMETRY(
        EventField f[] = { { .key = "origin",     .type = EventField::T::i64,     .i = h.origin },
                           { .key = "key_hash32", .type = EventField::T::i64,     .i = static_cast<int64_t>(h.key_hash32) },
                           { .key = "ttl",        .type = EventField::T::i64,     .i = static_cast<int64_t>(h.ttl - 1) },
                           { .key = "hard",       .type = EventField::T::boolean, .b = h.hard } };
        _hal.emit("h_forward", f, 4); );                   // dv:11661
    h_in fwd{};
    fwd.leaf_id = _cfg.leaf_id; fwd.origin = h.origin; fwd.key_hash32 = h.key_hash32;
    fwd.ttl = static_cast<uint8_t>(h.ttl - 1); fwd.hard = h.hard;   // preserve the variant across forwards
    uint8_t buf[8];
    const size_t n = pack_h(fwd, std::span<uint8_t>(buf, sizeof(buf)));
    if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// =============================================================================
// Phase B — the hash-bind RESPONSE (a routed DATA with the H_ANSWER inner) + the
// origin's receive (parse here; cache + drain the parked send-by-hash is Phase C).
// =============================================================================

// Enqueue a normal DATA carrying the H_ANSWER inner, addressed to the H-query origin; it routes home
// hop-by-hop on the existing rt[origin] (the H flood lays no reverse path). AUTHORITATIVE = the resolver
// answered as the owner (matches_self), not from a cached binding. (Lua send_hash_bind_response dv:5877.)
void Node::send_hash_bind_response(uint8_t to_origin, uint8_t target_layer, uint8_t node_id,
                                   uint32_t key_hash32, bool authoritative) {
    if (_tx_queue_n >= kTxQueueCap) return;                       // queue full -> drop (the querier can re-flood)
    hash_bind_inner hb{};
    hb.target_layer = target_layer; hb.node_id = node_id; hb.key_hash32 = key_hash32; hb.authoritative = authoritative;
    uint8_t inner[7];
    const size_t n = pack_hash_bind_inner(hb, std::span<uint8_t>(inner, sizeof(inner)));
    if (n == 0) return;
    TxItem item{};
    item.origin = _node_id; item.dst = to_origin;
    item.ctr = next_ctr(to_origin); item.ctr_lo = static_cast<uint8_t>(item.ctr & 0x0F);
    item.flags = 0;                                              // NORMAL DATA — typed by the H_ANSWER payload-flag, NOT payload_type_m
    for (size_t i = 0; i < n; ++i) item.inner[i] = inner[i];
    item.inner_len = static_cast<uint8_t>(n);
    item.enqueue_time_ms = _hal.now();
    _tx_queue[_tx_queue_n++] = item;
    MR_TELEMETRY(
        EventField f[] = { { .key = "to",            .type = EventField::T::i64,     .i = to_origin },
                           { .key = "node",          .type = EventField::T::i64,     .i = node_id },
                           { .key = "key_hash32",    .type = EventField::T::i64,     .i = static_cast<int64_t>(key_hash32) },
                           { .key = "authoritative", .type = EventField::T::boolean, .b = authoritative } };
        _hal.emit("hash_bind_response_enqueued", f, 4); );        // dv:5897
    become_free();                                               // kick the queue to route the answer home
}

// The querier received a DATA whose inner is a hash-bind answer (handle_data routed it here off the
// H_ANSWER payload-flag). Phase B: parse + emit. Phase C will id_bind_set(h_query, conf) + drain the
// parked send-by-hash. DELIBERATELY does NOT deliver as a DM (it is routing/identity info, not user content).
void Node::on_hash_bind_response(const uint8_t* inner, uint8_t inner_len) {
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, inner_len));
    if (!hb) return;
    // C.1 destination consume: WE asked -> source h_query; the answer's AUTHORITATIVE flag carries the
    // confidence (an owner answer is authoritative, a cache-relayed soft answer is claimed -> verify-on-use).
    id_bind_set(hb->node_id, hb->key_hash32, IdBindSource::h_query,
                hb->authoritative ? IdBindConf::authoritative : IdBindConf::claimed);
    MR_TELEMETRY(
        EventField f[] = { { .key = "node",          .type = EventField::T::i64,     .i = hb->node_id },
                           { .key = "key_hash32",    .type = EventField::T::i64,     .i = static_cast<int64_t>(hb->key_hash32) },
                           { .key = "target_layer",  .type = EventField::T::i64,     .i = hb->target_layer },
                           { .key = "authoritative", .type = EventField::T::boolean, .b = hb->authoritative } };
        _hal.emit("hash_bind_rx", f, 4); );
    drain_parked_sends(hb->key_hash32, hb->node_id);   // D: a parked send-by-hash for this hash can now fly to the resolved id
}

// C.2 cache-on-pass (NEW, beyond the Lua's gateway-only caching): a RELAYED hash-bind answer is
// forwarder-readable (CLEARTEXT) — snoop the binding so every node on the return path becomes a future
// resolver and repeat H floods shrink (measured in the Phase D multi-node sim). source = h_relay (snooped,
// distinct from the asked h_query); confidence rides the answer's AUTHORITATIVE flag. We do NOT consume —
// do_post_ack still forwards the DATA. Deliberate, measurable divergence (gate: flood reach trends down).
void Node::on_hash_bind_snoop(const uint8_t* inner, uint8_t inner_len) {
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, inner_len));
    if (!hb) return;
    id_bind_set(hb->node_id, hb->key_hash32, IdBindSource::h_relay,
                hb->authoritative ? IdBindConf::authoritative : IdBindConf::claimed);
    MR_TELEMETRY(
        EventField f[] = { { .key = "node",          .type = EventField::T::i64,     .i = hb->node_id },
                           { .key = "key_hash32",    .type = EventField::T::i64,     .i = static_cast<int64_t>(hb->key_hash32) },
                           { .key = "authoritative", .type = EventField::T::boolean, .b = hb->authoritative } };
        _hal.emit("hash_bind_snooped", f, 3); );
}

// =============================================================================
// Phase D — the send-by-hash trigger + verify-on-use. Address a DM by the target's
// stable key_hash32: send now if we hold an AUTHORITATIVE binding; else park the DM
// + flood an H query (a SOFT cached binding is HARD-verified before use) and fly it
// when the hash-bind answer resolves the id.
// =============================================================================

// on_command(send) routes here when dst_hash != 0 (the deferred "address by key_hash32"). Returns the DM ctr
// if sent immediately, else 0 (parked/resolving — the ctr is assigned when the binding arrives).
uint16_t Node::send_by_hash(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags) {
    IdBindConf conf = IdBindConf::claimed;
    const int id = id_bind_find_by_hash(key_hash32, &conf);
    if (id >= 0 && conf == IdBindConf::authoritative)            // confident binding -> send NOW
        return do_send(static_cast<uint8_t>(id), body, body_len, flags);
    // SOFT cached binding -> HARD verify-on-use (reach the owner for a correction); UNKNOWN -> SOFT flood.
    park_send(key_hash32, body, body_len, flags);
    emit_hash_query(key_hash32, /*hard=*/(id >= 0));
    return 0;
}

// Originate an H flood for key_hash32 (Lua send_hash_query dv:5625). hard = the verify-on-use escalation.
void Node::emit_hash_query(uint32_t key_hash32, bool hard) {
    if (key_hash32 == 0 || key_hash32 == _key_hash32) return;    // nothing to locate (degenerate / it's us)
    h_in in{};
    in.leaf_id = _cfg.leaf_id; in.origin = _node_id; in.key_hash32 = key_hash32;
    in.ttl = protocol::hash_query_max_ttl; in.hard = hard;
    uint8_t buf[8];
    const size_t n = pack_h(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    MR_TELEMETRY(
        EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64,     .i = static_cast<int64_t>(key_hash32) },
                           { .key = "ttl",        .type = EventField::T::i64,     .i = protocol::hash_query_max_ttl },
                           { .key = "hard",       .type = EventField::T::boolean, .b = hard } };
        _hal.emit("h_tx", f, 3); );                              // the originate (dv:5625)
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

void Node::park_send(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags) {
    if (_parked_sends_n >= protocol::cap_parked_sends) return;   // full -> drop (the app can retry)
    ParkedSend& p = _parked_sends[_parked_sends_n++];
    p = ParkedSend{};                                           // reset a RECYCLED slot: the array is compacted in
    // place (drain/age-out never clear vacated slots), so a slot last used by an L2c redirect would otherwise
    // keep is_redirect=true and mis-route this plain send-by-hash through the redirect branch on drain.
    p.key_hash32 = key_hash32; p.flags = flags; p.parked_at_ms = _hal.now();
    // Clamp to the DM body cap (NOT the 235-B inner buffer): drain_parked_sends -> do_send -> enqueue_data
    // writes body at inner[2+i], so a >233 body would overrun inner[]. on_command already rejects oversize
    // (err_too_large) — this is defense-in-depth so a parked body can never exceed the deliverable size.
    p.body_len = (body_len > protocol::dm_max_body_bytes) ? protocol::dm_max_body_bytes : body_len;
    for (uint8_t i = 0; i < p.body_len; ++i) p.body[i] = body[i];
    MR_TELEMETRY(
        EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) } };
        _hal.emit("send_parked_for_hash", f, 1); );
}

// L2c: hold a misdelivered DM awaiting the HARD-H resolution. Unlike park_send (a fresh origination), this
// stores the FULL inner (incl. DST_HASH) + the original origin/ctr/flags so drain FORWARDS it identity-intact.
void Node::l2c_park_redirect(uint32_t want_hash, const PostAck& pa) {
    if (_parked_sends_n >= protocol::cap_parked_sends) return;   // full -> drop (the sender retries)
    ParkedSend& p = _parked_sends[_parked_sends_n++];
    p = ParkedSend{};                                           // reset the recycled slot before stamping redirect state
    p.key_hash32 = want_hash; p.flags = pa.flags; p.parked_at_ms = _hal.now();
    p.is_redirect = true; p.origin = pa.origin; p.ctr = pa.ctr; p.ctr_lo = pa.ctr_lo;
    p.body_len = (pa.inner_len > protocol::max_payload_bytes_hard_cap) ? protocol::max_payload_bytes_hard_cap : pa.inner_len;
    for (uint8_t i = 0; i < p.body_len; ++i) p.body[i] = pa.inner[i];   // body[] holds the full inner for a redirect
    MR_TELEMETRY(
        EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(want_hash) },
                           { .key = "origin",     .type = EventField::T::i64, .i = pa.origin },
                           { .key = "ctr",        .type = EventField::T::i64, .i = pa.ctr } };
        _hal.emit("l2c_redirect_parked", f, 3); );
}

// A binding for key_hash32 just resolved -> fly every parked DM for it to resolved_id (the verify-on-use redirect:
// the id comes from the hash-bind ANSWER, so a stale soft binding is corrected here). A redirect (L2c) entry is
// FORWARDED identity-intact; resolved_id == OUR id means the want_hash owner holds our id => a CONFIRMED
// collision: heal (key-only) instead of forwarding-to-self (which would loop). A plain send-by-hash re-sends.
void Node::drain_parked_sends(uint32_t key_hash32, uint8_t resolved_id) {
    bool heal = false;                                            // a confirmed collision found this pass
    uint8_t w = 0;
    for (uint8_t r = 0; r < _parked_sends_n; ++r) {
        const ParkedSend p = _parked_sends[r];
        if (p.key_hash32 == key_hash32) {
            if (p.is_redirect) {
                if (resolved_id == _node_id) {
                    // Proven same-id collision (the owner of want_hash holds OUR id). DEFER the heal to AFTER
                    // the loop: l2c_confirmed_collision -> forced_rejoin mutates _node_id, and running it here
                    // would corrupt a sibling parked entry processed later in this same loop. The DM is dropped
                    // (forwarding-to-self loops); the sender's retry recovers it once the heal converges.
                    heal = true;
                } else if (l2c_enqueue_forward(resolved_id, p.origin, p.ctr, p.ctr_lo, p.flags, p.body, p.body_len)) {
                    MR_TELEMETRY(
                        EventField f[] = { { .key = "to",     .type = EventField::T::i64, .i = resolved_id },
                                           { .key = "origin", .type = EventField::T::i64, .i = p.origin },
                                           { .key = "ctr",    .type = EventField::T::i64, .i = p.ctr } };
                        _hal.emit("l2c_redirect_forward", f, 3); );      // recipient moved (stale binding) -> forward, no heal
                } else {
                    _parked_sends[w++] = p;                          // queue full -> KEEP parked, retry next drain/age-out
                }
            } else if (resolved_id == _node_id) {
                // A plain send-by-hash that resolves to OUR OWN id: the app addressed its own key, or a same-id
                // collision aliased it to us. Do NOT do_send-to-self (a self-addressed DM); give it up.
                MR_TELEMETRY(
                    EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) } };
                    _hal.emit("send_hash_giveup", f, 1); );
            } else {
                MR_TELEMETRY(
                    EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) },
                                       { .key = "node",       .type = EventField::T::i64, .i = resolved_id } };
                    _hal.emit("send_hash_resolved", f, 2); );
                do_send(resolved_id, p.body, p.body_len, p.flags);   // load-bearing (OUTSIDE the wrap): fly the held DM
            }
            continue;                                            // matched entry handled (forwarded / healed / kept-above / given up)
        }
        _parked_sends[w++] = p;
    }
    _parked_sends_n = w;
    if (heal) l2c_confirmed_collision(key_hash32);               // AFTER the loop -> forced_rejoin can't corrupt siblings
}

// Re-drain parked sends whose hash has SINCE gained an AUTHORITATIVE binding from a source other than the
// hash-bind answer we floods-and-waits for — typically the owner's periodic beacon arriving after the one H
// answer was lost. The hash-keyed analog of try_drain_deferred (which re-drains route-blocked sends on each
// beacon): it keeps a parked DM from aging out to send_hash_giveup when the node already holds everything it
// needs to deliver. Only AUTHORITATIVE bindings fire — a claimed (second-hand) binding still wants verify-on-use.
void Node::drain_resolved_parked_sends() {
    if (_parked_sends_n == 0) return;
    uint8_t w = 0;
    for (uint8_t r = 0; r < _parked_sends_n; ++r) {
        const ParkedSend p = _parked_sends[r];
        IdBindConf conf = IdBindConf::claimed;
        const int id = id_bind_find_by_hash(p.key_hash32, &conf);
        if (id >= 0 && conf == IdBindConf::authoritative && static_cast<uint8_t>(id) != _node_id) {
            if (p.is_redirect) {
                if (l2c_enqueue_forward(static_cast<uint8_t>(id), p.origin, p.ctr, p.ctr_lo, p.flags, p.body, p.body_len)) {
                    MR_TELEMETRY(
                        EventField f[] = { { .key = "to",     .type = EventField::T::i64, .i = id },
                                           { .key = "origin", .type = EventField::T::i64, .i = p.origin },
                                           { .key = "ctr",    .type = EventField::T::i64, .i = p.ctr } };
                        _hal.emit("l2c_redirect_forward", f, 3); );
                } else {
                    _parked_sends[w++] = p;                          // queue full -> KEEP parked for the next beacon/age-out
                    continue;
                }
            } else {
                MR_TELEMETRY(
                    EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(p.key_hash32) },
                                       { .key = "node",       .type = EventField::T::i64, .i = id } };
                    _hal.emit("send_hash_resolved", f, 2); );
                do_send(static_cast<uint8_t>(id), p.body, p.body_len, p.flags);   // load-bearing (OUTSIDE the wrap)
            }
            continue;                                            // drop the parked entry (forwarded / sent)
        }
        _parked_sends[w++] = p;
    }
    _parked_sends_n = w;
}

// Give up on parked sends whose hash never resolved (periodic, on kAgingTimerId). send_defer_ttl_ms window.
void Node::age_out_parked_sends() {
    if (_parked_sends_n == 0) return;
    const uint64_t now = _hal.now();
    uint8_t w = 0;
    for (uint8_t r = 0; r < _parked_sends_n; ++r) {
        const ParkedSend p = _parked_sends[r];
        if ((now - p.parked_at_ms) >= protocol::send_defer_ttl_ms) {
            MR_TELEMETRY(
                EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(p.key_hash32) } };
                _hal.emit("send_hash_giveup", f, 1); );
            continue;                                            // drop (the hash didn't resolve in time)
        }
        _parked_sends[w++] = p;
    }
    _parked_sends_n = w;
}

}  // namespace meshroute
