# BLE Companion, OTA, and Persistent Inbox — Design

**Status:** DESIGN (shape converged in discussion 2026-06-10; not yet implemented)
**Boards in scope:** Seeed XIAO nRF52840 (`xiao_sx1262`) + Heltec WiFi LoRa 32 V3 / ESP32-S3 (`heltec_v3`).
**Phasing:** Phase 1 = OTA (bare-metal trigger + off-the-shelf DFU tools — needs no custom client).
Phase 2 = phone connection (BLE companion). Phase 3 (later) = the persistent inbox.

## 1. Goal

Let a phone connect to a node to read its messages and drive it, and let firmware be updated
over-the-air — without sacrificing the deterministic LoRa timing that motivated the bare-metal
nRF52 build. One firmware build per board; BLE is a **configurable duty-cycle policy**, not a
compile-time variant. A later phase adds a **flash-backed persistent inbox** so a node holds
messages between connections.

## 2. Current state (what we build on)

- **The command seam is already transport-agnostic** (`command.h`): typed `Command` in / typed
  `Push` out, "lib/core never sees a transport byte," and it explicitly anticipates a
  "device **serial-BLE**" backend. `PushKind` mirrors MeshCore's `PUSH_CODE_*`.
- **Live inbox = the push ring** (`_push_ring`, `cap_push_ring = 16`, drop-oldest). Good for a
  connected client; not durable, and it mixes `msg_recv` with transient acks.
- **NV today = InternalFS** (Adafruit LittleFS on internal flash) for `/mrcfg` (config, NV v6) +
  `/mrid` (identity). `cfg set` persists config; `is_mobile`/`is_gateway` already persist.
- **nRF52 runs bare-metal** (no SoftDevice; `device_rng` reads `NRF_RNG` directly) — but is
  **already linked for S140** (`ldscript = nrf52840_s140_v7.ld`) and the Adafruit bootloader ships
  S140, so **the SoftDevice is present in flash, just not initialised**.
- **XIAO has a 2 MB QSPI flash (P25Q16), wired but unused** (the variant deselects its CS at boot).
- **Heltec V3 = ESP32-S3**, arduino-esp32 3.1.3, single-app flash layout today (no OTA partitions),
  no BLE. Has WiFi + BLE 5.0 + ~8 MB flash + dual core.

## 3. Architecture — one core, per-platform backends

```
                 lib/core (platform-agnostic)
   ┌───────────────────────────────────────────────────────────┐
   │  Node (typed Command/Push seam)                            │
   │  Companion policy (ble_mode / period / advertising window) │
   │  Inbox logic (records, eviction, DM vs channel, sync cur.) │  ← Phase 2
   └───────────────────────────────────────────────────────────┘
        │ companion transport HAL │ flash-store HAL │ ota HAL
   ┌────┴──────────┐      ┌────────┴────────┐   ┌────┴───────────────┐
   nRF52 (Bluefruit/S140) nRF52 (QSPI LittleFS)  nRF52 (OTAFIX bootldr DFU)
   ESP32 (NimBLE)         ESP32 (LittleFS part.) ESP32 (esp_ota)
```

The companion BLE transport just parses its wire **into the existing `Command` PODs** and drains
`next_push` — identical to what the USB-CDC console does today. The policy, the inbox, and the sync
protocol are platform-agnostic and **unit-testable in `native`**; the BLE stack, the flash store,
and OTA are platform device code, **bench-verified on hardware** (same split as `device_nv`).

---

## PART A — Phone connection (BLE companion)

### A.1 Duty-cycle policy (the key idea: window the *advertising*, not the SoftDevice)

A persisted config `ble_mode ∈ { off, on, periodic }` (+ `ble_period_min` for periodic), decided at
**boot** (reboot to change, like other cfg):

- **`off`** — the BLE stack is never initialised. nRF52 runs exactly as today (bare-metal, direct
  `NRF_RNG`). Pure timing, lowest power.
- **`on`** — BLE stack up, advertising continuously. For carried/mobile nodes (`is_mobile`) where a
  phone connects anytime.
- **`periodic N`** — BLE stack initialised **once** at boot and left up *idle*; **advertising** is
  toggled on for a short window every `N` minutes (and on a press-to-advertise gesture, A.4). A
  phone scanning in range connects during a window and is served until it disconnects or signals
  done; then advertising goes back to sleep until the next window.

**We never `sd_enable`/`sd_disable` (or tear down NimBLE) on the schedule** — only advertising
toggles. An *idle* enabled stack barely perturbs timing; the CPU-preemption cost is only during
active advertising/connection, i.e. only inside the windows. This sidesteps the fragile repeated
stack up/down entirely. (Pedantically: the node *advertises*; the phone, scanning, *connects*.)

**Per-platform rationale for duty-cycling differs:**
- **nRF52**: primarily **timing** — the SoftDevice preempts the CPU during BLE events, jittering the
  tuned RTS/CTS/DATA/ACK windows. Windows bound that to the advertising/connection periods.
- **ESP32-S3**: primarily **power** — it's dual-core (BLE pins to one core, app/LoRa to the other),
  so there's no SoftDevice-style preemption, but BLE-always-on costs power. The policy is the same.

### A.2 The companion transport (both platforms)

A `bleuart`-style GATT service (Nordic UART Service-compatible) carrying the **existing typed
command/push protocol** — the same bytes the JSON/line console exchanges, framed for BLE. RX
characteristic → parse into `Command` → `on_command`; TX characteristic ← drain `next_push`. No new
seam; it's a third backend next to USB-CDC and the sim's `FirmwareNode`.

- **nRF52**: Adafruit **Bluefruit** + `BLEUart`. `Bluefruit.begin()` initialises S140 at boot when
  `ble_mode != off`. `device_rng` becomes SD-aware: `sd_rand_application_vector_get` when the SD is
  enabled, `NRF_RNG` when not (the identity seed is read at boot, so this is a small branch).
- **ESP32-S3**: **NimBLE-Arduino** (footprint-friendly vs Bluedroid) GATT server, BLE task pinned to
  the non-LoRa core.

### A.3 Security / pairing (mandatory, not later)

A connecting phone reads messages and sends commands, so the link must be authenticated:
- **Static passkey (PIN)**: a baked-in **default** (e.g. `123456`), overridable via
  `cfg set ble_pin <6-digit>`, persisted in NV (v7). The SoftDevice/NimBLE does the pairing crypto;
  the app supplies the static passkey + requires an **encrypted, bonded** link before serving the
  GATT characteristics.
- Threat model: someone in range without the PIN can neither read the inbox nor issue commands.
  (Phase 2 inbox content at rest on flash is a separate question — note in §9.)

### A.4 Press-to-advertise (UX)

`periodic N` means up-to-`N`-min latency to connect. A **button/gesture → immediate advertising
window** gives on-demand connect without waiting. (`on` covers always-interactive nodes.) The period
is a power⇄responsiveness dial.

---

## PART B — OTA firmware update

A platform-dispatched **`ota`** console/companion command. The mechanism differs per platform; only
the command surface is common.

### B.1 nRF52 (XIAO) — bootloader BLE DFU via OTAFIX

- **One-time:** flash the **OTAFIX** bootloader (`oltaco/Adafruit_nRF52_Bootloader_OTAFIX`) via UF2
  (double-tap reset → drop `update-…​.uf2`). OTAFIX adds the safety net: a failed OTA stays in BLE
  DFU for retry instead of bricking (`main.c:219` sets `GPREGRET = 0xA8` when no valid app).
- **`ota` command (baseline, always works):** write the retained magic and reset into the bootloader's
  BLE DFU — verified against the OTAFIX source (`main.c:224-230`, `_ota_dfu = (gpregret == 0xA8) ||
  (gpregret == 0xB1)`):
  ```c
  NRF_POWER->GPREGRET = 0xA8;   // DFU_MAGIC_OTA_RESET — bootloader inits its own SD + BLE OTA DFU
  NVIC_SystemReset();           // GPREGRET is retained across the reset
  ```
  Works regardless of the app's BLE state (it's a terminal reboot). The user re-pushes via the
  **Nordic "nRF Device Firmware Update"** app to the bootloader's advert.
- **Enhancement (later):** when BLE is up, add the Bluefruit **`BLEDfu`** service so the *connected
  companion app* can trigger DFU in-session (magic `0xB1`, SD already inited) — no reconnect to a
  separate DFU advert. Same OTAFIX bootloader, nicer UX.
- **Package:** `pio run -e xiao_sx1262` already emits the Nordic DFU **`.zip`** (`upload_protocol =
  nrfutil`); unsigned (stock Adafruit/Nordic DFU has no signature step).
- **Identity survives** (OTA flashes only the app region; `/mrid` + `/mrcfg` on InternalFS are below
  it). The QSPI inbox (Phase 2) is in yet another region — but the OTAFIX *bootloader* install may
  disturb QSPI (the MeshCore "erase ExtraFS after a new bootloader" gotcha) — document it.

### B.2 ESP32-S3 (Heltec) — esp_ota partitions

Completely different (no bootloader DFU magic):
- **Partition table:** switch `board_build.partitions` to an **OTA-capable** layout (two app slots
  `ota_0`/`ota_1` + `otadata` + a data/LittleFS partition for the inbox). This is a build change;
  8 MB flash has ample room.
- **Write path:** `esp_ota_*` / `Update.h` writes the incoming `.bin` to the inactive slot, sets the
  boot partition, reboots; **rollback** on boot-failure for safety.
- **Transport — open decision (§10):**
  - **(B2a) WiFi SoftAP + web upload** (MeshCore's path: AsyncElegantOTA, browser → `192.168.4.1`).
    Proven, simplest, but needs WiFi up and a browser (off-theme vs "connect with phone over BLE").
  - **(B2b) BLE-push to `Update.h`** — stream the `.bin` in chunks over the companion GATT into
    `esp_ota_write`. Consistent with the BLE-companion direction, but a custom chunk/verify protocol.
  - **Lean:** B2b for UX consistency, but it's more work; B2a is the safe Phase-1 fallback. Decide
    before implementing ESP32 OTA.

### B.3 The `ota` command

Console (`fw_main service_debug`) + companion. Platform-dispatched: nRF52 → §B.1; ESP32 → §B.2. On
a board where OTA isn't wired yet, returns an explicit "unsupported on this build" rather than a
silent no-op.

---

## PART C — Persistent inbox (Phase 3 / later)

### C.1 Storage medium

- **nRF52 (XIAO):** the **2 MB QSPI flash** (present, unused). Bring it up with `Adafruit_SPIFlash` +
  a LittleFS instance on it. Room for thousands of short messages; ~10× the write endurance of
  internal flash; keeps the durable inbox off the config/identity flash. (This is MeshCore's
  InternalFS-small / QSPI-bulk split.)
- **ESP32-S3 (Heltec):** a **LittleFS data partition** (from the OTA-capable table in §B.2) on the
  internal 8 MB flash. Plenty of room.

The inbox is **flash-resident, not RAM-resident** — a "far larger than a few messages" store can't
sit in the 256 KB nRF52 RAM. `lib/core` owns the logic; a thin **flash-store HAL** owns the bytes
(QSPI/LittleFS on nRF52, LittleFS on ESP32) — mirroring `device_nv`'s discipline.

### C.2 Structure

A **circular append-log per type**:
- **DM log — large + durable.** Append on `msg_recv`; drop-oldest only on wrap; a missed DM is a real
  loss, so size generously (target ~thousands; final sizes in §10).
- **Channel log — smaller + freely drop-oldest.** Channels are broadcast, lower per-message value,
  and already have pull-repair, so aggressive eviction is cheap. (This is distinct from the RAM
  channel *buffer* used for gossip/relay — the channel *inbox* is the user-facing read store of the
  node's subscribed channels.)

Each record ≈ `origin · ctr · timestamp · flags(read/unread) · len · body`. Read by scan; a
read-cursor (or per-client cursor) tracks sync position. Wear: circular log + LittleFS wear-leveling;
batch where possible; QSPI/ESP endurance is the budget.

### C.3 Composition with the push ring

The **push ring stays** as the *live* notify path (real-time `msg_recv` to a connected client). The
**flash log is the durable history**. On `msg_recv`: append to the flash log **and** enqueue the push
(if a client is connected). A just-connected / periodic phone **catches up from the flash log**.

### C.4 Companion sync protocol (over the typed seam)

New typed commands/pushes (extending `command.h`):
- `pull_inbox <since_cursor>` → a stream of inbox-entry pushes (DM + channel) newer than the cursor.
- `mark_read <cursor>` / `delete <cursor-range>` → advance/prune.
No new transport concept — it's what the seam was built for. Unit-testable in `native` (drive
`on_command` + a fake flash-store HAL).

---

## 4. Config + NV (bump to v7)

New persisted `Blob` fields (NV `kVersion 6 → 7`, exact-size load gate as today; no migration — 3
test nodes):
- `ble_mode` (0=off,1=on,2=periodic), `ble_period_min` (uint8), `ble_pin` (uint32, default 123456).
- `cfg set` keys: `ble_mode <off|on|periodic>`, `ble_period <min>`, `ble_pin <6-digit>`; shown in
  `dump_cfg`. `ble_mode`/`ble_period`/`ble_pin` are **reboot-to-apply** (the BLE stack is brought up
  at boot from them).

## 5. Phasing

Reordered so the part that needs **no custom client** ships + tests first, and so every later build
can then flash wirelessly.

- **Phase 1 — OTA.** nRF52: the `ota` command (`0xA8` → the OTAFIX bootloader's BLE DFU) on the
  **current bare-metal build** — no SoftDevice/Bluefruit in the app (the bootloader owns the BLE DFU)
  — plus the OTAFIX-flash + Nordic-DFU-app runbook (`docs/ota.md`). ESP32: `esp_ota` partitions +
  rollback + the transport decision (§B.2). Tested with **nRF Connect / the Nordic DFU app** — no
  companion client required; once it works, every later build flashes over the air.
- **Phase 2 — Companion connect (per board).** BLE stack + `bleuart` GATT + the typed protocol +
  pairing/PIN + the `ble_mode`/period/window policy — this is where the SoftDevice actually gets
  initialised on nRF52 (and the §8.1 timing validation happens). Test client = a Python + `bleak`
  script (a BLE transport under `meshroute_client.py`) — the dev harness *and* a reference for the
  eventual phone app. Result: a phone reads live messages (push ring) + sends commands/config over BLE.
- **Phase 3 — Persistent inbox.** The flash-store HAL (QSPI/LittleFS) + the append-logs + the sync
  protocol + catch-up-on-connect.

## 6. Test plan

- **native (unit):** the companion policy (off/on/periodic window scheduling — pure logic), the inbox
  logic (append/evict/cursor, DM vs channel) against a fake flash-store HAL, the sync protocol
  (`pull_inbox`/`mark_read`/`delete` via `on_command` + pushes), and the NV v7 round-trip.
- **bench (hardware, user-verified — like `device_nv`):** the BLE stack bring-up + pairing + the
  duty-cycle on real silicon; the nRF52 SoftDevice-vs-LoRa-timing validation (the key risk, §10);
  QSPI bring-up + wear; the OTAFIX install + an actual OTA on each board.
- **both boards build green** at every phase.

## 7. Deliberate decisions / non-goals

- **One SD-capable build on nRF52** (S140 always linked — it already is); `ble_mode=off` simply never
  initialises it. No bare-metal-only variant to maintain.
- **Window the advertising, not the stack** (no scheduled `sd_enable/disable`).
- **Inbox is Phase 3** — Phase 2 (the companion) reads live messages over BLE (push ring); durability
  comes later. Phase 1 (OTA) touches none of this and stays bare-metal.
- **OTA is platform-specific** (nRF52 bootloader DFU vs ESP32 esp_ota); a single unified
  "DFU-over-BLE on both" is out of scope (would mean reimplementing the nRF52 bootloader protocol
  in-app).
- **No app-level firmware signing** initially (matches stock Adafruit/Nordic DFU); revisit if needed.

## 8. Open questions / risks (to resolve before / during implementation)

1. **nRF52 SoftDevice ↔ LoRa timing** — the headline risk. First **confirm the S140 binary is
   actually flashed** (it ships with the Adafruit XIAO bootloader; a SoftDevice-less bootloader would
   need the SD installed before BLE can init — the ldscript only proves the region is *reserved*).
   Then validate on the bench that an idle (and windowed-active) S140 doesn't break the tuned metal
   RX/TX/ACK windows. Check the SD doesn't claim a peripheral the HAL uses (TIMER0, SWI, etc. — the
   SX1262 is external SPI so the internal RADIO is not a conflict). If idle-SD measurably hurts
   timing, fall back to `off` on relays.
2. **ESP32 OTA transport** — WiFi-AP-web (B2a) vs BLE-push (B2b). Decide.
3. **QSPI bring-up + OTAFIX interaction** — the "erase ExtraFS after a new bootloader" gotcha; ensure
   the inbox store survives an app OTA (different region) and document the bootloader-install caveat.
4. **Inbox sizing** — concrete DM/channel log sizes (target message counts) → wear math.
5. **Press-to-advertise gesture** — which input on each board (XIAO/Heltec button availability).
6. **NimBLE vs Bluedroid** on ESP32 (footprint/RAM) — confirm NimBLE.
