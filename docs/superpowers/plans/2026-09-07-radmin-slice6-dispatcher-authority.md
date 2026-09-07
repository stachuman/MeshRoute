<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 6 — the common dispatcher's validator, context, result and authority table · brief · 2026-09-07

**Status: REVISION 2 (2026-09-07) — the coder's preflight STOP report (`docs/superpowers/evidence/2026-09-07-radmin-slice6.md`,
B343/B344/B345) is FOLDED IN (§0.1). Preparation committed at `d1a2906`; DISPATCHABLE at its pin-only successor
commit (§1: the one that contains this file's hash-pin lines), simulator `06746a9` clean.**
Roles (owner, 2026-09-07): the Quality Agent (Claude) AUTHORS briefs, gates independently and lands documentation;
**the coder is Codex** (`docs/2026-09-02-agent-roles.md`, revised the same day); the owner rules and commits. The coder
validates this brief against the source at the pinned base BEFORE editing (every `file:line` below is a claim to
re-check, V1/V2), records any disagreement as a STOP-1 to QA, then implements — exactly as its preflight did.

Authority: pre-check `docs/superpowers/plans/2026-09-06-radmin-slice6-precheck.md`; the RULED classification
`docs/superpowers/plans/2026-09-06-radmin-authority-classification-proposal.md` (R-RA-33, with R-RA-32 as its one
exception); rulings R-RA-1, R-RA-7, R-RA-14, R-RA-21, R-RA-24′, R-RA-29, R-RA-30, R-RA-32, R-RA-33 in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`; design
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §12, §12.1, §13, §17, §19 item 6, §19.1
row 6. This is a feature slice on `src/` + `lib/console/` + `tools/`: ⛔ no `lib/core` edit, ⛔ no wire, ⛔ no NV,
⛔ no simulator edit, ⛔ no handler edit.

## 0. What Slice 6 is, in one line

On the one transport-neutral seam, land (a) the ONE command-byte validator (embedded NUL/CR/LF refused; ONE
per-surface bound passed as an argument), (b) the typed `CommandContext` / dispatch outcome threaded through the
seam without changing a local output byte, and (c) the owner-ruled authority table as CHECKED metadata — every
generated inventory row carrying exactly one class and its disruptive flag, consumed by the seam ONLY for a remote
context (of which no producer exists yet) — with the generator's `peers` duplicate merged first.

## 0.1 The coder's preflight findings, verified by QA against source and folded in (revision 2)

| finding | verified fact | where the brief changed |
| --- | --- | --- |
| **B343** — a seam-only validator misses BLE's earlier executing handlers | `src/fw_main.cpp:478-565` `ble_dispatch_line` executes FOURTEEN arms BEFORE its seam fallback (`whoami` :481, `version` :503, `prep-restart` :509, `rcmd` :513, `duty` :517, `limits` :521, `status` :534, `cfg set` :543 → `handle_cfg_set(line + 8, mrcon)` whose `name` arm uses `strlen(val)` (`src/firmware_config.cpp:272-279`), `cfg` :549, `routes` :554, `peers` :560-561, `pull_inbox`/`mark_read`/`del_msg` :563-565). A BLE line `cfg set name AB␀CD` reaches :543 with `len` covering the NUL and the tail is silently dropped. USB has NO pre-seam arm (`service_console` :1145-1183 makes exactly one seam call), so the seam's call covers USB; the panel's `exec_command` has its own call | §4.1 (the BLE head call + precedence + envelope), §5 (fence: `ble_dispatch_line`'s head), §6 item 4 (structural proof; no host instrument compiles `fw_main.cpp`), §9 (the one bench residue) |
| **B344** — (verb, sub-verb) alone cannot carry a per-surface class | `status`/`routes`/`duty`/`limits` occur on `dispatch` (inventory :26/:53/:55), `ble_dispatch_line` (:207/:218/:219) AND the legacy `remote_encode` surface (:228-231, `src/firmware_remote.cpp:26-50`). Revision 1 keyed the table by semantic (verb, sub-verb) with one class per key AND demanded the `remote_encode`/`remote_exec` rows be `legacy` — unsatisfiable | §4.3/§4.4/§4.5: the CLASS is semantic and surface-independent; SURFACE ELIGIBILITY is a separate, generator-known attribute; the `legacy` class is reserved for the semantic legacy verbs |
| **B345** — the prescribed `peers` edits remove ONE row, not two | inventory :44-46 (`all` + two bare `dispatch` rows) and :213-214 (two bare BLE rows); §4.5 drops the `dispatch` level-guard row and RELABELS the BLE refusal row | §4.5/§7: predicted **203**, with the exact before/after mapping; the `<args>` discriminator is bound to the bare `peers` semantic row, never fed to the native lookup as a sub-verb |

| **B346** — `acl list` classified `physical` in §6 contradicts R-RA-33 call 8 (a remote OWNER may list) | proposal §G: `acl list` = owner (remote) / physical (local); `acl reset` = physical | §4.4 (the transcription rule for dual readings), §6 items 2–3 (`acl list` → owner; `acl reset confirm` is the all-remote-refused control) |

The four are registered (B343–B346, next free **B347**); the coder's report stays as the preflight record at the head
of the evidence file, and the implementation sections are appended to the same file.

## 1. Closure bindings — the Slice 5 QA PASS pins (measured by QA 2026-09-07, brief §10 of the Slice 5 brief)

**MeshRoute PREPARATION commit: `d1a2906` ("slice 6 spec", main) — this brief (revision 2), the coder's preflight
report, the register and the roles document on top of `d226189` ("slice 5"). MeshRoute DISPATCH BASE: the
pin-only SUCCESSOR of `d1a2906` — the commit that contains this paragraph, whose diff against `d1a2906` is exactly
this brief's hash-pin lines and nothing else. The coder's preflight records that successor's `git rev-parse HEAD`
as the measured base, verifies `git diff --stat d1a2906..HEAD` names only this file, and requires an empty status.**
Measured checkout `/home/staszek/MeshRoute`.
**Simulator base: `06746a97de5764415d6fcef10b97bca90569b9c7` ("Slice 5"), CLEAN** — the stale "uncommitted line"
note of revision 1 is withdrawn. The coder's clean-start check (`git status --short` EMPTY in BOTH repositories, both
`HEAD`s recorded and equal to the pins) is a STOP-1 on any mismatch; ⛔ no "measure around", no self-repin.

| Required binding | Slice 5 closure entry (QA's independent gate, all reproduced the coder's figures) |
| --- | --- |
| Native | **2825 cases / 119784 assertions / 0 failed** (the binary's figures; the pio wrapper prints its false "0 test cases") |
| Corpus | 36/36 streams, anchors 36/36, s18 keystone `32afbf11` / 269517 / 0 (`simulation/BASELINE.md` is the authority; NEVER edited) |
| `lus` | md5 `db6582a171a6d785720e324c2cfe43f1` — Slice 6 predicts BYTE-IDENTICAL with **0 build actions** (no `lib/core` edit; the new `lib/console` header is not included by the simulator) |
| Node ABI (`probe_board_abi.py`) | `meshroute::Node` native 224136/8 · heltec_mobile 117912/8 · gateway 150504/8 — **UNMOVED** by Slice 6 |
| `probe_b278_row_abi.py` | 42 measurements, 6/6 controls RED — unmoved |
| Console sink | `profiles=6 checks=720 structural=52 ble_guard=905 ownership=6 ownership_controls=3 controls=101 unusable_controls=0` |
| Inbox verbs | ACCEPT arm 180 checks / 30 controls · CLIENT arm 178 / 33 · 0 unusable |
| Firmware UI · custody USB · BLE line | 223 controls · 27/10 · 40/8 |
| Features | 9 cells / 120 checks / 59 controls, ownership census 9 files + 40 controls (`--no-neg` ends PROBE-ONLY) |
| Warning census | gateway_heltec 173 · gateway_heltec_v4 178 · heltec_mobile 177 · heltec_v3 177 · heltec_v4 182 · heltec_v4_mobile 182 · `-Wswitch` 0 |
| Boards (ruled pair) | gateway RAM 197180 / flash 544668 / 285 objects · heltec_mobile 207748 / 1367444 / 329 |
| Inventory / help | **204 rows**, `--check` byte-for-byte; help union 53 (router 44/43/41/42/40/41 per profile, parser 7) |
| Tools sweep | `Ran 329 tests … OK` |
| Mutation harness | `tools/probe_ui_model_mutations.py` `PIN_CASES, PIN_ASSERTS = 2825, 119784`; union 28 targets 516 RED / 1 unusable (`sliceBmac` M04 = B342, pre-existing at base — ⛔ not this slice's to repair) |
| Register | next free finding number **B347** (B336–B342 = Slice 5's proposals; B343–B346 = this brief's preflight findings, registered) |

## 2. Verbatim authority pins

R-RA-24′ item 3:

> `remote_command_max_bytes` = the command TAIL after `--` = the smallest authenticated carrier's command capacity,
> **201** today (0e: cross-layer by hash, depth 4, RPC cap 226 − 25), derived by Slice 2's `remote_body_cap` and
> enforced by the ONE shared validator (R-RA-7: NUL/CR/LF + a length), which takes its bound per surface.

R-RA-24′, consequences: *"R-RA-14's 'one universal cap' reads as 'one validator, three named bounds' … USB local lines
are not shortened."*

R-RA-21 (policy) — open: *"only exact, argument-free `status` and `routes`"*; operator / owner / physical as listed;
*"Controller-only wrappers and trust-store commands do not become recursively target-dispatchable merely because
they occur in the generated inventory. Anything genuinely ambiguous or absent from the classified table refuses
closed."* QA confirmation: *"the generator must either merge true duplicates or expose the semantic discriminator.
QA then requires every target-applicable semantic command/subcommand exactly once, exactly one authority on every
such row, no unclassified/multiply-classified row, and mutation controls for a missing row, a duplicate semantic
row, and an unclassified row."*

R-RA-33 (owner, 2026-09-07): *"with rest - I do agree"* — the classification proposal is the ruled table as a whole.
R-RA-32 (owner): *"pull_inbox - we allow it for operator - as it might contain diagnostic data - best, if we'd
implement pull_inbox which would show all with exception of dm"* — `pull_inbox`/`mark_read` = operator; the
DM-excluding REMOTE view is Slice 7b's (the first remote executor), carried here as row metadata only.

Design §12:

> `DispatchResult` distinguishes at least matched/completed, unmatched, refused, scheduled, and internal failure
> without parsing printed text; policy is metadata or a check attached to the common command/subcommand, not a
> copied remote allowlist; the context distinguishes ordinary local authority from the explicitly physical
> provisioning/recovery state; local serial/BLE behaviour remains unchanged unless a separately reviewed command
> correction is needed.

> Command bytes pass one shared validator before local or remote dispatch. It rejects embedded NUL, CR, and LF and
> takes the applicable named bound as an argument; there is no second, more-permissive remote parser.

Design §19 item 6: *"No remote request reaches it yet, and local serial/BLE behaviour remains unchanged except where
that classification/validation was separately ruled. This slice cannot start until the complete generated
authority table has received its separate owner classification; missing or duplicate rows refuse the brief."*
§19.1 row 6: *"zero remote events; all pre-existing local behaviour attributed; ruled pair | none"* (no bench part).

## 3. Verified source state at `d226189` (V1) and corrections to the pre-check (V2)

| Source verified | Consequence for this slice |
| --- | --- |
| `src/firmware_commands.cpp:1561` `LineExec exec_console_line(line, len, fmt, stream, reply, reply_cap)`: router (`dispatch`, `:1412`) first, then `parse_command` + `handle_peerkey`/`handle_peername`/`Node::on_command`; header `src/firmware_commands.h:142-144` says in as many words that `DispatchResult`, `CommandContext` and policy are Slice 6's | the seam gains the context parameter and the validator; the router keeps its `bool` (the verb map is not the dispatcher — the seam is) |
| `src/firmware_commands.cpp:1722` `ExecResult exec_command(line, len)` — the OLED panel's typed path, parser → `Node::on_command`, no router, no `Print` | the validator must sit here too, or the panel bypasses it |
| `src/fw_main.cpp:1145-1154` `service_console`: `static char line[1024]`, `\r` skipped, `\n` terminates, over-long refused loudly (`> err: line too long (>1023) — rejected`); an embedded NUL is NOT refused (the `strncmp` arms stop at it silently) | the USB bound is 1023 command bytes (1024 storage incl. NUL); the buffer size becomes the NAMED constant, behaviour byte-identical |
| `src/device_ble.h:180-193` `kLineStorageBytes = kProductLineMaxBytes + 1` (derived: `larger_of(send, send_layer, remote) + NUL`, `static_assert`ed against the grammar maxima); `:212-216` overflow refuses `{"err":"line_too_long"}`; `:301-302` the same NUL blindness | the BLE bound is `kLineStorageBytes − 1` command bytes, PASSED by the BLE caller from that one constant — ⛔ never a literal |
| `src/fw_main.cpp:478` `ble_dispatch_line`; `:560-561` the two `peers` arms (bare streams; `peers <args>` refuses `console_only`); `src/firmware_commands.cpp:1434-1439` the two `dispatch` arms (`len == 5` bare; `len > 5 "peers "` = the family guard whose block parses `all`) | the generator's duplicate: rows 45/46 and 213/214 of the inventory (`:1434`/`:1435`, `:560`/`:561`) |
| `src/fw_main.cpp:480` `if (len == 0) return 0;` then FOURTEEN command-owning BLE arms (`:481-565`) BEFORE the seam fallback (B343) | the validator's BLE call site is the head of `ble_dispatch_line`, immediately after the empty-line return; the seam's own call stays (USB's only entry, and the remote consumer's) |
| the inventory's SURFACES (`tools/gen_command_inventory.py:113-195`): `dispatch` / sub-verb handlers / `parse_command` (target semantic), `ble_dispatch_line` + `service_console` (transport twins), `help_command` (local), `remote_encode` + `remote_exec` (the §17 legacy surfaces, `src/firmware_remote.cpp`) — and `status`/`routes`/`duty`/`limits` occur on THREE of them (B344) | the class is per semantic (verb, sub-verb); surface eligibility is per SURFACE; §4.3–4.5 |
| `tools/gen_command_inventory.py:423` `Row.key()` = `(surface, verb, subverb, func, source)` — the duplicate check includes `file:line`, so two arms of one semantic row are NOT duplicates to it; `:773-793` `verify_rows` REFUSES any non-empty `authority`; `test_gen_command_inventory.py:364` `test_populated_authority_cell_is_refused`, `:508` `test_every_authority_cell_is_empty` | both invariants INVERT in this slice (§4.5) and their tests are rewritten as the new obligations, never deleted |
| `lib/core/remote_codec.h:159` `kRemoteOverheadAuthExecute = 25`; `:298` `remote_body_cap(const RemoteCarrier&, size_t&)` is a RUNTIME function (not `constexpr`) — the depth-4 cross-layer by-hash cap is 226 | the 201 bound cannot be a `constexpr` derivation without touching `lib/core`; §4.1 binds it at TEST time to the live codec instead |
| `lib/console/console_parse.h` (`namespace meshroute::console`, `enum class ParseErr`), compiled by native AND the simulator's FirmwareNode; `lib/console/console_json.h:95` `write_err(buf, cap, code, msg)` | the validator's home and its BLE refusal envelope helper (U1) |
| `tools/probe_firmware_ui/probe_main.cpp:161, :544` — the UI probe FAKES `mrfw::exec_command` (`run.sh:1463` substitutes the call) | ⚠ V2 CORRECTION of pre-check §2/§4: the panel's validator path CANNOT be proven by the firmware-ui probe; it is proven by `tools/probe_inbox_verbs`, which compiles the real `firmware_commands.cpp` and calls `exec_command` directly. Firmware-ui pins are predicted UNCHANGED (223) |
| `tools/probe_features/ownership.py` — the capability-naming census is NINE files (Slice 5); `firmware_commands.cpp` has 6 approved sites | the three new headers name ⛔ NO `MR_FEAT_RADMIN_*` (the [[B255]] idiom); the seam's remote-context arm is UNGATED (inert on every build until 7b) — the census stays 9 files / 40 controls |
| `tools/check_a0_matrix.py` — the a0 idiom: a Markdown matrix ↔ a C++ enum, positional sections, `--selftest` proving the control can fail | the authority checker follows it exactly |
| R-RA-29/R-RA-30 BLE guards in `ble_dispatch_line` refuse the `acl`/`admin-id` family and the controller mutations BEFORE the seam | they stay the shipped truth for LOCAL contexts; Slice 6 adds NO second physical-presence check on the local path (a later slice may consolidate them onto the table — proposed, not done) |

## 4. Author decisions (resolving pre-check §6.2–§6.7)

### 4.1 The validator — `lib/console/console_line.h`, header-only, `namespace meshroute::console`

```cpp
enum class LineErr : uint8_t { ok, embedded_nul, embedded_cr, embedded_lf, too_long };
// `max_bytes` = the largest COMMAND-TEXT length the surface admits (its storage minus the NUL). len > max_bytes -> too_long.
LineErr validate_command_line(const char* line, size_t len, size_t max_bytes);
inline constexpr size_t local_command_max_bytes  = 1023;   // USB: fw_main's 1024-byte storage minus the NUL (R-RA-24′ "unchanged")
inline constexpr size_t remote_command_max_bytes = 201;    // R-RA-24′ item 3 — see the derivation binding below
const char* line_err_name(LineErr);                         // the ONE token mapper: "embedded_nul" | "embedded_cr" | "embedded_lf" | "too_long"
```
* Pure, allocation-free, `constexpr`-friendly; scans every byte once; refuses `\0`, `\r`, `\n` anywhere and
  `len > max_bytes`. Order of checks is FIXED (length first, then bytes) and tested so the reason is deterministic.
* The USB caller passes `sizeof(line) − 1` where `line` is declared from `local_command_max_bytes + 1` (byte-identical
  storage; the literal `1024` in `fw_main.cpp:1146` becomes the named constant + 1); the BLE caller passes
  `mrble::kLineStorageBytes − 1`; the panel passes `local_command_max_bytes`; the future remote consumer passes
  `remote_command_max_bytes`. ⛔ ONE function, three named bounds, no per-transport twin.
* **The 201 binding:** `remote_body_cap` is not `constexpr`, so `remote_command_max_bytes` is a named constant whose
  DERIVATION is executed natively: `test/test_console_line.cpp` builds the depth-4 cross-layer by-hash
  `RemoteCarrier` (the smallest authenticated carrier per R-RA-24′), asserts `remote_body_cap(...) == 226` and
  `remote_command_max_bytes == 226 − kRemoteOverheadAuthExecute`; a codec change that moves the cap turns the suite
  RED at the constant, never silently. The alternative (a `constexpr` twin inside `remote_codec.h`) is REFUSED: it
  is a `lib/core` edit and a `lus` rebuild in a `src`/`tools` slice.
* **Placement (B343): the validator runs at every TRANSPORT ENTRY, and the seam keeps its own call.** (i) BLE: the
  first statement of `ble_dispatch_line` after `if (len == 0) return 0;` — BEFORE `whoami` and every other arm —
  refusing with `write_err(out, cap, "bad_line", line_err_name(e))` returned as the buffered reply (the same envelope
  the seam's `json` arm uses, so one refusal shape on the transport); (ii) USB: the seam (`service_console` has no
  pre-seam arm — verified); (iii) the panel: `exec_command`; (iv) the future remote consumer: the seam with the
  201 bound. A well-formed BLE line is therefore scanned twice (head + seam, ≤ 274 bytes) — accepted, stated, and
  ⛔ NOT optimized away with a "pre-validated" context flag (a flag a future caller could forget is exactly the
  B343 shape). Precedence: empty line → silent (unchanged); bad line → refused before ANY arm; then today's order.
* Local delta (the only ruled one): an embedded NUL on USB or BLE is now refused LOUDLY before the router/parser
  (R-RA-7) — on BLE also before every companion arm. CR/LF cannot reach the seam from either intake today (both strip `\r` and split on `\n`) and cap+1
  cannot reach it (the intakes refuse first, with their existing envelopes, which stay byte-identical) — the validator's
  bounds are PINNED equal to the intakes' by the named constants, and executed natively and by the probe.

### 4.2 Context and outcome — `src/firmware_command_context.h` (NEW, pure, no capability macro)

```cpp
enum class CommandTransport : uint8_t { usb, ble, remote };
enum class CommandAuthority : uint8_t { local, remote_open, remote_operator, remote_owner };
struct CommandContext {
    CommandTransport transport;
    CommandAuthority authority;
    bool             physical_presence;   // true for the USB console only (R-RA-21 "physical = local USB-only")
    uint64_t         request_id;          // 0 for a local command
    size_t           line_max_bytes;      // THE surface's bound, passed to the ONE validator (R-RA-24′ "per surface")
};
enum class DispatchOutcome : uint8_t { completed, unmatched, refused, scheduled, internal_failure };
enum class RefuseReason  : uint8_t { none, bad_line, authority, unclassified };
```
* `exec_console_line(line, len, fmt, stream, reply, reply_cap, const CommandContext&)` — the context is a NEW
  trailing parameter (both callers in `fw_main.cpp` and the inbox-verbs probe update; no default argument, so no
  caller can forget it). `LineExec` gains `outcome`, `refuse` and `line_err`; `State` stays as the local completion
  (0c's contract) and maps: `streamed`/`buffered` → `completed`, `unmatched` → `unmatched`, `empty` → `unmatched`
  (nothing was owned), validator refusal → `refused` + `bad_line`. `scheduled` and `internal_failure` have NO
  producer in this slice (7b's) and are asserted unreachable natively.
* `ExecResult` (the panel) gains `line_err`; a refused line returns `ok == false` with `line_err != ok` and
  `parse_err == ok` (the parser never ran). No UI change: the panel cannot produce such a line; `firmware_ui.cpp` is
  ⛔ NOT edited.
* The two local contexts, built by their callers and by nothing else: USB `{usb, local, true, 0, sizeof(line) − 1}`;
  BLE `{ble, local, false, 0, mrble::kLineStorageBytes − 1}`; the panel's `exec_command` uses
  `local_command_max_bytes` internally (it has no transport).
* `dispatch(line, len, out)` (the router) keeps its signature and its `bool`: the design's
  `dispatch(..., const CommandContext&)` shape is realized at the SEAM (`exec_console_line` is the transport-neutral
  dispatcher since 0c); the router is the verb map. Stated as the Author's reading of "the exact type layout is an
  implementation decision".

### 4.3 The authority table — `src/firmware_command_authority.h` (NEW, pure, `constexpr`)

```cpp
enum class CommandClass : uint8_t { open, operator_, owner, physical, controller_local, legacy, local_only };
struct CommandPolicy { const char* verb; const char* subverb; CommandClass cls; bool disruptive; };
inline constexpr CommandPolicy kCommandPolicy[] = { /* one row per RULED semantic (verb, sub-verb) */ };
const CommandPolicy* command_policy_lookup(const char* line, size_t len);          // longest-match verb cell, then sub-verb cell, else the bare row; nullptr = unclassified
bool command_authority_admits(const CommandPolicy& row, const CommandContext& ctx, const char* line, size_t len);
```
* The `verb`/`subverb` cells are the INVENTORY's cells verbatim (`"cfg set"`/`"e2e_dm"`, `"mobile"`/`"register scan"`,
  `"peers"`/`"all"`, `"peers"`/`"—"`), so the three artefacts key on the same strings. **The class is SEMANTIC and
  surface-independent (B344):** `status` is `open` on `dispatch`, on `ble_dispatch_line` and on the legacy
  `remote_encode` surface alike — what differs across surfaces is ELIGIBILITY, which is a property of the SURFACE
  (§4.4), never a second class on the key. `local_only` is the class of the `help` family (the proposal's "— (local
  only)"); `legacy` is reserved for the SEMANTIC legacy verbs §17 deletes (`rcmd`, `password`, `unlock`, `lock`,
  `password rotate`); `controller_local` = `admin-key`/`admin-target` (+ the Slice 8 `remote` wrapper when it exists).
  ⛔ The `remote_encode`/`remote_exec` ROWS are NOT classified `legacy` — revision 1's demand is withdrawn; they carry
  their semantic class and a legacy SURFACE mark.
* `admits`: `local` → **true, unconditionally, and the table is NOT consulted** (local behaviour byte-identical by
  construction); `remote_open` → the row is `open` AND the line is the exact argument-free verb (R-RA-21: no
  argument inherits open access); `remote_operator` → `open` or `operator_`; `remote_owner` → `open`, `operator_` or
  `owner`; `physical`, `controller_local`, `legacy`, `local_only` → **false for every remote authority**; `nullptr`
  row → refused `unclassified` (R-RA-21 "refuses closed").
* The seam consults the table ONLY when `ctx.authority != local`, AFTER the validator and BEFORE the router/parser:
  a refusal returns `refused` + `authority`/`unclassified` and executes NOTHING. No remote context exists in
  production yet; the arm is proven by the inbox-verbs probe with synthetic contexts through the REAL seam (§6) and
  the pure functions natively on every row.
* The `disruptive` flag is carried per the RULED table (§13's set) and exposed by the lookup; ⛔ no consumer in
  this slice (7b schedules). A native case asserts the flagged set equals the ruled list exactly.

### 4.4 The ruled Markdown table and the checker

* The coder writes `docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md`: ONE row per semantic
  (verb, sub-verb) with `class` and `disruptive`, transcribed from the RULED proposal (its §A–§H tables, R-RA-33 +
  R-RA-32), cited row by row; the file header names both rulings. **Transcription rule for the proposal's dual
  readings (B346):** a row ruled "owner (remote) / physical (local)" — `acl list`, `acl add`/`set`/`remove`,
  `admin-id show` — transcribes as class **`owner`** (a remote owner may run it); its "physical (local)" half is the
  R-RA-29 TRANSPORT fact (USB-only locally, the shipped BLE guard), not a second class. Class **`physical`** is only
  for rows ruled physical in every reading: `acl reset`, `admin-id generate`/`rotate`/`reset`, and the controller's
  seed operations (which are `controller_local` anyway). It is the normalized rendering of the owner's
  one-shot ruling — ⛔ not a re-classification: a row the proposal does not cover is a STOP to QA, never a guess.
* **Surface eligibility (B344)** is a per-SURFACE attribute the generator already knows (`SURFACES`, `:113-195`):
  `target` (dispatch, the sub-verb handlers, `parse_command` — remote-dispatchable by class), `transport` (the
  `ble_dispatch_line` / `service_console` twins — inherit), `local` (`help_command` — never remote), `legacy`
  (`remote_encode`, `remote_exec` — §17 deletes them; never remote, never consulted by the seam). The inventory's
  `authority` cell renders the semantic class and, on a non-target surface, appends the surface mark
  (`open · surface:legacy`), so one cell stays unambiguous and no key carries two classes.
* `tools/check_command_authority.py` (the a0 idiom) proves the THREE artefacts agree: (1) every inventory row's
  (verb, sub-verb) resolves to exactly one table row — the SAME row on every surface; (2) every table row exists in
  the inventory (no orphan) and in the header, with the same class and flag; (3) classes are members of
  `CommandClass`; (4) no duplicate semantic row in either; (5) every `legacy`/`local` SURFACE row is marked so, and
  no `legacy`-CLASS verb occurs on a `target` surface other than the legacy families named in §17. `--selftest`
  proves each failure mode RED on a scratch copy: a missing row, a duplicate row, an unclassified row, a
  table/header disagreement, an orphan, a surface mark dropped. It joins the tools unit sweep
  (`tools/test_check_command_authority.py`) and the gate chain beside `check_a0_matrix.py`.

### 4.5 The generator — `tools/gen_command_inventory.py`

1. **The `peers` merge (prerequisite, its own commit-able step 0):** an arm whose block yields sub-verb rows of the
   same verb is that verb's LEVEL GUARD, not a second bare row (`dispatch` `:1435` → only the `all` row survives
   beside the bare `:1434` row); an arm whose body is a REFUSAL (`write_*_err(…, "console_only")`) exposes its
   discriminator in the sub-verb cell (`ble_dispatch_line` `:561` → `peers | <args> — refused console_only`), so the
   two BLE rows are no longer indistinguishable. `verify_rows` gains a SEMANTIC duplicate refusal — equal
   (surface, verb, sub-verb) regardless of `file:line` — with unit tests for the merged shape and the refusal.
   **Predicted (B345, corrected): 204 → 203 rows.** Exact mapping: inventory :45 (`peers —` @`:1434`) stays; :46
   (`peers —` @`:1435`, the level guard) is REMOVED; :44 (`peers all`) stays; :213 (`peers —` BLE @`:560`) stays;
   :214 (`peers —` BLE @`:561`) becomes `peers | <args> — refused console_only` (relabelled, not removed). The
   `<args>` cell is a DISCRIMINATOR, not a sub-verb: the generator and the checker bind that row to the bare
   `peers` semantic row (class operator, transport arm), and ⛔ it is never fed to `command_policy_lookup` as a
   sub-verb in any test. Help union 53 unchanged; ownership.py's router/parser NAME sets unchanged.
2. **The `authority` column is FILLED** from the ruled Markdown table (the same document idiom the generator already
   writes): `class` plus ` D` when disruptive. `verify_rows` INVERTS: an EMPTY authority on any row is REFUSED
   (every row is classified by its SEMANTIC row — target verbs by the policy, `help` as `local_only`, the semantic
   legacy verbs as `legacy`, controller families as `controller_local` — and a non-target SURFACE additionally
   carries its surface mark, §4.4); a row whose (verb, sub-verb) the table lacks is REFUSED. The two
   old tests become `test_unclassified_row_is_refused` and `test_every_row_carries_exactly_one_authority` (the file's
   comment keeps the old obligation visible). `--write`, then bare + `--check` byte-for-byte, as today.

### 4.6 Envelopes (the only new local bytes)

* USB (`LineFormat::text`): `> err bad_line <reason>\n` written to `stream` (`reason` = `line_err_name`).
* BLE (`LineFormat::json`): `write_err(reply, cap, "bad_line", "<reason>")` → `{"err":"bad_line","msg":"<reason>"}`
  staged in `reply` (`State::buffered`), the companion's established shape (U1).
* Authority refusals (remote contexts only): NO envelope in this slice — the seam returns `refused`; 7b renders the
  RPC terminal. Asserted: a refused remote line writes ZERO bytes to `stream`/`reply`.

### 4.7 Numbering

Proposed findings start at **B347** (B343–B346 are this brief's own preflight findings, already registered); the
coder proposes in the evidence and ⛔ never edits the register.

## 5. Exact implementation/instrument fence

- NEW `lib/console/console_line.h` (header-only; no `.cpp`, so ⛔ no simulator source-list edit; not included by
  the simulator). NEW `src/firmware_command_context.h`, NEW `src/firmware_command_authority.h` (pure; no capability
  macro; `constexpr` table in `.rodata`).
- `src/firmware_commands.{h,cpp}`: the seam's new parameter, the validator call at the head of BOTH
  `exec_console_line` formats and of `exec_command`, the remote-only policy consultation, the two refusal envelopes,
  the `LineExec`/`ExecResult` fields, header comments corrected (the "Slice 6's" sentences become "landed"). ⛔ No
  handler, verb, help, BLE-guard or router-arm edit; `dispatch()`'s signature and every response byte unchanged.
- `src/fw_main.cpp`: the two callers build their local contexts and pass their bounds; the USB storage declared from
  the named constant; **the ONE validator call + `bad_line` envelope at the head of `ble_dispatch_line` (B343)**,
  immediately after the empty-line return and before `whoami`. ⛔ Nothing else — no arm reordered, no companion
  envelope changed; the intakes' own refusals stay byte-identical.
- `src/device_ble.h`: ⛔ untouched (its constant is READ by `fw_main.cpp`).
- `tools/gen_command_inventory.py` + `tools/test_gen_command_inventory.py` (§4.5); the regenerated
  `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` (`--write`, coder-owned).
- NEW `docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md`; NEW `tools/check_command_authority.py`
  + `tools/test_check_command_authority.py`.
- `tools/probe_console_sink/{structural.py,negctl.py,run.sh}`: structural rows (the validator precedes the router
  on both formats and in `exec_command`; the local path never consults the table; both callers pass a DERIVED bound;
  the seam writes nothing on a remote refusal) + their controls (delete the validator call; invert `admits`;
  consult the table for `local`; a literal bound; drop the `exec_command` call) — pins re-derived, attributed.
- `tools/probe_inbox_verbs/{probe_main.cpp,run.sh}`: executed rows on BOTH arms (§6); pins re-derived per arm.
- `tools/probe_ui_model_mutations.py`: NEW batteries for the three new headers (`consoleline`, `cmdcontext` if it
  carries decisions, `cmdauthority`) and the generator/checker are covered by their unit tests; PIN re-sync.
- NEW `test/test_console_line.cpp`, NEW `test/test_command_authority.cpp` (+ context mapping cases); the seam itself
  is not compiled natively (§B115) — its cover is the inbox-verbs probe's executed rows and the console-sink controls.
- `tools/test_probe_console_sink.py` / `tools/test_probe_features.py`: pins are DERIVED since Slice 5 — expected
  untouched; `tools/probe_features/ownership.py`: expected untouched (no new capability site).
- Evidence `docs/superpowers/evidence/2026-09-07-radmin-slice6.md` (the preflight report stays at its head; the
  implementation sections are appended), both repository statuses/diffs (the simulator diff must be EMPTY),
  proposed findings from B347.
- ⛔ OUT OF FENCE: `lib/core/*`, `src/firmware_ui*`, `src/device_ble.h`, `src/firmware_help.h`, any handler, the
  register/bench/manual/design/rulings/QA ledgers/BASELINE, the simulator.

Every modified/untracked path is listed; out-of-fence is STOP. No refactor/helper move, no test-only production
flag, no default argument on the seam, no second validator, no per-transport cap, no name-based allowlist.

## 6. Wiring proof — what the instruments must EXECUTE

1. **Native `test_console_line.cpp`:** every byte class at every position (first, middle, last) refuses with the
   named reason; length exactly at each bound admits and bound+1 refuses (1023/1024, 274/275, 201/202); the reason
   order is deterministic (a too-long line with a NUL reports `too_long`); the 201 derivation binding (§4.1); the
   token mapper covers every enumerator (the `cmdcode_name` idiom, walked).
2. **Native `test_command_authority.cpp`:** every `kCommandPolicy` row is found by `command_policy_lookup` from a
   line built from its own cells (and from that line plus an argument); `cfg set e2e_dm` → owner, `cfg set name` →
   operator, `mobile register scan` → operator (not disruptive), `peers all` and bare `peers` → operator, `status` →
   open and `status x` refused under `remote_open` but admitted under `remote_operator`, `team new` → owner +
   disruptive, `acl list` → owner (R-RA-33 judgment call 8: a remote OWNER may list), `acl reset` → physical,
   `admin-id show` → owner, `admin-id generate` → physical, `admin-key list` → controller_local, `rcmd` → legacy,
   `help` → local_only, an
   unknown verb → `nullptr`; the `admits` truth table for all 4 authorities × 7 classes; the disruptive set equals
   the ruled list; `local` never consults (a fake table with a poisoned row is never read for a local context —
   prove by a counting lookup or by the seam control, whichever is reachable).
3. **Inbox-verbs probe, BOTH arms, through the REAL `exec_console_line`:** on `text` and `json`: a line with an
   embedded NUL → the exact refusal bytes and NOTHING executed (the fake node's command counter unmoved); embedded
   CR and LF likewise; a line of exactly the bound admits and executes; bound+1 refuses (the probe calls the seam
   directly, so it CAN present cap+1 — the only instrument that can); `exec_command` with a NUL line → `ok=false`,
   `line_err=embedded_nul`, `parse_err=ok`, nothing executed; a local context leaves every pre-existing row's bytes
   IDENTICAL (the arm's whole prior transcript re-asserted); synthetic remote contexts `{remote, remote_open|
   remote_operator|remote_owner, false, 42, 201}` on `status` (admitted at all three), `status x` (refused only at
   open), `version` (refused at open, admitted at operator), `factory_reset confirm` (refused at open and operator,
   admitted at owner — and the fake NV proves it was NOT executed when refused), `acl list` (refused at open and
   operator, ADMITTED at owner — R-RA-33 call 8), `acl reset confirm` (refused at ALL three: physical — the
   all-remote-refused control), `admin-key list` (refused at all three: controller_local), `rcmd 1 status`
   (legacy: refused), an
   unknown verb (refused `unclassified`) — each refusal writing zero bytes.
4. **Console-sink probe:** structural rows S53+ per §5 with their controls — including **the BLE head (B343): the
   validator call is the first statement of `ble_dispatch_line` after the empty-line return, precedes the first
   command-owning arm, and refuses through `write_err("bad_line", …)`; controls: the call deleted, moved below the
   first arm, or its refusal replaced by fall-through — each RED.** ⚠ Stated honestly: NO host instrument compiles
   `src/fw_main.cpp` (`tools/probe_inbox_verbs/run.sh:158`; `probe_ble_line` drives `device_ble.h` with a fake
   dispatcher), so the BLE head is proven STRUCTURALLY plus the function natively plus ONE bench line (§9); the
   BLE-guard (905) and ownership (6/3) pins predicted unchanged; the `bad_line` envelopes' exact bytes on every profile.
5. **Generator + checker:** `--write` then bare + `--check`; the checker green on the tree and its `--selftest` RED
   on all SIX sabotages (§4.4: missing row, duplicate row, unclassified row, table/header disagreement, orphan,
   surface mark dropped); the tools sweep `OK` with the new tests counted and derived.

## 7. Mutation union and the full gate

Selectors derived from the final diff and printed separately: changed-source = the three new headers' batteries +
any battery configured on `src/firmware_commands.cpp`/`src/fw_main.cpp` (none exist — state so, and name the probe
controls that cover them); dependency = `radmin2codec` (the 201 binding reads the live codec) and any
`lib/console` battery the map already carries. Every new control mutates a REAL copy at exactly one match and
turns the suite or the probe RED; zero-match / still-green = unusable, reported, never scored.

Run base and final: `pio test -e native` THEN `./.pio/build/native/program`; the simulator `cmake --build` as a
CHECKED NO-OP (`lus` md5 identical, 0 build actions — a rebuild here means an out-of-fence edit); corpus
`--jobs=8 --require-anchors` 36/36 with the keystone reproduced; both ABI probes unmoved; all six probes with
controls, both inbox arms; `--no-neg`; the tools sweep; inventory `--write`/bare/`--check`; the NEW checker +
`--selftest`; `check_a0_matrix.py`; `check_data_type_literals.py`; the warning census at its pins with zero new
warnings; both repos' `git diff --check`; the ruled pair. **Predict before measuring:** RAM ±0 on both boards
(constexpr table in flash; the context is a stack value); flash + on both, attributed to the symbols (validator,
table, seam growth, the BLE head); native +N cases; inventory **203**; tools sweep 329 + the new tests;
console-sink and inbox-verbs pins +rows exactly as derived; everything else at its Slice 5 pin.

Evidence: predictions first, base/final outputs, the row/pin derivations, the three-artefact agreement transcript,
the union, the two-repo manifests (simulator diff EMPTY), proposed findings, and exactly:

`PIN re-synced? YES — <derivation>`

## 8. STOP conditions

1. Dirty measured base (either repository), a §1 placeholder, concurrent input, or an out-of-fence path — STOP to
   QA; never repair, repin, commit or "measure around".
2. A row the ruled proposal does not cover, a duplicate/orphan/unclassified row the checker cannot resolve by the
   generator's own rules, a semantic key that would need two classes, or a classification the coder believes
   wrong — STOP to QA/owner; ⛔ no guessing from names, ⛔ no surface dropped to make a key unique.
3. Any local output byte changed beyond the two `bad_line` envelopes on a NUL/CR/LF line; the table consulted for a
   `local` context; a second validator or per-transport cap; a literal bound; a name-based remote allowlist; the BLE
   guards moved; a `lib/core`, handler, UI or simulator edit — STOP.
4. A `scheduled`/`internal_failure`/authority envelope producer, any remote consumer, any wire or NV change — STOP.
5. `lus` rebuilt, a corpus delta, a Node ABI move, RAM ≠ 0 on either board without attribution, a census move, a
   failed gate, an unusable control, a lost row/pin — STOP; no re-anchor, count laundering or tolerance.

## 9. QA landing after the software PASS (QA authors and lands; the owner commits)

Read the evidence; verify the ruled table transcription row by row against the proposal (R-RA-33/32); close only
proven register obligations; land design §12/§12.1/§13/§19.1 row 6 as measured; publish the command authority
table in the manual from the generated inventory; keep B312/B317/B330/B337/B342 open for their own scope; close
B343–B346 as folded in. **One bench residue (M2, QA lands it in the bench script):** the BLE head is host-unexecutable,
so the bench sends a BLE-NUS line carrying an embedded NUL (`cfg set name AB␀CD`) and expects
`{"err":"bad_line","msg":"embedded_nul"}` with the node name UNCHANGED — the only metal-only behaviour this slice
adds (§19.1 row 6 otherwise: none). Propose, not do: consolidating the R-RA-29/30 BLE guards onto the table's `physical` /
`controller_local` classes (a later refactor slice, C1).
