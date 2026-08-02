// MeshRoute — lib/core/node_routing.cpp  (DV route table + R2 route-plane hardening)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Node methods for the bounded distance-vector route table: find/insert/remove,
// the merge + sort + strictly-better comparator, the rt_full telemetry, and the R2
// aging/TTL eviction + 3-cycle prune. Behaviour mirrors dv_dual_sf.lua. Part of the
// Node class (declared in node.h); split out of node.cpp for readability.
#include "node.h"
#include "airtime.h"   // anti-spam v2: airtime_ms() for the channel_cap_origin() T_ch term

namespace MESHROUTE_NS {

// ---- route table ------------------------------------------------------------

RtEntry* Node::rt_find(uint8_t dest, Plane plane) {
    // §mobile 6.2: a KNOWN same-team peer's route lives in the TEAM plane (_rt_team), NOT _rt (§18: its local id can
    // collide a static global id). Dispatch every route lookup by plane. Wave 2: AUTO dispatches by is_team_peer (today's
    // behaviour — is_team_peer is ALWAYS false for a static/non-team node -> byte-identical); TEAM forces _rt_team; GLOBAL
    // forces _rt (so a global send to an id that COLLIDES a teammate's team id still routes on the STATIC plane).
    // rt_merge/rt_prune_cycle use the EXPLICIT-table overloads (not this wrapper), so ingest/emit are unaffected.
#if MR_FEAT_TEAM
    const bool team = (plane == Plane::TEAM) || (plane == Plane::AUTO && is_team_peer(dest));
    if (team) return rt_find(dest, _active->_rt_team, _active->_rt_team_count);
#else
    (void)plane;
#endif
    return rt_find(dest, _active->_rt, _active->_rt_count);
}
RtEntry* Node::rt_find(uint8_t dest, RtEntry* rt, uint8_t rt_count) {
    for (uint8_t i = 0; i < rt_count; ++i) {
        if (rt[i].dest == dest) return &rt[i];
        if (rt[i].dest > dest)  return nullptr;          // sorted ascending
    }
    return nullptr;
}

RtEntry* Node::rt_insert(uint8_t dest) { return rt_insert(dest, _active->_rt, _active->_rt_count); }
RtEntry* Node::rt_insert(uint8_t dest, RtEntry* rt, uint8_t& rt_count) {
    if (dest == 0 || dest == 0xFF) return nullptr;        // §P0: never store a route to the reserved sentinel ids (defense-in-depth)
    if (rt_count >= protocol::cap_routes) return nullptr;
    uint8_t pos = 0;
    while (pos < rt_count && rt[pos].dest < dest) ++pos;
    for (uint8_t i = rt_count; i > pos; --i) rt[i] = rt[i - 1];   // shift right
    rt[pos]      = RtEntry{};
    rt[pos].dest = dest;
    rt_count++;
    return &rt[pos];
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

// §P2: the liveness tier penalty for `next_hop` (const, non-mutating — checks the untils against now w/o lazy-clearing).
int16_t Node::liveness_penalty_q4(uint8_t next_hop, bool team_plane) const {
    if (next_hop == 0 || next_hop == _node_id) return 0;
#if MR_FEAT_TEAM
    if (team_plane && next_hop == team_local_id()) return 0;   // §2c: never penalize our OWN team id (dual member: team_local_id != _node_id)
#endif
    const auto& L = *_active;
    const PeerLiveness* s = nullptr;
#if MR_FEAT_TEAM
    if (team_plane) { for (uint8_t i = 0; i < L._team_liveness_n; ++i) if (L._team_liveness[i].node_id == next_hop) { s = &L._team_liveness[i]; break; } }
    else
#else
    (void)team_plane;
#endif
    { for (uint8_t i = 0; i < L._peer_liveness_n; ++i) if (L._peer_liveness[i].node_id == next_hop) { s = &L._peer_liveness[i]; break; } }
    if (!s) return 0;
    const uint64_t now = _hal.now();
    if (s->dead_until_ms    > now) return protocol::peer_dead_penalty_q4;
    if (s->silent_until_ms  > now) return protocol::peer_silent_penalty_q4;
    if (s->suspect_until_ms > now) return protocol::peer_suspect_penalty_q4;
    return 0;
}

// §bidi: the bidirectionality penalty for `next_hop`. one_way -> bidi_penalty_one_way_q4; unknown AND confirmed -> 0
// (OI2 — the ONLY demotion is positively-confirmed one_way; a nudge on `unknown` would punish every not-yet-probed
// link on a cold mesh). PURE/non-mutating read. NOTE: defined + tested in Slice 3 but NOT yet folded into
// effective_score — Slice 4 composes it (Slice 3 stays delivery-neutral).
// ✔ §team-parity T5: `team_plane` selects the TEAM bidi state (the _team_liveness slot's team_bidi_state, keyed by
// team_local_id) instead of the static node_id-indexed _link_bidi. The guard shape is copied VERBATIM from
// liveness_penalty_q4 above (:87-89) — U3, and the `next_hop == team_local_id()` skip matters for the same reason
// there: on a dual member team_local_id != _node_id, so the generic `next_hop == _node_id` self-test does not cover it.
// Static reduction: team_plane==false takes the _link_bidi read unchanged, so every static caller is byte-identical.
int16_t Node::bidi_penalty_q4(uint8_t next_hop, [[maybe_unused]] bool team_plane) const {
    if (next_hop == 0 || next_hop == _node_id) return 0;
#if MR_FEAT_TEAM
    if (team_plane) {
        if (next_hop == team_local_id()) return 0;   // never penalize our OWN team id (mirrors :89)
        const PeerLiveness* s = team_liveness_find(next_hop);
        return (s && s->team_bidi_state == static_cast<uint8_t>(LinkBidi::one_way))
                   ? protocol::bidi_penalty_one_way_q4
                   : 0;
    }
#endif
    return _active->_link_bidi[next_hop] == static_cast<uint8_t>(LinkBidi::one_way)
               ? protocol::bidi_penalty_one_way_q4
               : 0;
}

// Anti-spam v2 (2026-06-30/07-02) — the channel CAPACITY C = max(1, D/T_ch). THE single source of C; both
// channel_cap_origin() (the enforced per-origin cap) and limits_snapshot() (the ch_ceiling shown to the user) call
// this, so the displayed ceiling can never drift from the enforced math (MF8). Returns 0 when duty is disabled.
uint32_t Node::channel_capacity_C() const {
    // MF1: the basis is the 5-MINUTE channel-window duty budget D (channel_duty_budget_ms()), NOT the 1-hour
    // _duty_cycle_budget_ms. MF2: D==0 (duty disabled) -> 0 (no duty-anchored capacity; the caller falls back).
    const uint32_t D = channel_duty_budget_ms();
    if (D == 0) return 0u;
    // MF3: a re-broadcast flood airs the 43-B FLOOD RTS-M (at routing_sf) THEN the DATA-M (at max_data_sf()). Both count.
    const uint32_t t_rts  = airtime_routing_ms(43);
    const uint32_t t_data = airtime_ms(max_data_sf(), active_bw_hz(), active_cr(),
                                       protocol::preamble_sym, protocol::channel_flood_sample_len);
    const uint32_t T_ch = t_rts + t_data;
    if (T_ch == 0) return 0u;                                          // defensive: no airtime -> 0 (caller falls back to the legacy cap, matching pre-refactor)
    // C = total distinct floods/window the duty plane sustains. C>=1 floor (tiny D / high SF) so the clamp can't invert.
    return (D / T_ch > 0) ? D / T_ch : 1u;
}

// Anti-spam v2 (2026-06-30) — the per-origin CHANNEL cap. Pure/const/draw-free. Spec
// docs/superpowers/specs/2026-06-30-antispam-duty-channel-cap.md (MF1/MF2/MF3).
uint16_t Node::channel_cap_origin() const {
    // MF2: D==0 (duty disabled) => C==0 => the legacy flat cap (the duty plane is the volume governor).
    const uint32_t C = channel_capacity_C();
    if (C == 0) return protocol::cap_channel_origin_legacy;
    // Share C fairly among the ACTIVE originators: N_active = max(1, floor(frac * rt_count())); cap = clamp(C/N_active, 1, C).
    uint32_t N_active = static_cast<uint32_t>(_cfg.channel_active_fraction * static_cast<float>(rt_count()));
    if (N_active < 1) N_active = 1;
    uint32_t cap = C / N_active;
    if (cap < 1u) cap = 1u;
    if (cap > C)  cap = C;
    return static_cast<uint16_t>(cap);
}

int16_t Node::effective_score(const RtCandidate& c, const RtCandidate* cands, uint8_t n, bool team_plane) const {
    // §P2: subtract BOTH the R4.2 budget tier AND the liveness tier (suspect/silent/dead) — Lua effective_score@4140.
    // §bidi: ALSO subtract the bidirectionality penalty (one_way next-hop). Rides the SORT only (composes here +
    // through route_strictly_better) — NOT a next_hop_selectable hard gate, so a SOLE one_way route stays pickable.
    // §2c: on the TEAM plane use the TEAM liveness tier + subtract NEITHER budget_penalty_q4 (_neighbor_budget_tier[id])
    // NOR bidi_penalty_q4 (_link_bidi[id]) — both were static-node_id-indexed arrays the team plane did not track;
    // reading them for a team candidate was the §18 read-alias. So a team candidate read ONLY _team_liveness.
    // ★★ ✔ §team-parity T5: THE BIDI HALF OF THAT IS NOW A REAL TEAM READ. bidi_penalty_q4 takes the plane and resolves a
    // team candidate against the TEAM bidi state (the _team_liveness slot), so a teammate we have positively established
    // does NOT hear us is demoted by bidi_penalty_one_way_q4 exactly as a static one-way next-hop is. This is spec §3/T5's
    // whole point: "what would have let 213 and 174 recognise that their direct link is one-way and commit to the 234 path
    // deliberately rather than by accident." SORT-only, like the static plane — never a next_hop_selectable hard gate, so a
    // SOLE one-way team route stays pickable and delivery cannot regress to zero on it.
    // ⚠ budget_penalty_q4 STAYS ZEROED and that is NOT T5's to fix: it reads _neighbor_budget_tier, the R4.2 tier map, a
    // node_id-keyed static ledger with no team mirror at all. T5 built a team BIDI plane, not a team BUDGET plane. Left
    // as-is, deliberately, and named here so the remaining asymmetry is visible rather than looking like an oversight.
    return static_cast<int16_t>(c.score - (team_plane ? 0 : budget_penalty_q4(c, cands, n))
                                        - liveness_penalty_q4(c.next_hop, team_plane)
                                        - bidi_penalty_q4(c.next_hop, team_plane));
}

// §cross-layer: is `dest` a gateway we'd use as a cross-layer egress? — a heard 1-hop schedule (_gw_schedules)
// OR a multi-hop type-4 TLV row (_bridged_layers) names it a gw. Routes to such a dest are the cross-layer routes;
// they are exempt from the freshness viability gate (gateways are intentionally quiet on our leaf).
bool Node::is_gateway_dest(uint8_t dest) const {
    if (dest == 0 || dest == 0xFF) return false;
    if (find_gw_schedule(dest) != nullptr) return true;
    for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i)
        if (_bridged_layers[i].valid && _bridged_layers[i].gw_id == dest) return true;
    return false;
}

bool Node::route_strictly_better(const RtCandidate& a, const RtCandidate& b,
                                 const RtCandidate* cands, uint8_t n, bool gw_dest, bool team_plane) const {
    const int16_t av = effective_score(a, cands, n, team_plane);     // R4.2 + §P2: budget + liveness penalty-adjusted (§2c team_plane -> team liveness)
    const int16_t bv = effective_score(b, cands, n, team_plane);
    // §P2 freshness eligibility: a candidate whose next-hop hasn't been heard within next_hop_live_ttl is NON-viable
    // (loses to any fresh candidate). Subsumes the A↔B mutual-refresh trap: an unconditionally-refreshed-but-stale
    // next-hop route can no longer be SELECTED. NB this is the SPEC's chosen layer (gate freshness in the SORT) — a
    // deliberate DRIFT from the Lua, which gates it at PICK time (issue_send@7234 defers + RREQs an all-stale route).
    // Consequence: with an alt, the fresh alt wins here (good); with NO alt, the only candidate stays selectable and
    // the DM still flies at the stale next-hop (no Lua-style defer/RREQ) — the no-alt path is a documented follow-up.
    // §cross-layer freshness exemption: a route whose DEST is a gateway (the cross-layer egress) is NEVER gated by
    // freshness — gateways are intentionally quiet on our leaf (time-multiplexed windows), and freshness was never
    // meant to govern cross-layer route selection (it wrongly demoted viable gateway paths → s15/s16 regression).
    // gw_dest short-circuits the is_next_hop_fresh viability check for those routes.
    // §2c: a TEAM candidate SKIPS the is_next_hop_fresh viability gate — the team plane keeps no _dest_seen_ms (freshness
    // omitted), so reading it for a team_local_id is the §18 read-alias; the cleanly-dead relay is demoted by the team
    // liveness PENALTY instead (via effective_score above). team_plane => always fresh-viable.
    const bool a_viable = av >= _routing_snr_floor_q4 && (gw_dest || team_plane || is_next_hop_fresh(a.next_hop));
    const bool b_viable = bv >= _routing_snr_floor_q4 && (gw_dest || team_plane || is_next_hop_fresh(b.next_hop));
    if (a_viable != b_viable) return a_viable;            // viable beats non-viable (a penalty/staleness CAN flip viability)
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

void Node::sort_candidates(RtEntry& e, bool team_plane) {
    // Lua comparator (dv:4248-4252):
    //   less(a,b) = strictly_better(a,b) or (not strictly_better(b,a) and eff_score(a) > eff_score(b))
    // viable_alts is a SET property (order-invariant); snapshot the candidate set so the penalty
    // context stays stable through the in-place shift — matching the Lua's swap-based table.sort,
    // which never exposes a transient duplicate to the comparator. Insertion sort (n <= K = 3), stable.
    const bool gw_dest = is_gateway_dest(e.dest);        // §cross-layer: a gateway-dest route is freshness-exempt
    RtCandidate snap[protocol::max_rt_candidates];
    for (uint8_t i = 0; i < e.n; ++i) snap[i] = e.candidates[i];
    for (uint8_t i = 1; i < e.n; ++i) {
        RtCandidate key = e.candidates[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0) {
            const RtCandidate& cur = e.candidates[j];
            const bool key_less = route_strictly_better(key, cur, snap, e.n, gw_dest, team_plane) ||
                                  (!route_strictly_better(cur, key, snap, e.n, gw_dest, team_plane) &&
                                   effective_score(key, snap, e.n, team_plane) > effective_score(cur, snap, e.n, team_plane));
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
            MR_EMIT("rt_penalty_rerank", EF_I("dest", entry.dest), EF_I("from_next", old_primary), EF_I("to_next", new_primary),
                    EF_I("penalized", node_id), EF_S("reason", source ? source : "neighbor_penalty"));
        }
    }
    if (changed > 0 && !local_only) schedule_triggered_beacon();   // re-advertise the new primaries
    return changed;
}

#if MR_FEAT_TEAM
// §2c: re-sort the TEAM routes (_rt_team) whose candidates include `team_local_id` after its liveness tier changed —
// a self-contained mirror of resort_routes_for_neighbor_penalty over the team table (team_plane sort). Keeps
// candidates[0] current so the NEXT team flight picks the fresh alt, not the just-demoted primary. Team-plane only.
// Wave-2 ruling 2.3 (FIX): a primary change here now dirty-marks the moved _rt_team entry + schedules ONE triggered
// beacon, using the SAME shared infrastructure as the static resort. This is plane-correct: `dirty` is a per-RtEntry
// field and emit_beacon selects DIRTY entries from src_rt = _rt_team when a team member advertises (team_emit ->
// node_beacon.cpp Phase-1 dirty pass), so a team liveness rerank is now advertised on the member's own cadence
// instead of waiting for something else to dirty the entry. Team liveness is always LOCAL evidence (team never
// gossips liveness), so — unlike the static path's local_only ACK-hint case — the mark + beacon are unconditional.
// [[maybe_unused]]: `reason` is telemetry-only and MR_EMIT is stripped on the device — the identical annotation the
// three sibling reason/source params already carry (resort_routes_for_neighbor_penalty :260, refresh_route_order :324,
// enqueue_data's tx_event). Without it the BOARD builds emit `unused parameter 'reason'`, which the gate caught.
// §team-parity T5: `reason` is a parameter rather than the hardcoded "team_liveness" literal, so a bidi confirm/recover
// rerank is distinguishable in the stream from a liveness-tier rerank. Both pre-T5 call sites take the default, which IS
// the old literal ⇒ byte-identical there.
void Node::team_resort_routes_through(uint8_t team_local_id, [[maybe_unused]] const char* reason) {
    int changed = 0;
    for (uint8_t e = 0; e < _active->_rt_team_count; ++e) {
        RtEntry& entry = _active->_rt_team[e];
        if (entry.n < 2) continue;                       // single candidate can't rerank
        bool affected = false;
        for (uint8_t i = 0; i < entry.n; ++i) if (entry.candidates[i].next_hop == team_local_id) { affected = true; break; }
        if (!affected) continue;
        const uint8_t old_primary = entry.candidates[0].next_hop;
        sort_candidates(entry, /*team_plane=*/true);
        const uint8_t new_primary = entry.candidates[0].next_hop;
        if (new_primary != old_primary) {
            entry.dirty = true;                          // §2.3: advertise the new team primary (emit_beacon dirty pass over _rt_team)
            ++changed;
            MR_EMIT("rt_penalty_rerank", EF_I("dest", entry.dest), EF_I("from_next", old_primary), EF_I("to_next", new_primary),
                    EF_I("penalized", team_local_id), EF_S("reason", reason));
        }
    }
    if (changed > 0) schedule_triggered_beacon();        // §2.3: re-advertise the new team primaries on our cadence
}
#endif

// Cleanup #B (Lua refresh_route_order dv:4455): re-sort ONE dest's candidates right before a cascade/issue pick, so a
// tier change since the last sort (a TTL-expiry between the mark-time re-sort and the cascade) is caught. Returns the
// entry (NEVER null for an existing dest, even <2 candidates — callers walk it; the Lua's <2->nil is its issue_send
// `or entry` fallback, which our pick-based callers don't need). A primary change dirties + emits + schedules ONE
// triggered beacon (the conditional draw), exactly like resort_routes_for_neighbor_penalty. Gate-inert: no tier change
// in a gate -> the re-sort keeps the primary -> no draw -> byte-identical.
RtEntry* Node::refresh_route_order(uint8_t dst, [[maybe_unused]] const char* reason, Plane plane) {
    RtEntry* e = rt_find(dst, plane);
    if (e == nullptr || e->n < 2) return e;              // <2 candidates: nothing to re-rank
    const uint8_t old_primary = e->candidates[0].next_hop;
    sort_candidates(*e, plane == Plane::TEAM);           // penalty-aware re-sort (draw-free). §2c: TEAM -> team liveness in the sort
    const uint8_t new_primary = e->candidates[0].next_hop;
    if (new_primary != old_primary) {
        e->dirty = true;
        MR_EMIT("rt_penalty_rerank", EF_I("dest", e->dest), EF_I("from_next", old_primary), EF_I("to_next", new_primary),
                EF_S("reason", reason ? reason : "refresh_route_order"));
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
    MR_EMIT("neighbor_budget_mark", EF_I("node", node_id), EF_I("tier", tier), EF_S("source", source ? source : "unknown"),
            EF_I("reranked", reranked));
    return reranked;
}

// Bidirectionality plane (2026-06-29): a LOCAL confirmation that next_hop hears us (a real CTS to our flight, or a
// complete-heard-set present hit). Set confirmed + stamp the dedicated decay source, then fan out via the
// resort_routes_for_neighbor_penalty pattern so a recovery re-sorts + re-dirties + re-advertises. Emits link_recover
// when the link was previously one_way (the §7 recovery signal). No penalty rides effective_score yet (Slice 4).
// ✔ §team-parity T5 (spec §3/T5) — the TEAM arm. This is the site T2 marked ✖ twice ("there is nothing to write it
// to", node_mac_rx.cpp:507/:1397): a team confirm may NOT call the static body, because _link_bidi/_link_bidi_confirmed_ms
// are node_id-indexed and `next_hop` here is a team_local_id — invariant I2/I8's write-alias. The team arm writes the
// team_bidi_* fields of the _team_liveness slot and fans out through team_resort_routes_through, the exact structural
// mirror record_peer_rts_timeout's team arm (:660-675) already uses. Static reduction: team_plane==false is the pre-T5
// body verbatim, so s18 and every static scenario are byte-identical by construction.
void Node::note_link_confirmed(uint8_t next_hop, [[maybe_unused]] bool team_plane) {
    if (next_hop == 0 || next_hop == 0xFF) return;
#if MR_FEAT_TEAM
    if (team_plane) {
        // create=true: a CTS or a heard-set hit from a teammate is proof enough to spend a slot on it (the static plane's
        // array is always present, so "create" is the team plane's equivalent of "the slot exists"). cap 16 vs R4's 1-10
        // members ⇒ the LRU evict is unreachable in a real team; if it ever fires it re-uses the STALEST slot, which is
        // the same policy _peer_liveness has always had.
        PeerLiveness* ts = team_liveness_slot(next_hop, /*create=*/true);
        if (!ts) return;
        const bool was_one_way = ts->team_bidi_state == static_cast<uint8_t>(LinkBidi::one_way);
        ts->team_bidi_state       = static_cast<uint8_t>(LinkBidi::confirmed);
        ts->team_bidi_confirmed_s = static_cast<uint32_t>(_hal.now() / 1000u);   // seconds — see the PeerLiveness note
        team_resort_routes_through(next_hop, "team_link_bidi_confirm");
        MR_EMIT("link_bidi_confirm", EF_I("next_hop", next_hop), EF_S("plane", "team"));
        if (was_one_way) MR_EMIT("link_recover", EF_I("next_hop", next_hop), EF_S("plane", "team"));
        return;
    }
#endif
    const bool was_one_way = _active->_link_bidi[next_hop] == static_cast<uint8_t>(LinkBidi::one_way);
    _active->_link_bidi[next_hop]              = static_cast<uint8_t>(LinkBidi::confirmed);
    _active->_link_bidi_confirmed_ms[next_hop] = _hal.now();
    resort_routes_for_neighbor_penalty(next_hop, "link_bidi_confirm", /*local_only=*/false);
    MR_EMIT("link_bidi_confirm", EF_I("next_hop", next_hop));
    if (was_one_way) MR_EMIT("link_recover", EF_I("next_hop", next_hop));
}

// Bidirectionality plane: a confirmed link whose last confirmation is older than bidi_confirm_ttl_ms decays to
// UNKNOWN — selectable + unpenalized again (a quiet-but-functional link must not self-degrade). MF6: it NEVER decays
// to one_way (that requires positive absent+complete heard-set evidence, set only by update_link_bidi_from_beacon).
// unknown/one_way slots are left as-is. DEFERRED: decay_link_bidi has NO wired caller in this initiative — it is a
// no-op for routing (confirmed and unknown are selection-equivalent, both 0 penalty), so a stale confirmed costs nothing.
// Kept for MF6-correctness; wire a lazy caller (e.g. inside candidate_degraded) ONLY if a future feature treats confirmed != unknown.
// ✖ §team-parity T5 — the TEAM arm exists and is likewise NOT WIRED, for the IDENTICAL reason, stated so the omission is
// not read as an oversight: nothing treats confirmed differently from unknown (bidi_penalty_q4 penalizes ONLY one_way and
// candidate_degraded tests ONLY one_way), so a stale team `confirmed` costs exactly nothing, and MF6 forbids decaying to
// one_way. It is threaded so that (a) a grep for the bidi TTL finds both planes and (b) wiring it later is one call, on
// both planes at once. The team arm is therefore corpus-dark AND native-dark by construction — see the T5 coverage note.
void Node::decay_link_bidi(uint8_t next_hop, [[maybe_unused]] bool team_plane) {
#if MR_FEAT_TEAM
    if (team_plane) {
        PeerLiveness* ts = team_liveness_slot(next_hop, /*create=*/false);
        if (!ts || ts->team_bidi_state != static_cast<uint8_t>(LinkBidi::confirmed)) return;
        // seconds on the team plane, so compare in seconds (bidi_confirm_ttl_ms is a whole number of seconds: 1200000).
        const uint32_t now_s = static_cast<uint32_t>(_hal.now() / 1000u);
        if (now_s - ts->team_bidi_confirmed_s >= protocol::bidi_confirm_ttl_ms / 1000u)
            ts->team_bidi_state = static_cast<uint8_t>(LinkBidi::unknown);
        return;
    }
#endif
    if (_active->_link_bidi[next_hop] != static_cast<uint8_t>(LinkBidi::confirmed)) return;
    const uint64_t now = _hal.now();
    const uint64_t conf = _active->_link_bidi_confirmed_ms[next_hop];
    if (now - conf >= protocol::bidi_confirm_ttl_ms)
        _active->_link_bidi[next_hop] = static_cast<uint8_t>(LinkBidi::unknown);
}

// Bidirectionality plane (MF5/OI1): the LIVE effective degraded state of a candidate — the wire-inherited component
// (a fact about what the advertiser said, stored on the candidate) OR-ed with the local one_way verdict (recomputed
// from _link_bidi every call). NEVER a sticky cached bool: a stuck-degraded cache would never clear on recovery and
// defeat §7. Read by select (Slice 4) + advertise (Slice 5); no caller consumes it in this state-only slice.
// ★★ ✔ §team-parity T5: the team arm was `return c.degraded_from_wire` — wire-only, because "the team plane keeps no
// _link_bidi" and reading the static array for a team_local_id was the §18 read-alias. It now ORs in the LOCAL team
// verdict, so a member's beacon advertises `degraded` for a team route whose next-hop IT has established is one-way,
// giving the team plane the §5 TRANSITIVE half the static plane has had since Slice 3.
// ⚠ SCOPE, MEASURED AND STATED PLAINLY: candidate_degraded has exactly ONE consumer in the engine —
// node_beacon.cpp:471, the advertised route-page bit — and `degraded` / `degraded_from_wire` are read by NOTHING that
// makes a routing decision (not effective_score, not route_strictly_better, not next_hop_selectable; grep them). So this
// arm is a GOSSIP/observability change, not a path-selection change. The path selection is bidi_penalty_q4's arm in
// effective_score. Both are wired here so the two planes are structurally identical, and this note exists so nobody reads
// a `degraded` bit as a routing input on either plane.
// Static reduction: team_plane==false is the pre-T5 expression verbatim.
bool Node::candidate_degraded(const RtCandidate& c, [[maybe_unused]] bool team_plane) const {
#if MR_FEAT_TEAM
    if (team_plane)
        return c.degraded_from_wire
            || (team_link_bidi_state(c.next_hop) == LinkBidi::one_way);
#endif
    return c.degraded_from_wire
        || _active->_link_bidi[c.next_hop] == static_cast<uint8_t>(LinkBidi::one_way);
}

Node::MergeAction Node::rt_merge(uint8_t dest, const RtCandidate& cand) { return rt_merge(dest, cand, _active->_rt, _active->_rt_count, /*team_plane=*/false); }
Node::MergeAction Node::rt_merge(uint8_t dest, const RtCandidate& cand, RtEntry* rt, uint8_t& rt_count, bool team_plane) {
    // ① never STORE a route that relays THROUGH a mobile peer (it roams away) — but deliver TO a mobile is fine
    // (the next_hop==dest carve-out inside the predicate). Hard skip, NOT a score penalty (Lua dv:4583).
    // §6.2: SKIP on the team plane — a same-team peer IS a legal transit in its own table (the whole point).
    if (!team_plane && route_uses_mobile_as_transit(dest, cand.next_hop)) {
        MR_EMIT("rt_skip_mobile_transit", EF_I("dest", dest), EF_I("next", cand.next_hop));
        return MergeAction::none;
    }
    RtEntry* entry = rt_find(dest, rt, rt_count);
    const bool gw_dest = is_gateway_dest(dest);          // §cross-layer: a gateway-dest route is freshness-exempt
    if (entry == nullptr) {
        entry = rt_insert(dest, rt, rt_count);
        if (entry == nullptr) { _hal.log("rt full, route dropped"); return MergeAction::none; }
        entry->candidates[0] = cand;
        entry->n     = 1;
        entry->dirty = true;
        return MergeAction::new_dest;
    }

    // Match-by-next_hop: refresh in place if cand strictly better.
    for (uint8_t i = 0; i < entry->n; ++i) {
        if (entry->candidates[i].next_hop == cand.next_hop) {
            if (route_strictly_better(cand, entry->candidates[i], entry->candidates, entry->n, gw_dest, team_plane)) {
                const bool was_primary = (i == 0);
                entry->candidates[i] = cand;
                sort_candidates(*entry, team_plane);
                const bool now_primary = (entry->candidates[0].next_hop == cand.next_hop);
                if (now_primary) { entry->dirty = true; return MergeAction::primary_refresh; }
                if (was_primary) { entry->dirty = true; return MergeAction::promote; }
                return MergeAction::alt_install;
            }
            entry->candidates[i].last_seen_ms     = cand.last_seen_ms;   // metadata only
            entry->candidates[i].n2_hop           = cand.n2_hop;
            entry->candidates[i].is_gateway       = cand.is_gateway;
            entry->candidates[i].learned_leaf = cand.learned_leaf;
            entry->candidates[i].degraded_from_wire = cand.degraded_from_wire;   // Slice 3: refresh the wire bit even on a metadata-only merge (clears on a clean re-advert)
            return MergeAction::none;
        }
    }

    // New next_hop, room to spare.
    if (entry->n < protocol::max_rt_candidates) {
        entry->candidates[entry->n] = cand;
        entry->n++;
        sort_candidates(*entry, team_plane);
        if (entry->candidates[0].next_hop == cand.next_hop) { entry->dirty = true; return MergeAction::promote; }
        return MergeAction::alt_install;
    }

    // Full table: replace the worst (last) only if cand strictly beats it.
    RtCandidate& worst = entry->candidates[entry->n - 1];
    if (!route_strictly_better(cand, worst, entry->candidates, entry->n, gw_dest, team_plane)) return MergeAction::none;
    worst = cand;
    sort_candidates(*entry, team_plane);
    if (entry->candidates[0].next_hop == cand.next_hop) { entry->dirty = true; return MergeAction::promote; }
    return MergeAction::alt_install;
}

void Node::maybe_emit_rt_full() {
    if (_rt_full_emitted || _cfg.peer_count == 0) return;   // peer_count 0 = sim telemetry off
    if (_active->_rt_count >= _cfg.peer_count) {
        MR_EMIT("rt_full", EF_I("peers", static_cast<int64_t>(_cfg.peer_count)));
        _rt_full_emitted = true;
    }
}

// ---- R2 route-plane hardening -----------------------------------------------

void Node::rt_remove(uint8_t idx) { rt_remove(idx, _active->_rt, _active->_rt_count); }
void Node::rt_remove(uint8_t idx, RtEntry* rt, uint8_t& rt_count) {
    if (idx >= rt_count) return;
    for (uint8_t k = idx; k + 1 < rt_count; ++k) rt[k] = rt[k + 1];   // shift down (reverse of rt_insert)
    --rt_count;
    rt[rt_count] = RtEntry{};                                          // scrub vacated slot
}

uint32_t Node::ttl_for_hops(uint8_t hops) const {
    return (hops <= 1) ? _cfg.rt_aging_ttl_neighbor_ms : _cfg.rt_aging_ttl_remote_ms;
}

void Node::age_out_stale_routes() {
    age_out_stale_routes(_active->_rt, _active->_rt_count, /*team_plane=*/false);   // static plane (byte-identical to the old body)
    // §mobile 6.2: age the TEAM plane too — else a roamed-away teammate's route lingers forever (rt_find dispatches on
    // is_team_peer with NO _rt fallback, so a stale team route black-holes that id + eventually exhausts _rt_team). A full
    // eviction clears the _team_peer bit so the dispatch stops shadowing the static plane. A static node has _rt_team empty -> no-op.
#if MR_FEAT_TEAM
    if (_active->_rt_team_count) age_out_stale_routes(_active->_rt_team, _active->_rt_team_count, /*team_plane=*/true);
#endif
}
void Node::age_out_stale_routes(RtEntry* rt, uint8_t& rt_count, bool team_plane) {
#if !MR_FEAT_TEAM
    (void)team_plane;   // §featuresplit: the team-plane _team_peer clear (below) is compiled out on a static build -> param unused
#endif
    // Walk rt[], evict each candidate past its hop-class TTL, drop empty entries,
    // dirty on primary eviction, one triggered re-beacon if any evicted
    // (dv_dual_sf.lua:5249-5302). ttl<=0 disables aging for that class.
    if (_cfg.rt_aging_ttl_neighbor_ms == 0 && _cfg.rt_aging_ttl_remote_ms == 0) return;
    const uint64_t now = _hal.now();
    bool any_evicted = false;
    uint8_t i = 0;
    while (i < rt_count) {
        RtEntry& e = rt[i];
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
                MR_EMIT("rt_aged", EF_I("dest", static_cast<int64_t>(e.dest)), EF_S("slot", (r == 0 ? "primary" : "alt")),
                        EF_I("next", static_cast<int64_t>(c.next_hop)), EF_I("hops", static_cast<int64_t>(c.hops)));
            } else {
                if (w != r) e.candidates[w] = e.candidates[r];
                ++w;
            }
        }
        e.n = w;
        if (e.n == 0) {
#if MR_FEAT_TEAM
            if (team_plane) _active->_team_peer[e.dest >> 3] &= static_cast<uint8_t>(~(1u << (e.dest & 7)));   // §6.2: no team route to e.dest left -> clear the dispatch bit (rt_find falls back to the static _rt; keeps the _team_peer <-> _rt_team invariant)
#endif
            rt_remove(i, rt, rt_count);   // do NOT advance i (entries shifted down)
        } else {
            if (primary_evicted) e.dirty = true;
            ++i;
        }
    }
    if (any_evicted) schedule_triggered_beacon();
}

void Node::rt_prune_cycle(uint8_t dest, uint8_t sender) { rt_prune_cycle(dest, sender, _active->_rt, _active->_rt_count); }
void Node::rt_prune_cycle(uint8_t dest, uint8_t sender, RtEntry* rt, uint8_t& rt_count) {
    // A beacon from `sender` reaching `dest` via US closes a me->X->sender->me
    // loop for any of our candidates for `dest` whose advertised next-hop
    // (n2_hop) is `sender`. Drop them (dv_dual_sf.lua:5193-5227). Direct
    // candidates (hops==1, n2_hop==0) carry no n2_hop, so the hops>1 guard skips
    // them even when sender==0.
    RtEntry* e = rt_find(dest, rt, rt_count);
    if (e == nullptr) return;
    uint8_t w = 0;
    bool primary_pruned = false, mutated = false;
    for (uint8_t r = 0; r < e->n; ++r) {
        const RtCandidate& c = e->candidates[r];
        if (c.hops > 1 && c.n2_hop == sender) {
            mutated = true;
            if (r == 0) primary_pruned = true;
            MR_EMIT("rt_prune", EF_I("dest", static_cast<int64_t>(dest)), EF_I("via", static_cast<int64_t>(c.next_hop)),
                    EF_I("sender", static_cast<int64_t>(sender)));
        } else {
            if (w != r) e->candidates[w] = e->candidates[r];
            ++w;
        }
    }
    if (!mutated) return;
    e->n = w;
    if (e->n == 0) {
        for (uint8_t i = 0; i < rt_count; ++i)            // e dangles after rt_remove — find idx first
            if (rt[i].dest == dest) { rt_remove(i, rt, rt_count); break; }
    } else if (primary_pruned) {
        e->dirty = true;
    }
    schedule_triggered_beacon();
}

// =============================================================================
// Peer-liveness + freshness plane (routing-liveness port, Lua dv:3986-4545).
// PHASE 1 — STATE ONLY: count RTS/ACK-timeout giveups -> suspect/silent/dead
// tiers (each with an expiry) + dest_seen for freshness; tracked + emitted but
// NOT YET applied to scoring/selection/cascade (the resort + triggered-beacon
// side-effects the Lua does here are DEFERRED to Phase 2/3 so P1 stays inert).
// =============================================================================
// §P2-4: the shared slot core (table-ref parameterization à la rt_merge) — find (or LRU-create) a slot in `tbl`.
// full -> evict the LEAST valuable. Prefer a HEALTHY slot (no live tier), stalest first — so an asymmetric DEAD
// peer (reached by TX, never heard => dest_seen 0 but a live tier) is NOT the first to go and re-trusted (the
// review's Gap A; matters once Phase 2 consults the tier). Fall back to the global stalest only if all are non-healthy.
Node::PeerLiveness* Node::peer_liveness_slot(uint8_t node_id, bool create, PeerLiveness* tbl, uint8_t& n, uint8_t cap) {
    for (uint8_t i = 0; i < n; ++i)
        if (tbl[i].node_id == node_id) return &tbl[i];
    if (!create) return nullptr;
    if (n < cap) {
        PeerLiveness& s = tbl[n++]; s = PeerLiveness{}; s.node_id = node_id; return &s;
    }
    const uint64_t now = _hal.now();
    int      best = 0;
    bool     best_healthy = false;
    uint64_t best_seen = 0;
    for (uint8_t i = 0; i < n; ++i) {
        const PeerLiveness& c = tbl[i];
        const bool healthy = !(c.dead_until_ms > now || c.silent_until_ms > now || c.suspect_until_ms > now);
        const bool better = (i == 0)
                          || (healthy && !best_healthy)                                   // a healthy slot beats any non-healthy one
                          || (healthy == best_healthy && c.dest_seen_ms < best_seen);     // same class -> the stalest
        if (better) { best = i; best_healthy = healthy; best_seen = c.dest_seen_ms; }
    }
    tbl[best] = PeerLiveness{}; tbl[best].node_id = node_id;
    return &tbl[best];
}
Node::PeerLiveness* Node::peer_liveness_slot(uint8_t node_id, bool create) {
    return peer_liveness_slot(node_id, create, _active->_peer_liveness, _active->_peer_liveness_n, protocol::cap_peer_liveness);
}

#if MR_FEAT_TEAM
// §2c: the TEAM-plane liveness slot — the SAME core over _team_liveness, keyed by team_local_id with its OWN on-demand
// LRU. NEVER reads _peer_liveness / _team_keys (that ring evicts by crypto-key recency = a different lifetime; sharing
// it would rebind suspect/dead state onto whoever next takes the slot).
Node::PeerLiveness* Node::team_liveness_slot(uint8_t team_local_id, bool create) {
    return peer_liveness_slot(team_local_id, create, _active->_team_liveness, _active->_team_liveness_n, protocol::cap_team_liveness);
}
// §team-parity T5: the const twin (the team bidi reads are const). See the header note for the C1 duplication with
// liveness_penalty_q4's inline scan.
const Node::PeerLiveness* Node::team_liveness_find(uint8_t team_local_id) const {
    const auto& L = *_active;
    for (uint8_t i = 0; i < L._team_liveness_n; ++i)
        if (L._team_liveness[i].node_id == team_local_id) return &L._team_liveness[i];
    return nullptr;
}
#endif

// Anti-spoof for the e2e-ack backstop exemption: a peer caught faking RTS_FLAG_E2E_ACK (its DATA was NOT a
// DATA_TYPE_E2E_ACK, verified at DATA-time in handle_data) has its e2e_ack_spoof_until_ms set = now + penalty.
// While that window holds, its RTS_FLAG_E2E_ACK is IGNORED (the backstop DROP re-applies). One free pass, then revoked.
// create=false: a peer never yet seen (no slot) is by definition not flagged.
bool Node::e2e_ack_spoofer_flagged(uint8_t src) {
    PeerLiveness* s = peer_liveness_slot(src, /*create=*/false);
    return s && s->e2e_ack_spoof_until_ms > _hal.now();
}

uint8_t Node::peer_suspect_level(uint8_t node_id) {
    if (node_id == 0 || node_id == _node_id) return 0;
    PeerLiveness* s = peer_liveness_slot(node_id, /*create=*/false);
    if (!s) return 0;
    const uint64_t now = _hal.now();                       // highest tier first; clear expired lazily (Lua get_peer_suspect_level)
    if (s->dead_until_ms    != 0) { if (s->dead_until_ms    > now) return 3; s->dead_until_ms    = 0; }
    if (s->silent_until_ms  != 0) { if (s->silent_until_ms  > now) return 2; s->silent_until_ms  = 0; }
    if (s->suspect_until_ms != 0) { if (s->suspect_until_ms > now) return 1; s->suspect_until_ms = 0; }
    return 0;
}

void Node::mark_peer_suspect(uint8_t node_id, uint8_t level, const char* source, uint8_t remote_src) {
    if (node_id == 0 || node_id == _node_id) return;
    PeerLiveness* s = peer_liveness_slot(node_id, /*create=*/true);
    if (!s) return;
    const uint64_t now  = _hal.now();
    const uint8_t  prev = peer_suspect_level(node_id);
    if      (level >= 3) s->dead_until_ms    = now + protocol::peer_dead_ttl_ms;
    else if (level >= 2) s->silent_until_ms  = now + protocol::peer_silent_ttl_ms;
    else                 s->suspect_until_ms = now + protocol::peer_suspect_ttl_ms;
    // §P4 gossip window: ONLY a LOCALLY-observed tier (remote_src==0 — rts_timeout is the sole local caller) is
    // advertised in our BCN suspect-TLV; a gossip-LEARNED tier (remote_src!=0) is applied above but NOT advertised
    // (anti-storm: a node never re-gossips a suspicion it heard, dv:1388-1390). DEAD clears the silent advertise (dv:4493).
    if (remote_src == 0) {
        if (level >= 3) { s->dead_advertise_until_ms = now + protocol::peer_dead_ttl_ms; s->suspect_advertise_until_ms = 0; }
        else            { s->suspect_advertise_until_ms = now + (level >= 2 ? protocol::peer_silent_ttl_ms : protocol::peer_suspect_ttl_ms); }
    }
    const uint8_t newl = peer_suspect_level(node_id);
    // §P2: a tier PROMOTION re-ranks routes via this now-penalized next-hop. §P4: a REMOTE (gossip) promotion reranks
    // LOCAL-ONLY (no dirty, no triggered beacon) so we never RE-ADVERTISE a heard suspicion (anti-storm guard #2,
    // dv:4501); a LOCAL promotion may re-advertise (local_only=false). (Lua mark_peer_suspect@4466/4504.)
    if (newl > prev) resort_routes_for_neighbor_penalty(node_id, source ? source : "peer_suspect", /*local_only=*/ remote_src != 0);
    MR_EMIT("peer_suspect_mark", EF_I("node", node_id), EF_I("level", newl), EF_I("previous_level", prev),
            EF_S("source", source ? source : "unknown"), EF_I("rts_timeouts", s->rts_timeouts), EF_I("remote_src", remote_src));
}

// §P2-4: the SHARED rts-timeout tier cascade — maps a running timeout `count` to a liveness tier (suspect@2 /
// silent@3 / dead@6-over-evidence-window), mutating first_timeout_ms exactly as both planes did inline. Returns the
// tier level (0=none). The THRESHOLD constants (the flagged drift risk) now live ONCE here. The APPLICATION of the
// tier stays per-plane at the call site: static -> mark_peer_suspect (advertise / resort / peer_suspect_mark emit);
// team -> the inline _team_liveness until-set + team_resort (no static gossip/advertise). No until fields are set
// here (static's mark_peer_suspect reads the PRIOR tier before setting them — moving the set here would corrupt it).
uint8_t Node::apply_timeout_tier(PeerLiveness& s, uint16_t count, uint64_t now) {
    if (count >= protocol::peer_silent_rts_timeouts) {                     // 3 -> at least SILENT; 6 over the evidence window -> DEAD
        if (s.first_timeout_ms == 0) s.first_timeout_ms = now;
        if (count >= protocol::peer_dead_rts_timeouts && (now - s.first_timeout_ms) >= protocol::peer_dead_evidence_window_ms)
            return 3;
        return 2;
    }
    if (count >= protocol::peer_suspect_rts_timeouts) return 1;            // 2 -> SUSPECT
    return 0;
}

// §P2-4: the SHARED recovery-on-heard clear-core — zero every tier + gossip-advertise field, return whether anything
// was live (the "emit only if `had`" gate). Both planes call it; the plane-only resort + emit stay at the call site.
// A team slot NEVER carries advertise fields (team never gossips) -> including them in `had` / clearing them is a
// no-op there = byte-identical to the old team clear that omitted them.
bool Node::clear_liveness_tiers(PeerLiveness& s) {
    const bool had = s.rts_timeouts != 0 || s.first_timeout_ms != 0 ||
                     s.suspect_until_ms != 0 || s.silent_until_ms != 0 || s.dead_until_ms != 0 ||
                     s.suspect_advertise_until_ms != 0 || s.dead_advertise_until_ms != 0;   // §P4 also stop gossiping it
    if (!had) return false;
    s.rts_timeouts = 0; s.first_timeout_ms = 0;
    s.suspect_until_ms = 0; s.silent_until_ms = 0; s.dead_until_ms = 0;
    s.suspect_advertise_until_ms = 0; s.dead_advertise_until_ms = 0;   // §P4: heard the peer -> stop advertising it as suspect (dv:4457-4463)
    return true;
}

void Node::record_peer_rts_timeout(uint8_t node_id, uint8_t ctr_lo, bool team_plane) {
    if (node_id == 0 || node_id == _node_id) return;
#if MR_FEAT_TEAM
    if (team_plane) {
        // §2c: accrue TEAM liveness on _team_liveness (self-slotted). Apply the tier untils INLINE from the shared
        // cascade — NO static mark_peer_suspect/resort. The demotion takes effect at the next refresh_route_order(
        // Plane::TEAM) in cascade_to_alt (effective_score reads the team penalty).
        PeerLiveness* ts = team_liveness_slot(node_id, /*create=*/true);
        if (!ts) return;
        ts->rts_timeouts = static_cast<uint16_t>(ts->rts_timeouts + 1);
        const uint16_t tn = ts->rts_timeouts;
        const uint64_t tnow = _hal.now();
        MR_EMIT("peer_rts_timeout_count", EF_I("node", node_id), EF_I("ctr_lo", ctr_lo), EF_I("count", tn), EF_S("plane", "team"));
        const uint8_t level = apply_timeout_tier(*ts, tn, tnow);
        if      (level >= 3) ts->dead_until_ms    = tnow + protocol::peer_dead_ttl_ms;
        else if (level >= 2) ts->silent_until_ms  = tnow + protocol::peer_silent_ttl_ms;
        else if (level >= 1) ts->suspect_until_ms = tnow + protocol::peer_suspect_ttl_ms;
        team_resort_routes_through(node_id);   // §2c: proactively re-sort _rt_team so candidates[0] updates NOW (else the next flight re-picks the demoted primary)
        return;
    }
#else
    (void)team_plane;
#endif
    PeerLiveness* s = peer_liveness_slot(node_id, /*create=*/true);
    if (!s) return;
    s->rts_timeouts = static_cast<uint16_t>(s->rts_timeouts + 1);
    const uint16_t n = s->rts_timeouts;
    MR_EMIT("peer_rts_timeout_count", EF_I("node", node_id), EF_I("ctr_lo", ctr_lo), EF_I("count", n));
    const uint8_t level = apply_timeout_tier(*s, n, _hal.now());
    if (level) mark_peer_suspect(node_id, level, "rts_timeout");   // static: the full tier machine (advertise/resort/peer_suspect_mark emit)
}

void Node::clear_peer_suspect(uint8_t node_id, const char* source, bool team_plane) {
    if (node_id == 0 || node_id == _node_id) return;
#if MR_FEAT_TEAM
    if (team_plane) {
        // §2c: recovery-on-heard on the TEAM liveness (mandatory — else a transiently-missed team relay stays demoted
        // forever). Clear the team slot's tiers; no static resort (the recovery lands at the next refresh_route_order(TEAM)).
        PeerLiveness* ts = team_liveness_slot(node_id, /*create=*/false);
        if (!ts || !clear_liveness_tiers(*ts)) return;
        team_resort_routes_through(node_id);   // §2c: recovery -> re-sort _rt_team (the recovered relay may regain primacy)
        MR_EMIT("peer_suspect_clear", EF_I("node", node_id), EF_S("source", source ? source : "team_rx"), EF_S("plane", "team"));
        return;
    }
#else
    (void)team_plane;
#endif
    PeerLiveness* s = peer_liveness_slot(node_id, /*create=*/false);
    if (!s || !clear_liveness_tiers(*s)) return;             // nothing to clear -> no event (Lua: emit only if `had`)
    // §P2: the penalty is lifted -> re-rank routes via this recovered next-hop (a demoted route may regain primacy) +
    // re-advertise on a primary change. dest_seen_ms left intact (a separate freshness fact). (Lua clear_peer_suspect@4501.)
    resort_routes_for_neighbor_penalty(node_id, source ? source : "peer_suspect_clear", /*local_only=*/false);
    MR_EMIT("peer_suspect_clear", EF_I("node", node_id), EF_S("source", source ? source : "rx_frame"));
}

void Node::mark_dest_seen(uint8_t node_id) {
    if (node_id == 0 || node_id == 0xFF || node_id == _node_id) return;
    const uint64_t now = _hal.now();
    _active->_dest_seen_ms[node_id] = now;                           // the freshness map (full range, no eviction) -> is_next_hop_fresh
    PeerLiveness* s = peer_liveness_slot(node_id, /*create=*/true);  // also track the direct neighbour for the liveness tiers + LRU dest_seen tiebreak
    if (s) s->dest_seen_ms = now;
}

bool Node::is_next_hop_fresh(uint8_t node_id) const {
    if (node_id == _node_id) return true;                  // self is always reachable
    const uint64_t seen = _active->_dest_seen_ms[node_id]; // dedicated freshness map (full range, survives PeerLiveness LRU eviction)
    if (seen == 0) return false;                           // never heard -> not fresh
    return (_hal.now() - seen) <= protocol::next_hop_live_ttl_ms;
}

// ① mobile-as-transit avoidance (Lua dv:1325-1334). is_mobile_peer reads the per-layer SET-only bitset.
bool Node::is_mobile_peer(uint8_t id) const {
    return (_active->_mobile_peer[id >> 3] >> (id & 7)) & 1u;
}
#if MR_FEAT_TEAM   // §featuresplit: bodies compiled out on a static-only build (the header inline-stubs these to inert)
bool Node::is_team_peer(uint8_t id) const {   // §mobile 6.2: a known same-team peer -> route via _rt_team
    return (_active->_team_peer[id >> 3] >> (id & 7)) & 1u;
}
// §enc: cache a same-team peer's key_hash32 (from its beacon). Team-SCOPED — NEVER _id_bind (the static plane, §18).
// Upsert by id; on a full ring evict the OLDEST. Read by team_key_of_id for an ENCRYPTED send BY team_local_id.
//
// ★★★ §id-hash S3 (spec 2026-08-01 §3-D2 + §3-D5c) — THIS TABLE NOW CARRIES THE CONFIDENCE LADDER, AND THE TWO RULES
// BELOW ARE WHAT KEEP IT HONEST. Owner ruling that frames both (2026-08-01): *"we can be sure only if we scan QR or
// exchange keys out of network. Otherwise we never can be sure."* ⇒ hash->pubkey self-verifies, id->hash does NOT, so
// an on-air id->hash answer is a CLAIM and must never be able to displace first-hand evidence.
//
// ① UPGRADE-ONLY, mirroring what §id-hash S2b established for the sibling `_id_bind` (node_hashlocate.cpp:153-181)
//    and what `peer_key_set` has always done for `_peer_keys` (:332-343). A `claimed` write onto an `authoritative`
//    row is a COMPLETE no-op: it may not demote the tier, may not relabel `source` (keeping `authoritative` while
//    writing `source = h_relay` would name a relay as the authority — the provenance mislabelling spec §3-D8
//    refuses), may not overwrite the hash, and — spec §3-D5c, the part that is easy to miss — may not refresh
//    `last_seen_ms`. On an authoritative row that stamp means *"when we last had FIRST-HAND evidence"*, and
//    node_hashlocate.cpp's own on_hash_bind_snoop header already named the hazard for THIS table: cache-on-pass
//    *"would both fake that liveness and let transit traffic evict genuine beacon rows."* Refreshing on hearsay is
//    exactly that fake. ⚠ THE CONSEQUENCE IS INTENDED: a teammate we only ever hear ABOUT ages out at 48 h.
//
// ② EVICTION PREFERS A CLAIMED VICTIM (spec §3-D5c's second bullet). Sixteen slots, evict-oldest: without this a
//    2-hop by-id query storm would evict the genuine beacon rows the seal/DST_HASH path depends on. Same shape as
//    peer_key_set's "evict the oldest NON-pinned" (U3) — except this one FALLS BACK to the oldest authoritative
//    instead of refusing, because the incoming write here is itself first-hand and refusing it would be worse.
//
// ⓘ NO SECOND TIMESTAMP, and that was the open question the dispatch asked me to answer by measurement rather than
//   assume. `last_seen_ms` is read for two different meanings — the 48 h freshness gate in team_key_of_id and the
//   eviction order here — and ONE field serves both, because the two rules above remove the only conflict: (a) a
//   claim can no longer touch an authoritative row's stamp, so within the authoritative cohort it still means
//   "first-hand evidence"; (b) a claimed row's own stamp is the age of the claim, which is the correct LRU key for
//   it AND the correct input to its own TTL; (c) the two cohorts are never LRU-compared against each other, because
//   the claimed cohort is drained first. A second field would have cost 16 slots x MR_N_LAYERS.
// ⓘ TELEMETRY ON THE REFUSALS was deliberately omitted by S3 (which shipped no producer of a claimed team binding)
//   and is STILL omitted by §id-hash S4a, which created the producer: the refusal fires 13 times across the corpus's
//   five team scenarios, so an emit here would re-anchor those streams in the same run as the behaviour change and
//   make the delta unattributable (C4 applied to telemetry). Coverage is native + the S4a probe matrix.
void Node::team_key_set(uint8_t id, uint32_t key_hash32, IdBindSource source, IdBindConf confidence) {
    if (id == 0 || id == 0xFF || key_hash32 == 0) return;
    auto& L = *_active;
    const bool authoritative = (confidence == IdBindConf::authoritative);
    for (uint8_t i = 0; i < L._team_keys_n; ++i)
        if (L._team_keys[i].id == id) {
            const bool existing_auth = (L._team_keys[i].confidence == static_cast<uint8_t>(IdBindConf::authoritative));
            if (!authoritative && existing_auth) return;         // ① a claim cannot demote, relabel, rebind OR re-date first-hand evidence
            L._team_keys[i].key_hash32   = key_hash32;
            L._team_keys[i].last_seen_ms = _hal.now();
            L._team_keys[i].source       = static_cast<uint8_t>(source);
            L._team_keys[i].confidence   = static_cast<uint8_t>(confidence);
            return;
        }
    uint8_t slot;
    if (L._team_keys_n < static_cast<uint8_t>(sizeof(L._team_keys) / sizeof(L._team_keys[0]))) {
        slot = L._team_keys_n++;
    } else {                                                     // full -> evict the OLDEST, ② CLAIMED COHORT FIRST
        slot = 0; uint64_t oldest = ~0ull; bool claimed_victim = false;
        for (uint8_t i = 0; i < L._team_keys_n; ++i) {
            const bool row_claimed = (L._team_keys[i].confidence != static_cast<uint8_t>(IdBindConf::authoritative));
            if (claimed_victim && !row_claimed) continue;        // already holding a claimed victim -> authoritative rows are out of the running
            if (row_claimed && !claimed_victim) { claimed_victim = true; oldest = ~0ull; }   // first claimed row RESTARTS the LRU search inside its cohort
            if (L._team_keys[i].last_seen_ms < oldest) { oldest = L._team_keys[i].last_seen_ms; slot = i; }
        }
    }
    L._team_keys[slot] = { id, static_cast<uint8_t>(source), static_cast<uint8_t>(confidence), key_hash32, _hal.now() };
}
// §enc: team-scoped id->key (for a CRYPTED send by team_local_id). §id-hash S3: `min` is the confidence FLOOR and
// `actual` reports the tier of the row that answered (nullptr = don't care). The default floor is `authoritative`,
// which is exactly the pre-S3 behaviour: the only writer today is the heard beacon, so nothing is filtered out yet.
bool Node::team_key_of_id(uint8_t id, uint32_t& out, IdBindConf min, IdBindConf* actual) const {
    if (_cfg.team_id == 0 || !is_team_peer(id)) return false;   // only a known same-team peer (gate on team membership + the peer bitmap)
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_team_keys_n; ++i)
        if (_active->_team_keys[i].id == id) {
            if (now - _active->_team_keys[i].last_seen_ms > protocol::id_bind_ttl_ms) return false;   // §P2-6: stale (>48 h) -> absent — the TTL the sibling id_bind/peer_key stores already carry (id unique -> no fresher dup)
            const IdBindConf conf = static_cast<IdBindConf>(_active->_team_keys[i].confidence);
            if (static_cast<uint8_t>(conf) < static_cast<uint8_t>(min)) return false;   // below the floor -> absent (id unique -> no better dup to keep scanning for)
            if (actual) *actual = conf;
            out = _active->_team_keys[i].key_hash32; return true;
        }
    return false;
}
// §mobile 6.4: reverse (hash->team_local_id) for a PLAINTEXT send-by-hash to a HEARD teammate.
// ★★ §id-hash S3: THIS is the reader v1 of the spec missed (§3-D1). It is on the LIVE send path — do_send's `-t`
// arm and the AUTO mobile cascade both resolve through it — and before S3 it accepted every fresh row with no
// confidence test at all, so a claimed binding would have gone straight into addressing a real transmission.
// Same default floor as the forward reader, so the two directions cannot disagree (spec §9: "claimed forward AND
// reverse lookups fail under the default authoritative floor — team_id_of_key included"). B30 additionally requires
// the freshest qualifying alias, shared with the address-book resolver rather than a first-match table scan.
bool Node::team_id_of_key(uint32_t key_hash32, uint8_t& out_id, IdBindConf min, IdBindConf* actual) const {
    uint8_t alias_dropped = 0;
    IdBindConf found = IdBindConf::claimed;
    const uint8_t id = team_id_of_key_freshest(key_hash32, alias_dropped, min, &found);
    if (id == 0) return false;                      // leave both outputs untouched on a miss
    out_id = id;
    if (actual) *actual = found;
    return true;
}
#endif   // MR_FEAT_TEAM
// True iff routing to `dest` via `next_hop` would relay THROUGH a mobile peer. The next_hop != dest carve-out is the
// whole point: deliver TO a mobile (it's the dest) is fine; relaying THROUGH one (it'll roam away) is not. (dv:1329-1334)
bool Node::route_uses_mobile_as_transit(uint8_t dest, uint8_t next_hop) const {
    // §mobile 6.2: a SAME-TEAM peer IS a legal transit — the whole point of the team plane is A->B->C through teammates.
    // This predicate gates BOTH merge (rt_merge) AND send-time selection (next_hop_selectable); without the carve-out the
    // select gate rejects a teammate transit even though the route lives in _rt_team. is_team_peer is false for any static
    // node / non-team member (a team_peer bit is set only when _cfg.team_id != 0), so the static plane is byte-identical.
    if (!(next_hop != 0 && dest != 0 && next_hop != dest && is_mobile_peer(next_hop) && !is_team_peer(next_hop)))
        return false;
    // §S0 ALIAS CARVE: is_mobile_peer is a SET-only per-id bitset — a hosted mobile's local id (e.g. 18) sets it when the
    // mobile beacons, but that SAME numeric id can be a real STATIC neighbour's node_id (the cold-boot pool alias). If we
    // hold an AUTHORITATIVE id_bind for next_hop whose hash is NOT one of OUR hosted mobiles, next_hop is a CONFIRMED
    // static -> a legitimate transit; do NOT reject it (the metal bug: home couldn't route dest 19 via next 18==S2 at all).
    // This does NOT rescue a genuine roaming mobile: a mobile's LOCAL id never enters the static id_bind plane (§18), so a
    // real is_mobile peer has no authoritative binding here and still returns true. Static-inert: with no hosted mobiles
    // nothing set the bit for a static id, so the guard above already returned false before reaching here.
    uint32_t bound_hash = 0;
    if (key_hash_of_id(next_hop, bound_hash)) {
        for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
            if (_active->_mobile_reg[i].key_hash32 == bound_hash) return true;   // the binding IS a hosted mobile -> genuine mobile transit
        return false;                                                            // authoritative binding to a non-hosted (static) hash -> real static transit
    }
    return true;
}

}  // namespace meshroute
