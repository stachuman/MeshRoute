# Codebase cleanup assessment

**Date:** 2026-07-15  
**Assessment range:** `546bb46..f09e950`  
**Purpose:** Review the result of the structural cleanup performed during 2026-07-14 and 2026-07-15, and retain the remaining work as an actionable follow-up list.

## Executive summary

The cleanup was substantially successful, but it is not complete. The firmware is materially easier to navigate and safer to extend. The main improvements are the decomposition of `fw_main.cpp`, the reduction and regrouping of `Node` declarations, centralization of dangerous carrier conversions, broader CI firmware coverage, and removal of tracked macOS metadata.

The highest-priority regression is documentation integrity: moving specifications into archive directories left many references pointing at their former paths. The main architectural limitation is that the extracted firmware modules still share most state through `fw_context.h`; this is a useful transitional seam, but not yet strong module isolation. Test and CI boundaries also need another pass.

No functional regression was found by the native test suite.

## Scope and observed change

The assessed range contains 250 changed paths and approximately 3,885 insertions and 2,133 deletions. Much of that path count comes from documentation moves and removal of AppleDouble metadata.

Notable source-size changes:

- `src/fw_main.cpp`: approximately 2,755 lines before cleanup, 1,193 afterward.
- `lib/core/node.h`: approximately 1,614 lines before cleanup, 1,314 afterward.
- New responsibility-oriented files:
  - `src/firmware_config.cpp`
  - `src/firmware_commands.cpp`
  - `src/firmware_inbox.cpp`
  - `src/firmware_remote.cpp`
  - `src/fw_context.h`
  - `lib/core/node_carriers.h`

## Improvements delivered

### 1. Firmware responsibilities are easier to locate

Configuration, command dispatch, inbox handling, and remote management were extracted from the former `fw_main.cpp` monolith. `fw_main.cpp` is now closer to board/runtime orchestration.

The result is a significant improvement in navigation and reviewability, even though the extracted modules still share global state.

### 2. Carrier conversion risk is centralized

`PendingTx` and related carrier declarations were moved from `node.h` to `node_carriers.h`.

More importantly, reconstruction of a `TxItem` from a `PendingTx` is centralized in `txitem_from_pending()`. Requeue sites use this helper, reducing the risk that fields such as `type`, `nonce_seed`, forwarding metadata, or mobile addressing marks are copied at one site but forgotten at another.

This directly addresses a historically high-risk field-drop class.

### 3. CI firmware profile coverage increased

The firmware workflow now contains these ten board profiles:

- `xiao_sx1262`
- `heltec_v3`
- `xiao_esp32s3`
- `gateway`
- `gateway_heltec`
- `gateway_esp32s3`
- `production`
- `xiao_mobile`
- `heltec_mobile`
- `xiao_esp32s3_mobile`

The workflow also rejects newly tracked `._*` and `.DS_Store` metadata.

### 4. Repository hygiene and documentation organization improved

Tracked AppleDouble and `.DS_Store` files were removed. Architecture and coding guidance were added in:

- `ARCHITECTURE.md`
- `docs/CODE_GUIDELINES.md`
- `docs/specs/README.md`
- `docs/superpowers/specs/README.md`

The archive directories make active specifications easier to distinguish from historical plans, subject to the broken-link finding below.

### 5. A native-test seam was introduced for pure configuration logic

`src/firmware_config_parse.h` exposes pure parsing helpers without pulling the device runtime into native tests. `test/test_firmware_config_parse.cpp` covers:

- `parse_sf_list`
- `kv_next`
- `team_fnv1a32`

This is a good pattern to continue for command parsing, configuration mutation, and response encoding.

## Remaining findings

### P0 — Documentation archive moves left broken references

A repository-wide scan of dated specification references found 34 missing target paths among 38 unique referenced paths. Some references occur in archived material, but active documentation also contains broken paths, including:

- `docs/PORT_PLAN.md`
- `docs/configuring-a-gateway.md`
- `docs/protocol.md`
- `docs/frames.md`
- iOS companion documentation

Most targets now exist below `docs/specs/archive/` or `docs/superpowers/specs/archive/`, while references still use the original path.

**Recommended action:** update references to the archived locations and add a Markdown link checker to CI.

### P0 — CI does not compile the native simulator

CI runs:

```text
pio test -e native
```

but does not run:

```text
pio run -e native
```

The simulator entry point can therefore stop compiling while the native tests remain green.

**Recommended action:** add a separate native simulator build step or include `native` in a suitable build job.

### P1 — `fw_context.h` is a transitional global-state seam

Every extracted firmware implementation includes `fw_context.h`, which exports the device stack and much of the mutable runtime, configuration, diagnostic, and administrative state.

This was a reasonable low-risk foundation for moving code without changing behavior. It should not become the final module interface: unrestricted `extern g_*` access keeps ownership unclear and allows coupling to grow again.

**Recommended action:** incrementally introduce narrow context/service structures and move state ownership into the modules that manage it. Do this one responsibility at a time rather than through another broad rewrite.

### P1 — Extracted firmware implementations have limited native coverage

The native environment sets `test_build_src = no`. This avoids a `main()` collision with `sim_main.cpp`, but it also means native tests do not compile or directly exercise:

- `firmware_config.cpp`
- `firmware_commands.cpp`
- `firmware_inbox.cpp`
- `firmware_remote.cpp`

Board builds validate compilation, while only the extracted pure parser header currently receives direct native coverage.

**Recommended action:** continue extracting pure logic behind device-independent headers or small libraries, then add tests for command parsing, configuration mutations, authorization decisions, and binary/JSON response encoding.

### P1 — Warning, formatting, and static-analysis enforcement are absent

No repository `.clang-format`, `.editorconfig`, `.clang-tidy`, cppcheck configuration, or equivalent CI enforcement was found.

At least one first-party compiler warning remains in `lib/core/node_hashlocate.cpp` around line 759: misleading indentation combines the registry match, copy loop, and `break` in a compact block.

The nRF framework itself emits an exceptional volume of unused-parameter warnings. Applying global `-Werror` would therefore be impractical and would hide the distinction between project and dependency quality.

**Recommended action:**

1. Reformat the first-party warning site for explicit control flow.
2. Add a formatter configuration.
3. Add formatting/static-analysis checks scoped to first-party paths.
4. Avoid treating third-party framework warnings as project failures.

### P1 — Behavioral protocol work was mixed into the cleanup batch

Commit `f09e950` changes the mobile OFFER wire representation from 9 to 13 bytes by adding `target_key_hash32`. It also replaces broadcast re-OFFER collision recovery with a targeted DENY flow.

The implementation has relevant native tests and `docs/frames.md` describes the 13-byte form. However:

- this is a functional and wire-format change, not behavior-preserving cleanup;
- the active `docs/superpowers/specs/2026-07-07-mobile-node-handling-assumptions.md` still describes a 9-byte mobile OFFER;
- old and new mobile firmware will reject each other's OFFER because parsing requires the exact frame length.

**Recommended action:** update the active mobile documentation, record the mixed-version compatibility constraint, and keep future protocol changes separate from structural-cleanup commits.

### P2 — Extracted files are still comparatively large

`firmware_commands.cpp` and `firmware_config.cpp` are each several hundred lines. This is still a major improvement over the original monolith, but their internal responsibilities may eventually warrant smaller pure components.

Further splitting should follow demonstrated ownership or testing boundaries rather than target line counts.

### P2 — Minor whitespace cleanup remains

`git diff --check 546bb46..HEAD` reports only an extra blank line at EOF in `docs/2026-07-14-t1000e-feasibility.md`. This is harmless and can be corrected with the next edit to that document.

## Verification performed

### Passed

- `pio test -e native -v`
  - 737 test cases passed
  - 25,368 assertions passed
- `pio run -e native`
- Tracked macOS metadata count: zero
- Worktree inspection showed no review-created modifications

### Board-build qualification

A combined local board-matrix build was started. It produced an extremely large volume of third-party nRF framework warnings and was manually stopped while compiling the mobile profiles, so it is not recorded here as a clean full-matrix result. The CI workflow defines all ten required board environments; their actual CI result should be used as the authoritative full-matrix gate.

### Not re-verified in this assessment

- Metal/bench behavior for the existing radio and OTA findings in the earlier triage document
- Simulator scenario byte-identity claims
- Mixed-version interoperability for the new 13-byte mobile OFFER

## Recommended next-step checklist

### Immediate

- [ ] Repair references affected by `specs/archive/` moves.
- [ ] Add a Markdown link checker to CI.
- [ ] Add `pio run -e native` to CI.
- [ ] Update the active mobile assumptions document to the 13-byte targeted OFFER.
- [ ] Document the mobile firmware compatibility boundary.

### Next structural increment

- [ ] Fix the first-party misleading-indentation warning.
- [ ] Add formatting and first-party static-analysis enforcement.
- [ ] Extract and test pure command parsing/dispatch selection.
- [ ] Extract and test configuration mutation and validation.
- [ ] Extract and test remote response encoders and authorization decisions.
- [ ] Begin replacing broad `fw_context.h` access with narrow module contexts.

### Process guardrails

- [ ] Keep protocol/wire changes separate from structural cleanup commits.
- [ ] Require native tests, native simulator build, and all board builds before accepting further broad moves.
- [ ] Prefer small ownership-driven extractions over line-count-driven splitting.

## Final assessment

The two-day cleanup delivered real architectural value rather than cosmetic rearrangement. The primary source monolith is smaller, responsibility boundaries are visible, a dangerous carrier-copy pattern is centralized, CI covers more firmware profiles, and the native suite remains green.

The work should be considered a successful first phase. Completion requires repairing documentation integrity, strengthening CI, expanding pure native-test boundaries, and gradually replacing the shared-global seam with narrower interfaces.
