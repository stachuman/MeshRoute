<!-- QA/Author: OpenAI Codex, replacing Claude; production coder: separate Codex session -->
# Remote-admin v2 Slice 7b-3 — deferred actions and scheduled terminals — 2026-09-13

**Revision 3 (2026-09-13, second reader): OWNER RULINGS R-RA-37/38/39 RECORDED — READY FOR CODER SOURCE-VALIDATION; IMPLEMENTATION HOLD narrows to B389 allocation, the 7b-3-0 codec prerequisite and B391.**
**Rulings applied to this text without rewriting it:** §2.2 is RULED as written (R-RA-37: one promise per target, ONE row, typed `action_busy` `0x08` via a separate 7b-3-0 codec slice — QA authors that brief next; this brief is reissued at the 7b-3-0 commit). §2.4 is RULED option (a) (R-RA-38: `prep-restart` stays schedulable; lockout documented, Part 57b + 8a warning). §2.1's optional scope fallback is PRE-AUTHORIZED (R-RA-39) under its listed conditions; the B389 row size / Node re-pins still await the coder's typed-plan enumeration. Where the revision-2 text below still says "proposal", "recommendation" or "owner call" for these three items, read it as ruled; ledger: `2026-09-03-remote-admin-v2-rulings.md` R-RA-37..39. Revision-2 status line, retained as history:
**Revision 2: SECOND-READ PASS WITH FOLD-INS APPLIED — READY FOR CODER SOURCE-VALIDATION; IMPLEMENTATION HOLD B389/B390/B391/B392.**
Base **`f993191be7f6980870f440f6032bca72278539a7`**, the owner's 7b-2/B388 closure commit. Simulator
**`06746a97de5764415d6fcef10b97bca90569b9c7`**, clean, unchanged. Both repositories were clean at this
pre-check's start. This is a complete proposed behavior/fence and validation contract, not a production
implementation authorization: **B389** needs a complete prepared-state budget and owner allocation ruling;
**B390** needs the owner's conflicting-action admission choice. **B391** is a reproduced gate-pattern defect
at the closure commit; its narrow instrument repair needs no owner policy decision, but must be verified.
**B392** additionally requires the owner's prep-restart lockout choice before dispatch. The second read's
PASS is for coder source-validation. Its recommendations for one row, typed conflict result, optional family
deferral and prep-restart are recorded below as proposals, not owner confirmations. The coder first returns
source-validation and complete candidate measurements; QA records actual owner decisions before dispatch.
No silent narrowing to only reboot/prep or assumption that access permission settled a design choice.

Revision 1 SHA-256 was `4c99a305fb44e6bbc05630eab14a1ba4acc497f96b45877edb56681f8f77243a`; it is retained
with the second-read input and revision-2 evidence in the pre-check directory. Revision 2 incorporates the
source-backed codec detail, explicit recovery boundaries, scalar status and physical-pre-emption fold-ins.
B390's revised recommendation is one row plus a separately prepared typed `action_busy` result; B389 still
awaits the coder's actual typed-plan enumeration. No owner ruling is created by this revision.

**Preparation inputs:** this brief; [author pre-check](2026-09-13-radmin-slice7b3-precheck.md) and
`docs/superpowers/evidence/2026-09-13-radmin-slice7b3-precheck/`; the register's dispatch/B389–B392 rows (including the preserved second-reader B392 edit);
MEMORY's current dispatch; design implementation header and §19.1 rows; 7b-2 brief's commit/erratum note;
7b-2 independent QA report §9; AGENTS D6 and its CODE_GUIDELINES detail; and the conditional Part 57b reservation
in `docs/2026-07-31-bench-test-script.md`. Revision-2 artifacts are nested under the same pre-check directory.
These are QA documentation/evidence
changes. The old coder receipt is preserved, not edited by QA. Coder creates
`docs/superpowers/evidence/2026-09-13-radmin-slice7b3.md`, identifies this brief's actual SHA-256, inventories
all preparation inputs, and appends preflight, implementation, failures and frozen handoff in order.

Preserve every tracked/untracked input. Never reset, clean or commit. An isolated snapshot must include the
actual dirty implementation, not just HEAD. No QA build or mutation run overlaps coder edits in a shared tree.
Owner alone rules, commits and verifies on metal; QA authors, independently gates and lands documentation.

## 1. Binding design and exact current state

Read AGENTS/CODE_GUIDELINES, the 2026-09-07 role override, MEMORY/register §0, design §§8.7–8.10, 10–13, 15,
19/19.1; rulings R-RA-16/20/22/23/24′/27/33/34/35/36; the 7a activation brief and evidence; 7b-1 revision 6,
7b-2 revision 4 and their independent QA reports, including the new B391 erratum. Recheck every source anchor
below at the actual base before acting (V1/V2). Existing owner rulings remain binding; §2 proposals are not rulings.

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
| `node_mac_rx.cpp:2190–2213`, `:2224–2275` | Sender distinguishes checked queued/parked/refused; cursor advances on ownership. Single expiry arm scans current rows. Neither raw counter nor queue capacity proves ownership. B391 affects the X09 reader of the final call. |
| `remote_activation.h:14–32`, `firmware_remote_activation.h:8–26`, `firmware_commands.cpp:72` | Use 7a's real live-PHY binding and effective-value resolver, including both HAL slops, maximal terminal length and impossible-PHY refusal. |
| `fw_main.cpp:320/335/395/423/1372/1759/1799` | Reset, OTA, crash and halt happen synchronously; v2 service is inside `!g_halted`; legacy action uses unrelated globals. A new pending action must not starve behind a halt/full queue or a reply-first early return. |
| `firmware_commands.cpp:1031/1071/1096`; `firmware_config.cpp:251/692/902/1180/2025/2422` | Real identity, reset, sleep, cfg, gateway, join/create/team/leave handlers mix validation and effects. Preparation must reuse their parsing/services and expose typed validity, not infer success from console text. |

Revision-1 author checks (not rerun for these documentation fold-ins): native **2909 cases /174485 assertions /0 failures /0 skips**; a fresh Release simulator
build executes 64 compiler actions (both namespaces); all **36** current anchors and actual stream bytes match
the independently gated 7b-2 corpus. No 7b-3 behavior is exercised. Candidate ABI and focused B391 proofs are
separate below. No new full tools/probes/census/board-pair/full-mutation gate was run for this authoring task.

## 2. Decisions and gate prerequisite exposed by the pre-check

### 2.1 B389 — owned arguments and prepared state: OWNER RULING REQUESTED after source-validation

0e's `DeferredActionRecord` is **24/8**, two rows **48 B**, with request ID, 32-bit deadline and five u8 flags.
It contains no arguments. Production completion wipes the ingress body, and ACK/epoch changes release the
transcript. Borrowing either buffer leaves parameterized actions dangling. R-RA-22 also prohibits borrowing
open/bootstrap capacity. A live `span`, parser pointer, shared command scratch or stack adapter is not ownership.

QA compiled an **illustrative argument-owning candidate**, not a finished implementation plan, with the real
native/ARM/Xtensa flags: full request/epoch/source identity, u64 deadline, frozen u32 delay, role/kind/phase,
length, and 202 bytes for the exact 201-byte line plus NUL. Each row is **248/8**. Adding independent u32
scheduled detail to each transcript header makes it **32/8**, up from 24/8. Two rows plus four enlarged headers
price **8824/8 → 9352/8: +528 B** on all three ABIs. Mobile's feature-inclusion marker remains absent; compiling
a candidate type on Xtensa is not evidence that a mobile instance is resident. No Node pin or linked RAM changed.

This candidate demonstrates the missing ownership cost. **Retaining a raw line alone is not sufficient proof
of reliable preparation.** Source-validation must enumerate the actual prepared representation for all families,
including generated identity material, config/provisioning deltas and backend information where needed; prove
that activation cannot fall back into a second mutable-policy/validation pass; and measure the complete candidate
on all three ABIs. An explicit bounded byte carrier is acceptable only with a single typed conversion and full
ownership/size proofs. Do not hide a whole NV blob, extra static scratch, heap allocation or resident adapter.

**Revision-2 recommendation:** price **one** deferred row under B390's serialized policy, with independent
immutable terminal detail and ACCEPT-only residency. This deliberately proposes replacing R-RA-22's two-row
capacity; it is not already authorized by that ruling. Fresh compile-only pricing of the same illustrative
248-byte row, once, gives **8824/8 →9104/8 (+280 B)** on native/ARM/Xtensa: 248 + four 8-byte header increases.
This is still not the expected implementation layout or linked RAM. The original +528 two-row measurement
remains valid historical evidence. No native/gateway Node re-pin is authorized until the complete plan is priced.

Prefer the existing **typed** request/plan paths. `firmware_config.cpp:915–918` already constructs `JoinRequest`
and calls `JoinService::apply_join`; the actual `JoinRequest` measures **32/8** on these three ABIs and
`firmware_join_service.h:150` owns validation. Existing cfg/PHY parsers likewise provide reuse points. Neither
that 32-byte request nor the raw-line candidate proves a complete prepared apply plan. The 248-byte row is a
comparison for storing a maximum raw line, not an expected allocation and not a mathematical upper bound for
every family's generated/prepared state. The coder must enumerate and measure the complete typed-plan union,
metadata, immutable detail and any auxiliary resident state before the owner rules B389. No hidden blob/heap.

**Optional scope fallback proposed for owner review:** if the enumeration demonstrates that a named family
needs a separate validate/apply preparation slice, the owner may authorize 7b-3 to keep that family remotely
`refused`, with zero disruptive effects and a registered, separately fenced follow-up. This is not an automatic
fallback. Preflight must list each affected policy row/family, missing seam, real-source reason, proposed
follow-up and closure proof; QA records the explicit exception before coding. Keep the authority classification
unchanged and test the retained refusal. Do not promise `scheduled`, label the family complete, quietly drop
its tests, or call the overall disruptive-command arc complete. Without that exception, the full-family fence
stands and any necessary preparatory split returns STOP-1. Role protocol step 7 reserves allocation/scope
rulings to the owner; this draft makes neither.

### 2.2 B390 — conflicting accepted actions: OWNER RULING REQUESTED

Two rows alone do not define which two promises can coexist. An accepted reboot/DFU/factory reset loses a second
RAM-only action; `prep-restart` sets `g_halted` and skips the current executor/timer block. These are planned
side effects, not the design's external power-loss exception. A second retune can also invalidate a prepared
first action or its reply. No production v2 scheduler currently exposes this bug; this is a design/admission gap.

**Revised recommendation:** at most **one outstanding disruptive promise per target, across all ACL slots**,
from successful reservation/preparation until activation consumes it, with **one deferred-action row**. The
second row would have no reachable use under this admission rule; dropping it is an explicit proposed change
to R-RA-22's capacity. B389 prices the final representation after enumeration.

Use a retained, immutable **typed TERMINAL `action_busy`**, proposed code **0x08**, for a distinct conflicting
request. Revision 1's `refused` plus `deferred_busy` text proposal is superseded: the controller must distinguish
capacity conflict without parsing OUTPUT. `refused` keeps its not-allowed/unsupported meaning. A busy request
has not executed, but its result is final for that request ID: exact retries replay identical bytes, never
become scheduled after capacity frees, and never occupy the action row. Any later execution is a fresh request
under 8a's policy; no automatic retry of an unknown non-idempotent outcome is authorized here.

**Separate 7b-3-0 codec prerequisite if this proposal is approved:** 0x08 is currently unallocated and rejected
(`remote_codec.h:73–86`, `remote_codec.cpp:641`). QA first authors a small codec-only brief; it allocates the
typed result, updates the real result guards/exhaustive consumers/reference and invalid-boundary tests, and
gets its own gate and owner commit, with no scheduler/producer/state change. For example the existing 0x08
invalid-result fixtures must move to the next genuinely unallocated value without losing their semantic-
failure coverage. No new envelope or `wire_version` is needed. Then QA reissues this behavior brief at that
actual hash with the codec gate's full acceptance floor. Do not put this new result into production under the
current behavior fence or hide it as an ADMISSION_RESULT code. **The five-byte `scheduled` terminal itself
needs no codec slice**: `remote_codec.cpp:654` already preserves bytes after its result as `result_detail`.

Safe and force rollover regard
active preparation or an **armed** action awaiting activation as target-wide executing work and return the
existing `executing` admission notice. A completed **unarmed** transcript instead retains existing safe-busy/
confirmed-force-abandon semantics: force can release its own slot's unowned action with the abandoned transcript.
This prevents an unsendable prepared response from becoming uncancellable executing work.
The restriction does not redefine command authority or the separate open-rate budget.

**Recovery precision for the second-read residual:** never-owned/unsendable is **unarmed**. An armed row already
has checked queued/parked ownership and an activation deadline; even if it never airs or reaches the controller,
loss of delivery/ACK does not extend that deadline. Force is not a cancellation API for an armed promise.
An unarmed response may retain the single row indefinitely; same-slot confirmed force can abandon it. A
*different operator* cannot roll that slot, but a permitted **remote owner ACL change** can invalidate it:
`firmware_admin_verbs.h:386–396` dispatches confirmed removal; `firmware_admin_acl.h:335–353/399–414` preserves
self/last-owner safeguards and commits the live invalidation; `remote_session.cpp:364–372` applies it. Under
§3.4 the invalidated unarmed action is released too. Thus “another credential has only physical recovery” is
too broad. Prove same-slot force and a different owner's permitted removal, operator/self/last-owner refusal,
and failed/no-op ACL write preservation. Physical recovery remains when no usable authorized remote path
exists. No new cross-slot force/cancel privilege is proposed.

This conservative policy is a proposed restriction, **not implied by R-RA-22 or access permission**. An alternative
allowing compatible concurrent promises needs an explicit complete action-pair matrix and proof that neither
accepted deadline, prepared meaning nor response ownership can be invalidated. The coder may source-validate
that alternative but cannot silently adopt it. The rest of this draft uses the recommended serialized policy;
QA reissues the affected clauses if the owner chooses differently.

### 2.3 B391 — stale X09 after B388: scoped instrument repair, no new owner ruling

At `f993191`, `radmin5rx` X09 in `tools/probe_ui_model_mutations.py:11020` still searches for the removed
comment `// to exactly what was already armed.` followed by `radmin_expiry_arm();`. A fresh run of the actual
harness gives baseline **2909/174485/0**, **VACUOUS /match count 0**, exit 1. Therefore the previous **772 RED**
full gate is historical evidence from before B388; unchanged executable bytes do not make this strict reader pass.
B388's comment correction remains correct and closed. The scoped-return claim of complete gate attribution
was too broad; the independent QA report §9 records that error rather than erasing the old report.

A private, code-based replacement pattern ending at the ACCEPT/CLIENT boundary matches once and drops exactly
the same expiry-arm call. The actual harness then gives **X09 RED /1 failed assertion /match count 1**, baseline
**2909/174485/0**. Proposal patch and both raw runs are retained in the pre-check directory. No shared harness
edit was made. The coder may apply this narrow instrument-only repair during source-validation and return it
separately for QA's scoped verification, before production work; no counts, assertions or production lines change.
Run the whole `radmin5rx` battery and all source-pattern matches after repair, plus tools discovery for the runner
edit. The future full gate still executes the full union. Do not count VACUOUS as the known B342 exception.


### 2.4 B392 — prep-restart remote lockout: OWNER CALL before dispatch

The second reader registered B392; preserve it open until the owner chooses. Source `fw_main.cpp:438` sets
`g_halted`; the `:1372–1803` operating block contains mesh RX, timers, TX and remote executor. Once prep-restart
activates, **mesh remote administration stops**: no remote `reboot`, rollover or follow-up request can recover
it. This is the existing halt behavior, not an instruction to keep radio administration secretly running.

**Recommendation:** retain remote scheduling and explicitly warn of this deliberate lockout. Recovery requires
**local restart**, not necessarily a physical power-cycle: USB/BLE service remains outside the halted block
(`fw_main.cpp:1808/1813–1814`), and local `reboot` reaches the reset wrapper (`firmware_commands.cpp:1528`).
Hardware reset/power-cycle also restarts it. BLE availability/security remains product-specific; do not grant
new physical-presence authority. A different remotely held ACL credential alone does not overcome stopped RX.

The alternative is a retained remote refusal for prep-restart with its operator/disruptive classification
unchanged. Keeping mesh RX/executor active while halted would change the deliberate silent-network contract
and is outside this fence. The owner chooses before dispatch; a second-read recommendation is not that choice.
If scheduling is selected, land the Part 57b lockout observation and pass this exact proposed warning to 8a:

> prep-restart stops mesh radio and remote administration. Restart the target locally to restore access; remote reboot and rollover cannot recover it while halted.

8a displays the warning before submission; this does not add a new command confirmation token or implement
controller UI in 7b-3. The bench reservation below is conditional and not a claim that current firmware schedules it.

## 3. Execution contract

### 3.1 Scope and preparation

All source-policy disruptive families are inventoried and source-validated (any delivery exception requires
the explicit B389 fallback ruling above; prep-restart additionally depends on B392): reboot/prep aliases, factory reset, ordinary `regen`, OTA
entry, crash tests, sleep, disruptive cfg keys, gateway, join/create, leave and team. The exact 48 policy rows
are retained in `disruptive-inventory.json`; aliases/subrows are not 48 independent handlers. Read-only/usage
forms within a coarsely disruptive family must be classified by the existing grammar: an invalid command,
missing confirmation, unsupported backend, disabled crash gate or usage-only form must never promise an action.
Operator/owner/physical and profile restrictions remain as ruled. Open execution remains exact status/routes.

Before any side effect, run the shared byte validator, source policy/authority and existing grammar/confirmation;
reserve mandatory transcript and permitted action state; obtain a valid 7a effective delay; prepare the complete
owned action; then freeze the response. No live retune, halt, wipe, membership change, identity installation,
OTA entry or crash/reset call is permitted during preparation. Non-disruptive preparation must be documented
per family, reversible on failure, and shown not to invalidate the current return path or administration trust.
Reuse `firmware_config_parse.h`, existing gateway/PHY parsers and provisioning prepare/validate/apply services.
Do not copy those parsers or perform a file move/refactor alongside this feature (C1/U1/U2).

Preparation returns a typed outcome. Invalid/unauthorized/unsupported requests get a truthful retained
refusal; conflict gets the proposed typed `action_busy` only after the separate approved codec prerequisite; internal preparation or response-staging failure gets `internal_error` before any scheduled transcript
exists. No action remains armed after either failure. Full transcript capacity retains existing ingress pacing
and exhaustion accounting: never execute without a terminal reservation. Capacity refusal after a transcript
reservation is completed and remembered, so an exact same-ID retry cannot later become scheduled under the same
terminal nonce. No new mutable `ADMISSION_RESULT` code or replacement negative-response cache is introduced.

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

The proposed explicit clock origin is **the first checked ownership of that scheduled TERMINAL** by the existing
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

Ordinary messaging `regen` preserves `/mradmid`, `/mracl` and administration sessions. Other authenticated
transcripts and their bytes/cursors/routes remain protected except already-ruled order-rank compaction.
Under §2.2's proposed policy, safe/force cannot pass while preparation is active or an armed action awaits
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
`radmin_action_phase=none|prepared|armed|due` and `radmin_action_armed=0|1`. For a present row also print
`radmin_action_request_id=<16 lowercase hex>`, `radmin_action_slot=<decimal>`, `radmin_action_kind=<stable name>`,
`radmin_action_activation_ms=<frozen decimal>` and `radmin_action_remaining_ms=<decimal|unarmed>`.
The phase determines presence (request ID zero is legal); none omits row-specific fields, prepared uses
`unarmed`; armed derives remaining time from one current clock snapshot with zero at/past the deadline;
due reports zero, including early eligibility from an ACK.
The armed flag means terminal ownership has occurred, including due. Values are live-derived, not new cached
resident state; pure status reads cannot arm, consume, expire or refresh a row. Local/authenticated/open text
status share this bounded scalar view under existing access rules; CLIENT-only builds expose none of it.
Assert exact seeded values, empty/prepared/armed/due transitions and no side effects in the real-TU status probe.

Keep the five saturating ACCEPT counters and meanings. Action busy/validation refusal is an execution result,
not transcript exhaustion or a failed send attempt; no sixth counter is authorized. Seal failure increments
only seal failure, with no enqueue attempt; checked send refusal increments enqueue failure; full pacing counts
neither. A labelled synthetic fault is allowed only in a fixture when a real-Node failure is otherwise
unreachable, with a reachable positive control, full state/cursor accounting and subsequent recovery.

## 5. Implementation fence

Predict `git diff --stat` paths/counts before editing. Expected feature diff spans core lifecycle, firmware
preparation/application and their executed instruments; no file move, unrelated cleanup or simulator change.

- Core: `lib/core/remote_session.{h,cpp}`, `node.{h,cpp}`, `node_mac_rx.cpp`; existing bounded state, transcript
  producer, ACK/install/scan and checked sender seams. New feature-local private headers only if they avoid
  dependency cycles and are named in preflight. No firmware/NV includes or handler execution in lib/core.
- Firmware: `src/firmware_remote_executor.h`, `firmware_commands.{h,cpp}`, `firmware_config.{h,cpp}`, relevant
  existing pure config/provisioning helpers; a focused `firmware_remote_actions.{h,cpp}` is the proposed new
  feature owner. `fw_context.h`/`fw_main.cpp` only declarations, concrete backend bindings and bounded loop
  calls/order. `device_ota.{h,cpp}` only if the explicit entry primitive requires it, preserving local behavior.
  Any broader parser/service rewrite is a STOP-1 proposal for separate preparation; only an explicit B389
  scope-fallback ruling can permit named families to remain refused with registered follow-up. No hidden cleanup.
- Native: extend existing executor/session/transcript/Node tests and 7a timing cases; a focused
  `test_remote_actions.cpp` is permitted. Use real codec/session/Node and count hardware effects with fixtures.
  No production fault hooks. ABI re-pins only after the owner's complete B389 allocation ruling and measurement.
- Instruments: narrow B391 harness repair; source/dependency mutation additions; board ABI measured entries;
  existing inbox/console/UI/features probes for new real bindings. A new `tools/probe_deferred_actions/` is
  permitted if needed to compile the **real command/config/action TUs** with hardware/store fakes; include
  default controlled and no-controls runs in the gate and test discovery. No copied handler implementation.
  Regenerate command inventory with its tool; 204 inventory rows /existing authority semantics should remain.
- Coder's new receipt and directly required instrument artifacts. Register, bench, design, rulings, MEMORY and
  QA reports remain QA-owned. Preparation documentation listed at the top is preserved as permitted input.

OUT: codec/domain/wire-version changes; NV schema/journal; simulator/anchor edits; new timers/capacity without
ruling; authority/DM/ACL policy changes; controller request/ACK/custody/confirmation UI; legacy removal or legacy
activation cleanup; transport redesign; unrelated B312/B315/B342/B350/B359/B364 fixes. Preserve full BLE command
bounds and default identity/trust separation. CLIENT-only builds gain no deferred state or remote action service.

## 6. Required discriminating proofs

| Surface | Evidence required from actual production decisions |
| --- | --- |
| Coverage/preparation | Map every one of the 48 disruptive policy rows and relevant forms to a real preparation decision and handler; valid, invalid/confirmation, unsupported/profile and authority arms. Assert zero disruptive effects before scheduling. Local USB/BLE grammar and behavior regressions are controlled. |
| Complete ownership | Overwrite RX/body/parser/stack scratch after preparation; release ingress, ACK transcript, reuse a different transcript/action slot; activation still uses the original complete plan. Generated secrets and buffers wipe on abort/consume. No local/BLE plaintext leakage. |
| Reservation and conflict | No terminal/action capacity ⇒ zero execution; approved typed action_busy replays unchanged after capacity returns; never parse text to identify conflict. Drive two credentials and two different disruptive commands; assert the owner-selected B390 policy, exact retries and no promise lost to reset/halt. |
| Terminal encoding | Real producer/codec: exact five-byte body, correct LE delay, output order/last sequence, all return carriers, delay at both edges, immutable retry ciphertext/detail before and after activation; other non-scheduled terminals retain old shape. |
| Ownership/send faults | No arming on OUTPUT, capture, full queue, seal/send refusal. Checked queued and parked-zero ownership arm once. Pending frame retries next eligible pass; exact request retry resets replay only. Real Node synthetic seal fault labelled and recovery verified. |
| ACK/clock | Before-owned ACK cannot consume a pending scheduled promise; after-owned valid ACK makes action eligible, actual hardware call only on next main-loop service. Duplicate/wrong identity/tampered ACK does nothing. Lost ACK: deadline−1 no action, deadline exactly one action, later none; wrap/saturation and delayed-loop cases. |
| Configuration/lifetime | Raw=0 follows live default; valid configured uses that value; all three invalid resolver states refuse. Change raw/PHY after scheduling: frozen detail/deadline unchanged. ACK and ordinary invalidation do not erase armed work; safe/force guard armed work; same-slot force or permitted other-owner ACL invalidation releases unarmed work; operator/self/last-owner and failed/no-op write controls; boot/power-loss clears without a fabricated result. |
| Real handlers | Real `firmware_commands.cpp`, `firmware_config.cpp`, action owner and provisioning decisions execute under host fakes. Assert NV/radio/identity/halt/reset/OTA operation order, exact confirm gate, crash debug/backend gate, ordinary regen admin-trust preservation and preexisting partial-erase failure behavior. Fake executors alone cannot prove this row. |
| Lockout/pre-emption | B392 owner-selected remote prep behavior, stopped mesh RX with local service retained; distinguish external local physical pre-emption/unknown outcome from an internal missed activation. No new cross-slot force or halt carve-out. |
| OTA | ESP inactive→entry; active never→stop from remote; nRF DFU deferred; unsupported refuses. Exact backend OUTPUT retained, no firmware body sent; local toggle regression control. |
| Main-loop/timer | Due action runs despite full TX/continually pending output and does not starve inside `!g_halted`; exactly one consumed action per service call. Real production owner runs in probe; existing fw_main extractor/structural controls must prove actual call and placement, not masquerade as compiling all fw_main. Wheel remains 92/id91, earliest bounded scan/no zero-delay livelock and sleep wake are tested. |
| Counter/compatibility | Preserve all five counters and saturation; new ACCEPT scalar action-status fields exactly match the same live snapshot without mutating it; no mobile residency/symbols; all previous B374–B388 runtime/ownership controls remain effective. B391 is repaired without reducing its detection power. |

Each new decision needs an effective mutation or executed real-TU probe control: premature action/ACK, missed
or repeated activation, deadline refresh, missing owned copy, mutable delay/terminal, wrong result mapping,
call-before-ownership, wrong slot/epoch, bypassed confirmation/authority, lost accepted action, OTA toggle and
hardware/global-sink escape. A source string match or fake executor is not a real-handler wiring gate.

## 7. Full implementation gate and frozen handoff

Run 7b-2 brief §8.2's entire standing chain, with the following explicit inherited/current obligations.
QA independently repeats every required instrument on the complete frozen final inputs; a coder PASS is not QA PASS.
If 7b-3-0 is approved, its own separately gated/committed result and full acceptance floor are prerequisites
for the reissued behavior brief; do not use this pre-codec base or its old reference counts as the final baseline.

1. Fresh `pio test -e native` **and** `./.pio/build/native/program`; derive actual cases/assertions and failure/
   skip counts. A filtered case is only a focused proof. Preserve known B364 limitations.
2. Fresh simulator normal/gateway build graphs and actual compile/link provenance (B385), all 36 current
   `simulation/BASELINE.md` anchors, manifest validation and actual byte comparison against a freshly matching
   base. Do not rewrite manifests to bypass an executable-hash mismatch; retain any comparator refusal and
   independently compare validated streams. Predict zero remote corpus events /36 unchanged; any delta STOP.
3. Both ABI probes, all controls; all six existing probes (console-sink, inbox-verbs, firmware-UI, custody-USB,
   BLE-line, features), default controlled and `--no-neg`; new deferred-action probe likewise if introduced.
   Set `MR_LUS_SRC` explicitly in private snapshots. Only controlled runs qualify; disclose B350.
4. Full tools unittest discovery with a real measured ELF under private `.pio-measure/` so it cannot skip;
   inventory `--write`, bare and `--check`; authority checker +six selftests; A0, DataType literal checker;
   both repositories' whitespace. Preserve the independent PyNaCl codec reference comparison/selftests:
   `2026-09-09-radmin-slice7b2-0-reference.py --compare test/test_remote_codec.cpp --selftest` (89 existing
   arrays including the old 87, four comparator controls and one-byte corruption check). No expected-byte edits.
5. Deterministic base/final **gateway then heltec_mobile**, sequential, same private paths/fixed identity,
   outputs under that checkout's `.pio-measure/`. Preserve/hash original ELFs/payloads and attribute Node,
   linked RAM, flash/sections/objects/symbols and any transient stack growth. Gateway/mobile are the only
   normal board pair; the warning census's own six-environment pinned set is the sole exception. Run it too.
6. Derive two mutation selectors: **S**, every configured TARGET_SRC changed; **H**, complete historical and
   dependency acceptance. Gate **S ∪ H**. H contains the complete 7b-2 **49 batteries** plus 7a's
   `remoteactivation` (22 patterns), `fwactivation` (10), `macwait` (10): **52 batteries /815 configured
   patterns at this base**, before new action controls. Names/counts live in `historical-mutation-floor.json`.
   **Do not report 814 RED as measured:** at the uncorrected closure base X09 is VACUOUS and B342 is the
   separate known unusable control. Repair B391, derive final counts again and execute every selected battery.
   Add preparation/provisioning/config dependencies even if their TARGET_SRC did not change; list each reason,
   pattern matches, actual clean worker baselines, RED/unusable totals and source-restoration hashes. Never
   silently drop a no-match control or replace the union with only new action tests.

Required exact report line: **`PIN re-synced? YES — <independently derived base + additions = final>`**.
Keep strict-reader pin declarations bare; derivations precede them (D5). D6 covers comment-dependent readers.
The frozen report includes actual HEAD/brief hash, all tracked/untracked input hashes, complete failures and
reruns, new tool discovery/inventory, changed path fence and remaining metal limitations. No fixture count is
hardware qualification. There is no fresh full implementation gate at this authoring checkpoint.

## 8. Predictions, STOP conditions and documentation landing

Predict simulator rebuild with 36 byte-identical streams, no NV/wire version movement, unchanged authority/
inventory semantics, ACCEPT-only state/code growth and no mobile Node/RAM growth. Current independently
compiled Node baselines are native **230896/8**, gateway **157264/8**, mobile **117912/8**. Earlier linked
7b-2 measurements, retained but not rerun here: gateway RAM **203956**, flash **570588**; mobile RAM **207756**,
flash **1372992**. Do not turn the +528 candidate into a replacement pin. Publish final allocations/stack and
linker attribution; stop on an unexplained delta, new warning or weakened check.

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
All other quoted prohibitions remain. Also STOP on missing owner storage/concurrency/B392 decisions or
a missing approved codec prerequisite; an unapproved family omission; stale/concurrent
inputs; predictable validation postponed until after scheduled; new nonce plaintext for an old terminal;
borrowed action/detail lifetime; internally canceled/extended armed promises (external local physical
pre-emption is the explicit unknown-outcome exception above); new timers; mobile Node/RAM growth;
production codec/NV/controller work; or another unusable/vacuous instrument. B342 stays separately named.

After independent PASS, QA closes findings in place and lands the measured allocation, timer/ACK/retry/lifetime
contracts in design/MEMORY/tracker, preserving the original proposals/rulings. Protocol/frame/manual replacement
remains Slice 9; an intervening documentation claim must still be accurate. Owner commits and verifies on metal.

**Part 57b has a conditional documentation reservation, not a runnable/passed gate now.** On software PASS QA adds only irreducible hardware checks:
real reboot/halt/DFU/OTA/reset/wipe effects and any newly grown hardware-only stack path. Require a scalar-only
scheduled diagnostic before activation with exact format
`> remote-action scheduled request_id=<16 lowercase hex> activation_ms=<decimal> action=<stable action name>`
and activation diagnostic
`> remote-action activate request_id=<same> trigger=<ack|deadline>`.
These are local metadata observations, never copies of command/output/secrets and never proof of RF delivery.
OTA backend is separately proven in captured response OUTPUT. The coder must bind/verify these formats and
publish the action-name list before the bench entry lands. Actual controller ACK/lost-ACK/RF round trips remain
8b's controller/carrier metal gate; a labelled host/controller fixture must not be described as that product gate.

If B392 keeps remote prep-restart, Part 57b must observe the scheduled/activate diagnostics for
`action=prep-restart`, the actual halt and loss of mesh remote access, followed by successful local restart.
The existing local success line is
`> prep-restart — routes + inbox cleared, network membership KEPT, node HALTED. Power-cycle the fleet to restart clean.`
It is a local handler/hardware reference, not permission to leak the remote transcript to USB. Preserve the
existing partial-erase warnings. Local `reboot` prints `> rebooting`; USB/BLE local service and hardware reset/
power-cycle are distinct recovery mechanisms. Do not present the lockout observation as an automated gate or
promise a remote recovery action after the halt. 8a inherits §2.4's pre-submission warning after the owner's choice.
