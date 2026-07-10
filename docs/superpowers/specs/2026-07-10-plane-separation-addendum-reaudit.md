<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Protocol Plane Separation — ADDENDUM: complete the local-id-write re-audit + §18-collision test — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-10). Follows `2026-07-10-protocol-plane-separation.md`. An independent gate confirmed the AS-BUILT separation is faithful and s18 byte-identical (native 675), **BUT found a recurring gap CLASS**: the audit guarded every `learn_direct_neighbor` call, yet MISSED the *sibling* id-keyed writes in the same RX handlers — a mobile/team LOCAL id was written into the static `_link_bidi` / anti-spam ledger / `_blind_until` / budget-tier planes (invariant A). **5 such WRITE sites were found and guarded** (see "Already fixed"). This addendum COMPLETES the sweep (the class may recur elsewhere), adds the §18-collision test that would have caught it, and updates the §4 table. User commits; I quality-gate.

**Root lesson:** the leak survived because the audit was done *by one mutator* (`learn_direct_neighbor`). Audit **by plane** instead — every write into a shared id-keyed structure — because a handler that guards the route-learn often has a second write (liveness / anti-spam / blind / tier) one or two lines away.

## Already fixed (do NOT redo — just confirm present)
All in `node_mac_rx.cpp`, each mirroring the adjacent (already-present) learn guard:
- `:450` handle_data DATA anti-spam `track_originator_observation(_pending_rx->from)` → `!_pending_rx->mobile_from`
- `:380` handle_cts `note_link_confirmed(c.tx_id)` → `!(_pending_tx->addr_len==1 || is_team_peer(_pending_tx->next))`
- `:1075` handle_ack budget-tier `mark_neighbor_budget_tier(_pending_tx->next,…)` → guard added to `if (k.budget_hint>healthy)`
- `:1256` handle_nack `_blind_until[pt.next]` write **and** `:1264` `mark_neighbor_budget_tier(pt.next,…)` → both `!(pt.addr_len==1 || is_team_peer(pt.next))`

Gate held after these: native 675/675, s18 `3ac88d40…` exact, s07/s21/s09/s15 0-fail (s21 626→622 = fewer spurious mobile writes, delivery intact).

## Task 1 — systematic re-audit (deliverable = a TABLE)
The regression class: **a received-frame handler (or a flight timeout) writes id-keyed state into a plane SHARED with static global ids, keyed on a `src`/`next`/`from` that can be a mobile/team LOCAL id, with no local-id guard.**

Enumerate EVERY call site of these shared-plane id-keyed mutators; for each, state the id-argument provenance, whether it can be a LOCAL id, and whether it is guarded:
- `learn_direct_neighbor(` → `_rt` (audited — re-confirm all 8).
- `id_bind_set(` → `id_bind` (re-confirm the H_ANSWER / h_relay paths `node_hashlocate.cpp:688/:777` are static-only *by the closed invariant* — a mobile answer is a separate `MOBILE_H_ANSWER` type cached in `mobile_home_cache`, never here).
- `track_originator_observation(` → anti-spam ledger.
- `note_link_confirmed(` → `_link_bidi` + `resort_routes_for_neighbor_penalty`.
- `mark_neighbor_budget_tier(` / `resort_routes_for_neighbor_penalty(` → `_rt` sort / tier (also the `peer_suspect` callers at `node_routing.cpp:576/613`).
- `_blind_until[` writes.
- **peer-liveness mutators keyed by node id**: `mark_dest_seen(`, `clear_peer_suspect(`, `mark_peer_suspect(`, `note_peer_timeout(`, `clear_peer_timeout(`, any `peer_liveness_slot(…, create=true)` write, one_way/degraded writes.
- dedup: `record_seen_origin(` / the `sokey` build — confirm the bit-62 disjoint-range covers EVERY plaintext path (not just handle_data).

**SPECIFIC SUSPECTS — check these first (most likely to still leak):**
1. **Flight-timeout paths.** When a mobile last-mile / team-DM flight TIMES OUT (RTS/ACK timeout → giveup), is `_pending_tx->next` (a LOCAL id) written into the liveness/suspect/one_way plane? Grep the `kRtsTimeoutTimerId` / `kAckTimeoutTimerId` timer cases, `tx_rts_retry`, and any giveup path for a `_pending_tx->next`-keyed `note_peer_timeout` / `mark_peer_suspect` / one_way write. These are NOT in the RX handlers, so the original audit likely never looked here.
2. `mark_dest_seen` / `clear_peer_suspect` fire *inside* `learn_direct_neighbor` (transitively guarded) — confirm there is NO OTHER caller reachable from a received frame with a local id.
3. **The `compute_originator_metric(_pending_rx->from)` READ at `node_mac_rx.cpp:454`** — a lesser *related* item (a READ, not an invariant-A write, but under a §18 collision it reads a colliding static entry → a possible spurious ACK-warn bit). Decide: guard under `!mobile_from` (init `orig_air = 0`) OR document as a throttle-only residual in §8. Do not leave it undecided.

Deliverable: a table `mutator · call-site file:line · id-arg provenance · can-be-LOCAL? · guard (present / MISSING)`. Every MISSING feeds Task 2.

## Task 2 — guard every newly-found leak
For each MISSING guard, add the local-id guard mirroring that handler's sibling learn guard: `!r.mobile_src` (RTS src) · `!_pending_rx->mobile_from` (DATA src) · `!(addr_len==1 || is_team_peer(next))` (our flight's next) · `!q.mobile` (Q src). If a write is DELIBERATELY kept (throttle-only, would need a new wire bit to mark), document it in §8 residuals — never leave it silent.

## Task 3 — the §18-collision native test (spec §9.5 — the test that would have caught all of this)
Add to `test/test_node_r3.cpp` (or `test/test_dual_layer.cpp`): construct the numeric collision and assert no cross-plane pollution.
1. Node N hosts a mobile M with LOCAL id L (e.g. 20) AND has a STATIC neighbour S with the SAME global id L (20).
2. Drive both a mobile exchange (M→N last-mile, or a team DM) and a static exchange (S↔N) that both touch id 20.
3. Assert, keyed on id 20:
   - N's `_rt` route to static-S is unchanged by the mobile traffic (no re-rank/blind from the mobile);
   - `_link_bidi[20]`, the anti-spam ledger for 20, `_blind_until[20]`, and the budget-tier for 20 reflect ONLY the static neighbour (the mobile writes were guarded out);
   - an ACK/NACK addressed to M (`mobile_to=1`) is NOT consumed by static-S, and a static ACK/NACK (`mobile_to=0`) is NOT consumed by M;
   - dedup: a plaintext DM from mobile-20 and one from static-20 with the same `ctr` do NOT alias (bit-62 disjoint).
4. **Falsifiability:** temporarily revert ONE of the 5 guards and confirm the test FAILS (prove it has teeth). Restore.

## Task 4 — update the canonical spec §4 table
Add the 5 now-guarded sites (DATA anti-spam, `note_link_confirmed`, ACK & NACK budget-tier, NACK `_blind_until`) as rows in `2026-07-10-protocol-plane-separation.md` §4, plus any new ones from Task 1. If the `:454` read is accepted, add it to §8; if guarded, add it to §4.

## Gate
native (≥675) green · s18 `3ac88d40e00d2605ff66659f696d52bf` byte-identical · s07/s21/s09/s15 0-fail · 4 boards · the §18-collision test present, passing, and demonstrably failing under a reverted guard.

## Sites (start here)
`node_mac_rx.cpp` (RX handlers + the timeout timer cases) · `node_routing.cpp` (the mutator defs + `peer_suspect` callers) · the peer-liveness plane (grep `mark_peer_suspect`/`note_peer_timeout`/`peer_liveness_slot`) · `node_hashlocate.cpp` (id_bind H-answer paths) · `test/test_node_r3.cpp` (the collision test) · `2026-07-10-protocol-plane-separation.md` §4/§8.
