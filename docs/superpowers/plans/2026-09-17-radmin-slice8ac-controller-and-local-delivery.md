<!-- QA/Author: Claude (revision 1); production coder: Codex; owner rules and commits -->
# Remote-admin v2 Slices 8a+8c (paired) — mobile controller core and local USB/BLE delivery

**Revision 4 — 2026-09-18: B405 folded (fence completed for the legacy-slot replacement); R1 re-measured under R-RA-45
— IMPLEMENTED; INDEPENDENT QA PASS 2026-09-18 (§10).** Revision 3 (`8b434921…`) → 4 changes §5 (measured figures) and §6 (four added paths).
Revision 2 (`210408d9…`) → 3 changed only §8 R1. Historical: **Revision 2 — 2026-09-17 (checkpoint after the coder's STOP-1):
B403 folded, R-RA-42/43/44 folded; ready except the R1 allocation.**
Revision 1 (`d7e4cf2a…`) changes: §1 `derive_base` row, §3 keys paragraph and §7 Keys row now state what "invalid
conversion" means at this source (B403); §8 records R-RA-42/43/44 as settled and states the R1 recommendation.
Base **`6086152b97b5934971d231a345b9939d5f2db1e9`** (owner commit `7b-3`), clean; simulator
`06746a97de5764415d6fcef10b97bca90569b9c7`, clean. Pairing per the owner's 2026-09-16 P6 ruling
(`docs/2026-09-02-agent-roles.md`): one brief, one implementation, one independent gate. Commits are not
blocking points; the coder pins this brief by content hash plus the inventory of the uncommitted QA documents
(this file, the register/design/tracker/MEMORY edits that announce it). A brief under implementation is frozen;
mid-slice rulings land in the ledger and register and are re-pinned at a checkpoint.

## 1. What binds this slice (links, not quotes)

Design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`: §6.1 credentials and the
`remote` syntax, §6.3 target book, §7.1 KDF, §7.2 discovery/bootstrap and the session cache, §8.2/§8.4/§8.5/§8.6/
§8.7/§8.9/§8.9a/§8.10 bodies and local delivery, §9 nonce and request-ID rules, §10 cases 1–5 as seen from the
controller, §12.1 authority levels, §13 controller honesty after timeout/epoch change, §14 and §14.1 the
capability and carrier split, §15 controller bounds and counters, §16 flow, §19 item 8, §19.2 the required
controller-boundary controls. Rulings (`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`):
R-RA-13 (mandatory SOURCE_HASH), R-RA-15 + addendum (`-a` optional ACK), R-RA-17 (role exclusivity, native
`{1,1}`), R-RA-18 (`-e` mandatory; exactly one of `open` / `-e`), R-RA-22 (controller profile capacities:
4 `PendingRequestInline`, 4 `SessionCacheEntry`, 2 `ResponseAssemblyHeader`, 8 `ResponseChunk`,
2 `RetainedResultHeader`, 8 `AckDebtEntry`; candidate 4272 B; one shared expiry scan), R-RA-27 (legacy round
trips suspended), R-RA-29 (fingerprint), R-RA-30 (BLE split for controller verbs), R-RA-33 (authority table
is ruled; new rows follow the design), R-RA-36/37 (typed admission and `action_busy` results the controller
must consume). Working rules: CLAUDE.md C1–C4, D1–D6, P4–P6; `docs/CODE_GUIDELINES.md`.

| Verified seam at `6086152` (pin by symbol; lines are hints) | Consequence |
| --- | --- |
| `lib/core/remote_codec.h`: `remote_body_encode`, `remote_body_decode`, `remote_kdf_base`, `remote_kdf_session`, `remote_nonce`, `remote_aad`, `remote_body_cap`, `remote_make_request_id`, `RemoteEntropyFn`, `RemoteKeys`, `RemoteSource`, `RemoteCarrier`, `RemoteDecoded` (`result_kind`, `terminal`, `result_detail`, `admission`, `protocol_error`) | The controller uses this codec unchanged for every request and response domain. No codec edit. |
| `lib/core/remote_session.cpp`: `derive_base` = `ed_pub_to_x25519` (`identity.cpp`, a void wrapper over `crypto_eddsa_to_x25519`, which maps any 32 bytes through (1+y)/(1−y) without Edwards point validation) → `remote_ecdh_shared` (refuses an all-zero shared result, which is every low-order peer point) → `remote_kdf_base` (refuses an all-zero shared input) | Mirror it on the controller with the same primitive calls and the same refusals; do not fork a second conversion (U1). **B403 precision:** "invalid conversion" at this source means exactly those X25519-level refusals plus the stores' existing encoding checks (32 bytes, not all-zero, no duplicate identity). No Ed25519 on-curve or canonical-encoding check exists, the vendored Monocypher exports no point decompression, and none is required by this slice: only keys installed by an authorized operator (`acl add`, `admin-target add`) or matched exactly in the ACL ever reach the conversion, and an off-curve encoding cannot authenticate because no seed produces it. Install-time encoding validation is parked as B404. A pure shared helper in `lib/core` is permitted if named in preflight. |
| `test/radmin_0e_candidate_types.h`: `PendingRequestCore`, `PendingRequestInline`, `SessionCacheEntry`, `ResponseAssemblyHeader`, `ResponseChunk`, `RetainedResultHeader`, `AckDebtEntry`, `kCandidateSealedRequestBytes` (231), `kCandidateSealedAckBytes` (25) | The ruled row shapes. Production types may rename fields but drop no field; sealed request bytes are inline (R-RA-22). |
| `lib/core/node.h`: `rx_remote_resp_client`, `remote_inbound_stage`, `take_remote_inbound`, `RemoteInbound`, `_remote_inbound`; `lib/core/node_mac_rx.cpp` `Node::rx_remote_resp_client` (stages into the legacy one-slot); `src/fw_main.cpp` the CLIENT drain block that prints `[rcmd N] …` | 8a replaces the legacy client staging with the pending table (design: "until Slice 8a's controller pending table replaces it"). The legacy slot, its drain and the `[rcmd …]` printer are removed in this slice; `rcmd` itself stays dispatchable until Slice 9 and its responses are counted and dropped. |
| `lib/core/node.h`: `radmin_rx_owner`, `rx_remote_cmd_accept`; `lib/core/mr_features.h` profile map (`MR_PROFILE_MOBILE` ⇒ client=1/accept=0; `ARDUINO` ⇒ 0/1; host ⇒ 1/1) | Direction ownership is settled (1b). Native drives target and controller in one process; boards keep exclusivity. |
| `src/firmware_admin_keyring.h` (`/mrmkeys` seeds, `self` read-only), `src/firmware_admin_targets.h` (`ITargetStore`, `TargetRow{admin_pub, key_hash32, hops, hop_count, label}`), `src/firmware_admin_identity.h` (`admin_fp_hex`), `src/firmware_admin_client_verbs.h` (`admin_client_router_arm`, `admin_client_ble_public`, `admin_client_ble_refuses`, `IClientRemoteDebt`, `client_regen_admitted`), `src/firmware_commands.cpp` `DeviceClientRemoteDebt::busy()` (returns `false` today) | Reuse: target resolution by label, credential derivation from a seed, fingerprints, the client router arm and BLE split. Bind `busy()` to the real pending/assembly/retained/ACK-debt state. |
| `src/device_rng.h` `mrrng::fill` (void), `lib/core/hal.h` `IHal::rand_bytes` (void) — **B312** | Add a checked entropy adapter that reports failure (SoftDevice unavailable, timeout, host fake) and bind it as the `RemoteEntropyFn`; a failed draw refuses the request (C2), never a zero ID. |
| `lib/core/node.h` `send_by_hash`, `do_send`, `send_remote_cmd`, `send_remote_response` | **No carrier in this slice (8b).** The controller transmits through one small interface (`IRadminCarrier`: submit request bytes, submit ACK bytes, `tx_queue_full`); the production binding on boards returns a typed `carrier_unavailable` refusal until 8b; native binds a loopback into the target Node. The legacy `send_remote_*` helpers are never called. |
| `src/firmware_commands.cpp` `dump_status` (ACCEPT `radmin_*` fields), `exec_console_line` (`CommandContext`, `LineFormat::json`), `lib/console/console_json.h` `write_event`, `write_err`, `write_push` | Controller status fields and BLE events use these writers; no second JSON writer. |
| `src/device_ble.h` `dispatch_current_line`, `g_out[256]`, `kProductLineMaxBytes` — **B292** (a 245..256-byte reply is one notification against a 244-byte ATT payload) | 8c bounds every line it emits to the ATT payload; it may close B292 by chunking at the one transport write (recommended, U1), never by a second output path. |
| `ios-companion/INBOX_SYNC_CONTRACT.md` "Pushes (node → app)", `test/test_console_json.cpp` (`write_push` goldens), `tools/probe_inbox_verbs/run.sh` CLIENT arm (compiles the real `firmware_commands.cpp` with `-DMR_PROFILE_MOBILE`) | 8c's events are contract-first: the doc section and the executed goldens land before production output. The CLIENT probe arm is the real-router gate for both slices. |
| `src/firmware_command_authority.h`, `docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md` (182 rows, no `remote` row; `rcmd` = legacy), `tools/check_command_authority.py`, `tools/gen_command_inventory.py` (204 rows) | New controller verbs need table rows (§8 R2). The inventory's ACCEPT column must show them absent (structural absence gate, §19.2). |

Baselines (7b-3 gate, 2026-09-16): native 2931/189998/0; corpus 36/36, s18 `32afbf11`/269517/0; Node
230976 native / 157344 gateway / 117912 mobile; gateway RAM 204036 / flash 574752; heltec_mobile RAM
207756 / flash 1373604; union floor 56 batteries / 880 configured; census 173/178/177/177/182/182.

## 2. Scope split inside the pair

**8a — controller core (host-visible, no carrier):** the `remote` verb and its controller-local control
forms; target and credential resolution; the checked request-ID source; discovery/bootstrap and the session
cache; request sealing with exact-byte retention; response authentication, reassembly and typed result
classification; ACK-debt creation; pressure/refusal behaviour; the six §15 counters; the regen-debt binding.
**8c — local delivery:** the request's supplied USB sink and the structured secured-BLE events; retained results,
re-offer and the explicit local acknowledgement; delivery acceptance as the ACK-debt trigger; console-stage-drop
refusal; the companion contract extension and its executed gate. **Out for both:** anything that puts bytes on
air (8b), custody consumption (8b), automatic retry timing (8b, it depends on carrier timing), legacy deletion
beyond the client staging slot (9), NV cleanup (10), any target-side or codec change.

## 3. Controller contract (8a)

**Syntax (R-RA-18, verbatim shape from design §6.1):** `remote <target> -e [using=self|keyN] [-a] -- <cmd>` seals;
`remote <target> open -- status|routes` is clear; `open -e`, neither, `-a` without `-e`, an `open` command other
than the two exact lines, or an empty/over-cap command tail refuse with a typed local error before any state is
touched. `<target>` is a `/mrtargets` label; the row supplies the immutable `admin_pub` (trust) and `key_hash32`
+ layer path (routing hints handed to 8b). The command tail is validated by `console_line.h`'s remote validator
(201 bytes) exactly as the target will validate it. Control forms, controller-local: `remote <target> -e
[using=…] rollover` (SAFE_ROLLOVER) and `remote <target> -e [using=…] rollover confirm` (FORCE_ROLLOVER, the
literal token is the §8.5 local confirmation); `remote-retry <16hex>` re-submits the exact retained sealed
bytes after a bootstrap epoch comparison (§13: same epoch ⇒ resend; changed epoch with no terminal ⇒ report
`unknown`, no resend); `remote-result show <16hex>` re-offers a retained result on the caller's transport;
`remote-ack <16hex>` is the local acknowledgement (8c). Reuse `console_parse.cpp`'s `-a`/`-e` flag grammar.

**Credential and keys:** `using=` defaults to `self`; `keyN` derives the identity from the `/mrmkeys` seed on
a guarded stack transient and wipes it. The pending record captures the selected controller public key, the
target public key, slot and epoch; nothing later reads mutable selector or UI state (§6.1, §19.2). Base and
session keys follow `remote_kdf_base`/`remote_kdf_session` with the controller's own Ed25519→X25519 conversion
mirroring `derive_base`: an all-zero/low-order shared result is an authentication failure (never a key, never a
fallthrough to open); the target-book row is read only if its stored encoding passes the store's validity checks.
No point-validity check is added (B403 precision in §1).

**Discovery and session cache:** on a cache miss for `(target admin_pub, controller pub)` send BOOTSTRAP
(§8.4) with a fresh ID; cache `(slot, epoch)` from the authenticated §8.6 response; an authentication failure
on a cached session triggers exactly one rediscovery for the same explicit credential, then refuses. The cache
is RAM-only and discarded on boot (§7.2).

**Request IDs and sealing (B312):** every ID comes from `remote_make_request_id` through the checked adapter;
a failed draw refuses the request. The sealed request is retained byte-exact in the inline row; a retry
re-submits those bytes, never a re-seal (§9 hard invariants). Capacity is asked from `remote_body_cap` for the
carrier the row records; without a real carrier the row records the same-layer/cross-layer shape the target
book implies and 8b's binding may only narrow it.

**Pending table admission (§15):** reserve the pending row, an assembly header and, for BLE, a retained-result
header before transmitting; refuse loudly (`request_table_full`, `assembly_full`, `result_full`) otherwise; never
evict a live request or an unacknowledged BLE result. `-a` additionally needs one delegated-correlation row
(R-RA-15) — in this slice that reservation is recorded and refused-when-full but the row is only consumed by 8b.

**Responses:** `rx_remote_resp_client` hands each addressed `REMOTE_RESP` body to the controller state, which
matches it to a pending row by request ID, authenticates and decrypts under the row's captured session key,
assembles contiguous `response_seq` from zero, and classifies the terminal: `completed`, `output_truncated`,
`refused`, `unknown_command`, `internal_error`, `scheduled` (with its 4-byte LE delay detail),
`action_busy`, the authenticated `PROTOCOL_ERROR{already_acknowledged}`, and the R-RA-36 admission results
(`session_full` ⇒ one automatic SAFE_ROLLOVER then the not-executed command is re-sealed under a **fresh ID**;
`ingress_full`/`executing`/`preparation_failed` ⇒ reported, no automatic retry; `session_busy` ⇒ reported with
its count, force needs the operator's `confirm` form). Unmatched, unauthenticated, out-of-order or over-capacity
frames are counted and dropped; a custody notice is never consumed here (§14.1). Bootstrap and rollover results
update the cache only for the row that asked.

**ACK debt:** after local delivery is accepted (8c), seal the 25-byte `RESPONSE_ACK` under the row's session key
and retain it in an `AckDebtEntry`; submit it through the carrier interface; retry opportunistically while the
entry lives; never infer target receipt from having transmitted it.

**Counters and status:** `radmin_client_request_table_pressure`, `_assembly_failure`, `_unmatched_response`,
`_auth_failure`, `_local_result_pressure`, `_radio_enqueue_failure` as saturating u16 in `status`, plus scalar
pending/retained counts. **Regen:** `DeviceClientRemoteDebt::busy()` is true while any pending, assembly,
retained-result or ACK-debt row is live; the existing refusal and note lines are unchanged.

## 4. Local delivery contract (8c)

**USB:** OUTPUT plaintext is written to the request's supplied sink as `> remote <16hex> out …` lines, then
one terminal line `> remote <16hex> <result-name>[ activation_ms=<n>][ detail=<n>]`; acceptance means every byte
was admitted by that sink (`Print::write` returned the full count); a console-stage drop keeps the result retained
and reports `> remote <16hex> retained` for `remote-result show`. **Secured BLE:** structured events through
`write_event`: `{"ev":"remote_output","id":"<16hex>","seq":<n>,"body":"…"}` and
`{"ev":"remote_terminal","id":"<16hex>","result":"<name>"[,"activation_ms":<n>]}`, each bounded to the ATT payload
(bodies split across events with contiguous `seq`); the complete result is retained until `remote-ack <id>`;
a disconnect is not an acknowledgement; after reconnect the retained result is re-offered from seq 0; an ACK for
an incomplete result is refused (§8.10). Plaintext only; no raw sealed body reaches USB or BLE. The contract
section and `test_console_json.cpp` goldens land before the production emitter; the inbox CLIENT probe arm
executes the real router for both transports.

## 5. Allocation

R-RA-22 rules the row counts; **R-RA-45 approves the re-measured pointer-free block: 4512 B; Node native
230976 → 235248 (+4272), heltec_mobile 117912 → 122176 (+4264), gateway 157344 unchanged** (coder receipt §5,
2026-09-18; the legacy `RemoteInbound` slot credit differs by ABI alignment). These are the production pins;
the linked mobile RAM and the removal of `fw_main`'s static `ri` are attributed at the gate. **Gateway Node/RAM
unchanged** (client compiled out) is the control. Report the one-off `xiao_mobile` flash (R-RA-30 pattern, B330
context). Any state beyond the R-RA-22 rows returns to QA for measurement (STOP-1).

## 6. Fence

Production: new `lib/core/remote_client.{h,cpp}` (pure controller state: pending/session/assembly/result/ACK-debt
lifecycles over the codec; no HAL, no NV, no `src/` include), `lib/core/node.{h,cpp}`, `node_mac_rx.cpp`
(`rx_remote_resp_client` → controller state; legacy client staging removed), **`lib/core/node_mac.cpp`**
(`take_remote_inbound`, both the CLIENT body and the `#else` stub, removed with the slot — B405), new `src/firmware_remote_client.{h,cpp}`
(the `remote`/`remote-*` verbs, resolution, entropy binding, carrier interface + board stub, local delivery,
status), `src/firmware_commands.{h,cpp}` (router arms, status, debt binding), `src/firmware_admin_client_verbs.h`
(BLE-public list), `src/firmware_help.h`, `src/firmware_command_authority.h` (+ rows per §8 R2),
`src/device_rng.h` (checked fill), `src/device_ble.h` (only the bounded-write change if B292 is closed here),
`src/fw_main.cpp` (loop call, legacy drain removal), `platformio.ini` (new TU in the three base filters),
`ios-companion/INBOX_SYNC_CONTRACT.md`. Tests: `test_remote_client.cpp` (new), extensions of
`test_node_remote_session.cpp` (two-Node loopback: controller Node ↔ target Node in one process),
`test_firmware_admin_client_verbs.cpp`, `test_console_json.cpp`, and **`test_node_r3.cpp`** (B405: its six
legacy-drain cases — five prove staging, one proves the ACCEPT-side refusal — are retargeted to the pending-table
consumer and to the guard-drop telemetry respectively; none is deleted without an equivalent assertion). Tools: inventory regeneration, **`tools/probe_board_abi.py`** (drop the legacy `RemoteInbound` entry, add the
controller types and the R-RA-45 Node pins), **`tools/probe_features/ownership.py`** (B405: the CLIENT consumer
census and its `W-S5-INVERT-TAKE` control move from the staging helper to the new client-owned consumer; site
counts re-derived, D5/D6), mutation batteries for every new decision file, the inbox CLIENT arm and a BLE-output
probe (extend `tools/probe_ble_line` or add `tools/probe_ble_output`). **OUT:** `send_by_hash`/`do_send` bindings, custody, the
target side, the codec, `firmware_remote.cpp` beyond the response drop, NV formats, controller UI beyond the
console/BLE contract. C1: no refactor rides along; the legacy staging removal is the replacement's own seam.

## 7. Required proofs (distribute §19.2; every new decision gets an executed control)

| Surface | Proof |
| --- | --- |
| Syntax | The R-RA-18 XOR matrix and `-a` independence; over-cap tail; unknown label; BLE-public vs refused forms |
| Credential | `using=keyN` authenticates as keyN with a differing target slot; captured identity survives selector change; seed wiped |
| Keys | Controller-side KATs reproduce Slice 2's independent vectors for base/session keys and a sealed request; every low-order peer point refuses (all-zero shared) with no key published; the four off-curve encodings from the B403 reproduction are a labelled characterization control (accepted by the conversion, unable to authenticate against a real target), not a refusal claim |
| Discovery | Exact full-key match on the target; cache hit/miss/stale-epoch; one rediscovery then refuse; cache empty after boot |
| Request IDs | Checked entropy: failure refuses; no zero/duplicate ID across the table; exact-byte retry replays identical bytes; changed-epoch retry reports unknown |
| Admission | Full table/assembly/result/correlation refuse before transmit; no eviction; counters saturate |
| Responses | Two-Node loopback end to end for every terminal, `action_busy`, `scheduled`+detail, protocol error, all five admission codes, session_full→auto safe rollover→fresh ID; wrong key/ID/seq/source dropped and counted; custody notice inert |
| ACK debt | Created only after local acceptance; sealed 25 B; retried while live; never claims receipt |
| USB delivery | Byte-exact sink acceptance; short write retains; re-offer replays; terminal line formats |
| BLE delivery | Event goldens; ATT bound on every line; retain until `remote-ack`; reconnect re-offer from 0; premature ACK refused; disconnect not an ACK |
| Regen | `busy()` true across all four row classes; refusal/note lines unchanged |
| Roles | ACCEPT inventory has no `remote`/`remote-*`; gateway build unchanged; accept-build receiving `remote …` as RPC text refuses |
| Legacy | `rcmd` still dispatches; its response is counted and dropped; the legacy slot is gone |

## 8. Owner rulings requested

- **R1 — RULED R-RA-45 (2026-09-18) as recommended:** R-RA-22 rows plus the coder-modelled additions, **minus the
  resident USB sink pointer** (store the `local_transport` tag; resolve `mrcon` at delivery through an injectable
  seam; tests bind their sink there). Re-measure the block on the three ABIs with the pointer removed; the
  re-measured block, the native and heltec_mobile `Node` re-pins and the linked mobile RAM are approved at those
  values, gateway `Node`/RAM unchanged as the control, the legacy `RemoteInbound` slot and `fw_main`'s static `ri`
  removed and attributed, `xiao_mobile` flash reported once. Any owned state beyond the listed fields is STOP-1.
- **R2 — RULED R-RA-42 (2026-09-17):** `remote` (execute/open/rollover forms), `remote-retry`, `remote-result`,
  `remote-ack` are `controller_local`, USB + secured BLE, absent from the ACCEPT inventory; QA types the table
  rows at the freeze together with the header so the three-artefact checker never disagrees.
- **R3 — RULED R-RA-43:** 8c closes B292 at the one BLE transport write; the coder notes `mrble::tx_line` already
  chunks by the negotiated MTU − 3 — route the direct dispatch reply through it (U1) and prove a 245..256-byte
  reply arrives intact, with a removal control RED.
- **R4 — RULED R-RA-44:** automatic silent-request retry timing is 8b's; 8a ships manual `remote-retry` and the
  single automatic safe rollover + fresh-ID resend on `session_full`.

## 9. Gate and landing

The standing chain (7b-3 brief §7): native wrapper and binary; simulator provenance and corpus 36/36 (predict
byte-identical: no scenario carries `REMOTE_RESP`); ABI probes; six probes + the deferred-actions probe + the new
BLE-output probe, default and `--no-neg`, explicit inbox CLIENT arm; tools discovery; inventory write/bare/check;
authority + selftests; A0; literals; whitespace; census six envs; deterministic pair gateway then heltec_mobile
plus the one-off `xiao_mobile` flash; union S ∪ H from the 56-battery floor plus every new battery; the exact
`PIN re-synced? YES` line. Receipt: `docs/superpowers/evidence/2026-09-17-radmin-slice8ac.md`. STOP: any stream
delta; bytes on air from the controller; a sealed body reaching a local sink; a request ID from a void draw;
state beyond R-RA-22; gateway Node/RAM movement; an unruled verb row. On PASS QA lands register/design/bench
(Part 57d reserved; 55b/56 unchanged) and closes B312 and, if R3 says so, B292.

## 10. Independent QA PASS — 2026-09-18

Gated by QA on the frozen tree (HEAD `e3a5fa0` + the uncommitted implementation; the coder's freeze inventory
matched 2726/2726) after landing the four R-RA-42 rows in the ruled table: native 2947/193734/0; reference 94/94 and
the coder's independent controller vectors reproduced by QA; corpus 36/36 byte-identical with zero radmin events —
once the simulator's source list named `remote_client.cpp` (**B406**: the one edit QA made outside this brief's fence,
in the simulator repo, awaiting the owner's commit); Node 235248/122176/157344 exact; gateway RAM and Node unchanged
(the control), mobile RAM +4016 / flash +19228; union 59 batteries / 917 RED / 1 known unusable B342 / 0 vacuous;
no §9 STOP condition observed (stream delta, gateway movement, unruled verb row, state beyond R-RA-45 and a void
draw are checked directly; bytes on air and a sealed body at a local sink rest on the stub carrier, the zero-event
corpus and the reproduced native/union controls). B292 and B312 closed; B407 records the coder's pre-freeze
self-review corrections. Evidence: [`2026-09-18-radmin-slice8ac-qa-gate.md`](../evidence/2026-09-18-radmin-slice8ac-qa-gate.md).
Part 57d reserved (metal-pending on 8b).
