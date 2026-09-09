<!-- QA/Author: OpenAI Codex; production coder: separate Codex session -->
# Remote-admin Slice 7b-2-0 — admission-response codec — revision 2 — 2026-09-09

**Status: SOFTWARE-COMPLETE / INDEPENDENT QA PASS 2026-09-09 — UNCOMMITTED.** The frozen revision-2
implementation passed QA's complete independent gate; [evidence](../evidence/2026-09-09-radmin-slice7b2-0-qa-gate.md).
The consumed contract SHA-256 was `16ee9214041bca71699c2c04fd1ea932f09b203af2b680b68176e4edeb282f1d`;
this status/closure landing does not revise its wire or behavior contract. B382/B383 are closed. R-RA-35/
R-RA-36 are unchanged. Owner commits the codec preparation separately; QA then reissues the 7b-2 behavior
brief at the actual successor. That behavior implementation remains HOLD.

## 1. Base, inputs and authority

MeshRoute base **`1d4b3ad5a74c2f24121d8fbe28b8d6e4b04f8dfe`**; simulator
**`06746a97de5764415d6fcef10b97bca90569b9c7`**, clean, no simulator edits authorized. At brief authoring, production,
tests and tools equalled the committed base; the following input inventory is that historical checkpoint.
The shared checkout had expected documentation changes:

- `MEMORY.md`, the maintained bug register, the remote-admin design and rulings ledger;
- the 2026-09-08 7b-2 behavior brief, pre-check and its evidence directory;
- the coder's `docs/superpowers/evidence/2026-09-08-radmin-slice7b2.md`, including §6 owner receipt;
- this brief and its [independent pre-check](2026-09-09-radmin-slice7b2-0-precheck.md).

Inventory and hash all of them. Preserve every tracked/untracked input; do not call this a clean shared tree.
An isolated implementation/QA snapshot includes the complete current working state, including new files;
HEAD alone is only the production attribution base. Never reset, clean or commit. No QA build/mutation run
may overlap coder edits in the same measured tree. Owner commits; QA independently gates the frozen handoff.

The coder's [revision-1 preflight and complete B383 reproducer](../evidence/2026-09-09-radmin-slice7b2-0.md)
are now expected inputs. Preserve that report's preflight/fixture history when appending implementation
evidence; QA has not edited it. The consumed revision-1 brief SHA-256 was
`0e9635c4a8457eab55ea7b722669257e678171953fdfd55eaa383df049261fc3`. QA independently matched all **13/13**
reported preparation hashes before this fold-in and reproduced **24/24 B383 checks** against unchanged
private-base archives. Pre-check §4 records provenance and limits; no implementation gate is implied.

Read design §§8.1, 8.9/8.9a, 8.11, 9, 19.1; rulings R-RA-4/13/28/35/36; the Slice-2 codec brief/evidence;
7b-1 revision-6 contract and independent QA §4; 7b-2 revision 3 and its pre-check. Historical Slice-2 statements
that the codec has no callers or no linked code are superseded by the current source ledger in §2.

**Verbatim owner pin, R-RA-36:**

> B379: the proposed admission-response format and separate codec preparation are approved.

**Verbatim scope pin, R-RA-36:**

> **Attribution and order:** implement, gate and owner-commit **7b-2-0 codec preparation separately**. It has
> no new target producer, controller consumer, session/open behavior or Node allocation. QA then reissues the
> 7b-2 behavior brief at the actual committed successor. No global `wire_version` bump or corpus re-anchor is
> part of this allocation: the remote opcode is the subprotocol discriminator (design §8.1). B379 remains open
> through codec/KAT verification and the subsequent real target full/busy/retry proofs; a ruling is not a PASS.

**Verbatim unchanged KAT authority, design §8.1:**

> data is `outer_type_u8 ||` the exact clear RPC-header bytes in wire order `|| source_hash_le32`; ciphertext
> and tag are not repeated in AAD. The independent-reference KATs must pin every domain, both directions and
> cross-domain inequality—not merely round-trip through the production codec.

The codec implements R-RA-36 only. R-RA-35's three open requests **total per target per five minutes** and
its storage/counters remain the later behavior slice's obligations. This brief adds no quota or live producer.

## 2. Source ledger and independently executed pre-check

Anchors below are at the named base; relocate by symbol before editing (V1/V2).

| Source | Current fact / consequence |
| --- | --- |
| `lib/core/remote_codec.h:67`, `remote_codec.cpp:170` | Response opcodes 0..4 exist; 5..15 reject. Append opcode 5; command opcode 5 remains FORCE_ROLLOVER. |
| `remote_codec.cpp:72/125/218` | `header_bytes_of`, `remote_layout` and fixed-overhead arithmetic are the single layout authority. Extend them, not a second admission codec. |
| `remote_codec.cpp:277/295/301` | Nonce hashes label/key/direction/ctl/id/seq/source; only two old response domains append epoch. Existing result/detail are not nonce inputs. |
| `remote_codec.cpp:313/332` | One `write_header` feeds encoding and exact-header AAD. Preserve this conversion path. |
| `remote_codec.cpp:437/489` | Public encode/decode enforce carrier admission and fixed lengths; fixed authenticated messages already tag empty plaintext. |
| `remote_codec.cpp:578` | Existing typed results are parsed from payload; clear admission fields must not enter the catch-all authenticated PROTOCOL_ERROR branch. |
| `remote_codec.h:314–322`, `remote_codec.cpp:554–594` | B383: the header's blanket untouched-plaintext promise is false. Valid-tag old variable bodies decrypt before result validation; `bad_result_code` can leave plaintext in the buffer, while `out` remains unpublished. Bad tags leave the buffer unchanged. Correct the comment, preserve this behavior. |
| `remote_codec.h:153/173/191/244/250` | Largest header 41 bytes, AAD 46; layout/message/decoded records are caller-owned values. Add typed admission metadata without persistent state. |
| `remote_session.cpp:592/797/835/862/926` | Real authenticated transcript/bootstrap/RX callers already exist. Their bytes, decisions and state stay unchanged in this slice. |
| `test/test_remote_codec.cpp:865/940/1078/1216/1359/1624/1689/2168` | Exhaustive ctl map, fixed-overhead table, complete wire literals, nonce matrix, exact lengths, typed results and fixed-body capacity tests must all include the new domain. |
| `tools/probe_ui_model_mutations.py:316/693/10277` | Existing `radmin2codec` target, native PIN and battery; extend the same battery, preserve existing controls. |

QA reran the unchanged private native binary: **2883 cases / 127709 assertions / 0 failed / 0 skipped**.
The original independent Python reference reran its external primitive anchors and matched **87/87 existing
native literals**. Fresh corpus: **36/36 current anchors**, zero failures, using the previously built,
source-matched two-variant simulator. No new codec implementation, board link or mutation gate is claimed.
The [pre-check](2026-09-09-radmin-slice7b2-0-precheck.md) records commands, hashes, reuse limits and raw evidence.

## 3. Exact new wire and cryptographic contract

Outer type is existing **REMOTE_RESP `0xA1`**. Response opcode **`0x5`**, actual slot **0..9**, produces ctl
`0x50..0x59`. Slots A..E remain reserved; F is an invalid pairing. Response opcodes 6..15 remain reserved.
No command opcode, global wire version, DATA type, outer carrier or old result number changes.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 1 | Response `ctl` |
| 1 | 8 | `request_id` LE64 |
| 9 | 1 | `request_ctl`, including matching ACL slot |
| 10 | 1 | Typed admission code |
| 11 | 1 | Detail |
| 12 | 16 | K_session tag over empty plaintext |

Exactly **12 clear header bytes + 16 tag bytes = 28 bytes**; application bytes are zero. No ciphertext,
response sequence, epoch, public-key field or variable suffix is carried. Encoding requires an empty body;
decoding succeeds with an empty plaintext-output span. Do not invent a one-byte encrypted result payload.

| Code | Allowed request opcode | Detail constraint |
| --- | --- | --- |
| `0x00 session_full` | AUTH_EXECUTE `0x0` | 0 |
| `0x01 ingress_full` | AUTH_EXECUTE `0x0`, SAFE_ROLLOVER `0x4`, FORCE_ROLLOVER `0x5` | 0 |
| `0x02 session_busy` | SAFE_ROLLOVER `0x4` | 1..255 on the wire |
| `0x03 executing` | SAFE_ROLLOVER `0x4`, FORCE_ROLLOVER `0x5` | 0 |
| `0x04 preparation_failed` | SAFE_ROLLOVER `0x4`, FORCE_ROLLOVER `0x5` | 0 |

The request slot must equal the response slot, both 0..9. All other request opcodes, result codes and pairings
refuse. Validate these semantics on encoding and decoding through one local authority. Distinguish structural
or semantic refusal from bad authentication, without publishing an accepted decoded result on either.
Do not hardcode a busy maximum of four in this stateless codec: the live consumer later enforces its ruled
capacity. Cover valid wire values 1, 2, 4 and 255, and reject zero. The caller cannot smuggle bytes into an old
result domain through admission fields; old-domain bytes and acceptance rules remain unchanged.

For this new domain only, the nonce is:

```text
BLAKE2b-512(
  ASCII("MeshRoute remote-admin v2 nonce") without NUL
  || K_session[32] || 0xA1 || response_ctl
  || request_id_le64 || 0x00 || controller_source_hash_le32
  || request_ctl || admission_code || detail
)[:24]
```

The zero byte is the existing no-sequence nonce term, not a new wire field. No epoch is appended. AAD is
`0xA1 || exact_12_byte_clear_header || controller_source_hash_le32`. Use existing BLAKE2b-512-then-truncate,
XChaCha20-Poly1305 wrappers, selected-key rule, bounded Writer/Reader, carrier admission and wiping idioms.
Never substitute parameterized BLAKE2b-192, a fresh label, base-key fallback or a second seal implementation.
The existing maximum nonce scratch covers an eight-byte epoch suffix; this new suffix is only three bytes.
The existing 41-byte header / 46-byte AAD maxima also cover this envelope. Derive/assert all sizes; do not
add resident scratch or arbitrarily grow maxima. Inactive old fields such as `response_seq` must not acquire
a nonce/wire meaning in this fixed domain.

SOURCE_HASH presence and value remain separate. Absent source refuses; present zero is a valid codec input.
This does not change the Node sender's `radmin_reply_no_dst` refusal. Sender accounting/routing is outside
this slice; existing N11 and B381 remain the boundary.

Append a distinct typed admission domain/result kind and code enum to the current public types, preserving
existing enum meanings. Carry request ctl/code/detail through the existing logical message/decoded path.
On success, the decoder reports authenticated admission metadata, an empty application body, and no
terminal/protocol-error interpretation. A decoded code 0 must distinguish all three meanings:
TERMINAL/completed, authenticated PROTOCOL_ERROR/already_acknowledged, ADMISSION_RESULT/session_full.
Clear scalar detail is not an encrypted-payload `result_detail` view. On any failed new-domain decode, keep
the caller's decoded object and plaintext span unchanged; do not publish a partially parsed header or try
another codec/domain. Encoding failure must not publish success or a new output length; existing scratch
write behavior on failed encoding is not a new whole-codec transactional guarantee.

**B383 — distinguish the existing variable-body contract:** every failed decode leaves the decoded result
object `out` unpublished. Failed authentication returns `auth_failed` and leaves `plaintext_out` untouched.
After a **valid tag**, an old variable-body result can fail semantic validation with `bad_result_code` after
decrypted bytes have already been written into the supplied buffer. The caller owns wiping. Correct the
public comment at `remote_codec.h:314–322` to state these separately; do not claim all failures return
`auth_failed` or preserve plaintext. Preserve old decode ordering, result acceptance, buffer writes and
caller responsibilities—no scrub/rollback buffer, transactional-decode refactor or caller repair.
The new ADMISSION_RESULT is fixed with zero application bytes, so §3's all-failures untouched-buffer
requirement remains specific to that new domain. Its complete logical-output/canary tests remain mandatory.

The old **87 reference literals remain byte-identical**, including TERMINAL 0x06/0x07. Old domains keep their
header, nonce, AAD, key choice and result semantics. Their existing unsafe hypothetical negative-producer
example remains a regression explanation, not a reason to alter those old vectors. Only later target
producers switch full/busy replies to the new domain. Do not tighten the existing protocol-error payload
length or rewrite the old codec as an incidental repair.

## 4. Independent vectors, public-path tests and controls

Extend the **remote-v2-independent-reference** method from Slice-2 evidence §3.7: CPython hashlib plus
PyNaCl/libsodium, explicit byte concatenation, the two existing external primitive anchors before vector
generation. Freeze new expected header/nonce/AAD/tag/complete-body bytes **before consulting production
output**. Preserve the original reference and 87-literal comparison; an extended strict comparison must
reject missing, extra and changed literals. Include complete executable reference source, inputs, versions,
commands and outputs in the codec evidence, not only a temporary path. No new system/project dependency.
Run a one-byte expected-literal corruption in a private copy: comparison must fail with readable diagnostics.

| Obligation | Required executed discriminator |
| --- | --- |
| Allocation/layout | Exhaustive two-direction × 256-ctl table updated only for response 0x50..0x59; slot F/A..E and response 6..15 refuse. Pin authenticated/session-key/fixed/no-seq/no-epoch/header12/overhead28. |
| Complete wire | For each code and permitted request class, compare public encode bytes to independent literals and decode those literals. Include every slot, nontrivial high ID/source bytes, source zero, and zero request ID (legal under the existing codec). |
| Fixed bounds | Every truncated prefix and added suffix refuses; body argument nonempty refuses; output buffer 27 refuses and 28 succeeds; empty plaintext span works. Cover this fixed body in the existing carrier/admission/physical-packing tables without changing their cap authority. |
| Typed semantics | All code bytes, all request-ctl bytes and matching/mismatching slots; positive busy 1/2/4/255, zero busy refused, every nonzero unused detail refused. Independently authenticated invalid tuples prove semantic rejection after a valid tag, rather than merely producing auth_failed. |
| Key/authentication | Missing/wrong-size/wrong-value session key, base-only key, absent source, changed ID/source/header/tag refuse without publication; correct session key succeeds even when an unrelated base key is supplied. No open fallback. |
| Changed notice | Same key/id/source: full notice versus a completed old TERMINAL have different nonces; busy count 2→1 changes nonce; ingress_full for execute/safe/force changes nonce. Identical notice is byte-identical. |
| Nonce and AAD independently | Pin every new header byte and nonce suffix byte separately. Mutate request ctl/code/detail through other valid tuples; each must change nonce and AAD. Tag-only tampering tests cannot by themselves detect an omitted nonce term. |
| Shared helper preservation | New-domain nonce is independent of unused response-seq/epoch/abandoned fields; old base/session/domain vectors and pairwise inequalities remain effective. New fields have no old-domain wire effect. |
| Failure publication | Seed decoded scalar fields and span identities plus plaintext canaries. For malformed, valid-tag-invalid-semantic and bad-tag new messages, compare the complete logical output and buffer, without relying on padding memcmp or selected-field assertions. |
| Existing failure contract (B383) | Rerun the evidence's 24-check old-domain fixture against the final codec: valid-tag invalid TERMINAL `08 d1` / PROTOCOL_ERROR `01 d1` leaves those decrypted bytes, returns `bad_result_code`, and retains the checked decoded sentinels; bad-tag controls leave the buffer untouched. This fixture checks named sentinel fields, not complete-object preservation. The latter follows the source publication point and is explicitly tested for the new domain above. No new native case/PIN change is required for the comment correction alone. |
| Call-site reach | New tests exercise real `remote_body_encode`/`remote_body_decode`, not just helpers. Full native preserves real Node/session/transcript/bootstrap tests. There is no new producer or controller wiring claim. |

Extend existing fixtures/helpers in `test/test_remote_codec.cpp`. Do not copy the codec into a test. The
B379 session proof already established the full→other-slot-rotation→same-key-execution trigger; preserve it.
This slice adds the wire-domain separation proof using real codec calls. New real target notice production,
control recovery, open lifecycle, rate and counters belong to the behavior implementation and remain ungated.

Extend **`radmin2codec`**, preserving its existing controls. New executable mutations must catch: new domain
absent/open/base-key/variable-body; wrong opcode/overhead/field order; each nonce suffix byte independently
omitted or reordered; each AAD field omitted; zero/nonzero detail checks removed; unsupported request/code
or mismatched slot admitted; clear result interpreted as terminal/protocol error; fixed length or cap bypass;
bad-tag/semantic-failure publication; old-domain preimage contaminated by new fields. Use separate public-path
KAT and helper assertions so shared encoder/decoder mistakes cannot cancel. Add controls for the reference
comparator itself; classify those separately from compiled/executed native mutations.

## 5. Edit fence and prediction

Production changes: **`lib/core/remote_codec.h` and `lib/core/remote_codec.cpp` only**. Tests:
`test/test_remote_codec.cpp`. Instrument: `tools/probe_ui_model_mutations.py` for existing codec controls
and independently derived native PIN. Evidence:
**`docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0.md`**, containing the complete independent reference
or an explicitly inventoried reference source beside it. Any added reproduction file is named and hashed.
Expected diff is a small two-file codec extension, additive/retargeted codec tests, one mutation tool and
evidence. No source-file move, production refactor, new compilation unit or simulator source-list edit.

**B382/B383 comment fold-ins:** the introductions in `remote_codec.h` and `test_remote_codec.cpp` still claim
there are no callers. Correct those touched comments to distinguish existing consumers from this new
domain's lack of a producer. Do not carry Slice-2's zero-linked-code prediction into this slice.
Also correct the existing public decoder comment in `remote_codec.h` as specified in §3 (B383), preserving
all old behavior. The separate primitive-comment finding B313 remains outside this edit fence and open.

No edits to `remote_session.*`, `node*`, `src/`, packers, crypto primitives, HAL, NV, scheduling, live counters,
Node/ABI pins, corpus anchors or feature ownership. QA owns the register/design/rulings/MEMORY/brief landing.
Generated inventory may be written by its required instrument, but must reproduce current content exactly;
this fence moves no command-source anchors. Report any unexplained change instead of accepting regeneration.

Prediction, then measured by the coder: no resident state or Node size change on any ABI; board RAM unchanged.
Shared codec functions are already linked on ACCEPT builds, so gateway flash may move. Derive the CLIENT
link reach from its ELF; mobile flash invariance is a prediction, not an excuse to ignore measured movement.
Every flash/section/object delta requires attribution. `lus` recompiles the codec in both variants and may
change as a binary, while **all 36 scenario streams remain byte-identical** and no new remote event airs.
No wire/NV version, inventory semantics, runtime counter or command behavior changes. Native case/assertion
and mutation pins change only by executed, reconciled additions and explicitly retargeted layout assertions.

## 6. Full implementation gate and frozen handoff

This is the gate the coder completes, then QA **reruns independently** after receiving a frozen report.
The pre-check is not a substitute. Use the current tools and standing 7b-2 §8.2 chain:

1. Verify bases, full input inventory and simulator binding to the measured checkout; capture fresh baseline,
   compiler flags, artifact hashes and predictions. No inherited figure is a measured pin.
2. Run the independent reference/anchors/strict old+new comparisons and its corruption control. Run
   `pio test -e native`, then **`./.pio/build/native/program`**, deriving full and per-case arithmetic.
3. Build normal/gateway simulator variants; verify actual source resolution, compilation and executable
   hashes. Run `tools/run_corpus.py` for all 36 with `--require-anchors`, validate both manifests and compare
   base/final streams. Read the live keystone from `simulation/BASELINE.md`; do not re-anchor.
4. Run both ABI instruments with controls; all six standing probes (console-sink, inbox-verbs, firmware-UI,
   custody-USB, BLE-line, features) in controlled default and `--no-neg` modes. Set `MR_LUS_SRC` explicitly
   for an isolated checkout. No source grep alone substitutes for executed codec paths or existing real-TU probes.
5. Deterministic base/final **gateway then heltec_mobile**, sequentially, same fixed identity and private
   paths under that checkout's `.pio-measure/`; preserve pristine ELFs/payloads and attribute every delta.
   No B378 allocation/re-pin in this slice. The warning census's own **six pinned environments** are the sole
   exception to this two-board gate. Record warnings; no new warning or suppressed `-Wreorder` is accepted.
6. Run full tools unittest discovery with a measured real ELF available (no hidden skip), inventory
   `--write`/bare/`--check`, authority checker and six selftests, A0 matrix, DataType literal checker, and
   whitespace/source-integrity checks in both repositories. Verify inventory byte identity.
7. Derive both mutation selectors: changed configured `TARGET_SRC` (expected `radmin2codec`) and the
   dependency/historical acceptance set. **The 47 named batteries in 7b-1 independent QA §4 remain the
   historical floor**, including `b20codec` and real session/transcript/RX callers. Gate their full union with
   all new codec controls; list names, reasons, counts, every baseline, outcome and restored source hash.
   A native mutation must compile, execute and fail its assertion; a build failure is not native RED.

The sole known unusable exception remains `sliceBmac` M04/**B342**, visibly reported and never counted RED.
No new unusable control or subset shortcut is authorized. B312/B315/B350/B359/B364 remain separate limits.
Record failures and corrected reruns; do not silently repair unrelated instruments. Preserve B364's full
suite boundary: filtered output is per-case arithmetic, not a substitute for whole-native coverage.

Required exact report line: **`PIN re-synced? YES — <independently derived base + additions = final>`**.
The durable report identifies brief revision/hash, actual HEAD, every tracked/untracked delivered input,
both source states, exact commands/output, independent vectors and ordering, both mutation selectors,
all board/simulator attribution, limits and freeze. QA then snapshots that full state and issues PASS/HOLD
from its own complete instrument runs. No commit or behavior dispatch is included in a coder recommendation.

## 7. STOP and landing

Standing verbatim 7b-1 STOP:

> Any stream delta — STOP.

Standing verbatim role/base STOP:

>    the brief's named base commit; a mismatch is a STOP to the dispatcher, never a stale-tree measurement or a
>    silent worktree repair.

Also STOP on an unexplained/concurrent input; any changed old vector/preimage; unavailable independent
reference; a new opcode/result/meaning beyond R-RA-36; a live producer or out-of-fence edit; Node/RAM growth;
unattributed binary/flash movement; an invalid field authenticated as a published result; a wrong-key/open
fallback; missing/vacuous/green/unusable required controls beyond B342; lost workers; failed source restoration;
or an unmeasured PIN/inventory change. Report to QA for a concrete correction; do not widen this slice.

On independent PASS, QA records codec readiness and closes B382/B383's touched-comment corrections. **B379 stays
open for real target producer/lifecycle proofs**, and B378 stays open for behavior allocation/rate gates.
Owner commits this codec preparation separately. QA then reissues 7b-2 against that actual successor base.
Frame/protocol/manual replacement remains Slice 9; the ruled design and codec comments stay current meanwhile.
There is no new metal-only behavior or bench part in this codec slice; controller-dependent round trips
remain 8b, and B312's real entropy qualification is not claimed by software tests.

## 8. Independent implementation closure — 2026-09-09

QA gated the complete frozen working state, including uncommitted/untracked inputs, at base `1d4b3ad`.
Native **2888/172264/0**, corpus **36/36 byte-identical**, mutation union **712 RED / one known unusable
B342**, tools **343 OK / zero skips**, both ABI probes, all six controlled/diagnostic probes, inventory/
authority/A0/literal checks, two sequential board pairs and the six-environment census pass. Node/RAM
stay unchanged; gateway flash −64 B is attributed and mobile linked sections are byte-identical.
The [independent report](../evidence/2026-09-09-radmin-slice7b2-0-qa-gate.md) preserves commands, failures,
corrected runs, hashes and limits. B382/B383 close; B384's corrected reference negative-set construction
and B385's discarded stale incremental QA build are recorded. Snapshot overlays preserving old mtimes
require verified recompilation or a fresh simulator build directory; the accepted gate used a fresh build.

The existing 87 literals, final 24-check B383 fixture and new valid-tag semantic controls pass. QA's
134-check real-session/codec proof includes explicitly synthetic admission notices and real sequence-zero
completion. **B378/B379 remain open for behavior implementation and real target lifecycle gates.**
No producer, owner commit, behavior dispatch, simulator edit or bench result is included in this PASS.
