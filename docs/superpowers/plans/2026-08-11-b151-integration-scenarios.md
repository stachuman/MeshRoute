<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B151-SCEN — the two missing §12.2 integration scenarios · dispatch brief · 2026-08-11

**Status: DISPATCHED 2026-08-11 on an owner ruling.** ★ Role split: the QA-gate wrote this brief and verifies your
claims at the code; an independent QA agent reviews; **the OWNER commits and rules.**
⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out another commit here.**

⚠⚠ **CITE THIS BUG BY ITS FULL NAME: *"B151 — missing §12.2 integration scenarios"*.** The id **`B151` is used
TWICE** in the register ([[B155]] records the ambiguity). The other B151 is the **already-CLOSED** §MH-S3 draw-site
documentation finding. ⛔ **A bare `B151` in a brief, a comment or a commit message is ambiguous between an open
scenario debt and a closed doc fix.**

**Baseline (measured):** HEAD **`eb9d46c`**; working tree carries §MH-S5b-ii + §B177-FIX. Native
**1512 / 81212 / 0**, `error:` 0. `lus` **`316b9cb1`**. Corpus **36 rows**, 0 assertion failures; delivery
**732 / `s06` 110** — the **canonical floor is `≥732` / `≥104`, provisional pending [[B163]]** (ledger §1.17).
`sizeof(Node)` **221880**.

---
## 0 — Why this exists, and the measurement that makes it worth doing

⛔⛔ **THIS IS SCENARIO AUTHORING ONLY. NO `lib/`, `src/` OR `test/` CHANGE.** If you find a firmware defect, **register
it and stop** — do not fix it here (C1).

★★ **The coverage gap, measured 2026-08-11 across all 15 mobile-bearing scenarios:** only **four** ever forward a DM to
a hosted mobile — `s27` (19 `mobile_lastmile_fwd` + 10 cross-layer delegations), `s22_mobile_team` (4), `s07` (3),
`s21_mobile_dm_milestone` (3) — **29 firings total, two-thirds of them in one scenario.** And **7 of the 15 never
register a mobile at all** (`s23`, `s25`, `s26`, `s30`, `s34`, `s35a`, `s38`: zero registrations, attachments and
adoptions).
★★★ **Worse, the delivery AUTHORITY cannot see the path in the very scenarios built for it: [[B162]] closed with a
NAMED RESIDUE on exactly two rows — `s21`'s homed-mobile indirection and `s27`'s id-0 mobiles.** ⇒ That is why the
owner ruled that positive hosted-mobile service rests on **native tests and the metal gate, not the delivery metric**
(ledger §1.17's caveat). **This slice is what changes that** — a focused scenario is better positive evidence than
extending noisy `s07`.

---
## 1 — Naming and placement (owner-required: follow the standard)

- **Directory: `simulation/`** — that is the 36-scenario corpus the gate runs. (⛔ **not**
  `~/lora-universal-simulator/scenarios/`, which is the simulator's own separate set.)
- **Filename: `sNN_short_name_meshroute.json`.** Highest existing is **`s38`** ⇒ use **`s39`** and **`s40`**.
  Suggested, adjust only if you have a better short name: **`s39_late_home_four_mobiles_meshroute.json`** and
  **`s40_mobile_autoregister_off_meshroute.json`**.
- **Internal shape, matching `s38_team_origin_learn_meshroute.json`** (read it first): top-level keys
  `_name`, `_desc`, `simulation`, `commands`, `expect`, `nodes`, `topology`. ⓘ **`_name` omits the `_meshroute`
  suffix** (s38's file is `..._meshroute.json`, its `_name` is `s38_team_origin_learn`) — **match that.**
- **`expect` is a LIST of assertions**, each with `type` (`script_emit_contains`, `event_count`, …), `node`,
  `emit_type`, `value`/`count`, and **`_c`: a NUMBERED comment saying what the assertion proves.** ★ Follow that: the
  `_c` numbering is how a reviewer checks a scenario against a spec clause.

### ⛔⛔ THE GATE CONSEQUENCE — flag it, do not resolve it yourself
Adding two scenarios takes the corpus from **36 to 38 rows**, and the `^### 36/36 corpus` anchor table has **36**.
⇒ **The two new rows must be ANCHORED, and the anchor table is the OWNER's single ruling.**
- ⛔ **Do NOT edit the anchor table.** Produce the two rows as a clearly-labelled **PROPOSAL** in your report and in a
  `§B151-SCEN` block in `BASELINE.md`, exactly as [[B170]] did (measure → attribute → report → owner rules → land).
- ⚠ **Also flag that the gate's own NAME changes** — *"36/36 corpus"* becomes *"38/38"* — in `BASELINE.md`'s heading
  and in the many places briefs say "36 rows". ⛔ **Do not mass-edit those**; list them and let the owner rule. A
  mass-rename would corrupt historical slice records the same way a blanket floor rewrite would have.
- ★ **The 36 existing rows must stay byte-identical.** New scenario files cannot move them — but **prove it**, don't
  assume it: run the full corpus and report all 36 md5s.

---
## 2 — Scenario A: the late-home auto-ON scenario (owner-specified; every bullet is a requirement)

1. **Four mobiles start before any valid home exists, with auto-registration ENABLED.**
2. **Each makes at least two UNSUCCESSFUL discovery attempts.**
3. **An ordinary static host starts later.**
4. ★★ **Its startup beacon causes PROMPT discovery — and you must set the timing so the next normal backoff would fall
   OUTSIDE the allowed window, otherwise beacon wakeup is NOT PROVEN.** ⛔ This is the assertion most likely to be
   vacuous: if the ordinary retry would have fired inside the window anyway, the scenario proves nothing about the
   beacon. **State the two timings explicitly in `_c` and show the arithmetic.**
5. **Four distinct OFFER/CLAIM transactions coexist with NO slot overwrite.**
6. **All four reach CONFIRMED attachment — not merely provisional adoption.** ⓘ `mobile_attach_confirmed`, not
   `mobile_adopted`; the two differ and the distinction is the point.
7. **The host records four unique `(hash, local_id, epoch)` registrations.**
8. **A gateway and a `host_mobiles=0` static node remain INELIGIBLE** — neither hosts anything.
9. ★★★ **At least one static→mobile DM reaches EACH confirmed mobile, plus preferably one mobile→static reply.**
   **This is the positive hosted-mobile delivery coverage that `s07` cannot supply, and it is the main reason this
   scenario exists.**
10. ⛔ **NO manual command occurs** — this is the auto-ON scenario.

## 3 — Scenario B: the auto-OFF scenario (owner-specified)

1. **No mobile emits DISCOVER before an explicit `mobile register`.**
2. **Only the commanded mobile seeks and attaches.**
3. **Once attached, ordinary presence checking AND loss recovery still work despite auto-OFF.**
4. **`mobile unregister` returns it to persistent dormancy with NO wire traffic.**

---
## 4 — Every assertion must be able to fail

★★ **A scenario is an instrument, and this arc has 20+ recorded instruments that could not fail** — including two
found in the last two slices (a control that queried right after a clock restamp so the row was never old enough, and
a gate that fired no timer at all). ⇒ **For each `expect` entry, establish what state the run actually reaches, not
what the assertion is named.** Specifically:
- **every `count: 0` assertion needs a positive control** proving the counted thing happens elsewhere in the same run,
  or it is indistinguishable from a scenario where the mechanism never ran at all;
- **the ineligibility assertions (§2.8) are `count: 0` assertions** — pair each with the eligible host's non-zero
  count in the same run;
- ★ **prove the four-mobile concurrency is real**: four *distinct* local ids and four *distinct* hashes, asserted as
  values, not as a count of four events;
- **the "two unsuccessful attempts" requirement is a lower bound** — assert `>= 2` shaped evidence, and say what
  bounds it above.

⇒ **Mutation-style validation for a scenario: perturb the scenario itself** (e.g. start the host early enough that
ordinary backoff covers the window; give the gateway `host_mobiles=1`; drop one mobile) and **show the intended
assertion goes RED.** Report each perturbation and which assertion caught it. ⛔ **A scenario committed without that
is an unvalidated instrument.**

---
## 5 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** — must be **unchanged at 1512 / 81212 / 0**
   (⛔ you are not touching `test/`; if it moves, something is wrong and you stop).
2. **`lus` must be UNCHANGED at `316b9cb1`** — ⛔ **you are not touching `lib/`**, so a changed `lus` means you did.
   ⚠ Print it beside every corpus figure anyway: a stale `lus` reports the previous arm's streams and looks exactly
   like "nothing moved", which has already produced one false conclusion in this arc.
3. **Run the full corpus: all 36 existing rows + your 2.** Report every md5. **The 36 must be byte-identical**;
   the 2 new ones are the anchor proposal.
4. **Both new scenarios must run with 0 assertion failures** — and ⛔ **report their event counts and runtimes**; a
   scenario that takes minutes is a gate tax on every future slice, so keep them focused.
5. **Delivery on the authority** (`--mode dm --json` → `totals.unique_deliveries`): the existing total must stay
   **732 / `s06` 110**; ★ **your new rows ADD to the corpus total — report the new total and the two rows separately,
   and state plainly that the floor's `≥732` was defined on the 36-row corpus.** ⛔ Do not silently redefine the floor;
   that is an owner ruling.
6. ★★ **Report what §2.9 actually measured** — how many static→mobile DMs were delivered to each of the four, by
   which mechanism, and whether the delivery **authority** resolves them (given [[B162]]'s named residue on `s21`/`s27`,
   ⛔ **do not assume it does — check, and if it does not, that is a finding worth its own register entry**).
7. **D2: not applicable** (no code change) — say so explicitly rather than leaving it blank.

---
## 6 — Out of scope

⛔ [[B178]]'s trigger 1 and the **refined option (iii)** (step 4 of the owner's sequence — this slice is the positive
evidence that should precede it) · **S6** · §5.1's static-beacon wakeup *implementation* (⚠ scenario A **tests** beacon
wakeup as already implemented; it does not add it) · [[B154]](a) · [[B152]]/[[B150]]/[[B144]] · [[B158]]'s jitter arc
(⛔ `retry_jitter_ms()`, `airtime_routing_ms(8)`, R3.x golden jitter assertions) · [[B159]]/[[B161]]/[[B163]]/[[B164]]/
[[B165]]/[[B166]] · routing T1–T3 · the OLED UI · **any `lib/`, `src/` or `test/` edit.**

---
## 7 — Method obligations

- ⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval that was not given; **never quote
  an owner ruling** — reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation.**
- ★ **A fact is established by the recorded state, never an inference.**
- ★ **A correction placed anywhere but the instruction a reader follows — ten-plus sites.**
- ⚠ `BASELINE.md`, the specs and the plans carry **explicitly HISTORICAL blocks whose present-tense sentences are FALSE
  BY DESIGN** — read the label before acting on any sentence.

**Report:** both scenario files with their paths · a clause-by-clause table mapping every §2 and §3 requirement to the
`expect` entries that prove it · the perturbation results · the corpus table **with the `lus` md5** showing 36
byte-identical + 2 new · the anchor-row **proposal** (⛔ not landed) · the "36/36 → 38/38" rename sites listed, not
edited · what §2.9 measured and whether the delivery authority resolves it · exact final `git status --short` and that
nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** any of the 36 existing rows moves · `lus` or the native counts change ·
a requirement cannot be expressed without a firmware change (⇒ **register it, do not fix it**) · beacon-wakeup timing
cannot be made to exclude the ordinary backoff · or the delivery authority cannot resolve the new mobile deliveries.
