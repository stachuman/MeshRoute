<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B278 S0 — delegated-custody correlation characterization · dispatch brief · 2026-09-02

**Status: COMPLETE 2026-09-02 — Opus evidence passed the Quality Agent; S0 closed; owner ruled capacity 8 and one 300 s row lifetime.**
Dispatch model used: **Opus**. Authority:
`docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md`, specifically §1.1's owner rulings,
§4.2–§4.5, §5, §10 and §12-S0. ⛔ **S0 IS CHARACTERIZATION ONLY:** zero production changes, zero device contact,
no wire/format/behavior change. S1a is deliberately a later brief: S0 may STOP for an owner capacity/RAM ruling
or may supply evidence for a later owner decision on the 750 s custody bound. Starting S1a before that checkpoint
would make the STOP ceremonial.

## The S0 contract — quoted verbatim, not paraphrased

The implementation spec's complete §12-S0 authority is:

> ### S0 — characterization and fixed decisions; zero production behavior change
>
> 1. Enumerate every reserve/activate/put/release/translate call and both mobile delegation
>    shapes.
> 2. Reproduce ACK-first one-shot clearing and non-E2E absence on current code.
> 3. Measure occupancy at the ruled 300 s / 750 s obligation bounds and all three ABI layouts.
> 4. Measure BUSY_RX retry amplification per refused `-a` send and count corpus custody
>    reports whose origination-to-report age exceeds 300 s.
> 5. Predict corpus movers and prove which streams contain eligible mobile delegation plus a
>    generated custody report.
> 6. Record baseline native, mutation, corpus, board and warning figures.
>
> STOP if the eight-row/no-eviction design fails the approved workload or the 32-byte row is
> not the measured layout.

The two §10 STOP/ruling boundaries also apply verbatim:

> If the last count is zero, report the evidence for an optional owner decision to collapse
> the custody bound to the 300 s ACK bound. Do not change R2's ruled 750 s custody bound inside
> S0 or S1 without that later ruling.

> If eight cannot cover the approved workload without ordinary false refusals, STOP for an
> owner capacity/RAM ruling. Do not silently evict or shorten the bound to make a test green.

## Scope fence

1. At report time, `git diff --stat -- lib src` must be empty. `variants/` and `platformio.ini` are likewise
   read-only. Allowed slice output is limited to `test/`, `tools/`, and the one durable evidence file under
   `docs/superpowers/evidence/`.
2. No production refactor, candidate struct in `node.h`, new event, changed event, simulator scenario, anchor-table
   edit, protocol/frame documentation change, register closure, tracker edit or bench step. The 32-byte candidate
   exists only as a probe-side mirror during S0.
3. No device contact. No `git add`, `git commit`, `git checkout`, poller or background monitor. Preserve every
   unrelated working-tree file.
4. Only the ruled board-build environments are allowed: `gateway` (ARM/nRF52840) and `heltec_mobile`
   (Xtensa/ESP32-S3), plus native as the host ABI reference. No third `measure_board.py` environment. The standing
   `tools/warning_census.sh` is the sole exception: it runs its own wider pinned environment set and does not widen
   the board-measurement ruling.
5. Every numeric result in the report is derived during this run. The pre-check ledger below is a hypothesis and
   cross-check, never report evidence to quote. A mismatch is a finding to explain, not a number to retype.

## S0-1 — authoritative call-site and state-transition census

Produce one table covering every current correlation operation:

```text
file:line | operation (reserve/activate/put/release/translate/prune) | mobile path
          | E2E flag gate | row phase before/after | release/expiry behavior
          | corpus event name/fields | native coverage owner
```

The census must include both mobile delegation shapes and every activation arm named by the design: resolved-id,
direct-host counter translation, cached-home, and all three park-fire sites. Separately mark the four arms S1b
will make ACK-only: direct last-mile (`addr_len == 1`), cached-home `send_cross_layer`, the wrapper XL branch, and
the XL park-fire site. S0 changes none of them.

Pin these current facts with source references and host characterization rather than prose alone:

- reservation exists only for `DATA_FLAG_E2E_ACK_REQ`;
- a plain non-E2E delegated send consumes no row;
- prune happens before admission; the ninth live row is refused, never evicted;
- ACK-first translation is one-shot and releases today's row;
- activation uniqueness and B251's two-mobile/equal-counter behavior remain current truth; and
- these telemetry names and their current fields are inventoried verbatim for S1a:
  `deleg_ack_reserved`, `deleg_ack_put`, `deleg_ack_put_refused`,
  `mobile_ctr_admission_refused`.

Any new characterization test must have a controlled RED proof. Use the isolated mutation harness against a
throwaway source copy where practical; never alter production files in the real tree. A passing case without a
negative control is an assertion, not characterization.

## S0-2 — current behavior and BUSY_RX retry pressure

Extend the existing native fixtures rather than inventing a second delegation model. V1 and cite the current
anchors around the full-ring BUSY NACK, ninth-row refusal, and both ACK-first clearing cases before editing tests.
Required measured cases:

1. ACK arrives first: exact `{home ctr → mobile ctr}` translation once, row unavailable afterward, a duplicate ACK
   cannot translate again.
2. At least nine otherwise-valid plain non-E2E delegated sends inside the modeled 750 s window: drain the TX queue
   between sends so its independent capacity cannot masquerade as ring pressure. The discriminator is the existing
   `mobile_ctr_admission_refused.reason`: **reason 1 = TX queue; reason 2 = correlation ring**. PASS requires zero
   `deleg_ack_reserved` and zero reason-2 refusals. Report every reason-1 event as queue pressure; never relabel it
   as a B278/ring result or hide it by silently changing the fixture.
3. Fill all eight current E2E rows, attempt one fresh `-a` send, and prove refusal occurs before the home hop ACK.
4. For that refused send, count every retransmitted DATA attempt until its terminal result. Derive from the active
   constants and decoded NACKs: encoded wait quanta, observed wait per NACK, requeue behavior, total attempts and
   elapsed lifetime. Do not infer “retryable” from the NACK label.
5. Repeat enough independent refused sends to state whether the per-send amplification is stable and whether a
   full ring can synchronize multiple senders into repeated attempts. If the fixture cannot represent the latter,
   report the limit explicitly rather than extrapolating it.

The fixture must prove its own counters are load-bearing: deleting the observed NACK/requeue, allowing a ninth row,
or allocating a non-E2E row must turn a focused control RED. Record match counts and unusable arms honestly.

## S0-3 — offline corpus occupancy and reachability model

The occupancy measurement is offline over canonical NDJSON output; S0 does not add simulator behavior. Its
telemetry limitations are an input, not a surprise to discover after claiming exactness:

- `deleg_ack_release`, expiry pruning and `deleg_ack_translate` emit nothing;
- an exact-retry reservation refresh is silent (only the first reservation emits); and
- the only ACK-release evidence is `mobile_reverse_ack{local,ctr}`, which carries the mobile-local id and `ctr_m`
  but neither `mobile_hash` nor `ctr_h`.

Use the recorded correlation events (`deleg_ack_reserved`, `deleg_ack_put`, `deleg_ack_put_refused`,
`mobile_reverse_ack` and timestamps) to model:

- current 300 s ACK-row occupancy;
- ruled 300 s ACK obligations plus provisional/confirmed 750 s custody obligations;
- reservation before activation, prune-before-admit, exact-row refresh, activation refusal/release, ACK release
  and independent expiry; and
- maximum simultaneous rows per node and stream, the owning identities and the time interval at the maximum.

The model must fail loud if an event lacks enough identity to bind a put, refresh or ACK uniquely. To translate
`mobile_reverse_ack.local` to a stable mobile hash, it must name and validate the registration authority it uses:
the current candidate is `mobile_registered{key,local_id,epoch}` at the same simulator node, with expiry/re-register
ordering applied. If that event cannot establish an unambiguous live mapping, label ACK release as a bound from
the start. It may not pair by counter alone, choose the nearest event, infer a missing mobile, or treat an
unclassifiable activation as custody-eligible. Silent refresh/release/expiry means an exact occupancy result is
not presumed: report separately labelled lower/upper bounds wherever telemetry cannot decide the row state, and
identify which missing event would close each interval.

Count custody-report ages independently. The receipt is
`custody_failure_rx{reporter,dst,ctr,seq}`. Candidate outward origins are
`tx_enqueue{origin,dst,ctr,depth}` for the wrapper path and
`mobile_ctr_translated{mobile_hash,dst,ctr_m,ctr_h}` for direct transit. Bind at the same simulator node on
`{dst,ctr}` plus event order, and classify it as delegated only when a matching `deleg_ack_put` exists at that
node. Report the full age list/distribution and the number strictly greater than 300 s. Unbound or ambiguous
reports are their own fail-loud census; they are never dropped from the denominator.

The current corpus is expected to contain no positive delegated-origin/custody-receipt intersection, so it cannot
prove this binder accepts its intended shape. Add a synthetic NDJSON stream or a two-node native fixture that
drives one exact positive and the negative siblings (wrong node, dst, ctr and order). The synthetic input tests
the instrument only and must never enter the canonical corpus or anchor table. If the measured canonical count
above 300 s is zero, tee up—but do not make—the optional owner ruling quoted above.

### Pre-review corpus hypothesis — must be independently reproduced

The reviewer found zero current streams exercising B278 end to end. The expected discriminator is:

- `s07` contains delegated E2E activations and custody traffic, but its correlation puts and custody receipt belong
  to different nodes/identities;
- `s06` and `twin_9node_dm` contain custody without delegation;
- `s22` contains delegated E2E flights without custody; and
- `s27` contains delegated originations which are non-E2E and therefore allocate no row under R1=A.

Re-derive this from fresh streams, including the per-node/per-identity counts. Do not copy the pre-check's figures.
Expected consequence: S3/S4 remain corpus-invisible and S5 would have no re-anchor proposal unless a new scenario
is authored. S0 may not author one: adding a scenario changes the owner-ruled corpus table and requires a separate
owner ruling.

## S0-4 — candidate ABI measurement without a production edit

`Node::DelegAck` is private; the standing ABI probe accepts namespace-scope production types. Therefore S0 must
not weaken access or put the candidate into `node.h`. Measure the spec's exact candidate field sequence as a
probe-side mirror compiled by the same host/ARM/Xtensa flag-and-toolchain authority used by
`tools/probe_board_abi.py`:

```text
uint64 ts_ms
uint32 mobile_hash
uint32 target
uint32 return_peer
uint16 ctr_h
uint16 ctr_m
uint8  layer
uint8  outward_type
uint8  target_kind
uint8  return_kind
uint8  state
uint8  obligations
```

Report `sizeof`, `alignof` and member offsets for all three ABIs. The mirror must measure 32 bytes or S0 STOPs
verbatim. Keep the current production row's own per-toolchain `static_assert` in view as the 24-byte control. Run
the full standing `tools/probe_board_abi.py` sweep as the current `Node`/ABI baseline. The report may show the
arithmetic ring projection (`8 × row delta`) but must label it projected, not measured Node RAM; S1a must measure
the real post-refactor `Node` on all ABIs.

Do not duplicate PlatformIO flag derivation in a new script. Reuse/import the ABI probe's authority or extend it
in a narrowly named synthetic-mirror mode. Add negative controls for at least: a missing candidate field, reordered
alignment-sensitive fields, a compile failure, and a target/toolchain silently omitted. A filtered or control-free
run is not a gate.

## Instruments and durable output

The durable deliverable is:

`docs/superpowers/evidence/2026-09-02-b278-s0.md`

It must contain the call-site table, behavior results, retry ledger, per-stream occupancy table, custody-age
ledger, ABI table, corpus reachability proof, every STOP decision, complete commands, tool versions and exact final
file inventory. Nothing load-bearing may live only in agent scratch.

If a new occupancy analyzer, ABI mode/probe TU or mutation target is added:

- give it a stable name under `tools/`;
- add it to an existing automatic tools gate, or add an auto-discovered `test_*.py` control suite;
- provide both positive and sabotage controls, with expected counts derived rather than hand-waved;
- run the gate which will catch it in a future slice (the B271 lesson); and
- list every new/untracked path explicitly in the evidence and final report so `git commit -a` cannot omit it.

## Required gates

1. **Native:** clean current binary first; then the S0 characterization additions. Derive case/assertion totals
   from the run, show the delta by named cases, zero failures, and re-synchronize every affected PIN/cross-check.
2. **Corpus:** rebuild `lus`, prove the rebuild actually compiled/relinked or explain the build graph, then run
   `tools/run_corpus.py --jobs=8 --require-anchors` into a gitignored/frozen output. Require 36/36 and derive the
   current keystone from `simulation/BASELINE.md`; run the offline analyzer on that validated snapshot.
3. **ABI:** full `tools/probe_board_abi.py` sweep with default negative controls, plus the synthetic mirror gate.
4. **Boards:** exactly one `tools/measure_board.py pair --jobs=2 --output <fresh-gitignored-dir>` invocation,
   covering only `gateway` and `heltec_mobile`. Derive every manifest field; no third gate environment.
5. **Warnings:** full `tools/warning_census.sh` at its own pinned environment set. This standing census is the one
   explicit exception to the two-env board-build rule: do not add its wider set to `measure_board.py`, and do not
   truncate the census to two environments. Explain drift; do not re-pin silently.
6. **Tools/mutations:** all touched automatic tool tests; all focused source-copy controls RED, zero silently
   unusable/vacuous instruments.
7. **Fence:** `git diff --stat -- lib src` empty; `git diff --check`; exact `git status --short`; no omitted
   untracked file.

The pre-check observed a green current corpus/native tree and a current rebuilt simulator. Those observations are
only a cross-check. The coder derives every baseline anew and reports any mismatch rather than quoting the
pre-check.

## Report shape

Lead with PASS or STOP and the exact controlling sentence. Then provide:

1. zero-production fence and exact file inventory;
2. call-site/state-transition table and both mobile shapes;
3. characterization cases and controlled RED ledger;
4. BUSY_RX retry amplification per refused send;
5. per-stream 300 s/750 s occupancy and custody-report-age tables;
6. corpus reachability result and explicit re-anchor expectation (`none`, unless STOP/finding);
7. host/ARM/Xtensa current and candidate ABI table, distinguishing measured from projected;
8. native, corpus/keystone, board pair, warning, ABI and tools gates—all derived during the run;
9. findings assigned to an existing row or proposed as a new row, never left in prose;
10. the exact line **`PIN re-synced? YES — <derivation/evidence>`** (or `NO`, which is a STOP); and
11. the exact final `git status --short`, with every untracked deliverable named.

On S0 PASS, stop and return the evidence. The supervisor/owner decides whether the 750 s bound and eight-row
capacity stand, then a separately reviewed S1a production brief is written. S0 never rolls directly into S1a.
