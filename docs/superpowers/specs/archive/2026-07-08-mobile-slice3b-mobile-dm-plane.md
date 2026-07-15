<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 3b: mobile DM plane (receive marked last-mile + outbound) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **Slice 3b of mobile v1** (design §13/§14/§17 A1/§18; builds on 2a/2b/3a). The user commits; I quality-gate. **This closes reachability:** the mobile *receives* the `addr_len=1` last-mile (and a colliding static id does not), *sends* outbound via its home_node stamped `origin=home_id`, and the host keeps the mobile's local-id out of the global rt (§17 A1). Includes the **local-id-collision test** (§18). **Teams / plane-selection = Slice 6.**

## ★★ NON-NEGOTIABLE: the static mesh must be UNAFFECTED
Every change is a no-op for a static node with only static neighbours:
- The receive mark-check reduces to the current check when `addr_len==0` on a non-mobile node (`(false)==(false)` → unchanged).
- The `learn_direct_neighbor` skip and the anti-spam mobile-keying are gated on `r.mobile_src` (0 on every static frame).
- The outbound `origin`/`next`/`mobile_src` changes are gated on `_cfg.is_mobile` / `_my_mobile_reg.active`.
- **Gate:** native green + **s18 fresh-md5 byte-identical** (`3ac88d40e00d2605ff66659f696d52bf`, re-establish pre-slice).

## Fix 1 — receive the marked last-mile (the mark disambiguates a colliding id)
A mobile-marked frame carries the mobile's **local** id in a receiver-addressing field. The acceptance test everywhere becomes **"my id AND the mark matches my kind":**
```
addressed-to-me  ==  (field == _node_id) && ((mark == 1) == _cfg.is_mobile)
```
i.e. a **mobile** accepts iff `mark==1`; a **static** node accepts iff `mark==0`. This makes a static node with a global id == the mobile's local id **ignore** the last-mile, and a mobile ignore a global-addressed frame that merely matches its local id.
- **RTS** (`node_mac_rx.cpp:138`) — the "addressed vs overheard" gate. Today:
  ```cpp
  if (r.next != _node_id) { /* overheard */ }
  ```
  →
  ```cpp
  if (r.next != _node_id || ((r.addr_len == 1) != _cfg.is_mobile)) { /* overheard */ }
  ```
  (also update the `"addressed"` telemetry bool at :133 to the same expression.) `r.addr_len` is the Slice-1 parsed field.
- **ACK** — the ack's `to`-is-me check (find the `a.to == _node_id` acceptance in the ACK handler): AND in `((a.mobile_to == 1) == _cfg.is_mobile)`. `mobile_to` is the Slice-1 ACK byte-1 mark; it is set on an ack **to** a mobile (the mobile's outbound flow).
- **DATA + CTS follow by flight-context, NOT an independent id-check** — a DATA/CTS belongs to the pending flight opened by the (now mark-correct) RTS (`_active->_pending_rx`/`_pending_tx`), and §14 sealed **CTS-by-context** (no CTS mark). ⚠ VERIFY the DATA-accept path keys on the pending-flight context; if it has a standalone `next == _node_id` accept, apply the same mark expression there.

## Fix 2 — §17 A1: keep the mobile's outbound local-id OUT of the global rt/anti-spam
A mobile's outbound RTS has `mobile_src=1` and `src = the mobile's local id` (which can collide a global id). The host must NOT treat it as a global neighbour:
- **`node_mac_rx.cpp:44`** — the neighbour learn:
  ```cpp
  if (learn_direct_neighbor(r.src, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
  ```
  →
  ```cpp
  if (!r.mobile_src && learn_direct_neighbor(r.src, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
  ```
  **This is the load-bearing collision fix** (§18): without it, `rt_find(20)` later resolves to the mobile and a mobile's E2E-ACK to a colliding global id 20 loops back.
- **`node_mac_rx.cpp:41`** — the anti-spam src-observation (`track_originator_observation(r.src, …)`): a `mobile_src` RTS's `src` is a **local** id, not a global identity — key it on the **mobile context** (or skip the src-observation for `mobile_src`; the origin-billing still applies because the mobile stamped `origin=home_id`, Fix 4, and `channel_origin_admit` bills the origin). Follow §17 A1: "keys anti-spam/dedup/CTS `rx_id` on the (mobile, local-id) context." Minimal correct form: **guard the src-keyed learn/track on `!r.mobile_src`**; the mobile's accountability rides `origin`.

## Fix 3 — mobile outbound: route via home_node + self-mark
- **Next-hop = the home_node.** In `issue_send` (node_mac.cpp:525, alongside the 3a Fix-4 `addr_len==1→direct`):
  ```cpp
  uint8_t first;
  if (pt.addr_len == 1)                                   first = pt.dst;                   // 3a: last-mile → direct
  else if (_cfg.is_mobile && _my_mobile_reg.active)        first = _my_mobile_reg.home_id;   // 3b: a mobile is a leaf → send everything via its registrar
  else                                                     first = pick_next_cascade_hop(pt);
  ```
  (A registered mobile has no global route table — it reaches the mesh through its 1-hop home_node. Team-member routing on the mobile plane = Slice 6, §18 P1.)
- **Set `mobile_src` on originate.** At the enqueue sites (§Fix 4 lists them), for a registered mobile set `item.mobile_src = true;` so the outbound RTS self-marks (drives Fix 2 at the host). A host forward keeps `mobile_src=false` (3a).

## Fix 4 — `origin = home_id` on a mobile's outbound (§13, deferred from 2b)
At the **5** originate sites, stamp the registrar so the mesh bills an accountable global node (the mobile's E2E identity still rides `sender_hash`):
```cpp
    item.origin = (_cfg.is_mobile && _my_mobile_reg.active) ? _my_mobile_reg.home_id : _node_id;
```
Sites: `node_mac.cpp:64` (`enqueue_data`), `node_mac.cpp:231` (gateway DM), `node_mac.cpp:351` (XL e2e-ack), `node_channel.cpp:569` (`enqueue_channel_m`), `node_channel.cpp:706` (`enqueue_flood_m`). Static / unregistered → `_node_id` (unchanged).

## ★ Fix 5 — the local-id-collision test (§18, the reason for this slice's discipline)
Add a node-level test: a **mobile** with `_my_mobile_reg = {active, home_id=H, my_local_id=20}` receives a DM (E2E_ACK_REQ) **from an origin whose GLOBAL id is 20**; it builds + sends the E2E-ACK. Assert:
1. the ack's outbound RTS goes to `next == H` (the home_node), `src == 20` (mobile), `mobile_src == 1`, `origin == H`;
2. at the host, that RTS does **NOT** call `learn_direct_neighbor` (no rt entry for id 20 → the mobile);
3. the ack's DATA `dst_hash == the origin's hash` (hash-addressed) so it resolves to the **global** node 20, not the mobile — **no loop, no self-drop**.

## Tests
- **Receive:** a mobile (`is_mobile`, `_node_id=20`) accepts an RTS with `next=20, addr_len=1`; a **static** node (`_node_id=20`) treats the same RTS as **overheard** (not addressed). The mobile treats a `next=20, addr_len=0` RTS as **overheard** (a global frame, not for it).
- **A1:** an RTS with `mobile_src=1` does **not** create a `learn_direct_neighbor` rt entry; a normal RTS still does.
- **Outbound:** a registered mobile's `enqueue_data` → `origin==home_id`, `mobile_src==true`, and `issue_send` picks `next==home_id`.
- **Fix 5** (above).
- **★ Static regression:** a static node with static neighbours — RTS/DATA/ACK acceptance, neighbour-learning, origin stamping all **unchanged** (the mark expressions collapse to the originals).

## Gate
- `pio test -e native` green (receive + A1 + outbound + collision + static-regression) — via `./.pio/build/native/program`.
- **s18 fresh-md5 byte-identical** (`3ac88d40…`, re-establish pre-slice) — the static tripwire.
- **s07 — a full DM to a mobile now DELIVERS end-to-end** (3a locate/forward + 3b receive): assert a `msg_recv` at a mobile and, for an E2E_ACK_REQ DM, an `e2e_ack_rx` back at the origin. This is the reachability milestone.
- 4 boards compile.

## Sites
`node_mac_rx.cpp`(RTS accept+telemetry ~133/138; `learn_direct_neighbor` skip ~44; src-observation ~41; ACK `to`-accept) · `node_mac.cpp`(`issue_send` ~525 mobile next-hop; `enqueue_data` origin+mobile_src ~64; gateway ~231; XL ack ~351) · `node_channel.cpp`(~569, ~706 origin) · tests. **NO team/plane-selection (Slice 6, §18 P1/P2), NO H-answer epoch (Slice 4).**
