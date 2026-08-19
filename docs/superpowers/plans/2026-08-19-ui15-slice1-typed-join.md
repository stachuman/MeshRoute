<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 1 — extract a TYPED STATIC-JOIN TRANSACTION · dispatch brief · 2026-08-19

**Status: DISPATCHED 2026-08-19. Slice 1 of 7** (see the plan's §9). ⛔ **Independently gated. Build slice 1 only.**
★ Role split: the QA-gate wrote this; **the OWNER runs QG and rules.** ⛔⛔ **NO DEVICE CONTACT.**

## Read first
`CLAUDE.md` + `docs/CODE_GUIDELINES.md` · ★ **`docs/superpowers/specs/2026-08-18-ui15-provisioning-implementation-plan.md`
(v6 — ⛔ NOT v5; v6 corrected the `bw` field after THIS slice measured that v5's `uint32_t bw_hz` broke byte-identity) §2.2, §9, §10** — the contract. ⚠ It carries five rounds of corrections; **the latest wins**.

---
## 1 — ★★ THIS IS A REFACTOR (C1). ITS ONE HARD PROPERTY IS BYTE-IDENTICAL OUTPUT.
`handle_join` (`src/firmware_config.cpp:769`, `#if MR_N_LAYERS < 2`) as built:
parse (4 required keys + `phy_args_in_range`) → `nv_load_stamped(b)` → set `freq_mhz` / `bw_hz` / `routing_sf` /
`leaf_id = layer & 0x0F` / **`layer0_id = layer` (full byte)**, zero `node_id`/`joined`/`lineage_id`/`config_epoch`/
`leaf_name_len` → **`mrnv::save(b)`** (fail ⇒ `> join err nv_save_failed`) → **`provision_apply_live(b, true)`** →
`write_join_started(...)`.
⇒ **Move that decision logic into a typed transaction. ⛔ Change no observable byte.**

## 2 — What to build
- **`JoinRequest { uint8_t layer; double freq_mhz; double bw_khz; uint8_t routing_sf; }`** ⛔ **CORRECTED: this brief said `uint32_t bw_hz`; measurement proved it breaks byte-identity on `bw=nan` (see plan v6 §2.2).**
  ⛔⛔ **`double freq_mhz`, NOT integer kHz** — **869.4625 MHz is 869462.5 kHz**, so an integer would round it and
  **change the radio**. The transient request must preserve the existing parser exactly.
- **A typed `JoinErr`** with arms for **invalid layer · invalid freq · invalid bw · invalid sf · load failure · save
  failure**, plus the synchronous verdict (`started` / `refused` / `nv_failed`).
  ⛔ **`ProvErr` is the TEAM vocabulary — do not press it into service here.**
- ★★ **An injected live seam**, because a natively-tested transaction cannot call device-only code:
  ```
  struct IJoinLive { virtual void apply_and_start(const mrnv::Blob&) = 0; };
  ```
  The device implementation delegates to the existing **`provision_apply_live(blob, /*do_dad=*/true)`**.
- Shared **validation + candidate construction**; **ONE save**; then the live seam.
- **`handle_join` routes through it** and remains the only console entry.

⛔ **OUT:** any OLED code · `/mrjoin` · the fingerprint · `handle_create`/`handle_team` · wire/NV/`kVersion` change.

## 3 — ⚠ THE AUDIT ITEM — REGISTER, ⛔ DO NOT FIX HERE (owner disposition 2026-08-19)
`reset_join_for_reprovision()` appears to cancel **only the claim guard**, not the old listen/retry timers
(`lib/core/node_join.cpp:531`). ⇒ **audit it and, if it is a defect, REGISTER it (M1) with evidence — then STOP.**
⛔ **Fixing it here would destroy this slice's byte-identity claim, which is the only thing making it a refactor.**

## 4 — Pins
1. ★★ **BYTE-IDENTICAL OUTPUT — and prove it, do not assert it:** for each of a valid join, each refusal arm, and the
   save-failure arm, the emitted console text **and** the `join_started` JSON must be **unchanged**. ⓘ Suggested proof:
   capture the literal set in `handle_join` plus the exact field values handed to `write_join_started` **before and
   after**, and diff. ⛔ "It still compiles" is not proof.
2. **Fakes pin the ordering** — ★ **zero** `IJoinLive` calls on validation / load / save failure · **exactly one**
   after a successful save · **save-before-live** ordering.
3. **`layer0_id` keeps the FULL byte and `leaf_id` the nibble** — ⛔ the transaction must not "tidy" that; a later
   slice's correlation rule depends on the distinction.
4. **Each `JoinErr` arm is reachable and distinct** (⛔ not a single generic refusal).

**Control rule:** ★ defect-specific regressions must **FAIL** against a controlled mutation; ★ unchanged positive
controls stay **GREEN**. ⛔ Not "everything must go red".
ⓘ **Prefer counted/measured discriminators** — sixteen instruments in this arc were green against the defect they were
written to catch.

## 5 — Gate
Baselines: native **1722 / 84041 / 0** · `probe_prov_tx` 19/19 + 40 RED · `probe_board_ui` 120/120 + 14/14 + 52/52 +
153 RED · `probe_firmware_ui` 229/229 · `probe_console_sink` PASS · census **174 / 178 / 178**, `-Wswitch` 0 · `lus`
`b77cfd3d` · s18 **`9868cad3` / 269905** · `sizeof(Node)` **221880**.
1. `pio test -e native`, **then RUN `./.pio/build/native/program`**.
2. All four probes with control sets.
3. ✅ **`warning_census.sh` — RUN IT** (owner-ruled and re-confirmed: **not** capped by the two-env limit; ⛔ not to be
   re-litigated).
4. **Four-step corpus proof.** ⛔ If a row moves, **STOP AND REPORT**; do not re-anchor.
5. ★★ **TWO ENVS ONLY.** ⛔ Not pre-authorised to escalate. `sizeof(Node)` from a **compile-only `static_assert`
   probe**. ⚠ [[B206]]: both arms in the **SAME directory** (a `/tmp` worktree shifts flash ~80 B and is **invalid**
   for measurement); deltas under ~32 B are noise.
6. **D2:** `sizeof(Node)` 221880, `kCap` 91, `git diff -- lib/` **empty** unless the audit requires a read-only look.

**Report:** the transaction with `file:line` · the `JoinErr` arms · the `IJoinLive` seam and its fake · **the
byte-identity proof and its method** · the audit result (registered or clean) · each pin with its control and match
count · native → after · four probes · census · four-step · two envs · D2 · exact final `git status --short`
confirming nothing was committed · and the M1/M2 text you owe.
⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ **Do not touch** the bug register, the bench script, any
plan or brief, or `tracker.md` — report owed text instead.
⛔ **Stop and report** if byte-identity cannot be preserved, if a corpus row moves, or if `sizeof(Node)` moves.
