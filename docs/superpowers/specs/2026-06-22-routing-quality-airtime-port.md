<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Routing-quality + airtime parity — 4 non-liveness Lua mechanisms

**Status:** coder instruction (gaps audit-verified 2026-06-22 by 4 parallel source-diff agents — verbatim Lua + the exact C++ decision point for each). The user does ALL commits — land each phase GREEN + uncommitted, report ready, I gate. **No fallbacks, no silent defaults:** every value below is either Lua-determined or a calibrated/flagged divergence (only ④'s threshold diverges, and it is gate-calibrated, not assumed).

## Why

The peer-liveness + freshness plane is now fully ported (`2026-06-17-routing-liveness-plane-port.md`, Phases 0–4). A follow-up source-diff of `scenarios/dv_dual_sf.lua` against the C++ core surfaced **4 routing-quality / airtime mechanisms the Lua has and the C++ lacks**. None is a correctness bug; each is a delivery-stability or airtime-frugality behavior in keeping with the honest-node / anti-flood ethos — the kind of thing that "closes" the port. All 4 are independently verified below.

**No wire change in any phase.** Every mechanism consumes fields already on the air (the `is_mobile` beacon bit, CTS `tx_id`/`chosen_data_sf`/`payload_len`, RTS `src`/`dst`/`ctr_lo`/`payload_len`) or purely local TX-queue state. So `wire_version` is untouched (consistent with the pre-deployment "reflash all together, no bump" policy), and there is **no byte-identical phase** — every phase changes routing behavior, so the gate is **result-comparison** throughout (see Gate discipline).

| # | mechanism | Lua | C++ today | verdict |
|---|---|---|---|---|
| **③** | blind_until from overheard CTS | SET `dv:10281-10302`; consult `classify_blind@3190`, `is_blind@3171` | `_blind_until` map (node.h:1081) + **consult side fully built**; SET feeders busy-NACK/BUDGET-NACK built; **overheard-CTS feeder MISSING** (`node_mac_rx.cpp:281-285` arms NAV only, returns) | smallest — near-bugfix |
| **①** | mobile-as-transit avoidance | `is_mobile_peer`/`route_uses_mobile_as_transit` `dv:1325-1334`; hard-exclude `@4099`/`@4583`/`@4201` | peer bit DECODED (`frame_codec.cpp:92`) but **never learned** — no `mobile_peers`, no `RtCandidate` field, no guard (`node_cascade.cpp:24` "mobile-transit are stubbed") | real, broader than reported |
| **②** | implicit-ACK from overheard forward-RTS | `dv:9863-9893` (matches `src==next ∧ dst ∧ ctr_lo ∧ payload_len`) | `handle_rts` NAV-arms + early-returns at `node_mac_rx.cpp:108` for the overheard RTS, **never consults `_pending_tx`** | real, multi-hop airtime |
| **④** | load-adaptive cascade back-pressure | `cascade_load_skip` `dv:6275-6303` (`effective_max = max(0, cascade_requeue_max − max(0, depth − threshold))`) | constant `cascade_requeue_load_threshold = 0` ported (`protocol_constants.h:165`) but **never read**; fixed budget + binary `_tx_queue_n ≥ kTxQueueCap` overflow only | real, riskiest |

> **Sharpenings the verification produced (vs the original finding wording):**
> - **①** is *broader* than "learns is_mobile but doesn't avoid" — C++ does **not even learn** the peer bit. Both halves (learn + avoid) are absent.
> - **②**'s trigger is the next-hop's **forward RTS**, not "a DATA frame onward." Integration is `handle_rts`, not `handle_data`.
> - **③** is **not** subsumed by the existing NAV. NAV is one global scalar ("*I* go silent entirely"); `blind_until` is per-neighbor ("*that node* is deaf — I may still RTS someone else"). The C++ map + consult side already exist; only the overheard-CTS feeder is missing.
> - **④** at the shipped default `threshold=0` is **not dormant — it is maximally aggressive** (`effective_max = 3 − depth`, all requeues dropped at depth ≥3). A faithful port at 0 is an immediately-active, possibly-regressive change; the C++ author may have left it unwired on purpose (cf. the liveness-Phase-3 "too churny, softened" precedent). → ported with a **tuned, gate-calibrated** threshold (decision below).

## Gate discipline (result-comparison — same as liveness Phases 2–4)

No phase here is byte-identical (all change routing). Gate the **delivery outcome** across the baseline suite, never a byte stream. Reference = `simulation/BASELINE.md` (re-captured 2026-06-21). **Per behavior-changing phase, run the suite and require no regression:**

| scenario | role | gate |
|---|---|---|
| `s18_meshroute` | single-layer dense — anchor (single-hop) | same-layer **≥ 98/113** |
| `s19_singlelayer_multihop_chain` | single-layer **MULTI-HOP** (reroute home) | **8/8** + `mean_hops` A↔B 3.0 / A↔R 2.0 |
| `s09` / `s10` | 2-layer cross-layer | same **3/3** · cross **2/2** |
| `s16_dense_gateway` | dense 2-layer **overload stress** | cross **≥ 57/80** |
| `s15_three_layer` | 3-layer cross-layer + channels | same **47/47** · cross **multi-seed mean ≥ ~90%** (seeds {1522,1,42,100,7,2024,999}; 1522 is the worst case — never gate on it alone) · channels ≥ 218/224 |
| `s17_metro` | **252-node scale** + channels | same **≥ 26/30** · channels `leaks 0` (cross-layer inert — do not gate) |

Run per scenario:
```
lus -e meshroute simulation/<s>.json /tmp/<s>.ndjson
python3 tools/dm_delivery_breakdown.py simulation/<s>.json /tmp/<s>.ndjson --failures
```
Plus the invariants every phase:
- **`leaks == 0`** everywhere (s15/s17 carry channels) — hard.
- **Failure taxonomy no worse** — no NEW failure class. Each phase has a *target* bucket that should trend **down** (named per phase).
- **Event-count within ~±3 %** of each scenario's baseline + **`mean_hops` stable** — the airtime/churn proxy that delivery-% can't see. (For ④ this guard is *relaxed and inverted* — see Phase 4.)
- **`pio test -e native`** all pass (`.pio/build/native/program | tail -3` for the true count — PlatformIO mis-reports).
- **3 boards build**: `pio run -e gateway -e xiao_sx1262 -e heltec_v3`.
- **A new focused sim scenario per phase** (in the sim repo `~/lora-universal-simulator/test/`, next free `t1xx`), asserting the mechanism's *intended* behavior in isolation — mirrors the `t96/t97/t98` liveness gates.
- **Leave GREEN + uncommitted — the user commits.**

> **Note on lossless scenarios (②/③ benefit visibility):** s19 has lossless links (seed 42), so an *explicit* ACK/CTS is never lost there — the implicit-ACK (②) and the deaf-window avoidance (③) only *bite* under contention/loss (dense s18/s16, or the purpose-built lossy `t1xx`). Gate the **benefit** in the dense scenarios + the focused scenario; gate **no-regression** across the whole suite.

## Constants

- **③** reuses `cts_to_data_gap_ms = 5` (`protocol_constants.h:81`) + the DATA-airtime term from `nav_duration_cts` (`node_mac.cpp:597`). **No new constant.**
- **①** adds a 256-bit peer-mobile set (storage, not a tunable). **No new constant.**
- **②** **No new constant.**
- **④** retune the **existing** `cascade_requeue_load_threshold` from `0` to the **calibrated** value (Phase 4 sweep). **No new constant** — the dead one becomes live.

---

## Phase 1 — ③ blind_until-from-CTS feeder  (smallest; near-bugfix; do first)

The per-neighbor deaf map + the entire consult side already exist and work; this phase wires the **one missing SET feeder** so the map is populated proactively (from an overheard CTS) instead of only reactively (after a wasted RTS→NACK).

1. **SET feeder.** In `handle_cts`, the overheard branch `if (c.rx_id != _node_id)` (`node_mac_rx.cpp:281-285`) currently arms NAV and returns. **Alongside** (not instead of) the existing `nav_arm`, add a max-merge write:
   ```
   const uint64_t window = protocol::cts_to_data_gap_ms
                         + data_air(c.chosen_data_sf, max-payload DATA);   // the CTS sender's deaf interval
   const uint64_t end = _hal.now() + window;
   auto& slot = _active->_blind_until[c.tx_id];
   if (end > slot) slot = end;                                            // max-merge (Lua dv:10293-10295)
   ```
   - `c.tx_id` is the CTS *sender* (the node that will be deaf) — the key the consult side already looks up. `chosen_data_sf` + `payload_len` are on `cts_out` (`frame_codec.h:163`).
   - `window` = the deaf interval = `cts_to_data_gap_ms + airtime(chosen_data_sf, DATA_HDR_LEN + max_payload + DATA_INNER_OVERHEAD)` (Lua `dv:10288-10291`). This is a **subset** of `nav_duration_cts` (which also covers ACK + a 2nd gap) — the sender is deaf only while *receiving the DATA*, not while sending its own ACK. Extract the `data_air` term from the same airtime helper `nav_duration_cts` uses; do **not** include `ack_air`.
   - Emit the parity event `blind_observed` here too (the consult-side already emits it from the NACK feeders at `node_mac_rx.cpp:905/994`; the Lua emits it from this site at `dv:10296` with a `chosen_data_sf` field) — for differential trace parity.
2. **Consult side — already done, do not touch.** `is_blind` (`node_cascade.cpp:30`) → `next_hop_selectable` (`:43`) → `pick_next_cascade_hop` → `issue_send` (`node_mac.cpp:499`). A blind primary already loses to a non-blind alt, or defers when none.
3. **Storage — reuse the existing `std::map<uint8_t,uint64_t> _blind_until` (node.h:1081).** No new structure, no LRU. The map is neighbour-bounded and self-prunes on read (`is_blind` treats expired as not-blind). (The Lua additionally `delete`s the stale entry on read at `dv:3176`; C++ leaving it is harmless — note it, don't add churn.)

> **Out of scope (flagged, not built):** the Lua `classify_blind` returns a 3-way `ok / alt / defer,remaining+1` and threads `remaining_ms` as the explicit defer delay. C++ today does alt-or-defer without the exact `remaining_ms` timing. That is a **consult-side refinement, independent of this feeder** — leave it; the gap this phase closes is purely the missing SET feeder. If a gate scenario shows a defer-timing tail, raise it as a separate item.

**Gate 1:** native unit (an overheard CTS with `rx_id != self` writes `_blind_until[tx_id] = now + window` with max-merge; a node with a blind primary + a non-blind alt picks the alt; the entry reads not-blind after the window) · **target bucket `drop_sf_mismatch` trends DOWN** in dense s18/s16 (fewer RTS at a deaf node) · BASELINE suite no-regress + `leaks 0` + event-count ±3 % · **new `t1xx_blind_until_cts`**: node B CTSes A; node C (with traffic for B *and* a non-blind alt) overhears — assert C issues **no RTS at B** during the window and still delivers (via alt or deferred-then-delivered) · 3 boards.

## Phase 2 — ① mobile-as-transit avoidance  (learn the bit + hard-exclude)

Two halves, both absent today: **learn** the peer's mobility, then **hard-exclude** (never penalize) a mobile next-hop as transit — but allow it as the final dest.

1. **Learn the bit.** In the beacon-RX handler (`node_beacon.cpp`, the decoded inbound `beacon_in`/`b`, ~line 210+), record `b.is_mobile` into a new **256-bit peer-mobile set** `_mobile_peer` (e.g. `uint8_t _mobile_peer[32]` or `std::bitset<256>` on `node.h`) keyed by `b.src`. Faithful to the Lua `mobile_peers` set: **eviction-free** (a 256-bit set, not the LRU `PeerLiveness` slot — that would miss gossip-only mobiles and diverge). **Set-only**, matching the Lua (`dv:9603-9604` sets on `is_mobile`, never clears) — `is_mobile` is a static per-node config flag, so it does not flip at runtime. *(If you judge a clear-on-`is_mobile==false` worthwhile, raise it — it is a deliberate divergence from Lua, not a default to assume.)*
2. **Predicate.** Add `bool is_mobile_peer(uint8_t id) const` (read the bitset) and `bool route_uses_mobile_as_transit(uint8_t dest, uint8_t next_hop) const` = `next_hop != 0 && dest != 0 && next_hop != dest && is_mobile_peer(next_hop)` — verbatim Lua `dv:1329-1334`. **The `next_hop != dest` carve-out is the whole point: deliver TO a mobile, never relay THROUGH one.**
3. **Hard-exclude at the two routing-decision sites** (Lua hard-excludes at three; the third is metrics-eligibility — fold in if the C++ has the analog, else the two below are the behavioral set):
   - **Route-learn choke** — `rt_merge` (`node_routing.cpp:230`), immediately after `rt_find`: if `route_uses_mobile_as_transit(dest, cand.next_hop)` → emit `rt_skip_mobile_transit` and return a skip (mirror the Lua `"mobile_transit_skip"` at `dv:4583` — the candidate is never stored).
   - **Next-hop pick** — `next_hop_selectable` (`node_cascade.cpp:38`), after the `is_blind` line: same check → `return false` (belt-and-suspenders for a route that turned mobile after install; Lua `dv:4099`).
   - **Hard-exclude, NOT penalize** — do **not** fold this into `effective_score`. It is a `return false`/skip-store at both sites (Lua is unambiguous).
4. **`RtCandidate` unchanged** — like the Lua, re-derive mobility per-call from the bitset by `next_hop`; no new candidate field.

**Gate 2:** native unit (a beacon with `is_mobile` marks the peer; `route_uses_mobile_as_transit` true for a mobile transit hop, **false when the mobile IS the dest**; `rt_merge` skips a mobile-transit candidate; `next_hop_selectable` rejects one) · **target bucket** — no route installs through a mobile transit hop · BASELINE suite no-regress + `leaks 0` (the suite has no mobile nodes ⇒ inert there — this guards against accidental exclusion of non-mobile hops) · **new `t1xx_mobile_transit_avoid`**: source S, dest D, a **mobile** node M that is the only-or-best transit on one path plus a non-mobile alt — assert S routes D via the alt (never through M as transit) **and** still delivers a DM addressed TO M directly · 3 boards.

## Phase 3 — ② implicit-ACK from overheard forward-RTS  (multi-hop airtime)

When you overhear your own next-hop *starting to forward* your in-flight frame (its RTS, one hop onward), that is proof the hop decoded — cancel the pending timeout instead of waiting out the ACK timer and firing a redundant retry that can collide with the forwarder's downstream DATA.

1. **Match + cancel.** In `handle_rts` (`node_mac_rx.cpp`), immediately after `track_originator_observation` (line 42) and **before** the overheard early-return at line 108 (the forward's RTS has `r.next` = the *forwarder's* next-hop ≠ us, so it would otherwise be swallowed by the NAV-only return):
   ```
   if (_active->_pending_tx
       && r.src     == _active->_pending_tx->next
       && r.dst     == _active->_pending_tx->dst
       && r.ctr_lo  == _active->_pending_tx->ctr_lo
       && <payload-len match, see §2>) {
       _hal.cancel(kRtsTimeoutTimerId);
       _hal.cancel(kAckTimeoutTimerId);
       _hal.cancel(kRetryBackoffTimerId);          // parity with handle_ack:800
       _hal.emit("implicit_ack_from_forward", ...);
       _active->_pending_tx.reset();
       become_free();
       return;
   }
   ```
   Mirrors the Lua `dv:9863-9893` and the existing explicit-ACK cleanup `handle_ack` (`node_mac_rx.cpp:799-836`). **Note:** the match does **not** key on `origin==me` — the next-hop re-originating *our* frame is identified purely by `next/dst/ctr_lo/len`.
2. **Payload-len match — the one frame-layout detail to nail (implementation, not a design decision).** Lua matched `r.payload_len == #payload + MAC_LEN`. C++'s post-redesign inner layout is `_pending_tx->inner_len` with `DATA_HDR_LEN = 8`, `data_mac_len(flags) = 4|8` (`frame_codec.h:384/405`), and the inner payload-flags byte was removed (`[[data-inner-payload-flags]]`), so it no longer maps 1:1. Resolve the exact C++ equality against `frame_codec.h` so `r.payload_len` (the on-air inner+MAC length the overhearer already reads for NAV at `node_mac_rx.cpp:116`) equals what `_pending_tx` implies. **If the offset is genuinely ambiguous after reading the codec, stop and ask — do not guess a `±MAC_LEN` fudge.** (The `next/dst/ctr_lo` triple is already a strong match; the length is the disambiguator against a `ctr_lo` wrap collision.)

**Gate 3:** native unit (construct a `_pending_tx`, feed an overheard RTS matching `next/dst/ctr_lo/len` → assert both timers cancelled, `_pending_tx` cleared, node free; a *non*-matching overheard RTS does NOT cancel) · **target bucket** — originator retry / `rts_giveup`-after-forward count **trends DOWN** in dense s18/s16 (the redundant retry is the airtime cost) · BASELINE suite no-regress + `leaks 0` · **new `t1xx_implicit_ack_forward`**: chain A→B→C with an **induced hop-ACK loss** A↔B — assert A cancels its pending timeout on overhearing B's forward RTS (no redundant retry) and the frame still completes · 3 boards.

## Phase 4 — ④ load-adaptive cascade back-pressure  (tuned threshold; riskiest; do last)

Shrink the cascade-requeue budget as this node's TX queue backs up, so a congested node sheds cascade-waste instead of requeuing at a fixed budget. **Port the mechanism; tune the threshold up from the Lua `0` default** (decision 2026-06-22) — `0` is maximally aggressive and may be why the C++ author left it unwired.

1. **Implement the gate** in `try_cascade_requeue` (`node_cascade.cpp:123`), between the existing hard-cap/age/overflow check (lines 121-123) and the requeue construction (line 134), reading the live depth `_active->_tx_queue_n`:
   ```
   const int thr  = protocol::cascade_requeue_load_threshold;
   const int over = (int)_active->_tx_queue_n - thr;
   const int load_excess  = over > 0 ? over : 0;
   const int eff_max      = (int)protocol::cascade_requeue_max - load_excess;        // may go ≤0
   if ((int)pt.requeue_count + 1 > (eff_max > 0 ? eff_max : 0)) {
       _hal.emit("cascade_load_skip", ...);       // soft drop — keep path_cascade_exhausted + giveup for the analyzer
       ... drop (same terminal path as the hard cap) ...
       return;
   }
   ```
   - **Clamped/signed arithmetic** — `uint8_t` subtraction would wrap; use `int` intermediates as above (Lua uses `math.max(0, …)`).
   - **Keep the existing `_tx_queue_n ≥ kTxQueueCap(8)` hard overflow** as a backstop (the Lua queue is unbounded and has no array cap; the C++ ceiling is a deliberate belt-and-suspenders — do not remove it).
2. **Tune the threshold — calibrated, not assumed.** Retune `cascade_requeue_load_threshold` (`protocol_constants.h:165`) from `0`. **Seed at `2`**, rationale: with `kTxQueueCap = 8` and `cascade_requeue_max = 3`, `threshold = 2` keeps the **full** requeue budget through depth 2, begins shrinking at depth 3, and fully gates at depth 5 — the soft gate engages only in the upper half of the queue, shedding cascade-waste under genuine backlog without punishing transient single-item queueing (which `threshold = 0` does). **Phase-4 gate SWEEPS `{1, 2, 3}`** (gates fully at depth `{4, 5, 6}` respectively) against `s16` (dense overload) + `s18` (dense) and picks the **lowest** threshold that reduces cascade-waste **without** regressing delivery below baseline. Record the chosen value + the sweep table in the commit/report.
3. **Telemetry** — the `cascade_load_skip` event is new; keep the terminal `path_cascade_exhausted` + the giveup event so the existing analyzers still see the drop.

**Gate 4 (the relaxed/inverted one):** native unit (at `_tx_queue_n` below threshold the budget is full `cascade_requeue_max`; above it the budget shrinks 1:1 and `cascade_load_skip` fires at `eff_max` exhaustion; the `int`-clamp does not wrap at high depth) · **the airtime-aware criterion** — unlike Phases 1–3, this phase *intends* to drop requeues, so the event-count guard is **inverted**: **cascade-family events (`cascade_requeue`, `path_cascade_exhausted`) must DROP** in s16/s18 (the cascade-waste reduction is the whole point) **while same/cross-layer delivery holds ≥ baseline** (s18 ≥98/113, s16 ≥57/80, s19 8/8, s09/s10 unchanged, s15/s17 per the multi-seed gate) · `leaks 0` · **the {1,2,3} sweep table recorded**, lowest non-regressing threshold chosen · 3 boards.

> **Stop-and-ask trigger for Phase 4:** if *every* swept threshold regresses delivery (i.e. the mechanism trades delivery for airtime at *any* setting on this suite), that corroborates the "deliberately omitted" hypothesis — **stop and report the sweep table** rather than shipping a delivery regression. The honest-node ethos favors airtime-frugality, but a delivery drop is the user's call, not a silent trade.

---

## Gate checklist (every phase)
- [ ] `pio test -e native` all pass (`.pio/build/native/program | tail -3` for the true count)
- [ ] BASELINE suite via `dm_delivery_breakdown.py` — no same/cross-layer regression vs `simulation/BASELINE.md` (s18 ≥98/113 · s19 8/8 + mean_hops · s09/s10 3/3+2/2 · s16 ≥57/80 · s15 47/47 + multi-seed cross ≥~90% · s17 ≥26/30), `leaks == 0`, taxonomy no-worse (the phase's target bucket trends down), event-count ±3 % + mean_hops stable (**Phase 4: inverted — cascade events DROP, delivery holds**)
- [ ] new `t1xx_*` focused sim scenario green (built in `~/lora-universal-simulator/test/`)
- [ ] 3 boards: `pio run -e gateway -e xiao_sx1262 -e heltec_v3`
- [ ] leave GREEN + uncommitted — the user commits

## Phase order + dependencies
**1 → 2 → 3 → 4**, ranked by value ÷ (effort × risk). **All four are mutually independent** (different files / decision points) — the order is by risk, not dependency, so each lands, gates, and commits on its own; a phase may be skipped without affecting the others.
- **Phase 1 (③)** first — smallest, infra-complete, ~zero regression risk; a clean warm-up that proves the suite-gate flow.
- **Phase 2 (①)** — prioritized per the 2026-06-22 decision (mobile nodes are in scope); cheap, Lua-unambiguous hard-exclude.
- **Phase 3 (②)** — multi-hop airtime; one frame-layout detail to resolve in `frame_codec.h`.
- **Phase 4 (④)** last — the only phase that can lower delivery; gated on the inverted airtime criterion + the threshold sweep, with an explicit stop-and-ask if no setting holds delivery.
