<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §HYBRID-RTS-S6 — cumulative decision gate + durable documentation · dispatch brief · 2026-08-10

**Status: DISPATCHED 2026-08-10. Live plan: `docs/superpowers/plans/2026-08-08-hybrid-rts-flight-identity.md` §S6
(line ~428).** ★ Role split: the QA-gate wrote this brief and verifies your claims **at the code**; an independent QA
agent reviews; **the OWNER commits and rules.**

⛔⛔ **HARD SAFETY RULES — the tree carries UNCOMMITTED WORK from S5 and the whole arc's documentation.**
- ⛔ **NEVER `git commit`, never offer to** (D4). ⛔ **NEVER `git add -A`.**
- ⛔⛔ **NEVER `git checkout --` anything, and NEVER check out another commit in this working tree.** An agent
  destroyed uncommitted work with `git checkout --` earlier in this arc; it was recovered only from a byte-exact
  snapshot. **To build an old commit, use a SEPARATE WORKTREE** — see §3's recipe. There is no other sanctioned way.
- ⛔ **Do not edit the `^### 36/36 corpus` anchor table** until §2's owner ruling is in hand. **Producing a PROPOSED
  replacement is your deliverable; landing it is not.**

**Start state (measured 2026-08-10, not carried forward):** HEAD **`06b63c2`**; uncommitted = the S5 comment-only
`lib/core` edits + `test/test_airtime.cpp` + arc documentation. Native **1487 / 80607 / 0**, `error:` 0.
`lus` **`9150ad1a`**. Delivery authority **734 unique / `s06` 110**; raw `delivered` cross-check **759**
(⚠ **759, not 734** — a prior note had copied the authority into the cross-check, making it unable to disagree).
PHY airtime **5 830 644 ms**. `sizeof(Node)` **221880**.

---
## 0 — What S6 is

Three deliverables, in this order. **Do not start the documentation until the gate has run** — writing "final" docs
against unmeasured numbers is how this arc's worst defects were born.

1. **The cumulative decision gate** — one comparison table across BASE / DELETE / the uniform-4B control / HYBRID,
   and the acceptance-floor verdict (§1).
2. **The re-anchor PROPOSAL** — measured, per-row, attributed, and **owner-gated** (§2).
3. **The full D1/D2 gate**, deferred for ~10 slices and now due: **all 10 board envs** + `warning_census.sh` +
   `-Wreorder` + `sizeof` asserts + per-board flash/RAM diff (§3). Then durable documentation (§4).

⛔ **OUT OF SCOPE — do not fix, do not close:** [[B158]] (⚠ **REOPENED by owner ruling — see §5**) · [[B159]] ·
[[B161]] · [[B163]] · [[B164]]'s airing half · [[B165]] · [[B166]] · [[B160-COV]]/[[B160-SIB]] · routing T1–T3 ·
the parked mobile-home arc · the OLED UI · the MeshRoute-native jitter arc.

---
## 1 — The cumulative comparison table

Produce **one** table with a column per arm: **BASE · DELETE · uniform-4B control · HYBRID (current)**, and a row per
item below. ★★ **Label every cell as either RE-MEASURED THIS SLICE or QUOTED FROM RECORD, with its source section.**
A table that mixes the two silently is the [[B162]] defect wearing a new hat.

Rows: native cases/assertions/failures · `s18` hash + event count · 36/36 assertions and changed streams · unique
deliveries **overall / `s06` / `s18` / `s27` / twin** · duplicate application deliveries · send failures by terminal
class · **RTS/CTS/DATA/ACK/NACK and total airtime using each event's ACTUAL PHY** · old-tuple at-risk/alias count vs
new plaintext/encrypted identity collision count · flash/RAM per board and `sizeof` deltas.

### ⛔ Which arms you may re-measure, and which you MUST NOT rebuild

- ✅ **HYBRID (current)** — re-measure everything from the working tree.
- ⛔ **BASE, DELETE, uniform-4B control — DO NOT rebuild them.** They require old code, and the only safe mechanism
  (a separate worktree) is reserved in this slice for the **flash/RAM** baseline, which is cheap and mechanical.
  Re-running old protocol arms invites exactly the cross-tool-revision comparison [[B162]] exists to forbid.
  ⇒ **Quote the [[B162]] ladder, which is the one authoritative single-revision/single-run-set measurement:**
  **BASE 733 · DELETE 707 · S1 690 · S2 724 · S2c 719 · S4 734**, `s06` BASE 104 / S4 110. Mark them **ARCHIVAL**.
- ⚠ **If a required row has NO archival figure for an old arm, write `NOT MEASURED` and say why.** ⛔ **Do not
  reconstruct it by inference, and do not leave the cell blank** — a blank reads as zero.

### The acceptance floor

- ★ **≥733 unique overall and ≥104 in `s06`**, by `tools/dm_delivery_breakdown.py --mode dm --json` →
  `totals.unique_deliveries` (**the authority**; the raw `delivered` count is a **cross-check only, never the figure
  of record** — state both and label which is which, every time).
- ⚠⚠ **`≥733` IS CONDITIONAL ON [[B163]], WHICH IS OPEN.** `s07_seattle_mobile_meshroute` carries a **correct**
  leased-id alias refusal (two mobiles genuinely wear the same wire id at different times), so **`s07`'s figure may be
  short on every arm by an amount that is not derivable** without a time-windowed alias map. ⇒ **A shortfall landing in
  `s07` is NOT automatically a regression, and a pass is not automatically clean. Diagnose `s07` separately before
  reading the total as a verdict, and ⛔ do not resolve B163 by choosing a number.**
- ⛔ **`s27` == 0 assertion failures is a SEPARATE, NON-TRADEABLE requirement.** It is not folded into the delivery
  figure and no delivery count may be exchanged for it. Both `s27` target payloads must deliver.
- **Duplicates:** no worsening beyond the pre-B153 count. ⓘ [[B159]] is **not** fixed here and stays open and
  explicitly independent.
- **Identity:** new plaintext identity has **zero** aliases by construction/census; encrypted identity has **zero
  observed** aliases. Report the census N, not just the zero — ★ **a zero from an unstated N is not a measurement.**
- **Airtime** must be reported against **both** BASE and DELETE, using actual event PHY. ⛔ **Never describe a
  symbol-event count as airtime.**

---
## 2 — The re-anchor PROPOSAL (⛔ owner-gated; measured by me before dispatch)

★★★ **MEASURED BY THE QA-GATE BEFORE DISPATCH, ALL 36 ROWS: EVERY SINGLE ROW HAS MOVED FROM THE ANCHOR TABLE.**
**36 / 36 MOVED — 16 rows with a CHANGED EVENT COUNT, 20 rows with BYTE-ONLY drift (identical event count).**

⛔⛔ **READ THE CONSEQUENCE BEFORE YOU PLAN: the `^### 36/36 corpus` table currently anchors NOTHING.** Every md5 in it
is stale, so the *"36/36 — 0 assertion failures"* claim repeated throughout this arc was always about **in-scenario
assertions**, never about reproducing an anchored hash. ⚠ **Do not report "the corpus is green" in a way that implies
hash agreement.** ⓘ This is expected and deliberate: the arc never re-anchored precisely because re-anchoring
re-baselines every stream at once and would have destroyed per-slice attribution. It is now S6's job to close.

**The 16 event-count movers (anchored → current), for you to attribute:**

| scenario | anchored | current | Δ |
|---|---|---|---|
| `s06_seattle_lifecycle` | 67 009 | 65 835 | **−1 174** |
| `s07_seattle_mobile_meshroute` | 109 391 | 108 936 | −455 |
| `s15_three_layer` | 52 488 | 52 293 | −195 |
| `s15_three_layer_metal` | 52 386 | 51 232 | **−1 154** |
| `s16_dense_gateway` | 26 267 | 24 397 | **−1 870** |
| `s17_metro` | 1 181 343 | 1 181 179 | −164 |
| `s18_meshroute` | 271 629 | 269 905 | **−1 724** |
| `s20_random_mesh` | 40 079 | 40 566 | **+487** |
| `s22_mobile_team_meshroute` | 1 820 | 1 822 | +2 |
| `s24_static_and_team_multihop_meshroute` | 1 556 | 1 583 | +27 |
| `s27_cross_layer_mobiles_meshroute` | 9 758 | 9 205 | −553 |
| `s28_mixed_team_channels_meshroute` | 3 852 | 3 856 | +4 |
| `s29_mixed_leaf_team_meshroute` | 2 031 | 2 020 | −11 |
| `s35a_cochannel_isolation_meshroute` | 2 388 | 2 356 | −32 |
| `s37_team_homed_origin_meshroute` | 748 | 750 | +2 |
| `twin_9node_dm` | 13 837 | 13 221 | **−616** |

⚠ **The deltas run in BOTH directions** (`s20` +487 vs `s16` −1 870) ⇒ ⛔ **no constant correction exists and none may
be inferred** — the same trap [[B162]] recorded. ⓘ Worked example from the byte-only set: `s09_two_layer_gateway`
hashes **`71120178`** against an anchored **`f171652c`** at an identical **2 266** events.
★ **VERIFY THIS SWEEP, DO NOT TRUST IT** — reproduce it by §2.1's method and say whether your figures match.
ⓘ **The raw sweep is in the repo, not in agent scratch** (a proven 33-assert scenario was once lost to volatile
scratch): **`simulation/s6_anchor_drift.tsv`** — TSV, one row per scenario, columns
`name · status · anchored_md5 · current_md5 · anchored_events · current_events`. ⛔ It is an INPUT to your work, not a
result to re-publish; delete it once its content is folded into your report and BASELINE proposal.

**Your deliverable — a PROPOSAL, not an edit:**
1. Re-run all 36 rows and produce, per row: **anchored md5 → current md5**, **anchored event count → current**, and
   **WHICH SLICE caused the movement**.
2. ★★ **Attribution is the point of the whole exercise, and with 36/36 moved it is also the hard part — a table where
   every row moved is indistinguishable from an un-audited re-baseline unless each row carries a cause.** The five
   movement sources MUST stay causally separable:
   ① the **parked mobile-home** arc's **8** mobile movers · ② **S1**'s all-36 · ③ **B160**'s zero ·
   ④ **S2/S2b/S2c**'s · ⑤ **S4**'s 9 (== exactly the 9 credit-firing scenarios, set identity).
   ⛔⛔ **NEVER absorb the mobile-home 8 into an RTS re-anchor** — three slices of deliberate draw-inertness bought
   that attribution, and the mobile-home arc is **unparked next**.
3. ⚠ **A row whose movement you cannot attribute to a named slice is a FINDING, not a rounding error.** Register it.
4. Present the proposed replacement table **in your report and in a clearly-labelled `§HYBRID-RTS-S6` PROPOSAL block
   in `BASELINE.md`** — ⛔ **the `^### 36/36 corpus` table itself stays untouched until the owner rules.**

### 2.1 — Method (reproducible, and state it beside every figure)

`lus <config.json> <events.ndjson>` then `md5sum | cut -c1-8`; event count from the `lus: N events emitted` line.
⛔ **Verify the anchor table by a FULL-REGION `diff` against `git show HEAD:simulation/BASELINE.md`** — ⛔ **never by
an md5 of a `sed` window.** (The QA-gate once published `e92d5cf1` as table-integrity evidence; it was the hash of a
2-line window that never reached the table. **An md5 without its extraction command is not a citation.**)

---
## 3 — The full D1/D2 gate (deferred ~10 slices; now DUE)

★★ **ALL TEN BOARD ENVS ARE MANDATORY THIS TIME, and here is the citation rather than a guess.** The standing owner
ruling is a **3-env** gate (`gateway` + `xiao_sx1262` + `xiao_esp32s3`), with all ten **only when `sizeof(Node)`, a
board `#if`, or the linker moves. `sizeof(Node)` MOVED: `221288 → 221880 (+592)` in §hybrid-rts S2**
(`lib/core/node.h:3110` carries the assert and the note). ⇒ **D2 is triggered; the exception applies; build all ten.**

Envs (`platformio.ini`): `xiao_sx1262` · `heltec_v3` · `xiao_esp32s3` · `gateway` · `gateway_heltec` ·
`gateway_esp32s3` · `production` · `xiao_mobile` · `heltec_mobile` · `xiao_esp32s3_mobile`. Build **sequentially**;
⛔ never build while mutating source.

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** — ⚠ the wrapper prints a false *"0 test cases"*;
   the binary prints the real counts. Report cases/assertions/failures and `grep -c "error:"`.
2. All 36 corpus rows, 0 assertion failures, with the §2 md5 table.
3. **All 10 board envs + `warning_census.sh`.** ★ Report the warning **multiset** per env and compare to the pinned
   baseline; **`-Wswitch` must be 0** — warnings are **gate-blocking** by standing ruling.
4. **`-Wreorder` clean · every `sizeof`/`offsetof` `static_assert` passing · per-board flash AND RAM diff.**
   ⓘ **Native alignment hides board padding — that is exactly why the per-board diff exists.**
5. **Codec negative/mutation probes: print match counts and positive controls.** ★★ **Every assertion must be shown
   capable of failing.** ⛔ A probe at match-count 1 that you did not mutate is not evidence.

### 3.1 — The flash/RAM baseline, and the ONLY safe way to get it

**Baseline commit: `6c9ce89`** *("Slices - mobile home register / pre RTS uniquness changes")* — the last commit before
the RTS work (`863ad31` starts it). ⇒ **Use a SEPARATE WORKTREE. Never check it out here:**

```
git worktree add /tmp/mr-base-6c9ce89 6c9ce89      # detached; leaves THIS tree untouched
# build the 10 envs there, record flash/RAM, then:
git worktree remove /tmp/mr-base-6c9ce89
```
⚠ **State clearly that the baseline includes the mobile-home arc** — so the diff you report is *"RTS arc only"*,
and ⛔ **do not attribute mobile-home's footprint to the RTS work.** ⓘ ±32 B ARM literal-packing quantum and the
±200 B xtensa object-set effect are **known** ([[B86]], [[B63]]) — do not report noise at that scale as a finding.

---
## 4 — Durable documentation (only AFTER the gate)

- **`docs/frames.md`** — ★ **the exact 10/11-byte unicast RTS byte offsets and the 6/7-byte terminal CTS layout. This
  is [[B167]]'s explicitly owed half**, deliberately deferred by S5 because inventing offsets inside a timing audit was
  the wrong risk. 9 B / 43 B are unchanged. ⚠ **Keep it WIRE-oriented — fields and byte offsets.** ⛔ S5 added ~20
  lines of prose to a file `CLAUDE.md` already flags as over-prosed: **migrate that argument to `protocol.md` and
  leave offsets here.**
- **`docs/protocol.md`** — identity, validation, the completed-flight cache, already-received and implicit-ACK
  behaviour. ⓘ It has **zero** `RTS_LEN` hits but was never broadly audited — audit it.
- **`simulation/BASELINE.md`** — each slice's causal delta plus the §2 proposal block.
- **`MEMORY.md`** — one durable line pointing at the ruled design.
- **Bench script (M2)** — nodes reflashed together; a lost-ACK/retry observation **only if a controllable metal
  procedure exists**. ⛔ **A step that cannot fail is worse than none** — if you cannot name the exact expected console
  line, write no step and say why.

### Bug-register dispositions (M1) — ⛔ read every constraint

| bug | S6's action |
|---|---|
| **B153** | ⛔ **MAY NOT BE CLOSED — [[B161]] is OPEN** (typed hash answers lack a canonical origin). Record the dependency; do not close. |
| **B157** | close **only** if exact identity + restored implicit ACK are both verified, preserving the deletion history |
| **B158** | ⛔⛔ **DO NOT CLOSE — REOPENED BY OWNER RULING. See §5.** |
| **B156** | remains closed **only** if `src_hint` cannot enter the restored cache key — **verify structurally** |
| **B159 · B161 · B163 · B164 (airing) · B165 · B166 · B168** | stay **OPEN**; state each explicitly |
| **B160** | close **only** after all three requeue paths preserve explicit `Plane` through the shared helper |
| **B167** | close the byte-table half here if §4's offsets land |

---
## 5 — ⛔⛔ B158 IS REOPENED — DO NOT CLOSE IT, AND DO NOT TOUCH `retry_jitter_ms()`

**The owner ruled on 2026-08-10 (reported form, no quotation — owner-rulings-ledger §1.13) that Lua parity is NOT a
final MeshRoute jitter requirement, and that [[B158]] stays open until MeshRoute-native jitter has been independently
measured and selected.** ✅ Only the `exchange_airtime_ms()` half is settled (`MEASURED-AND-LEFT`).

★★ **The reason it is bigger than a constant: ONE helper drives FOUR unrelated policies**, so retuning any one
silently retunes the rest — DM origination spreading (`node_mac.cpp:316`) · same-hop RTS/ACK retry
(`node_cascade.cpp:365`/`:398`) · BUSY_RX release (`node_mac_rx.cpp:2077`) · and ⚠ **the LBT release backoff**
(`node.cpp:508`/`:913`, `max(1, retry_jitter_ms()/2)`). ⇒ That work is **a separate arc, not an S6 line item.**

⛔ **If you find yourself editing `retry_jitter_ms()`, `airtime_routing_ms(8)`, or the R3.x golden jitter assertions,
STOP** — you are in the jitter arc, not S6. The `test_airtime.cpp` case is deliberately framed as a **temporary
pre-B158 baseline that B158 is expected to replace**; leave it.

---
## 6 — Method obligations (four classes; they account for nearly all rework in this arc)

1. ★ **A fact or budget is established by the physical act, never the request** — six sites. ⚠ `_hal.tx()` returns
   `ok` on **ENQUEUE** (`lib/hal/device_hal.cpp:10-12`) ⇒ `TxHandOff::handed` means **queued**, never **aired**;
   `tx_initiating`/`tx_with_retry` return TRUE-ish for a **deferred** frame, so **only definitive refusals are
   refusals**.
2. ★ **Identity is the whole tuple** — B133/B142/B147/B153/B157.
3. ★★ **Instruments that cannot fail — 16+ instances.** A telemetry counter is not a coverage measure.
   **"Byte-identical" is not "inert."** A discriminator returning zero must itself be positively controlled. A
   truncated or wrongly-scoped search is indistinguishable from a real negative — ⚠ **"no live path does X" is a
   STRUCTURAL/call-graph question, never a text-grep question.** ⓘ The freshest example: a cross-check row that had
   been copied from the authority it was meant to check, so it could not disagree.
4. ★ **A correction placed anywhere but the instruction a reader follows** — **ten** sites.

⚠⚠ **MEASURE; DO NOT PREDICT.** The QA-gate's own predictions were refuted **four times in four rounds**, every one
the same shape: *a claim about what could not happen, asserted without checking the other direction.*
⛔ **PROVENANCE (this arc's most expensive lesson, five incidents): never attribute a decision to the owner unless you
hold it first-hand, and NEVER put an owner ruling in quotation marks unless the exact characters are held — record it
in reported form.** A reconstruction in quotes is indistinguishable from an invention, *including to its author*.

⚠ `BASELINE.md`, both plans and the design spec carry **explicitly HISTORICAL blocks whose present-tense sentences are
FALSE BY DESIGN.** **Read the label before acting on any sentence**, and never let a grep count a withdrawn quotation
as a live claim.

---
## 7 — Report format

1. The **cumulative table**, every cell labelled re-measured vs archival.
2. The **acceptance-floor verdict**, with `s27` and `s07`/B163 handled separately as above.
3. The **re-anchor proposal**: per-row md5/event deltas + slice attribution + any unattributable row as a finding.
4. **Gate results**: native · corpus · **all 10 envs** · warning multisets · `-Wreorder`/`sizeof`/RAM+flash diff vs
   `6c9ce89` (worktree) · probe match counts and controls.
5. **Documentation diffs**, and for `frames.md` the offsets with their codec citation.
6. **Register dispositions**, honouring every constraint in §4's table.
7. **Exact final `git status --short`** + the statement that nothing was committed (D4) + **every skipped gate named**.
8. ⛔ **Anything you could not establish — say so plainly rather than closing it.** A named open residue is a better
   outcome than a confident closure.

**Stop and return evidence rather than improvising if:** a board env fails to build · a warning multiset moves and you
cannot attribute it · the acceptance floor is missed outside `s07` · a corpus row moved with no attributable slice ·
re-anchoring appears to require the owner's ruling before you can proceed (**it does — that is expected, stop and
report**) · or documenting an offset would require inventing one.
