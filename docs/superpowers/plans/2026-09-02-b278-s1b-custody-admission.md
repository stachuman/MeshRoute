<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B278 S1b — custody admission, collision refusal and exact lifecycle telemetry · dispatch brief · 2026-09-02

**Status: COMPLETE 2026-09-02 — software/QG closed; owner ruling applied; seventh corpus-table ruling landed; evidence at
`docs/superpowers/evidence/2026-09-02-b278-s1b.md`.**
Dispatch model after PASS: **Opus**. Authority:
`docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md`, especially
§4.2–§5, §10 and §12-S1b. Pre-check input:
`docs/superpowers/plans/2026-09-02-b278-s1b-precheck.md`. S1a evidence:
`docs/superpowers/evidence/2026-09-02-b278-s1a.md`.

This is B278's first behavior slice. It makes the already-landed `custody_state` meaningful,
strengthens cross-mobile activation uniqueness, and makes correlation lifecycle telemetry
exact. It does **not** add the translated custody wire form or consume a custody report.

## Contract quoted verbatim from the reviewed spec

> ### S1b — R1/R2 admission and obligation behavior
>
> 1. Add a provisional custody candidate only to rows already reserved by
>    `DATA_FLAG_E2E_ACK_REQ` and passing §5's reservation phase; confirm or clear it at every
>    activation arm.
> 2. Preserve zero row allocation for every non-E2E delegated send.
> 3. Strengthen activation uniqueness across all mobiles: refuse a second ACTIVE row sharing
>    `{ctr_h, return_kind, return_peer, layer}` even when `mobile_hash` differs, and flip the
>    existing B251 case which currently admits both rows. Measure this admission change in
>    S1b; do not describe it as refactor fallout.
> 4. Pin all activation paths, not only the resolved-id arm: resolved-id dispatch, direct-host
>    counter translation, cached-home dispatch, and all three parked-send fire sites currently
>    near `node_hashlocate.cpp:2259`, `:2277` and `:2331`. Explicitly drive and clear the
>    provisional custody candidate on all four ACK-only arms: direct last-mile (`addr_len=1`),
>    cached-home `send_cross_layer`, the wrapper XL branch near `node_mac_rx.cpp:1757`, and the
>    XL park-fire site near `node_hashlocate.cpp:2259`. The last is synthetic: its current
>    producer structurally leaves `reply_to_hash == 0`.
> 5. Apply `delegated_custody_ttl_ms == e2e_ack_deadline_xl_ms` to the complete row at the
>    exclusive 300 s edge; do not add separate ACK/custody timers.
> 6. Land F6's scalar measurement surface: make release and expiry observable, include
>    `target_kind` on reservation, and include `mobile_hash` plus `ctr_h` on reverse-ACK
>    translation. Preserve all existing event names; attribute every added field/event and
>    corpus movement to S1b.
>
> No custody wire or receiver change in S1b.

The complete §5 eligibility predicate is also authoritative, verbatim:

> It returns true only when the prospective outward carrier is:
>
> 1. a verified direct hosted-mobile transit or a valid `DATA_TYPE_MOBILE_SEND` wrapper from
>    a live hosted row;
> 2. explicitly requesting an E2E ACK through `DATA_FLAG_E2E_ACK_REQ`;
> 3. plaintext at the outward DATA-frame level;
> 4. static/global and same-layer;
> 5. prospectively a normal DATA carrier, not channel M/FLOOD;
> 6. not already known to be cross-layer, team, gateway re-inject or hosted-mobile last-mile;
> 7. not `DATA_TYPE_E2E_ACK` or `DATA_TYPE_CUSTODY_FAILURE`; and
> 8. addressed to a nonzero target with a complete target identity.

## Owner ruling recorded 2026-09-02 — wrapper collision signal

The new cross-mobile uniqueness refusal has two different timing shapes:

- direct transit checks before the mobile hop ACK and returns BUSY_RX reason 2; and
- a wrapper discovers the collision only after its outward DM was admitted. The reservation
  is released, the DM still flies, an eventual ACK cannot translate, and the mobile reaches
  `e2e_ack_timeout`.

**RULING: keep the wrapper consequence as measured and do not call
`presence_mark_deleg_fail` in S1b.** That one-shot becomes `send_failed{no_route}`, but this
outward DM was not dropped and may deliver; using it would manufacture a false delivery
claim. S1b must test and report the timeout consequence explicitly. If faster feedback is
wanted, register a separate typed `correlation_lost` outcome whose wording does not claim
that the message failed. The coder may not substitute another signal or reinterpret this
ruling.

## STOP conditions

STOP and report—do not widen the slice—if:

1. any non-E2E delegated send reserves a row or gains a B278 BUSY_RX refusal;
2. any candidate becomes eligible without an explicit activation-arm verdict, or any
   cross-layer/last-mile/E2E-ACK/custody carrier remains eligible;
3. the cross-mobile refusal can misdeliver, overwrite or evict the incumbent, or a direct
   refusal happens after the mobile hop ACK;
4. `forwarded` becomes reachable, any custody record is packed/parsed/received, or any wire,
   NV, timer-capacity, `src/` or UI surface must change;
5. a corpus line outside the predicted correlation telemetry changes, either predicted mover
   is absent, another stream moves, or any delivery/failure/routing event changes;
6. `sizeof(DelegAck)` or `sizeof(Node)` moves on any ABI, or either ruled board gains RAM;
7. release/expiry telemetry cannot be emitted from one semantic authority without duplicating
   lifecycle policy; or
8. the unreachable cross-layer park-fire would require inventing a production path merely to
   claim coverage.

## Scope fence

Allowed production edits are confined to the existing delegated-correlation authority and
its current callers in `lib/core/node.h`, `lib/core/node_hashlocate.cpp` and
`lib/core/node_mac_rx.cpp`. Allowed support edits are the affected tests and instruments
under `test/` and `tools/`, plus the one evidence file named below.

Nothing under `src/`, `variants/`, `platformio.ini`, `docs/frames.md`, `docs/protocol.md`, the
bench script or `simulation/BASELINE.md` may change. No codec, custody receiver,
`send_by_hash` call from that receiver, `forwarded` transition, new timer, simulator scenario
or anchor-table edit. No device contact. Preserve all unrelated and S0/S1a files. Never add,
commit, checkout or clean the working tree.

The only board environments are `gateway` and `heltec_mobile`; the warning census's own
pinned environment set remains the sole exception.

## S1b-1 — two-phase custody eligibility

Do not create a second type list. Implement one named eligibility authority which applies
the eight §5 terms and records why a row is only provisional. Reservation may use only facts
already parsed before the mobile hop ACK:

| Reservation path | State written before ACK |
| --- | --- |
| direct hosted-mobile transit | `candidate` only when `d.type` is reportable and all other §5 terms hold |
| `MOBILE_SEND` wrapper | `candidate` only when the verified wrapper is E2E-requesting and `ui->has_cross_layer == false`; enclosed type, last-mile and cached-home facts remain for activation |
| wrapper already marked cross-layer | `none`; it remains an ACK-correlation row only |
| every non-E2E carrier | no row, exactly as before |

Do not add a second body parser or peek at a wrapper byte merely to make the reservation
decision look final. `candidate` means “not yet disproven”; activation owns the final type
and dispatch-arm facts.

Every activation/put call receives an explicit custody verdict. `commit_deleg_ack` cannot
infer the arm from `{return_kind,return_peer}` because direct last-mile and cached-home
same-layer use the same shape. Its four callers must therefore supply their decision.

| Activation arm | Final state | Type authority / reason |
| --- | --- | --- |
| direct hosted-mobile transit | `eligible` | `d.type`; all §5 terms true |
| resolved-id wrapper | `eligible` when the final `itype` is reportable | same-layer static outward flight |
| direct last-mile (`addr_len == 1`) | `none` | custody generator excludes last mile |
| cached-home cross-layer | `none` | cross-layer exclusion |
| cached-home same-layer | `eligible` when final `itype` is reportable | same-layer outward DATA to the target home |
| wrapper cross-layer branch | `none` | `etype`; ACK-only even if reservation was already none |
| `drain_parked_sends` cross-layer fire | `none`, structural/synthetic | current producer never carries `reply_to_hash` |
| `drain_parked_sends` same-layer fire | `eligible` when `p.type` is reportable | same-layer resolved target |
| `drain_resolved_parked_sends` same-layer fire | `eligible` when `p.type` is reportable | same-layer authoritative binding |

`candidate` is valid only while RESERVED. An ACTIVE row is `eligible` or `none`.
`forwarded` is S3 and must remain unreachable. Exact retry preserves the candidate; active
refresh preserves the already-decided state. The whole row still expires at the one 300 s
edge.

Tests must drive every reachable arm in both directions. The cross-layer park-fire remains a
compile/structural control; never claim a production-shaped case for it.

## S1b-2 — cross-mobile return-key refusal

At both activation authorities, uniqueness becomes:

```text
{ctr_h, return_kind, return_peer, layer}
```

The incumbent mobile hash is deliberately not part of that wire-visible key. A conflicting
row is never overwritten or evicted.

Required proofs:

- flip the existing B251 two-mobile test and the second half of S1a's same-mobile-scoped
  case; both must now prove the second activation refuses and the incumbent remains intact;
- retain the same-mobile and layer/key/type discriminator controls;
- direct transit refuses before hop ACK with BUSY_RX reason 2;
- wrapper activation refusal releases only the new reservation, lets the already-admitted DM
  follow today's path, leaves the incumbent mapping intact, forwards any later ACK under the
  untranslated home counter and ends in the mobile timeout described in the ruling section;
- natural per-destination counters remain collision-free in the corpus; the guard is for
  wrap/malformed state, not a claimed ordinary event; and
- a mutation re-adding either `mobile_hash` term is RED.

Mutation D09 becomes production and must be retired with the correction idiom. Its inverse
replaces it. D12 must be re-scoped so it attacks candidate state on an ineligible carrier or
eligible state on a cleared arm; it may not remain a mutation of the now-correct behavior.

## S1b-3 — one exact lifecycle telemetry authority

Existing event names and existing field order remain intact. Append only the reviewed fields:

- `deleg_ack_reserved{mobile_hash,ctr_m,target,layer,target_kind}`;
- `mobile_reverse_ack{local,ctr,mobile_hash,ctr_h}`.

Add:

```text
deleg_ack_released{mobile_hash,ctr_m,target,target_kind,layer,cause}
deleg_ack_expired{mobile_hash,ctr_m,ctr_h,target,layer,custody_state}
```

Define one non-wire `DelegAckReleaseCause : uint8_t` authority and pass it into
`deleg_ack_release`; emit once inside that helper immediately before the matching RESERVED
row is cleared. Do not copy the event across call sites. The complete semantic inventory is:

- `activation_conflict` — the return key cannot activate;
- `commit_invariant` — the prechecked direct-transit commit unexpectedly fails;
- `carrier_ineligible` — the reserved wrapper becomes a carrier which does not use this map;
- `invalid_path` — malformed cross-layer path;
- `source_not_owned` — stale/spoofed wrapper source;
- `dispatch_refused` — enqueue, park, resolution or cross-layer dispatch did not admit; and
- `park_giveup` — the retained send ages out.

Every release call site must map to exactly one cause in an evidence table. Do not infer the
cause inside the helper from row fields. A no-match release emits nothing.

Use one expiry helper for the three existing prune sites so the event is emitted before the
whole-row clear and the `age >= delegated_custody_ttl_ms` decision remains one authority.
The helper must not change scan order, refresh behavior or prune-before-admit. Expiry of a
RESERVED or ACTIVE row reports its exact pre-clear values; free rows emit nothing.

Any local **or parameter** whose only consumer is `MR_EMIT` must be `[[maybe_unused]]` or
avoided by reading already-live values—the B169 warning class is a required gate. This
explicitly includes the new `cause` parameter to `deleg_ack_release` and any
`custody_state` value read only for `deleg_ack_expired`: `MR_EMIT` compiles out on device,
while the board builds use `-Wall -Wextra` and the warning census counts every `warning:`
line. Follow the existing production precedent
`enqueue_data(..., [[maybe_unused]] const char* tx_event, ...)` in
`lib/core/node_mac.cpp`; do not suppress the warning globally. Event fields are scalar only;
never emit bodies, keys or record bytes.

Update `tools/b278_correlation_census.py` to consume `mobile_reverse_ack.mobile_hash` and
`ctr_h` directly. Retire/re-scope its registration-hop controls (`O3s`/`O3a`) without losing
negative coverage, keep its TTL/header pin, and prove release plus expiry closes every
previous lower/upper-bound interval. The analyzer must fail loud on missing, wrong-typed or
ambiguous new fields.

## Corpus obligation — predicted movement, no behavior drift

Predict before running. The pre-check hypothesis is exactly two movers: `s07` and `s22`,
because they contain the correlation lifecycle events whose fields/companions change. No
other stream has a correlation row, and the cross-mobile refusal cannot arise from natural
per-destination counters. Re-derive that fact; do not quote the pre-check's counts.

Implement this comparison as the stable instrument
`tools/compare_corpus_slice_s1b.py`, with the auto-discovered control suite
`tools/test_compare_corpus_slice_s1b.py`. It consumes frozen before/after run directories and
compares ordered NDJSON. Permit only:

1. the appended `target_kind` on an existing `deleg_ack_reserved` line;
2. the appended `mobile_hash` and `ctr_h` on an existing `mobile_reverse_ack` line; and
3. a correctly bound `deleg_ack_released` or `deleg_ack_expired` line immediately before its
   corresponding lifecycle clear.

Every other line, field, ordering relation and event remains byte-identical. Add comparator
controls for: changing an unrelated field, moving an event, omitting an expected lifecycle
event, inserting one without a live row, wrong row identity, and accepting a third mover.
Delivery, duplicate, failure, airtime and assertion figures must remain identical per stream.

The comparator needs positive controls for each permitted transformation and sabotage
controls for every refusal above, with counts derived rather than hand-pinned. Its test file
must be found by the standing `test_*.py` tools discovery and also run directly in this
slice. Both new paths must appear in the evidence's untracked-file inventory so a
`git commit -a` cannot omit the instrument or its gate.

If hashes move as predicted, write a full in-tree **RE-ANCHOR PROPOSAL** in the S1b evidence:
old/new hash and event count for all 36 streams, exact line attribution for each mover, the
current keystone read from `simulation/BASELINE.md`, and a conspicuous prohibition on editing
the anchor table. The owner alone may approve the seventh table ruling after QG PASS.

Any unpredicted corpus movement is STOP, not something to normalize into the comparator.

## Native and mutation matrix

Required native cases:

- reservation candidate true/false for every §5 term which is knowable pre-ACK;
- final eligible/none result for every reachable activation arm and both final type classes;
- exact retry and active refresh preserve their existing state;
- `forwarded` is unreachable;
- both flipped cross-mobile tests and both direct/wrapper refusal consequences;
- non-E2E nine-send controls remain zero-row/zero-B278-refusal;
- 300 s − 1 and exact 300 s expiry for RESERVED and ACTIVE, with one exact expiry event;
- every release cause, no-match silence, and one-event-only behavior;
- the two appended event-field shapes and their types/order; and
- the census consumes direct reverse-ACK identity and resolves occupancy exactly.

Mutation requirements, all match count one and in full touched targets:

- candidate omitted or set on a known-ineligible reservation;
- each explicit activation verdict inverted, including the synthetic XL park-fire structural
  anchor;
- `candidate` allowed to survive ACTIVE or `forwarded` written early;
- either cross-mobile `mobile_hash` term restored;
- incumbent overwritten/evicted or refusal moved after the direct hop ACK;
- D09 retirement/inverse and D12 re-scope;
- release cause omitted/wrong, emit after clear, no-match emits, or one call site bypasses the
  helper;
- expiry emit omitted, emitted after clear, exact edge changed, or one prune site bypasses the
  helper;
- `[[maybe_unused]]` removed from an emit-only local/parameter on a feature-off board build;
- existing telemetry renamed/reordered instead of fields being appended;
- census ignores new identity, accepts a missing field, or keeps using registration as the
  ACK authority; and
- B169 feature-off compilation control.

Run the complete affected B251/B278 targets, not selected arms. Re-derive every anchor and
re-synchronize the native PIN. The report must contain exactly:
`PIN re-synced? YES — <derivation>`.

## Required gates

1. **Native:** build and run the binary; derive totals and the complete named-case delta;
   zero failures.
2. **Mutations/tools:** full affected targets, all applicable entries RED at match count one,
   no silent unusable/vacuous entries; all census selftests; direct and auto-discovered runs
   of `tools/test_compare_corpus_slice_s1b.py`, including every sabotage control.
3. **Corpus:** rebuild `lus` with a fired recompile/relink control; run the canonical runner
   at `--jobs=8`; run the ordered S1b delta comparator; produce the proposal only if every
   movement is permitted and attributed. Never edit `BASELINE.md`.
4. **ABI:** full `probe_b278_row_abi.py` and `probe_board_abi.py`; row and `Node` sizes must
   remain the S1a values on host/ARM/Xtensa. A move is STOP.
5. **Boards:** exactly `gateway` and `heltec_mobile` via
   `measure_board.py pair --jobs=2` in a fresh gitignored `.pio-measure/` directory; derive
   RAM/flash/object/symbol changes. RAM is predicted zero but must be measured.
6. **Warnings:** full `warning_census.sh` at its own pinned env set, the sole two-env
   exception; no B169 drift and no silent re-pin.
7. **Fence:** `git diff --check`; exact `git status --short`; explicit proof that `src/`, wire
   docs, bench, simulator scenarios and `BASELINE.md` are untouched.

Every figure is derived during the coder's run. Pre-check and S1a figures are hypotheses or
cross-checks only.

## Durable evidence and report shape

The one durable report is:

`docs/superpowers/evidence/2026-09-02-b278-s1b.md`

It must contain:

- the reservation/activation truth table and every call-site verdict;
- the two flipped tests and direct/wrapper collision outcomes;
- the release-cause call-site table and expiry authority;
- exact old/new telemetry schemas;
- census before/after precision and its negative controls;
- corpus prediction, ordered comparison, full re-anchor proposal if applicable, and every
  delivery/failure/airtime cross-check;
- native, complete mutation/tool, ABI, board and warning results;
- every STOP evaluated explicitly;
- `PIN re-synced? YES — <derivation>`;
- `OWNER RULING APPLIED` with the no-`deleg_fail` wrapper consequence and its measured
  timeout proof;
  and
- exact final `git status --short`, separating pre-existing files and naming every untracked
  path which needs an explicit add—including `tools/compare_corpus_slice_s1b.py` and
  `tools/test_compare_corpus_slice_s1b.py` if created by this slice.

No bench part is owed by S1b: its states, refusals, telemetry, corpus effects and RAM are all
host/ABI/board-instrument reachable. Metal remains Part 54 after S2–S5 complete the B278
wire and receiver path.
