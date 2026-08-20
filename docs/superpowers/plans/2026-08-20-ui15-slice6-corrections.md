<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 6 — CORRECTIONS after a QG HOLD · dispatch brief · 2026-08-20

**Status: DISPATCHED. Two gate blockers ([[B226]]/[[B227]]) + one hot-path fix ([[B228]]) + one stale comment, all
verified at the code.** ⛔ **NO DEVICE CONTACT.** ★ The slice's core passed — the four-term rule, its 25 mutations,
the store matrix, both probe arms' functional checks. ⛔ Build on the uncommitted slice 6 tree (HEAD `7976ee5` +
slice 6); revert nothing.

---
## Blocker 1 — [[B226]] ⛔ `STILL JOINING` IS UNPROVED THROUGH THE REAL RENDERER
The model latch is tested; the renderer arm (`join_wait_head(st.join_still)`, `src/firmware_ui.cpp:1277`) is not —
P16 renders only the immediate `JOINING` then advances time in small steps (`probe_main.cpp:2355`).
**Hard-wiring `join_wait_head(false)` in production would likely leave every gate green** — the vacuous-coverage
class, twenty-first occurrence.

**Required probe sequence (QG-ruled, verbatim in intent):**
1. advance beyond 60 s;
2. confirm the panel BLANKED but the join session remains ACTIVE;
3. send ONE short press — consumed as WAKE;
4. assert the actual OLED renderer displays **`STILL JOINING`**;
5. assert ZERO additional writes/applies;
6. **a renderer control hard-wiring `false` (or inverting `st.join_still`) goes RED** — sed matches, mutant
   compiles, probe reddens.

## Blocker 2 — [[B227]] ⛔⛔ THE RUNNER EXECUTES BACKTICKS IN A CONTROL LABEL
L19's label carries `` `/mrcfg` `` inside shell double quotes (`tools/probe_firmware_ui/run.sh:730`) — command
substitution ran `/mrcfg`, printed `No such file or directory`, and the gate still PASSED.
**Required:** labels handled as **inert data** — at minimum strip the backticks from L19; **preferably make the
runner print every label un-evaluated** (single quotes / printf `%s` of a variable assigned without expansion) so
no future label can execute. ⚠ Audit the OTHER labels for the same hazard while there (`$`, backticks). If you
harden the runner, prove it: a label containing backticks must print verbatim and execute nothing.

## Fix 3 — [[B228]] the per-push flash read
`ui_join_note_push` (`src/firmware_ui.cpp:1796`) loads `/mrcfg` before the pure kind gate rejects the push; the
session lives indefinitely after leaving the waiting screen, so the in-source "a minute or two" bound is FALSE —
fix that drifted comment too (V1).
**Required:** a cheap prefilter **`if (pu.kind != MESHROUTE_NS::PushKind::join_adopted) return;` BEFORE the load**,
with the complete four-term rule remaining the sole authority after it (⛔ the prefilter must not grow terms).
**Plus a probe:** an active session + `join_refused` and unrelated pushes ⇒ **ZERO record reads** (counted on the
store fake), and a control dropping the prefilter goes RED on that count.

## Fix 4 — the stale test header (mechanical, V1)
`test/test_firmware_ui_model.cpp:3043` still describes the B222-era surface ("activating CREATE TEAM or JOIN
NETWORK does nothing", "asserted below (`ui15-pending`)") — both stale since slices 5/6 landed the flows and
withdrew `ui15-pending`. Correct the historical description in place (state what WAS true then and what is true
now, the register's correction idiom).

## Pins
1. The B226 six-step sequence, each step asserted; the new renderer control RED.
2. The runner never evaluates a label; the L19 error line is gone from the output; a backtick-bearing label prints
   verbatim (if you harden rather than just strip, show it).
3. Zero record reads for non-adopt pushes during an active session (counted); the correlated-adopt path still
   completes (unchanged positive control); all 25 `uijoin` mutations still RED — the prefilter must not weaken the
   rule's coverage.
4. All existing functional checks and controls stay green/RED on both probe arms.

ⓘ ★★ **[[B217]]: re-pin `BASE_CASES`/`BASE_ASSERTS`** (current **1829 / 86790**) if native counts move, derivation
in place; confirm every battery you rely on RAN. Restore mutation sources exactly; `git diff --check` clean.

## Verification you run (QG re-runs the full gate — no boards, no corpus)
1. `pio test -e native` + RUN the binary — real counts, 0 failed.
2. **Full `model` battery + the three focused batteries (`uijoin`, `uiprov`, `joinprofiles`)** — QG named these
   four explicitly; RED counts + proof each ran.
3. `tools/probe_firmware_ui/run.sh` — both arms + all controls; ⛔ the output must carry NO stray shell errors.
4. `git diff --check` clean.

## Report
Each blocker's fix and its check/control names · the runner-hardening approach chosen and its proof · the prefilter
line and the zero-reads probe · the corrected comment texts (B228's bound + the stale header) · final counts, pin,
battery proofs, both probe arms · exact final `git status --short`.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ Do not touch the bug register, the bench script, any
plan/brief, `tracker.md`, `B164.md`, `docs/manual/`, or anything under `lib/`. ⛔ Evidence lands IN THE REPO.
⛔ Screen STRINGS are frozen pending the owner's ruling — the QG recommended approving the drafts, but ⛔ a
recommendation is not a ruling; change no wording in this dispatch.
