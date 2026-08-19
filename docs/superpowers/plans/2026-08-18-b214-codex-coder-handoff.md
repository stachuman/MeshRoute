<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B214 Codex-coder handoff — finish the independent regression guard

Date: 2026-08-18  
Role: **implementation coder**. The owner will commit; a separate QA pass will review your result.

## Start here

Read, in this order:

1. `AGENTS.md` (Codex project rules; it currently mirrors `CLAUDE.md`).
2. `docs/CODE_GUIDELINES.md`.
3. B214 in `docs/2026-07-30-open-bug-register.md`.
4. `docs/superpowers/plans/2026-08-18-b214-truthful-mobile-reg.md` in full.
5. The current implementation at `src/firmware_commands.cpp:308-338`.

Verify every cited line against the current tree before acting (V1/V2). The repository is dirty with other completed
and in-progress slices; those changes belong to the owner and must be preserved.

## Exact current state

B214 was metal-confirmed: `cfg` used to derive `mobile-reg:` only from `mobile_home_id()` and therefore lied in two
directions:

- no home id always printed `UNREGISTERED (scanning)`, even for the `dormant` state where no home-attachment work was
  scheduled;
- `claiming` already holds a provisional home id, so it printed `REGISTERED` before the chosen home confirmed the
  registration in its roster.

The behavior fix is **already present** in `src/firmware_commands.cpp:308-338`. It was written directly during the QA
session after two ordinary-coder dispatches failed with server-side `529 Overloaded`, so it has not yet received an
independent coder review.

Its intended mapping is:

| FSM state | `cfg` output |
|---|---|
| `attached`, home id nonzero | `REGISTERED home=<id>` |
| `attached`, home id zero | an explicit `INCONSISTENT` diagnostic |
| `dormant` | `UNREGISTERED (dormant)` |
| `seeking` | `UNREGISTERED (seeking)` |
| `claiming` | `UNREGISTERED (claiming)` |
| `recovering` | `UNREGISTERED (recovering)` |

The implementation currently:

- reads `mobile_attach_state()` as the authority;
- uses an exhaustive `switch` with no `default:`;
- reuses `Node::attach_state_name()` instead of hand-spelling state names;
- uses the home id only inside the `attached` arm;
- fails visibly for `attached` with no home id.

Do **not** rewrite this implementation merely to make it yours. Review it; change it only if you find a concrete
correctness defect and report that defect explicitly.

## Your bounded task

Finish the missing independent regression guard in the existing `tools/probe_console_sink` harness. That harness
already reads `src/firmware_commands.cpp`; do not create a new probe or duplicate the formatter in a stand-in model.

Primary edit scope:

- `tools/probe_console_sink/structural.py`
- `tools/probe_console_sink/negctl.py`
- `tools/probe_console_sink/run.sh` only if genuinely required by the existing harness structure

Do not touch unrelated dirty files. In particular, do not clean, restore, reformat, stage, or commit any existing
changes. Do not update the register, bench guide, B214 plan, or other documents; report the exact proposed closure
and metal-test text to QA instead.

## Required positive checks

Add one clearly named B214 structural check (or the smallest coherent set) that proves all of these from executable
source, not from comments:

1. the old home-id-only decision and the literal `UNREGISTERED (scanning)` are absent from live code;
2. `mobile_attach_state()` is read and controls an exhaustive five-arm `switch` with no `default:`;
3. `REGISTERED home=` is reachable only from the `attached` arm and still requires a nonzero home id;
4. `dormant`, `seeking`, `claiming`, and `recovering` use `UNREGISTERED (` plus
   `Node::attach_state_name(...)`;
5. `attached` with a zero home id emits the explicit inconsistency diagnostic;
6. the labels and `attach_state_name` call are counted so deletion cannot accidentally satisfy the check.

Strip or otherwise exclude comments when checking behavior. The current source comment intentionally quotes the old
defective text, so a raw substring test would confuse the audit trail with live code.

## Required controlled mutations

Each mutation must make the B214 check fail, while the unmodified source passes:

1. restore the old `if (home_id) REGISTERED else scanning` authority;
2. make `claiming` report `REGISTERED`;
3. remove the explicit `recovering` arm or hide it behind a `default:`;
4. remove the `attached`-without-home inconsistency diagnostic;
5. hand-spell the state label instead of calling `attach_state_name()`.

The mutation must reproduce the semantic defect being claimed, not merely create a syntax error. Report the failing
check for every control. Preserve all existing console-sink positive checks and controls.

## Verification context

Last independently observed before this handoff:

- `./tools/probe_console_sink/run.sh`: **52/52**, structural **11/11**, existing controls all effective — but none
  covered B214;
- `pio run -e xiao_esp32s3`: **SUCCESS**, RAM 214324 B, flash 1223596 B;
- the register correctly remains `OPEN — LABEL FIXED, GUARD OWED`.

Use the current baselines from the B214 plan and `simulation/BASELINE.md`; do not rely on remembered hashes. The owner
has ruled that board builds are limited to **two environments** to save time, while the warning census remains at its
existing scope.

Minimum completion gate is the gate in the B214 plan, including:

- native build **and direct execution** of `.pio/build/native/program`;
- all four established probes and their controls;
- warning census, especially zero `-Wswitch` warnings;
- the four-step simulator inertness proof with the current keystone read from `simulation/BASELINE.md`;
- two board environments only;
- `sizeof(Node)` / timer-capacity proof and `git diff -- lib/` empty.

If a full gate is temporarily impossible, do not call the task complete: report exactly what ran, what did not, and
why.

## Completion report

Return:

- exact files changed;
- the positive B214 checks and their measured/counting discriminators;
- all five mutations and which check each reddened;
- complete gate results, including skipped steps;
- confirmation that the behavior implementation was either unchanged or the concrete defect that required changing
  it;
- exact final `git status --short`;
- proposed register disposition and the short metal residue for the bench guide:
  - dormant/no session: `cfg` says `UNREGISTERED (dormant)`, never `scanning`;
  - during provisional claiming: `cfg` must not say `REGISTERED`.

Never run `git add`, `git commit`, `git checkout --`, or destructive cleanup. Leave the result uncommitted for owner
and QA review.
