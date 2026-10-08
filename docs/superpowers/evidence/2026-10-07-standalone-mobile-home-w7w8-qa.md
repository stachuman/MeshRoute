<!-- Author: Claude (the brief's author, performing the QA gate at the owner's request, 2026-10-07); a separate coder implemented; the owner rules and commits. -->
# W7+W8 — QA implementation gate

**Verdict: SOFTWARE QA PASS.** The complete §4.2 gate of the authorized
[brief revision 2](../plans/2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md), with the
[checkpoint return](2026-10-06-standalone-mobile-home-w7w8-stop-resolution.md), was rerun on the coder's frozen
candidate. **B480, B481, B498 and B499 close in place.** B444 stays open for non-UI senders; its panel path is now closed
by D19. B500 stays open and nonblocking. One new finding, **B501** (LOW, latent, nonblocking), is registered below. No
production, test, tool, brief or coder-evidence file was edited. Nothing was staged or committed; the simulator is
unchanged.

> **Independence is reduced, and this receipt says so.** Codex could not perform this gate, so at the owner's request
> it was run by the brief's author. To compensate, QA re-executed every instrument on the frozen tree with its own
> run script, comparators and collector, added a cold, cache-free native rebuild, and took no figure from the coder's
> report as a result. The remaining risk: the code was reviewed against a contract written by the same author, so a
> misreading shared by the brief and the code would not be caught here. The owner may still ask Codex to re-gate.

## 1. Frozen inputs and scope

- MeshRoute `4c1a000bc71706ac438e64161a9452966a66615f` plus the working tree, nothing staged; simulator
  `6585649ea5a780f0542b2931853a667be56a5b2b`, clean.
- Brief revision 2, **732 lines**, `47cb5a461d07cc3cbaf0ab6ae81fe535e206eda996201885f4dbe878dd11cb8e`; checkpoint return
  `8eabbaa9f973c74766acffb63a85cad4460c320220e772453d25fbfb8ad8eb0e`. Both unchanged.
- [Coder report](2026-10-04-standalone-mobile-home-w7w8.md) `f67784128c63560dc30e225a651f5c0ee5c5df251d305db8435b07b0bf68cb89`;
  its evidence seal verifies **47/47**.
- All **17 frozen files** match the coder's freeze inventory, including the two new ones (`src/firmware_ui_editor.h`
  `c197a80c…`, 367 lines; `test/test_firmware_ui_editor.cpp` `d99819f4…`, 523 lines). The 24 read-only dependencies
  match the brief. The preparation files match: design `7ed161d6…`, register `5ad4e8ac…` (the return's authorized
  hash), tracker `a0b5231a…`, MEMORY `f8c41ba2…`.
- Against the return's **2,569-path** checkpoint inventory, seven paths changed: the register (at the return's
  authorized hash), the two guarded tests, the coder's scope checker (a named delta) and three more of the coder's own
  evidence files (`changed_cases.py`, `reader_audit.py`, the ledger). The **43 added** paths are the coder's report
  and 25 evidence files, plus the checkpoint receipt and its 16. Nothing is missing.
- B498's guards: reverse-applying [guards.patch](2026-10-06-standalone-mobile-home-w7w8-stop-resolution/guards.patch)
  to the frozen tests gives back exactly the checkpoint hashes (model `95f6ead0…`, presets `8811f177…`).
- QA's own inventories of **2,612 MeshRoute and 285 simulator paths**, taken at the start and the end of the run, are
  identical ([stability](2026-10-07-standalone-mobile-home-w7w8-qa/stability.json)).

## 2. Independent runs

Every row is from this QA run. Commands and exits are in [runs.tsv](2026-10-07-standalone-mobile-home-w7w8-qa/runs.tsv);
the [collector](2026-10-07-standalone-mobile-home-w7w8-qa/results.json) reads every raw log, which stays under the
ignored `artifacts/2026-10-07-standalone-mobile-home-w7w8-qa/` and is hashed there.

| Instrument | QA result |
| --- | --- |
| `pio test -e native`, then `./.pio/build/native/program` | **3123 cases / 203168 assertions / 0 failed / 0 skipped** (the build was already current) |
| Cold rebuild: a fresh copy of the tree, `CCACHE_DISABLE=1 pio test -e native`, then its binary | full compile; **3123 / 203168 / 0 / 0** |
| Fresh stock `lus` | `e304147d99ae166fb815e6a2ef06c5ff905579b68add3b0ba142d7031e26caa2` |
| Corpus, `--require-anchors` | **36/36**, all **14 fields** identical to the coder's baseline and final manifests; s18 **269517** events, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`** |
| QA board pair, `--jobs=1`, `.pio-measure/w7w8/qa-1` | every measurement, payload, symbol, toolchain, identity and path field equal to the coder's final-1 **and** final-2 (§3) |
| Stock ABI | **290 checks, 9/9 controls RED, 0 unusable** |
| ABI with the coder's supplemental pins | **335 checks, 9/9 controls RED, 0 unusable**; every §2.2 size (below) |
| Firmware-UI default | **656 / 1130 / 656**, zero failures; **263 controls**, each accounted once with its accepted outcome; all 265 verdict lines identical to the coder's |
| Board-UI default | **603 identities**, each once; live wiring **62 × exit 0**, mutants **195 × exit 1**; V3/V4 controls **60/3**; the whole log identical to the coder's |
| Mutation, selector (a) + `uieditor` | **449/449 RED**, zero unusable or vacuous, every entry matched once; **29 worker baselines**, all **3123/203168/0** (§4) |
| B498's five entries | all RED, zero unusable (§4) |
| B499: the coder's proof against the real runner | fault exit **1**, only the failed row, no DONE; healthy exit **0**; the stop-removed control is caught |
| Tools discovery | **447 tests OK**, no skips |
| Command inventory `--check` | **197 rows**, byte-identical |

**ABI sizes** (host / heltec_mobile / gateway ABI): UiState **576 / 568 / 568**, UiModel **1216 / 1200 / 1200**,
SendReq **20**, Draft **176/4**, EditorView **10/1**, NameOrigin **1/1**, WrittenOutcome **4/1**, SendLive **12/4**;
unchanged UiSnapshot **1368** and UiChrome **20**. The native `w8-resources` case pins SendTracker **16** and InputFsm
**28**.

PIN re-synced? **YES** — approved base 3059/199638 + 64 cases (editor 17, model 16, send 30, chrome 1) / 3530
assertions = 3123/203168, executed twice here (shared and cold) and by every mutation worker; corpus 36/36; §2.2's
sizes on all three ABIs; firmware-UI 656/1130/656 with 263 controls; board-UI 603; selector (a) + `uieditor` 449;
discovery 447; inventory 197.

**One environmental abort, disclosed.** The first mutation step (`model`, eight workers) stopped with exit 9 and
**no verdicts**: each worker's clean tree failed to build with `Errno 28 — No space left on device`. The harness copies
the ignored `.pio-measure/` (2.4 GB) and `artifacts/` (1.7 GB) into every scratch tree, and six stale scratch roots
from earlier runs (2026-09-29 to 10-02, about 34 GB) still occupy `/tmp`; QA did not delete files it did not create.
The harness refused correctly and reported nothing. QA reran the same batteries with `--workers=4`; the harness
records its verdicts as identical at every worker count (its own 1/2/4/6/8 measurement). The aborted log is kept as
`enospc-mut-model.log`, and the collector accepts the superseded row only because that log shows `Errno 28` with no
clean baseline and the step then reran with exit 0.

## 3. Boards

| Env | RAM | Flash | Objects | Symbols | Payload SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| gateway | 203740 | 572096 | 285 | 6599 | `7fa27c2b884cf4b4b9869b3c9937ef264e40cd9369c5926c006aaf64f3fb837d` |
| heltec_mobile | 219748 | 1407844 | 329 | 13380 | `85fba0e0e8e783b51208b7198ef1ef38b940a001825576b70faa2d1af1e0aad2` |

Against the coder's base: gateway unchanged (its image has no OLED TU); heltec_mobile **+208 B linked RAM** and
**+7304 B flash**. QA's own symbol comparison finds exactly two changed RAM objects: `s_model` 1000 → 1200 (+200) and
`s_frame_state` 560 → 568 (+8). That is D16 + D18 exactly, with no alignment remainder. The warning count of each
build is identical at base, coder final and QA (gateway 18,692; heltec_mobile 175).

**Provenance, explained.** The stock `compare` of QA's manifests against final-1 fails, as expected, on the source
fields and `normal_pio_metadata.sha256` only ([board-fields](2026-10-07-standalone-mobile-home-w7w8-qa/board-fields.json)).
[board-provenance](2026-10-07-standalone-mobile-home-w7w8-qa/board-provenance.json) explains the source fields:
rebuilding the stock snapshot digest over the coder's chain-start path set (the checkpoint inventory plus the 18
paths its scope-start recorded), with the ledger at its recorded start hash, gives final-1's `tree_sha256` and
`git_status_sha256` exactly. Today's tree adds only the coder's report and 24 evidence files, written after their
chain, and the ledger's post-chain update — no build input. `normal_pio_metadata` records the mtimes of the ordinary
`.pio/`, which later native and probe builds touched. Neither is a measurement.

## 4. Mutation reconciliation

| Battery | Configured | RED | Unusable / vacuous | Worker baselines (all 3123/203168/0) |
| --- | ---: | ---: | ---: | ---: |
| `model` | 297 | 297 | 0 | 4 |
| `chrome` | 50 | 50 | 0 | 4 |
| `uisend` | 40 | 40 | 0 | 4 |
| `sliceCbudget` | 1 | 1 | 0 | 1 |
| `sliceCsend` | 1 | 1 | 0 | 1 |
| `w4aident` | 9 | 9 | 0 | 4 |
| `w4bhome` | 26 | 26 | 0 | 4 |
| `uieditor` | 25 | 25 | 0 | 4 |
| `uipresets` U13 (B498, `--workers=1`) | 1 of 39 | 1 | 0 | 1 |
| `uipresets` W6-P1 (B498, `--workers=1`) | 1 of 39 | 1 | 0 | 1 |
| `uipresets` W6-P3 (B498, `--workers=1`) | 1 of 39 | 1 | 0 | 1 |
| **selector (a) + `uieditor`** | **449** | **449** | **0** | |

Model includes **B480** (RED, 3 failed assertions) and B498's **Y02** (266) and **Y07** (3). B498's
uipresets entries, run singly on the serial reference path: **U13** 165, **W6-P1** 7, **W6-P3** 10 failed
assertions, each RED with a clean baseline of 3123/203168/0. Every battery reported the real tree untouched.

## 5. Source review and findings

The contract review, item by item, is in [source-review.md](2026-10-07-standalone-mobile-home-w7w8-qa/source-review.md).
Every §2.1–§2.7 item is met, including the note row (W7W8R-1), D19 at execution only with review admission untouched
(W7W8R-2), the one-call name seam outside the busy gate, the written lock and binding rules, the 18-value failure
classification, and the withdrawal by kind.

### Findings

**B501 — OPEN / LOW / LATENT, nonblocking.** `mr_ui_tick`'s alarm drain still reads
`s_tracker_normal.kind() != mrui::SendKind::dm`. Since W8 that is true for `dm_text`, so a written DM's tracking would
be abandoned at an alarm like a channel post. The brief review had named this consumer; the coder's reader audit gave
per-file counts and did not classify it. **Reachability:** such a transaction can be in the normal tracker when the
emergency becomes pending only on the `long_fire` tick that also releases the written result, so the next
`ui_pump_trackers` closes it anyway; a queued written request is withdrawn first, and no written request can start
or drain under a live alarm. No frame differs. Close by spelling it through `send_kind_dm` at the next authorized
touch of `firmware_ui.cpp`, with a control, and by fixing the two tracker comments that still say `_k == dm`.

**No finding:** `src/firmware_ui_presets.h:88` (read-only here) says `SendReq` carries `{slot, generation}`; still true
of the phrase identity it describes. **Carried:** the brief's rationale "no note fits beside `163/163`" is too broad
(FULL/EMPTY/BUSY fit); the rule is right and implemented; correct it at a future authorized documentation refresh.

## 6. Reader and preservation checks

[reader-audit.json](2026-10-07-standalone-mobile-home-w7w8-qa/reader-audit.json): outside the fence, every reader of a
changed symbol across `lib/`, `src/`, `test/` and `tools/` is a comment, except `test_firmware_ui_team.cpp`'s
`to_menu_home` fixture, which acts only on Home's list view and is unaffected (the native suite passes). Inside the
fenced production sources, all 17 remaining exact `SendKind::dm` / `channel_canned` spellings are classified: 16 are
phrase-only paths after a written early return, or the family helpers themselves; the 17th is B501. `SendReq`
aggregate initializers stay positional, so the appended `draft_id` is 0 wherever it is not named.

QA wrote only this receipt, its evidence folder and the landing below. [Preservation](2026-10-07-standalone-mobile-home-w7w8-qa/preservation.json)
compares the tree after landing with QA's start inventory.

## 7. Landing (brief §7)

- **Register:** §0 rewritten in place; **B480, B481, B498, B499 CLOSED**; B444's status records the closed panel path;
  B500 records this gate's control reconciliation and stays open; **B501** added; next free **B502**.
- **Design §13:** the W7 and W8 rows carry this PASS.
- **Pointers:** `tracker.md` and `MEMORY.md` updated in place (the tracker's stale "revision 2.24" link text now
  reads 2.27).
- **Metal plan (M2):** new **EDIT-01** and its result row; added steps in UI-04 and UI-06 (the unnamed prompt), UI-13
  (labels after a rename), UI-15 (written messages and the D19 line), UI-16/UI-17 (alarm over the editor flows), UI-19
  (gateway rename) and POWER-01 (typing, blank/wake and sleep). All OWED.
- **Next:** the owner's pick — W5, W9, W4c→W4d, W1b, or the open follow-ups.

## 8. Not run here

The coder carries the full union's selector (b) beyond B498's three entries, the warning census and the two
dependency safety nets (§4.2); QA did not rerun them. The 109-battery union and metal were not run by either side.

Evidence seal: [SHA256SUMS](2026-10-07-standalone-mobile-home-w7w8-qa/SHA256SUMS).
