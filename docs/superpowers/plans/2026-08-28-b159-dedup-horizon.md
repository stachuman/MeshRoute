<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B159 — de-duplication vs the retry horizon · dispatch brief · 2026-08-28

**Status: DISPATCHED (the owner-ruled pipeline, after B20/B21).** Authority: the register row **[[B159]]**
("DATA de-duplication can expire inside the retry horizon and deliver a retry twice" — OPEN/CORRECTNESS,
`docs/2026-07-30-open-bug-register.md:102`). Context to read: the hybrid-RTS design's completed-flight cache
(`docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md` — its cache semantics section) and
the B251 notes that deliberately kept B159 separate ("duplicate application deliveries, while keeping B159
separate"). ⛔⛔ **A `lib/core` slice — full corpus obligations.** ⛔ NO DEVICE CONTACT. ⛔ B134/custody later;
⛔ B35 is a separate row (the owner's pipeline skips it for now — do not fold it in).

## Phase 0 — REPRODUCE AND MEASURE before any fix
- Find the ACTUAL structures and horizons (V1, `file:line`): the DATA de-dup state (the completed-flight
  cache and/or any seen-set keyed on the flight identity), its expiry/TTL/eviction (capacity-based? timed?),
  and EVERY retry horizon that can outlive it — the sender's link-level retry schedule, the e2e-ack timeout
  driven resend paths (the 300 s `e2e_ack_deadline_xl_ms` era), the parked/deferred resend arms, and the
  delegated/home-translated retries (B251's boundary). Tabulate: worst-case retry arrival time vs the
  de-dup state's guaranteed-retention time, per path. The defect = any path where retention < horizon.
- Reproduce natively through the PUBLIC path: a DATA delivered once → the de-dup entry expired/evicted
  (drive time, or capacity pressure if eviction is the real mechanism — measure WHICH) → the retry arrives
  → **the application delivers TWICE**. Pin the double delivery at the app layer (inbox/push), not just the
  cache miss. If capacity eviction is reachable earlier than time expiry, reproduce BOTH shapes.
- Measure the corpus: do any scenarios exhibit double application deliveries today (the B251-era gate
  reported "duplicate application deliveries" as a tracked figure — find the current number and which
  streams)? The fix's predicted effect on that figure, stated before QG runs it.

## Phase 1 — the fix (design constraints, not a prescription)
- The invariant to establish: **within every retry horizon the dedup answer is guaranteed** — either the
  retention outlives the longest horizon (sizing/TTL fix), or the retry itself carries/refreshes what dedup
  needs, or the horizon is bounded to the retention. Pick from the MEASURED table, justify against the
  alternatives, and mind RAM (any cache growth: `sizeof(Node)` moves ⇒ the deliberate-assert discipline +
  the standing `tools/probe_board_abi.py` re-pin + the certified board measurement with attribution).
- ⛔ The scope stays DATA application-dedup (the row's words). The hybrid identity/cache semantics that
  B153/B157 closed on may be REUSED but not redesigned; if the fix would move them, STOP and report.
- C2 throughout: no silent drop of a legitimate first delivery to protect against the duplicate; expiry
  behaviour must stay honest for genuinely-new flights reusing an identity after the horizon (the wrap
  cases — the B239 deadline-wrap lesson applies to any new `now < deadline`).
- Fail-loud accounting: if a duplicate IS suppressed by the fix, the existing telemetry shape for it (find
  it) counts the suppression; no invented emit unless none exists (then report the candidate).

## Tests / mutations / gates
- The reproductions flipped (both shapes if both exist); the boundary driven both sides (retention−ε ⇒
  suppressed, horizon edge ⇒ suppressed, genuinely-new-after-horizon ⇒ delivered); the wrap case for any
  new time comparison; capacity pressure if eviction is real.
- Mutations (the `lib/core` targets per the b20/b161/b251 precedent, each RED at match count 1): the
  retention fix reverted (the defect verbatim) · off-by-one at the horizon edge · the wrap guard dropped ·
  the genuinely-new-flight arm suppressed (over-correction) · eviction re-admitted under pressure (if
  capacity is part of the fix).
- Corpus/D2: the occurrence measurement + per-stream prediction BEFORE the run; the s18 keystone from
  `simulation/BASELINE.md`; QG runs the 36. Boards via the certified runner if RAM/flash moves; the ABI
  probe for any struct move. Native (RUN the binary, PIN derivation), touched batteries full pass,
  `git diff --check` clean, warnings unchanged.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ No docs (protocol.md's dedup prose correction, if
  drifted, DRAFTED in the report — supervisor lands), no `tracker.md`, no `platformio.ini`, no
  parallel-session files. No pollers; never pipe the runner. Metal residue: DRAFT the real-air check if the
  horizon math depends on anything host-unreachable (likely none — say so).

## Report
The horizon-vs-retention table (`file:line` per path) · which shape(s) reproduce and the flipped cases ·
the chosen fix with the justification against alternatives · the corpus occurrence + prediction · the
mutation ledger + full passes · native + PIN · any board/ABI measurements attributed · the DRAFTED doc
corrections · exact final `git status --short`.
