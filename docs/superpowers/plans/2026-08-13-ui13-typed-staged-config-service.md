<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-13 — the typed staged-config service (HEADLESS) · dispatch brief · 2026-08-13

**Status: ✅ IMPLEMENTED 2026-08-13 · QG PASSED · UNCOMMITTED (D4 — the owner commits).** ⛔ **This brief is now a
HISTORICAL dispatch record; its instructions are spent. Do not re-execute it.** Evidence: `src/firmware_config_service.h`
+ `test/test_firmware_config_service.cpp`, `simulation/BASELINE.md` §UI-13, native **1581 / 81943 / 0**, 32/32
mutations RED. ⓘ **[[B192]] (RELOAD = the three-way merge) is OWNER-RULED — ledger §1.22.** ⚠ **Still owed and NOT
settled by this slice: the device binding and the NV / power-cut qualification ([[B193]])** — everything here is
proved against a counting/failing FAKE store. ★ Role split (unchanged for the next slice): the QA-gate writes briefs
and verifies claims at the code; **the OWNER runs QG and rules.**
⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out another commit here** —
the tree carries the uncommitted mobile-home arc **and** UI-7D slice B. ⛔ **CORRECTED: this line read *"UI-7D slice B
(awaiting the owner's QG)"* — that is stale. UI-7D slice B has PASSED QG; what remains for it is METAL (🧪), not a
review.**

**Normative spec: `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §3.6.1** — read it in full; it already
specifies this slice almost completely, and this brief does not restate all of it.

**Baseline:** HEAD **`47c0048`**; native **1552 / 81563 / 0**; `lus` **`43a7b6eb`**; `sizeof(Node)` **221880**.

---
## 0 — Scope: HEADLESS and narrow

**Build the typed configuration service and nothing else.** ⛔ **OUT: the SETTINGS renderer · team creation · join
profiles · BLE · presets.** **UI-14 consumes this service afterwards** — so the deliverable is an API plus its tests,
with **no panel, no screen, no cycle change.**

**The nine required behaviours (owner-specified; each maps onto §3.6.1):**
1. **persisted snapshot versus editable draft**;
2. **typed field validation**;
3. **dirty and external-change / conflict detection**;
4. **no-op save**;
5. **validate everything BEFORE writing**;
6. **ONE durable configuration write**;
7. **apply live fields ONLY after durable success**;
8. **retain the draft AND the old live state after a validation or NV failure**;
9. **explicit save / discard / conflict / reboot-required outcomes.**

---
## 1 — ⛔⛔ TWO NAMING AND SHAPE TRAPS, BOTH PRE-REGISTERED BY THE SPEC

**A · ⛔ DO NOT CALL IT `dirty`.** §3.6.1 says so outright: **`UiState::dirty` already means "a repaint is owed"**, and
a second meaning on that word would collide in the one place both are read. ⇒ **The flag is `config_unsaved`, and it is
true iff the draft differs from the recorded baseline.** ⚠ The owner's own shorthand for this slice was *"settings
dirty/save"* — **the spec's name wins.**

**B · ★★ THERE ARE THREE STATES, NOT TWO: PERSISTED, EFFECTIVE and DRAFT.** §3.6.1's heading says exactly that.
⇒ ⛔ **This slice is PRE-REGISTERED for the defect that just cost four rounds elsewhere: a BINARY TEST OVER A TERNARY
DOMAIN.** Four instances this session — `mobile_op_of_tag` (switch on an integer, five enum states) · a ledger with
before/after and no ordinal for the same-millisecond tie · `airtime_for` with observed/unobserved and no *conflicting*
· a backup recovery with same/differs and no *foreign*. ⇒ ★ **Every predicate you write here: name the third state
before you accept two branches.** A `draft == persisted` test that ignores *effective* is this defect.

---
## 2 — The behaviour, per §3.6.1 (and the existing seams to use)

- **Opening** snapshots **only the supported persisted fields** and records a **baseline fingerprint**.
- **Changing a row changes the RAM draft ONLY** — ⛔ no radio retune, no live mutation, no flash write.
- **SAVE** validates the **whole candidate**, writes it **once**, and **only then** applies live-capable fields.
  A save needing a reboot sets **`reboot_required`** — ★ **it is still durably saved and NO LONGER unsaved.**
- **DISCARD** reloads the persisted values and clears the marker. ⛔ **`BACK` and blanking PRESERVE the draft;
  silently discarding because attention timed out is FORBIDDEN.** ⓘ A power loss intentionally loses an unsaved RAM
  draft — that is by design, not a defect.
- **A no-op save performs ZERO NV writes.**
- **A failed write keeps the old effective/persisted state, keeps the draft AND the marker, and shows `SAVE FAILED`.**
- **Conflict:** serial/BLE keep their existing **immediate-write** behaviour. If either changes a covered field while a
  draft is open, the **baseline fingerprint no longer matches** ⇒ **show `CFG! RELOAD`, REFUSE SAVE, and require
  `RELOAD` or `DISCARD`.** ⛔⛔ **Last-writer-wins is explicitly forbidden — it would silently overwrite companion
  changes.**
- ⛔ **Runtime changes never set the marker** — routes, registration, battery, unread counts.
- ★★ **It must be a typed service SHARED by serial, BLE and OLED.** ⛔⛔ **The OLED must NOT loop through
  `handle_cfg_set` (`src/firmware_config.h:34`) or manufacture command strings** — that would apply/save fields one at
  a time, **expose partial success, and make atomic validation impossible.**
- ⛔ **Only fields already represented DURABLY may be covered.** Promoting a live-only field is **its own NV-schema
  slice** — if a field you want is live-only, **leave it out and say so.**

ⓘ **Existing seams, verified before dispatch:** the durable slot is **`/mrcfg`** (`src/device_nv.h:213`, `kSlotCfg`;
note `:225`'s accepted version RANGE and `:112`'s warning that `/mrcfg` is now **key material**). The shared radio
retune is **`apply_radio_live`** (`src/firmware_config.h:9`) — ★ **that is what "apply live fields" should reuse, not a
new retune path** (U1).

---
## 3 — Test obligations

Every one of the nine behaviours needs a test, and these are the ones that pass vacuously if written loosely:

- ★★ **"No-op save performs zero NV writes"** — **count the writes.** ⛔ Observing "save succeeded" proves nothing;
  a fake NV that counts calls is the instrument. **And a positive control: a real change must produce exactly ONE.**
- ★★ **"One durable write"** — assert **exactly one**, not "at least one". The defect being prevented is per-field
  writes with partial success.
- ★★ **"Apply live only after durable success"** — the test that matters is the **failure** path: **NV write fails ⇒
  the live state is UNCHANGED, the draft survives, the marker survives.** ⛔ A success-path-only test cannot see this.
- ★★ **Conflict detection** — an external write to a covered field **while a draft is open** must produce `CFG! RELOAD`
  and a **refused SAVE**. ★ **Plus the negative half: an external write to a NON-covered field, and a runtime change
  (route/registration/battery/unread), must NOT raise a conflict and must NOT set the marker.**
- **`BACK`/blanking preserve the draft** — assert the draft after both, since silent discard is forbidden.
- **`reboot_required`** — assert it is **saved and no longer unsaved**, i.e. the two flags are independent.
- **Validation** — the whole candidate is validated **before** any write; a single invalid field means **zero writes**.

★ **Every new assertion mutation-proven, match counts printed.** ⓘ There is now a reusable runner at
`tools/probe_ui_model_mutations.py` — ⚠ **read its header first: it took four rounds and four defects to become safe**
(shared backup · `atexit` cross-restore · stale mutant binary · foreign-edit clobber). **If you extend it to a new
file, key the backup by resolved path and keep the three-arm recovery.**

---
## 4 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   From **1552 / 81563 / 0**.
2. ★★ **Answer D2 explicitly.** ⚠ **If the service or its draft lands in `lib/core` and adds a `Node` member,
   `sizeof(Node)` MOVES and you owe the ten-env sweep** + `warning_census.sh` + `-Wreorder` + the `sizeof` asserts +
   per-board RAM/flash. `sizeof(Node)` is **221880** today. ⇒ ★ **Prefer `src/`**, where UI-7D's own +512 B landed
   without touching `Node` — and **say which you chose and why.**
3. **Run `warning_census.sh`** and report the multiset. ⚠ **[[B169]]'s shape applies if you add any emit** — board envs
   strip `MR_TELEMETRY` and orphan variables consumed only inside `MR_EMIT`; invisible to native and the corpus.
4. **Corpus: expect NO movement** — this is headless config, not protocol. Print the `lus` md5; **if a row moves, stop
   and report.**
5. ⛔ **Zero free timer ids** (`kCap == 91`). Allocate none.
6. **RAM matters:** a draft is a second copy of the covered fields. **Report the per-board RAM/flash diff**, and
   remember `heltec_v3` is already at **65.85 %** after UI-7D.

---
## 5 — Method

- ★★ **Name the third state** (§1B). Four instances this session.
- ★★ **A fact is established by the act, never inferred** — ⛔ **never clear `config_unsaved` before the durable write
  returns success**, and never apply a live field before it.
- ★★ **Instruments that cannot fail — 24 instances.** The write-counting fake is the whole test for two of the nine
  behaviours; **prove it can count wrong.**
- ★ **A correction placed anywhere but the instruction a reader follows** — twelve-plus sites, three in the last round
  alone. **If you supersede a spec line, fix the line AND grep for its siblings** (the last slice needed four sites,
  not the two that were cited).
- ⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval; **never quote an owner ruling** —
  reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation.**
- ⛔ **Do not describe Phase A as complete** — [[B164]]/[[B189]] gate on-device registration/team onboarding and final
  Phase-A acceptance, and UI-7D is still **🧪 metal**.

**Report:** the service's API with `file:line` · which of the nine behaviours each test pins, with its mutation · the
write-count instrument and its control · the conflict path and its negative half · the **D2 answer and where you put
the code** · native · `warning_census.sh` · corpus (no movement, with the `lus` md5) · per-board RAM/flash · exact
final `git status --short` and that nothing was committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** a required field turns out to be **live-only** (⇒ leave it out and
report; promoting it is its own NV-schema slice) · the durable write cannot be made atomic in one operation · a corpus
row moves · `sizeof(Node)` moves and you cannot run the ten-env sweep · or §3.6.1 and §3.6.2 disagree (⇒ **report the
conflict, do not pick a side**).
