<!-- Production coder / source-validation: Codex; QA/Author is a separate session. -->
# Slice 7b-2 — coder source-validation, revision 1

**2026-09-08 — SOURCE-VALIDATION COMPLETE; IMPLEMENTATION NOT STARTED.**
The draft's implementation HOLD remains: B378 and B379 require owner rulings, followed by the separately
authored/gated codec preparation 7b-2-0. This is not a software gate or a dispatch authorization.
Two additional documentation fold-ins are proposed below for QA/Author. No production, test, tool, simulator,
brief, register, design, ruling or MEMORY edit was made by the coder; this evidence file is the sole addition.

## 1. Inputs and scope

- MeshRoute HEAD: `1d4b3ad5a74c2f24121d8fbe28b8d6e4b04f8dfe`.
- Simulator: `/home/staszek/lora-universal-simulator`, HEAD
  `06746a97de5764415d6fcef10b97bca90569b9c7`, clean.
- Consumed brief: `docs/superpowers/plans/2026-09-08-radmin-slice7b2-session-open-status.md`, revision 1,
  SHA-256 `a15d984a20a1b62db1f0a5fc7828edfeeb5d4746d25d01b7210d9120a06eea12`.
- Pre-check: `docs/superpowers/plans/2026-09-08-radmin-slice7b2-precheck.md`, SHA-256
  `6fd7b4edf80b35253e8cc401dbfab4d73b4c7252e0c9e007a284eacced0826d9`.
- Initial tracked modifications: `MEMORY.md`, the maintained register, and design §19.1's 7b-2 row.
  Initial untracked inputs: the brief, pre-check and its evidence directory. All were preserved.
- Fresh diagnostic artifacts: `/tmp/mr-codex-s7b2-sourcecheck-As9Xbj`. These are temporary build/measurement
  artifacts, not a durable replacement for the checked-in reproducer sources or a complete QA snapshot.

Per V1/V2 and the active roles block, source facts were checked against the current files, not inferred from
prior QA PASS. All eight Markdown quote blocks in the draft were found verbatim in its named ruling/design/
7b-1 authority files. Product choices remain the owner's; QA owns correction and registration of the findings.

## 2. Reproduced observations

| Instrument | This coder's fresh result | Limit |
| --- | --- | --- |
| `pio test -e native`, then the actual binary | **2883 cases / 127709 assertions / 0 failed / 0 skipped** | Current base, no implementation additions |
| B379 supplied real-code reproducer | **116 checks PASS** | Hypothetical negative producers; not an existing on-air exploit |
| B378 candidate compile/read, native / gateway ARM / heltec Xtensa | State **3848/8 → 8824/8**, **+4976 B** on each | Candidate types, not a board link/RAM measurement |
| Candidate adverse layout control, all three ABIs | Adding one byte to each capture moves total to **8832 B** | Discriminates the proposed aggregate layout |
| Existing Node zero-destination case N11 | **1 case / 11 assertions / 0 failed**, 2882 unrelated cases skipped | Separate focused proof, not another full-suite result |
| Brief quote comparison | **8/8 exact blocks found** | Authority transcription, not implementation correctness |

The candidate has a 1658-byte, alignment-2 capture and a 4978-byte, alignment-2 aggregate with the two new
counters. Existing tail padding explains why appending that aggregate grows the complete state by 4976,
not 4978. The ABI helper measures type sizes unconditionally and emits profile inclusion separately
(`tools/probe_board_abi.py:455`); measuring the candidate on Xtensa does not instantiate ACCEPT state in the
mobile Node. Its current measured Node remains 117912 bytes. Future mobile board RAM invariance still needs
the prescribed linked gate.

The 116-check proof was linked against the freshly built base archives. It really fills the shared seen
pool with acknowledged slot-0 records, obtains a slot-1 full verdict, rotates only slot 0, and admits the
same slot-1 request under its unchanged key. The hypothetical full terminal and actual completed terminal
reuse a nonce with different plaintext. The separate busy-count example demonstrates the 2-to-1 collision;
switching to the existing protocol-error domain changes the nonce. The proof does not implement or validate
the proposed new admission domain. That requires its own codec/KAT slice.

No full 7b-2 gate was run: simulator rebuild/corpus, board links, standing probes, tools sweep, census,
checkers and mutation union were not rerun by this coder for this source-validation turn. The pre-check's
36/36 corpus result remains QA's observation, not a fresh coder claim. No board RAM, airtime or metal result
is claimed here. Production and simulator sources remain at their stated bases.

## 3. Source findings for QA/Author

The register's next free number was **B380** at review. The following are proposed rows, not entries silently
added to a QA-owned register. QA should register them and fold the resolved wording into the brief before
dispatch. Neither requires a new product ruling if the existing behavior is preserved.

### Proposed B380 — permitted preparation inputs omit the existing design edit

**Anchors:** brief lines 11–14; modified design §19.1 at
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md:2123`.

The named preparation set lists the brief, pre-check, its evidence directory, register and MEMORY, but not
the modified design file. Its actual diff is only the 7b-2 status row recording draft/readiness and B378/B379
HOLD; it changes no policy text or build input. The input is understood and preserved, but the contract's
named list is incomplete. This is a documentation provenance fold-in, not evidence of concurrent code edits.

**Requested fold-in:** name that exact design status-row edit among the permitted preparation inputs. Retain
the implementation HOLD and require a fresh actual base after codec preparation; do not discard the edit,
claim a clean tree now, or treat the document as authorization to implement either proposal.

### Proposed B381 — distinguish zero-source admission from an unroutable reply

**Anchors:** brief §4.2 lines 269–273 and §8.1 `Secrets/planes` at line 400;
`lib/core/node_mac_rx.cpp:2043` / `:2051`; `test/test_node_remote_session.cpp:967`.

The draft says source-present zero is accepted alongside real return-path obligations, without specifying
which boundary accepts it. The codec/classifier correctly distinguish source presence from numeric value.
The shared Node return sender nevertheless explicitly refuses destination hash zero with
`radmin_reply_no_dst` before `send_by_hash` or the cross-layer originator. The existing N11 real-flight test
executes that distinction: authenticated request and constructed bootstrap reply, no transport submission,
no aired response, bootstrap reservation released. It passed 11 assertions in this review.

**Requested fold-in:** state explicitly that present zero is accepted at codec/admission, not promised a
successful return flight. Preserve the sender's loud refusal and no origin/node-ID fallback. Use routable
nonzero hashes for successful same-layer/depth-2/3/4 flights; give zero its own admission-plus-send-refusal
proof, including the new response-enqueue accounting and the pending-open versus one-attempt fixed/bootstrap
lifecycle. Clarify that this accounting follows the checked refusal from the shared return sender even when
its zero-destination guard prevents a lower-level enqueue call; it is not an inbound missing-source failure.
Making zero hash routable would instead require an explicit transport-scope expansion, not this wording fix.

## 4. Other source-validated boundaries

- `remote_session.h:61–72`, `:185`, `:237` and `remote_session.cpp:858`: sixteen shared seen rows, two hard
  ingress partitions, three open plus one bootstrap staging row; open staging currently retains metadata
  and discards the decoded command. There is no independent open output capture or cooldown policy today.
- `remote_session.cpp:943`, `:972`, `:998`, `:1028`: ACK release consumes no ingress; safe/force staging has
  no consumer; execute retry retains the original route; full has no negative wire reply producer.
- `remote_codec.h:67`, `:164–166` and `remote_codec.cpp:277`: response opcode 5 is reserved, open response
  overhead is 10, bootstrap 33, rollover result 34; the present terminal nonce has no result/detail input.
- `remote_session.h:349` has a 33-byte fixed reply buffer. A future 34-byte rollover response cannot be
  placed there without the explicitly bounded adjustment/transient described in the draft.
- `remote_session.cpp:264`, `:331`, `:372`, `:383`: slot install owns invalidation; one deadline scan owns
  ingress/staging expiry. Open capture/cooldown must join those ownership paths, not get a parallel timer.
- `node.cpp:100`, `:113`: the existing epoch draw rejects zero but cannot report void-HAL RNG health;
  runtime commit uses the session installer. Equal-current rejection is new required control behavior,
  not an existing guarantee of the draw helper. B312 remains open.
- `firmware_remote_executor.h:49`, `firmware_commands.cpp:911` / `:1629`, and
  `firmware_command_authority.h:277`: one authenticated executor/real seam exists; open authority requires
  exact argument-free policy names. Target status needs the five new fields; native does not compile that
  real firmware handler, so its proof belongs to the inbox-verbs probe.

## 5. Reproduction and next handoff

Executed from `/home/staszek/MeshRoute`:

```sh
pio test -e native
./.pio/build/native/program
python3 docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/measure_candidate.py --root /home/staszek/MeshRoute --out /tmp/mr-codex-s7b2-sourcecheck-As9Xbj/candidate
g++ -std=c++20 -DMESHROUTE_NATIVE -Ilib/core -Ilib/monocypher docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/nonce-proof.cpp -Wl,--start-group .pio/build/native/lib095/libhal.a .pio/build/native/lib1d5/libmonocypher.a .pio/build/native/lib6be/libcore.a .pio/build/native/libf08/libconsole.a -Wl,--end-group -o /tmp/mr-codex-s7b2-sourcecheck-As9Xbj/nonce-proof
/tmp/mr-codex-s7b2-sourcecheck-As9Xbj/nonce-proof
./.pio/build/native/program --test-case='*radmin-5/N11*'
```

Native build/program logs are retained under the temporary root; the candidate subdirectory contains the
generated TU, each environment's actual compiler flags, object/compile log and measured JSON. The proof source
SHA-256 is `5530c51610b753ed24071f9cef507e2bd75f85beb8e6032fe0a87b805f057dca`; measurement script SHA-256 is
`e80bbe7750a04bc71104276538779a5a9ba1e28a65165fc872635fec86e55f2c`. All executed diagnostic tests passed.

Next: QA folds in/registers the two clarifications; the owner decides B378's allocation/rate and B379's exact
admission domain. If accepted, QA supplies the separate 7b-2-0 codec brief, then that slice is implemented and
independently gated/committed before the behavior brief is reissued at the actual successor base. No
implementation or pin replacement is authorized by this report, and nothing was committed.

## 6. Owner decision receipt — 2026-09-09

**The owner has now approved both proposals.** The waiting-for-rulings statements in the original
2026-09-08 preflight above are historical; they are preserved, not silently rewritten. B380/B381 have also
been folded into brief revision 2 and closed by QA as documentation corrections. This appended receipt is
coder-owned handoff evidence, not a numbered entry in QA/Author's rulings ledger or an implementation gate.

Owner's words, verbatim:

> b378 - agree with storage and agree with 5 minutes limit. To be clear - this is global limit - independent of who is requesting. b379 agree with proposal

Approval context: brief revision 2, SHA-256
`1802c25a99e0d470b299a769a072b56e1bd6f12d8df7e230962255de82fa387a`, and the preceding explanation of its
three-total-open-requests-per-five-minutes proposal.

- **B378 storage:** the proposed three independent 1648-byte ACCEPT captures and two counters are approved;
  candidate state growth is +4976 bytes, with final native/gateway ABI and linked board RAM still measured
  and attributed. Mobile Node/RAM must remain unchanged.
- **B378 rate:** the five-minute budget is **global to each target and shared across all requesters**.
  The approved proposal's ceiling is **three open admissions total per 300000 ms**, not three per requester.
  Changing requester identity does not create a fresh budget. Existing peer occupancy/cooldown constraints
  do not confer additional quota; authenticated work and bootstrap remain outside the open budget.
- **B379:** the exact revision-2 admission-notice proposal is approved: authenticated-clear 28-byte
  `ADMISSION_RESULT`, REMOTE_RESP opcode `0x5`, session-key authentication and request-ctl/code/detail nonce
  binding, with existing response encodings preserved and a separately attributed codec preparation.

QA/Author's next action is to record the owner quotation and settled proposals in the rulings ledger,
update dispatch status, and supply the separate **7b-2-0 codec brief**. B378/B379 remain implementation/gate
obligations, not closed findings merely because the owner ruled. The coder does not author that brief or
start the behavior slice from this receipt. No production, test, tool or simulator change, test rerun or
commit accompanies this addendum.

## 7. Revision-4 coder source-validation — 2026-09-09

**PASS — source-validation only; no fold-ins or new STOP findings. Implementation not started in this turn.**
This checkpoint supersedes the historical pending-ruling/codec statements above, without rewriting them or
the owner's §6 receipt. The committed codec prerequisite and revision-4 contract agree with the source.
The source-validation prerequisite is satisfied; this is not a behavior implementation PASS, an independent
QA gate, or closure of B378/B379. The next work is the fenced behavior implementation and its full gates.

### 7.1 Actual inputs and preservation

- MeshRoute: `564f460a3b755a104f146da70457e5c8c68e99b9`, parent
  `1d4b3ad5a74c2f24121d8fbe28b8d6e4b04f8dfe`. The production diff from this HEAD is empty. Its subject does
  not change the source fact: the commit contains the codec prerequisite, not the missing open/control producers.
- Simulator: `/home/staszek/lora-universal-simulator`,
  `06746a97de5764415d6fcef10b97bca90569b9c7`, clean; no simulator source/build change in this turn.
- Consumed behavior brief revision 4, SHA-256
  `0920bb419cf5f33fbedb8fabfa2c9dc9b91c81744727fd69cb21b16253a7200e`.
- Consumed pre-check, SHA-256
  `4cde5248f665126c173b41668179826c48b1d58dfac8170c5b4a3463018f3f84`.
- This historical coder receipt before the append, SHA-256
  `ee0f42aa690f0a84b7f2b8580402c85e9a0f34054afa67a0352ef573ca544ab6`.
- Initial uncommitted inputs are exactly the brief's named QA preparation: MEMORY, register, codec QA report,
  behavior pre-check/brief, codec brief, design, and the untracked
  `docs/superpowers/evidence/2026-09-09-radmin-slice7b2-reissue/` directory. None was edited by the coder.
- Scratch receipts: `/tmp/mr-codex-s7b2-r4-preflight-RrTvIR`. `input-paths.z` inventories all **1087** tracked
  and nonignored untracked paths; `inputs-before.sha256` hashes **1084** file contents, SHA-256
  `27e91340ff39b826d56c89959b84a76be681fbe353116bd051fc30931473bee9`. The other three are directory symlinks,
  not omitted production files or gitlinks: `spec/docs`, `spec/scenarios`, `spec/test`, pointing respectively
  to `../../lora-universal-simulator/{docs,scenarios,test}`. Their unchanged mode-120000 index blobs are
  `079932968582d0f60f520bd17f42719357c18002`, `b5cbba0d5e66f210ed1cf19aca75b5aa1aca578b`, and
  `393a9b98303a1d6c9eed13a8cd0286f894ec8774`; `symlink-index.txt` records them. Ignored build products are
  not represented as source inputs. `status-before.txt` and `preparation-before.diff` retain the dirty input set.

All 1084 content hashes matched before this append; production/test/tool/config/scenario/symlink diffs
remain empty. This append is the sole coder repository edit. No reset, clean, commit, inventory regeneration,
maintained-document landing, production repair or mutation was performed.

### 7.2 Source-validated integration boundaries (V1/V2, U1/U2)

| Current source | Verified reading / implementation boundary |
| --- | --- |
| `lib/core/remote_codec.h`, `remote_codec.cpp:127/243/318/372/628/655` | The committed admission opcode/domain, typed fields and 28-byte fixed envelope exist. The new nonce suffix and complete clear AAD are shared codec paths; typed publication follows authentication and semantic validation. Consume them without changing production codec or frozen vectors. |
| `remote_session.h:185/237`, `remote_session.cpp:858` | Three open plus one bootstrap metadata rows exist; decoded open bytes are not retained and there is no open executor/output/cooldown. The approved captures and target-wide budget are new behavior, not an interpretation of today's TTL. |
| `remote_session.cpp:943/972/998/1028` | ACK is synchronous and ingress-free; safe/force reserve CONTROL without a seen row and have no consumer. Exact execute retries protect first source/route. Already-acknowledged/full/ingress verdicts lack the new fixed reply producers. |
| `remote_session.cpp:240/264/331` | Slot install releases owned transcripts and seen/ingress, compacts later shared scheduling ranks, and rebuilds only free chunk links. B386's exception is necessary and sufficient; survivor output/identity/cursors and relative ordering remain protected. |
| `node.cpp:100/113` | One epoch draw refuses zero but cannot attest void-HAL RNG health. Equality with the current epoch must additionally refuse in the new control preparation. Commit uses the existing install path and expiry re-arm; B312 stays open. |
| `remote_session.h:349`, `remote_codec.h` overhead constants | Existing RX scratch is bootstrap-sized (33), successful rollover is 34, admission is 28 and the one-byte authenticated protocol error totals 27. Transient fixed-reply storage must be derived, not overrun or turned into a resident cache. |
| `node_mac_rx.cpp:2009/2043/2108/2119/2174` | One captured-route conversion and shared return sender exist. Destination zero refuses before either lower send path. Bootstrap releases after one checked outcome; transcript sends are paced, immutable on failure and distinguish queued/parked ownership from raw counters. Generalized accounting must avoid double counting. |
| `remote_session.cpp:372/383`, `node.cpp:126`, `node_mac_rx.cpp:2153` | One bounded earliest-deadline scan and Node timer owner exist. Open pending/cooldown must join them. Exact-edge availability and original admission deadlines are behavior obligations, not new timers or completion-relative windows. |
| `src/firmware_remote_executor.h:49`, `test/test_firmware_remote_executor.cpp` | Current send/dispatch precedence permits an authenticated capture dispatch while TX is full; the executed fixture pins this. Control-first pacing must not introduce a blanket full-TX return. Extend the same pure interfaces and typed outcome mapping. |
| `src/firmware_commands.cpp:252/266/409/911/1629`, `src/fw_main.cpp:1759` | Existing per-call Node/Print/seam adapters and one ACCEPT service call provide the binding. The seam owns validation, policy and context restoration; `status` has no five-counter suffix yet. No new main-loop call or resident adapter is needed by the planned implementation. |
| `src/firmware_command_authority.h:277`, `src/firmware_command_context.h:10` | Remote-open requires exact primary-verb equality to an open row (`status` or `routes`). REMOTE/open/no-physical/no-ACL-slot context is already expressible; no parallel core whitelist, class or context field is needed. |
| `tools/probe_inbox_verbs/remote_exec_rows.h`, its runner, `tools/probe_console_sink/structural.py` | Real firmware handler/Print/status claims belong to the real-TU inbox probe. Native's existing Node flight fixture uses a counting executor; structural ownership allowlists and probe pins require explicit re-derivation as interfaces grow. |

All **8/8 Markdown blockquotes** in revision 4 were found byte-for-byte, with quote markers removed, in the
named rulings, design and 7b-1 authority files. R-RA-35's global three-admissions-per-five-minutes rule and
R-RA-36's admission-versus-terminal distinction are carried unchanged. The former 33-byte RX reply and
no-open-consumer comments are implementation update targets inside the fence, not grounds to fork helpers.

### 7.3 Fresh executions, with limits

| Instrument | Coder result in this turn | Scope |
| --- | --- | --- |
| `pio test -e native`, then actual program | **2888 cases / 172264 assertions / 0 failed / 0 skipped** | Incremental current-base build followed by a fresh execution, not a new clean build or behavior additions. Wrapper still reports zero cases. |
| B386 freshly compiled real-session reproducer | **43 checks PASS** | Survivor ranks 1→0 and 2→1; all other header fields, owned chunks/seen records and pending seq1 wire bytes retained. Existing install boundary only, not a real force-control producer. |
| B379 freshly compiled real-session/codec reproducer | **134 checks PASS** | Shared-pool full→recovery lifecycle and nonce separation; notice producers remain explicitly synthetic. No claim that missing target producers were exercised. |
| B378 candidate compile/read, host / ARM / Xtensa | **3848/8 → 8824/8, +4976 B**, all three | Capture 1658/2, aggregate 4978/2; extra-byte control 8832. Actual current Nodes 225920/152288/117912. Candidate state is not linked board RAM or an instantiated mobile allocation. |
| Independent reference | **87/87 old literals; strict 89/89 arrays**, four comparator controls RED | External primitive anchors first; 25 positive and 1718 valid-tag-invalid admission tuples. Existing expected bytes unchanged; no separate standalone one-byte-corruption rerun in this preflight. |
| Existing real-Node N11 | **1 case / 11 assertions / 0 failed**, 2887 unrelated cases skipped | Current present-zero/bootstrap-send-refusal boundary only. New open/control lifetimes and counters are still unimplemented. |
| Inventory `--check` | **204 rows**, byte-identical | Read-only check, no regeneration. |
| Authority checker `--selftest` | Table/header/inventory agree; **6/6 controls RED** | Existing authority surface, no policy edit. |

All listed commands exited zero. The native program SHA-256 is
`438719e4585062f5e0b6da7329595b92ed91d3d4a24fcf553cf0cef144c1730c`. B386 source SHA-256 is
`463cb631a8aa6aa387288759ffd786f285963a0bee2dea171e77b27bfa2e200b`; B379 source is
`f9b0c29f3da9c681c7d6c8eb5e602fcd39ddf789186c396799d34eae67fba47a`; the candidate runner remains
`e80bbe7750a04bc71104276538779a5a9ba1e28a65165fc872635fec86e55f2c`.

No full behavior gate ran: no new simulator build/corpus, board link/size attribution, standing controlled
probe chain, full ABI controls, tools sweep, census or mutation union. The existing full-suite execution
includes the prior immutability cases, but no new focused 142-assertion Node-fault run is claimed. Current
BASELINE was read directly, SHA-256 `71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`;
its corpus/keystone is still a future implementation gate, not a fresh stream measurement here. B342/B350/
B359/B364 limitations stay visible. No software additions means no PIN re-sync in this preflight.

### 7.4 Prediction before implementation

Expected production paths are the five core files `remote_session.{h,cpp}`, `node.{h,cpp}` and
`node_mac_rx.cpp`, plus `src/firmware_remote_executor.h` and `src/firmware_commands.cpp`. No production codec,
context/authority header, NV, inbox/ACL policy, simulator or additional main-loop owner is predicted.
Extend the existing session/transcript/Node/executor fixtures; add the brief-authorized focused open/control
case files while keeping transport construction in the existing Node fixture. The existing custody Node-size
assertion and board ABI pins move only after measurement. Probe executor/ownership/status rows and their
controls/runners, the mutation harness, mechanically regenerated inventory and this receipt are the expected
support changes; inventory remains 204 semantic rows. Any newly discovered outside-fence dependency is a STOP,
not permission for an incidental repair.

The live `TARGET_SRC` map selects **15 existing batteries** for those predicted source edits:
`a0rx b159map b159rx b161rx b251rx radmin5rx radmin5session radmin7exec radmin7rx radmin7transcript
sliceBnode sliceBrx sliceEnode sliceGrx teamgrant`. This is separate from the **47-battery historical floor**;
all 15 overlap it. The future union retains the committed codec's **712 RED plus known unusable B342** floor,
with counts re-derived and new open/control controls added. These are selection/prediction facts, not a
mutation run. Actual source changes must be used to derive both final selectors again.

The measured candidate supports the brief's +4976 Node prediction (native 230896, gateway 157264), with
mobile Node/RAM unchanged. Linked RAM/flash, all pin arithmetic and zero-movement controls still require the
specified deterministic pair; do not substitute the candidate for them. Core changes require both simulator
variants to actually recompile (B385), with 36/36 stream identity predicted. No timer, wire version or NV
version changes; no 7b-3 scheduler/controller implementation.

### 7.5 Reproduction and handoff

Run from `/home/staszek/MeshRoute`; logs and fresh diagnostic executables are retained in the scratch root
named in §7.1. The two standalone C++ sources link the current native archives with this same command shape:

```sh
pio test -e native
./.pio/build/native/program
g++ -std=c++20 -DMESHROUTE_NATIVE -Ilib/core -Ilib/monocypher docs/superpowers/evidence/2026-09-09-radmin-slice7b2-reissue/b386-order.cpp -Wl,--start-group .pio/build/native/lib095/libhal.a .pio/build/native/lib1d5/libmonocypher.a .pio/build/native/lib6be/libcore.a .pio/build/native/libf08/libconsole.a -Wl,--end-group -o /tmp/mr-codex-s7b2-r4-preflight-RrTvIR/b386
/tmp/mr-codex-s7b2-r4-preflight-RrTvIR/b386
g++ -std=c++20 -DMESHROUTE_NATIVE -Ilib/core -Ilib/monocypher docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-qa/b379-codec.cpp -Wl,--start-group .pio/build/native/lib095/libhal.a .pio/build/native/lib1d5/libmonocypher.a .pio/build/native/lib6be/libcore.a .pio/build/native/libf08/libconsole.a -Wl,--end-group -o /tmp/mr-codex-s7b2-r4-preflight-RrTvIR/b379
/tmp/mr-codex-s7b2-r4-preflight-RrTvIR/b379
python3 docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/measure_candidate.py --root /home/staszek/MeshRoute --out /tmp/mr-codex-s7b2-r4-preflight-RrTvIR/candidate
/home/staszek/mr-slice2-ref/bin/python docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-reference.py --compare test/test_remote_codec.cpp --selftest
./.pio/build/native/program --test-case='*radmin-5/N11*'
python3 tools/gen_command_inventory.py --check
python3 tools/check_command_authority.py --selftest
```

The revision-4 source-validation prerequisite passes with **no new finding** (next free remains B387).
Production remains unchanged, B378/B379 remain open, and the full behavior gate remains pending. QA-owned
preparation is preserved alongside this append; the owner alone commits. Recheck the actual tree/hash before
the next implementation step; neither an older base nor a HEAD-only copy of dirty inputs is a valid substitute.

## 8. Implementation checkpoint — 2026-09-09, NOT a completed gate

The owner instructed Codex to start implementation after §7. HEAD remains
`564f460a3b755a104f146da70457e5c8c68e99b9`; revision-4 brief SHA-256 remains
`0920bb419cf5f33fbedb8fabfa2c9dc9b91c81744727fd69cb21b16253a7200e`.
QA preparation and all historical sections above are preserved. No commit or simulator-source edit.

Scratch: `/tmp/mr-codex-s7b2-rFHVpI`. `inputs-before.sha256`, `status-before.txt` and
`preparation-before.diff` record the starting dirty snapshot. `measure/` is an isolated shared clone for
same-path baseline/final board captures. Its baseline `boards-base.log` and
`.pio-measure/s7b2-base/pair.json` completed successfully. The fresh `sim-base/` build points to that
baseline clone; `corpus-base/` produced and independently on-disk-validated all 36 streams/anchors.

Current implementation adds the approved three independent captures/two counters, open cooldown and
clear response lifecycle, main-loop control prepare/commit/send, fixed admission/protocol replies,
scalar status snapshot and corresponding tests. Native compiler refusal at the old Node assertion in
`native-first-build.log` revealed **230896** rather than old **225920**. Independent `abi.measure` runs
using each environment's own flags (`abi-reveal.log`) measured native **230896/8**, heltec_mobile
**117912/8**, gateway **157264/8**; only the two authorized moved pins were updated. These are ABI
measurements, **not final linked RAM figures**.

Executed incremental results, not substitutes for the final frozen chain:

- Original binary: **2888 / 172264 / 0** (`base-native.log`).
- Latest focused remote run: **276 cases / 61721 assertions / 0 failed**, **2624 unrelated cases skipped**
  (`native-radmin-fourth.log`). The full final count and pin arithmetic are not yet claimed.
- Fresh candidate simulator build points at the implementation checkout. `corpus-candidate/` reproduces
  **36/36 streams and all anchors**, with a successful independent validate pass. Final frozen-source
  provenance/comparison remains to be collected.
- Structural instrument: **83 / 0** (`structural-first.log`), including the new scalar-only status row.
- Inbox positive run: ACCEPT **1363 / 0**, CLIENT historical **378 / 0** (`inbox-fourth.log`).
  The subsequently added CLIENT status-absence rows predict **384** and still require execution.
  The full control run is underway; no control verdict or final probe PASS is claimed here.

Failed attempts preserved:

- First native build intentionally encountered the old ABI tripwire; after the measured re-pin, the next
  run found the old `RemoteSessionState < 2064+1824` historical ceiling. Its assertion now prices exactly
  the approved +4976, rather than dropping the size proof.
- Inbox attempts 1/2 rejected new fixture code for a copied range-loop string and a signed/unsigned
  NV-write comparison (`inbox-first.log`, `inbox-second.log`). Both fixture mistakes were corrected.
- Attempt 3 executed four failing checks: three expected LF-only suffixes where the existing Arduino Print
  emits CRLF, plus a leading-space command expected `refused` instead of the existing lookup's typed
  `unknown_command`. Source verification and corrected expectations produced attempt 4's zero failures;
  real dispatch refusal/context/output checks were retained.
- A definitions-inspection command accidentally imported the mutation harness, whose module body starts
  its CLI runner. It began default-model scratch preparation at `/tmp/mr_mutation_run-1lu7mz27` and was
  interrupted with SIGINT (exit 130) before it produced any mutation verdict. The runner killed its workers
  and removed its scratch copies; working sources were not mutation targets. **This is not a gate run.**
  Subsequent selector inspection parses only assignment ASTs without importing/executing the CLI body.

Remaining: finish discriminating native/real-handler lifecycle and failure coverage, new mutation controls,
full frozen native/probe/tools/census/reference/ABI/board/corpus chain, complete two-selector union,
source restoration and final evidence/size attribution. **Slice 7b-2 remains incomplete and ungated.**

## 9. Frozen implementation and verification history — 2026-09-09

This section supersedes §8's incremental counts, not its history. Independent QA has not run this
implementation gate. HEAD and revision-4 brief hash remain exactly those in §8. The simulator remains
clean at `06746a97de5764415d6fcef10b97bca90569b9c7`. Nothing is committed.

### 9.1 Implementation and ownership

- `remote_session.{h,cpp}`: three independent open capture/staging pairs; global three-start rolling
  300-second bound shared across sources, including completed cooldown rows; capture ownership,
  truncation, clear framing, expiry and wipe; pure control view/check/prepare/release; typed, saturating
  five-counter snapshot. Fixed authenticated capacity notices consume the committed admission codec,
  not the TERMINAL domain. Acknowledged retries use the existing typed protocol-error value.
- `node.{h,cpp}` and `node_mac_rx.cpp`: authenticated real receive/retained-route replies, checked
  send ownership, main-loop control transaction and open sender, existing epoch draw/install idiom,
  before/after scalar diagnostics. A present-zero source is admitted as before and refused by the
  return sender, without an origin fallback. Fixed replies remain uncached, one attempt; open/auth
  pending output preserves its immutable bytes/cursor on checked failure.
- `firmware_remote_executor.h`: expiry, eligible CONTROL, authenticated frame/capture, then independent
  open frame/capture order. Full TX defers draws/sends but still permits eligible owned capture.
  The temporary open input is copied before the capture is reused and wiped on exit. All open
  commands reach the existing remote-context seam; no second whitelist or authority evaluator.
- `firmware_commands.cpp`: automatic ACCEPT binding and five scalar `status` fields, exactly once
  from one snapshot. No new global, timer, NV write/schema, JSON surface, authority row or mobile field.
  The committed codec, `fw_main.cpp`, simulator source and corpus anchors are unchanged.

The ruled `OpenCapture` is **1658 bytes, alignment 2**, three instances. The existing 3848-byte
`RemoteSessionState` grows to **8824, alignment 8**: captures begin at offset **3846**, using the old
two tail bytes, and the new counters are at **8820/8822**. Existing counters stay **3840/3842/3844**.
No additional resident state is hidden outside that block. Native/gateway Node grow by **4976**;
mobile remains the unchanged control. `RemoteRxResult`'s automatic reply scratch is sized from the
named 34-byte rollover overhead, with assertions covering every fixed reply shape.

### 9.2 Executed native arithmetic and proof boundaries

The frozen actual binary runs **2909 cases / 174485 assertions / 0 failed / 0 skipped** after
`pio test -e native`; the wrapper's zero-case summary is not used. Baseline actual binary is
**2888 / 172264 / 0**. Filtered XML is arithmetic evidence only: `base-remote.xml` and
`final2-remote.xml`, audited by `final_audit.py arithmetic`, list every new case and changed old count.
The 21 additions comprise 2 pure executor, 12 real Node, and 7 pure session cases. Their **2216**
assertions plus **5** added to existing cases explain the entire pin move; no old case disappears.
The five are the existing full-TX executor row (+1) and real-Node N11 zero-source accounting (+4).

**PIN re-synced? YES — 2888 + 21 = 2909 cases; 172264 + 2216 new-case assertions + 5 existing-case assertions = 174485 assertions.**

Native proves real codec/session/RTS/DATA/post-ACK/Node sender behavior with a counting fake executor.
It does **not** claim to execute real firmware handlers. New Node cases exercise safe/force/busy,
zero/equal draws, old/fresh sessions, committed rollover after zero-source refusal/bootstrap recovery,
full-seen notice → other-slot rotation → same-key execution, acknowledged retries, cross-slot rank
compaction with a nonzero surviving cursor, full-TX expiry without draws, parked clear terminal,
same-layer and depth-2/3/4 clear returns, and synthetic seal/send faults with retained cursor/recovery.
Paused authenticated/open execution is explicitly synthetic; no natural radio interleaving is claimed.
Pure cases cover exact 1648/+1 bytes, binary multi-frame output and every carrier cap, rolling global
admission/cooldown boundaries, short-output/stale-view preparation, scalar-only changes and saturation.

The independently compiled inbox probe's ACCEPT arm executes the real seam, `status` and `routes`
through clear radio replies and compares their bytes to local output at the same snapshot. It proves
no NV writes, no USB/BLE leak, restored command context, exact argument-free admission, malformed and
privileged refusals, all five distinct/zero/saturated status fields, and an actual firmware-bound
control flight with one eight-byte draw. Its CLIENT arm proves all five fields absent. Existing
B374/B375, ACL actor, private-DM diagnostic view and route-discrimination tests remain in the gate.

### 9.3 Frozen sources, artifact provenance and completed measurements

All artifact paths in this section are beneath `/tmp/mr-codex-s7b2-rFHVpI/` unless absolute.
`freeze2-inputs.z` includes the complete tracked/untracked dirty input set, not merely HEAD.
`gate2/` and `mutations2/` are independent frozen overlays; mutations run only in their own harness
worker copies. `gate2-*-sources-before.json` binds both launches; completion requires matching
after-manifests. `handoff-sources-final.json` hashes the complete shared checkout, with only this
appended coder receipt permitted to differ from the tested copy. All **623** other documentation
inputs (excluding generated inventory) match the implementation-start hashes; QA preparation is preserved.

Deterministic boards are built gateway then heltec_mobile, `--jobs=1`, at the same private build paths
in `measure/`, fixed Jan 1 2000 build identity and `b206b206b206` git stamp. Captures are
`measure/.pio-measure/s7b2-base/` and `s7b2-final2/`; logs are `boards-base.log` and `boards-final2.log`.

| Board | RAM base → final | Flash base → final | Objects base → final | Node size |
| --- | --- | --- | --- | --- |
| gateway, ARM | 198980 → 203956 (**+4976**) | 562748 → 570588 (**+7840**) | 285 → 285 | 152288 → 157264 |
| heltec_mobile, Xtensa | 207756 → 207756 (**0**) | 1372992 → 1372992 (**0**) | 329 → 329 | 117912 → 117912 |

Native Node is **230896**, alignment 8; both boards also alignment 8, measured before pin edits.
Gateway `.bss` increases exactly 4976, entirely `g_node`; heap reserve decreases by the same amount.
`.text` increases 7840 = **7644** bytes of unioned sized-symbol extents + **196** uncovered bytes
(padding/unsized extents). Raw symbol sizes sum to 7646 because the new OpenSink D1/D2 aliases overlap
by two bytes; no double counting. `.data`/`.ARM.exidx` retain their sizes with relocated contents.
Largest code changes: receive +1778, service clone +782, control prepare +688, control check +620,
next-open +438, Node control service +396. Complete changed symbols/sections and hashes are in
`symbol-attribution-final2.{json,log}` and `elf-attribution-final2.{json,log}`; ELFs were not modified.

Every allocated mobile section remains byte-identical. ELF differences are debug metadata only;
the `.bin` changes solely at the embedded ELF hash (offsets 176–207), image XOR checksum and trailing
SHA-256. The attribution script independently verifies both checksums and that exact changed-offset set.
ELF SHA-256 base → final:

- gateway: `6898849c74046ccd4a400fe1c03e350fcf500cefa6f7798d86273946bf1dec7e`
  → `f41a0d7f9ade6c7f9421251cedd0cdb0ae56679449d583f30223d888fd46046d`.
- heltec_mobile: `b8e0965097116c1c3f6b0c497ac7201f0fdbd879eb0fb9102a73544a248121d9`
  → `98c18923589b7db77882efa882d6ed60336499b377c5c4728d8b9a04898e3b4f`.

Fresh `sim-base/` and `sim-final2/` compile normal and gateway archives, including actual recompilation
of the three affected core TUs and the codec in both namespaces. `sim-*-build.log`, compile commands
and `corpus-provenance-final2.log` bind source/object hashes and paths. Baseline source hashes come
from exact `git show 564f460:path`, since the same-path measurement tree was overlaid only after its
baseline builds completed. LUS SHA-256 changes
`3826d36ee5b07fdaf2eb4836bfcf81240cb4ace18151fce4e66fe693670e186c` →
`862241173963b3c30a501849dea9e5e6184227575225aa8e263f0f5c48d1170a`.

`corpus-base/` and `corpus-final2/` both produce and independently validate **36/36** anchors.
Canonical comparison exits 1 solely for `lus_sha256` (`corpus-compare-final2.log`); manifests are
not rewritten. Independent comparison of every actual NDJSON byte passes **36/36**. Current
BASELINE SHA-256 is `71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`;
s18 is **269517 events**, full MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**, with no anchor change.

Independent `/home/staszek/mr-slice2-ref/bin/python` reference passes its RFC/XChaCha anchors,
**87/87** original literals and **89/89** strict literals including both admission arrays, plus
**4/4** comparator controls. `reference-final2.log` records dependency/version provenance.
A disposable copy changes the first expected admission byte `0x53` → `0x52`; strict comparison
exits **1**, preserved in `reference-corrupt-control-final2.log`. Production codec/test vectors are unchanged.

### 9.4 Additional attempts, still not concealed as successful gates

- The first CLIENT status fixture build rejected an unqualified `dispatch`. Qualifying the existing
  `mrfw::dispatch` call fixed the fixture; the subsequent CLIENT run passes 384 checks and 45 controls.
- First frozen `gate/`/`mutations/` runs were explicitly interrupted before completion for one U1
  correction: the acknowledged-retry byte now uses `RemoteProtocolError::already_acknowledged`
  instead of literal zero; its C13 mutation anchor follows it. Earlier logs are preserved and are
  **not** the final gate. `gate2/`/`mutations2/`, fresh simulator and same-path final2 board captures
  restart against the final source. The final2 simulator and board binaries reproduce the first
  final binaries, confirming the enum substitution is value-preserving.
- A scratch report parser initially counted the mutation harness's aggregate baseline line as a
  third worker baseline and asserted. Its corrected parser selects worker-prefixed lines; neither
  production nor any instrument/verdict was changed. Initial report-parser output is preserved.

Full chain and 49-battery union are still running; no complete coder gate or independent QA PASS
is claimed by this checkpoint. The next subsection will record their actual completed results.

### 9.5 Tools-sweep refusal and scoped correction — proposed B387 for QA's register landing

The first full tools discovery **failed**, not skipped or passed: **322 tests, one import error**
(`gate2-chain-tools.log`). `tools/test_probe_console_sink.py:112` accepts only whole-line numeric pin
assignments, then refuses a missing required pin at line 121. My added inline explanations on
`PIN_STRUCTURAL=83` and `PIN_CONTROLS=149` made those two declarations unreadable to the strict wrapper.
The standalone shell probe still passed, so only running the discovery sweep exposed the mistake.
The failed import replaced the module's 22 tests with one `_FailedTest`; 322 is **not** the full-suite pin.

Correction: move both explanations to preceding comment lines in `tools/probe_console_sink/run.sh`.
The pin values, shell commands, strict wrapper and all assertions remain unchanged. This is a repeat of
the plain-pin discipline behind B377, not permission to relax the reader. **Proposed B387:** coder-introduced
console-runner pin-format defect, corrected in-slice; QA owns recording/closing the row after its verification.
No new owner decision is needed. Suggested durable reminder for QA's documentation landing: parser-read
numeric assignments stay bare; put their derivations in preceding comments, never inline.

The failed chain stopped before inventory/checkers/census. It is preserved, including its result JSON.
`gate3/` is a fresh complete dirty overlay from `freeze3-inputs.z`; a filtered, explicitly named continuation
repeats **console-sink controlled and no-controls**, then runs the **entire tools discovery**, inventory
write/bare/check, authority/default+controls, A0, literal check and six-env warning census. It does not
silently substitute a focused test for the full sweep. `chain3-driver.log` records that exact selection.

`audit-tool-correction.log` proves that gate2 → gate3 differs only in this appended receipt and those two
comment relocations. Every production file, native fixture, codec literal, mutation harness/target, other
probe and build configuration is byte-identical. The continuing `mutations2/` run never reads the shell
runner and its complete mutation inputs are unchanged; no edits occur in its snapshot. Earlier native,
ABI, other five probes, simulator and board results therefore retain their exact compiled/tested inputs.
The final composed chain must contain every standing command, with the failed sweep visible and the full
corrected sweep separately successful. Final shared-checkout hash equality is against **gate3**, except
this coder receipt; source-restoration manifests remain per actual snapshot, not relabelled as a new run.

### 9.6 Standing results collected after the correction, before union completion

The corrected full tools sweep passes **343 tests / 0 failures / 0 skips** in 636.270 s. No
representative-ELF test was skipped: both private measurement artifacts were present. The original
322/error result above remains visible. Logs are `gate2-chain-*.log` for unchanged compiled inputs,
`gate3-chain-*.log` for the repeated console probe/full tools sweep and continued checkers/census;
`gate2-chain-results.json` and `gate3-chain-results.json` retain command, exit, duration and log SHA-256.

| Instrument | Executed result |
| --- | --- |
| Native wrapper, then actual binary | **2909 / 174485 / 0**, zero skipped; arithmetic in §9.2 |
| Board ABI | **191 checks**, **9/9** controls RED; native/heltec_mobile/gateway Node **230896/117912/157264** |
| B278 production-row ABI mirror | **42 measurements**, **6/6** controls RED |
| Console-sink | **720** sink/help checks, **83** structural, **905** BLE-guard checks; six ownership profiles + three controls; **149** negative controls, zero unusable |
| Inbox-verbs | ACCEPT **1363 + 60 controls**; CLIENT **384 + 45 controls**; both arms PASS |
| Firmware UI | **433** l2 / **868** child-enabled / **433** BLE-row checks, **223** verified controls; zero unusable |
| Custody USB | **27 checks / 10 controls**, zero unusable |
| BLE line | **40 checks / 8 controls**, zero unusable |
| Features | **9 cells / 120 checks / 59 controls**; includes all **40** ownership controls, nine-file census unchanged |
| Tools discovery | **343 OK**, no skips; failed first attempt preserved in §9.5 |
| Inventory | `--write`, bare, `--check` pass, **204 rows**; only source anchors move, every other byte unchanged |
| Authority / A0 / literals | All pass; authority **6/6** selftests RED |
| Warning census | All **6** environments at their warning pins, **0 switch warnings**; source hashes unchanged at chain completion |
| Simulator / corpus | Both variants freshly rebuilt; **36/36** validated anchors and actual streams byte-identical to fresh base |
| Codec independent reference | **87/87** old vectors, **89/89** strict literals, **4/4** comparator controls; separate one-byte corruption exits 1 |
| Deterministic board pair | gateway RAM **+4976**, flash **+7840**; mobile RAM/flash/loadable bytes **unchanged**; full attribution §9.3 |

All six probes additionally ran `--no-neg` successfully. These are **probe-only**, not substitutes for
their controlled runs. B350's pre-existing firmware-UI no-controls wording still prints `PASS`; it is
explicitly not counted as a gate here. The corrected console no-controls run is visibly PROBE-ONLY.
The tools-sweep ResourceWarning about a pre-existing unclosed `/dev/null` handle is preserved in its log;
it is not a new firmware warning or a suppressed test failure.

The census environment order is gateway_heltec, gateway_heltec_v4, heltec_mobile, heltec_v3, heltec_v4,
heltec_v4_mobile; warning counts are respectively **173/178/177/177/182/182**, matching every pin.
The census is the brief's six-environment exception, not an expansion of the deterministic measurement
pair. `gate3-chain-sources-before.json` and `gate3-chain-sources-after.json` are byte-identical, SHA-256
`ca23089b8a86c45edc6550cf56d6fa72c872d184db155b7d254108a1c2f09575`.

The known `sliceBmac` M04/B342 control reproduced **STAYED GREEN** (its worker returns 1), not a compiler
failure and not a newly measured RED. The battery reports **3 RED / 1 unusable**, with both worker source
hashes restored and the parent tree unchanged. This exact documented exception is retained; no control or
pin was weakened to hide it. The rest of the union is still running at this checkpoint.

## 10. Final coder handoff — 2026-09-09, ready for independent QA, NOT an independent QA verdict

**Implementation and the required coder verification are complete.** The composed standing chain has
all **25 commands** successful after the scoped tools correction in §9.5. The full **49-battery** union
completed at 17:25:49 UTC with **772 RED / 1 known unusable B342 / 0 new unusable**. All failures and
earlier checkpoints remain above; their pending language is historical and superseded by this section.
No commit, simulator-source edit, anchor rewrite, extra owner ruling or out-of-fence production change.

### 10.1 Two separately derived selectors; complete union, not a subset

**S — changed source (17):** `a0rx`, `b159map`, `b159rx`, `b161rx`, `b251rx`, `radmin5rx`,
`radmin5session`, `radmin72rx`, `radmin72session`, `radmin7exec`, `radmin7rx`, `radmin7transcript`,
`sliceBnode`, `sliceBrx`, `sliceEnode`, `sliceGrx`, `teamgrant`. These are every configured `TARGET_SRC`
whose resolved file changed. Firmware real-TU bindings/status are additionally attacked by the
inbox-verbs/console-sink controlled probes, not misrepresented as native-compiled firmware.

**H — historical/dependency (47):** every table row marked H below is retained from the brief's mandatory
47-battery floor. This re-proves codec/domain/cap behavior, dispatcher/authority, provisioning/invalidation,
owned carriers and hash/plane handling, inbox/storage/ACK/JSON, even when its source did not change.
It includes the committed codec's **103**, not its retired 66-control battery. S ∪ H is **49** batteries;
the two new ones add independent open/control intake/lifecycle/sender coverage.

| Battery | Selection reason | RED | Unusable |
| --- | --- | --- | --- |
| `a0rx` | S+H | 7 | 0 |
| `b134ack` | H | 2 | 0 |
| `b134inbox` | H | 5 | 0 |
| `b134ram` | H | 3 | 0 |
| `b134store` | H | 39 | 0 |
| `b159mac` | H | 2 | 0 |
| `b159map` | S+H | 2 | 0 |
| `b159rx` | S+H | 3 | 0 |
| `b161hash` | H | 6 | 0 |
| `b161mac` | H | 1 | 0 |
| `b161rx` | S+H | 8 | 0 |
| `b20codec` | H | 5 | 0 |
| `b20mac` | H | 11 | 0 |
| `b251hash` | H | 55 | 0 |
| `b251rx` | S+H | 19 | 0 |
| `cmdauthority` | H | 16 | 0 |
| `consoleline` | H | 12 | 0 |
| `devicenv` | H | 42 | 0 |
| `grantadmit` | H | 1 | 0 |
| `grantpark` | H | 3 | 0 |
| `radmin2codec` | H | 103 | 0 |
| `radmin3acl` | H | 36 | 0 |
| `radmin3id` | H | 23 | 0 |
| `radmin3verbs` | H | 28 | 0 |
| `radmin5runtime` | H | 16 | 0 |
| `radmin5rx` | S+H | 14 | 0 |
| `radmin5session` | S+H | 25 | 0 |
| `radmin72rx` | S, new open/control | 15 | 0 |
| `radmin72session` | S, new open/control | 36 | 0 |
| `radmin7exec` | S+H | 20 | 0 |
| `radmin7rx` | S+H | 13 | 0 |
| `radmin7transcript` | S+H | 20 | 0 |
| `sliceAinbox` | H | 1 | 0 |
| `sliceAjson` | H | 1 | 0 |
| `sliceBmac` | H | 3 | **1, B342** |
| `sliceBnode` | S+H | 8 | 0 |
| `sliceBrx` | S+H | 16 | 0 |
| `sliceCinbox` | H | 2 | 0 |
| `sliceCpull` | H | 2 | 0 |
| `sliceDack` | H | 1 | 0 |
| `sliceDclear` | H | 5 | 0 |
| `sliceDstore` | H | 3 | 0 |
| `sliceDtoken` | H | 2 | 0 |
| `sliceEnode` | S+H | 3 | 0 |
| `sliceGinbox` | H | 4 | 0 |
| `sliceGjson` | H | 16 | 0 |
| `sliceGrx` | S+H | 42 | 0 |
| `teamgrant` | S+H | 4 | 0 |
| `teamkeyring` | H | 68 | 0 |

All **773 patterns match exactly once**. Each actual worker independently executes baseline
**2909 / 174485 / 0**, then restores its target by hash. `--workers=2` is requested for every battery;
the harness uses one actual worker for a one-entry battery. Every parent report confirms all **56**
configured target files byte-identical to launch. No new compile error, crash, zero-test run or stayed-green
mutant is counted RED. Source-restoration before/after manifests for the complete mutation copy both hash to
`8e3253df15e5b0f91138c80296b522b38187115e33e6286314850c85fb4f02d8`.

The increase is **712 + 36 session/open/control + 15 Node RX/sender + 9 executor = 772 RED**. The new
controls cover owned input, phases/rate/deadline/expiry/wipe, truncation/sequence/freezing, notice domain and
protocol byte, preparation/commit/send ordering, zero-source accounting, saturation/wrong counters,
queue pressure, independent-pool dispatch and context/sink bindings. Existing route/nonce/ACK/actor/private-DM
controls remain active. Source patterns moved only where the production anchor genuinely changed.

`selectors-final2.json` records each selector membership, source path/SHA-256, every labelled pattern/count,
executed RED/unusable total and log SHA-256; its final SHA-256 is
`8d292316ffae3c2f19c319b20222fc9754353aab34ec0feecc7f6fc188571909`.
`gate2-mutations-results.json` and the 49 `gate2-mutations-<battery>.log` files retain each complete run.
`final-gate-audit.log` independently checks all log hashes, both selectors, all counts, worker baselines,
restorations and the composed 25-command chain; it is not a replacement for those raw results.

### 10.2 Exact handoff and reproduction

Base remains `564f460a3b755a104f146da70457e5c8c68e99b9`; revision-4 brief SHA-256 remains
`0920bb419cf5f33fbedb8fabfa2c9dc9b91c81744727fd69cb21b16253a7200e`.
Simulator remains clean at `06746a97de5764415d6fcef10b97bca90569b9c7`. Both repositories pass
`git diff --check`; current BASELINE was re-read at handoff, not inferred from a retired anchor.

The complete shared checkout has **1087 tracked/untracked inputs**, including the named QA preparation
and every file under its untracked `2026-09-09-radmin-slice7b2-reissue/` directory. There are no new
production/test/tool files: the coder modified exactly these **20** plus the generated inventory and this
receipt (**22 coder paths**), with every other preparation input preserved:

```text
lib/core/node.cpp
lib/core/node.h
lib/core/node_mac_rx.cpp
lib/core/remote_session.cpp
lib/core/remote_session.h
src/firmware_commands.cpp
src/firmware_remote_executor.h
test/test_custody_receive_g.cpp
test/test_firmware_remote_executor.cpp
test/test_node_remote_session.cpp
test/test_remote_session.cpp
tools/probe_board_abi.py
tools/probe_console_sink/negctl.py
tools/probe_console_sink/run.sh
tools/probe_console_sink/structural.py
tools/probe_features/ownership.py
tools/probe_inbox_verbs/probe_main.cpp
tools/probe_inbox_verbs/remote_exec_rows.h
tools/probe_inbox_verbs/run.sh
tools/probe_ui_model_mutations.py
```

`handoff-sources-final.json` hashes the final complete checkout, including this completed receipt.
The non-self-referential manifest `handoff-inputs-except-receipt.json` binds the other **1086** inputs,
SHA-256 **`984bebeb6a063b9d23187f8e86e8ce0d69cc188da73efbc21b2309a4a6138eb0`**.
`final_audit.py frozen` verifies root = gate3 except this appended receipt and the exact two-comment
transition from gate2; `compatibility` additionally proves the codec/literals/BASELINE/fw_main untouched
and the inventory's non-anchor bytes identical. Do not use a HEAD-only QA copy.

Commands and their exact exits are recorded by `gate_driver.py` and its result JSONs. The standing
entry points are the brief's §8.2 commands: native wrapper + actual binary, both ABI probes, six
`tools/probe_<name>/run.sh` default/no-controls pairs, full `python3 -m unittest discover -s tools -p
'test_*.py'`, inventory write/bare/check, authority/default+selftest, A0, literal checker and warning
census. Each mutation uses `python3 tools/probe_ui_model_mutations.py --target=<table row> --workers=2`.
Isolated probe/sweep processes explicitly set `MR_LUS_SRC=/home/staszek/lora-universal-simulator`.

For simulator reproduction use a **fresh** Release/Ninja build directory with `MESHROUTE_DIR` pointing
to the complete candidate, build target `lus` (normal + gateway core), then `tools/run_corpus.py --out
<fresh-run> --lus <fresh-build>/orchestrator/lus --jobs 3 --require-anchors`, `--validate` and compare
to a validated fresh base. Preserve the canonical comparator's executable-hash refusal and compare the
actual streams separately, as in §9.3. Board reproduction is `python3 tools/measure_board.py pair
--jobs=1 --output <same-checkout>/.pio-measure/<new-capture>` at the same baseline/final paths and fixed
identity; do not reuse stale source-mtime objects or mutate the captured ELF for attribution.

### 10.3 Limits and QA-owned landings

**No independent QA PASS is claimed.** QA next snapshots the complete dirty handoff and re-runs the gate.
B378/B379 remain open in the maintained register until that independent behavior gate satisfies them.
Proposed B387's console pin-format correction is fully coder-verified by the strict wrapper and the
343-test sweep; QA should record/close that occurrence under its maintained-register ownership.

B342 remains the single documented unusable control. B312 entropy-provider hardware qualification,
B315 measurement output-path restriction, B350 UI no-controls wording, B359 optional stale ABI overlay,
and B364's pre-existing invite fixture over-read are unchanged limitations, not fixed or silently waived.
The full native text run is real; filtered XML is used only for arithmetic, not a claim that B364 is safe.
The board measurer's known documentation hashing is not evidence of firmware code movement.

No new bench part is due now: open/control on-air flood/recovery joins **8b's controller/carrier metal
gate**, as the brief requires. No synthetic entropy/paused-executor fault is hardware qualification.
QA owns the register/design/tracker/MEMORY landings and the later 7b-3 brief. Remote disruptive actions
remain refused; deferred scheduling/controller/UI behavior is not implemented here. The owner commits.

## 11. B388 — comment-only scoped return · 2026-09-09

**Coder correction complete; independent QA's scoped return review remains pending.** This appendix
supersedes §10's next-step status only. The independent full gate is recorded in
[the QA report](2026-09-09-radmin-slice7b2-qa-gate.md), whose §2 authorizes this comment correction and
complete input-preservation proof without repeating the completed instruments. B388 remains QA-owned
and open until that review; no independent PASS or maintained-document closure is claimed here.

### 11.1 Exact change and source validation

Per V1 and brief §6, replaced only the three comment lines at `lib/core/node_mac_rx.cpp:2272–2274`
(current source anchor; QA's dispatch cited 2273–2275).
They now describe scanning CURRENT rows after every classified intake, including refusals, and state
that refusal accounting can change counters and open intake can expire older staging/capture pairs
before refusing. Removed the false promise of unchanged state and unchanged deadline. The caller,
timer arm, counters, admission logic and all executable source remain unchanged.

Verified against `remote_session_receive`, its `ReceiveCompletion` accounting, the `cmd_open_execute`
expiry-before-peer/rate checks and `Node::radmin_expiry_arm`. The source stays **3345 lines**: the
three-for-three replacement preserves downstream line anchors. Replacing exactly the new comment
with the old comment reconstructs the entire pre-return source byte for byte. No inventory regeneration,
test/tool edit, new counter, timer, behavior change or owner ruling.

### 11.2 Fresh focused reproduction and limits

Freshly compiled QA's retained `2026-09-09-radmin-slice7b2-qa/b388.cpp` against the current real
`remote_session.cpp`, `remote_codec.cpp`, `identity.cpp`, `dm_crypto.cpp` and `monocypher.c`; no previously
built QA or coder archive/executable was reused. **15 checks PASS**, exit 0: refused same-peer intake,
inbound **0→1**, earlier expired pair wiped, surviving row/capture unchanged, earliest deadline
**301000→302000**. This executes pure codec/session/deadline-scan behavior, **not** the Node timer/radio.

Scratch evidence root: `/tmp/mr-codex-b388-87e0Pk`. Reproduce with `python3` followed by that root's
`reproduce.py`; exact compiler commands, exits and logs are in `reproduction-complete-results.json`.
The initial standalone link omitted `dm_crypto.cpp` and failed with undefined `dm_seal`/`dm_open`
references (exit 1); `reproduction-results.json` and `reproduction-2.log` retain that setup failure.
Adding the existing source to the scratch compiler command resolved it; no repository change was needed.

**The full native/corpus/boards/probes/mutations/tools chain was not rerun for this comment return**,
as scoped by QA report §2. Its independently measured figures remain the prior QA results, not new
measurements attributed to this appendix. Both repositories' `git diff --check` pass.

### 11.3 Frozen return and preservation

MeshRoute HEAD remains `564f460a3b755a104f146da70457e5c8c68e99b9`; simulator remains clean at
`06746a97de5764415d6fcef10b97bca90569b9c7`. The consumed implementation brief hash in §10 remains
historical authority. Current QA-status brief SHA-256 is
`8de4effdb3bfceae2435bf4c672c1110e65da2548608536bdbb0a802b919883b`; scoped QA dispatch SHA-256 is
`6d8bb3201e642777a59e7a51b04c6c0959c1db71c368563cc0aa3e7a837c0f9f`. Both were preserved.

The fresh pre-return inventory includes **1240 MeshRoute inputs** (including QA's newly landed
documentation/artifacts) and **285 simulator inputs**. The only changed MeshRoute paths are the RX
source's exact three-line comment and this append-only receipt. All other **1238** inputs retain their
bytes, modes and symlink targets; the complete simulator inventory, HEAD and clean status are unchanged.
QA's register/brief/design/MEMORY/AGENTS/guidelines/evidence edits are preserved, not rolled back to §10.

Frozen SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| RX source before B388 | `24addc50e6fe1a5a72e92e8e74c73697ae70d0cf4cce8045392dc75fd7108894` |
| RX source after B388 | `4c1bee50d31298ba9ffcb0290ea936ae6a6ae309c5e52f8c847e043d74685eb6` |
| Receipt before this appendix | `3872539fb44f8bb57710e64237cd038bb00613377ce3bbe810589c7c405ff7e5` |
| `before.json` — complete pre-return snapshot | `01572e48dffa1bb3bc166d2040994dcf619583ee3cabeaf5e54cc540bb6e33f1` |
| `after-inputs-except-receipt.json` — 1239 final MeshRoute inputs | `4871f0a6e08c4431403de88615756af4a95fa1af68e949e2177c23537a10cacc` |

At the scratch root, `python3 audit.py after` verifies the complete input comparison, exact comment-only
replacement and receipt-prefix preservation. `source-comment.diff` retains the scoped diff; `after.json`
freezes both complete final inventories, including this finished receipt. `preservation-results.json`
records the final receipt and complete-manifest hashes outside the receipt to avoid a self-reference.
No commit was made; the next action is QA's scoped B388 review.
