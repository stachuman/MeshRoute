# Independent QA evidence for Slice 7b-2-0

See the [QA report](../2026-09-09-radmin-slice7b2-0-qa-gate.md) for the verdict, attribution and limits.
These records preserve the full frozen input manifest, measured commands/results, all 47 mutation logs,
board manifests/flags, independently computed comparisons, and executable focused proofs. Paths in JSON
and logs are the original private QA run locations; they are provenance, not portable destination defaults.
`raw-artifacts.json` hashes the retained full logs/binaries; the large originals remain in the named QA root.

The `qa-*.py` drivers preserve the actual execution recipe and require that root's snapshot layout. In
particular, `qa-measure.py` contains the rejected timestamp-preserving incremental attempt, and the fresh
simulator driver contains an overly narrow expected-exit assertion. Neither failed driver is a fresh gate
recipe to run blindly. The report describes the accepted fresh-build and validated stream comparison.
No bad comparison was used to produce PASS, and no source/manifest was changed to force agreement.

Portable focused reproduction from the repository root after building final native:

```sh
g++ -std=c++20 -DMESHROUTE_NATIVE -Ilib/core -Ilib/monocypher docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-qa/b383.cpp -Wl,--start-group .pio/build/native/lib*/lib*.a -Wl,--end-group -o /tmp/mr-b383-qa
/tmp/mr-b383-qa
g++ -std=c++20 -DMESHROUTE_NATIVE -Ilib/core -Ilib/monocypher docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-qa/b379-codec.cpp -Wl,--start-group .pio/build/native/lib*/lib*.a -Wl,--end-group -o /tmp/mr-b379-codec-qa
/tmp/mr-b379-codec-qa
/home/staszek/mr-slice2-ref/bin/python docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-qa/reference-audit.py docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-reference.py
```

Expected: 24 checks; 134 checks with explicitly synthetic admission notices; 25 valid / 1718 valid-tag
invalid records, with exactly two falsely negative records under the private B384 expression control.
The latter operates in memory and never edits the delivered reference. These codec/session proofs do not
establish a live target admission-result producer, controller behavior or on-metal readiness.
