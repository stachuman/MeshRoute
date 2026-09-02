<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B278 S3 — home correlation and translated-custody origination · dispatch brief · 2026-09-02

**Status: QUALITY-AGENT PASS — ready for Opus dispatch.**
Dispatch model after PASS: **Opus**. Authority:
`docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md`, especially
§4.4–§7, §10–§13 and §12-S3. Pre-check input:
`docs/superpowers/plans/2026-09-02-b278-s3-precheck.md`. S2 evidence:
`docs/superpowers/evidence/2026-09-02-b278-s2.md`.

S3 creates the first translated-record producer. A home which has already validated,
stored and pushed a direct custody report may correlate it to exactly one live delegated
row, build the 32-byte S2 form, and originate that form to the mobile through the existing
`send_by_hash` authority. S3 does **not** consume a translated form: every receiver retains
S2’s explicit interim refusal until S4.

## Contract quoted verbatim from the reviewed spec

> ### S3 — home translation
>
> 1. Preserve the existing direct store/push order.
> 2. Match complete identity after local handling.
> 3. Originate the translated `0x81` through `send_by_hash`.
> 4. Implement obligation transitions, no-map/ambiguous/refused telemetry and recursion proof.
> 5. Leave the cached-home `Plane::AUTO` behavior unchanged and pin that a static home's AUTO
>    resolution is equivalent to GLOBAL for this arm.

The normative receive order is §7’s eight steps: parse/validate → store → live Push → exact
lookup → bounded action → `send_by_hash` → conditional `forwarded` update → the caller’s one
existing `become_free()`. The order is not a suggestion; moving lookup/origination before
the local diagnostic or adding a second release changes the product contract.

## Ruled interpretations from the pre-check

### Parsed direct lengths are normalized only in the outbound copy

`pack_custody_failure_translated` deliberately accepts a **direct-shaped** prefix and
requires `record_len == 24`; it owns stamping 32 and bit 6. A parsed direct report may carry
an unknown future tail and therefore a wire `record_len > 24`.

S3 must:

1. store/push the original direct record and its complete received `record_len` unchanged;
2. copy only the parsed 24-byte semantic prefix into the bounded translation action;
3. set that copy’s `record_len = custody_record_v1_len` before calling the translated
   packer; and
4. let the S2 packer stamp the translated length and flag.

Do not relax the packer’s strict input contract. Do not forward an unknown direct tail as
if it were part of the translated record; §6.1 says the first 24 bytes retain their meaning
and §6.2 defines the translated bytes at 24–31.

### Normal absence is silent; a live-population mismatch is diagnostic

The corpus has direct custody receipts but no delegated-custody intersection. Emitting
“no map” for every valid direct report would add noise to a normal v1-only node and would
move four streams for no product benefit.

Therefore:

- zero live `eligible` rows at lookup is the normal no-map/ACK-first/expired state: store
  and Push the H1 diagnostic, originate nothing, emit no B278 lookup diagnostic;
- at least one live `eligible` row but zero exact matches emits one bounded
  `deleg_custody_no_map` diagnostic;
- more than one exact match emits one bounded `deleg_custody_ambiguous` diagnostic and
  originates nothing; and
- exactly one match materializes the action.

This is the §7/S3 no-map telemetry obligation without pretending that every unrelated
direct report was once delegated. It also keeps S3 corpus-byte-exact by construction; any
corpus movement is still a STOP, not a licence to re-anchor.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. the original direct record is not stored and pushed before correlation/origination, or
   S3 must alter the landed G validation/store/Push semantics;
2. lookup is performed by counter alone, by an event string, from `node_mac_rx.cpp` scanning
   `_deleg_acks` directly, or without all §4.4 return-key/type/layer terms;
3. zero/multiple matches guess a row, overwrite/evict one, or suppress H1’s local diagnostic;
4. an ACK-first, expired, already-`forwarded`, `candidate`, `none`, RESERVED or ineligible
   row can produce a translated send;
5. queued/parked does not mark the exact row `forwarded`, refusal marks it forwarded,
   refusal clears it, or the update trusts a stale slot without rechecking complete identity;
6. the translated packer or S2 receiver guard/codec must change, a translated receiver is
   added, or any JSON/USB/inbox/UI surface changes;
7. origination bypasses `send_by_hash`, changes a `send_by_hash` dispatch arm, inspects
   `_mobile_reg`, changes cached-home `Plane::AUTO`, requests E2E ACK, encryption, generic
   lifecycle, or creates a second mobile locator;
8. S3 calls `become_free()` directly inside `custody_failure_receive`, moves the caller’s
   existing release, or copies `PostAck`, `PendingTx`, the 241-byte body buffer, or the ring
   into the bounded action. The indirect pump performed by `enqueue_data` at the end of the
   existing `send_by_hash` path is established MOBILE_SEND re-origination behavior and is
   explicitly not a STOP;
9. a failed translated send generates `send_failed`, another custody notice, retries
   automatically, or mutates the original H1 record/Push;
10. `sizeof(Node)` or ruled-board RAM moves, compiler stack output is missing/dynamic/
    incomparable, or any stack delta is not fully explained by bounded S3 locals;
11. any corpus stream differs from the seventh owner-ruled table after a proven simulator
    relink, including movement caused only by telemetry;
12. the S1b census/comparator still rejects the now-legitimate `custody_state == forwarded`
    or its old “unreachable” claim is silently deleted rather than corrected in place;
13. any touched mutation target contains an unmatched, multi-matched, vacuous or unusable
    live entry; or
14. the implementation requires an S4 acceptance/presentation decision or an owner ruling.

## Fence

Allowed production files:

- `lib/core/node.h` — bounded action/result declarations and ring authority declarations;
- `lib/core/node_hashlocate.cpp` — exact lookup, identity recheck, and `eligible -> forwarded`;
- `lib/core/node_mac_rx.cpp` — steps 4–7 after the existing direct store/Push.

Allowed supporting files:

- `test/test_custody_receive_g.cpp`;
- `test/test_dual_layer.cpp` only for production-shaped `send_by_hash`/hosted-mobile routing
  cases which cannot be driven honestly in G’s fixture;
- `tools/probe_ui_model_mutations.py`, only the existing `sliceGrx` and `b251hash` targets;
- `tools/b278_correlation_census.py` and its existing test suite;
- `tools/compare_corpus_slice_s1b.py` and its existing test suite, solely to replace the
  S1b-era “forwarded is impossible” assertion with the S3 truth using the correction idiom;
- directly affected tool tests/checkers; and
- `docs/superpowers/evidence/2026-09-02-b278-s3.md`.

No codec file, `src/`, `lib/console/`, `lib/hal/`, `variants/`, simulator scenario,
`simulation/BASELINE.md`, `docs/frames.md`, `docs/protocol.md`, bench script, bug register,
spec status or project memory may be edited by the coder. No new mutation target or
production telemetry tool is expected; STOP rather than minting one without review.

The only board environments are `gateway` and `heltec_mobile`. The warning census’s pinned
set is the standing exception. B282, B283, B280, B281 and B112 remain separate.

## S3-1 — one lookup and update authority

Put the ring scan beside `deleg_ack_translate` in `node_hashlocate.cpp`, never in the
receiver. As its first operation, the authority must call `deleg_ack_prune_expired`, exactly
as `deleg_ack_reserve`, `deleg_ack_put` and `deleg_ack_translate` do, and only then scan.
It must consider only the remaining ACTIVE rows whose `custody_state == eligible` and match:

```text
ctr_h       == record.failed_ctr
return_kind == node_id
return_peer == record.failed_dst
layer       == record.reporter_layer
outward_type== record.failed_type
```

For a hash-addressed row, when the direct report carries `HAS_DST_HASH`, additionally require
`row.target == record.dst_hash32`. When that flag is absent, do not fail merely because a
hash is unavailable. A node-id row is already complete through the return key and retains
its original node target for the translated tail.

The authority returns an explicit disposition, not a nullable row that conflates cases:

- no live eligible rows after prune;
- eligible population present but no exact match;
- ambiguous exact matches; or
- exactly one materialized action.

The action is bounded and stack-only. It contains the 24-byte semantic record copy, the
eight-byte translated tail, the mobile hash and the complete row identity needed to prove
the same row still exists after `send_by_hash`. It contains no `PostAck`, `PendingTx`,
241-byte buffer, ring or pointer into mutable row storage. Pin its size/ABI if it is a named
type.

The post-send update uses the same exact row predicate. A slot number alone is not
authority. It writes `forwarded` only when:

- dispatch is `queued` or `parked`;
- the complete materialized identity still selects exactly one ACTIVE/eligible row; and
- no field changed between lookup and commit.

A refused/none dispatch leaves the row eligible until ACK or the 300 s expiry, allowing a
genuinely fresh repeat report to retry. A stale action changes nothing and emits the
refusal/invariant diagnostic; it may never mark a replacement row.

## S3-2 — translation construction and send

For an exact action:

- prefix: copy the direct record, normalize only the copy’s `record_len` to 24;
- `original_reporter = pa.origin`;
- `target_kind = row.target_kind`;
- `mobile_ctr = row.ctr_m`;
- `target_value = row.target`;
- pack with `pack_custody_failure_translated` into an exact 32-byte local buffer; and
- treat pack failure as a bounded forward refusal, never as permission to hand-build bytes.

Call the existing authority exactly once:

```text
send_by_hash(
    row.mobile_hash,
    body32, 32,
    flags=0,
    CryptIntent::off,
    reply_to_hash=0,
    mobile_ctr=0,
    Plane::GLOBAL,
    DATA_TYPE_CUSTODY_FAILURE,
    suppress_intro=false,
    &dispatch)
```

The type trait supplies no generic lifecycle. The send requests no E2E ACK and cannot
reserve another B278 row. `type != 0` prevents INTRO attachment. `reply_to_hash == 0`
prevents delegated-origin/reverse-ACK behavior.

Do not change `send_by_hash`’s cached-home arm: it uses `Plane::AUTO`. Add a production-
shaped test proving that, for a static home, this arm resolves to the same GLOBAL plane as
the explicit request. A dual team member’s existing behavior remains outside B278.

## S3-3 — order, ownership and lifecycle

The receiver’s order must be observable as:

```text
store original -> enqueue original Push -> lookup/materialize -> originate translated
-> conditionally mark forwarded -> caller releases received carrier once
```

Required lifecycle cases:

- **custody first:** queued/parked translation marks forwarded but retains the ACTIVE row;
  a later exact E2E ACK still translates to `ctr_m` and clears it;
- **ACK first:** the existing one-shot ACK translation clears the row; the later report is
  still stored/pushed at H1 but produces no translated send or B278 lookup noise;
- **duplicate report after forwarded:** still stored under existing direct-report rules,
  but no second mobile translation;
- **refused send:** row remains eligible; a later fresh report may retry and then mark it;
- **expiry:** prune-before-scan prevents a report arriving at age >= 300 s from matching an
  otherwise-unpruned eligible row; it originates nothing, and lifecycle telemetry reports
  the exact expired final state. A forwarded row expires at the same edge;
- **ambiguous/stale:** no send or state change; and
- **recursion:** a translated `0x81` terminal can emit transport telemetry only, never a
  custody report about itself.

The received 0x81 carrier still has one release owner: the existing caller below the typed
dispatch. S3 adds no `become_free()` inside the receiver.

## S3-4 — bounded telemetry

All events contain scalars only—never record bytes—and any emit-only local/parameter must
follow the measured `[[maybe_unused]]` precedent.

Use these exact event shapes:

```text
deleg_custody_no_map{
  dst, ctr_h, type, layer
}
deleg_custody_ambiguous{
  dst, ctr_h, type, layer, matches
}
deleg_custody_forwarded{
  mobile_hash, dst, ctr_h, ctr_m, type
}
deleg_custody_forward_refused{
  mobile_hash, dst, ctr_h, ctr_m, type
}
```

`no_map` fires only for a nonempty live-eligible population with zero exact matches.
`ambiguous` fires only for more than one exact match. `forwarded` fires only after the exact
row changed to forwarded; `forward_refused` covers pack refusal, `SendDispatch::none`/
`refused`, or an identity recheck failure. If distinguishing those refusal causes requires
another scalar field, STOP for review rather than inventing a second failure vocabulary.

The four events do not authorize a generic `send_failed`, Push, storage record or retry.
Pin each field name, order and integer type in native tests.

## S3-5 — repair the S1b instruments honestly

S1b correctly asserted that `forwarded` was unreachable then. S3 makes it reachable. Apply
the correction idiom in both tools:

- `compare_corpus_slice_s1b.py`: retain the historical statement as superseded, widen the
  lifecycle domain to the now-valid state 3, add a positive state-3 control and keep an
  out-of-range state-4 control RED;
- `b278_correlation_census.py`: consume `deleg_custody_forwarded` by complete row identity,
  transition exactly one eligible row to forwarded, accept expiry with state 3, and refuse
  missing/wrong-typed/ambiguous forward events.

Do not delete C7 or weaken integer type identity. Re-run both selftests and their complete
auto-discovered test suites. A forwarded event which does not bind exactly is a finding,
never ignored.

## Required native cases and falsifiers

At minimum, prove:

1. local store and Push happen before lookup/origination, and a no-map report still gets
   both local outcomes;
2. every match term—counter, return kind, return peer/destination, layer and outward type—
   fails independently; counter-only matching is RED;
3. hash target agrees when `HAS_DST_HASH` exists and is not required when absent;
4. zero eligible population is silent; eligible-but-no-match and ambiguity emit their exact
   bounded events and send nothing;
5. exact match builds the literal S2 32-byte form with H1/ctrH unchanged, `pa.origin` as the
   original reporter, and the row’s mobile counter/target;
6. a direct input with `record_len > 24` is stored whole locally but translated from a
   normalized 24-byte copy into exactly 32 bytes;
7. direct-host delivery uses type 0x81, no E2E/CRYPTED/generic lifecycle, `addr_len == 1`,
   no DST_HASH, H1 as outer origin, and reaches S2’s interim refusal at the mobile;
8. cached-home delivery retains the existing AUTO arm and is GLOBAL-equivalent for a
   static home, with DST_HASH equal to the mobile hash;
9. unresolved-home park is `parked`, preserves the exact 32-byte body/type and marks the
   row forwarded; park refusal remains eligible;
10. queued and parked mark forwarded; refused/none/pack-failed/stale-action do not;
11. a duplicate direct report after forwarded creates no second translation;
12. custody-first then ACK translates and clears; ACK-first then custody translates
    nothing and never downgrades;
13. an unpruned eligible row receiving a report at the 300 s edge or later is pruned before
    lookup and originates nothing; forwarded expiry reports state 3 and clears exactly once
    at that edge;
14. the caller remains the sole received-carrier release/pump owner;
15. a failed translated carrier produces no recursive custody record and no generic Push;
16. telemetry shapes and integer types are exact; and
17. removing the full-identity update recheck, marking on refusal, scanning from the
    receiver, changing AUTO, setting E2E/CRYPTED, or restoring state-3 rejection each turns
    a named case RED.

The direct-host and cached/re-homed transport cases may reuse existing production fixtures,
but no test seam may fabricate a Push or translated DATA in place of the real
`custody_failure_receive -> send_by_hash` path.

## Mutation ownership

Run both touched existing targets in full:

| target | source | S3 decisions |
| --- | --- | --- |
| `b251hash` | `lib/core/node_hashlocate.cpp` | complete lookup, no guessing, exact recheck, eligible→forwarded only |
| `sliceGrx` | `lib/core/node_mac_rx.cpp` | post-store/Push order, construction, one send, dispatch mapping, no second release |

Every live entry in both targets must be re-matched at count exactly one and re-proven RED.
Re-anchor any source text moved by S3 with the correction idiom. Add one arm per independent
decision above; no GREEN/vacuous/decorative live entry and no new target.

The S2 codec targets are not production-touched in S3 and are not automatically owed; the
native literal-vector cases remain the codec positive control. If S3 unexpectedly requires
a codec edit, STOP condition 6 fires.

## Corpus gate

Prediction first: no current stream contains both a delegated eligible row and a direct
custody receipt at the same node. Conditional no-map telemetry is therefore silent and no
translated carrier can be originated. All 36 streams must remain byte-exact against the
seventh owner-ruled table.

Rebuild `lus` and prove the relevant `node_mac_rx.cpp`/`node_hashlocate.cpp` actions and
relink fire. Run:

```text
tools/run_corpus.py --jobs=8 --require-anchors
```

Read every anchor and the keystone from `simulation/BASELINE.md`. Any event, hash, delivery,
duplicate, failure, routing or airtime movement is STOP. There is no S3 re-anchor proposal.
Additionally assert corpus-wide zero for all four new S3 events and for translated 0x81
transmissions; a zero without the relink control is not evidence.

## ABI, board RAM and compiler stack gate

### ABI and boards

Run the full board ABI and B278 row probes. `sizeof(Node)` must remain unchanged on host,
ARM and Xtensa; no persistent member is authorized. Measure exactly `gateway` and
`heltec_mobile` with `tools/measure_board.py pair --jobs=2`; RAM must remain zero-delta and
all flash/object/symbol movement must be attributed to the S3 path. Run the current warning
census with no re-pin and zero new warnings.

### Stack method—named, reproducible, and fail-loud

There is no standing repository stack instrument, so S3 uses the compiler’s own
`-fstack-usage` output without adding a repo tool:

1. preserve isolated pre-S3 and final-S3 source snapshots;
2. for each ruled env, obtain the real PlatformIO compile command for
   `node_mac_rx.cpp` and `node_hashlocate.cpp` from that env’s build/compilation database;
3. replay that exact command in each isolated snapshot with only `-fstack-usage` appended
   and the object/`.su` output redirected to a scratch path;
4. prove the command uses the ruled env’s real compiler/defines/includes and that a fresh,
   nonempty `.su` file was produced—missing/stale output is a gate failure;
5. record pre/post rows for `custody_failure_receive`, `send_by_hash`, `do_post_ack` and any
   new non-inlined S3 helper on both ARM and Xtensa, including GCC’s static/dynamic qualifier;
6. report the per-function deltas and a conservative call-chain sum for the translated
   receive→send path; and
7. require every row to be statically bounded and every delta to be explained by the
   32-byte body, eight-byte tail, `SendDispatch` and named bounded action plus ABI alignment.

Do not infer stack from board RAM, host ABI or `sizeof` alone. Do not edit `platformio.ini`
or normalize an absent function row as zero. Any unexplained or dynamic stack result is a
STOP finding for review, not permission to move buffers into `Node`.

## Wiring/checker gates

Run `tools/probe_firmware_ui/run.sh` and `tools/probe_inbox_verbs/run.sh` despite no UI
change, plus the DataType-literal and A0-matrix checkers. The translated receiver remains
refused, so no probe expectation should need widening. An unrelated failure is registered,
not repaired inside S3.

## Documentation draft held for PASS

`docs/frames.md` does not change: S2 already documents the final bytes. The evidence must
provide exact replacement text for the S2 paragraph in `docs/protocol.md`:

- replace “Nothing produces the translated form” with the S3 truth that an exact live
  delegated correlation at H1 now originates it through `send_by_hash`;
- retain that every receiver refuses it until S4;
- state queued/parked→forwarded, refusal→eligible, ACK-first suppression, custody-first
  preserving later ACK translation, and no automatic retry/generic failure; and
- keep the unauthenticated/not-proof-of-loss warning.

The Author lands that paragraph and S3 closure status only after QG PASS. No bench part is
owed by S3 alone because S4 still refuses the produced record; Part 54 remains S5’s first
end-to-end operator-visible gate.

## Required gates

1. **Fence/V1:** exact pre/post inventory; source search for one ring lookup authority, one
   `send_by_hash` origination site and no second release/offset writer/mobile locator.
2. **Native:** clean build and real binary; every case above; derived PIN movement with the
   prior pin retained.
3. **Mutations:** complete `b251hash` and `sliceGrx`, all live entries count one and RED;
   complete tool selftests for the corrected census/comparator.
4. **Order/lifecycle:** original store/Push first; exact action; queued/parked/refused,
   duplicate, stale, ACK-first/custody-first and expiry proofs.
5. **Corpus:** proven relink; 36/36 exact to the seventh table; zero S3 events/translated
   sends; no proposal.
6. **ABI/boards/stack:** both ABI probes; deterministic ruled pair with RAM +0; exact
   compiler `.su` method on both envs and fully attributed bounded deltas.
7. **Warnings/probes/checkers:** current census at pins; both wiring probes; both namespace
   checkers; all touched tool tests.
8. **Final:** `git diff --check`; exact `git status --short`; explicit untracked inventory;
   no device contact and no surviving process/monitor.

Every figure is derived by the coder during the run. Pre-check and S2 figures are
hypotheses or cross-checks only.

## Durable evidence and report shape

The one durable report is:

`docs/superpowers/evidence/2026-09-02-b278-s3.md`

It must contain:

- the exact lookup/update predicate, disposition type and bounded-action layout;
- the receive/store/Push/lookup/send/update/release order proof;
- construction of every direct-prefix and translated-tail field, including the
  `record_len > 24` normalization case;
- direct-host, cached-home/AUTO-equivalence, park/refusal and S2-interim-drop results;
- ACK-first, custody-first, duplicate, expiry, stale-action and recursion results;
- exact telemetry schemas and corpus-wide counts;
- the S1b comparator/census correction ledger and all sabotage controls;
- native/PIN and complete mutation results;
- proven simulator relink and 36/36 exact corpus result;
- host/ARM/Xtensa ABI, ruled board attribution, warning census, and the raw `.su` stack
  table with the command/qualifier evidence;
- both wiring probes and both namespace checkers;
- every STOP evaluated explicitly;
- exact protocol-text draft held for Author landing;
- the exact line `PIN re-synced? YES — <derivation>`; and
- exact final `git status --short`, separating pre-existing work and naming every untracked
  file which needs an explicit add.

No owner ruling is expected if all STOPs remain clear. Any corpus movement, RAM growth,
unbounded/unexplained stack change, altered cached-home plane behavior, or S4 surface need
is a finding, not permission to widen S3.
