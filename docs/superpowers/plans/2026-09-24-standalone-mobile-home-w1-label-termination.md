<!-- Author: Claude (brief author); QA: Codex (pre-check, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# Standalone Home W1 — B241: terminate peer labels in the UI adapter (`src`-only fix)

**Revision 3 — 2026-09-24 — DRAFT, awaiting Quality-Agent scoped review.** Revision 2 folded in the
[brief review](../evidence/2026-09-24-standalone-mobile-home-w1-brief-review.md) (HOLD, WR-1–WR-4); revision 3 folds
in the [scoped re-review](../evidence/2026-09-24-standalone-mobile-home-w1-brief-rereview.md) (HOLD on WR-4, with
fold-ins on WR-1 and WR-2). The changes are listed in §8. The accepted fix and the probe seam are unchanged.

- **Base:** commit **`4a230f4`** (owner commit "Pre-UI rework - design"). The simulator is at **`6585649`**, clean.
- **Inputs:** the executable inputs are committed at the base (§1). The documentation authorities and evidence are
  uncommitted and are pinned in the §1 inventory; commits are optional, preservation is not.
- **Freeze:** the coder pins this brief by the hash QA issues when the brief passes. Under P4 a brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** work happens in `/home/staszek/MeshRoute` on `main`, never in a worktree.
- **Authorities:**
  - the [W1 QA pre-check](../evidence/2026-09-24-standalone-mobile-home-w1-precheck.md) (source-fact ledger and
    baselines);
  - register **B241** (fix shape and CLOSE BY, rewritten 2026-09-24);
  - [design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) r2.18, §13 W1 and §14
    "Identity labels";
  - the process rules: `AGENTS.md` (P4–P7, D1–D6) and [agent roles](../../2026-09-02-agent-roles.md), including the
    owner ruling of 2026-09-16 on the coder's versus QA's gate.

## 0. What this is

**The defect.** `label_from_hash` hands its `%s` consumers an unterminated buffer:
- It copies a cached peer name with `Node::peer_name_find`, which returns a byte count and writes no terminator.
- It writes its `0x%08lx` fallback only when that count is zero.
- A short name such as `H1` leaves the destination's next byte untouched. On metal, stale bytes rendered as garbage
  after the name.
- A name of 15 bytes or more fills the 15-byte destination with no terminator at all.

**The fix.** Terminate in the one C-string adapter, and leave the raw API alone. `peer_name_find`'s full 32-byte
count is payload for two other consumers: the push body and `/mrpeers` persistence.

**Why the probe.** Native tests never compile `src/firmware_ui.cpp`, and the simulator never does either, so the
regression runs through the real UI translation unit in `tools/probe_firmware_ui`. A native guard also keeps the raw
API's full 32-byte count.

**Scope.** The slice is `src`-only and corpus-inert by construction.

## 1. Verified seams and pinned inputs at `4a230f4` (pin by symbol; lines are hints)

| Seam | Fact at the base | Change |
| --- | --- | --- |
| `label_from_hash` — `src/firmware_ui.cpp`, inside the file's anonymous namespace (`:~440–442`) | `if (g_node.peer_name_find(hash, out, cap) == 0) snprintf(out, cap, "0x%08lx", (unsigned long)hash);` — no terminator on the named path | **The fix (§2.1).** |
| `Node::peer_name_find` — `lib/core/node_hashlocate.cpp` (`:~450–458`) | Copies `min(name_len, cap)` bytes and returns that count. No terminator. A miss or a nameless row returns 0 and leaves the destination untouched. `protocol::peer_name_max` = 32 | **Unchanged.** |
| Raw 32-byte consumers: `Node::push_peer_key_cached` (`lib/core/node_hashlocate.cpp` `:~721–726`) and `mrfw::peer_store_sync` (`src/firmware_commands.cpp` `:~103–120`) | The first fills `Push::body` at capacity 32 and records the count. The second reads `char nm[32]` and passes `nm`/`nl` to `mrnv::peer_rec_put`. All 32 bytes are payload | **Unchanged.** |
| The invite-member projection in `build_snapshot` (`src/firmware_ui.cpp` `:~792–795`) | Passes `sizeof mem.name - 1`, writes `mem.name[nn] = '\0'`, and has no fallback | **Unchanged.** |
| Label consumers via `label_for_team_id` / `label_for_origin` (`:~448–460`) | TEAM rows (`:~766`, `TeamRow::label[kLabelCap + 1]`, zeroed by `UiSnapshot s{}`, which hides the short-name case); compose result (`:~2184`) and compose header (`:~2264`) through uninitialised `char label[kLabelCap + 1]` locals; receive routing (`:~2728`, `char who[…]` → `ui_route_recv_push` → `UiModel::on_reply`) | **No edit;** they receive a terminated string. |
| `mrui::kLabelCap` — `src/firmware_ui_model.h` (`:~224`) | 14; every UI destination is 15 bytes | Unchanged. |
| `tools/probe_firmware_ui/run.sh` | `build_variant` compiles the UI source under test straight to an object. `ctl(label, must_build, script)` sed-mutates a copy and rebuilds through `build_variant`; `must_build=no` declares a **required compile failure** — control **C0** restores `fw_context.h` and must fail to build. The BLE-row arm compiles `$FW_UI` and `probe_main.cpp` itself (`:~180–186`). `md5_sources` is the tree tripwire. Control O8 (`:~1446`) replaces the invite projection with `label_from_hash` | §2.2, §2.5 |
| `tools/probe_firmware_ui/probe_main.cpp` | Reaches the UI only through the public seam (`mr_ui_*`). `label_from_hash` is file-local, so it cannot be called directly today | §2.2–§2.3 |
| Native | `platformio.ini` `test_build_src = no`. The full-32 name checks in `test/test_node_hashlocate.cpp` use 64-byte destinations, so a global `cap − 1` change would pass them. The public `Node::on_hash_bind_pubkey` path already reaches `push_peer_key_cached` in that file (`:~1940–1963`) | §2.4 |
| `tools/probe_ui_model_mutations.py` | `PIN_CASES, PIN_ASSERTS = 2950, 195770`, with one derivation comment per change. It is an advisory cross-check: a stale figure prints the B217 banner and never changes an exit code. None of its 105 `TARGET_SRC` mappings names a fenced file (QA's read-only census) | Re-synced if the native counts move (§2.6) |
| `tools/warning_census.sh` | Clean builds of every derived, pinned OLED env (six); exits non-zero on a build failure, zero objects, a warning-count mismatch against its pins, any `-Wswitch` or a missing metric | Run by the coder (§4.1) |
| `tools/measure_board.py` | Deterministic, fixed-identity RAM/flash measurement of the ruled pair. `pair --jobs=1 --output <dir>` builds `gateway` then `heltec_mobile` at fixed `.pio-measure/env/` paths under one lock and writes `<dir>/<env>/manifest.json` per env. `validate_output_dir` accepts an output inside the repository only below the gitignored `.pio-measure/` (never `.pio-measure/env/`), or any directory outside it. `compare <manifest> <manifest>` takes two per-env manifest **files** and checks exact repeatability of the **same source**: every qualification field must match (`compare_qualification`), including the source snapshot (tree hash, Git status, nonignored untracked files) and the ordinary `.pio` metadata | Run by the coder (§4.1); not edited |

**Executable inputs** — the fenced files; SHA-256 of `git show 4a230f4:<path>`:

| File | SHA-256 |
| --- | --- |
| `src/firmware_ui.cpp` | `e1a491ae81c77e083360ace46456c41301ad00f4373128dba9655d30bf70c5be` |
| `tools/probe_firmware_ui/run.sh` | `f51a3e931ff4096163e1c89f3ae85e4dcddca7cbd46cf9c765aec872d574eb36` |
| `tools/probe_firmware_ui/probe_main.cpp` | `57d93951575e9ce9c45985b03f105db900c81ee00d8ef4c76fc0443d82996a24` |
| `test/test_node_hashlocate.cpp` | `2aa7932b6e7c5ffa8ea627c3dd87b10442cc1d9c34216e4486a7a87d78d7762c` |
| `tools/probe_ui_model_mutations.py` | `61f1cd06e363af9433757f8a79128401dd691e89ba92c4ee0f83a8eff656d67d` |

**Permitted preparation set** — uncommitted documentation authorities and evidence, to be preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | authority (r2.18) | `3eb9bc94e3982ac67ae076c1092c4f5db06adca04d1a1a218ba333a94bdee3ea` |
| `docs/2026-07-30-open-bug-register.md` | authority (B241) | `00e09d0a6e5142374fd99803febc068d352c825fa20e553a69689b270fa22807` |
| `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1-precheck.md` | authority (pre-check) | `19e7eae4247699a47fb17c3ff80492a4922782dad12feb30720da372532a40e1` |
| `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1-precheck/SHA256SUMS` | evidence — covers the directory's other ten files, including `corpus-manifest.json` (`ab513423…a067431`), the baseline | `76c6b5fc89eac2584bbf2ef719d7b04e8656667dde4ee8cc09b97fd900aa7f85` |
| `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1-brief-review.md` | authority (brief review) | `8a6eea90e18013232a4626e35c2e014c7f44a6ce1faf545717fa30a3248ec009` |
| `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1-brief-review/SHA256SUMS` | evidence — covers the directory's other six files | `d378e9c0e507f0ec8c10212fa72e6edf2755d0e454bf9566f83091653e27c3c3` |
| `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1-brief-rereview.md` | authority (scoped re-review) | `8eb6d79697a7085a9059b6617c809c9b41b74d9f130ad702532267fdb95677ef` |
| `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1-brief-rereview/SHA256SUMS` | evidence — covers the directory's other five files | `0d5b5b154589387862b405d50b208f0b6441cbdcc8db8e80e26fd57640b9246d` |
| `tracker.md`, `MEMORY.md` | context pointers | `197616512cea9f347d97202908ef663a2361d263b61ad3860ddb5e494075f54b`, `a354886f972591bd31233fb97e9bd8846286646d409053cf8cce56fbba08159e` |

This brief itself is pinned by the hash QA issues on PASS, not listed here.

**Preflight:**
1. Verify both repositories: MeshRoute `HEAD` = `4a230f4`, the simulator at `6585649` and clean.
2. Every executable input is clean against `HEAD` (`git diff --quiet HEAD -- <path>`).
3. Every preparation-set hash matches, and each evidence directory verifies (`sha256sum -c SHA256SUMS`).
4. Classify every other entry in `git status --porcelain`; anything outside this inventory is reported.

A different `HEAD`, a dirty executable input, a changed preparation-set file (`tracker.md` and `MEMORY.md`
included), or an unexplained change is STOP-1 to QA. The whole preparation set stays unchanged from preflight
through the freeze; nobody edits it while the slice runs.

## 2. Contract

### 2.1 The fix — one function in production

`label_from_hash(hash, out, cap)`:
- **`cap == 0`:** writes nothing and returns before any subtraction.
- **`cap ≥ 1`, cached name:** calls `peer_name_find(hash, out, cap − 1)` and writes `out[n] = '\0'` at the
  returned count `n`.
- **`cap ≥ 1`, nameless or unknown hash:** writes the existing `0x%08lx` fallback, spelled exactly as today
  (snprintf terminates it).

The result is terminated in every case with `cap ≥ 1`. For a 15-byte destination that means at most 14 name bytes,
the same visible clamp as `kLabelCap`.

Update the function's comment to say what it guarantees and why the raw API stays unterminated (the two 32-byte
consumers), per the owner's rule that a partly landed mechanism states in source what it does and why.

**Nothing else in production changes (C1/U1):**
- not `peer_name_find` or any `lib/` file;
- not the callers or the fallback spelling;
- no sanitisation, `»`, name precedence or default-name change (W1c and W4a own those);
- no clearing in any renderer, no extracted helper, no test hook, macro or linkage change in `src/`.

### 2.2 The probe seam — instrument only

The probe must reach the real, file-local adapter. Do it in the same translation unit, from the probe's side:
- Every build that links `probe_main.cpp` compiles the UI source under test through a **generated wrapper TU** in
  `$OUT`. That covers the `l2` live run, the `v3` child-enabled arm, the BLE-row arm and every `ctl` control.
- The wrapper `#include`s the source path that `build_variant` (or the BLE-row compile) was handed — the real
  `src/firmware_ui.cpp` or a control's mutant, never a hard-wired path. It then defines one probe-only trampoline
  with external linkage, for example `void mr_probe_label_from_hash(uint32_t, char*, uint8_t)` forwarding to
  `label_from_hash`.
- `probe_main.cpp` declares the trampoline and calls it.

**Rules for the seam:**
- The compiled code is the production source itself, never a copied implementation.
- A wrapper hard-wired to the live source would defeat every control.
- The `-Werror` bar on the live build stays.
- `md5_sources` keeps hashing the real files and its meaning.

The QA review compiled such a wrapper around the unchanged source in all three configurations under `-Werror`.
The coder may choose an equivalent same-TU mechanism if it satisfies all four rules, and states it in the report.

### 2.3 Probe checks — every arm

1. **Direct, deterministic** (through the trampoline):
   - **Setup:** a 15-byte destination pre-filled with a non-zero poison byte, guarded by canary bytes on both sides.
   - **Names checked:**
     - `H1` and `H2`;
     - a rename from a long name to `H1`, where the destination keeps its earlier poison and content;
     - names of 14, 15 and 32 bytes.
   - **Expect:** exact bytes, NUL at `min(len, 14)` and both canaries intact.
   - **Nameless or unknown hash:** exactly `0x%08lx`.
   - **Capacity 0/1:** if tested, label these cases *synthetic*; production callers pass 15.
   - **Bounded checks:** check bytes with bounded operations before any C-string rendering, so a diagnostic never
     depends on an unterminated read.
2. **Consumers, through the public seam:** for a named, a nameless and an unknown peer, the exact label renders in
   each consumer, with that screen's existing formatting and clamp. W1 does not adopt W4a's identity format.
   - the TEAM row;
   - the compose header and the compose result;
   - the receive path — for example a same-team reply to an alarm, which reaches `UiModel::on_reply`.

   A zero-initialised snapshot alone is not accepted as proof. If the coder claims a consumer's label is
   unreachable in every probe state, the report gives the source reason. QA disposes of that claim; it is not an
   automatic waiver.

### 2.4 Native guard on the raw API

In `test/test_node_hashlocate.cpp`, add an exact-capacity case:
- a 32-byte cached name read into a 32-byte destination followed by one canary byte;
- `peer_name_find` returns 32 and copies all 32 bytes, and the canary survives;
- `push_peer_key_cached`'s push body carries the full 32-byte name, reached through the existing public
  `on_hash_bind_pubkey` path (no core test hook).

A global `cap − 1` change must fail this case.

### 2.5 Controls (`run.sh` `ctl`)

**The two new B241 controls** must compile and go RED on the intended poisoned-buffer checks of §2.3(1), not on an
unrelated assertion. A crash, a vacuous sed or a failed compile is not a usable RED:
- **The original missing terminator:** the pre-fix body restored.
- **The tempting wrong repair:** the full capacity passed and only `out[cap - 1] = '\0'` written. The poison after
  `H1` survives.

**Existing controls:**
- Every existing `must_build=yes` control keeps compiling and going RED for its own property.
- **C0 keeps its `must_build=no` contract:** restoring `fw_context.h` must still fail the build, now through the
  wrapper. QA reproduced that compile failure through the wrapper.
- O8 and the invite full-hash/no-fallback controls keep their meaning.
- Audit every exact-source reader of the edited lines before editing (D6/P7). If a reader in `run.sh` keys on a
  displaced line, re-anchor it, keep its negative control, and name the change in the report.

### 2.6 Nothing else moves

- **Corpus:** 36/36, byte-identical to the pre-check's `corpus-manifest.json` (with full MD5 and SHA-256 per
  stream).
- **No change to:** `Node`, NV, the wire, `lib/`, or any file outside §3.
- **Native counts:** if they move, re-sync `PIN_CASES, PIN_ASSERTS` in the existing shape: the literal plus one
  derivation comment line, keeping the pin bare (D5). Then run tools discovery.

## 3. Fence

**IN:**
- `src/firmware_ui.cpp` — the `label_from_hash` body and its comment only;
- `tools/probe_firmware_ui/run.sh`:
  - the wrapper-TU compile in every arm and in `ctl`;
  - the two new B241 controls;
  - any justified re-anchoring of an existing reader in this runner that the edit displaces, named in the report;
- `tools/probe_firmware_ui/probe_main.cpp` — the trampoline declaration, the new checks and their fixtures;
- `test/test_node_hashlocate.cpp` — the exact-capacity case;
- `tools/probe_ui_model_mutations.py` — the PIN literal and one comment line, only if the native counts move;
- the evidence file and the evidence directory (§5).

**OUT:**
- `lib/` in full, `Node`, NV records, the wire;
- every other `src/` file, `platformio.ini`;
- `tools/probe_board_ui` (B418 belongs to W2);
- `tools/warning_census.sh` and its pins, and `tools/measure_board.py` (used, not edited);
- every native mutation battery's entries, patterns and targets in `tools/probe_ui_model_mutations.py`;
- every existing `ctl` control's meaning, including C0's required compile failure;
- the design, register, tracker, `MEMORY.md` and metal plan (QA lands the documentation).

## 4. Gates

The owner's ruling of 2026-09-16 (agent roles, P6) gives the coder the full chain, and QA an independent `src`-only
gate.

**Ordering, throughout:** run source-mutating instruments (the mutation batteries) separately from builds, board
measurements and coder edits. `measure_board.py`'s lock does not cover them.

### 4.1 The coder's gate

**Baselines, captured before the first implementation edit:**
- **Probe:** run the unmodified `tools/probe_firmware_ui/run.sh`, with its default controls. Record per arm the
  passed and total checks, and "controls verified / unusable".
- **Boards:** two back-to-back runs of the unmodified tree into fresh, empty directories below the gitignored
  `.pio-measure/`: `python3 tools/measure_board.py pair --jobs=1 --output .pio-measure/w1/base-1`, then the same
  into `.pio-measure/w1/base-2`. Nothing may touch the checkout or the ordinary `.pio/` between the two runs — no
  edit, no new nonignored file, no native or probe build — because the source snapshot and `.pio` metadata are
  qualification fields. Repeatability is the stock `compare` once per env:
  `compare .pio-measure/w1/base-1/gateway/manifest.json .pio-measure/w1/base-2/gateway/manifest.json`, and the
  same for `heltec_mobile`. Both must PASS.

**After the implementation:**
1. **Whitespace:** `git diff --check`.
2. **Native:** `pio test -e native`, then **run the binary** `./.pio/build/native/program`. Report the case and
   assertion counts derived from the binary; the pre-check baseline was 2,950 / 195,770 / 0. Zero failed, zero
   skipped.
3. **Corpus:** stock simulator rebuild, then `tools/run_corpus.py --require-anchors`. Expect 36/36, with streams
   byte-identical to the pre-check manifest.
4. **Firmware-UI probe:** `tools/probe_firmware_ui/run.sh` with its default controls.
   - Every arm green, and the tree tripwire clean.
   - Per arm, report checks and "controls verified / unusable" against the baseline.
   - The two new controls go RED on their intended checks; C0 still fails to build; 0 unusable.
5. **Boards:** two back-to-back runs of the implemented tree into `.pio-measure/w1/final-1` and `final-2`, under
   the same no-touch rule, then the stock `compare` per env (`final-1` against `final-2`); both must PASS.
   - **Base to final is not a stock `compare`:** it would fail on the intended source change by design. Instead,
     read the `base-1` and `final-1` manifests per env and report every `measurements.*` difference (RAM, flash,
     object count, loadable sections, symbol count and total, `symbols_sha256`) and `artifacts.payload.sha256`,
     each attributed.
   - `source.*` differences are expected and recorded. `toolchain.*`, `fixed_identity.*` and `paths.*` must be
     identical — that is the compatibility check.
   - Never reinterpret or weaken a failed repeatability verdict.
   - `gateway` does not compile the OLED TU, so its `measurements.*` and payload are predicted unchanged.
     `heltec_mobile` is measured and every delta attributed. A delta is an attributed measurement, not a numeric
     allowance. The ruled pair runs sequentially (`--jobs=1`).
6. **Warning census:** `tools/warning_census.sh` on its six pinned OLED envs — the stated exception to the two-env
   rule. Zero new warnings, `-Wswitch` 0, pins unchanged. No new env and no re-pin is authorized.
7. **Mutation union:**
   - Derive (a) the batteries whose configured source file this slice changes — QA's census found none — and
     (b) the dependency or historical batteries that define this acceptance surface.
   - Name and justify (b), then run the union of (a) and (b).
   - No battery entry, pattern or target changes.
8. **Tools discovery** (D5), if `run.sh` or the mutation harness changed:
   `python3 -m unittest discover -s tools -p "test_*.py"`. Report the count derived, OK, 0 skipped.

**Input stability:** the executable-input and preparation-set hashes are recorded before the first gate step and
again after the last. They must match each other and the final inventory (§5); a change in between restarts the
chain.

### 4.2 QA's independent gate (P6, `src`-only)

QA re-runs, on the frozen tree:
- native (binary run);
- the corpus (36/36, byte-identical);
- the two boards: its own `pair --jobs=1` into a fresh `.pio-measure/` or external directory. Its per-env
  `measurements.*` and payload hashes must equal the coder's `final-1` manifests. Its source snapshot may differ
  from the coder's only by the report and evidence written after the coder's last run, so this is the same
  field-by-field reading, not the stock `compare`;
- the touched mutation batteries, which is selector (a) — none, per QA's census. QA may add any battery from the
  coder's union at its own discretion;
- the firmware-UI probe with its controls.

The six-env census and the full union stay in the coder's chain, per the ruling.

### 4.3 STOP conditions

- any corpus stream delta;
- any edit outside §3, or any `lib/` edit;
- a new B241 control that is vacuous, crashes, fails to compile, or goes RED on an unrelated check;
- C0 building, or any existing control losing its meaning;
- any probe arm RED;
- a warning-census failure;
- a failed repeatability `compare` (base or final), an unattributed board delta, or a `gateway` delta;
- a fenced file dirty at preflight, a changed authority, or an input change during the chain.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1.md`, with an author line on line 1.
**Evidence directory:** `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-w1/`. It holds:
- the eight per-env manifests (`base-1`, `base-2`, `final-1`, `final-2` × `gateway`, `heltec_mobile`), copied from
  `.pio-measure/w1/` **after the last measurement run**, never between the two runs of a pair;
- the four repeatability `compare` outputs and the base-to-final attribution table;
- the probe, census, union and native logs;
- a `SHA256SUMS` covering everything.

The raw measurement directories stay under the gitignored `.pio-measure/w1/`, and other large build trees stay
outside the repository; the report names both.

The report contains:
1. **Preflight:** both repositories' identity and status, and every §1 hash as found.
2. **Baselines:** the probe counts, and the two base measurements with their per-env `compare` verdicts.
3. **Diff:** `git diff --stat` for the fenced files.
4. **Figures:** every figure from §4.1, derived by the coder. None may be copied from this brief or the pre-check.
5. **Controls:** each one with its verdict, C0 included.
6. **Selectors:** the two mutation selectors and their union.
7. **The exact line** `PIN re-synced? YES — <derivation>`, or `PIN re-synced? NOT NEEDED — native counts unchanged`.
8. **Freeze inventory:**
   - the final SHA-256 of every fenced file;
   - the authorized brief hash;
   - the simulator commit and status;
   - the preparation-set hashes, unchanged;
   - every new evidence path with its hash;
   - the stability statement from §4.1.
9. **Not run,** with the reason for each item.

## 6. Owner rulings

None requested. The fix shape is B241's listed alternative, recommended by the QA pre-check. The design's package
plan was aligned to it in r2.18, and no product behaviour or ruling changes.

## 7. Landing (QA, on PASS)

- **B241:** closed in place with the measured figures and the evidence link.
- **Documentation:** a design status note on §13 W1, plus pointers in `tracker.md` and `MEMORY.md`.
- **No new metal row** (M2): the probe reaches this behaviour deterministically. The metal plan's existing UI-13
  procedure names its devices H1/H2 and shows their labels on glass at the owner's next UI bench session.
- **Next package:** W1c or W0, per the owner's order.

## 8. Revision history

**Revision 3 (2026-09-24)** folds in the scoped re-review:
- **WR-4:** the board recipe now matches the stock tool. Outputs go below the gitignored `.pio-measure/`
  (`validate_output_dir` rejects `docs/`). `compare` takes per-env manifest files. It is used only for same-source
  repeatability, twice at base and twice at final, with nothing touching the checkout or `.pio` in between.
  Base-to-final is a field-by-field attribution of `measurements.*` and the payload hash, with `source.*` changes
  expected and toolchain, identity and path fields required identical. Manifests are copied into the evidence
  directory only after the last run. QA's board check reads fields the same way (§1, §4.1, §4.2, §4.3, §5).
- **WR-1 fold-in:** QA's independent gate re-runs the touched batteries (selector (a), none), not the coder's full
  union (§4.2).
- **WR-2 fold-in:** the exception letting `tracker.md` and `MEMORY.md` change during the slice is removed; the whole
  preparation set stays unchanged through the freeze. The set now also pins the re-review report and its evidence
  folder, which the header cites (§1).

**Revision 2 (2026-09-24)** folds in the brief review's four corrections:
- **WR-1:** the coder's gate now includes the six-env warning census and the mutation union; QA's independent
  `src`-only gate is stated separately (§4.1–§4.2).
- **WR-2:** the uncommitted authorities and evidence are inventoried with hashes; preflight verifies both
  repositories; the report ends with a freeze inventory and an input-stability statement (§1, §4.1, §5).
- **WR-3:** the compile-plus-assertion rule applies to the new controls and existing `must_build=yes` controls,
  while C0's required compile failure is preserved. The fence names what `run.sh` may change and limits the OUT
  line to the native batteries (§2.5, §3).
- **WR-4:** the probe and board baselines are captured before any edit. Boards are measured with
  `measure_board.py` (base twice for repeatability, then final and `compare`), evidence goes in a named directory,
  and the flash allowance is removed (§4.1, §5).

It also takes up the review's accepted notes: a wrapper that is never hard-wired, bounded checks before any string
rendering, the existing public push path, screen-native formatting, and QA disposition for an unreachable consumer.
The fix itself and the probe seam are unchanged.

**Revision 1 (2026-09-24)** was the first draft.
