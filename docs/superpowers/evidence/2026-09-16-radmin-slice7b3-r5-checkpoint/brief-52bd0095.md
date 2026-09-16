<!-- QA/Author: OpenAI Codex (revisions 1–4); revision 5 refresh: Claude (QA/Author); production coder: separate Codex session -->
# Remote-admin v2 Slice 7b-3 — deferred actions and scheduled terminals

**Revision 5 — 2026-09-16 (Claude): REFRESHED AT THE P1 COMMIT; READY FOR CODER SOURCE-VALIDATION AND
IMPLEMENTATION. BEHAVIOUR HOLD LIFTED — no open owner decision. B402 (coder preflight, 2026-09-16) folded: §3.2 anchor and the §2.1/§5 carrier rule below.**
Base **`7442e6f570abdcd74ceed20d4c0cb9e2855d0719`** (owner commit `P1`), clean tree, plus the permitted
uncommitted QA documentation listed below. Simulator **`06746a97de5764415d6fcef10b97bca90569b9c7`**,
clean/unchanged. Prerequisites now all hold: the 7b-3-0 codec (R-RA-37, commit `ac5f9a5`, B391 closed); the
7b-3-P1 simple-action preparation (B394/B399/B400 closed by the [independent P1 gate](../evidence/2026-09-15-radmin-slice7b3-p1-qa-gate.md),
owner commit `7442e6f`); and the B389 allocation (**R-RA-40**: +80 B, Node 230976 native / 157344 gateway,
mobile unchanged). R-RA-37/38/39/40 are settled, not renewed owner choices. Commits are not blocking points
(owner ruling 2026-09-15): the coder validates this revision against `7442e6f` and implements; nothing waits.

**What revision 5 changes (for the coder's source-validation):** (1) base `ac5f9a5` → `7442e6f`; (2) the
"future P1 API" rows are replaced by the committed API in `src/firmware_action_effects.h` (§1 table, §3.1, §4);
(3) every `fw_main.cpp`/`firmware_commands.cpp` anchor is re-derived at `7442e6f` (P1 shifted `fw_main.cpp`
by about +75 lines and `firmware_commands.cpp` by +11); (4) baselines are the P1 gate's: native 2916/184587/0,
boards gateway 203956/568220 and mobile 207756/1373576, union floor 53 batteries / 827 configured;
(5) **B401** (displaced include comment, `fw_main.cpp:43–44`) is folded into this fence as a comment-only fix;
(6) §2.3, §5, §7 and §8 no longer reference a pending P1 gate or a pending allocation ruling; (7) **B402 fold-in:**
the §3.2 decoder anchor is `remote_codec.cpp:652`, and the core row stores `kind`/`backend` (and the two
diagnostics) as **opaque `uint8_t`** with the ONE typed conversion in `src/firmware_remote_actions.h` — `lib/core`
never includes `firmware_action_effects.h`, and P1's header is not extracted or changed. Nothing in the
ruled contract (§§2.1, 2.2, 2.4, 2.5, 3, 4) changes meaning. Revision-4 SHA-256
**`4a7af9b19e723598470fa5b9b054403c63f7e88d44ffaffd7bb7aa717c31b397`** is retained unchanged in
`docs/superpowers/evidence/2026-09-15-radmin-slice7b3-reissue/brief-revision-4.md`.

This brief schedules **12 policy rows** through the committed P1 seams and keeps **36 exact rows as
retained remote refusals under R-RA-39** (§2.2), with registered, separately fenced follow-ups B395–B398. It
does not label those 36 rows complete.

**Source correction B394:** the coder enumeration says four rows have reusable no-argument effects and eight
need a small preparatory refactor. Its +80-byte comparison was explicitly for the four-row scope. The codec
QA completion summary's “12 rows preparable with existing seams” overstates that evidence. This reissue has
its own complete proposed twelve-row type model, freshly measured at +80 B (§2.1); this is neither linked RAM
nor an approved production allocation. Codec PASS/B391 closure remain intact; the historical receipt is preserved.

Revision-3 SHA-256 **`f7256bf12d4cf56a7dff23b0db8523470b27de4f31be2306555625a031d9cdd8`** is retained unchanged in
`docs/superpowers/evidence/2026-09-15-radmin-slice7b3-reissue/brief-revision-3.md`. The
[revision-4 author pre-check](2026-09-15-radmin-slice7b3-reissue-precheck.md) records the 48-row dispositions,
the +80 B layout model and its preservation; the [P1 independent gate](../evidence/2026-09-15-radmin-slice7b3-p1-qa-gate.md)
records the current baselines. Neither is relabelled as this slice's gate.

**Permitted preparation inputs (uncommitted on top of `7442e6f`):** this revision 5; the retained revision-4 copy;
the P1 QA gate evidence (`2026-09-15-radmin-slice7b3-p1-qa-gate.md` + `…-p1-qa/`); the P1 brief's §6 PASS block;
register B394/B399/B400 closures, B401 and §0; design §19.1/header; MEMORY.md; the ledger's 2026-09-16 note.
These are QA documentation, not production changes; the coder inventories them (SHA-256) as the frozen input set
per the 2026-09-15 commit ruling. The coder appends source-validation and later implementation/frozen handoff to
`docs/superpowers/evidence/2026-09-13-radmin-slice7b3.md`, naming the consumed revision and SHA.

Preserve every tracked/untracked input. Never reset, clean or commit. An isolated snapshot includes the
actual dirty implementation, not only HEAD. No QA build or mutation run overlaps coder edits in a shared tree.
Owner alone rules, commits and verifies on metal; QA authors, independently gates and lands documentation.

## 1. Binding design and exact current state

Read AGENTS/CODE_GUIDELINES, the 2026-09-07 role override, MEMORY/register §0, design §§8.7–8.10, 10–13, 15,
19/19.1; rulings R-RA-16/20/22/23/24′/27/33/34/35/36/37/38/39; the 7a activation brief and evidence; 7b-1 revision 6,
7b-2 revision 4, 7b-3-0, 7b-3-P1 and their independent QA reports (the P1 gate proves the seams below are
byte-for-byte behaviour-preserving: 156/156 pristine-vs-final transcripts). Recheck every source anchor at the
actual base (V1/V2). Every policy and allocation choice is ruled (R-RA-37/38/39/40); the P1 API below is the
committed code at `7442e6f`, not a required interface.

Design §13, verbatim:

> The target first authenticates and authorizes the command, validates any existing explicit confirmation
> token, reserves the response/transcript and deferred-action record, and performs only non-disruptive
> preparation needed to make later activation reliable. It then returns `scheduled` with the bounded
> activation delay. Erase, reboot, live retune/detach, and other reply-path-breaking effects remain deferred.
> The action is activated only after:
>
> - the `scheduled` terminal has been retained and accepted into the response transmission path; and
> - either the controller node sends the authenticated `RESPONSE_ACK` after the local-delivery condition in
>   §8.10, or a bounded fallback deadline expires.

> A valid `RESPONSE_ACK` may
>   activate earlier, but loss of that ACK never postpones the action beyond the configured promise;

The preceding excerpt is the clause inside the design's configuration bullet, with its original line break.
The controller/target distinction is pinned by the following complete bullet:

> - `remote_disruptive_outcome_deadline_ms = e2e_ack_deadline_xl_ms`
>   (`lib/core/protocol_constants.h:780`) is the controller's 300-second outer bound for retaining/reporting
>   the disruptive operation's transport outcome. Crossing it reports unknown;
>   it neither cancels an already accepted action nor extends its activation delay.

Design §11, verbatim:

> A completed authenticated transcript is immutable, including its terminal and frame count. A send-time seal or
> enqueue failure preserves its bytes and cursor; the next eligible main-loop pass retries the pending frame.
> An authenticated exact request retry instead restarts that completed transcript at sequence zero, without
> dispatching again.

R-RA-22, verbatim:

> Expiry uses **one shared earliest-deadline scan**. `TimerWheel::kCap` grows once, 91 → 92, measured as +8 bytes
> on host, ARM and Xtensa in the 0e mirror; no class receives a private timer ID. The scan visits only the bounded
> rows resident in that product profile and re-arms to the true earliest deadline.

> Candidate-layout totals are the ruling input, not permission for an unexplained
> larger production delta.

The latter is the exact end of R-RA-22's QA-confirmation paragraph. The 91→92 change already landed in Slice 5;
this slice gets no second increase. R-RA-34 authorized the 7b split and **7b-1** allocation, not any new 7b-3 total.

| Verified base seam (at `7442e6f`) | Consequence for this slice |
| --- | --- |
| `src/firmware_remote_executor.h:41/45/52/58/87–91`; `firmware_commands.cpp:1686` | Both the executor and the common command seam still refuse remote disruptive dispatch (`radmin_disruptive_refused`); `DispatchOutcome::scheduled` still maps to `internal_error` at `:52`. Replace both consistently through the prepared-action path, never an authorization bypass. |
| **P1 API — `src/firmware_action_effects.h`** (`ActionKind:12`, `ActionBackend:16`, `ActionAdmissionStatus:17`, `ActionPlan`/`ActionAdmission`, `ActionSupport:26`, `action_build_support():34` defined in the board TU at `fw_main.cpp:321`, `ActionOutcome:69`, `ActionObserver:74`) | Admission is pure and text-free after it returns: `action_reboot_admit(support)`, `action_ota_admit(support)`, `action_prep_restart_admit()`, `action_factory_reset_admit(arg,n,support)`, `action_sleep_admit(arg,n,support)`, `action_crash_admit(arg,n,debug,support)` → `{ActionPlan{kind,backend}, status ∈ ready/unsupported/confirmation/debug_disabled/usage}`. Effects are typed and sink-explicit: `action_reboot_apply(backend, Print&, observer, reset_outcome)` (`fw_main.cpp:341`), `action_ota_apply(backend, Print&, observer)` (`:369`, idempotent entry, never stop), `action_crash_apply(plan, Print& out, Print& reboot_out, observer)` (`:446`), `action_prep_restart_apply(Print&, observer)` (`:492`), `action_factory_reset_apply(backend, Print& out, Print& reboot_out, observer)` (`firmware_commands.cpp:1072`), `action_sleep_apply(kind, Print&, observer)` (`:1107`). `ActionObserver{context, report}` is call-scoped. `device_ota.h:15` `ota_start(Print&)`. **Use these; add no second parser, no raw line, no re-admission at activation.** |
| P1 local wrappers `do_reboot` `fw_main.cpp:361`, `do_ota :395`, `handle_crashtest :475`, `handle_prep_restart :518`; `handle_factory_reset` `firmware_commands.cpp:1092`, `handle_sleep :1120` | Untouched by this slice: local USB/BLE behaviour stays byte-identical (P1 gate). `do_ota` is the local toggle; the remote path calls `action_ota_apply`, never `do_ota`. |
| `src/firmware_command_authority.h`, `lib/console/console_line.h:11` | 180 policy entries, 48 marked disruptive; generated inventory has 204 rows. RPC tail cap is **201**. The policy/validator are the authorities; preparation is not a second whitelist. |
| `remote_session.h:208/213/255/293/459`, `remote_session.cpp:541/574/586` | Four headers/eight chunks; completion releases ingress/body; `remote_transcript_encode` builds the terminal plaintext from one byte at `:586` (`plain(&h.terminal, 1)`) — extend it for the five-byte scheduled body. No deferred pool exists. |
| `remote_session.cpp:1252`, `:344`, `:886` | ACK releases the transcript synchronously; install invalidates session rows; `remote_control_check` has the executing guard. Connect action identity/lifetime before destructive cleanup. |
| `node_mac_rx.cpp:2190–2218`, `:2224–2275`; `node.cpp:123/1438` | Sender distinguishes checked queued/parked/refused; cursor advances on ownership. One expiry arm (`radmin_expiry_arm`, timer id `kRadminExpiryTimerId`) scans current rows. The B391 X09 reader matches the final `radmin_expiry_arm();` at `:2275` — keep that boundary intact (D6). |
| `remote_activation.h:28/31`, `firmware_remote_activation.h:15/38`, `firmware_commands.cpp:73` | Use 7a's real live-PHY binding and effective-value resolver, including both HAL slops, the 46-byte maximal inner and impossible-PHY refusal. |
| `fw_main.cpp:507` (`g_halted = true`), `:1447` (`if (!g_halted) {`), `:1834` (`remote_executor_service_once()`), `:1878` (block end), `:1883/1888` (USB/BLE service outside) | v2 service and mesh RX/TX/timers are inside the halted block; local console/BLE stay outside. A new pending action must not starve behind a full queue or a reply-first early return; it must not move mesh service out of the halt (R-RA-38). |
| `fw_main.cpp:43–44` (**B401**) | The `// §cleanup 2026-07-14: extern decls …` comment belongs to `#include "fw_context.h"` and now trails `#include "firmware_action_effects.h"`. Comment-only fix in this slice's `fw_main.cpp` touch; rerun the fw_main source readers (D6). |
| 36 refused rows' handlers (`firmware_config.cpp`, `firmware_join_service.h`, `firmware_provisioning_service.h`, `do_regen` `firmware_commands.cpp:1032`) | Untouched by this slice (§2.2). |

Current baselines (P1 independent gate, 2026-09-16, at `7442e6f`): native **2916 / 184587 / 0 / 0 skips**;
simulator inert (identical `lus`), corpus **36/36**, s18 `32afbf11` / 269517 / 0; reference **94/94**; union floor
**53 batteries / 827 configured (826 RED + known unusable B342)**; boards **gateway 203956 RAM / 568220 flash /
285 objects, heltec_mobile 207756 / 1373576 / 329**; census 173/178/177/177/182/182; Node **230896 / 157264 /
117912**. These are the base figures this slice's gate is measured against; the coder derives its own.

## 2. Scope, preparation, ruled policy and allocation

### 2.1 B389 — complete twelve-row model: RULED R-RA-40 (owner, 2026-09-15) exactly as proposed below

The production action row does not exist. `remote_transcript_complete` releases ingress/body; ACK/invalidation
can release a transcript. No command bytes, stack Print, service pointer, raw parser buffer, open/bootstrap
scratch or old transcript may be borrowed as action state. No whole NV Blob, seed or dynamic payload is needed
for the selected twelve rows; the complex prepared transactions remain explicitly refused under §2.2.

The proposed row owns the following exact semantic information; internal enum names/order may follow the
codebase, but no field or lifetime may be dropped:

| Owned information | Representation in the measured model |
| --- | --- |
| Request/authority identity | request_id u64, admin_epoch u64, source_hash u32, controller_slot u8, authority u8 |
| Clock | activate_at_ms u64, frozen activation_ms u32 |
| Complete action argument | One u8 kind: none, reboot, prep_restart, ota, factory_reset, sleep_on, sleep_off, crash_hang, crash_fault, crash_reboot. Sleep boolean and crash mode are encoded in kind, not borrowed text. |
| Resolved hardware | One u8 backend: none, nrf_reset, esp_reset, nrf_dfu, wifi_ota, nrf_fault, esp_fault. Applicable kinds capture their backend before scheduled. |
| Ownership/eligibility | One u8 phase: none/preparing/prepared/armed/due; one u8 trigger: none/ack/deadline. Zero ID is legal; phase is presence. |

One row is **40/8** on native/ARM/Xtensa. Each of four transcript headers independently owns a u32 immutable
activation detail, growing **24/8→32/8**. Two retained u8 diagnostic fields (`last_activation_kind/outcome`)
plus final alignment are included: state **8824/8→8904/8 (+80 B)**. There are no secret-bearing payloads,
extra resident adapters or hidden sixth counter. Explicit zero-init/canonical padding and complete owned
transfer/clear at consume are required; transient apply/sink/report bindings are measured separately on stack.

QA's fresh compile-only **full Node layout model** measures:

| ABI | Current Node | Proposed Node | Change |
| --- | ---: | ---: | ---: |
| Native ACCEPT+CLIENT | 230896/8 | 230976/8 | +80 B |
| Gateway ARM ACCEPT | 157264/8 | 157344/8 | +80 B |
| Heltec mobile Xtensa CLIENT | 117912/8 | 117912/8 | 0 B |

The model uses private shadow headers; no production Node/header/pin was changed. The initial shadow compile
hit the current native static_assert; that failure is retained, followed by an explicit model-only proposed
pin. This is layout pricing, not a software implementation or linked-RAM result (P1 added no resident state:
gateway/mobile RAM unchanged at its gate). **Carrier rule (B402):** the core row's `kind` and `backend` are two
**opaque `uint8_t`** fields, as are `last_activation_kind`/`last_activation_outcome`; `lib/core` stores, clears,
compares presence and copies them, and never interprets them, never includes `src/firmware_action_effects.h`
(that header pulls in `firmware_config_parse.h`, a firmware dependency). The ONE typed conversion to and from
P1's `ActionPlan`/`ActionKind`/`ActionBackend`/`ActionOutcome` lives in `src/firmware_remote_actions.h`:
`static_assert` that each enum's underlying type is `uint8_t`, range-check both directions against the enum's
last value, and treat any out-of-range stored byte as `none` (never executed, reported as such). P1's header, enum
values and wrappers are not extracted, moved or changed (C1). `phase`/`trigger` are new core-owned fields. Before
behaviour code, the coder source-validates this complete representation against the committed P1 API; any extra
owned state returns to QA for measurement as STOP-1, not an unnoticed allocation increase.

**R-RA-40 (owner, 2026-09-15) — RULED as follows, verbatim from the recommendation:** approve one 40-byte ACCEPT-only row, four independent
u32 transcript details and the two diagnostic bytes/alignment, **+80 B total resident session state**, with
native/gateway Node re-pins **230976/157344**, measured final linked-RAM attribution and unchanged mobile Node
**117912** and RAM. Existing linked baselines from the committed P1 gate are gateway **203956 RAM /568220
flash**, mobile **207756 RAM /1373576 flash**. +80 gateway RAM is a prediction, measured at this slice's gate.
No approval is inferred from the codec commit, the four-row comparison, or general filesystem permission.

### 2.2 Exact R-RA-39 refusal list and named follow-ups

QA records these exceptions now under the owner's existing R-RA-39 authorization. The authoritative
[row-by-row disposition](../evidence/2026-09-15-radmin-slice7b3-reissue/all-48-dispositions.md) covers all
**180 policy entries /48 disruptive entries**, with **12 scheduled-scope +36 refused** and exact source
lines/aliases. Its extraction independently matches the coder's enumeration; no policy row is dropped.
Metadata alias/coarse rows are not extra independently executable commands. Source policy and the one
common grammar still decide what an actual request means.

| Refused family | Exact affected policy rows | Missing boundary / registered follow-up |
| --- | --- | --- |
| cfg set (22) | bw; cr; freq; gateway_only; host_mobiles; l1_bw; l1_cr; l1_freq; l1_layer_id; l1_node_id; l1_routing_sf; l1_sf_list; layer0_id; leaf_id; mobile; mobile_autoregister; mobile_autoregister true; n_layers; node_id; routing_sf (alias: control_sf); sf_list; tx_power | B395 / 7b-3-F-config. `firmware_config.cpp:251–631` interleaves parsing, live changes and save; existing ConfigService covers only its own fields and reloads/merges fresh NV. Need one typed delta/prepare/apply authority. |
| gateway (1) | gateway / — | B395 / 7b-3-F-config. `firmware_config.cpp:692–763` composes a deliberate pending-Blob subset inline, resolves defaults and saves. Preserve persist-only/reboot-to-apply, no implicit reboot. |
| join (2), create (7), leave (1) | join / —; join (alias: create) / —; create / —, active_fraction, ch_min_ms, dm_min_ms, duty, name, sf_list; leave / — | B396 / 7b-3-F-provision. JoinRequest is owned but apply_join revalidates/loads/saves/applies (`firmware_join_service.h:196–222`); create generates lineage and applies in one handler (`firmware_config.cpp:1180–1230`); leave rereads pending frequency then saves/applies (`:2422–2435`). |
| team (2) | team / —; team / new | B397 / 7b-3-F-team. `firmware_provisioning_service.h:755` owns project/stage/commit lifetimes; snapshot has borrowed key pointers. Keyring put reads, capacity-checks and writes (`firmware_team_keyring.h:519–556`) with no held reservation. |
| regen (1) | regen / — | B398 / 7b-3-F-regen. `firmware_commands.cpp:1031–1056` draws, saves and installs messaging identity together; no prepared identity commit. |

For every listed row and representative valid/invalid/profile/authority forms, retain typed **refused**, zero
execution/draw/write/live-change/retune/halt/reset/OTA effects and immutable retry. Existing command policy
classification stays unchanged; these exceptions are implementation support limits, not authority changes.
The full probe also preserves non-disruptive cfg/team/key forms and local USB/BLE semantics; do not refuse
an entire command family when only its disruptive rows are listed. These refused requests never occupy the
action row or become scheduled. They stay refused even while another action is busy or after capacity frees.

**Separately fenced follow-ups (registered, not implementation dispatch here):**

- **B395 / 7b-3-F-config (23 rows):** prepare a shared cfg/gateway typed delta and canonical conversion in
  firmware_config/config-parse/config-service and related carriers only as needed; local refactor first,
  then separately gated deferred support. Preserve current numeric grammar, deliberate gateway seed subset,
  pending-only versus live effects, all field mappings and unrelated fresh NV updates. Prove all 23 rows'
  local output/operation-order equivalence and later zero early mutation/frozen values/late-merge safety.
- **B396 / 7b-3-F-provision (10):** firmware_config/join-service/provisioning service and directly necessary
  pure carriers. Reuse blob_put_static_join/provision_apply_live; freeze lineage once, own full leaf/name/SF/
  duty/interval data, preserve unrelated state and save-before-live/DAD order. Refactor and deferred feature
  are separate increments; prove scratch destruction, save failure and profile refusals on actual services.
- **B397 / 7b-3-F-team (2):** firmware_provisioning_service/team-keyring and real config bindings only. Specify
  owned prepared transaction, admission/commit and capacity/membership lifetime before coding; no hidden
  reservation or stale snapshot. Prove no-change/live drift, full/corrupt stores, supplied/minted keys,
  keyring→cfg write order, full secret wiping and preserved DATA-SF/hosting state, with zero early effects.
- **B398 / 7b-3-F-regen (1):** messaging identity preparation/commit in firmware_commands and a focused pure
  identity helper using existing derivation/checked-seed idioms. Generate once; own prepared material, preserve
  current name/location and local behavior, wipe on all exits, install only at activation. Prove ordinary
  regen leaves dedicated admin/controller stores and sessions unchanged. No B312 provider fix in that slice.

Each follow-up needs its own source-validated brief, complete state/lifetime measurement and any owner
allocation ruling, full implementation/independent gate, and an explicit removal of its refusal exception.
Listing it does not authorize a broad refactor in this behavior fence or label the disruptive arc complete.

### 2.3 Prerequisites fulfilled: codec (7b-3-0) and preparation (7b-3-P1)

The selected twelve rows are reboot/prep aliases (3), OTA (1), factory-reset bare/confirm (2), sleep bare/off
(2), and crashtest bare/fault/hang/reboot (4). Bare/invalid factory/crash forms must refuse before scheduled;
"twelve rows" is coverage, not twelve unconditional success forms. **P1 is committed at `7442e6f` and independently
gated** (B394/B399/B400 closed): typed admission for all six families, typed sink-explicit effects, build-level
support (`ActionSupport`: absent reset/OTA/fault backend, power saving compiled out), local behaviour byte-identical.
This revision is the refresh R-RA-37 required after that gate. No further preparation slice precedes this feature.

The owner codec commit `ac5f9a5` allocates **RemoteTerminal::action_busy (08)** and preserves scheduled's opaque
detail. Do not edit the codec here. The P1 gate reran the extended reference (94/94) and the union (826 RED /
B342) on the current tree; those are the floor this slice extends, not this slice's measurements.

### 2.4 R-RA-37 — one promise, immutable conflict, rollover and recovery

At most **one outstanding disruptive promise per target across all ACL slots**, from reservation/preparation
until activation consumes it; exactly **one** action row. R-RA-37 replaces R-RA-22's two-row comparison for
this class. A distinct eligible supported disruptive request conflicting with that row receives retained,
immutable typed TERMINAL **action_busy (08)**. Authority/support/grammar refusals stay refused. No text is
required to identify the busy condition. That ID never later changes to scheduled; exact retries replay the
same bytes without occupying an action row. A fresh ID is required to try again.

Active preparation or an **armed/due** action reads as target-wide executing to safe/force, using R-RA-36's
existing admission code. A completed **unarmed** transcript retains same-slot safe-busy/confirmed-force-
abandon behavior. The unarmed row can persist indefinitely if its terminal never gains checked send ownership;
same-slot force or a permitted other-owner ACL invalidation can release it with its invalidated transcript.
No new timeout, cross-slot force/cancel privilege or general local-command lock. Prove operator/self/last-owner
refusals and failed/no-op ACL-write preservation. An armed row already has an activation deadline even if
its reply never airs; lost delivery/ACK cannot extend it, and force cannot cancel it.

### 2.5 R-RA-38 — prep-restart remains schedulable with documented lockout

R-RA-38 chooses scheduling, not remote refusal or a halt carve-out. Activation sets g_halted and stops mesh
RX/TX/timers/admin processing in the operating block (`fw_main.cpp:1447–1878`; the latch is set at `:507`). No remote reboot/rollover can
recover the target. Local USB/BLE service remains outside (`:1883/:1888`); local reboot where supported,
hardware reset or power-cycle can restart it. No new physical-presence authority is granted. Part 57b remains
pending software PASS/metal access; the 8a controller must display this exact warning before submission:

> prep-restart stops mesh radio and remote administration. Restart the target locally to restore access; remote reboot and rollover cannot recover it while halted.

There is no new confirmation token, no controller UI in this slice and no further owner choice on B392.

## 3. Execution contract

### 3.1 Scope and preparation

Apply the existing byte validator and command policy/authority once, preserve exact grammar/confirmation,
and reserve the mandatory transcript before any execution. For the twelve selected rows call the committed P1
admission with the request's argument tail and `action_build_support()`: `action_reboot_admit`, `action_ota_admit`,
`action_prep_restart_admit`, `action_factory_reset_admit(arg, n, support)`, `action_sleep_admit(arg, n, support)`,
`action_crash_admit(arg, n, meshroute::g_mr_trace_on, support)`. Status `ready` proceeds; `confirmation`,
`usage`, `debug_disabled` and `unsupported` each produce the retained typed **refused** (the local wrappers'
different handling of `unsupported` is local behaviour and stays). Record retained refusal for the 36 exceptions.
No call to a disruptive public handler or local wrapper merely to validate, no local/physical context substitution
and no response-text parsing.

Supported, authorized forms acquire the one action row only after admission and conflict handling are known.
Run 7a's real delay resolver; invalid/unsupported/debug-disabled/usage/confirmation forms refuse before any
promise. Freeze kind, backend, identity and delay with no reset/erase/halt/flag write/OTA start. Factory-reset
confirmation is consumed as authorization; sleep on/off and crash mode are represented by the owned kind.
Crash debug/backend admission is frozen here, never repeated after scheduled. MR_NO_POWERSAVE may retain
its current local no-op behavior; remote sleep is unsupported on such a build and refuses before scheduling.
P1's local prefix grammar must not become an incidental remote grammar rewrite; the existing authority and
byte validator still apply before that grammar.

Preparation/staging failure produces retained **internal_error** only before a truthful completed/scheduled
transcript exists; unsupported/validation/authority exceptions produce **refused**, conflict **action_busy**.
No action remains after either preparation failure. Do not increment a send-failure or exhaustion counter
for an ordinary execution refusal. Exhausted transcript capacity retains existing pacing and its accounting;
never execute without its terminal reservation. Freeze the completed response once; no different negative
terminal/plaintext for the same retained request ID/nonce after conditions change.

### 3.2 Frozen scheduled terminal

Use the existing TERMINAL domain and codec, K_session, original request ID/source/slot/epoch and final sequence.
The decoder already publishes `result_detail = payload.subspan(1)` (`remote_codec.cpp:652`); only the session
producer's one-byte terminal plaintext needs extending for scheduled detail. Do not change codec behavior for this.
Plaintext is exactly **five bytes**: `[RemoteTerminal::scheduled (0x01)][activation_ms:u32 little-endian]`.
No new opcode/version, timer value literal or extra terminal metadata. OUTPUT uses the existing capture; the
mandatory terminal/detail must be representable independently of output truncation. Do not convert a valid
scheduled promise to `output_truncated`, or send scheduled when preparation/mandatory metadata failed.

Freeze the terminal, detail, output, frame count and action meaning once. The transcript owns its delay even
after action consumption; retries after activation replay identical bytes without re-execution. ACK can free
its transcript without freeing a pending action. No detail is read from live cfg or reused action-slot memory.
Execution/staging failures are decided before completion. A subsequent seal/enqueue failure preserves the
frozen transcript and pending cursor and retries on the next eligible main-loop pass (B374/B375); an exact
request retry resets only the replay cursor to zero. No suspension latch, replacement terminal or added retry timer.

### 3.3 Clock origin, ACK and action ownership

The explicit clock origin is **the first checked ownership of that scheduled TERMINAL** by the existing
response sender: queued or parked. Reserving output, completing a transcript, accepting an earlier OUTPUT,
a raw counter, full-queue pacing, a seal refusal or a checked send refusal cannot arm it. Use the actual
`SendDispatch::Admit`, including parked with counter zero. Ownership is not an assertion that DATA aired or
that a controller received/delivered it. Lack of radio delivery remains an unknown-outcome case under §13.

Snapshot the resolved delay at preparation, and set `activate_at = remote_session_deadline(now, frozen_delay)`
once at terminal ownership. A later cfg/PHY update, send failure, exact retry, duplicate ACK or slot reuse cannot
recompute/extend it. The advertised delay is relative to this original target ownership event, not a new
countdown at each controller receipt. Unowned prepared responses cannot activate; retain their bounded action
and immutable transcript without an invented timeout that could leave a replayable false promise.

Only a codec-authenticated ACK for the original slot/epoch/request/source, after terminal ownership, authorizes
early activation. Signal action readiness before ordinary ACK transcript cleanup. An early authenticated ACK
for an unowned scheduled terminal cannot free its only promise and later trigger activation; retain it as
premature for this action, without caching an early-ACK permission. Wrong source/slot/epoch/tag, unrelated ACK,
preparation-time ACK and duplicates cannot execute. Non-disruptive ACK behavior stays unchanged.

Receive and timer paths only update bounded state/re-arm the scan. The **main-loop action service** checks due
work independently of TX capacity and reply-drain early returns, consumes the action exactly once before its
hardware call, and then activates. No dispatch, erase, fault or retune from RX/timer context. Expiry marks due
work without repeatedly re-arming an already-due row at zero; one shared scan/timer ID remains. A due action
cannot depend solely on the normal network operating block continuing after an unrelated local halt; prove
call placement and sleep wake-up against `mesh_service_once`. This action-service placement is not permission
to move mesh RX/TX/admin request processing out of `!g_halted` or undo B392's chosen prep-restart semantics. Use 64-bit time/saturating deadline arithmetic and exact-edge tests.

### 3.4 Invalidation and failure after acceptance

The listed remote `regen` row stays refused; ordinary local messaging `regen` still preserves `/mradmid`,
`/mracl` and administration sessions. Other authenticated
transcripts and their bytes/cursors/routes remain protected except already-ruled order-rank compaction.
Under R-RA-37, safe/force cannot pass while preparation is active or an armed action awaits
activation. Force may abandon a completed unarmed transcript/action in its own slot under the existing epoch-
rotation contract; another slot's work stays intact. Safe still refuses any same-slot unacknowledged transcript.
Explicit root/ACL invalidation may discard an **unarmed**, never-owned promise with its now-invalid transcript;
an **armed** action retains its original authorized, owned plan until activation even if its old response key
is invalidated. Do not borrow a live ACL role or erase it during generic session cleanup. Cold boot/power loss
clears RAM and reports no invented durable outcome; no NV operation journal is authorized.

A **deliberate local physical intervention** that actually pre-empts an armed promise (for example local reset,
power removal or a local command destroying the required runtime state) belongs to that external-interruption/
unknown-outcome class, not a scheduler lost-promise defect. This is an attribution boundary, not a new cancel
API or authority grant. Ordinary unrelated local commands do not excuse discarding work; a planned *remote*
action may not pre-empt another accepted promise. Preserve status/diagnostic honesty if the runtime survives;
a destroyed runtime cannot promise retained evidence. Add a labelled local-pre-emption fixture and distinguish
it from both an internal scheduler loss and normal metadata/config changes that leave the promise intact.

`schedule` acceptance is not a claim that flash erase, RF reattachment, RNG/hardware or OTA transfer later
succeeded. Existing truthful partial-erase warnings and backend failure handling remain effective. If a
post-acceptance hardware operation fails, retain a bounded scalar activation diagnostic and the original
immutable terminal; do not fabricate a second terminal with changed plaintext under the same nonce. Preparation
must nevertheless reject all predictable failures before promising, not use this boundary as a deferred
validation fallback. Do not rerun the public parser as local/physical authority at activation.

## 4. Consume 7a exactly; preserve surrounding contracts

Use `remote_activation_live_inputs()` and `remote_activation_resolve(g_remote_action_activation_ms, inputs)`.
Keep the persisted raw value/NV schema, setter semantics and five resolver states unchanged. Derived default
and configured valid values schedule; below-floor, above-ceiling and impossible-PHY values refuse loudly.
No clamp, fallback to the old three seconds, 30-second literal, or 300-second activation timer.

Prove the actual producer packs the five-byte scheduled terminal within 7a's **46-byte maximal inner**, including
the complete cross-layer and typed-wrapper bounds. Recompute every airtime/wait term, all three RTS attempts,
both slops and source constants independently; retain the zero-slop **7006/14012** reference solely as a test
vector. Floor equality and ceiling 299999 are valid when the live interval is possible; both adjacent invalid
edges refuse. The controller's named 300000 ms outer outcome bound remains documented/tested as a distinct
contract; product timeout/reporting/retry policy belongs to 8a/8b, not a target timer added here.

Remote OTA means **enter the available local Wi-Fi/BLE receiver**, with backend named in bounded captured OUTPUT
before the scheduled terminal. Firmware bytes do not traverse MeshRoute. Local `do_ota()` toggles an already-active
receiver off: never call it from the remote path. The remote activation calls `action_ota_apply(backend, sink,
observer)`, P1's idempotent entry (already-active returns success without stopping; `ota_start(Print&)` reports
startup through the supplied sink). A nRF BLE-DFU reset stays deferred. Keep `device_fault.h`'s hardware/ISR
ownership; fw_main remains glue — the applies it owns are already exposed by P1, so this feature adds only the
loop call, the B401 comment fix and any concrete binding there.

**ACCEPT status observability (fold-in, no additional counter):** extend the real scalar status snapshot;
never expose the action plan, arguments, secrets or a pointer to resident state. Always print
`radmin_action_phase=none|preparing|prepared|armed|due` and `radmin_action_armed=0|1`. For a present row also print
`radmin_action_request_id=<16 lowercase hex>`, `radmin_action_slot=<decimal>`, `radmin_action_kind=<stable name>`,
`radmin_action_activation_ms=<frozen decimal>` and `radmin_action_remaining_ms=<decimal|unarmed>`.
The phase determines presence (request ID zero is legal); none omits row-specific fields, preparing/prepared use
`unarmed`; armed derives remaining time from one current clock snapshot with zero at/past the deadline;
due reports zero, including early eligibility from an ACK.
The armed flag means terminal ownership has occurred, including due. Values are live-derived, not new cached
resident state; pure status reads cannot arm, consume, expire or refresh a row. Local/authenticated/open text
status share this bounded scalar view under existing access rules; CLIENT-only builds expose none of it.
Assert exact seeded values, empty/preparing/prepared/armed/due transitions and no side effects in the real-TU status probe.

Keep the five saturating ACCEPT counters and meanings. Action busy/validation refusal is an execution result,
not transcript exhaustion or a failed send attempt; no sixth counter is authorized. Seal failure increments
only seal failure, with no enqueue attempt; checked send refusal increments enqueue failure; full pacing counts
neither. A labelled synthetic fault is allowed only in a fixture when a real-Node failure is otherwise
unreachable, with a reachable positive control, full state/cursor accounting and subsequent recovery.

**Bounded activation result, included in §2.1's price:** retain only last kind and last outcome, both u8.
Cold init clears them to none; ordinary session/root/ACL invalidation preserves them like the five counters.
A failed preparation does not replace the last activation. On consumption set kind and outcome=started
before the effect; typed apply/report updates outcome while the runtime survives. Outcomes are
none/started/completed/inbox_partial/nv_partial/inbox_nv_partial/backend_failed/unexpected_return. Report
partial failures before any non-returning reset, using a call-scoped observer if needed; never parse output.
Success of a requested reset is not inferred from a return. Actual power loss/reset has no durable result
claim. Getter text is `radmin_last_activation_kind=<stable name|none>` and
`radmin_last_activation_outcome=<name>`; no retained old request ID, history, timestamp or extra counter.

Stable kind names in status/diagnostics: reboot, prep-restart, ota, factory_reset, sleep-on, sleep-off,
crash-hang, crash-fault, crash-reboot. These are scalar metadata, not echoed command lines. Remote apply passes
a non-forwarding `Print` sink (for both `out` and `reboot_out` where an apply takes two) and a call-scoped
`ActionObserver{context, report}` whose `report(context, ActionOutcome)` records the bounded outcome; raw
handler/transcript/backend text cannot escape through mrcon/BLE. P1's `ActionOutcome` names are exactly §4's list. Existing local handlers retain their exact output/warnings. Hardware failures remain observable via
these bounded outcomes and the permitted scalar diagnostic below. No raw output fallback after completion.

## 5. Implementation fence

This is the **feature after P1**, not permission to extract existing parsers/services while adding scheduling.
Predict the changed paths/counts before editing. No file move, unrelated cleanup or simulator edit.

- Core: `lib/core/remote_session.{h,cpp}`, `node.{h,cpp}`, `node_mac_rx.cpp`; one bounded action record
  (identity, clock, opaque `kind`/`backend` bytes, `phase`, `trigger`) plus the two opaque diagnostic bytes,
  independent transcript detail, ACK/install/expiry/checked-send ownership. Core must not include
  `firmware_action_effects.h` or any firmware/NV header, and never executes handlers; the typed conversion is
  firmware's (§2.1 carrier rule). No pure-header extraction of P1's enums in this slice.
- Firmware: `src/firmware_remote_executor.h`, `firmware_commands.{h,cpp}`, focused new
  `firmware_remote_actions.{h,cpp}`, with `fw_context.h`/`fw_main.cpp` limited to the main-loop action-service
  call/order, any concrete binding, and the **B401 comment-only fix** (`fw_main.cpp:43–44`). Reuse the committed
  P1 API in `src/firmware_action_effects.h` as is; do not change it, its local wrappers, or their behaviour; do not
  reparse at activation. No firmware_config/provisioning/keyring/identity-service or device_ota edits in this feature.
  Add a new TU to the actual three base build_src_filter lists in platformio.ini (`:123/:218/:398`) if needed.
- Native: extend existing session/transcript/executor/Node/7a tests; `test_remote_actions.cpp` if useful.
  Real codec/session/Node paths, no production fault hooks. ABI/Node pins exactly per R-RA-40 (230976 native / 157344 gateway; mobile 117912)
  and measured final layout/link attribution. Price transient transfer/observer stack as well as state.
- Instruments: all dependency/source mutation additions, actual measured ABI entries, existing six probes
  and the committed `tools/probe_deferred_actions/` (extend it for activation through the real applies; keep its
  four positive pins and 19 controls effective). Extend the real firmware command/action path under hardware/store fakes;
  compile actual owners and controlled board-function extracts, never copied handlers. Keep B391's X09
  effective after changed expiry/ACK code. Full tools discovery and D5/D6 source-reader checks are mandatory.
- Coder's append-only receipt and inventoried artifacts. QA owns maintained register/design/rulings/MEMORY/
  bench/brief updates. Regenerated inventory may move anchors, not its 204-row authority/command semantics.

OUT: codec/reference/domain/wire/NV-format changes, simulator/anchors, new timer IDs, unapproved resident
state, the 36 refused families' implementation, new command authority/DM/ACL rules, controller/confirmation/
ACK/custody UI, legacy cleanup and unrelated B312/B315/B342/B350/B359/B364 fixes. Existing factory-erase calls
are the selected effect, not permission to modify NV schema/erase semantics. CLIENT-only Node/RAM is unchanged
and gains no remote action service; P1's existing local behavior remains available in its original profiles.

## 6. Required discriminating proofs

| Surface | Evidence required from actual production decisions |
| --- | --- |
| Coverage/preparation | Map all 48 disruptive policy rows to the 12 prepared-scope decisions or 36 explicit retained refusals; valid, invalid/confirmation, unsupported/profile and authority arms. Assert zero disruptive effects before scheduling. Local USB/BLE grammar and behavior regressions are controlled. |
| Complete ownership | Overwrite RX/body/parser/stack scratch after preparation; release ingress, ACK transcript, reuse a different transcript/action slot; activation still uses the original complete plan. No secret payload in this selected scope; clear owned metadata/transfer on abort/consume. No local/BLE plaintext leakage. |
| Reservation and conflict | No terminal/action capacity ⇒ zero execution; approved typed action_busy replays unchanged after capacity returns; never parse text to identify conflict. Drive two credentials and two different disruptive commands; assert R-RA-37, exact retries and no promise lost to reset/halt. |
| Terminal encoding | Real producer/codec: exact five-byte body, correct LE delay, output order/last sequence, all return carriers, delay at both edges, immutable retry ciphertext/detail before and after activation; other non-scheduled terminals retain old shape. |
| Ownership/send faults | No arming on OUTPUT, capture, full queue, seal/send refusal. Checked queued and parked-zero ownership arm once. Pending frame retries next eligible pass; exact request retry resets replay only. Real Node synthetic seal fault labelled and recovery verified. |
| ACK/clock | Before-owned ACK cannot consume a pending scheduled promise; after-owned valid ACK makes action eligible, actual hardware call only on next main-loop service. Duplicate/wrong identity/tampered ACK does nothing. Lost ACK: deadline−1 no action, deadline exactly one action, later none; wrap/saturation and delayed-loop cases. |
| Configuration/lifetime | Raw=0 follows live default; valid configured uses that value; all three invalid resolver states refuse. Change raw/PHY after scheduling: frozen detail/deadline unchanged. ACK and ordinary invalidation do not erase armed work; safe/force guard armed work; same-slot force or permitted other-owner ACL invalidation releases unarmed work; operator/self/last-owner and failed/no-op write controls; boot/power-loss clears without a fabricated result. |
| Real handlers | Real `firmware_commands.cpp`, the action owner and the committed P1 admits/applies execute under host fakes (extend `tools/probe_deferred_actions/`). Assert all 12 supported/invalid/profile/authority paths, exact confirm/sleep/crash grammar, frozen debug/backend decision, NV/halt/reset/OTA operation order and partial failures. The 36 refused rows must produce zero backend/NV/identity effects, with non-disruptive/local forms preserved. Fake executors alone cannot prove this row. |
| Lockout/pre-emption | R-RA-38 remote prep behavior, stopped mesh RX with local service retained; distinguish external local physical pre-emption/unknown outcome from an internal missed activation. No new cross-slot force or halt carve-out. |
| OTA | ESP inactive→entry; active never→stop from remote; nRF DFU deferred; unsupported refuses. Exact backend OUTPUT retained, no firmware body sent; local toggle regression control. |
| Main-loop/timer | Due action runs despite full TX/continually pending output and does not starve inside `!g_halted`; exactly one consumed action per service call. Real production owner runs in probe; existing fw_main extractor/structural controls must prove actual call and placement, not masquerade as compiling all fw_main. Wheel remains 92/id91, earliest bounded scan/no zero-delay livelock and sleep wake are tested. |
| Counter/compatibility | Preserve all five counters and saturation; new ACCEPT scalar action-status fields exactly match the same live snapshot without mutating it; no mobile remote-action-service residency/symbols; all previous B374–B388 runtime/ownership controls remain effective. B391 X09 stays effective; all last-result diagnostic states are truthful and non-mutating on reads. |

Each new decision needs an effective mutation or executed real-TU probe control: premature action/ACK, missed
or repeated activation, deadline refresh, missing owned copy, mutable delay/terminal, wrong result mapping,
call-before-ownership, wrong slot/epoch, bypassed confirmation/authority, lost accepted action, OTA toggle and
hardware/global-sink escape. A source string match or fake executor is not a real-handler wiring gate.

## 7. Full implementation gate and frozen handoff

The coder runs this whole chain, then QA independently reruns every required instrument on the complete
frozen implementation. The committed codec and P1 gates are prerequisites, not replacement measurements.

1. Fresh base/final `pio test -e native` **and `./.pio/build/native/program`**. Derive cases/assertions/failures/
   skips; filtered per-case XML is arithmetic only, never a whole-suite substitute over B364's boundary.
2. Fresh simulator normal/gateway compile/link provenance (B385); all **36** current BASELINE anchors,
   validate both manifests and compare actual base/final bytes. Read the live s18 anchor. If differing lus
   hashes make canonical comparison refuse, retain the refusal and independently compare validated streams;
   never rewrite manifests. No re-anchor or remote corpus movement is authorized.
3. Both ABI probes with controls; six standing probes (console-sink, inbox-verbs, firmware-UI, custody-USB,
   BLE-line, features), controlled default and --no-neg, plus explicit inbox CLIENT arm. Include the committed
   `tools/probe_deferred_actions/run.sh` (default and --no-neg), extended for activation. Set MR_LUS_SRC
   explicitly for private snapshots. Disclose B350's wording.
4. Full tools unittest discovery with a measured real ELF under the private .pio-measure tree so no hidden
   skip; inventory write/bare/check; authority checker plus six selftests; A0; DataType literals; both repos'
   whitespace/source integrity. Run `2026-09-13-radmin-slice7b3-0-reference.py --freeze-check --compare
   test/test_remote_codec.cpp --selftest` in the independent PyNaCl environment: **94 arrays**, old 89
   unchanged, five comparator controls; retain separately executed one-byte corruption refusal. No expected
   vector edits. This is the committed extension, not the obsolete 89-only whole-file command.
5. Deterministic base/final **gateway then heltec_mobile**, sequentially, same fixed identity/private paths,
   under that checkout's .pio-measure/. Hash pristine ELFs/payloads; attribute Node/RAM/flash/sections/objects/
   symbols and transient stack. Only this normal board pair; also run the census's own **six pinned envs**.
   No new warnings or -Wreorder suppression; production allocation must match the actual owner ruling.
6. Derive **S**, every changed configured TARGET_SRC battery, and **H**, complete historical/dependency
   acceptance; gate **S ∪ H**. The current floor is the P1 gate's **53 batteries / 827 configured** (826 RED +
   known unusable B342), including `actionadmit` and the repaired X09. Names/counts are in
   `docs/superpowers/evidence/2026-09-15-radmin-slice7b3-p1-qa/union/summary.json`. Include every new behaviour
   dependency/control, even if its TARGET_SRC did not change. Derive final counts again, audit
   every pattern and run every selected battery; record actual clean worker baselines, assertion RED versus
   compile failures, no-match/green/unusable outcomes and restored source hashes. Historical totals are not
   a current run; a subset is not this union. Only sliceBmac M04/B342 remains the named unusable exception.

Required exact report line: **`PIN re-synced? YES — <independently derived base + additions = final>`**.
Strict-reader declarations stay bare; derivations precede them (D5). Comment/moved-source readers require
D6 audit and affected controls. Preserve every failed attempt and corrected rerun; do not silently repair
unrelated instruments. B312/B315/B350/B359/B364 remain separate limits. A fake board reset is not metal proof.

The frozen report names HEAD/brief hash, all tracked/untracked inputs, permitted preparation documents,
all new instruments, exact commands/output, both mutation selectors, allocation and linker attribution,
48-row disposition coverage, P1 dependency, known limits and freeze. QA builds/mutations cannot overlap coder
edits in the same tree. No shared code fix by QA, commit, or coder recommendation substitutes for independent
PASS/HOLD. No full implementation gate has run for this author reissue.

## 8. Predictions, STOP conditions and documentation landing

Predict 36 byte-identical streams, unchanged wire/NV/authority semantics and mobile Node/RAM; ACCEPT-only
state grows exactly per R-RA-40 (+80 B); any extra owned state is STOP-1 for measurement. Fresh current Node baselines are native 230896/8, gateway
157264/8, mobile 117912/8. The ruled allocation (R-RA-40) is native 230976/8, gateway 157344/8, mobile unchanged;
+80 B is approved, and its linked attribution (predicted +80 gateway RAM inside `g_node`, mobile 0) is measured
at this gate against the P1 baselines (gateway 203956/568220, mobile 207756/1373576). Do not carry the older
+280/+528 raw-line or four-row +80 comparison forward as a pin. Final production builds establish the actual
layout and linked/stack attribution.

Retained 7b-2 STOP text, verbatim:

> Any stream delta — STOP.

> A second dispatch of
> one request; a transcript evicted by time or by a newer request; a disruptive handler CALLED under a remote context;
> transcript bytes reaching `mrcon`/BLE; a raw counter reported as admission; a frame sent past a full TX queue —
> STOP.

**Explicit 7b-3 supersession:** the old blanket disruptive-handler prohibition is replaced only by this slice's
authenticated preparation → owned scheduled terminal → ACK/deadline → main-loop activation contract. It is
still forbidden to call a disruptive effect in ordinary remote dispatch, RX or timer context. A typed deferred
apply is one activation of the already-admitted request, not permission for a second public dispatcher call.
All other quoted prohibitions remain. Also STOP on any allocation beyond R-RA-40; a base other than `7442e6f`
plus the listed QA documentation; any change to P1's API, wrappers or local behaviour; an unlisted family omission; stale/concurrent
inputs; predictable validation postponed until after scheduled; new nonce plaintext for an old terminal;
borrowed action/detail lifetime; internally canceled/extended armed promises (external local physical
pre-emption is the explicit unknown-outcome exception above); new timers; mobile Node/RAM growth;
production codec/NV-schema/controller work; or another unusable/vacuous instrument. B342 stays separately named.

After independent PASS, QA closes findings in place and lands the measured allocation, timer/ACK/retry/lifetime
contracts in design/MEMORY/tracker, preserving the original proposals/rulings. Protocol/frame/manual replacement
remains Slice 9; an intervening documentation claim must still be accurate. Owner commits and verifies on metal.

**Part 57b is ruled but pending implementation/metal, not a runnable or passed gate now.** On software PASS QA adds only irreducible hardware checks:
real reboot/halt/DFU/OTA/reset/wipe effects and any newly grown hardware-only stack path. Require a scalar-only
scheduled diagnostic before activation with exact format
`> remote-action scheduled request_id=<16 lowercase hex> activation_ms=<decimal> action=<stable action name>`
and activation diagnostic
`> remote-action activate request_id=<same> trigger=<ack|deadline>`.
These are local metadata observations, never copies of command/output/secrets and never proof of RF delivery.
OTA backend is captured as exactly `> ota backend=wifi\n` or `> ota backend=ble-dfu\n` before scheduled;
these are backend availability labels, not claims that startup/upload succeeded. Stable action names are
listed in §4. When a typed effect reports a result, its permitted scalar local line is
`> remote-action result request_id=<same> outcome=<name>`, using only §4 outcomes. These bounded hardware
observations do not permit raw effect/transcript output to escape; local handlers keep their own exact warnings. Actual controller ACK/lost-ACK/RF round trips remain
8b's controller/carrier metal gate; a labelled host/controller fixture must not be described as that product gate.

Under R-RA-38, Part 57b must observe the scheduled/activate diagnostics for
`action=prep-restart`, the actual halt and loss of mesh remote access, followed by successful local restart.
The existing local success line is
`> prep-restart — routes + inbox cleared, network membership KEPT, node HALTED. Power-cycle the fleet to restart clean.`
It is a local handler/hardware reference, not permission to leak the remote transcript to USB. Preserve the
existing partial-erase warnings. Local `reboot` prints `> rebooting`; USB/BLE local service and hardware reset/
power-cycle are distinct recovery mechanisms. Do not present the lockout observation as an automated gate or
promise a remote recovery action after the halt. 8a inherits §2.5's ruled pre-submission warning. B395–B398 stay open for the refused rows; no full disruptive-arc completion is claimed.
