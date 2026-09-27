<!-- Author: Claude (brief author); QA: Codex (pre-check, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# Standalone Home W1c — D10: unnamed devices advertise no name (`lib/core`)

**Revision 2 — 2026-09-25 — DRAFT header retained; authorized by the separate Quality-Agent review receipt.**

- **Base:** commit **`8360802`** (`8360802904f7bd0023279d3da844453d61207ede`, owner commit "W1"; parent `4a230f4`).
  The simulator is at **`6585649`** (`6585649ea5a780f0542b2931853a667be56a5b2b`), clean.
- **Inputs:** the executable inputs are committed at the base (§1). The preparation set is uncommitted and pinned
  in the §1 inventory; commits are optional, preservation is not.
- **Freeze:** the coder pins this brief by the hash QA issues when it passes review. Under P4 a brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** `/home/staszek/MeshRoute` on `main`, never a worktree.
- **Authorities:**
  - the [W1c QA pre-check](../evidence/2026-09-25-standalone-mobile-home-w1c-precheck.md) — the source-fact ledger,
    the recommended shape and the baselines;
  - the [design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) r2.18, §4.6 (D10, ruled by
    the owner 2026-09-23) and §13 row W1c;
  - [register](../../2026-07-30-open-bug-register.md) B447 (its unnamed half) and B450 (context only; stays open);
  - the process rules: `AGENTS.md` / `CLAUDE.md` (P4–P7, D1–D6, C1–C4) and [agent roles](../../2026-09-02-agent-roles.md),
    including the owner ruling of 2026-09-16: a `lib/core` change takes the full gate on **both** sides.

## 0. What this is

**The rule (D10).** An unnamed device advertises no name. Today `Node::effective_name` makes up
`MeshRoute node: 0x<HASH8>` (26 bytes) whenever the stored name is empty. That string then rides the first-contact
INTRO, the authoritative key answer and the key request (WANT_PUBKEY H). Peers cache it as the device's name, and
`whoami` prints it.

**The change.** `Node::effective_name` becomes a plain counted copy of the stored name, and the default is deleted.
Nothing downstream changes, for three reasons:
- the three producers already emit the codec's existing nameless forms for a zero count;
- every receiver already accepts an empty name;
- `Node::peer_key_set` already ignores an empty name, so it never erases a cached one.

**What moves on air, for an unnamed device only.** A named device's bytes do not change.
- INTRO prefix: 59 → **33** bytes.
- Key-answer body (`0x8B`): 61 → **35** bytes.
- WANT_PUBKEY H: 67 → **40** bytes; team-scoped 71 → **44** bytes. The optional trailer is omitted, length byte
  included.
- A home's `MOBILE_KEY_FORWARD` body (`0x96`) for such a requester: 59 → **33** bytes.

**What does not move.** No wire, codec, `wire_version`, NV, `Node` layout, receiver or registration change. Every
simulated node is named (783/783), so the corpus is predicted **36/36 byte-identical**. The slice is `lib/core`, so
it takes the full gate on both sides.

## 1. Verified seams and pinned inputs at `8360802` (pin by symbol; lines are hints)

| Seam | Fact at the base | Change |
| --- | --- | --- |
| `Node::effective_name` — `lib/core/node.cpp` (comment `:~39`, body `:~41–53`) | `cap == 0` returns 0. A stored name copies `min(_name_len, cap)` bytes and returns the count. An empty name writes the 26-byte `MeshRoute node: 0x` + 8 uppercase hex digits of `_key_hash32`. It never terminates | **§2.1** |
| `lib/core/node.h` — the `effective_name` declaration comment (`:~576`) and the `_name` member comment (`:~2978`) | Both promise the default. `set_name` clamps to 32 bytes; `name_len()` is public | Comments only (§2.1) |
| The three callers in `lib/core/node_hashlocate.cpp`: `Node::intro_attach_prefix` (`:~767`), `Node::send_hash_bind_pubkey_response` (`:~1262`), `Node::emit_hash_query` (`:~2321`) | INTRO prefix = `ed_pub[32]`, `nlen`, name; `need = 33 + nlen`; it attaches only if `need + body_len <= protocol::dm_max_body_bytes` (232), otherwise the send goes plain with `intro_attach_too_large`. The key answer is the 34-byte base, `nlen`, name. `pack_h` appends `[name_len][name]` only when `want_pubkey && name_len` | **None** — this file is not edited |
| Forwarding and receiving — the forward in `Node::handle_h`; `Node::forward_requester_key_to_mobile` (per-row dedup `last_key_fwd_hash32`); INTRO in `Node::do_post_ack` (`lib/core/node_mac_rx.cpp` `:~2732–2751`, a null name when `nlen` is 0); `Node::on_hash_bind_pubkey`; `parse_h`; the owner branch of `Node::handle_h`; `Node::cache_want_pubkey_requester`; `Node::on_mobile_key_forward` | All accept a zero-length name (pre-check §3) | **None** |
| `Node::peer_key_set` (`node_hashlocate.cpp` `:~335–377`) | The refresh branch renames only under `if (name && name_len)` (`:~346`). A fresh insert zeroes `name_len`. A pinned row refuses on-air sets first. `Node::peer_name_set` given a count of 0 **would erase** the name (it stores `name_len = 0`); the guard is what prevents that. The bare guard statement appears **twice** (`:~346` refresh, `:~377` insert) | **None**; the new dependency control attacks the refresh guard (§2.5) |
| Mobile registration: `Node::send_mobile_pubkey_answer` (`0x95`) | Carries a hosted-row name that no production code writes; the P-probe has no name field (pre-check §4). Its stale comments are B450 | Out of scope |
| `handle_whoami` — `src/firmware_commands.cpp` (`:~1396–1406`, name line `:~1399`) | Prints ` name="`, then exactly the returned byte count, then `"`. A zero count already yields `name=""`. The trailing comment claims the default | Comment only (§2.2) |
| The boot load in `src/fw_main.cpp` (`:~927`), `g_node.set_name(idb.name, idb.name_len)` | The trailing comment claims the default | Comment only (§2.2) |
| The BLE GAP name in `src/fw_main.cpp` (`:~1123–1129`): `MeshRoute-<id>` when unnamed | The companion-discovery label, not a mesh name | **None** (outside D10) |
| The `peername` rationale in `lib/console/console_parse.cpp` (`:~195–198`) | Its motivating case is "a peer advertises the default `MeshRoute node: 0x…`" | Comment only (§2.2) |
| `tools/gen_command_inventory.py`; the tracked inventory `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` | `SCAN_FILES` includes all three comment-edited files. The inventory records `file:line` per row, and **48 / 7 / 1** rows sit below the edited lines in `firmware_commands.cpp` / `console_parse.cpp` / `fw_main.cpp`. `tools/test_gen_command_inventory.py`'s `TestRealTree.test_tracked_table_equals_fresh_generation` compares the tracked table with a fresh generation (B433) | **None**; kept valid by preserving line counts (§2.2) |
| Native tests | `test/test_node_r3.cpp` `§1.3 — effective_name defaults to 'MeshRoute node: 0x<hash>'…` (`:~9060`) pins the default. `test/test_node_hashlocate.cpp` `§S2 INTRO wire golden…` (`:~2176`) derives its expected length from the emitted length byte; `§S3 part2 — the HOME proxy-answer…` (`:~2028`) builds its H with `make_h` (no name). `test/test_dual_layer.cpp` holds the only INTRO receive driver, `DualLayerTestAccess::drive_post_ack_intro` (`:~226`; case `§S2 receive…` `:~7931`). `test/test_frame_codec.cpp` `§name — a WANT_PUBKEY H…` (`:~989`) already pins the 40-byte nameless codec form. The two old-default fixtures (`test_node_hashlocate.cpp:~1781`, `test_device_nv.cpp:~299`) are cached data. Both hash-locate TUs build with `-fno-exceptions` (`CHECK` only) | §2.3 |
| `tools/probe_inbox_verbs` | `probe_main.cpp`: `g_node` is built with the name `"node"` (`:~204`). X1/X2 (`:~808–815`) route `whoami` through TEXT and JSON/`LineSink` and never check the name. The X block provisions from `:~767`. `run.sh`: bare pins `PIN_CHECKS_ACCEPT=1394`, `PIN_CHECKS_CLIENT=477`, `PIN_CONTROLS_ACCEPT=61`, `PIN_CONTROLS_CLIENT=69`, each with derivation comments above. `ctl(label, which, script)`'s `router` kind sed-mutates `$FW_CMDS` (`src/firmware_commands.cpp`) and refuses only a mutation that changes nothing at all (`cmp`). `s6_ctl` shows the exactly-one-source-match guard. A default run executes both arms (ACCEPT, CLIENT) | §2.4, §2.5 |
| `tools/probe_ui_model_mutations.py` | 105 `TARGET_SRC` mappings; registry `MUTS_BY_TARGET`. Each entry must match its target **exactly once** (else VACUOUS), compile (else UNUSABLE) and turn the native suite RED (else FAIL); it runs in scratch trees. `PIN_CASES, PIN_ASSERTS = 2951, 195777` is an advisory B217 cross-check, with one derivation comment line per change | Two new batteries plus the PIN re-sync (§2.5) |
| `tools/lab/parsers.py::parse_whoami` | `name="([^"]*)"`: empty → `""`, absent → `None`, identity fields intact (the pre-check executed three cases) | **None**; compatibility re-checked (§4.1) |
| `tools/measure_board.py`, `tools/warning_census.sh`, `tools/probe_board_abi.py` | As in W1. `validate_output_dir` accepts in-repo output only below the gitignored `.pio-measure/` (not `.pio-measure/env/`); output outside the repo then fails in `tools/git_rev.py` (B281/B315). `compare` takes two per-env `manifest.json` **files** and checks same-source repeatability. ABI pins: Node **235208 / 122176 / 157304** (native / heltec_mobile / gateway), align 8 | Used, not edited |

**Executable inputs.** These are the fenced files, with the SHA-256 of `git show 8360802:<path>`. Each equals the
working tree at authoring time.

| File | SHA-256 | Lines |
| --- | --- | ---: |
| `lib/core/node.cpp` | `f33b971c89cfabd239022327da07d8549ba05fe98e16547a76d2df7bbf7b5317` | 2635 |
| `lib/core/node.h` | `3b9971fbd4f82a3f0d07c860488d5e63cf8f727fb3111f56c2dd585df4f20fad` | 4088 |
| `src/firmware_commands.cpp` | `761f3d43129e7c5b6d0e31b156abd963039aef85ebd7d0935382ab0705a2db15` | 1915 |
| `src/fw_main.cpp` | `9472d8ab71e92e51423c8695738f33e79cae822b5d383c75312121c744a71cbc` | 1875 |
| `lib/console/console_parse.cpp` | `3134d5406a69daf1f95439c10ff82be96e3c681ecb58cbf23716dcfeba593502` | 376 |
| `test/test_node_r3.cpp` | `a6ee531c11c056d579ef566dd4746f7f00dbbe529e37329d9484ba892ae33454` | 11674 |
| `test/test_node_hashlocate.cpp` | `198c0211715a82cf3e5d676d4e3f84ed8bf2f077c1eadc2041d75de34e5462ba` | 4829 |
| `test/test_dual_layer.cpp` | `260890cbef5d1b2d8e5b76597ebaaae403bd5729129d98e11afb3ed574c58d77` | 9193 |
| `tools/probe_inbox_verbs/probe_main.cpp` | `ae776415d524c01635fe7bc5c84bf7837967fd296710c26eefcdb46eb93e4a2e` | 2061 |
| `tools/probe_inbox_verbs/run.sh` | `2d1411e94bf39770886dbb65a28de7dcbb9ef35bd2e65f4e4775c45a94e388f8` | 855 |
| `tools/probe_ui_model_mutations.py` | `f135d051b626909ce2dd6099af8db5a98c79565d16e7b8c7d9c5a782bfd99afa` | 12379 |

**Permitted preparation set.** These uncommitted documents are preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | authority (r2.18; W1c status note) | `0b1af9089aef32ee9cc4af00c123553af0fd5f1716a1554f4f26e8584b14d8c9` |
| `docs/2026-07-30-open-bug-register.md` | authority (B447, B450, §0) | `01a29418a20ea098cd91a50bc648566ccc9a3ac5de8d3cc87351d6daddea2f9e` |
| `docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w1c-precheck.md` | authority (pre-check) | `f7d81ec0b8f3bb2ff0b9a0c13f64ab645ec43f9ee0d2e5d47696b87e8769bd2a` |
| `docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w1c-precheck/SHA256SUMS` | evidence — covers the directory's other 18 files, including `corpus-manifest.json` (`e669fb29…b5d603d`), the corpus comparison authority | `fcfa56b5422b0bb9cbf9f4417cdd2561bf8cdb11897da094cb535b638315b24d` |
| `tracker.md`, `MEMORY.md` | context pointers | `bcb46358607fa9dd4fe60a7c00626b44fb5acc0350399fdd04430958b73ed2c9`, `881b47db91b50ef31ddca7113dff4d4954e3519b3a4adfefeb2e3aab2be60e8b` |

This brief is pinned by the hash QA issues on PASS, not listed here.

**Preflight:**
1. Verify both repositories: MeshRoute `HEAD` = `8360802`; the simulator at `6585649` and clean.
2. Every executable input is clean against `HEAD` (`git diff --quiet HEAD -- <path>`).
3. Every preparation-set hash matches, and the pre-check directory verifies (`sha256sum -c SHA256SUMS`, 18 OK).
4. Classify every other entry in `git status --porcelain`. QA review reports and evidence added after this brief
   are explained preparation additions; anything else is reported.

Any of the following is STOP-1 to QA: a different `HEAD`, a dirty executable input, a changed preparation-set file,
or an unexplained change. The whole preparation set stays unchanged from preflight through the freeze.

## 2. Contract

### 2.1 Production — one function

`Node::effective_name(char* out, uint8_t cap) const`:
- **Copy.** It copies `n = min(_name_len, cap)` stored bytes to `out[0..n)` and returns `n`.
- **Empty.** When `n == 0` (an unnamed node, or `cap == 0`) it **writes nothing**.
- **No terminator, no reserved byte.** A 32-byte name fills a 32-byte destination.
- **No default.** No default synthesis; `_key_hash32` is no longer read here.

**Unchanged:** the signature and name (a rename would pull all four callers into the fence — U1/C1/P7), `set_name`,
`name_len()`, the constructor, and `_name`/`_name_len` with their order. No new accessor, pointer, formatter or
test hook; no caller edit.

**Comments (V1).** The definition comment and the two `node.h` comments state the D10 contract in a few words:
- the stored name only, which may be empty;
- an unnamed node advertises no name (D10);
- a counted copy that never terminates, because its callers write counted wire fields.

### 2.2 Comment-only edits outside `lib/core`

Three comments are rewritten:
- **`handle_whoami`'s trailing comment:** it prints the stored name exactly, `name=""` when unnamed (D10).
- **The boot `set_name` trailing comment in `fw_main.cpp`:** an empty `/mrid` name leaves the node unnamed, and it
  advertises no name (D10).
- **The `peername` rationale in `console_parse.cpp`:** the motivating case becomes a peer that advertises no name
  (it is shown by its ID) or a name the operator wants to replace. Grammar, refusals and code are unchanged.

**Line counts are preserved in these three files.** Each edit rewrites text on its existing lines, adds and removes
no line, and leaves every other line byte-identical. The reason is that the tracked command inventory records
`file:line` for rows below every edit. Consequently `python3 tools/gen_command_inventory.py --check` must pass
against the **unchanged** tracked inventory. A needed `--write` is STOP-1, not a fix.

**Audit before editing (D6/P7).** Grep `lib/`, `src/`, `test/` and `tools/` for every exact-source reader of each
line the slice edits: comment lines and the `effective_name` body. The pre-check found none keyed on them, and no
existing mutation anchor overlaps them (the author's census of every battery targeting `node.cpp` and `node.h`).
Record the grep.

### 2.3 Native tests — real producers, fresh caches, existing seams only

**The rules for every case:**
- **Seams.** Use existing public seams only, including the setup and inspection APIs (`on_init`, `set_name`,
  `set_crypto_identity`, `test_id_bind_set`, `peer_key_find`) and `on_command`, `on_recv`, `test_suspend_tx_drain`, `test_tx_*`,
  `test_add_host_mobile`, `on_hash_bind_pubkey`, `on_mobile_key_forward`, `peer_key_set`, `peer_name_set`,
  `peer_name_find`, `next_push`. In `test_dual_layer.cpp` also use `DualLayerTestAccess::drive_post_ack_intro`.
- **Fresh caches.** A freshly constructed `Node` has an empty peer cache. A board reboot is never used as one:
  `/mrpeers` restores names.
- **Paired variants.** Every unnamed/named pair uses **fresh nodes built from the same seed and fixture**, so the two
  outputs differ only by the name.

**(A) The accessor.** Replace the §1.3 case in `test/test_node_r3.cpp` and rename it to what it now proves. Every
call uses a destination of at least 40 bytes, pre-filled with a non-zero poison byte, whatever `cap` it passes. That
way an over-copying mutant writes in bounds and fails cleanly. Expect:
- **Unnamed** (constructor without a name): returns 0, and all 40 bytes are still poison.
- **A named node at `cap` 0:** returns 0, destination untouched.
- **`set_name("Alice", 5)`:** returns 5 with the exact bytes; byte 5 is still poison (no terminator).
- **A 32-byte name at `cap` 32:** 32 exact bytes; bytes 32.. still poison.
- **The same name at `cap` 7:** 7 bytes; bytes 7.. still poison.
- **The constructor name `"Bob"`:** returns 3.
- **`set_name(…, 0)` after a name** (the boot path for an unnamed `/mrid`): returns 0, destination untouched.

**(B) Exact bytes from the real producers.** These go in `test/test_node_hashlocate.cpp`. Each unnamed case has a
named control, and each checks the exact length, key bytes, flags, length byte, trailer and payload:
- **B1 INTRO.**
  - *Setup:* an unnamed, crypto-ready sender makes a first-contact plaintext send-by-hash of `hi!`.
  - *Expect:* the queued `DATA_TYPE_INTRO` unicast body is exactly `ed_pub[32] ‖ 00 ‖ "hi!"` (36 bytes).
  - *Named control:* `ed_pub ‖ len ‖ name ‖ "hi!"`.
  - *Existing golden:* strengthen the `§S2 INTRO wire golden` case in place with an independent `name_len == 0`
    and the exact total. Today it derives its expectation from the emitted byte and is not sufficient.
- **B2 INTRO fit boundary, unnamed.**
  - *Attaches:* a body of `protocol::dm_max_body_bytes − 33` (derived, not typed: 199) attaches INTRO, and the body
    is `dm_max_body_bytes` long.
  - *Falls back:* one byte more sends plain, with exactly one `intro_attach_too_large`.
  - *Existing case:* the at-cap `§S2 too-large fallback` case stays unchanged.
- **B3 key answer (`0x8B`).**
  - *Setup:* an unnamed, crypto-ready owner answers a real WANT_PUBKEY through the owner branch of `handle_h`.
  - *Expect:* the queued `DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY` body is exactly
    `target_layer ‖ node_id ‖ ed_pub[32] ‖ 00` (35 bytes).
  - *Named control:* the same 34-byte base, then `len ‖ name`.
- **B4 WANT_PUBKEY H.**
  - *Non-team:* an unnamed requester's `reqpubkey` transmits exactly **40** bytes, with `H_FLAG_WANT_PUBKEY` and
    `H_FLAG_HARD` set, bytes 8..39 equal to its `ed_pub`, and nothing after.
  - *Team-scoped:* use the existing team fixture shape (`is_mobile`, `team_id`, `set_team_local_id`, TEAM plane, as
    in the `§id-hash S1` cases). The frame is exactly **44** bytes, `H_FLAG_TEAM` is set, `team_id` is LE32 at
    40..43, and nothing follows.
  - *Named controls:* the unnamed frame is byte-identical to the named frame's first 40 (44) bytes, and the named
    frame continues with exactly `len ‖ name`.
- **B5 the home's forward (`0x96`).**
  - *Setup:* a fresh home hosting a live mobile (`test_add_host_mobile`) receives the **unnamed requester's real
    transmitted H** — B4's bytes, not `make_h`.
  - *Expect:* the queued `DATA_TYPE_MOBILE_KEY_FORWARD` has `addr_len` 1 and destination = the local id, and its
    inner is exactly `origin ‖ ed_pub[32] ‖ 00` (34 bytes).
  - *Named control:* on a second fresh home (fresh `last_key_fwd_hash32` dedup), the inner continues with
    `len ‖ name`.

**(C) Receiving and the cache — receivers unchanged.** The INTRO route goes in `test/test_dual_layer.cpp`, beside
`§S2 receive…`; the others go in `test/test_node_hashlocate.cpp`. The inputs are the exact zero-name bytes proven
in (B).

The five routes:
1. INTRO;
2. the key answer (`on_hash_bind_pubkey` with the 35-byte body);
3. WANT_PUBKEY at the owner (`on_recv`, the owner branch);
4. WANT_PUBKEY at a hosting home (`on_recv`, the proxy branch through `cache_want_pubkey_requester`);
5. `MOBILE_KEY_FORWARD` (`on_mobile_key_forward` with the 33-byte body).

What each route must show:
- **C1, fresh cache:** the key is cached authoritative, `peer_name_find` returns 0, and the `peer_key_cached` push
  has `body_len` 0. INTRO also delivers exactly the stripped message.
- **C2, retention:** the receiver already holds that peer's key with a name, in an **unpinned** row. (A pinned row
  returns before the name guard, so the §2.5 dependency control could not bite.) The zero-name arrival leaves the
  name byte-identical, and the push carries it.
  - A **`peername` label** (`peer_name_set`) seeds routes 1–3. These are the three paths B447 names.
  - The **old default string** as cached data (`MeshRoute node: 0x<HASH8>`) seeds routes 4–5.
- **Existing cases** — pinned-peer, named-refresh and the old-default fixtures — stay unchanged and green.

A C-case that fails on the unchanged receivers means a §1 source fact is wrong: STOP-1, and no receiver edit.

### 2.4 `whoami` through the real router — `tools/probe_inbox_verbs`

**Where the rows run.** New rows go on **both build arms** (ACCEPT and CLIENT), through `mrfw::exec_console_line`
on the TEXT transport and the JSON/`LineSink` transport:
- **Unnamed:** make `g_node` unnamed through the public `Node::set_name` with length 0. The output is exactly the
  identity line built from `g_node`'s own accessors, byte for byte, with `name=""`.
- **Named** (`Bench 1`): `name="Bench 1"`.
- **A 32-byte name:** exactly those 32 bytes between the quotes.
- **The rest of the line** (`id`, `hash`, `leaf`, `gw`, `gwonly`, `mobile`) is intact in every row.

**Placement.** Add the rows at the end of the X block, so no existing row moves or changes state. Restore the
fixture's name afterwards. Every existing row keeps its verdict.

**Parser compatibility** (evidence only, no tool edit): each new row prints the line it checked. Feed the unnamed
and named lines from the probe log to `tools/lab/parsers.py::parse_whoami`. The result must be `""` and the name
respectively, with the identity fields intact.

### 2.5 Controls

**Probe controls.** Two new controls in `tools/probe_inbox_verbs/run.sh`, `router` kind, shared by both arms.
**Every substitution a control relies on has its own exactly-one source-match guard** (the `s6_ctl` idiom; the B449
lesson: the stock `cmp` sees only a mutation that changes nothing at all).
- **The default restored at the router:** `handle_whoami` substitutes a nonempty default when the core returns 0.
- **The empty field dropped:** `name=""` is omitted when unnamed — the tempting "omit when empty". The field must
  be present and empty.

Each must compile and go RED on the new **unnamed** rows only, not on an unrelated row. A crash, a vacuous sed or a
failed compile is not a usable RED. Every existing control keeps its meaning.

**Native batteries.** Two new batteries in `tools/probe_ui_model_mutations.py`, each registered in `TARGET_SRC`,
with a `MUTS_<NAME>` list and a `MUTS_BY_TARGET` entry. Entries are written against the implemented text and match
exactly once. No existing battery's entries, patterns or targets change.
- **`w1cname` → `lib/core/node.cpp`** (`Node::effective_name`). At least these four entries:
  - **The default restored for an empty name** (the old synthesis back) → RED on (A) and (B).
  - **A terminator reserved:** the copy is clamped to `cap − 1` and NUL-terminated → RED on (A)'s 32-byte and
    no-terminator checks.
  - **The empty path writes a terminator** (`out[0] = '\0'` when unnamed and `cap ≥ 1`) → RED on (A)'s
    writes-nothing checks.
  - **The capacity ignored:** the stored length is copied whatever `cap` is → RED on (A)'s `cap` 7 check.
- **`w1cretain` → `lib/core/node_hashlocate.cpp`** (`Node::peer_key_set`, not edited — a dependency control).
  Exactly one entry:
  - **An empty advertisement erases the retained name:** the refresh-branch guard `if (name && name_len)` becomes
    `if (name)`. It must go RED on C2 for routes 2–5.
  - *Anchor:* the bare statement matches twice, so the anchor must carry the refresh branch's own trailing
    comment.
  - *INTRO:* route 1's receiver passes a null name for a zero count, so no single-site mutation erases the name
    there. The report says so rather than claiming coverage.

**Reporting.** For every new entry, report the failed-assertion count the harness prints and the cases the entry is
built to redden.

### 2.6 Nothing else moves

- **Corpus:** 36/36, and every stream equals the pre-check's `corpus-manifest.json` field by field (MD5, SHA-256,
  bytes, events, exit and assertion fields). The s18 anchor authority is `simulation/BASELINE.md`.
- **ABI:** Node **235208 / 122176 / 157304**, align 8, on the three ABIs — unchanged.
- **Wire and state:** no change to codecs, flags or `protocol::wire_version` (1); NV (`mrnv::kVersion` 26);
  receivers; registration; or the simulator.
- **Pins:**
  - Re-sync `PIN_CASES, PIN_ASSERTS` in the existing shape: the literal plus one derivation comment line.
  - Re-derive the four inbox-verbs pins from execution. Each keeps a derivation comment above a bare
    `NAME=<integer>` (D5), and the runner's row-family comment table names the new rows.
  - Then run tools discovery.

## 3. Fence

**IN:**
- `lib/core/node.cpp` — the `Node::effective_name` body and its comment only;
- `lib/core/node.h` — the `effective_name` declaration comment and the `_name` member comment only;
- `src/firmware_commands.cpp` — `handle_whoami`'s trailing comment only, line count unchanged;
- `src/fw_main.cpp` — the boot `set_name` trailing comment only, line count unchanged;
- `lib/console/console_parse.cpp` — the `peername` rationale comment only, line count unchanged;
- `test/test_node_r3.cpp` — the §1.3 case replaced (A);
- `test/test_node_hashlocate.cpp` — the (B) and (C) cases, plus the INTRO wire golden strengthened in place;
- `test/test_dual_layer.cpp` — the INTRO (C) cases beside `§S2 receive…`;
- `tools/probe_inbox_verbs/probe_main.cpp` — the §2.4 rows;
- `tools/probe_inbox_verbs/run.sh` — the two controls, the four pins with their derivation lines, and the
  row-family table;
- `tools/probe_ui_model_mutations.py` — the two batteries' registration and lists, plus the PIN literal with one
  derivation line;
- the report and the evidence directory (§5).

**OUT:**
- `lib/core/node_hashlocate.cpp` (B450's comments included), `lib/core/node_mac_rx.cpp`, `frame_codec.*`, every
  other codec, receiver, registration and NV file; `Node` members and layout;
- `test/test_frame_codec.cpp` — the nameless 40-byte form is already pinned, and B4 proves both forms through the
  real producer;
- `test/test_device_nv.cpp` and the old-default fixture near `test_node_hashlocate.cpp:~1781` — old defaults stay
  valid cached data, with no recognition, clearing or migration (M3);
- every other `src/` file, including the BLE GAP name; `platformio.ini`;
- the simulator — read-only, stock builds only;
- `tools/gen_command_inventory.py` and the tracked inventory (a `--write` is STOP-1);
- the unedited tools: `tools/lab/parsers.py`, `tools/measure_board.py`, `tools/warning_census.sh` and its pins,
  `tools/probe_board_abi.py`;
- every other probe or tool;
- every existing battery's entries, patterns and targets, and every existing `ctl` control's meaning;
- the design, register, tracker, `MEMORY.md`, metal plan, companion contract and address-book design (landing, §7).

## 4. Gates

`lib/core` changes, so **both sides run the full chain** (owner ruling 2026-09-16, P6).

**Ordering, throughout:**
- **Board pairs.** The two runs of each board pair are back-to-back. Nothing else runs between or during them: no
  edit, no new nonignored file, no native, probe, census, ABI or mutation run. The source snapshot and the ordinary
  `.pio` metadata are qualification fields.
- **Inventory before discovery.** `gen_command_inventory.py --check` runs before tools discovery (B433).

### 4.1 The coder's gate

**Baselines, captured before the first edit:**
- **Probe:** the unmodified `tools/probe_inbox_verbs/run.sh`, default (both arms). Record checks, controls and
  unusable per arm.
- **Boards:** `python3 tools/measure_board.py pair --jobs=1 --output .pio-measure/w1c/base-1`, then the same into
  `.pio-measure/w1c/base-2`. Then the stock `compare` once per env (`gateway`, `heltec_mobile`) on the two
  `manifest.json` files; both must PASS.
- **Borrowed baselines:** native, corpus and ABI take theirs from the pre-check.

**After the implementation:**
1. **Hygiene:** `git diff --check`. `wc -l` of the three comment-edited files equals the §1 line counts.
2. **Native:** `pio test -e native`, then **run the binary** `./.pio/build/native/program`. Report the counts from
   the binary (base 2951 / 195777 / 0): 0 failed, 0 skipped.
3. **Simulator and corpus:**
   - a fresh stock `lus`: `cmake -S <simulator> -B <new dir outside both repos> -DCMAKE_BUILD_TYPE=Release
     -DMESHROUTE_DIR=<MeshRoute>`, then `cmake --build <dir> --target lus`;
   - `python3 -B tools/run_corpus.py --out <new dir> --lus <dir>/orchestrator/lus --require-anchors`;
   - expect 36/36, compared field by field with the pre-check manifest (§2.6).
4. **ABI:** `python3 -B tools/probe_board_abi.py` (full, controls on) and `python3 -B tools/probe_b278_row_abi.py`.
   Node is unchanged on all three ABIs, and the controls are RED.
5. **Probes,** each in default and `--no-neg` modes:
   - inbox verbs — both arms by default, plus an explicit `MR_PROBE_ARM=client` run;
   - the console sink, firmware UI, custody USB, BLE line, feature matrix and deferred actions.

   New rows green; both new controls RED on their unnamed rows; 0 unusable. Every other probe passes at its existing
   pins.
6. **Parser compatibility:** as in §2.4.
7. **Inventory and literals:**
   - `python3 tools/gen_command_inventory.py --check`, which must PASS with the tracked inventory untouched;
   - `python3 tools/check_command_authority.py` and its `--selftest`;
   - `python3 tools/check_a0_matrix.py` and its `--selftest`;
   - `python3 tools/check_data_type_literals.py` and its `--selftest`;
   - whitespace clean in both repositories.
8. **Tools discovery** (D5), after step 7: `python3 -m unittest discover -s tools -p "test_*.py"`. Report the count
   derived (W1 recorded 356), OK, 0 skipped.
9. **Warning census:** `tools/warning_census.sh` on its six pinned OLED envs — the stated exception to the two-env
   rule. Zero new warnings, `-Wswitch` 0, pins unchanged.
10. **Boards, final.** Run `.pio-measure/w1c/final-1` and `final-2` under the ordering rule, then the stock
    `compare` per env; both must PASS.
    - **Base-to-final attribution.** Read the `base-1` and `final-1` manifests per env. Report every
      `measurements.*` difference and `artifacts.payload.sha256`, each attributed; symbol-level where RAM or a
      symbol moves.
    - **Expected differences.** `source.*` differences are expected. `toolchain.*`, `fixed_identity.*` and `paths.*`
      must be identical.
    - **Prediction** (no allowance): RAM unchanged on both envs. Flash falls on both, since both link `lib/core` and
      lose the synthesis code and its string.
11. **Mutation union,** in scratch trees, never overlapping a board pair.
    - **(a) — batteries whose configured source this slice changes:** `radmin8node` (`node.h`, 1 entry),
      `radmin73node` (5), `teamgrant` (4), `b159map` (2), `sliceBnode` (8) and `sliceEnode` (3) — the pre-check's
      6 / 23 — plus the new `w1cname`.
    - **(b) — dependency batteries:**
      - `b161hash` (6 entries). H05 anchors on the key-answer producer's `body[n] = nlen; n += 1u + nlen;`, which
        consumes `effective_name`'s count; H04 anchors on the `0x95` name tail.
      - The new `w1cretain`.
    - **Run** each with `python3 tools/probe_ui_model_mutations.py --target=<name>`. Expect every entry RED, 0
      unusable, 0 vacuous; every worker's clean baseline equals the step-2 counts.

**Input stability:** the executable-input and preparation-set hashes are recorded before the first gate step and
again after the last. They must match each other and the final inventory (§5); a change in between restarts the
chain.

### 4.2 QA's independent gate (P6 — full, `lib/core`)

QA re-runs **the whole §4.1 chain** on the frozen tree, steps 1–11.

The board pair is QA's own: `pair --jobs=1` into a fresh `.pio-measure/` directory. Its per-env `measurements.*` and
payload hashes must equal the coder's `final-1` manifests. The source snapshot may differ only by the report and
evidence written after the coder's last run. This is a field-by-field reading, not the stock `compare`.

### 4.3 STOP conditions

- **Corpus or ABI:** any stream delta against the pre-check manifest; any `Node` size change.
- **Scope:** any edit outside §3 — in particular any edit to `node_hashlocate.cpp`, `node_mac_rx.cpp` or a codec;
  any wire or NV change.
- **Inventory:** a line-count change in the three comment-edited files, or a failing inventory `--check`.
- **Controls and entries:**
  - a new control or entry that is vacuous, does not compile, crashes or reddens only unrelated checks;
  - an existing control or entry that loses its meaning or goes vacuous.
- **Probes and census:** any probe arm RED; a probe pin moving other than the four derived inbox-verbs pins; a
  warning-census failure.
- **Boards:** a failed repeatability `compare`, an unattributed board delta, or any RAM increase.
- **Inputs:** a fenced file dirty at preflight, a changed preparation-set file, or an input change during the chain.
- **Source facts:** a §1 fact found false — a semantic disagreement (P4), STOP-1 to QA.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w1c.md`, with an author line on line 1.

**Evidence directory:** `docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w1c/`. It holds:
- **Board manifests:** the eight per-env manifests (`base-1`, `base-2`, `final-1`, `final-2` × `gateway`,
  `heltec_mobile`), copied from `.pio-measure/w1c/` **after the last measurement run**;
- **Board comparisons:** the four `compare` outputs and the attribution table;
- **Logs:** native, corpus manifest and field comparison, ABI, every probe mode, the parser check, inventory and
  authority, discovery, census and union logs;
- **The §2.2 reader-audit greps;**
- **A `SHA256SUMS`** covering everything.

The raw measurement directories stay under the gitignored `.pio-measure/w1c/`. The simulator build and corpus
output stay outside the repositories. The report names all three.

The report contains:
1. **Preflight:** both repositories' identity and status, and every §1 hash as found.
2. **Baselines:** the inbox-verbs counts, and the base pair with its per-env `compare` verdicts.
3. **Diff:** `git diff --stat` for the fenced files, and the three line counts.
4. **Figures:** every §4.1 figure, derived by the coder; none copied from this brief or the pre-check.
5. **Controls:** each new control and battery entry, with its verdict and the cases it reddens.
6. **Selectors:** the two mutation selectors and their union.
7. **Pin lines:** the exact line `PIN re-synced? YES — <derivation>` for `PIN_CASES, PIN_ASSERTS`, and the same
   form for the inbox-verbs pins.
8. **Freeze inventory:**
   - the final SHA-256 of every fenced file;
   - the authorized brief hash;
   - the simulator commit and status;
   - the preparation-set hashes, unchanged;
   - the tracked inventory, unchanged;
   - every new evidence path with its hash;
   - the §4.1 stability statement.
9. **Not run,** with the reason for each item. Expected entries:
   - `tools/probe_board_ui` — B418 belongs to W2;
   - `tools/probe_device_radio` — it reads `fw_main.cpp` and `firmware_commands.cpp`, but its radio wiring
     predicates and controls do not target the name comments. The D6 audit must confirm this against the final
     edits; rerun any affected controls if that conclusion changes.
   - `tools/probe_prov_tx` — reads only the unfenced provisioning service and configuration files;
   - the remote-admin codec references — W1c changes no codec, vector or remote-admin test;
   - metal.

## 6. Owner rulings

None requested. The owner ruled D10 on 2026-09-23, and the pre-check recommends this shape with no ruling needed.
Anything beyond it needs its own scope and ruling rather than being absorbed here: a name-bearing registration, a
cache-clear policy, removing storage, or a codec change.

## 7. Landing (QA, on PASS)

- **Register:**
  - **B447:** record its unnamed half as closed, with the evidence link. The row stays OPEN for named-peer
    precedence.
  - **B450:** stays OPEN.
  - **§0:** rewritten in place.
- **Design:** the §13 W1c status note, and §4.6 if a figure differs from the measured one.
- **Pointers:** `tracker.md` and `MEMORY.md`.
- **Durable documents** (pre-check §8):
  - **The address-book design §2.3** (`docs/superpowers/specs/2026-07-29-peer-address-book-design.md` `:~127–128`).
    The motivating case becomes a peer that advertises no name, shown by its ID.
  - **`ios-companion/INBOX_SYNC_CONTRACT.md` `:~535–537`.** There is no generated default: an unnamed node advertises
    no name. Peer names ride INTRO, the WANT_PUBKEY request, the `0x8B` answer and the home's `0x96` forward. The
    `0x95` mobile answer carries the hosted row's name, which production never fills (B450). The retired `0x94` push
    is not described as live.
- **No new metal row** (M2). Every W1c behaviour is reached by the native suite, the inbox-verbs probe and the
  corpus. The bench caveat is already in design §4.6: caches filled before W1c keep old default names until a
  factory reset.
- **Next package:** per the owner's order.

## 8. Revision history

**Revision 1 (2026-09-25)** is the first draft. It is built on the W1c pre-check and adds two facts the author
verified in source:
- **The inventory pins line numbers.** The tracked command inventory records `file:line` below all three
  comment-only edits, so those edits must preserve line counts (§2.2).
- **The INTRO receive driver lives in `test_dual_layer.cpp`.** That file therefore joins the fence (§2.3 C).
