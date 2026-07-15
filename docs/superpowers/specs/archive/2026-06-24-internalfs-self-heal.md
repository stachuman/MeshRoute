<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# InternalFS self-heal — don't let a LittleFS corruption brick a node (nRF52)

**Status:** coder instruction. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. nRF52840 (Parts 1+2 — the Adafruit LittleFS InternalFS; ESP32 uses NVS, unaffected); Part 3 is platform-neutral. No wire change.

## Why

Bench-confirmed: a node halts at boot with `assertion "head >= 2 && head <= lfs->cfg->block_count" failed … lfs_ctz_find` — the **InternalFS (LittleFS) is corrupted** (a file's CTZ skip-list head points at a garbage block). And the Adafruit LittleFS build has **asserts ENABLED**, so it **halts** instead of returning `LFS_ERR_CORRUPT` — the node can't even reach its console (`factory_reset` is unusable). It's bricked-to-serial.

Root: a **reset landed during an InternalFS write**, and the nodes have been reset HARD (crashtest/WDT/manual/power-cycle) over a busy write surface — the inbox meta (`/mri_dm`,`/mri_ch`) **every 30 s unconditionally** (fw_main.cpp:1771 → `flush()` → `set_next_seq`), `/mrcfg` on the **channel-ctr change-detect (every channel send)**, `/mrfault` every boot. The NV writes are `remove()`+`open(O_WRITE)` (not crash-atomic). LittleFS is block-level power-safe, but enough reset-during-write + asserts-on = corruption → brick. This is a latent hole the reset-heavy testing exposed; one unlucky reset can brick any node.

**Decisions (user, 2026-06-24):** address it with **(1) asserts→errors, (2) format-on-corrupt self-heal, (3) cut the write churn.**

## The change

### Part 1 — `-DLFS_NO_ASSERT` (nRF52 build): a corruption returns an error, doesn't halt
Add `-DLFS_NO_ASSERT` to the nRF52 envs' `build_flags` (gateway, xiao_sx1262, and the gateway_heltec/esp32 nRF... — every env whose framework is the Adafruit nRF52 core / Adafruit_LittleFS). With it, `LFS_ASSERT` compiles out and lfs ops **return `LFS_ERR_CORRUPT`** on a bad block instead of `assert()`-halting. ⚠ **Verify it reaches `lfs.c`**: Adafruit_LittleFS is a framework lib — confirm the flag propagates to its compile (the assert at lfs.c:1144 must no longer halt; if the lib compiles with isolated flags, force it via the global build_flags / a `board_build` override). This alone un-bricks the boot (the corrupt read returns an error → the load falls back to defaults).

### Part 2 — format-on-corrupt mount (the self-heal): always boot
Add `device_nv::mount_or_repair()`, called ONCE at the very top of `setup()` (before any `load*`):
1. `InternalFS.begin()`.
2. **Detect corruption** (with asserts off, lfs now reports it): probe each known file — `/mrcfg`, `/mrid`, `/mrpeers`, `/mri_dm`, `/mri_ch`, `/mrfault` — open + read a byte; a read that returns an **error** (not cleanly-absent / EOF) ⇒ the FS is corrupt. (Also treat `begin()==false` as corrupt.)
3. On corruption: `InternalFS.format()` then `InternalFS.begin()` → a **clean** FS. Log it loudly (`Serial`/the boot banner: `INTERNALFS CORRUPT — REFORMATTED (re-provision needed)`), and record it so `faults`/`version` can surface it.
4. The normal `load*` then run on the clean FS → all defaults → the node **BOOTS** (no brick).

⚠ **Consequence (accepted):** a reformat wipes `/mrid` too → the node **re-mints its identity** (new key_hash32/address) and **loses its join** → it must be **re-provisioned** (the harness `provision`, or `cfg set` + `join`). That's the cost of self-heal vs a brick. (Identity-preservation across a corrupt-format is a possible later refinement — read+stash `/mrid` before formatting, restore after — but v1 accepts the loss; a corrupt FS makes even `/mrid` suspect.)

★ **This fix is ALSO the recovery image** for the currently-bricked node: flash it → it boots, sees the corrupt FS, reformats, comes up clean → re-provision.

### Part 3 — coalesce ALL the periodic InternalFS writes (one unified slow flush)

The corruption window is the *total* InternalFS write rate, so address **every** frequent writer, not just the inbox — under one principle: **user-commanded writes persist immediately (infrequent, expected); auto/frequent writes are change-detected, dirty-flagged, and coalesced onto a single slow periodic flush** (≈ every 120–300 s, tunable). A 5-second burst of activity then costs **one** write per blob, not dozens.

**The mechanism (fw_main):** a small dirty-flag set + ONE periodic `nv_flush()` in `loop()` (replacing the 30 s inbox-only flush at :1771). Each cycle it writes only the blobs whose dirty flag is set, then clears them. Change-detect at the write (skip if the blob bytes equal what's already on flash — many "changes" are no-ops).

Apply it to every auto-writer:
- **Inbox meta** (`/mri_dm`,`/mri_ch`): `Inbox::flush()` (inbox.cpp:157) becomes a **no-op when `_dm_unpersisted==0 && _chan_unpersisted==0`** (today it `set_next_seq`-writes unconditionally every 30 s). `nv_flush()` calls it on the slow cycle. Records are on QSPI; only the *cursor* meta is deferred — a power-loss loses ≤ one cycle of cursor advance, which the harness re-pull tolerates.
- **`/mrcfg` channel-ctr** (g_persist_channel_ctr, fw_main:137 — today a per-channel-send write): set a `cfg_dirty` flag on ctr change instead of writing; `nv_flush()` persists it. ⚠ Keep the reboot-id-reuse guard correct: write the ctr **`live + lease_margin`** (a lease) so a reboot inside the un-flushed window can't reuse — `lease_margin` ≥ the max channel sends per flush cycle (bounded; on boot, resume from the leased value).
- **Any other periodic InternalFS writer** — audit and route through the same dirty-flag/`nv_flush()` path (e.g. the DAD join-state persist g_persist_id/epoch/join if it can fire often; location/`loc_in_dm`). The rule: nothing auto-writes InternalFS inline — it sets a dirty flag.
- **Keep immediate (do NOT coalesce):** `cfg set` / `join` / `leave` / `create` / `regen` / `factory_reset` (user commands — must persist on the command, and they're rare) and **`/mrfault` at boot** (exactly one write per boot — not periodic).

**Result:** an idle node writes InternalFS ~never; a busy node a couple of times per flush cycle — a fraction of today's rate, so far fewer reset-during-write chances. Combined with Parts 1+2, a corruption that *does* slip through is survived, not fatal.

## Note (optional reinforcement, NOT in scope unless you say so)
Crash-atomic NV writes — write `/mrcfg.tmp` then `rename` to `/mrcfg` (LittleFS rename is atomic) — would close the `remove()`+rewrite corruption window at the source. Bigger change; Parts 1-3 make corruption survivable, which is the priority. Flagged for later.

## Tests / gate

- **Native unit:** `Inbox::flush()` is a no-op when `_unpersisted == 0` (assert no `set_next_seq` call), writes when `> 0`. (The `nv_flush()` coalescer + the channel-ctr lease live in fw_main = device — bench-verified; the format-on-corrupt + LFS_NO_ASSERT are device-only too.)
- **Build:** `pio run -e gateway -e xiao_sx1262 -e heltec_v3 -e xiao_esp32s3` — and **confirm the `lfs.c` assert is gone** on the nRF52 builds (the whole point of Part 1; grep the build or test on metal).
- **No sim impact:** device-NV only; lib/core `flush()` change is the only sim-visible bit and it's a write-gating no-op (the sim has no InternalFS store → `set_next_seq` is the RAM fake → verify s18 unchanged).
- **Metal (user — the proof):** (a) the **bricked node** recovers — flash this fw, it boots with `INTERNALFS CORRUPT — REFORMATTED`, comes up clean → re-provision; (b) a healthy node boots normally + the InternalFS write rate drops (observe far fewer NV writes over an idle minute); (c) ⚠ **the channel-ctr lease holds** — send several channels, **reboot BEFORE the flush cycle**, and confirm the resumed ctr is AHEAD of the last live value (no id reuse — the v15 reboot-id-reuse fix must survive the coalescing); (d) optional: induce a corruption (power-cut mid-write, or `crashtest reboot` in a tight loop during traffic) and confirm the node **self-heals** instead of bricking.

## Sequencing

Independent of the channel/rcmd/fault-log work. **High priority** — until it lands, every node is one unlucky reset from a brick, and the only recovery is `nrfjprog --eraseall` or this fix flashed via UF2. The reformat-on-corrupt + reduced churn is the durable fix.
