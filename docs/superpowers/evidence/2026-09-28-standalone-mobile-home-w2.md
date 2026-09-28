Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# Standalone Home W2 — the board-UI probe's stale readers (B418), with B451 and B458: coder report

**Role:** coder. **Contract:** brief **revision 3**,
[`docs/superpowers/plans/2026-09-28-standalone-mobile-home-w2-board-ui-readers.md`](../plans/2026-09-28-standalone-mobile-home-w2-board-ui-readers.md),
SHA-256 `341d4bc0c401c8fd254107eea90915cefb2dee94580cdcaf912a99f015c89737` (458 lines), authorized by the
[review](2026-09-28-standalone-mobile-home-w2-brief-review.md) (HOLD on revision 2) and the
[re-review](2026-09-28-standalone-mobile-home-w2-brief-rereview.md) (PASS). **Base:** owner commit `8079e54`;
simulator `6585649`, clean and read-only throughout. **Evidence:** [`2026-09-28-standalone-mobile-home-w2/`](2026-09-28-standalone-mobile-home-w2/)
with its own `SHA256SUMS`; raw logs under ignored `artifacts/2026-09-28-standalone-mobile-home-w2/` (hashed in
`raw-logs.json`); mutation scratch stayed outside both repositories.

**Result:** every §4.1 step passes on figures derived from this run. The board-UI stock default run exits **0** with
wiring **60/60** and **186** controls RED; an independent declared-versus-executed reconciliation matches all of them
one to one. **Ready for independent QA.** Nothing was staged or committed.

## 1. Preflight and the inventory check

`preflight.txt` (raw) — **PASS**, before any edit and before the baseline:

- HEAD `8079e545…`, simulator `6585649e…` with a clean status; brief SHA-256 equals the authorized hash.
- Fence: all four base hashes and line counts match §1 (run.sh 1428, Arduino.h 152, transcript_main.cpp 148,
  harness 12694).
- Read-only dependencies **19/19** match `recommended-inputs.json`; preparation pins **6/6** (design, register,
  pre-check report, pre-check `SHA256SUMS`, `tracker.md`, `MEMORY.md`); pre-check `SHA256SUMS` 26 OK.
- The pre-check `inputs.json` (1699 files + 4 symlinks): **4 changed, all preparation; 0 missing; 0 unexplained**.
  New paths **49**: the pre-check report and folder, both review receipts and their folders, and the brief. The
  simulator inventory (285) is unchanged, with nothing new.

**Source check before editing (§1, by symbol) — every fact holds:** the old `DISPATCH_SIG` matches 0 definitions and
the new prefix matches 1 (`firmware_commands.cpp:1548`); `handle_teststatus` sits just above it (`:1534`); the
guarded `ui` arm is `:1635–1637`; `dump_help` is gone from the TU. `ble_dispatch_line` has the seam call (`:681`)
and the streamed flush (`:682`) once each, and neither the old fallback nor `if (e == ParseErr::unknown_verb) {`
exists. W54's five tokens occur once each and `UI PRESETS` 0 times; the `ui` help row is `firmware_help.h:132–134`,
the file's only `MR_FEAT_OLED` region. The fake's false sentence is `Arduino.h:90–91`; the transcript cites S24
where the radix row is S27. X21–X26 build `hash=0x%lu` and sit outside any RADMIN guard (both arms). X17 deletes
the seam's router call. M103 is harness `:3512–3516`; its witnesses are live (M100/M101/M102/M105 in `model`, H25
in `w4bhome`).

## 2. Baseline (unmodified tree)

Stock `bash tools/probe_board_ui/run.sh` (default mode) → **exit 1**, failing exactly **W49, W51, W54** ("not wired").
V3 **124/124**, V4 **110/110**, traits **14**, missing **12**, structural **23/23**, wiring **56/59** with **171**
controls RED, negctl **60 + 3**. Identical line for line to pre-check Q6's summary. → `baseline-board-ui-summary.txt`.

## 3. Diff and the ledger

```
 tools/probe_board_ui/fakes/Arduino.h        | 14 ++++-
 tools/probe_board_ui/run.sh                 | 96 +++++++++++++++++++++--------
 tools/probe_inbox_verbs/transcript_main.cpp |  2 +-
 tools/probe_ui_model_mutations.py           | 15 +++--
 4 files changed, 94 insertions(+), 33 deletions(-)
```

- **run.sh.**
  - `DISPATCH_SIG` is now the current prefix (`…, CommandTransport transport) {`; `fn_body` matches by
    `index()==1`).
  - W49's fifth control is the all-or-nothing relocation (§2.1).
  - W51 has the seam and flush clauses, counted as occurrences, plus the unchanged ban, with four controls (§2.2).
  - W54 now has five tokens and controls 1/2/3/5, and its label drops "help".
  - New `HELP_H` and **W54-help** (§2.3).
  - §2.4 comments: the dated W49–W51 intro history note, the W49 relocation note, and W51's seam/S23/S24 text.
    The W54 header's "only instrument" claim is corrected with H3a and the inventory comparison. The W50 claim
    ("unconditional call, gate in `firmware_commands.h`'s inert stub") is corrected where it was false: W54's ⓘ
    line.
  - Every re-anchored check prints a counts line.
- **Arduino.h** (B451): the false sentence is replaced by the §2.4 fidelity note. `HEX` prints decimal. The note
  covers board-UI, firmware-UI, inbox-verbs X21–X26 on both arms (12 checks), and the transcript comparator. S27 is
  the dh/lp radix witness, and **nothing pins `whoami`'s radix (B460)**. Comments only.
- **transcript_main.cpp:** `structural.py` S24 → **S27**. Comments only.
- **Harness** (B458): the active M103 tuple and its explanation are replaced by a dated retirement record in the
  §CUSTODY-B style. The record carries the ID, the old edit, the reason (`go_menu_home`'s `_st.cursor = 0` and
  `settings_follow_screen`'s `_cfg_sel_valid = false`, plus B457's closed preview), and the witnesses
  M100/M101/M102/M105 and H25.

**The §2.6 ledger with the old → new ordinal map** (`ordinal-map.tsv`/`.json`, derived from source, base versus final):

| Check | New | Old | Disposition | Script vs old | Must fail (§2.6) |
| --- | --- | --- | --- | --- | --- |
| W49 | 1 | W49#1 | kept | identical | the arm's file-wide presence |
| W49 | 2 | W49#2 | kept | identical | the exact `line + 2`/`len - 2` arm |
| W49 | 3 | W49#3 | kept | identical | the handler's presence |
| W49 | 4 | W49#4 | kept | identical | the exact guarded arm |
| W49 | 5 | W49#5 | **re-anchored** (all-or-nothing) | new recipe | **only** the in-`dispatch()` clause |
| W49 | 6 | W49#6 | kept | identical | the guarded arm |
| W51 | 1 | W51#1 | **re-anchored** | new anchor | the three-token ban |
| W51 | 2 | W51#2 | **re-anchored** | new anchor | the three-token ban |
| W51 | 3 | W51#3 | **replaced** | delete the seam line | the seam-call clause |
| W51 | 4 | — | **new** | flush dropped | the flush clause |
| W54 | 1 | W54#1 | kept | identical | the include outside a region |
| W54 | 2 | W54#2 | kept | identical | the binding's tokens outside a region |
| W54 | 3 | W54#3 | kept | identical | the status token outside a region |
| W54 | 4 | W54#5 | kept | identical | the instance's presence |
| W54-help | 1 | W54#4 | **re-anchored** from W54 | new three-line block | **only** the region clause |

- **The rest of the runner.** Every one of the other 56 checks is byte-identical to the base: label, file,
  predicate name and all **171** control scripts.
- **Checks.** 59 → **60**; the only new check is W54-help. Declared controls go 185 → 186 (14 of the base's 185 were
  the dormant W49/W51/W54 ones). Every old control has a successor.
- **Labels changed.** W51: "…keeps the shared dispatch fallback" → "…keeps the ONE shared seam call". W54: "help"
  dropped. The W49 label is unchanged.

## 4. Figures (the §4.1 chain, in order; nothing else ran between or during the steps)

| Step | Instrument | Result |
| --- | --- | --- |
| 1 | scope and hygiene (`scope.py` → `step1-scope-start.json`) | **PASS** — `git diff --check` clean; 8 changed = 4 fence + 4 preparation, 0 unexplained, 0 missing; new = explained only; read-only 19/19; preparation 6/6; simulator `6585649` clean |
| 2 | comment-only (`comment_only.py` → `step2-comment-only.json`) | **PASS** — Arduino.h: 1 hunk (−2/+12), all comment lines, delta reverted = HEAD byte-identical, preprocessed tokens 735 = 735. transcript_main.cpp: 1 hunk (−1/+1), = HEAD, tokens 342 = 342. Control: one executable byte changed ⇒ tokens differ |
| 3 | M103 by AST (`m103_proof.py` → `step3-m103-ast.json`) | **PASS** — no active M103 anywhere; `MUTS_MODEL` 239 → **238**; AST(final) = AST(base minus M103), so every other entry, target and worker line is identical; one hunk, added lines all comments, removed code = the M103 tuple only; record 4/4 items; witnesses resolve live |
| 4 | board-UI stock default run | **exit 0**; V3 124/124, V4 110/110, traits 14, missing 12, structural 23/23, wiring **60/60**, **186** controls RED, negctl 60 + 3 (23 s) → `final-board-ui-summary.txt`; reconciliation and per-control proofs in §5 |
| 5 | console-sink stock default run | **exit 0** (68 s): `PINS profiles=6 checks=720 structural=84 ble_guard=905 ownership=6 ownership_controls=3 controls=152 unusable_controls=0`; S23, S24 ok; X14/X15/X16 → S23 FAIL, X17 → S24 FAIL. Scratch replay of the exact edits (`console_sink_replay.py`): X17 (deletes the seam's `dispatch(...)` router call) reddens **S24, S53, S62**; X14–X16 redden S23 only; each anchor ×1 → `step5-*` |
| 6 | `python3 tools/probe_ui_model_mutations.py --target=model` | **exit 0**: **238 RED / 0 unusable**, every entry match count 1; 8/8 worker baselines **3031 / 198613 / 0**; real tree untouched; declared 238 IDs = executed 238 RED IDs, no M103 (6 m 58 s) → `step6-model-summary.txt` |
| 7 | `python3 -m unittest discover -s tools -p 'test_*.py'` | **Ran 375, OK**, 0 skipped, 0 import failures (736 s). The printed "FAIL: §B278 census selftest, 9 failure(s)" is `test_the_selftest_is_not_vacuous`'s deliberate negative, present in every earlier discovery log → `step7-discovery-summary.txt` |
| 8 | `python3 tools/gen_command_inventory.py --check` | **PASS**, 197 command rows, byte-identical |
| 9 | reader audit (`reader_audit.sh` → `step9-reader-audit-greps.txt`) | dispositions below |

**Step 9 — every reader, and whether W2 touches its predicate:**

- **The runner, by path** (10 files): comment pointers in `src/`/`test/` to W1–W4, W2, W3, W6, W10, W24 and W47, a
  docstring in `probe_build_identity.py`, and `probe_firmware_ui/run.sh` comments. None names W49/W51/W54, and
  **no tools test opens the runner or its fakes**. Not touched.
- **Labels and IDs:** W49/W51/W54/W54-help occur only in `run.sh`. The old W51/W54 label texts now occur nowhere
  (0 files). No reader pins a label. Not touched.
- **Anchors** (production text, read only):
  - console-sink `negctl.py` reads the dispatch signature (`:211/:218`) and the streamed line (X15 `:278`; the
    ACCEPT/CLIENT guard insertions `:918`/`:1101`) as its own anchors.
  - `firmware_help.h` has many readers (console-sink, `probe_features/ownership.py`, `gen_command_inventory.py`
    and its test, `test_probe_console_sink.py`'s md5 of the file).
  - W2 edits none of these files, and steps 5, 7 and 8 re-ran their readers green.
- **M103:** its only occurrence is the retirement record. The harness's executable readers are
  `test_worker_formula_derived.py` and `test_mutation_unusable_reason.py`; step 3 proves the code identical, and
  both pass in step 7. The "239 entries" comments at `:628/:642` are the dated 2026-08-30 measurement and are kept
  (§2.5). No reader pins the model cardinality.
- **The fake's note:**
  - The old sentence ("ACCEPTED AND IGNORED", "Nothing this probe") has 0 readers.
  - The fake's consumers (board-UI, firmware-UI, the inbox-verbs probe and its transcript build) compile it, and
    its tokens are identical (step 2).
  - The two in-run `md5_sources` lists (inbox-verbs `run.sh:277–289`, firmware-UI `run.sh:192–197`) hash the
    fake before and after within one run and **pin no hash**.
  - console-sink, BLE-line and custody-USB use the separate console-sink fake.
- **The transcript locator:** its reader `transcript.py` compiles `transcript_main.cpp` (tokens identical), and
  `probe_main.cpp:396` refers to it in a comment. **No in-run hash list includes it**: the inbox-verbs
  `md5_sources` definition does not list it. The old "`structural.py` S24" text has 0 occurrences.

## 5. Reconciliation and per-control proofs (B459 is open, so the runner's summary is not trusted)

- **Reconciliation** (`reconcile.py` + `declared.py` → `step4-reconciliation.json`/`.tsv`).
  - **Declared side:** read from the runner **source**. Each column-0 `wchk`/`wchk_in` call is tokenized by
    bash's own parser.
  - **Executed side:** an **xtrace of the stock runner** (`PS4='+${FUNCNAME[0]:-main}@${LINENO}|'`,
    `BASH_XTRACEFD`; exit 0, same totals) plus that run's output.
  - **Result: PASS.**
    - Wiring: **60/60** declared checks executed once each and passed. **186/186** declared controls executed,
      each script byte-identical to the declared one and each followed by its RED increment. Trace final
      `w_ctl=186`, `w_pass=60`, no `w_fail`.
    - Structural: **23/23** across 8 call sites, three of them loops (3 + 3 + 12), with loop cardinalities derived
      by bash from the literal lists.
    - Traits: **14/14**. Missing-trait controls: **12/12**. negctl: `MUT_V3` **60/60** and `MUT_V4` **3/3**,
      each printed once, in its own arm's section, with its fail line.
    - Nothing missing, duplicated or unknown.
- **Self-controls on the reconciler.** It fails when W1's executed invocation is dropped from a trace copy
  ("executed 0 times"), and when one `w_ctl` increment (W54-help's) is dropped ("controls_red 0").

**Per-control proofs** (`per_control.py` → `step4-per-control.json`, mutant diffs in `mutants/`).

- **Method.** Each control uses the runner's own declared script, applied in scratch. Each check is evaluated
  with the runner's **extracted** helpers and predicate, and its clauses are then evaluated one by one.
- **Live.** Every clause holds on the real files.
- **Clauses per check:**
  - W49: A = arm line once in the file; B = guarded arm inside `dispatch()`.
  - W51: S = seam once; F = flush once; N = no `"ui`/`handle_ui`/`preset_`.
  - W54: C*k* = token *k* once; G*k* = token *k* inside a region, where the tokens are 1 include, 2 instance,
    3 boot restore, 4 `handle_ui`, 5 status.
  - W54-help: C = row once; G = row inside a region.

| Control | Anchor matches | Mutant differs (hunks) | Predicate RED | Clauses on the mutant | §2.6 clause fails |
| --- | --- | --- | --- | --- | --- |
| W49#1 | 1 | yes (1) | yes | A 0, B 0 | A ✔ |
| W49#2 | 1 | yes (1) | yes | A 0, B 0 | B ✔ |
| W49#3 | 1 | yes (1) | yes | A 0, B 0 | A ✔ |
| W49#4 | 1 | yes (1) | yes | A 1, B 0 | B ✔ |
| **W49#5** | 1 (destination) + 1 (block) | yes (2: move) | yes | **A 1, B 0**; guarded arms: dispatch 0, `handle_teststatus` 1 | **only B** ✔ |
| W49#6 | 1 | yes (1) | yes | A 1, B 0 | B ✔ |
| W51#1 | 1 | yes (1) | yes | S 1, F 1, N 0 | N ✔ |
| W51#2 | 1 | yes (1) | yes | S 1, F 1, N 0 | N ✔ |
| W51#3 | 1 | yes (1) | yes | S 0, F 1, N 1 | S ✔ |
| W51#4 | 1 | yes (1) | yes | S 1, F 0, N 1 | F ✔ |
| W54#1 | 1 | yes (1) | yes | G1 0, all else 1 | G1 ✔ |
| W54#2 | 1 | yes (1) | yes | G2 0, G3 0, G4 0 (the binding region's three tokens), all else 1 | G2 ✔ |
| W54#3 | 1 | yes (1) | yes | G5 0, all else 1 | G5 ✔ |
| W54#4 | 1 | yes (1) | yes | C2 0, G2 0, all else 1 | C2 ✔ |
| **W54-help#1** | 1 (the three-line block) | yes (1) | yes | **C 1, G 0** | **only G** ✔ |

**The W49 relocation in both directions (W2R-1):**

- **The recipe.** A single whole-input sed: `:a;N;$!ba;` followed by one `s@\n\(<handle_teststatus signature>\n\)\(.*\n\)\(<the complete guarded block, marker included>\)@\n\1\3\2@`.
- **On the real file** it moves the three lines to the top of `handle_teststatus`. One arm remains in the file,
  none is inside `dispatch()`, and only B fails.
- **Destination signature renamed** (its own scratch copy): the control's output **equals its input**, which
  `wchk_in` reports as vacuous, and live W49 is **GREEN** on that copy.
- **Source marker comment changed** (its own copy; a comment-only edit, invisible to W49): the output **equals its
  input**, and live W49 is **GREEN**.
- **For the record, the retired recipe on this base** is deletion-only. It drives A = 0 and B = 0, so W49 fails on
  the presence clause, not placement — the B418 shape.
- **Other properties.** The runner's declared recipe gives byte-identical output in the C and UTF-8 locales, and it
  is structural only: no mutant was compiled.

## 6. The pin line

PIN re-synced? YES — native 3031/198613 unchanged (8/8 model workers' clean baselines, step 6); model 239 − 1 retired M103 = 238 configured, 238 RED (step 3 AST, step 6); wiring checks 59 + 1 (W54-help) = 60 (step 4); wiring controls 171 + 6 (W49) + 4 (W51) + 4 (W54) + 1 (W54-help) = 186 (step 4, reconciled 186/186)

## 7. Freeze inventory (`freeze-inventory.json`)

| Fenced file | Base SHA-256 | Final SHA-256 | Lines |
| --- | --- | --- | --- |
| `tools/probe_board_ui/run.sh` | `a458be2b…cd6c` | `0ee0bc90df8d8a6e1d5adce73958f2dd32061388c0f3644f70d9e1f88ce56fe8` | 1428 → 1474 |
| `tools/probe_board_ui/fakes/Arduino.h` | `4655c951…08e4` | `deaa9f4a6f3d010fa4010722973c701c4f5effadb2442213812b7884f85ab853` | 152 → 162 |
| `tools/probe_inbox_verbs/transcript_main.cpp` | `cac89791…39af` | `3914de10acddb6a6e39384fa4f512461133e1d7ca538f50cb66939a8cd828163` | 148 → 148 |
| `tools/probe_ui_model_mutations.py` | `71c860f9…687e` | `81842cc53d806f1ed4dbce5bb7699a945e50dc6381f8258cf3c60eec73e2781a` | 12694 → 12699 |

- **Brief:** `341d4bc0c401c8fd254107eea90915cefb2dee94580cdcaf912a99f015c89737`, unchanged.
- **Simulator:** `6585649ea5a780f0542b2931853a667be56a5b2b`, status clean.
- **Preparation set, unchanged since preflight:**
  - design `a7b0515e…6428`;
  - register `56edcd1a…5986`;
  - pre-check report `e235319a…1a24`;
  - pre-check `SHA256SUMS` `38d5a482…1169`;
  - `tracker.md` `a40d074d…3f5c`;
  - `MEMORY.md` `cb10bdcd…b33b`.
- **Inventory check** against the pre-check `inputs.json`: 8 changed (4 fence + 4 preparation), 0 unexplained,
  0 missing. The new paths are the 49 explained preparation paths, this report and the evidence folder. Read-only
  19/19. The simulator has 0 changed and 0 new.
- **New evidence paths:** every file under `2026-09-28-standalone-mobile-home-w2/` is hashed in its `SHA256SUMS`.
  This report is the only other new path.
- **Stability.** Inputs were recorded at preflight, before the baseline, and again after the edits, before step 1
  (`step1-scope-start.json`). The final snapshot, taken after step 9 (`stability-end.json`), matches the step-1
  snapshot on the fence hashes, the preparation, the read-only set and the changed-file set, and every
  pre-existing explained file is unchanged. The chain added only this evidence folder; nothing changed during it.

## 8. Not run, with reasons (§4.2)

- **Native, corpus, board builds and measurements, ABI and the warning census.**
  - Step 1 proves that no product, build or test input changed: the only edits are four tool files, two of them
    token-identical (step 2).
  - The native suite still ran in isolation inside every model worker: 3031/198613/0, 8 times.
  - The reader audit finds no reader of the edited text among their inputs.
- **The firmware-UI and inbox-verbs probes.**
  - The only edits they compile are comments — the shared fake (both probes) and `transcript_main.cpp`
    (inbox-verbs' transcript tool) — and step 2 proves them token-identical.
  - Their only readers of these files are the compiler and the in-run `md5_sources` before/after comparison,
    which pins no hash.
  - Board-UI compiled the shared fake in step 4 anyway.
- **Out of scope, and not done:** B459's completeness accounting and total pins (this report's reconciliation
  compensates), B460's `whoami` radix pin, B453, B443, command-inventory regeneration, and the metal plan (W2 has no
  metal behaviour).

**Rulings I made (each within the brief; cost if wrong):**

1. **W51's label.** It changed from "keeps the shared dispatch fallback" to "keeps the ONE shared seam call",
   because §2.4 has W51 describe the one seam call where it described the old fallback, and the label was such a
   place. No reader pins it (step 9). Cost if wrong: one label string.
2. **The W49 relocation's destination** is the top of `handle_teststatus`'s body, directly after its column-0
   signature (anchored by a leading `\n`). Cost if wrong: the placement inside the neighbour, which no clause
   reads.
3. **W54-help counts "exactly once" in lines** (`grep -cF … = 1`), W54's own idiom (U3). Cost if wrong: two `ui`
   rows on one physical line would count once — a shape the help index does not use.
4. **The W49–W51 intro keeps its dated history** and gains a dated correction beneath it, rather than an edit of
   the 2026-08-25 text. Cost if wrong: comment length only.
5. **transcript_main.cpp's "The fake is NOT edited" stays.** It is about the fake's numeric behaviour, which W2
   leaves unchanged, and the brief's delta there is the one locator. Cost if wrong: one comment sentence.

**Observation, not edited (outside §2.4's list):** W50's comment says "Control (c) is the plausible SIMPLIFICATION",
but that is W50's *second* control. The lettering drift predates W2.
