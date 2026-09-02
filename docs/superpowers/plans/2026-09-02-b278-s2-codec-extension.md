<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B278 S2 — additive translated-custody codec extension · dispatch brief · 2026-09-02

**Status: COMPLETE 2026-09-02 — software/QG closed; evidence at
`docs/superpowers/evidence/2026-09-02-b278-s2.md`.**
Dispatch model after PASS: **Opus**. Authority:
`docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md`, especially
§6, §12-S2, §13.3 and §13.5. Pre-check input:
`docs/superpowers/plans/2026-09-02-b278-s2-precheck.md`. S1b evidence and the current
owner-ruled corpus authority:
`docs/superpowers/evidence/2026-09-02-b278-s1b.md` and `simulation/BASELINE.md`.

S2 changes only the custody wire codec and installs one temporary receiver refusal. It
allocates `notice_flags` bit 6 and the 32-byte translated record, but it creates **no
translated producer and no translated consumer**. S3 will originate the form; S4 will
replace the temporary refusal with the translated receiver. This intermediate state is a
deliberate gate feature, not unfinished behavior hidden by the tests.

## Contract quoted verbatim from the reviewed spec

> ### S2 — codec extension
>
> 1. Allocate bit 6 and the 32-byte translated form.
> 2. Implement the one pack/parse authority and all direct/translated golden vectors.
> 3. Keep direct 24-byte output byte-identical.
> 4. Update `docs/frames.md` and `docs/protocol.md` drafts in the same slice; land them only
>    after QG PASS.

The codec rules are §6.1–§6.3. In particular, the first 24 bytes retain their exact v1
meaning; bit 6 is `CUSTODY_FLAG_HOME_TRANSLATED`; bit 7 remains reserved; the translated
tail at offsets 24–31 is:

```text
24  u8      original_reporter
25  u8      target_kind          0=node_id, 1=key_hash
26  u16 LE  mobile_ctr           nonzero
28  u32 LE  target_value         nonzero and kind-valid
```

The direct packer continues to emit exactly 24 bytes and may never set bit 6. A translated
record has `record_len >= 32`; bytes beyond its 32-byte known prefix are retained as the
future tail. `protocol::wire_version` and record version 1 stay unchanged under the
reflash-together ruling.

## Source truth and the additive API ruling

The four current `parse_custody_failure` callers are:

1. `Node::custody_failure_receive` in `lib/core/node_mac_rx.cpp`;
2. live-push JSON in `lib/console/console_json.cpp`;
3. pulled-record JSON in the same console TU; and
4. the board-only USB renderer in `src/fw_main.cpp`.

S2 must keep all four compiling without touching `src/`. Therefore the API is additive:

- keep `parse_custody_failure(std::span<const uint8_t>) ->
  std::optional<CustodyFailureRecord>`;
- expose translated/direct through the parsed record's `notice_flags` and one named helper;
- add a small value type for the eight-byte tail;
- add `parse_custody_translated_tail(body, record)` returning an optional tail;
- add `pack_custody_failure_translated(record, tail, out)`; and
- keep `pack_custody_failure(record, out)` as the direct-only 24-byte operation.

The translated packer and tail parser share the prefix writer/reader authority with the
direct codec. No caller writes or reads offsets 24–31 itself. Do not put tail fields into
`CustodyFailureRecord`: the direct prefix remains one value and the translated metadata is
one separate value.

`custody_record_tail()` currently always starts at byte 24. Correct that authority so the
future tail starts after the record's own known prefix—24 direct, 32 translated—while
keeping existing callers source-compatible. A translated record's eight defined bytes are
not an “unknown tail”. Add a single prefix-length helper if useful; do not create a second
offset table.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. keeping the parse and direct-pack signatures source-compatible proves impossible, or
   `src/fw_main.cpp` must change;
2. a direct 24-byte golden vector changes by even one byte, direct unknown-tail acceptance
   changes, or `custody_notice_flags()` can set bit 6;
3. the translated codec needs a second prefix serializer/parser or any caller writes a
   translated offset directly;
4. a translated record reaches the landed direct receiver beyond the explicit interim
   guard, stores a record, emits a Push, or runs G's contextual acceptance terms;
5. any translated producer, `send_by_hash` call, correlation lookup/state transition,
   JSON/USB field, inbox mapping, UI behavior, DataType, NV format, timer, or wire-version
   change appears;
6. `sizeof(Node)` or ruled-board RAM moves, or the separate tail value becomes persistent
   node state rather than a bounded stack/local codec value;
7. any corpus stream differs from the seventh owner-ruled table after a proven simulator
   relink, or the keystone differs from the value read from that table;
8. either existing codec mutation target contains an unmatched, multi-matched, vacuous or
   unusable live entry after the codec rewrite;
9. bit 6 cannot be accepted by the pure codec while remaining rejected by the current
   production receiver without duplicating the eighteen G validation terms; or
10. implementation requires a behavior decision assigned to S3 or S4.

## Fence

Allowed production files:

- `lib/core/frame_codec.h`;
- `lib/core/frame_codec.cpp`; and
- `lib/core/node_mac_rx.cpp`, only for the one interim translated-record refusal and its
  replacement-truth comment.

Allowed supporting files:

- `test/test_custody_relay_f.cpp`;
- `test/test_custody_receive_g.cpp` for the interim-guard case only;
- `tools/probe_ui_model_mutations.py` for re-anchoring the two named codec targets and adding
  S2 mutations;
- directly affected checker/tool tests if their source anchors move; and
- `docs/superpowers/evidence/2026-09-02-b278-s2.md`.

No file under `src/`, `variants/`, `lib/console/`, `lib/hal/`, `simulation/`, or board
configuration may change. No `simulation/BASELINE.md`, `docs/frames.md`, `docs/protocol.md`,
bug-register, bench-script, spec-status or project-memory edit belongs to the coder. The
coder supplies exact documentation drafts in the evidence file; the Author lands them only
after QG PASS.

The only board environments are `gateway` and `heltec_mobile`. The warning census's own
pinned environment set is the standing exception. B283's future nRF52 census widening is a
separate instrument slice; S2 neither implements nor relies on it.

## S2-1 — one explicit wire vocabulary

In `frame_codec.h`:

1. define `CUSTODY_FLAG_HOME_TRANSLATED = 0x40` exactly once next to the existing custody
   flag constants;
2. narrow `custody_flags_reserved_mask` to bit 7 only;
3. define the translated known-prefix length as 32 from the direct 24-byte prefix plus the
   eight-byte extension—not as unrelated magic literals;
4. add a typed `CustodyTranslatedTargetKind` or equivalently bounded enum with exactly
   `node_id=0` and `key_hash=1`, plus a fail-closed validity helper;
5. add the separate eight-byte value type and the additive pack/parse declarations; and
6. provide one named translated predicate/prefix-length authority used by the parser,
   future-tail accessor, interim receiver guard and tests.

`0x40` is a flag value, never a DataType literal. `tools/check_data_type_literals.py` and
`tools/check_a0_matrix.py` remain untouched and green.

## S2-2 — codec rules, both directions

### Direct form

The direct packer retains every landed refusal and additionally refuses the now-allocated
translated flag. It always writes `record_len = 24` and returns 24. Its existing golden
vector must remain byte-identical.

The common prefix parser accepts bit 6 only as an instruction to validate the translated
form; bit 7 still refuses. Bit 6 clear retains today's `record_len >= 24 && <= body.size()`
future-tail behavior exactly.

### Translated form

The translated packer writes the same validated 24-byte prefix, sets bit 6, writes the
eight-byte tail once, sets `record_len = 32`, and returns 32. It refuses every direct-prefix
invalidity plus every invalid translated-tail term.

For bit 6 set, the shared parse path requires all of the following:

- `record_len >= 32` and `record_len <= body.size()`;
- `original_reporter` in 1..254;
- `target_kind` exactly node id or key hash;
- `mobile_ctr != 0`;
- `target_value != 0`;
- node-id target in 1..254 and equal to `failed_dst`;
- key-hash target equals `dst_hash32` when `HAS_DST_HASH` is set; and
- all existing direct-prefix invariants.

A key-hash target without `HAS_DST_HASH` is codec-valid as the deliberately synthetic
future-compatible vector. Bytes after offset 31 are retained. The tail parser refuses a
direct record, a malformed/short translated record, and any record/body disagreement; it
does not reinterpret the first eight future-tail bytes of a direct record as translation.

## S2-3 — interim receiver guard

Today bit 6 dies at the codec's reserved-mask check. Once the pure codec accepts it,
`Node::custody_failure_receive` must preserve the current product behavior explicitly:

1. parse through the one shared codec;
2. immediately detect `HOME_TRANSLATED` from the parsed record;
3. take the existing bounded `custody_failure_reject` exit exactly once; and
4. return before `plane_supported`, `addressed_to_us`, type, layer, domain, store or Push
   logic.

Do not copy or modify G's eighteen validations. The guard comment must say it is the
ratified S2→S4 intermediate state and name S4 as the slice that replaces it.

Production-shaped native proof: a fully well-formed 32-byte translated record which would
otherwise be addressed to and contextually acceptable by the static node produces exactly
one rejection and zero store/push. A direct golden record still passes the landed receiver.
The existing 38 G receiver cases remain green and otherwise unedited.

## S2-4 — golden vectors and boundary matrix

Extend the codec cases in `test/test_custody_relay_f.cpp`. Required positive cases:

- direct 24-byte vector is exactly the pre-S2 byte array;
- translated 32-byte vector checks every offset and both little-endian fields independently;
- translated parse returns the original 24-byte record plus the exact typed tail;
- node-id target and hash target with `HAS_DST_HASH` each round-trip;
- hash target without `HAS_DST_HASH` parses as a clearly labelled synthetic vector;
- direct record with unknown bytes after 24 retains all of them;
- translated record with unknown bytes after 32 retains all of them; and
- the direct flags helper never returns bit 6 for any valid stage/boolean combination.

Required negative cases, one independently observable arm each:

- bit 6 with `record_len` 24 through 31;
- bit 7;
- unknown target kind;
- zero/255 node-id target and node-id target unequal to `failed_dst`;
- zero key-hash target;
- key-hash target unequal to `dst_hash32` when `HAS_DST_HASH` is set;
- invalid `original_reporter` at 0 and 255;
- `mobile_ctr == 0`;
- direct packer handed bit 6;
- translated packer handed a bad direct prefix or bad tail;
- tail parser handed a direct record; and
- declared length beyond the supplied body.

Do not use only a pack→parse round trip for endianness or offsets: the fixed 32-byte golden
array is the independent wire authority.

## S2-5 — mutation and structural gates

The four existing battery targets own decisions by production file; do not put a mutation
in a target that cannot edit its source:

| target | production source | S2 ownership |
| --- | --- | --- |
| `sliceFtypes` | `lib/core/frame_codec.h` | bit-6 allocation/reserved-mask authority, the direct flags helper, translated predicate/prefix/type helpers |
| `sliceFcodec` | `lib/core/frame_codec.cpp` | packer/parser offsets and translated pack/refusal rules |
| `sliceGcodec` | `lib/core/frame_codec.cpp` | the existing receiver-relevant codec refusals after the shared parser rewrite |
| `sliceGrx` | `lib/core/node_mac_rx.cpp` | the S2 interim translated-record receiver guard |

`MUTS_SLICEFCODEC` currently has ten live entries and `MUTS_SLICEGCODEC` five; derive all
four targets' actual live counts at dispatch. The coder must:

1. derive those counts from the current table rather than quote this brief;
2. rematch every live entry against the rewritten source with match count exactly one;
3. re-prove every entry RED in the isolated battery;
4. re-anchor instead of deleting an entry whose authority moved;
5. add one mutation per independent new refusal/decision, routed by the table above:
   `sliceFtypes` owns “bit 6 kept reserved” and “the direct flags helper can set bit 6”;
   `sliceFcodec`/`sliceGcodec` own bit 7 accepted, the weakened translated-length floor,
   each weakened target-kind/value relation, dropped reporter/mobile-counter validation,
   wrong future-tail offset, direct packer accepting bit 6 and translated flag omission;
   `sliceGrx` owns removal of the interim receiver guard;
6. keep no GREEN, vacuous or decorative live entry; and
7. record every retirement/re-anchor with the correction idiom.

Run all four touched targets in full—`sliceFtypes`, `sliceFcodec`, `sliceGcodec`, and
`sliceGrx`—not selected entries. Every live entry in each target must be re-matched at
count exactly one and re-proven RED. Do not invent a fifth target. Any genuinely necessary
new instrument must have a stable name, sabotage controls, an auto-discovered `test_*.py`
suite where applicable, and an explicit-add line in the evidence inventory.

## Corpus, ABI, boards and warnings

### Corpus

Prediction first: S2 has no translated producer, so all 36 streams must remain byte-exact
against the seventh owner-ruled table. Rebuild `lus`; because `frame_codec.cpp` is in its
build graph, the relink/recompile control must demonstrably fire even though event streams
must not move. Run:

```text
tools/run_corpus.py --jobs=8 --require-anchors
```

Read the current hashes and keystone from `simulation/BASELINE.md` during the run; do not
copy figures from this brief or pre-check. Any mover is a STOP—there is no S2 re-anchor
proposal and the coder never edits the table.

### ABI and stack

Run the full board ABI probe. Prove `sizeof(Node)` unchanged on host, ARM and Xtensa and
board RAM unchanged. Measure `sizeof(CustodyFailureRecord)` and the new tail value on all
three toolchains. State the RX-stack change from the additional bounded tail local if one is
material; no `Node` member may hold it.

### Boards and warnings

Run the deterministic pair through `tools/measure_board.py pair --jobs=2`, exactly
`gateway` and `heltec_mobile`; report and symbol-attribute flash, RAM, objects and relevant
sections. Run `tools/warning_census.sh` at its current pinned set, with zero silent re-pin.
The codec must not introduce MR_EMIT-only locals or parameters.

Run both standing wiring probes despite the absence of a UI change:
`tools/probe_firmware_ui/run.sh` and `tools/probe_inbox_verbs/run.sh`. This is the B271
lesson applied to a touched receive/console-visible type. They must pass at their derived
pins; the coder must not repair an unrelated failure inside S2.

## Documentation drafts held for PASS

The S2 evidence must contain exact ready-to-land drafts for:

- `docs/frames.md`: allocate notice bit 6, state bit 7 remains reserved, and add the
  offsets 24–31 translated-tail table without behavior prose;
- `docs/protocol.md`: state that the translated form exists on the wire after S2 but every
  receiver explicitly refuses it until S4; and
- this spec's §6.3 only if implementation requires a source-compatible spelling more exact
  than the additive API already ruled here.

The Author lands those texts only after QG PASS. No bench part is owed by S2: the codec and
interim refusal are host-reachable. Part 54 remains the end-to-end S5 metal gate.

## Required gates

1. **Fence/V1:** pre- and post-diff inventory; `git diff --check`; zero changes under every
   forbidden path; source search proving exactly four old parse callers still compile and no
   caller writes translated offsets.
2. **Native:** clean rebuild and real binary; derive case/assertion movement and update the
   one current PIN with a written derivation.
3. **Touched batteries:** complete `sliceFtypes`, `sliceFcodec`, `sliceGcodec`, and
   `sliceGrx`; every live entry match count one, every usable mutation RED, zero silent
   retirement.
4. **Receiver:** production `Node::custody_failure_receive` translated refusal plus direct
   positive control; all existing G cases green.
5. **Corpus:** proven simulator relink; 36/36 byte-exact to the seventh ruled table through
   the canonical parallel runner; no proposal.
6. **ABI/boards:** full ABI probe; deterministic ruled pair; no Node/board-RAM movement;
   complete flash/stack attribution.
7. **Warnings/probes/checkers:** current warning census; both wiring probes; DataType and A0
   matrix checkers; every touched tool test and auto-discovered suite.
8. **Final:** exact `git status --short`, explicit untracked-file inventory, and proof no
   process/device monitor remains. No device contact.

Every figure is derived by the coder during the run. Pre-check and S1b numbers are
hypotheses or cross-checks only.

## Durable evidence and report shape

The one durable report is:

`docs/superpowers/evidence/2026-09-02-b278-s2.md`

It must contain:

- the final public codec API and why each existing caller remained source-compatible;
- the direct/translated layouts and complete validation ownership table;
- all golden bytes and every boundary/refusal result;
- proof the future-tail offset is 24 direct and 32 translated;
- the interim receiver guard's exact placement and zero-store/zero-Push result;
- the complete mutation re-anchor/addition ledger and match counts;
- simulator rebuild proof and the 36/36 byte-identity table result;
- host/ARM/Xtensa structure measurements, stack note, ruled board attribution and warnings;
- both wiring probes and both namespace/matrix checkers;
- every STOP condition evaluated explicitly;
- exact documentation drafts held for Author landing;
- the exact line `PIN re-synced? YES — <derivation>`; and
- exact final `git status --short`, separating pre-existing work and naming every untracked
  file which needs an explicit add.

No owner ruling is expected if every STOP remains clear. A wire-version question, RAM
movement, receiver behavior beyond the interim refusal, or any corpus delta is a finding,
not permission to widen S2.
