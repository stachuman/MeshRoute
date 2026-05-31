# Cascade-to-alt walk — design spec (MeshRoute C++ port)

**Status:** DONE — implemented, reviewed, verified. **Date:** 2026-05-31.
**Gate adjustment (vs §5 draft):** wired on `dm_diff_band` delivery ONLY, NOT `s3_diff`. The CTS-drop
forcing fires heavy same-hop retry activity, which shifts the discovery-beacon interleaving (a wire-airtime
artifact — the route SETS still converge identically), so `s3_diff`'s `beacon_tx` count parity false-DIFFs.
And `s3_diff`'s route-SET check wouldn't verify the alt-PICK anyway (alice holds all routes regardless). The
alt-pick correctness is covered by the unit tests (`path_cascade next == expected alt`); delivery parity by
`dm_diff_band`. **Forcing = CTS-drop, not RTS-drop:** the Lua labels same-hop RTS retries `RTS-rty` (≠ the
meshroute `RTS`), so an `RTS`-drop kills only the first attempt on the Lua → it would deliver via the primary,
no cascade. The CTS is labelled `CTS` on both engines (incl. re-CTS), so a CTS-drop forces the cascade
identically. **Gate result:** `r5_cascade_alt_diff.json` (3 sends, drop CTS relay_p+relay_a) — delivery
3/3 == 3/3 (0pp), `drop_forced=6`, `cts_rx/data_tx/data_rx/ack_rx` 6/6 exact; `rts_tx` reported (lua 6 /
meshroute 30 — the retry-label + wire divergence).
**Pins (decided):** P1=include `try_cascade_requeue` · P2=`forced_drops` gate · **P3=3 candidates (full K=3 walk —
gate forces TWO cascade steps: relay_p→relay_a→relay_b, deliver via relay_b)** · P4=full two-pass
`pick_next_cascade_hop` · P5=include the no-route defer+Q substrate (Lua defaults: cap 32, 1 s drain, TTL-FIRST).
**Predecessors:** R3 (data plane), R3.x (lossy gate + the rand-free `forced_drops` hook this milestone reuses).
**Working artifact** — not committed. The USER commits the implementation. Delicate firmware (`node.cpp`) — edited inline.

---

## 1. Scope / goal

Port the **in-flight cascade-to-alternate-next-hop walk** from `dv_dual_sf.lua`. The port has a bounded
K-candidate DV route table (`max_rt_candidates=3`, sorted) but two **drop-instead-of-walk holes**:

- **`issue_send` no-route → DROP** (`node.cpp:604-610`): drops unconditionally. The Lua **defers** for an
  *originator* (`dv:7049-7052`); only *forwarders* keep the drop (`dv:7041-7048`).
- **`rts_timeout_fire`/`ack_timeout_fire` retry the SAME next-hop** then give up (`node.cpp:873-905`):
  they `_pending_tx.reset()` + `become_free()`, never reading `e->candidates[1..n-1]`. The Lua walks to an
  alternate via `pick_next_cascade_hop` before giving up (`dv:6456-6516` RTS / `dv:6565-6620` ACK).

**Goal:** a flight whose primary next-hop fails must walk to the next-best candidate; on full exhaustion,
defer-and-Q (originator) rather than drop — matching the Lua **draw-for-draw on the mt19937 stream**, gated
by `dm_diff_band` delivery + `s3_diff` route-set parity.

---

## 2. Mechanisms IN scope

1. **`pick_next_cascade_hop(PendingTx&)`** — the two-pass sorted-candidate walk (`dv:5430-5450`): pass 1
   gradient-respecting (`allow_uphill=false`), pass 2 uphill fallback (`allow_uphill=true`). Returns the
   first selectable, non-tried candidate `next_hop`, else 0.
2. **Per-flight `alts_tried` set** on `PendingTx` (`dv:404`, mark-tried at `dv:6461`) — a linear
   `uint8_t[max_rt_candidates]` (membership-only, never iterated for a decision → determinism-safe; NOT
   `std::set`). Empty at flight start, discarded on ACK/reset (`node.cpp:847` already drops `_pending_tx`).
3. **Minimal `next_hop_selectable`** — only the determinism-load-bearing filters this milestone: skip
   `c.next_hop == previous_hop` (`dv:3992`), skip `alts_tried` membership (`dv:4006`), and the gradient
   two-pass guard shape (`dv:4030-4042`). **Stub** `is_blind→false`, `get_peer_suspect_level→0`,
   `is_next_hop_fresh→true`, `route_uses_mobile_as_transit→false`, `effective_score==score` (all empty-table
   no-ops, consistent with `node.h:57-58`).
4. **`cascade_to_alt(const char* trigger)`** — shared helper wired into BOTH `rts_timeout_fire` and
   `ack_timeout_fire` giveup (`retries_left==0`) branches (the Lua duplicates the body). On a successful
   alt switch: mark current `next` tried, set `pt.next = alt`, reset `retries_left`, and re-`tx_rts_retry()`
   **with NO jitter draw** (Lua `dv:6478` calls it directly). On exhaustion → `try_cascade_requeue`.
5. **`try_cascade_requeue(PendingTx&)`** — exhaustion → tail-append a `TxItem` to `_tx_queue` with **pure**
   exponential backoff `base*2^(requeue_count-1)` capped (`dv:6209-6213`, NO rand); give up (true drop +
   `rts_giveup`/`path_cascade_exhausted`) when `requeue_count > cascade_requeue_max` or
   `now - enqueue_time_ms >= cascade_requeue_total_max_ms`. `load_threshold = 0` (`dv:1020`).
6. **`issue_send` no-route split** — forwarder keeps `send_no_route` drop; originator calls `defer_send`.
7. **`defer_send` + `DeferredSend` queue** (`_deferred[cap_deferred_sends]`, insertion-order array) +
   **`try_drain_deferred`** with **TTL-FIRST ordering** (the `defer_ttl_route_exists_trap` fix `dv:6775-6782`),
   route-exists drain to `_tx_queue` head; a 1 s periodic drain timer + drain-on-`rt_changed` in
   `ingest_beacon`. **`q_response_settle_ms = 0`** so the drain re-queue draws no rand.
8. **Thread `requeue_count` + `enqueue_time_ms`** through `TxItem ↔ PendingTx` (the Lua `queue_meta`):
   `enqueue_time_ms` is the ORIGINAL first-enqueue time, preserved across every requeue.
9. **Comparator** — verify `route_strictly_better` matches `dv:4227-4245` (viable-first, hops-asc,
   score-desc; non-viable score-desc, hops-asc) AND add a deterministic **ascending-`next_hop` final
   tie-break** (Lua `table.sort` is unstable → ties are the named differential risk).
10. **Emit parity** — keep `send_no_route` (forwarder-only), `rts_giveup`, `data_ack_giveup`; add
    `path_cascade`, `path_cascade_exhausted`, `cascade_requeue`, `send_deferred` (each carrying origin+dst).

---

## 3. node.{h,cpp} structures + functions

- **`PendingTx`** (node.h:86-96) — add `uint8_t alts_tried[protocol::max_rt_candidates]; uint8_t
  alts_tried_n=0; uint8_t previous_hop=0; bool has_previous_hop=false; uint8_t requeue_count=0; uint64_t
  enqueue_time_ms=0;`. Private `bool alt_tried(uint8_t)` / `void mark_tried(uint8_t)`.
- **`TxItem`** (node.h:77-85) — add `uint8_t requeue_count=0; uint64_t enqueue_time_ms=0;`. Stamp
  `enqueue_time_ms=_hal.now()` at first enqueue in `do_send`.
- **`RtEntry`/`RtCandidate`** — NO new fields; `candidates[]` already sorted. Add the tie-break in the
  comparator.
- **`issue_send`** (node.cpp:604-620) — split the drop (forwarder vs originator-defer); copy
  `previous_hop/requeue_count/enqueue_time_ms` into `pt`; `alts_tried_n=0`.
- **`pick_next_cascade_hop`** (NEW) — `rt_find` + two-pass walk over `candidates[0..n-1]`.
- **`rts_timeout_fire`** (node.cpp:873-888) — replace the `retries_left==0` block with
  `cascade_to_alt("rts_giveup")`; the `retries_left>0` same-hop jittered branch (RNG #1) UNCHANGED.
- **`ack_timeout_fire`** (node.cpp:889-905) — mirror with `cascade_to_alt("ack_giveup")`; reset
  `awaiting_ack/awaiting_cts/chosen_data_sf` before re-`tx_rts_retry` (like `node.cpp:894`).
- **`try_cascade_requeue`** (NEW) + `kCascadeRequeueTimerId` (id 12).
- **`DeferredSend` + `_deferred[]` + `defer_send` + `try_drain_deferred`** (NEW) + `kDeferredDrainTimerId`
  (id 11); drain iterates `_deferred[]` in insertion order; TTL-FIRST.
- **`become_free`** — reuse as-is.

---

## 4. Determinism argument

**The minimal walk introduces ZERO new rand draws.** The two existing draws (`node.cpp:878/895`, the
same-hop retry jitter) keep firing only on the `retries_left>0` path. The alt-switch re-`tx_rts_retry()`s
with **no jitter** (matching `dv:6478` — adding a draw here is THE differential trap, the same failure mode
as the reverted `sf_index` change). `try_cascade_requeue` backoff is pure; the defer drain uses
`q_response_settle_ms=0` (suppressing the Lua settle-jitter `dv:6851`). So the mt19937 stream stays aligned.
The only non-rand determinism surfaces: the candidate sort comparator + the ascending-`next_hop` tie-break
(both pure), `alts_tried` (linear membership array), `_deferred[]` drain (insertion-order array), neighbor
lookups (`std::map` point-lookup, never iterated for a decision).

---

## 5. The gate

`r5_cascade_alt_diff.json` (engine-neutral, wired into `run_tests.sh`). **Topology:** `alice → {relay_p,
relay_a} → dave`, both relays 1-hop bidir, well-separated SNRs (relay_p 12 dB = `candidates[0]`, relay_a
8 dB = `candidates[1]`) so candidate order is unambiguous on both engines. Single layer, quiet channel,
seed 42. **Forcing:** `forced_drops [{from:alice,to:relay_p,label:RTS,nth:1,count:99}]` drops EVERY
alice→relay_p RTS (rand-free) → alice exhausts `rts_max_retries=3` same-hop retries (RNG #1), `cascade_to_alt`
fires (`path_cascade`), marks relay_p tried, picks relay_a, DATA delivered to dave **only via the alternate**
(relay_p stays alive → the alt-walk, not route-aging, is the exclusive path). **Verify:**
`dm_diff_band.py r5_cascade_alt_diff.json --band 0 --funnel report --expect-drops 1 --min-delivery full`
(delivery N/N on both; the drop must fire; funnel reported) + `s3_diff.py` (route-SET parity — catches a
comparator drift that picks a different alt). Existing gates (dm_diff:r3, dm_diff_band:r4_forced, s3_diff,
the native suite) MUST stay green — the cascade code only adds branches on the `retries_left==0` path.

---

## 6. Non-goals / DEFERRED

- Budget-tier plane (`mark_neighbor_budget_tier`, `budget_penalty_db`, `effective_score` penalties) → R4.
- NACK-driven cascade (`record_peer_rts_timeout`, suspect≥2 early-cascade `dv:6395-6454`) → NACK milestone.
- `classify_blind`/`blind_until`/CTS-overhear/`tx_blind_*` (`is_blind→false` stub) → NACK milestone.
- `try_cascade_requeue` adaptive load-shedding (beyond `load_threshold=0`) → R4 back-pressure.
- `gateway_doorstep_hold`/`gateway_schedule_defer_ms` → R7 cross-layer.
- Drain settle-jitter rand (`dv:6851`) + expanding-ring RREQ flood (`emit_route_request` `dv:5696`) →
  the Q-frame/reactive-discovery plane (later R). This milestone defers-and-waits-for-a-beacon-route only.
- `silent-next` `defer_send_for_route('all_candidates_silent')` → needs the suspect plane (NACK milestone).

---

## 6a. Post-review hardening (adversarial review: 26 candidates → 21 verified → 13 distinct fixed)

- **Tie-break REMOVED (HIGH).** My ascending-`next_hop` tie-break was WRONG — the Lua has no tie-break
  (preserves insertion order on a tie), so id-ordering actively *diverged* from the reference and leaked into
  `rt_merge`'s full-table eviction. Reverted to `return false` (insertion order, Lua-faithful); test inverted.
- **`effective_rts_max_retries(requeue_count)` (HIGH, determinism).** A requeued flight gets `max(0, max −
  requeue_count)` same-hop retries (dv:3119), not a flat `rts_max_retries` — else extra retry-jitter draws
  de-align the streams. Used at both `retries_left` init sites.
- **Forward-path `enqueue_time_ms` stamped (HIGH).** `do_post_ack`'s forward `TxItem` left it 0 → the
  cascade-requeue total-age cap (`now − 0`) tripped immediately → forwarded flights got ZERO requeues.
  Now stamped `_hal.now()` (dv:11391, "fresh hop attempt").
- **`next_attempt_ms` dequeue gate (HIGH).** The backoff was bypassable by a concurrent `become_free`.
  `TxItem.next_attempt_ms` now gates the dequeue; `become_free` SCANS for the first ready item (Lua-faithful —
  a fresh send isn't blocked behind a backing-off requeue; `==0` is always ready so plain queues are FIFO).
- **Defer plane matched to the Lua:** drain re-queues to the **HEAD** (oldest-first, dv:6843); a full queue
  **REFUSES the new** send (not drop-oldest) with `send_deferred_refused` + a `send_failed` push (no silent loss).
- **`path_cascade` carries origin/from_next** (analyzer parity). **Gate `--expect-drops 6`** (both relays ×3
  sends → proves the full two-step walk, catches a label-drift directive). New unit tests: ACK-cascade full
  path, TTL-FIRST-beats-route-exists, total-age-cap giveup, and a `rand_calls` counter asserting ZERO extra
  draws on the cascade switch (the #1 determinism risk). **80/80 native, all 6 gates green.**
- **Deferred (noted):** the load-adaptive requeue gate (`cascade_requeue_load_threshold`) — a no-op at
  queue-depth 0 (the only case the gate/tests hit); matters only under concurrent queue load → R4 back-pressure.

## 7. Risks

1. **DETERMINISM #1 (named differential):** an extra rand draw on the alt-switch path → mt19937 de-align.
   `cascade_to_alt` must NOT draw rand.
2. **DETERMINISM #2: unstable-sort ties.** Without the ascending-`next_hop` tie-break, lua-vs-meshroute can
   pick a different alt → `s3_diff` DIFF. The gate uses well-separated SNRs so it could pass with a missing
   tie-break — add a **tie-break unit test** on equal-score candidates (don't rely on the gate).
3. **Defer-TTL route-exists trap:** `try_drain_deferred` MUST check TTL BEFORE route-exists (`dv:6775-6782`)
   — getting it wrong plants the s12 477-defer infinite-loop bug. TTL-FIRST from the start.
4. **`queue_meta` threading:** `requeue_count`/`enqueue_time_ms` must survive `TxItem→PendingTx→requeued
   TxItem` unbroken, or the caps compute against a fresh `now` and giveup never fires.
5. **ACK-path reset omission:** on an ack-loss alt-switch, reset `awaiting_ack/cts/chosen_data_sf` before
   re-`tx_rts_retry`, or the alt flight starts mid-handshake. The RTS gate doesn't exercise this → add an
   ACK-cascade unit test.
6. **Vacuous-pass:** `--min-delivery full` + `--expect-drops 1` are LOAD-BEARING (mirror r4_forced).
7. **Forced-drop `count` too small:** must exceed `rts_max_retries=3` (use 99) or the primary recovers and
   the cascade never fires — the gate would pass via the primary, masking a broken walk.
8. **Scope creep into suspect/blind:** the silent-next + suspect≥2 branches LOOK like the same walk but
   need empty-this-R peer-state tables — keep stubbed; cascade ONLY on `retries_left<=0`.

---

## 8. Pins to decide

| # | Decision | Options | Recommendation |
|---|----------|---------|----------------|
| **P1** | Include `try_cascade_requeue` (exhaustion→backoff requeue) this milestone? | (a) **Include** (pure, load_threshold=0) · (b) defer to R4; on exhaustion just defer_send/drop | **(a)** rand-free, constants already ported; makes the exhaustion landing faithful. |
| **P2** | How to force the primary-hop failure in the gate? | (a) **`forced_drops` all primary RTS (count=99)** · (b) `dies_at_ms` on relay_p | **(a)** rand-free + engine-neutral; relay_p stays a live candidate so the **alt-walk** is the exclusive path (not aging). |
| **P3** | Gate topology candidate count? | (a) **2 (primary + 1 alt), wired** + a 3-cand REPORTED diagnostic · (b) 3 (full K=3) wired | **(a)** minimal, one cascade step, clean band-0 delivery; well-separated SNRs so the tie-break isn't the decider. |
| **P4** | `pick_next_cascade_hop` shape? | (a) **Full two-pass** (matches Lua) · (b) single pass (smallest, gradient guard unset) | **(a)** cheap + rand-free; the NACK/budget plane plugs into the two-pass shape later — avoids a re-shape. |
| **P5** | Defer-queue capacity / drain cadence? | (a) **Lua defaults (cap 32, 1 s drain + drain-on-rt_changed)** · (b) trim (drain-on-rt_changed only) | **(a)** constants already ported; the 1 s periodic drain is the TTL-giveup fallback — trimming risks the defer-TTL trap. |
