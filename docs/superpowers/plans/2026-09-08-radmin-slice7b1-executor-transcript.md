<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 7b-1 — the target executor, bounded transcript, authenticated responses, exact-retry replay and ACK release · brief · 2026-09-08

**Current status: SOFTWARE-COMPLETE / INDEPENDENT QA PASS 2026-09-08; final changes remain uncommitted.**
The complete gate and documentation landing are recorded in
[the independent QA report](../evidence/2026-09-08-radmin-slice7b1-qa-gate.md).
The revision-6 dispatch/checkpoint text below is historical; its consumed SHA-256 was
`b68785d099cac9fa85361c4d9c3da62ac8d5b938bfd752d2380391eb93c76b9c`.
This status/addendum does not change the implementation contract consumed by the coder. See §10 for closure.

**Historical dispatch status: REVISION 6 (2026-09-08) — B375 folded in (§0.5): explicit next-pass retry versus exact-request replay,
three counters throughout, completion-time versus send-time failure, and an authorized SYNTHETIC fault through the
REAL Node accounting path. QA/Author text is ready for coder source-validation and resume. Slice 7b-1 remains
INCOMPLETE; the coder's full implementation gate and independent QA gate are PENDING.**

Revision history: rev 5 folds B374 (§0.4, immutable completed transcripts); rev 4 folds B371/B372 and records B373
(§0.3); revs 2/3 fold B365–B370 and their ACK/invalidation/fixture follow-throughs (§0.1/§0.2). Preparation is
committed at `d467787`. **PERMITTED RESUME INPUTS beyond that HEAD:** the existing partial implementation, tests and
tools inventoried in `docs/superpowers/evidence/2026-09-08-radmin-slice7b1-qa-takeover.md` §§1–2 (including all its
untracked inputs); this QA-owned brief, the maintained register, the R-RA-32 source-facts correction in the rulings
ledger, `MEMORY.md`'s resume-contract line, and the QA takeover report; plus the coder's evidence file. Preserve all
of them. Record the actual status and this revision's SHA-256 in the coder evidence before resuming; unexpected
concurrent input remains STOP-1. No clean restart, reset, or HEAD-only snapshot. Binding is by content, without a
self-referential hash pin. The split and measured native/gateway Node re-pins remain ruled by R-RA-34 (§4.9).
Roles: the Quality Agent authors, gates and lands; **the coder is Codex** (validate every `file:line` before editing;
disagreements = STOP-1 preflight report); the owner rules and
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

## 0.1 The coder's preflight findings, verified by QA against source and folded in (revision 2)

| finding | verified fact | where the brief changed |
| --- | --- | --- |
| **B365** — `static_assert` against `remote_body_cap` is impossible | `lib/core/remote_codec.h:298` / `.cpp:369` are runtime functions (the packer authority); a syntax compile reproduces `call to non-'constexpr' function` | §4.1: `kRadminChunkBytes = 206` is a named constant with its derivation; a NATIVE case binds it to the live `remote_body_cap(same-layer carrier) − kRemoteOverheadAuthResponse` (the 201 idiom of Slice 6) |
| **B366** — a `Print` subclass cannot live in a native-tested pure header | native has no Arduino include path (`platformio.ini:67-93`); `src/firmware_commands.h:16` gets `Print` from `<Arduino.h>`; the established boundary is `IAdminLines` (pure) + `AdminPrintLines` (device adapter, `firmware_commands.cpp:290`) | §4.3: the sink is a PURE byte-span interface `IRadminTranscriptSink::append(const uint8_t*, size_t)` in the pure header; `firmware_commands.cpp` adapts it to a `Print` for the seam; arbitrary bytes, no line/formatting contract |
| **B367** — native cannot prove real-dispatcher behaviour | `platformio.ini:78` `test_build_src = no`; `dispatch` (`firmware_commands.cpp:1430`), the status handler (`:861`) and the seam (`:1579`) are not in the native binary | §6: native drives the REAL Node/RX/codec with a FAKE executor (counting, canned output) and proves the transport/transcript/replay/ACK mechanics; every "real handler wrote/was not called" proof (`status` bytes == `dispatch("status")`, NV untouched on an operator's `factory_reset`, `reboot` never reaching the handler) is the inbox-verbs probe's |
| **B368** — an unknown remote verb is `refused/unclassified` at the seam, before any router | `firmware_commands.cpp:1601-1606`: the policy check precedes the router; the probe pins `unknown-verb` → `refused/unclassified` (`probe_main.cpp:1875, :1887-1893`) | §4.5: the mapping keys on the typed reason — `unclassified` → `unknown_command` (the table is generated from the complete dispatcher, so "no policy row" IS "no handler"; the request is still never executed), `authority`/`bad_line` → `refused`, `unmatched` → `unknown_command` |
| **B369** — pool-pressure waiting had no continuation past ingress expiry | `remote_session.cpp:447` stamps the 300 s ingress deadline; `:320-332` expiry wipes the body but keeps the seen row; the retry branch `:701-720` returns `replay_transcript` with nothing to replay — pinned by `test_remote_session.cpp:805` | §4.1/§4.6: ingress expiry of an UNEXECUTED request (seen row `admitted`, `transcript_slot == none`) releases that seen row too — a request that never ran carries no at-most-once obligation ([[B332]] retention applies to EXECUTED rows only); an exact retry then re-admits as a fresh `admit` with a fresh body; the Slice 5 case is rewritten to the new obligation with the old kept visible |
| **B370** — remote `acl set`/`acl remove` would run WITHOUT the acting slot, bypassing the self-slot protection | `AclService::set` (`firmware_admin_acl.h:318`) and `remove` (`:343`) check `AclActor` (`:180`); `acl_verb` passes the DEFAULT actor (`firmware_admin_verbs.h:374, :390`) because the local USB caller has no slot; `CommandContext` (`firmware_command_context.h:10`) carries no slot ⇒ a remote owner could demote/remove ITS OWN slot (design §6.6, `:513`) and invalidate its own session mid-handler | §4.3/§4.7/§5: `CommandContext` gains `uint8_t acl_slot` (0xFF = none; the executor fills it from the seen row); `acl_verb` reads the ACTIVE context's slot and passes `AclActor{present, slot}` to `set`/`remove` — the second context consumer, authorized; local USB stays `AclActor{}` byte-identical; probe rows: a remote owner's self-demote/self-remove → `self_slot`, another slot → ok, last-owner rule intact |

The six are registered (B365–B370). Historical rev-2 next-free was B371; current allocation is §4.10.

## 0.2 The rev-2 review's follow-throughs (no new numbers; B369/B370 amended in the register)

| point | verified fact | where the brief changed |
| --- | --- | --- |
| ACK under pressure | `remote_session.cpp:670-679`: `cmd_response_ack` is a CONTROL domain and an OWNER execute also takes the CONTROL row; with the pool full and an owner waiting in CONTROL, an ACK could not be admitted, so nothing could free a transcript for that waiter; releasing CONTROL for the ACK would discard the waiter's body | §4.6: the ACK is consumed synchronously after authentication with NO ingress reservation and NO effect on any other operation's row; resource-neutral verdicts; owner-waiter and operator-waiter pressure cases |
| invalidation vs the promised `refused` | `remote_session_install` (`:287-290`) installs the new epoch and `invalidate_slot` (`:203-213`) clears the slot's seen and ingress rows (and, in 7b-1, its transcripts); the retired session cannot carry any reply | §4.3/§4.5: the "slot emptied/role changed → `refused`" arm is WITHDRAWN — invalidation ABANDONS pending work (no dispatch, no reply); a stale-view refusal may exist only as a labelled synthetic defensive case |
| the B369 fixture set | the old admission-retention behaviour is pinned by `test_remote_session.cpp` I4 (`:275/:288`), C5 (`:442/:459`), P3 (`:527/:535`), **E2 (`:805`, not E5)**, E5 (`:883/:900`) and `test_node_remote_session.cpp` N9 (`:584/:591`, the REAL Node timer path) | §4.1/§5: every one is rewritten to the new obligation with the old claim visible; capacity proofs (C5/P3) are rebuilt from RETAINED EXECUTED rows (completed/acknowledged), never by relaxing occupancy |

## 0.3 The implementation checkpoint's findings (B371–B373), verified and folded

| finding | verified fact | where the brief changed |
| --- | --- | --- |
| **B371** — §4.7 filtered by STORAGE KIND; the diagnostics R-RA-32 preserves are stored as DM-kind records | `lib/core/inbox.h:26` has only `dm` and `channel`; `lib/core/inbox.cpp:185` writes E2E receipts as `InboxKind::dm` type `DATA_TYPE_E2E_ACK`, `:198` writes custody reports as `InboxKind::dm` type `DATA_TYPE_CUSTODY_FAILURE`; the ONE read classifier is `inbox_record_is_internal(type)` = `data_type_traits(type).internal` (`inbox.h:111`); `console_json.cpp:680` renders the custody one as `custody_failure` | §4.7: the remote view excludes `kind == dm && !inbox_record_is_internal(type)` — the PRIVATE APPLICATION messages — and streams receipts, custody diagnostics, channel posts and the end record; positive executed rows for both diagnostic types + the negative private-DM row, for a remote operator AND a remote owner; the blanket `mark_read dm`/`del_msg dm` refusal stays (`mark_read(dm, seq)` moves the SHARED DM cursor, `inbox.cpp:265`) |
| **B372** — the inbox probe's fake medium truncates every serialized record to EIGHT bytes (pre-existing) | `tools/probe_inbox_verbs/probe_main.cpp:141` `body[8]`, `:151` clamps each append and reports success; production's minimum header is 32 B (`inbox.h:50`) and `inbox.cpp:45` rejects shorter records before any callback ⇒ "3 stored, 0 pulled" — the coder's linked reproduction against the real `inbox.cpp` prints exactly that, and a 273-byte candidate pulls all three | §5: the probe's medium is widened to the named `inbox_record_max_bytes` (273) and REFUSES oversize instead of clamping; a control restoring the 8-byte truncation must redden the positive channel/diagnostic rows; both arms' existing local-output assertions and pins re-derived honestly. An instrument correction only — no production inbox format change |
| **B373** — a comment correction, already applied | base `lib/core/node.h:220` said `admin_session_state()` exposes no secret; it returns the whole `RemoteSessionState`, whose first member is `admin_x_secret[32]` | recorded; the accessor's behaviour, visibility and layout are unchanged; the comment now names the in-process secret and forbids rendering it |

The three are registered (B371, B372 as a pre-existing instrument defect fixed in-slice, B373 closed); historical
rev-4 next-free was B374. R-RA-32's source-facts sentence in the ledger ("DM vs channel vs custody") is corrected
there: there is no third kind — the discriminator is the record's `type` through `inbox_record_is_internal`.

## 0.4 The second checkpoint's finding (B374), verified and folded

| finding | verified fact | where the brief changed |
| --- | --- | --- |
| **B374** — §4.5's "encode error after a truthful result → `internal_error`" would reuse a nonce | the response nonce is deterministic per key, control, request, sequence and source (`remote_codec.cpp:277`); replacing an already-sealed terminal with different plaintext in that nonce space is NONCE REUSE — design §10 case 4: *"never create different response plaintext in the old nonce space"*; the synthetic case reproduces it | §4.4/§4.5: a transcript is IMMUTABLE once completed, before publication; a send-time sealing/encode failure retains the frozen result AND cursor and increments `response_seal_failure`. Rev-6 clarification: the next eligible main-loop pass retries that pending frame; an authenticated exact request retry restarts replay from seq0. `internal_error` is a COMPLETION-time result only (execution or staging failed before a truthful normal result existed — §8.9), never a send-time replacement |

B374 is registered (fold-in); historical rev-5 next-free was B375. Its new counter and Node-path accounting proof
are still missing from the partial implementation. QA's focused synthetic verification is not a slice PASS.

## 0.5 QA takeover follow-through (B375), folded into revision 6

The owner relayed source confirmation and the retry wording after the takeover review. QA adopts it as the
explicit service contract below; no suspension latch, new timer or replacement terminal is introduced.

| confirmed inconsistency | source/authority re-checked | revision-6 disposition |
| --- | --- | --- |
| three counters versus two in the allocation/fence | `remote_session.h:255-256` still has the two pre-B374 fields; `node_mac_rx.cpp:2138` still counts encode refusal as enqueue failure | §4.2/§4.5/§4.8/§4.9/§5 specify all THREE named counters, disjoint failure accounting, and fresh final ABI/board attribution |
| retry-trigger ambiguity | `firmware_remote_executor.h:49` services a pending frame each eligible pass; `remote_session.cpp:1013-1021` resets a completed transcript on authenticated exact retry | §4.2/§4.4: send failure retries the unchanged pending frame next eligible pass; exact request retry restarts from seq0; no latch or timer |
| the existing synthetic proof does not execute Node accounting | `test_remote_transcript.cpp:245` calls the pure encoder with a one-byte buffer; `node_mac_rx.cpp:2124-2126` supplies the full-size buffer | §5/§6.6 explicitly authorize a labelled fixture-only epoch mismatch through the REAL Node sender and real encoder, with restoration, counter/emit discrimination and both retry forms |
| context/handler/resume/numbering drift | `firmware_admin_verbs.h:310` consumes the ACL actor; `firmware_inbox.cpp:22` consumes the view; owner authorizes the preserved partial tree and QA changes | opening and §4.3/§5 name both consumers and all resume inputs; current next-free is B376 (§4.10); old source tables remain explicitly historical |
| completion table omitted execution failure | design §8.9: *"execution or response staging failed before a truthful normal result existed"*; `firmware_remote_executor.h:44` maps `internal_failure` | §4.5 names both completion-time causes; send-time failure cannot change any frozen result |

B375's brief inconsistency is resolved by this revision; coder preflight still validates every claim before
continuing. B374 implementation/proof and the complete gates remain pending. The takeover report preserves the
independent 1/32 and 1/128 focused results and revision-5 findings as history.

## 0. What 7b-1 is, in one line

On the main loop, take one ADMITTED authenticated execute from Slice 5's ingress, reserve a transcript, run it ONCE
through Slice 6's seam under a REMOTE context derived from the ACL slot's role, capture its text into a bounded
transcript, answer with sealed OUTPUT frames and a TERMINAL on the retained return route (paced, every enqueue checked),
replay the identical frames on an exact retry, and release the transcript on the controller's `RESPONSE_ACK` — with
disruptive commands REFUSED (not scheduled) until 7b-3, and remote `pull_inbox` excluding private application DMs.

## 1. Closure bindings — the Slice 7a QA PASS pins (brief §10b)

**Original clean MeshRoute start: `d467787`, the owner's preparation commit on top of `1548e01`. Resume uses the
preserved partial tree and named inputs above, not a fresh clean checkout. Simulator `06746a9`, clean.**
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
| Register | current next free **B376** (B365–B375 registered); this cell is current dispatch metadata, not a Slice 7a measurement |

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

## 3. Historical pre-implementation source state at `1548e01` (V1)

The observations in this table describe the original base. The current partial tree is inventoried by the QA
takeover report; §0.1–§0.5 and §4 govern the amended implementation obligations.

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
inline constexpr uint16_t kRadminChunkBytes      = 206;  // = the largest authenticated response payload: same-layer RPC cap 232 − kRemoteOverheadAuthResponse 26 — BOUND AT TEST TIME to the live remote_body_cap (B365: the cap is a runtime function)
struct TranscriptHeader { uint64_t request_id; uint32_t bytes_total; uint16_t first_chunk; uint8_t frames; uint8_t next_seq_to_send; uint8_t controller_slot; uint8_t seen_index; uint8_t terminal; uint8_t state; uint8_t reserved[…]; };   // 24 B, offsets asserted
struct TranscriptChunk  { uint8_t bytes[kRadminChunkBytes]; uint16_t len; uint16_t next; };                    // 210 B, align 2
```
* Chunks are a free list threaded by `next`; a transcript owns a contiguous logical sequence of ≤ 8 chunks. Frame k
  (k < frames−1) carries chunk k as OUTPUT seq k; the TERMINAL is seq `frames−1` with `[result][detail…]`. A command
  with no output has `frames = 1` (terminal at seq 0, §8.9).
* **Reservation before dispatch (§10 case 1 / §15):** ONE free header AND at least ONE free chunk (the terminal
  needs no chunk, but any output does) — if either is missing, ⛔ no execution: the ingress row stays admitted, the
  §15 `transcript_exhaustion` counter increments, and the request runs when capacity frees (an exact retry meanwhile
  finds `admitted` with `transcript_slot == none`: the classifier may return `replay_transcript`, but there are
  NO replay bytes or second dispatch — it remains the same not-yet-run request).
* **Chunking at the RETURN carrier's cap:** the sink cuts at `remote_body_cap(reply_carrier) − 26` (same-layer 206;
  cross-layer depth d: `cap_d − 26`), computed ONCE per transcript from the retained `ReplyRoute`; ⛔ never at the
  storage size.
* **Truncation:** when the 8-chunk budget (or the pool's free chunks) is exhausted mid-output, the sink stops
  retaining, sets `truncated`, and the terminal is `output_truncated`; the retained prefix is still sent.
* **Release:** only `RESPONSE_ACK` (§4.6) or an epoch-invalidating change (rollover, root change — Slice 5's
  `invalidate_slot`/`_everything`, which now also frees transcripts) releases a transcript; ⛔ never time, ⛔ never
  a newer request (§10: "must not silently evict an unacknowledged transcript").
* **Pool-pressure continuation (B369):** a request waiting for a transcript is bounded by its INGRESS lifetime
  (300 s). When `remote_session_expire` releases an ingress row whose seen row is still `admitted` with
  `transcript_slot == none` — i.e. NEVER EXECUTED — it releases that seen row as well: a request that never ran has no
  at-most-once obligation, so its fingerprint protects nothing ([[B332]]'s retention rule is about EXECUTED rows and
  is unchanged). A later exact retry is then a fresh `admit` with a fresh body. **The existing cases that pin the old
  retention are rewritten to this obligation, the old claim kept visible:** `test_remote_session.cpp` I4 (`:275/:288`),
  C5 (`:442/:459`), P3 (`:527/:535`), E2 (`:805`), E5 (`:883/:900`) and `test_node_remote_session.cpp` N9 (`:584/:591`,
  the real Node timer path). C5/P3 (the full sixteen-row pool, the control reservation surviving seen exhaustion) are
  REBUILT from retained EXECUTED rows (`completed`/`acknowledged` fixtures, which the classifier already handles) —
  ⛔ never by relaxing the expected occupancy, and without crossing into 7b-2's `session_full` producer.

### 4.2 The executor — pure ordering in `src/firmware_remote_executor.h`, binding in `firmware_commands.cpp`

```cpp
struct IRadminTarget {   // pure tests bind a fake; native flights bind their Node, firmware binds g_node
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
   a parked frame is not re-enqueued by ordinary draining). A transport enqueue refusal increments
   `response_enqueue_failure`; an encode/seal refusal increments `response_seal_failure` WITHOUT attempting
   enqueue or incrementing `response_enqueue_failure` (§4.8). Either failure preserves the frozen transcript and cursor and
   consumes this pass's send attempt; the next eligible main-loop pass retries that pending frame. An authenticated
   exact request retry for a completed transcript separately restarts replay from sequence zero. No suspension
   latch, new timer or replacement terminal; `!tx_queue_full()` and the one-frame-per-pass bound still govern.
2. Else if `next_admitted()`: reserve (§4.1) or return; mark `executing`; build the context; run the seam
   (`IRadminExec::run(line, len, ctx, sink)` = `exec_console_line` in firmware); `transcript_complete(result)`.
   ⛔ ONE dispatch per request, ever: the seen row's state is the only guard (`admitted → executing → completed`),
   and an exact retry arriving mid-execution (the RX path) is a `replay_transcript` verdict the executor answers
   after completion.
3. Else nothing. The call is made from `mesh_service_once()` under `MR_FEAT_RADMIN_ACCEPT`, after the RX drain and
   before the sleep gate, so a CLIENT build compiles none of it.

### 4.3 The remote `CommandContext` and the transcript sink

`CommandContext{transport = remote, authority = (role == owner ? remote_owner : remote_operator), physical_presence =
false, request_id, line_max_bytes = remote_command_max_bytes, acl_slot = <the authenticated slot>}` — the role read
from the LIVE ACL image at execution time (`admin_session_state().acl[slot].role`). **Invalidation between admission
and execution ABANDONS the work:** a role change, removal, rollover or root change runs `invalidate_slot`/`_everything`,
which clears the slot's seen, ingress AND (7b-1) transcript rows — the executor then finds nothing to run, nothing is
dispatched, and ⛔ no reply is sent under the retired session (the controller times out and re-bootstraps, design
§13). By construction the live role cannot differ from the admitted one without that invalidation, so there is no
production "stale view" path; a defensive stale-view refusal may exist only as a SYNTHETIC native case and is labelled
so. **B370:** `acl_slot` is a NEW context field (`0xFF` = none; every local caller passes
none), the second context consumer being `acl_verb` (§4.7b).
The sink (B366) is a PURE interface in `src/firmware_remote_executor.h` — `IRadminTranscriptSink::append(const
uint8_t*, size_t)` over `transcript_append` — counting bytes, cutting at the carrier chunk size, dropping everything
after the budget with `truncated = true`; `firmware_commands.cpp` wraps it in a `Print` adapter for the seam (the
`IAdminLines`/`AdminPrintLines` precedent). ⛔ It writes to no other sink; nothing of the transcript reaches `mrcon`
(§11 "not copied to every local interface").
**The active context is published for the duration of the seam call** (`mrfw::CommandContextScope` in
`firmware_command_context.h`: a scoped setter/getter, default LOCAL when no scope is active) so a handler that must
behave differently remotely can read it — the TWO handler consumers in 7b-1 are the inbox view/refusals (§4.7) and
the ACL actor binding (§4.7b). ⛔ Not a global the handler can write; ⛔ not a second dispatch parameter.

### 4.4 The frames and their sending (`lib/core`)

`Node::radmin_send_frame()` selects the oldest pending transcript and seals chunk/terminal `next_seq_to_send` under the slot's CURRENT session key
(`remote_kdf_session(base, epoch[slot])`, derived per call, wiped) as `RemoteRespOpcode::output`/`terminal` with
`response_seq`, and sends it on the retained `ReplyRoute` through the generalized Slice 5 transmitter (same-layer
`send_by_hash(…Plane::GLOBAL, DATA_TYPE_REMOTE_RESP…)` / cross-layer reversed path). The crypto `RemoteSource` is the
ORIGINAL controller source (Slice 5's rule). The admission fact (`SendDispatch` / `CmdCode`) is returned, never the
raw counter ([[B333]]). A frame's plaintext is exactly its retained chunk; the terminal's plaintext is
`[result:u8]` (+ nothing in 7b-1; `scheduled`'s activation detail is 7b-3's).

**B374/B375 immutable send contract:** completion freezes plaintext chunks, terminal and frame count. A send
failure preserves them AND the cursor; the next eligible main-loop pass retries that pending frame. An authenticated
exact request retry for the retained completed transcript restarts replay from sequence zero, without dispatching
again. Admitted/executing requests still have nothing completed to replay (§4.1/§4.2); acknowledged tombstones remain
7b-2's response obligation. There is no suspension latch, new timer or replacement terminal. Queued/parked transport
ownership already counts as publication, even before airtime. Slot/root invalidation still wipes and abandons work.

### 4.5 Outcome → terminal mapping (one table, in code)

| seam outcome | terminal |
| --- | --- |
| `completed` (streamed/buffered) | `completed` (or `output_truncated` when the sink truncated) |
| `unmatched` (a CLASSIFIED verb no surface owns on this build, or empty) | `unknown_command` |
| `refused` with reason `unclassified` (no policy row — the table is generated from the complete dispatcher, so this IS "no handler"; B368) | `unknown_command` — NOTHING executed |
| `refused` with reason `authority` or `bad_line` | `refused` — NOTHING executed |
| the policy row is `disruptive` (and the context is remote) | `refused` — **7b-1 does NOT schedule** (marked MISSING in code: 7b-3 owns `scheduled`); the handler is ⛔ never called, so a remote `reboot` cannot reboot |
| execution or response staging failed before a truthful normal result existed (`internal_failure`, §8.9) | `internal_error` — decided at COMPLETION once, then frozen like any other result |
| a sealing/encode failure at SEND time (B374) | ⛔ no terminal change — frozen transcript and cursor stay; `response_seal_failure` increments per §4.8; no enqueue attempt or enqueue-failure increment; next eligible pass retries the pending frame, while authenticated exact request retry restarts at seq0 |
| a transport enqueue refusal after successful sealing | ⛔ no terminal change — frozen transcript and cursor stay; only `response_enqueue_failure` increments; the same next-pass/exact-request retry rules apply |

### 4.6 `RESPONSE_ACK` — the release (`lib/core/remote_session.cpp`)

Slice 5 admits `cmd_response_ack` into the CONTROL ingress row as `control_admitted` with no effect. 7b-1 changes
that admission for the ACK domain ONLY: **`RESPONSE_ACK` is consumed synchronously inside `remote_session_receive`,
after authentication, WITHOUT any ingress reservation** — it never competes with an owner execute for the CONTROL
row and never touches another operation's row (`SAFE_ROLLOVER`/`FORCE_ROLLOVER` keep Slice 5's reserved-control
admission for 7b-2). Effect: look up (slot, request_id); seen row `completed` → mark `acknowledged`, release its
transcript (header + chunks wiped), verdict `ack_released`; `admitted`/`executing` → `ack_premature` (no change);
already `acknowledged` → `ack_duplicate` (no change); unknown → `ack_unknown` (no change). All four are
resource-neutral. ⛔ The seen row is NEVER cleared by an ACK ([[B332]]): its fingerprint is the tombstone case 4
answers from (7b-2). No ACK-of-ACK; no response to an ACK. **Pressure proof (§6):** the pool full with four retained
transcripts, an OWNER execute waiting in CONTROL (and, separately, an operator waiting in GENERAL) → an ACK for an
earlier transcript is consumed, frees one transcript, the waiter executes on the next service pass, and the waiter's
ingress body is untouched throughout.

### 4.7 The R-RA-32 remote inbox view (`src/firmware_inbox.cpp`)

When `active_command_context().transport == remote`: `pull_inbox` skips exactly the PRIVATE APPLICATION messages —
`e.kind == InboxKind::dm && !inbox_record_is_internal(e.type)` (B371: the filter is by MEANING through the ONE read
classifier, `inbox.h:111`, never by the shared storage kind) — and streams everything else as today: E2E-ack
receipts and custody-failure reports (both stored DM-kind, both `internal`), channel posts and the end record.
`mark_read dm <seq>` and `del_msg dm <seq>` refuse with `{"err":"<verb>","msg":"remote_no_dm"}` (the DM cursor is
SHARED across hidden private messages, `inbox.cpp:265`, so no partial widening); `clear_inbox` is owner-class already.
Local behaviour byte-identical (the default scope is LOCAL). A remote OWNER is bound by the same view (QA reading
recorded at R-RA-32; the owner may overrule at the gate). Executed rows (§6.3): a private DM hidden, an E2E receipt
shown, a custody report shown, a channel post shown, the end record shown — for a remote operator and a remote owner.

### 4.7b The ACL actor on the remote path (B370)

`acl_verb` (`src/firmware_admin_verbs.h`) reads `active_command_context().acl_slot`; when present it passes
`AclActor{true, slot}` to `AclService::set` and `remove`, so the service's EXISTING self-slot protection
(`firmware_admin_acl.h:318`, `:343`; design §6.6) applies to a remote owner: demoting or removing one's own
authenticating slot refuses `self_slot`; another slot follows the last-owner rule as today. The local USB path keeps
`AclActor{}` — byte-identical. ⛔ No new rule in the service; ⛔ no slot inferred from anything but the authenticated
seen row.

### 4.8 Counters (§15) — THREE u16 in the ACCEPT session state block

`transcript_exhaustion`, `response_enqueue_failure`, `response_seal_failure` (B374): each saturates at `UINT16_MAX`.
Exhaustion keeps its existing reservation accounting. Each failed send attempt increments exactly its own failure
counter once unless saturated: encode/seal refusal → `response_seal_failure`, with no transport attempt;
successful sealing followed by transport refusal → `response_enqueue_failure`. Emit the corresponding scalar
`radmin_response_seal_failure` / `radmin_response_enqueue_failure` with `count`, without command/result bytes.
No pending frame or a full TX queue means no send attempt and no send-failure-counter increment. Their console surface
is 7b-2's (`status`). No other new session-block state beyond the transcript pool and the three counters.

### 4.9 ★ RULED — R-RA-34 (owner, 2026-09-08): the split and the Node ABI re-pin

The transcript pool is ACCEPT-only Node state: 4 × 24 + 8 × 210 = **1776 B** (+ the THREE counters and alignment;
the exact figure is MEASURED on all three ABIs by `probe_board_abi.py`'s compile-and-read, never inferred).
Native and gateway `sizeof(Node)` move by that amount; `heltec_mobile` is the UNMOVED control (as in R-RA-31).
Board RAM: gateway ≈ +1776 (+ counters + pad); mobile ±0. **RULED (R-RA-34):** *"Authorize the 7b-1 transcript pool
and measured native/gateway Node re-pins; mobile remains unchanged."* The owner's budget note is binding: 1776 B is
the POOL alone — the three counters and the alignment are measured on top, never predicted into the pin; mobile's Node
size AND board RAM must both read unchanged. Re-measure the FINAL three-counter layout and board RAM; the earlier
two-counter checkpoint is historical, even if the added field fits existing padding. R-RA-34 already authorizes
measured counter/alignment overhead; revision 6 changes no owner allocation ruling.

### 4.10 Numbering — current: the coder's proposals start at **B376** (B375 folded into revision 6).

## 5. Fence

- `lib/core/remote_session.{h,cpp}`: the transcript types/pool, the reservation/append/complete/release API, the
  ACK effect, the THREE counters (§4.8), the layout asserts (block size re-derived and asserted), `invalidate_*` extended to
  free transcripts. `lib/core/node.h` / `node.cpp` / `node_mac_rx.cpp`: the executor-facing Node API
  (`radmin_next_admitted`, `radmin_reserve_transcript`, `radmin_transcript_append/complete`, `radmin_send_frame`),
  the generalized transmitter (the bootstrap keeps its behaviour byte-for-byte: same emits, same release), the
  member growth + ledger; ⛔ no new timer id, no wire change, no `NodeConfig` field.
- NEW `src/firmware_remote_executor.h` (pure: `IRadminTarget`, `IRadminExec`, `IRadminTranscriptSink`, `radmin_service_once`,
  the outcome→terminal table — ⛔ no Arduino include); `src/firmware_command_context.h` (`acl_slot` field,
  `CommandContextScope` + `active_command_context()`); `src/firmware_admin_verbs.h` (B370: the actor from the active
  context in `set`/`remove`; `radmin3verbs` joins the union);
  `src/firmware_commands.cpp` (the seam publishes the scope; the `g_node` binding; the `IRadminExec` binding);
  `src/fw_main.cpp` (ONE call in `mesh_service_once` under `MR_FEAT_RADMIN_ACCEPT` — a seventh census site, the
  `ownership.py` entry authorized up front); `src/firmware_inbox.cpp` (§4.7).
- Simulator: ⛔ no edit (no new `.cpp`).
- Tests: NEW `test/test_remote_transcript.cpp` (pure pool/sink/ordering), NEW `test/test_node_remote_exec.cpp` (the
  two-endpoint exchange plus the labelled SYNTHETIC Node-path seal-refusal proof, §6.6), NEW
  `test/test_firmware_remote_executor.cpp` (the pure executor with fakes);
  `test/test_remote_session.cpp` (+ ACK cases; I4/C5/P3/E2/E5 rewritten per §4.1), `test/test_node_remote_session.cpp`
  (fixture extension including §6.6's temporary test-only fault and restoration; N9 — the real Node expiry path —
  rewritten per §4.1),
  `test/test_custody_receive_g.cpp` + `tools/probe_board_abi.py` (the re-pin, from measurements), the 0e mirror
  files only if a pin names the old block size.
- Tools: `tools/probe_ui_model_mutations.py` (batteries: `radmin7transcript`, `radmin7exec`, `radmin7rx`; PIN);
  `tools/probe_inbox_verbs` (the executed end-to-end rows, both arms — the CLIENT arm asserts the executor is
  ABSENT; **B372: the probe's fake NV/inbox medium widened from 8 bytes to the named `inbox_record_max_bytes` = 273,
  refusing oversize rather than clamping, with a control that restores the 8-byte truncation and must redden the
  positive channel/diagnostic rows; both arms' pins re-derived**); `tools/probe_console_sink` (structural: the scope is set by the seam only, the executor is main-loop only,
  the inbox filter reads the scope); `tools/probe_features/ownership.py` (the one `fw_main` entry); the ruled table
  + header + inventory only if a verb changes (none expected).
- Evidence `docs/superpowers/evidence/2026-09-08-radmin-slice7b1.md`.
- ⛔ OUT OF FENCE: open execute responses, rollover, `session_full`/`session_busy`/`PROTOCOL_ERROR` producers,
  deferred actions/`scheduled`, OTA reporting, counters on `status`, the controller side (8a), the companion, any
  handler except the inbox view/refusals (§4.7) and ACL actor binding (§4.7b); production fault-injection hooks,
  suspension latches or new timers; `simulation/BASELINE.md`, the register/bench/design/rulings/MEMORY/QA report
  (the last documents are QA-owned permitted resume inputs, not coder edit targets).

## 6. Wiring proof

1. **Native two-endpoint (the Slice 5 fixture extended; B367: a FAKE executor with canned output and a call
   counter — native holds no real dispatcher):** an authenticated execute from controller slot 1 → the executor runs
   the fake ONCE → OUTPUT frames + TERMINAL on the fake radio, decoded under the session key with the ORIGINAL
   controller source, concatenated in seq order == the fake's canned bytes; a canned empty output → ONE terminal at
   seq 0; a canned >1648-byte output → `output_truncated` with the prefix intact; the fake reporting each seam
   outcome/reason → the mapped terminal (§4.5, exhaustively); the same sealed request re-flown → the counter still 1,
   the frames re-sent BYTE-IDENTICAL (the deterministic nonce) from seq 0; `RESPONSE_ACK` → the transcript freed, the
   seen row `acknowledged` and RETAINED; a second ACK / an ACK for an unknown id → no change; pool exhaustion (4 in
   flight, none ACKed) → the fifth stays admitted, `transcript_exhaustion` +1, executes after one ACK; ingress expiry
   of an unexecuted request → its seen row released, a retry re-admits (B369); pacing → exactly one frame per service
   call, none while `tx_queue_full()`; the cross-layer return path at depths 2–4 → every frame on the reversed path;
   a `refused` enqueue → frozen transcript/cursor unchanged, only enqueue-failure counter +1, next eligible pass
   retries the pending frame without a new request; the SYNTHETIC seal-failure accounting proof is separately
   specified in §6.6 (not claimed as a naturally reachable radio fault); an epoch invalidation →
   transcripts freed; the chunk bound == live `remote_body_cap` − 26 (B365).
2. **Pure:** the sink's cut points at 206 and at a cross-layer cap; truncation edge (exactly 8 chunks vs one byte
   more); the outcome→terminal table exhaustively; the executor's one-unit-per-call ordering with counting fakes.
3. **Inbox-verbs probe (the REAL seam, REAL `g_node`, fake HAL — B367: every real-handler proof lives here):** a
   remote `status` end to end — the sealed frames on the fake radio decoded by the probe and their concatenation
   byte-identical to `dispatch("status")` into a local buffer; `version`; an unknown verb → `unknown_command` (B368);
   an operator slot on `factory_reset confirm` → `refused` with the fake NV untouched; `reboot` from an owner →
   `refused` and the fake HAL never reset (the handler is never called); a remote owner's `acl remove <own slot>` /
   `acl set <own slot> operator` → `self_slot`, another slot → ok, the last owner protected (B370); the remote
   `pull_inbox` keeps both diagnostic types, channels and the end record while hiding private application DMs
   for operator AND owner (§4.7); remote `mark_read dm` / `del_msg dm` refuse; local `pull_inbox` and local `acl`
   byte-identical to their Slice 6/3 rows; CLIENT arm: no executor symbol.
4. **Console-sink structural:** the scope is published only inside `exec_console_line`; the inbox filter reads the
   scope, never a global flag; the executor call is main-loop only and ACCEPT-gated.
5. Corpus 36/36 byte-identical; ABI re-pin measured; boards attributed; census at pins. Derive the mutation union
   from BOTH selectors: `TARGET_SRC` against every touched source (including `node.cpp` and the ACL handler), and
   the codec/authority/inbox dependencies plus historical gate obligations. Add `radmin7transcript`, `radmin7exec`
   and `radmin7rx`, then gate the complete union. A shorthand list or the changed-source selector alone is not
   the acceptance set (standing gate; takeover evidence §5).
6. **B374/B375 — explicitly authorized SYNTHETIC fault through the REAL Node accounting path:** extend the existing
   `TargetNode` fixture in `test_node_remote_session.cpp`, with its named case in `test_node_remote_exec.cpp`.
   Admit/authenticate and complete normally through the real Node plus counting executor. A scoped TEST-ONLY
   mutation of the fixture's non-const Node state, accessed via its existing `admin_session_state()` view, may
   temporarily mismatch `epoch[slot]` against the retained seen epoch. Direct fixture access (including a
   test-only `const_cast`) is authorized here; no production accessor or fault hook is added. This makes the REAL
   `remote_transcript_encode` refuse at its epoch guard (`remote_session.cpp:568-570`) when the REAL
   `Node::radmin_send_frame()` is called through the normal service binding. Label the case **SYNTHETIC**: a real
   epoch installation invalidates/wipes this work, so it cannot create that retained mismatch. Restore the injected
   field before recovery; do not call live invalidation and then pretend the transcript survived.

   Prove, relative to the faulted fixture before the attempt: retained chunks, terminal, frame count and cursor
   unchanged; `response_seal_failure` +1 and its named scalar emit; `response_enqueue_failure` unchanged; no response
   handed to transport or aired; fake executor still called once. Prove both send-failure counters' saturating behavior and
   that full-queue pacing makes no failure attempt. Use a nonzero pending sequence so reset-versus-preserve is
   discriminating: after removing the fault, the NEXT eligible main-loop pass resumes that sequence without any
   controller retry. Then an authenticated exact request retry restarts at seq0; every replayed RPC body is
   byte-identical to its original, with no second dispatch. Exercise the real sender's classification with mutation
   controls (missing/wrong counter, cursor advance/reset on failure, attempted enqueue on encode failure); the pure
   one-byte-buffer test alone is not this proof. Existing native/probe radio rows separately own actual admission,
   airtime and real-handler claims. No claim of a production nonce-reuse vulnerability or failing crypto provider.

## 7. Gates — the standing chain + the union; predictions first

`lus` changes (both variants), 36/36 identical; native +N; Node native/gateway re-pinned from measurement, mobile
117912 unmoved; gateway RAM + the pool (attributed by symbol), mobile ±0 (+0 flash expected beyond `mesh_service_once`
codegen); console-sink/inbox pins +rows derived; census unchanged; inventory 204; tools sweep 343 + new tests.
Evidence carries `PIN re-synced? YES — <derivation>`; §6.5 defines the required complete mutation union.

## 8. STOP conditions

1. Dirty base / concurrent input / out-of-fence path — STOP. **Resume clarification:** the opening's named partial
   implementation and QA inputs are explicitly authorized; this STOP applies to an unexplained input or concurrent
   change, not those preserved inputs. 2. Any stream delta — STOP. 3. A second dispatch of
one request; a transcript evicted by time or by a newer request; a disruptive handler CALLED under a remote context;
transcript bytes reaching `mrcon`/BLE; a raw counter reported as admission; a frame sent past a full TX queue —
STOP. 4. The ABI re-pin without the owner's word; a mobile Node/RAM move — STOP. 5. Any 7b-2/7b-3 behaviour
(open responses, rollover, protocol error, scheduling) — STOP.

## 9. QA landing after PASS

Close the register rows this slice creates; design §19.1 gains rows 7b-1/7b-2/7b-3; bench Part 57b-1 (the remote
`status` round trip on metal needs 8a's controller — recorded as DEFERRED to 8b's metal, no part yet); then the
7b-2 brief.

## 10. Independent QA closure — 2026-09-08

QA independently gated the complete frozen handoff at `146569a33b6451852555ac9ffcfd5079cd843393`, including
all uncommitted inputs, against original attribution base `d467787f5e02836ad19b79624d97729c81ebacf8`.
Native **2883/127709/0**, corpus **36/36 byte-identical**, mutation union **675 RED / 1 known unusable B342**,
tools **343 OK / zero skips**, all six probes with controls and no-controls modes, both ABI probes, inventory,
authority/selftests, checkers, deterministic board comparisons and the six-environment warning census completed.
The real Node SYNTHETIC proof passed **142 assertions**; B374/B375's three-counter and two-retry contract holds.

Gateway Node **+1784 B**, RAM **+1792 B**; mobile Node and RAM **unchanged**. Gateway flash **+7520 B** and
mobile flash **+260 B** are fully attributed in the QA report. The latter corrects the earlier zero-flash/
`mesh_service_once` prediction; it does not change R-RA-34's mobile Node/RAM constraint. The required inventory
regeneration moved source-line anchors only: all **204 rows** and semantic content match the base. QA accepts
that mechanical refresh under the standing inventory gate; the authority table/header and verb set did not change.

B365–B377 are closed in place. B376 restores the unchanged S22 route control (**25/25 RED** session battery);
B377 restores strict-reader-compatible pin assignments (**343 tools tests OK**). B315/B342/B350/B359/B364 remain
separate open instrument/fixture limits. The design's split rows, retry/retention/counter wording, maintained
register and MEMORY now record the landing. No bench part is added: the target `status` round trip remains
**DEFERRED to 8b's metal gate**. Session control/open responses/status exposure remain 7b-2; deferred actions
remain 7b-3. The next authoring step is the 7b-2 brief against the owner's ensuing commit; no next-slice
implementation is dispatched by this closure. Full receipts and exact source bindings are in the independent
QA report. Coder evidence and production inputs are preserved unchanged; the owner commits.
