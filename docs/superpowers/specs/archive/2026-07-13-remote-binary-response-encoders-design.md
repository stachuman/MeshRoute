<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Remote Binary Response Encoders — design

**Status:** design (2026-07-13, brainstorming output). **Prerequisite for** `2026-07-13-remote-management-auth-design.md` — that spec wires these encoders into the authenticated remote transport; this spec delivers only the encoders + decoders + native tests, standalone.

## 1. Problem
Remote (`rcmd`) responses ride a single **`REMOTE_RESP` DM ≤ 241 B** (`protocol::inbox_max_body`). The console's text/JSON responses overflow it (a full `cfg` is ~600 B, `status` ~300 B), which is why `remote_exec` today hand-rolls compact single-line text per verb — a reimplementation divorced from the real handlers (the drift the command-sink consolidation, `2026-07-13-command-sink-consolidation-design.md`, set out to kill). We want a **compact, structured, forward-compatible** response format that fits the DM and is decoded by the remote tool (and the harness) without brittle text parsing.

**Decision (2026-07-13): binary is REMOTE-ONLY.** USB keeps its (now-slimmed) text; BLE keeps its JSON (the iOS companion contract is untouched). So there is no BLE framing problem and no "shared sink" constraint — the encoder simply fills the remote DM buffer.

## 2. Scope & decomposition
A standalone module **`lib/console/console_binary.{h,cpp}`**, sitting beside `console_json.h` and serializing the **same field structs** (`StatusFields`, `LimitsFields`, `RouteRow`, `CfgExtras`, + a new `GatewayFields`) into compact **TLV**. Because it lives in `lib/console/` (not `fw_main`), it is **native doctest-testable** — encode→decode round-trips run in the host suite.

Built **now, standalone**. The remote-auth spec later calls `enc_*` to fill the `REMOTE_RESP` DM and ships the matching `dec_*` in the remote tool. **Out of scope here:** how remote *invokes* a binary variant (a flag on the query), the auth/allow-list, the transport, and any USB/BLE change.

**Verbs (7 data responses):** `status`, `cfg`, `duty`, `limits`, `faults`, `routes`, `gateway`. `uptime` (one u32) and `version` (mostly strings) stay text — binary barely helps; `reboot`/`prep-restart` are actions with a literal `ok` ack, not data.

## 3. Wire format — TLV
```
frame  = [ver u8=1][msg_type u8][ TLV field ]*                 // ver=1; msg_type per §3.1
TLV    = [tag u8][len u8][value, len bytes, little-endian]     // len ≤ 255; multi-byte ints LE
```
- **Forward-compatible:** a decoder skips any `tag` it doesn't know (advance by `len`). Adding a field = a new tag — **no `ver` bump**. `ver` is reserved for a breaking layout change (e.g. the frame header itself).
- **Endianness:** little-endian throughout (matches the host + the nRF52/ESP32 targets; no float on the wire — newlib-nano has no `%f`/`%lld`, so floats are pre-scaled to ints, mirroring `CfgExtras.freq_hz`/`duty_x1000`/`lat_e7`).
- **Absent/unavailable fields are OMITTED** (e.g. `batt_mv < 0`), never sent as a sentinel — the decoder treats a missing tag as "not present".
- **Lists (`routes`, `faults`) — fit-N + truncated flag (§3.2).**

### 3.1 `msg_type` registry
`0x01 status · 0x02 cfg · 0x03 duty · 0x04 limits · 0x05 faults · 0x06 routes · 0x07 gateway`. (Room to 0xFF.)

### 3.2 Lists: fit-N + `truncated`
A list frame packs as many records as fit `cap` (241 B minus the header), each record a `[tag=RECORD][len][record-body TLVs or fixed pack]`. A trailing **`tag=0xFE truncated (u8 = records_omitted, saturating)`** field signals the table didn't fit (0 = complete). No multi-DM streaming in v1 — remote diag rarely needs the whole table, and a continuation cursor is a later addition (a new tag, no `ver` bump). The encoder returns the count packed so the caller can log the drop.

### 3.3 Per-verb tag registries (the field set each `enc_*` emits; superset — emit what's available)
- **status (0x01)** — the remote-diag superset (companion `StatusFields` ∪ `remote_exec` status ∪ `dump_status`): `01 uptime_s u32 · 02 rx u32 · 03 tx u32 · 04 txq u16 · 05 txdrop u16 · 06 txto u32 · 07 rxbad u32 · 08 isr u32 · 09 rxarm u32 · 0A routes u8 · 0B duty_ms u32 · 0C pending u8 · 0D lbt u8 · 0E halted u8 · 0F slept u32 · 10 stackhw u16 · 11 reset_cause u8 · 12 batt_mv i16(omit if <0) · 13 nf_dbm i8 · 14 id u8 · 15 key u32`.
- **cfg (0x02)** — `NodeConfig` + `CfgExtras`: `node_id u8 · freq_hz u32 · routing_sf u8 · sf_list u16(bitmap) · bw u32 · cr u8 · tx_power i8 · duty_x1000 u32 · beacon_ms u32 · hop_cap u8 · lbt u8 · nav u8 · intra_relay u8 · host_mobiles u8 · leaf_id u8 · is_gateway u8 · is_mobile u8 · team_id u32 · lineage_id u16 · config_epoch u16 · ble_mode u8 · ble_period u16 · ble_pin u32 · loc_dm u8 · e2e_dm u8 · lat_e7 i32 · lon_e7 i32` (each its own tag; a busy `cfg` may exceed 241 → the encoder returns 0/overflow and the caller trims via a field-mask later — flagged §6).
- **duty (0x03)** — `pct u8 · avail_ms u32 · enabled u8`.
- **limits (0x04)** — the 13 `LimitsFields` u32, one tag each (`win_ms · win_left_ms · n · ch_sf · ch_cap · ch_used · ch_min_ms · ch_next_ms · ch_ceiling · dm_min_ms · dm_next_ms · duty_ms · duty_used_ms`).
- **faults (0x05)** — list of records; each record: `cause u8 · pc u32 · lr u32 · count u16` (the `mrfault::FaultRecord` core) + the fit-N `truncated`.
- **routes (0x06)** — list of `RouteRow` records; each: `dest u8 · next u8 · hops u8 · score i16 · flags u8(bit0=gw) · leaf u8 · age_ms u32 · cand u8` + `truncated`.
- **gateway (0x07)** — new `GatewayFields` (this node's gateway config + schedule): `n_layers u8 · window_period_ms u32`, then per-leaf (0..n_layers-1) a record: `layer_id u8 · node_id u8 · routing_sf u8 · sf_list u16 · bw u32 · cr u8 · window_ms u32 · window_offset_ms u32`.

## 4. API
Mirror the `console_json` `write_*` shape — a bounded buffer, return bytes written (**0 = overflow**, never a partial frame):
```cpp
namespace meshroute::console::bin {
  // scalars/structs
  size_t enc_status (uint8_t* buf, size_t cap, const StatusFields& s, uint8_t id, uint32_t key, /*extra diag*/ const StatusDiag& d);
  size_t enc_cfg    (uint8_t* buf, size_t cap, const NodeConfig& c, const CfgExtras& x);
  size_t enc_duty   (uint8_t* buf, size_t cap, uint8_t pct, uint32_t avail_ms, bool enabled);
  size_t enc_limits (uint8_t* buf, size_t cap, const LimitsFields& L);
  // lists (fit-N): caller passes a getter + count; encoder packs until cap, sets truncated
  size_t enc_routes (uint8_t* buf, size_t cap, const RouteRow* rows, uint8_t n, uint8_t* out_truncated);
  size_t enc_faults (uint8_t* buf, size_t cap, const FaultRow* rows, uint8_t n, uint8_t* out_truncated);
  size_t enc_gateway(uint8_t* buf, size_t cap, const GatewayFields& g);
  // a matching decoder for the native tests + the remote tool: walks TLVs, fills an out-struct, skips unknown tags.
  bool   dec_status (const uint8_t* buf, size_t len, StatusOut& out);   // ... one dec_* per msg_type
}
```
A tiny internal TLV writer (`put_u8/u16/u32/i16/i32(buf,cap,off,tag,val)` → advances `off`, returns false on overflow) keeps each `enc_*` a flat, readable list of `put_*` calls. `StatusDiag`/`FaultRow`/`GatewayFields`/`*Out` are small plain structs defined in the header (the encoders stay dependency-light — the caller in `fw_main`/remote gathers the live values, exactly as `write_status` takes `StatusFields`).

## 5. Testing (native doctest — the regression backbone)
Per verb: build the input struct with representative values, `enc_X`→`dec_X`, assert **every field round-trips** and **`len ≤ 241`** for a realistic payload. Plus: an **overflow test** (tiny `cap` → returns 0, no OOB write — ASAN clean); a **forward-compat test** (hand-craft a frame with an extra unknown tag → `dec_X` skips it and still fills the known fields); a **list truncation test** (N records > cap → `truncated` set, packed count correct). No device needed — this is why the module lives in `lib/console/`.

## 6. Out of scope / deferred (→ remote-auth spec or later)
- **Invocation:** how a remote query selects binary vs text (a flag byte on the `rcmd` query), the allow-list, auth, the transport wiring. The remote-auth spec owns this.
- **`cfg` overflow:** the full `cfg` field set may exceed 241 B. v1 returns 0 (overflow) if so; a **field-mask** (the query names which cfg fields it wants) is the natural follow-on — a new query param, not a format change.
- **Multi-DM streaming** for large `routes`/`faults` (a continuation cursor) — v1 is fit-N + truncated.
- **USB/BLE binary** — explicitly not wanted; those stay text/JSON.

## 7. Self-review
- **Placeholders:** none — the deferred items (§6) are explicit non-goals with their owning spec, not gaps.
- **Consistency:** the encoders mirror `console_json`'s `write_*` (bounded buf, 0-on-overflow) and reuse its field structs; TLV + skip-unknown gives the forward-compat the "self-describing" choice was made for; little-endian + int-scaled floats match the existing `CfgExtras` convention.
- **Scope:** one focused module (encoders + decoders + tests), no transport/auth — sized for a single plan, and independently useful/testable before remote-auth exists.
- **Ambiguity:** "which status fields" is resolved by the superset registry (§3.3) — the encoder emits what it's given; the decoder tolerates any subset. Lists are fit-N + `truncated` (§3.2), decided, not open.
