<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 1 — COMPLETION REPORT (durable evidence) · 2026-08-19

⛔⛔ **WHY THIS FILE EXISTS: the brief required controlled mutations and an exact before/after output comparison, and
those were produced in an AGENT SCRATCHPAD, which is VOLATILE.** This project has already lost a proven 33-assert
scenario that way. ⇒ the **results** are recorded here durably; ⚠ **the harness source itself was NOT retained and is
gone** — see §4.

★ **Provenance, stated so it is weighted correctly: every figure below is the CODER's measurement.** The QA-gate
independently re-verified only what §3 lists. ⛔ Nothing here is a QG approval.

## 1 — Byte-identity: method and result
**Method (this is the part that makes it evidence rather than assertion):**
- **Arm A** = `handle_join`'s body **verbatim from HEAD `164bc53`**; **arm B** = the same verb routed through
  `JoinService::apply_join`. Both over the **same fakes**.
- **24 inputs:** valid · fractional **869.4625** and **62.5** · layers **16 / 17 / 255** · save-failure · bad key ·
  4 × missing-key · layer **0 / 256 / 257 / −1** · freq/bw/sf out of range on **both** sides · **freq=nan** ·
  **bw=nan** · empty.
- **Compared:** console bytes **+** the `join_started` JSON **+ the whole `mrnv::Blob` in hex** **+** an ordered call log.

**Result: diff EMPTY on 24/24.**

★★ **And the comparison was proven NON-VACUOUS by three poisons, each of which made it fire:**
| poison | diff lines |
|---|---|
| nibble written into `layer0_id` | 6 |
| freq routed via integer kHz | 4 |
| a NaN-rejecting `bw` predicate | 4 |

## 2 — Mutation battery: **12/12 defect mutations RED · 3/3 positive controls GREEN**
| mutation | reinstates | RED |
|---|---|---|
| M1 live-before-save | ordering | 3 assertions |
| M2 no live call | apply lost | 3 |
| M3 live on save-failure | the [[B207]] defect class | 2 |
| M4 `layer0_id` collapsed to nibble | full-byte loss | 4 (rows 1,15,16,17,200,255) |
| M5 `leaf_id` widened to full byte | nibble loss | 4 |
| M6 four `JoinErr` arms collapsed | distinct diagnosis | 9 |
| + validate-after-save (35) · zero the name bytes (1) · NaN-rejecting bw (2) · integer-kHz freq (1) · rebuild-candidate-without-load (4) · retry-the-failed-save (1) | | RED |
**Positive controls GREEN:** `% 16` for the nibble · `r.bw_hz` read later · an explicit cast. ⇒ ⛔ **not "everything red"**.

## 3 — Independently re-verified by the QA-gate
`JoinRequest` carries **`double freq_mhz` + `double bw_khz`** (`src/firmware_join_service.h:73-78`) · native
**1733 / 84164 / 0** from the binary · `git diff -- lib/` **empty** · HEAD **`164bc53`**, nothing committed · the
`firmware_config_parse.h:95-99` NaN instruction quoted verbatim.
⛔ **NOT independently re-run:** the A/B harness, the 12 mutations, the four probes, the census, the four-step corpus
proof and the two board envs. Those remain the coder's figures.

## 4 — ⚠ What is LOST, and the standing lesson
**The A/B harness source is gone** (agent scratchpad). ⇒ **byte-identity is recorded but NOT RE-RUNNABLE.**
★ **If it must be re-runnable, it has to be rebuilt IN-REPO** — arm A cannot be reconstructed from memory once HEAD
moves past `164bc53`, so the cost of rebuilding rises with every commit.
⇒ **STANDING RULE, restated because it has now cost twice: a brief that requires measured evidence must require it
LANDED IN THE REPO, not merely produced.** Scratchpad output is dead the moment the agent exits.

## 5 — Transposition reported, not hidden
`mr_ui_on_config_saved()` moved from *between* save and live-apply to *after both* (the W17 idiom, since the
transaction now owns the live apply). Non-interference was **measured**: the hook only re-reads `/mrcfg` and compares
the four covered fields, and `provision_apply_live` writes none of them in NV or `NodeConfig`. Neither call prints.

## 6 — Findings this slice produced
- **[[B215]]** — `reset_join_for_reprovision()` leaves the listen/retry timers armed; a **left** node can still put a
  `J_CLAIM` on air up to 10 s later. **Measured on the real `TimerWheel`. Registered, deliberately NOT fixed** (a
  `lib/core` edit re-runs the corpus and would have destroyed this slice's byte-identity claim).
- **[[B216]]** — `freq=nan` / `bw=nan` are accepted and a join proceeds with `bw_hz = 0`. ⛔ Registered only because QG
  noticed the **source claimed a registration that did not exist** (`[[JOIN-BW-NAN]]`).
- **Plan v6** — v5's `uint32_t bw_hz` was corrected **by this slice's measurement**; it was the same defect the `freq`
  field already had, one field over, which neither QG nor the QA-gate had extended.
