<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B162 — the delivery metric is not reproducible · measurement-only slice · 2026-08-09

**Status: OWNER-SPECIFIED, NOT STARTED.** ⏳ **Sequencing is ruled: finish the plane correction (S2d ✅ done) → QG
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
