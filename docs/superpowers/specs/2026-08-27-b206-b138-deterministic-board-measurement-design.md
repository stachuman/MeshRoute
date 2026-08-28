<!-- Author: OpenAI Codex -->
# B206/B138 deterministic board-measurement design

*2026-08-28. Status: S1 PASSED / B138 CLOSED — S2 CORRECTED AND SELF-GATED / B206 AWAITING QG RECHECK. Scope: repair the board RAM/flash comparison
instrument before the internal-DATA/custody wire arc. This changes no protocol, frame,
routing, NV or product behaviour.*

## 0. Decision

Two independently gated slices were approved and executed in order:

1. **S1 — one build-identity authority:** preserve the banner and BLE `version` formats,
   but make one device translation unit own the build timestamp and Git revision strings.
2. **S2 — deterministic comparison runner:** add a same-directory, fixed-identity board
   build runner which proves repeatability before its output may be used as an A/B verdict.

Do not fold B205 or the newly recorded B253 provenance defect into either slice (C1).

## 1. Verified current state

### 1.1 Two timestamp producers are live

The current device image compiles `__DATE__ " " __TIME__` in two translation units:

- `src/fw_main.cpp` — BLE/companion `version` JSON;
- `src/firmware_commands.cpp` — boot and USB `version` banner.

Both also consume `GIT_REV` independently. `print_banner()` is already the one USB/boot
formatter, but build identity itself has no single authority.

### 1.2 B138 reproduced independently on 2026-08-27

Two clean `gateway` builds used identical sources, the same worktree path, toolchain and
`GIT_REV=32f4e6c-dirty`:

| arm | distinct timestamps in ELF | text | data | reported flash |
|---|---:|---:|---:|---:|
| A | 2 (`19:49:50`, `19:49:51`) | 502,236 | 984 | 503,220 |
| B | 1 (`19:54:32`) | 502,220 | 984 | 503,204 |

The only loadable-section movement was `.text +16` in arm A. A **clean build is not
sufficient**: two compilations can still straddle a second and lose literal merging. The
current mitigation “clean-build and verify one stamp” detects the defect after spending
the build; it does not make the measurement deterministic.

The pre-experiment current ELF was a positive control: it also held two timestamps one
second apart.

### 1.3 The reported one-stamp Xtensa residual did not reproduce

Three consecutive clean in-place `heltec_mobile` builds each contained exactly one
timestamp and each measured:

- RAM **218,916 B**;
- flash **1,329,244 B**;
- identical `size` section totals.

Their flashed `.bin` hashes differed, as expected, because the timestamp bytes differed.
The older one-stamp 1,280,684/1,280,700 pair remains an intermittent lead, not an
established mechanism. S2 must detect it if it exists; do not encode “all deltas below
32 B are noise” as permanent truth.

### 1.4 The path-length report is not independently reproduced

The reported +80 B worktree-path effect remains a lead. The current `gateway` loadable
image contains no absolute `/home/staszek/MeshRoute` string, so a direct `__FILE__`
explanation is unsupported. A path may still perturb PlatformIO library IDs, archive
order or linker relaxation.

This design avoids the confound by requiring both arms to use the same source directory
and stable build directory. It does not add a global `-ffile-prefix-map`, alter link order
or claim that the old +80 B mechanism is proved.

### 1.5 Provenance has a separate hole

`tools/git_rev.py` uses `git diff --quiet HEAD`, which does not see untracked files. An
untracked source selected by `build_src_filter` can alter an image whose banner lacks
`-dirty`. That is B253 and is deliberately separate from this correction.

## 2. S1 — one build-identity authority

### 2.1 Required shape

Keep the authority in the already-universal device command module; do not add a new
translation unit or edit every `build_src_filter`.

`src/firmware_commands.cpp` owns exactly one timestamp literal and one Git-revision
literal. `src/firmware_commands.h` exposes read-only accessors, for example:

```cpp
const char* build_stamp();
const char* git_revision();
```

`print_banner()` and `fw_main.cpp`'s BLE `version` arm both consume them. `fw_main.cpp`
no longer defines or directly reads `GIT_REV`, `__DATE__` or `__TIME__`.

An `extern const char[]` authority is also acceptable and avoids a pointer object; choose
the surrounding idiom with the smaller measured image. The invariant is one definition,
not the seam spelling.

### 2.2 Behaviour contract

- Boot/USB banner text is unchanged.
- BLE `version` JSON fields and spelling are unchanged.
- Both transports report byte-identical build and Git strings in one image.
- Normal builds still report their real compile time.
- The `nogit` fallback remains in S1. Making it fail loud belongs with B253.

### 2.3 Structural gate

After S1:

- production `src/` contains exactly one `__DATE__` and one `__TIME__` use;
- production `src/` contains exactly one `GIT_REV` value definition/fallback;
- production `lib/` contains zero `__DATE__`, `__TIME__` or `GIT_REV` uses;
- both output paths call the same authority;
- recompiling only `fw_main.cpp` cannot add a second timestamp string;
- recompiling only `firmware_commands.cpp` still leaves exactly one timestamp string;
- `lib/` contains zero `__DATE__`/`__TIME__`/`GIT_REV` uses (true today, measured — pinned so it stays true).

Measure the last two on `gateway`, the ABI which reproduced the defect.

## 3. S2 — deterministic board comparison runner

### 3.1 Purpose

The runner measures A/B size and RAM deltas. It is not the normal firmware build and its
artifacts must not be flashed as ordinary provenance-bearing images.

### 3.2 Fixed inputs and fail-loud outputs

The runner shall:

1. accept one explicit PlatformIO environment and one artifact/output directory;
2. run from the repository root and hold a lock which excludes every other invocation of
   this measurement runner;
3. use dedicated, gitignored `.pio-measure/` state for every arm, with the PlatformIO
   build directory below `.pio-measure/`; it must not use or clean the developer's `.pio/`,
   `mktemp`, or a worktree-dependent build path;
4. clean only the named environment through PlatformIO before building;
5. set a fixed `SOURCE_DATE_EPOCH` so GCC expands `__DATE__`/`__TIME__` identically;
6. set a fixed Git-revision override through a narrowly validated addition to
   `tools/git_rev.py`;
7. redirect the full build log and print only a bounded summary;
8. fail on build failure, zero objects, missing RAM/flash, missing flashed artifact, or
   anything other than exactly one build-stamp string;
9. record RAM, flash, loadable-section sizes, object count, ELF hash, canonical loadable
   payload hash, environment, package/toolchain identity, independent CC/CXX/LINK
   command/template/wrapper state and fixed build identity in a manifest.

The Git override makes the instrument independent of clean/dirty string length. Empty or
malformed explicit input fails loud; it never falls back silently.

The measurement lock serializes measurements; the dedicated build directory also stops a
board measurement from deleting `.pio/build/native` or another normal build product.
Source-mutating mutation batteries remain operationally exclusive with a measurement in
the same checkout because those existing tools do not share this lock. The runner must
state that constraint in its usage text and manifest. Do not claim that the lock detects
or excludes an independently started battery.

The manifest records the resolved CC, CXX and LINK commands and whether a wrapper is
active on each. LINK is recorded even when it aliases a compiler; the manifest separately
proves that alias. Today the board tools are unwrapped while host-native builds may use
the repository's ccache masquerade; that fact must be explicit, and a wrapper-state
difference on any role between A/B arms is a qualification failure rather than an
attributed firmware delta.

The implementation is explicitly Linux/POSIX-only because its non-blocking lock and
continuously drained build PTY use `fcntl` and `pty`. Windows portability is not part of
this slice.

`SOURCE_DATE_EPOCH` is not an assumed cross-toolchain feature here: the repository's
actual ARM GCC 12.3.1 and Xtensa ESP32-S3 GCC 13.2.0 both expand epoch `946684800` to
`Jan  1 2000 00:00:00` in a direct preprocessing control run.

### 3.3 Repeatability qualification

Run the finished runner twice without source changes for the two owner-approved essential
ABIs:

- `gateway` (ARM/nRF52);
- `heltec_mobile` (Xtensa/ESP32-S3).

For each environment, both runs must have identical RAM, flash, loadable-section sizes,
canonical loadable-payload hash, object count and package/toolchain identity, with exactly
one stamp. The payload is `firmware.hex` for `gateway` and `firmware.bin` for
`heltec_mobile`; do not gate on a ZIP/UF2 container whose packaging metadata may carry its
own timestamp. The ELF hash is recorded diagnostically, not made a loadable-firmware
verdict. Any loadable mismatch is a gate failure, not a tolerated ±16/±32 band.

### 3.4 A/B use protocol

Both arms build in the same checkout and stable build directory. The runner does not
mutate source or switch Git state. A caller may apply/revert a reviewed patch between
arms, but records pre/post source hashes and proves restoration. A `/tmp` worktree result
is not comparable unless a later separate slice proves path independence.

Only deltas between two qualified manifests are actionable. A normal `pio run` size line
may remain informational, but cannot establish a 16-byte regression.

## 4. Slices and gates

### S0 — evidence only (complete)

Record §1 without moving a baseline or pin.

### S1 — centralize identity

Files: `src/firmware_commands.{h,cpp}`, `src/fw_main.cpp`, focused source/probe checks and
docs. No `tools/git_rev.py` change.

Gate: native green; simulator rebuild with no relevant action and exact s18; `gateway`
incremental controls from §2.3; `heltec_mobile` build; zero RAM expected on both boards.
Flash movement is attributed to one fewer literal plus seam code, not assumed zero.

**Result, 2026-08-27:** complete. `firmware_commands.cpp` now owns one read-only build
stamp and Git-revision authority, consumed unchanged by the banner and BLE JSON paths.
The focused structural probe passes 13 source checks and 14 checks with either board ELF.
On `gateway`, rebuilding only `fw_main.cpp` retained the sole linked stamp; rebuilding
only `firmware_commands.cpp` replaced it and still left exactly one. Native passed at
2240/96469/0, s18 remained exact at `9868cad3`/269905/0, and both board ABIs retained
byte-identical RAM and flash (`gateway` 196196/503204; `heltec_mobile`
218916/1329244). B138 is closed independently of S2.

### S2 — runner and override

Files: `tools/git_rev.py`, one new `tools/` runner/test, `.gitignore` (add
`.pio-measure/`) and docs. No production C++.

Gate: negative controls for missing artifact, build failure, zero/multiple stamps,
malformed override, missing size result and concurrent use; separate CC/CXX/LINK wrapper
mismatches; and an independently literal qualification-field inventory whose every field
is mutated one at a time. Then run §3.3's two-build × two-ABI qualification. A normal build
without overrides must still stamp the real Git revision.

Under the owner's 2026-08-18 scope ruling, use only `gateway` and `heltec_mobile`; do not
run an all-environment census.

**Result, 2026-08-27:** complete. Fourteen negative/control tests pass, covering build
failure, missing artifacts and size output, zero objects, zero/multiple/wrong stamps,
empty or malformed overrides, normal-Git selection, compiler/wrapper recording and
mismatch, isolated state, the source identity invariant, and a real concurrent-runner
refusal. All four clean arms used one source snapshot
(`81862ba013fc523525f8381f28e09a67439db6a6937571fe4c5b985786699d28`, 711 files), one
fixed stamp (`Jan  1 2000 00:00:00`) and one fixed revision (`b206b206b206`).

The two `gateway` arms matched exactly at **196196 RAM / 503204 flash / 283 objects**,
loadable sections `.ARM.exidx=8`, `.data=984`, `.text=502212`, and `firmware.hex` SHA-256
`8f7c9373afbe98ff46eadac477be6c63c16dce8bc71e4d48a4f548231ca51f19`. The two
`heltec_mobile` arms matched exactly at **218916 RAM / 1329244 flash / 326 objects**;
all eight loadable-section sizes matched, and `firmware.bin` SHA-256 was
`2779186623d87eaf13f1ce27a906f7cd7bd687c171d1fd0cb66d4fa681fef487`. The recorded
ARM GCC 12.3.1 and Xtensa GCC 13.2.0 compiler paths, command templates and package sets
matched within their pairs; both were unwrapped. The normal `.pio/` metadata remained
exact across all four runs (`7738` entries, SHA-256
`d11904419de84182eb71541d3e7eee77a02249c4d0e131af495b9bba02f49815`). ELF hashes also
matched within each pair, although they remain diagnostic only.

A subsequent ordinary `pio run -e gateway`, with no measurement override, selected real
revision `32f4e6c-dirty` exactly once, contained no fixed revision or epoch stamp, and
retained its real `Aug 27 2026 20:39:54` build stamp. QG accepted this measurement as
genuine but held S2 because schema 1 recorded only CXX wrapper evidence and its comparator
inventory test was not independent. B206 therefore remains open pending schema 2 and a
repeated four-arm qualification. No all-environment census was run; B246 and B253 remain
separate work exactly as specified.

**Corrected schema-2 result, 2026-08-28 — self-gate passed; ✅ QG-PASSED 2026-08-28 (owner ruling: 18/18 controls, exact repeated builds on both approved ABIs — B206 CLOSED with S1's own PASS the same day: 9/9 controls RED, the measured extern-const-char[] seam accepted on its 16-byte gateway advantage, B138's two-TU mechanism structurally prevented and mutation-controlled).** All 18
named tests pass. They preserve the original 14 control obligations and add three
role-specific wrapper mismatches plus a literal 61-leaf inventory equality check; a
separate loop mutates each of those 61 fields and proves every mismatch is rejected. The
compiler-state writer and reader require exactly CC, CXX and LINK, their command templates,
resolved and real paths, versions and wrapper state. Both real ABIs record distinct CC and
CXX tools, LINK as an explicit CXX alias, and no wrapper on any of the three roles. The
usage text and manifest state the Linux/POSIX-only limitation.

All four schema-2 arms used source SHA-256
`5a05dbc9944d7779927253bbe9f15829a7a5ae44ad6e4a1936c68003c176c9ed`
over 711 files. `gateway` again matched exactly at **196196 RAM / 503204 flash / 283
objects**, sections `.ARM.exidx=8`, `.data=984`, `.text=502212`, and `firmware.hex`
SHA-256 `8f7c9373afbe98ff46eadac477be6c63c16dce8bc71e4d48a4f548231ca51f19`.
`heltec_mobile` again matched exactly at **218916 RAM / 1329244 flash / 326 objects**,
all eight section sizes, and `firmware.bin` SHA-256
`2779186623d87eaf13f1ce27a906f7cd7bd687c171d1fd0cb66d4fa681fef487`.
Normal `.pio/` remained exact across all four corrected arms: 7738 entries and SHA-256
`b91f5dc77218cd149bcf16d85971961890f729abbe4b78907a6a1be071d84c70`.
B206 remains open until independent QG accepts this corrected result.

## 5. Closure and follow-on

- **B138 closes with S1** as the two-TU timestamp/literal-merging defect.
- **B206 closes only after corrected S2** passes exact repeatability on both ABIs and QG
  accepts the result. The schema-1 measurements are genuine but insufficient to close the
  row. A future controlled mismatch opens a newly isolated mechanism rather than reviving
  a generic noise allowance.
- Retire the blanket “under ~32 B is noise” workaround on closure. Historical notes stay
  historical and are not rewritten.
- Fix **B253** normal-build provenance separately, then **B205**.
- **B246 is complementary, not superseded:** B206/B138 qualify deterministic image-level
  RAM/flash comparison, while B246 concerns board-ABI visibility of struct sizes. If its
  proposed compile-only Xtensa size probe is approved, its output is a natural additional
  manifest field, but that remains B246's separate owner ruling and slice.
- Only then should B20/B21 and the internal-DATA/custody wire arc use board-size deltas as
  acceptance evidence.
- ⓘ **B246 is complementary, not overlapping** (review note 2026-08-28): B206/B138 fix IMAGE-level
  determinism; B246 (open, owner ruling pending) fixes STRUCT-level board-ABI visibility. Neither
  supersedes the other; if B246's compile-only xtensa `sizeof` probe is adopted, its output is a
  natural extra manifest field — that is B246's ruling to make.
