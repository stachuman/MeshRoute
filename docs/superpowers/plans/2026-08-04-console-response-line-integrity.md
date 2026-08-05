<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Console Response Line Integrity — B95 Coder Brief

Date: 2026-08-04  
Status: ready for implementation; uncommitted; owner commits  
Trigger: Heltec V3 OLED H5-06 bench test

## 1. Required outcome

Preserve the existing USB anti-wedge rule—console output must never block the radio loop—but stop emitting syntactically corrupted command responses.

Under TX pressure, a response line may remain best-effort. It must be omitted as a whole and reported later. A line must never be assembled from unrelated surviving fragments.

This is a console/transport fix, not an OLED, radio, routing, protocol, or command-format change. H5-06's radio checks passed.

## 2. Bench evidence

The H5-06 capture includes:

```text
proto : duty=1.00% beacon_ms=900000168010102layer=5 leaf=5000
1Laye0000[route] dest=5 next=5 hops=1 ...
[route]   gw_sched period=15000ms heard_ms=39214608526@]5@0125015-20[route] dest=5 ...
```

These are plausible pieces of adjacent `cfg` and `routes` fields with labels, spaces, punctuation, and newlines selectively missing—not random memory. Help also sometimes lacks a proper line ending.

## 3. Root cause

### 3.1 Fragment admission

`src/console_sink.h::GuardedConsole` gates every individual `Print::write()` call against `Serial.availableForWrite()`.

A formatter submits one row as many calls: label, value, separator, next label, and finally newline. When one call does not fit, it is dropped and reported as written. The USB task may free capacity before the next call, so later fragments survive. Single-byte punctuation and newline are especially vulnerable.

### 3.2 Help bypass

`firmware_commands.cpp::dump_help(Print& out)` ignores `out`. Its `hl()` writes directly to `Serial`, loops/yields for up to 40 ms per line, and emits CRLF only if two FIFO bytes happen to be free. It violates the transport-sink contract, can omit the terminator, and can stall the radio loop cumulatively. Delete this bypass; help must write through its supplied sink.

### 3.3 SF-list bypass

`dump_cfg(Print& out)` calls `print_sf_list(bitmap)`, but that helper always writes to global `mrcon`. Change it to `print_sf_list(Print& out, uint16_t bitmap)` and update all four current callers. Do not retain a global-writing overload.

## 4. Required invariants

1. No console output waits for FIFO space, calls a blocking flush, or loops with delay/yield.
2. A USB command-response line is admitted as one unit: complete line including terminator, or zero bytes from that line.
3. Dropped or overflowed complete lines are counted. When capacity returns, emit one complete deferred report:

   ```text
   !! CONSOLE_DROP lines=<N>
   ```

   If this report cannot fit, retain the count and retry on a later loop pass. Do not count the report recursively.
4. A trailing partial response cannot fuse with the next response. Terminate it at the command boundary or drop it whole and count it.
5. Every command-response formatter honors `Print& out`; no hidden `Serial`/global-`mrcon` response path remains.
6. Debug and asynchronous output remain serial-only; do not route them into BLE/remote capture sinks.
7. `MR_CONSOLE=0` remains a true compile-out, including staging storage and `Serial` references.
8. Existing BLE JSON and remote binary formats remain byte-identical.
9. Do not accidentally stream the multi-kilobyte help text over BLE. Preserve its effective scope or add one bounded `console_only` refusal before BLE fallback.

## 5. Recommended implementation shape

1. Add a line-staging `Print` adapter for the USB command path. It buffers one line and submits it to the guarded console in one call.
2. Route the complete `service_console()` response through it: `dispatch()`, Node command ACKs, peer-key/name JSON, parse errors, and `print_reqpubkey_hint()`.
3. Flush the adapter at each command boundary.
4. Replace direct-Serial `hl()` with normal writes through `Print& out`.
5. Parameterize `print_sf_list` with its sink and update its two `dump_cfg` callers plus setup and the gateway-listening trace.
6. Keep the maximum line scratch off the ESP32 loop-task stack. The existing maximum is 1700 B for escaped inbox JSON. Reuse a proven static/external scratch under the single-threaded call graph or measure and justify another bound.
7. Preserve the guarded console's whole-chunk nonblocking write; the new adapter changes the unit from arbitrary fragments to complete lines.

Do not solve this by increasing FIFO sizes, adding delays, waiting longer, or flushing. Those weaken the anti-wedge guarantee.

## 6. Non-goals

- Guaranteed delivery of every diagnostic line.
- A queued/flow-controlled USB protocol.
- Command text, field-order, CRLF, RF, routing, OLED, or wire changes.
- Refactoring every asynchronous push/debug renderer.
- Changing the unrelated uncommitted `platformio.ini` Wire/LDF work.

If asynchronous rows later reproduce the same fusion, take that as a separately evidenced extension.

## 7. Automated proof

No current native test covers `GuardedConsole`, and `src/` is outside the native build. Add a committed `tools/probe_console_sink/` harness following the board-UI probe precedent, or extract only pure line-admission logic into a native-testable header.

Required tests:

1. A fake FIFO with capacity changing between fragment calls reproduces the old fused row.
2. The fixed path produces the exact complete line or zero bytes from it.
3. A dropped newline cannot fuse two responses.
4. After one dropped line and restored capacity, the next line is exact and `!! CONSOLE_DROP lines=1` appears once.
5. A disconnected host never waits.
6. Staging overflow drops the whole line and increments the counter; no prefix leaks.
7. Help captured through a non-USB sink performs no direct Serial write and every captured line is terminated. Pin the explicit BLE refusal separately if used.
8. The cfg SF-list bytes land in the supplied sink with zero global-console leakage.

Negative controls must prove that bypassing staging, admitting newline separately, restoring direct-Serial `hl()`, and restoring global `print_sf_list()` each break their corresponding test.

## 8. Regression gate

- New probe and all negative controls pass.
- Native suite green; report cases/assertions/failures.
- s18 and mandatory scenario corpus reproduce the current baseline with zero assertion failures.
- Build all board environments sequentially with no new warnings and zero `-Wswitch` findings.
- Build `production`/`MR_CONSOLE=0`; prove Serial and staging compile out.
- `sizeof(Node)` unchanged.
- Record and attribute per-board RAM/flash movement, especially staging storage.
- Existing BLE structured-command battery remains byte-identical.

## 9. Metal acceptance

On Heltec V3 under normal radio traffic:

1. Run `cfg` 20 times.
2. Run `routes` 20 times with a gateway schedule present.
3. Run `help` five times.
4. Complete the reachable DM from H5-06.
5. Temporarily stop host reads to force pressure, then resume.

Pass:

- no fused fields, inserted numeric fragments, or unterminated received rows;
- every received cfg/routes row is structurally complete;
- omissions occur only as complete lines and produce the deferred drop report;
- help never leaves following output on the same physical line;
- console stays responsive; no reset or wedge;
- radio ACK/receive behavior matches H5-06;
- a stalled/disconnected host never delays mesh work.

## 10. Closure

- Mark B95 fixed in the bug register with probe/build/metal evidence.
- Update H5-06: radio criterion passed; link the old console note to B95 and record the post-fix rerun.
- Record gate measurements in `simulation/BASELINE.md`.
- Leave changes uncommitted for owner review.
