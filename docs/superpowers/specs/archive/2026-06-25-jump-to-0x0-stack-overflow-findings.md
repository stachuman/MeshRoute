<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Jump-to-0x0 crash — root cause, fix, and findings (loop-task stack overflow)

**Status:** RESOLVED (fix verified fleet-wide, 2026-06-25). This is the consolidated record of the multi-session investigation into the recurring `jump-to-0x0` crash — its root cause, the fix, the dead ends, and the lessons. The live instrument spec (canary / DWT / per-timer-id, with all the intermediate results) is `2026-06-25-radio-module-canary.md`.

## TL;DR
The recurring fleet-wide `HARDFAULT pc=0x0 lr=0x36701 cfsr=0x20000 @0x0` was the **4 KB Arduino FreeRTOS loop task overflowing its stack** in the deep `do_post_ack` (DM deliver + e2e-ack) → routing path, writing past the task stack into the **adjacent heap `ArduinoHal`** and zeroing its vtable pointer → the next radio SPI call (`Module::SPItransferStream`) branched through it to address 0. **Fix:** moved `do_post_ack`'s ~480 B of payload buffers (`dec_body[241]`, `body[242]`) off the stack frame (made them `static`), restoring headroom at the exact overflow point. **Verified:** DM 17/17, e2e-ack 16/17 (was 0/17), no post-fix faults on any node, `stackhw` floor 352 B.

## The symptom (and why it resisted)
- `HARDFAULT pc=0x0 lr=0x36701 cfsr=0x20000 @0x0` — a UsageFault INVSTATE (branch to address 0), fleet-wide, across builds. A sibling `pc=0x362a4 cfsr=0x8200 @0xd2f1aa20` (precise bus fault) was the *same corruption* with a garbage value instead of 0.
- Ran 18 s – 6 h before dying — intermittent, traffic/timing dependent.
- It masqueraded as **three unrelated symptoms**: USB-CDC wedges, `e2e-acked 0/17` (every DM's application receipt missing), and stochastic channel orphaning (origins reaching 0/7).
- **It never reproduced in the simulator** — which sent the hunt down a wrong path (see False Trails).

## The investigation chain — each instrument handed to the next
1. **Watchdog + fault-log** — proved the crash was real and captured `pc/cfsr` *past the dead USB* (the wedge made the node un-pollable).
2. **Fault-log v3 (added the LR)** — `addr2line(lr)` → **`Module::SPItransferStream`** (the RadioLib SX1262 SPI HAL). Named the *crash site*: the radio's HAL pointer was being corrupted at runtime.
3. **Sim-vs-metal isolator** (`tools/lab/topo_to_sim.py`) — ran the *identical* real topology + workload in the sim. The sim delivered everything; metal orphaned. Verdict: **metal-specific, not flood logic.** (Right verdict — but the *reasoning* hid a flaw; see below.)
4. **Radio-Module canary** (`lib/hal/radio_canary.h`) — snapshot the HAL bytes at init, re-check after each loop subsystem. Tripped at `CW_node_tick` → a **Node timer handler** corrupts the HAL (the timer wheel itself exonerated — `after()` bounds the id).
5. **Per-timer-id canary** — recorded the firing timer id → **`timer id=9 = kPostAckTimerId = do_post_ack`** (DM deliver + e2e-ack generation).
6. **DWT data-write watchpoint** on the HAL vtable word → `WATCHPOINT pc=0x576cc lr=0x57a9d` → `addr2line` → **`route_strictly_better`** ← **`sort_candidates`**. The *exact instruction*.
7. **Diagnosis:** `route_strictly_better`'s `cands` param is `const` — it writes only stack locals. A store there hitting the watched HAL address ⇒ **SP is on the HAL = stack overflow.**

## Root cause
The Adafruit nRF52 core runs `setup()`/`loop()` in a FreeRTOS task: **`LOOP_STACK_SZ = 256*4 = 1024 words = 4 KB`** (`framework-arduinoadafruitnrf52/cores/nRF5/main.cpp:42/88`). `configCHECK_FOR_STACK_OVERFLOW` is `1`, but `vApplicationStackOverflowHook` is a `CFG_DEBUG`-gated no-op — so the overflow corrupted **silently**. `do_post_ack` is the heaviest + deepest path: `dec_body[241]` + `body[242]` + the `const PostAck pa` copy ≈ **700 B** of stack, then it descends into `sort_candidates` → `route_strictly_better`. The prologue push there tipped past 4 KB into the **adjacent heap `ArduinoHal`** (the radio HAL, `new`'d at static init), clobbering its vtable pointer → the next `SPItransferStream` branched through the now-null/garbage pointer (→ 0x0 = jump, or → `0xd2f1aa20` = bus fault). **The real metal/sim split is the FreeRTOS task stack — the sim has none.**

## False trails (the expensive lessons)
1. **"It's the inbox LittleFS heap file-cache."** A plausible chain — the QSPI inbox `File` ops `malloc`/`free` a per-file cache on the heap, where the `ArduinoHal` also lives — got written up as the root cause (ADDENDUM 3) and a `static`-buffer fix was even implemented. **The DWT watchpoint refuted it**: the pc was in routing, not the inbox. *Lesson: confirm the exact instruction before trusting a fix. The "DWT-first, don't rely on the inbox fix yet" call saved shipping a no-op.*
2. **The "double-free" sub-hypothesis** — refuted by reading the vendored code: Adafruit `File::_close()` guards on `isOpen()`, so a second close is a no-op.
3. **The elimination flaw (the big one):** "the sim runs `do_post_ack`/`sort_candidates` and never crashes, so the corruptor must be device-specific code." **This is FALSE for memory-corruption bugs.** A shared-code overflow only *manifests as a crash where the bad address lands* — on metal it hit the heap `ArduinoHal`; in the sim's different layout it hit harmless memory. "Sim works" never meant "shared code is clean." This is exactly what sent the hunt to the device-only inbox path.

## The fix
Made `do_post_ack`'s `dec_body` and `body` **`static`** (the handler is non-reentrant — one timer fires at a time in the single loop task; verified: only caller is `node.cpp` `case kPostAckTimerId`, and `bridge_cross_layer`/`l2c_handle_misdelivery` run before the buffers and return). Moves ~480 B off the frame at the overflow point. `dec_body_len` stays a fresh local; write-before-read holds. **s18 byte-identical** (`156915` events, md5 `776304cde868dba31f4bde7715d28fd6`) — a pure stack→bss move is logic-neutral. Native 521/521.

## Verification (fleet, oracle run 3d7377)
- **DM 17/17 delivered, e2e-acked 16/17** (was 0/17 — the crash was eating the e2e-ack mid-`do_post_ack`).
- **No post-fix faults** on any node (the `HARDFAULT`/`CANARY`/`WATCHPOINT` records are all pre-fix builds; recent boots are clean), with the canary + watchpoint still armed.
- **`stackhw` floor 352 B**, held across every path the oracle exercised: DM, crypted DM (`-e`), channel flood, repair-pull, forward. The crypted path did *not* go deeper (the routing peak, not the crypto, is the bottleneck; `dec_body` is now static).

## Remaining margin + the deferred robustness fix
352 B of 4 KB (8.6 %) is a **confirmed but thin** floor. The robust fix is a dedicated bigger-stack task — `xTaskCreate(mesh_task, "mesh", 8192/sizeof(StackType_t) /*=2048 words*/, …)` running the mesh loop, with the core's 4 KB `loop()` parked (`vTaskSuspend(nullptr)`). It covers every deep path at once. **Deferred to the production-hardening pass**: it fixes no current bug (the static move stabilized the current code, confirmed across all oracle paths), and `status stackhw=` (`uxTaskGetStackHighWaterMark`) is the live early-warning. Pull it forward if `stackhw` < ~150 B, a fault returns, or new deep code lands. A cheaper interim reclaim exists (the `const PostAck pa` copy, ~260 B) but isn't needed yet.

## Reusable instruments (keep behind `MR_RADIO_CANARY`, out of `[common]` for production)
- **Fault-log v3** — `pc/lr/cfsr` past a dead USB; `addr2line(lr)` names the caller.
- **Radio-Module canary** (`radio_canary.h` + `device_radio.h`) — snapshots the HAL, names the corrupting subsystem/timer.
- **DWT data-write watchpoint** — names the exact corrupting instruction (S140 *does* permit the DebugMonitor exception; `kCauseWatchpoint`).
- **`status stackhw=`** — the live stack-margin monitor. **Keep this one always-on** (it's cheap and it's the canary for future overflows).
- **`tools/lab/topo_to_sim.py`** — the 1:1 metal/sim isolator.

## What is NOT this bug — the next thread
The **channel orphaning is a separate problem.** It persists post-fix with **no faults** (`repair rescued 0.0%`, `eventual == flood`, origins 54/106/170/247 orphaned). The canary/watchpoint did not trip during the run, so it is not a stack overflow on the channel paths — it is the **repair-pull backstop being structurally dead** (a node that misses a flood is not pulling it). That is the next investigation, tracked separately.
