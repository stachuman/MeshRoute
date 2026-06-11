# BLE Companion Transport — Phase 2 Implementation Scope (nRF52/XIAO)

**Date:** 2026-06-11 · **Status:** SCOPE (awaiting decisions before coding) · researched via the ble-companion-scope workflow (4 surveys + synthesis).
**Gate-reviewed 2026-06-11** — claims verified against the tree; §0/§1 + Step 4 corrected: the diagnostics/`whoami` JSON gap, the single-ring push fan-out, and the RNG call-site. Decision folded: `whoami` is IN Phase 2; full `cfg`/`routes`/`status` JSON defers to App Phase 3.

# Phase 2 — nRF52 (XIAO) BLE Companion Transport: Implementation Scope

## 0. Reality check (what's already done — don't re-scope)

- **Phase 1 (OTA) is SHIPPED, not pending.** `do_ota()` lives at `src/fw_main.cpp:306-316`: it writes `NRF_POWER->GPREGRET = 0xA8` + `NVIC_SystemReset()` and `ota` is already in the help text (`fw_main.cpp:391`). The OTAFIX runbook is the only open Phase-1 item. **This scope is Phase 2 only.**
- **The messaging seam is already a third backend's worth of clean.** The *command-dispatch* Serial coupling is `service_console()` at `fw_main.cpp:523-548` (~25 lines: accumulate to `\n` into `static char line[160]`, `service_debug()` first, else `parse_command()` → `on_command()` → print result). `lib/console/*` has zero `Serial` references; `lib/core/*` is clean **except** `frame_trace.h` (a device-only `Serial` frame tracer, orthogonal to the console — it stays USB-only). NB: `service_debug()` itself IS Serial-coupled — the diagnostic verbs print plain text, not JSON (see §1).
- **Inbox stores are partly wired already** (`g_inbox_dm`/`g_inbox_ch` `DeviceInboxStore` at `fw_main.cpp:77-78`, `g_node.inbox().on_init(...)` at :511) but inert until the QSPI backend lands — that's **Phase 3**, explicitly out of this scope.

## 1. Architecture — plug into the seam (messaging is decoder-free; diagnostics + whoami need JSON wiring)

The BLE backend reuses the **messaging** half of `service_console()` verbatim on a different I/O sink. The iOS contract (`ios-companion/INBOX_SYNC_CONTRACT.md:9`) is *exactly* the existing wire: **app→node = line-ASCII** (the console verbs), **node→app = newline JSON**.

- **RX (app→node), messaging — no decoder:** BLE RXD bytes → accumulate to `\n` → `console::parse_command(line, len, cmd)` (`console_parse.cpp:55`) → `g_node.on_command(cmd)` → `console::write_ack(buf, cap, result)` (`console_json.cpp:65`) → BLE TXD notify. `send`/`send_ack`/`sendhash`/`send_channel` flow through as-is.
- **TX (node→app), pushes — ONE ring, fan out:** drain the ring (`fw_main.cpp:635 while(g_node.next_push(pu))`) → `console::write_push(buf, cap, pu)` (`console_json.cpp:88`, the 5 `ev` kinds `msg_recv`/`channel_recv`/`send_acked`/`send_failed`/`hash_resolved`) → BLE TXD notify. **`next_push` is a destructive pop on a single ring** (`node.h:213`/`:720`, drop-oldest) and the USB loop already drains it to empty each iteration — so do **NOT** add a second BLE drain "alongside" it (each push would reach exactly one sink; a BLE node still running the USB drain would silently miss messages). The *single* drain must fan each popped push out to every active sink (or gate the sink by connection state).
- **Diagnostics bypass JSON — they print straight to `Serial`.** `service_console()` calls `service_debug()` *first* (`fw_main.cpp:531`); `routes`/`status`/`cfg`/`cfg set`/`whoami`/`lookup`/`hashof`/`help`/`regen`/`reboot`/`ota`/`sleep`/`debug` are consumed there and emit plain text via `Serial.print(F(...))` (`dump_routes`/`dump_cfg`/`dump_status`/`handle_whoami`…) with NO JSON encoder. A bare `feed_line` twin therefore delivers `send` + pushes but **none of the diagnostic replies over BLE.**
- **`whoami` over BLE IS in Phase 2** — the companion's contact map is keyed on the peer's `key_hash32`, surfaced only by `whoami` ("the reason the app exists", ios-companion §4). The encoder already exists, just *unwired*: `write_ready`/`write_status` (`console_json.cpp:127`/`:138`) emit `id` + `key`(=key_hash32) + `leaf_id` + `gateway` + `routing_sf` as JSON. Phase-2 task: route a `whoami` BLE command to `write_ready` (extend with `gateway_only`/`mobile`/`name` if the app wants the full identity).
- **Full `cfg` get/set + `routes`/`status` JSON are OUT of Phase 2** — they need new encoders (18-key cfg surface + the route table), and the iOS plan already puts Config + status screens in *App* Phase 3 (after companion core). This phase carries messaging + pushes + `whoami` only.
- **Refactor ≈ 60-80 lines, not 30:** factor the *messaging* line→dispatch body of `service_console()` into `feed_line(const char* line, size_t len, char* out, size_t cap)` (Serial loop + BLE RXD both call it), wire `whoami`→`write_ready`, and restructure the push drain to fan out. `out` captures `write_ack`/`write_ready` but CANNOT capture `service_debug`'s fire-and-forget `Serial` output, so the remaining diagnostics stay USB-only this phase.
- **MTU fragmentation is the one real wire concern:** BLE notify payloads (≤20 B at default MTU, ~244 B negotiated) split long JSON pushes. The TXD side must chunk a `write_push` line across notifies; the RXD side already buffers to `\n` so inbound fragmentation is free. `write_push` can emit up to ~`max_payload_bytes_hard_cap`+JSON overhead, so chunking is mandatory, not optional.

## 2. Build order — [NATIVE-verifiable] vs [BENCH-only]

The `device_nv` discipline: pure logic in `lib/core` with native tests; silicon-touching code bench-verified by the user.

**Step 1 — NV v7 fields.** [NATIVE-verifiable]
- `src/device_nv.h`: add `ble_mode` (u8), `ble_period_min` (u8), `ble_pin` (u32) to `Blob` (after `leaf_id`, :38); bump `kVersion 6→7` (:41).
- **Subtlety the surveys got half-right:** the load gate is `version >= 2 && version <= kVersion` AND `n == sizeof(out)` (`device_nv.h:75`). The *size* check rejects every old blob when the struct grows, so v6 nodes fall back to defaults and **re-run `cfg set`** — confirm that's acceptable (it is per spec §4 "no migration — 3 test nodes"). Set `ble_mode=off` default so a re-provisioned node is bare-metal.
- **Not XIAO-only:** the `Blob` + `kVersion` are shared with the ESP32/Preferences backend (`device_nv.h:115`, same size-gate), so bumping to v7 re-provisions the Heltec too. Fine per §4, but note it's a both-board NV change even though the BLE backend is XIAO-only.
- Native: a round-trip test mirroring the existing NV test pattern.

**Step 2 — `cfg set` surface.** [NATIVE-verifiable for parsing / BENCH for persistence]
- `src/fw_main.cpp` `handle_cfg_set` (:217): add `ble_mode <off|on|periodic>`, `ble_period <min>`, `ble_pin <6-digit>`, all `live=false` (reboot-to-apply, like `node_id`/`duty` at :256/:271).
- `dump_cfg()` (:119): print the three fields.
- `tools/meshroute_client.py` `CFG_KEYS` (:435): add the three keys for CLI help.

**Step 3 — Companion policy state machine.** [NATIVE-verifiable] — the keystone of native testability.
- New `lib/core/companion_policy.h`: pure `CompanionPolicy{ set_mode(mode, period_min); on_tick(now_ms) -> {should_advertise, next_change_ms}; request_window() }`. No device I/O, no SoftDevice symbols.
- Invariants to test (`test/test_companion_policy.cpp`, FakeClock): `off`→never advertise; `on`→always; `periodic N`→advertise for a window then idle until +N min; `request_window()` opens immediately and holds for the window duration; window survives across ticks.

**Step 4 — `device_rng` SD-aware branch.** [DONE — native-compiled, SD path BENCH-VERIFICATION PENDING]
- `src/device_rng.h` (caveat at :11-12): when the SoftDevice is enabled, the direct `NRF_RNG->VALUE` reads are illegal. `mrrng::fill` now branches: SD up → `sd_rand_application_vector_get()` (poll bytes-available + accumulate); SD off → the byte-identical bare-metal path.
- **Gate on a FLAG, not the SVC probe (deviation from the original plan):** the branch is `if (mrrng::sd_enabled())`, an `inline bool&` flag — NOT `sd_softdevice_is_enabled()`. The probe is itself an SVC and would HardFault when the SD is *off* (the default path), so it cannot be the guard. The flag defaults `false` (bare-metal) and is set `true` by Step 5's BLE init.
- **Site correction (the surveys were wrong):** this is NOT `fw_main.cpp:506` — that line is `g_hal.seed_rng(...)`, a *software* PRNG seed (`device_hal.h:61` `_rng = seed`) that never touches `NRF_RNG`. Guarding it fixes nothing. The illegal reads live in `mrrng::fill`, called at **`fw_main.cpp:472` (first-boot identity)** AND **`:204` (`regen`)** — two sites, not one. The `regen` path is the dangerous one: it runs at **runtime**, exactly when a companion is connected and S140 is live. Guard inside `mrrng::fill` so both sites are covered. **Hard prerequisite before `ble_mode != off` ships.**
- **⚠ STEP-5 OBLIGATION (the guard is ARMED but UNSET):** the SD branch is dead code until Step 5 sets the flag. Step 5 MUST call `mrrng::sd_enabled() = true` **immediately after `Bluefruit.begin()`** (and before the main loop can reach `regen`). If Step 5 forgets, a `regen` under a live BLE link takes the raw `NRF_RNG` path → HardFault — the exact bug this step exists to prevent. See the Step-5 checklist.

**Step 5 — BLE transport backend.** [CODE COMPLETE 2026-06-11 — compile-verified; on-metal advertise/RX-TX is BENCH]
- **DONE** ‼ The keystone landed: `device_ble.h:begin()` sets `mrrng::sd_enabled() = true` on the line immediately after `Bluefruit.begin(1,0)`, before `bleuart.begin()` — exactly as the Step-4 obligation demanded.
- **DONE** `src/device_ble.h` (NEW) — a PURE transport: owns Bluefruit + `BLEUart` (NUS) + the `CompanionPolicy` window scheduler + the inbound line buffer; **zero `Node`/command knowledge** (fw_main hands it a `DispatchFn` + pre-formatted Push JSON). Guards mirror `device_rng.h` → real impl on XIAO, inert no-op stubs on ESP32 + native. Advert does `addService(bleuart)` (embeds the NUS **service UUID** — the iOS scan requirement). `connected()` tracked via connect/disconnect callbacks (single-core `volatile` byte — the correct idiom, documented in-file).
- **DONE** `fw_main.cpp` — `ble_dispatch_line()` reuses `parse_command`+`on_command` (grammar can't drift from USB); `whoami`→`write_ready`; unknown→`write_err` (**fail-loud**). The push drain is fanned ONCE to USB (plain, unchanged) + BLE (`write_push` JSON, buffer sized 1536 for the 6× `\uXXXX` worst case + loud `push_encode_overflow` on the unreachable overflow). BLE init gated on `g_ble_mode` with a loud `INIT FAILED` (no silent bare-metal fallback). `loop()`: `mrble::on_tick` (advertising reconciled against `Bluefruit.Advertising.isRunning()`, not a shadow flag) + `service_rx`; sleep-gate `&& !mrble::connected()`.
- **DONE** `console_json.cpp` — added the two `CmdCode` cases the BLE path surfaced (`err_unprovisioned`/`err_no_data_sf`; were lossy `err_unknown`) + a defensive `body_len` clamp in `write_push`.
- **Verified:** XIAO links full S140+Bluefruit at **34.2% flash / 54.8% RAM**; Heltec + native **288/288** green. Adversarially reviewed (37 candidates → real defects fixed, benign single-core/library-safe races documented).
- **STILL BENCH (Steps 7–9):** the on-metal advertise/connect/pair/RX-TX + the S140↔LoRa-timing soak (§3) need the user's XIAO with S140 flashed. (Step 6 security is now CODE COMPLETE — see below.)

**Step 6 — Security / pairing.** [CODE COMPLETE 2026-06-11 — compile-verified; the pairing handshake is BENCH]
- **DONE** in `device_ble.h:begin()` (static-PIN recipe, matching the BSP `pairing_pin.ino`): `Bluefruit.Security.setPIN(g_pin_str)` — a static 6-digit passkey that auto-sets `mitm=1` / legacy-SC / `IO=DisplayOnly` (so iOS shows passkey-ENTRY). PIN = `g_ble_pin` formatted `%06lu` into a **namespace-scope static** (setPIN stores it BY POINTER — must outlive pairing). **No `Security.begin()` needed** for legacy static PIN (it only inits LESC crypto, unused here).
- **DONE** the §A.3 GATT gate: `g_bleuart.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM)` **before** `bleuart.begin()` — inherited from `BLEService`, the floor for RXD/TXD, so the SD rejects unpaired writes and `service_rx()` only sees a bonded client's commands. **No BLEUart subclass needed** (the original survey was wrong — `bleuart.setPermission` is the public, canonical call).
- **DONE** `setSecuredCallback`/`setPairCompleteCallback` (+ connect/disconnect) print concise bench signal over USB (`[ble] connected/secured/pairing OK|FAILED`). Bonding auto-persists to InternalFS (`.bond=1` default). **Fail-loud:** if `setPIN` fails, `begin()` returns false → loud `INIT FAILED`, NO insecure BLE served (none > open). Keystone preserved (sd_enabled set right after `Bluefruit.begin`, before any failure return).
- **STILL BENCH:** the actual iOS pairing handshake (PIN entry, bond, TX-notify-subscribe readiness gate) validates on the user's XIAO+S140.

**Step 7 — Press-to-advertise.** [BENCH-only]
- A GPIO/gesture → `g_companion_policy.request_window()`. **Input pin is an open decision** (XIAO has no obvious user button; may default to a BLE-write trigger or skip on XIAO and rely on `periodic`).

**Step 8 — Test client.** [DONE 2026-06-11 — `--selftest` green; live BLE is BENCH]
- **DONE** `tools/meshroute_client_ble.py` (bleak): subcommands `scan`/`monitor`/`repl`/`send` (mirrors the USB `meshroute_client.py`). Scans by the **NUS service UUID** (falls back to the `MeshRoute` name prefix), connects, best-effort `pair()`, subscribes TXD (the readiness gate), writes line-ASCII to RXD, buffers notifies to `\n`, parses + pretty-prints the §4 NDJSON. **Doubles as the iOS transport reference** (the connect→pair→subscribe→write→parse flow is documented inline). `--selftest` (LineBuffer reassembly + `pretty()` over every schema variant, 11 checks, no device); lazy bleak import → clean install hint + exit 1 if missing.
- **DONE** dep convention: `tools/requirements-ble.txt` (`bleak>=0.21`) — the one tools/ script needing a 3rd-party lib (the rest are stdlib). Header documents the Linux/BlueZ static-PIN pre-bond recipe (`bluetoothctl` agent) + the macOS auto-pair behavior.
- **STILL BENCH:** the actual scan/connect/pair/RX-TX against a live node needs the user's XIAO+S140 (+ a BLE adapter or phone).

**Step 9 — LoRa-timing validation.** [BENCH-only] — see §3.

## 3. KEYSTONE RISK — S140 SoftDevice ↔ tuned LoRa timing (spec §8.1)

**Static peripheral analysis (no conflict found):**
- App uses **RTC1** for `millis()`/FreeRTOS ticks; S140 reserves **RTC0** + **TIMER0** → different instances, no conflict.
- The SX1262 is **external SPI** (NSS=D4, MOSI=D10, MISO=D9, SCK=D8, DIO1=D1); the S140-reserved *internal* RADIO is irrelevant. DIO1 is a plain GPIO falling-edge ISR (`device_radio.h` `mr_on_dio1`), not a reserved IRQn → no static claim.
- IRQ priorities: S140 owns 0/1/4; the app must stay in 2/3/5/6/7. The DIO1 GPIO ISR priority must be checked against this.

**The two REAL conflicts:**
1. **`device_rng` direct NRF_RNG access is illegal under an enabled S140** (`device_rng.h:11-12` caveat; reads at `:33-41`, inside `mrrng::fill`). Call sites are `fw_main.cpp:472` (first boot) and `:204` (`regen`, at runtime under live BLE) — NOT `:506` (a software PRNG seed). **Mitigated by Step 4** — a hard blocker, fixed in code before any `ble_mode != off` build.
2. **CPU preemption during BLE events.** An *idle* enabled S140 perturbs timing minimally; *active* advertising/connection runs SD event handlers (SWI, priority ≤4) that preempt the app mid-RTS/CTS/DATA/ACK. The spec's mitigation (§A.1) — **window the advertising, not the stack** — bounds preemption to the connect windows. This **cannot be verified without hardware.**

**Bench validation plan (Step 9):**
- **Idle test** (`ble_mode=periodic`, no client): measure RTS/CTS/DATA/ACK timing and timer-wheel `pop_due()` latency with S140 initialized-but-idle vs the bare-metal baseline; confirm jitter is within the ACK window slack (RTS/CTS ~10 ms, DATA/ACK ~200 ms — slack, but not unlimited).
- **Active-window test**: run a beacon + DM exchange *during* an advertising window with a phone connecting; measure peak latency + confirm DIO1 IRQ doesn't drop (watch `status isr=` counter).
- **RNG test**: confirm identity seed generation took the SD-aware path.
- **Soak**: 30+ min, periodic advertising + LBT + active TX/RX; the duty-cycle ledger must not log false airtime violations from SD stealing CPU mid-calc.

**Fallback (spec §8.1 last line):** if idle-or-windowed S140 measurably hurts timing on relays/infrastructure nodes, **set `ble_mode=off` on those nodes** — they revert to today's exact bare-metal path. Only carried/mobile nodes pay the timing cost, and only inside their windows. The policy is per-node config, so this fallback is free.

## 4. RAM / flash budget verdict — FITS, with a notional RAM squeeze

- **Flash:** current app ~226 KB / 792 KB region (28.5%, from `firmware.zip` ≈ DFU package). Bluefruit adds ~80 KB; **S140 itself is in the bootloader region and never counted against the app**. Post-BLE ≈ 306 KB (~39%), ~486 KB spare. **Comfortable.**
- **RAM:** current ~126 KB / 230 KB app region (53.6%). The ldscript (`boards/nrf52840_s140_v7.ld`) **already reserves the SD's RAM region whether or not BLE is on** — so `ble_mode=off` wastes it; `ble_mode=on` *uses* already-reserved space. Net app-visible RAM does **not shrink** when BLE flips on. Runtime additions: S140 ~8-16 KB of its reserved region + Bluefruit FIFOs/ATT ~0.5 KB. Effective post-BLE ≈ 137 KB (58%), ~94 KB spare.
- **Verdict: fits with healthy margin on both.** RAM is the tighter axis but the "squeeze" is notional (pre-reserved). **Caveat: the numbers can't be confirmed until a `Bluefruit.begin()` build links** — request a real link-map check at Step 5.

## 5. Open DECISIONS to confirm before coding

(see `top_decisions` for the ranked list)

## 6. Honest "can't verify without hardware" boundary

NATIVE gives us: NV v7 round-trip, `cfg set` parsing, the **entire** window/policy state machine, and the line→Command→push→JSON dispatch (already covered by `test_console_parse.cpp`/`test_console_json.cpp`). Everything in §3's validation plan, the actual flash/RAM link numbers, MTU chunking under a real negotiated MTU, pairing/bonding, and DIO1 IRQ jitter under live BLE are **bench-only and user-verified** — the `device_nv`/`device_radio` model. Do not claim the keystone risk is resolved from native results.

## Open decisions (ranked)

1. Board-first order: confirm XIAO nRF52840 (S140/Bluefruit) is the Phase-2 target and ESP32-S3/NimBLE is deferred (the surveys + spec both center XIAO; the timing risk is nRF52-specific).
2. PREREQUISITE TO VERIFY ON BENCH: is the S140 binary actually FLASHED on the test XIAOs? The ldscript only proves the region is reserved (spec §8.1). If the bootloader is SD-less, S140 must be installed before any ble_mode!=off build can init BLE — this gates Step 5 entirely.
3. Wire format stays line-ASCII in / newline-JSON out (the existing console). Messaging (`send`/`sendhash`/`send_channel`) reuses `console_parse`+`console_json` with NO new decoder; **`whoami` reuses the existing (unwired) `write_ready`/`write_status` encoders**; full `cfg`/`routes`/`status` JSON defers to App Phase 3 (see §1). Confirm the iOS app commits to ASCII commands (INBOX_SYNC_CONTRACT.md:9 says yes).
4. Pairing default: ship MITM passkey ON with default PIN 123456, overridable via `cfg set ble_pin`, gating GATT to an encrypted+bonded link before serving characteristics (spec §A.3 mandatory). Confirm this is non-negotiable for Phase 2 (vs deferring security).
5. Windowing params for `periodic N`: confirm the advertising WINDOW duration (e.g. 30 s) and default period N — these are the power-vs-responsiveness dial and drive the CompanionPolicy tests. Spec leaves them TBD (§8 Q5/Q1).
6. Press-to-advertise input on XIAO: the XIAO has no obvious user button — decide between (a) a BLE-characteristic-write trigger, (b) a specific GPIO, or (c) skip press-to-advertise on XIAO and rely on `periodic`/`on`.
7. NV re-provisioning: confirm it's acceptable that bumping to v7 forces every existing node to re-run `cfg set` (the size-gate at device_nv.h:75 rejects v6 blobs; spec §4 says fine for 3 test nodes).
8. Scope boundary: Phase 2 serves ONLY the live push ring (msg_recv/channel_recv/etc.). pull_inbox/mark_read and the sender_hash/channel_msg_id push-field additions (INBOX_SYNC_CONTRACT) are Phase 3 — confirm they stay OUT of this scope even though the inbox stores are already stubbed in fw_main.cpp.
9. Test client: confirm tools/meshroute_client_ble.py (bleak) is the dev harness + iOS reference, and that adding `bleak` as a dev dependency (currently absent from the repo) is approved.

## Biggest risk

S140 SoftDevice CPU preemption jittering the tuned RTS/CTS/DATA/ACK windows (spec §8.1) — UNVERIFIABLE without hardware. Static analysis clears all peripheral conflicts (app's RTC1/TIMER2 vs S140's RTC0/TIMER0; SX1262 is external SPI so the internal RADIO is irrelevant; DIO1 is a plain GPIO ISR — `mr_on_dio1`, `device_radio.h:48`, the only radio IRQ, its priority must clear S140's reserved 0/1/4), and there is ONE hard code blocker — `device_rng.h:33-41` reads `NRF_RNG` directly which is illegal under an enabled S140 (the caveat at `device_rng.h:11-12`), fixable by guarding `mrrng::fill` (call sites `fw_main.cpp:472` first-boot and `:204` `regen`) with `sd_rand_application_vector_get()`. But whether an idle/windowed S140 keeps timing jitter inside the ACK-window slack can only be answered on the bench (idle-timing + active-window + DIO1-IRQ-drop + 30-min soak tests). The mitigation is structural — window the advertising, not the stack (no sd_enable/disable on schedule) — and the fallback is cheap and per-node: set ble_mode=off on relays/infrastructure so they keep today's exact bare-metal path, paying the timing cost only on carried/mobile nodes inside their connect windows.

---

# Client reference (iOS companion) — the production client's exact wire behavior

**Added by the app side (2026-06-11)** so this scope's firmware backend can implement to a known client.
This is what the **implemented + tested** iOS app does on the wire (`ios-companion/`), not a proposal —
it's the production analog of the Step 8 `bleak` harness. Reference impl:
`ios-companion/MeshRouteCompanion/Sources/Transport/BLENodeLink.swift` (CoreBluetooth central) +
`ios-companion/MeshRouteKit` (protocol codec + `MockNodeLink`, which speaks this contract for headless tests).

## C.1 Discovery + connection handshake (exact order)

1. **Scan** `scanForPeripherals(withServices: [NUS])`. ⇒ **the firmware MUST advertise the NUS service
   UUID** in the advertising packet (`Bluefruit.Advertising.addService(bleuart)`). A **name-only advert
   will not be discovered** by the app (service-UUID filtering is required for iOS background scanning and
   is the right way to catch a `periodic`/windowed node). The Step 8 bleak client scans by name; the iOS
   app does not — please add the service UUID to the advert.
2. `didDiscover` → `connect`. → `didConnect` → `discoverServices([NUS])` → `discoverCharacteristics([RX, TX])`.
3. `setNotifyValue(true)` on **TX** (`6e400003`). This first touch of the encrypted characteristic triggers
   iOS pairing (§C.2).
4. **The app treats the link as READY — and sends nothing before this — only when the TX-notify
   subscription CONFIRMS** (`didUpdateNotificationStateFor` success, which is post-bond). ⇒ the node should
   serve the TX CCCD subscription over the bonded link; that subscription-enable is the app's readiness gate.
5. On ready the app **sends `whoami\n`** and expects a `ready` JSON (§C.4). The node need NOT greet
   unsolicited — but an unsolicited `ready` on connect is harmless (the app fetches identity exactly once
   per connection and is idempotent).
6. On disconnect the app **re-scans** (to catch a `periodic` node's next advertising window).

## C.2 Pairing (MITM passkey)

- The **user types the 6-digit PIN into iOS's own system dialog** — the app cannot pre-fill or supply it
  programmatically. Present the node's IO capabilities so iOS shows a **passkey-ENTRY** prompt (with
  `setMITM(true)` + `setPIN`, a "DisplayOnly" peripheral ⇒ the iPhone keyboard-enters the passkey — correct).
- A wrong PIN / refused bond surfaces to the app as a notify/subscribe **auth error** → it shows
  "pairing failed (check the node PIN)". Bonding **persists**; the app reconnects without re-pairing.

## C.3 Framing

- **app→node:** one console line per command, **`\n`-terminated**, UTF-8. The app chunks a long line across
  writes at the negotiated MTU (`maximumWriteValueLength(.withoutResponse)`), **Write Without Response**. ⇒
  the node reassembles RXD bytes to `\n` (it already does, per §1).
- **node→app:** **newline-delimited JSON, one object per line.** The node MUST chunk a `write_push`/`write_ack`
  line across notifies (§1 MTU) — the app reassembles by `\n` (a `LineAccumulator`: strips `\r`, ignores blank
  lines, 4 KB runaway-line guard). **Non-JSON / unrecognized lines are kept verbatim** in the app's debug
  console but never acted on — so stray human text is harmless, but anything the app must act on has to be JSON.

## C.4 Commands the app sends (Phase 2)

- **Messaging:** `send <id> <text>` · `send_ack <id> <text>` · `sendhash <hex> <text>` ·
  `sendhash_ack <hex> <text>` · `send_channel <ch> <text>` → each expects an `{"ack":…}`.
- **Identity:** `whoami` (on connect + on demand) → expects `{"ev":"ready",…}`.
- **`resolve <hex> [hard]`** → expects `{"ev":"hash_resolved",…}` (this IS wired in Phase 2 via the push ring).
- **USB-only in Phase 2 (the app tolerates no reply):** `status` · `cfg` · `cfg set …` · `lookup` · `hashof`.
  The app may send these (raw console) but does not block on a BLE reply — fine that they're Serial-only this phase.
- **Phase 3 only** (sent **only if** `ready` carried `inbox_epoch`): `pull_inbox <dm_since> <chan_since>` ·
  `mark_read <dm|chan> <seq>`.

## C.5 Pushes the app decodes (must match `console_json` exactly)

`{"ack":"<code>","ctr":N,"qd":N}` · `{"ev":"msg_recv","origin":N,"ctr":N,"body":"…"}` ·
`{"ev":"channel_recv","origin":N,"channel_id":N,"body":"…"}` · `{"ev":"send_acked"|"send_failed","dst":N,"ctr":N}` ·
`{"ev":"hash_resolved","node":N,"auth":0|1,"hash":<decimal u32>}` · `{"ev":"ready",…}` · `{"log":"…"}` ·
`{"err":"code","msg":"…"}`. Any other `{"ev":"X",…}` is retained as a generic event (no app change needed to
surface a new event type). Phase 3 adds `inbox_dm`/`inbox_channel`/`inbox_end` + the optional identity fields below.

## C.6 Graceful degradation (so Phase 2 ships without waiting on Phase 3)

The app treats **`sender_hash`** (on `msg_recv`/`inbox_dm`), **`channel_msg_id`** (on `channel_recv`/`inbox_channel`),
and **`inbox_epoch`** (on `ready`) as **optional**. Against a Phase-2 node that sends none of them: live
messaging + delivery state + `whoami` all work; DM threads key by `origin` (+ `resolve`), channel dedup is
best-effort, and inbox sync stays **dormant** (no `inbox_epoch` ⇒ the app never sends `pull_inbox`). When the
node reaches Phase 3 and adds those fields, stable-hash DM threading + inbox catch-up activate automatically —
**no app release required.** (Field semantics: `INBOX_SYNC_CONTRACT.md`.)

## C.7 `whoami` field set the app uses

From `write_ready`: **`key`** (= `key_hash32`, REQUIRED — it's the contact-map identity, "the reason the app
exists"), plus `id` · `mode` · `gateway` · `routing_sf` (shown on the Node screen). Optional, used-if-present:
`name` (a friendlier display than the hash) and `inbox_epoch` (gates Phase-3 sync). Not needed: `gateway_only`/`mobile`.
