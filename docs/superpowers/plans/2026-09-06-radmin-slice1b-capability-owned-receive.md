<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 1b — capability-owned pre-tail receive · dispatch brief · 2026-09-06

**Status: QUALITY-AGENT PASS 2026-09-06 — AUTHORIZED FOR DISPATCH.** No fold-ins.
Dispatch model: **Opus** (`model: opus`).

Authority: design §14, §19 item 1b and §19.1 row 1b in
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`; R-RA-8/17/19/26/27 in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`; the Quality-Agent pre-check
`docs/superpowers/plans/2026-09-05-radmin-slice1b-precheck.md`, with §4 ruled and its legacy-widening
recommendation superseded; pass-2 S3 in `docs/superpowers/plans/2026-09-05-fable-review-pass2.md`.

**Dispatch base is exactly `0da4d56d3c29d06dc8e16f592003f4f83c744824` (`0da4d56`, `0b prep`).** The owner
committed the preparation package: register, bench suspension, design, tracker, MEMORY and this brief. The tree
was clean before this Author status/base pin; the preparation commit changes documentation only.
Source facts below were checked at `5d2c00e17d13002a5f7eae076b9120b4ef7fc9f9` (`prep 2`), which contains
Slice 1's QA-passed implementation and R-RA-27; this source-state citation is NOT the dispatch base.
At dispatch, QA and any isolated-worktree coder compare `git rev-parse HEAD` to the exact pinned base.
Only the explicitly recorded Author status/base-pin edit may pre-exist uncommitted. A mismatch or any other
unexplained dirty start is a STOP; do not repin, repair or commit the checkout.

This is the **R-RA-27 receive-ownership behaviour change**, with only its necessary staging extraction (C1).
It is not a wholly behaviour-neutral refactor. No v2 execution, codec, storage or command surface is introduced.

## Authority quoted verbatim

From R-RA-27, settled item 1:

> 1. **Strict role gates, no legacy widening.** `REMOTE_CMD` staging is compiled iff `MR_FEAT_RADMIN_ACCEPT`; `REMOTE_RESP`
>    staging iff `MR_FEAT_RADMIN_CLIENT`. `MR_FEAT_REMOTE_MGMT` does NOT widen either owner. Consequence, accepted by the
>    owner (M3: MeshRoute is not deployed): on a static/gateway build (accept-only) a `REMOTE_RESP` is UNOWNED and falls
>    to the fail-closed guard — the legacy USB `rcmd` issuer on static nodes no longer receives responses (its `send`
>    half still airs a `REMOTE_CMD` until Slice 9 deletes the legacy issuer/acceptor). The design's 1b sentence "under
>    the legacy gate their bodies remain behaviour-identical" is SUPERSEDED for that case: the bodies stay byte-identical
>    where an owner exists; ownership itself follows R-RA-8 strictly. ⇒ the bench script's legacy `rcmd` round-trip parts
>    on static nodes are suspended (Author marks them) until Slice 9's replacement; no new metal for 1b.

From R-RA-27, settled items 2 and 3:

> 2. **The four native role-by-type arms are driven by a PURE routing decision** that takes the two capability values as
>    arguments (production calls it with the real macros; the tests with all four combinations). No runtime role gate
>    in production paths, no test-only macro override.
> 3. **A mobile (client-only) simply ignores an incoming `REMOTE_CMD`:** it is unowned, reaches the fail-closed guard and
>    is dropped with the one scalar `unsupported_internal` emit — no staging into the inert stub any more. Externally
>    identical (no response either way); not corpus-visible (the host compiles both roles); measured on `heltec_mobile`.

From the agent-roles handoff protocol, step 4:

>    starting work in an isolated worktree, the dispatched agent verifies and records `git rev-parse HEAD` against
>    the brief's named base commit; a mismatch is a STOP to the dispatcher, never a stale-tree measurement or a
>    silent worktree repair.

R-RA-27 also supersedes the pre-check's §0 neutrality label, §1 legacy-widening/board-effect hypotheses, §2
widened expressions and §3 static `rcmd` round-trip requirement, not just its explicitly withdrawn paragraph.
R-RA-19 and pass-2 S3 still require pre-tail owned entry points and unchanged owned staging semantics. Their
earlier neutrality wording is not permission to retain disabled owners. No owner decision remains open.
Likewise S3's earlier instruction to gate the accept-side slot in 1b is superseded by the explicit R-RA-27
RAM/slot boundary: 1b retains the unconditional slot; Slice 5 owns its replacement.

## Verified starting state (V1/V2)

| Fact at `5d2c00e`; re-derive before editing | Source |
| --- | --- |
| The current shared, ungated CMD/RESP arm is in `Node::do_post_ack`, before E2E_ACK, DST_HASH misdelivery handling and sealed/open processing. It stages then frees/returns, or emits drop-full then frees/returns. | `lib/core/node_mac_rx.cpp:2220`–`:2238` |
| `REMOTE_CMD=0xA0` and `REMOTE_RESP=0xA1` are known protocol-internal types. The existing final internal guard emits only type/origin/dst/ctr, frees and returns before ordinary delivery. | `lib/core/frame_codec.h:802`–`:803`, `:913`–`:916`; `lib/core/node_mac_rx.cpp:2427`–`:2432` |
| `RemoteInbound` has active/is_response, 8-bit origin/length and the bounded body. Its instance is unconditional; draining copies it and clears active. | `lib/core/node.h:154`–`:163`, `:2905`; `lib/core/node_mac.cpp:874`–`:879` |
| The main loop prints staged responses or calls the legacy executor for staged commands. Mobile's executor is inert. None of these firmware paths is edited here. | `src/fw_main.cpp:1670`–`:1695`; `src/firmware_remote.cpp:91`, `:159` |
| The RADMIN derivation is mobile client-only, Arduino non-mobile accept-only, otherwise both. Board-only diagnostics enforce exclusivity and ACCEPT equal to the legacy switch. Native and both real lus variants compile both endpoints. | `lib/core/mr_features.h:54`–`:64`, `:92`–`:105`; feature probe's source-derived environment map |
| Existing native tests drive real RTS/DATA/post-ACK CMD and RESP staging, drop-full and drain; other typed/internal and relay tests already compile this receiver TU. | `test/test_node_r3.cpp:6405` onward; `test/test_data_type_audit_a0.cpp`, `test/test_data_type_namespace.cpp`, `test/test_custody_relay_f.cpp`, `test/test_dual_layer.cpp` |
| The feature probe intentionally pins zero consumers in two places; its wrapper explicitly says the first consumer slice must update the pin. The header also says no consumer yet. | `tools/probe_features/run.sh:189`–`:215`; `tools/test_probe_features.py:20`–`:22`, `:99`–`:107`; `lib/core/mr_features.h:53` |

Starting measurements belong to Slice 1 evidence `docs/superpowers/evidence/2026-09-05-radmin-slice1.md`,
independently reproduced by QA: native 2610/110269/0; feature probe 9 cells/97 checks/19 controls RED;
tools sweep 305; Node native 222072 / heltec_mobile Xtensa 117912 / gateway ARM 148680. They are comparison
references, not figures to copy into a report. Read the current keystone and all stream anchors from
`simulation/BASELINE.md`; preserve fresh base captures for native, corpus, ABI, boards and probes.

## Required production shape

Use one pure, state-free routing decision with the logical signature
`radmin_rx_owner(type, client_on, accept_on) -> none | command_accept | response_client` (names follow the
surrounding idiom). It lives with the receiver implementation; a small declaration/value type in `node.h`
allows native to call that same production decision. No parallel test model, stored capability, virtual
dispatch, runtime configuration, test-only macro override or new header is needed. Production supplies
`MR_FEAT_RADMIN_CLIENT` and `MR_FEAT_RADMIN_ACCEPT` directly, in the declared argument order.

| Capability arguments `{client, accept}` | CMD owner | RESP owner | Reachability |
| --- | --- | --- | --- |
| `{0,0}` | none | none | Synthetic pure-decision test only; forbidden board configuration |
| `{0,1}` | command_accept | none | Static/gateway product |
| `{1,0}` | none | response_client | Mobile product |
| `{1,1}` | command_accept | response_client | Native/lus; forbidden board configuration |

Other DataTypes select none; this decision must not steal another handler's type. Test all eight remote
decisions plus non-remote controls. The four product-role-by-type arms are included, not substituted by
testing only the both-on host. Synthetic capability arguments do not change a Node's role at runtime.

Introduce explicit accept-owned command and client-owned response entry points. Compile each declaration,
definition and consuming call only under its respective new capability. A common staging helper is allowed
and preferred over duplicating carrier construction (U1/U2). Do not duplicate the existing staging slot.
The none decision consumes nothing: no slot write, early return, new drop branch or telemetry there; it leaves
the existing chain to reach its existing internal guard. Do not move that guard ahead of any forwarding role
or other established handler. No ordinary inbox delivery, `msg_recv` or generic send-lifecycle result may
replace remote consumption/refusal.

Owned staging preserves the existing data path: parsed `ui->body` versus raw `inner + 1` fallback, empty-body
handling, the existing length cast/clamp, drop-full emit, active/is_response/from/len/body writes, 8-bit
`pa.origin`, no overwriting a full slot, drain behaviour, and the existing free/return outcome. Keep it before
SEALED_RELAY/CRYPTED processing. This slice does not authenticate/open a v2 body or tighten the legacy clamp.
State the pass-2 obligations adjacent to the new seam as **missing/deferred by design**: mandatory SOURCE_HASH,
32-bit source identity, refuse-not-clamp and later role-owned storage. Slices 5/7b and 8b supply semantics;
Slice 9 removes legacy bodies without deleting the new ownership boundary.

The ignored-mobile-command and static/gateway-unowned-response outcomes are intentional. The latter breaks
legacy `rcmd` round trips now; do not preserve them via `|| MR_FEAT_REMOTE_MGMT`, a fallback owner or firmware
edits. The existing guard emits one scalar `unsupported_internal` per reached occurrence in telemetry builds;
device builds strip `MR_EMIT`. Do not promise a new console line on metal or add counters/rate-limit state.
Use valid addressed plaintext carriers to demonstrate the endpoint policy. Malformed, misdirected or sealed
carriers may exit at an earlier existing forwarding/open/refusal branch; preserve that ordering and report
fixture reachability rather than claiming every crafted remote frame necessarily reaches the tail.

**B307 adjacent comment correction:** the guard rationale at `node_mac_rx.cpp:2417` still says ten board
environments. The Author's read-only resolved PlatformIO census finds thirteen (4 mobile + 4 gateway + 5 static).
Correct this sentence to all board environments without a fragile count, keeping the former number visibly
withdrawn; isolate and prove its comment-only token identity. Do not change the guard, telemetry or matrix.

## Wiring proof and controls

These are complementary instruments, not interchangeable claims:

1. **Native compiles and executes the production receiver TU.** Extend the existing native TU(s), preferably
   `test_node_r3.cpp`, using the established real RTS → DATA → post-ACK drive. Prove both owned remote types
   stage the exact bytes, origin and response marker; full-slot preservation, drain, empty/body-boundary cases,
   no ordinary DM delivery/push, and the correct free/return outcome. Keep all existing cases green.
2. **The same native binary calls the production pure decision** with the matrix above. Record these as
   synthetic disabled-role decisions. That binary has both consumers compiled in: a none result is not itself
   an executed remote guard drop. Execute the existing real generic internal-guard fixtures as the separate
   proof of its scalar-only drop, no staging/inbox/push, and placement after forwarding. Do not add a test-only
   route from synthetic capabilities into the production RX function.
3. **Product ownership is compiled and checked.** Capture the actual ruled boards' resolved flags and
   preprocessed production receiver/declarations using their build compiler commands. Demonstrate CMD body/call
   absent on heltec_mobile, RESP absent on gateway, and the opposite owner present. Include symbol/disassembly
   evidence as available; missing symbols alone are not proof under inlining/LTO. Verify the real caller uses
   the pure decision with the correct two macros, with no runtime capability load or legacy widening.
4. **Extend the standing feature gate's consumer contract**, as specified below. Its controlled source checks
   bind the native decision tests to the actual production call/guards and the exact consumer census. A
   correctly tested helper which the real router bypasses must fail. A source check alone is not an RX drive;
   the native and product compilation evidence above remains mandatory.

Add measured receiver mutations to the existing `a0rx` and/or `sliceBrx` target(s), not a new cross-file target:
attack each pure decision term/type/direction, each real owner call, the response marker, staging preservation
and the consuming return. Reuse the existing guard-deletion/predicate/placement/scalar controls without
weakening them. Give each new assertion/claimed branch a named control or an explicit mapping to an existing
one. Mutants must compile and fail the intended native assertion; compilation failure is UNUSABLE here.
If a disabled-role wiring defect cannot be distinguished in both-on native (e.g. swapping macro arguments),
put its control in the feature ownership instrument, not a falsely labelled native RED.

### First-consumer transition of the feature probe

This is a narrow Author fence addition to the QA pre-check, required by its now-live first-consumer pin:
`run.sh` S3 and `test_the_pair_has_no_consumer` would otherwise reject the requested production change.
Replace zero-consumer with a literal, reviewed ownership/call-site contract, not removal of the census.
Separate production paths (`mr_features.h` definitions; `node_mac_rx.cpp` consumers; `node.h` guarded declarations
if used) from named native-test references. Scan all `lib/`, `src/`, `test/`; reject an extra file or extra
unapproved role-consuming site even within an allowed file. Tests must not become production owners.

Use a small **`tools/probe_features/ownership.py`** support instrument, called by the default existing runner
and exercised by `tools/test_probe_features.py`, for this contract and its isolated controls. It checks the
actual production sources, not a copied policy model. Require positive ownership/wiring checks and negative
controls for an unknown consumer, deleted/bypassed real decision call, swapped macro arguments, each wrong
owner guard, each legacy-widened gate, and an early consume/return for none. Each isolated edit has exactly one
verified match; report the named check that rejected it. Preserve source hashes for every input before/after.
No instrumentation mutates the shared checkout. A malformed/unreadable source or failed tool is a gate error,
not a successful ownership control. If the chosen syntax cannot be checked honestly within this fence, STOP.

Preserve the existing nine-cell configuration matrix, old feature projections, three diagnostics and all
19 prior controls. **Before any controls run, keep the existing classification declaration:** compile error
is expected RED only for the named production-header refusal fixtures whose exact production diagnostic is
observed; missing/broken compiler, syntax error, unrelated diagnostic, signal or timeout never qualifies.
Executable mutations still need a built, executed failing assertion. Ownership controls need the named
policy-check rejection, never merely a nonzero tool exit. Preserve controls-of-controls and source-integrity
checks. Derive added check/control counts; reconcile runner and wrapper pins, and prove dropping an obligation
cannot leave PASS. Default runs all controls; `--no-neg` ends `PROBE-ONLY — NOT A GATE`, without a standalone
gate-PASS line from a child. Keep the instrument's claims precise about configuration versus RX reachability.

## Fence

Allowed coder changes:

- `lib/core/node_mac_rx.cpp`: only the ownership decision, required staging extraction/calls and adjacent
  done-versus-deferred/correction comments;
- `lib/core/node.h`: only the pure-decision value type/declaration and needed handler/helper declarations with
  their guards; no member data, layout, `RemoteInbound` or other API rewrite;
- `lib/core/mr_features.h`: **comment only**, withdrawing the Slice 1 `:53` no-consumer claim now that 1b
  consumes the pair. This is the other explicit pre-check fence addition (V1). Preserve every directive,
  feature value, diagnostic and dependency; prove comment-only token/directive identity;
- existing native TUs `test/test_node_r3.cpp` and, only for the existing guard/relay fixtures,
  `test/test_data_type_audit_a0.cpp`, `test/test_custody_relay_f.cpp`, `test/test_dual_layer.cpp`;
- `tools/probe_ui_model_mutations.py`: new controls under existing RX targets, necessary exact re-anchors,
  native PIN with per-case derivation; no runner/verdict redesign or silent retirement;
- `tools/probe_features/run.sh`, new `tools/probe_features/ownership.py`, and `tools/test_probe_features.py`:
  the first-consumer contract, its controls, source integrity and measured pins only;
- `tools/probe_features/probe_main.cpp`: only adjacent historical/no-consumer comment correction; its
  executable matrix and expected-value source stay unchanged; and
- `docs/superpowers/evidence/2026-09-06-radmin-slice1b.md`.

Expected diff: one semantic production TU, declarations in its existing header, one comment-only feature
header; bounded additions to existing tests and two existing gate families; one new support script and one
evidence file. No new production file or move, `src/`, other core TU, frame/codec/constants, simulator,
`platformio.ini`, variant, NV/wire/timer/state/capacity, anchor table, command inventory or companion change.
No edits to QA ledgers, rulings, register, bench, design, tracker or MEMORY by the coder; those are Author
landings after PASS. In particular B304's ini residue and B306's `node_mac.cpp` citation remain outside scope.
Source-derived scope/pin adjustments above are for QA to gate, not permission for a coder to broaden them.

## Prediction and measurements

Before editing, record a per-stream census of REMOTE_CMD/RESP arrivals and whether they reach the existing
staging arm, using the established throwaway census idiom. Instrument only an isolated disposable copy;
retain its patch/commands/output in evidence, never in the delivered production diff or anchored stream run.
Zero occurrences are a measured reachability result, not missing evidence or a reason to skip the corpus.
Predict **36/36 byte-identical streams**: real native/lus builds have both owners and retain their staging.
Then rebuild lus through the changed core dependency, record actions and before/after binary hashes, and
compare the full corpus to the exact base and current `BASELINE.md` anchors. Binary hash may move in 1b;
stream identity may not. No simulator source edit or re-anchor is authorized.

Both `gateway` and `heltec_mobile` must have **RAM delta zero**, with unchanged Node/RemoteInbound layout on
native, Xtensa and ARM. `_remote_inbound` stays until Slice 5. Unlike Slice 1, flash/sections/symbols may move
on both boards: gateway loses RESP ownership, heltec_mobile loses CMD ownership, and the extraction may affect
inlining. Measure and attribute actual byte/section/object/symbol changes; an identical total does not alone
disprove compile-out, nor does a smaller total prove it. Do not copy the pre-check's superseded widened-gate
prediction or invent a numeric flash allowance. Compare deterministic base/final captures under fixed build
identity and same checkout/build paths; respect B254/B262 source/debug metadata versus executable differences.

## Mutation selectors and full gate

Report both selectors independently, with source/acceptance reasons and exclusions:

- **Changed-source:** current `TARGET_SRC` maps the receiver to `b161rx b251rx b159rx a0rx sliceBrx sliceGrx`.
  Derive again from the actual final production diff, including header dependencies where applicable.
- **Historical/dependency:** at least `a0rx`/`sliceBrx` for the staging/guard/forwarder boundary and
  `sliceAcodec` for the internal-range/trait authority on which the unowned drop relies. Derive any additional
  acceptance dependencies; do not infer that this set equals the changed-source set. The new feature probe
  separately gates profile/compile ownership. An untouched unrelated TX battery is not included merely for
  sharing an arc name; any inclusion/exclusion needs the actual dependency.

Gate the union in full, not just new controls. Current named union is the six RX targets plus `sliceAcodec`;
add any discovered applicable dependency. Existing unrelated decay rows are not waivers for this union.
Do not edit out-of-fence production to rescue a control. Native PIN changes only from independently measured
case/assertion additions; include the exact report line `PIN re-synced? YES — <derivation>`.

Preserve fresh base measurements, then run final gates sequentially, using only each instrument's supported
internal parallelism. Firmware board gate: **exactly gateway + heltec_mobile**, with the warning census's own
derived pinned environment set as the sole exception (agent roles supersede the generic all-board wording).

1. Base/fence and source census, prediction ledger, native `pio test -e native` **then run
   `./.pio/build/native/program`**; record the real cases/assertions/0 failures and per-case PIN derivation.
2. Full receiver/dependency mutation union, with per-control classification and restored source proof;
   complete feature matrix + new ownership gate with all controls, plus the product preprocessing evidence.
3. Forced lus rebuild and `tools/run_corpus.py --jobs=8 --require-anchors`; all 36 base streams/anchors match;
   report the current s18 keystone and semantic totals explicitly.
4. Full `tools/probe_board_abi.py` and `tools/probe_b278_row_abi.py` with controls. Run the deterministic
   `tools/measure_board.py pair --jobs=2` base/final workflow for the ruled pair, with provenance and RAM/flash
   attribution; Node assert and `-Wreorder` remain clean. No raw ordinary-PIO size line substitutes for A/B.
5. Default complete `probe_console_sink`, `probe_inbox_verbs`, `probe_firmware_ui`, `probe_custody_usb` and
   `probe_ble_line` gates; all existing pins remain exact. New feature ownership checks do not alter legacy
   command profile projections or fix unrelated instrument debt.
6. `python3 -m unittest discover -s tools -p 'test_*.py'`, including the revised feature wrapper; account for
   every added/changed test and control. Run both bare `python3 tools/gen_command_inventory.py` and its
   `--check` form. No command anchor moves, so no regeneration/`--write` is authorized here.
7. `tools/warning_census.sh`, `tools/check_a0_matrix.py` and `tools/check_data_type_literals.py` with their
   supported controls; source restoration, `git diff --check`, full modified/untracked inventory. No commit.

## STOP conditions

Stop and report to QA before proceeding or widening the fence if:

1. the committed dispatch base is unpinned, missing or different, or the starting tree has unexplained edits;
2. implementation requires legacy-widened owners, runtime role state, test-only role overrides, firmware or
   codec changes, moved forwarding/guard ordering, or a v2 body/security/storage behaviour;
3. an owned staging body changes semantics beyond the ruled ownership split, or the existing slot, any Node
   layout/ABI pin, RAM, wire/NV/timer/capacity or feature definition/diagnostic changes;
4. any corpus stream/anchor changes, the fresh simulator dependency rebuild cannot be demonstrated, or a
   board flash/section/object/symbol movement cannot be attributed to the fenced changes;
5. disabled-role proof requires claiming a synthetic pure decision as an executed native remote RX drop, or
   actual production ownership cannot be tied to both the tested decision and board-compiled arms;
6. any required control is green/vacuous/multi-matched/unusable, a refusal is misclassified, source integrity
   fails, an existing pin drifts without a measured in-fence derivation, or a gate requires an out-of-fence edit;
7. an unexplained warning, concurrent change, unaccounted modified/untracked file, inventory regeneration
   requirement or new owner choice remains.

## Evidence and Author handoff

Write `docs/superpowers/evidence/2026-09-06-radmin-slice1b.md`: exact base/dirty-start check; source-derived
strict gates; before census and prediction; owned-body comparison and deferred seam obligations; native
production routing versus synthetic decision matrix; real board preprocessing/compile-out proof; feature
ownership checks and controls-of-controls; both mutation selectors and full union; every fresh instrument
count/output; PIN derivation; corpus, ABI, deterministic board deltas and warning/checker results; comment-only
header/driver/B307 proof; source restoration; STOP audit; complete file inventory (including new support script and
executable modes); findings with measurements and proposed register rows. QA independently reruns every gate.

**Metal residue: none, as ruled by R-RA-27.** Bench §9.9's static/gateway `rcmd` round-trip portion is already
marked suspended effective from 1b until Slice 9; its local supplied-sink checks remain active. After QA PASS,
the Author lands design §19/§19.1 1b software-complete, the detailed manual's legacy issuer/reply limitation,
MEMORY/tracker status and any new register findings. Keep synthetic/native/board reachability distinctions in
that landing. No further owner ruling is requested. Leave implementation and evidence uncommitted (D4).
