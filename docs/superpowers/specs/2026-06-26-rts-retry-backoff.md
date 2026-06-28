<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# RTS/ACK same-hop retry — capped exponential backoff (graceful degradation under saturation)

**Status:** IMPLEMENTED + sim-gated; **shipped const-gated OFF (`max_shift=0`)** because the A/B **refuted** the hypothesis. No wire change — TX-scheduling only.

> ## ★ OUTCOME (2026-06-26) — BEB REFUTED; shipped flat (no-op)
> The twin A/B (24-seed `twin_9node_dm`, idealized RF, meshroute engine) shows BEB **monotonically REDUCES** DM delivery — the opposite of the hypothesis:
>
> | max_shift | **0 (flat)** | 1 | 2 | 3 |
> |---|---|---|---|---|
> | mean DM delivery | **47.1%** | 45.6% | 43.5% | 43.5% |
>
> **Why the "thrashing" intuition was wrong:** the same-hop retry budget is only **3** before giveup→cascade. Growing the window doesn't de-correlate contenders enough to matter at 3 attempts; it just makes a node **loiter on a doomed retry** — delaying the cascade-to-alt / giveup and holding `pending_tx` longer → **lower throughput** under saturation. Fast-fail (flat) frees the node to send the next DM sooner. So the saturation lever is *fewer/faster* retries + the cascade, **not** more backoff (the spec's own "load-adaptive / offered-load" caveat is the real direction).
>
> **Shipped:** `retry_backoff_max_shift = 0` (== today's flat, Lua-faithful, zero behaviour change). The BEB machinery (`retry_attempt` on `PendingTx`, `protocol::retry_backoff_window`, the two timeout sites + cascade resets) stays in place, **const-gated + unit-tested**, ready to flip to 3 for a **metal** re-test (real-RF contention dynamics may differ from idealized sim). native **525/525**.
> Sites: `protocol_constants.h` (`retry_backoff_max_shift=0` + `retry_backoff_window`) · `node.h` (`PendingTx::retry_attempt`) · `node_cascade.cpp` (both timeout sites + cascade reset) · `node_mac_rx.cpp` (LOOP_DUP cascade reset) · `test_protocol_constants.cpp` (+window-math test).
>
> The original spec text below is kept for the record.

**Const-gated** so the sim A/B is a clean flip.

## Why
Under uniform saturation (the deliberate all-DM stress: `scenario-dm.txt`, rate 3 × 9 nodes, DM 35% delivered), the same-hop RTS/ACK retry **thrashes**: the delay is `jit = rand_range(0, retry_jitter_ms)` with `retry_jitter_ms = 3×airtime(RTS)` — a **flat window, identical on every retry** (`node_cascade.cpp` rts_timeout_fire RNG #1 + ack_timeout_fire RNG #2). N contenders keep re-drawing from the same small slot → they re-collide. The Lua's escape hatch — give up the same hop and cascade to an alternate (with exponential backoff on the *requeue*, L197) — **doesn't help under uniform overload**, because the alternate is just as contended. So neither the flat retry nor the cascade degrades gracefully when everyone's saturated.

**Lua tracking:** the Lua's same-hop retry is *also* flat (`after rand(0, retry_jitter_ms): tx_rts_retry`, L255/262); it reserves exponential backoff for the cascade-requeue only. So this is a **deliberate divergence** — adding the classic CSMA window-growth to the same-hop retry for overload handling the Lua doesn't do. It is **const-gated with a flat fallback** = the Lua-faithful behaviour, so the divergence is opt-in and the A/B is exact.

## The change
In the two same-hop retry sites (`rts_timeout_fire`, `ack_timeout_fire`), replace the flat window with a **capped per-attempt doubling**:
```
// before --retries_left:
const uint8_t  s      = (pt.retry_attempt < retry_backoff_max_shift) ? pt.retry_attempt : retry_backoff_max_shift;
const uint32_t window = retry_jitter_ms() << s;                 // 1×, 2×, 4×, ... capped at (1<<max_shift)×
const int      jit    = _hal.rand_range(0, static_cast<int>(window) + 1);   // SAME RNG site/order — sim parity
(void)_hal.after(static_cast<uint32_t>(jit), kRetryBackoffTimerId);
++pt.retry_attempt;                                            // alongside the existing --retries_left
```
- **`retry_attempt`** — a new `uint8_t` on `PendingTx` (internal state, NOT on-wire), init 0, incremented at each same-hop retry. Use it (not `max − retries_left`) so it's independent of the init value. Reset to 0 when a fresh flight starts / on cascade (the alt is a new contention context).
- **Both sites** change identically (RNG #1 and #2). Keep `rand_range` as the *same* RNG call in the *same* order — the deterministic `std::mt19937` parity depends on call order.
- **`retry_jitter_ms()` unchanged** (still `3×RTS-airtime`) — it's just the base of the now-growing window.

## The constant (your pattern — one named knob, with the flat fallback baked in)
In `protocol_constants.h`:
```
inline constexpr uint8_t retry_backoff_max_shift = 3;   // doublings cap: window grows 1×,2×,4×,8× then holds (8×3RTS ≈ 1.6-2.4 s max).
                                                        // 0 = FLAT (the Lua-faithful current behaviour) — the A/B's "before" leg.
```
`max_shift = 0` ⇒ `window = retry_jitter_ms << 0 = retry_jitter_ms` ⇒ exactly today's flat retry. So the sim A/B is the single flip `0 ↔ 3`; no separate flag.

## Tests / gate
- **Native:** unit-test the window growth — attempt 0→`J`, 1→`2J`, 2→`4J`, ≥max_shift→`J<<max_shift` (capped); `max_shift=0`→always `J`; `retry_attempt` resets on a new flight/cascade. Pure scheduling math.
- **★ The sim-twin A/B (the payoff — deterministic, no RF variance):** run `simulation/twin_9node_dm.json` (the saturating all-DM workload, idealized RF so contention is the only loss) through `lus` at **`max_shift=0` (flat) vs `=3` (BEB)**. Pass = **DM delivery up** (the 35%-class number lifts toward the contention ceiling), latency cost bounded. Same seed both legs → the only variable is the backoff.
- **s18 no-regression:** BEB changes the dense-contention retry timing → the s18 keystone md5 WILL shift (it's a divergence). Gate on the **delivery breakdown ≥ the flat (`max_shift=0`) baseline** — BEB must not hurt the dense-but-not-saturated case (a too-aggressive backoff could add latency where there's no storm). If s18 delivery drops, lower the default shift.
- **Boards:** all 4 build (lib/core).
- **Metal confirm:** flash, re-run `scenario-dm` (saturating) — DM delivery up vs the flat run; and re-run `scenario-base` — no regression at the sustainable load.

## Notes / caveats
- BEB raises **throughput under contention**, it can't beat capacity — the all-DM load is over the channel, so it lifts 35% toward the ceiling, not to 100% (that's the load-adaptive / offered-load conversation).
- Topology stressors in the twin: **170 is heard by nobody** (its outbound retries can NEVER succeed — they should exhaust → cascade → giveup, not spin; confirm the backoff doesn't just delay an impossible send) and **204 hears only 138** (single-RX). Watch those two in the A/B.
- The cascade-requeue's own exponential backoff (Lua L197) is untouched — this is purely the same-hop leg.
