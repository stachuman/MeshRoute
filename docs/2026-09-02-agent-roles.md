<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Agent roles and handoff protocol (owner-ruled 2026-09-02)

> **⛔⛔ ROLE CHANGE — OWNER-RULED 2026-09-07 (the table below is the 2026-09-02 division, KEPT VISIBLE as history):**
> *"We change roles - from now on YOU are doing Quality Gates and you are preparing spec - the other agent will code."*
> ⇒ From Slice 6 on: **the Quality Agent (Claude) AUTHORS the design specs, the pre-checks and the dispatch briefs,
> gates every slice report independently, dispatches, and LANDS the documentation on PASS** (register rows, bench
> parts, design/manual/tracker status lines, MEMORY.md). **The coder is Codex**: it VALIDATES the brief against the
> source at the pinned base before editing (every `file:line` is a claim to re-check; disagreements are a STOP-1
> preflight report to the Quality Agent, never a silent repair), then implements and reports evidence. **The owner**
> rules (`OWNER RULING REQUESTED`), commits both repositories and bench-verifies on metal. The `model: opus` line of
> step 4 and "What a brief must contain" is retired; the coder is named in the brief's status line. Everything
> else in the cycle — clean-start pins, prediction-first figures, two board envs, the report shape, same-agent
> fix rounds — is unchanged.

Three parties work on MeshRoute. Each owns a distinct step; none takes over another's step.

| Party | Owns | Never does |
| --- | --- | --- |
| **Author** (Codex) | Design specs (`docs/superpowers/specs/`), dispatch briefs (`docs/superpowers/plans/`), doc landings on PASS (register rows, bench parts, `frames.md`/`protocol.md` drafts, spec status lines), high-level second read of a slice's evidence file | Dispatch coders, edit production code, edit the `BASELINE.md` anchor table, commit |
| **Quality Agent** (Claude, this repo's QA session) | Gate every spec, brief and slice report against the code and the tree; dispatch the Opus coder from a PASSED brief and drive its fix rounds; run every gate instrument itself | Author specs or briefs, edit files during a review, accept a coder's figures, make owner rulings |
| **Owner** | Relay between Author and Quality Agent (one hop per artefact), rulings, `git commit`, bench verification on metal | — |

The Opus **coder** is a fourth, transient party: one agent per slice, dispatched by the Quality Agent, resumed across fix rounds, reporting only to the Quality Agent.

## Why this shape

Every recorded defect class in this codebase (masking holes, vacuous controls, "a success that isn't", overclaimed instruments) was caught at verification, by a context that had not written the artefact. Author and gate must therefore be different contexts. The owner's relay is the cost of that independence; coder fix rounds stay inside the Quality Agent's session so the owner relays once per artefact, not once per round.

## Cycle for one slice

1. **Pre-check (Quality Agent → Author, via owner).** Before a brief is written, the Quality Agent verifies the slice's source facts (file:line), runs corpus and native, and hands the Author a short ledger of facts, obstacles and a corpus-mover prediction. Ask for it; do not write a brief without it.
2. **Brief (Author).** Written against the spec and the pre-check ledger. Status line: `DRAFT — awaiting Quality-Agent review`.
3. **Brief gate (Quality Agent).** Verdict PASS or HOLD, numbered corrections each anchored `file:line`, plus a "verified independently" list. The Author folds corrections in and flips the status to `QUALITY-AGENT PASS <date> — AUTHORIZED FOR DISPATCH`.
4. **Dispatch (Quality Agent).** `model: opus`, from the brief as written. Fix rounds: same agent, resumed. Before
   starting work in an isolated worktree, the dispatched agent verifies and records `git rev-parse HEAD` against
   the brief's named base commit; a mismatch is a STOP to the dispatcher, never a stale-tree measurement or a
   silent worktree repair.
5. **Slice gate (Quality Agent).** Re-runs native, corpus, ABI, boards and warning census itself. PASS/HOLD to the owner and Author.
6. **Second read (Author).** Reads the evidence file for narrative and doc consistency; findings go to the register, never into the evidence file directly.
7. **Rulings (owner).** Anything marked `OWNER RULING REQUESTED` is decided by the owner alone: anchor-table edits, delivery movement, capacity/RAM, design forks, wire bumps.
8. **Landing (Author) and commit (owner).** Author lands docs on PASS; owner commits and bench-verifies.

## What a brief must contain (checked at step 3)

- Spec pins **quoted verbatim**, never paraphrased; STOP conditions copied exactly.
- `model: opus`; **two board envs only** (`gateway` + `heltec_mobile`); the warning census's own pinned set is the sole exception.
- The slice's fence: which paths may change; `git diff --stat` expectations; refactor XOR feature (C1); a `wire_version` bump is its own slice (C4/M3).
- **Wiring-gate evidence** for any new verb, handler or push consumer: which instrument compiles the production TU and drives the real router. A "the glue makes no decisions" claim is rejected on sight.
- Every figure **derived by the coder**, none quoted from the brief or the pre-check.
- Mutation coverage must distinguish two selectors when both matter: (a) batteries whose
  configured source file was changed by the slice/arc, and (b) dependency or historical
  batteries that define that slice/arc's acceptance surface. These sets are not assumed equal
  or nested; derive both, explain differences, and gate their union when the brief requires
  complete arc coverage.
- The durable evidence file path under `docs/superpowers/evidence/`; every new instrument named, added to a gate, and listed in the untracked-file inventory.
- The report shape, including the exact line `PIN re-synced? YES — <derivation>`.
- Rulings requested from the owner, listed explicitly, with a recommendation.

## What a spec must do (checked at step 3 of the design cycle)

- Verify every code claim against the source and cite `file:line` (V1). Comments, `ROADMAP.md` and older specs are not evidence.
- Name every existing ruling or in-source decision the design reverses, by its anchor.
- State reachability honestly: if a case is unreachable in production, say so and label its test synthetic.
- Never claim a spare bit or codepoint without the codec anchor; the DATA flags byte and `q_opcode` are full.
- Pick the right shape, not the one that dodges a wire bump (M3); wire changes are free, attribution is the cost.

## Verdict vocabulary

- **PASS**: dispatch or land as written.
- **PASS with fold-ins**: text corrections that need no re-review; fold in, then proceed.
- **HOLD**: numbered required corrections; one re-review of the changed sections only.
- **STOP**: a coder's slice hit a brief-named stop condition; nothing proceeds until the owner rules.
