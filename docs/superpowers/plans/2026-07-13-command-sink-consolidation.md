<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Command-Sink Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Collapse the three drifting command dispatchers (serial `service_debug`, BLE `ble_dispatch_line`, remote `remote_exec`) onto **one** `dispatch(line, len, Print& out)` so every transport reuses the real `handle_*`/`dump_*` handlers instead of re-implementing them.

**Architecture:** The abstraction already half-exists — `mrcon` is a `Print`. Give every command-**response** producer a `Print& out` param (its response `mrcon.`→`out.`; its *debug* prints stay on `mrcon`). Extract the `service_debug` verb table into `dispatch(line, len, out)`. The three transports become thin adapters that pick a sink: serial passes `mrcon`; BLE passes a line-flushing `LineSink`; remote passes a capturing `BufferSink`. Pure refactor — output per command is byte-identical.

**Tech Stack:** C++20, PlatformIO (device envs `xiao_sx1262`/`heltec_v3`/`gateway`/`production` + `native` doctest host), Arduino `Print`. Spec: `docs/superpowers/specs/2026-07-13-command-sink-consolidation-design.md`.

## Global Constraints

- **The user does ALL git commits.** Never run `git commit` or offer to. Every task ends at "verify green + boards build" and is **left uncommitted for the user**. (Wherever a step below says "hand off", it means: report the task green, do not commit.)
- **Pure refactor — zero behaviour change.** Output per command must be **byte-identical** to the pre-refactor baseline on every transport. The gate is the on-device **output-parity battery** (Task 9), captured from the committed baseline BEFORE any task starts.
- **Debug-vs-response sink rule (the core discipline):** `out` carries a command's **RESPONSE only**. **Debug/diagnostic output is USB/serial-only** — it stays on the global `mrcon`, NEVER routed through `out` (never captured for BLE/remote). This applies to the ~56 boot/loop/push `mrcon` sites (untouched) **and** to any debug/trace line *inside* a handler (stays `mrcon.`, does NOT become `out.`). Each handler batch is a *read-and-classify*, not a blind `sed`.
- **`Sink = Print&`.** Do NOT invent a bespoke sink interface. `mrcon` (a `GuardedConsole : Print`) is the serial sink; new sinks derive `Print`.
- **s18 byte-identity untouched:** this is device-side (`fw_main`/console); `lib/core` routing is out of scope. `md5(s18_meshroute) == 3ac88d40e00d2605ff66659f696d52bf` must still hold (verify once, Task 9).
- s22 mobile part byte-identity untouched.
- **No-heap:** sinks are fixed-size `char[]` members (no `String`/malloc), matching `protocol_constants.h`.
- **Source header:** every new file carries `// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>` on line 2.
- **Boards-green per task:** after each task, `pio run -e xiao_sx1262 -e heltec_v3 -e gateway` build SUCCESS (the console is device-side; `native` does not compile `fw_main`).

---

## File Structure

- **`src/dispatch_sink.h`** (CREATE) — the two capture sinks: `BufferSink : Print` (accumulate to a bounded `char[]`, for remote/`rcmd` + reference capture) and `LineSink : Print` (line-buffered, flush each `'\n'`-terminated line to a callback, for BLE streaming). Arduino-`Print`-based, device-side.
- **`src/console_sink.h`** (unchanged) — the existing global `mrcon` (`GuardedConsole`). Referenced, not modified.
- **`src/fw_main.cpp`** (MODIFY, the bulk) — add `Print& out` to the 8 `dump_*` + 23 `handle_*` response producers; extract `dispatch(line,len,out)` from `service_debug`; rewire `ble_dispatch_line` + `remote_exec` to `dispatch`.
- **`test/console_battery.txt`** (CREATE) — the fixed command battery fed to a board (serial) + BLE to capture the byte-for-byte parity reference (Task 9).

**Note on native testability (spec §3.2):** the dispatch + handlers live in `fw_main.cpp` and depend on `g_node`/`g_hal`/`mrble`/Arduino `Print` — `native` (doctest) does **not** compile them, and `Print` is `#if defined(ARDUINO)`-only. A native `dispatch(line,testSink)` test therefore requires a Print shim on native **and** extracting dispatch+handlers into a native-compilable TU — a separate sub-project, **out of scope here**. The regression backbone for this plan is the **on-device output-parity battery** (Task 9). Native extraction is noted as a follow-on in Task 10.

---

## Task 1: The capture sinks (`BufferSink`, `LineSink`)

**Files:**
- Create: `src/dispatch_sink.h`

**Interfaces:**
- Produces:
  - `class BufferSink : public Print` — `const char* data() const`, `size_t len() const`, `bool truncated() const`, `void reset()`; overrides `write(uint8_t)`, `write(const uint8_t*,size_t)`, `printf(fmt,...)`, `flush()`.
  - `class LineSink : public Print` — ctor `explicit LineSink(void(*flush)(const char*,size_t))`; same `Print` overrides; `flush()` ships a trailing partial line.

- [ ] **Step 1: Create `src/dispatch_sink.h`** with both sinks (mirrors `GuardedConsole`'s `Print` API so handlers written against `mrcon` work unchanged):

```cpp
// MeshRoute — src/dispatch_sink.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §command-sink-consolidation: the two CAPTURE sinks a non-serial transport passes to dispatch(line,len,out).
// Both are Print (so a handler written against `mrcon` — also a Print — works verbatim). BufferSink accumulates the
// whole response into a bounded char[] (remote/rcmd + the parity-reference capture); LineSink flushes each complete
// '\n'-terminated line to a callback (BLE streaming — routes/pull_inbox emit many lines, unbounded). Fixed-size,
// no-heap. Serial keeps passing the global `mrcon` (unchanged). Debug output NEVER reaches these — it stays on mrcon.
#pragma once
#if defined(ARDUINO)
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

class BufferSink : public Print {
public:
    using Print::write;
    size_t write(uint8_t b) override {
        if (_len + 1 < sizeof _buf) _buf[_len++] = static_cast<char>(b); else _truncated = true;
        return 1;
    }
    size_t write(const uint8_t* p, size_t n) override { for (size_t i = 0; i < n; ++i) write(p[i]); return n; }
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char b[160]; va_list ap; va_start(ap, fmt);
        const int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        if (n > 0) write(reinterpret_cast<const uint8_t*>(b), static_cast<size_t>(n) < sizeof b ? static_cast<size_t>(n) : sizeof b - 1);
        return n;
    }
    void flush() {}
    const char* data() const { return _buf; }
    size_t      len()  const { return _len; }
    bool        truncated() const { return _truncated; }   // response exceeded the buffer (LOUD: mark it, never silently drop)
    void        reset() { _len = 0; _truncated = false; _buf[0] = '\0'; }
private:
    char   _buf[512] = {};   // bounded: remote/rcmd responses are single-line (<256); status/cfg fit. Streaming uses LineSink.
    size_t _len = 0;
    bool   _truncated = false;
};

class LineSink : public Print {
public:
    using Print::write;
    using Flush = void (*)(const char* line, size_t n);
    explicit LineSink(Flush f) : _flush(f) {}
    size_t write(uint8_t b) override {
        if (_len < sizeof _buf) _buf[_len++] = static_cast<char>(b);
        if (b == '\n' || _len == sizeof _buf) { _flush(_buf, _len); _len = 0; }   // ship on newline OR buffer-full (never overflow)
        return 1;
    }
    size_t write(const uint8_t* p, size_t n) override { for (size_t i = 0; i < n; ++i) write(p[i]); return n; }
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char b[160]; va_list ap; va_start(ap, fmt);
        const int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        if (n > 0) write(reinterpret_cast<const uint8_t*>(b), static_cast<size_t>(n) < sizeof b ? static_cast<size_t>(n) : sizeof b - 1);
        return n;
    }
    void flush() { if (_len) { _flush(_buf, _len); _len = 0; } }   // ship any trailing partial (no-newline) line
private:
    char   _buf[300] = {};   // one console line (the widest single NDJSON record + envelope)
    Flush  _flush;
    size_t _len = 0;
};
#endif  // ARDUINO
```

- [ ] **Step 2: Verify it compiles** (no callers yet — a header-only compile check):

Run: `cd /home/staszek/MeshRoute && pio run -e xiao_sx1262 2>&1 | grep -E "SUCCESS|error:"`
Expected: `SUCCESS` (the header is unused but must parse; `#include "dispatch_sink.h"` gets added in Task 6/7).

- [ ] **Step 3: Hand off** — report green (no commit; the user commits).

---

## Task 2: Migrate the `JsonSink` handlers to `Print& out`

The three `JsonSink` handlers are already sink-shaped — swap the func-ptr for `Print&` first (smallest, proves the pattern). They STREAM per-line, so `out` will be a `LineSink` on BLE.

**Files:**
- Modify: `src/fw_main.cpp` — `handle_routes` (1715), `handle_pull_inbox` (1400), `handle_mark_read` (1413), the `JsonSink` typedef (1361), `PullCtx` (1365), and their call sites in `service_debug`/`ble_dispatch_line`.

**Interfaces:**
- Consumes: nothing (first migration).
- Produces: `void handle_routes(Print& out)`, `void handle_pull_inbox(const char* args, Print& out)`, `void handle_mark_read(const char* args, Print& out)`. The `JsonSink` typedef and `PullCtx.sink` are DELETED.

- [ ] **Step 1: Change the three signatures + bodies.** Replace the `JsonSink sink` param with `Print& out`, and every `sink(buf, n)` call with `out.write(reinterpret_cast<const uint8_t*>(buf), n)`. Delete `using JsonSink = …;` (1361) and change `struct PullCtx { JsonSink sink; uint32_t count; };` → `struct PullCtx { Print& out; uint32_t count; };`. Inside the handlers the NDJSON lines already end in `'\n'` — a `LineSink` will ship one BLE line per record (framing preserved).

- [ ] **Step 2: Update the serial call sites** in `service_debug` (fw_main.cpp:1622+): `dump_routes()` stays serial for now (Task 3 migrates the `dump_*`); the `routes`/`pull_inbox`/`mark_read` verbs that currently call the `JsonSink` form with `ble_sink` are BLE-only today — point their serial path at `mrcon` and their BLE path (Task 6) at a `LineSink`. For this task, make the existing callers pass `mrcon` (serial parity) and leave the BLE reimplementation in place (Task 6 deletes it).

- [ ] **Step 3: Build the boards.**

Run: `cd /home/staszek/MeshRoute && pio run -e xiao_sx1262 -e heltec_v3 -e gateway 2>&1 | grep -E "SUCCESS|FAILED|error:"`
Expected: all `SUCCESS`.

- [ ] **Step 4: Hand off** — report green (user commits).

---

## Task 3: Migrate the `dump_*` response helpers to `Print& out`

The verb table calls eight `dump_*`/`print_banner` helpers that write the response to the global `mrcon`. Give each a `Print& out` param.

**Files:**
- Modify: `src/fw_main.cpp` — `dump_routes` (196), `dump_cfg` (266), `print_banner` (356), `dump_status` (377), `dump_duty` (416), `dump_help` (1301), `dump_limits` (1371), `dump_faults` (1430), and their callers in `service_debug` + `setup()` (`print_banner` is also the boot banner — see Step 3).

**Interfaces:**
- Produces: `void dump_routes(Print& out)`, `void dump_cfg(Print& out)`, `void print_banner(Print& out)`, `void dump_status(Print& out)`, `void dump_duty(Print& out)`, `void dump_help(Print& out)`, `void dump_limits(Print& out)`, `void dump_faults(Print& out)`.

- [ ] **Step 1: Worked example — `dump_status`.** Apply this exact transformation to each of the eight:

```cpp
// BEFORE
static void dump_status() {
    mrcon.print(F("  up=")); mrcon.print(g_hal.now() / 1000); mrcon.println(F("s"));
    // … more mrcon.… (all RESPONSE lines) …
}
// AFTER
static void dump_status(Print& out) {
    out.print(F("  up=")); out.print(g_hal.now() / 1000); out.println(F("s"));
    // … same lines, mrcon.→out. …
}
```

Rule: **response** `mrcon.`→`out.`. If a `dump_*` contains a genuinely *diagnostic* line (rare — e.g. a `[dbg]`-tagged trace), leave it `mrcon.` per the debug rule. Read each one; do not blind-replace.

- [ ] **Step 2: Update the `service_debug` call sites** to pass a sink: e.g. `dump_status()` → `dump_status(out)` (where `out` is `dispatch`'s param, introduced in Task 5; until then, the caller in `service_debug` passes `mrcon` explicitly — `dump_status(mrcon)` — so serial output is byte-identical).

- [ ] **Step 3: `print_banner` has two callers** — the `version` verb (response → gets `out`) AND the boot banner in `setup()` (a DIAGNOSTIC, serial-only). Keep `setup()`'s call as `print_banner(mrcon)` (debug rule: boot output is serial-only); the `version` verb passes `out`.

- [ ] **Step 4: Build the boards** (same command as Task 2 Step 3). Expected: all `SUCCESS`.

- [ ] **Step 5: Hand off** — report green (user commits).

---

## Task 4: Migrate the provisioning + config `handle_*` (batch A)

Batch the 23 `handle_*` by area so each task is one reviewable unit. Batch A = config/provisioning.

**Files:**
- Modify: `src/fw_main.cpp` — `handle_cfg_set` (485), `handle_gateway` (909), `handle_join` (1034), `handle_create` (1066), `handle_route_cmd` (169).

**Interfaces:**
- Produces: `handle_cfg_set(const char* args, Print& out)`, `handle_gateway(const char* args, Print& out)`, `handle_join(const char* args, Print& out)`, `handle_create(const char* args, Print& out)`, `handle_route_cmd(const char* args, Print& out)`.

- [ ] **Step 1: Add `Print& out` to each signature; migrate response prints** (`mrcon.`→`out.`), leaving any `[dbg]`/trace line on `mrcon`. These handlers also mutate NV/config — that logic is **unchanged**; only their console *output* moves to `out`.

- [ ] **Step 2: Update the `service_debug` call sites** to `handle_cfg_set(args, mrcon)` etc. (serial parity until Task 5 threads `out`).

- [ ] **Step 3: Build the boards.** Expected: all `SUCCESS` (verify `gateway` too — `handle_join`/`handle_create` are `#if MR_N_LAYERS<2`; confirm the gateway path still compiles).

- [ ] **Step 4: Hand off** — report green (user commits).

---

## Task 5: Migrate the mobile/team + inbox `handle_*` (batch B)

**Files:**
- Modify: `src/fw_main.cpp` — `handle_team` (1126), `handle_mobile` (1190), `handle_leave` (1263), `handle_lookup` (816), `handle_hashof` (830), `handle_whoami` (842).

**Interfaces:**
- Produces: each gains `Print& out` (e.g. `handle_team(const char* args, Print& out)`, `handle_whoami(Print& out)`).

- [ ] **Step 1: Add `Print& out`; migrate response prints.** Note `handle_mobile` is under `#if MR_FEAT_MOBILE` + `#if MR_N_LAYERS<2` (feature-split) and `handle_team` under `#if MR_N_LAYERS<2` — keep those guards; only the `mrcon.`→`out.` inside changes.

- [ ] **Step 2: Update `service_debug` call sites** to pass `mrcon` (serial parity).

- [ ] **Step 3: Build the boards.** Expected: all `SUCCESS`.

- [ ] **Step 4: Hand off** — report green (user commits).

---

## Task 6: Migrate the ops/test `handle_*` (batch C)

**Files:**
- Modify: `src/fw_main.cpp` — `handle_factory_reset` (753), `handle_sleep` (793), `handle_debug` (806), `handle_prep_restart` (1477), `handle_rcmd` (1542), `handle_crashtest` (1449), `handle_testsched` (1560), `handle_teststatus` (1609).

**Interfaces:**
- Produces: each gains `Print& out`. `handle_peerkey` (1760) already returns a JSON string via `char* out,size_t cap` — leave its signature (it is a *buffer-writer*, not an `mrcon` handler; it is already transport-neutral).

- [ ] **Step 1: Add `Print& out`; migrate response prints**, leaving debug/trace on `mrcon`. `handle_debug` toggles the debug flag — its *confirmation* line is a response (`out`); it does not itself emit debug.

- [ ] **Step 2: Update `service_debug` call sites** to pass `mrcon`.

- [ ] **Step 3: Build the boards.** Expected: all `SUCCESS`.

- [ ] **Step 4: Hand off** — report green (user commits).

---

## Task 7: Extract `dispatch(line, len, Print& out)` from `service_debug`

Now every response producer takes `out`. Thread a single `out` param through the verb table.

**Files:**
- Modify: `src/fw_main.cpp` — `service_debug` (1622) → rename/refactor to `dispatch`; `service_console` (2118) call site.

**Interfaces:**
- Consumes: every `handle_*(…, Print&)` / `dump_*(Print&)` from Tasks 2–6.
- Produces: `bool dispatch(const char* line, size_t len, Print& out)` — the single line→handler verb map; returns `true` if a verb matched.

- [ ] **Step 1: Rename `service_debug(line,len)` → `dispatch(line,len,Print& out)`** and replace every `mrcon`/hardcoded-serial call inside with `out`: `dump_status()` → `dump_status(out)`, `handle_cfg_set(a)` → `handle_cfg_set(a, out)`, the inline `mrcon.println(F("> …"))` error/usage lines → `out.println(F("> …"))`. (Those inline usage strings are RESPONSES.) Leave any inline `[dbg]` line on `mrcon`.

- [ ] **Step 2: Point `service_console` at `dispatch(line, pos, mrcon)`** — serial passes the global `mrcon`, so USB output is **byte-identical** to before.

- [ ] **Step 3: Build the boards.** Expected: all `SUCCESS`.

- [ ] **Step 4: Serial parity smoke-check** — flash a board, run the battery (Task 9's `console_battery.txt`) over USB, confirm output matches the pre-refactor capture. Expected: byte-identical.

- [ ] **Step 5: Hand off** — report green (user commits).

---

## Task 8: Rewire BLE + remote to `dispatch` (delete the two reimplementations)

**Files:**
- Modify: `src/fw_main.cpp` — `ble_dispatch_line` (1775), `remote_exec` (1487); `#include "dispatch_sink.h"` near the top includes.

**Interfaces:**
- Consumes: `dispatch(line,len,Print&)`; `BufferSink`, `LineSink` (Task 1); `mrble::tx_line` (BLE ship fn).

- [ ] **Step 1: BLE — KEEP the companion-JSON verbs, ADD a `dispatch` fallback (per the §2a convergence decision — the iOS contract is safe).** Do NOT replace `ble_dispatch_line`'s body wholesale. The companion parses specific BLE JSON — `whoami`/`ready`, `version`, `duty` (`write_duty`), `limits` (`write_limits`), `routes`/`pull_inbox`/`mark_read` (the shared JSON handlers), `prep-restart`, `rcmd` — those special-cases stay **byte-identical** (they are the contract). The only additive change: append a **fallback** so verbs the companion does NOT special-case become answerable over BLE as canonical text:

```cpp
static void ble_ship(const char* s, size_t n) { mrble::tx_line(s, n); }
// … the existing companion special-cases (whoami/version/duty/limits/routes/pull_inbox/mark_read/prep-restart/rcmd) stay EXACTLY as-is,
//    except the shared JSON handlers now take Print& (pass a LineSink — behaviour-identical to today's ble_sink per-line tx_line):
//      handle_routes(ls);  handle_pull_inbox(line+10, ls);  handle_mark_read(line+9, ls);   // ls = LineSink(&ble_ship)
// … then, at the END (after all companion special-cases), the additive fallback for everything else:
    LineSink ls(&ble_ship);
    if (dispatch(line, len, ls)) { ls.flush(); return 0; }   // canonical text for non-companion verbs (team/mobile/status/cfg/…) — NEW capability, no contract break
    return 0;
}
```

The shared JSON handlers (`handle_routes`/`pull_inbox`/`mark_read`) are JSON on every transport, so a `LineSink` ships the same per-line NDJSON the companion gets today — byte-identical. The companion never receives canonical text for a verb it already knows (its special-case runs first).

- [ ] **Step 2: remote (`remote_exec`) — DEFERRED to the remote-auth spec (decision 2026-07-13). Left UNCHANGED in this refactor.** Reason discovered during implementation: `remote_exec`'s compact single-line renderers (`up=Ns rx=… tx=…`) are deliberately sized to fit the **241-byte remote DM body** (`inbox_max_body`). Naively routing them through `dispatch` (whose canonical output is the verbose multi-line `dump_status`, and a gateway `cfg` reaches ~680 B) would **truncate/overflow** the remote response — a functional break, not just a format change. Converging remote therefore needs the DM-fit design that lives in `2026-07-13-remote-management-auth-design.md` (per the user: "don't worry about remote — we're doing that separately"). The `dispatch(line,len,out)` seam this plan lands is exactly the enabler §6 wants; remote reuses it there. **No edit to `remote_exec` here.**

- [ ] **Step 3: Build the boards + parity.** Expected: all `SUCCESS`; run the battery over BLE + `rcmd` and diff vs the baseline capture (Task 9).

- [ ] **Step 4: Hand off** — report green (user commits).

---

## Task 9: Output-parity gate (the regression backbone)

**Files:**
- Create: `test/console_battery.txt` — the fixed command list exercising every verb.

- [ ] **Step 1: BEFORE any code task, capture the baseline.** On the committed baseline, flash `xiao_sx1262`, feed `console_battery.txt` over USB, save the output as `test/console_battery.serial.ref`. Repeat over BLE (`.ble.ref`) and `rcmd` (`.rcmd.ref`). The battery must include: `help`, `version`, `status`, `cfg`, `routes`, `duty`, `limits`, `faults`, `whoami`, `team …`, `mobile …`, `gateway …`, `pull_inbox …`, `mark_read …`, an unknown verb, and a malformed verb.

```
# test/console_battery.txt
help
version
status
cfg
routes
duty
limits
faults
whoami
uptime
gateway status
pull_inbox 0 0
bogusverb
```

- [ ] **Step 2: AFTER Task 8, re-capture + diff (per the §2a convergence decision).**

Run: `diff test/console_battery.serial.ref <(new USB capture)`
Expected: **empty diff on USB** (the canonical anchor — byte-identical). For **BLE**, the companion-JSON verbs (`duty`/`limits`/`routes`/`pull_inbox`/`mark_read`/`ready`/`version`) must be byte-identical (`diff` their `.ble.ref`); non-companion verbs newly answering over BLE are additive (no prior ref). **Remote (`rcmd`) is exempt** — its output converges to canonical (reworked in the remote-auth spec), so no rcmd diff gate here.

- [ ] **Step 3: s18 + s22 tripwires** (device-side refactor must not touch `lib/core`, so both must be byte-identical):

```bash
cmake --build ~/lora-universal-simulator/build --target lus -j4 >/dev/null
LUS=~/lora-universal-simulator/build/orchestrator/lus
$LUS -e meshroute simulation/s18_meshroute.json /tmp/s18.ndjson >/dev/null 2>&1 && md5sum /tmp/s18.ndjson
$LUS -e meshroute simulation/s22_mobile_team_meshroute.json /tmp/s22.ndjson >/dev/null 2>&1 && md5sum /tmp/s22.ndjson
```
Expected: s18 `3ac88d40e00d2605ff66659f696d52bf` **and** s22 `d5f368a1d275cce5b1e1a0bb60b8753f` (both unchanged — `lib/core` untouched by a `fw_main`/console refactor).

- [ ] **Step 4: All envs build.**

Run: `for e in xiao_sx1262 heltec_v3 gateway gateway_heltec production native; do pio run -e $e 2>&1 | grep -E "SUCCESS|FAILED|error:" | tail -1; done`
Expected: all `SUCCESS`.

- [ ] **Step 5: Hand off** — report the full gate green (user commits).

---

## Task 10 (optional follow-on, NOT required for this refactor): native dispatch tests

Spec §3.2's native testability needs a Print shim on `native` + extracting `dispatch`+handlers into a native-compilable TU (they currently depend on `g_node`/`g_hal`/`mrble`/Arduino `Print`). That is a separate sub-project. **Do not block this plan on it.** If pursued later: move `dispatch` + the handlers to `lib/console/` behind a small `INode` seam, add a native `Print` shim, and add `test/test_console_dispatch.cpp` (feed a line, assert the `BufferSink` capture). Scope + design that as its own spec.

---

## Self-Review

- **Spec coverage:** §1 three dispatchers → Tasks 7 (dispatch), 8 (BLE/remote). §2 `Print&` unification + 376→~300 response scope → Tasks 2–6. §2 debug-vs-response rule → Global Constraints + every batch Step 1. §3.1 `MR_CONSOLE=0` → sinks are independent of `NullPrint` (BLE/remote pass `BufferSink`/`LineSink`, Task 8). §3.2 native testability → Task 10 (scoped out, with the honest reason). §4 migration order → Tasks 1–8 follow it (sinks → JsonSink → dump_* → text batches → dispatch → transports). §5 gate → Task 9 (parity battery + s18 + all-envs). §6 remote-auth → Task 8 keeps the seal/allow-list; auth is the separate spec.
- **Placeholder scan:** none — the one deferred item (native tests, format normalization) is explicitly out of scope with a reason, not a gap.
- **Type consistency:** `Print& out` is the single param type across Tasks 2–7; `dispatch(const char*, size_t, Print&) -> bool` matches its callers in Tasks 7–8; `BufferSink`/`LineSink` signatures in Task 1 match their use in Task 8. `handle_peerkey` deliberately keeps its `char* out,size_t cap` buffer-writer form (already transport-neutral) — noted in Task 6.
- **Ambiguity resolved:** streaming (`routes`/`pull_inbox`) uses `LineSink` (per-line flush), bounded verbs use `BufferSink` — both are `Print`, so `dispatch` is sink-agnostic (Task 1 + Task 8).
