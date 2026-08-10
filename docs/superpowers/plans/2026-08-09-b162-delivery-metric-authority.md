<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B162 — the delivery metric is not reproducible · measurement-only slice · 2026-08-09

**Status: ✅ EXECUTED 2026-08-09, then ⛔ REOPENED AND RE-CLOSED THE SAME DAY (second pass) — the results are
`simulation/BASELINE.md` §B162, sections (1)-(9) for the first pass and **(10)-(16)** for the second, plus the [[B162]]
and [[B163]] register entries.**
★★★ **THE SECOND PASS DID NOT MOVE THE LADDER: all 36 rows of all 6 arms re-measured IDENTICAL, so the floor stands.
What it fixed was the AIRTIME half, which had SIX more defects — the largest being that the total was summed only over
frames the tool could CORRELATE to an emit, so 2557 of 23913 aired frames (10.7%) were charged ZERO.** The total is now
a PHY-direct sum with an `unattributed_airtime` bucket included in it, and §3 below (the "derive, do not re-pin"
requirement) is superseded by that inversion: **lengths are still read off the wire, but the PRICE is the PHY event's
own `airtime_ms`, with the formula demoted to a cross-check** — which is how the missing SX126x SF5/SF6 case was found.
⛔ **NEW RESIDUE: [[B163]] (`s07`'s refused runtime-id alias).** ★ The instrument now has durable tests:
`tools/test_dm_delivery_breakdown.py`, 47 checks, each with a control.
★★ **LADDER, ONE TOOL REVISION / ONE RUN SET: BASE 733 · DELETE 707 · S1 690 · S2 724 · S2c 719 · CURRENT 719**
(authority = `--mode dm --json` → `totals.unique_deliveries`; raw `delivered` events 765 · 750 · 734 · 752 · 743 · 743
kept as a labelled CROSS-CHECK). ★ **`≥732` → `≥733` overall, `≥104` in `s06` unchanged.**
⚠⚠ **AND `≥733` IS CONDITIONAL, NOT FINAL — [[B163]] IS OPEN (§B162 (12)).** `s07_seattle_mobile_meshroute` carries a
**correct** runtime-id alias refusal: two mobiles genuinely wear the same **leased** wire id at different times, and a
time-BLIND alias map cannot resolve it. ⇒ **`s07`'s figure may be SHORT ON EVERY ARM by an amount that is not
derivable** without a time-windowed alias map. ⛔ Quote the floor as `≥733` **with this caveat attached**; do not
present it as a settled absolute, and do not "resolve" it by picking a number.
ⓘ Re-confirmed by §B162 (17)-(20) (the parser third pass): all 216 ladder cells reproduce the table above exactly, so
the floor did not move — **but it did not become unconditional either.**
⛔⛔ **THE DRIFT WAS NOT AN OFFSET: total off by −1/+1, but 10 of 36 rows wrong in BOTH directions (−4…+9), and
`s27`'s error DIFFERS BETWEEN ARMS (+7/+9) — a constant correction would have been rejected by the total and required
by ten rows.** ⛔ **AND THE TOOL ITSELF WAS WRONG FOUR WAYS: my first corrected pass returned BASE = 659** (numeric
destinations, the `send_hash*`/`send_layerx` verbs, a `layer_id`-keyed hash index, and the whole TEAM plane were
silently dropped ⇒ **14 of 36 scenarios read exactly ZERO**). ⛔ **CLOSED WITH A NAMED RESIDUE on 2 of 36 rows**
(`s21_mobile_dm_milestone` homed-mobile indirection; `s27`'s id-0 mobiles). ⛔ Zero `lib/`/`src/` lines changed.
⛔ Native 1471/79553/0, `error:` 0. ⛔ Board builds / `warning_census.sh` DID NOT RUN. ⛔ UNCOMMITTED (D4).
⛔ NO OWNER OR QA APPROVAL IS CLAIMED.

**Original brief below, unchanged.** **Status was: OWNER-SPECIFIED, NOT STARTED.** ⏳ **Sequencing is ruled: finish the plane correction (S2d ✅ done) → QG
review → THIS SLICE → S4.** ⛔ Do not start it before QG clears S2d. **D4: the owner commits.**

⛔⛔ **MEASUREMENT AND TOOLING ONLY. NO FIRMWARE OR ROUTING CHANGES IN B162.** If a firmware defect surfaces, register
it and stop — do not fix it here.

---
## 1 — Why this exists

The arc's headline delivery number **does not reproduce**, and the discrepancy was found by an implementing agent, not
by the gate:

- a raw `delivered` count on S2's own binary gives **752**, where the notes carried **728**;
- **`tools/dm_delivery_breakdown.py` — the repo's canonical instrument — agrees with the re-measurement, not the
  notes** (e.g. `s19`: 8 arrived, table says 4);
- ⇒ **per-row figures on rows marked "identical" were CARRIED FORWARD rather than re-measured**, so the error
  compounded silently across five slices.

⚠ **Consequence: the `≥732` floor cannot be evaluated as stated**, and it has been quoted as a gate in every slice
brief since S1. ⛔ **Do NOT assume the drift is a constant offset** — do not "just add +24". Carried-forward rows and
re-measured rows may differ **per scenario**; a uniform correction would re-bake the error under a new name.

★ **Related but separate:** the tool's airtime output is also invalid — see §3. Every airtime figure this arc has
quoted from it describes a wire that no longer exists.

---
## 2 — Required work (owner-specified, 2026-08-09)

1. **Register B162** as the non-reproducible delivery-gate / tool defect. ⓘ Next free id was **B162** at the time of
   writing — **verify before use** (the register has already reused an id once, recorded as B155).
2. **Define unique deliveries using the canonical configured-send analysis** — `tools/dm_delivery_breakdown.py
   --mode dm --json` is the **authority**, aggregating **first arrival per configured logical send**.
3. **Keep raw `delivered` event counts only as a CROSS-CHECK**, never as the figure of record. ★ State both, and state
   which is authoritative, every time either is quoted.
4. **Recalculate BASE, DELETE, S1, S2 and the current state with ONE tool revision.** ⚠ One revision, one run set —
   a comparison across tool versions is not a comparison.
5. **Replace the invalid `≥732` threshold with the reproduced BASE result** (total **and** the BASE `s06` value).
   ⚠ **State it as CONDITIONAL: `≥733` holds subject to [[B163]] (open) — `s07`'s figure may be short on every arm by
   a non-derivable amount.** A gate whose known uncertainty is unstated will be read as exact.
6. **Preserve `s27 == 0` assertion failures as a SEPARATE correctness requirement** — ⛔ it is not folded into the
   delivery figure and is not tradeable against it.

## 3 — The tool's airtime constants: derive, do not re-pin

`tools/dm_delivery_breakdown.py:~100` **hard-codes `RTS=8` and `CTS=3`.**
⛔⛔ **DO NOT simply change them to another fixed pair — that only moves the error.** ★ **Derive the actual frame length
per event:**
- **unicast RTS: 10 B plaintext / 11 B crypted** (frame length is the domain discriminator — design §2);
- **CTS: 3 / 4 / 6 / 7 B depending on shape** (ordinary vs terminal, plaintext vs crypted);
- M/flood RTS are **9 B / 43 B** and unchanged — ⚠ **that way round** (the design doc had them swapped until
  2026-08-08; the codec is the authority: `need = flood ? 43 : (m_bcast ? 9 : 7 + id.width)`).
★★ **FAIL LOUDLY when a frame's shape or length cannot be determined** (C2) — ⛔ no silent default, no "assume
ordinary". A tool that guesses a length is exactly how this defect was born.

---
## 4 — Method obligations (this arc's hard-won rules)

- ★★★ **Before trusting any measurement, ask whether it COULD have come out otherwise.** **Print counts beside every
  comparison**, and **positively control** each instrumented run.
- ★ **A discriminator that returns zero must itself be controlled** — a substring filter once matched **zero rows in
  both arms** and was believed; a `head -40` truncation once read as a negative result.
- ★ **A telemetry counter is not a coverage measure — check what it counts before using it as one.**
  `cts_terminal_mismatch == 0` was read as "the branch is unreached" when it covers **one of two** branches and the
  reachable one emits nothing.
- ★ **"Byte-identical" is not "inert."** A telemetry arm found three real ledger divergences behind a byte-identical
  corpus, because the affected function emits nothing.
- ⚠ **Do not compare a fresh figure to any number in the notes** until this slice has re-established the baseline —
  that is the whole point of the slice.

## 5 — Gate for this slice

- `pio test -e native` + **RUN the binary** to identify the tree (the wrapper prints a false *"0 test cases"*).
  Current: **1471 / 79553 / 0**.
- ⛔ **No firmware builds, no `warning_census.sh`** — the standing owner instruction holds; `lus` is the instrument and
  the board sweep is owed **once**, after behaviour stabilises. ★ **Record which gates ran and which were skipped;
  never imply a skipped gate passed.**
- ⛔ **Do not edit the `^### 36/36 corpus` anchor table** — re-anchoring is the owner's single ruling.
- ⚠ Build arms **sequentially**; ⛔ never build while mutating source; ⛔⛔ **NEVER `git checkout --`** (an agent did
  that inside a mutation loop and destroyed uncommitted work, recovered only from a byte-exact snapshot).

## 6 — Out of scope

⛔ **S4** · any firmware or routing change · **B161** (typed hash answers lack a canonical origin — **B153 cannot be
called closed while it is open**) · **B158**'s remaining `exchange_airtime_ms()` site (measured **DO-NOT-ADOPT**:
the +4 sat inside a ≥15-delivery chaotic band on a row with N=1) · **B159** · **B160-COV/SIB** · routing **T1–T3** and
`s07`/`s16` congestion sensitivity · the parked mobile-home arc · the OLED UI.

ⓘ **Settled rulings that bear on the numbers, so they are not re-litigated here:** the **752 → 743** regression is
**accepted** (*"accidental congestion suppression is not valid protocol behaviour"*); the **150 s** completed-flight
cache TTL **stays** (the 30 s arm's higher delivery count is *"a scheduling result, not a stronger correctness
argument"*); **§2.3 is owner-confirmed** (ledger §1.10, verbatim).
