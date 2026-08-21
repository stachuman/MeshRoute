<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-17 S2 — §3.3 retention conformance · dispatch brief · 2026-08-21

**Status: DISPATCHED. The APPROVED spec is the AUTHORITY:**
`docs/superpowers/specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md` — implement its **S2** slice
exactly (scope, pins, mutation classes). S2 is **RESOLVED by ruling R-1** (owner, 2026-08-20): §3.3 wins for BOTH
the compose sub-view and the inbox detail modal — **the two `kBlankMs` auto-closes are DELETED**; blanking is a
power action that preserves the interaction and its stable selection; the consumed wake press restores it; only
explicit `BACK`, a completed terminal operation, or the emergency transitions (`long_fire` closes compose,
`long_arm` closes the detail modal) may retire it. ⛔ **NO DEVICE CONTACT.** Build on the current tree (S1 +
B217/B235 QG-passed, uncommitted); revert nothing.

## Operational contract (updated 2026-08-21 — the harness changed)
- ⛔ S2 ONLY (C1). S1's landed behaviours and mutations survive (entries RED, cases GREEN).
- **Batteries:** iterate on new/changed entries; **ONE full pass of every touched target before reporting** (QA
  re-runs at the gate). ★ **The re-pin duty is RETIRED ([[B217]] closed):** the baseline is now DERIVED per run.
  When native counts move, **update the `PIN_CASES`/`PIN_ASSERTS` cross-check** so the stale-pin banner stays
  meaningful (it can no longer disarm anything — but a normalised warning is a dead instrument). ⓘ The harness now
  REFUSES unknown flags (exit 7), unknown targets (6) and zero-match prefixes (8) — a refusal is yours to fix,
  never to work around.
- ⚠ Never run a probe and a battery concurrently; wait-loops use the runner's own output markers, never pgrep.
- Verification you run (QG runs the full gate — no boards, no corpus): native (RUN the binary, real counts,
  0 failed) · touched-target batteries · `probe_firmware_ui` both arms if its expectations move (the blank/wake
  phases likely do) · `git diff --check` clean.
- Strings: the spec's inventory governs; a lexeme it lacks ⇒ STOP and report. Spec-vs-tree conflict the spec
  does not resolve ⇒ STOP and report.
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ Do not touch the register, the bench script, any
  plan/brief, the design doc, the SPEC, `tracker.md`, `B164.md`, `docs/manual/`, or anything under `lib/`.
  (The design doc's §3.2.1/§3.5 corrections are ALREADY DRAFTED and land supervisor-side on your PASS.)

## Report
Every S2 pin with case name and match count · the mutation ledger (new RED at match count 1; survivors confirmed;
the full-pass proof) · native counts + the cross-check sync if counts moved · probe proofs if touched · exact
final `git status --short`.
