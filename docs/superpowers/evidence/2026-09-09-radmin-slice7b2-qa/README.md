# Slice 7b-2 independent QA artifacts — 2026-09-09

[QA report and sole HOLD item B388](../2026-09-09-radmin-slice7b2-qa-gate.md).
These are this QA session's results, not copies of the coder's gate output.

- `receipt.json`, `inputs-before.json`, `coder-manifest.json`, consumed brief and pre-landing integrity bind all
  1087 tracked/untracked inputs at base `564f460`, including the frozen implementation and QA preparation.
- `measure-results.json` and logs record fresh base/final native, sequential board pairs and separate fresh
  simulator builds/corpora. The deliberate canonical identity refusal is retained. Manifest/stream byte audit
  lives in `corpus-audit.json`; simulator compiler actions/object hashes in `artifact-extra.json`.
- `chain-results.json`, `chain-audit.json` and raw `logs/chain-*` contain all 23 ABI/probe/tools/checker/census
  commands. The two native commands are separately measured; no command output tail substitutes for its exit.
- `union/selectors.json` derives S/H membership and every pattern; all 49 full battery logs plus audit contain
  773 patterns, 772 RED/B342, 164 clean worker baselines and all 56 target restoration hashes.
- Board manifests/sections/compiler state and artifact audits identify the exact pristine ELFs/payloads under
  `/tmp/mr-qa-s7b2-gate-0phs0ag4/measure/.pio-measure/qa-{base,final}/`. Large binaries and full disassembly
  remain there, with hashes; they were never modified for comparison. Mobile image metadata is distinguished
  from byte-identical linked sections. Warning normalization retains both its ANSI-sensitive first attempt
  and corrected result. Raw board build logs are at the manifest paths.
- `b387-pin-reader.py` extracts the actual strict reader and tests disposable runner copies; `b388.cpp`
  executes actual codec/intake/expiry functions using the established fixture provisioning. Its compile/run
  arguments and 15-check result are in `b388-results.json` and logs. This does not claim Node radio/timer execution.
- Reference results retain the original strict vectors, four controls and one-byte corruption refusal.

The archived driver scripts record actual commands and this run's private path layout; do not run them in
this evidence directory. A new gate needs new private copies of the complete current tracked/untracked state,
verified against its own frozen manifest, plus a separately identified committed base. Build normal/gateway
simulator variants into distinct fresh directories. Never overlay old mtimes onto a reused final simulator
build, never run mutations against the shared implementation, and never treat a HEAD-only clone as the freeze.
`qa-integrity.py` describes the pre-landing audit; after QA's documented status edits its original shared-input
equality assertion intentionally no longer applies. `post-landing-integrity.json` names those exact exceptions.

No production/test/tool/coder-receipt/simulator correction is included. The owner commits; the coder owns
B388's comment correction. `artifact-files.json` hashes this retained package except itself; final QA
preservation binds every pre-existing shared input outside the six named QA documentation updates.
