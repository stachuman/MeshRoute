<!-- Author: OpenAI Codex; owner review draft -->
# Remote administration v2 — compact independent RPC (revised proposal)

**Status: DESIGN PASS 2026-09-04 — IMPLEMENTATION AUTHORITY; round-1 findings A1-A8/B1-B6/C1-C14,
round-2 findings H1-H3/F1-F11, and owner rulings R-RA-1..R-RA-24 incorporated. Slices proceed
independently through §19; none is authorized until its own brief passes. Slices 0d and 0f have landed; Slice 0e
has completed measurement in its isolated worktree with its exact coder-owned integration package still pending.**

**Implementation update 2026-09-15:** 7b-1 is owner-committed at `1d4b3ad`, 7b-2-0 at `564f460`,
7b-2 at `f993191`, and the separate **7b-3-0 codec at `ac5f9a5`**. The committed
[codec independent QA gate](../evidence/2026-09-13-radmin-slice7b3-0-qa-gate.md) reports native 2912/184461/0,
reference 94/94, domain 9787, corpus 36/36, tools 343/no skips, all probes/ABI/checkers/census, unchanged
board pair, and 816 RED + known unusable B342 /817 configured. **B391 is closed**: X09's code-boundary repair
was independently effective. The pre-comment 7b-2 union remains historical; its B388 closure is unchanged.
The codec reviewer reproduced section/object/linked totals but not the coder's single-byte ELF attribution.

**Current dispatch: [7b-3 revision 4](../plans/2026-09-13-radmin-slice7b3-deferred-actions.md), reissued at
`ac5f9a5`, ready for source-validation; behavior HOLD B394 only — B389 RULED R-RA-40 (2026-09-15).** Twelve selected policy rows require separate
[7b-3-P1 preparation](../plans/2026-09-15-radmin-slice7b3-p1-simple-action-preparation.md), full gate/owner commit,
then a refreshed behavior base. B394 corrects the earlier twelve-existing-seams summary: four reusable
effects, eight needing extraction. QA records 36 exact R-RA-39 refusals/fenced follow-ups in brief §2.2
(B395 config/gateway 23; B396 join/create/leave 10; B397 team 2; B398 regen 1). They remain incomplete.
R-RA-37/38/39 are settled: one promise/one row/typed action_busy, remote prep-restart with lockout warning,
and the named-family fallback. B389's new complete twelve-row model is **+80 B**, Node **230976 native /
157344 gateway /117912 mobile**; allocation/re-pins remain unruled, linked RAM unmeasured. Fresh author
pre-check: native 2912/184461/0, rebuilt corpus 36/36 byte-identical, reference 94/94, all 52/817 patterns
match once and three-ABI model compiles. No new full implementation gate or mutation union this author turn.
B390 runtime and B392 metal/controller closure remain open; next free finding B399.

This revision incorporates the owner's decisions through 2026-09-04. It does not modify firmware behaviour
and remains subject to independent review. If ratified, it replaces the implementation direction in
`2026-07-26-remote-admin-challenge-response-design.md`; that document remains a historical decision record,
not a compatibility requirement. The completed v2 implementation must remove the current `rcmd` mechanism
rather than support both protocols indefinitely. The verbatim owner record for this revision is
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md` (R-RA-1..R-RA-24).

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
command/output limits are carrier-specific rather than falsely inheriting the former 239-byte direct-DM
ceiling; R-RA-25's one application-DM admission authority is now 232 bytes.
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
- ⚠ **CORRECTED 2026-09-05 by R-RA-25; prior 239-byte authority kept visible:** the supported normal-DM body
  ceiling was the deliberately conservative `dm_max_body_bytes = 239`, but executable Fable pass 2 proved that
  `enqueue_data()` then silently discarded `SOURCE_HASH` for bodies 233..236 and both known hash fields for
  237..239; a delegated mobile wrapper could instead be stored in its home node's inbox or be queued with an
  empty inner. The one application-DM BODY authority is therefore **232 bytes**, derived as
  `241 - DST_HASH(4) - origin(1) - SOURCE_HASH(4)`. Every `app_dm=true` carrier includes `SOURCE_HASH`; a supplied
  or derivable destination hash is also mandatory and may never be dropped for size. A truly unbound by-ID send
  may omit the unknown `DST_HASH`, but receives no larger cap. Slice 0h landed and gated the behaviour change before
  any v2 carrier consumes this path.
- `DATA_TYPE_REMOTE_CMD = 0xA0` and `DATA_TYPE_REMOTE_RESP = 0xA1` already carry remote request/response bodies
  (⛔ corrected 2026-08-29: ordinals 6/7 were RETIRED by the §CUSTODY-A namespace transition — the values now sit
  in the internal range's administration/security block `0xA0..`. Reusability is unchanged; no third DATA type is
  needed. Both are protocol-internal (`0x80..0xBF`), so `data_type_traits()` reports them
  `internal=true, generic_send_lifecycle=false` — the RPC's own response/timeout contract is the only outcome they
  carry, and no generic `send_acked`/`send_failed` may be raised for them.)
  (`lib/core/frame_codec.h`, `lib/core/node_mac.cpp`). They are reusable; no third DATA type is needed.
- At the Slice 1 closure commit `5d2c00e`, the addressed receive path consumes both remote types in an explicit
  handler at `lib/core/node_mac_rx.cpp:2220`, before the §CUSTODY-B protocol-internal tail guard at `:2427`.
  The pre-tail placement preserves owned handling, not legacy role compatibility: v2 must replace it with
  explicit handlers owned by direction/capability — `REMOTE_CMD` by **accept**, `REMOTE_RESP` by **client**. A build lacking the owner
  for an addressed type deliberately reaches the bounded `unsupported_internal` guard-drop. In particular,
  an accept-disabled mobile drops an addressed REQUEST but its enabled client still consumes a RESPONSE.
  Removing staging without installing those v2 handlers would make addressed RPCs disappear fail-closed but
  operationally invisible.
- A current sealed request body costs 35 bytes before command text:
  `[sealed_flag 1][rand8 8][nonce_ctr 2][node_hash 4][replay_counter 4][tag 16]`. Under the supported
  239-byte ceiling, the envelope arithmetic left 204 bytes. ⚠ **POST-0h CORRECTION 2026-09-05:** 239 is the
  retained pre-R-RA-25 figure; the 232-byte authority would leave 197 bytes. Neither is a shipped legacy command
  allowance. ⚠ **CORRECTED 2026-09-04:** the shipped
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

### 6.3.1 Slice 4 storage/console contract — software-complete, QA PASS 2026-09-07

**Software-complete / METAL-PENDING.** Independent QA reproduced every instrument; implementation and
evidence are committed in 19c10bf. Evidence: docs/superpowers/evidence/2026-09-06-radmin-slice4.md.
The following Author decisions are implemented, not an advance-draft prediction. Full contract/output bytes:
`docs/superpowers/plans/2026-09-06-radmin-slice4-controller-stores.md` §§1/4–8.

Author resolutions of pre-check §6.3–6.7:

- `/mrmkeys`: version 1, magic `0x4D524D4B`, 8-byte header plus ten 36-byte seed/reserved rows = 368 bytes,
  alignment 4. Guarded transient secrets only, independent of target ACL capacity; occupied slots cannot
  be replaced by generate/import. Duplicate derived public identities versus self/other slots refuse.
- `/mrtargets`: version 1, magic `0x4D525442`, 8-byte header plus 32 × 64-byte rows = 2056 bytes,
  alignment 4. Row offsets: public key 0, routing hash 32, four hop bytes 36, hop count 40, label length
  41, 16-byte label 42, flags 58, five reserved bytes 59. Occupied flag bit 0 only; canonical empty rows
  and reserved/unused bytes zero. Full key immutable/unique; label/routing hints mutable, no eviction or
  peer-store sharing. Hints contain 0 or 1–3 destination layers, excluding the origin the carrier prepends;
  the four-byte array does not admit four destinations. GLOBAL implicit, never a stored plane override.
- One CLIENT-only resident public book scratch, not a live authority cache or a second stack-sized
  candidate. Reload/classify on access and invalidate failed candidates; no resident management secret
  or Node state. Measure composed stack and actual RAM/NV budgets; the 32-row STOP in §6.3 is unchanged.
- `admin-key` retains §6.2's operations plus explicit invalid-only `reset confirm`; `admin-target`
  owns list/show/add/set/remove/reset, with label= or fp= selectors resolving the stored full key.
  Eight-physical-slot listing pages bound the USB stage; all public listings include full public key and
  R-RA-29 fingerprint. No control-plane hash is a trust selector. Explicit confirm-only invalid recovery;
  unreadable IO refuses every write, including reset. Real partial-write/self-heal limits remain B317,
  not an atomicity guarantee inferred from the transaction API.
- R-RA-30 settles public list/show over secured BLE and USB-only secret/book mutations. A separate
  client envelope `{"err":"admin-client","msg":"console_only"}` refuses non-list/show forms before
  the transport-neutral seam; the target's whole-family `admin` envelope remains unchanged.
- The real-router gate gains a second independently compiled mobile-role arm; neither arm includes the
  real boards' OLED define (B328), so this is not full board-define parity. Inventory gets a typed literal CLIENT
  column, one on mobile/mobile_oled and zero on all four static/gateway profiles. Extend Slice 3's
  delivered census, never relearn it from the edited source. B320 is closed after QA corrected the
  pre-check's contrary full_* host claim in place. B321 fences scope-wiping the shared hex decoder's
  scratch for seed import: direct monocypher.h include and a function-local guard calling crypto_wipe
  inside parse_hex32, without a grammar change. Do not import the team-keyring/NV layer into the pure
  parser or relocate the shared guard (C1). Any resulting non-client flash cost is attributed explicitly.
- Client successful regen adds exactly `> regen note old self ACL grants do not follow the new key; dedicated keys and targets preserved`
  after its existing success/name line, on the supplied sink only. Debt refusal is exactly
  `> regen err remote_busy`, before draws/writes. Future-caller predicates are tested but have no live
  producer before Slice 8a; no remote request/result implementation is implied. ACCEPT output unchanged.
- R-RA-30 requires one-off xiao_mobile fixed-path/fixed-identity base/final RAM/flash and stack attribution,
  not a third ruled board. Native/Xtensa/ARM record assertions and the two ruled boards remain required.
  Bench Parts 55b/56 and Part 59's conditional mobile output extension are software-bound, not run.

**Closure 2026-09-07:** native 2763/118344/0; mutation union 296 RED / 0 unusable; six probes, 329 tools
tests and 204-row inventory pass. Both client ABIs pay exactly 2056 RAM bytes for the one book. Gateway
195844 RAM / 531004 flash (+0/+32, B321 wipe only); heltec_mobile 207740 / 1367448 (+2056/+12156);
one-off xiao_mobile 172572 / 664636 (+2056/+86992). Record assertions and Node ABI pins unchanged;
36/36 anchored streams, s18 unchanged, simulator byte-identical with zero post-edit actions.
B321 and B327 closed; B328/B329 retain instrument scope obligations. B330 tracks the nRF52 client flash
cost (82.0% application-region use) for a separate size-control pre-check, not a Slice 4 gate failure or
Slice 5 optimization. No remote issuer, session, live debt producer or metal result is claimed.

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

**CORRECTED 2026-09-06, R-RA-29; earlier deferral above retained visibly:** the fingerprint is frozen NOW,
not left to the dispatcher: first eight digest bytes of BLAKE2b-512 over the full 32-byte Ed25519 public
key, rendered in digest order as 16 lowercase hex characters. One implementation serves v2 key display/
selection across ACL, both directions of USB provisioning, controller keyring and target book. USB listings
include the full 64-hex public key beside it. It is not `key_hash32`, a raw prefix, or an independent trust
anchor. Slice 3 owns an independent reference KAT; the controller surfaces remain Slice 4 work.
R-RA-29 also freezes the entire Slice 3 target family as USB-only: ACL listing/mutations/recovery and
administration-identity show/generate/rotate/recovery. The ACCEPT-build BLE guard refuses the whole family
before the transport-neutral seam; client-only builds have no target handlers. No listing exception or
controller BLE decision is introduced here.

### 6.6.1 Slice 3 implementation boundary — Author preparation, awaiting QA

The Slice 3 pre-check's remaining §6 decisions are resolved in
`docs/superpowers/plans/2026-09-06-radmin-slice3-target-stores.md` (DRAFT; not implementation):

- No resident target Identity, seed, ACL cache or Node state. Pure store services and stateless device
  adapters load per operation, wipe secret-bearing temporaries, and report boot state without installing
  anything or writing. No static I/O buffer is budgeted; measure automatic stack demand and both boards.
- `/mradmid` v1 is a 40-byte value record (MRA1, version, named reserved field, seed); `/mracl` v1 is
  368 bytes (MRL1, version, count, ten stable 36-byte rows). Exact validation, named padding, layout/offset
  asserts on each ABI, four-state reads, candidate validation and at most one save follow the team-keyring
  idiom. The ten slots are statically tied to the codec's 0..9 handles. No main-Blob version/legacy cleanup.
- New primary names are `admin-id` and `acl`. Generation is absent-only; rotate requires an existing valid
  identity and exact confirm. Explicit reset-confirm recovers invalid records only; ordinary operations
  never overwrite invalid records, and io_failed forbids EVERY write, including recovery. ACL reset yields
  a valid empty record, not an owner. First owner requires a separately valid durable target identity;
  failed/partial provisioning never claims both records committed atomically. A valid ACL's lost credential
  can be replaced through physical add-owner followed by removal; no implicit corruption recovery.
- Seed generation uses a checked caller-supplied source and refuses absent/false/partial-failure/zero
  material. The existing device draw's nonzero check is not RNG-health qualification; B312 remains open
  for the first RF consumer's truthful entropy integration. No provider/HAL rewrite belongs here.
- The §6.5 failed-save statement is a live-state guarantee, not an assertion of atomic flash replacement.
  Slice 3 has no resident live cache. nRF52 `write_slot` removes before replacement; failure or a power cut
  can leave absent/invalid/changed media, so subsequent operations re-read and report truthfully. Do not
  say nothing was written. The new stores receive their own Part 55a metal checks, not B193's old credit.
- **Preservation limit (B317):** ordinary regenerate/leave/configuration preserve the separate stores,
  and factory reset erases them; however existing nRF52 filesystem self-heal can also erase them after a
  failed mount or corruption in its fixed probe list. Do not add these optional stores to that list or
  claim §6.3 preservation across a destructive whole-filesystem repair. Mark the limitation in new code;
  changing self-heal/persistence atomicity requires a separate pre-check, scope and gate.

The local grammar/output contract, literal feature-site multiset, executed real-router gate, extracted BLE
guard, inventory/help projection and two mutation selectors are pinned in the brief. The controller half
of the exchange is not implemented by displaying its supplied key on the target. Part 55a is target-side
only; Part 55b remains the controller slice. This preparation makes no software-complete or metal claim.

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

### 7.4 Slice 5 Author preparation — pre-transcript state and bootstrap (2026-09-06)

**Preliminary brief QA PASS 2026-09-06, no fold-ins; not a software completion or measured ABI result.** R-RA-31 permits target
bootstrap responses on air in Slice 5 (same-layer by hash and reversed cross-layer path) and authorizes
only native/gateway Node re-pins. Execute/output/terminal/ACK/rollover replies remain Slice 7b's.
The advance brief is `docs/superpowers/plans/2026-09-06-radmin-slice5-target-session.md`; its
delivered results are filled from the QA-passed Slice 4 implementation at 19c10bf; the final dispatch
hash waits for the owner's landing/preparation commit and QA's final brief gate. QA accepted the
S5-A1–S5-A3 corrections and corrected its ledger visibly; the rows remain open until Slice 5 closure.
Author decisions resolving pre-check §6.3–§6.7:

- One ACCEPT-only state block holds the administration pair, live ACL, ten epochs, seen entries,
  partitioned ingress and open/bootstrap staging. Core reads no NV. Firmware uses prepared, fallible
  pre-save installation plans and non-failing post-durable commit; failed preparation/save preserves
  the old live authority/session state. Unchanged ACL slots stay intact; root rotation invalidates all
  old sessions, ordinary messaging regen does not. No unconditional epoch draw for an unprovisioned
  simulator Node; no epoch-zero readiness or claim that a nonzero void-HAL draw proves RNG health (B312).
- The 48-byte seen record contains the exact 16-byte authenticated tag. Its eight-byte retained route
  makes each complete entry 56 bytes; N=16 entries TOTAL shared across slots, not 16 per slot. A single
  slot can occupy at most 16; concurrent slots reduce available capacity and the future rollover cadence
  is not a guaranteed 16 executes per credential. Seen fingerprints survive staging expiry and future
  ACK release; only epoch-invalidating changes can clear those current-key replay tombstones.
- The explicit candidate is 2064 bytes: pair 64, ACL 340, readiness/status 4, epochs 80, seen 896,
  ingress headers 80, ingress bodies 472 and open/bootstrap staging 128. All sizes/offsets and actual
  Node/board placement must be measured. Four open/bootstrap rows split three open plus one bootstrap,
  with one-open-per-source and no borrowing authenticated or bootstrap reservation. The two authenticated
  pairs retain owner/control reservation. Bootstrap consumes no seen execute record and rotates no epoch.
- The legacy Node slot/helper and firmware drain become CLIENT-only. Target legacy command execution
  ends here; client legacy response printing stays until 8a. Native has both capabilities and retains
  that slot; gateway loses it. Account separately for any firmware drain scratch present in the base.
- One shared timer, ID 91, wheel kCap 92. Staging uses the named existing cross-layer transport horizon
  as its pre-dispatch holding ceiling; this is not a seen/session timeout or execution deadline. Exact-
  edge expiry cancels/re-arms the bounded earliest deadline. The wheel is in DeviceHal, not Node: mobile
  Node stays pinned while HAL RAM pays the global wheel increase (0e prices +8), with board padding and
  possible timer-code flash changes attributed. No new timer ID for any class or later slice.
- The brief names prepared-runtime boot/refusal lines and the real-router/state wiring gate. Simulator
  gains one shared core source-list entry; executable changes, all 36 old streams must remain exact.
  S5-A1–A3 are now B331–B333 with aliases retained; next free B336, subject to register recheck.
  No new bench part; update the existing legacy round-trip suspension after the owning slice passes.

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
| `REMOTE_RESP` | `0x5` | `ADMISSION_RESULT` — R-RA-36, codec and 7b-2 target producer QA-passed |

All other opcode nibbles remain reserved and reject. Values are append-only after the codec/KAT slice.

Slots 0..9 select established authenticated ACL sessions. Nibbles A..E are reserved and reject. Sentinel F
is valid only with `OPEN_EXECUTE`, an open response, or a bootstrap request before the target ACL row is
known. Bootstrap and ADMISSION_RESULT responses carry an actual slot 0..9; ADMISSION_RESULT has no open form.
Open and bootstrap are opcodes, not fake
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
`ROLLOVER_RESULT` domains. For **ADMISSION_RESULT only**, append `request_ctl_u8 || admission_code_u8 ||
detail_u8` after the source term instead; its no-sequence nonce term is zero (R-RA-36). All other domain
preimages remain unchanged. A bootstrap request cannot include an epoch the controller does not yet know;
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
ciphertext or open plaintext starts with one unsigned one-byte result code.

**Original 0x00..0x07 allocation accepted by QA and implemented by QA-passed Slice 2, 2026-09-06:**
those meanings below received values in their existing order. Independent wire vectors pin this accepted
allocation; it is append-only, with no renumbering or reuse of retired values. Slice 2 introduced the typed
codec, integrated at MeshRoute `f2735f7`, simulator `8688884` (§19 item 2); later slices added real consumers.
**R-RA-37 appends action_busy below (7b-3-0 independent QA PASS; owner commit ac5f9a5).** The new
allocation grants no command authority or producer behavior; 7b-3 owns the disruptive-conflict producer.

| `TERMINAL` result byte | Name |
| --- | --- |
| `0x00` | `completed` |
| `0x01` | `scheduled` |
| `0x02` | `unknown_command` |
| `0x03` | `refused` |
| `0x04` | `output_truncated` |
| `0x05` | `internal_error` |
| `0x06` | `session_full` |
| `0x07` | `session_busy` |
| `0x08` | `action_busy` — R-RA-37; codec 7b-3-0 independent QA PASS 2026-09-15, owner commit ac5f9a5; producer pending 7b-3 |

Under R-RA-37, values `0x09..0xFF` are unallocated and must not decode as a known terminal meaning or
accepted success. The 7b-3-0 codec (independent QA PASS 2026-09-15, owner commit `ac5f9a5`) decodes `0x08` as
`action_busy` for both authenticated and open TERMINAL; the producer is 7b-3's. No open disruptive execution is granted. Authenticated
PROTOCOL_ERROR `0x01..0xFF` and ADMISSION_RESULT validation remain unchanged. Existing terminal `0x08`
reference bytes are retained and become positive; new terminal `0x09` negatives replace their former
rejection role, while protocol-error `0x08` remains negative. The assigned meanings remain:

- `completed` — a matching handler returned; its normal text contains any command-specific warning/error;
- `scheduled` — a disruptive action was accepted and deferred until response handling permits it;
- `unknown_command` — no common handler matched;
- `refused` — the authority/transport is not allowed to run this command;
- `output_truncated` — the handler returned, but the bounded transcript could not retain all output;
- `internal_error` — execution or response staging failed before a truthful normal result existed;
- `session_full` and `session_busy` — retained codec meanings for the frozen `0x06/0x07` vectors only.
  **R-RA-36 supersedes their production assignment to TERMINAL:** no-execution capacity refusal and a changing
  safe-rollover busy count use ADMISSION_RESULT below. Old transcript bytes/nonces and decoder recognition
  remain unchanged; new target producers must not emit these state-dependent refusals in TERMINAL.
- `action_busy` — a distinct disruptive request conflicts with the target's single outstanding promise
  (R-RA-37; producer pending 7b-3). It is that request ID's retained immutable final result; an exact retry
  replays it unchanged and never later becomes scheduled. A fresh request ID is needed to try again.

Optional short detail bytes may follow, but ordinary handler output must not be duplicated into a special
terminal encoding. The `scheduled` result is the exception that must carry its bounded activation delay.
A successful command that prints nothing returns a terminal frame at sequence zero.

Authenticated `PROTOCOL_ERROR` is reserved for a structurally valid, authenticated request whose exact
operation is already known but for which no ordinary terminal replay exists—for v2, the concrete case is an
exact retry after that response was already acknowledged, carrying the compact code
`already_acknowledged`. **Slice 2 Author allocation, accepted by QA 2026-09-06:** this code is `0x00` in the
separate authenticated `PROTOCOL_ERROR` result namespace; it is not a terminal result. Its interpretation
requires the authenticated protocol-error opcode/domain, not just the result byte. It uses its distinct
response opcode and therefore does not reuse a `TERMINAL` nonce for different plaintext. Authentication/tag
failures remain silent. The clear/open `PROTOCOL_ERROR` is
limited to structurally valid open-request validation failures that can be answered within the open
rate/resource bounds; malformed or unauthenticated garbage remains a silent drop.

**Slice 2 decoded-result contract (QA fold-in, 2026-09-06):** decoding must retain the opcode domain as a
typed value alongside its domain-specific result; a bare result byte is not a decoded result API. In
particular, terminal `completed` and authenticated protocol-error `already_acknowledged` must remain
distinct typed meanings even though both use `0x00`. Independent known-answer tests must decode that byte
under both response opcodes and assert the different typed results. They must also reject every other
result-code byte (`0x01..0xFF`) in an authenticated `PROTOCOL_ERROR` body, never reinterpret it as a terminal
code or fall back to another domain. This is a constraint on the result-code field, not on the surrounding
envelope or authentication-tag bytes; it does not broaden the separate clear/open error policy above.

`response_seq` is one byte deliberately: at most 256 frames can belong to one response, with no more than
255 output frames followed by the required terminal frame. Even under the smallest accepted carrier cap,
that is already far beyond the response transcript firmware should retain in RAM. A two-byte sequence would
cost one byte in every response DM without increasing a usable target limit.

### 8.9a Admission result (R-RA-36, codec preparation 7b-2-0)

An ADMISSION_RESULT is a fixed, intentionally clear, session-key-authenticated response. It reports
non-executed admission/control outcomes; it is not an executed transcript, has no response sequence, and
creates no RESPONSE_ACK debt. The exact body is:

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 1 | `ctl`: opcode `0x5`, actual ACL slot 0..9 |
| 1 | 8 | `request_id`, little-endian |
| 9 | 1 | `request_ctl`: AUTH_EXECUTE / SAFE_ROLLOVER / FORCE_ROLLOVER, same slot |
| 10 | 1 | `admission_code` |
| 11 | 1 | `detail` |
| 12 | 16 | K_session authentication tag; empty plaintext, no ciphertext |

| Admission code | Allowed request | Detail |
| --- | --- | --- |
| `0x00 session_full` | AUTH_EXECUTE | Zero |
| `0x01 ingress_full` | AUTH_EXECUTE / SAFE_ROLLOVER / FORCE_ROLLOVER | Zero |
| `0x02 session_busy` | SAFE_ROLLOVER | Positive byte count |
| `0x03 executing` | SAFE_ROLLOVER / FORCE_ROLLOVER | Zero |
| `0x04 preparation_failed` | SAFE_ROLLOVER / FORCE_ROLLOVER | Zero |

Header length is 12, total length exactly 28, application capacity zero. No sentinel/open form, epoch,
controller public key or variable detail follows. The new-domain nonce binds all three clear notice bytes
as specified in §8.1; AAD is direction + complete clear header + stable logical controller SOURCE_HASH.
An identical notice is byte-identical; a changed code/count/request domain changes its nonce. The codec
retains a distinct typed admission result, rejects illegal pairings/codes/detail/length and publishes nothing
on failed authentication or semantic validation. The wire busy count is 1..255; the target consumer enforces
the ruled live transcript capacity (currently four), without coupling the pure codec to session storage.
The separate codec slice adds no producer; 7b-2 owns real target lifecycles and 8a owns controller handling.

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
| Authenticated-clear admission result (R-RA-36) | 28 | 0 |

`carrier_rpc_body_cap` is obtained from one codec authority,
`remote_body_cap(RemoteCarrier carrier)`, which derives its result from `data_inner_cap()` /
`data_frame_len()` and that carrier's required immutable fields. It is never copied as literals or derived
from `dm_max_body_bytes` by analogy. **CORRECTED 2026-09-06 by R-RA-28; earlier requirement kept visible:**
“Every supported request and response carrier must have a boundary test that the real packer accepts exactly
at its returned cap and refuses cap plus one.” The admission authority must always refuse cap plus one;
the raw packer's refusal is not universal when reserved DST_HASH bytes are absent. §8.11's R-RA-28 paragraph
below governs that distinction. As a current source-verified example, the same-layer registered-mobile
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

**Slice 2 input correction, accepted by QA 2026-09-06 under R-RA-28 (B308/B309):** the 0e figures above remain historical
raw-packer measurements, not permission to admit a larger application body after R-RA-25. The hash-addressed
shapes derive 232 bytes same-layer, 231 for a typed mobile wrapper, and 229/228/227/226 for full cross-layer
depth 1/2/3/4. Their field assumptions must be proved per v2 leg: a full administration key is not the ordinary
routing hash (§6.3), open requests make no administration-key claim (§6.1), and the current hosted-mobile
last-mile enqueue supplies no destination-hash override (R-RA-25's narrow addendum). Do not infer a new
universal destination-hash requirement from the historical table or silently alter that last-mile carrier.
Where DST_HASH is absent, raw packing room and the conservative application admission cap differ; record
which authority refuses cap+1. Slice 2 must implement and independently test the reconciled live carrier map
and include the cross-layer typed mobile wrapper: its destination-path depth 1..3 spends an enclosed-type
byte as well as the path block, yielding 228/227/226; the home's corresponding full path is depth 2..4.
The wrapper's depth 4 is invalid, not an additional 225-byte v2 carrier. No codec consumer or routing change
is authorized by this preparation note.

**Owner-settled 2026-09-06, R-RA-28:** always reserve the four DST_HASH bytes in the capacity calculation,
even where the legal wire form omits them. The one authority derives
`min(inner storage capacity, actual DATA air-fit capacity) - reserved origin/source/destination fields - carrier extras`
using the existing named constants and packers, with invalid shapes/underflow refused. The reserved base fields
total nine bytes; path and enclosed-type bytes are additional. Thus the same-layer RPC allowance is 232 with
or without a transmitted destination hash; the wrapper/path caps above remain unchanged. The admission/codec
boundary must refuse cap+1. When a reserved field is absent the raw packer may have spare room, so its physical
fit is a separate measurement, not a falsely claimed admission refusal. The all-carriers-hash-addressed premise
is unnecessary and is superseded as a justification for the cap. Last-mile hash attachment is an optional
separate carrier proposal (B310), not included in Slice 2 or authorized by this capacity ruling.

**Slice 2 software closure, QA PASS 2026-09-06 (B308/B309):** the implemented `remote_body_cap` derives this
reservation from the named storage/air-fit authorities. Native admission and real packing are measured
separately: hash-less same-layer 232 admits, 233 refuses admission but still physically packs; the raw
ceiling remains 236. The typed cross-layer wrapper admits destination depths 1..3 at 228/227/226 and refuses
depth 4 as `bad_carrier`, not a 225-byte allowance. Both directions and the hosted last mile are covered.
Every fixed RPC overhead in the table is static-asserted against its field arithmetic. The 0e test keeps its
historical measurements, with the stale admission interpretation superseded in comment-only hunks proven
token-identical. Evidence `docs/superpowers/evidence/2026-09-06-radmin-slice2.md` §§8/11; no packer, routing
path or destination-hash attachment rule changed. The authority has no runtime consumer yet.

The 16-byte authentication tag and 8-byte request identity are the two load-bearing authenticated envelope
costs; the transmitted one-byte control is the remaining RPC metadata. Open diagnostics omit the tag by
explicit policy and therefore provide no security claim. No acceptance test may call 214/213 the universal
normal-command/output budget.

## 9. Nonce and request-ID rules

For authenticated traffic, a 24-byte XChaCha nonce is derived, not transmitted, from the message's actual
sealing/tag key (`base_key32` or `session_key32` as defined in §8.1) plus a domain containing the complete `ctl` byte, message direction, `request_id`,
`response_seq` (zero for requests), and the stable logical controller `SOURCE_HASH` captured from the
request carrier. Only the bootstrap **response** and rollover-result response domains additionally include
the clear `admin_epoch`; the bootstrap request does not. ADMISSION_RESULT instead additionally includes
its clear `request_ctl`, `admission_code` and `detail`, with no sequence field and a zero sequence nonce term.
Thus a changed admission outcome/count changes the nonce even under the same session key and request ID;
an epoch-bearing response to a replayed request after rotation also changes its nonce. The
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

**Slice 2 implementation boundary, codec half QA-passed 2026-09-06 (B312; real integration remains open):**
`IHal::rand_bytes` (`lib/core/hal.h:183`) returns void; `DeviceHal::rand_bytes`
(`lib/hal/device_hal.cpp:158`) calls the void `mrrng::fill`. That interface cannot return an entropy-failure
status to the new codec. The consumer-free Slice 2 implements `remote_make_request_id` with an explicit
caller-supplied, status-returning eight-byte entropy input. Native tests drive its real success/refusal
boundary: absent, failed and partial-write-then-failed providers publish no ID and preserve the caller's
previous value; the corresponding mutation controls are RED. No Node, HAL or provider was changed. This
does not prove hardware RNG health or an on-air refusal. The first real consumer must supply and gate a
truthful entropy adapter; wrapping the existing void draw in unconditional success is not that proof.
B312 remains open for this integration obligation.

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

Executed session records are not silently evicted while their session key remains valid. This prevents a captured old
request from becoming executable again merely because a small ring wrapped. Per-slot rollover invalidates
every old request for that slot cryptographically and is occasional bounded maintenance, not a
command-to-command token chain.

The Slice 7b-1 B369 exception is an admission that **never executed**: when its 300-second ingress body expires,
its still-admitted seen row is released too, and an exact retry may admit it afresh. No at-most-once obligation
exists for that unexecuted command. Executed and acknowledged rows retain their protection. An authenticated
`RESPONSE_ACK` consumes no ingress reservation, so it can release transcript capacity while owner work waits.

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

A completed authenticated transcript is immutable, including its terminal and frame count. A send-time seal or
enqueue failure preserves its bytes and cursor; the next eligible main-loop pass retries the pending frame.
An authenticated exact request retry instead restarts that completed transcript at sequence zero, without
dispatching again. Neither failure creates a suspension latch, timer or replacement terminal. `internal_error`
is selected only at completion for execution or response-staging failure before a truthful normal result exists;
it cannot replace a frozen result in the deterministic response-nonce space (B374/B375).

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

Command bytes pass one shared validator before local or remote dispatch. It rejects embedded NUL, CR, and LF
and takes the applicable named bound as an argument; there is no second, more-permissive remote parser.
⚠ **CORRECTED 2026-09-04 by R-RA-24′; superseded claim kept visible:** this paragraph formerly required one
`common_command_max_bytes` literal across the USB line, BLE line, DM body and remote RPC tail, and the next
paragraph said that the universal value necessarily shortened the 1024-byte USB surface. That mixed three
different boundaries and would have made a full 239-byte local DM impossible. The current authorities are:

1. ⚠ **CORRECTED 2026-09-05 by R-RA-25; the former 239-byte decision remains visible in the history below:**
   `dm_max_body_bytes == 232` is the one conservative application-DM BODY authority, reserving destination hash,
   origin and source hash. Unknown by-ID addressing may omit a genuinely unavailable destination hash but does
   not gain extra body capacity;
2. `console_line_max_bytes` is per transport: USB remains 1024 bytes including NUL. ⚠ **CORRECTED AGAIN by the
   executed Slice-0f producer census; the superseded 272+NUL=273 `send`-only derivation is kept visible here:** the
   Slice-0f authority was `max(send 272, send_layer 274, remote 261) + 1` = **275 bytes including NUL**. ⚠
   **POST-0h CORRECTION 2026-09-05; the 272 term remains visible:** R-RA-25 changed the DM body from 239 to 232,
   so the current expression is `max(send 265, send_layer 274, remote 261) + 1` = **275 bytes including NUL**. Its
   binding definition is the canonical parser-accepted spelling of each product verb, each accepted option once,
   carrying the largest body its carrier admits. The 274-byte `send_layer` boundary vector is transport-admitted
   and returns `err_unsupported`; the 268-byte plaintext form is the queue-positive. This is a derivation, never a
   bare 275; and
3. `remote_command_max_bytes` counts only the exact command TAIL after `--`. It is derived from the smallest
   authenticated v2 carrier: 226 available request bytes minus the 25-byte authenticated request envelope =
   201 bytes today.

Every path tests its own boundary and cap-plus-one, and rejection is explicit rather than truncation or newline
splitting. The one validator owns NUL/CR/LF rejection and the supplied bound; three named bounds do not justify
three validators. Response/transcript limits remain separate. A full local DM remains usable over USB and BLE,
while a remotely executed command is honestly bounded by its radio carrier.

⚠ **AUTHOR VERIFICATION CORRECTION 2026-09-04:** the earlier phrase “longest syntactically legal local line” is
withdrawn. The permissive parser accepts repeated whitespace/options, and the debug `testsend` schedule can be
USB-buffer-sized, so no finite maximum follows from syntax alone. The **historical 272-byte** authority was the
canonical normal `send` spelling with each distinct option once and the former 239-byte DM body; R-RA-25/0h now
derives that same spelling as **265 bytes** from the 232-byte body. Above-bound non-canonical/debug lines refuse
loudly on BLE. The withdrawn “USB must shorten” conclusion therefore does not apply. The BLE buffer had to grow from
160 to the derived 273 bytes in Slice 0f; the longest legal remote line also fits because its wrapper is at most
60 bytes and `60 + 201 + 1 == 262`. In command syntax, `-e` remains optional for ordinary `send`,
where it overrides the configured encryption default, but R-RA-18 makes it mandatory on authenticated
`remote`. The manual and help must state that difference, and `remote open -e` or `remote` with neither
security statement refuses.

⚠ **SECOND SAME-DAY CORRECTION / IMPLEMENTED BY SLICE 0f:** the preceding 273-byte conclusion priced `send` alone
even though the product BLE surface also exposes `send_layer`. BLE `console_line_max_bytes` is now derived in
`src/device_ble.h` from the console grammar's named terms—`max(send 272, send_layer 274, remote 261) + 1`—and is
**275 including NUL**. The binding producer is `send_layer` at its depth-4 cross-layer carrier cap (226 B, a
labelled transitional mirror of `pack_unicast_inner`'s sizing, pinned by an executed pack-at-cap / cap-plus-one
check). At the Slice-0f landing, bounds 1 (`dm_max_body_bytes` = 239) and 3
(`remote_command_max_bytes` = 201) were unchanged; **R-RA-25 later supersedes only bound 1 with 232**, while the
0f history and measurement remain visible. The longest
canonical `remote` line (261 + NUL = 262) is statically proved to fit. USB's 1024-byte buffer is untouched.
Measured cost: **+120 B of nRF52 `.bss`** (+115 `g_line`, +5 alignment), 0 flash; `heltec_mobile` is byte-identical.
See [[B288]] for the derivation defect this corrects.

⚠ **POST-0h CORRECTION 2026-09-05:** the preceding Slice-0f formula remains the historical landing statement.
With R-RA-25's 232-byte DM cap, `device_ble.h` now evaluates the same symbolic authority as
`max(send 265, send_layer 274, remote 261) + 1 = 275`. No BLE source or storage changed; the Slice-0h probe
executed and re-derived the 265/274/261/275 tuple.

⚠ **CORRECTED 2026-09-04:** the legacy remote path's 56/63-byte truncating buffers are not evidence for this
authority and do not receive a compatibility cap. Characterization uses the actual USB/BLE inputs, the real
v2 packer-derived carrier bounds, and the complete command inventory. The legacy defect disappears when its
helpers and parser are deleted; no path may preserve silent truncation in the interim.

Current local command execution is not yet perfectly unified: `send` and `send_channel` are handled by
the serial/BLE callers around `dispatch()`. ⚠ **CORRECTED 2026-09-05 BY SLICE 0b; THE OLD CLAIM REMAINS VISIBLE:**
this sentence formerly added that “`regen` writes through the global console sink.” That was B279 and is now
false: `regen` and the canonical identity formatter use the supplied `Print&`, while the boot caller names
`mrcon` explicitly. The implementation may not claim same-command parity until the remaining applicable paths
use one transport-neutral execution seam and the supplied output sink. Per C1, that consolidation and remote
enablement must remain separately reviewable.

The command/subcommand authority table is generated from the complete production dispatcher and its USB/BLE
caller-only arms by a pinned `tools/` instrument in the `check_data_type_literals.py` idiom. The generator,
not a hand-written table, owns completeness and makes an added/removed verb or sub-verb a visible diff. Once
the generated inventory exists, the owner assigns exactly one minimum authority to every row in a separate
ruling; code generation does not guess policy from names. The implementation gate fails on an unclassified
or multiply classified row.

R-RA-21 now fixes that classification policy in §12.1, but the first 177-row 0e rendering is not yet the
ratified authority table: it renders two different `peers` parser decisions as indistinguishable duplicate rows
in both `dispatch` and `ble_dispatch_line`. Its correction must merge a true duplicate or expose the semantic
discriminator, then prove every target-applicable semantic command/subcommand appears exactly once with exactly
one authority. Controller-only wrappers and physical trust-store operations remain local surfaces; presence in
the inventory never makes them recursively remote-dispatchable.

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

**Current implementation scope (7b-3 revision 4):** after separate P1 preparation (the B389 allocation is ruled: R-RA-40, +80 B)
ruling, twelve policy rows cover reboot/prep aliases, OTA, confirmed factory reset, sleep and crash modes.
Thirty-six exact disruptive cfg/gateway/join/create/leave/team/regen rows remain retained remote refusals
under R-RA-39, listed with separately fenced follow-ups in the brief §2.2 (B395–B398). No early effects and
no scheduled promise for these exceptions; local semantics and non-disruptive forms stay unchanged. The
following general contract remains the end-state requirement when each deferred family is implemented.

**R-RA-37:** one outstanding disruptive promise per target across all ACL slots, in ONE deferred row.
An otherwise eligible conflicting request receives retained immutable TERMINAL action_busy 08; exact retry
replays that result, not a later execution. Preparing/armed/due work reads executing to rollover. A never-owned
unarmed promise can hold the row indefinitely until same-slot force or permitted other-owner ACL invalidation;
no invented timeout/cross-slot force. First checked queued/parked ownership of the scheduled terminal arms
its frozen delay once; OUTPUT ownership alone does not arm. An armed action survives ordinary transcript,
ACL/root/session invalidation and retains its deadline; deliberate local pre-emption is external interruption.

**R-RA-38:** prep-restart stays remotely schedulable. It halts mesh RX/TX/timers/admin until a local restart;
USB/BLE console reboot where supported, hardware reset or power-cycle can recover it. Do not carve RX out
of the halt. Part 57b is the metal obligation after software PASS and the controller/carrier is available.
8a must show this exact text before submission, with no new confirmation token:

> prep-restart stops mesh radio and remote administration. Restart the target locally to restore access; remote reboot and rollover cannot recover it while halted.

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
  + MAC CTS-wait windows for every RTS attempt at cfg PHY
                                        (production authority `start_rts_timeout()` named by Slice 0e)
  + MAC ACK-wait window at cfg PHY     (production authority named by Slice 0e)
  + cascade_requeue_base_ms
```

`cts_to_data_gap_ms` is 5 ms (`lib/core/protocol_constants.h:133`), the busy term is
`2 * 30 ms` (`rts_max_retries` at `:135`, `rts_busy_retry_ms` at `:134`), and
`cascade_requeue_base_ms` is 5 s (`:273`). The final term prices exactly one first requeue; it is not the
30-second cascade backoff cap and not `send_defer_ttl_ms`. Slice 0e names `start_rts_timeout()` and
`start_ack_timeout()` as the production wait authorities, derives the terminal DATA length through the actual
packer, and independently recomputes every airtime term with `airtime_ms()` (`lib/core/airtime.h`).
⚠ **R-RA-23, 2026-09-04:** the CTS term sums attempts 0, 1 and 2. The discarded interpretation counted only the
largest final-attempt window and produced 6,506/13,012 ms. At the characterized default PHY with host slop zero,
the complete sum is **7,006 ms floor / 14,012 ms default**. Production values remain derived from configured PHY
and include configured RX-window slop at every occurrence; these two numbers are a reference vector, never
production literals. Slice 7a—not
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
Slice 1 keeps that switch alongside the two new flags until Slice 9 deletes the legacy paths. ⚠ **SUPERSEDED
2026-09-06 by R-RA-27:** the former requirement that the scaffold “must not disable shipped `rcmd` behaviour early”
does not promise compatibility across Slice 1b: nothing is deployed. The scaffold itself is inert; 1b strictly
gates receive ownership by the new pair, without widening either with the legacy switch. Consequently a
static/gateway issuer may still air a legacy command but no longer stage its response. Its legacy round-trip
bench steps are suspended from 1b until Slice 9's replacement. The two exact flags and compile-time assertions
apply to every **board** profile, detected by `defined(ARDUINO)` under R-RA-26:
mobile means `{client=1, accept=0}`, while static and gateway mean `{client=0, accept=1}`. A build may not
enable both or neither in a product configuration. The native host-test build is deliberately not a product
profile and compiles `{client=1, accept=1}` so one process can drive controller and target end to end; its
tests must still exercise each role-disabled boundary separately. R-RA-27 uses one production-shared pure routing
decision with explicit capability arguments for the synthetic role matrix; production supplies the real macros,
never runtime role state or a test-only override. Native drives both real owned RX paths; product compilation and
controlled wiring checks establish the disabled arms. Separate controls preserve ordinary relay transport.

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
- observable counters for inbound refusal, open rate-limit refusal, transcript exhaustion, response enqueue
  failure and response seal failure; Slice 7b-1 implements the last three as separately attributable saturating
  u16 counters in ACCEPT state. A seal refusal does not attempt enqueue or increment the enqueue-failure counter;
  full-queue pacing is not a failed send attempt. The frozen 7b-2 implementation adds the first two counters
  and exposes all five in ACCEPT text `status`; all saturate and preserve totals across ordinary invalidation.
  Independent 7b-2 QA and its B388 comment-return closure pass;
- no execution unless space for its mandatory terminal result and required deferred-action state has first
  been reserved;
- paced draining into the existing TX queue, with every enqueue result checked.

**Target open allocation/rate, R-RA-35:** three independently owned 1648-byte ACCEPT captures pair with the
three open staging rows; bootstrap and authenticated pools remain separate. The limit is **three open
admissions total per target in any 300000 ms window, shared across all requesters**, not three per peer.
Each admission occupies one budget position until its original deadline; completed output is wiped while
peer/deadline cooldown remains. At most one row is retained per peer. Source/ID/route changes cannot create
extra budget; authenticated traffic and bootstrap consume none. Use the existing earliest-deadline scan;
no completion/retry window reset or new timer. Independent 7b-2 QA now measures actual native/gateway
Node growth **+4976 B** and linked gateway RAM **+4976 B**, entirely `g_node`; mobile Node/RAM stays
unchanged. Owned input/output, rolling budget, cooldown and expiry proofs pass. This allocation is in the
uncommitted behavior implementation, not the codec-only 7b-2-0 commit; independent 7b-2 QA PASS closes B378.

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
time. State-dependent non-executed refusals use R-RA-36's authenticated-clear ADMISSION_RESULT where
specified, when transport permits; they never manufacture a completed TERMINAL transcript. Exact already-
acknowledged execute retries use their fixed authenticated PROTOCOL_ERROR. Unauthenticated malformed
garbage remains a silent drop to avoid an oracle. A well-formed
open diagnostic may receive a clear refusal, but it never bypasses the shared bounds or rate limit.

Before production state lands, the controller pending/session/result records and target
seen/transcript/ingress records are written as candidate value types and measured through
`tools/probe_board_abi.py` on host, ARM (`gateway`) and Xtensa (`heltec_mobile`). The characterization reports
each `sizeof`/alignment, aggregate cap cost, remaining board RAM, and timer cost. Exact capacities are an owner
ruling; they are not guessed or silently reduced to fit.

⚠ **Historical R-RA-22 characterization, 2026-09-04; the earlier “numbers await measurement” state was withdrawn:** the mobile/client profile
owns 4 inline pending requests, 4 session entries, 2 response-assembly headers, 8 response chunks, 2 retained-
result headers and 8 ACK-debt entries: **4,272 candidate bytes**. Inline sealed requests are chosen; there is no
separate sealed-request pool. The managed static/gateway profile owns 16 seen-request rows, 4 transcript
headers, 8 transcript chunks, 2 ingress-operation headers, 2 ingress-body slots, 4 open/bootstrap staging rows
and 2 deferred-action rows: **2,968 candidate bytes**. Product-role exclusivity means no board pays both totals.
The production RAM deltas must be measured and attributed independently; these candidate totals do not excuse
padding, ownership or auxiliary-state drift.

**Current deferred-class supersession:** R-RA-37 replaces those two candidate deferred rows with ONE row.
The old 2,968 total above is historical characterization, not today's production allocation or a recomputed
one-row budget. B389 is RULED by R-RA-40 (2026-09-15): +80 B, Node 230976/157344, mobile unchanged. Revision 4's complete twelve-row author model prices a 40-byte action
row, four independent u32 transcript-detail/header increases and two diagnostic bytes/alignment: session
state 8824→8904 (+80) on all three ABIs, native/gateway Node +80 and mobile Node unchanged. These are
compile-only model figures, not linked RAM or permission to change pins. P1 must add no resident state.

The two authenticated ingress rows are partitioned so at least one remains available to owner/control work.
The four open/bootstrap staging rows permit at most one open response per peer. Neither class borrows the
other's reserved admission/transcript state, and tests attack starvation in both directions. A full retained
BLE result causes loud local backpressure; it is not an excuse to acknowledge the target early or discard an
older result.

Expiry uses one shared earliest-deadline scan. `TimerWheel::kCap` grows exactly once, 91 → 92, which the 0e
faithful mirror measures as +8 bytes on all three ABIs. No record class receives a private timer ID. The scan
walks the bounded records resident in the current product profile and re-arms to the true earliest deadline;
exact-edge expiry and re-arm behavior are mutation-pinned.

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

0. **Pre-feature phase — eight independent slices, each with its own brief, gate and commit:**

   - **0a:** complete B208's bounded help-topic split;
   - **0g, bare primary-verb help (owner ruling 2026-09-05):** immediately after 0a and before 0b/0c, supersede
     the topic/content half of B208 with one compact `help`/`?` index containing only the primary command names
     compiled into that build plus the manual pointer. The generated command inventory is the completeness and
     feature-gate oracle; retire `help <topic>`, all embedded descriptions, and the historical frozen-content
     baseline. Keep the 2048-byte stage, zero-drop, no-pager/no-bypass and BLE-refusal requirements. This slice
     also makes the console-sink runner enforce its own pins and adds the advertised inventory `--check` mode.
     ✅ **LANDED / QA-PASSED 2026-09-05:** the full-feature build renders 49 sorted primary names plus the manual
     pointer; real profiles render 45..49 names. The largest response is 460 B of the unchanged 2048-B stage,
     with zero drop. The inventory remains 177 classified rows after exactly nine topic rows disappeared;
     `--check` and bare verification agree. The direct probe covers six profiles with 720 behavioral, 20
     structural and 212 BLE-guard checks; 47 controls are RED and the runner enforces its own pins. Board RAM is
     unchanged; flash fell 6720/8400 B on the ruled pair. B208 is software-complete and awaits Part 58 metal;
   - **0h, application-DM hash preservation (R-RA-25):** immediately after 0g and before 0b/0c, replace the
     optional-for-size hash decisions with the one 232-byte application-DM cap. `SOURCE_HASH` is mandatory for
     every application carrier; a supplied/known `DST_HASH` is mandatory while a genuinely unknown by-ID hash
     may remain absent without earning capacity. Refuse before queueing if the complete inner does not pack,
     prove the delegated-wrapper home-inbox and zero-inner reproductions closed, re-derive BLE capacity, and
     predict/attribute the corpus before accepting movement. Correct the stale TX-bail comment in the same slice;
     ✅ **LANDED / QA-PASSED 2026-09-05, Part 62 metal pending:** the cap is derived as
     `241 − origin(1) − DST_HASH(4) − SOURCE_HASH(4) = 232`; every application carrier carries source identity and
     keeps a supplied/existing-lookup destination identity. Both park helpers refuse rather than clamp, the
     plaintext pack result is checked with correct lifecycle ownership, and a malformed source-hash-less mobile
     wrapper is refused at its home before the inbox tail. Native 2610/110269/0; a live pre-change census observed
     847 application-DM decisions with zero size-dropped fields, then corpus reproduced 36/36 byte-identical with
     s18 `32afbf11`/269517/0. Node and board RAM are unchanged; `src/device_ble.h` is unchanged and re-derives
     `send=265`, storage 275. B296/B297 are software-closed;
   - **0b — DONE (software-complete, METAL-PENDING on bench Part 59), Quality-Agent-passed 2026-09-05:**
     B279's supplied-sink defect is closed as its own micro-fix. `regen` and the canonical identity formatter now
     answer only through the `Print&` selected by the caller; the boot path names `mrcon` explicitly. The real
     dispatcher proves the identical 55-byte success line on USB and a BLE-shaped sink with zero cross-sink bytes,
     plus the unchanged failure and identity/NV behavior. Native is 2610/110269/0; corpus 36/36, ABI and RAM are
     unchanged. No remote execution, parser, command, storage format, wire behavior or product policy was added;
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
     any later brief consumes them; and
   - **0f, BLE line capacity — ✅ LANDED / QA-PASSED 2026-09-04, Part 61 metal pending:** implemented only
     R-RA-24′ bound 2. ⚠ The launch text derived 273 bytes from `send` alone; that conclusion is superseded and
     remains visible in §12. The production authority in `device_ble.h` is
     `max(send 272, send_layer 274, remote 261) + 1` = **275 bytes including NUL**. The 274-byte `send_layer`
     transport-positive reaches `err_unsupported`, the 268-byte plaintext form queues, 275 bytes refuses loudly,
     and byte-at-a-time newline intake, USB behavior and companion ATT chunking are unchanged. The real-intake
     probe executed 1-, 20- and 244-byte chunkings with 40 checks / 8 controls RED. Native is
     2604/109619/0; corpus 36/36 with s18 `32afbf11`/269517/0; gateway RAM +120 B fully attributed (+115 `g_line`,
     +5 alignment), flash ±0; `heltec_mobile` byte-identical. ⚠ **POST-0h:** the visible 272 term is historical;
     the current symbolic tuple is `max(send 265, send_layer 274, remote 261) + 1 = 275`, with no BLE edit.

   Do not combine any of these fixes/refactors with each other or with remote execution (C1).
1. **Feature-boundary scaffold:** add
   `MR_FEAT_RADMIN_CLIENT` and `MR_FEAT_RADMIN_ACCEPT`. Compile-time and behaviour controls prove
   `{client=1,accept=0}` on mobile and `{client=0,accept=1}` on static/gateway while ordinary transit stays
   available with neither endpoint consumer involved; native deliberately compiles `{1,1}` with separate
   role-disable controls. Keep `MR_FEAT_REMOTE_MGMT` alive until Slice 9 deletes the legacy paths; this does not
   widen the strict receive owners introduced in 1b (R-RA-27). No v2 wire behaviour yet.
   **✅ SOFTWARE-COMPLETE / QA-PASSED 2026-09-06**, implementation committed at `5d2c00e`. R-RA-26's
   Arduino discriminator, two capabilities and three board-only diagnostics are present; no consumer or Node
   state was added. QA independently reproduced native 2610 cases / 110269 assertions / 0 failed, the feature
   probe's 9 cells / 97 checks / 19 controls RED, and 305 tools tests. Forced simulator rebuild: 34 actions,
   identical binary; 36/36 stream anchors and the current s18 keystone reproduced. Both ruled boards have zero
   RAM, flash, section, object and symbol movement; Node remains native 222072 / heltec_mobile Xtensa 117912 /
   gateway ARM 148680. All standing probes, checkers and census pass. No metal residue. B304's header half is
   corrected; its `platformio.ini` sibling remains open. Evidence:
   `docs/superpowers/evidence/2026-09-05-radmin-slice1.md`. QA resolved that evidence's STOP 8: the concurrent
   pre-check/register edits were documentation only, read by no build. The evidence remains unchanged.
1b. **Capability-owned pre-tail remote handlers:** split only the existing staging arm at
   `lib/core/node_mac_rx.cpp:2220-2238` (base `5d2c00e`) behind two explicit entry points: `REMOTE_CMD` is owned
   strictly by accept and `REMOTE_RESP` strictly by client. R-RA-27 authorizes this ownership change (C1),
   with only its necessary staging extraction; no unrelated refactor. Bodies preserve legacy semantics wherever
   an owner exists; neither gate includes `MR_FEAT_REMOTE_MGMT` and
   a disabled role deliberately falls through to the existing bounded `unsupported_internal` tail guard.
   Prediction precedes a 36/36 corpus-identity proof. Native drives both real owned paths and the pure routing
   decision for all four capability pairs × both types; the disabled-role decisions are synthetic, not a claim
   that the both-on native binary has compiled-out roles. Product compile/wiring controls complete that proof;
   receiver-file mutations pin both entry points and the tail, and the ruled board pair covers the product
   roles. Slices 5/7b and 8b later fill the accept and client bodies respectively; Slice 9 deletes the legacy
   body without removing the capability-owned entry points.
   ⚠ **SUPERSEDED 2026-09-06 by R-RA-27, old promise retained:** “Under the legacy `MR_FEAT_REMOTE_MGMT` gate
   their bodies remain behaviour-identical” and the pre-check's legacy-widening recommendation no longer apply.
   Static/gateway responses now have no owner: the legacy issuer still sends but stops receiving replies.
   Mobile commands no longer stage into an inert stub; they are ignored at the existing guard. The owner accepts
   both outcomes on undeployed test hardware. One scalar `unsupported_internal` event accompanies a guard drop
   in telemetry-enabled builds; devices strip that telemetry. No new metal; suspend the old static-node `rcmd`
   round-trip check until Slice 9. RAM stays unchanged (`_remote_inbound` remains until Slice 5); flash movement
   on both boards must be measured and attributed to the role split, not assigned an assumed sign or tolerance.
   ⚠ **CORRECTED 2026-09-04, prior staging plan kept visible:** round 2 left the shared staging arm in place
   until the later semantic slices. That made the role-disabled fail-closed boundary depend on future work.
   R-RA-19 moves ownership into 1b; R-RA-27 settles its behaviour change. Neither enables a v2 body.
   **Pass-2 seam obligations, recorded 2026-09-05, ownership corrected by R-RA-27:** 1b keeps owned staging
   before the open / sealed-relay processing and preserves its current cleartext body, 8-bit-origin reply key, bounded drop and
   clamp semantics byte-for-byte. It must nevertheless expose role-owned entry points so later v2 bodies can
   require `SOURCE_HASH`, key identity on the 32-bit source hash, refuse instead of clamp, and remove the
   accept-side staging RAM from client-only product builds. Both carrier types remain allocated in the internal
   namespace; otherwise a role-disabled build would bypass the fail-closed guard and deliver the frame as a DM.
   **✅ SOFTWARE-COMPLETE / QA-PASSED 2026-09-06; owner closure commit `cc35137`.** Evidence:
   `docs/superpowers/evidence/2026-09-06-radmin-slice1b.md`. Native is 2615 cases / 111354 assertions / 0 failed;
   the feature probe is 9 cells / 114 checks / 38 verified controls / 0 unusable, and the tools sweep is 312 OK.
   Both real owned receive paths execute in native; the pure decision covers all four capability pairs.
   Board preprocessing plus object evidence proves the disabled declaration, definition and consuming call
   are absent, without claiming a synthetic decision is an executed native receive drop. Changed-source
   selects six RX batteries; historical/dependency coverage adds `sliceAcodec`; their full union is 99/99 RED,
   0 unusable. All standing probes, ABI probes, inventory checks, checkers and the warning census pass.
   Forced simulator rebuild performed 40 actions across both core variants; the binary changed but all
   36 streams stayed byte-identical and reproduced their anchors. Board RAM is unchanged: gateway 195844,
   heltec_mobile 205684; flash is gateway 512092 (+16), heltec_mobile 1355292 (−8), fully attributed in
   evidence §8. Node remains native 222072 / heltec_mobile Xtensa 117912 / gateway ARM 148680. No metal added;
   bench §9.9's legacy static/gateway round-trip suspension remains in force. B307 is closed by the isolated
   comment-only proof; evidence §14 F1 is registered as B311 (its proposed B310 was already occupied), and
   F2's new resource measurement is folded into B286. The staging-slot retirement remains Slice 5 work.
   **QA provenance resolution:** the concurrent Author changes were the register, design, rulings ledger and
   MEMORY.md, plus QA's Slice 2 pre-check ledger. All are Markdown read by none of the coder's gates. QA accepts
   the STOP audit and its own re-run; this fuller inventory does not amend the 1b PASS or the coder's evidence.
2. **Remote codec and independent KATs:** pin the frozen opcode/slot values, little-endian fields, exact
   KDF/nonce/AAD labels and layouts, invalid/all-zero ECDH refusal, authenticated/open codecs,
   `remote_body_cap(carrier)`, legacy-body rejection, corruption/nonce-separation controls, and the random
   64-bit request-ID bound at the non-enforced `2^16`-request analysis envelope. No global `wire_version`
   bump or corpus-wide version re-anchor.
   **Author integration preparation accepted by QA 2026-09-06:** use a dedicated codec `.h/.cpp`, following
   `dm_crypto`'s namespace/build shape. The future brief explicitly fences the one source-list addition in
   the separate `/home/staszek/lora-universal-simulator/CMakeLists.txt`: add the codec `.cpp` once to
   `_meshroute_core_srcs`, which feeds both normal and gateway core libraries. No simulator behaviour change
   or codec consumer is included. Record both repository bases/diffs and prove both variants compile the new
   TU; the owner commits each repository's changes. No simulator edit is made during 1b, and Slice 2's brief,
   dispatch base and measurement pins wait for the QA-passed 1b closure commit.
   **Preparation history:** 1b closure `cc35137` was followed by owner preparation commit `9ea4947`;
   the Author repinned `docs/superpowers/plans/2026-09-06-radmin-slice2-remote-codec.md` to `9ea4947` and
   simulator `fd3295d` before dispatch, retiring the uncommitted-preparation exception.
   **Software-complete / implementation QA PASS 2026-09-06, no fold-ins; implementation committed.**
   Evidence: `docs/superpowers/evidence/2026-09-06-radmin-slice2.md`, measured in the coder's
   `/home/staszek/mr-slice2` worktree at dispatch base `9ea4947`; the paired simulator build is
   `/home/staszek/mr-slice2-lus` against simulator base `fd3295d`. During the Author landing, implementation
   commit `f2735f79c94adc068dde550d60e20b7660696f16` became HEAD in both MeshRoute checkouts and simulator
   commit `868888419c7cc250d7019860d3403a7721ade1fc` landed the one-line source-list addition. All delivered
   file hashes remain identical to the QA-passed package; the Author's five documentation edits remain
   uncommitted in the shared checkout. Historical measurement bases are unchanged.
   The codec implements §8.1's frozen opcode/slot and KDF/nonce/AAD domains, §8.9's typed result namespaces,
   §8.11's reservation-based capacity authority and §9's checked entropy boundary; no runtime consumer exists.
   QA independently reproduced native **2640 cases / 115288 assertions / 0 failed** (+25 / +3934), independent
   reference **87/87** frozen vectors with external primitive anchors and a failing altered-expected-byte
   comparison control, and the full mutation union: changed-source `radmin2codec` **66/66 RED** plus
   historical/dependency `b20codec` **5/5 RED**, **0 unusable**, exact-one matches and hash restoration.
   Both simulator archives gained one codec object and twelve namespaced symbols each after five real build
   actions; the linked executable remains byte-identical with zero codec symbols. **36/36 byte-identical
   streams**, all anchors and s18 reproduced, no re-anchor. Deterministic board pairs are byte-identical in
   RAM/flash/ELF/payload: gateway **195844 / 512092**, heltec_mobile **205684 / 1355292**; each compiles one new
   object but retains no codec symbol. Node ABI pins remain native **222072**, heltec Xtensa **117912**,
   gateway ARM **148680**. Six probes, both ABI probes, inventory, tools **312 OK**, warning census and both
   checkers pass without repins; the feature-ownership contract stays at three files. Evidence §12 explicitly
   records the late base-probe capture and reconstructs pristine 179 versus final 182 scanned source files;
   this is not represented as an entirely pre-edit probe capture.
   B308/B309 are closed; B312's codec boundary is complete but its real-provider integration remains open.
   Findings §13 land as B314 (naming hazard avoided/closed), B315 (output-path validation gap, open), and B316
   (duplicate measurement folded into open B286; current harness rsync line **10114**, correcting the evidence's
   historical **9851** citation). B311 remains open: printable diagnostics avoid, but do not repair, its
   decoding failure. B313 remains open and B310 parked. **No metal added**; prior bench debts and the §9.9
   static/gateway legacy `rcmd` suspension remain. The owner still commits these Author documentation
   landings; QA's Slice 3 pre-check precedes the next Author brief, whose base waits for that preparation
   sequence rather than reusing an old dispatch pin.
3. **Target identity, ACL, and USB provisioning:** on accept builds add `/mradmid` and the fixed ten-slot
   `/mracl` transaction using the team-keyring persistence idiom, first-owner USB exchange, several-owner
   invariants, corrupt-state recovery, USB-only target-root rotation, role changes, and ordinary-`regen`
   preservation tests. No remote execution yet.
   **Author preparation 2026-09-06:** QA pre-check and R-RA-29 are committed at `7299eb9`; the DRAFT brief
   `docs/superpowers/plans/2026-09-06-radmin-slice3-target-stores.md` pins that existing MeshRoute base and
   unchanged simulator `8688884`. §6.6.1 records the Author decisions and limitations. Bench Part 55a is
   drafted, not runnable until QA-passed implementation/transcripts and the required fixture are available.
   QA gates the brief; any later preparation commit requires an Author repin before dispatch.
   **Brief review 2026-09-06:** QA verified the scope and B318 corrections; its sole HOLD fold-in is now
   applied in brief §5/§6 item 7. The inventory's typed profile table explicitly gains ACCEPT = 1 on the
   four static/gateway profiles and 0 on both mobile profiles, not a legacy-derived alias; its unit fixtures
   and console-sink per-profile ownership counts must be re-derived. B319 tracks that implementation debt.
   The changed sections await QA confirmation; neither a software PASS nor a new base is implied.
4. **Mobile controller keyring and target book:** on client builds add the ten seed-only `/mrmkeys` slots and
   32-row `/mrtargets` using the same persistence idiom, seed-derived identity path, USB-only secret
   operations, public list/show, transactional persistence, in-use refusal, accepted measured capacities,
   messaging-peer independence, and `regen` behaviour. No on-air RPC yet.
   **DONE — software-complete, independent QA PASS 2026-09-07; Parts 55b/56 METAL-PENDING.**
   Implementation/evidence in 19c10bf; §6.3.1 records the verified capacities, exact board costs and
   instrument figures. B321/B327 closed; B328/B329/B330 remain scoped follow-ups. Part 59's exact
   client warning is implemented and host-gated, not hardware-verified. No on-air RPC in this slice.
5. **Target authenticated session/dedup state:** full-key discovery/bootstrap, epoch/session derivation,
   bounded seen-table value types, retained source identity and request-fingerprint classification. It may
   establish the table keys, reservation shape and reboot epoch boundary, but it does **not** consume or
   manufacture response transcripts. **R-RA-31 permits bootstrap responses only in this slice**, through
   the existing same-layer or reversed-cross-layer application-DM path. §7.4 and the advance Slice 5
   brief record the Author decisions; Slice 4-delivered bindings are filled, with final base pin pending
   the owner's landing/preparation commit and QA's final brief gate.
   Exact transcript retry, ACK release/debt, `session_full` /
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
7. **Target transcript and activation contract — separately attributable slices (7b split by R-RA-34):**
   - **7a, activation configuration:** add only the persisted target
     `cfg.remote_action_activation_ms`, its source-derived default/floor/ceiling validation, schema migration,
     refusal of an impossible interval, and boundary controls. This is the R-RA-16 NV change; it changes no
     action timing yet.
   - **7b-1, authenticated executor/transcript — software-complete, independent QA PASS 2026-09-08:** bounded
     multi-DM sink, mandatory result reservation, main-loop dispatch once under the authenticated ACL context,
     paced OUTPUT/TERMINAL, immutable exact replay, ACK release with executed-fingerprint retention,
     three disjoint saturating transcript/send counters, and the R-RA-32 private-application-DM view.
   - **7b-2, session control and open responses — software-complete / independent QA PASS:** safe/confirmed-force rollover, `session_full` /
     `session_busy`, `PROTOCOL_ERROR{already_acknowledged}`, shared-credential behavior, measured open/bootstrap
     partition and rate limit, open staging/response release, and §15 status-counter exposure. Controller-side
     automatic rollover and ACK debt remain with the controller slices.
   - **7b-3, deferred actions — revision 4, implementation HOLD B394 only (B389 RULED R-RA-40):** ONE R-RA-37 deferred row,
     scheduled terminal/7a activation, ACK-earlier and the separate 300-second outcome bound. Twelve policy
     rows after independently gated P1 preparation; 36 exact retained-refusal rows and B395–B398 follow-ups.
     Remote OTA reports the locally reached backend. Each preparation/behavior increment has its own gate
     and owner commit; codec 7b-3-0 is committed at ac5f9a5.
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
| 0g | ✅ landed/QA-passed: bare primary-verb help, console-sink self-pins, inventory `--check`; `src/firmware_help.h` + tools | native 2604/109619/0; corpus 36/36; RAM +0/+0; flash −6720/−8400; 279 tools OK | **Part 58 re-issued:** bare inventory on USB, build gates and no drop |
| 0h | application-DM body/hash admission, `protocol_constants.h`, `node.cpp`, `node_mac.cpp`, derived BLE capacity + tests/tools | predict body-length and hash-field reach first; attribute every mover; ruled pair + ABI/warnings | only if a real transport boundary changes after derivation |
| 0b | supplied-sink `regen`, `src/firmware_config.cpp` + caller seam | semantic identity; ruled pair | none |
| 0c | dispatcher/sinks, `src/fw_main.cpp`, `src/firmware_commands.cpp`, sink headers | semantic identity; ruled pair | none |
| 0d | ✅ landed: four home-bound arms, `lib/core/node_hashlocate.cpp` | predicted 0 movers; measured 36/36 byte-identical; no re-anchor; ruled pair RAM +0 | none |
| 0e | ✅ measured in isolated worktree: generated inventory, ABI/cap/timing probes under `tools/` + fixtures under `test/` | 36/36 unchanged; host/ARM/Xtensa ABI and ruled pair; integration package pending | none |
| 0f | ✅ landed: BLE line-capacity derivation and real-intake probe, `src/device_ble.h` | native **2604 / 109619 / 0** (+7 / +85); corpus **36/36 anchors**, s18 `32afbf11`/269517/0, `lus` `eb298576` unchanged with 0 build actions (recompile control fired); `sizeof(Node)` 222072/117912/148680 unmoved; `gateway` RAM **+120 B** fully attributed to `g_line` (+115) and alignment (+5), flash ±0; `heltec_mobile` byte-identical in every measured field; census 6/6 at pin; probe **40 checks / 8 controls RED / 0 unusable**; tools sweep 238 OK | **Part 61:** the 274-byte `send_layer` line over real BLE under multiple write chunkings returns `err_unsupported`; the 268-byte plaintext form queues; a 275-byte line refuses loudly |
| 1 | ✅ software-complete / QA-passed 2026-09-06; consumer-free `lib/core/mr_features.h`, implementation `5d2c00e` | native 2610/110269/0; feature matrix 9 cells / 97 checks / 19 controls RED; tools 305; forced lus rebuild 34 actions, binary identical; 36/36 anchors; Node ABI and both boards' RAM/flash/sections/objects/symbols unchanged; evidence `2026-09-05-radmin-slice1.md` | none |
| 1b | ✅ software-complete / QA-passed 2026-09-06, owner closure commit `cc35137`; strict capability-owned pre-tail handlers, `lib/core/node_mac_rx.cpp`; R-RA-27 | native 2615/111354/0; feature probe 9 cells / 114 checks / 38 controls; tools 312; mutation union 99/99 RED; both product compile-out proofs; forced simulator rebuild 40 actions, binary changed, 36/36 byte-identical anchors; Node ABI unchanged; pair RAM ±0, flash gateway +16 / heltec_mobile −8 fully attributed; evidence `2026-09-06-radmin-slice1b.md` | none; legacy static-node `rcmd` round-trip suspended from 1b until Slice 9 |
| 2 | remote codec/KDF files and carrier-cap authority; **SOFTWARE-COMPLETE / QA-PASSED 2026-09-06**, implementation `f2735f7` / simulator `8688884`; measured bases `9ea4947` / `fd3295d`; Author documentation commit pending | native 2640/115288/0; independent reference 87/87; mutation union 66+5 = 71/71 RED, 0 unusable; 36/36 byte-identical and anchored; simulator executable and both ruled board ELFs byte-identical, zero runtime codec symbols; +1 object per board and per simulator core archive; six probes/ABI/inventory/tools312/census/checkers pass; full landing and evidence pointer in §19 item 2 above | none; existing metal debts and legacy round-trip suspension unchanged |
| 3 | target identity/ACL storage and USB provisioning owners; **DRAFT brief awaiting QA 2026-09-06**, base `7299eb9`, simulator `8688884`; R-RA-29 and §6.6.1 | predicted zero remote events, 36/36 unchanged; no resident target state/Node change; pair RAM ±0, mobile live flash ±0, gateway flash attributed; real-router plus extracted BLE guard and full mutation union | **Bench Part 55a:** DRAFT target-side physical-USB first owner, local recovery and real-flash limits; not yet run |
| 4 | **DONE — software-complete, independent QA PASS 2026-09-07**, R-RA-30 / §6.3.1; implementation/evidence 19c10bf | native 2763/118344/0; 296 RED/0 unusable; six probes, tools 329, inventory 204; 36/36 exact, s18 unchanged, simulator identical/zero actions; RAM/flash gateway 195844/531004, heltec_mobile 207740/1367448, one-off xiao_mobile 172572/664636; Node pins unchanged, book +2056 B on clients; B330 tracks ARM flash cost separately | **Bench Parts 55b/56 METAL-PENDING:** physical USB exchange, real storage/reboot/fault/stack and secured BLE boundary; Part 59 exact mobile warning also not run |
| 5 | ✅ software-complete / QA-passed 2026-09-07, owner commit `d226189`; target session/dedup files `lib/core/remote_session.{h,cpp}` + `src/firmware_admin_runtime.h`; R-RA-31; B341 (live activation across a reboot) fixed in-slice | native 2825/119784/0; corpus 36/36 byte-identical, keystone unmoved; `lus` changed (Node layout); Node re-pin native 224136 / gateway 150504, mobile 117912 unmoved; gateway RAM +1336 attributed to five symbols, mobile +8 (`g_hal`); union 516 RED / 1 unusable (B342, pre-existing) | none |
| 6 | ✅ software-complete / QA-passed 2026-09-07 (uncommitted at report); the shared validator `lib/console/console_line.h`, `CommandContext`/outcome, the ruled authority table `src/firmware_command_authority.h` + `docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md` + `tools/check_command_authority.py`; B343–B348 folded in before implementation | zero remote events; `lus` byte-identical with 0 build actions; corpus 36/36; local behaviour byte-identical except the ruled `bad_line` refusals; inventory 203 rows; ruled pair RAM ±0, flash +5536 / +3816 attributed | Part 63 (the BLE embedded-NUL line) |
| 7a | ✅ software-complete / QA-passed 2026-09-08 (two commits: 7a-0 `89071fb` the MAC wait-window refactor `lib/core/mac_wait_windows.h`; 7a the feature, uncommitted at report): NV v25 `remote_action_activation_ms` (offset 276, record 280, explicit `kVersionMinLoad = 25`), `lib/core/remote_activation.h` (R-RA-20/23 budget, KAT 7006/14012), `src/firmware_remote_activation.h` (five-state resolver, never clamps), the `cfg set` key, text/JSON read-out, post-init boot line; B352–B358, B360–B363 folded in; B354 (unreachable `gw_announce_interval`) fixed | zero remote events; `lus` changed twice with 36/36 byte-identical; inventory 204; ruled pair RAM +8 / +8 (global + alignment), flash +5072 / +1476 attributed; union 204 RED / 1 pre-existing | **Part 57a** (landed) |
| 7b-1 | **SOFTWARE-COMPLETE / INDEPENDENT QA PASS 2026-09-08**, owner-committed at `1d4b3ad`; authenticated executor/transcript/Node sender, real-handler/context probes; B365–B377 closed | native **2883/127709/0**; union **675 RED / 1 known unusable B342**; tools **343 OK**; all required probes/checkers/ABI/census; fresh simulator arms, **36/36 byte-identical**; gateway Node **+1784**, RAM **+1792**, flash **+7520 B**; mobile Node/RAM **unchanged**, flash **+260 B** fully attributed; [independent QA](../evidence/2026-09-08-radmin-slice7b1-qa-gate.md) | **DEFERRED to 8b:** real target `status` round trip requires the controller/carrier; no new bench part yet |
| 7b-2-0 | **SOFTWARE-COMPLETE / INDEPENDENT QA PASS 2026-09-09**, revision 2, attribution base `1d4b3ad`, owner commit `564f460`; R-RA-36 ADMISSION_RESULT codec, no new live producer; B382/B383 closed, B384/B385 corrected/recorded; [QA evidence](../evidence/2026-09-09-radmin-slice7b2-0-qa-gate.md) | native 2888/172264/0; old literals 87/87, new arrays/controls verified; corpus 36/36 byte-identical; 712 RED / known unusable B342; tools 343 OK / zero skips; all ABI/probes/checkers/census; Node/RAM unchanged, gateway flash −64 B attributed, mobile linked sections identical | none; owner-committed, no new metal-only behavior |
| 7b-2 | **SOFTWARE-COMPLETE / INDEPENDENT QA PASS 2026-09-09**, owner commit `f993191` (base `564f460`), consumed revision 4; B378/B379/B387/B388 closed. B388 is an exact three-comment-line return with full input preservation and fresh 15-check proof; prior full gate is historical; **Historical B391 erratum, now CLOSED by 7b-3-0**: post-comment X09 was vacuous; the separate instrument repair is independently verified. [QA evidence §8](../evidence/2026-09-09-radmin-slice7b2-qa-gate.md#8-b388-scoped-return--independent-pass-and-final-closure) | native 2909/174485/0; corpus 36/36 byte-identical; 772 RED /known unusable B342; tools 343/zero skips; all ABI/probes/checkers/census; gateway Node/RAM +4976 B, flash +7840 B, mobile unchanged; global three-admission and real notice/control/open producers verified | controller-dependent open/control round trips join 8b; no new bench part |
| 7b-3-0 | **SOFTWARE-COMPLETE / INDEPENDENT QA PASS 2026-09-15; owner commit ac5f9a5**, R-RA-37; B391 closed. [QA gate](../evidence/2026-09-13-radmin-slice7b3-0-qa-gate.md). Typed TERMINAL action_busy 08; terminal 09..FF reject; protocol-error/admission domains and old 89 bytes preserved; no producer/state/wire_version change | native 2912/184461/0, reference 94/94, domain 9787, corpus 36/36, union 816 RED + known unusable B342 /817, all required probes/ABI/checkers/boards; Node/RAM unchanged. Behavior reissued at this hash | none (no new metal behavior) |
| 7b-3-P1 | **REVISION 1, ready for source-validation at ac5f9a5**, [simple-action preparation](../plans/2026-09-15-radmin-slice7b3-p1-simple-action-preparation.md), B394; shared typed admission/effects/sinks for twelve selected rows, local behavior unchanged and remote disruptive guards still refuse | Separate C1 refactor/full gate/owner commit; zero Node/resident RAM growth, real-source equivalence/control proof; then QA refreshes behavior base | none added by this behavior-preserving refactor |
| 7b-3 | **REVISION 4 at ac5f9a5, READY for source-validation; behavior HOLD B394 only, B389 RULED R-RA-40 (+80 B).** [Brief](../plans/2026-09-13-radmin-slice7b3-deferred-actions.md): twelve selected rows after P1; 36 exact R-RA-39 refusals. One promise/one row, typed action_busy, frozen 7a activation | Fresh author native 2912/184461/0, corpus 36/36, reference 94/94. Complete model +80 on three ABIs, Node 230976/157344/117912; owner allocation and linked attribution pending. P1 commit requires base refresh before behavior implementation/full gate | Part 57b on software PASS/controller availability: prep-restart lockout/local recovery; 8a exact warning, no bench PASS |
| 7b-3-F-config / F-provision / F-team / F-regen | **PLANNED, NOT IMPLEMENTATION DISPATCH**, B395–B398; 23/10/2/1 retained-refusal policy rows. Individual fences/reasons/closure proofs in behavior §2.2 | Each needs a separate source-validated brief, any state ruling, refactor and feature increments/gates, and explicit removal of its refusal list; not complete in 7b-3 | derive only when the individual follow-up is authored |
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
42. **R-RA-14, superseded in its one-literal reading by R-RA-24′:** command validation is shared across USB,
    secured BLE and RPC. The legacy 56/63-byte silent truncation is a defect to remove, not compatibility
    evidence or a cap. The validator takes the applicable named boundary rather than conflating line, DM-body
    and remote-tail capacity.
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
47. **R-RA-19, neutrality superseded by R-RA-27:** Slice 1b, immediately after the feature scaffold, owns the
    legacy pre-tail receive staging into accept-owned `REMOTE_CMD` and client-owned `REMOTE_RESP` entry points.
    Disabled roles reach the existing fail-closed tail; semantic slices fill the bodies later.
48. **R-RA-20:** disruptive-action activation uses the exact first-hop formula in §13: configured-PHY airtime
    for RTS/CTS/maximum scheduled-terminal DATA/ACK, the CTS→DATA gap, bounded busy retry, MAC CTS/ACK waits,
    and one 5-second first requeue. The floor is one budget, the owner's default is twice it, and the ceiling
    remains one millisecond below the 300-second outer bound.
49. **R-RA-21:** the generated semantic command table is classified once under §12.1's open/operator/owner/
    physical policy. The indistinguishable duplicate `peers` rows in the first 0e rendering must be merged or
    disambiguated before that table can be ratified; missing, duplicate and unclassified rows fail the gate.
50. **R-RA-22:** the client and accept profiles use the exact bounded row counts and inline-request choice in
    §15, with owner/control admission isolated from open/bootstrap pressure. Expiry uses one shared earliest-
    deadline scan and grows `TimerWheel::kCap` only from 91 to 92.
51. **R-RA-23:** the first-hop budget sums CTS-wait windows for every RTS attempt. The characterized zero-slop
    reference is 7,006 ms floor / 14,012 ms default; production remains cfg/PHY/slop-derived.
52. **R-RA-24′, superseded only for its DM-body number by R-RA-25:** three named bounds replace R-RA-24's
    incorrect universal literal. Its DM-body value was 239; transport line
    storage derived per transport (USB 1024 including NUL, BLE **275** including NUL after Slice 0f); and remote
    command tail 201 from the smallest authenticated carrier. ⚠ The earlier 273 figure, retained in §12, priced
    `send` but omitted `send_layer`; Slice 0f's `send=272` term is now historical after R-RA-25/0h. The current
    syntactic authority is `max(send 265, send_layer 274, remote 261) + 1 = 275`. One validator enforces the
    supplied boundary and NUL/CR/LF policy.
53. **R-RA-25:** the one conservative application-DM body authority is 232 bytes. Every `app_dm=true` carrier
    carries `SOURCE_HASH`; a supplied or derived `DST_HASH` is also mandatory and never discarded for size. A
    truly unknown by-ID destination may omit that hash but gets no extra capacity. Slice 0h landed the refusal and
    zero-inner fixes before the remote feature phase.
54. **R-RA-26:** `defined(ARDUINO)` distinguishes products from native/lus, including no-profile static boards.
    Products have exactly one endpoint; ACCEPT equals the still-existing REMOTE_MGMT switch until Slice 9.
    The Slice 1 scaffold has no consumer and no `platformio.ini` change; its exact zero-movement proof passed.
55. **R-RA-27:** 1b uses strict ACCEPT-for-CMD / CLIENT-for-RESP ownership, never legacy-widened gates. It
    deliberately stops static/gateway legacy response staging and ignores mobile commands at the existing
    fail-closed guard. One pure routing decision takes capability values; production supplies the macros,
    native tests the synthetic matrix. No runtime role state, no test-only macro override, no new metal.
56. **R-RA-28:** always reserve DST_HASH space in RPC admission, even on a legal hash-less leg; no larger
    allowance for omitted fields. Codec/admission refusal and raw-packer fit are separately tested. Home-to-mobile
    hash attachment is being considered separately, not authorized by this ruling or added to Slice 2.

### 20.2 Derived artefacts and later measurement rulings

The owner-decision list, including the feature-boundary/capacity rulings, is closed by R-RA-1..R-RA-28. What remains
before the relevant implementation slices may land is evidence and generated authority, not permission to reopen those product
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
- implement the three R-RA-24′ boundaries without conflating them: Slice 0f derives and pins BLE line storage;
  Slice 2 derives `remote_command_max_bytes` from `remote_body_cap(carrier)`; Slice 6 lands the one validator
  with the supplied bound and identical NUL/CR/LF rejection at every applicable dispatcher entry (R-RA-7);
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
mandatory source hash, one parameterized command validator, authenticated-remote `-e` requirement, capability-owned
pre-tail handlers, or carrier-specific first-hop budget rule.


### Historical 7b-3 / 8a second-read handoff — 2026-09-13, superseded by R-RA-37/38/39 and revision 4

The following records the proposals at that date. The current ruled warning and dispatch are in §§13/19.1;
codec ac5f9a5 fulfills its prerequisite, and B394/P1 now holds behavior implementation; B389 is ruled (R-RA-40, +80 B).

The [revision-2 brief](../plans/2026-09-13-radmin-slice7b3-deferred-actions.md) preserves B389/B390/B391 HOLD
and adds B392. If B392 keeps remote prep-restart, 8a must display before submission:
“prep-restart stops mesh radio and remote administration. Restart the target locally to restore access;
remote reboot and rollover cannot recover it while halted.” This warning adds no confirmation token and
requires no radio-admin exception to the halt. Local USB/BLE availability is product-specific; local reboot,
hardware reset or power-cycle are recovery, so physical power-cycle is not the sole mechanism.

Proposed action_busy is a typed non-execution outcome retained for that request ID; exact retry must replay it,
not execute when capacity later frees. Any fresh-ID retry policy is 8a's separate responsibility. New code 0x08
needs owner-approved 7b-3-0 codec preparation, its own gate/commit, then a reissued behavior base; the existing
scheduled detail needs no codec slice. This handoff records proposals, not a new ruling or implemented feature.
