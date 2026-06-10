# Firmware OTA update (over BLE)

Update a node's firmware wirelessly — no USB cable. **nRF52 (XIAO) only** for now; the ESP32
(Heltec) `esp_ota` path is not wired yet (see the design spec §B.2). Design rationale +
verification details live in `docs/superpowers/specs/2026-06-10-ble-companion-ota-inbox-design.md`.

The mechanism: the app does **not** run a BLE stack. The `ota` console command writes a retained
"DFU" magic and resets; the **bootloader** then brings up its own SoftDevice and runs the BLE DFU.
So OTA works on the plain bare-metal firmware.

## One-time: install the OTAFIX bootloader

The stock Adafruit nRF52 bootloader's OTA is flaky; use the **OTAFIX** fork, which adds a safety net
(a failed update stays reachable in BLE DFU for a retry instead of bricking).

1. Get `update-xiao_nrf52840_bootloader-*.uf2` from
   <https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX/releases>.
2. **Double-tap RESET** on the XIAO → a `XIAO-SENSE`/`XIAO-BOOT` USB drive appears.
3. Drag the `.uf2` onto that drive. The board reboots with OTAFIX installed.
4. Re-flash the MeshRoute app once over USB (`pio run -e xiao_sx1262 -t upload`) — installing a
   bootloader can disturb the app region.

> **Checkpoints:** confirm the bootloader you install includes the **S140 SoftDevice** (the OTA DFU
> needs it). After this, verify the node still has its identity (`whoami`) and config (`cfg`) — the
> internal-flash NV (`/mrid`, `/mrcfg`) is in a different region and should survive, but check.

## Each update

1. **Build the DFU package:**
   ```
   pio run -e xiao_sx1262
   ```
   This produces the Nordic DFU zip at `.pio/build/xiao_sx1262/firmware.zip` (`upload_protocol =
   nrfutil`). That `.zip` — *not* the `.uf2` or the `.hex` — is what the DFU app expects.
2. **Put the node into DFU:** on the node's USB console (115200), type:
   ```
   ota
   ```
   It prints `entering OTA DFU (BLE)…` and reboots. The bootloader now advertises over BLE for DFU.
3. **Push the firmware over BLE:** open **Nordic "nRF Device Firmware Update"** (Android/iOS) or
   **nRF Connect** → select the advertised device → choose `firmware.zip` → **Start**.
   - Suggested settings (OTAFIX): PRN on, 30 packets, **reboot 0 ms / auto-reboot ON**, "Request high
     MTU" on (turn off if it fails early), Keep-bond off.
   - **The reboot into the new app is the DFU app + bootloader's job, not the firmware** (the app
     isn't running during DFU, so it can't trigger it). If the DFU app's auto-reboot/reset is **off**,
     the device **stays in DFU** after the transfer and you must double-tap RESET (or re-plug) to boot
     — so make sure auto-reboot is **on**.
4. On success — with auto-reboot on — the node reboots straight into the new firmware and the USB COM
   port reappears. Confirm over the console (`status`, `cfg`). (If the node clearly booted but the COM
   port doesn't come back on your host, that's a USB-CDC re-enumeration quirk — re-plug to force it.)

## If something goes wrong

- **Abort before pushing:** double-tap RESET → enters UF2 mode (a USB drive); drop a `.uf2` to
  recover over USB.
- **A failed/interrupted OTA:** with OTAFIX the node stays in BLE DFU (no UF2 drive, no serial port —
  that's expected) — just retry the push. Worst case, double-tap RESET → UF2 → re-flash over USB.

## ESP32 (Heltec) — not yet

`ota` prints "unsupported on this build yet." The ESP32 path is `esp_ota` partitions + rollback; the
delivery transport (WiFi-AP web upload vs BLE-push) is an open decision — see the spec §B.2.
