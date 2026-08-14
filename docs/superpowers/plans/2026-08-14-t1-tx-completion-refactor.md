<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §T1 — the TX-completion REFACTOR · **IMPLEMENTATION** dispatch brief · 2026-08-14

**Status: DISPATCHED 2026-08-14.** ★ Role split: the QA-gate wrote this brief and verifies your claims at the code;
**the OWNER runs QG and rules.**

★★★ **THIS BRIEF EXPLICITLY SUPERSEDES the design-only prohibition in
`docs/superpowers/plans/2026-08-13-b189-b164-tx-completion-design.md:4`, FOR T1 AND T1 ONLY.** You may now write
firmware — **for T1's scope as defined below and nothing else.** ⛔ **T2 and T3 remain design-only and are NOT
authorised by this brief.**

⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out another commit here.**

**Normative spec:** `docs/superpowers/specs/2026-08-13-tx-completion-path-design.md` — **read it in full**, especially
**§0** (attempt-level vs send-level), **§4.3** (the identity argument), **§10** (the slice table) and **§12** (the
eleven withdrawn claims, so you do not resurrect one). **Baseline:** HEAD **`48cd17d`**; native **1615 / 82362 / 0**;
`sizeof(Node)` **221880**; `TimerWheel::kCap` **91**, all ids consumed.

---
## 0 — Scope, from the spec's §10 row for T1. ⛔ NOTHING ELSE.

**T1 = REFACTOR ONLY:**
1. `TxOutcome` + `Node::on_tx_complete`;
2. `on_radio_busy`'s body moved **VERBATIM**;
3. the adapter (`on_radio_busy` becomes the thin caller);
4. the `TxParams` builder **taking identity as an ARGUMENT** (§4.3), routed through **all four** hand-build sites.

⛔⛔ **OUT — and building "just a bit" of them is the failure mode:** the `DeviceHal` outcome ring · `TxQEntry`'s tag ·
`pump_tx`/`service_tx` reporting · `PushKind::send_aired` · **every UI string** · the bench guide · the `NO RELAY
HEARD` rename. **Those are T2 and T3.**

★★ **C1 IS THE WHOLE POINT OF THIS SLICE: refactor XOR feature. T1 CHANGES NO BEHAVIOUR.** Every byte the node emits
must be identical afterwards — which is exactly what the gate below measures. ⇒ if you find yourself wanting to fix
something you can see is wrong, **register it and leave it** (M1).

### 0.1 — One extra item, and it is a comment
**Fix the misleading comment at `lib/core/node_mac.cpp:1872`.** It says the `duty_defer_fire` staleness guard
*"Mirrors retry_stashed"*. ⛔ **It does not:** `retry_stashed` has **no pre-transmit flight guard at all** — it
`_hal.tx()`s at `:1894` unconditionally, and only the **post-tx ACK re-arm** at `:1904` is guarded on
`_pending_tx->flight_gen == s.flight_gen`. ⇒ correct the comment to say what is true (V1: fix drifted comments you
touch). ⚠ **This is the comment whose falsehood made §4.3's identity bug easy to miss** — it is in T1 because T1 is
the slice that touches that builder.

---
## 1 — The traps, all pre-registered

- ★★ **`retry_stashed`'s frame may belong to a SUPERSEDED flight** (there is no pre-transmit guard, §0.1). ⇒ its
  identity argument is **`TxStashSlot::flight_gen`**, never the current `PendingTx`'s. §4.3's four-row table is
  normative: ordinary DATA → current `PendingTx::flight_gen` · `retry_stashed` → `s.flight_gen` · deferred carriers →
  the identity stored in that carrier · unrelated frames → **zero**. ⛔ **The builder must NEVER read current state to
  derive identity** — that is the reconstructed-fact class this whole design exists to prevent, and it would produce
  a **false confirmation**, the worst failure shape available here.
- ★★ **The moved body must be VERBATIM.** `on_radio_busy` today clears `awaiting_ack`, cancels `kAckTimeoutTimerId`,
  emits `rts_tx_blocked` / `data_tx_blocked` / `mobile_tx_refused` / `tx_giveup`, and drives `retry_slot_of` +
  `TxStashSlot::retries_left`. ⛔ **Do not "tidy" it while moving it.** A normalized verbatim diff is a gate item.
- ⚠ **`MR_TELEMETRY` / [[B169]]:** the `mobile_tx_refused` block is deliberately **inside** `MR_TELEMETRY` because
  `op` and its two name lookups exist only to fill the event; on a `MESHROUTE_NO_TELEMETRY` board build they must
  vanish **with** it. ⛔ **Moving that block out of the wrapper reproduces B169 on all ten board envs, invisible to
  native AND to all 36 corpus streams.** Run `warning_census.sh`.
- ⛔ **Zero free timer ids** (`kCap == 91`). Allocate none.
- ⛔ **No `Node` growth.** §12 records that round 2 **withdrew** the `PendingTx` de-dup bit as unnecessary (monotonicity
  makes a repeated `aired` idempotent), so `sizeof(Node)` has no reason to move. **If it moves, stop and report.**
- ⛔ **No wire change, no `wire_version` bump, no NV schema change.**

---
## 2 — Gate

★★★ **THIS SLICE TOUCHES `lib/core`, SO D2 IS LIVE AND THE `s18` KEYSTONE IS THE REAL GATE.**
1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   **From 1615 / 82362 / 0 — and for a pure refactor it must land EXACTLY there** unless you add N7, in which case
   report the delta and what it pins.
2. ★★ **`s18` must reproduce the CURRENT keystone in `simulation/BASELINE.md`.** ⛔⛔ **READ IT FROM `BASELINE.md`;
   NEVER hardcode or assume a value** (D1) — the spec quotes `9868cad3` / 269905 as a *reference figure*, and if
   `BASELINE.md` says otherwise, **`BASELINE.md` wins and you report the discrepancy.**
3. **Rebuild `lus` and print its md5 beside every corpus figure.** ⚠ A stale `lus` reports the previous slice's
   streams and looks exactly like *"nothing moved"* — that has already produced one false conclusion in this arc.
   **All 36 rows at their published values, 0 assertion failures.** ⛔ **Do not edit the `### 36/36 corpus` anchor
   table.** ⛔ **If any row moves, STOP AND REPORT — for a pure refactor a mover means the refactor was not pure.**
4. **`warning_census.sh`** with its multiset — from **PASS at 174 / 178 / 178**, `-Wswitch` **0**.
5. ★★ **Answer D2 explicitly:** `sizeof(Node)` **221880**, unmoved. Report the per-board RAM/flash diff (`heltec_v3`
   at **65.91 %**); a pure refactor should be at or near zero.
6. ★ **The normalized verbatim diff of the moved body** — this is the evidence that C1 held, so **produce it, do not
   assert it.**
7. **N7** and every new assertion **mutation-proven, match counts printed.**

---
## 3 — Method

- ★★ **A fact is established by the act, never reconstructed** — §4.3 is this rule in one function signature.
- ★★ **Instruments that cannot fail — 26 instances.** Ask of the verbatim diff and of N7: could this have come out
  otherwise?
- ★★ **Two tests that disagree about the same fact is an instrument-level defect** — that is exactly how round 3's
  blocker was caught (P1 vs N14). **Cross-check any new test against its siblings before you report green.**
- ★ **A correction placed anywhere but the instruction a reader follows** — the last four rounds each named fewer
  sites than existed. When you change a status, **grep for ALL its siblings.**
- ⛔ **PROVENANCE (ledger §3):** never claim an owner or QA approval; **never quote an owner ruling** — reported form
  only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation.**
- ⛔ **Do not describe Phase A as complete** ([[B164]]/[[B189]] gate it, and T1 closes neither). ⛔ **[[B193]] does not
  close.** ⛔ **Do not mark [[B164]]/[[B189]] fixed** — T1 is the refactor that makes the fix possible, nothing more.

**Report:** the new types and `Node::on_tx_complete` with `file:line` · the adapter · **the verbatim-diff evidence** ·
the builder's signature and **all four call sites with the identity each passes** · the corrected `:1872` comment ·
native · **`s18` against the value you READ from `BASELINE.md`, quoting both** · the `lus` md5 and 36/36 · census ·
the **D2 answer and per-board RAM/flash** · every mutation · exact final `git status --short` and that nothing was
committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** a corpus row moves · `sizeof(Node)` moves · the body cannot be moved
verbatim (⇒ **report what forced the change; a "small" behaviour edit here voids C1 and the whole gate**) · an
identity argument has no honest source at some call site (⇒ **report it; passing the current flight to make it
compile is the exact defect**) · or the spec and the code disagree (⇒ **report the conflict, do not pick a side**).

---
---
# ROUND 2 — QG HOLD, one EVIDENCE blocker (relayed by the owner 2026-08-14). ⛔ Do not commit T1.

⚠ QG's finding relayed by the owner — a recommendation, not an owner ruling (ledger §3 rule 5).
✅ **QG confirms the implementation itself is sound:** `TxOutcome` + the completion entry correctly introduced ·
`on_radio_busy` a thin, behaviour-preserving adapter · all four direct `_hal.tx()` sites using the stateless builder
with explicit identity · native 1617/82386/0 · `lus` `a66fc85d` · `s18` `9868cad3`/269905 exact · census PASS ·
`sizeof(Node)` 221880 · `git diff --check` clean. ⛔ **Do not re-open any of that.**

## R2.1 — ⛔⛔ BLOCKER: the GREEN stale-stash mutation IS testable in T1

**The report's claim that distinguishing `TxStashSlot::flight_gen` from the current pending flight *"requires a T2/T3
outcome consumer"* is INCORRECT.** ★★ The consumer the test needs is not the outcome path — **it is the HAL fake's
capture, which you already built**: `test/test_node_r3.cpp:67` does `f.seq = p.seq` at hand-off. ⇒ the identity is
observable **today**, and the site's correctness is measurable **in T1**.

**⛔ WITHDRAWN ROUND-2 METHOD — the native-only friend seam QG specified:**
1. create and stash DATA for **flight A**;
2. arm its busy retry;
3. **change/replace the pending flight generation with B**;
4. fire `kRadioBusyRetryTimerId + data_slot`;
5. **inspect the `TxParams::seq` already captured by `TestHal`.**

**The assertion must show the retransmission carries A, NOT B.** Then **mutating `lib/core/node_mac.cpp:1943` to read
the current pending generation must turn it RED.**

⇒ **⛔ WITHDRAWN ROUND-2 CONCLUSION:** *"only a native test seam"*. The proposed seam was
the kind already at `lib/core/node.h:3297`. ⓘ **Verified for you:** that block is `#ifdef`'d to the native test build
(*"zero firmware surface"*) and already holds `friend struct DualLayerTestAccess;` and `friend struct
E2eAckTestAccess;`, so **adding one more is the established idiom, not a new mechanism** (U1/U3).

### ⚠ Two ways this test can pass VACUOUSLY — check both before reporting green
- **The stash slot must still be `valid` when the retry fires.** `retry_stashed` returns early on `!s.valid`, and a
  newer same-tag TX clears the slot. **A test that accidentally clears it passes while measuring nothing.**
- **The two generations must actually DIFFER at step 4.** If the replacement did not take, `A == B` and the assertion
  is true for the wrong reason — **which is precisely the vacuity that let the site go unmeasured in the first
  place.** ⇒ **assert the difference itself**, not only the captured value.

## R2.2 — ⛔ The false claim must be corrected at ALL THREE sites QG names
- `test/test_node_r3.cpp:5417`
- `simulation/BASELINE.md:8`
- **[[B189]]**'s row, `docs/2026-07-30-open-bug-register.md:70`

★ **Correct in place, keep the withdrawn wording** (§3 rule 3). ⚠ **And grep for siblings** — the last four rounds each
named fewer sites than existed, and this claim was written into a report, a test header, a BASELINE note and a
register row in one pass.

## R2.3 — Re-gate
**Expected: 7 / 7 mutations RED.** Then **re-run native**, and **confirm the rebuilt `lus` and `s18` remain exact**
(⛔ read the keystone from `BASELINE.md`, never hardcode — D1). ⛔ **Do not commit T1.** ⛔ T2/T3 still unauthorised.
⛔ **[[B164]]/[[B189]] are not fixed; [[B193]] does not close; Phase A is not complete.**

---
# ROUND 3 — QG CORRECTION: the stale-stash A / live-flight B state is PUBLICLY REACHABLE

⚠ QG's finding relayed by the owner — a recommendation, not an owner ruling (ledger §3 rule 5).

⛔⛔ **R2.1's friend-seam method and its claim that only the state was unreachable through public APIs are
WITHDRAWN IN PLACE.** The state is reachable with production entry points and the already-tested implicit-forward
credit:

1. queue flights **A** and **B**;
2. progress A through CTS to DATA;
3. call `on_radio_busy(DATA)`, leaving A's retry stash and DATA retry timer armed;
4. feed an exact downstream-forward RTS for A through `on_recv`;
5. `implicit_ack_from_forward` closes A and `become_free()` installs/starts queued B;
6. fire A's original `kRadioBusyRetryTimerId + data_slot` timer;
7. inspect the HAL fake's captured DATA: its bytes and `TxParams::seq` are A's, never B's current identity.

**Required controls:** B must really become current (a second on-wire RTS carrying B's counter and a distinct RTS
flight identity); the stale, byte-identical A DATA must really reach the HAL after the timer fires. These replace the
white-box `stash.valid` / forced-generation controls while testing the production transition that creates the state.

⇒ Remove `TxStashTestAccess` and its `Node` friend declaration. Re-run the same production mutation
(`retry_stashed`: `s.flight_gen` → current `_pending_tx->flight_gen`); it must remain RED. Correct the round-2
public-unreachability claim in `node.h`, the test, `simulation/BASELINE.md`, [[B189]], and every sibling found by
