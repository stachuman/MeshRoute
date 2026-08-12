<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B181-INV — why does EVERY mobile-involving DM in `s07` fail to arrive? · INVESTIGATION brief · 2026-08-11

⛔⛔ **AMENDED 2026-08-11 BY INDEPENDENT QA — FOUR CORRECTIONS, DELIVERED TO THE RUNNING AGENT. READ THEM BEFORE ANY
SENTENCE BELOW:**
1. ⛔ **`s27` IS NOT THE POSITIVE CONTROL** — §1's *"6 of 6 static→mobile arriving"* is read off a **collapsed**
   row (five `node_id = 0` mobiles → one label) and is **WITHDRAWN as evidence**. ★ Clean controls:
   **`s21_mobile_dm_milestone_meshroute`** (static→mobile, hash-addressed) and **`s22_mobile_team_meshroute`**
   (both directions). `s27` = protocol/corpus comparison only, **never metric authority**.
2. ⛔ **§1's wording overstates twice.** Proven: only that the tool **emits 59 named failed rows**. ⇒ the metric is
   **not SYNTACTICALLY blind to mobile pairs, but whether those pairs are the correct RUNTIME identities is STILL
   UNDER INVESTIGATION** (candidate (c)). And they do **not subtract 59** — they **contribute zero arrivals** and are
   **up to 59 potentially RECOVERABLE deliveries, subject to dedup and correct attribution.**
3. ★★ **THE `send_hash` A/B IS NOT DECISIVE WITHOUT TIMING CONTROL.** A hash send may do an H lookup, refresh
   bindings/routes, park and delay the DM, and thereby dodge a collision ⇒ *"hash succeeds"* may be a **lookup or
   timing effect**, not proof of a stale id. Compare the configured-name/fixed-id send, an **explicitly captured
   current leased-id** send *if expressible*, and a hash send with the **binding already warm** — or an isolated
   scenario where both resolve before transmission and release at equivalent times. **Report the timing control.**
4. ★ **Every mobile scenario must state explicitly:** unprovisioned start · `mobile_autoregister` · which statics
   have `host_mobiles` · that **gateways cannot host** · GLOBAL/home-plane vs TEAM-plane · **confirmed attachment
   before the first hosted-mobile DM** · destinations **by hash** unless leased-id behaviour is under test ·
   **assertions on the runtime `(hash, leased local id, epoch)`, not the configured `node_id`.**

**Status: DISPATCHED 2026-08-11. ⛔⛔ INVESTIGATION ONLY — NO FIX, NO `lib/`, `src/`, `test/` OR `simulation/` EDIT.**
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; **an independent QA agent reviews
after you**; **the OWNER commits and rules.** ⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --`
anything; never check out another commit here** — the tree carries the whole uncommitted mobile-home arc.

**Deliverable: an ADJUDICATION between the four candidate causes below, each backed by measurement, plus a
recommendation. NOT a fix.** If the answer is a firmware defect, you **register it and stop**.

---
## 1 — The measurement that opened this (reproduce it first; do not take it on trust)

`tools/dm_delivery_breakdown.py simulation/s07_seattle_mobile_meshroute.json --run --mode dm`

| direction | pair rows | sent | arrived |
|---|---|---|---|
| static → mobile | **31** | 31 | **0** |
| mobile → static | **28** | 28 | **0** |

**59 DMs involving a mobile, 0 % delivery.** ⛔⛔ **WITHDRAWN SENTENCE (amendment 2), fenced in place rather than
deleted:** *"the delivery AUTHORITY counts them as sent-and-not-arrived, so they DEPRESS the corpus total the `≥732`
floor is measured against"*. ⇒ **They contribute ZERO ARRIVALS and are UP TO 59 potentially RECOVERABLE deliveries,
subject to deduplication and correct attribution — NOT a subtraction from an absolute count.**

⛔⛔ **WITHDRAWN PARAGRAPH (amendment 1), fenced in place:** *"The mechanism is NOT globally broken: the same tool on
`s27` measures 6 of 6 static→mobile DMs arriving (100 %) … that contrast is your sharpest lever."* ⇒ **That figure is
read off a COLLAPSED row and is NOT evidence.** ★ **Use `s21` and `s22` as the controls instead** (amendment 1).

⚠⚠ **A CLAIM IN THE RECORD IS WRONG, AND SO WAS ITS FIRST CORRECTION.** §MH-S5b / §B177-FIX say the metric is *"blind
to hosted-mobile service"*. ⛔ The QA-gate's rebuttal — *"THE METRIC IS NOT BLIND"*, flat — **overshot and is
withdrawn (amendment 2).** ★ **The accurate form: the metric is not SYNTACTICALLY blind to mobile pairs, but whether
those pairs represent the correct RUNTIME identities remains UNDER INVESTIGATION (candidate (c)).** The QA-gate made that error twice and relayed it to the owner. ⇒ **Treat every
"blind"/"no coverage" statement in the arc's notes as suspect and re-measure it.**

---
## 2 — The four candidate causes. ⛔ None is established. Do not guess; measure each.

**(d) is the owner's hypothesis and the newest information — start there.**

### ★★ (d) THE SCENARIO PREDATES THE ATTACHMENT DESIGN, so it may not set up `mobile register` correctly
**Established before dispatch:** `s07` was created **2026-07-07** and last touched **2026-07-21** (`git log`) — both
**before** the **2026-08-07** `mobile-home-attachment-reliability` design that this whole arc implements. And its
config is thin:
- each mobile sets only `is_mobile: true` (plus an unrelated `sync_response_requester_mobile_penalty_ms`);
  ⛔ **no `mobile_autoregister`**, no attachment fields;
- **no static node sets `host_mobiles`** at all — every static config is `{}`, so hosting is whatever the default is;
- ★★★ **the mobiles carry FIXED `node_id`s 50 / 51 / 52.**

⇒ **THE QUESTION THAT MATTERS MOST:** a real mobile starts **unprovisioned** and **LEASES** a local id from its home,
and the last-mile path addresses it by that **leased local id with `addr_len = 1`**. `s07` instead pre-assigns
global-looking ids, and `send <name>` resolves to **id 50/51/52**. **Are those two address spaces the same?** If a DM
addressed to global id 50 can never reach a mobile whose last-mile address is a leased local id, **the scenario is
addressing a mobile in a way the shipped design does not deliver to** — and the 0 % is a scenario defect, not a
firmware one. ⛔ **Establish this from the code path, not by inference:** follow `send <name>` → the addressed id →
`enqueue_data`/`addr_len` → the host last-mile fork (`node_mac_rx.cpp`) and say exactly where a global-id-addressed DM
to a hosted mobile is or is not deliverable.
ⓘ Note the tension you must resolve rather than wave at: **`s07` DOES register mobiles — 12 `mobile_registered`,
10 `mobile_attach_confirmed`, 3 `mobile_lastmile_fwd`** (measured). So attachment is happening. **Why do 3 last-mile
forwards occur while 31 static→mobile DMs arrive zero times?** That number is the discriminator — explain it.

### (b) `send <name>` resolves a STALE id at command time
`s07` has **0** `send_hash` commands; all 59 are `send <name>`. The tool's own note says *"a `send <name>` resolves to
the target's id at command time"*. A mobile's local id is **leased** and can be re-leased on re-home — and `s07` is the
**only** corpus scenario with a movement-driven home change (3 `mobile_redirect_recorded`).
★ **The decisive experiment:** express the same sends as **`send_hash <key_hash32>`** and see whether they arrive.
⛔ **Do it in a THROWAWAY copy under `/tmp`, NEVER by editing `simulation/`** — the corpus must not move.

### (a) A real functional defect on the mobile DM path under roaming
If `send_hash` also fails while `s27`'s equivalent succeeds, the difference is roaming/re-home. ⇒ Then this is a
**firmware finding** — ⛔ **register it and STOP.** ★ Bisect what differs: `s27`'s mobiles are stationary and
`node_id = 0`; `s07`'s move and are pre-assigned.

### (c) A pair-matching / attribution artefact in the tool
⛔ **`s27` collapses all five `node_id = 0` mobiles into ONE label** (`M5(0)`), so `M1 → M2` renders as the self-send
`M5(0) -> M5(0)` — 4 sends counted **sent/0-arrived** and flagged UNSENT — and the `ALIASED_IDS` warning is
**deliberately suppressed for id 0** (`tools/dm_delivery_breakdown.py:365`), so the collapse is **silent**. ⓘ That
comment's premise — *"each leases a real id at registration and never appears as origin 0"* — does not hold for the
**configured-send** side, which reads `node_id` from the scenario. ⇒ **Establish whether `s07` suffers an analogous
mis-attribution** (its ids are distinct, so probably not — but ⛔ *probably* is not a measurement).

---
## 3 — Method

- ★★ **Ask of every zero: could this have come out otherwise?** A 0 % arrival is a *signal*, not an absence of one —
  that conflation is exactly what produced the two wrong claims in §1.
- ★★ **Positively control every comparison.** If you show `send_hash` arrives, show in the same run that `send <name>`
  does not. If you claim a code path is unreachable, show the reachable sibling.
- ⚠ **"No live path does X" is a STRUCTURAL/call-graph question, never a text-grep question.**
- ⛔ You may **run** `lus` and the tool freely and create throwaway configs under `/tmp`. You may **read** anything.
  ⛔ You may **write** nothing in `lib/`, `src/`, `test/`, `simulation/` or `tools/`.
- ⛔ **PROVENANCE:** never claim an owner or QA approval; **never quote an owner ruling** — reported form only.

**Baseline:** HEAD `eb9d46c`; native **1512 / 81212 / 0**; `lus` **`316b9cb1`**; corpus 36 rows, delivery
**732 / `s06` 110**; canonical floor **≥732 / ≥104**, provisional pending [[B163]].
⚠ **Print the `lus` md5 beside every figure** — a stale `lus` reports the previous slice's streams and looks exactly
like "nothing moved"; that has already produced one false conclusion in this arc.

---
## 4 — Report

1. **The reproduction** of §1's table, with the `lus` md5.
2. **A verdict on each of (a)–(d)**, each `CONFIRMED` / `REFUTED` / `NOT ESTABLISHED`, with the measurement that
   decides it. ⛔ *"Probably"* is not a verdict.
3. ★ **The explanation of the 3 `mobile_lastmile_fwd` vs 31 zero-arrival DMs** — this is the discriminator.
4. **The `send_hash` experiment result** (throwaway config, path shown).
5. **What it means for [[B181]]'s floor impact:** how many of the 59 are inside the 732, and whether fixing the cause
   would move the corpus total. ⛔ **Do not change the floor** — that is the owner's.
6. **A recommendation**, explicitly separating *scenario repair* from *firmware fix* from *tool fix*, with the cheapest
   decisive next step named.
7. ★ **Whether [[B151]]'s new scenarios must address mobiles by hash** rather than by name, i.e. the constraint this
   investigation was opened to settle.
8. **Any other arc claim you found to be wrong** while measuring — the record has now been wrong twice in this area.
9. `git status --short` proving **nothing outside `/tmp` changed**.

**Stop and report rather than continuing if:** the cause is a firmware defect (⇒ register, do not fix) · the tool needs
changing to answer the question (⇒ say so, do not change it) · or any corpus row moves.
