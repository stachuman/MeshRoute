<!-- Author: Codex, independent Quality Agent -->
# W4b — independent implementation QA, 2026-09-28

**PASS — software, with M103 explicitly disposed as an inapplicable surviving mutation.** The implementation meets
revision 3's contract and the approved allocation. This is not a claim that the stock mutation command returned all
green: `model` returns 1 because M103 remains configured. Its retirement is authorized below; the still-active tuple
is registered as **B458**, not counted RED and not silently omitted. No production correction is required by this
review. Hardware qualification remains **OWED**.

**B456 and B457 close. B350 also closes:** B456's diagnostic-mode correction independently satisfies that existing
finding. B443 gains the remaining legacy BACK test-description observation. The next free finding is **B459**.

## Frozen inputs and method

- MeshRoute owner base: `c8e36d8d5d32f29edf408b36b60d8977993f5ad4`, with the uncommitted candidate, reconciled through
  the [revision-3 checkpoint receipt](2026-09-27-standalone-mobile-home-w4b-r3-repin.md). No reset to the older
  `8360802` wording in the brief.
- Simulator: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean and read-only.
- [Approved brief](../plans/2026-09-27-standalone-mobile-home-w4b-home-navigation.md), revision 3:
  `a89c075e6dab5184d68c06a31b9fd262c3bab5656bc140146a879e2608d8ca5a`.
- [Coder receipt](2026-09-27-standalone-mobile-home-w4b.md):
  `7ed0bb9a876a50eccae71d21da6fb9c710d70cb9a705f0cb869b9874a4f8ffd9`.
  Its evidence index is `cafcb5d2a03253e0538438aca640861644f0fd6d9463965bd3c441ab4ce82ad2`;
  all **52 covered files** and **21 reported freeze/preparation pins** independently match.
- The checkpoint comparison has only the declared four source/test/tool changes plus the coder's disposition
  ledger, and the subsequent evidence additions. No missing or unexplained input.

The [QA inventory](2026-09-28-standalone-mobile-home-w4b-qa/inputs.json) captures **1,674 MeshRoute paths and
285 simulator paths**, including untracked inputs and four MeshRoute symlinks. Every input, both heads and both Git
status snapshots remained identical through the final instrument run. Only after that check did QA add its receipt
and perform the authorized documentation landing. The brief, coder report/evidence and all production/test/tool
inputs remain unchanged. Nothing was staged or committed.

I ran the **§4.2 / P6 src-only independent gate** myself. The board pair ran sequentially, with no other build or
mutation overlapping it. Simulator and mutation builds used fresh external scratch directories; native used the
main checkout. The two additional fault replays copied the complete frozen inventory, including the uncommitted
candidate, rather than building HEAD alone. Commands, exit codes, timings and log locations are preserved in
[gate-runs.jsonl](2026-09-28-standalone-mobile-home-w4b-qa/gate-runs.jsonl).

## Independent results

| Instrument | QA result |
| --- | --- |
| Native build, then direct binary | **3031 cases / 198613 assertions / 0 failed / 0 skipped** |
| Fresh simulator build; corpus `--require-anchors`; validation | **36/36**, byte-identical to both pre-check and coder manifests on all **14 per-scenario keys** (13 if excluding `scenario_path`) |
| s18 against current `simulation/BASELINE.md` | **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**, zero assertion failures |
| Stock board ABI, controls enabled | **290 checks; 9/9 controls RED; zero unusable** |
| Supplemental layouts from actual declarations | Approved sizes on native, heltec_mobile and gateway; no extra retained state |
| Firmware-UI, default run | **557/557 layered, 1023/1023 V3, 557/557 BLE**; **238 controls verified, zero unusable** |
| Independent control-label reconciliation | **238 distinct declared labels**, each observed exactly once with its accepted outcome; no missing, duplicate, unknown label or shell error; C0 fails compilation as required |
| Firmware-UI coverage roll-up | **845/995** unioned check labels reddened by a control; uncovered checks remain explicitly listed by the instrument |
| B456 runner-accounting tests | **19 tests, OK, no skips**; removing the actual final accounting call defeats the missing-control regression as intended |
| Firmware-UI `--no-neg` | Diagnostic succeeds and explicitly ends **NOT A GATE**; controls not run |
| Board-UI supplemental `--no-neg` | Exit **1**, exactly the known **W49/W51/W54 (B418)** failures; V3 **124/124**, V4 **110/110**, structural **23/23**, wiring **56/59**, 171 wiring controls RED; not a full board-UI PASS |
| Stock QA board pair | Both ELFs, firmware payloads, every `measurements.*` field, toolchain, fixed identity and build paths **identical to coder final-1** |
| Whitespace / preservation | Clean; all frozen inputs preserved through the gate; simulator remains clean |

**PIN re-synced? YES — pinned W4a baseline 2986/197299 + 45 cases / 1314 assertions = independently observed 3031/198613; the mutation harness pin and every worker's clean baseline agree.** The prior baseline is the approved
W4a gate, not a second baseline build claimed by this QA run.

Touched-source mutation selector (a), run in full:

| Battery | Configured | RED | Other |
| --- | ---: | ---: | --- |
| model | 239 | 238 | M103 survives; explicit disposition below |
| sliceCbudget | 1 | 1 | — |
| w4aident | 9 | 9 | — |
| chrome | 48 | 48 | — |
| uistatus | 19 | 19 | — |
| w4bhome | 26 | 26 | H25 fails 14 assertions |
| **Total** | **342** | **341** | **One surviving entry; zero vacuous or failed-compilation entries** |

All **41 distinct worker baselines** are 3031/198613/0 (eight workers in each battery except the one-entry
sliceCbudget). Aggregate baseline lines are not extra workers. Every run reports all 62 target files unchanged in
the main checkout. Full labels and actual verdict lines are in
[mutation-results.json](2026-09-28-standalone-mobile-home-w4b-qa/mutation-results.json).

## Allocation and board comparison

| Type | Native | heltec_mobile | Gateway |
| --- | ---: | ---: | ---: |
| UiState | 520 / align 8 | 520 / align 8 | 520 / align 8 |
| UiSnapshot | 1368 / align 8 | 1368 / align 8 | 1368 / align 8 |
| UiModel | 944 / align 8 | 936 / align 8 | 936 / align 8 |
| UiChrome | 20 / align 2 | 20 / align 2 | 20 / align 2 |
| HomeCapture | 9 / align 1 | 9 / align 1 | 9 / align 1 |

FrameGate, TeamRow, InviteMember, InviteIdRows, OutcomeView and SettingsView also reproduce their unchanged pins.
The declaration measurement and generated TU are separate from the stock ABI instrument in the evidence folder.
The approved structure sum is **+64 native / +72 board ABI**; gateway's ordinary firmware excludes the OLED UI.

| Board | QA linked RAM | QA flash | Delta from archived coder baseline |
| --- | ---: | ---: | --- |
| gateway | 203740 B | 571936 B | 0 / 0; identical payload and ELF |
| heltec_mobile | 211788 B | 1397696 B | **+64 B RAM / +2856 B flash** |

On mobile, QA measures `s_model` 936, `s_frame_state` 520, `s_frame_snap` 1368 and `s_frame_chrome` 20 bytes.
The three growths are +24/+16/+32 = +72 B; section alignment absorbs 8 B, so linked RAM rises 64 B. Relative to the
archived base manifest, flash changes are `.dram0.data` +64, `.flash.rodata` +96 and `.flash.text` +2696 = +2856 B.
I checked the coder's symbol attribution against source and the reproduced final symbol/image hashes; I did not
rebuild its pre-feature baseline or claim new baseline repeatability runs.

QA's fresh `.pio` metadata and source inventory differ from the earlier measurement's, as expected. This is a
field-by-field final-image comparison, **not** misuse of `measure_board.py compare` across different source trees.
[Own manifests and comparison](2026-09-28-standalone-mobile-home-w4b-qa/boards-compare.json).

## Source, stages and B457 proof

Reviewed the production diff, the native navigation matrix, changed expectations, renderer controls and mutation
re-anchors against the brief's closed ledger. The single focus field, identity-based Home capture, five capability-gated
profiles, note/press rules, typed setup returns, existing blank cancellations, emergency boundaries, counted own-name
publication, frozen frames, press-free invalidation and Inbox read watermark agree with the contract. No unrelated
production change or additional resident member was found. Detailed source anchors are in
[source-review.md](2026-09-28-standalone-mobile-home-w4b-qa/source-review.md).

Stage A's 236 labels and mutation scripts equal HEAD; its accounting implementation and regression file are unchanged
at final. Stage B reconciliation reproduces **817 unchanged, 6 changed-expression, 17 retired and 50 new probe labels**;
**219 unchanged, 8 re-anchored, 9 retired and 11 new controls**. The changes fit the authorized ledger. New wiring
controls are RED: publisher omitted (11 failed checks), live-name read (1), invalidation unwired (4). The existing
position-freeze control C102 fails two checks. No result is inferred merely from a passing native projection test.

**B457:** the stock native cases cover Home INVITE and a roster grant over a closed service, pre-emption, rail escape,
recovery and both open-service counterparts. H25 fails **14 assertions**. P3v passes through the real renderer.
For an independent fault witness, I installed the exact H25 predicate replacement once in a copied frozen tree and
ran the stock renderer diagnostic. It compiled and failed the **two P3v cue/rail-escape checks plus P3u's recovery
preview**. The text-only `CFG UNAVAILABLE` check stays green, so the proof actually distinguishes the bad navigation
state. Layered and BLE arms still pass 557/557. This is a labelled synthetic source fault, not a fault injected into
real hardware. The exact failures and hashes are in
[focused-controls.json](2026-09-28-standalone-mobile-home-w4b-qa/focused-controls.json).

<a id="m103-disposition"></a>

## M103 disposition

**Accepted as inapplicable for this frozen gate; retirement authorized.** The stock entry is still present, and its
stock run still reports a survivor. B458 tracks removal of the active tuple with its history retained; QA did not
edit the harness or alter the frozen implementation.

M103 deletes **both** the cursor reset and selection-valid reset in `close_settings_menu` (`model.h:4426`), with one
anchor match. Both current callers were re-audited after B457:

1. Settings MENU immediately calls `go_menu_home`, which repeats both resets through its own cursor assignment and
   `settings_follow_screen`.
2. B457's normalization leaves the closed Settings preview in menu mode. Its renderer never uses the cursor, and
   `sync_settings` returns before its only selection-valid read. Short walks the rail and resets the cursor; leaving
   Settings resets the flag. Double refuses while unavailable; recovery enters only through `open_settings_menu`,
   which resets both fields. Blank/emergency paths add no intervening reader in that closed preview.

Thus the former cursor-highlighted closed-entry property has no current user-visible effect to protect. The full
model run independently reproduces survival. The copied-tree M103 renderer replay also passes **557/1023/557** with
zero failures. That replay is supporting evidence, not a substitute for the source proof or the default gate.
Live wrap, MENU destination, unavailable admission, arrival opening and normalization remain protected by
**M101/M102/M105/M100/H25**, all independently RED. A new production hook or private-state assertion solely to turn
the redundant assignments RED would not improve that coverage.

This disposition is bounded to the reviewed call graph. A later caller or consumer requires reassessment; it is not
a general permission to ignore survivors. Until the instrument retirement lands, report the configured tuple and
its known outcome explicitly. B458's close condition is an instrument-only change followed by `model` and tools
checks, with the live witnesses retained.

## Landing, scope and remaining work

QA updates only the register, design §13, tracker, MEMORY and current metal plan, plus this receipt/evidence:

- B350/B456/B457 close in place; B458 records the authorized M103 retirement; B443 records the fenced-out legacy
  TEAM/BACK test title/comment. That test still proves the sub-view BACK token, while the current MENU width is
  proved elsewhere; no test assertion was changed by QA.
- Current dispatch and W4b status are updated; tracker/MEMORY point to those homes. The approved brief and the coder
  evidence stay unchanged.
- Metal UI-01/03/04/06/19 are aligned to Home/menu-mode navigation; **UI-21** adds real-font own-name/My-device and
  list-window checks. All remain **OWED**. No glyph, I²C, flash persistence or physical-radio PASS is claimed.

Per P6, the **full 625-entry coder union, 375-test full tools discovery and six-environment warning census remain
coder-owned evidence**, inspected but not independently rerun here. My independent scope is the six touched
batteries, 19 B456 regressions and probes above. B418 remains W2's known diagnostic failure set; B453's unrelated E14
battery was not run. No new owner ruling is required. No next product package is selected by this verdict, and no
commit is a progress blocker.

The [checksummed QA folder](2026-09-28-standalone-mobile-home-w4b-qa/) contains compact results, source/freeze
inventories and proof sources. Raw logs/streams are retained locally under
`artifacts/2026-09-28-standalone-mobile-home-w4b-qa/`; board ELFs/payloads remain under
`.pio-measure/w4b-qa-20260928/final/`. Their hashes and paths are recorded in `raw-artifacts.json`. These ignored paths
are local evidence, not an external backup. The documentation landing's original five files are preserved under
`artifacts/2026-09-28-standalone-mobile-home-w4b-qa/landing-before/`.
