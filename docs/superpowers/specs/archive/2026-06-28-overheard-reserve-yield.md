<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Overheard-reserve yield — don't fight the medium, yield the handshake + retry (saturation throughput)

**Status:** IMPLEMENTED + sim-gated; **shipped const-gated OFF** because the twin A/B refuted Part A. No wire change — TX-scheduling/RX-tuning only.

> ## ★ OUTCOME (2026-06-28) — Part A REFUTED on the twin; shipped OFF (no-op)
> The twin A/B (24-seed `twin_9node_dm`, idealized RF) shows the yield **lowers** DM delivery — the *second* "be patient under contention" idea this twin rejects:
>
> | | yield OFF (today) | yield ON (Part A) |
> |---|---|---|
> | mean DM delivery | **47.1%** | **45.5%** |
>
> (`enable=0` reproduces the flat baseline **bit-identically** per-seed — the const-gate is a true no-op; and per-seed outcomes diverge with `enable=1`, so the yield *is* firing.) **Why:** yielding **extends a flight's lifetime** — instead of fast-failing (giveup→cascade→free the node for its next DM), the flight keeps yielding/retrying and **holds `pending_tx`, blocking the node's tx-queue** up to the 60 s giveup horizon → lower aggregate throughput under saturation. Same mechanism as BEB. **The BEB + yield double-refutation points the opposite way: the saturation lever is *fewer/faster* retries (fast-fail), not more patience.**
>
> **Shipped:** `reserve_yield_enable = 0`, `flood_yield_grab_enable = 0` (Part B untested — the twin carries no floods). Both == today's behaviour (zero change). The machinery (extend-only timeout via `PendingTx::timeout_deadline_ms` + the lifetime-bounded `reserve_yield()` + the two Part-A trigger sites + the Part-B flood hook) stays const-gated + unit-tested, ready for a **metal** re-test and for **moderate-contention** scenarios the extreme twin doesn't cover. native **526/526**.
> Sites: `protocol_constants.h` (3 gated constants) · `node.h` (`PendingTx::timeout_deadline_ms`, `reserve_yield` decl) · `node_mac.cpp` (`reserve_yield()` + deadline tracking in `start_rts/ack_timeout`) · `node_mac_rx.cpp` (Part-A in `handle_cts`/`handle_rts` overheard branches + Part-B in the flood branch) · `test_protocol_constants.cpp`.
> ⚠ The starvation guard works as designed (the yield clamps to `enqueue_time_ms + cascade_requeue_total_max_ms`), but a **60 s** horizon is *too generous* under saturation — it's exactly what lets a yielding flight hog the queue. A tighter bound (max-N-yields, or a short yield horizon) would mitigate but won't beat fast-fail on this twin.
>
> The original two-part spec text below is kept for the record.

Two parts: **A (unicast reserve)** = definite; **B (flood reserve)** = the more experimental "try". Const-gated so each is a clean A/B flip.

## Why
The BEB A/B refuted "widen the retry window" — with only ~3 same-hop retries, the win is **fail-fast, free the node**, not backoff. The same lens exposes a real waste (coder's caveats, cases 2/3/4): when A is mid-handshake (sent its RTS, `awaiting_cts`; or sent DATA, `awaiting_ack`) and **overhears its next-hop become busy**, A's CTS/ACK *cannot* come — yet A sits out the blind timeout, and worse, its timeout can **fire repeatedly during the overheard exchange, burning its retry budget** on attempts that the medium (LBT) won't even let out. A then cascades/gives up a next-hop that was merely *briefly* busy.

**Principle (the user's):** don't fight for the channel — *yield* to the exchange you overheard, then try again when it's free. This is CSMA politeness: contenders serialize instead of colliding. It should beat flat-retry exactly where BEB lost, because it removes wasted fighting (more airtime for the exchanges that complete) without the loiter (no burned retries).

**Lua tracking:** the Lua already NAVs on overheard RTS/CTS (virtual carrier sense) but does **not** couple the NAV to the sender's own pending handshake timeout — so this is a deliberate, const-gated extension, not a port.

## Part A — yield to an overheard UNICAST reserve (definite)
Trigger: A has `pending_tx` with `awaiting_cts` or `awaiting_ack`, and overhears a reserve that **involves A's next-hop**:
- an **overheard CTS** (`rx_id != A`) whose **sender (`tx_id`) == A's next-hop** → the next-hop just CTS'd someone else, about to receive their DATA → busy; or
- an **overheard RTS** (`r.next != A`) whose **target (`r.next`) == A's next-hop** → someone is about to occupy the next-hop.

Action (on top of the existing `nav_arm`):
1. Compute the **reserve duration** `D` (see Estimate below).
2. **Push A's pending timeout** (`kRtsTimeoutTimerId` if `awaiting_cts`, else `kAckTimeoutTimerId`) out to `max(current_deadline, now + D)`. **Do NOT decrement `retries_left` / bump `retry_attempt`** — a yield is not an attempt.
3. When the (pushed) timeout fires — after the exchange — A retries **once**, with the existing jitter, against a now-free next-hop.

Net: A's retries are spent on *real* attempts when the medium is free, never burned during a reserve.

## Part B — yield to (and grab) an overheard FLOOD RTS-M (the "try")
Trigger: A has `pending_tx` `awaiting_cts`, and overhears an **RTS-M for a flood A does NOT already own** (new channel id).

Rationale: a flood is network-wide, so A's next-hop will (probably) retune to the data-SF to catch the DATA-M → A's CTS won't come. So A should NOT block the flood-overhear (today's guard skips it only on `_pending_rx`, not on `awaiting_cts`):
1. **Allow the flood-grab** even while `awaiting_cts` — retune to the data-SF and catch the new channel message (the existing `kOverhearRetuneTimerId` returns A to routing-SF after).
2. **Push A's CTS-timeout** out by the flood's `D` (same yield as Part A; no retry burn).
3. If the CTS *does* arrive (next-hop didn't retune), the normal path still handles it — the push is just a longer ceiling. Else A retries the DM after the flood.

Net: A catches the flood (channel coverage) **and** doesn't burn its DM retries — both, instead of today's "miss the CTS → timeout → retry" with no flood caught.

## Reserve-duration estimate `D`
- **Overheard CTS / flood RTS-M:** the frame carries `chosen_data_sf` → `D = cts_to_data_gap + airtime(chosen_data_sf, est_len) + ack_airtime + slop`. Reuse `nav_duration_cts` — but set `est_len` to **½ max payload** (the user's call: the actual DATA length is unknown; half-max is the expected value, and the **LBT backstops any under-estimate** — A's post-yield RTS just defers again if the medium's still busy). If `nav_duration_*` currently assumes full-max, switch it to the shared half-max estimate.
- **Overheard RTS (no SF negotiated yet):** estimate the data-SF as **`max_data_sf`** (conservative — longest airtime) for `D`. Not exact, but "good enough" + LBT-backstopped.

## ★ Safety: yield must not become starvation
Under saturation the medium is always busy, so the yield needs two bounds (both already in the model — confirm they hold):
- **Giveup clock:** the push extends the per-attempt timeout but must **count toward the flight's total lifetime / giveup**. A yields politely a bounded number of times, then drops/cascades a genuinely dead next-hop. No infinite wait. (Don't let the push reset the enqueue/giveup clock.)
- **Retry jitter preserved:** the post-yield retry keeps its `rand(0, retry_jitter_ms)` so contenders that all yielded to the same exchange take *randomized* turns afterward, instead of firing in lockstep at the reserve's end and re-colliding.
- **170 (heard-by-nobody) check:** its outbound never gets a CTS and never overhears its own next-hop (it has effectively no usable next-hop) — confirm the yield doesn't *extend* its doomed sends; it should still hit the giveup and cascade/drop on schedule.

## Constants (named, tunable)
```
inline constexpr uint8_t  reserve_yield_enable      = 1;   // 0 = off (today's behaviour) — the A/B flip; Part A
inline constexpr uint8_t  flood_yield_grab_enable   = 1;   // 0 = off — Part B independently flippable
inline constexpr uint16_t reserve_est_payload_bytes = /* protocol::max_payload_bytes_hard_cap / 2 */;  // ½-max length estimate for D
```

## Tests / gate
- **Native:** the yield pushes the timeout without decrementing `retries_left`/`retry_attempt`; the giveup clock still expires on schedule (a flight under continuous reserves eventually gives up — assert it does NOT live forever); `D` math (½-max, max-SF for RTS).
- **★ Twin A/B (deterministic):** `simulation/twin_9node_dm.json` (saturating all-DM) — `reserve_yield_enable 0 vs 1`. **Pass = DM delivery up vs flat** (this is where BEB lost; the yield should win by removing wasted fighting). Watch latency (yields add delay, bounded by giveup) and **confirm no starvation** (no node's delivery collapses to ~0). Then flip `flood_yield_grab_enable` and add a channel-bearing twin to check Part B lifts channel coverage without hurting DM.
- **s18 no-regression:** delivery breakdown ≥ the `enable=0` baseline (the yield shouldn't hurt the non-saturated dense case).
- **Boards:** all 4 build.
- **Metal:** `scenario-dm` (saturating) DM up vs flat; `scenario-base` no regression.

## Notes
- This is the *correct* expression of the saturation insight: BEB tried to fix contention by spacing one node's own retries (lost); this fixes it by **not contending at all when the medium is already taken** (CSMA politeness) — the airtime saved goes to the exchange in progress, lifting aggregate throughput.
- Part B is the riskier half (it changes RX-SF behaviour mid-handshake) — keep it independently gated so A can ship even if B needs more iteration.
