# Gateway — Dual-Layer (cross-layer routing) Design Spec

**Date:** 2026-06-12 · **Status:** DESIGN — for user review, then a coding agent implements + I quality-gate.
**Goal:** a **gateway** node that is a full member of **two layers at once** with **one identity**, **routes DMs between the two layers** (its core job), and is **tested against a multi-layer scenario**.

**Examined first (2026-06-12):** the Lua reference (`spec/dv_dual_sf.lua`) gateway/layer/scheduler in depth + the current C++ state. This design **deliberately diverges from the Lua** where the user's model is cleaner (symmetric, no home/guest, layer-keyed structures, DATA-frame cross-layer instead of a separate `gw_env`). All `file:line` are from the current tree.

## 0. Decisions locked with the user (2026-06-12)

1. **Symmetric two layers — NO home/guest.** The Lua has an asymmetric `home`(primary) + `visit` model (`build_gateway_layer_ext` flips the advertised bridge-direction by active leaf; `gateway_schedule_defer_ms` defers-to-end on home vs defer-to-open on visit). **We reject that.** Both layers are equal; neither is primary.
2. **One identity, one `node_id` PER layer.** ONE `key_hash32` (the identity). `node_id` is a leaf-scoped 8-bit address, so the gateway runs **independent DAD on each leaf → one `node_id` per layer**, both bound to the shared `key_hash32`.
3. **Cross-layer routing is the CORE job, from the start.** A DM whose destination lives on the other layer is bridged by the gateway. **Intra-layer routing (relaying *other* nodes' same-leaf traffic) is NOT provided by default** — but every structure must carry `layer_id` so it can be added later.
4. **Single radio → time-multiplex. Local + receiver-anchored timing, NO global clock.** Each layer's presence window is scheduled off THIS node's own clock; neighbours learn it via a relative countdown re-stamped at TX (the only thing the Lua got right here). There is no shared epoch / slot grid.
5. **Layer-key EVERYTHING.** Queues, pending, routing table, all dedup tables, id-bind, channel buffer — per-layer. (The Lua keys only `rt`/`id_bind`/`dest_seen` per-layer and leaves the MAC pipeline + dedup global+tagged — a latent **aliasing bug**: the dedup key `(origin<<24)|(dst<<16)|ctr` (`node_mac_rx.cpp:393`) has no layer bits, so the same 8-bit id on two leaves collides. Layer-keying fixes it.)
6. **NO unagreed fallbacks.** §7 lists every Lua default; the symmetric model deletes the ~30 "…or home …or 0" idioms; the rest become an **agreed default table** (§3.2) the user signs off.
7. **Channels stay leaf-local — they do NOT cross.** Cross-layer = DMs only. The gateway remains the channel-plane **consumer-not-provider** ([[gateway-consumer-not-provider]]); the M-frame leak gate (`node_mac_rx.cpp:314`) is unchanged.
8. **`layer_id` is 8-bit (1..255 layers); `leaf_id` (4-bit) = `layer_id & 0x0F` is a DERIVED wire filter.** The config holds the full `layer_id` per layer. **Addressing** — the cross-layer layer-path AND every per-layer structure key — uses the **full 8-bit `layer_id`**; the byte-0 leaf nibble is only the coarse frame gate. Operators give co-channel layers distinct low nibbles so the wire filter separates them. (Lua collapses layer_id to 4-bit == leaf; we widen to 255.) **Wire fields stay 4-bit** — the byte-0 leaf gate AND the beacon `schedule_record.layer_id` (a window-presence filter) remain 4-bit; a node learns a peer's **full 8-bit `layer_id` only for addressing**, from the hash-locate **H-answer's `target_layer`** (already a `u8`, `frame_codec.h:428`) + the cross-layer layer-path. So nothing on the wire grows; the 8-bit id is config + addressing-local.
9. **Window symmetry = equal DATA throughput both ways, NOT equal time** (§4). Same SF on both layers ⇒ 50/50 split; different SF (e.g. 8 vs 9) ⇒ the slower/higher-SF layer gets a proportionally LONGER window so the SAME number of bytes flows each way. Defaults (§3.2) are user-modifiable via `cfg`.
10. **Cross-layer DM uses a CURSOR over a PRESERVED full layer-path** (§5) — gateways advance `cur` but never pop history, so the destination reverses the whole path for a **standard** E2E ack (no special ack frame). This matches the already-reserved DATA inner (`2026-06-10-data-flags-type-cleanup-design.md:78,82`).

## 1. Current state we build on (verified)

- **Single-layer by construction:** one `NodeConfig` with one `leaf_id` (`node.h:60`, "layer id (single-layer R1 = 0)"). Every TX stamps `_cfg.leaf_id`; every RX drops `leaf_id != _cfg.leaf_id`.
- **Cross-layer scaffolding is reserved + inert, ready to activate:**
  - `DATA_FLAG_CROSS_LAYER = 0x40` (`frame_codec.h:342`) — *"the inner carries a layer-path"*; decoded into `data_out::cross_layer`, **zero readers**. **This is our cross-layer primitive** — a normal DATA frame, not a separate envelope. (Better than the Lua `gw_env`; the user okayed diverging.)
  - `SendLayerCmd { uint8_t hops[gw_env_max_hops=4]; uint8_t hop_count; uint32_t dst_hash; }` (`command.h:28`) + `CmdKind::send_layer` (today → `err_unsupported`, `node.cpp:237`) + `err_no_gateway`/`err_no_binding` (`command.h:51`).
  - `schedule_record { layer_id(4-bit); routing_sf; period_unit_5s; duration_100ms; offset_100ms; period_units }` (`frame_codec.h:63`) in the beacon, with `beacon_in.self_gateway` + `gateway_spread_nibble` — the visit-window advert. **Codec exists; `emit_beacon` never populates it** (`node_beacon.cpp:157`), and `parse_beacon_schedule` has zero runtime callers. Greenfield.
  - `beacon_entry.is_gateway` (`frame_codec.h:52`) + `RtCandidate::is_gateway`/`learned_layer_id` (`node.h:101`) — learned but **never consumed for a forwarding decision** (always set to own leaf).
  - `hash_bind_inner { target_layer; node_id; key_hash32 }` (`frame_codec.h:428`) — hash-locate is already layer-aware on the wire (the H-answer says which layer the hash lives on).
- **Timer wheel** cap 64, Node owns ids 1..63 densely (`node.h:298-329`, `timer_wheel.h:23`). A second layer's beacon/window timers need id space.

## 2. Per-layer state (the refactor)

Introduce a `LayerRuntime` holding everything that is layer-scoped — **keyed by the full 8-bit `layer_id`** (§0.8), not the 4-bit leaf — and the Node holds **N=1 or 2** of them (a normal node has 1; a gateway 2). The MAC pump operates on the **active** `LayerRuntime` (swapped at each window switch). Per-layer:

- `node_id` (per-leaf DAD), `leaf_id`, `routing_sf`, `allowed_sf_bitmap`, beacon period/offset.
- Routing table (`_rt`/`_rt_count`), id-bind cache (`_id_bind`), `dest_seen`.
- **The MAC pipeline** the Lua left global: `_tx_queue`, `_pending_tx`, `_pending_rx`, `_post_ack`, `_deferred`.
- **All dedup**: `_seen_origins`(+`_from`), `_last_acked_from`, `_rreq_seen`, `_hash_query_seen`, `_peer_send_counter`, `_neighbor_budget_tier`, `_blind_until`.
- Channel state (`_channel_buffer`, pull/flood) — per-leaf (channels don't cross).

**Shared (single, not per-layer):** the identity (`key_hash32`, the master seed), the radio/Hal, the inbox (DMs from both layers land in the one inbox; `origin` is ambiguous across leaves → **the inbox record + the `Push` carry the receiving layer** [RESOLVED with the user], so the app/inbox knows which layer a DM arrived on; DM app-identity stays `(sender_hash, ctr)`), the cross-layer routing table (which gateway bridges which layer-pair — the only genuinely cross-layer structure).
  - **Q13 RESOLVED (2026-06-12, with the iOS agent): the receiving-layer field is the FULL 8-bit `layer_id`, not the 4-bit `leaf_id`.** Same byte width, but the leaf nibble is exactly what aliases across the 255-layer space (§0.8 keys everything on the full id) — a 4-bit field can't tell layer 7 from 23 from 39. So the `Push` POD + the inbox record + the live-push / `inbox_dm` / `inbox_channel` JSON carry `layer_id` (u8). Settled BEFORE the Push POD grows (i.e. before Slice 2a/4 adds the field), per the companion contract.

**RAM note (MEASURED 2026-06-12):** two `LayerRuntime`s ≈ +82 KB (one runtime ≈ 80 KB), which on the unconditional `_layers[2]` blew the XIAO to **98.6%**. The dominant cost is **NOT** the route table (`_rt` is only ~22 KB) — it's **`_channel_buffer` (~31 KB, 128 × 248 B)**, which a gateway **skips entirely** (Principle 11, the gossip plane is single-layer-only). Resolution + the build-conditional fix are in **§9.2**.

## 3. The two configs

### 3.1 Config model
A normal node config is unchanged. A gateway adds a **second symmetric layer block**. Proposed shape (no "home" — `layers[0]` and `layers[1]` are peers):
```
struct LayerConfig { uint8_t  layer_id;          // 8-bit FULL id (1..255); leaf_id = layer_id & 0x0F (derived wire filter)
                     uint8_t  routing_sf; uint16_t allowed_sf_bitmap;   // REQUIRED per layer (no inherit)
                     uint32_t beacon_period_ms;
                     uint32_t window_period_ms;   // the cycle length (default 15000, cfg-overridable)
                     uint32_t window_ms;          // presence this layer gets in the cycle; 0 = DERIVE it SF-weighted (§4)
                     uint32_t window_offset_ms; };// phase; 0 = derive anti-phase from the other layer's window
// NodeConfig gains: uint8_t n_layers (1 or 2); LayerConfig layers[2];   // single-layer node: n_layers=1, layers[0]
// is_gateway is DERIVED: n_layers == 2. (gateway_only / channel-consumer flags unchanged.)
```
`cfg set` gains the second-layer keys (all per-layer params are user-settable); NV (the Blob) gains `layers[1]` — **bump `kVersion`** (re-provisions all nodes; fine per the 3-test-node policy). The structure keys + the layer-path use the full `layer_id`; the wire byte-0 uses `leaf_id = layer_id & 0x0F`.

### 3.2 DEFAULTS (user-modifiable via `cfg`)
These are the documented defaults; **all are overridable**. A missing **REQUIRED** field is an error (fail loud), not a silent fill.

| Param | Default | Notes |
|---|---|---|
| `layer_id` (each layer) | — | **REQUIRED** — refuse the layer if unset (full 8-bit id; leaf = low nibble) |
| `routing_sf` (each layer) | — | **REQUIRED** (no "inherit the other layer" — that was a Lua fallback) |
| `allowed_sf_bitmap` (each layer) | — | **REQUIRED** (no inherit) |
| `window_period_ms` | **15000 ms** | the full layer0→layer1 cycle; cfg-overridable |
| `window_ms` (each layer) | **DERIVED** — SF-weighted split of the period (§4) | set explicitly to override the SF-weighting |
| `window_offset_ms` | **DERIVED** anti-phase (layer1 opens when layer0 closes) | set explicitly to override |
| `beacon_period_ms` (each layer) | the node default (900000) | cfg-overridable |
| schedule guard | **100 ms** | cfg-overridable |

**Default split = throughput-symmetric, anti-phase** (§4): the two windows fill the period back-to-back (never overlapping), durations weighted by SF so **equal DATA flows each way** (equal-SF ⇒ 50/50; SF8 vs SF9 ⇒ the SF9 side gets the longer window). **Validation (fail loud, no silent fix):** if explicit windows would overlap (`window0 + window1 > window_period_ms`) or a REQUIRED field is unset, `on_init` REFUSES — no auto-adjust (the Lua silently armed overlapping windows).

## 4. Scheduler / timing (the part you flagged)

**No global clock — confirmed model.** Each layer's presence is a **local self-rearming window**: at boot, arm `layer[i]` to open at `now + offset[i]`, hold `window_ms`, then re-arm one `window_period_ms` out. Two timer ids per layer (window-open, window-close) + one beacon id per layer. **Timer-id budget:** the cap-64 wheel with ids 1..63 already dense needs a per-layer id sub-range carved out — [open §9: confirm the id map; may need to raise `TimerWheel::kCap` or compact the existing ranges].

**Throughput-symmetric window split (§0.9 — equal DATA both ways, not equal time).** The default per-layer `window_ms` is DERIVED to equalize bytes-per-cycle across the two layers: weight each window by the layer SF's effective **per-byte airtime** (from the `airtime.h` model — a higher SF costs more airtime/byte, so it needs a longer window to move the same bytes):
`window_i = window_period_ms × airtime_per_byte(sf_i) / (airtime_per_byte(sf_0) + airtime_per_byte(sf_1))`, placed anti-phase (`offset[1] = window[0]`). Equal SFs → 50/50; e.g. SF8+SF9 → ≈36%/64% (the SF9 side longer). The user may override `window_ms`/`offset` explicitly (then validated non-overlapping, §3.2). A single-layer node has no split (one window == the whole period, i.e. always-on).

**Window switch** = `activate_layer(i)`: retune the radio to `layers[i].routing_sf` (re-arm RX), swap the active `LayerRuntime`. **Guard:** never switch mid-exchange — defer while `_pending_tx` **OR** `_pending_rx` **OR `_post_ack.pending`** is set (the post-ACK delivery step straddles the ACK and must finish on its own layer — code-verified 2026-06-12), to the end of the in-flight RTS/CTS/DATA/ACK + its delivery (mirror the Lua `pending` busy-guard, but symmetric).

**Neighbours learn the window** from the gateway's beacon `schedule_record`: `offset_100ms` = a **receiver-anchored countdown** to the next window-open on *that* layer, re-stamped at actual TX time (`apply_schedule_tx_fixup` equivalent — account for LBT defer + airtime so `heard_ms + countdown` lands true). A sender that wants the gateway **defers its send until the gateway's window on the sender's layer opens** (this replaces the Lua's asymmetric home/visit defer with a single symmetric rule: *the gateway is reachable on layer L only during layer L's window*).

**The Lua bug we DON'T inherit:** the Lua assumes a node is on exactly one leaf forever and a gateway never follows another gateway's schedule (`gateway_schedule_defer_ms` returns 0 for `self_gateway`) — so two gateways handing off is unhandled. Here, a gateway **is** a schedule-follower of its own two windows; cross-gateway handoff (multi-gateway transit) is explicitly **out of v1** (single gateway per layer-pair, §5) and the structures (layer-path, `hops[4]`) reserve it.

## 5. Cross-layer DM routing (the core)

**Mechanism — activate `DATA_FLAG_CROSS_LAYER` (no new frame type).** A cross-layer DM is a normal DATA frame with `CROSS_LAYER` set and a **layer-path** in the inner.

**Inner format — activates the reserved `CROSS_LAYER` slot** (matches the already-reserved order in `2026-06-10-data-flags-type-cleanup-design.md:78`):
```
inner = [dst_key_hash32:4  (iff DST_HASH — the final recipient)]
        [layer-path:        (iff CROSS_LAYER)  n_layers:1 | cur:1 | layer_ids: n_layers × 1B (FULL 8-bit ids)]
        [origin:1]
        [source_hash:4      (iff SOURCE_HASH)]
        [body…]
```
**CURSOR, not pop (§0.10):** `layer_ids[]` is the COMPLETE path origin→…→dest; `cur` indexes the next layer to enter and only **ADVANCES** — history is never removed. So the destination holds the whole path and **reverses it for a standard E2E ack** (no special ack frame). For v1 (2 layers, 1 gateway) the path is `[origin_layer, dst_layer]`, `cur` starts at 1. (`n_layers ≤ gw_env_max_hops = 4` reserves future multi-gateway transit.)

**Send (sender X on layer A → recipient Y whose hash is on layer B):**
1. X resolves `dst_hash → (node_id, layer)` via hash-locate (`H` flood; the H-answer's `target_layer` says B). If `target_layer == A` → a normal same-layer DM (no cross-layer).
2. X finds a gateway G that bridges A↔B (G beacons `self_gateway` on both A and B; X's layer-A routing table has a route to G with `is_gateway`). **No gateway known → `err_no_gateway`** (fail loud, no fallback flood).
3. X sends a `CROSS_LAYER` DATA on layer A, **next-hop routed to G**, inner layer-path `[A, B]` with `cur=1` (the full origin→dest path; §0.10), `dst_key_hash32 = Y`.
4. Defer until G's layer-A window (§4).

**Bridge (gateway G):**
1. In its layer-A window, G receives the `CROSS_LAYER` DATA addressed to it. Reads the path: next layer = B.
2. G resolves `dst_key_hash32 → Y.node_id` on **layer B's** id-bind. **Unknown → flood an `H` query on B + defer the handoff** (bounded by `gateway_handoff_defer_ttl_ms`; on giveup → drop + (optional) `send_failed` to X). **No binding + giveup → drop, not a silent reroute.**
3. G **re-injects** a DATA on layer B toward Y (in its layer-B window), **advancing `cur` (the full `layer_ids[]` history is preserved — §0.10)**. The re-inject is marked **`gw_relay`** so it is **exempt from the originator anti-spam throttle** (G re-originates on B with no preceding CTS — must not be mis-counted; mirror the Lua `RTS_FLAG_RELAY` exemption).
4. Bound the handoff queue (`cap_gateway_deferred_handoffs = 32`, already reserved) — refuse-when-full, loud.

**E2E-ack reverse path (standard, no special frame):** because the full path was preserved (§0.10), Y builds a **standard** E2E ack — `APP` + `TYPE=E2E_ACK` + `CROSS_LAYER` (`2026-06-10-data-flags-type-cleanup-design.md:82`) — over the **reversed** `layer_ids[]` (`cur` reset). It walks B→G→A back to X with no gateway-specific ack logic; G bridges it exactly like any other cross-layer DATA.

**What the gateway does NOT do:** relay *intra*-layer (a DM between two layer-A nodes that aren't it) — that's §6. And no channel bridging (§0.7).

## 6. Intra-layer routing — NOT in v1, structures ready

A gateway does not forward other nodes' same-leaf DMs by default. Because every queue/route/dedup is layer-keyed (§2), enabling it later = letting the active `LayerRuntime` forward on its own leaf (the normal relay path, currently single-node). Leave a `cfg` flag `intra_layer_relay=false` (default) so it's an explicit opt-in, never a silent behavior. [Reserved, not implemented.]

## 7. Fallbacks audit (every Lua default surfaced; our disposition)

- **DELETED by the symmetric model (~30 sites):** every `active_layer_id or self.layer_id` "fall back to home" and `… or 0` "default to layer 0" (`dv_dual_sf.lua` L4791-4812, L8853-8854, L9029, etc.). With two equal explicit layers and per-layer state, there is no "home" to fall back to and no implicit layer 0.
- **DELETED (inheritance fallbacks):** visit `routing_sf`/`allowed_data_sfs` inheriting the home layer's (L9030-9031) → **REQUIRED per layer** (§3.2).
- **KEPT as agreed defaults (§3.2):** window 7500 / period 15000 / offset 7500 / guard 100. (Lua L1133-1135, L1120.)
- **The one undeclared Lua default** `gateway_layer_busy_retry_ms = max(rts_busy_retry_ms or 100, 1000)` (L8425) → declare it explicitly as a named constant.
- **Silent truncations to surface, not copy:** Lua clamps a 0 duration/period to the smallest wire unit (L1649/L1667) and silently truncates the bridged-layers TLV to 9 (L1186) — we instead **validate + refuse** a 0 window/period (§3.2) and v1 has ≤2 layers so no truncation.

## 8. Test plan — MUST cover a multi-layer scenario

- **Native (the keystone — pure logic, FakeClock):**
  - `LayerRuntime` swap + the window scheduler (two anti-phase windows; the busy-guard defers a mid-exchange switch; no overlap; receiver-anchored countdown math).
  - Per-layer dedup **non-aliasing**: same 8-bit `origin` on both leaves does NOT collide (the bug we're fixing — assert it).
  - Cross-layer send/bridge/ack on a 2-layer harness: X@A → G → Y@B delivers; the ack returns; unknown-binding defers then gives up loud; no-gateway → `err_no_gateway`.
  - Config validation: overlapping windows / missing required per-layer field → `on_init` refuses (fail loud).
- **Sim — use the EXISTING multi-layer scenarios + diff against the Lua** (the established parity methodology, like the s18 route-discovery parity; **no new scenario**): `s09_two_layer_gateway_debug` / `s10_two_layer_gateway_separation` (2-layer + gateway), `s15_three_layer` / `s15_three_layer_channels` (3-layer), `s16_dense_gateway` / `s16_dense_gateway_2gw` (dense, 1–2 gateways), `s12_channels_dense_two_layer`. Run the C++ `FirmwareNode` against each and **compare to the Lua** for: cross-layer DM delivery, the channel leak-gate (channels do NOT cross the leaf boundary), and airtime/duty sanity while gateways time-multiplex. The 3-layer scenarios also exercise the **cursor path beyond v1's single hop** (multi-gateway transit).
- **Reality-split:** native + sim by me/the agent; the on-metal two-XIAO two-leaf gateway exchange is the user's bench.

## 9. OPEN DECISIONS

**RESOLVED with the user (2026-06-12), folded above:** inbox + Push carry the receiving **`layer_id`** (full 8-bit — Q13, settled with the iOS agent; the leaf nibble aliases across 255 layers), DM identity stays `(sender_hash, ctr)` (§2) · layer-path = cursor over a PRESERVED full path, reserved inner order, standard reversed-path e2e ack (§5/§0.10) · defaults documented + user-modifiable, windows SF-weighted throughput-symmetric (§3.2/§4/§0.9) · `layer_id` 8-bit (255 layers), `leaf_id`=low-nibble wire filter (§0.8) · NV `kVersion` bump for `layers[1]` (§3.1) · test against EXISTING multi-layer scenarios + Lua parity, no new scenario (§8).

**Resolved 2026-06-12 (implementation/bench details — no open design questions):**
1. **Timer-id budget** (§4) — **EXTEND the id-space, do NOT plumb `layer_id` through the wheel.** Key insight: the node runs ONE exchange on ONE layer at a time (the busy-guard defers a layer switch mid-exchange), so the **MAC timers (RTS-timeout / ACK-wait / retry / slot ranges) are active-layer-SHARED** — reused across layers, no layer tag. Only the **persistent** per-layer timers need distinct ids: each layer's **beacon** + the **window open/close** (≈3–4). **CORRECTION (code-verified 2026-06-12):** the wheel is currently FULL — ids 1..63 are dense (flood ring at 61–63), no free upper band — so **raise `TimerWheel::kCap` (~80)** to open a band for the per-layer beacon/window ids, and **verify no ring/slot base is hardcoded relative to 64** (the flood ring must move with kCap or be pinned, not silently overlap). The HAL wheel stays a dumb `id→deadline` map; the layer dimension lives in the Node. Pure plumbing — the coding agent handles it.
2. **Gateway-build RAM — RESOLVED (MEASURED 2026-06-12).** Unconditional `_layers[2]` blew the XIAO to **98.6%** (+82 KB; one `LayerRuntime` ≈ 80 KB). Three-part fix:
   - **(a) Build-conditional layer count.** `LayerRuntime _layers[MR_N_LAYERS]`, `MR_N_LAYERS` **defaults to 1** → single-layer XIAO/Heltec/native return to **63.8%** (a TRUE no-op; the agent's restore proved nothing references `_layers[1]`). A new `[env:gateway]` build sets `-DMR_N_LAYERS=2`.
   - **(b) Fail-loud guard.** `on_init` REFUSES `n_layers==2` when `MR_N_LAYERS<2` (no silent fallback to single-layer).
   - **(c) Size the gateway build for the gateway's REDUCED role — the dominant lever is `cap_channel_buffer`, NOT `cap_routes`.** A gateway **skips the channel gossip plane** (Principle 11), so its ~31 KB/layer `_channel_buffer` (128 × 248 B) is dead weight: **`cap_channel_buffer` → ~8 (from 128)** claws back **~60 KB across the two layers** (98.6% → ~73%). So **`cap_routes` STAYS ~254** — no routing sacrifice. Secondary: `cap_deferred_sends` → ~16 (a gateway doesn't queue 32 un-routable sends, each a 241-B-inner `TxItem`). A `gateway_only` pure bridge can drop `cap_channel_buffer` to ~1.
   - **Do NOT trim the normal-node caps** (`cap_routes`/`cap_channel_buffer` are parity-tuned for the dense sim scenarios; single-layer behavior must not change).
   - **RAM accounting (2026-06-12 review) — static vs heap.** The ~148 KB static `g_node` is the FIXED ARRAYS (`_rt`/`_channel_buffer`/`_deferred`/`_id_bind`…); the `std::map` dedup tables (`_seen_origins`/`_last_acked_from`/`_per_sender_originator`/…) are **HEAP** (~48 B static each) — their caps are **heap** levers (gate on a real free-heap reading after BLE init), NOT static. So static-RAM tuning = the array caps; don't expect the map caps to move the static number. **Build-neutral static wins (help ALL builds, no cap/parity change):** (1) `try_drain_deferred`'s ~12.5 KB function-static scratch (`drained[32]`+`nq[8]` of full `TxItem`s) → restructure to a `status[32]` byte-array + in-place `memmove` — they are `static` to dodge an ~11 KB stack frame, so do **not** make them stack locals; (2) TinyUSB unused-class compile-outs (~3 KB, `CFG_TUD_*=0` — verify the BSP honors the `-D`); (3) a shared NDJSON line scratch (−1.7 KB). The LittleFS `cache_buffer` (4 KB) is BSP-owned — don't edit vendored. (Already landed: #2 + #3 + `-Wl,-Map`; #1 is the meatier follow-up.)
   - **Verify + re-gate:** the gateway's channel-skip guards never write past the small buffer (assert no overflow on a gateway); single-layer build back to 63.8% (native byte-identical = no-op); gateway build < 100% RAM; the s18 single-layer sim parity diff stays clean.
3. **DAD per layer:** **two independent `join` runs (one per leaf) at boot**, both bound to the one `key_hash32`; **`whoami` surfaces both per-layer `node_id`s**.

---
**Next:** user reviews §0–§9 (especially the open decisions); I fold the answers; then a coding agent implements against this + I quality-gate (native + the new sim layers scenario green; both boards build; metal = user bench).
