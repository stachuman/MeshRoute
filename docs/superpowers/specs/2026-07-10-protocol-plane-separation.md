# MeshRoute — Protocol Plane Separation (canonical reference)

**Status:** 2026-07-10. Describes the AS-BUILT separation of the three traffic categories in MeshRoute, the wire marks that carry it, every guard that enforces it, the sanctioned cross-plane crossings, and the accepted residuals. **Intended for an independent quality-check agent** — every claim cites `file:line` (line numbers drift; grep the quoted token to re-anchor). The static-plane invariant is guarded by the `s18_meshroute.json` byte-identity tripwire (md5 `3ac88d40e00d2605ff66659f696d52bf`): all mobile/team code is `is_mobile`/`team_id`-gated and therefore inert on the static-only s18.

---

## 1. The three traffic categories

| # | category | endpoints | id namespace | routing table | reachability |
|---|----------|-----------|--------------|---------------|--------------|
| **1** | STATIC | static node ↔ static node | **global** `node_id` (DAD'd, 17..254; 1..16 gateways) | `_rt` | direct DV routes |
| **2** | STATIC ↔ MOBILE bridge | static node ↔ a registered mobile, **via the mobile's HOME** | crosses: static global ↔ mobile hash | `_rt` (to the home) | `home_id + dst_hash`; the home does the last-mile |
| **3** | MOBILE / TEAM only | mobile ↔ mobile, team member ↔ team member | mobile **home-assigned LOCAL** id · team **`_team_local_id`** | `_rt_team` (team) | team beacons/DMs/channel; off-grid |

**§18 — the three id namespaces collide numerically.** A static global id, a mobile local id, and a team local id are all `uint8_t` in the same 17..254 range and **can be equal by coincidence**. Correctness therefore never rests on a raw `id == id` compare across planes; the plane is always disambiguated by a **wire mark** or by **flight/receiver context**.

---

## 2. Separation invariants (what QA must confirm still holds)

- **A. Local-id invisibility.** A mobile local id and a team local id **never** enter a STATIC node's `_rt`, `id_bind` (global id↔hash cache), anti-spam ledger, or plaintext dedup namespace **as a global identity**.
- **B. Team containment.** Team traffic (team beacons/M-frames/channel/DMs) is never relayed, re-flooded, buffered, or delivered by a static or different-team node. `_rt_team` is a separate table from `_rt`.
- **C. Config non-membership (Option A).** A mobile is not a leaf-config-plane member: no lineage adopt, no `REQ_SYNC`/`CONFIG_PULL` broadcast; it adopts only the host PHY and runs its own/default config.
- **D. Sanctioned crossings only.** The *only* cross-plane crossings are the **home last-mile**, the **home hash-locate proxy**, and the **gateway cross-layer bridge**. Everything else stays in its plane.
- **E. No cross-namespace identity.** No code treats a raw id from one namespace as an identity in another (route dispatch, deliver/for-me, CTS/ACK/NACK match, dedup/anti-spam keys, id_bind, hash-locate answer).

---

## 3. Wire marks (the plane tags)

All default `0` → backward-compatible; all `0` on every static frame → s18 byte-identical. Codec: `lib/core/frame_codec.{h,cpp}`.

| mark | frame · bit | meaning | set by |
|------|-------------|---------|--------|
| `is_mobile` | Beacon byte-2 b5 | the beacon is a mobile/team member's | any mobile |
| team-id EXT-TLV (type 5) | Beacon ext | the sender's `team_id` | a team member |
| **`mobile_src`** | RTS byte-5 b1 | **the `src` is a LOCAL id** | a registered mobile **or** a team DM: `rin.mobile_src = pt.mobile_src \|\| team_next` (`node_mac.cpp` — grep `team_next`) |
| `addr_len==1` | RTS & DATA byte-3/byte-0 | **the `next` is a LOCAL id** | host last-mile, or `is_team_peer(pt.next)` (`node_mac.cpp`) |
| `mobile_to` | ACK byte-1 b1 · **NACK byte-1 b0** | **the `to` is a LOCAL id** | `ain.mobile_to = _pending_rx->mobile_from`; `nin.mobile_to = r.mobile_src` / `_pending_rx->mobile_from` |
| `H_FLAG_TEAM` (0x04) | H byte-7 b2 | a team-scoped locate (+`team_id`) | a team member (`emit_hash_query`) |
| **`H_FLAG_MOBILE_REQ`** (0x08) | H byte-7 b3 | the querier's `origin` is a LOCAL id | any `is_mobile` querier (`emit_hash_query`) |
| `channel_flavor_team` (0x80) | M-frame flavor | a team-scoped channel msg (+`team_id` tail) | a team member's channel post |

**Receiver-side context (no wire bit):** `PendingRx.mobile_from` (set from the RTS `mobile_src` at `node_mac_rx.cpp` — grep `prx.mobile_from`) carries "this exchange's src is a local id" to DATA/ACK/NACK time; the flight's `_pending_tx->addr_len==1 || is_team_peer(next)` identifies our own mobile/team flight for CTS/ACK/NACK.

---

## 4. Guard sites (invariant A/E — keep a LOCAL id out of the static plane)

Every place a received frame writes peer/id/route/dedup/anti-spam state was audited. The guard predicate is: **skip when the datum is a LOCAL id** = `mobile_src` (src) / `mobile_from` (DATA src) / `addr_len==1 || is_team_peer(next)` (our flight's next) / `mobile_to` (addressed to).

| site (`lib/core/…`) | writes | guard |
|---|---|---|
| `node_mac_rx.cpp` RTS-learn (grep `learn_direct_neighbor(r.src`) | `_rt` | `!r.mobile_src` |
| `node_mac_rx.cpp` RTS anti-spam (grep `track_originator_observation(r.src`) | ledger | `!(RELAY \|\| m_broadcast \|\| mobile_src)` |
| `node_query.cpp` Q-learn (grep `learn_direct_neighbor(q.src`) | `_rt` | `!q.mobile` |
| `node_mac_rx.cpp` DATA-learn (grep `learn_direct_neighbor(from`) | `_rt` | `!_pending_rx->mobile_from` |
| `node_mac_rx.cpp` CTS-learn (grep `learn_direct_neighbor(c.tx_id`) | `_rt` | `!(addr_len==1 \|\| is_team_peer(next))` |
| `node_mac_rx.cpp` ACK-learn / NACK-learn (grep `pending_tx->next, protocol::db_to_q4`) | `_rt` | `!(addr_len==1 \|\| is_team_peer(next))` |
| `node_mac_rx.cpp` CTS anti-spam (grep `own_mobile_team_cts`) | ledger | skip when it clears **our** mobile/team flight (own-exchange); overhear = documented residual §7 |
| `node_mac_rx.cpp` dedup `sokey` (grep `mobile_from ? (uint64_t(1) << 62)`) | `_seen_origins` | a local-id origin → disjoint namespace `[2^62, …)` |
| `node_beacon.cpp` beacon-src id_bind (grep `if (!b.is_mobile)`) | `id_bind` | `!b.is_mobile` |
| `node_beacon.cpp` beacon-src route-learn (grep `else if (!b.is_mobile && learn_direct_neighbor(b.src`) | `_rt` | `!b.is_mobile` (a same-team beacon → `_rt_team`) |
| `node_beacon.cpp` beacon carried-route self-entry (grep `e.dest == b.src`) | `_rt` | `!(b.is_mobile && !same_team && e.dest==b.src)` |
| `node_join.cpp` static-CLAIM defense (grep `if (_cfg.is_mobile) return;` in the claim branch) | DENY / id_bind | a mobile never defends its local id vs a static CLAIM |
| `node_hashlocate.cpp` WANT_PUBKEY id_bind (grep `if (!h.team_scoped && !h.mobile_req)`) | `id_bind` | skip for a team-scoped or `mobile_req` requester |

**Re-audit-by-plane additions (liveness / bidi / budget / blind — the sibling writes the by-mutator sweep missed; see `2026-07-10-plane-separation-addendum-reaudit.md`):** the predicate here is the FLIGHT's next-hop being a LOCAL id = `_pending_tx->addr_len==1 \|\| is_team_peer(_pending_tx->next)`.

| `node_mac_rx.cpp` DATA anti-spam (grep `track_originator_observation(_active->_pending_rx->from`) | ledger | `!_pending_rx->mobile_from` |
| `node_mac_rx.cpp` `note_link_confirmed(c.tx_id)` (CTS) | `_link_bidi[]` | `!(addr_len==1 \|\| is_team_peer(next))` |
| `node_mac_rx.cpp` `mark_neighbor_budget_tier` (ACK & NACK) | budget-tier | `!(addr_len==1 \|\| is_team_peer(next))` |
| `node_mac_rx.cpp` `_blind_until[pt.next]` (BUSY_RX **and** HOP_BUDGET/BUDGET NACK) | `_blind_until` | `!(pt.addr_len==1 \|\| is_team_peer(pt.next))` |
| `node_cascade.cpp` `record_peer_rts_timeout` (**rts_timeout_fire & ack_timeout_fire** — timer handlers, not RX) | `_peer_liveness` | `!(addr_len==1 \|\| is_team_peer(next))` |

*(Related READs — no write, not a leak, documented not fixed: `compute_originator_metric(_pending_rx->from)` `node_mac_rx.cpp:454`; `liveness_penalty_q4(_pending_tx->next)` in the timeout cascade decision `node_cascade.cpp`. A §18 hit reads a stale/foreign entry → at most a suboptimal cascade/warn, never a plane write.)*

**Deliver/for-me (invariant E):** `for_me_dst(dst)` = `dst == _node_id || (team && dst == _team_local_id)` (`node.h`) gates DATA deliver + hop-budget. ACK accept: `(mobile_to==1)==is_mobile` (`node_mac_rx.cpp` grep `k.mobile_to`). NACK accept: `(n.mobile_to==1)==is_mobile` (`handle_nack`). Route dispatch: `rt_find(dest)` dispatches to `_rt_team` when `is_team_peer(dest)` (`node_routing.cpp`).

---

## 5. Team containment (invariant B)

- **Beacon carried routes** merge into `_rt_team` for a same-team beacon, `_rt` for a static beacon; a non-team mobile beacon merges neither (`node_beacon.cpp` grep `merge_rt = same_team_beacon`).
- **Team M-frame / channel:** a static/non-team node does not participate in a `mobile_src` channel flood (`node_mac_rx.cpp` grep `!(r.mobile_src && _cfg.team_id == 0)`); `ingest_channel_m` drops a foreign `team_id` DATA-M and frees the flood-state (`node_channel.cpp` grep `_cfg.team_id != m.team_id`); a team channel post/flood/pull sets `mobile_src` (`node_channel.cpp` grep `channel_flavor_team`).
- **Team-scoped H-query** (`H_FLAG_TEAM`): a registered same-team member answers directly; others fall through to the home proxy / normal answer (`node_hashlocate.cpp`).
- **`route_uses_mobile_as_transit`** excludes a mobile as transit but allows `is_team_peer` (a teammate is a legal transit within the team plane) — `node_routing.cpp` grep `is_team_peer`.

---

## 6. Config non-membership — Option A (invariant C)

- **Membership exemption:** `node_beacon.cpp` grep `if (b.self_gateway || b.is_mobile || _cfg.is_mobile)` — a mobile receiver peers by nibble, learns routes, but never adopts a lineage or fires CONFIG_PULL.
- **No REQ_SYNC:** `send_req_sync_q` grep `if (_cfg.is_mobile) return;`.
- **Normalized unmanaged:** `on_init` grep `if (_cfg.is_mobile) { _cfg.lineage_id = 0; _cfg.config_epoch = 0; }` — a mobile is always `leaf_config_synced()` → can originate DMs.
- The `q.mobile` bit is defined but now **inert** (a mobile emits no Q); kept as a defensive marker.

---

## 7. Sanctioned crossings (invariant D — correct-by-design, VERIFIED-clean)

1. **Home last-mile** — the home resolves `dst_hash` → the mobile's current local id and sends a DIRECT `addr_len=1` DM (`node_mac_rx.cpp` bridge path). One-way into the mobile plane.
2. **Home hash-locate proxy** — the home answers `M → home_id` (`MOBILE_H_ANSWER`, type 8) and the sender caches it in a **separate `mobile_home_cache`, never `id_bind`** (`node_hashlocate.cpp` / `node.h` grep `mobile_home`).
3. **Gateway cross-layer bridge** — resolves a mobile `dst_hash` on the target leaf via the home (`node_mac_rx.cpp` grep `mobile_home_on_leaf`).
4. **Mobile → its own `_rt`** — a mobile learns STATIC routes into its OWN `_rt` to reach its home (one-way; a mobile's id never enters a static node's `_rt`).
5. **Home liveness refresh** — the home refreshes a hosted mobile's `last_heard_ms` from its beacon (`node_beacon.cpp` grep `_mobile_reg[i].key_hash32 == b.key_hash32`).
6. **Accountability** — `origin=home_id` bills the home, not the mobile local id.

---

## 8. Accepted residuals (throttle/airtime-only — NO misroute/misdeliver/id-plane pollution)

Each is documented in-code and needs a wire change (a flag-day or a new bit) not justified by its impact.

1. **CTS-overhear anti-spam ledger** — a *pure-overhear* team/mobile CTS (not clearing us) still meters a local id (`node_mac_rx.cpp` grep `own_mobile_team_cts`). The CTS flags nibble is full → no mark without a flag-day. **Throttle-only.**
2. **Foreign-team CHANNEL_PULL** — a different-team node can't team-gate a `mobile_src` RTS-M (the RTS carries no `team_id`); it tags the flood-state `team_flood` and **does not fast-pull** (`node.cpp` grep `!_active->_flood[i].team_flood`), and `ingest_channel_m` drops+frees the foreign DATA-M — so no cross-team delivery, at most a transient pull if a same-team fast-pull path existed. A full RTS-side gate needs `team_id` on the RTS-M.

*(The plaintext dedup and the non-team-mobile WANT_PUBKEY id_bind residuals from the earlier sweep are now FIXED — §4 rows `sokey` bit-62 and `H_FLAG_MOBILE_REQ`.)*

---

## 9. QA checklist

1. **Grep each §4/§5 guard token** — confirm the guard is present and matches the predicate. A missing/weakened guard on any learn/bill/id_bind/dedup site is a break.
2. **Audit BY PLANE, not by mutator** — for EACH shared id-keyed structure (`_rt`, `_rt_team`, `_id_bind`, anti-spam ledger, `_seen_origins`, `_peer_liveness`, `_link_bidi[]`, `_blind_until`, budget-tier), grep EVERY mutator and confirm the local-id guard. The recurring regression: a handler guards its route-learn but a SIBLING write (liveness/bidi/budget/blind), sometimes in a **timer handler** (not RX), is one line away and keys on the same local id. This is what the addendum re-audit caught.
3. **s18 byte-identity** — `lus -e meshroute simulation/s18_meshroute.json | md5sum` == `3ac88d40e00d2605ff66659f696d52bf`. Any drift means a mobile/team path is NOT properly gated on the static plane.
4. **Marks default 0** — confirm every mark packs `0` for a static node (the source of the s18 invariant).
5. **§18 collision traces** — for each of {RTS-learn, DATA-learn, CTS/ACK/NACK match, dedup, id_bind, deliver, liveness/bidi/budget/blind}, construct a trace where a team/mobile local id numerically equals a static global id and confirm no misroute/misdeliver/mis-bill/mis-suspect.
6. **Residual scope** — confirm the §8 residuals are still throttle/airtime-only (no path escalates them to a delivery/routing decision).

### §9.5 — the realized §18-collision test (must have teeth)
`test/test_node_r3.cpp` grep `§18 — a TEAM flight's LOCAL-id next-hop` — a team flight to a LOCAL id that numerically equals a static neighbour, timed out; asserts the static `_peer_liveness` is untouched, with a **positive control** (a static flight DOES suspect its global next) and a **falsifiability** note (reverting the guard makes it FAIL — verified). QA should extend it to `_link_bidi` / `_blind_until` / budget / dedup / ACK-NACK-consume collisions.

## 10. Gates (as of this doc)
native **676/676** · s18 byte-identical `3ac88d40…` · s07/s21/s09/s15 **0 assertion failures** · 4 boards SUCCESS. Regression tests: team-DM `mobile_src`, mobile-originator ACK acceptance, mobile-not-static-CLAIM, mobile-not-config-member, mobile-marked-Q-not-learned, H `mobile_req`/NACK `mobile_to` codec round-trip, **§18 liveness-collision (falsifiability-verified)** (`test/test_node_r3.cpp`, `test/test_dual_layer.cpp`).
