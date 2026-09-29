Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# Standalone Home W6 — saved phrases of up to 163 bytes, paged `ui preset list`, a review before every phrase send: coder report

**Role:** coder. **Contract:** brief **revision 2**,
[`docs/superpowers/plans/2026-09-29-standalone-mobile-home-w6-phrases.md`](../plans/2026-09-29-standalone-mobile-home-w6-phrases.md),
SHA-256 `cdcef1ee1826c14a8e941e05ef4b4bfdb8e7004926cfd22f7e2a44025716828e` (692 lines), authorized by the
[review](2026-09-29-standalone-mobile-home-w6-brief-review.md) (HOLD on revision 1) and the
[re-review](2026-09-29-standalone-mobile-home-w6-brief-rereview.md) (PASS). **Base:** owner commit `70ff486`, the
[W6 pre-check](2026-09-29-standalone-mobile-home-w6-precheck.md)'s `inputs.json` as the whole-tree authority;
simulator `6585649`, clean and read-only throughout. **Evidence:**
[`2026-09-29-standalone-mobile-home-w6/`](2026-09-29-standalone-mobile-home-w6/) with its own `SHA256SUMS`; raw logs
under the ignored `artifacts/2026-09-29-standalone-mobile-home-w6/` (indexed with hashes in
[`receipts/raw-logs.txt`](2026-09-29-standalone-mobile-home-w6/receipts/raw-logs.txt)); raw board outputs under
`.pio-measure/w6/`; simulator and mutation scratch outside both repositories. **Nothing staged, committed, reset,
cleaned or discarded.**

**Verdict: ready for independent QA.** The §4.1 chain is green on the frozen tree (attempt 3; the two earlier
attempts aborted and are reported in §4). Four findings for QA/the register are in §10.

## 1. Preflight and the inventory check

- Brief SHA-256 = the authorized `cdcef1ee…716828e` (692 lines). `CLAUDE.md` and `docs/CODE_GUIDELINES.md` read.
- `scope.py preflight` → [`preflight.json`](2026-09-29-standalone-mobile-home-w6/preflight.json): **PASS** — HEAD
  `70ff486`; simulator `6585649` clean; 0 staged; `git diff --check` clean; all 26 §1 fence inputs at their base
  SHA-256 and line counts; the deferred-actions read-only inputs 2/2; the six preparation files 6/6; the pre-check's
  `SHA256SUMS` 45/45 OK; both review receipts verified. Inventory against the pre-check `inputs.json` (1977 MeshRoute /
  285 simulator paths): changed 4 (= the preparation set), missing 0, new 73 — all explained (pre-check, brief, the two
  review receipts and their folders, this package's own outputs).
- The brief was checked against the source before any edit (by symbol). Every §1 fact held: the v1 record (slot 21,
  blob 372, `MRU1` v1, `reserved_tail[3]`, a four-armed `UiPresetRead`); 1..17 validation, two boot lines, five
  defaults; `kPresetLineMax` 160, `list` = 17 records + the end record, a token after `list` → the usage line; the
  8-B `SendReq {kind, peer, slot, gen}`; the 96-B local send line in `ui_perform_send`; `SendGate {send,
  preset_changed}`; a compose double that queued directly; the tick serving the Inbox before the frame freezes.

## 2. Baselines (the unmodified tree, before any edit; `artifacts/…/run-baselines.sh`, sequential)

| Instrument | Base result |
| --- | --- |
| native (`pio test -e native`, then the binary) | **3031 cases / 198613 assertions / 0 failed / 0 skipped** |
| firmware-UI default | PASS — l2 557, v3 1023, BLE 557; **238** controls verified / 0 unusable; coverage 845/995 |
| board-UI default | PASS — **592** identities accounted; wiring live 60 / mutant 186; negctl v3 60, v4 3 |
| console-sink default | PASS — checks 720, structural 84, ble_guard 905, controls 152 |
| inbox-verbs default | PASS — accept 1400 / 63 controls, client 483 / 71 controls |
| deferred-actions default | PASS — b0-p1 150, b1-p1 151, b2-p1 158, b2-p0 158; 40 controls RED |
| ABI (stock) | PASS — 290 checks, 9/9 controls RED |
| boards `pair --jobs=1` → `.pio-measure/w6/base-1`, `base-2` (quiet checkout, back to back) | both `compare` **PASS**; gateway RAM 203740 / flash 571936, payload `97deca60…2886`; heltec_mobile RAM 211788 / flash 1397696, payload `6a7250e6…9b14` |

The per-file native split of the base was measured on a build of `git archive 70ff486` (it reproduces 3031 / 198613):
model 421/7913, presets 28/976, preset verbs 10/559, send 117/977, chrome 41/1996, device_nv 27/471.

## 3. Diff and the ledger

`git diff --stat HEAD`: 25 files, 2942+ / 503− — the **21 fenced files** below plus the 4 pinned preparation files
(design, register, `tracker.md`, `MEMORY.md`), which this package did not touch (their hashes equal preflight's).

- **`src/`** (7): `device_nv.h` (the v2 record: `kUiPresetTextMax` 163, slot 167, blob 2852, one named tail byte,
  version 2; `old_v1` recognised by size, magic and version only), `firmware_ui_presets.h` (1..163 validation, D9's
  eight defaults, the old-v1 boot line, `bad_page`, the page geometry and reply lexemes), `firmware_ui_preset_verbs.h`
  (the derived 244-B `kPresetLineMax`, paged `list`, `text_max`/`page`/`pages` in the end record; still 394 lines, the
  command inventory's six tracked lines in place), `firmware_ui_model.h` (the §7.2 word wrap, the 16-B `SendReq`, the
  review phase/binding/state, the unified page cadence, the review's transitions), `firmware_ui_send.h` (`SendLive`,
  the three-question gate, the derived 199-B `kSendLineCap`, the capture), `firmware_ui.cpp` (the one static
  `s_send_line`, the live answers, the tick-served capture, the review screen and result arms),
  `firmware_commands.cpp` (usage text and the named comment corrections only; **1915 lines**).
- **`test/`** (5 of the 6 fenced; `test_device_nv.cpp` unchanged): +24 cases / +919 assertions (§7).
- **`tools/`** (7): the firmware-UI probe and runner, the inbox-verbs probe/runner/NV fake (B477), the ABI re-pins and
  the mutation harness.
- **Docs** (2): `ios-companion/INBOX_SYNC_CONTRACT.md` and `docs/manual/command-reference.md` (§2.2).

**The §2.9 closed disposition ledger:**
[`ledger/disposition-ledger.md`](2026-09-29-standalone-mobile-home-w6/ledger/disposition-ledger.md) — every changed,
retired or added native expectation, probe check, runner check, control and mutation on its own row, with its reason
and its must-fail result (built from the working TSVs beside it and the final union logs). The rulings taken while
coding are in [`ledger/coder-ledger.md`](2026-09-29-standalone-mobile-home-w6/ledger/coder-ledger.md); the ones QA
should weigh first:

1. **Inventory layout.** `gen_command_inventory.py --check` also pins `src/firmware_ui_preset_verbs.h:325/328/333/357/372/376`.
   Those lines stay at their base positions: the reply lexemes, page geometry and the 81-B boot-line bound live in
   `firmware_ui_presets.h`; `kPresetLineMax` stays in verbs, derived from them; the page emitter folded into
   `preset_emit_list(cat, out, page = 0)`.
2. **The DM peer's known bit** is bound at capture from `Node::team_key_of_id`'s own boolean — never `hash != 0`.
3. **Review invalidation precedence:** no team → Home; DM teammate gone → roster (`TEAMMATE GONE`); then the gate's
   order (generation, team, recipient). Every note lands on item 1.
4. **A capture that refuses on a moved generation re-seals the DM list on the generation it read** (found while
   writing the probe's capture race; native regression added). Without it the next tick closes the list and the
   `PRESET CHANGED` note is lost — no send either way.
5. **The static send line's single instance is a runner check** (`W6-L0`/`W6-L1` + three real-mutant falsifiers), not a
   `ctl`: the probe binary cannot see a linked-image property.
6. **Two device-side review controls** `W6-D1` (the capture executes) and `W6-D2` (the capture is never served) —
   firmware-UI controls 238 → 240.
7. **W6's own string assertions use the tree's `strcmp` idiom** so a failing check prints integers, never a raw `»`
   (see §4, attempt 2), and the two wrap-order witnesses carry an earlier space (see §4, the dev union).

## 4. Figures (the §4.1 chain, in order; nothing else ran between or during the steps)

**Attempt history — reported, not hidden.**
- **Attempt 1** stopped at step 3: step 1's `git diff --check` found a trailing blank line at the end of
  `test/test_firmware_ui_model.cpp`. Removed; restarted from step 0. Logs: `artifacts/…/final-aborted-1/`.
- **Attempt 2** ran steps 0–9 green and stopped at step 10: `--target=model` **RUN INTEGRITY FAILURE**, 7/8 workers
  died with `UnicodeDecodeError … byte 0xbb`. The harness decodes the native binary's output as strict UTF-8, and a
  failing doctest `CHECK` prints its `std::string`/`char` operands raw; W6's rows and review lines carry `»` (0xBB).
  Fix: W6's own 27 such assertions (26 model, 1 verbs) rewritten as `std::strcmp(…) == 0` / `std::strlen(…) == 0u` / an integer marker
  compare. The predicates and the CHECK count are unchanged. A **dev union of all 13 batteries** then showed 0
  tracebacks and one survivor: **W6-M24** (wrap branch (2) dropped). My branch-(2) witnesses had no earlier space, so
  the test did not measure the order it names. Both witnesses now carry one; W6-M24 is RED (5). Logs:
  `final-aborted-2/`, `dev-union-*.log`.
- **Attempt 3 is the gate.** Its step 9 first failed procedurally: `measure_board.py pair` refuses a non-empty
  `--output`, and attempt 2 had left `.pio-measure/w6/final-1|2`. With the fence verified byte-identical to attempt 3's
  start, I moved the stale pair to `aborted-2-final-1|2` and **resumed at step 9** (`run-final-resume9.sh`: 9 → 10 →
  11, same commands, same order). No input changed, so §4.1's restart trigger did not apply. QA may prefer a
  whole-chain rerun.

| Step | Instrument | Result (attempt 3) |
| --- | --- | --- |
| 0 | `scope.py final` → [`receipts/inputs-start.json`](2026-09-29-standalone-mobile-home-w6/receipts/inputs-start.json) | **PASS** — 25 changed (21 fenced + 4 preparation), 0 unexplained, 0 missing; simulator clean |
| 1 | hygiene: `git diff --check` | **exit 0**; the ledger is closed against the diff (§3) |
| 2 | native: `pio test -e native`, then `./.pio/build/native/program` | **3055 test cases / 199532 assertions / 0 failed / 0 skipped** |
| 3 | corpus: fresh stock `lus` (`cmake -S <simulator> -B <scratch> -DMESHROUTE_DIR=…`), `run_corpus.py --require-anchors`, `--validate` | **36/36 streams, 36/36 anchors reproduce `simulation/BASELINE.md`**, validate PASS; field by field against the pre-check manifest **36/36 identical on all 14 fields**, same BASELINE hash; the fresh `lus` is byte-identical to the pre-check's (`e304147d…`) — a `src`-only change ([`corpus-compare.txt`](2026-09-29-standalone-mobile-home-w6/receipts/corpus-compare.txt)) |
| 4 | ABI: stock, then `--extra-pins abi-supplemental.json` | stock **PASS 290 checks, 9/9 controls RED**; supplemental **PASS 308 checks, 9/9** (`SendLive` 12/4, `ReviewPhase` 1/1) — [`abi-final.txt`](2026-09-29-standalone-mobile-home-w6/abi-final.txt) |
| 5 | firmware-UI default | **PASS** — l2 **591**, v3 **1059**, BLE **591** (base 557/1023/557); **240 verified / 0 unusable**, "240 declared, 240 exactly once"; coverage **872/1031** (base 845/995); `W6-L0`, `W6-L1` ok, `W6-L1a/b/c` REJECTED (exit 1); `W6-D1` RED 19, `W6-D2` RED 105; sources unchanged |
| 5 | board-UI default | **PASS** — **592** identities, wiring live 60 / mutant 186, negctl 60 + 3 (unchanged) |
| 5 | console-sink default | **PASS** — 720 / 84 / 905 / 152 (unchanged) |
| 5 | inbox-verbs default (accept, client, **oled**) | **PASS** — accept 1400 / 63, client 483 / 71 (unchanged), **oled 27 / 7** (new arm) |
| 5 | deferred-actions default | **PASS** — 150 / 151 / 158 / 158, 40 controls RED (unchanged); its source byte-identical |
| 6 | stack (`stack-measure.py`, the pre-check's method) | see §5 and [`stack-summary.txt`](2026-09-29-standalone-mobile-home-w6/stack-summary.txt) |
| 7 | tools discovery | **Ran 406, OK, 0 skipped** (809 s) |
| 8 | checkers | inventory **PASS 197 rows** byte-identical; authority **PASS** + selftests **6/6 RED**; ownership **PASS** + `--controls` **43/0**; literals **PASS** (218 files); warning census **PASS 6/6, `-Wswitch` 0** (warnings 171/175/175/175/179/179 = baselines) |
| 9 | boards: `final-1`, `final-2` back to back, stock `compare` | **PASS both envs**; attribution §5 |
| 10 | mutation union, selector (a) then (b) | **549 entries, 549 RED, 0 unusable, 0 vacuous, every match count 1**; all **90** worker baselines **3055 / 199532 / 0** (= step 2); every battery: real tree untouched |
| 11 | `scope.py final` → `receipts/inputs-end.json` | **PASS** — the fence, preparation and read-only inputs are identical to step 0; the only differing new file is `stack-measure.json` (step 6's output) |

**The stage proof (§2.8)**, in the inbox-verbs OLED arm through the real writer, verb, catalog, adapter and 2048-B
stage:
- every maximum `list` page fits whole with a host that drains nothing (0 drops); the largest is **page 4 at 1097 B**;
- `reset all` fits at a ten-digit generation and across the wrap (**1517 B**); a draining host receives every page;
- every slot appears on exactly one of five pages;
- a `set` between two page reads moves the later page's generation (`W6-O7e`);
- the two controls are RED: `W6-C1` (160-B line) and `W6-C2` (unpaged, the stage drops lines).

**Persistence (W6R-1):** after a routed 163-byte `set`, the NV medium holds exactly the expected 2852-B v2 blob
(`W6-O7b`), and the real boot restore reloads it (`W6-O7c/d`); `W6-C3` makes both RED.

**The NV fake (B477):** capacity 2852 (compile-time asserted) with truthful short writes. `W6-O14` stores and reads
back a record of exactly the capacity; `W6-O14b` shows one byte more is a short write that stores nothing. `W6-C4`
restores the dishonest fake and goes RED. The accept/client arms' `retain_on_fail`/`drop_on_ok` controls are
unchanged (71/71).

## 5. Allocation (D15)

| Type | native | heltec_mobile | gateway | D15 |
| --- | --- | --- | --- | --- |
| `UiPresetSlot` / `UiPresetBlob` | 167 / 2852 | 167 / 2852 | 167 / 2852 | 167 / 2852 |
| `PresetCatalog` | 8584 | 8572 | 8572 | 8584 / 8572 |
| `SendReq` | 16 | 16 | 16 | 16 |
| `UiState` | 568 | 560 | 560 | 568 / 560 |
| `UiModel` | 1016 | 1000 | 1000 | 1016 / 1000 |
| `ComposeSlot` / `ComposeList` / `UiSnapshot` / `UiChrome` | 20 / 161 / 1368 / 20 | same | same | unchanged |
| the static send line | 199 (`s_send_line`, one instance: `W6-L1`) | 199 | — (headless) | 199 |

**Board objects:** catalog +7440, `s_model` +64, `s_frame_state` +40, send line +199 = **+7743 B** (D15's figure).
**Linked RAM (heltec_mobile, base-1 → final-1):** `ram_bytes` 211788 → **219540 (+7752)**, measured and attributed
by symbol ([`boards/attribution.txt`](2026-09-29-standalone-mobile-home-w6/boards/attribution.txt)):

| RAM object | Base | Final | Δ |
| --- | --- | --- | --- |
| `mrfw::preset_catalog()::cat` | 1132 | 8572 | **+7440** (exactly the estimate) |
| `(anonymous namespace)::s_send_line` | — | 199 | **+199** |
| `(anonymous namespace)::s_model` | 936 | 1000 | **+64** |
| `(anonymous namespace)::s_frame_state` | 520 | 560 | **+40** |

These four are the only RAM objects that changed (Σ +7743). `.dram0.bss` grew by +7640 (7639 in objects + 1 B of
alignment) and `.dram0.data` by +112 (104 in objects + 8 B of alignment), which is where the extra +9 B comes from.
- **Flash** 1397696 → **1400276 (+2580)** = `.flash.text` +2212 (59 functions; `mr_ui_tick` +927; the pure `ui_perform_send` is now
  inlined into its wrapper, −518 / +448) + `.flash.rodata` +256 (the lexemes became named arrays) + `.dram0.data` +112.
- **gateway: unchanged** — every `measurements.*` field and the payload (`97deca60…2886`) are identical.
- The remaining manifest differences are the source snapshot and `normal_pio_metadata`, which fingerprints the
  ordinary `.pio/` tree the native and census builds touched (`normal_pio_used: false`).

**Stack** (compile-only frames, not a task high-water mark):
- **Xtensa command chain** `mesh_service_once + exec_console_line + dispatch + handle_ui + preset_verb + emit_list +
  emit_record`: **3472 → 3632 B**, exactly the pre-check's §6 prediction.
- **Setup + boot wrapper:** **1200 → 1120 B**, the short-boot-buffer prediction (81-B bound).
- **The UI send chain:** 1872 → 1744 B; the 96-B stack line became the static line.
- **Forced-OLED ARM gateway** (a pricing profile only; the stock gateway builds no phrase path): the list path is
  504 → 696 B (the pre-check forecast 752), and the `reset all` render path is 736 → **1024 B (+288)**, which I report
  without a prediction.

## 6. Controls and mutations

**Label reconciliation** ([`receipts/fwui-labels.txt`](2026-09-29-standalone-mobile-home-w6/receipts/fwui-labels.txt),
[`receipts/fwui-control-reach.txt`](2026-09-29-standalone-mobile-home-w6/receipts/fwui-control-reach.txt)):

- **Firmware-UI checks:** 995 → 1031 labels — 36 new plus 4 renamed in place (P26a, P26b ×2, P27a; none retired
  without a successor).
- **Firmware-UI controls:** 238 → 240. 200 unchanged in reach, 32 rose, 6 fell. The six that fell are C29, C30, C90
  and C92 (−2 each), K3 (−6/+3) and R1 (−3). Each lost only collateral rows, and each is dispositioned on its own; a
  label-level replay at base and at W6 shows every target check still RED.
- **C134–C141**, each audited: 1→1, 5→5, 32→49, 46→68, 12→13, 15→20, 1→1, 1→1 — all RED.
- **Coverage:** 872 of 1031 checks are reddened by some control. Two base checks lost their collateral-only reach:
  - **P27d** "CLOSED by the successful change": a model-owned rule with no `firmware_ui.cpp` site;
  - **P27e** "SENDS NOTHING": D5 now makes a one-double send impossible.

  P9d's reach is restored by `W6-D2`. Every new unreddened check is a precondition, an instrument/oracle self-check,
  or a defence-in-depth negative whose halves are natively mutated, each justified in the ledger.
- **Inbox-verbs:** the new OLED arm has 27 checks and 7 controls (`W6-C1..C6` + B237); accept and client are
  unchanged.
- **Union by battery** ([`receipts/union.txt`](2026-09-29-standalone-mobile-home-w6/receipts/union.txt)):

| Selector | Battery | Base | W6 | Final |
| --- | --- | --- | --- | --- |
| (a) | model | 238 | **264** (+26 W6-M01..M26; S03/S05/S07/Y01 re-anchored) | 264 RED |
| (a) | devicenv | 42 | **46** (+4 W6-N1..N4) | 46 RED |
| (a) | uisend | 15 | **26** (+11 W6-S01..S11) | 26 RED |
| (a) | uipresets | 32 | **39** (+7 W6-P1..P7; U25 re-expressed at T, U31 label) | 39 RED |
| (a) | uipresetverbs | 19 | **25** (+6 W6-V1..V6; V03–V06, V18 re-anchored) | 25 RED |
| (a) | sliceCbudget / sliceCsend / w4aident / w4bhome | 1 / 1 / 9 / 26 | unchanged | all RED |
| (b) | chrome / uiteam / uiinvite / consoleline | 48 / 20 / 32 / 12 | unchanged | all RED |
| | **total** | **495** | **549** (+54, 0 retired) | **549 RED / 0 unusable / 0 vacuous** |

The required new-entry classes (§2.9) all have entries:

| Class | Entries |
| --- | --- |
| old-v1 recognition and zero writes | N1–N4, P1–P3 |
| the 163/164 limits | P6, U25 |
| page size and `bad_page` | P4, P5, V1 |
| the whole-reply bound | V2, V3 |
| full-text capture | M18, S08 |
| BACK preselected | M01 |
| no-send paging | M04, M05 |
| the team, hash and generation races | M10–M15, S01–S03, S05, S07 |
| the pending phrase with an alarm | M07–M09, M16 |
| the union's exclusivity | M19 |

## 7. The pin line

PIN re-synced? YES — `tools/probe_ui_model_mutations.py` `PIN_CASES, PIN_ASSERTS` 3031, 198613 → **3055, 199532**, derived per file from a native build of `git archive 70ff486` (which reproduces 3031/198613) against the frozen tree: model 421/7913 → 437/8244 (+16/+331), presets 28/976 → 32/1356 (+4/+380), preset verbs 10/559 → 11/689 (+1/+130), send 117/977 → 120/1051 (+3/+74), chrome 41/1996 → 41/2000 (+0/+4), device_nv 27/471 unchanged; Σ +24 cases / +919 assertions = 3055/199532 = step 2's binary = all 90 union worker baselines. Also re-synced from this package's own runs: inbox-verbs `PIN_CHECKS_OLED=27` / `PIN_CONTROLS_OLED=7` (counted from the arm's own `ok` lines; accept 1400/63 and client 483/71 unchanged) and the 18 D15 rows of the ABI `PIN_TABLE` (stock run PASS, 290 checks).

## 8. Freeze inventory

- **Authorized brief:** `cdcef1ee1826c14a8e941e05ef4b4bfdb8e7004926cfd22f7e2a44025716828e`, unchanged.
- **Base:** HEAD `70ff486b40c9b07001b33bcbcdf640ad494c1149`; nothing staged. **Simulator:** `6585649ea5a780f0542b2931853a667be56a5b2b`, status clean.
- **Fenced files** (final SHA-256; [`receipts/inputs-end.json`](2026-09-29-standalone-mobile-home-w6/receipts/inputs-end.json)):

| Fenced file | Lines | SHA-256 (frozen) | vs base |
| --- | --- | --- | --- |
| `docs/manual/command-reference.md` | 576 | `ee1f1b8c68990fa4bf7ba7635833d3a28dc2235bcf32607e6453f65033ed974d` | changed |
| `ios-companion/INBOX_SYNC_CONTRACT.md` | 1415 | `5448666c9372c67ca1043d4a1533a53b10463d02c7c9ccf55f6c3d7555981783` | changed |
| `src/device_nv.h` | 1590 | `5c0e4352ce99460146074a70151f94dde58a26e67ecc98142bbe004d4c9ce79c` | changed |
| `src/firmware_commands.cpp` | 1915 | `ce1113baa75ecc991a9c8219862334fc6354155b99fa06f65d10f961a72fe128` | changed |
| `src/firmware_ui.cpp` | 2936 | `11789f4392624b2e312492ad19c221659df9ccd01276070ef145f1c9fa8a32b1` | changed |
| `src/firmware_ui_model.h` | 6478 | `6ec20c82a720a09afe01afc847f8aba9433b0cbe8a656e2c555fc88fcccb61cf` | changed |
| `src/firmware_ui_preset_verbs.h` | 394 | `fb24f120f82cb70ed7a7b6153c4e1ca8a32ba2dd1e514a9d0f6154a17ff98477` | changed |
| `src/firmware_ui_presets.h` | 752 | `1d67253f74490dbb43d83eae06b7bb5389151d4256da37b3c97e114000aebe23` | changed |
| `src/firmware_ui_send.h` | 787 | `2d6b09354adf590f48972013a1e47e3a933614f9ed837d83178a9d5ab18c5563` | changed |
| `test/test_device_nv.cpp` | 930 | `93865b037b534addf7f7bfcbc084d94372a30bbf91cbe70205bf26b1085d1212` | unchanged |
| `test/test_firmware_ui_chrome.cpp` | 1389 | `b19c08ad5a4e2e841d92f1a1b9972e6f1ec46cdadb7d58ed12d09db2d4b2e478` | changed |
| `test/test_firmware_ui_model.cpp` | 12527 | `5f7d61cadf1b50a66cd07e2f229c9478c8c27c5e1f66a0597ff580a1b8397287` | changed |
| `test/test_firmware_ui_preset_verbs.cpp` | 625 | `a5b204b30f40833f83714d9bd7a0f3c418f9e4a067dbd64d193872f6cef8235f` | changed |
| `test/test_firmware_ui_presets.cpp` | 1044 | `8811f177de4d94467505758a1b09f294e5f30283f8387865e3ce95e211831c03` | changed |
| `test/test_firmware_ui_send.cpp` | 2994 | `a79e804ac53cd714455232e4cfdc92ec27f599dd262119e52f5ce7ec9bfbe736` | changed |
| `tools/probe_board_abi.py` | 1087 | `fabd78ac4f48177cb27d21181c724dd7a9898d6c062c58c721ca3522f0bd9e2f` | changed |
| `tools/probe_console_sink/probe_main.cpp` | 556 | `f087651e66daaaca4238042438d60371b79a4817d4659397fd96345efaaa9eb4` | unchanged |
| `tools/probe_console_sink/run.sh` | 457 | `f543dbf1829bd09d2e672bdca1f32456b358cb0b5addc507a96f920061588b73` | unchanged |
| `tools/probe_firmware_ui/probe_main.cpp` | 7886 | `188bb4eab5128de44536dd7897180bdf2bf29227093466f92e44c7e593a6776d` | changed |
| `tools/probe_firmware_ui/run.sh` | 2116 | `ffcb62af6655a1a3a5eec8164f0152cc2136e2ad13b5adee922e75436afcaab5` | changed |
| `tools/probe_inbox_verbs/fakes/Preferences.h` | 138 | `bd57ac322eafe89e4e882c35b8bc02b9fda60f4738e11bfe1dacb0699b617346` | changed |
| `tools/probe_inbox_verbs/probe_main.cpp` | 2432 | `263dc475a6aed2309dc4d1fee2aeb4761c2dc6cd10f5a072340fb718b6b765f1` | changed |
| `tools/probe_inbox_verbs/run.sh` | 933 | `098fc041537e0fa64aaa3cfb5bea04066428737368071a8a9dd133e574648e61` | changed |
| `tools/probe_ui_model_mutations.py` | 12894 | `8dfc782a8e81acea1c52bf70299747665f15bea20643dbf272aa87a95edad5f1` | changed |
| `tools/test_probe_board_abi.py` | 524 | `c6630098421aed5fc2ad54ea29f512e624ed6ba1f9d19b188a0b1632707951c3` | unchanged |
| `tools/test_probe_console_sink.py` | 376 | `4a3c3eae62dbdbe982f226c44777b974008abcf5920628ead907b8cdc2a90a1f` | unchanged |

- **Preparation set, unchanged since preflight:** design `d6ad9a12…d5d34bca`, register `b78da31b…a0bd0a1e85`,
  `tracker.md` `b9472fd2…d07e21`, `MEMORY.md` `b90794da…58834d0b`, pre-check report `3b7305a0…d94831e8`, pre-check
  `SHA256SUMS` `dfd4b52d…1023b039` (45/45 OK).
- **Read-only rerun inputs:** `tools/probe_deferred_actions/run.py` `694387bd…b3b55f` (190 lines),
  `probe.cpp` `57364be0…44ae587` (222 lines) — unchanged.
- **Inventory check:** against the pre-check's `inputs.json`: MeshRoute 1977 inventoried, 25 changed (21 fenced + 4
  preparation), 0 unexplained, 0 missing; new files only the explained pre-check/brief/receipt paths and this
  package's report and evidence. Simulator 285 inventoried, 0 changed, 0 missing, 0 new.
- **New evidence:** this report and `2026-09-29-standalone-mobile-home-w6/**`, each hashed in its
  [`SHA256SUMS`](2026-09-29-standalone-mobile-home-w6/SHA256SUMS).
- **Stability statement:** the inputs recorded at attempt 3's step 0 and step 11 differ only by new evidence (the
  step-6 `stack-measure.json`). No fenced, preparation or read-only input changed during the chain, or since: a last
  `scope.py final` with all evidence written ([`receipts/inputs-freeze.json`](2026-09-29-standalone-mobile-home-w6/receipts/inputs-freeze.json))
  is **PASS**, fence and preparation identical to step 11, new files all explained. Nothing was staged or committed.

## 9. Not run, with reasons

The six batteries outside selector (a)/(b) are **uistatus, uiprov, uijoin, provservice, config and teamkeyring**. Their
predicates are unaffected, as confirmed by the [reader audit](2026-09-29-standalone-mobile-home-w6/reader-audit.txt):
- their target files are untouched and all 200 of their anchors still match exactly once;
- W6 moves none of their transitions or field carriers. The saved-key and invite members of `UiState` only shift by
  +24 behind the review union, with the same types and fields.

As a supplemental measurement outside the chain, run after step 11 on the same frozen tree, I ran them anyway ([`receipts/supplemental-union.txt`](2026-09-29-standalone-mobile-home-w6/receipts/supplemental-union.txt)): **200/200 RED, 0 unusable** — uistatus 19, uiprov 45, uijoin 26, provservice 10, config 32, teamkeyring 68 — with every worker baseline 3055/199532/0 and the real tree untouched.

Every other instrument the brief names ran in the chain above; no probe was skipped.

## 10. Findings for QA and the register

1. **Instrument (latent) — the mutation harness decodes the native binary's output as strict UTF-8**. A failing
   `CHECK` whose operand holds a non-UTF-8 byte kills the worker ("RUN INTEGRITY FAILURE", entries MISSING), measured in
   attempt 2. W6 now avoids it test-side with the tree's `strcmp` idiom. The harness fix (e.g. `errors='replace'`) is
   outside this package's harness fence purpose, so it is a register candidate.
2. **Out-of-fence comment drift** — `src/firmware_commands.h:208` still says "through the four-state read … the two
   fault states"; W6 makes it five states and three lines. No executable reader. `firmware_commands.h` is not in the
   fence.
3. **Pre-existing coverage gap** — the DM compose list's close on a moved generation
   (`if (preset_generation_moved(s)) close_compose();` in `UiModel::on_tick`) has no dedicated model mutation; only
   Y06 (the predicate) and native cases reach it. P27d's device check had only collateral reach at base.
4. **Observation (pre-existing, unchanged)** — the native case "ui-model: a compose send cannot overwrite a queued
   alarm" never exercises a compose send: under a firing alarm the overlay absorbs the doubles (R2). W6 leaves it
   byte-identical.

**For the metal plan (QA lands, §7):** the eight defaults and `Return to base n»` on glass; `ui preset list 1`..`5`
over USB with no `CONSOLE_DROP`; the old-v1 line at every boot until the first change; a 163-byte phrase review (pages,
`LOC`, `BACK` first, blank/wake on a page, an alarm from the review) with complete sent bytes; NV-06's repeated 2852-B
replacements and a power cut across the write window.

**Ready for independent QA** — freeze inventory in §8. Uncommitted, per D4; the owner commits and bench-verifies on
metal.
