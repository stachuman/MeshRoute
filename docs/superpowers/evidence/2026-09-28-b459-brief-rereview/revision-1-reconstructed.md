<!-- Author: Claude (brief author); QA: Codex (pre-check, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# B459 — completeness accounting for the board-UI probe, with B461 (tool-only, two increments)

**Revision 1 — 2026-09-28 — draft for QA review.**

- **Base:** owner commit **`8079e54`** (`8079e545fb2e1065c2bbc347eb9a66160f6cbf8e`) **plus the uncommitted, QA-passed
  W2 candidate** — four tool files, W2's evidence and QA's W2 landing
  ([W2 QA receipt](../evidence/2026-09-28-standalone-mobile-home-w2-qa.md)). This is not HEAD alone: the B459
  pre-check's `inputs.json` is the authority for the whole tree (§1). The simulator is at **`6585649`**
  (`6585649ea5a780f0542b2931853a667be56a5b2b`), clean and read-only.
- **Freeze:** the coder pins this brief by the hash QA issues when it passes review. Under P4 the brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** `/home/staszek/MeshRoute` on `main`, never a worktree. Preserve every tracked and untracked file,
  including the W2 candidate: `tools/probe_board_ui/run.sh` already carries W2's edits, and B459 edits on top of them.
- **Authorities:**
  - the [B459 QA pre-check](../evidence/2026-09-28-b459-precheck.md) — cited as "pre-check Qn";
  - the [register](../../2026-07-30-open-bug-register.md): B459 and B461 in scope; B460 and B462–B473 out of scope;
  - B456's repair as the precedent: `tools/probe_firmware_ui/run.sh` and `tools/test_probe_firmware_ui.py`;
  - the process rules: `AGENTS.md` / `CLAUDE.md` (C1, U1, P4–P7, D1–D6, M1–M2) and
    [agent roles](../../2026-09-02-agent-roles.md).

## 0. What this is

**The defect (B459).** `tools/probe_board_ui/run.sh` counts results in six layers and reconciles none of them against
what should have run. It succeeds when a check silently disappears. Measured by the pre-check on copies (Q2) — the
negctl case in the default run, the others in `--no-neg`, which runs those layers the same way:
- a false guard, an undefined guard (exit 127) or an early return before W1 → **exit 0**, wiring 59/59 instead of
  60/60;
- a negctl loop shortened to skip one control per arm → **exit 0**, and its summary still claims 60 + 3 controls;
- one canvas `CHK` deleted → **exit 0**, with V3 123 and V4 109.

**Owner rulings, 2026-09-28:** B459 is its own tool package, right after W2 and before W6. **B461** — the W50
comment's stale "Control (c)" — folds into it.

**Two increments, each separately frozen and gated (C1, pre-check Q4):**
1. **Increment 1 — a refactor.** Move the generic half of B456's accounting — the recorder, the guard ledger and the
   final set comparison — out of the firmware-UI runner into a shared, sourced `tools/probe_accounting.sh`. The
   firmware-UI probe's behaviour and output stay identical.
2. **Increment 2 — the fix.** The board-UI probe records one observation per identity in every layer and compares
   them, through the shared comparator, against a **checked-in identity manifest**. It gains a static declaration
   census, explicit mode banners, a synthetic self-test mode and its own regression suite. B461's one-token comment
   correction rides here.

**After B459,** the board-UI probe's default run proves its own completeness, so future UI gates no longer
reconcile it by hand. Any future change to its check set shows up as a manifest edit.

**Not in B459:**
- **B460** (the `whoami` radix pin);
- **B462–B472** — the eleven other runners from the census; none is migrated to the shared comparator here;
- **B473** — crashed board-UI mutants counted as RED. No control contract is tightened here (pre-check Q2);
- any product, test, simulator or shared-fake change;
- the other W2 files, the mutation harness and the command inventory.

## 1. Verified seams and pinned inputs (pin by symbol; lines are hints from the pre-check inventory)

The per-seam facts are in pre-check Q2–Q8; the brief relies on them.

| Seam | Fact at the base | B459 change |
| --- | --- | --- |
| Firmware-UI runner: `record_verdict`, `record_guard_failure`, `expected_controls`, `account_controls`, `controls_final`, `--selftest-accounting` (`:82–151`); `ctl` (`:501–549`); the final tail (`:2030–2040`) | B456's accounting. `account_controls` mixes generic parts (the empty-set check, the awk comparison, the guard ledger) with firmware-UI's own (the `ctl` extractor's call-count agreement, the `--no-neg` policy) | Increment 1 moves only the generic parts (§2.1) |
| `tools/test_probe_firmware_ui.py` — 19 cases; `ACCOUNTING_STATEMENT` (`:31`); the bypass regression (`:152–164`) | The bypass case copies `run.sh` **alone** into a temporary directory and removes the exact final-accounting statement there | A sourced library must still resolve from that copy (§2.1) |
| Board-UI canvas: launches (`:57–84`); `probe_main.cpp` `CHK` (`:111`) and tally (`:654–659`) | `CHK` counts; only failures print labels; `P4a` runs ten times under one label (`:173`). The runner checks the exit status, never totals or identities | §2.2 |
| Traits `trait_control` (`:85–107`, calls `:109–122`); missing traits (`:129–153`) | Counter-based; the missing-trait loop's list is also its only declaration | §2.2 |
| Structural `schk` (`:165–168`), loops S2/S3/S6 (`:185–233`) | Counter-based; some labels embed measured values; S1's build failure increments the counter directly | §2.2 |
| Wiring `wchk_in` (`:261–276`) | A live failure returns before the controls; sed's exit status is unchecked; a predicate's exit status is only "zero or not" | §2.2 |
| `negctl.py` `MUT_V3` / `MUT_V4`, loop (`:337–375`) | Accepts a compile failure or failed checks; prints `len(MUT)` as the number of controls run | §2.2 |
| Modes | Default ends `exit $rc` with no final line; `--no-neg` skips only negctl and prints no "not a gate" line | §2.2.6 |
| W50 comment (`run.sh :~1291`) | "Control (c)" names the wrapper-removal control, which is control **2** (B461) | §2.3 |

**Executable inputs.** Each hash is the working-tree SHA-256 at the base.

| File | SHA-256 | Lines | Increment |
| --- | --- | ---: | --- |
| `tools/probe_accounting.sh` | **new file** | — | 1 |
| `tools/probe_firmware_ui/run.sh` | `60478a2a541e6a6959f69312f096a5c011ae3ede99b96dda29ff8d0b1d7b5bdc` | 2040 | 1 |
| `tools/test_probe_firmware_ui.py` | `50e517ed19079893d3b39fd0b795b581e528a68d5b494daabd901b132f122b16` | 178 | 1 — the library resolution only |
| `tools/probe_board_ui/run.sh` | `0ee0bc90df8d8a6e1d5adce73958f2dd32061388c0f3644f70d9e1f88ce56fe8` | 1474 | 2 (W2 candidate) |
| `tools/probe_board_ui/probe_main.cpp` | `6cffd422588280136e09b05a1bdf9b602420336c7fba76315953a13d23af07b8` | 659 | 2 — records and `P4a`'s iterations only |
| `tools/probe_board_ui/negctl.py` | `067e1e1b2e9bc464a4e4677b63c0058038a83568a4f801dd48192ba95917f76f` | 375 | 2 — records and completion only |
| `tools/probe_board_ui/expected.tsv` | **new file** — the identity manifest | — | 2 |
| `tools/probe_board_ui/accounting.py` | **new file**, optional — the static census | — | 2 |
| `tools/test_probe_board_ui.py` | **new file** — the regression suite | — | 2 |

All four new paths are absent at authoring.

**Read-only dependencies.** Every other path in the pre-check's `recommended-inputs.json` stays at its recorded hash
(its four documents are the preparation set below). Among them are the W2 candidate's other three files:
- `tools/probe_board_ui/fakes/Arduino.h` — `deaa9f4a…b853`;
- `tools/probe_inbox_verbs/transcript_main.cpp` — `3914de10…8163`;
- `tools/probe_ui_model_mutations.py` — `81842cc5…781a`.

**Base inventory.** The pre-check's `inputs.json` (1,843 MeshRoute paths, covered by its `SHA256SUMS`) is the
authority for every other path. Since QA wrote it, only the register (B462–B472 at the pre-check, then the author's
edits) and the tracker and `MEMORY.md` pointers have changed. New paths since then: the pre-check report and folder,
this brief, and QA's review artefacts.

**Permitted preparation set.** These uncommitted documents are preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| `docs/2026-07-30-open-bug-register.md` | authority (B459, B461 with the owner's fold ruling, B473; §0) | `86837517b071729925d5b9bbb67e68cce8f6eed64209ec8e74dc523b7de4d3ed` |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | context (§13; unchanged since the pre-check) | `dd64b400cab260cfc7b9edd07c7739a88e71665a1efb0ca8b485f99d83d137a4` |
| `docs/superpowers/evidence/2026-09-28-b459-precheck.md` | authority (pre-check) | `98d96dc538288145f4ea88b60a0271d02a53dfbeae8d0f8e01edc2c11734c710` |
| `docs/superpowers/evidence/2026-09-28-b459-precheck/SHA256SUMS` | evidence — 19 entries, including `inputs.json` (`22d1b731…051f`) and `recommended-inputs.json` (`f8c246b2…81d0`) | `a0eebbdd65cb494e3bc77f8c8e9d9008e919f44c7412c03709db9bddcb26265f` |
| `tracker.md`, `MEMORY.md` | context pointers | `ef73543d1f749b529a81751bc391613a6222b9e80f7d584685fa848e5a788276`, `a55e27aa416a7ed64a694a8ddbf222a34fcd3eb71188a04dc3ad947b536d278c` |

The author's register changes after the pre-check are the owner's B461 ruling, B473's registration, the next-free
number (B474) and a B459 entry in §0. This brief is pinned by the hash QA issues on PASS, not listed here.

**Preflight:**
1. Check both repositories: MeshRoute `HEAD` = `8079e54`; the simulator at `6585649` and clean.
2. Every executable input matches its hash above, and the four new paths are absent.
3. Every preparation-set hash matches, and the pre-check folder verifies (`sha256sum -c SHA256SUMS`, 19 OK).
4. Every path in `inputs.json` still has its recorded hash, except the register, `tracker.md` and `MEMORY.md`.
5. Classify every other path. QA review reports and evidence added after this brief are explained preparation
   additions; anything else is reported.

Any of the following is STOP-1 to QA: a different `HEAD`, a mismatched input, a changed preparation file, or an
unexplained path. The whole preparation set stays unchanged from preflight through the freeze.

## 2. Contract

### 2.1 Increment 1 — the shared comparator (a C1 refactor)

**What moves.** `tools/probe_accounting.sh` is a sourced library holding the **generic** half of B456's accounting:
- one terminal-observation recorder;
- the guard-failure ledger;
- the final comparison of an expected set against the observations and the guard ledger.

The expected set is `id`, then its one accepted outcome. The comparison fails, and reports, each of:
- an empty expected set;
- a duplicate declaration;
- a missing, duplicate or unknown observation;
- an observation with the wrong outcome;
- every recorded guard failure.

**What stays in the firmware-UI runner:**
- its `ctl` extractor and call-count agreement;
- its `yes`/`no` → `red`/`build_fail_ok` mapping — C0 stays the required build failure;
- its `--no-neg` policy, its self-test mode, its counters and every line of its output.

The library holds no runner's declarations, predicates, mutations, modes or labels (U1: only the comparison is
shared). Runner-specific message prefixes, such as `[[B456]]`, may be a parameter so that every firmware-UI line stays
byte-identical.

**Behaviour identical — the refactor's proof:**
- **Self-test cases:** every B456 self-test case gives the same exit status and byte-identical output before and
  after.
- **Stock default run:** it reproduces a fresh pre-extraction baseline — per-arm counts, the set of control verdicts,
  and every accounting and summary line (temporary paths may be normalized).

**The load-bearing regression keeps working (D6):**
- `ACCOUNTING_STATEMENT`, the exact final-accounting call that B456's bypass test removes, stays **exactly once** in
  the firmware-UI runner and still disables the real final comparison. If its text must change, only the test's
  constant changes with it.
- The bypass test copies `run.sh` alone into a temporary directory. That copy must still reach the **real** shared
  library through an explicit mechanism that the test sets up. A missing library must fail loudly with its own
  message, and must never be what makes the regression pass.
- In normal runs, the runner finds the library by an absolute path from its repository root.

**Freeze.** At the end of increment 1, record the SHA-256 of its three files and snapshot them into the evidence.
Increment 2 must not touch them.

### 2.2 Increment 2 — board-UI accounting (B459)

#### 2.2.1 Identities and the manifest

Every executed check or control has one stable identity (pre-check Q3):

| Namespace | Identities | Count |
| --- | --- | ---: |
| `canvas/v3/<id>`, `canvas/v4/<id>` | the `CHK` label's first token; `P4a`'s ten iterations become `P4a/0` … `P4a/9` | 124 + 110 |
| `trait/<id>` | `T1` … `T14` | 14 |
| `missing/<MACRO>` | the twelve removed traits | 12 |
| `struct/<id>` | `S1`, `S2/<hook>`, `S2b`, `S3/<slug>`, `S4a`, `S4b`, `S5`, `S6/<MACRO>` | 23 |
| `wiring/<W-id>` | the 60 wiring checks | 60 |
| `wiring/<W-id>/control/<n>` | each check's controls, by ordinal | 186 |
| `negctl/v3/<C-id>`, `negctl/v4/<C-id>` | the control label's first token | 60 + 3 |
| **Total** | | **592** |

Rules for identities:
- **Stable.** An identity never embeds a measured value; some human labels do (S1's symbol count, S3's and S6's
  match counts). Human text may stay in the output for diagnostics.
- **Wiring controls by ordinal.** Within a stable check ID, ordinals are an adequate identity. §2.4 freezes the
  mapping from ordinal to substitution to property; reordering or removing a control is a ledger change.

**The manifest.** `tools/probe_board_ui/expected.tsv` lists every identity, one per line: its ID and its one accepted
outcome kind (§2.2.3); human text is optional. It is the expected set for every run, not a copy derived from the run.

**Coverage preservation (pre-check Q3, proof 2).** At the freeze, the manifest must equal the pre-change identity
set:
- for the shell and negctl layers, the pre-check's independent reconciliation (`board-reconciliation.json`);
- for the canvas layer, which the stock binary never traced, the set derived from the base `probe_main.cpp` (its
  pinned hash above) per arm, reproducing the base totals of 124 and 110.

The only instrumentation change allowed is `P4a`'s ten iteration keys.

**Future changes.** Adding, retiring or renaming a board-UI check becomes a manifest edit, visible in the diff; the
brief that makes the change must list it.

**The honest limit.** Removing a check and its manifest line together cannot be caught at runtime — nothing can infer
the deleted intent (pre-check Q3). The manifest diff makes such a removal visible for review. The runner and its
comments must never claim more than that.

#### 2.2.2 Observations

**One recorder.** Every executed identity produces exactly **one** terminal observation, through the shared
recorder, in the run's single observation ledger:
- **The shell layers** — traits, missing traits, structural and wiring checks and their controls — record directly.
- **The two positive canvas runs** write their records to a file the runner names; the runner then imports them into
  the ledger.

  In `probe_main.cpp`, `CHK` emits `id`, then `pass` or `fail`, when the runner asks for records — for example, via an
  environment variable naming the file. `P4a`'s loop emits its per-iteration IDs.

  Nothing else in that file changes: no assertion, expression, fixture, printed `FAIL` line format or tally.
- **negctl.py** records each control's terminal outcome. It prints its final count from the completed records, not
  from `len(MUT)`; the line's wording stays. Its section headers and existing result-line forms stay, because W2's
  frozen evidence parser reads them. Its mutation lists and substitutions are unchanged.

**Mutant runs never count as positive runs.** Canvas binaries built from a mutant — trait controls, missing traits,
negctl — never write into the positive `canvas/` namespace. Records from different runs cannot satisfy each other's
expectations.

**Missing records are missing identities.** A child that exits 0 without every expected record — a canvas arm or a
negctl arm — leaves those identities missing, and the run fails.

#### 2.2.3 Outcome contracts — preserved as they are (pre-check Q2)

| Layer | Accepted | Rejected |
| --- | --- | --- |
| canvas `CHK` | `pass` | `fail` |
| trait control | `red`: compiled, and the probe exited nonzero | a compile failure; stayed green; an anchor miss |
| missing trait | `named_compile_error`: the named `#error` | compiled; an unrelated compile failure; an anchor miss |
| structural | `pass` | `fail` |
| wiring check | `pass` (the live predicate is true) | `fail` |
| wiring control | `red`: a changed copy and a false predicate | vacuous; still true; any harness error (§2.2.4) |
| negctl control | `red`, with its kind — `compile_fail` or `assert_red` — kept distinguishable in the record | stayed green; not applied |

No control's predicate, substitution, assertion or accepted outcome changes. A trait mutant that crashes still counts
as `red` exactly as today; that weakness is B473 and is not fixed here.

#### 2.2.4 Harness errors are recorded, never credited (pre-check Q2)

- **A failed sed** in `wchk_in` is a rejected control outcome, never a mutant.
- **A predicate's exit status other than 0 or 1** is a harness error, on both the live and the mutant path — for
  example 127 for an undefined function, or 2 for a grep error. It is never "false as expected".
- **A guard that fails before a call** — false, undefined or an early return — leaves that identity missing.
- **Measurement:** on the candidate, every live wiring predicate exits exactly 0, and every mutant evaluation exits
  exactly 1. An exception is STOP-1, never silently accepted.

#### 2.2.5 The static declaration census

Before anything executes, a fail-closed census checks that the declarations in the source equal the manifest, one
namespace at a time:
- **Canvas:** `CHK` first tokens in `probe_main.cpp`, per arm, preprocessed with that arm's defines, including
  `P4a`'s declared iteration keys.
- **Runner call sites:** the IDs at each `trait_control`, `schk`, `wchk_in` and `wchk` call, with the S2, S3, S6 and
  missing-trait loops expanded from their literal lists.
- **Wiring controls:** each wiring check's number of sed arguments.
- **negctl:** its two lists, read through Python's AST.

An unrecognized or ambiguous call shape fails the census. A narrow, documented declaration syntax is acceptable. The
census may live in `tools/probe_board_ui/accounting.py`, but it must not contain a second comparator (U1).

#### 2.2.6 Modes

- **Default:** all 592 identities must be observed exactly once with their accepted outcome, with no guard failure.
  The runner then prints one completion summary and a final `PASS` or `FAIL`. The existing summary lines stay.
- **`--no-neg`:**
  - The 63 negctl identities are skipped by mode, and any negctl observation in this mode fails the run.
  - The other 529 identities are accounted in full, so a missing inline control still fails.
  - The last line is `PROBE-ONLY — NOT A GATE (--no-neg: canvas mutation controls skipped)`, and the run never prints
    a whole-run `PASS`.
- **`--selftest-accounting <fixture>`:** a labelled synthetic mode. It replays records through the **same** recorder,
  guard handling and final comparison that a default run uses, then exits before compiling anything.
- **One finalization call** serves all three modes.

#### 2.2.7 The regression suite — `tools/test_probe_board_ui.py`

The case families (pre-check Q6); derive the actual count from discovery:

**Completeness in every namespace:**
- a complete set passes;
- an empty enabled namespace and a duplicate declaration are rejected;
- missing first, middle and last, unknown, duplicate, and an equal-count missing-plus-duplicate all fail. These run
  per layer, including one `P4a` iteration and both negctl arms.

**Harness errors:**
- a false guard, an undefined guard (127), and a recorded guard failure with every observation present;
- a failed sed, a malformed or truncated record, and an early return before recording;
- a missing child record file, and a child exiting 0 without all its records.

**Coverage:**
- an omitted call with its declaration intact;
- a census mismatch, and an unsupported call shape;
- a declaration and call removed together, which the manifest comparison rejects;
- shortened trait, missing-trait and S6 loops;
- a shortened negctl iterator while its full list stays declared.

**Outcome contracts:**
- a trait compile failure rejected;
- a missing trait's named error accepted, while an unrelated compile failure or an unexpected success is rejected;
- a wiring control's vacuity and changed-copy rules preserved;
- negctl's two accepted kinds distinguishable.

No generic "nonzero means every control passed".

**`--no-neg`:**
- positive and inline-control accounting is still required;
- the 63 negctl identities are classified as skipped;
- no whole-run `PASS`;
- an unexpected negctl observation is rejected.

**Load-bearing:**
- In a temporary runner copy, remove the **real** final accounting call: an omission must then pass, while the stock
  runner refuses it.
- The same holds for losing one whole child layer's records.
- The copy resolves the real shared library, exactly as in §2.1.

### 2.3 B461

In W50's comment, change "Control (c)" to "Control 2". Nothing else changes: not the line count, and no executable
byte. W50's three controls still fail for their original reasons.

### 2.4 The closed ledger

- **Identities:** 592, exactly as in §2.2.1. Their meanings are unchanged: every check, control, predicate,
  substitution, mutation and assertion stays as W2 froze it.
- **The one instrumentation change:** `P4a`'s ten iteration keys.
- **Wiring controls:** each keeps its ordinal → substitution → property mapping as frozen by W2's ledger.
- **Additions:** the new regression suite's tests, counted separately by discovery.

Anything else is a STOP.

### 2.5 Nothing else moves

- **Inputs:** no `src/`, `lib/`, `test/` or `variants/` file, no `platformio.ini`, no simulator input and no shared
  fake changes. The command inventory is checked, never regenerated.
- **Other tools:** the other W2 candidate files, the mutation harness and the other eleven runners stay
  byte-identical.
- **Increment 1's files** do not change in increment 2.
- **Metal:** nothing changes on hardware, so the metal plan does not change (M2).

## 3. Fence

**IN — increment 1:**
- new `tools/probe_accounting.sh`;
- `tools/probe_firmware_ui/run.sh` — the extraction only;
- `tools/test_probe_firmware_ui.py` — only what the library's resolution from a temporary copy needs.

**IN — increment 2:**
- `tools/probe_board_ui/run.sh` — the accounting, the census call, the modes, the self-test mode and B461;
- `tools/probe_board_ui/probe_main.cpp` — record emission and `P4a`'s iteration keys only;
- `tools/probe_board_ui/negctl.py` — records and completion only;
- new `tools/probe_board_ui/expected.tsv`, new `tools/probe_board_ui/accounting.py` (optional) and new
  `tools/test_probe_board_ui.py`;
- the report and evidence (§5).

**OUT:**
- every `src/`, `lib/`, `test/` and `variants/` file, `platformio.ini`, and the simulator;
- `tools/probe_board_ui/fakes/*`, `tools/probe_inbox_verbs/*`, `tools/probe_ui_model_mutations.py`, and every
  other tool or tools test;
- the eleven other runners (B462–B472) and any crash-classification change (B473);
- regenerating the command inventory;
- the design, register, tracker, `MEMORY.md` and metal plan (QA lands, §7).

If a repair seems to need anything outside this fence, that is STOP-1: a fence question for the owner.

## 4. Gates

**The pre-check's focused chain (Q9), the same for the coder and QA.** A tool-only change cannot move the firmware, so
the chain measures the edited instruments and proves by hash that nothing else changed.

**Ordering:** nothing else runs between or during these steps. No `--no-neg` result substitutes for a gate.

### 4.1 The coder's gate

**Baselines, on the unmodified tree, before increment 1:**
- **Firmware-UI:** the stock default run, all arms and controls, with an independent label-completeness count.
- **Its self-tests:** B456's 19 tests, and each self-test case's exit status and output, captured for §2.1's proof.
- **Board-UI:** the stock default and `--no-neg` runs, with pre-check Q9's figures.
- **Tools:** full discovery (375 expected) and `python3 tools/gen_command_inventory.py --check` (197 rows).

**Increment 1, then its gate:**
1. Only increment-1 files differ; `git diff --check` is clean.
2. The firmware-UI stock default run reproduces the baseline (§2.1).
3. B456's 19 tests pass, and every self-test output is byte-identical to its capture.
4. On scratch copies:
   - the bypass regression disables the **real** final call and resolves the real library;
   - a missing library fails with its own message.
5. **Freeze:** record the three files' hashes and snapshot them into the evidence.

**Increment 2, then the final chain:**
1. **Scope and inputs:**
   - only the §3 files, the report and the evidence differ from the preflight inventory;
   - increment 1's files still equal their freeze;
   - every read-only dependency is byte-identical — including the other W2 files, the mutation harness, and every
     product, test and simulator input;
   - `git diff --check` is clean.
2. **Syntax:** `bash -n` on each changed runner, and a Python compile of each changed or new Python file.
3. **Regressions:** `tools/test_probe_board_ui.py` passes, including the load-bearing bypass case; count derived.
4. **Board-UI, stock default run:**
   - exit 0, with all 592 identities accounted and per-namespace counts reported;
   - every control accepted under its own contract, and no guard failure;
   - §2.2.4's measurement: 60 live predicates exit 0, and 186 mutant evaluations exit 1.
5. **Board-UI, stock `--no-neg`:** exit 0, with the banner; 529 identities accounted and 63 skipped by mode.
6. **Independent reconciliation, once.** Derive the expected and observed sets separately from the implementation —
   never its own parser on both sides — for example from an xtrace plus the pre-check's reconciliation method. Show
   that:
   - the observed set equals the manifest;
   - the manifest equals the pre-change identity set, differing only in `P4a`'s keys.
7. **Firmware-UI again, at final:** the stock default run and B456's 19 tests. The shared library is an input to it.
8. **Tools discovery (D5):** `python3 -m unittest discover -s tools -p 'test_*.py'`. Expect 375 plus the new tests;
   OK, 0 skipped, no import failure.
9. **Command inventory:** `python3 tools/gen_command_inventory.py --check`. Expect 197 rows, byte-identical.
10. **Reader audit (D6/P7):** grep `lib/`, `src/`, `test/` and `tools/` for every reader of the changed lines and output.
    That includes W2's frozen evidence parsers: state whether they still read the new output, or explicitly version a
    new reader. Never edit historical evidence. Report each reader, and say whether the edits touch its predicate.

**Input stability:** inputs are recorded before the baselines and after the last step. They differ only by the fenced
edits and the new evidence; a change in between restarts the chain.

### 4.2 QA's independent gate

On the frozen tree, QA re-runs final-chain steps 1–10 itself, including its own reconciliation. It checks that
increment 1's snapshots equal the final increment-1 files, and verifies the freeze inventory.

**Not run, by either side,** with the reason each stays unaffected — confirmed by the reader audit:
- **Native, corpus, board builds and measurements, ABI, the warning census and the mutation battery:** no product,
  build or test input changes, and the harness file is untouched (step 1 proves both).
- **The other probes:** none of their inputs changes.

### 4.3 STOP conditions

- **Fence:** an edit outside §3; a change to the other runners; a crash-classification change (B473); an
  increment-1 file changed in increment 2.
- **Refactor:** any firmware-UI behaviour or output difference in increment 1; the bypass regression no longer
  disabling the real call; a missing library satisfying a regression.
- **Ledger:** an identity added, removed or renamed outside §2.4; the manifest differing from the pre-change set by
  more than `P4a`'s keys; a check or control's meaning changed; a contract in §2.2.3 tightened or loosened.
- **Accounting:** an identity observed twice or through two paths; mutant records reaching the `canvas/` namespace;
  a comparator duplicated in the census helper; a runner or comment claiming that paired deletions are detected.
- **Harness errors:** a live predicate exiting other than 0, or a mutant evaluation other than 1 (§2.2.4).
- **Other instruments:** firmware-UI not reproducing its baseline; a tools count other than the derived one, or any
  skip or import failure; a command-inventory difference.
- **Inputs:** a mismatch at preflight, a changed preparation file, or an input change during the chain.
- **Source facts:** a §1 fact found false — STOP-1 to QA, never a coder assumption.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-09-28-b459.md`, with an author line on line 1.

**Evidence directory:** `docs/superpowers/evidence/2026-09-28-b459/`. It holds:
- the baselines, and increment 1's freeze with its snapshots;
- the self-test output captures and their comparison;
- compact board-UI and firmware-UI summaries for every run;
- the manifest comparison with the pre-change set;
- the independent reconciliation;
- §2.2.4's exit-status measurement;
- compact discovery and inventory summaries;
- the reader-audit greps;
- the freeze inventory;
- a `SHA256SUMS` covering everything.

**Retention.** Follow [the evidence-retention policy](../evidence/README.md). Raw logs go under ignored
`artifacts/2026-09-28-b459/`; scratch copies stay outside both repositories.

**The report contains:**
1. **Preflight** and the inventory check.
2. **Baselines**, then increment 1 and its freeze.
3. **Diff:** `git diff --stat`, and the §2.4 ledger.
4. **Figures:** every §4.1 figure, derived by the coder.
5. **Reconciliation,** with the manifest comparison.
6. **The pin line:** the exact line `PIN re-synced? YES — <derivation>`. The expected derivation: native 3031/198613
   unchanged (no product or test input, and no mutation-harness change); board-UI identities 592 = 124 + 110 + 14 +
   12 + 23 + 60 + 186 + 60 + 3; tools tests 375 + the new count.
7. **Freeze inventory:**
   - the final SHA-256 of every fenced file;
   - the authorized brief hash;
   - the simulator commit and status;
   - the preparation-set hashes, unchanged;
   - the inventory check;
   - the new evidence paths with hashes;
   - the stability statement.
8. **Not run,** with reasons (§4.2). Each reason is that the instrument's predicates are unaffected — confirmed by the
   reader audit — never "it reads none of the files".

## 6. Owner rulings

- **Scope,** ruled 2026-09-28: B459 is its own tool package, right after W2 and before W6, and B461 folds into it.
- **Nothing else is requested.** The two-increment packaging follows C1 and the pre-check's Q4 recommendation.
- **The manifest** is an author decision within the fence: a checked-in expected set, so that a future change to the
  check set is an explicit, reviewable edit.
- **A contradictory interpretation** returns to the owner.

## 7. Landing (QA, on PASS)

- **Register:** **B459 and B461 CLOSED** in place; B460 and B462–B473 stay OPEN; the §0 dispatch is rewritten in
  place.
- **Pointers:** `tracker.md` and `MEMORY.md`; the design's §13 W2 row, where it names B459 as next.
- **The board-UI gate for future UI briefs:** the default run, exit 0, is complete by construction, so the manual
  declared-versus-executed reconciliation retires. A board-UI check change in a future package is a manifest edit
  that its brief lists. `--no-neg` stays a diagnostic.
- **Metal plan:** no change.
- **Next:** W6, which W2, W3 and W4b unblock — or W5, W9, W0, W1b, W4c, B460 or B462–B473, in the owner's order.

## 8. Revision history

**Revision 1 (2026-09-28)** is the first draft, built on the B459 pre-check. It adopts the pre-check's
recommendations:
- the shared comparator, introduced by a separately frozen refactor increment;
- per-layer identities, and recording rather than counting;
- preserved outcome contracts, and harness errors recorded explicitly;
- the mode banners, the synthetic mode and the case families;
- the focused chain.

It fixes three author choices:
- a checked-in identity manifest as the expected set;
- increment 2 may not touch increment 1's files;
- B461's one-token correction.

It keeps B460, B462–B472 and B473 out.
