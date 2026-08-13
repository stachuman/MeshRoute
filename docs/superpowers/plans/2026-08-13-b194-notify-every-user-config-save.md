<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B194 — notify on EVERY successful user-initiated `/mrcfg` save · dispatch brief · 2026-08-13

**Status: DISPATCHED 2026-08-13 on a QG HOLD verdict relayed by the owner.** ★ Role split: the QA-gate wrote this
brief and verifies your claims at the code; **the OWNER runs QG and rules.** ⚠ **The corrections below are QG's
recommendation relayed by the owner — that is still a recommendation, not an owner ruling** (ledger §3 rule 5).
⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out another commit here** —
the tree carries the uncommitted mobile-home arc, UI-7D slice B, UI-13 and UI-14, none of them committed.

**Baseline (the current tree, verified):** HEAD **`24d8931`**; native **1613 / 82339 / 0**; `lus` **`43a7b6eb`**;
`sizeof(Node)` **221880**; `probe_firmware_ui` 117/113/87 with 41 controls RED; `probe_board_ui` 13 structural + 15
wiring with 24 wiring controls RED.

---
## 1 — The blocker, verified at the code

§UI-14's follow-up wired `mr_ui_on_config_saved()` into **`handle_cfg_set` only**. **`leave` is a second serial verb
that changes covered settings and does not notify:**

- `src/firmware_config.cpp:1336` — `b = mrnv::Blob{};` **zeroes the whole record**, and the lines that follow restore
  only `magic`/`version`/`freq`/the radio defaults/`beacon_ms`/`duty`/the three anti-spam knobs. ⇒ **all four covered
  fields (`ble_mode`, `e2e_dm`, `intro_attach`, `mobile_autoregister`) land at 0**, whatever they were.
- `src/firmware_config.cpp:1342` — `if (!mrnv::save(b)) { … return; }` persists it.
- **No `mr_ui_on_config_saved()` call anywhere in the function.**

⇒ an open OLED draft does **not** immediately show `CFG! RELOAD`, contrary to the covered-field contract at
`docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md:564`. ⛔ **No owner ruling narrowed that contract to
`cfg set`.** ⓘ Note the shape: `leave` does not merely *touch* a covered field — it **resets all four**, so this is the
largest covered-field change any verb makes.

### ★ A refinement to [[B194]]'s own row — measure it, do not inherit it
The register row (`docs/2026-07-30-open-bug-register.md:101`) names *"`join`/`create`/`team`/`leave`"* as the
non-notifying writers. ⛔ **That list is not accurate, and you must correct it in place (M1, §3 rule 3) rather than
carry it forward.** Measured on this tree:

| writer | `file:line` | blob origin | writes a covered field? |
|---|---|---|---|
| `handle_cfg_set` | `:523` save, `:540` notify | `nv_load_stamped` | **yes, by design** — ✅ already notifies |
| `handle_leave` | `:1336` zero, `:1342` save | `nv_load_stamped`, then **discarded** by `Blob{}` | ⛔ **YES — all four reset to 0. THE BLOCKER.** |
| `handle_gateway` | `:621` `Blob b{}`, `:622` `if (!mrnv::load(b))`, `:653` save | persisted blob **on load success** | **only on the load-FAILURE path** — ⛔ **CORRECTED IN PLACE 2026-08-13 (§3 rule 3), disproven by the coder and re-verified by me: this cell read *"the seed subset at `:623+` sets none of the four ⇒ they persist as 0"*. IT SETS ONE** — `b.ble_mode = g_ble_mode` (now `src/firmware_config.cpp:657`); only `e2e_dm`, `intro_attach` and `mobile_autoregister` are left at 0 by `Blob b{}`. The line was in my own grep output when I wrote this table. **The conclusion — gateway must notify — is unchanged; the stated reason was wrong.** |
| `handle_join` | `:740` load, `:747` save | `nv_load_stamped` | **no** — none of the four is assigned |
| `handle_create` | `:783` load, `:794` save | `nv_load_stamped` | **no** — none of the four is assigned |
| `handle_team` | `:1112` load, `:1211` save | `nv_load_stamped` | **no** — none of the four is assigned |
| `password` | `:1360` load, `:1363` save | `nv_load_stamped` | **no** — admin fields only |
| fw_main ctr-lease / join persist | `src/fw_main.cpp:965` | `nv_load_stamped` | **no** — id/epoch/joined/ctr/team_local_id |
| fw_main leaf-config adopt | `src/fw_main.cpp:1268` | `mrnv::load` | **no** — C-frame fields |
| remote admin floor / pubkey | `src/firmware_remote.cpp:113`, `:139` | `mrnv::load` | **no** — admin fields only |

★ **Verify this table yourself before you rely on it** (V1) — it is my measurement, not a ruling, and if you find it
wrong, **say so and correct it** rather than implementing around it.

---
## 2 — What to build: the SYSTEMATIC rule, not the one site

**Notify after every successful USER-INITIATED `/mrcfg` save** — `leave` (the blocker) **and** `gateway` · `join` ·
`create` · `team` · `password`.

★★ **Why the systematic rule and not just `leave`, stated so you can disagree with the reasoning rather than the
conclusion:** the alternative — notify only where a covered field provably changes — makes **every future writer**
re-derive §1's table, and a writer that forgets is silently non-compliant. **That is this arc's class-4 defect (a
correction placed anywhere but the instruction a reader follows, twelve-plus sites) turned into a maintenance
policy.** The systematic rule is safe because it is **self-limiting by construction**: `mr_ui_on_config_saved`
(`src/firmware_ui.cpp:1023`) re-reads the record and hands it to `note_external_write`, which **compares only the four
covered fields against the baseline** ⇒ **a save that moved nothing covered raises nothing**, and the repaint is
edge-triggered so it cannot even ask for a redundant frame.

- ⛔ **The internal writers stay silent** — `fw_main.cpp:965`/`:1268` and `firmware_remote.cpp:113`/`:139`. They cannot
  change a covered field (§1), they are not user-initiated, and the lease fires on a timer: notifying there would put a
  flash read on a periodic path for a latch that can never move.
- ⛔ **No `MR_FEAT_OLED` may reach `src/firmware_config.cpp`** — the hook is feature-neutral, that is its whole point
  (`lib/hal/mr_ui.h:31` declaration, `:37` inline no-op). `probe_board_ui`'s **W13** already tests this; keep it true.

### ★★ Placement — a fact is established by the act
**Call it only after a write that BOTH happened and SUCCEEDED.** Two of the sites need care:

1. ⚠ **`handle_team` at `:1211` is the §3-A.4 UNCHECKED save**: `if (!mrnv::save(b)) out.println(…)` with **no
   `return`** — the live team state is already applied and is deliberately not rolled back. ⇒ you must **capture the
   verdict** (`const bool ok = mrnv::save(b); if (ok) mr_ui_on_config_saved(); else out.println(…);`) rather than
   assume it. ⛔ **Do not change that site's rollback behaviour** — only its notification.
2. ⚠ **`password` at `:1363`** already captures `const bool saved = …` and prints/returns at `:1366`. Put the notify on
   the success side of that existing verdict; ⛔ do not move the `memset` that wipes the derived keypair.

⛔ **Never on a failure branch, never before the write.** Those are exactly W12's four controls.

---
## 3 — Test obligations

- ★ **Extend `tools/probe_board_ui/run.sh`'s wiring checks** with the placement of every site you wire — the existing
  **W12** pattern (`run.sh:351-358`) is the model: a presence/shape check plus **four mutations** — *deleted* · *moved
  before the write* · *guard dropped* · *added to the failure branch*. ⛔ **Every new check needs its controls RED**;
  a wiring check that passes against a deleted call is this arc's 24-instance "instrument that cannot fail".
  ⓘ The call sites are in a file **no host build compiles**, which is precisely why the source-level probe is the
  instrument and inspection is not.
- ★ **`leave`'s semantic case in native** (`probe_firmware_ui` / the config battery, whichever owns the sibling
  `cfg set` cases at `probe_main.cpp:766-818`): a draft open with a covered field edited, a `leave`-shaped external
  write (all four → 0), then **`conflict()` true and SAVE refused**. **Plus the negative half**: a `join`-shaped
  external write that moves **no** covered field must raise **nothing** — that is the control that proves the
  systematic rule is self-limiting rather than merely loud.
- ★ **Every new assertion mutation-proven, match counts printed.** The runner is `tools/probe_ui_model_mutations.py`
  — ⚠ **read its header: six defects were needed to make it safe; use `guarded_write` and the existing locks, and do
  not write a source any other way.**

---
## 4 — Documentation, and the sibling sweep

1. **`docs/superpowers/specs/…-onboard-oled-ui-design.md:1344`** — the UI-14 row still publishes **1610/82310**; the
   live total is **1613/82339**. ⚠ **And that number will move again with this slice** ⇒ **publish the number you
   measure at the end, not this one.**
2. **`lib/hal/mr_ui.h:5`** — *"implements these **three** hooks"* is stale; there are **four**. ⓘ `:15` already calls
   the config hook *"THE FOURTH HOOK"*, so the file currently contradicts itself two lines apart.
3. ★★ **GREP FOR THE SIBLINGS BEFORE YOU CALL EITHER ONE FIXED.** Twelve-plus recorded sites of *a correction placed
   anywhere but the instruction a reader follows*, and **the last four slices each needed roughly double the sites
   that were cited.** Known starting points: `simulation/BASELINE.md` §UI-14 (`:5` and `:28` both publish totals, and
   `:20`'s follow-up note describes the notification as if `cfg set` were the whole of it) · the spec's §3.6.1 prose
   at `:564+` · the `mr_ui.h` header prose · **and [[B194]]'s own register row `:101`, whose writer list §1 shows is
   wrong.** ⛔ **`mr_ui.h:9`'s "The next board-UI PR just fills the seam" is not yours to sweep — leave prose you did
   not falsify alone.**
4. **[[B194]] closes** once the user-initiated writers notify **and** the register row records, with `file:line`
   evidence, **why the internal writers are exempt** — i.e. §1's table, measured. ⛔ **Close it in place; never delete
   the historical description** (M1, §3 rule 3). ⛔ **[[B193]] does NOT close** — no NVS/LittleFS write, no wear and no
   reset-during-write is exercised by anything here; bench Parts 19/20 remain the only closure path (M2).

---
## 5 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   From **1613 / 82339 / 0**.
2. ★ **Both UI probes**, with their control sets reported: `tools/probe_firmware_ui/` (from 117/113/87, 41 controls
   RED) and `tools/probe_board_ui/` (from 13 + 15, 24 wiring controls RED).
3. **`warning_census.sh`** with its multiset — from **PASS at 174 / 178 / 178**.
4. **Corpus: expect NO movement.** This is `src/` only. Print the `lus` md5 (**`43a7b6eb`**); **if a row moves, stop
   and report.**
5. ★★ **Answer D2 explicitly.** `sizeof(Node)` is **221880** and must not move — nothing here belongs in `lib/core`.
   **Report the per-board RAM/flash diff** (`heltec_v3` is at **215980 / 65.91 %**); the §UI-14 hook itself cost zero.
6. ⛔ **Zero free timer ids** (`kCap == 91`). Allocate none.

---
## 6 — Method

- ★★ **A fact is established by the act** — §2's placement rule is the whole of it: notify after a write that
  **happened** and **succeeded**, never on a refusal, never before.
- ★★ **Name the third state** — five-plus instances this session. `handle_team`'s save is not
  success/failure-with-return; it is **success · failure-but-live-anyway**, which is why capturing the verdict is not
  cosmetic.
- ★★ **Instruments that cannot fail — 25 instances.** Ask of every new wiring check and every new case: could it have
  come out otherwise? **Prove it by mutating.**
- ⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval; **never quote an owner ruling** —
  reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation.**
- ⛔ **Do not describe Phase A as complete** — [[B164]]/[[B189]] gate on-device registration/team onboarding and final
  acceptance; §UI-7D is 🧪 metal; §UI-13/§UI-14 are 🧪 NV/power-cut.

**Report:** every wired site with `file:line` and its placement relative to the save verdict · the writers you left
silent **and the measured reason** · the `leave` case and its negative half · every new wiring check with its controls
· native · both probes · census · corpus (no movement, with the `lus` md5) · the **D2 answer and per-board RAM/flash**
· every documentation site you swept, **including the ones you found beyond the four named above** · exact final
`git status --short` and that nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** §1's table is wrong (⇒ **report the correction, do not silently
re-scope**) · a site cannot be notified without changing its failure handling (⇒ **report it; changing rollback
behaviour is not in this slice**) · `handle_gateway` turns out unreachable on any OLED build and you judge the wiring
pointless (⇒ **report the reasoning and let the owner decide — do not drop it silently**) · a corpus row moves ·
`sizeof(Node)` moves.

---
---
# ROUND 2 — QG HOLD (relayed by the owner 2026-08-13). The runtime is sound; the TRIPWIRE and two DOCS are not.

⚠ **These are QG's findings relayed by the owner — a recommendation, not an owner ruling** (ledger §3 rule 5).
✅ **QG confirmed good and NOT to be re-opened:** all seven sites notify only after a successful save, including
`leave`; the internal timer/remote writers are reasonably exempt. ⛔ **Do not re-touch the seven call sites.**

## R2.1 — ⛔⛔ BLOCKER: the "future writer" tripwire is VACUOUS

`tools/probe_board_ui/run.sh` — `CFG_NOTIFY_SITES=7` and `nsite()` count **only** occurrences of
`mr_ui_on_config_saved()`. ⇒ **an eighth `mrnv::save()` added with NO notification leaves the count at seven and every
check green** — exactly the omission the block claims to catch.

★★ **And the comment is the worse half of this defect**, because it is the instruction a reader follows: the block
asserts *"a new writer that forgets to notify does not merely leave its own check unwritten — it makes all seven fail
loudly, which is the maintenance property `§notify-every-save` exists for."* **That is false as written.** ⇒ this is
BOTH class-3 (an instrument that cannot fail — 25 instances) AND class-4 (a claim living in the comment a reader
trusts). ⛔ **Fix the code AND the comment; do not leave a corrected count under a sentence that overstates it.**

**Required:**
1. **Count the command-side `mrnv::save(` sites too**, explicitly exempting `DeviceCfgStore::save`
   (`src/firmware_config.cpp:101`, the `ICfgStore` override — it is the service's own store, not a verb).
   ⓘ Verified for you: the file holds **8** `mrnv::save(` occurrences — the 7 verbs plus that override — and
   `grep -oF 'mrnv::save('` does **not** match `mrnv::save_id(` / `save_peers(` / `save_faults(`, so the `/mrid` and
   peer/fault writers are excluded by the paren, not by luck. **Re-verify that before relying on it** (V1).
2. **A negative control that inserts an EIGHTH BARE SAVE and proves the gate turns RED.** ⛔ That control is the whole
   point of this round — a two-clause count whose "someone added a save" arm was never run is the same defect with a
   larger constant.
3. **Keep the existing per-site ordering controls** — QG confirms they correctly verify success/failure placement.
4. ★ **State the tripwire's SCOPE LIMIT plainly** rather than letting the comment imply more than it guards: it is a
   per-file check on `src/firmware_config.cpp`. **A new user-initiated verb added in a DIFFERENT file is not caught.**
   ⛔ Say so in the comment; do not quietly widen the claim, and do not build a whole-tree scan in this round.

## R2.2 — ✅ ALREADY DONE, do not repeat

The plan's disproven `handle_gateway` cell (this file, §1's table) is **corrected in place by the QA-gate** — the
seed does set `ble_mode`. ⛔ **Do not edit this file's §1 table.**

## R2.3 — ⛔ BLOCKER: two live documents still say UI-14 does not exist

Both must be reworded as **historical §UI-13 slice boundaries** — UI-13 landed HEADLESS by scope; **UI-14 now consumes
it and supplies the device binding** — with ⛔ **[[B193]]'s real-flash / power-cut qualification still OPEN**.

1. **`src/firmware_config_service.h:41`** — the `✖ NOT IMPLEMENTED HERE` block still says *"There is no instance of
   this service on hardware yet, so nothing calls it"*, and `:39` still routes the SETTINGS renderer to a future
   §UI-14. Both landed. ⚠ **The obligations that paragraph records are still worth keeping** (the §nv-ritual load, the
   OFF→ON `mobile_register_current()` bridge) — ⛔ **do not delete them; restate them as what the binding DID.**
2. **`docs/superpowers/specs/…-onboard-oled-ui-design.md:49`** — the UI-13 row ends *"⛔ NOTHING RENDERS OR CALLS IT …
   so the running firmware's behaviour is unchanged"*, **immediately above the UI-14 row that says the opposite.**

★★ **AND SWEEP THE SIBLINGS AGAIN.** This is the third consecutive round in which the sites named were fewer than the
sites that existed. ⛔ Correct in place, keep the withdrawn wording (§3 rule 3), and **report every site you found
beyond the two named.**

## R2.4 — Gate for this round

From **1615 / 82362 / 0** (I re-ran the binary myself and confirm it). Both probes with their control sets ·
`warning_census.sh` · corpus expected **byte-identical** (`lus` `43a7b6eb`; R2 is a probe + comments + docs round —
⚠ if you touch `src/firmware_config_service.h` beyond comments, say so and re-run everything) · `sizeof(Node)`
**221880** · per-board RAM/flash only if any compiled line changes. ⛔ Nothing committed. ⛔ **[[B193]] does not close.**
⛔ **Phase A is not complete.**
