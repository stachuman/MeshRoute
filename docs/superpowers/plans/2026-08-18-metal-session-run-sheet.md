<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Metal session run sheet — v4, 2026-08-20

**Status: v4 — the §UI-15 qualification (Parts 28-30 + the slice-7 `/mrjoin` power-cut) added as §12-§17; v3's
front page and procedures retained below unchanged.** ⓘ The plan-§10 pre-gate for destructive OLED provisioning —
`/mrcfg` Part 20.5 — is **DISCHARGED** (§9 below, confirmed OK), so the UI-15 metal arc may run.
⚠ v3's hardware line is POINT-IN-TIME (2026-08-18) — ★ re-confirm the inventory from the banners at §0, never from
this sheet.
★★ **FOR THE SESSION ITSELF USE `2026-08-20-metal-session-walkthrough.md` — a single linear document with every
command and expected line INLINED** (owner-requested 2026-08-20: no cross-document jumping). This sheet stays as
the sequencing rationale; the bench script stays the authority over both.
⚠ **`docs/2026-07-31-bench-test-script.md` REMAINS THE AUTHORITY (M2).** This sheet adds *sequencing, preconditions and
stop rules* only. ⛔ Where the two disagree, **the bench script wins and this sheet is wrong.**

**HARDWARE — CORRECTED 2026-08-18 FROM THE BANNER, NOT ASSUMED: `heltec_mobile` ×2 (ESP32-S3, OLED) +
`xiao_esp32s3` ×1 — ⛔ the third node is ESP32-S3, NOT the nRF52 `xiao_mobile` this sheet first assumed.**
★ Three nodes still **resolve v1's gap**: Part 22.1's relay-only negative check (a third node must emit **no** AIRED)
is runnable.
One hardware consequence remains: **§7 (27.1, the stack check) cannot run on these three nodes.** `stackhw` is
nRF52-only (`#if NRF52…`; `loop_stack_free_bytes()` returns 0 elsewhere). [[B213]] is fixed in the current source by
adding `git_rev.py` to the `xiao_esp32s3` base environment, so ★ **the newly built XIAO must name its revision**;
`nogit` is now a failure, not an exception.


⛔ **v1 claimed Chrome was "state-neutral". IT IS NOT** — 24.3/25.6 persist `team 0` and reboot, 25.4 saves config,
25.2 sends a DM, 25.3 sends emergency posts, 25.7 needs a different firmware env. **Running 24-25 linearly would leave
the team before checks that need teammates.** The order below is rebuilt around that.

## Session record

- Firmware revision / dirty state: `________________`
- Flashed artefacts archive: `________________`
- Date / tester: `________________`
- Heltec #1 / #2: `________________` / `________________`
- Third node and exact environment: `________________`

## Checklist — mark only this list during the session

Already completed on the named older images:

- [x] Part 26 / B196 soak on `cb76d79`: 36,934 attempts, no panic.
- [x] Part 27 on `fc89e14`: 27.1, 27.3-27.5, 27.8-27.9.
- [-] 27.7: no safe real-NV failure injection available.
- [-] 27.10: state unreachable by construction.

Current-image run:

- [x] §0 — archive and flash the exact images.
- [x] §1 — record baseline from all nodes.
- [ ] §2 — OLED chrome while the team is connected.
- [x] §3 — one no-console sleep boot for 24.3 and 25.6.
- [x] §4 — restore the team and verify two-way connectivity.
- [x] §5a — persist the `sf_list=6,7` precondition.
- [x] §5b / 27.11 — B209: team retune does not start home seeking.
- [x] §5c-5d / 27.12-27.14 — B210/B211: preserve the record and truthful DAD output.
- [x] §5e / 27.15 — B212: the specific refusal wins.
- [x] §5f / 27.16 — B214: `cfg mobile-reg:` reports the real FSM state.
- [ ] §6 / Part 22 — B164 physical TxDone → `AIRED` → UI.
- [x] §7 / 27.1 — re-run on an nRF52 node, or mark not-run for this image with the hardware reason.
- [x] §8 / 27.6 — static-node `team new`, or mark not-run with the hardware reason.
- [x] §9 / Part 20.5 — B193 power-cut test; run last.
- [x] §10 / 25.7 — separate `gateway_heltec` image, or mark not-run with the build reason.

---
## §0 — Archive and flash (owner)
1. ⛔ **Commit the intended slice, then verify the SCOPED firmware worktree is clean:**
   `git status --short -- src/ lib/ variants/ platformio.ini tools/` ⇒ empty. ⓘ v1 said "commit the seven files" — a
   count that was already stale. **Scope, not count.**
2. Build `heltec_mobile` (both Heltecs) and `xiao_esp32s3` (the actual third node). Build an nRF52 `xiao_mobile`
   only if you choose to run §7.
3. ★ **Archive the ACTUAL FLASHED ARTEFACT PER ABI** — ⛔ not just `.bin`:
   - **ESP32-S3:** `firmware.elf` + `firmware.map` + the **`.bin` image(s) actually written**;
   - **nRF52, only if §7 is run:** `firmware.elf` + `firmware.map` + the **`.hex` / `.uf2` / `.zip` actually flashed**.
   Plus `COMMIT.txt`, **`DIRT.txt`**, `SHA256SUMS`, in a per-ABI `~/MeshRoute-artifacts/soak-<stamp>/`.
4. ⛔ **If any banner rev is `nogit` or is not the commit you just made, STOP.** This is the metal confirmation for
   [[B213]] on the XIAO family and the provenance guard for every later result.


## §1 — Baseline - confirmed OK
`version` · `cfg` · `status` · `routes` on all three. ★ **`debug on`** — several checks below read `»tx BCN`,
`J DISCOVER` and DAD-burst traces, which are otherwise invisible. ⛔ **Turn it off (or accept the noise) before the
one no-console sleep boot in §3.**

---
## §2 — CHROME, the TEAM-CONNECTED half (Heltec #1, teamed with Heltec #2)
Run **24.1, 24.2, 25.1, 25.2, 25.3, 25.4, 25.5** — everything that needs teammates, a DM peer, or emergency delivery.
- ★★ **25.3 (emergency full-width) is SAFETY-RELEVANT.**
- ⛔ **25.4: test PLAIN, UNSAVED and CONFLICT only.** The **`RESTART NEEDED`** arm needs `ble_mode`, and the BLE row is
  compiled out (`MR_UI_BLE_ROW` defaults 0, no env sets it) ⇒ **record it not-run with that reason, never as a FAIL.**

## §3 — CHROME, the sleep half — ONE no-console boot discharges BOTH 24.3 and 25.6
`team 0` → **reboot** → ⛔ **send NO console input at all** → leave it alone → then **one** `status` at the very end.
★ Reading `slept=` and `sleep=` from that single read satisfies **24.3 and 25.6 together**. ⛔ Any earlier byte latches
`g_host_present`, `sleep=off-host`, and the guard measures nothing.

## §4 — Re-provision before anything below
Restore the two-Heltec team (exported key), confirm two-way `rx BCN` and a team-route each way. ⛔ §5-§7 all need it.

---
## §5 — current team/mobile package (Heltec #1) — 27.11 → 27.16
### 5a — precondition - confirmed OK
`cfg set sf_list 6,7` → **reboot** → `cfg` ⇒ ★ **`sf_list=6,7`**. ⛔ If it does not persist, **STOP** — 27.12/13 would
be meaningless (the node's list was collapsed to `7` by the pre-fix build, [[B211]]).

### 5b — **27.11** ([[B209]]) - confirmed OK with remark - mobile register require all params, not only freq (it is OK)
`cfg set mobile_autoregister 0` → reboot → `mobile status` ⇒ `dormant`/`home_desired:false`. Then
`team new freq=869.4625 sf=7 bw=125` ⇒ ★ **still dormant, NO J DISCOVER for a minute**.
★ **Positive control (not optional):** `mobile register freq=869.4625` ⇒ `seeking` + DISCOVERs resume.

### 5c — **27.13 + 27.14 COMBINED** ⛔ this pairing is REQUIRED, not a convenience - confirmed OK
v1's *"same-team re-apply"* was **vacuous**: if it returns `no change`, **both old and fixed firmware omit the
`team-DAD` line**, so it proves nothing. ⇒ use an **unquestionably APPLIED** same-team transaction:
1. `mobile register freq=868.5 sf=9 bw=250` ⇒ `cfg` shows live collapsed to `sf_list=9` (★ unchanged behaviour).
2. `team <current id> freq=869.4625 sf=7 bw=125` ⇒ it **APPLIES**:
   - ★ **`> team PHY: … sf_list=6,7`** — ⛔ `sf_list=9` means it read LIVE and laundered an un-persisted collapse;
   - ★ **`> team -> team_id=…` present**, and ⛔ **NO `team-DAD:` line**, and ⛔ **no `»tx BCN` burst** ([[B210]]);
   - ★ `team_local_id` unchanged.
3. ★★ **POWER-CYCLE, then `cfg`** ⇒ still `sf_list=6,7`. ⛔ v1 omitted this and therefore never proved the corrected
   value reached NV.
4. **Positive control:** a genuine membership change (`team new`) ⇒ the `team-DAD:` line **must** appear, the
   `»tx BCN from=<N> n=0` burst **must** follow, and `<N>` must equal `cfg`'s `team_local_id` — ★ **never `0`**.

### 5d — **27.12**
Covered by 5c steps 2-3 (the preserve + power-cycle path). Record it against 27.12 as well.

### 5e — **27.15** ([[B212]]) - confirmed OK
`team 0 freq=868` · `team 0 sf=7` · `team 0 bw=125` ⇒ each gives the **two-line leave refusal**, ⛔ never
`> team new err: …`. Then `team 0 freq=868 wibble=3` ⇒ still `> team err bad/unknown key: wibble`.
★★ **MANDATORY positive control v1 DROPPED:** `team new freq=99999 sf=7 bw=125` ⇒ the **neutral `> team err:` range
error**. ⛔ **This is what proves the classifier did not globally bypass range validation.**
Also: a bare `team 0` leaves cleanly; `team <id> freq=… sf=… bw=…` still parses and applies.
⇒ when these hold, **27.2's diagnostics half is PASS**.

### 5f — **27.16** ([[B214]]) - confirmed OK
Reuse the transitions already created in 5b:
1. While `mobile status` is `dormant`, `cfg` must say `mobile-reg: UNREGISTERED (dormant)`, never `scanning`.
2. After explicit registration starts and status is `seeking`, `cfg` must say `UNREGISTERED (seeking)`.
3. Let a static host offer service. While status is `claiming`, `cfg` must say `UNREGISTERED (claiming)` —
   ⛔ never `REGISTERED home=` merely because the provisional home id is already non-zero.
4. After confirmation, status is `attached` and `cfg` must say `REGISTERED home=<same non-zero id>`.
If the claiming interval is missed, repeat it; an attached-state reading does not exercise the defect arm.

### 5g — restore the two-Heltec team again.

---
## §6 — [[B164]] Part 22 (all three nodes)
★ **Now fully runnable:** 22.1's negative check needs a **third, relay-only node** to emit **no** AIRED — that is the
XIAO (or Heltec #2). ⛔ v1 declared only two nodes and could not have run it.

## §7 — 27.1 stack — ⛔⛔ NOT RUNNABLE ON THIS HARDWARE
`stackhw` is **nRF52-only** and **no node in this session is nRF52** (2 × ESP32-S3 Heltec + 1 × ESP32-S3 XIAO).
⇒ **Choose ONE and record it:**
- **(a) bring an nRF52 node** (`xiao_sx1262` / `xiao_mobile`) and run 27.1 on it; **or**
- **(b) record 27.1 NOT-RUN for this build, with the reason.**
⚠ **It is not already discharged.** 27.1 passed (6016 → 4512 → 4408) on an **earlier** build; **[[B209]]-[[B212]] have
since changed this exact call path** — a new classifier frame in `handle_team` and B212's `static char phy_scan[96]`
⇒ **the figure may have moved and the previous pass does not carry over.**
⛔ The Heltecs do **not** print `stackhw`; its absence there is **not** a failure.

## §8 — 27.6, then decide - confirmed OK
Needs a **static** node; all three are mobile and `team 0` does not demote (R3). ⇒ **factory-erase one and run it, or
record not-run with the reason — before §9.** ⛔ It is the last item blocking [[B207]] closure.

## §9 — ⚠⚠ [[B193]] Part 20.5 — GENUINELY LAST, THE ONLY REMAINING DEVICE-MUTATING CHECK
⛔ v1 put §7 (27.6) *after* this and contradicted itself.
**Method, stated properly:** edit a value **through the SETTINGS screen**, position on **SAVE**, **double-press**, and
**cut power at varying delays** across ~5 attempts — ⛔ not merely "during a `/mrcfg` write".
**PASS = every boot yields the complete OLD or the complete NEW record, never a half-written one.**

## §10 — Not in this session
**Part 25.7** needs a **`gateway_heltec`** build, which §0 does not produce. ⇒ **run it separately, or record it
not-run with that reason.** ⛔ Do not fake it on a mobile image.

---
## §11 — Recording
Per check: verdict · the console lines verbatim · what was skipped and why.
⛔ **Report failures with their output; do not tune around one** (D3) — several of this arc's findings came from
exactly that discipline. ⛔ **Mark N/A items (25.4 restart arm, 25.7, possibly 27.6) as not-run WITH THE REASON**,
never as passes and never as failures.

---
# v4 — the §UI-15 QUALIFICATION RUN (slice 7) · added 2026-08-20

⚠ **`docs/2026-07-31-bench-test-script.md` Parts 28, 29, 30 REMAIN THE AUTHORITY (M2)** — this block adds
sequencing, preconditions and stop rules only. Run on the **slice-6 commit** with a fresh §0 (archive + banner
check on every node; `nogit` is a failure).

## Checklist — UI-15 run

- [ ] §12 — 28.4 fresh-chip line, ⛔ FIRST, on a full-erased node, before ANY other console input that saves.
- [ ] §13 — Part 28 remainder (28.1-28.3, 28.5; 28.6 stated-not-reachable; the gateway line not-run unless §16).
- [ ] §14 — leftover v3 items that need the TEAM: §2 chrome (24.1-25.5 subset) and §6 / Part 22 — BEFORE 29
      mutates team state.
- [ ] §15 — Part 29 (OLED team create), incl. 29.5 PHY divergence; restore/verify team state after.
- [ ] §16 — Part 30 (OLED static join) — needs a STATIC layer-17 peer (factory-erase one node; `team 0` does NOT
      demote, the §8 lesson).
- [ ] §17 — ⚠⚠ the slice-7 `/mrjoin` POWER-CUT — GENUINELY LAST (the only device-mutating check in this run).
- [ ] gateway-image arms (29.1 hidden PROVISION row; 28's `gateway_build` refusal) — a separate `gateway_heltec`
      flash, or not-run WITH THE REASON (the §10 rule; ⛔ never faked on a mobile image).

## §12 — 28.4 FIRST, and the window is ONE-SHOT
★★ **The fresh-chip line needs an `mr` NVS namespace that has NEVER been written, and the FIRST save of ANY record
closes the window forever** — including `cfg set`, `regen`, a team join, an inbox write.
⇒ on the chosen ESP32-S3 node, **full-erase before flashing** (`esptool.py erase_flash`, or PlatformIO's erase
target), flash §0's image, boot, and **the very first command is `joinprofile list`** ⇒ **`NO PROFILES`**, ⛔ never
the storage-failure line.
⚠ ⛔ **`factory_reset confirm` does NOT reopen the window** — it `clear()`s the keys but the namespace persists, so
`nvs_open(NVS_READONLY)` no longer answers `ESP_ERR_NVS_NOT_FOUND`. Only the full flash erase does. If you cannot
establish the namespace state, record the check AMBIGUOUS with the reason — ⛔ not PASS.

## §13 — Part 28 remainder (one Heltec, ordinary provisioning allowed from here on)
28.1 → 28.2 (★ the `869.4625` four-decimal Hz pin) → 28.3 (coalescing) → the 28.x power-cycle → 28.5 (strict
index) → `factory_reset` line. 28.6 is stated-not-reachable (record as such). The `gateway_build` refusal line
rides §16's gateway flash or is not-run.

## §14 — the v3 leftovers that need TEAMMATES — before Part 29 touches team state
Restore the two-Heltec team (§4 procedure), then run **§2's chrome subset** (24.1, 24.2, 25.1-25.5 as scoped in
v3) and **§6 / Part 22** (B164 TxDone→AIRED, the third node as the no-AIRED negative). ⓘ Sequenced HERE because
Part 29 creates/replaces teams — running chrome after it would re-provision twice.

## §15 — Part 29 (OLED team create)
Run 29.2-29.4, 29.6, 29.7 on a leaf Heltec per the script. Notes the script cannot carry:
- **29.5 (PHY divergence) leaves the radio LIVE-RETUNED off the persisted PHY** (`mobile register` persists
  nothing) — ★ after the refusal is recorded, **reboot** to restore live==persisted before anything else reads
  the radio.
- 29.3/29.4 replace team membership — **§14 must already be complete**; re-export the key if the old team is
  still wanted afterwards.
- 29.1 (gateway hidden row) belongs to the gateway-image arm (checklist last item).

## §16 — Part 30 (OLED static join)
Follow 30.0-30.7 verbatim — the script's own sequencing is already correct (sparse profile list, layer-17 peer,
blank/wake, the declared no-hijack behaviour, stop rules).
★ The static peer: **factory-erase one node first** (`team 0` does not demote — the §8 lesson), then
`create layer=17 …` per 30.0. ⓘ 30.6's conditional arms stay conditional — ⛔ do not manufacture collisions or
damage storage for a string; the host probes are the mandatory control for those.

## §17 — ⚠⚠ the slice-7 `/mrjoin` POWER-CUT — LAST (plan §10's second gate, the UI-15 closure item)
The B193 method, over `/mrjoin` (the §9 procedure was `/mrcfg`'s and does NOT carry over):
1. Seed a known state: `joinprofile reset confirm`, then `joinprofile set 1 …` with values you record.
2. Issue a DIFFERENT `joinprofile set 1 …` and **cut power at varying delays across ~5 attempts** (vary from
   immediately-after-enter to ~1 s).
3. **Every boot: `joinprofile list` yields the COMPLETE OLD or the COMPLETE NEW slot — ⛔ a `PROFILE STORE
   INVALID` after a cut IS the torn-record FAIL** this check exists to catch (honest detection is not a pass).
4. ⚠ ABI note: this run's nodes are ESP32-S3 (NVS, log-structured — the expected-robust arm). **The nRF52
   LittleFS arm (remove-then-write, the riskier path) is NOT-RUN unless an nRF52 node is on the bench — record
   it with the hardware reason**, exactly as §7 records `stackhw`.
5. Finish with `joinprofile reset confirm` + re-seed if profiles are still wanted.

## §18 — Recording
As §11: verdict per checkbox · console lines verbatim · not-run items WITH THE REASON, never as passes or
failures. Failures ship with their output (D3). Retain Part 30.7's evidence set for any stop-rule hit.
