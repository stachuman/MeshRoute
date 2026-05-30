# R3 — MAC data plane (RTS-CTS-DATA-ACK) — design proposal

**Date:** 2026-05-30  **Status:** IMPLEMENTED + REVIEWED (Q1=host name-resolve, Q4=ANY=3,
Q2=arrived%-only, Q5=lossless-first). **The C++ port now DELIVERS messages.** T1 (t86,
single-hop) + T2 (t87, 2-hop forward) gates PASS; the lua-vs-meshroute dm_delivery
differential PASSES with an EXACT funnel match (tx_enqueue->rts_tx->cts_rx->data_tx->
data_rx->ack_rx->delivered all 1/1, delivered payload identical); 57 doctest cases incl.
2 R3 unit tests. Adversarial review (5 angles, 28->21, ~5 distinct): fixed a CRITICAL
+ HIGH dedup leak/divergence (_seen_origins / _last_acked_from now check the stored TTL +
prune + cap — were permanent dedup + unbounded growth + a 4-bit ctr_lo alias that drops
real messages on sustained traffic), the retry-jitter range (Lua RTS_LEN=8 not the 7-B
C++ wire — determinism), a stale kRetryBackoffTimerId fire (cancel on CTS/ACK), and added
the dup-RTS-while-pending_rx re-CTS. Deferred (noted): exact timeout-pad alignment +
origin-loop-drop -> the lossy R3.x test. You commit.
**Track:** behaviour/R track, after R2 (route-plane hardening). EXTENDS the R2
`meshroute::Node`. Grounded in a 7-angle extract + reconcile of the Lua MAC state
machine (the file-top spec `dv_dual_sf.lua:179-717` + the real handlers).

---

## 0. Goal + headline

**Goal:** a `send <dst> <text>` command delivers ONE message end-to-end via
**RTS→CTS→DATA→ACK** (plus exactly **one forward** at a relay), so
`dm_delivery_breakdown` produces the **first real lua-vs-meshroute delivery number**
— the delivery north star, finally measurable. R3 is deliberately the SMALLEST
end-to-end data plane; the bulk of the Lua MAC is deferred (§4).

**The verification model finally arrives:** R1/R2 were convergence-gated because the
beacon plane emits no `rts_tx`/`data_tx`/`delivered`. R3 emits all of them → the
`dm_delivery_breakdown` funnel + per-pair delivery% becomes the gate (lua reference vs
meshroute).

**Critical correction (the stale-header trap — §6 risk):** the Lua file-top table
(`:9-13`, `:221`) says "CTS/DATA on data_sf". **The CODE is authoritative and says
otherwise:** RTS / CTS / ACK / NACK all ride **routing_sf**; only **DATA** rides the
chosen **data_sf** (a per-frame `TxParams.sf` override). The **sender NEVER retunes its
RX** (stays on routing_sf the whole flight); only the **receiver** retunes —
`set_rx_sf(data_sf)` after sending CTS to hear the DATA, back to `routing_sf` at
`data_rx` / expiry. A porter who trusts the header and retunes the sender breaks all
CTS/ACK reception. (Fix the stale Lua comment in both per [[feedback_port_wire_divergence]].)

---

## 1. The flight (extends R2's on_recv dispatch)

```
on_command("send <dst> <text>"):                          # node.cpp on_command stub
  ctr = next_ctr(dst)                                      # per-(self,dst) 16-bit counter (NOT rand)
  inner = [0x00, _node_id, body...]                        # src_addr_len=0 | origin | body
  _tx_queue.push({origin=self, dst, inner, ctr, flags=0}); emit tx_enqueue{origin,dst,ctr,...}
  become_free()

become_free():                                            # the ONE drain point
  if _pending_tx || _pending_rx: return                   # half-duplex serialize
  pop the ready FIFO head (next_attempt_ms<=now), else arm kQueueWakeupTimerId; else issue_send(item)

issue_send(item):
  next = rt_find(dst).candidates[0].next_hop              # R2 route; no route -> emit send_no_route + drop
  _pending_tx = {origin,dst,next,ctr_lo=ctr&0xF,inner,ctr,retries_left, awaiting_cts=true}
  pack_rts(leaf,self,next,ctr_lo,dst,sf_index,payload_len) ; _hal.tx(sf=routing_sf,label="RTS")
  emit rts_tx ; start_rts_timeout()                       # RX stays routing_sf — NO set_rx_sf

on_recv R (RTS), r.next==self:                            # the receiver
  if last_acked_from[(src,dst,ctr_lo,len)]: pack_cts(already_received=1) ; return  # dedup, no re-deliver
  chosen_sf = select_data_sf(rts sf_bitmap, my allowed_sf_bitmap)   # PURE, no rand
  _pending_rx = {from=src,dst,ctr_lo,chosen_sf,len} ; start_pending_rx_expiry()
  pack_cts(chosen_sf) ; _hal.tx(sf=routing_sf,label="CTS") ; set_rx_sf(chosen_sf)   # NOW retune to hear DATA
  emit rts_rx, cts_tx

on_recv C (CTS), matches _pending_tx.ctr_lo & awaiting_cts & src==next:   # the sender
  cancel(kRtsTimeoutTimerId) ; store chosen_data_sf ; emit cts_rx
  after(cts_to_data_gap_ms=5, kCtsToDataGapTimerId):
     pack_data(hops_remaining=31, visited=∅, inner=_pending_tx.inner) ; _hal.tx(sf=chosen_data_sf,label="DATA")
     emit data_tx ; awaiting_ack=true ; start_ack_timeout()        # sender RX still routing_sf

on_recv D (DATA), d.next==self & matches _pending_rx.ctr_lo:        # the next-hop
  emit data_rx ; cancel(kPendingRxExpiryTimerId) ; set_rx_sf(routing_sf) ; clear _pending_rx
  cache last_acked_from ; seen_origins dedup
  pack_ack(snr_bucket=bucket_of_snr_2b(rx_snr)) ; _hal.tx(sf=routing_sf,label="ACK") ; emit ack_tx
  after(ack_air_ms+1, kPostAckTimerId):
     if dst==self: emit delivered{body}                  # THE headline key (delivery% sourced here)
     else: _tx_queue.push(forward item, previous_hop=src) ; become_free()

on_recv K (ACK), matches _pending_tx.ctr_lo & awaiting_ack & src==next:   # the sender
  cancel(kAckTimeoutTimerId) ; emit ack_rx ; clear _pending_tx ; become_free()

rts_timeout_fire / ack_timeout_fire (captured ctr_lo still active):
  if _pending_rx: reschedule after rts_busy_retry_ms          # busy as receiver (deterministic)
  elif retries_left>0: --retries_left; after(rand_range(0, retry_jitter_ms+1), kRetryBackoffTimerId) -> tx_rts_retry
  else: emit rts_giveup / data_ack_giveup ; clear _pending_tx ; become_free()

pending_rx_expiry_fire: set_rx_sf(routing_sf) ; clear _pending_rx ; emit data_rx_timeout ; become_free()
```

`dm_delivery` keys on **tx_enqueue** (at origin) · **rts_tx** · **cts_rx** · **data_tx**
· **data_rx** (at dst) · **ack_rx** · **delivered** — exact emit_types + field names must
match `dm_delivery_breakdown.py`.

---

## 2. Determinism contract (the #1 R3 risk, sidestepped on the gate)

The `mt19937` is **globally shared** across all nodes' `rand` AND the PHY model (per-frame
fading/FEC/loss draws). Any extra/missing firmware draw shifts every later PHY draw →
divergent collisions → divergent delivery. Therefore:

- **Minimal R3 adds EXACTLY TWO new rand sites:** the retry backoff
  `rand_range(0, retry_jitter_ms+1)` in `rts_timeout_fire` and `ack_timeout_fire`. Both
  fire **only under loss/contention**. The happy-path RTS→CTS→DATA→ACK is **rand-free**
  (`select_data_sf` pure; `cts_to_data_gap`/`ack_air+1` fixed; all `start_*_timeout`
  delays deterministic).
- **The gate stays idle + lossless** (t12 `sigma_db=0`, single-flight) so on the happy
  path **neither engine draws** → lua==meshroute is deterministic-by-construction. The
  lossy/concurrent variant (T3, later) validates the two draws once they fire.
- **LBT must be OFF on the gate.** `lbt_enabled` defaults **TRUE** in the Lua, which draws
  `rand(0, lbt_backoff_ms+1)` the moment the channel is busy — minimal R3 omits LBT
  (direct `_hal.tx`). On an idle single-flight gate `channel_busy_until()` never exceeds
  now so neither side draws; **any** concurrency desyncs. Keep the gate idle/lossless (or
  set `lbt_enabled=false`); never run the differential against a contended scenario in R3.
- **`retry_jitter_ms` is DERIVED** = `3 × airtime_ms(routing_sf, RTS_LEN)` (floor math).
  Port `airtime_ms` **floor-exact** (unit-test vs the Python `lora_airtime_ms`,
  `dm_delivery_breakdown.py:105`) — a 1 ms rounding diff changes the rand RANGE and
  silently desyncs the first time a retry fires.
- **Closure→captured-state:** Lua timers capture `ctr_lo` in a closure and re-check
  `pending_tx.ctr_lo==captured` at fire (staleness guard). `Hal::after` is by-id, no
  closures → **store `captured_ctr_lo` in `_pending_tx`/`_pending_rx`** and re-check in the
  `on_timer` handler. The post-ACK `after` needs a `_post_ack_action {kind=deliver|forward,
  captured fields}` struct.
- **`std::map` (ordered) for every new table** (`_peer_send_counter`, `_last_acked_from`,
  `_seen_origins`) — `mt19937 + std::map` is the #1 documented port risk; never `unordered_map`.
- **2-bit ACK `snr_bucket`** = `bucket_of_snr_2b` (centers −16/−8/+4, `dv_dual_sf.lua:842`)
  — NOT the existing 4-bit `bucket_of_snr_4b`; a byte mismatch desyncs the differential.
- **SF-retune timing** (delicate): the receiver's `set_rx_sf` arms a SF-switch blind window
  (`FirmwareNode.cpp`) — a wrong retune ORDER shifts which frames decode → event order
  diverges → `dm_delivery` mismatch even with identical rng. (t12's `drop_sf_mismatch==0`
  assertion is a useful tripwire.)

---

## 3. Acceptance / gate (two-tier, `dm_delivery`)

A new **`tools/dm_diff.py`** (beside `s3_diff.py`): `make_variant` already swaps each node
between `script=dv_dual_sf.lua` (Lua) and `engine=meshroute`; run `lus` **twice** into two
NDJSON (never mix engines in one sim — the shared `mt19937` interleaves).
`dm_delivery_breakdown` is importable (`load_config`/`analyse`/`summarise` are module-level)
→ call them on each NDJSON and assert **per-(origin,dst) delivery% + outcome (arrived/acked)
match** lua-vs-meshroute.

- **T1 (smoke, NO forward)** = clone `test/t12_dv_single_hop.json` (2 nodes alice→bob,
  dest==next-hop, 40 s, seed 42, **`sigma_db=0`** = lossless, single `send bob hello` @20 s,
  RTS SF7 / DATA SF12) with `engine:meshroute`. PASS = **delivered 1/1 on both engines** +
  identical funnel. Rand-free by construction (§2).
- **T2 (the forward leg)** = a NEW 3-node line authored from `scenarios/r2_route_diff.json`
  (add `allowed_data_sfs`/`data_sf` + one `send carol hello` from alice @~20 s, after routes
  converge, `sigma_db=0`). Exercises bob-forwards / carol-delivers + the relay's 2nd flight.
- **NOT s01** (`s01_dv_dual_sf.json`'s t=32000 second-send is an intentional concurrent-flight
  stress, [[project_s01_concurrent_send]]) — it hits the deferred cascade/blind/NACK/LBT-rand
  machinery and forces early rng divergence. A post-R3 hardening target only.
- **Suite gate:** default `run_tests.sh` green (+ the T1/T2 t-tests); t84/t85 still pass.

---

## 4. Non-goals (deferred, with owner)

| Deferred | Owner |
|---|---|
| **E2E ACK** (per-message opt-in, the return flight + `delivered_confirmed`) — the hop-by-hop K-ACK already gives `dm_delivery` its number | R3.x/R4 |
| **M-broadcast / channel DATA** (no-CTS/no-ACK fire-and-forget + promiscuous merge) | R5 |
| **Gateway cross-layer handoff** (envelope transit, hash-bind, RELAY anti-spam exemption) | R7 |
| **NACK plane** (busy/budget/hop_budget/loop-dup NACK-tx + the `on_recv N` handler) — removes a whole rand site; silent-drop + `rts_timeout` recovers identically on a clean gate | R3.x/R4 |
| **Cascade-to-alt next-hop** (the first follow-up; single-route gates have no 2nd candidate; shares the retry jitter draw so it slots in without desync) | R3.x |
| **Cascade-requeue + adaptive back-pressure** (`effective_rts_max_retries` collapses to the constant when `requeue_count==0`) | R4 |
| **Anti-spam / originator-budget** (a silent-drop policy that only HURTS delivery on a clean gate) | R4 |
| **LBT** (`tx_initiating` rand) — keep the gate idle/lossless so the absent draw doesn't desync | R4 |
| **Blind-window awareness, implicit-ack-from-forward, hop_budget TTL enforcement, visited-set enforcement, peer-liveness/budget tiers, no-route defer+Q, rt-learn-from-DATA** | R4+ |

Minimal R3 stores the 6 visited bytes as zero and `hops_remaining=31` (no enforcement) so
those drop in later with no wire change. `seen_origins` + `last_acked_from` are the only
dedup kept (essential the moment any retry or forward exists).

---

## 5. New timers / state

- **7 timer ids** (after `kTriggeredBeaconTimerId=3`; `Hal` cap 64, ample): `kRtsTimeoutTimerId=4`,
  `kAckTimeoutTimerId=5`, `kPendingRxExpiryTimerId=6`, `kCtsToDataGapTimerId=7`,
  `kQueueWakeupTimerId=8`, `kPostAckTimerId=9`, `kRetryBackoffTimerId=10`. (Single-flight per
  node → each Lua anonymous-closure timer has one live instance → 1:1 id mapping.)
- **`_pending_tx`** (optional): `{origin,dst,next,ctr_lo,ctr,inner+len, chosen_data_sf(0=unset),
  retries_left, awaiting_cts, awaiting_ack}`. Defer alts_tried/visited/hop_budget/etc.
- **`_pending_rx`** (optional): `{from,dst,ctr_lo,chosen_data_sf,len}`.
- **`_tx_queue`** (bounded fixed array, CAP ~8–16): `{origin,dst,inner+len,ctr,flags,
  previous_hop,visited[6],fwd_hop_budget,...}`.
- **`_peer_send_counter`** `std::map<uint8_t,uint16_t>` (next_ctr, wraps 65535→1),
  **`_last_acked_from`** `std::map<key,{sf,t}>` (ttl ~10 s), **`_seen_origins`**
  `std::map<(origin,dst,ctr),expiry>` (ttl 30 s, cap 256). All ordered.
- Wire `_pending_tx||_pending_rx` into `emit_beacon`'s half-duplex skip (the R1/R2 stubs become real).
- **`NodeConfig`** add (config-overridable, like R2's TTLs): `retry_jitter_ms` (the load-bearing
  rand range — or compute floor-exact in `on_init`), `cts_to_data_gap_ms`/`rts_busy_retry_ms`/
  `rts_max_retries` (already in `protocol_constants.h`), `last_acked_ttl_ms`/`seen_origin_ttl_ms`,
  and the per-node `allowed_sf_bitmap`/`data_sf`. Confirm `FirmwareNode::onInit` maps them.

---

## 6. Open questions (recommendations; confirm or override)

1. **dst resolution in `on_command`**: the Lua resolves `send <name>` via `name_to_id`
   (from `sim:nodes()`); the C++ Node has no name map. **(a) numeric id** in the command
   (`send 1 hello`) or **(b) host-side resolve** in SimController. `dm_delivery`'s
   `configured_pairs()` resolves the command dst via the config's `name_to_id`, so scenario
   command text + config names must stay consistent. **rec: (b) host name-resolve** (keeps
   scenarios readable + the tool finds the pair) — small SimController change.
2. **`send_giveup` vs `rts_giveup`/`data_ack_giveup`**: a faithful port emits the latter;
   `dm_delivery` keys its giveup *outcome* on `send_giveup` (only emitted on the deferred
   cascade-requeue path). Consistent across both engines (both emit `rts_giveup`) so
   **delivery-% parity holds**, but the giveup column stays 0. **rec: gate on arrived% only**
   (don't synthesize `send_giveup`).
3. **`select_data_sf` EWMA**: raw `meta.snr` vs the `snr_ewma_in` min. Changes WHICH data_sf
   is chosen (+ DATA airtime + timeout sizing), NOT rand order. **rec: raw first**; wire the
   EWMA only if T1 shows a `chosen_data_sf` mismatch vs Lua.
4. **`sf_index` encoding**: pin **ANY=3** (receiver picks by SNR) vs compute from
   `allowed_data_sfs`. **rec: ANY=3** for the first cut — *if* the receiver's
   `select_data_sf` expands `sf_index=3` → full allowed bitmap (confirm vs the receiver's
   `allowed_sf_bitmap`; the sf_index change once broke cross-leaf, [[feedback_wire_format_multileaf_sweep]]).
5. **Gate loss tiering**: T1 + **T2 both `sigma_db=0` (lossless) first**; a later **T3** adds
   `sigma>0`/concurrency to fire the two retry rand sites. **rec: lossless first**.
6. **`ack_air_ms+1` forward defer**: the Lua defers `become_free` so the ACK and the next-hop
   RTS don't share a sim step (one-TX-at-a-time). **rec: keep it** (confirm the SimRadio step
   model has the same self-TX hazard; if not it's a harmless Lua-sim artifact).
7. **`RxMeta.src_hint` is `int8_t`** but Lua `meta.src` is 0..255 — the `src==pending_tx.next`
   guards + dedup keys read it. **Confirm `FirmwareNode` populates it losslessly for ids >127**
   (else the unexpected-src guard misfires on high node ids) — may need widening to `uint8_t`/`int16_t`.

## 7. Files

`MeshRoute/lib/core/node.{h,cpp}` (the on_recv R/C/D/K dispatch + the state machine + send),
`protocol_constants.h` / `airtime.{h,cpp}` (floor-exact airtime + a unit test vs the Python
ref), `node.h` (timers/state); `lora-universal-simulator/orchestrator/runtime/FirmwareNode.cpp`
(+ config keys; possibly `SimController` for name-resolve + `RxMeta.src_hint` widening);
`test/t12_meshroute_*.json` + the T2 3-node forward scenario; `tools/dm_diff.py` (new);
`MeshRoute/test/test_node_r3.cpp` (unit tests for the flight via the `TestHal`); fix the stale
CTS/DATA-SF comment in `dv_dual_sf.lua` + the C++ doc. Uncommitted — you commit.
