<!-- QA/Author: OpenAI Codex; production coder: separate Codex session -->
# Remote-admin Slice 7b-3-0 — typed action_busy codec preparation — 2026-09-13

**Revision 1 — IMPLEMENTED; INDEPENDENT QA PASS 2026-09-15 (Claude), owner commit `ac5f9a592065d08e7cc8c06ef395e79891d41b34` — see §8.**
The implementation/frozen gate base remains b9d75aa in the historical receipt. Codec/B391 prerequisites are
fulfilled; current behavior dispatch is [revision 4](2026-09-13-radmin-slice7b3-deferred-actions.md), HOLD for
B389 allocation and B394/P1 preparation. The original brief/pre-check/gate wording below is historical.
Historical status line: **Revision 1 — QA/AUTHOR PRE-CHECK PASS; READY FOR CODER SOURCE-VALIDATION.** After source-validation
passes, implement only this codec fence. R-RA-37 authorizes the allocation; no new owner policy call is
needed. This is not implementation QA PASS. **7b-3 behavior remains HOLD** for B389's measured allocation,
this codec's independent gate/owner commit, and B391's verified instrument repair. B390/B392 are ruled,
with their implementation/metal/controller closure obligations still open.

## 1. Exact inputs and order

Base **`b9d75aaca55a0e350d9707512e9b6eec8a81ab22`** (`7b-3 prep`), clean at this pre-check's start.
It is the owner's documentation-only successor to **`f993191be7f6980870f440f6032bca72278539a7`**:
`lib/`, `src/`, `test/`, `tools/`, `simulation/` and `platformio.ini` are unchanged. Simulator
**`06746a97de5764415d6fcef10b97bca90569b9c7`**, clean; no simulator edit is authorized.
The [independent pre-check](2026-09-13-radmin-slice7b3-0-precheck.md) records the complete snapshot,
fresh native/corpus and source-boundary proofs. Every source anchor below must be rechecked by the coder.

Permitted preparation inputs are this brief, its pre-check and
`docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0-precheck/`, plus QA's current register/MEMORY/design
alignment (including design §8.9). These are intentional documentation changes, not production scope.
The committed 7b-3 revision-3 brief is preserved as the enumeration contract. Continuing its source-validation
at `b9d75aa` is authorized after recording the above source equality; this does not reissue its behavior
implementation fence. R-RA-37 requires that reissue only after the separate codec owner commit.

The coder may independently enumerate 7b-3 typed plans and perform the already-authorized **B391** narrow
X09 repair. Its named receipt is `docs/superpowers/evidence/2026-09-13-radmin-slice7b3.md`; inventory any
supporting preflight artifacts individually. If B391's harness repair is present in the codec handoff,
identify it separately, preserve its before/after hashes and include its required checks in §6. It may share
this tool-file diff, but adds no production permission. An unrelated source change or new base returns STOP-1
to QA for reconciliation. Do not silently measure the old base or omit another agent's uncommitted inputs.

Read AGENTS/CODE_GUIDELINES (especially V1, C1, D4–D6), the 2026-09-07 role override, MEMORY/register §0,
design §§8.1, 8.7–8.10, 9, 13, 19.1; R-RA-36–39; 7b-2-0's codec brief/independent report; 7b-2 §8.2 and
its independent QA §9 B391 erratum; 7b-3 revision 3 §§2.2/7; and current `simulation/BASELINE.md`.
QA authors and gates; the separate coder implements/reports; the owner rules, commits and bench-verifies.
Preserve all tracked/untracked work. Never reset, clean or commit. Use a complete isolated snapshot for
builds/mutations while another session edits; HEAD alone is not a frozen implementation.

## 2. Binding allocation and current source

R-RA-37, verbatim:

> **Settled conflict reply:** a distinct conflicting disruptive request receives a retained, immutable **typed
> TERMINAL `action_busy` = `0x08`**, never `refused` + text: the controller must decide "retry with a fresh ID"
> versus "not allowed" from the typed field alone. The busy result is that request ID's final transcript: exact
> retries replay identical bytes, the ID never later becomes `scheduled`, and no action row is occupied.

The ruling also requires a separate codec gate/owner commit before behavior reissue. This slice allocates
the typed result; it does not implement the quoted producer, admission policy or immutable replay lifecycle.
R-RA-37's one-row rule replaces R-RA-22's two for deferred actions, but allocates no row in this slice.
R-RA-38's remote prep-restart lockout/warning and R-RA-39's named-family fallback remain behavior obligations.
B389 enumeration/allocation therefore does not block this consumer-compatible codec preparation.

Design §8.9, verbatim:

> The terminal frame is the next contiguous `response_seq` after the last output frame. Its authenticated
> ciphertext or open plaintext starts with one unsigned one-byte result code.

> Optional short detail bytes may follow, but ordinary handler output must not be duplicated into a special
> terminal encoding. The `scheduled` result is the exception that must carry its bounded activation delay.
> A successful command that prints nothing returns a terminal frame at sequence zero.

| Verified at b9d75aa | Consequence for this slice |
| --- | --- |
| `remote_codec.h:73–85`: `RemoteTerminal` is uint8_t, eight values 00..07; max 07 | Append `action_busy = 0x08`; keep size and all previous numeric values. |
| `remote_codec.cpp:111–112`: max bound to session_busy with an eight-code assertion | Bind the ceiling to action_busy and correct the diagnostic. Do not remove the guard. |
| `remote_codec.cpp:490–545`: encoder accepts arbitrary variable-body result bytes | Preserve this behavior. There is no terminal encoder ceiling to widen or newly enforce. |
| `remote_codec.cpp:637–654`: terminal auth/open share the ceiling; authenticated protocol error only accepts 00; detail is `payload.subspan(1)` | Only terminal 08 changes decoded meaning. Both terminal security classes recognize it; no open disruptive producer/authority is added. |
| `remote_codec.cpp:657`, public decoder comment in `remote_codec.h` | Publish decoded result only on success; preserve B383's valid-tag semantic-failure buffer behavior. |
| `test_remote_codec.cpp:5850–5907`, `:5947–5989` | Existing auth 08 literal is a negative; open 08 is a negative; authenticated protocol-error 08 is separately negative. Retarget only terminal rejection coverage. |
| `remote_codec.cpp:654` and existing scheduled KAT at `test_remote_codec.cpp:5889` | The five-byte scheduled body already decodes with its four detail bytes intact. No scheduled encoder/decoder redesign. |
| `tools/probe_ui_model_mutations.py:10454–10463`: R58/R60 | Update range descriptions; R60's exact-source pattern includes the changing range comment. Preserve its refusal-removal effect with a unique code-based match (D6). |
| `firmware_remote_executor.h:45–55`; `remote_session.cpp:541–558`, `:814–822`; Node wrappers | Existing mapping switches on DispatchOutcome, not an exhaustive RemoteTerminal enum. No current action_busy producer; no caller rewrite is needed from this scan. Re-enumerate consumers before coding. |

The baseline real-codec proof encodes all 256 bytes in each of four response domains. Terminal auth/open
each decode **8 accepted /248 rejected**; authenticated protocol error **1/255**; open protocol error
**256/0**, untyped. It passes **9781 checks**, including failure-buffer and bad-tag controls. Its future
`--allocated` mode fails on today's max 07, as intended. These are baseline facts, not a codec gate.

## 3. Exact change and compatibility boundary

Add `RemoteTerminal::action_busy = 0x08` and set `kRemoteTerminalMax = 0x08`. The shared decoder must publish
typed action_busy for authenticated and open TERMINAL 08. **09..FF remain bad_result_code**. Preserve every
other response/request opcode, slot rule, domain ID, result-kind value, layout, key selection, header,
nonce/AAD preimage, body cap, crypto primitive, failure ordering and decoded-output lifetime.
No wire_version/NV version bump, new field or new response opcode is part of this allocation.

Authenticated PROTOCOL_ERROR **01..FF still reject**, explicitly including 08. Open PROTOCOL_ERROR remains
untyped clear data under its existing rules; it must not acquire action_busy or already_acknowledged semantics.
ADMISSION_RESULT's five codes and tuple validation do not change. Allocating terminal 08 grants no new
command authority, open capability, execution result mapping or admission producer.

The encoder continues to accept arbitrary variable-body bytes, including an empty terminal body or reserved
result byte, subject to its existing envelope/cap/key rules. The decoder still rejects the empty terminal
body and reserved result values. Do not add symmetric encode validation as incidental cleanup.

Use the existing terminal payload shape: result byte plus optional opaque bounded detail. The future busy
producer can use the one-byte plaintext `08`; the codec does not require text, a busy count or activation
delay for it. That one-byte result has an authenticated body of 27 bytes (10-byte header +1 +16-byte tag)
or an open body of 11 bytes (10-byte header +1). Header order stays ctl, request_id LE64, response_seq;
authenticated TERMINAL uses the existing session key. Do not restrict or interpret detail as part of this
slice. The existing scheduled `01` plus
u32 LE delay remains unchanged; its actual session producer belongs to 7b-3.

For fixed key/slot/request ID/response sequence/source, changing the terminal result byte does **not** change
the existing nonce or AAD. Do not append the result to the nonce, introduce an admission-style suffix or
assert nonce inequality between terminal 08 and another terminal result. Alternate-plaintext reference
fixtures are synthetic codec inputs, not authorized live replacements. R-RA-37's retained immutable result
is the later producer's protection against changing plaintext under one terminal nonce.

All **89 existing reference arrays** (original 87 plus two admission arrays) retain their names and bytes.
Reuse `kRefBody_resp_terminal_auth_code08` as the new positive fixture; its historical name is accurate.
Add independently generated `...auth_code09` for the former lower-bound rejection. Keep terminal FF and
**protocol-error code08** negatives. Retarget the constructed open-terminal negative to 09 and add explicit
open 08 success. No global 08→09 substitution, including ciphertext bytes, sentinel values or other domains.

Preserve B383: failed authentication leaves plaintext untouched; valid authentication followed by an invalid
result can leave decrypted bytes in the supplied buffer while the decoded object remains unpublished.
Historical evidence's `08 d1` terminal reproducer stays archived unchanged. Fresh final coverage must use
`09 d1` for that negative, retain protocol-error `01 d1`/`08 d1` negatives and test terminal `08 d1` success.
No wipe/rollback/caller repair. Correct touched eight-code/reserved-range and "ninth terminal" comments to
remain accurate (use count-neutral wording for the separate protocol-error namespace).

## 4. Independent reference and discriminating tests

Extend the existing independent CPython hashlib + PyNaCl/libsodium method. Preserve the original Slice-2
reference embedded in its evidence and the 7b-2-0 reference script unchanged. Add the executable extension
at **`docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0-reference.py`**; it may import the prior reference
through its explicit path and reuse its strict comparator. No production codec/crypto implementation imports.
Run both external primitive anchors before emission, record versions and inputs, and freeze expected bytes
before consulting production output. QA's five independently generated additions are already frozen in the
pre-check's `independent-boundary-vectors.json`:

- `kRefBody_resp_terminal_auth_code09`;
- `kRefBody_resp_terminal_open_code08` and `...open_code09`;
- `kRefBody_resp_terminal_auth_code08_detail` and `...open_code08_detail` (`08 d1`).

These plus the unchanged 89 make a **94-array minimum**, not an inherited final measurement. The extension
must compare the complete old+new set strictly, rejecting changed, missing, extra, duplicate and mis-sized
literals; demonstrate the existing four controls and a separately executed one-byte-corruption refusal.
Preserve an explicit 89-array identity check. The old 89-only strict whole-file command would reject the new
arrays as extras: the extended command supersedes that command for this slice and the later reissued 7b-3
brief. Do not loosen the comparator or delete old expected arrays to make it pass. Record any added vectors
and derive the final count. Reference comparator controls are not compiled native mutation RED counts.

Extend existing native helpers/cases in `test/test_remote_codec.cpp`; do not copy a codec into the tests.

| Obligation | Required executed distinction |
| --- | --- |
| Allocation | Pin all nine enum values and uint8_t size; real decoder returns action_busy, not completed/session_busy, with terminal kind and correct security domain. |
| All result bytes | Auth/open TERMINAL accepts exactly 00..08 (9) and rejects 09..FF (247); auth PROTOCOL_ERROR accepts only 00 (1/255); open error remains untyped. Use valid-tag semantic negatives, not just broken tags. Preserve admission's exhaustive negatives including its code08. |
| Public wire | Encode 08 and compare to independent auth/open literals; decode those literals, empty/detail variants and 09/FF negatives. Check auth slots 0..9 and sentinel F's open behavior; no security fallback. Preserve nontrivial ID/sequence/source and existing cap/carrier/slot matrices. |
| Typed separation | Terminal 08 succeeds while authenticated protocol-error 08 rejects; both 00 meanings and admission metadata remain distinct. Open terminal 08 stays unauthenticated; open protocol-error 08 stays untyped. |
| Existing encoder | Reserved terminal bytes and empty terminal still encode as before; their decoder refusals remain. Do not mistake this asymmetry for a requested defect fix. |
| Failure/publication | Wrong key/source/tag refuses; bad-tag plaintext canaries survive. Valid-tag terminal 09/protocol-error 08 produces bad_result_code, retains decoded sentinels and may write plaintext. Check full logical output preservation for failed calls with existing helpers, not padding memcmp. |
| Detail | Terminal 08 empty/detail views and scheduled four-byte LE detail preserved exactly; no borrowed local scratch or new result-detail interpretation. |
| Runtime compatibility | Full native/probes retain actual Node/session/open/transcript/control tests. Existing callers continue old outcomes; no action_busy, scheduled activation, controller warning or new status producer is claimed. |

Extend **radmin2codec** with executed discriminators for a decoder still capped at 07 and an 08 result
misreported as another terminal. Keep existing domain-collapse, open-authentication, reserved-byte, missing-
result and detail controls effective. Prefer a decoder predicate mutation for the stale ceiling so it compiles
and fails the positive 08 assertion; a static-assert build failure is not a native RED. Audit all range/comment
readers after edits, including R58/R60. Preserve all existing controls and classify source-only checks honestly.
Run the pre-check's real-codec proof in `--allocated` mode against freshly compiled final sources too;
its default mode is a historical baseline expectation, not a final acceptance requirement.

## 5. Edit fence and predictions

Production: **`lib/core/remote_codec.h` and `lib/core/remote_codec.cpp` only**. Tests:
**`test/test_remote_codec.cpp`**. Instrument: **`tools/probe_ui_model_mutations.py`**, for codec controls,
derived native PIN, and the separately attributed B391 repair if not already landed. New independent
reference and named QA/coder reproduction artifacts are evidence, individually inventoried/hashed.
Coder's durable report: **`docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0.md`**.

Expected diff: one appended enum value/ceiling, matching static assertion and touched comments; additive/
retargeted codec tests; narrow mutation-tool changes; reference/evidence. No new production TU, source move,
refactor, production fault hook, temporary feature switch or simulator source-list change. An exhaustive
consumer outside these two production paths needing a change returns STOP-1, not a widened fence.

No `remote_session.*`, Node, firmware, scheduling, state/counters, ACL/NV, packer, transport, authority or
ABI-pin edits. Inventory `--write` must reproduce identical content. QA owns current register/design/MEMORY
and brief updates; the coder preserves these prepared inputs and all historical receipts.

Prediction: all Node/type sizes, resident state and linked RAM unchanged on all three ABIs; gateway flash
may move because the decoder is already linked. Derive mobile link reach from the ELF and attribute any
measured flash/section/object movement. Both simulator variants rebuild; all 36 streams stay byte-identical.
No re-anchor, RAM allocation or B389 re-pin is authorized. Every case/assertion/mutation figure is derived
from executed final results, never copied from this pre-check or a prior handoff.

## 6. Full implementation gate and frozen handoff

Coder completes this gate, then QA independently reruns **every required instrument** after a frozen handoff.
The author pre-check is not a substitute. Use 7b-2 §8.2's standing chain with these explicit updates:

1. Verify actual base, all tracked/untracked inputs, simulator source binding and predictions. Capture fresh
   base/final native builds with `pio test -e native` **then run `./.pio/build/native/program`**. Reconcile
   cases/assertions/failures/skips; filtered tests serve arithmetic only, not whole-suite B364 coverage.
2. Run independent anchors, the extended strict reference and explicit old-89 identity, comparator controls,
   corruption refusal, codec public-path tests and final `--allocated` proof. Inventory all new instruments.
3. Fresh normal/gateway simulator compile/link provenance (B385); all 36 current BASELINE anchors, manifest
   validation and actual base/final byte comparison. If a changed executable hash makes canonical comparison
   refuse, retain that refusal and compare validated actual streams separately; never rewrite manifests.
4. Both ABI instruments and all controls; six probes (console-sink, inbox-verbs, firmware-UI, custody-USB,
   BLE-line, features), controlled default and `--no-neg`. Set `MR_LUS_SRC` explicitly for isolated inputs;
   no-neg is diagnostic, not a replacement controlled gate. Disclose B350.
5. Deterministic base/final **gateway then heltec_mobile**, sequentially, same fixed identity and private
   paths under that checkout's `.pio-measure/`. Preserve pristine ELFs/payloads and attribute every delta.
   No Node/RAM growth or re-pin. Also run the warning census's own **six pinned environments**, the sole
   exception to the two-board role rule. No new warning or suppressed -Wreorder is accepted.
6. Full tools unittest discovery with a measured real ELF available (no hidden skip); inventory `--write`,
   bare, `--check`; authority checker plus six selftests; A0 matrix; DataType literal checker; both repos'
   whitespace/input preservation. Runner pins consumed by strict readers remain bare assignments (D5).
7. Derive **S**, changed configured TARGET_SRC batteries (codec expected), and **H**, historical/dependency
   acceptance. Gate **S ∪ H**: the 49-battery 7b-2 floor plus remoteactivation, fwactivation and macwait,
   **52 batteries /815 configured patterns at this base**, before new controls. Names are frozen in
   `2026-09-13-radmin-slice7b3-precheck/historical-mutation-floor.json`. Explain both selectors, add any new
   dependency, run every selected battery, record each clean worker baseline/outcome/restored source hash.
   These are configured counts, not 814 measured RED. **B391 X09 is currently zero-match/VACUOUS**; repair,
   full radmin5rx, entire union cardinality audit and full tools discovery are required before PASS. QA
   independently verifies B391, whether delivered with preflight or this frozen codec tool input.

Only **sliceBmac M04/B342** retains its known unusable exception, visibly separate and never counted RED.
Every native RED must compile, execute and fail an assertion. No new unusable/vacuous/green control, lost
worker, unexplained count decrease or historical-total shortcut is accepted. B312/B315/B350/B359/B364 remain
separate limits; preserve failed attempts and corrected reruns. No full gate has run for this author draft.

Exact report line: **`PIN re-synced? YES — <independently derived base + additions = final>`**.
The report names revision/hash, actual HEAD, complete tracked/untracked inputs, reference provenance,
commands/output, both selectors, source restoration, board/simulator attribution and known limits, then
declares the freeze. QA snapshots the full delivered state and issues its own PASS/HOLD. No concurrent
shared-tree build/mutation/edit run, inferred owner commit or coder recommendation substitutes for that gate.

## 7. STOP and landing

Standing STOP, verbatim:

> Any stream delta — STOP.

Standing role/base STOP, verbatim:

>    the brief's named base commit; a mismatch is a STOP to the dispatcher, never a stale-tree measurement or a
>    silent worktree repair.

Also STOP on unexplained concurrent inputs, out-of-fence production changes, additional result/opcode
allocation, changed old vector bytes/preimages, unavailable independent reference, new encoder semantics,
protocol-error/admission widening, security fallback, early decoded publication, Node/RAM growth, unmeasured
PIN changes, unattributed binary deltas or missing/ineffective required instruments beyond named B342.
Report the concrete discrepancy to QA; do not invent a fallback or silently broaden this codec slice.

On independent PASS, QA records codec readiness, measured attribution and B391 closure if its return passes.
**B390 remains open** until behavior admission/replay/rollover/release is implemented and gated. B389's
allocation/lifetime proof and B392's Part 57b/8a obligations remain separate. The owner commits this codec
preparation separately; QA then reissues 7b-3 against that exact hash and updated reference/gate baseline.
No new metal-only behavior or bench part is introduced here. Part 57b remains the deferred-action gate;
wire/manual replacement remains Slice 9. Nothing in this brief claims remote scheduling or hardware PASS.

## 8. Independent QA gate — PASS (2026-09-15)

QA re-executed every §6 instrument on the frozen tree (four fenced files hash-identical to the coder's
`freeze.json`): native **2912/184461/0**; extended reference **94/94** + old-89 identity + 5 comparator controls
RED; domain proof `--allocated` **9787 PASS** (terminal 9/247, protocol-error 1/255, open 256/0); simulator
rebuilt (2 codec objects), corpus **36/36 anchors byte-identical**, s18 `32afbf11`/269517/0, zero radmin events;
both ABI probes (Node 230896/117912/157264 unchanged); six probes controlled + `--no-neg` + CLIENT arm; tools
**343 OK / 0 skipped**; inventory 204 byte-identical; authority/A0/literals/whitespace clean; census at pins;
board pair **gateway 203956/570588, heltec_mobile 207756/1372992 — unchanged**; union **52 batteries / 816 RED /
1 known unusable B342 / 817 configured / 0 vacuous**, every worker baseline 2912/184461/0, staged sources
restored. **B391 closed** (X09 RED/1/1). Not independently reproduced: the coder's byte-level ELF attribution
(sizes/objects were). Evidence: `../evidence/2026-09-13-radmin-slice7b3-0-qa-gate.md` + `…-qa/`.
