<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B186a + HAL AUDIT + §B188 — honest TX outcomes, bounded · dispatch brief · 2026-08-12

**Status: DISPATCHED 2026-08-12 on an owner ruling (ledger §1.20).** ★ Role split: the QA-gate wrote this brief and
verifies your claims at the code; **the OWNER runs QG and rules.**
⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out another commit here** —
the tree carries the uncommitted mobile-home arc.

★★★ **THE OBJECTIVE, in the owner's framing: HONEST TX OUTCOMES AND EVIDENCE FOR RECOVERY.**
⛔ **NOT forcing `s07` to reattach. ⛔ NOT creating a generic retry mechanism. ⛔ NOT "fixing `s07`". ⛔ NOT a broad
redesign of mobile attachment.** This slice is **bounded**; every prohibition below is owner-ruled.

**Baseline:** HEAD **`c7bca52`**; native **1512 / 81212 / 0**; `lus` **`316b9cb1`**; corpus 36 rows.
⛔ **The delivery floor is FROZEN and UNRATIFIED** — you are not gated on a delivery number and must not propose one.

---
## 1 — B186a: distinct TX identities (implement FIRST)

**Give mobile DISCOVER, OFFER, initial CLAIM and re-CLAIM distinct INTERNAL TX identities.**

- ⛔ **NO wire-format change.** These are internal tags, not frame bytes.
- ⛔ **NO generic beacon retries.** ⚠ This is the trap: the defect looks like "beacons aren't retried", and the fix that
  suggests is exactly what the owner forbade. **You are making the outcome VISIBLE, not recovering it.**
- ⛔ **Ordinary beacons and floods stay UNCHANGED.**
- ★★ **CAPTURE initial-CLAIM versus re-CLAIM *BEFORE* TX HANDOFF. ⛔ Do NOT reconstruct it afterwards from FSM state.**
  ⇒ This is the arc's most-repeated defect class — *a fact is established by the act, never reconstructed later*. By the
  time the refusal callback fires, the FSM may have moved, and a reconstructed "was this a re-CLAIM?" would be a
  **false attribution wearing a diagnostic's name.**
- **On asynchronous TX refusal, report the exact mobile operation, the reason, the SF and `busy_until`.**
  ⓘ **Verified before dispatch — you need NO struct change for this:**
  `struct BusyInfo { BusyReason reason; uint16_t tag; int16_t sf; uint64_t busy_until_ms; }` (`lib/core/hal.h:40`)
  already carries **reason, tag, SF and `busy_until_ms`**. The new identity rides the existing **`tag`**.
- ⛔⛔ **`BusyInfo` has NO length field. DO NOT add length plumbing in this slice** (owner-ruled).

### Tests — positive AND negative for **every** subtype
⚠ **Including a TELEMETRY-DISABLED COMPILATION test.** ⇒ This is [[B169]]'s exact shape: board envs define
`MESHROUTE_NO_TELEMETRY`, which expands `MR_TELEMETRY` to `do {} while (0)` and **deletes the body**, orphaning any
variable whose only consumer is inside an `MR_EMIT`. That produced a `-Wunused-variable` on all ten board envs which
was **invisible to native and to all 36 corpus streams by construction** and survived four slices. **You are adding
emits: assume you will reproduce it unless you test for it.**
★ Every new assertion mutation-proven, match counts printed. ⛔ A probe at match count 1 you did not mutate is not
evidence — this arc has **23** recorded instruments that could not fail.

---
## 2 — The HAL reachability audit (BEFORE any B186b work)

**Enumerate EVERY HAL implementation** and establish, per implementation, whether it can **accept a frame and LATER
invoke `on_radio_busy`.**
- ★ **Distinguish simulator-only behaviour from behaviour reachable on physical boards.**
- ⛔⛔ **RECORD EVIDENCE, NOT ASSUMPTIONS.** ⚠ The known asymmetry: the simulator's `_hal.tx()` answers **ok** and
  refuses later via `onRadioBusy`, whereas real `DeviceHal::tx` refuses **synchronously** and may produce
  `tx_hal_rejected` — a **different path this arc has never measured.** ⇒ **Whether the two are equivalent is exactly
  the open question; do not assert either way without evidence.**
- **Cover every implementation, including test fakes** — a fake that cannot express late refusal means the tests
  around it prove nothing about the real path, and that is worth stating.

---
## 3 — B188: the rolling-window coverage gap (separate slice, same dispatch)

**A purpose-built fixture with an explicitly compressed `duty_cycle_window_ms`**, verifying:
1. **refusal at exhaustion**; 2. **correct `busy_until`**; 3. **incremental rolling expiry**; 4. **resumed transmission
once budget becomes available.**

⛔⛔ **DO NOT shorten `s07`'s duty window. DO NOT lengthen the general corpus to exercise rollover.** (Owner-ruled;
ledger §1.19 retains the 1-hour default and §1.20 forbids retuning `s07`.)
ⓘ **Why the gap exists — measured: 36 of 36 corpus scenarios are ONE-SHOT** (none sets `duty_cycle_window_ms`, all
inherit the 1 h default, every duration ≤ 1 h), so **the moment a rolling window RECLAIMS budget is untested corpus-wide.**
★ **That is why B183's busy-refusal path stayed invisible: a budget that never rolls is entered once and never left.**
⇒ Prefer the durable-fixture pattern already established: keep it **NON-CORPUS**, under
`docs/superpowers/evidence/` with a verifier that **exits nonzero**, not in `simulation/` and not in any anchor table.

---
## 4 — ⛔ DO NOT IMPLEMENT B186b RECOVERY. Report instead.

After B186a and the audit, **report**:
1. **which paths are simulator-only and which are hardware-reachable**;
2. **corpus movement caused ONLY by the new diagnostics** — ⚠ new emits add stream lines, so movement is expected;
   **attribute it to the diagnostics and nothing else**, and ⛔ **do NOT edit the anchor table** (re-anchoring is the
   owner's ruling, and **no `s07` re-anchor is authorised by this slice**);
3. **a MINIMAL recovery proposal per reachable mobile operation** — a proposal, ⛔ not an implementation.

---
## 5 — Preserved rulings (owner-ruled; violating any of these fails the slice)

- **`s07` saturation is LEGITIMATE STRESS BEHAVIOUR, not a defect** (ledger §1.19).
- ⛔ **Do not change `s07`'s load or window, and do not re-anchor it in this slice.**
- ⛔ **The delivery floor remains FROZEN / UNRATIFIED.** Report numbers; never a floor.
- **[[B187]] stands as expected saturation / reframed; [[B188]] owns the missing rolling-window coverage.**

---
## 6 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   From **1512 / 81212 / 0**.
2. **Rebuild `lus`; print its md5 beside every corpus figure.** From **`316b9cb1`**. ⚠ A stale `lus` reports the previous
   slice's streams and looks exactly like "nothing moved" — that has already produced one false conclusion here.
3. **All 36 rows**, 0 assertion failures, with §4.2's attribution.
4. ★★ **Answer D2 EXPLICITLY.** `sizeof(Node)` is **221880**. ⚠ **This slice is a likely D2 trigger: capturing
   initial-CLAIM vs re-CLAIM before hand-off may want state.** Prefer deriving from what exists over storing. If
   `sizeof(Node)`, a carrier size, a board `#if` or the linker moves, you owe the **ten-env sweep +
   `warning_census.sh` + `-Wreorder` + the `sizeof`/`offsetof` asserts + per-board RAM/flash diffs.**
   ⛔ **Run `warning_census.sh` either way** — you are adding emits, and [[B169]] was board-only and invisible to
   native and the corpus.
5. ⛔ **Zero free timer ids** (`TimerWheel::kCap == 91`, all consumed). Allocate none.
6. **Mutation-prove every new assertion; print match counts.**

---
## 7 — Method

- ★★ **A fact is established by the physical act, never reconstructed later** — six recorded sites, and §1's
  before-hand-off requirement is the seventh. ⚠ `_hal.tx()` returns `ok` on **ENQUEUE** (`lib/hal/device_hal.cpp:10-12`)
  ⇒ `handed` means **queued**, never **aired**; `tx_initiating`/`tx_with_retry` return TRUE-ish for a **deferred** frame.
- ★★ **Identity is the whole tuple.** Four operations sharing one tag is the defect you are removing; do not replace it
  with three sharing one.
- ★★ **Instruments that cannot fail — 23 instances.** The newest was a *durable evidence fixture* with empty `expect`
  arrays and no `sys.exit`. **Ask of every new emit and every new test: could this have come out otherwise?**
- ★ **A correction placed anywhere but the instruction a reader follows** — eleven-plus sites.
- ⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval; **never quote an owner ruling** —
  reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation.**

**Report:** B186a's four identities with `file:line` and where each is captured relative to hand-off · the refusal
report's fields · every subtype's positive/negative test **plus the telemetry-disabled compilation result** · the HAL
audit table (per implementation: can it accept-then-refuse? simulator-only or hardware-reachable? evidence) · B188's
fixture, its four verifications and its verifier's exit codes · the corpus table **with the `lus` md5** and diagnostics-only
attribution · the **D2 answer** and `warning_census.sh` result · the **minimal recovery proposals** (proposals only) ·
exact final `git status --short` and that nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** the four identities cannot be distinguished without a wire change ·
initial-vs-re-CLAIM cannot be captured before hand-off without new `Node` state you cannot justify · a corpus row moves
for a reason other than the new diagnostics · `sizeof(Node)` moves and you cannot run the ten-env sweep · or the HAL
audit cannot establish reachability for some implementation (⇒ **record the gap, do not assume**).
