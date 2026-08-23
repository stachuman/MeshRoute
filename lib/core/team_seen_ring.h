// MeshRoute — lib/core/team_seen_ring.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ §UI-16 N1 (spec docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md §2.1) —
// THE JOINER'S READ-ONLY NEARBY-TEAM OBSERVATION CACHE, as a PURE unit.
//
// A teamless (or differently-teamed) node hears team beacons on its current PHY: every team beacon
// carries the sender's team_id in the type-5 TLV (pack_team_id_tlv / parse_team_id_tlv,
// frame_codec.cpp). Before this header that id was parsed into a stack local, used twice as a
// predicate and DISCARDED (node_beacon.cpp, `peer_team`). The onboarding UI needs to *list* the
// teams that are audible, so the id is now RETAINED here — and NOTHING ELSE happens with it.
//
// ★★★ READ-ONLY BY CONSTRUCTION, AND THAT IS THE WHOLE POINT (spec §3 P-3/P-4). Observing a team is
// not peering with it, not routing to it, not binding an identity and not joining it. This unit
// touches ONE array. It calls no route merge, no `team_key_set`, no `id_bind_set`, no `peer_key_set`,
// no NV path; it sends nothing, arms no timer and emits NO telemetry (⛔ no `MR_EMIT` on this path —
// telemetry would re-anchor the team corpus streams in the same run as the behaviour change and make
// the delta unattributable; C4 applied to telemetry, the sibling refusal at node_routing.cpp:855-858).
//
// ★ WHY A NON-OWNING PURE UNIT (free functions over `TeamSeen(&)[Cap]` + a `uint8_t& n`), and not a
// `TeamSeenRing<Cap>` member type: recent_ring.h's own argument applies verbatim — `node.h` carries a
// `static_assert(sizeof(Node) == …)`, and wrapping array+count in a class gives that pair its own tail
// padding (the count byte stops packing into the following member's slack), moving the object layout.
// Operating on the EXISTING array + count members keeps the placement measurable and deliberate. It
// also makes every decision below drivable by the native suite with NO Node, NO HAL and NO friend seam.
//
// ★ AND IT IS NOT `recent_ring.h`, though that header is the closest relative and IS reused for the
// window boundary (`recent_ring_cutoff`, so "inside the window" has ONE definition in this tree).
// Two of recent_ring's four steps differ HERE, and both differences are ruled, not incidental:
//   · recent_ring's refresh updates ONLY `t_ms`; this record also folds the new SNR sample into an EWMA;
//   · recent_ring's overflow OVERWRITES the oldest slot IN PLACE, which shuffles the display order under
//     the operator's cursor. This ring SHIFTS the oldest out and APPENDS, which is what makes
//     first-observed order STRUCTURAL (owner ruling R-5/R-2(iii), 2026-08-22) rather than a sort at read.
// ⇒ a forced fit would have had to fork recent_ring's mark step anyway; the boundary is shared instead.
//
// ⓘ NO CONSUMER YET, AND THE ONE ARRIVING IS NAMED (the `ui_fmt_team_fingerprint` precedent,
// src/firmware_ui_chrome.h:204-208): N1 lands the cache and its two `const` accessors ONLY. The reader
// is §UI-16 slice N2 — the PROVISION → `JOIN TEAM` → NEARBY list, which projects each retained entry to
// `fingerprint · n/3 signal · age`. ⛔ N1 adds no screen, no console verb, no push and no wire byte.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: the gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include <cstdint>
#include <cstddef>
#include "protocol_constants.h"   // snr_ewma_update (the ONE SNR smoother) + cap_team_seen / team_seen_retain_ms
#include "recent_ring.h"          // recent_ring_cutoff — the ONE window-boundary definition in this tree

namespace MESHROUTE_NS {

// ★★ THE RECORD LAYOUT IS OWNER-RULED (2026-08-22, ruling R-2(iii)) AND ITS ABI IS PINNED BELOW, NOT
// ASSUMED. The briefed field ORDER `{u32 team_id; u64 last_ms; i16 snr_q4; u8 src_id}` PADS to 24 (the
// u64 forces 8-alignment after a lone u32); leading with the u64 packs the tail into one quantum.
// ⚠ The `PeerLoc` lesson is that a briefed 16 B measured 20 — so the figure below is MEASURED by the
// static_assert on every toolchain that compiles this header, never inferred from the field list.
struct TeamSeen {
    // The arrival stamp of the MOST RECENT beacon carrying this team id (ms, `Hal::now()` domain).
    // ★ MILLISECONDS and 8 bytes, unlike `PeerLoc::t_s` which is seconds and 4 — deliberately, and the
    // reason is that this stamp is a RETENTION DEADLINE INPUT, not only a displayed age: it is compared
    // against `recent_ring_cutoff(now, team_seen_retain_ms)` at every read, so it must live in the same
    // domain and the same width as every other deadline in this tree. `PeerLoc` bought its narrower
    // stamp to hold a 16-slot ring at 20 B/slot; here the u64 is what makes the record 16 B at all.
    uint64_t last_ms;
    // The observed team id (the type-5 TLV value). ⛔ NOT an identity we have verified anything about:
    // a team id is PUBLIC by design (spec §3 P-2) and hearing one proves only that something nearby
    // advertised it. It is a SELECTION AID for a human who must still confirm.
    uint32_t team_id;
    // The α=5/16 EWMA of the reported SNR across this team's beacons, q4 dB — folded through
    // `protocol::snr_ewma_update`, the SAME smoother the home per-mobile and mobile home-RX tables use.
    // ⛔ NEVER "the strongest sample seen in the window": max-seen LATCHES the best moment and never
    // decays, so a team that has walked out of range keeps rendering as strong (owner ruling R-2(ii)).
    // ⓘ The reader turns this into a token through `protocol::presence_quality_tier()` — ⛔ the UI does
    // not get to define a second notion of signal quality (ruling R-4).
    int16_t  snr_q4;
    // The LAST advertiser heard for this team, for diagnostics only. ⛔ C3: this is a foreign team's
    // beacon `src`, i.e. a TEAM-LOCAL id in someone else's id namespace — it must NEVER index a static
    // `node_id`-keyed array, and nothing in this tree does so with it. It is not rendered by N2 either
    // (a row's identity is the team fingerprint), and ⛔ an advertiser's node NAME is never presented as
    // the team's name (ruling F-15 rule 1).
    uint8_t  src_id;
    // ★ NAMED, not implicit — the `PeerLoc::reserved[3]` rule: implicit tail padding is INDETERMINATE
    // after `TeamSeen{}`, so any memcmp-style comparison over the record would be unsound. Naming the
    // byte makes it zero-initialised like every other member at no size cost.
    uint8_t  reserved;
};
// ★ PINNED AT THE DEFINITION, per-TARGET — the PeerLoc/PeerLiveness precedent, and better than a native
// test could be: this fires on every board toolchain, not just native. The two offsetofs are what prove
// the ruled ORDER actually paid: `team_id` at 8 means the u64 really does lead, and `src_id` at 14 means
// the tail (i16 + u8 + u8) fits inside ONE 8-byte quantum with no hole. Widen a field and the build
// fails here instead of silently costing 8 slots' worth of RAM.
static_assert(sizeof(TeamSeen) == 16 && alignof(TeamSeen) == 8 &&
              offsetof(TeamSeen, team_id) == 8 && offsetof(TeamSeen, src_id) == 14,
              "lib/core/team_seen_ring.h: TeamSeen's layout moved — re-measure the ring's RAM cost "
              "(sizeof(TeamSeen) x cap_team_seen) and update node.h's sizeof(Node) ledger; and keep the "
              "tail padding a NAMED member, never implicit");

// Observe one beacon-carried team id: REFRESH IN PLACE if we already hold it, else APPEND, else (full)
// SHIFT THE OLDEST OUT AND APPEND. `n` is the live prefix length and is updated in place.
//
// ⛔⛔ THE ELIGIBILITY GATE IS THE CALLER'S, DELIBERATELY AND IN EXACTLY ONE PLACE: this function
// records whatever it is handed, including `team_id == 0`. The rule ("a MOBILE beacon carrying a
// NON-ZERO team id") is written once, at the single write site in node_beacon.cpp, because half of it
// (`b.is_mobile`) is only knowable there — splitting it would give one decision two authorities, and a
// dropped call-site term would then be silently absorbed here.
// ⓘ OUR OWN team id is recorded like any other, also deliberately: the own-team filter is the READER's
// (N2), so "which teams are audible" and "which of them are worth offering" each live in one place.
template <std::size_t Cap>
void team_seen_ring_observe(TeamSeen (&ring)[Cap], uint8_t& n,
                            uint32_t team_id, int16_t sample_q4, uint8_t src_id, uint64_t now_ms) {
    static_assert(Cap <= 255, "team_seen_ring: the live count is a uint8_t");
    for (uint8_t i = 0; i < n; ++i) {
        if (ring[i].team_id != team_id) continue;
        // ★ REFRESH IN PLACE — the entry does NOT move. That is what keeps the displayed order
        // first-observed and stops the list re-ordering under the operator's cursor mid-walk.
        ring[i].snr_q4  = protocol::snr_ewma_update(ring[i].snr_q4, sample_q4);
        ring[i].last_ms = now_ms;
        ring[i].src_id  = src_id;
        return;
    }
    // A fresh slot SEEDS the accumulator from the first sample (snr_ewma_update's seed-if-zero arm) and
    // steps thereafter — written through the helper rather than as a bare assignment so the seeding rule
    // has one definition too.
    const TeamSeen fresh{ now_ms, team_id, protocol::snr_ewma_update(0, sample_q4), src_id, 0 };
    if (n < static_cast<uint8_t>(Cap)) { ring[n++] = fresh; return; }
    uint8_t o = 0;                                                  // FULL -> the STALEST stamp is the victim
    for (uint8_t i = 1; i < n; ++i) if (ring[i].last_ms < ring[o].last_ms) o = i;
    for (uint8_t i = o; i + 1 < n; ++i) ring[i] = ring[i + 1];      // shift the survivors down (relative order kept)
    ring[n - 1] = fresh;                                            // ...and APPEND the newcomer LAST
}

// How many entries are still inside the retention window RIGHT NOW.
// ★ RETENTION IS APPLIED AT THE READ, never by a sweep and never by a timer (⛔ no timer id is
// allocated by §UI-16 — nothing here is protocol time). A silent radio therefore cannot leave a stale
// entry readable, and the array itself is only ever rewritten by an observation.
template <std::size_t Cap>
uint8_t team_seen_ring_live_count(const TeamSeen (&ring)[Cap], uint8_t n, uint64_t now_ms, uint64_t retain_ms) {
    const uint64_t cutoff = recent_ring_cutoff(now_ms, retain_ms);
    uint8_t live = 0;
    for (uint8_t i = 0; i < n; ++i) if (ring[i].last_ms >= cutoff) ++live;
    return live;
}

// The `idx`-th entry still inside the window, in FIRST-OBSERVED order; nullptr = out of range (the
// normal end-of-list answer, not an error). ⛔ No sorting happens here or anywhere else: the order is
// the array's own, which the observe step maintains structurally.
template <std::size_t Cap>
const TeamSeen* team_seen_ring_live_at(const TeamSeen (&ring)[Cap], uint8_t n, uint8_t idx,
                                       uint64_t now_ms, uint64_t retain_ms) {
    const uint64_t cutoff = recent_ring_cutoff(now_ms, retain_ms);
    uint8_t live = 0;
    for (uint8_t i = 0; i < n; ++i) {
        if (ring[i].last_ms < cutoff) continue;
        if (live == idx) return &ring[i];
        ++live;
    }
    return nullptr;
}

}  // namespace MESHROUTE_NS
