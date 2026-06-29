<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Radio-Module canary — catch the WRITE that corrupts the radio SPI Module (find the jump-to-0x0 root cause)

**Status:** coder instruction — a DEBUG instrument (flag-gated, never in production). The user commits + flashes + benches; I gate. nRF52840 (the real SX1262/RadioLib Module); ESP32 stub. No wire change.

## Why

The fault-log v3 LR localized the recurring **jump-to-0x0** crash (fleet-wide, both nodes, multiple builds): `lr=0x36700` = **`Module::SPItransferStream`**, and the bus-fault sibling (`pc=0x362a4` in `loop()`'s radio poll, deref `0xd2f1aa20`). Both = the **RadioLib Module's pointers corrupted at runtime** (clobbered to 0x0 → null-call → branch-to-0x0; or to garbage → bus fault). It runs 18s–6m51s, then the next radio SPI op dies. This crash is the unifying root cause of the e2e-ack failures (crash mid-TX), the USB wedges (the crash), the crash-loops, AND the stochastic channel orphaning (a rebooting node can't relay the flood). The sim never reproduces it — its idealized radio has no real Module to corrupt.

`device_radio.h` is clean (poll_rx length-bounded; all ops are scalar RadioLib calls), so the corruptor is **a wild write from elsewhere** landing on the Module object, or inside RadioLib's SPI/DMA. The LR named the **crash site**; this canary names the **corruption site** — the same trick, one layer earlier.

## The canary

Snapshot the Module's critical bytes at init, re-check at fine-grained checkpoints through `loop()`; the FIRST checkpoint that sees a change names the subsystem that just ran = the corruptor.

**In `device_radio.h`** (it owns `_radio` → the Module):
- `Module* mod = _radio.getMod();` (RadioLib `PhysicalLayer::getMod()`). The Module's first bytes hold its HAL pointer / SPI callbacks + pin/config — exactly what `SPItransferStream` dereferences.
- `radio_canary_arm()` — `memcpy(g_canary, (const uint8_t*)mod, kCanaryN)` (kCanaryN ≈ 64, or `sizeof(*mod)` if the header exposes it). Also snapshot the **HAL object it points to** if reachable (`mod->hal` / the callback block) — the corruption may hit `*hal`, not `mod` itself; snapshot both regions. Call it at the END of `begin()` (radio fully initialized).
- `int radio_canary_check()` — `memcmp` current vs `g_canary` (both regions); return the first differing byte offset, or −1 if intact. PURE read, no SPI.

**In `fw_main.cpp` `loop()`** — gate the whole thing on `#if MR_RADIO_CANARY` (default OFF). After EACH subsystem, call a thin `canary("<where>")`:
```
canary("loop_top");
... poll_rx ...            canary("after_poll_rx");
... poll_tx_done ...       canary("after_tx_done");
... g_node tick / service_tx ...  canary("after_node_tick");
... service_console ...    canary("after_console");
... mrble::service_rx ...  canary("after_ble");
... inbox flush / nv_flush ...    canary("after_nv");
... sched_send tick ...    canary("after_sched");
... sample_noise ...       canary("after_noise");
```
`canary(where)` = `if (radio_canary_check() >= 0) { … }`. On the FIRST trip:
- **Record to the fault-log** (the durable, USB-independent path — this node's USB dies): add a `FaultCause::kCauseCanary` (or stash in the `.noinit` scratch) carrying the `where` string-id + the byte offset + the before/after dword. So `faults` (or `rcmd faults`) shows `CANARY @after_node_tick off=8 0x20001a40→0x00000000`.
- **Print** (`if (Serial) Serial.print(…)`) the same, for a live session.
- Then `mrfault::mark_expected_reset()` + `NVIC_SystemReset()` — recover cleanly with the canary record captured (don't ride the corruption into the opaque SPItransferStream crash).

That converts "somewhere device-side" into the exact subsystem. A second pass adds finer checkpoints *inside* the named subsystem to reach the call site.

## Reproduction (drive it, don't wait)

The crash is reproducible: a weak-link (SF9) **`e2e_ack_req` DM** crashed node 106 on cue. So: `send <weak-neighbour> "x" -a` between two nodes on a marginal link, repeatedly, with `MR_RADIO_CANARY` on — the canary trips at the corruptor within a few sends instead of waiting minutes. (Also worth a pass with channel floods, the other heavy radio path.)

## What I'll read in parallel (suspects for the wild write)
The crash correlates with the **e2e DM** path — receive → decrypt (dm_crypto/monocypher) → inbox deliver (QSPI store) → e2e-ack enqueue → TX. A length/bounds slip in any device-side buffer there could overrun into the Module. The canary checkpoints will point at which; I'll focus the read there once it names a subsystem.

## Tests / gate

- **Native:** the canary arm/check logic is pure (snapshot a buffer, memcmp, return the offset) — unit-test it (no change → −1; a single-byte change → that offset). Device-only otherwise.
- **Build:** all 4 boards with `MR_RADIO_CANARY=1` and `=0` (default). The `kCauseCanary` enum add is the only lib/core touch — verify s18 byte-identical (it's a device-only fault cause; the formatter gains one string).
- **Metal (the payoff):** flash with `-DMR_RADIO_CANARY=1`, run the weak-link e2e-DM repro, and `faults` shows `CANARY @<subsystem>` — the corruptor's subsystem. Then narrow + fix the root; the canary comes out (or stays behind the flag) for the production build.

## Sequencing
TOP priority — this is the root cause of the wedges, the e2e-ack failures, AND the channel orphaning. Everything else in both threads waits on it. Independent of the prior specs; it's the next thing to flash.

---

## ADDENDUM 2026-06-25 — RESULT + finer canary (per-timer-id)

**The canary tripped (node 106, boot 19, ran 27s): `CANARY @w3 off=64 0x7ccb8→0x0`.**
- `off=64` = the HAL region byte 0 → the **HAL object's first pointer**, zeroed (`0x7ccb8`→`0x0`) = the jump-to-0x0 (SPItransferStream calls the now-null callback).
- `@w3` = `CW_node_tick` (enum: `loop_top0 poll_rx1 tx_done2 node_tick3 …`). In `loop()`, `CW_poll_rx` (after `on_recv`) **passed**, `CW_node_tick` **tripped** — and the only code between them is the timer drain `for(id…) g_node.on_timer(id)`. So **a Node timer handler corrupts the radio HAL.**
- The wheel itself is exonerated: `TimerWheel::after()` bounds `timer_id ≥ kCap(80)`. So it's the *handler's* code (a buffer overrun / wild write), not the wheel store. RAM adjacency `g_hal(0x2001c028) … g_iradio(0x2001cfd8) g_radio(0x2001d000)` is consistent with a write near the radio object.

**Finer canary — name the exact timer.** In `fw_main.cpp` the timer-drain loop becomes (still `#if MR_RADIO_CANARY`):
```
for (int id; (id = g_hal.pop_due_timer()) >= 0; ) {
    g_node.on_timer((uint32_t)id);
    canary_timer((uint32_t)id);     // check after EACH timer; on trip, record id as the where
}
```
`canary_timer(id)` = the same trip path as `canary()`, but it records the **timer id (1..79)** instead of the coarse `CW_node_tick`. Encode it distinctly (e.g. `where = 100 + id`; the formatter prints `timer id=N` for `where≥100`). One metal run then shows `CANARY timer=57 …` → the exact handler:
- 57 = `kOverhearRetuneTimerId` (channel overhear `set_rx_sf`)
- 48..55 = `kChannelPullTimerId` ring · 61..63 = `kFloodRebcastTimerId` ring (channel flood)
- 4/5/10 = RTS/ACK/retry · 1/3/27 = beacon

Suspect order (radio-touching, channel-heavy, matches the e2e/flood correlation): overhear-retune → flood-rebcast → channel-pull → RTS/ACK retry → beacon. I read these handlers in parallel so the fix is ready when the id lands.

---

## ADDENDUM 2 2026-06-25 — RESULT: timer 9 (`do_post_ack`) → narrowed to the device QSPI inbox store; DWT watchpoint to name the line

**Finer canary tripped: `CANARY timer id=9 off=65 0x...→0x0`** = `kPostAckTimerId` = **`do_post_ack`** (the DM deliver + e2e-ack generation). The HAL is `g_mod.hal` (an `ArduinoHal`); its **vtable pointer** is zeroed → the jump-to-0x0 (and the garbage-value variant is the `@0xd2f1aa20` bus-fault sibling).

**Narrowed by elimination:** `do_post_ack`'s body copies are bounded *and* in `g_node` (84 KB from the radio). Its callees `parse_unicast_inner` / `enqueue_data` / `enqueue_push` / `Inbox::record` are shared lib/core — **the sim runs them and never crashes**. So the wild write is in the one **device-only** branch: `record_dm` → **`DeviceInboxStore` (QSPI segmented append-log, `src/device_inbox_store.h`)**. `FixedInboxStore::append` is bounded (ruled out); the live metal backend is the QSPI one (`MRINBOX_QSPI_READY`). Static adjacency was inconclusive (the heap `ArduinoHal` isn't next to `kSegScratch`/the LFS buffers), so pin it at runtime.

**DWT data-write watchpoint (names the exact instruction).** At `radio_canary_arm()` (we hold `g_canary_hal_p` = the HAL address), `#if MR_RADIO_CANARY`:
```
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk | CoreDebug_DEMCR_MON_EN_Msk;  // DWT + DebugMonitor (self-hosted, C_DEBUGEN must be 0 = no debugger)
DWT->COMP[0].COMP     = (uint32_t)g_canary_hal_p;   // watch the HAL's vtable-ptr word
DWT->COMP[0].MASK     = 2;                           // 4-byte range
DWT->COMP[0].FUNCTION = 0x6;                          // 0b0110 = data-write watchpoint -> debug event
```
Add a `DebugMon_Handler` that captures the stacked PC/LR (same frame walk as the HardFault handler) into the `.noinit` scratch with a new `kCauseWatchpoint`, then `NVIC_SystemReset()`. Next boot, `faults` shows `WATCHPOINT pc=0x… lr=0x…` → `addr2line` the **pc** = the exact store instruction that zeroes the HAL = the line in the `DeviceInboxStore` append/QSPI path.

Caveats: verify the S140 SoftDevice permits the DebugMonitor exception (set its priority below the SD's reserved high prios; the DWT comparators are app-usable). If the SD blocks it, fall back to a **sub-step canary hook** — a `_hal.dbg_radio_canary(uint8_t subid)` no-op-by-default Hal method (sim/native = no-op, so s18 unaffected), called after each `do_post_ack` callee, so the trip records *which* callee (record_dm vs enqueue vs send_e2e_ack). Both land on the same answer; the DWT just gives the line directly.

NEXT: implement the DWT watchpoint (or the sub-step hook), re-flash 106, same DM → `faults` → the pc → the exact write. Then the fix is a one-line bound/pointer correction in the device inbox store.

---

## ADDENDUM 3 2026-06-25 — ROOT CAUSE: the inbox LittleFS heap file-cache corrupts the radio's heap `ArduinoHal`

Reading `record_dm`'s device path to the metal: `DeviceInboxStore::append → qspi_seg_append` does **Adafruit_LittleFS `File` ops** — `File f(QSPIFlash); f.open(p, FILE_O_WRITE); f.seek(size); f.write(b,n); f.close();`. **LittleFS `File::open` `malloc`s a per-file cache on the heap; `close` frees it.** The radio's `g_mod.hal` is an `ArduinoHal` **`new`'d at static init = also on the heap.** The sim backend (`RamInboxStore`) uses no LittleFS and no heap — **that is the metal/sim split**: this exact code only runs on metal, which is why the sim (and every shared lib/core callee) never crashes.

**Mechanism (leading, to confirm via the DWT pc):** a **double-free** — the explicit `f.close()` PLUS the `File` destructor on scope-exit both freeing the cache — or a `cache_size`/`prog_size` overrun in `CustomLFS_QSPIFlash::_configure_lfs()`. Either corrupts the heap free-list/an adjacent block; the `ArduinoHal` vtable word is the casualty (→ 0 = jump-to-0x0; → garbage = the `@0xd2f1aa20` bus-fault sibling). Closes the whole chain: e2e-ack never returns (crash mid-`do_post_ack`), USB wedge, crash-loop, and the stochastic channel orphaning (a crashed node can't relay) — all one heap bug, all invisible to the sim.

**Confirm:** DWT watchpoint pc → `addr2line` should land in `lfs_free`/`lfs_cache`/`lfs_file_close` or the CustomLFS prog path.

**Fix (robust regardless of the exact heap bug):** make the inbox `File` ops use a **static file buffer** — `lfs_file_opencfg(... &cfg)` with a caller-owned `cache_size` buffer (or Adafruit's buffered-open variant) — so the hot inbox-append path **never allocates on the heap**. Removes both the corruption window adjacent to the radio object AND the per-record malloc/free churn. Pair with verifying `f.close()` isn't double-invoked by the destructor (guard or drop the explicit close). After the fix: re-flash 106, run the DM repro under the canary — **no `CANARY`/`HARDFAULT`, e2e-acks return, channel coverage recovers** (the orphaning was the crash). Then the canary + DWT come out of `[common]` (back behind the flag) for production.

---

## ADDENDUM 4 2026-06-25 — ★ TRUE ROOT CAUSE (DWT-confirmed): loop-task STACK OVERFLOW, not the inbox — ADDENDUM 3 IS WRONG

The DWT watchpoint fired (S140 permits DebugMonitor): `WATCHPOINT pc=0x576cc lr=0x57a9d` → `addr2line` = **`Node::route_strictly_better`** (pc), called from **`Node::sort_candidates`** (lr) — the routing-candidate sort, **NOT the inbox/LittleFS**. `route_strictly_better`'s 3rd param `cands` is **`const`** (verified in source) — it writes only stack locals + its prologue push. A store at `+4` hitting the watched HAL word ⇒ **SP is on the HAL = STACK OVERFLOW.**

**The mesh runs in the Adafruit core's loop task: `LOOP_STACK_SZ = 256*4 = 1024 words = 4 KB` (`main.cpp:42/88`), and `configCHECK_FOR_STACK_OVERFLOW = 0` (detection OFF → the overflow corrupts silently).** `do_post_ack` is the heaviest+deepest path — `dec_body[241]` + `body[242]` + the `PostAck` copy ≈ **700 B** of stack, then it descends into `sort_candidates`→`route_strictly_better`; the prologue push tips past 4 KB into the adjacent heap `ArduinoHal` → vtable word clobbered → jump-to-0x0 (garbage value = the `@0xd2f1aa20` bus-fault sibling). **The REAL metal/sim split = the FreeRTOS loop-task stack** (the sim has no task stack).

⚠ **ADDENDUM 3 (inbox static buffer) is the WRONG fix — the inbox was never involved; do NOT commit it.** The double-free refutation + the DWT both pointed away from it. Confirm-before-fix (the DWT-first call) earned its keep — it stopped a no-op masquerading as the fix.

**FIX (headroom + less peak + detection):**
1. Shrink `do_post_ack`'s frame: `dec_body`/`body` → `static` (the handler is non-reentrant — one timer fires at a time) or share one buffer. ~480 B off the peak; lib/core, no logic change (s18 unaffected by the breakdown).
2. `-DconfigCHECK_FOR_STACK_OVERFLOW=2` — `vApplicationStackOverflowHook` already exists in the core; turns the silent corruption into a clean, named "loop overflowed" fault (defense in depth).
3. Measure `uxTaskGetStackHighWaterMark(NULL)` from the loop after (1); if the margin is thin, move the mesh into `Scheduler.startLoop(mesh_loop, 8192)` for real headroom (`LOOP_STACK_SZ` is an unconditional `#define` in the vendored core → a separate bigger-stack task is the clean route; don't edit the framework).
4. Verify: re-flash with canary+watchpoint armed, run the DM → no `WATCHPOINT`/`CANARY` + a healthy high-water mark. Then canary+DWT+the inbox revert come out; the stack fix + `configCHECK_FOR_STACK_OVERFLOW` stay (full gate: native + 4 boards + s18 breakdown).

---

## ADDENDUM 4 — IMPLEMENTATION NOTE (2026-06-25, coder pass; gated-green, uncommitted)

Implemented the parts of ADDENDUM 4 that are unambiguous + verified, and corrected two framework assumptions the spec got wrong (verified against the vendored Adafruit nRF52 core, NOT asserted).

**Done:**
1. **Reverted ADDENDUM 3.** `src/device_inbox_store.h::qspi_seg_append` is back to the simple Adafruit `File` path (open/seek/write/close). The inbox was never the corruptor (the DWT named the stack); `File::close` is idempotent (no double-free). Comment documents the revert.
2. **Shrank `do_post_ack`'s frame (the measured overflow point).** `lib/core/node_mac_rx.cpp` — `dec_body[241]` (now :608) and `body[242]` (now :622) → `static` (handler is non-reentrant; `dec_body_len`/`blen` stay fresh locals). ~480 B off the peak exactly where `route_strictly_better` (reached via `send_e2e_ack`) tipped over the 4 KB loop stack. Behaviour-identical: native **521/521**, lus **s18 98/113** byte-for-byte the pre-change baseline. A 4-lens adversarial audit (metal-timer / sim-event / callee-recursion + synthesis) returned **GO** — single dispatch site (`node.cpp:702`), single arm site, no recursion, no threads, async TX, no aliasing (`e2e_open_inner` decrypts into its own stack-local then copies into `dec_body`).
3. **Added the stack high-water-mark instrument** (`fw_main.cpp` `loop_stack_free_bytes()` = `uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)`, nRF52-only): a `stackhw=` field in both `dump_status()` (USB) and the `rcmd <id> status` reply (USB-independent — read the margin over-the-air on a node whose USB is wedged). This is the verification/measurement tool that decides whether step (3)'s escalation is needed.

**Spec corrections (item 2 was wrong as written):**
- `configCHECK_FOR_STACK_OVERFLOW` is **already `1`** in the vendored `FreeRTOSConfig.h:78` (NOT `0`), and it's an **unconditional `#define`** → a `-DconfigCHECK_FOR_STACK_OVERFLOW=2` build flag just collides (redefinition warning) and the header's `1` wins → ineffective. Worse, the core's `vApplicationStackOverflowHook` (`rtos.cpp:84`) is a `while(CFG_DEBUG) yield()` **no-op** when `CFG_DEBUG=0` (our default) AND is **not `weak`**, so we can't override it without editing vendored files (forbidden). So "turn detection on via a -D flag" is a dead end. The honest, non-vendored substitute is the `stackhw=` high-water report above (item 3's instrument), which directly measures the margin. **Item 2 dropped; do not add the dead flag.**

**Deferred (bench-gated, per the spec's own item 3):** the dedicated bigger-stack task. No `Scheduler` library exists in this BSP, so it would be a raw `xTaskCreate(mesh_loop, …, 8192, …)` — a threading-model change (which task then services console/BLE/USB). Hold it until the **`stackhw=` reading after the frame-shrink** says whether ~480 B of recovered headroom is enough. ★ NEXT BENCH: flash, `rcmd <id> status` (or local `status`) → read `stackhw=` under the weak-link SF9 DM repro; if it stays comfortably > a few hundred bytes and no `WATCHPOINT`/`CANARY`/`HARDFAULT` fires, the frame-shrink sufficed and the canary+DWT+inbox-revert-instruments can come out. If thin, escalate to the dedicated task.

Gate: native 521/521 · s18 98/113 (identical) · all 4 boards + production build SUCCESS · 4-lens audit GO.

---

## ADDENDUM 5 (2026-06-29) — ⛔ ADDENDUM 4 (STACK OVERFLOW) REFUTED ON METAL → ADDENDUM 3 (inbox heap churn) RE-APPLIED + SOAK

**The crash recurred WITH the ADDENDUM-4 fix flashed.** A node reported `WATCHPOINT · ran 32m50s · pc=0x0 lr=0x36789` (lr ≈ `Module::SPItransferStream`, ~the prior `0x36700`; `pc=0x0` = the branch into the zeroed HAL vtable) on the **current build** — and **`status` reported `stackhw=1540` B free** on the loop task. So the 4 KB loop stack is **NOT** overflowing, yet it crashed. **The stack-overflow root cause (ADDENDUM 4) is wrong.** The DWT's earlier `route_strictly_better` capture was misleading, and the DWT now fires POST-crash (captures `pc=0x0`, not the corrupting write) — the corrupting write lands in a **SoftDevice-masked context** the DebugMon can't catch precisely.

Keep from ADDENDUM 4 (committed, harmless): the `do_post_ack` `dec_body`/`body` → `static` frame-shrink and the `stackhw=` instrument. They're not the fix, but they're cheap and `stackhw=` is what disproved the stack theory.

**Root cause is back to a genuine HEAP corruption of the heap-`new`'d `ArduinoHal`** (the original §Why premise). The canary (ADDENDUM 1) named `do_post_ack`/timer-9, whose ONLY device-only branch is `record_dm → DeviceInboxStore` — the QSPI **LittleFS `File`** append, which `malloc`/`free`s the lfs cache on EVERY record next to the radio HAL. That is **ADDENDUM 3's hypothesis**, which ADDENDUM 4 dismissed on (a) the double-free refutation and (b) the now-collapsed stack-DWT. ADDENDUM 3's static-buffer fix targets the cache CHURN regardless of double-free, so it's re-promoted.

**RE-APPLIED (uncommitted, compiles):** `src/device_inbox_store.h::qspi_seg_append` → a STATIC lfs cache (`s_cache[512]` + `s_file` + `lfs_file_opencfg` via `QSPIFlash._getFS()`, which is public @ Adafruit_LittleFS.h:78), NO heap alloc, wrapped in the SAME `_lockFS/_unlockFS` mutex the File path holds — so the ONLY delta vs the File path is the heap allocation (a clean test). A `prog_size > sizeof(s_cache)` guard falls back to the File path. **`pio run -e xiao_sx1262` SUCCESS** (the verification the original in-flight ADDENDUM-3 build never got).

**THE SOAK (user runs):** flash the crashing node with `pio run -e xiao_sx1262` (`=0` prod: a recurrence still HardFaults → fault-log `HARDFAULT` with the same `pc=0x0`/`lr≈SPItransferStream` signature; use `MR_RADIO_CANARY=1` if you want the WATCHPOINT). Baseline `faults` → drive SUSTAINED DM delivery **TO** the node (a RECEIVED DM = `record_dm` = the suspect append; faster = accelerated) for **≥ 4–8 h (≥ 8–15× the 33-min MTBF)** → `faults` / `rcmd <id> faults`:
- **No new HARDFAULT/WATCHPOINT → ADDENDUM 3 was the corruptor** (the inbox `File` heap churn). Drop the File-path fallback + the canary; ship.
- **Crash recurs → not it either →** next: a **sub-step canary** (a `_hal` checkpoint inside `do_post_ack` after each callee — `record_dm`/`enqueue`/`send_e2e_ack` — names the exact one; the polled canary catches the masked write the DWT can't), or a **guard-band around the heap `ArduinoHal`** to catch the overrun directly.

MTBF note: the inbox churns on every DM but the crash is ~33 min → the trigger is likely a heap-FRAGMENTATION threshold, not every append.
