<!-- Author: Claude (Opus 5.5), W1 coder — implementation receipt for QA's independent gate; the owner rules and commits -->
# Standalone Home W1 — B241 label termination: coder receipt

**Status: IMPLEMENTED, CODER GATE (§4.1) GREEN, READY FOR QA's INDEPENDENT GATE (§4.2).** Nothing is committed,
staged or cleaned. B241 stays OPEN until QA's gate passes.

- **Contract:** [W1 brief revision 3](../plans/2026-09-24-standalone-mobile-home-w1-label-termination.md), SHA-256
  `617c2be56cdf2db21981373aac3f8dd5a5baa09235d8b753f2a9210ccab5d018`. It was verified before any other action and is
  unchanged at the end.
- **Approval:** [QA PASS receipt](2026-09-24-standalone-mobile-home-w1-brief-rereview-2.md).
- **Evidence:** [`2026-09-24-standalone-mobile-home-w1/`](2026-09-24-standalone-mobile-home-w1/). It contains a
  `SHA256SUMS` over every file in it.
- **Ignored files:** raw `*.log` files in that directory are gitignored by repository policy (`.gitignore:54`,
  `docs/superpowers/evidence/README.md`). They are present locally and hashed. The figures they carry are repeated in
  the tracked `*.txt`/`*.json` receipts and in this report.

## 1. Preflight

| Check | Found |
| --- | --- |
| MeshRoute | `HEAD` = `4a230f4501c6a19d71b9234968bf1389484d8295`, branch `main` |
| Simulator | `6585649ea5a780f0542b2931853a667be56a5b2b`, `git status` empty — at preflight and at the end |
| Executable inputs, 5/5 | `git diff --quiet HEAD` clean; disk hash = `git show 4a230f4:` hash = brief §1 hash for all five |
| Preparation set, 10/10 | `sha256sum -c` OK against the brief's §1 table (design, register, pre-check, brief review, re-review, their three `SHA256SUMS`, `tracker.md`, `MEMORY.md`) |
| Pinned evidence directories | `sha256sum -c SHA256SUMS` OK: pre-check 10, brief-review 6, re-review 5 = **21 files** |
| Explained QA addition | re-review-2 report + folder (3 files + `SHA256SUMS`, which verifies) — classified under preflight item 4 as the receipt instructs |

`git status` classification at preflight:
- four `M` files, which are the preparation set with matching hashes;
- the brief, pinned by its authorized hash;
- the pre-check, brief-review and re-review reports and folders, pinned;
- the re-review-2 report and folder, explained.

Nothing else was present. Receipt: [`preflight.txt`](2026-09-24-standalone-mobile-home-w1/preflight.txt).

## 2. Baselines (captured before the first implementation edit)

- **Probe:** unmodified `tools/probe_firmware_ui/run.sh`, default controls, exit 0.

  | Arm | Checks passed / total |
  | --- | --- |
  | l2 | 433 / 433 |
  | v3 | 868 / 868 |
  | BLE-row | 433 / 433 |

  - **Controls: 223 verified / 0 unusable.** The l2 section has 144 (C0 included) and the v3 section 79.
  - Checks per arm: l2 404 · v3 839. Coverage 703 of 840.
  - Tree tripwire md5 `f87779aa254f64992ba36d19611e48a0`, unchanged.
  - Receipts: [`probe-base-summary.txt`](2026-09-24-standalone-mobile-home-w1/probe-base-summary.txt) and `raw/probe-base.log`.
- **Native** (baseline, my own run): 2,950 cases / 195,770 assertions / 0 failed / 0 skipped
  ([`native-summary.txt`](2026-09-24-standalone-mobile-home-w1/native-summary.txt)).
- **Boards:** two back-to-back runs, `pair --jobs=1` into `.pio-measure/w1/base-1` and `base-2`, as one chained
  command with nothing between them. Both runs are identical:

  | env | RAM | flash | objects | payload SHA-256 |
  | --- | --- | --- | --- | --- |
  | `gateway` | 203,740 | 572,240 | 285 | `cd56846e3272db109eff6f77d54c5ea97a05072c679976a4357188c1a35ae9c1` |
  | `heltec_mobile` | 211,724 | 1,394,704 | 329 | `e7049bb7ea6a0d4daee85cf00275838a58e089e5278f891ef0b4df86d58f23d7` |

  Stock `compare base-1 base-2`: **PASS `gateway`, PASS `heltec_mobile`**
  ([gateway](2026-09-24-standalone-mobile-home-w1/compare-base-gateway.txt),
  [heltec_mobile](2026-09-24-standalone-mobile-home-w1/compare-base-heltec_mobile.txt)).

## 3. Diff (fenced files only)

```
 src/firmware_ui.cpp                    |  13 +-
 test/test_node_hashlocate.cpp          |  39 +++++
 tools/probe_firmware_ui/probe_main.cpp | 258 +++++++++++++++++++++++++++++++++
 tools/probe_firmware_ui/run.sh         |  45 +++++-
 tools/probe_ui_model_mutations.py      |   3 +-
 5 files changed, 353 insertions(+), 5 deletions(-)
```

No `lib/` file, other `src/` file, `platformio.ini`, `tools/probe_board_ui`, census or measurement tool was touched.
The four pre-existing `M` documents are unchanged (§8).

**Production (§2.1).** `label_from_hash` now has four steps:
1. `cap == 0` returns before any subtraction.
2. It calls `peer_name_find(hash, out, uint8_t(cap - 1))`.
3. A zero count takes the unchanged `snprintf(out, cap, "0x%08lx", …)`.
4. Otherwise it writes `out[n] = '\0'`.

The function's comment now states the guarantee and why the raw API stays unterminated (its two 32-byte consumers).
Nothing else in production changed.

**Seam (§2.2).** `run.sh`'s new `ui_wrapper` writes, into `$OUT`, a TU that `#include`s **the path it is handed** and
defines `mr_probe_label_from_hash` → `label_from_hash`. It is the mechanism QA reviewed.
- **Used by:** `build_variant` (the l2 and v3 live runs and every `ctl` control) and the BLE-row arm.
- **Refusals (fail loud):** it refuses a path containing `"` or `\`, and it stops the run if the write fails. Both
  refusals were exercised and exit 1.
- **Unchanged:** the live builds keep `-Werror`, and `md5_sources` still hashes the real files.
- **Harness side:** `probe_main.cpp` declares the trampoline.

## 4. Figures (§4.1, all derived here)

Input hashes were recorded at the freeze ([`stability-start.txt`](2026-09-24-standalone-mobile-home-w1/stability-start.txt)).
The chain then ran in this order:

1. **Whitespace:** `git diff --check` exit 0.
2. **Native:** `pio test -e native` exit 0, then `./.pio/build/native/program`: **2,951 cases / 195,777 assertions /
   0 failed / 0 skipped**, exit 0. The delta is +1 case / +7 assertions, exactly the new case
   `B241 peer_name_find — a 32-byte name fills an exact 32-byte destination, and the push body carries all 32`
   (7 assertions).
3. **Corpus:** stock simulator configured and built fresh outside both repositories (`cmake -S
   ../lora-universal-simulator -B <scratch>/sim-build -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=…`, target `lus`).
   Then `tools/run_corpus.py --jobs 4 --lus <that lus> --require-anchors`, exit 0.
   - **36/36 streams validated, 36/36 anchors reproduce `simulation/BASELINE.md`**, `inputs_stable: true`.
   - **Byte-identity against the pre-check's `corpus-manifest.json`: 36/36 IDENTICAL.** Each stream was compared on
     full MD5, full SHA-256, byte count, events, exit status, assertion failures, anchor match and scenario hash.
   - s18 = 269,517 events, MD5 `32afbf11e43b4bf9d0bd470ad502ba0a`.
   - The freshly built `lus` SHA-256 `9fe475f3180b655b2550c9f275c6095fb74ce54a1523155c3468e6fb443ce52d` equals the
     pre-check's.
   - Receipts: [`corpus-compare.txt`](2026-09-24-standalone-mobile-home-w1/corpus-compare.txt) and
     [`corpus-manifest.json`](2026-09-24-standalone-mobile-home-w1/corpus-manifest.json).
4. **Firmware-UI probe** (default controls), exit 0, PASS.

   | Arm | Base | Final | New checks |
   | --- | --- | --- | --- |
   | l2 | 433 / 433 | **467 / 467** | +34 |
   | v3 | 868 / 868 | **902 / 902** | +34 |
   | BLE-row | 433 / 433 | **467 / 467** | +34 |

   - **Controls: 225 verified / 0 unusable** (l2 section 146 with C0, v3 section 79; base 223 + B241a + B241b).
   - **C0 still fails to build**, as required, now through the wrapper.
   - **B241a → RED (10)** and **B241b → RED (5)**.
   - The B227 audit reads 225/225 labels. Tripwire md5 `43ddc46062a4b30cff929ebd8ee8f127` is the same before and after.
   - Checks per arm: l2 438 · v3 873. Coverage 733 of 874.
   - Receipts: [`probe-summary.txt`](2026-09-24-standalone-mobile-home-w1/probe-summary.txt) and
     [`controls-verdicts.txt`](2026-09-24-standalone-mobile-home-w1/controls-verdicts.txt) (all 225 lines).
5. **Boards:** `final-1` and `final-2`, back-to-back as one chained command. Stock `compare final-1 final-2`:
   **PASS `gateway`, PASS `heltec_mobile`**. Base-to-final reading (`base-1` against `final-1`, field by field; not the
   stock `compare`):

   | env | field | base | final | attribution |
   | --- | --- | --- | --- | --- |
   | `gateway` | every `measurements.*` and `artifacts.payload.*` | — | **identical** | Prediction met: this env does not compile the OLED TU. Payload `cd56846e…` is unchanged. |
   | `heltec_mobile` | `ram_bytes` | 211,724 | 211,724 | unchanged |
   | | `flash_bytes` / `.flash.text` | 1,394,704 / 1,072,458 | 1,394,732 / 1,072,486 | **+28** |
   | | `object_count`, `symbol_count` | 329, 13,297 | 329, 13,297 | unchanged |
   | | `symbol_size_total` | 1,496,108 | 1,496,134 | **+26** |
   | | `symbols_sha256` | `27ec4102…` | `a0e27022…` | 12 symbol sizes changed |
   | | `payload.bytes` / `.sha256` | 1,395,344 / `e7049bb7…` | 1,395,376 / `ae615748…` | +32 (image padding of the +28 text) |

   **Attribution of the mobile delta:**
   - **Defining object.** The 12 changed symbols are all defined in `firmware_ui.cpp.o`, the only edited TU (the
     toolchain's own `nm` over the measurement build).
   - **The fix itself.** `label_from_hash` grows 34 → 50 B (+16): the `cap == 0` test, the `cap - 1` and the NUL store.
   - **Layout effects.** Eleven unchanged functions of the same object move by −4…+4 B, net +10. Their linked sizes
     differ from their object sizes (for example `mr_ui_tick` is 7,366 B in the object, 7,030 B linked at base and
     7,026 B at final). So Xtensa link-time relaxation is active, and these moves are placement effects of the shifted
     object, not code changes.
   - **Arithmetic.** The +26 symbol bytes plus 2 B of inter-function alignment give the +28 B of `.flash.text`.

   **Compatibility check:** all 48 `toolchain.*`, `fixed_identity.*`, `paths.*`, `concurrency.*`, `host.*`, `schema`
   and `environment` fields are identical, base to final, for both envs. `source.git_status_sha256`,
   `source.tree_sha256` and `normal_pio_metadata.sha256` differ as expected (the edit and the intervening native and
   probe builds). `source.git_head` and `source.file_count` are unchanged.

   Receipts: [`attribution-base1-final1.txt`](2026-09-24-standalone-mobile-home-w1/attribution-base1-final1.txt),
   [symbol diff](2026-09-24-standalone-mobile-home-w1/attribution-heltec_mobile-symbols.diff),
   [defining objects](2026-09-24-standalone-mobile-home-w1/attribution-heltec_mobile-defining-objects.txt),
   [object vs ELF](2026-09-24-standalone-mobile-home-w1/attribution-heltec_mobile-object-vs-elf.txt),
   [final compare gateway](2026-09-24-standalone-mobile-home-w1/compare-final-gateway.txt),
   [final compare heltec_mobile](2026-09-24-standalone-mobile-home-w1/compare-final-heltec_mobile.txt).
6. **Warning census:** `tools/warning_census.sh` exit 0, **PASS**. All six pinned OLED envs match their pins, with
   `-Wswitch` 0 on each:

   | env | warnings (= pin) |
   | --- | --- |
   | `gateway_heltec` | 171 |
   | `gateway_heltec_v4` | 175 |
   | `heltec_mobile` | 175 |
   | `heltec_v3` | 175 |
   | `heltec_v4` | 179 |
   | `heltec_v4_mobile` | 179 |

   No pin was changed and no env was added ([`census-summary.txt`](2026-09-24-standalone-mobile-home-w1/census-summary.txt)).
7. **Mutation union** (§6 below), each target run alone, one after another, with nothing else building:

   | Target | Result | Exit |
   | --- | --- | --- |
   | `uiteam` | **20 RED / 0 unusable** | 0 |
   | `uiinvite` | **32 RED / 0 unusable** | 0 |
   | `uisend` | **15 RED / 0 unusable** | 0 |

   - Every worker (24/24) derived the clean baseline **2,951 / 195,777 / 0**, equal to the re-synced PIN, so the B217
     stale-PIN banner never printed.
   - Each run reports the real tree untouched (62 target files byte-identical, no build in the checkout).
   - No battery entry, pattern or target changed.
   - Receipt: [`mutation-union-summary.txt`](2026-09-24-standalone-mobile-home-w1/mutation-union-summary.txt).
8. **Tools discovery** (D5; `run.sh` and the harness changed): `python3 -m unittest discover -s tools -p "test_*.py"`
   ran **356 tests, OK, 0 skipped**, exit 0
   ([`tools-discovery-summary.txt`](2026-09-24-standalone-mobile-home-w1/tools-discovery-summary.txt)).

**Input stability:** the inventory recorded at the freeze and the one recorded after step 8 are **identical**:
- the 5 fenced hashes;
- the brief hash;
- the 10 preparation-set checks;
- the re-review-2 hashes;
- the whole `git status`;
- both HEADs and the simulator status.

See [`stability-start.txt`](2026-09-24-standalone-mobile-home-w1/stability-start.txt) and
[`stability-end.txt`](2026-09-24-standalone-mobile-home-w1/stability-end.txt). No input changed during the chain.

## 5. Controls

- **Where to find each verdict:** all 225, verbatim, in
  [`controls-verdicts.txt`](2026-09-24-standalone-mobile-home-w1/controls-verdicts.txt). Every existing
  `must_build=yes` control compiled and went RED; none is unusable.
- **C0:** `restoring #include "fw_context.h" breaks the host build` → *build fails, as required*. The wrapper
  includes the mutant, the mutant includes `fw_context.h`, and the build dies at `<RadioLib.h>`.
- **B241a (the pre-fix body, restored):**
  - After the sed, its `label_from_hash` is byte-identical to `git show 4a230f4:src/firmware_ui.cpp`'s.
  - RED on 10 checks: P28a H1, H2, the 27-byte clamp, the rename to H1, the 14-, 15- and 32-byte names, and synthetic
    capacity 1; plus P28c's compose header and DELIVERED for the named teammate.
  - It compiles and exits 1 with named FAILs; nothing unrelated went RED.
- **B241b (the tempting wrong repair):** full capacity, and only `out[cap - 1]` terminated.
  - RED on 5 checks: P28a H1, H2 and the rename to H1 (the poison or stale tail after a short name survives), plus the
    named teammate's compose header and DELIVERED.
  - The 14/15/32-byte checks correctly stay green under it, because a last-byte terminator does fix long names.
- **How the RED sets were confirmed:** the runner prints counts only. The labels above come from rebuilding each mutant
  through the same wrapper route in scratch ([`helpers/mutrun.sh`](2026-09-24-standalone-mobile-home-w1/helpers/mutrun.sh)
  and [`controls-repro/`](2026-09-24-standalone-mobile-home-w1/controls-repro/)). The rebuild's counts equal the gate's.
- **Existing controls whose mutants call `label_from_hash`** (N4, N9, O2, O6, O8): each reddens the **identical set of
  checks** on the base tree (old direct compile) and on the W1 tree (wrapper). The counts are N4 8, N9 8, O2 1, O6 1,
  O8 2 in both (`controls-repro/res-*-{base,new}.txt`). Their meaning is unchanged. See F-1 for O8's pre-existing
  defect.
- **Re-anchoring:** none. The D6/P7 audit found no exact-source reader of the edited lines in `run.sh`,
  `tools/probe_board_ui`, `tools/probe_ui_model_mutations.py` or elsewhere in `tools/`/`test/`. N9, O6 and O8 only
  *insert* calls to `label_from_hash`; their patterns target other lines.

## 6. Selectors

- **(a) Batteries whose configured source file W1 changes: none.** A static AST read of `TARGET_SRC` (105 entries)
  finds no entry naming any fenced file ([`selector-a-census.txt`](2026-09-24-standalone-mobile-home-w1/selector-a-census.txt)).
  This matches QA's census.
- **(b) Dependency and historical batteries that define this acceptance surface.**
  - `uiteam` (20 entries): `ui_team_row` is the TEAM consumer's formatter and six-column clamp. T01 is the label
    precision itself.
  - `uiinvite` (32 entries): the separate invite name projection that W1 must leave alone. I07–I09 are the name
    lifecycle; I08 is the historical "falls back to `label_from_hash`'s `0x` spelling" refusal.
  - `uisend` (15 entries): `ui_route_recv_push`, the receive router through which `label_for_origin`'s label reaches
    `on_reply`, which P28d drives.
  - **Excluded:** `model` (239) — no entry touches the reply label, `copy_clamped(_reply_who…)` or `kLabelCap`. Also
    `b161hash`, `b251hash` and `grantpark` — they target `node_hashlocate.cpp`, but their entries mutate `park_send`,
    origin stamping and reverse lookup, never the name API.
- **Union = (a) ∪ (b) = {`uiteam`, `uiinvite`, `uisend`}**, 67 entries, all RED (§4 step 7). (a) is empty, so the union
  is (b).

## 7. PIN

`PIN re-synced? YES — 2950/195770 (the Slice 10 pin, reproduced by my pre-edit native binary) + 1 case / + 7 assertions (test_node_hashlocate "B241 peer_name_find — …", its 7 CHECKs) = 2951/195777, measured by the full native binary after the edit; the literal stays bare with one derivation comment line above it; tools discovery 356 OK; all 24 battery workers derived 2951/195777/0.`

## 8. Freeze inventory

| Item | SHA-256 / state |
| --- | --- |
| `src/firmware_ui.cpp` | `6fe8c435f0c3ada0a5355a2c4d8b934875c5d4e0cfd06f6fa1c90f07a4307e2f` |
| `tools/probe_firmware_ui/run.sh` | `cf18d721854f13264bdef9a0a223a8f85c9707cf6641088947c02c019c1c6a3e` |
| `tools/probe_firmware_ui/probe_main.cpp` | `887df0c299143570c9c119943f58dfa7f0e4398b881bc14c38a0b594e68cd324` |
| `test/test_node_hashlocate.cpp` | `198c0211715a82cf3e5d676d4e3f84ed8bf2f077c1eadc2041d75de34e5462ba` |
| `tools/probe_ui_model_mutations.py` | `f135d051b626909ce2dd6099af8db5a98c79565d16e7b8c7d9c5a782bfd99afa` |
| Authorized brief | `617c2be56cdf2db21981373aac3f8dd5a5baa09235d8b753f2a9210ccab5d018` (unchanged) |
| MeshRoute base | `4a230f4501c6a19d71b9234968bf1389484d8295` (HEAD unchanged; nothing staged or committed) |
| Simulator | `6585649ea5a780f0542b2931853a667be56a5b2b`, clean |
| Preparation set | All 10 unchanged, equal to the brief's §1 hashes (`stability-end.txt`) |
| Re-review-2 (explained addition) | report `f392e38e…1e94`, `SHA256SUMS` `ea70db54…8e0f` (unchanged) |
| New: this report | `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1.md` (its hash cannot be stated inside itself) |
| New: evidence directory | `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1/` — every file listed and hashed in its `SHA256SUMS` |

**Stability statement.** Every executable-input and preparation-set hash was the same at the freeze and after the last
gate step. The only additions after the last measurement run are this report and the evidence directory. They were
written after `final-2` and after the chain, so QA's board source snapshot will differ from mine only by them (§4.2).

**Locations outside the tracked tree** (none of them is durable storage):
- Raw measurement output: `.pio-measure/w1/{base-1,base-2,final-1,final-2}` (gitignored; ELFs, payloads, build logs,
  symbol/section dumps).
- Working logs and ledger: `.pio-measure/w1/logs/` and `.pio-measure/w1/ledger.md`.
- Simulator build and corpus streams: the session scratch directory
  `/tmp/claude-1001/-home-staszek-MeshRoute/7019fa4d-8324-4b18-b429-3c8de8a32587/scratchpad/gate/`. It is temporary,
  so recreate the streams if needed. The manifest carries every stream's full MD5 and SHA-256.

## 9. Not run, and why

- **QA's independent gate (§4.2):** QA's job, not the coder's.
- **Metal:** none. Brief §7 adds no metal row (M2); the existing UI-13 bench procedure shows the H1/H2 labels on glass.
- **Board envs beyond the ruled pair and the six-env census:** the two-env rule; the census is the stated exception.
- **`tools/probe_board_ui`:** not in the gate list. Its drift is B418, which is W2's. The D6 audit found none of its
  structural greps touching the edited lines.
- **The `model` battery and the `node_hashlocate.cpp` batteries:** outside selector (b), for the reasons in §6.
  - **Disclosure:** one `--target=model` run started by accident. I imported the harness module for a census, and its
    top-level code began the battery. I stopped it within about two minutes. It worked only in `/tmp` scratch copies,
    which I deleted. `git status` and `src/` were verified unchanged, and it gave no result. The census was then redone
    by a static AST read.
- **A standalone s18 run:** it is covered by the corpus runner (s18 is one of the 36 byte-identical streams).

## Findings for QA and the register (M1; none edited here — the register is fenced)

- **F-1 (pre-existing at `4a230f4`, not caused by W1): O8's second sed command is vacuous.**
  - The command is `s|            mem.name\[nn\] = .\\\\0.;|            ;|` (`run.sh`, control O8).
  - Inside the single-quoted script it matches **two** backslashes, and the source line has one.
  - So the mutant keeps `mem.name[nn] = '\0';` with `nn = 0`, and blanks every member name.
  - O8 goes RED (`P23d … the name column now reads Wolfga` and `P23e`) because names *vanish*. Its label promises "the
    truncated `0x` third spelling", which it does not produce.
  - Confirmed by replaying the sed; the independent reviewer found the same.
  - W1 leaves it exactly as it was, since fixing it would change an existing control's meaning, which is OUT.
  - Suggested repair in its own slice: `.\\0.`, then re-derive what O8 reddens.
- **F-2 (informational): four new P28 checks are reddened by no control.** They are:
  - `P28 precondition …`;
  - `P28a a NAMELESS cached key renders exactly 0x…`;
  - `P28a an UNKNOWN hash renders exactly 0x…`;
  - `P28a synthetic: capacity 0 writes nothing at all`.

  The fallback spelling and the capacity-0 guard are not mutated by B241a/B241b (the brief names exactly two new
  controls), so the roll-up lists them. QA may dispose.
- **F-3 (limit of the consumer checks, stated rather than implied):** P28b–P28d are regression coverage, not the
  deterministic proof.
  - The TEAM snapshot is zero-initialised, and the compose/receive labels are uninitialised stack locals.
  - On the pre-fix code, the named teammate's compose header and DELIVERED went RED (stack garbage after `H1`, the metal
    symptom; `raw/probe-red-prefix.log`), and the REPLY checks happened to pass.
  - The deterministic proof is P28a's poisoned, canary-fenced buffer, which both new controls redden.

## Native guard: RED proof

A global `cap - 1` change must fail the new native case, and it does. The case was shown to fail without touching
`lib/`:
- **Method:** the working tree was rsync'd to scratch (no `.git`/`.pio`, the mutation harness's own method), and only
  the scratch copy's `peer_name_find` got the "reserve and terminate inside the raw API" change
  ([diff](2026-09-24-standalone-mobile-home-w1/native-capm1-mutant.diff)).
- **Result:** the full native suite there ran **2,951 cases, 1 failed, 4 assertions failed**. The one failure is the
  B241 case: push `body_len` `31 == 32`, the raw read `31 == 32`, and both `memcmp`s.
- **Why it matters:** all 2,950 existing cases pass that change, which confirms the pre-check's warning. Summary:
  [`native-capm1-summary.txt`](2026-09-24-standalone-mobile-home-w1/native-capm1-summary.txt).

## Implementation record

- **RED → GREEN.** With the seam and P28 in place and the fix *not* yet applied, the probe failed deterministically on
  all three arms (`raw/probe-red-prefix.log`): 8 of P28a's checks plus the named teammate's compose checks. After the
  fix it was green (`raw/probe-green2-nonneg.log`).
- **Review.** A fresh read-only reviewer read the whole diff and found no Critical findings. I re-graded and fixed five
  items (all inside the fence) before the freeze:

  | # | Grade | Fix |
  | --- | --- | --- |
  | 1 | Important | An UNKNOWN-hash teammate (id 94, uncached `0xB2EE41EE`) added to the TEAM, compose-header and DELIVERED checks. Brief §2.3(2) names an unknown peer; `id 93` only exercised the bare-id arm. |
  | 2 | Important | The precondition now asks key presence and absence (`peer_key_find`), so nameless and unknown are different fixtures. |
  | 3 | — | One comment corrected (V1). |
  | 4 | — | `g_exec` reset in P28's restore, so P26 gets a fresh executor handle. |
  | 5 | — | `ui_wrapper` fails loud on a write error. |

  The reviewer's O8 note is F-1.
- **Rulings I made, each with its cost if wrong:**

  | Ruling | Reason | Cost if wrong |
  | --- | --- | --- |
  | P28 is placed before P26 | P26 must stay last because it leaves a FIRING alarm. P28 dismisses its own alarm and restores P18's canonical fixture. | None known; P26a's precondition passed. |
  | TEAM checks assert the marker + six-column label + separator (8 columns) and the 19-column width, not the route-age token | The age is clock-dependent and not W1's property. | Weaker than a whole-row match on the age columns only. |
  | B241b's replacement is spelled `out[cap - 1] = 0;` | A single-quoted sed script cannot carry `'\0'`. | None; the same byte is written. |
  | The native guard uses `CHECK`, not `REQUIRE` | The TU is `-fno-exceptions` (the file's own note). | None. |
  | The 32-byte name enters through `on_hash_bind_pubkey`'s appended `[name_len][name]` | It is the real public receive path, with no core hook. | None. |
  | Raw `.log` evidence stays gitignored per repository policy; figures are in tracked receipts and here | Repository policy. | QA/owner may prefer different packaging. |

**Next:** QA's independent `src`-only gate (§4.2) on this frozen tree. It covers native, the corpus, its own
`pair --jobs=1` compared field by field with `manifests/final-1/`, selector (a) (none) at its discretion, and the
firmware-UI probe with its controls. Everything is left uncommitted for the owner.
