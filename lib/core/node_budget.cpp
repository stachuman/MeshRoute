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

namespace MESHROUTE_NS {

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
// FIRST same-kind+dedup-key event within the retry window instead of appending (a retry is not a new
// origination). The `ctr_lo` slot is the per-kind dedup key: ctr_lo for RTS, rx_id for CTS (CTS dropped
// ctr_lo). Insertion-ordered so the dedup-first matches the Lua ipairs scan. Draw-free.
// FIXED RING (no per-frame heap): prune+dedup compact IN PLACE; on overflow evict the oldest. See node.h.
void Node::track_originator_observation(uint8_t sender, uint8_t kind, uint8_t ctr_lo, uint32_t air) {
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::originator_window_ms) ? (now - protocol::originator_window_ms) : 0;
    OrigRing& r = _active->_per_sender_originator[sender];

    // Prune expired + dedup-first, compacting survivors to the front in insertion order. w <= i throughout,
    // so writing r.ev[w] never clobbers an unread r.ev[i].
    uint8_t w = 0;
    bool dedup_hit = false;
    for (uint8_t i = 0; i < r.count; ++i) {
        if (r.ev[i].t < cutoff) continue;                 // prune expired
        if (!dedup_hit && r.ev[i].kind == kind && r.ev[i].ctr_lo == ctr_lo
            && (now - r.ev[i].t) < protocol::originator_retry_dedup_ms) {
            r.ev[i].t = now;                               // retry -> refresh, don't add a new event
            dedup_hit = true;
        }
        r.ev[w++] = r.ev[i];                              // keep
    }
    r.count = w;

    if (!dedup_hit) {
        if (r.count < protocol::cap_originator_events) {
            r.ev[r.count++] = OrigEvent{ now, kind, ctr_lo, air };
        } else {
            // Ring full: a sender with >cap non-deduped events in the window is spamming — drop the OLDEST
            // and keep the most recent, which preserves the anti-spam signal (recent rts/cts/air).
            for (uint8_t i = 1; i < r.count; ++i) r.ev[i - 1] = r.ev[i];
            r.ev[r.count - 1] = OrigEvent{ now, kind, ctr_lo, air };
        }
    }
}

// R4.4 sliding-window metric (dv:3263-3293). DISTINCT dedup-key per kind, cumulative airtime.
// apparent = max(0, rts - cts). Dedup key: RTS uses ctr_lo (4-bit -> 16-bit mask); CTS uses rx_id (the
// cleared requester, 0..254 -> 256-bit mask) since CTS dropped ctr_lo. Const (read-only). Draw-free.
void Node::compute_originator_metric(uint8_t sender, int& apparent, uint32_t& total_air,
                                     uint8_t& rts, uint8_t& cts) const {
    apparent = 0; total_air = 0; rts = 0; cts = 0;
    auto it = _active->_per_sender_originator.find(sender);
    if (it == _active->_per_sender_originator.end()) return;
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::originator_window_ms) ? (now - protocol::originator_window_ms) : 0;
    uint16_t rts_seen = 0;                 // RTS: distinct 4-bit ctr_lo
    uint8_t  cts_seen[32] = {};            // CTS: distinct rx_id (0..254) — dedup key is rx_id, not ctr_lo
    const OrigRing& r = it->second;
    for (uint8_t i = 0; i < r.count; ++i) {
        const OrigEvent& ev = r.ev[i];
        if (ev.t < cutoff) continue;
        total_air += ev.air;
        const uint8_t key = ev.ctr_lo;     // dedup-key slot: ctr_lo for RTS, rx_id for CTS
        if (ev.kind == 0) {
            const uint16_t bit = static_cast<uint16_t>(1u << (key & 0x0f));
            if (!(rts_seen & bit)) { rts_seen |= bit; ++rts; }
        } else if (ev.kind == 1) {
            if (!(cts_seen[key >> 3] & (1u << (key & 7)))) { cts_seen[key >> 3] |= static_cast<uint8_t>(1u << (key & 7)); ++cts; }
        }
    }
    const int a = static_cast<int>(rts) - static_cast<int>(cts);
    apparent = (a < 0) ? 0 : a;
}

}  // namespace meshroute
