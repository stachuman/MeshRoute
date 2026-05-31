// MeshRoute — lib/core/node_budget.cpp  (R4 duty-cycle budget + anti-spam metric plane)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The route-free duty-cycle tier from the rolling airtime window (compute_budget_tier),
// and the R4.4 originator anti-spam ledger: per-sender observation tracking with
// retry-dedup (track_originator_observation) + the sliding-window distinct-ctr_lo
// metric (compute_originator_metric). Consumed by the MAC RX handlers (budget-aware
// NACK, originator drop, ACK budget hint). Draw-free. Behaviour mirrors dv_dual_sf.lua.
// Part of the Node class (declared in node.h); split out of node_mac.cpp for readability.
#include "node.h"

#include <vector>
#include <utility>

namespace meshroute {

// R4.0: route-free duty-cycle tier from the rolling airtime window (Lua dv:3560-3571).
// Integer pct is tier-identical to the Lua float at the {50,80,95} integer thresholds.
Node::BudgetTier Node::compute_budget_tier() const {
    if (_cfg.duty_cycle <= 0.0 || _duty_cycle_budget_ms == 0) return BudgetTier::healthy;  // disabled
    const uint64_t used = _hal.airtime_used_ms(_cfg.duty_cycle_window_ms);
    const uint64_t pct  = (100ull * used) / _duty_cycle_budget_ms;
    if (pct >= protocol::budget_exhausted_pct) return BudgetTier::exhausted;
    if (pct >= protocol::budget_critical_pct)  return BudgetTier::critical;
    if (pct >= protocol::budget_strained_pct)  return BudgetTier::strained;
    return BudgetTier::healthy;
}

// R4.4 anti-spam ledger append (dv:3205-3239). Prune events older than the window; DEDUP — refresh the
// FIRST same-kind+ctr_lo event within the retry window instead of appending (a retry is not a new
// origination). Insertion-ordered vector so the dedup-first matches the Lua ipairs scan. Draw-free.
void Node::track_originator_observation(uint8_t sender, uint8_t kind, uint8_t ctr_lo, uint32_t air) {
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::originator_window_ms) ? (now - protocol::originator_window_ms) : 0;
    auto& events = _per_sender_originator[sender];
    std::vector<OrigEvent> kept;
    bool dedup_hit = false;
    for (auto& ev : events) {
        if (ev.t < cutoff) continue;                      // prune expired
        if (!dedup_hit && ev.kind == kind && ev.ctr_lo == ctr_lo
            && (now - ev.t) < protocol::originator_retry_dedup_ms) {
            ev.t = now;                                    // retry -> refresh, don't add a new event
            dedup_hit = true;
        }
        kept.push_back(ev);
    }
    if (!dedup_hit) kept.push_back(OrigEvent{ now, kind, ctr_lo, air });
    events = std::move(kept);
}

// R4.4 sliding-window metric (dv:3247-3277). DISTINCT ctr_lo per kind (ctr_lo is 4-bit -> a 16-bit mask),
// cumulative airtime. apparent = max(0, rts - cts). Const (read-only). Draw-free.
void Node::compute_originator_metric(uint8_t sender, int& apparent, uint32_t& total_air,
                                     uint8_t& rts, uint8_t& cts) const {
    apparent = 0; total_air = 0; rts = 0; cts = 0;
    auto it = _per_sender_originator.find(sender);
    if (it == _per_sender_originator.end()) return;
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::originator_window_ms) ? (now - protocol::originator_window_ms) : 0;
    uint16_t rts_seen = 0, cts_seen = 0;                  // distinct-ctr_lo masks (bit per 4-bit ctr_lo)
    for (const auto& ev : it->second) {
        if (ev.t < cutoff) continue;
        total_air += ev.air;
        const uint16_t bit = static_cast<uint16_t>(1u << (ev.ctr_lo & 0x0f));
        if      (ev.kind == 0) { if (!(rts_seen & bit)) { rts_seen |= bit; ++rts; } }
        else if (ev.kind == 1) { if (!(cts_seen & bit)) { cts_seen |= bit; ++cts; } }
    }
    const int a = static_cast<int>(rts) - static_cast<int>(cts);
    apparent = (a < 0) ? 0 : a;
}

}  // namespace meshroute
