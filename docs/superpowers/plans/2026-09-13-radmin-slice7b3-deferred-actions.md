<!-- QA/Author: OpenAI Codex; production coder: separate Codex session -->
# Remote-admin v2 Slice 7b-3 — deferred actions and scheduled terminals

**Revision 4 — 2026-09-15: REISSUED AT OWNER CODEC COMMIT; READY FOR CODER SOURCE-VALIDATION.
BEHAVIOR IMPLEMENTATION HOLD: B389 allocation and B394 shared-effect preparation.**
Base **`ac5f9a592065d08e7cc8c06ef395e79891d41b34`**, the owner's separate 7b-3-0 codec commit.
Simulator **`06746a97de5764415d6fcef10b97bca90569b9c7`**, clean/unchanged. Both repositories were clean
before this authoring pass. R-RA-37's codec prerequisite is fulfilled; **B391 is closed** by the committed
independent QA gate. R-RA-37/38/39 are settled, not renewed owner choices.

This reissue selects **12 policy rows for scheduling after preparation**, and records **36 exact rows as
retained remote refusals under R-RA-39**, with registered, separately fenced follow-ups. It does not label
those 36 families/rows complete. The 12 require [7b-3-P1](2026-09-15-radmin-slice7b3-p1-simple-action-preparation.md)
first: existing grammar/effect/sink seams must be separated under C1, independently gated and owner-committed.
QA will then refresh this behavior brief's base and actual APIs at the P1 hash before behavior implementation.
The codec commit satisfies its own sequencing ruling; it does not make the missing preparation disappear.

**Source correction B394:** the coder enumeration says four rows have reusable no-argument effects and eight
need a small preparatory refactor. Its +80-byte comparison was explicitly for the four-row scope. The codec
QA completion summary's “12 rows preparable with existing seams” overstates that evidence. This reissue has
its own complete proposed twelve-row type model, freshly measured at +80 B (§2.1); this is neither linked RAM
nor an approved production allocation. Codec PASS/B391 closure remain intact; the historical receipt is preserved.

Revision-3 SHA-256 **`f7256bf12d4cf56a7dff23b0db8523470b27de4f31be2306555625a031d9cdd8`** is retained unchanged in
`docs/superpowers/evidence/2026-09-15-radmin-slice7b3-reissue/brief-revision-3.md`.
The [new author pre-check](2026-09-15-radmin-slice7b3-reissue-precheck.md) records fresh instruments, exact
48-row dispositions, source equality, model compiler commands and preservation. The older September-13
pre-check and codec QA report remain their own historical evidence; their runs are not relabelled as this gate.

**Permitted preparation inputs:** this reissue; the P1 brief; the new pre-check/evidence directory; register
B389–B398/current dispatch; current design/MEMORY/ledger completion alignment; codec brief commit-status note; and Part 57b's ruled lockout
reservation. The design edit, explicit R-RA-39 list and follow-up fences are intended inputs, not unexpected
production changes. QA leaves coder/other-reviewer reports untouched. The coder appends source-validation
and later implementation/frozen handoff to `docs/superpowers/evidence/2026-09-13-radmin-slice7b3.md`, naming
the consumed revision and SHA. P1 has its own receipt/gate and adds no behavior implementation permission.

Preserve every tracked/untracked input. Never reset, clean or commit. An isolated snapshot includes the
actual dirty implementation, not only HEAD. No QA build or mutation run overlaps coder edits in a shared tree.
Owner alone rules, commits and verifies on metal; QA authors, independently gates and lands documentation.

## 1. Binding design and exact current state

Read AGENTS/CODE_GUIDELINES, the 2026-09-07 role override, MEMORY/register §0, design §§8.7–8.10, 10–13, 15,
19/19.1; rulings R-RA-16/20/22/23/24′/27/33/34/35/36/37/38/39; the 7a activation brief and evidence; 7b-1 revision 6,
7b-2 revision 4, 7b-3-0 and their independent QA reports, including B391 closure and the codec byte-level
ELF limitation. Recheck every source anchor at the actual base (V1/V2). §2.1 requests only B389 allocation;
the other policy choices are ruled. The future P1 APIs below are required interfaces, not claims of existing code.

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

| Verified base seam | Consequence for this slice |
| --- | --- |
| `src/firmware_remote_executor.h:41/45/58`, `firmware_commands.cpp:1644` | Both the executor and real command seam refuse remote disruptive dispatch; `scheduled` currently maps to `internal_error`. Replace both consistently through a prepared-action path, never an authorization bypass. |
| `src/firmware_command_authority.h`, `lib/console/console_line.h:11` | 180 policy entries, 48 marked disruptive; generated inventory has 204 rows across its surfaces. RPC tail cap is **201**, not withdrawn 159. The policy/validator are the authorities; preparation is not a second whitelist. |
| `remote_session.h:208/213/255`, `remote_session.cpp:541/574/612` | Four headers/eight chunks; completion releases ingress/body; terminal producer currently encodes one byte. No deferred pool exists. Completed transcript cannot borrow future action memory for its immutable detail. |
| `remote_session.cpp:1252`, `:344`, `:886` | ACK releases transcript synchronously; install invalidates session rows; rollover has an executing guard. Connect action identity/lifetime before destructive cleanup. |
| `node_mac_rx.cpp:2190–2213`, `:2224–2275` | Sender distinguishes checked queued/parked/refused; cursor advances on ownership. Single expiry arm scans current rows. Neither raw counter nor queue capacity proves ownership. The committed B391 repair preserves the X09 reader of the final call. |
| `remote_activation.h:14–32`, `firmware_remote_activation.h:8–26`, `firmware_commands.cpp:72` | Use 7a's real live-PHY binding and effective-value resolver, including both HAL slops, maximal terminal length and impossible-PHY refusal. |
| `fw_main.cpp:320/335/395/423/1372/1759/1799` | Reset, OTA, crash and halt happen synchronously; v2 service is inside `!g_halted`; legacy action uses unrelated globals. A new pending action must not starve behind a halt/full queue or a reply-first early return. |
| `firmware_commands.cpp:1031/1071/1096`; `firmware_config.cpp:251/692/902/1180/2025/2422` | Real identity, reset, sleep, cfg, gateway, join/create/team/leave handlers mix validation and effects. Preparation must reuse their parsing/services and expose typed validity, not infer success from console text. |

Fresh revision-4 author baseline: **2912 cases /184461 assertions /0 failures /0 skips**; fresh Release/Ninja
simulator, **64 actual compiler actions** across both namespaces; all **36 anchors/streams byte-identical**
to the validated codec final corpus. Extended reference **94/94**, old 89 unchanged, five comparator controls.
Current source audit: **52 batteries/817 configured patterns, all match once**, not an executed mutation union.
Three-ABI compile-only models are below. No full implementation gate or linked board-RAM measurement this turn.

## 2. Scope, preparation, ruled policy and allocation

### 2.1 B389 — complete twelve-row proposal: OWNER ALLOCATION RULING STILL REQUIRED

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
pin. This is layout pricing, not a software implementation or linked-RAM result. No P1 resident growth is
allowed. Before behavior code, the coder source-validates this complete representation against the gated P1
APIs; any extra owned state returns to QA for measurement, not an unnoticed allocation increase.

**Concrete recommendation for the owner's B389 ruling:** approve one 40-byte ACCEPT-only row, four independent
u32 transcript details and the two diagnostic bytes/alignment, **+80 B total resident session state**, with
native/gateway Node re-pins **230976/157344**, measured final linked-RAM attribution and unchanged mobile Node
**117912** and RAM. Existing linked baselines from the committed codec gate are gateway **203956 RAM /570588
flash**, mobile **207756 RAM /1372992 flash**. +80 gateway RAM is a prediction, not today's measurement.
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

### 2.3 B394 preparation; fulfilled codec/B391 prerequisites

The selected twelve rows are reboot/prep aliases (3), OTA (1), factory-reset bare/confirm (2), sleep bare/off
(2), and crashtest bare/fault/hang/reboot (4). Bare/invalid factory/crash forms must refuse before scheduled;
“twelve rows” is coverage, not twelve unconditional success forms. Four have reusable effects; eight need
shared parse/admission/apply extraction. All twelve need explicit typed outcomes and bounded sink/backend
bindings. **7b-3-P1** supplies these under C1, with local behavior unchanged and all remote disruptive guards
still refusing. It gets its own gate/owner commit before this feature, then QA refreshes this brief at that hash.
No implicit narrowing to only four rows and no hidden effect/parser refactor in 7b-3.

The owner codec commit ac5f9a5 already allocates **RemoteTerminal::action_busy (08)** and preserves scheduled's
opaque detail. Do not edit the codec again here. Its reported independent gate is **2912/184461/0**, reference
94/94, union **52 batteries /816 RED /known unusable B342 /817 configured**, with B391 X09 executed RED/one
match/one failed assertion. Four frozen codec/test/tool hashes match this commit. This author pass repeats
native/reference/corpus and cardinality, not that full gate. Preserve the independent report's limitation:
QA reproduced linked totals, not the coder's one-byte base/final gateway ELF attribution.

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
RX/TX/timers/admin processing in the operating block (`fw_main.cpp:1372–1803`). No remote reboot/rollover can
recover the target. Local USB/BLE service remains outside (`:1808/:1814`); local reboot where supported,
hardware reset or power-cycle can restart it. No new physical-presence authority is granted. Part 57b remains
pending software PASS/metal access; the 8a controller must display this exact warning before submission:

> prep-restart stops mesh radio and remote administration. Restart the target locally to restore access; remote reboot and rollover cannot recover it while halted.

There is no new confirmation token, no controller UI in this slice and no further owner choice on B392.

## 3. Execution contract

### 3.1 Scope and preparation

Apply the existing byte validator and command policy/authority once, preserve exact grammar/confirmation,
and reserve the mandatory transcript before any execution. Use P1's shared typed admission/argument seam
for the twelve selected rows; record retained refusal for the 36 exceptions. No call to a disruptive public
handler merely to validate, no local/physical context substitution and no response-text parsing.

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
The decoder already publishes `result_detail = payload.subspan(1)` (`remote_codec.cpp:654`); only the session
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
before the scheduled terminal. Firmware bytes do not traverse MeshRoute. Current ESP `do_ota()` toggles an
already-active receiver off: do not call that toggle as remote entry. Use an explicit ensure-entry path with
truthful already-active/unsupported handling; preserve local toggle semantics. A nRF BLE-DFU reset stays deferred.
Keep `device_fault.h`'s hardware/ISR ownership; fw_main remains glue, not the new feature implementation.

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
crash-hang, crash-fault, crash-reboot. These are scalar metadata, not echoed command lines. Remote apply uses
P1's non-forwarding effect sink and typed observer; raw handler/transcript/backend text cannot escape through
mrcon/BLE. Existing local handlers retain their exact output/warnings. Hardware failures remain observable via
these bounded outcomes and the permitted scalar diagnostic below. No raw output fallback after completion.

## 5. Implementation fence

This is the **feature after P1**, not permission to extract existing parsers/services while adding scheduling.
Predict the changed paths/counts before editing. No file move, unrelated cleanup or simulator edit.

- Core: `lib/core/remote_session.{h,cpp}`, `node.{h,cpp}`, `node_mac_rx.cpp`; one bounded action record,
  independent transcript detail, ACK/install/expiry/checked-send ownership. A pure feature-local header may
  hold the shared carrier if named in preflight; core must not include firmware/NV or execute handlers.
- Firmware: `src/firmware_remote_executor.h`, `firmware_commands.{h,cpp}`, focused new
  `firmware_remote_actions.{h,cpp}`, with `fw_context.h`/`fw_main.cpp` limited to concrete P1 bindings and
  loop calls/order. Reuse the gated P1 APIs; do not reparse at activation or change their local behavior.
  No firmware_config/provisioning/keyring/identity-service or device_ota implementation edits in this feature.
  Add a new TU to the actual three base build_src_filter lists in platformio.ini if needed; no profile change.
- Native: extend existing session/transcript/executor/Node/7a tests; `test_remote_actions.cpp` if useful.
  Real codec/session/Node paths, no production fault hooks. ABI/Node pins only after B389's explicit ruling
  and measured final layout/link attribution. Price transient transfer/observer stack as well as state.
- Instruments: all dependency/source mutation additions, actual measured ABI entries, existing six probes
  and P1's real-effect probe. Extend the real firmware command/action path under hardware/store fakes;
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
| Real handlers | Real `firmware_commands.cpp`, action owner and gated P1 effect decisions execute under host fakes. Assert all 12 supported/invalid/profile/authority paths, exact confirm/sleep/crash grammar, frozen debug/backend decision, NV/halt/reset/OTA operation order and partial failures. The 36 refused rows must produce zero backend/NV/identity effects, with non-disruptive/local forms preserved. Fake executors alone cannot prove this row. |
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
frozen implementation. The committed codec and future P1 gate are prerequisites, not replacement measurements.

1. Fresh base/final `pio test -e native` **and `./.pio/build/native/program`**. Derive cases/assertions/failures/
   skips; filtered per-case XML is arithmetic only, never a whole-suite substitute over B364's boundary.
2. Fresh simulator normal/gateway compile/link provenance (B385); all **36** current BASELINE anchors,
   validate both manifests and compare actual base/final bytes. Read the live s18 anchor. If differing lus
   hashes make canonical comparison refuse, retain the refusal and independently compare validated streams;
   never rewrite manifests. No re-anchor or remote corpus movement is authorized.
3. Both ABI probes with controls; six standing probes (console-sink, inbox-verbs, firmware-UI, custody-USB,
   BLE-line, features), controlled default and --no-neg, plus explicit inbox CLIENT arm. Include P1/new
   action probe default/--no-neg. Set MR_LUS_SRC explicitly for private snapshots. Disclose B350's wording.
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
   acceptance; gate **S ∪ H**. The codec floor is **52 batteries/817 configured**, reported independent
   **816 RED +known unusable B342**, now including the repaired X09. Names/counts are in this author
   pre-check's mutation-floor.json and the committed codec QA union summary. Include every new P1 and
   behavior dependency/control, even if its TARGET_SRC did not change. Derive final counts again, audit
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
state grows only under the final B389 ruling. Fresh current Node baselines are native 230896/8, gateway
157264/8, mobile 117912/8. The complete author model is native 230976/8, gateway 157344/8, mobile unchanged;
+80 B state is proposed, not approved or linked. §2.1 supplies the exact ruling request and measured scope.
Do not carry the older +280/+528 raw-line or four-row +80 comparison forward as a final allocation pin.
P1 and final production builds must establish actual API/layout and linked/stack attribution.

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
All other quoted prohibitions remain. Also STOP on missing B389 allocation or gated P1 preparation; a missing
base reissue after P1; an unlisted family omission; stale/concurrent
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
