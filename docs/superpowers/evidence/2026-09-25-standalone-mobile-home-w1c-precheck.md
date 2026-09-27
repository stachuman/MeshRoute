<!-- Author: Codex, Quality Agent — source pre-check and baselines for Claude's W1c brief; no implementation authorization -->
# Standalone Home W1c — source pre-check

**Pre-check complete, 2026-09-25. Ready for the Author's brief, with the corrections below.** No production,
test, tool, design, register, tracker or MEMORY edits. This is a source ledger and before-state measurement,
not a W1c implementation gate. D10 is not reopened.

## 1. Base reconciliation and inventory

The requested `4a230f4` plus uncommitted W1 has become owner commit **`8360802904f7bd0023279d3da844453d61207ede`
(`W1`)**, whose immediate parent is `4a230f4501c6a19d71b9234968bf1389484d8295`. The checkout was **clean**.
All five frozen W1 implementation hashes, the four QA-landed document hashes and the QA report hash match
the [W1 QA receipt](2026-09-25-standalone-mobile-home-w1-qa.md) and its landing inventory. The QA evidence
checksum file verifies all 18 entries. This is the same approved candidate committed by the owner, not an
unreviewed implementation substituted for the requested base. **The W1c brief should name `8360802`.**

Simulator: **`6585649ea5a780f0542b2931853a667be56a5b2b`**, clean. No simulator edit is needed or authorized.
The current preparation set is committed, including design **r2.18**, register, tracker, MEMORY, roles and
W1 evidence. [Pinned inputs](2026-09-25-standalone-mobile-home-w1c-precheck/pinned-inputs.json) records
**37** relevant source/document hashes; the full **1242-path MeshRoute / 285-path simulator** inventories,
including symlink targets, are retained in the raw artifact archive. Both inventories stayed identical
through the baseline runs. New pre-check evidence is the only subsequent repository addition.

## 2. Q1 — all `effective_name` users and the recommended shape

Pins below are by symbol; line numbers are relocation hints at `8360802`.

| Location | Actual dependency |
| --- | --- |
| `lib/core/node.cpp:41`, `Node::effective_name` | Definition: capacity zero returns 0; a stored name copies `min(_name_len, cap)` bytes; otherwise it manufactures the 26-byte default. No terminating NUL is promised. |
| `lib/core/node.h:576`, declaration | Public counted-copy API; comment promises the default. `name_len()` already exposes the stored count, but there is no public stored-name byte pointer. |
| `lib/core/node_hashlocate.cpp:767`, `Node::intro_attach_prefix` | Own name in the first-contact INTRO prefix. |
| `lib/core/node_hashlocate.cpp:1262`, `Node::send_hash_bind_pubkey_response` | Own name after the 34-byte authoritative key-answer base. |
| `lib/core/node_hashlocate.cpp:2321`, `Node::emit_hash_query` | Requester's own name in WANT_PUBKEY H. |
| `src/firmware_commands.cpp:1399`, `handle_whoami` | Writes ` name="`, exactly the returned byte count, and the closing quote. Zero count already produces `name=""`. |
| `test/test_node_r3.cpp:9060`, `§1.3 — effective_name defaults…` | Three calls: unnamed, `set_name`, and constructor name. The first pins the obsolete default. |
| `lib/core/node.cpp:32`; `lib/core/node.h:2978`; `src/fw_main.cpp:927` | Constructor, `_name` member and boot-load comments; the boot path calls `set_name`, not `effective_name`. |

The `rg` census across **lib/src/test/tools** found **four production calls**, that one native test with
three calls, the declaration/definition and the comments above. **No tool/probe text directly names
`effective_name`**. The exact census is retained in `effective-name-users.txt`. Other tool dependencies
on these files still apply (§7).

**Recommend retaining `effective_name(char*, uint8_t)` as the one pure stored-name counted-copy accessor.**
Remove only default synthesis. Copy `min(_name_len, cap)`, return that count, write nothing when it is zero,
and keep the non-terminating contract and full 32-byte capacity. Update its comments. This keeps every
producer on one path and makes `whoami` correct without a new formatter, public pointer, method rename or
call-site rewrite. Removing the API would broaden the fence to all callers for no product benefit (U1/C1/P7).
Keep `_name`, `_name_len`, their order and all other storage unchanged.

## 3. Q2 — exact bytes and receive/forward acceptance

These are **post-change predictions derived from the existing producers and codec**, not a claim that
W1c has been implemented. Named devices retain every existing byte. Counts below identify the prefix,
body or H frame explicitly; they are not all whole DATA-frame lengths.

| Surface | Unnamed bytes after W1c | Current → predicted | Source authority |
| --- | --- | --- | --- |
| INTRO **prefix**, before application text | `ed_pub[32]`, `00` | **59 → 33 B**, −26 | `intro_attach_prefix`, `node_hashlocate.cpp:767–776`; DATA framing and text follow unchanged |
| `AUTHORITATIVE_H_ANSWER_PUBKEY` **body** (`0x8B`) | `target_layer[1]`, `node_id[1]`, `ed_pub[32]`, `00` | **61 → 35 B**, −26 | `send_hash_bind_pubkey_response:1257–1267`; `pack_hash_bind_pubkey_inner`, `frame_codec.cpp:1403` |
| WANT_PUBKEY H, non-team | Existing 8-byte H header and `requester_ed_pub[32]`; **no name trailer at all** | **67 → 40 B**, −27 | `emit_hash_query:2321`; `pack_h`, `frame_codec.cpp:668–682` |
| WANT_PUBKEY H, team-scoped | Same, followed by existing `team_id` LE32; **no name trailer** | **71 → 44 B**, −27 | Same codec; TEAM flag and team ID unchanged |
| Home's `MOBILE_KEY_FORWARD` **body** (`0x96`) for that unnamed requester | `requester_ed_pub[32]`, `00` | **59 → 33 B**, −26, when a forward is emitted | `forward_requester_key_to_mobile`, `node_hashlocate.cpp:1439–1456`; caller passes the received H name/count |

The last row is an indirect effect of the changed request, not a fourth own-name producer. Existing
deduplication can suppress a repeated forward; tests must use a fresh requester/host fixture.

**Empty reception already works; receivers should not change:**

| Receiver / forwarder | Verified empty-name behavior |
| --- | --- |
| INTRO in `Node::do_post_ack`, `node_mac_rx.cpp:2732–2751` | Requires at least 33 bytes and a self-consistent SOURCE_HASH/key; `name_len=0` passes, sends a null name/count zero to `peer_key_set`, then removes exactly 33 prefix bytes. Message text is preserved. |
| `Node::on_hash_bind_pubkey`, `node_hashlocate.cpp:1271–1282` | Parses the 34-byte key-answer base; a 35-byte body with byte 34 = 0 is accepted. The key is cached and name count zero is passed on. |
| `parse_h`, `frame_codec.cpp:686–729` | WANT_PUBKEY requires its 32 key bytes. With no trailing bytes after the key and optional team ID, `name_len` stays zero in the initialized result. Existing native codec case `§name — a WANT_PUBKEY H…`, `test_frame_codec.cpp:989`, explicitly checks the 40-byte nameless form. |
| Owner branch of `Node::handle_h`, `node_hashlocate.cpp:1135–1148`; `Node::cache_want_pubkey_requester:1415–1427` | Still checks the requester key, caches it with the received count, and preserves the static/mobile/team addressing gates. Zero name count is accepted. |
| H forward in `Node::handle_h`, `node_hashlocate.cpp:1174–1182` | Copies the key and name count, copies zero name bytes, and `pack_h` emits no trailer. TEAM/mobile flags survive. |
| `Node::on_mobile_key_forward`, `node_hashlocate.cpp:1463–1475` | A 33-byte body with byte 32 = 0 passes length and nondegenerate-key checks; caches the key with zero name count. |
| `Node::on_mobile_hash_bind_pubkey_response`, `node_hashlocate.cpp:1481–1496` | The existing 40-byte mobile answer with byte 39 = 0 is accepted; caches key/home, passes null name/count zero. This format is not the retired push. |

All these cache paths converge on **`Node::peer_key_set`, `node_hashlocate.cpp:335–377`**. An existing row's
name is updated only under `name && name_len` (`:346`); a fresh/recycled row initializes `name_len=0`
(`:375`) before any optional copy (`:377`). A pinned row additionally refuses non-pinned on-air updates
before name handling. Thus **empty advertisements do not erase a retained cached name**. This is a source
fact; baseline native success does not substitute for the explicit W1c route-by-route regression cases.

**No wire-format or version change:** all predicted bytes are existing accepted encodings. Codecs, flags,
DATA types and header layouts remain untouched; `protocol::wire_version` remains **1**
(`protocol_constants.h:113`, native namespace guard). A shorter INTRO can now fit alongside longer application
text; test the 33-byte boundary and preserve the existing fail-loud too-large/plain-send behavior.

## 4. Q3 — mobile registration: corrected premise and scope boundary

The requested name-bearing `MOBILE_PUBKEY_PUSH` path **does not exist** in the current production code.
`0x94` is retained as a retired codepoint. The code at `node_hashlocate.cpp:1392` instead belongs to
**`Node::send_mobile_pubkey_answer`**, emitting **`MOBILE_H_ANSWER_PUBKEY` (`0x95`)**.

Tracing its supposed name producer yields:

1. `Node::presence_probe_fire`, `node_mobile.cpp:872–877` (and the searching branch `:825`), sends a
   `p_probe_in` containing identity, registration fields and optionally `ed_pub`.
2. `p_probe_in` / `p_probe_out`, `frame_codec.h:1404–1412`, have **no name field**.
   `pack_p_probe`, `frame_codec.cpp:1421–1432`, emits 8 bytes plus optional last-home 2 and pubkey 32;
   there is no name trailer.
3. `Node::presence_refresh_hosted_row`, `node_join.cpp:696–704`, refreshes liveness and copies only the
   verified key and `has_pubkey`; it never assigns a name.
4. `HostMobileEntry`, `node.h:3969–3974`, initializes `name[32]` and `name_len` to zero. Production row
   construction/recycling uses these initializers. The cross-core write census finds no production writer
   that populates those name fields; `send_mobile_pubkey_answer:1392–1393` only reads them.

**Result:** current registration carries **no own name, named or unnamed**. A production-created hosted row
therefore supplies zero name bytes to the home's 40-byte `0x95` answer. W1c must **leave this path and its
storage alone**. It neither needs the default removed there nor needs a new mobile registration feature.
The requester-key forward (`0x96`, §3) is a different direction and does carry the requesting peer's name.

**PC-1, existing source-comment drift / scope caution:** `HostMobileEntry` and the mobile-answer comments
still describe a name "pushed with the key". That is stale. This pre-check's explicit read-only fence leaves
the register untouched; this finding is handed to the Author for M1 registration/triage, with the retained
censuses as evidence. No new B-number is allocated here and no product repair is implied. If name-bearing
registration is desired separately, adding a P-probe field or reviving `0x94` would be additional protocol
work, outside W1c and its no-wire-change prediction.

## 5. Q4 — corpus census, prediction and fresh before-state

**Measured:** all **36** canonical scenarios contain **783/783** nonempty, non-NUL node names.
`JsonConfig::loadFromFile`, simulator `core/topology/JsonConfig.cpp:269`, requires a string field; that
alone does **not** prove nonemptiness. The JSON census establishes the stronger fact. There are no own-name
rename/set-name commands among **1160** scheduled command records and no Lua-only scheduled commands.
The per-scenario counts and exact command-verb distribution are in `scenario-name-census.json`.

There are **464** legacy node `script` references, not zero. They cannot rename a node in this gate:
`tools/run_corpus.py:476` passes `-e meshroute`; simulator `orchestrator/main.cpp:157–159` forces every
node's engine, and `SimController::initialize:791–797` loads scripts only for the Lua engine. The C++ path
passes the scenario name through `FirmwareNode::boot` (`FirmwareNode.cpp:73–74`) to the `NodeRuntime`
constructor (`NodeRuntimeWrapper.cpp:125–128`) and the existing `Node` constructor's `set_name` path.
The command adapter has no own-name setter. These source facts support the prediction rather than the
mere presence of a JSON label.

**Prediction for the future implementation:** **36/36 byte-identical**, including s18
**269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**. Any stream delta is an obstacle to explain,
not permission to update anchors. This is a `lib/core` change and still requires the full gate on both sides.

**Fresh baseline actually run here:**

| Instrument | Result |
| --- | --- |
| `pio test -e native`, then `./.pio/build/native/program` | Both exit 0; binary **2951 cases / 195777 assertions / 0 failed / 0 skipped** |
| Fresh Release simulator CMake configure/build, target `lus` | Exit 0; source MeshRoute `8360802`, simulator `6585649` |
| Stock `tools/run_corpus.py --require-anchors`, jobs 4 | **36/36 PASS**, all canonical anchors reproduce, inputs stable |
| Comparison with W1 coder corpus manifest | **36/36 identical** full MD5/SHA-256, byte counts, events, exit/assertion fields and scenario hashes |
| s18 in that corpus | **269517 events**, MD5 above; SHA-256 `27ecfa988f062d3b27ed9b19159965ac393a0f03e2499011d87d9797fe64cc54` |

The [complete per-stream before-state manifest](2026-09-25-standalone-mobile-home-w1c-precheck/corpus-manifest.json)
is the W1c comparison authority. The current `simulation/BASELINE.md` canonical table was consumed by the stock
runner; no historical anchor was substituted. Native used the existing build directory and the explicit binary
run; simulator build/output directories were new. Exact commands and exits are in `runs.json`.

## 6. Q5 — layout, NV and allocation

Source pins from `tools/probe_board_abi.py::PIN_TABLE` were independently re-measured with
`python3 -B tools/probe_board_abi.py --struct meshroute::Node --no-neg`:

| Target | `sizeof(Node)` | `alignof(Node)` |
| --- | ---: | ---: |
| native | 235208 | 8 |
| heltec_mobile (Xtensa) | 122176 | 8 |
| gateway (ARM) | 157304 | 8 |

The filtered baseline completes **38 checks**, including the instrument's ABI-divergence fixture and
feature-TU derivation. **Controls were skipped; this is not the full ABI gate.** The native assertion at
`node.h:4085` agrees. W1c needs no member, array, struct, alignment or pin change. No NV layout/version
change is needed (`mrnv::kVersion` remains 26); no cache migration or record rewrite is proposed.

Predicted resident-state delta: **zero**. Do not remove the inactive hosted-name fields as cleanup: that
would be a separate layout change. No board measurement was run here, as requested. The coder owns fresh
before/after linked measurements, **only below `.pio-measure/`** (B281/B315). Exact RAM/flash movement is
to be measured, not inferred from the deleted formatter; no allocation is granted by this ledger.

## 7. Q6 — `whoami`, source readers and mutation selectors

**Real device-output instrument:** `tools/probe_inbox_verbs/run.sh` compiles the real
`src/firmware_commands.cpp` and core under independent ACCEPT and CLIENT configurations. Its X1/X2
checks (`probe_main.cpp:808–815`) route `whoami` through text and JSON/LineSink paths and check destination
sink isolation. **They do not assert the name field.** Both arms need explicit empty and named/32-byte
name assertions, preserving quoting and the rest of the identity line. Add an assertion-failing control
that restores a nonempty default when unnamed. No production test hook is needed.

Current runner pins: ACCEPT **1394 checks / 61 controls**, CLIENT **477 / 69**. These are **source pins,
not fresh executions in this pre-check**. Fence both `probe_main.cpp` and `run.sh` for new checks, controls
and derived count changes. Keep numeric pins bare and run tools discovery after editing runners (D5).
`probe_console_sink` performs structural/help/profile checks on the same TU; it does not substitute for
executing `handle_whoami`. Its existing controls/readers must retain their effect (D6).

**Name-field consumers:** `tools/lab/parsers.py::parse_whoami` uses `name="([^"]*)"`, so empty is accepted
and returned as `""`; an absent field returns `None`. QA directly executed **three parser cases**: empty,
`Bench 1`, absent, with identity fields intact. Its existing `_selftest` covers named and absent, not
explicitly empty. `tools/lab/registry.py::discover` / `reattach` store that parsed value; lab provisioning
and manager/oracle paths use the same parser or pass-through response. `tools/fading_from_logs.py` parses
only the numeric identity. `meshroute_client_ble.py` is a console transport, not another name-field parser.
`test/console_battery.txt` invokes `whoami` but does not pin its default bytes. No tool needs a parser behavior
change for `name=""`; preserve the explicit compatibility proof.

**Selector (a), recommended minimal source fence:** AST-derived from all **105** `TARGET_SRC` mappings,
without importing the mutation harness:

| Battery | Configured source | Existing entries |
| --- | --- | ---: |
| `radmin8node` | `lib/core/node.h` | 1 |
| `radmin73node` | `lib/core/node.cpp` | 5 |
| `teamgrant` | `lib/core/node.cpp` | 4 |
| `b159map` | `lib/core/node.cpp` | 2 |
| `sliceBnode` | `lib/core/node.cpp` | 8 |
| `sliceEnode` | `lib/core/node.cpp` | 3 |

Total **6 batteries / 23 configured entries**; these are a source census, not a mutation run. The counts
do not establish name-behavior coverage. There are no `TARGET_SRC` mappings to `firmware_commands.cpp`,
`fw_main.cpp`, `console_parse.cpp` or the proposed test/probe files. A comment-only `node.h` edit still
includes its configured battery under selector (a).

If the Author instead removes the API or edits `node_hashlocate.cpp`, add **`grantpark` 3, `b161hash` 6,
`b251hash` 55**: that variant has **9 batteries / 87 entries**, before any new W1c controls. This is one
reason to retain the accessor and leave those callers unchanged. `mutation-selectors.json` records both
sets. The Author must name selector (b), new name-specific negative controls, and the required full-gate
union explicitly; neither **23** nor **87** is presented as that full union.

## 8. Q7–Q8 — documentation fence and cache preconditions

**Fence live source comments in the implementation brief**, so code and its promised behavior change together:

- `Node::effective_name` definition/comment; declaration and `_name` comment in `node.h`.
- `handle_whoami`'s inline default comment, `firmware_commands.cpp:1399`.
- Boot `set_name` comment, `fw_main.cpp:927`.
- `console_parse.cpp:195–198`, `peername` rationale: use an unnamed peer and a local display label as the
  motivating case. Grammar, refusals and parser executable code remain unchanged.

**Assign durable document updates explicitly to the Author/QA landing on PASS**, outside the coder's source
fence: address-book design **§2.3** (`2026-07-29-peer-address-book-design.md:128`), standalone design §4.6/§13
status and B447's unnamed-half disposition. Also include **`ios-companion/INBOX_SYNC_CONTRACT.md:535`**:
its live human-names section still promises the generated default. Its adjacent claim about "all three"
pubkey answers must not revive the retired push; describe actual paths and the hosted-name limitation
from §4. No Swift/UI or companion wire-contract extension is needed. Historical evidence stays historical.

The two other literal-default fixtures, `test_node_hashlocate.cpp:1781` (manual rename of a cached name) and
`test_device_nv.cpp:299` (stored peer record), need **no blanket rewrite**. Old default strings remain valid
cached data; W1c does not recognize, clear or migrate them.

**Fresh cache proof is available without new hooks.** Construct a fresh `TestHal` / `Node`, initialize its
configuration and use the existing key/receive/send seams in `test_node_hashlocate.cpp`. The per-layer
peer array/count starts empty. Do not treat a board reboot as an empty-cache guarantee: `/mrpeers` restores
the old name. Tests must separately prove (a) an unnamed fresh arrival remains nameless, and (b) zero-name
INTRO, key answer, WANT_PUBKEY and mobile-key-forward arrivals preserve an existing name (including an
old default or a local `peername` label). Keep the pinned-peer guard and named-peer refresh behavior.

B447 therefore closes **only its unnamed-advertiser overwrite half after implementation QA**. Named peers'
advertised names can still replace unpinned local labels; the broader precedence question stays open.

## 9. Recommended brief fence and acceptance obligations

**Production edits:** `lib/core/node.cpp` (`effective_name` body/comment only); `lib/core/node.h`
(the two own-name comments only, API and layout unchanged); `src/firmware_commands.cpp` and `src/fw_main.cpp`
(default-name comments only); `lib/console/console_parse.cpp` (`peername` rationale comment only).
No production edit to `node_hashlocate.cpp`, codecs, presence/registration, receivers, NV or simulator.

**Tests/tools:** `test/test_node_r3.cpp` for the obsolete default case and counted-copy boundaries;
`test/test_node_hashlocate.cpp` for exact real-producer bytes, receive/cache retention and forwarding;
`test/test_frame_codec.cpp` if additional empty/team codec goldens are useful; the inbox-verbs probe
main/runner for real `whoami` and controls; `tools/probe_ui_model_mutations.py` for explicitly named
name-specific controls and the derived native-count pin. Fence any new discovery regression by name in
the Author's brief; this ledger does not authorize an unspecified helper or source-reader workaround.

The brief should require:

1. Empty counted copy writes nothing, including capacity zero; named/constructor names still copy exact
   bytes through 32 bytes and truncation is only to the requested capacity. No implicit NUL reservation.
2. Exact unnamed INTRO/key-answer/H bytes from the **real Node producers**, with fresh peer caches and
   retained named controls; inspect key bytes, lengths, flags, team trailer and application payload.
   The current INTRO test derives its expected length from the emitted length byte, so strengthen it
   with an independent `name_len==0` and exact total; do not call that existing test sufficient as-is.
3. Empty receiver/forwarding and cached-name preservation cases from §§3/8; no invented name carrier
   on registration. A home-forward case uses the existing public seams and a fresh dedup state.
4. `whoami` empty/named output through real routing on both probe arms, with a usable negative control.
   Restoring the old default must also fail the native producer/accessor checks; controls compile and
   fail assertions rather than crash or become vacuous.
5. Full `lib/core` gate on **coder and QA**: native actual binary, fresh simulator/corpus with this complete
   baseline, ABI/required probes and controls, board pair plus six-environment warning census, tools
   discovery and the explicitly derived mutation union. Re-sync count pins from execution; do not
   inherit a W1 `src`-only QA reduction or equate selector (a) with the complete acceptance surface.

**No owner ruling is needed for the recommended W1c shape.** Obstacles to fold into the brief are the committed
base update, missing exact `whoami` assertions, the retired-push/name-less registration distinction, the live
companion document, explicit cache preconditions and the two mutation selectors. Any proposed name-bearing
presence protocol, cache-clear policy, storage removal or codec change is additional work and must return
for its own scope/ruling rather than being absorbed here.

## 10. Evidence and limitations

The [evidence folder](2026-09-25-standalone-mobile-home-w1c-precheck/) contains the full corpus manifest,
compact results, source/user censuses, input pins, base reconciliation and preservation receipts, plus
the read-only census/parser driver. Its `SHA256SUMS` covers every file in that folder.

Raw logs and the complete inventories are retained locally under ignored
`artifacts/2026-09-25-standalone-mobile-home-w1c-precheck/`; the fresh simulator build and 36 streams are
at `/tmp/meshroute-w1c-precheck-20260925-1c_q0s8j/`. These local locations are not off-machine backups.
No board pair, warning census, mutation union, inbox-verbs/full probe gate, full tools discovery or metal
test was run in this pre-check. The core and receiver claims above are source validation, supported by
the baseline suite and existing tests, not measurements of an unimplemented W1c candidate.

One scratch census initially tried to literal-evaluate the whole ABI table, which contains named constants
for unrelated types; it was corrected to extract only the literal `meshroute::Node` entries. The subsequent
stock compiler probe independently measured those entries. This did not modify any repository input or
affect the native/corpus results. Nothing was staged or committed.
