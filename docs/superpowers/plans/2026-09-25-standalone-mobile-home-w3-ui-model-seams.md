<!-- Author: Claude (brief author); QA: Codex (pre-check, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# Standalone Home W3 — three UI-model seams (`src`-only refactor, C1)

**Revision 2 — 2026-09-25 — QUALITY-AGENT PASS with fold-ins; authorized by the [QA brief-review receipt](../evidence/2026-09-25-standalone-mobile-home-w3-brief-review.md).**

- **Base:** commit **`8360802`** (`8360802904f7bd0023279d3da844453d61207ede`) **plus the uncommitted, QA-passed W1c
  candidate and its landing documents**. This is not HEAD alone. The authority for the whole working tree is the
  pre-check's inventory (§1). The simulator is at **`6585649`** (`6585649ea5a780f0542b2931853a667be56a5b2b`), clean.
- **Freeze:** the coder pins this brief by the hash QA issues when it passes review. Under P4 a brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** `/home/staszek/MeshRoute` on `main`, never a worktree. Preserve every tracked and untracked file,
  including the W1c candidate — one of its files, the mutation harness, is also fenced here.
- **Authorities:**
  - the [W3 QA pre-check](../evidence/2026-09-25-standalone-mobile-home-w3-precheck.md) — source ledger,
    recommended shape, mutation ledger and baselines. Its Q-sections are cited below as "pre-check Qn";
  - the [design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) r2.19, §13 row W3,
    §6.6 item 1 (the gate, D11), §7.2 (the review's word wrap stays out of W3) and §11.1;
  - the process rules: `AGENTS.md` / `CLAUDE.md` (C1, P4–P7, D1–D6) and [agent roles](../../2026-09-02-agent-roles.md),
    including the owner ruling of 2026-09-16 on the coder's versus QA's gate for a `src`-only slice.

## 0. What this is

W3 is a pure refactor (C1). It prepares three seams that W4a, W4b, W6 and W8 will use, and changes nothing a user or
a test can observe:
1. **A fixed-byte pager** — the detail modal's page count and one page's row slices become pure helpers, with the
   row and column counts as parameters.
2. **The compose-row display width** — a named constant derived from the body width minus the two marker columns.
   It replaces the record limit `mrnv::kUiPresetTextMax` in the compose row's buffer and copy. Both equal 17 today;
   W6 will raise the record limit and leave the row at 17 columns.
3. **The provisioning admission** — the SETTINGS → PROVISION gate becomes one function, sharing a repeat-safe opener
   with the arrival path, so W4b's Home can ask the same gate later.

**Not in W3:**
- the review's word wrap (§7.2), and any three-row page state (§11.1 now leaves that to the review package);
- any change to the record limit (`kUiPresetTextMax` stays 17), `»` truncation or a Home call site;
- any new resident state, layout, wire, NV, `lib/` or simulator change.

**How identity is proved.** The pre-check found that today's host renders do not cover the maximum-length detail
sequence, a 17-character compose row, or the blocked-PROVISION note placement (pre-check Q5). So the proof comes
**first**: new real-render fixtures are added and pass against the **unchanged** source (stage A). The extraction then
lands with those fixtures frozen, and they must pass unchanged (stage B).

## 1. Verified seams and pinned inputs (pin by symbol; lines are hints from the pre-check inventory)

All production policy lives in `src/firmware_ui_model.h` ("model"); the renderer is `src/firmware_ui.cpp` ("UI").

| Seam | Fact to preserve (pre-check) | W3 change |
| --- | --- | --- |
| Detail geometry: `kDetailCols`, `kDetailBodyRows`, `kDetailPageChars`, `kDetailMaxPages`, `kDetailPageMs` (model `:~1399–1404`) | 19 × 2 = 38 bytes a page; ceil(241/38) = 7 pages; 2000 ms. UI `:~1012` asserts `kBodyCols == mrui::kDetailCols` | **None** — the constants stay, for their readers (Q2) |
| `UiState::detail_line` (model `:~2444`) | `char[2][20]`: two terminated 19-column rows | **None** — no third row |
| `UiModel::on_inbox_opened` (model `:~3197`) | Checks the request identity. A null body is length 0; clamps to 241; sanitizes with `ui_display_byte`; terminates the backing body. Pages = ceil(n/38), with empty → 1. Then page 0, BACK selected, clock stamped, refresh, dirty | Page count comes from the helper; everything else stays here |
| `refresh_detail_page` (model `:~5341`) | Offset = page × 38, row offset = row × 19. Copies counted bytes only and writes one NUL at each row's end. Does **not** clear trailing bytes | Becomes the two-row adapter over the slice helper; the same writes |
| `close_detail`, tick paging (`:~2841`), `unblank` (`:~5261`), `on_msg_wake` (`:~3138`) | State transitions, cadence, blank and wake rules (Q2 table) | **None** |
| `ComposeSlot` (`:~1469`), `compose_project` (`:~1495`; clamp `:~1504`) | `text[kUiPresetTextMax + 1]`; the copy clamps at the record limit. Size 20 / align 1; offsets text 0, slot 18, loc 19; `ComposeList` 161 / 1 | Buffer bound and clamp use the display constant; layout unchanged |
| `compose_row_text` (`:~1616`), `compose_row_line` (`:~1648`); coupling comments `:~1470`, `:~1627` | Display readers of the copied row | Comments corrected (Q3) |
| `send_gate_of`, `ui_compose_send_line` (`src/firmware_ui_send.h`); `validate_preset_text`, `preset_slot_put` (`src/firmware_ui_presets.h`); `UiPresetSlot` (`src/device_nv.h`) | Sends from the **live catalog**, never from `ComposeSlot::text`; the record limit stays the record's | **None** |
| `UiModel::settings_activate` (`:~4106`), `CfgRow::provision` arm (`:~4142–4147`) | Clears the note first; refuses without an open service (no note); conflict before unsaved; never saves; `enter_provision(Provision::menu)` | The arm calls the admission function and keeps its transition |
| `sync_settings` (`:~4022`; open `:~4029`, closed-view return `:~4039`) | Opens on arrival **before** the closed-view return. A repeated `open()` returns `already_open` without touching the store. A failed load stays closed and a later sync retries, with no latch | The open goes through the shared opener at the same point |
| `activate`'s closed SETTINGS arm (`:~3917–3934`) | Without an open service the menu never opens, so the PROVISION unavailable guard is defence in depth today | **None** |
| `settings_note` (`:~2636`); `draw_settings_tail` (UI `:~2094`); `draw_settings_screen` (UI `:~2115`) | Exactly `RELOAD OR DISCARD` / `SAVE OR DISCARD` on body row 4, before any reboot note; `CFG UNAVAILABLE` on row 2 of the closed view | **None** |
| `tools/probe_firmware_ui` | Real device TU through W1's wrapper, drawn on a canvas fake. `ctl` mutates the **device cpp** only, with a `cmp` vacuity check. C0 is `must_build=no`. No numeric pins; counts are reported | New fixtures and controls (§2.4) |
| `tools/probe_ui_model_mutations.py` | Selector (a): `model` 239 and `sliceCbudget` 1, all matching exactly once. `PIN_CASES, PIN_ASSERTS = 2962, 195904` | Re-anchoring per §2.5, plus the PIN |
| `tools/probe_board_ui/run.sh` W43 | Requires the literals `static_assert(kBodyCols == mrui::kDetailCols,` and `constexpr int kBodyCols = 19;` in the UI | **None** (UI not edited). B418's W49/W51/W54 are W2's |
| `tools/probe_board_abi.py` | `UiState` 504 / 8; `UiSnapshot` 1336 / 8; `UiModel` 928 / 912 / 912, align 8; `ComposeSlot` 20 / 1; `ComposeList` 161 / 1; Node 235208 / 122176 / 157304 | **None** — no re-pin |

**Executable inputs.** These are the fenced files. Each hash is the working-tree SHA-256 at authoring time.

| File | SHA-256 | Lines | Note |
| --- | --- | ---: | --- |
| `src/firmware_ui_model.h` | `98b1992aec2a380b9594e769aa81ee7abb24196e723dec19fc6fc9597c08d2b5` | 5726 | equals HEAD |
| `test/test_firmware_ui_model.cpp` | `bb4650abf84dbd33156a62ffd3214d87ba64d1a9dbb352a6a13e43f9962a095f` | 9936 | equals HEAD |
| `tools/probe_firmware_ui/probe_main.cpp` | `887df0c299143570c9c119943f58dfa7f0e4398b881bc14c38a0b594e68cd324` | 7052 | equals HEAD |
| `tools/probe_firmware_ui/run.sh` | `cf18d721854f13264bdef9a0a223a8f85c9707cf6641088947c02c019c1c6a3e` | 1702 | equals HEAD |
| `tools/probe_ui_model_mutations.py` | `336e4c13ebb09b7ca7b8a4b4db42b1beda1157af09ca2edf7406b67b17c9d400` | 12438 | the **W1c candidate** (HEAD is `f135d051…`) |

The main pinned consumer, which is not edited: `src/firmware_ui.cpp` `6fe8c435f0c3ada0a5355a2c4d8b934875c5d4e0cfd06f6fa1c90f07a4307e2f`.

**Base inventory.** The pre-check's `inputs.json` (1,352 MeshRoute paths, covered by its `SHA256SUMS`) is the
authority for every other path. At authoring, after QA wrote it, only the four preparation documents below changed.
New paths since then: the pre-check report and folder, this brief, and QA review artefacts.

**Permitted preparation set.** These uncommitted documents are preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | authority (r2.19) | `40b71bdb46f05888440eb666eae628f26e40430eb82e636d75630fa4c3429b5b` |
| `docs/2026-07-30-open-bug-register.md` | authority (§0 dispatch) | `fbb23c93eb224d624683e15bdf3d29056a3457a69a38f481e2ffc19a65eab0c3` |
| `docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w3-precheck.md` | authority (pre-check) | `6dd927b592cb009cea5f06028e8977cc15a0ec01224f603ec2f21691c08d0cfc` |
| `docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w3-precheck/SHA256SUMS` | evidence — covers the folder's other 23 files, including `inputs.json` (`fcc2b842…060e02`) and `corpus-manifest.json` | `2619012ea75ea8e963eba9f88b192b34983413064870f90c5b50b423b73a843b` |
| `tracker.md`, `MEMORY.md` | context pointers | `a61466662168563a22bfc77b1b3d7314c5160be2a83b9ab1f9c7b4413e1d65a4`, `93d122451513209c7165356b7ed258fec544a8515e6d94ade80658e5e0e9075a` |

This brief is pinned by the hash QA issues on PASS, not listed here.

**Preflight:**
1. Check both repositories: MeshRoute `HEAD` = `8360802`; the simulator at `6585649` and clean.
2. Every executable input matches its hash above.
3. Every preparation-set hash matches, and the pre-check folder verifies (`sha256sum -c SHA256SUMS`, 23 OK).
4. Every path in `inputs.json` still has its recorded hash, except the four preparation documents above.
5. Classify every other path. QA review reports and evidence added after this brief are explained preparation
   additions; anything else is reported.

Any of the following is STOP-1 to QA: a different `HEAD`, a mismatched input, a changed preparation file, or an
unexplained path. The whole preparation set stays unchanged from preflight through the freeze.

## 2. Contract

### 2.1 The pager

- **Two pure helpers** in `src/firmware_ui_model.h`, beside the detail geometry:
  - **page count** from a counted length: ceil(len ÷ (cols × rows)), with an empty body giving **1**;
  - **one page's row slices:** from a counted body, its length and a page index, fill `rows` rows of at most `cols`
    bytes each. Each row gets the counted bytes, then exactly one NUL at the row's end — including an empty row. Never
    clear the rest of the buffer.
- **Geometry** (columns and rows) are parameters. Template dimensions are a good fit. The domain is constrained
  explicitly — nonzero geometry, for example `static_assert` — with no invalid-geometry fallback.
- **No truncation.** Arithmetic must not truncate for any length ≤ 255 and any page in range.
- **Nothing else moves in.** No `strlen`, allocation, sanitizing, timer, modal identity, `dirty` handling or `UiState`
  access goes into the helpers.
- **Callers:** `on_inbox_opened` takes its page count from the helper, and keeps normalization, sanitizing, start
  state and the clock. `refresh_detail_page` becomes the two-row adapter, with the same writes as today. `close_detail`,
  the tick cadence, blank/wake and deletion keep their state transitions verbatim.
- **Today's outputs must reproduce exactly** (pre-check Q2):
  - length 0 → one page, two empty rows;
  - 38 → one full page;
  - 76 → exactly two pages, with no extra empty page;
  - 241 → seven pages; page 6 starts at byte 228, with 13 bytes on its first row and an empty second row.

### 2.2 The compose display width

- **A named constant** for the compose text columns, derived from the model's body width minus the two marker columns
  (selection, location). The model's body-width authority is `kDetailCols`, which the renderer asserts equals
  `kBodyCols`. The value is **17**.
- **`ComposeSlot::text`** is sized from it, and `compose_project`'s copy clamps at it.
- **Unchanged:** slot identity, the location flag, enable and kind filtering, order, frame freezing, the snapshot
  publication and every layout fact in §1.
- **The record limit stays the record's.** `kUiPresetTextMax` keeps its readers in the record path (`device_nv.h`,
  presets validation and storage, the send path). The compose-row path must have **no** reader of it afterwards;
  prove this with a grep in the report.
- **No new geometry.** No common body-width constant and no change to `kDetailCols`'s line (it is mutation M52's
  anchor). No `»` and no claim that W6's truncation exists.
- **Comments.** Correct the two coupling comments and the test's "widest phrase the catalog can hold" wording, so the
  current record limit and the permanent display geometry read as separate facts.

### 2.3 The provisioning admission

- **A private repeat-safe opener** (for example `ensure_config_open`): when attached and not open, call `open()` once.
  `sync_settings` calls it at today's point, before the closed-view return. **Don't** add an "attempted" latch or change
  the failed-open retry.
- **A private admission function** (for example `provision_admit`) returns admitted or not. In this order:
  1. call the opener;
  2. refuse, with **no** note, when unattached or still not open;
  3. refuse with `ProvBlock::conflict` on conflict;
  4. refuse with `ProvBlock::unsaved` when unsaved;
  5. otherwise admit.

  It never saves, applies or transitions.
- **The `CfgRow::provision` arm** calls it and, when admitted, performs today's `enter_provision(Provision::menu)`.
  `settings_activate` keeps its note-clearing, row resolution and `dirty` timing.
- **No Home caller, saved origin, new member or new note** in W3.
- **The unreachable branch.** Admitting with a closed service cannot be reached on today's SETTINGS path, so W3 has no
  reachable test for it. Its first test comes with W4b's caller. The source comment says so ("mark done-vs-missing in
  code"). No production test hook is added to reach it.

### 2.4 The proof: stage A, then stage B

**Stage A — instrument first, against the unchanged production source.** Add, run and pass:
- **Firmware-UI probe fixtures** (`probe_main.cpp`) through the real renderer, with exact row text at exact rows:
  1. **Detail:** bodies of 0, 38, 39, 76 and 241 bytes. For each, every page's header and two body rows, including
     empty and partial rows. Multi-page bodies also cycle back to page 0 and blank/wake on a page other than 0
     (the page is kept); single-page bodies stay on page 0 across cadence ticks and blank/wake.
  2. **Compose:** a 17-byte DM phrase and a 17-byte channel phrase, each selected and unselected, with both location
     markers. GRANT KEY, BACK and the empty-list rows stay unchanged, and the generation and frozen-frame checks keep.
  3. **Blocked PROVISION**, on the real SETTINGS → PROVISION path, on a capability-bearing arm (today the
     child-enabled v3 arm):
     - unsaved → exactly `SAVE OR DISCARD` on body row 4;
     - conflict only → `RELOAD OR DISCARD`;
     - both flags → `RELOAD OR DISCARD`;
     - in each case no menu transition and no save or apply.

     An unavailable service keeps the closed view with `CFG UNAVAILABLE`, where the probe's config fake can refuse the
     load; otherwise that case is native-only, with the reason recorded. Never invent a PROVISION menu on an arm whose
     capability table omits it.
- **New probe controls** (`run.sh` `ctl`, which mutates the device cpp), `must_build=yes`. Each substitution has its
  own exactly-one source-match guard (B449's lesson). At least:
  - a detail body row drawn on the wrong row, or the second row drawn from the first;
  - the detail body clipped by one column (a lost final byte);
  - the compose location marker inverted, or the text clipped to 16 columns;
  - the blocked note dropped or moved off row 4.

  Each must compile and go RED on its intended new checks; list and explain any other failing check. A crash, vacuous
  sed or failed compile is not a usable RED. C0 keeps `must_build=no`, and every existing control keeps its meaning.
- **Native characterization** (`test_firmware_ui_model.cpp`):
  - add a **load counter** to `UiFakeStore`;
  - arrival opens the service exactly once, and repeated ticks and gestures on SETTINGS add no load;
  - each reachable PROVISION activation (clean, unsaved, conflict, both) adds no load, write or apply, and keeps the
    draft;
  - a failed open stays closed, and a later sync tries again (no latch);
  - the note clears on the next activation.

  Existing `ui7d-modal`, `ui17-hold`/`ui17-wake`, `b232-open` and `ui15-gate` cases stay as they are.
- **Freeze stage A.** Record the SHA-256 of `probe_main.cpp`, `run.sh` and the native test file at the end of stage A,
  with the stage-A run results.

**Stage B — the extraction.**
- The §2.1–§2.3 production changes.
- **Helper cases** with local test buffers: the (19, 2) geometry equals the detail modal; a 3-row geometry; small
  geometries; lengths 0, 1, exact multiples, +1 and the maximum. `UiState` is not widened.
- **The restated budget test:** `1 + 1 + <display constant> == body columns`, with the 17-byte fixture asserting the
  **display** bound. The record facts stay (`kUiPresetTextMax == 17`, the record-layout and overflow tests), and the
  ABI/offset asserts are unchanged.
- **Frozen instruments:** `probe_main.cpp` and `run.sh` stay **hash-identical to stage A**.
- **The native test file:** at the freeze it keeps every stage-A case unchanged. Its diff against stage A only adds the
  helper cases and restates the assertions the new constant requires; the report names each one.
- **Transcript (optional).** A focused frame transcript is permitted. If added (stage A), normalize bitmap pointers to
  content and detect recording-buffer truncation. It proves host draw operations, not physical pixels.

### 2.5 Mutations — re-anchor, never weaken

Selector (a) is `model` (239) and `sliceCbudget` (1). The pre-check's ledger (Q6) governs:

| Entry | Treatment; the mutant's meaning survives |
| --- | --- |
| M12, M20, Y02 | Statements untouched; exact anchors kept |
| M14 | Re-anchor on the helper's ceiling; the mutant floors partial pages |
| M15 | Re-anchor on the empty-to-one rule; the mutant returns zero pages for empty |
| M18 | Re-anchor on the helper's row stride; the mutant repeats row 0 on later rows |
| M55 | In the admission function, swap the conflict and unsaved guards; both-flags then tells a conflict to SAVE |
| M56 | Drop the unsaved refusal; an unsaved draft is admitted |
| M57 | Drop the conflict refusal; the conflict-only case is admitted or mis-noted |
| M59 | The unsaved guard saves the draft and admits, so the caller enters (no `break` transplant, no unrelated failure) |
| M100 | The opener call in `sync_settings` deferred below the closed-view return; passive arrival loses its baseline |

**Rules:**
- **Every other entry** — among them M13, M16/M17, M52/M53, M58, S05/S06/S07 and the Y compose entries — must still
  match exactly once. If an edit touches its anchor, it is re-anchored with the same meaning and named.
- **No entry is added, retired or weakened;** the configured counts stay **239 / 1**. Each re-anchored entry is listed
  old → new, with its RED.
- **Changed-once rule.** The bare guard statements must be anchored with enough context to match once.
- **Native PIN:** re-sync `PIN_CASES, PIN_ASSERTS` in the existing shape — the literal plus one derivation line (D5).

### 2.6 Nothing else moves

- **Layout:** no data member added to `UiState`, `UiSnapshot` or `UiModel`; every §1 ABI and offset fact unchanged.
- **Corpus:** 36/36, identical field by field to the pre-check's `corpus-manifest.json`; s18 per `simulation/BASELINE.md`.
- **Boards:**
  - `gateway` does not compile the OLED TU, so its image is predicted identical;
  - `heltec_mobile` RAM is predicted unchanged, and any RAM change is attributed;
  - flash is measured and attributed, not assumed (C1 does not imply flash neutrality).

## 3. Fence

**IN:**
- `src/firmware_ui_model.h` — the §2.1–§2.3 helpers, adapter, constant, opener and admission, plus the comments they
  make stale; no data member;
- `test/test_firmware_ui_model.cpp` — stage A's characterization and the load counter; stage B's helper cases and
  named restatements;
- `tools/probe_firmware_ui/probe_main.cpp` — stage A's fixtures, and the optional transcript;
- `tools/probe_firmware_ui/run.sh` — stage A's controls with their match guards, and any transcript integration; the
  wrapper and every existing control's meaning are preserved;
- `tools/probe_ui_model_mutations.py` — the §2.5 re-anchors and the PIN literal with one derivation line;
- the report and evidence (§5).

**OUT:**
- `src/firmware_ui.cpp` — the renderer, W43's literals and the C-controls' anchors. A needed edit is STOP-1 for
  reconciliation;
- `src/device_nv.h`, `src/firmware_config_service.h`, `src/firmware_ui_presets.h`, `src/firmware_ui_send.h` and every
  other `src/` file;
- `lib/`, `platformio.ini` and the simulator;
- `test/test_firmware_ui_presets.cpp`, `test/test_firmware_ui_send.cpp`, `test/test_firmware_config_service.cpp` and
  every other test;
- `tools/probe_board_abi.py` (no re-pin), `tools/probe_board_ui/*` (B418 is W2's), `tools/warning_census.sh`,
  `tools/measure_board.py` and every other tool;
- every existing battery's meaning, and every entry outside §2.5;
- the design, register, tracker, `MEMORY.md` and metal plan (QA lands, §7);
- the W1c candidate's other files — preserve them byte-identical.

## 4. Gates

The owner ruling of 2026-09-16 (P6) gives the coder the full chain, and QA the `src`-only independent gate.

**Ordering, throughout:** the two runs of each board pair are back-to-back, and nothing runs between or during them.
Mutation batteries run separately from board measurements.

### 4.1 The coder's gate

**Baselines, on the unmodified tree, before stage A:**
- **Firmware-UI probe:** default mode. Record per arm the checks and "controls verified / unusable" (pre-check:
  467 / 902 / 467, 225 / 0).
- **Board-UI supplemental:** `tools/probe_board_ui/run.sh --no-neg`. Record its exact failure set (expected: B418's
  W49/W51/W54).
- **Boards:** `python3 tools/measure_board.py pair --jobs=1 --output .pio-measure/w3/base-1`, then `base-2`. Then the
  stock `compare` per env on the two `manifest.json` files; both must PASS.

**Stage A:** the §2.4 stage-A additions, run on the unchanged production source.
- Every new check passes, and every new control is RED on its intended checks.
- Record the three stage-A hashes.

**Stage B, then the full chain:**
1. **Hygiene:** `git diff --check`. The stage-A hashes of `probe_main.cpp` and `run.sh` are unchanged.
2. **Native:** `pio test -e native`, then **run the binary** `./.pio/build/native/program`. Report the counts derived:
   0 failed, 0 skipped. `PIN re-synced? YES — <derivation>`.
3. **Corpus:**
   - a fresh stock `lus`: `cmake -S <simulator> -B <new dir outside both repos> -DCMAKE_BUILD_TYPE=Release
     -DMESHROUTE_DIR=<MeshRoute>`, then `cmake --build <dir> --target lus`;
   - `python3 -B tools/run_corpus.py --out <new dir> --lus <dir>/orchestrator/lus --require-anchors`;
   - expect 36/36, compared field by field with the pre-check manifest.
4. **ABI:** `python3 -B tools/probe_board_abi.py` (full, controls on). Every §1 figure is unchanged and 9/9 controls
   are RED.
5. **Probes:**
   - **Firmware-UI, default mode:** every arm green, stage-A fixtures included; every control verified, the new ones
     included; 0 unusable. Per-arm counts are reported against the baseline.
   - **Firmware-UI `--no-neg`:** diagnostic only; B350 applies.
   - **Board-UI supplemental:** a failure set identical to the baseline.
6. **Tools discovery** (D5), since `run.sh` and the harness change: `python3 -m unittest discover -s tools -p
   "test_*.py"`. Report the count derived (W1c recorded 356), OK, 0 skipped.
7. **Warning census:** `tools/warning_census.sh` on its six pinned OLED envs — the stated exception to the two-env
   rule. Zero new warnings, `-Wswitch` 0, pins unchanged.
8. **Boards, final.** Run `.pio-measure/w3/final-1` and `final-2`, then the stock `compare` per env; both must PASS.
   - **Attribution.** Read the `base-1` and `final-1` manifests field by field: every `measurements.*` and
     `artifacts.payload.sha256` difference is attributed, down to symbol level where RAM or a symbol moves.
   - **Compatibility.** `source.*` differences are expected. `toolchain.*`, `fixed_identity.*` and `paths.*` must be
     identical.
   - **Predictions (§2.6):** `gateway` payload identical; `heltec_mobile` RAM unchanged.
9. **Mutation union:**
   - **Batteries:**
     - (a) `model` (239) and `sliceCbudget` (1);
     - (b) the pre-check's dependency set: `uipresets` (32, the live catalog and record), `uisend` (15, the payload
       and stale-generation gate), `config` (32, opening, draft and refusal) and `uiprov` (45, the provision
       adapter).
   - **Totals:** 6 batteries / 364 configured entries.
   - **Run** each with `python3 tools/probe_ui_model_mutations.py --target=<name>`.
   - **Expect** every entry RED, 0 unusable, 0 vacuous. Every worker's clean baseline equals step 2's counts.
   - **A non-RED entry** is reported with its retained capture, whether or not it predates W3. QA disposes of it; it
     is never silently accepted.

**Input stability:** the executable-input, preparation-set and inventory state are recorded before stage A and after
the last step. They differ only by the fenced edits and new evidence; a change in between restarts the chain.

### 4.2 QA's independent gate (P6, `src`-only)

On the frozen tree, QA re-runs:
- **Native** — a fresh `pio test -e native`, then run `./.pio/build/native/program`; no inherited binary result;
- **Corpus** — 36/36 byte-identical;
- **Boards** — its own `pair --jobs=1` into a fresh `.pio-measure/` directory. Its `measurements.*` and payload hashes
  must equal the coder's `final-1`, read field by field;
- **Selector (a)** — `model` and `sliceCbudget`, the touched batteries; QA may add any battery from (b);
- **The affected probes** — firmware-UI (default, controls) and the ABI probe.

The census, the full union and discovery stay in the coder's chain, per the ruling.

### 4.3 STOP conditions

- **Frozen instruments:** a stage-A fixture expectation or control changed after stage A (hash drift in `probe_main.cpp`
  or `run.sh`), or a stage-A native case edited beyond the named restatements.
- **Behaviour and layout:** any changed existing native expectation, any stage-A render/check outcome changed by
  stage B, any corpus stream delta, a data member added, or any ABI/offset change. The planned added test counts and
  named assertion restatements are permitted; derive and report them, without weakening existing expectations.
- **Fence:** an edit outside §3, `src/firmware_ui.cpp` included.
- **SETTINGS opening:** a change to arrival opening, the no-latch retry, or the no-extra-load facts.
- **Controls and entries:**
  - a re-anchored entry whose meaning changed;
  - an entry or new control that is vacuous, does not compile, crashes, or goes RED only on unrelated checks;
  - an existing control losing its meaning, or C0 building.
- **Census:** a warning-census failure.
- **Boards:** a failed repeatability `compare`, an unattributed board delta, a `gateway` image change, or any RAM
  increase.
- **Inputs:** a mismatched input at preflight, a changed preparation file, or an input change during the chain.
- **Source facts:** a §1 fact found false — a semantic disagreement (P4), STOP-1 to QA.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w3.md`, with an author line on line 1.

**Evidence directory:** `docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w3/`. It holds:
- **Board manifests:** the eight per-env manifests (`base-1`, `base-2`, `final-1`, `final-2` × `gateway`,
  `heltec_mobile`), copied from `.pio-measure/w3/` **after the last measurement run**;
- **Board comparisons:** the four `compare` outputs and the attribution table;
- **Stage A:** its hashes and run results;
- **Durable receipts:** compact native, ABI, probe-mode, discovery, census and union summaries; the full corpus
  manifest and field comparison; locations and SHA-256 hashes of the raw logs retained outside this folder;
- **The re-anchor table and the reader-audit greps** (D6/P7), including the grep showing no `kUiPresetTextMax` reader
  in the compose-row path;
- **A `SHA256SUMS`** covering everything.

Follow [the evidence-retention policy](../evidence/README.md): raw runs/logs go under ignored
`artifacts/2026-09-25-standalone-mobile-home-w3/` or a named external archive. Raw board measurement outputs stay
under `.pio-measure/w3/`; the stock tool keeps its stable per-env build/libdeps/workspace roots under
`.pio-measure/env/`. Do not relocate those roots: the mobile payload is path-dependent. Native uses normal `.pio/`;
simulator and mutation scratch builds stay outside both repositories. Compact receipts and copied board manifests
remain in the durable evidence folder above.

The report contains:
1. **Preflight:** both repositories' identity and status, every §1 hash as found, and the inventory check.
2. **Baselines:** the firmware-UI counts, the board-UI failure set, and the base pair with its `compare` verdicts.
3. **Stage A:** the new checks and controls with their results on the unchanged source, and the three hashes.
4. **Diff:** `git diff --stat` for the fenced files, stage B's diff of the native test against stage A, and the
   restatements named.
5. **Figures:** every §4.1 figure, derived by the coder; none copied from this brief or the pre-check.
6. **Controls and mutations:** each new control; the union by battery, with RED, unusable and vacuous counts; the
   re-anchor table.
7. **The pin line:** the exact line `PIN re-synced? YES — <derivation>`.
8. **Freeze inventory:**
   - the final SHA-256 of every fenced file;
   - the authorized brief hash;
   - the simulator commit and status;
   - the preparation-set hashes, unchanged;
   - the inventory check;
   - every new evidence path with its hash;
   - the stability statement.
9. **Not run,** with reasons. Expected:
   - the full board-UI probe (B418, W2);
   - console sink, inbox verbs, custody USB, BLE line and deferred actions: no affected predicate or compiled
     implementation under the stated fence, subject to the final D6/P7 reader audit;
   - the full feature-matrix probe: its ownership scanner **does read** the model header and native test file,
     scanning `lib/src/test` for the remote-admin capability pair. W3 changes none of those predicates. Verify this
     against the final edits and rerun any affected check/control; do not claim that these files are unread;
   - the command inventory and authority checkers — `firmware_ui_model.h` is not in their scan (discovery still runs
     the inventory's real-tree test);
   - metal.

## 6. Owner rulings

None requested. The pre-check found no ruling needed. The design's §6.6 (D11) already rules that the gate is shared,
and §11.1's three-row option is now explicitly left to the review package (design r2.19).

## 7. Landing (QA, on PASS)

- **Register:** the §0 dispatch rewritten in place. No finding is expected to open or close.
- **Design:** the §13 W3 status note.
- **Pointers:** `tracker.md` and `MEMORY.md`.
- **No new metal row** (M2). W3 changes no behaviour, and the render identity is proved by the host probe.
- **Next:** W4a (it needs W1, W1c and W3), with B449 folded in, per the owner's order.

## 8. Revision history

**Revision 2 (2026-09-25)** folds in QA W3R-1–W3R-3: applicability of single-page versus multi-page fixtures,
planned count increases versus regression stops, an explicit fresh QA native build, the feature-ownership reader
scope, and raw-artifact/build-root wording. No production contract, fence, mutation effect or required gate is removed.

**Revision 1 (2026-09-25)** is the first draft, built on the W3 pre-check.
- It adopts the pre-check's recommended shape, fence, dependency selector and re-anchor ledger.
- It adds the stage-A/stage-B protocol that makes the render proof precede the extraction.
- It records the design clarification in r2.19: §11.1's three-row buffer belongs to the review package.
