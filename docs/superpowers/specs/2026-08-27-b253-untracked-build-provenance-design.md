<!-- Author: OpenAI Codex -->
# B253 untracked-build provenance design

*2026-08-27. Status: PREPARED FOR REVIEW — do not dispatch until B206 S2 has
landed, because both slices own `tools/git_rev.py`. Scope: make ordinary board
build provenance fail closed and include non-ignored untracked files. No protocol,
frame, routing, NV or product behaviour changes.*

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
   runner (and deliberate archive/release tooling). Do not add another override
   or silently fall back to it.
4. Preserve the existing decision to ignore submodule dirtiness. Ignored files
   remain outside this revision marker; a future build must not consume an
   ignored generated source without separately defining its provenance.

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

The effective `pio project config --json-output` configuration shows all 13 board
environments consuming `pre:tools/git_rev.py` exactly once. `native` intentionally
uses only `pre:tools/ccache_native.py` and relies on the C++ fallback because it is
not a provenance-bearing board image.

This is current evidence, not a permanent assumption. The structural probe must
derive the environment list from effective PlatformIO configuration and keep this
property pinned when environments are added or inheritance changes.

### 1.3 B206 S2 establishes the override seam

The in-progress B206 S2 slice adds the validated environment variable
`MESHROUTE_GIT_REV_OVERRIDE`, accepting 7–40 lowercase hexadecimal digits with an
optional `-dirty`. B253 consumes that exact seam after S2 lands. It must not edit
the validation language opportunistically or change deterministic measurement
manifests.

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

1. resolve the repository from PlatformIO's explicit project directory, not the
   process's incidental current working directory;
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

### 2.2 Fail-loud normal builds

On the ordinary Git-derived path, all of these abort the PlatformIO build with one
bounded diagnostic:

- Git executable unavailable;
- project directory is not inside a Git worktree;
- `HEAD` cannot be resolved (including an empty repository);
- revision is empty or malformed;
- porcelain status exits non-zero or cannot be decoded.

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

When it is absent, print `source=git`. This makes an accidentally inherited
override visible in every board log. B253 does not authorize a weaker or broader
override grammar.

### 2.4 Testable seam

The Git resolution logic must be testable without compiling a board. Either keep a
small pure helper importable by the hook or execute the real hook through a fake
PlatformIO environment; do not copy its Git rules into a test-only implementation.

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
- empty override;
- malformed override.

Required override controls:

- valid override is injected byte-for-byte;
- Git is demonstrably not invoked on that arm;
- removing the override returns to Git-derived state;
- log source is `override` vs `git` correctly.

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

## 4. Slices and gates

### P0 — evidence (complete)

Record §1.1 and the effective-environment census. No production or tool change.

### P1 — provenance correction

Files expected after B206 S2 lands:

- `tools/git_rev.py`;
- the existing build-identity probe and/or one focused provenance test;
- the C++ fallback comment only if needed for truthfulness;
- B253 register/design/baseline documentation.

No `lib/core`, frame, protocol, NV or simulator source changes.

Gate:

1. §3's complete test/control matrix;
2. existing build-identity probe still green;
3. native suite unchanged;
4. simulator rebuild reports no relevant compile action and s18 remains exact;
5. B206 deterministic runner with its fixed override reproduces its qualified
   `gateway` and `heltec_mobile` manifests exactly;
6. one ordinary `gateway` build and one ordinary `heltec_mobile` build succeed and
   contain exactly one Git revision/stamp authority;
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
- B206's explicit override and deterministic manifests remain unchanged; and
- no second dirty/build-graph authority was introduced.

Sequence:

1. finish and independently gate B206 S2;
2. rebase this design on the landed override spelling if it changed;
3. implement and gate B253;
4. implement the separate B205 build fix;
5. only then use deterministic board deltas as acceptance evidence for the
   internal-DATA/custody arc.
