# Durable Inbox (survives reboot) — Implementation Spec

**Date:** 2026-06-12 · **Status:** SPEC — ready for a coding agent. **Implemented by a coding agent; I quality-gate.**
**Goal:** make the persistent inbox **survive a reboot** on the XIAO nRF52840, replacing the interim volatile RAM store, **prioritising DM capacity**. Channel inbox is durable too but gets a smaller slice.

**Verified against the tree (2026-06-12):** the store *logic* already exists and is native-tested; this spec wires it to real flash + adds the two correctness pieces it needs. References are `file:line` from the current tree.

---

## 0. What already exists (reuse, don't rewrite)

- **`lib/core/segmented_inbox_store.h`** — the durable store LOGIC (segment ring + drop-oldest + reboot-restore + §10.1 epoch/high-water survival) behind two INJECTED interfaces. Native-tested (in the 300/300): `test/test_segmented_inbox_store.cpp` + `test/fake_inbox_storage.h`. **This is the engine — do not reimplement it.**
  - `struct ISegmentStore` — `mount(bool* formatted)`, `seg_size(idx,*sz)`, `seg_append(idx,b,n)`, `seg_read(idx,out,cap)`, `seg_erase(idx)`, `any_segments()`.
  - `struct IMetaStore` — `load(blob,len)`, `save(blob,len)`.
  - `class SegmentedInboxStore(ISegmentStore&, IMetaStore&, uint32_t cap_bytes, uint32_t seg_bytes)` — the `meshroute::InboxStore` the `Inbox` drains.
- **Interim default today:** `lib/core/fixed_inbox_store.h` — a volatile RAM ring (`FixedInboxStore<32>`), wired in `fw_main.cpp:86-92` under `#else` of `#if defined(MRINBOX_QSPI_READY)`. Lost on reboot; per-boot **random** epoch (`fw_main.cpp:641` `boot_epoch`) makes the app re-pull each boot. **This spec replaces it as the default.**
- **The old `src/device_inbox_store.h`** (`mrinbox::DeviceInboxStore`, QSPI stubs that fail) is **superseded** by `segmented_inbox_store.h`. Leave it only under the `MRINBOX_QSPI_READY` future branch (Step 2 makes it largely moot — see §6); do not extend it.

## 1. Capacity reality (set expectations — this is the safe path)

InternalFS is LittleFS with **4 KB-block granularity** (the nRF52840 flash page) and the segment ring needs **≥2 segments/store**. Of the ~28 KB Adafruit-default InternalFS (≈5 usable 4 KB blocks after LittleFS metadata; `/mrcfg`+`/mrid` inline, costing no data blocks), DM gets **~3 segments (12 KB) → ~140–200 short DMs**; channel **~2 segments (8 KB)**. Eviction is **segment-coarse**: a full store drops a whole oldest 4 KB segment (~60–70 DMs) at once, not one-at-a-time — acceptable because the phone is the long-term archive. **Confirm the real FS block count on the bench** (print `InternalFS` free space at boot). Bigger capacity ⇒ Step 2 (§6).

## 2. STEP 1 — InternalFS-backed durable store

### 2.1 New file `src/device_inbox_internalfs.h` (nRF52-gated, header-inline, like `device_nv.h`)
Implement the two interfaces over `Adafruit_LittleFS` / `InternalFS` (the SAME backend `device_nv.h` already uses for `/mrcfg` + `/mrid`):

- `class InternalFsSegmentStore : public meshroute::ISegmentStore`
  - ctor: a flat name prefix, e.g. `"idm"` / `"ich"` → segment files `"/idm.0"`, `"/idm.1"`, … (FLAT names — avoid LittleFS subdirs).
  - `mount(*formatted)`: `InternalFS.begin()`; set `*formatted=false` (InternalFS persists across reboot; it's only "formatted" if the core reformatted on corruption — treat a begin() that yields no segments as the wipe signal via `any_segments()`).
  - `seg_size`: `InternalFS.open(name,FILE_O_READ)`, return `.size()`; `false` if `!exists`.
  - `seg_append`: open `FILE_O_WRITE`, **seek to `.size()`** (LittleFS append), `write(b,n)`, `close`. (Confirm append semantics on the bench — if `FILE_O_WRITE` truncates, switch to read-modify or the core's append flag.)
  - `seg_read`: open `FILE_O_READ`, `read(out, cap)`, return bytes read.
  - `seg_erase`: `InternalFS.remove(name)`.
  - `any_segments`: true if any `"<prefix>.<i>"` exists with bytes (loop `i < seg_count`).
- `class InternalFsMetaStore : public meshroute::IMetaStore`
  - ctor: a meta file name, e.g. `"/idm.meta"`.
  - `load`/`save`: read/write the fixed `Meta` blob — copy `device_nv.h`'s `remove`-then-`write` save idiom verbatim.

### 2.2 Sizing + the read-scratch invariant
- **`seg_bytes = 4096`** for both stores (== `segmented_inbox_store.h`'s `kScratchBytes`). **Hard requirement:** `read_since` reads `min(seg, kScratchBytes)` of each segment, so a segment LARGER than the scratch **silently drops** records past 4 KB. Add `static_assert(seg_bytes <= kScratchBytes)` at the construction site (this is gate-finding #1 from the 2026-06-12 review — the unused `inbox_segment_bytes_dm/chan = 32K/16K` constants are a trap; either delete them or note they're NOT the InternalFS seg size).
- **Caps (DM-prioritised, tune to the bench FS size):** `cap_dm = 12*1024` (3 seg), `cap_chan = 8*1024` (2 seg). Add to `protocol_constants.h` as `inbox_internalfs_*` so they're one place to tune.

### 2.3 Epoch on a single filesystem (IMPORTANT correctness detail)
The §10.1 design bumps the epoch when records vanish but meta survives — that's natural with meta on a SEPARATE store. **On InternalFS both live on one FS**, so a full format wipes both, and `segmented_inbox_store.h:99` re-inits `epoch = 1`. If a *prior* epoch was also 1, the app would **silently miss** post-format messages (it keys re-pull on an epoch CHANGE).
- **Fix:** give `SegmentedInboxStore` a **fresh-epoch seed** used only on a fresh-init/format (add a ctor arg or `set_fresh_epoch(uint32_t)`; default `1` keeps the native tests deterministic). The device passes a **random** value (`mrrng::fill`, SD-safe) so a post-format epoch ≠ any prior epoch.
- A NORMAL reboot keeps `meta.epoch` (stable) ⇒ the app does **NOT** re-pull (records survived — that's the whole point). So `fw_main` must **drop the per-boot random `boot_epoch` override** for the durable store (`fw_main.cpp:641-642`): that override is only correct for the *volatile* store. The durable store's epoch comes from persisted meta, random-on-format only.

### 2.4 Wire it as the default (`fw_main.cpp`)
Restructure the inbox-store gate (`fw_main.cpp:86-92`):
```
#if defined(MRINBOX_QSPI_READY)        // future big QSPI store (Step 2 via app-flash makes this less needed)
   ... DeviceInboxStore ...
#elif defined(MR_INBOX_VOLATILE)        // opt-in: RAM ring (tests / no-persistence builds)
   FixedInboxStore<MR_RAM_INBOX_SLOTS> ...
#else                                    // DEFAULT: durable InternalFS
   InternalFsSegmentStore  g_seg_dm("idm"),  g_seg_ch("ich");
   InternalFsMetaStore     g_meta_dm("/idm.meta"), g_meta_ch("/ich.meta");
   SegmentedInboxStore     g_inbox_dm(g_seg_dm, g_meta_dm, inbox_internalfs_dm_bytes, 4096);
   SegmentedInboxStore     g_inbox_ch(g_seg_ch, g_meta_ch, inbox_internalfs_chan_bytes, 4096);
#endif
```
- Boot banner: `inbox = InternalFS durable, <N> seg/store`. Keep the volatile + QSPI banners under their flags.
- `g_node.inbox().on_init(&g_inbox_dm,&g_inbox_ch)` already gates on `begin()` (a mount failure ⇒ inbox disabled, inert — no crash). Unchanged.

## 3. STEP 1b — outgoing `ctr` persistence (correctness — the iOS-agent ask)

**Why:** the companion dedups DMs by **`(sender_hash, ctr)`**. The outgoing counter is `Node::_peer_send_counter` (`std::map<uint8_t,uint16_t>`, `node.h:696`, assigned by `next_ctr(dst)` `node.h:564`) — **per-dst, in RAM, volatile**. On reboot it restarts at 1, so a post-reboot `ctr=1` DM from this node **collides** with its pre-reboot `ctr=1` in the recipient's dedup ⇒ the recipient's app **silently drops** the new message. A durable inbox is pointless if the *sender's* ctr resets.

**Design (recommended — global monotonic floor, simplest + robust):**
- Persist ONE durable high-water `ctr_floor = max over dsts(next_ctr) + MARGIN` (e.g. `MARGIN=16`) to NV (a new `mrnv` field in the `/mrcfg` Blob — bump `kVersion`; OR a tiny `/mrctr` InternalFS file). **Skip-ahead** by `MARGIN` so a crash between persists never reuses a ctr.
- Batched persist: piggyback on the existing 30 s inbox-flush timer (`fw_main.cpp` loop) + change-detect (like `persist_join_if_changed`). NOT per-send (wear).
- Restore at boot (in `setup`, before any send): seed so `next_ctr(dst)` for an unseen dst starts at `ctr_floor` (Node needs a small `set_ctr_floor(uint16_t)` / restore API; `next_ctr` returns `max(_peer_send_counter[dst], floor)` then `++`).
- *Alternative (more faithful, more code):* persist the whole per-dst map (≤508 B). Preserves exact per-dst sequencing + 16-bit wrap headroom. Use only if the global floor's ctr-space skipping is a concern. **Recommend the global floor.**
- Native-test the floor logic (skip-ahead, restore, no-reuse-across-a-simulated-reboot); the NV write itself is bench.

## 4. STEP 2-of-this-spec — header trim (DM-density, do AFTER §2–§3 are green)

Firmware-internal on-flash record shrink (**no wire/contract change** — the companion JSON is unchanged): in `inbox.cpp` `serialize`/`deserialize`, drop the record's `seq` (it's duplicated in the segment frame `[u16 len][u32 seq]` — pass seq into `deserialize` from the frame), drop `kind` (the store knows it), shrink `rx_time` u64→u32 (node-uptime ms; 49-day wrap is fine). Header `24 → 15 B` ⇒ ~18% more DMs. Update `inbox_record_header_bytes`, the round-trip tests, and `test_segmented_inbox_store.cpp`. **Sequence it last** so the durable store lands on a verified format and the untestable flash change isn't bundled with a serialization change.

## 5. Test + bench plan (the `device_nv`/`device_rng` reality-split)

- **Native (must stay green):** the segmented logic + `fake_inbox_storage.h` already cover append/read/reboot-restore/drop-oldest/wipe/roll/torn-record. ADD: the fresh-epoch-on-format seed (epoch differs after a simulated format), the ctr-floor logic, and the header-trim round-trip. Both boards must build.
- **Bench (user-verified — I cannot exercise on-chip flash):**
  1. Send yourself N DMs → **reboot the node** → connect companion → `pull_inbox 0 0` → **the pre-reboot DMs are still there**.
  2. After reboot, send a DM to a peer you'd messaged before → the peer's app **shows it** (ctr did not collide).
  3. Fill past the DM cap → confirm coarse drop-oldest (oldest segment gone, newest intact) + seq still monotonic.
  4. Print InternalFS free space at boot → confirm the real block budget vs §1.
- **Acceptance:** #1 + #2 are the headline ("durable across reboot" + "no post-reboot dedup loss").

## 6. STEP 2 (FUTURE, separate effort) — grow into the application flash

**Captured per the 2026-06-12 decision.** The safe path above is bounded to ~28 KB InternalFS (~150–200 DMs). For **thousands** of DMs, grow InternalFS into spare APPLICATION flash:
- The app uses only ~37 % of its 792 KB region (xiao build: Flash ~37 %, ~299 KB). Dual-bank DFU needs ~2× the app image (~600 KB), leaving **~190 KB** carve-able without breaking OTA.
- **What changes:** the ldscript (`boards/nrf52840_s140_v7.ld` — shrink the app region's top) + the Adafruit InternalFS region size (`LFS_FLASH_TOTAL_SIZE` / page count) so the FS extends downward into the freed flash; then just raise `inbox_internalfs_dm_bytes` (more 4 KB segments). **The store code from Step 1 does NOT change** — only the cap + the flash layout. That's the payoff of the injected-HAL design: Step 2 is a config/layout change, not a rewrite.
- **RISK (why it's a separate, deliberately bench-verified step):** mis-sizing the regions can **brick DFU or corrupt NV** — the app, InternalFS (`/mrcfg`,`/mrid`,inbox), the bootloader, and dual-bank DFU all share the 1 MB flash. Required bench checks after the layout change: OTA DFU still completes, the bootloader still boots, `/mrcfg`+`/mrid` survive, and the enlarged inbox persists. Single-bank DFU (if ever adopted) frees more but removes the safe-rollback.
- **Trigger:** revisit if ~150–200 DMs proves too few in real use.

## 7. Notes for the coding agent

- Keep `Inbox`/`InboxStore` contracts unchanged — only ADD the InternalFS backends + the fresh-epoch seed + the ctr-floor API.
- The header-trim (§4) is the only thing touching the wire-adjacent serialization, and it's contract-neutral; do it last + re-run the golden round-trip tests.
- Do NOT silently truncate: keep the `static_assert(seg ≤ scratch)` (§2.2).
- Reality-split: mark every InternalFS-touching method as bench-verified-by-user in its header comment (mirror `device_nv.h` / `device_rng.h`); do not claim metal-verified from a native pass.
