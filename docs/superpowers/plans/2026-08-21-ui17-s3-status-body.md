<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-17 S3 — STATUS body · dispatch brief · 2026-08-21

**Status: DISPATCHED. The APPROVED spec is the AUTHORITY:**
`docs/superpowers/specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md` — implement its **S3** slice
exactly (scope, geometry, pins, mutation classes, the new pure header it names). Key ruled facts S3 carries:
the 24×24 mark slot with the `draw_rect` placeholder · the narrowed `x=40` text geometry then full-width rows ·
the five facts with their DETERMINISTIC substitution/priority table · **`4 KNOWN`** (⛔ never "heard" — route
evidence) · **R-3: NO config text returns to STATUS** (badge + SETTINGS words; only `RESTART NEEDED` competes for
its row) · the accepted drops (battery mV, per-kind newest ages) with today's STATUS body content inventoried,
nothing lost silently. ⛔ **NO DEVICE CONTACT.** Build on the current tree (S1+S2+B217/B235 QG-passed,
uncommitted); revert nothing.

## Operational contract (as S2's, unchanged)
- ⛔ S3 ONLY (C1). S1/S2 behaviours and mutations survive (entries RED, cases GREEN).
- ⚠ The spec pins TWO probe assertions as **re-pointed, never weakened**: `P14f` (`body_text_min_x() == 12`) and
  `P14a` (`bitmaps_on_page(0) == 11` — `draw_rect` counts). Honour that exactly as the spec states it.
- Batteries: iterate on changed entries; ONE full pass of every touched target before reporting; sync the
  `PIN_*` cross-check when counts move (derivation in place); harness refusals (exit 6/7/8) mean fix the
  invocation. Never probe+battery concurrently; wait-loops on runner markers, never pgrep.
- Strings: the spec's inventory (incl. the ruled substitutions) governs; a lexeme it lacks ⇒ STOP and report.
  Spec-vs-tree conflict the spec does not resolve ⇒ STOP and report.
- Verification you run (QG runs the full gate — no boards, no corpus): native (RUN the binary) · touched-target
  batteries (a NEW pure header gets its own target, the house shape) · `probe_firmware_ui` both arms ·
  `git diff --check` clean.
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ Do not touch the register, the bench script, any
  plan/brief, the design doc, the SPEC, `tracker.md`, `B164.md`, `docs/manual/`, or anything under `lib/`.
  Design-doc corrections S3 forces: DRAFT in your report (the STATUS-paragraph removal text is already drafted —
  extend only if the implementation forces more).
- RAM/flash: measure per the spec's resource plan (sizeof deltas; the placeholder's cost), report measured.

## Report
Every S3 pin with case name and match count · the mutation ledger (new target + entries, full-pass proof,
survivors) · the substitution/priority table's cases · native counts + cross-check sync · probe proofs (both
pinned assertions' re-points shown) · measured RAM/flash deltas · any drafted doc text · exact final
`git status --short`.
