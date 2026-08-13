<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# ACTIVE BUG REGISTER

Authoritative work queue as of 2026-08-13. The former 4,734-line register is preserved, without renumbering or
deleting any record, in [the historical evidence archive](archive/2026-08-12-bug-register-history.md).

This file answers only: **what can still require work, measurement, or an owner decision?** The archive carries
the complete findings, superseded premises, closure evidence and gate history. Search the archive for the exact
identifier before implementing anything; old file:line references are hints and must be relocated by symbol.

Rules:

- Never reuse or renumber a B identifier. B151 is already duplicated historically; always cite its title.
- Change status here only after the corresponding evidence or owner ruling exists durably in the repository.
- Do not rewrite the archive to make old statements look current. Corrections belong in a new active note.
- OPEN permits scoped work. PARKED and RECORD ONLY require a new owner ruling before implementation.
- Closed records live only in the archive. Their absence below does not mean they were deleted.

## Active protocol, routing and measurement work

| ID | Status | One-line dispatch point |
|---|---|---|
| B20 | OPEN | Encrypted DM lengths 215–216 can disappear without send_failed. |
| B21 | OPEN | An oversized DM reports the wrong condition and no send_failed. |
| B24 | OPEN | q_tx.rt_total remains plane-inconsistent telemetry. |
| B25 | OPEN / UNMEASURED | A team member may answer a static sync with a team-plane beacon. |
| B31 | OPEN / POLICY | key_hash_for_id has no authoritative or TTL gate. |
| B34 | OPEN | The simulator collapses refusal reasons to generic error. |
| B35 | OPEN / SILENT | Channel self-skip compares identifiers across planes. |
| B36 | OPEN / CONTRACT | Received-message JSON does not expose attached location. |
| B52 | OPEN / CONTRACT | JSON exposes team confidence but not static confidence. |
| B54 | OPEN / OWNER CALL | The first claim in a full first-hand team table evicts one beacon row. |
| B55 | OPEN / CONTRACT | Document the S4b meaning of reqpubkey_sent.hash == 0. |
| B56 | OPEN / CONTRACT DECISION | A stage-2 reqpubkey failure is not app-visible. |
| B60 | OPEN / SPEC READY | Multi-gateway send_layer resolves the final hash on an intermediate layer; ACK policy remains open. |
| B111 | OPEN | A queued DM with ctr == 0 has no correlatable outcome kind. |
| B112 | OPEN / CORE | A non-zero DM counter does not prove enqueue success. |
| B153 | OPEN / IMPLEMENTATION SUBSTANTIALLY COMPLETE | Hybrid 10/11-byte RTS identity remains open with the B161/B157 closure chain. |
| B157 | OPEN / BLOCKED BY B161 | Implicit-ACK implementation is complete; universal safety still needs typed-answer origin identity. |
| B158 | OPEN / REDESIGN | Design MeshRoute-native retry jitter; do not inherit obsolete Lua constants mechanically. |
| B159 | OPEN | DATA de-duplication can expire inside the real retry horizon and deliver a retry twice. |
| B160-COV | OPEN / COVERAGE | No corpus scenario combines a non-AUTO plane with a requeue. |
| B160-SIB | OPEN / PRE-EXISTING | txitem_from_pending drops the channel/flood carrier set on the narrow metal requeue path. |
| B161 | OPEN / BLOCKER | Two typed-answer families lack an origin byte, preventing canonical flight identity. |
| B163 | OPEN IN LEGACY CHECKLIST; DIAGNOSIS SUPERSEDED | B182 disproved the leased-id diagnosis; give this record an explicit disposition instead of implementing its old proposal. |
| B164 | OPEN / NARROWED | Admission truth is fixed; actual on-air completion remains unobservable after post-admission rejection. |
| B165 | OPEN / MEASUREMENT OWED | The RTS duty-defer slot can overwrite a live deferred RTS. |
| B166 | OPEN | Ordinary unicast NAV prices a 3-byte CTS although a legal 4-byte CTS can fly, releasing NAV early. |
| B167 | PARTLY CLOSED / RESIDUE OPEN | The byte-table half is closed; resolve the remaining historical-size/timing residue in the archive. |
| B168 | RECORDED / OWNER DECISION | Decide whether to require the proposed end-of-arc mechanical board-warning sweep. |

## Active mobile-home and simulation work

| ID                                         | Status                      | One-line dispatch point                                                                                        |
| ------------------------------------------ | --------------------------- | -------------------------------------------------------------------------------------------------------------- |
| B138                                       | OPEN / GATE DEFECT          | Board flash figures are not reproducible under the current measurement method.                                 |
| B140                                       | OPEN / OWNER CALL           | Resolve the remaining corpus byte-identity/re-anchor question from the mobile-home arc.                        |
| B141                                       | OPEN                        | Repair the timing-fragile s28 assertion and investigate its possible channel-overhear defect.                  |
| B144                                       | RECORDED                    | A single re-armed timer can service a due entry about 1 ms late.                                               |
| B150                                       | RECORDED / COVERAGE         | Mobile admission-rejection retry jitter is corpus-dark.                                                        |
| B151 — missing §12.2 integration scenarios | OPEN / PLAN READY           | Add the late-home and auto-OFF scenarios from 2026-08-11-b151-integration-scenarios.md.                        |
| B152                                       | OPEN / DELIBERATE RESIDUAL  | Two optional, airtime-free link-confidence refresh hooks remain unwired.                                       |
| B154                                       | PARTLY CLOSED               | Two items are closed; exposing seconds until the next retry still requires state or HAL plumbing.              |
| B155                                       | OPEN / REGISTER MAINTENANCE | B151 was assigned twice; preserve both numbers and always qualify the title.                                   |
| B178                                       | OPEN / NARROWED             | Measure candidate-aware proactive canvassing; the interim trigger-2-only policy remains in force.              |
| B181                                       | OPEN / REASSESS AFTER B182  | Its original mobile-delivery premise was metric-corrupted; retain only the still-valid scenario investigation. |
| B183                                       | DIAGNOSED 2026-08-12, NOT FIXED · ✅ **QG PASS 2026-08-12** (harness mutation-proved: `verify.py` 36/36 checks and all 36 inverted expectations detected; `selftest.py` ambiguity/refusal controls pass; `lostj.py` exits 1 on its 2 genuinely unattributable frames) | The reattach chain stops at the TRANSMITTER, not at a protocol decision. Split into B186 (firmware+observability) and B187 (scenario). Evidence: `docs/superpowers/evidence/b183/README.md`. |
| B184                                       | OPEN / SIMULATOR            | The wrapper cannot express a hash-addressed DM with E2E ACK although firmware can.                             |
| B186                                       | **B186a ✅ IMPLEMENTED 2026-08-12 (observability) · B186b STILL OPEN (recovery, NOT implemented)** | A **core handling defect demonstrated through the simulator's ASYNCHRONOUS busy callback**: a J DISCOVER/OFFER/CLAIM accepted by `_hal.tx()` and then refused is dropped silently — `FrameTag::beacon` ⇒ `retry_slot_of` = −1 ⇒ `on_radio_busy` returns with no retry and no attributable emit, while `mobile_offer_tx` has already claimed success. ⛔ **Equivalent metal behaviour is UNVERIFIED** (`DeviceHal::tx` refuses SYNCHRONOUSLY → `tx_hal_rejected`, a different path); not a general modem failure, and no claim of metal invisibility. ★ **B186a LANDED:** the four mobile operations now carry **distinct internal TX identities** — `LbtKind::mobile_reclaim` is new, and the op rides `TxParams::tag`'s previously-unused HIGH byte into `BusyInfo::tag`; the async refusal emits `mobile_tx_refused{op, reason, reason_name, sf, busy_until_ms}`. ⛔ No wire change, no `BusyInfo` field, no length plumbing, no retry, ordinary beacons/floods untouched, `p.label` deliberately still `"BCN"`. **Identity captured at the SENDING SITE, never reconstructed** (`mobile_reclaim_deferred_rejected()`'s FSM-derived refund decision is deliberately left as-is — that is B186b's). Native 1512/81212/0 → **1515/81320/0**, 7 mutations all RED, telemetry-disabled compile clean **with its own failing control**; corpus 5 rows moved (`s07` +71, `s28` +9, `s29` +5, `s27` +4, `s37` +1) and **every one is byte-identical once the new emit lines are removed**; `sizeof(Node)` **unmoved (221880)**, RAM byte-identical on 6 board ABIs, flash +68…+704. ★ In `s07` the 71 reports are [[B183]]'s 71 exactly — **including the 2 frames its post-hoc correlator correctly refused to guess at**. Evidence + the HAL audit + the per-operation recovery proposals: `docs/superpowers/evidence/b186a/README.md`. |
| B189                                       | **OPEN / RECORDED — NEW 2026-08-12 (from the B186a HAL reachability audit)** | ⛔⛔ **`Node::on_radio_busy` HAS NO CALLER ON HARDWARE.** Measured by whole-tree grep: the only callers are `lib/core` (its own definition), the native suite (direct invocation) and the simulator wrapper — nothing in `src/`, `lib/hal/`, `variants/` or `tools/`. ⇒ the asynchronous busy path **and the entire R4.5b stash-retry machinery behind it** (`retries_left`, `kRadioBusyRetryTimerId`, `tx_giveup`) are **simulator-only**; on metal `DeviceHal::tx` refuses **synchronously** (`busy` on a full 8-entry ring, `too_long` > 255) and `pump_tx`'s failed arm **drops the frame without telling the protocol** ([[B164]]). ★ The mirror measurement: the synchronous path is **corpus-dark** — `tx_hal_rejected` = 0, `tx_failed` = 0, `oversized` = 0 across all 36 streams (the simulator has **no bounded TX queue**, so `kSimTxBusy` is unreachable there). ⇒ **each refusal shape exists in exactly one implementation and neither is proven for the other.** ⛔ No fix proposed; sequence this before any B186b recovery aimed at `on_radio_busy`. Full table: `docs/superpowers/evidence/b186a/README.md` §4. |
| B187                                       | ✅ **CLOSED 2026-08-12 — RECLASSIFIED AS EXPECTED SATURATION, NOT A DEFECT** (owner-ruled; ledger §1.19). The 1-hour rolling duty window **STAYS**; `s07`'s saturation (16 of 36 nodes ending at ≥99.6 % of budget) is **valid stress behaviour**. ⛔ **Do NOT retune `s07`'s window or its load to restore delivery, and NO `s07` re-anchor is authorised.** ⓘ Measurement that produced the reclassification: **36 of 36 corpus scenarios are in the ONE-SHOT duty regime** — none sets `duty_cycle_window_ms`, all inherit the 1 h default, every duration ≤ 1 h ⇒ a 1 h scenario seeing exactly one window is **correct modelling**, not an authoring defect. The rolling-window coverage gap is registered separately as [[B188]]. |
| B188                                       | ✅ **COVERAGE CLOSED 2026-08-12 by a NON-CORPUS fixture — the ORIGINAL RECORD FOLLOWS UNCHANGED.** `docs/superpowers/evidence/b188/` adds three purpose-built scenarios with an **explicitly compressed `duty_cycle_window_ms` = 30 000** (1 % ⇒ a 300 ms allowance, 20 rolls in a 600 000 ms run): **A** the simulator's asynchronous hard-block (the only refusal carrying `busy_until_ms`, reachable because a `FrameTag::beacon` J frame skips `tx_with_retry`'s `slot >= 0` duty pre-check), **B** the firmware's own pre-check + the `kRtsDutyDeferTimerId` re-run, **C** a CONTROL identical to B but at the 1 h default. All four ruled verifications pass with the ledger **recomputed from the stream**: refusal at exhaustion (53/53 re-derived) · `busy_until` exact (53/53) · incremental rolling expiry (23 distinct window fronts, smallest wait **930 ms** ≪ window, **causal** in-window airtime drops **4×**) · resumed transmission (**18/19 deferred RTS fly at exactly `t + wait_ms`**). `verify.py` = **39 checks, 0 failed, exit 0, 39/39 mutation-proved**, plus `selftest.py` = **39 reader-level controls, 0 failed, 39/39 mutation-proved** (causal-ledger, node-binding incl. SF equality and missing-authority refusal, run-horizon, and shape-keyed pricing controls). Refusals are **53/53 priced — 49 from observation + 4 through the cross-checked `SimRadio::getEstAirtimeFor`, 0 UNPRICED** (a price is derived or refused, never substituted); mobile reports are **38/38 bound to exactly one PRECEDING SAME-NODE `tx_deferred`**; the run horizon is read from **`sim_end` = 600 000 ms**. ⓘ **The verifier was reopened and repaired 2026-08-13 over three review rounds — an acausal ledger, a substituted frame price, a length-only pricing key that overwrote conflicting observations and hid a wrong SF, a correlation that did not bind the node, a defaulted node authority, and a self-fulfilling horizon. The repair record, including the one withdrawn figure and its replacement, lives in `docs/superpowers/evidence/b188/README.md` §0; the values in this row are the current ones.** ⚠ **STANDING LESSON: 26 checks AND 26 mutations once passed over a wrong model — mutation coverage validates assertions, never the model they are written in.** the control measures the dark regime directly (**peak 13 929 ms of a 36 000 ms allowance, 38.7 %**, zero refusals). ⛔ **NON-CORPUS: never added to `simulation/`, the runner, the anchor table or any delivery figure; `s07`'s window and load untouched; no corpus scenario lengthened.** ⓘ ORIGINAL RECORD: | ⛔ **THE ROLLING DUTY WINDOW'S BOUNDARY BEHAVIOUR IS CORPUS-DARK — NEVER EXERCISED ANYWHERE.** Measured: **36 of 36** scenarios are one-shot (no scenario sets `duty_cycle_window_ms`; all inherit the 1 h default; every duration ≤ 1 h), so the moment a rolling window RECLAIMS budget is untested across the whole corpus. ★ **Owner-ruled method: test it with a COMPRESSED EXPLICIT `duty_cycle_window_ms`** — ⛔ **NOT by lengthening a scenario and NOT by shortening `s07`'s** (ledger §1.19 retains the 1 h default and forbids retuning `s07`). ⓘ This is the gap that let [[B183]]'s busy-refusal path stay invisible: a budget that never rolls means the post-exhaustion path is entered once and never left. |

B182 is closed and archived. Its measured 754 deliveries are **not** a ratified replacement floor; the delivery
floor remains frozen pending a separate owner ruling.

## Active Heltec/OLED and product work

| ID | Status | One-line dispatch point |
|---|---|---|
| B65 | OPEN / OLED UI | The first UI tick can blank a panel that has never drawn. |
| B70 | OPEN / PLAN DEFECT | A draining test helper is called twice and silently removes critical assertions. |
| B83 | OPEN / UI | Tracker retention remains bounded only by caller obligations. |
| B92 | OPEN / SPEC | OLED font names in the spec disagree with the plan and implementation. |
| B93 | OPEN / LATENT | mr_ui.h hardcodes the meshroute namespace instead of using MESHROUTE_NS. |
| B99 | OPEN / PLAN DEFECT | The Task-6 listing tears the emergency overlay and contains a tick that does not compile. |
| B104 | OPEN / COVERAGE | Task 6 removed part of the board-canvas redraw coverage. |
| B114 | PARTLY CLOSED | The bundled emergency report was split; only the B116 evidence gap remains open. |
| B116 | PARKED / OWNER-RULED | Channel HAVE is not accepted as delivery evidence; do not implement without a new ruling. |
| B118 | RECORD ONLY | Owner direction for a channel REQUIRES ANSWER app code; deliberately not implemented. |
| B119 | OPEN / SOURCE DOC | Push::enc documentation incorrectly says the channel path is always cleartext. |
| B124 | OPEN / DEDUP | The 1S-LiPo plausibility window is duplicated across translation units. |
| B125 | OPEN / OWNER CALL | Decide whether retaining the last good battery value is intended product behaviour. |
| B131 | OPEN | MR_UI_BLANK_MS is referenced by design but does not exist in the tree. |
| B134 | OPEN / PRODUCT · ⚠ **NOW USER-VISIBLE** | ESP32/Heltec inbox storage is volatile, so delete is not durable across reboot. ⛔⛔ **CORRECTED IN PLACE 2026-08-13 — MY FIRST ANNOTATION OF THIS ROW STATED THE GAP BACKWARDS and is withdrawn: it said *"a wearer who deletes a message on the panel gets it back after a power cycle"*. **THAT IS FALSE.** `FixedInboxStore` is a RAM ring (`lib/core/fixed_inbox_store.h`: `Slot _slot[Slots]`, `persisted_next_seq()` = 0, *"seq restarts at 1 each boot"*) and `fw_main.cpp` draws a fresh `storage_epoch` per boot because *"the volatile store lost its history"* ⇒ **after a reboot NEITHER the record NOR its tombstone exists; the WHOLE inbox is empty**, so nothing can come back. ⚠ **The real shape of the gap: ALL inbox history is volatile.** Within a runtime the delete is real and verified by `pull()`; across a reboot there is nothing to verify, so ⛔ **the "successful deletion survives reboot" criterion (spec §6.2) is UNTESTABLE on Heltec and would PASS VACUOUSLY — it is owed by a durable backend (nRF52 + QSPI).** ⓘ §UI-7D slice B (2026-08-13) makes the volatility user-visible in a new way — a wearer curates an inbox that empties itself at the next power cycle — which is a PRODUCT decision, not a defect in that slice: no part of the firmware claims otherwise. Bench Part 11.4 is the control that pins the volatility; Part 19.1 step 6 defers to it. |
| B190 | RECORDED / COVERAGE | ⛔ **The detail modal's "record not found by the pull" answer is UNREACHABLE-BY-CONSTRUCTION in one service pass, so no control can redden it.** Measured 2026-08-13: `build_snapshot()` fills the preview rows from a `pull()` at the TOP of `mr_ui_tick`, `note_inbox_cursor` can only select a row that pull produced, and `ui_service_inbox_request` runs later in the SAME pass — so a valid selection is always found. The branch (`if (!c.found) s_model.on_inbox_open_gone(...)`, `src/firmware_ui.cpp`) is retained as C2 fail-loud (the request must always be answered) and its MODEL side IS covered natively; what has no control is the device-side call itself. Its removal leaves `tools/probe_firmware_ui/` green — the probe's `MESSAGE GONE` case is reached through the LIST's identity refusal instead. ⇒ do not read the probe's green as cover for that line. |
| B191 | RECORDED / PRODUCT | **The panel can only delete what it can browse: `kInboxRowsPerKind` = 4 rows PER KIND** (spec §6.1's per-kind budget, chosen so a chatty channel cannot evict every DM row). A record outside that newest-4 window has no snapshot row, so it cannot be selected and therefore cannot be deleted on-device; `del_msg <dm\|chan> <seq>` over the console remains the only route to it. Recorded 2026-08-13 with §UI-7D slice B, which is what makes the bound reach a destructive action rather than only a view. ⛔ No widening is proposed, and ⛔ **the first version of this row gave the wrong reason** (*"raising the bound trades DM rows for channel rows"* — withdrawn): raising the PER-KIND bound does not redistribute anything, it costs **snapshot/model RAM** (each retained row is an `InboxRow` in `UiSnapshot` **and** in the reused `InboxRowBudget`, and this slice already measured +512 B on every OLED env) plus **browse depth** — more short presses to reach the oldest row on a 5-row panel. ★ **The DM-versus-channel trade is a REDISTRIBUTION within the fixed eight-row budget** (`kMaxInboxRows` split `kInboxRowsPerKind` each way), which is a different change and would reopen exactly the eviction hazard §6.1's per-kind split exists to prevent. |

| B192 | ✅ **BEHAVIOUR OWNER-RULED 2026-08-13** (ledger §1.22) · residue → B193 | ⛔ **CORRECTED IN PLACE 2026-08-13 (§3 rule 3): this row's status read `RECORDED / OWNER DECISION` and its text said the merge was *"implemented but not ruled"*, that *"no approval of this shape is claimed"*, and that a **QG recommendation** was *"not an owner ruling"*. ALL OF THAT WAS TRUE WHEN WRITTEN AND IS NOW FALSE — the owner ruled it. The old wording is withdrawn here, not deleted, and the description below is kept as the record of what was decided.** ★ **THE RULING, in reported form (⛔ no quotation — §3 rule 4): RELOAD performs the THREE-WAY MERGE — fields UNCHANGED in the OLED draft adopt the current persisted values, fields EDITED in the draft remain unsaved in the draft, and DISCARD remains the explicit full reset.** ⇒ **the implementation is what was ruled; no code change was required and the merge must not be re-opened or "improved".** ⚠ **WHAT THE RULING DOES NOT SETTLE, and this distinction stays: RELOAD's *behaviour* is ruled; its NV / power-cut QUALIFICATION remains DEFERRED to the UI-14 device binding and [[B193]]** — §UI-13 proves the logic against a counting/failing FAKE store (no NVS/LittleFS write, no wear, no reset-during-write). **A green suite says the logic is right, never that the storage is.** ⓘ Historical description follows. (was: **`RELOAD`'s semantics are implemented but not ruled.** Spec §3.6.1 defines `DISCARD` ("reloads the persisted values and clears the marker") and requires a conflict to be resolved by "`RELOAD` or `DISCARD`", but it never says what `RELOAD` does — and if it did the same thing as `DISCARD` the paragraph would not name two verbs. §UI-13 implements it (`ConfigService::reload`, `src/firmware_config_service.h`) as a **three-way merge of the three states**: a covered field the operator did **not** edit adopts the newer persisted value; a field they **did** edit keeps their value; the baseline becomes the freshly loaded record. ★ The reason it is not "keep the whole draft and re-baseline": that would re-save the operator's stale value for a field the companion had just changed and they never touched — i.e. it would reinstate exactly the **last-writer-wins the same paragraph forbids**. ⛔ **No approval of this shape is claimed**; the alternatives are (a) make `RELOAD` identical to `DISCARD` (costs the operator's edits, gains nothing in safety) or (b) refuse to merge and require `DISCARD` outright. Native cover: the merge case and its convergent-write sibling in `test/test_firmware_config_service.cpp`; mutations **C24**/**C25** redden both directions.) ⓘ **END of the historical description.** ⓘ **Provenance, kept because this arc has five incidents: the merge was implemented first and recorded through several review rounds as a QG RECOMMENDATION explicitly labelled NOT an owner ruling (§3 rule 5 — the channel a recommendation arrives on does not promote it). That labelling was correct at the time; the ruling of 2026-08-13 is what changed it, and it is recorded in the ledger, not inferred here.** |
| B193 | RECORDED / SCOPE | **The §UI-13 service has no device binding, deliberately.** `ICfgStore`/`ICfgLive` (`src/firmware_config_service.h`) are unimplemented on hardware, so nothing on a board constructs or calls the service and the firmware's behaviour is unchanged by that slice. The two obligations a future binding inherits are recorded **in the header**, not only here: the store's `load()` must be the §nv-ritual `nv_load_stamped` (load-or-seed + version stamp) so an unprovisioned chip opens on the live config rather than being refused, and `apply_live()` must reproduce `handle_cfg_set`'s **OFF→ON `mobile_register_current()` bridge** (`src/firmware_config.cpp`) — setting `mobile_autoregister` without it leaves a mobile that never discovers a home. ⚠ Also recorded: `apply_radio_live` is **not reachable** from the covered set, because none of the four covered fields is a radio parameter; it remains the correct hook the first time a radio field is covered. |

## Recorded methodology and coverage constraints

These are not implementation authorizations. They remain visible because they constrain future gates.

| ID | Status | Constraint |
|---|---|---|
| B63 | RECORDED | Xtensa link-order relaxation can move flash even when a moved object contributes zero bytes. |
| B77 | RECORDED | A heading grep can match the prose instructing the grep and create a false gate. |
| B82 | RECORDED | This build has no .d-file census; relative-path absence previously looked like success. |
| B86 | RECORDED | ARM literal packing can move by roughly 32 bytes inside one build session. |
| B89 | RECORDED | Most OLED flash cost belongs to the I²C transport rather than the display library. |
| B98 | RECORDED | A UI regression initially stayed green under its defect mutation; controls must prove reachability. |

## Parked or trigger-gated defects

Do not dispatch these without a new owner ruling or the named trigger.

| ID | Status | Trigger or reason |
|---|---|---|
| B5 | PARKED | channel_pull has no team scope and exposes the static source id. |
| B6 | PARKED | The team plane has no budget-penalty mirror. |
| B7 | PARKED | The team plane has no slow-reprobe state. |
| B8 | PARKED / UNMEASURED | Relay behaviour for src == 0 is unresolved. |
| B9 | PARKED / SIM TELEMETRY | Team rt_update.slot is mislabelled. |
| B10 | PARKED / TEST DEBT | The simulator has no working routes command. |
| B11 | PARKED / TRACE DEBT | Frame-trace switches omit live values. |
| B12 | PARKED / REFACTOR | Seal-or-refuse logic is triplicated. |
| B13 | PARKED / REFACTOR | The team-liveness scan is duplicated. |
| B14 | PARKED / COMMENT | A routing comment in node.h has drifted. |
| B15 | PARKED / REDESIGN DEPENDENCY | The binary config TLV omits team_ch_key. |
| B16 | PARKED / SIM PARITY | send_layer grammar differs between simulation and metal. |
| B18 | PARKED / D2 | A team H relay can read the static binding table. |
| B19 | PARKED / FOLD INTO B12 | deleg_ack_put is duplicated at eight call sites. |
| B23 | PARKED / POLICY | The metal resolve verb reaches AUTO and carries a dead field. |
| B37 | PARKED / DEPLOYMENT TRIGGER | Symbolic wire tests cannot detect deployed format drift. |
| B57 | PARKED / DELIBERATE | A beacon-learned binding does not consume pending reqpubkey intent. |
| B59 | PARKED / DO NOT DISPATCH | Reliable repair may require a routing/custody algorithm change. |

## Non-bug decisions and audits still open

| ID | Status | Decision |
|---|---|---|
| D1 | TRIGGER-GATED | Revisit the team DV hop cap only when a team path exceeds eight combined hops. |
| D2 | OPEN AUDIT | Audit plane-typed reads that can fall back to the static table. |
| O2 | PARKED | Fold deleg_ack_put de-duplication into B12; never take it alone. |
| O4 | OPEN SECURITY DECISION | Decide how BLE access to team exportkey is protected. |

## Closed and superseded records

All closed, fixed, disproven and superseded records—including their original wording, corrections and evidence—are
preserved in [the archive](archive/2026-08-12-bug-register-history.md). No bug number was changed or reused during
this cleanup. Search that file for the exact B token; the old wiki-style [[B###]] notation was never a filesystem
link.
