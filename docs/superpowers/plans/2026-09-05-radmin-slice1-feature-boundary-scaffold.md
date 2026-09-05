<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 1 — feature-boundary scaffold · dispatch brief · 2026-09-05

**Status: DRAFT — awaiting Quality-Agent review.** Dispatch model: **Opus** (`model: opus`).

Authority: design §19 item 1 and §19.1 row 1 of
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`; the Quality-Agent ledger
`docs/superpowers/plans/2026-09-05-radmin-slice1-precheck.md`; R-RA-8, R-RA-17 and the owner-settled R-RA-26 in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`.

**Dispatch base: NOT YET PINNED — STOP FOR THE CODER.** Source facts below were verified at `b942c37` (`0c`);
that is a source reference, not the dispatch base. At authoring, R-RA-26 was modified and the pre-check untracked.
The dispatch base is the owner's subsequent commit containing the preparation package, including this brief and
the ruled ledger. After that commit exists and QA passes the brief, the Author writes its exact hash here.
QA and every isolated-worktree coder verify `git rev-parse HEAD` against it before implementation. A placeholder,
different base or unexplained dirty start is a STOP; the coder neither repins nor silently repairs the checkout.
The later Author base-pin edit, if uncommitted, must be explicitly named as the sole permitted pre-existing edit.

This is an additive compile-time scaffold, not a runtime refactor (C1). One production header gains the endpoint
pair and its configuration checks. All current feature values, legacy consumers, runtime decisions, Node layout,
wire bytes and command surfaces remain identical. No endpoint consumer is added until a later slice.

## Authority quoted verbatim

From design §19 item 1 (`:1655` at authoring):

> 1. **Feature-boundary scaffold:** add
>    `MR_FEAT_RADMIN_CLIENT` and `MR_FEAT_RADMIN_ACCEPT`. Compile-time and behaviour controls prove
>    `{client=1,accept=0}` on mobile and `{client=0,accept=1}` on static/gateway while ordinary transit stays
>    available with neither endpoint consumer involved; native deliberately compiles `{1,1}` with separate
>    role-disable controls.

From R-RA-26 (`:566` at authoring):

> **Settled:** `lib/core/mr_features.h` derives the R-RA-8 pair as: `MR_PROFILE_MOBILE` → `{CLIENT 1, ACCEPT 0}`;
> `MR_PROFILE_GATEWAY` → `{0, 1}`; a BOARD build (`defined(ARDUINO)`) with no `MR_PROFILE_MOBILE` → `{0, 1}` (the five
> no-profile static envs `production`, `xiao_sx1262`, `heltec_v3`, `heltec_v4`, `xiao_esp32s3` are static products);
> a HOST build (no `ARDUINO`: native, lus) → `{1, 1}` (R-RA-17's host reading). `#error` on a board build with both or
> neither; `ACCEPT == MR_FEAT_REMOTE_MGMT` pinned on every board build until Slice 9 deletes the legacy switch. No
> `MR_PROFILE_STATIC`, no `platformio.ini` edit, no consumer in Slice 1 (4.2 implied: pure scaffold).

From the pre-check's dispatch condition:

> Base commit at dispatch = the owner's commit after this ledger + the Author's brief (name it; STOP if different).

R-RA-26 supplies the discriminator missing from R-RA-17's earlier profile-only wording. It also makes the scope
of the design's behaviour controls precise: compiled configuration values and production-header rejection rules,
with the existing runtime corpus unchanged. It does not authorize runtime handlers or disabled-role packet tests
in this slice. MeshRoute remains test-hardware-only (M3).

## Exact starting state and pre-check corrections (V1/V2)

| Verified fact at `b942c37` | Source authority |
| --- | --- |
| Gateway profile sets TEAM/MOBILE to 0; mobile profile sets legacy REMOTE_MGMT to 0. Defaults preserve the other features, with OLED defaulting to 0. The existing TEAM-requires-MOBILE `#error` stays. | `lib/core/mr_features.h:11`, `:23`, `:27`, `:47` |
| The two RADMIN capabilities do not exist: a search for `MR_FEAT_RADMIN` / `RADMIN_` in `lib/`, `src/`, `test/`, `tools/` returns no matches. | Current source census, repeated by the coder before editing |
| Five static board environments have no profile; four gateway and four mobile environments inherit their respective profiles. Board frameworks are Arduino. | `platformio.ini:95`, `:201`, `:337`, `:381`, `:453`, `:506`, `:518` and their `extends` chains |
| Native has `MESHROUTE_NATIVE=1`, two layers and no Arduino/profile define. Both lus variants have no Arduino/profile define; the gateway variant does set `MR_GATEWAY_BUILD=1`. | `platformio.ini:67`; simulator `CMakeLists.txt:108`–`:119` |
| Existing device headers already use `defined(ARDUINO)` as a device discriminator. The legacy feature header itself currently derives REMOTE_MGMT by profile/default, not by testing Arduino. | `src/device_rng.h:23`, `src/device_ble.h:24`; `lib/core/mr_features.h:23`, `:43` |
| Legacy state is still conditional on REMOTE_MGMT; `_remote_inbound` remains unconditional. No declaration moves. | `lib/core/node.h:104`, `:2900`–`:2905` |
| Current inventory/profile projections read the legacy feature set. No command consumes the new pair. | `tools/gen_command_inventory.py:203`–`:209`; `tools/probe_console_sink/run.sh:70`–`:75` |

The Author preprocessed the existing header under bare host, native, lus-gateway, Arduino static, Arduino gateway
and Arduino mobile configurations. All compiled; their six existing feature values agreed with the source above.
This is starting-state verification, not the new matrix gate or a substitute for the coder's measurements.

Two corrections must not be copied from the pre-check into implementation or evidence:

- **B304 — archived pointer, not missing history.** Pre-check `:23` says the feature-split spec is absent from the
  repository. `git ls-files` finds it at
  `docs/superpowers/specs/archive/2026-07-12-firmware-feature-split.md`. Correct the header's `:7` reference to
  that historical location; describe current endpoint policy using R-RA-26 and the current remote-admin design.
  The sibling stale pointer at `platformio.ini:456` is recorded in B304 but remains outside this fence.
- **B305 — ABI labels reversed.** Pre-check `:29` assigns the two board sizes to the wrong architectures.
  `tools/probe_board_abi.py:101`–`:106`, `:255`–`:302` pins native/HOST **222072**, `heltec_mobile`/Xtensa
  **117912**, and `gateway`/ARM **148680**. These are verified source pins, not fresh measurements; the coder
  independently measures and reports each named target. The incoming QA ledger remains untouched by the Author.

## Required result

The production derivation lives once in `lib/core/mr_features.h`, beside the existing profile/default authority.
The values are numeric compile-time flags usable by later `#if` consumers:

| Real build configuration | CLIENT | ACCEPT | Legacy REMOTE_MGMT, unchanged |
| --- | ---: | ---: | ---: |
| Arduino + mobile profile | 1 | 0 | 0 |
| Arduino + gateway profile | 0 | 1 | 1 |
| Arduino + no profile: static products, including `production` | 0 | 1 | 1 |
| Native: no Arduino/profile, `MESHROUTE_NATIVE=1`, two layers | 1 | 1 | 1 |
| lus normal: no Arduino/profile, default single layer | 1 | 1 | 1 |
| lus gateway: no Arduino/profile, two layers, `MR_GATEWAY_BUILD=1` | 1 | 1 | 1 |

Use `defined(ARDUINO)` for the board checks. Neither the absence of `MR_PROFILE_*` alone, `MESHROUTE_NATIVE`,
`MR_N_LAYERS`, nor `MR_GATEWAY_BUILD` distinguishes all these cases. In particular the simulator gateway must
retain both endpoints. Host probes that select a product profile remain synthetic configurations; enumerate
their flags and keep their existing six-feature projection exact rather than treating them as new product policy.

On every board configuration, reject both endpoints enabled and both disabled, and enforce
`MR_FEAT_RADMIN_ACCEPT == MR_FEAT_REMOTE_MGMT`. The consistency check must reject disagreement, not rewrite
either flag to hide it. Host `{1,1}` is permitted. Keep all six existing feature definitions and the TEAM/MOBILE
dependency unchanged; keep the legacy switch and all its consumers until Slice 9. Do not add a profile,
configuration override surface, header dependency, state, timer, function, stub or runtime gate merely for this
scaffold or its tests.

Correct the touched header's introductory profile/default description: static boards may have no profile and
the new pair does not default to both-on there; OLED already defaults off. Keep superseded claims visibly
withdrawn under the correction idiom. B304's header correction is prose only; isolate its comment sub-diff and
prove token identity under B254. No historical spec is revived as current implementation authority.

## Executable configuration gate and control classification

The new instrument is **`tools/probe_features/run.sh`**, with its small production-header driver and control
support under `tools/probe_features/`, plus **`tools/test_probe_features.py`** for the standing tools sweep.
Reuse the established runner/pin/source-integrity idioms from `probe_ble_line` and its wrapper. No Arduino fake
is needed: the header consumes defines, not a device API. Do not copy the production derivation into a fake or
generate the expected answers from the values being checked.

For valid configurations, host-compile the real `mr_features.h`, execute the driver, and independently assert and
print all six old flags plus the two new flags. Cover every real row above; board roles cover both OLED values
used by the current environment matrix. Derive the environment-to-cell mapping from the resolved build flags,
account for all five no-profile static products, and record the native and both simulator flag sets. This host
matrix is configuration coverage, not an additional firmware board build. Its native-host row is the new
host-executed acceptance case; no new TU or PIN change under `test/` is needed.

**Classification is declared before any control runs:**

| Control class | A measured RED requires | Never count as measured RED |
| --- | --- | --- |
| Production compile-time refusal | The declared invalid configuration fails preprocessing/compilation at the intended `mr_features.h` diagnostic; the corresponding legal configuration compiles and runs. Retain command, exit status and diagnostic. | Missing compiler/include, syntax error, macro-redefinition warning promoted to error, unrelated `#error`, signal, timeout or a probe-owned assertion |
| Executable matrix mutation | The mutated header builds, the driver runs and reports the named wrong-value assertion. | Any build failure or abnormal exit without the named failed assertion |
| Guard-removal or runner sabotage | The isolated defect changes the declared outcome and the enclosing gate rejects it for the named reason. | A missing/multiple mutation match, unexplained failure, or a still-green gate |

Compile failure is expected RED **only for explicitly declared production-header refusal controls**. This is the
Slice-1 exception to the runtime probes' compile-failure-is-unusable rule; it does not change those probes or the
standing mutation harness. A compiler that cannot compile a legal cell makes the gate fail, never pass by
rejecting all invalid inputs.

At minimum the instrument must establish these independently controlled obligations:

1. All real configuration rows and their six old feature values are correct; each new flag's derivation is
   independently attacked. A host losing either endpoint is detected, including the lus gateway case.
2. A board with `{1,1}` and one with `{0,0}` each reach the production exclusivity diagnostic. Choose fixtures
   whose legacy/accept equality holds so the consistency check cannot mask a missing exclusivity check.
3. An exclusive board pair inconsistent with REMOTE_MGMT reaches the production consistency diagnostic.
   Exercise both static/gateway and mobile disagreement directions while keeping exclusivity satisfied.
4. Delete each production check independently on an isolated copy: its invalid fixture must now compile, and
   the enclosing gate must reject that unexpected acceptance. A second check must not conceal the removal.
5. Wrong board/host discrimination or reversed role derivation is rejected in its predeclared control class.
   A board-only restriction applied to the legitimate host `{1,1}` must also be detected.
6. A compiler failure such as `CXX=/bin/false`, an unrelated syntax error and an unrelated diagnostic cannot be
   scored as successful policy refusal. Dropping a required configuration/check/control cannot preserve PASS.

Inject invalid assignments only into isolated header copies ahead of the production checks, with exactly one
verified mutation match. Do not add a production test hook or rely on command-line macro collisions to fake
invalid states. Guard-removal tests may reuse these named invalid fixtures as their established baseline; each
then changes only its one guard. Prove the included header is the intended original or mutant, and hash every
source the instrument uses/mutates before and after. The shared checkout must remain byte-identical.

Controls run by default. The runner enforces its derived configuration/check/control counts itself; the wrapper
pins the same measured inventory and validates the classifier's failure modes. If an optional `--no-neg` mode is
provided, its verdict is `PROBE-ONLY — NOT A GATE`, with no standalone gate-PASS line from any child. Preserve
rejected output for diagnosis and report the refusal controls separately from executed matrix mutations.
Every added instrument file, including executable mode and any support file, belongs in the evidence inventory.

## Fence and inertness proof

Allowed coder changes:

- `lib/core/mr_features.h`: the pair, its derivation, board checks and adjacent comment corrections only;
- `tools/probe_features/` and `tools/test_probe_features.py`; and
- `docs/superpowers/evidence/2026-09-05-radmin-slice1.md`.

Expected diff: exactly **one production header**, one dedicated instrument plus its discovered wrapper, and one
evidence file. No file move, other `lib/` file, `src/`, `test/`, `platformio.ini`, board/variant, simulator,
`BASELINE.md`, old instrument, generated inventory or companion edit. Register/spec/memory/tracker landings
belong to the Author after QA PASS. The header is the only production location allowed to name the new flags;
outside it they occur only in this slice's instrument and documentation. No consumer is enabled or disabled.

Measure the committed base and final tree with the existing deterministic board instrument, fixed build identity
and the same checkout/build paths. **Both `gateway` and `heltec_mobile` must have RAM delta 0 and flash delta 0.**
Loadable sections, object counts and symbols must also be unchanged. This is the unused-macro inertness proof,
not a tolerance: any movement is a STOP. Apply the existing B254/B262 distinction to raw ELF/payload hashes;
source/debug metadata movement must be isolated and reported, never used to excuse changed executable sections.

The simulator must rebuild through the changed core-header dependency; report build actions and binary hashes.
Rebuilding is required even if the resulting executable hash is identical. Require all 36 output streams
byte-identical and the current s18 keystone from `simulation/BASELINE.md`; no re-anchor is authorized.

Mutation coverage has two separately reported selectors:

- **Changed-source selector:** no standing `TARGET_SRC` entry names `lib/core/mr_features.h`.
- **Historical/dependency selector:** no existing battery owns the new endpoint derivation or checks; unchanged
  downstream consumers do not become new RADMIN consumers merely because they include the header.

Both are expected empty, as the pre-check records. Re-derive both from the final tree and document the union.
The new probe owns the scaffold's controls; an empty battery union does not mean zero mutation coverage. A newly
discovered applicable existing battery joins the union and runs in full; do not edit it or widen production scope
to make a failing or unusable control pass.

## STOP conditions

Stop and report to the Quality Agent before widening the slice if:

1. the exact committed dispatch base is missing, differs from `git rev-parse HEAD`, or the starting tree has an
   unexplained edit beyond the explicitly recorded Author pin;
2. the ruled matrix needs another profile, a `platformio.ini` change, a different discriminator, or a new owner
   decision about a real product configuration;
3. any existing feature value, legacy consumer, command, runtime path, Node state/layout, wire/NV/timer or
   simulator source must change, or a production consumer of the pair is needed;
4. either ruled board's RAM, flash, loadable sections, object count or symbols move;
5. any corpus stream changes or the rebuilt simulator fails to reproduce the current s18 keystone;
6. any ABI pin changes, a native PIN moves, the command inventory needs regeneration, or an existing instrument
   needs an out-of-fence edit;
7. a named policy-refusal control fails for an unrelated reason, a control is green/vacuous/multi-matched or
   unusable, source integrity fails, or a gate discrepancy cannot be resolved within the named support fence; or
8. an unexplained warning, conflicting concurrent edit or unaccounted modified/untracked file remains.

## Gate and report

Run gates sequentially on the final tree; use only each instrument's own supported internal parallelism. The
firmware board gate is exactly `gateway` + `heltec_mobile`, with the warning census's own derived pinned set as
the standing exception. Preserve BEFORE measurements from the exact committed base.

1. Verify base, source/profile census, allowed paths and no consumers; derive and run the complete new feature
   probe with all controls, including the classifier's controls-of-controls.
2. Run `pio test -e native`, then **run `./.pio/build/native/program`** and record real cases/assertions/failures.
   Reconcile against the current mutation harness PIN; report `PIN re-synced? YES — <derivation>`. No native
   addition is planned, so unchanged counts/PIN must be demonstrated rather than copied from this brief.
3. Rebuild lus, record actions and hashes, then run `tools/run_corpus.py --jobs=8 --require-anchors`. Compare all
   36 streams against the base and the current anchor table; report s18 explicitly. Do not type a historical
   keystone into a new authority.
4. Run both complete ABI probes with controls: `tools/probe_board_abi.py` and `tools/probe_b278_row_abi.py`.
   Report named host/ARM/Xtensa targets and unchanged Node/row layout; do not copy B305's swapped labels.
5. Run the deterministic `tools/measure_board.py pair --jobs=2` workflow for the ruled pair, comparing base and
   final captures with fixed identity and source/toolchain manifests. Require the exact zero deltas above.
6. Run the complete default `probe_console_sink`, `probe_inbox_verbs`, `probe_firmware_ui`, `probe_custody_usb`
   and `probe_ble_line` gates. Their current pins and legacy feature projections remain exact.
7. Run `python3 -m unittest discover -s tools -p 'test_*.py'`, including the new feature-probe wrapper.
8. Run both bare `python3 tools/gen_command_inventory.py` and its `--check` form. The inventory must already
   match; **no `--write`** in this slice because no firmware command anchor moves.
9. Run `tools/warning_census.sh` over its own derived pinned environments, plus both standing checkers
   (`tools/check_a0_matrix.py` and `tools/check_data_type_literals.py`) with their supported controls.
10. Report both mutation selectors and gate their union where non-empty; check `git diff --check`, source
    restoration and the complete modified/untracked-file inventory. Leave the work uncommitted (D4).

Write the durable report to `docs/superpowers/evidence/2026-09-05-radmin-slice1.md`. It must contain the base
verification, source-derived environment matrix, all old/new feature values, compile-time versus executable
control ledger with diagnostics, per-instrument measured pins, native and corpus outputs, ABI and board A/Bs,
warning/checker results, both mutation selectors, STOP audit, B304 header-only comment proof, findings and the
explicit file inventory. Every outcome is measured by the coder, then independently rerun by QA.

**Metal residue: none.** All new behaviour is compile-time and exercised by the configuration gate; no Bench
Part is added (M2). After QA PASS, the Author updates design §19/§19.1 Slice 1, records B304's header correction
without closing its out-of-fence `platformio.ini` residue, and lands any new findings in the maintained register.
R-RA-26 remains the owner authority. No further owner ruling is requested by this brief.
