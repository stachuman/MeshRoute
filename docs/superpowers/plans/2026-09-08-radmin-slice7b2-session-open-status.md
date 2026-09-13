<!-- QA/Author: OpenAI Codex, replacing Claude; production coder: separate Codex session -->
# Remote-admin v2 Slice 7b-2 — session control, open execution and status counters — brief — 2026-09-08

**Status: revision 4 SOFTWARE-COMPLETE / INDEPENDENT QA PASS 2026-09-09, uncommitted at
`564f460a3b755a104f146da70457e5c8c68e99b9`.** B388's exact three-line comment return is independently
verified; fresh focused reproduction 15 checks PASS, all executable/instrument inputs unchanged.
B378/B379/B388 are closed; B387 remains closed. The prior independent full gate remains attributable:
native 2909/174485/0, corpus 36/36 byte-identical, 772 RED /known unusable B342, tools 343/zero skips,
all ABI/probes/checkers/boards/census; gateway RAM +4976 B /flash +7840 B, mobile unchanged.
[QA gate and scoped closure §8](../evidence/2026-09-09-radmin-slice7b2-qa-gate.md#8-b388-scoped-return--independent-pass-and-final-closure).
No full-gate rerun for this comment return and no new owner ruling or contract revision. Consumed revision-4
SHA-256 remains `0920bb419cf5f33fbedb8fabfa2c9dc9b91c81744727fd69cb21b16253a7200e`, retained with QA
artifacts. The reissue/pre-check narrative below is historical; the implementation contract is unchanged.
Next: owner commit, then a separate 7b-3 brief against that actual hash and coder source-validation.

**Base and preserved inputs:** MeshRoute **`564f460a3b755a104f146da70457e5c8c68e99b9`**, clean at the start of
this reissue; simulator **`06746a97de5764415d6fcef10b97bca90569b9c7`**, clean, no simulator edits. Despite the
commit subject “7b-2”, source and the [independent codec gate](../evidence/2026-09-09-radmin-slice7b2-0-qa-gate.md)
confirm that it lands **7b-2-0 codec preparation and QA documentation**, with no new target producer/open state.
The previous `1d4b3ad` behavior-brief base and its then-dirty preparation inputs are now historical and committed.

The permitted uncommitted preparation inputs for this revision are this brief; the [author pre-check](2026-09-08-radmin-slice7b2-precheck.md)
§8 and its new `docs/superpowers/evidence/2026-09-09-radmin-slice7b2-reissue/` companion directory; the maintained
register's dispatch/B386 and commit-status updates; MEMORY's dispatch line; the design's implementation header
and §19.1 status rows; and the codec brief/independent QA report's owner-commit closure notes. Those are
QA documentation/evidence inputs, not production changes. The existing coder
[source-validation report](../evidence/2026-09-08-radmin-slice7b2.md), including its §6 owner receipt, is committed
and preserved unchanged by QA. Coder appends a new revision-4 preflight to it, identifying this base and this
brief's actual SHA-256; old preflight figures are history. Inventory/hash all actual tracked and untracked
inputs before implementation. Preserve all work: no reset, clean, commit, production repair by QA, or HEAD-only
snapshot of an eventual dirty implementation. Builds/mutations must not overlap coder edits in a shared tree.

**Revision provenance:** the coder consumed revision 1, SHA-256
`a15d984a20a1b62db1f0a5fc7828edfeeb5d4746d25d01b7210d9120a06eea12`. Revision 2 names the omitted design
input (B380) and separates present-zero admission from checked return-send refusal, including accounting and
lifecycle proofs (B381). QA independently checked the source and reran existing N11: **1 case / 11 assertions /
0 failed**, 2882 unrelated cases skipped. This closes the two wording findings, not B378/B379 or a behavior gate.
Revision 3 records the owner's approval of revision 2 (SHA-256
`1802c25a99e0d470b299a769a072b56e1bd6f12d8df7e230962255de82fa387a`), explicitly states the shared target-wide
budget, and changes the remaining HOLD from owner decisions to the separately gated codec prerequisite.
Revision 4 supersedes revision 3 (SHA-256 `fdbf7b38d55ed7b67897851544b9acd322fa30a2ecb7b43cff4f267839bf8acd`):
QA verified the actual codec landing, names its existing API, rechecks the baseline and retains its full gate
floor. B386 corrects the excessive whole-header immutability wording: existing shared scheduling-order
compaction is permitted, while other slots' response contents, identity, replay cursor and relative order stay
protected (§3.2). This is a source-backed clarification, not a new wire/storage/owner ruling.

This brief covers the target half only. **7b-3 retains deferred actions, `scheduled`, ACK-earlier activation,
the 300-second disruptive-action deadline and OTA reporting.** Slices 8a–8c retain the product controller,
automatic safe-rollover decisions, force-confirmation UI, request retry/ACK debt, BLE/USB delivery and custody
consumption. The existing target readiness gate remains; enabling open diagnostics on an unprovisioned target
is not an incidental change in this slice.

## 1. Binding agreements and source pre-check

Read the design §§7.3, 8.6–8.10, 9–11, 15, 19/19.1; rulings R-RA-10/13/21/22/24′/25/28/31/34; the 7b-1
revision-6 contract and independent QA report; also read the now-settled R-RA-35/R-RA-36. The following are
**verbatim pins**, with R-RA-36's B379 supersession explicitly identified in §2.1.

R-RA-22:

> The two authenticated ingress rows are partitioned so at least one is always available to owner/control work;
> the four open/bootstrap staging rows have a peer-local maximum of one open response. Open/bootstrap work cannot
> borrow authenticated transcript/ingress capacity, and authenticated ordinary work cannot consume the reserved
> owner/control admission.

> Expiry uses **one shared earliest-deadline scan**. `TimerWheel::kCap` grows once, 91 → 92, measured as +8 bytes
> on host, ARM and Xtensa in the 0e mirror; no class receives a private timer ID. The scan visits only the bounded
> rows resident in that product profile and re-arms to the true earliest deadline.

Design §7.3:

> When an execute request receives `session_full`, the controller automatically sends `SAFE_ROLLOVER` under the current
> session key with a fresh request ID and no plaintext. Execute capacity reserves admission for this control
> message. The target accepts it only for the same key slot/current session, when no operation is executing,
> and when every old response transcript has been acknowledged. It then rotates that slot to a fresh random
> epoch, clears the now-cryptographically-invalid old records, and returns the new epoch in the
> rollover-result layout under the base key, bound to the rollover request ID, with abandoned count zero.

> If any completed transcript remains unacknowledged, safe rollover refuses with `session_busy` and a bounded
> count; it never destroys the transcript.

> `FORCE_ROLLOVER` is the explicit recovery. It is never automatic: the controller presents the count of
> unacknowledged outcomes locally, requires a literal confirmation, and only then sends the authenticated
> control. The target again requires no operation executing, rotates the epoch, and reports how many retained
> outcomes were abandoned in the rollover result.

Design §11:

> A completed authenticated transcript is immutable, including its terminal and frame count. A send-time seal or
> enqueue failure preserves its bytes and cursor; the next eligible main-loop pass retries the pending frame.
> An authenticated exact request retry instead restarts that completed transcript at sequence zero, without
> dispatching again.

**QA source ledger:** current reissue pre-check §8.3; §3 is the original `1d4b3ad` observation. Key current
seams are `remote_session.cpp:972` (unconsumed control), `:858`
(metadata-only open staging), `:1028` (unanswered full verdict), `:1014` (acknowledged retry),
`node_mac_rx.cpp:2108/2119` (bootstrap versus transcript sending), `node.cpp:100/113` (draw/install),
`firmware_remote_executor.h:49` (authenticated executor only), `firmware_command_authority.h:277` (exact open
authority), and `firmware_commands.cpp:911/1629` (status and real seam). Relocate every anchor before editing.

QA matched **390/390 committed implementation/gate inputs** to its independently measured codec checkout,
and all five frozen codec/test/instrument/reference hashes. Fresh executions of the source-matched QA native
and simulator binaries give **2888/172264/0, zero skips**, **36/36 anchors and actual stream byte identity**
against QA's codec-final corpus. Reference strict comparison is **89/89** (the original **87/87** plus two new
arrays), with four comparator controls RED; the labelled synthetic B379/session proof is **134/134**.
Fresh host/ARM/Xtensa candidate compilation reproduces **+4976 B**, and the B386 real-session reproduction
passes **43 checks**. Artifact reuse and exact commands are disclosed in pre-check §8; no fresh native/simulator
build, board pair, mutation union or full implementation gate is implied by this authoring checkpoint.
The final gate remains the complete §8 chain, independently rerun after the frozen handoff.

## 2. Owner decisions and the committed codec prerequisite

The owner approved both revision-2 proposals on 2026-09-09. The exact confirmations and contracts are now
R-RA-35/R-RA-36 in the rulings ledger; the coder receipt §6 remains unchanged. The allocation and wire policy
are settled, while implementation and independent verification remain pending. Do not re-request access or
approval for these decisions. The separate 7b-2-0 codec preparation is complete at the pinned base; this
revision-4 behavior brief is the next source-validation input.

### 2.1 B379 — consume the committed nonce-safe admission codec

**Verified original problem, still forbidden for new producers:** terminal nonce derivation
(`remote_codec.cpp:318`) excludes result/detail. The original 116-check codec/session proof established: a `session_full` slot can later execute the identical request under
the same epoch when another slot releases the shared pool; a safe-rollover retry can see busy count 2 then 1.
Their planned terminal plaintexts differ under the same nonce. There is no such negative-result producer in
7b-1; its completed transcript rule remains correct. A controller policy to choose a fresh ID does not prevent
replay of a captured old request.

**Approved R-RA-36, codec now implemented:** new non-executed, state-dependent admission/control refusals
must use the existing fixed, **authenticated but intentionally clear** `ADMISSION_RESULT` response domain.
Its fields are non-command status metadata; command/output confidentiality is unchanged. **REMOTE_RESP opcode
`0x5`** is allocated, actual ACL slot 0–9 only. Consume the existing codec; do not allocate/reimplement it.

Use `RemoteRespOpcode::admission_result`, `RemoteDomainId::resp_admission_result`, `RemoteAdmission`, and
`RemoteResultKind::admission`. Compose ctl through `remote_ctl`; fill `RemoteMessage.request_ctl`,
`admission_code` and `admission_detail`, with empty application plaintext. Reuse `remote_body_encode` /
`remote_body_decode`, `RemoteKeys`, and the common carrier/cap path. The named fixed overhead is
`kRemoteOverheadAdmissionResult == 28`. Decoding publishes the typed fields in `RemoteDecoded.msg` only on
success. Code `0x00` is `session_full` in this admission domain, `completed` in TERMINAL and
`already_acknowledged` in authenticated PROTOCOL_ERROR; never interpret a bare result byte across domains.
The committed exact body is:

```text
offset  bytes  field
0       1      ctl = ADMISSION_RESULT opcode + actual ACL slot
1       8      request_id, little-endian
9       1      request_ctl (AUTH_EXECUTE / SAFE_ROLLOVER / FORCE_ROLLOVER, same ACL slot)
10      1      admission code
11      1      detail (bounded unacknowledged count for session_busy; zero otherwise)
12      16     authentication tag, no ciphertext
total   28
```

| Ruled admission code | Request domain | Meaning / detail |
| --- | --- | --- |
| `0x00 session_full` | AUTH_EXECUTE | Shared seen pool full; no execution; detail zero |
| `0x01 ingress_full` | Any of the three named request domains | Its permitted ingress partition is occupied; no new execution/rotation; detail zero |
| `0x02 session_busy` | SAFE_ROLLOVER | Same-slot completed/unacknowledged transcript count; positive, production maximum derived from the four-header pool |
| `0x03 executing` | SAFE_ROLLOVER or FORCE_ROLLOVER | An operation is executing; no rotation; detail zero |
| `0x04 preparation_failed` | SAFE_ROLLOVER or FORCE_ROLLOVER | No fresh usable epoch/prepared result could be committed; detail zero |

Use `K_session`. For this **admission domain only**, the committed nonce implementation already appends the
three exact clear bytes `request_ctl`, `admission_code`, `admission_detail` after its source-hash term.
The committed AAD path binds the entire clear header and stable controller `SOURCE_HASH`. Do not add a
second nonce/AAD builder or modify these preimages in the behavior slice. There is no response sequence in
this fixed domain (the existing no-sequence nonce term is zero). Thus every varying notice byte changes the
nonce as well as the AAD. Repetition of the same notice is byte-identical. The request-domain byte separates
execute/safe/force requests sharing an ID. Decoder retains a typed notice domain/code/detail; it rejects
reserved codes, invalid request/response slot pairings, unsupported request opcodes, nonzero unused detail,
zero busy count, extra/missing bytes and bad authentication. Consumer checks the busy count against its ruled
target capacity; the wire field itself is a byte. No open/sentinel form of this notice is allocated.

Existing OUTPUT/TERMINAL/PROTOCOL_ERROR/bootstrap/rollover bytes and KATs stay unchanged. Existing terminal
codes `0x06/0x07` remain recognized by the codec for their frozen vectors and are not repurposed; this target
implementation produces `session_full/session_busy` only in the new domain. Design §8.9's assignment of these
non-executed outcomes to TERMINAL is explicitly superseded by R-RA-36. `already_acknowledged` stays the
existing distinct authenticated PROTOCOL_ERROR, exactly one result byte, no mutable details.

**Attribution completed:** **7b-2-0** passed its separate full native/reference/mutation/corpus/board gate and
is owner-committed at `564f460`. It adds no target producer, Node allocation or open behavior. All old wire
vectors remain identical. QA's **134-check** real-session/codec reproduction independently confirms new-domain
separation from an actual completed seq0 transcript and changing busy 2→1 nonces/wire, using **labelled
synthetic notices**; it is not the missing real target producer proof. B379 stays open until §8's real
full/busy/retry lifecycles pass. No codec production change, new wire allocation or `wire_version` bump is
in this behavior slice. M3/C4 remain in force; no spare-sequence or evictable-cache substitute is authorized.

### 2.2 B378 — independent open capture capacity, rate policy and measured ABI re-pins

The current four staging rows are metadata only. They retain neither the decoded command nor its output.
R-RA-22 forbids using the four authenticated headers/eight chunks or either authenticated body for open work.
The prior real-handler fixture's `status` sample was **364 bytes**, already larger than one open response
frame. That is a historical fixture sample, not a maximum or a post-counter output-size pin.

**Approved R-RA-35 allocation:** preserve **3 open + 1 bootstrap** staging headers and every authenticated capacity.
Append ACCEPT-only `OpenCapture[3]`, paired by index with `staging[0..2]`; bootstrap has no capture buffer.
Each owns **`kRadminChunkSlots * kRadminChunkBytes == 1648` bytes**: the existing authenticated per-operation
maximum, used as an independently owned open bound, not borrowed storage. Add only two new saturating u16
counters: `inbound_refusal` and `open_rate_refusal`. All five counters then have status exposure (§6).

The measured candidate per capture has `bytes[1648]`, u16 `len/next_offset/frame_cap`, and u8
`phase/truncated/terminal/next_seq`: **1658 bytes, alignment 2**. The three captures plus two counters total
**4978/2**. Appended to the actual state declaration, existing tail padding participates: `RemoteSessionState`
**3848/8 → 8824/8**, **+4976 B**, independently measured with real host, ARM and Xtensa toolchain flags. An
extra-byte-per-capture control moves the total to **8832**, so the measurement is discriminating. These are
candidate layout measurements, not linked board RAM. R-RA-35 authorizes measured native/gateway Node re-pins and
fully attributed final RAM growth; **mobile Node and RAM remain unchanged**. No extra resident reply cache,
key cache, adapter instance, global work buffer or timer is included in the approved allocation.

**Approved R-RA-35 rate policy:** the limit is **three open admissions total per target in any 300000 ms
window, shared across all requesters—not three per requester**. Each of the three open rows retains one
budget position until 300000 ms after admission, with at most one row for a peer source. This is an explicit
rate-policy choice using the existing named `radmin_staging_lifetime_ms` duration, not an assertion that the old holding TTL
already was a rate limiter. After completion, retain the peer/deadline as a cooldown row until that original
deadline; wipe the command/output immediately. At the exact edge the row becomes available. This gives a
global burst of three and at most three starts per 300-second window, including changed/spoofed source hashes;
peer changes cannot manufacture more rows. It intentionally limits repeated open diagnostics to at most one
per five minutes per retained peer. Authenticated work and bootstrap consume no open quota. New IDs or retries
do not shorten a live row's deadline. No extra timer or clock source is needed.

At the exact deadline, the expired position can be reused; the counted admission interval is
`(now - 300000 ms, now]`. Test staggered admissions and completion/identity changes across that boundary,
not only three simultaneous requests. The final ABI pins must still come from actual measurements;
the approved storage proposal is not a precomputed linked RAM result.

## 3. Session-control behavior

### 3.1 Admission, identity and priority

Retain existing authentication-before-state ordering, mandatory source presence separate from numeric zero,
first-admitted route ownership and hard GENERAL/CONTROL partitioning. Owner execute and authenticated
safe/force use CONTROL; operator execute uses GENERAL. Neither borrows. ACK remains the synchronous,
reservation-free release already proved in 7b-1. The 16 seen rows remain a shared total, not sixteen per slot.

Consume the existing CONTROL ingress for safe/force on the main loop before ordinary transcript/open work.
No seen execute row is allocated for session control. If TX is full, do not draw an epoch, rotate, encode a
replacement result or count a failed send; leave the control pending until the next eligible pass or its
existing pre-execution expiry. Validate live slot/opcode/row ownership again before preparation. Real slot/root
invalidation already removes stale ingress; do not invent a post-invalidation reply under a retired key.

### 3.2 Safe and force rollover transaction

1. Use one current-state snapshot. Refuse rotation if **any operation is executing**; the guard is target-wide.
   For SAFE, additionally count **this slot's** completed, unacknowledged transcripts and return `session_busy`
   if nonzero. FORCE may abandon them; the exact count goes into its successful result. Other slots' epochs,
   seen records (including first route/source), ingress, owned transcript output bytes, terminal, frame count
   and replay/send cursor survive byte-for-byte. **B386:** existing `transcript_release` compacts the shared
   `TranscriptHeader::order` ranks of later retained headers; only that scheduling-rank adjustment is allowed
   in another slot's header. It must preserve relative send order and encoded pending bytes. The existing free
   chunk-list rebuild is also shared bookkeeping, not permission to alter another transcript's owned chunks.
   Do not promise a byte-identical whole session/header or redesign the shared pool to satisfy such a promise.
2. Prepare exactly one epoch draw through the existing `Node::admin_draw_epoch` boundary. Reject zero/failure
   **and equality with the current epoch**; no retry loop, clock/counter fallback or automatic force. A rejected
   draw preserves the old key/work and returns a preparation-failure notice if encoding/transport permit.
   B312 remains open: this existing void-HAL/nonzero check is not a hardware entropy-health proof.
3. Before changing live state, prepare the complete **34-byte** ROLLOVER_RESULT using the current root/base
   key, fresh epoch, exact request ID, original stable controller source and captured route. SAFE carries zero;
   FORCE carries the exact abandoned count. A codec/preparation failure changes no epoch or old work. Do not
   force the 34-byte result into `RemoteRxResult::reply[33]`; size transient fixed-reply scratch from the named
   codec overheads, with cap checks and compile/test bounds.
4. Commit via the existing `RemoteSessionInstall` / `Node::admin_session_commit` path, never direct writes to
   `epoch[]`, seen rows or the transcript pool. No root/ACL NV write occurs. Slot invalidation frees its executed
   tombstones, transcripts and unexecuted ingress. Following the literal §7.3 preconditions, a **never-executed
   admitted** request of that slot is cancelled by this invalidation too; it has no executed outcome to count
   as abandoned. Test and document that case rather than treating it as a completed transcript or silently
   adding a different safe-rollover precondition. The controller's handling of its now-stale pending work is 8a.
5. Offer the encoded result once to the existing shared return sender, with queued/parked/refused
   distinguished. A send failure after commit cannot roll back the epoch or recreate abandoned transcripts.
   A lost result is recovered by read-only bootstrap of the current epoch (§7.2); the old-session control then
   fails authentication. Do not retain a second session key/result cache or promise replay of that old control.

Busy/executing/preparation refusals preserve the protected epochs, seen rows and transcripts. Consuming the
control reservation, recording the specific counters and sending its notice are the explicit bookkeeping
effects, not a claim that the whole Node remains byte-identical. A codec failure must not recursively try to
encode an endless series of error notices. Wipe keys, plaintext and preparation scratch on every exit.

### 3.3 Full/ingress refusals and acknowledged retries

Use the new fixed admission domain for authenticated `session_full` and `ingress_full`; never allocate or
evict a seen row/transcript just to report a capacity refusal. These bounded fixed notices extend the existing
receive-time reply seam: at most one checked attempt, only when TX capacity permits, no deferred reply cache.
If no transport capacity exists, retain no false promise of delivery; the controller's exact request retry
can observe current admission state. This is explicit bounded backpressure, and the refused input is counted.
They are not retained execute transcripts, so they do not acquire 7b-1's next-pass transcript retry lifetime.

An exact execute retry after ACK produces existing authenticated `PROTOCOL_ERROR{already_acknowledged}` at
seq0 with **exactly** the one allocated result byte and the first retained route/source. No second dispatch,
ordinary terminal, mutable detail, new seen row or transcript. An ID-reuse mismatch remains a refusal with no
command execution or redirection; it does not expose or replace the original transcript. Bad auth/key/carrier
requests remain silent. One shared credential means one slot/session domain; a different authenticated source
cannot take over another source's request ID. Safe rotation can free only the selected slot's portion of the
shared pool; it cannot guarantee new execute capacity when other credentials occupy all sixteen rows.

## 4. Open execution and response lifecycle

### 4.1 Own input, one common authority decision

After the current structural/source/carrier/readiness checks, admit into one eligible open staging/capture pair
and copy the complete decoded command bytes. Do not retain a caller-owned decode/RX span. The raw command may
initially occupy that pair's own output array; it must be copied into a bounded, wiped **automatic** buffer
before capture resets/overwrites the array. The named raw storage bound remains separate from the shared
remote command-validator bound. No field-by-field copy of a second carrier or parallel whitelist is allowed.

Run only through the real `exec_console_line` seam with REMOTE / `remote_open`, no authenticated ACL slot,
no physical-presence claim, original request ID and `remote_command_max_bytes`. The existing generated policy
and `command_authority_admits` enforce **exact argument-free `status` or `routes`**. Leading/trailing spaces,
arguments, case changes, NUL/CR/LF, unknown commands and disruptive commands never reach a disallowed handler.
Core retains bytes/lifecycle; firmware retains command-policy/dispatcher ownership. Do not duplicate those
verb names into a core dispatch table. The real Print adapter remains in firmware, not the native pure header.

Each staged operation executes at most once while its row is live. An open duplicate never resets its cursor,
deadline or route and never re-executes in that row. After expiry it may be a new read-only snapshot; open has
no authenticated replay/tombstone/ACK semantics. Ordinary messaging DM, private-inbox view, ACL acting-slot
binding and every 7b-1 authenticated transcript invariant remain independently gated.

### 4.2 Capture, pacing, completion and release

Reserve the independent pair and mandatory terminal metadata **before dispatch**. Capture the same byte stream
the handler writes locally, up to the approved 1648-byte bound; output beyond it sets truncation without
claiming complete output. Derive each output frame's payload cap from `remote_body_cap` on the actual return
carrier **minus `kRemoteOverheadOpenResponse` (10)**, not the authenticated overhead (26) or the 206-byte auth
chunk storage constant. This allows multiple OUTPUT frames with contiguous seq0 onward and one terminal.
Use the existing typed seam-outcome mapping; `scheduled` still has no success producer. A well-formed staged
validation/authority failure may return the bounded clear `refused`/`unknown_command` terminal without a
handler call. Malformed outer/codec traffic is silent. No new open PROTOCOL_ERROR code allocation is needed.

Open response bodies use sentinel F, the existing clear output/terminal codec and no session/base key. They
still use the shared validated return-carrier builder, mandatory SOURCE_HASH, conservative DST_HASH allowance,
application-DM dispatch, `Plane::GLOBAL` and checked SendDispatch ownership. The received route is routing
metadata, not authenticated identity. Use routable **nonzero** peer hashes for successful same-layer and
reversed depth-2/3/4 return flights. Separately prove that numeric-zero source with presence true is accepted
at the codec/admission boundary, subject to the other ordinary checks. The existing shared sender
`Node::radmin_send_response` explicitly refuses destination zero with **`radmin_reply_no_dst`**, before
`send_by_hash` or the cross-layer originator; no transport submission or aired reply follows. Preserve this
refusal and never substitute origin or a static node ID for the peer hash. It is a checked response-send
failure (§6), not a missing-source intake failure or a promise that zero is routable.

Advance only on **queued or parked** ownership, never on a nonzero raw counter. Full queue means no attempt;
codec/seal failure and enqueue refusal preserve the frozen open bytes/result/cursor for the next eligible
pass, bounded by the existing admission deadline. Never rewrite the terminal after output has begun.
Successful terminal admission wipes the entire capture and changes its staging header to cooldown, preserving
only the peer/deadline data needed by the approved rate policy. A parked terminal is transport-owned; it is
not claimed aired or delivered. On expiry, wipe pending/cooldown open state and release the row; already
transport-owned frames cannot be recalled, so an incomplete open result remains incomplete. No RESPONSE_ACK
is required or accepted as an open-release signal.

Cooldown uses the **original admission deadline**, never now-plus-a-new-window on retries or completion.
The existing single expiry scan visits pending and cooldown rows and re-arms to the earliest active deadline.
Bootstrap remains the fourth reserved staging row and keeps its existing one-attempt/release behavior. Its
checked refusal from the shared return sender counts as enqueue failure, including the zero-destination
guard before any lower-level enqueue call; a paced sender that defers without calling the shared sender
does not. A zero-destination open response retains its frozen pending capture/cursor until the existing
deadline; bootstrap still releases its reservation after its one checked sender outcome. Fixed notices retain
no reply cache, and a committed rollover is not rolled back on this refusal (§3). Do not silently change
these lifetimes while sharing counter helpers. An
open flood may fill only its three pairs; completed authenticated transcripts and owner recovery never lend
their storage to it. Clearing/invalidation must not allow a spoofed open source to free unrelated state.

## 5. Main-loop and API shape

Extend the existing core/session and pure executor interfaces; no feature file move/refactor bundle and no
second executor implementation. Typed open/control views must distinguish staging indices from seen indices.
Retain a single return-carrier conversion and the existing real Print/seam adapter. Per service call:

1. Expire eligible pre-execution/open work using one current-time snapshot; consume a pending control first
   when TX is eligible, then return. Full TX preserves pending control and performs no epoch draw.
2. Otherwise preserve 7b-1's authenticated send/dispatch precedence. One attempted authenticated frame or one
   authenticated dispatch consumes the call. Preserve the existing distinction: full TX defers sending and
   control rotation, but does not by itself block an eligible authenticated dispatch into its reserved capture.
   Do not introduce a blanket full-TX return that changes that 7b-1 behavior. If an auth reservation cannot
   proceed because its own pool is exhausted, it need not block independently owned open work.
3. With no eligible auth/control unit, send at most one pending open frame, or dispatch one admitted open
   request. Deterministic oldest open admission first; bounded tie-breaking by index. No loop drains an entire
   transcript into the HAL in one service call.

Existing bootstrap and the explicitly named fixed receive-time notices are the bounded RX exception;
**no command handler or epoch rotation runs in RX/ISR context**. No new `fw_main.cpp` call site should be
necessary: it already calls the ACCEPT-gated executor once per main-loop service. Do not create resident
adapter objects or bypass the seam's context restoration. In CLIENT-only builds no target state, active
context pointer, new target producer or new status fields may be present.

## 6. Status counters and exact accounting

Expose a read-only scalar snapshot, not the secret-bearing `admin_session_state()` record. Append these five
decimal fields in this order to text `status` on ACCEPT builds only, before its existing newline:

```text
 radmin_inbound_refusal=<u16> radmin_open_rate_refusal=<u16> radmin_transcript_exhaustion=<u16> radmin_response_enqueue_failure=<u16> radmin_response_seal_failure=<u16>
```

All five saturate at `UINT16_MAX`. The last three are the existing counters, not new copies. They share the
existing whole-state-clear lifetime: ordinary epoch/root invalidation does not reset diagnostics; explicit
whole-state clear/teardown does. No clear-counter command, JSON schema or mobile status change is added.
The legitimate local/remote ACCEPT `status` byte delta is exactly these fields; existing prefixes and other
handler output stay unchanged. Capture one snapshot per render so individual fields are not mixed across reads.

| Event | Accounting |
| --- | --- |
| Malformed/missing-source/over-cap/not-ready/no-ACL/bad-key/auth-failed input | `inbound_refusal` once per input; never an on-air auth oracle. The Node early missing-source path counts too, without double counting the pure classifier. |
| ID reuse, already-acknowledged execute, session-full, ingress-full or occupied bootstrap reservation | `inbound_refusal` once; rendering its fixed reply does not count the input again. |
| Open peer already pending, or all three pairs active | `inbound_refusal` once; no new staging, dispatch or deadline refresh. |
| Open peer in cooldown, or no available pair because cooldown consumes the remaining capacity | `open_rate_refusal` once **instead of** inbound refusal; no response work is manufactured by rate rejection. |
| Staged open line rejected by shared validation/authority | `inbound_refusal` once at that decision; initial staging did not already count it. |
| Successful admit/replay/bootstrap/control admission; ACK released/duplicate/unknown/premature | No inbound/rate increment. ACK semantics and existing verdict distinctions remain unchanged. |
| Already-admitted authenticated command outcome, or a busy/executing/preparation-failed control result | No second intake refusal count; its typed result reports the outcome. The explicitly deferred open validation row above is the exception because open authority is first decided there. |
| Authenticated transcript reservation unavailable | Existing `transcript_exhaustion` semantics; open capture pressure never increments this authenticated-pool counter. |
| Any response encoder refuses before transport (auth, open or fixed reply) | Existing `response_seal_failure` once; no enqueue attempt/count. A valid input whose reply is unbuildable is not additionally an inbound-refusal event. Its historical name covers codec/pre-seal refusals, including clear-frame encoding; it does not imply that open output is encrypted. |
| Encoded response offered to the shared return sender yields checked refusal | `response_enqueue_failure` once per sender call, including `radmin_reply_no_dst` before lower-level transport submission; no additional inbound-refusal count for a present-zero source. Preserve pending transcript/open cursor; fixed/bootstrap replies keep their one-attempt lifetime. A queued or parked ownership result is success even if its raw counter is zero. |
| A paced sender defers because TX is already full, or expiry occurs without a sender call | No send-failure count. Bootstrap's existing one-attempt/release is accounted by its checked sender outcome, as specified in §4.2. |

For mixed open pressure, precedence is: same peer active → inbound; same peer cooldown → rate; a free pair →
admit; no free pair and any cooldown → rate; otherwise all active → inbound. Counters must not create secondary
policy state or alter the refusal's protected rows. Use scalar diagnostic events for failure accounting, not
command text, response text, key, tag or whole-state dumps. Encoding failure after a truthful result never
turns that result into `internal_error`; 7b-1/B374/B375 remains the controlling rule.

Refusal tests that previously required a byte-identical whole session now permit **only the specifically
expected counter delta**. Keep full-state comparisons with that named expected field changed; do not weaken
them into a few selected-field checks. Update touched comments that claim zero state changes on those paths.

## 7. Production/test fence and implementation evidence

At this committed codec base, the expected behavior diff is localized; **predict the actual
`git diff --stat` paths before editing**. No file move or unrelated cleanup joins this feature.

- Core: `lib/core/remote_session.{h,cpp}`, `lib/core/node.{h,cpp}`, `lib/core/node_mac_rx.cpp`. Extend current
  state/expiry/lifecycle/install/sender ownership; new private helpers belong beside their existing authority.
- Firmware: `src/firmware_remote_executor.h`, `src/firmware_commands.cpp`; its declarations header only if the
  binding requires it. Status fields and the existing real executor binding are the only handler/glue changes.
  `fw_main.cpp` only if source validation demonstrates an unavoidable existing-call adjustment; no second loop
  owner. No ACL/inbox handler semantics, NV/runtime store schema, authority row or local UI changes.
- Native: existing remote-session, transcript, Node-session/Node-exec and pure-executor fixtures. New focused
  `test_remote_control.cpp` / `test_remote_open.cpp` cases may share those fixtures; no copied transport harness.
  Update actual Node ABI assertions only after the ruled measurement. Preserve B376's live-route checks/S22.
- Instruments: `tools/probe_inbox_verbs/` for real handler/open/control/status flights; `tools/probe_console_sink/`
  for real scope/ownership/format checks; feature ownership check only if call ownership really changes;
  `tools/probe_board_abi.py` for measured types/pins; mutation harness and directly necessary tool tests. Preserve
  B377's plain pin assignments and strict reader. Regenerate the inventory through its tool; source anchors may
  move, but the **204-row verb/authority surface** should not.
- Coder evidence: `docs/superpowers/evidence/2026-09-08-radmin-slice7b2.md`, with actual base + brief SHA-256,
  revision-4 preflight, predictions, all final sources/untracked inputs, failures, pin arithmetic and frozen
  handoff. Append to the committed historical report; preserve its earlier preflights and §6 owner receipt.

OUT OF FENCE: production codec changes mixed into the behavior commit, simulator edits/anchors, wire-version
change, HAL entropy-provider redesign/B312 closure, new allocation beyond the ruled candidate, private-DM/ACL
policy change, controller carrier/confirmation UI, deferred actions/OTA, invitation fixture B364, unrelated
instrument repairs. Register/design/rulings/MEMORY/bench/QA reports are QA-owned, not coder cleanup targets.

## 8. Wiring and full gate requirements

### 8.1 Native and real-handler proofs

Native uses the real codec/session/Node/RX with a **counting fake executor**; it does not claim real status,
routes, NV or reboot handler behavior. The real `firmware_commands.cpp` and `firmware_inbox.cpp` TUs are owned
by the inbox-verbs probe's executed rows, preserving B367's boundary.

| Obligation | Required discriminating proof |
| --- | --- |
| Safe rollover | Authenticated real flight, zero unacked → exact base-key result/new epoch; old-session request fails, fresh-session succeeds; no NV write; other-slot invariants per §3.2; zero/current-epoch draw refusal. |
| Busy and force | Safe count is the live same-slot count, no work destroyed; ACK changes that count; same-ID retry uses nonce-safe notice; force reports exact abandoned count and wipes only its slot. Force has no local confirmation inference at target. |
| Cross-slot release | Force releases an earlier same-slot transcript with two later other-slot survivors: only permitted shared ranks compact; all other surviving header fields, owned chunks, seen/route/epoch/ingress and pending wire bytes match. Relative send order stays unchanged; include a nonzero cursor. B386's author proof is the existing install boundary, not a real control producer gate. |
| Executing guard | Use real reserve/capturing state with a **labelled paused-executor synthetic fixture**; do not claim a synchronous real firmware handler was naturally interleaved by the radio loop. Both safe/force preserve executing state. |
| Control admission/expiry | Full seen pool does not consume CONTROL; owner/operator partition rules; duplicate control cannot replace a pending route/deadline; full TX causes zero draws/sends while eligible authenticated capture dispatch retains 7b-1 behavior; exact expiry/re-arm; ACK can free capacity despite occupied CONTROL. |
| Negative notices | B379's full→other-slot-rotation→same-key execution reproduction is now safe; busy 2→1 changes nonce; changed request ctl/code/detail/source/slot fails authentication when tampered; no seen/transcript allocation for notices. |
| Acknowledged retry | Exact one-byte authenticated protocol error, correct typed domain, byte-identical repeats, no dispatch/new transcript; changed tag/source cannot redirect it. |
| Open ownership | Mutate original RX/scratch after staging, then dispatch; raw-line buffer/capture overwrite cannot corrupt the command; exact `status`/`routes` only; no auth key needed by the open encoder. |
| Open bytes | Real seam/radio `status` and `routes` equal local output at the same execution snapshot; fake executor separately proves empty, exact bound, bound+1, arbitrary binary bytes and multi-frame carrier cut points. |
| Open pressure/rate | Three open peers admitted, fourth refused; same peer/new ID/route cannot evade bound; no auth/bootstrap borrowing in either direction; cooldown protects window, exact edge permits a new request; spoofed sources do not exceed global starts. |
| Send ownership | Queued, parked-with-zero-counter, checked sender refusal and full queue; nonzero pending seq preserved on codec/send failure, next eligible pass resumes; terminal ownership releases capture to cooldown; expiry wipes an incomplete open result. |
| Counter/status wiring | Reach every new counter through real paths; distinct seeded values render each exact field once; zero and saturation; seal refusal does not enqueue; local and remote status carry same snapshot; CLIENT fields/state/symbols absent. |
| Zero-source boundary | Extend the real-Node N11 distinction: present zero reaches codec/admission and reply construction, then `radmin_reply_no_dst` with no lower-level submission/air or origin fallback. For a valid admitted request, count only `response_enqueue_failure`, once per checked sender call; no inbound/rate/seal increment. Prove open capture/cursor retained until its original expiry, bootstrap reservation released after one attempt, fixed notices uncached and a committed rollover preserved. These new accounting/open/control proofs are implementation obligations; current N11 proves only the existing bootstrap boundary. |
| Secrets/planes | No console/BLE output leak, no keys/tags in diagnostics, first source/route retained; successful same-layer and depth-2/3/4 real return flights use routable nonzero hashes. Absent source is silent and counts once as inbound refusal; present zero follows the separate row above. |
| Compatibility | All 7b-1 B374/B375 immutability/recovery tests, B369 expiry/ACK rules, B370 ACL actor, B371 private-application-DM view, B372 real medium and B376/S22 route discrimination remain effective. |

Map each new decision to native mutation controls or executed real-TU probe controls. Missing counter,
wrong counter, wrapping saturation, wrong notice domain, missing mutable nonce input, route replacement,
epoch mutation before preparation, rollback after a failed send, open/auth pool borrowing, deadline refresh,
double dispatch, cursor advance/reset on failure and scope leakage must all redden a relevant instrument.
Unreachable fault injection must be explicitly synthetic and fixture-only; no production hook.

### 8.2 Full chain and both mutation selectors

Run the full standing chain, then QA independently repeats it on the complete frozen implementation:

1. `pio test -e native`, **then run `./.pio/build/native/program`**. Derive base/final cases and assertions from
   executed output; use filtered per-file XML only for arithmetic, not a whole-suite claim over B364.
2. Build simulator normal/gateway variants; record actual compiler/link actions and the executable hash.
   **B385:** a snapshot overlay preserving old source mtimes can leave baseline objects silently reused;
   use a fresh final build directory or prove actual recompilation of every affected normal/gateway object.
   Source hashes alone do not prove executable provenance. Run all **36** scenarios against current
   `simulation/BASELINE.md` anchors, validate both manifests and compare actual final stream bytes to a fresh
   matching base. If canonical `--compare` refuses a changed `lus_sha256`, retain that refusal and independently
   compare the validated actual streams without rewriting manifests. No remote corpus traffic does not replace
   native/probe wiring proof.
3. Both ABI probes with all controls; all six probes (console-sink, inbox-verbs, firmware-UI, custody-USB,
   BLE-line, features) in default controlled and `--no-neg` modes. No-controls runs are not a gate. Configure
   `MR_LUS_SRC` explicitly for an isolated tree without a sibling simulator; preserve B350's known wording limit.
4. Full tools unittest discovery; inventory `--write`, bare, `--check`; authority checker and six selftests;
   A0 matrix, DataType literal check, both repositories' whitespace checks. Retain the committed codec reference:
   run `2026-09-09-radmin-slice7b2-0-reference.py --compare test/test_remote_codec.cpp --selftest` from its evidence
   path with the independent PyNaCl environment. Preserve all 87 old literals and both admission arrays, four
   comparator controls and the one-byte corruption refusal; 7b-2 must not change their expected bytes/meanings.
   Provide a measured ELF under the private `.pio-measure/` tree so the tools suite's existing real-ELF test
   does not silently skip.
5. Deterministic base/final **gateway then heltec_mobile**, sequentially, same private build paths and fixed
   identity. Outputs must be under that checkout's `.pio-measure/` (B315). Hash and preserve ELFs/payloads;
   attribute RAM, flash, sections, objects and changed symbols without mutating the measured ELF. The normal
   board gate is this pair only; the warning census's own **six-environment** pinned set is the sole exception.
6. Derive **two separate mutation selectors**: (a) every configured `TARGET_SRC` touched, and (b) dependency/
   historical acceptance obligations. Their union is mandatory. The **47 batteries named in 7b-1 QA §4** are
   the explicit historical floor, not a claim that the changed-source set has 47 members. The committed codec
   gate's union is the current starting floor: **712 RED + one known unusable B342**, including its extended
   `radmin2` codec coverage. Do not revert to 7b-1's historical 675 RED. Derive all counts again; add new
   control/open decisions and any newly touched-source batteries. Extend existing
   `radmin7exec`/`radmin7rx` and add focused control/open batteries where needed; real firmware status/seam is
   tested by real-TU probe controls, not a native battery against an uncompiled TU. Record every selected name,
   reason, pattern count, baseline, RED/unusable count and source-restoration hash. Never gate a shorthand subset.

**Known exception:** `sliceBmac` M04/B342 remains its previously recorded unusable control, visibly reported and
never counted RED. No new unusable control is accepted by this brief. B312/B315/B350/B359/B364 remain separate
limitations; do not erase them or weaken a guard. Every failed attempt and corrected rerun remains in evidence.

Required exact report line: **`PIN re-synced? YES — <independently derived base + additions = final>`**.
The final handoff identifies actual HEAD, complete tracked/untracked implementation, brief hash and a freeze.
QA snapshots include every uncommitted and untracked implementation input; builds/mutations cannot overlap
coder edits in that same tree. QA issues PASS/HOLD from its own executions, not the coder's recommendation.

## 9. Predictions, STOP conditions and QA landing

The behavior slice changes core state/logic, so **`lus` should rebuild/change; all 36 streams should remain
byte-identical**. It changes no NV/wire version. Native cases/assertions and affected probe/mutation pins grow
by executed additions only. Inventory remains 204 semantic rows. Status text gains exactly the five ACCEPT
fields; mobile fields and target state remain absent. Warnings are predicted to remain at the existing census
pins; a new warning requires evidence, not a suppression.

Under the approved, measured candidate allocation, predicted Node sizes are native **230896** and gateway **157264**
(current **225920/152288 +4976**); mobile **117912 unchanged**. Gateway RAM prediction is **203956**
(current independently measured **198980 +4976**), subject to final linker alignment/attribution; mobile RAM
**207756 unchanged**. These are explicitly predictions, not authorized replacement pins or fresh board
measurements. The committed codec baseline's independently measured flash is gateway **562748 B** and mobile
**1372992 B**. Gateway flash grows by the behavior implementation; derive and attribute the actual delta.
Mobile flash is predicted unchanged for ACCEPT-only additions; attribute any actual codegen movement, as
7b-1 required. Any extra resident auxiliary
state or capacity change is outside the approved allocation and returns to QA/owner.

The standing relevant 7b-1 STOP text is retained verbatim:

> Any stream delta — STOP.

> A second dispatch of
> one request; a transcript evicted by time or by a newer request; a disruptive handler CALLED under a remote context;
> transcript bytes reaching `mrcon`/BLE; a raw counter reported as admission; a frame sent past a full TX queue —
> STOP.

For this slice, additionally STOP on unexplained/concurrent inputs; an unrecorded owner ruling or stale
post-codec base; nonce reuse for changed response plaintext/AAD; open work borrowing authenticated/bootstrap
storage; a mobile Node/RAM move; epoch publication before preparation succeeds; a count/pin laundered from a
skipped or unusable instrument; or any 7b-3/controller behavior. Open expiry is the explicit bounded,
unauthenticated exception to retained authenticated transcript lifetime, not permission to evict the latter.

On independent PASS, QA closes this slice's findings in place, lands design/tracker/MEMORY status and corrects
the counter/retry/notice agreements in their durable homes. B378/B379 remain open until their respective
implementations and independent gates satisfy their closure requirements. Frame/protocol/manual replacement
remains Slice 9, with the intervening design/codec documentation kept accurate. The real open/control round
trips and on-air flood/recovery observations join **8b's controller/carrier metal gate**; there is no product
controller to run a new bench part now. No synthetic RNG test is hardware qualification. Then prepare 7b-3
against the owner's actual committed result; the owner commits and bench-verifies.
