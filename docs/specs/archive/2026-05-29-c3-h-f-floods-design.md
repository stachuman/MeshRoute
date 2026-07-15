# C3 — H (hash-locate) + F (RREQ/RREP) flood codecs (§10) — design proposal

**Date:** 2026-05-29  **Status:** PROPOSAL — awaiting review, no code written.
**Track:** codec track, after C2 (RTS/NACK/Q). MeshRoute-only; verified by
`pio test -e native`. Layouts verified against `dv_dual_sf.lua` pack/parse +
ROADMAP §10.3. H and F are the two forwardable TTL control floods (§3.7a/§3.7b).

---

## 0. CORRECTION to the C2 spike (H airtime)

I previously said "H 8→6B saves −10ms (the only §10 airtime-mover)." **That was a
miscount.** H's minimum is **7 bytes** (cmd|leaf + origin + key_hash32[4] + ttl) —
it can't be 6 B (key_hash32 alone is 4 B). At SF8, **7 B and 8 B are both in the
88ms bucket**, so:

| H form | wire bytes | SF8 airtime |
|---|---|---|
| current Lua | 8 | 88 ms |
| §10 keep-flag | 8 | 88 ms |
| **§10 drop-flag** | **7** | **88 ms — neutral** |

**Net: NO §10 frame changes SF8 airtime** (RTS/CTS/DATA/ACK/NACK/Q/F/H all
bucket-neutral at the locked plan). The H choice is **structural only** (compactness
vs flags headroom), not airtime — and dm_delivery parity vs the Lua is unaffected
by any of the §10 sizing.

---

## 1. H — hash-locate flood (cmd `0x7`)

Verified: Lua `pack_h_query`:2412 / `parse_h_query`:2420. **key_hash32 is
LITTLE-ENDIAN** (via `q_pack_u32_le`:2416). The flags nibble is **purely
reserved** — hard-zeroed on pack (2413), never read on parse → **dropping it
loses no functionality.**

**§10 drop-flag form — 7 B (recommended):**
```
byte 0   : cmd=0x7(7..4) | leaf_id(3..0)
byte 1   : origin              (querier node_id; PRESERVED across forwards)
bytes 2-5: key_hash32 (LE)
byte 6   : ttl                 (decremented per forward; dropped at 0)
```
Alternative keep-flag form (8 B) inserts an all-rsv `byte 2` before key_hash32.

**Contract:** `leaf_id &0x0F`, `origin`/`ttl` are u8 (the Lua's `ttl &0xff` /
clamp is satisfied by the type; config caps ttl ≤16). `key_hash32` opaque, never
clamped, LE on wire. Parse rejects len<7 / cmd≠0x7.

**Golden hex (7 B):** `{leaf=3, origin=0x2A, key_hash32=0xDEADBEEF, ttl=0x10}` →
`73 2A EF BE AD DE 10`  (byte0 `0x73`=cmd7|leaf3; key_hash32 LE = `EF BE AD DE`).
Keep-flag 8 B form would be `73 2A 00 EF BE AD DE 10` (one extra rsv `00`).

---

## 2. F — route-find RREQ/RREP flood (cmd `0x8`)

Verified: Lua `pack_r_request`:2444 / `pack_r_reply`:2452 / `parse_r`:2459. The
wire tag is `'F'` (not `'R'`=RTS); AODV naming is code-only. **6 B, §10 Δ=0.**

```
byte 0 : cmd=0x8(7..4) | leaf_id(3..0)
byte 1 : origin                (querier node_id; PRESERVED across forwards)
byte 2 : is_reply(bit 7) | rsv(6..0)        [0 = RREQ, 1 = RREP]
byte 3 : dst_id
byte 4 : ttl_or_next_hop  — RAW dual byte: ttl (RREQ) | next_hop (RREP)
byte 5 : hops                  (RREQ: from-origin↑ ; RREP: to-dst)
```

**Two codec-critical points (verified):**
1. **`is_reply` MOVES bit position:** Lua had it at byte2 **bit 0**; §10 puts it at
   byte2 **bit 7**. The codec must *re-place* it, not bit-copy the Lua `& 0x1`.
   (leaf_id likewise moves: Lua byte2-hi-nibble → §10 byte0-lo-nibble.)
2. **byte 4 is RAW / by-flag:** `parse_r` returns it verbatim and the *handler*
   interprets by `is_reply` (ttl: `≤0`/decrement; next_hop: `== self.id`). The
   codec MUST surface byte 4 as a raw `uint8_t` and **never interpret, clamp, or
   validate it** — clamping would corrupt a node address when `is_reply=1`.

**Contract:** `leaf_id &0x0F`, `origin`/`dst_id`/`hops` u8 mask, `is_reply` 1 bit,
byte4 raw u8 (mask only). Parse rejects len<6 / cmd≠0x8; rsv bits ignored on
receive (forward-compat).

**5 B collapse (§10.6): NOT worth it — recommend keep 6 B.** F at 5 B and 6 B are
both in the SF8 78ms bucket (zero airtime gain), and the collapse throws away the
only flag-extension surface F has and complicates the codec around the very field
(`is_reply`) whose position already shifts. Keep the clean fixed-offset 6 B.

**Golden hex:**
- RREQ `{leaf=3, origin=0x11, is_reply=0, dst=0x2A, ttl=8, hops=0}` → `83 11 00 2A 08 00`
- RREP `{leaf=3, origin=0x11, is_reply=1, dst=0x2A, next_hop=9, hops=4}` → `83 11 80 2A 09 04`
  (differ only in byte2 `0x00`↔`0x80` (is_reply bit 7) and byte4 `08` ttl ↔ `09` next_hop)

---

## 3. API (`frame_codec.h`)

```cpp
// H — cmd 0x7, 7 B (flag byte dropped, §10.6)
struct h_in  { uint8_t leaf_id; uint8_t origin; uint32_t key_hash32; uint8_t ttl; };
struct h_out { uint8_t leaf_id; uint8_t origin; uint32_t key_hash32; uint8_t ttl; };
size_t pack_h(const h_in& in, std::span<uint8_t> out);            // 7; 0 on short buf
std::optional<h_out> parse_h(std::span<const uint8_t> frame);     // nullopt: len<7 / cmd

// F — cmd 0x8, 6 B; byte 4 raw/by-flag (ttl for RREQ, next_hop for RREP)
struct f_in  { uint8_t leaf_id; uint8_t origin; bool is_reply; uint8_t dst_id;
               uint8_t ttl_or_next_hop; uint8_t hops; };
struct f_out { uint8_t leaf_id; uint8_t origin; bool is_reply; uint8_t dst_id;
               uint8_t ttl_or_next_hop; uint8_t hops; };
size_t pack_f(const f_in& in, std::span<uint8_t> out);            // 6; 0 on short buf
std::optional<f_out> parse_f(std::span<const uint8_t> frame);     // nullopt: len<6 / cmd
```
Uses C0's `wire::Writer/Reader` (`u32_le` for H key_hash32). No `wire.h` change.

---

## 4. Tests (append to `test_frame_codec.cpp`, CHECK-only)

- **H round-trip:** sweep leaf{0,3,15}, origin{0,42,255}, key_hash32 ∈ {0, 0xDEADBEEF,
  0xFFFFFFFF} (LE round-trip), ttl{0,1,16,255}. Golden `73 2A EF BE AD DE 10`.
  key_hash32 LE byte-order assertion. Reject len<7 + wrong-cmd.
- **F round-trip:** sweep leaf, origin, is_reply{false,true}, dst, byte4{0,9,255},
  hops. Golden RREQ `83 11 00 2A 08 00` + RREP `83 11 80 2A 09 04`. **is_reply
  isolation:** RREQ vs RREP differ only in byte2 bit 7 (`^ == 0x80`) and byte4.
  Reject len<6 + wrong-cmd.

## 5. Verification gate

- `pio test -e native` green (C2's 28 cases + new H/F cases).
- Sim: `meshroute_core` recompiles; meshroute Node doesn't call H/F yet → suite
  82/82, 0-diff (and §0 confirms airtime-neutral, so even when used later, no
  dm_delivery regression risk from sizing).

## 6. Files

`lib/core/frame_codec.{h,cpp}` (+ H/F structs, sigs, 4 functions),
`test/test_frame_codec.cpp` (+ cases). `wire.h` unchanged. All uncommitted — you commit.

## 7. Open questions

1. **H form: drop-flag 7 B (rec) vs keep-flag 8 B.** Airtime-neutral either way
   (§0); the flags nibble is pure reserved (no functionality lost). Rec **drop to
   7 B** per "shorten where functionality survives." Your call.
2. **F: keep 6 B (rec), no 5 B collapse** (airtime-neutral + keeps flag headroom).
3. Confirm `is_reply` sits at **byte2 bit 7** in §10 (codec re-places it from the
   Lua bit 0) — rec yes.
4. `f` byte 4 exposed as a **raw `uint8_t`** (not a ttl|next_hop union) — rec yes
   (mirrors `parse_r`; keeps codec free of handler semantics).
5. `key_hash32` stays **LE** in H (via `u32_le`) — rec yes (matches the Lua helper).
