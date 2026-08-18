<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B211 — preserve and report `sf_list` on a team PHY tail · dispatch brief · 2026-08-18

**Status: DISPATCHED 2026-08-18. Slice 2 of 4** (1 = [[B209]] ✅ done · **2 = this** · 3 = [[B210]] · 4 = [[B212]]).
⛔ **Each micro-slice is INDEPENDENTLY GATED. Build slice 2 only.**
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; **the OWNER runs QG and rules.**

⛔⛔ **NO DEVICE CONTACT** — no upload, no flashing, no serial. Native tests, host probes and `pio run` builds only.

★★ **OWNER RULING 2026-08-18 (reported form, ⛔ not quoted) — APPROVED, and it supersedes this brief's earlier
"QG recommendation, not a ruling" note:** **a team PHY tail PRESERVES the persisted `allowed_sf_bitmap`;
`mobile register` remains UNCHANGED; and successful team-PHY output REPORTS the resolved `sf_list`.**
⇒ §3.1, §3.2 and §3.3 are all now normative.

## Read first
1. `CLAUDE.md` + `docs/CODE_GUIDELINES.md` (U1-U3, V1-V2, C1-C4, D1-D4, M1-M3).
2. ★ The **B211** row of `docs/2026-07-30-open-bug-register.md` — the metal-confirmed defect and the policy evidence.
3. `docs/superpowers/specs/2026-08-17-team-provisioning-transaction-design.md` — ⚠ carries `CORRECTED v2/v3/v4`
   markers and collapsed `<details>` blocks of **withdrawn** wording; ⛔ never implement anything withdrawn.

---
## 1 — The defect
`parse_phy_tail` (`src/firmware_config.cpp:879-881`) builds a fresh `LayerConfig{}` and sets
`phy.allowed_sf_bitmap = (uint16_t)(1u << pa.sf)` — so `sf=` (the **routing/control** SF) **also collapses the DATA SF
set** to that one value. **Metal-confirmed 2026-08-18:** a node booted `data sf = 6,7` came back `data sf = 7` **and
the collapse survived a power-cycle** (it rides the candidate into NV).

★★ **The project's own policy contradicts it.** `lib/core/node.h:302-305` defines team-PHY compatibility as
*"freq/bw/routing_sf/cr; NOT layer_id … **NOT sf_list — F-SF-1 keeps that across registration**"*. ⇒ **team coherence
does NOT require a common `sf_list`**, so the collapse buys nothing and costs DATA-SF diversity.
⛔ An earlier defence of the collapse ("members must agree on sf_list") is **withdrawn as refuted by that line**.

---
## 2 — ★★ THE ONE FACT THAT SHAPES THE FIX
**`parse_phy_tail` is SHARED.** The team path calls it at `src/firmware_config.cpp:1333`; **`handle_mobile` calls the
same function at `:1432`.** ⇒ ⛔ **DO NOT change `parse_phy_tail`** — that would globally alter `mobile register`
semantics, which QG explicitly excluded. **The preservation belongs downstream, on the provisioning path only.**

---
## 3 — What to build

### 3.1 — Stop propagating the collapsed bitmap (team path only)
`src/firmware_config.cpp:1339` currently copies `rq.phy.allowed_sf_bitmap = phy.allowed_sf_bitmap;`.
⇒ **the team request must carry "not specified"** instead. ★ **`0` is a safe sentinel and needs no new field:** an
empty `sf_list` blocks DATA entirely and is already refused (`ProvErr::incomplete_phy`,
`src/firmware_provisioning_service.h:522`, requires non-zero), and the `data_sf`-removed ruling makes an empty set
illegal — **so `0` can never be a legitimate request value.** ⓘ If you prefer an explicit `sf_list_present` flag beside
`phy.present`, that is acceptable — **say which you chose and why**.

### 3.2 — ⛔⛔ RESOLVE IT *BEFORE* THE COMPARISONS — THIS IS THE REGRESSION RISK
The service must fill an unspecified bitmap from the **stored record** while composing the candidate, and it must do so
**before** either comparison runs:
- `differs` — `src/firmware_provisioning_service.h:507`
- `live_phy_matches` — `:331` (`snap.live_allowed_sf_bitmap == phy.allowed_sf_bitmap`)

★★ **If the raw `0` reaches those comparisons, `no_change` BREAKS:** a same-PHY re-apply would compare live/record
against `0`, look like a change, and **apply forever**. That is a direct regression of **bench 27.8/27.9, which QG has
already passed on metal** — so pin it (§5).

### 3.3 — Report the resolved `sf_list` — ★ **REUSING THE EXISTING FORMATTER (U1/U2)**
`src/firmware_config.cpp:1392-1394` prints `freq` / `sf` / `bw` only. ⇒ **add the RESOLVED `sf_list`**, so the
confirmation can no longer alter a fourth field it never mentions (the C2 objection that stands independently of the
routing question). Carry the resolved value out on the result so the printer reports **what actually landed**, not what
was asked for.

⛔⛔ **CALL THE EXISTING FORMATTER — DO NOT WRITE A THIRD BITMAP-TO-TEXT IMPLEMENTATION (QG).**
Use **`mrfw::print_sf_list(out, res.phy.allowed_sf_bitmap)`** — defined at `src/firmware_commands.cpp:250`, declared
in **`src/firmware_commands.h:46`**, so it is already reachable from `firmware_config.cpp` via that header. It emits
comma-separated values with no spaces (`6,7`) and `-` for an empty set, and it is the same formatter `dump_cfg`
(`:264`) and the boot banner (`src/fw_main.cpp:806`) use.
ⓘ **Its own comment is the reason this matters** (`:247-249`): it records a past defect where a formatter reached past
the sink it was given, and states *"No global-writing overload is kept: the compiler must break every future call site
that forgets the sink."* ⇒ **pass `out`; never a global.**

★ **PIN THE EXACT LINE**, e.g.:
```
> team PHY: freq=869.463 sf=7 bw=125.00 kHz sf_list=6,7
```
(`freq` at 3 decimals, `bw` at 2, per the existing prints.)

### 3.4 — Scope
⛔ **OUT:** `parse_phy_tail` · `handle_mobile` / `mobile register` semantics · [[B210]] DAD reporting ·
[[B212]] validation/reporting · any wire, NV or `kVersion` change · `Node` growth.

---
## 4 — Traps
- ⛔ Changing the shared parser (§2) — the single most likely wrong turn.
- ⛔ Resolving after the comparisons (§3.2) — breaks `no_change` and regresses a metal-passed check.
- ⚠ The incomplete-PHY refusal (`:522`) must still fire for a genuinely empty resolved bitmap — **resolution must not
  turn a real \"no DATA SF\" configuration into a silent pass.**
- ⛔ `sizeof(Node)` stays **221880**; `kCap` 91; no timer allocated.

---
## 5 — Pins
1. **`team <id> freq=… sf=… bw=…` PRESERVES the existing multi-SF `sf_list`** (e.g. `{6,7}` stays `{6,7}` while
   `routing_sf` becomes 7) — and it **persists** that way.
   ⛔⛔ **1b — MANDATORY, BECAUSE PIN 1 ALONE PASSES IF THE CODER READS THE *LIVE* BITMAP INSTEAD OF THE STORED ONE
   (QG).** Resolution must use the **persisted record**. Add a **divergence case**:
   | | |
   |---|---|
   | stored `sf_list` | **{6,7}** |
   | live `sf_list` | **{7}** |
   | request | names routing SF **7** |
   | required | the **candidate**, the **result** and the **applied PHY** all use **{6,7}** |
   ★ **Plus a mutation replacing the stored value with `snap.live_allowed_sf_bitmap` — it MUST turn RED.**
   ⓘ **The fixture is realistic, not contrived:** `mobile register sf=7` collapses the LIVE bitmap and does **not**
   persist, so live/persisted divergence is already the real [[B207]] case that bench 27.8 exercises.
2. ★★ **`no_change` still works after the change:** with record ≡ live ≡ request, a repeat reports `no_change`
   (0 saves, 0 applies). **This is the 27.8/27.9 regression guard — it must exist.**
3. **The confirmation line reports the resulting `sf_list`.**
4. **`mobile register freq=… sf=…` is UNCHANGED** — *unchanged positive control*.
5. **A genuinely empty resolved bitmap is still refused** (`incomplete_phy`).

**Control rule:** ★ **defect-specific regressions must FAIL** against the current implementation or under a controlled
mutation; ★ **unchanged positive controls (pin 4) must stay GREEN** — a positive control going red is a regression, not
a proof. ⛔ Do **not** try to make pin 4 fail.
⛔ Ask of every check: *could it have come out otherwise?* Print match counts; restore sources and md5-verify.
ⓘ This project has recorded **eleven** instruments that were green against the defect they were written to catch — the
most recent was pin 3 of [[B209]], where `home_desired`/`attach_state` passed in **both** arms and only a **counter**
discriminated. **Prefer a counted/measured discriminator over a state assertion.**

---
## 6 — Gate
Baselines: native **1716 / 83966 / 0** · `probe_prov_tx` **13/13 + 18 controls RED** · `probe_board_ui` 120/120 +
14/14 + 52/52 + **153** RED · `probe_firmware_ui` 229/229 · census **174 / 178 / 178**, `-Wswitch` 0 · `lus`
`b77cfd3d` · s18 keystone **`9868cad3` / 269905** · `sizeof(Node)` **221880**.

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (the wrapper falsely prints "0 test cases").
2. `probe_prov_tx` + both UI probes, with control sets.
3. `warning_census.sh` at its pins — ✅ **RULED 2026-08-18 (owner): the census STAYS AS IS.** ⇒ its three pinned
   OLED builds are **NOT capped by the two-env limit**; the cap governs the RAM/flash board sweep, not this
   warnings instrument (warnings are gate-blocking here). ⓘ On this slice it ran and **PASSED** — 174 / 178 / 178,
   `-Wswitch` 0, 326 objs/env.
4. **Four-step simulator proof.** ⓘ This slice is **`src/`-only ⇒ corpus-inert BY CONSTRUCTION** (`git diff -- lib/`
   must be empty) — but **prove it**; ⛔ no anchor-table edit, keystone read from `simulation/BASELINE.md`.
5. ★★ **BOARD BUILDS: TWO ENVS ONLY — standing owner ruling 2026-08-18.** ⛔ **This brief does NOT pre-authorise more,
   and you should push back on any instruction that does.** Use the pair the owner has named; if none is named, take
   **one nRF52 and one ESP32-S3** and say which. ⓘ `sizeof(Node)` comes from a **compile-only `static_assert` probe**,
   not a build sweep. ⚠ [[B206]]: build **both arms in the SAME directory** (a `/tmp` worktree's longer path was
   measured to shift flash by ~80 B), and treat any delta under ~32 B as noise.
6. **D2:** `sizeof(Node)` 221880, `kCap` 91. ⛔ **CORRECTED (QG): do NOT require `git diff -- lib/` to be EMPTY** —
   the accumulated **[[B209]]** slice legitimately modifies `lib/core/node.h` in this same working tree. ⇒ the correct
   assertion is: **"B211 introduces no additional `lib/` changes; the existing B209 delta remains."** Verify by
   attribution (the `lib/` diff contains only B209's `mobile_retune_phy` hunks), not by emptiness.

**Report:** the request-side change with `file:line` and which sentinel you chose · where resolution happens and the
proof it precedes **both** comparisons · the report-line change · each pin with its control and match count · native
baseline → after · probes · census · the four-step result · the two envs with RAM/flash · the D2 answer · exact final
`git status --short` confirming nothing was committed · and the M1/M2 text you owe.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ **Do not touch** the bug register, the bench script, the
B207 spec, either B209/B211 brief, or `tracker.md` — report owed M1/M2 text instead.
⛔ **Stop and report** rather than improvising if: `no_change` cannot be preserved · the fix cannot be made without
touching `parse_phy_tail` · a corpus row moves · or `sizeof(Node)` moves.
