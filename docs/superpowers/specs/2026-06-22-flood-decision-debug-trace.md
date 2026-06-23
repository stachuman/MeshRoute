<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Channel-flood decision trace — metal-visible debug instrumentation

**Status:** coder instruction. **DEBUG-ONLY — no behavior change.** Land GREEN + uncommitted; I quality-gate; the user commits and bench-verifies. This is instrumentation to *confirm a diagnosis*, not the fix.

## Why

On a 4-node metal bench a **channel (M-broadcast) flood from 254 does not reach leaf node 184** during the flood; 184 is delivered only later by the repair-pull. Diagnosis (verified in code): the flood coverage bitmap is seeded from `hops==1` neighbours = *"nodes I hear"* (one-way), but a broadcast's real coverage is *"nodes that hear me"* (the reverse link). 254 and 222 both have `dest=184 hops=1` (they hear 184) so they **seed 184 as covered**, even though 184 hears **only 166** (its routes go via 166; on the bench 222's 4 direct RTSes to 184 all went unanswered, then rerouted via 166). So 166 — the one node 184 can receive from — sees 184 already covered and stays silent (`flood_forward_decision`, `node_channel.cpp:565`).

**The blocker to confirming it on metal:** every flood event is `MR_EMIT`, which is compiled out under `MESHROUTE_NO_TELEMETRY` (the xiao/heltec/esp32 builds — `hal.h:60-63`), and the **silent-suppress path emits nothing even in sim**. So there is no way to see, on hardware, *why* a node went quiet or what its coverage view was. This adds the missing visibility via `_hal.log` (the metal-visible path already used for flood errors at `node_mac.cpp:438/448`), gated by the existing `debug on` switch.

One open loose end this resolves directly: a stale `dest=22 hops=1` in 166's dump *should*, per `flood_any_unmarked`, have forced a relay — yet 166 stayed silent. The per-neighbour dump (point 1) will show 166's exact `hops==1` set and each bit's covered state at flood time, settling it cold.

## 1. HAL gate — `trace_on()` (so `lib/core` can honour `debug on` without pulling in `Serial`)

`g_mr_trace_on` lives in `frame_trace.h` (Arduino/`Serial`-backed, firmware-only), so portable `lib/core/*.cpp` can't read it directly. Add a tiny HAL accessor:

- **`hal.h`** (next to `virtual void log(const char* msg) = 0;` at `:117`): add
  ```cpp
  virtual bool trace_on() const { return false; }   // debug-trace gate; overridden on metal -> g_mr_trace_on
  ```
  A **defaulted** virtual — existing HALs (the fake HAL in tests, the sim HAL) inherit `false` unchanged, so native/sim output and tests are untouched.
- **The device HAL** (the `Serial`-backed one in the firmware that implements `log()`): override
  ```cpp
  bool trace_on() const override { return frame_trace::g_mr_trace_on; }
  ```
  (use whatever namespace/qualifier `frame_trace.h` declares `g_mr_trace_on` in.) This reuses the `debug on` / `debug off` console toggle — zero output when off.

## 2. Helper — `flood_log_coverage` (in `node_channel.cpp`)

Add a private `Node` method (decl in `node.h` near the other channel/flood helpers; def in `node_channel.cpp`). `seen_test` is the static helper already at `node_channel.cpp:34`, in-TU:
```cpp
void Node::flood_log_coverage(const char* tag, uint32_t id, const uint8_t* bm) const {
    char buf[128];
    int n = snprintf(buf, sizeof buf, "flood %08lX %s nbrs:", (unsigned long)id, tag);
    for (uint8_t i = 0; i < _active->_rt_count && n > 0 && n < (int)sizeof buf - 8; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1)
            n += snprintf(buf + n, sizeof buf - (size_t)n, " %u=%c",
                          _active->_rt[i].dest, seen_test(bm, _active->_rt[i].dest) ? 'Y' : 'N');
    _hal.log(buf);
}
```
Bounded buffer + the `n < sizeof-8` guard make it truncation-safe (a 4-node net is ~3 neighbours; the guard covers a full table).

## 3. Instrumentation points (all `if (_hal.trace_on()) …`, all `node_channel.cpp`)

| # | site | line | add (gated on `_hal.trace_on()`) | reveals |
|---|---|---|---|---|
| **A** | **silent-suppress** in `flood_forward_decision` | **565**, before `flood_state_free(slot); return;` | `flood_log_coverage("SILENT", fs.id, fs.bitmap);` | **THE one** — 166's exact `hops==1` set + each bit's covered state ⇒ why it went silent + confirms `184=Y` |
| **B** | **originate seed** in the FLOOD-originate path | **~272**, right after `flood_set_my_coverage(bm);` | `flood_log_coverage("SEED", id, bm);` | proves 254 stamps `184` covered despite the one-way link |
| **C** | **relay scheduled** | **579** (beside the `flood_rebroadcast_scheduled` MR_EMIT) | `char b[72]; snprintf(b,sizeof b,"flood %08lX relay in %lums slot=%u",(unsigned long)fs.id,(unsigned long)backoff,slot); _hal.log(b);` | a node that *does* decide to relay + its backoff |
| **D** | `handle_flood_rts` outcomes | 548 / 551 / 553 | one-liner each: `"flood %08lX dup-cancel"` / `"… already-buffered"` / `"… state-full"` | why a received FLOOD RTS-M created no relay |
| **E** | `flood_rebroadcast_fire` | 589 / 591 | `"flood %08lX RELAY hop=%u"` (before `enqueue_flood_m`) / `"… hop-exhausted"` (the TTL drop) | the actual re-flood firing |

A and B are the decisive pair; C–E complete the metal-visible flood trace so the whole decision path is legible under `debug on`.

## 4. Constraints

- **Do NOT remove or alter any existing `MR_EMIT`** — they stay for the sim ndjson. These `_hal.log` lines are *additive*.
- **No behavior change** — only logging, all behind `_hal.trace_on()` (false unless `debug on`).
- **Portable** — `node_channel.cpp` must NOT `#include "frame_trace.h"` (it's `Serial`-bound). Gate solely via the `_hal.trace_on()` accessor.
- Keep buffers stack-local and bounded (metal RAM); no heap, no `String`.

## 5. Gate

- `pio test -e native` green (the defaulted `trace_on()` keeps the fake HAL at `false` ⇒ no new output, no test churn).
- `pio run -e gateway -e xiao_sx1262 -e heltec_v3` build SUCCESS (the device-HAL override + the log calls compile).
- **Behaviour check:** `debug off` (default) ⇒ none of the new lines appear; `debug on` ⇒ they do. (Native: a unit can drive a fake HAL whose `trace_on()` returns true and assert `log` receives a `"flood … SILENT nbrs:"` string when a fully-covered flood is forwarded — optional but nice.)
- Leave GREEN + uncommitted.

## 6. What the next bench run confirms

Flash → `debug on` → resend `send_channel 0 "…"` from 254 → collect **254's `SEED` line** and **166's `SILENT` line**. Expected if the diagnosis holds:
```
254:  flood FECC2901 SEED   nbrs: 184=Y 222=Y          (254 claims 184 covered)
166:  flood FECC2901 SILENT nbrs: 22=?  184=Y 222=Y    (166 sees 184 covered -> stays silent)
```
`184=Y` on 166 confirms the asymmetric-coverage root cause; the `22=?` value settles the dead-node loose end (and tells us whether `flood_any_unmarked` is even seeing 22). If the lines disagree with the prediction, they point precisely wherever the real cause is — which is the whole reason to instrument before fixing.
