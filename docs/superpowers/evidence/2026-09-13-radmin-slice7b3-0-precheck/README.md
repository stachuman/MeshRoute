# Slice 7b-3-0 author pre-check evidence

Baseline/source pre-check only, at MeshRoute **b9d75aa** and simulator **06746a9**.
See [the pre-check](../../plans/2026-09-13-radmin-slice7b3-0-precheck.md) and
[revision-1 codec brief](../../plans/2026-09-13-radmin-slice7b3-0-action-busy-codec.md).
No production implementation, full gate or B391 closure is claimed.

- `receipt.json`, `inputs-before.json`, `simulator-before.json`: complete input hashes/modes and snapshot
  symlink binding. Private root `/tmp/mr-qa-s7b30-author-x6wfc8ai`; shared checkout preserved.
- `precheck.py`, `results.json`: fresh native build/run and simulator/corpus commands, cwd, exits and hashes.
- `codec-audit.py`, `codec-results.json`: independent reference, controls, consumer audit and real-codec proof.
- `reference-environment.json`: measured CPython/PyNaCl versions and interpreter path.
- `result-domain-proof.cpp`: baseline result-domain/buffer proof; `--allocated` is the future contract and
  correctly fails at today's max 07. No production patch or mock codec.
- `independent-boundary-vectors.json`: five frozen independent new expectations plus hashes of the old 89
  arrays. The existing Slice-2 reference/PyNaCl stack supplies crypto; no production output supplies bytes.
- `mutation-pattern-audit.json`: 52 batteries/815 configured patterns; X09 is the sole zero-match. This is
  source-cardinality evidence, not executed mutation RED. B342 remains separately known unusable.
- `corpus-audit.py`, `corpus-identity.json`, `corpus-manifest.json`: both manifests validate and all 36 fresh
  streams match byte-for-byte; fresh build has 64 actual compiler actions.
- `*.log.gz` and `raw-logs.json`: exact raw logs, compressed to preserve compiler/CMake whitespace. JSON
  command-result log hashes refer to the uncompressed original bytes. Use `gzip -dc` to inspect.
- `document-audit.py`, `preservation.json`, `document-validation.json`, `artifact-sha256.json`: final shared-input and document
  checks plus hashes of this evidence bundle (hash index excludes itself).

The scripts run from the retained private root with `snapshot/`, `logs/` and `corpus/` siblings;
the repository copies retain their actual commands rather than silently changing their provenance. Restore
that layout and the manifest-pinned reference environment to replay them elsewhere. Build products and full
streams remain in the named private root; the durable evidence retains source, commands, logs and identities.
