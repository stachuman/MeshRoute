# Slice 7b-3 author pre-check artifacts — 2026-09-13

Read the [author pre-check](../../plans/2026-09-13-radmin-slice7b3-precheck.md) and
[revision-1 brief](../../plans/2026-09-13-radmin-slice7b3-deferred-actions.md).
Source base f993191be7f6980870f440f6032bca72278539a7; simulator 06746a97de5764415d6fcef10b97bca90569b9c7.

These are author measurements, not an implementation gate. `results.json`/raw logs record fresh native and
simulator/corpus commands. `corpus-manifest.json` and `corpus-identity.json` record validated byte identity;
large streams remain at `/tmp/mr-qa-s7b3-author-8srh2_5f/corpus/streams/` and can be regenerated from the pins.
`candidate-layout.cpp`/`candidate-measurements.json` retain the exact compile-only proposal and full real
compiler argv/nm sizes; no production allocation or linked RAM. The policy inventory counts 180 source
entries /48 disruptive, distinct from 204 generated command inventory rows.

`historical-mutation-floor.json` counts/selects 52 batteries /815 patterns but records no full union run.
`pattern-mismatches.json` records X09's only cardinality failure in that floor. `b391-x09-original.log`
is the actual harness failure; `b391-x09-proposed.log` is the RED private proposal. To reproduce the original:

```sh
python3 tools/probe_ui_model_mutations.py --target=radmin5rx X09
```

It must currently exit 1/VACUOUS at the pinned base. The proposal patch is evidence, **not applied** to shared
tools. `b391-proof.py` records how QA tested/restored it privately; the script assumes this retained private
root's complete `snapshot/`. The coder's actual repair and whole-battery/tools gate are still pending.

`inputs-before.json` and `simulator-before.json` capture original bytes/modes/symlinks; `preservation.json`
records permitted QA documentation writes and unchanged production/simulator. `artifact-sha256.json` hashes
all other retained files. Author audit failures (policy count, X09 discovery, wrong stream extension) are
retained alongside corrected runs; see pre-check §7. No failed run is counted as a pass.
