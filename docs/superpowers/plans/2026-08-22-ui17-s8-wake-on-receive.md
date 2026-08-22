<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-17 S8 — wake-on-receive · dispatch brief · 2026-08-22

**Status: DISPATCHED. The APPROVED spec is the AUTHORITY:**
`docs/superpowers/specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md` — implement its **S8** slice
exactly, under rulings **R-6/R-7**: the OLED wakes when **(a) a DM is delivered to us** (`msg_recv`, sealed or
not) or **(b) a channel post that arrived SEALED and was OPENED WITH OUR TEAM KEY** (`channel_recv` gated on
**`pu.enc == true`** — a CLEARTEXT post on a matching channel must NOT wake; the §8.15 stranger rule survives BY
CONSTRUCTION). No rate limiter (R-6, accept-for-v1; the cost stays stated and is measured at the spec's §7.8
step). The spec names the mechanism: one call per arm of the pure router `mrui::ui_route_recv_push`
(`firmware_ui_send.h`) — ⛔ not `mr_ui_on_push` (§B115); a **separate `_msg_wake_until_ms` deadline +
`wake_active()`** beside `hold_active()` — **the wake never writes `_last_input_ms`**, so S2 and S8 stay
order-independent; window = `kBlankMs` from the message. ⛔ A push NEVER navigates (M87/B233); ⛔ no emergency
field moves; the quiet-node idle-sleep guard must still pass. The spec's S8 section carries the 9 pins, 6+
mutation classes (incl. the enc-gate-dropped headline and the gate-copied-onto-msg_recv half-applied shape), the
new **`uisend`** battery target, the probe wake phase + zero-bus negative arm, and the ONE authorised `lib/`
exception: the drifted `command.h:321` comment (comment-only; s18 inert; the md5 re-run is QG's per D2).
⛔ **NO DEVICE CONTACT.** Build on the current tree (S1-S5 all passed); revert nothing.

## Operational contract (as S5's)
- ⛔ S8 ONLY (C1) + the one comment repair. S1-S5 behaviours/mutations survive (frozen-frame, retention incl.
  the page rules, the S4/S5 repaint gates — the wake must compose with ALL of them; the spec's pins say how).
- The handoff seam covered WITH the unit (the standing lesson): the real router driven with real pushes — a
  sealed post wakes, the SAME post cleartext does not, a DM wakes regardless of `enc`, every other `PushKind`
  wakes nothing; the blanked panel lights on the CURRENT screen with the interaction preserved; counters/dirty
  behaviour unchanged on every arm (the gate governs the WAKE only — a cleartext post still counts as unread).
- Batteries: iterate; ONE full pass per touched target; the new `uisend` target per the spec; sync `PIN_*` on
  moves; exit 6/7/8 = fix the invocation; never probe+battery concurrently; wait-loops on runner markers.
- Verification you run (QG runs the full gate): native (RUN the binary) · touched-target batteries · the probe
  both arms all controls (B237-hardened; no abnormal/silent) · `git diff --check` clean · `git diff -- lib/`
  shows EXACTLY the one comment line and nothing else.
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ Do not touch the register, the bench script, any
  plan/brief, the design doc, the SPEC, `tracker.md`, `B164.md`, `docs/manual/`, or any `lib/` line beyond the
  ONE authorised comment. Doc corrections S8 forces (design §8.15 area should need NONE — the scope preserves
  it; verify and say so): DRAFT in the report.

## Report
Every S8 pin with case name and match count · the handoff checks with controls RED (the cleartext-vs-sealed pair
explicit) · the sleep-guard proof · the `lib/` diff shown to be the one comment · mutation ledger + full-pass
proofs · native counts + cross-check sync · probe proofs · measured resources (should be ~zero — measure) · any
drafted doc text · exact final `git status --short`.
