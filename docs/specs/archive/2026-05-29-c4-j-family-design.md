# C4 — J join family codecs (§10 cmd-nibble) — design proposal

**Date:** 2026-05-29  **Status:** PROPOSAL — awaiting review, no code written.
**Track:** codec track, after C3 (H/F). MeshRoute-only; verified by `pio test -e
native`. Layouts verified against `dv_dual_sf.lua` pack_j_* / parse_j + §10.3.
The OTAA-style join + short-id lease family — 4 opcodes, fixed length per opcode.

---

## 0. Two framing notes

- **C4 is §10 cmd-nibble (cmd=0x9), NOT tag-byte `'J'`** — like every C++ codec.
  `byte 0 = cmd 0x9(7..4) | leaf_id(3..0)`. (The verification readers defaulted to
  the tag-byte form; the bodies are byte-identical, only byte0/byte1 differ, and
  the golden hex below is the cmd-nibble form.)
- **All J multi-byte fields are LITTLE-ENDIAN** (`pack_u32_le` / `pack_u16_le`):
  every `key_hash32`, and the `lease_age` u16. No BE anywhere in J.

---

## 1. Decision: byte-1 flag bit positions (RTS-byte5 class)

§10.3-proposed J byte 1 = `gateway_capable(1) | is_mobile(1) | opcode(2) | rsv(4)`
is ambiguous (the two §10.3 renderings disagree). Two readings:

- **(A) — recommended, consistent with RTS reading A:** top-down literal —
  `gateway_capable = bit 7 (0x80)`, `is_mobile = bit 6 (0x40)`, `opcode = bits 5..4`,
  `rsv = bits 3..0`.
- **(B):** preserve the Lua low-nibble positions (gw=bit3, mobile=bit2, opcode=bits1..0),
  rsv = the freed high nibble.

I recommend **A** — it matches the §10.3-proposed wording read top-down and the
RTS byte-5 precedent you already pinned (reading A). The golden hex below assumes A.
Your call (you own §10.3); a one-line §10.3 clarification settles it.

(Bit *values* are unchanged: opcodes DISCOVER=0, CLAIM=1, DENY=2, OFFER=3 —
**non-sequential**, hardcode them; `J_FLAG_MOBILE=0x04`/`GATEWAY_CAPABLE=0x08` are
the Lua masks but their on-wire position is per the reading above.)

---

## 2. Per-opcode §10 layout (verified) — header = byte0 `cmd0x9|leaf` + byte1 (reading A)

All offsets after the 2-byte header. Lengths are **exact** (parse rejects otherwise).

**DISCOVER (op 0, 6 B):** `key_hash32`(LE, off 2..5).
**OFFER (op 3, 8 B):** `responder_node_id`(2), `responder_key_hash32`(LE, 3..6), `data_sf_bitmap`(7).
**CLAIM (op 1, 11 B):** `key_hash32`(LE,2..5), `proposed_node_id`(6), `lease_age_seconds`(u16 LE,7..8), `claim_epoch`(9), `nonce`(10).
**DENY (op 2, 15 B):** `denied_node_id`(2), `owner_key_hash32`(LE,3..6), `claimant_key_hash32`(LE,7..10), `owner_lease_age_seconds`(u16 LE,11..12), `owner_claim_epoch`(13), `reason`(14).

**Field contract:** leaf_id/opcode mask; gw/mobile bit-flags; key_hash32 u32 mask;
node_ids/epoch/nonce/data_sf_bitmap/reason u8 (mask). **`lease_age_seconds`
SATURATES at 65535 — but at the PRODUCER** (`lease_age_seconds_now` clamps;
load-bearing for the older-lease-wins tie-break); the codec just takes a `uint16_t`
(type carries it, packs LE). `claim_epoch` is a wrapping u8 counter (NOT saturate).
`reason` passed through raw (codec doesn't range-check; forward-compat).

---

## 3. Golden hex (§10 cmd-nibble, reading A) — pinned in tests

- **DISCOVER** `{leaf=3, gw=1, mob=1, op=0, key_hash32=0x11223344}` → `93 C0 44 33 22 11`
  (byte0 `0x93`=cmd9|leaf3; byte1 `0xC0`=gw|mob|op0; key LE `44 33 22 11`)
- **OFFER** `{leaf=5, gw=1, mob=0, op=3, resp_node=0x2A, resp_kh=0xDEADBEEF, sf_bitmap=0x06}`
  → `95 B0 2A EF BE AD DE 06` (byte1 `0xB0`=gw|op3<<4)
- **CLAIM** `{leaf=5, gw=0, mob=1, op=1, kh=0xDEADBEEF, node=0x2A, lease=300, epoch=7, nonce=0x99}`
  → `95 50 EF BE AD DE 2A 2C 01 07 99` (byte1 `0x50`=mob|op1<<4; lease 300=`2C 01` LE)
- **DENY** `{leaf=5, gw=1, mob=0, op=2, denied=0x2A, owner_kh=0x11223344, claimant_kh=0xDEADBEEF, owner_lease=1000, owner_epoch=3, reason=3}`
  → `95 A0 2A 44 33 22 11 EF BE AD DE E8 03 03 03` (byte1 `0xA0`=gw|op2<<4; lease 1000=`E8 03`)

---

## 4. API (`frame_codec.h`)

```cpp
enum class j_opcode : uint8_t { discover = 0, claim = 1, deny = 2, offer = 3 };
constexpr uint8_t J_DENY_CONFLICT = 1, J_DENY_PENDING_CLAIM = 2, J_DENY_OWN_ID_DEFENSE = 3;

struct j_discover_in { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint32_t key_hash32; };
struct j_offer_in    { uint8_t leaf_id; bool gateway_capable; bool is_mobile;
                       uint8_t responder_node_id; uint32_t responder_key_hash32; uint8_t data_sf_bitmap; };
struct j_claim_in    { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint32_t key_hash32;
                       uint8_t proposed_node_id; uint16_t lease_age_seconds; uint8_t claim_epoch; uint8_t nonce; };
struct j_deny_in     { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint8_t denied_node_id;
                       uint32_t owner_key_hash32; uint32_t claimant_key_hash32;
                       uint16_t owner_lease_age_seconds; uint8_t owner_claim_epoch; uint8_t reason; };
size_t pack_j_discover(const j_discover_in&, std::span<uint8_t>);   // 6
size_t pack_j_offer   (const j_offer_in&,    std::span<uint8_t>);   // 8
size_t pack_j_claim   (const j_claim_in&,    std::span<uint8_t>);   // 11
size_t pack_j_deny    (const j_deny_in&,     std::span<uint8_t>);   // 15

// One parse returns opcode + the superset of fields (only the opcode's are valid),
// mirroring the Lua parse_j (one table). nullopt on wrong cmd / unknown opcode /
// wrong exact length.
struct j_out {
    uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint8_t opcode;
    uint32_t key_hash32;                                              // DISCOVER, CLAIM
    uint8_t  responder_node_id; uint32_t responder_key_hash32; uint8_t data_sf_bitmap;  // OFFER
    uint8_t  proposed_node_id; uint16_t lease_age_seconds; uint8_t claim_epoch; uint8_t nonce;  // CLAIM
    uint8_t  denied_node_id; uint32_t owner_key_hash32; uint32_t claimant_key_hash32;
    uint16_t owner_lease_age_seconds; uint8_t owner_claim_epoch; uint8_t reason;  // DENY
};
std::optional<j_out> parse_j(std::span<const uint8_t> frame);
```
Uses C0's `wire` (`u32_le`, `u16_le`). No `wire.h` change.

---

## 5. Tests (append to `test_frame_codec.cpp`, CHECK-only)

- **Round-trip per opcode:** pack_j_* → parse_j → fields match (sweep leaf, gw, mob,
  the LE u32/u16 fields incl. 0xDEADBEEF / 300 / 1000 to lock byte order). Each returns
  its exact length (6/8/11/15).
- **Golden hex:** the 4 vectors in §3 (cmd-nibble byte0 + reading-A byte1 + LE bodies).
- **Header isolation:** flipping gw / is_mobile / opcode changes only the expected
  byte1 bits (gw=0x80, mob=0x40, op=bits5..4).
- **Reject:** wrong cmd (parse_j on an RTS frame → nullopt); each opcode at the *wrong*
  length (e.g. a 7-B DISCOVER) → nullopt (strict exact-length, not `>=`); an unknown
  opcode value is impossible (2-bit field, all 4 used) but a truncated <6 B → nullopt.

## 6. Verification gate

`pio test -e native` green (C3's 30 cases + J cases). Sim: `meshroute_core`
recompiles; meshroute Node doesn't call J yet → 82/82, 0-diff (J is airtime-neutral
— sizes unchanged vs Lua).

## 7. Files

`lib/core/frame_codec.{h,cpp}` (+ J structs, enum, constants, 4 packers + parse_j),
`test/test_frame_codec.cpp` (+ cases). `wire.h` unchanged. Uncommitted — you commit.

## 8. Open questions

1. **byte-1 reading A** (gw=bit7, mob=bit6, op=bits5..4, rsv low) vs B (preserve Lua
   low-nibble) — rec **A** (consistent with RTS). Pin via §10.3 clarification.
2. `reason` passed through raw on parse (no 1..3 range-reject) — rec yes (forward-compat;
   matches Lua).
3. `nonce` is 1 B today; §10.6 flags it may widen to 4/8 B at §8.1 crypto lock-in — keep
   it isolated so widening is localized. Rec: 1 B now.
4. `parse_j` strict **exact length per opcode** (not `>=`) — rec yes (matches Lua).
5. `j_out` flat superset struct (mirrors Lua's one-table parse) vs per-opcode parse
   functions — rec flat (simplest, no-heap); consumer dispatches on `opcode`.
