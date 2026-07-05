<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# CTS-wait metal turnaround slop — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-05, from a metal cross-layer DM that RTS↔CTS-loops forever, never sends DATA). The user commits + flashes; I quality-gate. **No wire change, no NV bump.** One line in `start_rts_timeout` + a native test. **Independent** of the three gateway specs (it's a general MAC fix), but it is the **blocker for the first cross-layer DM**.

## Why (the metal bug — diagnosed from a live log)
The sender RTSes a gateway, the gateway CTSes, but the sender **never sends DATA** — it re-RTSes on the gateway's window cadence forever. Root cause: the **CTS-wait timeout is ~50-70ms too short on metal.**

`start_rts_timeout` (node_mac.cpp) arms the CTS-wait as:
```cpp
const uint32_t base  = airtime_routing_ms(8) + airtime_routing_ms(4);   // RTS + CTS on-air time (active BW)
const uint32_t shift = attempt < 2 ? attempt : 2;
const uint32_t delay = (base << shift) + 1;                              // ❌ NO rx_window_slop_ms
```
It is the **only one of the three handshake-waits that omits `rx_window_slop_ms`** — the real-metal radio-turnaround margin that `airtime_ms` can't see (`rx_window_slop_ms(sf) = ((1<<sf)*1000)/bw + 1 + 50` ≈ ~53ms). Both siblings include it, and `start_ack_timeout`'s own comment documents this exact failure for the ACK: *"landed ~70ms BEFORE the ACK on metal → cleared awaiting_ack → real ACK ignored → redundant re-RTS."*
- `start_ack_timeout` (ACK-wait): `... + rx_window_slop_ms(sf) + rx_window_slop_ms(_cfg.routing_sf)` ✅ (two turnarounds)
- `start_pending_rx_expiry` (DATA-wait): `... + rx_window_slop_ms(sf)` ✅
- `start_rts_timeout` (CTS-wait): **❌ none**

**Mechanism:** on metal the CTS round-trip = RTS airtime + [sender TX→RX turnaround] + [gateway RX→TX turnaround] + CTS airtime + RX_DONE lag. `base` covers only the airtime; the two turnarounds (~50-70ms) are exactly `rx_window_slop_ms`. So the timeout fires early → `rts_timeout_fire` clears `awaiting_cts` → the arriving CTS hits `if (!awaiting_cts) return;` (node_mac_rx.cpp:337) and is **ignored** → re-RTS.

**Why it loops forever (never caught by backoff):** for a gateway target, `rts_timeout_fire` (node_cascade.cpp:314) takes the `gateway_doorstep_hold()` path — a *patient window-aware requeue* — **instead of** the exponential-backoff retry. So it re-fires at **shift 0 every time** (window-aligned), and the `(base << shift)` backoff **never grows** to cover the late CTS. (This is why the observed retry cadence is the gateway's window period, not a doubling backoff. `gateway_doorstep_hold` is *correct* — the window was simply too short; do NOT change it.)

**Why it surfaced now:** the gateway's layer runs at **62.5kHz** (per-layer BW) — double the airtime of 125kHz — so the CTS reliably lands at ~**204ms**, well past the slop-less ~135ms window. At 125kHz the CTS lands ~120ms and usually slipped in, so single-layer DM "worked." This is a **latent CTS-wait bug the narrow BW exposed**, not a per-layer-BW regression.

## The fix (`lib/core/node_mac.cpp`, `start_rts_timeout`, the unicast tail — NOT the `m_broadcast` early-return branch)
Add the metal turnaround slop, mirroring `start_ack_timeout`. The CTS round-trip crosses **two** radio turnarounds (sender TX→RX, gateway RX→TX), both at `routing_sf` (RTS and CTS are routing-SF frames, no SF reconfig), so add **two** slops — matching how the ACK-wait covers its two reconfigs:
```cpp
const uint32_t slop  = _hal.rx_window_slop_ms(_cfg.routing_sf);   // ~0 on sim (HAL hook) -> s18 byte-identical; ~53ms/turnaround on metal
const uint32_t delay = (base << shift) + 2u * slop + 1u;          // +2 turnarounds: sender TX->RX + gateway RX->TX (base covers on-air only)
```
Keep `timeout_deadline_ms = _hal.now() + delay` as-is (it already reads the final `delay`). At SF7/62.5kHz this lifts the shift-0 window from ~135ms to ~240ms — comfortably past the observed 204ms CTS, and it never shrinks the window (slop ≥ 0), so no path regresses.

**Scope:** only the unicast CTS-wait `delay`. The `m_broadcast` branch (fire-and-forget, arms `kCtsToDataGapTimerId`) returns before this and is untouched.

## Tests
**Native (`test/test_node_r3.cpp` or `test/test_mac.cpp`):**
- **Inclusion test** — with a HAL stub whose `rx_window_slop_ms(sf)` returns a known non-zero `K`, drive an RTS send (unicast, awaiting_cts) and assert the armed CTS-wait deadline == `(base << shift) + 2*K + 1` (i.e. the slop is now present and doubled). If the test HAL currently hard-returns 0, add a settable slop hook (mirror any existing HAL knob). This is the regression guard that pins the fix.
- **Sim-inert / byte-identical** — the production/native HAL returns `rx_window_slop_ms == 0` (idealized sim), so `delay` is **unchanged** on native → the full native suite passes unchanged and lus **s18 stays byte-identical**. Confirm both.

**★ Bench (metal — the real proof, the user runs):**
- Repeat the failing case: `send_layer <hash> "Test"` from a layer-100 node to a layer-102 dest, gateway on 62.5kHz layer 100. **Expected:** after a CTS, the sender emits `»tx DATA` (a `data_tx` telemetry) within the **first or second** gateway window — no more endless RTS↔CTS. The cross-layer DM reaches layer 102.
- Also spot-check a same-layer metal DM (any SF/BW) still delivers — the slightly-longer CTS-wait must not regress normal DM (it only delays a retry on a genuinely-lost CTS by ~2 slops).

## Sites
`lib/core/node_mac.cpp` (`start_rts_timeout`, the unicast `delay`) · `test/test_node_r3.cpp` (or `test/test_mac.cpp`) — the inclusion + inert tests. **No wire, no NV, no other timeout touched.**
