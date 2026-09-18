# Slice 8b — coder source-validation and implementation checkpoints

**Latest checkpoint: revision-6 implementation FROZEN FOR INDEPENDENT QA, §11.**
The required coder chain passes with the established B342 carve-out. Supplemental board-UI instrument drift
B418 is disclosed below; B419 run setup is corrected and closed. Independent QA and metal checks remain pending.
Nothing staged or committed; simulator unchanged.

**Historical revision-2 preflight, 2026-09-18 — STOP-1 B409/B410.**

The requested base and simulator pins match. B408 is real and its proposed ring correction is supported by the
source and the bounded reproduction below. Two semantic disagreements require QA/Author fold-ins before coding:
the observation contract has no permitted delivery handoff, and the claimed execute-only ACK predicate does not
exist. R-RA-46 through R-RA-49 remain settled; these findings do not reopen their policy decisions. No commit is
required to resume; a corrected brief and explicit content re-pin suffice under the measured-tree rule.

## 1. Inputs and preservation

PIN re-synced? YES — MeshRoute HEAD `c07b77f50a16342d532618f760aad49e10208a5d`, simulator HEAD
`6585649ea5a780f0542b2931853a667be56a5b2b`, and revision-2 brief SHA-256
`fcad6fffbbd00d0e1485799213da9d74c09f5264619d161449205b0058f376fb` were read from the actual checkouts.
This line certifies preflight input identity, not an implementation or gate PASS.

The initial checkout contained five modified QA documents and the untracked brief. All six are permitted
preparation inputs: `MEMORY.md`, the maintained bug register, the remote-admin rulings ledger, the remote-admin
design, `tracker.md`, and `2026-09-18-radmin-slice8b-mobile-controller-carrier.md`. Their full hashes and original
diff are archived in [preparation-inputs.json](2026-09-18-radmin-slice8b-preflight/preparation-inputs.json) and
[preparation.patch](2026-09-18-radmin-slice8b-preflight/preparation.patch).

The isolated preflight tree `/tmp/mr-s8b-r2-ksqkjdbw/snapshot` includes all **2797 tracked and untracked input
paths**, not only HEAD. Internal symlinks were relocated to equivalent snapshot targets. Builds and private
characterizations ran there. The complete admission inventory is
[inputs.json](2026-09-18-radmin-slice8b-preflight/inputs.json); the 358 production/test/tool/platformio input subset
is [primary-inputs.json](2026-09-18-radmin-slice8b-preflight/primary-inputs.json).

The only shared-checkout edits made during this preflight are this receipt and its evidence directory, plus the
register's B409/B410 rows, next-free marker and dispatch correction. The brief, production, tests, tools and
other five preparation inputs are unchanged. The simulator remains unchanged and clean. Final preservation and
whitespace results are in [preservation.json](2026-09-18-radmin-slice8b-preflight/preservation.json).

## 2. STOP-1 B409 — observation delivery is outside the permitted seams

Brief §4 requires three **emit-free** observation hooks in the core immediately before the existing push sites,
and also requires immediate USB/BLE output through the request's transport. It permits no resident observation
state beyond `carrier_ctr`. The actual delivery adapters exist only within command/service call scopes:

- `src/firmware_commands.cpp:535`, `remote_client_service_once`, constructs `RemoteClientDelivery` on its stack.
  The command handler does the same; no adapter is resident in Node.
- `src/fw_main.cpp:1570` drains the existing push ring. `Node::next_push` at `lib/core/node.cpp:2484` copies and
  removes each row; inspecting that ring from the later controller service cannot recover the drained event.
- The controller service call at `src/fw_main.cpp:1842` receives a local BLE adapter. Its underlying `ble_sink`
  is file-local at line 418. There is no existing transport-aware remote delivery callback in the HAL.
- Brief §6 excludes `fw_main.cpp`, `firmware_commands.h`, `node.cpp` and `hal.h`. Thus the required pure hooks
  cannot emit, and neither the push-drain handoff nor a new delivery seam is authorized. Adding a resident sink,
  callback or private observation queue would also violate the allocation contract.

These are **15 static source/fence checks**, not an executable event-delivery proof:
[observation-audit.json](2026-09-18-radmin-slice8b-preflight/observation-audit.json).

**Recommended fold-in:** explicitly authorize a call-scoped observation matcher/emitter at the existing push
fanout, with the narrow board-glue call and declaration inside the fence. Preserve the existing custody arm's
single `print_custody_failure` call and the original pushes; own no observation state. QA must reconcile that
placement with §4's source-site requirement, or name another concrete permitted handoff. Do not silently install
a global callback or a second queue. The wiring proof must compile and exercise the production matcher/emitter
and structurally bind its board call.

The same section has a format mismatch: its prose requires both `rec.failed_origin` and
`tail.original_reporter`, but its literal USB/BLE formats have only `origin`, `layer` and `reason`. The corrected
contract must give the original reporter a distinct field before the goldens/emitter land; neither dropping it
nor substituting it for `origin` satisfies the stated contract.

## 3. STOP-1 B410 — rollover already passes the ACK flag

Brief §3 says `send_ready` already passes `e2e_ack` only for execute. Actual `lib/core/remote_client.cpp:175`
tests only `state == Phase::request_ready && (p.core.flags & kAck)`. Direct authenticated safe and force rollover
requests also enter `request_ready`; `remote_client_start` copies the supplied ACK flag into the row at line 352.
The local parser accepts both `remote t -e -a rollover` and `remote t -e -a rollover confirm`.

A characterization through the real parser, identity/key derivation, request encoder and controller, with a
recording carrier and a pre-populated valid session, reproduces:

| Parsed line | Actual `submit_request(e2e_ack)` |
| --- | --- |
| `remote t -e rollover` | false |
| `remote t -e -a rollover` | **true** |
| `remote t -e rollover confirm` | false |
| `remote t -e -a rollover confirm` | **true** |

Result: **36 checks, zero failures**. A separately compiled private copy with an execute-opcode guard makes the two
current-leak assertions fail: **36 checks, two failures, expected RED**. This establishes discrimination; that
private change is not a landed fix. See [control-ack-proof.cpp](2026-09-18-radmin-slice8b-preflight/control-ack-proof.cpp),
[positive log](2026-09-18-radmin-slice8b-preflight/logs/control-ack-run.log) and
[negative log](2026-09-18-radmin-slice8b-preflight/logs/control-ack-negative-run.log).

The current board carrier is a stub, so this has not sent an ACK-requesting rollover on a board. Binding the real
carrier would expose it. **Recommended fold-in:** correct the source claim and explicitly require execute-only
ACK eligibility consistently in sending, B408 admission/capacity accounting and observation matching, preserving
accepted local grammar. Prove bootstrap, both rollover forms and response ACKs remain flag-free, including a
supplied `-a`. This implements §3's stated policy; no new owner choice is proposed.

## 4. Verified allocation and B408

The standing ABI probe passes **290 checks, nine controls RED, zero unusable**. Separately, a private candidate
header adds only `uint16_t carrier_ctr` after `target_book_slot` and is compiled using each real environment's
derived compiler/flags. Baseline and candidate agree:

| Environment | Node bytes | Pending core / row / state bytes | State alignment |
| --- | ---: | --- | ---: |
| native | 235248 | 120 / 352 / 4512 | 8 |
| heltec_mobile | 122176 | 120 / 352 / 4512 | 8 |
| gateway | 157344 | 120 / 352 / 4512 | 8 |

`target_book_slot` is at offset 117 and candidate `carrier_ctr` at 118 on all three ABIs. The measurement symbols
encode offsets as array lengths of offset + 1, so the recorded 118/119 lengths mean offsets 117/118. This is
**compile-only layout evidence, not linked RAM or an implemented allocation**. Commands and measurements:
[layout/results.json](2026-09-18-radmin-slice8b-preflight/layout/results.json).

B408's labelled synthetic fixture fills the real mobile Node's `_pending_e2e_acks` ring through its existing
test friend and real `e2e_ack_arm`. The advertised correlation-free count remains eight while the actual ring
has zero free rows and the home-side `_deleg_acks` ring is empty. Both existing producer paths admit a wrapper
without tracking when full; the real untracked-ring-full diagnostic occurs twice, with no live-row eviction.
**30 checks, zero failures.** The opaque payload is sufficient for producer-capacity characterization; this is
not an authenticated RPC or end-to-end carrier proof. See
[b408-proof.cpp](2026-09-18-radmin-slice8b-preflight/b408-proof.cpp) and
[log](2026-09-18-radmin-slice8b-preflight/logs/b408-run.log).

Two minor source annotations should be corrected with the fold-ins: `e2e_ack_ring_full()` is private
(`node.h:2596`), not public; the brief's allowed Node-member implementation can access it. Also the XL
`delegate_send_layer` arm uses `dst_hash` as its ACK key with the XL tier; only the same-layer mobile wrapper arm
uses wildcard zero. Neither fact invalidates B408's choice of ring or requires another allocation.

## 5. Checks, limits and handback

| Instrument run freshly in preflight | Result |
| --- | --- |
| `pio test -e native`, then actual native binary | **2947 cases / 193734 assertions / 0 failed / 0 skipped** |
| Standing board ABI probe | **290 checks / 9 controls RED / 0 unusable** |
| Candidate two-byte padding probe | Unchanged sizes on all three ABIs, as above |
| B408 real-Node producer characterization | **30 checks / 0 failed** |
| Rollover ACK characterization / private guard control | **36 / 0**, then **36 / 2 expected failures** |
| Observation source/fence audit | **15 static checks** |
| Independent reference, freeze-check / compare / selftest | **94/94**, old 89 identical, **5 comparator controls RED** |

All logs and exact compiler command records are in
[the preflight archive](2026-09-18-radmin-slice8b-preflight/README.md). Initial fixture-development failures are
preserved: B408's first compile accessed a private method outside its friend; the rollover fixture's first run
decoded opcode bits incorrectly (four failed checks), followed by a missing codec-header compile failure. The
final fixtures use the existing friend and real `remote_layout`; the results above are their fresh runs.

**Not run:** implementation §9 gate, corpus/simulator rebuild, mutation union, linked board/RAM/flash gate,
warning census, or metal checks. No byte-identical corpus claim is made from this preflight; it remains the
brief's prediction for the implementation and independent gates. Production, test and tool inputs are unchanged.

**Handback to QA/Author:** fold B409/B410 and the minor source annotations into the brief, specify the delivery
handoff and complete custody format, and re-pin. Implementation remains STOP-1 pending that source-consistent
contract. The preserved uncommitted input set remains the base; commits are not a blocker.

## 6. Revision-3 resume — 2026-09-18, STOP-1 B411/B412

PIN re-synced? YES — actual MeshRoute HEAD `c07b77f50a16342d532618f760aad49e10208a5d`, actual clean simulator
HEAD `6585649ea5a780f0542b2931853a667be56a5b2b`, and revision-3 brief SHA-256
`ed2be13f3ce8f652fe5b9e301643502eb24c63c3e766fc83a76b9b4a073b44f0` match this dispatch. R-RA-46..49 are unchanged.
The original B409 handoff is now concretely fenced and the B410 execute-only correction is explicitly authorized.
Two remaining contract disagreements prevent a source-validation PASS under P4; neither is a commit/access issue.

The admission inventory now contains **2846 tracked/untracked paths**, including all revision-2 evidence. The
six QA preparation inputs and their full hashes are recorded in
[the revision-3 inventory](2026-09-18-radmin-slice8b-preflight-r3/preparation-inputs.json), with the exact brief and
original preparation diff beside it. All **358 primary inputs** match both the shared checkout and the complete
revision-2 snapshot. The bounded executables below therefore use that snapshot's real native libraries; this
resume does not claim a new full native build or full gate. The revision-2 measurements above remain historical.

### B411 — prescribed placement reverses the promised BLE order

Revision 3 §1/§4/§7 pins the new observer call **after the push switch but before the existing BLE fanout**.
The end of its handoff paragraph simultaneously requires **existing text/JSON first, then the controller line**.
At the actual `fw_main.cpp` push drain, the USB text is rendered in the switch, while the generic BLE JSON is
written in the subsequent `if (mrble::connected())` block. Inserting an emitting observer between them puts its
BLE `remote_carrier` event before the correlating generic push.

The existing adapter cannot defer that event until a later explicit flush: `LineSink::write` at
`src/dispatch_sink.h:47` invokes its callback immediately on newline, and `console::JsonBuf::finish` appends a
newline. A bounded characterization through the **real JSON event writer, push writer and LineSink** reproduces:

```text
{"ev":"remote_carrier","id":"0000000000000064","event":"acked","ctr":42}
{"ev":"e2e_acked","origin":2,"ctr":42,"sender_hash":0}
```

**9 checks / 0 failures.** Moving only the synthetic observer-event call after the existing push makes the
BEFORE-placement characterization RED (**9 checks / 3 expected failures**) and produces the promised push-first
order. The remote event is synthetic because no observer has been implemented; this is an executed writer/sink
ordering proof, not full board wiring. See
[fixture](2026-09-18-radmin-slice8b-preflight-r3/fanout-order-proof.cpp) and
[logs](2026-09-18-radmin-slice8b-preflight-r3/logs/fanout-order-run.log).

**Recommended narrow correction:** place the single observer call after the existing BLE fanout, still inside
the push-drain loop and CLIENT guard, and align §1/§4/§6/§7 plus the structural proof. That preserves both
existing renderings before the controller event, the one custody call, the unchanged pushes and zero resident
observation state. Include the necessary header/stack-adapter setup in the narrow glue fence. No policy ruling
or additional resident allocation is needed. If the before-BLE placement is intended instead, the promised BLE
order must be changed explicitly; the coder cannot satisfy both.

### B412 — carrier `full` cannot carry the promised ring-pressure reason

Revision 3 §3 step (3) says E2E-ring-full returns `RemoteClientSend::full` and that the core reports
`correlation_full`. Its §1 table calls the existing interface final. Actual `send_ready` at
`lib/core/remote_client.cpp:178` maps **every** `full` return to `radio_enqueue_failed`; the enum at
`remote_client.h:120` contains only `queued`, `full`, `unavailable`. Initial admission's separate
`correlation_free` check does report `correlation_full`, but it cannot describe pressure arising later, between
bootstrap and execute or before a resend. Changing every ACK-requesting `full` into `correlation_full` would
misclassify a TX enqueue refusal, which the same brief requires to remain `radio_enqueue_failed`.

Two characterizations reuse the unchanged fixture prefix from `test/test_remote_client.cpp`, real keys,
codec and controller, with **synthetic carrier refusal**:

| Situation | Actual result |
| --- | --- |
| Initial `correlation_free=0` | `correlation_full`, no carrier submission or pending row |
| Valid warm session, initial capacity available, carrier returns `full` | `radio_enqueue_failed`, row released, radio-enqueue-failure counter increments |
| Capacity available at start, real authenticated bootstrap reply, subsequent execute submit returns `full` | Remains `request_ready`, radio-enqueue-failure counter increments, no local terminal |

**2 cases / 22 assertions / 0 failures / 0 skipped.** This is a consumer-mapping characterization, not a
real-Node late-ring-fill or end-to-end proof. See
[fixture](2026-09-18-radmin-slice8b-preflight-r3/full-mapping-proof.cpp) and
[log](2026-09-18-radmin-slice8b-preflight-r3/logs/full-mapping-run.log).

**Correction needed:** distinguish initial capacity refusal from send-time pressure. If the latter must also
report `correlation_full`, authorize a distinct typed carrier outcome (no resident bytes or wire change) and
specify its service/retry handling. Otherwise explicitly retain the existing generic `full` mapping and limit
the `correlation_full` promise to initial admission. Either correction leaves R-RA-46..49 intact; neither may be
silently inferred from the inconsistent text. The real carrier must still refuse before enqueue on a full E2E
ring, independently of the selected local error mapping.

### Preservation and handback

The [archive README](2026-09-18-radmin-slice8b-preflight-r3/README.md) states reproduction commands and limits.
The initial fanout fixture compile failed because its global Arduino define changed the native feature layout
and its `EF_*` macro use was namespace-qualified incorrectly. The corrected fixture scopes the define to the
existing Print adapter include and keeps core/console on the native profile; no production assertion was
disabled. That failed attempt and the final successful runs are all archived.

No production, test, tool, brief or simulator file was changed in this resume. Only this receipt, the new
preflight archive, and register §0/B411/B412/next-free markers were updated. All prior work is preserved; final
hash and whitespace checks are in [preservation.json](2026-09-18-radmin-slice8b-preflight-r3/preservation.json).
No implementation, freeze, corpus run, mutation union, board build, full gate or metal check is claimed.

**Next action: QA/Author reconciles B411/B412 and re-pins the brief; then coder resumes implementation.**
The register's stale revision-2 STOP paragraph was replaced with this current source-validation result; B409/B410
remain closed as their original fold-ins. No new owner ruling or commit is requested.

## 7. Revision-4 preflight — PASS, implementation started 2026-09-18

PIN re-synced? YES — MeshRoute `c07b77f50a16342d532618f760aad49e10208a5d`, clean simulator
`6585649ea5a780f0542b2931853a667be56a5b2b`, and revision-4 SHA-256
`f82429fe979c3a139ebb9905cab3652fdbf89ce4fce30563cd623e360a3940e4`.
The complete 2866-path input inventory and six QA preparation hashes are in
`2026-09-18-radmin-slice8b-r4-implementation/`. The full dirty tree was copied into the isolated base snapshot
`/tmp/mr-s8b-r4-_d7gpj1e/snapshot`, including earlier untracked evidence. No production input had changed.

B411's call follows the existing BLE fanout, with include and stack adapter explicitly fenced. B412 authorizes
the new typed carrier result and defines both start refusal and service retry without radio-failure accounting.
The §1 pre-check summary cell still says `full` for E2E pressure; the explicit revision-4 header, §3 step (3), §4,
fence, proof and B412 resolution consistently supersede it with `correlation_full`. This is a stale summary
annotation, not an unresolved choice. The brief remains untouched/frozen. R-RA-46..49 are unchanged.

Implementation uses Node-member carrier access to the existing private send/ring predicates; no new TU and no
`delegate_send_layer` signature/body change is planned. Allocation remains only `carrier_ctr` in measured tail
padding. Full implementation and independent gates remain pending; this preflight is not a gate PASS.

## 8. Revision-4 implementation checkpoint — STOP-1 B413

**2026-09-18: partial implementation preserved; no implementation freeze or full gate PASS.** The §7 preflight
missed the authoritative-ID branch before the mobile wrapper. The semantic contradiction below supersedes its
permission to continue. Remaining gate processes were stopped after reproduction, not reported as passing.

PIN re-synced? YES — MeshRoute `c07b77f50a16342d532618f760aad49e10208a5d`, simulator
`6585649ea5a780f0542b2931853a667be56a5b2b`, brief SHA-256
`f82429fe979c3a139ebb9905cab3652fdbf89ce4fce30563cd623e360a3940e4`.
This certifies checkpoint identity, not completion. R-RA-46..49 remain unchanged; no commit prerequisite.

### B413 — a known target bypasses the prescribed wrapper and aliases the observation counter

The frozen §3 call to `send_by_hash` does not guarantee the wrapper described by §§0/3/4:

- `Node::send_by_hash`, `lib/core/node_hashlocate.cpp:1716`: an authoritative `id_bind_find_by_hash` result calls
  `do_send(resolved_id, …, DATA_TYPE_REMOTE_CMD, …)` **before** the registered-mobile wrapper branch. It still
  routes via the home, but the queued frame is direct `REMOTE_CMD`, not `MOBILE_SEND` destined to the home.
- `Node::next_ctr`, `lib/core/node_mac.cpp:29`, allocates per destination. Two different resolved destinations
  may both receive counter 1. `enqueue_data` at `:490` arms direct requests with a destination key and the 60-s
  tier; only the wrapper/last-mile arm gets the wildcard key and 300-s tier claimed for this same-layer carrier.
- These bindings are reachable: `lib/core/node_beacon.cpp:686` learns an authoritative static sender from its
  own beacon. The condition is `!b.is_mobile`, not a prohibition on a mobile receiver learning a static target.
- The prescribed and implemented `remote_client_observe_ack`, `lib/core/remote_client.cpp:666`, selects the first
  live execute/ACK row with the supplied counter. The firmware observer has no destination argument to pass to
  that matcher. Both requests therefore match the first row.

The [reproducer](2026-09-18-radmin-slice8b-r4-implementation/b413/test_b413_characterization.cpp) explicitly labels
its synthetic pending rows and opaque RPC bodies. The registered Node carrier, queue, counter allocation, E2E
ring, timer expiry, pushes and firmware observer are production code. It is a carrier/observation proof, not an
authenticated remote-execution chain. Two pending requests target hashes bound authoritatively to IDs 2 and 3:

| Observed property | Result |
| --- | --- |
| Local queue | Two `REMOTE_CMD` entries, destinations 2 and 3, both ACK-requesting |
| Recorded counters | Request 1: `1`; request 2: `1` |
| E2E ring | Two rows consumed; both still live at 59,999 ms after submission |
| Actual timer at 60,000 ms | Two timeout pushes, one per destination; both rows released |
| Actual observer | Both pushes attributed to request 1; destination 3's request is misattributed |

The selected test passes **1 case /26 assertions /0 failures** on the candidate's exact 361 production/test/tool
inputs. Its separately archived fixture adds one selected test, so the other **2966 cases are deliberately
skipped** in this bounded run. The normal suite result below has no skips. Changing only the fixture's binding
confidence to claimed (`MR_B413_SOFT_BINDINGS=1`) forces the existing wrapper branch: the characterization is
**RED, 22 assertions /11 expected failures**, exit 1. That is an input counterexample, not a production mutation.
[Build/run/control logs and reproduction instructions](2026-09-18-radmin-slice8b-r4-implementation/b413/README.md)
are retained. The first fixture compile failed because its HAL lacked `emit`; that scratch failure and the
corrected builds are also archived, not counted as a product or gate failure.

**Required QA fold-in:** reconcile the sender/correlation contract and its fence. Recommended: ensure the
controller uses the existing shared wrapper path even for an authoritative target binding, with a narrowly
authorized sender seam and controls for known/unknown bindings and concurrent targets. Do not duplicate wrapper
construction or add unmeasured observation state. Revision 4 explicitly excludes changes to the relevant
`send_by_hash`/`do_send`/`enqueue_data` bodies; they remain untouched. Alternatively, a different correlation
contract must specify enough identity to disambiguate the actual paths. The coder has implemented neither
out-of-fence alternative. The frozen brief stays unchanged until QA's explicit reissue/re-pin.

### Preserved implementation and unfinished instrument work

The candidate binds the real Node carrier, adds the target admission-gated E2E ACK, adds `carrier_ctr` in padding,
implements the one automatic resend and bounded ACK-debt cadence, and wires the pure matchers and call-scoped
USB/BLE observer after generic fanout. Tests cover the real mobile/home/target chain, replay, refusal, ring
pressure, timing, actual custody transport and observer output; the companion contract and affected probes and
mutation batteries were extended. These changes remain uncommitted and **are not complete** because of B413.

**B414:** `probe_console_sink` exits 1 at S45. Its `allowed_client_node` regex in
`tools/probe_console_sink/structural.py:660` recognizes the old controller accessors but rejects the now-required
stack `NodeRadminClientCarrier carrier(g_node)`. It reports 6 profiles /720 checks /83 structural checks and
149 controls /0 unusable; S45 fails. The pending repair must recognize only that authorized binding and retain
the S-C45 forbidden-`node_id` control. Default/`--no-neg` and tools discovery must be rerun. This is instrument
work, not an owner-policy question or a reason to call the existing failed gate green.

Earlier focused mutation iterations also exposed three test/instrument defects: `radmin8client` C39 matched
twice, C43 survived an insufficient same-target debt assertion, and `radmin8brx` X09 survived without checking
the resulting E2E-ring occupation. Their candidate pattern/test repairs are present, and the full candidate
native binary passes, but their controls have **not** completed a fresh run. Earlier RED totals are not inherited.

### Actual checks before STOP

The complete candidate snapshot `/tmp/mr-s8b-r4-_d7gpj1e/final-gate` contains **2870 input paths**, including all
uncommitted/untracked implementation and preparation documents. Its name is a driver label, not a freeze verdict.
The original admission snapshot remains at the same parent directory's `snapshot` path. Both are inventoried.
Mutation workers use private copies; the simulator checkout was neither edited nor used as a mutation target.

| Instrument | Observed result and limit |
| --- | --- |
| Native, final shared candidate | `pio test -e native` then actual binary: **2966 cases /195676 assertions /0 failed /0 skipped**. Logs under `iteration-logs/`, `mr-s8b-r4-native-final-build2.log` and `mr-s8b-r4-native-final2.log`. The later snapshot-driver repeat had not started. |
| Board ABI | **290 checks /9 RED /0 unusable**. Node **235248 native /122176 mobile /157344 gateway**, unchanged. Pending core **120**, row **352**, CLIENT block **4512**, `carrier_ctr` offset **118**. No resident growth. |
| Independent codec reference | **94/94**, old **89/89 identical**, five comparator controls RED. |
| Simulator + corpus | Stock simulator rebuilt against candidate, including both variants of changed core TUs. **36/36 current anchors match**, zero assertion failures; corpus validation passes. s18 **`32afbf11e43b4bf9d0bd470ad502ba0a`**, **269517** events. This is anchor matching; a separate `cmp` against every prior full stream was not run. |
| Deterministic board pair | Base and candidate pair complete. Gateway **204036 RAM /574944 flash**, delta **0 /+48**; mobile **211772 RAM /1395096 flash**, delta **0 /+2264**. Object counts **288/332**. Full section/object flash attribution is not complete. |
| Custody USB, BLE line, features | Default and `--no-neg` complete successfully. Custody **27/10**, new observer structure **4 checks /4 RED**; BLE **55/12**; features **121/62** (check/control pins). |
| Console sink | **FAIL, S45 (B414)**. No subsequent `--no-neg` completion claimed. |
| Mutation union | Selector audit derives **61 batteries /973 configured**, every pattern matches once. Only **14 completed batteries** have exit-0 result records; remaining runs were interrupted or not started. **No union PASS or final RED total.** |
| Remaining chain | Inbox default, explicit CLIENT and firmware UI were interrupted; deferred-actions has no completed result. One-off xiao_mobile was interrupted during compilation. Six-environment census, B278 ABI, full tools discovery, inventory/authority/A0/literal chain and later native repeat did not complete in this driver. |

The generated command inventory currently has re-derived source anchors; semantic authority is unchanged by
intent, but the pending checker chain must establish that, not this receipt. No metal check or independent QA
gate ran. B408 remains open.

### Evidence and preservation

- [Candidate input hashes](2026-09-18-radmin-slice8b-r4-implementation/final-gate-inputs.json) identify the complete
  candidate snapshot; [artifact index](2026-09-18-radmin-slice8b-r4-implementation/artifact-index.json) maps original
  logs to archives with full hashes. Large logs are gzip-compressed without changing their decoded bytes.
- `final-logs/` holds completed result records and interrupted logs; `final-union/` holds selector/native-input
  manifests, 14 completed result records and remaining partial logs. `drivers/` preserves exact invocations.
  [Stopped process record](2026-09-18-radmin-slice8b-r4-implementation/stopped-gates.json) identifies the task-owned
  gate processes terminated after B413; no reset or cleanup was performed.
- `boards/base/` and `boards/candidate/` retain manifests, section/symbol inventories and build logs. Full ELFs
  remain in the respective snapshot `.pio-measure/s8b-base` and `.pio-measure/s8b-final` directories; their hashes
  are in the manifests. Corpus streams and `lus` remain under the temporary parent (`corpus-final/`, `sim-final/`);
  `corpus/` archives the manifest, baseline and full stream SHA-256 inventory.
- [Preservation audit](2026-09-18-radmin-slice8b-r4-implementation/preservation.json) checks candidate production
  identity, preservation of preparation inputs other than the maintained register, brief identity, simulator
  HEAD/cleanliness and whitespace. The post-STOP shared edits are this receipt/evidence and register dispatch plus
  B413/B414; no further production/test/tool change was made. No staging or commit occurred.

**Next: QA/Author folds B413 into an explicitly re-pinned brief; coder resumes the preserved implementation,
repairs B414 and finishes the required chain before freezing.** R-RA-46..49 stay settled.

## 9. Revision-5 resume — STOP-1 B416; scoped B414 repair

**2026-09-18 — no implementation freeze or full chain PASS.** Production and all prior tests remain exactly at
the preserved revision-4 candidate. Revision 5 resolves the wrapper bypass and permits the B414 tool repair,
but its added target-binding predicate requires an input absent from its prescribed handoff. This was reported
before editing production. Only the independently authorized S45 repair proceeded; the brief remains frozen.

PIN re-synced? YES — MeshRoute `c07b77f50a16342d532618f760aad49e10208a5d`, clean simulator
`6585649ea5a780f0542b2931853a667be56a5b2b`, revision-5 brief SHA-256
`1501ab7ca6ba25265340fe7a9f399564a1afed7b4836c3502d0d85fd8ef7954a`.
R-RA-46..49 are unchanged. The [3018-path admission inventory](2026-09-18-radmin-slice8b-preflight-r5/inputs.json)
includes all uncommitted and untracked candidate/evidence files. All **361** prior production/test/tool/platformio
inputs matched the preceding checkpoint before the S45 edit. The complete isolated copy is
`/tmp/mr-s8b-r5-mfhusrrg/snapshot`; it includes the candidate rather than just HEAD.

The [six QA preparation hashes](2026-09-18-radmin-slice8b-preflight-r5/preparation-inputs.json) are re-inventoried.
Compared with the preceding final preservation audit, five changed: the brief, register, design, tracker and
MEMORY. The ledger is unchanged. Their admission bytes/diff are preserved; no QA preparation input except the
maintained register is edited by this resume.

### B416 — the required binding comparison has no input path

Revision 5 §4 prescribes this pure matcher:

```cpp
remote_client_observe_ack(const RemoteClientState&, uint16_t ctr, bool timed_out,
                         uint8_t push_dst, uint32_t push_sender_hash)
```

It requires an ACK's `push_dst` to equal the target binding **when the mobile has one**. The state and push do not
contain that binding:

- `RemoteClientRoute` at `lib/core/remote_client.h:10` holds only `target_hash`, layer hops and hop count.
  `RemoteClientState` has no Node reference, binding table or resolved target ID.
- `Node::id_bind_find_by_hash`, declared publicly at `lib/core/node.h:1339`, is the existing reusable const lookup.
  Its implementation reads that Node's `_id_bind` table. The five prescribed matcher arguments cannot access it.
- Revision 5 retains the firmware call passing `g_node.remote_client(), pu, …`, exactly the current call at
  `src/fw_main.cpp:1836`. The header observer and delivery wrapper receive state, push and sinks, not Node or a
  call-scoped binding lookup. On a same-layer ACK the real producer sets `sender_hash=0`, so that field cannot
  supply the missing stable identity either.

An information-dependency counterexample makes the gap explicit: hold all prescribed values fixed (target hash
`0x22222222`, live execute/ACK request 1, counter 7, ACK `dst=2`, `sender_hash=0`). If the omitted Node table binds
that hash to ID 2, the new predicate accepts; if it binds it to ID 3, the predicate rejects. A pure function of
the identical stated input values cannot produce both required answers. **This is a logical counterexample,
not an executed two-Node fixture.** The [source/contract audit](2026-09-18-radmin-slice8b-preflight-r5/b416-source-audit.json)
passes **16 static checks**; it makes no runtime or new gate claim. Its script reuses the repository's function
body extractor to scope the sender check; an initial scratch whole-file count also matched `request_resolve`
and was corrected to the named `send_by_hash` body.

**Required fold-in:** specify a call-scoped read of the existing Node lookup and how its result reaches the
identity check. For example, authorize a Node-aware firmware adapter that performs the binding veto after the
pure kind/counter matcher selects a candidate, and explicitly adjust the board-call/probe contract; alternatively
pass the lookup result through a revised pure matcher interface. Preserve zero additional resident bytes and
the single post-fanout observer. Do not silently omit the predicate, reach a global Node from the pure core,
infer an ID from the hash, or introduce a stored callback. B415 remains the separately parked equal-counter
residual; it does not authorize dropping this new predicate. No owner policy ruling or commit is required.

The `via_home=false` default and one `!via_home` conjunct are source-consistent and now explicitly fenced. The
old §2 OUT summary still excludes the sender body, but the new §3/§6 specific authorization supersedes it; that
stale summary is not a separate STOP. The wrapper change itself has not been applied while B416 remains open.

### B414 — scoped instrument repair

`tools/probe_console_sink/structural.py` now recognizes exactly
`meshroute::NodeRadminClientCarrier carrier(g_node);` alongside the existing allowed controller accessors.
The regex does not permit arbitrary `g_node` use. No runner pin changed. The
[patch](2026-09-18-radmin-slice8b-preflight-r5/b414.patch) is the only new tool/code change in this resume.

Fresh default probe: **PASS — 6 profiles /720 checks /83 structural /905 BLE-guard /6 ownership checks,
149 controls /0 unusable**. S45 passes on the real binding; **S-C45's injected `node_id` remains RED**, explicitly
requiring S45 to fail. Fresh `--no-neg` exits 0 at the same positive pins; its log correctly calls it probe-only,
not an independent negative-control gate. See [default log](2026-09-18-radmin-slice8b-preflight-r5/b414-console.log)
and [probe-only log](2026-09-18-radmin-slice8b-preflight-r5/b414-console-no-neg.log).

Fresh corrected full tools discovery: **351 tests /0 failures /0 skips**, exit 0. The exact command is
`MR_LUS_SRC=/home/staszek/lora-universal-simulator python3 -m unittest discover -s tools -p 'test_*.py' -v`.
[Corrected full log](2026-09-18-radmin-slice8b-preflight-r5/b414-tools-corrected.log) and
[command/result record](2026-09-18-radmin-slice8b-preflight-r5/commands.json) are retained.

**B417, coder setup error, corrected:** the first discovery omitted `MR_LUS_SRC`, so the isolated snapshot's
feature probe sought a nonexistent sibling simulator. It completed **351 tests /4 failures /1 skip**, exit 1.
All four failures were in `test_probe_features.TheGateRuns`; the skip was the explicit lack of a measured ELF
for the board-tool parser test. The [failed log](2026-09-18-radmin-slice8b-preflight-r5/b414-tools.log) is preserved.
The corrected run supplied the real unchanged simulator path and a hash-verified copy of the prior candidate's
gateway ELF solely as a parser fixture. The
[fixture provenance](2026-09-18-radmin-slice8b-preflight-r5/tools-elf-prerequisite.json) labels its older measurement;
this is not a new board build or linked-RAM claim. The omitted test also
[passed separately](2026-09-18-radmin-slice8b-preflight-r5/b414-tools-elf.log) before the full successful replay.
No source patch or lowered pin was used to conceal the setup failure. B417 closes that invocation error only.

The [final preservation audit](2026-09-18-radmin-slice8b-preflight-r5/preservation.json) confirms all **234**
production/test/platformio inputs unchanged, all **361** primary shared/snapshot inputs identical after the one
instrument repair, the five protected QA preparation documents unchanged, brief identity intact, simulator clean
at its pin, both whitespace checks passing and no staged paths. Only the S45 tool, receipt/evidence and maintained
register changed in this resume. No staging or commit in either shared repository.

No native suite, simulator/corpus, board build, ABI probe, full mutation union or full slice chain is claimed for
this resume. Earlier §8 measurements remain historical. B414's software repair still awaits independent QA;
B408 remains open. **Next: QA explicitly folds B416 into the matcher/handoff contract and re-pins; coder resumes
the preserved implementation and runs the whole chain before freeze.** R-RA-46..49 remain unchanged.

## 10. Revision-6 resume — preflight PASS

PIN re-synced? YES

Base `c07b77f50a16342d532618f760aad49e10208a5d`; clean simulator
`6585649ea5a780f0542b2931853a667be56a5b2b`; frozen revision-6 brief
`875c3160f983baf7a66944a38c92af80b565431b96b9ee548434dff3021284bb`.
All 361 production/test/tool inputs match the preserved revision-5 checkpoint, including the B414 repair.
The 3,036 admission paths include untracked implementation and prior evidence. Five QA preparation documents
changed (brief, register, design, tracker, MEMORY); the ledger is unchanged and remains inventoried.

Source validation confirms the existing public const `Node::id_bind_find_by_hash`, the target-hash carrier in
`RemoteClientRoute`, same-layer/XL ACK push identity and delegated timeout `dst=0`, and the exact authoritative
branch within `send_by_hash`. Revision 6 explicitly supplies the call-scoped callback/context seam and permits
the `via_home` conjunct; B416 and B413 no longer leave an anchor/fence contradiction. R-RA-46..49 unchanged.
No owner ruling or commit is awaited. The brief and QA preparation documents remain frozen.

Implementation adds no resident observation state: the lookup, context, output hash and delivery remain call
scoped. The core matcher uses producer identity before the firmware applies the same-layer binding veto.
The production sender edit is exactly its trailing parameter and the one guard conjunct, with the controller
opting in. Final proof counts and complete frozen inventories follow in the handoff after the fresh §9 gate.

## 11. Revision-6 frozen handoff — coder gate complete

PIN re-synced? YES

Base **`c07b77f50a16342d532618f760aad49e10208a5d`** plus the complete uncommitted candidate;
simulator **`6585649ea5a780f0542b2931853a667be56a5b2b`**, clean. Frozen revision-6 brief SHA-256:
**`875c3160f983baf7a66944a38c92af80b565431b96b9ee548434dff3021284bb`**. No commit is a prerequisite.
Frozen primary-inventory SHA-256: **`7293f6587e94424d2328234f84958655627a52caebaf1d343ec613b8e26faa4e`**.
R-RA-46..49 are unchanged. The brief was not edited. The QA preparation set was inventoried at admission;
only the register subsequently changed, for the coder's B418/B419 findings and B419 closure.

**Disposition:** implemented and frozen for the owner's independent gate. This is the coder's measurement,
not independent QA PASS. The required remote-admin chain passes with the existing B342 unusable-control
carve-out. The additional board-UI run is reported separately, including its nonzero exit; no instrument failure
is hidden by the summary. No on-metal result is claimed.

### Implementation and proof boundaries

The preserved 8b carrier, execute-only ACK eligibility, resend/cadence changes, delivery formats and B414
repair are retained. Revision 6 completes B413/B416: `send_by_hash` gains only the trailing defaulted `via_home`
argument and the single `!via_home` conjunct; the controller opts in, while all prior callers keep the default.
A mechanical comparison against `c07b77f` verifies that exact two-line implementation fence.

ACK matching now requires the producer-specific identity: delegated timeout `dst=0`, same-layer ACK with
zero sender hash, or cross-layer ACK with the target hash. The pure matcher returns the candidate target hash;
the firmware applies the same-layer-only veto through `remote_client_bind_lookup` and the call-scoped Node
context. No callback, sink, queue or Node pointer is stored. The board keeps one whole-push call after BLE
fanout. Custody still uses the existing codec and remains diagnostic only.

The new native cases exercise known (received owner beacon), unknown and claimed bindings with two live
request rows, actual wrapper TX to the home, distinct counters, and the real 60-s/300-s deadline service.
Those carrier-only cases label their pending/request bytes and ACK/custody pushes as synthetic; they do not
claim target authentication. The existing real M1→home→target→M1 cases independently cover authentication,
execution, all prior terminal meanings, E2E ACK, rollover and replay. The real custody transport proof preserves
the push, inbox record and existing output while observing the translated record.

B416 is executed with real Node binding tables in native tests and, separately, through the actual production
callback and observer in the CLIENT firmware probe. ID 2 accepts, ID 3 vetoes, no binding accepts; lookup is
not invoked on timeout/XL ACK. Missing lookup/context wiring and removal of the veto are controlled. The
B415 equal-counter case remains explicitly a characterization: a queued direct operator DM and a pending
wrapper, with a labelled ACK injection through a real target transmitter, home and mobile receiver. It proves
the pre-existing wildcard ambiguity, not delivery of that queued operator DM, and introduces no B415 fix.

### Fresh required instruments

All results below were executed on this candidate, not inherited from revision 4 or 5. The gate snapshot and
the separate mutation input copy contain all tracked and untracked working inputs, not HEAD alone.

| Instrument | Reproduced result |
| --- | --- |
| Native wrapper, then executable | **2970 cases / 195942 assertions / 0 failed / 0 skipped** |
| Extended independent reference | **94/94** strict; old **89/89** identical; **5** comparator controls RED |
| Simulator | Stock unchanged simulator; both variants freshly compile `remote_client.cpp`, `node_mac_rx.cpp`, `node_mac.cpp`, and `node_hashlocate.cpp` |
| Corpus | **36/36 anchored and byte-identical**, also compared against a separately rebuilt **fresh c07b77f base corpus**; s18 **`32afbf11e43b4bf9d0bd470ad502ba0a`**, 269517 events |
| Custody corpus surface | All eleven s07/s22 custody-related rows byte-identical: 5 `custody_notice_tx`, 4 `deleg_ack_expired`, 1 `custody_failure_rx`, 1 custody push; no such rows in s22 |
| ABI | **290 checks / 9 RED**; Node **235248 native / 122176 mobile / 157344 gateway**, unchanged; client state **4512** on all three ABIs; B278 **42 measurements / 6 RED** |
| Console sink | **720** checks, structural **83**, BLE guard **905**, **149** controls, 0 unusable; default and `--no-neg` pass; B414 repaired |
| Inbox verbs | ACCEPT **1374 / 60 RED**; CLIENT **457 / 68 RED**; both modes pass, plus explicit CLIENT default run |
| Firmware UI | **223 RED / 0 unusable**, coverage **703/840**; default and `--no-neg` pass |
| Custody USB / observer structure | **27 / 10 RED**, plus **4 structural checks / 6 RED**; both modes pass |
| BLE line | **55 / 12 RED**; both modes pass |
| Features | **9 cells / 121 checks / 62 RED**, ownership **43 RED**; both modes pass |
| Deferred actions | P1 **150/151/158/158**; remote **416/518/534/464**; radio **3160/3485/3689/3695**; **40 controls RED**; both modes pass |
| Tools discovery | **351 passed / 0 failed / 0 skipped**; actual simulator path and fresh measured ELF supplied |
| Inventory / authority / A0 / literals | Write, bare, check pass; **208 semantic rows unchanged**, only file:line anchors move; authority selftests **6 RED**; A0 and literal checks pass |
| Warning census | All six environments at **173/178/177/177/182/182**, zero `-Wswitch`; every RAM value unchanged |
| Board pair | Gateway **204036 RAM / 575008 flash**, **0 / +112 B** versus base, 288 objects; mobile **211772 / 1395276**, **0 / +2444 B**, 332 objects |
| One-off xiao_mobile | **176604 RAM / 700588 flash**, successful build |
| Mutation union | **61 batteries / 983 RED / 1 known unusable B342 / 984 configured / 0 vacuous**, complete fresh run |
| Whitespace / preservation | Both repos clean under `git diff --check`; simulator clean; no staging; all **361** primary inputs identical across shared, gate and mutation copies |

The gate's six standing probes are the established console, inbox, firmware UI, custody, BLE-line and features
set (also enumerated in the 8ac independent QA receipt); deferred-actions is additional. The extra board-UI
run below is not substituted for any of them.

The mutation selectors are derived: **S=16**, **H=59** (historical floor 918 configured), **S∪H=61**.
`S−H` is `radmin8brx` and `radmin8node`. Final extended batteries are `radmin8client` **57/57 RED**,
`radmin8verbs` **27/27**, `radmin8brx` **18/18**, and `radmin8node` **1/1**. Every pattern matches once.
The three repaired controls were executed in this fresh union: **C39: 5 failed assertions; C43: 3; X09: 2**.
B413's `via_home=false` control **X18 fails 17 assertions**. B416 controls V23..V27 fail
**1/6/16/5/1** assertions. The sole known unusable entry is `sliceBmac` M04/B342: it compiles and survives,
as already characterized at the base; its battery exits 1 and is not counted RED.

Flash accounting is recorded by section and normalized symbol inventory. Gateway `.text` grows **112 B**
(`send_by_hash` +40, admission ACK +56, three affected callers +4 each = +108 named function bytes, with
4 remaining section bytes not assigned to a function). Mobile grows **2284 B `.flash.text` +160 B
`.flash.rodata`**; normalized function sizes account for +2200 B, with 84 text-section bytes outside that sum.
These residual section bytes are disclosed, not claimed as individually proven instruction attribution.
Static data sections and linked RAM are unchanged. Retained native/pair/xiao logs contain no Node reorder
warnings; the three nRF52 `CustomLFS` reorder diagnostics match the archived base and are not new. Base ELF/manifests are the preserved admission measurement;
the candidate board pair and all census builds are fresh.

### Failed and supplemental runs, preserved rather than erased

**B418, open:** the additional `probe_board_ui` reports W49/W51/W54 failures, wiring **57/60**,
structural **23/23**, with **176** wiring controls RED. A fresh base `--no-neg` run reproduces the same
three failures. It is pre-existing source-reader drift outside the 8b fence and the established six-probe chain.
No production or board-UI checker change was made. The aggregate chain driver exits 1 solely because that
supplemental invocation was included; every required instrument's recorded exit is 0.

**B419, closed:** the first union attempt encountered `rsync` exit 24 while its source snapshot also hosted
active board build artifacts. The entire union was stopped and restarted from an immutable input-only copy;
no completed-battery count was inherited. That final full run has only B342's expected nonzero exit. Both
attempts and their driver exits are archived. Earlier native compile/fixture iteration failures are likewise
retained in `iterations/`; the corrected final build and complete executable passed.

### Frozen artifacts and QA handoff

Evidence directory: [`2026-09-18-radmin-slice8b-r6-implementation`](2026-09-18-radmin-slice8b-r6-implementation/).
`frozen-primary-inputs.json` pins all production/test/tool inputs; `inputs.json` and
`final-gate-inputs.json` inventory the complete admission and gate copies, including untracked implementation.
`preparation-inputs.json` pins the admitted QA documents; `freeze-inputs.json` records the current handoff tree,
excluding only this evidence directory to avoid self-reference. `artifact-index.json` hashes every archived
artifact except itself. `candidate.patch` and `implementation-overlay.tar.gz` provide the reviewable dirty
implementation and preparation inputs; the archive is labelled as an overlay, not a standalone repository.

The complete gate copy remains `/tmp/mr-s8b-r6-nwy4hz9d/final-gate`; the independent mutation input copy is
`/tmp/mr-s8b-r6-nwy4hz9d/union-inputs`. Fresh native binary, both board ELFs, simulator binaries and full base/final
corpus streams remain under that same root, with manifests/hashes archived here. Executed commands and exits,
all 61 battery logs, interrupted logs, board section/symbol reports, corpus comparison, action-probe control logs,
and final preservation checks are included.

**Handoff:** QA reruns the required chain independently on these frozen inputs and decides PASS/HOLD. B408 and
B414 implementation closure, the author documentation/bench landing and all metal results remain QA/owner work.
The coder has not staged, committed, changed the frozen brief, edited the simulator or claimed a metal PASS.
