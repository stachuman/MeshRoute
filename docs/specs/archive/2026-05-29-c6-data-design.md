# C6 — DATA (data plane) codec (§10 cmd-nibble) — design proposal

**Date:** 2026-05-29  **Status:** IMPLEMENTED + REVIEWED (Q2=flags-high, Q8=keep-14B,
all rec-yes confirmed). 53 doctest cases / 13095 assertions PASS; sim gate pending. You commit.
Adversarial review (5 angles, 11→7 verified): 0 wire-packing bugs; closed 6 doc/test gaps
(stale `.h` header, parse-side flag-bool + `ctr_lo4` assertions, inner-helper min-accept
boundaries, `addr_len` 3-bit width + rsv-bit-0 ignored). One semantic fix: `hops_remaining`
now defaults to **31** in `data_in` (faithful to the Lua `pack_data` `hb.remaining or 31`) —
a default-constructed `data_in` was packing `0` = the wire code for TTL-exhausted (drop at
first hop). See `frame_codec.h` `data_in`.
**Track:** codec track, after C5 (BCN). **The LAST codec — finishes C0–C6.**
MeshRoute-only; `pio test -e native`. Layout verified against `dv_dual_sf.lua`
`pack_data`/`parse_data` (L2787-2888) + constants (L2187-2223, L2904-2905) and
ROADMAP §10.3 (L3474-3490) by a 4-angle extract + line-by-line reconcile
(verdict: **CONFIRMED**, 0 byte-map corrections). Unblocks the behaviour track:
DATA is the frame `dm_delivery_breakdown` actually counts.

---

## 0. Scope (the C6 decision)

| Part | C6 | Why |
|---|---|---|
| §10 fixed header (**14 B**) | **implement** | core |
| 6-byte visited-set window | **implement** | loop guard, part of the header |
| 16-bit `ctr` (LE) + 5/3 `hop_budget` | **implement** | replay key + TTL |
| inner (ciphertext slot) | **OPAQUE span** | under §8 the whole inner is ciphertext (L2181-2184); the codec is a serializer, crypto is a behaviour layer |
| 4-byte MAC trailer | **OPAQUE span** | zero placeholder today (L2223); carry verbatim so the codec is unchanged when §8 fills real MACs |
| NORMAL / M inner sub-layouts | **separate OPTIONAL helpers** | `parse_unicast_inner` / `parse_m_inner`, called by the behaviour layer per the `PAYLOAD_TYPE_M` flag — NOT folded into the mandatory parse path (mirrors the BCN ext-block seam) |
| gateway-envelope · hash-bind · E2E-ACK body | **DEFERRED (behaviour layer)** | all three are MAGIC-prefixed / flag-gated structures *inside* the (to-be-encrypted) inner body; like the BCN ext-TLV bodies they ride with the cross-layer / channel / E2E iterations |

Same seam as C5: the codec packs/parses the **wire header + opaque inner + opaque
MAC**; everything that depends on decrypting or interpreting the body is a
behaviour-layer helper.

---

## 1. §10 header (14 B) — the C++ cmd-nibble layout

The Lua frame is `'D'`(tag) + 13 more = 14-B header; the C++ wire folds the tag
into the cmd nibble and relocates `addr_len`/`flags` per §10.3 (ROADMAP:3476-3477).
**Same 14-B header length** (Δ = 0 B vs Lua — see §9 Q8 for the shortening question).

```
byte 0 : cmd=0x3(7..4) | addr_len(3..1) | rsv(0)        [reading A; addr_len 0 this phase]
byte 1 : flags(7..4) | rsv(3..0)
byte 2 : next            (next-hop short-id, u8)
byte 3 : dst             (final dest short-id, u8 — present because addr_len==0)
byte 4 : hops_remaining(7..3, 5-bit 0..31) | committed_hops(2..0, 3-bit 0..7)
byte 5 : prev_fwd_rt_hops (soft hop-gradient, u8)
byte 6 : ctr_lo          ┐ 16-bit ctr, LITTLE-endian
byte 7 : ctr_hi          ┘ (ctr = lo | hi<<8)
bytes 8..13 : visited[6]  (fixed 6 slots, one short-id each, 0 = empty, no length prefix)
bytes 14..(13+n) : inner  (OPAQUE ciphertext slot, n bytes)
bytes (14+n)..(17+n) : MAC (OPAQUE 4-byte trailer)
on-wire total = DATA_HDR_LEN(14) + n + MAC_LEN(4) = 18 + n
```

**flags nibble** (byte1 bits 7..4 — the four Lua flag VALUES, shifted up one nibble):
`E2E_ACK_REQ=0x08·b7`, `E2E_IS_ACK=0x04·b6`, `PRIORITY=0x02·b5`
(formerly `IS_MULTICAST`; the file-top header L11 + a ROADMAP "Current" diagram
still carry the stale name), `PAYLOAD_TYPE_M=0x01·b4`. Pack `(flags&0x0F)<<4`,
parse `(b1>>4)&0x0F` — the RTS "shift the field, keep the values" pattern.

---

## 2. Field semantics (verified against the Lua code)

| field | wire | encoding / range | Lua |
|---|---|---|---|
| `addr_len` | b0 b3..1 | 0 this phase; **pack + parse REJECT ≠0** (deferred hierarchy) | L2797/2828 |
| flags | b1 b7..4 | 4 bits, values as above | L2187-2190 |
| `next` | b2 | raw u8 | L2813/2830 |
| `dst` | b3 | raw u8 (addr_len==0) | L2814/2831 |
| `hops_remaining` | b4 b7..3 | **saturate** 0..31 (default 31 = no TTL enforcement) | L2800-2802 |
| `committed_hops` | b4 b2..0 | **saturate** 0..7 | L2801-2802 |
| `prev_fwd_rt_hops` | b5 | raw u8 (gradient logic is behaviour, codec carries the byte) | L2803/2833 |
| `ctr` | b6-7 | **u16 LITTLE-endian**; parse also exposes `ctr_lo4 = ctr&0x0F` (derived, for CTS/ACK/NACK hop-match) | L2804-2805/2838/2863 |
| `visited[6]` | b8-13 | fixed 6×u8, 0=empty, no prefix; reserved-0 invariant (no real node short-id 0) | L2808-2810/2840-2844, VISITED_LEN=6 L2904 |
| inner | b14.. | OPAQUE span, `n = frame_len − 18` | L2845-2849 |
| MAC | last 4 | OPAQUE, `MAC_LEN=4` | L2811/2223 |

**Endianness — the copy-paste trap:** `ctr` is **LE**; `channel_msg_id` (inside the
M inner) is the file's **only BE** 4-byte field (L2210-2222); all `key_hash32`
(gateway-envelope / hash-bind, deferred) are **LE**. Call them out explicitly.

**Mandatory `parse_data` rejects:** cmd nibble ≠ 0x3; `frame_len < 18`; `addr_len ≠ 0`.
The inner-shape floors (NORMAL `#inner≥2`, M `#inner≥6`, `src_addr_len==0`) belong
to the OPTIONAL inner helpers, not the wire parse (Q9).

---

## 3. Inner sub-layouts (OPTIONAL helpers, dispatched by `PAYLOAD_TYPE_M`)

The single byte1 flag `PAYLOAD_TYPE_M` (b4) is the ONLY flag that changes the inner
LAYOUT. The behaviour layer (which owns decrypt) calls the matching helper on the
opaque inner span:

- **NORMAL** (`payload_type_m=0`): `src_addr_len(1, must=0) | origin(1) | body(N)`;
  `#inner≥2` (L2882-2886). `origin = inner[1]`.
- **CHANNEL "M"** (`payload_type_m=1`): `channel_msg_id(4 B, **BIG-endian**) |
  channel_id(1) | flavor(1, 0=PUBLIC/1=GROUP/2=PRIVATE) | body(N)`; `#inner≥6`
  (L2868-2878). No src framing — `origin = (channel_msg_id>>24)&0xFF`.

**Deferred to behaviour iterations** (NOT C6 — they live inside the encrypted body):
the **E2E-ACK body** (3 B `acked_ctr_lo|acked_ctr_hi|actual_hops_used`, LE, gated by
`E2E_IS_ACK`), the **gateway envelope** (magic `1F 47 32` + hop list + `dst_key_hash32`
LE), and the **hash-bind response** (magic `1F 48 31` + ids + `key_hash32` LE). These
are the DATA analogue of the BCN ext-TLVs.

---

## 4. Golden hex (§10 cmd-nibble; pinned in tests)

- **NORMAL** `{addr_len 0, flags 0, next 0x0B, dst 0x0C, hops_rem 10, committed 2,
  prev 3, ctr 0x1234, visited [5,9,0,0,0,0], inner [00 07 AA BB], mac 4×00}`
  → `30 00 0B 0C 52 03 34 12 05 09 00 00 00 00 00 07 AA BB 00 00 00 00` (22 B)
  (b0 `0x30`=cmd3|addr0; b4 `0x52`=(10<<3)|2; b6-7 `34 12`=ctr LE; b8-13 visited)
- **M / channel** `{flags PAYLOAD_TYPE_M(0x01), next 0x0B, dst 0xFF, hops_rem 31,
  committed 0, prev 0, ctr 0x0001, visited all-0, inner = msgid 0x07ABCDEF(BE) |
  chan 0x02 | flavor 0x01 | body [99], mac 4×00}`
  → `30 10 0B FF F8 00 01 00 00 00 00 00 00 00 07 AB CD EF 02 01 99 00 00 00 00` (25 B)
  (b1 `0x10`=flag M in high nibble; b4 `0xF8`=31<<3; msgid `07 AB CD EF`=BE)

---

## 5. API (`frame_codec.h`)

```cpp
inline constexpr size_t DATA_HDR_LEN     = 14;
inline constexpr size_t DATA_MAC_LEN     = 4;
inline constexpr size_t DATA_VISITED_LEN = 6;
enum DataFlag : uint8_t {            // Lua flag VALUES (packed into byte1 high nibble)
    DATA_FLAG_PAYLOAD_TYPE_M = 0x01, DATA_FLAG_PRIORITY = 0x02,
    DATA_FLAG_E2E_IS_ACK     = 0x04, DATA_FLAG_E2E_ACK_REQ = 0x08,
};

struct data_in {
    uint8_t  addr_len;          // 0 this phase (pack returns 0 if !=0)
    uint8_t  flags;             // OR of DataFlag
    uint8_t  next, dst;
    uint8_t  hops_remaining;    // saturated 0..31
    uint8_t  committed_hops;    // saturated 0..7
    uint8_t  prev_fwd_rt_hops;
    uint16_t ctr;               // packed LE
    std::span<const uint8_t> visited;  // empty -> 6 zero bytes; else exactly 6 (else pack->0)
    std::span<const uint8_t> inner;    // opaque ciphertext slot (0..max)
    std::span<const uint8_t> mac;      // empty -> 4 zero bytes; else exactly 4 (else pack->0)
};
size_t pack_data(const data_in& in, std::span<uint8_t> out);   // 0 on bad input / short buf

struct data_out {
    uint8_t  addr_len, flags;
    bool     e2e_ack_req, e2e_is_ack, priority, payload_type_m;
    uint8_t  next, dst, hops_remaining, committed_hops, prev_fwd_rt_hops;
    uint16_t ctr;               // full 16-bit LE
    uint8_t  ctr_lo4;           // derived ctr & 0x0F (hop-match convenience)
    size_t   visited_off, inner_off, inner_len, mac_off, frame_len;
};
std::optional<data_out> parse_data(std::span<const uint8_t> frame);
std::span<const uint8_t> data_visited(std::span<const uint8_t> frame, const data_out&); // 6 B
std::span<const uint8_t> data_inner  (std::span<const uint8_t> frame, const data_out&); // opaque
std::span<const uint8_t> data_mac    (std::span<const uint8_t> frame, const data_out&); // 4 B

// OPTIONAL inner helpers (behaviour layer; dispatched by data_out.payload_type_m):
struct data_unicast_inner { uint8_t origin; std::span<const uint8_t> body; };
std::optional<data_unicast_inner> parse_unicast_inner(std::span<const uint8_t> inner);  // src_addr_len must=0
struct data_m_inner { uint32_t channel_msg_id; uint8_t channel_id; uint8_t flavor; std::span<const uint8_t> body; };
std::optional<data_m_inner> parse_m_inner(std::span<const uint8_t> inner);              // channel_msg_id BE
```

`visited` / `mac` follow the BCN `seen_bitmap` rule the USER pinned (size-checked
span, empty → zero-fill). Uses C0 `wire` (`u16_le` for ctr, `u32_be` for the M
helper's channel_msg_id). No `wire.h` change (`Cmd::D = 0x3` already present).

---

## 6. Saturate / mask

`addr_len` reject ≠0 (return 0); `flags` 4-bit `(f&0x0F)<<4`; `hops_remaining`
**saturate** 0..31, `committed_hops` **saturate** 0..7 (matches Lua `math.min`, NOT a
mask-wrap); `next`/`dst`/`prev_fwd_rt_hops`/visited entries raw u8; `ctr` u16 LE;
`visited` exactly 6 B or empty→zero; `mac` exactly 4 B or empty→zero; `inner` opaque;
rsv bits emit 0 / ignored on parse.

---

## 7. Tests (append to `test_frame_codec.cpp`, CHECK-only)

- **Round-trip:** NORMAL + M frames across {empty inner, n-byte inner}, visited
  {empty, partial, full 6}, ctr {0, 0x1234, 0xFFFF}, hops_remaining {0, 31}, every
  flag bit; pack→parse→`data_visited`/`data_inner`/`data_mac` + the two inner helpers
  → fields/bytes match.
- **Golden hex:** both §4 vectors byte-exact (cmd nibble, addr_len, flag-nibble
  position, hop_budget 5/3 split, ctr LE, channel_msg_id BE, visited padding).
- **Saturate:** `hops_remaining=40 → 31`, `committed_hops=9 → 7` (pin it is NOT a
  wrap, the `pack_ack budget_hint` precedent).
- **Flag-bit isolation:** each flag toggles exactly its byte1 bit (the BCN idiom).
- **Reject:** wrong cmd; `frame_len < 18`; `addr_len ≠ 0` (pack AND a forged parse);
  truncation mid-header / mid-visited / short MAC; `visited` size ≠ 6 / `mac` size ≠ 4
  at pack → 0.
- **Endianness guard:** a test that would fail if `ctr` were BE or `channel_msg_id`
  were LE.

## 8. Verification gate

`pio test -e native` green (BCN + DATA cases). Sim: `meshroute_core` recompiles;
the Node doesn't call `pack_data`/`parse_data` yet (behaviour track wires it) →
**102/102, 0-diff**. With C6 the codec track is **C0–C6 complete**.

## 9. Open questions (reading-A defaults; confirm or override)

1. **byte0 low nibble** = `addr_len`(3..1) | `rsv`(0) — rec yes (mirrors the RTS
   byte3 `(b>>1)&0x07` precedent).
2. **flags in byte1 HIGH nibble** per §10.3, packed `(flags&0x0F)<<4` keeping the
   Lua flag VALUES — rec yes (follow §10; intentional position change vs Lua, not a
   defect). *(Alternative: keep flags in the LOW nibble → Lua-identical masks, but
   diverges from the documented §10 pack.)*
3. **`addr_len ≠ 0` rejected at BOTH pack and parse** (deferred hierarchy) — rec yes.
4. **`hop_budget` 5+3 split, saturating** — rec yes (the §7.6 "4+4 nibbles" PROSE is
   STALE; the shipped code + §10.3 are 5+3).
5. **`visited`/`mac` as size-checked spans** (empty→zero-fill; wrong size→pack 0),
   mirroring the BCN `seen_bitmap` — rec yes.
6. **inner + MAC OPAQUE in the mandatory parse; `parse_unicast_inner`/`parse_m_inner`
   as separate optional helpers; gateway-envelope/hash-bind/E2E-ACK deferred to
   behaviour** — rec yes (the BCN ext-block seam).
7. **`ctr` LE u16 + derived `ctr_lo4`; M `channel_msg_id` BE u32** — rec yes (flagged
   to prevent an endianness copy-paste bug).
8. **SHORTENING: follow §10's 14-B header** (the `addr_len` slot is reserved for
   future hierarchy) vs **shorten to 13 B this phase** by folding flags into byte0
   (`cmd|flags`) and dropping the always-0 `addr_len` (re-add when hierarchy lands).
   **rec: follow §10 (14 B)** — consistent with keeping BCN `n_entries`, and avoids a
   mid-port wire break when hierarchy arrives. Your call (you value lossless shortening).

## 10. Files

`lib/core/frame_codec.{h,cpp}` (add DATA structs + `pack_data`/`parse_data` + 3
accessors + 2 inner helpers + the 3 constants/flags), `test/test_frame_codec.cpp`
(+ cases). `wire.h` unchanged. Uncommitted working artifact — you commit.
