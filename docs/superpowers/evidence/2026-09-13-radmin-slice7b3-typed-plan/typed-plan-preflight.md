# B389: read-only typed-plan enumeration, 2026-09-14

Scope: bounded source-validation alongside 7b-3-0, not 7b-3 implementation,
brief reissue, QA verdict, allocation ruling or register closure. No repository
file was edited by this work; no shared-tree build or linked board build ran.

Base: `b9d75aaca55a0e350d9707512e9b6eec8a81ab22`. The complete `git diff
f993191be7f6980870f440f6032bca72278539a7 b9d75aa -- lib src` is empty. The
source authority remains the real code, not the old pending/proposal text in
behavior brief revision 3. R-RA-37/38/39 are settled; B389's allocation is not.

## Result

The source has **180 policy rows, exactly 48 disruptive rows in 12 families**.
The exact independently extracted enumeration, policy line anchors, authority
and primary handler anchors are in `disruptive-enumeration.json` and the readable
`all-48-rows.md`. No row, alias, coarse bare-family or profile refusal is dropped.

Existing typed requests do not constitute a complete owned activation plan.
In particular, JoinService and ProvisioningService still combine validation,
candidate construction, durable transaction and live application; their public
apply methods cannot be called later without repeating mutable decisions.
The 248-byte raw-line comparison is neither sufficient nor a valid upper bound.

**Recommended preparation split, not automatic scope narrowing:**

- Reuse the existing effect-only reboot/prep wrappers and OTA ensure-entry
  primitive through explicitly typed, sink-bounded hardware bindings. These are
  four policy rows, no command argument bytes or new secret generation.
- A small C1 preparatory increment should separate the real factory-reset,
  sleep and crash grammar/admission from their effect-only operations (eight
  rows). These are small seams, not reasons to abandon these useful actions.
- The remaining 36 rows require actual prepared transaction/delta services.
  They are the strongest R-RA-39 named-family exception candidates if the owner
  wants the scheduler before those preparations. The proposed exceptions are
  `cfg set`'s 22 disruptive rows, `gateway` (1), `join` including its alias row
  (2), `create` including its six argument rows (7), `leave` (1), `team`/`team new`
  (2), and `regen` (1). QA must record each exact exception and a fenced follow-up
  before coding. If it does not, full-family preparation remains required.
- Factory-reset/sleep/crash also meet the literal split requirement if QA chooses
  a temporary exception, but this report recommends the small preparatory split
  rather than automatically refusing another eight rows. No such exception is
  recorded here. **Prep-restart cannot be refused via this shortcut:** R-RA-38
  expressly chose scheduling, including its lockout.

There is no honest final all-family plan size yet: the missing service contracts
determine what state is owned versus merged and which families remain refused.
The measured small-scope carrier below is clearly a conditional comparison,
not a substitute allocation request or a declaration that four rows complete 7b-3.

## Families and actual seams

| Family / policy row count | Existing code and reusable typed authority | Owned plan / missing boundary and required proof |
| --- | --- | --- |
| Reboot/prep / 3 | Router `firmware_commands.cpp:1504,1528`; no-argument wrappers `fw_main.cpp:293,297`; effects `:320,423`. Alias policy row must resolve to the actual spelling, not conflate reboot and halt. | Kind is sufficient for semantic arguments. Reboot calls global mrcon; prep receives Print but returns void and encodes wipe outcome only in text (`:437–442`). Add an explicitly scoped typed effect/binding outcome, never parse Print to infer success. Preserve reset marking, delay/flush and both independent inbox erase attempts. Prep must consume the action before clearing learned state and halt; remote RX then stops, local recovery remains. |
| OTA / 1 | Exact router `firmware_commands.cpp:1531`; fw_ota/do_ota `fw_main.cpp:294,335` toggle on ESP. **`mrota::ota_start()` at `device_ota.cpp:92` already returns true while active**, otherwise starts SoftAP/server; `ota_stop()` is distinct at `:108`. | Own backend enum (Wi-Fi/BLE DFU) and kind, not a command string or live Print. Preparation checks compiled backend and captures truthful backend OUTPUT. Activation uses ensure-entry, never fw_ota's toggle. nRF effect at `fw_main.cpp:335–342` needs a typed board-glue binding; preserve fault reset marking. Existing Wi-Fi boot messages go to mrcon, so sink ownership/allowed scalar hardware diagnostics must be explicit. No remote firmware payload, no new OTA heap or cached adapter. |
| Factory reset / 2 | Exact confirm grammar in `firmware_commands.cpp:1072–1073`; equivalent shared `parse_confirm_token` at `firmware_config_parse.h:562`. Erase/reboot block `firmware_commands.cpp:1083–1086`. | No semantic payload after confirm is proved, but there is no effect-only reset helper. Calling the old handler at activation reruns public grammar; copying its destructive block forks the operation. Small C1 extraction should keep shared exact token behavior, two non-short-circuited inbox wipes, factory_erase and reboot with the existing partial-erase warnings. The bare/invalid form cannot become scheduled. |
| Sleep / 2 | `firmware_commands.cpp:1096–1105`, router `:1558`. Current grammar: skip spaces; any tail with prefix `off` selects false; everything else, including empty and other text, selects true. | One owned boolean, no live pointer. There is no shared typed decision/effect seam; extract it without silently making local grammar stricter. Test `sleep`, `sleep on`, `sleep off`, `sleep off...` and non-off tails, and MR_NO_POWERSAVE's existing no-op limitation. Effect remains the flag assignment at activation, not preparation. |
| Crash / 4 | `fw_main.cpp:395–416`; first mutable guard is g_mr_trace_on; prefix parses hang/fault/reboot; fault backend gates NRF52_PLATFORM/MRFAULT_ESP32; reboot delegates to do_reboot. | Own typed mode plus resolved supported backend; freeze the debug admission at preparation. Existing fw_crashtest takes text and rechecks debug, so a deferred call could refuse after scheduled. Small C1 split must preserve local prefix semantics and messages, expose pure mode/admission and typed hardware apply, reject disabled/unsupported/usage before any promise, and never include device_fault.h in another TU. |
| Regen / 1 | `firmware_commands.cpp:1031–1056`: optional CLIENT debt guard, IdBlob load, void RNG draw, save_id, identity_from_seed, set_identity and set_crypto_identity. `Identity` at `lib/core/identity.h:34`; checked seed interface `firmware_admin_identity.h:165`, real adapter `firmware_commands.cpp:302`. | Own fresh seed and prepared identity material, or one authoritative deterministic seed-to-identity plan proven complete. Name/location preservation is an IdBlob delta issue, not permission to retain stale metadata. Never call do_regen after scheduled (would draw fresh material), copy admin root, or rotate /mradmid, /mracl, controller stores or sessions. Extract prepare/commit so predictable read/draw/admission failure is before promise, installation is after accepted ownership and activation. Wipe seed, derived private material and temporary IdBlob on abort/consume; no hidden 196-byte Identity static. Preserve existing local behavior; do not incidentally fix the void HAL RNG provider. |
| Cfg / 22 | `handle_cfg_set` at `firmware_config.cpp:251`; shared predicates and PhyArgs in `firmware_config_parse.h`; live radio `firmware_config.cpp:60`; save and apply at `:611,630–631`. | Need a typed key discriminator and exact value/derived side effects, not raw text or a whole stale Blob. Parsing, live mutation and persistence interleave (details below), so invoking the handler to validate already changes state. Existing ConfigService covers only four UI fields (`firmware_config_service.h:106`) and explicitly lacks a radio apply class (`:127`); it is not a ready 22-key remote service. Prepare/apply extraction must preserve key grammar and U2 conversion, derive role restrictions before promise, own all values, and preserve unrelated durable updates made while waiting. Reboot-only keys stay reboot-to-apply: scheduling their write must not silently add reboot. |
| Gateway / 1 | `GatewayProvision` `lib/core/node_carriers.h:74`; pure parser/validator `node.cpp:477,405`; handler `firmware_config.cpp:692–763`. | Own fully resolved layers/windows and explicit/inherited field semantics. Handler seeds a deliberate pending-Blob subset, resolves current BW/CR/frequency, validates/derives windows, maps fields inline then saves. No shared typed candidate/commit seam exists. Extract the mapping once; do not replace its deliberate subset by seed_blob_from_live. It is **persist-only, reboot-to-apply**, not a live retune or implicit reboot. Preserve omitted beacon/default semantics and unrelated changes at delayed commit. |
| Join / 2 | `JoinRequest` `firmware_join_service.h:73`; validate_join `:150`; blob_put_static_join `:173`; apply_join `:196–222`; real binding/parser `firmware_config.cpp:902–936`. | Request owns values but public apply revalidates and loads mutable state. Preparation must capture the resolved provisioning meaning and reuse the one Blob conversion; activation needs a validated-only commit/live seam. Do not retain a borrowed request, repeat parser, or call apply_join late. Proof includes full-byte layer vs low-nibble live leaf, preserved unrelated fields/name bytes, save failure with zero live effects, and DAD only on successful activation. Gateway build's existing refusal remains. |
| Create / 7 | Handler `firmware_config.cpp:1180–1230`; pure kv_next/phy_arg_take/phy_args_in_range/parse_sf_list reused, but extra parsing and candidate assignment live in handler. Fresh nonzero lineage loop `:1211`; save `:1213`; provision_apply_live `:1215`. | Own PHY, SF bitmap, quantized duty/active fraction, intervals, copied leaf-name bytes+length and the **already generated** lineage/epoch. The local name buffer is length-bound, not a retained C string. No CreateRequest or prepared apply service exists. Extract its sole parse+composition path before deferred use; no activation-time lineage generation or stale whole-Blob replay. Preserve current clamps/defaults as local behavior; future bounded entropy changes would be a separately identified fix. |
| Leave / 1 | `firmware_config.cpp:2422–2435`: reads pending Blob, keeps freq only, builds explicit defaults, save, notification, provision_apply_live(false). | Own selected keep-frequency and fixed reprovision meaning; no argument string. This still needs typed prepare/commit because the public handler rereads mutable frequency at activation and reconstructs the carrier inline. Preserve the one default composition and no-DAD apply. Do not conflate with `team 0`, which preserves team keys; ordinary leave's admin stores remain separate. |
| Team / 2 | `TeamRequest`, TeamProjection and TeamPlan at `firmware_provisioning_service.h:87,264,278`; shared project_team/stage_team_candidate `:423,509`; apply_team `:755`; real parser/snapshot `firmware_config.cpp:2025–2246`. | Own minted team ID, projected role, resolved PHY/DATA-SF set, key action and full retained key material, membership/DAD decisions and config/keyring commit information. Do not retain ProvSnapshot: it contains **borrowed live key pointers**. Existing pure builders are valuable, but apply_team owns their lifetimes and immediately keyring.put + saves + mutates. Keyring.put itself validates/read-classifies/capacity-checks and writes (`firmware_team_keyring.h:519–556`); there is no held reservation. A safe split must handle keyring capacity changing while waiting, corrupt stores, no-change vs explicit-live drift, unrelated cfg writes, and wipe every request/plan/candidate secret. Keep non-disruptive team key verbs outside the fallback. |

All anchors above are in the unchanged b9d75aa source; detailed source hashes
and a live-tree equality check (excluding the parallel codec work, which is not
an enumeration input) are recorded in `source-audit.json`.

## Cfg row detail: complete key grouping, not 22 independent implementations

- Radio: `freq` (firmware_config.cpp:305), `routing_sf`/`control_sf` (:315),
  `bw` (:318), `cr` (:321), `tx_power` (:324). Shared range predicates exist;
  radio/Node/HAL changes occur after the save via apply_radio_live. Preparation
  must resolve a complete typed radio change without saving or retuning.
- Live direct fields: `sf_list` (:330), `host_mobiles` (:345), `leaf_id` (:423),
  `gateway_only` (:428), `mobile` (:451), `mobile_autoregister` (:484, two policy
  rows including `mobile_autoregister true`). These modify live config before
  the final save; sf_list may bump the persisted epoch; mobile_autoregister can
  start registration. The role/host guards read mutable state. Existing code
  therefore is not a read-only preparation API.
- Pending/reboot-only: `node_id` (:296), `n_layers` (:534), `layer0_id` (:539),
  `l1_layer_id` (:559), `l1_node_id` (:564), `l1_routing_sf` (:569),
  `l1_sf_list` (:574), `l1_freq` (:594), `l1_bw` (:599), `l1_cr` (:604).
  These persist values and do not live-apply or reboot. All sub-arm line anchors
  were checked by executable key matches at the pinned source; re-derive them
  again when a future extraction brief is issued.

## Common delayed-state hazards and ownership contract

1. Ingress completion wipes the command body. Exact retry/ACK/slot invalidation
   cannot leave a stored parser pointer, Print adapter or stack request alive.
2. Transcript owns frozen u32 delay independently from the one action row;
   action consumption and reuse cannot change retained terminal bytes. No need
   to copy the response route into the action: the transcript/seen path already
   owns routing; the action must retain request/slot/epoch/source identity.
3. A snapshot of the whole 280-byte config is not a safe shortcut. It contains
   unrelated mutable durable fields, including the channel counter lease and
   team keys. Existing ConfigService deliberately reloads and merges covered
   fields (`firmware_config_service.h:389–415`) to avoid reverting those. A
   prepared delta must preserve unaffected updates without repeating policy
   or silently failing a previously accepted action on normal config drift.
4. Mutable role/hosting/team/keyring prerequisites cannot simply be rechecked
   and refused at activation. The preparation split must establish how their
   meaning stays valid; ordinary metadata/config edits are not the brief's
   external-physical-pre-emption exception. Do not invent a new runtime lock or
   reservation without sizing and explicit fence review.
5. No fresh identity/team/lineage draw after scheduled. Wipe every secret-bearing
   transient and resident copy on abort, unarmed cancellation, cold init and
   consume. A consumed row may need a stack-owned transfer until apply finishes;
   price that stack separately and wipe it before non-returning reboot paths.
6. Keep only the existing five counters. The brief's bounded activation failure
   diagnostic is not a sixth counter; a proposed two-byte kind/outcome snapshot
   is priced below rather than hidden. Its final enum/retention policy still
   needs specification. Status reads must expose only scalar metadata.
7. No core header may depend on firmware/NV types. A future typed plan should
   use a feature-local pure carrier or a single bounded byte representation with
   one typed conversion. Native and gateway own one instance; mobile owns none.
   A borrowed service pointer/vtable cached in Node is not a zero-cost solution.

## Follow-up fences and closure proofs proposed for QA

**F-simple (small C1 preparation, eight rows plus shared effect diagnostics):**
extract existing grammar and typed effect-only seams for factory/sleep/crash;
typed result/sink binding for prep/reset and explicit OTA entry binding. No
scheduler behavior in the refactor. Prove local real-TU byte/operation-order
equivalence, exact factory confirm corpus, prefix grammar, debug/backend gates,
both wipe failures and reset marking, plus existing native/probes/mutations and
board/corpus gates. Then 7b-3 adds delayed semantics separately.

**F-config (22 cfg + gateway row):** one typed parsed field/delta and one
candidate/live-apply authority; gateway retains its deliberate seed subset.
Separate refactor first; no stricter numeric grammar or incidental local
save-before-live correction. Closure: all 23 rows' valid/invalid/profile forms,
identical durable bytes/local output and operation order, pending-only vs live
effects, preserved unrelated fresh NV fields and no dropped carrier fields.
The later deferred feature must additionally prove no early mutation, immutable
prepared values despite overwritten input, and no delayed-policy refusal.

**F-provision (join 2 + create 7 + leave 1):** reuse blob_put_static_join and
provision_apply_live; expose canonical prepare + validated apply, and establish
the corresponding typed create/leave deltas without copying composition.
Closure: generated lineage frozen once, full leaf/name/SF/duty/interval data
owned, role/profile refused before effects, save failure order, unchanged local
goldens and real radio/DAD call counts; delayed tests overwrite all scratch and
alter unrelated NV fields before activation without losing either intent.

**F-team (2):** extend existing project/stage service with an owned prepared
transaction and authoritative apply, including the keyring's admission/commit
boundary. Closure: no-change vs live drift, same/new/zero team, supplied/minted
keys, role refusals, full/corrupt keyring, exactly ordered keyring then cfg saves,
all wipe exits, retained saved keys, DATA-SF preservation and zero early airtime.
Prove how capacity/membership changes during the wait cannot turn scheduled into
an ordinary predictable refusal or orphan hosts. This semantic lifecycle question
must be settled in the preparation design, not hidden in a raw TeamRequest.

**F-regen (1):** typed staged messaging identity prepare/commit using existing
identity derivation and checked-seed idiom; retain name/location semantics and
local behavior. Closure: one generation, no early identity install, exact staged
material applied after all scratch is overwritten, failure/abort/consume wipes,
save failure with old live identity, and admin root/ACL/sessions and controller
records unchanged. Do not broaden to B312's provider repair.

If QA records an R-RA-39 exception instead of completing a prerequisite first,
the behavior gate must retain executed refusals for **every affected policy row**
and representative supported/invalid/profile/authority forms, zero draw/write/
retune/halt/reset/OTA/airtime effects, and unchanged authority classification.
Named follow-ups remain open; neither family nor disruptive arc is complete.

## Measured type inventory and explicit non-claims

`measure.py` uses the existing board-ABI instrument's PlatformIO idedata and
compile-command builder in a private pinned source archive. Three sequential
compile-only TUs; each target's own nm reads sizeof/alignof symbols. No linker,
Node production declaration change or RAM delta claim. Setup failures and their
corrections are retained in `measurement-attempts.md`.

The resulting native/ARM/Xtensa values are in `measurements.json`. Production
types: JoinRequest 32/8, TeamRequest 136/8, TeamProjection 8/4, TeamPlan 128/8,
ProvPhy 40/8, GatewayProvision 88/8, LayerConfig 40/8, Identity 196/4, Blob 280/8,
IdBlob 80/4, TeamKeyBlob 296/4, CfgValues 4/1. ProvSnapshot contains pointers and
therefore its size is not an owned-plan bound: it measures 40/8 native and 32/8
on each board ABI. PhyArgs has the same 40/8 versus 32/8 difference because its
parse-time long fields change width. Node measured 230896/8 native, 157264/8
gateway ARM and 117912/8 mobile Xtensa; its production pins remain unmodified.

The **conditional effect-only comparison**, for the four rows with no owned
parameterized transaction, stores request ID/epoch/deadline, source hash/delay,
slot/authority/kind/phase/backend/trigger. It is 40/8. Candidate header is 32/8,
up from 24/8. One action row plus four header increases and two diagnostic bytes
with alignment yields candidate state 8904/8 versus actual 8824/8: **+80 bytes**.
This comparison deliberately prices diagnostics and padding, not just payload.
It is **not** a 48-row plan, not the recommended final scope, not an approved
allocation, not a Node re-pin, and not linked RAM. Even in this limited scope,
the effect sink/result and retained diagnostic contracts must be finalized.

No full native suite, corpus, probes, mutations, warning census or board links
were run for this read-only enumeration. The codec's concurrent gate remains
the parent task. B389 stays open pending the chosen prepared service scope,
complete final typed-plan pricing, owner allocation and later independent QA.
