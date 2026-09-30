<!-- Author: Claude (brief author); QA: Codex (pre-check, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# Standalone Home W0 — the identity record: one live conversion, the rename service and loud refusals (`src` fix)

**Revision 3 — 2026-09-30 — for QA's scoped re-review.** The [scoped re-review](../evidence/2026-09-29-standalone-mobile-home-w0-brief-rereview.md)
closed W0R-1–W0R-3 and held revision 2 (`393af1fc…31ca`) on W0R-4; this revision settles it by the owner's ruling (§8).

- **Base:** owner commit **`0f23aee`** (`0f23aee8f5eeaa6e176e3ea37b2acaffb33622ec`, "W6"), with no uncommitted candidate. The
  pre-check's `inputs.json` is the authority for the whole tree (§1). The simulator is at **`6585649`**
  (`6585649ea5a780f0542b2931853a667be56a5b2b`), clean and read-only.
- **Freeze:** the coder pins this brief by the hash QA issues when it passes review. Under P4 the brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** `/home/staszek/MeshRoute` on `main`, never a worktree. Preserve every tracked and untracked file.
- **Authorities:**
  - the [W0 QA pre-check](../evidence/2026-09-29-standalone-mobile-home-w0-precheck.md) — cited as "pre-check §n";
  - the [design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) **r2.24** — §4.3 (the
    identity-record contract) and §13 W0;
  - the [register](../../2026-07-30-open-bug-register.md): B440, B448 and B482, which W0 closes; B478 and B483–B486
    stay separate;
  - the process rules: `AGENTS.md` / `CLAUDE.md` (C1–C3, U1–U3, P4–P7, D1–D6, M1–M2) and
    [agent roles](../../2026-09-02-agent-roles.md), including the owner ruling of 2026-09-16 on the coder's versus
    QA's gate.

## 0. What this is

**The defects — measured on the real config and command TUs (pre-check §3, §5):**
- **B440.** `cfg set name`, `cfg set lat`, `cfg set lon` and `regen` each rebuild the `/mrid` record field by field,
  starting from whatever `load_id` left behind. After a failed or rejected read they save an empty name, a zero
  position or a lost coordinate, although the running device knows all of them. A failed read does not even promise
  a zeroed record: a short read leaves a prefix, and a bad header leaves the old payload.
- **B448.** `cfg set name` cuts a name over 32 bytes to 32 bytes — even through the middle of a UTF-8 character —
  and still answers `cfg ok name`.
- **B482.** `cfg set lat` / `lon` change the live position before saving it. After a failed save the device reports
  `nv_save_failed` but keeps using the new position, which reboot then loses.

**The fix — one package (pre-check §4, §9):**
- **One pure conversion** beside `IdBlob`, building a complete candidate record from live values only.
- **One rename service** — design §4.3's transaction, with a typed result. The console's `cfg set name` is its first
  caller; W7's panel will be the second.
- **The same conversion** serves the two coordinate setters and `regen`.
- **Loud refusals:** a name over 32 bytes answers `too_long`, with zero writes.
- **Publish after save:** a coordinate is published live only after its save succeeds (B482).

**Owner rulings:**
- **2026-09-29:** B482 folds into W0. B478 — the mutation tool's decoding — is its own tool package after W0 and
  before W7, so W0's tests print bytes in hex, never raw high bytes.
- **2026-09-30:** the inbox-verbs transcript comparator is broken at the base (B487, B488), so it is not one of W0's
  gates. B487 and B488 join B478's tool package. §4.2 names the proofs that cover W0 instead.

**Not in W0:**
- the boot load and re-mint policy;
- the BLE `cfg set` and `ready` adapters (B483, B485);
- the leaf-label setters (B484);
- nRF's record-length check (B486);
- `Node::set_name` and the core's name capacity;
- the panel rename (W7);
- any `lib/`, wire, NV layout or version, simulator, `platformio.ini` or `src/fw_main.cpp` change.

## 1. Verified seams and pinned inputs (pin by symbol; lines are hints from the pre-check inventory)

The complete per-seam facts are in pre-check §3–§7; the brief relies on them.

| Seam | Fact at the base | W0 change |
| --- | --- | --- |
| `device_nv.h`: `IdBlob` (`:~146`, 80 B, align 4; magic 0, version 4, name_len 6, seed 8, name 40, lat 72, lon 76), `kIdMagic`, `kIdVersion`, `load_id` / `save_id` (`:~1470–1474`) | Exact count/magic/version predicate; no coalescing in `save_id` | A pure `id_blob_from_live` helper beside `IdBlob` (§2.1); nothing else in the file changes |
| `firmware_config.cpp::handle_cfg_set`, the lat/lon arm (`:~262–270`) and the name arm (`:~274–282`) | Build from a failed load plus the running seed; the name clamps at 32; lat/lon publish before saving | §2.2–§2.4 |
| `firmware_config.h` | No identity service | The typed result and the service/adapter declarations (§2.2) |
| `firmware_commands.cpp::do_regen` (`:~1067–1107`) | CLIENT debt admission; an unchecked `load_id`; a new seed; one checked `save_id`; then the installs | §2.5 |
| `fw_main.cpp`: the boot load and re-mint (`:~915–929`), the BLE `ready` read (`:~525–546`), the BLE `cfg set` branch (`:~580–587`) | Boot policy; B485; B483 | **Unchanged** (out of the fence) |
| Live authorities | Seed `g_identity.seed`; name `g_node.effective_name(out, 32)` (counted, no terminator); position `g_lat_e7` / `g_lon_e7`, mirrored into `g_node.config().lat_e7` / `lon_e7` | The only source of a candidate's fields (§2.1) |
| `tools/probe_inbox_verbs/probe_main.cpp` (`:~264–278`) | Stubs `handle_cfg_set` and seven sibling handlers, each recording that it ran | A narrow stub exclusion for the new identity arm only (§2.7) |
| `tools/probe_inbox_verbs/run.sh` builder | `rc=0` / `if ! build_support;` appears once; `tools/probe_deferred_actions/run.py` reuses the prefix | Kept exactly; the identity arm is added beside it (§2.7) |

**Executable inputs.** These are the fenced files. Each hash is the working-tree SHA-256 at the base.

| File | SHA-256 | Lines |
| --- | --- | ---: |
| `src/device_nv.h` | `5c0e4352ce99460146074a70151f94dde58a26e67ecc98142bbe004d4c9ce79c` | 1590 |
| `src/firmware_config.h` | `32cabbba4e56706a6abff945507f378278d39c067f4c4dcdba64050fde97015e` | 237 |
| `src/firmware_config.cpp` | `a104f0c87ecefb450f051a383b56b4c6e7195135d39399a2c6cc64aae2b32f2a` | 2436 |
| `src/firmware_commands.cpp` | `ce1113baa75ecc991a9c8219862334fc6354155b99fa06f65d10f961a72fe128` | 1915 |
| `test/test_device_nv.cpp` | `93865b037b534addf7f7bfcbc084d94372a30bbf91cbe70205bf26b1085d1212` | 930 |
| `tools/probe_inbox_verbs/run.sh` | `098fc041537e0fa64aaa3cfb5bea04066428737368071a8a9dd133e574648e61` | 933 |
| `tools/probe_inbox_verbs/probe_main.cpp` | `263dc475a6aed2309dc4d1fee2aeb4761c2dc6cd10f5a072340fb718b6b765f1` | 2432 |
| `tools/probe_inbox_verbs/identity_main.cpp` | **new file** — the real-config identity arm's driver | — |
| `tools/probe_inbox_verbs/identity_platform.h` | **new file** — that arm's local platform shim | — |
| `tools/probe_ui_model_mutations.py` | `8dfc782a8e81acea1c52bf70299747665f15bea20643dbf272aa87a95edad5f1` | 12894 |
| `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` | `ca393c0d18451a252dd58c62db8a679793e2ec15cff1284f15a93334c93f6741` | 259 |

The two new paths are absent at authoring.

**Read-only — unchanged; the gates rerun each of these except `transcript_main.cpp`, which is not run (§4.2):**

| File | SHA-256 | Lines |
| --- | --- | ---: |
| `tools/probe_deferred_actions/run.py` | `694387bd6fe1baa1671ff23e1fc0e2002eb844b6af7dd2538ab8de24dfb3b55f` | 190 |
| `tools/probe_inbox_verbs/transcript_main.cpp` | `3914de10acddb6a6e39384fa4f512461133e1d7ca538f50cb66939a8cd828163` | 148 |
| `tools/probe_deferred_actions/probe.cpp` | `57364be0188f908c62f1a4659c7625def338bf3b6cc9bc09865a2ad7a44ae587` | 222 |
| `tools/probe_console_sink/structural.py` | `753c2477e256a49a220565cddf419103faefeb1dbfbea61b1e55342368d36c19` | 908 |
| `tools/probe_board_ui/run.sh` | `366a8bf089604c5391b3933b284561c33b6ce5e05cdf5a9e0c8255210beea5bf` | 1665 |
| `tools/probe_board_ui/expected.tsv` | `2a30e4d1f10683a17b46eccbdd1b31de4ac7252a7491d9ff60e2d202edcb2196` | 598 |
| `tools/gen_command_inventory.py` | `b9d141a4b6a560a3f4bfd0af68bbdb2c461fcd38a3b67eb5ac04fb206f3db68a` | 1186 |

**Base inventory.** The pre-check's `inputs.json` (2,123 MeshRoute paths, covered by its `SHA256SUMS`) is the authority
for every other path. Since QA wrote it, only the four preparation documents below have changed. New paths since
then: the pre-check report and folder, this brief, and QA's review artefacts.

**Permitted preparation set.** These uncommitted documents are preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | authority (r2.24) | `da85f228c03d246cc50473caa1853a05bac778ab733d428da6e86f4838f166f0` |
| `docs/2026-07-30-open-bug-register.md` | authority (B440, B448, B478, B482–B488; §0) | `88fe7cea2aabb382dc3a50da32911957db2ab28f9400e4b243b00ab905834b86` |
| `docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0-precheck.md` | authority (pre-check) | `a10f1186cfb595ea42f05b84efeac6c0717945e1ee18c80f526f45237fa40942` |
| `docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0-precheck/SHA256SUMS` | evidence — 36 entries, including `inputs.json` (`552fe453…f1a7`) | `88fed7716cfe8d130d339d5e0307b275ffc5b8ca0bc32b547b1f0ed690b916c1` |
| `tracker.md`, `MEMORY.md` | context pointers | `ee6ae9daee59901a29b7c17378f61bcabe3ca2aa6a38b1f08733bc8f88e59669`, `242610cc1bca53d02e73e861f15b2eb7b9774fff892979cb33dc0f2db057f229` |

The design changes are r2.24: §4.3's service and conversion, B482's rule, W0R-1's correction of §4.3's item 2, §13
W0, and the transcript note. The register changes are QA's B482–B486 from the pre-check and B487/B488 from the
re-review, the owner's rulings in the B478, B482, B487 and B488 rows, and the W0 entry in §0. The tracker and
`MEMORY.md` pointers carry the same rulings. This brief is
pinned by the hash QA issues on PASS, not listed here.

**Preflight:**
1. Check both repositories: MeshRoute `HEAD` = `0f23aee`; the simulator at `6585649` and clean.
2. Every executable and read-only input matches its hash above, and the two new paths are absent.
3. Every preparation-set hash matches, and the pre-check folder verifies (`sha256sum -c SHA256SUMS`, 36 OK).
4. Every path in `inputs.json` still has its recorded hash, except the four preparation documents.
5. Classify every other path. QA review reports and evidence added after this brief are explained preparation
   additions; anything else is reported.

Any of the following is STOP-1 to QA: a different `HEAD`, a mismatched input, a changed preparation file, or an
unexplained path. The whole preparation set stays unchanged from preflight through the freeze.

## 2. Contract

### 2.1 One pure conversion (design §4.3; pre-check §4)

`src/device_nv.h` gains one pure inline helper beside `IdBlob`, above the platform conditionals — for example:

`bool id_blob_from_live(IdBlob& out, const uint8_t (&seed)[32], const char* name, size_t name_len, int32_t lat_e7, int32_t lon_e7)`

It must:
- validate `name_len` ≤ 32 as a `size_t`, **before** any narrowing, and accept an empty name;
- initialise the complete 80-byte candidate deterministically — unused name bytes zero — then stamp `kIdMagic` and
  `kIdVersion` and copy every field;
- on refusal, leave no partially usable candidate;
- touch no NV, globals, `Node` or Arduino, and have no side effects.

Nothing else in `device_nv.h` changes: not `IdBlob`'s layout (80 B, align 4), its constants, `load_id`, `save_id` or
any read or validation primitive.

**The firmware snapshot adapter** — declared in `firmware_config.h`, defined in `firmware_config.cpp` — gathers the
three live authorities of §1 and calls the helper:
- the seed from `g_identity.seed`;
- the name from `g_node.effective_name(out, 32)`, counted — never through `strlen` or a reserved terminator;
- the position from `g_lat_e7` / `g_lon_e7`.

Each writer replaces exactly one field — the requested name, one coordinate, or `regen`'s new seed — and every
other field comes from live values. **No candidate field ever comes from failed or rejected NV bytes** (pre-check
§3). That is the invariant, not "everything else becomes zero".

### 2.2 The rename service and `cfg set name` (design §4.3; B448)

**The service** is declared in `firmware_config.h` and implemented in `firmware_config.cpp`. It is stateless — no
retained object or allocation — and returns a typed result: `saved`, `unchanged`, `too_long`, `bad_args` and
`nv_save_failed`, or names as explicit. It follows design §4.3's transaction:
1. Admit the requested counted name through the §2.1 helper. A name over 32 bytes is `too_long`, with zero writes and
   no live change.
2. Build the candidate from live values, with the requested name.
3. Compare it with the durable record **only when `load_id` succeeds**, and only to coalesce an exactly equal
   candidate over its complete bytes. An equal one is `unchanged`: zero writes, **and then the requested name is
   made live** through `Node::set_name` if the live name differs — the durable record already holds it (W0R-1).
   - Live-name equality alone is never enough: an absent, invalid, partial or different record is repaired, even
     when the requested name is unchanged.
   - Rejected record bytes are never resurrected.
4. Otherwise, exactly one `save_id`. Only after it succeeds is the requested name published through
   `Node::set_name`.
5. A failed save is `nv_save_failed`: nothing is published, and the live identity is unchanged.

Both success paths — already durable (`unchanged`) and newly saved (`saved`) — end with the requested counted name
live. `unchanged` describes the write decision, not the previous live name.

**The console arm** — the service's first caller:

| Case | Console output | Effect |
| --- | --- | --- |
| empty value | `> cfg err bad_args` | as today, before anything else |
| `saved` or `unchanged` | `> cfg ok name (saved to /mrid)` | today's success bytes; `unchanged` costs no write; both leave the requested name live |
| over 32 bytes | `> cfg err too_long` | zero writes; the seed, name, both position mirrors, membership and every unrelated NV slot unchanged |
| a failed save | `> cfg err nv_save_failed` | live identity unchanged |

**The grammar is unchanged.** The arm takes everything after the key's separating space as the name, to the end of
the line. Spaces and literal quotation marks are name bytes, and an all-space nonempty name is accepted, as today.
W0 adds no repertoire or Unicode validation — only the length refusal.

### 2.3 `cfg set lat` / `cfg set lon` (B440, B482)

- Build the candidate from live values with the one coordinate replaced, and make exactly one `save_id`. There is no
  coalescing here, as today.
- **Only after the save succeeds**, publish both mirrors of that coordinate — the global and `NodeConfig` — and print
  `> cfg ok (saved to /mrid)`.
- On a failed save, print `> cfg err nv_save_failed`, and leave the live position — both mirrors — unchanged.
- Parsing the value is unchanged.

### 2.4 Printing and diagnostics

- Every success and error line keeps its current bytes, except the new `> cfg err too_long`.
- Tests and probe diagnostics compare byte arrays and counts, and print bytes in hex — never raw high or malformed
  UTF-8 bytes. B478 is fixed separately, before W7.

### 2.5 `regen` (B440)

`do_regen` keeps, in its current order:
- the CLIENT remote-debt admission and its refusal;
- the entropy draw;
- **the one checked `save_id` inside `do_regen` itself** — console-sink's S37/S48 read it there;
- the post-save installs;
- the printed identity and the CLIENT warning.

Only the candidate changes: it comes from the live snapshot with the new seed. The name and position are carried
from live values, never from an unchecked `load_id`. Raw `save_id` stays unconditional, because `regen` must always
write its fresh seed.

### 2.6 What stays as it is

- The boot load and re-mint.
- `load_id` / `save_id` and the shared read and validation paths — B486 is separate.
- The BLE `cfg set` and `ready` adapters in `fw_main.cpp` — B483 and B485: a BLE caller still receives the `cfg`
  object and does not see a refusal.
- The leaf-label setters (B484), `create`, the join profiles and the peer-name setters.
- `Node::set_name` and the core's capacity.
- The command grammar, authority and transport classification.
- The administration stores and the remote debt.

### 2.7 Real-path proofs

**Native** (`test/test_device_nv.cpp`, through the real helper):
- names of 0, 31, 32, 33 and 256 bytes, the last proving admission happens before any narrowing;
- every field copied, unused name bytes zero, and the magic and version stamped;
- arbitrary high bytes, compared as bytes.

**The identity arm** — a new, explicitly named arm of the inbox-verbs probe:
- **Scope:** it compiles the **real `firmware_config.cpp` and `firmware_commands.cpp`, exactly as shipped** — never a
  probe-edited copy — with the core, console and existing platform/NV fakes, and executes `cfg set name|lat|lon` and `regen`. It uses a USB-shaped routed
  path, plus a supplied `LineSink` for `regen`.
- **Build:** `identity_main.cpp` is its driver and `identity_platform.h` its local platform shim — the LoRa defaults,
  the `default_output_dbm` stand-in, the `__FlashStringHelper` alias, and the missing `g_persist_team_local_id`
  definition (pre-check §7).
- **Stub exclusion:** `probe_main.cpp` gains only a narrow exclusion of its config-handler stubs, and of the legacy
  stand-in below, for this arm. The existing stub-based dispatch-ownership cases stay exactly as they are.
- **Real code only:** the arm links the real adapter and service from `firmware_config.cpp`, and its controls
  perturb the real production adapter and callers — never the stand-in.
- **Roles:** it runs under both the ACCEPT and CLIENT defines; CLIENT's `regen` debt refusal is preserved.
- **Transport:** it is never described as the BLE adapter — B483's branch stays outside it.

**The legacy binding (W0R-2).** Once `do_regen` calls the snapshot adapter defined in `firmware_config.cpp`, every
existing build of the command TU that does not link the config TU needs a provider for that symbol:
- inbox-verbs' ACCEPT, CLIENT and OLED arms (`run.sh::build_variant`);
- the transcript driver — `transcript_main.cpp` includes `probe_main.cpp`. Its build is broken at the base
  (B487/B488) and W0 does not run it; once B478's package repairs it, the stand-in keeps it linkable;
- deferred-actions — its `probe.cpp` includes `probe_main.cpp`.

`probe_main.cpp` therefore gains **one labelled, signature-correct adapter stand-in, for those legacy arms only**:
- it builds the candidate from the fixture's live fields through the real `id_blob_from_live`;
- it reads no NV, installs no identity or crypto, and adds no production hook;
- the identity arm excludes it, together with the eight config-handler stubs, and never satisfies a proof through
  it;
- the legacy handler-recording behaviour, `transcript.py`, `transcript_main.cpp`, and the deferred-actions builder
  and source stay unchanged.

**The proof matrix** (pre-check §9):
- **Storage × writer:** healthy, absent, unreadable, short and bad-header storage × name, lat, lon and `regen`. After
  each, the saved record carries the live seed, name and position with only the intended field replaced, and the
  live state matches.
- **Live versus durable:** a durable record that differs from the live state is repaired from live values.
- **Names:** exact full 32-byte and high-byte names; save success, failure, byte-identical no-op (zero writes, by the
  NV fake's counter) and repair.
- **Already durable, not yet live (W0R-1):** live `Live Name`, a durable record holding `Saved Name` with the same
  seed and position, requested `Saved Name` — the result is `unchanged`, with zero writes, and `Saved Name` is live
  afterwards. The existing repair cases, with the live name equal to the request but the record missing, bad or
  different, stay.
- **B448 through the real router:** empty; spaces and literal quotes; 31, 32 and 33 ASCII bytes; a multi-byte
  character within 32; a 2- or 3-byte character crossing byte 32; 32 high bytes; more than 255 bytes.
- **Refused input:** zero writes and unchanged live state, as §2.2 states.
- **B482:** a failed coordinate save leaves both mirrors unchanged and prints the error.
- **`regen`:** one checked write and the post-save installs; the CLIENT busy guard and the administration stores
  preserved.
- **Boot:** the re-mint policy is untouched.

Controls, each failing its intended assertion:
- a writer that takes a field from failed NV bytes;
- the missing overlength early return;
- publication before the save — name and coordinate;
- a no-op that writes;
- a coalescing that skips the repair of a bad record;
- an equal-record branch that returns before making the requested name live.

A control counts only when it fails its intended assertion; a crash, a build failure or a vacuous control is
rejected, never counted RED.

**Fixtures that must follow the live precondition:**
- `regen`'s R12, R21, R22, R25 and R26 seed only NV. Correct them to install the matching live name and position,
  while keeping every expected output and persisted field. **Do not** make `seed_id` install crypto globally —
  R1–R17 deliberately observe crypto becoming ready.
- The transcript driver's own live-name precondition goes with B487/B488 to B478's tool package; W0 leaves that
  file unchanged.

**The builder contract stays.** `tools/probe_deferred_actions/run.py` reuses the inbox runner's prefix up to its one
`rc=0` / `if ! build_support;` boundary, and `probe.cpp` includes the inbox `probe_main.cpp`:
- keep that boundary exactly once, and keep `build_support` and `build_variant` callable as today;
- keep the identity arm out of the deferred-actions build;
- a deferred-actions source change is STOP-1.

### 2.8 The closed disposition ledger

The coder writes the ledger into the evidence, closed against the full diff, and classifies each changed assertion
and control on its own. Anything absent from the ledger is a STOP. Known entries:
- the regen fixtures' live preconditions (§2.7);
- the new identity arm's checks and controls;
- the legacy adapter stand-in (W0R-2), named, with the link reconciliation of the legacy inbox arms and
  deferred-actions;
- the new native cases;
- the `devicenv` additions.

**Mutations:**
- **Selector (a)** — the batteries whose source W0 changes: `devicenv` (46 at the base).
- **New `devicenv` entries** for the helper: a dropped seed, name, latitude, longitude or stamp; accepted overlength;
  narrowing before admission.
- **Selector (b):** `config` 32, `w1cname` 4, `consoleline` 12 and `radmin4verbs` 30 — **78**. With (a), the union is
  **124** entries before W0's additions (W0R-3).
- If W0 touches `firmware_config_parse.h`, add `cfgparse` 8 and `sliceDtoken` 2 to (a). It is not in the fence, so
  that would be a STOP-1 first.

**Readers that constrain the edits** (pre-check §7) — each keeps its predicate:
- **board-UI:** W12–W18 and their siblings read `/mrcfg` notification placement in the config functions. Keep the
  statements and function boundaries they own; the default run keeps its 592 identities, with no manifest delta.
- **console-sink:** S37/S48 and S-C37/S-C48 (`save_id` inside `do_regen`; no administration-store writes); S67 stays
  as it is.
- **device-radio** S16/S17, **prov-tx**, and the **feature ownership scanner**.
- **inbox-verbs:** C8/C9/C10, C12/C13, and CLIENT C8–C10 — re-anchored only with an explicit property ledger, never
  weakened.

**The command inventory** scans both TUs and their caller hops. It may be regenerated with the stock generator **for
line-anchor movement only**: 197 semantic rows unchanged, with identical authority, transport and disruptive
properties.

### 2.9 Allocation and stack

- **No resident allocation.** No retained object, and no `Node`, layout, NV-version or schema change. Two 80-B
  candidates plus a counted name are bounded stack scratch.
- **Measured and reported:** the compiled stack effect, and all linked RAM and flash movement on the boards,
  attributed. Nothing claims the fix is byte-neutral.
- **Stock ABI** plus the pre-check's `IdBlob` overlay: 80 B, align 4, on all three ABIs, unchanged.

### 2.10 Nothing else moves

- **Corpus:** 36/36 streams byte-identical on every comparison field; s18 at `simulation/BASELINE.md`'s keystone.
- **`gateway`:** measured and explained. It compiles the config and command TUs, so its image may move, and every
  difference is attributed.
- **Probes and readers:** every stock probe of §2.8 keeps its predicates.
- **Earlier packages:** W1c–W6 keep their properties.

## 3. Fence

**IN:**
- `src/device_nv.h` — the pure helper only (§2.1);
- `src/firmware_config.h` — the typed result and the service/adapter declarations;
- `src/firmware_config.cpp` — the three `cfg set` arms, the adapter, the service, and accurate comments where W0
  touches them;
- `src/firmware_commands.cpp` — `regen`'s candidate (§2.5), and accurate comments where W0 touches them;
- `test/test_device_nv.cpp` — the native cases;
- `tools/probe_inbox_verbs/run.sh` and `probe_main.cpp` (the stub exclusion and the legacy adapter stand-in), and the
  new `identity_main.cpp` and `identity_platform.h` — §2.7;
- `tools/probe_ui_model_mutations.py` — the `devicenv` additions and the native PIN;
- `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` — a stock regeneration, line anchors only;
- the report and evidence (§5).

**OUT:**
- `lib/`, the simulator, `platformio.ini`, `variants/`, `src/fw_main.cpp` and every other `src/` file, including
  `firmware_config_parse.h`;
- every other tool or tools test — including the shared fakes, the board-UI, console-sink, deferred-actions,
  device-radio and prov-tx probes, and `gen_command_inventory.py`;
- a new production header or TU, and a new native test file;
- the design, register, tracker, `MEMORY.md` and metal plan (QA lands, §7).

If a repair seems to need anything outside this fence, that is STOP-1: a fence question for the owner.

## 4. Gates

The owner ruling of 2026-09-16 (P6) gives the coder the full chain, and QA the `src`-only independent gate.

**Ordering:** the two runs of each board pair are back-to-back, and nothing runs between or during them. Mutation
batteries run separately from board measurements. No `--no-neg` result substitutes for a gate.

### 4.1 The coder's gate

**Baselines, on the unmodified tree:**
- **Native:** the count (pre-check: 3055/199532/0).
- **Probes:** inbox-verbs, all three arms; console-sink; board-UI (592); deferred-actions; device-radio; prov-tx; the
  ownership scanner.
- **ABI:** stock, plus the `IdBlob` overlay.
- **Boards:** `python3 tools/measure_board.py pair --jobs=1 --output .pio-measure/w0/base-1`, then `base-2`, then the
  stock `compare` per env on the two `manifest.json` files; both must PASS.

**The final chain:**
1. **Hygiene:** `git diff --check`. The ledger is complete against the diff.
2. **Native:** a fresh `pio test -e native`, then **run the binary** `./.pio/build/native/program`. Report the counts
   derived: 0 failed, 0 skipped.
3. **Corpus:**
   - build a fresh stock `lus` into a new directory outside both repositories:
     `cmake -S <simulator> -B <dir> -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=<MeshRoute>`, then
     `cmake --build <dir> --target lus`;
   - run `python3 -B tools/run_corpus.py --out <new dir> --lus <dir>/orchestrator/lus --require-anchors`;
   - expect 36/36, compared field by field with the pre-check manifest.
4. **ABI:** stock, plus the `IdBlob` overlay by the pre-check's method (`identity-abi.json`); report the two
   separately.
5. **Probes:**
   - inbox-verbs default: ACCEPT, CLIENT, OLED and the new identity arm, with every check and control accounted
     for;
   - console-sink, board-UI (exit 0, 592), deferred-actions (source unchanged), device-radio, prov-tx and the
     ownership scanner;
   - report the counts against the baselines, and reconcile the ledger.
6. **Stack:** `-fstack-usage` frames for the name, coordinate and `regen` paths on the `heltec_mobile` toolchain,
   against the base. These are compiler frames, not a task high-water mark.
7. **Tools discovery** (D5): `python3 -m unittest discover -s tools -p 'test_*.py'`. Report the count derived, OK,
   0 skipped.
8. **Checkers:**
   - `python3 tools/gen_command_inventory.py --check`: 197 rows. Regenerate only for line anchors, and report the
     diff;
   - the authority and literals checkers;
   - `tools/warning_census.sh` on its six pinned OLED envs — zero new warnings, `-Wswitch` 0.
9. **Boards, final.** Run `.pio-measure/w0/final-1` and `final-2`, then the stock `compare` per env; both must PASS.
   Read the `base-1` and `final-1` manifests field by field: every `measurements.*` and payload difference is
   attributed, for both `gateway` and `heltec_mobile`.
10. **Mutation union:** selectors (a) and (b) with the §2.8 additions.
    - Every configured entry must be RED, with 0 unusable and 0 vacuous.
    - Every worker's clean baseline equals step 2's counts.
    - A non-RED entry is reported with its capture; QA disposes of it, and it is never silently accepted.
11. **The pin line:** `PIN re-synced? YES — <derivation>`.

**Reader audit (D6/P7), before the freeze.** Grep `lib/`, `src/`, `test/` and `tools/` for every reader of the changed
statements and symbols, including the directory scanners. Pre-check `readers.txt` and `symbol-readers.json` are the
starting point. Rerun any check whose predicate the edits touch.

**Input stability:** inputs are recorded before the baselines and after the last step. They differ only by the fenced
edits and new evidence; a change in between restarts the chain.

### 4.2 QA's independent gate (P6, `src`-only)

On the frozen tree, QA re-runs:
- **Native** — a fresh `pio test -e native`, then the binary;
- **Corpus** — 36/36 byte-identical;
- **Boards** — its own `pair --jobs=1` into a fresh `.pio-measure/` directory, read field by field against the coder's
  `final-1`;
- **Selector (a)** — `devicenv` with W0's additions;
- **The affected probes:** inbox-verbs — its identity arm and its legacy arms — console-sink, board-UI,
  deferred-actions, device-radio, prov-tx and the ownership scanner; the stock ABI plus the overlay.

**The transcript comparator is not run, by either side** (owner ruling 2026-09-30). It fails at the base (B487), and
its USB wrapper admits only seven bytes (B488); nothing claims it passed or that its coverage is unchanged. W0's
coverage comes instead from:
- the identity arm — the real `firmware_config.cpp` and `firmware_commands.cpp`, with `cfg set name|lat|lon` and
  `regen` outputs checked byte for byte through the real router, and `regen` through a supplied `LineSink`;
- the legacy inbox arms — the R-series `regen` outputs, byte for byte, on USB and on the supplied sink.

The union's selector (b), full discovery and the warning census stay in the coder's chain.

### 4.3 STOP conditions

- **Record:** a candidate field taken from failed or rejected NV bytes; an `IdBlob` layout, constant or version
  change; a changed read, validation or boot path.
- **Refusal:** a name over 32 bytes that writes, publishes or narrows before admission; a changed success or error
  line other than `> cfg err too_long`.
- **Ordering:** a live name or coordinate published before the requested record is durable; anything published
  after a failed save; an `unchanged` success that leaves the previous live name.
- **Coalescing:** a no-op that writes, or a repair skipped because the live name matched.
- **`regen`:** its save moved out of `do_regen`; a changed admission, entropy or install order; a lost CLIENT
  refusal.
- **Fence:** an edit outside §3; a `fw_main.cpp` change; a moved board-UI anchor or a manifest delta; a changed
  inbox builder contract; a `transcript.py` or `transcript_main.cpp` change; any deferred-actions source change;
  `seed_id` made to install crypto globally; a proof satisfied through the legacy stand-in; a claim that the
  transcript ran or passed.
- **Ledger:** any changed, retired or added expectation, control or mutation absent from the §2.8 ledger, or any
  weakened.
- **Inventory:** a semantic inventory change.
- **Census:** a warning-census failure.
- **Boards:** a failed repeatability `compare`, or an unattributed delta.
- **Corpus:** any stream delta.
- **Allocation:** a retained object or resident allocation.
- **Inputs:** a mismatched input at preflight, a changed preparation file, or an input change during the chain.
- **Source facts:** a §1 fact found false — STOP-1 to QA, never a coder assumption.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0.md`, with an author line on line 1.

**Evidence directory:** `docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0/`. It holds:
- **Board manifests:** the eight per-env manifests, copied from `.pio-measure/w0/` **after the last measurement run**;
- **Board comparisons:** the four `compare` outputs and the attribution table;
- **The ledger:** the §2.8 disposition ledger;
- **Measurements:** the ABI overlay and the stack frames;
- **Durable receipts:** compact native, probe, discovery, census, inventory and union summaries; the full corpus
  manifest and field comparison; the locations and hashes of the raw logs;
- **The reader-audit greps;**
- **A `SHA256SUMS`** covering everything.

**Retention.** Follow [the evidence-retention policy](../evidence/README.md):
- raw logs go under ignored `artifacts/2026-09-29-standalone-mobile-home-w0/`;
- raw board outputs stay under `.pio-measure/w0/`, and the stock tool's `.pio-measure/env/` roots are not relocated;
- native builds use `.pio/`;
- simulator and mutation scratch builds stay outside both repositories.

**The report contains:**
1. **Preflight** and the inventory check.
2. **Baselines.**
3. **Diff:** `git diff --stat` and the ledger.
4. **Figures:** every §4.1 figure, derived by the coder.
5. **Allocation:** the stack frames and the attributed board deltas.
6. **Controls and mutations:** the reconciliation, and the union by battery.
7. **The pin line:** the exact line `PIN re-synced? YES — <derivation>`.
8. **Freeze inventory:**
   - the final SHA-256 of every fenced file;
   - the authorized brief hash;
   - the simulator commit and status;
   - the preparation-set hashes, unchanged;
   - the inventory check;
   - the new evidence paths with hashes;
   - the stability statement.
9. **Not run,** with reasons. Each reason is that the instrument's predicates are unaffected — confirmed by the reader
   audit — never "it reads none of the files".

## 6. Owner rulings

- **B482** folds into W0: the coordinate setters publish only after a successful save (2026-09-29).
- **B478** is its own tool package after W0 and before W7 (2026-09-29). W0's diagnostics avoid raw high bytes.
- **The transcript comparator** leaves W0's gates, and B487/B488 join B478's package (2026-09-30).
- **Nothing else is requested.** The service and the conversion are design §4.3's reviewed contract. B483–B486 stay
  separate.
- **A contradictory interpretation** returns to the owner.

## 7. Landing (QA, on PASS)

- **Register:** **B440, B448 and B482 CLOSED** in place; B478 and B483–B486 stay OPEN; the §0 dispatch is rewritten in
  place.
- **Design:** the §13 W0 status.
- **Pointers:** `tracker.md` and `MEMORY.md`.
- **Metal plan (M2):** QA decides whether a console check belongs there. For example: `cfg set name` of 33 bytes on a
  real board answers `> cfg err too_long` and changes nothing, and a normal rename survives a reboot. The physical
  flash-failure residue stays with the existing NV procedures.
- **Next:** the tool package — B478, B487 and B488 — then W7; or W5, W9, W4c or W1b, in the owner's order.

## 8. Revision history

**Revision 1 (2026-09-29)** is the first draft. It is built on the W0 pre-check and design r2.24.
- It adopts the pre-check's one-package recommendation: the pure conversion, the stateless rename service and the
  real-config identity arm.
- It folds in the owner's B482 ruling.
- It keeps B478 and B483–B486 out.
- It defines the closed disposition ledger and the proof matrix.

**Revision 2 (2026-09-29)** folds in the brief review (HOLD, W0R-1–W0R-3):
- **W0R-1:** an `unchanged` rename — the identical record already durable — costs no write but still makes the
  requested name live. Both success paths end with the requested name live, and a failed save publishes nothing.
  Added: the divergent-name case, its control, and the STOP wording. Design r2.24's §4.3 item 2 is corrected to
  match.
- **W0R-2:** the legacy command-TU builds get one labelled adapter stand-in in `probe_main.cpp`, built on the real
  pure conversion. The identity arm excludes it and exercises only the real adapter and service. The transcript
  profiles join the gates.
- **W0R-3:** selector (b) is 78 entries; 124 is the union with (a).
- **Also, from the review's notes:** B448's boundary and over-255 cases through the real router, and control
  classification that rejects crashes, build failures and vacuous controls.
- **Pins:** the design and the register.

Nothing else changes.

**Revision 3 (2026-09-30)** settles W0R-4 from the scoped re-review (W0R-1–W0R-3 closed). The transcript
comparator that revision 2 added to the gates is broken at the base (B487, B488), and the owner ruled it out of W0's
gates, with B487/B488 joining B478's tool package:
- `transcript_main.cpp` leaves the fence and is pinned read-only;
- the transcript's live-name precondition moves with it;
- §4.2 names the proofs that cover W0 instead, and states that the transcript is not run or claimed;
- the STOP list forbids any transcript file change, or a claim that it passed;
- the pins cover the design, the register (B487/B488 and the rulings), `tracker.md` and `MEMORY.md`.

Nothing else changes.
