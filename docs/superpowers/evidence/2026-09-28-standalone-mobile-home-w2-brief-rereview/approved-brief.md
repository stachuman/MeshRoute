<!-- Author: Claude (brief author); QA: Codex (pre-check, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# Standalone Home W2 — the board-UI probe's stale readers (B418), with B451 and B458 (tool-only)

**Revision 3 — 2026-09-28 — for QA's scoped re-review.** The [brief review](../evidence/2026-09-28-standalone-mobile-home-w2-brief-review.md)
held revision 2 (`0b8606ea…60c0`) on W2R-1–W2R-3; this revision folds them in (§8).

- **Base:** owner commit **`8079e54`** (`8079e545fb2e1065c2bbc347eb9a66160f6cbf8e`, "W4b implemented"). The tree was
  clean at the pre-check, and W1c, W3, W4a and W4b are all committed, so no uncommitted candidates are stacked on the
  base. The pre-check's `inputs.json` is the authority for every path (§1). The simulator is at **`6585649`**
  (`6585649ea5a780f0542b2931853a667be56a5b2b`), clean and read-only.
- **Freeze:** the coder pins this brief by the hash QA issues when it passes review. Under P4 the brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** `/home/staszek/MeshRoute` on `main`, never a worktree. Preserve every tracked and untracked file.
- **Authorities:**
  - the [W2 QA pre-check](../evidence/2026-09-28-standalone-mobile-home-w2-precheck.md) — cited as "pre-check Qn";
  - the [design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) r2.22, §13 row W2;
  - the [register](../../2026-07-30-open-bug-register.md): B418, B451 and B458 in scope; B459 and B460 out of scope;
  - the process rules: `AGENTS.md` / `CLAUDE.md` (C1, P4–P7, D1–D6, M1–M2) and
    [agent roles](../../2026-09-02-agent-roles.md).

## 0. What this is

**B418 — three stale readers in the board-UI probe.** `tools/probe_board_ui/run.sh` pins source wiring that no native
test or simulator compiles. Three of its wiring checks read code that has since moved:
- **W49** extracts `dispatch()` by its old three-parameter signature;
- **W51** requires the old direct `dispatch()` fallback in `ble_dispatch_line`, replaced since RADMIN-0c by one call
  into the shared seam;
- **W54** requires a `UI PRESETS` help group that no longer exists — `help` is now a flat list in
  `src/firmware_help.h`.

All three fail on the real tree. `wchk_in` skips a check's controls when the live check fails, so **none of their 14
negative controls has run since B418 began.** The pre-check executed them separately (pre-check Q5): four change zero
bytes, and W49's fifth has silently turned into a plain deletion. Every UI gate since has carried an exception for
these three failures.

**W2** re-anchors the three checks and moves W54's help clause into a new check against `src/firmware_help.h`. It
proves each of the 14 controls again, adds one control to W51, and corrects the comments that describe moved code.
After W2 the probe's **default run exits 0**, and that becomes its gate in future UI briefs.

**Two tool-only fold-ins, chosen by the owner on 2026-09-28:**
- **B451** — the shared Arduino fake's comment says no assertion reads a based number. That is false: inbox-verbs
  X21–X26 do. Comments only, plus the one stale locator in the inbox-verbs transcript's matching fidelity note.
- **B458** — retire the `model` battery's obsolete M103 entry and keep its history, as W4b QA authorized.

**Why now:** W6 edits the `/mrui` sites that W54 guards (design §13: W2 before W6).

**Not in W2:**
- **B459** — completeness accounting for the board-UI runner. The owner ruled on 2026-09-28 that it is its own tool
  package, dispatched right after W2. Until it lands, the gate reconciles the check set independently (§4.1 step 4).
- **B460** — a radix pin for `whoami`'s hash.
- **B453** (E14), **B443** (a test title in `test/`), and the fake's numeric behaviour.
- Any `src/`, `lib/`, `test/`, `variants/`, `platformio.ini` or simulator change; regenerating the command inventory.

## 1. Verified seams and pinned inputs (pin by symbol; lines are hints from the pre-check inventory)

"Runner" is `tools/probe_board_ui/run.sh`. The per-seam facts are in pre-check Q2–Q9; the brief relies on them.

| Seam | Fact at the base | W2 change |
| --- | --- | --- |
| `DISPATCH_SIG` (runner `:~1245`) with `fn_body` / `fn_flat` | Old signature `bool dispatch(const char* line, size_t len, Print& out) {` matches 0 definitions, so the extraction is empty and W49 fails | The current prefix `bool dispatch(const char* line, size_t len, Print& out, CommandTransport transport) {` matches once (`firmware_commands.cpp:1548`). The definition carries a trailing comment after `{`; `fn_body` already matches by prefix |
| W49 (`:~1258–1273`); the arm at `firmware_commands.cpp:1635–1637` | The guarded `ui` arm is intact, once. With only the signature fixed, W49 is GREEN and all six controls make it RED — but control 5 inserts into `dump_help`, which is gone, so it only deletes | §2.1 |
| W51 (`:~1297–1318`); `ble_dispatch_line` (`fw_main.cpp:519–696`) | The three-token ban holds (0 tokens). The old fallback is absent; the seam call is `:681` and its streamed flush `:682`. All three controls change 0 bytes | §2.2 |
| W54 and `oled_guarded` (`:~1361–1405`) | Five of six tokens are present once each, inside OLED regions; `out.println(F("UI PRESETS` matches 0. Help's `ui` row is `firmware_help.h:132–134`. Control 4 changes 0 bytes | §2.3 |
| `wchk_in` (`:~261–276`) | Runs a check's controls only when the live check passes; totals only what executes (B459) | **Unchanged** |
| Console-sink `structural.py` S23/S24; `negctl.py` X14–X17 | S23: the BLE adapter makes one seam call, with its own sinks and exactly one flush. S24: the seam calls the router exactly once, router-first. X14–X16 turn S23 RED. X17 turns S24 RED (also S53 and S62); its label says "parser BEFORE router", but its edit **deletes** the router call. Structural reader 84/84 (pre-check Q3) | **Unchanged.** W51's comment cites them as the seam-to-router witness |
| `fakes/Arduino.h` (`:90–91`) | "Nothing this probe asserts reads a based number" — false: inbox-verbs X21–X26 compare exact `whoami` lines built with `hash=0x%lu` | §2.4 |
| `transcript_main.cpp` fidelity note (`:29–33`) | Cites "`structural.py` S24" as the HEX pin; the dh/lp radix pin is S27 | §2.4 |
| `model` M103 (`probe_ui_model_mutations.py:3512–3516`) | Its only active definition; survives since W4b; W4b QA authorized retirement (B458) | §2.5 |

**Executable inputs.** These are the fenced files. Each hash is the working-tree SHA-256 at the base.

| File | SHA-256 | Lines | Edit |
| --- | --- | ---: | --- |
| `tools/probe_board_ui/run.sh` | `a458be2be55d4aefb246f73d2bbe41e2c5075ca2123dddfb57ebe3372eedcd6c` | 1428 | §2.1–§2.3, §2.4 comments |
| `tools/probe_board_ui/fakes/Arduino.h` | `4655c95199dc0dd8e57011381a4f064f0408f4e78617682684cb4247479208e4` | 152 | comments only (§2.4) |
| `tools/probe_inbox_verbs/transcript_main.cpp` | `cac897912d573bcdcf9a0226156971e23ee398098772eee8e4bc55e685bc39af` | 148 | comments only (§2.4) |
| `tools/probe_ui_model_mutations.py` | `71c860f9a1fd9a4807fa4789da58545f9fbd4c1d90befee5545ece418d15687e` | 12694 | M103 only (§2.5) |

**Read-only dependencies.** Every path under `readonly_dependencies` in the pre-check's `recommended-inputs.json`
stays at its recorded hash, with two exceptions: `transcript_main.cpp` moves into the fence above, and the four
preparation documents below changed at authoring. The remaining 19 paths are:
- 16 code and tool files: the four command and help sources, both board-UI helpers, console-sink's four files, the
  firmware-UI probe's two files, inbox-verbs' other three, and `platformio.ini`;
- three process documents: `AGENTS.md`, `docs/CODE_GUIDELINES.md` and the agent-roles document.

**Base inventory.** The pre-check's `inputs.json` (1,702 MeshRoute paths, covered by its `SHA256SUMS`) is the
authority for every other path. Since QA wrote it, only the four preparation documents below have changed. New paths
since then: the pre-check report and folder, this brief, and QA's review artefacts.

**Permitted preparation set.** These uncommitted documents are preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | authority (r2.22; §13 statuses) | `a7b0515e633682bdb246f64b46dd345ca9b021764e078a10863a796d92796428` |
| `docs/2026-07-30-open-bug-register.md` | authority (B418, B451, B458–B460; §0) | `56edcd1addf15879021a133b4726e2acf82aa9e483ca1cd4714e45ec7b7e5986` |
| `docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-precheck.md` | authority (pre-check) | `e235319a7a4d4368a863a2b3e6645d50d51c9f0489fd8800a42c4b91dd651a24` |
| `docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-precheck/SHA256SUMS` | evidence — 26 entries, including `inputs.json` (`af364f91…34f6`) and `recommended-inputs.json` | `38d5a4821073a532e094e7798c98aca90e9eb2333edb32488f85cb5f95161169` |
| `tracker.md`, `MEMORY.md` | context pointers | `a40d074d643c6d1be6820a6a5da88ca8bd7e92743c819aeaaea8d6993f923f5c`, `cb10bdcde351184ea91d7a0640d8d7312b929cbd4a20927e4ba2171f2793b33b` |

The design and register changes are status pointers (now naming revision 3), B460's registration, the next-free
number (B461) and the owner's B459 ruling. This brief is pinned by the hash QA issues on PASS, not listed here.

**Preflight:**
1. Check both repositories: MeshRoute `HEAD` = `8079e54`; the simulator at `6585649` and clean.
2. The four fenced files match their hashes above. Every other read-only dependency matches `recommended-inputs.json`.
3. Every preparation-set hash matches, and the pre-check folder verifies (`sha256sum -c SHA256SUMS`, 26 OK).
4. Every path in `inputs.json` still has its recorded hash, except the four preparation documents.
5. Classify every other path. QA review reports and evidence added after this brief are explained preparation
   additions; anything else is reported.

Any of the following is STOP-1 to QA: a different `HEAD`, a mismatched input, a changed preparation file, or an
unexplained path. The whole preparation set stays unchanged from preflight through the freeze.

## 2. Contract

Every check keeps the property its comment states; only the anchors move. No check may be weakened — an exact-form
or placement clause turned into mere presence, for example — and none may pass because a pattern matched nothing.
Every re-anchored or new control must match each of its anchors exactly once, make a real change, and turn its check
RED on the clause named in §2.6.

### 2.1 W49 — the `ui` arm inside `dispatch()`, with its offsets and its guard

- **Extraction:** `DISPATCH_SIG` becomes the current definition's prefix through `{` (pre-check Q2). Keep both
  existing clauses: the arm's `handle_ui(line + 2, len - 2, out); return true;` occurs exactly once in the file, and
  the complete **guarded** arm (`UI_ARM_GUARDED`) occurs inside the extracted `dispatch()`.
- **Controls 1–4 and 6:** kept verbatim.
- **Control 5 — re-anchored as a true relocation.** It moves the whole guarded block (the `#if MR_FEAT_OLED` line, the
  arm and its `#endif`) out of `dispatch()` and into `static void handle_teststatus(Print& out) {`, which sits just
  above it. The guard moves with the arm, so the experiment tests placement alone. The mutant keeps exactly one arm
  in the file and none in `dispatch()`, so W49 fails on the in-`dispatch()` clause alone.
- **All-or-nothing, in both directions (W2R-1).** The control must establish **both** anchors — the destination
  signature and the exact source block, marker comment included — before it changes anything. If either is
  missing, its output must equal its input, so that `wchk_in` reports it vacuous and fails. It must never become
  deletion-only (no arm — how B418 hid it) or insertion-only (a second arm, which W49 would wrongly credit as a
  uniqueness failure).
- **How.** A recipe that inserts line by line and deletes later behind a flag protects only one direction. Use a
  whole-input transformation instead: for example, a sed program that first reads the whole file (`:a;N;$!ba`)
  and then makes one substitution whose pattern spans both the destination signature and the source block, so it
  can match only when both exist.
- **Structural only.** The mutant is not compiled; claim no compilation proof for it (pre-check Q2).

### 2.2 W51 — BLE has no `ui` handler of its own and keeps the shared seam

- **Positive half.** Inside the extracted `ble_dispatch_line`, require exactly one of each:
  - the seam call, `const mrfw::LineExec ex = mrfw::exec_console_line(line, len, mrfw::LineFormat::json, ls, out, cap, ctx);`
  - the streamed arm, `if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }`

  Count occurrences, not matching lines: `fn_flat` flattens the function onto one line.
- **Negative half.** The three-token ban (`"ui`, `handle_ui`, `preset_`) is unchanged.
- **Controls:**
  1. **Re-anchored:** the existing independent-handler insertion (`LineSink lu` + `handle_ui`), placed immediately
     before the seam line.
  2. **Re-anchored:** the existing BLE-only `ui` refusal (`write_err(out, cap, "ui", "console_only")`), at the same
     point.
  3. **Replaced:** remove the seam line.
  4. **New:** remove only the streamed flush (`{ ls.flush(); return 0; }` → `{ return 0; }`).
- **The seam-to-router link stays with console-sink.** W51 pins BLE's side only. Its comment names console-sink
  **S24** as the witness that the seam calls `dispatch()`, whose control **X17** deletes that call. Describe X17 by
  that edit, not by its label (pre-check Q3). S23 (with X14–X16) pins the adapter's shape on the same side as W51.
  X17's label is outside this fence.

### 2.3 W54 and the new W54-help — every `/mrui` site is `MR_FEAT_OLED`-gated

- **W54** keeps its five command-file tokens: the preset-verbs include, the one catalog instance,
  `preset_boot_restore_console`, `handle_ui` and the status catalog reference. Its controls are the existing 1, 2, 3 and
  5, verbatim; old control 5 becomes W54's fourth. Its label drops "help".
- **W54-help (new check).** It uses the same `wchk_in` and `oled_guarded` against `src/firmware_help.h`, through one new
  path variable. It requires `out.println(F("ui"));` exactly once in the file, and inside an `MR_FEAT_OLED` region.
- **W54-help's control:** the old W54 control 4, re-anchored. It replaces the uniquely matched three-line block — the
  `#if MR_FEAT_OLED` line, the `ui` line and its `#endif` — with the single ungated `ui` line. The name survives, so
  the failure is the missing guard, never the deletion (pre-check Q4).
- **No replacement witness.** Console-sink's help controls H-C5 and H-C6 change `team` and `mobile`, not `ui`, so they
  are not a witness for this guard; do not retire the property by citing them.

### 2.4 Comments

**Runner comments** in the edited reader area (pre-check Q4). Keep dated history distinguishable from current claims;
correct only what is now false:
- W54 is not the only instrument that can see help: console-sink's H3a and the inventory comparison see the rendered
  list per product profile.
- Help no longer lives in the command file.
- The command-file loop has five tokens; the help row is W54-help.
- W50 pins the **unconditional** boot call — the guard lives in `firmware_commands.h`'s inert inline stub — not a guard
  around that call.
- The W49–W51 introduction's claim that `src/firmware_commands.cpp` cannot be host-compiled is history. Since
  §CUSTODY-D, inbox-verbs compiles it against fakes. `fw_main.cpp` as a whole is still not host-compiled; the
  inbox-verbs transcript tool lifts only two regions of it.
- W51 describes the one seam call where it describes the old fallback, and names S23/S24 as in §2.2.
- Each re-anchored check keeps a printed counts line for its new anchors, as the file already does.

**B451 — the shared fake** (`tools/probe_board_ui/fakes/Arduino.h`, pre-check Q9). Replace the false sentence at
`:90–91` with a current fidelity note:
- The limitation stays explicit: the numeric overloads accept a base and ignore it, so `HEX` prints decimal digits.
- **board-UI:** no assertion depends on a based numeral.
- **firmware-UI:** no console assertion reads based digits. Panel hex comes from the UI formatters, not this shim.
- **inbox-verbs X21–X26**, on both the ACCEPT and CLIENT arms (12 checks), compare exact `whoami` lines whose oracle
  (`hash=0x%lu`) deliberately follows the shim. They prove equality with the shim, not the board's hex.
- **inbox-verbs' transcript comparator** shares the limitation.
- **Radix witnesses:** console-sink **S27** pins the send handle's dh/lp radix. Nothing pins `whoami`'s radix — say so
  and cite **B460**; claim no witness that does not exist.

Every executable byte and every signature stays identical.

**The transcript's locator** (`tools/probe_inbox_verbs/transcript_main.cpp`, fidelity note `:~29–33`). Change
"`structural.py` S24" to **S27**, the row that actually pins the dh/lp `HEX` radix. Comments only. This fix is an
author addition beyond the pre-check's three-file fence: it corrects the same limitation's documentation. The file's
only tool reader is `transcript.py`, which compiles it; no in-run hash list includes it (step 9 confirms).

### 2.5 B458 — retire M103

Remove the active M103 tuple and its now-obsolete explanation from the `model` battery. In their place, write a dated
retirement record, following the same file's retired §CUSTODY-B record (`:~350`). The record keeps:
- the ID;
- the old edit: both `_st.cursor = 0` and `_cfg_sel_valid = false` removed from `close_settings_menu`;
- the reason, from the W4b QA disposition and B458: MENU's `go_menu_home` repeats both resets, and B457's closed
  menu-mode preview neither renders the cursor nor reads the flag before leaving or re-entering Settings sets both
  again;
- the live witnesses: M100, M101, M102 and M105 in `model`, and H25 in `w4bhome`.

Nothing else in the file changes: every other entry, target, worker logic and the native PIN (3031/198613) stay
identical. The "239 entries" comments at `:~628/:~642` are a dated 2026-08-30 measurement and stay as they are
(pre-check Q8). Do not add a private-state test for the obsolete behaviour.

### 2.6 The closed ledger

| Check | Control (by meaning) | Disposition | Must fail |
| --- | --- | --- | --- |
| W49 | delete the arm | kept | the arm's file-wide presence |
| W49 | unsliced `handle_ui(line, len, out)` | kept | the exact `line + 2` / `len - 2` arm |
| W49 | handler removed, ownership return kept | kept | the handler's presence |
| W49 | `strncmp` length 2 → 1 | kept | the exact guarded arm |
| W49 | guarded block moved to `handle_teststatus` | **re-anchored** (all-or-nothing) | **only** the in-`dispatch()` clause; one arm remains in the file |
| W49 | `#if` guard commented out | kept | the guarded arm |
| W51 | BLE's own `ui` handler | **re-anchored** | the three-token ban |
| W51 | BLE's own `ui` refusal | **re-anchored** | the three-token ban |
| W51 | seam call removed | **replaced** | the seam-call clause |
| W51 | streamed flush removed | **new** | the flush clause |
| W54 | include guard commented out | kept | the include outside a region |
| W54 | catalog-binding guard commented out | kept | the binding's tokens outside a region |
| W54 | status guard commented out | kept | the status token outside a region |
| W54 | the one catalog instance deleted | kept | the instance's presence |
| W54-help | the `ui` help row ungated | **re-anchored** from W54 | **only** the region clause; the literal remains once |

**Totals (pre-check Q5/Q6):**
- **Wiring:** 59 → **60 checks**, all passing. Controls: 171 → **186**, all RED (+6 W49, +4 W51, +4 W54, +1 W54-help).
- **Unchanged:** structural 23/23, V3 124/124, V4 110/110, trait controls 14 RED, missing-trait compile controls 12
  (each a required compiler refusal), negctl 60 (V3) + 3 (V4).
- **Old → new ordinals:** the report maps them for every control. The ledger names controls by meaning.

Anything outside this ledger is a STOP.

### 2.7 Nothing else moves

- **Inputs:** no product, build, test or simulator input changes; the §1 read-only set stays byte-identical.
- **Command inventory:** it stays at 197 rows, checked and never regenerated.
- **Model battery:** 238 configured entries, all RED. Every worker's native baseline stays 3031/198613/0.
- **Tools suite:** 375 tests; W2 adds none.
- **Metal:** the metal plan does not change — W2 has no behaviour a board can show (M2).

## 3. Fence

**IN:**
- `tools/probe_board_ui/run.sh` — §2.1–§2.3 and the §2.4 runner comments;
- `tools/probe_board_ui/fakes/Arduino.h` — comments only (§2.4);
- `tools/probe_inbox_verbs/transcript_main.cpp` — the one locator, comments only (§2.4);
- `tools/probe_ui_model_mutations.py` — M103's retirement only (§2.5);
- the report and evidence (§5).

**OUT:**
- every `src/`, `lib/`, `test/` and `variants/` file, `platformio.ini`, and the simulator;
- every other tool, including the console-sink files (X17's label stays as it is), `tools/probe_board_ui/negctl.py`
  and `probe_main.cpp`, and every tools test;
- completeness accounting or expected-total pins in the runner (B459), and a `whoami` radix pin (B460);
- the fake's numeric behaviour, E14 (B453) and B443's test title;
- regenerating the command inventory;
- the design, register, tracker, `MEMORY.md` and metal plan (QA lands, §7).

If a repair seems to need a production edit — a marker comment to anchor a control, for example — that is STOP-1:
a fence question for the owner.

## 4. Gates

**The pre-check's focused chain (Q10), the same for the coder and QA.** A tool-only change cannot move the firmware, so
the chain measures the edited instruments and proves by hash that nothing else changed.

**Ordering:** nothing else runs between or during these steps. No `--no-neg` result substitutes for a control gate.

### 4.1 The coder's gate

**Baseline, on the unmodified tree:** the stock `bash tools/probe_board_ui/run.sh` (default mode). Expect exit 1 with
exactly W49/W51/W54 failing, and pre-check Q6's counts.

**Edits (§2), then the chain:**
1. **Scope and hygiene:**
   - `git diff --check` is clean;
   - only the four fenced files, the report and the evidence differ from the preflight inventory;
   - every read-only dependency is byte-identical;
   - the simulator is still clean at `6585649`.
2. **Comment-only proof** for `Arduino.h` and `transcript_main.cpp`. Either the file is byte-identical to its base
   once the approved comment delta is removed, or the comment-stripped tokens are equal (`g++ -fpreprocessed -dD -E
   -P`, base versus final). Compare the tokens, never the file names or line markers of two temporary paths.
3. **M103 proof:**
   - an AST check that the `model` battery contains no active M103 entry;
   - every other entry, target and worker line is byte-identical to the base;
   - the retirement record contains the four items of §2.5.
4. **Board-UI, stock default run:** exit 0, with §2.6's totals. Then, while B459 is open:
   - **Reconciliation.** Build a declared-versus-executed ledger independently of the runner's summary: every check
     and control declared in the runner source, matched to exactly one executed result. A trace of the stock runner
     is acceptable (pre-check Q7). Nothing missing, duplicated or unknown.
   - **Per-control proof** for each of the 15 re-anchored, replaced, new or formerly dormant controls:
     - each edit matches its anchor once;
     - the mutant differs from the real file;
     - the check is RED;
     - a decomposed evaluation of the check's clauses on the mutant shows the §2.6 clause failing — for W49's
       relocation and W54-help, **only** that clause.
   - **W49 relocation, both directions (W2R-1):** each missing anchor is tested on its own scratch copy, on which
     the live W49 check is still GREEN:
     - `handle_teststatus`'s signature renamed — the control's output equals its input;
     - the source block's marker comment changed, a comment-only edit that W49 ignores — the control's output
       equals its input.

     The positive proof on the real file stays: one arm in the file, none inside `dispatch()`, and only the
     placement clause fails.
5. **Console-sink, stock default run:** exit 0. S23 and S24 GREEN. X14–X16 RED on S23; X17 RED on S24 (and S53/S62),
   reported by its actual edit. Record its counts.
6. **Model battery:** the stock `python3 tools/probe_ui_model_mutations.py --target=model`. Expect 238 configured,
   238 RED, 0 unusable, 0 vacuous; every worker baseline 3031/198613/0.
7. **Tools discovery** (D5): `python3 -m unittest discover -s tools -p 'test_*.py'`. Expect 375 tests, OK, 0 skipped,
   no import failure.
8. **Command inventory:** `python3 tools/gen_command_inventory.py --check`. Expect 197 rows, byte-identical.
9. **Reader audit (D6/P7):** grep `lib/`, `src/`, `test/` and `tools/` for every reader of the edited text:
   - the W49, W51, W54 and W54-help labels and anchors;
   - M103;
   - the two comment edits, including the two in-run `md5_sources` lists (inbox-verbs and firmware-UI), which pin no
     hash.

   Report each reader, and say whether the edits touch its predicate.

**Input stability:** inputs are recorded before the baseline and after the last step. They differ only by the fenced
edits and the new evidence; a change in between restarts the chain.

### 4.2 QA's independent gate

On the frozen tree, QA re-runs chain steps 1–9 itself, including its own reconciliation and per-control proofs, and
verifies the freeze inventory. The coder's baseline is not repeated: pre-check Q6 already measured it.

**Not run, by either side,** with the reason each stays unaffected — confirmed by the reader audit:
- **Native, corpus, board builds and measurements, ABI and the warning census:** no product, build or test input
  changes (step 1 proves it), and the model battery's workers rebuild native in isolation.
- **The firmware-UI and inbox-verbs probes:** the only edits they compile are comments — the shared fake (both
  probes) and `transcript_main.cpp` (inbox-verbs' transcript tool) — proven comment-only by step 2. Board-UI compiles
  the shared fake in step 4 anyway.

### 4.3 STOP conditions

- **Fence:** an edit outside §3; a production edit that seems necessary (STOP-1 — the owner's fence question);
  completeness logic or expected-total pins in the runner (B459).
- **Comments:** any executable byte or signature change in `Arduino.h` or `transcript_main.cpp`; a fidelity note
  that claims a `whoami` radix witness.
- **Ledger:** a check or control changed, added or retired outside §2.6; a check weakened; totals other than §2.6's.
- **Controls:** a re-anchored, replaced or new control that is vacuous, matches more than once, or goes RED on a
  clause other than its named one; a W49 relocation that can become insertion-only or deletion-only.
- **Model battery:** any entry other than M103 changed; any non-RED entry; a worker baseline other than
  3031/198613/0.
- **Other instruments:** console-sink not exit 0, or S23, S24 or X14–X17 behaving differently from pre-check Q3; a
  tools-suite count other than 375, or any skip or import failure; a command-inventory difference.
- **Inputs:** a mismatch at preflight, a changed preparation file, or an input change during the chain.
- **Source facts:** a §1 fact found false — STOP-1 to QA, never a coder assumption.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2.md`, with an author line on line 1.

**Evidence directory:** `docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2/`. It holds:
- the baseline and final board-UI summaries;
- the declared-versus-executed ledger;
- the per-control proofs: each mutant diff, its clause evaluation and the relocation demonstration;
- the old → new ordinal map;
- the comment-only proofs and the M103 AST proof;
- compact console-sink, model, discovery and inventory summaries;
- the reader-audit greps;
- the freeze inventory;
- a `SHA256SUMS` covering everything.

**Retention.** Follow [the evidence-retention policy](../evidence/README.md). Raw logs go under ignored
`artifacts/2026-09-28-standalone-mobile-home-w2/`; mutation scratch stays outside both repositories.

**The report contains:**
1. **Preflight** and the inventory check.
2. **Baseline.**
3. **Diff:** `git diff --stat` and the §2.6 ledger with the ordinal map.
4. **Figures:** every §4.1 figure, derived by the coder.
5. **Reconciliation** and the per-control proofs.
6. **The pin line:** the exact line `PIN re-synced? YES — <derivation>`, derived from the coder's own run. The
   expected derivation: native 3031/198613 unchanged; model 239 − 1 retired M103 = 238; wiring checks 59 + 1 = 60;
   wiring controls 171 + 6 + 4 + 4 + 1 = 186 (W2R-2).
7. **Freeze inventory:**
   - the final SHA-256 of every fenced file;
   - the authorized brief hash;
   - the simulator commit and status;
   - the preparation-set hashes, unchanged;
   - the inventory check;
   - the new evidence paths with hashes;
   - the stability statement.
8. **Not run,** with reasons (§4.2). Each reason is that the instrument's predicates are unaffected — confirmed by the
   reader audit — never "it reads none of the files".

## 6. Owner rulings

- **Scope.** The owner chose W2 with B451 and B458 on 2026-09-28. Nothing else is requested.
- **B459 — ruled 2026-09-28:** not folded into W2. It is its own tool package, dispatched right after W2 and before
  W6. Until it lands, §4.1 step 4 compensates.
- **B460** was registered by the author while writing this brief; it stays outside W2.
- **A contradictory interpretation** returns to the owner.

## 7. Landing (QA, on PASS)

- **Register:** **B418, B451 and B458 CLOSED** in place; B459 and B460 stay OPEN; the §0 dispatch is rewritten in
  place.
- **Design:** the §13 W2 status.
- **Pointers:** `tracker.md` and `MEMORY.md`.
- **The board-UI gate for future UI briefs:** the default run, exit 0, with an independent declared-versus-executed
  reconciliation while B459 is open. The B418 known-failure exception retires. `--no-neg` stays a diagnostic.
- **Metal plan:** no change.
- **Next:** B459, as its own tool package (owner ruling 2026-09-28). Then W6, which W2, W3 and W4b unblock — or W5,
  W9, W0, W1b, W4c or B460, in the owner's order.

## 8. Revision history

**Revision 1 (2026-09-28)** is the first draft, built on the W2 pre-check. It adopts the pre-check's recommendations:
- the three-file tool fence;
- W54's help clause as a separate W54-help check;
- W51's fourth control;
- the focused chain for both sides.

It adds three author choices:
- the W49 relocation control must be all-or-nothing;
- the transcript's S24 → S27 locator, comment-only;
- B460's registration.

It keeps B459 and B460 out.

**Revision 2 (2026-09-28)** folds in the owner's B459 ruling before QA review: B459 is its own tool package,
dispatched right after W2. The delta is the §0 and §6 B459 bullets, the §7 next step, and the four preparation pins
(design, register, `tracker.md`, `MEMORY.md`), which changed only to record the ruling. The contract, fence, closed
ledger and gates are unchanged.

**Revision 3 (2026-09-28)** folds in the brief review (HOLD, W2R-1–W2R-3):
- **W2R-1:** revision 2's relocation recipe protected only against a missing destination; with the source anchor
  changed it would insert a second arm. §2.1 now requires both anchors before any change (a whole-input
  transformation), §4.1 step 4 tests each missing anchor on its own copy, and §4.3's STOP names insertion-only and
  deletion-only.
- **W2R-2:** §5 adds the `PIN re-synced? YES` line and its expected derivation.
- **W2R-3:** the register §0 and design §13 pointers name revision 3; their pins are refreshed.
- **Also, from the review's notes:** §4.1 step 2's preprocessing alternative compares tokens, not file names or
  line markers.

Nothing else changes.
