# MeshRoute Protocol — behaviour map

Companion to [`frames.md`](frames.md) (wire structure). This is a **navigation map, not a spec**: each
behaviour plane gets a few sentences of *what it does and why*, then points to the **source of truth** — the
code (`file::function`) and the design spec(s).

> **For future agents — read this first.**
> - **The code is authoritative.** This file is deliberately high-level; it tells you *where* the behaviour
>   lives and *why* it exists, not the line-by-line *how*. When in doubt, the code wins.
> - **To get oriented:** skim the plane list below, then jump to the cited `Source:` (code) or `Spec:` (design doc).
> - **To keep it from rotting:** when you change behaviour, update a plane's one-line summary here *only if the
>   shape changed*. Put the real detail in the code comment + the spec — never re-narrate it here. If an entry
>   ever disagrees with the code, the code is right and the entry is a bug: fix the pointer, don't grow the prose.
> - Specs live under `docs/superpowers/specs/` (recent) and `docs/specs/` (the 2026-05/06 foundation set). They
>   are point-in-time design records; the newest one for a topic supersedes older ones.

---

## 1. Routing

Distance-vector route table (per-leaf), merged from beacons; **on-demand AODV** (F-frame RREQ/RREP flood) fills a
gap when the table has no route. Candidates are scored from link SNR + hop count; the primary is `candidates[0]`.
- **Source:** `node_routing.cpp` (`rt_merge`, `learn_direct_neighbor`, scoring) · `node_route_discovery.cpp` (RREQ/RREP)
- **Spec:** `docs/specs/2026-05-30-r2-route-hardening-design.md` · `docs/specs/2026-05-29-c3-h-f-floods-design.md`

## 2. MAC / data plane

Single-slot stop-and-wait: **RTS→CTS→DATA→ACK** (NACK to refuse), each hop re-running the handshake. Listen-before-
talk (LBT) + NAV virtual carrier sense gate the TX; a rolling duty-cycle budget tier throttles under load; failed
next-hops cascade to alternates / hop-budget reroute.
- **Source:** `node_mac.cpp` (`do_data_tx`, `duty_over_budget`, budget tiers) · `node_mac_rx.cpp` (RX handlers) · `node_cascade.cpp` (alt-walk)
- **Spec:** `docs/specs/2026-05-30-r3-data-plane-design.md` · `2026-05-31-r4.5-lbt-design.md` · `2026-06-07-nav-virtual-carrier-sense-design.md` · `2026-05-31-r4-budget-nack-design.md`

## 3. Beacons

In **discovery** (first ~60 s, or route-starved) a node beacons fast + full-page and broadcasts `Q:REQ_SYNC` to pull
neighbours' tables; in **steady state** it sends dirty-only differential beacons under an adaptive channel-busy
throttle. (Gateways override this — see §5.)
- **Source:** `node_beacon.cpp` (`emit_beacon`, `periodic_beacon_fire`) · `node_query.cpp` (REQ_SYNC)
- **Spec:** `docs/specs/2026-05-29-r1-beacon-emit-design.md` · `2026-05-29-c5-bcn-design.md` · `2026-05-31-r4.3-beacon-throttle-design.md`

## 4. Routing-liveness

Reception-driven freshness: any frame heard from a neighbour stamps it fresh (`mark_dest_seen`); repeated RTS
timeouts escalate a peer suspect→silent→dead, applying a score penalty so routing avoids it; tiers gossip to peers.
A route's next-hop must be fresh to be viable (cross-layer gateway routes are exempt).
- **Source:** `node_routing.cpp` (`is_next_hop_fresh`, `record_peer_rts_timeout`, `liveness_penalty_q4`)
- **Spec:** `docs/superpowers/specs/2026-06-17-routing-liveness-plane-port.md` · `docs/specs/2026-05-31-r4.2-tier-penalty-design.md`

## 5. Gateway dual-layer

A gateway time-multiplexes two leaves: each leaf owns a window on an **absolute grid** (`epoch + k·period`); a busy
switch slips to protect an in-flight exchange but snaps back to the grid (bounded drift). It advertises a per-leaf
schedule (receiver-anchored countdown) and beacons a leaf **reactively** (dirty / `Q:REQ_SYNC` + a duty-gated
heartbeat — not on a timer). Senders defer their RTS to the advertised window + a herd-jitter spread sized from the
gateway's 1-hop herd × the airtime-computed exchange. Cross-layer DMs bridge via a layer-path; each layer may run its
own RF frequency (provisioning-only).
- **Source:** `node.cpp` (`window_switch_fire`, `window_grid_now`, `activate_layer`, `gateway_schedule_defer_ms`, `gateway_spread_nibble`, `exchange_airtime_ms`) · `node_beacon.cpp` (schedule emit, `maybe_emit_gateway_beacon`) · `node_cascade.cpp` (`gateway_doorstep_hold`)
- **Spec:** `docs/superpowers/specs/2026-06-12-gateway-dual-layer-design.md` · `2026-06-14-multihop-gateway-discovery.md` · `2026-06-19-gateway-provision-command-design.md`

## 6. Channel plane (group messages)

Leaf-scoped broadcast groups: a message rides a managed **flood** (FLOOD RTS-M + coverage bitmap, 1-hop-suppressed
re-flood) on the data SF; a BCN channel-digest + `Q:CHANNEL_PULL` are the repair backstop for misses. **Principle 11:**
a dual-layer gateway is entirely out of the channel plane (never originates, pulls, or bridges channel traffic).
- **Source:** `node_channel.cpp` (`process_channel_digest`, `channel_origin_admit`, `flood_forward_decision`)
- **Spec:** `docs/superpowers/specs/2026-06-08-channel-flood-redesign.md` · `2026-06-09-lean-channel-m-frame-design.md`

## 7. E2E DM crypto

Opt-in sealed-sender DMs: X25519 ECDH → BLAKE2b KDF → XChaCha20-Poly1305 seals `origin` + everything after it
(only `dst_key_hash32` stays cleartext as AAD). The receiver recovers the sender by **trial decryption** over its
cached peer keys; no candidate opens ⇒ silent drop. Peer keys are provisioned via the H `WANT_PUBKEY`/`REQ_PUBKEY`
mutual exchange. Optional 6-B sender location rides the sealed inner.
- **Source:** `dm_crypto.{h,cpp}` · `node_mac.cpp`/`node_mac_rx.cpp` (seal/open at enqueue/deliver) · `node_hashlocate.cpp` (pubkey resolution)
- **Spec:** `docs/superpowers/specs/2026-06-16-e2e-sealed-sender-redesign.md` · `2026-06-15-phase1-e2e-dm-crypto.md` · `2026-06-16-e2e-peer-key-provisioning.md` · `2026-06-14-location-propagation.md`

## 8. Identity / join

Node-ids are claimed by **Duplicate-Address-Detection** (listen → pick a free id → CLAIM → adopt unless DENY'd);
`key_hash32`-only tiebreak (lower wins). Reserved id bands: 0 unprovisioned, 1–16 gateways, 17–254 normal, 0xFF
sentinel (the reservation is a Join-time convention, enforced there — not at config time).
- **Source:** `node_join.cpp` (`join_start_claim`, `handle_j`)
- **Spec:** `docs/specs/2026-06-05-node-id-auto-assignment-design.md` · `docs/superpowers/specs/2026-06-19-normal-node-id-reservation-design.md` · `2026-06-15-join-e2e-phase0.md`

## 9. Hash-locate

H-frame flood resolves an identity `key_hash32` → `node_id` (soft = any cache answers; hard = owner-only) and, with
`WANT_PUBKEY`, the peer's E2E pubkey; the answer routes home as a DATA `H_ANSWER`. Relays cache answers on-pass.
- **Source:** `node_hashlocate.cpp` (`handle_h`)
- **Spec:** `docs/specs/2026-05-29-c3-h-f-floods-design.md`

## 10. Anti-spam

Self-measured airtime backstop: an originator that exceeds its per-sender airtime cap is warned (ACK `AIRTIME_WARN`
→ back off) and its over-budget originations are dropped at ingestion; per-origin/window caps bound channel flooding.
- **Source:** `node_mac_rx.cpp` (warn emit, ingestion caps) · `node_mac.cpp` (originator self-cap)
- **Spec:** `docs/specs/2026-05-31-r4.4-originator-antispam-design.md`

---

*Device-side concerns (console/`cfg`, NV blob, BLE companion, OTA, persistent inbox) are firmware integration, not
mesh protocol — see `src/fw_main.cpp` + the `docs/superpowers/specs/2026-06-10-*` / `*-inbox-*` specs.*
