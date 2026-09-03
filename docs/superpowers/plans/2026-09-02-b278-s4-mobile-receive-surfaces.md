<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B278 S4 — mobile receive and translated-custody surfaces · dispatch brief · 2026-09-02

**Status: COMPLETE — implementation and independent Quality Gate PASS 2026-09-03.**
Dispatch model after PASS: **Opus**. Authority:
`docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md`, especially
§8, §12-S4 and §13.3–§13.5. Pre-check input:
`docs/superpowers/plans/2026-09-02-b278-s4-precheck.md`. S3 evidence:
`docs/superpowers/evidence/2026-09-02-b278-s3.md`.

S4 replaces S2's interim refusal with the first translated-record consumer. A configured
mobile accepts a translated `0x81` only when its outer routing context agrees with the
codec-validated record and tail, stores the complete record before emitting the existing
`PushKind::custody_failure`, and renders the translated identity consistently on live JSON,
pulled JSON and USB. Direct custody behavior and bytes remain compatible. S4 adds no OLED
state machine, RPC state machine, retry, wire field, PushKind or persistent layout.

## Contract quoted verbatim from the reviewed spec

> ### S4 — mobile receive and surfaces
>
> 1. Split direct vs translated contextual validation.
> 2. Persist translated records and emit the reused PushKind.
> 3. Add JSON, pulled-record and USB fields.
> 4. Re-run Slice C visibility and unread-budget gates.
> 5. Add the generic exact-tuple consumer fixture; no OLED/RPC state machine is implemented
>    here unless separately included by an approved amendment.

## Required result

At the end of S4:

- a direct record follows the landed G/S3 path byte-for-byte;
- a translated record is consumed only by its intended configured mobile;
- the store and Push mappings are exactly §8.3's;
- all three presentation surfaces expose §8.4's same translated fields while direct output
  stays byte-identical;
- OLED rows/unread budgets still exclude both forms by the landed internal-type trait;
- raw `pull_inbox` still returns both forms; and
- a test-only generic consumer demonstrates complete-tuple matching without adding a
  product UI/RPC consumer.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. accepting translated custody requires a codec, wire, trait, PushKind, inbox format,
   `send_by_hash`, correlation-ring or `Node`-layout change;
2. a static node accepts a translated record, a mobile accepts one on team/crypted arrival,
   or either rule-7 arm can bypass the configured-mobile requirement;
3. receiver code re-implements a byte offset/domain already owned by the S2 codec rather
   than citing `parse_custody_failure` and `parse_custody_translated_tail`;
4. direct validation, storage, Push, telemetry or JSON/USB bytes move;
5. translated storage or Push uses `pa.origin`/`failed_ctr` where §8.3 requires
   `original_reporter`/`mobile_ctr`, truncates the body to 24/32 instead of `record_len`, or
   pushes before the store returns its sequence;
6. the translated branch reaches S3's home lookup/origination block, generates an E2E ACK,
   becomes an ordinary inbox DM, or creates generic send lifecycle;
7. JSON/USB invents a second parser, renames an existing direct field, emits both target
   fields, adds `via_home`/`home_ctr`, calls the report a NACK/failure of delivery, or changes
   the meaning of existing `ctr`;
8. the USB branch remains decision-bearing code in uncompiled `fw_main.cpp`, or its gate
   merely scans structure without executing the real formatter;
9. OLED/history/unread code changes instead of the landed Slice-C trait exclusion being
   re-proven, or raw `pull_inbox` begins filtering either custody form;
10. any production matcher uses the store key `(origin,msg_id)`, target/counter only, or a
    private home telemetry event as the operation identity;
11. any corpus stream differs from the seventh owner-ruled table after a proven simulator
    relink, including movement in the direct `custody_failure_rx` or store/Push events on
    the eleven direct receipts;
12. `sizeof(Node)`, ruled-board RAM, or another persistent ABI moves; compiler stack output
    is missing/dynamic/incomparable or an increase is not bounded and attributed;
13. any touched mutation/probe target has an unmatched, multi-matched, vacuous, unusable or
    silently retired live control; or
14. an OLED/RPC state transition, companion implementation, retry policy, new owner ruling
    or S5 metal decision is required.

## Fence

Allowed production files:

- `lib/core/node_mac_rx.cpp` — replace the interim refusal with the split receiver and map
  the translated record into the existing store/Push path;
- `lib/console/console_json.cpp` — translated-only live and pulled fields, sharing the
  existing custody emitter;
- `src/fw_main.cpp` — replace only the custody USB switch body with a call to the gated
  production renderer;
- one narrowly named production header, preferably `src/firmware_custody_push.h`, solely to
  hold that exact USB renderer where both firmware and a host probe compile and execute it.

`lib/core/inbox.*` is allowed only if inspection proves §8.3 cannot use its existing
`record_custody_failure` signature; the expectation is **no inbox production edit**. No
codec, `node.h`, `node_hashlocate.cpp`, `lib/hal/`, OLED renderer, remote-admin code,
companion implementation, simulator scenario, `simulation/BASELINE.md`, `docs/frames.md`,
bench script, bug register, spec status or project memory may be edited by the coder.

Allowed supporting files:

- `test/test_custody_receive_g.cpp` and directly affected console/inbox tests;
- `tools/probe_ui_model_mutations.py`, only existing touched targets;
- `tools/probe_firmware_ui/` and `tools/probe_inbox_verbs/` only for required expectation
  updates or Slice-C re-runs;
- a new stable `tools/probe_custody_usb/` executable wiring probe plus an auto-discovered
  `tools/test_probe_custody_usb.py` invocation/contract test;
- directly affected checkers; and
- `docs/superpowers/evidence/2026-09-02-b278-s4.md`.

The coder drafts the protocol, companion-contract and command-reference text in the evidence
file; the Author lands it only after QG PASS. The only board environments are `gateway` and
`heltec_mobile`; the warning census's pinned set is the standing exception. B277, B282,
B283, B280, B281 and B112 remain separate.

## S4-1 — split contextual validation without duplicating the codec

Keep the one frame-level entrance and the one shared parser:

1. `DATA_FLAG_CRYPTED` must be clear;
2. the standard unicast inner must exist;
3. call `parse_custody_failure(ui->body)` exactly once; and
4. branch on `custody_record_is_translated(rec.notice_flags)`.

The following remain **codec-owned and are cited, not re-implemented or double-counted**:
body/record length; version; reserved flag bit 7; mandatory forwarded bit and exactly one
stage; known nonzero reason; the four static-ID domains; nonzero `failed_ctr`; hash-flag
agreement; reserved prefix bytes; translated length; valid `original_reporter`; defined
target kind; nonzero `mobile_ctr` and target; node-id width/equality; and the optional
translated target-hash cross-check. `parse_custody_translated_tail(ui->body, rec)` is the one
tail reader. A translated record returned by the prefix parser must yield a tail; refusal if
it does not is a defensive codec-coherence check, not a second validation table.

The following remain **receiver-owned common context** and are evaluated for both modes:

- body claims `static_same_layer` and the carrier arrived off the team plane;
- `failed_type` is neither E2E ACK nor custody failure; and
- the four count/hop values fit the existing protocol authorities.

Direct mode retains its exact landed context:

- `rec.failed_origin == _node_id`; and
- `rec.reporter_layer == active_layer_id()`.

Translated mode instead requires every one of these receiver-context terms:

- `_cfg.is_mobile`—a static build/node never consumes this form;
- `pa.origin == rec.failed_origin`;
- when the parsed inner carries `DST_HASH`, its hash equals this mobile's `_key_hash32`;
- without `DST_HASH`, the mobile is registered, `mobile_home_id() == pa.origin`, and
  `rec.reporter_layer == active_layer_id()`.

Both rule-7 alternatives are production requirements, not synthetic conveniences. S3
already measured the normal direct-host form without `DST_HASH` and the re-homed last-mile
form with `DST_HASH == mobile`. A wrong current home must reject the former; the latter may
arrive with H1 as outer origin through a different current H2 and must accept by stable hash.
No membership, trust, authentication or retry meaning is inferred from acceptance.

All refusals continue through the existing one bounded
`custody_failure_reject{type,origin,dst,ctr}` exit, with no body bytes, store, Push or generic
unsupported-internal event. Another unknown internal type must still reach Slice B's
fail-closed tail guard.

## S4-2 — one store/Push order, two explicit mappings

After validation, derive semantic locals once and run the existing order:

```text
validate -> select direct/translated mapping -> store full record -> obtain seq
         -> enqueue Push carrying that seq -> emit factual receive telemetry -> return
```

Direct mapping stays exactly as landed:

```text
record origin / Push origin = pa.origin
record msg_id / Push ctr    = rec.failed_ctr
Push dst                    = rec.failed_dst
layer                       = active_layer_id()
body                        = record_len bytes
```

Translated mapping is exactly §8.3:

```text
record origin / Push origin = tail.original_reporter
record msg_id / Push ctr    = tail.mobile_ctr
Push dst                    = rec.failed_dst
layer                       = rec.reporter_layer
body                        = record_len bytes, including every accepted future byte
```

Both use `DATA_TYPE_CUSTODY_FAILURE`; `Push::reason` remains `none`; `seq` is the sequence
returned by the store or zero when storage is disabled. Storage precedes Push. The factual
`custody_failure_rx` telemetry follows the selected public identity, while its direct form
and field order remain byte-identical.

After the common store/Push/emit block, translated mode returns. Only direct mode may enter
S3's delegated-row lookup and translated-origination block. Neither mode reaches ordinary
DM delivery or generates an E2E ACK.

Required persistence cases include translated `record_len == 32`, a translated accepted
future tail above 32 retained whole, storage-disabled `seq == 0`, and a remount/pull proving
`origin=original_reporter`, `msg_id=mobile_ctr`, `layer=reporter_layer`, full body retention,
and Push `seq` equal to the stored sequence.

## S4-3 — live and pulled JSON stay one semantic surface

Extend the existing shared custody-field emitter. Direct input must produce exactly its
pre-S4 bytes. For translated input, append without renaming the existing fields:

```text
delegated      true
target_kind    "node_id" | "hash"
target_id      numeric target_value       (node-id form only)
target_hash    existing canonical hex form (hash form only)
mobile_ctr     tail.mobile_ctr
```

The existing `ctr` remains `rec.failed_ctr`/ctrH. Existing `reporter` comes from Push/store
origin and therefore equals `tail.original_reporter` for translated records. Existing
`failed_origin` remains H1. Emit exactly one target-value field and no `via_home` or
`home_ctr`. Both live `write_push` and pulled `write_inbox_dm` must call the same field
authority and produce the same semantic tuple, differing only in their established envelope
fields (`seq`, and pulled `rx_ms`). An unparseable stored custody record retains its existing
fail-loud behavior.

Required golden cases cover direct byte identity; translated node-id and hash forms on both
live and pulled paths; future-tail retention with presentation reading only the known
32-byte prefix; exactly-one target field; no aliases; and integer/string type identity.

## S4-4 — the USB renderer must become executable evidence

`src/fw_main.cpp` is outside native and simulator builds. Therefore do not leave translated
field selection in its switch body and call that “glue.” Extract the complete custody USB
renderer—direct and translated—into one narrow production header such as
`src/firmware_custody_push.h`. The `PushKind::custody_failure` switch arm calls that helper
exactly once; it retains no parsing, target selection or wording decision of its own.

`tools/probe_custody_usb/` must compile and execute that exact production helper against the
real custody codec and the existing faithful Arduino `Print`/`F()` fake at
`tools/probe_console_sink/fakes/Arduino.h`—never a second fake. The helper takes a `Print&`,
the same output seam used by `dispatch()`. The probe must drive:

- the pre-S4 direct golden line byte-for-byte;
- translated node-id and hash golden lines;
- the unparseable-record fail-loud line;
- omitted `seq` at zero and present `seq` when nonzero;
- `delegated`, `target_kind`, exactly one target value and `mobile_ctr` with the same names
  and value types as JSON; and
- the unchanged “NOT proof the destination missed it” warning, with no NACK or delivery-
  failure claim.

The runner also pins that the real `fw_main.cpp` switch delegates exactly once to the helper
and contains no second custody body parser. This structural wiring pin is necessary but is
not sufficient: the executable helper cases above are the behavioral half.

Controls run by default and must independently turn RED when: the switch call is removed;
the direct line changes; translated-tail parsing is bypassed; `mobile_ctr` uses ctrH; target
kind/value is hard-wired; both target fields are printed; an alias is introduced; the
warning is removed; or the helper accepts an unparseable record. Controls must distinguish
vacuous mutation, build failure, crash/dead worker and a surviving mutant. `--no-neg`, if
offered, prints `PROBE-ONLY — NOT A GATE`, never PASS. Pin check/control counts with written
derivations. Add `tools/test_probe_custody_usb.py` so the standing tools discovery executes
the clean/default gate and its anti-vacuity contract; list every new file explicitly for
commit.

## S4-5 — visibility, raw pull and the generic consumer fixture

Do not change OLED or unread code. Re-run and cite the existing authority:
`inbox_record_is_internal(e.type)` derives from `data_type_traits(type).internal`, so direct
and translated `0x81` are excluded before OLED row/detail callbacks and before visible
budgets. Re-run the Slice-C positive and ordering controls, `probe_firmware_ui`, and the
inbox-verb probe. Raw `Inbox::pull()` remains deliberately unfiltered and returns both
forms; pin this with direct and translated records in one pull.

Add a test-only generic operation-consumer fixture. It parses the record/tail through the
production codec and matches the complete body tuple:

```text
{failed_origin, reporter_layer, target_kind, target_value, mobile_ctr, failed_type}
```

It must not use the store key `(origin,msg_id)` as correlation authority. Prove independently
that wrong home, layer, target kind, target value, mobile counter and failed type each refuse;
matching by counter only and matching by `(origin,msg_id)` only must each turn a named
mutation/control RED. The positive case consumes exactly one matching operation. This is a
fixture proving the contract is sufficient for Slice H/remote-admin; it adds no production
consumer or state transition.

## Required native cases and falsifiers

Drive production-shaped frames through the real receiver and real store/Push/JSON paths:

1. direct receipt remains byte- and behavior-identical, including JSON and telemetry;
2. translated direct-host/no-DST_HASH accepts only for an active mobile registered to the
   outer H1 on the matching layer;
3. translated re-homed/DST_HASH accepts through H2 when the carried hash equals the mobile,
   while `pa.origin == failed_origin == H1` remains true;
4. a static receiver rejects translated input;
5. configured-mobile, plaintext, static-arrival, outer-home equality, DST_HASH equality,
   no-hash selected-home relation, layer, reportable type and count domains each fail alone;
6. codec-owned malformed prefix/tail cases remain receiver refusals through their existing
   codec tests and are not duplicated as new S4 policy mutations;
7. wrong current home rejects the no-hash arm; absence of DST_HASH does not reject the valid
   direct-host arm; presence of the correct hash does not require old-home equality;
8. another unknown internal type still takes the fail-closed guard;
9. translated storage and Push carry original reporter/mobile counter/full body and the
   store-assigned seq, including disabled-storage and future-tail arms;
10. translated mode returns before every S3 lookup/origination event and produces no send;
11. live and pulled JSON agree on the complete translated semantic fields for both target
    kinds, direct JSON is byte-identical, and forbidden aliases are absent;
12. the executable USB probe covers the same two target kinds and direct compatibility;
13. OLED rows, detail callbacks and unread budgets exclude both forms before budgeting,
    while raw pull returns both;
14. the generic fixture requires the full six-field tuple and refuses every one-field
    mismatch; and
15. a translated carrier's failure remains non-recursive and generates no generic Push.

Each receiver-owned independent decision gets a falsifier. Do not mint duplicate mutations
for a codec-owned term already attacked in `sliceFtypes`/`sliceFcodec`/`sliceGcodec`; run the
relevant codec cases as positive dependencies instead.

## Mutation and instrument ownership

Run every touched existing target in full, with every live entry re-matched at count one:

| target | source | S4 responsibility |
| --- | --- | --- |
| `sliceGrx` | `lib/core/node_mac_rx.cpp` | mode split, contextual terms, mapping/order, direct-only S3 tail |
| `sliceGjson` | `lib/console/console_json.cpp` | shared live/pulled translated fields and direct byte identity |

Run `sliceGinbox`, `sliceCpull` and `sliceCinbox` as unchanged dependency gates; add no entry
unless their production source genuinely changes. Run the S2/G codec targets only as their
complete existing positive/refusal batteries if an anchor moved; any codec production edit
is STOP 1.

The USB helper is owned by `probe_custody_usb`, not forced into an unrelated per-source
mutation target. Its default controls and auto-discovered test are part of the gate. Any
existing assertion/comment which says translated custody is refused must be corrected in
place with the old claim visible and the S4 replacement named; never silently delete it.

## Corpus gate

Prediction: **36/36 byte-identical** to the seventh owner-ruled table. S3 proved that the
corpus has zero translated transmissions and all four S3 events are zero. The corpus does
exercise eleven direct receipts through their `custody_failure_rx` and store/Push events,
which must remain unchanged. It does **not** render custody JSON: the simulator links
`console_json.cpp` only for `pushkind_name`, and the streams carry only the structured Push
emit. Direct JSON compatibility is therefore owned by G/5's exact golden case and the
complete `sliceGjson` battery, not inferred from corpus identity.

1. prove the simulator relinked after the final S4 production edit; a zero-action rebuild is
   evidence only after a changed-binary/recompile control has fired for this tree;
2. run `tools/run_corpus.py --jobs=8 --require-anchors` against a fresh ignored output;
3. require all 36 hashes/event counts/failures to reproduce the ruled table and the s18
   keystone to reproduce exactly;
4. require the four S3 events and translated-receipt count to remain corpus-wide zero; and
5. treat any movement as a STOP—no S4 re-anchor proposal and no BASELINE edit.

## ABI, boards, stack, warnings and probes

- Native: run the complete suite, derive the S4 case/assertion delta from per-case filters,
  and re-sync the standing PIN with its written arithmetic.
- ABI: run `probe_board_abi.py` and `probe_b278_row_abi.py`; `Node`, `DelegAck` and the S3
  action must stay at their established sizes on host/ARM/Xtensa.
- Boards: run the deterministic `gateway` + `heltec_mobile` pair with `--jobs=2`; require RAM
  delta zero and attribute every flash/symbol movement to the receiver, JSON or USB helper.
- Stack: repeat S3 evidence §10.4 exactly—derive each env's real command from PlatformIO
  `idedata`, replay it in isolated pre/post snapshots with `-fstack-usage`, require fresh
  `.su` files and zero dynamic frames, and report `custody_failure_receive` plus any new
  non-inlined helper on both ABIs. Explain every delta using bounded locals.
- Warning census: run its own pinned environment set and retain zero `-Wswitch`; apply the
  B169 rule to any emit-only local or parameter.
- Probes/checkers: run the new USB gate by default, complete tools discovery, both existing
  wiring probes, DataType literal checker and A0 matrix checker.
- Run `git diff --check`; list all untracked files explicitly.

## Documentation held for Author landing

The S4 evidence must contain exact proposed text, not edit these documents:

1. replace `docs/protocol.md`'s “every receiver still refuses until S4” paragraph with the
   landed direct/translated receiver rules, mappings, surfaces and unauthenticated/no-retry
   caution;
2. extend `ios-companion/INBOX_SYNC_CONTRACT.md`'s custody section with the translated live
   and pulled fields, complete-tuple correlation rule, and B277 compatibility boundary;
3. extend `docs/manual/command-reference.md`'s custody line with the translated USB/JSON
   fields and exactly-one-target rule; and
4. state explicitly that `docs/frames.md` needs no S4 edit because S2 already documented the
   final bytes.

No bench part is owed by S4 alone. The four-radio end-to-end operator-visible proof remains
Part 54, owned by S5.

## Required report shape

The evidence file must report:

- exact production/supporting diff and every STOP disposition;
- codec-owned versus receiver-owned validation table;
- direct/no-hash and re-homed/hash acceptance plus every independent refusal;
- persistence/Push order and mapping, including future tail and disabled storage;
- live/pulled/USB golden outputs and direct byte-identity proof;
- USB probe architecture, pinned counts, controls, auto-discovery and tracked-file status;
- Slice-C OLED/unread/raw-pull re-verification and generic complete-tuple fixture;
- native total and derived PIN arithmetic;
- full mutation/probe ledgers with zero unusable/vacuous live entries;
- simulator relink proof and 36/36 exact corpus result;
- ABI, deterministic board, symbol, stack and warning results;
- held documentation drafts and explicit no-frames/no-bench statements;
- `PIN re-synced? YES/NO`, `git diff --check`, untracked inventory; and
- a final PASS/HOLD verdict with any owner question separated from findings.
