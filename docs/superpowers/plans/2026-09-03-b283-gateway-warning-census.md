<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B283 — add one nRF52/gateway cell to the warning census · dispatch brief · 2026-09-03

**Status: DRAFT — awaiting Quality-Agent review.**  Dispatch model after PASS: **Opus**.
Authority: owner ruling recorded in register row B283 and R-S5-4 in
`docs/superpowers/plans/2026-09-03-b278-s5-precheck.md`. Pre-check input:
`docs/superpowers/plans/2026-09-03-b283-precheck.md`.

This is an **instrument-only** slice. It makes the warning census observe the parameter half
of the B169 class by adding exactly one derived nRF52/ARM gateway-profile cell. It pins the
current warning surface honestly; it does not clean production warnings, alter build flags,
or relax any existing pin. The independently measured 32 warnings in MeshRoute-owned ARM
sources are registered separately as B284.

## Owner ruling

**OWNER RULING 2026-09-03:** widen `tools/warning_census.sh` with exactly one nRF52/ARM
coverage cell, `gateway`, derived as **nRF52 platform AND gateway profile**. Keep the six
OLED-derived cells and their pins unchanged. Derive and pin the new cell's warning, object,
RAM and flash baseline; add a real sabotage control which removes a load-bearing
`[[maybe_unused]]` parameter annotation and must turn the gate RED. This is census coverage,
not a production-cleanup slice, and may run independently in parallel with B278 S5.

## Required result

B283 passes only when:

- the existing `MR_FEAT_OLED=1` derivation remains intact and still yields its same six envs;
- a second, independent config derivation selects exactly `gateway` from
  `platform = nordicnrf52` plus the effective gateway-profile define;
- the normal gate builds all seven cells cleanly in isolated build roots and enforces exact
  warning pins, zero `-Wswitch`, nonzero object counts, and present RAM/flash metrics;
- a third clean `gateway` measurement agrees with the two pre-check measurements before its
  values are made authoritative;
- removing the chosen MR_EMIT-only parameter's `[[maybe_unused]]` in an isolated source copy
  compiles successfully, adds exactly one gateway warning, makes the overall gate fail, and
  leaves all six OLED rows green;
- a vacuous or multi-match mutation, a failed mutant build, a missing row, a missing pin, or
  a selftest that never exercised the gateway cell is itself a failure;
- the real checkout is byte-identical before and after every control; and
- no production, build configuration, board image, corpus, ABI, protocol or baseline file
  changes.

## STOP conditions

STOP and report before widening the slice if:

1. the derived predicate selects zero, more than one, or any nRF52 env other than `gateway`;
2. adding the cell requires typing `gateway` into the selected environment list rather than
   deriving it from effective PlatformIO configuration;
3. the coder's clean-build warning/object/RAM/flash measurement differs from both independent
   pre-check builds, or the two pre-check builds no longer agree on the starting tree;
4. any existing OLED environment, derivation, warning pin, object count, RAM/flash result or
   `-Wswitch` result moves;
5. the parameter sabotage matches other than once, does not compile, fails to add exactly one
   warning in `gateway`, or changes any of the six OLED verdicts;
6. a control edits the live checkout, relies on an incremental warning log, or reports a
   partial/failed build as a measurement;
7. the proposed implementation suppresses, filters, allowlists or fixes a production warning,
   changes `-Wno-unused-parameter`, or edits `platformio.ini`;
8. the gate describes the current 32 own-source warnings as clean or harmless as a class;
9. the tools-only change moves a native, simulator, ABI or board artifact; or
10. any new owner decision is required.

## Fence

Coder-owned files:

- `tools/warning_census.sh`;
- at most one stable supporting runner under `tools/` if separation is necessary;
- an auto-discovered `tools/test_*.py` only if Python logic is added; and
- `docs/superpowers/evidence/2026-09-03-b283.md`.

No file under `lib/`, `src/`, `test/`, `variants/`, `simulation/` or `ios-companion/` may
change. No `platformio.ini`, `simulation/BASELINE.md`, wire/protocol doc, bench script,
register or project-memory edit belongs to the coder. The one historical-pin documentation
addendum described below is drafted in evidence and landed by the Author after QG.

## B283-1 — preserve one derivation and add one derivation

Keep `derive_oled_envs()` byte-for-byte unless a mechanical interface change is unavoidable;
its output remains the six effective `MR_FEAT_OLED=1` environments. Add a separate derivation
over one `pio project config` result which selects the intersection:

```text
effective platform is nordicnrf52
AND effective build flags contain MR_PROFILE_GATEWAY
```

The derived ARM set must contain exactly one member and that member must be `gateway`. The
cardinality and identity are both gate terms: a future config change cannot silently add a
second ARM build or substitute another profile. Do not derive `platform = nordicnrf52` alone;
it selects four current environments. Do not type an ad-hoc environment array.

`--list` must expose enough classification to prove the six OLED cells and the one ARM cell
came from their respective predicates. Change the final verdict from “N OLED env(s)” to an
honest form such as “6 OLED + 1 ARM”, derived from the measured sets rather than a literal
banner. Pin the banner/cardinality so deleting either derivation cannot pass vacuously.

## B283-2 — derive the seventh baseline

Use the census's existing method: fresh isolated `PLATFORMIO_BUILD_DIR`, complete
`pio run -e gateway`, `grep -c 'warning:'`, `grep -c 'Wswitch'`, `*.o` count, and the final
RAM/Flash size lines. Strip ANSI colour only for diagnostic classification; it must not alter
the count authority.

The pre-check hypotheses are **not authority**. The coder derives all of them again:

| gateway field | hypothesis to confirm |
| --- | ---: |
| warning lines | 18631 |
| `-Wunused-parameter` lines | 18196 |
| objects | 283 |
| RAM | 195724 |
| flash | 516668 |
| `-Wswitch` | 0 |
| warnings in MeshRoute-owned paths | 32 |

If the third clean build agrees, pin the exact warning count for `gateway` and record all
fields in evidence. The existing verdict already treats either a higher or lower total as
RED; preserve that zero-drift rule. RAM/flash remain required measurements, not acceptance
bands. Record the own-source warning multiset by class and path so B284 has a reproducible
starting point, but do not create exclusions for it.

Keep the six current pins unchanged and prove the effective output in this order-independent
set:

```text
gateway_heltec=173
gateway_heltec_v4=178
heltec_mobile=177
heltec_v3=177
heltec_v4=182
heltec_v4_mobile=182
```

An explicit `EXPECT_gateway=<N>` override may follow the script's existing reviewed-repin
idiom, but an override cannot alter derivation or let an unpinned derived cell disappear.

## B283-3 — default sabotage controls

The load-bearing negative control is the parameter annotation on
`Node::deleg_ack_release(..., [[maybe_unused]] DelegAckReleaseCause cause)` in
`lib/core/node_hashlocate.cpp`. On a byte-for-byte isolated source copy:

1. prove the exact mutation anchor matches once;
2. remove only that `[[maybe_unused]]`;
3. run the same census authority, never an incremental-log surrogate;
4. prove `gateway` compiled with the same nonzero object count and gained exactly one warning;
5. prove the gate verdict is RED because the pinned count moved; and
6. prove all six OLED cells still compile and remain at their pins, demonstrating the old
   census's blindness rather than merely asserting it.

The default gate must include this control, or its normal PASS banner must state
`PROBE-ONLY — NOT A GATE`. A failed mutation build is not RED evidence. Preserve rejected logs
for diagnosis and name the failed term in the verdict. Hash the controlled source tree before
and after; mismatch is failure.

Also retain one positive control for the already-covered **local-variable** half of B169:
remove the exact `[[maybe_unused]]` from `generic_lifecycle` near the wrapper path in
`node_hashlocate.cpp`, require its anchor to match once, and prove at least one existing OLED
row turns RED for the expected added warning. This prevents the new parameter control from
accidentally replacing the coverage the six-cell census already had.

Add cheap non-build controls for the instrument's own trust boundary: missing ARM cell,
duplicate ARM cell, absent pin, zero objects, missing size, nonzero `-Wswitch`, and a banner
whose claimed 6+1 cardinality disagrees with the actual rows must all fail. If implemented in
Python, the tests must be auto-discovered by
`python3 -m unittest discover -s tools -p "test_*.py"`; otherwise the shell runner must expose
and report its selftest control count.

## B283-4 — proportional gate

Run and record:

1. `tools/warning_census.sh` with its default negative controls: seven clean baseline rows,
   the parameter sabotage, the local-variable positive control, exact control count, and one
   final PASS;
2. `tools/warning_census.sh --list`, proving six OLED plus exactly one ARM/gateway cell;
3. `python3 -m unittest discover -s tools -p "test_*.py"` if any auto-discovered test is
   added, with the prior count reproduced before deriving the delta;
4. `git diff --check` and a complete staged/untracked inventory; and
5. `git diff --stat -- lib src test variants platformio.ini simulation ios-companion`, which
   must be empty for this slice.

Native, corpus, board-pair measurement and ABI probes are not owed: the implementation is a
gate script plus its own isolated builds, and the fence forbids inputs to those artifacts.
Do not claim they ran. B283's `gateway` census build is not a widening of the two-env board
qualification rule; it is the standing warning-census exception.

## B283-5 — evidence and held Author landings

The evidence file must include:

- authoritative starting HEAD and exact diff;
- both derived environment sets and the effective config terms which selected them;
- the third clean gateway baseline with warning/object/RAM/flash values and agreement against
  both pre-check builds;
- all seven normal rows, all pin values and verdicts;
- the own-source warning multiset, including an explicit statement that B283 pins rather than
  approves or fixes it;
- every sabotage/control result, exact anchor match count, build return code, warning delta,
  OLED-row invariance and real-tree before/after hash;
- runtime before/after if the default gate becomes materially slower;
- the tools-test result if applicable, `git diff --check`, and explicit-add inventory;
- a draft B283 register closure; and
- a dated Author addendum for
  `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md` recording that the original six
  OLED pins remain unchanged and the separately derived gateway cell is now part of the
  standing census.

PASS closes B283 as an instrument gap. It does not close B284 or declare the gateway warning
surface clean.
