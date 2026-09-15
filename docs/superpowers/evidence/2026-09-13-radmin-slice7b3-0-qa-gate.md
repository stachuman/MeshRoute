<!-- Independent QA gate: Claude (second reader / QA-gate role); coder: Codex; brief author: Codex QA/Author session -->
# Slice 7b-3-0 — independent QA gate — 2026-09-15

**Verdict: INDEPENDENT QA PASS.** The frozen 7b-3-0 implementation (typed TERMINAL `action_busy` = `0x08`,
R-RA-37) reproduces every figure in the coder receipt
[`2026-09-13-radmin-slice7b3-0.md`](2026-09-13-radmin-slice7b3-0.md) when re-executed by QA from the shared
frozen tree and a separate staged copy. **B391 is closed** by this gate. Nothing is committed; the owner commits
this codec preparation separately, after which the 7b-3 behaviour brief is reissued at that hash (R-RA-37).

## 1. Frozen state gated

| Item | Value |
| --- | --- |
| MeshRoute base | `b9d75aaca55a0e350d9707512e9b6eec8a81ab22` (`7b-3 prep`), 15 dirty paths at gate start |
| Simulator | `06746a97de5764415d6fcef10b97bca90569b9c7`, clean, unchanged |
| `lib/core/remote_codec.h` | SHA-256 `dcd8d09c8e223dee…` = coder `freeze.json` |
| `lib/core/remote_codec.cpp` | `afc81d9205ba0e1d…` = coder `freeze.json` |
| `test/test_remote_codec.cpp` | `1e5d61080edf4c0a…` = coder `freeze.json` |
| `tools/probe_ui_model_mutations.py` | `08c7471b91db70db…` = coder `freeze.json` |

Production diff read in full: `RemoteTerminal::action_busy = 0x08`, `kRemoteTerminalMax` 0x07 → 0x08, the
static assertion re-bound to `action_busy`, the decoder comment `0x08..` → `0x09..`, and header comments made
count-neutral. No other production path changed (the diff touches only the two codec files). Tests: the old
`kRefBody_resp_terminal_auth_code08` literal becomes the ninth positive; five new independently frozen literals
(`auth_code09`, `open_code08`, `open_code09`, `auth_code08_detail`, `open_code08_detail`); the open negative
retargets to `0x09`; three new cases (allocation/public wire, all-256-bytes per domain, failure publication).
Tool: R58/R60 descriptions, R60 pattern made code-based (D6), new R67/R68 controls, PIN re-synced, X09
pattern made code-based (B391).

## 2. Instruments QA executed and their results

Chain (`2026-09-13-radmin-slice7b3-0-qa/chain.sh`, log `chain.log`) ran in the frozen shared tree; the
mutation union (`mut.sh`, log `mut.log`) ran from an rsync-staged copy with three workers, concurrently.

| Instrument | QA result | Coder receipt |
| --- | --- | --- |
| `pio test -e native` then `./.pio/build/native/program` | **2912 cases / 184461 assertions / 0 failed / 0 skipped** | same |
| Extended reference `--freeze-check --compare --selftest` (PyNaCl env) | frozen inputs match; **94/94** strict; **89/89** old identity; 5 comparator controls RED (changed/missing/extra/duplicate/mis-sized) | same |
| Real-codec domain proof (`result-domain-proof.cpp` relinked against final native archives) | `--allocated` **9787 checks PASS**: terminal auth/open **9 / 247**, auth protocol-error 1/255, open protocol-error 256/0; default mode exits 1 at check 2 (stale-ceiling control, expected) | same |
| Simulator rebuild (`cmake --build`, both variants) | 38 actions, 2 `remote_codec` compilations; `lus` md5 `1c1a7435` → `34140fc1` | executable changed as predicted |
| Corpus `run_corpus.py --require-anchors` | **36/36 streams validated, 36/36 anchors reproduce `simulation/BASELINE.md`**; s18 **`32afbf11` / 269517 / 0**; zero streams contain a `radmin` event | same |
| `probe_board_abi.py` | Node **230896/8 native, 117912/8 mobile, 157264/8 gateway**; 191 checks, 9/9 controls RED | same |
| `probe_b278_row_abi.py` | 42 measurements, 6/6 controls RED | same |
| console-sink | 6 profiles, 720 checks, structural 83, BLE guard 905, 149 controls, 0 unusable; `--no-neg` PASS | same |
| inbox-verbs | ACCEPT 1363 checks / 60 controls; CLIENT 384 / 45; `MR_PROBE_ARM=client` 384 / 45; `--no-neg` PASS | same |
| firmware-UI | 223 controls, 0 unusable, coverage 703/840; `--no-neg` prints bare PASS (B350, disclosed) | same |
| custody-USB | 27 checks, 10 controls; BLE-line 40 checks, 8 controls; features 9 cells / 120 checks / 59 controls + 40 ownership; all `--no-neg` PASS | same |
| tools unittest discovery (real ELF present) | **343 tests OK, 0 skipped** | same |
| inventory `--write` / bare / `--check` | byte-identical, **204 rows** | same |
| authority checker + selftest; A0; DataType literals | PASS; 6 selftest controls RED; A0 rc 0; literals rc 0 | same |
| `git diff --check` both repos | clean | same |
| warning census, six pinned environments | 173/178/177/177/182/182 = pins, zero `-Wswitch`; RAM/flash per cell identical to the 7b-2 base | same |
| deterministic board pair, gateway then heltec_mobile | gateway **RAM 203956 / flash 570588 / 285 objects**; mobile **RAM 207756 / flash 1372992 / 329 objects** — identical to the 7b-2 base | same |

## 3. Mutation union S ∪ H

S = {radmin2codec} (changed TARGET_SRC); H = the 52-battery floor frozen in
`2026-09-13-radmin-slice7b3-precheck/historical-mutation-floor.json` (49 from 7b-2 + remoteactivation,
fwactivation, macwait). QA ran all **52** batteries:

| Total | QA measured |
| --- | --- |
| Configured patterns | **817** (815 floor + R67 + R68) |
| RED | **816** |
| Unusable | **1** — `sliceBmac` M04, the known B342 exception, visibly separate |
| VACUOUS / zero-match / mismatched battery | **0** |
| Worker clean baselines | every worker **2912 / 184461 / 0** |
| Staged fenced sources after the union | byte-identical to before (`mut_stage_hashes*.txt`) |

Per-battery counts are in `2026-09-13-radmin-slice7b3-0-qa/union/summary.json`; every battery equals its
configured count. Codec battery **105 RED / 0 unusable** (R67 stale ceiling: compiles, fails 77 assertions;
R68 misreport as `session_busy`: fails 18; R60 code-based: fails 510). Full logs for all 52 batteries are in
`union/all-52-battery-logs.tar.gz`; `radmin2codec`, `radmin5rx` and `sliceBmac` are also kept uncompressed.

## 4. B391 independently verified

`radmin5rx` battery **14 RED / 0 unusable**. X09 (expiry-arm deletion) **compiles, matches exactly once, fails 1
assertion → RED** on a 2912/184461/0 baseline (`union/mut_radmin5rx.log` line 18). The production file
`lib/core/node_mac_rx.cpp` is untouched by the repair; only the harness pattern changed. Full tools discovery
(343, zero skips) passed with the runner edit. **B391 CLOSED.**

## 5. Not independently reproduced (D3)

- The coder's byte-level ELF attribution (one changed `.text` byte at gateway `0x85992`, `cmp r2,#7` →
  `#8`; mobile debug-info only). QA reproduced the section/RAM/flash/object totals on both boards and hashed its
  own final ELFs (`gateway 584829bf…`, `heltec_mobile 5149d76a…`) but did not build a base ELF for a byte diff.
  The coder's pristine base/final ELFs are retained in its archive.
- Board links were QA's own fresh builds; the coder's `.pio-measure` outputs were not reused.

## 6. Verdict and landings

**PASS.** Landed by QA: this file and its log directory; register B391 → CLOSED, §0 dispatch rewritten;
design §19.1 row 7b-3-0 → PASS and §8.9 `0x08` row/sentence updated; brief 7b-3-0 status + §8 PASS block;
repo `MEMORY.md` dispatch rewritten in place; ledger completion note under R-RA-37/39. Next free finding
remains **B394**. **Next:** owner commits the codec preparation (plus `git add` for the untracked brief/pre-check/
evidence paths) → the 7b-3 behaviour brief is reissued at that hash with the B389 typed-plan enumeration
folded in (12 rows preparable now, 36 rows R-RA-39 refusal candidates pending validate/apply splits) → owner
rules the B389 allocation.
