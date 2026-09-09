<!-- QA/Author: OpenAI Codex, replacing Claude under the owner's role assignment -->
# Remote-admin Slice 7b-2 — independent pre-check — 2026-09-08

**Owner update 2026-09-09:** R-RA-35/R-RA-36 approve B378/B379's proposals. Behavior brief revision 3 is HOLD
for the separate codec prerequisite, whose [brief](2026-09-09-radmin-slice7b2-0-admission-codec.md) and
[fresh author pre-check](2026-09-09-radmin-slice7b2-0-precheck.md) are now available for coder source-validation.
Sections 1–6 preserve the **2026-09-08 pre-approval observations**, including then-current proposal/HOLD wording
and the coder report's pre-addendum hash. Section 7 records the new disposition. This remains authoring and
measurement, not a production implementation or its full gate.

## 1. Exact base and work preservation

MeshRoute started **clean** at owner commit **`1d4b3ad5a74c2f24121d8fbe28b8d6e4b04f8dfe`**, which includes the
7b-1 implementation and QA landing. Simulator **`06746a97de5764415d6fcef10b97bca90569b9c7`**, clean.
The preceding gate's `146569a` handoff and `d467787` attribution base are history, not the 7b-2 base.
QA cloned the clean current base into `/tmp/mr-qa-s7b2-precheck-no59tyku/source`; a separate census copy contains
one labelled observation in the existing probe fixture. No shared production/test/tool source or simulator
source changed; shared additions/edits are QA documentation, its reproduction artifacts and the subsequent
coder source-validation report preserved in §6. No commit,
reset or clean operation was performed.

## 2. Independently executed pre-checks

| Instrument | Measured result |
| --- | --- |
| `pio test -e native`, then the actual program | **2883 cases / 127709 assertions / 0 failed / 0 skipped** |
| Fresh Release simulator build | `lus` SHA-256 **`7ad152073f13a76192ff2185488326910d28a759c5c882b1cfa59b012b95e2b9`**, MD5 **`1c1a7435713ea2dfb50457aac6b1db50`**, identical to independently gated 7b-1 |
| Fresh corpus plus manifest validation | **36/36 live anchors**, zero assertion failures; all 36 output SHA-256s equal 7b-1 QA's final corpus |
| s18 | **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**, current BASELINE authority |
| Real codec/session negative-reply reproduction | **116 checks pass**; two different planned terminal plaintexts share the current nonce |
| Candidate ABI | Actual host/ARM/Xtensa compiler flags: open capture **1658/2**, three captures plus two counters **4978/2**; final candidate state **8824/8**, current **3848/8**, delta **4976 B** on each ABI |
| Candidate adverse layout control | One additional byte per capture moves final state **8824 → 8832** on all three targets |
| Existing real-handler ACCEPT fixture, observation only | Local `status` **364 bytes**, `version` **113 bytes**; existing probe checks **771 ACCEPT / 378 CLIENT**, zero failures, no controls in this census run |

The 364-byte status is a measured fixture sample, **not** a board-wide maximum or a guarantee that every
`routes` dump fits. The storage proposal explicitly preserves truncation. Candidate sizes are compile/read
measurements using the existing board-ABI instrument's flag and symbol readers; they are **not linked RAM
deltas**, and the mobile must instantiate none of the candidate state. The completed 7b-1 board values remain
the preceding gate's measurements: gateway RAM/flash **198980/562812**, mobile **207756/1372992**. They were not
re-measured as board images during this authoring task. No full mutation union, six-probe gate, tools sweep or
board census is claimed for an unimplemented slice.

Raw receipts are under `/tmp/mr-qa-s7b2-precheck-no59tyku`. Durable measurements and executable reproductions
are in [the evidence directory](../evidence/2026-09-08-radmin-slice7b2-precheck/receipt.json).
Initial QA setup errors are retained: `corpus.log` refused the guessed `sim/lus` path with exit 2;
`corpus-configured.log` uses the actual `sim/orchestrator/lus` and passes. `nonce-build-missing-include.log`
records the first standalone proof compile missing `frame_codec.h`; the corrected proof includes the actual
DataType authority and compiles/runs successfully. Neither correction changed repository source or a gate pin.

## 3. Source facts the brief consumes (V1/V2)

| Source at the pinned base | Verified fact and implication |
| --- | --- |
| `remote_session.h:61`, `:64`, `:70` | Seen capacity is **16 total shared across slots**, authenticated ingress **2**, staging **3 open + 1 bootstrap**; these are not per-credential quotas. |
| `remote_session.h:185`, `remote_session.cpp:858` | Open rows retain request ID, source, route and expiry only. The decoded command span is discarded; no output bytes or rate history are retained. |
| `remote_session.h:237`, `:255` | ACCEPT state ends with the four/eight authenticated transcript pool and **three** saturating counters. There is no separate open transcript. |
| `remote_session.cpp:755` | Readiness gates **all** current target ingress, including open. The brief preserves that existing condition rather than silently enabling unprovisioned targets. |
| `remote_session.cpp:943`, `:972` | ACK is already consumed without ingress. Safe/force control reserves CONTROL without a seen row, retains no body, and has no consumer. |
| `remote_session.cpp:998`, `:1028` | Exact retries preserve the first route; acknowledged retries and shared-pool exhaustion are verdicts without wire producers. |
| `remote_session.cpp:331`, `:372`, `:383` | One live-install path invalidates selected-slot work; one earliest-deadline scan owns ingress/staging expiry. No new timer is needed for the proposal. |
| `remote_session.cpp:428`, `firmware_remote_executor.h:49` | The current main-loop executor can select only admitted seen requests; a control row with `seen_index == none` and an open staging row cannot run. |
| `node_mac_rx.cpp:2108`, `:2119`, `:2195` | Receive-time reply is bootstrap-only, with 33-byte result scratch; authenticated transcript sending is paced and distinguishes codec/seal failure from enqueue failure. |
| `node.cpp:100`, `:113` | Existing epoch draw refuses zero but uses a void HAL provider; existing commit invalidates then re-arms expiry. B312's hardware/provider limitation remains open. |
| `remote_codec.cpp:277`, `remote_codec.h:67` | Response nonce binds key, response ctl, request ID, seq and source; only bootstrap/rollover add epoch. It does **not** bind the terminal result/detail. Response opcode `0x5` is currently reserved. |
| `remote_codec.cpp:200`, `:202` | Bootstrap/rollover result already use the base key and clear epoch in the nonce; rollover result is 34 bytes, not the 33-byte bootstrap scratch size. |
| `firmware_command_authority.h:277` | `remote_open` admits only exact byte equality to an open row's primary verb: `status` and `routes`; no new whitelist is needed in core. |
| `firmware_commands.cpp:1629`, `:911` | The real seam validates before routing and owns context scope. Text status has no radmin counters yet. Native does not compile this TU; the inbox-verbs probe owns its wiring proof. |

Paths without a prefix in this table are under `lib/core/` or `src/` as appropriate. Every anchor must be
relocated and source-validated by the coder; none is permission to reproduce a stale line number.

## 4. Registered findings

**B378 — missing independent open storage/rate policy.** R-RA-22 expressly forbids open/bootstrap borrowing
authenticated transcript/ingress capacity. Existing staging is four 32-byte metadata rows and cannot retain
even the 364-byte fixture `status`. Dispatching later from the discarded span is also impossible. The proposed
three independent 1648-byte captures preserve all four/eight authenticated pool capacities. They cost **4976 B
of additional state**, with a measured layout control. The proposed admission window is **300 seconds**, at
most three open starts globally and one per retained peer per window, implemented with existing staging
deadlines/cooldown rows. Both capacity and rate policy are owner proposals, not consequences silently inferred
from the existing staging TTL.

**B379 — nonce collision in the planned non-executed terminal replies.** The standalone proof links the actual
base's native archives. It fills all 16 seen rows with acknowledged slot-0 work, gets `session_full` for slot 1,
rotates only slot 0 through the real install function, then admits the **same** slot-1 execute under its unchanged
epoch and completes it with no output. A hypothetical `session_full` terminal and the real completed terminal
have different plaintext and the same current nonce. A second codec proof encodes `session_busy` detail 2 then
1 for the same request: nonce is equal and ciphertext XOR equals detail XOR. The existing separate
`PROTOCOL_ERROR` opcode supplies a discriminating nonce-inequality control. There is no production full/busy
producer at this base; this is a demonstrated **design/consumer-integration defect**, not a demonstrated 7b-1
on-air vulnerability. Requiring a well-behaved controller to choose a new ID does not protect a target from a
replayed, already authenticated old request.

The brief proposes a distinct, authenticated, fixed admission-notice envelope with every changing result field
in its clear header, nonce and AAD. Existing transcript bytes/nonces stay unchanged. That wire/design change
requires an owner ruling and a separately attributed codec preparation before the behavior implementation.

## 5. Reproduction

Use a private checkout of the named base. Build native first. Compile
[nonce-proof.cpp](../evidence/2026-09-08-radmin-slice7b2-precheck/nonce-proof.cpp) with C++20, `MESHROUTE_NATIVE`,
include paths `lib/core` and `lib/monocypher`, and the freshly built `.pio/build/native/*/lib*.a` archives inside
the linker's `--start-group`/`--end-group`. Run it: the expected final line is **`PASS: 116 checks`**. The
hypothetical negative replies are labelled in the source; all admission, key, nonce, encode and transcript
operations use production functions.

Run [measure_candidate.py](../evidence/2026-09-08-radmin-slice7b2-precheck/measure_candidate.py)
with `--root <private-checkout> --out <private-evidence-directory>`. It derives the current state declaration,
appends the proposed value-type state in a generated TU, obtains each environment's actual compiler flags
through `probe_board_abi.py`, reads symbol sizes and checks the adverse layout control. It does not edit or
link production state. The checked-in measurement JSON is the receipt, not a pin to copy into implementation.

## 6. QA fold-in checkpoint after coder source-validation

MeshRoute remains at `1d4b3ad`; simulator remains at `06746a9`. The coder's
[revision-1 report](../evidence/2026-09-08-radmin-slice7b2.md) is preserved unchanged, SHA-256
`396e213cfebc3136a9ffc2d1ab35933bb6cdbe3e93d19d25459826ce043a068c`. Its reproduced native/ABI/nonce figures
are the coder's measurements; QA's independent measurements remain §2. This checkpoint did not repeat the
full native suite, candidate ABI, nonce proof, corpus or board gate.

**B380:** QA inspected the actual design diff: only §19.1's 7b-2 status row was modified. Revision 2 names that
exact design path/row and the subsequent coder report among expected preparation inputs. The original clean
base is historical; the current documentation changes are expected and preserved. No ruling text was changed.

**B381:** QA read `lib/core/node_mac_rx.cpp:2043/2051` and
`test/test_node_remote_session.cpp:967`. The shared sender starts with refused admission and returns immediately
on destination hash zero with `radmin_reply_no_dst`, before either lower-level send path. QA matched all **342**
tracked files under `lib/`, `src/`, `test/`, `tools/`, plus `platformio.ini`, between the shared checkout and the
clean private base used for §2's native build. The existing private binary was then run independently:

```sh
/tmp/mr-qa-s7b2-precheck-no59tyku/source/.pio/build/native/program --test-case='*radmin-5/N11*'
```

Result: **1 case / 11 assertions / 0 failed**, **2882 unrelated cases skipped**; exit 0. Log:
`/tmp/mr-qa-s7b2-precheck-no59tyku/foldin-n11.log`. This executes authenticated present-zero ingress, reply
construction, explicit sender refusal, no transport submission/air, and bootstrap reservation release.
It does **not** prove the unimplemented open/control behavior or its new counter wiring.

Revision 2 preserves that boundary, uses routable nonzero hashes for successful return-flight proofs, counts a
checked shared-sender refusal even when it precedes lower-level enqueue, and requires separate pending-open,
fixed/bootstrap and committed-rollover lifecycle proofs. B380/B381 close as documentation corrections only;
B378/B379 remain open pending specific owner rulings and their implementation gates. The next authorized
authoring stage after those rulings is a separate 7b-2-0 codec brief; no codec implementation is dispatched here.

## 7. Owner rulings and codec prerequisite — 2026-09-09

The owner approved the revision-2 proposals, explicitly clarifying that the budget is **three open requests
total per target per five minutes, shared across all requesters**. QA recorded R-RA-35 (independent storage,
global admission budget and measured native/gateway growth with mobile invariance) and R-RA-36 (exact
28-byte ADMISSION_RESULT and separate codec preparation) in the rulings ledger. The coder receipt's §6
contains the owner's original wording and remains unchanged by QA.

The behavior brief is now revision 3: the owner-decision HOLD is resolved, while behavior implementation
waits for separately gated/owner-committed **7b-2-0** and a reissued brief at that actual successor base.
The new codec brief is ready for coder source-validation; its pre-check records fresh executions and
the precise reuse of the existing source-matched native/simulator builds. B378/B379 stay open through their
implementation/gate obligations. B382 registers the two stale codec/test introductions that claim no callers;
their correction is scoped to the already-touched files in 7b-2-0. No production change is made by this update.
