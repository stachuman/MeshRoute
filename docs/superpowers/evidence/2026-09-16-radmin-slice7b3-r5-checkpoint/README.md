# Revision 5 implementation checkpoint — input changed, STOP-1

This is a partial coder checkpoint, not an implementation freeze or QA PASS.

The initial complete checkout snapshot has HEAD `7442e6f570abdcd74ceed20d4c0cb9e2855d0719`
and brief SHA-256 `52bd009501f97a25ad05203c722b41910bc7d8885015c8617e8c8c00e8294547`.
It includes the uncommitted and untracked preparation documents, not just HEAD. B402's opaque-byte
resolution was source-validated; all 24 focused anchors now match. The earlier 180-policy/48-disposition
source comparison is retained in the preceding preflight directory; it is not runtime coverage.

During implementation the live brief changed to
`f1d38f8673a411fff3d4f2d6c280daf96b015b86d13c97b882b6ce33094621c8`.
`brief-change.diff` shows R-RA-41 making the same 36 refused rows permanent and retiring their follow-ups.
No changed runtime requirement was found in that diff. Nevertheless, the explicit frozen-input mismatch
STOP applies; the coder has not silently substituted this brief for the authorized hash. The concurrently
changed AGENTS/roles instructions retain the base-plus-inventory mismatch STOP while permitting symbol
relocation. No line-number disagreement is being raised here. This is an input-integrity checkpoint,
not a proposed new product defect or owner allocation ruling.

`input-deltas.json` distinguishes every changed path from `inputs-before.json`. External documentation
changes are AGENTS.md, CLAUDE.md, MEMORY.md, tracker.md, the roles document, register, rulings ledger,
behavior brief and design. The coder preserved them. Production/test/instrument changes are the partial
implementation; no P1 definition, local wrapper, simulator, codec or NV implementation was edited.
The initial evidence receipt is unchanged until the appended checkpoint linked here.

## Work preserved

The implementation adds the 40-byte core row, four 32-byte transcript headers and two diagnostic bytes
with alignment (8904-byte session, +80); firmware owns checked typed conversions. It connects preparation,
five-byte scheduled encoding, first checked queued/parked ownership, ACK/deadline state transitions,
main-loop consumption, typed conflict mapping, action status and B401's comment correction. Native
coverage was started, and the existing deferred-actions builder now links the real new action TU.
These descriptions identify the current edits; they are not claims that the full behavior gate passed.

`implementation.patch` and `untracked-source/` preserve these edits; individual current hashes are in
`inputs-at-stop-before-receipt.json`. The private complete checkout and pristine base board artifacts remain
at `/tmp/mr-codex-s7b3-0gt630zl`. New evidence files and the appended receipt are deliberately outside that
pre-receipt inventory; `artifact-sha256.json` binds this evidence directory after writing.

## Executed instruments

- Fresh private baseline: `pio test -e native`, then `./.pio/build/native/program`:
  **2916 cases /184587 assertions /0 failed /0 skipped**.
- Fresh baseline normal/gateway simulator build with verbose compilation and actual private MeshRoute
  source, all **36 anchors**, manifest validation: all commands exit 0. Final corpus/comparison is pending.
- Fresh deterministic baseline board pair, gateway then heltec_mobile: both pass. Manifest-measured
  gateway **203956 RAM /568220 flash**, mobile **207756 RAM /1373576 flash**. Final pair/ABI/link/stack
  attribution is pending; no measured final +80 RAM claim is made.
- Partial implementation native: wrapper plus binary, **2923 /185326 /0 /0 skipped**. The first private
  iteration failed five old-expectation cases; both the failure and corrected run are retained.
- `bash tools/probe_deferred_actions/run.sh --no-neg --out <private-work>/action-iteration-1`:
  existing P1 checks **150 /151 /158 /158**, **39 local transcripts each**, pass. This compiles the new
  action TU but does **not** yet exercise the new remote activation through real applies; that extension
  and controlled run are pending. No claim of 19 freshly reproduced controls is made.
- An initial shared native attempt failed before compilation because the doctest dependency directory
  was missing. Its complete output is retained; dependencies were not repaired. Subsequent builds used
  the private checkout, including every initial tracked/untracked preparation input and synced source edits.

The command JSONs, scripts, logs and probe generated sources are retained. No full tools/ABI/probe/census/
mutation-union/reference/final-board/final-corpus chain has completed. There is no `PIN re-synced? YES`
claim. No independent QA or metal gate was performed. No commit, reset, clean or simulator edit occurred;
simulator HEAD remains `06746a97de5764415d6fcef10b97bca90569b9c7`, clean.

## Remaining work after the author input pin is refreshed

Extend the action probe through actual applies, and add real-Node ownership/refusal/recovery proofs.
Complete all §6 coverage, mutation controls and exact pin derivations; run the entire §7 chain on the
complete final inputs, attribute ABI/RAM/flash/stack, then append the implementation freeze for independent QA.
The current shared implementation is incomplete and ungated. A commit is not a prerequisite.
