<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B182-FIX — the delivery tool needs TWO identity layers, not one · dispatch brief · 2026-08-12

**Status: DISPATCHED 2026-08-12 on an owner ruling.** ★ Role split: the QA-gate wrote this brief and verifies your
claims; **an independent QA agent reviews after you**; **the OWNER commits and rules.**
⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out another commit here** —
the tree carries the whole uncommitted mobile-home arc.

⛔⛔ **SCOPE, EXACTLY: `tools/dm_delivery_breakdown.py` ONLY.** ⛔ **NOT the simulator engine. NOT firmware. NOT
`lib/`, `src/`, `test/` or `simulation/`.** This is a **measurement** defect; the protocol is fine.

**Baseline:** HEAD **`eb9d46c`**; native **1512 / 81212 / 0**; `lus` **`316b9cb1`**; corpus 36 rows.
⛔⛔ **THE `≥732` FLOOR IS FROZEN AND TEMPORARILY NON-AUTHORITATIVE PENDING THIS SLICE** (owner-ruled, ledger §1.18).
⇒ **You are not gated on a delivery number. Do not treat 732 as a target, and do not "preserve" it.**

---
## 0 — The defect, and the control that proves it is narrow

★★ **`s22` is decisive and is your positive control throughout:**

| pair | authority reads | truth |
|---|---|---|
| `TeamA(50) -> TeamC(52)` — **both mobiles**, team plane | **4 / 4 = 100 %**, `h1ack` 100 % | ✅ correctly attributed |
| `S2(30) -> M1(60)` — static → hosted mobile | **2 sent / 0 arrived**, `intended-but-missing: 2` | ⛔ arrives; tool reports zero |
| `M1(60) -> S2(30)` — hosted mobile → static | **2 sent / 0 arrived**, `intended-but-missing: 2` | ⛔ arrives; tool reports zero |

Also: `s21` **0/3**, `s07` **0/59** (28 static→mobile + 3 mobile→mobile + 28 mobile→static).
⇒ ⛔ **The tool is NOT blind to mobiles — it mis-attributes ONE addressing path.** Any fix that changes the team-plane
row has broken something.

### Why it fails (owner's analysis; verify it, then implement to it)
The tool treats the **configured `node_id`** and the **on-wire id** as one identity space. Hosted mobiles violate that
in **both** directions:
- **static → mobile:** configured destination is the mobile's scenario identity; on the wire the **first leg targets the
  home**; **final delivery targets a leased local id**;
- **mobile → static:** the logical sender is the mobile, but the **wire origin is its home id**;
- **team mobile → team mobile:** team ids remain directly attributable ⇒ that path already works.

⛔⛔ **AND WHY THE OBVIOUS FIX IS WRONG: no global id alias map can fix this safely, because a home id
SIMULTANEOUSLY identifies a real static node.** ⇒ Do not build an alias map.

---
## 1 — The required design: two separate identity layers

- **Logical endpoint** — the stable **scenario node slot / name / hash**.
- **Wire endpoint** — the runtime **id, plane, layer and time**.

**Implement exactly these seven, in this order:**
1. **Key configured intentions by unique scenario SLOT — not by `node_id`.** ★ This also fixes `s27`'s five
   `node_id: 0` mobiles collapsing into one label.
2. **On `tx_enqueue`, record the EMITTING SCENARIO NODE as the logical sender** — even when the wire origin is its home.
3. **On final `delivered`, record the EMITTING SCENARIO NODE as the logical recipient** — even when `data.dst` is a
   leased id.
4. **For the static→mobile last mile, correlate the delivery back to the original record through `(origin, ctr)`**
   when the destination changes from home id to leased id.
5. ★★ **REFUSE ambiguous `(origin, ctr)` matches rather than guessing.** ⛔ A guess here is how a delivery gets
   attributed to the wrong mobile, which is the defect wearing a new hat.
6. **Keep wire ids for route/hop diagnostics ONLY** — ⛔ never as logical pair identities.
7. **Add explicit counters for REFUSED / AMBIGUOUS logical correlations** — and ★ **print them in `--json` and in the
   table**, so a future reader sees the residue instead of inferring soundness from a clean total.

⚠ **Payload is a CONTROLLED CROSS-CHECK, never the primary identity** — messages can legitimately repeat the same text.

---
## 2 — Required tests (owner-specified; pin every one)

1. `s21`: static → hosted mobile **3/3**.
2. `s22`: static → hosted mobile **2/2**.
3. `s22`: hosted mobile → static **2/2**.
4. ★ `s22`: team mobile → team mobile **remains 4/4** — the regression guard.
5. `s27`: the **five configured id-zero mobiles remain DISTINCT logical nodes**.
6. ★★ **The same home id producing its OWN DM while simultaneously representing a hosted-mobile origin — concurrently,
   with NO cross-attribution.** This is the case an alias map cannot express and is the sharpest test here.
7. **Mutation controls: restoring id-keyed pairing must turn the hosted-mobile cases RED.**

⇒ **Extend `tools/test_dm_delivery_breakdown.py`** (it exists and had 110 checks). ⛔ **Every new assertion must be
mutation-proven; print match counts.** ⚠ Two controls in this arc were vacuous because of **what state the test
actually reached** — one queried right after a clock restamp, one fired no timer. **Establish the state, not the name.**

---
## 3 — After the fix: RE-RUN. ⛔ NEVER ARITHMETICALLY ADJUST.

Owner-ruled (ledger §1.18). Re-run and report, each with the `lus` md5 and the tool revision:
**current tree · pre-[[B177]] · beacon-removal-only · selected-arm-only · all [[B178]] trigger arms · the [[B162]] /
hybrid-RTS comparison ladder** if those figures are still decision inputs.

★★ **The reason it must be a re-run: the correction may affect arms DIFFERENTLY, because their attachment windows
differ.** ⇒ One delta applied to every arm would re-bake the error under a new name — the [[B162]] lesson exactly.
⛔ **Do not propose a new floor.** Report the numbers; the floor is the owner's.
⚠ **Re-running old arms needs old code. Use `git worktree add` in `/tmp` — NEVER check out in this tree.** If an arm
cannot be reconstructed that way, say so and report it as not-re-run rather than estimating it.

---
## 4 — Out of scope

⛔ [[B183]] (the roaming re-attach finding — **next**, but not here) · [[B184]] (hash + E2E-ACK not expressible in the
simulator **wrapper** — a third surface, do not bundle) · [[B151]]'s scenarios · `s07`'s repair · [[B178]]'s refined
trigger · S6 · [[B163]] · firmware of any kind. ⛔ **If you find a firmware or engine defect, register it and stop.**

---
## 5 — Method

- ★★ **Ask of every zero: could it have come out otherwise?** Reporting `0` for a delivery that happened is precisely
  this defect; do not replace it with a `0` of a different kind.
- ⚠ **This arc has 20+ instruments that could not fail.** Your new counters (item 7) are themselves instruments —
  positively control them.
- ⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval; **never quote an owner ruling** —
  reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation.**
- ⚠ **Print the tool revision AND the `lus` md5 beside every figure.** A stale `lus` reports the previous slice's
  streams and looks exactly like "nothing moved"; that has already produced one false conclusion here.
- ★ **Label every figure `RE-MEASURED` or `ARCHIVAL`.** Mixing them without labels is what [[B162]] exists to forbid,
  and §B181-INV did it again by reporting stream figures (`s21` 3/3) beside authority figures (`s21` 0/3).

**Report:** the two-layer design as built, with `file:line` for each of the seven items · all seven tests and their
mutation results · the refused/ambiguous counters and what they read on all 36 rows · the re-run table from §3 with
every figure labelled · what you could NOT re-run · exact final `git status --short` showing **only
`tools/` touched** · and that nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** the team-plane 4/4 row changes · a correlation needs a guess to
resolve · reconstructing an arm needs anything beyond a `/tmp` worktree · or the fix would require an engine or
firmware change.
