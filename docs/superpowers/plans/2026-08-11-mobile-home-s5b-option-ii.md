<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §MH-S5b-ii — land OPTION (ii): keep trigger 2, DEFER trigger 1 · dispatch brief · 2026-08-11

**Status: DISPATCHED 2026-08-11 on an owner ruling (ledger §1.15).** ★ Role split: the QA-gate wrote this brief and
verifies your claims at the code; an independent QA agent reviews; **the OWNER commits and rules.**
⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything, and never check out another commit here.**

**Baseline (measured):** HEAD **`eb9d46c`**; working tree carries §MH-S5b with **all three items landed**. Native
**1509 / 81087 / 0**, `error:` 0. `lus` **`8f368173`**. **728 unique / `s06` 110** (raw 753) — ⛔ **below the `≥733`
floor, which is why this slice exists.** 31/36 anchor-identical, 5 movers. `sizeof(Node)` **221880**, `kCap` **91**.

---
## 0 — The ruling, and exactly what to do

**The owner REJECTED the 728 outcome and ruled that §MH-S5b lands as OPTION (ii): §8.3 trigger 2 plus items 2 and 3,
with trigger 1 DEFERRED under [[B178]].** (Reported form — ⛔ do not quote it; ledger **§1.15** is the record.)

⇒ **Your job is a NARROWING, not new behaviour:**
1. **Remove trigger 1 from the searching-probe decision.** `Node::presence_searching_probe_due()`
   (`lib/core/node_mobile.cpp:744`) currently ORs **trigger 2** (`_presence_miss > 0`, an *admitted* miss) with
   **trigger 1** (`_presence_prescan`). **Keep trigger 2. Drop the `_presence_prescan` disjunct.**
2. **Keep items 2 and 3 exactly as landed** — the host-row refresh (`presence_refresh_hosted_row()`,
   `node_join.cpp:691`, with its `host_row_live_direct()` **and** low-byte epoch terms) and the verified-echo switch
   requirement (`node_mobile.cpp:1156`). ⛔ **Do not touch either.** Item 3 is measurably byte-inert; that is not a
   reason to remove it — it is the §8.4 term the owner explicitly listed as preserved.
3. **Mark trigger 1 DEFERRED in the spec** (§8.3) under [[B178]], and in `docs/protocol.md` if it says otherwise.

### ⛔ What must NOT happen to the record
★★ **The owner named the limitation and it must be stated wherever §8.3's status is described, not buried:**
**a weak but CONSISTENTLY RESPONDING home will not proactively initiate candidate verification, so the mobile changes
home only AFTER connectivity begins failing.** ⇒ **This is a conservative INTERIM policy, not completed proactive
roaming.** ⛔ **Do not record §8.3 as satisfied**, and do not let a reader infer that §S6.4-C's purpose — *leave a weak
home BEFORE loss* — is met. Write that sentence into the spec's §8.3, the register and `BASELINE.md`.

★ **Test hygiene (B101 precedent):** any test that asserts trigger 1 fires must be **rewritten in place** to assert
the deferred behaviour — ⛔ **never deleted, never disabled** — and its heading must say it pins a deferral, so the
next reader does not "restore" it.

---
## 1 — The target figures (already A/B'd; verify, do not re-derive)

§MH-S5b measured the *"trigger 2 only"* arm with one `lus`, all 36 rows, authority
`tools/dm_delivery_breakdown.py --mode dm --json` → `totals.unique_deliveries`:

| arm | overall | `s06` | `s07` |
|---|---|---|---|
| base (anchor) | 734 | 110 | 80 |
| ★ **trigger 2 only + items 2+3 — YOUR TARGET** | **737** | **110** | **83** |
| all three (the landed tree) | 728 | 110 | 74 |

⇒ **Expect 737 / `s06` 110 / `s07` 83, i.e. ABOVE the anchor.** ⚠ **That is a target to CONFIRM, not a prediction to
trust** — the QA-gate has had **seven** predictions of this shape refuted in this arc. **If you get a different number,
report it with an A/B; do not adjust anything to reach 737.**

⛔ **`s27` must stay at zero assertion failures.** The rejected *"items 2+3"* arm scored 739 but was **inadmissible**
because `s27` went RED (3 failures) — trigger 2 is what keeps its re-home reachable. **If `s27` reddens, stop.**

---
## 2 — Corpus movers and the anchor table

The landed tree has **5 movers**; removing trigger 1 will change that set. ⛔ **Do not edit the
`^### 36/36 corpus` table** — re-anchoring is the owner's ruling and [[B170]] established the sequence
(measure → attribute → report → owner rules → land).
★★ **Attribute every remaining mover to trigger 2, item 2 or item 3 by in-tree A/B.** An unattributable mover is a
**finding**. ⚠ If a row returns to its anchor, say so explicitly — a *shrinking* mover set is the expected shape here
and is worth stating as evidence rather than leaving implicit.

---
## 3 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   From **1509 / 81087 / 0**; expect the count to **fall** as trigger-1 assertions are rewritten — report the delta and
   what each removed/rewritten assertion pinned.
2. **Rebuild `lus`; print its md5 beside every corpus figure.** ⚠⚠ **A stale `lus` reports the previous arm's streams
   and looks exactly like "nothing moved"** — this has already produced one false conclusion in this arc. From
   **`8f368173`**.
3. **All 36 rows**, 0 assertion failures, with §2's attribution.
4. **Delivery** on the authority; raw `delivered` is a labelled cross-check only. Floor **≥733 / `s06` ≥104**,
   conditional on the open [[B163]].
5. ★★ **Answer D2 explicitly.** Removing a disjunct should not move `sizeof(Node)` (**221880**) — confirm the
   `static_assert` still compiles. ⓘ If `_presence_prescan` becomes unused, ⛔ **do not delete the member in this
   slice** — trigger 1 returns with the refined option (iii), and removing then re-adding a member would move
   `sizeof(Node)` twice and cost two ten-env sweeps. **Mark it in-source as retained for [[B178]]'s refined trigger**
   (the mark-done-vs-missing rule). Run `warning_census.sh` either way — an unused private member can warn.
6. ⛔ **Zero free timer ids** (`kCap == 91`); allocate none.
7. **Mutation-prove every rewritten assertion; print match counts.** ★ **Keep §MH-S5b's hazard control green** — the
   mutation where a searching probe does **not** refresh the host row must still turn a test RED. Item 2 stays, so that
   control must stay meaningful.

---
## 4 — What comes AFTER this slice (context only — ⛔ NOT in scope)

The owner ruled the sequence; do **not** start any of it here:
1. this slice;
2. the spec deferral mark (part of this slice);
3. ⚠ **[[B177]] fixed SEPARATELY AND BEFORE step 4** — `node_beacon.cpp:856` touches a hosted row on **hash alone**
   (no row-kind, no freshness, no epoch), restamping a **redirect** row over §9.2's breadcrumb clock and
   **resurrecting an expired** row. It is the **eleventh** consumer that should use `host_row_live_direct()`.
   ★ **It matters to the sequence because its erroneous refresh can alter the liveness and quality inputs trigger 1
   reads** — measuring the refined (iii) on top of it would measure the wrong tree;
4. **the refined option (iii)**, which the owner specified: a proactive searching probe **only** when the home is
   weak/critical **and** at least one candidate is **fresh**, **compatible**, **passively observed**, **still
   unverified**, and whose **measured one-way quality could possibly satisfy the two-tier rule** — **and** the
   candidate **hold** and **anti-flap dwell** are already satisfied. ⛔ The broad *"weak home + any audible
   candidate"* form is **REJECTED**: in a dense scenario nearly every mobile has an audible candidate, so it would
   reproduce the storm unchanged;
5. its acceptance criteria: **`≥733`** *plus* **no increase in `presence_home_lost` in `s07`**, **bounded roster
   airtime and peak-window collisions**, and **no repeated canvass once a plausible candidate is verified**.

⛔ Also out: **S6** · [[B154]](a) (deferred) · [[B152]]/[[B150]]/[[B144]]/[[B151]] · [[B158]]'s jitter arc
(`retry_jitter_ms()`, `airtime_routing_ms(8)`, R3.x golden jitter assertions untouchable) · [[B159]]/[[B161]]/[[B163]]/
[[B164]]/[[B165]]/[[B166]] · routing T1–T3 · the OLED UI · §5.1's static-beacon wakeup.

---
## 5 — Method obligations

1. ★★ **A fact is established by the recorded state, never an inference** — [[B174]] came from *"physically in range if
   it is asking"*. ⚠ `_hal.tx()` returns `ok` on **ENQUEUE**; `tx_initiating`/`tx_with_retry` return TRUE-ish for a
   **deferred** frame.
2. ★★ **Identity is the whole tuple** — `host_row_live_direct()` is the single predicate with **ten** consumers (soon
   eleven). Use it; never re-spell it.
3. ★★ **Instruments that cannot fail — 17+ instances.** ⚠ Two fresh ones from the last two slices: a control that
   could not see an **age-shaped** over-fix because the test queried right after a clock restamp, and gate 24 which
   **fired no timer at all** and therefore could not fail. ⇒ **Establish what state a test actually reaches, not what
   its name implies.**
4. ★ **A correction placed anywhere but the instruction a reader follows — ten-plus sites.**

⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval that was not given; **never quote an
owner ruling** — reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation** — label
the source, not the messenger.

**Report:** the narrowing with `file:line` · every rewritten test and its mutation · the corpus table **with the `lus`
md5** and the attribution of every remaining mover (and every row that RETURNED to anchor) · delivery · the D2 answer ·
the spec/register/BASELINE deferral wording including the owner's named limitation · exact final `git status --short`
and that nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** delivery is not ≥733 · `s27` reddens · a mover cannot be attributed ·
removing the disjunct requires touching item 2 or item 3 · or `sizeof(Node)` moves.
