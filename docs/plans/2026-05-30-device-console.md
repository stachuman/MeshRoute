# Device Console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A transport-agnostic device console — line commands in, NDJSON events out — built as a shared heap-free core (`lib/console`) reused by the device backend and the sim's `FirmwareNode`.

**Architecture:** A shared, heap-free, C++17-includable core (parser `text→Command`/`cfg`, bounded JSON writers) in `lib/console`, depending only on already-shipped `lib/core` types (`command.h`, `NodeConfig`, `EventField`). A device-only driver (`ConsoleDevice` + `IConsoleTransport` + `UsbCdcTransport`) binds the core to USB-CDC and the running `Node`. The sim adopts the same core so the wire grammar/schema cannot drift.

**Tech Stack:** C++17/20, PlatformIO, doctest (native tests), Arduino (device), no heap / `-fno-exceptions`.

**Spec:** `docs/specs/2026-05-30-device-console-design.md`.

**Phasing (see spec §15):** Phase A (`lib/console` core + native tests) is greenfield and collision-free — **build now**, on its own branch. Phase B (device driver + `fw_main`) is gated on the H-track device `Hal` (H3) and shares `fw_main`/`lib/hal` with the parallel agent — **coordinate**. Phase C (sim `FirmwareNode` adoption) is a low-risk follow-up — **coordinate**.

**Branch:** before Task A1, `git checkout -b console-core` so Phase A merges cleanly alongside the agent's H-track commits on `main`.

---

## File Structure

| File | Responsibility | Phase |
|---|---|---|
| `lib/console/console_json.{h,cpp}` | bounded heap-free JSON line writers (`JsonBuf` + `write_*`) | A |
| `lib/console/console_parse.{h,cpp}` | `parse_command` (`send`→`Command`), `parse_cfg` (`cfg k v`→`NodeConfig`/id/key) | A |
| `test/test_console_json.cpp` | doctest unit tests for the writers | A |
| `test/test_console_parse.cpp` | doctest unit tests for the parsers | A |
| `lib/hal/iconsole_transport.h` | the transport seam interface | B |
| `lib/hal/console_device.{h,cpp}` | lifecycle/state machine + dispatch; binds core ↔ Node ↔ transport | B |
| `lib/hal/usb_cdc_transport.{h,cpp}` | `IConsoleTransport` over Arduino `Serial` (device-only TU) | B |
| `test/test_console_device.cpp` | `ConsoleDevice` against a `FakeTransport` + stub `Hal` + real `Node` | B |
| `src/fw_main.cpp` (modify) | construct `ConsoleDevice`, `pump()` in `loop()`, route device `Hal` `emit`/`log` → sink | B |
| `…/runtime/FirmwareNode.{cpp,h}` (modify, sim repo) | adopt `console::parse_command` + `write_*`; golden parity test | C |

**Verified types (do not redefine):** `Command`/`SendCmd`/`CmdResult`/`CmdCode`/`Push`/`PushKind` in `lib/core/command.h`; `NodeConfig` in `lib/core/node.h:28`; `EventField` (`{key,type∈{i64,f64,str,boolean},i,f,s,b}`) in `lib/core/hal.h`. `Push` fields: `kind,origin,dst,ctr,body[235],body_len`. `SendCmd` fields: `dst_id,dst_hash,flags` (E2E=`0x08`, PRIORITY=`0x02`).

---

# PHASE A — `lib/console` core (build now, branch `console-core`)

### Task A1: `JsonBuf` — the bounded heap-free writer primitive

**Files:**
- Create: `lib/console/console_json.h`
- Create: `lib/console/console_json.cpp`
- Test: `test/test_console_json.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_console_json.cpp
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_json.h"
#include <cstring>

using namespace meshroute::console;

TEST_CASE("JsonBuf — primitives, escaping, overflow latch") {
    char b[64];
    {   JsonBuf j(b, sizeof b);
        j.lit("{"); j.key("n"); j.i64(-7); j.ch(',');
        j.key("s"); j.str("a\"b\n", 4); j.ch('}');
        size_t len = j.finish();
        CHECK(std::string(b, len) == "{\"n\":-7,\"s\":\"a\\\"b\\n\"}\n");
    }
    {   char tiny[8]; JsonBuf j(tiny, sizeof tiny);   // overflow → finish()==0
        j.lit("123456789");
        CHECK(j.finish() == 0);
        CHECK(j.overflow);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native`
Expected: BUILD FAILS — `console_json.h: No such file or directory` (the RED).

- [ ] **Step 3: Implement `console_json.h` (declarations)**

```cpp
// lib/console/console_json.h
#pragma once
#include "command.h"   // CmdResult, Push, CmdCode, PushKind  (lib/core)
#include "hal.h"       // EventField                          (lib/core)
#include "node.h"      // NodeConfig                          (lib/core)
#include <cstddef>
#include <cstdint>

namespace meshroute::console {

// Bounded, heap-free JSON writer. Every append is overflow-safe: once `cap`
// is reached `overflow` latches and further appends are no-ops; finish()
// then returns 0 so callers never emit a truncated line.
struct JsonBuf {
    char*  buf;
    size_t cap;
    size_t pos = 0;
    bool   overflow = false;
    JsonBuf(char* b, size_t c) : buf(b), cap(c) {}
    void   ch(char c);
    void   lit(const char* s);            // raw literal, no escaping
    void   str(const char* s, size_t n);  // quoted, JSON-escaped string value
    void   key(const char* k);            // `"k":`
    void   i64(int64_t v);
    void   u32(uint32_t v);
    void   f64(double v);
    size_t finish();                       // append '\n', NUL-terminate if room; 0 if overflow
};

// Complete NDJSON line serializers (return bytes written incl. '\n', 0 on overflow).
size_t write_ack   (char* buf, size_t cap, const CmdResult& r);
size_t write_push  (char* buf, size_t cap, const Push& p);
size_t write_event (char* buf, size_t cap, const char* type, const EventField* f, size_t n);
size_t write_log   (char* buf, size_t cap, const char* msg);
size_t write_err   (char* buf, size_t cap, const char* code, const char* msg);  // msg nullable
size_t write_ready (char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* mode);
size_t write_status(char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* state);

const char* cmdcode_name(CmdCode c);
const char* pushkind_name(PushKind k);

}  // namespace meshroute::console
```

- [ ] **Step 4: Implement `JsonBuf` in `console_json.cpp`**

```cpp
// lib/console/console_json.cpp
#include "console_json.h"
#include <cstdio>
#include <cstring>

namespace meshroute::console {

void JsonBuf::ch(char c) {
    if (overflow) return;
    if (pos + 1 >= cap) { overflow = true; return; }  // keep 1 byte for NUL
    buf[pos++] = c;
}
void JsonBuf::lit(const char* s) { while (*s) ch(*s++); }
void JsonBuf::str(const char* s, size_t n) {
    ch('"');
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"':  lit("\\\""); break;
            case '\\': lit("\\\\"); break;
            case '\n': lit("\\n");  break;
            case '\r': lit("\\r");  break;
            case '\t': lit("\\t");  break;
            default:
                if (c < 0x20) { char u[8]; std::snprintf(u, sizeof u, "\\u%04x", c); lit(u); }
                else ch(static_cast<char>(c));
        }
    }
    ch('"');
}
void JsonBuf::key(const char* k) { ch('"'); lit(k); lit("\":"); }
void JsonBuf::i64(int64_t v) { char t[24]; std::snprintf(t, sizeof t, "%lld", static_cast<long long>(v)); lit(t); }
void JsonBuf::u32(uint32_t v) { char t[12]; std::snprintf(t, sizeof t, "%u", v); lit(t); }
void JsonBuf::f64(double v)  { char t[24]; std::snprintf(t, sizeof t, "%.4g", v); lit(t); }
size_t JsonBuf::finish() {
    ch('\n');
    if (overflow) return 0;
    buf[pos] = '\0';   // pos < cap guaranteed by ch()
    return pos;
}

}  // namespace meshroute::console
```

- [ ] **Step 5: Run to verify it passes**

Run: `pio test -e native`
Expected: PASS — `JsonBuf — primitives, escaping, overflow latch` green, all prior suites still green.

- [ ] **Step 6: Commit**

```bash
git add lib/console/console_json.h lib/console/console_json.cpp test/test_console_json.cpp
git commit -m "feat(console): JsonBuf bounded heap-free JSON writer"
```

---

### Task A2: `write_ack` + `write_event` + name tables

**Files:**
- Modify: `lib/console/console_json.cpp`
- Test: `test/test_console_json.cpp:end` (append)

- [ ] **Step 1: Write the failing test (append to test_console_json.cpp)**

```cpp
TEST_CASE("write_ack — CmdResult → ack JSON") {
    char b[96];
    size_t n = write_ack(b, sizeof b, CmdResult{CmdCode::queued, 7, 1});
    CHECK(std::string(b, n) == "{\"ack\":\"queued\",\"ctr\":7,\"qd\":1}\n");
    n = write_ack(b, sizeof b, CmdResult{CmdCode::err_unknown_dst, 0, 0});
    CHECK(std::string(b, n) == "{\"ack\":\"err_unknown_dst\",\"ctr\":0,\"qd\":0}\n");
}

TEST_CASE("write_event — type + typed EventField k/v") {
    char b[128];
    EventField f[2] = {
        { "from", EventField::T::i64,     5, 0,    nullptr, false },
        { "snr",  EventField::T::f64,     0, 7.25, nullptr, false },
    };
    size_t n = write_event(b, sizeof b, "cts_rx", f, 2);
    CHECK(std::string(b, n) == "{\"ev\":\"cts_rx\",\"from\":5,\"snr\":7.25}\n");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native`
Expected: BUILD FAILS — `write_ack`/`write_event`/`cmdcode_name` undefined (RED).

- [ ] **Step 3: Implement (append to console_json.cpp)**

```cpp
const char* cmdcode_name(CmdCode c) {
    switch (c) {
        case CmdCode::queued:              return "queued";
        case CmdCode::err_unknown_dst:     return "err_unknown_dst";
        case CmdCode::err_too_large:       return "err_too_large";
        case CmdCode::err_no_gateway:      return "err_no_gateway";
        case CmdCode::err_priority_capped: return "err_priority_capped";
        case CmdCode::err_no_binding:      return "err_no_binding";
        case CmdCode::err_unsupported:     return "err_unsupported";
    }
    return "err_unknown";
}
const char* pushkind_name(PushKind k) {
    switch (k) {
        case PushKind::msg_recv:    return "msg_recv";
        case PushKind::send_acked:  return "send_acked";
        case PushKind::send_failed: return "send_failed";
    }
    return "unknown";
}
size_t write_ack(char* buf, size_t cap, const CmdResult& r) {
    JsonBuf j(buf, cap);
    j.lit("{\"ack\":\""); j.lit(cmdcode_name(r.code)); j.ch('"');
    j.lit(",\"ctr\":"); j.u32(r.ctr);
    j.lit(",\"qd\":");  j.u32(r.queue_depth);
    j.ch('}');
    return j.finish();
}
size_t write_event(char* buf, size_t cap, const char* type, const EventField* f, size_t n) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\""); j.lit(type); j.ch('"');
    for (size_t i = 0; i < n; ++i) {
        j.ch(','); j.key(f[i].key);
        switch (f[i].type) {
            case EventField::T::i64:     j.i64(f[i].i); break;
            case EventField::T::f64:     j.f64(f[i].f); break;
            case EventField::T::str:     j.str(f[i].s ? f[i].s : "", f[i].s ? std::strlen(f[i].s) : 0); break;
            case EventField::T::boolean: j.lit(f[i].b ? "true" : "false"); break;
        }
    }
    j.ch('}');
    return j.finish();
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native`
Expected: PASS (both new cases green).

- [ ] **Step 5: Commit**

```bash
git add lib/console/console_json.cpp test/test_console_json.cpp
git commit -m "feat(console): write_ack + write_event + name tables"
```

---

### Task A3: `write_push` — the functional channel

**Files:**
- Modify: `lib/console/console_json.cpp`
- Test: `test/test_console_json.cpp` (append)

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("write_push — msg_recv carries escaped body; acked/failed carry dst+ctr") {
    char b[300];
    Push m{}; m.kind = PushKind::msg_recv; m.origin = 3; m.ctr = 7;
    const char* body = "hi\"x"; m.body_len = 4; std::memcpy(m.body, body, 4);
    size_t n = write_push(b, sizeof b, m);
    CHECK(std::string(b, n) == "{\"ev\":\"msg_recv\",\"origin\":3,\"ctr\":7,\"body\":\"hi\\\"x\"}\n");

    Push a{}; a.kind = PushKind::send_acked; a.dst = 5; a.ctr = 7;
    n = write_push(b, sizeof b, a);
    CHECK(std::string(b, n) == "{\"ev\":\"send_acked\",\"dst\":5,\"ctr\":7}\n");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native`
Expected: BUILD FAILS — `write_push` undefined (RED).

- [ ] **Step 3: Implement (append to console_json.cpp)**

```cpp
size_t write_push(char* buf, size_t cap, const Push& p) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\""); j.lit(pushkind_name(p.kind)); j.ch('"');
    if (p.kind == PushKind::msg_recv) {
        j.lit(",\"origin\":"); j.u32(p.origin);
        j.lit(",\"ctr\":");    j.u32(p.ctr);
        j.lit(",\"body\":");   j.str(reinterpret_cast<const char*>(p.body), p.body_len);
    } else {  // send_acked / send_failed
        j.lit(",\"dst\":"); j.u32(p.dst);
        j.lit(",\"ctr\":"); j.u32(p.ctr);
    }
    j.ch('}');
    return j.finish();
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/console/console_json.cpp test/test_console_json.cpp
git commit -m "feat(console): write_push (msg_recv/send_acked/send_failed)"
```

---

### Task A4: `write_err` / `write_log` / `write_ready` / `write_status`

**Files:**
- Modify: `lib/console/console_json.cpp`
- Test: `test/test_console_json.cpp` (append)

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("write_err / write_log / write_ready / write_status") {
    char b[200];
    size_t n = write_err(b, sizeof b, "parse", "expected: send <dst> <body>");
    CHECK(std::string(b, n) == "{\"err\":\"parse\",\"msg\":\"expected: send <dst> <body>\"}\n");
    n = write_err(b, sizeof b, "not_started", nullptr);
    CHECK(std::string(b, n) == "{\"err\":\"not_started\"}\n");
    n = write_log(b, sizeof b, "hello");
    CHECK(std::string(b, n) == "{\"log\":\"hello\"}\n");

    NodeConfig c{}; c.routing_sf = 7; c.data_sf = 12; c.is_gateway = false; c.leaf_id = 0;
    n = write_ready(b, sizeof b, 3, 0xa1b2c3d4u, c, "existing");
    CHECK(std::string(b, n) ==
      "{\"ev\":\"ready\",\"id\":3,\"key\":\"a1b2c3d4\",\"leaf_id\":0,\"mode\":\"existing\",\"gateway\":false,\"routing_sf\":7,\"data_sf\":12}\n");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native`
Expected: BUILD FAILS — symbols undefined (RED).

- [ ] **Step 3: Implement (append to console_json.cpp)**

```cpp
size_t write_log(char* buf, size_t cap, const char* msg) {
    JsonBuf j(buf, cap);
    j.lit("{\"log\":"); j.str(msg ? msg : "", msg ? std::strlen(msg) : 0); j.ch('}');
    return j.finish();
}
size_t write_err(char* buf, size_t cap, const char* code, const char* msg) {
    JsonBuf j(buf, cap);
    j.lit("{\"err\":"); j.str(code, std::strlen(code));
    if (msg) { j.lit(",\"msg\":"); j.str(msg, std::strlen(msg)); }
    j.ch('}');
    return j.finish();
}
static void key_hex32(JsonBuf& j, uint32_t key) {
    char t[16]; std::snprintf(t, sizeof t, "\"%08x\"", key); j.lit(t);
}
size_t write_ready(char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* mode) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"ready\",\"id\":"); j.u32(id);
    j.lit(",\"key\":"); key_hex32(j, key);
    j.lit(",\"leaf_id\":"); j.u32(c.leaf_id);
    j.lit(",\"mode\":"); j.str(mode, std::strlen(mode));
    j.lit(",\"gateway\":"); j.lit(c.is_gateway ? "true" : "false");
    j.lit(",\"routing_sf\":"); j.u32(c.routing_sf);
    j.lit(",\"data_sf\":"); j.u32(c.data_sf);
    j.ch('}');
    return j.finish();
}
size_t write_status(char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* state) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"status\",\"id\":"); j.u32(id);
    j.lit(",\"key\":"); key_hex32(j, key);
    j.lit(",\"state\":"); j.str(state, std::strlen(state));
    j.lit(",\"leaf_id\":"); j.u32(c.leaf_id);
    j.lit(",\"gateway\":"); j.lit(c.is_gateway ? "true" : "false");
    j.lit(",\"routing_sf\":"); j.u32(c.routing_sf);
    j.lit(",\"data_sf\":"); j.u32(c.data_sf);
    j.ch('}');
    return j.finish();
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/console/console_json.cpp test/test_console_json.cpp
git commit -m "feat(console): write_err/log/ready/status"
```

---

### Task A5: `parse_command` — `send <dst> <body>` → `Command`

**Files:**
- Create: `lib/console/console_parse.h`
- Create: `lib/console/console_parse.cpp`
- Test: `test/test_console_parse.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_console_parse.cpp
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_parse.h"
#include <cstring>

using namespace meshroute::console;

TEST_CASE("parse_command — send <dst> <body>") {
    const char* line = "send 5 hello world";
    Command c{};
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == meshroute::CmdKind::send);
    CHECK(c.u.send.dst_id == 5);
    CHECK(c.u.send.flags == 0x08);                 // E2E default
    CHECK(c.body_len == 11);                        // "hello world"
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hello world");
}

TEST_CASE("parse_command — errors") {
    Command c{};
    CHECK(parse_command("ping 5 x", 8, c) == ParseErr::unknown_verb);
    CHECK(parse_command("send x hi", 9, c) == ParseErr::bad_args);   // non-numeric dst
    CHECK(parse_command("send 999 hi", 11, c) == ParseErr::bad_args); // dst > 254
    CHECK(parse_command("", 0, c) == ParseErr::empty);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native`
Expected: BUILD FAILS — `console_parse.h` missing (RED).

- [ ] **Step 3: Implement `console_parse.h`**

```cpp
// lib/console/console_parse.h
#pragma once
#include "command.h"   // Command, CmdKind, SendCmd
#include "node.h"      // NodeConfig
#include <cstddef>
#include <cstdint>

namespace meshroute::console {

enum class ParseErr : uint8_t { ok, empty, unknown_verb, bad_args };

// Parses a `send <dst> <body...>` line into `out`. `out.body` BORROWS into
// `line` (valid only while `line` lives — caller passes its line buffer and
// calls Node::on_command before reusing it). Only `send` is handled here;
// control verbs (cfg/start/verbose/status) are dispatched by the caller.
ParseErr parse_command(const char* line, size_t len, Command& out);

enum class CfgErr : uint8_t { ok, unknown_key, bad_value };

// Parses one `cfg <key> <val>` line, mutating the targets in place.
CfgErr parse_cfg(const char* line, size_t len, NodeConfig& cfg,
                 uint8_t& node_id, uint32_t& key_hash32);

}  // namespace meshroute::console
```

- [ ] **Step 4: Implement `parse_command` in `console_parse.cpp`**

```cpp
// lib/console/console_parse.cpp
#include "console_parse.h"
#include <cstring>

namespace meshroute::console {
namespace {

struct Scan { const char* p; const char* end; };
void skip_ws(Scan& s) { while (s.p < s.end && (*s.p == ' ' || *s.p == '\t')) ++s.p; }
// Reads a non-space token; returns {start,len}. len==0 at end.
struct Tok { const char* s; size_t n; };
Tok token(Scan& s) { skip_ws(s); const char* b = s.p; while (s.p < s.end && *s.p != ' ' && *s.p != '\t') ++s.p; return { b, static_cast<size_t>(s.p - b) }; }
bool tok_eq(const Tok& t, const char* lit) { return t.n == std::strlen(lit) && std::memcmp(t.s, lit, t.n) == 0; }
// Parse decimal token into [0,max]; returns false on empty/non-digit/overflow.
bool parse_u32_tok(const Tok& t, uint32_t max, uint32_t& out) {
    if (t.n == 0) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < t.n; ++i) { char c = t.s[i]; if (c < '0' || c > '9') return false; v = v * 10 + static_cast<uint32_t>(c - '0'); if (v > max) return false; }
    out = v; return true;
}

}  // namespace

ParseErr parse_command(const char* line, size_t len, Command& out) {
    Scan s{ line, line + len };
    Tok verb = token(s);
    if (verb.n == 0) return ParseErr::empty;
    if (!tok_eq(verb, "send")) return ParseErr::unknown_verb;

    Tok dst = token(s);
    uint32_t dst_id = 0;
    if (!parse_u32_tok(dst, 254, dst_id)) return ParseErr::bad_args;

    // body = remainder after exactly one separating space (verbatim, incl. spaces).
    if (s.p < s.end && (*s.p == ' ' || *s.p == '\t')) ++s.p;
    size_t body_len = static_cast<size_t>(s.end - s.p);
    if (body_len > protocol::max_payload_bytes_hard_cap) body_len = protocol::max_payload_bytes_hard_cap;

    out = Command{};
    out.kind = CmdKind::send;
    out.u.send.dst_id = static_cast<uint8_t>(dst_id);
    out.u.send.dst_hash = 0;
    out.u.send.flags = 0x08;  // E2E (command.h: E2E=0x08); PRIORITY=0x02 deferred
    out.body = reinterpret_cast<const uint8_t*>(s.p);
    out.body_len = static_cast<uint8_t>(body_len);
    return ParseErr::ok;
}

}  // namespace meshroute::console
```

- [ ] **Step 5: Run to verify it passes**

Run: `pio test -e native`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/console/console_parse.h lib/console/console_parse.cpp test/test_console_parse.cpp
git commit -m "feat(console): parse_command (send -> Command, E2E default)"
```

---

### Task A6: `parse_cfg` — `cfg <key> <val>` → `NodeConfig`/id/key

**Files:**
- Modify: `lib/console/console_parse.cpp`
- Test: `test/test_console_parse.cpp` (append)

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("parse_cfg — keys map to NodeConfig/id/key") {
    NodeConfig c{}; uint8_t id = 0; uint32_t key = 0;
    CHECK(parse_cfg("cfg id 3", 8, c, id, key) == CfgErr::ok);          CHECK(id == 3);
    CHECK(parse_cfg("cfg routing_sf 9", 16, c, id, key) == CfgErr::ok); CHECK(c.routing_sf == 9);
    CHECK(parse_cfg("cfg data_sf 12", 14, c, id, key) == CfgErr::ok);   CHECK(c.data_sf == 12);
    CHECK(parse_cfg("cfg gateway 1", 13, c, id, key) == CfgErr::ok);    CHECK(c.is_gateway == true);
    CHECK(parse_cfg("cfg key a1b2c3d4", 17, c, id, key) == CfgErr::ok); CHECK(key == 0xa1b2c3d4u);
    CHECK(parse_cfg("cfg routing_sf 99", 17, c, id, key) == CfgErr::bad_value);  // SF out of 5..12
    CHECK(parse_cfg("cfg nope 1", 10, c, id, key) == CfgErr::unknown_key);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native`
Expected: BUILD FAILS — `parse_cfg` undefined (RED).

- [ ] **Step 3: Implement (append to console_parse.cpp, reuse Task A5 helpers)**

```cpp
// add near the other anonymous-namespace helpers:
namespace {
bool parse_hex32_tok(const Tok& t, uint32_t& out) {
    if (t.n == 0 || t.n > 8) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < t.n; ++i) {
        char c = t.s[i]; uint32_t d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
        else return false;
        v = (v << 4) | d;
    }
    out = v; return true;
}
}  // namespace

CfgErr parse_cfg(const char* line, size_t len, NodeConfig& cfg,
                 uint8_t& node_id, uint32_t& key_hash32) {
    Scan s{ line, line + len };
    Tok verb = token(s);
    if (!tok_eq(verb, "cfg")) return CfgErr::unknown_key;  // dispatched here only for cfg
    Tok key = token(s);
    Tok val = token(s);

    uint32_t u = 0;
    if (tok_eq(key, "id")) {
        if (!parse_u32_tok(val, 254, u)) return CfgErr::bad_value; node_id = static_cast<uint8_t>(u);
    } else if (tok_eq(key, "key")) {
        if (!parse_hex32_tok(val, key_hash32)) return CfgErr::bad_value;
    } else if (tok_eq(key, "routing_sf")) {
        if (!parse_u32_tok(val, 12, u) || u < 5) return CfgErr::bad_value; cfg.routing_sf = static_cast<uint8_t>(u);
    } else if (tok_eq(key, "data_sf")) {
        if (!parse_u32_tok(val, 12, u) || u < 5) return CfgErr::bad_value; cfg.data_sf = static_cast<uint8_t>(u);
    } else if (tok_eq(key, "gateway")) {
        if (tok_eq(val, "1") || tok_eq(val, "true")) cfg.is_gateway = true;
        else if (tok_eq(val, "0") || tok_eq(val, "false")) cfg.is_gateway = false;
        else return CfgErr::bad_value;
    } else if (tok_eq(key, "beacon_period_ms")) {
        if (!parse_u32_tok(val, 0xFFFFFFFFu, u)) return CfgErr::bad_value; cfg.beacon_period_ms = u;
    } else if (tok_eq(key, "leaf_id")) {
        if (!parse_u32_tok(val, 254, u)) return CfgErr::bad_value; cfg.leaf_id = static_cast<uint8_t>(u);
    } else {
        return CfgErr::unknown_key;
    }
    return CfgErr::ok;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native`
Expected: PASS — Phase A complete (the shared core is fully unit-tested).

- [ ] **Step 5: Commit**

```bash
git add lib/console/console_parse.cpp test/test_console_parse.cpp
git commit -m "feat(console): parse_cfg (NodeConfig/id/key, validated)"
```

**End of Phase A.** `lib/console` is a complete, host-verified shared core. Push the `console-core` branch; it merges cleanly onto `main` regardless of the H-track (no shared files touched).

---

# PHASE B — device driver + `fw_main` (GATED on the H-track device `Hal`/H3)

> **Coordination gate:** these tasks touch `lib/hal` + `src/fw_main.cpp` — the parallel agent's active area — and depend on the device `Hal` facade (H3) for real TX/RX and for the `emit`/`log` forwarding hook. **Do not start B until the device `Hal` lands**, and have the agent leave one hook: the device `Hal`'s `emit`/`log` call `ConsoleDevice::on_emit(type,fields,n)` / `on_log(msg)` (instead of formatting JSON themselves). The interfaces below are final (they're console-owned); only the `fw_main` wiring (B4) depends on the agent's `Hal` shape.

### Task B1: `IConsoleTransport` + `FakeTransport`

**Files:**
- Create: `lib/hal/iconsole_transport.h`
- Test: `test/test_console_device.cpp` (the `FakeTransport` test double lives here)

- [ ] **Step 1: Define the interface**

```cpp
// lib/hal/iconsole_transport.h
#pragma once
#include <cstddef>
namespace meshroute {
struct IConsoleTransport {
    virtual ~IConsoleTransport() = default;
    // Non-blocking: if a complete '\n'-terminated line is available, copy it
    // (without the newline) into buf[0..cap), set len, return true; else false.
    virtual bool read_line(char* buf, size_t cap, size_t& len) = 0;
    virtual void write(const char* buf, size_t len) = 0;
};
}  // namespace meshroute
```

- [ ] **Step 2: Add a `FakeTransport` in the test (queued input lines, captured output)** — a `std::vector<std::string>` input queue + an output string; `read_line` pops one queued line, `write` appends. (Native test code may use STL freely.) Commit with B2.

### Task B2: `ConsoleDevice` — lifecycle + dispatch (TDD against `FakeTransport` + stub `Hal` + real `Node`)

**Files:**
- Create: `lib/hal/console_device.{h,cpp}`
- Test: `test/test_console_device.cpp`

`ConsoleDevice` owns: a line buffer (`char[288]` — fits `send <dst> ` + 235-byte body + slack), the `verbose` flag, the lifecycle `State{boot,config,operating}`, the accumulated `NodeConfig`/`node_id`/`key`, and references to the `IConsoleTransport` + (post-`start`) the `Node`. Its API:

```cpp
// lib/hal/console_device.h (sketch — finalized at execution)
class ConsoleDevice {
public:
    ConsoleDevice(IConsoleTransport& tx, uint32_t hw_key /*FICR-derived*/);
    void pump(Node* node_or_null);     // node non-null once started; called from loop()
    void on_emit(const char* type, const EventField* f, size_t n);  // device Hal -> here (verbose-gated)
    void on_log(const char* msg);
private:
    void handle_line(const char* line, size_t len, Node*& node);  // dispatch cfg/start/send/verbose/status
};
```

TDD cases (against `FakeTransport`, a stub `Hal` like the one in `test/test_node_r3.cpp`, and a real `Node`): `send` before `start` → `{"err":"not_started"}`; `cfg routing_sf 8` → `{"ack":"cfg",…}`; `start` → `{"ev":"ready",…}` then `cfg` → `{"err":"already_started"}`; `send 5 hi` after start → `{"ack":"queued",…}` and (driving the peer path) a drained `Push` → `msg_recv` JSON; an over-length line → `{"err":"line_too_long"}`. Each case: write failing test → run (RED) → implement the dispatch branch → run (GREEN) → commit. The dispatch uses `console::parse_command`/`parse_cfg` and `console::write_*` from Phase A.

### Task B3: `UsbCdcTransport` (device-only TU)

**Files:** Create `lib/hal/usb_cdc_transport.{h,cpp}` — `IConsoleTransport` over Arduino `Serial`: `read_line` accumulates `Serial.read()` into a static line buffer until `'\n'`; `write` calls `Serial.write`. Not native-tested (Arduino dep) — bench-verified in B4. Guard with the device build (`#if !MESHROUTE_NATIVE`).

### Task B4: `fw_main` integration (the one coordinated edit)

**Files:** Modify `src/fw_main.cpp`. In `setup()`: derive `hw_key` from `NRF_FICR->DEVICEID` (fold 64→32, e.g. FNV-1a), construct `UsbCdcTransport` + `ConsoleDevice`. In `loop()`: call `console.pump(node)`. Wire the device `Hal`'s `emit`/`log` to `console.on_emit`/`on_log`. **This is the merge point with the agent** — apply as a small delimited block once the device `Hal` + `Node` construction exist in `fw_main`. Bench-verify: USB `cfg`/`start`/`send`, then the 2-device exchange (H3).

---

# PHASE C — sim `FirmwareNode` adoption (coordinate; low-risk)

### Task C1: `FirmwareNode` adopts the shared core + golden parity test

**Files (sim repo):** Modify `orchestrator/runtime/FirmwareNode.{cpp,h}`. Replace `onCommand`'s bespoke parsing (`FirmwareNode.cpp:74`) with `console::parse_command`; route its push/event emission through `console::write_*` into a buffer it writes to its NDJSON stream. Add a golden test asserting device-core JSON == sim-core JSON for a fixed `CmdResult`/`Push`/event set. Coordinate timing so the agent isn't mid-edit in `FirmwareNode`. (The sim links `lib/console` the same way it links `lib/core`.)

---

## Self-Review

**Spec coverage:** §2 module layout → File Structure + Tasks A1–A6/B1–B4/C1. §3 grammar → A5 (`send`), A6 (`cfg`), B2 (`start`/`verbose`/`status` dispatch). §4 JSON schema → A2 (ack/event), A3 (push), A4 (err/ready/status/log). §5 lifecycle → B2. §6 pump → B4. §7 errors → JsonBuf overflow (A1), parse errors (A5/A6), state errors + `line_too_long` (B2). §8 A2 → C1. §9 identity → B4 (FICR fold), A6 (`cfg id`/`key`). §10 testing → A* unit tests, B2 integration, C1 golden. §11 transport roadmap → B1/B3 (USB) with BLE/WiFi out of scope. §15 phasing → the Phase A/B/C structure + the branch + the coordination gate. **No gaps.**

**Placeholder scan:** Phase A steps carry complete code. Phase B/C are intentionally interface-complete + task-structured but **gated** on the agent's device `Hal` (B) and the sim repo (C); their exact `fw_main`/`FirmwareNode` diffs finalize when those land — flagged explicitly, not hidden TODOs.

**Type consistency:** `CmdCode`/`PushKind` names match `command.h`; `EventField::T` arms match `hal.h`; `NodeConfig` fields (`routing_sf`/`data_sf`/`is_gateway`/`leaf_id`/`beacon_period_ms`) match `node.h:28`; `Command.u.send.{dst_id,dst_hash,flags}` and the `0x08` E2E flag match `command.h`; `protocol::max_payload_bytes_hard_cap` (235) used consistently in A5 and the B2 line buffer.
