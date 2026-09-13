<!-- Independent QA/Author: OpenAI Codex, replacing Claude -->
# Remote-admin Slice 7b-3 — author pre-check — 2026-09-13

**Current outcome: revision 2 incorporates the second-read fold-ins; ready for coder source-validation,
implementation HOLD B389/B390/B391/B392.** Sections 1–7 retain revision-1 measurements/proposals; §8
supersedes the two-row/text-conflict recommendations and records source clarifications. No new owner ruling.
[Current brief](2026-09-13-radmin-slice7b3-deferred-actions.md) is the proposed implementation contract.
B389/B390 are explicit allocation/concurrency decisions, not newly granted owner rulings. B391 needs a narrow
instrument repair; QA found no new production runtime defect at the closure commit. No production, native-test,
shared-tool, simulator, anchor or NV edits were made. No commit by QA.

## 1. Exact starting state and independence

MeshRoute **`f993191be7f6980870f440f6032bca72278539a7`**, parent `564f460a3b755a104f146da70457e5c8c68e99b9`,
subject `7b-2`; simulator **`06746a97de5764415d6fcef10b97bca90569b9c7`**. Both clean at start.
QA inventories **1254** tracked/nonignored-untracked files/symlinks with bytes, modes and link targets.
The closure's previous **1240** inputs match after the recorded QA documentation landing; the **14** added
inputs are B388 evidence. In particular `lib/core/node_mac_rx.cpp` SHA-256 is
`4c1bee50d31298ba9ffcb0290ea936ae6a6ae309c5e52f8c847e043d74685eb6` and the coder's 7b-2 receipt remains
`994ef94bdc1298b4d11853094bba8e3c672721bd19a3c9f3b744e369646d8a84`.

Private root: `/tmp/mr-qa-s7b3-author-8srh2_5f`; complete snapshot at `snapshot/`, fresh build at `sim-build/`,
actual corpus streams at `corpus/streams/`. No shared build or mutation. All file/link bytes match the source;
clone normalization of group-write bits on 67 files was detected and corrected only in the private snapshot
before measurement. The initial assertion and correction remain in the retained log. No source byte mismatch
was hidden. The generated candidate is outside production and was never linked.

[Retained artifacts](../evidence/2026-09-13-radmin-slice7b3-precheck/README.md) include exact input maps,
commands/exits/logs, candidate source/toolchain flags/symbol sizes, corpus manifest/comparison, policy inventory,
mutation selection audit and the B391 reproduction/proposal. `preservation.json` identifies the final QA-only
documentation writes; no unlisted initial input changed and no initial input disappeared.

## 2. Fresh baseline, with limits

| Instrument actually executed | Independently observed result |
| --- | --- |
| `pio test -e native`, then actual `program` | **2909 cases /174485 assertions /0 failures /0 skips**. Wrapper output is not the count authority. |
| Fresh CMake Release/Ninja `lus`, verbose | **70 actions /64 compiler actions**, normal and gateway core variants; no stale object overlay. |
| Corpus `--require-anchors`, current validate, prior QA corpus validate, canonical `--compare` | **36/36 current anchors**, zero assertions; both manifests validated, actual stream bytes equal. Canonical comparator succeeds because this fresh binary is byte-identical to QA's earlier final simulator binary. |
| Candidate type compilation, native/gateway/heltec_mobile | Actual env toolchains/flags and nm; sizes below, no link/residency inference. |
| Inventory `--check` and authority checker | Fresh generation matches **204 rows**; ruled table, production header and inventory agree. |
| Actual mutation harness, `--target=radmin5rx X09` | Original: **VACUOUS /0 matches /exit 1**, clean full native worker baseline **2909/174485/0**. Private proposed pattern: **RED /1 failed assertion /1 match /exit 0**, same worker baseline. Shared inputs unchanged and private harness restored. |

Simulator SHA-256 **`862241173963b3c30a501849dea9e5e6184227575225aa8e263f0f5c48d1170a`**;
current `simulation/BASELINE.md` SHA-256
**`71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`**. Compare source:
`/tmp/mr-qa-s7b2-gate-0phs0ag4/corpus-final`, independently produced by QA during 7b-2, not copied coder results.
`corpus-identity.json` records independent byte comparisons for all 36 actual `.ndjson` streams, not just hashes.

This is an **author pre-check**, not the 7b-3 implementation gate or a new full 7b-2 gate. No board links,
full ABI-controlled sweep, six-probe chain, full tools discovery, warning census or complete mutation union
was executed now. Prior linked RAM/flash and 772 RED figures are historical evidence, not fresh measurements;
B391 narrows their attribution at the post-comment closure commit. The future brief explicitly requires the
entire chain after a frozen implementation handoff.

## 3. Source reconstruction and preparation boundary

The brief §1 records current file:line seams; coder must relocate every anchor. The decisive source facts are:

- Both executor and common command seam refuse disruptive remote commands. There is no production deferred
  pool, scheduled-delay terminal producer or action pump. A typed `scheduled` outcome currently maps to error.
- `remote_transcript_complete` releases ingress/body. `remote_transcript_encode` constructs terminal from one
  byte. `RESPONSE_ACK` releases transcript and leaves the acknowledged seen record. Borrowed action arguments
  or borrowed terminal detail would have different lifetimes from their owners.
- 7a's live resolver already derives current PHY/slop timing, validates the persisted raw value and exposes
  all five states. Its maximum scheduled inner is **46 B**, with **5 B** terminal plaintext; consume it.
- `firmware_command_authority.h` contains **180 policy entries**, **48** disruptive. The generated inventory
  has **204** rows across command surfaces; these counts measure different things. `disruptive-inventory.json`
  lists every source policy row/line/authority. RPC command tail is 201 bytes, per R-RA-24′ and `console_line.h`.
- Parameterized disruptive families include radio/config/provisioning commands. The real firmware handlers
  combine validation with persistence/live effects; a fake executor does not prove their pre-activation safety.
  Reuse existing validators and provisioning phases; expose a bounded typed prepared plan with no later
  mutable-policy/validation fallback. If that needs a separate prerequisite slice, return STOP-1 before coding.
- `fw_main.cpp:335` implements a local ESP OTA toggle; remote design is receiver entry. `:423` prep-restart
  sets `g_halted`; `:1372` gates the current timer/executor operating block. Reset/DFU/fault destroy or stop
  pending RAM actions. Those specific effects require the missing concurrency/admission contract.

## 4. B389 — storage pricing, not an approved allocation

`test/radmin_0e_candidate_types.h:255` has the original metadata-only deferred row. QA's candidate includes
full request/epoch/source identity, u64 deadline, frozen u32 delay, role/kind/phase/length and **202 B** owned
command storage. The transcript header independently retains a u32 scheduled detail.

| Type | Native | ARM gateway | Xtensa mobile ABI |
| --- | --- | --- | --- |
| Historical deferred candidate | 24/8 | 24/8 | 24/8 |
| Argument-owning deferred candidate | 248/8 | 248/8 | 248/8 |
| Production → candidate transcript header | 24/8 →32/8 | 24/8 →32/8 | 24/8 →32/8 |
| Production → candidate complete session state | 8824/8 →9352/8 | 8824/8 →9352/8 | 8824/8 →9352/8 |
| Candidate difference | **+528 B** | **+528 B** | **+528 B type cost only** |
| Current production Node | 230896/8 | 157264/8 | 117912/8 |

Two 248-byte rows plus four 8-byte header increases explain +528. No mobile ACCEPT inclusion marker; no new
resident instance anywhere. No replacement Node assertion or linked RAM prediction is authorized by this figure.
A raw owned line is not yet a complete prepared-action representation, especially for generated identity and
provisioning state. The coder's preflight must establish the actual carrier and cost; QA requests the owner's
allocation ruling on that complete measurement. The historical 48-byte pair and R-RA-34's 7b-1 authorization
do not authorize arbitrary new state. The recommended shape keeps two rows and independent immutable detail.

## 5. B390 — acceptance collision and recommendation

R-RA-22 prices two deferred rows but does not settle which simultaneous actions may be promised. A first
planned reset loses a second action; a first halt suppresses the current scheduler block. Neither is an
external, unanticipated power loss. No runtime exploit is claimed: no v2 scheduler is implemented yet.

Brief §2.2 proposes **one outstanding disruptive promise per target across all requesters**, retaining the
ruled two-row storage capacity. Distinct conflicts get an immutable remembered refusal; exact retries never
allocate/re-execute. Safe/force treat active preparation/armed actions as executing; completed unarmed responses retain safe-busy/
force-abandon behavior so an unsendable response cannot become uncancellable work. The alternative
requires a complete compatibility/lifetime matrix. This admission restriction is explicitly for owner review,
not an invented ruling. The brief also makes clock origin, independent action/transcript ownership, premature
ACK, invalidation and main-loop consumption explicit for source-validation.

## 6. B391 — comment-sensitive control missed by the scoped return

The closure source removed the old comment correctly. X09's exact match retained its final old line:

```text
    // to exactly what was already armed.
    radmin_expiry_arm();
```

Source-pattern audit finds **one** mismatch across the historical-plus-timing floor: X09, zero matches.
The actual harness confirms VACUOUS, not RED; baseline is clean. B388's 15-check focused reproduction still
proves the comment's factual correction, but it did not check readers of that comment. QA's assertion that
all gate attribution survived the comment-only return was therefore too broad. The old gate is preserved as
history and the independent report gets an explicit erratum. B388 stays closed; B391 stays open until repair.

A private proposal uses the unique executable/feature-boundary suffix:

```text
    radmin_expiry_arm();
}
#endif

#if MR_FEAT_RADMIN_CLIENT
```

Its replacement removes only the arm call, preserving the comment and surrounding code. Exactly one match;
actual X09 becomes RED with one failed assertion. `b391-proposed-pattern.patch` is evidence, not an applied
shared-tool change. The entire `radmin5rx` battery, all pattern cardinalities and tools discovery remain
required for the coder's narrow repair; the full union remains required after 7b-3 implementation.

Mutation dependency derivation: retain all 49 names from 7b-2, plus `remoteactivation` 22, `fwactivation` 10,
`macwait` 10, yielding **52 batteries /815 patterns** before new action/prepare dependencies. This is a static
selection/count, **not 814 measured RED**. X09 currently fails cardinality and B342 remains known unusable.
The changed-source selector must be independently derived at final implementation and unioned with this floor.

## 7. Author-tool attempts and preservation

The private audit's first policy-count assertion incorrectly equated 180 source policy rows with the 204-row
multi-surface inventory. It was corrected to name/count each surface. Its next assertion exposed real B391;
that failure was retained and converted into the explicit mismatch ledger rather than bypassed as success.
The initial independent stream walk used the wrong `.jsonl` extension; it refused, was changed to the actual
`streams/*.ndjson` files, then compared all 36 byte-for-byte. Canonical corpus validation/comparison had already
passed. Raw failed audit logs and their initial scripts are retained; no failed attempt is a passing instrument.

The B391 proposal changed only the private harness while its disposable worker ran, then restored it byte-for-
byte. All 56 target source files remained unchanged in the harness's own check. The author's candidate is
compile-only. Final preservation checks compare all initial shared input bytes/modes/link targets, allow only
the named QA documentation files, preserve coder evidence and verify simulator HEAD/status/input hashes.

## 8. Revision-2 second-read fold-ins — 2026-09-13

Second read: **PASS with fold-ins for coder source-validation**, not implementation approval. The user changed
only the register, adding B392 and advancing the next free ID to B393. QA snapshots the complete incoming tree
and preserves that register input under `revision-2/`; no production/shared-tool/simulator input changed.
The consumed revision-1 brief SHA-256 is `4c99a305fb44e6bbc05630eab14a1ba4acc497f96b45877edb56681f8f77243a`.

Fresh source reads confirm `remote_codec.cpp:654` preserves scheduled detail, with 0x08 currently unallocated
and refused; `remote_session.cpp:574` is the one-byte producer to extend. Thus scheduled needs no codec slice;
only the **proposed new action_busy code** requires separately gated/committed codec preparation if approved.
Checked queued/parked ownership and saturating deadline APIs remain as pinned. B391's old/proposed patterns
still have 0/1 matches; no mutation or full baseline gate was rerun for documentation fold-ins.

The revised B390 recommendation is target-wide serialization with **one** row and retained typed
`action_busy` (proposed 0x08) instead of refused plus text. This replaces the revision-1 recommendation, not
R-RA-22 itself: owner confirmation is still required. A fresh compile-only one-row comparison measures
**9104/8 complete candidate /+280 B** on native/ARM/Xtensa (248 + four 8-byte header increases). Existing typed
`mrfw::JoinRequest` measures **32/8** on all three. These are type sizes, not linked RAM or a complete union.
`firmware_config.cpp:915–918` uses that request and `firmware_join_service.h:150` supplies its validation;
per-family prepared-plan enumeration still determines B389. The raw-line candidate is a comparison, not an
expected layout or an upper bound for every generated plan. Commands/flags/nm sizes are retained in
`revision-2/measurements.json`; existing full two-row measurements remain untouched.

B389 also records the second reader's optional **owner-approved** partial-family fallback: enumerate exact
rows/seams and register preparation follow-ups, retaining remote refusal until their own slices. It is not
an automatic exception to full coverage, and C1 forbids hiding a broad preparatory refactor in this feature.

Two second-read recovery claims need source/contract precision:

- An unsendable, never-owned terminal is **unarmed**; an armed action has already acquired queued/parked
  ownership and falls back at its frozen deadline even if delivery fails. Same-slot force may abandon unarmed
  work. Another permitted remote owner can invalidate that slot via ACL mutation, subject to self/last-owner
  protections and successful persistence (`firmware_admin_verbs.h:386`, `firmware_admin_acl.h:335/399`,
  `remote_session.cpp:364`). That does not grant another operator cross-slot force or cancel an armed promise.
- Prep-restart stops mesh remote access, but “until a physical power-cycle” is overly specific. Local USB/BLE
  service remains outside `!g_halted` (`fw_main.cpp:1808/1813`); local reboot dispatches the reset wrapper
  (`firmware_commands.cpp:1528`). Recovery requires local restart, hardware reset or power-cycle. B392's
  source finding is valid; its recovery wording is clarified in place without deleting the second-read record.

Revision 2 adds the requested pure scalar ACCEPT status view (including armed state, no sixth counter), and
classifies a deliberate local physical intervention actually pre-empting an armed action as external
interruption/unknown outcome. Neither grants new authority or a cancel API; internal missed/deferred actions
still fail. B392 stays an owner call. Conditional Part 57b and 8a pre-submission warning text are prepared,
with no claim that the current image schedules prep-restart or that any new bench/gate ran.

The revised brief, pre-check, current design/MEMORY/register status and conditional bench reservation are
QA documentation only. The owner-rulings ledger and both coder receipts are unchanged. Final preservation
checks are in `revision-2/preservation.json`; revision-1 receipts remain historical and are not rewritten.

Staging exposed two trailing-space lines emitted by CMake in the original configuration log. The readable
copy is normalized; its exact original bytes are preserved in `revision-2/sim-configure-original.log.gz`,
matching the historical run SHA. The original measurement stays unchanged; staged/unstaged whitespace
checks are repeated after this documentation-only correction.
