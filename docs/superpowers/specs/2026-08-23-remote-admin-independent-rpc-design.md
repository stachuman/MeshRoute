<!-- Author: OpenAI Codex; owner review draft -->
# Remote administration v2 — compact independent RPC (revised proposal)

**Status: REVIEW DRAFT — owner rulings incorporated through 2026-09-01; not implementation authority until
independent design review passes.**

This revision incorporates the owner's decisions through 2026-09-01. It does not modify firmware behaviour
and remains subject to independent review. If ratified, it replaces the implementation direction in
`2026-07-26-remote-admin-challenge-response-design.md`; that document remains a historical decision record,
not a compatibility requirement. The completed v2 implementation must remove the current `rcmd` mechanism
rather than support both protocols indefinitely.

## 1. Decision in one paragraph

Remote administration has three authority levels. Exact `status` and `routes` requests are open cleartext
diagnostics. Operator and owner requests are authenticated, encrypted RPCs carrying the same command line
that the local serial/BLE dispatcher accepts. The locally attached MeshRoute **controller node** is the RPC
endpoint: it chooses either its normal node identity or one of ten persistent dedicated management
identities, seals requests, and authenticates, reassembles, and decrypts responses. The companion is only a
local plaintext command/output UI; it never holds a MeshRoute management private key or handles an opaque
RPC body. Intermediate relays still transport opaque bytes. Each authenticated request gets an independent
random `request_id` (64 bits is the compact proposal pending the collision analysis in §9); it does not
consume or produce the token for the next request. The target feeds
the line to the common dispatcher, captures its normal textual output, returns that output in as many
response DMs as needed, and always ends with a terminal result. A one-byte `response_seq` orders those DMs
and detects gaps. A separate, fixed ten-slot target ACL holds full controller public keys with operator or
owner roles and permits several owners. A compact authenticated discovery bootstrap resolves the selected
public key to its target ACL slot without adding bytes to normal requests. Each managed target also has one
stable administration identity, separate from its ordinary `/mrid`, so ordinary identity regeneration does
not break management trust. The target epoch is likewise kept out of normal authenticated commands. Exact
command/output limits are carrier-specific rather than falsely inheriting the 239-byte direct-DM ceiling.

## 2. Problem being corrected

The implemented scheme uses a sender counter and target replay floor. A lost command advances only the
sender, while a lost or failed floor persistence can make the target's view differ again after reboot. The
July replacement changes the counter into one current node-issued challenge, but that still makes command
`B` depend on the response to command `A`: one challenge is one shared serialization point for every client
and every in-flight request.

Remote administration should instead have ordinary lossy-RPC behaviour:

- losing request `A` must not invalidate independently created request `B`;
- losing one response must not block later requests;
- retransmitting the exact request must not execute it twice;
- multiple output DMs must be orderable and their end must be unambiguous;
- a reboot must be reported honestly when it makes the prior outcome unknowable.

## 3. Scope

### 3.1 Goals

- Remotely administer static nodes and gateways.
- Carry a normal console command line, not a second remote-only verb language.
- Route all accepted commands through the same handler map used by local serial/BLE command input.
- Return the handler's standard textual output without remote-only binary encoders.
- Return a terminal result even when the command itself prints nothing.
- Support multi-DM output with gap, duplicate, and completion detection.
- Keep exact `status` and `routes` available as explicitly unauthenticated cleartext diagnostics.
- Authenticate and encrypt operator/owner traffic end-to-end; intermediate relays transport opaque bytes
  and receive no administration key merely by relaying them.
- Keep controller private-key selection, request sealing, response authentication, sequence assembly, and
  decryption in the controller node. The companion exchanges only local plaintext commands, results, and
  transport acknowledgements.
- Support the node's normal identity as the default controller credential and ten persistent dedicated
  management identities selected explicitly per request.
- Resolve the selected controller public key to its target ACL slot through an authenticated bootstrap; do
  not require a companion or operator to maintain target-side slot numbers.
- Authenticate the managed target through its own stable administration identity, held independently of
  its ordinary messaging identity and preserved across ordinary `regen`.
- Bind the controller node's stable routable `SOURCE_HASH` into authenticated requests and retained response
  state, so a copied request cannot redirect its result by changing unauthenticated carrier metadata.
- Retain a completed authenticated BLE result until the local companion acknowledges it; do not acknowledge
  and discard a target transcript before the local consumer has accepted it.
- Make requests independent within a target session and safe to retry exactly.
- Keep wire overhead small and all target state bounded.
- Split the abilities to originate, transport, and accept remote administration.

### 3.2 Non-goals for the first implementation

- Parallel command execution. The firmware may serialize dispatch in arrival order; request independence is
  a protocol property, not a promise of concurrent handlers.
- A certificate hierarchy, delegated certificates, per-command ACL bitmasks, roles beyond
  open/operator/owner, or time-based expiry.
- Remote acceptance by mobile nodes. A mobile may originate or carry an opaque request, but is not a target.
- Preserving the current password-derived shared administrator identity as the trust model.
- Passphrase-encrypted management-key storage or a hardware secure-element requirement. Dedicated seeds
  receive the same at-rest protection as the node's ordinary identity in the first implementation.
- Importing, generating, exporting, or deleting dedicated private management keys over BLE. Those secret
  operations are physical USB-serial only; BLE may select an already-installed key and inspect public
  metadata.
- Guaranteeing an unknown mutating command's outcome across target power loss without a durable operation
  journal.
- Guaranteeing an in-flight result across controller-node power loss without a durable controller journal.
- Inventing a remote-only formatter for every command.
- Defining the companion UI in this document.

## 4. Source-verified constraints (refreshed 2026-09-01)

The following are code facts, not inherited assumptions from an older design:

- `protocol::lora_max_frame_bytes = 255`, the C++ DATA header is 8 bytes, and `TxItem::inner` supplies a
  241-byte STORAGE bound (`lib/core/protocol_constants.h`, `lib/core/node_carriers.h`). The on-air authority
  is `data_inner_cap()` / `data_frame_len()` (`lib/core/frame_codec.h`, the landed [[B20]]/[[B21]] rule), so
  every carrier's governing cap is the minimum of the storage bound and the actual packer's air-fit answer.
  The 241-byte term happens to govern the current RPC carriers; it is not a universal DATA-frame formula.
- The supported normal-DM body ceiling is the deliberately conservative `dm_max_body_bytes = 239`.
- `DATA_TYPE_REMOTE_CMD = 0xA0` and `DATA_TYPE_REMOTE_RESP = 0xA1` already carry remote request/response bodies
  (⛔ corrected 2026-08-29: ordinals 6/7 were RETIRED by the §CUSTODY-A namespace transition — the values now sit
  in the internal range's administration/security block `0xA0..`. Reusability is unchanged; no third DATA type is
  needed. Both are protocol-internal (`0x80..0xBF`), so `data_type_traits()` reports them
  `internal=true, generic_send_lifecycle=false` — the RPC's own response/timeout contract is the only outcome they
  carry, and no generic `send_acked`/`send_failed` may be raised for them.)
  (`lib/core/frame_codec.h`, `lib/core/node_mac.cpp`). They are reusable; no third DATA type is needed.
- The current addressed receive path consumes both remote types in an explicit handler at
  `lib/core/node_mac_rx.cpp:1958`, before the §CUSTODY-B protocol-internal tail guard. That placement is a
  compatibility invariant, not incidental ordering: v2 must replace it with explicit pre-tail handlers owned
  by direction/capability — `REMOTE_CMD` by **accept**, `REMOTE_RESP` by **client**. A build lacking the owner
  for an addressed type deliberately reaches the bounded `unsupported_internal` guard-drop. In particular,
  an accept-disabled mobile drops an addressed REQUEST but its enabled client still consumes a RESPONSE.
  Removing staging without installing those v2 handlers would make addressed RPCs disappear fail-closed but
  operationally invisible.
- A current sealed request body costs 35 bytes before command text:
  `[sealed_flag 1][rand8 8][nonce_ctr 2][node_hash 4][replay_counter 4][tag 16]`. Under the supported
  239-byte ceiling, at most 204 bytes remain for a command.
- The current target `remote_exec()` has its own small verb set and binary response encoders; it does not call
  the shared `dispatch()` (`src/firmware_remote.cpp`). Unknown or oversized results can therefore disappear
  without a response.
- `Identity` already has one 32-byte master seed and `identity_from_seed()` derives its Ed25519 and X25519
  material. `ed_pub_to_x25519()` and `ecdh_shared()` are the existing conversion/ECDH path
  (`lib/core/identity.h`). A dedicated key slot therefore needs to persist only one seed, not a second
  hand-maintained expanded keypair.
- `/mrpeers` persists only 16 authoritative or pinned messaging-peer keys and shares their eviction/capacity
  domain with ordinary DMs (`src/device_nv.h`, `lib/core/node_hashlocate.cpp`). Owner ruling: remote
  administration must not reuse that store. Managed-target trust gets a separate 32-entry controller-side
  record; a 32-bit routing hash alone is not a cryptographic trust anchor.
- A same-layer registered-mobile `MOBILE_SEND` carrying a non-zero enclosed type prefixes one byte and the
  home accepts that wrapper only with both `DST_HASH` and `SOURCE_HASH`. Against the current 241-byte inner
  buffer, `[dst_hash 4][origin 1][source_hash 4][enclosed_type 1]` leaves at most 231 bytes for the RPC body,
  before any cross-layer path overhead (`lib/core/node_hashlocate.cpp`, `lib/core/node_mac_rx.cpp`). Therefore
  239/214-byte direct-body arithmetic is not a carrier-independent contract.
- `protocol::wire_version` is a join/beacon/roster compatibility wall, not a version field on arbitrary typed
  DATA bodies (`lib/core/protocol_constants.h`, `lib/core/node_join.cpp`, `lib/core/node_beacon.cpp`,
  `lib/core/node_mobile.cpp`). The v2 remote codec must fail closed through its own opcode/layout; owner ruling:
  no global `wire_version` bump for this replacement.
- The legacy `rcmd` issuer already seals on the node and `fw_main` already opens its response on the node.
  Moving v2 controller cryptography into firmware changes the credential/session model, but does not invent
  a new device-side cryptographic boundary (`src/firmware_remote.cpp`, `src/fw_main.cpp`).
- `dispatch(line, len, Print&)` is the common textual handler map, but its current Boolean result says only
  whether a handler matched (`src/firmware_commands.cpp`).
- Serial and BLE currently handle `send`/`send_channel` around `dispatch()`, and `regen` writes through
  the global `mrcon` sink. Those paths are not yet transport-neutral despite sharing most command handling
  (`src/fw_main.cpp`, `src/firmware_commands.cpp`).
- `BufferSink` is a 512-byte whole-response capture and `LineSink` is a 1700-byte line streamer
  (`src/dispatch_sink.h`). Neither size should be adopted as the remote transcript budget without measuring
  real command output and board RAM.
- The main `/mrcfg` blob still embeds the legacy single-admin public key, replay floor, and provisioned flag.
  Configuration code rebuilds that blob for operations including `leave`, while separate versioned records
  such as `/mrjoin` and `/mrteams` already establish the pattern needed for an independent ACL
  (`src/device_nv.h`, `src/firmware_config.cpp`).
- The current local OTA backends are Wi-Fi SoftAP/WebServer upload on ESP32 and BLE DFU on nRF52
  (`src/device_ota.cpp`, `src/fw_main.cpp`). MeshRoute carries only the owner-authorized command that enters
  the board's OTA mode; it never carries the firmware image. The project has no MeshRoute
  application-signing trust anchor, which is why OTA cannot be delegated below owner.
- The node currently exposes one overwriteable inbound remote slot, so a future implementation must not
  claim pipelining until that ingress boundary is made bounded and explicit.
- BLE's current local command line buffer is 160 bytes. Node-side sealing avoids transporting a base64 RPC
  body through that buffer, but does not by itself raise the local BLE command-text ceiling
  (`src/device_ble.h`).
- `MR_PROFILE_MOBILE` currently forces the single `MR_FEAT_REMOTE_MGMT` off, while static and gateway builds
  retain it (`lib/core/mr_features.h`, `platformio.ini`). That single switch cannot express the approved
  client-without-acceptor mobile role and must be decomposed before the mobile controller path is enabled.

## 5. Architecture

There are five separate responsibilities:

1. **Local companion/console adapter** — submits a plaintext target, credential selector, and exact command
   line to the attached controller node; receives plaintext output plus structured progress/terminal state;
   and, for BLE, explicitly acknowledges local receipt. It performs no MeshRoute RPC cryptography.
2. **Controller endpoint in the node** — resolves the target's stable administration public key, selects `self`
   or a dedicated local management seed, creates and retains the exact sealed request, authenticates and
   reassembles responses, decrypts them, and decides whether retry is safe.
3. **Carrier adapter** — puts the controller node's opaque request bytes into the existing typed-DM path and
   returns opaque response bytes plus routing/source metadata. Static DM and mobile delegation may use
   different carrier adapters without changing the RPC body. An intermediate carrier needs no
   administration secret merely to forward bytes.
4. **Target authenticator/session** — uses its stable administration private identity, resolves the presented
   controller public key to an ACL slot, authenticates the request and its stable carrier source, performs
   replay/dedup checks, and creates encrypted response frames.
5. **Common dispatcher** — receives the original command line, writes normal command output to a `Print`
   sink, and returns a small transport-neutral completion classification.

The controller endpoint and the first carrier hop normally live in the same node; they remain separate
responsibilities so relays do not inherit access to plaintext or keys. The target retains one stable
administration private seed in addition to its independently regenerable ordinary MeshRoute identity; the
target ACL retains only controller public keys. The return address belongs to the surrounding carrier, not
to the sealed command body. Authenticated traffic must carry a stable logical `SOURCE_HASH` through that
carrier, bind it as AEAD associated data, copy it into the accepted-request record before dispatch, and use
that stored value for every original/replayed response. Mutable next-hop/relay headers remain outside AEAD.
Changing the stable source invalidates the tag rather than redirecting a valid request. This reuses carrier
identity bytes instead of adding a return address to every RPC body.

## 6. Trust and provisioning

### 6.1 Controller credentials and explicit per-request selection

The controller node supports exactly two credential forms:

- `self` — the node's normal `/mrid` identity; and
- `key0` through `key9` — ten stable local slots holding identities generated explicitly for management.

The target treats both forms identically. It stores the selected credential's full Ed25519 public key in
one ACL slot and does not store a credential-type flag. Authority is target-local: the same public key may
be owner on target A and operator on target B.

Credential choice is explicit in each local request and is copied into the bounded pending-request record.
There is no mutable global "active management key" from which a later response or retry may infer identity.
The conceptual local syntax is:

```text
remote <target> [using=self|keyN] -- <exact command line>
remote <target> open -- status
remote <target> open -- routes
```

Omitting `using=` means authenticated `using=self`. Open access must be written explicitly and never becomes
a fallback after target-key lookup, bootstrap, or authentication failure. The `--` delimiter is
load-bearing: bytes after it are passed unchanged to the common dispatcher, including spaces and quoting;
the local `remote` wrapper is not part of the sealed command text.

For an authenticated request, `<target>` resolves through the separate management-target book to one stable
target-administration public key plus current routing metadata. A local management-target label or the
stable administration-key fingerprint is the trust selector; a short static ID or ordinary node hash may
locate it only when the current binding resolves unambiguously to that same record. Neither routable value
alone is an ECDH trust anchor. Missing, stale, or ambiguous target authority refuses before transmission.
The explicit open form needs only an unambiguous routable target and makes no target-key or
target-authentication claim.

The controller key choice affects only authentication and encryption. Routing still uses the selected
target and the existing static or mobile-delegated carrier.

### 6.2 Dedicated controller keyring

The controller has exactly ten dedicated management slots, independent of the target's ten ACL slots. Each
occupied local slot persists only one 32-byte master seed. Firmware derives the full `Identity` through
`identity_from_seed()` when needed, uses the existing conversion/ECDH path, and wipes transient expanded
secret material when the operation no longer needs it. Firmware must not persist separately supplied public
and private halves that can disagree.

The seeds live in their own versioned record (provisionally `/mrmkeys`), not in `/mrcfg`, `/mrid`,
`/mrpeers`, `/mrtargets`, `/mradmid`, or `/mracl`. Network create/join/leave and ordinary configuration
preserve the keyring; `factory_reset` removes it. Its transaction follows the same fail-loud discipline
required for `/mracl`: classify absent, valid, corrupt/unsupported, and I/O-failed states; validate a
candidate; save it durably; and only then
activate it. A failed write leaves the prior live keyring unchanged. Corrupt/unreadable keyring state refuses
dedicated-key use while leaving `self` and explicit open diagnostics available.

The conceptual local commands are:

```text
admin-key list
admin-key show <self|keyN>
admin-key generate <keyN>
admin-key import <keyN> <64-hex-character-seed>
admin-key export <keyN>
admin-key remove <keyN> confirm
```

`list` and `show` expose only public keys/fingerprints and may be used through USB or secured BLE. Generating,
importing, exporting, or removing secret material is physical USB-serial only in the first implementation.
BLE may select an already-installed key for a remote request but cannot create, extract, replace, or delete
one. Imports derive the public identity from the seed before display and durable activation. Duplicate local
public identities are rejected rather than creating two names for one principal. A slot referenced by an
in-flight request or a retained BLE result cannot be replaced or removed.

Dedicated seeds have the same at-rest physical-capture exposure as `/mrid`; this slice adds no passphrase or
secure-element wrapper. Export/import of the same seed intentionally lets another controller node represent
the same ACL principal. Those controllers then have the same role and target slot, cannot be audited or
revoked separately, share that target slot's session, and are all revoked by removing that one target ACL
entry. Administrators who need independent revocation or attribution provision different keys.

`regen` changes only `self`. Any target ACL authorizing the old normal node identity stops authorizing the
regenerated controller, and the local confirmation/output must warn about that unavoidable consequence.
Dedicated management slots and the target book survive `regen` and remain independent. A controller refuses
`regen` while any RPC, response assembly, retained local result, or response-ACK debt still binds its current
routable source identity; once those records are clear, regeneration remains available normally. Stable
administration across controller regeneration uses a dedicated `keyN`, not `self`.

### 6.3 Stable target administration identity and target book

Each managed static node has one stable administration identity, provisionally persisted as one 32-byte
master seed in a separate versioned `/mradmid` record. Firmware derives its Ed25519/X25519 material through
the same canonical `identity_from_seed()` path used elsewhere. The seed is generated during first-owner USB
provisioning, never exported through the ordinary remote/BLE surface, survives ordinary `/mrid` `regen`,
network create/join/leave and configuration changes, and is erased by `factory_reset`. Rotating or recovering
this root identity is physical USB-only in v2; ordinary remotely invoked `/mrid` `regen` does not rotate it.

The managing node stores the corresponding target administration public keys in a separate versioned
`/mrtargets` record, never in `/mrpeers`. Its design capacity is 32 entries. Each valid row binds the full
32-byte target administration public key to bounded display/selection data and current routable metadata;
the exact packed fields and update rules are finalized from the routing and ABI audit. The public key is the
cryptographic target identity, while node ID/ordinary identity hash/network location are replaceable routing
hints. Messaging-peer expiry, replacement, or capacity pressure cannot mutate this book.

The 32-row capacity must be measured on every essential controller ABI before its storage slice is approved.
If it does not fit the accepted RAM/NV budget, implementation stops for an owner decision; it must not
silently fall back to 16, reuse `/mrpeers`, or evict a live/pinned management target. Absent, valid,
corrupt/unsupported, and I/O-failed states are distinct, candidate writes are validated and durable before
activation, and `factory_reset` removes the record.

These two directions are intentionally different: `/mracl` answers **which controller may manage me** by
holding controller public keys, while `/mradmid` plus `/mrtargets` answer **which managed target am I talking
to**. Authenticated encryption needs both. Merely preserving the ACL cannot authenticate or encrypt to a
target whose only private identity was regenerated.

### 6.4 Initial physical USB trust exchange

Initial ownership is established through physical USB serial in v2:

1. the operator chooses the controller node's `self` identity or generates/selects one dedicated slot;
2. the target generates or loads its stable administration identity and exposes its full public key;
3. the controller transactionally stores that key and the target's initial routing metadata in `/mrtargets`;
4. the target transactionally stores the selected controller's full public key as the first owner in
   `/mracl`; and
5. both nodes show full-key fingerprints so the operator can verify both directions.

Provisioning reports success only when the required target-identity and ACL records are valid and durable;
partial failure remains physically recoverable and never invents an active remote owner. The controller
private seed never enters the target, and the target administration seed never enters the controller. The
target need not export its chosen ACL row to a companion or persist a controller-side target-slot map: the
authenticated discovery bootstrap in §7.2 returns the current slot. A password is not sent as the remote
authenticator.

The storage and provisioning operation remain transport-neutral internally so a later BLE implementation
can reuse them, but BLE is not a v2 first-owner authority. Enabling it later requires a separately reviewed
physical-presence mechanism; a secured bond/static PIN alone is not physical presence.

### 6.5 Fixed flat target ACL and persistence

The target has exactly ten stable credential slots. Each slot contains either no entry or:

- one full 32-byte controller public key; and
- one role: `operator` or `owner`.

Slot numbers are stable wire handles, not security identities. Empty entries are not compacted, an ACL-full
condition refuses loudly, and adding a duplicate public key is rejected. The target does not persist display
labels; it reports slot, role, and a fingerprint. Controller-side `keyN` names do not imply or equal a
target-side ACL slot number.

The ACL belongs in its own versioned `/mracl` record, not in the main `/mrcfg` blob. The current `/mrcfg`
contains the legacy single-admin fields and is rebuilt by configuration operations such as `leave`; remote
network switching must not erase remote-management authority. The separate record remains in the ordinary
factory-reset domain, so normal network create/join/leave/configuration preserves it while `factory_reset`
removes it. Its exact packed layout is an implementation-slice decision, but the following persistence
semantics are fixed:

- absent, valid, corrupt/unsupported, and I/O-failed states are distinguished;
- corrupt or unreadable ACL state refuses all authenticated administration and requires physical
  recovery; the separately approved open diagnostics remain available;
- mutation uses candidate copy, full validation, durable save, then live activation;
- save failure leaves the old ACL and sessions active and returns an error;
- role change or removal invalidates that slot's authenticated session only after the new ACL is durable.

### 6.6 ACL-management invariants

- Several owner entries are allowed.
- Only physical USB provisioning may create the first owner in v2.
- A remote owner may list entries and add an operator or owner.
- A remote owner may change an entry's operator/owner role or remove an entry.
- An operator cannot inspect full public keys, grant, revoke, or change any ACL role.
- The last owner cannot be removed or demoted remotely.
- A request cannot remove or demote its own authenticating slot. Rotation is add replacement owner, verify a
  session through that owner, then remove the old entry.
- ACL changes acknowledge success only after the new state is durable.

The common command family is conceptually `acl list`, `acl add <operator|owner> <public-key>`,
`acl set <slot> <operator|owner>`, and `acl remove <slot> confirm`; exact textual encoding and fingerprint
format are finalized with the dispatcher slice. Roles are fixed policy levels, not owner-editable
per-command permission masks.

### 6.7 Why the target stores public keys

A symmetric management secret would not reduce the per-message AEAD tag and would make the target a holder
of controller credentials. Pinning public keys lets the target authorize only public material and reuses
MeshRoute's existing identity conversion, ECDH, BLAKE2b, and XChaCha20-Poly1305 primitives. A deliberately
shared management private key remains possible, but sharing occurs only by exporting and importing the same
controller seed and has the one-principal consequences described in §6.2.

## 7. Compact authenticated session

### 7.1 Base and session keys

For an authorized slot, both endpoints derive a target-specific base key from static ECDH. The KDF is domain
separated for remote administration and binds the full controller-credential and target-administration
Ed25519 public keys in one fixed order. The controller derives it from the selected local credential and the
target public key stored in `/mrtargets`; the target derives it from `/mradmid` and the controller public key
stored in the matched ACL row. Ordinary `/mrid` regeneration changes neither side of this ECDH pair.
Intermediate relays see only encoded RPC bodies.

Both paths must reject an invalid peer conversion or an all-zero/low-order X25519 shared result before the
KDF. Failure is an authentication failure, never a usable all-zero base key and never a fallthrough to the
open decoder.

After target boot, an authenticated bootstrap exchange returns a fresh random 64-bit `admin_epoch` for the
selected key slot. Both ends derive:

```text
session_key = KDF(base_key, admin_epoch, "MeshRoute remote-admin v2 session")
```

Normal requests use `session_key`; `admin_epoch` is not carried in them. That saves eight bytes on every
command. Bootstrap messages use `base_key`, so a controller node can recover after reboot without knowing
the new epoch.

Each authorized slot has its own epoch/session. The epoch is stable for that administration session, not
advanced by commands or responses. Consequently, losing command `A` or any response for `A` has no effect
on command `B`, and rolling one credential session does not invalidate any other ACL slot.

### 7.2 Authenticated ACL discovery and bootstrap

The controller must not require the operator or companion to know the target's ACL row. It sends a
`BOOTSTRAP` request using the bootstrap opcode and no established-slot value, with a fresh `request_id`, its
selected full Ed25519 public key, and a tag under the target-specific base key. The public key is clear
authenticated data, not a secret. The target performs an exact full-key lookup across its bounded ten-row
ACL, derives the base
key only for the matched row, and verifies the tag. Missing keys, invalid key conversion, and invalid tags
are silent authentication failures and do not reveal ACL membership.

The target returns the matched slot and its current epoch in clear but authenticated under the base key and
bound to that request ID. The epoch is a nonce/session generation, not a secret. Keeping it clear lets it
participate in the bootstrap-response nonce, so replaying the same bootstrap request after a target reboot
cannot make the target encrypt different epoch plaintext under a reused base-key nonce. Replaying an old
bootstrap response for another ID, slot, or epoch fails authentication.

The controller caches `(full target-administration public key, selected full controller public key) ->
(target ACL slot, admin_epoch)` in bounded RAM. That cache is an optimization/session handle, not authority
and not companion configuration. It is discarded on controller reboot and refreshed through discovery.
Authentication failure using a cached slot may trigger a new discovery bootstrap for the same explicitly selected
credential; it must never try a different local credential or downgrade to open access.

Bootstrap does not dispatch a command and never changes the epoch. It is needed after first provisioning,
controller or target reboot, a stale slot/session cache, or a lost rollover response; it is not a
per-command challenge round trip. Because it is read-only, replaying a captured bootstrap request cannot
rotate or disrupt a later session.

### 7.3 Explicit bounded-session rollover

When an execute request receives `session_full`, the controller may send `SAFE_ROLLOVER` under the current
session key with a fresh request ID and no plaintext. Execute capacity reserves admission for this control
message. The target accepts it only for the same key slot/current session, when no operation is executing,
and when every old response transcript has been acknowledged. It then rotates that slot to a fresh random
epoch, clears the now-cryptographically-invalid old records, and returns the new epoch in the
rollover-result layout under the base key, bound to the rollover request ID, with abandoned count zero.

If any completed transcript remains unacknowledged, safe rollover refuses with `session_busy` and a bounded
count; it never destroys the transcript. The controller keeps bounded response-ACK debt and retries the
exact authenticated ACK opportunistically. Because the ACK itself has no ACK-of-ACK, a permanently lost ACK
may leave an otherwise completed session unable to roll safely.

`FORCE_ROLLOVER` is the explicit recovery. It is never automatic: the controller presents the count of
unacknowledged outcomes locally, requires a literal confirmation, and only then sends the authenticated
control. The target again requires no operation executing, rotates the epoch, and reports how many retained
outcomes were abandoned in the rollover result. Those outcomes become `outcome_unknown`. This is occasional
bounded garbage collection, not a token consumed by each command.

After either rotation, the old rollover request cannot authenticate under the new session key. If its
response is lost, ordinary read-only bootstrap discovers the already-current epoch. If several controller
nodes deliberately share one management credential, they also share this slot session: rollover by one
invalidates the others' cached epoch, and they bootstrap again. Safe rollover cannot abandon another controller's
unacknowledged result; force rollover can, but only through the explicit confirmed recovery contract above.
Key sharing does not recreate a continuous per-command counter or prevent later independent requests.

## 8. Proposed wire bodies

All multi-byte integers below use one declared byte order in the codec; little-endian is proposed to match
the surrounding C++ wire idiom. Exact codec constants belong in one shared implementation path (U2).

### 8.1 Control byte, opcodes, and slot values

The control byte is transmitted as the first byte of every RPC body. Keys, ACL rows, epochs, and request
tables are local state; `ctl` is not. It remains one byte so every command and every multi-frame response
does not pay a second byte:

```text
bits 7..4  opcode within REMOTE_CMD or REMOTE_RESP
bits 3..0  established ACL slot 0..9, or sentinel F
```

The outer DATA type already distinguishes request from response. Proposed `REMOTE_CMD` opcodes are
`AUTH_EXECUTE`, `OPEN_EXECUTE`, `BOOTSTRAP`, `RESPONSE_ACK`, `SAFE_ROLLOVER`, and `FORCE_ROLLOVER`; the
remaining ten command opcodes stay reserved and reject. Proposed `REMOTE_RESP` opcodes are `OUTPUT`,
`TERMINAL`, `BOOTSTRAP`, `ROLLOVER_RESULT`, and `PROTOCOL_ERROR`; the remaining eleven response opcodes stay
reserved and reject. Exact numeric values are frozen with the codec KAT slice, append-only thereafter.

Slots 0..9 select established authenticated ACL sessions. Nibbles A..E are reserved and reject. Sentinel F
is valid only with `OPEN_EXECUTE`, an open response, or a bootstrap request before the target ACL row is
known. A bootstrap response carries the actual matched slot 0..9. Open and bootstrap are opcodes, not fake
ACL slots. The decoder chooses one body layout from outer direction, opcode, slot class, and exact length;
an invalid authenticated request never falls back to the open decoder.

The opcode namespace plus the remote-v2 KDF labels are the subprotocol discriminator. A later incompatible
remote codec consumes a reserved opcode namespace or a separately allocated DATA type. It does not consume
the unrelated global join/beacon `wire_version` merely because an `0xA0`/`0xA1` body changed.

### 8.2 Authenticated execute request

```text
REMOTE_CMD body, slot 0..9:
    ctl             1
    request_id      8   random u64, clear but authenticated
    ciphertext      N   exact command-line bytes; no terminator and no length field
    tag             16  XChaCha20-Poly1305 tag
```

The DATA body length supplies `N`; a separate command length would be redundant. The RPC envelope overhead
is 25 bytes. The final command limit is `carrier_rpc_body_cap - 25`, not universally 214; §8.11 defines the
current measured example and the one packer-derived authority.

`request_id` identifies the logical operation and supplies the unique seed for nonce derivation. It
replaces both the current replay counter and the separate random/nonce-counter wrapper.

### 8.3 Open execute request

```text
REMOTE_CMD body, OPEN_EXECUTE + sentinel F:
    ctl             1
    request_id      8   random correlation ID; not an authenticator
    command         N   cleartext exact command-line bytes
```

The envelope overhead is nine bytes, although policy accepts only exact `status` and `routes`, both far
below every supported carrier bound. It has no confidentiality, integrity, controller authentication, or
target authentication. The ID only correlates response chunks. These two commands are read-only, so the
open path does not need authenticated at-most-once execution or a retained response ACK. It remains subject
to bounded admission and rate limiting so replay cannot create unbounded response work. An authorized
controller may instead send either diagnostic through `AUTH_EXECUTE` when it needs an authenticated,
confidential result.

### 8.4 Authenticated discovery/bootstrap request

```text
REMOTE_CMD body, BOOTSTRAP + sentinel F:
    ctl             1
    request_id      8
    controller_pub  32  selected full Ed25519 public key; clear, authenticated
    tag             16  base-key tag over the complete header; no ciphertext
```

The fixed cost is 57 bytes and is paid only when establishing or rediscovering a session. The full public
key makes target ACL lookup exact; its short hash is never used as authorization. The target returns nothing
unless that full key is present and the base-key tag validates.

### 8.5 Authenticated session control request

```text
REMOTE_CMD body, slot 0..9:
    ctl             1   RESPONSE_ACK, SAFE_ROLLOVER, or FORCE_ROLLOVER
    request_id      8
    tag             16  authenticates the header; no ciphertext
```

Both rollover opcodes use the current session key and a fresh request ID. `RESPONSE_ACK` uses the current
session key and the acknowledged execute request's ID; its distinct opcode nonce domain prevents collision
with that execute request. `FORCE_ROLLOVER` is accepted only after the controller's local literal-confirmation
state selected that opcode; it is never inferred from a failed safe request.

### 8.6 Bootstrap response

The base-key response has its own fixed layout:

```text
REMOTE_RESP body, BOOTSTRAP kind:
    ctl             1   actual matched ACL slot 0..9
    request_id      8
    admin_epoch     8   clear, random, authenticated; also a nonce input
    tag             16  authenticates the complete header; no ciphertext
```

The epoch does not need confidentiality: without the controller private key it cannot produce the base or
session key. Its clear presence makes the response nonce unique when boot/session rollover changes the
answer to a replayed bootstrap request. This 33-byte bootstrap cost is occasional and consumes no regular
command payload.

A successful safe or force rollover uses a separate base-key response opcode because it reports one more
fact than discovery:

```text
REMOTE_RESP body, ROLLOVER_RESULT kind:
    ctl                 1   actual ACL slot 0..9
    request_id          8
    admin_epoch         8   new clear authenticated epoch
    abandoned_count     1   zero for safe; exact bounded count for force
    tag                 16
```

This occasional response is 34 bytes. The distinct opcode and epoch-bearing nonce domain prevent it from
colliding with a bootstrap response carrying the same request ID.

### 8.7 Authenticated session response frame

```text
REMOTE_RESP body, slot 0..9:
    ctl             1   OUTPUT, TERMINAL, or authenticated protocol error
    request_id      8
    response_seq    1   starts at 0 and increments across every frame for this response
    ciphertext      N
    tag             16
```

The authenticated response envelope overhead is 26 bytes. Its output limit is
`carrier_rpc_body_cap - 26` and may differ by return carrier.

### 8.8 Open response frame

```text
REMOTE_RESP body, sentinel F:
    ctl             1   OUTPUT, TERMINAL, or clear protocol error opcode
    request_id      8
    response_seq    1
    plaintext       N
```

The open-response envelope overhead is ten bytes. Anyone may read, alter, inject, or replay it;
the controller and companion must label open results as unauthenticated. If an open transcript is
incomplete, the controller starts a new read-only request with a new ID rather than relying on authenticated
transcript replay.

### 8.9 Terminal frame

The terminal frame is the next contiguous `response_seq` after the last output frame. Its authenticated
ciphertext or open plaintext starts with one compact result code. Proposed meanings are:

- `completed` — a matching handler returned; its normal text contains any command-specific warning/error;
- `scheduled` — a disruptive action was accepted and deferred until response handling permits it;
- `unknown_command` — no common handler matched;
- `refused` — the authority/transport is not allowed to run this command;
- `output_truncated` — the handler returned, but the bounded transcript could not retain all output;
- `internal_error` — execution or response staging failed before a truthful normal result existed;
- `session_full` — no authenticated execution occurred; explicitly roll over the bounded session and retry
  is safe; and
- `session_busy` — safe rollover refused because one or more completed transcripts remain unacknowledged;
  the bounded detail carries their count and no epoch/state changed.

Optional short detail bytes may follow, but ordinary handler output must not be duplicated into a special
terminal encoding. The `scheduled` result is the exception that must carry its bounded activation delay.
A successful command that prints nothing returns a terminal frame at sequence zero.

`response_seq` is one byte deliberately: at most 256 frames can belong to one response, with no more than
255 output frames followed by the required terminal frame. Even under the smallest accepted carrier cap,
that is already far beyond the response transcript firmware should retain in RAM. A two-byte sequence would
cost one byte in every response DM without increasing a usable target limit.

### 8.10 Authenticated response acknowledgement and local delivery

After authenticating and receiving one contiguous series from sequence zero through `TERMINAL`, the
controller node decrypts and retains the result for the local transport that originated the request. It
sends a sealed `RESPONSE_ACK` with the same `request_id` only after local delivery has been accepted:

- for USB serial, successful admission of every byte of the complete plaintext transcript and terminal by
  the request's supplied local sink counts as acceptance; a console-stage drop does not; and
- for secured BLE, the node emits structured plaintext output/terminal events containing the request ID and
  retains the complete result until the companion sends an explicit local acknowledgement for that ID.

A BLE disconnect is not an acknowledgement and does not evict the result. While retained, the pending
record continues to bind the target-administration public key, stable controller `SOURCE_HASH`, request ID,
selected local key, target ACL slot, and epoch; firmware must not infer any of them from current UI or
key-selection state. The sealed target `RESPONSE_ACK` body is
25 bytes. After a secured-BLE reconnect, the controller re-offers the retained complete result from its
beginning until that local request ID is acknowledged; an ACK received before the complete local result is
available is refused. A failed USB admission likewise retains the result for explicit local re-offer. The
target ACK lets the target release the potentially large encrypted transcript while retaining a compact
already-executed record for replay safety.

An ACK is not in the critical path for the next command. Losing it consumes bounded cache for longer, but
does not desynchronize requests. After local acceptance, the controller retains compact bounded ACK debt and
retries the exact sealed ACK opportunistically; it must not infer target receipt merely because it
transmitted one. A controller whose retained-BLE-result or ACK-debt capacity is full refuses a new
authenticated operation before transmission; it never evicts a live result and falsely acknowledges it. Permanently lost
ACK debt is resolved only by the explicit safe/force rollover contract. Open responses do not use this ACK
contract.

### 8.11 Byte budget

| RPC body | Fixed RPC overhead | Application bytes |
|---|---:|---:|
| Authenticated execute request | 25 | `carrier_rpc_body_cap - 25` |
| Open execute request | 9 | `carrier_rpc_body_cap - 9` |
| Authenticated output response | 26 | `carrier_rpc_body_cap - 26` |
| Open output response | 10 | `carrier_rpc_body_cap - 10` |
| Authenticated discovery/bootstrap request | 57 | 0 |
| Authenticated ACK/safe-rollover/force-rollover request | 25 | 0 |
| Authenticated bootstrap response | 33 | 0 |
| Authenticated rollover result | 34 | 0 |

`carrier_rpc_body_cap` is obtained from the actual DATA packer and required immutable carrier fields, never
from `dm_max_body_bytes` by analogy. As a current source-verified example, the same-layer registered-mobile
request wrapper must fit `[dst_hash 4][origin 1][source_hash 4][enclosed_type 1][RPC body]` in the 241-byte
inner buffer, so its RPC-body cap is 231 and its authenticated command cap is 206. A cross-layer path spends
additional carrier bytes and must publish its own lower bound. Direct static, static-to-mobile, and every
reverse path receive the same packer-derived treatment in the carrier slice.

The 16-byte authentication tag and 8-byte request identity are the two load-bearing authenticated envelope
costs; the transmitted one-byte control is the remaining RPC metadata. Open diagnostics omit the tag by
explicit policy and therefore provide no security claim. No acceptance test may call 214/213 the universal
normal-command/output budget.

## 9. Nonce and request-ID rules

For authenticated traffic, a 24-byte XChaCha nonce is derived, not transmitted, from the selected
base/session key plus a domain containing the complete `ctl` byte, message direction, `request_id`,
`response_seq` (zero for requests), and the stable logical controller `SOURCE_HASH` captured from the
request carrier. The base-key bootstrap and rollover-result response domains additionally include the clear
`admin_epoch`, so the response to a replayed request after epoch rotation uses a different nonce. The
complete clear RPC header and stable logical source are authenticated as AEAD associated data. Full target
administration and controller-credential identities are already bound through key derivation. Mutable
next-hop, retry, and relay headers are not AEAD inputs.

Open bodies have no nonce or authentication tag. Their random ID is only a correlation value and
must never be described as replay protection or proof of origin.

Hard authenticated invariants:

- A controller node must never use one `request_id` for different plaintext under the same
  credential/session key.
- A retry retransmits the exact sealed request bytes; it does not reseal changed text under the same ID.
- Response sequence values are never reused for different plaintext within one request transcript.
- Bootstrap, authenticated/open execute, ACK, safe rollover, force rollover, output, and terminal domains
  cannot collide even when IDs are equal.
- Changing the stable logical controller source under otherwise identical authenticated bytes fails the tag
  and cannot replace the return identity retained on first acceptance.
- RNG failure refuses authenticated request creation loudly.
- Two controller nodes sharing one private credential are one logical principal. A random-ID collision is
  detected by the target's request fingerprint check; there is no shared mutable command counter to
  synchronize.

A random 64-bit ID remains the compact proposal, not yet a frozen claim. Target-side fingerprint detection
prevents ambiguous execution but occurs after two same-key/same-nonce ciphertexts may already have appeared
on air; it does not undo AEAD nonce reuse. The codec/session slice must first pin the maximum requests per
epoch, include deliberately shared-controller concurrency, calculate the collision bound, and obtain review
acceptance. If that bound is not accepted, the request ID widens to 96 or 128 bits and carrier budgets are
recomputed; payload preference is not a reason to misdescribe collision detection as cryptographic safety.

## 10. Execution, deduplication, and replay

The target keeps a bounded authenticated session table per ACL slot, keyed by `request_id` (the physical
storage may be one shared bounded pool). It also retains the original authenticated request tag as the exact
128-bit request fingerprint and the stable logical controller `SOURCE_HASH` authenticated by that request.

For an authenticated `EXECUTE`:

1. **ID absent and capacity available:** reserve the seen record and enough response/transcript capacity for
   a truthful terminal before dispatch, dispatch exactly once, retain the resulting transcript and terminal,
   then send it.
2. **ID present and request tag identical, transcript unacknowledged:** resend the original transcript from
   sequence zero; never dispatch again. The controller node discards duplicate sequence values.
3. **ID present and request tag differs:** reject as request-ID reuse after authentication; never dispatch
   either plaintext under an ambiguous ID.
4. **ID present and response already acknowledged:** never execute again and never create different response
   plaintext in the old nonce space.
5. **This ACL slot's session table is full:** return `session_full` without executing. A subsequent
   authenticated `SAFE_ROLLOVER` rotates only this slot when no operation is executing and every transcript
   is acknowledged. If an unacknowledged transcript remains, return `session_busy` with its bounded count and
   change nothing. Only explicitly confirmed `FORCE_ROLLOVER` may abandon those outcomes. After either
   successful rotation, the controller may seal the not-executed command under the new session.

Session records are not silently evicted while their session key remains valid. This prevents a captured old
request from becoming executable again merely because a small ring wrapped. Per-slot rollover invalidates
every old request for that slot cryptographically and is occasional bounded maintenance, not a
command-to-command token chain.

When several controller nodes share a credential, they share that slot's table and epoch. Rollover by one
invalidates the other controllers' cached epoch, so they bootstrap again. Safe rollover first proves there
is no unacknowledged operation to lose. A confirmed force rollover makes every old unacknowledged operation
for that credential `outcome_unknown`. Separate keys are required when one controller must not share another
controller's session-retention and force-recovery domain.

The response-transcript pool may be smaller than the seen-request table, but it must not silently evict an
unacknowledged transcript and then execute its request again. Under pressure the target refuses a new command
before execution. Exact capacities are deliberately not guessed in this proposal; implementation planning
must measure the real board ABI, current RAM headroom, and command-output census.

Open requests do not enter the authenticated seen table. Only exact read-only `status` and `routes`
are accepted, so a duplicate may safely produce a fresh snapshot. The target still reserves bounded output
capacity before dispatch and rate-limits the open path. An incomplete open response is retried as a new
read-only operation with a new correlation ID.

## 11. Multi-DM output contract

The remote sink is another bounded `Print` implementation following the existing sink seam. It captures the
same bytes the selected command handler writes locally and divides them according to the selected return
carrier's packer-derived RPC-body cap minus the authenticated/open response overhead in §8.11. Chunk
boundaries have no semantic meaning; the controller node concatenates plaintext in `response_seq` order.

For an authenticated response, the controller node:

- accepts frames only for the expected target, ACL slot, session, and request ID;
- decrypts and buffers or emits only a contiguous prefix;
- ignores exact duplicate sequence values;
- detects a gap and resends the exact sealed execute request to ask for transcript replay;
- considers the RPC complete only after a valid terminal frame follows all preceding sequence values;
- delivers plaintext only to the local transport that originated the request; and
- sends `RESPONSE_ACK` only under the local-delivery rule in §8.10.

For an open response, the controller uses target/routing metadata, request ID, and sequence only to assemble
a best-effort transcript and then gives plaintext to the originating local transport. None proves origin or
integrity. A gap causes a new read-only request with a new ID, and the displayed result remains explicitly
marked unauthenticated.

Sensitive authenticated plaintext is not copied to every local interface or global diagnostic console.
Firmware may expose non-secret counters and request-state metadata globally, but command text and response
text remain scoped to the USB or secured-BLE origin recorded at admission.

The implementation transcript is finite. If a handler writes beyond the per-operation limit, the sink marks
the operation truncated, stops retaining further bytes, and terminates with `output_truncated`. It never
silently presents a partial response as complete.

## 12. Common-dispatch integration and authority policy

The target must not retain a second `remote_exec` verb map. The intended seam is conceptually:

```cpp
struct CommandContext {
    CommandTransport transport;   // serial, BLE, remote
    CommandAuthority authority;   // local, remote_open, remote_operator, remote_owner
    bool physical_presence;       // explicit provisioning/recovery session
    uint64_t request_id;          // zero for a local command
};

DispatchResult dispatch(const char* line, size_t len, Print& out,
                        const CommandContext& context);
```

The exact type layout is an implementation decision, but these semantics are required:

- one command parser and one handler implementation;
- handler output always goes to the supplied `Print`;
- `DispatchResult` distinguishes at least matched/completed, unmatched, refused, scheduled, and internal
  failure without parsing printed text;
- policy is metadata or a check attached to the common command/subcommand, not a copied remote allowlist;
- the context distinguishes ordinary local authority from the explicitly physical provisioning/recovery
  state;
- local serial/BLE behaviour remains unchanged unless a separately reviewed command correction is needed.

Current local command execution is not yet perfectly unified: `send` and `send_channel` are handled by
the serial/BLE callers around `dispatch()`, and `regen` writes through the global console sink. The
implementation may not claim same-command parity until those applicable paths use one transport-neutral
execution seam and the supplied output sink. Per C1, that consolidation and remote enablement must remain
separately reviewable.

### 12.1 Three authority levels

| Authority | Command policy |
|---|---|
| **Open** | Only the exact `status` and `routes` command lines. They are cleartext and unauthenticated. No aliases, arguments, or other diagnostics inherit open access. |
| **Operator** | Every target-applicable ordinary command not classified owner-only or physical-only. This explicitly includes reading ordinary diagnostics/configuration, changing ordinary configuration, and creating, joining, leaving, or switching the static network. Gateway/radio configuration and operational recovery such as reboot are operator work, subject to the disruptive-command contract where applicable. Other command families are assigned by the required source census rather than inferred here. |
| **Owner** | Everything available to operator plus target-applicable commands that manage security, identity, the target ACL, private/secret application key material, factory reset, fault injection, and OTA. Remote owner is intended to have the same authority as a locally connected serial/BLE administrator except for the narrow physical recovery operations and controller-only trust surfaces below. |

Policy is attached at command/subcommand granularity. For example, a harmless team status subcommand need not
inherit the owner authority required for private-key operations such as `team exportkey`. Before
implementation, an exhaustive census of the current source handler map and the serial/BLE caller-only paths
must assign every command and subcommand exactly one minimum authority; anything missing refuses closed.

OTA is owner-only. A remote `ota` command may only schedule the target to enter an implemented **local**
firmware-receiver mode. MeshRoute never transports the firmware image: an operator must subsequently reach
the target's local Wi-Fi SoftAP/WebServer or local BLE DFU service. The terminal identifies the available
backend; a board with no implemented receiver refuses `unsupported` before scheduling. The
current ESP path accepts a raw image without a MeshRoute application-signing trust anchor, so granting this
mode to an operator would let that operator replace the code enforcing the ACL. A future signed-update
design may revisit the authority policy, but it is separate from this RPC.

Physical-only authority is deliberately narrow:

- installing the first owner when no valid ACL exists;
- recovering from a corrupt/unreadable ACL or from loss of every owner;
- generating, importing, exporting, replacing, or removing a dedicated controller-management seed;
- generating, recovering, or rotating the target's stable administration identity; and
- any future operation whose security proof explicitly requires physical presence.

All four operations above are USB-only in v2. Their internal service seam may be transport-neutral, but a
future BLE caller is not authorized until it supplies a separately reviewed physical-presence signal.

These exceptions are recovery roots, not a general local-only escape from owner parity. The old
`password`, `unlock`, and `lock` flow is not part of v2.

ACL-management commands are ordinary common-dispatch commands gated to `remote_owner` or approved local
physical authority. Operators cannot authorize another key.

The local `remote ...` wrapper and `admin-key ...` family are controller-side trust surfaces, not target
commands recursively available through an authenticated RPC. `remote ...` and public `admin-key list/show`
are accepted only from local USB or secured BLE; the secret-key operations remain physical USB-only per
§6.2. A remote owner may manage the target ACL and target application secrets under the ordinary policy,
but cannot make the target pivot through one of its controller credentials or extract/replace those
credentials. This is the explicit security reason for the exception to otherwise-local owner parity.

## 13. Disruptive commands and reply-path honesty

A command that reboots, halts, erases state, changes the ordinary target identity, starts OTA/fault
injection, or removes the current RF/return path cannot activate its disruptive side effect inside the
handler before a truthful acceptance result has entered the reply path. Examples include `reboot`, `prep-restart`,
`factory_reset`, ordinary `/mrid` `regen`, OTA-mode entry, destructive crash tests, and network/radio changes
that retune or detach the target.

The target first authenticates and authorizes the command, validates any existing explicit confirmation
token, reserves the response/transcript and deferred-action record, and performs only non-disruptive
preparation needed to make later activation reliable. It then returns `scheduled` with the bounded
activation delay. Erase, reboot, live retune/detach, and other reply-path-breaking effects remain deferred.
The action is activated only after:

- the `scheduled` terminal has been retained and accepted into the response transmission path; and
- either the controller node sends the authenticated `RESPONSE_ACK` after the local-delivery condition in
  §8.10, or a bounded fallback deadline expires.

The fallback means a lost ACK cannot block an accepted recovery action forever. Receiving `scheduled` tells
the controller—and, once locally delivered, its companion—that the target accepted the action and will
execute it by the stated deadline; loss of the subsequent ACK does not cancel that promise. Receiving no
response is different: the controller cannot know whether the request or response was lost, must report the
outcome as unknown, and must not automatically retry a non-idempotent command.

Existing command-level safeguards such as a literal `confirm` token remain in force. The transport-level
`scheduled` result is an additional pre-activation confirmation, not a replacement for deliberate user
confirmation.

Ordinary target `/mrid` `regen` preserves `/mradmid` and `/mracl`, so it does not rotate target
administration trust or authenticated sessions. It may still change routable identity metadata and is
therefore deferred until its response is safe exactly like another return-path-changing action. The
controller updates `/mrtargets` routing hints when it later obtains authenticated/current identity evidence.
Rotating `/mradmid` itself is not this command and is USB-only in v2.

Every target boot generates a different `admin_epoch`. An old authenticated request therefore fails under
the new session key. After timeout, the controller bootstraps and compares epochs:

- same epoch: resend the exact request; the target replays its transcript without re-execution;
- changed epoch and no terminal was received: outcome is **unknown** for a mutating command; do not retry it
  automatically;
- changed epoch for a source-verified read-only/idempotent command: the controller may offer or perform a new
  RPC according to explicit command policy.

A RAM-only design cannot honestly promise the old outcome after power loss. A durable operation journal could
add that guarantee later, but it is a separate NV/wear design and not hidden inside v2.

Controller reboot is also an honesty boundary. A late authenticated response is accepted only by a pending
record that already binds its target-administration public key, stable local `SOURCE_HASH`, selected
credential, slot, epoch, and request ID. Firmware must not try every stored key against an unsolicited
response. If a controller reboot loses a mutating request's
pending record before a terminal was locally delivered, its local outcome is unknown; the companion must
not silently recreate and retry it.

## 14. Controller, companion, profile, and carrier split

Three firmware capabilities are required instead of one broad `REMOTE_MGMT` meaning:

- **transport** — carry opaque request/response bodies and preserve their source/return-route metadata;
- **client** — accept a local plaintext request, select `self` or `keyN`, resolve the target-administration
  public key, discover/bootstrap the target ACL session, seal and retain exact request bytes,
  authenticate/decrypt and reassemble responses, enforce retry honesty, and deliver scoped local plaintext;
- **accept** — decode the target-side body, authenticate when required, enforce authority, and dispatch a
  remote command; enabled only for static nodes and gateways.

The companion-facing product surface enters the client capability with plaintext target/selector/command
data and receives plaintext response events plus a terminal. It does not accept an already-sealed body from
the companion, expose raw authenticated response bodies as the normal result, or ask the companion to apply
a private management key. Node-side sealing uses the exact command tail; it does not recreate the old
remote-only verb language.

Mobile-controller builds enable **client + transport** and keep **accept** off. A companion attached to a
mobile can therefore administer a multi-hop static target through the mobile's existing delegated routing
path without making that mobile remotely administrable. Static and gateway controller builds may enable
all three capabilities. A node acting only as an intermediate relay neither opens the RPC body nor needs a
dedicated management secret merely to forward it; a secret is present only if that node itself has an
occupied client key slot.

The current `MR_FEAT_REMOTE_MGMT` flag and its `MR_PROFILE_MOBILE => 0` rule cannot represent this split.
Implementation replaces it with independently testable client and accept state (transport may be a common
core capability or its own feature as measurement dictates). Compile-time controls must prove that mobile
acceptance remains absent while mobile origination is present, and that disabling client keyring code does
not disable ordinary relay transport.

The receive-dispatch side follows the same capability split. `REMOTE_CMD` has an explicit pre-tail consumer
only when **accept** is enabled; `REMOTE_RESP` has one when **client** is enabled. Static/gateway controllers
may own both. An accept-disabled mobile therefore guard-drops an addressed request with bounded scalar
telemetry after every forwarding role, while still consuming an addressed response through its client. A
transport-only build guard-drops either type when it is the final destination. No addressed RPC may depend on
ordinary DM delivery, and replacing the legacy staging arm must not accidentally remove either owned handler.

The compact RPC **layout** is carrier-independent; its maximum body length is not. Every remote-admin carrier
is explicitly static/global-plane traffic (`Plane::GLOBAL`), never `AUTO` and never team-plane. An
authenticated carrier must preserve mandatory `SOURCE_HASH`, expose it to the codec as associated data,
retain it before the incoming flight identity is lost, and use it as the response destination. It must ask
the existing DATA packer for the remaining body cap and expose enqueue failure rather than silently losing
an accepted response. Open bodies are cleartext, but the carrier still treats them as opaque protocol
payloads and applies its own measured cap.

### 14.1 Custody notices and the mobile-controller boundary

`DATA_TYPE_REMOTE_CMD` and `DATA_TYPE_REMOTE_RESP` are deliberately custody-eligible when their OUTER DATA
flight is plaintext, static/global, same-layer and otherwise satisfies custody §10.1. Application-level RPC
sealing does not set `DATA_FLAG_CRYPTED`: relays still see an opaque body inside a plaintext DATA carrier.
Custody excludes only a notice about another custody notice and an E2E ACK by type; internal RPC types are not
exempt. An `AUTH_EXECUTE` request, `OUTPUT` response, or any other RPC opcode dying at an eligible transit
relay can therefore produce the existing `custody_failure` record/push.

That evidence is a separate, unauthenticated carrier observation. It must never enter the `REMOTE_RESP`
decoder, satisfy an RPC terminal, become an authentication error, or prove that the target did not execute:
another copy may already have arrived. For a STATIC controller, a pending request may correlate the notice
only on the exact outer `{failed_dst, failed_ctr}` pair and expose a nonterminal
`transport_loss_reported / target_outcome_unknown` fact. It retains the pending request and the exact sealed
bytes. The report may make an explicit exact-bytes retry available under §10's existing dedup rule, but it
does not itself trigger a retry, dispatch, route change, trust change or session rollover; this preserves the
custody design's unauthenticated-evidence rule. A report about a response likewise remains separate factual
evidence at the target; the first implementation does not change transcript resend timing solely because it
arrived—the authenticated response-ACK/debt rules remain authoritative.

The PRIMARY product controller topology is a mobile attached locally over USB (and later secured BLE). Its
carrier has two identities: the mobile delegates wrapper `{dstHome, ctrM}` to Home1, then Home1 re-originates
the static/global RPC flight under `{dstTarget, ctrH}`. The wrapper and final hosted-mobile last mile are
outside custody v1; the inter-home/static leg is eligible, but a request-loss notice names Home1/`ctrH` and
terminates at Home1 while the controller pending record lives on the mobile under `ctrM`. There is currently
no custody equivalent of the E2E-ACK `ctrH -> ctrM` translation. Therefore [[B278]] is a REQUIRED prerequisite
for Slice 9, not optional polish: its reviewed design must prove the delegated-correlation record lives across
every eligible terminal, translate the complete identity (never counter alone), forward a typed factual
outcome to the correct hosted mobile, and leave an absent/expired mapping as an uncorrelated Home1 diagnostic.
It must not broaden custody to team-plane or hosted-last-mile failures without a separate ruling. Until B278
lands, a mobile controller may remain timeout-correct but must not claim custody-aware request evidence.

## 15. Bounded queues and backpressure

Replacing the current single inbound slot is part of correctness, not an optional throughput improvement.
The target implementation needs:

- a bounded inbound operation queue or an explicit one-at-a-time admission contract;
- a bounded authenticated response transcript pool and seen-request table;
- bounded staging for an open multi-frame response;
- resource partitioning or a reserved authenticated-control floor so unauthenticated open/bootstrap pressure
  cannot consume every owner-recovery admission;
- observable counters for inbound refusal, open rate-limit refusal, transcript exhaustion, and response
  enqueue failure;
- no execution unless space for its mandatory terminal result and required deferred-action state has first
  been reserved;
- paced draining into the existing TX queue, with every enqueue result checked.

The controller implementation also needs:

- a bounded pending-request table binding local transport, target-administration full key, stable local
  `SOURCE_HASH`, selected local credential, target ACL slot, epoch, request ID, exact sealed request bytes,
  and retry state;
- a bounded session/discovery cache keyed by both full target and full selected-controller public keys;
- bounded authenticated response assembly, retained-BLE-result capacity, and compact response-ACK debt;
- refusal before transmission if required request/assembly/local-delivery state cannot be reserved;
- no eviction of a live request or unacknowledged BLE result to admit a newer one;
- refusal of key replacement/removal while any live record references that dedicated slot; and
- observable local counters/results for request-table pressure, assembly failure, unmatched response,
  authentication failure, local-result pressure, and radio enqueue failure.

Independent IDs permit several requests to be in flight, but the target may still dispatch only one at a
time. If a request was authenticated but cannot be admitted, it receives a terminal refusal when transport
capacity permits; unauthenticated malformed garbage remains a silent drop to avoid an oracle. A well-formed
open diagnostic may receive a clear refusal, but it never bypasses the shared bounds or rate limit.

Exact capacities are measurement outputs, not guesses. The implementation plan must measure controller and
acceptor ABIs separately, including the mobile client-only profile. A full retained BLE result causes loud
local backpressure; it is not an excuse to acknowledge the target early or discard an older result.

## 16. End-to-end flow

The controller node owns credential selection, target-slot discovery, session state, sealing, response
opening, and local delivery. The companion supplies and receives plaintext. Relays know only how to route the
opaque body; the relay column below may be absent for a direct neighbour.

```text
companion             controller node                 relay(s)        target static node
    | plaintext target, using=key2, command |              |                    |
    |-------------------------------------->|              |                    |
    |                resolve /mrtargets; reserve; derive selected key           |
    |                         BOOTSTRAP(slot=F, pub, source_hash, R0)           |
    |                                       |------------->|------------------->|
    |                                       |<-------------| BOOTSTRAP(slot=3, epoch)
    |                         authenticate; cache slot 3; derive session key    |
    |                         seal AUTH_EXECUTE(slot=3, source_hash, R1)        |
    |                                       |------------->|------------------->|
    |                                       |              |   reserve + dispatch once
    |                                       |<-------------| OUTPUT(R1, seq=0) |
    |                                       |<-------------| TERMINAL(R1, seq=1)
    |                         authenticate, decrypt, assemble, retain           |
    |<--------------------------------------| plaintext output + terminal       |
    | local ACK R1 (BLE; USB emission is acceptance)                           |
    |-------------------------------------->|                                   |
    |                         seal RESPONSE_ACK(slot=3, R1)                     |
    |                                       |------------->|------------------->|
```

A later `R2` is valid regardless of whether an R1 request, response, or ACK was lost. Selecting a dedicated
credential changes only the controller identity and discovered target ACL slot; routing is unchanged.

The open path skips bootstrap and sealing:

```text
companion --plaintext "remote <target> open -- status"--> controller
controller --clear OPEN_EXECUTE(slot=F, id=O1, "status")--> relay(s) --> target
controller <--clear OUTPUT...TERMINAL, explicitly unauthenticated<-- relay(s) <-- target
companion <--plaintext result labelled unauthenticated-- controller
```

## 17. Replacement, compatibility, and reuse

Reuse:

- `DATA_TYPE_REMOTE_CMD` and `DATA_TYPE_REMOTE_RESP` as the outer carrier types;
- the common command handlers and supplied-`Print` sink idiom;
- `Identity`/`identity_from_seed()`, identity conversion, ECDH, BLAKE2b KDF building blocks,
  XChaCha20-Poly1305, and the cryptographic RNG;
- one codec construction/opening path with controller/target direction as parameters.

New independent persistent authorities are `/mrmkeys` (controller credential seeds), `/mracl` (authorized
controller public keys/roles), `/mradmid` (the target's stable administration seed), and `/mrtargets`
(32 managed-target administration public keys plus routing metadata). None reuses `/mrpeers`.

Hard replacement requirements:

- remove the `rcmd` console command rather than preserve it as a second controller;
- remove `remote_exec`, its remote-only verb allowlist, and its binary response encoders;
- remove the old `unlock`, `lock`, and `password` administrator flow;
- remove `REMOTE_FLAG_SEALED`, the old sealed/clear body parser, sender counter, replay floor, and resync
  behaviour;
- remove the legacy `admin_pubkey`, `admin_counter_floor`, and `admin_provisioned` fields when the NV
  cleanup slice can do so atomically, replacing their authority with the separate `/mracl` record;
- add all four separate versioned management records and their physical-USB roots described above;
- provide a new local plaintext `remote` controller surface and structured plaintext BLE result/ACK
  contract; the product companion neither supplies already-encoded authenticated bodies nor receives raw
  encrypted responses; and
- do not rename a plaintext special-verb path and call it v2.

The v2 `OPEN_EXECUTE` `status` and `routes` path is a newly specified clear RPC envelope with sequence and terminal
semantics. It is not retention of the old clear `rcmd` parser. No decoder accepts legacy request/response
bodies, and there is no compatibility fallback from failed v2 authentication.

This changes the bodies of DATA types `0xA0`/`0xA1` (corrected 2026-08-29 — were 6/7 pre-§CUSTODY-A).
MeshRoute is not deployed, so legacy-body compatibility is not a deployment constraint. The new opcode and
exact-length decoder rejects legacy bodies and failed authentication never falls back to open. Owner ruling:
the unrelated global join/beacon `wire_version` does not change. Attribution still matters, so the remote-v2
codec/KAT slice precedes dispatcher semantics and pins the old/new fail-closed boundary without re-anchoring
all corpus streams merely for a global version number. Durable `docs/frames.md` and `docs/protocol.md` change
only with approved implementation. Once this design is ratified, the July challenge-response document is
historical rather than an alternative implementation path.

## 18. Security properties and limits

For operator/owner traffic, this proposal provides:

- controller authentication and command/response confidentiality/integrity;
- target binding through the stable `/mradmid`/`/mrtargets` ECDH/KDF inputs;
- explicit node-side selection of the normal identity or one dedicated management identity;
- exact full-public-key ACL discovery without a normal-command payload tax;
- independent request creation without a continuous command/response chain;
- at-most-once execution within a target session, including exact lost-response retries;
- cryptographic invalidation of old requests on reboot/session rollover;
- authenticated binding of each request/result to the controller node's stable carrier source;
- explicit ordering and termination for multi-DM output;
- no management private key or authenticated RPC body crossing the normal companion interface;
- local-delivery binding that prevents a BLE disconnect from becoming a false target acknowledgement; and
- bounded RAM behaviour with loud refusal/truncation.

The open diagnostic path intentionally provides none of controller authentication, target authentication,
confidentiality, integrity, or replay protection. It exposes only the owner-approved `status` and `routes`
outputs, and the controller/companion must not present those results as trusted.

The complete design does not provide:

- concealment of radio metadata such as route, packet size, or timing;
- a trustworthy pre-reboot outcome after volatile target or controller request state is lost;
- protection after a controller private key is compromised;
- forward secrecy: later compromise of either static administration private key exposes recorded sessions
  whose clear bootstrap epochs were captured;
- passphrase-encrypted at-rest storage or secure-element protection for controller seeds;
- protection from a locally authorized USB user or secured-BLE companion invoking an already-installed
  owner credential;
- per-device attribution or revocation among controller nodes that share one dedicated seed;
- unlimited retained output or unlimited unacknowledged operations;
- remote physical recovery from loss/corruption of all owner authority.

## 19. Proposed review/implementation slices (not yet authorized)

0. **Preparatory corrections, each separate from the feature:** complete B208's bounded help-topic split;
   fix B279's source-confirmed `regen` supplied-sink defect; then, as a third behaviour-neutral refactor,
   make the existing dispatcher/caller output path transport-neutral without adding remote context or
   policy. Do not combine any of these refactors/fixes with remote execution (C1).
1. **Feature-boundary scaffold:** replace the broad `MR_FEAT_REMOTE_MGMT` meaning with independently probed
   client/transport/accept boundaries. Prove mobile client-without-accept and static/gateway accept without
   adding v2 wire behaviour yet.
2. **Remote codec and KATs:** freeze the one-byte opcode/slot control, byte order, KDF/nonce/AAD domains,
   stable-source binding, invalid/all-zero ECDH refusal, authenticated/open codecs, per-carrier budget
   formulae, legacy-body rejection, corruption/nonce-separation controls, and the quantified request-ID
   collision bound. No global `wire_version` bump or corpus-wide version re-anchor.
3. **Target identity, ACL, and USB provisioning:** add `/mradmid`, the fixed ten-slot `/mracl` transaction,
   first-owner USB exchange, several-owner invariants, corrupt-state recovery, USB-only target-root rotation,
   role changes, and ordinary-`regen` preservation tests. No remote execution yet.
4. **Controller keyring and target book:** add the ten seed-only `/mrmkeys` slots and 32-row `/mrtargets`,
   seed-derived identity path, USB-only secret operations, public list/show, transactional persistence,
   in-use refusal, ABI measurements, messaging-peer independence, and `regen` behaviour. No on-air RPC yet.
5. **Target authenticated session/dedup:** full-key discovery/bootstrap, bounded seen table, exact retry,
   retained source identity, ACK release/debt, `session_full`/`session_busy`, shared-credential behaviour,
   safe/confirmed-force rollover, and reboot-unknown tests.
6. **Common dispatcher remote context/result:** on the already transport-neutral seam, add the remote
   authority context/result and complete the source-derived per-command/subcommand authority census. No
   remote request reaches it yet, and local serial/BLE behaviour remains unchanged.
7. **Target transcript and activation contract:** multi-DM sink, authenticated/open terminal responses,
   exhaustive enqueue handling, open/bootstrap rate limiting, authenticated-resource reservation,
   ingress/backpressure, local-only OTA-mode reporting, and deferred disruptive actions with mutation controls.
8. **Node-side controller and static carrier:** local plaintext command admission, explicit credential
   selection, `/mrtargets` resolution, discovery/session cache, sealing/opening, exact-request retry,
   bounded response assembly, scoped USB output, structured BLE output/local ACK, and every pressure/failure
   result. The companion receives plaintext only. Add the static-controller custody interaction from §14.1:
   exact-pair nonterminal evidence, never an RPC response/authentication result and never an automatic retry.
9. **Mobile-delegated controller carrier:** add the forced-global, source-bound mobile route and its smaller
   packer-derived command/response caps as a separately gated slice.
   Target acceptance remains static/gateway-only. B112's first-hop-ACK defect and [[B278]]'s exact
   `ctrH -> ctrM` custody-outcome translation must be resolved before this slice can claim truthful
   mobile-origin admission and custody-aware progress; neither blocks the preceding static-controller path.
10. **Legacy deletion, companion integration, and durable docs:** delete every item in §17's replacement
    list, remove the dead main-NV fields through an attributable NV slice, and update `docs/frames.md`,
    `docs/protocol.md`, help/manual, companion plaintext event handling, and historical cross-references.

### 19.1 Required controller-boundary controls

The slice plans must distribute, but not omit, these end-to-end and mutation controls:

- an omitted selector uses the controller node's current `self` identity; `using=key2` derives key2's seed
  and authenticates as key2 even when its local slot number differs from the target ACL slot;
- replacing selected-key derivation with `self`, a different key slot, or current mutable selector state
  turns the tests red; retry and response opening use the identity captured by the pending request;
- discovery matches the full presented public key, returns the actual target slot, and rejects absent,
  malformed, all-zero/low-order, wrong-tag, wrong-target, and stale-slot cases without open fallback;
- `/mradmid`, `/mracl`, `/mrmkeys`, and `/mrtargets` are independent versioned records with their exact
  ownership directions; `/mrtargets` has 32 design rows whose cost is pinned per ABI, and neither its
  contents nor capacity derive from `/mrpeers`;
- ten local seed slots persist independently of `/mrid` and every other record; seed import derives the
  expected public identity; corrupt/I/O-failed records and duplicate identities refuse loudly;
- ordinary target `regen` preserves `/mradmid` and `/mracl`; controller `regen` preserves `/mrmkeys` and
  `/mrtargets`, refuses while any source-bound RPC/result/ACK debt is live, leaves `keyN` authority usable,
  and explicitly reports that old `self` ACL grants cannot follow the regenerated public key;
- changing, omitting, or reconstructing the authenticated carrier `SOURCE_HASH` from later mutable state
  reddens the tests; a copied valid request with a different source cannot execute or redirect a replayed
  response;
- production-shaped custody notices about an `AUTH_EXECUTE` request and an `OUTPUT` response remain
  `custody_failure`: neither can satisfy response assembly, an RPC terminal or authentication failure; a
  static pending request correlates only the complete outer `{failed_dst,failed_ctr}` pair, retains its exact
  request bytes, reports target outcome unknown, and initiates zero TX solely because the notice arrived;
- the v2 direction handlers remain explicit and pre-tail: deleting/moving the `REMOTE_CMD` handler on an
  accept-enabled build or the `REMOTE_RESP` handler on a client-enabled build turns a real addressed RPC into
  `unsupported_internal` and makes the control RED; on an accept-disabled mobile an addressed request must
  produce exactly that bounded guard-drop with zero staging, dispatch or inbox text while a real response is
  still client-consumed;
- the mobile carrier test uses distinct `ctrM` and `ctrH`: a static-leg custody notice first terminates at the
  home, then reaches exactly the originating mobile only through the complete delegated-correlation mapping;
  wrong mobile/hash/destination/counter, a counter-only match and an expired mapping cannot move any controller
  pending request or fabricate a translated report;
- BLE can select an installed identity but cannot generate/import/export/remove one; neither `remote` nor
  `admin-key` is accepted recursively through target RPC, including with owner authority;
- first-owner provisioning and target-administration-identity generation/recovery/rotation are USB-only;
  merely adding a secured BLE caller without a physical-presence authority reddens the structural gate;
- each static, static-to-mobile, mobile-wrapper, and cross-layer carrier asks the common packer for its own
  RPC-body cap; mutations restoring universal 214/213 claims, allowing field drop to fit, or using
  `Plane::AUTO`/team plane turn the controls red;
- discovery's full controller public key appears only in its occasional 57-byte body;
- the normal companion interface accepts plaintext command components and emits scoped plaintext results;
  mutations that move key use, sealing, raw authenticated bodies, or response decryption into that interface
  fail structural checks;
- a BLE disconnect, missing/wrong/early local ACK, and full retained-result pool never produce a target
  `RESPONSE_ACK` or live-result eviction; reconnect re-offers the same complete result, and the correct local
  ACK causes exactly one target ACK;
- a USB console-stage drop likewise does not acknowledge the target; successful complete admission does;
- ACK loss creates bounded retryable debt; safe rollover refuses with `session_busy` while an unacknowledged
  transcript exists, force rollover requires explicit local confirmation, and mutations making either path
  silently abandon an outcome turn red;
- controller reboot or an unsolicited response cannot cause trial-decryption across stored keys or automatic
  replay of a mutating command;
- two controller nodes sharing one seed interoperate as one ACL principal, while two different keys remain
  independently revocable/session-scoped; and
- mobile builds originate and decrypt through client+transport while every target-accept entry point remains
  absent/inert. Static/gateway accept and ordinary intermediate transport remain intact.

Temporary coexistence may be needed between development slices, but it is not an accepted shipped state: v2
is incomplete until the legacy mechanism and compatibility parser are gone. Each implementation slice
receives its own source-derived gate plan. This review draft authorizes none of them. O4's broader BLE
key-export hardening remains complementary work; v2 does not depend on it because management secret
movement is USB-only.

## 20. Settled owner rulings and remaining design work

### 20.1 Owner rulings incorporated

The following product decisions are no longer open:

1. Remote administration must manage static nodes over multi-hop routes.
2. The target executes the same command implementation used by local serial/BLE and returns its normal
   textual output plus a mandatory transport terminal.
3. There are three authority levels: open, operator, and owner.
4. Only exact `status` and `routes` are open; their requests and responses are cleartext and
   unauthenticated.
5. Operator may perform ordinary administration, including creating, joining, leaving, and switching the
   static network.
6. Owner has local serial/BLE-equivalent authority for target-applicable commands, with only explicit
   physical trust-recovery exceptions. Security, identity, ACL, secret-key, factory-reset, fault-injection,
   and OTA commands require owner.
7. OTA is owner-only.
8. The target ACL has a fixed total of ten stable public-key entries, each operator or owner, and may contain
   several owners.
9. A target may authorize either the controller node's normal identity public key or a dedicated management
   public key. A dedicated seed may be shared between controller nodes, with all holders intentionally
   representing one ACL principal.
10. The controller node—not the companion—selects and uses the credential, constructs requests, and
    authenticates, reassembles, and decrypts responses. The companion exchanges local plaintext only;
    intermediate relays transport opaque bodies.
11. The controller has exactly ten persistent dedicated management slots. Each stores only a 32-byte seed;
    the full identity is derived through the existing canonical path. Secret generate/import/export/remove
    operations are physical USB-only; secured BLE may select an installed key and inspect public metadata.
12. Credential selection is explicit per request. Authenticated requests default to `self`; the open path
    must be explicitly selected and is never an authentication fallback. The `remote` wrapper and
    `admin-key` family are local controller surfaces, not commands a remote owner can recursively dispatch.
13. Authenticated bootstrap presents the selected full controller public key through the `BOOTSTRAP` opcode
    with sentinel slot F and returns the target's actual ACL slot. A companion/operator does not maintain
    target ACL-row mappings, and normal requests pay no discovery-key payload overhead.
14. Authenticated BLE results remain in bounded controller state until the companion explicitly acknowledges
    local receipt. USB emission counts as local acceptance. The controller sends the target response ACK only
    after that condition and never evicts a live result to admit a new one.
15. Mobile builds may originate/transport remote administration but never accept it. Client, transport, and
    accept are separate capabilities.
16. Commands that destroy state or their reply path return `scheduled` before activation. A lost ACK does
    not cancel an accepted action; lack of any response leaves the outcome unknown and forbids automatic
    non-idempotent retry.
17. Required controller and target request/transcript/deferred-action capacity is reserved before use. If it
    cannot be reserved, the request refuses without unsafe execution or false acknowledgement.
18. The current `rcmd`/counter/password/special-verb mechanism is removed, not preserved alongside v2.
19. Every managed target has a stable administration identity separate from ordinary `/mrid`; ordinary
    `regen` preserves it and the target ACL. Its seed is generated/recovered/rotated only through physical
    USB in v2 and is erased by factory reset.
20. Controller credentials, target ACL, target administration identity, and managed-target book use four
    independent stores: `/mrmkeys`, `/mracl`, `/mradmid`, and `/mrtargets`. `/mrtargets` has a design capacity
    of 32 and never reuses `/mrpeers`; an ABI-budget failure returns to the owner rather than silently reducing
    capacity.
21. The authenticated carrier's stable controller `SOURCE_HASH` is AEAD-bound and retained as the response
    identity. All remote administration uses the static/global plane, never `AUTO` or team-plane routing.
22. Safe rollover occurs only after every transcript is acknowledged. Abandoning retained outcomes requires
    an explicitly confirmed force rollover and reports them as unknown.
23. The transmitted control remains one byte with opcode and ACL-slot nibbles. Open and bootstrap are
    opcodes, not pseudo-ACL slots; reserved opcodes remain available.
24. The v2 remote body is self-discriminating and receives no global join/beacon `wire_version` bump.
25. RPC layout is carrier-independent but payload capacity is not. Every carrier publishes a packer-derived
    limit; 214/213 are not universal command/output contracts.
26. Remote `ota` only enters an implemented local Wi-Fi/BLE firmware-receiver mode. Firmware bytes never
    traverse MeshRoute, and an unsupported board refuses before scheduling.
27. First-owner provisioning is USB-only in v2. A future BLE path may reuse the internal operation only after
    a separately reviewed physical-presence mechanism exists.
28. The primary controller topology is a node reached locally over USB on a mobile build. Therefore the
    mobile-delegated carrier is product scope, not an optional adapter, and [[B278]]'s exact home-to-mobile
    custody-outcome translation is a prerequisite for claiming custody-aware mobile request progress.

### 20.2 Work still required before implementation approval

The settled architecture still needs source- or measurement-derived details rather than more product-policy
guesses:

- an exhaustive current command/subcommand authority table, including serial/BLE caller-only paths;
- exact local USB/BLE plaintext request, response-event, and BLE acknowledgement framing;
- measured controller pending/session/result capacities and target seen/transcript/ingress capacities on
  every essential client/acceptor ABI;
- the disruptive-action fallback deadline and open-diagnostic rate limit;
- the exact versioned `/mracl`, `/mrmkeys`, `/mradmid`, and 32-row `/mrtargets` binary layouts, validation,
  routing-metadata update rules, ABI costs, and main-NV cleanup sequence;
- final control values, byte order, KDF labels, nonce domains, and known-answer vectors;
- the quantified request-ID collision bound and final 64/96/128-bit choice;
- exact command-byte validation, including embedded NUL/CR/LF rejection and USB/BLE length behaviour;
- authenticated/open/bootstrap resource partitioning and rate limits that preserve owner recovery;
- the [[B278]] delegated-custody mapping lifetime and exact home-to-mobile outcome carrier required by the
  primary USB-attached mobile-controller topology, including the no-map and late-map verdicts;
- final compile-time feature names and dependency checks for client/transport/accept; and
- precise packer-derived carrier caps, stable-source binding, route-metadata refresh, and failure reporting
  for static and later mobile-delegated paths.

None of those remaining measurements changes the settled independent-request model, three-level authority,
node-owned key/crypto boundary, four-store trust split, legacy removal, or carrier-specific budget rule.
