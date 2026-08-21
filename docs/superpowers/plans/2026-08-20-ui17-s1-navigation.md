<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-17 S1 — TEAM/INBOX navigation migration · dispatch brief · 2026-08-20

**Status: DISPATCHED. The APPROVED spec is the AUTHORITY:**
`docs/superpowers/specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md` — owner-approved 2026-08-20 with
ZERO open questions. **Implement its S1 slice exactly: scope, pins, mutation classes, probe/battery impact as the
spec states them.** This brief adds only the operational contract. ⛔ **NO DEVICE CONTACT.** Build on current
HEAD + any uncommitted QG-passed work; revert nothing.

## Operational contract (standing, unchanged from the UI-15/16 cadence)
- ⛔ S1 ONLY — no other spec slice rides along (C1); the spec's slice boundary is the scope.
- The B232-landed SETTINGS idiom is the pattern; the B231/B233/B232 behaviours and their mutations must SURVIVE
  the migration (existing entries stay RED, existing cases GREEN — the spec's own pin).
- Strings: the spec's inventory governs; any lexeme S1 needs that the spec does not carry ⇒ STOP and report.
- Probe press sequences change (TEAM/INBOX now enter by `double`) — update `probe_firmware_ui` phases per the
  spec; both arms + all controls green/RED, no stray shell output.
- Drafts in your REPORT, ⛔ never landed by you: any design-doc correction S1 forces beyond what the spec already
  drafted; any bench-script press-sequence note (the arc's metal lives in the spec's §7 walkthrough — verify §7
  covers S1's residue and draft the gap if you find one).
- ★★ [[B217]]: re-pin `BASE_CASES`/`BASE_ASSERTS` (READ the current pin from the harness) when counts move,
  derivation in place; confirm every battery RAN; restore mutation sources exactly. ⚠ Never run a probe and a
  battery concurrently.
- Verification you run (QG runs the full gate — no boards, no corpus): native (RUN the binary, real counts,
  0 failed) · every battery whose target source you touch · `tools/probe_firmware_ui/run.sh` both arms ·
  `git diff --check` clean.
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ Do not touch the register, the bench script, any
  plan/brief, the design doc, the SPEC itself, `tracker.md`, `B164.md`, `docs/manual/`, or anything under `lib/`.
- ⛔ Any conflict between the spec and the as-built tree the spec does not already resolve ⇒ STOP and report.

## Report
Every S1 pin with its case name and match count · the mutation ledger (new entries, each RED at match count 1;
survived entries confirmed) · final native counts and the pin · battery/probe proofs · any drafted doc text ·
exact final `git status --short`.
