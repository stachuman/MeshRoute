Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# Standalone Home W4b — Home and navigation, with B456 first: coder report

**Role:** coder. **Contract:** brief **revision 3**,
`docs/superpowers/plans/2026-09-27-standalone-mobile-home-w4b-home-navigation.md`, SHA-256
`a89c075e6dab5184d68c06a31b9fd262c3bab5656bc140146a879e2608d8ca5a` — the P4 checkpoint re-pin after this coder's
STOP-1 ([receipt](2026-09-27-standalone-mobile-home-w4b-r3-repin.md), PASS; revision 2 was `a20e2dbf…4f64`, receipts
[review](2026-09-27-standalone-mobile-home-w4b-brief-review.md) HOLD and
[re-review](2026-09-27-standalone-mobile-home-w4b-brief-rereview.md) PASS). **Verdict requested: ready for independent
QA.** ⛔ Nothing staged or committed; the simulator untouched.

Evidence: [`2026-09-27-standalone-mobile-home-w4b/`](2026-09-27-standalone-mobile-home-w4b/) — the
[disposition ledger](2026-09-27-standalone-mobile-home-w4b/disposition-ledger.md), [reader audit](2026-09-27-standalone-mobile-home-w4b/reader-audit.txt),
`boards/`, `layout/`, `receipts/`, `recon/`, `stage-a/`, `SHA256SUMS`. Raw logs: `artifacts/2026-09-27-standalone-mobile-home-w4b/`
(ignored; hashed in §8).

## 1. Preflight and the inventory check

- **Start (revision 2):** the brief hash equalled the authorized one. **Base deviation, owner-ruled (Ruling 1):** `HEAD`
  was `c8e36d8` (owner commit "W4b", parent `8360802`), not `8360802`; the working tree was clean and its content
  equalled the pre-check `inputs.json` inventory (1576 paths; only the four preparation documents changed, 0 missing,
  35 new = the pre-check, brief and review receipts). The owner chose to proceed on `c8e36d8`. Simulator `6585649`,
  clean, 285 paths unchanged. 14/14 executable inputs hash- and line-matched.
- **STOP-1 (2026-09-27)** — the fresh whole-diff review found an emergency pre-emption over a closed configuration
  service leaving an invisible Settings menu (pre-existing via the TEAM-roster grant; W4b's wrapping menu made it
  inescapable). Settled by the author as design r2.22 §6.1 option (A), B457; brief revision 3; QA's P4 re-pin PASS.
- **Resume:** revision-3 hash checked on disk; re-inventory against the receipt's `inputs.json`: `HEAD c8e36d8` OK,
  1621 inventoried paths, **0 changed, 0 missing**, 8 new = the receipt and its folder; simulator `6585649` OK, 285/285,
  0 changed.

## 2. Baselines (unmodified tree), then stage A

**Baselines:**
- Firmware-UI probe: l2 529/529, v3 980/980, BLE 529/529; controls 236 verified / 0 unusable; coverage 804/952.
  Independent completeness (`recon/completeness.py`): 236 declared = 236 observed exactly once.
- Board-UI `--no-neg`: exit 1, FAIL exactly **W49, W51, W54** (B418); V3 124/124, V4 110/110, traits 14, missing 12,
  structural 23/23, wiring 56/59 with 171 controls.
- Supplemental layouts (three ABIs): UiState 504/8, UiSnapshot 1336/8, UiModel 928 | 912 | 912 / 8, UiChrome 20/2,
  FrameGate 28/4, TeamRow 40/4, InviteMember 20/4, InviteIdRows 26/1, OutcomeView 52/4, SettingsView 8/1 — equal to the
  pre-check, whose `layout-measure.py` rerun reproduced all five variants (`layout/layout-baseline.*`).
- Boards: `base-1` == `base-2` (stock `compare` PASS, `boards/compare-base-*.txt`); `gateway` 203740 / 571936 / 285
  (payload `97deca60…`), `heltec_mobile` 211724 / 1394840 / 329 (payload `820b8bd3…`).

**Stage A — B456 (product unchanged), frozen** ([`stage-a/receipt.txt`](2026-09-27-standalone-mobile-home-w4b/stage-a/receipt.txt)):
- `run.sh`: the expected set is every source-declared control (B227's fail-closed extractor + call-count agreement, no
  count literal); ONE verdict path (`record_verdict`); guard failures recorded (`record_guard_failure`: `once`, a failed
  `sed`, an undefined helper); ONE accounting call (`controls_final` → `account_controls`) shared by the real tail and
  `--selftest-accounting` — zero expected, extraction mismatch, duplicate declaration, MISSING, DUPLICATE, UNKNOWN, an
  outcome other than the accepted one (`build_fail_ok` for C0, `red` otherwise) and any guard failure fail the run.
  The old `[ "$n_bad" -eq 0 ] || rc=1` tail is gone, so the accounting is the only path from a bad verdict to FAIL.
  `--no-neg` prints "NOT RUN … NOT a gate" and ends "NOT A GATE", never PASS.
- `tools/test_probe_firmware_ui.py` (new): 19 cases (§2.8's list), including the **load-bearing** bypass (removing the
  one accounting statement from a copy lets a missing control exit 0; stock: 1).
- Run on the unchanged product: 529 / 980 / 529; 236 verified / 0 unusable; "236 declared, 236 exactly once …, 0 guard
  failure(s)"; PASS; independent count 236 = 236; every per-control RED count identical to the baseline.
- Frozen: `run.sh` `ed49e5b3…b237`, `tools/test_probe_firmware_ui.py` `50e517ed…2b16` (the test is unchanged at the
  freeze; stage B edited `run.sh` only in its control declarations — the accounting block is byte-identical).

## 3. Diff and the ledger

`git diff --stat` (tracked, fenced): 

```
 src/firmware_ui.cpp                    |  177 ++--
 src/firmware_ui_chrome.h               |   25 +-
 src/firmware_ui_model.h                |  536 +++++++++--
 src/firmware_ui_status.h               |  256 +++--
 test/test_firmware_ui_chrome.cpp       |   68 ++
 test/test_firmware_ui_model.cpp        | 1638 ++++++++++++++++++++++++++++++--
 test/test_firmware_ui_send.cpp         |   21 +
 test/test_firmware_ui_status.cpp       |  446 +++++----
 test/test_firmware_ui_team.cpp         |   14 +
 tools/probe_board_abi.py               |   34 +-
 tools/probe_firmware_ui/probe_main.cpp |  612 ++++++++----
 tools/probe_firmware_ui/run.sh         |  292 +++++-
 tools/probe_ui_model_mutations.py      |  349 +++++--
 13 files changed, 3645 insertions(+), 823 deletions(-)
```

plus the new `tools/test_probe_firmware_ui.py`. Every changed, retired or added expectation, probe check, control and
mutation is in the [§2.9 disposition ledger](2026-09-27-standalone-mobile-home-w4b/disposition-ledger.md), re-derivable
from `git show HEAD:` by the `recon/` scripts:
- native: 380 → 421 model cases (41 new: 38 `w4b-` + 3 `b457`), 17 changed + 3 renamed with reasons, 16 prefix-only;
  status 8 retired → 9 new, 4 kept (3 with the width constant renamed); chrome +3, 1 changed (+1 assertion); team/send fixture
  helpers only (counts identical);
- probe checks: 840 → 873 labels — 817 verbatim, 6 changed expression, 17 retired, 50 new (incl. P3v, P17m/n/g/r, P29d);
- controls: 236 → 238 — 219 verbatim, 8 re-anchored, 9 retired (C83, C96–C98, C100–C101, C121–C123), 11 new
  (W4b-N1…N7 incl. N2a/N2b, W4b-W1…W3);
- mutations: `model` 239 (19 re-anchored incl. the revised contracts M101/M102/M109; M103 kept visible, §6),
  `uistatus` 13 → 19 (S01, S05–S12 retired with replacements; S14–S28 new), `chrome` 44 → 48 (X45–X48), new `w4bhome` 26
  (H01–H25 + H10b).

**Rulings made by the coder** (each with its cost if wrong, in the ledger): (1, owner) proceed on `c8e36d8`; (2) the
key-received landing is Home in list focus; (3) `HomeCapture::changed` serves both OPTIONS CHANGED and PRESET CHANGED;
(4) a same-tick press is consumed and the note stays — now written into design r2.22 §6.4; (5) grant-chain resume never
re-homes (OQ-3 verbatim), only terminal exits honour origin home; (6) Home INVITE triggers the ordinary SETTINGS-arrival
open; (7) `home_return()` resolves on the next sync; (8) after a committed alarm the Send list reopens on item 1;
(9) `uistatus` S01 retired as the brief lists it; (10) the probe's `leave_list` waits out the 2 Hz throttle.
**Settled by STOP-1 (not a coder ruling):** B457, design r2.22 §6.1.

**Review:** a fresh read-only reviewer (whole diff vs the brief and design) reported 1 Important (→ STOP-1/B457) and
4 Minor: five false comments + one stale test comment (fixed), Ruling 4 (now r2.22 §6.4), and the empty Send list's
note covering MENU (now r2.22 §6.5, with a native case). **Observation for QA, left verbatim by the fence:**
`test_firmware_ui_team.cpp`'s case "ui17-team: the interactive list's `BACK` row fits beside these…" still describes the
pre-W4b TEAM exit row in its title and comment; its assertions (`kListBackText == "BACK"`, the sub-view spelling) still
hold, and the MENU row's width is pinned by `ui17-lex`, `w4b-home` and P18b. The brief limits that file to navigation
fixtures, so it is unchanged.

## 4. Figures (the §4.1 chain, fresh, in order — `receipts/chain-steps.tsv`)

1. **Hygiene:** `git diff --check` clean; the ledger complete against the stage-A → final diff.
2. **Native:** fresh `pio test -e native`, then the binary: **3031 test cases, 198613 assertions, 0 failed, 0 skipped**
   (+45 / +1314 against the W4a pin 2986 / 197299, derived per file against a build of `HEAD` = the frozen base, which
   reproduces 2986 / 197299: model 380/6729 → 421/7913, status 12/115 → 13/182, chrome 38/1933 → 41/1996, team 23/161 and
   send 117/977 unchanged).
3. **Corpus:** a fresh stock `lus` (`cmake -S <simulator> -B /tmp/w4b-final-sim-…`), `run_corpus.py --require-anchors`:
   **36/36 streams, 36/36 anchors reproduce `simulation/BASELINE.md`**, `--validate` PASS; field by field against the
   pre-check manifest: **36/36 identical on all 13 stream fields**, the same BASELINE hash, inputs stable
   (`receipts/corpus-*`).
4. **ABI:** the stock `probe_board_abi.py`: **PASS, 290 checks, 9/9 controls RED, 0 unusable** at the re-pinned §2.2
   sizes. **Supplemental** (`layout/`): native / heltec_mobile / gateway — UiState 520/8, UiSnapshot 1368/8, UiModel
   944 | 936 | 936 / 8, UiChrome 20/2, **HomeCapture 9/1**; FrameGate 28/4, TeamRow 40/4, InviteMember 20/4,
   InviteIdRows 26/1, OutcomeView 52/4, SettingsView 8/1 unchanged.
5. **Probes:** firmware-UI default, all arms: **l2 557/557, v3 1023/1023, BLE 557/557** (baseline 529 / 980 / 529;
   stage A 529 / 980 / 529); **238 verified / 0 unusable**; the stage-A guard: "**238 declared, 238 exactly once with
   the accepted outcome, 0 guard failure(s)**"; the independent count **238 = 238, COMPLETE**; coverage 845/995; sources
   unchanged; PASS. `--no-neg`: "NOT RUN … NOT a gate … NOT A GATE". Board-UI `--no-neg`: failure set **identical** to
   the baseline (W49, W51, W54); V3 124, V4 110, traits 14, missing 12, structural 23/23, wiring 56/59 with 171 controls.
6. **Tools discovery:** **375 tests, OK, 0 skipped** (356 existing + 19 in the new `test_probe_firmware_ui.py`).
7. **Warning census:** **PASS** — the six pinned OLED envs match their warning baselines, `-Wswitch` 0 (gateway_heltec
   171, gateway_heltec_v4 175, heltec_mobile 175, heltec_v3 175, heltec_v4 179, heltec_v4_mobile 179).
8. **Boards, final:** `final-1`, `final-2` back to back on a quiet checkout (nothing ran between or during them); stock
   `compare` **PASS for both envs** (`boards/compare-final-*.txt`). Attribution in §5.
9. **Mutation union:** run fresh after the board pair ([`receipts/union.txt`](2026-09-27-standalone-mobile-home-w4b/receipts/union.txt)). **625 entries: 624 RED, 0 vacuous, 1 not RED (`model` M103, §6).** Every worker's clean baseline is **3031 / 198613 / 0** (= step 2), and every battery reports the real tree byte-identical before and after.

**Reader audit** ([reader-audit.txt](2026-09-27-standalone-mobile-home-w4b/reader-audit.txt)): every remaining reference
to a removed symbol is a comment; the four product files' readers are all gated above; the directory scanners rerun on
the frozen tree — `check_data_type_literals.py` PASS (218 files), `probe_features/ownership.py` PASS and `--controls`
43 verified / 0 unusable.

## 5. Allocation

- **Sizes** (three ABIs, §2.2 exactly): UiState **520**, UiSnapshot **1368**, UiModel **944** native / **936**
  heltec_mobile / **936** gateway, UiChrome **20**, HomeCapture **9** (align 1). Retained members added: `UiState`
  `home` + `home_view`; `UiModel` `_home_return` + `_setup_origin`; `UiSnapshot` `own_name[32]` + `own_name_len`;
  `UiChrome` `menu_cue`. Nothing else (B457 adds no state).
- **Static sum:** +64 native / **+72** on each board ABI (`s_frame_snap` +32, `s_frame_state` +16, `s_model` +24,
  `s_frame_chrome` +0).
- **Linked RAM** (`boards/attribution-base1-vs-final1.txt`, base-1 vs final-1, field by field): `heltec_mobile`
  `ram_bytes` 211724 → **211788 (+64)** = `.dram0.data` 25260 → 25324; the RAM-section OBJECT symbols that changed are
  exactly the three above (+72), 8 B absorbed by existing section alignment. `gateway`: every `measurements.*` and
  `artifacts.*` field identical — **payload `97deca60…` identical, as §2.11 predicts**. `toolchain.*`,
  `fixed_identity.*`, `paths.*`, `environment.*` identical for both envs; `source.*` differs (expected).
- **Flash** (`boards/flash-symbols-heltec_mobile.txt`): `flash_bytes` 1394840 → 1397696 (**+2856**) = `.flash.text`
  +2696, `.flash.rodata` +96, `.dram0.data` +64; sized FUNC/OBJECT symbols account for .text +2584 (the Home, My-device,
  note and navigation functions — `mr_ui_tick` +1056 as the renderer inlines Home's body; `home_gesture` +380,
  `ui_home_item_label` +242, `home_items_of` +199, `home_capture_refresh` +191, …; retired `ui_status_location`'s
  out-of-line copy −267, `body_back_row` −70 / `body_menu_row` +70) and .rodata −52 (the 24×24 mark asset, −72, is no
  longer referenced on the board; its source is kept for W5); the remainder is unsized literal pools and merged string
  literals (+112 .text, +148 .rodata — the new Home, note and key-help texts).

## 6. Controls and mutations

**Label reconciliation** (firmware-UI controls): baseline 236 → stage A 236 (all accounted) → final **238** = 219
verbatim + 8 re-anchored + 11 new; 9 retired (visible in `run.sh` with their replacements). Every new and re-anchored
control was also tried alone in a scratch loop before the run and went RED including its named check; the full run
verifies all 238 (0 unusable) and C0 still fails to build.

**Union by battery:** 

| Selector | Battery | Result |
| --- | --- | --- |
| (a) | `model` | 238 RED / 1 unusable |
| (a) | `sliceCbudget` | 1 RED / 0 unusable |
| (a) | `w4aident` | 9 RED / 0 unusable |
| (a) | `chrome` | 48 RED / 0 unusable |
| (a) | `uistatus` | 19 RED / 0 unusable |
| (a) | `w4bhome` | 26 RED / 0 unusable |
| (b) | `config` | 32 RED / 0 unusable |
| (b) | `uiprov` | 45 RED / 0 unusable |
| (b) | `uijoin` | 26 RED / 0 unusable |
| (b) | `uiinvite` | 32 RED / 0 unusable |
| (b) | `uiteam` | 20 RED / 0 unusable |
| (b) | `uisend` | 15 RED / 0 unusable |
| (b) | `sliceCsend` | 1 RED / 0 unusable |
| (b) | `uipresets` | 32 RED / 0 unusable |
| (b) | `uinearby` | 11 RED / 0 unusable |
| (b) | `uinearbyrow` | 9 RED / 0 unusable |
| (b) | `joinprofiles` | 21 RED / 0 unusable |
| (b) | `provservice` | 10 RED / 0 unusable |
| (b) | `uigeo` | 18 RED / 0 unusable |
| (b) | `icons` | 11 RED / 0 unusable |


**M103** (kept visible, not dispositioned by the coder — QA decides at the freeze): **not RED in the fresh final run** (`model`: "the suite still PASSES; nothing measures this"). The entry (unchanged) deletes BOTH `_st.cursor = 0` and `_cfg_sel_valid = false` from `close_settings_menu()`. At the freeze that helper has TWO callers: (1) the Settings `MENU` row (`CfgRow::back`), followed at once by `go_menu_home(s)`, which repeats both resets (`settings_follow_screen()` clears `_cfg_sel_valid` once the screen is STATUS; `_st.cursor = 0`); (2) B457's new rule in `sync_settings`, which leaves SETTINGS on its CLOSED view in menu mode — where neither field is read before it is re-established: the closed view's renderer ignores the cursor; a menu-mode `short` sees `list_len` = 1 and leaves with `cursor = 0`; `sync_settings` returns at its closed-view line before the only `_cfg_sel_valid` read; and a `double` reaches only `open_settings_menu`, which resets both. ⇒ measured non-RED, with that call-graph trace as the coder's evidence; **QA disposes** (the brief's §4.1 route). Pre-STOP it was non-RED too (one caller then).

## 7. The pin line

`PIN re-synced? YES — 3031 / 198613: +45 cases / +1314 assertions against the W4a pin 2986 / 197299 — model +41 cases
(38 w4b- + 3 b457 = +1125 assertions; +59 in the rewritten navigation expectations), status 12 → 13 (8 retired → 9
w4b-, +67), chrome +3 (+62; +1 in a re-prefixed case), team/send unchanged; measured per file by the full native binary
against a build of HEAD that reproduces 2986 / 197299.`

## 8. Freeze inventory

**Fenced files, final SHA-256** (every one byte-identical to the pre-chain freeze `artifacts/…/freeze-pre-chain.sha256`, checked after the last step; `tools/test_probe_board_abi.py` is unchanged — no dependent size assertion exists):

| File | SHA-256 |
| --- | --- |
| `src/firmware_ui_model.h` | `ddf0025148a7723b24b31e0893ee7017506ce2d14b2c17c2e381fd0b76e3e286` |
| `src/firmware_ui.cpp` | `3d32d7cb9c81f39b7188861c973b8213261783810dc6ca8428c0fe305b9e9fe9` |
| `src/firmware_ui_chrome.h` | `b4c1ff24451725a03d2e966fd1da3a8102c61815b74cc995f1fd8f2c0ee6a181` |
| `src/firmware_ui_status.h` | `338548341e579a3faac514592103cda794e2cca8cd1dd5fcc28a3a645d4c38cd` |
| `test/test_firmware_ui_model.cpp` | `3ac8c6f2457c4d784a0661a531ef467ace82ba55f675c156abf4180a130ff6c7` |
| `test/test_firmware_ui_chrome.cpp` | `544875a89e6683d2cb50c33591f828d189925376369e60acc9d31f06b098641f` |
| `test/test_firmware_ui_status.cpp` | `4605ac3df22c451b1b639a7a970e90fb71b3370b92277796542b7f02f611f6d8` |
| `test/test_firmware_ui_team.cpp` | `a97383e0df80825a0b8f39e047c6a261e8357f2ab714fad419c63fb586c296c8` |
| `test/test_firmware_ui_send.cpp` | `66527ed7c3d858fb8767e522a3fb19da86f225ce81e5c59ce2368ba68adba0dd` |
| `tools/probe_firmware_ui/run.sh` | `60478a2a541e6a6959f69312f096a5c011ae3ede99b96dda29ff8d0b1d7b5bdc` |
| `tools/probe_firmware_ui/probe_main.cpp` | `fb45d28e69b0e7ae280c77d91840df659ade5d9f4784837f6317d84793aff9d3` |
| `tools/test_probe_firmware_ui.py` | `50e517ed19079893d3b39fd0b795b581e528a68d5b494daabd901b132f122b16` |
| `tools/probe_ui_model_mutations.py` | `71c860f9a1fd9a4807fa4789da58545f9fbd4c1d90befee5545ece418d15687e` |
| `tools/probe_board_abi.py` | `4a5c0997fff20fb3c04cfd75c2527fd09f0b55edfb9b3f273c3afc99f0b2a579` |
| `tools/test_probe_board_abi.py` | `c6630098421aed5fc2ad54ea29f512e624ed6ba1f9d19b188a0b1632707951c3` |

**Authorized brief:** revision 3 `a89c075e6dab5184d68c06a31b9fd262c3bab5656bc140146a879e2608d8ca5a` (unchanged on disk).

**Simulator:** `6585649ea5a780f0542b2931853a667be56a5b2b`, clean (`git status` empty), 285 inventoried paths unchanged; read-only throughout.

**Preparation set, unchanged** (the revision-3 pins; `tracker.md`, `MEMORY.md` and the pre-check at their existing pins):

| File | SHA-256 |
| --- | --- |
| `docs/superpowers/plans/2026-09-27-standalone-mobile-home-w4b-home-navigation.md` | `a89c075e6dab5184d68c06a31b9fd262c3bab5656bc140146a879e2608d8ca5a` |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | `80f7078419c9d70a2f3f03b52375f355c10e285fa397530a0c98210cba3ccae9` |
| `docs/2026-07-30-open-bug-register.md` | `1f54f9c3cc3bb9fc83b882bb77e2beb423a3e999b61fb16d82dfd452afdbbc0f` |
| `tracker.md` | `bd1566b362047afe58be0668f74c1a2a0fedb1adf2a4b96aeb486e0ff3d399f2` |
| `MEMORY.md` | `fb3826dc9254190fe66f15fa9edae81f646595601a8ac90c14bdf82d464d1092` |
| `docs/superpowers/evidence/2026-09-27-standalone-mobile-home-w4b-precheck.md` | `38b699335991cf04ffc21c7eb9702f36f3f8a3ba640ad5ff0d9b5a51047294aa` |

**Inventory check** (re-run after the last step against the re-pin receipt's `inputs.json`, `recon/inventory_check2.py`): MeshRoute `HEAD c8e36d8` OK — 1621 inventoried paths, 0 missing, **5 changed**: the four fenced files the revision-3 delta edits (`src/firmware_ui_model.h`, `test/test_firmware_ui_model.cpp`, `tools/probe_firmware_ui/probe_main.cpp`, `tools/probe_ui_model_mutations.py`) and this package's `disposition-ledger.md`; **new** = the re-pin receipt and its folder (8) and this package's evidence; simulator 285/285, 0 changed. Nothing staged in either repository.

**New evidence:** this report and [`2026-09-27-standalone-mobile-home-w4b/`](2026-09-27-standalone-mobile-home-w4b/), every file covered by its `SHA256SUMS`; raw logs hashed in `receipts/raw-logs.sha256`.

**Stability:** the fenced files were frozen before chain step 1 and are byte-identical after step 9; no input changed during the chain (the only files written during it are ignored raw logs, `.pio-measure/w4b/final-*`, and — after the board pair — this evidence).

## 9. Not run, with reasons

- `tools/probe_features/run.sh`'s nine-cell MR_FEAT_* matrix: the W4b diff adds or edits **0** capability-macro or
  preprocessor lines, so the matrix predicate is unaffected; its file scanner (`ownership.py`, with controls) was run.
- Every other probe or battery outside §4.1: no reader of the edited statements or changed symbols (reader audit).
- Nothing in §4.1 was skipped. Stage-A, baseline and pre-STOP runs are historical and substitute for nothing above.
