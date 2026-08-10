<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §MH-S5 — mobile-home candidates and lifecycle · dispatch brief · 2026-08-10

**Status: DISPATCHED 2026-08-10. THE MOBILE-HOME ARC IS UNPARKED.**

> ## ✅ OUTCOME — EXECUTED 2026-08-10. Read this before acting on any instruction below.
> **Evidence: `simulation/BASELINE.md` §MH-S5 (top). Native 1487/80613/0 → 1493/80752/0, `error:` 0.**
> **DONE:** §5.1's *hint* half is **NOT** done (see below) · **§8.2 in full** (freshness at selection, the
> after-gap FRESH-OBSERVATION reset, candidate retention across an adopt) · **§8.4's verified cross-layer widening**
> with the team-PHY rule preserved by construction · **§9.1–§9.3** (one `mobile_reg_remove(slot, reason)` primitive;
> the 25-minute PHYSICAL expiry of direct **and** redirect rows as a **deadline scan on `kAgingTimerId`** — no timer
> id allocated, `kCap == 91` asserted) · **§9.2's breadcrumb-receipt lifetime stamp** · **§9.4's eight-step test,
> with step 8 RED under mutation** · **two of [[B154]]'s three §10 items.**
> **GATES PASSED:** 11 (freshness half) · 12 · 13 · 14 · 15 · 16 · 24 · 25 (all three hysteresis arms) · 26 · 29.
> **NINE mutations, each at match count 1, all RED.** 35/36 corpus rows byte-identical; delivery **734 / `s06` 110**
> unchanged. **D2 decided EXPLICITLY: it does NOT trigger** — no data member added, `sizeof(Node)` **221880
> unchanged** — and `warning_census.sh` was run anyway and **EXITS 0** with **ΔRAM = 0** on all three censused board
> envs, which independently confirms the answer on real ABIs.
> ⛔ **TWO STOP CONDITIONS WERE HIT AND ARE REPORTED, NOT IMPROVISED AROUND:**
> 1. **[[B170]] — `s07` MOVES** (`de7920a2`/108936 → `f0601741`/108938). The move is **exactly the two new
>    `mobile_reg_expired` lines**; deleting just those two reproduces `de7920a2` byte-for-byte. §12.2 blocks the
>    slice ⇒ **the anchor table is NOT edited and an OWNER DECISION is owed** (re-anchor `s07` alone, or drop the
>    emit and regain 36/36 — both priced in the register).
> 2. **[[B171]] — §8.3's three searching-probe triggers and §8.4's `echo_tier != 0xFF` requirement are ONE
>    INDIVISIBLE, CORPUS-LIVE sub-slice** and were NOT landed. Measured: only a SEARCHING probe can set
>    `echo_tier`, so enforcing the requirement alone would make voluntary re-home **structurally unreachable**;
>    trigger 1 is reachable (17 tier-1 rosters in `s07`, 2 live `presence_rehome` firings that fire today on
>    RX-only evidence); and it drags in a third change, because `presence_ingest_probe` stamps `last_heard_ms` only
>    on the `!searching` arm — with rows now mortal, a weak-home mobile would be **evicted at 25 min while probing
>    every 1-8 min.** ⇒ gate 11's *unverified* half is **not** discharged.
> ⛔ **ALSO NOT DONE, stated rather than implied:** **§5.1 static-beacon wakeup was not implemented** — it adds an
> earlier DISCOVER on a beacon hint, i.e. new transmissions on a corpus-live path (every mobile scenario), so it is
> a second corpus-live sub-slice of the same shape as [[B171]] and landing it would have compounded an
> already-blocked attribution. **[[B154]](a)** (next-attempt remaining ms) stays open with its price named.
> **[[B152]] / [[B150]] / [[B144]] / [[B151]]** are arc-owed and were **not** S5 items — untouched, as instructed.
**Live spec: `docs/superpowers/specs/2026-08-07-mobile-home-attachment-reliability-design.md` — §S5 at line ~1020,
with its normative sections §5.1, §8.1–8.4, §9.1–9.4 and its gates in §12.1.**
★ Role split: the QA-gate wrote this brief and verifies your claims **at the code**; an independent QA agent reviews;
**the OWNER commits and rules.**

⛔⛔ **HARD SAFETY RULES — the tree carries the ENTIRE uncommitted hybrid-RTS arc plus S0–S4b of this arc.**
- ⛔ **NEVER `git commit`, never offer to** (D4). ⛔ **NEVER `git add -A`.**
- ⛔⛔ **NEVER `git checkout --` anything, and NEVER check out another commit in this working tree.** An agent
  destroyed uncommitted work this way earlier in this arc; it was recovered only from a byte-exact snapshot. If you
  need another commit's content, use `git worktree add` in `/tmp` and remove it afterwards.
- ⛔ **Do not "restore" any S0–S4b work from HEAD — it is NOT there.** HEAD is `06b63c2`; all of S0–S4b is
  uncommitted working-tree state.

---
## 0 — Read first, and read the parking record before the spec body

1. **The PARKING RECORD at the head of the spec** — it states exactly what landed (S0–S4b), what is not started, and
   what is owed. ⛔ The spec's original status line *"proposed design; no implementation is claimed"* is **FALSE and
   withdrawn**; large parts are implemented.
2. `CLAUDE.md` — working rules; cite rule IDs.
3. `docs/2026-08-05-owner-rulings-ledger.md` — **§1 is settled, do not re-litigate.** ⚠ **§3 rules 4 and 5 are new and
   they bind you: never quote an owner ruling unless you hold the exact characters (use reported form), and a QA
   recommendation relayed by the owner is STILL a recommendation — label the source, not the messenger.**
4. `simulation/BASELINE.md` §MH-S0…S4b for this arc, and the **§HYBRID-RTS-*** sections for what changed underneath it.
5. `docs/2026-07-30-open-bug-register.md` — B137, B139, B142, B144–B147, B150–B155, and B159/B161/B163–B169.

---
## 1 — ⚠⚠ WHAT CHANGED UNDER THIS ARC WHILE IT WAS PARKED. Do not trust a pre-parking figure.

The spec and its notes were written **before** the hybrid-RTS arc. Since then:

- ★★ **THE CORPUS ANCHOR TABLE WAS RE-ANCHORED TODAY (2026-08-10, owner-ruled).** `simulation/BASELINE.md`'s
  `^### 36/36 corpus` table now holds **post-RTS** md5s and event counts, verified by a from-scratch 36-row
  re-measurement. ⇒ **§12.2's rule — *"static keystone scenarios byte-identical in every slice; a mover in S1, S4,
  S5 or S6 BLOCKS that slice"* — is still in force, but it must now be measured against the NEW table.**
  ⛔ **Never compare against the old anchors or against any delivery/airtime figure in a pre-parking note.**
- **The unicast RTS is now 10 B plaintext / 11 B crypted** (was 7 B), the terminal CTS is 6/7 B, and
  `sizeof(Node)` moved **221288 → 221880**. ⇒ ⚠ **Line references inside the spec may have rotted. V1: verify every
  `file:line` at the source before relying on it, and fix the drifted comments you touch.**
- **The delivery authority is `tools/dm_delivery_breakdown.py --mode dm --json` → `totals.unique_deliveries`**, now
  **734 overall / `s06` 110**, floor **≥733 / ≥104** and ⚠ **conditional on [[B163]], which is OPEN**. The raw
  `delivered` count is **759** and is a **cross-check only, never the figure of record.**
- ⛔ **`retry_jitter_ms()` is UNTOUCHABLE here** — [[B158]] is owner-reopened as a separate MeshRoute-native jitter
  arc. If you find yourself editing it, `airtime_routing_ms(8)`, or the R3.x golden jitter assertions, **stop**.

---
## 2 — S5's scope (spec §S5, line ~1020)

1. **Static-beacon wakeup for seeking/recovering mobiles** (§5.1).
2. **Passive-only candidate collection, freshness, bidirectional verification, hint-vs-authority** (§8.1–8.2).
   ★ *Passive-only* is the load-bearing word: collection must add **no** transmissions.
3. **The three searching-probe triggers and "adequate before optimal"** (§8.3); **hysteretic switching** including
   verified **same-PHY cross-layer** candidates, with the **team-PHY restriction preserved** (§8.4).
4. **One removal primitive + the 25-minute direct/redirect age-out** (§9.1–9.3).
   ⓘ Verified: `protocol::mobile_liveness_ms = 1500000` (25 min).
   ⛔⛔ **THERE ARE ZERO FREE TIMER IDS — `TimerWheel::kCap` is 91 and ALL are consumed** (top allocated id 90;
   `kMobileOfferBackoffTimerId` is 80). ⇒ **The age-out MUST be a deadline scan on an existing timer, exactly as S2
   did. A new timer id is not available and `kCap` must not be raised as a convenience** — gate 16 asserts `kCap == 91`.
5. ★★ **The expired-id return case and its own native test (§9.4).** **Step 8 is the point of the whole test** —
   *stale traffic addressed to A's old local id must NOT be delivered to B as A*; steps 5–7 are only the setup that
   makes step 8 reachable. ⇒ **The last-mile decision must be hash-anchored, never local-id-anchored.**
   ⚠ **Epoch alone is NOT the collision protection** — it distinguishes *generations of one mobile*, never *two
   different mobiles*. Hash matching + reservation-aware free-id selection + the targeted CLAIM DENY as a **backstop**
   provide it jointly. ⛔ **The DENY is never the allocator.**
6. **[[B154]]** — §10's remaining two diagnostic fields are **reassigned by name to S5** in the operative §10 FIELD
   LEDGER. They are yours.

### Gates this slice owns (spec §12.1)
**11 · 12 · 13 · 14 · 15 · 24 · 25 · 26 · 27 · 28 · 29**, plus **16** (`kCap == 91`, asserted not inspected).
★ Note the shape of two of them, because a weaker test passes vacuously:
- **24** — healthy adequate home + measurably stronger candidate ⇒ **assert ZERO additional transmissions**, not merely
  "no adopt".
- **13** — at 25 min **minus 1 ms** the row remains; **at** 25 min it is removed *everywhere* and its local id is
  reusable. **Both sides of the boundary.**
- **28** — the strong-link idle-loss test must **assert the ≈8-minute bound and name it as the accepted trade-off**, so
  a later "regression" is read as the ruling it is.

⛔ **OUT OF SCOPE — do not fold in:** **S6** (product integration: fresh-config default, companion status, Heltec
states/commands, [[B151]]'s §12.2 scenarios, docs) · [[B159]] · routing **T1–T3** · [[B158]]'s jitter arc ·
[[B161]] · [[B163]] · [[B164]]-airing · [[B165]] · [[B166]] · the OLED UI. ⓘ **[[B152]]** (§7.2's two permissively-worded
link refreshers) and **[[B150]]**/**[[B144]]** are arc-owed but **not** S5 items — leave them, and say so.

---
## 3 — Method obligations (four classes; they account for nearly all rework in both arcs)

1. ★★ **A fact or budget is established by the PHYSICAL ACT, never the request** — **six** sites already, four of them
   in *this* arc (B84's `_tries`, B139's `_presence_miss`, B145/B146's OFFER counters, the re-CLAIM budget).
   ⚠ **`tx_initiating` / `tx_with_retry` return TRUE-ish for a DEFERRED frame** — S1's own key measured fact — so a
   return-value test **cannot** detect admission failure. ⚠ `_hal.tx()` returns `ok` on **ENQUEUE**
   (`lib/hal/device_hal.cpp:10-12`) ⇒ `handed` means **queued**, never **aired**.
2. ★★ **Identity is the whole tuple** — B133 (`seq` without kind) · B142 (`LbtKind` alone) · B147
   (`(key_hash32, proposed_node_id)`, not hash alone) · B153/B157. ⇒ §9.4 **is this class**: `(hash, local_id, epoch)`.
3. ★★ **Instruments that cannot fail — 16+ instances, and TWO were found in the last two slices alone** (a
   cross-check row copied from the authority it was meant to check; a 5-B CTS rejection probe built from a *terminal*
   frame so the terminal-bit branch refused it before the length gate). ⇒ **Mutation-prove every new assertion**, and
   **positively control every discriminator that returns zero.** A truncated or wrongly-scoped search is
   indistinguishable from a real negative — **"no live path does X" is a STRUCTURAL/call-graph question, never a
   text-grep question.**
4. ★ **A correction placed anywhere but the instruction a reader follows** — **ten** sites. If you supersede a claim,
   fix it **where a reader will act on it**, not only in a note further down.

★ **S0's characterization tests: rewrite in place, NEVER delete or disable** ([[B101]]). When S5 fixes a defect its S0
test pinned, that test **goes red for real** and is rewritten to assert the new behaviour, keeping the record.

⚠⚠ **MEASURE; DO NOT PREDICT.** The QA-gate's own predictions were refuted **four times in four rounds**, all one
shape: *a claim about what could not happen, asserted without checking the other direction.*

---
## 4 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** — ⚠ the wrapper prints a false *"0 test cases"*.
   Current baseline: **1487 / 80613 / 0**, `grep -c "error:"` 0. Report cases/assertions/failures.
2. **All 36 corpus rows against the NEWLY RE-ANCHORED table**, 0 assertion failures. ⓘ Reproduce with
   `lus <config.json> <events.ndjson>` then `md5sum | cut -c1-8`; event count from the `lus: N events emitted` line.
   ★★ **§12.2: any mover in S5 BLOCKS the slice** — report every row's md5 and, if one moves, **stop and report**
   rather than re-anchoring. ⛔ **Do not edit the `^### 36/36 corpus` table** — it was just re-anchored on the owner's
   single ruling and no agent may touch it again without a new one.
3. **Delivery: `--mode dm --json` → `totals.unique_deliveries`** — floor **≥733 / `s06` ≥104**, conditional on B163.
   State the raw cross-check separately and label which is authoritative.
4. **Board builds:** ⛔ **skipped by standing owner instruction** *(lus is the instrument)* — **UNLESS your change
   moves `sizeof(Node)`, a carrier size, a board `#if`, or the linker**, which triggers **D2** and then you owe the
   **full ten-env sweep + `warning_census.sh` + `-Wreorder` + the `sizeof`/`offsetof` asserts + per-board RAM/flash
   diffs**. ★★ **Decide this EXPLICITLY and say which way it went** — ⚠ [[B169]] was a board-only `-Wunused-variable`
   that survived four slices precisely because this question was never asked, and it is **invisible to native and to
   all 36 corpus streams by construction**. **If you add any state to `Node`, assume D2 applies.**
5. **Probes/mutations: print match counts and positive controls.** ⛔ A probe at match count 1 you did not mutate is
   not evidence.
6. ★ **Record which gates ran and which were skipped. Never imply a skipped gate passed** (D3).

---
## 5 — Report format

1. **What landed**, per §S5 item, with the `file:line` of each change.
2. **Gate results** — native · the 36-row md5 table vs the new anchors · delivery · the D2 decision and its
   consequence · probe match counts and mutation results.
3. **§9.4's eight-step test**, with step 8 called out and its mutation control.
4. **[[B154]]**'s two diagnostic fields, and what consumes them (⛔ a field with no reader is the defect this arc
   already fixed twice — `_presence_reg_confirmed` and `presence_claim_max_retries` were 2-writes/0-reads).
5. **Register dispositions** (M1) and any **new** finding, with its measurement.
6. **Bench-script obligations** (M2) — only if a metal-only behaviour is added **and** you can name the exact expected
   console line. ⛔ **A step that cannot fail is worse than none.**
7. **Exact final `git status --short`** + nothing committed (D4).
8. ⛔ **Anything you could not establish — say so plainly rather than closing it.** A named open residue beats a
   confident closure.

**Stop and return evidence rather than improvising if:** a corpus row moves · the age-out appears to need a new timer
id · §9.4 step 8 cannot be made to fail under mutation · a change would move `sizeof(Node)` and you cannot run the
ten-env sweep · or an owner/QA decision is needed (**report it as owed — never substitute one**, ledger §3 rules 1–5).
