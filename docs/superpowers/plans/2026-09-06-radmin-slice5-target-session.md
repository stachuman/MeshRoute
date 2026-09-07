<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 5 — target session, admission and bootstrap · advance brief · 2026-09-06

**Status: PRELIMINARY QUALITY-AGENT PASS 2026-09-06 — no fold-ins. Slice 4 QA PASS; closure facts filled 2026-09-07. NON-DISPATCHABLE pending the owner's landing/preparation commit, Author base pin and final QA gate.**
`model: opus`. Author documents; QA gates/dispatches; the owner commits. The owner permits advance drafting,
not overlapping implementations. Slice 4's report, QA verdict and owner closure come first.

QA verified the Author decisions and corrected S5-A1–S5-A3 in its ledger with the old claims visible.
Those intake rows remain open until Slice 5 closure; this is a preliminary brief verdict, not a software
PASS. Section 1's delivered bindings are now filled; after the owner commits these landings and the
Author pins that hash, QA runs the final brief gate and QA dispatches Slice 5
on the main tree under the clean measured-start and input-audit requirements below.

Authority: `docs/superpowers/plans/2026-09-06-radmin-slice5-precheck.md`; R-RA-31, R-RA-22 and the preceding
remote-admin rulings in `docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`; design
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §§6.5–7.3, 8–10, 14–15, 19–19.2.
Pre-check §6.3–6.7 Author decisions are §4 below. This is a feature slice, not a refactor or legacy cleanup.

## 1. Closure bindings — results filled; final dispatch base PENDING

**MeshRoute dispatch base: PENDING — the owner's commit of these Slice 4 landings and filled preparation.**
Slice 4 implementation/evidence and earlier preparation are committed at
`19c10bfc4bd3d57ec7257b27996fc4b6f6900ad0` (main, clean before these Author edits).
That is the source binding below, NOT permission to start with uncommitted landings. The owner commits,
the Author explicitly pins the resulting hash, and QA verifies the final brief. Do not reuse a0ff994.

**Simulator base: `868888419c7cc250d7019860d3403a7721ade1fc`**, clean at this landing inspection,
repository `/home/staszek/lora-universal-simulator`. This slice authorizes one new
core source-list entry in that separate repository (§5). Record its absolute path, both repository bases,
both complete diffs/statuses and the measured build's MESHROUTE_DIR, which must name the measured checkout.

Bindings below are from Slice 4 evidence `docs/superpowers/evidence/2026-09-06-radmin-slice4.md`, the
owner-relayed independent QA PASS, source at 19c10bf and retained artifacts inspected read-only by the
Author. They are not a new Author-run software gate. Reproduce them in the clean pre-edit capture.

| Required binding | Closure entry |
| --- | --- |
| Slice 4 evidence, QA verdict, owner closure hash | Evidence above, independent QA PASS with no fold-ins; implementation 19c10bf. Final landing/preparation hash PENDING as above. |
| Both repository bases and clean measured-start policy | Paths/hashes above; measured MeshRoute /home/staszek/MeshRoute. Simulator cache MESHROUTE_DIR=/home/staszek/lora-universal-simulator/../MeshRoute. No dirty-input exception. |
| Native and tools | 2763 cases / 118344 assertions / 0 failed; Slice 4 adds 61 / 1442. Tools 329 OK. Six probe bindings below. |
| Actual ABI and deterministic captures | Node size/alignment: native 222072/8, heltec_mobile 117912/8, gateway 148680/8. TimerWheel kCap=91, 824 bytes on all three ABIs. DeviceHal: native 4400 bytes (actual binary DWARF), both board g_hal symbols 4376 bytes. Captures and flags discipline below. |
| Delivered target/client seams | firmware_commands.cpp:239–313: DeviceAdminIdStore/DeviceAclStore save only NV; DeviceAdminSeed checks the void draw for nonzero; admin_stores_boot_report_console reads/reports only. AdminIdService (identity header :202), AclService (:226), MgmtKeyService (:200), TargetService (:264): no running Node activation. Slice 5 adds prepare/commit/discard ahead of durable saves, including recovery; preserve Slice 4 client services/scratch/wipe. |
| Census, axes and projections | Literal seven-file contract and six profile rows below; inventory 204 rows, union 53 names, parser 7 per profile. |
| Mutation and remaining instruments | Complete configured source/test map is tools/probe_ui_model_mutations.py at 19c10bf, SHA256 a7a9e8fdd1fcfbf64d410aa920b1f41fb9d76444bde82d87a628c410aa64fc5c. Existing test-file/name selectors remain authoritative; derive BOTH Slice 5 selectors under §7, not the old Slice 4 union. Warning/checker pins below. |
| Finding allocation | B323–B326 prior evidence intake reconciled; B327 closed, B328–B330 open. S5-A1–A3 = B331–B333, still open; HOME-A1/A2 = B334/B335. Next free B336, subject to current register recheck. |

### 1.1 Standing instrument and profile bindings

| Instrument | QA-passed Slice 4 starting pin |
| --- | --- |
| Firmware UI | 223 controls RED; retain its per-arm rows from the runner |
| Console sink | profiles 6; checks 720; structural 50; BLE guard 905; ownership 6; ownership controls 3; controls 99; unusable 0 |
| Inbox verbs | ACCEPT 146 checks / 30 RED; CLIENT 178 / 33; unusable 0. Independently compiled reduced profiles: neither defines OLED (B328), NOT complete board-define parity. |
| Custody USB | 27 checks / 10 RED |
| BLE line | 40 checks / 8 RED |
| Features | 9 configuration cells / 118 checks / 52 RED (33 ownership controls included); no-controls ends PROBE-ONLY |
| Node ABI / B278 record probe | 191 checks / 9 RED; 42 measurements / 6 RED, respectively |
| Warnings | gateway_heltec 173; gateway_heltec_v4 178; heltec_mobile 177; heltec_v3 177; heltec_v4 182; heltec_v4_mobile 182; zero switch warnings |
| Other standing checks | check_a0_matrix.py and check_data_type_literals.py PASS; both repository whitespace checks PASS |

Generator PROFILES at tools/gen_command_inventory.py:294 is a literal typed product table; preserve
every existing axis and env mapping. In row order full_oled/full_headless/gateway/gateway_oled/mobile/
mobile_oled: ACCEPT = 1/1/1/1/0/0; CLIENT = 0/0/0/0/1/1; router/help counts = 44/43/41/42/40/41.
Parser has seven names per row, disjoint from router; all-profile union is 53. These are device profiles,
not native/simulator roles. Slice 5 adds no primary verb or BLE policy change.

The complete approved multiset is tools/probe_features/ownership.py at 19c10bf, SHA256
00ef927efbf756356f78cd3ca5ccd102b0b9eaf52cdffa68769ae442afce5b08. Its seven files contain respectively
mr_features.h 9 entries, node.h 3, node_mac_rx.cpp 6, firmware_commands.cpp 6, firmware_commands.h 2,
fw_main.cpp 4, firmware_help.h 2 (32 total). Exact expressions/owners matter, not this count alone.
Extend that literal contract for the fenced Slice 5 owners; never auto-learn a replacement from edited code.

Slice 4's reproduced union, for provenance only: changed-source devicenv 42, cfgparse 8, sliceDtoken 2,
radmin4key 28, radmin4targets 31, radmin4verbs 30; historical/dependency radmin3id 23, teamkeyring 68,
radmin3acl 36, radmin3verbs 28. Total 296/296 RED, zero unusable. Slice 5's union is separately derived.

### 1.2 Resource and simulator starting captures

| Environment | RAM bytes | Flash bytes | Objects | Slice 4 delta RAM / flash |
| --- | ---: | ---: | ---: | ---: |
| gateway (ruled ARM) | 195844 | 531004 | 284 | 0 / +32 |
| heltec_mobile (ruled Xtensa) | 207740 | 1367448 | 328 | +2056 / +12156 |
| xiao_mobile (Slice 4 one-off only) | 172572 | 664636 | 284 | +2056 / +86992 |

Retained captures: .pio-measure/s4-final2-pair/{gateway,heltec_mobile} and s4-final2-xiao/xiao_mobile;
compare their base siblings s4-base-pair and s4-base-xiao only under the standing same-path rules.
Each environment's manifest.json/compiler-state.json records the real CC/CXX/LINK identities, versions,
templates and wrapper state; do not substitute a guessed optimization flag or a pure-service stack-probe
command for effective board flags. Slice 5 captures actual expanded flags again BEFORE edits and retains
the same platform/core configuration, fixed identity and .pio-measure/env/<env> paths. No platformio.ini,
optimization/toolchain or B330 size-control change is authorized. Its scarce nRF52 client flash is an open
product cost, not permission for a third standing board or silent scope expansion.

The current BASELINE.md keystone is 32afbf11 / 269517 / 0. Slice 4 reproduced 36/36 anchored streams;
lus md5 b1b1d92c541cc7f6f63864a2bcc6a355, zero post-edit build actions, with a successful five-action
recompile control. Slice 5 MUST rebuild both core variants and changes the executable while preserving
those streams. Do not carry forward Slice 4's no-build/no-binary-movement prediction.

Re-verify all source anchors by symbol against that base. A pending entry is a STOP for dispatch, not an
instruction to the coder to invent its value. Provide the reviewed brief out of band if needed; no dirty
preparation exception in a measured tree. Capture base instruments BEFORE editing production/tests/tools.
The coder records input manifests and audits concurrent changes, including Markdown read by a gate;
no silent repair, repin, cherry-pick, commit or retrospective base measurement.

Drafting provenance: Slice 4 implementation changes appeared in the shared checkout during this Author
turn. Author edits are this brief, the maintained register/design/bench and MEMORY/tracker only; no
production/test/tool or QA-ledger edit, build, regeneration or implementation gate was performed.
Original drafting source facts were anchored to a0ff994, not the evolving uncommitted Slice 4 tree; the
2026-09-07 fill-in above and refreshed §3 bind the committed implementation at 19c10bf. QA/coder
retain ownership of their concurrent-input audit; this note does not waive a STOP or certify that audit.

## 2. Verbatim authority pins

R-RA-31:

> **Owner:** *"Agree - Slice 5 answers bootstrap on air, ABI re-pin authorized"*

R-RA-22:

> **Settled managed static/gateway profile:** 16 `SeenRequestRecord`, 4 `TranscriptHeader`, 8 `TranscriptChunk`,
> 2 `IngressOperationHeader`, 2 `IngressBodySlot`, 4 `OpenStagingSlot`, and 2 `DeferredActionRecord` rows:
> **2,968 bytes** from the 0e candidate layouts.

Design §7.2:

> Bootstrap does not dispatch a command and never changes the epoch. It is needed after first provisioning,
> controller or target reboot, a stale slot/session cache, or a lost rollover response; it is not a
> per-command challenge round trip. Because it is read-only, replaying a captured bootstrap request cannot
> rotate or disrupt a later session.

Design §10:

> The target keeps a bounded authenticated session table per ACL slot, keyed by `request_id` (the physical
> storage may be one shared bounded pool). It also retains the original authenticated request tag as the exact
> 128-bit request fingerprint and the stable logical controller `SOURCE_HASH` authenticated by that request.

Design §10:

> Session records are not silently evicted while their session key remains valid. This prevents a captured old
> request from becoming executable again merely because a small ring wrapped. Per-slot rollover invalidates
> every old request for that slot cryptographically and is occasional bounded maintenance, not a
> command-to-command token chain.

Design §15:

> Expiry uses one shared earliest-deadline scan. `TimerWheel::kCap` grows exactly once, 91 → 92, which the 0e
> faithful mirror measures as +8 bytes on all three ABIs. No record class receives a private timer ID.

Agent roles, step 4:

>    starting work in an isolated worktree, the dispatched agent verifies and records `git rev-parse HEAD` against
>    the brief's named base commit; a mismatch is a STOP to the dispatcher, never a stale-tree measurement or a
>    silent worktree repair.

R-RA-31 permits bootstrap response TX only: no execute/output/terminal/ACK/rollover/protocol-error reply.
R-RA-27's strict owners and pre-tail receive ordering remain; R-RA-13 mandates SOURCE_HASH; R-RA-25/28 cap
and packing rules remain. No wire/schema bump, remote dispatcher context, controller state or delegated
request/custody consumer. Those belong to 6/7b/8a/8b. No new bench part (design §19.1).

## 3. Verified preparation state and corrections (V1/V2)

| Source verified at Slice 4 implementation 19c10bf | Consequence |
| --- | --- |
| `node_mac_rx.cpp:1949`–`:1982`; `node.h:154`–`:163`, `:2944` | ACCEPT and CLIENT feed one legacy slot. ACCEPT changes to v2 admission; the slot/helper/drain become CLIENT-only. Existing CLIENT copy/clamp/output stays unchanged until 8a. |
| `node_mac_rx.cpp:2314`; the pure radmin_rx_owner decision | Keep role forwarding ahead of the owned arms and both arms ahead of outer encrypted/relay open and the fail-closed tail. A none decision consumes nothing. |
| `src/fw_main.cpp:1743` legacy drain, `:1749` plaintext scratch | Firmware has its own static RemoteInbound scratch and, inside the legacy sealed-response arm, a static pt[241]. Account for each symbol actually allocated in the base; removing the Node slot alone is not the full board-RAM delta. |
| `src/firmware_commands.cpp:239`–`:313`; firmware_admin_identity.h / firmware_admin_acl.h | Target NV adapters save records but install no running identity/ACL. Session installation is new feature work, never a core→NV read. |
| `lib/core/remote_codec.h:266`–`:345`; identity.cpp:47 | Codec/KDF/nonce/checked low-order boundary already exist. The ECDH adapter reads Identity.x_secret; use one explicit wiped transient adapter for the resident pair, not a second ECDH/KDF implementation. |
| `lib/hal/timer_wheel.h:25`; `lib/hal/device_hal.h:175` | Wheel storage is inside DeviceHal, NOT Node. Raising kCap affects the mobile HAL too. Unchanged mobile sizeof(Node) does not imply unchanged mobile RAM/flash. R-RA-22's global +8-byte wheel price remains binding. |
| `test/test_timer_wheel.cpp:81`–`:82`; test_radmin_characterization_0e.cpp:330; test_node_join.cpp:5268 | Three executed tests assert the old timer boundary. Update these obligations explicitly; never delete the unrelated mobile-aging proof or pretend the historical 91→92 mirror now prices 92→93. |
| `test/radmin_0e_candidate_types.h:180`–`:260`; design §10 | The old 32-byte seen candidate omits the 16-byte tag; a 48-byte record is mandatory. Retaining a route beyond ingress expiry costs additional explicit bytes. ACK may release transcript/body, NEVER the same-epoch seen fingerprint. |
| `node_mac.cpp:887` send_xl_ack; frame_codec.h:1335–1341 | Reverse the complete received layer path and retain source/carrier/cursor facts. Four full layers imply at most three destination hops. No generic peer lookup may replace captured source identity. |
| `node_hashlocate.cpp:1610`; node.h:1643 | send_by_hash's counter return is not its complete admission result; use SendDispatch to distinguish queued, parked and refused. R-RA-31 adds no app_dm argument to this API. |
| simulator CMakeLists.txt:58–83 | Add remote_session.cpp to the shared source list for both namespaces. New Node layout changes the executable; predict unchanged streams, not identical binary or zero rebuild actions. |

Register intake S5-A1/S5-A2/S5-A3 (§4.7) records the RAM/pinning, ACK-retention and send-result corrections.
The pre-check's ≈+1650 gateway / mobile ±0 figures are hypotheses, not acceptance pins. In addition,
bootstrap is read-only: it must not consume a seen-execute record or change epoch when staging a reply.
Case-4/case-5 verdict tests are not evidence that Slice 7b's ACK/full/rollover wire producers exist.

## 4. Author decisions resolving pre-check §6.3–§6.7

### 4.1 Residency and installation (§6.3)

Choose core-resident images, mirroring set_crypto_identity: one administration X25519 secret + full
Ed25519 public key (64 bytes), a ten-row live ACL image and ten uint64 epochs. No resident seed, full
expanded Identity, per-slot base/session-key cache or NV record in core. Derive keys through the codec
into bounded wiped transients; the messaging identity never substitutes for the administration root.

The entire new RemoteSession state is ONE ACCEPT-only Node member block. Types/services themselves
are namespace-correct pure core; they take explicit time, inputs and seams and never include src/ NV.
Firmware translates the validated NV records through ONE conversion path and installs them at boot
and after durable mutation. Empty/unreadable/unsupported root or ACL disables accept readiness; no
implicit provisioning, fallback key or active owner. Ordinary regen does not replace this pair/ACL.

**RNG isolation:** an unprovisioned Node, including every existing simulator scenario, must perform ZERO
new epoch draws, timer arms or session telemetry during initialization. Do not generate ten epochs
unconditionally in Node::on_init: that perturbs all deterministic scenario RNG streams. Only a valid
installed root plus valid ACL enables initialization. Mint the ten epoch candidates through the HAL
draw and explicit nonzero-material check; no epoch 0 in service, clock/counter/retry fallback or true
wrapper around a void draw. B312 remains open: nonzero material is not a hardware health guarantee.

Use a complete candidate before publishing readiness. On cold-boot entropy failure keep acceptance
disabled, wipe temporary keys and print the runtime boot error below; do not change NV. Current-epoch
requests fail authentication after a successful reboot/rotation because the session key changed, not
because a mutable counter comparison happened to reject one fixture.

**Durable/live ordering:** prepare every fallible live update before the NV save; publish it only after
save succeeds. The pre-check's simple post-save install call must not hide a fallible RNG draw after
durable commit. Extend the target services with an explicit prepare/commit/discard live-install seam:
prepare validates the candidate, derives its pair and obtains needed fresh epochs without mutating
Node; failed preparation returns typed `runtime_unavailable` with zero NV writes. Failed NV save
discards the plan and preserves the running ACL/pair/epochs/seen/ingress exactly. After successful save,
commit is non-failing and completes before any success line. Plans are per-call wiped transients, not
another resident image. Pure-service tests use an explicit fake live seam; firmware always binds Node.

Same-value/no-op changes do not draw, save, reinstall or invalidate. ACL change/removal/replacement
invalidates only affected slots and their work AFTER durability; unchanged slots and their epochs
remain exact. Added/changed occupied slots receive fresh prepared nonzero epochs; removed slots become
unusable. Root generate/rotate/recovery installs the new pair and invalidates all old sessions while
preserving the ACL; do not clear epochs and accidentally run epoch 0. Reset/ordinary mutation failures
do not install empty images. Preserve the existing non-atomic-flash qualification (B317): unchanged
running state after failure does not promise unchanged physical bytes on a remove-before-write backend.

Keep the two existing target boot-report lines, then add exactly one runtime line on ACCEPT firmware:
`> admin-session boot state=<ready|disabled|entropy_failed> slots=<validated occupied count or 0>`.
Disabled covers absent/invalid/io_failed prerequisites; preceding record lines retain the detailed cause.
No keys/epochs/fingerprints in this line. Runtime-preparation refusal is
`> admin-id err runtime_unavailable` or `> acl err runtime_unavailable` on the supplied USB sink;
old successful grammar/output and the BLE refusal remain unchanged. No new primary verb.

### 4.2 Real state layout, N and retention (§6.4)

Freeze this candidate for compiler measurement before production state lands. Sizes below are arithmetic
targets, not measured ABI results. Static-assert field offsets/size/alignment on native/ARM/Xtensa;
re-derive actual Node placement and board deltas from the closure, not from the 0e aggregate.

| Type | Explicit fields/offsets | Size / align |
| --- | --- | --- |
| AdminAclRow | ed_pub[32] @0; role u8 @32; reserved u8 @33 | 34 / 1 |
| ReplyRoute | layer_ids[4] @0; n_layers u8 @4; cur u8 @5; carrier u8 @6; origin u8 @7 | 8 / 1 |
| SeenRequestRecord | request_id u64 @0; admin_epoch u64 @8; first_seen_ms u32 @16; source_hash u32 @20; request_tag[16] @24; controller_slot/result_code/state/transcript_slot u8 @40/41/42/43; reserved[4] @44 | 48 / 8 |
| SeenEntry | SeenRequestRecord @0; captured ReplyRoute @48 | 56 / 8 |
| IngressOperationHeader | request_id u64 @0; expires_at_ms u64 @8; source_hash u32 @16; body_len u16 @20; seen_index u8 @22; ctl u8 @23; ReplyRoute @24; partition/state/body_slot u8 @32/33/34; reserved[5] @35 | 40 / 8 |
| IngressBodySlot | bytes[233] @0; reserved u8 @233; len u16 @234 | 236 / 2 |
| OpenStagingSlot | request_id u64 @0; expires_at_ms u64 @8; peer_source_hash u32 @16; ReplyRoute @20; kind u8 @28; controller_slot u8 @29; reserved[2] @30 | 32 / 8 |

ACL roles preserve Slice 3's typed values; empty rows/reserved bytes are canonical zero. Empty/state flags,
not request_id==0 or source_hash==0, express absence: the codec permits numeric zero with presence true.
Same-layer ReplyRoute zeroes path bytes/count/cursor; cross-layer retains all validated received fields.
origin is diagnostic only, never a reply identity. Preserve the route with the seen entry after releasing
ingress; a retry cannot replace the first-admitted authenticated source or captured return metadata.
Ingress uses its own captured route for pending work; keep index/length associations validated.
The path is validated routing metadata, NOT AEAD-authenticated data; mutable relay/path headers remain
outside the cryptographic binding. Capturing it does not turn it into an authentication claim.

**N = 16 TOTAL seen entries per target, shared across ten ACL slots**, keyed by (slot, request_id), with
epoch and the exact original 128-bit tag/source checked. It is NOT 16 per slot (160 entries). A single
session can occupy at most 16; under sharing, remaining capacity depends on the other sessions. Report
that distinction as the future rollover-cadence cost, not a guaranteed 16 requests for every concurrent
credential. Do not silently introduce quotas/eviction to promise a fixed per-slot cadence.

Seen fingerprints are the received authenticated tags, not R-RA-29 display fingerprints, recomputed
plaintext hashes or short routing hashes. Verify authentication BEFORE lookup decisions affect state.
For absent IDs reserve seen + ingress atomically or change neither; lack of the later transcript capacity
means NO dispatch in this slice. Existing ID/same tag returns replay verdict; different tag/source returns
ID-reuse refusal after authentication. Preserve the first accepted row. Acknowledged-row and capacity-full
states return the typed case-4/case-5 verdicts only; no terminal/protocol-error TX is implemented here.

**No seen-record TTL, ACK eviction or ring overwrite.** Ingress/open expiry releases scratch only, never
the current-epoch seen tag. Future ACK releases transcript/body, leaving the seen tombstone needed for
already_acknowledged. Only an epoch-invalidating change may clear that slot's old seen records. Slice 7b
will implement rollover and ACK producers; until then acknowledged/transcript-state classifier arms
are tested with explicitly synthetic value fixtures, not called executed wire behaviour.

Resource arithmetic: seen 16×56=896 (including 256 bytes of tags and 128 bytes of retained routes beyond
the old 512-byte candidate); ingress 2×40 + 2×236=552; open/bootstrap 4×32=128; pair/ACL/epochs
64+340+80=484. Sum **2060 bytes**, plus four explicit readiness/status bytes = **2064-byte state candidate**.
Place pair[64], ACL[340], status[4], epochs[80], seen[896], headers[80], bodies[472], staging[128] in that
order: offsets 0/64/404/408/488/1384/1464/1936. No hidden resident counters, vtables, key cache or transcript
allocation. 7b's four transcripts/eight chunks/two deferred actions (1824 candidate bytes) stay absent.
The extra route and control bytes are explicit source-derived attribution, never concealed in 2968.

### 4.3 Partition and pre-transcript operation

Two authenticated header/body pairs are not four operations. Ordinary/operator execute uses the general
pair; the second is reserved for authenticated owner/control admission. An owner execute or authenticated
session-control request is eligible for that reserved class; unauthenticated claims to owner/slot are not.
Control admission does not allocate an execute seen row, so seen exhaustion cannot consume control's
reservation. No ACK/rollover effect or response occurs yet: retain/defer or expire that control only.

Four open/bootstrap rows are explicitly partitioned **three open + one bootstrap**, with no borrowing
between these subpartitions or authenticated ingress. At most one active open row per source; request ID
changes do not evade that bound. Authenticate bootstrap before its reservation, encode/enqueue its one
response and release that staging row on the checked outcome. Bootstrap never allocates seen or changes
epoch. Open rows may stage only exact status/routes diagnostics; no dispatcher or response exists yet.
This narrow storage admission is not the common command validator or the open-rate policy of 6/7b.

All retained inputs are owned copies, never spans into PostAck, stack decode scratch or mutable RX memory.
Stash validated decoded command bytes in the paired body slot; preserve the original tag/source/route
separately. The 233-byte candidate array is storage, NOT an admission cap: remote_body_cap remains the
authority (232 same-layer and the ruled cross-layer sizes, plus authenticating overhead). Refuse over-cap
before copy; no clamp. Wipe plaintext/key/transient plans when released, expired, invalidated or rejected.

### 4.4 Legacy handoff (§6.5)

Compile _remote_inbound, its staging helper and take_remote_inbound's real body only for CLIENT; ACCEPT
has no legacy staging or command execution drain. Guard firmware's entire drain/scratch block CLIENT-only.
On a CLIENT product, preserve response copying, full-slot refusal, supplied legacy print behaviour and
every base byte. Native has both roles and therefore KEEPS the legacy client slot alongside new state.
Do not subtract that slot from native sizeof(Node). A disabled take API may use a zero-state false stub
if required by existing API users; no unconditional hidden storage or legacy widening.

Target-side legacy rcmd execution ends here because A0 belongs exclusively to the v2 decoder. No fallback
on absent hash, old format, malformed ctl or failed authentication. Existing old legacy tests expecting
target execution are superseded in place by this ruled receive replacement, not silently deleted to
reduce native counts. Keep remaining legacy issuers, definitions/NV and deferred-action cleanup for 9/10.
The bench's suspended static/gateway round trip gains a draft effective-on-Slice-5 note, not a new part.

### 4.5 One shared expiry timer (§6.6)

Allocate only Node::kRadminExpiryTimerId = 91; TimerWheel::kCap = 92 on all builds. The ACCEPT timer case
delegates to remote_session's bounded earliest-deadline scan, which cancels when empty and otherwise arms
the true earliest deadline. Snapshot HAL now once per scan; expire at now >= expires_at, release rows
before calculating the next arm, no zero-delay livelock, per-class timer or global-default-layer mixup.

Author staging-lifetime choice: use a named pre-dispatch staging lifetime derived from the existing
`protocol::e2e_ack_deadline_xl_ms` (the maximum transport horizon, currently 300000 ms), not a new magic
literal. Node passes now and computed absolute deadlines to the pure state API. This is a resource
holding ceiling while there is no executor, NOT a seen/session timeout, execution promise or RPC terminal
deadline. Slice 7b owns active operation/transcript/deferred-action lifetimes. Bootstrap normally releases
its staging synchronously after its one enqueue attempt; no application retry scheduler is added here.

Use uint64 absolute deadlines and checked/saturating addition at the edge; cancellation/invalidation
re-arms against remaining rows, not a stale cached minimum. Future-class rows join this same scan later.
Update old executed kCap==91 checks to preserve their actual obligations: 91 admitted, 92 refused; the
mobile-aging test still proves it allocates no NEW timer. Keep 0e's 91/92 historical mirrors and prove
the production wheel equals the 92 mirror, +8 versus 91; do not rename its +1 option into a second increase.

### 4.6 Resource predictions and ABI authority

R-RA-31 authorizes Node's native and gateway re-pin only, including the Node ledger and board-ABI tool
entries. Heltec_mobile's Node pin remains **117912 / alignment 8**, refreshed against closure but never
changed here. Native adds the entire measured ACCEPT state; gateway adds it minus the effective removed
legacy-slot/padding contribution; native retains that CLIENT slot. Do not infer either final sizeof from
the candidate: compile the real target flags and attribute each member/offset/padding change, -Wreorder clean.

Mobile HAL RAM is NOT zero movement: the globally owned wheel grows +8 by the R-RA-22 mirror, with actual
DeviceHal/board padding measured separately. Wheel code may move mobile flash too; no accept/session
symbol or new Node data may survive there. Gateway's board delta additionally includes firmware drain
scratch eliminated by the CLIENT guard, plus the wheel; price only symbols actually present in the base.
No inventing a flash tolerance or treating sizeof(Node) as the whole firmware RAM measurement. No third
board: R-RA-30's xiao_mobile exception was Slice 4 only; pair and census retain their standing scopes.

### 4.7 Finding allocation (§6.7)

Allocation landed 2026-09-07: Slice 4 owns B327–B330; S5-A1/S5-A2/S5-A3 are B331/B332/B333 in place,
with their aliases and history retained. All three remain OPEN until the Slice 5 gate measures their
closure obligations; QA's pre-check corrections alone do not close them. HOME-A1/A2 are B334/B335.
Next free B336, rechecked against the maintained register before use. The coder proposes findings in
evidence only; no register edits or racing another slice for an ID. B328/B329/B330 are follow-ups, not
additional implementation/tool fences for Slice 5.

## 5. Exact implementation/instrument fence

- New `lib/core/remote_session.h/.cpp`: the pure value/state/crypto/admission/expiry implementation in
  MESHROUTE_NS. One Node ACCEPT block, no includes from src, heap state or new wire layout.
- `lib/core/node.h`, `node.cpp`, `node_mac_rx.cpp`: core header/API/member integration, checked epoch
  source, install preparation/commit, ACCEPT receive/bootstrap reply, one timer case, client-only slot
  boundaries, native static_assert/ledger. radmin_rx_owner's truth table/order stays unchanged.
- `lib/core/node_mac.cpp`: CLIENT guard on take_remote_inbound only, plus directly associated comment.
  Reuse existing cross-layer/send helpers WITHOUT rewriting them. No send_xl_ack generalization, legacy
  helper deletion or unrelated scheduling change. New reply orchestration belongs in remote_session.cpp.
- `lib/hal/timer_wheel.h`: kCap 91→92 and history comment only; no wheel algorithm or profile-specific cap.
  `lib/core/protocol_constants.h`: named staging lifetime derivation and corrected timer exhaustion
  comment only; no old lifetime/airtime/retention constant change.
- `src/firmware_admin_identity.h`, `firmware_admin_acl.h`, `firmware_admin_verbs.h`, plus new pure
  `src/firmware_admin_runtime.h`: prepare/commit/discard live seams, typed runtime preparation error and
  one candidate conversion authority, preserving old validation/persistence/format contracts. No NV
  layout, new store or backend change. Existing callers/fakes explicitly bind the seam.
- `src/firmware_commands.cpp/.h`, `src/fw_main.cpp`: runtime adapters/install/boot line, successful
  post-durable activation, CLIENT-only old drain. No new verbs/BLE policy or common dispatcher context.
- `lib/core/remote_codec.h/.cpp`: ONLY comment correction from consumer-free to first consumer where
  touched; algorithms, constants, public signatures and KATs remain byte-for-byte by normalized proof.
  New core code reuses remote_ecdh_shared, remote_kdf_base/session, remote_layout, decode and encode.
- Simulator CMakeLists.txt: ONE new `_meshroute_core_srcs` line for remote_session.cpp, both variants;
  comment-only correction of the neighboring codec's obsolete no-consumer claim. No other simulator edit.
- New `test/test_remote_session.cpp`, `test/test_node_remote_session.cpp`,
  `test/test_firmware_admin_runtime.cpp`; additive target-service/verb/router tests and bounded updates
  in test_node_r3.cpp for replaced legacy ACCEPT behaviour. Update test_timer_wheel.cpp,
  test_radmin_characterization_0e.cpp, radmin_0e_candidate_types.h (timer mirror commentary/assertion
  authority, old candidate record history retained) and test_node_join.cpp's old timer-pin clause only.
- `tools/probe_ui_model_mutations.py`: new full per-file radmin5session/radmin5runtime batteries, any
  decision-bearing new-header battery, additive entries for changed services/timer boundaries and native
  PIN. No B286/B311 harness repair or removal of historical batteries.
- `tools/probe_features/{ownership.py,run.sh}` and tools/test_probe_features.py: closure-based literal
  site multiset and updated ownership controls. Old shared legacy helper is now CLIENT-only; do not
  retain the obsolete ACCEPT-or-CLIENT helper contract or weaken the pure decision/none/forwarding proof.
- `tools/probe_board_abi.py`, tools/test_probe_board_abi.py: R-RA-31 native/gateway Node re-pin from
  actual measurements and additive type measurements/assertions. Heltec Node and every B278 row pin
  unchanged; no duplicate PINNED table. Any claimed needed B278 mirror edit requires proof of its actual
  dependency, not the pre-check's general assertion that it mirrors Node's full size.
- Existing inbox-verbs runner/probe/transcript files and platform fact fakes: both delivered real product
  arms; add actual NV-failure/prepare/commit/Node-activation observations, preserve mobile Slice 4 rows.
  Console-sink/ownership wrappers: rederive pins only where source anchors or the new runtime error/boot
  proof requires; no help/transport redesign. Generator --write is coder-owned when command anchors move;
  semantic inventory/help counts and transport sets stay unchanged.
- Evidence `docs/superpowers/evidence/2026-09-06-radmin-slice5.md`, including both repository diffs and
  proposed findings. Coder never edits the register/bench/manual/design/rulings/QA ledgers/BASELINE.

Every modified/untracked path must be listed; out-of-fence is STOP. No refactor/helper moves, production
test-only flags, private-member visibility changes, wire version or main NV schema bump. Pure header
record assertions are not permission for another resident copy. Author landings follow QA PASS.

## 6. Receive/crypto/reply contract and wiring proof

1. Drive the REAL RTS/DATA/post-ACK ACCEPT path. Require parsed unicast inner plus SOURCE_HASH presence,
   valid carrier shape, outer plaintext v2 family and codec body-cap admission before copying. Retain
   valid carrier/source exactly; no alias to 8-bit pa.origin, no zero-as-absent, clamp or legacy decode.
   Retain existing forwarding ahead of this arm; other types/disabled owners reach the existing guard.
2. Use remote_layout to classify ctl/shape. Only bounded syntax needed for key selection may be examined
   before authentication. BOOTSTRAP full-key lookup is exact across ten LIVE rows; convert that matched
   Ed25519 key using the existing conversion, reject invalid/low-order ECDH, derive base bound to BOTH
   ordered full identities, then decode/authenticate. Never use ordinary messaging/peer-book keys.
   Normal requests derive the selected slot's current session key; absent/inactive slot, tag/conversion/
   key/epoch failure is silent (no response oracle, fallback or reservation). Wipe all owned key/plaintext
   transients, including decoder failure buffers: B313 means a failed decode does not wipe for the caller.
3. Authenticated execute returns §10's typed verdict and bounded state only. Case-4 fixtures are marked
   synthetic until 7b's ACK producer; a full pool is real, but no session_full wire response exists yet.
   The result exposes the authenticated slot/source/first route/row index; no later mutable global selector.
   Identical retries never dispatch/reseal/overwrite; different source on unchanged sealed bytes fails
   auth, while a newly valid shared-credential same-ID/different-tag request is reuse refusal.
4. Valid bootstrap sends exactly the codec's 33-byte response under base key, matched slot/current epoch
   and request ID. Crypto RemoteSource remains the ORIGINAL controller source in both directions; the
   response carrier's SOURCE_HASH is the target's own routing identity. These are different facts: do not
   feed the physical response source back as the codec's controller-domain input. Reply DST is the
   captured controller source; zero/unroutable value is an explicit send failure, not fallback to origin.
5. Same-layer send_by_hash: explicit GLOBAL, REMOTE_RESP, CryptIntent::off, zero optional ACK flags,
   no delegated override/intro; use SendDispatch for queued/parked/refused. A parked transport-owned
   copy is not aired TX yet, and a zero immediate counter is not by itself failure. No new application
   retry or B278 debt. Existing application-DM path must stamp SOURCE_HASH and obey DST_HASH admission;
   confirm in actual packed frames, not a function-name claim or fabricated app_dm parameter.
6. Cross-layer: reverse the captured COMPLETE path once, validate destination/origin/cursor and active
   layer, pass only reversed destination hops to originate_layer_path so it prepends the origin once.
   Reuse the existing enqueue path and check its CmdCode/out counter. Reject unavailable reverse gateway
   or invalid path, never substitute a same-layer reply. Do not call send_xl_ack to send an RPC: reuse
   its path principle without its ACK payload/type. All full depths 2–4 are tested; same-layer is separate.
7. Execute a two-endpoint native exchange with requests generated by the production codec and independent
   retained KATs, captured emitted DATA and response decode under the controller fixture. No controller
   implementation is implied: the test builds requests explicitly. Test all ten ACL slots, wrong target,
   absent key, low-order key, bad tag/source/epoch, cold-boot dead draw, cross-layer reversal, queued vs
   parked vs refused sends, no generic user-send success/failure laundering, and lost response then exact
   read-only bootstrap retry. Only REMOTE_RESP bootstrap may be emitted by the new owner.
8. Test atomic seen/ingress allocation, full 16-row pool with concurrent credentials, both partition-
   starvation directions, three open + reserved bootstrap, per-source open bound, body lifetime and
   immutable route after ingress expiry. Invalid packets reserve nothing. Test now just below/equal/above
   expiry, multiple equal deadlines, cancellation after invalidation, safe time-add overflow and no
   seen deletion on time/ACK. Future completed/acknowledged fixtures are not reported as RF-executed.
9. Real firmware wiring is the delivered inbox-verbs probe, compiling firmware_commands.cpp with real
   core/console and platform fakes under its two independently compiled role profiles. Neither arm sets
   OLED (B328); do not claim full board-define parity or expand its UI linkage in this slice.
   Drive boot/install, every target mutation and no-op,
   prepare failure before write, NV failure after prepare, success-before-output, slot-local versus root-
   wide invalidation, and ordinary regen preservation. Assert actual Node state/authentication before
   and after, not callback counts alone. Mobile arm retains its complete Slice 4 byte pins. Native pure
   tests and console-sink guard extraction alone do not prove this wiring.

MR_EMIT observations for bounded refusal/expiry/bootstrap enqueue outcomes are acceptable only on exercised
remote state; device strips them. No new resident counter or status JSON in this slice. Enqueue failure
is checked and classified; it never claims an authenticated execute result. All new controls mutate REAL
source copies, exactly one match, then compile and execute with a diagnostic test failure. Crash/compiler
failure/timeout/zero-match/lost-worker is unusable, not RED. Preserve the standing feature-matrix's narrow
compile-refusal exception; it does not apply to these service/router/crypto controls. Restore source hashes.

## 7. Mutation union and full gate

Derive BOTH selectors from the completed closure and final diff, print them separately, gate their full
deduplicated union; no per-battery sample or assumption that changed-source implies complete coverage.

- Changed-source: new session/runtime files; all configured node.cpp/node.h/node_mac.cpp/RX/constant/
  timer and changed target-service targets. At preparation the RX six are b161rx/b251rx/b159rx/a0rx/
  sliceBrx/sliceGrx; node.cpp includes teamgrant/b159map/sliceBnode/sliceEnode, and node_mac.cpp includes
  grantadmit/b161mac/b20mac/b159mac/sliceBmac. These are cross-checks, NOT a substitute for the full map.
  Comment-only files/headers still require explicit selector accounting; report any absence of a native
  battery and the executed wiring/structural instrument covering the new decision.
- Historical/dependency: full radmin2codec (first consumer), radmin3id/acl/verbs (live installation),
  fingerprint/seed/wipe/parser dependencies actually reused, and the relevant full by-hash/park/cross-layer
  acceptance batteries even where their source is unchanged. Derive exact names from configured sources;
  include grantpark and the full existing hash/cross-layer batteries that define reply admission/return.
  Explain every included/excluded dependency. No falsely empty selector or old pin copied as a result.

Run base and final: pio test -e native THEN the actual .pio/build/native/program; all six standing probes
with default controls and both inbox arms; complete tools unit sweep; codec independent references and
bad-expected-value control; full mutation union; inventory --write/bare/--check as required; warning census
at its pinned env set with zero switch/new warnings; check_a0_matrix.py/check_data_type_literals.py;
both ABI probes; fixed-identity same-path ruled gateway/heltec_mobile pair; both repos' whitespace checks.
Pair concurrency only through its own ruled runner; mutations stay exclusive of source-dependent captures.
Use existing two-worker isolated staging, recording B286/B311 hazards without fixing them in this slice.

Simulator: rebuild BOTH core variants with remote_session.cpp, record source/object/archive/executable
hashes and real build actions. Predict changed executable, **36/36 byte-identical validated streams** and
current BASELINE.md s18 keystone, no re-anchor. Capture remote-type/event census: zero in the old corpus;
native exercises the new events separately. No unconditional install/draw/timer action in unprovisioned
sim nodes. A build still pointing at another checkout, missing variant or stale object cannot pass.

Record concrete sizes/offsets, actual effective flags and every Node/DeviceHal/board RAM contributor;
show ACCEPT compiled out on mobile and client legacy storage absent on gateway. Repeat deterministic
captures as required by the standing board instrument; attribute payload/sections/symbols separately
from debug metadata. Re-pin native/gateway Node ONLY from actual ABI measurements; mobile Node and B278
stay at closure pins. Record remaining RAM/stack headroom; if state cannot fit, STOP, do not shrink N.

Evidence contains pre-edit predictions, full base/final outputs, case/assertion additions (and every
superseded legacy/timer case), probe/pin derivations, all references/controls/mutation selectors and union,
restore hashes, two-repo manifests/diffs, runtime install/boot and receive/TX transcripts, synthetic-arm
labels, measured N and shared-pool caveat, resource attribution and proposed findings. Include exactly:

`PIN re-synced? YES — <derivation>`

## 8. STOP conditions

1. Any §1 placeholder, wrong/dirty measured base, missing prior closure, unreviewed concurrent input or
   out-of-fence path. STOP to QA; never repair, repin, commit or manufacture a baseline.
2. A new product capacity/ownership/physical authority or ABI pin outside R-RA-31 is needed; missing
   resource headroom, unexplained padding/flash/RAM, or mobile Node/B278 pin moves. STOP for owner scope.
3. Authentication fallback/oracle, missing SOURCE_HASH, mutable-source/route replacement, over-cap clamp,
   secret lifetime leak, failed-save live activation, epoch-zero readiness or ordinary regen rotating
   administration state. STOP; no partial success or stronger entropy claim than the actual provider.
4. Seen rows evicted by time/ACK/capacity, bootstrap consumes seen/changes epoch, partitions borrow,
   a raw counter is misreported as send admission, or a cross-layer request has no valid return shape. STOP.
5. Any execute/terminal/output/ACK/rollover/protocol-error reply, remote dispatcher/controller/custody
   consumer or legacy ACCEPT fallback is added; any synthetic future-state arm is called executed RF. STOP.
6. New corpus event/stream delta, simulator wrong source/variant, extra timer ID, unconditional epoch
   draw at unprovisioned init, failed gate, mutation survivor/unusable worker, lost row/pin/control or
   missing restoration/evidence. STOP; no re-anchor, count laundering or tolerance to force green.

## 9. Author landing after software QA PASS

Read the evidence without editing it; reconcile new findings and close only proven register obligations.
Land measured design §19/19.1, resident-state/N/cadence and actual Node/HAL attribution, and §6.5 live-
activation documentation. Keep B312/B317 and prior metal debts open for their actual scope. Update the
existing bench suspension to target receive removed in Slice 5; no new bench part or RF metal PASS.
Part 57c remains the later full controller/carrier observation. Update manual/boot/error documentation
only from verified transcripts; help remains bare names. Owner commits each repository; Author does not.
