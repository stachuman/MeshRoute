<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B212 — the specific `team 0` refusal must win, and errors must name the right verb · dispatch brief · 2026-08-18

**Status: DISPATCHED 2026-08-18. Slice 4 of 4 — the LAST of the [[B207]] follow-up package**
(1 = [[B209]] ✅ · 2 = [[B211]] ✅ · 3 = [[B210]] ✅ · **4 = this**). ⛔ **Independently gated. Build slice 4 only.**
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; **the OWNER runs QG and rules.**
⛔⛔ **NO DEVICE CONTACT.** Native, host probes and `pio run` builds only.

⛔ **CORRECTED 2026-08-18: slices 1-3 are now COMMITTED in `6281fc2` ("teams — bug fixing").** This brief was
written when they were uncommitted at `fc89e14`. ⇒ **the B212 worktree carries NO `lib/` changes at all**, and the
gate's `lib/` assertion (§5.6) is simply *"B212 introduces no `lib/` changes"* — ⛔ there is no accumulated delta to
excuse. ⓘ State HEAD as *observed at dispatch*, never as fact; the coder correctly verified rather than trusting it.

## Read first
`CLAUDE.md` + `docs/CODE_GUIDELINES.md` · the **B212** row of `docs/2026-07-30-open-bug-register.md` · the B207 spec
⚠ (`CORRECTED v2/v3/v4` markers + collapsed `<details>` blocks of **withdrawn** wording — ⛔ never implement those).

---
## 1 — The defect, in two halves
**(a) The parser MASKS the specific refusal.** ⛔⛔ **CORRECTED 2026-08-18 (QG) — this brief previously claimed
`phy_args_in_range` "requires freq AND bw AND sf all valid". THAT IS FALSE, and the source comment at
`src/firmware_config.cpp:881` said so all along: *"sf is REQUIRED with freq (0 fails the 5..12 check); bw is
OPTIONAL, default 125 kHz"*.** ★ **The accurate mechanism:** `parse_phy_tail` DEFAULTS an omitted `bw=` to 125 kHz (`src/firmware_config.cpp:881`). So `freq=868` fails because **`sf` remains invalid**, while `sf=7` or `bw=125` fails EARLIER at `!pa.has_freq` (`:889`) because **`freq=` is absent**. **Two different parser failures, both masking the more specific leave refusal.** Either way `handle_team` returns and
**the request never reaches the transaction**, so the accurate refusal never runs. Metal-confirmed: `team 0 freq=868`
answered *"> team new err: freq 100..1000 MHz…"*.

**(b) The prefix names the wrong subcommand.** `src/firmware_config.cpp:1335` hardcodes `"> team new err: …"` inside
the `PhyTailMsgs` shared by **all three** forms (`team new`, `team <id>`, `team 0`).
★ **The inconsistency is visible inside the same initialiser** — `:1333` and `:1334` already say `"> team err"`; only
the third diverges.

---
## 2 — ★★ THE SERVICE IS ALREADY CORRECT — DO NOT TOUCH IT
`validate` refuses on leave **first**: `key_on_leave` (`src/firmware_provisioning_service.h:392`) then
**`phy_on_leave` (`:393`)** — *before* role, id, projection and the PHY-validity check at `:557`. The message text
already exists in the verdict reporter (`src/firmware_config.cpp:1162`).
⇒ **Nothing in the service or the message set needs adding. The whole fix is in `handle_team`: get the request TO the
transaction instead of dying in the parser.**

---
## 3 — What to build
1. ★★ **EXTRACT A PURE TAIL CLASSIFIER into `src/firmware_config_parse.h`** (QG-specified shape), **reusing
   `kv_next` (`:55`) and `phy_arg_take` (`:86`)** — ⛔ never a hand-written `strcmp("freq")…` set, which would be a
   second definition of "what is a PHY key" and would drift. It returns three cases:
   | result | meaning |
   |---|---|
   | **`none`** | no tokens at all |
   | **`phy_only`** | every token is a recognised PHY key |
   | **`invalid_or_mixed`** | at least one token is not a recognised PHY key |
   ★ **Why a pure header: it is NATIVELY TESTABLE.** That is what makes the behavioural pins provable (§4/§B4.1).
2. ⛔⛔ **MIXED-TAIL PRECEDENCE — SPECIFIED, because "any PHY token ⇒ skip parsing" was UNDER-SPECIFIED (QG) and would
   have made `team 0 freq=868 wibble=3` silently ignore `wibble=3`:**
   - **`phy_only`** ⇒ set `rq.phy.present = true`; the transaction answers `phy_on_leave` (§2).
   - **`invalid_or_mixed`** ⇒ ⛔ **retain the EXISTING parser's unknown/malformed-token error.** The unknown token is
     the more actionable complaint and must not be swallowed.
   - **`none`** ⇒ unchanged; a bare `team 0` leaves cleanly.
   ★★ **AND STATE IT IN-SOURCE: an INVALID NUMERIC VALUE under a RECOGNISED PHY key still counts as prohibited PHY on
   `team 0`.** Its range is **irrelevant** — leaving never accepts PHY at all, so `team 0 freq=99999` is `phy_only`
   and earns `phy_on_leave`, ⛔ **not** a range complaint. Classification is by **key recognition**, never by value
   validity.
3. **Wire it in `handle_team`** ahead of `parse_phy_tail`. ★ **One message authority (U1) — ⛔ do NOT emit the refusal
   text from `handle_team`;** it must come from the reporter that already owns it (`src/firmware_config.cpp:1162`).
4. **Make the range message verb-correct or neutral** (`:1335`) — matching its two siblings. ⛔ No new string family.
5. ⛔⛔ **CORRECT THE PRODUCTION COMMENT THAT YOUR CHANGE FALSIFIES (QG).** `src/firmware_config.cpp:860-861` states
   the per-verb strings include *"`team`'s own inconsistency (`> team err bad/unknown key:` vs `> team new err:`),
   which is **preserved rather than tidied (C1)**"*. ⇒ ★ **the inconsistency was a KNOWN, DELIBERATE artefact, not an
   oversight** — so record that [[B212]] makes it a **sanctioned fix** (a wrong verb name is a defect, not tidying)
   and that the wording is now verb-correct. ⛔ Leaving that comment standing would be the fifth false comment this
   arc has produced.

### ⛔⛔ THE TRAP
**`kv_next` (`src/firmware_config_parse.h:55`) takes `char*&` and TOKENISES THE BUFFER IN PLACE.** A pre-scan that
runs it over the live tail will **destroy the tail the real parse still needs** on the non-leave paths.
⇒ **scan a copy, or scan only when `t == 0` (where no further parse happens).** ★ **Pin 6 exists to catch exactly
this** — a valid `team <id> freq=… sf=… bw=…` must still parse and apply after your change.

---
## 4 — Pins
1. ★★ **`team 0 freq=868` (partial tail) ⇒ the SPECIFIC leave refusal**, not the range error. **The defect case.**
2. **`team 0 sf=7` alone** and **`team 0 bw=125` alone** ⇒ the same specific refusal. ⓘ These are the two repetitions
   bench 27.2 calls for that currently do **not** reach the branch — 27.2 is *behaviour PASS / diagnostics FAIL* until
   this lands, and this slice is what turns it green.
3. **`team 0 freq=869.4625 sf=7 bw=125` (a tail that PARSES cleanly) still refuses specifically** — ★ *unchanged positive control,
   must stay GREEN* (it already works).
4. **Range errors on `team new` / `team <id>` no longer say `team new err`** when the verb was not `team new`.
5. **A genuinely out-of-range PHY on `team new` still refuses with the range message** — ★ *positive control*.
6. ⛔ **A valid `team <id> freq=… sf=… bw=…` still parses and APPLIES** (the destructive-tokeniser guard, §3).
7. **`team 0` with NO tail still leaves cleanly** — ★ *positive control*; ⛔ the pre-scan must not make a bare leave refuse.

**Control rule:** ★ defect-specific regressions must **FAIL** against the current implementation or under a controlled
mutation; ★ unchanged positive controls (3, 5, 7) must stay **GREEN**. ⛔ Not "everything must go red".
ⓘ **Prefer a counted/measured discriminator over a state assertion** — thirteen instruments in this arc were green
against the defect they were written to catch; the last three slices were saved by counts (B209 timer arms, B211
preserved-field count, B210 literal occurrences). Here the natural discriminators are **which message text appears**
and **whether the request reached the transaction** — assert both, not just "it refused".

### 4.1 — ⛔⛔ WHERE EACH PIN IS PROVEN — CORRECTED (QG), BECAUSE THE PREVIOUS SPLIT WAS IMPOSSIBLE
This brief previously demanded *"assert which message text appears AND whether the request reached the transaction"*
and then declared **all** coverage structural. ⛔ **A structural probe reads SOURCE TEXT — it cannot observe which
message a run emits, nor whether the transaction was reached.** The two statements could not both hold.
★ **The split that works, and the reason the classifier is extracted:**
- **NATIVE — the classifier itself** (`src/firmware_config_parse.h` is a pure header the native suite compiles):
  drive it with the **actual tails** — `""` · `freq=868` · `sf=7` · `bw=125` · `freq=869.4625 sf=7 bw=125` ·
  `freq=99999` (out of range, still `phy_only`) · `freq=868 wibble=3` (**mixed**) · `wibble=3` — and assert the
  three-way result. **Pins 1, 2, 5 and the §3.2 precedence are proven HERE.**
- **STRUCTURAL — `tools/probe_prov_tx`** only for what source text can honestly show: the classifier is **called
  before** `parse_phy_tail`; the `phy_only` arm sets `rq.phy.present`; ⛔ **no refusal literal is emitted from
  `handle_team`** (the one-authority rule); and the range message no longer says `team new err`.
- ⛔ The service side (`phy_on_leave` ordering) is **already natively covered — do not duplicate it.**

---
## 5 — Gate
Baselines: native **1720 / 83996 / 0** · `probe_prov_tx` **17/17 + 30 controls RED** · `probe_board_ui` 120/120 +
14/14 + 52/52 + **153** RED · `probe_firmware_ui` 229/229 · census **174 / 178 / 178**, `-Wswitch` 0 · `lus`
`b77cfd3d` · s18 keystone **`9868cad3` / 269905** · `sizeof(Node)` **221880**.

1. `pio test -e native`, **then RUN `./.pio/build/native/program`**.
2. `probe_prov_tx` + both UI probes with control sets.
3. ✅ **`warning_census.sh` at its pins — RUN IT. SETTLED: the census STAYS AS IS.**
   ★ **Owner-ruled 2026-08-18, CHALLENGED by a QG review, and RE-CONFIRMED by the owner the same day.** ⇒ its three
   pinned OLED builds are **NOT** capped by the two-env limit, and ⛔ **this is not to be re-litigated per slice.**
   ⓘ The reasoning on record: the census is a *warnings* instrument and warnings are gate-blocking here, so capping it
   trades away different coverage than capping the RAM/flash sweep. **Accepted cost: 3 census + 2 board builds.**
4. **Four-step simulator proof.** `src/`-only ⇒ inert by construction — **prove it**; ⛔ no anchor-table edit.
5. ★★ **BOARD BUILDS: TWO ENVS ONLY** (standing ruling). ⛔ Not pre-authorised to escalate. `sizeof(Node)` from a
   **compile-only `static_assert` probe**. ⚠ [[B206]]: both arms in the **same directory**; deltas under ~32 B are noise.
6. **D2:** `sizeof(Node)` 221880, `kCap` 91. ⛔ **CORRECTED: `git diff -- lib/` IS expected empty** now that slices
   1-3 are committed in `6281fc2`; the earlier "do not require empty" carve-out described the pre-commit tree.

**Report:** the pre-scan with `file:line` and how it avoids the destructive tokeniser · proof the refusal text still has
ONE authority · the message-prefix change · each pin with its control and match count · native baseline → after ·
probes · census · the four-step result · the two envs · the D2 answer · exact final `git status --short` confirming
nothing was committed · and the M1/M2 text you owe (⇒ **bench 27.2's diagnostics half can be marked PASS once pins 1-2
hold** — say so).

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ **Do not touch** the bug register, the bench script, the
B207 spec, any brief, or `tracker.md` — report owed M1/M2 text instead.
⛔ **Stop and report** if the specific refusal cannot win without changing the service, or if a corpus row moves.
