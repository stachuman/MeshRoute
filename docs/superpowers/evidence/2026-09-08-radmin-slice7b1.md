<!-- Coder: OpenAI Codex -->
# Remote-admin v2 Slice 7b-1 — coder preflight and implementation checkpoints, 2026-09-08

**Status: STOP-1 before implementation.** R-RA-34 and the clean-start requirements are satisfied, but
the rev-1 brief has the source/fence disagreements below. This is not a software-completion report or a
QA verdict. No production, tests, tools, inventory, brief, register, bench, design, rulings or simulator
file has been edited by the coder. This evidence file is the sole coder-created repository file.
QA owns the brief corrections and maintained-register landings (M1).

**Latest checkpoint: revision 4 implemented in part; STOP-1 on proposed B374, §8 below.** The
original preflight and prior reviews below are history. This is NOT a software-completion report.

## 1. Observed inputs

- MeshRoute `/home/staszek/MeshRoute`: HEAD `d467787f5e02836ad19b79624d97729c81ebacf8`,
  `slice 7b-1 spec`; parent `1548e01`, `slice 7a activation config`. Initial status was EMPTY,
  checked again before creating this evidence. The brief is committed at this base.
- Brief: `docs/superpowers/plans/2026-09-08-radmin-slice7b1-executor-transcript.md`, rev 1,
  read completely; SHA-256 `e4ea520ad909fac0f256815cdfb8e81ba5ea08ed6798082de36217d7f3ec3b47`.
- Simulator `/home/staszek/lora-universal-simulator`: EMPTY status at
  `06746a97de5764415d6fcef10b97bca90569b9c7`. No rebuild or repository edit in this preflight.
- R-RA-34 verified at `docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md:761`:
  the split and ACCEPT-only pool are authorized; 1776 bytes describes the pool alone; counters and
  padding are additional measured costs; native/gateway Node re-pins come from measurement;
  heltec_mobile Node size AND board RAM stay unchanged. No allocation ruling remains outstanding.
- `docs/CODE_GUIDELINES.md` and the current role block in `docs/2026-09-02-agent-roles.md` read.
  Codex validates, implements and reports; QA authors, gates and lands; owner rules and commits.
- Register next-free checked at `docs/2026-07-30-open-bug-register.md:272`: **B365**.
  B365–B370 below are proposals for QA to land, not already registered rows.

## 2. Proposed findings and requested fold-ins

### B365 / S7b1-C1 — the requested compile-time cap binding calls a runtime function

Brief §4.1 specifies `kRadminChunkBytes = 206` with a `static_assert` against `remote_body_cap`.
The real declaration at `lib/core/remote_codec.h:298` and definition at
`lib/core/remote_codec.cpp:369` are not `constexpr`. The codec also calls the live packer capacity
authority. Its files are outside the implementation fence.

A syntax-only compilation against the actual headers, without creating a source file, reproduced:

```text
exit 1
error: call to non-'constexpr' function 'meshroute::RemoteStatus meshroute::remote_body_cap(const RemoteCarrier&, size_t&)'
lib/core/remote_codec.h:298:28: note: declared here
error: non-constant condition for static assertion
```

Reproducer (compiler stdin):

```sh
g++ -std=c++20 -fsyntax-only -x c++ -I lib/core -I lib/hal -I lib/monocypher/src - <<'CPP'
#include "remote_codec.h"
#include "frame_codec.h"
constexpr bool brief_chunk_bound() {
    meshroute::RemoteCarrier c{};
    c.outer_data_type = meshroute::DATA_TYPE_REMOTE_RESP;
    size_t cap = 0;
    return meshroute::remote_body_cap(c, cap) == meshroute::RemoteStatus::ok
        && cap - meshroute::kRemoteOverheadAuthResponse == 206;
}
static_assert(brief_chunk_bound(), "7b-1 brief chunk bound");
CPP
```

Requested fold-in: keep layout/offset and named-constant arithmetic assertions compile-time, but bind
the storage constant to the live codec in executed native tests, covering the return-carrier matrix.
This is the existing Slice 6 idiom (`lib/console/console_line.h:10`, `test/test_console_line.cpp`).
Runtime admission and chunking must still use the codec's result. Do not silently refactor the codec
or introduce a second capacity authority to satisfy an impossible assertion.

### B366 / S7b1-C2 — the pure executor header cannot own an Arduino Print subclass as specified

Brief §4.2/§4.3/§5 put both the native-tested pure executor and a concrete `Print` subclass in the new
`src/firmware_remote_executor.h`. Native has no Arduino framework or Arduino include path:
`platformio.ini:67` through `:93`. `src/firmware_commands.h:16` obtains `Print` from `<Arduino.h>`;
the host probes instead explicitly supply `tools/probe_board_ui/fakes/Arduino.h`, whose real
`class Print` declaration is at line 71. That shim is not a native-suite include directory.

The established boundary is visible in actual declarations: `src/firmware_admin_verbs.h:52` exposes
`IAdminLines`, while `src/firmware_commands.cpp:290` implements its device `Print` adapter. The new
transcript needs arbitrary byte spans, so its interface must not accidentally inherit a line-only
contract or add formatting bytes.

Requested fold-in: keep the new executor header platform-free, exposing a byte-span sink/interface;
place the concrete Arduino `Print` adapter in the already-fenced `firmware_commands.cpp`. Native
tests drive pure byte capture/order; the inbox probe drives the real adapter through the real seam.
This preserves the design's bounded-Print requirement at the firmware boundary. Do not import a
probe shim into production or add an unlisted native build-matrix edit.

### B367 / S7b1-C3 — the native wiring proof is attributed to a translation unit it does not compile

Brief §6.1 asks the native two-endpoint test for real `status`/`version` output, comparison with
`dispatch("status")`, handler/NV refusal proofs and a counting execution fake in the same list.
`platformio.ini:78` sets `test_build_src = no`; `firmware_commands.cpp` is not linked into that binary.
The real `dispatch` is at `src/firmware_commands.cpp:1430`, its status handler at `:861`, and the
transport-neutral seam at `:1579`. The native fixture can drive the real Node/RX/codec and a fake
executor; it cannot prove that the real firmware handler wrote bytes or was not called.

Brief §6.3 correctly assigns real firmware wiring to the inbox-verbs probe, but currently moves only
`status` and the inbox rows there. The probe's `tools/probe_inbox_verbs/run.sh` compiles the actual
command/inbox translation units with platform fakes and the real core/console libraries.

Requested fold-in: separate the claims explicitly. Native proves the authenticated exchange, pool,
sequence/replay/ACK behavior and pure executor with labeled fake command output. The inbox probe owns
the real-command byte comparison and real handler-not-called checks, including owner `reboot` and
operator `factory_reset confirm`, with discriminatory controls. No fake may be reported as execution
of the real `dispatch("status")`. Keep the client-absence arm.

### B368 / S7b1-C4 — an unknown remote verb maps to refused, not the required unknown_command

The remote policy check at `src/firmware_commands.cpp:1601` runs before the router/parser. A missing
policy returns `DispatchOutcome::refused` plus `RefuseReason::unclassified` (`:1604`–`:1606`).
The existing executed probe explicitly pins this for `unknown-verb`:
`tools/probe_inbox_verbs/probe_main.cpp:1875` and `:1887`–`:1893`.

Brief §4.5 maps all three refusal reasons, including `unclassified`, to terminal `refused`, but §6.1
requires an unknown verb to produce `unknown_command`. Following the specified mapping cannot pass
the specified real-path test. A fake returning `unmatched` would hide that disagreement.

Requested fold-in: distinguish the terminal mapping by the typed refusal reason. Recommend mapping
`refused/unclassified` to `unknown_command`, leaving bad-line and authority refusals as `refused`,
without weakening or bypassing the seam's fail-closed policy check. State this in the mapping table
and prove it through the real seam as well as the pure mapper. This is a requested Author decision,
not a coder-applied semantic change.

### B369 / S7b1-C5 — pool-pressure waiting has no defined continuation across ingress expiry

Brief §4.1 says a request without transcript capacity stays admitted and runs when capacity frees.
The existing ingress deadline remains 300 seconds (`lib/core/remote_session.cpp:447`). At the
deadline, `remote_session_expire` (`:320`–`:332`) releases and wipes the ingress body but retains the
seen row. The exact retry branch (`:701`–`:720`) wipes its decoded scratch and returns a replay
verdict without restoring an ingress body. Thus, once that deadline passes, freeing a transcript
alone cannot run the waiting command: its body is gone and it has no transcript to replay.

This is already pinned by `test/test_remote_session.cpp:805`: the row remains `admitted` after
expiry and a retry still reports `replay_transcript`. A targeted run of the EXISTING native binary:

```text
./.pio/build/native/program --test-case='*radmin-5/E2*' --no-colors=true
test cases: 1 | 1 passed | 0 failed | 2854 skipped
assertions: 13 | 13 passed | 0 failed
```

This is not a fresh native build or a full gate. The source establishes the current behavior; the
targeted execution corroborates its existing test. The liveness problem arises when the new executor
is added under the brief's unqualified wait-and-run promise.

Requested fold-in: settle the never-executed row's lifecycle before implementation. Recommend
retaining the existing scratch deadline and allowing an exact authenticated retry to reacquire
ingress ONLY for `admitted`/no-transcript, never-executed rows, retaining the original tag, source and
return route. After expiry, recovery would require that retry, not merely newly free capacity.
Alternatively specify a different bounded policy explicitly. Do not clear a same-epoch seen row,
evict an unacknowledged transcript, or invent a 7b-2 terminal to fill the gap. Supersede the affected
old assertions visibly and add before/at/after-deadline, pool-exhaustion and no-second-execution tests.

The same pressure proof must cover an OWNER waiter: at `remote_session.cpp:670`–`:679`, owner execute
and ACK currently share the CONTROL ingress row. Keeping a waiting owner there and processing ACK
only after ordinary control admission would prevent ACK from freeing capacity. Make §4.6's
authenticated ACK fast path independent of that reservation, without clearing or overwriting the
waiting owner's ingress. Test both owner and operator pressure, including premature/unknown ACKs.

### B370 / S7b1-C6 — remote ACL execution lacks its authenticating actor; self-protection is bypassed

The newly enabled remote-owner path admits `acl set` and `acl remove` as non-disruptive owner verbs
(`src/firmware_command_authority.h:23`, `:25`). The existing service has the correct self protections:
`AclService::set` checks `actor.present` and `actor.slot` at `src/firmware_admin_acl.h:318`;
`remove` does so at `:343`. `AclActor` exists at `:180` and defaults to no actor.

But the actual router binding calls `acl_verb` without an actor (`src/firmware_commands.cpp:324`),
and that verb calls `acl.set(slot, role)` and `acl.remove(slot)` with the default actor
(`src/firmware_admin_verbs.h:374`, `:390`). `CommandContext` at
`src/firmware_command_context.h:10` carries the role/request ID, not the authenticating slot.
Brief §4.3 retains that context shape and explicitly makes inbox the only context consumer;
§5 excludes other handlers and does not authorize the pure ACL verb header.

With several owners present, merely enabling the specified executor therefore lets a remote owner
remove or demote its own slot: the last-owner check does not substitute for the actor check.
This contradicts design §6.6 (`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md:513`).
It also invalidates the executing request's own session/ingress during the handler. This is a
source-derived first-consumer gap, not a claim that a working over-air executor already exists.

Requested fold-in: carry the authenticated slot as typed context metadata and thread the existing
`AclActor` into the existing ACL service through the real verb path. Explicitly authorize the
additional context consumer, binding/header/test edits, affected mutation dependency and inventory
regeneration if its source anchors move. Preserve local no-actor behavior. Prove, with two owners,
that remote self-removal/self-demotion refuses without writes, epoch changes or transcript loss;
another owner's permitted change still works. Do not fork the ACL rule or silently mark the whole
family disruptive to avoid supplying identity.

Related correction to §4.3/§4.5: a legitimate ACL removal/role change BEFORE dispatch installs a new
epoch and clears that slot's seen/ingress state (`src/firmware_admin_runtime.h:117`–`:123`,
`lib/core/remote_session.cpp:287`–`:290`, `:203`–`:213`). That request normally disappears from the
executor rather than producing a `refused` terminal under its retired session. Distinguish this
production invalidation from a synthetic stale-view refusal; do not retain old session keys merely
to manufacture the table's terminal. The new transcript pool must join the existing invalidation.

## 3. Handoff and verification limits

- Implementation is stopped under brief STOP-1 / V1 / P3. The six proposed rows need QA verification,
  maintained-register landing and brief fold-ins before coding resumes; no additional owner RAM
  ruling is requested by this report.
- Checks performed: clean-start/ancestry/content receipts, source and fence inspection, the
  syntax-only cap diagnostic, and the one existing native expiry case above.
- Not performed: implementation, native rebuild/full native run, simulator rebuild/corpus, ABI
  measurement, probes/controls, tools sweep, inventory generation, warning census, board pair,
  mutation union. No inherited closure figures are presented as re-measured results.
- No PIN re-sync or software-ready claim. No commits. QA-owned files are unchanged.

## 4. Revision 2 review — remaining instructions, 2026-09-08

### 4.1 Receipt and accepted corrections

HEAD remains `d467787f5e02836ad19b79624d97729c81ebacf8`. Revision 2 was read completely;
its SHA-256 is `e22f7da4741e9529a3d0bee4a54d7235e0763826c3aa319e553abb547dbd5abe`.
The register now contains B365–B370; its SHA-256 is
`9ce137953a99326af73e575848f0a2153995022245c7bd6394ecb4f83c904e18`.
Simulator HEAD/status remain `06746a97de5764415d6fcef10b97bca90569b9c7` / EMPTY.

All six principal changes are present: executed cap binding, pure byte sink/device Print adapter,
honest native-versus-probe attribution, typed unknown-command mapping, expiry release limited to
never-executed `admitted`/no-transcript rows, and the authenticated ACL actor. The Author's B369
choice supersedes the coder's suggested reacquisition mechanism in §2; the coder does not substitute
that earlier recommendation for the chosen fresh-admission policy. Executing, completed and
acknowledged records must still retain their replay protection until epoch invalidation.

Actual MeshRoute status has the TWO known QA fold-in edits (brief and register) plus this evidence
file; no implementation file changed. Revision 2's opening still says EMPTY apart from evidence.
Record those exact QA-owned edits as permitted resume inputs if they are intended to ride the coder's
tree; the evidence does not label them an unexplained concurrent source change or claim an empty tree.
No commit or cleanup has been attempted.

### 4.2 B369 remainder — ACK must be able to release capacity while an owner waits

The original report's B369 last paragraph remains unaddressed in §4.6. That section still describes
control-ingress release, without specifying the fast path or ownership of the row being released.
The source at `lib/core/remote_session.cpp:670`–`:679` puts both owner execute and ACK in CONTROL,
and refuses ordinary control admission when that row is occupied.

The failing sequence under that admission order is concrete: four retained transcripts fill the
pool; a fifth OWNER execute occupies CONTROL while waiting for a transcript; an ACK for an earlier
transcript cannot obtain CONTROL, so it cannot free the capacity needed by that waiter. The new
300-second expiry eventually removes the waiter, but does not satisfy §6's execute-after-one-ACK
pressure proof while it is waiting. Simply releasing CONTROL when processing the ACK would instead
discard a different operation's admitted body.

Required fold-in, as requested in rev 1: authenticate and consume RESPONSE_ACK before ordinary
control-ingress reservation, without releasing or changing another operation's ingress. Specify
resource-neutral premature/unknown/duplicate ACK paths and test an OWNER waiter as well as an
operator waiter. Rollover and other unimplemented control domains retain their fenced behavior.
This is a source-derived ordering conflict, not a claimed post-implementation gate failure.

### 4.3 B370 remainder — normal invalidation cannot produce a terminal in a retired session

Revision 2 §4.3 and §4.5 still promise a `refused` terminal when the slot emptied or its role changed
between admission and execution. The original report's related correction remains applicable:
`remote_session_install` at `lib/core/remote_session.cpp:287`–`:290` installs the new epoch then
invalidates the slot; `invalidate_slot` at `:203`–`:213` clears its seen and ingress records.
The new transcript pool is also required to join that invalidation. Such work is no longer available
to the executor, and its old response session is no longer in service.

Required fold-in: describe the actual production outcome as invalidation/no dispatch/no new reply
under the retired session. A defensive stale-view refusal may be tested synthetically, but must not
be presented as an executed production terminal after normal ACL invalidation. Do not preserve a
revoked session or weaken the existing invalidation just to satisfy the stale table row.

### 4.4 B369 proof/fence follow-through — more than one old case relies on expiry retaining admissions

Revision 2 explicitly rewrites the case at `test/test_remote_session.cpp:805` (actually **E2**, not
E5). Other source-verified dependencies on the same old behavior include:

- `test/test_remote_session.cpp:275` / `:288` (I4): expiry frees scratch while constructing three
  retained seen rows for the slot-local/root invalidation proof.
- `:442` / `:459` (C5) and `:527` / `:535` (P3): expiry constructs a full sixteen-row seen pool.
  Under B369 that setup no longer fills the pool; preserving these capacity/control proofs requires
  completed/acknowledged records, not relaxing their expected occupancy.
- `:883` / `:900` (E5): route retention and replay after expiry of an unexecuted admission.
- `test/test_node_remote_session.cpp:584` / `:591`: the real Node timer path still expects the
  unexecuted seen row to survive and the retry to reserve no ingress.

Explicitly authorize the affected old fixture/assertion rewrites in both existing test files, while
preserving each still-valid obligation and visibly superseding the old admission-retention claim.
The current test fence names ACK additions and Node fixture extension, which does not describe the
required Node expiry-case change. No production session-full response is needed: retained executed
records can exercise the existing classifier without crossing into 7b-2's response producers.

### 4.5 Disposition

STOP-1 remains for the outstanding instructions above; these are follow-through on B369/B370,
not six rejected fixes or six newly allocated findings. **B371 remains unused by this report.**
The only coder edit this turn is this evidence update. Source inspections and whitespace checks
were performed; no implementation, build, probe, mutation or full gate was run. QA owns the remaining
brief/register text changes. No owner allocation ruling or commit is requested here.

## 5. Revision 3 — implementation resumed (2026-09-08)

The rev-2 follow-throughs are explicitly resolved in brief §0.2 and the operative sections.
MeshRoute remains at `d467787f5e02836ad19b79624d97729c81ebacf8`; brief revision-3 SHA-256:
`35f8cf8206e2c2205f531ed212180aece75457c4680dcad55fce910e13d3728f`.
The only resume inputs were exactly its permitted set: the QA-edited brief and register, and this
coder evidence file. Simulator remains clean at `06746a97de5764415d6fcef10b97bca90569b9c7`.

Predictions before implementation: no scenario stream changes; no new initialization/RNG activity;
ACCEPT-only transcript state grows native/gateway Node by its measured layout, mobile Node/RAM stay
unchanged; one response enqueue at most per service call, no execution before transcript reservation;
no rollover/open/deferred-action producers. Existing executed replay tombstones are never time-evicted.
The new B369 expiry exception is strictly `admitted` plus no transcript and the matching ingress row.

Fresh native baseline: `pio test -e native`, then the real binary: **2855 / 121999 / 0**, zero skipped.
Logs: `/tmp/mr-s7b1-base-native-build.log`, `/tmp/mr-s7b1-base-native.log`.
Fresh deterministic board baseline captured BEFORE production edits:
`.pio-measure/s7b1-base-d467787/` (pair, jobs=1; 42.7 s), with runner guards passing.
Other working logs/corpus: `/tmp/mr-s7b1-i6VKJo/`.

## 6. Implementation checkpoint — STOP-1, inbox visibility and its fixture (2026-09-08)

**NOT READY FOR THE SLICE GATE.** Implementation resumed under revision 3, then stopped on a
source contradiction in §4.7 while extending the real-handler proof. The three revision-3
follow-throughs remain accepted and implemented; this does not reopen their disposition.
The brief hash is still the revision-3 hash above. No coder edit to the QA-owned brief/register,
no simulator-repository edit and no commit.

### 6.1 Partial implementation and executed checks

Implemented so far: transcript allocation/capture/sealing/replay/ACK release; expiry of never-executed
seen rows; invalidation release; the common checked return-route transmitter; pure main-loop executor
and firmware binding; scoped context/ACL actor; the literal §4.7 inbox filter; native pool/executor/
two-endpoint cases and the authorized old fixture rewrites. **The literal inbox filter is provisional
and must not land: it exhibits B371 below.** The new probe-local radio fake extends the existing radio
fake with byte capture; its end-to-end driver is not yet written.

Fresh native build and actual binary: **2876 cases / 127066 assertions / 0 failed / 0 skipped**,
against baseline **2855 / 121999 / 0**. Logs: `native-node-build.log`, `native-node.log` in the working
log directory above. The added 21 cases comprise 7 pure executor/context, 11 transcript/lifecycle,
and 3 real Node flight cases (counting fake executor, NOT real command handlers). The real flights
cover multi-frame output, silent terminal zero, truncation, exact ciphertext replay and ACK tombstones.

Intermediate errors, not hidden: the first new test used unsupported `REQUIRE` under the repository's
no-exception doctest configuration; corrected to `CHECK` plus explicit early exits. New test includes
and the transcript decode call needed correction; the Node sender needed the explicit monocypher
include. The disruptive-command fixture misspelled `factory_reset`; corrected against the table.
A binary invoked after an earlier failed build was stale and failed that old fixture; it is NOT the
2876-case result. The final checkpoint build and binary both completed successfully.

Compile-and-read Node measurements using `probe_board_abi`'s real per-environment flags:

| Target | Before | Measured now | Delta |
| --- | ---: | ---: | ---: |
| native | 224136 | 225920 | +1784 |
| heltec_mobile | 117912 | 117912 | 0 |
| gateway | 150504 | 152288 | +1784 |

All alignments remain 8. The +1784 is the pool 1776 + counters 4 + tail alignment 4. Pins in `node.h`,
the existing native assertion and the ABI tool were updated only AFTER measurement (R-RA-34).
The full ABI controls and changed-board RAM measurements have NOT run. The scoped firmware context
also has an ACCEPT-only active pointer; its board cost still needs attribution, separately from Node.

The fresh PRE-EDIT simulator/corpus capture passed **36/36 streams, 36/36 anchors**, with current s18
`32afbf11` / 269517 rows from `simulation/BASELINE.md`. This is the BASE capture, not a claim about
the edited core. A point-in-time inbox probe-only compile/run passed its old 370/368 checks before
its new end-to-end rows and final refusal changes; this is neither the completed probe nor a gate.

Still required after the STOP resolves: the remaining Node pressure/pacing/cross-layer/refusal
proofs, the real-handler probe rows and controls, the structural probe rows and controls, all three
mutation batteries and both selectors, the native PIN update, inventory regeneration/check,
simulator rebuild and before/after stream comparison, full standing probes/ABI/checkers/tools sweep,
census, deterministic board pair and symbol attribution. The sealing-error terminal obligation also
remains to be completed/verified; current encode refusal retains the cursor and reports enqueue failure.

### 6.2 Findings proposed for the maintained register (QA landing, M1)

Register re-read: next free B371. The numbers below are **proposals held here for QA**, not claims
that the QA-owned register has already been edited.

**B371 — STOP-1: §4.7's storage-kind filter contradicts the ruled diagnostic view.**

- Brief `docs/superpowers/plans/2026-09-08-radmin-slice7b1-executor-transcript.md:242` says to skip
  every `InboxKind::dm`, but line 243 says custody notices remain streamed.
- `lib/core/inbox.h:26` has only `dm` and `channel`, not a third custody kind.
- `lib/core/inbox.cpp:185` writes E2E receipts as `InboxKind::dm`, type `DATA_TYPE_E2E_ACK`;
  `:198` writes custody reports as `InboxKind::dm`, type `DATA_TYPE_CUSTODY_FAILURE`.
- `lib/console/console_json.cpp:680` renders the latter as the semantic `custody_failure` event;
  the DM-store location does not make it private application text.
- R-RA-32 (`.../2026-09-03-remote-admin-v2-rulings.md:721`) retains diagnostic records and excludes
  direct messages. Its source-facts sentence also needs correction: there is no third custody kind.

Requested Author fold-in: define the remote view by message meaning, not the shared storage kind.
Recommendation: exclude `kind == dm && !inbox_record_is_internal(type)`, reusing the existing
READ classifier at `lib/core/inbox.h:111`; retain receipts, custody diagnostics, channels and end.
Require executed positive rows for both diagnostic types alongside the private-DM negative row,
for remote operator AND owner. Keep the brief's blanket `mark_read dm` / `del_msg dm` refusal:
`mark_read(dm, seq)` advances the shared DM-store cursor (`inbox.cpp:265`), not a per-record
diagnostic cursor. Widening it would affect hidden private messages and is not authorized here.

**B372 — pre-existing probe blind spot: its eight-byte medium truncates every serialized inbox record.**

`tools/probe_inbox_verbs/probe_main.cpp:141` allocates `body[8]`; `:151` clamps every append to eight
bytes while returning success. Production's minimum header is **32 bytes** (`lib/core/inbox.h:50`);
`lib/core/inbox.cpp:45` rejects shorter records before any callback. The existing destruction/count
checks can pass while `pull_inbox` sees no message of either kind. A negative-only no-DM row on that
fixture would therefore prove nothing.

Executed diagnostic (outside the repository): extracted the actual `ProbeStore` class unchanged,
linked it with real `lib/core/inbox.cpp`, then compared it with a full-width candidate using the
named `inbox_record_max_bytes` = 273. Each stored one application DM, one E2E receipt and one channel
record through the REAL producers. Command:

```sh
g++ -std=c++20 -Ilib/core /tmp/mr-s7b1-i6VKJo/inbox-storage-proof.cpp lib/core/inbox.cpp -o /tmp/mr-s7b1-i6VKJo/inbox-storage-proof
/tmp/mr-s7b1-i6VKJo/inbox-storage-proof
```

Exit 0, exact output:

```text
actual-8-byte stored=3 first_length=8
actual-8-byte pulled=0
candidate-273-byte stored=3 first_length=33
 kind=0 type=0 body=1
 kind=0 type=128 body=0
 kind=1 type=0 body=1
candidate-273-byte pulled=3
```

Recommendation for the brief's tools/proof instructions: extend this EXISTING probe medium to
`inbox_record_max_bytes`, refuse oversize instead of clamping, and add a control restoring the
eight-byte truncation which must fail the positive channel/diagnostic rows. Re-derive both probe
arms' existing local-output assertions and pins honestly. This is an instrument correction inside
the named probe directory, not a production inbox format change. The repository fixture remains
untouched pending the fold-in.

**B373 — comment-only correction, already applied within the touched Node API block.**

Base `lib/core/node.h:220` described `admin_session_state()` as exposing no secret. It returns the
entire `RemoteSessionState`, whose `admin_x_secret[32]` is its first member. The replacement comment
explicitly identifies in-process secret-bearing state and forbids rendering it. No accessor behavior,
visibility or layout changes for this correction. The original line remains available in the diff.

### 6.3 Resume contract

QA folds B371's semantic visibility correction and B372's explicit fixture/positive-control proof
into the brief and places the proposed rows in the maintained register. No new owner ruling is
requested: R-RA-32 already states the diagnostic visibility; the contradiction is its translation
to a storage enum. Resume on this existing partial implementation, with the brief/register/evidence
updates explicitly named; do not discard it or mistake the checkpoint native pass for slice completion.

## 7. Revision 4 resume (2026-09-08)

B371/B372 are folded into revision 4; B373 is registered closed. User explicitly authorizes the
partial implementation and QA documentation updates as resume inputs. HEAD remains
`d467787f5e02836ad19b79624d97729c81ebacf8`, simulator clean at `06746a9`; revision-4 brief SHA-256 is
`647641388245458cd181cdad5286aedcdc6525bfa3b857862720effe43ee59bb`.
The QA-owned register, rulings ledger (R-RA-32 source-facts correction) and brief are preserved.
No baseline recapture: the pre-edit native, boards and corpus in §5 remain the attributable base.
The §6 STOP is resolved for implementation; its old findings stay visible as history. Next free B374.

### 7.1 Revision-4 implementation and executed checkpoints

B371's semantic filter now excludes only private application DMs and reuses
`inbox_record_is_internal`; both diagnostic types remain visible. B372's existing medium now uses
`inbox_record_max_bytes`, refuses oversize and has a truncation-restoration control. Executed
operator/owner rows preserve receipts, custody reports, channels and the end record, hide the private
DM, and refuse shared-DM-cursor mutation/deletion. Local output is compared before/after the scope.

The inbox probe now drives the REAL firmware binding, Node, session codec, HAL and radio adapter
against the existing platform fakes. The new probe-local radio extension captures transmitted bytes
and models in-progress reception separately from RX mode. Requests are authenticated flights, not
directly seeded ingress rows. The fixture waits for an actual RTS (including the real DeviceHal's
nonzero origination-jitter timer) before CTS; it checks hop-ACK completion, authenticated ingress
admission, the mandatory terminal, and RESPONSE_ACK's retained tombstone/released transcript.

Real `status`/`version` output matches local dispatch at execution time; unknown/disruptive refusals
do not reach the handler; remote ACL self-remove/self-demote refuse, removing another owner succeeds,
and the last owner remains protected. CLIENT has no executor symbol; ACCEPT has the binding.

New native cases drive owner-CONTROL and operator-GENERAL pool pressure through real Node flights,
prove the waiting body survives an ACK, execute on the next pass, and prove no enqueue attempt when
the actual Node TX queue is full. At cross-layer depths 2/3/4, every decoded OUTPUT/TERMINAL uses
the reversed full path and its own cap. A missing gateway first refuses without moving the cursor;
a real gateway schedule beacon then permits the same retained transcript to progress.

| checkpoint (NOT the complete slice gate) | result / artifact |
| --- | --- |
| latest native build AND real binary | **2882 cases / 127520 assertions / 0 failed / 0 skipped**; `/tmp/mr-s7b1-i6VKJo/native-reseal-build.log`, `native-reseal.log` |
| native derivation | base 2855/121999 → +27 cases, +5521 assertions; includes B374's labelled synthetic nonce/refusal proof below. Mutation harness PIN is NOT yet re-synced; the new batteries remain pending |
| inbox probe, full default run | **ACCEPT 771 checks / 50 controls RED / 0 unusable; CLIENT 378 / 45 RED / 0 unusable; BOTH ARMS PASS**; `/tmp/mr-s7b1-i6VKJo/inbox-r4-controls.log`; source hash restored/unchanged; executor symbol ownership passes both arms |
| inbox pin derivation | ACCEPT old 370 +2 Y7 disruptive refusals +8 shared content/medium rows +17 remote view rows +374 executed radio checks =771; CLIENT old 368+2+8=378. Controls: old 41/44 +1 shared B372 +8 ACCEPT-only wiring controls =50/45 |
| console structural/control extension | **PASS: 82 structural / 146 controls / 0 unusable**, with 720 executed checks, BLE905 and ownership6/3 unchanged; `/tmp/mr-s7b1-i6VKJo/console-r4-complete.log`. Six new rows S77–S82 and ten controls; S51 confines the six new Node links to RemoteTarget while store bindings keep their three links; S55 retains admission plus the new disruptive guard |
| deterministic board checkpoint | `.pio-measure/s7b1-checkpoint-r4`: gateway **198980 RAM / 562796 flash / 285 objects**; heltec_mobile **207756 / 1372992 / 329**. Against §5 base: gateway RAM +1792, flash +7504; mobile RAM **0**, flash +260, object counts unchanged. Both compilation runs and measurement guards PASS; final symbol attribution is still pending |
| ABI checkpoint from §5 | native225920/8, gateway152288/8, heltec117912/8; the +1784 Node growth is pool1776 + counters4 + tail alignment4. Full ABI probe/control re-run remains pending |
| simulator / whitespace | simulator still clean at `06746a9`; no edited-core rebuild/corpus gate yet. `git diff --check` passes |

Intermediate fixture failures were not scored as passes: missing ACL count, offering CTS before the
real origination timer, and the shared radio fake's mode-versus-packet distinction were corrected
inside the new fixture. Early native test additions failed to compile because of `REQUIRE` with
exceptions disabled, mixed `auto` declarators, and a missing `<cstdint>` include; all corrected in
the new tests before the real binary passed. The first console runs refused an over-specific scope
pattern and a stale inverted-admission control anchor; their failed logs are retained under the
same checkpoint directory. No production fallback or timing change was introduced for these fixes.
After the inbox run, only its new radio header's opening comment was corrected to describe both
capture and carrier-sense modelling; the measured behavior was unchanged. These are checkpoints,
not a frozen final gate manifest.

## 8. STOP-1 — proposed B374: encode-failure terminal mapping needs an immutable-result boundary

This is a **brief clarification**, not a demonstrated production nonce-reuse vulnerability. The
partial implementation currently preserves the retained transcript on an encode refusal; it does
NOT implement the unsafe substitution described below. The fault injection is synthetic: the
production sender uses a full-size stack buffer, not the deliberately undersized test buffer.

### 8.1 Verified disagreement

- Brief §4.5, `.../2026-09-08-radmin-slice7b1-executor-transcript.md:235`, maps
  `execution or sealing failed after a truthful result existed (encode error)` to `internal_error`.
  It does not limit this conversion to a pre-publication phase.
- The same brief's §6 (`:328`) requires byte-identical replay, and design §10 (`:1147`) requires
  resending the original transcript from sequence zero, without dispatching again.
- `lib/core/remote_session.cpp:560` seals from retained plaintext every time, including retries.
  `lib/core/node_mac_rx.cpp:2119` uses that encoder for both original and replay frames; a successful
  queued OR parked admission advances the cursor. A parked copy already belongs to the transport,
  so lack of observed radio airtime is not proof that its plaintext can still change.
- `lib/core/remote_codec.cpp:277` derives the nonce from key/domain/opcode/slot/request/sequence/source
  (and epoch for its specified domains). The terminal result byte is NOT a nonce input. Therefore
  changing a previously sealed TERMINAL from `completed` to `internal_error` at the same sequence
  uses the same key and nonce for different plaintext and violates the frozen replay obligation.
- Changing the terminal during a replay can also change an already retained transcript even where
  the error happened while encoding an earlier OUTPUT; a send-time error must not silently replace
  a result the transport/controller may already hold.

### 8.2 Executed, discriminating proof

`test/test_remote_transcript.cpp:245` is labelled SYNTHETIC and uses the existing Pool fixture and
REAL codec. It completes a silent request, seals TERMINAL seq0, advances its send cursor, receives
an exact retry, then forces `bad_buffer` with a one-byte destination. It verifies zero output and
byte-identical original terminal on a later full-buffer encode.

On a SEPARATE copied state only, it substitutes `internal_error`. Both resulting frames authenticate
and decode through the real decoder, one as `completed` (0x00), the other as `internal_error` (0x05).
Their ciphertexts differ, while `remote_nonce` returns identical 24-byte nonces. All assertions pass
in the native checkpoint above. No production transcript is overwritten by this proof.

### 8.3 Requested Author fold-in and resume boundary

Recommendation: retain the existing execution-outcome mapping to `internal_error` at completion,
but make every later sealing/encode refusal resource-neutral for the result: preserve plaintext,
terminal, frame count and cursor; emit/count the failure; retry only the original frozen bytes.
Ordinary slot/root invalidation still abandons and wipes the retired-session work as already ruled.

If the Author instead intends a pre-publication sealing-validation stage that can freeze an
`internal_error` terminal BEFORE any frame is handed to the transport, specify that stage and its
proof obligations explicitly; once any response is queued/parked, no replacement terminal is allowed.
No opcode, nonce or wire-format change is proposed here.

Per M1/roles, QA places B374 in the maintained register and clarifies §4.5; the coder does not edit
the register or brief. Resume on this partial tree with the named QA document changes. Nothing was
committed. Remaining work after the fold-in: finish the sealing-error obligation, add/execute the
three new mutation batteries and derive BOTH selectors/union, synchronize native PIN, regenerate
inventory anchors, run the remaining standing probes/ABI controls/tools sweep/checkers/census,
rebuild both simulator variants and reproduce all 36 current anchors, and complete final board
symbol attribution. These checkpoints do not authorize dispatching a subsequent slice.
