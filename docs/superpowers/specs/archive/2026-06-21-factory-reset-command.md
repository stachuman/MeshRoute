<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# `factory_reset` — full NV wipe to factory-fresh

**Status:** READY FOR CODER. Decision **(a) full wipe incl. identity** LOCKED (author 2026-06-21). I quality-gate; author commits.

A console command that erases **all** persisted flash and reboots → the node comes up brand-new: default config, **a freshly-minted identity** (new key/address), no pinned peers, empty inbox. Today only `leave` exists (clears `/mrcfg`, keeps freq) — there is no full reset.

## 1. The command — `factory_reset confirm` (confirm-gated, fw_main-direct)

Dispatch alongside `reboot` (a direct `strncmp` in the command loop, NOT `console_parse` — it's a device action):
- **`factory_reset`** (bare) → print a warning + usage, do NOTHING:
  `> factory_reset WIPES ALL flash (config + identity + peers + inbox) and reboots to factory. Type 'factory_reset confirm' to proceed.`
- **`factory_reset confirm`** → `Serial.println(F("> factory reset — erasing all NV, rebooting…"));` then `mrnv::factory_erase();` then `do_reboot();`.

The literal `confirm` token guards against an accidental paste/fat-finger (irreversible). No other arg form is valid.

## 2. `mrnv::factory_erase()` — per-platform (in `device_nv.h`, beside the load/save backends)

Erases every NV slot. `save()` already wipes via `remove()`/overwrite, so this reuses the same primitives:

- **nRF52 / LittleFS** (`/mrcfg` `/mrid` `/mrpeers` + the inbox files): `InternalFS.begin();` then `InternalFS.remove(...)` each of the three config blobs **and the inbox records + meta files** (see `device_inbox_store.h` for the inbox paths — wipe BOTH the records store and the InternalFS **meta** that holds `next_seq`/`epoch`; this is a TRUE reset, not the records-only wipe that deliberately preserves the seq high-water — moot here since the identity is also gone).
- **ESP32 / Preferences** (`#elif ESP32`): `Preferences p; p.begin("mr", false); p.clear(); p.end();` (clears the whole `"mr"` namespace) + erase the inbox store's keys/partition.
- **native / unknown** (`#else`, the `return false` stub block): add `inline bool factory_erase() { return true; }` so the device-less build still compiles (no-op).

Signature: `inline bool factory_erase();` returning false if any slot failed to erase (the command logs it but still reboots — a partial wipe is still cleaner, and the next boot re-defaults any blob whose `magic`/`version` no longer validates).

> Implementation note: prefer **targeted `remove()` of the known files** over a blanket `InternalFS.format()` — the format is more thorough but would also nuke anything else on the FS (e.g. OTA state). Targeted removal of cfg/id/peers/inbox is the safe full-reset. (If the FS is known to hold ONLY these, `format()` is acceptable and simpler — coder's call, but default to targeted.)

## 3. `help`
Add to `dump_help()` (near `reboot`, in the `diag` group or its own line):
`[help] reset:      factory_reset confirm   (WIPE all flash — config + identity + peers + inbox — and reboot to factory)`

## 4. Tests + gate
- **Both boards build** (the erase + command are device-only; this is the primary gate — nRF52 `xiao_sx1262` **and** ESP32 `heltec_v3` if available, since the two NV backends differ).
- **Native suite green** (the `#else` no-op keeps native compiling; no native logic added).
- **Confirm-gating reviewable:** bare `factory_reset` must NOT erase (only warns); `factory_reset confirm` erases+reboots. (No console_parse unit since it's fw_main-direct; a focused read + the build is the gate.)
- `help` shows the new line.

## 5. Build order
1. `mrnv::factory_erase()` in `device_nv.h` — nRF52 + ESP32 + the native no-op stub.
2. `factory_reset` command in `fw_main.cpp` (confirm-gated, → `factory_erase()` + `do_reboot()`).
3. `help` line.
4. Build both boards + native; hand back green-shaped + uncommitted → I gate.
