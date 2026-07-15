# C0 + C1 — codec foundation + CTS/ACK codecs (design proposal)

**Date:** 2026-05-29  **Status:** PROPOSAL — awaiting review, no code written.
**Track:** codec track (PORT_PLAN §4: C0 codec foundation, C1 CTS+ACK). The first
*real* wire codecs. **Touches MeshRoute only** (`lib/core/`, `test/`); the
simulator just recompiles `meshroute_core` and must stay bit-identical (§5).

Why codecs next (not S3): the differential harness only becomes a meaningful gate
once the C++ node reproduces protocol behaviour, which needs real frames. Codecs
are the one piece fully testable *in isolation* now (round-trip + golden-hex), so
they're the productive foundation. CTS/ACK first — the two simplest frames (3 B
each, D2 simplest-first) — to de-risk the codec+test pattern before the complex
beacon/DATA.

---

## 0. Authority: §10 for byte positions, Lua for field semantics

- **Byte layout = ROADMAP §10.3** (the cmd-nibble v2 wire — our committed C++
  wire; Lua stays tag-byte, they diverge by design). Byte 0 is always
  `cmd(4 hi) | flags(4 lo)`.
- **Field *meaning* = the Lua `pack_*`/`parse_*` code** (`dv_dual_sf.lua`), which
  is authoritative over the prose docs (PROTOCOL.md is stale in places).
- **Golden hex = hand-derived from §10.3** (NOT captured from the Lua — the wire
  differs). The vectors below are computed in this doc and pinned in the test.

`frame_codec.h` currently documents the *stale* tag-byte BCN layout; C0/C1 do not
touch the BCN stub (it migrates to §10 at C5). C1 adds CTS/ACK in §10 and the
shared cmd-nibble conventions.

---

## 1. Goal & non-goals

**Goal:** (C0) a tiny, no-heap/no-exception wire-primitive layer — cmd-nibble
byte-0 helpers + a `Cmd` enum + bounded `Writer`/`Reader` cursors + LE/BE
integer primitives; (C1) `pack_cts`/`parse_cts` + `pack_ack`/`parse_ack` in §10
layout, with round-trip + golden-hex + robustness tests.

**Non-goals:** no other frames (RTS/NACK/Q = C2; H/F = C3; J = C4; **BCN = C5**;
DATA = C6); no protocol logic (the Node doesn't call these yet — that's the R
track); no snr→bucket / sf-selection logic (that's protocol-layer; the codec
packs the already-computed 2-bit/3-bit fields). No simulator changes.

---

## 2. C0 — codec foundation (`lib/core/wire.h`, new; header-only)

```cpp
// MeshRoute — lib/core/wire.h   (C++20; meshroute_core internal — NOT in hal.h)
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace meshroute::wire {

// §10.1 primary command codes (byte 0 high nibble).
enum class Cmd : uint8_t {
    B=0x0, R=0x1, C=0x2, D=0x3, K=0x4, N=0x5, Q=0x6, H=0x7, F=0x8, J=0x9, EXT=0xF
};
constexpr uint8_t cmd_byte(Cmd c, uint8_t flags4) { return (uint8_t(c) << 4) | (flags4 & 0x0F); }
constexpr Cmd     cmd_of(uint8_t b)   { return Cmd(b >> 4); }
constexpr uint8_t flags_of(uint8_t b) { return b & 0x0F; }

// Bounded writer — no heap, no exceptions; sets !ok() on overflow, never writes OOB.
class Writer {
public:
    explicit Writer(std::span<uint8_t> out) : _out(out) {}
    void u8(uint8_t v);
    void u16_le(uint16_t v);  void u32_le(uint32_t v);
    void u16_be(uint16_t v);  void u32_be(uint32_t v);   // channel-msg-id etc. are BE
    bool   ok()   const { return _ok; }
    size_t size() const { return _pos; }                 // bytes written
private:
    std::span<uint8_t> _out; size_t _pos = 0; bool _ok = true;
};

// Bounded reader — sets !ok() on read-past-end; returns 0 past the end.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> in) : _in(in) {}
    uint8_t  u8();
    uint16_t u16_le();  uint32_t u32_le();
    uint16_t u16_be();  uint32_t u32_be();
    bool   ok()        const { return _ok; }
    size_t remaining() const { return _in.size() - _pos; }
private:
    std::span<const uint8_t> _in; size_t _pos = 0; bool _ok = false ? false : true;
};

}  // namespace meshroute::wire
```
`frame_codec.cpp` includes `wire.h`; the cursors stay internal to `meshroute_core`
(C++20) and never appear in the C++17-clean `hal.h`/`node.h`.

**C0 success:** compiles; unit tests for cursor write/read round-trip, overflow /
read-past-end flagging, and LE vs BE byte order.

---

## 3. C1 — CTS + ACK in §10 layout (`frame_codec.h`/`.cpp`)

### CTS — cmd=`0x2`, 3 B (§10.3)
```
byte 0: cmd=0x2(4 hi) | ctr_lo(4 lo)
byte 1: (sf-5)(3 hi) | already_received(1) | rsv(4 lo)
byte 2: to(8)
```
Semantics (from Lua `pack_cts`/`parse_cts`): `ctr_lo` = low 4 bits of the DATA
ctr (hop match); `chosen_data_sf` ∈ 5..12 → wire stores `sf-5` (0..7, 3 bits);
`already_received` short-circuits a resend whose ACK was lost; `to` = intended
requester id.
```cpp
struct cts_in  { uint8_t ctr_lo; uint8_t chosen_data_sf; bool already_received; uint8_t to; };
struct cts_out { uint8_t ctr_lo; uint8_t chosen_data_sf; bool already_received; uint8_t to; };
size_t pack_cts(const cts_in& in, std::span<uint8_t> out);   // 3 on success, 0 on bad input/short buf
std::optional<cts_out> parse_cts(std::span<const uint8_t> frame);  // nullopt: wrong cmd / len != 3
```

### ACK — cmd=`0x4`, 3 B (§10.3)
```
byte 0: cmd=0x4(4 hi) | ctr_lo(4 lo)
byte 1: budget_hint(2 hi) | snr_bucket(2) | rsv(4 lo)
byte 2: to(8)
```
Semantics (Lua `pack_ack`/`parse_ack`): `ctr_lo` hop match; `budget_hint` 2-bit
duty-cycle tier hint; `snr_bucket` 2-bit coarse SNR (the snr_q4→bucket mapping is
**protocol-layer**, added when ACK is used at R3 — the codec packs the 2-bit value
directly); `to` = intended previous-hop id.
```cpp
struct ack_in  { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; };
struct ack_out { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; };
size_t pack_ack(const ack_in& in, std::span<uint8_t> out);
std::optional<ack_out> parse_ack(std::span<const uint8_t> frame);
```

---

## 4. Tests (`test/test_frame_codec.cpp`, doctest)

1. **Round-trip:** `parse(pack(x)) == x` across representative inputs — `ctr_lo`
   ∈ {0,5,15}, `chosen_data_sf` ∈ {5,7,8,12}, `already_received` ∈ {false,true},
   `to` ∈ {0,1,255}; ACK `budget_hint`/`snr_bucket` ∈ {0,1,2,3}. (This *is* the
   "2-node interop" check — both nodes run the same codec, so parse-of-pack
   covers A→B.)
2. **Golden hex (pinned §10 bytes, hand-derived here):**
   - `pack_cts({ctr_lo=0x5, sf=8, already_received=true, to=0x2A})` →
     **`25 70 2A`**  (`0x25` = cmd0x2|0x5; `0x70` = (8−5)=3<<5 | 1<<4; `0x2A`=to)
   - `pack_cts({ctr_lo=0x0, sf=5, already_received=false, to=0xFF})` → **`20 00 FF`**
   - `pack_ack({ctr_lo=0x3, budget_hint=2, snr_bucket=1, to=0x07})` →
     **`43 90 07`**  (`0x43`=cmd0x4|0x3; `0x90`= 2<<6 | 1<<4; `0x07`=to)
   - `pack_ack({ctr_lo=0xF, budget_hint=3, snr_bucket=3, to=0x00})` → **`4F F0 00`**
3. **Robustness:** `parse_cts`/`parse_ack` return `nullopt` on wrong cmd nibble,
   on `len != 3`, and on an empty span; `pack_*` returns 0 on a too-small `out`
   span or out-of-range `sf` (CTS).
4. **Field isolation:** changing one field flips only its bits (catches
   mask/shift errors) — e.g. `already_received` toggles only byte1 bit 4.

**Run:** `pio test -e native` (alongside the iteration-1 `test_airtime` /
`test_protocol_constants`). *Open Q1: confirm `pio` is available in this env; if
not, a direct `g++ -std=c++20 -I lib/core test/test_frame_codec.cpp` (doctest is
header-only) is the fallback — verified at implementation time.*

---

## 5. Verification gate

- **MeshRoute:** `pio test -e native` green — the new frame_codec cases + the
  existing 12 iteration-1 cases all pass.
- **Simulator unaffected:** `meshroute_core` recompiles `frame_codec.cpp` (now
  with real CTS/ACK), but `FirmwareNode` doesn't call them yet → re-run the S0
  baseline set, **expect suite 82/82 + 0 NDJSON diff** (sanity that the codec
  change didn't perturb the sim).
- Clean MeshRoute device-target compile is **not** required here (C0/C1 are pure
  `lib/core`, no RadioLib).

---

## 6. Files touched

| File | Change |
|---|---|
| `lib/core/wire.h` | NEW — `Cmd` + cmd-nibble helpers + `Writer`/`Reader` + LE/BE |
| `lib/core/wire.cpp` *(or header-only)* | Writer/Reader bodies (Open Q2: header-only vs .cpp; a .cpp must be added to `meshroute_core` in the sim's root CMake) |
| `lib/core/frame_codec.h` | + CTS/ACK structs & signatures; §10 doc for C/K (BCN doc left for C5) |
| `lib/core/frame_codec.cpp` | implement `pack/parse_cts`, `pack/parse_ack` (replacing nothing — additive) |
| `test/test_frame_codec.cpp` | NEW — round-trip + golden-hex + robustness |
| `platformio.ini` / `library.json` | only if a new TU needs listing (PlatformIO auto-globs `lib/core`; the sim CMake lists sources explicitly — see Open Q2) |

---

## 7. Open questions

1. **`pio` availability** for `pio test -e native`, or g++ fallback for doctest?
   (Verified at implementation; doesn't change the design.)
2. **`wire.h` header-only vs `wire.cpp`.** Header-only (`inline`) avoids touching
   the sim's explicit `meshroute_core` source list; a `.cpp` is cleaner but means
   adding `wire.cpp` to the root `CMakeLists` `meshroute_core` target. **Rec:
   header-only** for C0 (tiny, inline) — zero CMake churn either side.
3. **`pack_ack` input — 2-bit `snr_bucket` (rec) vs raw `snr_q4`.** Keep the codec
   pure (packs the already-bucketed 2-bit value); the snr_q4→bucket mapping is a
   protocol helper added when ACK is wired at R3. Same for CTS sf-selection.
4. **Commit boundary.** C0+C1 as **one commit** in MeshRoute (rec — C0 alone has
   no consumer; both are small), or two (C0 foundation, then C1 frames)?
5. **Who commits the MeshRoute side** — me (as part of executing C0/C1) or you
   (you took the S2 `lib/core` commit)? 
