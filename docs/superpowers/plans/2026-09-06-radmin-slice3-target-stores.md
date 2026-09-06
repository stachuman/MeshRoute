<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 3 — target identity, ACL and USB provisioning · dispatch brief · 2026-09-06

**Status: DRAFT — awaiting Quality-Agent review.** Not authorized for dispatch.
**Brief gate 2026-09-06: HOLD on one inventory-profile fold-in; applied below in §5 and §6 item 7.**
The changed sections await QA confirmation. All other sections were verified by QA at `7299eb9`;
B318's QA-ledger corrections are accepted and closed. Preparation commit and explicit Author repin still
precede dispatch; this edit does not invent a PASS or a new base hash.
Dispatch model: **Opus** (`model: opus`). Author writes; QA gates and dispatches; owner commits and runs metal.

Authority: design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`
§§6.3–6.6, 12.1, 17, 19 item 3, 19.1 row 3 and 19.2; rulings R-RA-6/8/17/21/26/27/29 in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`; QA's
`docs/superpowers/plans/2026-09-06-radmin-slice3-precheck.md`, with §6.1/6.4 ruled by R-RA-29 and the
remaining Author decisions below. This is a target-store feature, not a storage refactor (C1).

## 1. Base and measurement provenance

MeshRoute base: **`7299eb90c787c867cba3dc17902e87b838f5447c`**, owner commit `Slice 3 prep`.
It exists and commits the rulings ledger plus the Slice 3 pre-check; its diff from Slice 2 documentation
closure `231e1be` is only those two Markdown files. Simulator reference:
**`868888419c7cc250d7019860d3403a7721ade1fc`**. No simulator repository edit is authorized.
Both checkouts were clean at Author verification, before these documentation edits.

The preferred dispatch sequence is owner commit of this Author preparation, then Author explicit repin to
that existing commit before QA dispatch. Never silently follow HEAD. If this written base is used, QA must
provide the reviewed brief/Author decisions as out-of-band authority to a CLEAN isolated worktree at this
exact base; no named dirty-preparation exception in a measured tree. A later owner preparation commit
requires an explicit repin, not an assumption that its code is identical. The coder never commits or repins.

Record exact HEAD/status in both repositories, measured input manifests, and the paired simulator build's
`MESHROUTE_DIR`. It must name the measured MeshRoute worktree. Capture EVERY base gate before the first
production/test/tool edit; building a paired simulator from scratch is base setup, not post-edit movement.
Protect unrelated checkouts and running jobs. Named base mismatch, dirty measured start or unexplained
concurrent input change is STOP 1. Markdown-only provenance must be audited, not guessed.

## 2. Verbatim authority pins

R-RA-29:

> **Owner:** *"Agree - BLAKE2b fingerprint frozen, refuse the whole family over BLE"*

R-RA-29:

> **Settled, append-only:** `fp(ed_pub) = BLAKE2b-512(ed_pub)[:8]`, rendered as 16 lowercase hex characters. It is the
> ONE fingerprint of a 32-byte Ed25519 public key everywhere remote-admin v2 shows or selects one: the target ACL listing
> (§6.5 "slot, role, and a fingerprint"), the USB first-owner exchange (§6.4 step 5, both directions), the controller's
> `/mrtargets` trust selector (§6.3) and `admin-key show` (§6.2). On USB listings the FULL 64-hex key is printed beside
> it, because the physical exchange copies the full key; the fingerprint is a display/selection handle, never a trust
> anchor by itself and never a routing hash — `key_hash32` (the first four key bytes, LE) is explicitly NOT a
> fingerprint (`lib/core/identity.h:40` "NOT a security anchor").

R-RA-29:

> **Settled for Slice 3:** the whole target-side family — the ACL verbs (list/add/set/remove/recovery) and the
> administration-identity verbs (show/generate/rotate) — is refused over BLE in `ble_dispatch_line` BEFORE the
> transport-neutral seam, with one named `console_only` envelope (the `help` shape, `src/fw_main.cpp:573-574`), executed
> by the console-sink probe's BLE-guard rows and recorded by the inventory as a `serial`-only surface.

Design §6.4:

> Provisioning reports success only when the required target-identity and ACL records are valid and durable;
> partial failure remains physically recoverable and never invents an active remote owner. The controller
> private seed never enters the target, and the target administration seed never enters the controller.

Design §6.5:

> Slot numbers are stable wire handles, not security identities. Empty entries are not compacted, an ACL-full
> condition refuses loudly, and adding a duplicate public key is rejected.

Design §6.6:

> - The last owner cannot be removed or demoted remotely.
> - A request cannot remove or demote its own authenticating slot. Rotation is add replacement owner, verify a
>   session through that owner, then remove the old entry.
> - ACL changes acknowledge success only after the new state is durable.

Agent roles, step 4 (base-mismatch STOP):

>    starting work in an isolated worktree, the dispatched agent verifies and records `git rev-parse HEAD` against
>    the brief's named base commit; a mismatch is a STOP to the dispatcher, never a stale-tree measurement or a
>    silent worktree repair.

The legacy compatibility/airtime rationale does not authorize edits to the old remote path. C4/M3 and the
roles' two-board gate supersede CODE_GUIDELINES' historical fleet-reflash/all-board instructions.

## 3. Verified source and pre-check corrections (V1/V2)

All anchors verified at `7299eb9`; relocate by symbol before editing. These are source facts, not fresh
Author build measurements. The coder and QA independently derive the measurements.

| Authority | Current fact and consequence |
| --- | --- |
| `src/device_nv.h:295` / `:395` / `:623` / `:1074` | Fixed team records, named padding, Slot table, four-state classifier, typed load/save wrappers. Reuse `SlotIo`, `kSlotAbsent`, `blob_valid_exact`, `read_slot` and `write_slot`; do not edit the shared primitives. |
| `src/firmware_team_keyring.h:96`, `:441`, `:461`, `:507` | Fakeable store, invalid/I/O separation, `SecretWipeGuard`, candidate/no-op/one-save idiom. Reuse the guard without moving or changing it. |
| `src/device_nv.h:846` / `:867` / `:885` | nRF52 write removes the old file before replacement; factory erase formats; boot self-heal may also format the entire FS. No transaction-journal or atomicity guarantee may be invented. B317. |
| `lib/core/identity.h:34`, `src/device_rng.h:46` | Expanded Identity is 196 B; derive from seed through `identity_from_seed`. RNG returns void, host writes zeros; nonzero output is not a hardware-health proof (B312). |
| `lib/core/remote_codec.h:52` | Slots 0..9; bind ACL capacity by static assertion. Including this declaration is not a runtime codec consumer. |
| `src/firmware_config_parse.h:224`, `:545` | Reuse public `mrfw::parse_hex32`, `parse_index_strict` and `parse_confirm_token` for bounded token copies/exact confirm tails. Pre-check §2's `console_parse.cpp:75` helper is private to that TU; do not export it, redeclare its private type or fork another decoder. |
| `src/firmware_commands.cpp:672`, `src/firmware_config.cpp:2397` | `do_regen` writes `/mrid`; `handle_leave` rebuilds `/mrcfg`. Neither writes the new slots. Preserve their bodies. |
| `src/fw_main.cpp:573`, `:583`, `:829` | BLE help guard precedes the execution seam; setup's legacy `admin_load` is the adjacent boot anchor. Keep both legacy operations intact. |
| `tools/probe_inbox_verbs/run.sh:57`, `:160`, `:198` | This instrument compiles the REAL `firmware_commands.cpp` and real core/console against platform fakes, then executes dispatch/seam. It is the new router/device-adapter wiring gate. |
| `tools/probe_console_sink/run.sh`, `ble_guard.py:33`, `ownership.py` | These compile the real help header, execute an extracted BLE condition and census router/parser ownership from source. The pre-check's attribution of real-router compilation and 720 checks to one router gate is withdrawn here: 720 is the sink/help corpus, not execution of the command TU. B318 is closed after QA corrected its ledger in place. |
| `tools/probe_inbox_verbs/fakes/Preferences.h:38`, `fakes/esp_random.h:23` | Existing byte-counted fake medium and deterministic draw stream support actual production wrappers. Extend these fact providers, not a second simulated storage policy. |
| `tools/gen_command_inventory.py:110`, `:124` | Source-derived top surface and serial-only help precedent. New target surface gets real `reached_from` proof, not a transport label with no guard evidence. |
| `tools/probe_features/ownership.py:62` | Literal multiset of three approved files today. Extend the named sites below, retaining every existing RX/legacy-agreement control. |

B193 is not inherited qualification of the new stores: its actual closed row names `/mrcfg` and `/mrjoin`,
not `/mracl` or `/mradmid`. Part 55a owns the new power-cut/physical-transport residue.

## 4. Author decisions resolving pre-check §6

### 4.1 Residency and future boundaries (§6.2)

No resident administration Identity, seed, ACL cache or Node member. No new global/static I/O buffer is
authorized: automatic bounded scratch and stateless adapters only. Every command loads its records, derives
public material if needed, renders through its supplied sink, and wipes every secret-bearing transient on
every exit. Boot only validates and reports; it installs no identity/ACL/session and writes nothing.
Measure stack frames and their maximum composed call chain; a scratch-size problem is a STOP, not permission
to add a cache or static secret. Predict gateway RAM delta zero (explain any compiler-emitted storage);
gateway flash grows with the new local feature. Mobile RAM and live flash must remain unchanged.

Mark the deferred work in the new code: session residency/epoch/invalidation belongs to the session slice;
remote caller authority and `CommandContext` to Slice 6; no remote execution or codec call is added now.
Do not wire these stores into legacy `g_admin_id`, `admin_load`, password/unlock/lock or remote_exec.

### 4.2 Record layout and transactions

Freeze two v1 records as value types in `device_nv.h`, with no packing pragma or main-Blob version change:

| Record | Exact layout | Size/alignment |
| --- | --- | --- |
| `AdminIdBlob` | magic u32 `0x4D524131` (MRA1), version u16 = 1, reserved u16 = 0, seed[32] | 40 / 4; seed offset 8 |
| `AclRow` | ed_pub[32], role u8 (0 empty, 1 operator, 2 owner), reserved[3] = 0 | 36 / 1; role offset 32 |
| `AclBlob` | magic u32 `0x4D524C31` (MRL1), version u16 = 1, count u16, rec[10] | 368 / 4; rows offset 8 |

Pin `sizeof`, `alignof` and offsets where EVERY board compiles them, and add native literal layout tests.
`kAclSlots == kRemoteSlotSessionMax + 1` is a compile-time binding, not another free-standing ten.
Slots: `kSlotAdmid { "/mradmid", "mr", "admid" }` and `kSlotAcl { "/mracl", "mr", "acl" }`.
Do not add either to the nRF52 self-heal probe list or fault-history preservation domain.

Typed reads distinguish ok/absent/invalid/io_failed; backend failure outranks bytes that happened to arrive,
oversize is invalid, exact length/magic/version are mandatory, and non-ok output is never trusted.
Pure record init/validation is the sole composition path. Empty rows have zero key/reserved bytes; occupied
rows have nonzero, unique full keys and legal roles; count equals occupied rows, holes remain holes; a
nonempty ACL without an owner is invalid. Never clamp counts, compact rows or adopt a partially valid ACL.
An admin record with an all-zero seed is invalid. Save wrappers delegate once, without an extra read.

Use new pure `IAdminIdStore` / `IAclStore` seams. Every ordinary mutation is load/classify → candidate → full
validation → byte comparison → at most ONE durable save → success verdict. Identical role assignment costs
zero writes; duplicate add REFUSES (not unchanged); full add evicts nothing. A rejected input costs no write.
Adding selects the lowest free slot and never shifts existing handles. Stable occupied slots remain stable
after reboot. `set`/`remove` require an occupied slot 0..9; exact decimal parsing rejects signs, overflow,
trailing junk and extra tokens. Role and key parsing never defaults.

The last owner cannot be removed/demoted by these services, including the local path. An explicit optional
acting slot is checked by set/remove: a present slot cannot remove/demote itself. Local USB supplies NONE;
test the present-slot arms directly and label them future-caller service tests, not executed remote requests.
Same-role no-op is legal. No remote authority context or runtime role flag is introduced.

### 4.3 Creation, recovery and root rotation (§6.3/6.7)

`admin-id generate` creates only an ABSENT identity; it refuses an existing, invalid or I/O-failed record.
`admin-id rotate confirm` replaces only an OK identity. `admin-id reset confirm` recovers only INVALID by
generating a fresh root; it refuses absent/ok/io_failed. Both replacement operations use the same draw,
derive, validate and save authority as generation; neither erases the ACL or touches `/mrid`.
`acl reset confirm` reinitializes only INVALID to a valid empty record; absent/ok/io_failed refuse. First
owner then uses ordinary `acl add owner …`; a new operator can never be the first/only credential.
Valid records with lost controller credentials can be repaired physically by adding a replacement owner
and then removing the old row; no old remote credential is needed on this USB-only path.

**Resolve the pre-check's shorthand precisely:** invalid and io_failed both forbid EVERY ordinary write;
the explicit confirm-gated INVALID recovery is the only exception. IO_FAILED permits NO write, including
reset, generation or rotation. No automatic recovery on load, boot, show/list, add, failed save or version
mismatch. A missing confirm token, wrong token or extra token has zero writes and zero entropy draws.

ACL add requires a separately read, valid administration identity; a missing/invalid/I/O-failed identity
cannot produce a first-owner success. Generation can leave a valid root with no ACL: report only identity
creation, never completed provisioning. Provisioning readiness is a read-only conjunction of valid root,
valid ACL and at least one owner, never an independently stored boolean. There is no two-store atomic commit.

Use a checked 32-byte seed source in the pure identity service: missing provider, false (including partial
write then false) and all-zero result refuse before any save/derive/output of a key. The device binding may
call existing `mrrng::fill` and return the ACTUAL nonzero-result check, never unconditional success. Its
meaning is only completed draw/nonzero material, not a device RNG health/failure-reporting guarantee: void
draws can block and cannot expose all failure modes. Prove host-zero refusal, count real fake-platform draws,
and keep B312 OPEN for truthful first-RF entropy integration. No RNG/HAL rewrite or clock/counter fallback.

Non-atomicity must be stated beside the service/save contract. A reported save failure publishes no success
and installs nothing in RAM (there is no live cache here); it does NOT promise previous flash bytes survive.
nRF52 can reboot absent/invalid after remove-before-write; a failed write may even have reached the medium.
The next command re-reads and classifies. No console wording may claim nothing was written. No journal,
witness file, rollback write or automatic retry; physical recovery and Part 55a cover the remaining limit.
Self-heal triggered by a different probed file or failed mount can erase both records (B317); ordinary
regen/leave preservation is not preservation across a whole-filesystem repair.

### 4.4 Fingerprint, grammar, outputs and transport (§6.1/6.4)

One pure fingerprint implementation in `firmware_admin_identity.h`, reused by both listings and available
to Slice 4 without a copy. Hash exactly the 32 public-key bytes with BLAKE2b-512, take the first eight bytes
in digest order, render 16 lowercase hex. No little-endian integer conversion, 8-byte-output BLAKE2 variant,
domain label, seed hashing, routing hash or four-byte prefix. Full USB public keys are 64 lowercase hex.
Generate frozen independent reference literals before implementing the helper, using a non-production
BLAKE2 implementation anchored on an external known-answer vector; preserve generator/version/input/output
and an altered-expected-byte failing comparison in evidence. No production-generated expected fingerprints.

Two ACCEPT-only primary verbs; nine subcommands, no aliases or implicit bare defaults:

| Form | Required result on success |
| --- | --- |
| `admin-id show` | `> admin-id ok fp=<16hex> pub=<64hex>` |
| `admin-id generate` | `> admin-id generated fp=<16hex> pub=<64hex>` |
| `admin-id rotate confirm` | `> admin-id rotated fp=<16hex> pub=<64hex>` |
| `admin-id reset confirm` | `> admin-id recovered fp=<16hex> pub=<64hex>` |
| `acl list` | Occupied rows in slot order: `> acl slot=<0..9> role=<operator|owner> fp=<16hex> pub=<64hex>`; then `> acl end count=<n> owners=<n> operators=<n>` |
| `acl add <operator|owner> <hex64>` | `> acl added slot=<n> role=<role> fp=<16hex> pub=<64hex>` |
| `acl set <slot> <operator|owner>` | `> acl updated slot=<n> role=<role>` or `> acl unchanged slot=<n> role=<role>` |
| `acl remove <slot> confirm` | `> acl removed slot=<n>` |
| `acl reset confirm` | `> acl recovered count=0 owners=0 operators=0` |

One LF-terminated line per record, no seeds/expanded secrets anywhere, no global sink or unsolicited BLE
output. Refusals: `> admin-id err <reason>` / `> acl err <reason>`. Reasons are literal typed mappings:
`bad_args`, `absent`, `store_invalid`, `store_io_failed`, `already_present`, `not_invalid`, `entropy_failed`,
`nv_save_failed`, `identity_absent`, `identity_invalid`, `identity_io_failed`, `duplicate_key`, `zero_key`,
`acl_full`, `slot_empty`, `first_owner_required`, `last_owner`, `self_slot` as applicable. Wrong grammar,
including unknown subcommands, uses bad_args; a foreign primary token remains unowned. The formatter must
exhaustively map service outcomes; do not add unreachable success/reason enums to meet a count.

`admin-id` and `acl` recognize exact token boundaries (space/tab or end), not `admin-identity`, `aclx` or
`admin-key`. Parse bounded input without assuming the dispatch span is NUL-terminated; use a bounded token
copy for the existing hex decoder, reject long/short keys and trailing data. No parser-core command enum.
Reuse `parse_index_strict` then enforce 0..9 before narrowing; reuse `parse_confirm_token` on the remaining
raw confirm tail, retaining its refusal of trailing spaces/junk. Do not trim a malformed confirmation into
an accepted one. Pure verb formatting uses a small caller-supplied line sink interface, not Arduino/Print;
the device binding adapts the supplied Print, following `firmware_ui_preset_verbs.h`'s separation.
Bare help stays names plus the manual pointer, sorted; add exactly `acl`, `admin-id` on ACCEPT profiles.
No description/topic returns. Detailed syntax lands only in the manual on QA PASS.

On ACCEPT builds, one BLE guard covers BOTH complete primary-token families, including malformed subforms,
before `exec_console_line`, with exactly `write_err(out, cap, "admin", "console_only")`. Its envelope is
`{"err":"admin","msg":"console_only"}`. Guard and boot call are ACCEPT-compiled, as are the USB handlers
and help names. On CLIENT-only boards the target family is absent rather than a callable stub; neither
USB nor BLE can enter a target store, and mobile flash need not acquire an unused target-family guard.
The BLE extraction must respect that product gate. Controller `admin-key` is not matched or implemented.
Never put a transport/name exception inside `dispatch`'s shared execution seam. Do not widen the existing
BLE reboot/regen/ota/factory_reset policy or make the unclassified inventory an authority table.

Boot's single ACCEPT-only adapter beside legacy `admin_load` calls the pure read-only report path. Lines:
`> admin-id boot state=<ok|absent|invalid|io_failed>` and
`> acl boot state=<ok|absent|invalid|io_failed> count=<n> owners=<n> operators=<n>`.
For every non-ok ACL state counters are zero and mean no accepted rows, not knowledge that flash is empty.
No boot auto-generation, fingerprint/key bytes, persistent status flag or status/version JSON expansion.

This is only the TARGET half of the USB exchange. Show the target key/fingerprint and the pasted controller
key/fingerprint in its ACL row. Slice 4 owns controller `admin-key show self`, target-book storage and the
other node's USB output (Part 55b); do not add a mobile verb or claim both-node exchange is complete now.

## 5. Exact implementation and instrument fence

Production allowed:

- `src/device_nv.h`: two records, slots, classifiers/init/validation and typed wrappers only; existing Blob,
  version, SlotIo, backends, factory erase, probe list and existing record semantics are unchanged.
- New `src/firmware_admin_identity.h`, `src/firmware_admin_acl.h`, `src/firmware_admin_verbs.h`: pure services,
  fingerprint, parsing, typed formatting and read-only boot report. No capability macros in these headers.
- `src/firmware_commands.cpp`: ungated pure-header includes; ONE ACCEPT block of stateless store/draw/sink
  bindings plus boot wrapper, and ONE ACCEPT dispatch forwarding block. No existing handler-body edits.
- `src/firmware_commands.h`: ONE ACCEPT block declaring the boot wrapper; no mobile callable stub needed.
- `src/fw_main.cpp`: ONE ACCEPT block for the BLE refusal, ONE for the boot call; no other execution changes.
- `src/firmware_help.h`: ONE ACCEPT block for the two sorted primary names; adjacent stale provenance
  comments may be corrected separately with comment-only/token proof.

Thus the new feature-site multiset is exactly: commands.cpp two, commands.h one, fw_main.cpp two,
firmware_help.h one, each normalized site `#if MR_FEAT_RADMIN_ACCEPT`. Existing three files/sites unchanged;
approved file census becomes seven. No legacy widening, new macro, Node edit or capability name in native tests.
This also pins compiled-out declarations, instances, dispatch, help, boot and BLE guard on the mobile arm.

Tests: new `test/test_firmware_admin_identity.cpp`, `test/test_firmware_admin_acl.cpp`,
`test/test_firmware_admin_verbs.cpp`; extend `test/test_device_nv.cpp` for the records. Existing native cases
must retain their counts/meaning except a separately derived addition. Pure headers are ungated so native
can exercise every service arm without defining product roles.

Tools, explicitly pre-authorized (pre-check §6.5, corrected wiring attribution):

- `tools/probe_ui_model_mutations.py`: full new per-file targets `radmin3id`, `radmin3acl`, `radmin3verbs`,
  new record entries in `devicenv`, dispatch entries and independently derived native PIN only. No B286/B311 repair.
- `tools/probe_console_sink/{run.sh,probe_main.cpp,ble_guard.py,structural.py,negctl.py,ownership.py}` and
  `tools/test_probe_console_sink.py`: second literal extraction anchor, new systematic guard rows, help/
  inventory projection, boot/storage structural pins and controls, literal re-derived count pins, including
  the ownership probe's per-profile router counts under the added ACCEPT axis below and its wrapper fixtures.
- `tools/probe_inbox_verbs/{run.sh,probe_main.cpp,transcript.py,transcript_main.cpp}` and its
  `fakes/{Preferences.h,esp_random.h}`: real router/store/entropy/sink tests and controls; re-derived pins.
  Fakes may model reads/writes/draws and fault facts only, never decide a production verdict.
- `tools/probe_features/{ownership.py,run.sh}` and `tools/test_probe_features.py`: EXACT new-site multiset,
  absence/wiring controls and derived pins. No env-map, header feature values or RX contract change.
- `tools/gen_command_inventory.py`, `tools/test_gen_command_inventory.py`: serial-only target surface
  in the new verb header, reached through the gated real dispatch forwarding call; actual subcommand rows
  and full call-chain/gate/transport proof. No hand-populated authority cell. Preserve unrelated semantics.
  **QA fold-in / B319:** extend `PROFILES` (`:202`–`:209` at the base) from four macro axes to five by adding
  an explicit integer `MR_FEAT_RADMIN_ACCEPT` column in EACH existing typed profile row. Values are:
  `full_oled=1`, `full_headless=1`, `gateway=1`, `gateway_oled=1`, `mobile=0`, `mobile_oled=0`.
  These are literal ruled product values, NEVER computed/aliased from `MR_FEAT_REMOTE_MGMT` inside the tool,
  and never inferred from `MR_FEAT_MOBILE` (the full static profiles also set that feature to one).
  Keep the six profile names, PROFILE_ENVS mapping and all four existing axes unchanged. No firmware feature
  derivation, env-map or build-matrix edit. Preserve `eval_gate`'s unknown-axis refusal (`:735`): the first
  ACCEPT-gated dispatch arm cannot be projected until the table explicitly knows that axis; do not default
  it to zero or hardcode an evaluator exception. Extend the generator's own unit fixtures for the new axis
  and re-derive affected projection/fixture pins together with console-sink's per-profile ownership pins.
- Generated `docs/superpowers/plans/2026-09-04-radmin-command-inventory.md`: CODER runs
  `python3 tools/gen_command_inventory.py --write` after anchors move, then bare and `--check` must match.

Evidence: **`docs/superpowers/evidence/2026-09-06-radmin-slice3.md`**. No other evidence/QA-ledger/Author-doc
edit by the coder. Author owns the manual, bench, register, design, tracker and MEMORY landings.
Expected diff is additive within these paths; give per-file diff stats and complete untracked inventory.
No new firmware `.cpp`, platformio/source-list edit, lib/core/lib/console/HAL change, simulator edit, wire/NV
main-version bump, session/remote behaviour, source move, shared-helper refactor or unrelated bug repair.

## 6. Tests and wiring proof

Before edits, enumerate cases/outputs/controls and predict each pin delta; numbers in §8 are starting
cross-checks, not expected answers copied into instruments. Counts derive from executed named rows.

1. Native records/services: size/offset/alignment, short/oversize/wrong magic/version/reserved/role/count,
   duplicate/all-zero/ownerless records, poisoned partial reads, all four storage states × each operation.
   Count reads, draw calls and writes; keep candidate/committed bytes distinct. Test absent creation, full
   and sparse ACLs, first-owner-only creation, several owners, last-owner/self-slot refusals, identical-role
   no-op, repeated deletes, stable holes, failed saves (including medium partially changed), and reload.
   Reset invalid only with exact confirm; io_failed always zero writes. Identity generate/rotate/recover
   share the same path; caller ID/ACL remain unrelated. All-zero/missing/failed/partial entropy refuses.
   Exercise the real SecretWipeGuard with an outliving carrier, and pin guards' placement at all real
   secret-bearing lifetimes rather than inspecting dangling stack memory. Frozen independent fingerprints
   and seed-derived public identity tests discriminate truncated/changed/reordered material.
2. Pure verb unit: exact grammar and complete output bytes, no seed leakage, bounded non-NUL spans,
   key case/length/junk, slot edges/overflow, every service refusal, maximum ten-row listing fitting the
   2048-byte stage with zero CONSOLE_DROP; supplied sink only. Declare maximum output before execution.
3. REAL router: extend `probe_inbox_verbs`, not a synthetic copy of dispatch. Run real dispatch and
   `exec_console_line(LineFormat::text)` through GuardedConsole with the real services/device wrappers and
   byte-counted fake Preferences. Cover successful generate → first owner → second owner → set/remove,
   public listing, invalid/I/O states, recovery, failed save and exact confirms, and verify the actual
   namespace/key/write bytes. Track unrelated slots and global sink bytes. Real fake-platform zero draws
   must refuse. Call boot wrapper on the same platform facts and require read-only report. This host board
   arm is a static ACCEPT profile, NOT execution of gateway hardware or a real BLE transport.
4. BLE: generalized console-sink extractor retains help's exact anchor and gains the UNIQUE admin envelope
   anchor. Extract within ble_dispatch_line, check ACCEPT guard and order before the seam, compile the
   source condition and execute against real new verb recognition over all applicable profiles. Exercise
   every subform, bare forms, spaces/tabs, trailing junk, short spans and near misses; malformed OWNED forms
   still refuse BLE, foreign tokens do not. No copied predicate or test-derived expected classification.
   Mobile disabled forms execute neither store nor handler. Envelope bytes and no USB/store/draw effects
   are pinned. Missing/duplicate/comment-only anchors, wrong envelope, moved-after-seam, partial-family,
   listing-only escape, wrong feature gate and broad admin-prefix matches must all fail the instrument.
5. Structural device boundary: boot call exactly once/ACCEPT-only after filesystem setup; no resident
   secrets/cache/Node link; actual wrappers point to the correct slots; no new self-heal list entry;
   regen/leave write sets unchanged, factory-reset namespace/format still includes both stores. Every
   claim gets a deliberate sabotage control. This is NOT execution of setup or real power-cut recovery.
6. Feature ownership: seven-file exact multiset plus controls deleting/swapping/widening each NEW owner
   boundary, a duplicate/unknown consumer and stale census. Keep every existing RX-decision and board
   refusal control. On both ruled boards preprocess and inspect objects/symbols for presence/absence of
   the actual bindings/verbs/boot/help/guard. No synthetic NONE result called an executed product path.
7. Inventory/help: predict exact rows before editing from two primary families and nine semantic
   subcommands; derive N from the generator's row representation, enumerate it, then prove 177 → 177+N
   without a missing/duplicate row. Full-build primary-name union 49 → 51, per-profile adds two only where
   ACCEPT. Source gate/transport assertions must fail for a mobile listing or BLE widening. Retain bare
   help/manual-only rendering, router/parser disjointness, both-direction name completeness and all old controls.
   **QA fold-in:** test all six literal ACCEPT values in §5 and evaluate real recorded ACCEPT-gated rows
   under them; the two mobile profiles omit both families, and the four other profiles include both. The
   generator's unit fixtures must cover the new axis, its missing-axis refusal and the unchanged refusal
   of an unrelated unknown macro; preserve typed literal-row provenance rather than deriving this column
   from the legacy switch. A synthetic evaluator-only fixture may vary the legacy value with ACCEPT held
   fixed to demonstrate independent axis use; it is not a newly legal board profile. Wrong/flipped ACCEPT
   cells must be caught by the per-profile source/build projection checks, not accepted by merely repinning
   the rendered list. Re-run `tools/probe_console_sink/ownership.py --show` and derive every router/parser
   count from named forms. Prediction from the current source pins: full_headless 41→43, full_oled 42→44,
   gateway 39→41, gateway_oled 40→42, mobile 38→38, mobile_oled 39→39; parser remains seven on each profile
   and the intersection remains empty. These are predictions to verify, not replacement measurements.
   Update `ownership.py`'s literal PINS and affected generator/console-sink unit fixtures from those actual
   rows, recording the derivation; counts alone do not replace exact two-direction name/transport coverage.

No mutation may count compiler failure, linker failure, crash, timeout, missing worker, unreadable output,
wrong match count or an unrelated assertion as RED. New policy mutants must compile and fail the intended
executed assertion; structural instrument controls must fail their intended named invariant. A broken
compiler/extractor/empty row set must invalidate the gate, never masquerade as a policy refusal.
The pre-existing feature probe's specifically classified board compile-refusal controls remain that
instrument's narrow exception; Slice 3 adds no inverted class. `--no-neg` remains PROBE-ONLY, never PASS.

## 7. Mutation selectors and resource safety

Report BOTH selectors independently and gate their full union:

- Changed-source: full `devicenv` including new record/wrapper entries; new `radmin3id`, `radmin3acl`,
  `radmin3verbs`. Changed command/glue files have no native per-file battery, so their mandatory executed
  router/guard and structural sabotage controls above are reported separately, never claimed as native REDs.
- Historical/dependency: full `teamkeyring`. Its shared SecretWipeGuard is a live acceptance dependency
  even though its source is UNCHANGED; this qualifies it independently of the pre-check's changed-helper
  suggestion. Also full `cfgparse` and `sliceDtoken`: both target `firmware_config_parse.h` and pin the
  reused strict index/confirmation acceptance boundaries. The exact hex parser's new caller tests and
  wrong-key controls are additional, not a claim that an unrelated old battery already attacks every hex
  byte. The existing shared source is not edited. `radmin2codec` is not selected merely for the slot
  constant/header include: no runtime codec
  function is reached. Derive any additional genuine service dependency and explain inclusion/exclusion.

Attack record length/version, wrong slots/namespace, count/holes, duplicate/full/first-owner rules,
last-owner/self-slot checks individually, save-before-success, no-op writes, invalid-vs-I/O recovery,
partial entropy, zero seed, fingerprint digest length/offset/input/format, output honesty, wipe guards and
real wiring. Every replacement must match exactly once, be useful, and restore source by hash.
Run whole selected batteries, not a subset of new entries; derive counts, never invent a mutation total.

B286/B316: measure free space and real input/artefact sizes FIRST, use at most two workers and a
source-identical isolated staging copy excluding only `.git`, `.pio`, `.pio-measure` and
`.claude/worktrees` artefacts where present. Preserve every ordinary tracked/untracked source/test/tool
input with before/after manifests; never delete the owner's build trees. B311: printable diagnostics may
avoid the UTF-8 trigger but do not fix it. B315: board capture directories must be new/empty beneath
repository `.pio-measure/`, outside its per-env build roots. Do not repair those instruments in this slice.

## 8. Complete gate, predictions and evidence report

Pre-check/Slice 2 cross-checks: native 2640 / 115288 / 0; Node native 222072, heltec Xtensa 117912,
gateway ARM 148680; gateway RAM/flash/objects 195844/512092/284, heltec_mobile 205684/1355292/328;
inventory 177; full primary-name union 49; tools 312. Re-record all starting figures from this base.
Read current s18 and ALL scenario anchors from `simulation/BASELINE.md`'s current 36/36 table, never a
historical headline. Do not copy a truncated old lus hash as a measurement.

Run and retain complete base/final transcripts:

1. `pio test -e native`, THEN RUN `./.pio/build/native/program`. Report real cases/assertions/failed;
   list per-case additions and prove their sum, existing base cases preserved. Re-sync the harness PIN.
2. Independent fingerprint reference, frozen literal comparison and a corrupted-expected-value control.
3. Full mutation union and all new/standing instrument controls with useful/RED/unusable/match/restoration
   evidence. Default six probes: console_sink, inbox_verbs, firmware_ui, custody_usb, ble_line, features.
   Re-derive changed pins in runner and wrapper tests together; unchanged pins remain pinned.
4. Tools unit sweep; inventory `--write` by coder then bare and `--check`; warning census's own pinned
   environment set with zero new warnings/zero switch warnings; `check_a0_matrix.py` and
   `check_data_type_literals.py`; both repositories `git diff --check`.
5. Paired simulator base/final build/executable/archive hashes and 36/36 validated anchored corpus plus
   comparison. Source-only edit predicts ZERO post-edit build actions and byte-identical executable/streams.
   Call that a checked no-op build, NOT a forced recompilation. Separately touch a genuine core build input
   in the isolated measured worktree as the recompile control, prove actual build actions and restored
   identical source/executable hashes; no simulator/core edit or re-anchor. Reuse the paired base build,
   not a build still pointing at a different checkout. Fresh base configuration actions are not movers.
6. `measure_board.py pair` at identical fixed-identity paths, **gateway + heltec_mobile only**; census's
   existing set is the sole exception. Separate live RAM/flash/sections/symbols from ELF/debug metadata;
   every byte of movement attributed. Predict no Node/RAM/new resident state, gateway flash increase and
   mobile live flash ±0; no tolerance invented. Measure new service/command stack demand. Run
   `probe_board_abi.py` and `probe_b278_row_abi.py`; new record asserts compile on both ABIs without adding
   their own duplicated PINNED entries. No ABI re-pin unless separately authorized.

Evidence includes bases/status/manifests, source facts/corrections, choices, pre-edit predictions, record
sizes, exact command/boot/guard transcripts, native accounting, reference reproduction, all gates and
controls, both mutation selectors/full union, board attribution/stack/compile-out, unchanged corpus table,
STOP audit, and every modified/untracked path in both repositories. State skipped/unreachable checks
honestly. Include the exact report line:

`PIN re-synced? YES — <derivation>`

New findings are proposed register rows with source/measurement, not edits to the register. Keep B312,
B313, B315 and B317 open for their separate obligations. No claim of an on-air RPC or a completed two-node
trust exchange; no metal-PASS inference from host fakes. Bench Part 55a remains pending owner execution.

## 9. STOP conditions

1. Missing/different base, dirty measured start, wrong paired simulator source, or unexplained concurrent
   input change. STOP to QA; no silent repair/repin or retrospective base measurement.
2. New owner policy, changed fingerprint/role/capacity, BLE widening, remote/codec/session/Node consumer,
   legacy-flow change, wire/main-NV version change, or edit outside the fence. STOP; no bundled fix.
3. Record ABI/layout or ten-slot binding differs; valid data is clamped/compacted; invalid ordinary or ANY
   io_failed state permits a write; failed/partial save reports provisioning success. STOP.
4. An entropy callback is converted to unconditional success, a zero/fallback seed is accepted, secrets
   escape to output/resident state, or synthetic draw tests are claimed as hardware/RF entropy proof. STOP.
5. Real router/store/guard path bypasses tested service, output ignores its supplied sink, an extraction
   is ambiguous/unreachable, a new surface has no ownership/transport proof, or a pin is repointed without
   executed-row accounting. STOP.
6. Any corpus/anchor/live mobile flash/Node/RAM movement contradicts prediction, board or stack cost is
   unattributed, or a claimed unchanged simulator build has no working recompile control. STOP to QA; if
   capacity/policy is implicated, owner rules. No new buffer/tolerance/re-anchor to force green.
7. Green/vacuous/multi-matched/unusable/misclassified control, lost worker, failed source restoration,
   failed standing gate, warning or unreproduced reference. STOP; no failed-compiler-as-RED scoring.
8. Power-cut/factory-repair behaviour is claimed atomic or identity-preserving without proof, a future
   controller/session operation is claimed implemented, or required evidence/metal residue is omitted. STOP.

## 10. Author landing after QA PASS

Read the coder evidence; land design §§6/19/19.1 **software-complete / Part 55a metal-pending**, actual
record/fingerprint/verb contracts in the manual, bench transcripts/fixture prerequisites in reserved
Part 55a, register findings/partial closures, tracker and MEMORY. No new on-air layout or frames.md update
for storage-only Slice 3. Mark ordinary regeneration preservation separately from B317's self-heal loss
and nRF52 partial-write exposure. Controller half remains Part 55b/Slice 4. Owner commits; record that
closure hash only when it exists. No owner ruling is outstanding for this brief's stated scope.
