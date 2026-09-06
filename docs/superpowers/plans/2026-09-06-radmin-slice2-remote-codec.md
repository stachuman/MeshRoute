<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 2 — remote codec and independent known-answer tests · dispatch brief · 2026-09-06

**Status: SOFTWARE-COMPLETE — implementation Quality-Agent PASS 2026-09-06, no fold-ins.**
Implementation is committed in both repositories; the Author documentation landing awaits its owner commit.
The dispatch contract below is retained as run provenance, not a new dispatch request.
Dispatch model: **Opus** (`model: opus`). Author writes; QA gates and dispatches; owner commits both repositories.

Authority: design §8, §9, §19 item 2 and §19.1 row 2 in
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`; R-RA-3/4/5/13/25/28 in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`; QA's
`docs/superpowers/plans/2026-09-06-radmin-slice2-precheck.md`, **with its §4 resolutions controlling**;
the QA-passed 1b evidence `docs/superpowers/evidence/2026-09-06-radmin-slice1b.md`.

**Completion landing 2026-09-06:** QA independently reproduced the implementation gate in
`/home/staszek/mr-slice2` at the pinned MeshRoute base and the paired simulator build
`/home/staszek/mr-slice2-lus`. Evidence: `docs/superpowers/evidence/2026-09-06-radmin-slice2.md`
in that coder worktree. Native 2640 cases / 115288 assertions / 0 failed; independent reference 87/87;
mutation union `radmin2codec` 66 + full `b20codec` 5 = 71/71 RED, 0 unusable. All 36 streams reproduce their
anchors; both ruled board ELFs and the simulator executable are byte-identical to their respective bases.
The new TU compiles into both board builds and both simulator core archives but has no runtime consumer.
No metal is added. Design §19/§19.1 and the register carry the Author landings. **Commits verified during
landing:** MeshRoute `f2735f79c94adc068dde550d60e20b7660696f16` contains the QA-passed code/evidence and is
now HEAD in both shared and coder checkouts; simulator `868888419c7cc250d7019860d3403a7721ade1fc` contains
the single source-list addition. All six delivered MeshRoute files and simulator CMakeLists retain their
pre-landing SHA-256 hashes. Only the five Author documentation edits remain uncommitted in the shared
checkout. These implementation commits do not replace the historical dispatch bases below.

## Bases and starting provenance

| Repository | Exact pinned base | Meaning |
| --- | --- | --- |
| `/home/staszek/MeshRoute` | `9ea4947d99ec004f2952553ce6fd34b166eeebce` | `9ea4947`, owner commit `Slice 2 prep`; includes 1b closure and the complete Author preparation package |
| `/home/staszek/lora-universal-simulator` | `fd3295d8eaf71466270434f2cbf4f4eafe19edcc` | Independent simulator base, verified clean when this brief was written |

Both are existing commits, not placeholders. **Author repin after the owner's preparation commit:** `9ea4947`
replaces the former `cc35137` dispatch base. Its diff from `cc35137` contains only this brief, the register,
design, MEMORY and tracker; all preparation is committed. Both repositories were verified clean before this
post-commit brief-pin edit. The former uncommitted-preparation exception is retired.

QA supplies the reviewed, post-pin Author brief as the dispatch authority; the copy inside the preparation
commit necessarily predates this pin. Keep the post-commit pin outside the coder's measured input tree:
start from a clean isolated MeshRoute worktree at `9ea4947` if the shared checkout contains this Author edit.
Do not copy the dirty brief into that worktree or revert the Author's copy. Both measured repositories must
start clean at their exact pinned commits. QA/coder record `git rev-parse HEAD` and full status in **both**
repos before work; a missing/different base or dirty measured start is a STOP. The coder never repins,
commits or repairs a checkout to hide a mismatch. A separate simulator worktree/build must point
`MESHROUTE_DIR` at the exact paired MeshRoute worktree, not silently compile the main checkout.

This is an **additive, consumer-free codec feature**, not a refactor of DM crypto or legacy remote management
(C1). No Node, RX/TX, command, ACL/session, storage, timer, entropy-provider implementation or wire-version
change is included. Nothing airs and no new metal part is owed. No owner ruling is outstanding.

## Authority quoted verbatim

Design §8.1:

> Slots 0..9 select established authenticated ACL sessions. Nibbles A..E are reserved and reject. Sentinel F
> is valid only with `OPEN_EXECUTE`, an open response, or a bootstrap request before the target ACL row is
> known. A bootstrap response carries the actual matched slot 0..9. Open and bootstrap are opcodes, not fake
> ACL slots. The decoder chooses one body layout from outer direction, opcode, slot class, and exact length;
> an invalid authenticated request never falls back to the open decoder.

Design §8.1:

> The exact ASCII KDF labels (without a trailing NUL) are
> `MeshRoute remote-admin v2 base`, `MeshRoute remote-admin v2 session`, and
> `MeshRoute remote-admin v2 nonce`. Base-key input order is
> `label || shared32 || controller_ed_pub32 || target_admin_ed_pub32`; session-key input order is
> `label || base_key32 || admin_epoch_le64`. In the nonce formula, `selected_key32` means the actual 32-byte
> key under which that message is sealed or tagged: `base_key32` for a bootstrap request, bootstrap response,
> or rollover-result response, and `session_key32` for established-session traffic. The nonce input order is
> `label || selected_key32 || outer_type_u8 || ctl_u8 || request_id_le64 || response_seq_u8 ||
> source_hash_le32`, followed by `admin_epoch_le64` only for the **bootstrap response** and
> `ROLLOVER_RESULT` domains. A bootstrap request cannot include an epoch the controller does not yet know;
> the BLAKE2b-512 result is truncated to 32 bytes for keys and 24 bytes for the XChaCha nonce. AEAD associated
> data is `outer_type_u8 ||` the exact clear RPC-header bytes in wire order `|| source_hash_le32`; ciphertext
> and tag are not repeated in AAD. The independent-reference KATs must pin every domain, both directions and
> cross-domain inequality—not merely round-trip through the production codec.

R-RA-28:

> **Settled for Slice 2:** the capacity authority always reserves the four DST_HASH bytes, alongside the mandatory
> SOURCE_HASH and origin bytes, whether or not a particular legal carrier transmits DST_HASH. Omitting that field
> never increases the admitted RPC body. Derive the governing inner budget from the storage and real air-fit
> authorities, subtract the named reserved fields and the particular carrier's path/wrapper overhead, and refuse
> oversize bodies rather than clamp. This confirms R-RA-25's conservative allowance; it does not infer the ordinary
> routing hash from a target administration key or require an administration key for open diagnostics.

Design §8.9:

> **Slice 2 decoded-result contract (QA fold-in, 2026-09-06):** decoding must retain the opcode domain as a
> typed value alongside its domain-specific result; a bare result byte is not a decoded result API. In
> particular, terminal `completed` and authenticated protocol-error `already_acknowledged` must remain
> distinct typed meanings even though both use `0x00`. Independent known-answer tests must decode that byte
> under both response opcodes and assert the different typed results. They must also reject every other
> result-code byte (`0x01..0xFF`) in an authenticated `PROTOCOL_ERROR` body, never reinterpret it as a terminal
> code or fall back to another domain. This is a constraint on the result-code field, not on the surrounding
> envelope or authentication-tag bytes; it does not broaden the separate clear/open error policy above.

Design §9:

> The `request_id` is frozen as a cryptographically random 64-bit value. It is not shortened, replaced by a
> counter, or coupled to a per-epoch cap.

Agent-roles protocol, step 4 (the base-mismatch STOP):

>    starting work in an isolated worktree, the dispatched agent verifies and records `git rev-parse HEAD` against
>    the brief's named base commit; a mismatch is a STOP to the dispatcher, never a stale-tree measurement or a
>    silent worktree repair.

**Precedence/corrections:** pre-check §4.1 supersedes its earlier all-v2-carriers-have-DST_HASH premise, its §3
blanket raw-packer cap+1-refusal requirement, and the repeated pre-ruling recommendations below §5. R-RA-28
also supersedes using 0e's larger no-hash packing limits as current admission caps. Preserve the historical
packing measurements, not their old policy interpretation. §4.2 settles the separate-repo source-list edit;
§4.3 and design §8.9 settle terminal numbering and typed domains. No B310 attachment change is authorized.
The pre-check's HAL-fake RNG-failure suggestion is qualified by the source fact below (B312); its failure
invariant remains required. The old R-RA-5 per-epoch-cap wording is superseded by design §9's non-enforced
analysis envelope. Roles' ruled board pair supersedes generic all-board instructions; C4/M3 supersede the
old deployment-cost paragraph in CODE_GUIDELINES.

## Verified source state (V1/V2)

Anchors below were checked at `cc35137`; the committed preparation diff to `9ea4947` changes no production,
test, tool or simulator source. Relocate by symbol and re-derive before editing.

| Fact | Source |
| --- | --- |
| No production `remote_body_cap`, remote-v2 KDF/nonce or codec exists. 0e has a test-local packing characterization, not a callable runtime authority. | `lib/core/` census; `test/test_radmin_characterization_0e.cpp:87`, `:122`, `:142`, `:151` |
| `DATA_TYPE_REMOTE_CMD=0xA0`, `DATA_TYPE_REMOTE_RESP=0xA1` already exist; no allocation/version change is needed. | `lib/core/frame_codec.h:802`–`:803` |
| 1b's pure decision and strict capability-owned calls are implemented; no codec is connected. | `lib/core/node_mac_rx.cpp:1938`, `:1969`, `:1978`, `:2311` |
| BLAKE2b uses a 64-byte digest then truncates; DM seal/open already wrap monocypher AEAD. Reuse the primitives/wrappers, not the DM-specific KDF label, sorted short hashes or nonce. | `lib/core/dm_crypto.cpp:23`–`:52`, `:92` |
| Public-key conversion and raw ECDH exist; `ecdh_shared` returns void and does not reject an all-zero result. The new key-derivation boundary must check it. | `lib/core/identity.cpp:43`–`:49`; all-zero accumulation idiom `:52`–`:57` |
| `dm_open`'s comment promises an output wipe on bad tag; the actual primitive writes plaintext only on a valid tag and wipes its context, not caller output. Do not rely on the comment (B313). | `lib/core/dm_crypto.cpp:92`–`:96`; `lib/monocypher/src/monocypher.c:2912`–`:2928`, `:2942`–`:2952` |
| Independent-reference literals, not production-generated expected values, are the existing KAT idiom. | `test/test_dm_crypto.cpp:26`–`:56`, `:129`; `test/test_identity.cpp:95` |
| Legacy frame is rand8 + nonce_ctr16 + ciphertext + tag; counter-floor semantics stay until Slice 9. Do not extend/import that protocol into v2. | `lib/core/admin_auth.h:30`–`:50`; `lib/core/admin_auth.cpp:35` onward |
| Air-fit and storage are different authorities; the named reservation terms are 1 + 4 + 4. | `lib/core/frame_codec.h:738`–`:748`; `lib/core/protocol_constants.h:1054`–`:1078` |
| Real inner packer validates full path depth/cursor and output span before writing. Whole-frame packer rejects outer CRYPTED without DST_HASH. | `lib/core/frame_codec.cpp:1079`–`:1093`, `:896`–`:906` |
| Mobile cross-layer delegation permits destination depth 1..3 because the home prepends a layer; one enclosed-type byte is also spent. | `lib/core/node_mac.cpp:924`–`:959` |
| A hosted-mobile local last-mile enqueue currently passes destination override zero; a matched administration identity is not proof of a wire DST_HASH. | `lib/core/node_hashlocate.cpp:1818`–`:1821`; `lib/core/node_mac.cpp:234`–`:240` |
| Existing bounded byte Reader/Writer has u8/u16/u32 little-endian operations, but no u64 operation. Compose a codec-local u64 operation from it; no shared-helper refactor. | `lib/core/meshroute_wire.h:38`–`:78` |
| RNG status is not exposed: `IHal::rand_bytes` and the device implementation return void; the latter calls void `mrrng::fill`. | `lib/core/hal.h:177`–`:183`; `lib/hal/device_hal.cpp:158`; `src/device_rng.h:46` |
| One source-list addition feeds both simulator libraries; the current cache points at this MeshRoute repo. New files are not globbed. | Simulator `CMakeLists.txt:58`–`:83`, `:108`–`:113`; `build/CMakeCache.txt`, `MESHROUTE_DIR` |
| Existing mutation dependency `b20codec` owns air-fit/length arithmetic; no DM-crypto/identity target exists in TARGET_SRC. The feature gate pins 1b's exact three-file capability ownership census. | `tools/probe_ui_model_mutations.py:246`, `:7299`; `tools/probe_features/ownership.py:58`–`:93` |

Reference pins from 1b's QA-passed evidence, **not a substitute for fresh base captures**: native
2615 cases / 111354 assertions / 0 failed; tools sweep 312; feature probe 9 cells / 114 checks / 38 controls,
0 unusable. Node: native 222072, heltec_mobile Xtensa 117912, gateway ARM 148680. Deterministic gateway:
195844 RAM / 512092 flash / 283 objects; heltec_mobile: 205684 / 1355292 / 327. The existing lus executable
is `build/orchestrator/lus`, not `build/lus`; its read-only Author hash is
`b1b1d92c541cc7f6f63864a2bcc6a355`, matching 1b. Read the actual keystone and all 36 anchors from the current
`simulation/BASELINE.md` corpus block; at this base s18 is `32afbf11` / 269517 / 0. Do not quote an older
historical table elsewhere in that file as the current anchor. Every final measurement is derived anew.

## Production shape and byte contract

Use new `lib/core/remote_codec.h` and `lib/core/remote_codec.cpp`, with the existing `MESHROUTE_NS` idiom so
both simulator namespaces compile. Expose small typed values, caller-owned spans/buffers and explicit
success/refusal results. No heap allocation, static mutable state, global constructors, Node/HAL ownership,
virtual dispatch or firmware includes. The codec is shared, not another capability consumer: do not add
`MR_FEAT_RADMIN_*` references or weaken 1b's ownership census. Existing production TUs must not include/call
the new module in this slice. Native tests call the real out-of-line implementation.

Keep one byte-layout/domain decision shared by the encode/decode and cryptographic entry points. Provide the
logical operations `remote_kdf_base`, `remote_kdf_session`, `remote_nonce`, AAD construction, typed body
encode/decode/seal/open, `remote_body_cap(RemoteCarrier)`, and checked request-ID creation. Names may follow
the local idiom within these two files; no second implementation in a fixture. Use existing identity/ECDH,
`dm_seal`/`dm_open` or their existing monocypher primitives, `crypto_blake2b`, `crypto_wipe`, and bounded wire
Reader/Writer. Remote KDF inputs are full, **ordered** controller and target public keys, never DM's sorted
short hashes. Do not change the existing helpers or treat raw ECDH as already validated.

All integers are explicitly little-endian, never host-layout memcpy of an integer/struct. Freeze named
unsigned-byte enums/constants and static assertions for the nibble split and these fixed overheads:

| Outer / opcode | Slot | Clear header in wire order | Remaining body | Fixed overhead |
| --- | --- | --- | --- | --- |
| CMD AUTH_EXECUTE `0` | 0..9 | ctl, request_id64 | ciphertext N, tag16 | 25 |
| CMD OPEN_EXECUTE `1` | F | ctl, request_id64 | clear command N | 9 |
| CMD BOOTSTRAP `2` | F | ctl, request_id64, controller_pub32 | tag16 | 57 |
| CMD RESPONSE_ACK `3`, SAFE_ROLLOVER `4`, FORCE_ROLLOVER `5` | 0..9 | ctl, request_id64 | tag16 | 25 |
| RESP OUTPUT `0`, TERMINAL `1`, PROTOCOL_ERROR `4` | 0..9 | ctl, request_id64, response_seq8 | ciphertext N, tag16 | 26 |
| RESP OUTPUT `0`, TERMINAL `1`, PROTOCOL_ERROR `4` | F | ctl, request_id64, response_seq8 | clear body N | 10 |
| RESP BOOTSTRAP `2` | 0..9 | ctl, request_id64, admin_epoch64 | tag16 | 33 |
| RESP ROLLOVER_RESULT `3` | 0..9 | ctl, request_id64, admin_epoch64, abandoned_count8 | tag16 | 34 |

CMD 6..F, RESP 5..F and slot A..E reject; so does every invalid opcode/slot pairing. Bootstrap responses
cannot use sentinel F, and bootstrap requests use F, not an established slot. Validate outer type before
interpreting ctl; a foreign DATA type is not a remote direction. Pin all byte assignments with independent
literal expectations; enums/constants are append-only after this slice.

For fixed bodies, exact length means neither a short prefix nor trailing bytes is accepted. For variable
bodies, N is the complete remaining span after that layout's fixed fields/tag: no invented command-length
field, terminator, ignored suffix, cast-before-bounds check or clamp. An extra byte in an open variable body
can be legitimate payload; do not falsely classify it as fixed-layout trailing garbage. Require a result-code
byte for TERMINAL and authenticated PROTOCOL_ERROR. Preserve bounded terminal detail bytes exactly; this slice
does not implement scheduling-delay policy, transcript counts or command validation. In particular it must
not claim to enforce the future command allow-list, dispatcher result semantics, or at-most-once execution.

Pin TERMINAL codes `00 completed`, `01 scheduled`, `02 unknown_command`, `03 refused`,
`04 output_truncated`, `05 internal_error`, `06 session_full`, `07 session_busy`; other terminal values
reject. Authenticated PROTOCOL_ERROR has only `00 already_acknowledged`; `01..FF` reject even though some
are valid terminal codes. The successful decoded result includes typed opcode/security domain and the
domain-specific meaning, not a bare shared byte. Open results remain explicitly unauthenticated. The open
PROTOCOL_ERROR envelope does not acquire authenticated already-acknowledged semantics; §8.9's separate
open-validation policy remains for its later owner. Do not invent another code allocation here.

Enforce the selected authenticated layout's tag before publishing an authenticated decoded value/plaintext.
Malformed/authentication failures return a typed local refusal with no forged plaintext result and no
fallback to open. The codec emits no console/telemetry/network error itself: silent-on-air tag failure is
the later caller's contract. A structural view of unverified header bytes is not authenticated acceptance.
Test the authenticated entry point when claiming corrupted sealed bytes are rejected; a separately valid
open envelope is inherently unauthenticated, not something the codec can recognize as a former sealed frame.
Legacy rejection uses real legacy fixtures under the old codec and proves they are not accepted as v2
authenticated requests; no heuristic legacy-prefix detector or trial-open with legacy keys is allowed.

The old `dm_open` buffer-wipe comment is not an output contract (B313): distinguish refusing to publish a
decoded result from wiping caller-owned storage, and test the new API's actual failure-output guarantee.

### Keys, nonce, AAD and checked entropy

Derive BLAKE2b-512 then truncate, not parameterized BLAKE2b-256/192. The base key binds shared32, the full
controller public key then the full target administration public key. Refuse all-zero/low-order ECDH results
before deriving/publishing a key, with controls through the existing real X25519 primitive, not just a fabricated
failure flag. Wipe intermediate shared/key/digest buffers on all relevant exits; no secret in a diagnostic.

The actual domain selects base key for bootstrap request/response and rollover result, session key otherwise.
Requests use response_seq zero; epoch is included in nonce input **only** for bootstrap response and rollover
result. Encode/open must reach that selector and the same header/AAD authority; merely testing an unused
nonce helper is not wiring proof. The source input is the stable logical **controller** SOURCE_HASH captured
from the request, including when authenticating responses; it is not a relay, next-hop or current UI identity.
Require it explicitly with no absent-source fallback. Source presence and a numerical hash value must not
be conflated. No routing lookup or live SOURCE_HASH receive enforcement is added in 2; those consumers are later.

AAD is direction byte + exactly the clear header in the table + controller source_hash LE32. Include clear
controller_pub, epoch and abandoned_count wherever that layout carries them. Do not add the implicit request
sequence zero to AAD, repeat ciphertext/tag there, or bind mutable forwarding headers. Open has no AEAD nonce/tag.

**B312, source-derived qualification of the pre-check:** `IHal::rand_bytes` is void, so a HAL fake cannot
return a production RNG failure status through it. Keep that API unchanged. Give the new codec's stateless
request-ID creator a caller-supplied **status-returning entropy function**, with caller-owned context and an
exact eight-byte destination. On success it means all eight bytes were supplied; on false, absent provider,
or a reported incomplete fill, return an explicit entropy refusal and publish no usable ID/request. A fake
provider tests that actual production creator, including failure after writing some bytes and preservation
of the caller's committed output. No retry loop, clock/counter fallback, RNG caching or per-epoch request cap.
Keep successful ID bits intact; do not silently reserve an ID value to stand in for provider failure.

This is the codec boundary's RNG-failure proof, **not** proof that today's device HAL detects/reports hardware
failure or that a console refuses before a real RF send. No production adapter may convert a void draw into
unconditional success here. The first entropy-using controller/session integration must supply and gate that
real adapter; B312 stays open for that obligation. Fake-provider entropy is labelled synthetic, never hardware
entropy qualification. Compute the birthday bound from `n*(n-1)/(2*2^64)` at `n=2^16` (approximately `2^-33`)
in the evidence; 2^16 is analysis, not a new operational limit. Tests pin all eight bytes/high bits and a
successful repeat draw with a different input, not statistical randomness or target dedup (later slices).

## Carrier admission — B308/B309 and R-RA-28

`RemoteCarrier` describes the real carrier shape, not a controller's administration key: outer DATA type/flags,
path form/depth and enclosed-type overhead as needed. Its one capacity authority derives
`min(storage authority, real air-fit authority) - reserved origin/source/destination fields - path/wrapper extras`
from the existing named constants and codec functions. Never implement it by subtracting from a copied 232,
by analogy with `dm_max_body_bytes`, or by letting omission of DST_HASH reclaim four bytes. SOURCE_HASH is
mandatory on the live RPC carrier map. Invalid descriptors/depths/cursors, missing required fields and
underflow refuse; do not return a wrapped large cap or silently normalize an invalid shape.

| Live plaintext outer carrier | Path meaning | RPC admission cap |
| --- | --- | --- |
| Home-originated same-layer request; response; hosted last mile | No path; addr_len 0 or last-mile 1 does not add inner bytes | 232, with or without transmitted DST_HASH |
| Typed mobile-to-home same-layer wrapper | One enclosed-type body byte | 231 |
| Full cross-layer request/response | Full depth 1 / 2 / 3 / 4; path overhead 2 + depth | 229 / 228 / 227 / 226 |
| Typed mobile-to-home cross-layer wrapper | Destination depth 1 / 2 / 3; path overhead 2 + depth, plus enclosed type | 228 / 227 / 226 |
| Typed cross-layer wrapper at destination depth 4 | Home would prepend a fifth layer | Invalid, **not 225** |

Derive and report a leg-by-leg map using the existing producers/packers, including legally hash-less legs,
both RPC directions and the typed wrapper's **actual MOBILE_SEND outer type** versus enclosed REMOTE_CMD/RESP.
The home's corresponding full path depth 2..4 must have the same 228/227/226 allowance as its wrapper. Do
not introduce a static controller, a new lookup, carrier reconstruction, hash attachment or B310 last-mile fix.
Authenticated/open application-byte limits subtract their own 25/9/26/10 RPC overhead from the returned cap.
Fixed bootstrap/control bodies must also fit through admission; none bypasses it by assuming it is small.

Tests distinguish three different questions, each with a named verdict:

1. **Admission:** the real new codec/admission path accepts a valid body at cap and refuses cap+1 with the
   caller's buffer deliberately large enough. An undersized test buffer is not an admission proof. Test encode
   and decode admission, using a valid independently sealed oversize fixture where authentication would
   otherwise mask the size check. Oversize is refused, never truncated or returned as a partially valid body.
2. **Physical packing:** use `pack_unicast_inner` with the real 241-byte storage span and `pack_data` with
   the real frame span. At admitted cap every live shape fits. Cap+1 on fully hash-populated binding rows
   physically refuses; identify storage versus air-fit versus structural refusal. On a same-layer no-DST_HASH
   shape raw 233 **can fit** while admission refuses 233. Execute that positive packer counterexample; do not
   claim a raw-packer refusal there or exclude a legal no-hash leg to manufacture agreement.
3. **Shape/authority sensitivity:** legal full depths versus invalid full 0/5 and invalid cursor; wrapper
   destination 0/4 refusal separately from raw inner depth 4 (which is legal as a full path). Vary source,
   destination and wrapper/path terms one at a time. Exercise the real air-fit bound under a physically legal
   outer-CRYPTED/hash-present comparison, where storage no longer binds, and the real structural rejection of
   outer CRYPTED without DST_HASH. These are labelled packing/arithmetic controls, **not** permission to add
   outer encryption to the live v2 send paths; RPC AEAD and outer DATA CRYPTED are distinct layers.

Reuse 0e's real-packer method, not its old admission interpretation. A **comment-only** correction is permitted
in `test/test_radmin_characterization_0e.cpp`: mark the old no-hash allowances as historical raw fits superseded
for admission by R-RA-28; distinguish the last-mile hash-present fixture from the current producer's absent
override; withdraw the claim that the existing REMOTE_CMD codepoint is only a not-yet-allocated placeholder.
Keep every executable token, fixture row, expected value and 0e measurement intact. New admission/wrapper
tests belong in `test/test_remote_codec.cpp`. Prove the old TU's comment-only token identity in isolation.

## Independent known-answer and wiring gate

Use `test/test_remote_codec.cpp` in the ordinary native build, with the existing doctest main and no new
test-only production define. The tests compile and execute the delivered `.cpp`, not a header-only surrogate.
Use literal expected bytes produced **before consulting production output** by an independent reference.
Do not regenerate them on test execution or derive expected data via any production KDF/nonce/encoder.

Name the evidence-contained reference instrument **remote-v2-independent-reference**. Record the complete
executable Python commands/program, input bytes, interpreter/dependency versions, reference provenance and
every resulting vector in the evidence, with a mapping to each literal native fixture. `hashlib.blake2b`
with digest_size 64 supplies KDF/nonce vectors; explicit independent byte construction supplies header/AAD
vectors. For complete sealed-message ciphertext/tag vectors, use an independent XChaCha20-Poly1305
implementation, anchored by the existing external primitive vector (e.g. the Python libsodium binding),
not MeshRoute or a second call to its monocypher library. The Author's host has no importable `nacl`, `Crypto`
or `cryptography`; the coder may provision a disposable, version-recorded reference environment outside
either repository. No system-Python mutation, vendored crypto, project dependency or opaque generated blob.
Reference unavailability is a STOP, not permission to substitute round-trips. No persistent new generator is
needed: the complete reproducible reference transcript is part of the evidence and QA reruns it.

The reference must reproduce its external primitive anchors before its remote vectors are trusted. Compare
its output with the frozen native literals; a deliberately changed expected byte must fail that comparison.
Run the reference in the full gate, not just once while authoring tests. Round-trips and two-peer agreement
are secondary coverage. Use bytewise/numeric assertions that report printable diagnostics (B311).

Minimum independently pinned coverage:

- Base KDF, session KDF, each full key/order/epoch input and BLAKE2b truncation; valid existing identity/ECDH
  path and invalid/all-zero/low-order shared-point refusal. No short-hash substitution or endpoint sorting.
- Every authenticated opcode/direction's exact ctl/header, selected key, nonce, AAD and full sealed/tag-only
  body; representative nonzero high bytes in request_id, epoch and source_hash. Both slot endpoints and all
  legal slot values are decoded; reserved slots/opcodes and invalid direction/pairings are negative cases.
- Pairwise nonce inequality across the authenticated domain set at the same request ID; include same opcode
  nibble in opposite directions, ACK versus execute, output versus terminal versus protocol error, bootstrap
  versus rollover result, different slots and response sequences. Open domains have no nonce, not a fake zero
  nonce that participates in an authenticated inequality claim. Distinct controller SOURCE_HASH values with
  the same credential/session key produce distinct nonces; changed source/header/key fails authentication.
- Bootstrap request has no epoch input; bootstrap/rollover responses change nonce/tag when epoch changes.
  Pin that ordinary requests do not quietly acquire an epoch field, and that response_seq is one byte.
- Every fixed-layout exact-length boundary, variable-body length/caller-span/admission boundary, tag-only
  empty ciphertext, open request/response literals, and complete-field byte order. Short inputs never cause
  out-of-bounds reads/writes; sentinel output buffers expose partial-success or stale-result publication.
- Every byte of representative sealed frames corrupted in turn: structural refusal or failed authenticated
  open, no authenticated plaintext/result and no open fallback. Cover tag, ciphertext and clear-header bytes
  separately; never score open plaintext as if it had an integrity guarantee.
- All eight terminal meanings through the real result decoder; independently sealed TERMINAL `00` and
  PROTOCOL_ERROR `00` decode to different typed meanings. Authenticate a fixture for each invalid protocol-error
  result byte so rejection proves the code-domain check, not merely a broken tag; terminal 08..FF also reject.
  Preserve detail bytes and explicit open/authenticated classification; no bare-byte result API.
- A real old `admin_cmd_seal` fixture still opens with the old decoder but fails v2 authenticated acceptance;
  invalid v2 authentication is never retried as open or as legacy. Do not modify the legacy tests/codec.
- Every live carrier row, reservation/packing counterexample and invalid wrapper depth above; checked entropy
  success/failure and all 64 ID bits; independent birthday derivation. No simulated duplicate table or runtime
  controller state is introduced merely to claim a future shared-controller execution test.

For each claim, name which public codec entry point is exercised and its mutation falsifier. Tests of KDF,
nonce, AAD or cap helpers alone do not prove that the actual seal/open/admission path uses them. Require
deleted/bypassed call controls at those in-module call sites. No real RX router or transmitter calls this codec
yet: state that absence honestly rather than constructing a fake runtime consumer to satisfy a wiring label.

## Mutation selectors, controls and instrument hygiene

Add `radmin2codec -> lib/core/remote_codec.cpp` to the existing per-file mutation harness, plus its battery
and required dispatch-table entry. Keep executable decisions in that TU; the header is declarations/types/
constants, not a second untested policy implementation. If an executable header decision is genuinely needed,
QA must review the additional per-file target/fence before proceeding. No cross-file mutant/restore shortcut.

Derive and report **both selectors**, then gate their full union:

- **Changed-source:** new `radmin2codec`; no existing production file is edited. The new header's dependency
  reach and the comment-only 0e hunk must still be accounted for, not called semantic source changes.
- **Historical/dependency:** at least existing `b20codec`, because `remote_body_cap` depends on the real
  air-fit/length authority, despite `frame_codec.h` being unchanged. This qualifies the pre-check's
  no-touched-existing-file shorthand. Derive any further genuine acceptance dependency. Do not include RX,
  custody, routing or timer batteries solely because their files share an include or an old arc name.
  Existing DM-crypto and identity KATs run in the full native binary; TARGET_SRC currently has no mutation
  battery for those modules. State that fact instead of inventing an existing target or a coverage claim.

Baseline union is **new radmin2codec + full b20codec**, not an empty pair of selectors and not just new entries.
Map independent controls to omitted/reordered/truncated KDF inputs and labels, wrong selected key, each
nonce/AAD domain field, caller bypasses, all-zero acceptance, wrong endian/width, reserved opcode/slot acceptance,
length/admission checks, source/destination reservation terms, wrapper byte/depth, authenticated-open fallback,
ignored tag result, typed-result domain collapse/invalid-code acceptance, and ignored entropy failure.
Cap controls must reach both storage- and air-governed cases; no permanently storage-masked air control.

**Declare classification before running controls:** native mutations must compile, execute and fail the
intended named assertion. Compiler/linker failure, crash, timeout, missing worker, unreadable output, wrong
match count, or a green mutant is UNUSABLE/gate failure, never RED. This slice adds no inverted compile-error
control class. Static allocation asserts remain useful build invariants; do not count a compiler rejecting
one as an executed native mutation. Existing feature-header refusal controls keep their separately declared
production-diagnostic classification; it does not extend to the codec battery.

Every edit matches exactly once; worker clean baselines derive their own native counts; source restoration
and real-tree hashes must match. Preserve all existing controls. Update the native PIN only from measured
per-case additions with the report line `PIN re-synced? YES — <derivation>`. A failed new control is not
rescued by an out-of-fence production edit or weaker assertion.

B286 and B311 are known open instrument debts, not scope additions. Before `--workers=2`, measure free disk
and actual scratch-copy inputs: 1b measured about 5.1 GB per worker against 5.8 GB free. Its sanctioned
workaround is available: an isolated staging copy excluding only identified non-input artefacts, with every
tracked/untracked build/test/tool input (including the new files) byte-verified against the delivery tree
before and after. Do not mutate/clean the owner's source/build directories. Record exact exclusions and
manifests; do not silently reduce the selected battery or gate against HEAD without the new files. Keep
non-UTF-8 diagnostics readable in the test assertions; any worker loss is a STOP and that affected full
battery must be rerun after an in-fence remedy. Do not repair the harness decoder or copy policy in this slice.

## Fence

Allowed coder changes in **MeshRoute**:

- New `lib/core/remote_codec.h` and `lib/core/remote_codec.cpp`: only the stateless codec, types/constants,
  carrier authority and explicit-input entropy boundary described above.
- New `test/test_remote_codec.cpp`: literal independent vectors, real-codec/packer/entropy tests and bounded
  test-local helpers; no alternate production codec or runtime role override.
- `test/test_radmin_characterization_0e.cpp`: **comments only**, the historical-cap/carrier/type corrections
  named above; every executable token and existing assertion unchanged.
- `tools/probe_ui_model_mutations.py`: new per-file target/battery wiring, controls and measured native PIN;
  no orchestrator, copy, parser, verdict, worker-count-default or source-restoration redesign.
- `docs/superpowers/evidence/2026-09-06-radmin-slice2.md`: complete evidence, including the independently
  reproducible reference instrument, its inputs/outputs and any proposed register rows.

Allowed change in the **separate simulator repository**:

- `CMakeLists.txt`: exactly **one added source-list line** for
  `${MESHROUTE_DIR}/lib/core/remote_codec.cpp` beside the existing crypto TU in `_meshroute_core_srcs`.
  No per-variant duplication, wrapper/factory/flag/configuration or simulator-behaviour edit.

Expected delivered diff: two new production files, one new native TU, one comment-only old native TU,
one existing mutation tool, one evidence file; simulator one line. No file move. No `src/`, Node header/TU,
HAL/entropy provider, existing crypto/identity/frame codec/constants, feature gate, `platformio.ini`, variants,
command inventory, NV, wire-version, timer/capacity, simulation scenarios/anchors, companion, QA ledger,
rulings/register/bench/design/MEMORY/tracker edits by the coder. These last documents are Author-owned.
No temporary reference dependency, generated vector file, census instrumentation or scratch manifest is an
unaccounted delivered artefact. The owner commits each repository; the coder never commits either one.

## Prediction and full gate

Before editing, preserve base native, full corpus/anchors, all probe/tool pins, ABI and deterministic board
captures at the paired bases. Derive source reach and write the prediction first: zero new runtime codec calls
or entropy draws, zero remote-v2 events, **36/36 byte-identical streams**. 1b's zero-remote-arrival census is
prior evidence, not this run's measurement. Report current per-stream remote-type/event reach using the
existing isolated-census idiom if needed; never instrument the delivery tree or an anchored stream run.

Force the new simulator dependency to compile for **both** `meshroute_core_normal` and `meshroute_core_gw`.
Record commands/actions, paired source paths, actual objects/archive members and their symbols/namespaces.
A build that reports nothing to do, or merely adds an uncompiled line, proves nothing. Do not force a runtime
call to retain the object. The baseline executable hash above is a comparison, not permission to reuse the
old binary. New unreferenced archive members should not add live code/data; record final binary hash and
attribute any metadata-only difference separately. Every stream must still match the base and current anchors.

Both ruled boards must compile the new TU but acquire **zero RAM and zero live flash delta**: no consumer,
Node state, static storage or initializer was added. Require unchanged live code/data sections and no new
reachable codec symbols; attribute symbol/layout metadata separately from loadable bytes. New object/archive
members are expected (derive the inventory-count delta, normally one object per board), unlike Slice 1's
same-object scaffold. Their presence is compilation proof, not flash use. Compare fixed-identity base/final
captures at identical checkout/build paths using `measure_board.py`; preserve B254/B262's debug/path distinction.
An unexpected live byte change is a STOP for attribution, not an invented flash tolerance. Node's three ABI
pins and existing staging slot remain unchanged; do not repin size assertions to make this pass.

Run gate families sequentially/exclusively, using only each instrument's supported internal parallelism.
Firmware board gate is **gateway + heltec_mobile only**; the warning census's own pinned environment set is
the sole exception. No whole-matrix firmware sweep or unreviewed instrument-policy change.

1. Paired base/status/configuration proof; fresh baseline captures; source/consumer census and prediction.
2. Execute **remote-v2-independent-reference**, its external primitive anchors and fixture comparison/control;
   then `pio test -e native` **and** `./.pio/build/native/program`. Derive real cases/assertions/zero failures,
   additions by case, all vector/negative-case results and the PIN change. No wrapper zero-case claim.
3. Full derived mutation union, at least `radmin2codec` and `b20codec`, complete targets sequentially with
   `--workers=2` and measured scratch headroom/provenance. Record every control, match, named assertion,
   classification and source restoration. No selected-only green summary substitutes for full union results.
4. Forced paired simulator build and object proof; `python3 tools/run_corpus.py --jobs=8 --require-anchors
   --out <fresh-output> --lus <paired-build>/orchestrator/lus`. Report every before/after row, output hashes,
   events/assertions/semantic totals, current s18 keystone and before/after executable hashes. No re-anchor.
5. Full `python3 tools/probe_board_abi.py` and `python3 tools/probe_b278_row_abi.py`, with controls;
   `python3 tools/measure_board.py pair --jobs=2 --output <capture>` for deterministic base/final ruled-pair
   captures. Report RAM/flash/sections/objects/symbols, namespace/compile proof, provenance and attribution.
6. Complete default `tools/probe_console_sink/run.sh`, `tools/probe_inbox_verbs/run.sh`,
   `tools/probe_firmware_ui/run.sh`, `tools/probe_custody_usb/run.sh`, `tools/probe_ble_line/run.sh`, and
   `tools/probe_features/run.sh`; all controls and pins remain exact. Feature `--no-neg` must still end
   PROBE-ONLY, not a gate PASS; its source-file count may grow, not its ownership contract/check pins.
7. `python3 -m unittest discover -s tools -p 'test_*.py'`; bare
   `python3 tools/gen_command_inventory.py` and `python3 tools/gen_command_inventory.py --check`.
   No firmware command anchor moves: no inventory regeneration or `--write` here.
8. `tools/warning_census.sh`, `python3 tools/check_a0_matrix.py`,
   `python3 tools/check_data_type_literals.py`, with the existing supported controls; zero new warnings and
   clean Node size assert / `-Wreorder`. Comment-only 0e proof, both repos' `git diff --check`, before/after
   source integrity and complete modified/untracked inventories. QA independently reruns every instrument.

## STOP conditions

Stop and report to QA before proceeding or widening scope if:

1. either exact committed base is missing/different, either measured repository starts dirty, the paired
   simulator points at the wrong MeshRoute tree, or concurrent edits cannot be identified and shown irrelevant
   to the measured inputs;
2. an implementation requires a new opcode/result allocation, owner policy, wire-version/re-anchor,
   carrier/hash/routing change, consumer, Node/HAL/NV/timer/state change or other out-of-fence edit;
3. a legal carrier's derived allowance contradicts R-RA-28, an invalid wrapper depth is admitted, or admission
   and physical packing cannot be proved separately without altering existing packers;
4. independent reference vectors/provenance are unavailable or disagree; only self-round-trips pass; the real
   encode/open paths bypass the tested KDF/nonce/AAD/cap/refusal authority; typed opcode/security domain is lost;
5. RNG failure is hidden by a void-to-success adapter, fallback ID, unreported incomplete draw or a claim
   that synthetic entropy tests prove real device failure handling or RF refusal;
6. any corpus row/anchor/semantic output changes, either simulator variant did not compile the new TU, RAM/
   Node ABI moves, or an unexpected board/executable movement remains unattributed;
7. any required control is green/vacuous/multi-matched/unusable or misclassified, a worker is lost, reference
   comparison is not reproducible, source restoration fails, or a pin moves without a measured in-fence derivation;
8. a warning, failed standing gate, unaccounted modified/untracked file, inventory-regeneration need or
   unexplained concurrent input change remains. Do not repair another instrument/feature to force a PASS.

## Evidence and Author handoff

Write `docs/superpowers/evidence/2026-09-06-radmin-slice2.md`. Include both exact bases and full starting
status; paired CMake source resolution; source-derived API/domain/carrier map and correction of pre-check
hypotheses; all independent-reference commands/versions/inputs/outputs and fixture mapping; primitive anchors
and comparison control; literal wire/result/negative-case tables; actual codec call-site wiring; entropy
scope/deferral and computed birthday bound; before prediction and final per-stream proof; every fresh gate
command/output; both mutation selectors, exclusions and full union; resource staging manifests; exact
`PIN re-synced? YES — <derivation>` line; both board deltas and both simulator variants' object/symbol proof;
0e comment-only proof; per-item STOP audit; both repositories' diffs and complete untracked inventory, including
all new codec/test/evidence files. Put any finding in a proposed register row with measurement, not only prose.

**Metal residue: none (M2).** No runtime consumer or hardware path changed. This does not close older bench
debts or the suspended static/gateway legacy `rcmd` round trip. After QA PASS, the Author lands design §19/§19.1
Slice 2 software-complete and the exact frozen codec allocations, B308/B309 closures only from their actual
admission/packer/wrapper evidence, the codec half of B312 with real entropy integration still open, and
MEMORY/tracker plus any new findings. Preserve B310 parked, B286/B311 as separate instrument work, and
B313's existing-crypto comment correction outside this slice.
Record both owner commits when they exist; never invent either hash. Leave all work uncommitted (D4).
