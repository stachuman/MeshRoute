// MeshRoute — lib/core/recent_ring.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ONE discipline for the family of bounded "have I seen this recently / mark it seen" rings
// (§3-B item 1 of docs/2026-07-20-realism-and-duplication-review.md). Seven of them existed as
// hand-rolled twins — F RREQ flood-dedup (+ its team-plane twin), H hash-query flood-dedup, the
// channel re-pull window, the Q responder window, the L2a mediated-DENY window, the L2c
// redirect-suppression window and the DAD denied-id list — each writing the same four steps
// (scan-for-key / refresh-in-place / append-if-room / evict-oldest-if-full) and each writing its
// KEY PREDICATE TWICE (once in `…_recently`, once in `mark_…`). That doubled predicate is the
// missed-twin class the review ranked highest: adding a key field to one copy and not the other
// silently widens or narrows the dedup window.
//
// ★ WHY NON-OWNING (free functions over `Entry(&)[Cap]`, not a `RecentRing<Entry,Cap>` member type):
// every one of these rings is a Node member and `node.h` carries `static_assert(sizeof(Node) == …)`.
// Wrapping array+count in a class would give that pair its own tail padding (the `uint8_t` count
// would stop packing into the following member's slack), moving the object layout. Operating on the
// EXISTING `Entry array[]` + `uint8_t n` members keeps `sizeof(Node)` identical *by construction* —
// no member is touched at all. It also absorbs the two structural variations for free: a ring may
// live in `_active` (per-leaf) or be Node-global, and the F ring is a per-plane PAIR of different
// capacities selected at runtime — the caller passes whichever array applies.
//
// ENTRY CONTRACT (each of the seven entry structs satisfies it):
//   • `uint64_t t_ms`                          — the mark stamp.
//   • `bool same_key(const Entry&) const`      — the ring's identity, defined ONCE.
// Both are plain members of an aggregate, so `= {}` and `{ a, b, now }` init still work and the
// struct layout is unchanged (a non-static member function contributes no storage).
//
// ★ WINDOW BOUNDARY — UNIFIED, deliberately. Before this header two boundary forms coexisted:
// a clamped INCLUSIVE one (`cutoff = now>=ttl ? now-ttl : 0; recent iff t_ms >= cutoff`, i.e.
// age <= ttl) used by the F and H rings, and a subtract-and-compare EXCLUSIVE one
// (`recent iff (now - t_ms) < ttl`, i.e. age < ttl) used by the other five. They differ ONLY for an
// entry whose age is exactly `ttl` (below the first full window both forms are unconditionally
// true, so the early-boot regime already agreed). The clamped form is kept because it cannot
// underflow if `now` ever moves backwards — the exclusive form would then compute a huge age and
// fail OPEN (suppression lost, re-flood). Verified unobservable: the whole 27-scenario corpus is
// byte-identical across the unification.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: the gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include <cstdint>
#include <cstddef>

namespace MESHROUTE_NS {

// The window test, in one place. Clamped so the first `ttl` milliseconds after boot never underflow.
inline uint64_t recent_ring_cutoff(uint64_t now, uint64_t ttl) { return (now >= ttl) ? now - ttl : 0; }

// Is `probe`'s key present with a stamp inside the window? `probe.t_ms` is ignored.
template <typename Entry, std::size_t Cap>
bool recent_ring_hit(const Entry (&ring)[Cap], uint8_t n, const Entry& probe, uint64_t now, uint64_t ttl) {
    const uint64_t cutoff = recent_ring_cutoff(now, ttl);
    for (uint8_t i = 0; i < n; ++i)
        if (ring[i].same_key(probe) && ring[i].t_ms >= cutoff) return true;
    return false;
}

// Refresh `fresh`'s key in place, else append, else (ring full) overwrite the oldest stamp.
// `fresh` carries the new stamp in its own `t_ms`, so the ring never needs a second `now`.
template <typename Entry, std::size_t Cap>
void recent_ring_mark(Entry (&ring)[Cap], uint8_t& n, const Entry& fresh) {
    static_assert(Cap <= 255, "recent_ring: the live count is a uint8_t");
    for (uint8_t i = 0; i < n; ++i)
        if (ring[i].same_key(fresh)) { ring[i].t_ms = fresh.t_ms; return; }   // refresh the window
    if (n < static_cast<uint8_t>(Cap)) { ring[n++] = fresh; return; }
    uint8_t o = 0;                                                           // full -> evict the oldest
    for (uint8_t i = 1; i < n; ++i) if (ring[i].t_ms < ring[o].t_ms) o = i;
    ring[o] = fresh;
}

// Periodic sweep: compact the survivors to the front, drop everything outside the window. Uses the
// SAME boundary as recent_ring_hit, so a sweep can never drop an entry `…_recently` still honours.
template <typename Entry, std::size_t Cap>
void recent_ring_age_out(Entry (&ring)[Cap], uint8_t& n, uint64_t now, uint64_t ttl) {
    const uint64_t cutoff = recent_ring_cutoff(now, ttl);
    uint8_t w = 0;
    for (uint8_t r = 0; r < n; ++r) if (ring[r].t_ms >= cutoff) ring[w++] = ring[r];
    n = w;
}

}  // namespace MESHROUTE_NS
