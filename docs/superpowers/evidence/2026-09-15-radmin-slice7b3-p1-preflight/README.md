# P1 source-validation STOP-1 evidence — 2026-09-15

Named source base ac5f9a5; observed checkout c591721c2e09bb4e7583f49e458da9e5155cf822.
See the [coder receipt](../2026-09-15-radmin-slice7b3-p1.md). This is a preflight STOP, not an implementation freeze.

input-receipt.json and inputs-before.json record all 1,459 initial paths, including directory symlinks,
and the seven permitted uncommitted preparation hashes. permitted-preparation.patch.gz preserves the
original uncommitted documentation diff. Simulator's 285 inputs remain unchanged. inventory.py was run
before this evidence landing; the private root is recorded by /tmp/mr-codex-s7b3p1-preflight-active.

factory-proof.py extracts the actual handle_factory_reset body from source and compiles it under fake
Print/inbox/NV/reboot operations. Eight complete output/operation-order comparisons pass. The private
inbox-warning-after-nv control compiles and fails an assertion. These are extracted-function proofs,
not the complete production TU/router, hardware/NV-store behavior or any part of a full P1 implementation gate.
Generated C++ files, commands, hashes and lossless logs are retained. No production mutation occurred.

preservation.json accounts for the final documentation-only landing against every initial input.
artifact-sha256.json hashes these artifacts, excluding itself. Existing owner rulings, coder/QA reports,
production/test/tool inputs and simulator are preserved. No source pin was silently changed.
