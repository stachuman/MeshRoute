<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §HYBRID-RTS-S5 — remaining RTS/CTS timing audit ([[B158]]) + the PHY ledger · dispatch brief · 2026-08-10

**Status: ✅ EXECUTED 2026-08-10 — ⛔⛔ HISTORICAL. DO NOT DISPATCH THIS BRIEF AGAIN, AND DO NOT FOLLOW ITS
`retry_jitter_ms` INSTRUCTION.** Results: `simulation/BASELINE.md` §HYBRID-RTS-S5 (the 128-cell ledger, the
semantic-owner table, the `+1 ms`-at-every-cell margin result), the new `test/test_airtime.cpp` cases, and
[[B166]] / [[B167]]. Audit + test only: **no behaviour changed**, `lus` byte-identical at `9150ad1a`.

⛔⛔ **SUPERSEDED FOR B158 JITTER POLICY (2026-08-10, owner ruling — reported form, owner-rulings-ledger §1.13):
the owner ruled that Lua parity is NOT a final MeshRoute jitter requirement and that [[B158]] stays OPEN until
MeshRoute-native jitter has been independently measured and selected.** ⇒ **§1's `⛔ DO NOT TOUCH` row for
`retry_jitter_ms()` — and every "owner-ruled parity constant" phrasing in this brief — is WITHDRAWN.** The helper
drives **four** coupled policies (DM origination · same-hop retry · BUSY_RX release · the **LBT backoff** at
`retry_jitter_ms()/2`), and the owed work is a separate **MeshRoute-native jitter arc**: four behaviour-identical
semantic helpers, `rts_contention_quantum_ms` = 11-B RTS airtime **as the scale not the window**, then a **flat**
`K = 1,2,3,4,6` sweep across the 36 scenarios + the 24-seed saturated twin + metal. ⛔ **K=3 is a candidate, not a
conclusion.** ✅ Only the `exchange_airtime_ms()` half of B158 is settled (`MEASURED-AND-LEFT`).

**Original brief below, unchanged apart from this header.** Live plan:
`docs/superpowers/plans/2026-08-08-hybrid-rts-flight-identity.md` §S5 (line ~402).
★ Role split: the QA-gate wrote this brief and will verify your claims **at the code**; an independent QA agent reviews;
**the OWNER commits and rules.** ⛔ **D4: never `git commit`, never offer to.** ⛔ **NEVER `git add -A`.**
⛔⛔ **NEVER `git checkout --` anything in this tree** — an agent did that inside a mutation loop earlier in this arc and
destroyed uncommitted work; it was recovered only from a byte-exact snapshot.

**Start state (verified 2026-08-10):** HEAD **`06b63c2`**, tree clean apart from two untracked docs
(this file, and a compaction handover **since DELETED 2026-08-10** — it had become a third drifting authority; its one
piece of unique content, the three proposed mechanical checks, is preserved as [[B168]]) and the register edit
that added [[B165]].
Native **1483 / 79761 / 0**. `lus` **`9150ad1a`**. Corpus **36/36, 0 assertion failures**.
Delivery authority **734 unique / `s06` 110** via `tools/dm_delivery_breakdown.py --mode dm --json` →
`totals.unique_deliveries`. PHY airtime **5 830 644 ms**.

---
## 0 — What this slice IS, and what it is NOT

S5 is **an audit that produces a table and a test.** It is **not** a licence to normalise timing constants.

- ✅ **IN:** a complete semantic-owner table for every production airtime/length constant in the RTS/CTS/DATA/ACK timing
  paths; verification that `start_rts_timeout` is non-negative-margin at **every** PHY; a new PHY-grid timing test;
  narrowly-targeted changes **only** where a use-site genuinely represents unicast-RTS airtime **and** the measured
  longer frame violates that site's own invariant.
- ⛔ **OUT:** blanket constant replacement · M/flood timing · Lua-parity constants · fixing [[B165]] · [[B161]] ·
  [[B163]] · [[B164]]'s airing half · [[B159]] · the parked mobile-home arc · the OLED UI · S6's cumulative gate ·
  any `wire_version` change (nothing here needs one).

★★ **The single most likely way to fail this slice is to "tidy" three different numbers into one.** The tree contains
`airtime_routing_ms(3)`, `(4)`, `(8)`, `(43)` and `cts_air_len` for frames that genuinely differ, and at least three of
those are **deliberate, documented, load-bearing divergences**. Your table must say *what each one owns*, and you change
only the ones whose owner's invariant is now false.

---
## 1 — The site inventory (all read at HEAD `06b63c2`; treat as a starting map, VERIFY each line yourself per V1)

### ⛔ DO NOT TOUCH — settled, with the reason

⛔⛔ **PARTLY WITHDRAWN 2026-08-10 (see the header): the `retry_jitter_ms()` row below is NO LONGER SETTLED.** B158 is
REOPENED and Lua parity is not a final requirement — the row stands only as a record of what S5 was told, and its
`⛔ unchanged (owner-ruled parity)` verdict describes the **pre-B158 baseline**, not an accepted design. The
`exchange_airtime_ms()` row IS still settled (`MEASURED-AND-LEFT`).

| site | expression | why it stays |
|---|---|---|
| `lib/core/node_mac.cpp:48` `retry_jitter_ms()` | `3 * airtime_routing_ms(8)` | ★ **Owner-ruled: an explicit Lua cross-engine parity constant.** The contract is written at `node.h:100-103` (*"3*airtime_routing(RTS_LEN=8) must equal …"*). It is a **timing** constant, not a description of our wire. |
| `lib/core/node.cpp:1152-1160` `exchange_airtime_ms()` | `a(8) + a(4) + DATA + a(4)` | ⚠ **MEASURED `DO-NOT-ADOPT`** (§B158-EXCHANGE-ARM). Three arms `a(8)`/`a(10)`/`a(11)` gave **691 / 695 / 691** corpus deliveries — and a sweep over 7/8/10/11/**13**/15 gave 56/56/60/56/**70**/55, i.e. an *arbitrary wrong* 13-B over-price gained **+14**, 3.5× the "correct" 10 B, **on a row with N=1**, with 35 of 36 rows byte-identical at every length 7→16. ⇒ **The +4 is NOISE. Do not re-open it as an optimisation.** ★ Its once-claimed dependency is **WITHDRAWN**: it does **not** feed `channel_capacity_C()`, which prices the flood exchange independently at `node_routing.cpp:142` (`airtime_routing_ms(43)`). Live consumers are the gateway herd-spread nibble (`node.cpp:1172`) and window-tail headroom (`node.cpp:1072`) only — ⚠ and the nibble is **saturated at 15**. |
| `node_mac.cpp:2011-2019` (M/flood arm of `start_rts_timeout`) | `airtime_routing_ms(flood ? 43 : 9)` | Already the **actual** wire length. Verify, do not edit. |
| `node_routing.cpp:142` | `airtime_routing_ms(43)` | The flood exchange, priced independently and correctly. |
| `node_mac.cpp:1492`, `:1640`, `node_mac_rx.cpp:724/783/829` | actual `len` / `cts_air_len` | Already actual-length (S1/S2b/S4). Verify, do not edit. |

### ✅ ALREADY FIXED — verify only, then say so explicitly

| site | what to verify |
|---|---|
| `node_mac.cpp:2021-2057` `start_rts_timeout` unicast arm | It prices `unicast_rts_wire_len(crypted)` + `terminal_cts_wire_len(crypted)` (10+6 plaintext / 11+7 crypted), with the no-pending-flight fallback taking the **crypted worst case** (over-waits ⇒ fails safe). ★ **§S5's gate demands the margin be non-negative at EVERY supported PHY** — that is the measurement below, not a re-read of the comment. |

### ⚠ THE ACTUAL AUDIT TARGETS — decide each by evidence, **in both directions**

1. **`node_mac.cpp:1224-1238` `nav_duration_rts`** — an overheard **unicast** RTS reserves `CTS(3 B) + DATA + ACK(3 B)
   + 3 gaps`. Since S1 the responder's answer may instead be a **terminal 6/7-B CTS**. Question: does any combination
   under-reserve? ⚠ Note the same function carries a **flat `+13` DATA header** that the code itself documents as
   over-reserving by 4–5 B (`:1268-1270`), and a terminal CTS means **no DATA follows at all**. ⇒ There is a plausible
   argument that no under-reservation exists — **it is a hypothesis, and you must test the other direction too**, at the
   PHYs where a 3 B→6/7 B step crosses a symbol boundary (see §2's grid). State the worst-case signed margin in ms.
2. **`node_mac.cpp:2080-2104` `start_pending_rx_expiry`** — `airtime_routing_ms(4) /*CTS_LEN=4*/` prices **our own
   outgoing** CTS. An ordinary CTS is **3 B** without a NAV hint and **4 B** with one (`frame_codec.cpp:360-361`,
   `frame_codec.h:273`). Two questions: **(a)** is 4 always ≥ the actual, at every PHY (i.e. is the error only ever the
   safe direction)? **(b)** ⛔ **can a path that emits a TERMINAL 6/7-B CTS ever reach this function?** A terminal CTS
   asserts *"already received"*, so no DATA should be expected — **prove that structurally, from the call graph, not
   from the comment.**
3. **`node_mac_rx.cpp:653`** — `cts_air_len = already_received ? 3u + c.id.width : 4u`. The ordinary arm is a
   hard-coded `4` where the frame that actually flies may be **3**. `:650` states this is a **deliberate** retention of
   the Lua `CTS_LEN=4` timing constant. Your job: confirm that claim is still true of every consumer of `cts_air_len`
   (ledger charge, timing, or both?), and record which. If it is a **billing** term, over-charging is a real airtime
   distortion, not a safe margin — say which it is.
4. **`node_mac_rx.cpp:1352`** — `airtime_routing_ms(3) + 1` post-ACK timer. The ACK is genuinely 3 B
   (`frame_codec.h:283-291`). Confirm and close.
5. **`node_mac.cpp:2058-2079` `start_ack_timeout`** and **`node_mac.cpp:1271-1291` `nav_duration_cts`** — both use
   `airtime_routing_ms(3)` for the **ACK**, which is correct at 3 B. Confirm; note the already-documented deliberate
   CR residual (`:2067-2073`) without re-litigating it.
6. **`node.cpp:506-510` and `:908-915`** — beacon max-defer, `airtime_routing_ms(protocol::beacon_max_bytes)`. Not an
   RTS length. ⚠ But `:506`'s comment asserts *"retry_jitter_ms() is the same RTS_LEN=8 timing constant"* — verify that
   sentence is still true and fix it if it drifted (V1).
7. ⚠ **A grep for `RTS_LEN`/`CTS_LEN` prose across `lib/core/`, `docs/frames.md`, `docs/protocol.md`.** Several
   comments describe a **7-byte** unicast RTS that no longer exists. **Fix the ones you touch (V1); list the ones you
   do not.**

### ⛔ [[B165]] — REGISTERED THIS SESSION, **DO NOT FIX IT HERE**

`RtsDutyDefer::buf[16]` (`node.h:2215`) silently truncates a **43-B FLOOD RTS** to 16 B and
`rts_duty_defer_fire` (`node_mac.cpp:1483`) transmits the fragment. Full evidence is in the register entry.
- ⛔ **The fix is its own slice** (`buf[16] → buf[43]` moves `sizeof(Node)` by ~27 B ⇒ D2 per-board RAM diff, and
  C1 forbids folding it into an audit).
- ✅ **What IS yours:** the **reachability measurement**, because you are already running the corpus —
  the count of `duty_cycle_blocked` with `label=RTS` **while the pending flight is a flood**, per scenario.
  ★ **A zero is a coverage statement, not an absolution** — if it is zero, say *"the corpus cannot reach it"*, and say
  what would.

---
## 2 — The PHY ledger (the deliverable the gate names)

Produce **one table**, in `simulation/BASELINE.md` under a new `§HYBRID-RTS-S5` section:

- `airtime_ms()` for **CTS 3 / 4 / 6 / 7 B** and **RTS 7 / 9 / 10 / 11 / 43 B** across the full supported grid:
  **SF 5..12 × BW {62 500, 125 000, 250 000, 500 000} Hz × CR 5..8** = 128 cells per length
  (this is the same grid §HYBRID-RTS-S0 used, so the two are comparable).
  ⓘ **RTS 7 B is retired from the wire** — include it as the *historical* column so the audit shows what changed.
- For **`start_rts_timeout`**: the **signed margin** = (armed delay) − (actual worst-case on-air time of the request
  plus its longest legal response), for plaintext **and** crypted, at every cell. ★ **The gate requires it to be
  non-negative everywhere.** Report the **minimum** cell and its PHY, not just "all pass".
- ⓘ Known reference points from §HYBRID-RTS-S0, for cross-checking your grid: the S1 change cost **+20 ms** at
  `s18`'s SF8/BW125k/CR5, **+41 ms** at `s06`'s SF8/BW62.5k/CR5, **+0 ms** at BW62.5k/CR5/SF10, and **+1049 ms** at
  BW62.5k/CR8/SF12. ⚠ **The sign is not monotone in SF** — if your grid is monotone, your grid is wrong.
- ⛔ **Do not add a new airtime formula.** `lib/core/airtime.h`'s `airtime_ms()` is the one model (including the
  SX126x SF5/SF6 §6.1.4 special case, which is **deliberate** — do not "simplify" it).

### The new test

Add a timing test (extend `test/test_airtime.cpp` if it fits its idiom — **grep it first, U1/U3** — otherwise a new
`test/test_mac_timing.cpp`) covering the grid above for the CTS 3/4/6/7 and RTS 9/10/11/43 lengths, and including
`s06`'s **BW 62.5 kHz** and the **SF12** worst case explicitly.
★★ **Every assertion must be able to fail.** For each new check, run a **mutation**: perturb the constant it guards and
show the test goes red. ⛔ **A test at match-count 1 that you did not mutate is not evidence** — this arc has 16+
recorded instances of instruments that could not fail.

---
## 3 — Gate (D1/D3)

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** — ⚠ the wrapper prints a false *"0 test cases"*;
   the binary prints the real counts. Report `cases / assertions / failures` and `grep -c "error:"`.
2. Rebuild `lus`, run **all 36 corpus streams**. Report the hash and **0 assertion failures**.
3. **Delivery: `tools/dm_delivery_breakdown.py --mode dm --json` → `totals.unique_deliveries`** is the **authority**
   (current **734**, `s06` **110**). The raw `delivered` event count is a **labelled cross-check only, never the figure
   of record**. Floor: **≥733 overall / ≥104 in `s06`** — ⚠ **conditional on [[B163]], which is OPEN**: `s07` carries a
   *correct* leased-id alias refusal and its figure may be short on every arm by a non-derivable amount. ⛔ **Diagnose
   `s07` separately before reading the total as a verdict; do not resolve B163 by choosing a number.**
4. Report **PHY airtime** against the current **5 830 644 ms**.
5. ⛔ **Board builds and `warning_census.sh` are SKIPPED by standing owner instruction** (*"we don't need to build all
   firmwares, we focus on lus at the moment"*) — the sweep is owed **once, at S6**. ★ **Say explicitly that they were
   skipped. Never imply a skipped gate passed** (D3).
6. ⛔ **Do not edit the `^### 36/36 corpus` anchor table.** Re-anchoring is the owner's single ruling and is still owed.
   ⚠ If you must verify that table's integrity, do it with a **full-region `diff` against `git show HEAD:`** — ⛔ **never
   an md5 of a `sed` window.** (The QA-gate published `e92d5cf1` as table-integrity evidence and it was the hash of a
   2-line window that never reached the table. **An md5 without its extraction command is not a citation.**)
7. Build arms **sequentially**; ⛔ never build while mutating source.

### ★★ Attribution — the reason this arc is measurable at all

**Five movement sources must stay causally SEPARABLE:** ① the parked mobile-home arc's **8** mobile movers ·
② S1's all-36 · ③ B160's zero · ④ S2/S2b/S2c's · ⑤ S4's 9. ⛔ **Never absorb the mobile-home 8 into an RTS
re-anchor** — three slices of deliberate draw-inertness bought that attribution.
⇒ If S5 moves any stream, **name which timing site caused it and why**, or revert the change and report instead.

---
## 4 — Method obligations (these four classes account for nearly all rework in this arc)

1. ★ **A fact or budget is established by the physical act, never by the request** — six recorded sites.
   ⚠ `_hal.tx()` returns **`ok` on ENQUEUE** (`lib/hal/device_hal.cpp:10-12`, *"Returns ok when queued"*) ⇒
   `TxHandOff::handed` means **queued**, never **aired**. `tx_initiating` / `tx_with_retry` return TRUE-ish for a
   **deferred** frame — a deferred frame is legitimately in flight, so **only definitive refusals are refusals**.
2. ★ **Identity is the whole tuple** — B133/B142/B147/B153/B157.
3. ★★ **Instruments that cannot fail — 16+ instances.** A telemetry counter is not a coverage measure. **"Byte-identical"
   is not "inert."** A discriminator returning zero must itself be positively controlled. A truncated or wrongly-scoped
   search is indistinguishable from a real negative — ⚠ **verifying "no live path does X" is a STRUCTURAL/call-graph
   question, never a text-grep question.** A comment asserting a guarantee is not the guarantee.
4. ★ **A correction placed anywhere but the instruction a reader follows** — nine sites, and [[B165]]'s stale
   `node.h:2215` comment is the tenth.

⚠⚠ **MEASURE; DO NOT PREDICT.** The QA-gate's own predictions were refuted **four times in four rounds**, all the same
shape — *a claim about what could not happen, asserted without checking the other direction*. Do not open a report with
an expectation; open it with a measurement.

⚠ `simulation/BASELINE.md`, both plans and the design spec carry **explicitly HISTORICAL blocks whose present-tense
sentences are FALSE BY DESIGN.** **Read the label before acting on any sentence**, and never let a grep count a
withdrawn quotation as a live claim.

---
## 5 — Report format

1. **The semantic-owner table** — every site, its owner, its invariant, and **verified / changed / unchanged-and-why**.
2. **The PHY ledger** + the minimum `start_rts_timeout` margin cell.
3. **The new test**, with its **mutation results** (which perturbation reddened which check).
4. **[[B165]]'s reachability count**, per scenario.
5. **Gate results**, with every skipped gate named.
6. **Exact final `git status --short`** and the statement that nothing was committed (D4).
7. ⛔ **Anything you could not establish — say so plainly rather than closing it.** A named open residue is a better
   outcome than a confident closure.

**Stop and return evidence rather than improvising if:** a timing change would move a stream you cannot attribute to a
single named site · closing an audit item needs a wire change · the margin is negative at some PHY and the fix is not
local · or an item turns out to need `sizeof(Node)` to move (that is a D2 slice, not this one).
