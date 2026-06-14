# Multi-hop Gateway Discovery (`bridged_layers`) — Coder Instruction

**Date:** 2026-06-14 · **Status:** INSTRUCTION for the coding agent · quality-gated after.
**Decision (user, 2026-06-14):** implement **(B) the real Lua-faithful mechanism** — *not* the v1 "scan rt for `is_gateway`" shortcut. Reason: **one leaf can host several gateways, each bridging to a different layer**, so "is a gateway" is not enough — the originator must know *which* gateway bridges *which* layer, and that knowledge must propagate multi-hop.

This fixes ONE thing: **the originator's gateway selection**. The cross-layer message format, the multi-hop forwarding, the bridge, and the schedule-defer are all correct — leave them alone.

---

## 0. The bug (confirmed against the Lua + the C++ tree)

The cross-layer path has three sub-mechanisms. Two are correct; one half is missing:

| Sub-mechanism | Where | Status |
|---|---|---|
| Multi-hop forwarding of the CROSS_LAYER DATA | bridge gate `!is_forward` (`node_mac_rx.cpp:502`) — relays forward toward G | ✅ correct |
| Schedule-defer (hold for the window) | `issue_send` keyed on **next-hop** (`node_mac.cpp:406`, `gateway_schedule_defer_ms(first)`) — fires at the gateway's **direct neighbor**, which has the 1-hop `_gw_schedules` | ✅ correct — DO NOT TOUCH |
| Gateway **selection** at origination | `select_gateway_for_leaf` (`node_mac.cpp:111`) scans **`_gw_schedules`** (populated only from a **directly-heard** `self_gateway` beacon, `node_beacon.cpp:265`) | ❌ 1-hop only |
| Multi-hop propagation of "which gateway bridges which layer" | — | ❌ **MISSING** (no `bridged_layers` in `lib/core`) |

Consequence: a node **>1 hop from a gateway cannot originate a cross-layer DM** — `select_gateway_for_leaf` returns 0 → `err_no_gateway`/`send_failed`. Every prior test (s20, native units) had the sender adjacent to the gateway, so it stayed hidden.

The Lua reference: `select_gateway_for_layer` (`spec/dv_dual_sf.lua:5168`) reads `self.bridged_layers` — a table fed by the propagated **type-4 BCN TLV** (`build_gateway_layer_ext` :1510, `ingest_gateway_layer_entries` :4936), re-gossiped by **every** node (incl. non-gateways), so the mapping travels the whole mesh.

---

## 1. What STAYS (correct — do not modify)

- The `CROSS_LAYER` inner format, the bridge fork (`do_post_ack` DELIVER branch), the cursor advance, the reversed-path E2E ack.
- Multi-hop forwarding of the cross-layer DATA.
- **The schedule-defer in `issue_send` keyed on the next-hop.** This already *is* "the neighbour holds it for the window": the message routes multi-hop toward G like normal DATA; only the **last hop** (next-hop == G, a direct neighbour with `_gw_schedules`) defers; intermediate hops forward immediately (`gateway_schedule_defer_ms` returns 0 for a non-gateway next-hop). No change.

---

## 2. What's ADDED — the discovery half (6 parts)

### 2.1 Wire: BCN ext-TLV **type 4** (gateway-layer) — mirror the Lua exactly

- `protocol_constants.h`: add `inline constexpr uint8_t bcn_ext_type_gateway_layer = 4;` (digest is 3 → 4 is free). Add `cap_bridged_layers = 8`, `bridged_layers_ttl_ms` (Lua = 48 h; device default fine, sim gate may shrink), `bridged_layers_max_per_tlv = 9`.
- `frame_codec.{h,cpp}`: add, mirroring `pack/parse_channel_digest_tlv`:
  - `size_t pack_gateway_layer_tlv(const GwLayerEntry* e, uint8_t n, std::span<uint8_t> out);`
  - `uint8_t parse_gateway_layer_tlv(std::span<const uint8_t> ext, GwLayerEntry* out, uint8_t max);` — scan the ext block for type 4, skip other types (forward-compat).
- **Wire format = the Lua split-list** (`build_gateway_layer_ext` :1565-1605), byte-for-byte:
  - Header byte `(bcn_ext_type_gateway_layer << 4) | (body_len & 0x0f)`.
  - Body = `N × gw_id(1B)` then `ceil(N/2)` packed layer-nibble bytes: entry *i*'s `dest_leaf` (4-bit) at low nibble if *i* even, high nibble if *i* odd.
  - `GwLayerEntry { uint8_t gw_id; uint8_t dest_leaf; }` — `gw_id` = the gateway's **node_id on the advertising leaf**; `dest_leaf` = the 4-bit leaf it bridges TO.
  - Cap `N ≤ bridged_layers_max_per_tlv (9)` → body ≤ 14 ≤ the 4-bit len cap (15).

### 2.2 State: `_bridged_layers` — Node-global fixed array (mirror `_gw_schedules`)

- `node.h`: `struct BridgedLayer { uint8_t gw_id; uint8_t dest_leaf; uint64_t last_seen_ms; bool valid; };` and `BridgedLayer _bridged_layers[protocol::cap_bridged_layers];` (Node-global — leaves carry it too, they're the ones that originate).
- Helpers (mirror `store_gateway_schedule`/`find_gw_schedule`):
  - `ingest_bridged_layer(uint8_t gw_id, uint8_t dest_leaf)` — **last-write-wins** (Lua :4936): find the row with this `gw_id` → overwrite `dest_leaf` + `last_seen_ms`; else use a free/oldest slot. One row per `gw_id`.
  - `prune_aged_bridged_layers(now)` — invalidate rows older than `bridged_layers_ttl_ms`. Call at the top of selection (Lua :5171).

### 2.3 Ingest (RX): type-4 → `_bridged_layers`

In `ingest_beacon` (`node_beacon.cpp` ~:242, right after the channel-digest parse), when `b.has_ext`: `parse_gateway_layer_tlv(ext, …)` → for each entry call `ingest_bridged_layer(gw_id, dest_leaf)`. **Skip** an entry whose `gw_id == _node_id` or `dest_leaf == _cfg.leaf_id` (our own layer — useless, Lua skips it).

### 2.4 Emit (TX): `build_gateway_layer_ext`, appended to the beacon ext

In `emit_beacon` (`node_beacon.cpp` ~:190): grow `ext_buf` (16 → 32) and **append** the gateway-layer TLV after the channel-digest build. Entries =
- **(a) Self-advert (gateway only — `is_gateway` ≡ `n_layers==2`):** for each OTHER layer this gateway serves, `{ gw_id = _layers[active].node_id, dest_leaf = other_leaf }` (Lua: `dest_layer` = the other layer relative to `active_leaf`).
- **(b) Propagate (ALL nodes):** every valid `_bridged_layers` row with `dest_leaf != active_leaf` → `{ gw_id, dest_leaf }`.
- Dedup `gw_id` (each at most once — Lua `seen_in_tlv`); sort by `last_seen_ms` desc; cap 9; pack the split-list.
- **★ KEYSTONE-CRITICAL: return 0 (emit NO TLV) when there are no entries** — exactly like `build_channel_digest_ext` returns 0 when nothing is dirty. A single-layer node is not a gateway and never ingests a type-4 TLV, so `_bridged_layers` stays empty → no TLV → **s18 wire byte-identical**. This is the gate's hard guard.

### 2.5 Selection: rewrite `select_gateway_for_leaf` as a **two-pass** (Lua :5168)

Keep the signature `uint8_t select_gateway_for_leaf(uint8_t target_leaf) const` (0 = none). `prune_aged_bridged_layers(now)` first. Then:

- **Pass 1 — a gateway WITH a live route (preferred).** Scan the active routing table `_active->_rt[]`; for each routed `dest_id` (primary candidate, `!= _node_id`): `bridges_target` if **either** a `_gw_schedules[dest_id]` record has `leaf_id == target_leaf` (1-hop) **or** a `_bridged_layers` row has `gw_id == dest_id && dest_leaf == target_leaf` (multi-hop). Among matches pick **best by (fewest hops, then best score)**. Return it.
- **Pass 2 — known-to-bridge but UNROUTED (fallback).** If Pass 1 found none: return any gateway known to bridge `target_leaf` — first from `_gw_schedules` (direct neighbour), then from `_bridged_layers` **with the on-layer seen-guard**: the `gw_id` must have been seen recently on THIS leaf (`id_bind`/`dest_seen`), else SKIP it. The caller enqueues toward it; `issue_send`'s no-route path fires the ROUTE_QUERY.
  - **★ The seen-guard is a real correctness rule (Lua :5234), DO NOT omit:** it rejects a *cross-layer TLV leak* — e.g. a `(gw→L2)` entry that propagated INTO L1 via a dual-layer gateway. That gw lives on L2, is unreachable from L1; addressing it would route nowhere. Only trust a propagated entry for a gw we've actually heard on our own leaf.

### 2.6 Route fallback (⚠️ VERIFY, may need wiring)

Pass-2 returns a gw with no live route → `enqueue_cross_layer` → `issue_send` no-route path must **park + fire a reactive ROUTE_QUERY/RREQ** for that gw (the 4d.2 design), re-flown by `try_drain_deferred` when the route appears; ages to `send_failed` on the deferred TTL — **loud, never a silent drop**.
- **⚠️ My grep of `defer_send` (`node_mac.cpp`) found NO `emit_route_request`.** Confirm the originated (`is_forward == false`) cross-layer no-route item actually triggers a ROUTE_QUERY — it may live in `issue_send`/`try_drain_deferred`, or it may need wiring. Do **not** leave it a silent park.

---

## 3. Fixes/additions vs the original coder sketch

1. **Two-pass selection** (routed-preferred, then unrouted fallback) — *not* "1-hop schedule first, then bridged_layers". Reading `_gw_schedules` for *selection* conflates the timing table with the routing table and would pick a worse-routed gw; `_gw_schedules` is for the **defer only**. Selection prefers the best **route**.
2. **The Pass-2 on-layer seen-guard** (cross-layer TLV-leak rejection) — was omitted.
3. **`build_gateway_layer_ext` MUST return 0 when empty** — the s18 byte-identical guard, called out explicitly.
4. **The sim test must become multi-hop** (§4) — a 1-hop test cannot exercise this fix.
5. **The ROUTE_QUERY fallback is VERIFY-not-assume** (§2.6).
6. **`_bridged_layers` = flat fixed array** mirroring `_gw_schedules` (not a `std::map`).

---

## 4. Test plan

- **Native units:** TLV pack/parse round-trip incl. **odd N** nibble packing; `ingest_bridged_layer` last-write-wins; **`build_gateway_layer_ext` returns 0 when empty** (the s18-safety unit); propagation (a non-gateway re-advertises a learned entry in its own beacon ext); `select_gateway_for_leaf` two-pass — (a) routed gw preferred over an unrouted one, (b) Pass-2 seen-guard **REJECTS** a gw never seen on our leaf, (c) two gateways on one leaf bridging different leaves → the right gw is chosen per target; **multi-hop origination** (gw 2 hops away: `_bridged_layers` populated, `_gw_schedules` empty → selects + enqueues, NOT `send_failed`).
- **Sim — make it MULTI-HOP:** evolve `s20` (or add `s21`) so the sender is **≥2 hops** from the gateway — insert a relay R: X hears R, R hears G, **X does NOT hear G's beacon**. Assert: Y@layerB receives the DM (multi-hop cross-layer delivers), and the schedule-defer fires at **R** (the gateway-neighbour), not X. Also fold in the prior gate's open items: **add the leak-gate negative assertion** to the scenario `expect` (y_l5 gets NO channel push) and **fix the wrapper's `channel_recv` → `send_failed` mislabel** in `NodeRuntimeWrapper.cpp drainPushes` (so channel deliveries are observable — s18 has 0 such pushes, so it's keystone-safe).
- **s18 keystone: BYTE-IDENTICAL** (779015 / md5 `77205506d944af2eec03b8c9aac405bc`) — the type-4 TLV must be inert for single-layer.
- **4 builds** green; gateway RAM < 100% (the array is ~8 × 11 B ≈ 88 B — trivial).

## 5. Gate criteria (what gets checked)

native (incl. the multi-hop origination unit) + 4 builds + **s18 byte-identical** + the **multi-hop** sim scenario delivers + read the two-pass selection & the seen-guard + confirm `build_gateway_layer_ext` returns 0 when empty + confirm the ROUTE_QUERY fallback actually fires (§2.6).

## 6. Out of scope (explicit — document, don't silently mis-handle)

A **single** gateway bridging **3+ layers via PROPAGATION**: the `gw_id → single dest_leaf` table (last-write-wins, Lua-faithful) loses the 2nd `dest_leaf` when re-gossiped. Direct neighbours still know all served leaves via `_gw_schedules`. Full multi-bridge propagation belongs with the **3-layer scenarios (Slice 5)**. Note the limitation in a comment; do not silently mis-route.
