// MeshRoute — lib/core/jittered_tx_stash.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ONE discipline for the family of "de-storm" stashes (§3-B item 5 of
// docs/2026-07-20-realism-and-duplication-review.md): a frame that several nodes would otherwise key
// up for in the SAME millisecond is not transmitted where it is built — it is copied into a stash and
// a one-shot timer fires it after a uniform jitter draw, so the existing LBT defers the later sibling.
//
// Three members today, all previously hand-rolled:
//   • §F-XL-1  H-forward     — ring of 4, node_hashlocate.cpp     (sibling relays of one hash-locate flood)
//   • §F-XL-2  RREQ-forward  — ring of 4, node_route_discovery.cpp (sibling relays of one AODV RREQ flood)
//   • §S6/QA-3b mobile OFFER — SINGLE slot, node_join.cpp          (co-located hosts answering one DISCOVER)
// A RING is used where concurrent floods for DIFFERENT keys must not clobber one another; the OFFER is
// deliberately single-slot (last DISCOVER wins). Both shapes are the same ritual, which is why the ring
// entry point is written in terms of the single-slot one rather than beside it.
//
// ★ WHY NON-OWNING (free functions over `Slot(&)[Cap]` + `uint8_t& rr`, not a `Stash<Cap>` member type):
// these are Node members and `node.h` carries `static_assert(sizeof(Node) == …)`. The two rings are in
// fact the exact members whose introduction last moved that number (+312 §F-XL-1, +192 §F-XL-2), and
// `_h_forward_rr` is recorded as packing into a neighbouring member's slack. Wrapping array+cursor in a
// class would give the pair its own tail padding and move the object layout; operating on the EXISTING
// members touches no member at all, so `sizeof(Node)` is identical *by construction*. Same reasoning as
// recent_ring.h (§3-B item 1), and the OFFER slot — a bare `uint8_t buf[13]` + `uint8_t len` inside
// LayerState, not a struct — drops out for free because the single-slot entry point takes the two
// pieces separately.
//
// SLOT CONTRACT (both ring entry structs satisfy it): `uint8_t buf[N]` + `uint8_t len`, where `len == 0`
// means "nothing armed / already fired". Plain aggregate members, so `= {}` init and the layout are
// unchanged.
//
// ★ INVARIANTS THIS HEADER NOW OWNS — each was previously re-typed per site, and one site had drifted:
//   1. A frame that does not fit is REFUSED WHOLE: no copy, no RNG draw, no timer. Silent (as before) —
//      the caller's flood dedup has already marked the frame seen, so a refused forward simply does not
//      propagate, exactly as it did when the guard was inline.
//      ⚠ The OFFER site had NO such guard (it relied on pack_j_offer's own bound); it inherits one here.
//   2. The round-robin cursor advances ONLY for an ACCEPTED frame — a refused stash must not burn a slot
//      (the pre-existing order at both ring sites: guard, then read the cursor, then advance).
//   3. `Cap` is deduced from the array's own extent, so the slot index, the `% Cap` wrap and the
//      `timer_base + slot` id can never disagree with the array the frame was written into.
//   4. The jitter is `hal.rand_range(min, max + 1)` — a draw from the SHARED sim RNG stream, INCLUSIVE
//      of `max`. Keep it a draw: the sim's draw ORDER is load-bearing (an added or removed draw
//      phantom-shifts unrelated scenario assertions), and the deterministic-mix alternative used by the
//      §F-SL-1 parked re-flood is a different mechanism, not a substitute.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: the gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include <cstddef>
#include <cstdint>
#include "hal.h"

namespace MESHROUTE_NS {

// Single-slot: copy `src[0..n)` into the stash and arm `timer_id` after a jitter of
// [jitter_min_ms, jitter_max_ms] ms. No-op (invariant 1) if the frame is empty or does not fit.
inline void jtx_stash_arm(Hal& hal, uint8_t* buf, std::size_t cap, uint8_t& len,
                          const uint8_t* src, std::size_t n,
                          uint16_t jitter_min_ms, uint16_t jitter_max_ms, uint32_t timer_id) {
    if (n == 0 || n > cap) return;
    for (std::size_t i = 0; i < n; ++i) buf[i] = src[i];
    len = static_cast<uint8_t>(n);
    const uint32_t jit = static_cast<uint32_t>(hal.rand_range(jitter_min_ms, jitter_max_ms + 1));
    (void)hal.after(jit, timer_id);   // timer table full -> the frame stays stashed and is simply never sent
}

// Ring: take the round-robin slot, then stash+arm it on `timer_base + slot` (the fire side recovers the
// slot as `timer_id - timer_base`). The fit guard runs BEFORE the cursor moves — invariant 2.
template <typename Slot, std::size_t Cap>
void jtx_ring_arm(Hal& hal, Slot (&ring)[Cap], uint8_t& rr,
                  const uint8_t* src, std::size_t n,
                  uint16_t jitter_min_ms, uint16_t jitter_max_ms, uint32_t timer_base) {
    static_assert(Cap >= 1 && Cap <= 255, "jtx ring: the round-robin cursor is a uint8_t");
    if (n == 0 || n > sizeof(ring[0].buf)) return;
    const uint8_t slot = rr;
    rr = static_cast<uint8_t>((rr + 1) % Cap);
    jtx_stash_arm(hal, ring[slot].buf, sizeof(ring[slot].buf), ring[slot].len,
                  src, n, jitter_min_ms, jitter_max_ms, timer_base + slot);
}

// Fire side: the slot's armed frame, or nullptr for an out-of-range slot / one that already fired.
template <typename Slot, std::size_t Cap>
Slot* jtx_ring_armed(Slot (&ring)[Cap], uint8_t slot) {
    if (slot >= Cap) return nullptr;
    return ring[slot].len ? &ring[slot] : nullptr;
}

}  // namespace MESHROUTE_NS
