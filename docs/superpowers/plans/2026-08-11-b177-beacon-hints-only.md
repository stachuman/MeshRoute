<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B177-FIX — beacons are HINTS ONLY; epoch-bearing P probes are the sole hosted-row authority · 2026-08-11

**Status: DISPATCHED 2026-08-11 on an owner ruling (ledger §1.16).** ★ Role split: the QA-gate wrote this brief and
verifies your claims at the code; an independent QA agent reviews; **the OWNER commits and rules.**
⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out another commit here.**

**Baseline (measured):** HEAD **`eb9d46c`**; the working tree carries §MH-S5b-ii. Native **1509 / 81105 / 0**,
`error:` 0. `lus` **`1c0c63cb`**. Delivery **737 / `s06` 110 / `s07` 83** (raw 763). **31/36 anchor-identical, 5
movers** (all attributed: `s22`/`s28`/`s29` → trigger 2; `s07` → trigger 2 + item 2; `s27` → all three).
`sizeof(Node)` **221880**, `kCap` **91**.

---
## 0 — ⛔⛔ READ THIS FIRST: THE REGISTER'S PROPOSED FIX IS IMPOSSIBLE AND IS WITHDRAWN

[[B177]] recorded that the fix is *"the same one-line shape item 2 uses (`host_row_live_direct` + the low-byte epoch
match)"*. ★★ **THAT CANNOT BE DONE: A MOBILE BEACON CARRIES THE HASH BUT NOT THE REGISTRATION EPOCH** — verified at
the code 2026-08-11 against the BCN layout in `lib/core/frame_codec.h`. ⇒ Copying item 2's shape onto the beacon path
would ship **a gate that ASSERTS an epoch guarantee it does not hold** — the instruments-that-cannot-fail class, this
time inside a fix rather than a test.

⛔ **So the answer is a REMOVAL, not a gate.** Do not add a partial gate and do not report B177 closed by one.

---
## 1 — What to do (owner-ruled; ledger §1.16)

1. **Treat beacons as presence/candidate HINTS ONLY.**
2. ★ **REMOVE the beacon → `mobile_reg_touch()` registry refresh** — `lib/core/node_beacon.cpp:~856`, the
   `if (_active->_mobile_reg[i].key_hash32 == b.key_hash32) { mobile_reg_touch(i, meta_snr_q4); break; }` loop inside
   the `if (b.is_mobile)` arm. ⛔ **Delete the registry touch; keep everything else in that arm** — in particular the
   `_mobile_peer` mobility bit on the line above it, which is unrelated and load-bearing.
3. **Make epoch-bearing P probes the sole ongoing authority** for hosted-row liveness and SNR refresh.
4. ⛔ **Do NOT add an epoch byte or TLV to the beacon.** The owner ruled that out explicitly: it spends **permanent
   airtime** to preserve a now-redundant mechanism.
5. **Update §9.1's wording** in the spec from *"mobile beacons/probes refresh `last_heard_ms`"* to **"validated
   registration probes refresh it"** (`docs/superpowers/specs/2026-08-07-mobile-home-attachment-reliability-design.md`,
   ~`:792`), and correct `docs/protocol.md` if it says otherwise.
6. ⚠ **Correct the stale in-source rationale, not just the code.** The comment above the touch argues the beacon feeds
   the SNR EWMA *"so a stationary mobile's roster tier is not frozen at its CLAIM seed"* and that a stationary mobile
   needs beacons to avoid expiry. **The owner ruled that reasoning PREDATES the presence mechanism and is stale** —
   P checks run every **~1–8 minutes**, well inside the **25-minute** host expiry. ⛔ Do not delete the comment
   silently; **withdraw it in place with the reason**, so the next reader does not "restore" the touch.
   ⓘ **But do establish what is actually lost:** the SNR-EWMA/tier feed genuinely came from this path too. Say
   explicitly which probe arm now supplies it and **at what cadence**, or name it as a measured residual.

## 2 — ⚠⚠ THE ADJACENT SITE — handle it honestly, the owner required this

`lib/core/node_join.cpp:804` — the **SELECTED**-probe arm — also finds the hosted row by **hash alone** and refreshes
it with **no `host_row_live_direct()` and no `reg_epoch` check**. `sel_me` only proves the probe names *us* as home; it
says nothing about row kind, freshness or generation.
★★ **Since §MH-S5b's SEARCHING arm carries both terms, the two arms are now inconsistent and the OLDER one is
WEAKER.** ⇒ **Choose one, explicitly, and say which:**
- **(a)** fix it inside this slice as the same hosted-row identity invariant; **or**
- **(b)** register it separately, before claiming anything about stale-row refresh.

⛔ **B177 may NOT be reported as "stale-row refresh closed" while that arm stands.** ★ **(a) is preferred** — it is the
same invariant, the predicate already exists, and the epoch term is available on a P probe (unlike the beacon).

---
## 3 — Required tests (owner-specified; each needs a mutation control)

1. **A beacon cannot refresh a LIVE hosted row** — ⓘ note this is the *positive* direction of the removal and it is
   the easy one to get wrong: after this slice a beacon must not refresh **even a perfectly valid row.**
2. **A beacon cannot refresh a REDIRECT, an EXPIRED, or a WRONG-EPOCH row.**
3. **A correct-epoch P probe DOES refresh a live direct row.**
4. **Wrong-epoch, redirect and expired rows are refreshed by NEITHER P-probe arm** (searching **and** selected).
5. ★ **A beacon still performs its unrelated functions** — mobility (`_mobile_peer`), team/route learning, candidate
   collection. **This is the over-removal control.** ⛔ Without it, deleting too much passes silently.
6. **Corpus movement measured and attributed.** ⛔ **No [[B178]] trigger work bundled in** — trigger 1 stays deferred.

⚠ **Establish what state each test actually reaches, not what its name implies.** Two controls in this arc were
vacuous for exactly that reason: one queried right after a clock restamp so the row was never old enough for an age
gate to bite, and gate 24 fired no timer at all.

---
## 4 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   From **1509 / 81105 / 0**.
2. **Rebuild `lus`; print its md5 beside every corpus figure.** From **`1c0c63cb`**. ⚠⚠ **A stale `lus` reports the
   previous arm's streams and looks exactly like "nothing moved" — this has already produced one false conclusion in
   this arc.**
3. **All 36 rows**, 0 assertion failures. ⚠ **Expect movement: this removes a live refresh on the beacon plane.**
   ⛔ **Do not edit the `^### 36/36 corpus` table** — attribute every mover (beacon removal vs the §2 choice) by
   in-tree A/B and report; the owner rules on re-anchoring, per [[B170]]'s sequence.
   ★ The five existing movers are attributed already — **distinguish YOUR movers from those**, and say plainly if a
   previously-moving row now moves differently.
4. **Delivery** on the authority (`--mode dm --json` → `totals.unique_deliveries`); floor **≥733 / `s06` ≥104**,
   conditional on the open [[B163]]. Raw `delivered` is a labelled cross-check only.
   ⚠ **If delivery falls, report it — do not restore the beacon touch to recover it.** That is an owner decision.
5. ★★ **Answer D2 explicitly.** `sizeof(Node)` is **221880** and the `static_assert` must still compile. Run
   `warning_census.sh` either way — ⓘ removing the only reader of something can produce an unused-variable or
   unused-function warning, which is exactly [[B169]]'s shape and is **invisible to native and the corpus**.
6. ⛔ **Zero free timer ids** (`kCap == 91`); allocate none.
7. **Mutation-prove every new assertion; print match counts.** ★ Keep §MH-S5b's hazard control green (a searching
   probe that does not refresh the row must still turn a test RED) — **it must still be meaningful after this slice,
   since the searching arm is now one of only two refresh paths.**

---
## 5 — Out of scope

⛔ [[B178]]'s trigger 1 and the **refined option (iii)** — this slice is step 3 of the owner's five-step sequence and
the refined trigger is step 4, deliberately **after** it (this bug's erroneous refresh alters the liveness and quality
inputs that trigger will read). Also out: **S6** · §5.1's static-beacon wakeup · [[B154]](a) · [[B152]]/[[B150]]/
[[B144]]/[[B151]] · [[B158]]'s jitter arc (⛔ `retry_jitter_ms()`, `airtime_routing_ms(8)`, the R3.x golden jitter
assertions) · [[B159]]/[[B161]]/[[B163]]/[[B164]]/[[B165]]/[[B166]] · routing T1–T3 · the OLED UI.

---
## 6 — Method obligations

1. ★★ **A fact is established by the recorded state, never an inference** — [[B174]] came from *"physically in range
   if it is asking"*. ⚠ `_hal.tx()` returns `ok` on **ENQUEUE**; `tx_initiating`/`tx_with_retry` return TRUE-ish for a
   **deferred** frame.
2. ★★ **Identity is the whole tuple** — a hosted row is `(hash, local_id, direct-vs-redirect, live-vs-expired,
   epoch)`. ⛔ **This bug is the fifth site of that error in this arc** ([[B147]], [[B172]], [[B174]], the selected
   arm, this one). `host_row_live_direct()` is the single predicate — use it, never re-spell it.
3. ★★ **Instruments that cannot fail — 17+ instances**, and §0 above is a **new variant: a proposed FIX that would
   assert a guarantee it cannot deliver.** When you write a gate, ask what the frame actually carries.
4. ★ **A correction placed anywhere but the instruction a reader follows — ten-plus sites.** The stale beacon-SNR
   rationale is the next one if you delete code and leave the comment arguing for it.

⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval that was not given; **never quote an
owner ruling** — reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation** — label
the source, not the messenger.

**Report:** the removal with `file:line` · the §2 choice (a or b) stated explicitly · all six required tests with their
mutations · what now supplies the SNR-EWMA/tier feed and at what cadence · the corpus table **with the `lus` md5** and
your movers attributed separately from the existing five · delivery · the D2 answer · the §9.1/`protocol.md` wording
change · the withdrawn in-source rationale · exact final `git status --short` and that nothing was committed.
⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** delivery falls below the floor · a mover cannot be attributed ·
removing the touch breaks a beacon function you cannot restore without re-adding it · the SNR-EWMA feed turns out to
have no probe-side equivalent · or `sizeof(Node)` moves.
