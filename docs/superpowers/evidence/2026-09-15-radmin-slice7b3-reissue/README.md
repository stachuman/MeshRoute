# Revision-4 author evidence — 2026-09-15

This is **author pre-check and compile-only allocation evidence**, not a full implementation gate.
See the [pre-check](../../plans/2026-09-15-radmin-slice7b3-reissue-precheck.md) and
[behavior brief](../../plans/2026-09-13-radmin-slice7b3-deferred-actions.md).
Production base ac5f9a592065d08e7cc8c06ef395e79891d41b34; simulator 06746a97de5764415d6fcef10b97bca90569b9c7.

- receipt.json and the two before manifests identify the complete private snapshot and original modes/hashes.
- precheck.py/results.json contain fresh native/simulator/corpus commands. corpus-audit.py/corpus-identity.json
  and both corpus manifests retain validation and actual stream comparison evidence; large raw streams stay
  in `/tmp/mr-qa-s7b3-r4-zwfe7qky/corpus` and the named prior coder archive.
- source-audit.py checks the preserved coder's 25 source hashes, extracts all 48 dispositions and audits all
  52/817 patterns. It runs the independent reference; no native mutation is executed by that script.
- all-48-dispositions.md/row-dispositions.json enumerate the exact twelve selected and 36 refused rows.
  The typed-plan subdirectory is the unchanged input extracted from the coder's existing evidence archive.
- measure-plan.py, compile TUs, real idedata, model patches and plan-measurements.json describe the complete
  twelve-row proposal. These are **private shadow models**, not production code or approved allocation.
  The initial old-pin compile failed inadvertently; its preserved `*-control` filenames do not make it an
  intentional mutation control. The pre-check explains the corrected private model pin and successful rerun.
- logs.json records hashes/lengths of exact original logs; each logs/*.gz decompresses byte for byte. Model
  patches are likewise losslessly compressed to preserve diff context whitespace. artifact-sha256.json
  hashes the final artifacts other than itself.
- brief-revision-3.md is the unchanged prior brief (SHA f7256bf12d4cf56a7dff23b0db8523470b27de4f31be2306555625a031d9cdd8).
  Original coder and other-reviewer reports are not edited by this author landing.

Executed drivers resolve `snapshot` next to themselves and use the toolchain/simulator/reference paths named
in their commands. They were run from `/tmp/mr-qa-s7b3-r4-zwfe7qky`; do not run them in this evidence directory.
For reproduction, construct a fresh complete snapshot of the pinned inputs in another temporary directory,
copy the drivers and typed-plan input there, create `logs`, and independently validate any retained prior
corpus before comparison. Never run builds or mutations over a coder's changing shared checkout.

preservation.json accounts for all initial inputs, with only the explicitly listed author documents changed;
document-audit.json checks disposition arithmetic, quoted pins, current dispatch and local links. No board
link, census, full tools/probes/domain proof or mutation union is claimed for this author pass.
