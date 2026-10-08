Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# Standalone Home W7+W8 — the one-button editor, the panel rename and written messages: coder report

**Role:** coder. **Contract:** brief **revision 2**,
[`docs/superpowers/plans/2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md`](../plans/2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md),
SHA-256 `47cb5a461d07cc3cbaf0ab6ae81fe535e206eda996201885f4dbe878dd11cb8e` (732 lines), authorized by the
[re-review](2026-10-04-standalone-mobile-home-w7w8-brief-rereview.md); plus the QA checkpoint return
[`2026-10-06-standalone-mobile-home-w7w8-stop-resolution.md`](2026-10-06-standalone-mobile-home-w7w8-stop-resolution.md),
SHA-256 `8eabbaa9f973c74766acffb63a85cad4460c320220e772453d25fbfb8ad8eb0e` (Option A: B498 guards, B499 runner).
**Base:** owner commit `4c1a000` with the §1 uncommitted inputs; simulator `6585649`, clean and read-only throughout.
**Evidence:** [`2026-10-04-standalone-mobile-home-w7w8/`](2026-10-04-standalone-mobile-home-w7w8/), sealed by its own
`SHA256SUMS`; every checkpoint note and ruling is in its [`ledger.md`](2026-10-04-standalone-mobile-home-w7w8/ledger.md);
raw logs under the ignored `artifacts/2026-10-04-standalone-mobile-home-w7w8/`, indexed with hashes in
[`receipts/raw-logs.txt`](2026-10-04-standalone-mobile-home-w7w8/receipts/raw-logs.txt); board pairs under
`.pio-measure/w7w8/`. **Nothing staged, committed, reset, cleaned or discarded.**

**Verdict: ready for independent QA.** The complete §4.1 chain ran once, fresh, start to finish on one freeze
(`receipts/run-final.sh`, every step exit 0, `DONE`), after the authorized checkpoint repairs. The union is
**16 batteries / 674 entries, 674 RED, zero unusable**.

## 1. Preflight

- `scope.py preflight` → [`preflight.json`](2026-10-04-standalone-mobile-home-w7w8/preflight.json): **PASS**.
  HEAD `4c1a000`; simulator `6585649`, clean; nothing staged; `git diff --check` clean; the brief at its authorized
  hash (732 lines); all 14 fenced inputs at their §1 hashes and line counts; the two new paths absent; 24 read-only
  inputs and 12 preparation inputs verified; the five sealed folders verify (W7 pre-check 26, W8 pre-check 30, brief
  review 10, re-gate 40, re-review 6). Against the W8 pre-check's `inputs.json` (2495 MeshRoute, 285 simulator paths):
  only the four preparation files changed, nothing missing, every new path explained.
- **Resume after the checkpoint return:** the return's hash and its seal (16/16) verified; the checkpoint inventory
  (2569 paths) matched the tree except the register at its authorized `5ad4e8ac…0c835`, and the receipt itself.

## 2. Baselines (the unmodified tree, before any edit; `run-baselines.sh`, sequential, every step exit 0)

| Instrument | Base result |
| --- | --- |
| native (`pio test -e native`, then the binary) | **3059 cases / 199638 assertions / 0 failed / 0 skipped** |
| corpus (fresh stock `lus` `e304147d…caa2`) | **36/36**, anchors 36/36; s18 269517 events, `32afbf11e43b4bf9d0bd470ad502ba0a` |
| boards `pair --jobs=1` into `base-1` / `base-2` | both stock `compare` **PASS**; gateway RAM 203740 / flash 572096; heltec_mobile RAM 219540 / flash 1400540 |
| warning census | **PASS**, 6 OLED envs |
| ABI | **290** checks, 9/9 controls RED |
| firmware-UI default | **591 / 1059 / 591**, **240** controls accounted |
| board-UI default | **592** identities exactly once |
| tools discovery / inventory | **447** OK / **197** rows |
| static census | selector (a) 375, (b) 225, **15 batteries / 600 entries** |

Every figure equals the brief's. Recorded in [`baselines-base.json`](2026-10-04-standalone-mobile-home-w7w8/baselines-base.json),
[`union-census-base.json`](2026-10-04-standalone-mobile-home-w7w8/union-census-base.json), `boards/base-*`,
`receipts/corpus-manifest-base.json`.

## 3. Checkpoint notes (detail and every ruling in the ledger)

1. **The pure editor** — `src/firmware_ui_editor.h` (the 42-byte repertoire, the three rings, the draft operations,
   the window, the ring rows) and `test/test_firmware_ui_editor.cpp` (17 cases); the new `uieditor` battery
   (25 entries). Native 3076 / 201482.
2. **The name flow (W7)** — storage at §2.2's sizes; My device's ` CHANGE NAME >BACK`; the editor; `SAVE NAME?` with
   `WAS`; the one-shot request seam (`take_name_request` → `ui_service_name_request` → `mrfw::rename_node`, after the
   drains, outside the send busy gate); the results; the unnamed-device prompt after `provision_admit`. Native
   3091 / 201858.
3. **Written messages (W8)** — `dm_text` / `channel_text`; the typed WRITE row; the capture-bound DM label; the
   written review (` SEND >EDIT     n/m`); `BUSY`; the per-kind gate with D19 at execution only; the written composer
   (never `-l`); the outcome record and its 18-value classification; the tracker family; `long_fire`'s withdrawal.
   Native 3118 / 202782.
4. **Instruments** — firmware-UI probe (hline recorder, honest `rename_node` stand-in, the D19 fixture, phase P32,
   +23 controls); board-UI W55/W56 (+11 identities); mutation entries and re-anchors; ABI pins and supplemental.
5. **Development union** (the full union before the chain, as the brief asks): 665 / 674. This package's four
   (M52 — my model-header `static_assert` broke M52's compile; W8-M04 — an equivalent mutant; W8-M05 and W8-S12 —
   survivors) were fixed in the fence; the §2.5 edge-timing cases found uncovered were added (+5 cases). Five
   pre-existing entries (Y02, Y07, U13, W6-P1, W6-P3) were UNUSABLE on the exact base tree too → **STOP-2 to QA**.
6. **Checkpoint return (Option A)** — the three B498 null-safe guards verbatim from `proposed-guards.json`
   (`test_firmware_ui_model.cpp` 1; the newly fenced `test_firmware_ui_presets.cpp` 2); B499's `step()` repair with
   its synthetic proof; `scope.py`'s named checkpoint deltas. Healthy counts unchanged (3123 / 203168); the five
   entries re-run on the candidate: Y02 RED (266), Y07 RED (3), U13 RED (165), W6-P1 RED (7), W6-P3 RED (10).

## 4. The diff and the §2.9 ledger

| File | Change |
| --- | --- |
| `src/firmware_ui_editor.h` (new, 367 lines) | the pure editor (E1–E4, rings, draft, window, notes) |
| `src/firmware_ui_model.h` | +761 / −26: storage, name flow, prompt, written flow, gate wiring, outcome record, trackers' predicate |
| `src/firmware_ui_send.h` | +116 / −18: kinds, family helpers, `SendLive.team_local_id`, `send_dest_gate_of` / `send_exec_gate_of`, written composer, editor capture |
| `src/firmware_ui_chrome.h` | +2 / −1: `HomeView::name_prompt` (rail STATUS) |
| `src/firmware_ui.cpp` | +137 / −4: name drain, live answers, peer name, editor/name/prompt/result rendering, the editor-grid `static_assert` |
| `test/test_firmware_ui_editor.cpp` (new, 523 lines) | 17 cases |
| `test/test_firmware_ui_model.cpp` | +710 / −76: 16 new `w7-` cases; re-expressions (§6); B481 retitle; the B498 guard |
| `test/test_firmware_ui_send.cpp` | +1212 / −82: 30 new `w8-` cases; signature updates |
| `test/test_firmware_ui_chrome.cpp` | +27 / −1: 1 new rails case; one `SendLive` initializer |
| `test/test_firmware_ui_presets.cpp` | +3 / −3: the two B498 guards only (fenced by the checkpoint return) |
| `tools/probe_firmware_ui/probe_main.cpp`, `run.sh` | +523 / −22, +105: P32, the recorders, the fake, re-expressions; +23 controls |
| `tools/probe_board_ui/run.sh`, `expected.tsv` | +36, +11: W55 (name drain) and W56 (draft-source wiring) — +11 rows; `accounting.py` unchanged |
| `tools/probe_ui_model_mutations.py` | +308 / −20: `uieditor` (25), +33 model / +14 uisend / +2 chrome entries, 9 re-anchors, the pin |
| `tools/probe_board_abi.py` | +22 / −9: the three stock re-pins |

**Recorded behaviour changes (§2.9), and nothing else:** My device's row; the unnamed JOIN/CREATE prompt; the WRITE
rows and GRANT KEY's index; D19's phrase refusal (`NOT SENT` / `NO TEAM ID YET`); written messages; the name and
message renders, rails and results; B481's title. Unchanged by measurement: the corpus (36/36 identical), the gateway
image (byte-identical payload and ELF), the 197-row command inventory, the 202/206-row transcripts.

**Rulings** (all in the ledger with their costs): R1–R3 (E4 rows, ring marker cells, CHECK only), R4 (DM binding and
label captured by the device in the tick), R5 (`binding_seen`), R6 (landing on WRITE), R7 (D19's mechanism:
`send_exec_gate_of`), R8 (the review header shares `review_header`), R9 (M52's tie moved to the renderer), R10
(W8-M04 reshaped), R11 (Y02/Y07 held for the single ruling — superseded by Option A).

## 5. Every §4.1 figure, from the fresh chain (`receipts/final-runs.tsv`, all exit 0)

| Step | Result |
| --- | --- |
| 0 B499 proof | the real `step()`: fault exit 1, one row, no continuation; healthy exit 0, both rows, `DONE`; the stop-removed control is caught ([`final/b499-proof.json`](2026-10-04-standalone-mobile-home-w7w8/final/b499-proof.json)) |
| 1 scope + inputs | **PASS** at start and end: only fenced files and this evidence differ; read-only 24/24; preparation 13/13 (the register at `5ad4e8ac…`, the receipt at `8eabbaa9…`); seals incl. the receipt (16); the checkpoint inventory: only the two guarded tests, this evidence and the register at its authorized hash differ; simulator clean |
| 2 native | `pio test -e native` on the frozen tree, then the binary: **3123 / 203168 / 0 failed / 0 skipped** (an incremental build: no native input changed since the checkpoint build; every union worker cold-builds the frozen tree and derives the same 3123 / 203168 / 0) |
| 3 corpus | fresh stock `lus`, byte-identical `e304147d…caa2`; **36/36**, anchors 36/36; **36/36 streams identical on all 14 per-stream fields** to baseline step 3 |
| 4 boards | `final-1`, `final-2`; stock `compare` **PASS** for gateway and heltec_mobile — with the baseline's two, **four PASS**; eight manifests in `boards/`; attribution in §7 |
| 5 ABI | stock **290** checks, 9/9 controls RED; supplemental **335** checks, 9/9 |
| 6 warning census | **PASS**, 6 OLED envs at their pinned baseline |
| 7 firmware-UI | **656 / 1130 / 656** checks (l2 / v3 / BLE row); **263** controls verified, 263 accounted exactly once, 0 unusable |
| 8 board-UI | **603** identities exactly once (592 + W55 4 + W56 5 controls + 2 wiring checks); canvas 124/110, traits 14, structural 23, wiring 62 + 195 controls, negctl 60/3 |
| 9 mutation union | **16 batteries / 674 entries, 674 RED, 0 unusable / vacuous / missing**; every worker baseline 3123 / 203168 / 0 ([`final/union-verify.json`](2026-10-04-standalone-mobile-home-w7w8/final/union-verify.json)) |
| 10 discovery / inventory | **447** OK, no skips (no tool test added or changed); **197** rows byte-identical |
| 11 safety nets | inbox-verbs default, all five arms PASS: accept 1400 / 63, client 483 / 71, oled 27 / 7, identity_accept 175 / 13, identity_client 176 / 13; transcripts **202** (full_headless) and **206** (mobile) rows, each fresh pair `COMPARE: identical` |
| 12 reader audit | PASS ([`final/reader-audit.json`](2026-10-04-standalone-mobile-home-w7w8/final/reader-audit.json)) — see below |
| 13 input stability | **PASS**: scope start = end on all 14 groups |

**The union, per battery (RED / entries):** model 297/297, chrome 50/50, uisend 40/40, sliceCbudget 1/1,
sliceCsend 1/1, w4aident 9/9, w4bhome 26/26 (selector (a) **424**); uistatus 19/19, uiteam 20/20, uiinvite 32/32,
uiprov 45/45, uijoin 26/26, uipresets 39/39, config 32/32, consoleline 12/12 (selector (b) **225**); uieditor 25/25.
Static census: every pattern matches its target exactly once; **re-anchors: 9** — model S03, W10, Y05, Y06, W6-M08,
W6-M09; uisend W6-S02, W6-S10; w4bhome H24 (each keeps its property and match count; `w4aident`'s table is identical
to base). New entries: 74 (uieditor 25, model 33 incl. B480, uisend 14, chrome 2).

**Reader audit (D6/P7):** `SendKind`, `ComposeRow`, `ReviewPhase`, `SendLive`, the trackers and the new gate helpers
have readers only inside the fence. Outside it, and byte-identical to HEAD (their predicates cannot move): `SendReq`
in `device_nv.h`, `firmware_commands.h`, `firmware_ui_presets.h`, `firmware_ui_preset_verbs.h` — comments only
(`firmware_ui_presets.h:88` still says `SendReq` carries `{slot, generation}`; it now also carries the appended
`draft_id`, a read-only file's comment left for QA); `send_gate_of` in the same two preset headers — comments;
`ui_nav_slot` in `firmware_ui_status.h` — a comment; `HomeView::list` in `test_firmware_ui_team.cpp` — a fixture
predicate for a named device, unaffected by the unnamed-device prompt (the team cases pass). `SendReq{…}`
aggregate initialisers: model 4, send test 46, mutants 2 — all positional, so the appended `draft_id` is 0 where it
is not named. Whole-tree scanners: `check_data_type_literals` PASS (220 files) + selftest 4/4; ownership O1–O13 PASS
+ 43/0 controls; build identity 27 checks, 12/12 controls.

## 6. Classification of every changed assertion and control

**Native — pre-existing cases** ([`final/changed-cases.json`](2026-10-04-standalone-mobile-home-w7w8/final/changed-cases.json)):
- *Recorded behaviour change — the WRITE row / GRANT KEY's index* (positional re-expressions, property unchanged):
  `sub-view: back leaves…`, `the channel list's last row…`, `the compose cursor wraps…`, `declared bounds`,
  `ui7-B66`, `ui16-k7-act`, `ui16-k7-self`, `ui16-k7-keyless`, `ui10-p3-slot`, `ui10-p3-row`, `ui10-p3-r1`,
  `w4b-menu`, `w4b-wrap`, `w4b-send labels`; retitled `ui10-p3-empty` (… offers WRITE MESSAGE and `back`) and
  `w4b-send … covers … item 1 (WRITE MESSAGE)`.
- *Recorded behaviour change — My device's row:* retitled `w4b-items: MY DEVICE — a short toggles CHANGE NAME / BACK…`.
- *§2.2 re-pins* (UiState 576/568, UiModel 1216/1200): `ui16-reqpubkey-resources`, `ui16-k7/k5/k6-resources`,
  `ui10-p3-resources`, `w4b-resources`.
- *Signature only* (meaning unchanged): `SendLive` 3 → 4 members (`w6-review`, `w6-race`, `w6-gate`, `ui-frame F2`,
  `chrome-nav`), the composer's appended `DraftView{}` and the executor's `kLiveWithId` (38 send cases, `-0 +0` or
  one-for-one).
- *Fixtures:* `nearby_snap` / `home_snap` carry a name (a named device goes straight on).
- *B481:* `ui-model: under the alarm overlay the compose gestures are ABSORBED, and the queued alarm is kept` —
  retitled and recommented, assertions unchanged.
- *B498:* three comparisons made null-safe (`ui10-p3-empty`, `ui10-p1-defaults`, `w6-v1`), every assertion kept,
  healthy counts unchanged.

**Native — new:** editor 17 cases; model 16 `w7-`; send 30 `w8-`; chrome 1 `w7w8-chrome`.

**Firmware-UI controls:** 240 → **263**: +W7-C1…C13, +W8-C1…C7, C9, C10, C12, each with its classified outcome. The
re-expressed checks (P17m/P17r, P24k7b/d/f/e, P27b/b2/e, P29b, P31e) keep their contracts; no control retired or
weakened. **Coverage roll-up** 872/1031 → 939/1102; four pre-existing checks are no longer reddened by any control —
**B500**, registered by QA, non-blocking; attribution in [`probe-attribution.json`](2026-10-04-standalone-mobile-home-w7w8/probe-attribution.json)
(incidental cascades only; the P29 cause is inferred, not traced).

**Board-UI:** W55 (4 controls) and W56 (5 controls) plus their two wiring checks; every existing predicate holds.

**Mutation:** 9 re-anchors, 74 new entries, W8-M04 reshaped (development, before any freeze).

## 7. Sizes and RAM/flash attribution ([`final/board-attribution.json`](2026-10-04-standalone-mobile-home-w7w8/final/board-attribution.json))

| Type | native | heltec_mobile | gateway |
| --- | --- | --- | --- |
| `UiState` | 568 → **576** | 560 → **568** | 560 → **568** |
| `UiModel` | 1016 → **1216** | 1000 → **1200** | 1000 → **1200** |
| `SendReq` | 16 → **20** | 16 → **20** | 16 → **20** |
| `Draft` / `EditorView` / `NameOrigin` / `WrittenOutcome` | 176/4, 10/1, 1/1, 4/1 | same | same |
| `SendLive` | 12 (unchanged) | 12 | 12 |
| `UiSnapshot`, `UiChrome` | 1368, 20 (unchanged) | same | same |

- **gateway:** RAM 203740 → 203740, flash 572096 → 572096; payload and ELF byte-identical (only source/`.pio`
  provenance fields differ) — as predicted, its stock image has no OLED TU.
- **heltec_mobile:** RAM 219540 → **219748 (+208)** = exactly the two §2.2 objects, `s_model` 1000 → 1200 (+200) and
  `s_frame_state` 560 → 568 (+8), alignment +0. Flash 1400540 → **1407844 (+7304)** = `.flash.text` +6728
  (73 changed symbols summing +6524, the rest alignment/literal pools; largest: `compose_gesture` +507,
  `editor_ring_rows` +486, `editor_gesture` +475, `mr_ui_tick` +421, `ui_chrome` +418, `home_activate` +320; the
  send path moved out of line: `mrui::ui_perform_send` +530 / the inlined copy −421) + `.flash.rodata` +368 +
  `.dram0.data` +208 (the two objects' initialized images).

## 8. PIN

PIN re-synced? YES — native 3059/199638 + 64 cases / 3530 assertions = **3123/203168** (model 437/8244 → 453/8808,
send 120/1051 → 150/2121, chrome 41/2000 → 42/2052, editor 0 → 17/1844, every other file unchanged; B498's guards
move no healthy count — presets 32/1356 before and after); corpus 36/36 identical; §2.2 sizes UiState 576/568,
UiModel 1216/1200, SendReq 20, Draft 176, EditorView 10, NameOrigin 1, WrittenOutcome 4, SendLive 12; firmware-UI
656/1130/656 with 263 controls, board-UI 603; union 16/674 all RED with `uieditor` 25; ABI 290 / 335; discovery 447;
inventory 197.

## 9. Freeze inventory (the frozen tree, from [`final/scope-end.json`](2026-10-04-standalone-mobile-home-w7w8/final/scope-end.json))

| File | SHA-256 | Lines |
| --- | --- | --- |
| `src/firmware_ui_editor.h` (new) | `c197a80c792b7dc72dab47a238b60e0bbd6088155f2eaa3299e4337fa6b02c4f` | 367 |
| `src/firmware_ui_model.h` | `0355c83ccf3a5d0680b76ddafe97bd7bc0e025e61bae29076c1098a37f150479` | 7213 |
| `src/firmware_ui_send.h` | `20b7d504fd98972de464e820101136f4e2d137fb4d8899922fea3926e0b401d0` | 885 |
| `src/firmware_ui_chrome.h` | `435ca5ff97e3ef5cee148b3b25193ea77e6a43a09a0cdd4d271466743e1b51bc` | 622 |
| `src/firmware_ui.cpp` | `5f37785391d681fe05b5718c34fc56606ff0bab77b704bf20ea29155badfd323` | 3069 |
| `test/test_firmware_ui_editor.cpp` (new) | `d99819f46354adb4c08d84e8c1faeaddb78602e20d2b255a85651fc4f94aedf9` | 523 |
| `test/test_firmware_ui_model.cpp` | `d013efc25900a7c78d35f5b820cf1bc83dc6d6a9179200d14880997ff13f212f` | 13161 |
| `test/test_firmware_ui_send.cpp` | `c0e2492a9a14e20e0bcae8cbca1ed0c563b1d51c1360922c713c3672c99eec48` | 4124 |
| `test/test_firmware_ui_chrome.cpp` | `e5e2fa4b5e76a9452a532ed0d6c80b86c930f480a96251e371388afc34d3939b` | 1415 |
| `test/test_firmware_ui_presets.cpp` | `9d7f5e8949cb4c7f3cdcf85b0296ddd9f16d679d5fae7cfc5f0d2da13b352ff6` | 1044 |
| `tools/probe_firmware_ui/probe_main.cpp` | `20927ee3cdb30d0236768a81dd756908bc716aa91e39b3a043891233a0f62a79` | 8387 |
| `tools/probe_firmware_ui/run.sh` | `2587c0ab4b5e665b070f46a0ca378913b28dc21a009da620c8d5232eb243dd52` | 2221 |
| `tools/probe_board_ui/run.sh` | `6903a42bb9f341204038f40f8e848290d43c6d9e54d58b68803ff1e2c3d01071` | 1701 |
| `tools/probe_board_ui/expected.tsv` | `f294cf1a5ccabdec04286b434e89f6eec68e38acadf2e24a54c7b8ec498aa004` | 609 |
| `tools/probe_board_ui/accounting.py` (unchanged) | `2a23f7521805f38c6a48cd24008cd9eeeac1e8ad7b0b2aa8e3a85e38398f1e09` | 256 |
| `tools/probe_ui_model_mutations.py` | `f8285aa249bf8c2211286a9cc21c336ce3dc88e8af67eb954c550341e558b693` | 13450 |
| `tools/probe_board_abi.py` | `6aae6ca35dae515dc8794367977befbbfeabd105076498f687e288a1c9ee762c` | 1100 |

Board manifests (`boards/`): base-1/2 gateway `fbdf8f35…`, heltec_mobile `5a73fb56…`; final-1/2 gateway
`09fceb76…`, heltec_mobile `c381c00d…` (each pair identical). Corpus manifests: `receipts/corpus-manifest-base.json`,
`receipts/corpus-manifest-final.json`.

## 10. Not run, and why

- **Metal** — §7 lands its residue (QA). **The full 109-battery union** — out of scope by §4.2; the static census proves
  the unrun tables unchanged: of 110 tables only chrome, model, uisend, w4bhome and the new uieditor differ from base
  by AST fingerprint — all inside the union.
- **Other boards** — the ruled pair only (gateway + heltec_mobile); the warning census covers the six OLED envs.
- **Nothing in the chain was skipped**, and no development or checkpoint result is credited as a chain result.

## Open items for QA

- **B498** — the three guards are in; the five entries are RED in the final union (closure is QA's).
- **B499** — `step()` stops on failure; the proof and its control are step 0 of the chain (closure is QA's).
- **B500** — the four incidentally unreddened probe checks; non-blocking, recorded with the attribution.
- A drifted comment in a read-only file: `src/firmware_ui_presets.h:88` (`SendReq` carries `{slot, generation}`) —
  `SendReq` now also carries `draft_id`.
