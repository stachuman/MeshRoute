# Metal RX / Async-TX / Light-Sleep — Design Spec

**Status:** **DRAFT for sign-off 2026-06-07.** Reworks the device radio path from *polled RX + blocking
TX + busy-spin* to *interrupt-driven RX + async TX + software-LBT + light-sleep*, **based on the proven
MeshCore driver** (`/home/staszek/MeshCore`, vendored chip headers already in `lib/meshcore/`). Delivered
as **4 independently metal-testable steps**. Code-grounded against `lib/hal/device_radio.h`,
`src/fw_main.cpp`, and MeshCore's `Dispatcher.cpp` / `RadioLibWrappers.cpp`.
**Author:** Stanislaw Kozicki <cgpsmapper@gmail.com> · **Date:** 2026-06-07

---

## 0. Scope

The **device (metal) radio + main-loop** only. The MeshRoute protocol/MAC logic above the HAL is
**unchanged** — this is a HAL/loop rework, native-proven against `MockRadio` throughout.

**In:** (1) event-driven RX via the DIO1 IRQ; (2) non-blocking async TX; (3) software noise-floor LBT
(replacing the disabled HW-CAD); (4) light-sleep between events (radio stays in continuous RX).

**Out / deferred:**
- **Endpoint deep-sleep duty-cycle** (a leaf that powers down its receiver and is deaf while asleep →
  needs parent store-and-forward + a wake schedule). That is a **protocol** change → its own spec.
  This spec keeps the MeshCore stance: **every node listens continuously; sleep only halts the CPU.**
- Deep `SYSTEMOFF`/`esp_deep_sleep` (shutdown / battery-protection) — not the inter-packet idle path.
- BLE/USB-CDC power management; the protocol layer; the sim engine.

---

## 1. Current metal state (verified)

| Concern | Today | Where | Limitation |
|---|---|---|---|
| RX detect | **polled** `getIrqFlags()` every loop | `device_radio.h:71-90` | needs CPU awake to poll → **blocks sleep** |
| RX IRQ | reverted | `device_radio.h:7-10` | "delivered no RX events [isr flag never surfaced a packet]" |
| TX | **blocking** `_radio.transmit()` | `device_radio.h:44` | freezes timers/RX for the whole airtime (SF12 ≈ seconds) |
| LBT | HW-CAD `scanChannel()`, **disabled** | `device_radio.h:64-66` | can block unbounded on CAD-done IRQ |
| Idle | **busy-spin** | `fw_main.cpp:398-439` | 100% CPU duty, no sleep |

The polled path is the *working bring-up baseline* — it is the regression oracle for Step 1.

---

## 2. Reference model — MeshCore (proven on SX1262 / XIAO-nRF52840 / Heltec-V3)

1. **One DIO1 IRQ, one flag, shared RX+TX done.** `setPacketReceivedAction(setFlag)` registers the ISR;
   the ISR only does `state |= STATE_INT_READY` (`RadioLibWrappers.cpp:18-28`). The base state
   (`STATE_RX` vs `STATE_TX_WAIT`) disambiguates: `recvRaw()` consumes the flag as RX-done
   (`.cpp:109-137`), `isSendComplete()` consumes it as TX-done (`.cpp:156-163`). ISR is
   `ICACHE_RAM_ATTR` on ESP32.
2. **Async TX.** `startTransmit()` returns immediately → loop polls `isSendComplete()` → `finishTransmit()`
   → re-arm RX (`.cpp:143-169`). Never blocks.
3. **Re-arm discipline.** After every read, `recvRaw()` calls `startReceive()` again (`.cpp:128-135`).
   `readData()` clears the RX IRQ.
4. **Software LBT.** `isReceiving()` = `isReceivingPacket()` (preamble/header-valid, our
   `CustomSX1262::isReceiving()`, `CustomSX1262.h:89-93`) **OR** `isChannelActive()` =
   `RSSI > noise_floor + threshold`; `noise_floor` is a rolling 64-sample average maintained in
   `loop()` (`.cpp:76-94, 171-175`). Non-blocking — no CAD spin.
5. **Light-sleep, gated on no pending work.** The repeater loop runs `the_mesh.loop()` then, iff
   `powersaving_enabled && !hasPendingWork()`, calls `board.sleep()` (`simple_repeater/main.cpp:150-167`).
   - **nRF52:** `__WFE()` / `sd_app_evt_wait()` — radio stays in RX, wakes on *any* IRQ (DIO1 / RTC-tick /
     USB); the `secs` arg is ignored (`NRF52Board.cpp:254-278`).
   - **ESP32-S3:** `esp_light_sleep_start()` + `ext1_wakeup(DIO1)` + optional `timer_wakeup(secs)`; DIO1
     must be an RTC-capable GPIO (`ESP32Board.h:59-78`).

---

## 3. Target architecture

### 3.1 Final loop (MeshRoute-adapted)

```
loop():
  if rx_flag (DIO1 fired in RX state):
     while drain_rx(buf): node.on_recv(buf, meta)      # readData clears IRQ + re-arm RX
  if tx_pending && tx_done_flag (DIO1 fired in TX state):
     finish_tx(); restore_rx_sf(); tx_pending = false  # back to listening on the routing SF
  for id in due_timers: node.on_timer(id)              # beacons / RTS-ACK / retries / SF-hop windows
  drain node.next_push -> console ; service_console() ; persist_join_if_changed()
  if !tx_pending && !radio.is_receiving():             # nothing in flight, no frame mid-air
     deadline = timer_wheel.earliest_due()             # NEW (see 3.6)
     board.sleep_until(deadline)                        # WFE (nRF52) / esp_light_sleep (ESP32)
```

The radio is in **continuous RX** the whole time; the CPU only halts between events and wakes on DIO1
(frame arrived / TX finished) or the next-due timer. Dual-SF needs nothing special — SF-hop windows are
timers, so `earliest_due()` bounds the sleep to the window deadline.

### 3.2 `IRadio` seam (`lib/hal/iradio.h`) + native impact

- Split `transmit()` → **`start_transmit(...)`** (arm + return) + **`poll_tx_done()`** (bool).
- Add **`is_receiving()`** (preamble/header-valid) for LBT.
- Add a noise-floor LBT primitive (keep `channel_busy()` name, reimplement) — see §3.5.
- `MockRadio` + `test_device_hal` / `test_timer_wheel` follow the seam — **no logic leaves native
  coverage**. The real `Sx1262Radio` stays a device-only TU (`#if ARDUINO`).

### 3.3 DIO1 ISR + flag (the recipe — copy MeshCore)

`volatile uint8_t g_radio_state` with `STATE_RX / STATE_TX_WAIT / STATE_INT_READY(bit)`. ISR
(`ICACHE_RAM_ATTR` on ESP32) sets `STATE_INT_READY`. Arm via `setPacketReceivedAction(isr)` **then**
`startReceive()` (RadioLib sets the RxDone DIO1 mask). `readData()` clears the IRQ; re-arm `startReceive()`
after each read. **Step 1 caveat:** while TX is still blocking, clear a stale TX-done flag after
`transmit()` returns, before re-arming RX (else a spurious empty RX-drain).

### 3.4 Async TX ↔ Node MAC (verify during Step 2)

**Design intent:** the Node's MAC is fire-and-forget (it sends, then arms timers for CTS/ACK waits —
matching the sim, where radio TX is instantaneous and airtime/duty is modeled in the Hal ledger). If so,
async TX is **transparent** to the Node: `DeviceHal::tx` calls `start_transmit` and returns `TxResult::ok`
on a successful *arm*; the loop re-arms RX on TX-done. **To confirm by reading `node_mac.cpp` tx path
in Step 2.** Two consequences either way:
- **Half-duplex serialization:** no new `start_transmit` until the prior TX completes. Reuse the existing
  radio-busy hold (`r4.5b on-radio-busy`) — report the radio busy until TX-done so the Node's existing
  LBT/duty defer serializes sends. No new MAC state.
- **Airtime ledger unchanged:** debited from the **computed** chip-accurate `airtime_ms` at send time (as
  now), preserving sim parity — not from measured completion.

### 3.5 Software LBT (noise-floor)

Replace HW-CAD with: `channel_busy()` = `is_receiving()` **OR** `RSSI > noise_floor + threshold`.
`noise_floor` = rolling N-sample RSSI average sampled in the loop while in RX and not receiving. Bounded,
non-blocking, sleep-compatible. Re-enables LBT (currently forced off, `device_radio.h:64`).

### 3.6 Sleep seam + timer support

- **`TimerWheel::earliest_due() -> uint64_t`** (`timer_wheel.h`): O(64) scan of `_due[]`, min active
  deadline or `UINT64_MAX` if none. (Today only `due_at(id)` exists, `timer_wheel.h:34`.)
- **Board sleep seam** (device-only, `#if ARDUINO`): `sleep_until(uint64_t deadline_ms)` → nRF52 `WFE`
  (deadline ignored, wakes on tick/IRQ); ESP32 `esp_light_sleep_start()` with `timer_wakeup(deadline-now)`
  + `ext1_wakeup(DIO1)`. Gate: only sleep when `!tx_pending && !is_receiving()`.

---

## 4. Stepped plan — each step flashes + verifies on 2 devices

> Console hooks used below already exist in `fw_main.cpp`: `[rx]`/`[tx]`/`[pre]`/`[rxdone]` traces,
> `send <id> <text>`, `status` (`rx=`/`tx=`/`pending=`), `routes`, `RECV from=`, `ACKED ctr=`.
> Suggested addition for diagnostics: an `isr=<count>` field in `status` (ISR fire counter).

### Step 1 — Event-driven RX (restore DIO1). TX stays blocking.
- **Files:** `device_radio.h` (ISR + flag + flag-drain RX + re-arm), `fw_main.cpp` (loop RX section; `isr=`
  in `status`). Keep the polled path behind `#define MR_RX_POLL` as a one-step fallback.
- **Build/native:** both board envs compile; `pio test -e native` 235/235 (MockRadio unaffected).
- **METAL TEST:** flash A + B. Both auto-DAD join. Each prints `[rx] ... cmd=…` for the other's beacons;
  `status` shows `rx=` climbing **and `isr=` climbing**. `send <B> hi` on A → B prints `RECV from=<A>: hi`,
  A prints `ACKED ctr=…`.
- **PASS:** RX arrives via the IRQ (the historically-broken path), DM round-trips, no regression vs the
  polled baseline.
- **Diagnostic (the bench's first job):** `isr=0` ⇒ ISR never fires ⇒ **pin/mask** (see §5). `isr>0` but
  `rx=0` ⇒ ISR fires but frame not read/re-armed ⇒ fix the drain/re-arm. This split localizes the old bug.

### Step 2 — Async TX.
- **Files:** `iradio.h` (`start_transmit`/`poll_tx_done`), `device_radio.h` (async + shared DIO1 flag,
  remove the Step-1 flag-clear hack), `device_hal.*` (tx flow + in-flight/busy hold), `fw_main.cpp` (TX-done
  in loop), `MockRadio`/native tests. Confirm the §3.4 fire-and-forget assumption against `node_mac.cpp`.
- **Build/native:** native green (updated for the seam); both board builds green.
- **METAL TEST:** DM still round-trips (`send`→`RECV`→`ACKED`). **Crucially:** during a long SF12 TX,
  timers keep firing — observe `[tx] … t=…` then beacon/`[rx]` timestamps that *don't* freeze for the TX
  duration (the blocking-freeze is gone). Multi-hop forward still works on a 3-node line.
- **PASS:** TX completes via TX-done IRQ; loop responsive during TX; no DM/forward regression.

### Step 3 — Software-LBT (noise-floor), re-enable LBT.
- **Files:** `device_radio.h` (`is_receiving` + noise-floor `channel_busy`), boot default `lbt_enabled`.
- **Build/native:** native green; both board builds green.
- **METAL TEST:** `cfg set lbt 1` + reboot on both. Force contention (two nodes send near-simultaneously);
  observe LBT-defer telemetry/console and that sends still complete (no unbounded block, no hang). `status`
  duty/tx sane.
- **PASS:** LBT defers on a busy channel, never blocks, DM throughput intact.

### Step 4 — Light-sleep.
- **Files:** `timer_wheel.*` (`earliest_due`), board sleep seam (device-only), `fw_main.cpp` (gate + sleep),
  a `cfg set powersave 0|1` knob (default on).
- **Build/native:** `earliest_due` unit-tested on native; both board builds green.
- **METAL TEST:** with powersave on, node A idles. From B, `send <A> ping` → A still prints `RECV` (DIO1
  woke it). Beacons from A still fire on schedule (timer wake). On Heltec, measure the current drop in idle
  (light-sleep); on XIAO confirm idle behavior + no missed frames. `cfg set powersave 0` returns to
  busy-spin for A/B comparison.
- **PASS:** sleeps between events, **wakes on RX (no missed frames)**, beacons on time, lower idle current
  (ESP32 measurable).

---

## 5. Why DIO1 failed before — diagnosis checklist (Step 1)

Prime suspects, ordered, vs the MeshCore recipe:
1. **DIO1 pin mismatch** — `g_mod(NSS, DIO1, RST, BUSY)` (`fw_main.cpp:44`). Verify `LORA_PIN_DIO1`
   matches the board variant's actual DIO1 (MeshCore uses the variant `P_LORA_DIO_1`). *Most likely.*
2. **Not armed** — `setPacketReceivedAction` alone does **not** enter RX; `startReceive()` must follow
   (it also sets the RxDone DIO1 mask).
3. **Not re-armed** — must `startReceive()` again after each `readData()` (else ≤1 packet).
4. **IRQ not cleared** — `readData()` clears it; without it the line stays asserted.
5. **ESP32 ISR in flash** — must be `ICACHE_RAM_ATTR`.

The `isr=` counter splits #1/#2 (counter stuck at 0) from #3/#4 (counter climbs, no delivery).

---

## 6. Decisions & non-goals

- **Power model = always-on RX + light-sleep for every node** (MeshCore stance). No node deep-sleeps its
  receiver in this spec → **no missed frames**, modest savings (radio RX ~5–6 mA dominates; CPU halt saves
  ~3–5 mA, larger on ESP32 light-sleep). Endpoint deep-sleep duty-cycle = future protocol spec.
- **Async TX adopted** (Step 2) — independently fixes the blocking-TX-freezes-timers bug.
- **Software-LBT replaces HW-CAD** (Step 3) — non-blocking, sleep-safe, re-enables LBT.
- **No protocol/wire changes.** No new MAC state if §3.4 holds (reuse the radio-busy hold).

## 7. Risks

- **DIO1 bring-up** (Step 1) is the historical unknown — mitigated by the `isr=` diagnostic + the polled
  fallback `#define`.
- **Async-TX ↔ MAC assumption** (§3.4) — verify against `node_mac.cpp` before changing `DeviceHal::tx`;
  if the MAC expects synchronous completion, model it via the busy hold (still no new state).
- **nRF52 WFE shallowness** — wakes every tick; saves CPU current but not radio RX. Set expectations: the
  big idle win is on ESP32 light-sleep; nRF52 is a CPU-duty win, not a radio-duty win.
- **ESP32 DIO1 must be RTC-GPIO** for `ext1` wake — verify on Heltec V3 before Step 4.
