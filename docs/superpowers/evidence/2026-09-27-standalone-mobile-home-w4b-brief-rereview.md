<!-- Author: Codex, independent Quality Agent; scoped review of Claude's W4b revision 2 -->
# W4b revision 2 — scoped brief re-review, 2026-09-27

**Verdict: PASS with one minor fold-in (W4BR-1).** The four findings in the [first review](2026-09-27-standalone-mobile-home-w4b-brief-review.md) are addressed. Apply the exact one-cell correction below, then proceed without another review. The approved allocation and product decisions are not reopened.

Reviewed [brief revision 2](../plans/2026-09-27-standalone-mobile-home-w4b-home-navigation.md), SHA-256:

`75893d37bd1ff4f3ab4404891d833f99458f87a11b4cc053d105a4341576e419`

**Authorized brief after the exact fold-in:**

`a20e2dbf70ddf10aaeb67590ed7de1d816ec1e64ed520b3d69ac1713709b4f64`

The [patch](2026-09-27-standalone-mobile-home-w4b-brief-rereview/required-fold-in.patch) passes `git apply --check`; it has **not** been applied. Only the C99/C124 must-fail table cell changes. Leave the DRAFT header, revision number and pinned documents untouched: this receipt records authorization, and changing those bytes would invalidate the authorized hash. A commit is not required. The coder still performs source-validation and all brief-prescribed gates; this is not implementation QA PASS.

## Independent input checks

- MeshRoute remains `8360802904f7bd0023279d3da844453d61207ede` plus the uncommitted, QA-passed W1c/W3/W4a candidates and landings. Simulator remains `6585649ea5a780f0542b2931853a667be56a5b2b`, clean; neither repository has staged changes.
- **20/20 input hashes, 14/14 source line counts and 7/7 links match.** The B456 tools-test path is absent as declared.
- **14/14 pre-check evidence files and 8/8 first-review evidence files verify**, including the first review's original report hash. Earlier QA evidence is unchanged.
- The pre-check inventory differs only in the four permitted existing documents: design, register, `tracker.md`, `MEMORY.md`. Added paths are the named pre-check, brief and first-review artifacts. No implementation input changed and no unexplained path appeared.
- This review captured **1,603 MeshRoute paths and 285 simulator paths** before adding its evidence. The final preservation check covers every one of them.

## Finding dispositions

| Finding | Scoped result |
| --- | --- |
| **W4B-1 — Home return after saved-key blanking** | **PASS.** §2.5 explicitly includes `ProvRow::back` / `close_provisioning`: Home origin returns to its opener or item 1; Settings and none return to Settings browsing. Actions from the PROVISION menu retain the existing origin. The Home and Settings saved-key → blank → menu → BACK traces are required native cases. Source still confirms the blank destination at `UiModel::on_tick:3016`, the BACK call at `provision_gesture:4511`, and the current Settings destination at `close_provisioning:4357`. The cancellation itself is unchanged. |
| **W4B-2 — renderer-control dispositions** | **Major correction satisfied.** §2.9 names C123 with the removed bitmap controls, preserves C102–104's frozen-position/publisher meanings, and permits C108/C124 re-anchors without retaining dead product helpers. C35/C84/C92–95/C99 also have explicit property-based dispositions. W41's exact box statement remains required; QA's §4.2 now includes board-UI's supplemental run with exactly the known W49/W51/W54 failure set. One must-fail cell needs the minor correction below. |
| **W4B-3 — real publication/frame-refresh proof** | **PASS.** New §2.10 assigns the actual producer, empty/short/32-byte/high-byte names, mid-frame rename, next-frame repaint and press-free body invalidation to the firmware-UI probe. Omitted publication, live-name rendering and unwired invalidation each need a compiling control that fails the intended renderer check. Native tests claim projection/navigation only. C102 preserves the coordinate counterpart. The probe's production TU and frozen `mr_ui_tick` path are the correct instrument boundary. |
| **W4B-4 — allocation disclaimer** | **PASS.** Design §11.1 now explicitly exempts both D7 and the W4b owner grant from its no-allocation disclaimer. The refreshed design hash matches. The previously reproduced +64 B native / +72 B board-ABI static-structure sum remains accepted; no new layout or linked-RAM claim is made here. |

## W4BR-1 — MINOR: distinguish C99's bitmap check from C124's rectangle check

The combined C99/C124 row currently requires both controls to fail “the chrome body-rectangle census.” Source confirms that they inject different canvas record kinds:

- `tools/probe_firmware_ui/run.sh:1010`, C99 injects `draw_bitmap(...)`.
- `tools/probe_firmware_ui/run.sh:1168`, C124 injects `draw_rect(...)`.
- `probe_main.cpp:612–613` records a bitmap with its byte pointer and an empty text tag; a rectangle has a null pointer and the `[rect]` tag.
- `body_rects_on_page` at `probe_main.cpp:427` counts only `[rect]` records. It cannot detect C99's injected bitmap. C99 must remain a forbidden-mark/bitmap witness; it must not become another rectangle mutation.

Replace only that row's final cell:

> the chrome body-rectangle census

with:

> C99: the forbidden-mark bitmap check; C124: the body-rectangle census

This corrects the assertion mapping while preserving both mutant meanings, the fence, allocation and gate chain. The attached patch defines the exact authorized bytes. No re-review or owner ruling is needed for this fold-in.

## Evidence, limits and preservation

The [checksummed evidence directory](2026-09-27-standalone-mobile-home-w4b-brief-rereview/) contains the input inventory, pin/checksum reconciliation, source witnesses, exact patch, conditional authorization and final preservation receipt. C99/C124 sed programs were applied only to in-memory source strings to confirm their edits; no mutants were compiled or run. Raw copies of the reviewed and prospective authorized brief are identified in `authorization.json` under ignored `artifacts/`.

This scoped review rechecked source and input integrity. Native, corpus, board builds, ABI measurements, probes, mutation batteries, discovery, warning census and metal were **not rerun**: the executable inputs and accepted layout are unchanged. Their actual implementation-gate obligations remain intact.

Only this review report and evidence were added. No brief, design, register, tracker, memory, production, test, tool, prior evidence or simulator input was edited; nothing was staged or committed. B456 remains open until the repaired instrument passes independent implementation QA. No new production finding was assigned a register number; W4BR-1 is an authoring correction recorded here.
