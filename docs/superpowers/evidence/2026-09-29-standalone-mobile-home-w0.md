Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# Standalone Home W0 — the identity record: one live→record conversion, the rename service and loud refusals (B440, B448, B482): coder report

**Role:** coder. **Contract:** brief **revision 3**,
[`docs/superpowers/plans/2026-09-29-standalone-mobile-home-w0-identity-record.md`](../plans/2026-09-29-standalone-mobile-home-w0-identity-record.md),
SHA-256 `2197165149e58a25b3eeccfc88906e652e69641734d8a532a123ecc42e56a6d8` (585 lines), authorized by the
[review](2026-09-29-standalone-mobile-home-w0-brief-review.md) (HOLD), the
[re-review](2026-09-29-standalone-mobile-home-w0-brief-rereview.md) (HOLD on W0R-4) and the
[second re-review](2026-09-30-standalone-mobile-home-w0-brief-rereview-2.md) (PASS). **Base:** owner commit `0f23aee`,
with the [W0 pre-check](2026-09-29-standalone-mobile-home-w0-precheck.md)'s `inputs.json` as the whole-tree authority;
simulator `6585649`, clean and read-only throughout. **Evidence:**
[`2026-09-29-standalone-mobile-home-w0/`](2026-09-29-standalone-mobile-home-w0/) with its own `SHA256SUMS`; raw logs
under the ignored `artifacts/2026-09-29-standalone-mobile-home-w0/`, indexed with hashes in
[`receipts/raw-logs.txt`](2026-09-29-standalone-mobile-home-w0/receipts/raw-logs.txt); raw board outputs under
`.pio-measure/w0/`; simulator and mutation scratch stayed outside both repositories. **Nothing staged, committed, reset,
cleaned or discarded.**

**Verdict: ready for independent QA.** The §4.1 chain ran once, start to finish, green on the frozen tree.

## 1. Preflight and the inventory check

- The brief's SHA-256 equals the authorized `21971651…6a8` (585 lines). `CLAUDE.md` and `docs/CODE_GUIDELINES.md`
  were read.
- `scope.py preflight` → [`preflight.json`](2026-09-29-standalone-mobile-home-w0/preflight.json): **PASS**.
  - HEAD is `0f23aee`; the simulator is at `6585649`, clean; nothing is staged; `git diff --check` is clean.
  - All 9 executable inputs are at their base SHA-256 and line counts, and the two new paths are absent.
  - The 7 read-only inputs, the 6 preparation files and the pre-check's `SHA256SUMS` (36/36) all verify.
  - Against `inputs.json` (2123 MeshRoute and 285 simulator paths): 4 paths changed (the preparation set), 0 missing,
    and 72 new paths, all explained (the pre-check, the brief, the three review receipts, this package's evidence).
- **Source check before editing (by symbol): every §1 fact held.**

| Seam | What the source shows at the base |
| --- | --- |
| `IdBlob` | 80 B with the stated offsets; `kIdMagic` is `'MRID'`, `kIdVersion` is 1 |
| `load_id` / `save_id` | an exact predicate; `save_id` does not coalesce |
| lat/lon arm | builds from `load_id` plus the running seed, and publishes the coordinate before saving |
| name arm | clamps `strlen` to 32 and publishes after a successful save |
| `do_regen` | CLIENT admission, an unchecked `load_id`, a new seed, one checked save, then the installs |
| inbox-verbs builder | 8 config-handler stubs; the `rc=0` / `build_support` boundary appears once; deferred-actions links the command TU without the config TU |

## 2. Baselines (the unmodified tree, before any edit; `run-baselines.sh`, sequential, every step exit 0)

| Instrument | Base result |
| --- | --- |
| native (`pio test -e native`, then the binary) | **3055 cases / 199532 assertions / 0 failed / 0 skipped** |
| inbox-verbs default | accept **1400 / 63**, client **483 / 71**, oled **27 / 7** (checks / controls) |
| console-sink | checks 720, structural 84, ble_guard 905, ownership 6/3, **152** controls |
| board-UI | **592** identities; wiring live 60 / mutant 186; negctl 60 + 3 |
| deferred-actions | 150 / 151 / 158 / 158 checks, 39 transcripts; **40** controls RED |
| device-radio | 96 + 41 checks, 25 structural, **72** controls, 0 unusable |
| prov-tx | 20 structural checks PASS, **47** controls RED |
| ownership scanner | PASS (218 files); `--controls` **43 / 0** |
| ABI | stock **290** checks, 9/9 controls RED; with the pre-check's `IdBlob` overlay **299** checks, `IdBlob` **80/4** on native, heltec_mobile and gateway |
| boards, `pair --jobs=1` into `base-1` / `base-2`, back to back | both `compare` **PASS** |

The base board figures:
- **gateway:** RAM 203740, flash 571936, payload `97deca60…2886`.
- **heltec_mobile:** RAM 219540, flash 1400276, payload `6af09c96…5998`.

Receipt: [`receipts/baselines.txt`](2026-09-29-standalone-mobile-home-w0/receipts/baselines.txt).

## 3. Diff and the ledger

`git diff --stat HEAD` shows 13 tracked files: **9 fenced files** plus the 4 pinned preparation files (design, register,
`tracker.md`, `MEMORY.md`), which W0 did not touch (their hashes equal preflight's). There are also **2 new fenced
files**.

- **`src/device_nv.h`** (+19) — the pure `mrnv::id_blob_from_live`, beside `IdBlob`, above the platform `#if`:
  - it admits the length on its `size_t` before any narrowing, and refuses a null pointer with a non-zero length;
  - it zeroes all 80 bytes, stamps the magic and version, and copies every field;
  - a refusal leaves a wholly zero record;
  - nothing else in the file moved.
- **`src/firmware_config.h`** (+20) — the declarations: the adapter `id_candidate_from_live` (with an optional name
  override), `enum class RenameResult { saved, unchanged, too_long, bad_args, nv_save_failed }` and `rename_node`.
- **`src/firmware_config.cpp`** (+53 / −14) — three changes:
  - the adapter, the one gatherer of `g_identity.seed`, the counted `effective_name(…, 32)` and `g_lat_e7` / `g_lon_e7`;
  - the stateless rename service, following design §4.3's transaction;
  - the lat/lon arm, which now builds from the live snapshot with one coordinate replaced and publishes BOTH mirrors only
    after a successful save (B482), plus the name arm as the service's first caller, raw to the end of the line.
- **`src/firmware_commands.cpp`** (+5 / −3) — only `do_regen`'s candidate changed: the live snapshot with the new seed.
  Its admission, entropy draw, checked in-function `save_id` and installs keep their order.
- **`test/test_device_nv.cpp`** (+90) — four `device_nv/W0:` cases through the real helper.
- **`tools/probe_inbox_verbs/`** — `probe_main.cpp` (+31 / −3), `run.sh` (+128 / −10), and the new files
  `identity_main.cpp` (423 lines) and `identity_platform.h` (44 lines). See §6.
- **`tools/probe_ui_model_mutations.py`** (+38 / −1) — ten `devicenv` entries and the PIN.
- **The command inventory** — a stock `--write` regeneration: 138 anchor lines moved, and the result is
  anchor-normalised identical ([`receipts/inventory.txt`](2026-09-29-standalone-mobile-home-w0/receipts/inventory.txt)).

**The §2.8 closed disposition ledger:**
[`ledger/disposition-ledger.md`](2026-09-29-standalone-mobile-home-w0/ledger/disposition-ledger.md). It lists every
changed or added expectation, fixture, control and mutation on its own row, with its must-fail result and the final
verdicts. The rulings taken while coding are in
[`ledger/coder-ledger.md`](2026-09-29-standalone-mobile-home-w0/ledger/coder-ledger.md):

1. **One adapter, with an optional name override.** `id_candidate_from_live(out, name = nullptr, len = 0)`:
   - lat/lon and `regen` call it without an override, then replace their one field;
   - it cannot refuse without an override (the live name is at most 32 bytes by construction), so those calls cast the
     result to `(void)`, with a comment at each site.
2. **`bad_args` means a null or empty requested name**, with zero writes. The console's own empty-value refusal runs
   first and is unchanged; W7 inherits the rule.
3. **`unchanged` makes the requested name live only if it differs** (design §4.3's wording); the observable result is
   identical either way.
4. **R23 also needed the live precondition.** The brief named R12, R21, R22, R25 and R26. Measured without the
   precondition, exactly those plus R23 fail, printing the probe's constructor-default live name `"node"`
   ([`receipts/r-fixture-necessity.txt`](2026-09-29-standalone-mobile-home-w0/receipts/r-fixture-necessity.txt)). R23 is
   the USB repeat of R12's line and gets the same correction. Every expected output and persisted field is unchanged,
   and `seed_id` installs no crypto.
5. **The identity arms run only their own rows and controls.** The shared R7/A7/A10 control block is now gated to
   arms 1/2, as it already was away from the OLED arm; on the identity arms each would score `passes` (measured).

## 4. Figures (the §4.1 chain, in order; nothing else ran between or during the steps)

| Step | Instrument | Result |
| --- | --- | --- |
| 0 | `scope.py final` → [`receipts/inputs-start.json`](2026-09-29-standalone-mobile-home-w0/receipts/inputs-start.json) | **PASS** — 13 changed (9 fenced + 4 preparation), 0 unexplained, 0 missing; the simulator is clean |
| 1 | hygiene: `git diff --check` | **exit 0**; the ledger is closed against the diff |
| 2 | native: `pio test -e native`, then `./.pio/build/native/program` | **3059 test cases / 199638 assertions / 0 failed / 0 skipped** |
| 3 | corpus: a fresh stock `lus` (`cmake -S <simulator> -B <scratch> -DMESHROUTE_DIR=…`), `run_corpus.py --require-anchors`, then `--validate` | **36/36 streams, 36/36 anchors** reproduce `simulation/BASELINE.md` (s18 **269517 / `32afbf11…`**); validate PASS. Compared field by field with the pre-check manifest: **36/36 identical on all 14 fields**, same BASELINE hash. The fresh `lus` is byte-identical to the pre-check's (`e304147d…`) ([`corpus-compare.txt`](2026-09-29-standalone-mobile-home-w0/receipts/corpus-compare.txt)) |
| 4 | ABI: stock, then the pre-check's `identity-abi.json` overlay | stock **PASS, 290 checks, 9/9 controls RED**; overlay **PASS, 299 checks**, `IdBlob` **80/4** on all three ABIs, unchanged ([`abi-final.txt`](2026-09-29-standalone-mobile-home-w0/abi-final.txt)) |
| 5 | inbox-verbs default, five arms | accept **1400 / 63**, client **483 / 71**, oled **27 / 7** (all unchanged); **identity_accept 175 / 13**, **identity_client 176 / 13**; sources unchanged |
| 5 | console-sink | **PASS** — 720 / 84 / 905 / 6+3 / 152 (unchanged) |
| 5 | board-UI | **PASS** — **592** identities, wiring 60/186, negctl 60 + 3; no manifest delta |
| 5 | deferred-actions (source unchanged) | **PASS** — 150 / 151 / 158 / 158, 40 controls RED (unchanged) |
| 5 | device-radio / prov-tx / ownership | **PASS** — 96+41, 25, 72 controls / 20 structural, 47 RED / scanner PASS, `--controls` 43/0 (all unchanged) |
| 6 | stack (`stack-measure.py`) | see §5 |
| 7 | tools discovery | **Ran 406, OK, 0 skipped** (812 s) |
| 8 | checkers | inventory `--check` **PASS, 197 rows**; authority **PASS** plus **6/6** selftests RED; literals **PASS** (218 files); warning census **PASS 6/6, `-Wswitch` 0**, warnings 171/175/175/175/179/179 (the pinned baselines) |
| 9 | boards: `final-1`, `final-2` back to back, stock `compare` | **PASS on both envs**; attribution in §5 |
| 10 | mutation union: selector (a), then (b) | **134 entries, 134 RED, 0 unusable, 0 vacuous, every match count 1**; all **36** worker baselines **3059 / 199638 / 0** (= step 2); every battery reports the real tree untouched |
| 11 | `scope.py final` → `receipts/inputs-end.json` | **PASS** — the fence, the two new files, the preparation set and the read-only inputs are identical to step 0; the only differing new file is `stack-measure.json`, written by step 6 |

**The identity arms** (brief §2.7) link the REAL, unedited `firmware_config.cpp` and `firmware_commands.cpp` and drive
`cfg set name|lat|lon` through the real router onto the real guarded console. `regen` is driven through the production
`LineSink`. Their rows:

- **M, the storage × writer matrix (140 rows).** Four writers (name, lat, lon, regen) × seven media (a
  healthy-but-DIFFERENT record, absent, unreadable, short, bad magic, bad version, oversize) × five aspects:
  - the exact output line;
  - exactly one write;
  - the saved record equals the live seed, name and position with only the intended field replaced, against a record
    built field by field — the durable record deliberately carries a different seed, name and position;
  - the live state;
  - every unrelated store untouched.
- **N, rename coalescing and repair (12 rows).** A byte-identical no-op costs zero writes. W0R-1: an already-durable,
  not-yet-live name answers `unchanged` with zero writes and the name live afterwards. When the live name equals the
  request but the record is absent, bad, short, different or unreadable, the record is repaired; a stale position is
  repaired too. A failed save publishes nothing. A live 32-byte name survives a coordinate write whole.
- **G, B448 through the real router (18 rows).**
  - Accepted and saved whole: spaces, quotes, an all-space name, 31 and 32 bytes, a 2-byte character within 32, and
    32 high bytes.
  - `bad_args`: an empty value, including the trailing-space form.
  - `too_long`: 33 bytes, a 2-byte and a 3-byte character crossing byte 32, and 300 bytes. Each refusal writes nothing
    and leaves the seed, name, both position mirrors, membership and every NV slot unchanged.
- **C, B482 (4 rows).** A failed coordinate save prints `nv_save_failed` and leaves both mirrors unchanged.
- **R (1 row, plus 1 on CLIENT).** A refused `regen` save changes nothing. On CLIENT, remote debt refuses `regen` with
  zero writes.

Diagnostics print lengths and hex only (B478).

**Transport scope.** This is not the BLE adapter: the B483 branch in `fw_main.cpp` stays outside, and nothing here
claims it.

## 5. Allocation: stack frames and the attributed board deltas

**No retained allocation.** The adapter and service are stateless (two 80-B candidates and a 32-B name on the stack).
There is no `Node`, layout, NV-version or schema change, and **RAM is unchanged on both boards (+0)**.

**Stack.** These are compile-only `-fstack-usage` frames, not a task high-water mark
([`stack-summary.txt`](2026-09-29-standalone-mobile-home-w0/stack-summary.txt)).

Xtensa heltec_mobile, where the common head (`mesh_service_once` 2128 + `exec_console_line` 400 + `dispatch` 336) is
2864 B:

| Path | Base | Final | Change | New frames |
| --- | --- | --- | --- | --- |
| name | 3216 B | **3536 B** | +320 | `rename_node` 240 + adapter 80 |
| coordinate | 3216 B | **3296 B** | +80 | the adapter |
| `regen` | 3184 B | **3264 B** | +80 | the adapter |

`handle_cfg_set` (352) and `do_regen` (320) are unchanged. All three paths stay below W6's measured 3632 B console
chain.

On the gateway (ARM), `handle_cfg_set` goes 440 → 448, the adapter is 64, and the compiler reports 0 for the
out-of-line `rename_node` (the body is inlined into `handle_cfg_set`, as its +8 shows).

**Boards (base-1 → final-1, field by field, down to symbols;
[`boards/attribution.txt`](2026-09-29-standalone-mobile-home-w0/boards/attribution.txt)):**

| env | RAM | flash | attributed to |
| --- | --- | --- | --- |
| gateway | 203740 → 203740 (**+0**) | 571936 → 572096 (**+160**, all `.text`) | `rename_node` (clone `.part.0`) +396, `id_candidate_from_live` +144, `handle_cfg_set` −284 (the three arms shrank), and inlining shifts in the command TU W0 edited (`dispatch` −68 with `do_regen` inlined and smaller, `acl_emit_err` −92 / `acl_verb` +48); symbols net +144, and +16 is section alignment |
| heltec_mobile | 219540 → 219540 (**+0**) | 1400276 → 1400540 (**+264**) | `.flash.text` +248: `rename_node` +282, `id_candidate_from_live` +140, `handle_cfg_set` −110, `load_id` −50 (fewer callers, inlined differently), `do_regen` −28, and ±1–8 B of alignment/literal-pool shifts in neighbouring config-TU functions whose source did not change; `.flash.rodata` +16: the new `> cfg err too_long` literal |

The payload hashes move accordingly: gateway `.hex` +450 B, heltec_mobile `.bin` +272 B. The `source.*` fields move with
the tree. `normal_pio_metadata` fingerprints the ordinary `.pio/` tree, which the native and census builds touched
(`normal_pio_used: false`). **The gateway image moves, as the brief anticipated** (it compiles both TUs), and every byte
is attributed.

## 6. Controls and mutations

**Reconciliation.**
- **The legacy inbox arms** are unchanged at 1400/63 and 483/71.
  - Their eight config stubs are kept byte for byte behind `#ifndef MR_PROBE_IDENTITY_ARM`.
  - The labelled legacy adapter stand-in in `probe_main.cpp` keeps accept, client, oled, deferred-actions and the
    (not run) transcript driver linkable. It builds through the real `mrnv::id_blob_from_live` and is excluded from the
    identity arms.
  - The legacy controls anchoring on `do_regen` are untouched and RED: C8, C9, C35, C36, C12/C13 and the CLIENT C8
    series.
- **The identity arms** have 175 / 176 rows (the derivation is in `run.sh`) and 13 controls each. Every control
  mutates real production code. Each counts only if it reddens its INTENDED row — a crash, a build failure or a
  vacuous mutant is never RED:

| Control | Real code mutated | Intended row | Reddened (both arms) |
| --- | --- | --- | --- |
| W0-C1 | lat/lon candidate from `load_id` | `M.lat.*.rec` | 15 |
| W0-C2 | `regen` candidate from an unchecked `load_id` | `M.regen.*.rec` | 14 |
| W0-C3 | overlength early return removed | G8/10/11/13 | 8 |
| W0-C4 | the console arm clamps to 32 (B448) | G8/10/11/13 | 8 |
| W0-C5 | name published before the save | N5b | 1 |
| W0-C6 | coordinate published before the save (B482) | C.lat/lon.live | 2 |
| W0-C7 | only the global lat mirror published | `M.lat.*.live` | 7 |
| W0-C8 | no-op writes (coalescing dropped) | N1/N2 | 2 |
| W0-C9 | no-op keyed on live-name equality | N3a–e | 7 |
| W0-C10 | equal branch returns before `set_name` | N2b | 1 |
| W0-C11 | adapter reserves a terminator byte | N6 | 1 |
| W0-C12 | adapter reads seed/position from `load_id` | `M.name.*.rec` | 35 |

**The union by battery** ([`receipts/union.txt`](2026-09-29-standalone-mobile-home-w0/receipts/union.txt)):

| Selector | Battery | Base | W0 | Final |
| --- | --- | --- | --- | --- |
| (a) | devicenv | 46 | **56** (+10: W0-N1 seed, N2 name, N3 lat, N4 lon, N5 magic, N6 version, N7 overlength accepted, N8 narrowed to `uint8_t`, N9 narrowed to `uint16_t`, N10 not zeroed) | 56 RED |
| (b) | config / w1cname / consoleline / radmin4verbs | 32 / 4 / 12 / 30 | unchanged | all RED |
| | **total** | **124** | **134** | **134 RED / 0 unusable / 0 vacuous** |

## 7. The pin line

PIN re-synced? YES — `tools/probe_ui_model_mutations.py` `PIN_CASES, PIN_ASSERTS` 3055, 199532 → **3059, 199638**, derived from the full native binary per source file: `test_device_nv.cpp` 27/471 → 31/577 (+4 cases / +106 assertions — the four `device_nv/W0:` cases), every other file unchanged (no other test file is in W0's fence); 3055 + 4 = 3059 and 199532 + 106 = 199638 = step 2's binary = all 36 union worker baselines. The inbox-verbs arms' own pins were derived from their clean runs: `PIN_CHECKS_IDENTITY_ACCEPT=175` (M 140 + N 12 + G 18 + C 4 + R1 1), `PIN_CHECKS_IDENTITY_CLIENT=176` (+R2), `PIN_CONTROLS_IDENTITY=13` ([[B237]] + W0-C1..C12); accept/client/oled pins unchanged (1400/63, 483/71, 27/7).

## 8. Freeze inventory

- **Authorized brief:** `2197165149e58a25b3eeccfc88906e652e69641734d8a532a123ecc42e56a6d8`, unchanged.
- **Base:** HEAD `0f23aee8f5eeaa6e176e3ea37b2acaffb33622ec`; nothing staged. **Simulator:** `6585649ea5a780f0542b2931853a667be56a5b2b`, status clean.
- **Fenced files** — final SHA-256 values, recorded in
  [`receipts/inputs-end.json`](2026-09-29-standalone-mobile-home-w0/receipts/inputs-end.json):

| Fenced file | Lines | SHA-256 (frozen) |
| --- | --- | --- |
| `src/device_nv.h` | 1609 | `36296bcbc1c6975a6cf0bea2caf1ca5eebd182832f2550c12de1a7185429fffa` |
| `src/firmware_config.h` | 257 | `c72d59600745da4d7b65b8cbd37d3da5b9f5a74da33cc5a71a98a4fa8aac8bd6` |
| `src/firmware_config.cpp` | 2475 | `accb667703290ba57e590b4ec07d20ab6bf21dc8ae7d8b9ae1840e2bf3dfdc18` |
| `src/firmware_commands.cpp` | 1917 | `48f9b19a9f402585c8f3c061999d4c993ec53cf936b6f6b9a4921e586bc060fd` |
| `test/test_device_nv.cpp` | 1020 | `6d5c49406ab6748e600487d7ffafe29f6886d11bfbd1a295a42b4d3f21389260` |
| `tools/probe_inbox_verbs/run.sh` | 1051 | `db7add6c5fb335097bc84a192b925b43604f638f7951e422cd926f7ab6292948` |
| `tools/probe_inbox_verbs/probe_main.cpp` | 2460 | `69cc26c59f0be12952421501fb0365e85195ff9343995cd4056068e20f0f7650` |
| `tools/probe_inbox_verbs/identity_main.cpp` (new) | 423 | `014468b2ff410685682197f8c3d1ddf6c1212a9d813da9ac21c1b6f072413e65` |
| `tools/probe_inbox_verbs/identity_platform.h` (new) | 44 | `edf12b2681bf43091ef94810bb60f140be67670834922a7318460560c6fcb8a3` |
| `tools/probe_ui_model_mutations.py` | 12931 | `b71270ee2ed63d67bfbc4051a8b98cb80ce2e2da49cbba27aa548d781fb13a70` |
| `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` | 259 | `c320046822c0cb47bb38ca76f4c1ea8ebd42c7f2346bb50791a35e05d74ae807` |

- **Read-only inputs, all unchanged** (same hashes as the brief's §1):
  - `tools/probe_deferred_actions/run.py` and `probe.cpp`;
  - `tools/probe_inbox_verbs/transcript_main.cpp`;
  - `tools/probe_console_sink/structural.py`;
  - `tools/probe_board_ui/run.sh` and `expected.tsv`;
  - `tools/gen_command_inventory.py`.
- **Preparation set, unchanged since preflight:**
  - design `da85f228…66f0`;
  - register `88fe7cea…4b86`;
  - pre-check report `a10f1186…0942`;
  - pre-check `SHA256SUMS` `88fed771…16c1` (36/36 OK);
  - `tracker.md` `ee6ae9da…9669`;
  - `MEMORY.md` `242610cc…f229`.
- **Inventory check:** against the pre-check's `inputs.json`:
  - MeshRoute: 2123 paths inventoried, 13 changed (9 fenced + 4 preparation), 0 unexplained, 0 missing;
  - new paths: only the explained pre-check, brief and receipt paths, the two new fenced files and this package's
    report and evidence;
  - simulator: 285 paths, 0 changed, 0 missing, 0 new.
- **New evidence:** this report and `2026-09-29-standalone-mobile-home-w0/**`, each hashed in its
  [`SHA256SUMS`](2026-09-29-standalone-mobile-home-w0/SHA256SUMS).
- **Stability statement:** the inputs recorded at step 0 and step 11 differ only by new evidence (the step-6
  `stack-measure.json`). No fenced, preparation or read-only input changed during the chain, or since. A last
  `scope.py final`, taken with all evidence written ([`receipts/inputs-freeze.json`](2026-09-29-standalone-mobile-home-w0/receipts/inputs-freeze.json)),
  is the freeze snapshot. Nothing was staged or committed.

## 9. Not run, with reasons

- **The inbox-verbs transcript comparator (`transcript.py` / `transcript_main.cpp`)** — **not run, by the owner's
  ruling of 2026-09-30 (B487/B488)**. It fails at the base, and its USB wrapper admits only seven bytes. Nothing here
  claims it ran or passed, or that its coverage is unchanged. Its files are unchanged, and the stand-in keeps it
  linkable once B478's package repairs it.
- **`cfgparse` and `sliceDtoken` mutation batteries** — their predicates are unaffected: their only target,
  `src/firmware_config_parse.h`, is untouched (the brief adds them only if it is edited).
- **The other mutation batteries outside selectors (a) and (b)** — their predicates are unaffected. Their target files
  are untouched, and the reader audit finds none of them reading the W0 TUs. The only shared header, `device_nv.h`,
  gained an additive helper, and `devicenv` covers it.
- **BLE-line, custody-USB and the nine-cell feature probe (`tools/probe_features/run.sh`)** — their predicates are
  unaffected. The reader audit shows none of them compiles or reads `firmware_config.cpp`, `firmware_commands.cpp` or the
  identity helper; their `probe_main.cpp` matches are their own files.
- **Supplemental, not required:** I ran the firmware-UI probe, since it compiles `device_nv.h`. It is identical to the
  pre-check: 591 / 1059 / 591, 240 controls
  ([`receipts/supplemental-fwui.txt`](2026-09-29-standalone-mobile-home-w0/receipts/supplemental-fwui.txt)).

**For QA's landing (§7):**
- **Register:** B440, B448 and B482 can close in place. B478 and B483–B486 stay open.
- **Metal plan:** a console check such as `cfg set name` with 33 bytes, which should answer `> cfg err too_long` and
  change nothing, and a normal rename that survives a reboot.

**Observations (not changed, outside W0's purpose):**
- The runner's closing line still says `BOTH ARMS PASS`, although five arms now run (the wording predates W6).
- The identity arms' summary line reuses the legacy `against the REAL dispatch()+handle_clear_inbox` wording.

**Ready for independent QA** — freeze inventory in §8. Uncommitted, per D4; the owner commits and bench-verifies on
metal.
