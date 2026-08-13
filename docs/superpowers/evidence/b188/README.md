<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B188 — the rolling duty window's boundary behaviour, made reachable · **NON-CORPUS fixture + verifier** · 2026-08-12

⛔ **Nothing here is an owner ruling or a QA approval.** ⛔⛔ **These three scenarios are a NON-CORPUS coverage
fixture.** They must never be added to `simulation/`, the 36-row runner, `BASELINE.md`'s anchor table, or any
delivery-floor computation. ⛔ **No corpus scenario was lengthened and `s07`'s duty window was NOT shortened**
— the method is an explicitly compressed window in a purpose-built fixture.

---

## 0 — ⛔⛔ REPAIRED 2026-08-13: THE LEDGER RECONSTRUCTION WAS ACAUSAL, AND ONE PUBLISHED FIGURE WAS WRONG

**What was wrong.** The first version of `verify.py` reconstructed each node's duty ledger from **the whole
run's** `tx` records and then filtered by timestamp. A frame transmitted **after** the event being checked — or
in the **same millisecond but later in the stream** — was therefore counted as airtime the node had already
spent. ⚠ **26 checks and all 26 of their mutations passed over that model**, which is the sharper problem: they
validated a wrong model consistently, because the model is the language they were written in.

★★ **THE ORDINAL RULE THE LEDGER NOW OBEYS:** every record keeps its **stream ordinal** `k` (its 0-based line
position), and the ledger for an event at ordinal `K` contains **only `tx` records with ordinal `< K`**.
· A timestamp is **not** an ordering — many records share a millisecond — so **the ordinal is what breaks the
tie**. · The simulator's own ledger is a FIFO whose order is the order frames were staged, i.e. the order they
appear in the stream. · `_alive()` / `used()` / `oldest()` all take `before=K`; there is no way to ask them a
question without saying *when*.

**Every ledger-derived figure was re-derived both ways. ONE moved:**

| figure | pre-repair (acausal) | **repaired (causal)** | |
|---|---|---|---|
| `A3c` in-window airtime **drops** between successive refusals | ~~15~~ | **4** | ⛔ **CORRECTED — the published 15 is WITHDRAWN** |
| `A1b` refusals re-derived as genuine exhaustion | 53 / 53 | **53 / 53** | unchanged |
| `A2` `busy_until` exact | 53 / 53 | **53 / 53** | unchanged |
| `A3a` distinct window fronts | 23 | **23** | unchanged |
| `A4` resumptions after the first refusal | 15 | **15** | unchanged |
| `B2` non-negative deviations · `B2b` band | 19 / 19 · 1..6 ms | **19 / 19 · 1..6 ms** | unchanged |
| `C3` peak 1 h-window airtime | 13 929 ms | **13 929 ms** | unchanged (formula also made explicit: it now includes the measured frame's own airtime) |

ⓘ *"Unchanged"* here is a **measurement**, not an argument: each figure was computed with `before=K` and again
with `before=∞` (the retired behaviour) on the same streams. ⛔ That the other aggregates happen to coincide on
these streams is **not** evidence that causality is unnecessary — the reader-level controls in §3.3 are.

**★★★ THREE FURTHER BLOCKERS WERE FOUND IN THIS VERIFIER AND REPAIRED 2026-08-13. Each was a case of the
instrument saying more than it knew:**

**(i) `airtime_for()` INVENTED A VALUE, AND ITS SAFETY CLAIM WAS BACKWARDS.** Four refused **4-byte** frames in
scenario A were never transmitted, so the old code substituted *"the smallest airtime observed on this PHY"* =
**177 ms** — while this PHY prices 4 bytes at **156 ms**. The substitute was therefore **LARGER** than the
truth, so `used + airtime > budget` could be **overstated**, which is exactly what its docstring promised was
impossible. ⇒ **THE SUBSTITUTION IS GONE.** Frames are now priced through the **simulator's own formula**
(`SimRadio::getEstAirtimeFor` — AN1200.13 plus the SX126x SF5/6 special case — transcribed from the source),
and that formula is **cross-checked against every `(len → airtime)` pair the stream itself published**
(`A1c`/`A1d`). If a single observed pair disagreed, or the PHY were not uniform, pricing would be declared
invalid and the affected refusals reported **UNPRICED** (`A1e`) — never guessed.
⇒ **Measured: 6 of 6 observed pairs reproduced exactly, 0 wrong; 49 refusals priced from observation, 4 by
formula, 0 unpriced.** ⓘ And the exhaustion proof does **not** rest on the old inflation: all four are
`causal_used = 197` at a **300 ms** budget, so `197 + 156 = 353 > 300` holds on the true price. Under the
alternative disposition the figure would read **49/49 priced + 4 UNPRICED**; the formula-with-cross-checks
option was taken because it costs no coverage and invents nothing. ⚠ **This was the third invented value in
this arc** ([[B182]]'s tool got it right, `lostj.py` was fixed on review, this is the third) — **an instrument
that cannot price something must say so.**

**(ii) THE B186a CORRELATION DID NOT BIND THE NODE.** `A5c` matched only `(t, busy_until, reason)`. The node was
unpacked in the very same tuple and never compared, so **one `mobile_tx_refused` record moved from `MobileM` to
`Peer` still reported 26/26 PASS** — the check that validates B186a's whole point could not see the wrong node.
⇒ binding is now a named helper, `pair_refusal()`, requiring **all four**: the emit's node **index** resolves
(through `node_started` order) to the deferred record's node **name** · the deferred record's ordinal
**precedes** the emit's · millisecond, `busy_until_ms` and reason agree · and **exactly one** candidate
qualifies (**ambiguity is a refusal, never a pick**). Only duty-refused mobile emits enter the denominator.
`A5c` **38/38 bound**, new `A5d` **38/38 agree on the node**, and the by-hand mutation is now a permanent
control (`selftest.py` `S4b`).

**(iii) THE "PAST END OF RUN" HORIZON WAS SELF-FULFILLING.** It used the timestamp of the **last transmitted
frame**, so a *missing* late transmission moved the yardstick **backwards** and was then excused as *outside the
run*. ⇒ the horizon is now the **`sim_end` record** (`B3z`). In this fixture `sim_end` is **600 000 ms** while
the last frame is at **582 223 ms** — a **17 777 ms** window in which a lost retry used to excuse itself. The
counts are unchanged here (**18 flew / 0 lost / 1 past-end**, the one defer being due at 612 406 ms, past both
horizons), but the classification no longer depends on what it is measuring, and `selftest.py` `S5b`/`S5c` pin
the difference: the same synthetic case is **`lost`** under `sim_end` and **`past_end`** under the retired
horizon.

**★★ AND A THIRD ROUND FOUND THREE MORE, TWO OF WHICH WERE ONE DEFECT — the pricing key.** `verify.py` keyed
observations on **length alone** (`airtime_of_len[len] = airtime`, a bare assignment) while capturing
`(sf, bw, cr)` on the line above and discarding it. That single wrong key produced **both** symptoms:
· **a conflicting duplicate observation was overwritten instead of detected** (an incorrect 14-byte observation
followed by a correct one still cross-checked as `(1, 0)`); and
· **a wrong refusal SF was invisible** (one report's `sf` changed 8→12 still passed, because the candidate
filter never compared SF and pricing never used it).
⇒ observations are now a **set per `(len, SF, BW, CR)` shape**, a conflict is a refusal, the refused frame is
priced at **its own SF**, and `pair_refusal()` requires SF equality.
**The third:** `sender = S.node_names.get(idxs[0], 'Home')` — **removing every `node_started` record from
scenario B left the whole run PASSING** on a stream with no node authority at all: an unagreed silent default
(C2) inside the binding path the previous round had repaired (that round fixed the *comparison*; the
*resolution* still defaulted). ⇒ **there is no default**: `resolve_node()` returns `None`, `B1c` fails loudly,
and `pair_refusal()` reports unbound.
★ **Every input is now validated or refused** — a `malformed` list (asserted empty by `A0`/`B0`/`C0`) catches a
`tx` without `hex`/`sf`/`bw_hz`/`cr`, a `tx_deferred` without `busy_until_ms`/`len`/`sf`, an emit without
`data`, a `sim_end` without a timestamp, an untyped record, and a **duplicate `node_started`** (which would
slide the whole index→name binding). The full audit, including the defaults judged safe and why, is in the
`__init__` and `resolve_node` comments.

**★ AND ONE NON-BLOCKING ORDERING FIX, recorded because it is the same shape as everything above:**
`airtime_for()` served an **observed** value before asking whether the stream's observations conflicted, so a
stream with a conflict on ONE shape could still hand back an exact price for a **different, clean** shape —
pricing was void stream-wide and one path never looked. ⓘ It could not produce a false-green gate (the complete
verifier still failed via `A1d`/`A1f`), which is why it was non-blocking. The order is now
**(1) stream-wide validity → (2) establish this SF's shape → (3) serve**, pinned by `S6m`/`S6n`.
★★ **THE GENERAL FORM, written into the function: VALIDATE THE INPUT BEFORE SERVING IT, NEVER AFTER.**
⚠ And the fix immediately earned its keep: with the cross-check now running first, it **rejected an invented
airtime inside one of these controls** (a hand-chosen 730 ms for a 14-byte SF10 frame; the formula says **790**),
so the control was corrected to the derived value. The instrument refused its own test's invented number.

**Two more repairs landed with it:**
* ⛔ **THE SECOND PARSER IS GONE.** Frame length is now collected in the **one** JSON pass; the raw-substring
  scan (`'"type":"tx"' in line`) that used to re-read the file has been deleted. Two readers over one stream is
  how the two disagree ([[B162]]), and that one was **whitespace-dependent** as well — proved, in `selftest.py`
  `S3e`, by reproducing it and watching it miss a normally-spaced encoding entirely.
* ★ `verify.py`'s resumption check now uses **ordinal order** (`tx_after_ordinal`), so a frame in the same
  millisecond but earlier in the stream can no longer count as a resumption.

---

## 1 — The gap, measured

**36 of 36 corpus scenarios are in the ONE-SHOT duty regime:** not one sets `duty_cycle_window_ms`, all
inherit the 1 h default, and every `duration_ms` is ≤ 1 h. A budget that never rolls is entered once and never
left ⇒ **the moment a rolling window RECLAIMS budget is untested corpus-wide**, which is why the busy-refusal
path behind [[B183]]/[[B186]] could stay invisible for so long.

★ Measured here rather than argued: the control scenario (§2, C) reproduces that regime deliberately and its
**peak in-window airtime is 13 929 ms against the 36 000 ms allowance (38.7 %)** — the post-exhaustion path is
never entered, so the rolling boundary is never crossed.

**The arithmetic the fixture is built on:** `simulation.radio.duty_cycle` = 1 **PERCENT** and
`simulation.radio.duty_cycle_window_ms` = 30 000 ⇒ **a 300 ms allowance per rolling 30 s**, and a 600 000 ms run
therefore rolls the window **20 times**. ⓘ The global key drives **both** enforcers: `SimController` injects
`_sim_duty_cycle_window_ms` into every node config, which `NodeRuntimeWrapper::map_duty_cycle_window_ms` maps
onto `NodeConfig::duty_cycle_window_ms` — the value the **firmware's own** `duty_over_budget()` pre-check uses.

---

## 2 — Three scenarios, one job each

| id | file | what it exercises |
|---|---|---|
| **A** | `b188_a_rolling_mobile.json` | the **simulator's ASYNCHRONOUS** duty hard-block — the only refusal that carries `busy_until_ms`. Reached by a mobile J frame: those are `FrameTag::beacon`, so `retry_slot_of` is −1 and `tx_with_retry`'s duty **pre-check (gated on `slot >= 0`) does not apply** — the modem is asked for airtime the firmware never pre-checked |
| **B** | `b188_b_rolling_data.json` | the **firmware's own** duty pre-check on a DM (`duty_cycle_blocked{RTS, wait_ms}`) and the `kRtsDutyDeferTimerId` re-run that later **airs** the frame |
| **C** | `b188_c_control_oneshot.json` | **CONTROL** — identical to B except `duty_cycle_window_ms` is left at the 1 h default (the corpus regime). The same load must produce **ZERO** duty refusals |

★ **C is the attribution.** Same topology, same commands, same load; the only difference is the window. Without
it, A/B's refusals could be a property of the traffic rather than of the boundary being crossed.

---

## 3 — The four required verifications, and where each is pinned

`verify.py` — **39 checks, 0 failed, exit 0**; **exit 1 on any failure**.

| requirement | check(s) | measured |
|---|---|---|
| **1 · refusal at exhaustion** | `A1`, `A1b`–`A1e` (and `B1`, `B1b`) | 53 asynchronous refusals in A; **every one re-derived from the CAUSAL ledger** as `used + frame airtime > 300 ms` (**53/53, 0 unpriced**), with the price either observed (49) or from the cross-checked formula (4). 19 firmware pre-check refusals in B |
| **2 · correct `busy_until`** | `A2`; `B2`, `B2b` | **53/53** exact: `busy_until == oldest_in_window_tx_end + window`. For the firmware's promised wait: 19/19 satisfy `wait_ms >= oldest + window − now` with a **1..6 ms** deviation whose whole cause is named (below) |
| **3 · incremental rolling expiry** | `A3a`, `A3b`, `A3c`; `B4`, `B4b` | **23 distinct window-front entries** feed the refusals; the smallest wait is **930 ms**, i.e. a *fraction* of the 30 000 ms window (only the oldest entry has to age out); and the **causally** reconstructed in-window airtime **drops between successive refusals 4 times** — budget genuinely reclaimed. ⛔ **4, not the ~~15~~ first published: that figure was acausal and is withdrawn (§0)** |
| **4 · resumed transmission once budget is available** | `A4`, `A4b`; `B3z`, `B3`, `B3b`, `B3c` | the refused mobile airs frames again at/after its first `busy_until` (and inside the following window); **18 of 19** deferred RTS fly at **exactly `t + wait_ms`**, 0 vanish inside the run, 1 is excluded because its due instant (612 406 ms) lies past **`sim_end` = 600 000 ms** (asserted separately so the exclusion cannot hide a loss) |

Plus `A5`–`A5d`: the fixture doubles as [[B186a]] evidence — **all four** mobile operations are named on
one stream (`claim`, `discover`, `offer`, `reclaim`), and every duty-refused operation is **bound to exactly one preceding `tx_deferred` ON THE SAME NODE**
(§0 (ii)).

### 3.1 ★ Why the numbers are real: the ledger is RECONSTRUCTED **CAUSALLY**, not trusted

`verify.py` rebuilds the per-node duty ledger from the stream's own `tx` records — **in one JSON pass, with
each record's stream ordinal retained** — and recomputes the simulator's arithmetic independently
(`FirmwareNode::airtimeUsedInWindow` / `oldestTxEndMs`, read at the source): one entry per aired frame at
`end_ms = tx.time_ms + tx.airtime_ms` — ⚠ **a LOWER-BOUND reconstruction, not an exact one: the record's
`time_ms` is the DECISION instant, while the ledger stamps `max(now, earliest_tx) + airtime`, and the RX→TX
turnaround inside that `max` is not published in the stream** — **only entries whose ordinal precedes the event
being checked**, entries
with `end_ms <= now − window` dropped, `used` = the survivors' sum, `oldest` = the earliest survivor. ⇒ the
checks assert a **number the verifier derived itself, from what was true at that instant**, not the presence of
a field.

⚠ **The one tolerance is measured and its DIRECTION is pinned** (`B2`/`B2b`), which is what keeps it from being
a fudge: the stream's `tx.time_ms` is the DECISION instant (`now`), while the ledger stamps
`max(now, earliest_tx) + airtime` — `SimController` pushes a synthesised TX past the radio's RX→TX turnaround
(`f.start_ms = (now > earliest_tx) ? now : earliest_tx`), a latency the stream does not publish. The
reconstruction is therefore a **lower bound by construction**: the reported wait may exceed it by the
accumulated turnaround (**measured: 1..6 ms**) and may **never** be below it. A wrong formula is wrong by the
window (~30 000 ms), so the band cannot launder one.

★★ **PRICING A FRAME THAT NEVER FLEW — THE KEY IS THE AIRTIME FUNCTION'S WHOLE DOMAIN.** ⛔ The
*"smallest airtime observed on this PHY"* fallback this paragraph used to describe is **deleted** (§0 (i)), and
so is the `length`-only observation key that made it possible. Observations are a **set per
`(len, SF, BW, CR)` shape**, so a second, different airtime for one shape is a **CONFLICT** that invalidates
pricing (`A1f`) instead of silently overwriting the first. `airtime_for(len, sf)` returns:
· the **observed** value for that exact shape, or
· the **formula's** value for that shape — only if the cross-check is clean (`A1c`/`A1d`), or
· **`None` = UNPRICED**, which `A1e` counts and reports, when the shape cannot be established (two `(BW, CR)`
  pairs at one SF), when observations conflict, or when the formula disagrees with any observation.
⛔ The refused frame's **own SF** does the pricing, and `pair_refusal()` requires the report's SF to equal the
refusal record's — one omission was behind both the invisible wrong SF and the colliding observations.

### 3.2 ★★ MUTATION-PROVED — 39 of 39 (`verify.py`) and 39 of 39 (`selftest.py`)

```
for id in $(grep -oE "C\.(eq|ge|lt)\('[^']+'" verify.py | sed "s/.*('//;s/'//"); do
  python3 verify.py /tmp/b188 --mutate "$id" >/dev/null || echo "mutation $id detected"; done   # 39 lines
```
Every one of the 39 expectations, inverted one at a time, produces a `FAILED` line naming that check and
**exit 1**; the same sweep over `selftest.py` detects **39 of 39**. ★★ **AND THE THREE DEFECTS REVIEW FOUND BY
HAND ARE NOW CAUGHT END-TO-END ON THE REAL STREAMS** (injected into a copy, never into the fixture): a
conflicting duplicate observation ⇒ **4 FAILED** (`A1c`/`A1d`/`A1f`/`A1e`) · one report's `sf` changed 8→12 ⇒
**3 FAILED** (`A5c`/`A5d`/`A5e`) · every `node_started` removed ⇒ **`B1c` FAILED** and `check_b` stops · and the
earlier wrong-node move ⇒ **3 FAILED**. ⇒ this fixture cannot join the list of
instruments that could not fail — which is exactly the failure the [[B183]] fixture had to be repaired for
(empty `expect` arrays, no `sys.exit`). ⚠ **AND MUTATION COVERAGE IS NOT ENOUGH ON ITS OWN**: the pre-repair
version was 26/26 mutation-proved over an acausal ledger. That is why §3.3 exists.

### 3.3 ★★★ SIX READER-LEVEL CONTROL GROUPS — `selftest.py`, **39 controls, 0 failed, exit 0**

Checks over the fixture streams cannot catch a wrong ledger MODEL, because they are expressed in it. These run
on synthetic streams whose right answer is known by construction, and **each one also proves the PRE-REPAIR
behaviour would have failed it** — a control the old code would also have passed is not a control.

| control | what it pins | discriminating evidence |
|---|---|---|
| **1 · FUTURE TX** (`S1a`–`S1d`) ★ *the control that would have caught the defect* | a `tx` AFTER the checked event is excluded from its ledger | causal `used` **100** vs the retired acausal **300**; and on `oldest` — which `busy_until` is computed from — causal **0** (everything expired) vs the retired **950**, a window front invented from a frame not yet transmitted |
| **2 · SAME-TIMESTAMP ORDERING** (`S2a`–`S2d`) | three records inside ONE millisecond are consumed in ordinal order | ordinals strictly increasing in file order; causal `used` **100** (the earlier record only) vs a timestamp filter's **300**; and the later same-ms frame is still visible to the ordinal-ordered resumption check |
| **3 · COMPACT vs SPACED NDJSON** (`S3a`–`S3e`) | the reader is formatting-independent | identical ledger, identical `len→airtime` table and identical `used` across both encodings — **and the retired substring scan, reproduced inline, finds 1 record in the compact encoding and 0 in the spaced one** |
| **4 · NODE BINDING** (`S4a`–`S4g`) ★ *the three by-hand review mutations, made permanent* | a report must bind to a refusal **on its own node**, at a **preceding** ordinal, with the **same SF**, **unambiguously** — and a **missing node authority must refuse** | the correct attribution binds; the same report moved to `Peer` does **not**; two indistinguishable candidates ⇒ **unbound**; a later-ordinal `tx_deferred` ⇒ **unbound**; **`sf` 8 vs 12 ⇒ unbound** (`S4e`); **with no `node_started` records the index resolves to `None` and the report is unbound, never defaulted to a name** (`S4f`/`S4g`) |
| **5 · RUN HORIZON** (`S5a`–`S5e`) | `sim_end` decides what is inside the run, not the last frame | a retry due **before** `sim_end` with no later TX is **`lost`** — and the retired last-TX horizon calls the identical case `past_end`; a retry that flies at `t + wait_ms` is `flew`; one due after `sim_end` is `past_end` |
| **6 · PRICING** (`S6a`–`S6n`) | the key is `(len, SF, BW, CR)`; prices are derived or refused, never substituted | the formula reproduces the observation; a never-aired 4-byte frame prices at **156 ms**, not the 197 ms *"smallest observed"* substitution; an observed shape still comes from the observation; ★ **a conflicting duplicate observation is RETAINED as a conflict `{(14,8,62500,5): [197, 999]}`, invalidates the cross-check and yields UNPRICED** (`S6f`–`S6h`); **one length at two SFs is two observations, each pricing its own refusal** (`S6i`–`S6k`); **two bandwidths at one SF, a formula/observation disagreement, and a `tx` missing `hex` all refuse** (`S6d`/`S6e`/`S6l`); ★ **and the ORDERING: a clean, observed shape is still UNPRICED when another shape conflicts** (`S6m`/`S6n`) |

---

## 4 — Replay

```bash
cd /home/staszek/MeshRoute/docs/superpowers/evidence/b188
md5sum ~/lora-universal-simulator/build/orchestrator/lus        # expect 43a7b6eb… (the §B186a build)
mkdir -p /tmp/b188
for f in b188_a_rolling_mobile b188_b_rolling_data b188_c_control_oneshot; do
  ~/lora-universal-simulator/build/orchestrator/lus $f.json /tmp/b188/$f.ndjson
done
python3 verify.py /tmp/b188 ; echo "verify exit=$?"      # must print "39 checks, 0 failed" and exit 0
python3 selftest.py ; echo "selftest exit=$?"          # must print "39 controls, 0 failed" and exit 0
mkdir -p /tmp/regen && python3 gen.py /tmp/regen \
  && for f in b188_*.json; do cmp $f /tmp/regen/$f; done                          # gen.py is byte-faithful
```

**Streams produced by `lus` `43a7b6eb`:** A `82aaa4de` / 674 events · B `85da6482` / 365 · C `310a1904` / 972.
⚠ These md5s are a **replay convenience, not an anchor**: they will move whenever the firmware legitimately
moves, and ⛔ **nothing in `BASELINE.md` references them.** The 39 checks and the 39 controls are the contract.

**Exit-code contract:** `verify.py` **0** = all 39 checks pass · **1** = at least one failed · **2** = a stream
is missing. `selftest.py` **0** = all 39 reader/ledger controls pass · **1** = at least one failed. Both accept
`--mutate <check-id>` and must then report that id as `FAILED` and exit 1.

**Files.** `gen.py` regenerates the three JSONs byte-identically (verified) · `verify.py` the 39-check gate over
the fixture streams, holding **the ONE reader and the ONE causal ledger** · `selftest.py` the six reader-level
control groups. ⛔ `pair_refusal()` and `classify_defer()` live in `verify.py` and are IMPORTED by the
self-test, so the binder and the classifier a control validates are the ones the gate uses. ⛔ **Do not add a second parser or a second ledger:** both files bind through
`verify.Stream`, so a future relaxation cannot be made in one caller only.

---

## 5 — What this fixture does NOT establish

* **Nothing about hardware.** Both refusal paths here are the simulator's. On metal `Node::on_radio_busy` has
  no caller at all and `DeviceHal` refuses **synchronously** — see the audit in
  `docs/superpowers/evidence/b186a/README.md` §4.
* **Nothing about `s07`.** ⛔ Its window and load are untouched, its saturation stands as expected stress
  behaviour, and no re-anchor is claimed or authorised.
* **No delivery figure.** These scenarios are excluded from every delivery computation by construction.
* The **absolute** counts above (53 / 19 / 13 / 23 / **4**) are properties of this fixture at this firmware
  revision; the checks that carry meaning are the exact arithmetic ones (`A1b`, `A2`, `B2`, `B3`), the
  control's zeros (`C1`, `C1b`) and the reader controls in §3.3.
* ⛔ **Anything not re-derivable causally is withdrawn rather than carried forward** — see §0's table; the
  single withdrawn figure is the acausal `A3c` = ~~15~~.

---
## 6 — ★★★ FINAL STATUS (the mobile-home investigation ENDS HERE, owner-ruled 2026-08-13)

* **The coverage gap [[B188]] was registered for is CLOSED**, outside the corpus: `verify.py` **39 checks /
  0 failed / exit 0 / 39-of-39 mutation-proved** and `selftest.py` **39 controls / 0 failed / exit 0 /
  39-of-39 mutation-proved**, over streams from `lus` **`43a7b6eb`**.
* **Owed: nothing for this fixture.** Its residual — stated rather than left implicit — is that **both refusal
  paths it exercises are the simulator's**; the hardware side is [[B189]] and it is open.
* **The verifier took FOUR rounds of review.** Every defect was the same shape — *the instrument said more than
  it knew*: an acausal ledger, a substituted price, an unbound node, a defaulted node authority, an unchecked
  SF, an overwritten observation, and finally a value served before it was validated. ⇒ **the rule this fixture
  now encodes everywhere: validate the input before serving it, and refuse what you cannot establish.**
  ⚠ **And it is not enough to mutation-prove the assertions** — the first version was 26/26 mutation-proved over
  a wrong model. That is what §3.3's reader-level controls exist for.
* ⛔ **Untouched by all of it:** `s07`'s window and load · every corpus scenario · `BASELINE.md`'s anchor table ·
  the delivery floor (frozen/unratified). ⛔ Nothing here is an owner or QA approval.
