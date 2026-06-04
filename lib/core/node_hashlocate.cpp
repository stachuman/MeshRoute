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
            if (!authoritative) {                               // claimed -> refuse, keep the known binding
                MR_TELEMETRY(
                    EventField f[] = { { .key = "node",                .type = EventField::T::i64, .i = node_id },
                                       { .key = "known_key_hash32",    .type = EventField::T::i64, .i = static_cast<int64_t>(_id_bind[i].key_hash32) },
                                       { .key = "observed_key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) },
                                       { .key = "source",              .type = EventField::T::str, .s = id_bind_source_str(source) } };
                    _hal.emit("addr_conflict_observed", f, 4); );
                return false;
            }
            _id_bind[i].key_hash32 = key_hash32;                // authoritative -> overwrite the hash
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

// per-(origin, key_hash32) flood dedup (Lua hash_query_seen; hash_query_seen_ttl_ms window). Mirrors rreq_seen.
bool Node::hash_query_seen_recently(uint8_t origin, uint32_t key_hash32) {
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::hash_query_seen_ttl_ms) ? now - protocol::hash_query_seen_ttl_ms : 0;
    for (uint8_t i = 0; i < _hash_query_seen_n; ++i)
        if (_hash_query_seen[i].origin == origin && _hash_query_seen[i].key_hash32 == key_hash32
            && _hash_query_seen[i].t_ms >= cutoff) return true;
    return false;
}
void Node::mark_hash_query_seen(uint8_t origin, uint32_t key_hash32) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _hash_query_seen_n; ++i)
        if (_hash_query_seen[i].origin == origin && _hash_query_seen[i].key_hash32 == key_hash32) {
            _hash_query_seen[i].t_ms = now; return;
        }
    if (_hash_query_seen_n < protocol::cap_hash_query_seen) {
        _hash_query_seen[_hash_query_seen_n++] = { origin, key_hash32, now };
    } else {                                              // ring full -> evict the oldest
        uint8_t o = 0;
        for (uint8_t i = 1; i < _hash_query_seen_n; ++i) if (_hash_query_seen[i].t_ms < _hash_query_seen[o].t_ms) o = i;
        _hash_query_seen[o] = { origin, key_hash32, now };
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
        EventField f[] = { { .key = "origin",     .type = EventField::T::i64, .i = h.origin },
                           { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(h.key_hash32) },
                           { .key = "ttl",        .type = EventField::T::i64, .i = h.ttl } };
        _hal.emit("h_rx", f, 3); );                        // dv:11638

    // Resolve (dv:11641): own hash = HARD (we ARE the owner); else a cached binding carries its own
    // confidence (a beacon = authoritative/first-hand; a snooped hash-bind = claimed/second-hand, Phase C).
    int node_id = -1; [[maybe_unused]] bool authoritative = false;   // [[maybe_unused]]: telemetry-only until Phase B passes it to the response
    if (h.key_hash32 == _key_hash32) { node_id = _node_id; authoritative = true; }
    else {
        IdBindConf conf = IdBindConf::claimed;
        const int found = id_bind_find_by_hash(h.key_hash32, &conf);
        if (found >= 0) { node_id = found; authoritative = (conf == IdBindConf::authoritative); }
    }

    if (node_id >= 0) {                                    // RESOLVER path (dv:11644) — answer + SUPPRESS the forward
        mark_hash_query_seen(h.origin, h.key_hash32);      // mark BEFORE replying so a re-flood doesn't double-answer (dv:11647)
        MR_TELEMETRY(
            EventField f[] = { { .key = "origin",        .type = EventField::T::i64,     .i = h.origin },
                               { .key = "key_hash32",    .type = EventField::T::i64,     .i = static_cast<int64_t>(h.key_hash32) },
                               { .key = "node",          .type = EventField::T::i64,     .i = node_id },
                               { .key = "target_layer",  .type = EventField::T::i64,     .i = _cfg.leaf_id },
                               { .key = "authoritative", .type = EventField::T::boolean, .b = authoritative } };
            _hal.emit("h_resolved", f, 5); );              // dv:11649
        // Phase B: send_hash_bind_response(h.origin, _cfg.leaf_id, node_id, h.key_hash32, authoritative);
        return;                                            // SUPPRESS — the whole point: the flood stops here
    }

    // FORWARD path (dv:11655): we don't know it -> re-broadcast once, deduped, until TTL runs out.
    if (hash_query_seen_recently(h.origin, h.key_hash32)) return;   // flood dedup (dv:11656)
    mark_hash_query_seen(h.origin, h.key_hash32);                   // (dv:11657)
    if (h.ttl == 0) return;                                         // TTL exhausted (dv:11658)
    MR_TELEMETRY(
        EventField f[] = { { .key = "origin",     .type = EventField::T::i64, .i = h.origin },
                           { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(h.key_hash32) },
                           { .key = "ttl",        .type = EventField::T::i64, .i = static_cast<int64_t>(h.ttl - 1) } };
        _hal.emit("h_forward", f, 3); );                   // dv:11661
    h_in fwd{};
    fwd.leaf_id = _cfg.leaf_id; fwd.origin = h.origin; fwd.key_hash32 = h.key_hash32;
    fwd.ttl = static_cast<uint8_t>(h.ttl - 1);
    uint8_t buf[8];
    const size_t n = pack_h(fwd, std::span<uint8_t>(buf, sizeof(buf)));
    if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

}  // namespace meshroute
