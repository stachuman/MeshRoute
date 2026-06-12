// MeshRoute — lib/core/node_routing.cpp  (DV route table + R2 route-plane hardening)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Node methods for the bounded distance-vector route table: find/insert/remove,
// the merge + sort + strictly-better comparator, the rt_full telemetry, and the R2
// aging/TTL eviction + 3-cycle prune. Behaviour mirrors dv_dual_sf.lua. Part of the
// Node class (declared in node.h); split out of node.cpp for readability.
#include "node.h"

namespace meshroute {

// ---- route table ------------------------------------------------------------

RtEntry* Node::rt_find(uint8_t dest) {
    for (uint8_t i = 0; i < _active->_rt_count; ++i) {
        if (_active->_rt[i].dest == dest) return &_active->_rt[i];
        if (_active->_rt[i].dest > dest)  return nullptr;          // sorted ascending
    }
    return nullptr;
}

RtEntry* Node::rt_insert(uint8_t dest) {
    if (_active->_rt_count >= protocol::cap_routes) return nullptr;
    uint8_t pos = 0;
    while (pos < _active->_rt_count && _active->_rt[pos].dest < dest) ++pos;
    for (uint8_t i = _active->_rt_count; i > pos; --i) _active->_rt[i] = _active->_rt[i - 1];   // shift right
    _active->_rt[pos]      = RtEntry{};
    _active->_rt[pos].dest = dest;
    _active->_rt_count++;
    return &_active->_rt[pos];
}

// R4.2 tier penalty table [tier][viable_alts], Q4 dB (Lua dv:3843-3848). tier 0 = no penalty.
// Demote harder when MORE viable alternatives exist (somewhere else to route).
static constexpr int16_t kTierScorePenaltyQ4[4][3] = {
    {   0,   0,   0 },   // HEALTHY
    {  16,  64, 112 },   // STRAINED
    { 112, 224, 336 },   // CRITICAL
    { 128, 240, 400 },   // EXHAUSTED
};

uint8_t Node::get_neighbor_tier(uint8_t node_id) const {
    auto it = _active->_neighbor_budget_tier.find(node_id);
    if (it == _active->_neighbor_budget_tier.end() || it->second == 0) return 0;
    auto sit = _active->_neighbor_budget_tier_set_at.find(node_id);
    const uint64_t set_at = (sit != _active->_neighbor_budget_tier_set_at.end()) ? sit->second : 0;
    if (_hal.now() - set_at >= protocol::neighbor_budget_tier_ttl_ms) {   // TTL expired -> lazy prune (dv:3863-3868)
        _active->_neighbor_budget_tier.erase(it);
        if (sit != _active->_neighbor_budget_tier_set_at.end()) _active->_neighbor_budget_tier_set_at.erase(sit);
        return 0;
    }
    return it->second;
}

int16_t Node::budget_penalty_q4(const RtCandidate& c, const RtCandidate* cands, uint8_t n) const {
    const uint8_t tier = get_neighbor_tier(c.next_hop);
    if (tier == 0) return 0;                              // HEALTHY -> no penalty
    uint8_t viable_alts = 0;                              // OTHER candidates, RAW score >= floor (dv:3874-3884), cap 2
    for (uint8_t i = 0; i < n; ++i) {
        if (cands[i].next_hop != c.next_hop && cands[i].score >= _routing_snr_floor_q4) {
            if (++viable_alts >= 2) { viable_alts = 2; break; }
        }
    }
    const uint8_t t = (tier > 3) ? 3 : tier;
    return kTierScorePenaltyQ4[t][viable_alts];
}

int16_t Node::effective_score(const RtCandidate& c, const RtCandidate* cands, uint8_t n) const {
    return static_cast<int16_t>(c.score - budget_penalty_q4(c, cands, n));   // suspect penalty deferred (0)
}

bool Node::route_strictly_better(const RtCandidate& a, const RtCandidate& b,
                                 const RtCandidate* cands, uint8_t n) const {
    const int16_t av = effective_score(a, cands, n);     // R4.2: penalty-adjusted; == score for HEALTHY next_hops
    const int16_t bv = effective_score(b, cands, n);
    const bool a_viable = av >= _routing_snr_floor_q4;
    const bool b_viable = bv >= _routing_snr_floor_q4;
    if (a_viable != b_viable) return a_viable;            // viable beats non-viable (a penalty CAN flip viability)
    if (a_viable) {                                       // both viable: hops-asc, eff-score-desc
        if (a.hops != b.hops) return a.hops < b.hops;
        if (av != bv)         return av > bv;
    } else {                                              // both non-viable: eff-score-desc, hops-asc
        if (av != bv)         return av > bv;
        if (a.hops != b.hops) return a.hops < b.hops;
    }
    // True tie -> NOT strictly better, matching the Lua route_strictly_better (dv:4227-4245), which
    // has NO id tie-break: on a tie both less(a,b) and less(b,a) are false, so the stable insertion
    // sort + rt_merge keep INSERTION order. An ascending-next_hop tie-break would DIVERGE (and leak
    // into rt_merge's full-table eviction). (The gates use well-separated SNRs so ties never decide.)
    return false;
}

void Node::sort_candidates(RtEntry& e) {
    // Lua comparator (dv:4248-4252):
    //   less(a,b) = strictly_better(a,b) or (not strictly_better(b,a) and eff_score(a) > eff_score(b))
    // viable_alts is a SET property (order-invariant); snapshot the candidate set so the penalty
    // context stays stable through the in-place shift — matching the Lua's swap-based table.sort,
    // which never exposes a transient duplicate to the comparator. Insertion sort (n <= K = 3), stable.
    RtCandidate snap[protocol::max_rt_candidates];
    for (uint8_t i = 0; i < e.n; ++i) snap[i] = e.candidates[i];
    for (uint8_t i = 1; i < e.n; ++i) {
        RtCandidate key = e.candidates[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0) {
            const RtCandidate& cur = e.candidates[j];
            const bool key_less = route_strictly_better(key, cur, snap, e.n) ||
                                  (!route_strictly_better(cur, key, snap, e.n) &&
                                   effective_score(key, snap, e.n) > effective_score(cur, snap, e.n));
            if (!key_less) break;
            e.candidates[j + 1] = e.candidates[j];
            --j;
        }
        e.candidates[j + 1] = key;
    }
}

// R4.2: re-sort every rt entry that routes via `node_id` (>1 candidate) under the new tier penalty;
// a primary change marks the entry dirty (unless local_only) + emits rt_penalty_rerank. One triggered
// beacon if anything moved on a non-local mark. Lua dv:4255-4318.
int Node::resort_routes_for_neighbor_penalty(uint8_t node_id, [[maybe_unused]] const char* source, bool local_only) {
    int changed = 0;
    for (uint8_t e = 0; e < _active->_rt_count; ++e) {
        RtEntry& entry = _active->_rt[e];
        if (entry.n < 2) continue;                       // single candidate can't rerank
        bool affected = false;
        for (uint8_t i = 0; i < entry.n; ++i)
            if (entry.candidates[i].next_hop == node_id) { affected = true; break; }
        if (!affected) continue;
        const uint8_t old_primary = entry.candidates[0].next_hop;
        sort_candidates(entry);                          // penalty-aware re-sort
        const uint8_t new_primary = entry.candidates[0].next_hop;
        if (new_primary != old_primary) {
            if (!local_only) entry.dirty = true;
            ++changed;
            MR_TELEMETRY(
                EventField f[] = { { .key = "dest",      .type = EventField::T::i64, .i = entry.dest },
                                   { .key = "from_next",  .type = EventField::T::i64, .i = old_primary },
                                   { .key = "to_next",    .type = EventField::T::i64, .i = new_primary },
                                   { .key = "penalized",  .type = EventField::T::i64, .i = node_id },
                                   { .key = "reason",     .type = EventField::T::str, .s = source ? source : "neighbor_penalty" } };
                _hal.emit("rt_penalty_rerank", f, 5); );
        }
    }
    if (changed > 0 && !local_only) schedule_triggered_beacon();   // re-advertise the new primaries
    return changed;
}

// Cleanup #B (Lua refresh_route_order dv:4455): re-sort ONE dest's candidates right before a cascade/issue pick, so a
// tier change since the last sort (a TTL-expiry between the mark-time re-sort and the cascade) is caught. Returns the
// entry (NEVER null for an existing dest, even <2 candidates — callers walk it; the Lua's <2->nil is its issue_send
// `or entry` fallback, which our pick-based callers don't need). A primary change dirties + emits + schedules ONE
// triggered beacon (the conditional draw), exactly like resort_routes_for_neighbor_penalty. Gate-inert: no tier change
// in a gate -> the re-sort keeps the primary -> no draw -> byte-identical.
RtEntry* Node::refresh_route_order(uint8_t dst, [[maybe_unused]] const char* reason) {
    RtEntry* e = rt_find(dst);
    if (e == nullptr || e->n < 2) return e;              // <2 candidates: nothing to re-rank
    const uint8_t old_primary = e->candidates[0].next_hop;
    sort_candidates(*e);                                 // penalty-aware re-sort (draw-free)
    const uint8_t new_primary = e->candidates[0].next_hop;
    if (new_primary != old_primary) {
        e->dirty = true;
        MR_TELEMETRY(
            EventField f[] = { { .key = "dest",      .type = EventField::T::i64, .i = e->dest },
                               { .key = "from_next",  .type = EventField::T::i64, .i = old_primary },
                               { .key = "to_next",    .type = EventField::T::i64, .i = new_primary },
                               { .key = "reason",     .type = EventField::T::str, .s = reason ? reason : "refresh_route_order" } };
            _hal.emit("rt_penalty_rerank", f, 4); );
        schedule_triggered_beacon();                     // re-advertise + the conditional rand draw (matches the Lua)
    }
    return e;
}

// R4.2: record neighbour `node_id`'s budget tier (max-merge, TTL-stamped) + rerank affected routes.
// Two callers: the BUDGET NACK react (reverse, local_only=false) and the ACK budget_hint (forward,
// local_only=true). Lua dv:4320-4342.
int Node::mark_neighbor_budget_tier(uint8_t node_id, uint8_t tier, const char* source, bool local_only) {
    if (tier == 0) return 0;                             // <= HEALTHY -> nothing to mark
    const uint8_t current = get_neighbor_tier(node_id);  // (also lazy-prunes an expired mark)
    if (current > tier) return 0;                        // max-merge: never downgrade a worse mark
    _active->_neighbor_budget_tier[node_id]        = tier;
    _active->_neighbor_budget_tier_set_at[node_id] = _hal.now();
    const int reranked = resort_routes_for_neighbor_penalty(node_id, source, local_only);
    MR_TELEMETRY(
        EventField f[] = { { .key = "node",     .type = EventField::T::i64, .i = node_id },
                           { .key = "tier",     .type = EventField::T::i64, .i = tier },
                           { .key = "source",   .type = EventField::T::str, .s = source ? source : "unknown" },
                           { .key = "reranked", .type = EventField::T::i64, .i = reranked } };
        _hal.emit("neighbor_budget_mark", f, 4); );
    return reranked;
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
            if (route_strictly_better(cand, entry->candidates[i], entry->candidates, entry->n)) {
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
    if (!route_strictly_better(cand, worst, entry->candidates, entry->n)) return MergeAction::none;
    worst = cand;
    sort_candidates(*entry);
    if (entry->candidates[0].next_hop == cand.next_hop) { entry->dirty = true; return MergeAction::promote; }
    return MergeAction::alt_install;
}

void Node::maybe_emit_rt_full() {
    if (_rt_full_emitted || _cfg.peer_count == 0) return;   // peer_count 0 = sim telemetry off
    if (_active->_rt_count >= _cfg.peer_count) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "peers", .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.peer_count) } };
            _hal.emit("rt_full", f, 1); );
        _rt_full_emitted = true;
    }
}

// ---- R2 route-plane hardening -----------------------------------------------

void Node::rt_remove(uint8_t idx) {
    if (idx >= _active->_rt_count) return;
    for (uint8_t k = idx; k + 1 < _active->_rt_count; ++k) _active->_rt[k] = _active->_rt[k + 1];   // shift down (reverse of rt_insert)
    --_active->_rt_count;
    _active->_rt[_active->_rt_count] = RtEntry{};                                          // scrub vacated slot
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
    while (i < _active->_rt_count) {
        RtEntry& e = _active->_rt[i];
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
                MR_TELEMETRY(
                    EventField f[] = {
                        { .key = "dest", .type = EventField::T::i64, .i = static_cast<int64_t>(e.dest) },
                        { .key = "slot", .type = EventField::T::str, .s = (r == 0 ? "primary" : "alt") },
                        { .key = "next", .type = EventField::T::i64, .i = static_cast<int64_t>(c.next_hop) },
                        { .key = "hops", .type = EventField::T::i64, .i = static_cast<int64_t>(c.hops) },
                    };
                    _hal.emit("rt_aged", f, 4); );
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
            MR_TELEMETRY(
                EventField f[] = {
                    { .key = "dest",   .type = EventField::T::i64, .i = static_cast<int64_t>(dest) },
                    { .key = "via",    .type = EventField::T::i64, .i = static_cast<int64_t>(c.next_hop) },
                    { .key = "sender", .type = EventField::T::i64, .i = static_cast<int64_t>(sender) },
                };
                _hal.emit("rt_prune", f, 3); );
        } else {
            if (w != r) e->candidates[w] = e->candidates[r];
            ++w;
        }
    }
    if (!mutated) return;
    e->n = w;
    if (e->n == 0) {
        for (uint8_t i = 0; i < _active->_rt_count; ++i)            // e dangles after rt_remove — find idx first
            if (_active->_rt[i].dest == dest) { rt_remove(i); break; }
    } else if (primary_pruned) {
        e->dirty = true;
    }
    schedule_triggered_beacon();
}

}  // namespace meshroute
