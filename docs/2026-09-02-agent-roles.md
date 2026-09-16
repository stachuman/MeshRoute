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

## Owner ruling 2026-09-15 — commits are not blocking points

**Owner, verbatim:** *"Commit are not blocking points for progress."*

**What changes:** a slice's next step (source-validation of the next brief, its implementation, the QA gate, a
brief refresh at the new base) may start on the **frozen, independently QA-passed** tree without waiting for the
owner's `git commit`. The commit remains the owner's, whenever the owner chooses (D4 unchanged: agents never
commit). What still gates progress is the **gate**, not the commit.

**What does not change:**
- A brief still names its base. When the base is uncommitted, the brief pins **the last commit hash plus the
  SHA-256 of every uncommitted input it builds on** (the frozen-handoff inventory), and the coder verifies both
  before editing — step 4's "mismatch is a STOP" applies to that pair exactly as it applied to a commit hash.
- Attribution stays per slice: a later slice must not be measured against a base that mixes in unreviewed
  edits. The frozen QA-passed state is the base; anything else on the tree is a STOP-1 to reconcile.
- Never reset, clean or discard another agent's uncommitted work; the measured-tree rule applies to the
  *frozen set*, not to `git status` being empty.
- Step 8 reads: Author lands docs on PASS; the owner commits at a time of their choosing and bench-verifies.
  Wording in briefs such as "own gate and owner commit, then QA refreshes the base" means "own gate, then
  refresh"; the commit may land before, during or after that refresh.

## Owner ruling 2026-09-16 — process optimization: symbol pins, two status homes, slice granularity

**Owner, verbatim:** *"Agreed, write the rules and pair 8a/8c"* (to the second reader's assessment that the
per-slice cost was round trips and paper, not gates: ~60 % of B365–B402 were brief/instrument findings, 7b-1
took six brief revisions and 7b-3 five, while a full independent gate runs in about an hour).

1. **Symbol pins (P4).** A brief pins `file:symbol`; any `file:line` beside it is a hint. The coder relocates by
   symbol and records the actual line in the receipt; that is not a STOP. STOP-1 is reserved for a semantic
   disagreement (the source does something other than the brief says, or an interface the brief relies on does
   not exist). Step 4's "mismatch is a STOP" keeps applying to the BASE (commit + inventory), not to line drift.
2. **Two status homes (P5).** The design's §19.1 row and the register (§0 dispatch + finding rows) are the only
   places that state a slice's current status. `tracker.md` and the repo `MEMORY.md` carry one-line pointers to
   them; the ledger holds rulings; briefs and evidence hold their own history. A brief is contract + fence + gate
   list and links to rulings instead of quoting them at length.
3. **Slice granularity (P6).** C1 (refactor XOR feature) and C4/M3 (a wire bump is its own slice) stand. Beyond
   them: a producer-free codec change (the 7b-2-0 / 7b-3-0 shape) rides inside the feature slice that consumes
   it, gated by its KATs and the independent reference; slices that share one product surface and cannot move
   the corpus are paired — **8a+8c** (controller state/crypto + local USB/BLE delivery) is one brief and one
   gate. **9 and 10 stay separate**: R-RA-6 and design §19 item 10 make the main-NV cleanup a standalone,
   separately measured NV-version change. For a `src`-only slice the independent gate re-runs native, corpus,
   boards, the touched mutation batteries and the affected probes; the full union and the six-environment census
   stay in the coder's gate. A `lib/core` or wire change keeps the full gate on both sides.

Kept unchanged: the register as the single findings log; the coder's preflight as a real second read; owner-only
rulings; the full corpus gate for anything under `lib/core`.

**Addendum 2026-09-16 (QA process error, recorded so it does not recur):** while the coder was implementing 7b-3
revision 5, QA folded R-RA-41 into the brief in place; the coder correctly stopped on the frozen-input rule (the
authorized hash `52bd0095…` no longer matched). Rule: **a brief under implementation is frozen from preflight PASS
to the freeze.** A ruling that lands mid-slice is recorded in the ledger and the register only; the brief is
refreshed at the next checkpoint (a STOP report or the freeze), and QA issues an explicit re-pin message: the new
brief hash, the exact delta, and the statement that the implementation contract is or is not affected. Re-pin
issued for revision 5 at `f1d38f86…`: delta = R-RA-41 wording in §0, §2.2 and §8 only; contract unchanged.
