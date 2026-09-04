<!-- Author: OpenAI Codex; owner review draft -->
# Remote administration v2 — compact independent RPC (revised proposal)

**Status: DESIGN PASS 2026-09-04 — IMPLEMENTATION AUTHORITY; round-1 findings A1-A8/B1-B6/C1-C14,
round-2 findings H1-H3/F1-F11, and owner rulings R-RA-1..R-RA-20 incorporated. Slices proceed
independently through §19; none is authorized until its own brief passes. Slice 0d has landed, and Slice 0e has
completed measurement in its isolated worktree with its exact coder-owned integration package still pending.**

This revision incorporates the owner's decisions through 2026-09-04. It does not modify firmware behaviour
and remains subject to independent review. If ratified, it replaces the implementation direction in
`2026-07-26-remote-admin-challenge-response-design.md`; that document remains a historical decision record,
not a compatibility requirement. The completed v2 implementation must remove the current `rcmd` mechanism
rather than support both protocols indefinitely. The verbatim owner record for this revision is
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md` (R-RA-1..R-RA-20).

## 1. Decision in one paragraph

Remote administration has three authority levels. Exact `status` and `routes` requests are open cleartext
diagnostics. Operator and owner requests are authenticated, encrypted RPCs carrying the same command line
that the local serial/BLE dispatcher accepts. The locally attached **mobile controller node** is the RPC
endpoint: it chooses either its normal node identity or one of ten persistent dedicated management
identities, seals requests, and authenticates, reassembles, and decrypts responses. The companion is only a
local plaintext command/output UI; it never holds a MeshRoute management private key or handles an opaque
RPC body. Intermediate relays still transport opaque bytes. Each authenticated request gets an independent
cryptographically random 64-bit `request_id`; it does not
consume or produce the token for the next request. The target feeds
the line to the common dispatcher, captures its normal textual output, returns that output in as many
response DMs as needed, and always ends with a terminal result. A one-byte `response_seq` orders those DMs
and detects gaps. A separate, fixed ten-slot target ACL holds full controller public keys with operator or
owner roles and permits several owners. A compact authenticated discovery bootstrap resolves the selected
public key to its target ACL slot without adding bytes to normal requests. Each managed target also has one
stable administration identity, separate from its ordinary `/mrid`, so ordinary identity regeneration does
not break management trust. The target epoch is likewise kept out of normal authenticated commands. Exact
command/output limits are carrier-specific rather than falsely inheriting the 239-byte direct-DM ceiling.
Carrier E2E acknowledgement is an optional per-request `-a`, default off; the authenticated RPC response is
the ordinary receipt.

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
- Split the abilities to originate and accept remote administration at compile time: mobile profiles are
  client-only, while static and gateway profiles are accept-only. Ordinary typed-DATA transit remains
  type-agnostic and requires no remote-administration capability flag.

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

## 4. Source-verified constraints (refreshed 2026-09-04)

The following are code facts, not inherited assumptions from an older design:

- `protocol::lora_max_frame_bytes = 255`, the C++ DATA header is 8 bytes, and `TxItem::inner` supplies a
  241-byte STORAGE bound (`lib/core/protocol_constants.h`, `lib/core/node_carriers.h`). The on-air authority
  is `data_inner_cap()` / `data_frame_len()` (`lib/core/frame_codec.h:738-747`, the landed [[B20]]/[[B21]] rule), so
  every carrier's governing cap is the minimum of the storage bound and the actual packer's air-fit answer.
  The 241-byte term governs only the approved plaintext RPC carriers. ⚠ **CORRECTED 2026-09-04 by Slice 0e-C;
  prior unconditional claim kept visible:** this paragraph said setting `DATA_FLAG_CRYPTED` on a nonzero typed
  carrier lowers `data_inner_cap()` to 238 and must redden every 241/231/206-byte cap claim. That lower cap exists
  only for a hash-addressed carrier; a by-node-id carrier has no legal outer-`CRYPTED` form because `pack_data`
  requires the clear `DST_HASH` used by the per-DM nonce. RPC confidentiality lives inside the authenticated RPC
  body, not in the outer DATA `CRYPTED` flag.
- The supported normal-DM body ceiling is the deliberately conservative `dm_max_body_bytes = 239`.
- `DATA_TYPE_REMOTE_CMD = 0xA0` and `DATA_TYPE_REMOTE_RESP = 0xA1` already carry remote request/response bodies
  (⛔ corrected 2026-08-29: ordinals 6/7 were RETIRED by the §CUSTODY-A namespace transition — the values now sit
  in the internal range's administration/security block `0xA0..`. Reusability is unchanged; no third DATA type is
  needed. Both are protocol-internal (`0x80..0xBF`), so `data_type_traits()` reports them
  `internal=true, generic_send_lifecycle=false` — the RPC's own response/timeout contract is the only outcome they
  carry, and no generic `send_acked`/`send_failed` may be raised for them.)
  (`lib/core/frame_codec.h`, `lib/core/node_mac.cpp`). They are reusable; no third DATA type is needed.
- The current addressed receive path consumes both remote types in an explicit handler at
  `lib/core/node_mac_rx.cpp:2195`, before the §CUSTODY-B protocol-internal tail guard at `:2403`. That placement is a
  compatibility invariant, not incidental ordering: v2 must replace it with explicit pre-tail handlers owned
  by direction/capability — `REMOTE_CMD` by **accept**, `REMOTE_RESP` by **client**. A build lacking the owner
  for an addressed type deliberately reaches the bounded `unsupported_internal` guard-drop. In particular,
  an accept-disabled mobile drops an addressed REQUEST but its enabled client still consumes a RESPONSE.
  Removing staging without installing those v2 handlers would make addressed RPCs disappear fail-closed but
  operationally invisible.
- A current sealed request body costs 35 bytes before command text:
  `[sealed_flag 1][rand8 8][nonce_ctr 2][node_hash 4][replay_counter 4][tag 16]`. Under the supported
  239-byte ceiling, the envelope arithmetic leaves 204 bytes. ⚠ **CORRECTED 2026-09-04:** the shipped
  implementation does not expose that theoretical command capacity: `firmware_remote.cpp` uses a 64-byte
  plaintext buffer and silently truncates the legacy verb to 63 bytes, while its effective sealed-command
  text ceiling is 56 bytes (`pt[64]`/`v[64]` at `src/firmware_remote.cpp:92`; the `vl` truncation at `:190`).
  That is a legacy defect to delete, not characterization evidence and not an interim cap. R-RA-14 requires
  the one common validator/cap instead.
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
- A mobile's registered **home is always a static node**, never a gateway ([[B132]]), and the wrapper toward
  that home is therefore global-plane traffic by definition. ✅ **LANDED 2026-09-04 by Slice 0d; prior required
  state kept visible:** the pre-slice correction said the current `send_by_hash` implementation did not make this
  invariant explicit, that three delegation arms stamped `Plane::AUTO`, and that R-RA-12 required those arms to
  stamp `Plane::GLOBAL`. Per D-0d-1 the landed slice covered **four** arms, not three: the registered-mobile plain,
  enclosed-type and sealed-relay wrappers plus the cached-home send all stamp `Plane::GLOBAL`. The source proof
  remains `can_host_mobiles()` (`lib/core/node.h:639`), the `flight_is_team_plane()` AUTO resolver
  (`lib/core/node.h:446-452`), and `is_team_peer()` (`lib/core/node_routing.cpp:823`). A prediction-first four-site
  instrument measured `is_team_peer(home_id)` false at all eight corpus occurrences; the final corpus was 36/36
  byte-identical, with no re-anchor (`docs/superpowers/evidence/2026-09-04-radmin-0d.md`).
- The legacy `send_remote_cmd` / `send_remote_response` helpers call `enqueue_data(..., app_dm=false, ...)`
  and therefore attach no `SOURCE_HASH`. They are not v2 carrier precedent. R-RA-13 requires every v2 request
  and response to use the `send_by_hash` / `do_send` application-DM path (`app_dm=true`) so the clear inner
  carries mandatory `SOURCE_HASH`; that clear field is authenticated as AAD while the RPC body is encrypted.
  The two legacy helpers are deleted with the legacy mechanism.
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
- The USB input line is 1024 bytes, the BLE input line is 160 bytes, the guarded USB output stage is 2048
  bytes, `BufferSink` is a 512-byte whole-response capture, and `LineSink` is a 1700-byte line streamer
  (`src/fw_main.cpp:1071`, `src/device_ble.h:79`, `src/console_sink.h:67`,
  `src/dispatch_sink.h:35,63`). These distinct current
  transport limits must be characterized before one shared command-byte validator freezes the v2 command
  cap; none is a remote transcript budget by analogy.
- The main `/mrcfg` blob still embeds the legacy single-admin public key, replay floor, and provisioned flag.
  Configuration code rebuilds that blob for operations including `leave`, while separate versioned records
  such as `/mrjoin` and `/mrteams` already establish the pattern needed for an independent ACL
  (`src/device_nv.h`, `src/firmware_config.cpp`).
- The current local OTA backends are Wi-Fi SoftAP/WebServer upload on ESP32 and BLE DFU on nRF52
  (`src/device_ota.cpp`, `src/fw_main.cpp`). MeshRoute carries only the owner-authorized command that enters
  the board's OTA mode; it never carries the firmware image. The project has no MeshRoute
  application-signing trust anchor, which is why OTA cannot be delegated below owner.
- ⚠ **CORRECTED 2026-09-04; prior claim kept visible:** the round-1 draft called the current inbound remote
  slot “overwriteable”. The node actually exposes one inbound remote slot and **refuses** a second frame with
  `remote_inbound_drop_full`; it does not overwrite the occupied slot (`lib/core/node_mac_rx.cpp:2195-2199`,
  `lib/core/node.h:2905`). A future implementation must not claim pipelining until that ingress boundary is
  made bounded and explicit.
- BLE's current local command line buffer is 160 bytes. Node-side sealing avoids transporting a base64 RPC
  body through that buffer, but does not by itself raise the local BLE command-text ceiling
  (`src/device_ble.h`).
- `MR_PROFILE_MOBILE` currently forces the single `MR_FEAT_REMOTE_MGMT` off, while static and gateway builds
  retain it (`lib/core/mr_features.h:11-45`, `platformio.ini`). That single switch cannot express the approved
  product split and must be replaced by exactly two capabilities before v2 is enabled:
  `MR_FEAT_RADMIN_CLIENT` is true only for mobile profiles and `MR_FEAT_RADMIN_ACCEPT` is true only for static
  and gateway profiles. Transit needs no flag because ordinary DATA forwarding is type-agnostic.
- `TimerWheel::kCap` is 91 and the constants file records the last free timer ID as consumed
  (`lib/hal/timer_wheel.h:25`, `lib/core/protocol_constants.h:403`). No capacity design may silently assume a new
  per-table expiry timer; the characterization slice must price either a cap increase or one shared scan.
- The main configuration blob and `Node` still carry the legacy `admin_pubkey`, `admin_provisioned`, and
  `admin_counter_floor` state (`src/device_nv.h:110-112`, `src/firmware_config.cpp:2426-2427`,
  `lib/core/node.h:2901-2903`; accessors at `:103-110`). Removing it is
  an attributable NV-version slice after the four replacement stores exist, not part of their first semantic
  landing.

## 5. Architecture

There are five separate responsibilities:

1. **Local companion/console adapter** — submits a plaintext target, credential selector, and exact command
   line to the attached controller node; receives plaintext output plus structured progress/terminal state;
   and, for BLE, explicitly acknowledges local receipt. It performs no MeshRoute RPC cryptography.
2. **Controller endpoint in the node** — resolves the target's stable administration public key, selects `self`
   or a dedicated local management seed, creates and retains the exact sealed request, authenticates and
   reassembles responses, decrypts them, and decides whether retry is safe.
3. **Carrier adapter** — puts the controller node's opaque request bytes into the `send_by_hash` / `do_send`
   application-DM path with `app_dm=true`, `Plane::GLOBAL`, and type `0xA0` or `0xA1`; this is the path that
   attaches mandatory clear-inner `SOURCE_HASH`. It
   returns opaque response bytes plus routing/source metadata. The mobile-delegated wrapper, its
   home-originated static leg, and the reverse hosted-mobile path use distinct carrier shapes without
   changing the RPC body. An intermediate carrier needs no
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

The mobile's home is invariantly a static node and never a gateway ([[B132]]). Every wrapper leg to that
home is therefore explicitly `Plane::GLOBAL`; neither caller intent nor `AUTO` resolution may turn it into a
team flight. The preparatory source slice in §19 makes that invariant executable before the first controller
carrier is enabled.

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
remote <target> -e [using=self|keyN] [-a] -- <exact command line>
remote <target> open -- status
remote <target> open -- routes
```

Every `remote` line carries exactly one explicit security statement: `open` selects the two cleartext
diagnostics, while `-e` selects the sealed authenticated form. `open -e` refuses, and a line containing
neither `open` nor `-e` refuses before transmission. ⚠ **CORRECTED 2026-09-04, superseded claim kept
visible:** round 2 said *“`-e` keeps its existing encryption meaning and is not an ACK alias”* but failed to
make it mandatory; the intervening R-RA-15 addendum even said the wrapper refused `-e`. R-RA-18 replaces both
readings: `-e` retains its meaning **and is required here**. Omitting `using=` means authenticated
`using=self`. Optional `-a` requests the existing carrier E2E ACK and B278 custody correlation; it defaults
off. Open access must be written explicitly and never becomes
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

All four remote-administration stores use the existing team-keyring shape as their persistence authority:
a versioned fixed-slot value record, exact-length/version validation, candidate validation before activation,
transactional durable save, static layout assertions, and a `SecretWipeGuard` for every transient carrying
secret material (`src/firmware_team_keyring.h:462`). Their concrete record layouts are designed and ABI-measured
as value types before production storage code lands; no store is introduced as an open-ended heap container.

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

Each managed static node or gateway has one stable administration identity, provisionally persisted as one 32-byte
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

Once `/mrmkeys`, `/mracl`, `/mradmid`, and `/mrtargets` have landed and own their state, a separate
NV-version slice removes `admin_pubkey`, `admin_provisioned`, and `admin_counter_floor` from the main config
blob and removes their `Node` mirrors. That cleanup is not combined with a store's semantic landing: its
reflash/reset consequence and RAM/flash attribution remain independently measurable (C1/C4).

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
base_key = BLAKE2b-512("MeshRoute remote-admin v2 base" || shared32 ||
                      controller_ed_pub32 || target_admin_ed_pub32)[:32]
session_key = BLAKE2b-512("MeshRoute remote-admin v2 session" || base_key ||
                         admin_epoch_le64)[:32]
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

When an execute request receives `session_full`, the controller automatically sends `SAFE_ROLLOVER` under the current
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

The resource measurement must report the exact seen-record count `N` per session, because automatic safe
rollover makes one extra round trip after each `N` newly admitted authenticated executes. That cadence is a
product cost, not an implementation detail. An explicit operator action is required only for force rollover.

After either rotation, the old rollover request cannot authenticate under the new session key. If its
response is lost, ordinary read-only bootstrap discovers the already-current epoch. If several controller
nodes deliberately share one management credential, they also share this slot session: rollover by one
invalidates the others' cached epoch, and they bootstrap again. Safe rollover cannot abandon another controller's
unacknowledged result; force rollover can, but only through the explicit confirmed recovery contract above.
Key sharing does not recreate a continuous per-command counter or prevent later independent requests.

## 8. Proposed wire bodies

All multi-byte integers below use little-endian byte order. Exact codec constants belong in one shared
implementation path (U2); the values and domains below are frozen design inputs and the codec slice proves
them against independent Python-reference known-answer vectors.

### 8.1 Control byte, opcodes, and slot values

The control byte is transmitted as the first byte of every RPC body. Keys, ACL rows, epochs, and request
tables are local state; `ctl` is not. It remains one byte so every command and every multi-frame response
does not pay a second byte:

```text
bits 7..4  opcode within REMOTE_CMD or REMOTE_RESP
bits 3..0  established ACL slot 0..9, or sentinel F
```

The outer DATA type already distinguishes request from response. The opcode nibbles are:

| Outer type | Nibble | Meaning |
|---|---:|---|
| `REMOTE_CMD` | `0x0` | `AUTH_EXECUTE` |
| `REMOTE_CMD` | `0x1` | `OPEN_EXECUTE` |
| `REMOTE_CMD` | `0x2` | `BOOTSTRAP` |
| `REMOTE_CMD` | `0x3` | `RESPONSE_ACK` |
| `REMOTE_CMD` | `0x4` | `SAFE_ROLLOVER` |
| `REMOTE_CMD` | `0x5` | `FORCE_ROLLOVER` |
| `REMOTE_RESP` | `0x0` | `OUTPUT` |
| `REMOTE_RESP` | `0x1` | `TERMINAL` |
| `REMOTE_RESP` | `0x2` | `BOOTSTRAP` |
| `REMOTE_RESP` | `0x3` | `ROLLOVER_RESULT` |
| `REMOTE_RESP` | `0x4` | `PROTOCOL_ERROR` |

All other opcode nibbles remain reserved and reject. Values are append-only after the codec/KAT slice.

Slots 0..9 select established authenticated ACL sessions. Nibbles A..E are reserved and reject. Sentinel F
is valid only with `OPEN_EXECUTE`, an open response, or a bootstrap request before the target ACL row is
known. A bootstrap response carries the actual matched slot 0..9. Open and bootstrap are opcodes, not fake
ACL slots. The decoder chooses one body layout from outer direction, opcode, slot class, and exact length;
an invalid authenticated request never falls back to the open decoder.

The exact ASCII KDF labels (without a trailing NUL) are
`MeshRoute remote-admin v2 base`, `MeshRoute remote-admin v2 session`, and
`MeshRoute remote-admin v2 nonce`. Base-key input order is
`label || shared32 || controller_ed_pub32 || target_admin_ed_pub32`; session-key input order is
`label || base_key32 || admin_epoch_le64`. In the nonce formula, `selected_key32` means the actual 32-byte
key under which that message is sealed or tagged: `base_key32` for a bootstrap request, bootstrap response,
or rollover-result response, and `session_key32` for established-session traffic. The nonce input order is
`label || selected_key32 || outer_type_u8 || ctl_u8 || request_id_le64 || response_seq_u8 ||
source_hash_le32`, followed by `admin_epoch_le64` only for the **bootstrap response** and
`ROLLOVER_RESULT` domains. A bootstrap request cannot include an epoch the controller does not yet know;
the BLAKE2b-512 result is truncated to 32 bytes for keys and 24 bytes for the XChaCha nonce. AEAD associated
data is `outer_type_u8 ||` the exact clear RPC-header bytes in wire order `|| source_hash_le32`; ciphertext
and tag are not repeated in AAD. The independent-reference KATs must pin every domain, both directions and
cross-domain inequality—not merely round-trip through the production codec.

The opcode namespace plus these remote-v2 KDF labels are the subprotocol discriminator. A later incompatible
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

Authenticated `PROTOCOL_ERROR` is reserved for a structurally valid, authenticated request whose exact
operation is already known but for which no ordinary terminal replay exists—for v2, the concrete case is an
exact retry after that response was already acknowledged, carrying the compact code
`already_acknowledged`. It uses its distinct response opcode and therefore does not reuse a `TERMINAL` nonce
for different plaintext. Authentication/tag failures remain silent. The clear/open `PROTOCOL_ERROR` is
limited to structurally valid open-request validation failures that can be answered within the open
rate/resource bounds; malformed or unauthenticated garbage remains a silent drop.

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

The local USB/BLE surface extends `ios-companion/INBOX_SYNC_CONTRACT.md`; it does not invent remote-admin-only
framing. Requests remain plaintext console commands. Structured BLE output, terminal, re-offer, pressure, and
receipt acknowledgement use that contract's existing newline-delimited `{"ev":...}` event and
`{"ack":...}` acknowledgement conventions, with the 64-bit request ID represented in one byte-exact form
fixed by the contract before implementation. USB renders the same semantic output/terminal through the
request's supplied `Print&`, while successful complete emission is its acceptance event rather than a second
inbound acknowledgement command. The companion-contract slice supplies byte-exact golden lines and an
executed renderer/dispatch gate using the `tools/probe_console_sink/` and `tools/probe_custody_usb/` precedent;
a structural grep alone is not a wiring gate.

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

`carrier_rpc_body_cap` is obtained from one codec authority,
`remote_body_cap(RemoteCarrier carrier)`, which derives its result from `data_inner_cap()` /
`data_frame_len()` and that carrier's required immutable fields. It is never copied as literals or derived
from `dm_max_body_bytes` by analogy. Every supported request and response carrier must have a boundary test
that the real packer accepts exactly at its returned cap and refuses cap plus one. As a current
source-verified example, the same-layer registered-mobile
request wrapper must fit `[dst_hash 4][origin 1][source_hash 4][enclosed_type 1][RPC body]` in the 241-byte
inner buffer, so its RPC-body cap is 231 and its authenticated command cap is 206. A cross-layer path spends
additional carrier bytes and must publish its own lower bound. The home-originated static leg, target
response, hosted-mobile last mile, and every cross-layer variant receive the same packer-derived treatment
in the carrier slice; there is no direct static-controller carrier.

⚠ **CORRECTED 2026-09-04; prior unconditional wording kept visible:** this paragraph said *"setting
`DATA_FLAG_CRYPTED` makes the real air-fit authority return 238 before carrier fields, so the cap battery must
include that mutation."* That is true only where outer encryption is structurally legal. Every request and response
carrier still requires `SOURCE_HASH`; no implementation may drop it to recover body capacity. The encrypted object
is the RPC body, while `SOURCE_HASH` remains clear and AEAD-bound.

⚠ **Measured 2026-09-04 by Slice 0e-C: outer `CRYPTED` is available only to a hash-addressed carrier.**
`pack_data` (`lib/core/frame_codec.cpp:900`) refuses a `CRYPTED` frame carrying no `DST_HASH`, because the per-DM
nonce derives from the cleartext `dst_key_hash32`. For the six by-node-id carriers the sealed variant therefore
has **no cap at all**, rather than a lower one; the cap battery's outer-`CRYPTED` mutation applies to the eight
hash-addressed carriers only.

The same Slice 0e real-packer table independently confirms the registered-mobile example above and measures
plaintext RPC-body caps spanning **226 through 236 bytes** across all fourteen carriers. The minimum is the
cross-layer, key-hash-addressed carrier at legal path depth 4 (**226 bytes**); later carrier slices must design
their authenticated command/output budgets against their own row, never promote the largest row to a universal
cap. Slice 2 owns the production `remote_body_cap(RemoteCarrier)` authority; the characterization table is its
accepted KAT input, not a second runtime authority.

The 16-byte authentication tag and 8-byte request identity are the two load-bearing authenticated envelope
costs; the transmitted one-byte control is the remaining RPC metadata. Open diagnostics omit the tag by
explicit policy and therefore provide no security claim. No acceptance test may call 214/213 the universal
normal-command/output budget.

## 9. Nonce and request-ID rules

For authenticated traffic, a 24-byte XChaCha nonce is derived, not transmitted, from the message's actual
sealing/tag key (`base_key32` or `session_key32` as defined in §8.1) plus a domain containing the complete `ctl` byte, message direction, `request_id`,
`response_seq` (zero for requests), and the stable logical controller `SOURCE_HASH` captured from the
request carrier. Only the bootstrap **response** and rollover-result response domains additionally include
the clear `admin_epoch`; the bootstrap request does not. Thus the response to a replayed request after epoch
rotation uses a different nonce. The
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

Two controllers intentionally sharing one credential also share the session key. Their distinct stable
`SOURCE_HASH` values in the nonce domain are the pre-encryption nonce-separation property; target fingerprint
detection is only the after-the-fact collision refusal. Controllers that clone both the management seed and
the ordinary `/mrid` (and therefore the source hash) are outside this design's supported identity model.

The `request_id` is frozen as a cryptographically random 64-bit value. It is not shortened, replaced by a
counter, or coupled to a per-epoch cap. Bootstrap is read-only and returns the target's current epoch; after a
controller reboot a counter would therefore restart under the same session key and reuse XChaCha20 nonces.
At the design's `2^16`-request analysis envelope (not an enforced per-epoch cap), the birthday collision
probability is about `2^-33`. The codec/session evidence must derive that bound, include deliberately shared-controller
concurrency, and prove RNG failure is a loud pre-transmission refusal. Target-side fingerprint detection
still rejects an authenticated repeated ID carrying a different tag, but it occurs after same-key/same-nonce
ciphertexts may have appeared and is not the nonce-safety argument.

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
   plaintext in the old nonce space; return authenticated `PROTOCOL_ERROR{already_acknowledged}` under its
   distinct opcode/nonce domain so an exact retry receives a bounded truthful answer.
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

Command bytes pass one shared validator before local or remote dispatch. It rejects embedded NUL, CR, and LF,
and enforces one named `common_command_max_bytes` limit for USB, secured BLE, authenticated RPC, and open RPC;
there is no more-permissive remote parser. The preparatory characterization must derive that value against the
current 1024-byte USB input, 160-byte BLE input, carrier-specific RPC caps, and existing command corpus before
implementation freezes it. Every path tests the boundary and cap-plus-one, and a rejection is explicit rather
than truncation or newline splitting. Response/transcript limits remain separate.

That universal value cannot exceed the smallest accepted command carrier, so it necessarily shortens the
current 1024-byte USB command surface: even the current registered-mobile example permits only 206 command
bytes, and a measured cross-layer carrier may lower the common value further. The characterization must
report the exact `1024 → common_command_max_bytes` change and obtain the capacity ruling knowingly; it may
not hide the product change behind “common”. In command syntax, `-e` remains optional for ordinary `send`,
where it overrides the configured encryption default, but R-RA-18 makes it mandatory on authenticated
`remote`. The manual and help must state that difference, and `remote open -e` or `remote` with neither
security statement refuses.

⚠ **CORRECTED 2026-09-04:** the legacy remote path's 56/63-byte truncating buffers are not evidence for this
authority and do not receive a compatibility cap. Characterization uses the actual USB/BLE inputs, the real
v2 packer-derived carrier bounds, and the complete command inventory. The legacy defect disappears when its
helpers and parser are deleted; no path may preserve silent truncation in the interim.

Current local command execution is not yet perfectly unified: `send` and `send_channel` are handled by
the serial/BLE callers around `dispatch()`, and `regen` writes through the global console sink. The
implementation may not claim same-command parity until those applicable paths use one transport-neutral
execution seam and the supplied output sink. Per C1, that consolidation and remote enablement must remain
separately reviewable.

The command/subcommand authority table is generated from the complete production dispatcher and its USB/BLE
caller-only arms by a pinned `tools/` instrument in the `check_data_type_literals.py` idiom. The generator,
not a hand-written table, owns completeness and makes an added/removed verb or sub-verb a visible diff. Once
the generated inventory exists, the owner assigns exactly one minimum authority to every row in a separate
ruling; code generation does not guess policy from names. The implementation gate fails on an unclassified
or multiply classified row.

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

Physical-only authority is deliberately narrow and split by the profile that owns it:

- on a managed static/gateway accept build: install the first owner when no valid ACL exists, recover from a
  corrupt/unreadable ACL or loss of every owner, and generate/recover/rotate the target's stable
  administration identity;
- on a mobile client build: generate, import, export, replace, or remove a dedicated
  controller-management seed.

Those concrete operations are USB-only in v2. Their internal service seams may be transport-neutral, but a
future BLE caller is not authorized until it supplies a separately reviewed physical-presence signal. A
future operation is not classified merely by being future work; if its security proof genuinely requires
physical presence, it must receive a separate explicit classification.

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

The two deadlines are frozen as named derivations, not literals at call sites. ⚠ **CORRECTED 2026-09-04;
superseded claims kept visible:** the first draft stated a bare 30-second activation value as though it were
inherited, and round 2 replaced it with an underspecified “one complete reply path” formula plus an
“approximately 30 s” example. The legacy remote path actually defers for only 3 seconds
(`src/firmware_remote.cpp:148`). R-RA-20 replaces all three claims with this exact first-hop budget:

```text
remote_scheduled_reply_path_budget_ms(cfg) =
    airtime_ms(RTS at cfg PHY)
  + airtime_ms(CTS at cfg PHY)
  + airtime_ms(maximum TERMINAL{scheduled} DATA at cfg PHY)
  + airtime_ms(ACK at cfg PHY)
  + cts_to_data_gap_ms
  + rts_max_retries * rts_busy_retry_ms
  + MAC CTS-wait window at cfg PHY     (production authority named by Slice 0e)
  + MAC ACK-wait window at cfg PHY     (production authority named by Slice 0e)
  + cascade_requeue_base_ms
```

`cts_to_data_gap_ms` is 5 ms (`lib/core/protocol_constants.h:133`), the busy term is
`2 * 30 ms` (`rts_max_retries` at `:135`, `rts_busy_retry_ms` at `:134`), and
`cascade_requeue_base_ms` is 5 s (`:273`). The final term prices exactly one first requeue; it is not the
30-second cascade backoff cap and not `send_defer_ttl_ms`. Slice 0e must name the production CTS-wait and
ACK-wait authorities, derive the terminal DATA length through the actual packer, independently recompute
every airtime term with `airtime_ms()` (`lib/core/airtime.h`), and publish the first concrete budget/default
values. Slice 7a—not
Slice 0e—owns the production `remote_scheduled_reply_path_budget_ms(cfg)` authority and its persisted cfg
consumer.

- `remote_action_activation_min_ms(cfg) = remote_scheduled_reply_path_budget_ms(cfg)`; an action may not be
  configured to fire before the scheduled terminal has left this first-hop reply path.
- `remote_action_activation_default_ms(cfg) = 2 * remote_scheduled_reply_path_budget_ms(cfg)`. The factor
  of two is the owner's explicit 2026-09-04 choice; no approximate literal is a design authority.
- `remote_action_activation_max_ms = e2e_ack_deadline_xl_ms - 1`, strictly below the outer 300-second bound.
  A target configuration whose derived floor/default cannot fit this interval must refuse disruptive remote
  scheduling loudly; it may not clamp an impossible promise.
- target `cfg.remote_action_activation_ms` is runtime-configurable only inside
  `[remote_action_activation_min_ms(cfg), remote_action_activation_max_ms]`. Adding and persisting this field
  is its own attributable NV/schema change before the behaviour consumes it. A valid `RESPONSE_ACK` may
  activate earlier, but loss of that ACK never postpones the action beyond the configured promise; and
- `remote_disruptive_outcome_deadline_ms = e2e_ack_deadline_xl_ms`
  (`lib/core/protocol_constants.h:780`) is the controller's 300-second outer bound for retaining/reporting
  the disruptive operation's transport outcome. Crossing it reports unknown;
  it neither cancels an already accepted action nor extends its activation delay.

The characterization must publish every formula input and the measured current default before the owner
accepts its concrete value. Codec/config/scheduler tests pin both sides of the floor and ceiling, prove the
default changes with its source timing inputs, refuse an impossible interval, and distinguish the legacy
3-second behaviour from v2. This is deliberately a **first-hop** budget: once the first relay has
hop-acknowledged the scheduled terminal, a target reboot no longer loses that answer from the target's reply
path. Continued end-to-end delivery is governed by the existing 300-second outer bound. Reusing that
cross-layer deadline keeps the controller's maximum wait aligned with the mobile-delegated carrier instead
of minting an unrelated patience literal.

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

Exactly two firmware capabilities replace the broad `REMOTE_MGMT` meaning:

- **client** (`MR_FEAT_RADMIN_CLIENT`) — accept a local plaintext request, select `self` or `keyN`, resolve the target-administration
  public key, discover/bootstrap the target ACL session, seal and retain exact request bytes,
  authenticate/decrypt and reassemble responses, enforce retry honesty, and deliver scoped local plaintext;
- **accept** (`MR_FEAT_RADMIN_ACCEPT`) — decode the target-side body, authenticate when required, enforce
  authority, and dispatch a remote command.

Transit is deliberately not a third capability. Every relay already forwards typed DATA without interpreting
its body, so opaque request/response transport remains available regardless of both remote-administration
flags. Adding a transport flag would create a way to break routing without protecting a secret or endpoint.

The companion-facing product surface enters the client capability with plaintext target/selector/command
data and receives plaintext response events plus a terminal. It does not accept an already-sealed body from
the companion, expose raw authenticated response bodies as the normal result, or ask the companion to apply
a private management key. Node-side sealing uses the exact command tail; it does not recreate the old
remote-only verb language.

Mobile builds enable **client** and compile **accept** out. A companion attached to a
mobile can therefore administer a multi-hop static target through the mobile's existing delegated routing
path without making that mobile remotely administrable. Its home in that path is always a static node,
never a gateway ([[B132]]), and every mobile-to-home wrapper is explicitly global-plane. Static and gateway builds enable **accept** and
compile **client** out: they are managed endpoints, never controllers. A node acting only as an intermediate
relay neither opens the RPC body nor needs a dedicated management secret merely to forward it. Consequently,
controller keyring and target-book state exist only in mobile builds; target identity and ACL state exist
only in static/gateway builds.

The current `MR_FEAT_REMOTE_MGMT` flag and its `MR_PROFILE_MOBILE => 0` rule cannot represent this split.
Implementation keeps that legacy gate alongside the two new flags until the legacy deletion slice; the
feature-boundary scaffold must not disable shipped `rcmd` behaviour early. It introduces the two exact flags
above and compile-time assertions for every **board** profile:
mobile means `{client=1, accept=0}`, while static and gateway mean `{client=0, accept=1}`. A build may not
enable both or neither in a product configuration. The native host-test build is deliberately not a product
profile and compiles `{client=1, accept=1}` so one process can drive controller and target end to end; its
tests must still exercise each role-disabled boundary separately. Separate compile and behaviour controls prove that
disabling either endpoint capability leaves ordinary relay transport intact.

The receive-dispatch side follows the same capability split. `REMOTE_CMD` has an explicit pre-tail consumer
only when **accept** is enabled; `REMOTE_RESP` has one when **client** is enabled. Static/gateway builds never
own the response consumer. An accept-disabled mobile therefore guard-drops an addressed request with bounded
scalar telemetry after every forwarding role, while still consuming an addressed response through its client.
A static/gateway build guard-drops an addressed response, which cannot be a valid endpoint result there. No
addressed RPC may depend on ordinary DM delivery, and replacing the legacy staging arm must not accidentally
remove either owned handler.

The compact RPC **layout** is carrier-independent; its maximum body length is not. Every remote-admin carrier
is explicitly static/global-plane traffic (`Plane::GLOBAL`), never `AUTO` and never team-plane. The source
path is `send_by_hash` / `do_send` with `app_dm=true`; the legacy `send_remote_*` helpers are forbidden because
they do not attach the source field. Every authenticated or open v2 carrier must preserve mandatory clear-inner
`SOURCE_HASH`, expose it to the codec as associated data,
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
another copy may already have arrived. There is no static-controller pending-request path in v2. A report
about a response remains separate factual evidence at the managed target; the first implementation does not
change transcript resend timing solely because it arrived—the authenticated response-ACK/debt rules remain
authoritative.

The only product controller topology is a mobile attached locally over USB or secured BLE. Its
carrier has two identities: the mobile delegates wrapper `{dstHome, ctrM}` to Home1, then Home1 re-originates
the static/global RPC flight under `{dstTarget, ctrH}`. The wrapper and final hosted-mobile last mile are
outside custody v1; the inter-home/static leg is eligible, but a request-loss notice names Home1/`ctrH` and
terminates at Home1 while the controller pending record lives on the mobile under `ctrM`.

⚠ **UPDATED 2026-09-03, OLD CLAIM KEPT VISIBLE.** This paragraph used to end: *"There is currently no
custody equivalent of the E2E-ACK `ctrH -> ctrM` translation. Therefore [[B278]] is a REQUIRED prerequisite
for Slice 9 … Until B278 lands, a mobile controller may remain timeout-correct but must not claim
custody-aware request evidence."* **[[B278]] has landed (software-complete 2026-09-03; metal Part 54
pending); “Slice 9” in the historical quotation meant the controller carrier, now Slice 8b. That carrier now
EXISTS and this design consumes it rather than waiting for it.** The landed
contract, and the whole of what a remote-admin consumer may rely on:

- every local `remote` line carries exactly one security statement. `remote <t> open -- status|routes` is
  cleartext; `remote <t> -e [using=self|keyN] [-a] -- <cmd>` is authenticated and sealed. `open -e` and a
  line carrying neither `open` nor `-e` both refuse. Here `-e` is mandatory for the authenticated form,
  while `-a` remains independent, optional and default-off;
- an RPC request sent from a mobile through its home requests the **existing E2E ACK only when the local
  operator supplied `-a`**. The option is per request and defaults off; `-e` retains its existing encryption
  meaning and is not reused. Without `-a`, R1=A allocates no delegated-correlation row, B278 supplies no
  custody feedback, and the RPC response is the receipt (`lib/core/node_hashlocate.cpp:1640,1666,1669`).
  With `-a`, the request costs one extra return flight and one of the mobile's eight correlation rows
  (`lib/core/node.h:3185`) for up to 300 seconds (`lib/core/protocol_constants.h:790`). B278 adds no new
  request-progress mechanism and no new opcode;
- a custody failure on the **static leg** returns to the mobile as the existing `custody_failure` event,
  carrying the home's `ctrH` unchanged in `ctr` and the mobile's own counter in **`mobile_ctr`**, plus
  `delegated=true`, `target_kind` and exactly one of `target_id` / `target_hash`;
- a remote-admin consumer matches it on the **complete six-field body tuple**
  `{failed_origin, reporter_layer, target_kind, target_value, mobile_ctr, failed_type}` — ⛔ never on a
  counter alone and never on the inbox record key;
- ⛔ it **cannot** become an RPC response, satisfy an RPC terminal, become an authentication or permission
  failure, or prove that the target did not execute: another copy may already have arrived;
- it **may** support the UNCERTAIN/retry policy designed elsewhere in this document, but only under that
  design's exact-byte and idempotence rules — **B278 itself never retries**; and
- ⛔ **[[B112]] remains OPEN and SEPARATE.** B278 does not resolve first-hop-ACK admission truthfulness and
  must never be described as doing so. B112 is a prerequisite of the first controller-carrier slice, because
  the mobile cannot report honest RPC admission while that first-hop ACK can overclaim it.

The v1 generator boundary is unchanged: custody is still generated only on static/global same-layer transit,
and team-plane / hosted-last-mile generation still requires a separate ruling.

## 15. Bounded queues and backpressure

Replacing the current single inbound slot is part of correctness, not an optional throughput improvement.
The target implementation needs:

- a bounded inbound operation queue or an explicit one-at-a-time admission contract;
- a bounded authenticated response transcript pool and seen-request table;
- bounded staging for an open multi-frame response;
- a fixed authenticated/open/bootstrap partition so unauthenticated work cannot consume every
  owner-recovery admission;
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

The optional carrier E2E ACK is a separate, explicit capacity choice. A default RPC request uses no
delegated-correlation row and treats its authenticated RPC response as receipt. Supplying `-a` requests the
extra E2E ACK, enables B278 custody feedback, adds one short return flight, and occupies one of the mobile's
eight shared delegated-correlation rows for at most `delegated_custody_ttl_ms == 300 s`. Consequently no more
than eight ACK-requesting delegated sends—RPC and ordinary `-a` traffic combined—can be outstanding through
one mobile at once; non-ACK RPC pending-table capacity is independent of that ring.

Independent IDs permit several requests to be in flight, but the target may still dispatch only one at a
time. If a request was authenticated but cannot be admitted, it receives a terminal refusal when transport
capacity permits; unauthenticated malformed garbage remains a silent drop to avoid an oracle. A well-formed
open diagnostic may receive a clear refusal, but it never bypasses the shared bounds or rate limit.

Before production state lands, the controller pending/session/result records and target
seen/transcript/ingress records are written as candidate value types and measured through
`tools/probe_board_abi.py` on host, ARM (`gateway`) and Xtensa (`heltec_mobile`). The characterization reports
each `sizeof`/alignment, aggregate cap cost, remaining board RAM, and timer cost. Exact capacities are then an
owner ruling; they are not guessed or silently reduced to fit. The current gateway measurement is already
about 83% RAM and `TimerWheel::kCap == 91` has no free ID, so expiry work must explicitly choose and measure a
cap increase or a shared scan timer.

The resource partition rule is fixed even though its numbers await that measurement: open/bootstrap traffic
has a bounded peer-local admission (provisionally no more than one open response in flight per peer), and at
least one authenticated owner/control slot is reserved where open traffic cannot take it. Open work may
neither starve authenticated recovery nor consume transcript state already promised to an authenticated
request. The measurement slice replaces the provisional quantities with exact reviewed numbers and attacks
both starvation directions. A full retained BLE result causes loud local backpressure; it is not an excuse
to acknowledge the target early or discard an older result.

## 16. End-to-end flow

The mobile controller owns credential selection, target-slot discovery, session state, sealing, response
opening, and local delivery. The companion supplies and receives plaintext. Its home and all other relays
know only how to route the opaque body; the relay column may be absent on a shorter static leg, but the home
is always the mobile's delegation boundary.

```text
companion       mobile controller             Home1             relay(s)       target static/gateway
    | plaintext target, -e, using=key2, command | |                 |                    |
    |-------------------------------------->|   |                   |                    |
    |        resolve /mrtargets; reserve; derive selected key      |                    |
    |        BOOTSTRAP(slot=F, pub, source_hash, R0)                |                    |
    |                                      |--delegated wrapper--->|------------------->|
    |                                      |<----------------------| BOOTSTRAP(slot=3, epoch)
    |        authenticate; cache slot 3; derive session key        |                    |
    |        seal AUTH_EXECUTE(slot=3, source_hash, R1)             |                    |
    |                                      |--delegated wrapper--->|------------------->|
    |                                      |                       | reserve + dispatch once
    |                                      |<----------------------| OUTPUT(R1, seq=0) |
    |                                      |<----------------------| TERMINAL(R1, seq=1)
    |        authenticate, decrypt, assemble, retain               |                    |
    |<-------------------------------------| plaintext output + terminal                 |
    | local ACK R1 (BLE; USB emission is acceptance)                                    |
    |------------------------------------->|                                             |
    |        seal RESPONSE_ACK(slot=3, R1)                                              |
    |                                      |--delegated wrapper--->|------------------->|
```

A later `R2` is valid regardless of whether an R1 request, response, or ACK was lost. Selecting a dedicated
credential changes only the controller identity and discovered target ACL slot; routing is unchanged.
The diagram omits the optional carrier E2E ACK. A default request relies on the authenticated RPC response as
its receipt and allocates no B278 row. Adding local `-a` requests the separate carrier ACK/custody path; it
does not replace the later authenticated `RESPONSE_ACK`, whose purpose is transcript release after local
delivery.

The open path skips bootstrap and sealing:

```text
companion --plaintext "remote <target> open -- status"--> mobile controller
mobile controller --delegated clear OPEN_EXECUTE(slot=F, id=O1, "status")--> Home1 --> relay(s) --> target
mobile controller <--delegated clear OUTPUT...TERMINAL, explicitly unauthenticated<-- Home1 <-- target
companion <--plaintext result labelled unauthenticated-- mobile controller
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
- remove `send_remote_cmd` and `send_remote_response`; their `app_dm=false` carrier lacks mandatory
  `SOURCE_HASH` and cannot be a v2 compatibility path;
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

The ruled 36-stream corpus currently contains zero `0xA0`/`0xA1` DATA and zero `rcmd` events. Legacy
replacement is therefore expected to be corpus-inert, but that absence is not behavioural proof: the
pre-tail receive boundary, capability-disabled guard-drop, universal command cap and both live carriers must
be proven by production-shaped native cases and their mutation controls. A new corpus scenario would be a
separate table-ruling decision, not an unreviewed way to manufacture coverage.

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

0. **Pre-feature phase — five independent slices, each with its own brief, gate and commit:**

   - **0a:** complete B208's bounded help-topic split;
   - **0b:** fix B279's source-confirmed `regen` supplied-sink defect;
   - **0c:** make the existing dispatcher/caller output path transport-neutral without adding remote context
     or policy;
   - **0d, static-home plane invariant:** implement R-RA-12 as the only behaviour change. ⚠ **SCOPE
     CORRECTION D-0d-1, 2026-09-04; the passed three-arm wording remains visible:** the passed design said
     “the three `send_by_hash` delegation arms that send a wrapper toward the mobile's static home stamp
     `Plane::GLOBAL` instead of `Plane::AUTO`.” The same invariant also governs the cached-home arm that sends
     to a target mobile's static home (`lib/core/node_hashlocate.cpp:1859` at pre-check): an unregistered team
     mobile has the same local-ID collision exposure. Slice 0d therefore changes all **four** home-bound arms
     together and re-aims B278-S3's static-sender AUTO-equivalence pin to explicit GLOBAL. Pin “home is static,
     never gateway” and the mixed-ID collision
     that previously selected team plane; predict and attribute every corpus delta before accepting it. Its
     source fence includes the correction-idiom rewrite of `lib/core/node_mac.cpp:159-161`, whose claim that
     the named paths are “the only producers of GLOBAL” becomes false when these four producers land.
     ✅ **LANDED 2026-09-04.** All four arms now stamp `Plane::GLOBAL`; the B278-S3 static-sender pin was re-aimed
     to explicit GLOBAL and gained a mixed-ID collision control, while the stale producer census was corrected
     with its old claim visible. Native moved 2578/108904/0 → 2587/109106/0; corpus was predicted and measured
     36/36 byte-identical; 63 mutations were RED with zero unusable; `Node` and ruled-board RAM were unchanged,
     and both ruled images were size/section/symbol identical. No re-anchor, owner ruling or metal step is owed;
     and
   - **0e, characterization and generated authorities:** generate and pin the complete command/subcommand
     inventory for the later owner classification (R-RA-1); define candidate value types for every
     controller/target bounded state record and measure their cap/RAM/timer cost on host, ARM and Xtensa
     (R-RA-2); and derive every carrier's request/response cap through the real packers, including boundary
     and cap-plus-one probes including the outer-`CRYPTED` negative control (R-RA-3). It also derives
     first-hop budget inputs from the production timing authorities, names the MAC CTS-wait and ACK-wait
     windows, and reports the resulting `remote_scheduled_reply_path_budget_ms(cfg)` and default/floor
     feasibility. This slice reports facts; it neither guesses authority nor lands capacities,
     configuration, or the production function—Slice 7a owns that authority.
     ✅ **MEASURED 2026-09-04.** The characterization produced a 177-row blank-authority command inventory,
     host/ARM/Xtensa candidate-layout and timer-cost sheets, fourteen real-packer carrier rows, and the first
     source-derived activation budget. Its owner classification/capacity/timer choices remain separate rulings;
     the unconditional outer-`CRYPTED` claim is corrected in §8.11. The coder-owned evidence and instruments were
     produced in the isolated 0e worktree and must be integrated from that worktree as one exact package before
     any later brief consumes them.

   Do not combine any of these fixes/refactors with each other or with remote execution (C1).
1. **Feature-boundary scaffold:** add
   `MR_FEAT_RADMIN_CLIENT` and `MR_FEAT_RADMIN_ACCEPT`. Compile-time and behaviour controls prove
   `{client=1,accept=0}` on mobile and `{client=0,accept=1}` on static/gateway while ordinary transit stays
   available with neither endpoint consumer involved; native deliberately compiles `{1,1}` with separate
   role-disable controls. Keep `MR_FEAT_REMOTE_MGMT` alive for the shipped legacy issuer/acceptor until Slice
   9 deletes those paths. No v2 wire behaviour yet.
1b. **Capability-owned pre-tail remote handlers:** refactor only the existing staging arm at
   `lib/core/node_mac_rx.cpp:2195-2212` behind two explicit entry points: `REMOTE_CMD` is owned by accept and
   `REMOTE_RESP` by client. Under the legacy `MR_FEAT_REMOTE_MGMT` gate their bodies remain behaviour-identical;
   a disabled role deliberately falls through to the existing bounded `unsupported_internal` tail guard.
   Prediction precedes a 36/36 corpus-identity proof, native drives all four role-by-type combinations,
   receiver-file mutations pin both entry points and the tail, and the ruled board pair covers the product
   roles. Slices 5/7b and 8b later fill the accept and client bodies respectively; Slice 9 deletes the legacy
   body without removing the capability-owned entry points.
   ⚠ **CORRECTED 2026-09-04, prior staging plan kept visible:** round 2 left the shared staging arm in place
   until the later semantic slices. That made the role-disabled fail-closed boundary depend on future work.
   R-RA-19 moves only the ownership/refactor into 1b; it does not enable a v2 body.
2. **Remote codec and independent KATs:** pin the frozen opcode/slot values, little-endian fields, exact
   KDF/nonce/AAD labels and layouts, invalid/all-zero ECDH refusal, authenticated/open codecs,
   `remote_body_cap(carrier)`, legacy-body rejection, corruption/nonce-separation controls, and the random
   64-bit request-ID bound at the non-enforced `2^16`-request analysis envelope. No global `wire_version`
   bump or corpus-wide version re-anchor.
3. **Target identity, ACL, and USB provisioning:** on accept builds add `/mradmid` and the fixed ten-slot
   `/mracl` transaction using the team-keyring persistence idiom, first-owner USB exchange, several-owner
   invariants, corrupt-state recovery, USB-only target-root rotation, role changes, and ordinary-`regen`
   preservation tests. No remote execution yet.
4. **Mobile controller keyring and target book:** on client builds add the ten seed-only `/mrmkeys` slots and
   32-row `/mrtargets` using the same persistence idiom, seed-derived identity path, USB-only secret
   operations, public list/show, transactional persistence, in-use refusal, accepted measured capacities,
   messaging-peer independence, and `regen` behaviour. No on-air RPC yet.
5. **Target authenticated session/dedup state:** full-key discovery/bootstrap, epoch/session derivation,
   bounded seen-table value types, retained source identity and request-fingerprint classification. It may
   establish the table keys, reservation shape and reboot epoch boundary, but it does **not** consume or
   manufacture response transcripts. Exact transcript retry, ACK release/debt, `session_full` /
   `session_busy`, shared-credential transcript behaviour, automatic safe rollover, confirmed force
   rollover, and the already-acknowledged protocol response belong to Slice 7b after transcripts exist. Use
   the measured resource partition and explicit shared-scan/timer decision and report the measured table
   count `N`.
   ⚠ **CORRECTED 2026-09-04, prior allocation kept visible:** round 2 assigned “exact retry, ACK
   release/debt, `session_full`/`session_busy`, automatic safe rollover, confirmed force rollover and
   already-acknowledged protocol error” to Slice 5. Those are transcript behaviours and cannot be implemented
   before Slice 7b owns the transcript; Slice 5 is now fenced to pre-transcript session/dedup state.
6. **Common dispatcher remote context/result:** on the already transport-neutral seam, land the shared
   command-byte validator and remote authority context/result, then consume the generated inventory plus its
   separate owner-approved command/subcommand classifications. No remote request reaches it yet, and local
   serial/BLE behaviour remains unchanged except where that classification/validation was separately ruled.
   This slice cannot start until the complete generated authority table has received its separate owner
   classification; missing or duplicate rows refuse the brief.
7. **Target transcript and activation contract — two separately attributable commits:**
   - **7a, activation configuration:** add only the persisted target
     `cfg.remote_action_activation_ms`, its source-derived default/floor/ceiling validation, schema migration,
     refusal of an impossible interval, and boundary controls. This is the R-RA-16 NV change; it changes no
     action timing yet.
   - **7b, transcript and action behaviour:** multi-DM sink, authenticated/open terminal responses,
     exhaustive enqueue handling, measured open/bootstrap partition and rate limit, authenticated-resource
     reservation, ingress/backpressure, exact transcript retry, ACK release/debt, `session_full` /
     `session_busy`, shared-credential transcript behaviour, automatic safe rollover on `session_full`,
     confirmed force rollover, `PROTOCOL_ERROR{already_acknowledged}`, local-only OTA-mode reporting, and the
     configured activation plus 300-second outer disruptive-action deadline with mutation controls.
8. **Mobile-delegated controller — three bounded slices:**
   - **8a, controller state and crypto (host-visible, no carrier):** pending/session/result state, explicit
     credential selection, `/mrtargets` resolution, discovery cache, request sealing, response opening,
     exact-byte retry state, and pressure/refusal behaviour. No on-air RPC is enabled.
   - **8b, the only product controller carrier:** only after [[B112]] is fixed, connect the mobile client to
     `send_by_hash` / `do_send` with `app_dm=true`, mandatory `SOURCE_HASH`, type `0xA0/0xA1` and
     `Plane::GLOBAL`; implement optional per-request `-a` (default off), packer-derived caps, request/response
     routing and B278 custody consumption. A custody report stays distinct from response/authentication and
     initiates no automatic retry. There is no static/gateway controller carrier.
   - **8c, local USB/BLE delivery:** before production output lands, extend
     `ios-companion/INBOX_SYNC_CONTRACT.md` and its executed gate; then add scoped USB output, structured BLE
     output/re-offer/local ACK, console-stage-drop refusal, and every local pressure/failure result. The
     companion receives plaintext only.
9. **Legacy protocol deletion and durable protocol docs:** delete every protocol/command legacy item in §17,
   excluding only the main-NV/`Node` fields assigned to Slice 10; this includes
   `send_remote_cmd` / `send_remote_response`, the legacy bodies now hidden behind the Slice-1b entry points,
   and `MR_FEAT_REMOTE_MGMT`. Keep the capability-owned v2 handlers. Update `docs/frames.md`,
   `docs/protocol.md`, help/manual, and historical cross-references. The ruled 36-stream corpus has no remote
   traffic, so replacement is expected corpus-inert and native/wiring tests own the behavioural proof.
10. **Main-NV cleanup (standalone R-RA-6 slice):** only after `/mracl`, `/mradmid`, `/mrmkeys` and `/mrtargets`
    are live, remove the legacy main-config `admin_pubkey`, `admin_provisioned`, `admin_counter_floor` and
    matching `Node` state in one separately measured NV-version change. Do not combine it with legacy
    protocol deletion, companion work, or documentation cleanup.

### 19.1 Per-slice gate ownership

Every slice brief must name five things explicitly: native cases, mutation target(s) mapped to each touched
decision file, corpus expectation against the current ruled 36-row table, the ruled `gateway` +
`heltec_mobile` board pair (with the warning-census pinned-set exception), and metal residue. “No traffic in
the corpus” never substitutes for a native case. The table intentionally specifies coverage shape and file
ownership rather than invented test names: each brief derives its concrete native cases and case counts from
the implementation seams visible when that slice dispatches. The minimum map is:

| Slice | Native + mutation file ownership | Corpus and board gate | Metal residue |
|---|---|---|---|
| 0a | help dispatch, `src/firmware_commands.cpp` | semantic identity; ruled pair unconditionally | none |
| 0b | supplied-sink `regen`, `src/firmware_config.cpp` + caller seam | semantic identity; ruled pair | none |
| 0c | dispatcher/sinks, `src/fw_main.cpp`, `src/firmware_commands.cpp`, sink headers | semantic identity; ruled pair | none |
| 0d | ✅ landed: four home-bound arms, `lib/core/node_hashlocate.cpp` | predicted 0 movers; measured 36/36 byte-identical; no re-anchor; ruled pair RAM +0 | none |
| 0e | ✅ measured in isolated worktree: generated inventory, ABI/cap/timing probes under `tools/` + fixtures under `test/` | 36/36 unchanged; host/ARM/Xtensa ABI and ruled pair; integration package pending | none |
| 1 | `lib/core/mr_features.h` plus legacy compile owners | 36/36 unchanged; both endpoint-disabled builds and ruled pair | none |
| 1b | capability-owned pre-tail handlers, `lib/core/node_mac_rx.cpp` | prediction-first 36/36 identity; four role-by-type native arms; ruled pair | none |
| 2 | remote codec/KDF files and carrier-cap authority | zero remote events, 36/36 unchanged; ruled pair | none |
| 3 | target identity/ACL storage and USB provisioning owners | zero remote events, 36/36 unchanged; ruled pair | **Bench Part 55a:** target-side physical-USB first owner and local recovery only |
| 4 | mobile keyring/target-book storage and local command owners | zero remote events, 36/36 unchanged; ruled pair | **Bench Part 55b:** controller `/mrtargets` exchange with the Part-55a target; **Part 56:** USB seed lifecycle and BLE public select/show only |
| 5 | target session/dedup files | zero remote events, 36/36 unchanged; ruled pair | none |
| 6 | common dispatcher/context/authority-table consumers | zero remote events; all pre-existing local behaviour attributed; ruled pair | none |
| 7a | target cfg/NV schema and validation | zero remote events, 36/36 unchanged; ruled pair with isolated NV attribution | **Part 57a:** cfg migration/reboot; exact `cfg remote_action_activation_ms=<N>` value persists and invalid bounds refuse |
| 7b | transcript, scheduler and deferred-action owners | zero remote events, 36/36 unchanged; ruled pair | **Part 57b:** exact scheduled-terminal line carries request ID and activation delay before the action occurs |
| 8a | mobile controller state/crypto files | no carrier, 36/36 unchanged; ruled pair | none |
| 8b | `node_mac*` / hash-routing carrier and B278 consumer | zero A0/A1 corpus reach expected; any other DATA delta is STOP; ruled pair | **Part 57c:** real mobile→home→target request/result line with request ID; optional ACK/custody fields agree with the selected option |
| 8c | USB/BLE renderer/router and companion contract gate | corpus cannot prove direct JSON; executed golden wiring gates; ruled pair | **Part 57d:** exact contract NDJSON re-offer/ACK plus USB result or stage-drop line |
| 9 | protocol/command legacy owners named in §17 (NV fields excluded) | 36/36 unchanged; native fail-closed replacement and both probes; ruled pair | **Part 57e:** legacy help entry absent and the common dispatcher’s exact unknown-command refusal for `rcmd` |
| 10 | main config/NV + `Node` legacy fields only | 36/36 unchanged; ABI/NV migration and ruled pair | **Part 57f:** boot migration line reports the new schema healthy, legacy fields absent, and all four replacement stores retained |

⚠ **CORRECTED 2026-09-04, prior gate claims kept visible:** the old 0a row said the ruled pair ran only “if
production bytes move”; it is now unconditional. Before D-0d-1 the 0d row said “three home arms”; the current
four-arm scope is authoritative. The old Part 55 assigned `/mrtargets` exchange to Slice 3,
before that controller store exists; it is split into target-side 55a and controller-side 55b. The old table
assigned one undifferentiated “Part 57” to Slices 7a through 10; Parts 57a-f now give every owning slice an
executable observation and expected console/contract line.

### 19.2 Required controller-boundary controls

The slice plans must distribute, but not omit, these end-to-end and mutation controls:

- an omitted selector uses the controller node's current `self` identity; `using=key2` derives key2's seed
  and authenticates as key2 even when its local slot number differs from the target ACL slot;
- replacing selected-key derivation with `self`, a different key slot, or current mutable selector state
  turns the tests red; retry and response opening use the identity captured by the pending request;
- independent KATs define `selected_key32` as the message's actual base/session sealing key, prove a
  bootstrap request has no epoch input while bootstrap/rollover responses do, and prove distinct
  `SOURCE_HASH` values separate nonces for two controllers sharing one management seed; cloning both that
  seed and `/mrid` is rejected as outside the supported identity model rather than laundered by fingerprint
  detection;
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
- every authenticated and open v2 request/response carrier contains `SOURCE_HASH` through the
  `app_dm=true` path; restoring either legacy `send_remote_*` helper, dropping the field to gain capacity, or
  claiming it is ciphertext turns the controls red;
- production-shaped custody notices about an `AUTH_EXECUTE` request and an `OUTPUT` response remain
  `custody_failure`: neither can satisfy response assembly, an RPC terminal or authentication failure; a
  mobile pending request correlates only through B278's complete translated six-field tuple, retains its exact
  request bytes, reports target outcome unknown, and initiates zero TX solely because the notice arrived;
- the v2 direction handlers remain explicit and pre-tail: deleting/moving the `REMOTE_CMD` handler on an
  accept-enabled build or the `REMOTE_RESP` handler on a client-enabled build turns a real addressed RPC into
  `unsupported_internal` and makes the control RED; on an accept-disabled mobile an addressed request must
  produce exactly that bounded guard-drop with zero staging, dispatch or inbox text while a real response is
  still client-consumed;
- filling the current one-slot legacy ingress and presenting a second request proves the shipped
  `remote_inbound_drop_full` refusal (never overwrite); the v2 bounded-ingress tests then pin their own
  reserve-before-dispatch and full-capacity refusal semantics without claiming legacy pipelining;
- the mobile carrier test uses distinct `ctrM` and `ctrH`: a static-leg custody notice first terminates at the
  home, then reaches exactly the originating mobile only through the complete delegated-correlation mapping;
  wrong mobile/hash/destination/counter, a counter-only match and an expired mapping cannot move any controller
  pending request or fabricate a translated report;
- the same carrier is exercised with and without `-a`: default/no-option creates no delegated-correlation
  row and receives no B278 report, while `-a` creates one row and permits the translated report; parsing `-e`
  as ACK, forcing ACK for every RPC, or letting more than eight ACK-bearing delegated sends evade the shared
  ring turns the controls red;
- remote syntax is exercised as the exact XOR contract: authenticated `remote <t> -e ... -- <cmd>` and
  clear `remote <t> open -- status|routes` accept, while a line with neither `open` nor `-e` and `open -e`
  both refuse. A mutation making authenticated `-e` optional, accepting both statements, or coupling `-a`
  to the security choice turns the controls red;
- BLE can select an installed identity but cannot generate/import/export/remove one; neither `remote` nor
  `admin-key` exists in an accept-build command inventory (a structural absence gate), and the behavioural
  twin proves that an accept build receiving `remote ...` or `admin-key ...` as RPC command text returns
  `unknown_command` even with owner authority;
- first-owner provisioning and target-administration-identity generation/recovery/rotation are USB-only;
  merely adding a secured BLE caller without a physical-presence authority reddens the structural gate;
- each mobile wrapper, home-originated static leg, target response, hosted-mobile last mile, and cross-layer
  variant asks `remote_body_cap(carrier)` for its own RPC-body cap; mutations restoring universal 214/213
  claims, allowing field drop to fit, setting outer `CRYPTED`, or using `Plane::AUTO`/team plane turn the
  controls red; all three home-delegation arms stamp `GLOBAL`, and a mixed team/static ID cannot alter it;
- discovery's full controller public key appears only in its occasional 57-byte body;
- the normal companion interface accepts plaintext command components and emits scoped plaintext results;
  mutations that move key use, sealing, raw authenticated bodies, or response decryption into that interface
  fail structural checks;
- the USB/BLE surface uses the companion contract's existing `{"ev":...}` / `{"ack":...}` conventions and
  byte-exact renderer/dispatcher probes; changing only one transport's spelling or accepting an early/wrong
  request ID turns the controls red;
- a BLE disconnect, missing/wrong/early local ACK, and full retained-result pool never produce a target
  `RESPONSE_ACK` or live-result eviction; reconnect re-offers the same complete result, and the correct local
  ACK causes exactly one target ACK;
- a USB console-stage drop likewise does not acknowledge the target; successful complete admission does;
- ACK loss creates bounded retryable debt; safe rollover refuses with `session_busy` while an unacknowledged
  transcript exists, force rollover requires explicit local confirmation, and mutations making either path
  silently abandon an outcome turn red;
- `session_full` automatically initiates safe rollover; the measured table size `N` and rollover cadence are
  pinned, while an unacknowledged transcript still produces `session_busy` and never automatic force;
- an exact retry after its response was acknowledged returns authenticated
  `PROTOCOL_ERROR{already_acknowledged}` without dispatch; tag failure stays silent and an open protocol
  error is emitted only for a structurally valid bounded open request;
- activation config at floor-minus-one and ceiling-plus-one refuses; exactly at both bounds accepts; changing
  any source timing input changes the derived floor/default; replacing the one
  `cascade_requeue_base_ms` term with the 30-second backoff cap or `send_defer_ttl_ms` turns the control red;
  the `scheduled` terminal is retained and enters the first-hop reply path before either ACK-triggered or
  fallback activation; and the outer 300-second expiry reports unknown without activating, cancelling, or
  extending an action;
- saturating open/bootstrap peer and global quotas cannot consume the reserved authenticated owner/control
  admission, while saturating authenticated work cannot bypass the separately bounded open rate/resource
  limits; mutations deleting either partition boundary turn the controls red;
- controller reboot or an unsolicited response cannot cause trial-decryption across stored keys or automatic
  replay of a mutating command;
- two controller nodes sharing one seed interoperate as one ACL principal, while two different keys remain
  independently revocable/session-scoped; and
- the generated command inventory refuses missing/duplicate classifications, and changing a production verb
  or sub-verb without regenerating the owner-reviewed table turns the gate red;
- embedded NUL/CR/LF, cap-plus-one and silent truncation are refused identically by USB, BLE and remote
  command admission; and
- mobile builds originate/decrypt through client while every target-accept entry point remains absent/inert;
  static/gateway builds accept while all client keyring, pending-request and response-consumer paths are
  absent/inert; ordinary intermediate DATA transport remains intact in both directions without a transport
  feature flag.

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
15. Exactly two endpoint capabilities exist: mobile builds set `MR_FEAT_RADMIN_CLIENT=1` and
    `MR_FEAT_RADMIN_ACCEPT=0`; static/gateway builds set the reverse. Transit is ordinary type-agnostic DATA
    forwarding and has no remote-administration flag.
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
28. The only controller topology is a mobile build reached locally over USB or secured BLE. Therefore the
    mobile-delegated carrier is product scope, not an optional adapter, and [[B278]]'s exact home-to-mobile
    custody-outcome translation — ⚠ **updated 2026-09-03: previously written as a prerequisite, it is now
    LANDED** (software-complete; metal Part 54 pending) — supplies that evidence. Claiming custody-aware
    mobile request progress therefore depends on §14.1's consumption rules, not on further B278 work.
29. **R-RA-1:** command/subcommand completeness comes from a pinned source-derived generator; minimum
    open/operator/owner classification is a separate owner ruling over that complete generated table.
30. **R-RA-2:** controller and target state records are first expressed as value types and measured through
    the ABI probe on host, ARM and Xtensa. Capacities and timer strategy are ratified from those measurements;
    the design does not spend a nonexistent 92nd timer ID by assumption.
31. **R-RA-3:** `remote_body_cap(carrier)` is the one packer-derived capacity authority, with at-cap and
    cap-plus-one tests for every request/response carrier. The 214/213 figures remain examples only.
32. **R-RA-4:** §8.1's control values, little-endian fields, exact KDF labels/input order, nonce domains, and
    AAD layout are frozen design inputs and receive independent-reference known-answer vectors.
33. **R-RA-5:** every request ID is a cryptographically random 64-bit value. It is never shortened or
    replaced by a rebooting counter and has no artificial per-epoch request cap; at `2^16` requests in one
    epoch its collision probability is approximately `2^-33`.
34. **R-RA-6:** `/mrmkeys`, `/mracl`, `/mradmid`, and `/mrtargets` use the versioned fixed-slot,
    transactional, exact-validation and secret-wipe idiom established by the team keyring. Legacy main-NV
    fields and `Node` mirrors are removed only in a separate attributable NV-version slice.
35. **R-RA-7:** one shared validator rejects embedded NUL/CR/LF and enforces one derived command-byte cap for
    USB, secured BLE and remote dispatch. No path truncates or silently splits an invalid command.
36. **R-RA-8:** static nodes and gateways are managed-only; mobile nodes manage-only. The mobile-delegated
    carrier is the first and only controller carrier, and [[B112]] is its admission-truthfulness prerequisite.
37. **R-RA-9:** disruptive actions promise a named, source-derived activation delay, while the controller's
    outer outcome bound reuses the named 300-second cross-layer E2E-ACK deadline.
38. **R-RA-10:** authenticated owner/control capacity is partitioned from open/bootstrap work so an open
    flood cannot starve recovery. Exact quantities follow R-RA-2's measured capacity ruling.
39. **R-RA-11:** local remote-admin request/result/ack framing extends the companion contract's existing
    plaintext NDJSON event/ack conventions and is executed by a console-sink-style wiring gate; no second
    framing language is introduced.
40. **R-RA-12:** a mobile home is only a static node and never a gateway. Each `send_by_hash` wrapper arm
    toward that home stamps `Plane::GLOBAL`; the current hard-coded `AUTO` behaviour is corrected in its own
    attributable preparatory slice.
41. **R-RA-13:** every v2 request and response carrier contains mandatory clear-inner `SOURCE_HASH` through
    the `send_by_hash` / `do_send` `app_dm=true` path. The field is AEAD-bound and the surrounding RPC body is
    encrypted; the legacy hash-less helpers are deleted.
42. **R-RA-14:** `common_command_max_bytes` and its shared validator are universal across USB, secured BLE and
    RPC. The legacy 56/63-byte silent truncation is a defect to remove, not compatibility evidence or a cap.
43. **R-RA-15:** the carrier E2E ACK is optional per RPC request and defaults off. The local wrapper reuses
    `-a` for it (`-e` remains encryption). Without it the response is receipt and no B278 row/feedback exists;
    with it, the request pays one return flight and one of eight shared correlation rows for up to 300 s.
44. **R-RA-16:** disruptive-action activation defaults to twice the source-derived worst-case scheduled
    reply-path budget, is target-configurable from one such budget through strictly less than 300 s, and
    refuses configurations whose promised interval is impossible. Its persisted cfg field is separately
    attributable.
45. **R-RA-17:** every board profile is exactly client-only mobile or accept-only static/gateway. Native is a
    non-product `{client=1,accept=1}` test build, with product exclusivity asserts scoped to board profiles.
46. **R-RA-18:** every local `remote` line states exactly one security mode. `open` is cleartext and restricted
    to exact `status|routes`; authenticated remote administration requires `-e`. `open -e` and a line with
    neither mode refuse. The independent `-a` E2E-ACK option remains optional and default-off.
47. **R-RA-19:** Slice 1b, immediately after the feature scaffold, owns the behaviour-neutral split of the
    legacy pre-tail receive staging into accept-owned `REMOTE_CMD` and client-owned `REMOTE_RESP` entry points.
    Disabled roles reach the existing fail-closed tail; semantic slices fill the bodies later.
48. **R-RA-20:** disruptive-action activation uses the exact first-hop formula in §13: configured-PHY airtime
    for RTS/CTS/maximum scheduled-terminal DATA/ACK, the CTS→DATA gap, bounded busy retry, MAC CTS/ACK waits,
    and one 5-second first requeue. The floor is one budget, the owner's default is twice it, and the ceiling
    remains one millisecond below the 300-second outer bound.

### 20.2 Derived artefacts and later measurement rulings

The owner-decision list through round 2 is closed by R-RA-1..R-RA-20. What remains before the relevant
implementation slices may land is evidence and generated authority, not permission to reopen those product
choices:

- generate the complete production command/subcommand inventory, then obtain the separate owner ruling that
  assigns each row exactly one open/operator/owner/physical minimum authority (R-RA-1);
- measure candidate controller pending/session/result and target seen/transcript/ingress value types on host,
  ARM and Xtensa, including aggregate caps, gateway RAM headroom and the timer-wheel strategy; then obtain the
  exact capacity/partition/rate-limit ruling (R-RA-2/R-RA-10);
- derive and pin `remote_body_cap(carrier)` against every real packer and publish each carrier's limit,
  stable-source requirement, routing metadata, and refusal outcome (R-RA-3);
- implement the frozen §8.1 table and domains with independent-reference known-answer vectors, including the
  `2^16`-request random-64 collision calculation and nonce-separation controls (R-RA-4/R-RA-5);
- specify and ABI-measure the exact versioned `/mracl`, `/mrmkeys`, `/mradmid`, and `/mrtargets` records in the
  fixed-slot transaction idiom, followed later by the separate main-NV cleanup slice (R-RA-6);
- derive `common_command_max_bytes` from the characterized USB, BLE and carrier boundaries, then pin identical
  NUL/CR/LF and length rejection at every dispatcher entry (R-RA-7);
- derive `remote_scheduled_reply_path_budget_ms(cfg)` term-by-term from the exact §13 first-hop formula,
  including the named MAC CTS/ACK windows and one first-price requeue; prove that the current
  default/floor/ceiling interval is feasible, then land the production authority and configurable field in
  Slice 7a's own NV change (R-RA-16/R-RA-20);
- extend `ios-companion/INBOX_SYNC_CONTRACT.md` with byte-exact request/output/terminal/re-offer/ack lines and
  execute them through the production renderer/router probes (R-RA-11);
- fix [[B112]] before the first mobile controller carrier claims honest admission; and
- complete B278's Part 54 metal check. Its landed 300-second mapping, translated 32-byte `0x81` record,
  no-map behaviour and late-ACK monotonicity already govern §14.1 and are not redesigned here.

None of those derived artefacts changes the settled independent-request model, three-level authority,
mobile-only client/static-gateway-only accept split, node-owned key/crypto boundary, four-store trust split,
legacy removal, random 64-bit request identity, optional-default-off carrier ACK, static-only home invariant,
mandatory source hash, universal command validator, authenticated-remote `-e` requirement, capability-owned
pre-tail handlers, or carrier-specific first-hop budget rule.
