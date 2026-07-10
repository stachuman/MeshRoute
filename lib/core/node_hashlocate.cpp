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
#include "identity.h"      // ed_pub_to_x25519 (E2E ECDH)
#include "dm_crypto.h"     // dm_kdf / dm_nonce / dm_seal / dm_open (E2E seal/open)
#include "monocypher.h"    // crypto_x25519 / crypto_wipe

#include <span>

namespace MESHROUTE_NS {

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
    for (uint16_t r = 0; r < _active->_id_bind_n; ++r) {
        if (_active->_id_bind[r].key_hash32 == key_hash32 && _active->_id_bind[r].node_id != keep_node_id) { ++evicted; continue; }
        _active->_id_bind[w++] = _active->_id_bind[r];
    }
    _active->_id_bind_n = w;
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
    for (uint16_t i = 0; i < _active->_id_bind_n; ++i) {                  // existing entry for this node_id?
        if (_active->_id_bind[i].node_id != node_id) continue;
        if (_active->_id_bind[i].key_hash32 != key_hash32) {              // CONFLICT: a different hash claims this id
            if (node_id == _node_id && _active->_id_bind[i].key_hash32 == _key_hash32) {
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
                                       { .key = "known_key_hash32",    .type = EventField::T::i64, .i = static_cast<int64_t>(_active->_id_bind[i].key_hash32) },
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
                && _active->_id_bind[i].confidence == static_cast<uint8_t>(IdBindConf::authoritative)) {
                const uint32_t existing_key = _active->_id_bind[i].key_hash32;
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
            _active->_id_bind[i].key_hash32 = key_hash32;                // authoritative -> overwrite the hash (incoming wins / same-node rekey)
        }
        _active->_id_bind[i].last_seen_ms = now;                         // refresh (silent — not new)
        _active->_id_bind[i].source       = static_cast<uint8_t>(source);
        _active->_id_bind[i].confidence   = static_cast<uint8_t>(confidence);
        id_bind_evict_other_hash_holders(key_hash32, node_id);  // one hash -> one id (heal a same-hash rehome)
        return true;
    }
    // NEW node_id: heal any stale holder of this hash FIRST (a pure rehome frees its slot), then cap-check.
    id_bind_evict_other_hash_holders(key_hash32, node_id);
    if (_active->_id_bind_n >= _cfg.cap_id_bind) {                        // table full -> refuse (Lua dv:4707)
        MR_TELEMETRY(
            EventField f[] = { { .key = "table",  .type = EventField::T::str, .s = "id_bind" },
                               { .key = "cap",    .type = EventField::T::i64, .i = _cfg.cap_id_bind },
                               { .key = "size",   .type = EventField::T::i64, .i = _active->_id_bind_n },
                               { .key = "action", .type = EventField::T::str, .s = "refuse" },
                               { .key = "node",   .type = EventField::T::i64, .i = node_id } };
            _hal.emit("table_cap_hit", f, 5); );
        return false;
    }
    _active->_id_bind[_active->_id_bind_n++] = { key_hash32, now, node_id, static_cast<uint8_t>(source), static_cast<uint8_t>(confidence) };
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
    for (uint16_t i = 0; i < _active->_id_bind_n; ++i) {
        if (_active->_id_bind[i].key_hash32 != key_hash32) continue;
        const bool self_keep = (_active->_id_bind[i].node_id == _node_id && _active->_id_bind[i].key_hash32 == _key_hash32);
        if (!self_keep && _cfg.id_bind_ttl_ms > 0
            && (now - _active->_id_bind[i].last_seen_ms) >= _cfg.id_bind_ttl_ms) continue;   // expired -> skip
        if (conf_out) *conf_out = static_cast<IdBindConf>(_active->_id_bind[i].confidence);  // soft/hard for the H resolver
        return _active->_id_bind[i].node_id;
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
    for (uint16_t i = 0; i < _active->_id_bind_n; ++i) {
        if (_active->_id_bind[i].node_id != id) continue;
        if (_active->_id_bind[i].confidence != static_cast<uint8_t>(IdBindConf::authoritative)) continue;  // confident only
        const bool self_keep = (id == _node_id && _active->_id_bind[i].key_hash32 == _key_hash32);
        if (!self_keep && _cfg.id_bind_ttl_ms > 0
            && (now - _active->_id_bind[i].last_seen_ms) >= _cfg.id_bind_ttl_ms) continue;     // expired
        out = _active->_id_bind[i].key_hash32;
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
    for (uint16_t r = 0; r < _active->_id_bind_n; ++r) {
        const IdBind e = _active->_id_bind[r];
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
        _active->_id_bind[w++] = e;
    }
    _active->_id_bind_n = w;
}

// ---- §mobile 3c: sender-side mobile_hash -> home_id cache -------------------------------------------------------
// id_bind is one-hash-per-node_id and a home already owns its own AUTHORITATIVE hash, so a mobile's stable hash ->
// its home_node can't live in id_bind. This small TTL'd cache holds it. No bijection (many mobiles -> one home).
// SILENT: emits NO telemetry, so even a stray write can't change byte output (s18 byte-identical).
int Node::mobile_home_find(uint32_t mobile_hash, uint8_t* home_layer_out) const {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_mobile_home_cache_n; ++i) {
        const auto& e = _active->_mobile_home_cache[i];
        if (e.mobile_hash != mobile_hash) continue;
        if ((now - e.last_seen_ms) >= protocol::mobile_home_cache_ttl_ms) return -1;   // expired -> miss
        if (home_layer_out) *home_layer_out = e.home_layer;                            // §5b: the home's layer (for cross-layer routing)
        return static_cast<int>(e.home_id);
    }
    return -1;
}

int Node::mobile_home_on_leaf(uint8_t leaf, uint32_t mobile_hash) const {   // §5b: the cross-layer bridge resolves M on a NON-active target leaf
    if (leaf >= _n_layers) return -1;
    const LayerRuntime& L = _layers[leaf];
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < L._mobile_home_cache_n; ++i) {
        const auto& e = L._mobile_home_cache[i];
        if (e.mobile_hash != mobile_hash) continue;
        if ((now - e.last_seen_ms) >= protocol::mobile_home_cache_ttl_ms) return -1;
        return static_cast<int>(e.home_id);
    }
    return -1;
}

void Node::mobile_home_set(uint32_t mobile_hash, uint8_t home_id, uint8_t epoch, uint8_t home_layer) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_mobile_home_cache_n; ++i)
        if (_active->_mobile_home_cache[i].mobile_hash == mobile_hash) {          // existing entry
            // §mobile 4a: freshest-proxy wins. SAME home -> refresh (keep the newer epoch). A FRESHER epoch for a
            // DIFFERENT home -> the mobile re-homed, adopt it. A STALE (older-epoch) answer for a different home -> IGNORE
            // (the old home's overlap answer must not overwrite the new home). Wrap-aware compare (int8_t(a-b)>0).
            const bool fresher = static_cast<int8_t>(epoch - _active->_mobile_home_cache[i].epoch) > 0;
            if (home_id == _active->_mobile_home_cache[i].home_id) {
                if (fresher) { _active->_mobile_home_cache[i].epoch = epoch; _active->_mobile_home_cache[i].home_layer = home_layer; }
                _active->_mobile_home_cache[i].last_seen_ms = now;
            } else if (fresher) {
                _active->_mobile_home_cache[i].home_id = home_id;
                _active->_mobile_home_cache[i].epoch = epoch;
                _active->_mobile_home_cache[i].home_layer = home_layer;
                _active->_mobile_home_cache[i].last_seen_ms = now;
            }
            return;
        }
    uint8_t slot;
    if (_active->_mobile_home_cache_n < protocol::cap_mobile_home_cache) {
        slot = _active->_mobile_home_cache_n++;
    } else {                                                                       // full -> evict the OLDEST
        slot = 0;
        for (uint8_t i = 1; i < _active->_mobile_home_cache_n; ++i)
            if (_active->_mobile_home_cache[i].last_seen_ms < _active->_mobile_home_cache[slot].last_seen_ms) slot = i;
    }
    _active->_mobile_home_cache[slot] = { mobile_hash, now, home_id, epoch, home_layer };
}

void Node::mobile_home_age_out() {
    const uint64_t now = _hal.now();
    uint8_t w = 0;
    for (uint8_t r = 0; r < _active->_mobile_home_cache_n; ++r) {
        const auto e = _active->_mobile_home_cache[r];
        if ((now - e.last_seen_ms) >= protocol::mobile_home_cache_ttl_ms) continue;   // drop expired
        _active->_mobile_home_cache[w++] = e;
    }
    _active->_mobile_home_cache_n = w;
}

// ---- E2E peer-pubkey cache (Phase 1 §6): key_hash32 -> ed_pub, hash-verified + authoritative-never-downgraded ----
bool Node::peer_key_set(uint32_t key_hash32, const uint8_t ed_pub[32], PeerKeyConf conf) {
    // Hash-verifiable: key_hash32 == LE(ed_pub[0..3]) (== identity.h key_hash32_of). A forged binding is REFUSED.
    const uint32_t derived = static_cast<uint32_t>(ed_pub[0]) | (static_cast<uint32_t>(ed_pub[1]) << 8) |
                             (static_cast<uint32_t>(ed_pub[2]) << 16) | (static_cast<uint32_t>(ed_pub[3]) << 24);
    if (derived != key_hash32) return false;
    const uint64_t now = _hal.now();
    auto& L = *_active;
    for (uint16_t i = 0; i < L._peer_keys_n; ++i) {                 // already cached -> refresh; upgrade, never downgrade
        if (L._peer_keys[i].key_hash32 == key_hash32) {
            const bool existing_pinned = (L._peer_keys[i].confidence == static_cast<uint8_t>(PeerKeyConf::pinned));
            if (existing_pinned && conf != PeerKeyConf::pinned) return true;   // §1: PINNED is IMMUTABLE to an on-air set (no-op, no refresh)
            L._peer_keys[i].last_seen_ms = now;
            if (conf == PeerKeyConf::pinned || static_cast<uint8_t>(conf) > L._peer_keys[i].confidence) {  // upgrade, or a user re-pin
                for (int b = 0; b < 32; ++b) L._peer_keys[i].ed_pub[b] = ed_pub[b];
                L._peer_keys[i].confidence = static_cast<uint8_t>(conf);
            }
            return true;
        }
    }
    uint16_t slot;
    if (L._peer_keys_n < protocol::cap_peer_keys) { slot = L._peer_keys_n++; }
    else {                                                          // cache full -> evict the least-recently-seen NON-PINNED
        // §1: a PINNED entry (QR-scanned, NV-backed) is NEVER evicted. Evict the oldest NON-pinned; if EVERY slot is
        // pinned, REFUSE the insert (fail loud: peer_key_full) rather than drop a scanned key.
        // [R5: eviction is otherwise pure LRU with no authoritative floor — a TYPE-5 cache-on-pass flood can churn the
        // non-pinned entries (a sustained-flood availability DoS within the documented TOFU/not-MITM model: the next
        // seal fails the authoritative gate -> the DM is REFUSED, never cleartext, and self-heals on a re-request). A
        // recency/usage floor protecting hot keys is a FUTURE decision — it trades against the cache-on-pass feature.]
        int victim = -1;
        for (uint16_t i = 0; i < L._peer_keys_n; ++i) {
            if (L._peer_keys[i].confidence == static_cast<uint8_t>(PeerKeyConf::pinned)) continue;   // never evict a pinned key
            if (victim < 0 || L._peer_keys[i].last_seen_ms < L._peer_keys[static_cast<uint16_t>(victim)].last_seen_ms) victim = i;
        }
        if (victim < 0) { MR_EMIT("peer_key_full", EF_I("hash", static_cast<int64_t>(key_hash32))); return false; }   // all pinned -> refuse
        slot = static_cast<uint16_t>(victim);
    }
    L._peer_keys[slot].key_hash32 = key_hash32;
    for (int b = 0; b < 32; ++b) L._peer_keys[slot].ed_pub[b] = ed_pub[b];
    L._peer_keys[slot].confidence = static_cast<uint8_t>(conf);
    L._peer_keys[slot].last_seen_ms = now;
    return true;
}

bool Node::peer_key_find(uint32_t key_hash32, uint8_t ed_pub_out[32], PeerKeyConf* conf_out) {
    const uint64_t now = _hal.now();
    auto& L = *_active;
    for (uint16_t i = 0; i < L._peer_keys_n; ++i) {
        if (L._peer_keys[i].key_hash32 == key_hash32) {
            const bool pinned = (L._peer_keys[i].confidence == static_cast<uint8_t>(PeerKeyConf::pinned));
            if (!pinned && protocol::peer_key_ttl_ms != 0 && (now - L._peer_keys[i].last_seen_ms) >= protocol::peer_key_ttl_ms)
                return false;                                       // aged (a PINNED key never ages)
            for (int b = 0; b < 32; ++b) ed_pub_out[b] = L._peer_keys[i].ed_pub[b];
            if (conf_out) *conf_out = static_cast<PeerKeyConf>(L._peer_keys[i].confidence);
            return true;
        }
    }
    return false;
}

void Node::peer_key_age_out() {
    if (protocol::peer_key_ttl_ms == 0) return;
    const uint64_t now = _hal.now();
    auto& L = *_active;
    uint16_t w = 0;
    for (uint16_t r = 0; r < L._peer_keys_n; ++r) {
        const bool pinned = (L._peer_keys[r].confidence == static_cast<uint8_t>(PeerKeyConf::pinned));
        if (!pinned && (now - L._peer_keys[r].last_seen_ms) >= protocol::peer_key_ttl_ms) continue;   // drop expired (PINNED never ages)
        L._peer_keys[w++] = L._peer_keys[r];
    }
    L._peer_keys_n = w;
}

// ---- E2E seal/open (§4/§5 + §1c sealed origin): CRYPTED inner = [dst_hash 4][ct][tag 16]; pt = [origin 1][source_hash?][loc?][body] ----
size_t Node::e2e_seal_inner(uint8_t* inner, size_t cap, uint8_t seed8[8], uint8_t flags, uint32_t dst_key_hash32,
                            uint8_t origin, uint16_t ctr, uint32_t source_hash, int32_t lat_e7, int32_t lon_e7,
                            const uint8_t* body, uint8_t body_len, SealOutcome& outcome) {
    outcome = SealOutcome::cross_layer;
    if (flags & DATA_FLAG_CROSS_LAYER) return 0;                    // v1: same-layer CRYPTED only
    outcome = SealOutcome::no_identity;                            // R3: NEVER seal under a zero key — fail loud if no identity
    if (!_crypto_ready) return 0;                                  // (set_crypto_identity not called -> _x_secret is zeros)
    outcome = SealOutcome::no_pubkey;
    uint8_t peer_ed[32]; PeerKeyConf conf = PeerKeyConf::overheard; // 1. recipient's AUTHORITATIVE pubkey (else fail loud)
    if (!peer_key_find(dst_key_hash32, peer_ed, &conf) || static_cast<uint8_t>(conf) < static_cast<uint8_t>(PeerKeyConf::authoritative)) return 0;  // authoritative OR pinned
    uint8_t peer_x[32]; ed_pub_to_x25519(peer_x, peer_ed);          // 2. ECDH -> per-pair key
    uint8_t shared[32]; crypto_x25519(shared, _x_secret, peer_x);
    // L10 (2026-07-04, crypto): a peer advertising a LOW-ORDER X25519 point drives the ECDH shared secret to
    // ALL-ZERO -> dm_kdf derives a key ANY observer can reproduce -> a "sealed" DM is decryptable by everyone
    // while we believe it confidential. REFUSE loudly (fail like a missing pubkey: return 0, no cleartext send)
    // rather than seal under a degenerate secret. Constant-time OR-accumulate over all 32 bytes (no early return
    // -> no timing leak on WHERE the first non-zero byte is). Catches the zero result whether or not monocypher's
    // crypto_x25519 already zeroed on the low-order point (it does for some, not all, contributory-check off).
    uint8_t sh_acc = 0; for (int i = 0; i < 32; ++i) sh_acc |= shared[i];
    if (sh_acc == 0) { crypto_wipe(shared, 32); outcome = SealOutcome::no_pubkey; return 0; }   // degenerate ECDH -> refuse (no seal, never cleartext)
    uint8_t key[32]; dm_kdf(key, shared, _key_hash32, dst_key_hash32);
    _hal.rand_bytes(seed8, 8);                                      // 3. fresh nonce-seed (HAL crypto RNG) -> nonce
    // R7: a broken crypto RNG returning an all-zero seed collapses nonce uniqueness to the 16-bit ctr -> keystream
    // reuse under the static per-pair key (catastrophic). Refuse loudly rather than seal with a degenerate nonce.
    bool seed_zero = true; for (int i = 0; i < 8; ++i) if (seed8[i]) { seed_zero = false; break; }
    if (seed_zero) { crypto_wipe(key, 32); crypto_wipe(shared, 32); outcome = SealOutcome::bad_rng; return 0; }
    uint8_t nonce[24]; dm_nonce(nonce, seed8, ctr, dst_key_hash32);
    uint8_t aad[4] = { uint8_t(dst_key_hash32), uint8_t(dst_key_hash32 >> 8),
                       uint8_t(dst_key_hash32 >> 16), uint8_t(dst_key_hash32 >> 24) };   // 4. cleartext AAD = [dst_hash 4 LE] (§1c: origin SEALED)
    uint8_t pt[protocol::max_payload_bytes_hard_cap]; size_t pt_len = 0;   // 5. plaintext = [origin 1][source_hash?][location?][body]
    // R2/R6: any size overflow below -> too_large, and ALWAYS wipe key/shared/pt first (single wipe_fail exit).
    outcome = SealOutcome::too_large;
    auto wipe_fail = [&]() -> size_t { crypto_wipe(key, 32); crypto_wipe(shared, 32); crypto_wipe(pt, sizeof pt); return 0; };
    pt[pt_len++] = origin;                                          // §1c: origin is the FIRST sealed byte (privacy: relays can't read it)
    if (flags & DATA_FLAG_SOURCE_HASH) { pt[pt_len++]=uint8_t(source_hash); pt[pt_len++]=uint8_t(source_hash>>8);
                                         pt[pt_len++]=uint8_t(source_hash>>16); pt[pt_len++]=uint8_t(source_hash>>24); }
    if (flags & DATA_FLAG_LOCATION) { if (pt_len + 6 > sizeof pt) return wipe_fail(); pack_loc6(lat_e7, lon_e7, std::span<uint8_t>(pt + pt_len, 6)); pt_len += 6; }
    for (uint8_t i = 0; i < body_len; ++i) { if (pt_len >= sizeof pt) return wipe_fail(); pt[pt_len++] = body[i]; }
    const size_t total = sizeof aad + pt_len + DM_TAG_LEN;         // 6. inner = aad || ciphertext || tag
    if (total > cap) return wipe_fail();
    for (size_t i = 0; i < sizeof aad; ++i) inner[i] = aad[i];
    uint8_t tag[DM_TAG_LEN];
    dm_seal(inner + sizeof aad, tag, key, nonce, aad, sizeof aad, pt, pt_len);
    for (size_t i = 0; i < DM_TAG_LEN; ++i) inner[sizeof aad + pt_len + i] = tag[i];
    crypto_wipe(key, 32); crypto_wipe(shared, 32); crypto_wipe(pt, sizeof pt);
    outcome = SealOutcome::ok;
    return total;
}

bool Node::e2e_open_inner(const uint8_t* inner, size_t inner_len, const uint8_t seed8[8], uint8_t flags, uint16_t ctr,
                          uint32_t sender_hash, uint32_t& origin_out, uint32_t& source_hash_out, bool& has_location_out,
                          int32_t& lat_out, int32_t& lon_out, uint8_t* body_out, uint8_t& body_len_out) {
    origin_out = 0; source_hash_out = 0; has_location_out = false; lat_out = 0; lon_out = 0; body_len_out = 0;
    if (flags & DATA_FLAG_CROSS_LAYER) return false;                // v1: same-layer only
    // §1c: origin is SEALED (pt[0]), recovered AFTER dm_open below — NOT read from the cleartext inner.
    // 1. SENDER pubkey for this candidate hash (the trial passes each cached key; a wrong key tag-fails below)
    uint8_t sender_ed[32]; PeerKeyConf conf = PeerKeyConf::overheard;
    if (!peer_key_find(sender_hash, sender_ed, &conf) || static_cast<uint8_t>(conf) < static_cast<uint8_t>(PeerKeyConf::authoritative)) return false;  // authoritative OR pinned
    uint8_t sx[32]; ed_pub_to_x25519(sx, sender_ed);               // 2. ECDH -> key (same KDF both directions)
    uint8_t shared[32]; crypto_x25519(shared, _x_secret, sx);
    // L10 (2026-07-04, crypto): mirror the seal-side low-order/all-zero ECDH reject. A candidate sender key that
    // is a low-order point yields an all-zero shared secret -> a key any observer can derive; NEVER open under it
    // (a forger could otherwise craft a frame that "opens" against the degenerate key). Constant-time OR-accumulate
    // over all 32 bytes (no early return -> no timing leak). Fails like a tag mismatch: wipe + return false.
    uint8_t sh_acc = 0; for (int i = 0; i < 32; ++i) sh_acc |= shared[i];
    if (sh_acc == 0) { crypto_wipe(shared, 32); return false; }     // degenerate ECDH -> hard drop
    uint8_t key[32]; dm_kdf(key, shared, _key_hash32, sender_hash);
    uint8_t nonce[24]; dm_nonce(nonce, seed8, ctr, _key_hash32);   // 3. we are dst -> dst_key_hash32 == our key
    const size_t aad_len = 4;                                       // 4. [dst_hash 4] (§1c: origin SEALED in pt[0])
    if (inner_len < aad_len + DM_TAG_LEN) { crypto_wipe(key, 32); crypto_wipe(shared, 32); return false; }
    const size_t ct_len = inner_len - aad_len - DM_TAG_LEN;
    uint8_t pt[protocol::max_payload_bytes_hard_cap];
    if (ct_len > sizeof pt) { crypto_wipe(key, 32); crypto_wipe(shared, 32); return false; }
    const bool ok = dm_open(pt, key, nonce, inner, aad_len, inner + aad_len, ct_len, inner + aad_len + ct_len);
    crypto_wipe(key, 32); crypto_wipe(shared, 32);
    if (!ok) { crypto_wipe(pt, sizeof pt); return false; }          // tag fail -> hard drop
    size_t off = 0;                                                 // 5. parse [origin 1][source_hash?][location?][body]
    if (ct_len < off + 1) { crypto_wipe(pt, sizeof pt); return false; }   // §1c: origin is the FIRST sealed byte
    origin_out = pt[off]; off += 1;
    if (flags & DATA_FLAG_SOURCE_HASH) {
        if (ct_len < off + 4) { crypto_wipe(pt, sizeof pt); return false; }
        source_hash_out = uint32_t(pt[off]) | (uint32_t(pt[off+1])<<8) | (uint32_t(pt[off+2])<<16) | (uint32_t(pt[off+3])<<24); off += 4;
    }
    if (flags & DATA_FLAG_LOCATION) {
        if (ct_len < off + 6) { crypto_wipe(pt, sizeof pt); return false; }
        unpack_loc6(std::span<const uint8_t>(pt + off, 6), lat_out, lon_out); has_location_out = true; off += 6;
    }
    body_len_out = uint8_t(ct_len - off);
    for (size_t i = 0; i < body_len_out; ++i) body_out[i] = pt[off + i];
    // 6. anti-spoof: the SEALED source_hash must equal the resolved sender's hash (only the real sender's key opens to it)
    if ((flags & DATA_FLAG_SOURCE_HASH) && source_hash_out != sender_hash) { crypto_wipe(pt, sizeof pt); body_len_out = 0; return false; }
    crypto_wipe(pt, sizeof pt);
    return true;
}

// §1a sealed-sender: trial decryption. Try each AUTHORITATIVE/PINNED cached peer key until the AEAD tag verifies for
// one (false-accept 2^-128) -> that key's owner IS the sender. No cached key opens it -> false (caller drops silently).
// (Perf: recomputes ECDH per candidate; caching the per-pair AEAD key in PeerKey is the documented optimisation.)
bool Node::e2e_open_trial(const uint8_t* inner, size_t inner_len, const uint8_t seed8[8], uint8_t flags, uint16_t ctr,
                          uint32_t& sender_hash_out, uint32_t& origin_out, uint32_t& source_hash_out,
                          bool& has_location_out, int32_t& lat_out, int32_t& lon_out, uint8_t* body_out, uint8_t& body_len_out) {
    sender_hash_out = 0; origin_out = 0; source_hash_out = 0; has_location_out = false; lat_out = 0; lon_out = 0; body_len_out = 0;
    if (!_crypto_ready) return false;
    auto& L = *_active;
    for (uint16_t i = 0; i < L._peer_keys_n; ++i) {
        if (static_cast<uint8_t>(L._peer_keys[i].confidence) < static_cast<uint8_t>(PeerKeyConf::authoritative)) continue;  // authoritative/pinned only
        if (e2e_open_inner(inner, inner_len, seed8, flags, ctr, L._peer_keys[i].key_hash32, origin_out,
                           source_hash_out, has_location_out, lat_out, lon_out, body_out, body_len_out)) {
            sender_hash_out = L._peer_keys[i].key_hash32;          // the opening key's owner is the authenticated sender
            return true;
        }
    }
    return false;
}

// =============================================================================
// Phase A — the H flood + resolve handler. The defining behaviour: ANY node that
// already holds the binding answers + STOPS the flood; the flood is the fallback.
// =============================================================================

// per-(origin, key_hash32, VARIANT) flood dedup (Lua hash_query_seen; hash_query_seen_ttl_ms window). Keying on
// `hard` is load-bearing: a HARD query (verify-on-use) must NOT be suppressed by a prior SOFT's seen-entry, or
// the escalation that reaches the owner is silently swallowed. Mirrors rreq_seen.
bool Node::hash_query_seen_recently(uint8_t origin, uint32_t key_hash32, bool hard, bool want_pubkey) {
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::hash_query_seen_ttl_ms) ? now - protocol::hash_query_seen_ttl_ms : 0;
    for (uint8_t i = 0; i < _active->_hash_query_seen_n; ++i)
        if (_active->_hash_query_seen[i].origin == origin && _active->_hash_query_seen[i].key_hash32 == key_hash32
            && _active->_hash_query_seen[i].hard == hard && _active->_hash_query_seen[i].want_pubkey == want_pubkey
            && _active->_hash_query_seen[i].t_ms >= cutoff) return true;
    return false;
}
void Node::mark_hash_query_seen(uint8_t origin, uint32_t key_hash32, bool hard, bool want_pubkey) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_hash_query_seen_n; ++i)
        if (_active->_hash_query_seen[i].origin == origin && _active->_hash_query_seen[i].key_hash32 == key_hash32
            && _active->_hash_query_seen[i].hard == hard && _active->_hash_query_seen[i].want_pubkey == want_pubkey)
            { _active->_hash_query_seen[i].t_ms = now; return; }
    if (_active->_hash_query_seen_n < protocol::cap_hash_query_seen) {
        _active->_hash_query_seen[_active->_hash_query_seen_n++] = { origin, key_hash32, now, hard, want_pubkey };
    } else {                                              // ring full -> evict the oldest
        uint8_t o = 0;
        for (uint8_t i = 1; i < _active->_hash_query_seen_n; ++i) if (_active->_hash_query_seen[i].t_ms < _active->_hash_query_seen[o].t_ms) o = i;
        _active->_hash_query_seen[o] = { origin, key_hash32, now, hard, want_pubkey };
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
    int node_id = -1; bool authoritative = false; bool mobile_proxy = false; uint8_t mobile_epoch = 0; uint8_t mobile_layer = 0;   // §mobile 4a proxy flag + epoch; §5b the home's layer
    const bool same_team = h.team_scoped && _cfg.team_id != 0 && h.team_id == _cfg.team_id;   // §mobile-team: a teammate's locate (the mobile IS the endpoint on the team plane)
    // §mobile: a REGISTERED mobile is INVISIBLE to the static plane — it SKIPS own-hash resolution (the home proxies) so its
    // LOCAL id never leaks. It DOES answer a same-team locate (the 6.2 team-scoped table routes to its local id). A static
    // node (is_mobile=false) is unchanged. This also suppresses its want_pubkey owner-answer -> the home answers it (Part 2).
    if (h.key_hash32 == _key_hash32 && (!(_cfg.is_mobile && _my_mobile_reg.active) || same_team)) { node_id = _node_id; authoritative = true; }   // own-hash: resolves either variant
    else if (!h.hard) {                                                              // HARD skips the cache -> flood to the owner
        IdBindConf conf = IdBindConf::claimed;
        const int found = id_bind_find_by_hash(h.key_hash32, &conf);
        if (found >= 0) { node_id = found; authoritative = (conf == IdBindConf::authoritative); }
    }
    // §mobile 3a: HOST proxy — I HOST this mobile, so answer with MY id (home_id) as a CLAIMED binding; the querier caches
    // mobile_hash -> home_id and routes the DM to me (the host), which then last-mile-forwards it (do_post_ack). The home is
    // the mobile's LOCATION AUTHORITY, soft AND hard (was `!h.hard`, which let a HARD locate — e2e_ack_req drives it — bypass
    // the home + flood to the mobile owner). Redirect forwards unconditionally; the DIRECT proxy is LIVENESS-gated so a
    // long-dead mobile's entry stops black-holing. Gated on _mobile_reg_n>0 -> a non-host is byte-identical (no wire change).
    if (node_id < 0 && _active->_mobile_reg_n > 0) {
        for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
            if (_active->_mobile_reg[i].key_hash32 == h.key_hash32) {   // §mobile 4a: a MOBILE_H_ANSWER carrying the registration epoch (freshest-proxy wins)
                if (_active->_mobile_reg[i].redirect_home_id != 0) {    // §mobile 4b/5b: we're STALE -> redirect to the mobile's NEW home + ITS layer (NOT liveness-gated)
                    if (h.want_pubkey) break;                          // §Part 2: a STALE home holds NO key for M -> do NOT answer/suppress a WANT_PUBKEY locate (that black-holes the encrypted DM when we're a flood cut-vertex). Leave node_id=-1 -> FORWARD the flood on to the NEW home, which cached M's key (Fix 6) and answers with the pubkey. The plain (location) redirect below is unaffected.
                    node_id = _active->_mobile_reg[i].redirect_home_id;
                    mobile_epoch = _active->_mobile_reg[i].redirect_epoch;
                    mobile_layer = _active->_mobile_reg[i].redirect_home_layer;
                    authoritative = false; mobile_proxy = true;
                } else if (_hal.now() - _active->_mobile_reg[i].last_heard_ms < protocol::mobile_liveness_ms) {   // §mobile: I'm the home -> proxy ONLY if the mobile is recently alive
                    node_id = _node_id;
                    mobile_epoch = static_cast<uint8_t>(_active->_mobile_reg[i].epoch);
                    mobile_layer = active_layer_id();
                    authoritative = false; mobile_proxy = true;
                }
                break;   // matched (live/stale/redirect) — STALE leaves node_id=-1 -> forward -> the locate times out = "unreachable" (NOT a black hole)
            }
    }

    if (node_id >= 0) {                                    // RESOLVER path (dv:11644) — answer + SUPPRESS the forward
        mark_hash_query_seen(h.origin, h.key_hash32, h.hard, h.want_pubkey);   // mark BEFORE replying so a re-flood doesn't double-answer (dv:11647)
        MR_TELEMETRY(
            EventField f[] = { { .key = "origin",        .type = EventField::T::i64,     .i = h.origin },
                               { .key = "key_hash32",    .type = EventField::T::i64,     .i = static_cast<int64_t>(h.key_hash32) },
                               { .key = "node",          .type = EventField::T::i64,     .i = node_id },
                               { .key = "target_layer",  .type = EventField::T::i64,     .i = _cfg.leaf_id },
                               { .key = "authoritative", .type = EventField::T::boolean, .b = authoritative } };
            _hal.emit("h_resolved", f, 5); );              // dv:11649
        if (h.want_pubkey && mobile_proxy) {                        // §Part 2 Fix 7: the HOME answers WANT_PUBKEY on behalf of its LIVE mobile (Option 1 — the home carries the key). MUST precede the owner branch: a live proxy has node_id==_node_id, so the owner branch would otherwise leak the HOME's own key under the mobile's hash.
            const uint8_t* mk = host_mobile_ed_pub(h.key_hash32);  // the mobile's cached ed_pub (Fix 6 push), iff a LIVE direct proxy has_pubkey (a redirect carries no local key)
            if (mk) send_mobile_pubkey_answer(h.origin, mobile_layer, static_cast<uint8_t>(node_id), h.key_hash32, mobile_epoch, mk);
            // no cached key (the push hasn't arrived yet, or this is a redirect) -> stay SILENT on WANT_PUBKEY: the locate times out and the sender's reqpubkey retries (the push races registration). The flood is still suppressed by the return below.
        } else if (h.want_pubkey && node_id == _node_id && _crypto_ready) {   // §6 + review#1: ONLY the OWNER (own-hash) answers WANT_PUBKEY
            // §2 MUTUAL: cache the requester's key + id_bind (from the H's appended ed_pub) BEFORE answering, so we can
            // both DECRYPT and ADDRESS its future sealed DMs -> the exchange provisions BOTH directions in one round.
            // requester_hash = requester_ed_pub[:4] LE (self-consistent: peer_key_set derives/checks the same hash).
            const uint32_t requester_hash = uint32_t(h.requester_ed_pub[0]) | (uint32_t(h.requester_ed_pub[1]) << 8)
                                          | (uint32_t(h.requester_ed_pub[2]) << 16) | (uint32_t(h.requester_ed_pub[3]) << 24);
            bool req_zero = true; for (int i = 0; i < 32; ++i) if (h.requester_ed_pub[i]) { req_zero = false; break; }
            if (!req_zero && requester_hash != 0                       // review#15: never cache a zero/degenerate requester key
                && peer_key_set(requester_hash, h.requester_ed_pub, PeerKeyConf::authoritative)) {
                id_bind_set(h.origin, requester_hash, IdBindSource::h_query, IdBindConf::authoritative);   // review#3: the ADDRESSING half (seal-back w/o waiting for a beacon)
                MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(requester_hash)), EF_I("node", h.origin));   // review#11: schema aligned with §7
                Push pu{}; pu.kind = PushKind::peer_key_cached; pu.sender_hash = requester_hash; enqueue_push(pu);   // review#10: app-notify on device too
            }
            send_hash_bind_pubkey_response(h.origin, _cfg.leaf_id, static_cast<uint8_t>(node_id), _ed_pub);
        } else
            send_hash_bind_response(h.origin, mobile_proxy ? mobile_layer : _cfg.leaf_id, static_cast<uint8_t>(node_id), h.key_hash32, authoritative, mobile_proxy, mobile_epoch);   // §5b: a mobile answer carries the HOME's full layer_id (not the proxy's leaf)
        return;                                            // SUPPRESS — the whole point: the flood stops here
    }

    // FORWARD path (dv:11655): we don't know it (or it's a HARD query and we're not the owner) -> re-broadcast
    // once, deduped per variant, until TTL runs out.
    if (hash_query_seen_recently(h.origin, h.key_hash32, h.hard, h.want_pubkey)) return;   // flood dedup (dv:11656) — §2: WANT_PUBKEY is its own variant
    mark_hash_query_seen(h.origin, h.key_hash32, h.hard, h.want_pubkey);                   // (dv:11657)
    if (h.ttl == 0) return;                                         // TTL exhausted (dv:11658)
    // L7: h.ttl is an unauthenticated wire byte — a forged ttl=255 would re-flood with a 255-hop horizon. Clamp to
    // flood_hop_max so the re-flooded ttl can't exceed the mesh diameter (dedup already bounds re-broadcasts per node).
    const uint8_t fwd_ttl = (h.ttl > protocol::flood_hop_max ? protocol::flood_hop_max : h.ttl) - 1;
    MR_TELEMETRY(
        EventField f[] = { { .key = "origin",     .type = EventField::T::i64,     .i = h.origin },
                           { .key = "key_hash32", .type = EventField::T::i64,     .i = static_cast<int64_t>(h.key_hash32) },
                           { .key = "ttl",        .type = EventField::T::i64,     .i = static_cast<int64_t>(fwd_ttl) },
                           { .key = "hard",       .type = EventField::T::boolean, .b = h.hard } };
        _hal.emit("h_forward", f, 4); );                   // dv:11661
    h_in fwd{};
    fwd.leaf_id = _cfg.leaf_id; fwd.origin = h.origin; fwd.key_hash32 = h.key_hash32;
    fwd.ttl = fwd_ttl; fwd.hard = h.hard;                          // preserve the variant across forwards
    fwd.want_pubkey = h.want_pubkey;   // R4: PRESERVE the E2E pubkey-request flag so a multi-hop WANT_PUBKEY reaches the owner
    if (h.want_pubkey) for (int i = 0; i < 32; ++i) fwd.requester_ed_pub[i] = h.requester_ed_pub[i];   // §2: carry the requester's pubkey across the forward
    fwd.team_scoped = h.team_scoped; fwd.team_id = h.team_id;   // §mobile-team Fix 1b: PRESERVE team scope across a multi-hop forward, else a same_team mobile >1 hop away sees a plain query + stays silent (Fix 1). Inert today (no originator sets team_scoped -> byte-identical) until 6.2 turns it on.
    uint8_t buf[8 + 32 + 4];           // §2: WANT_PUBKEY H is 40 B; §mobile-team: +4 B for team_id (a team_scoped WANT_PUBKEY is 44 B)
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
                                   uint32_t key_hash32, bool authoritative, bool mobile_proxy, uint8_t epoch) {
    if (_active->_tx_queue_n >= kTxQueueCap) return;                       // queue full -> drop (the querier can re-flood)
    hash_bind_inner hb{};
    hb.target_layer = target_layer; hb.node_id = node_id; hb.key_hash32 = key_hash32;   // authoritative rides the frame TYPE, not the inner
    hb.epoch = epoch;                                           // §mobile 4a: packed only for the mobile variant (7 B)
    uint8_t inner[7];                                          // 7 for the mobile variant (+epoch); the normal answer packs 6 -> byte-identical
    const size_t n = pack_hash_bind_inner(hb, std::span<uint8_t>(inner, sizeof(inner)), mobile_proxy);
    if (n == 0) return;
    TxItem item{};
    item.origin = _node_id; item.dst = to_origin;
    item.ctr = next_ctr(to_origin); item.ctr_lo = static_cast<uint8_t>(item.ctr & 0x0F);
    item.flags = 0;                                              // byte-1 flags clear; the H_ANSWER TYPE byte (below) types it
    item.type  = mobile_proxy ? DATA_TYPE_MOBILE_H_ANSWER
               : authoritative ? DATA_TYPE_AUTHORITATIVE_H_ANSWER : DATA_TYPE_H_ANSWER;
    for (size_t i = 0; i < n; ++i) item.inner[i] = inner[i];
    item.inner_len = static_cast<uint8_t>(n);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_TELEMETRY(
        EventField f[] = { { .key = "to",            .type = EventField::T::i64,     .i = to_origin },
                           { .key = "node",          .type = EventField::T::i64,     .i = node_id },
                           { .key = "key_hash32",    .type = EventField::T::i64,     .i = static_cast<int64_t>(key_hash32) },
                           { .key = "authoritative", .type = EventField::T::boolean, .b = authoritative } };
        _hal.emit("hash_bind_response_enqueued", f, 4); );        // dv:5897
    become_free();                                               // kick the queue to route the answer home
}

// E2E §6: the owner answers a WANT_PUBKEY query with its ed_pub — a routed DATA TYPE 5 (cleartext; cache-on-pass).
void Node::send_hash_bind_pubkey_response(uint8_t to_origin, uint8_t target_layer, uint8_t node_id, const uint8_t ed_pub[32]) {
    if (_active->_tx_queue_n >= kTxQueueCap) return;
    hash_bind_pubkey_inner hb{}; hb.target_layer = target_layer; hb.node_id = node_id;
    for (int i = 0; i < 32; ++i) hb.ed_pub[i] = ed_pub[i];
    uint8_t inner[34];
    const size_t n = pack_hash_bind_pubkey_inner(hb, std::span<uint8_t>(inner, sizeof inner));
    if (n == 0) return;
    TxItem item{};
    item.origin = _node_id; item.dst = to_origin;
    item.ctr = next_ctr(to_origin); item.ctr_lo = static_cast<uint8_t>(item.ctr & 0x0F);
    item.flags = 0; item.type = DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY;   // TYPE 5 (owner-only; the query rode HARD)
    for (size_t i = 0; i < n; ++i) item.inner[i] = inner[i];
    item.inner_len = static_cast<uint8_t>(n);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_EMIT("hash_bind_pubkey_response_enqueued", EF_I("to", to_origin), EF_I("node", node_id));
    become_free();
}

// E2E §6: a DATA TYPE 5 (delivered to us OR relayed-through) -> cache the owner's ed_pub AUTHORITATIVE. The pubkey is
// immutable + hash-verifiable, so cache-on-pass can't decay it (peer_key_set re-verifies ed_pub[:4] == key_hash32).
void Node::on_hash_bind_pubkey(const uint8_t* inner, uint8_t inner_len) {
    auto o = parse_hash_bind_pubkey_inner(std::span<const uint8_t>(inner, inner_len));
    if (!o) return;
    const uint32_t kh = static_cast<uint32_t>(o->ed_pub[0]) | (static_cast<uint32_t>(o->ed_pub[1]) << 8)
                      | (static_cast<uint32_t>(o->ed_pub[2]) << 16) | (static_cast<uint32_t>(o->ed_pub[3]) << 24);
    if (peer_key_set(kh, o->ed_pub, PeerKeyConf::authoritative)) {
        MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(kh)), EF_I("node", o->node_id));
        Push pu{}; pu.kind = PushKind::peer_key_cached; pu.sender_hash = kh; enqueue_push(pu);   // §7: app prompts "secure send ready — resend"
    }
    // [#38b park/drain follow-up: on a fresh authoritative insert, drain parked CRYPTED sends to kh -> seal + send.]
}

// The querier received a DATA whose inner is a hash-bind answer (handle_data routed it here off the
// H_ANSWER payload-flag). Phase B: parse + emit. Phase C will id_bind_set(h_query, conf) + drain the
// parked send-by-hash. DELIBERATELY does NOT deliver as a DM (it is routing/identity info, not user content).
void Node::on_hash_bind_response(const uint8_t* inner, uint8_t inner_len, bool authoritative) {
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, inner_len));
    if (!hb) return;
    // C.1 destination consume: WE asked -> source h_query; the answer's AUTHORITATIVE bit (now the frame TYPE,
    // passed in) carries the confidence (an owner answer is authoritative, a cache-relayed soft answer is
    // claimed -> verify-on-use).
    id_bind_set(hb->node_id, hb->key_hash32, IdBindSource::h_query,
                authoritative ? IdBindConf::authoritative : IdBindConf::claimed);
    // §mobile 4a: the 3c key_hash_of_id heuristic is GONE — a mobile proxy now carries the distinct DATA_TYPE_MOBILE_H_ANSWER
    // (handled in on_mobile_hash_bind_response), so a plain H_ANSWER for a hash we don't own is NEVER treated as a mobile proxy.
    MR_TELEMETRY(
        EventField f[] = { { .key = "node",          .type = EventField::T::i64,     .i = hb->node_id },
                           { .key = "key_hash32",    .type = EventField::T::i64,     .i = static_cast<int64_t>(hb->key_hash32) },
                           { .key = "target_layer",  .type = EventField::T::i64,     .i = hb->target_layer },
                           { .key = "authoritative", .type = EventField::T::boolean, .b = authoritative } };
        _hal.emit("hash_bind_rx", f, 4); );
    drain_parked_sends(hb->key_hash32, hb->node_id, hb->target_layer);   // D: a parked send-by-hash can now fly; target_layer drives the cross-layer fork (4d)
    // Slice 4f: the binding for a DEFERRED cross-layer handoff just arrived on THIS leaf -> re-resolve + drain it now
    // (else it waits a full visit period). _active is the leaf the answer arrived on; the caller become_free()s next.
    drain_xl_handoffs_for_leaf(static_cast<uint8_t>(_active - &_layers[0]));
}

// §mobile 4a: a MOBILE_H_ANSWER (a host PROXYing for a hosted mobile) -> cache M->home + its registration epoch, and
// NOTHING else. Crucially NO id_bind_set: a mobile-proxy binding must stay OUT of id_bind, or a repeat send-by-hash
// would find the claimed M->home id_bind and hard-verify (which the soft-only proxy never answers -> send_hash_giveup).
// The cache (freshest-wins) is the sole store; drain lets a parked first send fly now (do_send override_dst_hash=M, 3c Fix5).
void Node::on_mobile_hash_bind_response(const uint8_t* inner, uint8_t inner_len) {
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, inner_len));
    if (!hb) return;
    mobile_home_set(hb->key_hash32, hb->node_id, hb->epoch, hb->target_layer);   // §5b: M -> home + the home's LAYER (freshest-proxy wins)
    MR_EMIT("mobile_home_cached", EF_I("key", static_cast<int64_t>(hb->key_hash32)), EF_I("home", hb->node_id), EF_I("epoch", hb->epoch));
    drain_parked_sends(hb->key_hash32, hb->node_id, hb->target_layer);    // a parked first send can now fly via the cache/override
}

// §mobile hash-locate Part 2 (Fix 7): the cached ed_pub for a hosted mobile M (hash), IFF we hold it (Fix 6 push) AND this is
// a LIVE DIRECT proxy (redirect_home_id==0 — a redirect points elsewhere and carries no local key). Returns nullptr otherwise.
const uint8_t* Node::host_mobile_ed_pub(uint32_t key_hash32) const {
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == key_hash32 && _active->_mobile_reg[i].has_pubkey
            && _active->_mobile_reg[i].redirect_home_id == 0)
            return _active->_mobile_reg[i].ed_pub;
    return nullptr;
}

// §mobile hash-locate Part 2 (Fix 7): a WANT_PUBKEY answer for a hosted mobile — inner = the mobile hash_bind (7 B: home
// routing + epoch) ‖ the mobile's ed_pub[32] (39 B), TYPE 13. Distinct from the owner's TYPE-5 answer: the sender must learn
// BOTH the mobile's key AND that it routes via the HOME (not to the local id). Mirrors send_hash_bind_response's TxItem shape.
void Node::send_mobile_pubkey_answer(uint8_t to_origin, uint8_t target_layer, uint8_t home_id,
                                     uint32_t key_hash32, uint8_t epoch, const uint8_t ed_pub[32]) {
    if (_active->_tx_queue_n >= kTxQueueCap) return;
    hash_bind_inner hb{}; hb.target_layer = target_layer; hb.node_id = home_id; hb.key_hash32 = key_hash32; hb.epoch = epoch;
    uint8_t inner[7 + 32];
    const size_t n = pack_hash_bind_inner(hb, std::span<uint8_t>(inner, 7), /*mobile=*/true);   // 7 B mobile variant (home routing + epoch)
    if (n == 0) return;
    for (int i = 0; i < 32; ++i) inner[n + i] = ed_pub[i];                                      // ‖ the mobile's ed_pub
    TxItem item{};
    item.origin = _node_id; item.dst = to_origin;
    item.ctr = next_ctr(to_origin); item.ctr_lo = static_cast<uint8_t>(item.ctr & 0x0F);
    item.flags = 0; item.type = DATA_TYPE_MOBILE_H_ANSWER_PUBKEY;
    const size_t total = n + 32;
    for (size_t i = 0; i < total; ++i) item.inner[i] = inner[i];
    item.inner_len = static_cast<uint8_t>(total);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_EMIT("mobile_pubkey_answer_tx", EF_I("to", to_origin), EF_I("home", home_id));
    become_free();
}

// §mobile hash-locate Part 2 (Fix 8): the querier received a home's MOBILE_H_ANSWER_PUBKEY -> cache the mobile's key
// (hash-verified, authoritative) AND route via the home (mobile_home_set). NEVER id_bind the local id. Combines the owner
// pubkey ingest (on_hash_bind_pubkey) with the mobile-home cache (on_mobile_hash_bind_response) — the sender can now seal
// to M and address the sealed DM to the home (do_send override_dst_hash=M seals under peer_key[M]).
void Node::on_mobile_hash_bind_pubkey_response(const uint8_t* inner, uint8_t inner_len) {
    if (inner_len < 7 + 32) return;
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, 7));   // the mobile 7 B: home routing + epoch (ignores the ed_pub tail)
    if (!hb) return;
    const uint8_t* ed = inner + 7;
    const uint32_t kh = uint32_t(ed[0]) | (uint32_t(ed[1]) << 8) | (uint32_t(ed[2]) << 16) | (uint32_t(ed[3]) << 24);
    if (kh == hb->key_hash32 && peer_key_set(kh, ed, PeerKeyConf::authoritative)) {   // the key MUST hash to M (self-consistent) — never id_bind the LOCAL id
        MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(kh)), EF_I("node", hb->node_id));
        Push pu{}; pu.kind = PushKind::peer_key_cached; pu.sender_hash = kh; enqueue_push(pu);   // §7: app prompts "secure send ready — resend"
    }
    mobile_home_set(hb->key_hash32, hb->node_id, hb->epoch, hb->target_layer);   // M -> home (+layer): the sealed DM routes via the home, not to the local id
    MR_EMIT("mobile_home_cached", EF_I("key", static_cast<int64_t>(hb->key_hash32)), EF_I("home", hb->node_id), EF_I("epoch", hb->epoch));
    drain_parked_sends(hb->key_hash32, hb->node_id, hb->target_layer);   // a parked CRYPTED send can now seal (peer_key[M]) + fly (via home)
}

// C.2 cache-on-pass (NEW, beyond the Lua's gateway-only caching): a RELAYED hash-bind answer is
// forwarder-readable (CLEARTEXT) — snoop the binding so every node on the return path becomes a future
// resolver and repeat H floods shrink (measured in the Phase D multi-node sim). source = h_relay (snooped,
// distinct from the asked h_query); confidence rides the answer's AUTHORITATIVE flag. We do NOT consume —
// do_post_ack still forwards the DATA. Deliberate, measurable divergence (gate: flood reach trends down).
void Node::on_hash_bind_snoop(const uint8_t* inner, uint8_t inner_len, bool authoritative) {
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, inner_len));
    if (!hb) return;
    id_bind_set(hb->node_id, hb->key_hash32, IdBindSource::h_relay,
                authoritative ? IdBindConf::authoritative : IdBindConf::claimed);
    MR_TELEMETRY(
        EventField f[] = { { .key = "node",          .type = EventField::T::i64,     .i = hb->node_id },
                           { .key = "key_hash32",    .type = EventField::T::i64,     .i = static_cast<int64_t>(hb->key_hash32) },
                           { .key = "authoritative", .type = EventField::T::boolean, .b = authoritative } };
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
uint16_t Node::send_by_hash(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt) {
    IdBindConf conf = IdBindConf::claimed;
    const int id = id_bind_find_by_hash(key_hash32, &conf);
    if (id >= 0 && conf == IdBindConf::authoritative)            // confident binding -> send NOW
        return do_send(static_cast<uint8_t>(id), body, body_len, flags, crypt);   // §8b: thread the per-message crypt intent
    uint8_t home_layer = 0;
    const int home = mobile_home_find(key_hash32, &home_layer);  // §mobile 3c/5b: a cached mobile -> its home_node (+layer)?
    if (home >= 0) {
        if (home_layer != 0 && home_layer != active_layer_id()) {   // §5b: the home is on ANOTHER layer -> reach it via a gateway (the bridge resolves M on the target leaf, Fix 3)
            send_cross_layer(static_cast<uint8_t>(home), key_hash32, home_layer, body, body_len, flags);
            return 0;
        }
        // same layer (4a path): send to the home carrying the MOBILE's hash (so home forwards, not consumes). NO hard-verify.
        return do_send(static_cast<uint8_t>(home), body, body_len, flags, crypt, /*override_dst_hash=*/key_hash32);
    }
    // SOFT cached binding -> HARD verify-on-use (reach the owner for a correction); UNKNOWN -> SOFT flood.
    park_send(key_hash32, body, body_len, flags, crypt);   // M3: carry the crypt intent so a parked sendhashx flies CRYPTED, not cleartext
    emit_hash_query(key_hash32, /*hard=*/(id >= 0));
    return 0;
}

// Originate an H flood for key_hash32 (Lua send_hash_query dv:5625). hard = the verify-on-use escalation.
void Node::emit_hash_query(uint32_t key_hash32, bool hard, bool want_pubkey) {
    if (key_hash32 == 0 || key_hash32 == _key_hash32) return;    // nothing to locate (degenerate / it's us)
    if (want_pubkey && !_crypto_ready) {                         // §2: the mutual exchange needs OUR pubkey -> fail loud, no flood
        MR_EMIT("h_want_pubkey_no_identity", EF_I("key_hash32", static_cast<int64_t>(key_hash32)));
        return;
    }
    h_in in{};
    in.leaf_id = _cfg.leaf_id; in.origin = _node_id; in.key_hash32 = key_hash32;
    in.ttl = protocol::hash_query_max_ttl; in.hard = hard; in.want_pubkey = want_pubkey;
    if (want_pubkey) for (int i = 0; i < 32; ++i) in.requester_ed_pub[i] = _ed_pub[i];   // §2: attach our pubkey so the owner caches us (mutual)
    if (_cfg.is_mobile && _cfg.team_id != 0) { in.team_scoped = true; in.team_id = _cfg.team_id; }   // §mobile 6.2 Fix 5: a team member's locate is TEAM-scoped -> a same-team target answers directly (its local id, for the team plane); others fall through to the home/normal answer. team_id==0 (static/lone) -> unset -> byte-identical H.
    uint8_t buf[8 + 32 + 4];                                     // §2: WANT_PUBKEY H = 40 B; §mobile 6.2: +4 B team_id (a team_scoped WANT_PUBKEY is 44 B)
    const size_t n = pack_h(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    MR_TELEMETRY(
        EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64,     .i = static_cast<int64_t>(key_hash32) },
                           { .key = "ttl",        .type = EventField::T::i64,     .i = protocol::hash_query_max_ttl },
                           { .key = "hard",       .type = EventField::T::boolean, .b = hard } };
        _hal.emit("h_tx", f, 3); );                              // the originate (dv:5625)
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

void Node::park_send(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt) {
    if (_parked_sends_n >= protocol::cap_parked_sends) return;   // full -> drop (the app can retry)
    ParkedSend& p = _parked_sends[_parked_sends_n++];
    p = ParkedSend{};                                           // reset a RECYCLED slot: the array is compacted in
    // place (drain/age-out never clear vacated slots), so a slot last used by an L2c redirect would otherwise
    // keep is_redirect=true and mis-route this plain send-by-hash through the redirect branch on drain.
    p.key_hash32 = key_hash32; p.flags = flags; p.parked_at_ms = _hal.now();
    p.crypt = crypt;                                            // M3: stamp the per-message crypt intent -> drain re-seals CRYPTED (never a silent cleartext downgrade)
    // Clamp to the DM body cap (NOT the 241-B inner buffer): drain_parked_sends -> do_send -> enqueue_data
    // writes body at inner[2+i], so a >239 body would overrun inner[]. on_command already rejects oversize
    // (err_too_large) — this is defense-in-depth so a parked body can never exceed the deliverable size.
    p.body_len = (body_len > protocol::dm_max_body_bytes) ? protocol::dm_max_body_bytes : body_len;
    for (uint8_t i = 0; i < p.body_len; ++i) p.body[i] = body[i];
    MR_TELEMETRY(
        EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) } };
        _hal.emit("send_parked_for_hash", f, 1); );
}

// Slice 4d: park a CROSS-LAYER-capable send. Identical to park_send but marks cross_layer, so when the H-answer
// resolves (node_id, target_layer) the drain originates a CROSS_LAYER DM via a gateway iff target_layer != our leaf.
void Node::park_send_layer(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags) {
    if (_parked_sends_n >= protocol::cap_parked_sends) return;   // full -> drop (the app can retry)
    ParkedSend& p = _parked_sends[_parked_sends_n++];
    p = ParkedSend{};
    p.key_hash32 = key_hash32; p.flags = flags; p.parked_at_ms = _hal.now(); p.cross_layer = true;   // 4d/e2e: keep the app's flags (E2E_ACK_REQ) -> the drain threads them onto the cross-layer DM
    p.body_len = (body_len > protocol::dm_max_body_bytes) ? protocol::dm_max_body_bytes : body_len;
    for (uint8_t i = 0; i < p.body_len; ++i) p.body[i] = body[i];
    MR_TELEMETRY(
        EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) } };
        _hal.emit("send_layer_parked", f, 1); );
}

// L2c: hold a misdelivered DM awaiting the HARD-H resolution. Unlike park_send (a fresh origination), this
// stores the FULL inner (incl. DST_HASH) + the original origin/ctr/flags so drain FORWARDS it identity-intact.
void Node::l2c_park_redirect(uint32_t want_hash, const PostAck& pa) {
    if (_parked_sends_n >= protocol::cap_parked_sends) return;   // full -> drop (the sender retries)
    ParkedSend& p = _parked_sends[_parked_sends_n++];
    p = ParkedSend{};                                           // reset the recycled slot before stamping redirect state
    p.key_hash32 = want_hash; p.flags = pa.flags; p.parked_at_ms = _hal.now();
    p.is_redirect = true; p.origin = pa.origin; p.ctr = pa.ctr; p.ctr_lo = pa.ctr_lo; p.type = pa.type;   // S1/M7a: keep the DataType so the forwarded redirect isn't downgraded to a plain DM
    p.body_len = (pa.inner_len > protocol::max_payload_bytes_hard_cap) ? protocol::max_payload_bytes_hard_cap : pa.inner_len;
    for (uint8_t i = 0; i < p.body_len; ++i) p.body[i] = pa.inner[i];   // body[] holds the full inner for a redirect
    for (int i = 0; i < 8; ++i) p.nonce_seed[i] = pa.nonce_seed[i];     // §1c: keep the originator's seed so a CRYPTED redirect stays openable after the heal
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
void Node::drain_parked_sends(uint32_t key_hash32, uint8_t resolved_id, uint8_t target_layer) {
    bool heal = false;                                            // a confirmed collision found this pass
    uint8_t w = 0;
    for (uint8_t r = 0; r < _parked_sends_n; ++r) {
        const ParkedSend p = _parked_sends[r];
        if (p.key_hash32 == key_hash32) {
            if (p.is_resolve) {                                  // notify-only `resolve` diag: report, don't send
                IdBindConf conf = IdBindConf::claimed;
                (void)id_bind_find_by_hash(key_hash32, &conf);   // confidence was just set by the caller
                push_hash_resolved(key_hash32, resolved_id, conf == IdBindConf::authoritative);
            } else if (p.is_redirect) {
                if (resolved_id == _node_id) {
                    // Proven same-id collision (the owner of want_hash holds OUR id). DEFER the heal to AFTER
                    // the loop: l2c_confirmed_collision -> forced_rejoin mutates _node_id, and running it here
                    // would corrupt a sibling parked entry processed later in this same loop. The DM is dropped
                    // (forwarding-to-self loops); the sender's retry recovers it once the heal converges.
                    heal = true;
                } else if (l2c_enqueue_forward(resolved_id, p.origin, p.ctr, p.ctr_lo, p.flags, p.type, p.body, p.body_len, p.nonce_seed)) {
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
            } else if (p.cross_layer && target_layer != 0xFF && target_layer != _cfg.leaf_id) {
                // Slice 4d (§5): the dst lives on ANOTHER layer -> originate a CROSS_LAYER DM via a bridging gateway.
                send_cross_layer(resolved_id, key_hash32, target_layer, p.body, p.body_len, p.flags);
            } else {
                MR_TELEMETRY(
                    EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(key_hash32) },
                                       { .key = "node",       .type = EventField::T::i64, .i = resolved_id } };
                    _hal.emit("send_hash_resolved", f, 2); );
                // same-layer (incl. a cross_layer park whose dst turned out to be on OUR leaf, §5.1): a plain DM.
                do_send(resolved_id, p.body, p.body_len, p.flags, p.crypt, /*override_dst_hash=*/p.key_hash32);   // load-bearing (OUTSIDE the wrap): fly the held DM; M3: thread crypt; §mobile 3c: carry the queried hash so even the FIRST flood-resolved send to a mobile stamps DST_HASH=M (home forwards, not consumes). For a normal send p.key_hash32 == key_hash_of_id(resolved_id) -> byte-identical.
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
        if (p.cross_layer) { _parked_sends[w++] = p; continue; }   // 4d: a cross-layer park needs the H-answer's target_layer (a beacon carries no addressing layer) -> keep parked
        IdBindConf conf = IdBindConf::claimed;
        const int id = id_bind_find_by_hash(p.key_hash32, &conf);
        if (id >= 0 && conf == IdBindConf::authoritative && static_cast<uint8_t>(id) != _node_id) {
            if (p.is_resolve) {
                push_hash_resolved(p.key_hash32, static_cast<uint8_t>(id), true);   // a beacon resolved it -> answer
            } else if (p.is_redirect) {
                if (l2c_enqueue_forward(static_cast<uint8_t>(id), p.origin, p.ctr, p.ctr_lo, p.flags, p.type, p.body, p.body_len, p.nonce_seed)) {
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
                do_send(static_cast<uint8_t>(id), p.body, p.body_len, p.flags, p.crypt);   // load-bearing (OUTSIDE the wrap); M3: thread the stamped crypt intent (a beacon-resolved parked sendhashx still flies CRYPTED)
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
            if (p.is_resolve) {
                push_hash_resolved(p.key_hash32, 0, false);     // a `resolve` that never resolved -> timeout answer
            } else {
                MR_TELEMETRY(
                    EventField f[] = { { .key = "key_hash32", .type = EventField::T::i64, .i = static_cast<int64_t>(p.key_hash32) } };
                    _hal.emit("send_hash_giveup", f, 1); );
            }
            continue;                                            // drop (handled: reported / gave up)
        }
        _parked_sends[w++] = p;
    }
    _parked_sends_n = w;
}

// ---- Diagnostic `resolve` (CmdKind::resolve) -----------------------------------------------------------
// Locate the node owning key_hash32 WITHOUT sending a DM. An authoritative cache hit (or our own hash)
// answers immediately; otherwise park a notify-only request + flood H, and the answer/timeout rides the
// hash_resolved push. SOFT (hard=false) accepts a cached authoritative binding; HARD always floods to reach
// the owner (verify-on-use), mirroring send_by_hash.
void Node::request_resolve(uint32_t key_hash32, bool hard) {
    if (key_hash32 == 0) return;                                  // 0 = no-hash sentinel
    if (key_hash32 == _key_hash32) { push_hash_resolved(key_hash32, _node_id, true); return; }   // it's us
    if (!hard) {
        IdBindConf conf = IdBindConf::claimed;
        const int id = id_bind_find_by_hash(key_hash32, &conf);
        if (id >= 0 && conf == IdBindConf::authoritative) {
            push_hash_resolved(key_hash32, static_cast<uint8_t>(id), true); return;               // cached + trusted
        }
    }
    park_resolve_request(key_hash32);                             // unknown / soft-cached / hard -> flood + wait
    emit_hash_query(key_hash32, hard);
}

// Park a notify-only resolve request (no body). De-dup by hash so a re-issued `resolve` refreshes the timer
// instead of consuming a second slot. Bounded by cap_parked_sends (shared with send-by-hash / L2c redirect).
void Node::park_resolve_request(uint32_t key_hash32) {
    for (uint8_t i = 0; i < _parked_sends_n; ++i)
        if (_parked_sends[i].is_resolve && _parked_sends[i].key_hash32 == key_hash32) {
            _parked_sends[i].parked_at_ms = _hal.now(); return;  // already pending -> refresh the TTL
        }
    if (_parked_sends_n >= protocol::cap_parked_sends) return;   // full -> drop (operator re-runs)
    ParkedSend& p = _parked_sends[_parked_sends_n++];
    p = ParkedSend{};
    p.key_hash32 = key_hash32; p.is_resolve = true; p.parked_at_ms = _hal.now();
}

// Enqueue the hash_resolved push: origin = owner node_id (0 = unresolved/timeout), dst = authoritative?1:0,
// body[0..3] = the queried hash (LE) so the host knows which `resolve` this answers.
void Node::push_hash_resolved(uint32_t key_hash32, uint8_t node_id, bool authoritative) {
    Push p{};
    p.kind    = PushKind::hash_resolved;
    p.origin  = node_id;
    p.dst     = authoritative ? 1 : 0;
    p.body[0] = static_cast<uint8_t>(key_hash32);
    p.body[1] = static_cast<uint8_t>(key_hash32 >> 8);
    p.body[2] = static_cast<uint8_t>(key_hash32 >> 16);
    p.body[3] = static_cast<uint8_t>(key_hash32 >> 24);
    p.body_len = 4;
    enqueue_push(p);
}

}  // namespace meshroute
