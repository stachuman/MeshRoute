<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 4 — controller keyring and target book · draft brief · 2026-09-06

**Status: DRAFT — awaiting Quality-Agent review. NON-DISPATCHABLE: Slice 3 is still running.**
**Preliminary QA gate 2026-09-06:** otherwise PASS pending closure; the sole B321 layering fold-in is
applied in §§4.2/5, with the matching proof in §6. B320 is closed by QA's ledger correction. Final QA
re-gate still follows the completed §1 base/anchor checklist; this is NOT dispatch authorization.
`model: opus`. Author prepares documentation only; QA gates and dispatches; the owner commits and benches.
This is advance preparation authorized by the owner, not permission to run two implementation slices together.

Authority: QA's `docs/superpowers/plans/2026-09-06-radmin-slice4-precheck.md`, R-RA-30 in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`, and design
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §§6.1–6.4, 12.1, 14, 19/19.1/19.2.
R-RA-6/8/21/26/27/29 remain binding. Author decisions resolving pre-check §6.3–6.7 are explicit below;
they are proposed implementation contracts for QA review, not additional owner rulings.

## 1. Base, prerequisites and provenance — STOP until completed

- **MeshRoute dispatch base: PENDING — the owner's Slice 3 closure commit.** Not `7299eb9`, not a Slice 2
  base, not the in-flight coder's implementation-only commit. The owner commits the preparation package;
  the Author pins the actual closure hash here before QA's dispatch authorization.
- **Paired simulator base: PENDING — verify exact HEAD/status at Slice 3 closure.** The simulator is
  read-only in Slice 4. Record the real repository path and the paired build's `MESHROUTE_DIR`.
- **Slice 3 delivered anchors and baseline pins: PENDING — populate the checklist below from its closure.**
  Every current `file:line` in §3 was verified at `7299eb90c787c867cba3dc17902e87b838f5447c`; these are
  preparation observations, not assertions that the pending implementation already exists.

| Closure prerequisite | What the Author/QA must bind before dispatch |
| --- | --- |
| Slice 3 QA PASS, evidence and owner closure | Exact commit, report path, clean status and all measured starting pins |
| Shared fingerprint helper | Delivered file:symbol:line, public-key input and independent frozen vector |
| Checked local seed source and wipe guard | Delivered file:symbol:line, failure/zero-draw handling, lifetime contract |
| Target services and adapters | Delivered record/slot names, interfaces, boot function and real-router rows |
| BLE extractor and target guard | Delivered extraction API/anchors, exact target envelope, executed controls |
| B319 typed ACCEPT profile axis | Delivered six literal rows, generator fixtures, help/ownership counts |
| Feature ownership census | Delivered full literal per-file multiset (planned seven files), runner/wrapper pins |
| Mutations and standing gates | Delivered target names, both selectors, native/probe/tool/ABI/census pins |

All rows above must be resolved in place; no coder may resolve a pending base by assumption. QA then gates
the resulting brief. A clean isolated measured checkout is required: no permitted dirty preparation set.
Provide the reviewed brief out of band if necessary, with its hash recorded. No silent checkout repair,
repin, commit or cherry-pick. Capture both repository statuses and complete input manifests before edits;
audit concurrent changes, including Markdown, against what the instruments actually read.

## 2. Verbatim authority

R-RA-30, owner:

> **Owner:** *"Agree - implement the design's split, add the one-off xiao_mobile measurement"*

Design §6.2:

> The controller has exactly ten dedicated management slots, independent of the target's ten ACL slots. Each
> occupied local slot persists only one 32-byte master seed. Firmware derives the full `Identity` through
> `identity_from_seed()` when needed, uses the existing conversion/ECDH path, and wipes transient expanded
> secret material when the operation no longer needs it.

Design §6.2:

> `list` and `show` expose only public keys/fingerprints and may be used through USB or secured BLE. Generating,
> importing, exporting, or removing secret material is physical USB-serial only in the first implementation.

Design §6.3 (capacity STOP authority):

> The 32-row capacity must be measured on every essential controller ABI before its storage slice is approved.
> If it does not fit the accepted RAM/NV budget, implementation stops for an owner decision; it must not
> silently fall back to 16, reuse `/mrpeers`, or evict a live/pinned management target.

Design §6.4:

> Provisioning reports success only when the required target-identity and ACL records are valid and durable;
> partial failure remains physically recoverable and never invents an active remote owner. The controller
> private seed never enters the target, and the target administration seed never enters the controller.

Agent roles, step 4 (base-mismatch STOP):

>    starting work in an isolated worktree, the dispatched agent verifies and records `git rev-parse HEAD` against
>    the brief's named base commit; a mismatch is a STOP to the dispatcher, never a stale-tree measurement or a
>    silent worktree repair.

R-RA-29's fingerprint remains the one BLAKE2b-512(public-key32) first-eight-digest-byte function, rendered
as 16 lowercase hex; USB prints the full 64-hex public key alongside it. R-RA-30 permits only public client
list/show on secured BLE, not first-owner mutations. R-RA-28's reserved carrier capacities do not change;
Slice 4 has no carrier/codec consumer. The ruled board pair remains gateway + heltec_mobile. R-RA-30 adds
one sequential xiao_mobile measurement, not a third ruled board; census keeps its own existing pinned set.

## 3. Source checks and corrections to carry into the gate (V1/V2)

| Verified source at preparation HEAD | Consequence |
| --- | --- |
| `lib/core/mr_features.h:60`; `platformio.ini:518`–`:541` | Only the four mobile board envs derive CLIENT=1; hosts have both capabilities, but are not product profiles. No feature-header or platformio edit. |
| `tools/gen_command_inventory.py:202`, `:210`, `:735` | `full_*` map to real static boards, not hosts; the evaluator refuses unknown macros. Pre-check §3's original full_* 51→53 prediction was incorrect; QA has withdrawn it in place (B320 closed). Its corrected §3 and §1/§6.5 agree on the literal CLIENT matrix. |
| `tools/probe_inbox_verbs/run.sh:66`, `:160`, `:198` | Existing real-router arm is heltec_v3, ACCEPT-only. Add a separately compiled heltec_mobile arm, not a macro override of one TU against incompatible support objects. |
| `tools/probe_inbox_verbs/fakes/Preferences.h:38` | Current per-key fake payload is 512 bytes, below a 2056-byte book (and PeerBlob). Recheck Slice 3's delivered fake; extend medium capacity and key slots only as needed, preserving byte counts/faults. |
| `src/firmware_commands.cpp:60`, `:172`; `src/fw_main.cpp:277` | Public large records already use resident scratch to avoid the nRF52 fixed 4 KB startup stack. Keyring secrets do not inherit that residency. |
| `src/console_sink.h:73`; `src/dispatch_sink.h:63` | USB staging is 2048 bytes; LineSink is 1700 bytes per buffered line. A 32-full-key listing needs explicit bounded pages, not a larger sink or a silent drop. |
| `lib/core/command.h:43`–`:44`; `lib/console/console_parse.cpp:304`–`:323`; `lib/core/node.cpp:2165` | Stored cross-layer hints are destination hops, excluding the origin. The existing carrier prepends its layer: maximum three destination hops, not four. Four-byte storage is not a four-destination admission rule. |
| `src/firmware_config_parse.h:129`, `:224`, `:545` | Reuse public strict index/key/confirm parsers. The private console Tok/hash decoder is not an exportable helper. The public hex parser's local 32-byte buffer currently has no wipe guard (B321); seed import must not extend that lifetime hole. |
| `src/firmware_team_keyring.h:462`; `src/device_nv.h:623`, `:846`, `:885` | Existing scope wipe, four-state record IO, non-atomic nRF52 write and destructive self-heal limits are the authority. No new journal/repair or implicit trust-store initialization. |
| `src/firmware_commands.cpp:672`; bench Part 59 | Regen already uses its supplied sink, saves before live identity installation, and prints one success line. Client warning/debt admission are the only intended regen behaviour changes. |
| `tools/measure_board.py:93`, `:732`, `:866`, `:889` | The CLI and pair use a two-env table. Use §8's evidence-only one-off invocation; do not permanently add xiao_mobile to that table or widen pair concurrency. |

B320 is CLOSED: QA corrected pre-check §3 in place, retaining the withdrawn full_* prediction visibly;
the Author verified it against the product table and did not edit that ledger. B321 remains a narrow
seed-import safety dependency, explicitly fenced below. Its shared-parser wipe can affect gateway flash,
so the pre-check's zero-flash prediction there is qualified, not treated as a tolerance or hidden mover.

## 4. Author decisions — records, services and local contract

### 4.1 Record layout and residency (pre-check §6.3)

Freeze two independent version-1 records in `src/device_nv.h`; integer fields follow existing NV value
record ABI, not a newly invented network encoding. Static-assert size, alignment and every stated offset
on native, Xtensa and ARM compilers. These are target layouts to measure, not completed ABI results.

| Value | Frozen fields and offsets | Size / alignment |
| --- | --- | --- |
| `MgmtKeyRow` | seed[32] @0; reserved[4] @32 | 36 / 1 |
| `MgmtKeyBlob` | magic u32 @0 = `0x4D524D4B`; version u16 @4 = 1; count u16 @6; rec[10] @8 | 368 / 4 |
| `TargetRow` | admin_pub[32] @0; key_hash32 u32 @32; hops[4] @36; hop_count u8 @40; label_len u8 @41; label[16] @42; flags u8 @58; reserved[5] @59 | 64 / 4 |
| `TargetBlob` | magic u32 @0 = `0x4D525442`; version u16 @4 = 1; count u16 @6; rec[32] @8 | 2056 / 4 |

Recheck magic uniqueness against the closure. Slots are `/mrmkeys` and `/mrtargets`, with distinct backend
keys, typed four-state wrapper pairs and exact-length/version equality. Controller capacity is ten,
bound to key0…key9 grammar with its own constant; NEVER bind it to the codec's ten target ACL slots.
Book capacity is 32, independent of `/mrpeers` and its capacity/eviction.

Keyring empty rows are entirely zero. An occupied row has nonzero seed material and four zero reserved
bytes; count equals occupied rows, holes stay at their indices. Do not compact, replace or evict keys.
Full derived public identities must be unique across occupied slots and current self. Nonzero material
is not an entropy-health claim. Reuse Slice 3's checked draw source and identity derivation; no new HAL
adapter which turns a void draw into unconditional success. B312's RF entropy obligation stays open.

Book flags: bit 0 means occupied; every other bit is reserved zero. Empty rows are ALL zero. Occupied
rows require a valid nonzero full administration public key under the reused target public-key rules,
a nonzero routing hash, canonical label and hints, and zero reserved/unused bytes. Labels are 1–16
ASCII bytes from `[A-Za-z0-9_.-]`, case-sensitive, unique among occupied rows, with zero unused tail.
The full public key is immutable and unique; routing hash is replaceable metadata, never derived from
the administration key as a substitute for the operator's routing hint. GLOBAL is implicit (R-RA-12).
hop_count=0 means same-layer and all hop bytes zero; otherwise 1–3 nonzero destination layer IDs, unused
bytes zero. No fourth destination, clamp, automatic path repair or stored source layer. Slice 8b still
validates actual source/home/path reachability and R-RA-28 caps at send time; storage does not prove reachability.

**Residency:** one CLIENT-only resident `TargetBlob` (2056 bytes) used as public IO/candidate scratch,
not a live authority cache. Every access classifies/reloads the store; no reference escapes a call and
an unsuccessful candidate is invalidated before reuse. Build the candidate in that buffer after complete
validation; a row-sized edit buffer is permitted, a second whole-book stack or static buffer is not.
Startup and console access must follow the existing non-overlap/cooperative scratch ownership proof.
No heap container, Node member, persistent expanded identity, secret IO buffer or additional live table.
The keyring is guarded stack transient(s), not resident; measure actual composed stack peaks including
IO, candidate, derivation, parser and output frames rather than summing only individual record sizes.

### 4.2 Transactions, recovery and future callers

New pure header services use `IMgmtKeyStore` and `ITargetStore`-style seams, following Slice 3's delivered
interfaces by idiom; do not copy its fingerprint, checked seed source, public-key validation or wipe class.
Read states remain absent / ok / invalid / io_failed. Absent permits a first valid creation; invalid and
io_failed forbid EVERY ordinary write. Reset requires exact confirm and permits invalid recovery only;
io_failed refuses even reset. Reset of absent/healthy data is not a bulk-delete escape.

Parse and validate all input before constructing a candidate; successful semantic changes save at most
once, unchanged values save zero times, refusals save zero times. Save must succeed before publishing
success or adopting a derived identity. No post-failure RAM activation; no claim that the non-atomic
nRF52 backend preserved old FLASH bytes on a partial write. Failed/short write stays an error; the next
read classifies what survived. Preserve B317's self-heal qualification, mark it beside both new stores,
and keep them OUT of the legacy self-heal probe list. No boot write, generation or automatic recovery.

Generate/import only into a vacant keyN, compare against self and every other key's derived public key,
and refuse duplicates. Export returns exactly the selected seed only through the USB command's supplied
sink. Wipe all owned transient seed/expanded-identity/seed-hex/parse buffers on every exit, including
failed parsing. **B321 mechanism, QA fold-in:** include `monocypher.h` directly in the pure parser header
and define a function-local scope guard inside the EXISTING `parse_hex32`. Bind it to that function's
32-byte scratch immediately after declaration; its destructor calls `crypto_wipe` over the whole buffer
on every exit after allocation, including malformed input and success. No manual per-return wipe list
or optimizable memset. Keep grammar and output-on-failure unchanged. The earlier instruction to import
the team-keyring `SecretWipeGuard` here is withdrawn: its header includes device_nv.h and would invert
the parser's layering. Do not include either NV or team-keyring for this fix. Store services keep their
existing shared guard; do not move it into a new header (a separate C1 refactor). No second decoder,
broad parser cleanup or team-key behaviour change. Borrowed console input and
intentional USB export output are not falsely described as wiped by a service guard; do not log secrets
or retain another owned copy. Evidence uses disposable deterministic fixtures, never owner seed exports.

Removal refuses an in-use key; target set/remove refuses an in-use full target identity. Invalid reset
refuses if the corresponding store has any in-use reference. Supply these predicates through the pure
service seam, not a production runtime flag or an overridable feature macro. Slice 4's production binding
is explicitly no references because Slice 8a has no producer yet. Fake-predicate tests are labelled
**future-caller service tests**, not executed live-RPC/retained-result tests.

### 4.3 Verb family, selectors and bounded output (pre-check §6.3)

The CLIENT-only primary families are `admin-key` and **`admin-target`**. No new `remote` issuer yet.
Use the existing local router and transport-neutral seam; pure verb services take supplied line sinks,
not Arduino Print/globals. Freeze this grammar (metavariables are not literal angle brackets):

```text
admin-key list
admin-key show <self|keyN>
admin-key generate <keyN>
admin-key import <keyN> <64hex-seed>
admin-key export <keyN>
admin-key remove <keyN> confirm
admin-key reset confirm
admin-target list [page=0|page=1|page=2|page=3]
admin-target show <label=LABEL|fp=16hex>
admin-target add <LABEL> <64hex-public-key> hash=<0x1..8hex> [layer=<id[,id[,id]]>]
admin-target set <label=LABEL|fp=16hex> label=<NEWLABEL> hash=<0x1..8hex> layer=<none|id[,id[,id]]>
admin-target remove <label=LABEL|fp=16hex> confirm
admin-target reset confirm
```

keyN is exactly key0…key9, no self mutation/export and no key10/sign/leading-zero aliases. Use explicit
selector prefixes for book commands; both resolve to one stored full key, never to a routing hash.
No row-index trust selector. Unknown/ambiguous selector refuses, never first match or peer-book fallback.
If fingerprint collision handling is driven by synthetic digest values, label it so; do not claim to
have generated a real BLAKE2b collision. Upper/lowercase public-key and seed input follows parse_hex32;
fingerprints are canonical lowercase. Parse the bounded numeric hash with the existing reusable parser
if available at closure; otherwise a narrowly tested local numeric adapter is permitted (no export of
the private Tok helper). No unbounded atoi, partial parse or numeric overflow acceptance.

Book add takes the lowest free physical row; duplicate key/label and full refuse. Omitted layer on add
explicitly means same-layer. Set requires all three mutable fields and preserves the full key/slot;
layer=none explicitly clears the path. No key replacement, hidden defaults, compaction or eviction.
Confirm uses the existing strict token contract, with extra/trailing tokens rejected. Unknown family
subverbs/arguments are owned errors, not fallthrough to a second router or help topic.

List has four fixed pages of eight PHYSICAL slots. No page argument means page 0 by this contract;
there is no automatic multi-page dump. Print occupied rows on that page only, retain stable indices,
and print total occupied count plus page index in its footer. This is explicit pagination, not truncation.
Each public row includes full lowercase public key and fingerprint even on BLE; labels contain no
spaces/escapes under the frozen grammar. Use one LF per line and existing line-sink idioms, never an
oversized one-shot LineSink::printf buffer. Pin the maximal encoded row/page lengths and show a full page
fits the 2048-byte USB stage without drain; keep each line below LineSink's actual bound. Do not grow sinks.
The templates below predict a maximum 169-byte target row including LF and 1387 bytes for eight maximal
rows plus footer. Independently derive and execute these boundaries; these are not measured probe pins.

Successful output templates (fields named in angle brackets are derived values; no seeds in other rows):

```text
> admin-key self fp=<16hex> pub=<64hex>
> admin-key key<N> fp=<16hex> pub=<64hex>
> admin-key end count=<0..10>
> admin-key generated key<N> fp=<16hex> pub=<64hex>
> admin-key imported key<N> fp=<16hex> pub=<64hex>
> admin-key exported key<N> seed=<64hex>
> admin-key removed key<N>
> admin-key reset
> admin-target slot=<0..31> label=<LABEL> fp=<16hex> pub=<64hex> hash=0x<8UPPERhex> layer=<none|ids>
> admin-target end page=<0..3> count=<0..32>
> admin-target added slot=<0..31> fp=<16hex> pub=<64hex>
> admin-target updated slot=<0..31> fp=<16hex> pub=<64hex>
> admin-target removed slot=<0..31>
> admin-target reset
```

admin-key list prints self followed by occupied key slots then its footer; show prints just the requested
row. show self remains available when the dedicated store is unreadable; list and dedicated show refuse
unreadable state rather than imply an empty keyring. Book show uses the same row as list. Mutations
report only durable completion. Errors are `> <family> err <reason>`, with a typed closed vocabulary:
`bad_args`, `needs_confirm`, `store_invalid`, `store_io_failed`, `nv_save_failed`, `not_found`, `occupied`,
`duplicate`, `full`, `in_use`, `bad_material`, `entropy_failed`, `ambiguous`, `unchanged`, `not_invalid`.
Use only reachable family-specific values; unchanged is a zero-write non-mutation, never a saved success.
Reuse Slice 3's exact shared error values where applicable at closure; any mismatch must be reconciled
in this brief before dispatch, not silently translated by the coder.

Read-only CLIENT boot lines, with no public key/seed/fingerprint and no status/version JSON change:

```text
> admin-key boot state=<absent|ok|invalid|io_failed> count=<n>
> admin-target boot state=<absent|ok|invalid|io_failed> count=<n>
```

Only ok reports validated occupied counts; all other states print zero as non-active count, not as an
assertion that corrupt media contains no records. Record state is never collapsed into the count.

### 4.4 BLE split (R-RA-30)

On CLIENT builds one distinct subverb-aware guard in `ble_dispatch_line`, BEFORE `exec_console_line`,
allows only public list/show of admin-key/admin-target. Every other owned family form (including bare,
unknown, malformed mutation and reset) refuses with **`{"err":"admin-client","msg":"console_only"}`**.
An allowed list/show token still undergoes full command parsing; it cannot smuggle a second operation.
No secret operation reaches the seam/NV/draw provider. Keep the target R-RA-29 whole-family guard and its
different `admin` envelope unchanged. A target listing escape is a failing control; a client public
listing pass is the positive case. Match token boundaries, not unsafe family/subverb prefixes.

This is a secured BLE transport policy; it does not turn a bond into physical presence. Execute the
delivered generalized extractor against both distinct production guards and their real conditions;
wire any new helper through that extraction rather than reproducing its decision in Python. No fake
handwritten BLE router. Public command execution is separately measured by the real-router mobile arm.

### 4.5 Regen (pre-check §6.6)

Before any entropy draw, NV write or identity change, a CLIENT-only pure admission predicate refuses
source-bound RPC/assembly/retained-result/response-ACK debt with exactly `> regen err remote_busy` + LF.
Use a no-debt production binding until Slice 8a; tests with debt are labelled future-caller tests.
Preserve the existing save-before-activation sequence and existing success/name line. After that complete
line, on successful CLIENT regeneration only, print exactly:

```text
> regen note old self ACL grants do not follow the new key; dedicated keys and targets preserved
```

Both lines go ONLY to the supplied sink. No warning on refusal/save failure; ACCEPT output remains
byte-identical. Do not draw, load or rewrite either controller store during regen; prove preservation
with byte snapshots and IO counters, including failure paths. Re-derive inbox-verbs and console-sink
mobile transcripts plus Part 59's conditional mobile pins; existing ACCEPT pins must still pass.

## 5. Exact coder fence, including pre-check §6.4/§6.5

Feature/security increment, not a refactor or file move. No production/test/tool edit is authorized by
this draft while §1 is pending. After dispatch the only implementation paths are:

- `src/device_nv.h`: two value records, validators/slot entries/typed wrappers/layout assertions and
  explicit existing backend limits. No old record/schema/main NV migration or backend policy change.
- New pure headers `src/firmware_admin_keyring.h`, `src/firmware_admin_targets.h`,
  `src/firmware_admin_client_verbs.h`: controller services, grammar/output and future-caller predicates.
  Existing Slice 3 identity/fingerprint services are reused by include, not edited or duplicated.
- `src/firmware_config_parse.h`: B321 only — direct `monocypher.h` include, a function-local scope guard
  inside parse_hex32 whose destructor wipes its existing 32-byte buffer with `crypto_wipe`, and the
  explanatory comment. No NV/team-keyring include, shared-guard relocation/new header, parser decision
  change or new conversion path. All cleanup remains at this one function plus the direct crypto include.
- `src/firmware_commands.cpp/.h`: CLIENT IO adapters, one resident public book, boot entry, verb routing,
  supplied sinks and narrowly fenced regen changes; no unrelated dispatch/legacy/team behaviour.
- `src/fw_main.cpp`: CLIENT boot call and subverb-aware BLE refusal glue only. Feature decisions live in
  the pure module; no new runtime state, scheduler or command implementation in fw_main.
- `src/firmware_help.h`: two CLIENT-gated bare primary names in order, manual pointer unchanged.
  Detailed syntax lands later in `docs/manual/command-reference.md`, never help topics.
- New native tests `test/test_firmware_admin_keyring.cpp`, `test/test_firmware_admin_targets.cpp`,
  `test/test_firmware_admin_client_verbs.cpp`; additive `test/test_device_nv.cpp` and
  `test/test_firmware_config_parse.cpp` cases only. Preserve all Slice 3 cases.
- `tools/probe_ui_model_mutations.py`: new full per-file radmin4key/radmin4targets/radmin4verbs batteries,
  additive NV/parser entries, derived native PIN; no general harness repair (B286/B311 remain separate).
- `tools/probe_inbox_verbs/{run.sh,probe_main.cpp,transcript.py,transcript_main.cpp}` and its existing
  `fakes/Preferences.h`, `fakes/esp_random.h`: second complete build arm and new counted rows/controls.
  Preserve every old ACCEPT row and default-control invocation. Same production TU, core, console and
  support objects in each arm, all compiled under that arm's actual product defines; isolated outputs.
  Mirror heltec_mobile's effective OLED/profile settings, not just a guessed -D list. Extend fake medium
  facts for 2056-byte records, peer independence and honest IO failures; no policy in fakes.
- `tools/probe_console_sink/{run.sh,probe_main.cpp,ble_guard.py,structural.py,negctl.py,ownership.py}` and
  `tools/test_probe_console_sink.py`: distinct client guard/executed rows, six-profile help/ownership,
  derived pins in runner and wrapper. No weakening target whole-family controls or omission of help.
- `tools/gen_command_inventory.py`, `tools/test_gen_command_inventory.py` and its existing generated
  outputs: explicit typed CLIENT axis on top of Slice 3's ACCEPT axis; mixed subcommand transports;
  new literal surfaces/fixtures; coder regenerates with --write when command anchors move, then bare
  and --check pass. No deriving CLIENT from legacy or conflating MR_FEAT_MOBILE with CLIENT.
- `tools/probe_features/{ownership.py,run.sh}`, `tools/test_probe_features.py`: extend the exact closure
  census at CLIENT glue boundaries and add sabotage controls. Pure service headers remain ungated.
- New evidence `docs/superpowers/evidence/2026-09-06-radmin-slice4.md`. Evidence-only measurement snippets
  under gitignored .pio-measure are permitted; no permanent measure_board/pair/ABI target-list expansion.

The six typed product rows are fixed; hosts do NOT gain rows in this table:

| Product profile | ACCEPT (Slice 3) | CLIENT (Slice 4) | Slice 4 primary-name delta |
| --- | --- | --- | --- |
| full_oled | 1 | 0 | 0 |
| full_headless | 1 | 0 | 0 |
| gateway | 1 | 0 | 0 |
| gateway_oled | 1 | 0 | 0 |
| mobile | 0 | 1 | +2 |
| mobile_oled | 0 | 1 | +2 |

Native/lus still derive {1,1} from the header; the real-router probe never pretends this is a product.
Inventory must list 13 new subverb forms (seven key, six book) with list/show public `serial,ble` and
all remaining forms `serial` only. Root-family/alias rows are separately counted under the delivered
schema; derive total row delta before edit rather than equating 13 forms to every generated row.
Preserve router/parser disjointness on each profile. Combined name union gains two; full_* do not.

Census extends the delivered multiset, not a fresh auto-learned inventory. New CLIENT groups are adapters/
resident scratch/boot definition, dispatch, regen admission, regen warning in commands.cpp; boot declaration
in commands.h; boot call and BLE guard in fw_main; bare names in help. Bind exact expression multiplicities
and line anchors after Slice 3 closes. Include guards are organizational, never legacy-widened role gates.
Do not add core/API/Node consumers. Swapping CLIENT/ACCEPT, deleting a site, widening to legacy, adding an
unknown consumer or bypassing the pure decision must make controls fail; retain all old census/RX controls.

Excluded: `lib/core/`, `lib/console/`, platformio.ini, new firmware .cpp/build filters, legacy remote code,
radio/codec/session/custody consumers, main schema/wire_version, simulator files and BASELINE anchors.
The shared hex scratch wipe is the sole non-client behaviour-neutral safety change; no other gateway
movement is authorized. Coder does not edit the register, bench, manual, design, rulings or QA ledgers.

## 6. Executed proof and discriminating controls

1. **Value/IO and services:** assert layouts/offsets, magic/version/exact length, canonical padding/holes,
   count/full bounds, backend_failed vs absent and oversize/short reads. Exercise every reachable operation
   against all four read states, write-counted; no-op/refusal zero writes, success one, failed save no
   activation/success. Test valid/absent/invalid/io_failed reset distinction and confirm hardening.
2. **Key material:** known seed→full public identity, shared independent fingerprint literal and corrupted
   expected-value control, ten stable slots, duplicate versus self/another slot, zero/failed draw,
   export/import same principal on distinct test stores, in-use/any-in-use predicate refusals. Exercise
   the store services' shared wipe guard over live fixtures and inspect before deallocation, never
   dangling stack memory. For the decoder's function-local guard, observe the real `crypto_wipe` call
   through a test-only link wrapper in the isolated inbox-verbs probe: inspect the wiped bytes while
   the buffer is still alive, then return to production. No production callback or replacement decoder.
   Pin the 32-byte wipe on success and each post-allocation refusal; null input allocates no scratch.
   Removing the local guard's wipe must fail this executed observation, not merely a source grep.
   Structural/sabotage checks bind each secret local to its own guard and forbid NV-layer parser includes.
3. **Book:** all 32 rows, exact full refusal and no eviction, independent peer put/eviction leaves target
   bytes unchanged; same hash on different public-key rows does not merge trust identities; immutable
   key on set, unique label, fingerprint resolution/ambiguity, lowest-hole reuse, path count 0/1/3 and
   refusal 4, invalid/padded path, overflow/hash/label/token bounds. Pages 0–3 enumerate 32 physical slots
   without loss/duplication and maximal public output fits actual sinks.
4. **Real router:** execute both independently compiled inbox-verbs arms. CLIENT exercises every new
   grammar arm through dispatch and the production seam using USB and BLE-shaped LineSink as appropriate;
   each ordinary refusal measures stores/draws/no foreign handler, not merely a returned string. ACCEPT
   retains Slice 3 target rows, old regen output, and absence of client handlers/boot/resident state.
   A LineSink-shaped execution alone is NOT BLE admission evidence; item 5 owns that boundary.
5. **Actual BLE guards:** extracted production predicates accept client public list/show; refuse all
   key/target mutations, reset/bare/unknown forms with exact named envelope BEFORE seam. Target list/show
   remains refused. Prefix/suffix/whitespace, malformed token and helper-bypass controls; mutation escaping,
   public listing blocked, target listing escaped, wrong envelope, moved-after-seam and sink leak all RED.
   Executed source-bound extraction must not replace subverb ownership with a fixture's decision.
6. **Regen/boot:** real mobile success prints old line then exact warning to requested sink only; saved
   identity adopted only after save; failures print neither success nor warning, both stores unchanged.
   Pure synthetic debt test proves no draws/writes; source binding is checked without claiming active
   RPC reach. Boot reports four states/counts read-only; no auto-creation. Native fake factory erase and
   ordinary leave/regen slot independence supplement structural proof; actual flash remains bench.
7. **Tools/profiles:** typed CLIENT fixtures across all six profiles, missing/unknown-axis refusal, exact
   per-subverb transport sets, real router/parser intersection empty, code-derived primary names equal
   help output and manual pointer intact. Default controls mandatory; re-derive EVERY changed runner/
   wrapper pin from named rows. Deleting a row must fail a pin. No-controls modes remain PROBE-ONLY.
8. **Compile-out/ABI:** preprocess both ruled boards and one-off xiao_mobile; show client bodies/resident
   symbol absent on gateway and present on mobiles. Record exact compiler flags/sizeof/alignof/offsetof
   for both records and unchanged Node/B278 ABI pins. Do not duplicate the standing PINNED dictionaries.

Controls mutate copies of REAL source with exactly one match. For new controller tests ordinary RED
means a compiled, executed, diagnostic test failure: compile failure, crash, timeout, zero executed
rows or zero match are unusable, never killed mutants. The standing feature matrix's explicitly declared
production compile-refusal controls keep their existing narrow inverted classification and controls-of-
controls; that exception does not extend to new controller/guard/router mutations. Prove source hash restoration.

## 7. Mutation coverage — report two selectors, gate their union

- **Changed-source:** full devicenv; new full radmin4key/radmin4targets/radmin4verbs; full cfgparse AND
  sliceDtoken (both target the shared parser touched for B321). Name every configured target for any
  additionally changed source, or STOP if outside the fence. Router/glue controls are executed wiring
  controls, not falsely counted as native per-file batteries.
- **Historical/dependency:** full Slice 3 radmin3id (reused fingerprint/checked seed/public-key service),
  teamkeyring (wipe/persistence authority), cfgparse and sliceDtoken. Include radmin3acl/radmin3verbs
  only if delivered shared helpers create a real acceptance dependency; importing an unused type is not
  such a claim. Resolve this from the closure, explain inclusions/exclusions; no arbitrary battery sample.

Deduplicate the union; report each selector and every full battery's total/useful/RED/unusable/restored
counts. No unknown survivor or baseline failure. New mutations cover grammar, exact identity selection,
all read states, confirmation/in-use/debt, commit ordering/count, seed wipes, duplicate/self checks,
full/no-eviction, immutable trust key vs routing hint, path/page bounds and named output/transport decisions.

Use isolated mutation staging with two workers. B286's existing copy-size hazard and B311's non-UTF-8
worker failure are not repairs in this slice: exclude measured build/git/worktree artifacts before the
manifested staging copy, preserve every required source/fixture, use printable diagnostics, and report
a lost/unusable worker honestly. No mutation runs concurrently with source-dependent board captures in
the same checkout. Reuse standing staging discipline; no unrelated harness rewrite.

## 8. Full gate, one-off measurement and predictions

Starting numeric pins are PENDING §1. Derive every figure from the actual closure and final execution;
do not copy Slice 2 numbers or planned Slice 3 totals. Run base gates BEFORE any production/test/tool edit.

1. Native `pio test -e native`, then RUN `./.pio/build/native/program`; exact cases/assertions/0 failed,
   all old cases retained and per-case addition sum. Shared independent fingerprint/reference checks
   remain; no production-generated expectations.
2. Six standing probes (console_sink, inbox_verbs, firmware_ui, custody_usb, ble_line, features) with all
   controls, both inbox arms; tools unit sweep; complete mutation union; inventory --write/bare/--check;
   warning census's existing pinned envs and zero switch warnings; check_a0_matrix.py and
   check_data_type_literals.py; whitespace checks in both repos. Explain each pin movement or unchanged pin.
3. Simulator correctly paired to measured tree: base/final hashes and build logs, predict **zero post-edit
   build actions**, byte-identical executable and 36/36 streams validated against CURRENT BASELINE.md,
   including exact s18 keystone. This is a checked no-op build, not forced recompilation. A separately
   executed recompile control on a real input must cause build actions; restore input and final hashes.
   No re-anchor or simulator diff. Initial configuration/build actions belong to the base capture.
4. Ruled pair via measure_board.py at identical fixed-identity paths; Node and B278 ABI probes at pins.
   Predict CLIENT RAM +2056 bytes for the sole book scratch plus explicitly attributed alignment/storage;
   gateway RAM unchanged. Mobile flash includes services/verbs/guard and shared scratch wipe. Gateway
   flash is unchanged EXCEPT any live shared parse_hex32 wipe cost (B321), isolated by symbols/disassembly;
   no client symbols may survive there. Attribute sections/objects/symbols/payload/debug metadata
   separately, no invented byte tolerance. Measure stack peaks on both compiler ABIs.
5. R-RA-30 one-off xiao_mobile base/final RAM/flash/objects/sections/symbols/stack, same fixed identity
   and exact paths, sequential and outside the pair. This is not a third permanent board or ABI pin.
   The evidence-only adapter below reuses the runner unchanged; pin its exact text for BOTH captures,
   verify its assumptions against the closure before using it, and retain manifests like the pair:

```python
import sys
sys.path.insert(0, "tools")
import measure_board as measurement
assert set(measurement.ENVIRONMENTS) == {"gateway", "heltec_mobile"}
measurement.ENVIRONMENTS = dict(measurement.ENVIRONMENTS, xiao_mobile="firmware.hex")
with measurement.MeasurementLock():
    output = measurement.validate_output_dir(sys.argv[1])
    measurement.run_measurement("xiao_mobile", output)
```

Run from the measured repository; output must be a fresh path below its gitignored .pio-measure but
outside .pio-measure/env (B315). This process-local extension is not an edit to the runner, never calls
pair, and holds the same lock; there is no new concurrency authorization. Do not flash fixed-identity
measurement images. If the delivered runner cannot faithfully capture this env, STOP and return the
instrument requirement to QA, not an ordinary uncontrolled pio-size substitute.

6. NV/stack budget: new serialized storage is 368 + 2056 = **2424 bytes**, independent of other stores.
   Derive a per-profile sum of the actually coexisting records and backend capacity/metadata overhead
   (NVS/FS/inbox consumption), not just a raw-byte sum implying guaranteed fit. Record .bss allocation
   and worst composed startup/console frame demands on Xtensa and nRF52, including LineSink/crypto.
   If the accepted budget is exceeded or headroom cannot be justified, STOP for owner decision under
   design §6.3; no 16-row downgrade, heap/stack fallback or enlarged task stack.

Evidence `docs/superpowers/evidence/2026-09-06-radmin-slice4.md` must contain both bases/statuses/input
manifests, resolved source anchors, pre-edit predictions, records/ABI/budgets, named Author decisions,
complete real-router and guard/output transcripts, base/final gate logs, both mutation selectors/union,
all controls/classifications/restoration, pair and one-off attribution, exact corpus results, STOP audit,
and every modified/untracked path. Include the exact report line:

`PIN re-synced? YES — <derivation>`

Propose new register rows in evidence findings with source/measurement; do not edit Author/QA docs.
Explicitly identify fake future-caller tests and metal not run. No on-air RPC, session, actual BLE notification,
cryptographic power-cut recovery or healthy entropy claim follows from these host gates.

## 9. STOP conditions

1. Any §1 placeholder, missing Slice 3 QA/closure, base mismatch, dirty measured start or unexplained
   concurrent input change. STOP to QA; no silent repair/repin or retrospective baseline.
2. An owner decision is needed for capacity/RAM/NV, physical authority, capability split or trust format;
   any out-of-fence edit, remote/codec/Node consumer, schema/wire bump or simulator mover. STOP, do not widen.
3. Unreadable state writes, implicit recovery/default/eviction, seed leak, missing owned-transient wipe,
   duplicate principal accepted, partial save acknowledged or failed candidate activated. STOP.
4. Client mutation crosses BLE, public list/show is disabled there, target listing escapes its old guard,
   a real-router arm is synthetic/mixed-profile, or generated inventory/census differs from its literal
   ownership contract. STOP; no re-pinning around missing coverage.
5. Book residency/size/capacity changes, stack/NV budget fails, compile-out fails, RAM/flash movement is
   unexplained or one-off capture is not comparable. STOP; no widening pair or accepting tolerance.
6. Any failed standing gate, corpus delta, survivor, unusable control/lost worker, count/anchor mismatch,
   absent restoration or skipped mandatory gate. STOP; report output and cause, never a fabricated PASS.
7. A pending-request/retained-result or metal arm is reported as production-executed before its producer/
   hardware exists, or evidence omits a finding or required bench residue. STOP.

## 10. Author landing after QA PASS

Author reads QA-passed evidence, closes only proven register items in place (B321 if fixed; B320 is
already closed by QA's pre-check correction), and lands measured design §19/19.1 plus the final local
grammar in the sole detailed manual. B312, B317 and old metal debts remain open for their actual
obligations. No evidence edits.

Pre-check §6.7 is resolved: bench **Part 55b** is controller-side two-node USB trust exchange against a
Part-55a target; **Part 56** is real seed-store/export/import lifecycle and secured-BLE public list/show
versus mutation refusal on xiao_mobile. Both are drafted in the maintained bench script, NOT RUN.
Part 59 gains the conditional client warning; replace placeholders with coder/QA transcripts before
owner execution. Ordinary erase/power-cut/physical-capture limits remain explicit; no simulated result
closes a metal row. Owner commits; the Author neither commits nor dispatches Slice 5.
