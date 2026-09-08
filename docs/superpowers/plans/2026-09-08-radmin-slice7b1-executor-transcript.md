<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 7b-1 — the target executor, bounded transcript, authenticated responses, exact-retry replay and ACK release · brief · 2026-09-08

**Status: DRAFT rev 1 — written and source-verified by the Quality Agent as Author. DISPATCHABLE at the owner's commit
that ADDS this file (`git add` it — rev 1 of the 7a brief was left untracked once): the coder's preflight records
`git rev-parse HEAD` (parent chain containing `1548e01`, "slice 7a activation config"), this file's SHA-256 and an EMPTY
status in both repositories; binding by content + clean status, no self-referential hash pin. The split and the
native/gateway `sizeof(Node)` re-pin are RULED — R-RA-34 (§4.9).**
Roles: the Quality Agent authors, gates and lands; **the coder is Codex** (validate every `file:line` before editing;
disagreements = STOP-1 preflight report — the pattern has caught 19 real gaps since Slice 6); the owner rules and
commits.

**The 7b split (Author decision, RULED by R-RA-34):** design §19 item 7b is one slice with fourteen obligations. It is
delivered as THREE separately gateable sub-slices, each with its own commit and attribution: **7b-1 (this brief)** —
the authenticated executor, the bounded transcript, OUTPUT/TERMINAL responses on the retained return route,
exact-retry replay, `RESPONSE_ACK` release, the R-RA-32 remote inbox view; **7b-2** — session control (safe/force
rollover, `session_full`/`session_busy` terminals, `PROTOCOL_ERROR{already_acknowledged}`, shared-credential behaviour)
and the OPEN execute path (open responses, rate limit, staging release) plus the §15 counters on `status`; **7b-3** —
the deferred-action owner (`scheduled` terminal, the two `DeferredActionRecord` rows, activation through the 7a
resolver with ACK-earlier, the 300 s outer deadline, local-only OTA reporting). Nothing in 7b-1 forecloses 7b-2/7b-3;
what each leaves MISSING is stated in code.

Authority: design §8.7 (:862-873), §8.9 (:891-955), §8.10 (:956-994), §10 (:1136-1181), §11 (:1181-1211), §15
(:1603-1670), §16 (:1670-1715); rulings R-RA-13, R-RA-21, **R-RA-22** (4 `TranscriptHeader`, 8 `TranscriptChunk`),
R-RA-25/28 (caps), R-RA-27, **R-RA-32** (`pull_inbox` remote no-DM view), R-RA-33; Slice 5 (`lib/core/remote_session.{h,cpp}`,
the verdicts with no producers), Slice 6 (`CommandContext`, the seam's remote arm), Slice 7a (nothing consumed here).

## 0. What 7b-1 is, in one line

On the main loop, take one ADMITTED authenticated execute from Slice 5's ingress, reserve a transcript, run it ONCE
through Slice 6's seam under a REMOTE context derived from the ACL slot's role, capture its text into a bounded
transcript, answer with sealed OUTPUT frames and a TERMINAL on the retained return route (paced, every enqueue checked),
replay the identical frames on an exact retry, and release the transcript on the controller's `RESPONSE_ACK` — with
disruptive commands REFUSED (not scheduled) until 7b-3, and the remote `pull_inbox` showing everything except DMs.

## 1. Closure bindings — the Slice 7a QA PASS pins (brief §10b)

**MeshRoute base: the owner's commit of this brief, on top of `1548e01` (clean). Simulator `06746a9`, clean.**
7b-1 edits `lib/core` ⇒ `lus` REBUILDS; prediction **36/36 byte-identical streams** (no scenario airs `0xA0`/`0xA1`;
no new `on_init` action).

| binding | Slice 7a closure entry |
| --- | --- |
| Native | **2855 / 121999 / 0**; `PIN_CASES, PIN_ASSERTS = 2855, 121999` |
| Corpus / `lus` | 36/36, keystone `32afbf11` / 269517 / 0; `lus` md5 `9f5a4b9872c4fe407c319f767b9236b6` |
| Node ABI | native 224136/8 · heltec_mobile 117912/8 · gateway 150504/8 — **7b-1 MOVES native + gateway (§4.9), mobile is the control** |
| Console sink · inbox verbs | 76 structural / 136 controls · ACCEPT 370/41, CLIENT 368/44 |
| UI · custody · BLE line · features | 223 · 27/10 · 40/8 · 9/120/59 + ownership 40 (nine files; `fw_main.cpp` six sites) |
| Tools sweep · inventory · checker | 343 OK · 204 rows · PASS + 6/6 |
| Census · boards | 173/178/177/177/182/182; gateway 197188 / 555292 / 285 · heltec_mobile 207756 / 1372732 / 329 |
| Register | next free **B365** |

## 2. Verbatim authority pins

Design §10 case 1: *"ID absent and capacity available: reserve the seen record and enough response/transcript capacity
for a truthful terminal before dispatch, dispatch exactly once, retain the resulting transcript and terminal, then send
it."* Case 2: *"ID present and request tag identical, transcript unacknowledged: resend the original transcript from
sequence zero; never dispatch again."* *"The response-transcript pool may be smaller than the seen-request table, but it
must not silently evict an unacknowledged transcript and then execute its request again. Under pressure the target
refuses a new command before execution."*

Design §11: *"The remote sink is another bounded `Print` implementation following the existing sink seam. It captures
the same bytes the selected command handler writes locally and divides them according to the selected return carrier's
packer-derived RPC-body cap minus the authenticated/open response overhead … If a handler writes beyond the
per-operation limit, the sink marks the operation truncated, stops retaining further bytes, and terminates with
`output_truncated`. It never silently presents a partial response as complete."* *"Sensitive authenticated plaintext is
not copied to every local interface or global diagnostic console."*

Design §8.9: *"A successful command that prints nothing returns a terminal frame at sequence zero."* §8.7: the
authenticated response envelope is 26 bytes; *"Its output limit is `carrier_rpc_body_cap - 26` and may differ by return
carrier."* §8.10: *"The target ACK lets the target release the potentially large encrypted transcript while retaining a
compact already-executed record for replay safety."* §15: *"no execution unless space for its mandatory terminal result
… has first been reserved; paced draining into the existing TX queue, with every enqueue result checked."*
*"the target may still dispatch only one at a time."* §13: a disruptive command *"cannot activate its disruptive side
effect inside the handler before a truthful acceptance result has entered the reply path."*

R-RA-22: *"16 `SeenRequestRecord`, 4 `TranscriptHeader`, 8 `TranscriptChunk`, …"* R-RA-32: *"pull_inbox … we allow it for
operator … implement pull_inbox which would show all with exception of dm"*.

## 3. Verified source state at `1548e01` (V1)

| Source verified | Consequence |
| --- | --- |
| `lib/core/remote_session.h`: `SeenRequestRecord` {state `SeenState` admitted/executing/completed/acknowledged, `transcript_slot` = `kRadminNoTranscript`, `result_code`, `request_tag[16]`, `controller_slot`, `source_hash`} + `ReplyRoute` beside it; `IngressOperationHeader` {`seen_index`, `body_slot`, `partition`, `ctl`, `route`} + `IngressBodySlot` (233 B); `RemoteRxResult` verdicts `admit` / `replay_transcript` / `already_acknowledged` / `control_admitted` … all CLASSIFIED with **no consumer**; `remote_session_receive` at `remote_session.cpp:472` | 7b-1 is the consumer of `admit`, `replay_transcript` and the `response_ack` control; `already_acknowledged` / `session_full` / rollover stay 7b-2's; the seen row's `state`/`transcript_slot`/`result_code` fields exist and are unused |
| `remote_session.cpp:700-731` the classifier keys (slot, request_id); case-2 replay is a VERDICT; `commit_admit` copies the body into `body[ii]` and stamps the ingress `expires_at_ms` (staging lifetime 300 s) | the executor consumes the ingress body BEFORE its expiry and releases the row after execution; the seen row outlives it |
| `lib/core/node_mac_rx.cpp:2050-2170` (Slice 5): `rx_remote_cmd_accept` → `remote_session_receive` → `radmin_send_reply` (the ONLY transmitter: the bootstrap; same-layer `send_by_hash(… Plane::GLOBAL, DATA_TYPE_REMOTE_RESP, suppress_intro, &dsp)` / cross-layer `originate_layer_path(rev+1, n−1, …, DATA_TYPE_REMOTE_RESP)`; the staging row released on the CHECKED outcome) | the frame transmitter is GENERALIZED (one function: send one REMOTE_RESP body on a `ReplyRoute` to a source hash, returning the admission fact) and reused by the bootstrap unchanged in behaviour |
| `lib/core/node.h:1221` `bool tx_queue_full() const` PUBLIC; `kTxQueueCap = 8` (`:3131`); `enqueue_data` SILENTLY drops when full (`:388`) — callers must check first | the pacing rule: at most ONE response frame enqueued per main-loop service pass, and only when `!tx_queue_full()`; the enqueue result (`SendDispatch` / `CmdCode`) is checked and classified |
| `lib/core/remote_codec.h:67-73` `RemoteRespOpcode::output` = 0x0, `terminal` = 0x1; `:159-166` overheads (auth response 26); `:191-196` `RemoteMessage.response_seq`; `:285` the nonce binds `outer_type ‖ ctl ‖ request_id ‖ response_seq ‖ source_hash` — DETERMINISTIC per (key, request, seq) | re-sealing the SAME plaintext at the same seq yields byte-identical ciphertext ⇒ the transcript retains PLAINTEXT chunks and seals at send time; a replay is provably identical (§6) |
| `src/firmware_commands.cpp:1561` `exec_console_line(line, len, fmt, stream, reply, cap, const CommandContext&)` (Slice 6): validator → remote-only policy check (`refused` + `authority`/`unclassified`) → router → parser; `LineExec.outcome ∈ completed/unmatched/refused`; `scheduled`/`internal_failure` have no producer | the executor calls the seam with `LineFormat::text`, a transcript `Print`, and `CommandContext{remote, <authority from role>, false, request_id, remote_command_max_bytes}`; the outcome maps to the terminal (§4.5) |
| `src/firmware_command_authority.h` `command_policy_lookup(line, len)` → `CommandPolicy{cls, disruptive}` (Slice 6) | the executor reads `disruptive` and REFUSES such rows for remote contexts in 7b-1 (7b-3 schedules them) |
| `src/fw_main.cpp:1362` `mesh_service_once()` — the main loop; the legacy CLIENT drain block (`#if MR_FEAT_RADMIN_CLIENT`) sits inside it | the executor's service call (ACCEPT-only) is placed beside it, on the main loop, never on the RX path |
| `src/firmware_inbox.cpp:16-36` `PullCtx` + `inbox_pull_cb` writes `write_inbox_dm` for `InboxKind::dm` and `write_inbox_channel` otherwise; `handle_mark_read`/`del_msg` parse `<dm|chan> <seq>` | the R-RA-32 remote view filters `InboxKind::dm` records when the ACTIVE context is remote; a remote `mark_read dm …` refuses |
| `tools/probe_inbox_verbs/run.sh:258-274` compiles `firmware_commands.cpp` + `firmware_inbox.cpp` + all `lib/core` + `lib/console` with a fake HAL and the REAL `g_node` | the executor's binding lives in `firmware_commands.cpp` so the probe drives the real end-to-end path (a remote `status` on the fake radio → sealed frames captured); `fw_main.cpp` only CALLS it |
| `test/test_node_remote_session.cpp:139-289` the Slice 5 two-endpoint fixture (`TargetNode`: `provision`, `learn_peer`, `flight`, `pump_tx`, `last_rpc_body`, `bootstrap_request`; `controller_base_key`) | reused; the coder adds an `auth_execute_request` builder (session key from base + epoch) — the controller side stays a FIXTURE (8a builds the real one) |
| `test/radmin_0e_candidate_types.h:201-218` the 0e candidates `TranscriptHeader` (24 B) and `TranscriptChunk` (`kCandidateResponseChunkBytes` = 231 − 26 = 205 + len + next); R-RA-28 ruled the same-layer RPC cap **232** ⇒ the largest chunk is **206** | the production rows are derived from the codec's caps, `static_assert`ed, and their size MEASURED; the 0e candidate stays history |

## 4. Author decisions

### 4.1 The transcript pool — Node ACCEPT state (`lib/core/remote_session.h`, extended)

```cpp
inline constexpr uint8_t  kRadminTranscriptSlots = 4;    // R-RA-22
inline constexpr uint8_t  kRadminChunkSlots      = 8;    // R-RA-22
inline constexpr uint16_t kRadminChunkBytes      = 206;  // = the largest authenticated response payload: same-layer RPC cap 232 − kRemoteOverheadAuthResponse 26; static_assert against remote_body_cap
struct TranscriptHeader { uint64_t request_id; uint32_t bytes_total; uint16_t first_chunk; uint8_t frames; uint8_t next_seq_to_send; uint8_t controller_slot; uint8_t seen_index; uint8_t terminal; uint8_t state; uint8_t reserved[…]; };   // 24 B, offsets asserted
struct TranscriptChunk  { uint8_t bytes[kRadminChunkBytes]; uint16_t len; uint16_t next; };                    // 210 B, align 2
```
* Chunks are a free list threaded by `next`; a transcript owns a contiguous logical sequence of ≤ 8 chunks. Frame k
  (k < frames−1) carries chunk k as OUTPUT seq k; the TERMINAL is seq `frames−1` with `[result][detail…]`. A command
  with no output has `frames = 1` (terminal at seq 0, §8.9).
* **Reservation before dispatch (§10 case 1 / §15):** ONE free header AND at least ONE free chunk (the terminal
  needs no chunk, but any output does) — if either is missing, ⛔ no execution: the ingress row stays admitted, the
  §15 `transcript_exhaustion` counter increments, and the request runs when capacity frees (an exact retry meanwhile
  finds `admitted` with `transcript_slot == none` and is NOT a replay — it is the same not-yet-run request).
* **Chunking at the RETURN carrier's cap:** the sink cuts at `remote_body_cap(reply_carrier) − 26` (same-layer 206;
  cross-layer depth d: `cap_d − 26`), computed ONCE per transcript from the retained `ReplyRoute`; ⛔ never at the
  storage size.
* **Truncation:** when the 8-chunk budget (or the pool's free chunks) is exhausted mid-output, the sink stops
  retaining, sets `truncated`, and the terminal is `output_truncated`; the retained prefix is still sent.
* **Release:** only `RESPONSE_ACK` (§4.6) or an epoch-invalidating change (rollover, root change — Slice 5's
  `invalidate_slot`/`_everything`, which now also frees transcripts) releases a transcript; ⛔ never time, ⛔ never
  a newer request (§10: "must not silently evict an unacknowledged transcript").

### 4.2 The executor — pure ordering in `src/firmware_remote_executor.h`, binding in `firmware_commands.cpp`

```cpp
struct IRadminTarget {   // the Node seam the executor drives; the native suite binds a fake, firmware binds g_node
    virtual bool next_admitted(RadminIngressView& out) = 0;          // the oldest ADMITTED execute (seen_index, slot, request_id, body span, route, reply_carrier)
    virtual bool reserve_transcript(uint8_t seen_index, uint16_t chunk_bytes) = 0;
    virtual void transcript_append(uint8_t seen_index, const uint8_t* p, size_t n) = 0;   // the sink's storage; sets truncated when the budget ends
    virtual void transcript_complete(uint8_t seen_index, RemoteTerminal result) = 0;      // seen row -> completed; ingress row released
    virtual bool tx_queue_full() = 0;
    virtual RadminSend send_next_frame() = 0;                          // seals + enqueues the next unsent frame of the oldest transcript with frames pending; returns queued/parked/refused/none
};
void radmin_service_once(IRadminTarget&, IRadminExec&);   // ONE unit of work per call, in this order:
```
1. If any transcript has a frame pending and `!tx_queue_full()`: `send_next_frame()` — exactly ONE frame per pass
   (§15 pacing); `parked` counts as pending-until-resolution (the transport owns the copy; the cursor advances —
   a parked frame is not re-sent by us); `refused` leaves the cursor and increments `response_enqueue_failure`
   (the transcript stays; the controller's retry triggers a replay).
2. Else if `next_admitted()`: reserve (§4.1) or return; mark `executing`; build the context; run the seam
   (`IRadminExec::run(line, len, ctx, sink)` = `exec_console_line` in firmware); `transcript_complete(result)`.
   ⛔ ONE dispatch per request, ever: the seen row's state is the only guard (`admitted → executing → completed`),
   and an exact retry arriving mid-execution (the RX path) is a `replay_transcript` verdict the executor answers
   after completion.
3. Else nothing. The call is made from `mesh_service_once()` under `MR_FEAT_RADMIN_ACCEPT`, after the RX drain and
   before the sleep gate, so a CLIENT build compiles none of it.

### 4.3 The remote `CommandContext` and the transcript sink

`CommandContext{transport = remote, authority = (role == owner ? remote_owner : remote_operator), physical_presence =
false, request_id, line_max_bytes = remote_command_max_bytes}` — the role read from the LIVE ACL image at execution
time (`admin_session_state().acl[slot].role`); a slot that became empty since admission → terminal `refused`.
The sink is a `Print` subclass over `transcript_append` (`src/firmware_remote_executor.h`, pure): it counts bytes,
cuts at the carrier chunk size, and drops everything after the budget with `truncated = true`. ⛔ It writes to no
other sink; nothing of the transcript reaches `mrcon` (§11 "not copied to every local interface").
**The active context is published for the duration of the seam call** (`mrfw::CommandContextScope` in
`firmware_command_context.h`: a scoped setter/getter, default LOCAL when no scope is active) so a handler that must
behave differently remotely can read it — the ONE consumer in 7b-1 is the inbox (§4.7). ⛔ Not a global the handler
can write; ⛔ not a second dispatch parameter.

### 4.4 The frames and their sending (`lib/core`)

`Node::radmin_send_frame(seen_index)` seals chunk/terminal `next_seq_to_send` under the slot's CURRENT session key
(`remote_kdf_session(base, epoch[slot])`, derived per call, wiped) as `RemoteRespOpcode::output`/`terminal` with
`response_seq`, and sends it on the retained `ReplyRoute` through the generalized Slice 5 transmitter (same-layer
`send_by_hash(…Plane::GLOBAL, DATA_TYPE_REMOTE_RESP…)` / cross-layer reversed path). The crypto `RemoteSource` is the
ORIGINAL controller source (Slice 5's rule). The admission fact (`SendDispatch` / `CmdCode`) is returned, never the
raw counter ([[B333]]). A frame's plaintext is exactly its retained chunk; the terminal's plaintext is
`[result:u8]` (+ nothing in 7b-1; `scheduled`'s activation detail is 7b-3's).

### 4.5 Outcome → terminal mapping (one table, in code)

| seam outcome | terminal |
| --- | --- |
| `completed` (streamed/buffered) | `completed` (or `output_truncated` when the sink truncated) |
| `unmatched` (no router/parser owner, or empty) | `unknown_command` |
| `refused` (`authority` / `unclassified` / `bad_line`) | `refused` — NOTHING executed |
| the policy row is `disruptive` (and the context is remote) | `refused` — **7b-1 does NOT schedule** (marked MISSING in code: 7b-3 owns `scheduled`); the handler is ⛔ never called, so a remote `reboot` cannot reboot |
| the ACL slot emptied / role changed since admission | `refused` |
| execution or sealing failed after a truthful result existed (encode error) | `internal_error` |

### 4.6 `RESPONSE_ACK` — the release (`lib/core/remote_session.cpp`)

Slice 5 admits `cmd_response_ack` into the CONTROL ingress row as `control_admitted` with no effect. 7b-1 gives it
its effect in `remote_session_receive`: after authentication, look up (slot, request_id); if the seen row is
`completed` → mark `acknowledged`, release its transcript (header + chunks wiped), release the control ingress row,
verdict `ack_released`; if `admitted`/`executing` (the ACK outran the terminal? impossible by construction — the
controller ACKs only after a full transcript) → `ack_premature` (no change); if unknown → `ack_unknown` (no change).
⛔ The seen row is NEVER cleared by an ACK ([[B332]]): its fingerprint is the tombstone case 4 answers from (7b-2).
No ACK-of-ACK; no response to an ACK.

### 4.7 The R-RA-32 remote inbox view (`src/firmware_inbox.cpp`)

When `active_command_context().transport == remote`: `pull_inbox` skips every `InboxKind::dm` record (channel posts,
custody notices and the end record are streamed as today); `mark_read dm <seq>` and `del_msg dm <seq>` refuse with
`{"err":"<verb>","msg":"remote_no_dm"}`; `clear_inbox` is owner-class already. Local behaviour byte-identical (the
default scope is LOCAL). A remote OWNER is bound by the same view (QA reading recorded at R-RA-32; the owner may
overrule at the gate).

### 4.8 Counters (§15) — two u16 in the session state block, `transcript_exhaustion` and `response_enqueue_failure`,
incremented as described; MR_EMIT'd; their console surface is 7b-2's (`status`). No other new state.

### 4.9 ★ RULED — R-RA-34 (owner, 2026-09-08): the split and the Node ABI re-pin

The transcript pool is ACCEPT-only Node state: 4 × 24 + 8 × 210 = **1776 B** (+ the two counters and alignment;
the exact figure is MEASURED on all three ABIs by `probe_board_abi.py`'s compile-and-read, never inferred).
Native and gateway `sizeof(Node)` move by that amount; `heltec_mobile` is the UNMOVED control (as in R-RA-31).
Board RAM: gateway ≈ +1776 (+ counters + pad); mobile ±0. **RULED (R-RA-34):** *"Authorize the 7b-1 transcript pool
and measured native/gateway Node re-pins; mobile remains unchanged."* The owner's budget note is binding: 1776 B is
the POOL alone — the two counters and the alignment are measured on top, never predicted into the pin; mobile's Node
size AND board RAM must both read unchanged.

### 4.10 Numbering — the coder's proposals start at **B365**.

## 5. Fence

- `lib/core/remote_session.{h,cpp}`: the transcript types/pool, the reservation/append/complete/release API, the
  ACK effect, the two counters, the layout asserts (block size re-derived and asserted), `invalidate_*` extended to
  free transcripts. `lib/core/node.h` / `node.cpp` / `node_mac_rx.cpp`: the executor-facing Node API
  (`radmin_next_admitted`, `radmin_reserve_transcript`, `radmin_transcript_append/complete`, `radmin_send_frame`),
  the generalized transmitter (the bootstrap keeps its behaviour byte-for-byte: same emits, same release), the
  member growth + ledger; ⛔ no new timer id, no wire change, no `NodeConfig` field.
- NEW `src/firmware_remote_executor.h` (pure: `IRadminTarget`, `IRadminExec`, the sink, `radmin_service_once`, the
  outcome→terminal table); `src/firmware_command_context.h` (`CommandContextScope` + `active_command_context()`);
  `src/firmware_commands.cpp` (the seam publishes the scope; the `g_node` binding; the `IRadminExec` binding);
  `src/fw_main.cpp` (ONE call in `mesh_service_once` under `MR_FEAT_RADMIN_ACCEPT` — a seventh census site, the
  `ownership.py` entry authorized up front); `src/firmware_inbox.cpp` (§4.7).
- Simulator: ⛔ no edit (no new `.cpp`).
- Tests: NEW `test/test_remote_transcript.cpp` (pure pool/sink/ordering), NEW `test/test_node_remote_exec.cpp` (the
  two-endpoint exchange), NEW `test/test_firmware_remote_executor.cpp` (the pure executor with fakes);
  `test/test_remote_session.cpp` (+ ACK cases), `test/test_node_remote_session.cpp` (fixture extension),
  `test/test_custody_receive_g.cpp` + `tools/probe_board_abi.py` (the re-pin, from measurements), the 0e mirror
  files only if a pin names the old block size.
- Tools: `tools/probe_ui_model_mutations.py` (batteries: `radmin7transcript`, `radmin7exec`, `radmin7rx`; PIN);
  `tools/probe_inbox_verbs` (the executed end-to-end rows, both arms — the CLIENT arm asserts the executor is
  ABSENT); `tools/probe_console_sink` (structural: the scope is set by the seam only, the executor is main-loop only,
  the inbox filter reads the scope); `tools/probe_features/ownership.py` (the one `fw_main` entry); the ruled table
  + header + inventory only if a verb changes (none expected).
- Evidence `docs/superpowers/evidence/2026-09-08-radmin-slice7b1.md`.
- ⛔ OUT OF FENCE: open execute responses, rollover, `session_full`/`session_busy`/`PROTOCOL_ERROR` producers,
  deferred actions/`scheduled`, OTA reporting, counters on `status`, the controller side (8a), the companion, any
  handler except the inbox filter, `simulation/BASELINE.md`, the register/bench/design/rulings.

## 6. Wiring proof

1. **Native two-endpoint (the Slice 5 fixture extended):** an authenticated `status` execute from controller slot 1
   → the executor runs once → OUTPUT frames + TERMINAL on the fake radio, decoded under the session key with the
   ORIGINAL controller source, concatenated in seq order == the bytes `dispatch("status")` writes into a local
   buffer (byte-identical — the §11 property); `version` (short) → frames as derived; a command printing nothing →
   ONE terminal at seq 0; a >1648-byte output (`peers all` on a filled book, or a test verb) → `output_truncated`
   with the prefix intact; unknown verb → `unknown_command`; an operator slot on `factory_reset confirm` → `refused`
   and the fake NV untouched; `reboot` from an owner → `refused` and the fake HAL never reset (7b-1's fence, in code);
   the same sealed request re-flown → NO second dispatch (a counting exec fake), the frames re-sent BYTE-IDENTICAL
   (the deterministic nonce) from seq 0; `RESPONSE_ACK` → the transcript freed, the seen row `acknowledged` and RETAINED;
   a second ACK / an ACK for an unknown id → no change; pool exhaustion (4 in flight, none ACKed) → the fifth stays
   admitted, `transcript_exhaustion` +1, executes after one ACK; pacing → exactly one frame per service call, none
   while `tx_queue_full()`; the cross-layer return path at depths 2–4 → every frame on the reversed path; a
   `refused` enqueue → cursor unmoved, counter +1, replay later; an epoch invalidation → transcripts freed.
2. **Pure:** the sink's cut points at 206 and at a cross-layer cap; truncation edge (exactly 8 chunks vs one byte
   more); the outcome→terminal table exhaustively; the executor's one-unit-per-call ordering with counting fakes.
3. **Inbox-verbs probe (the REAL seam, REAL `g_node`, fake HAL):** a remote `status` end to end (the sealed frames on
   the fake radio decoded by the probe); the remote `pull_inbox` shows channel records and no DM; a remote
   `mark_read dm 1` refuses; local `pull_inbox` byte-identical to its Slice 6 rows; CLIENT arm: no executor symbol.
4. **Console-sink structural:** the scope is published only inside `exec_console_line`; the inbox filter reads the
   scope, never a global flag; the executor call is main-loop only and ACCEPT-gated.
5. Corpus 36/36 byte-identical; ABI re-pin measured; boards attributed; census at pins; union: every `lib/core`
   battery on the touched files (`radmin5session`, `radmin5rx`, the RX six, `radmin2codec`), the three new ones, and
   the inbox/authority dependencies.

## 7. Gates — the standing chain + the union; predictions first

`lus` changes (both variants), 36/36 identical; native +N; Node native/gateway re-pinned from measurement, mobile
117912 unmoved; gateway RAM + the pool (attributed by symbol), mobile ±0 (+0 flash expected beyond `mesh_service_once`
codegen); console-sink/inbox pins +rows derived; census unchanged; inventory 204; tools sweep 343 + new tests.
Evidence carries `PIN re-synced? YES — <derivation>`.

## 8. STOP conditions

1. Dirty base / concurrent input / out-of-fence path — STOP. 2. Any stream delta — STOP. 3. A second dispatch of
one request; a transcript evicted by time or by a newer request; a disruptive handler CALLED under a remote context;
transcript bytes reaching `mrcon`/BLE; a raw counter reported as admission; a frame sent past a full TX queue —
STOP. 4. The ABI re-pin without the owner's word; a mobile Node/RAM move — STOP. 5. Any 7b-2/7b-3 behaviour
(open responses, rollover, protocol error, scheduling) — STOP.

## 9. QA landing after PASS

Close the register rows this slice creates; design §19.1 gains rows 7b-1/7b-2/7b-3; bench Part 57b-1 (the remote
`status` round trip on metal needs 8a's controller — recorded as DEFERRED to 8b's metal, no part yet); then the
7b-2 brief.
