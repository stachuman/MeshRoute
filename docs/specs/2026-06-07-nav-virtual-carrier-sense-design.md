# NAV — Virtual Carrier Sense (overheard-RTS/CTS reservation) — Design Spec

**Status:** **DRAFT for sign-off 2026-06-07.** Adds predictive medium reservation (802.11-style NAV) on
top of the physical LBT: a node that overhears a **unicast** RTS/CTS not addressed to it stays quiet for
the rest of that exchange, so it can't step on the CTS/DATA/ACK during the silent gaps that LBT reads as
"clear." **C++-only** (the Lua `dv_dual_sf.lua` is a frozen baseline). Pure protocol (`lib/core`),
**sim-testable**. Builds on `docs/specs/2026-06-07-metal-rx-async-tx-sleep-design.md` (Step 3 LBT).
**Author:** Stanislaw Kozicki <cgpsmapper@gmail.com> · **Date:** 2026-06-07

---

## 0. Scope

A node maintains a **NAV** (`_nav_until_ms`): a "medium virtually reserved until" timestamp, set from
**overheard unicast RTS and CTS**, that **defers the node's own *new* transmissions** until it expires.

**In:** the NAV state, the set rules (which overheard frames reserve), the defer rules (which own
transmissions wait), the duration computation, the integration into the existing defer machinery, and a
companion origination jitter. Gated by a new `nav_enabled` config flag (**default off**).

**Out:** the device LBT signal (that's Step 3 of the metal spec); any Lua change (frozen baseline); the
wire format (NAV needs **no new fields** — see §4).

---

## 1. Problem — the silent-gap collision

After a unicast RTS, the channel is **silent** until the CTS comes back. A node that overheard the RTS,
using only LBT (physical carrier sense), reads that gap as idle, transmits, and **collides with the CTS**
at the RTS sender. The same gap exists CTS→DATA and DATA→ACK. LoRa makes it expensive: a collided SF12
DATA wastes ~3 s of airtime and forces a full retransmit.

Crucially, **hidden nodes hear different parts**: a node near the sender hears the RTS; a node near the
receiver hears the CTS. So the reservation must be driven by **both** overhear-sets — neither alone covers
the exchange. This is preventive, not perfect (a node that hears neither isn't covered), but it protects
the network where it can.

---

## 2. Mechanism

Overhear a **unicast** RTS or CTS (dst ≠ me) → compute the remaining-exchange duration → set
`_nav_until_ms = max(_nav_until_ms, now + duration)` → **defer my own new transmissions** until then.
Predictive (virtual) carrier sense, complementing LBT (physical).

```
   S ──RTS──▶ R     S's neighbours hear the RTS → NAV: silent for CTS+DATA+ACK
   S ◀──CTS── R     R's neighbours hear the CTS → NAV: silent for DATA+ACK
                     └─ this silences the hidden node near R that is deaf to S's RTS
```

---

## 3. Duration — from the EXISTING frames (no wire change)

The Node already has `airtime_ms()` and the fixed control-frame sizes, so this is local arithmetic.

- **From an RTS** — it carries `payload_len` + `sf_index` + `rts_flags` (`frame_codec.h:160`):
  `NAV = CTS_air(routing_sf) + DATA_air(sf, payload_len) + ACK_air + 3·turnaround`,
  where `sf` = the SF pinned by `sf_index` (0..2) or, if `sf_index == 3` (ANY/receiver-picks),
  the node's **max** data SF (conservative). Size is exact.
- **From a CTS** — it carries `chosen_data_sf` (`frame_codec.h:120`):
  `NAV = DATA_air(chosen_data_sf, MAX_PAYLOAD) + ACK_air + 2·turnaround`.
  The CTS has no length field, so DATA size is taken as **max (conservative)** — the decided trade-off.
  (A future +1-byte `payload_len` on the CTS would make it exact; not now.)

`turnaround` = a fixed processing/mode-switch gap constant (reuse/scale the bench-measured
`rx_window_slop_ms`).

---

## 4. Set rules — what reserves the medium

| Overheard frame | Sets NAV? |
|---|---|
| **Unicast RTS** (`!(rts_flags & RTS_FLAG_M_BROADCAST)`), dst ≠ me | **yes** — reserve CTS+DATA+ACK |
| **CTS**, rx_id ≠ me | **yes** — reserve DATA+ACK |
| **Channel/broadcast RTS** (`RTS_FLAG_M_BROADCAST`) | **no** — a flood, no CTS to protect (handled by the flood-LBT plane) |
| RTS/CTS addressed to me | no — that's *my* exchange (§5) |
| DATA / ACK / NACK / beacon / floods | no |

`max()` accumulation: a later CTS extends/refreshes a NAV an earlier RTS set.

---

## 5. Defer rules — what waits on an active NAV

While `now < _nav_until_ms`:

- **DEFERS** (unsolicited / *starting* an exchange):
  - the own-message **queue drain** (the RTS for a queued DM, incl. **channel/M-broadcast** sends — *not* exempt),
  - **beacons** and **floods** (F/H/J/Q),
  - a **new incoming unicast RTS** is **ignored** (no CTS, no NACK) — the requester is a hidden node that
    didn't hear the reservation; it times out and retries. This reuses the MAC's existing "busy → drop RTS"
    path (`become_free`/half-duplex serialize, `node_mac.cpp:104`) — NAV just makes the node *virtually busy*.
- **NEVER defers** (time-critical / already committed): the node's own CTS/DATA/ACK/NACK for an exchange it
  is *already in*. A node holding someone else's NAV is not in its own exchange (one flight, half-duplex),
  so there is no conflict — NAV gates only *starting/accepting* a new exchange, never the continuation of
  one. (Matches the time-critical send-path classification: `tx_initiating`/`tx_flood` = deferrable;
  `tx_with_retry` responses = fire ASAP.)

---

## 6. Integration — fold into the existing busy-until defer

NAV is `_nav_until_ms` on the Node, set in the overhear path and consumed by the existing defer machinery:

- **Set:** in `handle_rts` / `handle_cts` (`node_mac_rx.cpp`), at the `dst != me` early-return — compute §3
  and `max()` it into `_nav_until_ms`. (The Node already receives overheard frames; today it drops the
  not-for-me ones — we reserve first, then drop.)
- **Consume:** extend the busy signal the initiation paths already check —
  `effective_busy_until = max(channel_busy_until(), _nav_until_ms)`. Then `tx_initiating` (RTS/floods) and
  `tx_flood` (beacons) defer *unchanged*, via `schedule_lbt_defer(... busy_until + rand(0, lbt_backoff+1))`
  — so **the NAV-held frame inherits the LBT jitter** (no lockstep release; §7). The own-message
  `become_free` drain checks the same gate (defer the queue item to `_nav_until_ms` if set).
- **Ignore incoming RTS:** in `handle_rts`, if `now < _nav_until_ms` and the RTS is addressed to me, drop
  it (no CTS) — the virtual-busy extension of the existing busy-drop.

This means **no new defer plane** — NAV is one more input to the busy-until the LBT already honors, gated
by `nav_enabled`.

---

## 7. Jitter — release de-synchronization (verified)

- The **deferred** RTS is already jittered: `busy_until + rand(0, lbt_backoff+1)`,
  `lbt_backoff = retry_jitter_ms()/2 = 1.5·airtime_routing(8)` (`node.cpp:56-59`, `node_mac.cpp:260`).
  Folding NAV into `channel_busy_until` (§6) means **NAV-released RTS inherits this jitter** → deferred
  nodes spread over the backoff window at `nav_until`, the first RTS re-arms the others' NAV. Self-regulating.
- The **initial** clear-channel RTS is **not** jittered (`become_free` fires it immediately,
  `node_mac.cpp:103-146`). So two *fresh* originators can fire RTS within the ~RTS-airtime window before
  either hears the other → collide, which NAV can't prevent (no RTS decoded).
  **Companion fix (in this feature, gated by `nav_enabled`):** a small origination jitter —
  delay a fresh own-origination RTS by `rand(0, J)` with `J ≳ airtime_routing(RTS)` — so one fires first
  and the other overhears it + sets NAV. Magnitude is a tunable; default `J = retry_jitter_ms()`.

---

## 8. Layer, config, and testability

- **Pure protocol** (`lib/core/node*`) — no device HAL. Runs in the simulator, which **models the medium +
  collisions** (the r3x lossy gate, the LBT-diff scenarios). NAV's collision reduction is therefore
  **measurable in the sim** and lands on metal for free (same Node).
- **`nav_enabled` config flag, default OFF.** Off → byte-identical to today; the existing lua↔meshroute
  differential gates stay green (the Lua has no NAV). On → the new behaviour.
- **Validation = meshroute-engine-only scenarios** (no Lua comparison, since the Lua is frozen):
  - a dense/hidden-node scenario with `nav_enabled` off vs on → **collision count down, delivery up**;
  - a 2-fresh-originator scenario → the origination jitter removes the first-attempt RTS collision;
  - native unit tests for the duration math + the set/defer/ignore rules (FakeClock, crafted RTS/CTS).

---

## 9. Sequencing

1. **NAV** (this spec) — sim-first, `nav_enabled` default off, validated in meshroute-only scenarios.
2. **Step-3 correction** (in the metal spec) — remove the wrong pump CSMA guard; keep the non-blocking
   `channel_busy()` (noise-floor + `is_receiving`) feeding the Node's existing per-frame LBT.

Together: physical carrier sense (LBT) + virtual carrier sense (NAV) = full CSMA/CA.

## 10. Decisions (captured) & open points

**Captured:** channels defer-but-don't-set · C++-only + `nav_enabled` default-off + meshroute-only
scenarios · CTS-NAV conservative (max size, no wire change) · NAV before the Step-3 correction.

**Open for cross-check:**
- The `turnaround` constant + the origination-jitter magnitude `J` (start: `rx_window_slop`-scaled and
  `retry_jitter_ms()`; tune in the sim).
- Whether "ignore incoming RTS while NAV active" should instead send a fast NACK (lets the requester back
  off sooner, but adds a transmission into a reserved window — I recommend **ignore**, not NACK).
