<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B205 — `MR_NO_POWERSAVE` ESP32 compile fix · dispatch brief · 2026-08-28

**Status: DISPATCHED.** Authority: the register row
(`docs/2026-07-30-open-bug-register.md` B205 — "Make `MR_NO_POWERSAVE` compile on ESP32 by matching the OTA
include guard to its consumer") + whatever fuller context the row's history/related notes carry — READ the
register's B205 vicinity and grep the tree for the actual defect FIRST (the exact guard mismatch, the
consumer, which envs break) and STATE the reproduction before fixing. ⛔ NO DEVICE CONTACT.

## Contract
- C1: the one guard fix. ⛔ No refactors riding along, no behaviour change beyond making the flag compile
  where its consumer exists.
- REPRODUCE first: a build (or preprocessor probe) showing the current failure with `MR_NO_POWERSAVE` on the
  affected ESP32 env(s); then the fix; then the same arm green. ⚠ SCOPED board-build exception (the build IS
  the subject): the failing env arm + the fixed arm, compute only, ⛔ no flashing; keep to the minimum envs
  that demonstrate it (+ the two owner-approved envs unchanged-check if your fix touches a shared header).
- A guard fix in a shared header can move other envs — check what else includes the touched site; if the fix
  is env-visible beyond the broken arm, measure the two approved envs (`gateway`, `heltec_mobile`)
  before/after with the B206 deterministic runner (it is landed — use it; this is exactly what it is for)
  and report the manifests.
- Native (RUN the binary) unchanged; s18-inert argument (state what the sim compiles vs your touched files);
  no battery unless a `TARGET_SRC` file moves (verify, state); `git diff --check` clean.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ No docs/tracker/platformio.ini(unless the fix IS an
  env flag — if so, say why and keep it minimal)/parallel-session files. No pollers; never pipe the runner.
  Metal residue: none expected (a compile fix) — say so, or draft the one line if a powersave-on-glass check
  genuinely needs metal.

## Report
The reproduced failure (exact error, env, guard site) · the fix with the guard-to-consumer match explained ·
the fixed-arm proof · the shared-header impact check (with runner manifests if owed) · native counts ·
exact final `git status --short`.
