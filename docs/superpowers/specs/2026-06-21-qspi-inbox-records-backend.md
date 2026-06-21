<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# QSPI inbox-records backend — wire the 6 `qspi_*` primitives (nRF52 / XIAO 2 MB)

**Status:** READY FOR CODER. The store logic is DONE + host-tested; only the external-flash bring-up + the 6 `qspi_*` seams remain. On-metal QSPI behaviour is **BENCH-VERIFIED BY THE USER** (the reality-split in `device_inbox_store.h`). I quality-gate the build + the host logic; the user bench-verifies the flash. Author commits.

> **MeshCore reference — RESOLVED (Explore pass over `~/MeshCore`, 2026-06-21):** MeshCore brings the XIAO QSPI up via the **`oltaco/CustomLFS`** library — `CustomLFS_QSPIFlash`, an `Adafruit_LittleFS` subclass driving the nRF52840 `nrfx_qspi` HAL directly (NO `Adafruit_SPIFlash`, NO `SdFat` → no TinyUSB clash), with a built-in JEDEC chip table that already covers the XIAO's **P25Q16H (2 MB)**. §2 is the exact recipe — PORT that, don't invent one.

## 1. What's done vs what's left
- **DONE + host-tested (do NOT touch the logic):** `mrinbox::DeviceInboxStore` (`src/device_inbox_store.h`) — the segmented append-log, the ring (head/tail, roll-at-`seg_bytes`, drop-oldest), the framing `[u16 framed_len][u32 seq][rec]`, the §10.1 epoch-bump-on-wipe, and the **META on the on-chip InternalFS** (`load_meta`/`save_meta`, the proven `device_nv` File API). Verified host-side against a fake (`segmented_inbox_store.h`).
- **TO BUILD:** the **RECORDS** half — an `Adafruit_LittleFS` filesystem on the XIAO's **external 2 MB QSPI flash**, and the **6 `qspi_*` primitives** implemented as File ops on it. Then `#define MRINBOX_QSPI_READY` (which removes the stub block at `device_inbox_store.h:241-248`).

## 2. The crux — add the CustomLFS QSPI library (the "MeshCore way", RESOLVED)
The proven dead-end (`device_inbox_store.h:233` + `platformio.ini`): `adafruit/Adafruit SPIFlash` pulls `SdFat - Adafruit Fork`, whose `SS`/`File` symbols break `Adafruit_TinyUSB`; and `Adafruit_LittleFS` has no ctor over a `SPIFlash`. **Do not retry that.**

MeshCore solved this with **`oltaco/CustomLFS`** — `CustomLFS_QSPIFlash` is an `Adafruit_LittleFS` subclass that drives the nRF52840 **`nrfx_qspi`** peripheral directly (no SdFat) and carries a JEDEC table that already includes the XIAO's **P25Q16H (2 MB)**. It exposes the SAME Arduino `File` API we already use for the InternalFS meta. So this slice is mostly *configuration*, not driver code. Port MeshCore's `Xiao_nrf52_companion_radio_*` env:

- **lib_deps:** add `https://github.com/oltaco/CustomLFS @ 0.2.1` to the `xiao_sx1262` env. (Or vendor it pinned per [[meshcore-vendoring-strategy]] — coder/user call; lib_deps mirrors MeshCore.)
- **build_flags:** `-D QSPIFLASH=1 -D NRFX_QSPI_ENABLED=1` (the header `#error`s without the latter). (MeshCore also carries `-D EXTRAFS=1 -D LFS_NO_ASSERT=1` — see the ⚠ ld note.)
- **QSPI pins** in our vendored variant (`variants/Seeed_XIAO_nRF52840/variant.{h,cpp}` — note OUR variant is itself vendored from MeshCore, so it may ALREADY carry these; check first): `PIN_QSPI_SCK 24 / CS 25 / IO0 26 / IO1 27 / IO2 28 / IO3 29`, mapped in `g_ADigitalPinMap` to the XIAO physical pins (MeshCore: P0.21 / P0.25 / P0.20 / P0.24 / P0.22 / P0.23).
- **The instance:** a global `CustomLFS_QSPIFlash QSPIFlash;` + a one-time `QSPIFlash.begin()` in `fw_main` setup (after `InternalFS.begin()`; format-on-mount-fail is built in). Then the 6 `qspi_*` just call `QSPIFlash.open()/.read()/.write()/.remove()/.exists()/.format()` — identical to `load_meta`/`save_meta` on `InternalFS`. The on-chip `InternalFS` (our device_nv + inbox META) is untouched and independent.
- **⚠ Linker script / EXTRAFS — the one real risk:** MeshCore's QSPI env swaps `board_build.ldscript` to `boards/nrf52840_s140_v7_extrafs.ld` (we use `nrf52840_s140_v7.ld`). **Try `QSPIFLASH` ALONE first, WITHOUT `EXTRAFS`/the extrafs ld** — records live on the SEPARATE 2 MB chip, so no on-chip repartition should be needed, and keeping our current ld leaves the on-chip `InternalFS` (device_nv + meta) in place. Adopt the extrafs ld ONLY if `QSPIFlash.begin()` actually requires it — and then bench-verify existing NV survives (a moved InternalFS region wipes cfg/id/peers/meta on first flash; OK only as an intentional one-time reset). Resolve empirically on the bench.

MeshCore files to read: `CustomLFS/src/CustomLFS_QSPIFlash.{h,cpp}` (driver + JEDEC table + lfs_config), `variants/xiao_nrf52/variant.{h,cpp}` (pins), `variants/xiao_nrf52/platformio.ini` (the `QSPIFLASH=1` env + ld), `examples/companion_radio/main.cpp` (the `QSPIFlash.begin()` sequence).

## 3. The 6 `qspi_*` primitives (exact contracts — from `device_inbox_store.h`)
Implement these as open/read/write/remove on the `QSPIFlash` instance (§2), MIRRORING `load_meta`/`save_meta`. Paths are `<dir>/<idx>` via the existing `seg_path()` (e.g. `/dm/0`, `/ch/3`). The class already does all ring/seq/cap math — these are pure file ops:

| primitive | contract |
|---|---|
| `static bool qspi_mount(bool* formatted)` | mount `g_qspi_fs`; on mount failure, `format()` + remount and set `*formatted=true`. Return false only if truly unmountable (→ `begin()` fails → inbox disabled, fail-loud). |
| `bool qspi_seg_size(uint16_t idx, uint32_t* size) const` | size of `<dir>/<idx>`; false if the file is absent (not an error). |
| `bool qspi_seg_append(uint16_t idx, const uint8_t* b, uint16_t n)` | append `n` bytes to `<dir>/<idx>` (open append/create, write, close). |
| `uint32_t qspi_seg_read(uint16_t idx, uint8_t* out, uint32_t cap) const` | read the WHOLE segment into `out` (≤ `cap`); return bytes read (0 if absent). `cap` is `inbox_segment_bytes` (4 KiB) = `kSegScratch`. |
| `void qspi_seg_erase(uint16_t idx)` | remove/empty `<dir>/<idx>` (drop-oldest + new-head-clear both call this). |
| `bool qspi_any_segments() const` | does `<dir>` hold ANY record bytes? (used for §10.1 wipe-detect; check the dir for non-empty segment files). |

Ensure the record dirs (`/dm`, `/ch`) exist (mkdir-on-mount if the FS needs it). Keep `kSegScratch`/`inbox_segment_bytes` as the single segment-size source (begin() already guards `_seg <= kSegScratchBytes`).

## 4. Flip the switch
`#define MRINBOX_QSPI_READY` (where the stub `#ifndef` guard is) once the six are real — this drops the `return false` stubs so `begin()` succeeds and the inbox goes live on the XIAO. Confirm the inbox is actually wired in `fw_main` (the two `DeviceInboxStore` instances DM+channel constructed with `/dm`,`/mri_dm` and `/ch`,`/mri_ch`).

## 5. Resolve the `factory_erase` TODO (`device_nv.h`)
With records now on QSPI, `mrnv::factory_erase()` must wipe them too (the TODO comment I added marks the spot). Add: mount `g_qspi_fs` (or reuse the inbox's mount) and **remove the `/dm` + `/ch` record dirs/segments** alongside the InternalFS meta (`/mri_dm`,`/mri_ch`) it already removes. Net: a `factory_reset confirm` leaves zero messages on QSPI. (Keep it best-effort/idempotent like the rest of `factory_erase`.)

## 6. ESP32 / Heltec — OUT OF SCOPE (next slice)
Leave the ESP32 branch (`device_inbox_store.h:251`) stubbed → `begin()` fails → inbox disabled on Heltec (record_* inert), exactly as today. A LittleFS-data-partition backend there is a later slice. So `factory_erase` on ESP32 stays as-is (the `Preferences.clear()` already covers all its persisted NV; no records to wipe).

## 7. Gate
- **Host logic unchanged:** native suite green (the store mechanics are host-tested via the fake; this slice only fills the device seams — no `#if !ARDUINO` logic changes).
- **Both boards build:** `xiao_sx1262` (now with `MRINBOX_QSPI_READY` → real qspi_*) **and** `heltec_v3` (still stubbed). `gateway` too.
- **⚠ Linker/flash-layout (§2):** if the extrafs ld is adopted, verify the build links AND that existing on-chip NV (device_nv cfg/id/peers + inbox meta) survives — a moved `InternalFS` region wipes it. Prefer `QSPIFLASH`-only (no `EXTRAFS`) so our current ld + `InternalFS` stay put.
- **★ USER BENCH-VERIFY (on metal, the reality-split):** mount + format-on-first-boot; append across a segment roll; `read_since` returns the right records oldest-first; drop-oldest at the byte cap; **power-cycle restore** (records survive reboot, seq continues, no reuse); **§10.1 wipe-detect** (erase QSPI records only → epoch bumps, meta/next_seq survive on InternalFS); and **`factory_reset confirm` leaves the QSPI empty** (§5). The companion `pull_inbox` returns the persisted history after a reconnect.
- I gate the build + host suite + the diff (no store-logic change, the meta path untouched, lib_deps clean); the user signs off the bench checks before commit.

## 8. Build order (coder)
1. Add `oltaco/CustomLFS @ 0.2.1` + `-D QSPIFLASH=1 -D NRFX_QSPI_ENABLED=1` + QSPI pins (check our vendored variant FIRST) + a global `CustomLFS_QSPIFlash QSPIFlash;` & `QSPIFlash.begin()` in fw_main (§2). Try WITHOUT `EXTRAFS`/the extrafs ld first.
2. The 6 `qspi_*` as File ops on it (`<dir>/<idx>` via `seg_path`); mkdir `/dm` `/ch`.
3. `#define MRINBOX_QSPI_READY`; confirm fw_main constructs the two stores.
4. `factory_erase` QSPI-records wipe (§5) — resolves the TODO.
5. Build all three envs + native; hand back green-shaped + uncommitted → I gate, then you bench-verify (§7) before the author commits.
