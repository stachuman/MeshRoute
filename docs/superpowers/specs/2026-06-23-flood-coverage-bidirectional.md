<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Channel-flood coverage — seed only bidirectionally-confirmed neighbours

**Status:** ⏸ **PARKED (user decision 2026-06-23) — NOT to be built for now.** Root cause is CONFIRMED (the `166 SILENT nbrs: 184=Y` trace line) and the fix is designed, but the seed-confirmed change risks more flood air traffic (it degrades toward a full flood in low-unicast scenarios), and that cost isn't justified against the benefit — faster asymmetric-leaf delivery — when the **repair-pull already delivers** to those leaves (just slower). The repair-pull stays the accepted backstop. **Revisit if leaf-delivery latency becomes a real operational problem.** The coder instruction below is preserved as the ready design for that day.

---

_Original coder instruction (preserved, not active):_

**Was:** coder instruction. Behavior-changing. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. Bench-confirmed root cause (2026-06-22/23, the `166 SILENT nbrs: 184=Y` trace line). ⚠ Has a real regression risk (see Gate) — there is a **stop-and-ask** built in.

> **Sequencing:** land `2026-06-22-channel-ctr-epoch-persist.md` FIRST — otherwise reboot id-reuse (`already-buffered`) keeps masking the bench tests for this change.

## Why (confirmed root cause)

The flood coverage bitmap is seeded from a node's `hops==1` neighbours (`flood_set_my_coverage`, node_channel.cpp:506) = *"nodes I HEAR"*. But a broadcast's real coverage is *"nodes that HEAR ME"* (the reverse link). On asymmetric links these differ. Bench proof (verified by the flood trace): in chain `254—222—166—184`, **222 heard 184 one-way** (had it `hops==1` at flood time) so it stamped `184` covered on relay; `166` — 184's *only* real link — then saw `184=Y` and **stayed silent** (`flood_forward_decision:565`). 184 got the message only via the repair-pull, **5 min** later.

**Why the cheap signals don't work** (ruled out, so the coder doesn't retry them):
- **Liveness-suspect exclusion** — `clear_peer_suspect` fires on *any* RX from a peer, and the asymmetric claimer *hears* the sink's beacons, so a suspect set by a failed RTS is cleared by the next beacon. Never sticks for the asymmetric case.
- **Beacon back-reference** (does the sink's beacon list me?) — the sink sends `n=0` identity-only beacons; nothing to read.

The only durable signal for *"N hears me"* is **N having transmitted a frame addressed to me** (a CTS to my RTS, an ACK to my DATA, or any unicast Q/DM to me). That proves N decoded one of *my* transmissions, and — crucially — it is **NOT** set by merely hearing N's beacon.

## The signal — `last_heard_me_ms` on `PeerLiveness`

1. **Field.** Add `uint64_t last_heard_me_ms = 0;` to `struct PeerLiveness` (node.h:973). Cheap — `PeerLiveness` is the bounded per-direct-neighbour LRU, and flood coverage only ever asks about direct (`hops==1`) neighbours, so they're exactly the slots that exist.
2. **Set it on a unicast addressed to ME** (never on a beacon):
   - `handle_cts` (node_mac_rx.cpp:295) — in the **addressed-to-me** branch (`c.rx_id == _node_id`, i.e. the CTS answers *my* RTS): `peer_liveness_slot(c.tx_id, true)->last_heard_me_ms = _hal.now();`
   - `handle_ack` (node_mac_rx.cpp:813) — the ACK is to us → stamp the ACK sender.
   - The **Q** (`handle_q`) and **DATA** (`handle_data`) addressed-to-me paths — stamp the sender too. This is what catches a channel-listener that only ever **pulls** (a `CHANNEL_PULL` Q from N to me proves N heard my digest beacon ⇒ N hears me) even with no DM traffic. CTS/ACK alone covers the DM case; add Q/DATA so a pull-only leaf is also confirmable.
   - ⚠ **Do NOT** stamp it from `ingest_beacon` / any broadcast RX — that reintroduces the one-way bug.
3. **Helper** (node.h): `bool peer_hears_me(uint8_t n) const { const PeerLiveness* s = peer_liveness_slot(n, false); return s && (_hal.now() - s->last_heard_me_ms) <= protocol::flood_confirm_ttl_ms; }` (add a const overload of `peer_liveness_slot`, or make the helper non-const — coder's call).
4. **Constant** `flood_confirm_ttl_ms` (protocol_constants.h) — the freshness window. Default **1 800 000 ms (30 min ≈ 2 beacon periods)**: a node that DMs or pulls within two beacon periods stays confirmed (the sink pulls each time it hears a digest, so it self-refreshes). Tunable; mirror the `next_hop_live_ttl_ms`/`seen_bitmap_ttl_ms` style.

## The change

**Core (the fix):** `flood_set_my_coverage` (node_channel.cpp:506) — seed a `hops==1` neighbour **only if `peer_hears_me(dest)`**; always seed `_node_id` (self). So 222 (no unicast ever received from 184) stops claiming 184; 166 (gets 184's CTS/ACK/pulls) still claims it.

```cpp
void Node::flood_set_my_coverage(uint8_t* bm) const {
    seen_set(bm, _node_id);
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1
            && peer_hears_me(_active->_rt[i].dest))                    // ← NEW: only claim confirmed-reverse links
            seen_set(bm, _active->_rt[i].dest);
}
```

**Decision — the relay-decision side `flood_any_unmarked` (node_channel.cpp:511) — your call:**
- **Option R (reach-first — RECOMMENDED):** leave `flood_any_unmarked` **UNCHANGED** (counts all `hops==1`). The real link-owner (166) then relays for 184 whenever 184 is unmarked, **regardless of confirmation freshness** ⇒ 184 is always reached by the flood. Cost: in a topology where an upstream has the sink as a `hops==1`-but-unreachable neighbour, that upstream wastes *one* relay (it can't reach the sink; its relay doesn't mark the sink, so the real owner still relays). Bounded, and only in asymmetric-direct layouts. **Matches your "reach 184 reliably" priority + needs only one function changed.**
- **Option F (frugal):** also gate `flood_any_unmarked` on `peer_hears_me`. No wasted relays, but 166 relays for 184 only while its confirmation is fresh; a stale gap falls to the repair-pull (the slow path you hit). Self-heals (the sink's next pull refreshes confirmation).

Recommend **Option R**. Whichever, the **seed change is the essential fix**.

## Not a wire change

No frame/format change — only *which bits a node sets in the existing 32-B coverage bitmap*. `wire_version` untouched.

## Gate — and the stop-and-ask

The channel flood is exercised by the **channel-carrying suite scenarios (s15, s17)**, so this CAN move their numbers. Run the full `simulation/BASELINE.md` suite:

- **No DM/cross-layer delivery regression** vs the table (s18 ≥98/113 · s19 8/8 · s09/s10 3/3+2/2 · s16 ≥57/80 · s15 47/47 + cross multi-seed ~90% · s17 ≥26/30), **`leaks == 0`**.
- ⚠ **THE risk — channel delivery + airtime in s15/s17.** Seed-confirmed *removes* coverage claims for any neighbour that hasn't sent us a unicast, so in a low-unicast scenario the flood degrades toward a **full flood** (more rebroadcasts → more airtime, possibly more collisions). **Gate `s15` channels at ≥ 218/224 and watch the channel/`flood_tx` event-count.** If channel delivery drops or flood airtime balloons, **STOP and report** — the signal may need broadening (e.g. also confirm from a successful *overheard* exchange) or Option F may be the wrong polarity. Do not ship a channel-delivery regression silently.
- **Native units:** `peer_hears_me` true only within TTL after a CTS/ACK/Q-to-me (false after a beacon-only RX, false past TTL); `flood_set_my_coverage` omits an unconfirmed neighbour and includes a confirmed one + self.
- **NEW sim scenario `t1xx_flood_asymmetric_leaf`** *if the engine models per-direction asymmetry* (A hears B, B does not hear A): assert the flood **reaches** the leaf (vs. silent-suppressed before). If the sim can't express a one-way link, say so and gate this on **metal** instead (below) — don't fake it.
- **3 boards** build.
- **Metal re-test (the real proof):** with the `2026-06-22-flood-decision-debug-trace.md` trace on, resend a channel from 254 and confirm **166 logs `RELAY`, not `SILENT`**, and 184 receives it in the flood window (not minutes later via pull).

## Self-heal + backstop (context, not code)

Even with Option F, the system self-heals: a missed flood → the sink pulls the digest → its `CHANNEL_PULL` stamps `last_heard_me_ms` at the real owner → the next flood reaches it. And the repair-pull remains the backstop for the first-flight / stale-confirmation gap — this change makes the flood do its job in the common case, it doesn't remove the safety net.
