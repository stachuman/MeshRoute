# NACK plane (minimal slice) — design spec (MeshRoute C++ port)

**Status:** IMPLEMENTED + verified (UNIT-TEST gated). **Date:** 2026-05-31.
**Gate reality (vs §6 draft):** the two NACK sim gates proved INTRACTABLE to trigger deterministically and
were dropped. BUSY_RX is a genuinely narrow window — a forced CTS-drop sends the receiver to `data_sf`
(deaf on `routing_sf`), so it can't decode the contending RTS to NACK it (exactly the F1 limitation the
grounding flagged); the gate passed vacuously (delivery via pending_rx-expiry, NOT the NACK). LOOP_DUP needs
a cascade-fork (b1 exhausts on merge → cascades to b2 → b2 re-forwards), but `merge` re-CTSes b1's retries
(so b1 progresses instead of exhausting) and gets overloaded forwarding downstream — the fork never
materialised cleanly. Both reasons are instead covered by **7 dedicated unit tests** (BUSY_RX emit, silent
pending_tx, short-busy nack_wait + blind + ONE-draw, long-busy requeue + blind-EXCLUSION, LOOP_DUP emit,
LOOP_DUP cascade recovery, LOOP_DUP-miss direct-giveup). All pre-existing gates (r3/r4/r5/s3) + 87/87 native
stay green.
**Pins:** Scope=BUSY_RX + LOOP_DUP · P1=direct giveup inline on LOOP_DUP miss · P2=`is_blind` inside
`next_hop_selectable` · P3=add `PendingRx.expiry_ms` · P4=diamond gate · P5=match Lua on `busy_for==0`.
**Predecessors done:** R1–R3, R3.x, cascade-to-alt. **Source of truth:** `dv_dual_sf.lua`.
**The 'N' codec (cmd 0x5, 4 B) ALREADY EXISTS + is golden-tested** (`frame_codec.cpp:273-298`,
`test_frame_codec.cpp:197-220`) — this milestone does NOT touch the codec. Delicate firmware (`node.cpp`),
edited inline. The USER commits.

---

## 1. Objective

Land the *minimal* NACK plane: the two reasons whose recovery machinery already exists in the port —
**BUSY_RX (reason 0)** and **LOOP_DUP (reason 3)**. Today a busy next-hop SILENTLY DROPS the RTS
(`node.cpp` `_pending_tx||_pending_rx -> return`) and `on_recv 'N'` is a no-op. NACK gives the sender FAST
feedback so it reacts immediately (blind + wait/requeue/cascade) instead of grinding the full rts_timeout.
**Exactly ONE new rand draw** (N1, the nack_wait jitter). Defer BUDGET (→ R4 duty tiers) and HOP_BUDGET
(→ hop-budget milestone). Keep all existing diff gates green.

## 2. Behavioural contract

### 2.1 Per-reason payload decode (the codec returns raw `payload`)
The NACK plane owns the split (Lua does it in `parse_nack`): **BUSY_RX** `busy_for_ms = payload*16`
(encode `payload = ceil(busy_for/16)`, busy_for pre-clamped `[0,65535]`, payload clamp 255); **LOOP_DUP**
`prior_from = payload` (raw). (BUDGET `tier=(payload>>4)&0xF` / HOP_BUDGET `committed=(payload>>4)&0xF` deferred.)

### 2.2 Emission — BUSY_RX (split the busy guard in `handle_rts`)
Split `if (_pending_tx || _pending_rx) return;`:
- `if (_pending_rx)` (holding someone else's reception; this RTS is a DIFFERENT flight) → emit a BUSY_RX
  NACK: `busy_for = pending_rx.set_at_ms + expiry_ms - now` clamp `[0,65535]`, `payload =
  ceil(busy_for/nack_busy_quantum_ms)` clamp 255, `pack_nack(ctr_lo, 0, payload, r.src)`, TX routing_sf,
  `emit nack_tx`, return.
- `if (_pending_tx)` → **STAY SILENT** (`emit rts_drop_pending_tx`, return). Do NOT re-add the removed
  pending_tx-NACK 5th site (the "busy_for is a lie" bug — risk).

### 2.3 Emission — LOOP_DUP (split the live-dup path in `handle_data`)
At the existing seen-origin dup detection: split on prior-hop identity —
- `_seen_origin_from[sokey] != rx_from` (the same flight looped back via a DIFFERENT prev-hop) → emit a
  LOOP_DUP NACK `pack_nack(ctr_lo, 3, prior_from, rx_from)` + `dup_drop`, and **NO ACK** (the NACK fires
  INSTEAD of the dup re-ACK — ordering matters, risk).
- same prev-hop → ACK-only lost-ACK recovery (unchanged).

### 2.4 Sender handler `handle_nack` (add `case` in the on_recv dispatch)
- **Guards** (Lua 10366-10382): `to != _node_id` → return; `!_pending_tx` → return; `ctr_lo !=
  pending_tx.ctr_lo` → return; `src_hint >= 0 && src_hint != pending_tx.next` → `emit nack_drop_unexpected_src`
  + return.
- **Cancel** `kRtsTimeoutTimerId` + `kAckTimeoutTimerId`; clear `awaiting_cts/awaiting_ack`. (Do NOT cancel
  the retry-backoff timer — Lua doesn't.)
- **Per-reason:**
  - **LOOP_DUP(3)** — cascade-to-alt *shape*: `mark_tried(pt, pt.next)` → `pick_next_cascade_hop`; HIT →
    `emit path_cascade` + `tx_loop_alt`, switch hop, `retries_left = effective_rts_max_retries(requeue_count)`,
    `tx_rts_retry()` (NO draw); MISS → **DIRECT giveup** (`path_cascade_exhausted` + `rts_giveup` + reset +
    `become_free`), NOT `try_cascade_requeue` (**Pin P1**).
  - **BUSY_RX(0)** — NEVER path-switch. `emit nack_rx`; if `busy_for_ms > 0`: `_blind_until[pt.next] =
    max(prev, now + busy_for_ms)` + `emit blind_observed`. Then split on `nack_wait_threshold_ms (2000)`:
    - `busy_for_ms <= 2000` → **nack_wait same hop**: capture ctr_lo; `wait = busy_for_ms + 1 +
      rand_range(0, retry_jitter_ms()+1)` [**N1 — the only new draw**, AFTER cancel + blind-mark]; `after(wait,
      kNackWaitTimerId)`; on fire guard `_pending_tx && ctr_lo==captured` → `tx_rts_retry()`.
    - `busy_for_ms > 2000` → **manual requeue same hop**: build a `TxItem` copying `requeue_count` /
      `enqueue_time_ms` **VERBATIM** (NOT via `try_cascade_requeue` — no increment/backoff), `next_attempt_ms=0`,
      push `_tx_queue` tail, `emit tx_requeued`, reset + `become_free`.

### 2.5 Un-stub `is_blind` (the load-bearing READ)
`next_hop_selectable` currently stubs blind. Wire the real read: a candidate whose `next_hop` is in
`_blind_until` and still active (`now < _blind_until[next_hop]`, lazy-prune on read) is NOT selectable —
**Pin P2** (chokepoint vs caller). Without the read the `_blind_until` writes are inert and the
RTS→NACK→requeue→RTS ping-pong is not closed.

## 3. New state (node.h)
`std::map<uint8_t,uint64_t> _blind_until;` (node→absolute_ms) · `std::map<uint32_t,uint8_t>
_seen_origin_from;` (sokey→prev-hop, written/pruned at EVERY `_seen_origins` site) · `PendingRx.expiry_ms`
(**Pin P3**) · `kNackWaitTimerId = 13`.

## 4. New constants
`nack_busy_quantum_ms = 16`, `nack_wait_threshold_ms = 2000`, `NACK_REASON_*` enum (BUSY_RX 0 / BUDGET 1 /
HOP_BUDGET 2 / LOOP_DUP 3).

## 5. Determinism — exactly ONE new draw

**N1** = the nack_wait retry jitter `rand_range(0, retry_jitter_ms()+1)` (Lua 10662), gated on
`busy_for_ms <= 2000`, drawn AFTER the timeout-cancel + blind-mark. Same range/stream as the two existing
retry sites. **Everything else is draw-free:** LOOP_DUP re-RTS (no jitter, like `cascade_to_alt`); long-busy
requeue (manual push, no jitter); the NACK TX emits (jitter-free while LBT is off); busy_for /
blind-max-merge / is_blind are pure arithmetic + sorted-map reads. Two new `std::map`s (sorted) → no
rand-order perturbation. `dm_diff_band --band 0` holds iff no draw is added on any other NACK path.

## 6. The gate (two scenarios, both `dm_diff_band --band 0`)

- **GATE A — BUSY_RX** (`r6_nack_busy_diff.json`): star, bob the contended next-hop. Force bob's
  pending_rx open by dropping the CTS bob→alice (`forced_drops [{from:bob,to:alice,label:CTS,nth:1,count:1}]`)
  so alice never sends DATA → bob holds pending_rx; schedule carol→dave during that window → carol's RTS hits
  `_pending_rx`-busy → BUSY_RX NACK → carol's nack_wait (N1) → carol retries after the window → dave delivers.
  `--expect-drops 1 --min-delivery full` (BOTH carol→dave AND alice→dave deliver N/N — catches a "NACK drops
  the 2nd flight" regression).
- **GATE B — LOOP_DUP** (`r6_nack_loopdup_diff.json`): diamond `alice→{b1,b2}→merge→dave` with a b1↔b2 link.
  `forced_drops [{from:merge,to:b1,label:ACK,nth:1,count:1}]`: b1 forwards alice's single copy to merge (merge
  records `seen_origin_from=b1`, ACKs), the merge→b1 ACK is dropped, b1's ack_timeout cascades the SAME flight
  to b2→merge, merge sees the same `(origin,dst,ctr)` from prev-hop b2 ≠ b1 → LOOP_DUP NACK → b2 marks-tried +
  cascades/gives up; dave already has the first copy (**Pin P4** = this fork is the only construction that
  exercises the discriminator under one-copy). `--expect-drops 1 --min-delivery one`.

Both gates: cross-engine delta=0, delivery floor met, expected drops fired; all pre-existing gates green.

## 7. Out of scope (DEFERRED)
BUDGET emit+reaction + `mark_neighbor_budget_tier` + tier penalty in `route_strictly_better` → R4 duty tiers.
HOP_BUDGET emit+reaction (forwarder decrement + drop-at-0 + RX `rt.hops` bump) → hop-budget milestone.
CTS-overhear blind population (passive F1) → defer. The LBT busy-backoff draw on the NACK TX → R4.

## 8. Risks
1. **DETERMINISM:** exactly ONE draw (N1), ordered + gated as in Lua. Any extra draw fails `--band 0`.
2. **Silent-drop regression:** the `_pending_tx` leg MUST stay silent (no NACK) — re-adding the 5th site
   reintroduces the "busy_for is a lie" 28s-airtime bug.
3. **Double-response on the dup path:** LOOP_DUP NACK fires INSTEAD of the dup re-ACK, not in addition.
4. **Inert blind writes:** un-stub the `is_blind` READ or the writes are dead + the ping-pong stays open
   (`--min-delivery full` catches the stall).
5. **`_seen_origin_from` parity:** written + pruned at EVERY `_seen_origins` site, same key.
6. **Requeue-count corruption** on long-busy: copy `requeue_count/enqueue_time_ms` VERBATIM, bypass
   `try_cascade_requeue`.
7. **Stale Lua header docs:** NACK rides routing_sf + is 4-byte (the header's "data_sf"/"3-byte" is stale) —
   port from the LIVE code lines.
8. **Ceil-quantize edge:** `ceil(busy_for/16)`, pre-clamp `[0,65535]` BEFORE quantize, then payload clamp 255.

## 9. Pins to decide

| # | Decision | Options | Recommendation |
|---|----------|---------|----------------|
| **Scope** | Which reasons this milestone? | (a) **BUSY_RX + LOOP_DUP** · (b) BUSY_RX only (defer LOOP_DUP too) | **(a)** both reuse existing machinery (cascade/seen_origins/blind); LOOP_DUP adds only `_seen_origin_from` + the diamond gate. |
| **P1** | LOOP_DUP with no untried candidate | (a) **DIRECT giveup inline** · (b) `try_cascade_requeue` | **(a)** Lua does a direct giveup on LOOP_DUP miss (no requeue) — (b) would wrongly requeue a looped flight. |
| **P2** | Where the `is_blind` read goes | (a) **inside `next_hop_selectable`** (chokepoint) · (b) at caller sites `&& !is_blind` (Lua's literal structure) | **(a)** the port already routes all candidate scans through `next_hop_selectable` + documents it as the plug-in point; single chokepoint, behaviourally equivalent here. |
| **P3** | pending_rx expiry source | (a) **add `PendingRx.expiry_ms`** (set at `start_pending_rx_expiry`) · (b) recompute at the NACK site | **(a)** one field, matches the Lua; recompute risks formula drift skewing the busy window. |
| **P5** | BUSY_RX when `busy_for_ms == 0` | (a) **match Lua: guard blind on >0, STILL nack_wait** · (b) skip both, fall to requeue | **(a)** Lua guards the blind write on >0 but still routes 0≤2000 into nack_wait (`wait = 0+1+jitter`) — (b) changes the blind map + the N1 ordering. |
