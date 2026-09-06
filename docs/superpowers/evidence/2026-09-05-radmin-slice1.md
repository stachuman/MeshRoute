<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 1 — feature-boundary scaffold · coder evidence · 2026-09-05

Brief: `docs/superpowers/plans/2026-09-05-radmin-slice1-feature-boundary-scaffold.md` (QUALITY-AGENT PASS).
Authority: R-RA-8 (design ruling 15), R-RA-17, **R-RA-26** (the `defined(ARDUINO)` discriminator, no
`platformio.ini` change, no consumer). Register rows touched by findings: **B304** (partially), **B305**
(labels used as corrected), plus one NEW finding in §12.

⛔ **UNCOMMITTED (D4).** Every figure below was measured by me on this tree with the command shown beside it.
⛔ **`simulation/BASELINE.md` was NOT edited**; its keystone was READ, never typed from memory.

---

## 1. Base verification (STOP 1)

```
$ git rev-parse HEAD
c1c342b90ae770c169d7a28ac4ec32ef0c216d13
```
Matches the brief's named dispatch base exactly. The starting tree was clean **except** the single tracked file
the brief names as the Author's permitted pin:

```
$ git status --short          # at start of work, 2026-09-05 ~20:55 UTC
 M docs/superpowers/plans/2026-09-05-radmin-slice1-feature-boundary-scaffold.md
```

That file was never touched, staged or reverted by me. ⚠ **Two further edits appeared in the tree DURING the
run and are NOT mine — see §11 (STOP 8, fired).**

## 2. What changed

| path | kind | lines |
| --- | --- | --- |
| `lib/core/mr_features.h` | production (the ONLY production file) | +59 / −4 |
| `tools/probe_features/run.sh` | new instrument — the gate | new |
| `tools/probe_features/probe_main.cpp` | new instrument — the executed driver | new |
| `tools/probe_features/envmap.py` | new instrument — the env→cell derivation | new |
| `tools/probe_features/mutate.py` | new instrument — the single-match control mutator | new |
| `tools/test_probe_features.py` | new wrapper, joins the standing `tools` sweep | new |
| `docs/superpowers/evidence/2026-09-05-radmin-slice1.md` | this file | new |

No other `lib/`, no `src/`, no `test/`, no `platformio.ini`, no variant, no simulator source, no `BASELINE.md`,
no existing instrument, no generated inventory, no companion file. **No consumer of the pair anywhere.**

The production derivation, verbatim from the final header:

```c
#if defined(MR_PROFILE_MOBILE)
#  define MR_FEAT_RADMIN_CLIENT 1
#  define MR_FEAT_RADMIN_ACCEPT 0
#elif defined(ARDUINO)
#  define MR_FEAT_RADMIN_CLIENT 0
#  define MR_FEAT_RADMIN_ACCEPT 1
#else
#  define MR_FEAT_RADMIN_CLIENT 1
#  define MR_FEAT_RADMIN_ACCEPT 1
#endif
```

and the three board-only rules, each a separate `#error` so each can fail — and be deleted — on its own:

```c
#if defined(ARDUINO)
#  if MR_FEAT_RADMIN_CLIENT && MR_FEAT_RADMIN_ACCEPT      -> #error "...both 1..."
#  if !MR_FEAT_RADMIN_CLIENT && !MR_FEAT_RADMIN_ACCEPT    -> #error "...both 0..."
#  if MR_FEAT_RADMIN_ACCEPT != MR_FEAT_REMOTE_MGMT        -> #error "...disagrees with itself..."
#endif
```

## 3. Source-derived environment → cell matrix

Derived by `tools/probe_features/envmap.py`, which resolves `extends` through **PlatformIO's own**
`pio project config --json-output` and reads the simulator's `CMakeLists.txt` — never a hand-rolled ini parser.
14 platformio environments + 2 simulator core variants, all accounted for:

| probe cell | environments mapped to it (derived) |
| --- | --- |
| `board_mobile_oled0` | `xiao_mobile`, `xiao_esp32s3_mobile` |
| `board_mobile_oled1` | `heltec_mobile`, `heltec_v4_mobile` |
| `board_gateway_oled0` | `gateway`, `gateway_esp32s3` |
| `board_gateway_oled1` | `gateway_heltec`, `gateway_heltec_v4` |
| `board_static_oled0` | `xiao_sx1262`, `xiao_esp32s3`, **`production`** |
| `board_static_oled1` | `heltec_v3`, `heltec_v4` |
| `host_native` | `native` (`-DMESHROUTE_NATIVE=1 -DMR_N_LAYERS=2`, no `ARDUINO`, no profile) |
| `host_lus_normal` | simulator `meshroute_core_normal` (no extra defines) |
| `host_lus_gateway` | simulator `meshroute_core_gw` (`MESHROUTE_NS=meshroute_gw MR_N_LAYERS=2 MR_GATEWAY_BUILD=1 MR_CAP_CHANNEL_BUFFER=8 MR_CAP_DEFERRED_SENDS=16`) |

`ARDUINO` is real, not assumed: `.pio/build/gateway/idedata.json` carries `ARDUINO=10804`,
`.pio/build/heltec_mobile/idedata.json` carries `ARDUINO=10812`, and `.pio/build/native/idedata.json` carries
**none**. `grep ARDUINO ../lora-universal-simulator/CMakeLists.txt` returns nothing.

The 13 envmap assertions all pass, including the two that make the per-cell projection honest rather than
convenient:
- **E13** — the only `MR_FEAT_*` any environment sets directly is `MR_FEAT_OLED`, so a cell's define set
  {`ARDUINO`, `MR_PROFILE_*`, `MR_FEAT_OLED`, `MESHROUTE_NATIVE`, `MR_N_LAYERS`, `MR_GATEWAY_BUILD`} is complete.
- **E12** — `platformio.ini` names no `MR_FEAT_RADMIN*` and no `MR_PROFILE_STATIC` (R-RA-26 held).

## 4. All old and new feature values, per row (MEASURED by execution)

Each row is the driver's own `derived:` line, printed by a host-compiled, host-**run** TU that includes the real
`lib/core/mr_features.h`. `RM` = the legacy `MR_FEAT_REMOTE_MGMT`.

| cell | TEAM | MOBILE | MOBILE_HOST | GATEWAY | OLED | RM | **CLIENT** | **ACCEPT** |
| --- | --: | --: | --: | --: | --: | --: | --: | --: |
| `board_mobile_oled0` | 1 | 1 | 1 | 1 | 0 | 0 | **1** | **0** |
| `board_mobile_oled1` | 1 | 1 | 1 | 1 | 1 | 0 | **1** | **0** |
| `board_gateway_oled0` | 0 | 0 | 1 | 1 | 0 | 1 | **0** | **1** |
| `board_gateway_oled1` | 0 | 0 | 1 | 1 | 1 | 1 | **0** | **1** |
| `board_static_oled0` | 1 | 1 | 1 | 1 | 0 | 1 | **0** | **1** |
| `board_static_oled1` | 1 | 1 | 1 | 1 | 1 | 1 | **0** | **1** |
| `host_native` | 1 | 1 | 1 | 1 | 0 | 1 | **1** | **1** |
| `host_lus_normal` | 1 | 1 | 1 | 1 | 0 | 1 | **1** | **1** |
| `host_lus_gateway` | 1 | 1 | 1 | 1 | 0 | 1 | **1** | **1** |

Every value agrees with the brief's Required-result table and with R-RA-26. The six pre-existing flags are
**unchanged on every row**. ★ The `host_lus_gateway` row is the one no profile-only reading of R-RA-17 would
have got right: it sets `MR_GATEWAY_BUILD=1` and `MR_N_LAYERS=2` with **no** `ARDUINO`, and keeps BOTH endpoints.

**Instrument pins measured on the final tree:** `matrix: 9 configuration cells (pin 9), 97 checks, 0 failed
(pin 97) · controls: 19 verified / 0 unusable (pin 19) · PASS`. 97 = 3 runner-structural + 13 envmap +
9 cells × (1 source-integrity pin + 8 asserted values).

## 5. Control ledger — compile-time refusals vs executable mutations

Classification was **printed by the runner before any control ran** (asserted by the wrapper:
`test_the_classification_is_declared_before_any_control_runs`). Every mutation goes into an isolated header copy
via `mutate.py`, which **asserts the occurrence count** — the production derivation deliberately spells
`#  define MR_FEAT_RADMIN_CLIENT 1` twice, so an unverified `sed` would have hit the wrong arm.

### Class A — production compile-time refusal (a compile failure IS the declared RED, here and nowhere else)

The refusal is attributed to a diagnostic string **extracted from the production header at runtime** (check S1),
not typed into the runner. All four also require their **legal sibling cell to still build and run green** under
the same mutant.

| id | mutation (single verified match) | invalid fixture | diagnostic reached | legal sibling still green |
| --- | --- | --- | --- | --- |
| A1 | board arm `CLIENT 0`→`1` ⇒ `{1,1}` | `board_static_oled0` (ACCEPT 1 == RM 1, so only exclusivity can fire) | `both` — *"board build is BOTH remote-admin endpoints…"* | `board_mobile_oled0` ✓ |
| A2 | mobile arm `CLIENT 1`→`0` ⇒ `{0,0}` | `board_mobile_oled0` (ACCEPT 0 == RM 0) | `neither` — *"board build is NEITHER remote-admin endpoint…"* | `board_static_oled0` ✓ |
| A3 | `#if defined(MR_PROFILE_MOBILE)`→`#if defined(ARDUINO)` | `board_static_oled0` ⇒ `{1,0}` vs RM 1 | `consistency` — *"…ACCEPT != MR_FEAT_REMOTE_MGMT…"* | `board_mobile_oled0` ✓ |
| A4 | `#if defined(MR_PROFILE_MOBILE)`→`#if defined(MR_PROFILE_GATEWAY)` | `board_mobile_oled0` ⇒ `{0,1}` vs RM 0 | `consistency` (the **opposite** direction) | `board_static_oled0` ✓ |

A1/A2 keep `ACCEPT == REMOTE_MGMT` satisfied deliberately, so the consistency check **cannot mask** a missing
exclusivity check; A3/A4 keep exclusivity satisfied, so exclusivity cannot mask a missing consistency check.

### Class B — executable matrix mutations (the mutant BUILDS; the driver names the wrong value)

| id | mutation | cell that goes RED | assertion named | collateral cell still green |
| --- | --- | --- | --- | --- |
| B1 | host arm `CLIENT 1`→`0` | `host_native` | `MR_FEAT_RADMIN_CLIENT` | `board_mobile_oled0` |
| B2 | host arm `ACCEPT 1`→`0` | `host_lus_normal` | `MR_FEAT_RADMIN_ACCEPT` | `board_static_oled0` |
| B3 | `#elif defined(ARDUINO)` → `… \|\| defined(MR_GATEWAY_BUILD)` | **`host_lus_gateway`** | `MR_FEAT_RADMIN_CLIENT` | `host_lus_normal` |
| B4 | `#elif defined(ARDUINO)` → `#elif !defined(MESHROUTE_NATIVE)` | `host_lus_normal` | `MR_FEAT_RADMIN_CLIENT` | `host_native` |
| B5 | gateway profile `MR_FEAT_MOBILE 0`→`1` | `board_gateway_oled0` | `MR_FEAT_MOBILE` | `board_static_oled0` |
| B6 | default `MR_FEAT_OLED 0`→`1` | `board_static_oled0` | `MR_FEAT_OLED` | `board_static_oled1` |

B1/B2 are "a host loses either endpoint"; **B3 is the lus-gateway case named by the brief**; B4 is wrong
host/board discrimination; B5/B6 prove the six **pre-existing** flags and both OLED values are live measurements
and not decoration.

### Class C — guard removal and guard scope

Each reuses class A's named invalid fixture as its established baseline and then changes exactly one guard.

| id | guard changed | outcome required and measured |
| --- | --- | --- |
| C1 | the both-enabled `#error` **deleted** | `board_static_oled0` `{1,1}` now **COMPILES** ⇒ that guard, and no other, was refusing it |
| C2 | the neither-enabled `#error` **deleted** | `board_mobile_oled0` `{0,0}` now **COMPILES** |
| C3 | the `ACCEPT == RM` `#error` **deleted** | A3's static-direction fixture now **COMPILES** |
| C4 | the `ACCEPT == RM` `#error` **deleted** | A4's mobile-direction fixture now **COMPILES** |
| C5 | the checks' fence `#if defined(ARDUINO)` → `#if 1` | the **legitimate host** `{1,1}` is now REFUSED at `both` ⇒ a board-only restriction applied to the host is detected |

"A second check must not conceal the removal" is **measured, not argued**: if another guard had still refused the
fixture, `ctl_guard_accepts` reports `FAIL … another check concealed the removal` and names which diagnostic fired.

### Class X — the controls of the controls

| id | what is executed | required verdict |
| --- | --- | --- |
| X1 | `CXX=/bin/false` on a legal cell | classifies `unrelated`, **never** a policy refusal |
| X2 | an unrelated **syntax error** injected into the header copy | classifies `unrelated` |
| X3 | an unrelated **`#error`** injected into the header copy | classifies `unrelated` — the sharpest of the three: it fails the *same way* a refusal does, and only the extracted-text match separates them |
| X4 | the classifier's truth table | `1+diag=refusal · 1+other=unrelated · 0=accepted · 139=abnormal`, and `matched_diag` attributes the right one |

**"Dropping something cannot preserve PASS" is executed**, by the wrapper, through the runner's own sabotage
switch `MR_PROBE_DROP`:

| sabotage | measured result |
| --- | --- |
| `MR_PROBE_DROP=cell` | `matrix: 8 cells (pin 9), 88 checks (pin 97)` → `CELL COUNT MOVED` + `CHECK COUNT MOVED` → **FAIL**, no `PASS` printed |
| `MR_PROBE_DROP=check` | `88 checks (pin 97)`; 3 controls also become unusable → **FAIL** |
| `MR_PROBE_DROP=control` | `controls: 18 verified (pin 19)` → `CONTROL COUNT MOVED` → **FAIL** |
| `CXX=/bin/false` (whole run) | `MATRIX BUILD FAILED`, 4 controls survive of 19 → **FAIL**, no `PASS` |

`--no-neg` prints `PROBE-ONLY — NOT A GATE (controls skipped…)` and **never** an ordinary `PASS`; no child
process prints a standalone gate PASS. Source integrity: `tree unchanged: the real sources' md5 is identical
before and after (a0060377916fe33f882021c850b0e83d)` — asserted by the runner over the header, the driver, both
python helpers and `platformio.ini`; and per-cell by the driver's FNV-1a-64 pin (`hdr_integrity`), computed
independently in bash/python and in C++.

## 6. Native

```
$ pio test -e native                      -> native:* [PASSED]  (the wrapper's usual false "0 test cases")
$ ./.pio/build/native/program
[doctest] test cases:   2610 |   2610 passed | 0 failed | 0 skipped
[doctest] assertions: 110269 | 110269 passed | 0 failed |
[doctest] Status: SUCCESS!
```

**PIN re-synced? YES — unchanged; `tools/probe_ui_model_mutations.py:616` holds `PIN_CASES, PIN_ASSERTS = 2610,
110269` and the binary measured exactly 2610 / 110269 / 0. Slice 1 adds no `test/` TU, no doctest case and no
assertion: the only production change is two macro definitions and three `#if`-guarded diagnostics with no
consumer, so zero movement is the derivation, and it is demonstrated rather than copied from the brief.**

## 7. Simulator relink and the 36-scenario corpus

The changed header is a `lib/core` header, so the simulator **must** rebuild — and it did:

```
$ md5sum .../build/orchestrator/lus     0b018a5e445b6d294848950f4603a5af   (BEFORE)
$ cmake --build /home/staszek/lora-universal-simulator/build
   34 build actions; "Linking CXX static library libmeshroute_core_normal.a",
   "… libmeshroute_core_gw.a", "… libmeshroute_console.a", "Linking CXX executable lus"; exit 0
$ md5sum .../build/orchestrator/lus     0b018a5e445b6d294848950f4603a5af   (AFTER — IDENTICAL)
```

★ The rebuild is what makes the identity a measurement: both core variants and the executable were genuinely
recompiled and relinked through the changed dependency, and the binary still reproduces byte-for-byte, because
the two new macros have no consumer.

```
$ python3 tools/run_corpus.py --jobs=8 --require-anchors --out <scratch> --lus .../lus
PASS: 36/36 streams produced and validated, 0 failures
  anchors: 36/36 rows reproduce simulation/BASELINE.md
  wall_clock=51.3s jobs=8
```

Manifest cross-check over all 36 rows: `anchor_match` **True × 36**, `assertion_failures` **0 × 36**.

**s18, explicitly** (keystone READ from `simulation/BASELINE.md`'s `### 36/36 corpus` block, never typed from
memory): anchor `32afbf11` / 269517 events / 0 assertion failures — measured
`output_md5 = 32afbf11e43b4bf9d0bd470ad502ba0a`, `events = 269517`, `assertion_failures = 0`. **No mover; no
re-anchor is proposed or authorized.**

## 8. ABI

```
$ python3 tools/probe_board_abi.py
  struct                          native        heltec_mobile          gateway
  meshroute::Node             222072/8 T           117912/8 T       148680/8 T
PASS: board ABI (191 checks, 9/9 controls RED, 0 unusable)

$ python3 tools/probe_b278_row_abi.py
PASS: B278 production correlation-row ABI mirror (42 measurements, 6/6 controls RED)
  DelegAck sizeof/alignof 32/8 on gateway, heltec_mobile and native
```

Named targets, per **B305's corrected** labels and independently measured here: **native/HOST 222072 ·
`heltec_mobile`/Xtensa 117912 · `gateway`/ARM 148680**. Node layout and the B278 row are unchanged — expected,
since slice 1 adds no Node state.

## 9. Board A/B — the inertness proof (STOP 4)

`python3 tools/measure_board.py pair --output .pio-measure/s1-{before,after} --jobs=2`, same fixed build identity
(`stamp=Jan 1 2000 00:00:00 git=b206b206b206`) and the same per-env build paths in both arms. BEFORE was captured
on the pristine base tree before any edit.

| env | RAM | ΔRAM | flash | Δflash | objects | sections | symbol count / size / sha256 | ELF sha256 | payload sha256 |
| --- | --: | --: | --: | --: | --: | --- | --- | --- | --- |
| `gateway` (ARM) | 195844 | **0** | 512076 | **0** | 283 | 3/3 identical (`.text` 511092 · `.data` 976 · `.ARM.exidx` 8) | 6159 / 686817 / identical | identical | identical |
| `heltec_mobile` (Xtensa) | 205684 | **0** | 1355300 | **0** | 327 | 8/8 identical (`.flash.text` 1038702 · `.flash.rodata` 212468 · `.iram0.text` 77843 · …) | 13082 / 1454964 / identical | identical | identical |

Field-by-field over the whole manifest: **70 fields (gateway) / 75 (heltec_mobile), of which 4 moved — and
`measurement/artifact fields moved: 0` on both.** The four are exclusively source-snapshot metadata:

| field | before → after | why |
| --- | --- | --- |
| `source.file_count` | 848 → 854 | **+6 = my 5 new instrument files + 1 concurrent doc** (§11) |
| `source.tree_sha256` | `a21562cb…` → `429bb387…` | the tree content changed — that is the slice |
| `source.git_status_sha256` | `f051da06…` → `71f8875f…` | new untracked paths + the concurrent doc edits |
| `normal_pio_metadata.sha256` | `d0184535…` → `f18b75e9…` | PlatformIO's project metadata snapshot |

**B254 / B262 distinction, isolated and reported rather than used as an excuse:** the header edit is
line-count-changing, which B254 records as *not* automatically payload-inert on xtensa. It is here, and that is
measured, not argued: `heltec_mobile`'s `payload_sha256` is **b0c98a3f… in both arms**, and so is its ELF hash —
because `mr_features.h` contains no `__LINE__` use and no string or data that reaches the image. No measurement
field was excused by metadata movement; none needed to be.

`tools/measure_board.py compare` itself **refuses** both pairs with
`ERROR: repeatability mismatch in: source.git_status_sha256, source.file_count, source.tree_sha256,
normal_pio_metadata.sha256` — that is the tool working correctly (it is a *repeatability* comparator and the
source genuinely changed), which is exactly why the A/B above is presented field-by-field.

## 10. Standing instruments, sweep, inventory, warnings, checkers

| gate | result (measured) |
| --- | --- |
| `tools/probe_console_sink/run.sh` | **PASS** — `profiles=6 checks=720 structural=29 ble_guard=212 ownership=6 ownership_controls=3 controls=58 unusable=0` |
| `tools/probe_inbox_verbs/run.sh` | **PASS** — 91 checks (pin 91), 22 controls (pin 22), 0 unusable |
| `tools/probe_firmware_ui/run.sh` | **PASS** — l2 404 · v3 839 checks, 223 controls verified, 0 unusable |
| `tools/probe_custody_usb/run.sh` | **PASS** — 27 checks (pin 27), 10 controls (pin 10) |
| `tools/probe_ble_line/run.sh` | **PASS** — 40 checks (pin 40), 8 controls (pin 8) |
| `python3 -m unittest discover -s tools -p "test_*.py"` | **OK — Ran 305 tests** (279 before + **26** from the new `test_probe_features.py`; the arithmetic closes exactly) |
| `python3 tools/gen_command_inventory.py` | **PASS** — matches fresh generation byte-for-byte, **177 command rows** |
| `python3 tools/gen_command_inventory.py --check` | **PASS**, same figure. ⛔ **`--write` was NOT used** — no firmware command anchor moved |
| `tools/warning_census.sh` | **PASS — 6 OLED envs at their pinned baselines, nothing re-pinned**; `-Wswitch` 0 on all six; `gateway_heltec` 327/173 · `gateway_heltec_v4` 328/178 · `heltec_mobile` 327/177 · `heltec_v3` 327/177 · `heltec_v4` 328/182 · `heltec_v4_mobile` 328/182 |
| `python3 tools/check_a0_matrix.py` | **PASS** — 21 enum members, all matrix rows present |
| `python3 tools/check_data_type_literals.py` | **PASS** — 179 files scanned, no numeric `DataType` literal survives |

The census's `heltec_mobile` RAM/flash (205684 / 1355300) equals the board A/B figure, independently.

## 11. STOP audit — all eight evaluated explicitly

| # | condition | verdict |
| --- | --- | --- |
| 1 | wrong/missing base, or an unexplained starting edit | **NOT FIRED at start.** `HEAD = c1c342b90ae…`; the only starting edit was the Author's brief pin. |
| 2 | the ruled matrix needs another profile / an ini change / a different discriminator / a new owner decision | **NOT FIRED.** `defined(ARDUINO)` alone separates all four cases; no `MR_PROFILE_STATIC`; `platformio.ini` untouched (envmap E12 measures this). |
| 3 | an existing feature value, legacy consumer, command, runtime path, Node state/layout, wire/NV/timer or simulator source must change | **NOT FIRED.** Six old flags identical on all 9 rows; `MR_FEAT_REMOTE_MGMT` and all its owners untouched; `sizeof(Node)` unchanged on all three ABIs; 36/36 corpus byte-identical; command inventory unchanged at 177 rows. |
| 4 | either ruled board's RAM/flash/sections/objects/symbols move | **NOT FIRED.** All zero on both (§9). |
| 5 | a corpus stream changes, or the rebuilt simulator misses the keystone | **NOT FIRED.** 36/36 anchored, s18 `32afbf11`/269517/0. |
| 6 | an ABI pin changes, native PIN moves, the inventory needs regeneration, or an existing instrument needs an out-of-fence edit | **NOT FIRED.** ABI pins hold; PIN unchanged at 2610/110269; inventory `--check` PASS with no `--write`; no existing instrument was edited. |
| 7 | a named refusal control fails for an unrelated reason, or a control is green/vacuous/multi-matched/unusable | **NOT FIRED.** 19/19 controls verified, 0 unusable; every mutation single-match-verified by `mutate.py`; X1–X4 execute the three "not-a-refusal" failure modes. |
| 8 | an unexplained warning, **conflicting concurrent edit**, or unaccounted modified/untracked file | ⚠ **FIRED — reported, not widened.** See below. |

### STOP 8, in full — two files changed under me, mid-run, and neither is mine

The tree was clean at 20:55 apart from the Author's pin. Two further changes appeared **while the gate was
running**, by mtime:

| path | state | mtime (UTC) | what it is |
| --- | --- | --- | --- |
| `docs/superpowers/plans/2026-09-05-radmin-slice1b-precheck.md` | **new, untracked** | 21:00:24 | the **next** slice's Quality-Agent pre-check ledger |
| `docs/2026-07-30-open-bug-register.md` | **modified** | 21:10:05 | a doc-only reflow of the "Heltec/OLED and product backlog" table (36 insertions / 36 deletions, no row content change visible in the diff head) |

For reference: my board BEFORE capture finished 20:58:42, my header edit landed 21:04:42, my board AFTER capture
finished 21:18:07.

**What I did:** nothing. I did not touch, stage, revert or `git add` either path — they are outside my fence and
docs belong to the Author. I am reporting them for the Quality Agent and the owner to rule on.

**What it did to the measurements — stated precisely rather than waved away:** nothing measurable, and that is
demonstrated, not assumed. Both are Markdown documents; no board build, no native TU, no simulator source and no
instrument reads either. The proof is in §9: on both boards **every measurement and artifact field is
identical**, including the two payload hashes; only the three source-snapshot fields and PlatformIO's metadata
hash moved, and the `file_count` delta closes exactly on the arithmetic **848 + 5 (mine) + 1 (the slice-1b
pre-check) = 854**. Native, corpus, ABI, the standing probes, the census and both checkers do not read `docs/`
at all.

⚠ The one honest residue: the board A/B's BEFORE arm was captured before those two files existed. Had they been
capable of affecting a build, the arm would be incomparable. They are not, and the identical measurement fields
are the evidence — but the QA should confirm the two edits are the expected parallel doc work rather than a
second agent operating in this checkout.

## 12. Mutation selectors (both derived from the final tree)

- **Changed-source selector — EMPTY.** `tools/probe_ui_model_mutations.py`'s `TARGET_SRC` holds **76** entries;
  **none** names `lib/core/mr_features.h`. Intersection of {tracked files this slice changed} with
  {battery target files} = **∅**.
- **Historical/dependency selector — EMPTY.** No existing battery owns the endpoint derivation or its checks.
  The one battery target file that so much as mentions the header is `lib/core/node_mac.cpp`, and only in a
  prose comment (`:669`) — it is not a `#include` and it names no `RADMIN` token. The 17 `RADMIN` occurrences in
  the battery file are all PIN-derivation **comments** naming earlier slices (`§RADMIN-0d`, …);
  `grep -c MR_FEAT_RADMIN tools/probe_ui_model_mutations.py` = **0**. An unchanged downstream includer does not
  become a RADMIN consumer merely by including the header.
- **Union = EMPTY**, exactly as the pre-check predicted. ⚠ **An empty battery union is not zero mutation
  coverage:** the scaffold's acceptance surface — the nine-row compile-time matrix and the three board-only
  refusals — is owned by `tools/probe_features/run.sh`, whose 19 controls are enumerated in §5. No existing
  battery was edited and production scope was not widened to make anything pass.

## 13. B304 — the header correction is provably comment-only

The brief requires the B304 prose correction isolated and proved token-identical under B254. Comments and blank
lines were stripped from both versions with the compiler itself
(`gcc -fpreprocessed -dD -E -P -x c++`), leaving only preprocessor directives:

```
directive-only lines: base 28  ->  final 49
diff(base, final):  +21 lines, and EVERY ONE of them is new endpoint code —
                    the 10-line derivation block and the 11-line board-check block.
                    ZERO lines of the diff come from the intro or the pointer correction.
```

⇒ the B304 edit contributes **no directive token whatsoever**; it is prose, and the board A/B (§9) shows it cost
zero bytes on both ABIs including the xtensa payload hash.

What the header now withdraws visibly, under the correction idiom:
1. *"An env sets ONE `MR_PROFILE_*`"* — **WAS**; five board envs set none and are static products.
2. *"No profile set => every `MR_FEAT_*` defaults to 1"* — **WAS**; `MR_FEAT_OLED` defaults to 0 and the RADMIN
   pair is derived, not defaulted.
3. *"See `docs/superpowers/specs/2026-07-12-firmware-feature-split.md`"* — **WAS**; the spec was **archived**,
   not lost: `docs/superpowers/specs/archive/2026-07-12-firmware-feature-split.md`, labelled history and
   explicitly **not** current authority. Current authority is named as R-RA-26 plus the current design §19.

⛔ **B304 is only PARTIALLY discharged and must not be closed:** its sibling stale pointer at
`platformio.ini:456` is out of fence (R-RA-26 forbids an ini edit in slice 1) and remains **OPEN**.

## 14. Findings

1. **NEW — stale `file:line` citation of `mr_features.h`, out of fence.** `lib/core/node_mac.cpp:669` reads
   *"(mr_features.h:47 makes MR_FEAT_TEAM imply MR_FEAT_MOBILE…)"*. The claim is **true**; the line number is
   not. At the base the `#error` was at **:49** (`:47` was already the section-header comment, so the citation
   was imprecise before this slice); after this slice it is at **:87**. `node_mac.cpp` is production `lib/core`
   **and** a mutation-battery target — editing it would break C1 and invalidate the corpus/board inertness
   proofs — so it was **deliberately not touched**. Recommend a one-line comment fix in a later slice that
   already touches that file, or its own trivial doc slice. For the register (M1).
2. **B305's labels were used as corrected and independently reproduce**: native 222072 · `heltec_mobile`/Xtensa
   117912 · `gateway`/ARM 148680, measured by `tools/probe_board_abi.py` on this tree.
3. **The five no-profile board envs really are static products in the code now**, not "profile missing" — the
   header says so and `envmap.py` E4 measures the set is exactly
   {`production`, `xiao_sx1262`, `heltec_v3`, `heltec_v4`, `xiao_esp32s3`}.
4. **Metal residue: NONE** (M2). All new behaviour is compile-time and is exercised by the configuration gate;
   no Bench Part is added.

## 15. File inventory (exact, final tree)

```
$ git status --short
 M docs/2026-07-30-open-bug-register.md                                          <- NOT MINE (§11)
 M docs/superpowers/plans/2026-09-05-radmin-slice1-feature-boundary-scaffold.md  <- the Author's base pin
 M lib/core/mr_features.h                                                        <- MINE (the only production change)
?? docs/superpowers/plans/2026-09-05-radmin-slice1b-precheck.md                  <- NOT MINE (§11)
?? tools/probe_features/                                                         <- MINE
?? tools/test_probe_features.py                                                  <- MINE

$ git status --short --untracked-files=all | grep '^??'
?? docs/superpowers/plans/2026-09-05-radmin-slice1b-precheck.md   (not mine)
?? tools/probe_features/envmap.py            (mine, mode 644)
?? tools/probe_features/mutate.py            (mine, mode 644)
?? tools/probe_features/probe_main.cpp       (mine, mode 644)
?? tools/probe_features/run.sh               (mine, mode 755 — executable)
?? tools/test_probe_features.py              (mine, mode 644)
+ docs/superpowers/evidence/2026-09-05-radmin-slice1.md  (mine — this file)

$ git diff --check      -> clean, no output
$ git diff --stat HEAD -- lib/core/mr_features.h
 lib/core/mr_features.h | 63 ++++++++++++++++++++++++++++++++++++++++++++------
 1 file changed, 59 insertions(+), 4 deletions(-)
```

Every new file carries `// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>` (or `# Author:`) on line 2.

⛔ **Nothing was committed, stashed, checked out or reset (D4).** The work is green and left uncommitted for the
owner.
