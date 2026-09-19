# Slice 9 revision 5 — coder freeze for independent QA

See [receipt §11](../2026-09-18-radmin-slice9.md#11-revision-5-implementation-freeze--ready-for-independent-qa-2026-09-19).
Independent QA and physical bench checks are pending.

Base: `84edd3ebfb08d807645d077269bace145ac2a2ab`; simulator:
`6585649ea5a780f0542b2931853a667be56a5b2b`, clean/unchanged.
Brief revision 5: `6eb070f1ada27081e96d84564a0655a53e3b011f71a43a03109a1aebcb2a2b9e`.

Fresh runs in this return:

- `logs/tools.log`: complete discovery, 349 tests, zero failures/skips. The actual ELF fixture comes from
  this return's stock gateway pair. B425 controls extend the existing source-identity case; no case retired.
- `logs/census.log`: all six derived cells at 171/175/175/175/179/179; zero `-Wswitch`.
- `logs/pair.log`: stock `measure_board.py`, sequential pair, `/usr/bin/git`, ordinary indexes;
  no wrapper, index override or staging. Both manifests contain all eight deletion markers.
- `B425-controls.json`: three private in-memory defects each fail the real unit test. No source file
  was changed by these controls. Changing the manifest deletion list also fails qualification controls.

Each run has a command/result JSON with its exit code, source directory, duration and log SHA-256.
`stock-board-pair.tar.xz` includes full logs, ELFs, payloads, manifests and symbol/section inventories;
`boards/` exposes the small records. `pair-checkpoint-comparison.json` verifies identical resource/symbol
measurements to the earlier candidate. Gateway payload is identical; mobile payload is path-dependent
(B262) and is not claimed identical across scratch directories.

The owner requested scoped reruns. Native, references, corpus, ABI, standing probes, xiao and the full
mutation union were **not rerun**. Their completed results remain in the linked revision-4 checkpoint.
`inherited-gate-input-proof.json` proves their inputs unchanged; it explicitly identifies the five tool
files that differ from that full-chain snapshot. The fresh tools run covers those test/tool changes.

Input identity:

- `preparation-inputs.json` includes the complete preserved candidate and QA documentation changes.
  `resume-qa-delta.json` identifies the five changed QA inputs; production/test/tool inputs were preserved.
- `tools-inputs.json` and `boards-inputs.json` identify each complete tested snapshot, including deletions
  and prior untracked inputs. Internal symlinks are relocated to the same targets within each copy.
- `frozen-primary-inputs.json` contains 361 records; each matches the shared tree and both tested copies.
- `post-run-report-delta.json` identifies the later receipt/register reporting only.
- `freeze-inputs.json` covers the final full tree, excluding only itself and `artifact-sha256.json` to
  avoid circular hashes. The artifact index covers all archived evidence except itself.
- `freeze-overlay.tar.xz` includes every dirty tracked file and every prior untracked input outside this
  evidence directory; `freeze-deletions.json` lists the eight removed files. Apply these to the named base
  in a fresh private checkout to reconstruct the complete uncommitted candidate. Do not overwrite other work.

The copied drivers document this run and its scratch paths. Gates are the checked-in commands in the
result JSONs. All underlying snapshots/builds also remain at `/tmp/mr-s9-r5-6tusdqki`.
Nothing is staged or committed. The stock tool replaces the old private-index workaround. B429 is a
non-blocking count typo in the frozen brief; its explicit six-environment pin table is correct.
