<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Dead-handshake fast-fail — graceful degradation for handshake-isolated peers (#3)

> ## ⛔ REVERTED (2026-06-29) — kept as a FINDINGS reference, code removed
> The implementation was **reverted from the tree** (clean `git checkout HEAD` of the 7 files; zero `#3` references remain). Decision: **address asymmetric connections at the ALGORITHM (routing) level**, not as a MAC band-aid. This doc is retained for the findings that inform that work:
> 1. **The diagnosis stands:** delivery is **topology/asymmetry-bound** (204/247 have zero bidirectional links; ~37% of DMs touch them). No MAC retry/backoff/yield/fast-fail moves it — confirmed three times (BEB, reserve-yield, this).
> 2. **A reusable signal:** the CTS-only `rts_no_cts` streak (reset only by a real CTS, NOT by a beacon) is how you DETECT asymmetric handshake-isolation — the liveness tiers can't, being beacon-cleared. An algorithm-level approach likely wants this signal feeding the **route plane** (don't select / advertise a route through a handshake-isolated next-hop), not a MAC drop.
> 3. **The `< silent` distinction** (asymmetric-isolated = healthy-tier + never-CTS, vs departed-relay = silent-tier) is the line the algorithm must also draw to not break §P3 recovery.
> 4. **Metal caveat:** the idealized sim has 204/247 at 0% delivery; metal ~40% via lucky marginal links. Any "drop the isolated peer" policy sacrifices those — tune/verify on metal.
>
> Original spec text (the reverted implementation) follows.

**Status:** ⛔ REVERTED — was: IMPLEMENTED + sim-gated, shipped ON at `dead_handshake_rts_threshold = 10`. Device-only (no wire change). The third leg of the saturation work — after BEB and the reserve-yield were both refuted, this was the firmware change the structural picture justified, now superseded by an algorithm-level asymmetric-link approach.

## Why
The 9-node saturation problem is **topology-bound, not MAC-bound**. In the live node-72 topology, **204 and 247 are handshake-isolated** — zero bidirectional links (they hear others / are heard, but never *both directions on one link*, so an RTS-CTS-DATA-ACK can never complete). **37% of the 54 DMs are to/from them.** The baseline delivery (~57% sim / 63% metal) is sitting on the structural ceiling.

A doomed flight to such a peer otherwise spams **~9–80 RTS over ~60 s** of cascade-requeue, stealing airtime + battery. This change makes those flights **fail fast** so the airtime goes to the deliverable traffic and the isolated nodes stop thrashing.

**The trap the existing liveness falls into:** the suspect/silent/dead tiers (`record_peer_rts_timeout` → `mark_peer_suspect`) are **cleared by ANY frame RX**, including the isolated peer's **beacons** (`clear_peer_suspect`, node_beacon.cpp). We *do* hear 204/247's beacons — we just can't complete a handshake back — so the tiers never escalate them to dead. The liveness plane is **beacon-blind to asymmetric handshake-isolation.** A dedicated **CTS-only** signal is required.

## The mechanism
A per-peer streak that **only a real CTS resets** (never a beacon), plus a TTL re-probe.

- **State** (`PeerLiveness`, node.h): `uint8_t rts_no_cts` (consecutive no-CTS RTS-giveups) + `uint64_t dead_handshake_until_ms`.
- **Increment:** `record_peer_rts_timeout(node, ctr_lo, no_cts)` gains a `no_cts` arg — `rts_timeout_fire`'s giveup passes `true` (no CTS = isolation evidence), `ack_timeout_fire`'s passes `false` (a CTS WAS seen). On `no_cts`, bump `rts_no_cts`; at `>= dead_handshake_rts_threshold`, set `dead_handshake_until_ms = now + dead_handshake_ttl_ms` (node_routing.cpp).
- **Reset:** `handle_cts` — when a real CTS matches our flight (`c.tx_id == pending_tx->next`), clear `rts_no_cts` + the mark for that peer. **This is the ONLY reset path** — `clear_peer_suspect` (beacons) deliberately does NOT touch it (node_mac_rx.cpp).
- **Fast-drop:** in `cascade_to_alt`'s `alt == 0` branch (all candidates exhausted), BEFORE the §P3 RREQ + `try_cascade_requeue` — if `from_next` is marked dead-handshake **AND** `liveness_penalty_q4(from_next) < peer_silent_penalty_q4` (still HEALTHY-tier) → give up NOW (`send_failed` push, reset `pending_tx`, `become_free`), skipping the RREQ flood + the 60 s requeue (node_cascade.cpp). Counter `_dead_handshake_drops++`.
- **Counter:** `status` / `rcmd <id> status` gain `fastdrop=` (fw_main.cpp).

**The `< silent` guard is the keystone.** It splits the two no-CTS cases:
- **Asymmetric-isolated (204/247):** never CTS'd BUT healthy-tier (we hear its beacons) → fast-drop. The RREQ would find nothing (the link, not the route, is the problem).
- **Departed / genuinely-silent relay (the §P3 case):** never CTS'd AND silent-tier (we stopped hearing it) → **NOT** fast-dropped → §P3's `emit_route_request` legitimately recovers it via a fresh path. (Verified: the two single-candidate cascade tests, test_node_r3.cpp:505/1011, only pass because their relay goes silent.)

**Sites:** `protocol_constants.h` (2 constants) · `node.h` (`PeerLiveness` +2 fields, `record_peer_rts_timeout` sig, `_dead_handshake_drops` + getter) · `node_routing.cpp` (increment) · `node_cascade.cpp` (2 call sites + the fast-drop) · `node_mac_rx.cpp` (`handle_cts` reset) · `fw_main.cpp` (`fastdrop=`) · `test_node_r3.cpp` (callers pass `no_cts`).

## ★ OUTCOME (2026-06-28) — delivery is STRUCTURAL; #3 is an airtime/graceful-degradation win
16-seed A/B on the node-72 topology (`simulation/topo_9node.json` + the `tools/runs/41507e` ledger → `/tmp/sim_41507e.json`):

| `dead_handshake_rts_threshold` | DM delivery | total rts_tx | fast-drops |
|---|---|---|---|
| baseline (off) | 56.9% | 848 | 0 |
| 3 | **53.2%** ⚠ | 583 | 25.2 |
| 6 | 56.6% | 715 | 20.4 |
| **10 (shipped)** | **57.3%** | 785 | 12.9 |

- **Delivery is FLAT at K=10** (+0.4 = noise). This is the conclusive result: the loss is **purely structural** (the isolated nodes), exactly as predicted — reclaiming the doomed airtime does NOT add deliveries, because the deliverable traffic was never airtime-starved and the isolated nodes deliver 0% regardless. **MAC has no delivery headroom here; the delivery fix is RF/deployment for 204/247.**
- **#3's value is AIRTIME / graceful degradation:** the isolated nodes stop thrashing (~7% less total channel traffic at K=10; `fastdrop` ≈ the true doomed-DM count ~10–12, i.e. no false positives).
- **K too low harms:** at K=3, `fastdrop=25` ≫ the doomed count — it catches transiently-**congested-but-reachable** peers (3 consecutive no-CTS under saturation) and drops recoverable flights for the TTL → −3.7 delivery. K=10 is the safe point.

**⚠ Metal caveat the sim cannot show:** the idealized sim has 204/247 at **0%** delivery, so fast-dropping them sacrifices nothing. On **metal** they deliver **~40%** via lucky marginal-link fluctuations — and #3 would **sacrifice those** for airtime. The sim is blind to this. So #3's metal *delivery* impact is uncertain (neutral to slightly-negative for the marginal nodes); **metal-A/B before trusting it.**

## How to tune
Two knobs (`protocol_constants.h`), and the threshold doubles as the on/off gate (set it to 255 to disable for a clean A/B).

**`dead_handshake_rts_threshold` (K) — the primary knob (shipped 10).**
- *What it trades:* lower K = catch isolation faster + save more airtime, BUT risk **false positives** on congested-but-reachable peers; higher K = only truly-never-CTS peers, delivery-safe, less airtime saved.
- *The tuning rule:* set K **above the longest no-CTS streak a reachable-but-congested peer hits under your load.** A reachable peer CTSes occasionally → its streak is **bounded**; a handshake-isolated peer never CTSes → **unbounded**. K must sit in the gap.
- *The diagnostic:* `fastdrop` (per run, or the `status` counter on a node) should ≈ **the count of DMs to/from genuinely-isolated nodes.** If `fastdrop` is much larger, K is too low (false positives — and delivery will sag, as K=3 showed).

**`dead_handshake_ttl_ms` — the re-probe interval (shipped 60000).**
- After the TTL, exactly one flight re-probes the peer (re-marks if still doomed; clears on a CTS).
- *Shorter:* notices a recovered/marginal link sooner → catches more of the marginal nodes' **lucky deliveries** (matters on metal, where they're ~40% not 0%), at the cost of less airtime saved + more re-probe RTS.
- *Longer:* saves more airtime, slower to re-trust a recovered node.
- **Tune the TTL on METAL**, not the sim — the sim under-shows the lucky-delivery sacrifice (it has the isolated nodes at 0%).

**The A/B method.**
```
# sim (ranks airtime + flags false positives; does NOT predict metal delivery)
sed -i 's/dead_handshake_rts_threshold = [0-9]*/dead_handshake_rts_threshold = 255/' lib/core/protocol_constants.h   # off
cmake --build ~/lora-universal-simulator/build --target lus -j8
python3 /tmp/ff_ab.py "off"     # vs "=10" — compare delivery (must not drop), fastdrop (≈ doomed count), total rts_tx
# metal (the authoritative delivery gate — the only place the lucky-link sacrifice is visible)
#   run the oracle yield-off baseline vs threshold=10; watch 204/247's OWN delivery + the airtime, not just the aggregate.
```

## Tests / gate
- **Native:** 526/526 (the giveup-plane tests at test_node_r3.cpp pass `no_cts`; the §P3 + requeue-cap tests confirm the `< silent` guard preserves departed-relay recovery). No value is pinned in a test — K is a tunable.
- **Sim:** done (above) — delivery-flat at K=10, no false positives, ~7% airtime saved.
- **★ Metal (pending):** flip K 255↔10; confirm the aggregate delivery doesn't drop and the isolated nodes' airtime falls; tune the TTL for the lucky-delivery balance.
- **Boards:** all 4 build (lib/core + fw_main).

## Bottom line
This conclusively closes the saturation MAC investigation: **delivery is topology-bound** (204/247), not retry/backoff/yield/fast-fail tunable. #3 is the graceful-degradation / airtime cleanup — keep it (an isolated node shouldn't burn battery spamming doomed RTS) — but the *delivery* answer is RF/deployment for the isolated nodes.
