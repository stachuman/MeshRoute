# C2 — RTS + NACK + Q codecs (§10 cmd-nibble) — design proposal

**Date:** 2026-05-29  **Status:** PROPOSAL — awaiting review, no code written.
**Track:** codec track, after C0/C1 (CTS/ACK). MeshRoute-only (`lib/core`, `test/`);
verified by `pio test -e native`. Layouts verified against `dv_dual_sf.lua`
pack_*/parse_* + ROADMAP §10.3; field semantics from the Lua, byte positions from §10.

---

## 0. Wire-decision spike (folded in) — result

Computed SF8/BW125/CR5/pre16 airtime by length (buckets step every 4 B):
`L3-6→78ms · L7-10→88ms · L11-14→98ms · L15-18→109ms`. Per-frame size change:

| frame | Lua→§10 | SF8 airtime | verdict |
|---|---|---|---|
| **RTS** | 8→7 (mbcast 10→9) | 88→88 | **neutral** |
| **NACK** | 4→4 | 78→78 | neutral |
| **Q** | 4→4 (+body) | 78→78 | neutral |
| CTS/ACK/DATA | unchanged | — | neutral |
| H | 8→6 | 88→**78** | **−10ms (C3 decision)** |
| F | 6→5 | 78→78 | neutral |

**All three C2 frames are airtime-neutral at SF8** → the §10 shortening is free
(clarity + header room), and `dm_delivery_breakdown` should show **no size-driven
delta** vs the Lua when these frames are exercised (R-track). The only
airtime-mover in the whole §10 set is **H→6B** (−10ms, rare gateway frame) — a
C3 call (§10.6: collapse for the saving vs keep the flag byte for SF8 parity).

---

## 1. Open decision (BLOCKS RTS golden-hex) — §10.3 byte-5 bit packing

§10.3 writes RTS byte 5 as `sf_index(2 hi) | rts_flags(4 lo) | rsv(2)` — which is
**internally contradictory** (2+4+2 = 8 bits, but "rts_flags(4 **lo**)" and
"rsv(2)" can't both be at the bottom). Two readings:

- **(A) — recommended:** `sf_index` = bits 7..6, `rts_flags` = bits **5..2**,
  `rsv` = bits 1..0. (sf_index at top, rsv at bottom, flags in the middle.)
  Then within byte5: M_BROADCAST→bit2 (0x04), RELAY→bit3 (0x08); decode flags via
  `(b5 >> 2) & 0x0F`.
- **(B):** `sf_index` = bits 7..6, `rsv` = bits 5..4, `rts_flags` = bits **3..0**
  (literal "low nibble"). Then M_BROADCAST→bit0 (0x01), RELAY→bit1 (0x02).

The golden vectors below assume **(A)**. This is yours to pin (you own §10.3) — a
one-line clarification to ROADMAP §10.3 settles it. NACK and Q have no such
ambiguity. (I can implement NACK+Q immediately and hold RTS for the byte-5 call,
or do all three once you confirm — your preference.)

---

## 2. Per-frame §10 layout + field contract (verified)

### RTS — cmd `0x1`, 7 B (+2 B if M_BROADCAST) — Lua pack_rts:2037 / parse_rts:2072
```
byte 0: cmd=0x1(7..4) | leaf_id(3..0)
byte 1: src
byte 2: next
byte 3: ctr_lo(7..4) | addr_len(3..1) | rsv(0)
byte 4: dst                                  (addr_len=0 only; hierarchy deferred)
byte 5: sf_index(7..6) | rts_flags(5..2, reading A) | rsv(1..0)
byte 6: payload_len
byte 7-8: id_lo16 (BE)  — ONLY if rts_flags & M_BROADCAST
```
Flags: `M_BROADCAST=0x01`, `RELAY=0x02` (within the 4-bit flags field).
**Contract — all MASK/wrap, NO clamp** (the RTS path has no saturating field, unlike
ACK's budget_hint): `leaf_id &0xF`, `addr_len &0x7`, `ctr_lo &0xF`, `rts_flags &0xF`,
`sf_index &0x3`, `id_lo16 &0xFFFF`. **`payload_len` wraps mod-256** (Lua `% 256`,
2049) — 256→0, NOT clamp-to-255. Parse REJECTS: len<7, cmd≠0x1, `addr_len≠0` (2076).
M_BROADCAST ext read only if flag set & len≥9. `sf_index` 0..2 = singleton, 3 = ANY
(no reject; subset→ANY fallback is protocol-layer).

### NACK — cmd `0x5`, 4 B — Lua pack_nack:2291 / parse_nack:2299
```
byte 0: cmd=0x5(7..4) | reason(3..0)
byte 1: ctr_lo(7..4) | rsv(3..0)
byte 2: payload   (reason-specific)
byte 3: to
```
Reasons: `BUSY_RX=0, BUDGET=1, HOP_BUDGET=2, LOOP_DUP=3`. **Contract:** `reason &0xF`,
`ctr_lo &0xF` (mask). `payload` is a `uint8_t` so it's inherently 0..255 — the Lua's
explicit clamp-to-[0,255] (2294) is satisfied by the type (no clamp code needed; note
in a comment). The reason-specific sub-encodings of `payload` (BUSY_RX = busy_ms/16
ceil + 65535/255 clamps; BUDGET = tier|headroom; HOP_BUDGET = committed<<4; LOOP_DUP =
prior_from) are **protocol-layer**, NOT the codec — the codec packs the byte verbatim.
Parse rejects len<4 / cmd≠0x5.

### Q — cmd `0x6`, 4 B header (+ CHANNEL_PULL body) — Lua pack_q:2365 / parse_q:2382
```
byte 0: cmd=0x6(7..4) | leaf_id(3..0)
byte 1: src
byte 2: dest                                 (0xFF = REQ_SYNC broadcast convention)
byte 3: opcode(7..6) | mobile(bit5) | rsv(4..0)
[CHANNEL_PULL only:] byte 4: count ; then count × channel_msg_id (4 B BE)
```
Opcodes: `REQ_SYNC=1, CHANNEL_PULL=3` (0/2 reserved). `channel_msg_id` is **BIG-ENDIAN**
(`origin<<24 | key_hash32_low16<<8 | ctr`) — distinct from the LE key_hash32 elsewhere;
**keep it BE.** **Contract:** `leaf_id &0xF`, `opcode &0x3` (mask), `mobile` bit-flag
(0x04 in the Lua flags nibble → bit5 in §10), `count &0xFF`. Parse rejects len<4 /
cmd≠0x6, and for CHANNEL_PULL rejects `len < 5 + count*4` (truncated id list).

---

## 3. Golden-hex vectors (verified; pinned in tests)

**RTS (reading A):**
- plain unicast `{leaf=2,src=0x0A,next=0x0B,ctr_lo=5,dst=0x0C,sf_index=3,flags=0,plen=20}`
  → `12 0A 0B 50 0C C0 14`
- M_BROADCAST `{leaf=1,src=0x07,next=0x09,ctr_lo=0xF,dst=0xFF,sf_index=2,flags=M_BROADCAST,plen=200,id=0x…5678}`
  → `11 07 09 F0 FF 84 C8 56 78`  (byte5 `0x84`: sf_index2 | flags 0x01<<2)
- RELAY+M_BCAST, plen wrap `{leaf=0,…,sf_index=0,flags=0x03,plen=256→0,id=0x00FF}`
  → `10 14 15 00 16 0C 00 00 FF`  (payload_len byte = `0x00`, wrapped not clamped)

**NACK:**
- BUSY_RX `{reason=0,ctr_lo=5,payload=5,to=0x11}` → `50 50 05 11`
- HOP_BUDGET `{reason=2,ctr_lo=0xA,payload=0x30,to=0x07}` → `52 A0 30 07`
- LOOP_DUP `{reason=3,ctr_lo=1,payload=0xFF,to=0x09}` → `53 10 FF 09`

**Q:**
- REQ_SYNC mobile `{leaf=1,src=0x14,dest=0xFF,opcode=1,mobile=1}` → `61 14 FF 60`
- CHANNEL_PULL 1-id `{leaf=2,src=0x14,dest=0x21,opcode=3,mobile=0, id=0x14123407}`
  → `62 14 21 C0 01 14 12 34 07`  (4 hdr + count `01` + BE id `14 12 34 07`)

---

## 4. API (`frame_codec.h`)

```cpp
// RTS — cmd 0x1
constexpr uint8_t RTS_FLAG_M_BROADCAST = 0x01;
constexpr uint8_t RTS_FLAG_RELAY       = 0x02;
struct rts_in  { uint8_t leaf_id, src, next, ctr_lo, dst, sf_index, rts_flags, payload_len;
                 uint16_t m_payload_id_lo16; };   // id used iff rts_flags & M_BROADCAST
struct rts_out { uint8_t leaf_id, src, next, ctr_lo, addr_len, dst, sf_index, rts_flags, payload_len;
                 bool m_broadcast; uint16_t m_payload_id_lo16; };
size_t pack_rts(const rts_in&, std::span<uint8_t> out);          // 7 or 9; 0 on short buf
std::optional<rts_out> parse_rts(std::span<const uint8_t> frame); // nullopt: len/cmd/addr_len!=0

// NACK — cmd 0x5
struct nack_in  { uint8_t reason, ctr_lo, payload, to; };
struct nack_out { uint8_t reason, ctr_lo, payload, to; };
size_t pack_nack(const nack_in&, std::span<uint8_t> out);        // 4; 0 on short buf
std::optional<nack_out> parse_nack(std::span<const uint8_t> frame);

// Q — cmd 0x6 (+ CHANNEL_PULL body)
enum class q_opcode : uint8_t { req_sync = 1, channel_pull = 3 };
struct q_in  { uint8_t leaf_id, src, dest; q_opcode opcode; bool mobile;
               std::span<const uint32_t> channel_ids; };  // ids only for channel_pull
struct q_out { uint8_t leaf_id, src, dest, opcode; bool mobile; uint8_t channel_id_count; };
size_t pack_q(const q_in&, std::span<uint8_t> out);              // 4 (+1+4N for pull); 0 on short buf
std::optional<q_out> parse_q(std::span<const uint8_t> frame);
std::optional<uint32_t> parse_q_channel_id(std::span<const uint8_t> frame,
                                           const q_out&, uint8_t index);  // i-th BE id
```
Uses C0's `wire::Writer/Reader` (`u32_be` for channel_msg_id, `u16_be` for id_lo16).

---

## 5. Tests (`test_frame_codec.cpp`, CHECK-only, append to C1's)

- **Round-trip** sweeps: RTS over leaf/ctr_lo/sf_index{0..3}/flags{0,M,R,M|R}/dst, both
  no-ext and M_BROADCAST(+id); NACK over reason{0..3}/ctr_lo/payload/to; Q over
  leaf/opcode{1,3}/mobile, CHANNEL_PULL with 0/1/3 ids (BE round-trip).
- **Golden-hex:** the 8 vectors in §3 (pin byte-exact §10 layout, incl. RTS byte5
  reading A, payload_len wrap, Q BE channel id).
- **Contract:** RTS `payload_len=256 → byte=0x00` (wrap, NOT 255); RTS sf_index/flags
  bit isolation; Q CHANNEL_PULL truncated body (`len < 5+4N`) → `parse_q` nullopt;
  wrong-cmd reject for each (`parse_rts` on a NACK frame → nullopt, etc.).

## 6. Verification gate

- `pio test -e native` green (C1's 23 cases + new RTS/NACK/Q cases).
- Sim: `meshroute_core` recompiles `frame_codec.cpp`; the meshroute Node doesn't call
  these yet → suite 82/82, delivery unaffected (and §0 confirms airtime-neutral).

## 7. Files

`lib/core/frame_codec.h` (+ RTS/NACK/Q structs, sigs, RTS flag constants),
`lib/core/frame_codec.cpp` (+ 4 pack + 4 parse), `test/test_frame_codec.cpp` (+ cases).
`wire.h` unchanged (u32_be/u16_be already present). All uncommitted — you commit.

## 8. Open questions

1. **RTS byte-5 packing (§1) — BLOCKS RTS golden hex.** Reading A (flags bits 5..2) or
   B (flags bits 3..0)? Rec A; pin via a §10.3 clarification.
2. Implement all three now (pending #1), or **NACK+Q now / RTS after #1**?
3. NACK BUDGET `headroom` low-nibble is always 0 in the current producer — keep the
   field defined-but-zero (rec) or drop?
4. Confirm reserved bits (NACK byte1 lo nibble, Q byte3 lo 5 bits, RTS byte5 lo 2) are
   **zero-on-send / ignored-on-receive** (no validation) — rec yes.
5. Confirm `channel_msg_id` stays **BE** in the Q body (vs the LE key_hash32 elsewhere)
   — rec yes (it's a distinct field).
