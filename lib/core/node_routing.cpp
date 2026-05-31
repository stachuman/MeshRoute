// MeshRoute — lib/core/node_routing.cpp  (DV route table + R2 route-plane hardening)
//
// Node methods for the bounded distance-vector route table: find/insert/remove,
// the merge + sort + strictly-better comparator, the rt_full telemetry, and the R2
// aging/TTL eviction + 3-cycle prune. Behaviour mirrors dv_dual_sf.lua. Part of the
// Node class (declared in node.h); split out of node.cpp for readability.
#include "node.h"

namespace meshroute {

// ---- route table ------------------------------------------------------------

RtEntry* Node::rt_find(uint8_t dest) {
    for (uint8_t i = 0; i < _rt_count; ++i) {
        if (_rt[i].dest == dest) return &_rt[i];
        if (_rt[i].dest > dest)  return nullptr;          // sorted ascending
    }
    return nullptr;
}

RtEntry* Node::rt_insert(uint8_t dest) {
    if (_rt_count >= protocol::cap_routes) return nullptr;
    uint8_t pos = 0;
    while (pos < _rt_count && _rt[pos].dest < dest) ++pos;
    for (uint8_t i = _rt_count; i > pos; --i) _rt[i] = _rt[i - 1];   // shift right
    _rt[pos]      = RtEntry{};
    _rt[pos].dest = dest;
    _rt_count++;
    return &_rt[pos];
}

bool Node::route_strictly_better(const RtCandidate& a, const RtCandidate& b) const {
    // effective_score == score until R4 (budget/suspect penalties are 0).
    const int16_t av = a.score;
    const int16_t bv = b.score;
    const bool a_viable = av >= _routing_snr_floor_q4;
    const bool b_viable = bv >= _routing_snr_floor_q4;
    if (a_viable != b_viable) return a_viable;            // viable beats non-viable
    if (a_viable) {                                       // both viable: hops-asc, score-desc
        if (a.hops != b.hops) return a.hops < b.hops;
        if (av != bv)         return av > bv;
    } else {                                              // both non-viable: score-desc, hops-asc
        if (av != bv)         return av > bv;
        if (a.hops != b.hops) return a.hops < b.hops;
    }
    // True tie (same viability/hops/score) -> NOT strictly better, matching the Lua
    // route_strictly_better (dv_dual_sf.lua:4227-4245), which has NO id tie-break: on
    // a tie both less(a,b) and less(b,a) are false, so the stable insertion sort
    // (sort_candidates) + rt_merge keep INSERTION order. An ascending-next_hop tie-break
    // here would DIVERGE from the Lua reference (and leak into rt_merge's full-table
    // eviction, changing the stored candidate set on a tie), so we faithfully preserve
    // insertion order. (The cascade gate uses well-separated SNRs so ties never decide.)
    return false;
}

void Node::sort_candidates(RtEntry& e) {
    // Lua comparator (dv_dual_sf.lua:4248-4252):
    //   less(a,b) = strictly_better(a,b) or (not strictly_better(b,a) and score(a) > score(b))
    // Insertion sort (n <= K = 3), stable, deterministic.
    for (uint8_t i = 1; i < e.n; ++i) {
        RtCandidate key = e.candidates[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0) {
            const RtCandidate& cur = e.candidates[j];
            const bool key_less = route_strictly_better(key, cur) ||
                                  (!route_strictly_better(cur, key) && key.score > cur.score);
            if (!key_less) break;
            e.candidates[j + 1] = e.candidates[j];
            --j;
        }
        e.candidates[j + 1] = key;
    }
}

Node::MergeAction Node::rt_merge(uint8_t dest, const RtCandidate& cand) {
    RtEntry* entry = rt_find(dest);
    if (entry == nullptr) {
        entry = rt_insert(dest);
        if (entry == nullptr) { _hal.log("rt full, route dropped"); return MergeAction::none; }
        entry->candidates[0] = cand;
        entry->n     = 1;
        entry->dirty = true;
        return MergeAction::new_dest;
    }

    // Match-by-next_hop: refresh in place if cand strictly better.
    for (uint8_t i = 0; i < entry->n; ++i) {
        if (entry->candidates[i].next_hop == cand.next_hop) {
            if (route_strictly_better(cand, entry->candidates[i])) {
                const bool was_primary = (i == 0);
                entry->candidates[i] = cand;
                sort_candidates(*entry);
                const bool now_primary = (entry->candidates[0].next_hop == cand.next_hop);
                if (now_primary) { entry->dirty = true; return MergeAction::primary_refresh; }
                if (was_primary) { entry->dirty = true; return MergeAction::promote; }
                return MergeAction::alt_install;
            }
            entry->candidates[i].last_seen_ms     = cand.last_seen_ms;   // metadata only
            entry->candidates[i].n2_hop           = cand.n2_hop;
            entry->candidates[i].is_gateway       = cand.is_gateway;
            entry->candidates[i].learned_layer_id = cand.learned_layer_id;
            return MergeAction::none;
        }
    }

    // New next_hop, room to spare.
    if (entry->n < protocol::max_rt_candidates) {
        entry->candidates[entry->n] = cand;
        entry->n++;
        sort_candidates(*entry);
        if (entry->candidates[0].next_hop == cand.next_hop) { entry->dirty = true; return MergeAction::promote; }
        return MergeAction::alt_install;
    }

    // Full table: replace the worst (last) only if cand strictly beats it.
    RtCandidate& worst = entry->candidates[entry->n - 1];
    if (!route_strictly_better(cand, worst)) return MergeAction::none;
    worst = cand;
    sort_candidates(*entry);
    if (entry->candidates[0].next_hop == cand.next_hop) { entry->dirty = true; return MergeAction::promote; }
    return MergeAction::alt_install;
}

void Node::maybe_emit_rt_full() {
    if (_rt_full_emitted || _cfg.peer_count == 0) return;   // peer_count 0 = sim telemetry off
    if (_rt_count >= _cfg.peer_count) {
        EventField f[] = { { .key = "peers", .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.peer_count) } };
        _hal.emit("rt_full", f, 1);
        _rt_full_emitted = true;
    }
}

// ---- R2 route-plane hardening -----------------------------------------------

void Node::rt_remove(uint8_t idx) {
    if (idx >= _rt_count) return;
    for (uint8_t k = idx; k + 1 < _rt_count; ++k) _rt[k] = _rt[k + 1];   // shift down (reverse of rt_insert)
    --_rt_count;
    _rt[_rt_count] = RtEntry{};                                          // scrub vacated slot
}

uint32_t Node::ttl_for_hops(uint8_t hops) const {
    return (hops <= 1) ? _cfg.rt_aging_ttl_neighbor_ms : _cfg.rt_aging_ttl_remote_ms;
}

void Node::age_out_stale_routes() {
    // Walk rt[], evict each candidate past its hop-class TTL, drop empty entries,
    // dirty on primary eviction, one triggered re-beacon if any evicted
    // (dv_dual_sf.lua:5249-5302). ttl<=0 disables aging for that class.
    if (_cfg.rt_aging_ttl_neighbor_ms == 0 && _cfg.rt_aging_ttl_remote_ms == 0) return;
    const uint64_t now = _hal.now();
    bool any_evicted = false;
    uint8_t i = 0;
    while (i < _rt_count) {
        RtEntry& e = _rt[i];
        uint8_t w = 0;                    // compact survivors forward (preserves sort)
        bool primary_evicted = false;
        for (uint8_t r = 0; r < e.n; ++r) {
            const RtCandidate& c = e.candidates[r];
            const uint32_t ttl = ttl_for_hops(c.hops);
            const bool expired = (ttl > 0) && (now >= c.last_seen_ms) &&
                                 (now - c.last_seen_ms >= ttl);
            if (expired) {
                any_evicted = true;
                if (r == 0) primary_evicted = true;
                EventField f[] = {
                    { .key = "dest", .type = EventField::T::i64, .i = static_cast<int64_t>(e.dest) },
                    { .key = "slot", .type = EventField::T::str, .s = (r == 0 ? "primary" : "alt") },
                    { .key = "next", .type = EventField::T::i64, .i = static_cast<int64_t>(c.next_hop) },
                    { .key = "hops", .type = EventField::T::i64, .i = static_cast<int64_t>(c.hops) },
                };
                _hal.emit("rt_aged", f, 4);
            } else {
                if (w != r) e.candidates[w] = e.candidates[r];
                ++w;
            }
        }
        e.n = w;
        if (e.n == 0) {
            rt_remove(i);                 // do NOT advance i (entries shifted down)
        } else {
            if (primary_evicted) e.dirty = true;
            ++i;
        }
    }
    if (any_evicted) schedule_triggered_beacon();
}

void Node::rt_prune_cycle(uint8_t dest, uint8_t sender) {
    // A beacon from `sender` reaching `dest` via US closes a me->X->sender->me
    // loop for any of our candidates for `dest` whose advertised next-hop
    // (n2_hop) is `sender`. Drop them (dv_dual_sf.lua:5193-5227). Direct
    // candidates (hops==1, n2_hop==0) carry no n2_hop, so the hops>1 guard skips
    // them even when sender==0.
    RtEntry* e = rt_find(dest);
    if (e == nullptr) return;
    uint8_t w = 0;
    bool primary_pruned = false, mutated = false;
    for (uint8_t r = 0; r < e->n; ++r) {
        const RtCandidate& c = e->candidates[r];
        if (c.hops > 1 && c.n2_hop == sender) {
            mutated = true;
            if (r == 0) primary_pruned = true;
            EventField f[] = {
                { .key = "dest",   .type = EventField::T::i64, .i = static_cast<int64_t>(dest) },
                { .key = "via",    .type = EventField::T::i64, .i = static_cast<int64_t>(c.next_hop) },
                { .key = "sender", .type = EventField::T::i64, .i = static_cast<int64_t>(sender) },
            };
            _hal.emit("rt_prune", f, 3);
        } else {
            if (w != r) e->candidates[w] = e->candidates[r];
            ++w;
        }
    }
    if (!mutated) return;
    e->n = w;
    if (e->n == 0) {
        for (uint8_t i = 0; i < _rt_count; ++i)            // e dangles after rt_remove — find idx first
            if (_rt[i].dest == dest) { rt_remove(i); break; }
    } else if (primary_pruned) {
        e->dirty = true;
    }
    schedule_triggered_beacon();
}

}  // namespace meshroute
