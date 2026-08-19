<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 2 — the `/mrjoin` profile store + console handler · dispatch brief · 2026-08-19

**Status: DISPATCHED 2026-08-19. Slice 2 of 7.** ⛔ **Independently gated. Build slice 2 only — NO UI.**
★ Role split: the QA-gate wrote this; **the OWNER runs QG and rules.** ⛔⛔ **NO DEVICE CONTACT.**

## Read first
`CLAUDE.md` + `docs/CODE_GUIDELINES.md` · ★ **`docs/superpowers/specs/2026-08-18-ui15-provisioning-implementation-plan.md`
(v6) §3** — the contract. ⚠ Six revisions of corrections; **the latest wins**, withdrawn wording is kept visible and
⛔ must not be implemented. ⓘ Slice 1 (`src/firmware_join_service.h`) is **uncommitted in the tree** — build on it.

---
## 1 — What to build
**A NEW `mrnv` record, SEPARATE from `/mrcfg`** — §3.6.3: its corruption must not reset config, identity, team keys or
presets.
- ★ **Add a `Slot`** beside `kSlotCfg`/`kSlotId`/`kSlotPeers`/`kSlotFault` (`src/device_nv.h:212-216`).
  ★★ **Put it in the `"mr"` NAMESPACE — that is what makes the owner's factory-reset ruling free.** `factory_erase()`
  clears `"mr"` in one shot, and `/mrfault` sits in its **own** namespace precisely to survive it (`:209`). `/mrjoin`
  is **user configuration, not fault history** ⇒ it **must be deleted** by a factory reset ⇒ **`"mr"`**.
- **Own `kMagic`, own `kVersion`.** A mismatch ⇒ **reject the whole record**, ⛔ never a partial parse (the `/mrcfg`
  discipline, `:120-121`).
- **Layout, fixed four slots, no dynamic count:**
  `struct JoinProfile { uint8_t present; uint8_t layer; uint8_t routing_sf; uint8_t name_len; uint32_t freq_hz;
  uint32_t bw_hz; char name[12]; }`
  ★ **naturally 24 bytes ⇒ ⛔ DO NOT invent per-profile padding.** Any `reserved` belongs in the **record header**.
  **`static_assert(sizeof(JoinProfile) == 24)`** + one for the record. ★ **Zero the unused `name` bytes** — determinism
  is what makes coalescing meaningful.
- ★★ **`freq_HZ`, not kHz:** 869.4625 MHz is **869462.5 kHz** (not integral) but **exactly 869462500 Hz**.
- ⛔ **No `sf_list` in a profile** — team-PHY compatibility excludes it (`lib/core/node.h:302-305`, [[B211]]).

## 2 — Grammar (one shared console handler; the OLED will only *select*, later)
```
joinprofile list
joinprofile set <1..4> layer= freq=<MHz> bw=<kHz> sf= [name="…"]
joinprofile clear <1..4>
joinprofile reset confirm
```
- ★ **`freq=<MHz>` and `bw=<kHz>` — the SAME units the `join` verb takes.** Only storage is integral.
- ⛔ **A malformed or out-of-range index refuses loudly and writes NOTHING** (C2). ⛔ **A missing `confirm` on `reset`
  refuses without writing.**
- ★★ **Validation REUSES slice 1's `validate_join`** (`src/firmware_join_service.h:150`) — ⛔ **one authority, never a
  second range table** (U1).

## 3 — Absent vs corrupt (normative — the honesty requirement)
| verb | ABSENT | CORRUPT |
|---|---|---|
| `set` | ★ **seeds a valid empty four-slot record, applies the slot, ONE write** | ⛔ refuses (`PROFILE STORE INVALID`) |
| `list` | **`NO PROFILES`** (ordinary, non-alarming) | **`PROFILE STORE INVALID`** |
| `clear` | ★ **NO CHANGE, ZERO WRITES**, honest non-error | ⛔⛔ **MUST NOT recover** |
| `reset confirm` | ★ **already empty ⇒ ZERO WRITES**, honest non-error | ★ **the ONLY recovery path** |
⇒ ★ **Silent fallback would make corruption indistinguishable from a fresh device** — that is the whole point.
⇒ ⛔ **`clear` must never be a backdoor repair**: it would rewrite three slots it could not read.

## 4 — ⛔⛔ THE FOUR TRAPS
1. ★★ **NaN → UNDEFINED BEHAVIOUR ON CONVERSION, AND THIS IS NEW TO SLICE 2.** `validate_join` **accepts** `nan`
   (**[[B216]]**, deliberate at `firmware_config_parse.h:95-99`). Slice 1 could carry it because its request holds a
   `double`. **This record is INTEGRAL, and `static_cast<uint32_t>(NaN)` is UB.**
   ⇒ ★ **`joinprofile set` MUST REFUSE a non-finite freq/bw**, and that is **legitimate**: this is **new surface with
   no byte-identity obligation**, unlike the `join` verb. ⛔ **Do NOT "fix" B216 in the shared validator here** — that
   would change the existing `join` verb's behaviour inside a storage slice (C1). **Refuse at the profile boundary.**
2. ⛔⛔ **DO NOT ADD `/mrjoin` TO THE nRF52 CORRUPTION-PROBE LIST** (`src/device_nv.h:400-413`). `mount_or_repair()`
   recovers by calling **`InternalFS.format()`**, whose own comment records *"a reformat wipes `/mrid` too → the node
   re-mints its identity + loses its join"*. ⇒ listing an **optional** store there would make its corruption **destroy
   identity and config** — the exact opposite of §1. **Handle a failed/short/invalid read LOCALLY.**
3. **Byte-identical write coalescing**, as `/mrcfg` does (`:484-501`) — a re-`set` of identical values writes nothing.
4. ⛔ **No `/mrcfg` layout or `kVersion` change. No UI. No `Node` growth.**

## 5 — Pins
1. **Round-trip**: set → load → identical bytes, including **869.4625 MHz ⇒ `freq_hz == 869462500`** exactly.
2. **The absent/corrupt matrix (§3), all eight cells**, ★ with **write counts asserted** (`clear`/`reset` on absent =
   **0 writes**).
3. **Wrong magic · wrong version · wrong size ⇒ the whole record rejected**, ⛔ no partial parse.
4. **Slot index 0 and 5 refuse and write nothing**; `1` and `4` work (off-by-one both ends).
5. ★ **Units are not swapped** — a `bw=125` stores **125000**, a `freq=869.4625` stores **869462500**. ⓘ A mutation
   swapping kHz↔Hz must go RED.
6. ★★ **A non-finite freq/bw is REFUSED with zero writes** (trap 1).
7. **Re-setting identical values performs zero writes** (coalescing).
8. ⛔ **`clear` on a corrupt record does not repair it.**

**Control rule:** ★ defect-specific regressions must **FAIL** under a controlled mutation; ★ unchanged positive
controls stay **GREEN**. ⛔ Not "everything red".
ⓘ **Prefer counted/measured discriminators** — seventeen instruments in this arc were green against the defect they
were written to catch. **Assert write counts, not just verdicts.**

## 6 — ★ EVIDENCE MUST LAND IN THE REPO
⛔⛔ **Slice 1's byte-identity harness was produced in a scratchpad and is GONE.** ⇒ **any harness or measured evidence
this slice produces must be COMMITTED as a file under `tools/` or `test/`, not left in scratch.** A measurement that
cannot be re-run is a claim, not evidence.

## 7 — Gate
Baselines: native **1733 / 84164 / 0** · `probe_prov_tx` 19/19 + 40 RED · `probe_board_ui` 120/120 + 14/14 + 52/52 +
**154** RED · `probe_firmware_ui` 229/229 · `probe_console_sink` PASS · census **174 / 178 / 178**, `-Wswitch` 0 ·
`lus` `b77cfd3d` · s18 **`9868cad3` / 269905** · `sizeof(Node)` **221880**.
1. `pio test -e native`, **then RUN the binary**.
2. All four probes with control sets.
3. ✅ **`warning_census.sh` — RUN IT** (owner-ruled, re-confirmed, **not** capped by the two-env limit).
4. **Four-step corpus proof.** ⛔ A moved row ⇒ **STOP AND REPORT**.
5. ★★ **TWO ENVS ONLY.** `sizeof(Node)` from a **compile-only `static_assert` probe**. ⚠ [[B206]]: both arms in the
   **SAME directory**; a `/tmp` worktree is **invalid** for flash measurement; deltas under ~32 B are noise.
6. **D2:** `sizeof(Node)` 221880, `kCap` 91, `git diff -- lib/` **empty**.

**Report:** the record + `Slot` + namespace choice with `file:line` · the `static_assert`s · the grammar · **the
absent/corrupt matrix with write counts** · each pin with its control and match count · native → after · four probes ·
census · four-step · two envs · D2 · **where the evidence file landed** · exact final `git status --short` · and the
M1/M2 text you owe (⇒ `/mrjoin` power-cut is **slice 7**, ⛔ not owed here).
⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ **Do not touch** the bug register, the bench script, any
plan/brief, or `tracker.md`.
⛔ **Stop and report** if the factory-reset behaviour cannot be had from the namespace choice, if a corpus row moves,
or if `sizeof(Node)` moves.
