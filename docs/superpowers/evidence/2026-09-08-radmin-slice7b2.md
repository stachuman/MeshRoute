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
