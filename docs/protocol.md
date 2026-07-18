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
- **Mobile marks (codec — §mobile Slice 1).** A mobile uses a home-assigned LOCAL id that can collide with a global id, so **RTS/DATA carry `addr_len=1`** (`next` is a mobile local-id), **RTS a `MOBILE` bit** (byte-5 b1 — the `src`/originator is a mobile), and **ACK a `MOBILE` bit** (byte-1 b1 — the `to` is a mobile local-id); **CTS relies on the marked-RTS context**. These keep the mobile plane's local-ids distinct from global ids. The codec round-trips them (marks default `0` → backward-compatible); wire layout = `frames.md`. The mobile plane itself (registration, last-mile, presence) is **§12–14** below.

## 3. Beacons

In **discovery** (first ~60 s, or route-starved) a node beacons fast + full-page and broadcasts `Q:REQ_SYNC` to pull
neighbours' tables; in **steady state** it sends dirty-only differential beacons under an adaptive channel-busy
throttle. (Gateways override this — see §6.)
- **Source:** `node_beacon.cpp` (`emit_beacon`, `periodic_beacon_fire`) · `node_query.cpp` (REQ_SYNC)
- **Spec:** `docs/specs/2026-05-29-r1-beacon-emit-design.md` · `2026-05-29-c5-bcn-design.md` · `2026-05-31-r4.3-beacon-throttle-design.md`

## 4. Routing-liveness

Reception-driven freshness: any frame heard from a neighbour stamps it fresh (`mark_dest_seen`); repeated RTS
timeouts escalate a peer suspect→silent→dead, applying a score penalty so routing avoids it; tiers gossip to peers.
A route's next-hop must be fresh to be viable (cross-layer gateway routes are exempt).
- **Source:** `node_routing.cpp` (`is_next_hop_fresh`, `record_peer_rts_timeout`, `liveness_penalty_q4`)
- **Spec:** `docs/superpowers/specs/2026-06-17-routing-liveness-plane-port.md` · `docs/specs/2026-05-31-r4.2-tier-penalty-design.md`

## 5. Asymmetric-link bidirectionality

A per-next-hop plane **orthogonal to liveness**: it scores down a link that is **one-way** — we hear a neighbour but it can't hear us, so the RTS→CTS→DATA→ACK handshake can never complete (liveness is beacon-cleared, hence blind to this). Detection is **proactive gossip** — a node advertises its complete `hops==1` heard-set (its direct-neighbour route entries) under a beacon `heard_set_complete` flag; a receiver absent from a *complete* heard-set learns the link is one-way, present ⇒ confirmed bidirectional (a real CTS also confirms). The verdict adds a **sort-only** penalty (never a `next_hop_selectable` hard gate — a sole one-way route still flies) and rides a transitive `degraded` route-entry bit so the mesh routes around it; a doomed sole route slow-re-probes once per TTL instead of RTS-storming, and recovers for free when the link flips back. Keep-don't-delete + backward-compatible (both wire bits default 0). Gateways skip the census (leaf-only — the leaf→gateway direction is liveness-backstopped).
- **Source:** `node_routing.cpp` (`bidi_penalty_q4`, `candidate_degraded`, `note_link_confirmed`) · `node_beacon.cpp` (`update_link_bidi_from_beacon`, the census in `emit_beacon`) · `node_cascade.cpp` (the one-way slow-reprobe)
- **Spec:** `docs/superpowers/specs/2026-06-29-asymmetric-link-aware-routing-design.md`

## 6. Gateway dual-layer

A gateway time-multiplexes two leaves: each leaf owns a window on an **absolute grid** (`epoch + k·period`); a busy
switch slips to protect an in-flight exchange but snaps back to the grid (bounded drift). It advertises a per-leaf
schedule (receiver-anchored countdown) and beacons a leaf **reactively** (dirty / `Q:REQ_SYNC` + a duty-gated
heartbeat — not on a timer). Senders defer their RTS to the advertised window + a herd-jitter spread sized from the
gateway's 1-hop herd × the airtime-computed exchange. Cross-layer DMs bridge via a layer-path; each layer may run its
own RF frequency (provisioning-only).
- **Source:** `node.cpp` (`window_switch_fire`, `window_grid_now`, `activate_layer`, `gateway_schedule_defer_ms`, `gateway_spread_nibble`, `exchange_airtime_ms`) · `node_beacon.cpp` (schedule emit, `maybe_emit_gateway_beacon`) · `node_cascade.cpp` (`gateway_doorstep_hold`)
- **Spec:** `docs/superpowers/specs/2026-06-12-gateway-dual-layer-design.md` · `2026-06-14-multihop-gateway-discovery.md` · `2026-06-19-gateway-provision-command-design.md`

## 7. Channel plane (group messages)

Leaf-scoped broadcast groups: a message rides a managed **flood** (FLOOD RTS-M + coverage bitmap, 1-hop-suppressed
re-flood) on the data SF; a BCN channel-digest + `Q:CHANNEL_PULL` are the repair backstop for misses. **Principle 11:**
a dual-layer gateway is entirely out of the channel plane (never originates, pulls, or bridges channel traffic).
- **Source:** `node_channel.cpp` (`process_channel_digest`, `channel_origin_admit`, `flood_forward_decision`)
- **Spec:** `docs/superpowers/specs/2026-06-08-channel-flood-redesign.md` · `2026-06-09-lean-channel-m-frame-design.md`

## 8. E2E DM crypto

Opt-in sealed-sender DMs: X25519 ECDH → BLAKE2b KDF → XChaCha20-Poly1305 seals `origin` + everything after it
(only `dst_key_hash32` stays cleartext as AAD). The receiver recovers the sender by **trial decryption** over its
cached peer keys; no candidate opens ⇒ silent drop. Peer keys are provisioned via the H `WANT_PUBKEY`/`REQ_PUBKEY`
mutual exchange. Optional 6-B sender location rides the sealed inner.
- **Source:** `dm_crypto.{h,cpp}` · `node_mac.cpp`/`node_mac_rx.cpp` (seal/open at enqueue/deliver) · `node_hashlocate.cpp` (pubkey resolution)
- **Spec:** `docs/superpowers/specs/2026-06-16-e2e-sealed-sender-redesign.md` · `2026-06-15-phase1-e2e-dm-crypto.md` · `2026-06-16-e2e-peer-key-provisioning.md` · `2026-06-14-location-propagation.md`

## 9. Identity / join

Node-ids are claimed by **Duplicate-Address-Detection** (listen → pick a free id → CLAIM → adopt unless DENY'd);
`key_hash32`-only tiebreak (lower wins). Reserved id bands: 0 unprovisioned, 1–16 gateways, 17–254 normal, 0xFF
sentinel (the reservation is a Join-time convention, enforced there — not at config time).
- **Source:** `node_join.cpp` (`join_start_claim`, `handle_j`)
- **Spec:** `docs/specs/2026-06-05-node-id-auto-assignment-design.md` · `docs/superpowers/specs/2026-06-19-normal-node-id-reservation-design.md` · `2026-06-15-join-e2e-phase0.md`

## 10. Hash-locate

H-frame flood resolves an identity `key_hash32` → `node_id` (soft = any cache answers; hard = owner-only) and, with
`WANT_PUBKEY`, the peer's E2E pubkey; the answer routes home as a DATA `H_ANSWER`. Relays cache answers on-pass.

**Mobile locate (§mobile §4).** A mobile has no global id — it's addressed by its stable `key_hash32`. Its **registrar (home_node) proxies** that hash: on an H-query for `M` the home answers a **`MOBILE_H_ANSWER (M → home_id)`** (a distinct DATA TYPE, always *claimed*). The sender caches `M → home` in a **separate mobile-home cache — NOT `id_bind`** (the mobile's LOCAL id must stay out of the global id-plane), keyed by the registration **epoch** so the **freshest** home wins during an old+new-home overlap. A DM to the mobile then routes `dst=home, dst_hash=M` and the home does the last mile (the `addr_len=1` mark). **Staleness (three overlapping heals, §S6):** the **NEW home** sends the `MOBILE_BREADCRUMB` to the old home on adopt (home-originated since S6 — survives a sleeping mobile; same-layer today), the old home thereafter **redirecting** (`MOBILE_H_ANSWER (M → new_home)`); the mobile's next **P-probe** naming its selected home makes any OTHER registry holding its hash **prune instantly** (§14); the liveness prune is the backstop. Spec `2026-07-08-mobile-slice4a/4b` + `2026-07-17-…-first-contact` §S6.4-D.
- **Source:** `node_hashlocate.cpp` (`handle_h`, the proxy + `on_mobile_hash_bind_response`) · `node_join.cpp` (`presence_notify_old_home`)
- **Spec:** `docs/specs/2026-05-29-c3-h-f-floods-design.md`

## 11. Anti-spam

Airtime *fairness* layered on the duty-cycle governor: the duty plane bounds each node's **volume**, anti-spam adds **fairness** (no origin hogs the shared air) + **smoothness** (burst floors). Two planes, split by what a relay can see:
- **Channel (group) messages** — a per-**cleartext-origin** cap = a fair share of the leaf's duty-bounded channel capacity `C = D/T_ch` among the active originators (SF- and mesh-aware, `∝ 1/N`), plus a 10-s spacing floor. Enforced at the receiver (drop the over-cap re-broadcast) and self-applied at origination. A dual-layer **gateway is exempt** — it bridges, never originates.
- **Direct messages** — the plain duty budget + a per-**physical-sender** measured-airtime backstop at each relay (keys on the immediate sender, never the **sealed** e2e origin — so it works without seeing who a DM is from), plus a 3-s self-spacing floor. **E2E delivery-acks are exempt** from the backstop (throttling an ack is self-defeating), guarded by a verify-at-DATA anti-spoof.
- **Companion** — an advisory `limits` query ("next send in N s" + live caps) and send-outcome feedback (`send_blocked` / `send_failed` / `channel_sent`) so the app paces itself.
- **Tunables** (per-leaf, on the C frame): `channel_active_fraction`, `channel_min_interval_ms`, `dm_min_interval_ms`.
- **Source:** `node_routing.cpp` (`channel_cap_origin`) · `node_channel.cpp` (channel admit + self-gate) · `node_mac_rx.cpp` (DM backstop + e2e-ack exempt/anti-spoof) · `node_mac.cpp` (DM floor)
- **Spec:** `docs/superpowers/specs/2026-06-30-antispam-duty-channel-cap.md` · **user guide:** `docs/anti-spam.md`

## 12. Mobile node (home / care-of)

A **mobile** is a roaming endpoint: a stable `key_hash32`, a home-assigned LOCAL id, reachable by hash — the
mesh resolves hash → its current **home** (§10 proxy), routes the DM to the home, and the home does the
`addr_len=1` last mile. A registered mobile never floods on the static plane itself: hash-addressed sends are
**delegated** to the home (`DATA_TYPE_MOBILE_SEND` wrapper), which re-originates with `SOURCE_HASH`=M so replies
and E2E-acks route back (the delegated ctr map translates them). Registration rides the J family (leaf-exempt
DISCOVER + targeted, jitter-stashed OFFER + CLAIM); a reprovision (`join`/`create`/`leave`) wipes the host's
registry + suspends OFFERs and rosters while `_node_id == 0`.
- **Source:** `node_mobile.cpp` (the FSM) · `node_join.cpp` (host side) · `node_hashlocate.cpp` (delegation, `:866`) · `node_mac_rx.cpp` (`MOBILE_SEND` unwrap, last-mile)
- **Spec:** `docs/superpowers/specs/2026-07-07-mobile-node-handling-assumptions.md` · `2026-07-17-cross-layer-mobile-first-contact-design.md`

## 13. Team plane

A `team_id`-scoped overlay of mobiles (member-to-member routing + group chat), fully **separated** from the
static plane and from other teams: team beacons/DV (`_rt_team`), team F discovery, a team-scoped H-flood, and a
`team_id`-tagged channel — a static node never learns, relays, or ingests any of it (the s18 byte-identity
tripwire + s22/s24/s25 assert both axes). Members self-assign `team_local_id`s by team-DAD (no host needed —
an off-grid team routes among itself); a DUAL member (also home-registered) keeps the two id spaces distinct.
Reaching a teammate REQUIRES the team plane (`send -t`); a plain send is global/home.
- **Source:** `node_beacon.cpp` (team beacon/DV) · `node_route_discovery.cpp` (team F) · `node_hashlocate.cpp` (team H) · `node_channel.cpp` (team M) · `node_mobile.cpp` (team-DAD)
- **Spec:** `docs/superpowers/specs/2026-07-15-team-plane-routing-parity-design.md` · `2026-07-16-team-plane-liveness-2c-design.md` · `2026-07-10-protocol-plane-separation.md`

## 14. Presence plane (mobile ↔ home)

Replaces every periodic mobile↔home poll (the 10-min re-CLAIM keepalive, the 10-min layer-directory pull, the
pubkey push — all retired) with two **leaf-free, unacked 1-hop broadcasts**: a mobile's tiny **P-probe**
(quality-adaptive period, jittered; carries its selected-home pair, optionally its last home + its pubkey =
key custody) and the home's coalesced **P-roster** (its hosted list + per-mobile 2-bit link quality + `has_key`
+ the layer-directory `dir_epoch`; one roster refreshes every listener). Semantics: absent-from-roster ⇒
re-register now; probe naming ANOTHER home ⇒ that registry prunes instantly; unanswered probes ⇒ home lost in
minutes (not the old 30); weak quality ⇒ pre-scan and **proactively re-home** (bottleneck-direction ranking,
dwell + hysteresis) while both homes are alive — the new home then notifies the old (§10 staleness).
- **Source:** `node_mobile.cpp` (`presence_probe_fire`, `presence_ingest_roster`, `presence_maybe_rehome`) · `node_join.cpp` (`presence_ingest_probe`, `presence_emit_roster`) · `frame_codec.cpp` (`pack/parse_p_*`)
- **Spec:** `docs/superpowers/specs/2026-07-17-cross-layer-mobile-first-contact-design.md` §S6 (wire = `frames.md` §P)

---

*Device-side concerns (console/`cfg`, NV blob, BLE companion, OTA, persistent inbox) are firmware integration, not
mesh protocol — see `src/fw_main.cpp` + the `docs/superpowers/specs/2026-06-10-*` / `*-inbox-*` specs.*
