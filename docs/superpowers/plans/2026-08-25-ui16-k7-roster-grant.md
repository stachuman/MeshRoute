<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-16 slice 11 — the roster grant (K7, [[B245]]) · dispatch brief · 2026-08-25

**Status: ✅ COMPLETE — QG-PASSED 2026-08-25 (combined K6+K7 gate); [[B245]] CLOSED. Metal residue = bench
Part 45.** ⓘ QG wording correction, recorded: the contract line "`git diff -- lib/` EMPTY" is read as *K7
itself adds no library change*; the combined K6+K7 diff intentionally contains exactly `lib/hal/mr_ui.h`
(K6's, per the spec §0 amended boundary). **The APPROVED spec is the
AUTHORITY:** `docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md` **§K7** (immediately before
§5) — owner-ruled 2026-08-25 as option 1 of [[B245]]. Read it IN FULL, plus §N5/§N6 (the machinery this slice
is an ENTRY POINT to — ⛔ nothing re-implemented), the §UI-17 spec's TEAM passive/entered contract, and the
B245 register row. ⛔ **NO DEVICE CONTACT.**

## The one-sentence slice
An operator-initiated per-member act on the **entered TEAM screen's roster** that opens the **landed N5/N6
chain verbatim** (preflight → REQUEST PUBKEY ceremony → GRANT KEY confirmation → dispatch-truth outcomes +
`{dst, ctr}` correlation) — so a member who joined before any invite window opened (the B245 bench repro) is
grantable from the panel.

## Non-negotiables (spec §K7, quoted)
- ⛔ No new screen, lexeme, send path, or state machine — an entry point only; a piece that cannot be reached
  verbatim ⇒ STOP and report.
- The invite window's F-11 diff + F-13 handled set stay **byte-for-byte** (its landed cases and controls
  re-run untouched — pin 3); P-12 stays whole (the operator navigates and acts; nothing is unsolicited).
- Target = the roster row's **`key_hash32`** (the TEAM chain's one resolution), ⛔ never name/index; P-7c/P-7d
  re-proven through this entry. ⛔ No self-grant; a keyless node offers nothing (placement of the hide-vs-
  refuse choice is a REPORTED design decision, as is where the act hangs in the entered-TEAM model).
- Pins 1-8 and the mutation classes as the section lists them — the headline is **the act auto-issuing on
  row selection** (the no-unsolicited shape) and **a second outcome mapping forked** (anchor on the reused
  call).

## Operational contract (standing)
- C1: K7 only. Everything landed survives (through K6). Batteries (parallel runner, per its header): iterate;
  ONE full pass per touched target; sync `PIN_*` with derivation; ⛔ never pipe; ⛔ no pollers; ⛔ never edit a
  target mid-run. Handoff seam WITH the unit: the probe drives the early-joiner repro end-to-end through the
  REAL services (a member present before any window ⇒ selected on TEAM ⇒ granted, `GRANT QUEUED`→`KEY SENT`
  on the real edge where the harness allows); controls RED.
- Verification you run (QG runs boards; `src/`-only ⇒ s18-inert by construction — say so): native (RUN the
  binary) · touched-target batteries · both probes green · `git diff --check` clean · `git diff -- lib/`
  EMPTY.
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ No docs/plans/specs/register/bench/tracker/
  platformio.ini/ccache_native.py/parallel-session files. Metal residue (the B245 repro on glass: create →
  immediate join → roster grant, plus the invite window unchanged beside it): DRAFT in your report.

## Report
The two design decisions (where the act hangs · hide-vs-refuse when keyless) · every pin (1-8) with case name
and match count · the invite-window untouched proof · the mutation ledger + full passes · native + PIN
derivation · probe proofs · measured resources · the DRAFTED bench residue · exact final `git status --short`.
