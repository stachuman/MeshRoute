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
                MR_EMIT("addr_conflict_self_defended", EF_I("node", node_id),
                        EF_I("observed_key_hash32", static_cast<int64_t>(key_hash32)), EF_S("source", id_bind_source_str(source)));
                return false;
            }
            if (!authoritative) {                               // claimed -> refuse, keep the known binding
                MR_EMIT("addr_conflict_observed", EF_I("node", node_id),
                        EF_I("known_key_hash32", static_cast<int64_t>(_active->_id_bind[i].key_hash32)),
                        EF_I("observed_key_hash32", static_cast<int64_t>(key_hash32)), EF_S("source", id_bind_source_str(source)));
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
                    MR_EMIT("addr_conflict_mediated", EF_I("node", node_id), EF_I("winner", static_cast<int64_t>(winner)),
                            EF_I("loser", static_cast<int64_t>(loser)));
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
        if (authoritative) evict_aliased_hosted_mobile(node_id, key_hash32);   // §S0(b): a confirmed static reclaims an id we gave a mobile
        return true;
    }
    // NEW node_id: heal any stale holder of this hash FIRST (a pure rehome frees its slot), then cap-check.
    id_bind_evict_other_hash_holders(key_hash32, node_id);
    if (_active->_id_bind_n >= _cfg.cap_id_bind) {                        // table full -> refuse (Lua dv:4707)
        MR_EMIT("table_cap_hit", EF_S("table", "id_bind"), EF_I("cap", _cfg.cap_id_bind), EF_I("size", _active->_id_bind_n),
                EF_S("action", "refuse"), EF_I("node", node_id));
        return false;
    }
    _active->_id_bind[_active->_id_bind_n++] = { key_hash32, now, node_id, static_cast<uint8_t>(source), static_cast<uint8_t>(confidence) };
    MR_EMIT("id_bind_set", EF_I("node", node_id), EF_I("key_hash32", static_cast<int64_t>(key_hash32)),
            EF_S("source", id_bind_source_str(source)), EF_S("confidence", id_bind_conf_str(confidence)));
    if (authoritative) evict_aliased_hosted_mobile(node_id, key_hash32);   // §S0(b): a confirmed static reclaims an id we gave a mobile
    return true;
}

// Find a NON-EXPIRED binding for key_hash32 -> its node_id (Lua id_bind_find_by_hash dv:4764). Skips (does
// not remove) expired entries — removal is the periodic age_out sweep. The self-binding never expires.
// This is the call that makes "any node that knows answers" work. Returns -1 on miss.
int Node::id_bind_find_by_hash(uint32_t key_hash32, IdBindConf* conf_out) const {
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
            MR_EMIT("id_bind_aged", EF_I("node", e.node_id), EF_I("key_hash32", static_cast<int64_t>(e.key_hash32)),
                    EF_I("age_ms", static_cast<int64_t>(now - e.last_seen_ms)), EF_I("ttl_ms", static_cast<int64_t>(_cfg.id_bind_ttl_ms)));
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
bool Node::peer_key_set(uint32_t key_hash32, const uint8_t ed_pub[32], PeerKeyConf conf, const char* name, uint8_t name_len) {
    // Hash-verifiable: key_hash32 == LE(ed_pub[0..3]) (== identity.h key_hash32_of). A forged binding is REFUSED.
    const uint32_t derived = key_hash32_of(ed_pub);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
    if (derived != key_hash32) return false;
    const uint64_t now = _hal.now();
    auto& L = *_active;
    for (uint16_t i = 0; i < L._peer_keys_n; ++i) {                 // already cached -> refresh; upgrade, never downgrade
        if (L._peer_keys[i].key_hash32 == key_hash32) {
            const bool existing_pinned = (L._peer_keys[i].confidence == static_cast<uint8_t>(PeerKeyConf::pinned));
            if (existing_pinned && conf != PeerKeyConf::pinned) return true;   // §1: PINNED is IMMUTABLE to an on-air set (no-op, no refresh)
            L._peer_keys[i].last_seen_ms = now;
            if (name && name_len) (void)peer_name_set(key_hash32, name, name_len);   // §1.3: REFRESH the name (mutable) even when the key is unchanged. §AB2: ONE name writer (cannot miss — we are inside its match)
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
    L._peer_keys[slot].name_len = 0;
    L._peer_keys[slot].peer_confirmed = false;   // §S2: a fresh (or evicted-recycled) entry is UNCONFIRMED until we open a sealed frame from it (a plaintext/INTRO cache never confirms)
    if (name && name_len) (void)peer_name_set(key_hash32, name, name_len);   // §1.3: cache the peer's name with the key. §AB2: ONE name writer — key_hash32 was written above and the early scan proved it unique, so this always matches
    return true;
}

// ★ §AB2 (spec 2026-07-29 §2.3): THE ONE name writer for _peer_keys — the write twin of peer_name_find below, and the
// engine half of the `peername` verb. Clamps to protocol::peer_name_max and overwrites; touches NOTHING else (not
// ed_pub, not confidence, not last_seen_ms, not peer_confirmed). See node.h for why this is NOT peer_key_set.
// ⚠ It deliberately does NOT age-gate the lookup the way peer_key_find does: an aged-but-present row is still the row
// `nameof`/the AB3 view will show, so refusing to rename it would make two verbs disagree about one entry.
bool Node::peer_name_set(uint32_t key_hash32, const char* name, uint8_t name_len) {
    if (!name) return false;
    auto& L = *_active;
    for (uint16_t i = 0; i < L._peer_keys_n; ++i) {
        if (L._peer_keys[i].key_hash32 != key_hash32) continue;
        const uint8_t nl = name_len > protocol::peer_name_max ? protocol::peer_name_max : name_len;
        for (uint8_t b = 0; b < nl; ++b) L._peer_keys[i].name[b] = name[b];
        L._peer_keys[i].name_len = nl;
        return true;
    }
    return false;   // C2: no row for this hash -> the caller refuses loud; never invent a keyless placeholder
}

// ==================== ★★★ §AB4 — RETAINED PEER LOCATION (spec 2026-07-29 §2.7) ====================
// See node.h (PeerLocSrc, the _peer_loc ring, and the two declarations) for the design: RAM-only and why, why neither
// PeerKey nor _team_keys is the home, why recent_ring.h is a refused forced fit, and the shared-key trust bound.

// THE ONE SETTER, for both sources. Four steps, mirroring peer_key_set's shape (U3) over this ring's own stamp:
// refresh-in-place / append-if-room / evict-the-STALEST. ⚠ No "never evict" tier here, unlike peer_key_set's pinned
// exemption: every position is equally perishable, so the oldest is always the right victim — a pinned-key analogue
// would pin a position, which is the stale-fix failure mode the whole RAM-only ruling exists to avoid.
bool Node::peer_loc_set(uint32_t key_hash32, int32_t lat_e7, int32_t lon_e7, PeerLocSrc src) {
    if (key_hash32 == 0) return false;   // C2: 0 is the "no hash" sentinel (peer_book_by_hash refuses it too), never an identity
    // ms -> SECONDS, the ring's stamp unit. See PeerLoc::t_s: uint32 seconds spans ~136 years of uptime, so the wrap is
    // unreachable on a monotonic since-boot clock and there is deliberately no wrap handling.
    const uint32_t now_s = static_cast<uint32_t>(_hal.now() / 1000u);
    uint8_t slot;
    uint8_t i = 0;
    for (; i < _peer_loc_n; ++i) if (_peer_loc[i].key_hash32 == key_hash32) break;
    if (i < _peer_loc_n)                        slot = i;                    // a FRESHER position for a hash we already hold -> replace it
    else if (_peer_loc_n < cap_peer_loc)        slot = _peer_loc_n++;        // room
    else {                                                                  // full -> evict the STALEST slot
        uint8_t victim = 0;
        for (uint8_t k = 1; k < _peer_loc_n; ++k) if (_peer_loc[k].t_s < _peer_loc[victim].t_s) victim = k;
        slot = victim;
    }
    _peer_loc[slot] = PeerLoc{};   // ★ whole-record reset FIRST, so `reserved` stays zero and no field of an evicted
                                   // predecessor can survive into the new entry (that is what the NAMED pad buys)
    _peer_loc[slot].key_hash32 = key_hash32;
    _peer_loc[slot].lat_e7 = lat_e7;
    _peer_loc[slot].lon_e7 = lon_e7;
    _peer_loc[slot].t_s    = now_s;
    _peer_loc[slot].src    = src;
    return true;
}

bool Node::peer_loc_find(uint32_t key_hash32, int32_t& lat_e7, int32_t& lon_e7,
                         uint32_t& age_s, PeerLocSrc& src) const {
    if (key_hash32 == 0) return false;
    for (uint8_t i = 0; i < _peer_loc_n; ++i) {
        if (_peer_loc[i].key_hash32 != key_hash32) continue;
        const uint32_t now_s = static_cast<uint32_t>(_hal.now() / 1000u);
        // ★ A backwards clock (a test rewinding TestHal, or a clock that ever moved) reports MAXIMALLY STALE, never 0:
        // 0 would render an unknown-vintage position as CURRENT, which is precisely the misleading-fix failure the
        // RAM-only ruling exists to prevent. Fail in the direction the app discards (C2's spirit for a read).
        age_s  = (now_s >= _peer_loc[i].t_s) ? (now_s - _peer_loc[i].t_s) : 0xFFFFFFFFu;
        lat_e7 = _peer_loc[i].lat_e7;
        lon_e7 = _peer_loc[i].lon_e7;
        src    = _peer_loc[i].src;
        return true;
    }
    return false;   // no position for this hash — the NORMAL case, not an error
}

uint8_t Node::peer_name_find(uint32_t key_hash32, char* out, uint8_t cap) const {   // §1.3: the cached name for a peer hash (0 = unknown)
    for (uint16_t i = 0; i < _active->_peer_keys_n; ++i)
        if (_active->_peer_keys[i].key_hash32 == key_hash32) {
            const uint8_t n = _active->_peer_keys[i].name_len < cap ? _active->_peer_keys[i].name_len : cap;
            for (uint8_t b = 0; b < n; ++b) out[b] = _active->_peer_keys[i].name[b];
            return n;
        }
    return 0;
}

// ================== ★★ §AB3 — THE GENERATED ADDRESS-BOOK VIEW (spec 2026-07-29 §2.1/§2.5) ==================
// See node.h for the row shape, the zero-RAM rationale, the §2.6(a) bound and the FORBIDDEN _id_bind repair.
// Every function here is a PURE READ: no table is written, no last_seen_ms refreshed, no telemetry emitted. That is
// load-bearing twice over — it is what makes the corpus byte-identical BY CONSTRUCTION even though lib/core IS
// compiled by the simulator, and it is what lets `nameof` show an aged-but-present row without extending its lease.

// _peer_keys row index for a hash, or -1. NOT age-gated: "is there a row" and "is its key usable" are different
// questions, and the view needs the first (a name survives its key's lease — see peer_book_fill_from_peer_key).
// ⓘ peer_name_find / peer_name_set / peer_confirmed each still carry their own copy of this two-line scan; folding
// them onto this helper is a dedup slice, not this one (C1). Noted so it can be found.
int Node::peer_key_slot_of(uint32_t key_hash32) const {
    if (key_hash32 == 0) return -1;
    for (uint16_t i = 0; i < _active->_peer_keys_n; ++i)
        if (_active->_peer_keys[i].key_hash32 == key_hash32) return static_cast<int>(i);
    return -1;
}

#if MR_FEAT_TEAM
// hash -> team_local_id, FRESHEST wins, and count the losers. See node.h for why this table (unlike _id_bind) really
// can alias, and why the gate is team_key_of_id's verbatim gate rather than team_id_of_key's.
// ⚠ DELIBERATELY NOT team_id_of_key (node_routing.cpp), and this is a FINDING rather than a preference: that function
// returns the FIRST matching row, so on an aliased hash it silently picks by table order. It is on the live
// PLAINTEXT send-by-hash path, so changing it is a behaviour fix in its own right (C1) — reported, not folded in here.
uint8_t Node::team_id_of_key_freshest(uint32_t key_hash32, uint8_t& alias_dropped) const {
    alias_dropped = 0;
    if (key_hash32 == 0 || _cfg.team_id == 0) return 0;
    const uint64_t now = _hal.now();
    uint8_t  best = 0; uint64_t best_seen = 0; uint8_t hits = 0;
    for (uint8_t i = 0; i < _active->_team_keys_n; ++i) {
        const auto& e = _active->_team_keys[i];
        if (e.key_hash32 != key_hash32 || !is_team_peer(e.id)) continue;
        if (now - e.last_seen_ms > protocol::id_bind_ttl_ms) continue;      // §P2-6 48 h staleness — team_key_of_id's rule
        ++hits;
        if (best == 0 || e.last_seen_ms > best_seen) { best = e.id; best_seen = e.last_seen_ms; }
    }
    alias_dropped = hits ? static_cast<uint8_t>(hits - 1) : 0;
    return best;
}
#endif

// The reverse (hash -> id) joins for a row that already carries a hash. Fills static_id/static_authoritative and
// team_id/team_alias_dropped, leaving them at 0/false when that plane holds nothing.
void Node::peer_book_join_ids(PeerBookRow& r) const {
    if (r.hash == 0) return;                                  // an id-only row has nothing to join BY
    IdBindConf ic = IdBindConf::claimed;
    const int sid = id_bind_find_by_hash(r.hash, &ic);        // U1: the existing _id_bind reverse scan (skips expired)
    if (sid >= 0) { r.static_id = static_cast<uint8_t>(sid); r.static_authoritative = (ic == IdBindConf::authoritative); }
    r.team_id = team_id_of_key_freshest(r.hash, r.team_alias_dropped);
    peer_book_join_loc(r);
}

// ★ §AB4: hash -> the retained position. Leaves has_location false (and the four fields at their `PeerBookRow{}` zeros)
// when nothing is held, which is the NORMAL outcome for most rows. Kept as its own function rather than inlined into
// peer_book_join_ids because peer_book_walk's unkeyed passes resolve their ids directly and never call join_ids — see
// node.h for why those two extra call sites matter to CL2.
void Node::peer_book_join_loc(PeerBookRow& r) const {
    if (r.hash == 0) return;   // an id-only row has no identity to key a position by (§1.2)
    r.has_location = peer_loc_find(r.hash, r.lat_e7, r.lon_e7, r.loc_age_s, r.loc_src);
}

// _peer_keys[slot] -> a row, plus both reverse id joins.
// ★★ THE ONE JUDGEMENT CALL IN HERE, and it is the §0.1 failure mode in miniature: an AGED non-pinned row is still
// PRESENT (peer_name_find and peer_name_set deliberately do not age-gate, so a rename and `nameof` agree about it), but
// peer_key_find REFUSES it, so a seal to that peer would FAIL. Reporting its stored `authoritative` would make the app
// offer "send encrypted" and the user get `FAILED (no recipient pubkey)` — exactly the defect AB2's `conf` field exists
// to remove. ⇒ an unusable row keeps its NAME and its ids but reports has_key=false AND conf=overheard, the
// least-capable level (peerkeyconf_name's own out-of-range policy: never claim a capability we cannot back).
void Node::peer_book_fill_from_peer_key(uint16_t slot, PeerBookRow& r) const {
    const PeerKey& p = _active->_peer_keys[slot];
    r = PeerBookRow{};
    r.hash = p.key_hash32;
    r.name_len = p.name_len > protocol::peer_name_max ? protocol::peer_name_max : p.name_len;
    for (uint8_t b = 0; b < r.name_len; ++b) r.name[b] = p.name[b];
    const bool pinned = (p.confidence == static_cast<uint8_t>(PeerKeyConf::pinned));
    const bool usable = pinned || protocol::peer_key_ttl_ms == 0
                        || (_hal.now() - p.last_seen_ms) < protocol::peer_key_ttl_ms;   // peer_key_find's rule, verbatim
    if (usable) { r.has_key = true; r.conf = static_cast<PeerKeyConf>(p.confidence); r.peer_confirmed = p.peer_confirmed; }
    peer_book_join_ids(r);
}

// The single emission pass of spec §2.1. DEDUP IS ON `hash`, and it needs NO auxiliary "seen" array: every later pass
// can ask the earlier pass's TABLE directly whether it already covers a hash, which is what keeps this zero-RAM.
uint16_t Node::peer_book_walk(bool include_id_rows, PeerBookVisit fn, void* ctx) const {
    uint16_t n = 0;
    PeerBookRow r{};
    // (1) _peer_keys FIRST — the only rows that carry a name, a key, a confidence and peer_confirmed. Their static_id
    //     and team_id are MERGED here by the reverse joins, which is what makes passes (2)/(3) pure "not already
    //     covered" filters instead of needing to reach back into an already-emitted row.
    for (uint16_t i = 0; i < _active->_peer_keys_n; ++i) {
        peer_book_fill_from_peer_key(i, r);
        ++n; if (fn) fn(r, ctx);
    }
    if (!include_id_rows) return n;                          // ★ §2.6(a): the JSON book stops here, ≤ cap_peer_keys rows
    // (2) _id_bind — a hash pass (1) already emitted was merged there, so only an UNKEYED hash yields a row.
    //     ⓘ hash == 0 ⇒ the spec §1.3 "id-only" row. It is supported (cheap, and the shape is real) but see the
    //     report: no live id_bind_set caller passes 0, and the hash-uniqueness eviction collapses all such rows to
    //     ONE, so this arm is reachable from the test seam and effectively not from the network.
    for (uint16_t i = 0; i < _active->_id_bind_n; ++i) {
        const IdBind& e = _active->_id_bind[i];
        if (e.key_hash32 && peer_key_slot_of(e.key_hash32) >= 0) continue;   // already emitted by (1)
        r = PeerBookRow{};
        r.hash = e.key_hash32;
        r.static_id = e.node_id;
        r.static_authoritative = (e.confidence == static_cast<uint8_t>(IdBindConf::authoritative));
        if (r.hash) r.team_id = team_id_of_key_freshest(r.hash, r.team_alias_dropped);   // §18: the same hash may hold both
        peer_book_join_loc(r);                                                          // §AB4 (no join_ids here: static_id came straight off the row)
        ++n; if (fn) fn(r, ctx);
    }
#if MR_FEAT_TEAM
    // (3) _team_keys — a hash covered by (1) or (2) is already merged; anything else is a new hash+team_id row.
    for (uint8_t i = 0; i < _active->_team_keys_n; ++i) {
        const auto& e = _active->_team_keys[i];
        uint8_t dropped = 0;
        if (e.key_hash32 == 0 || team_id_of_key_freshest(e.key_hash32, dropped) != e.id) continue;   // stale alias / gated out -> (1)/(2)/the winner covers it
        if (peer_key_slot_of(e.key_hash32) >= 0) continue;                    // covered by (1)
        if (id_bind_find_by_hash(e.key_hash32) >= 0) continue;                 // covered by (2)
        r = PeerBookRow{};
        r.hash = e.key_hash32; r.team_id = e.id; r.team_alias_dropped = dropped;
        peer_book_join_loc(r);                                                          // §AB4 (ditto — team_id came straight off the row)
        ++n; if (fn) fn(r, ctx);
    }
    // (4) _team_peer bits nothing else covered ⇒ TEAM-ID-ONLY rows: a teammate we route to whose hash we never cached
    //     (spec §1.3's second id-only flavour — this one IS network-reachable: node_beacon sets the bit from a
    //     multi-hop DV entry that carries no key at all).
    if (_cfg.team_id != 0) {
        for (uint16_t id = 1; id <= 254; ++id) {
            if (!is_team_peer(static_cast<uint8_t>(id))) continue;
            uint32_t th = 0;
            if (team_key_of_id(static_cast<uint8_t>(id), th) && th != 0) continue;   // has a _team_keys row -> (1)/(2)/(3)
            r = PeerBookRow{};
            r.team_id = static_cast<uint8_t>(id);
            ++n; if (fn) fn(r, ctx);
        }
    }
#endif
    return n;
}

bool Node::peer_book_by_hash(uint32_t key_hash32, PeerBookRow& out) const {
    out = PeerBookRow{};
    if (key_hash32 == 0) return false;                       // 0 is the "no hash" sentinel, never a queryable identity
    const int slot = peer_key_slot_of(key_hash32);
    if (slot >= 0) { peer_book_fill_from_peer_key(static_cast<uint16_t>(slot), out); return true; }
    out.hash = key_hash32;
    peer_book_join_ids(out);
    return out.static_id != 0 || out.team_id != 0;            // no key, no id anywhere -> we know nothing about it
}

// ★ Spec §2.5's fix. The two arms resolve through key_hash_of_id and team_key_of_id — the SAME functions the send path
// (node_mac.cpp) and `reqpubkey <team-id>` (node.cpp CmdKind::reqpubkey) use — so `hashof`, `reqpubkey` and a send can
// no longer answer an id differently. When one number resolves in BOTH planes it fills BOTH rows: the §18 dual-identity
// space is real, and silently picking one is the defect this whole slice exists to remove.
uint8_t Node::peer_book_by_id(uint8_t id, PeerBookRow& static_out, PeerBookRow& team_out) const {
    static_out = PeerBookRow{}; team_out = PeerBookRow{};
    uint8_t mask = 0;
    if (id == 0 || id == 0xFF) return 0;                     // 0 = unprovisioned, 0xFF = reserved
    uint32_t h = 0;
    if (key_hash_of_id(id, h)) {                             // STATIC: authoritative + fresh, exactly what DST_HASH stamps
        if (!peer_book_by_hash(h, static_out)) { static_out = PeerBookRow{}; static_out.hash = h; }
        static_out.static_id = id;                           // the queried id IS the binding's id (one-hash-one-id)
        static_out.static_authoritative = true;
        mask |= kPeerBookStatic;
    }
    uint32_t th = 0;
    if (team_key_of_id(id, th) && !(mask && th == h)) {       // TEAM: the team key cache — and skip an exact duplicate
        if (!peer_book_by_hash(th, team_out)) { team_out = PeerBookRow{}; team_out.hash = th; }
        // ⚠ team_id is left as the view RESOLVED it, NOT overwritten with `id`: when two team ids alias one hash the
        // freshest is the honest answer and team_alias_dropped says a loser exists. The caller reports the queried id.
        if (team_out.team_id == 0) team_out.team_id = id;
        mask |= kPeerBookTeam;
    }
    return mask;
}

// §S6: enqueue a peer_key_cached push carrying the peer's cached NAME (copied NOW, at cache time — the cache may
// age by drain time). body empty (body_len 0) when unknown -> write_push omits "name". One path for all 4 cache sites.
// ★ §AB2: it now also carries the CONFIDENCE, read back out of the LIVE table through the existing peer_key_find (U1/U2
// — the same "read every field from the live table" discipline as src/firmware_commands.cpp's peer_store_sync, so no
// caller can announce a confidence that disagrees with RAM). peer_key_find is a pure read (no last_seen_ms refresh), so
// this addition cannot move a scenario stream.
// ⚠ Its `false` case (absent, or aged between the cache event and here) LEAVES peer_conf at 0 = `overheard` = "you
// cannot seal to this peer". That is the SAFE direction on purpose: over-reporting the level is what produced the
// §0.1 failure this field exists to fix.
void Node::push_peer_key_cached(uint32_t key_hash32) {
    Push pu{}; pu.kind = PushKind::peer_key_cached; pu.sender_hash = key_hash32;
    pu.body_len = peer_name_find(key_hash32, reinterpret_cast<char*>(pu.body), protocol::peer_name_max);
    uint8_t ed[32]; PeerKeyConf conf = PeerKeyConf::overheard;
    if (peer_key_find(key_hash32, ed, &conf)) pu.peer_conf = static_cast<uint8_t>(conf);
    enqueue_push(pu);
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

bool Node::peer_confirmed(uint32_t key_hash32) const {
    // §S2: no cached entry -> unconfirmed -> INTRO attaches (first contact). An entry confirmed once (a sealed
    // open, e2e_open_trial) stays confirmed for its lifetime (a plaintext/INTRO cache never sets the bit).
    for (uint16_t i = 0; i < _active->_peer_keys_n; ++i)
        if (_active->_peer_keys[i].key_hash32 == key_hash32) return _active->_peer_keys[i].peer_confirmed;
    return false;
}

// §S2 INTRO first-contact attach (D1): build [ed_pub 32][name_len 1][name] into pfx (the sender's key prefix), or
// return 0 = no attach. Gates: intro_attach cfg ON · a crypto identity exists (_crypto_ready — ALSO the s18-inert
// gate: an identity-less node has no key to attach) · the send resolves to PLAINTEXT (a sealed body must never carry
// a cleartext key prefix) · dst is a real other peer · we have NOT peer_confirmed(dst) (no sealed frame opened from
// them yet) · prefix+body fits the DM body cap (else send WITHOUT the attach — message delivery beats key bootstrap).
uint8_t Node::intro_attach_prefix(uint32_t dst_hash, CryptIntent crypt, uint8_t body_len, uint8_t* pfx, uint8_t pfx_cap) {
    if (!_cfg.intro_attach) return 0;
    if (!_crypto_ready) return 0;                                  // no identity -> nothing to attach (== s18-inert)
    const bool want_crypt = (crypt == CryptIntent::on)  ? true
                          : (crypt == CryptIntent::off) ? false
                                                        : _cfg.e2e_dm;
    if (want_crypt) return 0;                                      // sealed send -> NEVER prefix a cleartext key (leak)
    if (dst_hash == 0 || dst_hash == _key_hash32) return 0;        // degenerate / self
    if (peer_confirmed(dst_hash)) return 0;                        // they already hold our key -> plain send
    char nm[32]; const uint8_t nlen = effective_name(nm, 32);      // INTRO name field is <=32 B (matches the PeerKey name cap)
    const uint8_t need = static_cast<uint8_t>(33 + nlen);          // ed_pub 32 + name_len 1 + name
    if (need > pfx_cap) return 0;
    if (static_cast<size_t>(need) + body_len > protocol::dm_max_body_bytes) {   // too large -> deliver the message plain
        MR_EMIT("intro_attach_too_large", EF_I("hash", static_cast<int64_t>(dst_hash)), EF_I("body_len", body_len));
        return 0;
    }
    for (uint8_t i = 0; i < 32; ++i) pfx[i] = _ed_pub[i];
    pfx[32] = nlen;
    for (uint8_t i = 0; i < nlen; ++i) pfx[33 + i] = static_cast<uint8_t>(nm[i]);
    return need;
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
            L._peer_keys[i].peer_confirmed = true;                 // §S2: a SEALED frame opened from this peer => they hold OUR key => stop attaching INTRO to plaintext sends toward them (set ONLY here, never on a plaintext receipt)
            return true;
        }
    }
    return false;
}

// §S4 SEALED_RELAY seal: seal `body` to `target_hash` under OUR identity + pack the relay body [seal_ctr 2 LE][seed8 8]
// [ct‖tag]. The seal is SAME-LAYER-shaped (aad = target_hash, pt = [origin][source_hash=_key_hash32][body]); a
// dedicated CARRIED ctr (++_relay_seal_ctr) drives the nonce so a delegating home can re-originate under its OWN frame
// ctr (MAC dedup) while the recipient still reproduces the nonce from the carried ctr + seed8. e2e_seal_inner does the
// key lookup / fail-loud (no_pubkey / no_identity); we reuse its [target_hash 4][ct‖tag] output, dropping the 4-B aad
// prefix (the recipient re-derives the aad from its own key_hash32). origin byte = 0 (layer-local, IGNORED on open).
uint8_t Node::build_sealed_relay_body(uint32_t target_hash, const uint8_t* body, uint8_t body_len,
                                      uint8_t* out, uint8_t out_cap, SealOutcome& outcome) {
    outcome = SealOutcome::ok;
    if (out_cap < 10) { outcome = SealOutcome::too_large; return 0; }
    const uint16_t seal_ctr = ++_relay_seal_ctr;
    uint8_t seed[8];
    // Seal IN PLACE (no scratch buffer): e2e_seal_inner writes [aad(dst_hash) 4][ct‖tag] at out+6, so ct‖tag lands at
    // out[10..]; we then overwrite out[0..9] with [seal_ctr 2][seed8 8] (the aad at out[6..9] is discardable — the
    // recipient re-derives the aad from its own key_hash32). Net relay body = [seal_ctr 2][seed8 8][ct‖tag].
    // ✖ MISSING (§loc-per-send, 2026-07-31): the seal flags and lat/lon are HARD-CODED, so a SEALED_RELAY carries NO
    // location. This is not an oversight to patch here — the relay body is [seal_ctr 2][seed8 8][ct‖tag] with NO flags
    // word on the wire, and the receiver's e2e_open_relay hard-codes the matching `DATA_FLAG_SOURCE_HASH` on the open
    // side. Adding LOCATION on this side alone would make the peer parse the 6 position bytes as message TEXT. Carrying
    // it needs a SEALED_RELAY body-format change (a flags byte or a second inner variant) = its own slice (C1/C4).
    // ⇒ until then a `-l` send that would take this transport is REFUSED, not silently stripped: the guard is at
    // enqueue_cross_layer (the structural choke point) plus node.cpp's send_layer verb (the operator-facing one).
    const size_t n = e2e_seal_inner(out + 6, static_cast<size_t>(out_cap) - 6, seed,
                                    static_cast<uint8_t>(DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH),
                                    target_hash, /*origin*/ 0, seal_ctr, _key_hash32,
                                    /*lat*/ 0, /*lon*/ 0, body, body_len, outcome);
    if (n == 0) return 0;                                          // outcome set (no_pubkey/no_identity/too_large/bad_rng)
    // n = [aad 4][ct‖tag]; ct‖tag now sits at out[10..] (out+6 offset 4). Stamp the clear header over out[0..9].
    out[0] = static_cast<uint8_t>(seal_ctr); out[1] = static_cast<uint8_t>(seal_ctr >> 8);
    for (int i = 0; i < 8; ++i) out[2 + i] = seed[i];
    return static_cast<uint8_t>(static_cast<size_t>(2) + 8 + (n - 4));   // 10 + (ct‖tag len)
}

// §S4 SEALED_RELAY open (directed — the CLEAR SOURCE_HASH names the sender, no trial). Rebuild the SAME-LAYER-shaped
// sealed inner [our_hash 4 (aad)][ct‖tag] the seal produced and hand it to e2e_open_inner: nonce = dm_nonce(seed8,
// seal_ctr, our_hash), aad = our_hash, and e2e_open_inner's own check enforces the SEALED source_hash == source_hash
// (anti-spoof, §S4 Part B). The sealed origin byte is recovered-but-IGNORED (layer-local garbage). false = drop.
bool Node::e2e_open_relay(const uint8_t* relay_body, size_t len, uint32_t source_hash,
                          uint8_t* body_out, uint8_t& body_len_out) {
    body_len_out = 0;
    if (!_crypto_ready) return false;
    if (len < static_cast<size_t>(2) + 8 + DM_TAG_LEN) return false;   // [seal_ctr 2][seed8 8][>=tag]
    const uint16_t seal_ctr = static_cast<uint16_t>(relay_body[0] | (relay_body[1] << 8));
    const uint8_t* seed     = relay_body + 2;
    const uint8_t* sealed   = relay_body + 10;
    const size_t   sealed_len = len - 10;
    static uint8_t tmp[protocol::max_payload_bytes_hard_cap];   // static (non-reentrant loop task) — this opens on the cramped do_post_ack stack (ADDENDUM 4); keep the ~241 B out of the frame
    if (static_cast<size_t>(4) + sealed_len > sizeof tmp) return false;
    tmp[0] = static_cast<uint8_t>(_key_hash32);       tmp[1] = static_cast<uint8_t>(_key_hash32 >> 8);
    tmp[2] = static_cast<uint8_t>(_key_hash32 >> 16); tmp[3] = static_cast<uint8_t>(_key_hash32 >> 24);
    for (size_t i = 0; i < sealed_len; ++i) tmp[4 + i] = sealed[i];
    uint32_t origin_ignored = 0, src_out = 0; bool hl = false; int32_t la = 0, lo = 0;
    return e2e_open_inner(tmp, static_cast<size_t>(4) + sealed_len, seed,
                          static_cast<uint8_t>(DATA_FLAG_SOURCE_HASH), seal_ctr, source_hash,
                          origin_ignored, src_out, hl, la, lo, body_out, body_len_out);
}

// =============================================================================
// Phase A — the H flood + resolve handler. The defining behaviour: ANY node that
// already holds the binding answers + STOPS the flood; the flood is the fallback.
// =============================================================================

// per-(origin, key_hash32, VARIANT) flood dedup (Lua hash_query_seen; hash_query_seen_ttl_ms window). Keying on
// `hard` is load-bearing: a HARD query (verify-on-use) must NOT be suppressed by a prior SOFT's seen-entry, or
// the escalation that reaches the owner is silently swallowed. Mirrors rreq_seen.
// ★ §team-parity T6/B: `team_scoped` (the H frame's own plane bit) joins the key — see the HashQuerySeen struct comment
// for the measured reason (the role-exclusion invariant it used to rely on is defeatable by `cfg set team_id` on a
// non-mobile node). Static reduction: every static H carries team_scoped=false and every existing team H carries true,
// so no static entry's key value changes and a static-only mesh is byte-identical by construction.
bool Node::hash_query_seen_recently(uint8_t origin, uint32_t key_hash32, bool hard, bool want_pubkey, bool team_scoped) {
    return recent_ring_hit(_active->_hash_query_seen, _active->_hash_query_seen_n,
                           HashQuerySeen{ origin, key_hash32, 0, hard, want_pubkey, team_scoped },
                           _hal.now(), protocol::hash_query_seen_ttl_ms);
}
void Node::mark_hash_query_seen(uint8_t origin, uint32_t key_hash32, bool hard, bool want_pubkey, bool team_scoped) {
    recent_ring_mark(_active->_hash_query_seen, _active->_hash_query_seen_n,
                     HashQuerySeen{ origin, key_hash32, _hal.now(), hard, want_pubkey, team_scoped });
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
    // §P2-1 (mixed-leaf team): a SAME-TEAM team-scoped H is leaf-EXEMPT — its membership is team_id, so a teammate homed on
    // another nibble still resolves it (mirrors handle_f). Everything else on a foreign leaf drops HERE, before the h_rx emit:
    // a STATIC H (team_scoped=false), a foreign-team H, and a static receiver (same_team()==false). Every existing team-scoped
    // H is single-leaf (h.leaf_id==_cfg.leaf_id) -> the gate never fires -> s18/s21-s28 byte-identical.
    if (h.leaf_id != _cfg.leaf_id && !(h.team_scoped && same_team(h.team_id))) return;   // foreign-layer (dv:11635)
    if (h.origin == _node_id) return;                      // our own query echoed back (dv:11637)
    MR_EMIT("h_rx", EF_I("origin", h.origin), EF_I("key_hash32", static_cast<int64_t>(h.key_hash32)), EF_I("ttl", h.ttl),
            EF_B("hard", h.hard));  // dv:11638

    // §mobile (2026-07-11): a MOBILE is a LEAF on the static plane — it does NOT participate in a STATIC hash-locate flood:
    // it never ANSWERS (its local id is invisible + home-proxied) and it never RELAYS (re-flooding puts a leaf on the static
    // flood plane + drains its battery; bench: a mobile re-tx'd its home's H). It DOES process a TEAM-scoped locate (the 6.2
    // team plane, where a teammate relays to reach a >1-hop teammate). A static node (is_mobile=false) is unchanged.
    if (_cfg.is_mobile && !h.team_scoped) {
        // §S3 part3 (TX-free overhear): a WANT_PUBKEY H for OUR OWN hash carries the requester's appended key. Cache it
        // BEFORE returning (no answer, no relay, no TX — the home answers on our behalf, Part 2). Covers the sender-in-RF-range
        // case at zero cost; redundant with the Part-2 forward when the home is in range, kept for the home-momentarily-deaf case.
        if (h.key_hash32 == _key_hash32 && h.want_pubkey) (void)cache_want_pubkey_requester(h);
        return;
    }

    // Resolve. SOFT query (default): own-hash OR any cached binding answers ("anyone who knows"). HARD query
    // (verify-on-use, dv §3.7a): resolve ONLY via own-hash — SKIP the cache so it reaches the OWNER for an
    // authoritative correction. A cached binding carries its own confidence (beacon = authoritative/first-hand;
    // snooped hash-bind = claimed/second-hand, Phase C).
    int node_id = -1; bool authoritative = false; bool mobile_proxy = false; uint8_t mobile_epoch = 0; uint8_t mobile_layer = 0;   // §mobile 4a proxy flag + epoch; §5b the home's layer
    const bool same_team = h.team_scoped && this->same_team(h.team_id);   // §mobile-team / §P2-1: a teammate's locate (the mobile IS the endpoint on the team plane) — ONE same_team() definition, leaf-agnostic
    // §team-multihop (spec 2026-07-15 §2): a team-scoped H is a TEAM-plane flood — only same-team members answer/relay it.
    // A static node (team_id==0) or a wrong-team member DROPS it here, BEFORE any answer, forward, or mark_hash_query_seen,
    // so a team locate never rides the static plane (the s24 assertion-2 separation axis). UNCONDITIONAL (NOT #if MR_FEAT_TEAM):
    // a !MR_FEAT_TEAM gateway build has team_id==0 -> same_team false -> it still DROPS any team-scoped H (the co-located-team
    // leak the separation audit caught). same_team is false for every non-team-scoped H (s18-inert: no static H is team_scoped).
    if (h.team_scoped && !same_team) return;
    // §mobile: a REGISTERED mobile is INVISIBLE to the static plane — it SKIPS own-hash resolution (the home proxies) so its
    // LOCAL id never leaks. It DOES answer a same-team locate (the 6.2 team-scoped table routes to its local id). A static
    // node (is_mobile=false) is unchanged. This also suppresses its want_pubkey owner-answer -> the home answers it (Part 2).
    if (h.key_hash32 == _key_hash32 && (!mobile_registered() || same_team)) { node_id = _node_id; authoritative = true; }   // own-hash: resolves either variant (mobile_registered() is false on a static/gateway build -> always resolves)
    else if (!h.hard) {                                                              // HARD skips the cache -> flood to the owner
        // ⚠ ✖ MISSING (register B2's READ-side twin, deliberately NOT fixed here — C1; it is spec §12 / register D2's
        // read-path plane audit): this cache lookup has NO plane test, so a TEAM-scoped H can still be answered out of
        // the STATIC `_id_bind` — handing a team querier a STATIC node_id. The WRITE half is closed (a team-plane H
        // answer no longer enters `_id_bind`, see on_hash_bind_response/on_hash_bind_snoop), which removed the only
        // corpus-reachable instance: s24/s25/s26 measurably stopped answering a repeat team locate from a relay's
        // static cache and now let the OWNER answer (+78/+78/+56 events, delivery unchanged). What remains reachable is
        // a genuinely-static binding (from a beacon) answering a team-scoped query — same class, other direction.
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
        mark_hash_query_seen(h.origin, h.key_hash32, h.hard, h.want_pubkey, h.team_scoped);   // §T6/B: keyed by the H's own plane. mark BEFORE replying so a re-flood doesn't double-answer (dv:11647)
        // §F-TR-2: the ANSWER binding for a TEAM-scoped own-hash resolve must name our TEAM identity (team_local_id), NOT the
        // host-assigned static node_id. A DUAL (registered) member's _node_id is its static host id (e.g. 254); answering a
        // team locate with that sends the querier to _rt_team looking for a static id that has no team route -> no_route/giveup.
        // Owner-detection below still keys on node_id==_node_id (unchanged); only the emitted/answered id is substituted, and
        // mirrors the team H-flood origin (:~1207) + team RTS src. OFF-GRID team & static: team_local_id()==_node_id (or not
        // same_team / not registered) -> answer_node_id==node_id -> byte-identical (s18/s22-s26 unshifted; audited).
        uint8_t answer_node_id = static_cast<uint8_t>(node_id);
        if (node_id == _node_id && same_team && mobile_registered() && team_local_id() != 0)
            answer_node_id = team_local_id();
        MR_EMIT("h_resolved", EF_I("origin", h.origin), EF_I("key_hash32", static_cast<int64_t>(h.key_hash32)),
                EF_I("node", answer_node_id), EF_I("target_layer", _cfg.leaf_id), EF_B("authoritative", authoritative));  // dv:11649
        if (h.want_pubkey && mobile_proxy) {                        // §Part 2 Fix 7: the HOME answers WANT_PUBKEY on behalf of its LIVE mobile (Option 1 — the home carries the key). MUST precede the owner branch: a live proxy has node_id==_node_id, so the owner branch would otherwise leak the HOME's own key under the mobile's hash.
            const uint8_t* mk = host_mobile_ed_pub(h.key_hash32);  // the mobile's cached ed_pub (Fix 6 push), iff a LIVE direct proxy has_pubkey (a redirect carries no local key)
            if (mk) send_mobile_pubkey_answer(h.origin, mobile_layer, static_cast<uint8_t>(node_id), h.key_hash32, mobile_epoch, mk);
            // no cached key (the push hasn't arrived yet, or this is a redirect) -> stay SILENT on WANT_PUBKEY: the locate times out and the sender's reqpubkey retries (the push races registration). The flood is still suppressed by the return below.
            // §S3 part2 (D3 eager): the requester needs OUR mobile's key (above) AND our mobile needs the REQUESTER's key to
            // DECRYPT its future sealed DM. Cache the requester here (the owner branch's mutual-exchange, which this proxy
            // branch replaces for a hosted mobile) + FORWARD it to the mobile as a 1-hop last-mile so the mobile can e2e_open.
            const uint32_t rq = cache_want_pubkey_requester(h);
            if (rq != 0) forward_requester_key_to_mobile(h.key_hash32, h.requester_ed_pub, reinterpret_cast<const char*>(h.name), h.name_len);
        } else if (h.want_pubkey && node_id == _node_id && _crypto_ready) {   // §6 + review#1: ONLY the OWNER (own-hash) answers WANT_PUBKEY
            // §2 MUTUAL: cache the requester's key + id_bind (from the H's appended ed_pub) BEFORE answering, so we can
            // both DECRYPT and ADDRESS its future sealed DMs -> the exchange provisions BOTH directions in one round.
            // requester_hash = requester_ed_pub[:4] LE (self-consistent: peer_key_set derives/checks the same hash).
            const uint32_t requester_hash = key_hash32_of(h.requester_ed_pub);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
            bool req_zero = true; for (int i = 0; i < 32; ++i) if (h.requester_ed_pub[i]) { req_zero = false; break; }
            if (!req_zero && requester_hash != 0                       // review#15: never cache a zero/degenerate requester key
                && peer_key_set(requester_hash, h.requester_ed_pub, PeerKeyConf::authoritative,
                                reinterpret_cast<const char*>(h.name), h.name_len)) {   // §name: cache hash->name too (WITH the pubkey), symmetric to the TYPE-5 answer
                // review#3: the ADDRESSING half (seal-back w/o waiting for a beacon). §mobile: a MOBILE/TEAM requester's
                // origin (h.mobile_req, or a team_scoped locate) is a LOCAL id -> do NOT id_bind it into the global plane;
                // the KEY half (peer_key_set, hash-keyed) still runs, and the seal-back routes by hash via home / _rt_team.
                // Only a STATIC requester (global id) is id_bound. Closes the WANT_PUBKEY local-id leak (was deferred Finding-2).
                if (!h.team_scoped && !h.mobile_req)
                    id_bind_set(h.origin, requester_hash, IdBindSource::h_query, IdBindConf::authoritative);
                MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(requester_hash)), EF_I("node", h.origin));   // review#11: schema aligned with §7
                push_peer_key_cached(requester_hash);   // review#10: app-notify on device too (§S6: + the cached name)
            }
            send_hash_bind_pubkey_response(h.origin, _cfg.leaf_id, answer_node_id, _ed_pub, h.mobile_req ? requester_hash : 0, /*team_scoped=*/h.team_scoped);   // §mobile: a MOBILE requester -> DST_HASH=the mobile so the answer routes to origin (=the mobile's home) + last-miles; §F-TR-2: team-scoped own answer names team_local_id + routes on the team plane
        } else
            send_hash_bind_response(h.origin, mobile_proxy ? mobile_layer : _cfg.leaf_id, answer_node_id, h.key_hash32, authoritative, mobile_proxy, mobile_epoch, /*team_scoped=*/h.team_scoped);   // §5b: a mobile answer carries the HOME's full layer_id (not the proxy's leaf); §F-TR-2: team-scoped own answer names team_local_id (not the static host id) + routes on the team plane (_rt_team + team RREQ)
        return;                                            // SUPPRESS — the whole point: the flood stops here
    }

    // FORWARD path (dv:11655): we don't know it (or it's a HARD query and we're not the owner) -> re-broadcast
    // once, deduped per variant, until TTL runs out.
    if (hash_query_seen_recently(h.origin, h.key_hash32, h.hard, h.want_pubkey, h.team_scoped)) return;   // flood dedup (dv:11656) — §2: WANT_PUBKEY is its own variant
    mark_hash_query_seen(h.origin, h.key_hash32, h.hard, h.want_pubkey, h.team_scoped);    // (dv:11657)
    if (h.ttl == 0) return;                                         // TTL exhausted (dv:11658)
    // L7: h.ttl is an unauthenticated wire byte — a forged ttl=255 would re-flood with a 255-hop horizon. Clamp to
    // flood_hop_max so the re-flooded ttl can't exceed the mesh diameter (dedup already bounds re-broadcasts per node).
    const uint8_t fwd_ttl = (h.ttl > protocol::flood_hop_max ? protocol::flood_hop_max : h.ttl) - 1;
    MR_EMIT("h_forward", EF_I("origin", h.origin), EF_I("key_hash32", static_cast<int64_t>(h.key_hash32)),
            EF_I("ttl", static_cast<int64_t>(fwd_ttl)), EF_B("hard", h.hard));  // dv:11661
    h_in fwd{};
    fwd.leaf_id = _cfg.leaf_id; fwd.origin = h.origin; fwd.key_hash32 = h.key_hash32;
    fwd.ttl = fwd_ttl; fwd.hard = h.hard;                          // preserve the variant across forwards
    fwd.want_pubkey = h.want_pubkey;   // R4: PRESERVE the E2E pubkey-request flag so a multi-hop WANT_PUBKEY reaches the owner
    if (h.want_pubkey) for (int i = 0; i < 32; ++i) fwd.requester_ed_pub[i] = h.requester_ed_pub[i];   // §2: carry the requester's pubkey across the forward
    if (h.want_pubkey) { fwd.name_len = h.name_len; for (uint8_t i = 0; i < h.name_len; ++i) fwd.name[i] = h.name[i]; }   // §name: carry the requester's name across the forward (WITH the pubkey)
    fwd.team_scoped = h.team_scoped; fwd.team_id = h.team_id;   // §mobile-team Fix 1b: PRESERVE team scope across a multi-hop forward, else a same_team mobile >1 hop away sees a plain query + stays silent (Fix 1). Inert today (no originator sets team_scoped -> byte-identical) until 6.2 turns it on.
    fwd.mobile_req = h.mobile_req;   // §mobile: PRESERVE the "origin is a LOCAL id" mark across a multi-hop forward so the OWNER (>1 hop away) still skips the id_bind.
    uint8_t buf[8 + 32 + 4 + 1 + 32];  // §2: WANT_PUBKEY H is 40 B; §mobile-team: +4 B team_id; §name: +1+name_len (max 33) -> up to 77 B
    const size_t n = pack_h(fwd, std::span<uint8_t>(buf, sizeof(buf)));
    // §F-XL-1: DO NOT re-tx immediately — sibling relays that heard this same flood copy would key up at the
    // identical ms (deterministic collision, no capture, at any common/downstream receiver). Stash the built frame
    // + fire after a small random delay (kHForwardTimerId+slot); the existing LBT then defers the later sibling.
    // A small round-robin RING so a concurrent flood for a DIFFERENT hash doesn't clobber this pending one.
    // §3-B.5: the stash ritual (fit guard / cursor / copy / jitter draw / timer arm) lives in jittered_tx_stash.h.
    jtx_ring_arm(_hal, _h_forward_stash, _h_forward_rr, buf, n,
                 protocol::h_forward_jitter_min_ms, protocol::h_forward_jitter_max_ms, kHForwardTimerId);
}

// §F-XL-1: fire a de-stormed (jittered) h_forward from its ring slot. The frame is self-contained (leaf_id packed
// in), so it tx's regardless of the currently-active layer. A slot with len==0 has already fired / never armed.
void Node::h_forward_fire(uint8_t slot) {
    if (HForwardStash* st = jtx_ring_armed(_h_forward_stash, slot)) jtx_fire(st->buf, st->len);
}

// =============================================================================
// Phase B — the hash-bind RESPONSE (a routed DATA with the H_ANSWER inner) + the
// origin's receive (parse here; cache + drain the parked send-by-hash is Phase C).
// =============================================================================

// Enqueue a normal DATA carrying the H_ANSWER inner, addressed to the H-query origin; it routes home
// hop-by-hop on the existing rt[origin] (the H flood lays no reverse path). AUTHORITATIVE = the resolver
// answered as the owner (matches_self), not from a cached binding. (Lua send_hash_bind_response dv:5877.)
void Node::send_hash_bind_response(uint8_t to_origin, uint8_t target_layer, uint8_t node_id,
                                   uint32_t key_hash32, bool authoritative, bool mobile_proxy, uint8_t epoch, bool team_scoped) {
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
    // §F-TR-2: a TEAM-scoped H answer routes home on the TEAM plane. AUTO dispatches by is_team_peer(to_origin), which is
    // FALSE when the origin (the querier's team_local_id) has not yet been learned as a team peer — AUTO then falls to the
    // static _rt, RREQs the team id on the STATIC plane, and the dual-member owner (whose static id != team id) never
    // self-answers that RREQ -> no_route/giveup. Forcing TEAM routes via _rt_team + a TEAM RREQ (owner self-answers on
    // team_local_id). Byte-identical where a team route already exists (AUTO already picked _rt_team); s22-s26 audited.
    item.plane = team_scoped ? Plane::TEAM : Plane::AUTO;
    item.type  = mobile_proxy ? DATA_TYPE_MOBILE_H_ANSWER
               : authoritative ? DATA_TYPE_AUTHORITATIVE_H_ANSWER : DATA_TYPE_H_ANSWER;
    for (size_t i = 0; i < n; ++i) item.inner[i] = inner[i];
    item.inner_len = static_cast<uint8_t>(n);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_EMIT("hash_bind_response_enqueued", EF_I("to", to_origin), EF_I("node", node_id),
            EF_I("key_hash32", static_cast<int64_t>(key_hash32)), EF_B("authoritative", authoritative));  // dv:5897
    become_free();                                               // kick the queue to route the answer home
}

// E2E §6: the owner answers a WANT_PUBKEY query with its ed_pub — a routed DATA TYPE 5 (cleartext; cache-on-pass).
// Wave 2: built as a STANDARD DM (enqueue_data -> [dst_hash?][origin][body]) not a raw inner, so a MOBILE requester's
// answer can carry dst_hash=the mobile -> route to origin (=the mobile's HOME) -> the home last-mile-forwards it
// (do_post_ack). dst_hash==0 (a static requester) -> a plain [origin][body] DM to to_origin. The consumer reads ui->body.
void Node::send_hash_bind_pubkey_response(uint8_t to_origin, uint8_t target_layer, uint8_t node_id, const uint8_t ed_pub[32], uint32_t dst_hash, bool team_scoped) {
    hash_bind_pubkey_inner hb{}; hb.target_layer = target_layer; hb.node_id = node_id;
    for (int i = 0; i < 32; ++i) hb.ed_pub[i] = ed_pub[i];
    uint8_t body[34 + 1 + 32];                                     // the pubkey answer BODY; enqueue_data wraps it in the standard inner
    size_t n = pack_hash_bind_pubkey_inner(hb, std::span<uint8_t>(body, 34));
    if (n == 0) return;
    const uint8_t nlen = effective_name(reinterpret_cast<char*>(body + n + 1), 32);   // §1.3: ‖ [name_len][name] (OUR name; the owner answers its own key)
    body[n] = nlen; n += 1u + nlen;
    (void)enqueue_data(to_origin, body, static_cast<uint8_t>(n), /*flags=*/0, "hash_bind_pubkey_response_enqueued",
                       /*app_dm=*/false, DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY, CryptIntent::off, /*override_dst_hash=*/dst_hash,
                       /*override_source_hash=*/0, /*addr_len=*/0, /*plane=*/team_scoped ? Plane::TEAM : Plane::AUTO);   // §F-TR-2: team-scoped WANT_PUBKEY answer routes on _rt_team
}

// E2E §6: a DATA TYPE 5 (delivered to us OR relayed-through) -> cache the owner's ed_pub AUTHORITATIVE. The pubkey is
// immutable + hash-verifiable, so cache-on-pass can't decay it (peer_key_set re-verifies ed_pub[:4] == key_hash32).
void Node::on_hash_bind_pubkey(const uint8_t* inner, uint8_t inner_len) {
    auto o = parse_hash_bind_pubkey_inner(std::span<const uint8_t>(inner, inner_len));
    if (!o) return;
    const uint32_t kh = key_hash32_of(o->ed_pub);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
    const char* nm = nullptr; uint8_t nlen = 0;                            // §1.3: appended [name_len][name] after the 34-B base
    if (inner_len > 34) { nlen = inner[34]; if (nlen > 32) nlen = 32; if (35u + nlen <= inner_len) nm = reinterpret_cast<const char*>(inner + 35); else nlen = 0; }
    if (peer_key_set(kh, o->ed_pub, PeerKeyConf::authoritative, nm, nlen)) {
        MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(kh)), EF_I("node", o->node_id));
        push_peer_key_cached(kh);   // §7: app prompts "secure send ready — resend" (§S6: + the cached name)
    }
    // [#38b park/drain follow-up: on a fresh authoritative insert, drain parked CRYPTED sends to kh -> seal + send.]
}

// The querier received a DATA whose inner is a hash-bind answer (handle_data routed it here off the
// H_ANSWER payload-flag). Phase B: parse + emit. Phase C will id_bind_set(h_query, conf) + drain the
// parked send-by-hash. DELIBERATELY does NOT deliver as a DM (it is routing/identity info, not user content).
void Node::on_hash_bind_response(const uint8_t* inner, uint8_t inner_len, bool authoritative, bool team_plane) {
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, inner_len));
    if (!hb) return;
    // C.1 destination consume: WE asked -> source h_query; the answer's AUTHORITATIVE bit (now the frame TYPE,
    // passed in) carries the confidence (an owner answer is authoritative, a cache-relayed soft answer is
    // claimed -> verify-on-use).
    // ★★ §hashbind-plane (2026-07-31, register B2): NOT when the answer rode the TEAM plane. `hb->node_id` is then a
    // TEAM LOCAL id (handle_h answers a team-scoped locate with team_local_id, node_hashlocate.cpp F-TR-2), and
    // _id_bind is the STATIC node_id-indexed plane -> writing it is the I2 breach s24 asserts a static bystander never
    // commits. Same rule and same shape as the two shipped gates in this file (the WANT_PUBKEY owner branch and
    // cache_want_pubkey_requester, both `!h.team_scoped && !h.mobile_req`), which also just SKIP the write.
    // ⚠ NOT redirected into team_key_set, and that is a deliberate refusal — see the on_hash_bind_snoop note below.
    // Nothing else is lost: drain_parked_sends (right below) takes `hb->node_id` DIRECTLY, so the parked team send
    // still flies on this answer; only the static-plane CACHE of a team id is withheld.
    if (!team_plane)
        id_bind_set(hb->node_id, hb->key_hash32, IdBindSource::h_query,
                    authoritative ? IdBindConf::authoritative : IdBindConf::claimed);
    // §mobile 4a: the 3c key_hash_of_id heuristic is GONE — a mobile proxy now carries the distinct DATA_TYPE_MOBILE_H_ANSWER
    // (handled in on_mobile_hash_bind_response), so a plain H_ANSWER for a hash we don't own is NEVER treated as a mobile proxy.
    // ★ §hashbind-plane: outside the gate deliberately — this records the ANSWER ARRIVING (and the drain below still
    // runs off it); the STORE record is the `id_bind_set` emit above, whose absence is the plane refusal.
    MR_EMIT("hash_bind_rx", EF_I("node", hb->node_id), EF_I("key_hash32", static_cast<int64_t>(hb->key_hash32)),
            EF_I("target_layer", hb->target_layer), EF_B("authoritative", authoritative));
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
    uint8_t inner[7 + 32 + 1 + 32];
    const size_t n = pack_hash_bind_inner(hb, std::span<uint8_t>(inner, 7), /*mobile=*/true);   // 7 B mobile variant (home routing + epoch)
    if (n == 0) return;
    for (int i = 0; i < 32; ++i) inner[n + i] = ed_pub[i];                                      // ‖ the mobile's ed_pub
    uint8_t nlen = 0;                                                                            // §1.3: ‖ the hosted MOBILE's name (from _mobile_reg, pushed with the key)
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == key_hash32) { nlen = _active->_mobile_reg[i].name_len > 32 ? 32 : _active->_mobile_reg[i].name_len;
            for (uint8_t b = 0; b < nlen; ++b) inner[n + 32 + 1 + b] = static_cast<uint8_t>(_active->_mobile_reg[i].name[b]); break; }
    inner[n + 32] = nlen;
    TxItem item{};
    item.origin = _node_id; item.dst = to_origin;
    item.ctr = next_ctr(to_origin); item.ctr_lo = static_cast<uint8_t>(item.ctr & 0x0F);
    item.flags = 0; item.type = DATA_TYPE_MOBILE_H_ANSWER_PUBKEY;
    const size_t total = n + 32 + 1u + nlen;
    for (size_t i = 0; i < total; ++i) item.inner[i] = inner[i];
    item.inner_len = static_cast<uint8_t>(total);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_EMIT("mobile_pubkey_answer_tx", EF_I("to", to_origin), EF_I("home", home_id));
    become_free();
}

// §S3 part2/3: validate + cache a WANT_PUBKEY H's appended requester key, mirroring the owner branch's peer_key_set + name +
// the mobile/team id_bind gate. Self-consistent by construction: peer_key_set derives + checks ed_pub[:4]==requester_hash (so
// a mismatched/degenerate key is REFUSED there). Fires the peer_key_cached telemetry + push (the app's "secure send ready"
// surface). Returns the requester hash on a successful cache, 0 on reject. Only the NEW paths call this (the home proxy-answer
// branch + the mobile TX-free overhear cache) — the owner branch keeps its inline logic byte-identical (s18-inert).
uint32_t Node::cache_want_pubkey_requester(const h_out& h) {
    const uint32_t requester_hash = key_hash32_of(h.requester_ed_pub);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
    bool req_zero = true; for (int i = 0; i < 32; ++i) if (h.requester_ed_pub[i]) { req_zero = false; break; }
    if (req_zero || requester_hash == 0) return 0;                 // never cache a zero/degenerate requester key
    if (!peer_key_set(requester_hash, h.requester_ed_pub, PeerKeyConf::authoritative,
                      reinterpret_cast<const char*>(h.name), h.name_len)) return 0;   // ed_pub[:4]!=hash -> refused
    // The ADDRESSING half: a MOBILE/TEAM requester's origin is a LOCAL id -> do NOT id_bind it into the global plane (the KEY
    // half is hash-keyed + routes by hash). Only a STATIC requester (global id) is id_bound. Mirrors the owner branch's gate.
    if (!h.team_scoped && !h.mobile_req)
        id_bind_set(h.origin, requester_hash, IdBindSource::h_query, IdBindConf::authoritative);
    MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(requester_hash)), EF_I("node", h.origin));
    push_peer_key_cached(requester_hash);
    return requester_hash;
}

// §S3 part2: FORWARD a WANT_PUBKEY requester's key to a hosted mobile as a 1-hop last-mile DM (DATA_TYPE_MOBILE_KEY_FORWARD,
// addr_len=1, plaintext). Body = [requester_ed_pub 32][name_len u8][name <=32]. Dedup: skip if we ALREADY forwarded this same
// requester to this mobile last (per-entry last_key_fwd_hash32) — the cheapest guard against a reqpubkey-retry re-forwarding.
void Node::forward_requester_key_to_mobile(uint32_t mobile_hash, const uint8_t requester_ed_pub[32],
                                           const char* name, uint8_t name_len) {
    const uint32_t rq = key_hash32_of(requester_ed_pub);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == mobile_hash && _active->_mobile_reg[i].redirect_home_id == 0) {
            if (_active->_mobile_reg[i].last_key_fwd_hash32 == rq) return;   // already forwarded this requester -> dedup (no re-forward)
            _active->_mobile_reg[i].last_key_fwd_hash32 = rq;
            const uint8_t local_id = _active->_mobile_reg[i].mobile_local_id;
            uint8_t nlen = name_len > 32 ? 32 : name_len;
            uint8_t body[32 + 1 + 32];
            for (int b = 0; b < 32; ++b) body[b] = requester_ed_pub[b];
            body[32] = nlen;
            for (uint8_t b = 0; b < nlen; ++b) body[33 + b] = static_cast<uint8_t>(name[b]);
            (void)enqueue_data(local_id, body, static_cast<uint8_t>(33 + nlen), /*flags=*/0, "mobile_key_forward",
                               /*app_dm=*/false, DATA_TYPE_MOBILE_KEY_FORWARD, CryptIntent::off,
                               /*override_dst_hash=*/0, /*override_source_hash=*/0, /*addr_len=*/1);
            MR_EMIT("mobile_key_forward_tx", EF_I("local", local_id), EF_I("req", static_cast<int64_t>(rq)));
            return;
        }
}

// §S3 part2 (mobile side): the home forwarded a WANT_PUBKEY requester's key to us. Cache it (self-consistency-checked via
// peer_key_set: ed_pub[:4] must equal the derived hash) + name, then fire peer_key_cached (the app's "secure send ready"
// surface). Closes the recipient-side decrypt gap: we can now e2e_open the requester's sealed DM. body = [ed_pub 32][name_len][name].
void Node::on_mobile_key_forward(const uint8_t* body, uint8_t len) {
    if (len < 33) return;                                          // need ed_pub[32] + name_len
    const uint8_t* ed = body;
    const uint32_t rq = key_hash32_of(ed);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
    bool zero = true; for (int i = 0; i < 32; ++i) if (ed[i]) { zero = false; break; }
    if (zero || rq == 0) return;                                   // degenerate key -> reject
    uint8_t nlen = body[32];
    if (nlen > 32) nlen = 32;
    if (static_cast<size_t>(33) + nlen > len) nlen = static_cast<uint8_t>(len - 33);   // clamp a claimed len past the frame
    if (!peer_key_set(rq, ed, PeerKeyConf::authoritative, reinterpret_cast<const char*>(body + 33), nlen)) return;   // ed_pub[:4]!=hash -> reject
    MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(rq)), EF_I("node", 0));
    push_peer_key_cached(rq);
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
    const uint32_t kh = key_hash32_of(ed);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
    const char* nm = nullptr; uint8_t nlen = 0;                            // §1.3: appended [name_len][name] after the 39-B base
    if (inner_len > 39) { nlen = inner[39]; if (nlen > 32) nlen = 32; if (40u + nlen <= inner_len) nm = reinterpret_cast<const char*>(inner + 40); else nlen = 0; }
    if (kh == hb->key_hash32 && peer_key_set(kh, ed, PeerKeyConf::authoritative, nm, nlen)) {   // the key MUST hash to M (self-consistent) — never id_bind the LOCAL id
        MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(kh)), EF_I("node", hb->node_id));
        push_peer_key_cached(kh);   // §7: app prompts "secure send ready — resend" (§S6: + the cached name)
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
// ★★ §hashbind-plane (2026-07-31, register B2) — WHY THE TEAM BINDING IS DROPPED RATHER THAN RE-HOMED INTO
// `team_key_set`. The dispatch brief proposed routing it to the team map (whose own comment says team-scoped bindings
// belong there), and that was measured and REFUSED, three reasons, all at source:
//   (1) `_team_keys` has NO confidence dimension, and this path ingests CLAIMED bindings (a cache-relayed
//       DATA_TYPE_H_ANSWER, and every snoop is second-hand by construction). `_id_bind` keeps claimed-vs-authoritative
//       and send_by_hash refuses to use a claimed row (node_hashlocate.cpp `conf == authoritative`); team_key_of_id /
//       team_id_of_key have no such test, so re-homing would UPGRADE an unverified observation to a trusted one — and
//       one of its consumers is DST_HASH derivation for a SEALED team DM (node_mac.cpp).
//   (2) It feeds the team-DAD L2a mediation: node_beacon.cpp compares team_key_of_id(b.src) against the beacon's own
//       key and sends a mediated DENY on a mismatch. Seeding that comparator from unauthenticated transit traffic
//       manufactures spurious DENYs against legitimate teammates — the exact hazard node.cpp already warns about for
//       stale `_team_keys` rows.
//   (3) `_team_keys` is a 16-slot evict-OLDEST LRU whose documented feed is the BEACON ("cache a same-team peer's
//       key_hash32 (from its beacon)"), and its `last_seen_ms` means "heard now". Cache-on-pass would both fake that
//       liveness and let transit traffic evict genuine beacon rows.
// ⇒ the correct fix is the one the two shipped sibling gates already make: DON'T write the wrong plane. This does NOT
// contradict the address-book design (2026-07-29 §2.5), which forbids the `_id_bind` write and fixes `hashof` with a
// VIEW over both maps, "never by a write". ✖ MISSING, stated so it is not mistaken for done: a team member still has
// no hash->team_local_id cache from an H answer, so a repeat `send -t 0x<hash>` to an unheard teammate re-floods the
// locate instead of resolving from cache. That is a FEATURE (a team-plane bind store with its own confidence field),
// not this fix, and it needs the trust question in (1) answered first.
void Node::on_hash_bind_snoop(const uint8_t* inner, uint8_t inner_len, bool authoritative, bool team_plane) {
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, inner_len));
    if (!hb) return;
    if (!team_plane)   // ★ §hashbind-plane: a TEAM-plane answer carries a TEAM LOCAL id -> never the static _id_bind (§18/C3)
        id_bind_set(hb->node_id, hb->key_hash32, IdBindSource::h_relay,
                    authoritative ? IdBindConf::authoritative : IdBindConf::claimed);
    // ★ The emit stays OUTSIDE the gate on purpose: it records "a relayed answer passed through us" (the frame IS still
    // forwarded), not "we stored it". The store record is the `id_bind_set` emit above, so its ABSENCE beside a
    // `hash_bind_snooped` is exactly the plane refusal — which is how the s24/s25/s26/s28/s34 delta reads in the stream.
    MR_EMIT("hash_bind_snooped", EF_I("node", hb->node_id), EF_I("key_hash32", static_cast<int64_t>(hb->key_hash32)),
            EF_B("authoritative", authoritative));
}

// =============================================================================
// Phase D — the send-by-hash trigger + verify-on-use. Address a DM by the target's
// stable key_hash32: send now if we hold an AUTHORITATIVE binding; else park the DM
// + flood an H query (a SOFT cached binding is HARD-verified before use) and fly it
// when the hash-bind answer resolves the id.
// =============================================================================

// on_command(send) routes here when dst_hash != 0 (the deferred "address by key_hash32"). Returns the DM ctr
// if sent immediately, else 0 (parked/resolving — the ctr is assigned when the binding arrives).
//
// ★★★ §no-auto-reqpubkey — THE ONE PLACE THIS IS WRITTEN DOWN. OWNER-RATIFIED 2026-07-29:
// *"reqpubkey should NOT be issued automatically."* Referenced from every locate site below rather than repeated
// (U1); the sites are tagged `§no-auto-reqpubkey`.
//
// WHAT IT MEANS. Every hash-locate this function fires passes **want_pubkey = false** — FOUR sites, not the three the
// dispatch brief listed: :1026 (team-plane park), :1137 (off-grid team park), :1143 (the global park), and :1282
// (park_reflood_fire — the bounded retry of any of them, which would re-open the hole if it escalated on its own; it
// was missing from the brief's list). So a CRYPTED send to an UNRESOLVED hash fails loud with `no_pubkey` and does **not**
// escalate into a WANT_PUBKEY locate. That is DELIBERATE, not an oversight or an unfinished TODO.
//
// WHY. Auto-escalating would silently prefer the **on-air TOFU** path over the **MITM-resistant QR ceremony**, for a
// send the user explicitly marked `-e`. On-air WANT_PUBKEY resolution is NOT MITM-secure (whoever answers first is
// believed, and the cached key is `authoritative`, indistinguishable downstream from a scanned one); a physical scan
// IS the trust ceremony. Failing loud keeps that choice with the OPERATOR instead of making it for them invisibly.
//
// THE REMEDY IS `reqpubkey <hash>` (or a QR import), and it is MUTUAL: the requester's own pubkey rides across every
// forward of the query, so ONE call keys both sides. The consoles say so — the `no_pubkey` refusals name the verb.
//
// ⚠ WHERE IT BITES HARDEST: §team-ch-key T-K3's `team grantkey` ships a **PRIVATE key**, so it is the single worst
// place to quietly downgrade to TOFU. Its refusal path (Node::team_key_grant_send, node.cpp) repeats the rule at the
// site and points the operator at `reqpubkey`.
//
// ★ A future slice that "fixes" this annoyance by auto-resolving is REVERSING A RULING, not improving ergonomics.
// And do not add a config knob to enable auto-resolution — that was ruled out with it.
uint16_t Node::send_by_hash(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt, uint32_t reply_to_hash, uint16_t mobile_ctr, Plane plane, uint8_t type, bool suppress_intro) {
    // §S2 INTRO first-contact attach (D1): at ORIGINATION (type==0 = not a pre-built INTRO; reply_to_hash==0 = not a
    // HOME re-originating for its mobile), a PLAINTEXT hash-addressed send rides as DATA_TYPE_INTRO carrying our
    // pubkey iff intro_attach_prefix says so (no peer_confirmed(dst), a crypto identity exists, cfg on, plaintext,
    // fits). itype/sbody thread through every dispatch below; the delegation wraps enclosed_type=itype. sbuf is
    // static (non-reentrant loop task) so the ~241 B stays off the cramped do_post_ack stack — the home
    // re-originate reaches here with type!=0 so it never allocates the prefix / touches sbuf (byte-identical).
    const uint8_t* sbody = body; uint8_t sblen = body_len; uint8_t itype = type;
    if (type == 0 && reply_to_hash == 0 && !suppress_intro) {   // §D1 `-K`: suppress_intro skips the attach for this one send (a no-op on a sealed send — intro_attach_prefix already returns 0 for want_crypt)
        static uint8_t sbuf[protocol::max_payload_bytes_hard_cap];
        uint8_t pfx[33 + 32];
        const uint8_t pn = intro_attach_prefix(key_hash32, crypt, body_len, pfx, sizeof pfx);
        if (pn && static_cast<size_t>(pn) + body_len <= sizeof sbuf) {
            for (uint8_t i = 0; i < pn; ++i)       sbuf[i]      = pfx[i];
            for (uint8_t i = 0; i < body_len; ++i) sbuf[pn + i] = body[i];
            sbody = sbuf; sblen = static_cast<uint8_t>(pn + body_len); itype = DATA_TYPE_INTRO;
        }
    }
#if MR_FEAT_TEAM
    // §6.4 HARD SPLIT: `send -t 0x<hash>` (TEAM plane) resolves a HEARD teammate from the team-key cache ONLY (beacon-only,
    // NO id_bind / home / H-flood / global fallback). An unheard teammate FAILS LOUD -> the app retries when a beacon arrives.
    if (plane == Plane::TEAM) {
        uint8_t tid = 0;
        if (_cfg.team_id != 0 && team_id_of_key(key_hash32, tid))
            return do_send(tid, sbody, sblen, flags, crypt, /*override_dst_hash=*/0, /*type=*/itype, /*override_source_hash=*/reply_to_hash, Plane::TEAM);
        // §F-TR-1: an UNHEARD teammate (>1 hop away, not in the beacon-only team-key cache) is resolved by a TEAM-SCOPED H
        // flood (emit_hash_query TEAM => team_scoped + origin=team_local_id; the answer routes home via _rt_team) — mirrors
        // the off-grid AUTO team path (:~1146). EXPLICIT `-t` WINS over the home-delegation default: a REGISTERED dual member
        // reaches HERE (the plane check precedes the delegate branch below) and floods the TEAM plane itself — no home. Gated
        // on team_local_id()!=0 (a routable team origin); a team member with no adopted team id still fails loud below.
        if (_cfg.team_id != 0 && team_local_id() != 0) {
            park_send(key_hash32, sbody, sblen, flags, crypt, /*reply_to_hash=*/reply_to_hash, /*mobile_ctr=*/mobile_ctr, /*type=*/itype,
                      /*reflood=*/true, /*reflood_hard=*/false, /*reflood_plane=*/Plane::TEAM);
            emit_hash_query(key_hash32, /*hard=*/false, /*want_pubkey=*/false, Plane::TEAM);   // §no-auto-reqpubkey (see the header note): want_pubkey stays FALSE, owner-ratified 2026-07-29
            return 0;
        }
        MR_EMIT("team_send_unresolved", EF_I("key_hash32", static_cast<int64_t>(key_hash32)));
        push_send_failed(SendFailReason::mobile_no_home, /*dst=*/0, /*ctr=*/0);
        return 0;
    }
#endif
    IdBindConf conf = IdBindConf::claimed;
    const int id = id_bind_find_by_hash(key_hash32, &conf);
    if (id >= 0 && conf == IdBindConf::authoritative) {         // confident binding -> send NOW (a mobile still routes via its home; the reply returns by SOURCE_HASH -> no H-query, no storm)
        const uint16_t ch = do_send(static_cast<uint8_t>(id), sbody, sblen, flags, crypt, /*override_dst_hash=*/0, /*type=*/itype, /*override_source_hash=*/reply_to_hash, plane);   // §8b: thread the per-message crypt intent + Wave 2 plane; §S2: itype threads an auto-attached INTRO
        if (reply_to_hash != 0) deleg_ack_put(reply_to_hash, ch, mobile_ctr);   // §mobile reverse-ack: HOME re-originating for its mobile toward a STATIC target -> map ctr_H->ctr_M keyed by the MOBILE's hash (no-op if mobile_ctr==0)
        return ch;
    }
#if MR_FEAT_TEAM
    // §mobile 6.4: AUTO cascade — try a HEARD teammate (team_key cache -> team_local_id, is_team_peer -> _rt_team) BEFORE the
    // global path. GLOBAL (a plain `send`) SKIPS this so a teammate never shadows the intended global target; TEAM was already
    // handled + returned at the top. Reached only after an id_bind miss (a mobile's hash isn't in id_bind).
    if (plane != Plane::GLOBAL && _cfg.is_mobile && _cfg.team_id != 0) {
        uint8_t tid = 0;
        if (team_id_of_key(key_hash32, tid))
            return do_send(tid, sbody, sblen, flags, crypt, /*override_dst_hash=*/0, /*type=*/itype, /*override_source_hash=*/reply_to_hash, /*plane=*/Plane::TEAM);   // resolved to a teammate -> force the team plane
    }
#endif
    // §mobile delegate (2026-07-11): UNRESOLVED + we are the MOBILE (reply_to_hash==0) -> DO NOT flood an H query (origin=our
    // LOCAL id -> the answer can't route back -> RREQ storm). Hand it to the HOME as DATA_TYPE_MOBILE_SEND (dst=home_id,
    // DST_HASH=target); the home resolves + re-originates on our behalf, and the target's reply routes back via SOURCE_HASH
    // (=our hash, stamped by stamp_origin). The HOME re-originating (reply_to_hash!=0) falls through to the resolve+flood below.
#if MR_FEAT_MOBILE
    if (reply_to_hash == 0 && _cfg.is_mobile && _my_mobile_reg.active) {
        // §team-ch-key T-K3 (C2): a TEAM KEY GRANT cannot be DELEGATED in v1. Both arms below spend the MOBILE_SEND
        // wrapper's SINGLE enclosed-type byte — the sealed arm on DATA_TYPE_SEALED_RELAY, the plaintext arm on `itype`
        // — so a type-19 either loses its TYPE (the home re-originates a plain sealed DM and the recipient files 37
        // raw private-key bytes as inbox TEXT) or, on the plaintext arm, airs the key in the clear. Neither is
        // acceptable, and there is no third slot: giving SEALED_RELAY an inner type byte changes an already-landed
        // frame's body format, which is its own slice. REFUSE loud. team_key_grant_send pre-checks this same shape so
        // the operator gets a directed message; this is the structural backstop for every other caller.
        if (itype == DATA_TYPE_TEAM_KEY_GRANT) {
            MR_EMIT("team_key_grant_refused", EF_I("hash", static_cast<int64_t>(key_hash32)), EF_S("reason", "delegated"));
            push_send_failed(SendFailReason::unsealable, /*dst=*/0, /*ctr=*/0);
            return 0;
        }
        // §S4 delegated SEALED (fixes the §1b-3 TODAY-broken path): seal the body to the target HERE — only the mobile
        // holds the ECDH pair — and wrap the sealed blob under a PLAINTEXT MOBILE_SEND (enclosed_type=SEALED_RELAY). The
        // home re-originates the type-17 relay WITHOUT re-sealing. CRITICAL: the wrapper is PLAINTEXT (CryptIntent::off);
        // the old code passed `crypt` into the wrapper do_send, which sealed the WRAPPER to the target so the home could
        // never read source_hash -> the frame fell through to deliver -> e2e_open_no_key SILENT DROP at the home.
        const bool want_crypt = (crypt == CryptIntent::on) ? true : (crypt == CryptIntent::off) ? false : _cfg.e2e_dm;
        if (want_crypt) {
            uint8_t rbody[protocol::max_payload_bytes_hard_cap]; SealOutcome oc = SealOutcome::ok;
            const uint8_t rn = build_sealed_relay_body(key_hash32, body, body_len, rbody, sizeof rbody, oc);
            if (rn == 0) {                                                   // fail loud (no pubkey / identity / too large) — NEVER cleartext
                MR_EMIT("e2e_no_pubkey", EF_I("hash", static_cast<int64_t>(key_hash32)), EF_I("oc", static_cast<int>(oc)));
                push_send_failed((oc == SealOutcome::no_pubkey)   ? SendFailReason::no_pubkey
                               : (oc == SealOutcome::no_identity) ? SendFailReason::no_identity
                                                                  : SendFailReason::too_large,
                                 /*dst=*/0, /*ctr=*/0);
                return 0;
            }
            uint8_t wbody[protocol::max_payload_bytes_hard_cap];
            wbody[0] = DATA_TYPE_SEALED_RELAY;
            for (uint8_t i = 0; i < rn; ++i) wbody[1 + i] = rbody[i];
            return do_send(_my_mobile_reg.home_id, wbody, static_cast<uint8_t>(rn + 1),
                           static_cast<uint8_t>(flags | DATA_FLAG_MS_ENCLOSED_TYPE), CryptIntent::off,
                           /*override_dst_hash=*/key_hash32, /*type=*/DATA_TYPE_MOBILE_SEND);
        }
        if (itype != 0) {
            // §S2 same-layer delegated INTRO (spec §3b): the SAME-LAYER MOBILE_SEND wrapper has no enclosed-type byte,
            // so mark it with DATA_FLAG_MS_ENCLOSED_TYPE + prefix the wrapper body with the enclosed TYPE. The home
            // strips both and re-originates with that TYPE (send_by_hash type=itype). sbody already holds the INTRO prefix.
            uint8_t wbody[protocol::max_payload_bytes_hard_cap];
            wbody[0] = itype;
            for (uint8_t i = 0; i < sblen; ++i) wbody[1 + i] = sbody[i];
            return do_send(_my_mobile_reg.home_id, wbody, static_cast<uint8_t>(sblen + 1),
                           static_cast<uint8_t>(flags | DATA_FLAG_MS_ENCLOSED_TYPE), crypt,
                           /*override_dst_hash=*/key_hash32, /*type=*/DATA_TYPE_MOBILE_SEND);
        }
        return do_send(_my_mobile_reg.home_id, sbody, sblen, flags, crypt, /*override_dst_hash=*/key_hash32, /*type=*/DATA_TYPE_MOBILE_SEND);
    }
    // §mobile: a mobile WE HOST (in our _mobile_reg) is reached by a DIRECT last-mile (addr_len=1 -> its local id), NOT an H
    // query — the home is BOTH the querier and the proxy, so a flood deadlocks (the registered mobile suppresses its own-hash
    // H answer, node_hashlocate.cpp handle_h). Mirrors do_post_ack's forwarded last-mile. redirect_home_id==0 = a LIVE local
    // hosting (a migrated mobile falls through to the mobile_home_find redirect below). Gated on _mobile_reg_n -> non-host byte-identical.
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == key_hash32 && _active->_mobile_reg[i].redirect_home_id == 0) {
            const uint16_t lch = enqueue_data(_active->_mobile_reg[i].mobile_local_id, sbody, sblen, flags, "tx_enqueue", /*app_dm=*/true,
                                              /*type=*/itype, crypt, /*override_dst_hash=*/0, /*override_source_hash=*/reply_to_hash, /*addr_len=*/1, plane);
            // ★ §xl-deleg-ack: the THIRD site that stamped the mobile's SOURCE_HASH without mapping ctr_H->ctr_M. Reached
            // when a home hosts BOTH the delegating mobile and the target: the target acks to us with DST_HASH = M, and the
            // hosted-mobile last-mile fork's deleg_ack_translate missed, so M received the HOME's ctr. Same one-line shape.
            if (reply_to_hash != 0) deleg_ack_put(reply_to_hash, lch, mobile_ctr);
            return lch;
        }
#endif
    uint8_t home_layer = 0;
    const int home = mobile_home_find(key_hash32, &home_layer);  // §mobile 3c/5b: a cached mobile -> its home_node (+layer)?
    if (home >= 0) {
        if (home_layer != 0 && home_layer != active_layer_id()) {   // §5b: the home is on ANOTHER layer -> reach it via a gateway (the bridge resolves M on the target leaf, Fix 3)
            // ★ §xl-crypt-intent (2026-07-29): `crypt` is THREADED — this call used to drop it, so a `-e` DM to a mobile
            // whose home sits on another layer aired IN THE CLEAR. send_cross_layer now seals it into a SEALED_RELAY or
            // refuses loud. sbody/sblen are safe to pass instead of body/body_len: intro_attach_prefix returns 0 for a
            // want_crypt send, so under crypt they are identical to body/body_len and itype is 0.
            // ★★ §xl-deleg-ack (BUG FIX 2026-07-30) — WHAT USED TO BE MISSING HERE, now done. This arm dropped BOTH
            // halves of the delegation contract: `override_source_hash` (which send_cross_layer/enqueue_cross_layer
            // simply did not expose on this path) and the `deleg_ack_put` its same-layer sibling below now also makes.
            // So when a HOME re-originated for its hosted MOBILE (reply_to_hash != 0) toward a target whose own home
            // sits on a THIRD layer, the frame aired SOURCE_HASH = the HOME's key. Two failures, both user-visible:
            //   (1) PLAINTEXT — the far recipient's reversed 4e E2E-ack is addressed to the HOME (send_xl_ack sends to
            //       dm.source_hash), so the home CONSUMES it (dst_key_hash32 == our key => the hosted-mobile last-mile
            //       fork does not fire) and the mobile never sees its ack; after the 300 s XL budget its wildcard
            //       pending entry expires into a spurious send_failed{e2e_ack_timeout}.
            //   (2) ★ DELEGATED SEALED — the mobile sealed under ITS OWN hash, so the recipient's directed open runs
            //       peer_key_find/ECDH against the HOME's identity: either no key at all or a wrong shared secret, so
            //       dm_open's Poly1305 tag fails (the sealed-vs-clear source_hash compare is the SECOND line of
            //       defence, not the one that trips). Result: DROPPED, and node_mac_rx.cpp's e2e_open_no_key emit is
            //       sim-only telemetry, so on metal it leaves NO TRACE AT ALL. That is why this ranked first.
            // The fix is symmetric with the sibling: pass the mobile's hash, take the ctr the DM flew with from the
            // return, and map ctr_H -> ctr_M so the returning ack reaches the mobile with the ctr IT is waiting on.
            const uint16_t xch = send_cross_layer(static_cast<uint8_t>(home), key_hash32, home_layer, sbody, sblen, flags, crypt, itype, /*override_source_hash=*/reply_to_hash);
            if (reply_to_hash != 0 && xch != 0) deleg_ack_put(reply_to_hash, xch, mobile_ctr);   // §mobile reverse-ack, XL arm: xch==0 means nothing flew (next_ctr never mints 0) -> never record a phantom ctr_H
            // ⚠ DELIBERATELY still `return 0` (C1): send_by_hash's contract is "the ctr if sent immediately, else 0",
            // and this arm has always answered 0. `xch` is now available, but returning it would change what the
            // console/companion reports for a hash-addressed cross-layer send (and what on_command arms) — a separate
            // behaviour change from the delivery bug, so it is not folded in.
            return 0;
        }
        // same layer (4a path): send to the home carrying the MOBILE's hash (so home forwards, not consumes). NO hard-verify.
        // ★ §xl-deleg-ack: the deleg_ack_put here is NEW TOO — this sibling passed override_source_hash but never mapped
        // ctr_H->ctr_M, so a delegated DM to a target whose home is on OUR layer had the same ack failure as (1) above
        // (the target acks to the home with DST_HASH = M, the last-mile fork rewrites the ctr via
        // deleg_ack_translate, and a MISS forwarded the HOME's ctr to a mobile awaiting its own). The XL-CRYPT note that
        // sat here claimed this line already called deleg_ack_put; it did not — corrected per V1 while fixing it.
        const uint16_t hch = do_send(static_cast<uint8_t>(home), sbody, sblen, flags, crypt, /*override_dst_hash=*/key_hash32, /*type=*/itype, /*override_source_hash=*/reply_to_hash);
        if (reply_to_hash != 0) deleg_ack_put(reply_to_hash, hch, mobile_ctr);   // §mobile reverse-ack: same shape as the id_bind arm above (no-op if mobile_ctr==0)
        return hch;
    }
    // SOFT cached binding -> HARD verify-on-use (reach the owner for a correction); UNKNOWN -> SOFT flood. (The HOME re-originating
    // for its mobile floods as ITSELF, origin=home_id -> the answer routes back; the parked send keeps the mobile's reply hash.)
#if MR_FEAT_TEAM
    // §team-multihop (spec 2026-07-15 Plane 1): an off-grid team member with an UNHEARD teammate (not in the team-key cache
    // above) RESOLVES it via a TEAM-SCOPED hash-locate — restore what 98f71dd did before this became a fail-loud. park + a
    // team-scoped H flood (origin=team_local_id, routes back via _rt_team); same-team members relay it multi-hop (handle_h,
    // team-scope preserved), the owner answers, and the parked send delivers on the returned binding. Forcing Plane::TEAM
    // (vs the AUTO param) guarantees the team scope + the team_local_id origin. (reply_to_hash==0 = our own origination; a
    // HOME re-originating for its mobile falls through to the global flood below.)
    if (reply_to_hash == 0 && _cfg.is_mobile && _cfg.team_id != 0 && !mobile_registered()) {
        park_send(key_hash32, sbody, sblen, flags, crypt, /*reply_to_hash=*/reply_to_hash, /*mobile_ctr=*/mobile_ctr, /*type=*/itype,
                  /*reflood=*/true, /*reflood_hard=*/false, /*reflood_plane=*/Plane::TEAM);   // §F-SL-1: a quiet-net team flood miss re-tries before giveup
        emit_hash_query(key_hash32, /*hard=*/false, /*want_pubkey=*/false, Plane::TEAM);   // §no-auto-reqpubkey (see the header note)
        return 0;
    }
#endif
    park_send(key_hash32, sbody, sblen, flags, crypt, /*reply_to_hash=*/reply_to_hash, /*mobile_ctr=*/mobile_ctr, /*type=*/itype,
              /*reflood=*/true, /*reflood_hard=*/(id >= 0), /*reflood_plane=*/plane);   // §F-SL-1: bounded jittered retry so a re-homed contact re-resolves in a quiet net
    emit_hash_query(key_hash32, /*hard=*/(id >= 0), /*want_pubkey=*/false, plane);   // Wave 2: GLOBAL flood is NOT team-scoped; AUTO keeps today's behavior. §no-auto-reqpubkey (see the header note): a CRYPTED send to an unresolved hash fails loud with no_pubkey — it does NOT escalate to WANT_PUBKEY
    return 0;
}

// §mobile reverse-ack (delegated) ctr map. A home re-originates a hosted mobile's delegated send under its OWN ctr
// (ctr_H); the target's E2E-ack (for ctr_H) comes home, and this map recovers the mobile's original ctr (ctr_M) so the
// last-miled ack matches what the mobile awaits. The TTL keeps a stale entry from mistranslating a much-later ack to the
// same target that happens to reuse ctr_H.
static constexpr uint64_t kDelegAckTtlMs = 180000;   // 3 min — well past the e2e-ack round trip

void Node::deleg_ack_put(uint32_t mobile_hash, uint16_t ctr_h, uint16_t ctr_m) {
    if (ctr_m == 0 || mobile_hash == 0) return;                // 0 = not a delegated send (guard)
    const uint64_t now = _hal.now();
    uint8_t pick = 0; uint64_t oldest = ~0ull;                 // reuse a free/expired slot, else evict the OLDEST
    for (uint8_t i = 0; i < kDelegAckCap; ++i) {
        DelegAck& e = _deleg_acks[i];
        if (!e.valid || (now - e.ts_ms) > kDelegAckTtlMs) { pick = i; break; }
        if (e.ts_ms < oldest) { oldest = e.ts_ms; pick = i; }
    }
    _deleg_acks[pick] = { mobile_hash, ctr_h, ctr_m, now, true };
    MR_EMIT("deleg_ack_put", EF_I("mobile_hash", static_cast<int64_t>(mobile_hash)), EF_I("ctr_h", ctr_h), EF_I("ctr_m", ctr_m));
}

bool Node::deleg_ack_translate(uint32_t mobile_hash, uint16_t acked_ctr, uint16_t& out_mobile_ctr) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < kDelegAckCap; ++i) {
        DelegAck& e = _deleg_acks[i];
        if (!e.valid) continue;
        if ((now - e.ts_ms) > kDelegAckTtlMs) { e.valid = false; continue; }   // prune expired
        if (e.mobile_hash == mobile_hash && e.ctr_h == acked_ctr) {
            out_mobile_ctr = e.ctr_m;
            e.valid = false;                                                    // one-shot: this ack is delivered
            return true;
        }
    }
    return false;
}

// Originate an H flood for key_hash32 (Lua send_hash_query dv:5625). hard = the verify-on-use escalation.
void Node::emit_hash_query(uint32_t key_hash32, bool hard, bool want_pubkey, Plane plane) {
    if (key_hash32 == 0 || key_hash32 == _key_hash32) return;    // nothing to locate (degenerate / it's us)
    if (want_pubkey && !_crypto_ready) {                         // §2: the mutual exchange needs OUR pubkey -> fail loud, no flood
        MR_EMIT("h_want_pubkey_no_identity", EF_I("key_hash32", static_cast<int64_t>(key_hash32)));
        return;
    }
    h_in in{};
    // §P2-1: for a TEAM-scoped H (team_q below) in.leaf_id is ADVISORY — the receiver's handle_h skips the leaf gate for a
    // team-scoped frame (membership is team_id), so a mixed-leaf team resolves across nibbles. We still stamp our own leaf so
    // a same-leaf teammate's frame is unremarkable and any static overhearer sees a well-formed nibble. A STATIC H's leaf_id
    // remains load-bearing (the receiver leaf-gates it).
    in.leaf_id = _cfg.leaf_id; in.origin = _node_id; in.key_hash32 = key_hash32;
    in.ttl = protocol::hash_query_max_ttl; in.hard = hard; in.want_pubkey = want_pubkey;
    in.mobile_req = _cfg.is_mobile;                              // §mobile: OUR origin (in.origin=_node_id) is a mobile/team LOCAL id -> tell the owner NOT to id_bind it (the seal-back caches by hash + routes via home/_rt_team). Static -> 0 -> byte-identical H.
    if (want_pubkey) {
        for (int i = 0; i < 32; ++i) in.requester_ed_pub[i] = _ed_pub[i];   // §2: attach our pubkey so the owner caches us (mutual)
        in.name_len = effective_name(reinterpret_cast<char*>(in.name), 32);   // §name: our name rides WITH our pubkey -> the owner caches hash->name too (mirrors the TYPE-5/12/13 answer frames)
    }
    // §mobile 6.2 Fix 5 / Wave 2: a team-scoped locate answers directly on the team plane. TEAM (explicit `-t`) forces it +
    // sets origin=team_local_id so the owner's answer routes back via _rt_team (a static node_id origin is unroutable on the
    // team plane — the reqpubkey/encrypted-team gap). GLOBAL is NEVER team-scoped. AUTO keeps the pre-split default (a team
    // mobile team-scopes, origin=node_id) for the sim/companion -> byte-identical. team_id==0 (static/lone) -> unset.
    const bool team_q = (plane == Plane::TEAM) || (plane == Plane::AUTO && _cfg.is_mobile && _cfg.team_id != 0);
    if (team_q && _cfg.team_id != 0) { in.team_scoped = true; in.team_id = _cfg.team_id; }
    if (plane == Plane::TEAM && team_local_id() != 0) in.origin = team_local_id();   // team-scoped -> answer routes back on the team plane (is_team_peer(origin) -> _rt_team)
#if MR_FEAT_MOBILE
    // §mobile: a REGISTERED mobile stamps origin=home_id (GLOBAL/non-team) so the owner's answer routes to the HOME, which
    // last-mile-forwards it. The mobile's own node_id is a LOCAL id — unroutable + collision-prone on the static plane.
    if (plane != Plane::TEAM && mobile_registered() && _my_mobile_reg.home_id != 0) in.origin = _my_mobile_reg.home_id;
#endif
    // §mobile step 2: a WANT_PUBKEY answer is a ROUTED DM back to in.origin. If we're a mobile and origin is STILL our own
    // LOCAL node_id — no home overrode it above, and it's not team-scoped (so no team return path) — the owner has NO way
    // back: routing/RREQ for a local id can even resolve a WRONG static node (the user's "F query does not make sense").
    // Fail loud, do NOT flood. (Registered => origin=home_id; TEAM/AUTO-team => team_scoped — both skip this.)
    if (want_pubkey && in.mobile_req && in.origin == _node_id && !in.team_scoped) {
        MR_EMIT("h_want_pubkey_mobile_no_route", EF_I("key_hash32", static_cast<int64_t>(key_hash32)));
        return;
    }
    uint8_t buf[8 + 32 + 4 + 1 + 32];                            // §2: WANT_PUBKEY H = 40 B; §mobile 6.2: +4 B team_id; §name: +1+name_len (max 33) -> a named team_scoped WANT_PUBKEY is up to 77 B
    const size_t n = pack_h(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    // the originate (dv:5625)
    MR_EMIT("h_tx", EF_I("key_hash32", static_cast<int64_t>(key_hash32)), EF_I("ttl", protocol::hash_query_max_ttl), EF_B("hard", hard));
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

void Node::park_send(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt, uint32_t reply_to_hash, uint16_t mobile_ctr, uint8_t type, bool reflood, bool reflood_hard, Plane reflood_plane) {
    if (_parked_sends_n >= protocol::cap_parked_sends) return;   // full -> drop (the app can retry)
    ParkedSend& p = _parked_sends[_parked_sends_n++];
    p = ParkedSend{};                                           // reset a RECYCLED slot: the array is compacted in
    // place (drain/age-out never clear vacated slots), so a slot last used by an L2c redirect would otherwise
    // keep is_redirect=true and mis-route this plain send-by-hash through the redirect branch on drain.
    p.key_hash32 = key_hash32; p.flags = flags; p.parked_at_ms = _hal.now();
    p.reply_to_hash = reply_to_hash;                            // §mobile delegate: the mobile's hash (home re-originating) -> SOURCE_HASH on drain
    p.mobile_ctr = mobile_ctr;                                  // §mobile reverse-ack: the mobile's original ctr -> the drain's ctr_H->ctr_M map
    p.crypt = crypt;                                            // M3: stamp the per-message crypt intent -> drain re-seals CRYPTED (never a silent cleartext downgrade)
    p.type = type;                                             // §S2: a parked INTRO re-originates with its TYPE (+ the already-built key prefix in body) on drain
    // §F-SL-1: schedule the first bounded H re-flood so a QUIET-net single-flood miss self-heals before giveup. The
    // FIRST deadline is a FIXED offset (NO rand draw here): park times already differ across nodes, and drawing at park
    // time would perturb the shared-mt19937 draw ORDER for the rest of the run even when no re-flood ever fires (the sim
    // is draw-order-deterministic) — a phantom behaviour shift. The per-retry jitter is drawn in park_reflood_fire, i.e.
    // ONLY when a re-flood actually happens, so a run with no re-floods is RNG-identical to before this fix.
    if (reflood) {
        p.reflood = true; p.reflood_hard = reflood_hard; p.reflood_plane = reflood_plane;
        p.reflood_at_ms = p.parked_at_ms + protocol::park_reflood_retry_ms;
    }
    // Clamp to the DM body cap (NOT the 241-B inner buffer): drain_parked_sends -> do_send -> enqueue_data
    // writes body at inner[2+i], so a >239 body would overrun inner[]. on_command already rejects oversize
    // (err_too_large) — this is defense-in-depth so a parked body can never exceed the deliverable size.
    p.body_len = (body_len > protocol::dm_max_body_bytes) ? protocol::dm_max_body_bytes : body_len;
    for (uint8_t i = 0; i < p.body_len; ++i) p.body[i] = body[i];
    MR_EMIT("send_parked_for_hash", EF_I("key_hash32", static_cast<int64_t>(key_hash32)));
    if (reflood) park_reflood_arm();
}

// §F-SL-1: (re)arm the shared re-flood scan timer to the EARLIEST pending re-flood among parked sends. A one-shot
// (re-armed after each fire) so the parked sends can carry INDEPENDENT jittered re-flood deadlines through one timer
// id — exactly like earliest_due bounds the idle sleep. No pending re-flood -> cancel (idempotent).
void Node::park_reflood_arm() {
    uint64_t earliest = ~0ull;
    for (uint8_t i = 0; i < _parked_sends_n; ++i) {
        const ParkedSend& p = _parked_sends[i];
        if (p.reflood && p.reflood_count < protocol::park_reflood_max_retries && p.reflood_at_ms < earliest)
            earliest = p.reflood_at_ms;
    }
    if (earliest == ~0ull) { _hal.cancel(kParkRefloodTimerId); return; }
    const uint64_t now = _hal.now();
    const uint32_t delay = (earliest > now) ? static_cast<uint32_t>(earliest - now) : 0;
    (void)_hal.after(delay, kParkRefloodTimerId);
}

// §F-SL-1: re-flood every parked send whose deadline elapsed (bounded by park_reflood_max_retries), then re-arm. The
// re-emitted H reproduces the parked send's ORIGINAL query (hard bit + plane) so a re-homed contact is re-located the
// same way the first flood tried. Resolution removes the entry (drain) => it simply stops being re-flooded. In a QUIET
// net (the F-SL-1 case) this is the retry the single park-time flood lacked; the send_defer_ttl_ms giveup still bounds it.
void Node::park_reflood_fire() {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _parked_sends_n; ++i) {
        ParkedSend& p = _parked_sends[i];
        if (!p.reflood || p.reflood_count >= protocol::park_reflood_max_retries || p.reflood_at_ms > now) continue;
        emit_hash_query(p.key_hash32, p.reflood_hard, /*want_pubkey=*/false, p.reflood_plane);   // §no-auto-reqpubkey (send_by_hash's header note): the FOURTH site — a retry must not escalate what the original send deliberately did not
        MR_EMIT("send_hash_reflood", EF_I("key_hash32", static_cast<int64_t>(p.key_hash32)), EF_I("try", p.reflood_count + 1));
        p.reflood_count++;
        // §F-SL-1: DETERMINISTIC per-(hash,node,try) jitter — NOT a rand_range() draw. The re-flood targets a QUIET net,
        // where the flood's own LBT never draws (LBT only draws when busy), so this would be the re-flood's ONLY shared
        // -mt19937 consumption; drawing here reorders the whole downstream draw sequence and phantom-flips timing-fragile
        // deliveries that occur LATER (s27 post-m4, +49 s after the re-flood — the flood's transient event-ordering has
        // long settled by then, but a stream reorder is permanent). The mix decorrelates the re-fire beat across hashes,
        // nodes, and retries (batch B's "not the same beat" requirement) without touching the RNG stream.
        const uint32_t djit = (p.key_hash32 * 2654435761u + static_cast<uint32_t>(_node_id) * 40503u
                               + p.reflood_count * 2246822519u) % (protocol::park_reflood_jitter_ms + 1);
        p.reflood_at_ms = now + protocol::park_reflood_retry_ms + djit;
    }
    park_reflood_arm();
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
    MR_EMIT("send_layer_parked", EF_I("key_hash32", static_cast<int64_t>(key_hash32)));
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
    MR_EMIT("l2c_redirect_parked", EF_I("key_hash32", static_cast<int64_t>(want_hash)), EF_I("origin", pa.origin), EF_I("ctr", pa.ctr));
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
                // ⚠ ✖ MISSING / APP-VISIBLE (register B2, 2026-07-31 — reported, deliberately not changed, C1): this reads
                // the confidence back out of `_id_bind`, which the caller had "just set". On a TEAM member that is no
                // longer true — `resolve <hash>` issues an AUTO team-scoped H (emit_hash_query's team_q), and a
                // team-plane answer no longer writes `_id_bind` (§hashbind-plane), so `conf` stays CLAIMED and the
                // hash_resolved push reports authoritative=FALSE. `resolved_id` is still correct; only the trust bit
                // degrades. The clean fix is to thread the answer's own `authoritative` into this function instead of
                // re-reading a table — a signature change on a 4-caller path, and the same surface the peer
                // address-book design (2026-07-29 §2.5) is replacing with a VIEW. Corpus-invisible: no scenario runs
                // `resolve` (measured — 0 occurrences across all 36).
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
                    // recipient moved (stale binding) -> forward, no heal
                    MR_EMIT("l2c_redirect_forward", EF_I("to", resolved_id), EF_I("origin", p.origin), EF_I("ctr", p.ctr));
                } else {
                    _parked_sends[w++] = p;                          // queue full -> KEEP parked, retry next drain/age-out
                }
            } else if (resolved_id == _node_id) {
                // A plain send-by-hash that resolves to OUR OWN id: the app addressed its own key, or a same-id
                // collision aliased it to us. Do NOT do_send-to-self (a self-addressed DM); give it up.
                MR_EMIT("send_hash_giveup", EF_I("key_hash32", static_cast<int64_t>(key_hash32)));
            } else if (p.cross_layer && target_layer != 0xFF && target_layer != _cfg.leaf_id) {
                // Slice 4d (§5): the dst lives on ANOTHER layer -> originate a CROSS_LAYER DM via a bridging gateway.
                // §xl-crypt-intent: p.crypt is THREADED (it used to be dropped, like the sibling site above). ⚠ MEASURED,
                // not assumed: a `cross_layer` park can ONLY come from park_send_layer, which node.cpp reaches only when
                // want_crypt was FALSE (a sealed XL send with hop_count==0 returns err_unsupported first) and which stores
                // no crypt at all — so p.crypt is the `def` default here TODAY and this is byte-identical. It is threaded
                // anyway because "unreachable today" is not a confidentiality guarantee: if e2e_dm is turned ON between
                // the park and this drain, sealing is the CORRECT outcome, and a future park_send_layer that does carry
                // an intent gets it honoured instead of silently downgraded.
                // ★★ §xl-deleg-ack: `p.reply_to_hash` / `p.mobile_ctr` are THREADED here for the same reason and with
                // the same measurement. My brief expected this to share the ack defect with the arm above, because its
                // OWN same-layer sibling (the `else` branch below) does call deleg_ack_put. ⚠ IT DOES NOT, and the
                // reason is structural, not the crypt one XL-CRYPT gave: a `cross_layer` park can ONLY come from
                // park_send_layer, which starts from `ParkedSend{}` and sets exactly {key_hash32, flags, parked_at_ms,
                // cross_layer, body} — it NEVER stores reply_to_hash or mobile_ctr, and its single caller
                // (node.cpp CmdKind::send_layer) is a console origination, never a home re-originating for a mobile.
                // So p.reply_to_hash is provably 0 here TODAY and both lines below are byte-identical no-ops. Threaded
                // anyway on XL-CRYPT's principle — "unreachable today is not a guarantee" — so that a future
                // park_send that does carry a delegation cannot silently lose it the way this arm's sibling did.
                const uint16_t pch = send_cross_layer(resolved_id, key_hash32, target_layer, p.body, p.body_len, p.flags, p.crypt, p.type, /*override_source_hash=*/p.reply_to_hash);
                if (p.reply_to_hash != 0 && pch != 0) deleg_ack_put(p.reply_to_hash, pch, p.mobile_ctr);
            } else {
                MR_EMIT("send_hash_resolved", EF_I("key_hash32", static_cast<int64_t>(key_hash32)), EF_I("node", resolved_id));
                // same-layer (incl. a cross_layer park whose dst turned out to be on OUR leaf, §5.1): a plain DM.
                const uint16_t ch = do_send(resolved_id, p.body, p.body_len, p.flags, p.crypt, /*override_dst_hash=*/p.key_hash32, /*type=*/p.type, /*override_source_hash=*/p.reply_to_hash);   // §S2: p.type re-originates a parked INTRO with its TYPE (0 = plain, byte-identical); load-bearing (OUTSIDE the wrap): fly the held DM; M3: thread crypt; §mobile 3c: carry the queried hash so even the FIRST flood-resolved send to a mobile stamps DST_HASH=M (home forwards, not consumes); §mobile delegate: reply_to_hash -> SOURCE_HASH so the target's reply routes back to the mobile. For a normal send p.key_hash32 == key_hash_of_id(resolved_id) + reply_to_hash==0 -> byte-identical.
                if (p.reply_to_hash != 0) deleg_ack_put(p.reply_to_hash, ch, p.mobile_ctr);   // §mobile reverse-ack: a parked delegated re-origination resolved -> map ctr_H->ctr_M keyed by the MOBILE's hash (no-op if mobile_ctr==0)
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
                    MR_EMIT("l2c_redirect_forward", EF_I("to", id), EF_I("origin", p.origin), EF_I("ctr", p.ctr));
                } else {
                    _parked_sends[w++] = p;                          // queue full -> KEEP parked for the next beacon/age-out
                    continue;
                }
            } else {
                MR_EMIT("send_hash_resolved", EF_I("key_hash32", static_cast<int64_t>(p.key_hash32)), EF_I("node", id));
                const uint16_t ch = do_send(static_cast<uint8_t>(id), p.body, p.body_len, p.flags, p.crypt, /*override_dst_hash=*/0, /*type=*/p.type, /*override_source_hash=*/p.reply_to_hash);   // §S2: p.type re-originates a parked INTRO with its TYPE (0 = plain, byte-identical); load-bearing (OUTSIDE the wrap); M3: thread the stamped crypt intent (a beacon-resolved parked sendhashx still flies CRYPTED); §mobile delegate: reply_to_hash -> SOURCE_HASH
                if (p.reply_to_hash != 0) deleg_ack_put(p.reply_to_hash, ch, p.mobile_ctr);   // §mobile reverse-ack: a beacon-resolved parked delegated re-origination -> map ctr_H->ctr_M keyed by the MOBILE's hash
            }
            continue;                                            // drop the parked entry (forwarded / sent)
        }
        _parked_sends[w++] = p;
    }
    _parked_sends_n = w;
}

// Give up on parked sends whose hash never resolved (periodic, on kAgingTimerId). hash_locate_giveup_ms window
// (P-BUDGET: DECOUPLED from the deferred-queue's send_defer_ttl_ms — a parked send waits on a slow multi-hop flood
// round-trip that must be RE-FLOODED across several independent windows, not a local route reappearing on a beacon).
void Node::age_out_parked_sends() {
    if (_parked_sends_n == 0) return;
    const uint64_t now = _hal.now();
    uint8_t w = 0;
    for (uint8_t r = 0; r < _parked_sends_n; ++r) {
        const ParkedSend p = _parked_sends[r];
        if ((now - p.parked_at_ms) >= protocol::hash_locate_giveup_ms) {
            if (p.is_resolve) {
                push_hash_resolved(p.key_hash32, 0, false);     // a `resolve` that never resolved -> timeout answer
            } else {
                MR_EMIT("send_hash_giveup", EF_I("key_hash32", static_cast<int64_t>(p.key_hash32)));
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
