<!-- Author: OpenAI Codex -->
# B253 untracked-build provenance design

*2026-08-28. Status: ✅ IMPLEMENTED AND QG-PASSED 2026-08-28 (37/37 tests; probe 27 checks / 12 controls RED; both deterministic ABI pairs exact; the real GIT_DIR abort proven in a board build; historical payload/measurement fields exact). The four §0 rulings were owner-confirmed before dispatch. B206 S1/S2 has passed independent QG and
is closed, so its one override seam and deterministic runner are now prerequisites,
not pending work. Scope: make ordinary board-build provenance fail closed and
include non-ignored untracked files. No protocol, frame, routing, NV or product
behaviour changes.*

## 0. Recommended owner rulings

Approve these rules:

1. **`-dirty` describes the whole non-ignored Git worktree**, not an attempted
   reconstruction of PlatformIO's per-environment source graph. Any staged,
   unstaged or non-ignored untracked path makes the revision dirty.
2. **A normal board build requires usable Git provenance.** Missing Git, no
   repository, no `HEAD`, or a failed status query aborts the build; it must not
   silently produce `nogit`.
3. **Reuse B206 S2's one explicit override.** A valid
   `MESHROUTE_GIT_REV_OVERRIDE` bypasses Git for the deterministic measurement
   runner. Do not broaden it into a general release/archive interface, add another
   override, or silently fall back to it.
4. Preserve the existing decision to ignore submodule dirtiness. Ignored files
   remain outside this revision marker; a future build must not consume an
   ignored generated source without separately defining its provenance.

ⓘ **Expected consequence in THIS repository's workflow (owner-confirmed 2026-08-28):** uncommitted
plan/spec/brief files are the normal between-commit state of this tree, so under rule 1 virtually every
mid-arc build will carry `-dirty`. That is correct provenance, ⛔ not a regression to file; committing before
flashing is what yields a clean banner. The owner also confirmed rule 3's tightened charter deliberately:
a git-less source-archive build has NO path and fails — a future tarball/release build needs its own ruling,
never a quiet override reuse.

The full-worktree rule is intentionally conservative. The current implementation
already marks a build dirty for an edited tracked document, even if that document
cannot enter the image. Applying the same rule to untracked files is consistent,
simple and fail-safe. Parsing `build_src_filter`, library dependency discovery and
conditional source selection would create a second, incomplete build-graph
authority and could still produce a false-clean banner.

## 1. Verified defect and boundaries

### 1.1 B253 reproduces without a build

`tools/git_rev.py` currently decides dirtiness with:

```text
git diff --quiet HEAD --ignore-submodules
```

That command considers staged and unstaged changes to tracked paths, but not
untracked paths. On 2026-08-27, against a real non-ignored untracked file in this
checkout, the exact discriminator returned clean while:

```text
git status --porcelain=v1 --untracked-files=normal --ignore-submodules=all -- <path>
```

returned `?? <path>`. `git check-ignore` confirmed the path was not ignored.

Therefore an untracked `.cpp`, header, variant or generated include selected by a
board build can change the image while the banner reports only the clean short
commit id.

### 1.2 Hook coverage is currently complete

The effective `pio project config --json-output` configuration on 2026-08-28 has
14 `env:*` sections: all 13 non-native board environments consume
`pre:tools/git_rev.py` exactly once; `native` consumes it zero times and uses only
`pre:tools/ccache_native.py`. Native relies on the C++ fallback because it is not a
provenance-bearing board image.

This is current evidence, not a permanent assumption. The structural probe must
derive the environment list from effective PlatformIO configuration and keep this
property pinned when environments are added or inheritance changes.

### 1.3 B206 S2 established the override seam

The closed B206 S2 slice added the validated environment variable
`MESHROUTE_GIT_REV_OVERRIDE`, accepting 7–40 lowercase hexadecimal digits with an
optional `-dirty`. B253 consumes that exact seam. It must not edit the validation
language opportunistically or weaken the deterministic measurement manifests.

B206's corrected schema-2 qualification passed independent QG with 18/18 controls
and exact repeated builds on `gateway` and `heltec_mobile`. B253 must keep those
controls green. It cannot, however, reproduce the old manifest files byte-for-byte:
the manifests record a source-tree hash, and this slice deliberately changes
`tools/git_rev.py`. The correct invariant is specified in §4.

### 1.4 Explicit non-goals

B253 does not:

- hash the source tree or make `GIT_REV` identify the contents of a dirty build;
- enumerate only files which happen to enter one board environment;
- report ignored files, framework packages or compiler packages through `-dirty`;
- make a build atomic against a source edit which begins after the pre-build hook;
- fix B205, B206, B246 or normal artifact archival;
- remove the C++ fallback needed by non-board/native compilation.

A `-dirty` image still requires the diff/untracked inputs to reproduce it. B253
only prevents one known false-clean class.

## 2. Required implementation

### 2.1 One Git query authority

Keep `tools/git_rev.py` as the single PlatformIO adapter. Its ordinary path shall:

1. start from PlatformIO's explicit `$PROJECT_DIR`, not the process's incidental
   current working directory, and resolve the containing Git worktree root;
2. obtain and validate the short `HEAD` id;
3. run one fail-loud porcelain-status query equivalent to:

   ```text
   git status --porcelain=v1 --untracked-files=normal --ignore-submodules=all
   ```

4. append `-dirty` iff that output is non-empty; and
5. inject and print the resulting revision.

Use argument-vector subprocess calls with an explicit working directory. Do not
invoke a shell, parse human-readable `git status`, or list untracked paths in the
normal build log. The hook needs only the empty/non-empty result; filenames may be
sensitive and an unbounded list would make build output noisy.

`--untracked-files=normal` is sufficient: an untracked directory makes the tree
dirty without paying to enumerate every child. Git-standard ignores remain in
force.

Treat porcelain stdout as **bytes** and test only empty versus non-empty. Do not
decode filenames: Git permits path bytes which are not valid UTF-8, and such a path
must make the image dirty rather than make an otherwise valid build fail. Only the
short revision is decoded and validated as ASCII hexadecimal. The two subprocess
calls are one authority even though Git exposes revision and status separately; do
not add a second dirty predicate elsewhere.

### 2.2 Fail-loud normal builds

On the ordinary Git-derived path, all of these abort the PlatformIO build with one
bounded diagnostic:

- Git executable unavailable;
- project directory is not inside a Git worktree;
- `HEAD` cannot be resolved (including an empty repository);
- revision is empty or malformed;
- porcelain status exits non-zero.

Do not return `nogit` from any of these arms. Detached `HEAD` is valid and must
work. Do not print a Python traceback as the primary diagnosis if the SCons hook
can provide one concise error first.

The C++ `#ifndef GIT_REV` fallback remains for native/custom compilation, but its
comment must no longer imply that a repository board environment may silently use
it. The effective-config structural gate is what prevents a new board environment
from omitting the hook.

### 2.3 Explicit override path

When `MESHROUTE_GIT_REV_OVERRIDE` is present:

- validate it using the one S2-established validator;
- do not invoke Git;
- print that the revision source is `override`, not `git`;
- inject the exact validated value; and
- preserve S2's fail-loud behavior for empty or malformed values.

When it is absent, print `source=git`. Keep the line bounded and preserve the
revision value already printed by the hook. This makes an accidentally inherited
override visible in every board log. B253 does not authorize a weaker or broader
override grammar.

### 2.4 Testable seam

The Git resolution logic must be testable without compiling a board. Keep one
function in the real hook which takes the explicit project directory and an
injectable subprocess runner (or an equivalently narrow command seam). The existing
`runpy` + fake-PlatformIO harness may load that real function under a valid override
and call it against temporary repositories. Do not copy its Git rules into a
test-only implementation or introduce a second helper module unless review proves
the in-file seam unworkable.

Tests may create temporary Git repositories. They must exercise the production
command construction and dirty decision, not merely test a separately written
predicate.

## 3. Required controls

### 3.1 Red reproduction before the fix

Pin the current failure first:

1. clean temporary repository with one commit -> clean revision;
2. add a non-ignored untracked source/header -> the current implementation still
   reports clean (**RED defect reproduction**);
3. apply B253 -> the same repository reports `<sha>-dirty`.

Mutation: restore `git diff --quiet HEAD` or disable untracked reporting. The
untracked control must turn RED.

### 3.2 Dirty matrix

The production helper/hook must pass all of these:

| repository state | required result |
|---|---|
| clean committed tree | `<sha>` |
| unstaged tracked edit | `<sha>-dirty` |
| staged tracked edit | `<sha>-dirty` |
| non-ignored untracked file | `<sha>-dirty` |
| non-ignored untracked directory | `<sha>-dirty` |
| ignored untracked file only | `<sha>` |
| detached `HEAD`, otherwise clean | `<sha>` |

The clean and ignored cases are negative controls: a broken implementation which
always appends `-dirty` must fail.

### 3.3 Failure and override matrix

Required fail-loud cases:

- no Git executable;
- not a repository;
- repository without a commit;
- failed status command;
- empty or malformed Git-derived revision;
- empty override;
- malformed override.

Required override controls:

- valid override is injected byte-for-byte;
- Git is demonstrably not invoked on that arm;
- removing the override returns to Git-derived state;
- log source is `override` vs `git` correctly.

Also inject porcelain output containing invalid UTF-8 bytes. It must be classified
as dirty without decoding or printing the path. This is a synthetic subprocess
control and therefore works on hosts whose filesystem cannot create that filename.

### 3.4 PlatformIO structural coverage

Extend the existing build-identity probe to derive effective environments from
`pio project config --json-output` and assert:

- every non-native board firmware environment has exactly one
  `pre:tools/git_rev.py` entry;
- native has no such entry;
- `src/firmware_commands.cpp` remains the only production C++ identity authority;
- `lib/` remains free of `GIT_REV`, `__DATE__` and `__TIME__`.

A hard-coded list of today's 13 names is not sufficient on its own: adding a new
environment must make the derived coverage check evaluate it.

Controls must remove the hook from one parsed board environment, add a synthetic
future board environment without it, add it to native, and duplicate it in one
board environment. Each must turn the coverage check RED. Mutating only a separate
hand-written list is not a valid control.

## 4. Slices and gates

### P0 — evidence (complete)

Record §1.1 and the effective-environment census. No production or tool change.

### P1 — provenance correction

Expected files:

- `tools/git_rev.py`;
- `tools/test_measure_board.py` and `tools/probe_build_identity.py` (or one focused
  provenance test if review demonstrates a cleaner ownership boundary);
- the C++ fallback comment only if needed for truthfulness;
- B253 register/design/baseline documentation.

No `lib/core`, frame, protocol, NV or simulator source changes.

Gate:

1. §3's complete test/control matrix, including every mutation RED and zero
   unusable controls;
2. B206's 18/18 measurement controls and the build-identity probe's 9/9 controls
   still green/RED as appropriate;
3. native suite unchanged;
4. simulator rebuild reports no relevant compile action and s18 remains exact;
5. run B206's deterministic runner twice for each approved ABI. Each environment's
   two **new** schema-2 manifests must compare exactly. Against B206's historical
   qualification, the source snapshot is expected to move because this slice
   changes the hook/tests/docs; the fixed identity, compiler state, RAM, flash,
   sections, object count and flashed-payload hash must remain equal. Do not claim
   the whole historical manifest is byte-identical;
6. one ordinary `gateway` build and one ordinary `heltec_mobile` build succeed and
   contain exactly one Git revision/stamp authority. Explicitly remove
   `MESHROUTE_GIT_REV_OVERRIDE` from these two build environments and require the
   log to say `source=git`;
7. only the owner-approved two board environments are built; no all-environment
   census is required;
8. `git diff --check` clean.

For an ordinary build from the currently dirty checkout, an unchanged `-dirty`
banner means the image may remain byte-identical. Do not demand or invent a board
size movement. The decisive behavior is the controlled clean/untracked matrix.

## 5. Closure and sequencing

B253 closes only when:

- the untracked-file reproduction turns from false-clean to dirty;
- all ordinary Git failure arms abort rather than issuing `nogit`;
- all effective board environments remain covered by the hook;
- B206's explicit override semantics remain unchanged, each new two-run ABI pair
  is exact, and the historical payload/measurement fields remain equal; and
- no second dirty/build-graph authority was introduced.

Sequence:

1. B206 S1/S2: **complete and independently QG-passed 2026-08-28**;
2. independently review and obtain the §0 owner rulings for this design;
3. implement and gate B253 as one slice;
4. implement the separate B205 build fix;
5. only then use deterministic board deltas as acceptance evidence for the
   internal-DATA/custody arc.
