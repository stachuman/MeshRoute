# Configuring a Gateway

A **gateway** is one node that is a full member of **two layers at once** (one identity, one `node_id` per layer) and bridges DMs between them. It's a **dedicated firmware build**, configured over the **serial console** with `cfg set …`, and the layer config is **applied on reboot** (persisted to NV).

> A gateway is set up on the node itself via serial today. The companion/BLE path and join-distributed config are not wired yet.

## 1. Flash the gateway build

```
pio run -e gateway -t upload
```
`[env:gateway]` compiles with `MR_N_LAYERS=2` + `MR_GATEWAY_BUILD=1` (and the gateway RAM caps). A gateway build **requires** `n_layers==2` — it refuses to come up as a single-layer node (and a normal build refuses `n_layers==2`). So: gateway build ⇒ must be configured as a gateway.

## 2. Set the two layers (serial `cfg set`)

**Layer 0 (the "home" leaf)** reuses the standard single-layer keys, plus `layer0_id`:

| key | meaning |
|---|---|
| `node_id`    | layer-0 node_id (per-leaf short id) |
| `layer0_id`  | layer-0 **full 8-bit** layer id (1..255; leaf = id & 0x0F) |
| `routing_sf` | layer-0 routing/control SF (5..12) |
| `sf_list`    | layer-0 allowed data SFs (e.g. `10` or `9,10`) |
| `beacon_ms`  | layer-0 beacon period (optional) |

**Layer 1 (the second leaf)** uses the `l1_*` keys:

| key | meaning |
|---|---|
| `l1_layer_id`   | layer-1 full 8-bit layer id |
| `l1_node_id`    | layer-1 node_id |
| `l1_routing_sf` | layer-1 routing SF |
| `l1_sf_list`    | layer-1 allowed data SFs |
| `l1_beacon_ms`  | layer-1 beacon period (optional) |

**Shared window schedule** (optional — sensible defaults):

| key | meaning |
|---|---|
| `window_period_ms` | the one shared layer0↔layer1 cycle (default 15000) |
| `l0_window_ms` / `l1_window_ms` | presence per cycle; **`0` = derive** an SF-weighted anti-phase split |
| `l0_window_offset_ms` / `l1_window_offset_ms` | phase; **`0` = derive** anti-phase |

Finally:

| key | meaning |
|---|---|
| `n_layers` | `2` to make it a gateway (`1` = single-layer) |

Order doesn't matter — every key persists to NV; one reboot applies them together.

## 3. Worked example — a gateway bridging layer 4 ↔ layer 5

```
cfg set node_id 10          # layer 0: home node_id
cfg set layer0_id 4         # layer 0: full layer id (leaf 4)
cfg set routing_sf 7        # layer 0: routing SF
cfg set sf_list 10          # layer 0: data SF(s)
cfg set l1_layer_id 5       # layer 1: full layer id (leaf 5)
cfg set l1_node_id 11       # layer 1: node_id
cfg set l1_routing_sf 8     # layer 1: routing SF
cfg set l1_sf_list 11       # layer 1: data SF(s)
cfg set n_layers 2          # become a gateway
reboot                      # layer config is reboot-to-apply
```
Windows left unset ⇒ the firmware derives a non-overlapping anti-phase split over the default 15 s cycle.

## 4. Verify

```
status      # shows  n_layers=2  + one extra line per leaf (layer_id / node_id / routing_sf / window)
whoami      # same per-leaf summary
```

## 5. What gets rejected (fail-loud — the node stays down for re-provisioning)

`on_init` refuses an invalid gateway config rather than silently fixing it:
- `n_layers` not in `{1,2}`, or `2` on a non-gateway build.
- Either layer with `layer_id == 0`, `routing_sf` outside 5..12, or an empty `sf_list`.
- Both layers sharing the same **leaf nibble** (`layer_id & 0x0F`) — they must differ (it's the coarse on-wire filter).
- Explicit windows that overlap, or a `window_period_ms` mismatch between the layers.

If `status` shows the node didn't come up as a gateway, re-check these.

## Notes
- **Reboot-to-apply:** the dual-layer keys are `live=false` — set them, then `reboot`. (Single-layer radio knobs like `routing_sf` apply live for a normal node; for a gateway they take effect at the next boot via the layer config.)
- **Identity is shared:** one `key`/seed across both layers (set via `cfg set key` / `regen`); only `node_id` is per-leaf.
- **Channels:** a dual-layer gateway skips the channel-gossip plane (it's a DM bridge, not a channel relay).
- See `docs/frames.md` (BCN gateway-layer TLV) and `docs/superpowers/specs/2026-06-12-gateway-dual-layer-design.md` for the protocol detail.
