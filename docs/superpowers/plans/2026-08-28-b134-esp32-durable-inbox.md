<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B134 — durable ESP32/Heltec inbox storage · dispatch brief · 2026-08-28

**Status: DISPATCHED (the owner-ruled pipeline, after B159 ✅).** Authority: the register row **[[B134]]**
(`docs/2026-07-30-open-bug-register.md` — "Provide durable ESP32/Heltec inbox storage; deleted records can
return after reboot"; ⓘ the row's wording predates the 2026-08-13 sharpening — the CURRENT truth is worse:
on every ESP32 target the inbox is a VOLATILE RAM ring, so a reboot destroys records, tombstones and the
whole history alike; nothing "returns" because nothing survives). Context you MUST read first: the OLED spec's
§UI-7D AS-BUILT + B134 sharpening blocks (`docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`
around :1130-1180) — the platform-neutral `Inbox::erase` contract, the durable-backend acceptance criteria
(power-loss atomicity · deletion survives reboot · seq/cursor/epoch meanings · bounded tombstones), and the
nRF52 precedent (`src/device_inbox_store.h`, the QSPI/CustomLFS segmented store, B133/B135/B136 all closed).
⛔ NO DEVICE CONTACT. ⛔ custody Slice A is the NEXT stage, separate.

## The slice
Give the ESP32/Heltec targets a DURABLE `InboxStore` backend satisfying the SAME contract the segmented
store already discharges natively — this is a backend slice, not a redesign:
- **Phase 0 (present the exact state first):** inventory the store backends and which env selects which
  (`FixedInboxStore` / segmented / `DeviceInboxStore` — file:line per env); how nRF52 mounts its FS and how
  the ESP32 side currently persists ANYTHING (`device_nv.h` — NVS/Preferences? is LittleFS already mounted
  anywhere?); the `storage_epoch`/`persisted_next_seq`/read-cursor semantics on boot; the write-path hot
  sections (the USB-CDC/radio-critical lessons — no store write may block a radio-critical interval).
- The backend: prefer the ESP32 Arduino core's OWN filesystem (LittleFS is built in — verify in-tree; ⛔ if
  a new library/platformio.ini change is genuinely unavoidable, STOP and report, don't add it) hosting the
  EXISTING segmented append-only ring logic (U1 — reuse the segmented store; a thin ESP32 file/partition
  seam, not a parallel store implementation). The `Inbox::erase` tombstone contract is already
  backend-neutral — prove it holds unchanged.
- Boot semantics change deliberately: `storage_epoch` no longer fresh every boot on ESP32; records and
  tombstones survive; ⚠ sweep every consumer that ASSUMES the volatile behaviour (the UI-7D bench 19.1
  "n/a, volatile store" qualification, `pull_inbox`/companion cursor logic, any "fresh epoch every boot"
  comment) — list them; doc corrections DRAFTED for the supervisor.
- Corruption/power-cut: the established fail-loud-then-recover idiom (the InternalFS lesson: detect-on-mount
  → format-on-corrupt → never boot-brick; B135's mid-frame tear class) applied to the ESP32 FS; wear:
  coalesced writes, no per-message sync storm — measure the write amplification and state it.

## Gate
- Native: the existing segmented-store cases must pass UNCHANGED against the reused logic; new cases for the
  ESP32 seam's decision logic (pure-header idiom if the glue isn't host-linked — CHECK first, lib/hal links
  into native per B159's finding); the B135-style power-cut injection re-aimed at the new seam; mutations
  into the isolated harness (torn-write guard dropped · epoch persistence dropped · tombstone cap ignored ·
  wear-coalescing removed), each RED at match 1, full pass per touched target, PIN derivation written.
- Corpus: state the inertness argument precisely (which files the sim compiles — if lib/core is untouched,
  inert by construction; if `inbox.cpp`/contract files move, predict + A/B). s18 keystone from
  `simulation/BASELINE.md`. Boards: the ruled pair `gateway` + `heltec_mobile`; gateway expected
  BEHAVIOUR-UNCHANGED (state the guard — the nRF52 path must not move); heltec_mobile RAM/flash measured and
  attributed (the certified runner if the movement is material); ABI probe re-pin for any touched pinned
  struct. Warning census; `git diff --check`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ No docs (spec/bench/register drafts go in the REPORT),
  no `tracker.md`, no `platformio.ini`, no parallel-session files. No pollers; never pipe the runner.
- Metal residue is REAL here (M2): real flash wear, a real power cycle proving records+deletions survive,
  reflash-wipes-once expected — DRAFT the exact bench steps with expected console lines.

## Report
Phase 0 inventory (file:line) · the backend design + reuse boundary (what is the segmented store's, what is
the new seam's) · boot-semantics consumers swept · power-cut/corruption evidence · mutation ledger + native
PIN · corpus argument/result · board measurements attributed · DRAFTED doc corrections + M2 bench steps ·
exact final `git status --short`.
