<!-- Author: Claude (Opus 5.5), W4a coder — implementation receipt for QA's independent gate; the owner rules and commits -->
# Standalone Home W4a — identity labels (one formatter, per-site budgets, the `»` marker) — coder receipt

**Status: IMPLEMENTED, CODER GATE (§4.1, steps 1–9) GREEN, READY FOR INDEPENDENT QA (§4.2).** Nothing is committed,
staged, reset or cleaned. The simulator was only read. No STOP condition fired, and I found no semantic disagreement
with the brief. Three rulings are listed at the end; none changes the contract.

- **Contract:** [W4a brief](../plans/2026-09-26-standalone-mobile-home-w4a-identity-labels.md), SHA-256
  `7fafe9ef08aa50842a30240b08fa079cf8ce67c608f46d8ebe390ed879fff067`. I verified it first; it is unchanged at the end.
- **Authorization:** [QA brief review](2026-09-26-standalone-mobile-home-w4a-brief-review.md) (`73b20bed…`; its folder's
  `SHA256SUMS` `df086000…`, 12/12 OK).
- **Evidence:** [`2026-09-26-standalone-mobile-home-w4a/`](2026-09-26-standalone-mobile-home-w4a/), with a `SHA256SUMS`
  over every file. Raw logs stay in the ignored `artifacts/2026-09-26-standalone-mobile-home-w4a/` (brief §5, evidence
  README); [`raw-logs.txt`](2026-09-26-standalone-mobile-home-w4a/raw-logs.txt) names each one with its SHA-256. Board
  runs stay under `.pio-measure/w4a/`, and the stock per-env roots under `.pio-measure/env/` were not moved. Simulator,
  layout and mutation scratch builds stayed outside both repositories.

## 1. Preflight

| Check | Found |
| --- | --- |
| MeshRoute | `HEAD` `8360802904f7bd0023279d3da844453d61207ede`, `main`, 0 staged, plus the uncommitted W1c and W3 candidates |
| Simulator | `6585649ea5a780f0542b2931853a667be56a5b2b`; `git status` empty at preflight and at the end |
| Brief | `7fafe9ef…f067`, equal to the dispatch hash |
| Executable inputs 10/10 | Each disk hash and line count equals the brief's §1: `firmware_ui.cpp` `6fe8c435…` 2785, model `2a452940…` 5777, invite `fc1c7257…` 746, team `aefa86f4…` 280, native model test `42dc27ff…` 10229, team test `b14780f6…` 592, invite test `2a2f7c6f…` 1314, `probe_main.cpp` `d0cf6502…` 7283, `run.sh` `f754b745…` 1753, harness `9429254f…` 12450 |
| Preparation set 6/6 | design, register, pre-check, pre-check `SHA256SUMS`, `tracker.md`, `MEMORY.md`: every hash OK |
| Pre-check folder | `sha256sum -c`: **30/30 OK** |
| Explained addition | The review receipt `73b20bed…` and its folder (`SHA256SUMS` `df086000…`, **12/12 OK**) |
| Inventory (`inputs.json`, 1,455 paths) | 0 missing; exactly the 4 permitted preparation documents changed; 47 new paths = the review (14), the pre-check (32) and the brief (1). The simulator's 285 paths are unchanged. |

Receipts: [`preflight.txt`](2026-09-26-standalone-mobile-home-w4a/preflight.txt),
[`stability-start.txt`](2026-09-26-standalone-mobile-home-w4a/stability-start.txt).

**Source validation (P4).** Every §1 fact held by symbol before the first edit:
- `label_from_hash` asked `peer_name_find` for `cap − 1` bytes, terminated at the count, and fell back to lowercase
  `0x%08lx` with no sanitizing;
- `label_for_team_id` and `label_for_origin` kept their `id %u` fallbacks;
- TEAM filled `TeamRow::label[15]` through `label_for_team_id`; the row printed `%-6.6s`; `ui_team_rows_equal`
  compared the six visible bytes;
- the compose header, DELIVERED and REPLY each used a `[kLabelCap + 1]` local;
- the invite publication copied `peer_name_find(…, cap − 1)` raw plus a NUL, guarded by `if (hash != 0u)`;
- `ui_fmt_invite_row` printed `m.name` at `%-6.6s`, and the confirmation kept the full `0x%08lX` hash.

**One caller outside the §2.6 ledger:** `test_firmware_ui_model.cpp` (~7899, a W3 pager case) calls
`ui_fmt_invite_row` and pins `>Wolfga T200 BBCCDD`. Pre-check Q5 marks it *Preserve*. I pass the carrier verbatim as
the prepared name, so the row's own `%-6.6s` bound keeps the literal unchanged (Ruling 1).

## 2. Baselines (unmodified tree, before stage A)

- **Supplemental layout** (the pre-check's base recipe, current headers, `OutcomeView` extracted once from
  `firmware_ui.cpp`, generated TU outside the checkout, the stock ABI module's `measure` with each target's flags):

  | ABI | `UiState` | `UiSnapshot` | `UiModel` | `TeamRow` | `InviteMember` | `InviteIdRows` | `OutcomeView` |
  | --- | --- | --- | --- | --- | --- | --- | --- |
  | native | 504/8 | 1336/8 | 928/8 | 40/4 | 20/4 | 26/1 | 52/4 |
  | `heltec_mobile` | 504/8 | 1336/8 | 912/8 | 40/4 | 20/4 | 26/1 | 52/4 |
  | `gateway` | 504/8 | 1336/8 | 912/8 | 40/4 | 20/4 | 26/1 | 52/4 |

  Equal to the pre-check's base measurement. Driver, generated TU hash and input hashes:
  [`layout-baseline.json`](2026-09-26-standalone-mobile-home-w4a/layout-baseline.json).
- **Firmware-UI probe, default:** l2 **518/518**, v3 **964/964**, BLE **518/518**; **231 controls verified /
  0 unusable**; B227 231/231; coverage 787/936. O6 RED (1 check), O8 RED (2 checks).
- **Board-UI `--no-neg`:** exit 1 with exactly **W49, W51, W54** failing (B418, W2's).
- **Boards:** `pair --jobs=1` into `.pio-measure/w4a/base-1`, then `base-2`, back to back with nothing between them.

  | env | RAM | flash | objects | payload SHA-256 |
  | --- | --- | --- | --- | --- |
  | `gateway` | 203,740 | 571,936 | 285 | `97deca60…` |
  | `heltec_mobile` | 211,724 | 1,394,588 | 329 | `70c33b3f…` |

  Stock `compare base-1 base-2`: **PASS `gateway`, PASS `heltec_mobile`**.

## 3. Stage A — the instrument repairs, against the unchanged product

Stage A ran on the unmodified product source (`firmware_ui.cpp` `6fe8c435…`, model `2a452940…`, invite `fc1c7257…`,
team `aefa86f4…`), with only `run.sh` edited.

- **O8 (B449) repaired.** Each of its two substitutions now has its own `once` guard. The O8a anchor occurs exactly once
  and its script changes exactly one line. The O8b regex had been escaped one level too deep and matched nothing; it
  is now `.\\0.`, and its guard proves one match and one changed line.
  - Before the repair, the first substitution alone ran. The row lost its real name, and the control failed P23d
    rule 3 and P23e. That is the "blanked real name" the brief says is not O8's proof.
  - Measured after the repair: the nameless candidate row is **`>0x00be T221 BEDEAD`** (19 bytes:
    `3e 30 78 30 30 62 65 20 54 32 32 31 20 42 45 44 45 41 44`). The control fails **exactly two checks**:
    - `P23b ★ rule 2 — a BLANK name column and a POPULATED member fingerprint, at 19 columns`;
    - `P23d precondition: the candidate's row is on the panel with a BLANK name`.
- **O6 (B455) retired.** I removed the call and left a comment naming B455 and O20 as the coverage. Measured before
  retirement: O6 failed **only** `P23d the request confirmation carries the FULL hash` (the lowercase spelling).
  O20, every full-hash check and O5/O9's two-site topology are untouched.
- **The run** (stock `run.sh`, all arms, controls): l2 **518/518**, v3 **964/964**, BLE **518/518**; **230 controls
  verified / 0 unusable** (231 − O6); B227 230/230; coverage 787/936 (unchanged); O8 RED (2); product sources md5
  `ac070e2b…`, unchanged. The only control difference from the baseline is O6's absence.
- **Frozen:** `run.sh` `79d68a22acd6d386657901ff345df1d40b6ac1af7d0dc13d7cd44844374964f9`, `probe_main.cpp`
  `d0cf6502737b3d12f60bda7a54cc04c2fe9b82896204a4369472a4019d89673a` (unchanged). Copies are kept under the ignored
  artifacts folder (`stageA-snapshot/`).

Receipts: [`stage-a.txt`](2026-09-26-standalone-mobile-home-w4a/stage-a.txt) and
[`o8-rows.txt`](2026-09-26-standalone-mobile-home-w4a/o8-rows.txt).

**How the row bytes were captured.** The stock probe prints only check names, never row bytes. Each row was therefore
captured in a scratch build outside the checkout: the frozen probe plus one or two `printf` lines, linked against the
exact mutant the control builds. The stage-A capture uses the base product sources, which I rebuilt by inverting my
scripted stage B edits. Each rebuilt file equals its preflight hash. The scratch run fails the same two checks the
stock control does. The gate's verdicts are always the stock `run.sh`'s.

## 4. Stage B and the diff

**Production** (brief §2.1–§2.4):

- `src/firmware_ui_model.h`: `kIdentityMarker` (`char(0xBB)`), `enum class IdentityFmt { name, hash, none, no_fit }`
  and `ui_fmt_identity(out, cap, bytes, len, key_hash32, cols)` sit right after `ui_display_byte`. The function has
  one sanitizing loop. The marker is appended after sanitizing, so it stays `0xBB`. Member tokens come from the
  existing invite helpers (`ui_fmt_member_hash_full` at 10+ cells, `ui_fmt_member_fingerprint` at 6–9).
  - `cap == 0` writes nothing.
  - `cap < cols + 1` is `no_fit` with an empty result.
  - A name at `cols == 0` is `no_fit`, and no name byte is read.
  - An unnamed peer under 6 cells is `no_fit`, never a clipped hash.
  - No name and no hash is `none`, with an empty result.
  - Comments on the carriers (`kLabelCap`, `TeamRow::label`, the invite section, `_team_sel`, `_reply_who`) now
    describe formatted labels.
- `src/firmware_ui_invite.h`: `ui_fmt_invite_row(out, cap, marker, m, name6)` places a prepared name at its own
  `%-6.6s`. A null `name6` gives a blank column. `kInviteRowNameCols = 6` is added, and the carrier comments are
  updated.
- `src/firmware_ui_team.h`: comments only. The `ui_team_row` precision and `ui_team_rows_equal` statements are
  byte-identical, and so are the `uiteam` anchors.
- `src/firmware_ui.cpp`:
  - **Adapter.** `label_from_hash(hash, out, cap, cols)` reads the full counted name into a local
    `raw[peer_name_max]` (no pre-clip) and makes one formatter call. `label_for_team_id` and `label_for_origin` pass
    `cols` through and keep their `id <n>` fallbacks.
  - **Budgets**, named and asserted:
    - `kTeamNameCols` = `kInviteRowNameCols` = 6;
    - `kComposeToCols` = 19 − `"to: "` = 15;
    - `kDeliveredCols` = 19;
    - `kReplyWhoCols` = 14;
    - `kInviteNameCols` = 14 (the carrier);
    - `kInviteCandCols` = 6.
    - Every budget is ≥ 6 and fits its carrier, and the candidate budget is narrower than the carrier.
  - **The sites:**
    - TEAM at 6;
    - the compose header at 15;
    - DELIVERED at 19;
    - REPLY at 14;
    - the invite publication formats the full raw name at 14 only when the lookup returned a name (the
      `if (hash != 0u)` guard is kept);
    - the candidate row projects the 14-cell carrier to 6 cells.

  No resident member or array changed. The locals are listed in §5.8.

**Tests and instruments:** the eight §2.6 ledger rows; the §2.6 new cases (ten `w4a-ident` native cases, one TEAM
repaint edge, two invite cases, probe P30); `run.sh`'s re-anchors, the O8 re-aim and six new controls; and the
`w4aident` battery with the PIN.

**`git diff --stat` for the fenced files** (against `HEAD`, so it includes the uncommitted W1c/W3 work already in the
model header, the native model test, `probe_main.cpp`, `run.sh` and the harness):

```
 src/firmware_ui.cpp                    | 107 +++++--
 src/firmware_ui_invite.h               |  46 +--
 src/firmware_ui_model.h                | 184 +++++++++---
 src/firmware_ui_team.h                 |  13 +-
 test/test_firmware_ui_invite.cpp       |  90 ++++--
 test/test_firmware_ui_model.cpp        | 518 ++++++++++++++++++++++++++++++++-
 test/test_firmware_ui_team.cpp         |  93 +++++-
 tools/probe_firmware_ui/probe_main.cpp | 511 +++++++++++++++++++++++++++++---
 tools/probe_firmware_ui/run.sh         | 174 +++++++++--
 tools/probe_ui_model_mutations.py      | 164 +++++++++--
 10 files changed, 1668 insertions(+), 232 deletions(-)
```

**W4a's own diff**, against verified copies of the preflight inputs (all ten equal their preflight hashes;
[`base-copies.sha256`](2026-09-26-standalone-mobile-home-w4a/base-copies.sha256)):

```
src/firmware_ui.cpp                      +78 -29
src/firmware_ui_invite.h                 +26 -20
src/firmware_ui_model.h                  +54 -9
src/firmware_ui_team.h                   +9 -4
test/test_firmware_ui_invite.cpp         +71 -19
test/test_firmware_ui_model.cpp          +206 -1
test/test_firmware_ui_team.cpp           +78 -15
tools/probe_firmware_ui/probe_main.cpp   +234 -46
tools/probe_firmware_ui/run.sh           +117 -34
tools/probe_ui_model_mutations.py        +51 -8
```

Every W1c/W3 line in those files is still present; nothing was reverted. Every W1c/W3 file outside the fence is
byte-identical (§8, stability).

**The classified stage A → final probe diff** is in
[`probe-diff-classified.md`](2026-09-26-standalone-mobile-home-w4a/probe-diff-classified.md). Every hunk falls into
one of these classes:
- a §2.6 ledger row;
- a signature adaptation that keeps its expectation;
- a strengthened absence check;
- a comment or label that follows its expectation;
- the new P30 phase;
- a `run.sh` re-anchor, re-aim or new control, each guarded;
- the `once()` relocation (§5.5).

The raw unified diffs are in the folder too ([`stageA-to-final-probe_main.diff`](2026-09-26-standalone-mobile-home-w4a/stageA-to-final-probe_main.diff), [`stageA-to-final-run.sh.diff`](2026-09-26-standalone-mobile-home-w4a/stageA-to-final-run.sh.diff), and stage A's own [`base-to-stageA-run.sh.diff`](2026-09-26-standalone-mobile-home-w4a/base-to-stageA-run.sh.diff)).

## 5. Figures — the §4.1 chain on the final tree

### 5.1 Hygiene (step 1)

`git diff --check` exits 0. The stage A → final probe diff is classified in
[`probe-diff-classified.md`](2026-09-26-standalone-mobile-home-w4a/probe-diff-classified.md) (§4 above).

### 5.2 Native (step 2)

A fresh `pio test -e native`, then the binary `./.pio/build/native/program`: **2986 test cases / 197299 assertions /
0 failed / 0 skipped**. That is +13 cases and +1188 assertions over the W3 pin 2973/196111:
- 10 `w4a-ident` formatter cases;
- 1 TEAM repaint edge;
- 2 invite cases: the row's own name bound, and long or high-byte confirmation names.

Receipt: [`native.txt`](2026-09-26-standalone-mobile-home-w4a/native.txt).

### 5.3 Corpus (step 3)

- **Simulator build:** a fresh stock `lus`, from `cmake -S ../lora-universal-simulator -B <scratch> -DCMAKE_BUILD_TYPE=Release
  -DMESHROUTE_DIR=<MeshRoute>` and `--target lus` (`lus` sha256 `e304147d…`).
- **Corpus run:** `tools/run_corpus.py --require-anchors` gives **36/36 produced and validated, 36/36 anchors reproduce
  `simulation/BASELINE.md`**.
- **Comparison:** field by field against the pre-check's `corpus-manifest.json`, **36 streams identical on all 14 fields,
  0 deltas**.

Receipts: [`corpus-manifest.json`](2026-09-26-standalone-mobile-home-w4a/corpus-manifest.json) and
[`corpus-comparison.txt`](2026-09-26-standalone-mobile-home-w4a/corpus-comparison.txt).

### 5.4 ABI (step 4) — two instruments, reported separately

- **Stock probe** (`tools/probe_board_abi.py`, full, controls on): **PASS, 290 checks, 9/9 controls RED, 0 unusable**.
  Every pinned size is unchanged, including the §1 types the probe pins: `Node`, `UiState` 504,
  `UiSnapshot` 1336, `UiModel` 928 (host) / 912 (boards) and `InviteMember` 20. `TeamRow`, `InviteIdRows` and
  `OutcomeView` are not stock pins, which is why the brief adds the supplemental measurement below.
- **Supplemental three-type layout** (the pre-check's base recipe; `OutcomeView` extracted once from the final
  `firmware_ui.cpp`; the generated TU outside the checkout; the stock module's `measure` with each target's flags):
  **identical to the baseline and the pre-check on all three ABIs**. The `OutcomeView` declaration hash
  `4d59c967…` is unchanged.

  | ABI | `TeamRow` | `InviteIdRows` | `OutcomeView` | (`InviteMember`, `UiSnapshot`, `UiModel`, `UiState`) |
  | --- | --- | --- | --- | --- |
  | native | 40/4 | 26/1 | 52/4 | 20/4, 1336/8, 928/8, 504/8 |
  | `heltec_mobile` | 40/4 | 26/1 | 52/4 | 20/4, 1336/8, 912/8, 504/8 |
  | `gateway` | 40/4 | 26/1 | 52/4 | 20/4, 1336/8, 912/8, 504/8 |

Receipts: [`abi.txt`](2026-09-26-standalone-mobile-home-w4a/abi.txt),
[`layout-baseline.json`](2026-09-26-standalone-mobile-home-w4a/layout-baseline.json),
[`layout-final.json`](2026-09-26-standalone-mobile-home-w4a/layout-final.json) and
[`w4a_layout.py`](2026-09-26-standalone-mobile-home-w4a/w4a_layout.py). The two JSON files carry the generated TU's
SHA-256 and the input hashes.

### 5.5 Probes (step 5)

- **Firmware-UI, default, all arms: PASS.**

  | | baseline | stage A | final |
  | --- | --- | --- | --- |
  | l2 | 518/518 | 518/518 | **529/529** |
  | v3 | 964/964 | 964/964 | **980/980** |
  | BLE | 518/518 | 518/518 | **529/529** |
  | controls verified / unusable | 231 / 0 | 230 / 0 | **236 / 0** |
  | B227 labels / call sites | 231 / 231 | 230 / 230 | **236 / 236** |
  | coverage | 787/936 | 787/936 | **804/952** |

  Count arithmetic: 231 − O6 (retired, stage A) + 6 new controls (W4a-S1…S6) = 236. No existing control's RED count fell.
  - 196 controls are identical on all three runs.
  - 34 rose, because the new P30 checks and the richer B241 re-anchors give them more to redden.

  Per-control detail is in [`control-compare.txt`](2026-09-26-standalone-mobile-home-w4a/control-compare.txt).

  An outside cross-check of the log (not an instrument change) confirmed it: 236 call sites = 236 verified +
  0 unusable, 0 `command not found`, 0 shell errors.
- **Firmware-UI `--no-neg`:** diagnostic only (B350): 529 / 980 / 529, PASS.
- **Board-UI supplemental `--no-neg`:** exit 1 with the failure set **identical** to the baseline: W49, W51, W54 (B418,
  W2's). Every summary line is byte-identical to the baseline too: structural 23/23, wiring 56/59 with 171 controls
  RED, V3 124/124, V4 110/110, traits 14/14, missing 12/12.

Receipt: [`probes.txt`](2026-09-26-standalone-mobile-home-w4a/probes.txt).

**⚠ The first chain run was invalid, and was stopped and rerun from step 1.** The first run printed PASS with
`controls: 229 verified / 0 unusable` against 236 `ctl` call sites. The log carried `once: command not found` seven
times, for C120, B241a, B241b and W4a-S1…S4.
- **Cause.** W3 had defined `once()` inside its own control block, below the guards I added in stage B. A bash
  function exists only once its definition has run, so each of those guards exited 127, and its `&&` skipped the
  control with no FAIL line.
- **How my own check missed it.** My scratch control check ran each mutant through my own harness, which defines
  `once` separately, so it never exercised the stock file's ordering.
- **Fix** (in `run.sh`, inside the fence). `once()` and its B449 paragraph moved to top level, beside `ctl()`; W3's
  block keeps a one-line pointer. The FAIL text now says "a control guard" instead of "a W3 control guard"; nothing
  reads that text.
- **The rerun.** The first run's steps 1–4b had been green, but I stopped it at step 6 and reran the whole chain from
  step 1, because an input had changed mid-chain. Its logs are kept under `artifacts/…/invalid-chain-1/`.
- **The gap behind it** is an observation for QA (below).

### 5.6 Discovery (step 6)

`python3 -m unittest discover -s tools -p "test_*.py"`: **Ran 356 tests, OK, 0 skipped**
([`discovery.txt`](2026-09-26-standalone-mobile-home-w4a/discovery.txt)).

### 5.7 Warning census (step 7)

`tools/warning_census.sh`: **PASS**. All six pinned OLED envs match their baseline: warnings
171/175/175/175/179/179 (pins unchanged), **`-Wswitch` 0**, no new warning
([`census.txt`](2026-09-26-standalone-mobile-home-w4a/census.txt)).

### 5.8 Boards, final (step 8)

`pair --jobs=1` into `.pio-measure/w4a/final-1`, then `final-2`. They ran back to back, alone, after the chain, with
nothing between or during them.

| env | RAM | flash | objects | payload SHA-256 | `compare final-1 final-2` |
| --- | --- | --- | --- | --- | --- |
| `gateway` | 203,740 | 571,936 | 285 | `97deca60…` | **PASS** |
| `heltec_mobile` | 211,724 | 1,394,840 | 329 | `820b8bd3…` | **PASS** |

- **Compatibility:** `toolchain.*`, `fixed_identity.*`, `paths.*`, `concurrency.*`, `host.*` and schema are identical
  (48 fields per env). Only `source.*` and the normal-pio metadata hash differ, as expected.
- **Prediction `gateway` identical: holds.** Every measurement, the symbols hash and the payload `97deca60…` are equal.
- **Prediction `heltec_mobile` RAM unchanged: holds** (211,724).
- **Flash** +252 = `.flash.text` +268 + `.flash.rodata` −16, attributed to the byte in
  [`attribution.txt`](2026-09-26-standalone-mobile-home-w4a/attribution.txt):
  - `ui_fmt_identity` (+149, new);
  - `label_from_hash` (50) folded into `label_for_team_id` (61 → 111, now taking `cols`);
  - `mr_ui_tick` +68, `mr_ui_on_push` +37 and `draw_provision_screen` +28 (the §2.3 sites);
  - `kComposeToPrefix` (+5 rodata);
  - the strings: −`0x%08lx`, −`to: %s`, +`to: `;
  - six weak/local functions whose source is unchanged moved by −1 to −4 bytes. **Measured, not assumed:** a scratch
    compile of `firmware_ui.cpp.o` from the base and the final sources shows the same shifts in the object. The
    translation unit grew, which changed code generation for them; Xtensa link relaxation accounts for the rest.
- **Stack locals added:**
  - `raw[32]` per label lookup (every TEAM, compose, DELIVERED and REPLY label);
  - `raw[32]` per member in the invite publication loop;
  - compose `label` 15 → 16 and DELIVERED `label` 15 → 20;
  - `name6[7]` in the candidate row;
  - REPLY's `who[15]` is unchanged.

Receipts: the eight manifests under `manifests/`, [`compare-base-*.txt`, `compare-final-*.txt`](2026-09-26-standalone-mobile-home-w4a/),
[`attribution-fields.txt`](2026-09-26-standalone-mobile-home-w4a/attribution-fields.txt) (every field, listed) and
[`object-symbols.txt`](2026-09-26-standalone-mobile-home-w4a/object-symbols.txt).

### 5.9 Mutation union (step 9)

`python3 tools/probe_ui_model_mutations.py --target=<name>` ran each battery alone, after the last board pair.

| battery | configured | RED | unusable | clean baseline (cases/asserts/failed) | real tree untouched |
| --- | ---: | ---: | ---: | --- | --- |
| `model` | 239 | **239** | 0 | 2986/197299/0 (8 worker tree(s) agree) | yes |
| `sliceCbudget` | 1 | **1** | 0 | 2986/197299/0 (1 worker tree(s) agree) | yes |
| `uiteam` | 20 | **20** | 0 | 2986/197299/0 (8 worker tree(s) agree) | yes |
| `uiinvite` | 32 | **32** | 0 | 2986/197299/0 (8 worker tree(s) agree) | yes |
| `w4aident` | 9 | **9** | 0 | 2986/197299/0 (8 worker tree(s) agree) | yes |
| `uisend` | 15 | **15** | 0 | 2986/197299/0 (8 worker tree(s) agree) | yes |
| `sliceCsend` | 1 | **1** | 0 | 2986/197299/0 (1 worker tree(s) agree) | yes |
| `chrome` | 44 | **44** | 0 | 2986/197299/0 (8 worker tree(s) agree) | yes |
| `uinearbyrow` | 9 | **9** | 0 | 2986/197299/0 (8 worker tree(s) agree) | yes |
| `uigeo` | 18 | **18** | 0 | 2986/197299/0 (8 worker tree(s) agree) | yes |
| **union** | **388** | **388** | **0** | every worker 2986/197299/0 | yes |

- **Selector (a):** `model` 239, `sliceCbudget` 1, `uiteam` 20, `uiinvite` 32 and `w4aident` 9, all RED.
- **Selector (b):** `uisend` 15, `sliceCsend` 1, `chrome` 44, `uinearbyrow` 9 and `uigeo` 18, all RED.
- **Floor:** 379 + 9 = 388. 0 vacuous, and every anchor matches exactly once (AST census, [`anchor-census.txt`](2026-09-26-standalone-mobile-home-w4a/anchor-census.txt)).
- **Re-anchored:** I07 13, I08 5, I09 20.
- **Kept `uiteam` anchors:** T01 6, T02 41, T03 22, T09 3.
- **New `w4aident` F01–F09:** 34/168/31/4/3/24/36/6/13.

Receipts: [`union.txt`](2026-09-26-standalone-mobile-home-w4a/union.txt) and [`reanchor-table.md`](2026-09-26-standalone-mobile-home-w4a/reanchor-table.md).

### 5.10 Reader audit (D6/P7) and input stability

- **Reader audit** ([`reader-audit.txt`](2026-09-26-standalone-mobile-home-w4a/reader-audit.txt)): every reader of
  `label_from_hash`, `label_for_team_id`, `label_for_origin`, `ui_fmt_invite_row`, `ui_fmt_identity`/`IdentityFmt`/
  `kIdentityMarker`/`kInviteRowNameCols`, `kLabelCap`, `kInviteNameCap` and `kTeamLabelCols` across `lib/`, `src/`,
  `test/` and `tools/` is inside the fence, except three comments that remain accurate:
  - `test/test_node_hashlocate.cpp:1981`: `label_from_hash` is still the UI's one terminating C-string adapter;
  - `src/firmware_ui_geo.h:178` and `src/firmware_ui_nearby_row.h:64`: `label_for_team_id` is still the name step.
- **Scanners.** Nothing in `tools/probe_board_ui` anchors on an edited statement (0 hits for every edited pattern).
  Its structural checks read `firmware_ui.cpp` function definitions, which are unchanged; the run's failure set is
  identical to the baseline.
- **Scanner inputs.** The console-sink scanner reads its generator's `SCAN_FILES`. The feature-ownership and
  data-type-literal scanners walk `lib/src/test`. All three reran in the chain and passed:
  - ownership 25 ok / 0 FAIL, 218 files hashed before and after;
  - literals PASS, self-test 4/4 RED;
  - build identity 27 checks, 12/12 controls RED.

  Receipt: [`audits.txt`](2026-09-26-standalone-mobile-home-w4a/audits.txt).
- **No formatted label reaches a console, JSON, companion or BLE path.** No such file references the formatter or the
  adapters.
- **Input stability:** inputs were recorded at preflight, before stage A
  ([`stability-start.txt`](2026-09-26-standalone-mobile-home-w4a/stability-start.txt)), and after the last step
  ([`stability-end.txt`](2026-09-26-standalone-mobile-home-w4a/stability-end.txt)). They differ only by the ten
  fenced files and the new evidence. The one mid-chain input change (the `once()` move) restarted the chain.

## 6. Controls and mutations

Full table: [`reanchor-table.md`](2026-09-26-standalone-mobile-home-w4a/reanchor-table.md).

- **Retired:** O6 (stage A, B455). O20 covers it, and every full-hash check and O5/O9's topology stay.
- **Re-aimed:** O8 is now the post-change proof. The publication formats even when the lookup returned no name, so the
  row reads `>0x00B» T221 BEDEAD`. It stays 19 columns with the correct fingerprint, and the control fails exactly P23b
  rule 2 and the P23d blank-name precondition ([`o8-rows.txt`](2026-09-26-standalone-mobile-home-w4a/o8-rows.txt)).
- **Re-anchored, meaning kept** (each guarded by `once`):
  - C120 (68 → 77);
  - B241a (10 → 42) and B241b (5 → 40): both still fail P28a's poisoned `H1`/`H2` and rename checks;
  - N4 (8), N9 (8) and O2 (1): the anchors are unchanged; only the replacement gains the budget argument, and O2
    still produces a device label, never the team token;
  - O3 (1 → 2).
- **New:** W4a-S1 silent clip (17), W4a-S2 sanitizer skipped (5), W4a-S3 DELIVERED at 15 (1), W4a-S4 REPLY
  re-sanitized (1), W4a-S5 invite carrier at 6 (4) and W4a-S6 candidate row unprojected (2). These are the four §2.6
  requires plus two for the invite sites.
- **Unchanged:** O20, O5, O9, O18, O22 and C114 (the two `hash != 0` guards stay distinct). C0 still fails to build.
- **Harness:**
  - New battery `w4aident`, F01–F09: the seven §2.7 shapes, plus F08 (the capacity rule) and F09 (name before hash).
  - `uiinvite` I07/I08/I09 re-anchored onto `name6`.
  - `uiteam` T01/T02/T03/T09 anchors unchanged; the synthetic carriers are kept.
  - No entry is retired or weakened. Every existing battery's configured count is unchanged.

**Union by battery:** see §5.9.

## 7. The pin line

PIN re-synced? YES — 2973/196111 (W3) + 10 `w4a-ident` model cases + 1 TEAM repaint edge + 2 invite cases = +13 cases / +1188 assertions = 2986/197299, measured by the full native binary; `PIN_CASES, PIN_ASSERTS = 2986, 197299` (bare literal) with its one derivation line above it (D5).

## 8. Freeze inventory

**Fenced files, final** (the only W4a edits; `git diff --check` clean):

| file | SHA-256 (final) | lines | preflight |
| --- | --- | ---: | --- |
| `src/firmware_ui.cpp` | `bfe5d70e0795effcbeaa7623dc3ee84f4481bc5b04d5728f8167619b92f3530e` | 2834 | `6fe8c435…` |
| `src/firmware_ui_model.h` | `4a499098be6b55cb2c3c02c3cf5462b87489eca34e8a15d30e6b18c2346239c9` | 5822 | `2a452940…` |
| `src/firmware_ui_invite.h` | `f3f61d692752bb24b131390690b2529327c75b47a6a769d720f36749220b9c7c` | 752 | `fc1c7257…` |
| `src/firmware_ui_team.h` | `63f9074be7ba73072816ba3a3d3654853da1a721b8dfeff7db57e854e9c72cdf` | 285 | `aefa86f4…` |
| `test/test_firmware_ui_model.cpp` | `ba92a0f5348144150f6c9ecb6cd5c35dee728396bfae81799fd1be260a62f373` | 10434 | `42dc27ff…` |
| `test/test_firmware_ui_team.cpp` | `1833ed10acc01bd71d7ca7142b3180ff5fb68bff2ac508181033edbd36ece38a` | 655 | `b14780f6…` |
| `test/test_firmware_ui_invite.cpp` | `8b5c32bea178acda225b3651596e0e204a305a915ba1dfbb79e33ce313b39438` | 1366 | `2a2f7c6f…` |
| `tools/probe_firmware_ui/probe_main.cpp` | `3090a371e36295338a02e6c878eb1c32471f33dffd6260e68698d31b560c6905` | 7471 | `d0cf6502…` |
| `tools/probe_firmware_ui/run.sh` | `89e41dd80920ba42e72216d982a0702c3444fcf1a62ec71df44ccddd86252468` | 1836 | `f754b745…` |
| `tools/probe_ui_model_mutations.py` | `b1574c35d23cf0e5bf2f2898be6bf14c8ef453accbd6db51eefe5e71e3e10b4b` | 12493 | `9429254f…` |

- **Brief:** `7fafe9ef08aa50842a30240b08fa079cf8ce67c608f46d8ebe390ed879fff067`, unchanged and equal to the dispatch hash.
- **MeshRoute:** `HEAD` `8360802904f7bd0023279d3da844453d61207ede` on `main`; **0 staged**. Nothing committed, reset, cleaned or discarded.
- **Simulator:** `6585649ea5a780f0542b2931853a667be56a5b2b`, `git status` empty (read only; its 285 inventoried paths are unchanged).
- **Preparation set, unchanged:** design `0b5706de…`, register `92256413…`, pre-check `f7e55b32…`, pre-check `SHA256SUMS` `e4268d8d…` (**30/30 OK**), `tracker.md` `a03b3cbe…`, `MEMORY.md` `271edd08…`. The review receipt `73b20bed…` and its `SHA256SUMS` `df086000…` (**12/12 OK**) are unchanged, and the metal plan is untouched.
- **Inventory** (the pre-check's `inputs.json`, 1,455 paths): 14 changed = exactly the 4 permitted preparation documents + the 10 fenced files. 0 missing. The 47 new paths are exactly the preflight's (review 14, pre-check 32, brief 1), before this receipt's own folder. Unexplained: **0**. Every W1c/W3 file outside the fence is byte-identical to its preflight hash, and every W1c/W3 line inside the fence is kept (the W4a-only diff is against hash-verified copies of the preflight inputs).
- **New evidence:**
  - [`2026-09-26-standalone-mobile-home-w4a/`](2026-09-26-standalone-mobile-home-w4a/): 44 files, all listed in its `SHA256SUMS` (SHA-256 `88dd1139e572a86cb3cafea9b7bb3d3838d4d179f8215e746b328603f19abc38`, `sha256sum -c` 44/44 OK);
  - this report, `docs/superpowers/evidence/2026-09-26-standalone-mobile-home-w4a.md`, whose hash is in my final message.
  - Raw logs stay in the ignored `artifacts/2026-09-26-standalone-mobile-home-w4a/` (named with hashes in `raw-logs.txt`). Board runs stay under `.pio-measure/w4a/{base-1,base-2,final-1,final-2}`.
- **Stability:** the inputs at preflight (`stability-start.txt`) and after the last step (`stability-end.txt`, 21:33 UTC, after the union) differ only by the ten fenced files. The one mid-chain input change (the `once()` move, §5.5) restarted the chain from step 1. Nothing ran during either board pair.

## 9. Not run, with reasons

The reader audit (§5.10) finds no reader of the edited statements or changed signatures outside the fence, except
three accurate comments. None of the following probes has a predicate over `label_from_hash`, the three adapters,
`ui_fmt_invite_row`, the new formatter or the fenced tests and instruments. Their predicates are unaffected:
- **Full board-UI probe (with controls).** B418's W49/W51/W54 are W2's to fix. Only the `--no-neg` supplemental ran,
  and its failure set and every summary line equal the baseline. It has no anchor on any edited statement.
- **Console-sink probe.** Its structural readers read the generator's `SCAN_FILES` (the command and console sources).
  No formatted label reaches a console, JSON or companion path.
- **Inbox-verbs probe.** Its verbs and store are untouched. Its `firmware_ui.cpp` reference is a comment about
  `ui_emergency_active`, which W4a does not touch.
- **Custody USB and the BLE line.** No serial, BLE or command path changed.
- **Feature matrix and deferred actions.** No `MR_FEAT_*` gate, no feature TU and no deferred-action path changed. The
  ownership, literal and build-identity scanners ran and passed (§5.10).
- **Metal.** W4a's on-glass rendering (the `»` glyph in the 6-px font, the abbreviations at each site) is a metal
  observation for QA to place in the metal plan. The glyph is `0xBB` in the Latin-1 panel font; the probe proves the
  bytes, not the pixels.

## Observations for QA (M1 — for QA to register or dispose; nothing is asserted as a new production bug)

1. **The firmware-UI runner cannot see a skipped control.** `run.sh` counts its `ctl` call sites (the B227 audit)
   but never compares them with `n_ctl + n_bad`. A guard that fails *as a command* (exit 127 from an undefined helper,
   or any non-`once` failure before `&&`) skips its control with no FAIL line, and the run still prints PASS. This is
   how the first W4a chain passed with 229 of 236 controls (§5.5). The two counts were equal at the baseline
   (231 = 231) and at stage A (230 = 230), so the check would be one line. I did not add it; it is outside the brief.
2. **An equal-width second pass re-sanitizes a retained marker.** W4a-S5 is also RED on P23d/P23e for this reason, and
   it is exactly §2.3's "strictly narrower" rule, measured. A future site must never re-format a formatted label at
   its own width.
3. **E14 (`sliceEcascade`) has 0 matches.** It is already registered as **B453** (open, from W3 QA), so this is not a
   new finding. Its target `lib/core/node_cascade.cpp` equals `HEAD`, the battery is outside W4a's union, and no W4a
   file is involved.

## Rulings I made (the ledger)

1. **The out-of-ledger caller keeps its literal.** `test_firmware_ui_model.cpp` (~7899, a W3 pager case) pins
   `>Wolfga T200 BBCCDD` through `ui_fmt_invite_row`. The pre-check's Q5 says *Preserve*, so I pass the member carrier
   verbatim as `name6`: the row's own `%-6.6s` bound gives the same 19 bytes, and no expectation changes.
   *Cost if wrong:* one call re-pointed through the projection, plus a ledger amendment.
2. **P30 is additive.** It adds new checks, not an expectation change: the real lookup at every §2.3 site with a
   20-byte name and `C5 82 41 42`, all expected bytes literal. It then restores P28's canonical fixture. Unnamed peers
   stay covered by P18/P28. *Cost if wrong:* none (additive).
3. **`w4aident` carries F08 and F09** beyond the required seven: the capacity rule, and name before hash.
   *Cost if wrong:* two more entries.

Two instrument notes, neither a ruling against the brief:
- The `once()` relocation (§5.5) is a fix to my own stage B error in a fenced file.
- The O8 row bytes were re-captured in a scratch build from hash-verified base sources, because the stock probe does
  not print row bytes (§3).
