<!-- QA/Author: Claude (revision 1); production coder: Codex; owner rules and commits -->
# Remote-admin v2 Slice 8b — the mobile controller carrier (`-a`, custody consumption, retry timing)

**Revision 6 — 2026-09-18: B416 folded (the binding veto gets its seam: a call-scoped Node lookup handed to the firmware observer) — READY FOR CODER RESUME AND IMPLEMENTATION.**
Revision 5 (`1501ab7c…`) → 6 changes: §4 splits the ACK observation into the pure kind/counter matcher (which also
returns the candidate's target hash) and a firmware-side binding veto fed by `Node::id_bind_find_by_hash` through a
call-scoped function pointer + context (the `RemoteEntropyFn` / `client_console_drops` idiom, never stored); the
`fw_main` call gains those two arguments; §2 no longer excludes the one `via_home` line; §6/§7 aligned; B414 is
repaired by the coder. R-RA-46..49 are unchanged. Historical: revision 5 (`1501ab7c…`) ← revision 4 (`f82429fe…`) changes: the carrier forces the shared wrapper path with ONE defaulted `send_by_hash`
parameter (`via_home`), so every controller request leaves as `MOBILE_SEND` to the home — one counter space, the
300-s tier, the B278 row (B413); the ACK matcher keys on `(ctr, kind-specific identity)` never the counter alone
(§4), with the pre-existing E2E-ring wildcard ambiguity registered as B415, out of scope; the fence gains the two
`send_by_hash` lines and the S45 allow-list repair (B414); §7 gains the known/unknown/claimed × two-target proof.
R-RA-46..49 are unchanged. The coder's preserved partial implementation is the base to resume from. Historical:
revision 4 (`f82429fe…`) ← revision 3 (`ed2be13f…`) changes: the observer call moves AFTER the existing BLE fanout (B411: text, then generic JSON,
then the controller event — one order, stated once), and `RemoteClientSend` gains the typed `correlation_full` outcome so
send-time E2E-ring pressure is reported as `correlation_full` and waits, never as `radio_enqueue_failed` (B412);
§1/§3/§4/§6/§7 aligned. R-RA-46..49 are unchanged. Historical: revision 3 (`ed2be13f…`) ← revision 2 (`fcad6fff…`) changes §1 (four seam rows corrected/added), §3 (`-a` is execute-only in every half — B410),
§4 (observations are matched by pure core functions and emitted at the existing `fw_main` push fanout through a
call-scoped adapter, formats carry `ctr` and a distinct `reporter` — B409), §6, §7 and one §9 STOP item. R-RA-46..49
are unchanged. Historical: revision 2 settled R1–R4 as recommended (R-RA-46/47/48/49); revision 1 (`9789ed31…`) was
the first issue.
Base **`c07b77f`** (owner commit `8` = the 8a+8c freeze, [independent QA PASS](../evidence/2026-09-18-radmin-slice8ac-qa-gate.md)),
clean; simulator **`6585649`** (owner commit `8`: the `remote_client.cpp` source-list line), clean. The coder pins this
brief by content hash plus the inventory of the QA documents that announce it (register, design, tracker, MEMORY).
A brief under implementation is frozen (P4); a mid-slice ruling lands in the ledger and register and is re-pinned at
a checkpoint. This is the last `lib/core` slice of the arc that touches the carrier; 9 and 10 follow (R-RA-6, P6).

## 0. What 8b is, in one paragraph

8a+8c left the controller complete but airless: `DeviceRemoteCarrier` returns `unavailable`, so every board `remote`
refuses with `carrier_unavailable` and transmits nothing. 8b binds the controller to the ONE product carrier the design
allows (§14: a mobile delegating through its static home), on the existing application-DM path (`send_by_hash` /
`delegate_send_layer` → `MOBILE_SEND` wrapper → the home re-originates a plaintext `Plane::GLOBAL` `REMOTE_CMD` with
`SOURCE_HASH` = the mobile's stable hash; the target answers to that hash through the home's last mile). It adds the
per-request `-a` carrier ACK end to end (the target's E2E ACK after admission, B278 §9.2), consumes the mobile's
existing E2E-ACK / ACK-timeout / translated-custody pushes as carrier observations that never touch the RPC result, and
fixes the two timing derivations R-RA-44 deferred (silent-request resend, ACK-debt cadence). No codec, no new verb,
no new resident state (predicted), no `wire_version`, no corpus movement.

## 1. What binds this slice (links, not quotes)

Design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`: §4 (carrier facts: 232-B app-DM
cap, 231-B wrapper body, `send_remote_*` forbidden, `0xA0/0xA1` internal types raise no generic `send_*` lifecycle),
§5 (carrier adapter = the `send_by_hash`/`do_send` path with `app_dm=true`, `Plane::GLOBAL`, `0xA0/0xA1`), §8.11 /
`remote_body_cap` (the one cap authority), §9 (exact-byte retry), §10 (dedup: executed fingerprints and transcripts are
never time-evicted, so an exact resend can never re-execute), §13 (timeout honesty, epoch compare, "must not
automatically retry a non-idempotent command" AFTER the outcome deadline), §14 + §14.1 (carrier split; custody is a
separate unauthenticated observation; B112 statement), §15 (`-a` = one delegated-correlation row; bounded pending
table), §16 (flow), §19 item 8b, §19.2 controls. Rulings ledger `docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`:
R-RA-12 (home is static ⇒ wrapper is GLOBAL), R-RA-13 (`SOURCE_HASH` mandatory), R-RA-15 + addendum (`-a` optional,
default off; requests the existing E2E ACK, a correlation row and custody feedback), R-RA-17/26/27 (roles), R-RA-20/23
(the named-derivation shape for timing), R-RA-22/45 (controller rows and the 4512-B block), R-RA-28 (DST_HASH always
reserved), R-RA-42 (any further verb needs its own row — 8b adds none), R-RA-44 (resend timing is 8b's), **R-RA-46/47/48/49
(2026-09-18: the four §8 rulings, settled as recommended)**. B278 design
`docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md` §9.2 (remote admin must request the E2E ACK
on its delegated carrier and the target must send it only after admission; B278 never parses an RPC body). Working
rules CLAUDE.md C1–C4, D1–D6, P4–P7; `docs/CODE_GUIDELINES.md`.

| Verified seam at `c07b77f` (pin by symbol; lines are hints) | Consequence for 8b |
| --- | --- |
| `lib/core/remote_client.h`: `IRadminCarrier{tx_queue_full, submit_request(route, source, carrier, bytes, e2e_ack), submit_ack(...)}`, `RemoteClientSend{queued, full, unavailable}` (**rev 4 adds `correlation_full`** — a typed outcome, no resident byte, no wire — B412), `RemoteClientRoute{target_hash, hops[3], hop_count}`, `RemoteClientPendingCore` (`next_retry_ms`, `retries` exist and are UNUSED; `flags` bit `kAck`), `RemoteClientAck{next_retry_ms, retries}`, `remote_client_carrier(route, type, carrier)` (carrier 1 = typed mobile wrapper ⇒ `outer=MOBILE_SEND`, `enclosed=type`, `path_depth = hop_count`) | The interface is final except for that one added enum value; 8b supplies the production implementation and the timing. `RemoteClientRequest::carrier` is already 1 on boards (`handle_remote_client` passes `1`) and 0 in the 8ac loopback. |
| `lib/core/remote_client.cpp`: `send_ready` (calls `carrier.tx_queue_full()` then `submit_request`; `queued` advances `bootstrap_ready→bootstrap_wait`, `request_ready→response_wait`, `compare_ready→compare_wait`, `rollover_ready→rollover_wait`; `full`⇒`radio_enqueue_failed`, `unavailable`⇒`carrier_unavailable`; at start any error releases the row, in `remote_client_service` only `carrier_unavailable`/`authentication_failed` complete it and every other refusal leaves the row in its `*_ready` phase for the next pass — **B412:** a send-time E2E-ring-full therefore needs its own typed outcome, since `full` means a TX-queue refusal), `remote_client_service` (ACK-debt loop: every due row is re-submitted, `next_retry_ms = now + kAckRetryMs` where `kAckRetryMs = cascade_requeue_base_ms` = 5 s, **unbounded**), `remote_client_expire` (only `outcome_deadline_ms` = `kOutcomeMs` = `e2e_ack_deadline_xl_ms` 300 s; ACK debt never expires — by design §8.10 it is wiped only by `epoch_update`), `remote_client_start` (`correlation_full` when `correlation >= in.correlation_free`) | **B410:** `send_ready` passes `e2e_ack` for ANY `request_ready` row carrying `kAck`, and the two rollover opcodes ride `request_ready` too, so `remote t -e -a rollover [confirm]` would request an E2E ACK (the coder's 36-check characterization); §3/§4 make `-a` execute-only in every half. The four `*_wait` phases have no resend today; with a real carrier the ACK-debt loop would air a 25-B wrapper every 5 s until the target's next epoch. Both are R-RA-44's deferred timing: §5 and rulings R2/R3. |
| `lib/core/node.h`: `remote_client_correlation_free()` counts free `_deleg_acks` rows — the HOME-side delegated-flight ring (`deleg_ack_put` is called only by a home re-originating for a hosted mobile); on a client-only mobile it is constant 8 (the pending table has 4 rows, so `correlation_full` is unreachable). The ring a mobile's `-a` send actually arms is `_pending_e2e_acks` (`cap_pending_e2e_acks` = 8): `enqueue_data` (`node_mac.cpp`, the `app_dm && E2E_ACK_REQ` arm) arms it with wildcard key 0 and the 300-s XL tier for a `MOBILE_SEND` wrapper, and `delegate_send_layer` arms it for the cross-layer wrapper (key = `dst_hash`, `is_xl`, the same tier); a full ring at arm time is NOT refused there — it emits `e2e_ack_untracked_ring_full` and the send flies untracked; the console's `send`/`send_layer` verbs pre-refuse with `e2e_ack_ring_full()` (`node.cpp`, `err_ack_ring_full`) | **B408** (QA, 2026-09-18, owned by 8b): the controller's `-a` bound must be the ring the carrier arms. 8b re-points `remote_client_correlation_free()` to the free `_pending_e2e_acks` rows and the carrier pre-checks `e2e_ack_ring_full()` before an `-a` submit (U1: the console's predicate), so an `-a` request can never fly untracked. |
| `lib/core/node_hashlocate.cpp` `Node::send_by_hash` (arm order: TEAM plane; **the authoritative-binding arm** `id_bind_find_by_hash(key_hash32) == authoritative ⇒ do_send(resolved_id, …, plane, out_dispatch)` — a DIRECT typed send, taken on a registered mobile whenever it has heard the target's own beacon (`node_beacon.cpp` installs an authoritative binding for every static sender); then the registered-mobile wrapper arms 1–3: `reply_to_hash == 0 && _cfg.is_mobile && _my_mobile_reg.active` ⇒ `do_send(home_id, [type][body], flags\|DATA_FLAG_MS_ENCLOSED_TYPE, crypt, override_dst_hash=target, type=MOBILE_SEND, Plane::GLOBAL, out_dispatch)`; the park/flood arms come after, so a registered mobile never parks). **B413:** the direct arm gives each request the TARGET's per-destination counter (`Node::next_ctr(dst)`, `node_mac.cpp`), so two known targets both get ctr 1; arms the E2E ring with `key = dst` and the 60-s same-layer tier; and reserves NO home-side `_deleg_acks` row, so `-a` custody feedback cannot exist on it; `lib/core/node_mac.cpp` `Node::delegate_send_layer(dst_hash, hops, hop_count, enclosed_type, body, len, flags)` (returns the wrapper ctr, 0 = no home / bad path / overflow / queue full — it stores directly, so a non-zero return IS admission); `Node::radmin_send_response` (`node_mac_rx.cpp`) = the target's own carrier binding: same-layer `send_by_hash(source_hash, body, len, 0, CryptIntent::off, 0, 0, Plane::GLOBAL, DATA_TYPE_REMOTE_RESP, suppress_intro=true, &dsp)` with **`SendDispatch` as the only admission fact**, cross-layer `originate_layer_path` | The controller's binding MIRRORS `radmin_send_response` (U1): same-layer rows → `send_by_hash(target_hash, bytes, len, flags, CryptIntent::off, 0, 0, Plane::GLOBAL, DATA_TYPE_REMOTE_CMD, /*suppress_intro=*/true, &dsp, /*via_home=*/true)` — **rev 5 adds the trailing defaulted `bool via_home = false`; `true` skips the authoritative-binding arm (one added `!via_home &&` conjunct), so a registered mobile ALWAYS wraps; every existing caller is byte-identical (B413)**; cross-layer rows (`hop_count 1..3`) → `delegate_send_layer(target_hash, hops, hop_count, DATA_TYPE_REMOTE_CMD, bytes, len, flags)`. `flags` = `DATA_FLAG_E2E_ACK_REQ` iff the execute request carries `-a`; 0 for bootstrap/rollover/ACK. The home's same-layer arm strips the enclosed type and re-originates `send_by_hash(type=0xA0, reply_to_hash=mobile, mobile_ctr=pa.ctr)`; the XL arm re-originates through `originate_layer_path(type=0xA0, override_source_hash=mobile)`. Nothing in those arms changes. |
| `lib/core/node.h` `mobile_registered()`, `mobile_home_id()`, `tx_queue_full()` (public), `e2e_ack_ring_full()` (**private**, `node.h:~2596` — reachable by the Node-member carrier) | The carrier's pre-checks. Not registered ⇒ `unavailable` (the design's only topology needs a static home; no send is attempted); TX queue full ⇒ `full`; `-a` with a full E2E ring ⇒ `full` (the controller reports it as `correlation_full`, the typed refusal that already exists). |
| `lib/core/node_mac_rx.cpp` `Node::rx_remote_cmd_accept` (ACCEPT): refuses without `SOURCE_HASH`, builds carriers, `remote_session_receive`, `radmin_send_reply`, re-arms expiry, returns — the generic post-ACK E2E arm (`if (pa.flags & DATA_FLAG_E2E_ACK_REQ) { CROSS_LAYER ? send_xl_ack(*ui, pa.ctr) : send_e2e_ack(origin, pa.ctr, sender_hash); }`, ~`:2888`) sits AFTER the owner switch and is never reached for an RPC | **A `-a` request receives no E2E ACK today.** 8b adds the ACK inside `rx_remote_cmd_accept`, after `remote_session_receive`, for the admitted verdicts only (§4), as the same two calls in the same order as the generic arm (U1). The corpus carries no `0xA0`, so this ACCEPT-side arm is byte-inert there. |
| `lib/core/node_mac_rx.cpp` push sites the mobile already raises: `send_e2e_acked` (`pu.dst = pa.origin, pu.ctr = acked, pu.sender_hash`, ~`:2641` — after the home's `ctrH → ctrM` translation the mobile sees its own wrapper ctr), `send_failed{e2e_ack_timeout}` (`node_mac.cpp` `e2e_ack_deadline_fire`, `push_send_failed(e2e_ack_timeout, dst, ctr)`), `custody_failure` (`custody_failure_receive`, translated arm: `tail->mobile_ctr`, `rec.failed_type`, `rec.failed_origin`, `rec.reporter_layer`, `tail->target_kind/target_value`, `tail->original_reporter`, then `enqueue_push`) | **B409:** core has no path to a sink at these sites (no resident adapter — R-RA-45; `Node::next_push` copies and removes each row; the delivery adapters are call-scoped in `firmware_commands.cpp`), so the pushes are NOT hooked here. Core gains two PURE matchers; the ONE emitter call sits at the existing `fw_main` push fanout (§4). `Push` does not grow; the pushes, the inbox record and the custody line are unchanged. |
| `src/fw_main.cpp`: the push fanout `while (g_node.next_push(pu)) { … mr_ui_on_push route …; switch (pu.kind) { … case custody_failure: mrfw::print_custody_failure(mrcon, pu); … } if (mrble::connected()) { write_push → mrble::tx_line } }` (~`:1570–1832`); `tools/probe_custody_usb/run.sh` U23–U25 pin that `fw_main` delegates the custody arm exactly once and holds no parser; `src/firmware_remote_client.h` is compiled by the `radmin8verbs` battery and the inbox CLIENT probe; `lib/core/node.cpp` `Node::next_push` | The board glue for observations: exactly ONE new call per drained push, **after the `if (mrble::connected()) { … }` fanout block and before the loop's closing brace** (B411: the plain-text line and the generic JSON push both precede the controller event on every transport; `LineSink` flushes on newline, so nothing can be deferred), under `#if MR_FEAT_RADMIN_CLIENT`, passing `pu` whole to a header function that parses, matches and emits (§4); its call-scoped BLE adapter (`LineSink local_ble(ble_sink)`, the `:1840` idiom) is part of that one glue site. U23–U25 stay true; the custody arm's single call stays. |
| `lib/core/protocol_constants.h`: `send_defer_ttl_ms` 30 s, `e2e_ack_deadline_ms = 2 * send_defer_ttl_ms` (60 s, same-layer round trip), `gateway_send_giveup_ms` 150 s, `e2e_ack_deadline_xl_ms = 2 * gateway_send_giveup_ms` (300 s, the delegated/cross-layer round trip = the controller's outcome bound and `delegated_custody_ttl_ms`), `cascade_requeue_base_ms` 5 s / `cascade_requeue_backoff_cap_ms` 30 s / `cascade_requeue_max` 3 (the MAC's own requeue shape); `lib/core/remote_session.h` `radmin_staging_lifetime_ms` (300 s); `remote_session_expire` never time-evicts executed fingerprints or transcripts | The only inputs the §5 derivations may use (R-RA-20/23 shape: named constants, factors stated, no literal). |
| `src/firmware_commands.cpp`: `DeviceRemoteCarrier` (the stub), `handle_remote_client` / `remote_client_service_once` (build `RemoteClientServices{…, carrier, client_entropy, …, g_node.remote_client_correlation_free(), /*carrier_kind=*/1}`), `client_console_drops`; `src/firmware_remote_client.{h,cpp}`: `RemoteClientDelivery` (USB `> remote <id16> …` lines, BLE events through `console::write_event` into a 245-B buffer), `remote_client_status` (`radmin_client_*` fields) | The stub is REPLACED by the Node-side carrier (§3); the delivery adapter gains the carrier-observation line/event (§4) and `status` gains `radmin_client_ack_debt`. No new verb, so the authority table, header and 208-row inventory are unchanged (structural control). |
| `test/test_node_remote_session.cpp` `TargetNode` (provisioned ACL, `pump_tx`), `ControllerLoopCarrier` (the 8ac loopback: bytes copied by hand — "production board carrier remains unavailable"); `test/test_custody_receive_g.cpp` `GPair` (two static Nodes over the real MAC, `hop_2_to_1`, `send_typed`), `S4Chain` (M1 registered at node 1 via `test_set_my_mobile_reg` + `s3_host`, `hop_to_m1`, `deliver_lastmile` = node 2 originates hash-addressed and node 1 last-miles to M1); `test/support/test_hal.h` `TestHalBase` | The 8b native proof is the REAL chain: M1 (controller, real binding) → node 1 (home) → node 2 (target with `TargetNode`'s ACL) and back through the last mile, using these fixtures; shared pieces may be lifted into `test/support/` (test-only headers, not `lib/core`, so P7's simulator line is not triggered). The loopback fixture may stay as a unit control but is no longer the carrier proof. |
| `docs/2026-07-30-open-bug-register.md` **B112** (OPEN / CORE: `ctr != 0` does not imply enqueue; a `MOBILE_SEND` wrapper's first hop can be ACKed before the home's re-origination fails; owner 2026-08-05: "separate core slice"); design §14.1 / §19 item 8b: "only after B112 is fixed"; `protocol.md`: "B112 remains a separate, non-blocking first-hop-ACK issue" | **RULED R-RA-46** (§8). At the ONE site 8b uses, the admission fact is already `SendDispatch` (UI-16 N6b) / a truthful store return (`delegate_send_layer`), not the counter; the home's outward refusal reaches the mobile only as the uncorrelated `send_failed{no_route}` one-shot (`deleg_fail`, ctr 0) — the controller consumes NOTHING from it and claims NOTHING beyond "wrapper stored in my TX queue". |

Baselines (8a+8c gate, 2026-09-18): native 2947/193734/0; corpus 36/36, s18 `32afbf11`/269517/0 (`s07`/`s22`
carry the eleven custody receipts that must not move); Node 235248 native / 122176 mobile / 157344 gateway; gateway
RAM 204036 / flash 574896; heltec_mobile RAM 211772 / flash 1392832; xiao_mobile 176604 / 696940; inventory 208;
union floor 59 batteries / 918 configured; census 173/178/177/177/182/182.

## 2. Scope

**IN:** the production carrier binding (§3); the `-a` path end to end and the three carrier observations (§4); the
resend and ACK-debt derivations (§5); B408; the contract section, goldens and probes for the one new local event; the
real-chain native proof; Part 57c reservation. **OUT:** codec; target session/admission logic beyond the E2E-ACK arm;
custody generation (v1 boundary unchanged); the bodies of `send_by_hash` (except the ONE `via_home` parameter and guard conjunct of §3/§6) / `do_send` /
`enqueue_data` / `delegate_send_layer` / the home's wrapper arms (a `SendDispatch*` out-parameter with a `nullptr`
default MAY be added to `delegate_send_layer` if the coder prefers it to pre-checks — byte-identical for every
existing caller, named in preflight); B112's 25-site core fix (its own slice); legacy deletion (9); NV (10); any static/gateway controller
(there is none, §14); OLED/companion UI beyond the console/BLE contract. C1: no refactor rides along.

## 3. Carrier contract

**Where it lives:** in `lib/core` so the native suite and the simulator compile the real binding — `struct
NodeRadminClientCarrier : IRadminCarrier` bound to a `Node&` (declared in `node.h` under `#if MR_FEAT_RADMIN_CLIENT`,
implemented in `node_mac_rx.cpp` next to `radmin_send_response`, whose shape it mirrors), or the equivalent `Node`
member functions. **No new translation unit** (P7: if one is added, `lora-universal-simulator/CMakeLists.txt` joins
§6 in the same slice). Firmware's `DeviceRemoteCarrier` is deleted and `handle_remote_client` /
`remote_client_service_once` bind the Node carrier; the inbox CLIENT probe (real `g_node`) then compiles the real path.

**`tx_queue_full()`** = `Node::tx_queue_full()`.

**`submit_request(route, source, leg, bytes, e2e_ack)`**, in order: (1) `!mobile_registered()` ⇒ `unavailable`,
nothing sent (R-RA-12/§14: the only topology is a mobile with a static home); (2) `tx_queue_full()` ⇒ `full`;
(3) `e2e_ack && e2e_ack_ring_full()` ⇒ **`correlation_full`** (the rev-4 typed outcome; B408/B412 — see §4);
(4) `route.hop_count == 0` ⇒ `send_by_hash(route.target_hash, bytes, len, flags, CryptIntent::off, 0, 0,
Plane::GLOBAL, DATA_TYPE_REMOTE_CMD, /*suppress_intro=*/true, &dsp, /*via_home=*/true)` (B413: the wrapper is
mandatory even when the mobile holds an authoritative binding for the target — one counter space at the home,
the delegated 300-s tier, the B278 correlation row; a `queued` dispatch whose `dsp.dst != mobile_home_id()` is a
contract violation the controls catch); `route.hop_count 1..3` ⇒
`delegate_send_layer(route.target_hash, route.hops, route.hop_count, DATA_TYPE_REMOTE_CMD, bytes, len, flags)`;
`flags = e2e_ack ? DATA_FLAG_E2E_ACK_REQ : 0`. (5) Admission: `SendDispatch::Admit::queued` (or a non-zero
`delegate_send_layer` return) ⇒ `queued` and the wrapper ctr is recorded in the pending row (`carrier_ctr`, §4);
`refused` ⇒ `full` (a TX-queue refusal, reported `radio_enqueue_failed`); `none` ⇒ `unavailable`; `parked` is unreachable for a registered mobile (the delegation arm
precedes the park arm) — treat as `full`, count `radio_enqueue_failure`, and prove the arm is not taken. The
`source` argument is the mobile's own stable hash (already `svc.self.key_hash32`); the carrier never overrides
`SOURCE_HASH` — `stamp_origin`/`enqueue_data` stamp it. The body cap is whatever `remote_body_cap(leg)` already
gave the sealer (231 B wrapper / less for a cross-layer path); the carrier adds no clamp and refuses nothing on size
(the packer's own refusal surfaces as `none`/0 and is counted). **`submit_ack`** is the same path with `flags = 0`. **`-a` is execute-only, in every half (B410):**
`remote_local_parse` refuses `-a` on `rollover` / `rollover confirm` with the same typed local error as `open -a`
(accepted grammar otherwise unchanged); `remote_client_start` refuses `e2e_ack` on every non-execute opcode
(`bad_args`); `send_ready` derives the carrier flag from `opcode(p) == auth_execute` (never from the phase alone);
the B408 correlation accounting counts `kAck` only on execute rows. A bootstrap, a rollover (with or without a
supplied `-a`) and an ACK therefore never carry `DATA_FLAG_E2E_ACK_REQ`; each half has its own control.

**What the controller may claim (B112 / R1):** `queued` means "the wrapper is stored in this mobile's TX queue".
`send_aired`, the hop ACK and the home's re-origination are never consulted; the home's `deleg_fail` one-shot
(`send_failed{no_route}`, ctr 0) is not consumed. Delivery evidence is the authenticated response (or the `-a`
observations of §4, labelled as carrier facts). Silence ends at the outcome deadline as `unknown` (§13), after the
one automatic exact resend of §5. The USB/BLE acknowledgement of a `remote` line stays `{"ack":"remote","id":…}` —
it already means "accepted locally", nothing more.

## 4. The `-a` path and the three carrier observations

**Request side:** `-a` sets `DATA_FLAG_E2E_ACK_REQ` on the execute request's wrapper only (§3, B410).
`enqueue_data` arms `_pending_e2e_acks` for the same-layer wrapper (wildcard key, 300-s tier) and
`delegate_send_layer` for the cross-layer one (key = `dst_hash`, same tier) — existing. **B408:**
`remote_client_correlation_free()` returns the free `_pending_e2e_acks` rows; the core's initial `correlation_full` refusal
(before a row is reserved) and the carrier's step (3) are the two halves of one bound (a control removes each).
**B412 — send-time pressure:** `send_ready` maps the carrier's `correlation_full` to `RemoteClientError::correlation_full`;
at start that refuses and releases the row exactly like the initial check; in `remote_client_service` (after a
bootstrap, before a resend) the row stays in its `*_ready` phase and is re-offered on the next pass until the ring
frees or the outcome deadline reports `unknown` — no counter increments (it is not a radio failure), no local
terminal, no eviction (R-RA-22). A plain `full` keeps its meaning: TX-queue refusal, `radio_enqueue_failed`,
counter incremented. Ring-full is never inferred from an ACK-requesting enqueue failure. The pending row records the
wrapper ctr in a new `uint16_t carrier_ctr` at offset 118 — the coder's preflight measured core 120 / row 352 / block
4512 on all three ABIs with it (no growth); **nothing else is stored for observations** (no flags, no details, no
sink, no queue).

**Target side (ACCEPT, R-RA-49):** in `rx_remote_cmd_accept`, after `remote_session_receive` and `radmin_send_reply`,
when `pa.flags & DATA_FLAG_E2E_ACK_REQ` and the verdict shows the target took ownership of the request — admitted
to execute, transcript replay of an identical fingerprint, or the authenticated `already_acknowledged` protocol
error — send the E2E ACK exactly as the generic arm does: `CROSS_LAYER ? send_xl_ack(*ui, pa.ctr) :
send_e2e_ack(pa.origin, pa.ctr, ui->source_hash)`. Silent authentication failures and admission refusals
(`session_full`, `ingress_full`, `session_busy`, `executing`, `preparation_failed`) send NO E2E ACK; the `-a` row
then ends at `e2e_ack_timeout`. The home translates `ctrH → ctrM` and last-miles the ACK (B251/B278 S3, unchanged).

**Controller side — matched in core, emitted at the push fanout (B409).** Core stays pure and emit-free:
`lib/core/remote_client.{h,cpp}` gains two matchers that read state and return the matched request ID or 0 —
`remote_client_observe_ack(const RemoteClientState&, uint16_t ctr, bool timed_out, uint8_t push_dst, uint32_t push_sender_hash, uint32_t* target_hash_out)` (returns the candidate's request ID and writes its `route.target_hash`) and
`remote_client_observe_custody(const RemoteClientState&, uint16_t mobile_ctr, uint8_t failed_type, uint8_t
target_kind, uint32_t target_value)` — matching ONLY a live pending row whose `flags & kAck` and whose `carrier_ctr`
equals the ctr — and never the counter alone (B413): a timeout push must carry `push_dst == 0` (the delegated
wildcard/XL entry's shape; a direct entry carries the target id); an acked push must carry `push_sender_hash ==
route.target_hash` for a cross-layer row and `push_sender_hash == 0` for a same-layer row (`node_mac_rx.cpp`'s
E2E-ACK receive stamps the acker's hash only on a cross-layer ack). **The binding veto (B416) is a firmware-side
step, not a pure-matcher term:** for a same-layer ACK only (never a timeout, never a cross-layer ACK whose hash
already matched), the observer asks the existing public const `Node::id_bind_find_by_hash(target_hash_out)`
through a call-scoped function pointer + context handed in by the board call — `mrfw::remote_client_bind_lookup(void*
node, uint32_t hash)` defined in `src/firmware_remote_client.cpp` (includes `node.h`; the `RemoteEntropyFn` /
`client_console_drops` idiom, U1) with `&g_node` as the context — and drops the observation when a binding exists
and differs from `push_dst` (the coder's counterexample: binding ID 2 accepts, ID 3 rejects, all else equal). No
binding ⇒ no veto. Nothing is stored; core never sees a Node. Because every controller request now leaves through the wrapper,
live rows never share a counter (one `next_ctr(home)` space). The residual — an operator's own direct `-a` DM to
the SAME target whose per-target counter happens to equal a live wrapper counter — is the E2E ring's pre-existing
wildcard ambiguity (`e2e_ack_clear` clears a wildcard entry on the counter alone), registered as **B415**, out of
8b's scope; the matcher inherits that fidelity and an observation is diagnostic, never a terminal. Custody
additionally requires `failed_type == DATA_TYPE_REMOTE_CMD && target_kind == hash &&
target_value == route.target_hash`; never a counter alone; no phase change, no counter, no emit. The board glue is
the existing push fanout in `src/fw_main.cpp` (`while (g_node.next_push(pu))`): exactly ONE new call, **after the
`if (mrble::connected()) { … }` fanout block and before the loop's closing brace** (B411), under
`#if MR_FEAT_RADMIN_CLIENT`, once per drained push, passing `pu` whole — `mrfw::remote_client_observe_push(g_node.remote_client(), pu, mrcon,
<call-scoped BLE line sink or nullptr>, mrble::connected(), mrfw::remote_client_bind_lookup, &g_node)` (the last
two are the B416 veto's call-scoped lookup and its opaque context — never stored). No parsing enters `fw_main` (custody probe U25) and the
custody arm's single `print_custody_failure` call stays (U24). The function lives in `src/firmware_remote_client.h`
(compiled by the `radmin8verbs` battery and the inbox CLIENT probe): it recognises the three push kinds
(`send_e2e_acked`; `send_failed` with `reason == e2e_ack_timeout`; `custody_failure`), for custody re-reads the record
through the ONE codec `print_custody_failure` already uses (`parse_custody_failure` + `parse_custody_translated_tail`,
U1) to obtain `failed_type`, `target_kind`/`target_value`, `failed_origin`, `reporter_layer`, `original_reporter` and
the reason name, calls the core matcher, and on a match emits through a call-scoped `RemoteClientDelivery` (new
method `carrier(transport, id, event, ctr, const CarrierDetail*)`) to the row's `local_transport` only. Nothing is
retained: an observation whose BLE companion is disconnected at that instant is dropped (the custody push itself is
durable in the inbox record; the E2E outcome is the row's own). Order per drained push, stated once: the existing plain-text line (in the switch), then the existing generic JSON push
(the BLE fanout), then the controller line/event — on every transport the correlating detail precedes the
controller event, and the placement above is the only one that yields it (the coder's 9-check writer/sink proof).

**Formats** — contract-first (`ios-companion/INBOX_SYNC_CONTRACT.md` section, `test_console_json.cpp` goldens and the
inbox CLIENT probe land before the emitter), each ≤ 244 B through `console::write_event`:
- USB: `> remote <id16> carrier acked ctr=<n>` · `> remote <id16> carrier ack_timeout ctr=<n>` ·
  `> remote <id16> carrier custody_failure ctr=<n> origin=<failed_origin> reporter=<original_reporter> layer=<reporter_layer> reason=<name>`;
- BLE: `{"ev":"remote_carrier","id":"<id16>","event":"acked","ctr":<n>}` (likewise `"ack_timeout"`) ·
  `{"ev":"remote_carrier","id":"<id16>","event":"custody_failure","ctr":<n>,"origin":<n>,"reporter":<n>,"layer":<n>,"reason":"<name>"}`.
  `origin` is the home's outward `failed_origin`; `reporter` is the relay that reported (`original_reporter`) — two
  distinct fields (B409); `ctr` lets the companion correlate with the `custody_failure` push it also receives.

An observation never changes the row's phase, never satisfies a terminal, never counts as authentication failure,
never triggers a resend (item 8b: "initiates no automatic retry"), never releases the E2E ring or the home's row. A
row without `-a` never produces one (control). `custody_failure` keeps its own push, inbox record and
`print_custody_failure` line byte-for-byte (`tools/probe_custody_usb` U23–U25 and the `s07`/`s22` receipts are the
controls); the simulator never compiles `fw_main`, so the corpus reach of the observer is zero by construction, and
core gains no emit.

## 5. Timing (R-RA-44) — named derivations, no literals

Both derivations reuse the existing constants and the R-RA-20/23 shape; the shared expiry scan
(`remote_client_next_expiry`) gains the two new edges — no new timer, no private timer ID (R-RA-22). The two
derivations below are **RULED R-RA-47 and R-RA-48** (§8), exactly as written.

- **Automatic exact resend of a silent request (R2).** For every `*_wait` phase (bootstrap, response, compare,
  rollover): ONE automatic resend of the exact outstanding bytes at
  `remote_client_resend_ms(route) = route.hop_count ? gateway_send_giveup_ms : e2e_ack_deadline_ms`
  (150 s = the one-way doorstep hold of the cross-layer class; 60 s = the same-layer DM round trip); then silence
  until `outcome_deadline_ms` (`e2e_ack_deadline_xl_ms`, 300 s) ⇒ `unknown`. Safety is §10: the target never
  time-evicts an executed fingerprint or transcript, so a resend inside the window replays and never re-executes;
  a bootstrap is read-only; a rollover resent under a rotated epoch fails authentication silently and the compare
  path recovers. The resend reuses `send_ready` (same `submit_request`, same `-a` flag) and counts nothing new on
  success; `pending.next_retry_ms`/`retries` hold the state. No resend after the deadline (§13), none of changed
  bytes (§9), none for a `complete` row.
- **ACK-debt cadence (R3).** Attempt 0 at local acceptance (existing). Retries follow the MAC's own requeue shape:
  `cascade_requeue_base_ms` doubling per attempt, capped at `cascade_requeue_backoff_cap_ms`, for at most
  `cascade_requeue_max` retries (5 s, 10 s, 20 s). After that the debt is DORMANT: retained (design §8.10 — wiped only
  by `epoch_update`), shown in `status` as `radmin_client_ack_debt=<n>`, and re-attempted exactly once each time a
  new request to the same target is started (it rides ahead of that request on a live session) — never as a
  periodic beacon. `RemoteClientAck.retries` holds the count; a `full`/`unavailable` submit does not consume an
  attempt.

## 6. Fence

Production: `lib/core/remote_client.{h,cpp}` (`carrier_ctr`; `RemoteClientSend::correlation_full` and its `send_ready` /
service mapping — B412; the two pure matchers; `-a` execute-only in
`remote_client_start`, `send_ready` and the B408 accounting; the resend edge; the ACK-debt cadence;
`radmin_client_ack_debt` accessor), `lib/core/node.h` (`NodeRadminClientCarrier` / members;
`remote_client_correlation_free()` re-pointed — B408), `lib/core/node_mac_rx.cpp` (the carrier implementation beside
`radmin_send_response`; the E2E-ACK arm inside `rx_remote_cmd_accept` under ACCEPT), **`lib/core/node_hashlocate.cpp` (`Node::send_by_hash`: ONE trailing defaulted parameter `bool via_home = false`
and ONE added conjunct `!via_home &&` on the authoritative-binding arm — nothing else in the body; every existing
caller byte-identical; the corpus proves it — B413) and its declaration in `lib/core/node.h`**, `lib/core/node_mac.cpp`
only if `delegate_send_layer` gains the defaulted `SendDispatch*`, **`src/fw_main.cpp` (exactly ONE new call after the BLE fanout block inside the push-drain loop, under
`#if MR_FEAT_RADMIN_CLIENT`, with its call-scoped `LineSink` adapter, the `remote_client_bind_lookup, &g_node`
arguments and any include that call needs; no other edit)**, `src/firmware_commands.cpp` (delete `DeviceRemoteCarrier`;
bind the Node carrier; `correlation_free` source), `src/firmware_remote_client.{h,cpp}`
(`remote_client_observe_push` with the B416 binding veto; `remote_client_bind_lookup` — the `.cpp` may include
`node.h` for it; `RemoteClientDelivery::carrier`; `-a` refusal on the rollover forms in `remote_local_parse`;
`status` field), `ios-companion/INBOX_SYNC_CONTRACT.md` (`remote_carrier` event). **No
`platformio.ini` change, no new TU** (P7 note in §3). Tests: `test/test_remote_client.cpp` (resend / cadence /
matchers / B408 / execute-only flag on the fake carrier), `test/test_node_remote_session.cpp` (the real
M1→home→target chain replaces or joins the loopback), a new `test/test_node_remote_carrier.cpp` if the chain wants its
own file (native only), `test/support/*.h` lifts of `GPair`/`S4Chain`/`TargetNode` if shared,
`test/test_firmware_admin_client_verbs.cpp` (parser refusal of `-a` on rollover; the observer/emitter on synthetic
pushes), `test/test_console_json.cpp` (goldens), `test/test_custody_receive_g.cpp` (a control that a translated
report about a `-a` `REMOTE_CMD` still pushes/records unchanged and, handed to the real observer, matches only the
right row). Tools: `tools/probe_features/ownership.py` (the new `#if MR_FEAT_RADMIN_CLIENT` site in `fw_main.cpp` and
the `node.h`/`node_mac_rx.cpp` sites — census re-derived, D5/D6), `tools/probe_custody_usb` (U23–U25 unchanged =
control; a structural check that the fanout call exists exactly once may join it or `probe_console_sink/structural.py`),
`tools/probe_board_abi.py` (re-run; pins predicted unchanged), **`tools/probe_console_sink/structural.py` (S45's
`allowed_client_node` allow-list gains exactly the call-scoped `NodeRadminClientCarrier carrier(g_node)` binding and
nothing else; the S-C45 injected-`node_id` control stays RED — B414)**, `tools/probe_inbox_verbs` CLIENT arm (now
compiles the real carrier AND the observer), mutation batteries: extend `radmin8client` (`lib/core/remote_client.cpp`: matchers,
execute-only flag, resend edge, cadence) and `radmin8verbs` (`src/firmware_remote_client.h`: parser refusal, observer
kinds, emitter fields/transport); add `radmin8brx` for `lib/core/node_mac_rx.cpp` (the E2E-ACK verdict gate, the
carrier pre-check order, the admission mapping) — every new decision gets an executed control.

## 7. Required proofs (distribute §19.2; every new decision gets an executed control)

| Surface | Proof |
| --- | --- |
| Real chain | M1 (registered at node 1) `remote t -e -- status`: wrapper on the real MAC → node 1 re-originates `0xA0` GLOBAL with `SOURCE_HASH` = M1 → node 2 (`TargetNode` ACL) admits, executes, answers `0xA1` to M1's hash → node 1 last-miles → M1 assembles and delivers; every terminal meaning of the 8ac loopback reproduced on the chain; cross-layer row via `delegate_send_layer`; bootstrap, rollover and ACK travel the same path |
| Wrapper always (B413) | known (authoritative binding from the target's own beacon), unknown and claimed bindings × two simultaneous targets: every request leaves as `MOBILE_SEND` to `mobile_home_id()` with distinct wrapper counters, `carrier_ctr` unique across live rows, each ACK / timeout / custody observation attributed to its own row; the coder's B413 reproducer with `via_home` forced false is the RED control; an operator's direct `-a` DM to one of those targets with a different counter matches no row; the equal-counter case is a labelled characterization (B415), not a claim |
| Admission | not registered ⇒ `carrier_unavailable`, zero frames; TX full ⇒ `radio_enqueue_failed`, row released at start; `-a` with a full E2E ring ⇒ `correlation_full` before any frame — at start (initial check, row never reserved; carrier pre-check with the initial check removed) AND at send time after a real bootstrap reply (row stays `request_ready`, no counter, no terminal, resumes when the ring frees — B412); a plain `full` after bootstrap increments `radio_enqueue_failure` and is never reported as `correlation_full`; the park arm is never taken; `SendDispatch::refused` ⇒ `full` and no `carrier_ctr` |
| No overclaim (B112) | a home whose re-origination is refused (queue full / bad XL path — the existing `xl_delegate_no_route` shape) leaves the row in `response_wait` with `carrier_ctr` set; nothing is reported until the resend and the deadline; the uncorrelated `send_failed{no_route}` push is untouched and unconsumed |
| `-a` end to end | target E2E-ACKs an admitted, a replayed and an already-acknowledged request; NOT a refused or unauthenticated one; the ACK reaches M1 as `send_e2e_acked{ctrM}` and the row reports `carrier acked ctr=<n>` once; a row without `-a` gets nothing; `-a` on `open` still refuses (R-RA-18); **B410:** `-a` on `rollover` / `rollover confirm` refuses at the parser, `remote_client_start` refuses `e2e_ack` on a non-execute opcode, and bootstrap / both rollovers / ACK submits carry `e2e_ack == false` with `-a` supplied or not (the coder's 36-check shape, now with each half's control RED) |
| Custody | a translated record for the `-a` request (S4Chain transport A) drained through the real observer ⇒ `carrier custody_failure ctr=… origin=… reporter=… layer=… reason=…` on the row's transport only; the same record with another counter / target / `failed_type == 0xA1` / a row without `-a` ⇒ nothing; the push, inbox record and USB custody line are byte-identical with and without the observer; no phase change, no resend, no terminal |
| Binding veto (B416) | the coder's counterexample executed through the real observer with a real Node table: target hash bound to ID 2 ⇒ the same-layer ACK `{dst=2, ctr, sender_hash=0}` is attributed; bound to ID 3 ⇒ dropped; no binding ⇒ attributed; the veto never runs for a timeout or a cross-layer ACK; an operator's direct `-a` DM to a DIFFERENT known target with an equal counter is vetoed; removing the veto or the lookup argument is RED; no Node pointer or callback is stored (structural) |
| Wiring (B409/B411) | the inbox CLIENT probe compiles `remote_client_observe_push` (with the real `g_node` lookup) and a structural check binds its ONE `fw_main` call (after the BLE fanout block, inside the drain loop, carrying the lookup + context arguments); the writer/sink order proof shows text, generic JSON, then the controller event, and the before-fanout placement is RED; synthetic `send_e2e_acked` / `send_failed{e2e_ack_timeout}` / `custody_failure` pushes through the real observer emit the exact golden USB line or BLE event to the matching transport only; a disconnected BLE row drops (no retention); `fw_main` holds no parser (custody probe U25) and the custody arm's single call survives (U24) |
| Resend (R2) | exact bytes at the derived edge, once, per phase; none after `complete`; none after the deadline; the target replays without re-executing (execution counter); cross-layer vs same-layer edges differ by the named constants; mutating the factor/constant is RED |
| ACK debt (R3) | 5/10/20 s then dormant; one re-attempt on the next request to that target; `status` field; a `full` submit consumes no attempt; epoch change wipes; never a periodic beacon (frame count over a simulated hour) |
| Roles / structure | gateway build unchanged (Node/RAM); ACCEPT inventory still 208 rows with no `remote*`; `fw_main` keeps one call per push arm; no new MR_EMIT on any non-RPC path; `s07`/`s22` custody receipts unmoved |
| Allocation | `sizeof(RemoteClientState)` 4512 on all three ABIs with `carrier_ctr`; Node pins unchanged; mobile RAM delta attributed (expected 0 resident) |

## 8. Owner rulings — ALL RULED 2026-09-18 (owner, verbatim: "R1-R4 approved as recommended, record them as R-RA-46..49")

- **R1 — RULED R-RA-46:** 8b proceeds with B112 OPEN. Reason: at the one
  site the controller uses, admission is already stated by the admitting layer (`SendDispatch` from `send_by_hash`,
  a truthful store return from `delegate_send_layer`); the controller claims nothing beyond "stored locally" and
  consumes neither the hop ACK nor `send_aired`; loss anywhere beyond the mobile is covered by the resend and the
  300-s `unknown`. B112's residue for 8b is stated, not hidden: a request whose home re-origination was refused is
  indistinguishable from any other loss until then. B112 keeps its owner-agreed separate core slice (25
  `enqueue_data` sites) and its named characterization; the design's item 8b / §14.1 sentence is rewritten to this
  ruling. (The alternative — fix B112 first — was not chosen.)
- **R2 — RULED R-RA-47:** §5's ONE resend at `hop_count ? gateway_send_giveup_ms :
  e2e_ack_deadline_ms`, then `unknown` at 300 s. (Neither alternative — no automatic resend, or two — was chosen.)
- **R3 — RULED R-RA-48:** §5's cascade burst (5/10/20 s) then dormant with one re-attempt per new request to the
  same target. (The steady 300-s retry alternative was not chosen.)
- **R4 — RULED R-RA-49:** admitted / replay / already-acknowledged only (B278 §9.2's
  "after the admission boundary"); refusals and silent failures never. (Acknowledging every authenticated arrival
  was not chosen.)

B408 (the mobile's `-a` bound reads the home-side ring) is a QA finding folded into this brief; it needs no ruling.
Nothing is HOLD: the coder source-validates this revision and implements.

## 9. Gate and landing

Full gate on both sides (`lib/core` change): native wrapper and binary; extended reference (`--freeze-check
--compare`, 94/94 — no codec change, identity expected); simulator rebuild (both variants recompile
`remote_client.cpp`, `node_mac_rx.cpp`, `node_mac.cpp`) and corpus `--require-anchors` **predicted 36/36
byte-identical** (no scenario carries `0xA0/0xA1`; the CLIENT hooks are emit-free and match nothing; the eleven
custody receipts in `s07`/`s22` must not move) — any stream delta is STOP; ABI probes (Node 235248/122176/157344,
block 4512); six probes + deferred-actions + BLE-line, default and `--no-neg`, explicit inbox CLIENT arm; tools
discovery; inventory write/bare/check (208); authority + selftests; A0; literals; whitespace both repos; census six
envs; deterministic pair gateway then heltec_mobile + the one-off `xiao_mobile`; union S ∪ H from the 59/918 floor
plus `radmin8brx` and the extended batteries — run fresh from scratch at the freeze (the three controls the coder
repaired mid-slice, `radmin8client` C39/C43 and `radmin8brx` X09, must show fresh RED; no earlier total is inherited);
B414 repaired and `probe_console_sink` PASS with `--no-neg` before the freeze; the exact `PIN re-synced? YES` line. **STOP:** a frame on air from an
unregistered mobile or after `carrier_unavailable`; a controller claim beyond the local dispatch; an observation
reaching the assembly, a terminal or the auth counter; a resend after the deadline or of changed bytes; a periodic
ACK beacon; any resident byte beyond `carrier_ctr` in padding; a resident sink, callback or queue for observations, or an observation emitted from core; a `REMOTE_CMD` queued from the mobile with `dst != mobile_home_id()`; a new TU without its simulator line; gateway Node/RAM
movement; a new verb. Receipt: `docs/superpowers/evidence/2026-09-18-radmin-slice8b.md`. On PASS QA lands the
register (B408 closed; B392's Part 57b and 8c's Part 57d flip from "needs 8b" to RUNNABLE), design §19.1 row 8b +
the item-8b/§14.1 B112 sentence per R1, bench **Part 57c** (real mobile→home→target request/result line with request
ID; `-a` shows `carrier acked`; a pulled relay shows `carrier custody_failure`; Parts 54/57b/57d become runnable),
tracker, MEMORY, and the R-RA-46..49 completion notes in the ledger.
