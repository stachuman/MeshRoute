<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B278 S1a — behavior-neutral delegated-correlation refactor · dispatch brief · 2026-09-02

**Status: DRAFT — awaiting Quality-Agent review; no dispatch is authorized by this draft.**
Dispatch model after PASS: **Opus**. Authority:
`docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md`, especially
§1.1 items 4–6, §4.1–§4.5, §10 and §12-S1a. Pre-check input:
`docs/superpowers/plans/2026-09-02-b278-s1a-precheck.md`.

⛔ **C1 is the governing fence: this slice is a refactor, not the B278 behavior change.**
The present same-mobile-scoped activation decision remains exact. The stronger cross-mobile
refusal and the B251 test flip belong to S1b. S1a adds no custody eligibility, report lookup,
wire form, receiver, telemetry or changed admission result.

## Contract quoted verbatim from the reviewed spec

> ### S1a — behavior-neutral correlation refactor
>
> 1. Generalize the one ring and preserve target + return identities.
> 2. Add outward type and custody lifecycle state without changing which flights reserve;
>    retain one timestamp and the one named 300 s row TTL—no per-obligation clocks.
> 3. Preserve the current same-mobile-scoped activation-uniqueness decision byte-for-byte;
>    the stronger cross-mobile refusal and its B251 test flip belong to S1b.
> 4. Route every current reserve/activate/release/translate site through the renamed authority.
> 5. Prove all 36 corpus streams byte-identical, current E2E ACK wire behavior exact, and
>    measure the row/`Node` RAM movement independently of the admission change. Byte identity
>    includes keeping every existing telemetry event name and field verbatim:
>    `deleg_ack_reserved`, `deleg_ack_put`, `deleg_ack_put_refused`, and
>    `mobile_ctr_admission_refused`.
>
> No custody admission, wire or receiver behavior changes in S1a.

The slice-boundary correction in §4.3 is also load-bearing:

> **Slice-boundary correction 2026-09-02:** S1a preserves
> that same-mobile-scoped decision byte-for-byte; S1b owns the stronger cross-mobile refusal
> and must flip the existing B251 test which currently admits both rows while proving only ACK
> lookup isolation. Putting the flip in the behavior-neutral refactor would violate C1, and
> the corpus cannot expose the mistake because home counters are per destination.

## STOP conditions

STOP and report to the Quality Agent—do not repair around the finding—if any of these occurs:

1. any current reserve, refresh, refusal, activation, release, translate, prune or telemetry
   decision must change to complete the layout refactor;
2. the current cross-mobile B251 case no longer admits both rows, or the current same-mobile
   ambiguity refusal weakens;
3. any canonical corpus byte, event, event order, count, delivery, failure or anchor moves;
4. the production row is not the reviewed 32-byte shape on host, ARM and Xtensa, or the real
   `Node` movement cannot be fully attributed to the one eight-row replacement;
5. a new field, helper or renamed authority would require a wire-version, NV, timer-capacity,
   `src/`, UI or protocol behavior change; or
6. an existing B251 mutation cannot be re-anchored to one live source match and re-proven RED.

The current corpus has no delegated-flight/custody-report intersection. Its byte identity is
therefore a neutrality gate, not evidence that the refactor is correct. Native cases and the
ABI instruments own the positive proof.

## Scope fence

Allowed production edits are confined to the existing correlation authority in:

- `lib/core/node.h`;
- `lib/core/node_hashlocate.cpp`;
- the existing reserve/activate callers in `lib/core/node_mac_rx.cpp`; and
- `lib/core/protocol_constants.h` only for the reviewed named alias
  `delegated_custody_ttl_ms = e2e_ack_deadline_xl_ms`.

Allowed supporting edits are the directly affected tests and instruments under `test/` and
`tools/`, plus the one evidence file named below. Nothing under `src/`, `variants/`,
`platformio.ini`, `docs/frames.md`, `docs/protocol.md`, the bench script or
`simulation/BASELINE.md` may change. No simulator scenario or anchor-table edit. No device
contact. No `git add`, commit, checkout, worktree cleanup or destructive command. Preserve
all unrelated and all S0 working-tree files.

At report time, show the exact diff outside the allowed paths and prove it empty. A comment
or telemetry rename is still a change and is not exempt from the fence.

## S1a-1 — one 32-byte row, same ring

Refactor the existing private `Node::DelegAck` ring in place. Do not add a second correlation
container and do not move the ring within `Node`. Capacity remains eight. Preserve the public
test-facing names `DelegAckPeer`, `DelegAckState`, `kDelegAckNoSlot` and the seven
`deleg_ack_*` methods; aliases are preferable to a rename storm if an internal name changes.

The production row carries, in this order:

```text
uint64  ts_ms
uint32  mobile_hash
uint32  target
uint32  return_peer
uint16  ctr_h
uint16  ctr_m
uint8   layer
uint8   outward_type
uint8   target_kind
uint8   return_kind
uint8   state
uint8   obligations
```

The reviewed spec calls the last byte `custody_state` schematically and explicitly says row
names are implementation choices. The pre-check and S0 ABI authority call it `obligations`;
use that spelling so the production row and measured probe share one vocabulary. This byte
does **not** mean two clocks: S1a sets only the E2E-ACK obligation on every row, leaves all
custody/custody-forwarded bits clear, and retains one timestamp and one 300 s TTL. Do not add
an ACK clock, custody clock or custody-only row.

At reservation, write `target`/`target_kind` once and never overwrite them. At activation,
write `return_peer`/`return_kind`. The existing phase-dependent `peer` behavior must be
represented exactly by those two fields—no changed key, refusal or release decision.

The S0 prediction is a uniform eight-row delta with no surrounding alignment hole. Treat
that only as a hypothesis: independently derive `sizeof`, alignment, offsets, the containing
`Node` size, board RAM and symbols on all three ABIs. Update every production and probe pin
from measurement, with the arithmetic recorded.

## S1a-2 — preserve the complete current decision set

Route the current operations through the generalized row while retaining these decisions
byte-for-byte:

| Operation | Decision which must remain exact |
| --- | --- |
| reserve key | `{mobile_hash, ctr_m, target_kind, target, layer}` |
| exact reserved retry | refresh the same row in place |
| full ring | refuse with the same computed `retry_ms`; never evict |
| activation availability | today’s **same-mobile-scoped** `{mobile_hash, ctr_h, return_kind, return_peer, layer}` test |
| active refresh | same key and same `ctr_m`; refresh in place |
| direct put | same free-slot selection and no-live-row eviction |
| release | reserved rows only, using the existing reservation identity |
| translate | active rows only; exact current key; one-shot clear |
| prune | one timestamp, `age >= delegated_custody_ttl_ms`, whole-row clear |

Required native controls include both sides of every row above. In particular:

- the existing different-mobile/same-wire-key B251 case must continue accepting and
  translating both rows in S1a;
- the same-mobile ambiguity case must continue refusing;
- target identity must survive activation while return identity changes independently;
- `outward_type` must be observable as stored but must affect no current lookup or decision;
- the E2E-ACK obligation must be set on every row while every custody-related obligation bit
  remains clear through every S1a path;
- exact retry, active refresh, reserved release, one-shot translate, full-ring refusal and
  the 300 s boundary must remain exact; and
- non-E2E sends must continue allocating no row.

Add a focused mutation which implements S1b’s future cross-mobile refusal early; it must be
RED in S1a. That is the positive proof that the slice did not hide a behavior change behind
corpus invisibility.

## S1a-3 — populate `outward_type` from values already in scope

No new parameter may be threaded merely to discover type. Use the existing value at each
site and prove it reaches the row:

| Site | Type authority |
| --- | --- |
| direct hosted-mobile transit | `d.type` in `node_mac_rx.cpp` |
| same-layer wrapper commit lambda | `itype` in `node_hashlocate.cpp` |
| wrapper cross-layer branch | `etype` in `node_mac_rx.cpp` |
| `drain_parked_sends` cross-layer fire (synthetic/unreachable today) | `p.type` in `node_hashlocate.cpp` |
| `drain_parked_sends` same-layer fire | `p.type` in `node_hashlocate.cpp` |
| `drain_resolved_parked_sends` same-layer fire | `p.type` in `node_hashlocate.cpp` |

V1 every current call site before editing; do not trust the approximate pre-check lines after
the struct changes. The cross-layer park-fire site formerly near
`node_hashlocate.cpp:2259` must route through the generalized authority but is unreachable
from today’s producer because `park_send_layer` never stores `reply_to_hash`. Mark its proof
structural/synthetic; do not claim a production-shaped native case.

## S1a-4 — telemetry is frozen verbatim

Zero new events, zero deleted events, and zero changes to event names, field names, field
order or values. The frozen surface is:

- `deleg_ack_reserved{mobile_hash,ctr_m,target,layer}`;
- `deleg_ack_put{mobile_hash,ctr_h,ctr_m,peer,layer}`—the emitted field remains named
  `peer` even though the struct field becomes `return_peer`;
- `deleg_ack_put_refused{mobile_hash,ctr_h,ctr_m}`;
- `mobile_ctr_admission_refused{reason,mobile_hash,dst,ctr_m,retry_ms}`; and
- adjacent `deleg_originated`, `mobile_ctr_translated`, `mobile_ctr_commit_failed` and
  `mobile_reverse_ack` events.

F6’s release/expiry events and extra scalar fields belong to S1b. A telemetry convenience in
S1a is a behavior change and triggers STOP.

## S1a-5 — tests and instruments which must move with the refactor

1. Keep `DualLayerTestAccess` compiling against the ring and preserve its established public
   enum/method names. Extend the seam only enough to assert the new split identities, type and
   custody state; it must not become a second implementation.
2. Re-match and run all eight `MUTS_B251HASH` entries and the three `MUTS_B251RX` entries in
   `tools/probe_ui_model_mutations.py`. Match count must be exactly one for every live arm;
   run the complete touched targets, not selected mutations.
3. Re-point `tools/probe_b278_row_abi.py`: retire the 24-byte current-row control now that the
   candidate is production, bind the production 32-byte pin, and keep controls for omitted
   fields, reordered fields, compile failure and a missing target/toolchain. The instrument
   must bind the same `obligations` field sequence as production rather than retaining a
   separate candidate-row authority.
4. Run the complete `tools/probe_board_abi.py` sweep and update all three `Node` pins from
   measured layouts.
5. Re-synchronize every affected native/mutation PIN. The report must contain exactly:
   `PIN re-synced? YES — <derivation>`.

Every new instrument or test target must be auto-discovered or added to a standing gate,
have a controlled RED/sabotage proof, and appear in the final untracked inventory. A helper
which only the author remembers to invoke is not a gate.

## Required gates

1. **Native:** build the native environment and run the binary itself. Derive case/assertion
   totals and deltas from this run; zero failures. Include the decision matrix above and the
   premature-S1b mutation.
2. **Mutations:** full `b251hash` and `b251rx` targets plus every newly affected target; each
   applicable arm RED at match count one, zero silently unusable/vacuous entries. Derive the
   anchor population and re-sync it.
3. **Corpus:** rebuild `lus` and prove the recompile/relink control fires; run
   `tools/run_corpus.py --jobs=8 --require-anchors` against a frozen output. Require all 36
   streams byte-identical to the current owner-ruled baseline. Read the keystone from
   `simulation/BASELINE.md` during the run. **Any movement is STOP; there is no re-anchor
   proposal in S1a.**
4. **ABI:** full `tools/probe_board_abi.py` gate and the re-pointed B278 row ABI gate, with
   all negative controls. Derive row/member offsets and `Node` sizes on host, ARM and Xtensa.
5. **Boards:** exactly `gateway` and `heltec_mobile`, through
   `tools/measure_board.py pair --jobs=2` into a fresh gitignored `.pio-measure/` location.
   Compare with freshly validated S0 manifests; derive and symbol-attribute RAM/flash/object
   movement. No third board environment.
6. **Warnings:** full `tools/warning_census.sh` at its own pinned environment set. This is the
   sole exception to the two-board rule. Watch the B169 shape: locals used only by `MR_EMIT`
   disappear on feature-off builds.
7. **Tools/fence:** all touched tool tests and sabotage controls; `git diff --check`; exact
   `git status --short`; an explicit diff proving `src/`, `variants/`, `platformio.ini`, wire
   docs and `simulation/BASELINE.md` unchanged.

The coder derives every numeric baseline and result. Values in the pre-check and S0 evidence
are cross-checks, not figures to quote as this run’s measurements.

## Durable evidence and report shape

The one durable report is:

`docs/superpowers/evidence/2026-09-02-b278-s1a.md`

It must include:

- a before/after row table with every field, offset, size and ABI;
- the complete decision-preservation matrix with source anchors;
- every reserve/activate/put/release/translate site and its `outward_type` authority;
- the unreachable cross-layer park-fire disposition;
- frozen telemetry comparison;
- native and full mutation results, match counts and PIN derivation;
- corpus rebuild proof and byte-identity result;
- host/ARM/Xtensa `Node` sizes and board symbol attribution;
- warning/tool gates;
- every STOP evaluated explicitly;
- the exact line `PIN re-synced? YES — <derivation>`; and
- exact final `git status --short`, separating pre-existing files from this slice and naming
  every untracked file which needs an explicit add.

No owner ruling is expected if the measured result satisfies this brief. A size/layout
departure or any behavior/corpus movement is a STOP finding, not permission to re-scope S1a.
