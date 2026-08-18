<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B210 — the `team-DAD:` line must reflect whether DAD actually ran · dispatch brief · 2026-08-18

**Status: DISPATCHED 2026-08-18. Slice 3 of 4** (1 = [[B209]] ✅ · 2 = [[B211]] ✅ · **3 = this** · 4 = [[B212]]).
⛔ **Independently gated. Build slice 3 only.**
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; **the OWNER runs QG and rules.**
⛔⛔ **NO DEVICE CONTACT.** Native, host probes and `pio run` builds only.

⚠ **The tree carries slices 1 and 2 UNCOMMITTED and QG-passed.** Build on them; ⛔ do not revert or re-derive.

## Read first
`CLAUDE.md` + `docs/CODE_GUIDELINES.md` · the **B210** row of `docs/2026-07-30-open-bug-register.md` (the metal
reproduction and the fix shape) · the B207 spec ⚠ (`CORRECTED v2/v3/v4` markers and collapsed `<details>` blocks of
**withdrawn** wording — ⛔ never implement anything withdrawn).

---
## 1 — The defect, and why it is nearly free to fix
`src/firmware_config.cpp:1435` prints the line whenever the applied result is a mobile in a team:
```cpp
if (res.team_id != 0 && g_node.config().is_mobile) { out.print(F("  team-DAD: local_id=")); … }
```
⛔ It **never consults whether DAD ran**, so it reads as an airtime claim on every applied team command.
**Metal-confirmed** (bench 27.5/27.8): printed on same-team re-applies where membership never changed, the id never
moved, and no beacon burst followed.

★★ **The truth is ALREADY computed, carried out, and tested — the print site is the only thing ignoring it:**
`ProvResult::dad_fired` (`src/firmware_provisioning_service.h:262`) is set **only** inside
`if (plan.fire_dad) { _live.fire_dad(); r.dad_fired = true; }` (`:716`), and `plan.fire_dad` is
`membership_changed && team_id != 0 && projected_is_mobile` (`:619`). It is already asserted natively at
`test/test_firmware_provisioning_service.cpp:295/342/1222`. ⇒ **this slice is one gate expression.**

---
## 2 — What to build
**Gate the line on `res.dad_fired`** (owner/QG-specified wording: *print `team-DAD` only when `res.dad_fired` is
true, not merely because the applied result is a mobile team member*).

ⓘ **The register also recorded a second option — relabelling the line so it cannot be read as a claim.
⛔ NOT CHOSEN. Do not implement it, and do not blend the two.**

⚠ **Accept the consequence and do not paper over it:** on a same-team apply the operator no longer sees their
`local_id` on that line. That is intended — it is available from `cfg` / `status`, and a line that says `team-DAD`
must mean DAD. ⛔ **Do not add a substitute "current local id" line to compensate**; that is a new feature, unruled.

### ⛔⛔ THE ONE TRAP
The line prints **`g_node.team_local_id()`** — read **LIVE**, which is correct: when DAD has just fired, the live id
is the fresh one.
★ **`res.persisted_team_local_id` is NOT a substitute and swapping to it is a silent defect:** it carries the
**candidate's** value, which is **`0` on exactly the membership-change case where this line now prints** (§B207
design v2 — `team_local_id = 0` means DAD-pending). Using it would print `local_id=0` every time.
⇒ **keep the live read; change only the condition.**

---
## 3 — Pins
1. ★ **a genuine membership change (`team 0` → `team <id>`, or `team new`) PRINTS the line** — positive case.
2. ★★ **a same-team re-apply (re-key, PHY-only, or bare) does NOT print it** — the defect case.
3. **the printed id is the LIVE `team_local_id`**, not the candidate's 0 (guards the trap above).
4. **`no_change` and every refusal print nothing** — they already return before this site (`:1376`); confirm, don't assume.

⚠ **Where the coverage has to live:** `src/firmware_config.cpp` is **outside the native suite**
(`test/test_firmware_config_parse.cpp:277`), so pins 1-3 are **structural** — extend `tools/probe_prov_tx` (it already
reads this adapter and its report block). Native already pins `dad_fired` itself; ⛔ do not duplicate that.
**Controls that must go RED:** the old gate restored · the gate deleted (always prints) · gated on `res.team_id != 0`
alone · the id source swapped to `res.persisted_team_local_id`.

**Control rule:** ★ defect-specific regressions must **FAIL** against the current implementation or under a controlled
mutation; ★ unchanged positive controls must stay **GREEN**. ⛔ Not "everything must go red".
ⓘ **Prefer a counted/measured discriminator over a state assertion** — twelve instruments in this arc were green
against the defect they were written to catch; the last two slices were saved by counts (B209's timer arms, B211's
preserved-field count). Here the natural discriminator is **presence/absence of the literal**, so assert the
occurrence count, not merely "it compiled".

---
## 4 — Gate
Baselines: native **1720 / 83996 / 0** · `probe_prov_tx` **16/16 + 25 controls RED** · `probe_board_ui` 120/120 +
14/14 + 52/52 + **153** RED · `probe_firmware_ui` 229/229 · census **174 / 178 / 178**, `-Wswitch` 0 · `lus`
`b77cfd3d` · s18 keystone **`9868cad3` / 269905** · `sizeof(Node)` **221880**.

1. `pio test -e native`, **then RUN `./.pio/build/native/program`**.
2. `probe_prov_tx` + both UI probes with control sets.
3. ✅ **`warning_census.sh` at its pins — RUN IT. Owner-ruled 2026-08-18: the census STAYS AS IS and its three pinned
   OLED builds are NOT capped by the two-env limit.**
4. **Four-step simulator proof.** `src/`-only ⇒ inert by construction — **prove it**; ⛔ no anchor-table edit.
5. ★★ **BOARD BUILDS: TWO ENVS ONLY** (standing ruling). ⛔ This brief does not pre-authorise more. `sizeof(Node)` from
   a **compile-only `static_assert` probe**. ⚠ [[B206]]: both arms in the **same directory**; deltas under ~32 B are noise.
6. **D2:** `sizeof(Node)` 221880, `kCap` 91. ⛔ **Do NOT require `git diff -- lib/` to be empty** — slices 1's
   `node.h` delta is legitimately present. Assert instead: **"B210 introduces no additional `lib/` changes."**

**Report:** the gate expression with `file:line` · confirmation the live read was kept and why · each pin with its
control and match count · native baseline → after · probes · census · the four-step result · the two envs · the D2
answer · exact final `git status --short` confirming nothing was committed · and the M1/M2 text you owe.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ **Do not touch** the bug register, the bench script, the
B207 spec, any brief, or `tracker.md` — report owed M1/M2 text instead.
⛔ **Stop and report** if the line cannot be gated without changing the transaction, or if a corpus row moves.
