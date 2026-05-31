# HOP_BUDGET enforcement — design spec (MeshRoute C++ port)

**Status:** IMPLEMENTED + verified. **Date:** 2026-05-31.
**Gate reality (vs §8 draft):** the forced-EXHAUSTION direction is NOT a sim gate — an exhausted message
terminally dies (the recovery is "learn the route is longer," not a redelivery), so there's no delivery to
band-compare (same family as the NACK gates). It is covered by the unit tests (forwarder remaining==0 → NACK,
dest exempt, sender rt.hops-bump recovery + the feedback loop). The DELIVERY-NEUTRAL direction IS wired:
`hop_budget_chain_diff.json` — a 3-hop chain where the budget (6) covers the path, each forwarder decrements
(6→5→4), dest exempt → delivered 1/1 on both engines (catches a budget computed too small / a false
exhaustion). **5 HOP_BUDGET unit tests (92/92 native); the chain gate + all existing gates green.**
**Pins:** P-CODEC=inline decode · P-GATE-MODE=both (forced-drop `--band 0` + chain band) · P-FALLBACK=rt_hops 1 ·
P-NACK-SF=routing_sf · P-PREV-FWD=`candidates[0].hops` fallback 0 · P-COMMITTED-CAP=keep `min(15)`.
**Predecessors done:** R1–R3, R3.x, cascade-to-alt, NACK plane (BUSY_RX + LOOP_DUP). **Source of truth:**
`dv_dual_sf.lua` (behaviour, not byte-for-byte). The C6 codec fields (`hops_remaining`/`committed_hops`/
`prev_fwd_rt_hops`) ALREADY exist + round-trip — this milestone wires the ENFORCEMENT around them.
**NO new rand draws.** Delicate firmware (`node_mac.cpp` after the split). The USER commits.

---

## 1. Goal / non-goal

**IN** — the four-part enforcement loop: (a) the sender's route-derived INITIAL budget, (b) the forwarder's
per-hop DECREMENT + drop-at-exhaustion, (c) the forwarder's HOP_BUDGET NACK (in lieu of ACK), (d) the
sender's TERMINAL `rt.hops`-bump recovery on that NACK. Completes the NACK plane (reason 2).

**OUT (deferred to R4)** — anything BUDGET-tier (`nack_reason_budget`=1, duty-cycle tiers); the reason-1
defer-and-restore in `handle_nack` stays exactly as today. Also deferred: `prev_fwd_rt_hops` route-learning
*consumption* by overhearers (the field is STAMPED here, not consumed); no retry/cascade on HOP_BUDGET (the
recovery is terminal by design).

## 2. The three wire fields (recap)
- `hops_remaining` (5-bit, 0–31): the decrementing TTL. 31 = sentinel.
- `committed_hops` (3-bit, 0–7): hops already walked; informational; only consumer is the HOP_BUDGET NACK
  payload. Saturates at 7.
- `prev_fwd_rt_hops` (8-bit): the sender's own `rt[dst].hops` claim, RE-STAMPED by every transmitter (never
  inherited). Used by overhearers to learn routes (NOT this milestone).

## 3. Enforcement — the four sites

### 3.1 Sender initial budget — `do_data_tx` (replaces the hardcode `31/0/0`)
Computed per-flight + threaded on `PendingTx`:
- **Originator** (`!pt.has_previous_hop`): `rt_hops = e->candidates[0].hops` (fallback **1**, Pin P-FALLBACK);
  `remaining = min(31, rt_hops + hop_budget_slack(3))`; `committed = 0`; `prev_fwd_rt_hops = rt_hops`.
- **Forwarder** (`pt.has_previous_hop`): `remaining`/`committed` INHERITED (decremented in `handle_data`,
  threaded via PostAck→TxItem→PendingTx); `prev_fwd_rt_hops` RE-STAMPED to self's `e->candidates[0].hops`
  (fallback 0, Pin P-PREV-FWD).
- rt lookup reuses the `pick_next_cascade_hop` rt-scan idiom. Pure arithmetic, NO rand.

### 3.2 Forwarder decrement + enforce + NACK — `handle_data`
Gate goes AFTER the LOOP_DUP block, BEFORE the ACK (§7.6: NACK in lieu of ACK so upstream's `pending_tx`
doesn't clear early):
- `hb_new_remaining = (int)d.hops_remaining - 1`; `hb_new_committed = min(7, d.committed_hops + 1)`.
- **Exhaustion (EXACT):** `d.dst != _node_id && hb_new_remaining < 0` (arriving `hops_remaining==0` at a
  non-destination). Strict `< 0` on the DECREMENTED value — NOT `<= 0`. **Destination is exempt.**
- On exhaustion: emit `hop_budget_exceeded`; record `_seen_origins`/`_seen_origin_from` (mirror the dedup
  write); pack+tx NACK `reason = nack_reason_hop_budget(2)`, `payload = (hb_new_committed & 0xf) << 4`
  (committed in the HIGH nibble), on `routing_sf` (Pin P-NACK-SF); emit `nack_tx`; `become_free()`; **return**
  — NO ACK, NO `_post_ack`.
- On pass: stash `hb_new_remaining`/`hb_new_committed` into `_post_ack.fwd_remaining/fwd_committed`.

### 3.3 Budget threading — PostAck → TxItem → PendingTx
Today none carry hop-budget fields (THE gap). Add `uint8_t fwd_remaining; uint8_t fwd_committed;` to PostAck,
TxItem, PendingTx. handle_data(pass) → `_post_ack`; `do_post_ack` forward branch → TxItem; `issue_send` →
PendingTx; `do_data_tx` forwarder branch reads PendingTx. Originator: unset → 3.1 computes from rt.

### 3.4 Sender HOP_BUDGET recovery — TERMINAL — `handle_nack` reason-2
Insert a `reason == nack_reason_hop_budget` branch BEFORE the reason-1 defensive restore (reason-1 keeps its
`awaiting_cts=true; start_rts_timeout()` defer):
- Inline-decode `committed = (n.payload >> 4) & 0xf` (Pin P-CODEC).
- `RtEntry* e = rt_find(pt.dst);` if `e && e->n > 0`: `new_hops = min(15, max(e->candidates[0].hops,
  committed + 1))` (Pin P-COMMITTED-CAP — the `min(15)` 4-bit DV clamp is a field invariant); if changed, set
  `candidates[0].hops` + emit `rt_update` trigger=`hop_budget_nack`.
- **Terminal drop** (template = the LOOP_DUP-miss giveup): emit `nack_rx`, `path_cascade_exhausted`
  trigger=`hop_budget`, `rts_giveup`; push `send_failed`; `_pending_tx.reset(); become_free(); return`.
- NO retry, NO cascade (a retry would recompute the same budget; rt.hops must update first).

## 4. The feedback loop
3.4's `min(15, max(old, committed+1))` feeds 3.1's `min(31, rt_hops + slack)` on the NEXT send to that dst.
Two halves of one loop — if either drifts, the lua-vs-meshroute differential trips.

## 5. Determinism
Every site is pure integer arithmetic (`-1`, `+1`, `min`/`max`) + deterministic `std::map` writes + the
existing NACK send path. The only data-plane draw (BUSY_RX wait-jitter N1) is untouched. **NO new draw.**
Lock with a `rand_calls`-delta==0 golden test.

## 6. Minimal change set
1. `node.h` — `fwd_remaining`/`fwd_committed` on TxItem, PendingTx, PostAck.
2. `node_mac.cpp issue_send` — copy the two fields TxItem→PendingTx.
3. `node_mac.cpp do_data_tx` — originator budget from rt OR forwarder budget from pt.
4. `node_mac.cpp handle_data` — decrement+enforce+NACK gate before the ACK; on pass write `_post_ack`.
5. `node_mac.cpp do_post_ack` — copy the two fields into the forward TxItem.
6. `node_mac.cpp handle_nack` — reason-2 terminal branch (leave reason-1 defer).

## 7. Tests (one per behaviour)
Originator budget = `min(31, candidates[0].hops + 3)`, `prev_fwd_rt_hops = rt_hops`; no-candidate fallback →
rt_hops=1 → remaining=4. handle_data: arriving remaining=1 survives (forwarded 0); arriving remaining=0 at a
non-dst → NACK reason=2, payload high-nibble=committed, NO ACK/forward; arriving remaining=0 AT the dst →
delivered, NO NACK. Forward threading carries (remaining,committed). handle_nack reason-2: rt.hops bumped to
`max(old, committed+1)` + terminal giveup, NO retry/cascade. A `rand_calls`-delta==0 determinism golden.

## 8. The gate (Pin P-GATE-MODE: both)
- **Forced-drop (wired, `--band 0`)** `hop_budget_diff_forced.json`: a chain `alice → r1 → r2 → dave` where
  alice's `rt[dave].hops` UNDER-claims the distance so the originator budget is too small → a forwarder hits
  exhaustion → HOP_BUDGET NACK → alice bumps `rt.hops` + terminal-drops. `--expect-drops` + `--min-delivery`
  tuned to the single HOP_BUDGET event. Verified delivery parity lua-vs-meshroute.
- **Chain band gate** (band): the too-small-budget chain where a future/retried send delivers after the bump.

## 8a. Post-review hardening (combined NACK + HOP_BUDGET adversarial review, 2026-05-31)

A combined refute-by-default review (21 candidates → 17 surviving) found the initial implementation had
the `handle_data` gate order INVERTED vs the Lua source of truth and several requeue paths dropping the
budget. Confirmed fixes applied (94/94 native, all sim gates green):

- **[HIGH] handle_data reorder (review #04/#05/#10).** Lua runs HOP_BUDGET (dv:10918-10964) ABOVE the
  loop-dup dedup (dv:10966+) and **records `seen_origins`/`seen_origin_from` on exhaustion** (dv:10933-10940).
  The C++ had dedup-first AND skipped the write (my "we do NOT record" comment was wrong). Restored Lua order
  + added the prune+cap seen_origins write in the exhaustion branch. WHY the write matters: a later
  *non-exhausted* arrival of the same flight via a *different* prev-hop must be caught as LOOP_DUP (not
  accepted+forwarded); without the write it would be accepted. Locked by a new test (exhausted-via-2 then
  fresh-via-3 → LOOP_DUP). No rand impact (both gates are rand-free in handle_data).
- **[HIGH] requeue budget threading (review #00/#01).** The BUSY_RX long-busy requeue (`node_mac.cpp`) and
  `try_cascade_requeue` (`node_cascade.cpp`) rebuilt the TxItem without `fwd_remaining`/`fwd_committed`, so a
  FORWARDED flight reset its budget to 0 → the next hop terminally HOP_BUDGET-killed an in-transit message
  with ample budget. Both now thread the two fields (mirroring `issue_send`/`do_post_ack`). Locked by a new
  test (forward → long-busy NACK → re-issue via alt → DATA still carries the inherited budget).
- **[LOW] queue-full giveup (review #11).** The long-busy requeue silently dropped the flight when
  `_tx_queue` was full; now emits `path_cascade_exhausted`+`rts_giveup`+`send_failed` (matches
  `try_cascade_requeue`).
- **[MED] nack-wait stranding (review #06/#12).** Added `clear_nack_wait()` called at the top of `issue_send`
  — the single choke point that installs a new `_pending_tx`. A stale BUSY_RX wait timer left armed by a
  torn-down prior flight could otherwise spuriously re-RTS a NEW flight on a 4-bit ctr_lo collision. (The Lua
  has the same latent closure-captured-ctr_lo window; the C++ is now strictly harder. No gate triggers it.)
- **[LOW] doc/test gaps:** stale `(15)`→`(31)` cap comment in the Lua (#09); `is_blind` "lazy-prune" comment
  corrected (#14); forwarder `prev_fwd_rt_hops` re-stamp now asserted (#15); originator-budget rand-neutrality
  bracketed (#16); the real emitted HOP_BUDGET NACK bytes' committed-nibble verified end-to-end (#03).

**Gate upgrade (review #02).** The original 3-hop chain over-covered the route (budget 6 vs path 3) so it was
delivery-NEUTRAL but NOT load-bearing on the decrement. Replaced with a **6-hop chain** (budget 9, forwarders
9→4): at six hops a per-hop decrement scaled ≥2× exhausts the last forwarder (9→7→5→3→1→−1 → no delivery →
band trips), as does a budget under-computed by ≥5. It still cannot catch a single-off decrement (the +3
slack + exempt-dst keep a valid route delivering for any per-hop decrement in [0..1]) — that exact value is
asserted by the unit tests. Fundamental limit (documented in the scenario): budget enforcement is purely
SUBTRACTIVE (it only ever kills a delivery at a forwarder; the dst is always exempt), so no delivery-band
gate can be tight on one decrement.

**Deferred (out of this milestone):** (#07) a BUSY_RX `busy_for` ceil-quantize unit assertion (fragile to
predict the pending_rx airtime); (#13) the central `learn_rx_source('nack_frame')` direct-neighbour learning
(rt-seed / SNR-EWMA / id-bind) is absent from the WHOLE C++ port, not just `handle_nack` — track as a single
global port item, not a HOP_BUDGET fix (adding it only to one handler would itself risk mt19937 desync).

## 9. Pins to decide

| # | Decision | Options | Rec |
|---|----------|---------|-----|
| **P-CODEC** | Decode `committed_hops` inline or reshape the codec? | (a) **inline** `(payload>>4)&0xf` in handle_nack · (b) add reason-dispatch + `committed_hops` to `parse_nack`/`nack_out` | **(a)** minimal diff, keeps `nack_out` a raw POD; the codec is golden-tested + untouched. |
| **P-GATE-MODE** | Gate construction? | (a) too-small-budget chain · (b) forced-drop single-event · (c) **both** | **(c)** forced-drop as the `--band 0` wired gate; chain as the band delivery gate. |
| **P-FALLBACK** | Originator budget when no rt candidate? | (a) **rt_hops=1 → remaining=4** (Lua) · (b) 0→3 · (c) sentinel 31 | **(a)** matches Lua `entry.candidates[1].hops or 1`. |
| **P-NACK-SF** | Which SF carries the HOP_BUDGET NACK? | (a) **routing_sf** (the C++ control SF, like the existing NACKs) · (b) a distinct ack_control_sf constant | **(a)** consistent with the LOOP_DUP/BUSY_RX NACK tx. |
| **P-PREV-FWD** | Forwarder `prev_fwd_rt_hops` re-stamp with no rt candidate? | (a) **`candidates[0].hops`, fallback 0** (Lua) · (b) pass through received | **(a)** Lua `entry.candidates[1].hops or 0`. |
| **P-COMMITTED-CAP** | Keep `min(15,..)` on the rt.hops bump (unreachable from one NACK)? | (a) **keep verbatim** (Lua) · (b) drop as dead code | **(a)** a 4-bit DV-field invariant; keep faithful. |
