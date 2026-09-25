<!-- Author: Codex, independent Quality Agent; Claude authored the brief; a separate coder implemented W1; the owner rules and commits -->
# Standalone Home W1 / B241 — independent QA

**PASS — 2026-09-25. B241 is closed in software.** The frozen implementation meets approved brief revision 3.
This verdict does not grant a hardware PASS or authorize the next package. No production, test, tool or simulator
source was changed by QA. Nothing was staged or committed.

## 1. Exact input and scope

- MeshRoute base: `4a230f4501c6a19d71b9234968bf1389484d8295`, with the complete uncommitted candidate and preparation set.
- Simulator: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean before and after.
- [Approved brief](../plans/2026-09-24-standalone-mobile-home-w1-label-termination.md), revision 3:
  `617c2be56cdf2db21981373aac3f8dd5a5baa09235d8b753f2a9210ccab5d018`.
  Its historical DRAFT header stays untouched; [approval](2026-09-24-standalone-mobile-home-w1-brief-rereview-2.md)
  holds the verdict.
- [Coder receipt](2026-09-24-standalone-mobile-home-w1.md):
  `a157a3b5bd1a71618dc5ca6b9e9eb9c7b4ba21b7ef5a16bb042e3e82d42de7f9`.
- Coder evidence `SHA256SUMS`: `66ff7a785bbc62fee03d2051f01ad6043a23860eb150685997d1dfc5b790d441`;
  all **64** entries, including local ignored logs, verified. The preparation inventory's **10** entries, previous
  evidence directories' **21** entries and approval evidence's **3** entries also verified.

| Frozen implementation input | SHA-256 |
| --- | --- |
| `src/firmware_ui.cpp` | `6fe8c435f0c3ada0a5355a2c4d8b934875c5d4e0cfd06f6fa1c90f07a4307e2f` |
| `tools/probe_firmware_ui/run.sh` | `cf18d721854f13264bdef9a0a223a8f85c9707cf6641088947c02c019c1c6a3e` |
| `tools/probe_firmware_ui/probe_main.cpp` | `887df0c299143570c9c119943f58dfa7f0e4398b881bc14c38a0b594e68cd324` |
| `test/test_node_hashlocate.cpp` | `198c0211715a82cf3e5d676d4e3f84ed8bf2f077c1eadc2041d75de34e5462ba` |
| `tools/probe_ui_model_mutations.py` | `f135d051b626909ce2dd6099af8db5a98c79565d16e7b8c7d9c5a782bfd99afa` |

QA read the complete fenced diff. The only production change is `label_from_hash` and its comment: capacity zero
returns before subtraction, named copies reserve one byte and terminate at the returned count, and the zero-count
fallback retains its exact spelling. The raw `peer_name_find`, its 32-byte push/persistence consumers, the invite
projection, every other production file, `Node`, NV and the wire are unchanged. No resident state is added.

The generated probe wrapper includes the supplied live or mutant source path. All three live configurations and
controls compile through it. The direct checks use a poisoned 15-byte destination and adjacent canaries; TEAM,
compose header/result and incoming same-team alarm reply checks use the real UI entry points. The receive fixture
actually fires and submits the alarm. P28 restores its fixtures before P26. The native case guards all 32 payload
bytes through the public key-answer/push path and an exact-capacity lookup.

## 2. Independently executed gate (§4.2)

| Instrument | QA result |
| --- | --- |
| `pio test -e native`, then `./.pio/build/native/program` | Both exit 0; actual binary **2951 cases / 195777 assertions / 0 failed / 0 skipped** |
| Fresh Release simulator build, stock `tools/run_corpus.py --require-anchors` | **36/36**, every stream identical to both pre-check and coder manifests by full MD5, SHA-256, bytes and events; zero assertion failures; inputs stable |
| s18, as part of that corpus | **269517 events**; MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**; SHA-256 `27ecfa988f062d3b27ed9b19159965ac393a0f03e2499011d87d9797fe64cc54` |
| Stock deterministic board pair, sequential | **PASS**, gateway then heltec_mobile; every `measurements.*` field and both payload hashes exactly match coder `final-1` |
| Stock firmware-UI probe with default controls | **l2 467/467; v3 902/902; BLE-row 467/467**; **225 verified / 0 unusable** |
| Required control semantics | **224 assertion-RED controls plus C0's required compile failure**, not 225 runtime failures; B241a **10**, B241b **5** failed checks |
| Touched native mutation selector (a) | **Empty**, independently derived from all **105** `TARGET_SRC` mappings by AST, without importing/executing the harness |
| Frozen inputs and whitespace | **PASS**: 1222 MeshRoute and 285 simulator listed paths unchanged across the chain; symlink targets checked; `git diff --check` clean; index empty |

The native invocation used the existing build directory; the actual binary was independently run. The simulator
build directory was fresh. Corpus anchors were read from the current canonical `### 36/36 corpus` block through
the stock parser, not from historical prose elsewhere in `simulation/BASELINE.md`.

Commands and locations:

```text
pio test -e native
./.pio/build/native/program
cmake -S /home/staszek/lora-universal-simulator -B /tmp/meshroute-w1-qa-20260925-s2s8uk5l/sim-build -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=/home/staszek/MeshRoute
cmake --build /tmp/meshroute-w1-qa-20260925-s2s8uk5l/sim-build --target lus -j 4
python3 -B tools/run_corpus.py --out /tmp/meshroute-w1-qa-20260925-s2s8uk5l/corpus --jobs 4 --lus /tmp/meshroute-w1-qa-20260925-s2s8uk5l/sim-build/orchestrator/lus --require-anchors
python3 -B tools/measure_board.py pair --jobs=1 --output .pio-measure/w1/qa-20260925-s2s8uk5l
bash tools/probe_firmware_ui/run.sh
```

The probe used a private external `TMPDIR`. QA copied its freshly built, immutable support archives and harness
objects before the stock runner removed its temporary directory; the focused replays below reuse those QA objects,
not coder build products. No native/probe/mutation run overlapped the board measurement; no document was edited
until the complete gate and input-preservation check had passed.

**PIN re-synced? YES — 2950/195770 + 1 case / + 7 assertions = 2951/195777.** The literal and its derivation comment
match the executed binary; no mutation entries changed.

## 3. Board attribution and the failed first attempt

| Board | RAM | Flash | Delta from coder's verified baseline | Payload SHA-256 |
| --- | ---: | ---: | --- | --- |
| gateway | 203740 | 572240 | RAM 0; flash 0; image identical | `cd56846e3272db109eff6f77d54c5ea97a05072c679976a4357188c1a35ae9c1` |
| heltec_mobile | 211724 | 1394732 | RAM 0; flash **+28 B** | `ae61574884fd6cd913768f32408f9fbf6ed9234ec71d3d7d313ce937de211ed2` |

QA's manifests match coder `final-1` in measurements, ELF and payload metadata/hashes, toolchain, fixed identity,
paths, host, concurrency and schema. The archived base pair and final pair manifests each match one another.
QA did not rebuild the pre-fix board tree or rerun those four historical repeatability comparisons.

The source difference is fully accounted for: removing only the **49** post-measurement coder receipt/evidence files
from QA's inventory reconstructs coder `final-1`'s **1173-file source digest and Git-status digest exactly**.
QA's measured source digest is `5ea7a70fcda5a7937be365c6d0e4ea2d20621b1e2ec6421496cdc5a1da34cbf5`.
Ordinary `.pio` metadata differs after QA's native invocation, as expected; board builds use their separate fixed
paths. This comparison is field-by-field, not misuse of stock `compare` across different source snapshots.

QA re-derived all mobile symbol differences from hashed baseline and QA symbol inventories: **12** changed sizes,
all defined in `firmware_ui.cpp.o`; `label_from_hash` grows **34 → 50 B**. The other eleven unchanged-source functions
sum to **+10 B**, giving **+26 B** in symbols and **+28 B** in `.flash.text`; **2 B** remain section alignment/padding.
No other loadable section grows. Linker relaxation/layout explains the object-to-linked size changes consistently;
QA did not independently compile a pre-fix object to isolate each relaxation decision. Payload file size grows
**32 B**, which is distinct from linked flash growth. Gateway matches the baseline symbol inventory and image.

**Failed attempt, retained:** the brief permits external measurement output, and `validate_output_dir` accepts it.
QA's first stock invocation with `--output /tmp/meshroute-w1-qa-20260925-s2s8uk5l/boards` nevertheless exited **2**
after **16.013 s**: `tools/git_rev.py::_write_measurement_compiler_state` raises
`MESHROUTE_MEASURE_COMPILER_STATE must be below the repository .pio-measure/`.
This reproduces existing **B281/B315**, not a new W1 defect. No figures from that attempt were accepted. The stock
rerun at the supported ignored path exited 0 in **44.854 s**. Neither tool was modified. Future recipes must use
`.pio-measure/` until that separately scoped defect is resolved; the approved historical brief stays hash-identical.

## 4. Controls, findings and dispositions

**B241 proof:** focused replays of the exact maintained B241a/B241b sed scripts both compile, exit **1**, and fail the
`P28a H1 lands exactly, NUL at byte 2, both canaries intact` check plus their other recorded checks (**10 / 5**).
These are assertion failures on the intended poisoned buffers, not crashes or unrelated REDs.

**Existing controls retained:** QA replayed N4, N9, O2, O6 and O8 against both committed base source/probe and frozen
source/probe. Each script is unchanged; each pre/post failed-check list is identical: **8, 8, 1, 1, 2** respectively.

**New register entry B449 — OPEN / LOW / TOOL, pre-existing:** O8 does not generate its advertised truncated `0x`
fallback. Its first substitution matches once and its second matches **zero** times, in both base and candidate.
The overescaped second pattern leaves `mem.name[nn] = '\0'` in place after `nn = 0`; the mutant blanks names. It goes
RED on two name-display checks, not on the claimed fallback property. W1 preserves the actual existing mutation and
does not create this defect; the brief fences changes to existing control meanings out. Repair O8 separately with
an exact-match proof and a control that fails on the intended fallback. This PASS does **not** certify O8's label.

**Four uncovered P28 checks:** the stock roll-up is **733/874**, and lists the fixture precondition, two fallback
checks and synthetic capacity-zero check as not reddened by its maintained controls. The fixture precondition is
intentionally outside mutations of the production UI file. For the other three, QA added **temporary, labelled
synthetic faults outside the checkout**: changing the adapter's fallback to `0y` fails both fallback checks (12
failures overall), and writing one byte on the capacity-zero path fails exactly that check (1 failure). Both compile
and exit 1. This proves the assertions can fail; it does **not** increase the maintained runner's coverage or add
permanent controls. The brief requires exactly the two B241 controls, both satisfied; this residual is non-blocking.

**Consumer limitation accepted:** zero-initialized TEAM snapshots and incidental compose/reply stack contents are
not a deterministic missing-terminator proof. The poisoned-buffer checks supply that proof; consumer checks verify
the required integration paths and their existing formatting. No claim that all consumer checks fail pre-fix.

## 5. Attribution limits and evidence

Per P6 and brief §4.2, QA did **not** rerun the coder-only six-environment warning census, optional dependency
mutation union (`uiteam` 20, `uiinvite` 32, `uisend` 15), or tools discovery (356). Their hashed logs were inspected;
their results remain attributed to the coder. QA independently verified selector (a) is empty and accepts selector
(b)'s scope: those three batteries exercise label consumers; the model/core batteries omitted by the coder do not
mutate the changed adapter or raw-name path. The separate raw-API cap-minus-one native fault run likewise remains
coder evidence, not a claimed QA rerun. No metal test, new ABI measurement or `probe_board_ui` gate was run; the latter
belongs to W2/B418. No new metal row is required: the regression is deterministic in the firmware-UI probe, while
the existing UI-13 glass check remains the owner's.

Compact receipts, per-board manifests, focused proof driver and exact failed-check lists:
[`2026-09-25-standalone-mobile-home-w1-qa/`](2026-09-25-standalone-mobile-home-w1-qa/), covered by its `SHA256SUMS`.
Raw logs, source inventory, generated fault sources, corpus manifest and board diagnostics are retained locally in
ignored `artifacts/2026-09-25-standalone-mobile-home-w1-qa/`; its `SHA256SUMS` is
`bd00928c631bea2dbf0007870e99905f4c7fbb84c387a6134b0c0e45c9a94be1`.
The successful board ELFs/images remain at `.pio-measure/w1/qa-20260925-s2s8uk5l/`; fresh simulator streams and build
remain at `/tmp/meshroute-w1-qa-20260925-s2s8uk5l/`. These local paths are evidence locations, not off-machine backups.

Three QA diagnostic scripts needed correction before producing receipts: a corpus pretty-printer assumed a table
where the baseline has a fenced block; an inventory display assumed a dict where the capture is a list; source-digest
reconstruction initially treated dangling symlinks as ordinary files. Corrected parsing and symlink-target hashing
produced the retained receipts. None changes an instrument result or repository input.

**Landing:** close B241 in place; add the measured B449 row and B315 reproduction; update the design's W1 status and
tracker/MEMORY pointers. Preserve the brief, coder receipt, prior evidence and all unrelated work. W1c or W0 can
follow the owner's package order through their own pre-check and approved brief; no commit is a progress gate.
