<!-- Author: Codex, independent Quality Agent -->
# W1 label-termination brief — independent QA review

**Verdict: HOLD — 2026-09-24.** The production fix and the proposed probe seam are sound. Correct the dispatch,
control and measurement requirements below, then return the changed sections for review. No owner ruling is
requested. This review does not authorize implementation or close B241.

## Reviewed tree

- MeshRoute HEAD: `4a230f4501c6a19d71b9234968bf1389484d8295`.
- Simulator HEAD: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean.
- [Brief](../plans/2026-09-24-standalone-mobile-home-w1-label-termination.md), DRAFT, SHA-256
  `44321c6b6fe66fdbae959344a87f5adab637f8a13e6a10a69cc67778c12dbb7f`.
- Design r2.18 SHA-256: `3eb9bc94e3982ac67ae076c1092c4f5db06adca04d1a1a218ba333a94bdee3ea`.
- All five fenced-file hashes independently match both the brief's table and `git show 4a230f4:<path>`.
- Compared with the completed W1 pre-check inventory, only `MEMORY.md`, the register, the design and `tracker.md`
  changed. Production, tests, tools, configuration and simulation inputs remain byte-identical to that pre-check.

The input hashes, preservation result and focused compile receipts are retained in the companion
[evidence directory](2026-09-24-standalone-mobile-home-w1-brief-review/). The reviewed brief and all other
pre-existing tracked/untracked files were left untouched.

## Required corrections

### WR-1 — MAJOR: the coder's warning census is missing; distinguish the two gates

**Anchor:** brief §4, lines 172–195. It says the coder and QA run the same list, lists only the two board builds,
and contains no warning-census step.

**Authority:** `AGENTS.md:P6` and `docs/2026-09-02-agent-roles.md:110–118` retain the full mutation union and
six-environment warning census in the **coder's** gate; the independent `src`-only gate is native, corpus, boards,
touched batteries and affected probes. The two-board rule explicitly permits the census's own pinned set
(`agent-roles.md:47`). `tools/warning_census.sh` contains six current environment pins.

**Correction:** name the stock warning census and its zero-new-warning/zero-`-Wswitch` requirement in the coder
chain; exempt that instrument's own six environments from the “two envs only” wording. Retain the brief's existing
requirement to derive both mutation selectors and run their full union for the coder. State QA's separate P6
scope, rather than implicitly reducing the coder's chain to QA's list. No new environment or changed warning pin
is authorized by this correction.

The source-file selector really is empty: read-only AST inspection of all **105** `TARGET_SRC` mappings finds no
target among the five fenced files. That does not derive the dependency/historical selector, nor does it demand
running all 105 targets. The coder must name and justify that second set and the union. The draft already asks
for both selectors; the missing executable instrument is the warning census.

### WR-2 — MAJOR: pin the uncommitted authorities and the final freeze

**Anchors:** brief opening lines 7–17; §1 preflight; §5 lines 209–217.

The brief calls the uncommitted documentation “context, not an input,” yet names the uncommitted r2.18 design,
rewritten B241 row and untracked QA pre-check as authorities. Its receipt requires only HEAD and the five hashes
**at the start**. It does not require a final SHA-256 inventory binding the implementation to the reported gates.
Checking only the five editable files also does not classify unrelated dirty inputs elsewhere in the checkout.

**Correction:** inventory the permitted preparation set, including the brief, design, register, tracker, MEMORY,
pre-check and its baseline manifest/evidence; distinguish documentation authorities from executable inputs without
exempting either from provenance. Verify both repositories and all unexpected changes at preflight. At freeze,
record final hashes of the implementation/test/tool inputs, the authorized brief hash, simulator identity/status,
the preserved preparation set and all new evidence paths. Require input stability across the measured chain and
through handoff. Commits remain optional; preservation of existing work remains mandatory.

This follows the owner-ruled commit-plus-inventory protocol (`agent-roles.md:75–94`), not a request for a clean
documentation tree or for a commit. Only the planned implementation files must begin at their agreed source base.

### WR-3 — MAJOR: the control contract and fence contradict the existing instrument

**Anchors:** brief §2.5 line 137 (“Every control must compile and fail on assertions”); §3 line 167
(“every mutation entry, pattern, target or control” is OUT), versus §3 line 157 permitting new probe controls.

`tools/probe_firmware_ui/run.sh:382–397` deliberately supports `must_build=no`; **C0** (`:439–443`) restores
`fw_context.h` and is verified by the resulting compile failure. Thus the blanket assertion-only rule would reject
an existing valid control or invite changing its meaning, which the same brief forbids. The blanket OUT line also
literally prohibits the two newly required `ctl` mutations and any re-anchoring promised by §2.5.

**Correction:** apply the compile-plus-assertion-failure rule to the **two new B241 controls** and existing
`must_build=yes` controls; explicitly preserve C0's intentional `must_build=no` contract. Qualify the OUT fence as
the native mutation batteries' entries/patterns/targets, while permitting the two new firmware-UI `ctl` controls
and necessary, justified preservation of existing readers within the fenced runner. Keep the no-crash/no-vacuity
requirement and require the new controls to fail the intended poisoned-buffer checks, not an unrelated assertion.

**Independent reproduction:** the proposed wrapper compiles the unchanged UI TU in all three probe configurations
under `-Werror`. Applying C0's exact one-match substitution to a disposable source copy and compiling it through
that wrapper exits 1 on missing `RadioLib.h`, as C0 requires. This is an expected compile-negative measurement,
not a failed live gate. Commands and output are in the companion receipts.

### WR-4 — MAJOR: make before/after measurements reproducible before editing

**Anchors:** brief §4.4–§4.5, lines 182–190; §3 line 170; §5 receipt.

The brief requests pre/post probe counts and attributed board RAM/flash/warning deltas, but provides neither a
board baseline nor a baseline-capture step before editing. The QA pre-check expressly did **not** run boards or
the firmware-UI probe. An ordinary build size line is not the fixed-identity measurement required by the existing
B138/B206 procedure (`MEMORY.md`, deterministic board measurement; `tools/measure_board.py`).

**Correction:** require baseline capture before the first implementation edit. Name the stock
`tools/measure_board.py` workflow, the fixed-identity/same-path comparison and repeatability evidence, with the
ruled pair sequential (`pair --jobs=1`, or the equivalent two sequential stock invocations). Bind base and final
measurements to their inputs; retain manifests/logs and attribute any delta. Run source-mutating instruments
separately from builds and coder edits. Permit a named evidence directory, or explicitly identify another durable
artifact location and its hashes; “only the evidence file” must not silently strand the freeze's evidence in
temporary directories.

Capture the unmodified firmware-UI probe's counts before editing as well. Treat “a few bytes of flash at most”
as an unmeasured prediction, not a numeric acceptance bound. `gateway` does not compile the OLED TU; its fixed-build
RAM/flash prediction is unchanged. Measure `heltec_mobile` instead of assigning an invented flash allowance.

## Accepted source and test design

- The core API is a raw counted-byte read; both `push_peer_key_cached` and `peer_store_sync` use all 32 bytes.
  The one C-string boundary is the correct production fence. The invite projection already reserves and appends
  its own terminator and must retain its distinct no-fallback behavior.
- The zero-capacity guard, `cap - 1` copy, terminator at the returned count and unchanged hexadecimal fallback
  describe the intended fix. No core API change, resident state, production hook or helper extraction is needed.
- The generated wrapper is a valid same-TU way to call the anonymous-namespace function without changing
  production linkage. The disposable check compiled the unchanged real source with the runner's `l2`, `v3` and
  BLE-row flags. `nm -C` shows the local adapter and an external trampoline in each object. This proves compile
  feasibility only; no claim is made about the future runner's full linkage, runtime checks or control verdicts.
- The separate BLE compile path is correctly included in the contract. Every mutant must be included from the
  source argument selected by `build_variant`; a wrapper hardwired to the live source would defeat controls.
- Poisoned short names, a long-to-short rename, 14/15/32-byte names, adjacent canaries and the “terminate only at
  capacity end” control address the actual coverage gap. Check bytes with bounded operations before attempting
  any C-string rendering, so the diagnostic itself does not depend on an unterminated read.
- The exact-capacity native guard is necessary: the existing full-name tests use 64-byte buffers. The public
  `on_hash_bind_pubkey` path already reaches the private push producer (`test_node_hashlocate.cpp:1940–1963`),
  so the new full-body proof needs no core test hook.
- Public-seam TEAM, compose/result and alarm-reply checks are appropriate. Match existing screen formatting
  and clamps; W1 does not adopt W4a's identity-format policy. Any claimed unreachable consumer needs a source
  explanation and QA disposition, not an automatic waiver of a required check.
- The r2.18 design diff consistently moves W1 to the UI adapter in the six cited sections. It does not change
  the owner's product rulings. B241 remains an implementation defect until the completed slice passes QA.

## Minor author housekeeping

`tracker.md:35` still says revision 2.17, while the design is r2.18. `MEMORY.md:37` still calls the reviewed design
“a DRAFT” beneath its REVIEWED heading. Align these pointers without turning design review into implementation
authorization. These are wording corrections, not additional product work.

## What ran and what did not

Ran: source/caller and exact-reader inspection; five base-hash comparisons; pre-check inventory comparison;
read-only mutation-target census; **three isolated wrapper object compiles, all exit 0 under `-Werror`**;
**one expected C0 compile-negative reproduction**, one substitution, missing `RadioLib.h` confirmed; preservation
and whitespace checks.

Native/corpus were not rerun for this documentation-only brief review. Their independently executed pre-check
remains **2,950 cases / 195,770 assertions / 0 failed / 0 skipped**, **36/36 corpus anchors**, with unchanged
executable inputs. No new runtime B241 regression, full probe, board build, warning census, mutation union,
tools discovery or metal gate ran. No production/test/tool changes, staging or commits were made.

WR-1–WR-4 are corrections to the proposed brief, recorded here for its author; no new production defect or
register number is asserted. Return the corrected contract/fence/gate/freeze sections for one scoped review;
the accepted fix and wrapper approach need no new design cycle.
