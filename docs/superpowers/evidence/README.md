# Evidence and generated files

Keep gate receipts, findings, compact result tables, source-input hashes and reusable reference/proof sources in Git. Preserve historical verdicts with their original scope and limitations. A receipt is not permission to discard the only copy of a failure capture or flashed ELF.

Write new raw runs under the ignored repository `artifacts/` directory, or a named external artifact directory. Raw runs include build logs, mutation-worker logs, board ELFs/maps, simulator streams, copied checkouts and whole-tree freeze overlays. Do not copy these wholesale into a new evidence directory. The receipt must name their location, exact source snapshot and relevant hashes. Keep flashed images/ELFs and unique fault evidence in a durable owner archive before clearing local build output. An ignored directory is not itself a backup.

Reusable tests, reference programs and fixtures needed by a maintained gate belong under `test/` or `tools/`. Deliberately retained small evidence fixtures remain allowed; `.gitignore` only keeps common raw outputs out by default. Avoid blanket rules that would hide real source or test fixtures.

The [2026-09-20 recovery catalog](../../archive/2026-09-20-generated-evidence.md) covers 40 completed remote-admin run directories. Large payloads now live in their existing Git history at the pinned commit, with a per-file hash inventory and exact recovery commands. Markdown reports and directly linked evidence remain in place. Historical plain-text references to a retired payload resolve through that catalog; a historical driver may require recovering its complete bundle before use. Current command inventory, command authority, A0 matrix and the root reference scripts remain available to their instruments.

This cleanup changes storage and navigation only. It grants no new gate PASS, changes no implementation and does not rewrite Git history. Future cleanup must inventory and verify the exact files first, preserve uncommitted work, and avoid an active build or gate.
