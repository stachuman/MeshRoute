<!-- Author: Codex, independent Quality Agent; reviewed author: Claude; no implementation authority exercised -->
# W4b revision 1 — independent brief review, 2026-09-27

**Verdict: HOLD.** W4B-1 through W4B-3 require corrections and a scoped re-review. W4B-4 is a minor documentation fold-in. The owner-approved allocation is accepted; no new owner ruling is requested. No coder dispatch hash is authorized by this receipt.

Reviewed [brief revision 1](../plans/2026-09-27-standalone-mobile-home-w4b-home-navigation.md), SHA-256 **`a354c251da007415de8b3b616d65da47933918d0dcc8e67cc0d4c90a3edf599e`**, against [design r2.21](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md), the [pre-check](2026-09-27-standalone-mobile-home-w4b-precheck.md), and executable source. References below use symbols as authority; line numbers locate this frozen candidate (P4).

## 1. Base and independent checks

- MeshRoute: `8360802904f7bd0023279d3da844453d61207ede` **plus the existing uncommitted W1c/W3/W4a candidates and QA landings**. No HEAD-only substitution.
- Simulator: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean.
- All **20 hashes**, **14 source line counts**, **14 pre-check evidence checksums**, and **6 Markdown links in the brief** match. The new B456 tools-test path is absent as declared.
- Against the pre-check inventory, only the four declared existing documents changed: design, register, `tracker.md`, `MEMORY.md`. Additions are the pre-check's own report/evidence and the new brief. No missing or unexplained implementation input.
- This review inventoried **1,593 MeshRoute paths and 285 simulator paths**. Nothing was staged. Preservation is checked again after writing this review's evidence.
- Re-ran the pre-check's structure-only measurement driver using all three ABI compiler/flag sets. Its approved `reuse_focus_append_name` variant reproduces the brief exactly:

| Type | Current | Approved candidate | Alignment |
| --- | --- | --- | --- |
| `UiState`, all three | 504 | 520 | 8 |
| `UiSnapshot`, all three | 1336 | 1368 | 8 |
| `UiModel`, native | 928 | 944 | 8 |
| `UiModel`, mobile / gateway ABI | 912 | 936 | 8 |
| `UiChrome`, all three | 20 | 20 | 2 |
| Full `HomeCapture`, all three | absent | 9 | 1 |

The sum of one model, one frozen state, one frozen snapshot and one frozen chrome grows **64 B native / 72 B on either board ABI**. This is a scratch-header layout measurement, **not linked RAM**. In particular it does not predict added gateway image RAM: the ruled gateway image does not compile the UI. The coder still measures and attributes the actual board pair. No alternate allocation is proposed.

## 2. Required corrections

### W4B-1 — MAJOR: the Home-origin exit after saved-key blanking is excluded by the routing rule

**Brief §2.5, lines 279–286; model `on_tick`, `provision_gesture`, `close_provisioning` at 3016, 4511, 4357.**

The brief correctly retains the OQ-3 cancellation: blanking a saved-key offer calls `enter_provision(Provision::menu)`, with the Home origin surviving. It then promises that the next explicit exit honors that origin. But the origin-routing rule applies **only** to sites that currently return to the PROVISION menu. The menu's own BACK calls `close_provisioning()`, which instead sets `Settings::browsing`. That exit is excluded by the literal rule.

Concrete trace: Home JOIN → acknowledged join with saved-key offer → blank → PROVISION menu → wake → BACK. Keeping the source destination for the last step lands in the Settings menu, whereas the Home-origin return promise requires Home on the opener (or the first item if the opener vanished).

**Required:** explicitly include `ProvRow::back` / `close_provisioning` in the origin table. State its Home, Settings and none destinations separately; retain the current Settings/none behavior. Clarify origin handling if a fresh action is chosen from the safety-returned menu. The blank itself must retain its existing destination; do not replace the OQ-3 cancellation with Home navigation. Add the complete trace to the native origin matrix, with the Settings-origin counterpart. This is completion of the agreed return contract, not a request to change the blank ruling.

### W4B-2 — MAJOR: complete the control disposition list before dispatch

**Brief §2.9, lines 371–383; probe runner `ctl` entries C102–104, C108, C123–124 at 1025–1033, 1059, 1163–1169.**

The named retirements stop at C121–122 for the removed Home mark. **C123 edits the exact same bitmap call.** A read-only sed witness confirms that it changes the current source and makes no change after deleting only that required-to-disappear call. It therefore needs an explicit disposition too.

Two neighboring dependencies also need named treatment: **C124** injects a forbidden body rectangle using the old mark constants; **C108** injects the incorrect x40 TEAM draw through `status_text`. If those obsolete helpers/constants disappear, retaining these controls verbatim produces must-build failures. The negative properties remain valid. Do not keep dead product helpers solely to make their mutants compile.

Conversely, the blanket C90–104 retirement includes **C102–104**, whose meanings survive the move to My device: frozen coordinates, no invented fix, and a valid fix when either coordinate is nonzero. C103/C104 mutate the still-live `build_snapshot` publisher, not a retired STATUS formatter. A pure formatter replacement cannot cover that publisher.

**Required:** name C123's retirement/replacement; re-anchor C108 and C124 as necessary while preserving their forbidden-origin/rectangle checks; retain C102–104 or name equivalent **real-renderer/publisher** controls on My device. Record each expected targeted failure, not just a new label or a total control count. Keep the stock full run and Stage A's completeness mechanism authoritative. These changes fit the existing instrument fence.

Also make the **W41** reader explicit in the implementation notes: `tools/probe_board_ui/run.sh:1042–1046` requires the existing bare selection-box statement. Adding braces around it to draw the new gutter would fail that reader despite equivalent box behavior. Preserve that statement and add the cue separately, or return for a narrowly scoped reader amendment; do not absorb B418. Include the affected board-UI supplemental run in QA's §4.2 list and require the same known W49/W51/W54 failure set.

### W4B-3 — MAJOR: assign publication and frame-refresh proof to the real firmware-UI probe

**Brief §2.4 lines 244–255 and §2.9 lines 407–419; `platformio.ini:78`; UI `build_snapshot`, `mr_ui_tick` at 721, 2530, 2573–2619.**

The production contract requires the counted own name to be published by `build_snapshot`, body-only changes to request a new frame, and each in-progress OLED frame to keep one frozen snapshot. The listed proof places “frozen-frame repaint of the name, team, profile, fix and focus” in the **native** `on_gesture` / `on_tick` matrix. Native does not compile `firmware_ui.cpp`. Injecting a correct `UiSnapshot` into the model cannot detect a missing publisher, a renderer reading live config between pages, or an unwired body-invalidation call.

The six proposed new renderer controls cover useful geometry/text cases, but do not assign this new publication/refresh contract a real-TU positive and negative witness. Existing coordinate P17b is a useful starting point; C102 is precisely its live-config counterexample.

**Required:** keep native projection/navigation tests and add an explicit firmware-UI probe subsection:

- Drive the real own-name producer for empty, short and 32-byte names. Check Home's 16-cell identity and My device's two counted, sanitized rows, including high-byte input.
- Change the name during an in-progress page sequence: all remaining pages retain the old name and the next eligible complete frame uses the new name. Retain the analogous coordinate proof after its move to My device.
- Change visible name, full team/local ID, profile and fix **without a navigation press or an unrelated strip change**; check the relevant Home/My-device body refresh. Keep focus/cue frame consistency and blank/quiet behavior covered at their existing boundaries.
- Require controls for omitted/wrong name publication, live-name rendering, and omitted body-invalidation wiring, each RED on the intended real-renderer assertions. Reuse existing controls where their effect fits; no extra resident state or production hook is authorized.

This closes the wiring-gate obligation in the roles document and P7. It does not require broader board builds or a new package.

### W4B-4 — MINOR: remove the stale allocation disclaimer

**Design §11.1, lines 982–995.** The new owner-approved W4b allocation is immediately followed by the old statement, “Apart from the catalog estimate the owner accepted with D7, no allocation is granted by this document.” Qualify that sentence to include the W4b grant. Refresh the design's pin in the revised brief. The allocation itself is settled and reproduced above.

## 3. Accepted parts and gate assessment

The one-feature package with an instrument-first B456 stage is appropriate (C1/P6). Reusing `Screen::status`, `ListView`, the existing compose engine and the measured frozen carriers avoids a parallel navigation or send path. The following author decisions consistently address the pre-check: capability-filtered lists; key-missing precedence; zero-count omission; Settings preview without an arrow; Settings MENU retaining `on_back()`; the `OPTIONS CHANGED` consume-next-press table; frozen blocked-admission reason; and Send's explicit catalog-change refusal. No editor, splash, card, NV, wire or core change is authorized.

B456's expected-versus-observed label sets, exact one-verdict rule, C0 exception, guard/127 cases, and equal-count duplicate-plus-missing regression are sound as specified. Its bypass control must exercise the stock runner's final accounting path. Stage A's product and original-control meanings remain frozen while Stage B changes the navigation witnesses.

The brief keeps paired board measurements under `.pio-measure/`, with no intervening mutation; makes the coder derive its figures; retains corpus and ABI obligations; distinguishes coder union/census from the P6 independent gate; and preserves all previously QA-passed candidates. The proposed metal additions concern pixels, glyphs, physical wake and real setup, which are appropriate M2 residue. They remain proposals until implementation QA PASS.

## 4. Evidence and limits

The [evidence directory](2026-09-27-standalone-mobile-home-w4b-brief-review/) contains the full starting inventory, preflight checks, fresh layout results, reproducible read-only source/anchor witnesses, and final preservation receipt, covered by `SHA256SUMS`. `source-audit.py` runs sed only on in-memory strings; it does not build or run mutants and is not claimed as a runtime gate.

This turn re-ran input verification, source/reader checks and the three-ABI structure measurement. It did **not** re-run native, corpus, UI/board probes, mutations, board images, tools discovery, warning census or metal. Those pre-check results are already pinned in the brief; no executable input changed during review. This is a **brief review**, not implementation QA.

No reviewed document, production file, test, tool, register, metal plan or simulator input was edited. No new production defect number was assigned: W4B-1–4 are brief/design corrections for the author, recorded here without mutating the pinned register. B456 and B418 keep their existing status.

Return a revised brief with the corrected transition, instrument dispositions and real-renderer proof, the minor design wording fix, and refreshed hashes for documents the author changes. The next review is scoped to these corrections and pin/preservation checks; the accepted allocation and settled product decisions need not be reopened.
