# 8a+8c revision 4 coder freeze evidence

The authoritative coder receipt is [§7 of the slice evidence](../2026-09-17-radmin-slice8ac.md#7-revision-4-final-coder-freeze-2026-09-18).
This is implementation evidence for independent QA, not an independent QA verdict.

## Inputs and attribution

- Source base: `6086152b97b5934971d231a345b9939d5f2db1e9`.
- Admission HEAD: `e3a5fa03eab11806b05094953a04e7c1524f221a`; its successor changes from the source base were documentation only.
- Brief revision 4: SHA-256 `0d45d7b729958a4061db037a71f7bcac81d8881f75f3fd1b70b4563604f1c27a`.
- `audit/inputs.json` inventories all 2327 initial tracked and untracked input paths. `audit/preparation-inputs.json` pins the six already-dirty QA documents. `freeze-code-inputs.json` pins the final executable/test/tool inputs; `freeze-code-parity.json` verifies their equality with the tested snapshot.
- The full freeze inventory and its receipt hash are generated after the logs and receipt are written. They include the uncommitted implementation and untracked files. A checkout of HEAD alone is insufficient.
- Every initial path was preserved. No commit, reset, clean, stage, or simulator edit was performed.

The candidate authority-table patch is deliberately **unapplied in the shared checkout**: brief §8 R2 assigns that transcription to QA at freeze. The isolated snapshot applies exactly `r42-authority-transcription.patch`; the regenerated 208-row inventory and the three-artifact tests refer to that candidate. QA must land the patch before asserting shared-checkout agreement. No production/test/tool source differs between the shared tree and the tested candidate. Snapshot symlinks are relocated to the same resolved content, not used to import another implementation.

## Reproduction

The archived drivers under `scripts/` record exact commands and the original absolute snapshot paths. Those paths are provenance, not a requirement to reuse the coder's binaries. Recreate a complete isolated checkout from the final inventory, apply the QA table patch, and independently run brief §9. `final-logs/` contains the final probe/checker/tool runs; `union/` contains all selectors, input hashes and per-battery logs; `logs/final-*` contains native, simulator and board runs. Earlier development/checkpoint logs are explicitly historical.

The simulator's stock CMake list does not include the new controller TU. `audit/sim-controller.cmake` is the disclosed build-only source-list hook used to compile the actual `remote_client.cpp` in both core variants without editing the simulator checkout. The configure command and verbose compilation are archived. The hook is needed for this frozen simulator base, not a simulator source change.

`boards/base` and `boards/final` preserve manifests, sections, symbols and build logs. The corresponding original ELFs/payloads remain in `/tmp/mr-s8ac-r4-b1edsao2/{snapshot,board-gate}/.pio-measure/{s8ac-base,s8ac-final2}` and their SHA-256 values are in the manifests. Independent QA should rebuild them. `audit/allocation-attribution.json` uses the final pair; the earlier checkpoint is labelled separately.

For the supplementary crypto proofs, `controller-reference.py` uses independent Python hashlib/libsodium primitives (PyNaCl 1.5.0; complete Python requirements are archived in `final-logs/reference-requirements.txt`); its JSON matches the native controller KAT. `controller-vectors-attempt1-missing-zero-sequence.json` preserves the incorrect historical reference output and is not a passing vector. `low-order-reference.py` independently enumerates all eight canonical torsion points. After building native, run `python3 run-low-order-proof.py /path/to/complete/checkout`: it links the real controller, checks all 264 assertions and runs a private compiled false-success control. It never edits production source.

## Limits and QA landing

The production carrier returns `carrier_unavailable` until 8b. The on-air path in these proofs is a labelled test loopback; there is no radio delivery claim. Metal-only BLE/entropy checks remain for QA's Part 57d landing and the owner. QA retains register/design/bench status ownership and closes B292/B312 only after its independent gate. The brief and all six preparation-document hashes remain unchanged.

Self-review findings fixed within this implementation are detailed in receipt §7: reconnect across local transports, safe/force rollover under full ACK debt, and the stale inventory-test expectations. No new allocation or ruling was needed. The earlier scheduled-detail coverage gap and retargeted source controls are preserved in §6 and the development logs.
