# C5 — BCN (beacon) codec (§10 cmd-nibble) — design proposal

**Date:** 2026-05-29  **Status:** PROPOSAL — awaiting review, no code written.
**Track:** codec track, after C4 (J). MeshRoute-only; `pio test -e native`.
The largest codec — variable-length, 4 sub-blocks. Layout verified against
`dv_dual_sf.lua` pack_beacon/parse_beacon (+ helpers) and §10.3. **Gate to R1
(beacon emit)** — the first `dm_delivery`-measured behaviour milestone.

---

## 0. Scope (the C5 decision)

| Sub-block | C5 | Why |
|---|---|---|
| §10 header (8 B) | **implement** | — |
| route entries (**4 B** each) | **implement** | core |
| schedule block (layer-count byte + 4-B records) | **implement** | gateway beacons need it |
| seen-bitmap (32 B) | **implement** | reachability |
| ext-TLV block | **OPAQUE** (`ext_len` + raw byte span) | the 4 TLV bodies couple to behaviour-layer state (peer-suspect tables, `channel_buffer` dirty/ad-count, `bridged_layers`); their codecs ride with the channel/gateway/liveness iterations. Keeps the BCN codec a pure (de)serializer. |

**Keep `n_entries`** (defer the §10.6 reorder-drop): both readers confirm the
reorder frees byte3's entry-bits but **saves 0 bytes** unless byte3 is also
repurposed (it still holds rsv), and it complicates the parser. Not worth it now.

R1 beacon-emit needs exactly this surface: header + entries + (schedule for
gateways) + (optional seen-bitmap) + an optional opaque ext payload from a higher
layer.

---

## 1. §10 header (8 B) + the byte-2 flag-order decision

```
byte 0 : cmd=0x0(7..4) | leaf_id(3..0)
byte 1 : src
byte 2 : has_schedule(b7) | self_gateway(b6) | is_mobile(b5) | has_seen_bitmap(b4) | has_ext(b3) | n_entries_lo(b2..0)
byte 3 : n_entries_hi(b7..5) | rsv(b4..0)
bytes 4-7 : key_hash32 (LITTLE-ENDIAN)
body: [schedule if has_schedule] → n_entries × 4-B entries → [32-B seen-bitmap] → [ext_len + ext bytes]
```
`n_entries` = `(byte2 & 0x07) | (((byte3 >> 5) & 0x07) << 3)` — full **6 bits**
(0..63), matching the current `BCN_N_ENTRIES_MASK=0x3f`. (Doc note "typically fits
3 bits" — but implement the full 6-bit split for wire-compat.)

**DECISION (byte-2 flag bit order):** §10.3 lists the five flags by name only (no
bit numbers) — same ambiguity class as RTS byte-5 / J byte-1. The **top-down
reading** above (has_schedule=b7 … has_ext=b3, n_entries_lo=b2..0) is the natural
one and consistent with the RTS/J **reading A** you already pinned. My golden hex
assumes it. Your call (a §10.3 clarification settles it).

(Note: the iteration-1 `frame_codec.h` `beacon_in` had a bogus `req_sync` flag —
the real byte1/byte2 bit0/low is **rsv**, not req_sync. The structs are redesigned.)

---

## 2. Sub-block layouts (verified)

**Route entry — 4 B** (the §10.3 "3 B" text is STALE; code is 4 B):
`dest(1) · next(1) · score_bucket(4 hi)|rsv(3)|is_gateway(b0) · hops(full byte)`.

**Schedule block** (if has_schedule): leading byte `gateway_spread_nibble(4 hi) |
layer_count(4 lo)` (layer_count ≤ 15), then `layer_count` × 4-B records:
`b0 = layer_id(4 hi) | (routing_sf-5)(b3..1) | period_unit_5s(b0)` ·
`b1 = duration_100ms` · `b2 = offset_100ms` · `b3 = period_units`.
`period_unit_5s`: 0 → period ×1000 ms, 1 → ×5000 ms.
**`offset_100ms` (b2) is re-stamped at TX time by the runtime** (`apply_schedule_tx_fixup`)
— the codec just packs the given byte; the re-stamp stays a runtime concern (the
codec keeps that byte addressable at `header + 1 + k*4 + 2`).

**Seen-bitmap** (if has_seen_bitmap): fixed **32 bytes**; bit `id` set ⇒ dest `id`
reachable. `byte = id/8`, `bit = id%8` (LE within byte), `id ∈ 0..254`.

**Ext block** (if has_ext): `ext_len(1)` + `ext_len` opaque bytes (C5 passes them
through verbatim; TLV bodies decoded by behaviour layers).

---

## 3. Golden hex (§10 cmd-nibble; pinned in tests)

- **Minimal** `{leaf=3, src=0x11, key=0xDEADBEEF, entries=[{d5,n7,bkt0xC,gw0,hops2},{d9,n7,bkt0xA,gw1,hops3}]}`
  → `03 11 02 00 EF BE AD DE 05 07 C0 02 09 07 A1 03` (16 B; byte2 `0x02`=n_lo2, byte3 `0x00`)
- **Schedule + seen-bitmap** `{leaf=1, src=5, self_gateway, has_schedule (spread0,1 rec: layer2/sf8/unit1s/dur30/off15/period60), seen ids {5,9,130}, 0 entries}`
  → `01 05 D0 00 EF BE AD DE 01 26 1E 0F 3C` + 32-B bitmap (`[0]=0x20,[1]=0x02,[16]=0x04`, rest 0)
  (byte2 `0xD0`=has_schedule|self_gateway|has_seen_bitmap; record `26 1E 0F 3C`)
- **Opaque ext** (one CHANNEL_DIGEST TLV, treated as raw): ext payload `35 01 07 56 78 05`
  (6 B) → on wire the BCN appends `06 35 01 07 56 78 05` (`ext_len=06` + the 6 raw bytes);
  C5 packs/round-trips it as an opaque span, no TLV interpretation.

---

## 4. API (`frame_codec.h` — redesigns the iteration-1 BCN scaffold)

```cpp
struct beacon_entry    { uint8_t dest; uint8_t next; uint8_t score_bucket; bool is_gateway; uint8_t hops; };
struct schedule_record { uint8_t layer_id; uint8_t routing_sf; bool period_unit_5s;
                         uint8_t duration_100ms; uint8_t offset_100ms; uint8_t period_units; };
struct beacon_in {
    uint8_t  leaf_id; bool self_gateway; bool is_mobile; uint8_t src; uint32_t key_hash32;
    uint8_t  gateway_spread_nibble;                 // schedule spread (0..15)
    std::span<const schedule_record> schedule;      // empty → has_schedule = 0  (≤15 records)
    std::span<const beacon_entry>    entries;       // ≤63
    const uint8_t* seen_bitmap;                     // nullptr → has_seen_bitmap=0; else 32 bytes
    std::span<const uint8_t> ext;                   // empty → has_ext=0; else opaque payload (≤255)
};
size_t pack_beacon(const beacon_in& in, std::span<uint8_t> out);   // 0 on bad input / short buf

struct beacon_out {
    uint8_t leaf_id; bool self_gateway; bool is_mobile; uint8_t src; uint32_t key_hash32;
    bool has_schedule; uint8_t gateway_spread_nibble; uint8_t schedule_count;
    uint8_t n_entries; bool has_seen_bitmap; bool has_ext;
    // section offsets/lengths into `frame` for the accessors below:
    size_t schedule_off; size_t entries_off; size_t seen_off; size_t ext_off; size_t ext_len; size_t frame_len;
};
std::optional<beacon_out> parse_beacon(std::span<const uint8_t> frame);
std::optional<beacon_entry>    parse_beacon_entry   (std::span<const uint8_t> frame, const beacon_out&, uint8_t i);
std::optional<schedule_record> parse_beacon_schedule(std::span<const uint8_t> frame, const beacon_out&, uint8_t i);
std::span<const uint8_t>       beacon_seen_bitmap   (std::span<const uint8_t> frame, const beacon_out&); // 32 B or empty
std::span<const uint8_t>       beacon_ext           (std::span<const uint8_t> frame, const beacon_out&); // opaque or empty
```
`pack_beacon` derives the flags from the spans (has_schedule=!schedule.empty(),
etc.), caps n_entries at 63, validates schedule ≤15 / ext ≤255 (else returns 0).
Uses C0's `wire` (`u32_le` for key_hash32). No `wire.h` change.

---

## 5. Saturate / mask

`leaf_id`/`score_bucket` mask (4 bits); `n_entries` capped 63 (the 6-bit split);
`layer_count` validated ≤15 (return 0 if exceeded — the nibble can't hold more);
`seen_bitmap` must be exactly 32 B (else `pack→0`); `ext` ≤255 (else `pack→0`);
`routing_sf` packed as `(sf-5)&0x07`; `hops`/`duration`/`offset`/`period_units` are
`uint8_t` packed verbatim; `key_hash32` LE u32; rsv bits emit 0 / ignored on parse.

**Schedule floor — RESOLVED (2026-05-29, decision A):** Lua `pack_schedule_record`
floors `duration_100ms` (`:1628`) and `period_units` (`:1646`) to `[1,255]`, but it
does so *inside* the same function that converts ms→units and computes the offset
countdown — all of which the C++ design moves to the RUNTIME (the C++ `schedule_record`
carries post-conversion units; `offset_100ms` is re-stamped by the runtime, not the
codec). So the floor is a **runtime/caller contract**, not a codec job: the runtime
floors to `[1,255]` during its ms→units conversion, and `pack_beacon` packs the bytes
verbatim. A `0` reaching the codec is a runtime conversion bug we want to surface, not
silently rewrite. Documented in `frame_codec.h` `schedule_record`; R1 enforces + tests it.

---

## 6. Tests (append to `test_frame_codec.cpp`, CHECK-only)

- **Round-trip:** beacons across {0..N entries}, with/without schedule (1–3 records,
  both period units), with/without 32-B seen-bitmap, with/without opaque ext — pack
  → parse → iterate entries/schedule via accessors → fields + bitmap bits + ext bytes
  match. `n_entries` 6-bit split tested past 7 (e.g. 10, 63) to exercise byte3.
- **Golden hex:** the 3 vectors in §3 (byte2 flag order, 4-B entries, schedule record
  bits, bitmap bit positions, opaque ext round-trip).
- **Reject:** wrong cmd; truncated each section (header / mid-schedule / mid-entries /
  short bitmap / ext_len past end); `layer_count>15` and `entries>63` at pack → 0.

## 7. Verification gate

`pio test -e native` green (C4's 35 cases + BCN cases). Sim: `meshroute_core`
recompiles; meshroute Node doesn't call pack_beacon yet (R1 will) → 82/82, 0-diff.

## 8. Files

`lib/core/frame_codec.{h,cpp}` (redesign BCN structs + pack_beacon + parse_beacon +
3 accessors), `test/test_frame_codec.cpp` (+ cases). `wire.h` unchanged. Uncommitted — you commit.

## 9. Open questions

1. **byte-2 flag order** = top-down (has_schedule=b7 … has_ext=b3, n_lo=b2..0) — rec
   yes (RTS/J reading-A consistency).
2. **ext block OPAQUE** (ext_len + raw span; TLV codecs deferred to channel/gateway/
   liveness iterations) — rec yes (keeps BCN a pure (de)serializer).
3. **Keep `n_entries`** (defer §10.6 reorder — 0 bytes saved today) — rec yes.
4. `layer_count` validated ≤15 / `n_entries` ≤63 at pack (return 0 over-cap) — rec yes.
5. Parse exposes section offsets + accessors (mirrors the scaffold's
   parse_beacon + parse_beacon_entry) vs decoding everything into one big struct —
   rec accessors (no-heap, zero-copy into the frame).
