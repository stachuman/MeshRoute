<!-- Coder preflight only. QA authors the reissue and follow-up fences; owner rules allocation and commits. -->
# Slice 7b-3 — parallel typed-plan preflight and B391 instrument repair

**Read-only behavior preflight delivered; 7b-3 behavior remains HOLD.** Executed 2026-09-14 at
`b9d75aaca55a0e350d9707512e9b6eec8a81ab22`, alongside the separately fenced codec implementation.
No behavior brief reissue, runtime fallback, prepared-action allocation, production effect extraction,
Node re-pin, independent QA PASS or owner commit is claimed.

## B389: complete enumeration, not yet a complete prepared allocation

The actual policy table has **48 disruptive rows** across twelve families. The parallel read-only worker
enumerated every row, including aliases, coarse family entries and individual cfg/create arguments, against
real handlers. Source hashes were rechecked by the parent before publishing. The enumeration's inputs remain
byte-identical to b9d75aa; `f993191..b9d75aa` has no `lib`/`src` changes. The concurrent codec edits are not
enumeration inputs. See [full source-backed analysis](2026-09-13-radmin-slice7b3-typed-plan/typed-plan-preflight.md)
and [all 48 policy rows](2026-09-13-radmin-slice7b3-typed-plan/all-48-rows.md).

The important distinction is an **owned parsed request versus an owned, fully prepared action**. Existing
JoinRequest/TeamRequest builders are useful, but public apply services still make mutable decisions, perform
durable transactions and immediately change live state. Retaining a request and calling those services later
would not fulfill the accepted-before-scheduled contract. A stale whole config Blob is also unsafe: the real
ConfigService reloads and merges only covered fields to preserve unrelated channel-counter/key writes.

| Proposed preparation group | Policy rows | Source-backed conclusion |
| --- | ---: | --- |
| reboot/prep aliases and OTA | 4 | No parameter payload; typed effect outcome and explicit sink/backend binding still needed. Existing `ota_start()` is idempotent; do not use the toggling wrapper. Prep reports erase outcome only through Print today; do not parse it to infer typed success. |
| factory reset, sleep, crash | 8 | Small separate C1 grammar/admission/effect extraction proposed. Keep exact confirm, existing sleep/crash prefix grammar, debug/backend checks and reset/wipe order. |
| disruptive cfg, gateway, join/create/leave, team, regen | 36 | Actual prepared delta/transaction services needed. These are **conditional R-RA-39 refusal candidates**, not recorded exceptions or an implemented fallback. QA must list precise rows and fenced follow-ups before behavior coding. |

Prep-restart remains schedulable under R-RA-38; it cannot be silently removed by the fallback. Non-disruptive
team/key verbs remain outside the proposed exceptions. Predictable validation, capacity and generation failures
must occur before scheduled; activation cannot become a routine revalidation refusal. The full analysis names
specific follow-up fences and closure proofs for simple effects, config, provisioning, team and regeneration.
These are recommendations for QA's preparation, not unilateral scope changes.

Fresh **compile-only**, sequential native/ARM/Xtensa pricing uses each real PlatformIO environment and nm.
No board link or linked-RAM claim belongs to this preflight. The existing production Node remains
230896 native /157264 gateway /117912 mobile. JoinRequest is 32/8, TeamRequest 136/8, TeamPlan 128/8;
ProvSnapshot is 40/8 native versus 32/8 boards and contains borrowed pointers, so is not an owned-plan bound.

A deliberately limited **four-row effect-only comparison** measures action 40/8; independent transcript
header 24/8→32/8; two diagnostic bytes plus alignment; complete comparison state 8824/8→8904/8, **+80 bytes**.
This is not a 48-row representation, final proposed scope, Node re-pin or owner allocation request. The final
representation depends on the prepared service/fallback scope still to be authored; **B389 remains open**.

Supporting artifacts are individually hashed in
`2026-09-13-radmin-slice7b3-typed-plan/artifact-sha256.json`; `artifacts.tar.gz` contains the exact scripts,
source audit, full JSON enumeration, candidate TU, real compiler commands/idedata, measurements, logs and
three small ABI objects. `published-sha256.json` binds the archive and readable copies. Raw workspace:
`/tmp/mr-codex-s7b3-typed-plan-ZXuR9w`. Two private setup failures (omitted custom boards, then missing Git
metadata) are disclosed in the archived `measurement-attempts.md`; the corrected three-target run uses the
actual pinned revision override. No shared-tree build or mutation was performed by the enumeration worker.

## B391: separately attributed repair delivered with the codec tool file

The sole production-file consumer edit is **none**: `lib/core/node_mac_rx.cpp` stays byte-identical. X09's
source pattern in `tools/probe_ui_model_mutations.py` no longer depends on B388's correctly retired comment;
it uniquely selects the same `radmin_expiry_arm()` call plus code/gate boundary. The mutation still deletes
that call, never changes the behavior under test to obtain a RED.

The source-cardinality audit moves X09 **0→1 matches** and leaves all other historical matches one. The final
full radmin5rx battery is **14 RED /0 unusable**; X09 compiles and fails **1 assertion /1 match**. Both worker
baselines are the codec candidate's **2912/184461/0** and both restore source MD5
`13aab3b4d2b0d9e36b39d4201790d030`. Before/after tool, search/replacement and unchanged source hashes are
preserved in the codec's `preservation-checkpoint.json`.

The full tools discovery and 52-battery union are part of the concurrent
[codec gate receipt](2026-09-13-radmin-slice7b3-0.md), whose current status is authoritative; this focused
result alone is not a full gate. B391 closure belongs to independent QA. The later behavior reissue waits
for the codec's independent PASS and separate owner commit as well as the remaining allocation/preparation.


## Revision 5 source-validation checkpoint — 2026-09-16 — STOP-1

**Implementation not started.** This checkpoint supersedes the historical dispatch state above without
rewriting its evidence. The actual base is **7442e6f570abdcd74ceed20d4c0cb9e2855d0719**; revision 5 SHA-256
**bfd0fd5dd06595328e925b328c3993a3c00a6dc2a229fe7637869e0a9e776410**. Simulator is
**06746a97de5764415d6fcef10b97bca90569b9c7**, clean and unchanged. No commit prerequisite is imposed.

All **59** dirty preparation files are within the permitted QA set: six modified tracked documents and
53 untracked documentation/evidence files (51 inside the P1 QA directory, its gate markdown, and the retained
revision-4 brief). Every file has an individual SHA-256 inventory; the complete initial checkout inventory
has **1,571** tracked/untracked entries. The archived revision-4 content hash matches the brief exactly.
[Focused evidence and reproduction](2026-09-16-radmin-slice7b3-r5-preflight/README.md).

**STOP-1 — two author fold-ins, proposed B402 for QA registration (no owner allocation change):**

1. **Confirmed anchor disagreement.** Revision 5 §3.2 cites `lib/core/remote_codec.cpp:654` for
   `d.result_detail = payload.subspan(1)`. At this exact base the statement is **line 652**; 654 is blank.
   The decoder behavior is as described. Correct the anchor, preserve codec behavior. Of the **24 focused
   source-anchor checks executed here, 23 match and this one does not**. This is not a complete anchor audit.
2. **Carrier fence clarification needed.** The requested core-owned row embeds P1's `mrfw::ActionPlan`;
   its sole definition is `src/firmware_action_effects.h:18–21` (kind/backend enums at :12/:16), and that
   header imports `firmware_config_parse.h` at :6. Brief §5 simultaneously allows a pure shared carrier,
   forbids core firmware/NV includes, and says to reuse P1 as is without changing it. A concrete route needs
   to be stated: preferably explicitly fence the definition-only extraction of those three types into a
   pure shared header and the P1 header's include adjustment, preserving all names, enum values, two-byte
   layout, admission/effect signatures, wrappers and local behavior. That would be a source-map/fence
   clarification; the coder has not inferred it or edited P1. The original layout model uses separate
   QA kind/backend enums, so it does not prove the requested production include boundary.

The dependency fixture independently compiles the unchanged core baseline (state **8824**, header **24**),
reproduces the expected incomplete-type failure when embedding a merely forward-declared ActionPlan, and
compiles the actual P1 include as the positive control (**ActionPlan 2 bytes**). Compiler dependencies show
both firmware headers. This is a focused language/include proof, **not a broken existing production build,
mutation RED, three-ABI allocation result or software gate**. The required +80 B allocation remains approved;
no additional owned state is proposed. No new owner ruling is requested.

Static policy checks independently confirm **180 entries /48 disruptive**, with all **48** retained disposition
rows matching the current source and the same policy hash (**12 selected /36 refusals**). They do not replace
runtime coverage. B401 remains the permitted future comment fix; it has not been changed here.

**Scope and preservation.** Only this append-only coder receipt and the new focused evidence directory were
added by this turn. Every other input is preserved; production, tests, tools, maintained QA documents and the
simulator are untouched. Registration/reissue is handed back to QA under §5's documentation ownership;
B402 is proposed, not represented as an already-landed register row. The current register's authoritative
next-free marker is B402; its older B401 marker is historical drift, not used to allocate an ID here.

**Not run (D3):** full native suite, corpus, independent reference, ABI/standing probes, tools discovery,
boards, census, mutation union, linker/stack attribution or independent QA. Source-validation stopped before
implementation and remains incomplete. No `PIN re-synced? YES` or implementation freeze is claimed. Resume
source-validation against the corrected author inputs using HEAD plus their SHA-256 inventory; no commit
is required. The +80 B/Node pins and the 36-refusal scope stay unchanged.

## Revision 5 B402 resume — partial implementation, input-change STOP-1 — 2026-09-16

This supersedes the preceding checkpoint's "implementation not started" state. Source-validation resumed
at **7442e6f570abdcd74ceed20d4c0cb9e2855d0719**, with the authorized B402-folded brief SHA-256
**52bd009501f97a25ad05203c722b41910bc7d8885015c8617e8c8c00e8294547**. All **24 focused anchors** match;
core opaque-byte storage and firmware typed conversion fit the fence without changing P1. The complete
tracked/untracked preparation inventory was captured before implementation and copied to a private checkout.

**STOP-1: that brief changed during implementation.** Its live SHA-256 is now
**f1d38f8673a411fff3d4f2d6c280daf96b015b86d13c97b882b6ce33094621c8**. The diff adds R-RA-41's permanent
36-refusal disposition and removes future preparation slices. The runtime contract appears unchanged;
the stop is the explicit base-plus-input-inventory mismatch rule, not a line relocation or a commit wait.
Concurrent maintained/process documentation changes were preserved and inventoried. No extra owner
allocation is requested. QA owns the author pin and maintained status; no new finding ID was assigned.

**Partial implementation is preserved and ungated.** Core/firmware scheduling, the opaque/typed boundary,
five-byte producer, checked ownership, ACK/deadline state, main-loop service, conflict/status plumbing and
B401 comment change are present. Native tests and affected instruments have partial updates. The action
probe compiles the new TU; remote activation through the real applies and the complete required controls
are still to be added. This is not an implementation freeze and is not ready for independent QA.

Fresh measured baseline: native **2916 /184587 /0 /0 skipped**; corpus **36 current anchors validated**;
gateway **203956 RAM /568220 flash**, mobile **207756 RAM /1373576 flash**. Partial implementation native:
**2923 /185326 /0 /0 skipped**. Existing action-probe `--no-neg` pins **150/151/158/158**, **39 local
transcripts each**, pass. No complete final §7 chain or final allocation/link attribution has run. No
`PIN re-synced? YES` claim is made. The initial shared dependency failure and first private native
expectation failures are retained alongside corrected runs.

[Checkpoint, inventories, both brief versions, diff, commands and logs](2026-09-16-radmin-slice7b3-r5-checkpoint/README.md).
Private work remains `/tmp/mr-codex-s7b3-0gt630zl`; pristine base board artifacts are preserved there.
Simulator **06746a97de5764415d6fcef10b97bca90569b9c7** is clean and untouched. Nothing was committed,
reset or cleaned. Resume against a refreshed author input pin; **no commit is required**.

## Revision 5 / R-RA-41 resume — implementation freeze — 2026-09-16

**Implementation complete; coder gate passed; independent QA pending.** This supersedes the two partial
checkpoints above. Nothing was committed, reset or cleaned. No commit prerequisite was imposed. The shared
checkout retains the owner's preparation documents and every pre-existing tracked/untracked input.

Base **7442e6f570abdcd74ceed20d4c0cb9e2855d0719**, authorized revision-5 SHA-256
**f1d38f8673a411fff3d4f2d6c280daf96b015b86d13c97b882b6ce33094621c8**. The explicit re-pin changes only
R-RA-41's disposition: the same 36 rows are permanently refused, with no follow-up slices. The 24 focused
source anchors still agree; implementation was resumed only after the documentation set was re-inventoried.
The complete private checkout includes dirty and untracked implementation, not just HEAD. Simulator
**06746a97de5764415d6fcef10b97bca90569b9c7** remains clean, source-identical and unedited.

[Frozen evidence, inventories, reproduction commands, logs and archived measurements](2026-09-16-radmin-slice7b3-r5-coder-freeze/README.md).

### Behavior and source boundary

One core-owned 40-byte action row stores the complete request/epoch/source/authority identity and opaque
kind/backend bytes. The sole checked firmware conversion preserves P1's two-byte plan and maps invalid
bytes to none. P1's API, extracted handlers/applies, `device_ota`, authority table, codec and independent
vectors are unchanged. Four immutable transcript details produce the existing scheduled terminal with
five-byte plaintext: result plus little-endian delay. Replay survives action consumption/reuse unchanged.

Only checked ownership of the scheduled terminal (`queued` or `parked`) starts the deadline; OUTPUT,
TX pressure, failed sealing/sending and premature ACK do not. Authenticated ACK can make the owned action
eligible early. Timer/RX paths update state only; the ACCEPT main-loop service consumes and clears the
resident/transfer rows before the actual P1 apply. The shared expiry timer and sleep wake-up path are reused.
The service precedes the halt block; mesh RX/execution remain inside it, console/BLE outside it.
B401's misplaced include comment is corrected, with the affected source readers and controls rerun.

The 12 selected policy rows schedule through typed admission; each of the 36 permanent-refusal rows is
exercised through the real command seam and radio/session fixture with zero effects. Target-wide conflict
returns retained typed `action_busy`; force can abandon only same-slot unowned completed work. Armed
promises survive explicit ACL/root invalidation. Admission rejects all non-ready P1 support/grammar/debug
states and invalid 7a timing. Remote OTA uses idempotent entry, including bounded backend OUTPUT.

The real apply probe covers all backends, disabled power saving, real owner extraction, scalar-only output,
mutable input overwrite, ACK/deadline activation, unrelated local halt, local runtime reconstruction, all
factory-erase failure combinations, already-active/failed Wi-Fi OTA, ACL success/no-op/failure/self/role/
last-owner paths and non-returning reset/fault fakes. Synthetic fixtures are labelled: time/phase/status
seeding, external power-loss reconstruction, and real-Node seal/send failure injection. These are software
proofs, not hardware reset, flash, radio-delivery or entropy qualification.

### Measured gate

| Instrument | Fresh result |
|---|---|
| Native wrapper **and executed binary** | Base 2916 cases /184587 assertions; final **2931 /189998 /0 failed /0 skipped** |
| Corpus | **36/36** current anchors; actual base/final stream bytes identical; s18 **32afbf11e43b4bf9d0bd470ad502ba0a**, 269517 events, zero failures |
| Simulator provenance | Fresh normal and gateway compile commands for codec, session, Node and MAC RX; both linked into the rebuilt lus |
| Independent reference | **94/94**, old **89/89** identical; five comparator controls RED; separate first-byte corruption refused |
| ABI probes | **218 checks /9 controls RED** and B278 **42 /6 RED**; no unusable controls |
| Extended action probe | P1 **150/151/158/158**, 39 transcripts each; remote **416/518/534/464**, radio **3160/3485/3689/3695**; **40** compile/assertion RED, four extraction and six placement controls |
| Standing probes | Controlled default and --no-neg for all six; explicit inbox CLIENT controlled arm; full pins in evidence |
| Tools and policy | **351 passed /0 skipped**, real measured ELF; inventory write/bare/check; **204** semantic rows unchanged; authority plus six selftests; A0/literals/whitespace |
| Warning census | All six pinned environments: **173/178/177/177/182/182**, zero `-Wswitch` |
| Mutation union | **56 batteries /880 configured /879 assertion RED /1 known unusable B342**, zero vacuous/missing/other unusable; all restored hashes match |

**PIN re-synced? YES — cases 2916 +15 =2931; assertions 184587 +5411 =189998; failures 0; skips 0.**

The fifteen new case definitions are independently counted by file (2+1+4+1+7); whole-binary baseline/final
outputs derive assertions and the mutation cross-check pins. S is the 20 changed configured TARGET_SRC
batteries; H is all 53 P1 historical/dependency batteries. Their union adds the three new action/conversion/
Node batteries (35+10+5 controls), plus three checked-ownership controls in radmin7rx: 827+53=880.
Each worker derives its own full clean native baseline; every RED compiles, matches once and fails actual
assertions. B342 M04 remains the previously documented ineffective reachable-path control, not an accepted
compile error. X09 remains effective. Full selectors, individual verdicts, worker baselines and restoration
hashes are retained; the interrupted earlier union is not included in the final totals.

Canonical corpus comparison refuses solely because the rebuilt lus hashes differ. Both manifests validate;
the separate byte comparison reads all 36 actual streams. Manifests were not rewritten. Firmware-UI's bare
PASS in --no-neg is the still-open B350 wording defect, never substituted for its controlled gate.

### Allocation, linked attribution and stack

The three ABI measurements agree: row **40/8**, headers **24→32/8**, session **8824→8904/8**.
The allocation is **40 +4×8 +2 diagnostic bytes +6 alignment =80 B**. Node is **230976 native**, **157344
gateway**, **117912 mobile**, all align 8. No extra action-owned resident state, counter, timer ID or NV state.

| Image | Base RAM → final | Base flash → final |
|---|---:|---:|
| Gateway | 203956 → **204036 (+80)** | 568220 → **574752 (+6532)** |
| Mobile | 207756 → **207756 (0)** | 1373576 → **1373604 (+28)** |

Both pairs were measured gateway then mobile, sequentially, under the same deterministic identity and
private paths. Only `g_node` grows as a resident symbol (+80). Gateway `.data` +4 is one additional init-array
entry for the new TU's existing inline console initializer; `.bss` is +76 after a four-byte alignment credit.
Thus linked RAM is +80, and the heap reservation shrinks by 80. Gateway `.text` is +6528; its symbol-extent
union grows 5830 and uncovered bytes grow 698. This accounts for the complete section delta, not only a
sum of overlapping symbol sizes. Mobile has an empty new action TU, no target service or resident action;
its only resized linked symbol is `exec_console_line` (804→826), plus six text gap bytes: +28 flash.

Retained actual-toolchain objects and `.su` files price transients separately. Gateway transfer row 40,
ActionPlan 2, ActionObserver 8, EffectSink 8 and ActivationReport 16 bytes. Native ActionObserver is 16;
these are call-scoped, not additional state. Gateway compiler frames: prepare **144**, service **128**,
status **80**, report **48**. The existing main-loop frame stays **760**, common seam **376**, sender **288**;
terminal encoder **248→256**. P1 apply frames are unchanged, including 152-byte factory/prep frames.
These are individual compiler frames, not a measured whole-call-chain or hardware stack high-water claim.

### Iteration history and limitations

All failed attempts and corrected runs are retained. Early native expectations and new probe fixtures were
corrected before the final run: missing fixture includes, trace noise while debug was enabled, NV baseline
assumptions, impossible-PHY fixture drain timeout, and newly added positive counts. The initial byte-only
sink control stayed GREEN because actual Print output uses the buffer overload; it was replaced with a
meaningful diagnostic-kind control, while the buffer sink escape control remains. No additional ineffective
control was accepted. The console reader now handles a mutated missing authority guard as a failed check;
its old no-scheduled-producer and five-counter-only assumptions were advanced with effective controls.
The standing inbox fixture now expects internal failure for owner preparation without an admitted
transcript, and its +11 check count is derived by label census against a fresh base run. Full tools discovery first
rejected the stale generated inventory anchors; the authorized regeneration preserves all 204 semantic rows,
and complete discovery was repeated after that correction.

A new-TU heavy-context include exposed extra vendored warnings during attribution. The command seam now
passes the configured delay explicitly and the action TU uses the existing pure context header. No P1
API or state changed. A corresponding new binding control is RED; native/board/action/standing runs were
refreshed, and the complete mutation union restarted on the corrected input inventory. The earlier
interrupted chain/union remains historical, not gate evidence. The final six-environment census passes
without a warning re-pin or suppression.

B312/B315/B342/B350/B359/B364 remain separate known limitations. The reserved Part 57b metal check remains
pending, including prep-restart lockout/local recovery, real reset/DFU/OTA/erase/fault effects, radio timing,
and hardware stack behavior. QA owns maintained brief/register/design/bench/ruling/MEMORY status updates;
this receipt does not issue independent PASS, close those limitations or claim metal verification.

**Freeze:** use the complete recorded input inventory, patch and untracked-input archive with base 7442e6f;
the retained private checkout also contains the complete candidate. Source equality between shared and
private implementation inputs is checked at freeze. Coder edits/builds/mutations are finished; independent
QA may now take the frozen candidate. No commit is required before that gate.
