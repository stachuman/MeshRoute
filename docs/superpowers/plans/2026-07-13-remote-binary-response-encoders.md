<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Remote Binary Response Encoders Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone `lib/console/console_binary.{h,cpp}` that TLV-encodes the seven remote data responses (`status`/`cfg`/`duty`/`limits`/`faults`/`routes`/`gateway`) into a ≤241 B DM, with matching decoders and native round-trip tests.

**Architecture:** A tiny TLV writer/reader over a caller-owned `uint8_t[]` buffer (no heap). One `enc_*`/`dec_*` pair per verb, reusing the existing `console_json` field structs (`StatusFields`/`LimitsFields`/`RouteRow`/`CfgExtras`) plus three new plain structs. Frame = `[ver=1][msg_type]` + `[tag][len][value LE]` TLVs; decoders skip unknown tags. Native doctest is the regression backbone (the module is in `lib/`, so it compiles + tests on the host).

**Tech Stack:** C++20, doctest (native), PlatformIO. Spec: `docs/superpowers/specs/2026-07-13-remote-binary-response-encoders-design.md`.

## Global Constraints

- **The user does ALL git commits.** Never `git commit` or offer to. Each task ends at "tests green"; leave it **uncommitted for the user**. (The "Commit" step in each task = *report green, do not commit*.)
- **No-heap:** every buffer is a caller-owned `uint8_t[]` (fixed size); no `String`/`std::vector`/malloc. Matches `protocol_constants.h`.
- **Bounded, fail-closed:** every `enc_*` returns **`0` on overflow** and never writes past `cap` (no partial frame). Every `dec_*` is bounds-checked (never reads past `len`).
- **Little-endian on the wire; no float.** Multi-byte ints LE. Floats are pre-scaled to ints by the caller (mirrors `CfgExtras.freq_hz`/`duty_x1000`/`lat_e7`).
- **Forward-compatible:** decoders skip unknown tags (`default:` in the tag switch). Adding a field = a new tag, no `ver` bump.
- **Source header:** every new file carries `// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>` on line 2.
- **Namespace:** `meshroute::console::bin` (beside `meshroute::console` in `console_json.h`).
- **Regression scope:** this is NEW, uncalled-by-routing code. `native` doctest must stay green and grow; s18/s22 md5 are unaffected (nothing in `lib/core` routing changes). Device envs still compile (`lib/console` is compiled by all envs; the unused encoders dead-strip until remote-auth calls them) — spot-check one board at the end.

---

## File Structure

- **`lib/console/console_binary.h`** (CREATE) — the TLV primitives (inline `put_*`/`get_*`/reader), the `msg_type` + tag constants, the new structs (`StatusDiag`, `FaultRow`, `GatewayFields`, and the `*Out` decode structs), and the `enc_*`/`dec_*` declarations.
- **`lib/console/console_binary.cpp`** (CREATE) — the `enc_*`/`dec_*` bodies.
- **`test/test_console_binary.cpp`** (CREATE) — doctest round-trip/overflow/forward-compat/truncation tests (no `main` — `test_airtime.cpp` provides it, same as `test_console_json.cpp`).

---

## Task 1: TLV primitives + frame header + reader

**Files:**
- Create: `lib/console/console_binary.h`
- Test: `test/test_console_binary.cpp`

**Interfaces:**
- Produces (all `inline`, header-only): `bool put_bytes/put_u8/put_u16/put_u32/put_i16/put_i32(uint8_t* b, size_t cap, size_t& off, uint8_t tag, <val>)`; `size_t frame_begin(uint8_t* b, size_t cap, uint8_t msg_type)`; `struct TlvReader`; `bool reader_init(TlvReader&, const uint8_t*, size_t)`; `bool reader_next(TlvReader&, uint8_t& tag, const uint8_t*& val, uint8_t& n)`; `uint8_t get_u8`, `uint16_t get_u16`, `uint32_t get_u32`, `int16_t get_i16`, `int32_t get_i32(const uint8_t* v, uint8_t n)`. `msg_type` constants `MSG_STATUS=0x01 … MSG_GATEWAY=0x07`; `TAG_TRUNCATED=0xFE`.

- [ ] **Step 1: Write the failing test** — `test/test_console_binary.cpp`:

```cpp
// MeshRoute — test_console_binary.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_binary.h"
#include <cstring>

using namespace meshroute::console::bin;

TEST_CASE("bin TLV primitives — put/reader/get round-trip, LE, overflow") {
    uint8_t b[64];
    size_t off = frame_begin(b, sizeof b, MSG_DUTY);
    CHECK(off == 2);
    CHECK(b[0] == 1);          // ver
    CHECK(b[1] == MSG_DUTY);
    CHECK(put_u8 (b, sizeof b, off, 0x01, 0x42));
    CHECK(put_u16(b, sizeof b, off, 0x02, 0x1234));
    CHECK(put_u32(b, sizeof b, off, 0x03, 0xDEADBEEF));
    CHECK(put_i32(b, sizeof b, off, 0x04, -5));
    // little-endian check on the u16 value bytes: tag(0x02) len(2) 0x34 0x12
    // walk it back
    TlvReader r;
    CHECK(reader_init(r, b, off));
    CHECK(r.ver == 1);
    CHECK(r.msg_type == MSG_DUTY);
    uint8_t tag, n; const uint8_t* v;
    CHECK(reader_next(r, tag, v, n)); CHECK(tag == 0x01); CHECK(get_u8(v, n)  == 0x42);
    CHECK(reader_next(r, tag, v, n)); CHECK(tag == 0x02); CHECK(get_u16(v, n) == 0x1234);
    CHECK(reader_next(r, tag, v, n)); CHECK(tag == 0x03); CHECK(get_u32(v, n) == 0xDEADBEEF);
    CHECK(reader_next(r, tag, v, n)); CHECK(tag == 0x04); CHECK(get_i32(v, n) == -5);
    CHECK_FALSE(reader_next(r, tag, v, n));   // end
}

TEST_CASE("bin TLV — overflow returns false, no OOB") {
    uint8_t b[4];                     // room for header + nothing
    size_t off = frame_begin(b, sizeof b, MSG_DUTY);   // off=2
    CHECK(put_u32(b, sizeof b, off, 0x01, 1) == false); // 2+2+4 > 4
    CHECK(off == 2);                  // unchanged on failure
}

TEST_CASE("bin TLV — reader rejects a truncated buffer") {
    uint8_t b[8] = {1, MSG_DUTY, 0x01, 4, 0, 0};  // claims a 4-byte value but only 2 remain
    TlvReader r; CHECK(reader_init(r, b, 6));
    uint8_t tag, n; const uint8_t* v;
    CHECK_FALSE(reader_next(r, tag, v, n));        // len 4 overruns -> false, no read
}
```

- [ ] **Step 2: Run it — verify it fails to COMPILE** (`console_binary.h` doesn't exist).

Run: `cd /home/staszek/MeshRoute && pio test -e native 2>&1 | grep -E "console_binary.h|error:" | head`
Expected: `fatal error: console_binary.h: No such file or directory`.

- [ ] **Step 3: Create `lib/console/console_binary.h`** with the primitives:

```cpp
// MeshRoute — lib/console/console_binary.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// TLV encoders for the REMOTE (rcmd) data responses — compact, forward-compatible, ≤241 B DM. Serializes the same
// field structs as console_json.h. Remote-only (USB/BLE keep text/JSON). Frame = [ver=1][msg_type] + [tag][len][value LE];
// decoders skip unknown tags. No heap: every buffer is caller-owned. See docs/superpowers/specs/2026-07-13-remote-binary-response-encoders-design.md.
#pragma once
#include <cstdint>
#include <cstddef>

namespace meshroute::console::bin {

inline constexpr uint8_t VER = 1;
inline constexpr uint8_t MSG_STATUS = 0x01, MSG_CFG = 0x02, MSG_DUTY = 0x03, MSG_LIMITS = 0x04,
                         MSG_FAULTS = 0x05, MSG_ROUTES = 0x06, MSG_GATEWAY = 0x07;
inline constexpr uint8_t TAG_TRUNCATED = 0xFE;   // u8 = records omitted (saturating); 0/absent = complete

// ---- writer (returns false on overflow; `off` unchanged on failure) ----
inline bool put_bytes(uint8_t* b, size_t cap, size_t& off, uint8_t tag, const uint8_t* v, uint8_t n) {
    if (off + 2u + n > cap) return false;
    b[off] = tag; b[off + 1] = n;
    for (uint8_t i = 0; i < n; ++i) b[off + 2 + i] = v[i];
    off += 2u + n; return true;
}
inline bool put_u8 (uint8_t* b, size_t cap, size_t& off, uint8_t tag, uint8_t v)  { return put_bytes(b, cap, off, tag, &v, 1); }
inline bool put_u16(uint8_t* b, size_t cap, size_t& off, uint8_t tag, uint16_t v) { uint8_t t[2] = {uint8_t(v), uint8_t(v >> 8)}; return put_bytes(b, cap, off, tag, t, 2); }
inline bool put_u32(uint8_t* b, size_t cap, size_t& off, uint8_t tag, uint32_t v) { uint8_t t[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; return put_bytes(b, cap, off, tag, t, 4); }
inline bool put_i16(uint8_t* b, size_t cap, size_t& off, uint8_t tag, int16_t v)  { return put_u16(b, cap, off, tag, uint16_t(v)); }
inline bool put_i32(uint8_t* b, size_t cap, size_t& off, uint8_t tag, int32_t v)  { return put_u32(b, cap, off, tag, uint32_t(v)); }

inline size_t frame_begin(uint8_t* b, size_t cap, uint8_t msg_type) { if (cap < 2) return 0; b[0] = VER; b[1] = msg_type; return 2; }

// ---- reader ----
struct TlvReader { const uint8_t* p; size_t len; size_t off; uint8_t ver; uint8_t msg_type; };
inline bool reader_init(TlvReader& r, const uint8_t* buf, size_t len) {
    if (len < 2) return false; r.p = buf; r.len = len; r.ver = buf[0]; r.msg_type = buf[1]; r.off = 2; return true;
}
inline bool reader_next(TlvReader& r, uint8_t& tag, const uint8_t*& val, uint8_t& n) {
    if (r.off + 2u > r.len) return false;
    tag = r.p[r.off]; n = r.p[r.off + 1];
    if (r.off + 2u + n > r.len) return false;     // value overruns -> malformed
    val = r.p + r.off + 2; r.off += 2u + n; return true;
}
inline uint8_t  get_u8 (const uint8_t* v, uint8_t n) { return n >= 1 ? v[0] : 0; }
inline uint16_t get_u16(const uint8_t* v, uint8_t n) { uint16_t x = 0; for (uint8_t i = 0; i < n && i < 2; ++i) x |= uint16_t(v[i]) << (8 * i); return x; }
inline uint32_t get_u32(const uint8_t* v, uint8_t n) { uint32_t x = 0; for (uint8_t i = 0; i < n && i < 4; ++i) x |= uint32_t(v[i]) << (8 * i); return x; }
inline int16_t  get_i16(const uint8_t* v, uint8_t n) { return int16_t(get_u16(v, n)); }
inline int32_t  get_i32(const uint8_t* v, uint8_t n) { return int32_t(get_u32(v, n)); }

}  // namespace meshroute::console::bin
```

- [ ] **Step 4: Run the tests — verify they pass.**

Run: `pio test -e native -v 2>&1 | grep -E "console_binary|assertions:|Status:" | tail -4`
Expected: the three `bin TLV …` cases pass; overall `[doctest] Status: SUCCESS!`.

- [ ] **Step 5: Report green (do not commit).**

---

## Task 2: `duty` — the canonical scalar verb (enc + dec)

**Files:**
- Modify: `lib/console/console_binary.h` (add tags + `DutyOut` + decls), `lib/console/console_binary.cpp` (CREATE, add bodies), `test/test_console_binary.cpp`

**Interfaces:**
- Consumes: the Task 1 primitives.
- Produces: `size_t enc_duty(uint8_t* buf, size_t cap, uint8_t pct, uint32_t avail_ms, bool enabled)`; `struct DutyOut { uint8_t pct; uint32_t avail_ms; bool enabled; }`; `bool dec_duty(const uint8_t* buf, size_t len, DutyOut& out)`.

- [ ] **Step 1: Write the failing test** — append to `test/test_console_binary.cpp`:

```cpp
TEST_CASE("bin duty — round-trip") {
    uint8_t b[241];
    size_t n = enc_duty(b, sizeof b, 42, 1500, true);
    CHECK(n > 2); CHECK(n <= 241);
    DutyOut o{};
    CHECK(dec_duty(b, n, o));
    CHECK(o.pct == 42); CHECK(o.avail_ms == 1500u); CHECK(o.enabled == true);
}
TEST_CASE("bin duty — overflow returns 0") {
    uint8_t b[3];                       // header fits, no field
    CHECK(enc_duty(b, sizeof b, 42, 1500, true) == 0);
}
TEST_CASE("bin duty — decoder skips an unknown tag (forward-compat)") {
    uint8_t b[241]; size_t n = enc_duty(b, sizeof b, 7, 0, false);
    size_t off = n; CHECK(put_u16(b, sizeof b, off, 0x7F /*future tag*/, 0xBEEF));  // append unknown
    DutyOut o{}; CHECK(dec_duty(b, off, o));
    CHECK(o.pct == 7); CHECK(o.enabled == false);   // known fields still decoded, unknown skipped
}
```

- [ ] **Step 2: Run — verify it fails** (`enc_duty` undeclared).

Run: `pio test -e native 2>&1 | grep -E "enc_duty|error:" | head`
Expected: `error: 'enc_duty' was not declared`.

- [ ] **Step 3a: Add to `lib/console/console_binary.h`** — before the closing namespace brace:

```cpp
// duty (0x03)
inline constexpr uint8_t TAG_DUTY_PCT = 0x01, TAG_DUTY_AVAIL = 0x02, TAG_DUTY_ENABLED = 0x03;
struct DutyOut { uint8_t pct = 0; uint32_t avail_ms = 0; bool enabled = false; };
size_t enc_duty(uint8_t* buf, size_t cap, uint8_t pct, uint32_t avail_ms, bool enabled);
bool   dec_duty(const uint8_t* buf, size_t len, DutyOut& out);
```

- [ ] **Step 3b: Create `lib/console/console_binary.cpp`**:

```cpp
// MeshRoute — lib/console/console_binary.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "console_binary.h"

namespace meshroute::console::bin {

size_t enc_duty(uint8_t* buf, size_t cap, uint8_t pct, uint32_t avail_ms, bool enabled) {
    size_t off = frame_begin(buf, cap, MSG_DUTY);
    if (!off) return 0;
    if (!put_u8 (buf, cap, off, TAG_DUTY_PCT,     pct))            return 0;
    if (!put_u32(buf, cap, off, TAG_DUTY_AVAIL,   avail_ms))       return 0;
    if (!put_u8 (buf, cap, off, TAG_DUTY_ENABLED, enabled ? 1 : 0))return 0;
    return off;
}
bool dec_duty(const uint8_t* buf, size_t len, DutyOut& out) {
    TlvReader r; if (!reader_init(r, buf, len) || r.msg_type != MSG_DUTY) return false;
    uint8_t tag, n; const uint8_t* v;
    while (reader_next(r, tag, v, n)) switch (tag) {
        case TAG_DUTY_PCT:     out.pct      = get_u8 (v, n); break;
        case TAG_DUTY_AVAIL:   out.avail_ms = get_u32(v, n); break;
        case TAG_DUTY_ENABLED: out.enabled  = get_u8 (v, n) != 0; break;
        default: break;   // forward-compat: skip unknown
    }
    return true;
}

}  // namespace meshroute::console::bin
```

- [ ] **Step 4: Run — verify pass.** Run: `pio test -e native -v 2>&1 | grep -E "bin duty|Status:" | tail -4`. Expected: 3 duty cases pass, `SUCCESS!`.

- [ ] **Step 5: Report green (do not commit).**

---

## Task 3: `limits` (13 u32 fields)

**Files:** Modify `console_binary.h` (tags + `LimitsOut` + decls), `console_binary.cpp`, `test_console_binary.cpp`.

**Interfaces:**
- Consumes: `meshroute::console::LimitsFields` (from `console_json.h`).
- Produces: `size_t enc_limits(uint8_t* buf, size_t cap, const LimitsFields& L)`; `struct LimitsOut` (the 13 u32 fields); `bool dec_limits(const uint8_t*, size_t, LimitsOut&)`.

- [ ] **Step 1: Write the failing test:**

```cpp
#include "console_json.h"   // (add at top if not present) LimitsFields
TEST_CASE("bin limits — round-trip") {
    meshroute::console::LimitsFields L; L.win_ms = 300000; L.win_left_ms = 42000; L.n = 7;
    L.ch_cap = 5; L.ch_used = 2; L.dm_next_ms = 1200; L.duty_used_ms = 88000;
    uint8_t b[241]; size_t n = enc_limits(b, sizeof b, L);
    CHECK(n > 2); CHECK(n <= 241);
    LimitsOut o{}; CHECK(dec_limits(b, n, o));
    CHECK(o.win_ms == 300000u); CHECK(o.n == 7u); CHECK(o.ch_cap == 5u);
    CHECK(o.dm_next_ms == 1200u); CHECK(o.duty_used_ms == 88000u);
}
```

- [ ] **Step 2: Run — verify fail** (`enc_limits` undeclared).

- [ ] **Step 3a: Add to `console_binary.h`** (include `console_json.h` for `LimitsFields`):

```cpp
// at top of console_binary.h, after the <cstddef> include:
#include "console_json.h"   // LimitsFields, StatusFields, RouteRow, CfgExtras, NodeConfig
// ...
// limits (0x04) — tags 0x01..0x0D map to the 13 LimitsFields in declaration order
inline constexpr uint8_t TAG_LIM_WIN_MS=0x01, TAG_LIM_WIN_LEFT=0x02, TAG_LIM_N=0x03, TAG_LIM_CH_SF=0x04,
    TAG_LIM_CH_CAP=0x05, TAG_LIM_CH_USED=0x06, TAG_LIM_CH_MIN=0x07, TAG_LIM_CH_NEXT=0x08, TAG_LIM_CH_CEIL=0x09,
    TAG_LIM_DM_MIN=0x0A, TAG_LIM_DM_NEXT=0x0B, TAG_LIM_DUTY_MS=0x0C, TAG_LIM_DUTY_USED=0x0D;
struct LimitsOut { uint32_t win_ms=0, win_left_ms=0, n=0, ch_sf=0, ch_cap=0, ch_used=0, ch_min_ms=0,
    ch_next_ms=0, ch_ceiling=0, dm_min_ms=0, dm_next_ms=0, duty_ms=0, duty_used_ms=0; };
size_t enc_limits(uint8_t* buf, size_t cap, const LimitsFields& L);
bool   dec_limits(const uint8_t* buf, size_t len, LimitsOut& out);
```

- [ ] **Step 3b: Add to `console_binary.cpp`:**

```cpp
size_t enc_limits(uint8_t* buf, size_t cap, const LimitsFields& L) {
    size_t off = frame_begin(buf, cap, MSG_LIMITS); if (!off) return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_WIN_MS,   L.win_ms))      return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_WIN_LEFT, L.win_left_ms)) return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_N,        L.n))           return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_SF,    L.ch_sf))       return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_CAP,   L.ch_cap))      return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_USED,  L.ch_used))     return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_MIN,   L.ch_min_ms))   return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_NEXT,  L.ch_next_ms))  return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_CEIL,  L.ch_ceiling))  return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_DM_MIN,   L.dm_min_ms))   return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_DM_NEXT,  L.dm_next_ms))  return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_DUTY_MS,  L.duty_ms))     return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_DUTY_USED,L.duty_used_ms))return 0;
    return off;
}
bool dec_limits(const uint8_t* buf, size_t len, LimitsOut& o) {
    TlvReader r; if (!reader_init(r,buf,len) || r.msg_type != MSG_LIMITS) return false;
    uint8_t tag,n; const uint8_t* v;
    while (reader_next(r,tag,v,n)) switch (tag) {
        case TAG_LIM_WIN_MS: o.win_ms=get_u32(v,n); break;      case TAG_LIM_WIN_LEFT: o.win_left_ms=get_u32(v,n); break;
        case TAG_LIM_N: o.n=get_u32(v,n); break;                case TAG_LIM_CH_SF: o.ch_sf=get_u32(v,n); break;
        case TAG_LIM_CH_CAP: o.ch_cap=get_u32(v,n); break;      case TAG_LIM_CH_USED: o.ch_used=get_u32(v,n); break;
        case TAG_LIM_CH_MIN: o.ch_min_ms=get_u32(v,n); break;   case TAG_LIM_CH_NEXT: o.ch_next_ms=get_u32(v,n); break;
        case TAG_LIM_CH_CEIL: o.ch_ceiling=get_u32(v,n); break; case TAG_LIM_DM_MIN: o.dm_min_ms=get_u32(v,n); break;
        case TAG_LIM_DM_NEXT: o.dm_next_ms=get_u32(v,n); break; case TAG_LIM_DUTY_MS: o.duty_ms=get_u32(v,n); break;
        case TAG_LIM_DUTY_USED: o.duty_used_ms=get_u32(v,n); break;
        default: break;
    }
    return true;
}
```

- [ ] **Step 4: Run — verify pass** (`pio test -e native -v 2>&1 | grep -E "bin limits|Status:"`). Expected: pass.
- [ ] **Step 5: Report green (do not commit).**

---

## Task 4: `status` (the remote-diag superset)

**Files:** Modify `console_binary.h`/`.cpp`/`test`.

**Interfaces:**
- Produces: `struct StatusDiag { uint32_t txto=0, rxbad=0, isr=0, rxarm=0, slept=0; uint16_t stackhw=0; uint8_t reset_cause=0, halted=0; int8_t nf_dbm=0; };` (the fields `StatusFields` lacks); `size_t enc_status(uint8_t* buf, size_t cap, uint8_t id, uint32_t key, const StatusFields& s, const StatusDiag& d)`; `struct StatusOut { uint32_t uptime_s,rx,tx,txto,rxbad,isr,rxarm,slept; uint16_t txq,txdrop,stackhw; uint8_t routes,pending,lbt,halted,reset_cause,id; int16_t batt_mv; int8_t nf_dbm; uint32_t key; };`; `bool dec_status(...)`.

- [ ] **Step 1: Write the failing test:**

```cpp
TEST_CASE("bin status — round-trip + batt omitted when <0") {
    meshroute::console::StatusFields s; s.uptime_ms = 1234000; s.rx = 88; s.tx = 12; s.txq = 0;
    s.routes = 7; s.pending = true; s.lbt = true; s.batt_mv = -1; s.duty_ms = 340;
    StatusDiag d; d.rxbad = 2; d.stackhw = 352; d.reset_cause = 3; d.nf_dbm = -110;
    uint8_t b[241]; size_t n = enc_status(b, sizeof b, 9, 0xABCD1234, s, d);
    CHECK(n > 2); CHECK(n <= 241);
    StatusOut o{}; CHECK(dec_status(b, n, o));
    CHECK(o.uptime_s == 1234u);    // ms -> s
    CHECK(o.rx == 88u); CHECK(o.routes == 7u); CHECK(o.pending == 1u);
    CHECK(o.rxbad == 2u); CHECK(o.stackhw == 352u); CHECK(o.reset_cause == 3u);
    CHECK(o.nf_dbm == -110); CHECK(o.id == 9u); CHECK(o.key == 0xABCD1234u);
    CHECK(o.batt_mv == 0);         // omitted -> stays default (the caller reads "absent")
}
```

- [ ] **Step 2: Run — verify fail.**

- [ ] **Step 3a: Add to `console_binary.h`** the tags (per spec §3.3 status registry), `StatusDiag`, `StatusOut`, decls:

```cpp
// status (0x01)
inline constexpr uint8_t TAG_ST_UPTIME=0x01, TAG_ST_RX=0x02, TAG_ST_TX=0x03, TAG_ST_TXQ=0x04, TAG_ST_TXDROP=0x05,
    TAG_ST_TXTO=0x06, TAG_ST_RXBAD=0x07, TAG_ST_ISR=0x08, TAG_ST_RXARM=0x09, TAG_ST_ROUTES=0x0A, TAG_ST_DUTY=0x0B,
    TAG_ST_PENDING=0x0C, TAG_ST_LBT=0x0D, TAG_ST_HALTED=0x0E, TAG_ST_SLEPT=0x0F, TAG_ST_STACKHW=0x10,
    TAG_ST_RESET=0x11, TAG_ST_BATT=0x12, TAG_ST_NF=0x13, TAG_ST_ID=0x14, TAG_ST_KEY=0x15;
struct StatusDiag { uint32_t txto=0, rxbad=0, isr=0, rxarm=0, slept=0; uint16_t stackhw=0; uint8_t reset_cause=0, halted=0; int8_t nf_dbm=0; };
struct StatusOut { uint32_t uptime_s=0, rx=0, tx=0, txto=0, rxbad=0, isr=0, rxarm=0, slept=0, duty_ms=0, key=0;
    uint16_t txq=0, txdrop=0, stackhw=0; int16_t batt_mv=0; uint8_t routes=0, pending=0, lbt=0, halted=0, reset_cause=0, id=0; int8_t nf_dbm=0; };
size_t enc_status(uint8_t* buf, size_t cap, uint8_t id, uint32_t key, const StatusFields& s, const StatusDiag& d);
bool   dec_status(const uint8_t* buf, size_t len, StatusOut& out);
```

- [ ] **Step 3b: Add to `console_binary.cpp`:**

```cpp
size_t enc_status(uint8_t* buf, size_t cap, uint8_t id, uint32_t key, const StatusFields& s, const StatusDiag& d) {
    size_t off = frame_begin(buf, cap, MSG_STATUS); if (!off) return 0;
    if (!put_u32(buf,cap,off,TAG_ST_UPTIME, uint32_t(s.uptime_ms / 1000))) return 0;   // ms -> s
    if (!put_u32(buf,cap,off,TAG_ST_RX, s.rx))      return 0;
    if (!put_u32(buf,cap,off,TAG_ST_TX, s.tx))      return 0;
    if (!put_u16(buf,cap,off,TAG_ST_TXQ, s.txq))    return 0;
    if (!put_u16(buf,cap,off,TAG_ST_TXDROP, s.txdrop)) return 0;
    if (!put_u32(buf,cap,off,TAG_ST_TXTO, d.txto))  return 0;
    if (!put_u32(buf,cap,off,TAG_ST_RXBAD, d.rxbad))return 0;
    if (!put_u32(buf,cap,off,TAG_ST_ISR, d.isr))    return 0;
    if (!put_u32(buf,cap,off,TAG_ST_RXARM, d.rxarm))return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_ROUTES, s.routes)) return 0;
    if (!put_u32(buf,cap,off,TAG_ST_DUTY, s.duty_ms)) return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_PENDING, s.pending?1:0)) return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_LBT, s.lbt?1:0)) return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_HALTED, d.halted)) return 0;
    if (!put_u32(buf,cap,off,TAG_ST_SLEPT, d.slept)) return 0;
    if (!put_u16(buf,cap,off,TAG_ST_STACKHW, d.stackhw)) return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_RESET, d.reset_cause)) return 0;
    if (s.batt_mv >= 0 && !put_i16(buf,cap,off,TAG_ST_BATT, int16_t(s.batt_mv))) return 0;  // omit when <0
    if (!put_u8 (buf,cap,off,TAG_ST_NF, uint8_t(d.nf_dbm))) return 0;    // i8 packed in a 1-byte value
    if (!put_u8 (buf,cap,off,TAG_ST_ID, id))  return 0;
    if (!put_u32(buf,cap,off,TAG_ST_KEY, key)) return 0;
    return off;
}
bool dec_status(const uint8_t* buf, size_t len, StatusOut& o) {
    TlvReader r; if (!reader_init(r,buf,len) || r.msg_type != MSG_STATUS) return false;
    uint8_t tag,n; const uint8_t* v;
    while (reader_next(r,tag,v,n)) switch (tag) {
        case TAG_ST_UPTIME: o.uptime_s=get_u32(v,n); break;  case TAG_ST_RX: o.rx=get_u32(v,n); break;
        case TAG_ST_TX: o.tx=get_u32(v,n); break;            case TAG_ST_TXQ: o.txq=get_u16(v,n); break;
        case TAG_ST_TXDROP: o.txdrop=get_u16(v,n); break;    case TAG_ST_TXTO: o.txto=get_u32(v,n); break;
        case TAG_ST_RXBAD: o.rxbad=get_u32(v,n); break;      case TAG_ST_ISR: o.isr=get_u32(v,n); break;
        case TAG_ST_RXARM: o.rxarm=get_u32(v,n); break;      case TAG_ST_ROUTES: o.routes=get_u8(v,n); break;
        case TAG_ST_DUTY: o.duty_ms=get_u32(v,n); break;     case TAG_ST_PENDING: o.pending=get_u8(v,n); break;
        case TAG_ST_LBT: o.lbt=get_u8(v,n); break;           case TAG_ST_HALTED: o.halted=get_u8(v,n); break;
        case TAG_ST_SLEPT: o.slept=get_u32(v,n); break;      case TAG_ST_STACKHW: o.stackhw=get_u16(v,n); break;
        case TAG_ST_RESET: o.reset_cause=get_u8(v,n); break; case TAG_ST_BATT: o.batt_mv=get_i16(v,n); break;
        case TAG_ST_NF: o.nf_dbm=int8_t(get_u8(v,n)); break; case TAG_ST_ID: o.id=get_u8(v,n); break;
        case TAG_ST_KEY: o.key=get_u32(v,n); break;
        default: break;
    }
    return true;
}
```

- [ ] **Step 4: Run — verify pass.** - [ ] **Step 5: Report green (do not commit).**

---

## Task 5: `cfg` (NodeConfig + CfgExtras; may overflow → 0)

**Files:** Modify `console_binary.h`/`.cpp`/`test`.

**Interfaces:**
- Consumes: `meshroute::NodeConfig`, `meshroute::console::CfgExtras`.
- Produces: `size_t enc_cfg(uint8_t* buf, size_t cap, const NodeConfig& c, const CfgExtras& x)`; `struct CfgOut {…}`; `bool dec_cfg(...)`. Tags per spec §3.3 cfg registry (`0x01 node_id … 0x1B lon_e7`).

- [ ] **Step 1: Write the failing test** (assert the key fields round-trip + a realistic single-layer cfg is ≤241):

```cpp
TEST_CASE("bin cfg — round-trip, fits 241 for a single-layer node") {
    meshroute::NodeConfig c{}; c.routing_sf = 8; c.allowed_sf_bitmap = (1<<7)|(1<<9);
    c.leaf_id = 1; c.is_gateway = false; c.is_mobile = false; c.team_id = 0xABCD; c.config_epoch = 4;
    meshroute::console::CfgExtras x; x.node_id = 17; x.freq_hz = 869525000; x.tx_power = 14; x.lat_e7 = 522297000;
    uint8_t b[241]; size_t n = enc_cfg(b, sizeof b, c, x);
    CHECK(n > 2); CHECK(n <= 241);
    CfgOut o{}; CHECK(dec_cfg(b, n, o));
    CHECK(o.node_id == 17u); CHECK(o.freq_hz == 869525000u); CHECK(o.routing_sf == 8u);
    CHECK(o.sf_list == ((1<<7)|(1<<9))); CHECK(o.team_id == 0xABCDu); CHECK(o.lat_e7 == 522297000);
}
```

- [ ] **Step 2: Run — verify fail.**

- [ ] **Step 3a: Add to `console_binary.h`** — the cfg tags (`0x01..0x1B` mapping node_id/freq_hz/routing_sf/sf_list/bw/cr/tx_power/duty_x1000/beacon_ms/hop_cap/lbt/nav/intra_relay/host_mobiles/leaf_id/is_gateway/is_mobile/team_id/lineage_id/config_epoch/ble_mode/ble_period/ble_pin/loc_dm/e2e_dm/lat_e7/lon_e7), a `CfgOut` mirroring them, and the decls. (Write the `constexpr uint8_t TAG_CFG_* = 0x01…0x1B;` block explicitly, one per field, in the order above.)

- [ ] **Step 3b: Add to `console_binary.cpp`** — `enc_cfg` = one `put_*` per field (u8 for the bool/id/sf/cr, u16 for sf_list/lineage/config_epoch/ble_period, u32 for freq_hz/bw/duty_x1000/beacon_ms/team_id/ble_pin, i8 for tx_power, i32 for lat_e7/lon_e7; ble_mode as a u8 enum 0=off/1=on/2=periodic), each guarded `if (!put_… ) return 0;`. `dec_cfg` = the matching `switch`, `default: break;`. (Follow the Task-3/4 pattern exactly; field list + widths per spec §3.3.)

- [ ] **Step 4: Run — verify pass.** - [ ] **Step 5: Report green (do not commit).**

> Note: a dual-layer/team-heavy cfg can exceed 241 B → `enc_cfg` returns 0. That's the documented v1 behaviour (spec §6); the caller logs the overflow. The field-mask is deferred to remote-auth.

---

## Task 6: `routes` (list — fit-N + truncated)

**Files:** Modify `console_binary.h`/`.cpp`/`test`.

**Interfaces:**
- Consumes: `meshroute::console::RouteRow`.
- Produces: `size_t enc_routes(uint8_t* buf, size_t cap, const RouteRow* rows, uint8_t n, uint8_t* out_truncated)`; `struct RouteOut { RouteRow rows[32]; uint8_t n; uint8_t truncated; }`; `bool dec_routes(const uint8_t* buf, size_t len, RouteOut& out)`. Each route is one TLV: `[TAG_ROUTE_REC][len=9][dest,next,hops,score_lo,score_hi,flags,leaf,age(4)... ]` — a FIXED 12-byte record body packed LE (dest u8,next u8,hops u8,score i16,flags u8,leaf u8,age_ms u32,cand u8 = 12 B).

- [ ] **Step 1: Write the failing test:**

```cpp
TEST_CASE("bin routes — pack N, round-trip, truncated flag") {
    meshroute::console::RouteRow rows[3];
    rows[0] = {10, 11, 1, 40, false, 1, 500, 1};
    rows[1] = {12, 11, 2, 30, true, 1, 1500, 2};
    rows[2] = {13, 12, 3, 20, false, 2, 2500, 1};
    uint8_t b[241]; uint8_t trunc = 9;
    size_t n = enc_routes(b, sizeof b, rows, 3, &trunc);
    CHECK(n > 2); CHECK(trunc == 0);
    RouteOut o{}; CHECK(dec_routes(b, n, o));
    CHECK(o.n == 3); CHECK(o.truncated == 0);
    CHECK(o.rows[1].dest == 12); CHECK(o.rows[1].gw == true); CHECK(o.rows[1].age_ms == 1500u); CHECK(o.rows[2].hops == 3);
}
TEST_CASE("bin routes — truncates when over cap") {
    meshroute::console::RouteRow rows[40];
    for (uint8_t i = 0; i < 40; ++i) rows[i] = {uint8_t(20+i), 11, 1, 10, false, 1, 100, 1};
    uint8_t small[64]; uint8_t trunc = 0;
    size_t n = enc_routes(small, sizeof small, rows, 40, &trunc);
    CHECK(n <= 64); CHECK(trunc > 0);          // some omitted
    RouteOut o{}; CHECK(dec_routes(small, n, o));
    CHECK(o.n < 40); CHECK(o.truncated == trunc);
}
```

- [ ] **Step 2: Run — verify fail.**

- [ ] **Step 3a: Add to `console_binary.h`:**

```cpp
// routes (0x06) — one record TLV per row; a trailing TAG_TRUNCATED u8 when the table didn't fit
inline constexpr uint8_t TAG_ROUTE_REC = 0x01;
inline constexpr uint8_t ROUTE_REC_LEN = 12;   // dest,next,hops,score(2),flags,leaf,age(4),cand
struct RouteOut { meshroute::console::RouteRow rows[32]; uint8_t n = 0; uint8_t truncated = 0; };
size_t enc_routes(uint8_t* buf, size_t cap, const meshroute::console::RouteRow* rows, uint8_t n, uint8_t* out_truncated);
bool   dec_routes(const uint8_t* buf, size_t len, RouteOut& out);
```

- [ ] **Step 3b: Add to `console_binary.cpp`:**

```cpp
size_t enc_routes(uint8_t* buf, size_t cap, const meshroute::console::RouteRow* rows, uint8_t n, uint8_t* out_truncated) {
    size_t off = frame_begin(buf, cap, MSG_ROUTES); if (!off) return 0;
    uint8_t packed = 0;
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t rec[ROUTE_REC_LEN];
        const auto& r = rows[i];
        rec[0]=r.dest; rec[1]=r.next; rec[2]=r.hops;
        rec[3]=uint8_t(r.score); rec[4]=uint8_t(uint16_t(r.score) >> 8);
        rec[5]=uint8_t(r.gw ? 1 : 0); rec[6]=r.leaf;
        rec[7]=uint8_t(r.age_ms); rec[8]=uint8_t(r.age_ms>>8); rec[9]=uint8_t(r.age_ms>>16); rec[10]=uint8_t(r.age_ms>>24);
        rec[11]=r.cand;
        if (!put_bytes(buf, cap, off, TAG_ROUTE_REC, rec, ROUTE_REC_LEN)) break;   // no room -> stop, mark truncated
        ++packed;
    }
    const uint8_t omitted = uint8_t(n - packed);
    if (omitted && !put_u8(buf, cap, off, TAG_TRUNCATED, omitted)) { /* no room for the flag: still report via out param */ }
    if (out_truncated) *out_truncated = omitted;
    return off;
}
bool dec_routes(const uint8_t* buf, size_t len, RouteOut& o) {
    TlvReader r; if (!reader_init(r, buf, len) || r.msg_type != MSG_ROUTES) return false;
    uint8_t tag, n; const uint8_t* v;
    while (reader_next(r, tag, v, n)) {
        if (tag == TAG_ROUTE_REC && n == ROUTE_REC_LEN && o.n < 32) {
            auto& d = o.rows[o.n++];
            d.dest=v[0]; d.next=v[1]; d.hops=v[2];
            d.score=int16_t(uint16_t(v[3]) | (uint16_t(v[4])<<8));
            d.gw = v[5]!=0; d.leaf=v[6];
            d.age_ms = uint32_t(v[7]) | (uint32_t(v[8])<<8) | (uint32_t(v[9])<<16) | (uint32_t(v[10])<<24);
            d.cand=v[11];
        } else if (tag == TAG_TRUNCATED) {
            o.truncated = get_u8(v, n);
        }   // else: unknown tag -> skip
    }
    return true;
}
```

- [ ] **Step 4: Run — verify pass.** - [ ] **Step 5: Report green (do not commit).**

---

## Task 7: `faults` (list — fit-N + truncated)

**Files:** Modify `console_binary.h`/`.cpp`/`test`.

**Interfaces:**
- Produces: `struct FaultRow { uint8_t cause; uint32_t pc, lr; uint16_t count; };`; `size_t enc_faults(uint8_t* buf, size_t cap, const FaultRow* rows, uint8_t n, uint8_t* out_truncated)`; `struct FaultOut { FaultRow rows[16]; uint8_t n; uint8_t truncated; }`; `bool dec_faults(...)`. Record = fixed 11-byte body (`cause u8, pc u32, lr u32, count u16`).

- [ ] **Step 1: Write the failing test** (mirror Task 6's two cases with `FaultRow{cause,pc,lr,count}`; assert round-trip + truncation).
- [ ] **Step 2: Run — verify fail.**
- [ ] **Step 3: Implement** `enc_faults`/`dec_faults` exactly like `enc_routes`/`dec_routes` (Task 6) with `FAULT_REC_LEN = 11` and the `FaultRow` field packing (`cause` u8, `pc`/`lr` u32 LE, `count` u16 LE). `TAG_FAULT_REC = 0x01`.
- [ ] **Step 4: Run — verify pass.** - [ ] **Step 5: Report green (do not commit).**

---

## Task 8: `gateway` (this node's gateway config + schedule)

**Files:** Modify `console_binary.h`/`.cpp`/`test`.

**Interfaces:**
- Produces: `struct GatewayLeaf { uint8_t layer_id, node_id, routing_sf; uint16_t sf_list; uint32_t bw; uint8_t cr; uint32_t window_ms, window_offset_ms; };`; `struct GatewayFields { uint8_t n_layers; uint32_t window_period_ms; GatewayLeaf leaf[2]; };`; `size_t enc_gateway(uint8_t* buf, size_t cap, const GatewayFields& g)`; `struct GatewayOut { GatewayFields g; }`; `bool dec_gateway(...)`. Top-level tags: `TAG_GW_NLAYERS=0x01 (u8)`, `TAG_GW_PERIOD=0x02 (u32)`, `TAG_GW_LEAF=0x03` (a record TLV, one per leaf; body = `layer_id u8, node_id u8, routing_sf u8, sf_list u16, bw u32, cr u8, window_ms u32, window_offset_ms u32` = 16 B).

- [ ] **Step 1: Write the failing test** (build a 2-layer `GatewayFields`, round-trip, assert `n_layers==2`, `leaf[1].window_offset_ms`, `leaf[0].sf_list`).
- [ ] **Step 2: Run — verify fail.**
- [ ] **Step 3: Implement** — `enc_gateway`: `put_u8(n_layers)`, `put_u32(window_period_ms)`, then for each leaf pack the 16-byte body and `put_bytes(TAG_GW_LEAF, body, 16)`. `dec_gateway`: switch — `TAG_GW_NLAYERS`/`TAG_GW_PERIOD` scalars; `TAG_GW_LEAF` (n==16) unpack into `out.g.leaf[idx++]` (guard `idx < 2`); `default: skip`.
- [ ] **Step 4: Run — verify pass.** - [ ] **Step 5: Report green (do not commit).**

---

## Task 9: Robustness sweep + build check

**Files:** Modify `test/test_console_binary.cpp`; no new source.

- [ ] **Step 1: Add cross-cutting tests:**

```cpp
TEST_CASE("bin — a wrong-msg-type buffer is rejected by every decoder") {
    uint8_t b[241]; size_t n = enc_duty(b, sizeof b, 1, 0, false);
    LimitsOut lo{}; CHECK_FALSE(dec_limits(b, n, lo));   // duty frame, limits decoder -> false
    StatusOut so{}; CHECK_FALSE(dec_status(b, n, so));
}
TEST_CASE("bin — a zero-length / 1-byte buffer never crashes a decoder") {
    DutyOut o{}; uint8_t one = MSG_DUTY;
    CHECK_FALSE(dec_duty(nullptr, 0, o));
    CHECK_FALSE(dec_duty(&one, 1, o));
}
TEST_CASE("bin — status decoder tolerates a truncated value at the tail") {
    uint8_t b[241]; size_t n = enc_status(b, sizeof b, 1, 2, {}, {});
    // chop the last byte off -> the final TLV overruns -> reader_next stops early, no OOB (ASAN)
    StatusOut o{}; CHECK(dec_status(b, n - 1, o));   // returns true, partial fields, no crash
}
```

- [ ] **Step 2: Run the full native suite under ASAN** (the native env builds with sanitizers; confirm no OOB in the overflow/truncation paths):

Run: `pio test -e native -v 2>&1 | grep -E "test cases:|assertions:|Status:|ERROR|runtime error" | tail -5`
Expected: all `bin *` cases pass, `[doctest] Status: SUCCESS!`, no ASAN `runtime error`.

- [ ] **Step 3: Confirm the module compiles into a device build** (it's in `lib/console`, compiled by all envs; unused → dead-stripped):

Run: `pio run -e heltec_v3 2>&1 | grep -E "SUCCESS|FAILED|error:" | tail -1`
Expected: `SUCCESS`.

- [ ] **Step 4: Confirm s18/s22 unchanged** (nothing in `lib/core` routing changed):

Run: `cmake --build ~/lora-universal-simulator/build --target lus -j4 >/dev/null && LUS=~/lora-universal-simulator/build/orchestrator/lus && $LUS -e meshroute simulation/s18_meshroute.json /tmp/s18.ndjson >/dev/null 2>&1 && md5sum /tmp/s18.ndjson && $LUS -e meshroute simulation/s22_mobile_team_meshroute.json /tmp/s22.ndjson >/dev/null 2>&1 && md5sum /tmp/s22.ndjson`
Expected: s18 `3ac88d40e00d2605ff66659f696d52bf`, s22 `d5f368a1d275cce5b1e1a0bb60b8753f`.

- [ ] **Step 5: Report green (do not commit).**

---

## Self-Review

- **Spec coverage:** §2 module/location → Task 1 (file created in `lib/console/`, native-tested). §3 format (`[ver][type]`+TLV, skip-unknown, LE) → Task 1 primitives + every `dec_*` `default: break`. §3.1 msg_type registry → Task 1 constants. §3.2 lists fit-N+truncated → Tasks 6/7. §3.3 per-verb registries → Tasks 2–8 (one per verb). §4 API (bounded, 0-on-overflow, `dec_*`) → every task. §5 testing (round-trip, overflow, forward-compat, truncation) → Tasks 2/6 + the Task 9 sweep. §6 out-of-scope (invocation/auth/cfg-mask) → not implemented (correct). §7 self-review → this section.
- **Placeholder scan:** Tasks 5/7/8 Step 3 describe the encode/decode as "follow the Task-3/4/6 pattern exactly" with the full field list + widths given — not a bare "TODO"; the mechanical `put_*`/`switch` body is fully determined by the field list + the worked examples in Tasks 2–4/6. No `TBD`/`add error handling`/vague steps.
- **Type consistency:** `enc_*`/`dec_*` signatures, the `*Out` structs, and the tag constants are named consistently across tasks; `RouteRow`/`LimitsFields`/`StatusFields`/`CfgExtras`/`NodeConfig` come from `console_json.h`; `frame_begin`/`put_*`/`reader_next`/`get_*` names match Task 1 throughout.
- **No-heap / bounds:** every buffer is caller-owned `uint8_t[]`; `put_*` fail-closed on overflow; `reader_next` bounds-checks the value length. The Task-9 ASAN sweep is the safety gate.
