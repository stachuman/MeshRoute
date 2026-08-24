<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-16 slice 7b — the grant's dispatch truth (N6b, corrective) · dispatch brief · 2026-08-24

**Status: DISPATCHED. This is N6's QG-ruled corrective slice.** The AUTHORITY is
`docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md` §4-N6, **specifically its 2026-08-24
correction blockquote** (after the correlation bullet) + the corrected **F-13** (§1.7) + §8 **S-37/S-38**.
⛔⛔ **THIS SLICE TOUCHES `lib/core`** — the corpus obligations below are not optional. ⛔ **NO DEVICE CONTACT.**

## The two blockers (QG-measured; verify each site before editing — V1)
1. **`GRANT QUEUED`/`GRANT PARKED` infer more than the core guarantees.** A full TX queue silently drops the
   frame but still returns a non-zero counter (`lib/core/node_mac.cpp:340`); a full parked-send ring stores
   nothing (`node_hashlocate.cpp:1891`); yet `team_key_grant_send()` returns `queued` in every case
   (`node.cpp:231`). Either word can currently be false.
2. **The TxDone correlation stores the frozen roster id, not the send-time destination.** The UI records the
   old selection's `dst_id` (`src/firmware_ui_invite.h:616`) while the core resolves the hash against the
   CURRENT binding at send (`node_hashlocate.cpp:1605`) — a re-DAD between selection and confirmation airs the
   grant to the new id and the `send_aired` never correlates; the screen stays `GRANT QUEUED` forever.

## The ruled fix
- **Core:** `team_key_grant_send` (and whatever it delegates to) returns an **EXPLICIT dispatch result** —
  *actually queued* · *actually parked (stored)* · *a distinct admission refusal* (full TX queue / full parked
  ring) — **plus the actually-resolved destination together with the counter**. ⛔ This is **return-value
  plumbing of facts the core already computes** — the enqueue/store results it currently discards. ⛔ **No
  behavioural change**: no drop becomes a retry, no branch moves, nothing new is emitted. If reporting a fact
  requires MOVING a branch, STOP and report. ⓘ If `TeamKeyGrantTx` gains enumerators, every `switch` over it is
  found by `-Wswitch` (gate-blocking, default-less mappers) — extend each consumer deliberately.
- **UI mapping** consumes it: actually queued ⇒ `GRANT QUEUED` · actually parked/stored ⇒ `GRANT PARKED`
  (S-37 — ⛔ ONLY for the explicit stored outcome, never inferred from `ctr == 0`) · admission refusal ⇒
  **`GRANT QUEUE FULL`** (S-38 — ⛔ never collapsed into `GRANT FAILED`, which stays the correlated in-flight
  failure's word). The correlation stores the **send-time resolved `dst`** from the result.
- **F-13:** the handled set is `REJECT`-only (spec corrected). QG measured the code as ALREADY REJECT-only —
  verify and state it; ⛔ do not add a grant-side write.

## Required new tests (QG-ruled, each a native case + its mutation)
- **Full TX queue:** the grant reports the admission refusal, ⛔ not `GRANT QUEUED`; nothing airs.
- **Full parked ring:** the grant reports the refusal, ⛔ not `GRANT PARKED`; nothing is stored.
- **Re-DAD between selection and send:** the member's id changes after the row is frozen; the grant airs to the
  NEW id and the correlated `send_aired` (carrying the send-time `dst`) **promotes to `KEY SENT`** — the
  blocker-2 scenario, now green.
- Mutations (each RED at match count 1): the `ctr == 0 ⇒ PARKED` inference restored (the blocker itself) ·
  the admission refusal laundered back into `queued` · `GRANT QUEUE FULL` collapsed into `GRANT FAILED` · the
  correlation `dst` taken from the frozen selection instead of the result.

## The corpus/D2 obligations (⛔ non-negotiable — this is a `lib/core` slice)
- **s18 inertness is an ARGUMENT you must make AND the md5 QG runs must confirm:** the change plumbs
  already-computed facts into return values, emits nothing, moves no branch — state precisely why no emitted
  event can move. **Read the CURRENT keystone from `simulation/BASELINE.md` if you cite it — NEVER assume it.**
- If `sizeof(Node)` or any core struct moves, update the assert DELIBERATELY with old→new arithmetic; report
  the native delta (the per-board diff is QG's — D2).
- Warnings: the two-env comparison (`heltec_mobile` + `gateway`) with `-Wswitch` = 0 is QG's; your half is a
  clean native build with no new warnings.

## Operational contract (standing)
- C1: this fix only. Everything landed survives (through N6 — its batteries/probe arms move only where the
  mapping's inputs changed; re-anchor, never delete). Strings: S-37/S-38 verbatim; forbidden set unchanged.
- Batteries: iterate; ONE full pass per touched target (`uiinvite`, `uisend`, `model`, + a core target if the
  pure dispatch-result logic gets one); sync `PIN_*` with derivation; ⛔ no background pollers outliving the
  final report; ⛔ never edit a battery's target mid-run; ⚠ a parallel session's board build can wipe
  `.pio/build/native` mid-battery — restore, rebuild, re-run in full.
- Verification you run: native (RUN the binary) · touched-target batteries · probe both arms all controls
  (P24a-c2 will need the new arms/words) · `git diff --check` clean · `git diff --stat -- lib/` reported
  EXACTLY (this slice's core files only).
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ Do not touch the register, the bench script, any
  plan/brief, either UI spec, the design doc, `tracker.md`, `docs/manual/`, or the parallel-session files.
  Bench Part 41 additions this fix implies (the on-glass QUEUE FULL arm, if metal-provokable): DRAFT in report.

## Report
The core result's exact shape with the plumbed sites (`node_mac.cpp:340` / `node_hashlocate.cpp:1891` facts
now reported) · the s18-inertness argument · the three new scenario cases + the four mutations RED ·
`sizeof` deltas · full-pass battery proofs · probe proofs · native counts + PIN derivation · exact
`git diff --stat -- lib/` · exact final `git status --short`.
