# Standalone mobile/Home — W1 (B241) QA pre-check

Date: 2026-09-24. **PRE-CHECK COMPLETE — ready for Claude to author the W1 brief.**
This is a source-fact ledger and independently executed baseline, not a brief approval or implementation gate.
B241 remains OPEN; no production, test or tool source was edited.

The owner's current role assignment applies: Codex QA performs pre-checks, reviews briefs and gates frozen
implementations; Claude authors; a separate coder implements. Commits are not prerequisites for the next step.

## 1. Measured inputs and preservation

- MeshRoute: `4a230f4501c6a19d71b9234968bf1389484d8295` (`Pre-UI rework - design`).
- Simulator: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean throughout.
- Design: revision 2.17, REVIEWED; SHA-256
  `bc645d9242cf1e17330df942296ef20fc7bef54ff4d23f454badc78d1e0190d8`.
- Permitted preparation set at entry: the two existing uncommitted documentation edits below. Both were preserved.

| Path | SHA-256 of input |
| --- | --- |
| `docs/2026-07-30-open-bug-register.md` | `febd4d9dea84e0c4094a05494504703877069fae12928ad4e07fff0d2fae1abe` |
| `tracker.md` | `d45f067cf4ac94be7804dffba86d28c2dc1391f56ed4521e5a89119fef31b8fd` |

The [MeshRoute inventory](2026-09-24-standalone-mobile-home-w1-precheck/inputs.json) records all 1,146
tracked/nonignored input paths, HEAD and initial status; its SHA-256 is
`c1f264817b7c757e18f49cac8fb4b8705b73e7bc7da397b9ed0cdd36e62e82e7`.
The [simulator inventory](2026-09-24-standalone-mobile-home-w1-precheck/sim-inputs.json) records 285 paths;
its SHA-256 is `04fc868c00d2a76732299309076a9ea755615dedb3ff4069f78d41da529a3561`.
All inventoried bytes and both HEADs were checked again at completion
([preservation record](2026-09-24-standalone-mobile-home-w1-precheck/preservation.json)). Only this report and
its evidence directory were added. No staging, commits, resets, cleanup or simulator edits were performed.

## 2. Source facts: preserve the raw API, repair the string adapter

Symbols are the pins (P4); these line numbers are relocation hints at the measured base.

`Node::peer_name_find`, `lib/core/node_hashlocate.cpp:450–458`, copies
`min(cached_name_len, cap)` bytes and returns that count. It does not append a NUL. A miss or nameless row
returns zero without clearing the destination. A zero capacity copies nothing. A named row with a nonzero
capacity requires a valid destination; this audit does not infer a null-pointer guarantee.
`protocol::peer_name_max` is **32** (`lib/core/protocol_constants.h:106`).

There are exactly **four production call sites**, excluding the declaration and definition:

| Caller | Current use | W1 implication |
| --- | --- | --- |
| `Node::push_peer_key_cached`, `lib/core/node_hashlocate.cpp:721–726` | Copies into `Push::body` with capacity 32 and records the returned `body_len` | All 32 bytes are payload. Reserving a NUL inside the raw API would discard byte 32. |
| `mrfw::peer_store_sync`, `src/firmware_commands.cpp:103–120` | Copies into `char nm[32]`, then passes `nm` and returned `nl` to `mrnv::peer_rec_put` | RAM-to-`/mrpeers` persistence also needs all 32 bytes. This is the caller previously described as a console lookup. |
| `label_from_hash`, `src/firmware_ui.cpp:440–442` | Passes the whole destination capacity; only writes a formatted fallback when the returned count is zero | This is the defective adaptation to a C string. Fix termination centrally here. |
| Invite member projection in `build_snapshot`, `src/firmware_ui.cpp:792–795` | Passes `sizeof mem.name - 1`, then writes `mem.name[nn] = '\0'` | Already reserves and writes the terminator. Preserve this separate raw-name projection and its no-fallback rule. |

The actual console `handle_nameof` reads `Node::peer_book_by_hash` and its `PeerBookRow`, then calls
`write_peer_name` with a length (`src/firmware_commands.cpp:1189–1197`); it is not a fifth caller.

`mrui::kLabelCap` is **14** (`src/firmware_ui_model.h:224`); the UI destinations have **15 bytes**.
`label_for_team_id` and `label_for_origin` delegate to `label_from_hash` (`firmware_ui.cpp:448–460`).
Their consumers are the TEAM snapshot (`:766`), compose result (`:2184`), compose header (`:2264–2265`),
and receive-push routing (`:2728–2730`). The latter three use uninitialised local label buffers.
A successful copy of `H1` or `H2` leaves byte 2 untouched; a name of at least 15 bytes fills the buffer
without any terminator. The compose header subsequently uses `%s`. Zeroed fixtures can mask the short-name
defect; this pre-check confirms the defect from source, not a newly executed hardware or poisoned-buffer reproduction.

**Recommendation for the brief:** use B241's already-listed alternative, termination in `label_from_hash`.
Reserve one byte there, terminate at the returned length, preserve the current `0x%08lx` fallback, and handle
zero capacity before subtracting one. Keep the existing raw core API and the two 32-byte consumers unchanged.
No renderer-specific clearing, new state, name sanitisation, new truncation marker, name-precedence change,
default-name change or production helper extraction belongs in W1 (C1/U1). W4a and W1c retain their own scope.

This is a **`src`-only fix recommendation**, not a core contract change. Claude must align the design's
§1.2 package-summary sentence, §4.1 dependency, §11.2 shared-build statement, §11.3 core-package list and
§13 W1 row when authoring; those currently assume termination inside `peer_name_find`. The B241 addendum's
“console lookup” wording should become “peer-store persistence.” The register already permits the central
UI alternative, so this recommendation does not require reopening the owner's name/UI rulings.
The reviewed design and existing author edits were left untouched in this pre-check.

## 3. Coverage and obstacles for the author

The caller search included production, native tests and tools (`rg 'peer_name_find\s*\(' lib src test tools`).
There are **23 executable native call sites**: 17 in `test_node_hashlocate.cpp`, three in `test_node_r3.cpp`,
three in `test_dual_layer.cpp`; the firmware-UI probe has two more (`probe_main.cpp:4790,5336`).
Native covers rename, count-based reads, overlength clamp, empty-name retention, aged-but-present rows and
misses. Existing full-32 checks in `test_node_hashlocate.cpp` use a **64-byte** destination: they do not prove
that a 32-byte destination retains 32 payload bytes if the API is changed to reserve a terminator.

`platformio.ini:78` sets `test_build_src = no`; native does not compile `src/firmware_ui.cpp`.
The simulator compiles core, not this UI TU. The pure `test_firmware_ui_team.cpp` label fixtures do not exercise
the defective producer. Therefore native and corpus green cannot establish B241 closure.

Use the existing **`tools/probe_firmware_ui/` real-TU probe** for the regression, with its normal layered,
v3 and BLE-row builds and default negative controls. Author the narrow test/instrument fence around that seam;
do not replace it with a copied implementation or only a source-text check. Required acceptance coverage:

1. Drive the real label producer with a poisoned 15-byte destination and boundary canaries: `H1`, `H2`,
   long-name-to-short-name rename, and 14-, 15- and 32-byte names. Check NUL at the copied length and no write
   outside the destination. If testing capacity 0/1, label those helper-boundary cases as synthetic; production
   label callers here use 15 bytes.
2. Preserve unknown/nameless hash fallback and bare-ID paths. Reach the actual compose/result and receive
   consumers as well as TEAM projection; a zero-initialised snapshot alone is insufficient.
3. Keep the raw API's full 32-byte output/count and the push/persistence consumers unchanged; include an
   exact-capacity regression so a global `cap - 1` change cannot silently pass the current 64-byte tests.
4. Demonstrate a compiled, assertion-failing control for the original missing terminator and for the tempting
   wrong repair that only writes `out[cap - 1] = '\0'` (which leaves the short-name garbage). Crashes or failed
   compiles are not usable RED controls.

The probe runner has existing source consumers around these symbols, including O8 (`run.sh:1446–1447`),
which replaces invite projection with `label_from_hash`. Preserve that control's meaning and the invite
full-hash/no-fallback controls; audit exact-source readers before editing (D6/P7). Keep numeric gate pins bare
and run tools discovery if runners change (D5). The separate `probe_board_ui` drift recorded as **B418** remains
W2's scope; this pre-check did not rerun that probe or inherit its historical counts as current results.

**Corpus prediction:** a fix confined to the UI adapter is inert by construction; all 36 streams should match
the full hashes captured below. No wire, NV format or Node layout change is needed; no resident-state growth
is proposed. ABI and linked RAM/flash were not measured in this pre-check.
The brief still names the coder's full gate and the independent `src`-only gate per P6: native, corpus, boards,
touched batteries and affected probes. A widened core scope requires a fresh scope decision and the full gate
on both sides, not a silent API change. These are brief obligations, not claims that those gates ran here.

## 4. Independently executed baselines

| Instrument | Result |
| --- | --- |
| `pio test -e native` | Exit 0; ordinary incremental invocation, not a clean build. Wrapper printed its misleading “0 test cases.” |
| `./.pio/build/native/program` | **2,950 cases / 195,770 assertions / 0 failed / 0 skipped**, exit 0. |
| Fresh stock simulator CMake configure and `lus` build in `/tmp` | Both exit 0; rebuilt normal and gateway core variants against the measured MeshRoute tree. |
| `tools/run_corpus.py --require-anchors` | **36/36 anchor matches**, every run exit 0 and zero assertion failures; runner reports `inputs_stable: true`. |
| s18 | **269,517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**; current canonical prefix `32afbf11` reproduced. |

Anchors were read from the current `### 36/36 corpus` table through the stock runner, not historical prose.
`simulation/BASELINE.md` SHA-256: `71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`.
The table supplies MD5 prefixes and event/failure counts; this is **fresh anchor reproduction**, not a claim of
byte-for-byte comparison against an earlier archived corpus. The new manifest stores full MD5 and SHA-256
for every stream, suitable as W1's before/after baseline.

Exact commands, exits, timings and binary hashes are in [runs.json](2026-09-24-standalone-mobile-home-w1-precheck/runs.json).
The [corpus manifest](2026-09-24-standalone-mobile-home-w1-precheck/corpus-manifest.json) is copied verbatim
(SHA-256 `ab513423ff9b44481ffbb0e89c773515e160c7ceaaae7e937a7cbbba6a067431`).
Raw native, configure/build and corpus logs are beside it. Large binaries/build directories and NDJSON streams
remain outside the repository at `/tmp/meshroute-w1-precheck-pv8ozdmu`; that path is temporary, not a durable
archive. Recreate streams if needed; do not assume `/tmp` survives until implementation.

**Not run:** new focused B241 regression, firmware-UI/other probes, boards, ABI/RAM/flash measurements,
warning census, mutation batteries, tools discovery or metal checks. This pre-check supplies the source facts
and fresh native/corpus baselines requested before authoring. B241 stays open; the brief review and the
implementation's independent gate are still ahead. No new register ID was allocated.
