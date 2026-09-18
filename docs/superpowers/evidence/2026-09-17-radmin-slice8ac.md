# Remote-admin 8a+8c — coder preflight and R1 allocation model — 2026-09-17

**Latest checkpoint: revision 3, §5 below — R1 settled/re-measured; STOP-1 B405 (fence).**

**Historical revision-1 STOP-1: B403, invalid-key conversion contract disagrees with the existing source. Implementation also
awaits the already-requested R1 allocation ruling.** R2/R3/R4 are settled by R-RA-42/43/44. No production,
test, tool, authority-table, inventory or brief edit; no implementation gate or independent QA PASS claimed.

[Reproduction, input inventory, compiler commands and model](2026-09-17-radmin-slice8ac-preflight/README.md).

## 1. Inputs and settled rulings

Authorized brief SHA-256: **d7e4cf2a6098b5ce3fd7626cdeefc0080b8fbef4a13681416ae30b158a358521**.
The file remains byte-identical. Its source base is `6086152b97b5934971d231a345b9939d5f2db1e9`;
actual HEAD is `e3a5fa03eab11806b05094953a04e7c1524f221a` (`Doc update`). All 44 committed paths between
those hashes are documentation/evidence; all production/test/tool inputs match the brief base. Both hashes
and the complete 2147-input inventory are recorded rather than silently treating HEAD as the named base.
The six permitted uncommitted preparation files are individually hashed: brief, MEMORY, ledger, register,
design and tracker. The four newly announced QA changes are included. Simulator
`06746a97de5764415d6fcef10b97bca90569b9c7` is clean and unchanged.

R-RA-42 supplies the four controller-local verb families and the execute/open/rollover forms, USB plus
secured BLE, structurally absent on ACCEPT. Its table/header/inventory landing must form one gate input
set; no table row is changed in this preflight. R-RA-43 puts B292's one-write fix in 8c. Existing
`mrble::tx_line` already chunks by the **negotiated** MTU minus three (20-byte fallback); reuse it for the
direct reply, respecting both smaller negotiated payloads and the configured 244-byte maximum. R-RA-44
leaves automatic silent-request retry timing to 8b; exact manual retry and the single session-full safe
rollover/fresh-ID resend belong here.

84 symbol anchors were located. Two initial scanner assumptions were corrected without source changes:
label lookup is `target_mask_by_label`, and the legacy printer uses separate `print("[rcmd ")` calls.
`admin_client_router_arm` lives in `firmware_commands.cpp`, not the service header; this is a P4 symbol
relocation, not a behavior disagreement. The semantic disagreement below was reproduced separately.

## 2. B403 — invalid Ed point is accepted by the existing target key helper

Brief §3 and §7 require invalid conversions to refuse and describe the controller as mirroring the target's
existing checked conversion. At the pinned source:

- `lib/core/identity.cpp:43`, `ed_pub_to_x25519`, is a **void** wrapper around `crypto_eddsa_to_x25519`.
- `lib/monocypher/src/monocypher.c:2342` computes `(1+y)/(1-y)` without checking that the supplied compressed
  Edwards point has an x coordinate.
- `lib/core/remote_codec.cpp:264`, `remote_ecdh_shared`, rejects an **all-zero X25519 shared result**.
- `lib/core/remote_session.cpp:171`, `derive_base`, performs that conversion, shared-result check and KDF;
  it adds no Ed-point validity check.

The labelled synthetic C++ reproduction includes the **unchanged actual remote_session.cpp** and calls its
real `derive_base`, linking the freshly built production identity/codec/crypto objects. It passes **46
characterization checks**: a valid derived identity succeeds; a low-order input refuses and preserves the
32-byte output; four invalid compressed Edwards encodings, with `y = 2, 7, 8, 11`, all return `ok` and publish
a base key. This is observed acceptance, not a guessed consequence of the void signature.

A separate integer-arithmetic check proves all four are off-curve: with `p=2^255-19` and
`d=-121665/121666 mod p`, `(y²-1)/(d*y²+1)` has Legendre symbol -1. Thus no Edwards x exists, even before
subgroup or canonical-encoding policy is considered. This follows the point-decoding rule in
[RFC 8032 §5.1.3](https://www.rfc-editor.org/rfc/rfc8032.html#section-5.1.3).
The [Monocypher API documentation](https://monocypher.org/manual/eddsa) also declares conversion to return
nothing. The local vendored source and executed helper are the behavior authority here.

**Scope:** this reproduces a validation-contract mismatch, not an authentication bypass. The helper is
called with synthetic inputs; this is not a forged, end-to-end authenticated RPC. The target-book content
validator (`firmware_admin_targets.h::target_content_valid`) checks nonzero public material, not Edwards
point validity, so the controller must not treat loading a row as that validation proof either.

**QA disposition needed before coding:** specify the checked controller input-validation boundary and its
invalid/off-curve/noncanonical/low-order controls. Reusing the existing conversion and shared-secret guard
alone cannot satisfy the brief. The target/codec are explicitly out of fence; do not silently broaden their
behavior or weaken the controller's promised rejection. A shared helper touching the target requires an
explicit fence decision. B403 is registered as open; no fix is included here.

## 3. R1 — concrete proposed state, not a production allocation

The historical R-RA-22 rows reproduce **4272 bytes** on all three ABIs. Adding only the six required u16
counters gives **4288**, including four bytes of final alignment. That historical total therefore cannot
be carried forward as a complete new state size without attribution.

The supplied proposal preserves every historical field and the exact 4/4/2/8/2/8 capacities. Its shape is
in `layout/controller-model.h`; it is deliberately outside `lib/`, `src/`, `test/` and `tools/`.

| Row | Count | Historical → proposed bytes per row | Aggregate increase |
|---|---:|---:|---:|
| Pending inline | 4 | 336 → 360 | +96 |
| Session cache | 4 | 144 → 144 | 0 |
| Assembly header | 2 | 24 → 32 | +16 |
| Response chunk | 8 | 210 → 210 | 0 |
| Retained-result header | 2 | 24 → 32 | +16 |
| ACK debt | 8 | 72 → 88 | +128 |
| Six diagnostic counters | 1 | 0 → 12 | +12 |
| Aggregate tail alignment | — | 0 → 4 | +4 |
| **Total state** | | **4272 → 4544 / align 8** | **+272** |

The additions own specific obligations:

- Pending: a separate 64-bit discovery ID while the old execute ID and sealed bytes survive manual
  bootstrap/epoch comparison; immutable target routing hash/three-hop path; explicit local credential and
  target-book indexes for the existing in-use guards; an opaque `uintptr_t` USB sink binding.
- Session: credential/book indexes fit its existing two tail bytes. Keys remain the existing bounded
  base/session keys; no seed or second identity object is made resident.
- Assembly and retained headers: typed result domain/code and u32 detail. The retained header owns the
  scheduled delay or admission detail after assembly release; no borrowed decoder span or output-buffer tail.
- ACK debt: frozen SOURCE_HASH, route, carrier and local indexes survive pending/result release. No later
  source or credential reconstruction from mutable UI state. The existing 25-byte sealed ACK remains inline.
- Counters: the six separately saturating values required by the brief. No new timer or separate correlation
  pool. The existing delegated ring remains the owner of `-a` reservation; the pending row records its state.

The USB binding is an explicit lifetime requirement, not permission to retain a temporary Print. Production
USB supplies the long-lived `mrcon`; test callers must keep their supplied sink alive until completion.
BLE stores **no pointer** to its stack `LineSink`. A failed/released request clears the binding. The pointer
is 8 bytes native and 4 on boards; alignment before the following u64 makes this particular row 360 bytes
on all three. No resident carrier object, entropy adapter or global sink registry is proposed.

The real Node header is measured first with its production assertions intact. Private shadows then remove
only the legacy client member and insert the proposed CLIENT-only block after `_channel_seal_ctr`; their
native assertion is explicitly model-only. No production pin changed. The same toolchain/defines measure:

| ABI/profile | Baseline Node | Proposed Node | Delta |
|---|---:|---:|---:|
| Native ACCEPT+CLIENT | 230976 | **235280** | **+4304** |
| ARM gateway ACCEPT | 157344 | **157344** | **0** |
| Xtensa heltec_mobile CLIENT | 117912 | **122208** | **+4296** |

All Node alignments remain 8. The legacy type itself is **245/1**, but its whole-Node credit is **240 native**
and **248 mobile**, not a hand-subtracted 245. Measured offsets explain the eight-byte difference: native
`_cfg` 9376→13680 and mobile 432→4728; gateway stays 9064. New block starts at native 232/mobile 184.
The brief's predicted equal native/mobile growth is therefore not the measured result, even with an identical
4544-byte block. Exact offsets and compiler commands are retained.

45 historical member-type checks and 25 additional member-width/presence checks pass in each of six
baseline/proposed ABI compilations. The standing ABI probe separately passes 218 checks and nine controls.

**R1 recommendation for review:** approve this explicit 4544-byte CLIENT block and the measured Node pins
235280 native /122208 mobile /157344 gateway, with all further owned-state growth returning as STOP-1.
These are **compile-only candidate values**, not linked RAM permission by arithmetic. The linked gate must
also attribute removal of the separate `fw_main` static `ri` (245-byte type, outside Node), new TU/linker effects,
and transient stack. No board link or xiao_mobile flash measurement was run in this preflight.

## 4. Fresh checks, preservation and next checkpoint

- Native wrapper and executed binary: **2931 cases /189998 assertions /0 failed /0 skipped**.
- Standing ABI: **218 checks /9 controls RED /0 unusable**.
- Independent reference: **94/94**, old **89/89** unchanged; five comparator controls RED.
- Private layout: three ABIs, historical/counters-only/proposed variants, full Node and offsets.
- Real-helper invalid-point characterization: **46 checks /0 failed**, four off-curve acceptances reproduced.

Iteration records are kept: an initial model include-path failure; a generated-model newline that hid the
new route in a comment, detected and corrected before reporting the final model (explicit new-member checks
now cover it); an earlier less-efficient placement before the channel counter; two mistaken scanner names;
and an initial reference invocation using the wrong directory. None supplies a final figure.

Corpus, board links, census, full tools/probes/mutation union and implementation freeze were **not run**.
This is source-validation and allocation preparation; the prior 7b-3 PASS is not relabelled as an 8a+8c gate.

The only shared edits from this turn are this receipt, its evidence directory and the B403 register intake.
The brief and all production/test/tool inputs remain unchanged; other QA preparation bytes are preserved.
No reset, clean, commit, simulator edit or shared build/mutation run. QA can now resolve B403 and present R1's
concrete allocation for the owner; the next resume re-inventories those documentation inputs. No commit is
needed to continue once those contract conditions are settled.


## 5. Revision 3 resume — R-RA-45 re-measured; STOP-1 B405 — 2026-09-18

**R1 is settled. B403 is closed as contract precision; B404 stays parked.** The remaining STOP at this checkpoint
is the §6 fence omission below, not allocation, point validation or a missing commit. No production edit or
implementation freeze is claimed. [New input inventory, model and reproduction](2026-09-18-radmin-slice8ac-r3-preflight/README.md).

### 5.1 Resume identity and preservation

Revision 3 SHA-256 is **8b434921d3893e390fc77844f31c870b0e7ab597eeea85046cdda6981d882b13**; source base remains
`6086152b97b5934971d231a345b9939d5f2db1e9`, actual HEAD remains the documentation-only successor
`e3a5fa03eab11806b05094953a04e7c1524f221a`. The complete **2259-input** inventory includes the prior untracked
coder receipt/evidence and all QA preparation. Since the prior checkpoint the changed original inputs are
exactly the reissued brief plus the announced ledger/register/design/tracker; MEMORY and production/test/tool
inputs remain unchanged. The six preparation files are retained with exact bytes and hashes. Simulator
`06746a97de5764415d6fcef10b97bca90569b9c7` stays clean. **84/84** named source anchors are still present.

R-RA-42/43/44/45 are read as settled. R-RA-45 replaces the stored USB pointer with a local-transport tag and
an injected delivery-time binding; the model makes only that field removal. Revision 2 bytes were not supplied
in this checkout, so the receipt pins the actual authorized revision 3 rather than claiming a byte comparison
to the revision-2 hash prefix.

### 5.2 R-RA-45 allocation first: exact re-measurement

| Profile | Baseline Node | Pointer-free Node | Delta |
|---|---:|---:|---:|
| Native ACCEPT+CLIENT | 230976 | **235248** | **+4272** |
| Xtensa heltec_mobile CLIENT | 117912 | **122176** | **+4264** |
| ARM gateway ACCEPT | 157344 | **157344** | **0** |

The complete controller block is **4512 bytes / align 8 on all three ABIs**, down 32 from the pointer-bearing
4544-byte proposal. Each pending row is now **352** instead of 360 bytes. The four rows account for that entire
−32; all other fields/capacities stay intact. Composition: `4×352 + 4×144 + 2×32 + 8×210 + 2×32 + 8×88 + 12`
counter bytes + 4 final padding = **4512**. The retained type of the removed legacy slot is 245/1; its whole-Node
credit remains 240 native /248 mobile because of surrounding alignment. All Node alignments remain 8.

Twelve layout compilations (baseline/historical/counters-only/re-measured, three ABIs) succeed. Six private
offset compilations additionally enforce **45 historical member-type checks +24 added member-width checks**
each. Exact compiler arguments, offsets, objects' hashes and final summary are retained. Private headers use
the measured replacement; production Node and its assertions are untouched.

These are the values authorized by R-RA-45's formula. **Linked RAM and xiao_mobile flash have not yet been
measured.** The eventual link attribution must still include removal of fw_main's separate static `ri`, new
TU effects and any stack impact; no linked result is inferred by subtracting 245 from a RAM total.

### 5.3 B405 — required legacy replacement crosses the written fence

The brief expressly removes the client staging slot and drain, but §6 omits three files that still own that
implementation and its mandatory gate:

1. **`lib/core/node_mac.cpp::Node::take_remote_inbound` (:884–891).** Its CLIENT body directly reads and writes
   `_remote_inbound`. A byte-identical copy of this production TU compiles on native, ARM and Xtensa with the
   real baseline header. Against the R-RA-45 private replacement header, native and mobile both fail at :885
   and :886 with **`'_remote_inbound' was not declared in this scope`**; gateway still compiles. This is six
   compiler invocations with the predicted outcomes, not a inferred link problem or a line-number drift.
2. **`test/test_node_r3.cpp` (:6411, :6463, :6596, :6613, :6636, :6669).** Six actual test bodies contain twelve
   drain calls; five positively require legacy responses to stage/drain and one checks the ACCEPT refusal.
   They need in-place supersession to CLIENT consumption/drop and no app/inbox/push leakage, retaining the
   direction-ownership coverage. Leaving a false compatibility stub would still fail their positive assertions.
   This is a static caller/expectation census, not an executed replacement suite.
3. **`tools/probe_features/ownership.py` (`APPROVED_SITES`, O6c and the legacy slot/drain controls).** All **25**
   checks returned by its unchanged `run_checks` pass on the baseline. A labelled private removal of only the
   obsolete staging declaration/definition/call is rejected by **O6c: 0 guarded sites, want 3**. Its exact
   `take_remote_inbound` and fw_main legacy-drain mutation anchors likewise require supersession. These
   controls must follow the new owned consumer/state and continue rejecting absent/wrong/ungated consumers;
   deleting or weakening ownership checks is not the proposed repair.

**Required QA fold-in:** add those three narrowly scoped paths to §6 for the replacement and its existing
proofs. Keep all carrier send functions in node_mac.cpp untouched. Include supersession of historical
staging mutation entries **1b-09..1b-13** in the already-named mutation runner: after staging removal their
old needles cannot remain live gate controls. Preserve/reforge their relevant ownership, pressure, binding
and byte-integrity obligations on the v2 path; report retired semantics and configured-count changes honestly.
No new owner policy/allocation ruling is needed. This source-dependent scope repair belongs at this preflight
checkpoint; the coder has not edited the brief or crossed its fence.

### 5.4 Checkpoint limits and iteration record

No new native wrapper/binary run, corpus, reference, complete probe chain, board link, mutation union or
implementation gate ran on this resume; the earlier receipt's figures remain historical. Fresh evidence here
is the three-ABI layout, field/offset audit, six real-TU compile outcomes, source anchors and the source-reader
refusal. The model is not implementation and the two deliberate compile failures are the evidence for B405.

An initial inventory attempt tried to hash the directory symlink `spec/docs` as a file; it stopped before shared
edits and was corrected to record the link target. An initial caller census counted a historical comment after
the pure ownership test as a seventh case; stripping comments yields the six executable test bodies above.
Those corrections do not supply any final implementation/gate count.

Only this receipt, the new evidence directory and B405 register intake changed in the shared checkout.
The brief, production, tests, tools, prior evidence and other QA preparation bytes are preserved. All work is
uncommitted; no staging, reset, clean, simulator edit or shared mutation/build was performed. Resume after the
QA fence fold-in, with this allocation already measured and R1 settled.

## 6. Revision 4 coder source-validation and measured implementation (2026-09-18)

This supersedes the revision-3 B405 stop for the explicitly enlarged fence. No ruling remains open.
Source base is `6086152b97b5934971d231a345b9939d5f2db1e9`; checkout HEAD is
`e3a5fa03eab11806b05094953a04e7c1524f221a` (documentation-only successor at admission).
The authorized revision-4 brief is frozen at SHA-256
`0d45d7b729958a4061db037a71f7bcac81d8881f75f3fd1b70b4563604f1c27a`.
No commit was a prerequisite. No source anchor disagreement was found against revision 4.

The complete preparation snapshot, including uncommitted and untracked inputs, is
`/tmp/mr-s8ac-r4-b1edsao2/snapshot`. Its 2327-path inventory and the separate six-document QA preparation
inventory are preserved with this receipt's archive. Those six inputs are the brief, ledger, register,
design, tracker and already-dirty MEMORY entry. All six hashes remain unchanged. The source-validation
and gate snapshots contain the actual working inputs, not just the last commit.

The simulator remains at `06746a97de5764415d6fcef10b97bca90569b9c7`, with no authorized or performed
source edit. Its explicit CMake source list does not name the new `remote_client.cpp`; the isolated gate
uses an archived, build-only CMake deferred `target_sources` hook to compile that exact file in both
normal and gateway core variants. This is a disclosed build integration input outside the simulator
checkout, not a claim that its stock source list gained the new TU.

### 6.1 Implemented scope

- The pure controller owns the R-RA-45 block, captured identities and routes, checked random request IDs,
  bootstrap/session cache, retained sealed requests, bounded assembly/results, local delivery state and
  exact sealed ACK debt. RX authenticates and owns bytes; local delivery and retries run on the main loop.
- Manual retry authenticates the current epoch before replaying the exact retained bytes. A changed epoch
  reports unknown. `session_full` permits one safe rollover and a fresh-ID reseal of the unexecuted request.
  Subsequent local events and ACKs use that fresh ID, as the companion contract records.
- The four CLIENT verbs use the existing router, selected key/target services and supplied transport.
  Ephemeral selected seeds and expanded identities are wiped. Live key/target use and regeneration debt
  bind to the actual controller block. Status exposes all six saturating counters.
- USB checks both accepted byte counts and console-stage drops. BLE frames bounded JSON events, preserves
  UTF-8 across owned chunk boundaries, retains until explicit local ACK and re-offers from zero after
  reconnect. A NUL-containing or invalid UTF-8 result is retained for USB rather than silently truncated.
- Direct BLE command replies use the existing `tx_line` path and its negotiated MTU-minus-three bound.
  The production request/ACK carrier explicitly returns `carrier_unavailable`; this slice sends no
  controller RPC bytes on air. Native two-Node tests bind a labelled loopback carrier instead.
- The old Node staging slot and main-loop `ri` are removed. Legacy `rcmd` remains; its unowned responses
  are counted and dropped. Both sides retain their profile guards.

### 6.2 R-RA-45 allocation, historical implementation checkpoint

| Item | Native | Heltec mobile | Gateway |
| --- | ---: | ---: | ---: |
| Controller block size/alignment | 4512 / 8 | 4512 / 8 | type 4512 / 8, member absent |
| Node size/alignment | 235248 / 8 | 122176 / 8 | 157344 / 8 |

The block contains four 352-byte pending rows, four 144-byte session rows, two 32-byte assembly headers,
eight 210-byte chunks, two 32-byte retained headers, eight 88-byte ACK rows, twelve counter bytes and four
tail-padding bytes. There is no resident sink pointer or additional owned state. The existing Node-size
assertion in `test_custody_receive_g.cpp` is re-pinned alongside the approved Node pin; it changes no test logic.

| Linked deterministic pair | Base RAM | Final RAM | Delta | Base flash | Final flash | Delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Gateway | 204036 | 204036 | 0 | 574752 | 574896 | +144 |
| Heltec mobile | 207756 | 211772 | +4016 | 1373604 | 1392752 | +19148 |

Both pairs ran gateway then mobile with one build job and fixed identity/stamp. The final pair uses a
separate complete checkout so other gate tools cannot change its normal `.pio` directory. The first final
pair attempt was rejected by that preservation check and is not counted. Xiao mobile's required one-off
flash measurement is 697308 bytes. All six warning-census environments retain their existing warning pins,
with zero `-Wswitch`.

Symbol attribution on mobile: `g_node` grows 117912 → 122176 (+4264); the 245-byte
`mesh_service_once::ri` disappears; three bytes of linker padding disappear, yielding +4016 linked RAM.
The two existing 512-byte dispatch buffers change mangled names with the explicit transport argument,
but their sizes and count do not change on either board. Gateway resident-object net change is zero.
The measurement tool's `compare` subcommand checks repeatability of one input tree, so its expected
refusal on distinct base/final trees is not used as a delta instrument; the manifests, sections and
symbol inventories supply the recorded attribution.

### 6.3 Proof scope and instrument updates

At this historical checkpoint the native runner measured 2945 cases / 193601 assertions / zero failed / zero skipped. Final measurements and the two subsequent recovery fixes are recorded in §7.
Two real Nodes cover all nine terminals, scheduled detail, the protocol-error domain, all five admission
codes and session-full → safe rollover → fresh-ID execution. Pressure/tombstone preconditions are explicitly
labelled synthetic fixtures; target admission, codec, MAC TX, controller MAC RX and local delivery run real code.
The shared eight-chunk pool packs OPEN frame tails and retains its entire 1648-byte budget atomically.

The independent Python identity/KDF/AEAD reference matches the controller KAT. Its first request-nonce
reference omitted the design's sequence-zero term; the original output is retained and labelled historical.
The corrected reference includes that term. Production was not changed to match the erroneous reference.
An additional independent Edwards-group enumeration supplies all eight canonical low-order peers:
264 real-controller checks pass, preserving the caller's output on refusal; a false-success control is RED.
The four B403 off-curve characterization inputs remain accepted by conversion and fail to authenticate
against the actual target; no additional point validator was invented.

D6 instrument changes retain their effects while moving to the new source boundaries: the five retired
legacy-staging mutants now attack real CLIENT intake; shared expiry and X09 patterns retain unique matches;
console dispatch controls carry the transport argument; CLIENT ownership admits only the three approved
Node seams; removal of the staging owner changes the feature census 122 → 121, with all 62 controls retained.
Four new CLIENT router forms change mobile ownership 40 → 44 and mobile-OLED 41 → 45, parser seven unchanged.
The BLE probe adds exact 245/256-byte replies at negotiated MTUs and executed checked-entropy failures.
The inbox probe exercises real selected-key wiping, controller routing/delivery, six exact counter values,
key/target in-use bindings and all four regeneration-debt classes.

The first new controller mutation sweep exposed a missing assertion for scheduled detail retained in the
USB assembly fallback when both BLE result headers are occupied. The added native proof makes that control
RED. The first console/inbox sweeps also exposed old-signature anchors and one malformed shell substitution;
those controls were repaired and rerun, without dropping an entry or counting a vacuous control as RED.

### 6.4 R-RA-42 document landing remains QA-owned

The literal four-row transcription is supplied as
[an unapplied patch](2026-09-18-radmin-slice8ac-r4-implementation/r42-authority-transcription.patch).
The shared authority table is untouched. The isolated gate applies exactly that patch and regenerates
208 inventory rows; all four new verbs are serial/BLE, CLIENT-only and `controller_local`. The three-artifact
check and its six controls pass on that candidate. This does not assert three-artifact agreement in the
shared checkout before QA performs its assigned table landing. The final freeze inventory distinguishes
shared inputs from this sole proposed QA-table delta.

Full §9 completion and the freeze manifest follow below; all figures in §6 describe the earlier checkpoint and are superseded by §7. This receipt is not independent QA PASS.


## 7. Revision 4 final coder freeze (2026-09-18)

**Implementation frozen for independent QA.** The final code/test/tool inputs match the complete isolated
candidate used below. The only proposed semantic documentation delta in that candidate is the four-row
R-RA-42 authority-table transcription reserved to QA by brief §8 R2; its patch is supplied and remains
unapplied in the shared checkout. The generated 208-row inventory has been copied back. Accordingly,
three-artifact agreement is proven on that candidate, and requires QA's table landing in the shared tree.
This is a coder receipt, not independent QA PASS; B292/B312 remain QA-owned closures.

The source base, admission HEAD, revision-4 brief hash and six QA preparation hashes are exactly those in
§6. The brief, ledger, register, design, tracker and MEMORY preparation inputs were not edited. No ruling,
commit or allocation request remains open. All work remains uncommitted; simulator HEAD is still
`06746a97de5764415d6fcef10b97bca90569b9c7`, clean.

### 7.1 Final measured gates

| Instrument | Final result |
| --- | --- |
| Fresh base native wrapper + actual binary | 2931 cases / 189998 assertions / 0 failed / 0 skipped |
| Fresh final native wrapper + actual binary | **2947 cases / 193734 assertions / 0 failed / 0 skipped** |
| Independent reference | **94/94** strict arrays; old 89 unchanged; five comparator controls RED; separately executed one-byte corruption rejected |
| Controller key proofs | Independent hashlib/PyNaCl KAT matches public keys, base/session keys and sealed request; all eight canonical low-order peers: **264 checks / 0 failed**, compiled false-success control RED |
| Simulator and corpus | Fresh normal/gateway compile/link for base and final; both sets **36/36** anchors, validated; direct `cmp` proves all 36 actual streams byte-identical |
| s18 | **32afbf11e43b4bf9d0bd470ad502ba0a**, 269517 events, zero assertions |
| ABI | **290 checks / 9 controls RED**; B278 **42 measurements / 6 controls RED**; no unusable controls |
| Console sink | 720 executable, 83 structural, 905 BLE guard and 6 ownership checks; **149 controls**, no unusable |
| Inbox verbs | ACCEPT **1374 / 60 controls**; CLIENT **439 / 64 controls**; explicit separately compiled CLIENT arm also passes |
| Firmware UI | 433 / 868 / 433 across the three arms; **223 controls**, no unusable |
| Custody USB / BLE line / features | **27/10**, **55/12**, **121/62** positive/control pins respectively; all controls usable |
| Deferred actions | Remote **416/518/534/464**, radio **3160/3485/3689/3695**; existing local **150/151/158/158**, 39 transcripts per arm; **40 compiled assertion controls RED**, source/placement controls preserved |
| Probe invocation coverage | Every standing probe and deferred-actions probe ran default and `--no-neg`; default runs supply the control evidence |
| Full tools discovery | **351 tests, OK, zero skips**, using measured real ELFs in the isolated `.pio-measure` tree |
| Inventory / authority | Generated **208 rows**; write/bare/check pass; candidate ruled table/header/inventory agree; **6/6 authority controls RED** |
| A0 / literals / whitespace | All pass; both repositories checked; simulator source unchanged |
| Warning census | Six environments at **173/178/177/177/182/182**, zero `-Wswitch`; no `-Wreorder` warning or suppression |
| Mutation union | **59 batteries / 918 configured: 917 RED, 1 known unusable B342, zero vacuous**; all final worker baselines 2947/193734/0 |

The canonical corpus comparator correctly refuses the different base/final `lus` identities. Its refusal,
both validated manifests and the separate byte comparison are archived; neither manifest was rewritten.
The final simulator build uses the disclosed external CMake source-list hook from §6, with both actual
controller/codec variants compiled. No simulator source file was changed.

The union is the measured S ∪ H, extending the 56-battery/880-entry floor with `radmin8client` (29),
`radmin8verbs` (8) and `radmin8rng` (1). B342 is specifically `sliceBmac` M04: it compiles but the suite stays
GREEN, so that battery returns 1 and the control is **not** counted RED. All other selected batteries return 0.
The archived union driver's final aggregate assertion reports this known nonzero result; the explicit
reconciliation accepts only that named standing exception. No failed new control is hidden by the exception.

PIN re-synced? YES — measured base 2931 cases / 189998 assertions + 16 cases / 3736 assertions = final 2947 cases / 193734 assertions; zero failed and zero skipped.

### 7.2 Final allocation and linked measurements

The pointer-free controller block remains **4512 bytes / alignment 8** on all three ABIs. Node is
**235248 native / 122176 mobile / 157344 gateway**, with the controller member absent on gateway.
No owned state beyond R-RA-45 was added. Transient identity, key and delivery adapters remain call-scoped;
no worst-case stack-depth or metal reliability claim is made by the resident-state measurements.

| Deterministic board pair | Base RAM | Final RAM | Delta | Base flash | Final flash | Delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Gateway | 204036 | **204036** | **0** | 574752 | **574896** | **+144** |
| Heltec mobile | 207756 | **211772** | **+4016** | 1373604 | **1392832** | **+19228** |

Gateway then mobile ran sequentially with fixed identity/stamp in isolated measurement paths. Final
`xiao_mobile` is **176604 RAM / 696940 flash**. Section/symbol attribution still gives mobile Node +4264,
removed legacy `ri` −245 and linker padding −3 = **+4016**. The two existing 512-byte dispatch buffers have
changed symbol names only; their count and sizes are preserved. Gateway resident-object net change is zero.
Final board inputs match all 141 production source/header files and `platformio.ini` in the shared tree.
The earlier §6 flash values are superseded; pristine base/final ELF hashes and raw measurements are retained.

### 7.3 Self-review corrections and failed attempts

Two recovery defects in the in-progress implementation were fixed before this final freeze, without
changing the allocation. Explicit `remote-result show` now adopts the caller's transport, and the assembly
fallback preserves completed local acceptance while marking reconnect re-offer separately. Three compiled
controls (C25–C27) each go RED. A full eight-row ACK debt table now permits safe/force session controls;
only authenticated execution reserves transcript ACK debt. Session controls reject execution-terminal
responses. C28/C29 go RED, with 26/24 failing assertions respectively. These are corrected implementation
findings for QA's register landing, not new requested policy or silently added resident state.

The real two-Node proof now uses selected credential `key4` against target slot 3, while the ordinary
messaging identity is different. It exercises real target admission/codec/MAC and real controller MAC intake
for every response domain; labelled synthetic pressure/tombstone fixtures remain explicit. The final five
new recovery controls run in the full union, not just filtered tests.

Five inventory-test expectations still described 204 rows, 48 mobile-OLED primary verbs and two CLIENT
families. They failed against the four new forms. The tests now require 208 rows, 52 primary verbs and the
exact six-family set, preserving serial-only key stores and serial/BLE controller verbs. All **78** focused
inventory tests and then all **351** discovered tool tests pass. No test or expected authority obligation was
removed. One diagnostic was accidentally run against the shared tree before QA's table landing and correctly
reported unclassified controller rows; that output is historical, not passing evidence.

The first full-tools attempt was interrupted after those stale expectations were identified; its exit −15
and log are retained. The final discovery rerun is complete and clean. A missing-real-ELF skip in that earlier
attempt was eliminated by supplying the actual measured final board ELFs, not by altering the skip/test logic.
The earlier source-reader repairs, scheduled-detail coverage gap, first pair's preservation rejection and
incorrect first reference nonce are preserved in §6/development logs. The final independent reference was
reproduced in a private PyNaCl 1.5.0 environment; system Python lacked that dependency, so its import failure
was not counted as a run.

B350 remains a known wording limitation: firmware-UI `--no-neg` prints bare PASS with zero controls; only
its controlled default run is used as gate evidence. B315/B359/B364 remain existing separate instrument or
fixture limits. No new claim is made about physical BLE, flash wear, reset delivery or radio reliability.

### 7.4 Frozen handoff and QA boundary

[Freeze metadata](2026-09-18-radmin-slice8ac-r4-implementation/freeze.json) pins the final receipt and
[complete input inventory](2026-09-18-radmin-slice8ac-r4-implementation/freeze-inputs.json).
[Code input hashes](2026-09-18-radmin-slice8ac-r4-implementation/freeze-code-inputs.json) and
[parity proof](2026-09-18-radmin-slice8ac-r4-implementation/freeze-code-parity.json) distinguish the actual
uncommitted source/test/tools from HEAD alone. The archive README links commands, logs, mutation selectors,
reference reproductions, board manifests/sections/symbols and both corpus manifests. All 2327 preparation
paths survive; the six QA documents remain hash-identical.

Before its independent gate, QA lands the supplied R-RA-42 table patch (the sole candidate semantic-doc delta).
The board carrier remains explicitly unavailable until 8b; the real command path refuses without transmitting.
No on-air controller or physical-device PASS is claimed. QA retains register/design/bench landing, Part 57d
and B292/B312 closure. Coder production/test/tool inputs are frozen from here; nothing was staged or committed.
