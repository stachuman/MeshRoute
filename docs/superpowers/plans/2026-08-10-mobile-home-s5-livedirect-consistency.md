<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §MH-S5-FIX2 — apply the live-direct boundary CONSISTENTLY to every home/last-mile service path · 2026-08-10

**Status: DISPATCHED 2026-08-10 on three QA findings against §MH-S5-FIX, all verified at the code by the QA-gate, plus
an OWNER RULING that makes the rule systematic.** ⛔ Still **before** S5b and S6.
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; an independent QA agent reviews;
**the OWNER commits and rules.** ⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything, and
never check out another commit in this tree.**

**Baseline (measured, working tree):** HEAD **`95287cf`** ("mobile home progress") — ✅ the owner-ruled corpus
re-anchor **is** committed. Native **1497 / 80816 / 0**, `error:` 0. `lus` **`972fbf5c`**.
`s07` **`2ce470f9` / 108951**; the other 35 rows byte-identical to the anchor table. Delivery **734 / `s06` 110**
(raw cross-check 759). `sizeof(Node)` **221880**. `TimerWheel::kCap` **91**, zero free ids.

---
## 0 — ★★★ THE OWNER'S RULING (reported form — no quotation offered, per owner-rulings-ledger §3 rule 4)

**The owner ruled on 2026-08-10 that a registry row at or beyond `mobile_liveness_ms` must NOT provide direct hosted
service or last-mile service, even before physical compaction — and that this rule must be applied CONSISTENTLY to
all such service paths.**

⇒ That settles the design question §MH-S5-FIX left half-open. It also **supersedes an in-source instruction**: the
comment at `lib/core/node.h:1483-1487` currently says *"⛔ NOT the same question as the bare `redirect_home_id == 0`
tests … Do not mechanically fold them in here."* ⛔ **That instruction is WITHDRAWN for service paths and must be
corrected in place** — leaving it would be the tenth-plus instance of a correction placed anywhere but the instruction
a reader follows.

⚠ **The ruling is about SERVICE, not about the redirect mechanism.** A redirect row must still **answer a
hash-location redirect** (that is the redirect doing its job, and `node_hashlocate.cpp`'s redirect fork is
deliberately **not** liveness-gated). ⇒ **Gating the redirect answer would be a regression, and §MH-S5-FIX's
"the redirect still redirects" positive control exists precisely to catch it. Keep that control green.**

---
## 1 — Finding A: the MOBILE_SEND ownership scan has NO row-kind test at all

`lib/core/node_mac_rx.cpp:1379-1381`:
```cpp
for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
    if (_active->_mobile_reg[i].key_hash32 == ui->source_hash) { ours = true; break; }
```
⇒ Any matching hash counts as *"ours"* — **redirect rows and expired rows included.**

⛔⛔ **THE PREVIOUS SLICE'S JUSTIFICATION IS FALSE AND IS WITHDRAWN.** It argued the mobile *"is physically in range if
it is asking."* **It is not:** mobile delegation is sent to the home through the ordinary routed `do_send` path
(`lib/core/node_channel.cpp:810`, `lib/core/node_hashlocate.cpp:1659`), so a MOBILE_SEND can reach the **old** home
**over multiple hops**. ⚠ Note the shape — an assumption about physical proximity used to license skipping a state
check; **the state is the authority, never an inference about radio range.**

**Two consequences, both real:**
1. an old home keeps providing **upstream delegation** after it has already recorded that the mobile moved;
2. ★★ if re-origination fails, `presence_mark_deleg_fail()` sets the bit on the **redirect** row — and since
   §MH-S5-FIX correctly filters redirect rows out of the roster, **that bit can no longer be carried. The failure
   becomes INVISIBLE**, so the mobile never fires its one-shot `send_failed{no_route}`. ⇒ This is a defect the roster
   fix *created* in combination with the missing scan test; the two must be closed together.

**Required:** gate the scan on `host_row_live_direct(i)`, with **a direct-row positive control** and **redirect and
expired negative cases**.

---
## 2 — Finding B: the expiry half is applied on ONE path out of five (the half-and-half state)

§MH-S5-FIX added expiry to the forwarded-DM last mile (`node_mac_rx.cpp:1461`) only. **Verified full inventory of the
bare `redirect_home_id == 0` tests — QA named three, there is a FOURTH:**

| # | site | what it provides | disposition |
|---|---|---|---|
| 1 | `lib/core/node_hashlocate.cpp:1681` | the `send_by_hash` **direct last-mile enqueue** | ★ **service — gate it.** A locally-originated send and a forwarded send currently treat the same expired row **differently** |
| 2 | `lib/core/node_hashlocate.cpp:1407` `forward_requester_key_to_mobile` | forwards a requester's key **to the mobile** | ★ **service — gate it** |
| 3 | `lib/core/node.cpp:214` | hosted-mobile **grant pre-check** (`hosted ⇒ direct last-mile keeps the type`) | ★ **service decision — gate it** |
| 4 | `lib/core/node_hashlocate.cpp:1346` `host_mobile_ed_pub` | answers with a hosted mobile's cached `ed_pub` | ⚠ **NOT named by QA — YOUR CALL, and you must ESTABLISH it, not assume it.** Answering *"here is my hosted mobile's key"* looks like direct hosted service under the ruling; but if you find it is only reachable behind a gate that already excludes expired rows, say so **with the call graph**, and leave it |

⚠ **QA's own words on the adjacent consumers: *"Some may already be protected transitively, but that should be
established rather than inferred."*** ⇒ For **every** site you leave alone, give the **structural** reason —
⛔ **"no live path does X" is a call-graph question, never a text-grep question.**

⛔ **The half-and-half state is explicitly the least desirable outcome.** Either finish it (preferred, and what the
owner ruled) or revert the expiry half from `node_mac_rx.cpp:1461` and do the systematic change separately.
**Do not leave it mixed.**

---
## 3 — Finding C: `presence_mark_deleg_fail()` needs an explicit disposition

⛔ **"Self-clearing" was the previous slice's word and it is wrong** — a hidden bit can persist until re-CLAIM or
expiry. QA's recommended disposition, adopted here:
1. **refuse to set `deleg_fail` unless the row is live AND direct**;
2. **clear any existing `deleg_fail` when a breadcrumb converts the row to a redirect**
   (`lib/core/node_mac_rx.cpp`'s `DATA_TYPE_MOBILE_BREADCRUMB` arm — the same place that stamps the lifetime clock).

★ Point 2 also closes a **race**: delegation accepted while the row was direct, breadcrumb arriving **before** the
failure was recorded. Without it the bit lands on a row nothing will ever advertise.

---
## 4 — [[B170]]: the re-anchor is APPROVED but CONDITIONAL

**The owner approved re-anchoring `s07_seattle_mobile_meshroute` to `2ce470f9` / 108951 — provided the completed
corrective work still reproduces that value after these live-direct fixes.**
⇒ ⛔ **Do NOT edit the `^### 36/36 corpus` table.** Measure `s07`, report it, and:
- if it is still **`2ce470f9` / 108951**, say so — the approval's condition is met and the owner lands it;
- if it **moved again**, that is a **new decision point**: report the new value **with its cause attributed to a named
  change in this slice**, and stop. ⛔ Do not assume the approval transfers to a different number.

---
## 5 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   From **1497 / 80816 / 0**. Report counts and `grep -c "error:"`.
2. **Rebuild `lus`, print its md5 beside every corpus figure.** ⚠⚠ **A STALE `lus` REPORTS THE PREVIOUS SLICE'S
   STREAMS AND LOOKS EXACTLY LIKE "NOTHING MOVED" — this has already produced one false conclusion in this arc.**
   From **`972fbf5c`**.
3. **All 36 rows.** Expect `s07` to be the only pre-existing mover. ⛔ **Any additional mover must be attributed to a
   named change in this slice — with an A/B if the cause is not obvious — or you stop and report.**
   ⓘ §MH-S5-FIX's own A/B showed the last-mile, coverage-seed and coverage-unmarked filters were each **byte-inert on
   all 36 rows**, and only the **roster** filter moved anything. Use that as your prior, ⛔ **not as a prediction** —
   the QA-gate has had **five** predictions of this exact shape refuted in this arc.
4. **Delivery** via `tools/dm_delivery_breakdown.py --mode dm --json` → `totals.unique_deliveries`; floor
   **≥733 / `s06` ≥104**, conditional on the open [[B163]]. Raw `delivered` is a **cross-check only**.
5. ★★ **Answer D2 explicitly.** Predicates are functions and cost no `Node` bytes; a data member does. If
   `sizeof(Node)` (**221880**), a carrier size, a board `#if` or the linker moves, you owe the ten-env sweep +
   `warning_census.sh` + `-Wreorder` + `sizeof` asserts + per-board RAM/flash. ⓘ §MH-S5-FIX ran
   `warning_census.sh` voluntarily and it cost little — **do the same**; [[B169]] was a board-only warning that
   survived four slices because nobody asked.
6. ⛔ Zero free timer ids. ⛔ `retry_jitter_ms()` / `airtime_routing_ms(8)` / the R3.x golden jitter assertions are
   untouchable ([[B158]] is a separate arc).
7. **Mutation-prove every new assertion; print match counts.** ★ Keep §MH-S5-FIX's **over-filter control** green —
   the test that turns RED if you gate the redirect *answer* (`h_resolved` 1→0). It is the guard against fixing this
   too hard.

---
## 6 — Method obligations

1. ★★ **A fact is established by the physical act / the recorded state, never by an inference** — and Finding A is
   exactly that class: *"in range because it is asking"* replaced a state check with a guess about radio range.
   ⚠ `_hal.tx()` returns `ok` on **ENQUEUE** (`lib/hal/device_hal.cpp:10-12`); `tx_initiating`/`tx_with_retry` return
   TRUE-ish for a **deferred** frame.
2. ★★ **Identity is the whole tuple** — a registry row's identity is `(hash, local_id, direct-vs-redirect, live-vs-expired)`.
   Matching `hash` alone is Findings A and B.
3. ★★ **Instruments that cannot fail — 16+ instances**, and Finding A shows a *new* variant: the roster fix made a
   real failure **unreportable**, so the absence of a `send_failed` would have read as success.
4. ★ **A correction placed anywhere but the instruction a reader follows — ten-plus sites**, and `node.h:1483-1487` is
   the next one if you do not fix it.

⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval that was not given; **never quote an
owner ruling** — reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation** — label
the source, not the messenger.

**Report:** each finding with `file:line` and its tests + mutations · the full disposition table from §2 including every
site you left alone **and its structural reason** · the corpus table **with the `lus` md5** · `s07`'s value against
B170's condition · delivery · the D2 answer · register/doc updates including the `node.h:1483-1487` correction ·
exact final `git status --short` and that nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** a corpus row other than `s07` moves without an attributable cause ·
`s07` moves to a value other than `2ce470f9` · gating a site would require de-const'ing or restructuring beyond the
function · `host_mobile_ed_pub`'s classification is genuinely ambiguous · or a change moves `sizeof(Node)`.
