<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §MH-S5b — the coupled minimum: weak/missed-home searching probe · host-row refresh · verified-echo switch · 2026-08-11

**Status: DISPATCHED 2026-08-11 · ✅ IMPLEMENTED 2026-08-11 — all three items landed, gated and UNCOMMITTED (D4).**
Evidence: `simulation/BASELINE.md` **§MH-S5b** (the newest note). [[B171]] closed; **[[B177]] and [[B178]] opened.**
⛔⛔ **ONE OWNER DECISION IS OWED AND NOTHING HERE SUBSTITUTES FOR IT: delivery falls 734 → 728, all six inside `s07`,
all six attributed to §8.3's trigger 1** (`s06` 110 unchanged; total PHY airtime −0.51 %). Three options are priced in
[[B178]]/**O5**. ⛔ **Two of this brief's own premises were measured and are wrong** — item 3 blocks NEITHER of the two
live `presence_rehome` firings (it is byte-inert on all 36 rows), and the §0 eviction hazard was mitigated all along by
the hosted-mobile BEACON touch, which is itself ungated and is now [[B177]]. Live spec: `docs/superpowers/specs/2026-08-07-mobile-home-attachment-reliability-design.md`
— **§8.2 (line ~713), §8.3 (~722), §8.4 (~746)**, gates in §12.1 (**11's unverified half · 24 · 25 · 26 · 27 · 28**).
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; an independent QA agent reviews;
**the OWNER commits and rules.** ⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything, and
never check out another commit in this tree.**

**Baseline (measured, working tree):** HEAD **`95287cf`**; native **1504 / 80878 / 0**, `error:` 0; `lus`
**`79b01d8a`**; **all 36 rows anchor-identical** (⇐ `s07` was re-anchored to `2ce470f9`/108951 and [[B170]] is CLOSED);
delivery **734 / `s06` 110** (raw cross-check 759); `sizeof(Node)` **221880**; `TimerWheel::kCap` **91**, zero free ids.

---
## 0 — Scope: EXACTLY three items, because they are genuinely coupled

QA measured the coupling and **narrowed** what §MH-S5 had called indivisible. **This slice is these three and nothing
else:**

1. **the weak/missed-home searching probe** — §8.3 triggers **1** (home quality weak or critical) and **2** (home
   misses a check, on the way to `lost`);
2. **a searching probe refreshes an existing direct hosted row** (host side);
3. **a voluntary switch requires a verified echo** — §8.4's `echo_tier != 0xFF` requirement.

★★ **WHY THEY CANNOT BE SPLIT — verified at the code, so do not re-derive it:**
- Only a **searching** probe elicits an echo from a non-home node, and `echo_tier != 0xFF` means *"this candidate
  echoed one of OUR probes"* (`lib/core/node.h:639`). ⇒ **Landing item 3 alone would make voluntary re-home
  structurally unreachable**, because nothing would ever set `echo_tier`.
- `presence_ingest_probe` refreshes the hosted row via `mobile_reg_touch()` on its **`mine >= 0 && !searching`** arm
  only. Today's attached checks are **non-searching** (`node_mobile.cpp:822`, `searching = claiming ? 1 : 0`), so they
  **do** refresh. ⇒ ⚠⚠ **Landing item 1 without item 2 CREATES a real defect: with rows now mortal (§MH-S5), a
  weak-home mobile would probe every 1–8 minutes and still be EVICTED at 25 minutes**, because its own probes would
  no longer touch the row.
- ⛔ **That defect DOES NOT EXIST TODAY.** §MH-S5 claimed it did and the QA-gate relayed that claim; **both were wrong
  and it is corrected in the record.** It is a hazard **this slice must not create** — never a bug you are fixing.

### ⛔ Explicitly OUT of scope — separable, and QA said so
- **§8.3 trigger 3** (an *attributable home-path failure*) — **its own slice.**
- **§5.1 static-beacon wakeup** — **its own slice** (it adds transmissions on a corpus-live path).
- **S6** (product integration) · [[B154]](a) (**deferred**: diagnostic/product only, and not worth growing `Node` or
  widening the HAL — if ever needed, store **one authoritative deadline**) · [[B152]] · [[B150]] · [[B144]] ·
  [[B151]] · [[B158]]'s jitter arc (⛔ `retry_jitter_ms()`, `airtime_routing_ms(8)`, the R3.x golden jitter assertions
  are untouchable) · [[B159]] · [[B161]] · [[B163]] · [[B164]] · [[B165]] · [[B166]] · [[B171]] (this slice's landing
  is what closes it) · routing T1–T3 · the OLED UI.

---
## 1 — What §8.3 must NOT become

⛔⛔ **The whole point of §8.3 is an AIRTIME hole, and the spec says it outright: *"Do not send a searching probe merely
because another node may be stronger… a fleet of mobiles each canvassing for a better home is a fleet-wide roster storm
bought with nothing."*** ⇒ The permitted triggers are a **closed list of three** (two of them yours). Everything else
stays passive:
- collect same-PHY hints and overheard rosters while attached — **zero transmissions**;
- ordinary healthy-home checks stay **selected-home** probes;
- on home **loss** the existing searching probe fires immediately — ⓘ **already implemented**
  (`node_mobile.cpp:~774`, the `_presence_miss > presence_probe_k_miss` arm). **Do not duplicate it.**
- the home-side roster coalescing + 10 s `presence_roster_min_interval_ms` rate-limit stay the shared de-storm.

★ **And keep the current home while it is adequate even if another is measurably stronger** — gate **24** asserts
**zero additional transmissions** in that case, not merely "no adopt". A weaker test passes vacuously.

---
## 2 — §8.4: what item 3 must and must not change

A voluntary switch requires **all** of: fresh evidence (§8.2) · **recent bidirectional verification — a roster echo or
OFFER, never a beacon alone** · candidate bottleneck ≥ **2 tiers** above the current-home bottleneck
(`presence_rehome_tier_delta = 2`) · the **60 s hold** (`presence_candidate_hold_ms`) · the **5-minute anti-flap dwell**
(`presence_rehome_dwell_ms = 300000`) · compatible wire version.

⇒ **Item 3 adds the `echo_tier != 0xFF` requirement to that conjunction.** ⛔ **Do not weaken any other term** and do
not re-tune a constant — all six exist and are pinned.
⚠ **§8.4's cross-layer widening and the team-PHY restriction already landed in §MH-S5** — verify, do not redo.
⛔ **The verified-candidate diagnostic is the ELIGIBILITY FLOOR, not the selection predicate** (`node.h:~645`, corrected
in §MH-S5-FIX); selection additionally applies the tier delta, hold, dwell and current-home exclusion. **Do not
"reconcile" the two by changing the count** — a golden test pins it.

---
## 3 — ⚠⚠ EXPECT CORPUS MOVERS. THIS SLICE IS DIFFERENT FROM THE LAST THREE.

Measured before dispatch by §MH-S5: **§8.3's triggers are corpus-LIVE.** `s07` carries **17 tier-1 rosters**, and there
are **2 live `presence_rehome` firings that fire today on RX-only evidence** — item 3 will **block** those. So unlike
§MH-S5-FIX/FIX2 (byte-inert), **this slice is expected to move mobile streams.**

⇒ **Handle it exactly like [[B170]] was handled, which is the precedent that worked:**
1. **Do NOT stop dead** — a mover is the expected outcome here, not a stop condition.
2. ⛔ **Do NOT edit the `^### 36/36 corpus` table.** Re-anchoring is the owner's ruling, and [[B170]] has just shown
   the sequence: measure → attribute → report → owner rules → land.
3. ★★ **Attribute EVERY moved row to ONE of the three items, by in-tree A/B** (disable each item independently and
   show which rows return). An unattributable mover is a **finding**, not a rounding error.
4. **Report the delivery figures for each arm**, because a re-home that no longer fires can change delivery. Floor
   **≥733 / `s06` ≥104**, conditional on the open [[B163]]. ⚠ **If delivery FALLS, that is a design question for the
   owner — not something to fix by relaxing item 3.** State it plainly with the count.

⚠ ⛔ **§12.2 says a mover in S5 blocks the slice.** That rule stands; this brief's position is that the block is
discharged by **attribution + an owner ruling**, exactly as B170's was — **not** by you re-anchoring.

---
## 4 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   From **1504 / 80878 / 0**.
2. **Rebuild `lus` and print its md5 beside every corpus figure.** ⚠⚠ **A stale `lus` reports the PREVIOUS slice's
   streams and looks exactly like "nothing moved" — this has already produced one false conclusion in this arc.**
   From **`79b01d8a`**.
3. **All 36 rows**, 0 assertion failures, with the §3 attribution.
4. **Delivery** via `tools/dm_delivery_breakdown.py --mode dm --json` → `totals.unique_deliveries` (**the authority**;
   raw `delivered` is a labelled cross-check only).
5. ★★ **Answer D2 explicitly.** `sizeof(Node)` is **221880**. ⚠ **This slice is the most likely of the arc to need
   state** (a "home is weak/missed" edge, an echo timestamp). **If you add a member, D2 fires and you owe the ten-env
   sweep + `warning_census.sh` + `-Wreorder` + `sizeof` asserts + per-board RAM/flash diffs.** ⓘ Prefer deriving from
   what exists (`_presence_miss`, `echo_tier`, `presence_quality_tier`) over storing. Run `warning_census.sh` either
   way — the last two slices did and it cost little.
6. ⛔ **Zero free timer ids** (`kCap == 91`). The 1–8 minute probe cadence already exists; **do not allocate a timer.**
7. **Gates 11 (unverified half) · 24 · 25 · 26 · 27 · 28.** ★ Note the shapes that pass vacuously otherwise:
   **24** = *zero additional transmissions*, counted · **25** = all three hysteresis arms (delta, hold, dwell) ·
   **27** = steady-state airtime **no worse** than the existing adaptive cadence, measured at each quality tier with
   other eligible statics audible · **28** = assert the **≈8-minute** strong-link idle-loss bound **and name it as the
   accepted trade-off**, so a later "regression" reads as the ruling it is.
8. **Mutation-prove every new assertion; print match counts.** ⛔ A probe at match count 1 you did not mutate is not
   evidence. ★ **Include a control for the hazard in §0**: a mutation that makes a searching probe NOT refresh the host
   row must turn a test RED — that is the guard against re-creating the 25-minute eviction.

---
## 5 — Method obligations

1. ★★ **A fact is established by the physical act or the recorded state, never by an inference.** [[B174]] came from
   *"it is physically in range if it is asking"* — a proximity guess replacing a state check.
   ⚠ `_hal.tx()` returns `ok` on **ENQUEUE** (`lib/hal/device_hal.cpp:10-12`); `tx_initiating`/`tx_with_retry` return
   TRUE-ish for a **deferred** frame ⇒ only **definitive** refusals are refusals.
2. ★★ **Identity is the whole tuple** — a registry row is `(hash, local_id, direct-vs-redirect, live-vs-expired)`;
   `host_row_live_direct()` is the single predicate and it now has **ten** consumers. **Use it; do not re-spell it.**
3. ★★ **Instruments that cannot fail — 17+ instances, and the freshest one is a WARNING FOR THIS SLICE:** §MH-S5-FIX's
   *"the redirect still redirects"* control could not see an **age-shaped** over-fix, because the test queried right
   after the breadcrumb and §9.2 stamps `last_heard_ms = now` there — **the row was never old enough for the gate to
   bite.** ⇒ **When you write a control, ask what state the test actually reaches**, not what it names.
4. ★ **A correction placed anywhere but the instruction a reader follows — ten-plus sites.** Fix claims where a reader
   acts on them.

⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval that was not given; **never quote an
owner ruling** — reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation** — label
the source, not the messenger.

**Report:** each of the three items with `file:line`, its tests and mutations · the corpus table **with the `lus`
md5** and the per-item A/B attribution of every mover · delivery per arm · the D2 answer · gates 11/24/25/26/27/28 with
their non-vacuous evidence · register/doc updates ([[B171]] closes here if all three land) · exact final
`git status --short` and that nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** a mover cannot be attributed to one of the three items · delivery falls
below the floor outside `s07` · item 1 appears to need a new timer or new `Node` state you cannot avoid · gate 27 shows
steady-state airtime **worse** than today · or an owner/QA decision is needed (**report it as owed, never substitute
one**).
