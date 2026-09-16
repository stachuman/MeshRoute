# Slice 7b-3 revision 5 — coder freeze

Implementation gate passed; independent QA and metal verification remain pending. The appended receipt in
[the running evidence](../2026-09-13-radmin-slice7b3.md) reports behavior, allocation, exact counts and limitations.
No commit was made or required. Base is `7442e6f570abdcd74ceed20d4c0cb9e2855d0719`; authorized brief is
`f1d38f8673a411fff3d4f2d6c280daf96b015b86d13c97b882b6ce33094621c8`.

## Inputs and reconstruction

- `state.json`, `authorized-brief.md`: actual base, brief, simulator and snapshot identity.
- `resume-inputs.json` / `resume-deltas.json`: complete resume inventory and the re-pin's documentation delta.
- `inputs-at-freeze-before-receipt.json`: every tracked/untracked file before this receipt/artifact output.
  Symlink targets, resolved content hashes and modes are preserved. The evidence being authored is output,
  not a hidden production input; `artifact-sha256.json` covers its landed contents except itself.
- `permitted-preparation.json`: the pre-existing dirty documentation/evidence set, individually hashed.
- `implementation-inputs.json`: every modified/new production, test, tool and PlatformIO input. These bytes
  equal the private gated checkout at freeze, including both new action files and the remote probe rows.
- `input-deltas-since-resume.json`, `status-at-freeze.txt`: all changes, with previous hashes retained.
- `frozen.patch` plus `untracked-inputs.tar.xz`: apply the patch to a separate checkout of the pinned base,
  then extract the untracked archive there. It includes the existing untracked QA preparation and new
  implementation. Do not substitute a HEAD-only checkout. Do not apply to the shared working tree.

The complete gated checkout is retained at `/tmp/mr-codex-s7b3-0gt630zl/gate`; pristine and final board artifacts,
all scratch measurement objects and every iteration remain beside it. QA must use its own private build and
mutation work, or coordinate access to a frozen tree. The coder has finished edits and mutation runs.

## Instruments and archives

`final-gate-index.json` selects the final evidence for each required instrument; earlier attempts do not supply a final result.

`logs/` contains native build/binary runs, all standing probe attempts, controls, reference and corruption,
corpus validation/comparison, census and tool/checker output. The `*-results.json` files preserve argv,
working directory where applicable, exit status, elapsed time and log hashes. `scripts/` retains the exact
orchestration and attribution helpers, including interrupted versions; their absolute scratch paths are
provenance, not a portable path assumption for a new QA run.

`union2/` is the **only final mutation union**. Its `selectors.json` derives S and H; `native-inputs.json`
binds the worker inputs; `results.json` records actual process exits; `summary.json` audits every worker's
clean baseline, each single-match assertion RED and each source restoration. `sliceBmac` exits 1 for B342's
known M04 ineffective control. The orchestrator resumed the remaining batteries on identical inputs after
verifying that precise exception. This is not a swallowed generic failure. `union-interrupted/` is historical.

`action-final3/` and `action-final3-no-neg/` contain the four real-apply variants, logs and control records.
`action-final-summary.json` verifies all source hashes and the 156 P1 transcript identities. Original P1
controls remain effective; the extended controls total 40. `action-attempts.tar.xz` preserves failed and
interrupted attempts plus earlier passes, with their own logs and generated sources; none supplies a final
count. All hardware primitives are labelled fakes and actual owner definitions are extracted, not copied.

`corpus.tar.xz` contains both validated run directories, including input snapshots/lus and all 72 stream
files. `corpus-byte-comparison.json` compares the actual stream bytes. `simulator-compile-provenance.json`
retains both normal/gateway compiler commands for the codec, session, Node and MAC RX. Canonical comparison
refused only the changed lus hash; the refusal is preserved and neither manifest is rewritten.

`measurements.tar.xz` contains `s7b3-base`, `s7b3-final-2` and `stack2`: pristine/final ELFs, payloads, compiler
manifests, complete build logs, sections/symbols, real-toolchain owning-TU objects and `.su` frame measurements.
`resources-final.json` and `elf-attribution2.json` attribute the whole RAM/flash/section delta, symbol extent
unions and gaps; object totals are not mislabeled as linked residency. The mobile action object is empty.
`archives.json` hashes all three archives. Earlier board/stack attempts remain in the retained private directory.

`p1-and-wire-preservation.json` proves source identity of the extracted P1 owners and unchanged API/OTA/
authority/codec inputs. `native-case-additions.json` independently counts the fifteen added case definitions.
`inbox-count-derivation.json` attributes 1363→1374 by executed check label; `inventory-semantics.json` compares
all 204 generated rows independent of source-line movement. The separately corrupted vector is a private
copy, with hashes in `reference-corrupt-result.json`; production expected arrays were never changed.

## Re-run contract

Run the complete brief §7 chain against the reconstructed candidate, deriving each result independently.
The retained helpers record the exact commands used here. In particular:

- Run both `pio test -e native` and `./.pio/build/native/program`; wrapper "0 test cases" is not the result.
- Set `MR_LUS_SRC=/home/staszek/lora-universal-simulator` for private probes. Build fresh normal and gateway
  simulator objects; validate both corpus manifests and all current BASELINE anchors before byte comparison.
- Run both ABI probes with controls; all six standing probes with default controls and --no-neg; the explicit
  inbox CLIENT arm; extended action default and --no-neg. Firmware-UI's --no-neg PASS wording is B350, not a gate.
- Run full tools discovery with a real measured ELF in private `.pio-measure/`; inventory write/bare/check;
  authority plus six selftests; A0; literals; both repositories' source/whitespace integrity.
- Run the committed 7b-3-0 independent reference in the PyNaCl environment, its comparator controls, and a
  separate one-byte-corruption refusal. Keep all 94 production arrays and the old 89 hashes untouched.
- Measure gateway then mobile sequentially, deterministic identity, private `.pio-measure/`; run the six-env
  warning census. Keep the archive's pristine base ELFs immutable.
- Derive and run every S ∪ H battery, not a remembered subset. Count actual worker baselines and effective
  assertion failures. Only named B342 M04 is an accepted unusable exception; no compile/vacuous failures.

Known limits remain B312/B315/B342/B350/B359/B364. Part 57b remains a metal reservation pending software QA
and controller/carrier availability, including prep-restart lockout and local recovery. No hardware, independent
QA, deployment or durable-reset-outcome claim is made by this coder receipt.
