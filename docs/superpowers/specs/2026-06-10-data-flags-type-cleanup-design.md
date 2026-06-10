# DATA Frame Flags/Type Cleanup — Design

**Status:** DESIGN (shape approved 2026-06-10; not yet implemented)
**Touches:** the core DATA wire format (`frame_codec.h/.cpp`), its consumers (`node_mac.cpp`,
`node_mac_rx.cpp`), `frames.md`, and the DATA tests. A deliberate wire change with **no
migration** (all peers run this C++; 3 physical test nodes) — like the `visited[]` removal.

## 1. Problem

Today the DATA frame mixes *flags* and *types* across two bytes and two layers:
- **byte 1 (header, high nibble):** `PRIORITY` (flag), `E2E_IS_ACK` (type), `E2E_ACK_REQ` (flag).
- **`inner[0]` (payload-flags byte):** `CROSS_LAYER` (flag), `H_ANSWER` (type), `AUTHORITATIVE`
  (type-qualifier), `CRYPTED` (flag), `SOURCE_HASH` (flag, doc-only), `DST_HASH` (flag).

Flags (independent, combinable modifiers) and types (mutually-exclusive message kinds) are
interleaved within each byte, and the type bits are split across the header and the inner. The
`inner[0]` payload-flags byte also has to stay cleartext under `CRYPTED` so relays can read
`H_ANSWER`/`DST_HASH` — a special case.

## 2. Design — one flags byte + a conditional type byte

**byte 1 becomes a full flags byte** holding every combinable bit plus a new **`APP`** gate.
**When `APP=1`, a single TYPE byte at byte 8** carries the special meanings as an enum (1..255).
The `inner[0]` payload-flags byte is **removed**: its flag-bits move to byte 1, its type-bits
become the TYPE enum.

### Wire layout

```
byte 0     : cmd=0x3(7..4) | addr_len(3..1) | rsv(0)
byte 1     : FLAGS  (see table)
byte 2     : next                       ┐ fixed routing header — relays read these at
byte 3     : dst                        │ constant offsets regardless of APP
byte 4     : hops_remaining(7..3) | committed_hops(2..0)
byte 5     : prev_fwd_rt_hops           │
bytes 6-7  : ctr (16-bit, LITTLE-endian)┘
byte 8     : TYPE   (present IFF APP=1; enum 1..255)
bytes 8/9..: inner  (no payload-flags byte; layout determined by the byte-1 flags)
last 4     : MAC
```

`DATA_HDR_LEN` stays **8** (the fixed part). `inner_off = 8 + (APP ? 1 : 0)`. The TYPE byte sits
exactly where the old `inner[0]` was — promoted from the inner into the cleartext header.

### byte 1 — FLAGS (full byte)

| bit | mask | flag | status |
|----|------|------|--------|
| b7 | `0x80` | `APP` | **new** — a TYPE byte follows the header |
| b6 | `0x40` | `CROSS_LAYER` | reserved (R7 — inner carries a layer-path) |
| b5 | `0x20` | `CRYPTED` | reserved (E2E — inner body sealed) |
| b4 | `0x10` | `E2E_ACK_REQ` | live (request an end-to-end ack) |
| b3 | `0x08` | rsv | free |
| b2 | `0x04` | `SOURCE_HASH` | reserved (inner carries origin's key_hash32) |
| b1 | `0x02` | `DST_HASH` | live (inner carries recipient's key_hash32) |
| b0 | `0x01` | `PRIORITY` | decoded (no behavior wired yet) |

Reserved flags keep their wire position now; behavior lands with their feature (matches today's
`CROSS_LAYER`/`CRYPTED`). `SOURCE_HASH` (currently doc-only) becomes a real reserved bit here.

### byte 8 — TYPE (enum, present IFF APP=1)

| code | type | inner shape |
|------|------|-------------|
| 0 | *(reserved / invalid — never on the wire; APP=0 means no TYPE byte)* | — |
| 1 | `H_ANSWER` | `[target_layer 1][node_id 1][key_hash32 4 LE]` (6 B) |
| 2 | `AUTHORITATIVE_H_ANSWER` | same as `H_ANSWER`; authoritative |
| 3 | `E2E_ACK` | normal-unicast inner, `body` = acked `ctr` (2 B LE) |
| 4..255 | future | WANT_PUBKEY answer, gateway-envelope, … |

`AUTHORITATIVE` stops being an independent bit — it's folded into the code
(`H_ANSWER` vs `AUTHORITATIVE_H_ANSWER`). `E2E_IS_ACK` stops being a byte-1 flag — it's the
`E2E_ACK` type.

### Inner layouts (no payload-flags byte)

- **Normal DM (APP=0):**
  `[dst_key_hash32 4B LE — iff DST_HASH] [layer-path — iff CROSS_LAYER] [origin 1B] [source_hash 4B LE — iff SOURCE_HASH] [body…] [Poly1305 tag 16B — iff CRYPTED]`.
  The presence of each optional field is read from the **byte-1 header flags**, not a payload byte.
- **H_ANSWER / AUTHORITATIVE_H_ANSWER (APP=1):** `[target_layer][node_id][key_hash32 4 LE]` (6 B).
- **E2E_ACK (APP=1):** the normal-unicast inner with `body` = the acked `ctr`. Flags still compose
  (e.g. a cross-layer ack = `APP` + `TYPE=E2E_ACK` + `CROSS_LAYER`).

## 3. Why this is cleaner

1. **Flags vs types are now separated by kind, not by byte** — combinable modifiers in byte 1,
   mutually-exclusive meanings in the TYPE enum.
2. **`CRYPTED` simplifies** — flags + type live in the always-cleartext header, so the old "the
   inner[0] payload-flags byte must stay clear under CRYPTED" special case disappears. `CRYPTED`
   seals only the inner body.
3. **Fixed routing header** — `next`/`dst`/`hops`/`ctr` stay at bytes 2–7; relays never branch on
   APP to route. The TYPE byte is read only by endpoints / cache-on-pass snoopers that already
   look past the routing fields.
4. **Wire size:** normal DMs **shrink 1 B** (the payload-flags byte is gone, no TYPE byte when
   APP=0); special/APP frames are unchanged (TYPE +1 B offsets the removed payload-flags byte).
   Compounds with the `visited[]` removal — normal DMs are now lean.

## 4. Codec API changes (`frame_codec.h/.cpp`)

- **`DataFlag` enum** → the full byte-1 layout above (new `APP`; `CROSS_LAYER`/`CRYPTED`/
  `SOURCE_HASH`/`DST_HASH` relocate here from the old `PayloadFlag`).
- **New `DataType` enum** (`H_ANSWER=1`, `AUTHORITATIVE_H_ANSWER=2`, `E2E_ACK=3`).
- **`PayloadFlag` enum is deleted** (its bits split into `DataFlag` + `DataType`).
- **`data_in`** gains `uint8_t type` (0 = normal DM, no APP) plus the byte-1 flag fields.
  **`pack_data` derives `APP` from `type`**: `type != 0` sets the `APP` bit *and* emits the TYPE
  byte at offset 8. The caller never sets `APP` by hand, so flag and type can't disagree.
- **`data_out`** gains `bool app, cross_layer, crypted, e2e_ack_req, source_hash, dst_hash,
  priority` and `uint8_t type`; `e2e_is_ack` becomes derived (`type == E2E_ACK`); `inner_off`
  becomes `8 + (app ? 1 : 0)`.
- **`parse_unicast_inner`** takes the header flags (DST_HASH/CROSS_LAYER/SOURCE_HASH/CRYPTED) as
  input instead of reading a payload-flags byte; the `src_addr_len != 0` reject is removed (no such
  byte anymore).
- **`pack_hash_bind_inner` / `parse_hash_bind_inner`** drop the leading payload-flags byte (6 B,
  was 7); H_ANSWER/AUTHORITATIVE are signalled by the frame TYPE, not the inner.

## 5. Consumer changes

- **`node_mac.cpp`** — `send_e2e_ack` sets `APP` + `type = E2E_ACK` (was `flags = E2E_IS_ACK`);
  the `DST_HASH` send path sets the byte-1 flag (was the payload-flags byte).
- **`node_mac_rx.cpp`** — the three checks move from flag/inner-bit to the TYPE byte:
  - `pa.flags & E2E_IS_ACK` → `pa.type == E2E_ACK` (`:499`).
  - `inner[0] & H_ANSWER` (consume `:494`, snoop `:541`) → `pa.type == H_ANSWER ||
    pa.type == AUTHORITATIVE_H_ANSWER`.
  - `E2E_ACK_REQ` stays a byte-1 flag (`:537`) — unchanged in meaning.

## 6. Test plan

- **`test_frame_codec.cpp`** — rework the DATA golden (new byte-1 byte, TYPE byte for an APP
  frame, no payload-flags byte), the round-trip (flags + type combinations, APP=0 vs APP=1
  offsets), the reject/min cases; rework the hash-bind inner round-trip (6 B, typed via TYPE); the
  unicast-inner tests (layout from flags). The byte-1 flag-bit isolation test updates to the new
  positions.
- **`test_node_r3.cpp` / hashlocate / mac tests** — update the DATA builders to set `type` instead
  of the `E2E_IS_ACK`/`H_ANSWER` payload bits; assertions on the e2e-ack / hash-answer paths read
  the TYPE.
- **Verify:** native suite green; both boards; a DM-delivery + E2E-ack + hash-answer sanity run in
  the sim (behavior unchanged — this is a wire reshaping, not a logic change).

## 7. Deliberate decisions / non-goals

- **TYPE at byte 8 (not right after flags)** — keeps the routing header fixed; one branch only at
  the endpoints, none on the relay hot path.
- **Keep the not-yet-live flags reserved** (`CROSS_LAYER`/`CRYPTED`/`SOURCE_HASH`) — wire positions
  locked now, behavior later; cheaper than re-laying the byte when they land.
- **`APP` gates the TYPE byte** so the common case (normal user DM) pays 0 type bytes and the
  frame stays 1 B smaller.
- **No migration / compat shim** — wire change, all-C++ peers (deliberate divergence from the
  frozen Lua, like `visited[]` / `data_sf` / the M frame).
- **`PRIORITY` stays decoded-only** — this cleanup does not wire its behavior (out of scope).

## 8. Open / to-confirm

- Bit order within byte 1 and the type-code numbers are as shown — confirm or adjust on review.
- `0` TYPE is reserved/invalid (APP=0 ⇒ no byte); future codes append from 4.
