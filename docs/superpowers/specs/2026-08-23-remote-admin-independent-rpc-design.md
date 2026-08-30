<!-- Author: OpenAI Codex; owner review draft -->
# Remote administration v2 — compact independent RPC (revised proposal)

**Status: DISCUSSION DRAFT — not ratified and not implementation authority.**

This revision incorporates the owner's decisions through 2026-08-23. It does not modify firmware behaviour
and remains subject to review. If ratified, it replaces the implementation direction in
`2026-07-26-remote-admin-challenge-response-design.md`; that document remains a historical decision record,
not a compatibility requirement. The completed v2 implementation must remove the current `rcmd` mechanism
rather than support both protocols indefinitely.

## 1. Decision in one paragraph

Remote administration has three authority levels. Exact `status` and `routes` requests are open cleartext
diagnostics. Operator and owner requests are authenticated, encrypted RPCs carrying the same command line
that the local serial/BLE dispatcher accepts. A companion owns the controller credentials, chooses either
its normal controller identity or a dedicated management identity, seals requests, and authenticates,
reassembles, and decrypts responses; the nearby MeshRoute node only transports opaque bodies and gains no
dedicated private-key store. Each authenticated request gets an independent random 64-bit `request_id`; it
does not consume or produce the token for the next request. The target feeds the line to the common
dispatcher, captures its normal textual output, returns that output in as many response DMs as needed, and
always ends with a terminal result. A one-byte `response_seq` orders those DMs and detects gaps. A separate,
fixed ten-slot target ACL holds full controller public keys with operator or owner roles and permits several
owners. A short boot/session handshake keeps the target epoch out of every normal authenticated command,
preserving 214 of the conservative 239 remote-body bytes for command text.

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
- Authenticate and encrypt operator/owner traffic end-to-end; relays transport opaque bytes and hold no
  administration key.
- Keep all controller private-key selection and response decoding in the companion for the first
  implementation.
- Make requests independent within a target session and safe to retry exactly.
- Keep wire overhead small and all target state bounded.
- Split the ability to originate/transport remote administration from the ability to accept it.

### 3.2 Non-goals for the first implementation

- Parallel command execution. The firmware may serialize dispatch in arrival order; request independence is
  a protocol property, not a promise of concurrent handlers.
- A certificate hierarchy, delegated certificates, per-command ACL bitmasks, roles beyond
  open/operator/owner, or time-based expiry.
- Remote acceptance by mobile nodes. A mobile may originate or carry an opaque request, but is not a target.
- Preserving the current password-derived shared administrator identity as the trust model.
- A firmware-resident store for dedicated management private keys, or standalone authenticated remote
  administration without a companion.
- Guaranteeing an unknown mutating command's outcome across target power loss without a durable operation
  journal.
- Inventing a remote-only formatter for every command.
- Defining the companion UI in this document.

## 4. Source-verified constraints (2026-08-23)

The following are code facts, not inherited assumptions from an older design:

- `protocol::lora_max_frame_bytes = 255`, the C++ DATA header is 8 bytes, and the hard inner payload cap is
  241 bytes (`lib/core/protocol_constants.h`).
- The supported normal-DM body ceiling is the deliberately conservative `dm_max_body_bytes = 239`.
- `DATA_TYPE_REMOTE_CMD = 0xA0` and `DATA_TYPE_REMOTE_RESP = 0xA1` already carry remote request/response bodies
  (⛔ corrected 2026-08-29: ordinals 6/7 were RETIRED by the §CUSTODY-A namespace transition — the values now sit
  in the internal range's administration/security block `0xA0..`. Reusability is unchanged; no third DATA type is
  needed. Both are protocol-internal (`0x80..0xBF`), so `data_type_traits()` reports them
  `internal=true, generic_send_lifecycle=false` — the RPC's own response/timeout contract is the only outcome they
  carry, and no generic `send_acked`/`send_failed` may be raised for them.)
  (`lib/core/frame_codec.h`, `lib/core/node_mac.cpp`). They are reusable; no third DATA type is needed.
- A current sealed request body costs 35 bytes before command text:
  `[sealed_flag 1][rand8 8][nonce_ctr 2][node_hash 4][replay_counter 4][tag 16]`. Under the supported
  239-byte ceiling, at most 204 bytes remain for a command.
- The current target `remote_exec()` has its own small verb set and binary response encoders; it does not call
  the shared `dispatch()` (`src/firmware_remote.cpp`). Unknown or oversized results can therefore disappear
  without a response.
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
- The current ESP OTA path accepts a raw firmware image and the project has no MeshRoute application-signing
  trust anchor (`src/device_ota.cpp`). This is why OTA cannot be delegated below owner.
- The node currently exposes one overwriteable inbound remote slot, so a future implementation must not
  claim pipelining until that ingress boundary is made bounded and explicit.

## 5. Architecture

There are four separate responsibilities:

1. **Controller endpoint** — the companion/host holds and selects the controller private key, creates sealed
   requests, reassembles and opens responses, and decides whether retry is safe. It may select its normal
   controller identity or a named dedicated management identity.
2. **Carrier adapter** — puts opaque request bytes into the existing typed-DM path and returns opaque response
   bytes plus routing/source metadata. Static DM and mobile delegation may use different carrier adapters
   without changing the RPC body. The carrier neither receives nor persists a dedicated management private
   key.
3. **Target authenticator/session** — selects the authorized public key, authenticates the request, performs
   replay/dedup checks, and creates encrypted response frames.
4. **Common dispatcher** — receives the original command line, writes normal command output to a `Print`
   sink, and returns a small transport-neutral completion classification.

The target necessarily retains its own ordinary MeshRoute identity private key, and the ACL retains only
controller public keys. Neither is a controller-side dedicated-key ring. The return address belongs to the
carrier, not to the sealed command body. Static-node ID, source-hash, or a mobile delegation route must
therefore be reused from the surrounding DATA path; adding a return address to every command body would
waste payload and couple authentication to one routing plane.

## 6. Trust and provisioning

### 6.1 Controller credentials and their storage boundary

The target treats both supported authenticated-controller forms identically:

- a companion's normal controller identity keypair; or
- a MeshRoute-compatible keypair generated explicitly for remote management.

In both cases the target stores the full public key in one ACL slot. It does not store a credential-type
flag. The companion selects the private credential for each request, conceptually `self` for its default
controller identity or a local label for a dedicated management identity. Here `self` means the
companion's default controller credential; it does not implicitly borrow the attached carrier node's
firmware-held identity. The carrier identity and the selected administration identity may be different.

There is no dedicated management private-key store in MeshRoute firmware in this design. The companion owns
the private key, applies it while constructing the RPC envelope, and sends only the already-encoded opaque
request to the carrier node. It also performs response sequence assembly, authentication, decryption, and
presentation. The carrier firmware must not add private-key import/export commands, persist a transient
management key, or decode authenticated response text on the companion's behalf.

A dedicated management keypair may be copied to another companion. All copies are then one ACL principal:
they have the same role and slot, cannot be audited or revoked separately, share that slot's target session,
and are all revoked by removing that one entry. A rollover initiated by one copy changes the epoch for all
copies; another companion recovers by bootstrapping the current epoch. Administrators who need independent
revocation or attribution should provision separate ACL keys instead.

Consequently, a standalone MeshRoute console cannot originate operator/owner RPCs in the first
implementation. Adding such support later would require a separately reviewed secret-keyring and recovery
design. This restriction does not affect multi-hop routing: a companion-created opaque request may still
travel through any supported carrier path.

### 6.2 Initial physical exchange

Initial ownership is established locally over serial, or over an explicitly physical/provisioning BLE
session:

1. the companion obtains and pins the target's full identity public key;
2. the target receives and atomically persists the selected controller public key as an owner;
3. the target assigns that key one stable ACL slot in the range 0..9 and returns that slot to the
   companion;
4. both sides show fingerprints so the operator can verify the exchange.

The controller private key never enters the target. A passphrase or platform keystore may protect it on the
companion, but a password is not sent as the remote authenticator.

### 6.3 Fixed flat ACL and persistence

The target has exactly ten stable credential slots. Each slot contains either no entry or:

- one full 32-byte controller public key; and
- one role: `operator` or `owner`.

Slot numbers are stable wire handles, not security identities. Empty entries are not compacted, an ACL-full
condition refuses loudly, and adding a duplicate public key is rejected. The target need not persist display
labels; the companion owns human-readable key names and the target reports slot, role, and a fingerprint.

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

### 6.4 ACL-management invariants

- Several owner entries are allowed.
- Only physical provisioning may create the first owner.
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

### 6.5 Why the target stores public keys

A symmetric management secret would not reduce the per-message AEAD tag and would make the target a holder
of controller credentials. Pinning public keys lets the target authorize only public material and reuses
MeshRoute's existing identity conversion, ECDH, BLAKE2b, and XChaCha20-Poly1305 primitives. A deliberately
shared management private key remains possible, but sharing occurs only between companion keystores and has
the one-principal consequences described in §6.1.

## 7. Compact authenticated session

### 7.1 Base and session keys

For an authorized slot, both endpoints derive a target-specific base key from static ECDH. The KDF is domain
separated for remote administration and binds the full controller and target public keys. On the controller
side this derivation belongs to the companion; the carrier node sees only encoded RPC bodies.

After target boot, an authenticated bootstrap exchange returns a fresh random 64-bit `admin_epoch` for the
selected key slot. Both ends derive:

```text
session_key = KDF(base_key, admin_epoch, "MeshRoute remote-admin v2 session")
```

Normal requests use `session_key`; `admin_epoch` is not carried in them. That saves eight bytes on every
command. Bootstrap messages use `base_key`, so a companion can recover after reboot without knowing the new
epoch.

Each authorized slot has its own epoch/session. The epoch is stable for that administration session, not
advanced by commands or responses. Consequently, losing command `A` or any response for `A` has no effect
on command `B`, and rolling one credential session does not invalidate any other ACL slot.

### 7.2 Bootstrap

The companion sends an authenticated `BOOTSTRAP` request with a fresh `request_id` and no plaintext body.
The target returns its current epoch in clear but authenticated under the base key and bound to that request
ID. The epoch is a nonce/session generation, not a secret. Keeping it clear lets it participate in the
bootstrap-response nonce, so replaying the same bootstrap request after a target reboot cannot make the
target encrypt different epoch plaintext under a reused base-key nonce. Replaying an old bootstrap response
for another ID or epoch fails authentication.

Bootstrap does not dispatch a command and never changes the epoch. It is needed after first provisioning,
target reboot, or a lost rollover response; it is not a per-command challenge round trip. Because it is
read-only, replaying a captured bootstrap request cannot rotate or disrupt a later session.

### 7.3 Explicit bounded-session rollover

When an execute request receives `session_full`, the companion sends `SESSION_ROLLOVER` under the current
session key with a fresh request ID and no plaintext. Execute capacity reserves admission for this control
message. The target accepts it only for the same key slot/current session and only when no operation is
executing, rotates that slot to a fresh random epoch, and returns the new epoch in the bootstrap-response
layout under the base key, bound to the rollover request ID.

After rotation, the old rollover request cannot authenticate under the new session key. If its response is
lost, ordinary read-only bootstrap discovers the already-current epoch. If several companions deliberately
share one management credential, they also share this slot session: rollover by one invalidates the others'
cached epoch, and they bootstrap again. Any unacknowledged operation in the old session becomes
`outcome_unknown`; key sharing does not recreate a continuous per-command counter or prevent later
independent requests.

## 8. Proposed wire bodies

All multi-byte integers below use one declared byte order in the codec; little-endian is proposed to match
the surrounding C++ wire idiom. Exact codec constants belong in one shared implementation path (U2).

### 8.1 Control byte and slot values

One byte carries three fields:

```text
bits 7..6  protocol version (`01` for this proposal)
bits 5..4  message kind within REMOTE_CMD or REMOTE_RESP
bits 3..0  authorization slot
```

Slot values 0..9 select the fixed authenticated ACL entries, 10..14 are reserved and rejected, and 15 means
the explicitly open cleartext diagnostic path. The decoder chooses one unambiguous body layout from this
value; an invalid authenticated request must never fall back to the open decoder.

For `REMOTE_CMD`, proposed kinds are `EXECUTE`, `BOOTSTRAP`, `RESPONSE_ACK`, and
`SESSION_ROLLOVER`. Open slot 15 permits only `EXECUTE`. For `REMOTE_RESP`, proposed kinds are `OUTPUT`,
`TERMINAL`, `BOOTSTRAP`, and `PROTOCOL_ERROR`. The outer DATA type already distinguishes request from
response, so the control byte does not repeat direction.

### 8.2 Authenticated execute request

```text
REMOTE_CMD body, slot 0..9:
    ctl             1
    request_id      8   random u64, clear but authenticated
    ciphertext      N   exact command-line bytes; no terminator and no length field
    tag             16  XChaCha20-Poly1305 tag
```

The DATA body length supplies `N`; a separate command length would be redundant. The maximum supported
authenticated command text is therefore `239 - 1 - 8 - 16 = 214` bytes.

`request_id` identifies the logical operation and supplies the unique seed for nonce derivation. It
replaces both the current replay counter and the separate random/nonce-counter wrapper.

### 8.3 Open execute request

```text
REMOTE_CMD body, slot 15:
    ctl             1   EXECUTE + open slot
    request_id      8   random correlation ID; not an authenticator
    command         N   cleartext exact command-line bytes
```

The open request can carry `239 - 1 - 8 = 230` command bytes, although policy accepts only the exact
`status` and `routes` command lines. It has no confidentiality, integrity, controller authentication, or
target authentication. The ID only correlates response chunks. These two commands are read-only, so the open
path does not need authenticated at-most-once execution or a retained response ACK. It remains subject to
bounded admission and rate limiting so replay cannot create unbounded response work. An authorized
companion may instead send either diagnostic through slot 0..9 when it needs an authenticated, confidential
result.

### 8.4 Authenticated control request

```text
REMOTE_CMD body, slot 0..9:
    ctl             1   BOOTSTRAP, RESPONSE_ACK, or SESSION_ROLLOVER
    request_id      8
    tag             16  authenticates the header; no ciphertext
```

`BOOTSTRAP` uses the base key and a fresh request ID. `SESSION_ROLLOVER` uses the current session key and a
fresh request ID. `RESPONSE_ACK` uses the current session key and the acknowledged execute request's ID;
its distinct message-kind nonce domain prevents collision with that execute request.

### 8.5 Bootstrap response

The base-key response has its own fixed layout:

```text
REMOTE_RESP body, BOOTSTRAP kind:
    ctl             1
    request_id      8
    admin_epoch     8   clear, random, authenticated; also a nonce input
    tag             16  authenticates the complete header; no ciphertext
```

The epoch does not need confidentiality: without the controller private key it cannot produce the base or
session key. Its clear presence makes the response nonce unique when boot/session rollover changes the
answer to a replayed bootstrap request. This 33-byte bootstrap cost is occasional and consumes no regular
command payload.

### 8.6 Authenticated session response frame

```text
REMOTE_RESP body, slot 0..9:
    ctl             1   OUTPUT, TERMINAL, or authenticated protocol error
    request_id      8
    response_seq    1   starts at 0 and increments across every frame for this response
    ciphertext      N
    tag             16
```

An authenticated output frame can carry `239 - 1 - 8 - 1 - 16 = 213` bytes of ordinary command output.

### 8.7 Open response frame

```text
REMOTE_RESP body, slot 15:
    ctl             1   OUTPUT, TERMINAL, or clear protocol error
    request_id      8
    response_seq    1
    plaintext       N
```

An open output frame can carry `239 - 1 - 8 - 1 = 229` bytes. Anyone may read, alter, inject, or replay it;
the companion must label open results as unauthenticated. If an open transcript is incomplete, the client
starts a new read-only request with a new ID rather than relying on authenticated transcript replay.

### 8.8 Terminal frame

The terminal frame is the next contiguous `response_seq` after the last output frame. Its authenticated
ciphertext or open plaintext starts with one compact result code. Proposed meanings are:

- `completed` — a matching handler returned; its normal text contains any command-specific warning/error;
- `scheduled` — a disruptive action was accepted and deferred until response handling permits it;
- `unknown_command` — no common handler matched;
- `refused` — the authority/transport is not allowed to run this command;
- `output_truncated` — the handler returned, but the bounded transcript could not retain all output;
- `internal_error` — execution or response staging failed before a truthful normal result existed;
- `session_full` — no authenticated execution occurred; explicitly roll over the bounded session and retry
  is safe.

Optional short detail bytes may follow, but ordinary handler output must not be duplicated into a special
terminal encoding. The `scheduled` result is the exception that must carry its bounded activation delay.
A successful command that prints nothing returns a terminal frame at sequence zero.

`response_seq` is one byte deliberately: at most 256 frames can belong to one response, with no more than
255 output frames followed by the required terminal frame. That is over 54 KiB of authenticated wire
capacity, already far beyond the response transcript that this firmware should retain in RAM. A two-byte
sequence would cost one byte in every response DM without increasing a usable target limit.

### 8.9 Authenticated response acknowledgement

After receiving one contiguous authenticated series from sequence zero through `TERMINAL`, the companion
sends a sealed `RESPONSE_ACK` carrying the same `request_id` and no plaintext. Its body is therefore 25
bytes, like an empty authenticated execute request. The ACK lets the target release the potentially large
transcript while retaining a compact already-executed record for replay safety.

An ACK is not in the critical path for the next command. Losing it consumes bounded cache for longer, but
does not desynchronize requests. Open responses do not use this ACK contract.

### 8.10 Byte budget

| Body | Fixed overhead | Available application bytes |
|---|---:|---:|
| Supported normal DM body | — | 239 |
| Current sealed command | 35 | 204 command bytes |
| Proposed authenticated execute request | 25 | 214 command bytes |
| Proposed open execute request | 9 | 230 command bytes |
| Proposed authenticated output response | 26 | 213 output bytes |
| Proposed open output response | 10 | 229 output bytes |
| Proposed authenticated bootstrap/ACK/rollover request | 25 | 0 |
| Proposed authenticated bootstrap response | 33 | 0 |

The authenticated proposal saves ten bytes relative to the implemented sealed-command wrapper while adding
an independent operation identity. The 16-byte authentication tag and 8-byte request identity are the two
load-bearing authenticated costs; the one-byte control is the remaining routing/authorization metadata
inside the RPC body. Open diagnostics omit the tag by explicit policy and therefore provide no security
claim.

## 9. Nonce and request-ID rules

For authenticated traffic, a 24-byte XChaCha nonce is derived, not transmitted, from the selected
base/session key plus a domain containing at least message direction, message kind, `request_id`, and
`response_seq` (zero for requests). The base-key bootstrap-response domain additionally includes the clear
`admin_epoch`, so the response to a replayed request after epoch rotation uses a different nonce. The clear
header is authenticated as AEAD associated data. Full target/controller identity is already bound through
the key derivation; mutable relay headers are not AEAD inputs.

Open slot-15 bodies have no nonce or authentication tag. Their random ID is only a correlation value and
must never be described as replay protection or proof of origin.

Hard authenticated invariants:

- A companion must never use one `request_id` for different plaintext under the same credential/session key.
- A retry retransmits the exact sealed request bytes; it does not reseal changed text under the same ID.
- Response sequence values are never reused for different plaintext within one request transcript.
- Bootstrap, execute, ACK, rollover, output, and terminal domains cannot collide even when IDs are equal.
- RNG failure refuses authenticated request creation loudly.
- Two companions sharing one private credential are one logical principal. A random-ID collision is detected
  by the target's request fingerprint check; there is no shared mutable command counter to synchronize.

A random 64-bit ID is the compact choice. The target session is deliberately bounded and rolls over long
before birthday-scale request counts, even when one credential is deliberately shared, and the target
detects accidental ID reuse. A 128-bit ID would consume eight more command bytes without providing a
practical benefit at remote-administration volume; a shorter ID would give away too much collision margin.

## 10. Execution, deduplication, and replay

The target keeps a bounded authenticated session table per ACL slot, keyed by `request_id` (the physical
storage may be one shared bounded pool). It also retains the original authenticated request tag as the exact
128-bit request fingerprint.

For an authenticated `EXECUTE`:

1. **ID absent and capacity available:** reserve the seen record and enough response/transcript capacity for
   a truthful terminal before dispatch, dispatch exactly once, retain the resulting transcript and terminal,
   then send it.
2. **ID present and request tag identical, transcript unacknowledged:** resend the original transcript from
   sequence zero; never dispatch again. The companion discards duplicate sequence values.
3. **ID present and request tag differs:** reject as request-ID reuse after authentication; never dispatch
   either plaintext under an ambiguous ID.
4. **ID present and response already acknowledged:** never execute again and never create different response
   plaintext in the old nonce space.
5. **This ACL slot's session table is full:** return `session_full` without executing. A subsequent
   authenticated `SESSION_ROLLOVER` rotates only this slot to a fresh random epoch and clears its old
   session when no operation is executing. The companion may then seal the not-executed command under the
   new session.

Session records are not silently evicted while their session key remains valid. This prevents a captured old
request from becoming executable again merely because a small ring wrapped. Per-slot rollover invalidates
every old request for that slot cryptographically and is occasional bounded maintenance, not a
command-to-command token chain.

When several companions share a credential, they share that slot's table and epoch. Rollover by one makes
every old unacknowledged operation for that credential `outcome_unknown`; the other companions bootstrap
again and continue. Separate keys are required when one controller must not affect another controller's
session retention.

The response-transcript pool may be smaller than the seen-request table, but it must not silently evict an
unacknowledged transcript and then execute its request again. Under pressure the target refuses a new command
before execution. Exact capacities are deliberately not guessed in this proposal; implementation planning
must measure the real board ABI, current RAM headroom, and command-output census.

Open slot-15 requests do not enter the authenticated seen table. Only exact read-only `status` and `routes`
are accepted, so a duplicate may safely produce a fresh snapshot. The target still reserves bounded output
capacity before dispatch and rate-limits the open path. An incomplete open response is retried as a new
read-only operation with a new correlation ID.

## 11. Multi-DM output contract

The remote sink is another bounded `Print` implementation following the existing sink seam. It captures the
same bytes the selected command handler writes locally and divides them into 213-byte authenticated output
payloads or 229-byte open output payloads. Chunk boundaries have no semantic meaning; the companion
concatenates plaintext in `response_seq` order.

For an authenticated response, the companion:

- accepts frames only for the expected target, ACL slot, session, and request ID;
- buffers or displays only a contiguous prefix;
- ignores exact duplicate sequence values;
- detects a gap and resends the exact sealed execute request to ask for transcript replay;
- considers the RPC complete only after a valid terminal frame follows all preceding sequence values;
- sends `RESPONSE_ACK` only after that complete contiguous transcript is present.

For an open response, the companion uses target/routing metadata, request ID, and sequence only to assemble a
best-effort transcript. None proves origin or integrity. A gap causes a new read-only request with a new ID,
and the displayed result remains explicitly marked unauthenticated.

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
| **Owner** | Everything available to operator plus commands that manage security, identity, the ACL, private/secret key material, factory reset, fault injection, and OTA. For target-applicable commands, remote owner is intended to have the same authority as a locally connected serial/BLE administrator, except for the narrow physical recovery operations below. |

Policy is attached at command/subcommand granularity. For example, a harmless team status subcommand need not
inherit the owner authority required for private-key operations such as `team exportkey`. Before
implementation, an exhaustive census of the current source handler map and the serial/BLE caller-only paths
must assign every command and subcommand exactly one minimum authority; anything missing refuses closed.

OTA is owner-only. The current ESP path accepts a raw firmware image and the current project has no
MeshRoute application-signing trust anchor; granting OTA to an operator would let that operator replace the
code that enforces the ACL. A future signed-update design may revisit this policy, but this protocol does not
assume it.

Physical-only authority is deliberately narrow:

- installing the first owner when no valid ACL exists;
- recovering from a corrupt/unreadable ACL or from loss of every owner; and
- any future operation whose security proof explicitly requires physical presence.

These exceptions are recovery roots, not a general local-only escape from owner parity. The old
`password`, `unlock`, and `lock` flow is not part of v2.

ACL-management commands are ordinary common-dispatch commands gated to `remote_owner` or approved local
physical authority. Operators cannot authorize another key.

## 13. Disruptive commands and reply-path honesty

A command that reboots, halts, erases state, changes target identity, starts OTA/fault injection, or removes
the current RF/return path cannot activate its disruptive side effect inside the handler before a truthful
acceptance result has entered the reply path. Examples include `reboot`, `prep-restart`,
`factory_reset`, `regen`, OTA, destructive crash tests, and network/radio changes that retune or detach
the target.

The target first authenticates and authorizes the command, validates any existing explicit confirmation
token, reserves the response/transcript and deferred-action record, and performs only non-disruptive
preparation needed to make later activation reliable. It then returns `scheduled` with the bounded
activation delay. Erase, reboot, live retune/detach, and other reply-path-breaking effects remain deferred.
The action is activated only after:

- the `scheduled` terminal has been retained and accepted into the response transmission path; and
- either the companion acknowledges the complete response or a bounded fallback deadline expires.

The fallback means a lost ACK cannot block an accepted recovery action forever. Receiving `scheduled` tells
the companion that the target accepted the action and will execute it by the stated deadline; loss of the
subsequent ACK does not cancel that promise. Receiving no response is different: the companion cannot know
whether the request or response was lost, must report the outcome as unknown, and must not automatically
retry a non-idempotent command.

Existing command-level safeguards such as a literal `confirm` token remain in force. The transport-level
`scheduled` result is an additional pre-activation confirmation, not a replacement for deliberate user
confirmation.

Every boot generates a different `admin_epoch`. An old authenticated request therefore fails under the new
session key. After timeout, the companion bootstraps and compares epochs:

- same epoch: resend the exact request; the target replays its transcript without re-execution;
- changed epoch and no terminal was received: outcome is **unknown** for a mutating command; do not retry it
  automatically;
- changed epoch for a source-verified read-only/idempotent command: a client may offer or perform a new RPC
  according to explicit command policy.

A RAM-only design cannot honestly promise the old outcome after power loss. A durable operation journal could
add that guarantee later, but it is a separate NV/wear design and not hidden inside v2.

## 14. Companion, profile, and carrier split

Two firmware capabilities are required instead of one broad `REMOTE_MGMT` meaning:

- **transport** — accept an already-encoded opaque request from a companion, carry request/response bodies,
  and return raw response bodies plus source/routing metadata;
- **accept** — decode the target-side body, authenticate when required, enforce authority, and dispatch a
  remote command; enabled only for static nodes and gateways.

The companion, not carrier firmware, originates authenticated RPC envelopes and opens their responses.
Carrier ingress therefore takes target/routing metadata plus an opaque v2 body, not plaintext `rcmd`
arguments or a private management key. Its return path exposes complete raw bodies; it does not translate
them into legacy binary results or console text.

Mobile firmware must never become remotely administrable merely because it can transport this traffic.
Conversely, a companion attached to a mobile should be able to use the mobile's existing delegated routing
path as a carrier once that adapter is designed. No administration secret is installed in an intermediate
mobile, static home node, or gateway merely to forward bytes.

The compact RPC body is carrier-independent. A carrier adapter must preserve the stable logical source or
return route outside that body and must expose enqueue failure rather than silently losing an accepted
response. Open bodies are cleartext, but the carrier still treats them as opaque protocol payloads.

## 15. Bounded queues and backpressure

Replacing the current single inbound slot is part of correctness, not an optional throughput improvement.
The implementation needs:

- a bounded inbound operation queue or an explicit one-at-a-time admission contract;
- a bounded authenticated response transcript pool and seen-request table;
- bounded staging for an open multi-frame response;
- observable counters for inbound refusal, open rate-limit refusal, transcript exhaustion, and response
  enqueue failure;
- no execution unless space for its mandatory terminal result and required deferred-action state has first
  been reserved;
- paced draining into the existing TX queue, with every enqueue result checked.

Independent IDs permit several requests to be in flight, but the target may still dispatch only one at a
time. If a request was authenticated but cannot be admitted, it receives a terminal refusal when transport
capacity permits; unauthenticated malformed garbage remains a silent drop to avoid an oracle. A well-formed
open diagnostic may receive a clear refusal, but it never bypasses the shared bounds or rate limit.

## 16. End-to-end flow

The companion stores the mapping from target identity and chosen local credential to the target's ACL slot.
The carrier knows only how to route the opaque body.

```text
companion/controller             carrier node                    target static node
    | choose credential + slot        |                                  |
    | seal BOOTSTRAP(id=R0)           |                                  |
    |-- target + opaque body -------->|-- routed REMOTE_CMD ------------>|
    |                                 |<-- routed REMOTE_RESP ------------|
    |<-- source + opaque response ----|                                  |
    | authenticate epoch; derive session key                             |
    | seal EXECUTE(id=R1, "routes")   |                                  |
    |-- target + opaque body -------->|-- routed REMOTE_CMD ------------>|
    |                                 |                    reserve + dispatch once
    |                                 |<-- OUTPUT(R1, seq=0) -------------|
    |<-- raw response ----------------|                                  |
    |                                 |<-- TERMINAL(R1, seq=1) -----------|
    |<-- raw response ----------------|                                  |
    | authenticate, decrypt, assemble |                                  |
    | seal RESPONSE_ACK(id=R1)        |                                  |
    |-- target + opaque body -------->|-- routed REMOTE_CMD ------------>|
```

A later `R2` is valid regardless of whether an R1 request, response, or ACK was lost. Selecting a dedicated
credential changes only the companion key and ACL slot; routing is unchanged.

The open path skips bootstrap and sealing:

```text
companion -- OPEN EXECUTE(slot=15, id=O1, "status") --> carrier --> target
companion <-- clear OUTPUT...TERMINAL, explicitly unauthenticated <-- carrier <-- target
```

## 17. Replacement, compatibility, and reuse

Reuse:

- `DATA_TYPE_REMOTE_CMD` and `DATA_TYPE_REMOTE_RESP` as the outer carrier types;
- the common command handlers and supplied-`Print` sink idiom;
- identity conversion, ECDH, BLAKE2b KDF building blocks, XChaCha20-Poly1305, and the cryptographic RNG;
- one codec construction/opening path with controller/target direction as parameters.

Hard replacement requirements:

- remove the `rcmd` console command rather than preserve it as a second controller;
- remove `remote_exec`, its remote-only verb allowlist, and its binary response encoders;
- remove the old `unlock`, `lock`, and `password` administrator flow;
- remove `REMOTE_FLAG_SEALED`, the old sealed/clear body parser, sender counter, replay floor, and resync
  behaviour;
- remove the legacy `admin_pubkey`, `admin_counter_floor`, and `admin_provisioned` fields when the NV
  cleanup slice can do so atomically, replacing their authority with the separate `/mracl` record;
- provide a new companion-to-carrier surface for already-encoded bodies and raw responses; do not rename a
  plaintext special-verb path and call it v2.

The v2 slot-15 `status` and `routes` path is a newly specified clear RPC envelope with sequence and terminal
semantics. It is not retention of the old clear `rcmd` parser. No decoder accepts legacy request/response
bodies, and there is no compatibility fallback from failed v2 authentication.

This changes the bodies of DATA types `0xA0`/`0xA1` (corrected 2026-08-29 — were 6/7 pre-§CUSTODY-A). MeshRoute is not deployed, so compatibility is not a deployment
constraint. Attribution still matters: the v2 codec/global-wire-version change receives its own slice and
anchor work before dispatcher semantics (C4/M3). Durable `docs/frames.md` and `docs/protocol.md` change
only with approved implementation. Once this design is ratified, the July challenge-response document is
historical rather than an alternative implementation path.

## 18. Security properties and limits

For operator/owner traffic, this proposal provides:

- controller authentication and command/response confidentiality/integrity;
- target binding through target-specific ECDH/KDF inputs;
- independent request creation without a continuous command/response chain;
- at-most-once execution within a target session, including exact lost-response retries;
- cryptographic invalidation of old requests on reboot/session rollover;
- explicit ordering and termination for multi-DM output;
- bounded RAM behaviour with loud refusal/truncation.

The open diagnostic path intentionally provides none of controller authentication, target authentication,
confidentiality, integrity, or replay protection. It exposes only the owner-approved `status` and `routes`
outputs, and the companion must not present those results as trusted.

The complete design does not provide:

- concealment of radio metadata such as route, packet size, or timing;
- a trustworthy pre-reboot outcome after volatile state is lost;
- protection after a controller private key is compromised;
- per-device attribution or revocation among companions that share one dedicated private key;
- unlimited retained output or unlimited unacknowledged operations;
- remote physical recovery from loss/corruption of all owner authority;
- a firmware-resident controller keyring or standalone authenticated-controller console.

## 19. Proposed review/implementation slices (not yet authorized)

1. **Protocol version and KATs:** freeze control values, byte order, KDF/nonce domains, authenticated and open
   codecs, byte-budget assertions, corruption/nonce-separation controls, and the isolated global
   `wire_version`/corpus re-anchor required by C4.
2. **ACL record and physical provisioning:** add the fixed ten-slot `/mracl` transaction, several-owner
   invariants, corrupt-state recovery, role changes, and stable-slot tests. No remote execution yet.
3. **Target authenticated session/dedup:** bootstrap, bounded seen table, exact retry, ACK release,
   `session_full`, shared-credential behaviour, rollover, and reboot-unknown tests.
4. **Common dispatcher context/result:** add transport-neutral context/result, consolidate applicable
   caller-only paths without changing local serial/BLE behaviour, route all output through the supplied sink,
   and complete the source-derived per-command/subcommand authority census.
5. **Target transcript and activation contract:** multi-DM sink, authenticated/open terminal responses,
   exhaustive enqueue handling, open rate limiting, ingress/backpressure, and deferred disruptive actions
   with mutation controls.
6. **Companion controller and static carrier:** companion credential selection, target/slot mapping,
   bootstrap/session logic, sealing/opening, response assembly, retry UI state, and the firmware opaque-body
   carrier API. No private management key is persisted in carrier firmware.
7. **Mobile-delegated carrier:** add the plane-aware opaque mobile route as a separately gated slice. Target
   acceptance remains static/gateway-only.
8. **Legacy deletion and durable docs:** delete every item in §17's replacement list, remove the dead main-NV
   fields through an attributable NV slice, and update `docs/frames.md`, `docs/protocol.md`, help/manual,
   and historical cross-references.

Temporary coexistence may be needed between development slices, but it is not an accepted shipped state: v2
is incomplete until the legacy mechanism and compatibility parser are gone. Each implementation slice
receives its own source-derived gate plan. This discussion draft authorizes none of them.

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
9. A target may authorize either a companion-held normal controller identity public key or a dedicated
   management public key. A dedicated private key may be shared, with all holders intentionally representing
   one ACL principal. The attached carrier node's identity is not implicitly used.
10. No dedicated management private key is stored by controller/carrier firmware in the first
    implementation. The companion selects and uses the credential, constructs requests, and authenticates,
    reassembles, and decrypts responses; the MeshRoute carrier transports opaque bodies.
11. Commands that destroy state or their reply path return `scheduled` before activation. A lost ACK does
    not cancel an accepted action; lack of any response leaves the outcome unknown and forbids automatic
    non-idempotent retry.
12. Required terminal/transcript/deferred-action capacity is reserved before execution. If it cannot be
    reserved, the target refuses without executing.
13. The current `rcmd`/counter/password/special-verb mechanism is removed, not preserved alongside v2.

### 20.2 Work still required before implementation approval

The settled architecture still needs source- or measurement-derived details rather than more product-policy
guesses:

- an exhaustive current command/subcommand authority table, including serial/BLE caller-only paths;
- exact companion opaque-carrier framing, target/credential/slot mapping storage, and keystore UX;
- measured seen-table, transcript, ingress, and open-response capacities on every essential board;
- the disruptive-action fallback deadline and open-diagnostic rate limit;
- the exact versioned `/mracl` binary layout, validation, and main-NV cleanup sequence;
- final control values, byte order, KDF labels, nonce domains, and known-answer vectors;
- precise carrier return-route binding and failure reporting for static and later mobile-delegated paths.

None of those remaining measurements changes the settled independent-request model, three-level authority,
companion key boundary, legacy removal, or payload budgets above.
