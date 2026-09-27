<!-- Author: Claude (Opus 5.5), W3 coder — implementation receipt for QA's independent gate; the owner rules and commits -->
# Standalone Home W3 — UI-model seams (pager, compose width, PROVISION admission) — coder receipt

**Status: IMPLEMENTED, CODER GATE (§4.1, steps 1–9) GREEN, READY FOR INDEPENDENT QA (§4.2).** Nothing is committed,
staged, reset or cleaned. The simulator was only read. No STOP condition fired, and I found no semantic disagreement
with the brief.

- **Contract:** [W3 brief revision 2](../plans/2026-09-25-standalone-mobile-home-w3-ui-model-seams.md), SHA-256
  `9a84d03b0f2e9826df67feba52ff87c50644383bfb85ef8bc06af7ed8e25d825`. I verified it first; it is unchanged at the end.
- **Authorization:** [QA brief review](2026-09-25-standalone-mobile-home-w3-brief-review.md) (PASS; fold-ins W3R-1–W3R-3
  are in revision 2).
- **Evidence:** [`2026-09-25-standalone-mobile-home-w3/`](2026-09-25-standalone-mobile-home-w3/), with a `SHA256SUMS`
  over every file. Raw logs stay in the ignored `artifacts/2026-09-25-standalone-mobile-home-w3/` (brief §5, evidence
  README); [`raw-logs.txt`](2026-09-25-standalone-mobile-home-w3/raw-logs.txt) names each one with its SHA-256. Board
  runs stay under `.pio-measure/w3/`, and the stock per-env roots under `.pio-measure/env/` were not moved. Simulator and
  mutation scratch builds stayed outside both repositories.

## 1. Preflight

| Check | Found |
| --- | --- |
| MeshRoute | `HEAD` `8360802904f7bd0023279d3da844453d61207ede`, `main`, plus the uncommitted W1c candidate |
| Simulator | `6585649ea5a780f0542b2931853a667be56a5b2b`; `git status` empty at preflight and at the end |
| Brief | `9a84d03b…d825`, equal to the dispatch hash |
| Executable inputs 5/5 + the pinned consumer | Each disk hash and line count equals the brief's §1: model `98b1992a…` 5726, native test `bb4650ab…` 9936, `probe_main.cpp` `887df0c2…` 7052, `run.sh` `cf18d721…` 1702, harness `336e4c13…` 12438 (the W1c candidate), `firmware_ui.cpp` `6fe8c435…` |
| Preparation set 6/6 | design, register, pre-check, pre-check `SHA256SUMS`, `tracker.md`, `MEMORY.md`: every hash OK |
| Pre-check folder | `sha256sum -c`: **23/23 OK** |
| Explained addition | The review receipt `05511a43…` and its folder (`SHA256SUMS` `f26fb5d9…`, **12/12 OK**) |
| Inventory (`inputs.json`, 1,352 paths) | 0 missing; exactly the 4 permitted preparation documents changed; 40 new paths = the review (14), the pre-check (25) and the brief (1). The simulator's 285 paths are unchanged. |

Receipts: [`preflight.txt`](2026-09-25-standalone-mobile-home-w3/preflight.txt),
[`stability-start.txt`](2026-09-25-standalone-mobile-home-w3/stability-start.txt).

**Source validation (P4).** Every §1 fact held by symbol before the first edit:
- the detail geometry is 19 × 2 = 38, with 7 maximum pages and 2,000 ms;
- `on_inbox_opened` counts pages by ceiling, and an empty body gives 1 page;
- `refresh_detail_page` copies counted bytes, writes one NUL per row and clears nothing;
- `ComposeSlot::text` is `kUiPresetTextMax + 1`, and `compose_project` clamps at `kUiPresetTextMax`;
- `sync_settings` opens the service before the closed-view return;
- the `CfgRow::provision` arm checks, in order: no service or not open, conflict, unsaved, then enter.

The renderer's rows are body row 1/2 (detail), row 4 (note) and row 2 (`CFG UNAVAILABLE`).

## 2. Baselines (unmodified tree, before stage A)

- **Firmware-UI probe, default:** l2 **467/467**, v3 **902/902**, BLE **467/467**; **225 controls verified / 0 unusable**;
  coverage 733/874.
- **Board-UI `--no-neg`:** exit 1 with exactly **W49, W51, W54** failing (B418, W2's); structural 23/23; wiring 56/59;
  V3 124/124; V4 110/110.
- **Boards:** `pair --jobs=1` into `.pio-measure/w3/base-1`, then `base-2`, chained with nothing between them.

  | env | RAM | flash | objects | payload SHA-256 |
  | --- | --- | --- | --- | --- |
  | `gateway` | 203,740 | 571,936 | 285 | `97deca60…` |
  | `heltec_mobile` | 211,724 | 1,394,592 | 329 | `38739c4a…` |

  Stock `compare base-1 base-2`: **PASS `gateway`, PASS `heltec_mobile`**.

## 3. Stage A — instruments first, against the unchanged model

Receipt: [`stage-a.txt`](2026-09-25-standalone-mobile-home-w3/stage-a.txt). Every new check passed and every new control
went RED while `src/firmware_ui_model.h` was still `98b1992a…` (HEAD).

**Firmware-UI fixtures (`probe_main.cpp`).**
- **P3u** runs on every arm; it covers the *unavailable* service. P3's `settle` is the binary's first SETTINGS arrival.
  I measured the fake store's load count at 0 at P3 and 1 at P4, so the service is still closed there. The fake
  refuses every load across P3, whose checks count frames only. P3u then checks four things:
  - exactly `CFG UNAVAILABLE` on body row 2, with no marker, entry row or menu;
  - a `double` wrote nothing and every sync retried the load;
  - once the store answers, the next sync opens it;
  - the entry row replaces the notice with exactly one more load.
  The press is a `double`, so P4 and P5 start where they always did.
- **P29a** (every arm) covers the detail pager on glass for bodies of **0, 38, 39, 76 and 241 bytes**. The body bytes
  cycle through 36 printable characters, so no two row slices are equal. On a continuous 10 ms clock, every page's
  header (`DM from 51     p/N`) and both body rows are exact, empty and partial rows included:
  - single-page bodies stay `1/1` across two cadence deadlines;
  - multi-page bodies turn every page and cycle back to page 1;
  - blank and wake happen on page 2 for multi-page bodies (page 1 for single-page) with the page kept and `back` still
    selected.
  A frame is forgotten before any "unchanged" read, so only a frame painted after the event can answer.
- **P29b** (every arm) covers the compose row at the display width: 17-byte DM and channel phrases, located and plain,
  selected and unselected, with the BACK row exact. The phrases are set through the real `preset` verbs, and the phase
  ends with `preset reset all`.
- **P29c** (v3 only, the child-enabled arm) covers the blocked PROVISION on the real SETTINGS → PROVISION path:
  - unsaved gives exactly `SAVE OR DISCARD` on body row 4;
  - conflict only gives `RELOAD OR DISCARD`;
  - both flags give `RELOAD OR DISCARD`.
  Each time the menu stays up (marker on row 0, `>PROVISION` highlighted), and the fake store and live seam prove
  nothing was saved, applied or loaded. A clean precondition and a clean exit are checked.
- **P29** also checks that nothing in the phase reached the executor, so P26 still inherits P28's executor.

**New counts:** l2 467 → **518** (+51), v3 902 → **964** (+62), BLE 467 → **518** (+51).

**New controls (`run.sh`).** Each is `must_build=yes` behind its own exactly-one guard `once`. The guard checks that
the literal anchor occurs once *and* that the script changes exactly one line. A failed guard counts as an unusable
control.

| Control | Mutation of `firmware_ui.cpp` (scratch copy) | RED on |
| --- | --- | --- |
| W3-D1 | detail row 2 drawn from `detail_line[0]` | 20 P29a page checks: every page whose second row differs from its first |
| W3-D2 | every detail row clipped to 18 columns | 17 P29a checks that read a full 19-column row |
| W3-C1 | compose row composed into 19 bytes (16 text columns) | exactly the 8 P29b 17-byte row checks |
| W3-C2 | the `L`/`-` column inverted after composition | 15 on the `l2` arm it runs on: the 8 P29b row checks plus 7 existing P27b/c/d checks that read the same column |
| W3-U1 | `CFG UNAVAILABLE` drawn on row 1 | the 2 P3u view checks |
| W3-N1 (v3) | the settings note drawn on row 3 | the 3 P29c note checks |

- **C0 and the existing controls.** C0 still fails to build. All 225 existing controls stay RED, and no control's
  reddened-check count fell between the baseline and stage A. `run.sh` logs counts, not per-control check lists. Counts
  only rose, where broad controls now also break the new fixtures.
- **Checks no control reddens.** Among the new checks, only the five P29a record preconditions and the three P29c "saved,
  applied and loaded nothing" checks. That is negative space: a renderer mutant cannot make the model write or load.
  The native `w3-prov` cases and M59 own that property.
- **Stage-A gate.** `run.sh` default passed: **231 verified / 0 unusable**, B227 231/231, coverage **787/936**.

**Native characterization (`test_firmware_ui_model.cpp`).** `UiFakeStore` gained a `loads` counter (every attempt), and
seven cases were added:
- `w3-open`: arrival opens the service **once**, and 20 ticks plus menu gestures add no load;
- `w3-open`: a failed open stays closed and each later sync tries **once** more, with no latch. The next sync after
  the store recovers opens it, and nothing reloads afterwards;
- `w3-prov`, one case per cell (clean, unsaved, conflict, both): each PROVISION activation adds no load, write or apply
  and keeps the draft;
- `w3-prov`: the refusal note belongs to one activation. RELOAD through the service turns `RELOAD OR DISCARD` into
  `SAVE OR DISCARD` on the next `double`, and after DISCARD the next `double` admits with no note.

Result: **2969/195978**, 0 failed (+7 cases / +74 assertions). I checked the cases' sensitivity in a scratch tree:
- a sync that re-read the store once it was open;
- an admission that re-read it before deciding;
- a one-shot latch on a failed open.

Each reddened `w3-` cases. The two re-reads leave `ui15-gate`'s CLEAN and UNSAVED cases green, so for those cells the
load count is the only witness. The comment says exactly this.

**Stage-A freeze (SHA-256).** Stage B left `probe_main.cpp` and `run.sh` byte-identical:
- `tools/probe_firmware_ui/probe_main.cpp` `d0cf6502737b3d12f60bda7a54cc04c2fe9b82896204a4369472a4019d89673a` (7283
  lines);
- `tools/probe_firmware_ui/run.sh` `f754b745c75968dd615e80e092144bad681775d10b6a99c6914446249b4f2579` (1753 lines);
- `test/test_firmware_ui_model.cpp` `e752732b71c08af62b8131f25a35a7f39bb23ef11b66d21329796d685effb3f1` (10096 lines).

## 4. Stage B and the diff

**Production (`src/firmware_ui_model.h`, +86/−35).** C1: extraction only, no behaviour change.
- **The pager.** `detail_page_count<Cols, Rows>(len)` returns ceil(len ÷ (Cols × Rows)), never zero.
  `detail_page_rows<Cols, Rows>(body, len, page, out)` gives each row its counted bytes and exactly one NUL, and clears
  nothing. Both have a `static_assert` on nonzero geometry and use 32-bit arithmetic. There is no `strlen`, sanitizing,
  clock, identity, `dirty` or `UiState` access. `on_inbox_opened` takes its page count from the helper and keeps
  normalization, sanitizing, the start state and the clock. `refresh_detail_page` is now the one-line two-row adapter.
- **The compose width.** `kComposeTextCols = kDetailCols − 2` (17). `ComposeSlot::text` is sized by it and
  `compose_project` clamps at it. The two coupling comments (at `ComposeSlot` and at the location-column note) now keep
  the display geometry and the record's limit as separate facts.
- **The admission.** `ensure_config_open()` opens only when the service is attached and not open, and has no latch.
  `sync_settings` calls it at the same point, above the closed-view return. `provision_admit()` runs in this order:
  1. the opener;
  2. refuse with no note while unattached or not open;
  3. refuse with `ProvBlock::conflict` on conflict;
  4. refuse with `ProvBlock::unsaved` when unsaved;
  5. admit.

  The `CfgRow::provision` arm is now `if (provision_admit()) enter_provision(Provision::menu);`. The source states that
  step 2 cannot be reached from today's only caller and that its first test comes with W4b. `settings_activate`'s note
  that pointed at the old line now names `provision_admit`.
- **Layout.** No data member was added.

**Native test (stage B against stage A)** ([diff](2026-09-25-standalone-mobile-home-w3/stageB-native-test-vs-stageA.diff)).
The diff was cut against the stage-A file, reconstructed byte-exact at `e752732b…`. It adds four `w3-pager` cases:
- (19, 2) equals the modal on every page of every boundary body (0/1/38/39/76/77/241);
- (19, 2) counts and slices, with a `#`-poisoned tail proving one NUL and no clearing, including 241 → page 7 at byte
  228: 13 bytes, then an empty row;
- a three-row geometry;
- small geometries (1×1 up to 255 pages, 4×2, 255×255 with no wrap, a counted body holding NUL bytes).

It **restates exactly two assertions**, both in `chrome4-audit: every PURE panel string fits the rail's 19-column body`:
- `1u + 1u + size_t(mrnv::kUiPresetTextMax) == size_t(kCols)` becomes `… size_t(kComposeTextCols) …`;
- `strlen(w17) == size_t(mrnv::kUiPresetTextMax)` becomes `… size_t(kComposeTextCols)`.

Their comments, including "widest phrase the catalog can hold" and "can never be clamped", were corrected. Every
stage-A case is otherwise unchanged. The record facts (`kUiPresetTextMax == 17`, layout and overflow) stay in
`test_firmware_ui_presets.cpp`, untouched.

**Harness (`tools/probe_ui_model_mutations.py`, against the W1c candidate)**
([diff](2026-09-25-standalone-mobile-home-w3/harness-vs-W1c-candidate.diff)): the eight §2.5 re-anchors with three short
"anchor moved" notes, plus the PIN (one derivation line and the literal). The W1c candidate was reconstructed
byte-exact at `336e4c13…` for the diff.

`git diff --stat` (the harness line includes W1c's earlier changes against HEAD):

```
 src/firmware_ui_model.h                | 121 +++++++++----
 test/test_firmware_ui_model.cpp        | 311 ++++++++++++++++++++++++++++++++-
 tools/probe_firmware_ui/probe_main.cpp | 231 ++++++++++++++++++++++++
 tools/probe_firmware_ui/run.sh         |  51 ++++++
 tools/probe_ui_model_mutations.py      | 109 ++++++++++--
```
W3's own harness delta is +30/−18 lines.

## 5. Figures — the §4.1 chain on the final tree

I derived every figure from this session's runs; none is copied from the brief or the pre-check. Steps 1–7 and the
reader audit ran as one sequential script, then the board pair ran alone, then the union.

1. **Hygiene:** `git diff --check` is clean. `probe_main.cpp` and `run.sh` equal their stage-A SHA-256. My script also
   checked the native test against its stage-A hash, which fails by design: stage B adds the helper cases and the named
   restatements (§4).
2. **Native:** `pio test -e native` (exit 0), then `./.pio/build/native/program`: **2973 test cases / 196111
   assertions, 0 failed, 0 skipped** ([summary](2026-09-25-standalone-mobile-home-w3/native-summary.txt)). The `w3-*`
   cases alone are 11 cases / 207 assertions.
3. **Corpus:**
   - a fresh stock `lus` was built with `cmake -S ../lora-universal-simulator -B <scratch>/w3-gate/sim-build
     -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=…`, then `--target lus` (both exit 0), SHA-256 `e304147d…` (W1c's; W3
     touches no `lib/`);
   - `run_corpus.py --jobs 4 --require-anchors`: **36/36 PASS, anchors 36/36**;
   - against the pre-check `corpus-manifest.json`, **36/36 identical on all 14 per-scenario fields**;
   - s18 is **269,517 events, MD5 `32afbf11e43b4bf9d0bd470ad502ba0a`**, the `BASELINE.md` keystone.

   See the [comparison](2026-09-25-standalone-mobile-home-w3/corpus-compare.txt) and the
   [manifest](2026-09-25-standalone-mobile-home-w3/corpus-manifest.json).
4. **ABI:** `probe_board_abi.py` (full): **PASS, 290 checks, 9/9 controls RED, 0 unusable**. Every §1 figure is
   unchanged:
   - `UiState` 504/8, `UiSnapshot` 1336/8, `UiModel` 928/912/912 (align 8);
   - `ComposeSlot` 20/1, `ComposeList` 161/1;
   - `Node` 235208/122176/157304.
5. **Probes** ([summary](2026-09-25-standalone-mobile-home-w3/probes-summary.txt)):
   - firmware-UI, default: **PASS — l2 518/518, v3 964/964, BLE 518/518; 231 controls verified / 0 unusable**; coverage
     787/936; B227 231/231. Against the baseline, that is +51/+62/+51 checks and +6 controls, all W3's. Against stage A,
     **all 233 control-outcome lines, the "no control reddens" list and every other `ok`/`FAIL` line are identical**,
     so stage B changed no render or check outcome;
   - firmware-UI `--no-neg` (diagnostic, B350): 518/964/518, exit 0;
   - board-UI `--no-neg`: exit 1 with **exactly W49, W51 and W54** failing, the same three `FAIL` lines as the baseline,
     byte for byte; structural 23/23, wiring 56/59, V3 124/124, V4 110/110.
6. **Tools discovery:** `python3 -m unittest discover -s tools -p "test_*.py"`: **356 tests, OK, 0 skipped**. It ran
   after `run.sh` and the harness changed.
7. **Warning census:** `tools/warning_census.sh`: **PASS**. The six pinned OLED envs are at 171 / 175 / 175 / 175 / 179 /
   179, with `-Wswitch` 0, no new warning and no pin changed
   ([summary](2026-09-25-standalone-mobile-home-w3/discovery-census-corpus-summary.txt)).
8. **Boards, final:** `pair --jobs=1` into `final-1`, then `final-2`, chained with nothing between or during them.
   Stock `compare final-1 final-2`: **PASS `gateway`, PASS `heltec_mobile`**. Base-to-final attribution (`base-1`
   against `final-1`, field by field) ([table](2026-09-25-standalone-mobile-home-w3/attribution-base1-final1.txt)):

   | env | field | base | final | Δ |
   | --- | --- | --- | --- | --- |
   | `gateway` | every `measurements.*` field and the payload | 203,740 RAM · 571,936 flash · `97deca60…` | identical | **0** — the image is byte-identical, as predicted (no OLED TU) |
   | `heltec_mobile` | `ram_bytes` | 211,724 | 211,724 | **0** (the prediction) |
   | | `flash_bytes` / `.flash.text` | 1,394,592 / 1,072,362 | 1,394,588 / 1,072,358 | **−4** / −4 |
   | | `symbol_count` / `symbol_size_total` | 13,296 / 1,495,992 | 13,297 / 1,495,987 | +1 / −5 |
   | | payload SHA-256 | `38739c4a…` | `70c33b3f…` | changed (follows the −4 B) |

   **Attribution by symbol** (`heltec_mobile`; the
   [symbol diff](2026-09-25-standalone-mobile-home-w3/attribution-heltec_mobile-symbols.diff) and
   [object-vs-linked sizes](2026-09-25-standalone-mobile-home-w3/attribution-heltec_mobile-object-vs-elf.txt)):
   - **W3's source changes:**
     - `UiModel::ensure_config_open` is new, an out-of-line `isra` clone of 110 B called from both sites (the +1
       symbol);
     - `sync_settings` 311 → 209 (−102), because the open body moved out;
     - `settings_activate` 514 → 522 (+8), because `provision_admit` is inlined there;
     - the device's `inbox_detail_cb`, which inlines `on_inbox_opened`, goes 255 → 263 (+8) with the 32-bit page
       count;
     - `refresh_detail_page` 134 → 132 (−2).
     - Net +22.
   - **Link-time placement in unedited functions of the same TU (`firmware_ui.cpp.o`):**
     - `mr_ui_tick` −8, `mr_ui_on_push` −1, `ui_prov_join_team` −4, `UiProvisionAdapter::perform` −4;
     - `cfg_row_field` +1, `saved_keys_sel_rows` +2;
     - `on_gesture` −4, `settings_edit_gesture` −4, `provision_menu_gesture` −4, `saved_keys_select_gesture` −1.
     - Their object sizes differ from their linked sizes (for example `mr_ui_tick` is 7,366 B in the object and
       7,018 B linked), so these are Xtensa link-time effects. Net −27.
   - The symbol total is **−5**, against a −4 section total; the one byte is inter-function padding.
   - **Compatibility:** every `toolchain.*`, `fixed_identity.*`, `paths.*`, `concurrency.*`, `host.*`, `schema` and
     `environment` field is identical. `source.*` and `normal_pio_metadata.*` differ as expected.

   **The predictions are met:** the `gateway` payload is identical and `heltec_mobile` RAM is unchanged. No RAM grew
   anywhere, and flash is attributed rather than assumed.
9. **Mutation union:** **6 batteries / 364 configured entries: 364 RED, 0 unusable, 0 vacuous**
   ([summary](2026-09-25-standalone-mobile-home-w3/union-summary.txt)). Each battery ran alone, after the last board
   pair. Every worker's clean baseline was **2973 / 196111 / 0**, equal to step 2 (41 worker trees). The harness
   reports the real tree untouched after every battery. The per-battery table is in §6.

**Reader audit (D6/P7)** ([receipt](2026-09-25-standalone-mobile-home-w3/reader-audit.txt)):
- **`kUiPresetTextMax`.** Comments stripped, it has **0 code readers in `firmware_ui_model.h` and 0 in
  `firmware_ui.cpp`**. The compose-row path now reads `kComposeTextCols` only. The remaining code readers are the record
  path: `device_nv.h` (the definition) and `firmware_ui_presets.h` (validation and storage). One explanatory comment in
  the model names the record constant to separate the two facts.
- **Readers of fenced files.** Every tool that reads one, by path or by directory scan, was rerun or is covered by a
  step above:
  - the feature-ownership scanner: PASS, O1–O13, 218-file integrity. W3's diff contains no `MR_FEAT_`/RADMIN token;
  - the data-type literal scanner: PASS, 218 files, selftest 4/4 RED;
  - the build-identity probe: PASS, 27 checks, 12/12 controls RED;
  - the ABI probe, board-UI, discovery and the union: covered above.
  - Five run scripts mention the harness only in a comment (the PIN idiom).
- **Symbols.** No symbol was removed, and no `lib/core` TU was added.

## 6. Controls and mutations

- **New controls:** the six W3 controls in §3 are all verified RED on the final tree, with the same counts as at stage
  A: D1 20, D2 17, C1 8, C2 15, U1 2, N1 3. C0 still fails to build, and all 225 existing controls keep their outcomes.
- **Re-anchor table** ([detail](2026-09-25-standalone-mobile-home-w3/reanchor-table.md), old → new with the exact find
  and replace strings). The labels are unchanged; every other `MUTS_MODEL` entry (231) and the `sliceCbudget` entry
  are byte-identical.

  | Entry | Old anchor (W1c candidate) | New anchor (final) | Meaning kept | RED |
  | --- | --- | --- | --- | --- |
  | M14 | `on_inbox_opened`: `uint8_t((n + kDetailPageChars - 1) / kDetailPageChars)` | `detail_page_count`: `(uint32_t(len) + per - 1) / per` | a partial page floored away | RED (25) |
  | M15 | `_st.detail_pages = p ? p : uint8_t(1);` | `return p ? uint8_t(p) : uint8_t(1);` | zero pages for an empty body | RED (6) |
  | M18 | `refresh_detail_page`: `off + uint16_t(row) * kDetailCols + n` | `detail_page_rows`: `off + uint32_t(row) * Cols + n` | every row re-reads row 0 | RED (15) |
  | M55 | the arm's conflict/unsaved `break` pair | `provision_admit`'s pair (`return false`) | unsaved tested first: both flags told SAVE | RED (4) |
  | M56 | the arm's unsaved line → `;` | `provision_admit`'s unsaved line → `;` | an unsaved draft admitted | RED (8) |
  | M57 | the arm's conflict line → `;` | `provision_admit`'s conflict line → `;` | conflict-only admitted; both mis-noted | RED (10) |
  | M59 | unsaved → `save(); enter_provision(menu); break;` | unsaved → `save(); return true;` (the arm then enters) | the helpful write C2 forbids, then entry | RED (17) |
  | M100 | `(void)_cfg->open();` → guarded by `settings != closed` | `ensure_config_open();` in `sync_settings` → the same guard | passive arrival takes no baseline | RED (986) |

  Every anchor is unique by its own text: the new guard lines are the full `if (…) { _st.prov_block = …; return
  false; }` statements, and M100 anchors on the call together with its trailing comment. M12, M20 and Y02 are
  untouched, and M13, M16/M17, M52/M53, M58 and S05/S06/S07 match exactly once. The AST census (the harness is never
  imported) finds **all 364 union entries matching exactly once**.
- **Union** — each ran alone with `python3 tools/probe_ui_model_mutations.py --target=<name>`, after the last board pair.

  | Battery | Source | Entries | RED | Unusable / vacuous | Clean baseline (every worker) |
  | --- | --- | ---: | ---: | --- | --- |
  | `model` | `src/firmware_ui_model.h` | 239 | 239 | 0 / 0 | 2973 / 196111 / 0 (8 trees) |
  | `sliceCbudget` | `src/firmware_ui_model.h` | 1 | 1 | 0 / 0 | 2973 / 196111 / 0 (1 tree) |
  | `uipresets` | `src/firmware_ui_presets.h` | 32 | 32 | 0 / 0 | 2973 / 196111 / 0 (8) |
  | `uisend` | `src/firmware_ui_send.h` | 15 | 15 | 0 / 0 | 2973 / 196111 / 0 (8) |
  | `config` | `src/firmware_config_service.h` | 32 | 32 | 0 / 0 | 2973 / 196111 / 0 (8) |
  | `uiprov` | `src/firmware_ui_prov.h` | 45 | 45 | 0 / 0 | 2973 / 196111 / 0 (8) |
  | **Total** | | **364** | **364** | **0 / 0** | |

  The model battery also re-measured the untouched named anchors on the final tree, all RED: M12 (34), M13 (164),
  M16 (1), M17 (18), M20 (57), M52 (46), M53 (2), M58 (8), S05 (8), S06 (6), S07 (12) and Y02 (84).

## 7. The pin line

PIN re-synced? YES — 2962/195904 + stage A 7 cases / 74 assertions (`w3-open`, `w3-prov`) + stage B 4 / 133
(`w3-pager`) = **2973/196111**, measured by the full native binary. It is written as one derivation line above the
bare literal `PIN_CASES, PIN_ASSERTS = 2973, 196111` (D5).

## 8. Freeze inventory

**Fenced files, final SHA-256** (receipt: [`stability-end.txt`](2026-09-25-standalone-mobile-home-w3/stability-end.txt)):

| File | SHA-256 | Lines | Preflight SHA-256 |
| --- | --- | ---: | --- |
| `src/firmware_ui_model.h` | `2a452940474091d90b20e91f43205b2c04f160206ace8042342e357f62079af1` | 5777 | `98b1992a…` |
| `test/test_firmware_ui_model.cpp` | `42dc27ffb4c2205bf8743a9c3d39d80fc6d9b6e33068262bff9c6ddc8799ee63` | 10229 | `bb4650ab…` (stage A `e752732b…`) |
| `tools/probe_firmware_ui/probe_main.cpp` | `d0cf6502737b3d12f60bda7a54cc04c2fe9b82896204a4369472a4019d89673a` | 7283 | `887df0c2…` (= the stage-A freeze) |
| `tools/probe_firmware_ui/run.sh` | `f754b745c75968dd615e80e092144bad681775d10b6a99c6914446249b4f2579` | 1753 | `cf18d721…` (= the stage-A freeze) |
| `tools/probe_ui_model_mutations.py` | `9429254f5d2ec79c32ac1dd4beca3fc9fbadab0d432e8fa57087f8bb4cebe932` | 12450 | `336e4c13…` (the W1c candidate) |

- **Never edited:** `src/firmware_ui.cpp` stays at `6fe8c435f0c3ada0a5355a2c4d8b934875c5d4e0cfd06f6fa1c90f07a4307e2f`.
- **The authorized brief** stays at `9a84d03b0f2e9826df67feba52ff87c50644383bfb85ef8bc06af7ed8e25d825`.
- **The simulator** is at `6585649ea5a780f0542b2931853a667be56a5b2b`, and `git status` is empty.
- **MeshRoute** `HEAD` is `8360802`, with nothing staged.
- **The preparation set** is unchanged: design `40b71bdb…`, register `fbb23c93…`, pre-check `6dd927b5…`, pre-check
  `SHA256SUMS` `2619012e…`, `tracker.md` `a6146666…`, `MEMORY.md` `93d12245…`. The pre-check folder verifies 23/23. The
  review receipt stays at `05511a43…`, and its folder verifies 12/12.
- **The inventory check** compares the final tree against the pre-check `inputs.json` (1,352 paths):
  - **changed: 9**, which are the 4 permitted preparation documents plus the 5 fenced files;
  - **missing: 0**;
  - **new:** the 40 preparation additions found at preflight, plus this report and its evidence folder;
  - the simulator's 285 paths: 0 changed, 0 missing;
  - **0 unexplained**.
- **The W1c candidate's other files** are byte-identical to preflight, since any change would have been listed above.

**Stability statement.** The executable-input, preparation-set and inventory state was recorded before stage A (the
preflight, [`stability-start.txt`](2026-09-25-standalone-mobile-home-w3/stability-start.txt)) and after the last step
(`stability-end.txt`). It differs only by the fenced edits and the new evidence. No input changed during the chain:
- the stage-A instruments are byte-identical at the end;
- the firmware-UI probe's own source md5 tripwire reported "sources unchanged" at stage A and on the final run;
- every mutation battery reported the real tree untouched.

**Evidence files** (every path in the folder, with its SHA-256, as listed in its `SHA256SUMS`):

```
5751d4579bc307d6bc2cb1d381063d04934751386731e9567ddeee33267eb74e  attribution-base1-final1.txt
cb796620943736fd3d994a9910a46698b53953de22f959ed474102ac4ba7309b  attribution-heltec_mobile-object-vs-elf.txt
a14216f698c85252517185cece97876266bad8313ab2fccce0332052c4787642  attribution-heltec_mobile-symbols.diff
db5aba2511da203400dccbdd2256d22314ee85d857fddb9a23f4295ecf9cd82a  coder-ledger.md
24e7cb2c9b595c04c1552c4ec0ca31ca30e2fa02bcec89b4e708b013f193f988  compare-base-gateway.txt
04a3135287857abe66c8476ec40a77cc7bc3d739daae0bc4f88d7bb06884d4fb  compare-base-heltec_mobile.txt
94f0874697b421b0ef1bb8d0a6c5e259bce44a007a86eda8948be6664420b045  compare-final-gateway.txt
a3d618ee62d51567ba935cd3b055957fe0b2a2b4356c225095a848e15b67d8ae  compare-final-heltec_mobile.txt
a87a6adc96c81ad9a267b71fdad0e6c2dbed3e24c7fde7d8ae90f9d78b963293  corpus-compare.txt
69f1a619ef6013f7d92e3502016f6869f0c0fd81edf3b1345dbdd9e5c5f91b42  corpus-manifest.json
87fc9dd8d2d979eee9d1425e87577a6ddeb0292ccc97c23492fa7ff82703df1b  discovery-census-corpus-summary.txt
bc6c27444960187dffbce8e5d205e86491edf9826f9c5f14057adcdd7e19f73b  harness-vs-W1c-candidate.diff
8f7b17336078c59cb700c9ed9c79a93013f20ca365b32d37356db6a1834049c7  manifests/base-1/gateway/manifest.json
61a10ccb6b449ad2d74e1b468aa434dc5e9a2d317df7b972836cf0fe2be77d26  manifests/base-1/heltec_mobile/manifest.json
8f7b17336078c59cb700c9ed9c79a93013f20ca365b32d37356db6a1834049c7  manifests/base-2/gateway/manifest.json
61a10ccb6b449ad2d74e1b468aa434dc5e9a2d317df7b972836cf0fe2be77d26  manifests/base-2/heltec_mobile/manifest.json
a5cbeb2ef40963a58e7ad87b39cb430d489d8903c81b35f0c17bd831fe3b2b45  manifests/final-1/gateway/manifest.json
f338ed786e2e3c2eb576b2b931ff29aa8fb5649d6c88387795a3e90ea1f4ea90  manifests/final-1/heltec_mobile/manifest.json
a5cbeb2ef40963a58e7ad87b39cb430d489d8903c81b35f0c17bd831fe3b2b45  manifests/final-2/gateway/manifest.json
f338ed786e2e3c2eb576b2b931ff29aa8fb5649d6c88387795a3e90ea1f4ea90  manifests/final-2/heltec_mobile/manifest.json
224d91c2aa51e8f8a16f8a5eadbab63aa2d216b38d2e37138c6755eef24d5acf  native-summary.txt
f87a8a2f3f3c0f1a35d035671545294cd248220d44d2e6530c64e910b4aecc5c  preflight.txt
7e9dfa48860111263d7b87df56e65ce6b611a6a98c3d72fb1145acbbe1635733  probes-summary.txt
f988370940a280d3321bdd038c285538200fb7e31c0f45002bfb1abf8a9ae300  raw-logs.txt
2612faaa085964e28987a41fca7d05782b0ce2463b4417864d05d440de484402  reader-audit.txt
b5dd191c05a76b162cd464ff44ab1ca9fd8d5df985830438cff70b0dcafabb70  reanchor-table.md
753a654e6268f4c0db3d9d3c39f136a9c0473a11dc659e4ee76cec485b8da59b  stability-end.txt
8567526582fa78be27c6d802b2a425652df5779a2a8bffc333adca864b440fde  stability-start.txt
39b8d2b67cf6e104d1c7a3128c0cd88f7390ff2607487d9913124327f7b4b3ca  stage-a.txt
55ddd17d4e8e99d14cce2b9e05dafa2d1eebaa83b3fd61849e5eb0a9b986d943  stageB-native-test-vs-stageA.diff
e2205f91ddcd4ece98575530b7f67aa73f1bacfc14e9a6d2654069baca0ba032  union-summary.txt
```

`SHA256SUMS` itself, and this report, are hashed in the hand-off message.

## 9. Not run, with reasons

- **The full board-UI probe:** B418's W49/W51/W54 are W2's. The `--no-neg` diagnostic ran, with its failure set equal to
  the baseline.
- **Console sink, inbox verbs, custody USB, BLE line and deferred actions:** none compiles or reads a fenced file, and
  none has an affected predicate. I checked this in code:
  - by the transitive `#include` closure, `src/firmware_ui.cpp` is the only `src`/`lib` TU that includes the model;
  - none of these probes compiles `firmware_ui.cpp`. `probe_inbox_verbs` names it only in a comment saying it does not;
  - their run scripts mention the harness only in a comment (reader audit §8).
- **The full feature-matrix probe:** its ownership scanner **does read** `firmware_ui_model.h` and the native test (a
  directory scan of `lib/src/test`). I reran that scanner on the final tree, and it PASSES (O1–O13, 218-file
  integrity). W3's diff names no `MR_FEAT_`/RADMIN/capability token, so the matrix's predicates are untouched.
- **The command inventory and authority checkers:** `firmware_ui_model.h` is not among the inventory's surfaces or the
  authority checker's header (verified in code). Discovery still ran the inventory's real-tree test.
- **Metal:** W3 changes no behaviour, and the host probe proves the render identity. I add no M2 row.

## Observations for QA (M1 — for QA to register or dispose; nothing is asserted as a new production bug)

1. **A pre-existing zero-match entry outside the union.** In the whole-registry AST census, `sliceEcascade`'s **E14**
   (`lib/core/node_cascade.cpp`, "THE SLICE-F INSERTION POINT PREMATURELY ENQUEUES A CUSTODY NOTICE") matches **0**
   times. It does the same with `HEAD`'s harness, and W3 touches neither that entry nor its source. It is outside W3's
   union.
2. **Negative space no renderer control can reach:** the five P29a record preconditions and the three P29c
   "saved/applied/loaded nothing" checks. This is the same standing as P6b2's store check. The model owns the property,
   and the native `w3-prov` cases and M59 measure it.

## Rulings I made (the ledger)

- **The native "note clears on the next activation" case** drives RELOAD and DISCARD through the **service**, between
  two PROVISION `double`s, instead of through gestures. A navigation press would move the cursor and retire the note on
  its own, which would measure the wrong transition. Cost if wrong: one case reworded.
- **P3u sits in P3's window**, not in a new phase: P3 is the only first-SETTINGS arrival in the binary (measured). The
  fake refuses loads across P3, whose checks count frames only, and P3u's press is a `double`, so no later phase moves.
  Cost if wrong: a later phase's screen position would shift. I checked this: the final control-outcome lines equal
  stage A's, and every existing check still passes.
- **M100's new anchor keeps the old mutant's shape** (`if (_st.settings != Settings::closed) …`) rather than moving the
  call textually below the closed-view return. The effect is identical: the opener runs only off the closed view. Cost
  if wrong: one anchor re-spelled.

