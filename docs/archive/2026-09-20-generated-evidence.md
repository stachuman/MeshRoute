# Historical generated evidence — recovery catalog

The owner requested repository cleanup on 2026-09-20. At the clean starting commit `0795225486c627a0ab834c94ee25e6c0751c80af`, Git tracked 4762 files / 671842282 bytes; historical evidence accounted for 640799357 bytes. This cleanup removes 3631 raw historical files / 626459333 bytes from the working tree across 40 completed remote-admin run directories. All were verified byte-for-byte against that commit before retirement.

The [file inventory](2026-09-20-generated-evidence.jsonl) records each original path, byte count, Git blob ID and SHA-256. Git history is the portable archive: no replacement archive containing another copy of the same large payloads is added. Existing reports, all Markdown within those directories, and directly linked payloads remain in place. Current source, test/tool inputs, simulator anchors, independent reference scripts and the bench-original archive are outside the retirement set. The [retention policy](../superpowers/evidence/README.md) governs new output.

Recover a complete historical bundle into an ignored scratch directory, from the repository root:

```sh
mkdir -p artifacts/recovered
git archive 0795225486c627a0ab834c94ee25e6c0751c80af \
  docs/superpowers/evidence/2026-09-19-radmin-slice10-r3-freeze \
  | tar -x -C artifacts/recovered
```

This restores the original paths below `artifacts/recovered/`, including files that remain in the live tree. To recover a single payload:

```sh
mkdir -p artifacts/recovered
git show 0795225486c627a0ab834c94ee25e6c0751c80af:docs/superpowers/evidence/2026-09-19-radmin-slice10-r3-freeze/native-binary.tar.gz \
  > artifacts/recovered/native-binary.tar.gz
sha256sum artifacts/recovered/native-binary.tar.gz
```

Compare the digest with the inventory before using it. Recovery requires the named commit's objects; a shallow clone may need to fetch that history. Old drivers may embed their original checkout/output paths: recovery is preservation, not a claim that a historical gate runs against today's source.

The local safety copy and old untracked measurement runs are additionally retained outside the checkout at `/home/staszek/MeshRoute-artifacts/2026-09-20-cleanup-0795225/`. That directory's manifest records the moves, including ELFs. `.pio-measure/qa-s10-final/` stays in place so the maintained real-ELF measurement test keeps its prerequisite. The current `.pio/build/` and downloaded dependency caches also remain available. Nothing in this cleanup removes Git objects, resets a checkout, stages changes or commits.

Validation and final size measurements: [cleanup receipt](../superpowers/evidence/2026-09-20-repository-cleanup.md).
