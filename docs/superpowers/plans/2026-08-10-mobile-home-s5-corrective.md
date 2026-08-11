<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §MH-S5-FIX — the corrective slice: redirect rows are not directly-hosted rows · dispatch brief · 2026-08-10

**Status: DISPATCHED 2026-08-10, on two QA blocking findings against §MH-S5 — both independently VERIFIED AT THE CODE
by the QA-gate before dispatch.** ⛔ **This slice comes BEFORE S5b and BEFORE S6.**
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; an independent QA agent reviews;
**the OWNER commits and rules.** ⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything, and
never check out another commit in this tree** — it holds the whole uncommitted mobile-home arc.

ⓘ HEAD is **`95287cf`** ("mobile home progress") as of the end of this slice, and ✅ **the owner-ruled corpus
re-anchor IS now committed in it.** ⛔ **SUPERSEDED — this brief was dispatched saying HEAD was `34246e8` and that
the re-anchor was uncommitted; the owner has committed twice since, and the agent correctly did NOT "fix" the
apparent discrepancy.** Working-tree baseline: native
**1493 / 80752 / 0**, `lus` **`7074d78f`**, delivery **734 / `s06` 110** (raw cross-check 759).

---
## 1 — Blocker A: a redirect row is still treated as a directly-hosted row

**The spec is explicit** (`docs/superpowers/specs/2026-08-07-mobile-home-attachment-reliability-design.md:815`):
> *redirect rows never reserve last-mile service or advertise as directly hosted.*

And §9.1 (`:789-790`) says an expired row is *"absent from rosters and coverage accounting"* — the same two consumers.

**Verified 2026-08-10: three live sites include EVERY registry row, with no redirect test anywhere.** The registry's
marker is `redirect_home_id != 0` (used correctly by `mobile_reg_age_out` and by `host_mobile_row`, so the datum
exists and is trustworthy — it is simply not consulted by these three):

| site | what it does today | why it is wrong |
|---|---|---|
| `lib/core/node_join.cpp` `presence_emit_roster` (~`:1112`) | copies **every** `_mobile_reg[i]` into `PRosterEntry` | ⇒ the **old** home advertises a moved mobile as its own for up to 25 minutes. The roster wire entry has **no redirect marker**, so a listener cannot tell |
| `lib/core/node_mac_rx.cpp` last-mile forward (~`:1446`) | matches `key_hash32` over all rows, then re-addresses to `mobile_local_id` | ⇒ a DM for a **moved** mobile is forwarded to a local id **at this home**, where the mobile no longer is |
| `lib/core/node_channel.cpp` `flood_set_my_coverage` (~`:1548`) and `flood_any_unmarked` (~`:1564`) ⛔ (**name corrected 2026-08-10 — this brief said `flood_mark_direct_neighbours`, which DOES NOT EXIST**; the wrong name came from §MH-S5's residual comment and was copied without grepping it — a V1 violation by the QA-gate) | `seen_set` / `seen_test` over all rows | ⇒ a redirected mobile is counted as covered / drives re-floods it cannot receive |

### ⚠ THE TRAP — read this before you filter the roster

`presence_emit_roster` ends with:
```cpp
for (uint8_t i = 0; i < n; ++i) _active->_mobile_reg[i].deleg_fail = false;   // "entry i maps 1:1 to _mobile_reg[i]"
```
★★ **That 1:1 assumption is exactly what a redirect filter breaks.** Once `n < _mobile_reg_n`, `i` no longer indexes
the registry, so this loop would clear **the wrong rows'** one-shot `deleg_fail` bits — a silent loss of the
"delegated send dropped" signal the mobile depends on to fire `send_failed{no_route}` once.
⇒ **Carry the source slot per entry** (a small parallel `uint8_t slot[]`, or clear inside the copy loop) and
**assert the mapping in a test**. ⛔ Do not leave the loop keyed on the entry index.

### Required tests (QA-specified; each needs a mutation control)
A redirect row:
1. **is absent from the direct-host roster**;
2. **cannot trigger direct last-mile forwarding**;
3. **contributes no channel coverage** (both `flood_set_my_coverage` and `flood_any_unmarked`);
4. ★ **still performs its intended hash-location redirect** — this is the positive control that proves you filtered
   the right thing. ⓘ `node_hashlocate.cpp` tests `redirect_home_id != 0` **first** and is deliberately **not**
   liveness-gated; that behaviour must survive unchanged.
5. **the `deleg_fail` one-shot clears the correct row** when the roster is filtered.

---
## 2 — Blocker B: the pre-coverage age-out was omitted, and the stated reason is not sufficient

**The spec** (`:831-836`) requires `mobile_reg_age_out()`:
> *from the normal aging timer; before allocating a local id or refusing because `cap_host_mobiles` is full;*
> **before emitting a roster / using hosted rows for channel coverage.** *Do not duplicate age predicates at each
> consumer.*

S5 wired the first two and **not** the third, documenting an accepted **60-second** stale window at
`lib/core/node_join.cpp:735-738` because `flood_set_my_coverage` / `flood_any_unmarked` are `const`.
⛔ **QA rejects that reason and so does this brief: a `const` signature is a choice, not a constraint.** It can be
changed, or — better, and what the spec's own *"do not duplicate age predicates"* line points at — **both consumers
can read ONE shared "live direct rows" abstraction** that already excludes expired **and** redirect rows.

★★ **Note how neatly the two blockers converge: coverage must exclude expired rows AND redirect rows. One predicate,
one place, both consumers.** Solving them separately is how the duplicate-predicate problem the spec warns about gets
created. ⛔ But **do not** silently widen the shared predicate into the roster's or the last-mile's decision without
saying so — state exactly which consumers share it and what each one now excludes.

---
## 3 — Also in scope (small, and QA-specified)

**Fix the prose, not the accessor,** at `lib/core/node.h:646`: the diagnostic is described as *"exactly what re-home
would consider"*, and it is not. It counts **fresh, compatible, bidirectionally-verified** candidates — it does **not**
apply the quality delta, the 60 s hold, the 5-minute dwell, or the current-home exclusion that selection uses.
⇒ Reword it to say what it counts. ⛔ Do not change the number; the golden test pins it.

---
## 4 — Out of scope

⛔ **S5b** (the narrowed coupled sub-slice — see §6) · **S6** · [[B170]]'s re-anchor decision (**owner-deferred until
after this slice**, see §5) · [[B154]](a) (**owner-deferred**) · [[B152]] · [[B150]] · [[B144]] · [[B151]] · [[B158]]'s
jitter arc (⛔ `retry_jitter_ms()`, `airtime_routing_ms(8)` and the R3.x golden jitter assertions are untouchable) ·
[[B159]] · [[B161]] · [[B163]] · [[B164]] · [[B165]] · [[B166]] · routing T1–T3 · the OLED UI.

---
## 5 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   Baseline **1493 / 80752 / 0**, `grep -c "error:"` 0.
2. **Rebuild `lus` and re-run all 36 rows.** ⚠⚠ **`lus` WAS FOUND STALE IN THE BUILD DIR DURING S5 VERIFICATION at
   the previous slice's hash — and a stale `lus` silently reports the PREVIOUS slice's streams, which looks EXACTLY
   like "nothing moved."** ⇒ **Print the `lus` md5 beside every corpus figure, every time.**
3. **Expected mover set:** `s07` already moves (`de7920a2` → `f0601741`, +2 `mobile_reg_expired` lines, [[B170]]).
   ⛔ **Anything else that moves must be attributed to a named change in THIS slice, or you stop and report.**
   ⚠ A redirect filter on coverage/roster/last-mile **could legitimately move mobile scenarios** — if it does,
   **that is a finding to report with its cause, not a thing to absorb.**
   ⛔ **Do not edit the `^### 36/36 corpus` table.** It was re-anchored 2026-08-10 on the owner's single ruling; the
   owner has deferred the `s07` re-anchor until **after** this slice, explicitly so it is not done twice.
4. **Delivery:** `tools/dm_delivery_breakdown.py --mode dm --json` → `totals.unique_deliveries`; floor
   **≥733 / `s06` ≥104**, conditional on the open [[B163]]. Raw `delivered` (759) is a **cross-check only**.
5. ★★ **Answer the D2 question EXPLICITLY and report which way it went.** If you add a data member, change a carrier
   size, a board `#if` or the linker, D2 fires and you owe the **ten-env sweep + `warning_census.sh` + `-Wreorder` +
   the `sizeof`/`offsetof` asserts + per-board RAM/flash diffs**. `sizeof(Node)` is **221880** today.
   ⓘ A parallel `uint8_t slot[cap_host_mobiles]` **local to the emit function** costs no `Node` bytes — a member does.
   ⚠ [[B169]] was a board-only warning that survived four slices because nobody asked this. **If in doubt, run
   `warning_census.sh`** — S5 ran it voluntarily and it cost little.
6. ⛔ **There are ZERO free timer ids** (`TimerWheel::kCap == 91`, all consumed). No new timer.
7. **Mutation-prove every new assertion**, print match counts, and positively control every discriminator that can
   return zero. ⛔ A probe at match count 1 you did not mutate is not evidence.
8. Record which gates ran and which were skipped; **never imply a skipped gate passed** (D3).

---
## 6 — For the record: S5b is NARROWER than S5 claimed, and one S5 claim was FALSE

★★ **QA measured that the "indivisible" set is smaller. The genuinely coupled minimum is three items:**
1. the weak/missed-home **searching probe**;
2. a **searching probe refreshes an existing direct hosted row**;
3. a **voluntary switch requires a verified echo**.

⇒ **The attributable-home-path failure trigger and §5.1's static-beacon wakeup are SEPARATE slices**, not part of that
knot. Do not treat them as blocked.

⛔⛔ **AND THE URGENCY ARGUMENT S5 GAVE WAS WRONG — CORRECTED HERE SO IT IS NOT REPEATED: S5 claimed it had
introduced a live *"healthy weak home is evicted at 25 minutes while probing every 1–8 minutes"* defect. IT HAS NOT.**
Verified at the code: `presence_ingest_probe` (`lib/core/node_join.cpp`) refreshes the hosted row via
`mobile_reg_touch()` on its `mine >= 0 && !searching` arm, and **today's attached checks are non-searching
selected-home probes** — so they **do** refresh `last_heard_ms`. ⇒ **That failure becomes reachable only if attached
checks are made *searching* WITHOUT the refresh change (item 2 above), i.e. it is a HAZARD THAT S5b MUST NOT CREATE,
never a bug that exists today.** ⓘ The QA-gate relayed the false claim to the owner; it is corrected in both
directions here.

---
## 7 — Method obligations

1. ★★ **A fact or budget is established by the physical act, never the request** — six sites. `_hal.tx()` returns
   `ok` on **ENQUEUE** (`lib/hal/device_hal.cpp:10-12`) ⇒ `handed` means **queued**, never **aired**;
   `tx_initiating`/`tx_with_retry` return TRUE-ish for a **deferred** frame.
2. ★★ **Identity is the whole tuple** — and **this slice is that class again**: a registry row's identity is
   `(hash, local_id, direct-vs-redirect)`. Treating `hash` alone as "I host this" is precisely blocker A.
3. ★★ **Instruments that cannot fail — 16+ instances**, two found in the last two slices alone (a cross-check copied
   from the authority it checked; a rejection probe whose frame was refused by an earlier branch than the one under
   test). ⚠ **"No live path does X" is a STRUCTURAL/call-graph question, never a text-grep question.**
4. ★ **A correction placed anywhere but the instruction a reader follows** — ten sites. Fix claims **where a reader
   acts on them.**

⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval that was not given; **never quote an
owner ruling** — use reported form; and ⚠ **a QA recommendation relayed by the owner is STILL a recommendation —
label the source, not the messenger.**

**Report:** what landed per blocker with `file:line`; the five required tests and their mutations; the corpus table
**with the `lus` md5**; the delivery figures; the D2 decision; the register/doc updates; exact final
`git status --short` and that nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** a corpus row other than `s07` moves · de-const'ing the coverage
readers reaches further than the two functions · the shared live-rows abstraction would change the roster's or the
last-mile's semantics beyond excluding expired/redirect rows · or a change moves `sizeof(Node)` and you cannot run the
ten-env sweep.
