<!-- Independent QA gate: Claude (QA-gate role); coder: Codex; brief author: Codex QA/Author session -->
# Slice 7b-3-P1 — independent QA gate — 2026-09-16

**Verdict: INDEPENDENT QA PASS.** The P1 simple-action preparation refactor, frozen by the coder and since
committed by the owner as **`7442e6f`** (`P1`), reproduces every figure in the coder receipt
[`2026-09-15-radmin-slice7b3-p1.md`](2026-09-15-radmin-slice7b3-p1.md) when re-executed by QA, and passes an
independent local-equivalence proof against the pristine pre-P1 handlers. **B394, B399 and B400 close.** One
new LOW finding (B401, a displaced include comment) is registered and does not affect behaviour.

## 1. State gated

| Item | Value |
| --- | --- |
| MeshRoute | owner commit `7442e6f570abdcd74ceed20d4c0cb9e2855d0719`, clean tree (the coder froze at `c591721` + inventory; the commit matches that freeze: 1501 of 1504 inventory entries hash-identical, the 3 remaining are the simulator symlink/resolved-path entries) |
| Simulator | `06746a97de5764415d6fcef10b97bca90569b9c7`, clean; `lus` md5 `34140fc1` before and after a rebuild with **0 build actions** — P1 is simulator-inert by construction (no `lib/core` change) |
| Production diff read in full | `src/firmware_action_effects.h` (new, pure: kinds/backends/admission/observer), `firmware_commands.cpp` (factory-reset + sleep through typed effects), `fw_main.cpp` (reboot/OTA/crash/prep effects + `action_build_support()` in the board TU), `device_ota.{h,cpp}` (`ota_start(Print&)`, no-arg wrapper kept). No `lib/core`, `platformio.ini`, codec, NV, wire, authority or remote-guard change. `fw_context.h` wrappers unchanged. |

Refactor review (C1): every local handler keeps its grammar, output bytes, sink, warning order and effect
order. Factory reset: skip spaces, exactly `confirm` (now the shared `parse_confirm_token`, expression-identical),
both inbox wipes, inbox warning, NV erase, NV warning, `> rebooting` to `mrcon`, flush, delay, reset mark, reset.
Sleep: `off` prefix else on, flag written and message printed even on a build without power saving (support is
reported in the typed result only). Crashtest: debug gate first, then hang/fault/reboot prefixes, unsupported
fault still prints its two original lines. OTA: local toggle preserved (active → stop), entry idempotent, hook
installed before the message. Prep-restart: body unchanged, halt order unchanged.

## 2. Instruments QA executed

Chain (`…-p1-qa/chain.sh`) in the committed tree; union (`mut.sh`) from an rsync-staged copy, three workers;
equivalence (`equiv2.sh`) with the pristine `c591721` handler files extracted by QA.

| Instrument | QA result | Coder receipt |
| --- | --- | --- |
| Native, wrapper then binary | **2916 / 184587 / 0 / 0 skipped** | same |
| Extended reference | 94/94, old 89 identical, 5 comparator controls RED | same |
| Corpus `--require-anchors` | **36/36 validated, 36/36 anchors**, s18 `32afbf11` / 269517 / 0, zero radmin events | same |
| ABI probes | Node **230896 / 117912 / 157264 unchanged**; 191 checks 9/9 RED; B278 42 measurements 6/6 RED | same |
| console-sink | 6 profiles, 720 checks, structural 83, BLE guard 905, 149 controls, 0 unusable; `--no-neg` PASS | same |
| inbox-verbs | ACCEPT 1363 / 60 controls; CLIENT 384 / 45; explicit CLIENT arm 384 / 45; `--no-neg` PASS | same |
| firmware-UI | 223 controls 0 unusable, coverage 703/840; `--no-neg` bare PASS (B350) | same |
| custody-USB / BLE-line / features | 27 / 10 · 40 / 8 · 9 cells 120 checks 59 controls + 40 ownership; all `--no-neg` PASS | same |
| **deferred-actions probe (new)** | b0-p1 **150**, b1-p1 **151**, b2-p1 **158**, b2-p0 **158** checks, 39 transcripts each; **19 controls compile and fail an executed assertion**; `--no-neg` PASS | same |
| tools unittest discovery | **351 tests OK, 0 skipped** (343 + 8 generator tests) | same |
| inventory write / bare / check | byte-identical, **204 rows** | same |
| authority + selftests; A0; DataType literals; whitespace both repos | PASS; 6 selftests RED; rc 0; rc 0; clean | same |
| warning census, six envs | 173/178/177/177/182/182 at pins, zero `-Wswitch`; RAM identical per cell | same |
| board pair, gateway then heltec_mobile | gateway **RAM 203956 (0) / flash 568220 (−2368) / 285 objects**; mobile **RAM 207756 (0) / flash 1373576 (+584) / 329 objects** | same |

## 3. Independent local-equivalence proof (the C1 core)

QA extracted `src/fw_main.cpp`, `src/firmware_commands.cpp` and `src/device_ota.cpp` from `c591721`
(SHA-256s in `equivalence/pristine-c591721-source-sha256.txt`), ran the probe in `--baseline-source` mode
(114 / 114 / 117 / 117 checks, 39 transcripts per variant) and in positive-only mode on the final sources, then
compared every `TRANSCRIPT` line file by file:

| Variant | Baseline lines | Final lines | Result |
| --- | ---: | ---: | --- |
| b0-p1 absent backend | 39 | 39 | IDENTICAL |
| b1-p1 nRF | 39 | 39 | IDENTICAL |
| b2-p1 ESP | 39 | 39 | IDENTICAL |
| b2-p0 ESP, power saving compiled out | 39 | 39 | IDENTICAL |

**156 / 156 transcript lines byte-identical** (`equivalence/{baseline,final}/`). These cover confirmation
refusals, the eight inbox/NV failure combinations and warning order, sleep tails, prep-restart's clear → two
wipes → halt → output order, crashtest debug refusal/usage/prefixes, reset marks and flushes, OTA toggle,
failure and idempotent entry, and the escaped non-returning operations.

## 4. Mutation union S ∪ H

S = {actionadmit}; H = the 52 historical batteries (including `sliceDtoken`, whose confirmation-token predicate
P1 now reuses, and `radmin5rx` with the B391 X09 repair). **53 batteries / 827 configured / 826 RED / 1 known
unusable (sliceBmac M04, B342) / 0 vacuous**; every worker baseline **2916 / 184587 / 0**; staged fenced
sources restored byte-identical. `actionadmit` 10/10 RED, including A05 "compiled-out sleep reported
supported" — the control for the build-level-support fold-in. X09 again RED with one match.
Per-battery counts: `union/summary.json`; all logs: `union/all-53-battery-logs.tar.gz`.

## 5. Findings

- **B399** (warning-order wording) and **B400** (inventory generator source map) — folded by the coder,
  independently verified here (transcripts identical; 351 tools tests; 204 rows byte-identical) → **CLOSED**.
- **B394** (shared simple-action preparation) — implemented, gated, zero resident state, remote guards still
  refusing → **CLOSED**.
- **B401 (new, LOW, comment only):** `src/fw_main.cpp:43-44` — the `// §cleanup 2026-07-14: extern decls of
  the shared device-stack/runtime globals…` comment that belonged to `#include "fw_context.h"` now trails the
  new `#include "firmware_action_effects.h"` line. No behaviour effect. Close by moving it back in the 7b-3
  behaviour slice (its fence already includes `fw_main.cpp`), re-running the fw_main source readers (D6).

## 6. Not independently reproduced (D3)

- The coder's symbol-level flash attribution (gateway −2368 from inlining changes in `SegmentedInboxStore`,
  mobile +584) and its `-fstack-usage` per-function frames. QA reproduced the section/object/RAM/flash totals
  and the six-cell census on its own builds; the coder's pristine ELFs and `.su` files are retained in its archive.

## 7. Verdict and landings

**PASS.** QA landed: this file + `…-p1-qa/`; register B394/B399/B400 closed, B401 registered (next free
**B402**), §0 rewritten; design §19.1 rows 7b-3-P1 → PASS and 7b-3 → HOLD lifted; P1 brief §6 PASS block;
repo `MEMORY.md` rewritten in place; ledger note. No bench change: P1 adds no metal behaviour. **Next:** the
7b-3 behaviour brief refresh (revision 5) at `7442e6f`, replacing the "future P1 API" rows with the real
`firmware_action_effects.h` names, then behaviour implementation under R-RA-37/38/39/40.
