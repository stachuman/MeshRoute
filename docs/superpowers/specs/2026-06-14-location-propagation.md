# Location Propagation (opt-in lat/lon in DATA + M) — Coder Instruction

**Date:** 2026-06-14 · **Status:** INSTRUCTION for the coding agent · quality-gated after.

**Goal:** let a node optionally piggyback its own location on the DATA and M frames it originates, so location spreads further than a BCN (1-hop) — to the DM recipient (DATA) and to the whole leaf (M broadcast). The node already *has* a location (`cfg set lat/lon` → `g_lat_e7/g_lon_e7`, stored in the `/mrid` identity blob as `int32` deg×1e7); it just never transmits it.

## Decisions locked with the user (2026-06-14)
1. **Encoding:** 6 bytes, ~11 m, global. **21-bit lat + 22-bit lon** quantized from the stored `int32 e7`; 5 spare bits reserved=0. (5 B would be ~19–38 m at the equator — too coarse; 6 B is the floor for the 10–15 m target and lands at ~11 m.)
2. **DATA (DM):** new flag **`DATA_FLAG_LOCATION = 0x08`** (the one free bit). The 6-B location sits **inside the sealed region** (after `source_hash`, before `body`) — private to the recipient when `CRYPTED` lands; cleartext until then, like `body`.
3. **M (channel):** byte-2 `flavor` is **officially a flags byte** now — low 3 bits keep the crypto variant (`0=public`, `1=group`), and **bit `0x08` = location-included** (same bit as DATA, for consistency). M is broadcast → location is inherently public there. The 6-B location follows `channel_msg_id`.
4. **Opt-in, default OFF:** two independent node settings — **`loc_in_dm`** and **`loc_in_m`** — settable via `cfg` *or* the companion. When on, every originated DATA / M carries the location. Never send a `(0,0)` location (= unset).
5. **Airtime:** the flag is free (reused bit); the only cost is the 6 B, only when the toggle is on.

## 1. The 6-byte codec (`frame_codec.{h,cpp}`)

```
// ~11 m, global. Use int64 intermediates (the lon path overflows int32).
size_t pack_loc6(int32_t lat_e7, int32_t lon_e7, std::span<uint8_t> out6);   // 6, or 0 on short buf
bool   unpack_loc6(std::span<const uint8_t> in6, int32_t& lat_e7, int32_t& lon_e7);
```
- Encode: `u_lat = clamp((int64)(lat_e7 + 900000000) >> 10, 0, (1<<21)-1)` (21 bits); `u_lon = clamp((int64)(lon_e7 + 1800000000) >> 10, 0, (1<<22)-1)` (22 bits). Step = 1024 e7-units = 0.0001024° ≈ 11.4 m. Pack the 48-bit value `(u_lat << 27) | (u_lon << 5)` MSB-first into 6 bytes (low 5 bits reserved=0).
- Decode: `u_lat = (v>>27)&((1<<21)-1)`, `u_lon = (v>>5)&((1<<22)-1)`; `lat_e7 = (int32)((int64)(u_lat<<10) - 900000000 + 512)`; `lon_e7 = (int32)((int64)(u_lon<<10) - 1800000000 + 512)` (the +512 centres the cell). **The lon math MUST be int64** (`u_lon<<10` reaches 3.6e9 > int32 max).

## 2. Config (`node.h` `NodeConfig` + `fw_main.cpp`)
- `NodeConfig` (node.h:57): add `int32_t lat_e7 = 0, lon_e7 = 0;` and `bool loc_in_dm = false, loc_in_m = false;`.
- `fw_main.cpp` NodeConfig build (~:793): `cfg.lat_e7 = g_lat_e7; cfg.lon_e7 = g_lon_e7; cfg.loc_in_dm = g_loc_in_dm; cfg.loc_in_m = g_loc_in_m;` (add device globals + persist the two toggles in the cfg blob; the cfg-extras at :713 already carry lat/lon). Add `cfg set loc_in_dm on|off` / `loc_in_m on|off` console keys + list them in the help (:561) and `status`. When `cfg set lat/lon` changes `g_lat_e7/g_lon_e7`, also push into `g_node.mutable_config()` so a running node updates without reboot.

## 3. DATA send + parse (`frame_codec` + `node_mac`)
- `DATA_FLAG_LOCATION = 0x08` in the flags enum (frame_codec.h, the free b3).
- **Inner order (the LOCKED order — update pack AND parse together):**
  `[dst_key_hash32 — iff DST_HASH] [layer-path — iff CROSS_LAYER] [origin] [source_hash — iff SOURCE_HASH] [location 6 B — iff LOCATION] [body] [tag — iff CRYPTED]`
  — location is the new slot after `source_hash`, before `body` (inside the origin-onward sealed region).
- `pack_unicast_inner` (frame_codec.h:421): add `int32_t lat_e7, int32_t lon_e7` params; when `flags & DATA_FLAG_LOCATION`, write `pack_loc6` at the slot. Keep the size-first overflow check (the +6 B must still fit; body cap shrinks by 6 when location present).
- `data_unicast_inner` (parse output): add `bool has_location; int32_t lat_e7, lon_e7;`. `parse_unicast_inner` reads the 6 B when the flag is set.
- `node_mac` `enqueue_data` (and the same-layer DM origination): set `DATA_FLAG_LOCATION` **iff** `_cfg.loc_in_dm && (_cfg.lat_e7 || _cfg.lon_e7)`, and pass the coords. **Do NOT set it on relays/forwards or on E2E-acks** — only when THIS node originates an app DM. (Cross-layer origination may set it too, same condition.)

## 4. M send + parse (`frame_codec` + `node_channel`)
- `m_in`/`m_out`: document `flavor` as a flags byte (b0..2 variant, b3 `0x08` = LOCATION, b4..7 rsv); add `bool has_location; int32_t lat_e7, lon_e7;` to `m_out`.
- `pack_m`: when `in.flavor & 0x08`, write `pack_loc6` immediately after `channel_msg_id` (header 7 B → 13 B). `parse_m`: read it when the bit is set (and bump the min-length check accordingly).
- `node_channel` M origination: set `flavor |= 0x08` + pass coords **iff** `_cfg.loc_in_m && (_cfg.lat_e7 || _cfg.lon_e7)`. The RTS-M `payload_len` already covers the M body length — confirm it still sizes the overhear retune window correctly with the +6 B header (it announces the *frame* body; verify the +6 is counted).

## 5. Receive side — surface to the app
- On a DATA `msg_recv` with `has_location`, and on an M receive with `has_location`: attach the coords to the `Push` (add `bool has_location; int32_t lat_e7, lon_e7;` to the `Push` POD) so the companion gets them (the companion already renders location). Emit a telemetry `peer_location` (origin/hash + lat_e7/lon_e7) for the sim/gate to assert on.
- **Optional (recommended, can be a follow-up):** a small firmware cache `PeerLocation _peer_locs[cap_peer_locations≈16] { uint32_t key_hash32; int32_t lat_e7, lon_e7; uint64_t heard_ms; bool valid; }`, keyed by `source_hash` (DATA) / origin (M), evict-oldest + aged — for firmware-side use. Not required for v1 if the companion holds the map; note the decision in a comment.

## 6. ★ Keystone (the hard gate)
The toggles default **off**, and the location flag/slot is added **only** when a toggle is on AND the location is non-zero. So a single-layer node with no location config never emits the flag → **s18 must stay BYTE-IDENTICAL** (779015 / md5 `77205506d944af2eec03b8c9aac405bc`). `pack_unicast_inner`'s new params, when the flag is absent, must produce the exact same bytes. This is the keystone — verify before/after.

## 7. Test plan
- **Native:** `pack_loc6`/`unpack_loc6` round-trip incl. clamping at the poles/antimeridian and the ~11 m quantization error bound; `pack/parse_unicast_inner` with LOCATION set (round-trips coords) AND **unset (byte-identical to today)**; `pack/parse_m` with the `flavor` 0x08 bit; `enqueue_data` sets the flag iff `loc_in_dm && location!=0` (and NOT on a forward/ack); the `Push` carries the coords on receive.
- **Sim:** a 2-node scenario, sender `loc_in_dm`/`loc_in_m` on → assert the receiver emits `peer_location` with the sender's coords (within the ~11 m bound); a control with toggles off → no `peer_location`, and **s18 keystone byte-identical**.
- **Builds:** native + 3 boards (the field adds ~16 B to NodeConfig + Push — trivial).

## 8. Gate criteria
native (incl. the round-trip + the *unset = byte-identical* unit) + 4 builds + **s18 byte-identical** + the sim scenario shows location delivered on DM and M + read the codec (int64 lon path) + confirm the flag is set ONLY on origination with a non-zero location (never on relays/acks/forwards).
