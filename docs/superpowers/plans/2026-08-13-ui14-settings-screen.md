<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-14 — the SETTINGS screen, marker and save/discard/reboot states · dispatch brief · 2026-08-13

**Status: DISPATCHED 2026-08-13 on an owner ruling.** ★ Role split: the QA-gate wrote this brief and verifies your
claims at the code; **the OWNER runs QG and rules.**
⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out another commit here** —
the tree carries the uncommitted mobile-home arc, UI-7D slice B and UI-13, all QG-passed and awaiting the owner's commit.

**Normative spec: `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §3.6.2** (plus §3.6.1 for the state
model and §3.2's gesture table). **Read them in full.**

**Baseline:** HEAD **`47c0048`**; native **1581 / 81943 / 0**; `lus` **`43a7b6eb`**; `sizeof(Node)` **221880**.

---
## 0 — Scope, from the slice table (`spec:1311`)

**UI-14 = the SETTINGS screen, the draft marker, and the save/discard/reboot states (§3.6.2).** Gate: **native + board
probe + target.**
⛔ **OUT — these are later slices, and building "just a bit" of them is the failure mode:** **§3.6.3 provisioning
(create team / static join) is UI-15** · **§3.6.4 nearby-team scan is UI-16** · **UI-12's BLE transport does not exist.**

### ★★ Two scope seams you must DECIDE EXPLICITLY and report — do not silently pick
1. **The `PROVISION` row.** §3.6.2 lists it as a row of the first menu, but its behaviour (§3.6.3) is **UI-15's**.
   ⇒ **State which you built: an absent row, or a present-but-inert row.** ⛔ Do **not** implement §3.6.3's
   *"if a settings draft is unsaved, PROVISION first requires SAVE or DISCARD"* precondition — that is UI-15's, and
   implementing it here means you built part of UI-15.
2. **The `BLE mode` row is CONDITIONAL: §3.6.2 says the row is ABSENT when the UI-12 transport is not compiled** —
   and **UI-12 is `📝` — it does not exist.** ⚠ `ble_mode` is nonetheless a **covered field in UI-13's service**, so
   the field being present is **not** a reason to render the row. ⇒ **Render it only under the transport's compile
   condition, and TEST BOTH — the row absent in today's builds, and present when the condition is met.**

---
## 1 — The service already exists. Consume it; do not re-implement it.

`src/firmware_config_service.h` (UI-13, QG-passed) gives you `open` · `set` · `save` · `discard` · `reload` ·
`config_unsaved()` · `conflict()` · `reboot_required()` and the typed outcomes. ⛔ **Do not add a second state model,
do not loop through `handle_cfg_set`, and do not manufacture command strings** (§3.6.1 forbids it: partial success,
no atomic validation).

- ⛔⛔ **THE MARKER IS `config_unsaved`, NEVER `dirty`.** `UiState::dirty` already means *"a repaint is owed"*, and this
  is the slice where both are read in one file. **A renderer that shows `UiState::dirty` as the unsaved marker is the
  defect §3.6.1 named in advance.**
- ★ **`reboot_required` and `config_unsaved` are INDEPENDENT** — a save needing a reboot is **durably saved and no
  longer unsaved**. Render them as two facts, not one.
- ★★ **[[B192]] is OWNER-RULED (ledger §1.22): `RELOAD` performs the three-way merge** — unchanged fields adopt the
  current persisted values, edited fields stay in the draft, **`DISCARD` remains the explicit full reset.** ⛔ Do not
  re-open or "improve" that.
- ⚠ **The conflict path is `CFG! RELOAD`, SAVE refused**, and the only ways out are **RELOAD** or **DISCARD**.

---
## 2 — The screen, per §3.6.2

- **Rows:** BLE mode (conditional, see §0.2) · DM encryption default (`e2e_dm`) · first-contact key attachment
  (`intro_attach`) · mobile auto-registration (`mobile_autoregister`) · PROVISION (see §0.1) · **SAVE / DISCARD / BACK**.
- **Gestures — ★ `short` does DOUBLE DUTY and that is the trap:** *"`short` advances rows **or cycles a finite value
  while editing**"*; `double` **enters/accepts** the highlighted row. ⇒ **Two modes of `short`, so name the state.**
  ⓘ **UI-7D's detail modal solved the same shape** (short toggles the action, double activates) — **follow that
  precedent rather than inventing a second idiom** (U1/U3).
- ⛔⛔ **The LONG gesture ALWAYS leaves the editor and arms emergency.** ★ **Close at `long_arm`, NOT `long_fire`** —
  this is exactly UI-7D's correction, where copying compose's fire-time close was wrong. **And test the cancel path:
  `long_arm → long_cancel` must not restore the editor with a destructive row selected.**
- **`BACK` is safe and preserves an unsaved draft; `DISCARD` is a separate deliberate action.** ⛔ Silently discarding
  because attention timed out is **forbidden** (§3.6.1).
- **Cycle placement:** SETTINGS appends — **STATUS → TEAM → INBOX → SEND → SETTINGS**, or **STATUS → INBOX → SETTINGS**
  on a non-team build (§3.1).
- ⛔ **Excluded from this first menu** (§3.6.2, verbatim in substance): arbitrary text · frequency digits · transmit
  power · duty · OTA/GPS controls · volatile debug knobs · and privacy-unsafe `team_channel_crypt=off`.
  ⓘ Preset **text** stays a BLE/serial matter — **the OLED selects configured text, it does not type it.**
- ★ **The renderer reads only frame-frozen state**, never a live model buffer — the UI-7D contract.

---
## 3 — The device binding (B193), and what it does NOT settle

[[B193]] records the two obligations a device binding inherits, and **QG named UI-14 as where it lands**:
1. **the `§nv-ritual` load — `nv_load_stamped`, NOT a bare `mrnv::load`.** ⚠ The service **refuses to open** when the
   load fails, precisely so it cannot destroy the non-covered fields it must preserve.
2. **`handle_cfg_set`'s OFF→ON `mobile_register_current()` bridge** — the live-apply side must not lose it.

⛔⛔ **AND STATE PLAINLY WHAT REMAINS UNPROVEN.** Even with the binding, this slice runs **native + board probe +
target**. ⇒ **No NV wear, no reset-during-write, no power-cut qualification** unless you actually run them — and
⛔ **you must not claim them.** ★ Keep UI-13's formulation: **a green suite says the logic is right, never that the
storage is.** If you do not exercise real NVS/LittleFS, say so and leave `🧪 NV/power-cut` standing.

---
## 4 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   From **1581 / 81943 / 0**.
2. ★ **Both UI probes**: `tools/probe_firmware_ui/` and `tools/probe_board_ui/`. Report the control sets. ⓘ The
   feature probe is the right instrument for a renderer — **use it rather than asserting by inspection.**
3. **`warning_census.sh`** with its multiset. ⚠ **[[B169]]'s shape applies if you add any emit** — board envs strip
   `MR_TELEMETRY` and orphan variables consumed only inside `MR_EMIT`; invisible to native **and** the corpus.
4. **Corpus: expect NO movement.** Print the `lus` md5; **if a row moves, stop and report.**
5. ★★ **Answer D2 explicitly.** `sizeof(Node)` is **221880** and must not move — this belongs in `src/`.
   ⚠⚠ **RAM is the real constraint: `heltec_v3` is already at 65.85 % (215 764 / 327 680) after UI-7D's +512 B.**
   **Report the per-board RAM/flash diff, and if SETTINGS pushes it materially further, SAY SO rather than burying it.**
6. ⛔ **Zero free timer ids** (`kCap == 91`). Allocate none; the UI tick exists.
7. **Every new assertion mutation-proven, match counts printed.** ⓘ The runner is `tools/probe_ui_model_mutations.py`
   — ⚠ **read its header: SIX defects were needed to make it safe, and its six-row table states the rule — a guard
   belongs to the INVARIANT, so every path that can violate it calls the SAME primitive.** If you add a target, use
   `guarded_write` and the existing locks; do not write the source any other way.

---
## 5 — Method

- ★★ **Name the third state.** Five instances this session, the latest in UI-13 itself (`save()` ignoring the conflict
  latch: *bytes differ · bytes match, latch clear · bytes match, LATCH SET*). **This slice has at least two ternaries:
  `short`'s two modes plus "not editing", and unsaved/conflict/reboot-required.**
- ★★ **A fact is established by the act** — ⛔ never render "saved" before `save()` returns success, and never clear the
  marker on a refusal.
- ★★ **Instruments that cannot fail — 24 instances.** Ask of every new test: could it have come out otherwise?
- ★ **A correction placed anywhere but the instruction a reader follows — twelve-plus sites.** ⇒ **When you change a
  status, grep for ALL of its siblings**: the last four slices each needed roughly double the sites that were cited.
- ⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval; **never quote an owner ruling** —
  reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation.**
- ⛔ **Do not describe Phase A as complete** — [[B164]]/[[B189]] gate on-device registration/team onboarding and final
  acceptance; UI-7D is **🧪 metal**; UI-13 is **🧪 NV/power-cut**.

**Report:** the screen with `file:line` · **the two §0 scope decisions, stated explicitly** · how `config_unsaved`,
`conflict` and `reboot_required` render as distinct facts · `short`'s two modes and the state that separates them · the
`long_arm` close and its cancel test · the BLE-row conditional with **both** arms tested · the device binding and
⛔ **exactly what remains unproven** · native · both probes · census · corpus (no movement, with the `lus` md5) · the
**D2 answer and the per-board RAM/flash** · every mutation · exact final `git status --short` and that nothing was
committed. ⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** a required row needs a field the service does not cover (⇒ **report it;
promoting a live-only field is its own NV-schema slice**) · the PROVISION row cannot be built without §3.6.3 behaviour ·
a corpus row moves · `sizeof(Node)` moves · RAM on `heltec_v3` approaches a level you judge unsafe (⇒ **report the
number and stop, do not trim the feature silently**) · or §3.6.2 and §3.2's gesture table disagree (⇒ **report the
conflict, do not pick a side**).
