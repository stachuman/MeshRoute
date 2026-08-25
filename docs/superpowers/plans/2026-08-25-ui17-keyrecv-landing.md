<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI — the `TEAM KEY RECEIVED` ack lands on STATUS · dispatch brief · 2026-08-25

**Status: DISPATCHED 2026-08-25 (K5 QG-passed — the file conflict cleared).** **Owner-ruled 2026-08-25,
shape (a):** acknowledging the **`TEAM KEY RECEIVED`** result jumps to the **main STATUS screen**,
⛔ not back into the provision flow — after receiving a key the operator's next question is "am I set up?",
which is STATUS's job. ⛔ No new row, no new lexeme (shape (b) was considered and rejected — a `BACK TO MAIN`
row would need a ruling and break the shared terminal shape). ⛔ **NO DEVICE CONTACT.**

## Scope (deliberately narrow — the owner's ask, verbatim)
- **ONLY the `team_key_received` success note's acknowledgement** (K4's arm — the durable note wired by
  K3/K4b). Every OTHER terminal result keeps its landed landing byte-for-byte: `TEAM JOINED`, `ADOPTED`,
  `TEAM CREATED`, every refusal, and **the failure pair** (`TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT`)
  — a failed save leaves the operator where remedies are, not on STATUS. ⓘ The consistency extension (all
  happy-path terminals → STATUS) is a possible future owner ruling, ⛔ not this slice.
- The landing is the OPERATOR'S press choosing a destination — the ⛔ a-push-never-navigates ruling (K4 pin 3)
  is untouched: the note's ARRIVAL still navigates nothing, wakes nothing.
- The jump lands on **passive STATUS** per the UI-17 navigation contract (a top-level screen in its passive
  state, cursor semantics as the contract rules them).

## Pins
(1) Ack of the success note (either press) ⇒ **STATUS, passive**. (2) The note's arrival still changes no
screen/cursor (the landed K4 pin, re-proven through the new landing). (3) The failure pair's ack ⇒ the LANDED
landing, unchanged. (4) Every other terminal's ack ⇒ unchanged (drive the neighbours). (5) The landed
"ack lands on the menu" cases for THIS arm: updated with the **correction idiom** — withdrawn expectation kept
visible, ⛔ never deleted. (6) Blank/wake retention on the new landing follows the landed STATUS rules.
- Mutations (each RED at match count 1): the landing reverted to the menu (the ask undone — the headline) ·
  the FAILURE pair's ack also jumping to STATUS (the scope overrun) · the note's ARRIVAL navigating (the K4
  control re-proven through the new path).

## Operational contract (standing)
- C1: this landing only. Everything landed survives (through K5). Batteries (parallel runner, per its header):
  iterate; ONE full pass per touched target (`model` at least, `uisend` if touched); sync `PIN_*` with
  derivation; ⛔ never pipe the runner; ⛔ no background pollers; ⛔ never edit a battery's target mid-run.
- Probe: the P15k success arm's post-ack expectation moves (STATUS, passive); the P15k2 failure arm's must NOT
  move — both re-proven; controls RED.
- Verification you run (QG runs boards; `src/`-only ⇒ s18-inert by construction — say so): native (RUN the
  binary) · touched-target batteries · both probes green · `git diff --check` clean · `git diff -- lib/`
  EMPTY.
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ No docs, no plans/briefs/specs/register/bench,
  no parallel-session files. Metal residue: ONE line for the bench draft (on glass, the ack of a real received
  key lands on STATUS; a failed save's ack does not) — DRAFT in report.

## Report
The landing change's exact site · pins 1-6 with case names and match counts · the three mutations RED ·
full-pass proofs · native + PIN derivation · probe proofs · the one-line bench draft · exact final
`git status --short`.
