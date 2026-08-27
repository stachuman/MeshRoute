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

#include <cstdio>          // §id-hash S4b: snprintf, for the `!!` operator-critical log lines (node_channel.cpp's idiom)
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

// ★★★ §id-hash S2b-fix (2026-08-01, QA finding P1a): "is this hash held AUTHORITATIVELY by a DIFFERENT node_id?"
// -> that node_id, else -1. The gates are `key_hash_of_id`'s VERBATIM (authoritative + within id_bind_ttl_ms, with the
// self-binding exempt from the TTL) — U1, because this predicate exists to protect exactly what that reader answers:
// asking a different question here would let a row block a rehome that the reader can no longer see, or vice versa.
int Node::id_bind_auth_holder_other(uint32_t key_hash32, uint8_t except_node_id) const {
    if (key_hash32 == 0) return -1;
    const uint64_t now = _hal.now();
    for (uint16_t i = 0; i < _active->_id_bind_n; ++i) {
        const IdBind& e = _active->_id_bind[i];
        if (e.key_hash32 != key_hash32 || e.node_id == except_node_id) continue;
        if (e.confidence != static_cast<uint8_t>(IdBindConf::authoritative)) continue;
        const bool self_keep = (e.node_id == _node_id && e.key_hash32 == _key_hash32);
        if (!self_keep && _cfg.id_bind_ttl_ms > 0 && (now - e.last_seen_ms) >= _cfg.id_bind_ttl_ms) continue;
        return e.node_id;
    }
    return -1;
}

// Insert/update a binding (Lua id_bind_set dv:4677, + the rejoin/authoritative amendments). Maintains the
// (node_id <-> key_hash32) bijection: dedup-by-hash on accept (one hash -> one id), and a same-id CONFLICT
// (a different hash claims this node_id) is OVERWRITTEN by an authoritative source (self / owner-confirmed
// hash-bind) but REFUSED for a claimed one (emit addr_conflict_observed; the join-defense J_DENY stays
// deferred). NEW + table full -> table_cap_hit refuse. Only a NEW node_id emits id_bind_set (an update is
// silent — the Lua is_new gate). Returns true if the binding is now present.
// ★ §id-hash S2b: the update is UPGRADE-ONLY on BOTH axes — a `claimed` observation can neither demote an
// `authoritative` binding on a matching row (nor refresh its liveness stamp), nor EVICT an authoritative holder of the
// same hash under another id. See the two blocks below for the full reasoning; the second was QA finding P1a.
// ⓘ `IdBindSource::manual` (an operator assertion, spec §3-D8) is deliberately NOT added here: its only producer
// would be the `confirmid` verb, which is S5. An enumerator with no producer is the very `PeerKeyConf::overheard`
// smell the spec criticises in §2.4 — the tier exists in the ladder, in NV and in the app contract with nothing
// writing it. It lands WITH its writer or not at all.
bool Node::id_bind_set(uint8_t node_id, uint32_t key_hash32, IdBindSource source, IdBindConf confidence) {
    if (node_id == 0xFF) return false;                           // reserved id
    const uint64_t now = _hal.now();
    const bool authoritative = (confidence == IdBindConf::authoritative);
    // ★★★ §id-hash S2b-fix (QA finding P1a) — THE UPGRADE-ONLY RULE ALSO HAS TO HOLD ACROSS A REHOME, and it did not.
    // The matching-row guard below stops a claim demoting the SAME row, but this function also enforces the reverse
    // uniqueness rule (one hash -> one node_id) via id_bind_evict_other_hash_holders, which BOTH accept paths call
    // WITHOUT looking at confidence. So the demotion walked in through a different door:
    //     1. authoritative {id=10, H}                      (a first-hand beacon)
    //     2. claimed      {id=20, H}                       (a relayed soft H answer)
    //     3. no row for 20 -> the NEW-node_id path -> evict_other_hash_holders(H, 20) DELETES the authoritative row,
    //        then inserts {20, H, claimed} ⇒ key_hash_of_id(10) goes dark on a third party's say-so.
    // ⇒ A CLAIMED OBSERVATION MAY NOT DISPLACE AN AUTHORITATIVE HOLDER OF THE SAME HASH. Refusing the whole write is
    //   the right shape, not "insert without evicting": skipping the eviction would leave TWO rows for one hash and
    //   break the bijection id_bind_find_by_hash depends on (it returns the first match) — strictly worse. This is
    //   also the SAME policy, wording and telemetry family as the same-id conflict arm 20 lines below (U3).
    // ⓘ RULED (owner, 2026-08-01): claimed -> claimed stays NEWEST-WINS. There is no trust ordering between two
    //   claims, so keeping the existing behaviour keeps this a fix rather than a redesign (C1).
    // ⓘ An AUTHORITATIVE rehome is untouched and still evicts — that is the rejoin self-heal this whole mechanism
    //   exists for, and a test holds it as the positive control.
    // ⓘ The check is here, ABOVE the loop, because it must guard both accept paths and its answer cannot differ
    //   between them; `except_node_id = node_id` is what keeps a row from blocking its own refresh.
    if (!authoritative) {
        const int holder = id_bind_auth_holder_other(key_hash32, node_id);
        if (holder >= 0) {
            MR_EMIT("addr_rehome_refused", EF_I("node", node_id), EF_I("holder", holder),
                    EF_I("key_hash32", static_cast<int64_t>(key_hash32)), EF_S("source", id_bind_source_str(source)));
            return false;
        }
    }
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
        // ★★★ §id-hash S2b (spec 2026-08-01 §1-E) — A LIVE DEMOTION BUG, and it needed no new feature to bite.
        // These three lines wrote the incoming source and confidence UNCONDITIONALLY, so a `claimed` observation
        // silently DEMOTED an `authoritative` binding: a relayed soft H answer (IdBindSource::h_relay, :1252) landed
        // on a first-hand beacon row, and `key_hash_of_id` — which hard-filters `confidence != authoritative` (:148)
        // — then refused to stamp DST_HASH for that peer until the next beacon re-asserted it. Every reader of
        // `_id_bind` sat behind that filter, so the whole binding went dark on a THIRD PARTY's say-so.
        // ⇒ CONFIDENCE IS NOW UPGRADE-ONLY, mirroring peer_key_set's own rule for the sibling store (:255-261): the
        //   two tables answer the same shape of question and must not have opposite overwrite policies.
        // ★ AND `last_seen_ms` MOVES WITH IT — a claimed sighting does NOT extend an authoritative row's lease.
        //   RATIONALE (owner-specified, and deliberately symmetric with the spec's team-plane rule §3-D5c): on an
        //   authoritative row `last_seen_ms` means *"when we last had FIRST-HAND evidence"*, and it is load-bearing
        //   twice — the TTL gate in key_hash_of_id/id_bind_find_by_hash and the id_bind_age_out sweep. A relayed
        //   claim is not first-hand evidence, so refreshing on it would FAKE that liveness, exactly the hazard
        //   node_hashlocate.cpp:1240 names for _team_keys' cache-on-pass.
        //   ⚠ THE CONSEQUENCE IS INTENDED, not an oversight: an authoritative row that only ever gets re-CLAIMED now
        //   ages out at id_bind_ttl_ms (48 h) instead of living forever on hearsay. A binding nobody re-asserts
        //   first-hand SHOULD lapse. (The self-binding is exempt everywhere — id_bind_age_out's self_keep.)
        // ★ `source` is frozen with them, on purpose: it records WHICH observation established the confidence now
        //   held. Keeping `authoritative` while writing `source = h_relay` would label a relay as the authority —
        //   the provenance mislabelling §3-D8 refuses to ship.
        // ⓘ NO TELEMETRY ADDED, deliberately: a refused demotion is ROUTINE (every relayed answer about a known
        //   peer), and an emit here would re-anchor scenario streams and make this behaviour change unattributable
        //   in the same run (C4's rule applied to telemetry). Observability is the poison matrix + native tests.
        const bool existing_auth = (_active->_id_bind[i].confidence == static_cast<uint8_t>(IdBindConf::authoritative));
        if (authoritative || !existing_auth) {
            _active->_id_bind[i].last_seen_ms = now;                     // refresh (silent — not new)
            _active->_id_bind[i].source       = static_cast<uint8_t>(source);
            _active->_id_bind[i].confidence   = static_cast<uint8_t>(confidence);
        }
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
// send path to stamp DST_HASH (L2c verify-on-delivery) on an app DM. By DEFAULT only an AUTHORITATIVE
// (owner-confirmed / first-hand beacon / self) binding qualifies — a CLAIMED (second-hand / relayed) binding can be
// stale and would stamp a wrong dst_hash that triggers a spurious redirect at the recipient (mirrors send_by_hash's
// trust model, which HARD-verifies a soft binding before use). Returns false (DST_HASH omitted) otherwise.
// ★★ §id-hash S3 (spec §3-D1): the AUTHORITATIVE test became the `min` FLOOR and the row's real tier is reported
// through `actual`. THE DEFAULT IS THE OLD CONSTANT, so every pre-S3 caller — DST_HASH stamping (node_mac.cpp:137),
// the gateway trust test (:425), the sender-vs-origin check (:665), route_uses_mobile_as_transit, peer_book_by_id —
// is byte-identical. ⚠ Spec §3-D7 keeps it that way for the SEND path specifically: a false claimed binding would
// not be REJECTED by the recipient, it would be REDIRECTED (l2c_handle_misdelivery forwards to the claimed hash's
// owner), so a claim must never reach DST_HASH. Only display/inspection callers may lower the floor.
bool Node::key_hash_of_id(uint8_t id, uint32_t& out, IdBindConf min, IdBindConf* actual) const {
    const uint64_t now = _hal.now();
    for (uint16_t i = 0; i < _active->_id_bind_n; ++i) {
        if (_active->_id_bind[i].node_id != id) continue;
        const IdBindConf conf = static_cast<IdBindConf>(_active->_id_bind[i].confidence);
        if (static_cast<uint8_t>(conf) < static_cast<uint8_t>(min)) continue;   // below the floor (the pre-S3 "confident only")
        const bool self_keep = (id == _node_id && _active->_id_bind[i].key_hash32 == _key_hash32);
        if (!self_keep && _cfg.id_bind_ttl_ms > 0
            && (now - _active->_id_bind[i].last_seen_ms) >= _cfg.id_bind_ttl_ms) continue;     // expired
        if (actual) *actual = conf;
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
// hash -> team_local_id, FRESHEST qualifying row wins, and count the qualifying losers. See node.h for why this table
// (unlike _id_bind) really can alias. Display calls at the claimed floor; send calls at the authoritative floor.
uint8_t Node::team_id_of_key_freshest(uint32_t key_hash32, uint8_t& alias_dropped,
                                      IdBindConf min, IdBindConf* conf_out) const {
    alias_dropped = 0;
    if (conf_out) *conf_out = IdBindConf::claimed;   // ★ §id-hash S3: the SAFE default — "no row" never reads as first-hand
    if (key_hash32 == 0 || _cfg.team_id == 0) return 0;
    const uint64_t now = _hal.now();
    uint8_t  best = 0; uint64_t best_seen = 0; uint8_t hits = 0;
    IdBindConf best_conf = IdBindConf::claimed;
    for (uint8_t i = 0; i < _active->_team_keys_n; ++i) {
        const auto& e = _active->_team_keys[i];
        if (e.key_hash32 != key_hash32 || !is_team_peer(e.id)) continue;
        if (now - e.last_seen_ms > protocol::id_bind_ttl_ms) continue;      // §P2-6 48 h staleness — team_key_of_id's rule
        const IdBindConf conf = static_cast<IdBindConf>(e.confidence);
        if (static_cast<uint8_t>(conf) < static_cast<uint8_t>(min)) continue;
        ++hits;
        // FRESHEST STILL WINS inside the selected floor — trust filters candidates, it never re-ranks them.
        if (best == 0 || e.last_seen_ms > best_seen) { best = e.id; best_seen = e.last_seen_ms; best_conf = conf; }
    }
    alias_dropped = hits ? static_cast<uint8_t>(hits - 1) : 0;
    if (conf_out) *conf_out = best_conf;
    return best;
}
#endif

// The reverse (hash -> id) joins for a row that already carries a hash. Fills static_id/static_authoritative and
// team_id/team_authoritative/team_alias_dropped, leaving them at 0/false when that plane holds nothing.
void Node::peer_book_join_ids(PeerBookRow& r) const {
    if (r.hash == 0) return;                                  // an id-only row has nothing to join BY
    IdBindConf ic = IdBindConf::claimed;
    const int sid = id_bind_find_by_hash(r.hash, &ic);        // U1: the existing _id_bind reverse scan (skips expired)
    if (sid >= 0) { r.static_id = static_cast<uint8_t>(sid); r.static_authoritative = (ic == IdBindConf::authoritative); }
    IdBindConf tc = IdBindConf::claimed;                      // ★ §id-hash S3: the team plane's twin of the line above
    r.team_id = team_id_of_key_freshest(r.hash, r.team_alias_dropped, IdBindConf::claimed, &tc);
    r.team_authoritative = (r.team_id != 0) && (tc == IdBindConf::authoritative);
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
        // ★ §id-hash S2 (spec 2026-08-01 §1-D): SKIP OUR OWN BINDING. on_init/rejoin call
        // id_bind_set(_node_id, _key_hash32, IdBindSource::self, …) (node.cpp:77/:539/:864), so before this the address
        // book listed US as a peer — `[peer] hash=0x8CC9BDFF static_id=42(auth)` on node 42, verbatim from the bench
        // transcript in spec §0. An address book is a list of OTHERS.
        // ⚠ THE PREDICATE IS THE SELF-BINDING, NOT THE ID: `node_id == _node_id` alone would also hide a FOREIGN key
        // claiming our id — an address collision, which is the single most diagnostic row this dump can carry. Same
        // (id AND hash) test id_bind_set's own self-defence uses (:59) — U1, one spelling of "this row is us".
        if (e.node_id == _node_id && e.key_hash32 == _key_hash32) continue;
        r = PeerBookRow{};
        r.hash = e.key_hash32;
        r.static_id = e.node_id;
        r.static_authoritative = (e.confidence == static_cast<uint8_t>(IdBindConf::authoritative));
        if (r.hash) {                                                                   // §18: the same hash may hold both
            IdBindConf tc = IdBindConf::claimed;
            r.team_id = team_id_of_key_freshest(r.hash, r.team_alias_dropped, IdBindConf::claimed, &tc);
            r.team_authoritative = (r.team_id != 0) && (tc == IdBindConf::authoritative);   // §id-hash S3
        }
        peer_book_join_loc(r);                                                          // §AB4 (no join_ids here: static_id came straight off the row)
        ++n; if (fn) fn(r, ctx);
    }
    // ★★ (2b) §id-hash S2 (spec §1-C): _rt dests nothing else covered ⇒ STATIC-ID-ONLY rows — the exact twin of the
    //     team pass (4) below, which has existed since §AB3 while the static plane had NO equivalent. That asymmetry
    //     is spec §0's bench evidence: on one node `[peer] team_id=114` / `team_id=214` were listed as
    //     routable-but-unidentifiable, while `[route] dest=48/59/109` — the same condition on the static plane —
    //     appeared in `routes` and NOWHERE in `peers all`.
    //     ⓘ DEDUP is against `_id_bind` MEMBERSHIP, not against key_hash_of_id: pass (2) emits a row for EVERY
    //     _id_bind entry, including a `claimed` one and one whose hash is 0, both of which key_hash_of_id filters out
    //     — testing through the accessor would re-emit those as duplicates. Scanning the earlier pass's TABLE directly
    //     is this function's stated dedup idiom (see the header note), which is why there is no auxiliary "seen" array.
    //     ⓘ COST: O(_rt_count x _id_bind_n), both <= 254. That is the same shape pass (4) already pays (254 ids x a
    //     16-slot _team_keys scan) and it runs only for `peers all`, which is TEXT-CONSOLE ONLY (§2.6(a)).
    for (uint8_t i = 0; i < _active->_rt_count; ++i) {
        const uint8_t d = _active->_rt[i].dest;
        if (d == 0 || d == 0xFF || d == _node_id) continue;      // 0 unprovisioned / 0xFF reserved / ourselves (§1-D)
        bool covered = false;
        for (uint16_t j = 0; j < _active->_id_bind_n && !covered; ++j)
            covered = (_active->_id_bind[j].node_id == d);        // pass (2) already emitted this id
        if (covered) continue;
        r = PeerBookRow{};
        r.static_id = d;                                          // hash 0 ⇒ an id-only row; static_authoritative stays
        ++n; if (fn) fn(r, ctx);                                  //   false because there is no binding to vouch for
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
        r.team_authoritative = (e.confidence == static_cast<uint8_t>(IdBindConf::authoritative));   // §id-hash S3: straight off the row, like static_authoritative in pass (2)
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
            // ★★ §id-hash S3 — THE DISPLAY FLOOR IS EXPLICIT HERE, and passing the default would have PLANTED a
            // duplicate-row bug for S4a. This is a DEDUP against what pass (3) emitted, and pass (3) resolves through
            // `team_id_of_key_freshest` at its `claimed` display floor. If this asked `team_key_of_id` at its
            // `authoritative` default, a CLAIMED row would answer false; pass (4) would then emit an id pass (3)
            // already emitted WITH its hash, and `peers all` would print it twice. Exactly the S2 trap recorded for
            // (2b) — *"the dedup must read the TABLE, not the accessor"* — reached through the new floor parameter.
            // ⓘ INERT IN S3 by construction: the beacon is the only writer and it writes `authoritative`, so no row
            //   is below the floor yet. The explicit floor is what keeps it inert once S4a adds the claimed writer.
            if (team_key_of_id(static_cast<uint8_t>(id), th, IdBindConf::claimed) && th != 0) continue;   // has a _team_keys row -> (1)/(2)/(3)
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
    // ★★★ §id-hash S4a (register B53, spec §3-D6) — THE FLOOR IS NOW `claimed`, ON BOTH ARMS, AND THEY MOVED
    // TOGETHER. S3 left it at `authoritative` because lowering it is NOT inert (claimed static rows exist today, so
    // `hashof <id>` starts answering for them and `reqpubkey <id>` starts spending AIRTIME on them) and S3's contract
    // was inertness. S4a is the slice that both creates the team-side claimed tier and needs the tier to be visible:
    // ⚠ **without this, S4a would land a mechanism nothing can observe** — the claimed row it writes would be
    // invisible to every verb that could show or use it (spec §3-D1's own warning).
    // ⚠ BOTH ARMS OR NEITHER: a resolver that filters one plane harder than the other rebuilds spec §1-C's asymmetry
    //   defect, one of the five this arc exists to remove. B53 says so explicitly.
    // ★ WHAT THIS DOES **NOT** UNLOCK, and the distinction is the whole trust model: the SEND path is untouched.
    //   `key_hash_of_id` / `team_key_of_id` / `team_id_of_key` keep their `authoritative` DEFAULT, so DST_HASH
    //   stamping (node_mac.cpp:137), sealing and `team grantkey` still refuse a claim (spec §3-D6/D7). The floor is
    //   lowered HERE, in the resolver behind display and pubkey INSPECTION — where a claim is the thing you want to
    //   see and then check, because fetching the pubkey self-verifies against the hash and upgrades nothing.
    // ⓘ `actual` was already read and PROPAGATED into the row by S3 rather than hardcoded, precisely so the display
    //   could not start lying the moment this moved: a claimed row now renders `(claimed)`, not `(auth)`.
    IdBindConf sc = IdBindConf::claimed;
    if (key_hash_of_id(id, h, IdBindConf::claimed, &sc)) {   // STATIC: §3-D6's display/inspection floor
        if (!peer_book_by_hash(h, static_out)) { static_out = PeerBookRow{}; static_out.hash = h; }
        static_out.static_id = id;                           // the queried id IS the binding's id (one-hash-one-id)
        static_out.static_authoritative = (sc == IdBindConf::authoritative);
        mask |= kPeerBookStatic;
    }
    uint32_t th = 0;
    // ★★★ §id-hash S1b (2026-08-01, QA finding P2) — THE DE-DUP `&& !(mask && th == h)` IS GONE, and its removal is a
    // CORRECTNESS fix, not tidying. It suppressed the TEAM bit whenever both planes resolved the SAME hash. That was a
    // harmless display choice while only `hashof` read this — but S1 made the mask an AIRTIME decision, and there it
    // produced two wrong answers: a bare `reqpubkey <id>` silently picked STATIC instead of §3-D9's ambiguity refusal,
    // and an explicit `reqpubkey <id> -t` saw `has_team == false` and refused `err_no_binding` FOR A TEAM BINDING THAT
    // EXISTS. Hash equality does not make the two planes equal — the routes, the return paths and the flood scope all
    // still differ, which is exactly what the flag selects.
    // ⇒ this function reports PRESENCE, per plane, and nothing else. Any identity de-duplication is a RENDERER
    //   concern; `handle_hashof` now prints both rows and their equal hashes say "one identity, two planes" on their
    //   own, which is more information than the suppressed row carried.
    IdBindConf tc = IdBindConf::claimed;
    if (team_key_of_id(id, th, IdBindConf::claimed, &tc)) {   // TEAM: the team key cache, at the SAME §3-D6 floor as the static arm (B53 — both or neither)
        if (!peer_book_by_hash(th, team_out)) { team_out = PeerBookRow{}; team_out.hash = th; }
        // ⚠ team_id is left as the view RESOLVED it, NOT overwritten with `id`: when two team ids alias one hash the
        // freshest is the honest answer and team_alias_dropped says a loser exists. The caller reports the queried id.
        // ★ §id-hash S3: team_authoritative follows team_id for the same reason — when the join named a DIFFERENT
        // (fresher) id, its tier is the one being displayed. Only the fallback below describes the QUERIED id.
        if (team_out.team_id == 0) { team_out.team_id = id; team_out.team_authoritative = (tc == IdBindConf::authoritative); }
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
// ★ §id-hash S4a: `by_id` joins the key too — `key_hash32` here is the H's RAW bytes 2-5, so a by-id query for id
// 114 and a by-hash query for 0x00000072 are the same 32-bit value. See the HashQuerySeen struct comment.
bool Node::hash_query_seen_recently(uint8_t origin, uint32_t query_key32, bool hard, bool want_pubkey, bool team_scoped, bool by_id) {
    return recent_ring_hit(_active->_hash_query_seen, _active->_hash_query_seen_n,
                           HashQuerySeen{ origin, query_key32, 0, hard, want_pubkey, team_scoped, by_id },
                           _hal.now(), protocol::hash_query_seen_ttl_ms);
}
void Node::mark_hash_query_seen(uint8_t origin, uint32_t query_key32, bool hard, bool want_pubkey, bool team_scoped, bool by_id) {
    recent_ring_mark(_active->_hash_query_seen, _active->_hash_query_seen_n,
                     HashQuerySeen{ origin, query_key32, _hal.now(), hard, want_pubkey, team_scoped, by_id });
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
    // ⚠ §id-hash S4a: `key_hash32` here is the RAW query key (an id when `h.by_id`), and NO `by_id` FIELD IS ADDED
    // to this emit — deliberately. h_rx/h_forward/h_resolved fire on every H in the corpus, so a new field would
    // re-anchor all 36 streams and make THIS slice's real behaviour delta (the team ingestion, below) unattributable
    // in the same run — exactly what C4 forbids. The by-id path is observed in native tests and by the store writes.
    MR_EMIT("h_rx", EF_I("origin", h.origin), EF_I("key_hash32", static_cast<int64_t>(h.query_key32)), EF_I("ttl", h.ttl),
            EF_B("hard", h.hard));  // dv:11638

    // §mobile (2026-07-11): a MOBILE is a LEAF on the static plane — it does NOT participate in a STATIC hash-locate flood:
    // it never ANSWERS (its local id is invisible + home-proxied) and it never RELAYS (re-flooding puts a leaf on the static
    // flood plane + drains its battery; bench: a mobile re-tx'd its home's H). It DOES process a TEAM-scoped locate (the 6.2
    // team plane, where a teammate relays to reach a >1-hop teammate). A static node (is_mobile=false) is unchanged.
    if (_cfg.is_mobile && !h.team_scoped) {
        // §S3 part3 (TX-free overhear): a WANT_PUBKEY H for OUR OWN hash carries the requester's appended key. Cache it
        // BEFORE returning (no answer, no relay, no TX — the home answers on our behalf, Part 2). Covers the sender-in-RF-range
        // case at zero cost; redundant with the Part-2 forward when the home is in range, kept for the home-momentarily-deaf case.
        if (h.query_hash() == _key_hash32 && h.want_pubkey) (void)cache_want_pubkey_requester(h);   // by_id ⇒ query_hash()==0 ⇒ never matches (and by_id+want_pubkey is refused at the codec)
        return;
    }

    // Resolve. SOFT query (default): own-hash OR any cached binding answers ("anyone who knows"). HARD query
    // (verify-on-use, dv §3.7a): resolve ONLY via own-hash — SKIP the cache so it reaches the OWNER for an
    // authoritative correction. A cached binding carries its own confidence (beacon = authoritative/first-hand;
    // snooped hash-bind = claimed/second-hand, Phase C).
    // ★★ §id-hash S4a / spec §3-D4 — TWO FACTS, NOT ONE BOOLEAN. The single local `authoritative` conflated them:
    //   · `answered_by_owner`   — we ARE the target: selects owner-only behaviour (the WANT_PUBKEY answer, the
    //                             team_local_id substitution). It is NOT `node_id == _node_id` any more, because a
    //                             team BY_ID owner answers with its team_local_id, which is a DIFFERENT number.
    //   · `binding_verifiable`  — the id->hash assertion in the answer is one the receiver can check: selects plain
    //                             `DATA_TYPE_H_ANSWER` vs `DATA_TYPE_AUTHORITATIVE_H_ANSWER`.
    // By-hash self-match: BOTH true. **BY_ID self-match: owner TRUE, verifiable FALSE** — the owner possesses the
    // key, but an id is an address, not a commitment (owner ruling: "we can be sure only if we scan QR or exchange
    // keys out of network"). Making that structural is the point: the alternative is one call site passing a
    // surprising `false` and a later branch reading the wrong one.
    int node_id = -1; bool answered_by_owner = false; bool binding_verifiable = false;
    bool mobile_proxy = false; uint8_t mobile_epoch = 0; uint8_t mobile_layer = 0;   // §mobile 4a proxy flag + epoch; §5b the home's layer
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
    // ★★★ §id-hash S4a / spec §3-D3 — A BY_ID QUERY IS ANSWERED BY THE OWNER ONLY, AND NEVER FROM A CACHE.
    // The principle: **a cached answer is allowed exactly when the answer is SELF-VERIFYING.** hash->pubkey is
    // (`peer_key_set` recomputes `key_hash32_of(ed_pub)` and refuses a mismatch), which is what makes cache-on-pass
    // sound for the TYPE-5 path. id->hash is NOT, so a third party relaying its guess is pure attack surface for
    // nothing — it cannot be checked at the receiver and it would out-race the owner's real answer. Hence: no
    // `_id_bind` / `_team_keys` consult on this branch at all, only a SELF-match, and the self-match is on the
    // plane's OWN id — `team_local_id()` for a team-scoped locate, `_node_id` for a static one (spec §3-D3
    // "instead of `_key_hash32`"). `_key_hash32 != 0` is a genuine precondition, not defence: an answer carrying
    // hash 0 reads as "no hash" to every reader, so a keyless node must stay silent and let the flood continue.
    if (h.by_id) {
        const uint8_t self_id = same_team ? team_local_id() : _node_id;
        if (self_id != 0 && h.query_id() == self_id && _key_hash32 != 0 && (!mobile_registered() || same_team)) {
            node_id = self_id; answered_by_owner = true; binding_verifiable = false;   // §3-D4: owner, but the binding is a CLAIM
        }
    }
    else if (h.query_hash() == _key_hash32 && (!mobile_registered() || same_team)) { node_id = _node_id; answered_by_owner = true; binding_verifiable = true; }   // own-hash: resolves either variant (mobile_registered() is false on a static/gateway build -> always resolves)
    else if (!h.hard) {                                                              // HARD skips the cache -> flood to the owner
        // ⚠ ✖ MISSING (register B2's READ-side twin, deliberately NOT fixed here — C1; it is spec §12 / register D2's
        // read-path plane audit): this cache lookup has NO plane test, so a TEAM-scoped H can still be answered out of
        // the STATIC `_id_bind` — handing a team querier a STATIC node_id. The WRITE half is closed (a team-plane H
        // answer no longer enters `_id_bind`, see on_hash_bind_response/on_hash_bind_snoop), which removed the only
        // corpus-reachable instance: s24/s25/s26 measurably stopped answering a repeat team locate from a relay's
        // static cache and now let the OWNER answer (+78/+78/+56 events, delivery unchanged). What remains reachable is
        // a genuinely-static binding (from a beacon) answering a team-scoped query — same class, other direction.
        IdBindConf conf = IdBindConf::claimed;
        const int found = id_bind_find_by_hash(h.query_hash(), &conf);
        // §3-D4: a cache hit is NOT an owner answer (we hold somebody else's binding), and `binding_verifiable`
        // keeps the row's own tier exactly as the pre-S4a `authoritative` local did.
        if (found >= 0) { node_id = found; answered_by_owner = false; binding_verifiable = (conf == IdBindConf::authoritative); }
    }
    // §mobile 3a: HOST proxy — I HOST this mobile, so answer with MY id (home_id) as a CLAIMED binding; the querier caches
    // mobile_hash -> home_id and routes the DM to me (the host), which then last-mile-forwards it (do_post_ack). The home is
    // the mobile's LOCATION AUTHORITY, soft AND hard (was `!h.hard`, which let a HARD locate — e2e_ack_req drives it — bypass
    // the home + flood to the mobile owner). Redirect forwards unconditionally; the DIRECT proxy is LIVENESS-gated so a
    // long-dead mobile's entry stops black-holing. Gated on _mobile_reg_n>0 -> a non-host is byte-identical (no wire change).
    // ★ §id-hash S4a: `!h.by_id` — the host proxy is keyed by the mobile's HASH, and a hosted mobile's local id is
    // not a globally addressable number in the first place. Proxy-answering a by-id query would also be exactly the
    // NON-owner answer §3-D3 forbids (the home does not own the binding; it holds a registration).
    if (node_id < 0 && !h.by_id && _active->_mobile_reg_n > 0) {
        for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
            if (_active->_mobile_reg[i].key_hash32 == h.query_hash()) {   // §mobile 4a: a MOBILE_H_ANSWER carrying the registration epoch (freshest-proxy wins)
                if (_active->_mobile_reg[i].redirect_home_id != 0) {    // §mobile 4b/5b: we're STALE -> redirect to the mobile's NEW home + ITS layer (NOT liveness-gated)
                    if (h.want_pubkey) break;                          // §Part 2: a STALE home holds NO key for M -> do NOT answer/suppress a WANT_PUBKEY locate (that black-holes the encrypted DM when we're a flood cut-vertex). Leave node_id=-1 -> FORWARD the flood on to the NEW home, which cached M's key (Fix 6) and answers with the pubkey. The plain (location) redirect below is unaffected.
                    node_id = _active->_mobile_reg[i].redirect_home_id;
                    mobile_epoch = _active->_mobile_reg[i].redirect_epoch;
                    mobile_layer = _active->_mobile_reg[i].redirect_home_layer;
                    answered_by_owner = false; binding_verifiable = false; mobile_proxy = true;   // §3-D4: a proxy is neither the owner nor verifiable
                } else if (_hal.now() - _active->_mobile_reg[i].last_heard_ms < protocol::mobile_liveness_ms) {   // §mobile: I'm the home -> proxy ONLY if the mobile is recently alive
                    node_id = _node_id;
                    mobile_epoch = static_cast<uint8_t>(_active->_mobile_reg[i].epoch);
                    mobile_layer = active_layer_id();
                    answered_by_owner = false; binding_verifiable = false; mobile_proxy = true;   // §3-D4: a proxy is neither the owner nor verifiable
                }
                break;   // matched (live/stale/redirect) — STALE leaves node_id=-1 -> forward -> the locate times out = "unreachable" (NOT a black hole)
            }
    }

    if (node_id >= 0) {                                    // RESOLVER path (dv:11644) — answer + SUPPRESS the forward
        mark_hash_query_seen(h.origin, h.query_key32, h.hard, h.want_pubkey, h.team_scoped, h.by_id);   // §T6/B: keyed by the H's own plane; §S4a: + its key space. mark BEFORE replying so a re-flood doesn't double-answer (dv:11647)
        // §F-TR-2: the ANSWER binding for a TEAM-scoped own-hash resolve must name our TEAM identity (team_local_id), NOT the
        // host-assigned static node_id. A DUAL (registered) member's _node_id is its static host id (e.g. 254); answering a
        // team locate with that sends the querier to _rt_team looking for a static id that has no team route -> no_route/giveup.
        // ★ §id-hash S4a: the owner test is now `answered_by_owner` (§3-D4), NOT `node_id == _node_id`. Behaviour is
        // unchanged on the by-hash paths — `id_bind_set`'s `addr_conflict_self_defended` arm makes it impossible for
        // a FOREIGN hash to bind to our own id, so a cache hit could never return `_node_id` — but a team BY_ID owner
        // answers with `team_local_id()`, which is a different number, and the old predicate would read FALSE for it.
        uint8_t answer_node_id = static_cast<uint8_t>(node_id);
        if (answered_by_owner && same_team && mobile_registered() && team_local_id() != 0)
            answer_node_id = team_local_id();                       // idempotent for BY_ID (node_id already IS team_local_id())
        // ★ §id-hash S4a: BY_ID answers OUR OWN hash — the query carried an id, so the hash is the thing being
        // reported. Only the owner reaches here on that path (§3-D3), so `_key_hash32` is the correct and only answer.
        const uint32_t answer_hash = h.by_id ? _key_hash32 : h.query_hash();
        MR_EMIT("h_resolved", EF_I("origin", h.origin), EF_I("key_hash32", static_cast<int64_t>(h.query_key32)),
                EF_I("node", answer_node_id), EF_I("target_layer", _cfg.leaf_id), EF_B("authoritative", binding_verifiable));  // dv:11649
        if (h.want_pubkey && mobile_proxy) {                        // §Part 2 Fix 7: the HOME answers WANT_PUBKEY on behalf of its LIVE mobile (Option 1 — the home carries the key). MUST precede the owner branch: a live proxy has node_id==_node_id, so the owner branch would otherwise leak the HOME's own key under the mobile's hash.
            const uint8_t* mk = host_mobile_ed_pub(h.query_hash());  // the mobile's cached ed_pub (Fix 6 push), iff a LIVE direct proxy has_pubkey (a redirect carries no local key)
            if (mk) send_mobile_pubkey_answer(h.origin, mobile_layer, static_cast<uint8_t>(node_id), h.query_hash(), mobile_epoch, mk, h.team_scoped);
            // no cached key (the push hasn't arrived yet, or this is a redirect) -> stay SILENT on WANT_PUBKEY: the locate times out and the sender's reqpubkey retries (the push races registration). The flood is still suppressed by the return below.
            // §S3 part2 (D3 eager): the requester needs OUR mobile's key (above) AND our mobile needs the REQUESTER's key to
            // DECRYPT its future sealed DM. Cache the requester here (the owner branch's mutual-exchange, which this proxy
            // branch replaces for a hosted mobile) + FORWARD it to the mobile as a 1-hop last-mile so the mobile can e2e_open.
            const uint32_t rq = cache_want_pubkey_requester(h);
            if (rq != 0) forward_requester_key_to_mobile(h.query_hash(), h.requester_ed_pub, reinterpret_cast<const char*>(h.name), h.name_len);
        } else if (h.want_pubkey && answered_by_owner && _crypto_ready) {   // §6 + review#1: ONLY the OWNER answers WANT_PUBKEY (§3-D4's `answered_by_owner`; by_id+want_pubkey is refused at the codec so this is by-hash only)
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
            send_hash_bind_response(h.origin, mobile_proxy ? mobile_layer : _cfg.leaf_id, answer_node_id, answer_hash, binding_verifiable, mobile_proxy, mobile_epoch, /*team_scoped=*/h.team_scoped);   // §5b: a mobile answer carries the HOME's full layer_id (not the proxy's leaf); §F-TR-2: team-scoped own answer names team_local_id (not the static host id) + routes on the team plane (_rt_team + team RREQ). §S4a: BY_ID reports OUR hash + binding_verifiable=false -> plain H_ANSWER -> lands `claimed` (§3-D5a)
        return;                                            // SUPPRESS — the whole point: the flood stops here
    }

    // FORWARD path (dv:11655): we don't know it (or it's a HARD query and we're not the owner) -> re-broadcast
    // once, deduped per variant, until TTL runs out.
    if (hash_query_seen_recently(h.origin, h.query_key32, h.hard, h.want_pubkey, h.team_scoped, h.by_id)) return;   // flood dedup (dv:11656) — §2: WANT_PUBKEY is its own variant; §S4a: so is BY_ID
    mark_hash_query_seen(h.origin, h.query_key32, h.hard, h.want_pubkey, h.team_scoped, h.by_id);    // (dv:11657)
    if (h.ttl == 0) return;                                         // TTL exhausted (dv:11658)
    // L7: h.ttl is an unauthenticated wire byte — a forged ttl=255 would re-flood with a 255-hop horizon. Clamp to
    // flood_hop_max so the re-flooded ttl can't exceed the mesh diameter (dedup already bounds re-broadcasts per node).
    const uint8_t fwd_ttl = (h.ttl > protocol::flood_hop_max ? protocol::flood_hop_max : h.ttl) - 1;
    MR_EMIT("h_forward", EF_I("origin", h.origin), EF_I("key_hash32", static_cast<int64_t>(h.query_key32)),
            EF_I("ttl", static_cast<int64_t>(fwd_ttl)), EF_B("hard", h.hard));  // dv:11661
    h_in fwd{};
    // ★ §id-hash S4a: `query_key32` is copied VERBATIM and `by_id` rides with it — spec §4, "forwarders preserve the
    // bit AND the canonical value". Dropping the bit is not a degradation to "no answer": the frame would re-pack as
    // a by-HASH query for a small integer, which a low-valued hash could actually MATCH, so a multi-hop by-id query
    // would silently become a different question after the first hop. (pack_h re-validates the canonical value, so a
    // forward of a non-canonical frame is impossible — parse_h already rejected it.)
    fwd.leaf_id = _cfg.leaf_id; fwd.origin = h.origin; fwd.query_key32 = h.query_key32; fwd.by_id = h.by_id;
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
// hop-by-hop on the existing rt[origin] (the H flood lays no reverse path). (Lua send_hash_bind_response dv:5877.)
// ★★ §id-hash S4a (spec §3-D4): the flag is `binding_verifiable`, and the rename is the point. It used to be
// `authoritative`, read as *"the resolver answered as the owner"* — which conflated OWNERSHIP with CHECKABILITY.
// The AUTHORITATIVE frame TYPE means *"this id->hash assertion is one you can verify"*, and a BY_ID answer is not:
// the owner proved it holds the key, never that it holds the id. So the owner passes FALSE here for a by-id answer,
// the plain `DATA_TYPE_H_ANSWER` goes out, and `on_hash_bind_response` maps it to `IdBindConf::claimed` — honest
// reuse of an existing codepoint (§3-D5a), not a workaround.
bool Node::pack_typed_answer_inner(TxItem& item, Plane plane, uint8_t dst,
                                   const uint8_t* body, uint8_t body_len) {
    item.plane = plane;
    stamp_origin(item, item.plane, dst);
    const size_t n = pack_unicast_inner(std::span<uint8_t>(item.inner, sizeof item.inner), /*flags=*/0,
                                        /*dst_key_hash32=*/0, /*layer_ids=*/nullptr, /*n_layers=*/0, /*cur=*/0,
                                        item.origin, /*source_hash=*/0, body, body_len, /*lat_e7=*/0, /*lon_e7=*/0);
    if (n == 0) return false;
    item.inner_len = static_cast<uint8_t>(n);
    return true;
}

void Node::send_hash_bind_response(uint8_t to_origin, uint8_t target_layer, uint8_t node_id,
                                   uint32_t key_hash32, bool binding_verifiable, bool mobile_proxy, uint8_t epoch, bool team_scoped) {
    if (_active->_tx_queue_n >= kTxQueueCap) return;                       // queue full -> drop (the querier can re-flood)
    hash_bind_inner hb{};
    hb.target_layer = target_layer; hb.node_id = node_id; hb.key_hash32 = key_hash32;   // authoritative rides the frame TYPE, not the inner
    hb.epoch = epoch;                                           // §mobile 4a: packed only for the mobile variant (7 B)
    uint8_t inner[7];                                          // 7 for the mobile variant (+epoch); the normal answer packs 6 -> byte-identical
    const size_t n = pack_hash_bind_inner(hb, std::span<uint8_t>(inner, sizeof(inner)), mobile_proxy);
    if (n == 0) return;
    TxItem item{};
    item.dst = to_origin;
    item.flags = 0;                                              // byte-1 flags clear; the H_ANSWER TYPE byte (below) types it
    // §F-TR-2: a TEAM-scoped H answer routes home on the TEAM plane. AUTO dispatches by is_team_peer(to_origin), which is
    // FALSE when the origin (the querier's team_local_id) has not yet been learned as a team peer — AUTO then falls to the
    // static _rt, RREQs the team id on the STATIC plane, and the dual-member owner (whose static id != team id) never
    // self-answers that RREQ -> no_route/giveup. Forcing TEAM routes via _rt_team + a TEAM RREQ (owner self-answers on
    // team_local_id). Byte-identical where a team route already exists (AUTO already picked _rt_team); s22-s26 audited.
    item.type  = mobile_proxy ? DATA_TYPE_MOBILE_H_ANSWER
               : binding_verifiable ? DATA_TYPE_AUTHORITATIVE_H_ANSWER : DATA_TYPE_H_ANSWER;
    if (!pack_typed_answer_inner(item, team_scoped ? Plane::TEAM : Plane::AUTO, to_origin,
                                 inner, static_cast<uint8_t>(n))) return;
    item.ctr = next_ctr(to_origin); item.ctr_lo = static_cast<uint8_t>(item.ctr & 0x0F);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_EMIT("hash_bind_response_enqueued", EF_I("to", to_origin), EF_I("node", node_id),
            EF_I("key_hash32", static_cast<int64_t>(key_hash32)), EF_B("authoritative", binding_verifiable));  // dv:5897 — the field name is the WIRE bit's name (the answer TYPE), kept so no stream re-anchors on a rename
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
    // ★★ §hashbind-plane (2026-07-31, register B2): NEVER `_id_bind` when the answer rode the TEAM plane.
    // `hb->node_id` is then a TEAM LOCAL id (handle_h answers a team-scoped locate with team_local_id,
    // node_hashlocate.cpp F-TR-2), and _id_bind is the STATIC node_id-indexed plane -> writing it is the I2 breach
    // s24 asserts a static bystander never commits. Same rule and same shape as the two shipped gates in this file
    // (the WANT_PUBKEY owner branch and cache_want_pubkey_requester, both `!h.team_scoped && !h.mobile_req`).
    // ⓘ THAT rule stands unchanged; what changed in S4a is where the team binding GOES instead of the floor.
    // ★★★ §id-hash S4a (spec §3-D5b) — THE TEAM ARM IS NOW BUILT, and B2's "deliberate refusal" is retired in place.
    // S3 built the STORE (`TeamKey{source,confidence}`, upgrade-only, claimed-cohort-first eviction, floors on every
    // reader) and deliberately shipped NO producer. This IS the producer: a team-plane answer we ASKED for lands in
    // `_team_keys` — never `_id_bind`, whose index space is the static `node_id` plane (§18/C3).
    // ★★ IT LANDS `claimed` UNCONDITIONALLY, INCLUDING FOR AN OWNER'S AUTHORITATIVE by-HASH ANSWER — a deliberate
    //    ASYMMETRY vs the static line above, and the reason is `on_hash_bind_snoop`'s reason (2), the one refusal
    //    reason S3 left LIVE: `_team_keys` feeds the team-DAD L2a mediation comparator (`node_beacon.cpp` compares
    //    `team_key_of_id(b.src)` against the beacon's own key and sends a mediated DENY on a mismatch). That
    //    comparator reads at the DEFAULT `authoritative` floor, so writing `authoritative` from ANY on-air source
    //    would let transit-derived rows manufacture spurious DENYs against legitimate teammates. The static plane
    //    has no analogue of that comparator, which is why its tiering can follow the frame TYPE and this cannot.
    //    ⇒ first-hand beacon = the ONLY writer of an authoritative team row (`node_beacon.cpp:839`), exactly as
    //    before; everything learned on air is a claim. Spec §3-D5b says `claimed` and §9 gates it.
    // ★ NEITHER ARM SETS `_team_peer` (spec §3-D5b, ★): membership is not manufacturable from a binding. A claimed
    //   row for a non-member is inert by construction — `team_key_of_id` gates on `is_team_peer(id)` first — which
    //   is the property that makes this safe to write without a membership test of its own.
    // ⓘ NO NEW EMIT on either arm, and that is C4 applied to telemetry: this path IS corpus-reachable (s24/s25/s26/
    //   s28 carry team-plane answers), so an emit here would re-anchor those streams in the same run as the
    //   behaviour change and make the delta unattributable. The store is observed by native tests.
    if (!team_plane)
        id_bind_set(hb->node_id, hb->key_hash32, IdBindSource::h_query,
                    authoritative ? IdBindConf::authoritative : IdBindConf::claimed);
    else
        team_key_set(hb->node_id, hb->key_hash32, IdBindSource::h_query, IdBindConf::claimed);
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
    // ★★★ §id-hash S4b (spec §5 step 3): LAST, and the position is deliberate. Everything above is what this answer
    // already did before S4b — store, record, drain — and it stays in the same order, so a corpus stream can only move
    // if an intent actually matches. Nothing in the 36-scenario corpus can arm one (the sim console has no by-id
    // `reqpubkey` form at all), so this call is inert there BY CONSTRUCTION, not by luck.
    id_pubkey_intent_consume(hb->node_id, team_plane, hb->key_hash32);
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
// a LIVE DIRECT row (a redirect points elsewhere and carries no local key). Returns nullptr otherwise.
// ★★★ §MH-S5-FIX2 (owner-ruled 2026-08-10, ledger §1.14) — **THE CLASSIFICATION WAS ESTABLISHED FROM THE CALL GRAPH,
// NOT ASSUMED, AND IT IS SERVICE ⇒ GATED.** Answering *"here is my hosted mobile's key"* is direct hosted service; the
// only question was whether both callers already stood behind a gate that excludes an expired row. They do not:
//   · `handle_h`'s WANT_PUBKEY proxy answer (`:1125`) — ALREADY live-and-direct, structurally: `mobile_proxy` is
//     assigned at exactly two places (`:1097` redirect / `:1102` direct) and the redirect arm is preceded by
//     `if (h.want_pubkey) break;`, so under WANT_PUBKEY only the DIRECT arm can set it, and that arm's own gate is
//     `now - last_heard_ms < mobile_liveness_ms` — the exact complement of `host_row_expired`. ⇒ the new term is
//     REDUNDANT there, and deliberately so (§9.3: the boundary is spelled once, not re-asked per consumer).
//   · `node.cpp`'s `reqpubkey`/`emit_hash_query` short-circuit (`:1920`) — **NO liveness gate of any kind.** This is
//     the caller that made the "leave it alone" option unavailable: an expired row let this node answer its own
//     operator as key authority for a mobile it may no longer host, and (this is the behaviour change) that arm
//     returns `queued` WITHOUT airing anything. Refusing now lets the ordinary WANT_PUBKEY flood run, which reaches
//     the mobile's CURRENT home — the node that really holds the key (Fix 6 push).
// ⚠ NOT the redirect ANSWER: the location-redirect fork at `:1092` is untouched and stays un-liveness-gated. A
//   redirect must still redirect; that is the mechanism §9.2 keeps the row alive FOR (pinned by a positive control).
const uint8_t* Node::host_mobile_ed_pub(uint32_t key_hash32) const {
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == key_hash32 && _active->_mobile_reg[i].has_pubkey
            && host_row_live_direct(i))
            return _active->_mobile_reg[i].ed_pub;
    return nullptr;
}

// §mobile hash-locate Part 2 (Fix 7) / B161: a WANT_PUBKEY answer for a hosted mobile — canonical unicast inner
// `[origin][body]`, where BODY = the mobile hash_bind (7 B: home routing + epoch) ‖ ed_pub[32] ‖ name_len ‖ name,
// TYPE 13. Distinct from the owner's TYPE-5 answer: the sender must learn BOTH the mobile's key AND that it routes via
// the HOME (not to the local id). The H-query's plane is explicit so stamp_origin chooses the same namespace as types 1/2/8.
void Node::send_mobile_pubkey_answer(uint8_t to_origin, uint8_t target_layer, uint8_t home_id,
                                     uint32_t key_hash32, uint8_t epoch, const uint8_t ed_pub[32], bool team_scoped) {
    if (_active->_tx_queue_n >= kTxQueueCap) return;
    hash_bind_inner hb{}; hb.target_layer = target_layer; hb.node_id = home_id; hb.key_hash32 = key_hash32; hb.epoch = epoch;
    uint8_t body[7 + 32 + 1 + 32];
    const size_t n = pack_hash_bind_inner(hb, std::span<uint8_t>(body, 7), /*mobile=*/true);   // 7 B mobile variant (home routing + epoch)
    if (n == 0) return;
    for (int i = 0; i < 32; ++i) body[n + i] = ed_pub[i];                                      // ‖ the mobile's ed_pub
    uint8_t nlen = 0;                                                                            // §1.3: ‖ the hosted MOBILE's name (from _mobile_reg, pushed with the key)
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i) {
        if (_active->_mobile_reg[i].key_hash32 != key_hash32) continue;
        nlen = _active->_mobile_reg[i].name_len > 32 ? 32 : _active->_mobile_reg[i].name_len;
        for (uint8_t b = 0; b < nlen; ++b) body[n + 32 + 1 + b] = static_cast<uint8_t>(_active->_mobile_reg[i].name[b]);
        break;
    }
    body[n + 32] = nlen;
    TxItem item{};
    item.dst = to_origin;
    item.flags = 0; item.type = DATA_TYPE_MOBILE_H_ANSWER_PUBKEY;
    const size_t total = n + 32 + 1u + nlen;
    if (!pack_typed_answer_inner(item, team_scoped ? Plane::TEAM : Plane::AUTO, to_origin,
                                 body, static_cast<uint8_t>(total))) return;
    item.ctr = next_ctr(to_origin); item.ctr_lo = static_cast<uint8_t>(item.ctr & 0x0F);
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
// ★★★ §MH-S5-FIX2 (owner-ruled 2026-08-10, ledger §1.14): `host_row_live_direct(i)` replaces the bare
// `redirect_home_id == 0`. The body of this function IS a last-mile transmission (`addr_len=1` to `mobile_local_id`),
// so it is service in exactly the sense the ruling names, and an expired row's local id points at a mobile that has
// been silent for 25 minutes. ⓘ Refusing is a plain no-op for the requester: this push is the EAGER half of the
// WANT_PUBKEY exchange, and its absence already had to be tolerated (the caller's own comment: the push races
// registration, and the mobile's `reqpubkey` retry is the backstop).
void Node::forward_requester_key_to_mobile(uint32_t mobile_hash, const uint8_t requester_ed_pub[32],
                                           const char* name, uint8_t name_len) {
    const uint32_t rq = key_hash32_of(requester_ed_pub);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == mobile_hash && host_row_live_direct(i)) {
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
    if (inner_len < 40) return;                                  // hash-bind 7 + ed_pub 32 + name_len
    const uint8_t nlen = inner[39];
    if (nlen > 32 || inner_len != static_cast<uint8_t>(40 + nlen)) return;
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, 7));   // the mobile 7 B: home routing + epoch (ignores the ed_pub tail)
    if (!hb) return;
    const uint8_t* ed = inner + 7;
    const uint32_t kh = key_hash32_of(ed);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
    if (kh != hb->key_hash32) return;                              // malformed answer earns no key/home/drain effect
    const char* nm = nlen ? reinterpret_cast<const char*>(inner + 40) : nullptr;
    if (!peer_key_set(kh, ed, PeerKeyConf::authoritative, nm, nlen)) return;   // never id_bind the LOCAL id
    MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(kh)), EF_I("node", hb->node_id));
    push_peer_key_cached(kh);   // §7: app prompts "secure send ready — resend" (§S6: + the cached name)
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
//   (1) ✅ ANSWERED AND BUILT by §id-hash S3 (2026-08-02) — kept as the audit trail, NOT as a live reason.
//       IT SAID: "`_team_keys` has NO confidence dimension, and this path ingests CLAIMED bindings … team_key_of_id /
//       team_id_of_key have no such test, so re-homing would UPGRADE an unverified observation to a trusted one."
//       IT NOW HAS ONE: `TeamKey{source,confidence}` reusing IdBindSource/IdBindConf, the writer is upgrade-only, and
//       BOTH readers named there (plus `key_hash_of_id`) take a confidence FLOOR defaulting to `authoritative` — so
//       the DST_HASH consumer this bullet worried about refuses a claimed row exactly as `send_by_hash` does.
//   (2) ⚠ STILL LIVE, and it is now the LOAD-BEARING one. It feeds the team-DAD L2a mediation: node_beacon.cpp
//       compares team_key_of_id(b.src) against the beacon's own key and sends a mediated DENY on a mismatch. Seeding
//       that comparator from unauthenticated transit traffic manufactures spurious DENYs against legitimate teammates
//       — the exact hazard node.cpp already warns about for stale `_team_keys` rows. ⇒ that call site keeps the
//       DEFAULT `authoritative` floor, so a claimed row can never reach the comparator once S4a starts writing them.
//   (3) ✅ NEUTRALISED by §id-hash S3's D5c rules, and this bullet is what specified them. It said `_team_keys` is a
//       16-slot evict-OLDEST LRU whose `last_seen_ms` means "heard now", so cache-on-pass "would both fake that
//       liveness and let transit traffic evict genuine beacon rows." Both consequences are now structurally
//       prevented: a claimed write cannot refresh an authoritative row's stamp, and eviction drains the CLAIMED
//       cohort before it will touch any authoritative row (node_routing.cpp team_key_set).
// ⇒ the correct fix is the one the two shipped sibling gates already make: DON'T write the wrong plane. This does NOT
// contradict the address-book design (2026-07-29 §2.5), which forbids the `_id_bind` write and fixes `hashof` with a
// VIEW over both maps, "never by a write".
// ✅ BUILT BY §id-hash S4a (2026-08-02) — the team-plane INGEST that S3 deliberately left out. Both halves now land:
// the DESTINATION consume in `on_hash_bind_response` (`h_query`) and the RELAY observation here (`h_relay`), each
// `team_key_set(id, hash, …, IdBindConf::claimed)`, ★ neither setting `_team_peer`. Reason (2) is respected by the
// tier, not by a special case: a `claimed` row is below the DAD comparator's default `authoritative` floor.
// ⚠⚠ **CORRECTED 2026-08-02 (QA, §B30 review). The line here previously read "⇒ a repeat `send -t 0x<hash>` to an
// unheard teammate now resolves from cache instead of re-flooding the locate." THAT IS FALSE, and the chain is short
// enough to check: (1) `send -t 0x<hash>` resolves through `team_id_of_key` at its DEFAULT `authoritative` floor
// (send_by_hash's team arm, ~:1572); (2) both ingest sites above write `IdBindConf::claimed` UNCONDITIONALLY (:1311
// destination, :1507 relay) — the answer's own AUTHORITATIVE bit is deliberately not consulted, because an id is not
// self-verifying whichever direction the query ran; (3) claimed < authoritative ⇒ the lookup MISSES; (4) the send
// falls through to the §F-TR-1 team-scoped H flood; (5) that answer lands `claimed` again. ⇒ **IT NEVER CONVERGES.**
// ★ WHAT THE INGEST ACTUALLY BUYS IS THE VIEW, NOT THE SEND: `hashof`/`peers all` can now name and LABEL an unheard
//   teammate (`team_id_of_key_freshest` defaults to `claimed`), which is the whole point of §id-hash S4a. The send
//   path deliberately declines to route on hearsay.
// ★★ THE STANDING COST, RECORDED SO IT IS NOT "DISCOVERED" LATER: a repeat `send -t 0x<hash>` to a teammate we hold
//   only a CLAIM for re-floods the locate every time (rate-limited only by `hash_query_seen_ttl_ms`). That is the
//   ACCEPTED trade, not an oversight — spec §3-D7's reasoning: a false claimed binding used as a send target does not
//   merely fail, it routes the message to the owner of the false hash. ⇒ **DO NOT "fix" the airtime by lowering this
//   floor** — that reverses D7. The convergent cure is a first-hand beacon (which writes `authoritative`) or a QR.
void Node::on_hash_bind_snoop(const uint8_t* inner, uint8_t inner_len, bool authoritative, bool team_plane) {
    auto hb = parse_hash_bind_inner(std::span<const uint8_t>(inner, inner_len));
    if (!hb) return;
    if (!team_plane)   // ★ §hashbind-plane: a TEAM-plane answer carries a TEAM LOCAL id -> never the static _id_bind (§18/C3)
        id_bind_set(hb->node_id, hb->key_hash32, IdBindSource::h_relay,
                    authoritative ? IdBindConf::authoritative : IdBindConf::claimed);
    else
        // ★★★ §id-hash S4a (spec §3-D5b, "relay observation, if retained -> source = h_relay, confidence = claimed").
        // RETAINED, and the three refusal reasons in the header above are why it is now safe rather than why it was
        // dropped: (1) the confidence dimension exists, (3) D5c stops it faking liveness or evicting a beacon row,
        // and (2) — the live one — is respected by writing `claimed`, which the DAD comparator's default
        // `authoritative` floor cannot see. ★ It does NOT set `_team_peer`: storing a binding for display is not
        // membership, and `team_key_of_id`'s `is_team_peer` gate keeps a non-member's row inert.
        team_key_set(hb->node_id, hb->key_hash32, IdBindSource::h_relay, IdBindConf::claimed);
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
// ★★★★ §UI-16 N6b (2026-08-24) — `out_dispatch`, AND WHAT IT IS FOR. The return value has always been *"the ctr if
// sent immediately, else 0"*, and both halves of that are ambiguous: a non-zero ctr survives a FULL TX QUEUE (the
// frame is dropped and the counter is still minted) and a zero one covers a stored park, a FULL PARKED RING and a
// loud refusal alike. ⇒ each arm below now states its OWN admission through the out-parameter, together with the
// destination it RESOLVED AT SEND TIME. ⛔ NOTHING ELSE MOVED: no branch changed, no drop became a retry, no emit
// was added or reordered, and `nullptr` (every pre-existing caller) is byte-for-byte the previous function.
// ⓘ AN ARM THAT LEAVES IT AT `Admit::none` IS SAYING SOMETHING TRUE — *"no admission point was reached"* — and every
//   such arm has already called `push_send_failed`. The cross-layer arm is deliberately among them: a type-19 is
//   REFUSED inside `enqueue_cross_layer` (node_mac.cpp, §team-ch-key T-K3), and exposing that arm's own handle is
//   ruled a separate behaviour change by the `return 0` note at its site (C1).
uint16_t Node::send_by_hash(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt, uint32_t reply_to_hash, uint16_t mobile_ctr, Plane plane, uint8_t type, bool suppress_intro, SendDispatch* out_dispatch) {
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
    const bool delegated_e2e = reply_to_hash != 0 && mobile_ctr != 0
                            && (flags & DATA_FLAG_E2E_ACK_REQ) && itype != DATA_TYPE_E2E_ACK;
    const bool delegated_origin = reply_to_hash != 0 && mobile_ctr != 0 && itype != DATA_TYPE_E2E_ACK;
    auto release_deleg_ack = [&]() {
        if (delegated_e2e)
            deleg_ack_release(reply_to_hash, mobile_ctr, DelegAckPeer::key_hash,
                              key_hash32, active_layer_id());
    };
    auto commit_deleg_ack = [&](uint16_t ctr_h, const SendDispatch* dispatch,
                                DelegAckPeer return_kind, uint32_t return_peer) {
        // B251 QG: enqueue_data deliberately returns its minted counter even when the queue is full. Only the
        // admission authority may create logical-origin evidence or turn the pre-ACK reservation ACTIVE. The
        // nullptr fallback preserves the pre-existing contract for callers that did not request dispatch evidence;
        // the MOBILE_SEND consumer now always supplies it.
        const bool admitted = dispatch ? dispatch->admit == SendDispatch::Admit::queued : ctr_h != 0;
        if (!admitted) {
            release_deleg_ack();
            return false;
        }
        if (delegated_origin) emit_deleg_originated(reply_to_hash, ctr_h, mobile_ctr);
        if (!delegated_e2e) return true;
        const bool ok = deleg_ack_put(reply_to_hash, ctr_h, mobile_ctr,
                                     DelegAckPeer::key_hash, key_hash32,
                                     return_kind, return_peer, active_layer_id());
        if (!ok) release_deleg_ack();
        return ok;
    };
#if MR_FEAT_TEAM
    // §6.4 HARD SPLIT: `send -t 0x<hash>` (TEAM plane) resolves a HEARD teammate from the team-key cache ONLY (beacon-only,
    // NO id_bind / home / H-flood / global fallback). An unheard teammate FAILS LOUD -> the app retries when a beacon arrives.
    if (plane == Plane::TEAM) {
        uint8_t tid = 0;
        if (_cfg.team_id != 0 && team_id_of_key(key_hash32, tid))
            // ★ `tid` IS THE SEND-TIME RESOLUTION (§UI-16 N6b): it is read from the team-key cache HERE, not frozen by
            //   a caller, so a re-DAD between a UI selection and this line lands the frame on the NEW id — which is
            //   why the dispatch carries the id `enqueue_data` really addressed rather than the one the caller held.
            return do_send(tid, sbody, sblen, flags, crypt, /*override_dst_hash=*/0, /*type=*/itype, /*override_source_hash=*/reply_to_hash, Plane::TEAM, out_dispatch);
        // §F-TR-1: an UNHEARD teammate (>1 hop away, not in the beacon-only team-key cache) is resolved by a TEAM-SCOPED H
        // flood (emit_hash_query TEAM => team_scoped + origin=team_local_id; the answer routes home via _rt_team) — mirrors
        // the off-grid AUTO team path (:~1146). EXPLICIT `-t` WINS over the home-delegation default: a REGISTERED dual member
        // reaches HERE (the plane check precedes the delegate branch below) and floods the TEAM plane itself — no home. Gated
        // on team_local_id()!=0 (a routable team origin); a team member with no adopted team id still fails loud below.
        if (_cfg.team_id != 0 && team_local_id() != 0) {
            // §UI-16 N6b: the park's OWN answer — `false` means the ring was full and this send was DROPPED, which
            // used to be indistinguishable from a stored park (both returned 0 here).
            const bool stored = park_send(key_hash32, sbody, sblen, flags, crypt, /*reply_to_hash=*/reply_to_hash, /*mobile_ctr=*/mobile_ctr, /*type=*/itype,
                                          /*reflood=*/true, /*reflood_hard=*/false, /*reflood_plane=*/Plane::TEAM);
            if (!stored) release_deleg_ack();
            if (out_dispatch) out_dispatch->admit = stored ? SendDispatch::Admit::parked : SendDispatch::Admit::refused;
            emit_hash_query(key_hash32, /*hard=*/false, /*want_pubkey=*/false, Plane::TEAM);   // §no-auto-reqpubkey (see the header note): want_pubkey stays FALSE, owner-ratified 2026-07-29
            return 0;
        }
        MR_EMIT("team_send_unresolved", EF_I("key_hash32", static_cast<int64_t>(key_hash32)));
        release_deleg_ack();
        push_send_failed(SendFailReason::mobile_no_home, /*dst=*/0, /*ctr=*/0);
        return 0;
    }
#endif
    IdBindConf conf = IdBindConf::claimed;
    const int id = id_bind_find_by_hash(key_hash32, &conf);
    if (id >= 0 && conf == IdBindConf::authoritative) {         // confident binding -> send NOW (a mobile still routes via its home; the reply returns by SOURCE_HASH -> no H-query, no storm)
        const uint16_t ch = do_send(static_cast<uint8_t>(id), sbody, sblen, flags, crypt, /*override_dst_hash=*/0, /*type=*/itype, /*override_source_hash=*/reply_to_hash, plane, out_dispatch);   // §8b: thread the per-message crypt intent + Wave 2 plane; §S2: itype threads an auto-attached INTRO
        (void)commit_deleg_ack(ch, out_dispatch, DelegAckPeer::node_id, static_cast<uint8_t>(id));
        return ch;
    }
#if MR_FEAT_TEAM
    // §mobile 6.4: AUTO cascade — try a HEARD teammate (team_key cache -> team_local_id, is_team_peer -> _rt_team) BEFORE the
    // global path. GLOBAL (a plain `send`) SKIPS this so a teammate never shadows the intended global target; TEAM was already
    // handled + returned at the top. Reached only after an id_bind miss (a mobile's hash isn't in id_bind).
    if (plane != Plane::GLOBAL && _cfg.is_mobile && _cfg.team_id != 0) {
        uint8_t tid = 0;
        if (team_id_of_key(key_hash32, tid))
            return do_send(tid, sbody, sblen, flags, crypt, /*override_dst_hash=*/0, /*type=*/itype, /*override_source_hash=*/reply_to_hash, /*plane=*/Plane::TEAM, out_dispatch);   // resolved to a teammate -> force the team plane
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
                           /*override_dst_hash=*/key_hash32, /*type=*/DATA_TYPE_MOBILE_SEND,
                           /*override_source_hash=*/0, /*plane=*/Plane::AUTO, out_dispatch);
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
                           /*override_dst_hash=*/key_hash32, /*type=*/DATA_TYPE_MOBILE_SEND,
                           /*override_source_hash=*/0, /*plane=*/Plane::AUTO, out_dispatch);
        }
        return do_send(_my_mobile_reg.home_id, sbody, sblen, flags, crypt, /*override_dst_hash=*/key_hash32, /*type=*/DATA_TYPE_MOBILE_SEND,
                       /*override_source_hash=*/0, /*plane=*/Plane::AUTO, out_dispatch);
    }
    // §mobile: a mobile WE HOST (in our _mobile_reg) is reached by a DIRECT last-mile (addr_len=1 -> its local id), NOT an H
    // query — the home is BOTH the querier and the proxy, so a flood deadlocks (the registered mobile suppresses its own-hash
    // H answer, node_hashlocate.cpp handle_h). Mirrors do_post_ack's forwarded last-mile. Gated on _mobile_reg_n -> non-host byte-identical.
    // ★★★ §MH-S5-FIX2 (owner-ruled 2026-08-10, ledger §1.14) — `host_row_live_direct(i)` REPLACES the bare
    // `redirect_home_id == 0` here. This is the LOCALLY-ORIGINATED twin of the forwarded last mile §MH-S5-FIX already
    // gated in `node_mac_rx.cpp`, and the two treated the SAME EXPIRED ROW DIFFERENTLY — a forwarded DM refused it
    // while `send`-ing to the same hash from this node's own console still aired a 1-hop frame at a mobile 25 minutes
    // silent. One boundary, every service path (spec §9.1/§9.2 + §9.3's no-duplicate-predicates rule).
    // ⓘ WHERE A REFUSED ROW GOES, unchanged from the redirect case: straight on to `mobile_home_find` below, i.e. the
    //   hash plane / the cached home — which is exactly what a migrated mobile has always done here.
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == key_hash32 && host_row_live_direct(i)) {
            const uint16_t lch = enqueue_data(_active->_mobile_reg[i].mobile_local_id, sbody, sblen, flags, "tx_enqueue", /*app_dm=*/true,
                                              /*type=*/itype, crypt, /*override_dst_hash=*/0, /*override_source_hash=*/reply_to_hash, /*addr_len=*/1, plane, out_dispatch);
            // ★ §xl-deleg-ack: the THIRD site that stamped the mobile's SOURCE_HASH without mapping ctr_H->ctr_M. Reached
            // when a home hosts BOTH the delegating mobile and the target: the target acks to us with DST_HASH = M, and the
            // hosted-mobile last-mile fork's deleg_ack_translate missed, so M received the HOME's ctr. Same one-line shape.
            (void)commit_deleg_ack(lch, out_dispatch, DelegAckPeer::node_id, _node_id);
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
            if (xch != 0) (void)commit_deleg_ack(xch, nullptr, DelegAckPeer::key_hash, key_hash32);
            else          release_deleg_ack();
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
        const uint16_t hch = do_send(static_cast<uint8_t>(home), sbody, sblen, flags, crypt, /*override_dst_hash=*/key_hash32, /*type=*/itype, /*override_source_hash=*/reply_to_hash, /*plane=*/Plane::AUTO, out_dispatch);
        (void)commit_deleg_ack(hch, out_dispatch, DelegAckPeer::node_id, static_cast<uint8_t>(home));
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
        const bool stored = park_send(key_hash32, sbody, sblen, flags, crypt, /*reply_to_hash=*/reply_to_hash, /*mobile_ctr=*/mobile_ctr, /*type=*/itype,
                                      /*reflood=*/true, /*reflood_hard=*/false, /*reflood_plane=*/Plane::TEAM);   // §F-SL-1: a quiet-net team flood miss re-tries before giveup
        if (out_dispatch) out_dispatch->admit = stored ? SendDispatch::Admit::parked : SendDispatch::Admit::refused;   // §UI-16 N6b
        emit_hash_query(key_hash32, /*hard=*/false, /*want_pubkey=*/false, Plane::TEAM);   // §no-auto-reqpubkey (see the header note)
        return 0;
    }
#endif
    const bool parked_ok = park_send(key_hash32, sbody, sblen, flags, crypt, /*reply_to_hash=*/reply_to_hash, /*mobile_ctr=*/mobile_ctr, /*type=*/itype,
                                     /*reflood=*/true, /*reflood_hard=*/(id >= 0), /*reflood_plane=*/plane);   // §F-SL-1: bounded jittered retry so a re-homed contact re-resolves in a quiet net
    if (!parked_ok) release_deleg_ack();
    if (out_dispatch) out_dispatch->admit = parked_ok ? SendDispatch::Admit::parked : SendDispatch::Admit::refused;   // §UI-16 N6b
    emit_hash_query(key_hash32, /*hard=*/(id >= 0), /*want_pubkey=*/false, plane);   // Wave 2: GLOBAL flood is NOT team-scoped; AUTO keeps today's behavior. §no-auto-reqpubkey (see the header note): a CRYPTED send to an unresolved hash fails loud with no_pubkey — it does NOT escalate to WANT_PUBKEY
    return 0;
}

// B251 reverse-ACK correlation. The old ring keyed only (mobile_hash,ctr_H) even though next_ctr is destination-scoped,
// and overwrote the oldest LIVE row when full. Both properties can misdeliver an ACK. The strengthened ring has an
// admission phase (RESERVED before the mobile's hop ACK) and an ACTIVE phase keyed by what the returning ACK actually
// exposes. Its TTL is the existing delegated/cross-layer E2E deadline, not the old 180 s literal (the real deadline is
// 300 s). Every scan prunes first; no live row is ever evicted.
static constexpr uint64_t kDelegAckTtlMs = protocol::e2e_ack_deadline_xl_ms;

void Node::emit_deleg_originated(uint32_t mobile_hash, uint16_t ctr_h, uint16_t ctr_m) {
    if (mobile_hash == 0 || ctr_h == 0 || ctr_m == 0) return;
    // Application-identity evidence is independent of the reverse-ACK ring: a delegated non-E2E send still
    // has a mobile logical origin, but must not consume one of the eight correlation rows.
    MR_EMIT("deleg_originated", EF_I("mobile_hash", static_cast<int64_t>(mobile_hash)),
            EF_I("ctr_h", ctr_h), EF_I("ctr_m", ctr_m));
}

bool Node::deleg_ack_reserve(uint32_t mobile_hash, uint16_t ctr_m, DelegAckPeer target_kind,
                            uint32_t target, uint8_t layer, uint8_t& out_slot, uint32_t& retry_ms) {
    out_slot = kDelegAckNoSlot;
    retry_ms = protocol::nack_busy_quantum_ms;
    if (mobile_hash == 0 || ctr_m == 0 || target == 0) return false;
    const uint64_t now = _hal.now();
    uint8_t free_slot = kDelegAckNoSlot;
    uint64_t earliest_expiry = UINT64_MAX;
    for (uint8_t i = 0; i < kDelegAckCap; ++i) {
        DelegAck& e = _deleg_acks[i];
        if (e.state != DelegAckState::free && now - e.ts_ms >= kDelegAckTtlMs) e = DelegAck{};
        if (e.state == DelegAckState::reserved
            && e.mobile_hash == mobile_hash && e.ctr_m == ctr_m
            && e.peer_kind == target_kind && e.peer == target && e.layer == layer) {
            e.ts_ms = now;                                      // exact retry: keep the same reservation
            out_slot = i;
            return true;
        }
        if (e.state == DelegAckState::free) {
            if (free_slot == kDelegAckNoSlot) free_slot = i;
        } else {
            const uint64_t expiry = e.ts_ms + kDelegAckTtlMs;
            if (expiry < earliest_expiry) earliest_expiry = expiry;
        }
    }
    if (free_slot == kDelegAckNoSlot) {
        if (earliest_expiry > now) {
            const uint64_t wait = earliest_expiry - now;
            retry_ms = static_cast<uint32_t>(wait > UINT32_MAX ? UINT32_MAX : wait);
        }
        return false;
    }
    DelegAck& e = _deleg_acks[free_slot];
    e.ts_ms = now; e.mobile_hash = mobile_hash; e.peer = target;
    e.ctr_h = 0; e.ctr_m = ctr_m; e.layer = layer;
    e.peer_kind = target_kind; e.state = DelegAckState::reserved;
    out_slot = free_slot;
    MR_EMIT("deleg_ack_reserved", EF_I("mobile_hash", static_cast<int64_t>(mobile_hash)),
            EF_I("ctr_m", ctr_m), EF_I("target", static_cast<int64_t>(target)), EF_I("layer", layer));
    return true;
}

bool Node::deleg_ack_activation_available(uint8_t slot, uint16_t ctr_h, DelegAckPeer return_kind,
                                         uint32_t return_peer, uint8_t layer, uint32_t& retry_ms) const {
    retry_ms = protocol::nack_busy_quantum_ms;
    if (slot >= kDelegAckCap || ctr_h == 0 || return_peer == 0
        || _deleg_acks[slot].state != DelegAckState::reserved) return false;
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < kDelegAckCap; ++i) {
        if (i == slot) continue;
        const DelegAck& e = _deleg_acks[i];
        if (e.state != DelegAckState::active || now - e.ts_ms >= kDelegAckTtlMs) continue;
        if (e.mobile_hash == _deleg_acks[slot].mobile_hash && e.ctr_h == ctr_h
            && e.peer_kind == return_kind && e.peer == return_peer && e.layer == layer) {
            const uint64_t wait = kDelegAckTtlMs - (now - e.ts_ms);
            retry_ms = static_cast<uint32_t>(wait > UINT32_MAX ? UINT32_MAX : wait);
            return false;                                       // two live rows would be wire-indistinguishable
        }
    }
    return true;
}

bool Node::deleg_ack_activate(uint8_t slot, uint16_t ctr_h, DelegAckPeer return_kind,
                             uint32_t return_peer, uint8_t layer) {
    uint32_t ignored_retry_ms = 0;
    if (!deleg_ack_activation_available(slot, ctr_h, return_kind, return_peer, layer,
                                        ignored_retry_ms)) return false;
    DelegAck& e = _deleg_acks[slot];
    e.ts_ms = _hal.now(); e.ctr_h = ctr_h; e.peer = return_peer; e.layer = layer;
    e.peer_kind = return_kind; e.state = DelegAckState::active;
    MR_EMIT("deleg_ack_put", EF_I("mobile_hash", static_cast<int64_t>(e.mobile_hash)),
            EF_I("ctr_h", ctr_h), EF_I("ctr_m", e.ctr_m),
            EF_I("peer", static_cast<int64_t>(return_peer)), EF_I("layer", layer));
    return true;
}

bool Node::deleg_ack_put(uint32_t mobile_hash, uint16_t ctr_h, uint16_t ctr_m,
                        DelegAckPeer target_kind, uint32_t target,
                        DelegAckPeer return_kind, uint32_t return_peer, uint8_t layer) {
    if (mobile_hash == 0 || ctr_h == 0 || ctr_m == 0 || target == 0 || return_peer == 0) return false;
    const uint64_t now = _hal.now();
    uint8_t free_slot = kDelegAckNoSlot;
    for (uint8_t i = 0; i < kDelegAckCap; ++i) {
        DelegAck& e = _deleg_acks[i];
        if (e.state != DelegAckState::free && now - e.ts_ms >= kDelegAckTtlMs) e = DelegAck{};
        if (e.state == DelegAckState::reserved && e.mobile_hash == mobile_hash && e.ctr_m == ctr_m
            && e.peer_kind == target_kind && e.peer == target && e.layer == layer)
            return deleg_ack_activate(i, ctr_h, return_kind, return_peer, layer);
        if (e.state == DelegAckState::active && e.mobile_hash == mobile_hash && e.ctr_h == ctr_h
            && e.peer_kind == return_kind && e.peer == return_peer && e.layer == layer) {
            if (e.ctr_m != ctr_m) return false;                    // same return key, different answer: ambiguous
            e.ts_ms = now;                                        // exact active refresh, never a replacement
            MR_EMIT("deleg_ack_put", EF_I("mobile_hash", static_cast<int64_t>(mobile_hash)),
                    EF_I("ctr_h", ctr_h), EF_I("ctr_m", ctr_m),
                    EF_I("peer", static_cast<int64_t>(return_peer)), EF_I("layer", layer));
            return true;
        }
        if (e.state == DelegAckState::free && free_slot == kDelegAckNoSlot) free_slot = i;
    }
    if (free_slot == kDelegAckNoSlot) {
        ++_mobile_ctr_admission_refused_n;
        MR_EMIT("deleg_ack_put_refused", EF_I("mobile_hash", static_cast<int64_t>(mobile_hash)),
                EF_I("ctr_h", ctr_h), EF_I("ctr_m", ctr_m));
        return false;                                             // every row is live: never evict one
    }
    DelegAck& e = _deleg_acks[free_slot];
    e.ts_ms = now; e.mobile_hash = mobile_hash; e.peer = return_peer;
    e.ctr_h = ctr_h; e.ctr_m = ctr_m; e.layer = layer;
    e.peer_kind = return_kind; e.state = DelegAckState::active;
    MR_EMIT("deleg_ack_put", EF_I("mobile_hash", static_cast<int64_t>(mobile_hash)),
            EF_I("ctr_h", ctr_h), EF_I("ctr_m", ctr_m),
            EF_I("peer", static_cast<int64_t>(return_peer)), EF_I("layer", layer));
    return true;
}

void Node::deleg_ack_release(uint32_t mobile_hash, uint16_t ctr_m, DelegAckPeer target_kind,
                            uint32_t target, uint8_t layer) {
    for (DelegAck& e : _deleg_acks)
        if (e.state == DelegAckState::reserved && e.mobile_hash == mobile_hash && e.ctr_m == ctr_m
            && e.peer_kind == target_kind && e.peer == target && e.layer == layer) {
            e = DelegAck{};
            return;
        }
}

bool Node::deleg_ack_translate(uint32_t mobile_hash, uint16_t acked_ctr, DelegAckPeer return_kind,
                              uint32_t return_peer, uint8_t layer, uint16_t& out_mobile_ctr) {
    const uint64_t now = _hal.now();
    for (DelegAck& e : _deleg_acks) {
        if (e.state != DelegAckState::free && now - e.ts_ms >= kDelegAckTtlMs) e = DelegAck{};
        if (e.state != DelegAckState::active) continue;
        if (e.mobile_hash == mobile_hash && e.ctr_h == acked_ctr
            && e.peer_kind == return_kind && e.peer == return_peer && e.layer == layer) {
            out_mobile_ctr = e.ctr_m;
            e = DelegAck{};                                      // one-shot: this ACK consumed the correlation
            return true;
        }
    }
    return false;
}

// Originate an H flood for key_hash32 (Lua send_hash_query dv:5625). hard = the verify-on-use escalation.
// ★★★ §id-hash S1b (QA P1c): it RETURNS its disposition now. See Node::HQueryOutcome for why — in short, this
// function has four silent early-outs and its reqpubkey caller was reporting all of them to the app as a flood.
// ⚠ The three OTHER callers (node_join.cpp:468, node_mac_rx.cpp:1384, node.cpp's send_layer arm) deliberately keep
// ignoring the value: they are best-effort locates whose failure is already handled by a park/timeout, and giving
// them outcome handling would be a behaviour change in three unrelated paths (C1). If one of them ever needs it,
// the value is now there to read.
Node::HQueryOutcome Node::emit_hash_query(uint32_t query_key32, bool hard, bool want_pubkey, Plane plane, bool by_id) {
    // ★ §id-hash S4a: the BY_ID degeneracy tests are the by-hash ones re-stated in the id key space — a
    // non-canonical id (0 / 255 / upper bytes set) and OUR OWN id are both "nothing to locate". The self test is
    // per-PLANE: a team-scoped by-id query for our own `team_local_id()` is as degenerate as a static one for
    // `_node_id`. Catching it HERE rather than at pack_h is what keeps the operator's error honest (`degenerate` ->
    // err_unsupported), because pack_h's refusal maps to `encode_failed` -> err_too_large, a wrong diagnosis.
    if (by_id) {
        const bool team_q_self = (plane == Plane::TEAM) && team_local_id() != 0;
        const uint8_t self_id  = team_q_self ? team_local_id() : _node_id;
        if (!h_by_id_key_canonical(query_key32) || query_key32 == self_id) return HQueryOutcome::degenerate;
    }
    else if (query_key32 == 0 || query_key32 == _key_hash32) return HQueryOutcome::degenerate;   // nothing to locate (degenerate / it's us)
    if (want_pubkey && !_crypto_ready) {                         // §2: the mutual exchange needs OUR pubkey -> fail loud, no flood
        MR_EMIT("h_want_pubkey_no_identity", EF_I("key_hash32", static_cast<int64_t>(query_key32)));
        return HQueryOutcome::no_identity;
    }
    h_in in{};
    // §P2-1: for a TEAM-scoped H (team_q below) in.leaf_id is ADVISORY — the receiver's handle_h skips the leaf gate for a
    // team-scoped frame (membership is team_id), so a mixed-leaf team resolves across nibbles. We still stamp our own leaf so
    // a same-leaf teammate's frame is unremarkable and any static overhearer sees a well-formed nibble. A STATIC H's leaf_id
    // remains load-bearing (the receiver leaf-gates it).
    in.leaf_id = _cfg.leaf_id; in.origin = _node_id; in.query_key32 = query_key32; in.by_id = by_id;
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
    // ★ §id-hash S4a WIDENS THIS GUARD TO `by_id`, and it is the same defect not a new one: the by-id ANSWER is the
    // same routed DATA back to `in.origin`, so an unregistered mobile asking a GLOBAL by-id question has exactly the
    // same no-way-back problem — and unlike the best-effort by-hash locate (which a park/timeout covers) this one is
    // operator-initiated and must report. B47's class, reached through the new door.
    if ((want_pubkey || by_id) && in.mobile_req && in.origin == _node_id && !in.team_scoped) {
        MR_EMIT("h_want_pubkey_mobile_no_route", EF_I("key_hash32", static_cast<int64_t>(query_key32)));
        return HQueryOutcome::no_return_route;
    }
    uint8_t buf[8 + 32 + 4 + 1 + 32];                            // §2: WANT_PUBKEY H = 40 B; §mobile 6.2: +4 B team_id; §name: +1+name_len (max 33) -> a named team_scoped WANT_PUBKEY is up to 77 B
    const size_t n = pack_h(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return HQueryOutcome::encode_failed;
    // the originate (dv:5625)
    // ⚠ THE `h_tx` EMIT STAYS BEFORE THE HAND-OFF, deliberately: moving it after would reorder it against
    // `tx_lbt_defer` / `tx_lbt_defer_dropped` and re-anchor scenario streams — a telemetry re-anchor folded into a
    // behaviour fix is exactly what C4/C1 forbid. It means "we originated an H"; the drop, when it happens, is
    // reported by the very next line's `tx_lbt_defer_dropped` and by the outcome below.
    MR_EMIT("h_tx", EF_I("key_hash32", static_cast<int64_t>(query_key32)), EF_I("ttl", protocol::hash_query_max_ttl), EF_B("hard", hard));
    // ★★ §id-hash S1c (QA round 2): tx_initiating's `bool` was discarded ONE LAYER DOWN, so a full LBT defer ring
    // dropped the frame and this still answered `sent`. Spec §5.1: `reqpubkey_sent` "must not be reachable from any
    // bail point" — and that was one.
    if (!tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0)) return HQueryOutcome::tx_dropped;
    return HQueryOutcome::sent;
}

// ★★ §UI-16 N6b (2026-08-24): it RETURNS its disposition — `false` = the ring was FULL and this send was DROPPED.
// ⛔ THE BEHAVIOUR IS UNCHANGED (the drop was there before and stays; the app still retries); what changed is that
// the answer is no longer thrown away one frame up. Same shape and same reason as `emit_hash_query`'s §id-hash S1b
// return: a function with a silent early-out whose caller was reporting that early-out to the app as a success.
// ⓘ Every pre-existing caller ignores the value deliberately — they are best-effort parks whose failure is already
//   covered by the H re-flood and the giveup, and giving them outcome handling would be a behaviour change (C1).
bool Node::park_send(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt, uint32_t reply_to_hash, uint16_t mobile_ctr, uint8_t type, bool reflood, bool reflood_hard, Plane reflood_plane) {
    if (_parked_sends_n >= protocol::cap_parked_sends) return false;   // full -> drop (the app can retry)
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
    return true;
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
                if (p.reply_to_hash != 0 && pch != 0 && p.type != DATA_TYPE_E2E_ACK) {
                    emit_deleg_originated(p.reply_to_hash, pch, p.mobile_ctr);
                    if (p.flags & DATA_FLAG_E2E_ACK_REQ)
                        (void)deleg_ack_put(p.reply_to_hash, pch, p.mobile_ctr,
                                            DelegAckPeer::key_hash, p.key_hash32,
                                            DelegAckPeer::key_hash, p.key_hash32, active_layer_id());
                }
            } else {
                MR_EMIT("send_hash_resolved", EF_I("key_hash32", static_cast<int64_t>(key_hash32)), EF_I("node", resolved_id));
                // same-layer (incl. a cross_layer park whose dst turned out to be on OUR leaf, §5.1): a plain DM.
                SendDispatch dispatch{};
                const uint16_t ch = do_send(resolved_id, p.body, p.body_len, p.flags, p.crypt, /*override_dst_hash=*/p.key_hash32, /*type=*/p.type, /*override_source_hash=*/p.reply_to_hash, Plane::AUTO, &dispatch);   // §S2: p.type re-originates a parked INTRO with its TYPE (0 = plain, byte-identical); load-bearing (OUTSIDE the wrap): fly the held DM; M3: thread crypt; §mobile 3c: carry the queried hash so even the FIRST flood-resolved send to a mobile stamps DST_HASH=M (home forwards, not consumes); §mobile delegate: reply_to_hash -> SOURCE_HASH so the target's reply routes back to the mobile. For a normal send p.key_hash32 == key_hash_of_id(resolved_id) + reply_to_hash==0 -> byte-identical.
                const bool delegated_e2e = p.reply_to_hash != 0 && (p.flags & DATA_FLAG_E2E_ACK_REQ)
                                        && p.type != DATA_TYPE_E2E_ACK;
                if (dispatch.admit == SendDispatch::Admit::refused && delegated_e2e) {
                    _parked_sends[w++] = p;                      // queue full: keep both the send and its reservation
                    continue;
                }
                if (p.reply_to_hash != 0 && dispatch.admit == SendDispatch::Admit::queued
                    && p.type != DATA_TYPE_E2E_ACK) {
                    emit_deleg_originated(p.reply_to_hash, ch, p.mobile_ctr);
                    if (delegated_e2e && !deleg_ack_put(p.reply_to_hash, ch, p.mobile_ctr,
                                                       DelegAckPeer::key_hash, p.key_hash32,
                                                       DelegAckPeer::node_id, resolved_id, active_layer_id()))
                        deleg_ack_release(p.reply_to_hash, p.mobile_ctr, DelegAckPeer::key_hash,
                                          p.key_hash32, active_layer_id());
                } else if (delegated_e2e && dispatch.admit != SendDispatch::Admit::refused) {
                    deleg_ack_release(p.reply_to_hash, p.mobile_ctr, DelegAckPeer::key_hash,
                                      p.key_hash32, active_layer_id());
                }
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
                SendDispatch dispatch{};
                const uint16_t ch = do_send(static_cast<uint8_t>(id), p.body, p.body_len, p.flags, p.crypt, /*override_dst_hash=*/0, /*type=*/p.type, /*override_source_hash=*/p.reply_to_hash, Plane::AUTO, &dispatch);   // §S2: p.type re-originates a parked INTRO with its TYPE (0 = plain, byte-identical); load-bearing (OUTSIDE the wrap); M3: thread the stamped crypt intent (a beacon-resolved parked sendhashx still flies CRYPTED); §mobile delegate: reply_to_hash -> SOURCE_HASH
                const bool delegated_e2e = p.reply_to_hash != 0 && (p.flags & DATA_FLAG_E2E_ACK_REQ)
                                        && p.type != DATA_TYPE_E2E_ACK;
                if (dispatch.admit == SendDispatch::Admit::refused && delegated_e2e) {
                    _parked_sends[w++] = p;                      // retry after capacity returns; reservation stays RESERVED
                    continue;
                }
                if (p.reply_to_hash != 0 && dispatch.admit == SendDispatch::Admit::queued
                    && p.type != DATA_TYPE_E2E_ACK) {
                    emit_deleg_originated(p.reply_to_hash, ch, p.mobile_ctr);
                    if (delegated_e2e && !deleg_ack_put(p.reply_to_hash, ch, p.mobile_ctr,
                                                       DelegAckPeer::key_hash, p.key_hash32,
                                                       DelegAckPeer::node_id, static_cast<uint8_t>(id), active_layer_id()))
                        deleg_ack_release(p.reply_to_hash, p.mobile_ctr, DelegAckPeer::key_hash,
                                          p.key_hash32, active_layer_id());
                } else if (delegated_e2e && dispatch.admit != SendDispatch::Admit::refused) {
                    deleg_ack_release(p.reply_to_hash, p.mobile_ctr, DelegAckPeer::key_hash,
                                      p.key_hash32, active_layer_id());
                }
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
                if (p.reply_to_hash != 0 && (p.flags & DATA_FLAG_E2E_ACK_REQ) && p.type != DATA_TYPE_E2E_ACK)
                    deleg_ack_release(p.reply_to_hash, p.mobile_ctr, DelegAckPeer::key_hash,
                                      p.key_hash32, active_layer_id());
                MR_EMIT("send_hash_giveup", EF_I("key_hash32", static_cast<int64_t>(p.key_hash32)));
            }
            continue;                                            // drop (handled: reported / gave up)
        }
        _parked_sends[w++] = p;
    }
    _parked_sends_n = w;
}

// ============================================================================================================
// ★★★ §id-hash S4b (spec 2026-08-01 §5) — THE TWO-STAGE by-id `reqpubkey`, i.e. ONE command again.
//
// S4a made `reqpubkey <unresolved id>` fly a BY_ID query instead of refusing, but it flew the WRONG STAGE'S query:
// the answer is an id->hash BINDING, never a key, so the operator had to run the verb a SECOND time once the binding
// landed. These four functions close that: arm a bounded intent at stage 1, consume the answer, emit the existing
// HARD WANT_PUBKEY query by the returned hash, and give up loudly if no answer comes.
//
// ★★★ THIS IS NOT THE AUTO-RESOLUTION `§no-auto-reqpubkey` FORBIDS, AND THE DISTINCTION IS EXACT. That ruling
// (owner-ratified 2026-07-29, stated in full at send_by_hash above) forbids a **send** silently escalating into a
// WANT_PUBKEY locate — because that would substitute on-air TOFU for the QR ceremony on a message the user marked
// `-e`, making the trust decision FOR them, invisibly. Here:
//   · the operator typed `reqpubkey`, the verb whose entire purpose is that on-air fetch — stage 2 is the COMPLETION
//     of the command they issued, not a new decision taken on their behalf;
//   · it is BOUNDED (one intent, one escalation, protocol::id_pubkey_intent_ttl_ms) and REPORTED (below);
//   · it escalates NOTHING else: no send path, no seal, no `team grantkey` reaches this code.
// ⇒ a future reader must not "fix" this by deleting it. The four `want_pubkey = false` locate sites the ruling names
//   are untouched and still carry their tags.
//
// ⚠ THE HONEST COST, recorded by the spec and not hidden: TWO query/answer exchanges when the binding is absent.
// ============================================================================================================

// Refresh-or-insert. Returns false ONLY when the ring is full of OTHER live intents — the caller must then refuse
// BEFORE spending airtime, because a query we cannot remember asking is a query whose answer we will discard.
// Re-issuing the SAME (id, plane) refreshes the deadline instead of consuming a second slot (park_resolve_request's
// precedent): a retry is the operator re-asking one question, not asking two.
bool Node::id_pubkey_intent_arm(uint8_t id, uint8_t plane) {
    if (id == 0) return false;                                   // structurally impossible (canonical gate) — fail closed
    for (auto& e : _pending_id_pubkey)
        if (e.id == id && e.plane == plane) { e.deadline_ms = _hal.now() + protocol::id_pubkey_intent_ttl_ms; return true; }
    for (auto& e : _pending_id_pubkey)
        if (e.id == 0) { e.id = id; e.plane = plane; e.deadline_ms = _hal.now() + protocol::id_pubkey_intent_ttl_ms; return true; }
    // FULL. ★ REFUSE, NEVER EVICT — an evicted intent is a request the operator was told had been accepted and that
    // then dies in silence, which is the exact class this arc spent six review rounds removing. cap_pending_id_pubkey
    // documents why the bound is airtime rather than RAM.
    MR_EMIT("reqpubkey_intent_ring_full", EF_I("node", id), EF_I("plane", plane));
    return false;
}

// Undo an arm. The ONE caller is the stage-1 command path, when `emit_hash_query` then reports a non-`sent` outcome:
// arming precedes the emit (the ring-full refusal must beat the airtime), so a rejected frame must not leave an
// intent behind — it would wait out its whole TTL and then report a timeout for a query that never flew.
void Node::id_pubkey_intent_clear(uint8_t id, uint8_t plane) {
    for (auto& e : _pending_id_pubkey)
        if (e.id == id && e.plane == plane) { e = PendingIdPubkey{}; return; }
}

// Spec §5 step 3: an id->hash answer just landed -> if we were waiting for exactly this (id, plane), consume the
// intent and emit the EXISTING HARD WANT_PUBKEY query by the hash the answer returned. Step 4 (the pubkey answer
// self-verifying into `_peer_keys` as authoritative) is `on_hash_bind_pubkey`, unchanged — S4b adds no trust
// semantics, it only stops making the operator type the second command.
//
// ⓘ IT KEYS ON THE BINDING ARRIVING, NOT ON "OUR BY-ID QUERY BEING ANSWERED", and that is deliberately wider than the
//   spec's wording: an ordinary by-HASH answer that happens to bind the same id answers the same question we asked,
//   so consuming it is correct and strictly faster. The intent carries no query nonce to match against anyway.
// ⚠ NOT hooked into `on_hash_bind_snoop` (the relay observation): a snooped answer is addressed to someone else. If it
//   were addressed to us it would arrive here instead.
// ⚠ NOT hooked into the BEACON id_bind/team_key writers either, and that is a KNOWN RESIDUAL rather than an
//   oversight: a beacon that resolves the id while an intent is pending does not escalate, so the operator gets the
//   bounded timeout below and a re-issued `reqpubkey <id>` then resolves immediately from the beacon-learned row. The
//   hook would sit on the hottest corpus path in the tree (node_beacon), re-anchoring every stream for an ergonomic
//   gain on the case the by-id query exists precisely because it does NOT cover (routable but never heard).
void Node::id_pubkey_intent_consume(uint8_t id, bool team_plane, uint32_t key_hash32) {
    const uint8_t plane = static_cast<uint8_t>(team_plane ? Plane::TEAM : Plane::GLOBAL);
    for (auto& e : _pending_id_pubkey) {
        if (e.id != id || e.plane != plane) continue;
        e = PendingIdPubkey{};                                   // CONSUMED: one intent buys exactly one escalation
        // The stage-2 query is the pre-existing one, byte-for-byte: HARD + WANT_PUBKEY, keyed BY HASH, on the same
        // plane the operator selected. `by_id = false` — the id question is answered; asking it again would be the
        // one-round form spec §5 explicitly does not design (and `pack_h` refuses BY_ID|WANT_PUBKEY outright).
        const HQueryOutcome oc = emit_hash_query(key_hash32, /*hard=*/true, /*want_pubkey=*/true,
                                                 static_cast<Plane>(plane), /*by_id=*/false);
        if (oc == HQueryOutcome::sent) return;
        // ★★ SPEC §5.2's RULE, one level up: the disposition must reach the last thing that can discard it, and the
        // synchronous ack for this command was returned SECONDS AGO. Stage 2 can still fail — `degenerate` (the answer
        // named our own hash), `tx_dropped` (a bounded TX queue), `encode_failed` — so the failure is REPORTED, never
        // swallowed. `no_identity` cannot reach here: the command path pre-flights `_crypto_ready` precisely because
        // it is the one stage-2 precondition knowable at stage 1 (node.cpp).
        // ⚠ `_hal.log` WITH THE `!!` PREFIX, not MR_EMIT alone, and the reason is a QA finding this arc already paid
        //   for once (S1d/P2): MR_EMIT is stripped on the device, and `fw_main`'s log sink is otherwise trace-gated —
        //   so an unprefixed report is invisible on metal under normal `debug off` operation. The prefix is what makes
        //   "no silent loss" true rather than merely written down.
        MR_EMIT("reqpubkey_escalate_failed", EF_I("node", id), EF_I("key_hash32", static_cast<int64_t>(key_hash32)),
                EF_I("outcome", static_cast<int64_t>(oc)));
        char b[96];
        snprintf(b, sizeof b, "!! reqpubkey %u: id resolved to %08lX but the pubkey request was NOT sent (rc=%u)",
                 static_cast<unsigned>(id), static_cast<unsigned long>(key_hash32), static_cast<unsigned>(oc));
        _hal.log(b);
        return;
    }
}

// Spec §5 step 5: the bounded timeout. Periodic on kAgingTimerId, beside age_out_parked_sends — the SAME sweep for the
// same question ("a hash-locate that never came back"), which is why this needs no timer id of its own. The effective
// window is [id_pubkey_intent_ttl_ms, + rt_aging_check_period_ms]; protocol_constants.h states it and the operator
// line below does not pretend to be tighter than it is.
void Node::age_out_pending_id_pubkey() {
    const uint64_t now = _hal.now();
    for (auto& e : _pending_id_pubkey) {
        if (e.id == 0 || now < e.deadline_ms) continue;
        MR_EMIT("reqpubkey_id_giveup", EF_I("node", e.id), EF_I("plane", e.plane));   // mirrors send_hash_giveup's shape
        char b[112];
        snprintf(b, sizeof b, "!! reqpubkey %u: nobody answered \"who owns id %u\" on the %s plane within ~%us — no pubkey was requested",
                 static_cast<unsigned>(e.id), static_cast<unsigned>(e.id),
                 e.plane == static_cast<uint8_t>(Plane::TEAM) ? "team" : "static",
                 static_cast<unsigned>(protocol::id_pubkey_intent_ttl_ms / 1000));
        _hal.log(b);                                             // `!!` = operator-critical: prints on metal under `debug off` (S1d/P2)
        e = PendingIdPubkey{};
    }
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
