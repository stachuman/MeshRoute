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

**Sender-CR advertisement — sizing the receiver's DATA wait (2026-07-27).** After CTSing an RTS the receiver arms
a DATA-wait window (`start_pending_rx_expiry`) and abandons the flight when it expires. That window is
`CTS airtime + cts_to_data_gap + DATA airtime + margin`, and the DATA airtime depends on the **sender's** coding
rate — CR multiplies the payload-symbol count directly. It used to be computed from `active_cr()`, the
**receiver's own** CR, which is correct only while every node shares one CR.

A gateway does not: each layer may carry its own CR, so a gateway transmitting on a CR4/8 layer talks to leaves at
CR4/5. **CR is auto-detected at the LoRa PHY** — the explicit header carries it, so such a frame demodulates
perfectly — but the firmware's *clock* was wrong: the receiver armed for its own lighter CR, timed out, and
dropped a frame still on the air. Measured in `s32_dual_cr_gateway` (SF11 / BW 250 kHz, `payload_len` 38): the
receiver armed **607 ms** for a DATA needing **853 ms** — a **246 ms shortfall** — abandoning it 130 ms before it
landed. Both gateways were affected **symmetrically**, giving 26 `data_rx_timeout`s, an exhausted path cascade and
zero cross-layer joiner delivery.

⇒ **mixed-CR links were interoperable at the PHY but not in this firmware's RX-window sizing.** The fix: the RTS
states the sender's CR in two previously-reserved bits (`cr_adv`, no length change and no `wire_version` bump —
layout in `frames.md`), the receiver stashes it on its `PendingRx`, and the DATA term is computed from *that*.
s32's 26 timeouts go to 0, both cross-layer joiner DMs deliver, and the retry churn disappears (3015 → 2266
events).

**Sender-CR sizing completed for the overhearer (2026-07-27, `§rts-cr-overhear`).** The advert is now consumed
everywhere the datum exists, not only by the addressed receiver:
- the two **channel-overhear retune** windows (`FLOOD` and `M_BROADCAST` RTS-M) — an overhearer at CR4/5 used to
  retune off the data SF before a CR4/8 sender's M-frame finished, and lose it to `drop_sf_mismatch`;
- the **NAV reservation taken from an overheard RTS**, which reserves for that sender's own DATA;
- the **anti-spam airtime ledger**'s RTS and DATA terms. This one is an *accountability* fix, not a timing one:
  the ledger answers *"how much airtime did this sender impose on us"*, so costing a peer's frame at our own rate
  **under-billed** every heavier-coded sender against `originator_airtime_share`. ⚠ Its RTS term reached
  `active_cr()` **indirectly, through `airtime_routing_ms()`** — invisible to a grep for `active_cr()`.

Measured in `s33_mixed_cr_channel_overhear` (cr8 gateway layers flooding to cr5 leaves): `channel_msg_received`
**26 → 35** and `channel_msg_overheard` **17 → 26**, so the nine frames the overhearers now catch remove nine
repair pulls (`channel_pull_sent` **10 → 1**) and the contention they cost (`tx_lbt_defer` 17 → 7, `collision`
8 → 4). Net cost **+1.0 % on-air for +35 % delivery** — 911 → 683 ms per delivered message. In
`s32_dual_cr_gateway` the ledger fix shows as the two cr8 senders' `orig_airtime_ms` **rising** (600 → 821,
320 → 443) while the cr5 senders' are untouched. ⚠ Only the FLOOD retune and the ledger are corpus-*validated*;
the `M_BROADCAST` retune and the NAV-from-RTS site execute constantly but only ever with cr5 senders, so native
tests are their sole regression detectors.

**Deliberately deferred — the honest remainder:**
- The **ACK wait** (`start_ack_timeout`) still uses `active_cr()`. Its dominant term is our *own* DATA, where that
  is correct; only the small `airtime_routing_ms(3)` ACK term is the peer's. In the gateway-high-CR shape a
  light-CR node awaiting a heavy-CR gateway's ACK **over**-waits, which fails safe.
- ★ The **NAV reservation taken from an overheard CTS** (`nav_duration_cts`'s two `handle_cts` callers) is the one
  site of this class that **cannot** be closed. It must size the DATA that CTS is clearing — a third party's frame
  at that party's CR — and the CTS has **no bit to carry one**: its flags nibble is fully spent on `(sf−5)` +
  `already_received`, `tx_id`/`rx_id`/`payload_len` are whole bytes, and `parse_cts` accepts exactly 3 or 4 B.
  ⚠ Unlike the ACK wait, this error runs in the **dangerous** direction: against a heavier-CR peer the overhearer
  **under**-reserves and releases NAV while the DATA is still on the air — the hidden-terminal collision NAV
  exists to prevent, failing in the one case it is for. Measured (cr5 overhearer, cr8 peer, BW 250 kHz):
  SF11 / `payload_len` 38 reserves **690 ms for a 935 ms frame — a 245 ms hole**, the same order as the 246 ms
  DATA-wait shortfall above; SF12 / 120 B reaches **−1327 ms**. The reverse case merely over-reserves. Bounded but
  unfixed: LBT still backstops physically, and the `payload_len == 0` branch reserves for a 255 B frame — but a
  NAV-enabled CTS *does* carry `payload_len`, so the common case is exact and fully exposed. Closing it costs a
  5th conditional CTS byte (a flag day, taxing every CTS in every mesh) or reclaiming the load-bearing
  `already_received`.
- The **CTS term of the anti-spam ledger** is billed at our own CR for the same reason — a fixed 4 B on the
  routing SF, single-digit ms against a DATA term of hundreds, same under-billing direction.

**Fleet assumption.** The 2-bit code is total over CR 5..8, so all-zero bits mean **CR4/5**, not "unknown". Every
board env compiles `-DLORA_CR=5` and `radio_cr` defaults to 5, so an un-reflashed node genuinely *is* at CR4/5 and
a new receiver decodes it correctly. The hazard is reachable only if the fleet's **global** CR is moved off 5
while un-reflashed nodes remain — do that only behind a `wire_version` bump.
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
**Plane-keyed coverage (§S7):** the SAME flood coverage/re-flood/backoff code serves both planes — a team-scoped
flood's coverage consults the TEAM peer set, a static flood the static rt (+ the home's hosted mobiles, so
registered mobiles RECEIVE leaf channels, receiver-only — they never re-flood; off-grid mobiles receive none).
`send_channel`: `-t` = team · plain/`-g` = GLOBAL (a registered mobile delegates to its home, which mints) ·
`-t -g` = both. ⚠ a team mobile's plain send flipped team→global (D17, DM symmetry).
- **Source:** `node_channel.cpp` (`process_channel_digest`, `channel_origin_admit`, `flood_forward_decision`)
- **Spec:** `docs/superpowers/specs/2026-06-08-channel-flood-redesign.md` · `2026-06-09-lean-channel-m-frame-design.md`

## 8. E2E DM crypto

Opt-in sealed-sender DMs: X25519 ECDH → BLAKE2b KDF → XChaCha20-Poly1305 seals `origin` + everything after it
(only `dst_key_hash32` stays cleartext as AAD). The receiver recovers the sender by **trial decryption** over its
cached peer keys; no candidate opens ⇒ silent drop. Peer keys are provisioned via the H `WANT_PUBKEY`/`REQ_PUBKEY`
mutual exchange; for a **registered mobile** the mutual half is home-mediated (§S3): the home answering on the
mobile's behalf also caches the requester's attached key and last-miles it to the mobile
(`MOBILE_KEY_FORWARD`), and the mobile TX-free-caches a requester key it overhears for its own hash — so a
sealed DM to a mobile opens. **First contact needs no key round at all (§S2 INTRO):** a plaintext
hash-addressed DM to a peer whose custody is unconfirmed auto-attaches the sender's pubkey (+33 B once);
the reply attaches back by the same rule ⇒ mutual keys in one round trip, then sealed traffic sets
`peer_confirmed` and attaching stops. `cfg set intro_attach` opts out; `-K` suppresses one send.
**Cross-layer + delegated sealed DMs ride `SEALED_RELAY` (§S4):** the sealer seals same-layer-shaped under a
CARRIED `seal_ctr` (so a delegating home re-originates under its own frame ctr without re-sealing) and the
recipient does a DIRECTED open keyed by the clear `SOURCE_HASH` (sealed-vs-clear cross-checked). ⚠ these are
sealed-CONTENT but attributable-envelope; sealed-SENDER privacy (trial-decrypt, origin hidden) holds only for
same-layer direct sealed DMs. XL E2E-acks are cleartext, parity with same-layer acks. Optional 6-B sender
location rides the sealed inner.
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

**Switching teams (`§clean-team`, 2026-07-27).** Every live `team_id` change — `team new`, `team <id>`, `team 0`
and `cfg set team_id` — goes through **`Node::set_team_id()`**, which drops the whole team plane before the new id
takes effect: `_rt_team`/`_team_peer`, the team liveness mirror, the **team key cache `_team_keys`**, the team RREQ
dedup/rate rings, and `_team_local_id`. It returns *switched*, so a same-team no-op clears nothing and skips the
re-DAD. ⚠ **The static plane is deliberately untouched** — a homed mobile changing team keeps its routes,
id-bindings, gateway schedules, hosted mobiles and home registration; only `join`/`create`/`leave` wipe both
(`clear_routing_state()`, which calls the same helper). **Boot assigns `team_id` directly and must not clear** —
nothing is stale at boot, and the persisted DAD id is loaded immediately after.
Why it matters: `rt_find` dispatches on `is_team_peer` with **no `_rt` fallback**, so a stale team peer does not
merely misroute — it **shadows the static plane**. And a stale `_team_keys` entry is worse than a stale route: it
maps a reused team-local id to the *previous* team member's `key_hash32`, i.e. **mis-addressing** (wrong DST_HASH
at `node_mac.cpp:94`; hash → wrong teammate at `node_hashlocate.cpp:988/1019`). `is_team_peer` does not guard it —
the bit is set from a multi-hop DV entry that carries no key.
**Residual, not yet fixed:** team-flavored channel-buffer rows are not purged, and the M emit re-stamps them with
the *current* `team_id`, so an old team's buffered message can still be re-broadcast into the new team.

A `team_id`-scoped overlay of mobiles (member-to-member routing + group chat), fully **separated** from the
static plane and from other teams: team beacons/DV (`_rt_team`), team F discovery, a team-scoped H-flood, and a
`team_id`-tagged channel — a static node never learns, relays, or ingests any of it (the s18 byte-identity
tripwire + s22/s24/s25 assert both axes). Members self-assign `team_local_id`s by team-DAD (no host needed —
an off-grid team routes among itself); a DUAL member (also home-registered) keeps the two id spaces distinct.
Reaching a teammate REQUIRES the team plane (`send -t`); a plain send is global/home.
**Leaf-agnostic (P2-1, 2026-07-20):** team membership is `team_id`, never `leaf_id` — a team's `create` defines
the PHY all members share, and members homed onto DIFFERENT layers (mixed leaf nibbles) stay team-reachable:
every team-scoped RX path (beacon, H, F, RTS/MAC, channel-M) accepts a same-team frame despite a foreign leaf
(`Node::same_team()` is the ONE predicate; statics keep the plain leaf gate → byte-identical). Corollary
(ruled option (a)): a TEAM member **refuses to home** onto a layer whose PHY (freq/bw/routing-sf/cr) differs
from its team-provisioned `layers[0]` — `mobile_home_phy_mismatch`, stays off-grid-but-team-reachable;
cross-LAYER same-PHY re-home is the supported case. (Residual: the explicit `mobile register freq=/bw=`
console path bypasses the guard — operator's deliberate choice.)
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
