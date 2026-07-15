# firmware_commands — seam design spec

*2026-07-15. For QA review BEFORE any code moves. The hardest cluster in the fw_main split — deferred to a design pass precisely because `dispatch` is a hub entangled with board-glue.*

**Goal:** extract the console command-router (`dispatch`) + the diagnostic/console handlers out of `src/fw_main.cpp` into `src/firmware_commands.{h,cpp}` (`namespace mrfw`), behavior-preserving, gated on native + all 10 boards + s18 byte-identical — leaving `fw_main.cpp` as board/runtime glue (setup/loop/mesh_service/canary/service_console + the transports).

**Grounding:** derived from a 6-agent coupling map (5 group readers + a completeness critic) over the current `fw_main.cpp`. This spec is the reconciled, self-consistent result.

---

## 0. The one thing to get right (the riskiest coupling)

**The `MRFAULT_HW` / `MRFAULT_ESP32` macro trap — not the `mark_expected_reset` symbol trap.**

`device_fault.h` is the single-TU ISR-vector header (non-inline `fault_capture` + naked `HardFault_Handler`/`MemManage`/`BusFault`/`UsageFault` + `.noinit g_scratch`) — the same trap `firmware_remote` avoided via the `fw_wdt_feed()` wrapper. But the sharper danger is that **`MRFAULT_ESP32` (device_fault.h:20-21) and `MRFAULT_HW` (device_fault.h:26) are `#define`d *inside that header*.** So a TU that (per the established pattern) does **not** include `device_fault.h` sees these macros **undefined on every board**.

Two functions have `#if defined(MRFAULT_HW)` / `#if defined(MRFAULT_ESP32)` bodies:
- **`dump_faults`** — whole body under `#if defined(MRFAULT_HW)`.
- **`handle_crashtest`** — the `abort()` arm under `#if defined(MRFAULT_ESP32)`, the fault arm under `NRF52_PLATFORM`.

If either is moved to the `device_fault.h`-free `firmware_commands` TU, it **compiles its `#else` "unsupported" branch on every board** — a **green-compiling, test-passing, runtime-only regression** (no linker error catches it). Therefore **both STAY in fw_main**, reached from the moved `dispatch` via thin wrappers. This is non-negotiable and is why the standard "the formatters are link-safe, so it can move" reasoning must be overridden here.

---

## 1. Move / Stay / Wrapper sets (confirmed)

### MOVE → `firmware_commands.cpp` (`mrfw::`)
- **Pure verbs/dumps:** `dump_routes`, `dump_status`, `dump_cfg`, `dump_duty`, `dump_limits`, `handle_routes`, `handle_route_cmd`, `handle_lookup`, `handle_nameof`, `handle_hashof`, `handle_whoami`, `handle_sleep`, `handle_debug`, `handle_testsched`, `handle_teststatus`, `dump_help`, `hl`.
- **Status/cfg feeders:** `make_status_fields`, `make_cfg_extras`, `node_state_str`, `read_batt_mv`.
- **Shared console helpers (must be NON-static + header-exported — see §3):** `board_name`, `print_banner`, `print_identity`, `print_sf_list`.
- **Identity:** `do_regen` (verified device_fault-clean — moves with `print_identity`).
- **Peerkey pair:** `handle_peerkey`, `persist_pinned_peer`.
- **The hub:** `dispatch`.

### STAY → `fw_main.cpp` (board-glue / transport / macro-trap)
- `do_reboot`, `do_ota` — call `mrfault::mark_expected_reset` (device_fault.h symbol) + raw reset vectors; also invoked from the loop deferred-recovery path.
- **`dump_faults`** — `MRFAULT_HW` macro trap (§0).
- **`handle_crashtest`** — `MRFAULT_ESP32` macro trap + calls `do_reboot` (§0).
- `handle_prep_restart` — writes the loop's `g_halted` latch and is called directly by the loop deferred-recovery path (`mesh_service_once`); inline-safe but loop-coupled → stay.
- `service_console`, `ble_dispatch_line`, `ble_sink` — the transport seam (`ble_dispatch_line`'s address is taken in `setup()`).

### WRAPPER → thin fw_main shims (the `fw_wdt_feed` pattern; defined in fw_main, declared in `fw_context.h`)
The moved `dispatch` (and moved `handle_factory_reset`) call these staying functions, so:
1. **`fw_reboot()`** → `do_reboot()` — for `dispatch` (`reboot` verb) **and** moved `handle_factory_reset` (its terminal reset).
2. **`fw_ota()`** → `do_ota()` — for `dispatch` (`ota` verb).
3. **`fw_faults_dump(Print&)`** → `dump_faults(out)` — for `dispatch` (`faults` verb); wraps the `MRFAULT_HW` macro trap.
4. **`fw_crashtest(const char*, Print&)`** → `handle_crashtest(args, out)` — for `dispatch` (`crashtest` verb); wraps the `MRFAULT_ESP32` macro trap.
5. **`fw_prep_restart(Print&)`** → `handle_prep_restart(out)` — for `dispatch` (`prep-restart` verb). (Inline-safe, so a plain header decl would also work; the wrapper is recommended for symmetry with 1-4.)

> **`handle_factory_reset` MOVES** (its NV/inbox-wipe logic is clean) but reaches the reset via `fw_reboot()`.
> **No `fw_regen`** — `do_regen` moves, `dispatch` calls it in-TU.

---

## 2. Shared state — no new promotions

Every global a mover touches is **already `extern` in `fw_context.h`**: `g_sched` (moved `handle_testsched` writes; loop-tick fire reads), `g_force_sleep` (moved `handle_sleep` writes; `board_sleep_until`/`service_console` read), `g_halted` (`handle_prep_restart` stays), `s_inbox_jb` (**written by both** moved `dump_limits`/`handle_routes` AND staying `ble_dispatch_line` status/cfg/ready paths — MUST remain the single `extern char s_inbox_jb[1700]`; the mover must not copy it). Verified: nothing needs newly promoting.

---

## 3. Required `firmware_commands.h` interface

These MOVE but STAYING fw_main callers reach them, so they must be non-static + declared (else link error):

| Symbol | Staying callers |
|---|---|
| `bool dispatch(const char*, size_t, Print&)` | `service_console`, `ble_dispatch_line` |
| `size_t handle_peerkey(char*, size_t, const meshroute::Command&)` | `service_console`, `ble_dispatch_line` |
| `void print_banner(Print&)` | `setup()` |
| `void print_identity(const mrnv::IdBlob&)` | `setup()` |
| `void print_sf_list(uint16_t)` | `setup()`, `mesh_service_once()` |
| `const char* board_name()` | `ble_dispatch_line` |
| `void handle_routes(Print&)` | `ble_dispatch_line` |
| `meshroute::console::StatusFields make_status_fields()` | `ble_dispatch_line` |
| `const char* node_state_str()` | `ble_dispatch_line` |
| `meshroute::console::CfgExtras make_cfg_extras()` | `ble_dispatch_line` |

fw_main reaches them via `using mrfw::<sym>` (call sites unchanged). All other moved functions are `static` inside `firmware_commands.cpp`.

`fw_context.h` gains the 5 wrapper decls (`fw_reboot`/`fw_ota`/`fw_faults_dump`/`fw_crashtest`/`fw_prep_restart`).

---

## 4. `firmware_commands.cpp` includes + build wiring

**Includes:** `firmware_commands.h`, `fw_context.h`, `firmware_config.h` + `firmware_remote.h` + `firmware_inbox.h` (dispatch re-fans-out to those `mrfw::` handlers via `using`), `sched_send.h`, `frame_trace.h` (`g_mr_trace_on`), `console_json.h` + `console_binary.h`, `fault_log.h` (formatters — inline-safe), `device_nv.h`, `device_rng.h`, `<Arduino.h>`, `<cstdlib>`/`<cstring>`/`<cstdio>`.
**MUST NOT include** `device_fault.h`.

**Build:** register `+<firmware_commands.cpp>` in the three `build_src_filter`s (platformio.ini:96/177/218). **Critical:** it must compile with the **identical `-D` set** as fw_main (`MR_N_LAYERS`, `MR_FEAT_MOBILE`, `MR_FEAT_REMOTE_MGMT`, `MR_FEAT_TEAM`, `MR_CONSOLE`) — the board envs already apply the same flags per-TU, but any `#if`-gated dump/verb body (and the macro-trap functions that STAY) must see the same flags or the verb map / dump bodies diverge.

---

## 5. Staging — DECISION: one increment (not C1/C2)

The firmware_config A/B split balanced ~250+250 lines. Here it would **not** balance: C1 (all 26 non-dispatch movers) is ~90% of the work and C2 (dispatch alone) is trivial — and keeping `dispatch` in fw_main for C1 forces a **transitional export of ALL ~26 dispatch-reached handlers** (each made non-static + header-declared just so fw_main's dispatch can reach them), then a **collapse** of the ~16 dispatch-only ones back to `static` in C2. That export-then-collapse churn buys little when C1 is already the whole cluster.

**Chosen: ONE increment (QA-endorsed alt).** Move `dispatch` + all handlers + `do_regen` + the peerkey pair + the shared helpers together into `firmware_commands.{h,cpp}`, and add the 5 wrappers. Only the **§3 set (~10 symbols)** is non-static + header-exported (the staying `setup`/`ble_dispatch_line`/`service_console`/`mesh_service` callers); the ~16 dispatch-only handlers are `static` in `firmware_commands.cpp` (dispatch calls them in-TU). Reaches the clean end state directly, no churn. Bigger single diff — executed in careful per-function verbatim batches (feeders → dumps → verbs → helpers → dispatch), then wired + gated once.

**Coupling budget = the 5 wrappers.** The fw_main↔firmware_commands call graph is intentionally bidirectional (transports/`setup` call in via §3; the 5 wrappers call out). If execution discovers it needs a *6th* shim, that's a signal to re-examine a Stay/Move call, not to pile on wrappers.

---

## 6. Gate + a bench-verify owed

Every increment: native (unaffected — fw not compiled there), all 10 boards, s18 byte-identical (lib/core untouched). **Plus a bench-verify item unique to this cluster:** because the macro-trap regression is *runtime-only*, after C1 confirm on real hardware that **`faults` still dumps the fault ring** (nRF52) and **`crashtest fault`** still faults (nRF52) / **`crashtest`** behaves (ESP32) — i.e. the `fw_faults_dump`/`fw_crashtest` wrappers still route into the device_fault.h-including TU. A green build does NOT prove this.

---

## 7. `handle_prep_restart` — RESOLVED: STAY (QA, 2026-07-15)
It mutates the loop's own `g_halted` latch and the loop's deferred-recovery path calls it directly — that's loop-control, not a console handler. Keep it co-located with the loop; the moved `dispatch` reaches it via `fw_prep_restart(Print&)`. Consistent with `do_reboot`/`do_ota` staying; moving it would split loop-control across TUs for no gain.
