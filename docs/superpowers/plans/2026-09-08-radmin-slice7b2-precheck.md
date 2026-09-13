<!-- QA/Author: OpenAI Codex, replacing Claude under the owner's role assignment -->
# Remote-admin Slice 7b-2 — independent pre-check — 2026-09-08

**Current checkpoint 2026-09-09:** owner committed the independently gated codec preparation at
`564f460a3b755a104f146da70457e5c8c68e99b9`. **Section 8 is the current revision-4 reissue pre-check**;
sections 1–7 preserve their dated observations at `1d4b3ad`, including then-pending proposals/prerequisites.
The [behavior brief](2026-09-08-radmin-slice7b2-session-open-status.md) is ready for coder source-validation;
implementation remains HOLD until that validation passes. B378/B379 remain open for implementation/gates.
This is authoring and measurement, not a production implementation or its full gate.

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

## 8. Committed-codec reissue — revision 4 — 2026-09-09

### 8.1 Actual base, preservation and artifact provenance

Owner commit **`564f460a3b755a104f146da70457e5c8c68e99b9`**, parent
`1d4b3ad5a74c2f24121d8fbe28b8d6e4b04f8dfe`, is the current attribution base. Its subject is “7b-2”, but its
actual source contains the separately gated **7b-2-0 codec preparation** and QA documentation, without new
session-control/open execution or state. The shared checkout was clean; QA inventoried **1067 tracked
file/symlink entries** before authoring. Simulator remains clean at
`06746a97de5764415d6fcef10b97bca90569b9c7`. New authoring changes are only the revision-4 permitted documents
and this section's companion evidence. No production/test/tool input, coder report, ruling, simulator or
anchor was edited; no commit/reset/clean operation was performed.

QA matched every one of **390 implementation/gate input paths** under `lib/`, `src/`, `test/`, `tools/`,
`simulation/`, `variants/`, `boards/`, plus `platformio.ini`, to its own final measured codec checkout
`/tmp/mr-qa-s7b20-gate-qfpdn1ul/measure`. All five frozen codec/header/test/mutation/reference hashes also
match independent gate §1 exactly. The original revision-3 behavior brief hash was
`fdbf7b38d55ed7b67897851544b9acd322fa30a2ecb7b43cff4f267839bf8acd`.

Private source checkout: `/tmp/mr-qa-s7b2-reissue-xo4_k4zt/source`, cloned from this clean owner commit.
Commands below run from that checkout. QA **reused its own final native, B379 and fresh-final simulator
binaries after the source comparison**; it did not build a new native or simulator executable in this task.
The simulator artifact is the corrected **fresh-final** build from codec QA §3, not B385's discarded stale
incremental binary. Its original build compiled both normal/gateway codec objects in the 70-action fresh
build. Reissue executions are new; the builds' provenance remains the previous independent codec gate.

| Reused QA artifact | SHA-256 |
| --- | --- |
| `measure/.pio/build/native/program` | `eb6025299f43a8e53b4b7644a7008813014354ae55c9903e5fdaf8b76e7e2dd0` |
| `b379-codec` | `f4da76eee6f233b87d57298c4dbe7799236de64f6f169ec4e78c8903ee988571` |
| `sim-final-fresh/orchestrator/lus` | `3826d36ee5b07fdaf2eb4836bfcf81240cb4ace18151fce4e66fe693670e186c` |

All three paths above are relative to `/tmp/mr-qa-s7b20-gate-qfpdn1ul`.
Durable [receipt](../evidence/2026-09-09-radmin-slice7b2-reissue/receipt.json),
[input inventory](../evidence/2026-09-09-radmin-slice7b2-reissue/inputs-before.json) and
[commands/exit codes/log hashes](../evidence/2026-09-09-radmin-slice7b2-reissue/runs.json) record the exact inputs.

### 8.2 Independent executions and their limits

| Reissue instrument | Actual outcome |
| --- | --- |
| Re-execute source-matched native program | **2888 cases / 172264 assertions / 0 failed / 0 skipped**, exit 0 |
| Re-execute real-session/codec B379 proof | **134 checks**, exit 0; the new notices remain explicitly synthetic |
| Independent admission reference `--compare test/test_remote_codec.cpp --selftest` | **87/87 old literals**, strict **89/89** including both admission arrays; **25 positive / 1718 valid-tag-invalid tuples** generated; all four comparator controls RED, exit 0 |
| All 36 current scenarios, `--require-anchors`, then `--validate` | **36/36 anchor matches**, zero assertion failures; both commands exit 0 |
| Independent comparison to codec QA's `corpus-final-fresh` | All **36 actual stream files byte-identical**, and every recorded SHA-256 matches both actual streams/manifests |
| Current s18 | **269517 events / 0 assertion failures**, full MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**; authority is current BASELINE, not this receipt |
| Fresh candidate-TU compile/read with actual host, ARM and Xtensa flags | State **3848/8 → 8824/8**, **+4976 B** on each ABI; single capture **1658/2**, three captures plus two counters **4978/2** |
| Candidate adverse control | One extra byte per capture moves candidate state to **8832** on all three ABIs |
| Freshly compile/run B386 standalone real-session proof | **43 checks**, exit 0; existing release compacts two survivor ranks while preserving their other header/seen/chunk/pending-wire bytes |

The exact executable paths/argument arrays are in `runs.json`. Principal commands:

```sh
/tmp/mr-qa-s7b20-gate-qfpdn1ul/measure/.pio/build/native/program
/tmp/mr-qa-s7b20-gate-qfpdn1ul/b379-codec
/home/staszek/mr-slice2-ref/bin/python docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-reference.py --compare test/test_remote_codec.cpp --selftest
python3 tools/run_corpus.py --out /tmp/mr-qa-s7b2-reissue-xo4_k4zt/corpus --lus /tmp/mr-qa-s7b20-gate-qfpdn1ul/sim-final-fresh/orchestrator/lus --jobs 3 --require-anchors
python3 tools/run_corpus.py --validate /tmp/mr-qa-s7b2-reissue-xo4_k4zt/corpus
python3 docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/measure_candidate.py --root /tmp/mr-qa-s7b2-reissue-xo4_k4zt/source --out /tmp/mr-qa-s7b2-reissue-xo4_k4zt/candidate
```

Corpus BASELINE SHA-256 remains `71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`.
Both the new manifest and actual-byte comparison receipt are retained in the companion directory; bulky
streams remain under the private QA roots. Candidate commands, symbol sizes, generated TU and compiler
logs are retained too. These are **candidate types**, not new linked Node/RAM. The tool's measurements of the
current actual Node remain native **225920/8**, gateway **152288/8**, mobile **117912/8**.

No fresh board pair, six-environment warning census, ABI-control suite, full six-probe/tools chain or mutation
union ran during this authoring task. Their current base observations come from the **independently executed
codec QA gate**, now bound to this source-identical commit: **712 RED / known unusable B342**, gateway
RAM/flash **198980/562748**, mobile **207756/1372992**, unchanged Node/RAM. The separate one-byte reference
corruption control is also that prior gate's result, not a new reissue execution. The behavior brief requires
all of these instruments again on the frozen implementation. No B378/B379 closure or behavior PASS follows
from these pre-checks.

### 8.3 Current source ledger and B386 correction

The session, Node, RX, authority and firmware anchors in §3 still resolve at this base; those source files
are unchanged from `1d4b3ad`. The codec anchor/“reserved 0x5” statements there are historical and superseded:

| Current source | Verified contract consumed by revision 4 |
| --- | --- |
| `remote_codec.h:63`, `:97`, `:124`, `:175` | Existing `admission_result` opcode, `RemoteAdmission`, `resp_admission_result` and 28-byte named overhead; actual slots only. |
| `remote_codec.h` `RemoteMessage` / `RemoteDecoded` | `request_ctl`, typed `admission_code`, `admission_detail`; `RemoteResultKind::admission` publishes a separate domain, not a TERMINAL result byte. |
| `remote_codec.cpp:127`, `:243`, `:318`, `:372`, `:655` | Semantic pairing, layout, nonce suffix and clear header already implemented; decoded output publishes only on success. Reuse this codec without production codec edits. |
| `remote_session.cpp:858`, `:972`, `:1014`, `:1028` | Open still stores metadata only; safe/force CONTROL lacks a consumer; acknowledged/full verdicts lack new fixed response producers. B378/B379 behavior work is still real. |
| `node_mac_rx.cpp:2051`, `:2108`, `:2119`, `:2195` | Zero destination refuses before transport; RX reply is bootstrap-only; three transcript counters exist but the new intake/rate/all-sender accounting is absent. |
| `firmware_commands.cpp:911`, `:1629`; `firmware_command_authority.h:277` | Status lacks the five fields. Existing real seam validates/restores context; open authority is exact primary-verb equality, not a second core whitelist. |
| `firmware_remote_executor.h:49` | Full TX defers an auth frame, but the function may still reserve/dispatch an admitted auth request. New control pacing must preserve that distinction. |
| `remote_session.cpp:240–256`, `:262`, `:484`, `:550` | Shared release renumbers later headers' `order`, preserving relative order. Slot install uses that release; whole other-slot transcript headers cannot be promised byte-identical. |

**B386 — brief-only overstatement, closed by revision 4.** Revision 3 §3.2 required other slots' transcripts
“byte-for-byte” unchanged while requiring the existing slot-install path. QA reproduced the conflict with
three real completed transcripts: an earlier slot-0 transcript and two later slot-1 survivors, each with a
nonzero pending send cursor. Rotating only slot 0 through `remote_session_install` renumbers the survivor
headers **1→0 and 2→1**. Their full headers differ; compare against the original with **only `order` decremented**
and they match. Seen records, owned binary output chunks, other epoch/ingress, pending seq1 encoded replies
and surviving relative send order are unchanged. No production control consumer is claimed by this test.

The [43-check reproducer](../evidence/2026-09-09-radmin-slice7b2-reissue/b386-order.cpp) reuses the committed
B379 fixture setup and links the source-matched QA native archives. Its complete compile command and output
are retained. This is a direct observation of existing session functions; no synthetic production hook or
production mutation was used. Revision 4 explicitly permits only the existing shared scheduling-rank/free-list
bookkeeping, preserves all other response/replay/identity invariants, and requires the real force-producer
cross-slot proof in its final gate. No owner wire/storage/rate agreement changes.

### 8.4 Reissued dispatch

Revision 4 consumes the committed codec symbols and the current **2888/172264** and **712 RED + B342** base,
retains the full two-selector gate, and names B385's actual-recompilation requirement. The original storage
prediction is freshly reproduced, with actual linked attribution still owed. Approved global rate, first
source/route, present-zero versus sender refusal, five-counter accounting and deferred/controller boundaries
are unchanged. Existing coder reports and owner rulings remain byte-identical.

**Next: coder source-validates revision 4 against `564f460` and appends its fresh base/brief hash and findings
before coding. Implementation remains HOLD until that validation passes.** QA will independently rerun the
complete gate only after the separate coder's frozen handoff. B378/B379 remain OPEN; B386 is a corrected brief
inconsistency, not a production fix or behavior gate. No permission or owner ruling is outstanding for the
already-approved storage and codec choices.
