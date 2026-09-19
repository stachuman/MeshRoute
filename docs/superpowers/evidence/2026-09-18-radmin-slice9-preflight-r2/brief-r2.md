<!-- QA/Author: Claude (revision 1); production coder: Codex; owner rules and commits -->
# Remote-admin v2 Slice 9 — legacy protocol deletion and the durable protocol docs

**Revision 2 — 2026-09-18: R1 RULED as recommended (R-RA-50) — READY FOR CODER SOURCE-VALIDATION AND IMPLEMENTATION;
nothing is HOLD.** Revision 1 (`c9b629eb…`) → 2 changes only this header, the §1 lab-tool row and §8; the contract is
unchanged.
Base **`84edd3e`** (owner commit `part 8` = the 8b freeze, [independent QA PASS](../evidence/2026-09-18-radmin-slice8b-qa-gate.md)),
clean; simulator **`6585649`**, clean. The coder pins this brief by content hash plus the inventory of the QA documents
that announce it. A brief under implementation is frozen (P4); a mid-slice ruling lands in the ledger and register and
is re-pinned at a checkpoint. Slice 10 (main-NV cleanup) stays separate (R-RA-6, P6) and follows this slice.

## 0. What Slice 9 is, in one paragraph

With 8b the v2 product path is complete, so the legacy remote-management mechanism has no remaining user: delete it
whole — the `rcmd` issuer and its target executor, the `password` / `unlock` / `lock` administrator flow, the sealed
body codec (`admin_auth`), the binary TLV response encoders (`console_binary`), the `app_dm=false` senders
`send_remote_cmd` / `send_remote_response`, the `MR_FEAT_REMOTE_MGMT` switch and every reader of it — and write the
durable protocol documentation for what replaced it (`docs/frames.md` byte layouts, `docs/protocol.md` behaviour,
console help, companion contract, bench). Two things are deliberately NOT deleted here: the three legacy `Node`
mirrors and the three `/mrcfg` blob fields (`admin_pubkey`, `admin_counter_floor`, `admin_provisioned`) plus the
boot `admin_load` that copies one into the other — those are Slice 10's atomic NV-version change (design item 10,
R-RA-6). Their compile gate simply moves from the deleted switch to `MR_FEAT_RADMIN_ACCEPT`, which the existing
`#error` already proves equal on every board. Predicted: corpus byte-identical, `sizeof(Node)` unchanged on all three
ABIs, RAM and flash DOWN on both boards, no wire change, no new verb, no new TU (P7: two TUs are REMOVED and neither
is in the simulator's source list — verified).

## 1. What binds this slice (links, not quotes)

Design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`: §17 (the hard replacement list —
this slice executes every bullet except the NV/`Node` fields), §19 item 9 (the assignment, incl. "keep the
capability-owned v2 handlers" and the documentation duty), §19 item 10 (what stays for 10), §19.1 row 9, §8.1–§8.11
(the wire bodies to document), §5–§16 (the behaviour to document). Rulings: R-RA-6 (NV cleanup is its own slice),
R-RA-8/17/26/27 (capability ownership; the legacy round trips were suspended "until Slice 9's replacement" — this is
that replacement), R-RA-13 (the legacy senders lack `SOURCE_HASH` and are not a carrier), R-RA-33 (the authority table
is ruled; removing rows follows the design). Working rules CLAUDE.md C1–C4, D1–D6, P4–P7; `docs/CODE_GUIDELINES.md`
(delete dead code with its feature; fix drifted comments you touch — V1).

| Verified seam at `84edd3e` (pin by symbol; lines are hints) | What Slice 9 does |
| --- | --- |
| `src/firmware_remote.{h,cpp}` (217 + 38 lines): `REMOTE_FLAG_SEALED`, `remote_encode`, `remote_verb_open`, `remote_seal_resp`, `remote_exec` (real + the inert stub), `handle_unlock`, `handle_lock`, `admin_verb_gated`, `handle_rcmd` | **Delete both files.** `handle_rcmd`'s cleartext path is compiled on EVERY profile today (the header says so), so the mobile image shrinks too. |
| `lib/core/admin_auth.{h,cpp}` (`admin_key_from_password`, `AdminCmd`, `admin_cmd_seal`, `admin_cmd_open`, `admin_counter_ok`, `AdminVerdict`, `ADMIN_KDF_ITERS`, `ADMIN_SALT`) + `test/test_admin_auth.cpp` (4 cases); users: `firmware_remote.cpp`, `firmware_config.cpp` (`handle_password`), `fw_main.cpp` (include), `test/test_remote_codec.cpp:36` | **Delete** the codec, its test and its includes. `test_remote_codec.cpp` §8 ("a legacy frame is never accepted as v2") keeps its proof by replacing the live `admin_cmd_seal` call with the FROZEN byte literal that call produces at `84edd3e` for its fixed inputs (record it once, label it "last output of the deleted legacy sealer"); the v2 rejection assertions are unchanged. Not in the simulator source list (verified) — no CMake line to remove. |
| `lib/console/console_binary.{h,cpp}` (`bin::enc_status/enc_routes/enc_duty/enc_limits/enc_cfg`, `StatusDiag`) + `test/test_console_binary.cpp` (16 cases); users: `firmware_remote.cpp` only (`fw_main.cpp` includes it under the switch; `node.cpp:~1811` and `node_carriers.h:~157` mention it in comments) | **Delete** (design §17: "its binary response encoders"); fix the two comments. Not in the simulator source list (verified). No tool includes it. |
| `lib/core/node.h` `send_remote_cmd` / `send_remote_response` (`:~176`) and their bodies in `lib/core/node_mac.cpp` (`:~868`, `app_dm=false`, tx events `rcmd_tx` / `rcmd_resp_tx`) | **Delete** (R-RA-13). The two tx-event names never fire in the corpus (zero `rcmd`/`0xA0` traffic — design §17). |
| `lib/core/node.h` `#if MR_FEAT_REMOTE_MGMT` accessor block (`:~126–137`: `admin_provisioned()`, `admin_pubkey()`, `admin_counter_floor()`, `admin_set_pubkey`, `admin_load`, `admin_counter_check_advance`, and the `#else` stubs) and the field block (`:~3027`: `_admin_pubkey[32]`, `_admin_counter_floor`, `_admin_provisioned`) | **Re-gate both blocks on `MR_FEAT_RADMIN_ACCEPT`** (identical truth table: boards by the `#error`, native and lus `{1,1}`), **keep** `admin_load` and the three read accessors with their stubs (Slice 10 removes them with the fields), **delete** `admin_set_pubkey` and `admin_counter_check_advance` in both arms (their only callers die here). `sizeof(Node)` must not move on any ABI. |
| `lib/core/mr_features.h`: the `MR_PROFILE_MOBILE ⇒ MR_FEAT_REMOTE_MGMT 0` block (`:~33`), the `#ifndef … 1` default (`:~86`), the board `#error "… ACCEPT != REMOTE_MGMT …"` (`:~104–109`), the gateway comment (`:~25`) | **Delete all four.** The RADMIN pair is already derived on its own (R-RA-26); nothing references the legacy switch afterwards. `tools/probe_features` and `ownership.py` pins move with it (below). |
| `src/fw_main.cpp`: `using mrfw::remote_exec/handle_rcmd/handle_password/handle_unlock/handle_lock` (`:~99–119`), the switch-gated `admin_auth.h` / `console_binary.h` includes (`:~120–123`), the `firmware_remote.h` include (`:~49`), `g_admin_id` / `g_admin_unlocked` / `g_admin_tx_ctr` definitions (`:~531–541`), the unconditional `g_remote_action` / `g_remote_action_at` (`:~143–145`) and their consumer in the operating block (`:~1853–1856`, the legacy "respond first, act 3 s later" reboot / prep-restart), the BLE `rcmd` arm in `ble_dispatch_line` (`:~596–599`, `{"ev":"rcmd_sent"}`), `fw_wdt_feed` (`:~285`, its only callers are the two deleted KDF sites) | **Delete all of it** (the deferred-action mechanism of the legacy executor is fully replaced by 7b-3's `remote_action_take`). `admin_load` at `:~972` stays (Slice 10). `fw_context.h`: the `g_remote_action*` externs (`:~39–40`), the `g_admin_*` externs (`:~117–120`) and `fw_wdt_feed` (`:~130`) go with them; the header comment at `:~12` is corrected. |
| `src/firmware_commands.cpp`: `#include "firmware_remote.h"` (`:~21`), the `rcmd` arm (`:~1569`), the `password` / `unlock` / `lock` arms under the switch (`:~1679–1683`), the `:~1465` and `:~1548` comments; `src/firmware_config.{h,cpp}`: `handle_password` (`:~2437–2461`, and the `admin_auth.h` include at `:~38`); `src/firmware_help.h`: the three switch-gated entries (`lock`, `password`, `unlock`) and the `rcmd` line (`:~114`), the `:~37` comment | **Delete.** After this, `rcmd`, `password`, `unlock` and `lock` are ordinary unknown verbs: USB `> parse error`, BLE `{"err":"parse","msg":"unknown_cmd"}` — the existing common-dispatcher refusal (`firmware_commands.cpp:~246/441`, `fw_main.cpp:~738`), no special message. |
| `src/firmware_command_authority.h`: rows `lock`, `password`, `password rotate`, `rcmd`, `unlock` (`CommandClass::legacy`); `docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md`: the four `legacy` rows + the header prose mentioning legacy (`:~11`); `tools/check_command_authority.py`: `LEGACY_FAMILIES` and the `legacy`-class rule (`:~19/93/115`); `tools/gen_command_inventory.py`: `"legacy"` in `AUTHORITY_CLASSES` and the `return "legacy"` arm (`:~79/763`), the `firmware_remote.cpp` `Surface` entries and its reached-from table (`:~225–265`), the `MR_FEAT_REMOTE_MGMT` profile column (`:~309–321`) and the `:~288` comment; `tools/test_gen_command_inventory.py` (`:~692`, `:~885–912` — the "not the legacy switch under another name" tests); `test/test_command_authority.cpp:~69` (`{"rcmd 1 status", CommandClass::legacy, false}`) | **Remove the five rows from all three artefacts in one freeze** (the checker must never see them disagree — R-RA-42's landing rule), delete `CommandClass::legacy` and every `legacy` special case in the tools, drop the profile column, retire the two tests that exist only to keep the two switches apart (their obligation ends with the switch), and retarget the `rcmd 1 status` fixture to the unknown-verb expectation. Inventory rows: **208 → 202** (`lock`, `password`, `rcmd` ×2, `unlock`, `password rotate`). |
| `tools/probe_console_sink/probe_main.cpp` (`:~207–209` expected-verb table, `:~276–279` handler stubs, `:~1912` the `rcmd 1 status` sample), `ownership.py:~66` (the mobile verb count comment); `tools/probe_inbox_verbs/probe_main.cpp:~276–279` (stubs), `transcript_main.cpp:~110` (prints the switch); `tools/probe_features/probe_main.cpp:~110/121`, `run.sh` (`EXP_REMOTE_MGMT`, A1/A2 wording, controls C3/C4 which delete the agreement `#error`), `ownership.py` (`APPROVED_SITES` for `mr_features.h`, `LEGACY_AGREEMENT_PIN`, O5, and the `|| MR_FEAT_REMOTE_MGMT` widening controls at `:~536–680`); `tools/probe_ui_model_mutations.py:~8860`; `tools/probe_console_sink/structural.py` S34/S45 (absence of `remote_exec` / `g_admin_id` / `admin_load` — stay as absence checks, update their rationale text) | Re-derive every census and expected list (D5/D6). Controls whose whole point was the legacy switch (C3/C4, O5's widening family) are **retired with an explicit note or retargeted to another forbidden widening** — never left vacuous; the coder chooses in preflight and the receipt says which. |
| `tools/meshroute_lab.py`: `cmd_rcmd` (`:~212–235`) and its sub-parser (`:~302–308`) | **Delete both (R-RA-50).** A lab `remote` helper is a separate tooling item after Part 57c. |
| `ios-companion/INBOX_SYNC_CONTRACT.md`: "Ask 1 — remote-admin app surface" (`:~928–937`, the `rcmd` spine / `unlock` asks), `:~971` (the `rcmd` response printer remark), `:~1038` ("Remote management: `rcmd <dst> <verb>`"), `:~824` ("e2e-ack / rcmd are exempt"); `docs/2026-07-31-bench-test-script.md`: the 9.9 R-RA-27 suspension block (`:~757–768`), the Part 55a sentence (`:~4229`) | Mark Ask 1 SUPERSEDED by the landed `remote` / `remote_carrier` sections (8a+8c, 8b) and delete the `rcmd_sent` event; rewrite the three lines; the bench block becomes "REMOVED in Slice 9" and **Part 57e** lands. |
| `docs/frames.md` rows `0xA0` / `0xA1` (`:~250–251`, still "OTA remote-diagnostics … plaintext inner"); `docs/protocol.md` (no remote-administration section at all); `platformio.ini:~514` (comment); `lib/core/node_mac_rx.cpp:~1905–1992` and `node.h:~1795` (comments naming the legacy switch / "Slice 9 deletes") | The documentation duty (§4). Comments are corrected where touched (V1). |

Baselines (8b gate, 2026-09-18): native 2970/195942/0; corpus 36/36, s18 `32afbf11`/269517/0; Node 235248 native /
122176 mobile / 157344 gateway; gateway RAM 204036 / flash 575008; heltec_mobile RAM 211772 / flash 1395276;
xiao_mobile 176604 / 700588; inventory 208; union floor 61 batteries / 984 configured; census 173/178/177/177/182/182.

## 2. Scope

**IN:** every deletion in §1; the re-gate of the kept `Node` legacy block; the documentation in §4; the tool/probe
re-derivations; Part 57e. **OUT:** the NV blob fields, the `Node` mirrors and `admin_load` (Slice 10); any change to
the v2 handlers, codec, stores, sessions, actions, controller or carrier (they are the replacement, not the subject —
a diff hunk inside `remote_*.{h,cpp}`, `firmware_remote_client*`, `firmware_remote_executor*`,
`firmware_remote_actions*`, `firmware_admin_*` or `rx_remote_*` is STOP unless it is a comment); new verbs; new corpus
scenarios (design §17: coverage is native/wiring, not a manufactured stream); companion app code. C1: this is a
deletion-plus-docs slice — no refactor of surviving code rides along, no behaviour of a surviving path changes.

## 3. Deletion contract

1. **Whole-feature deletion, fail-closed residue.** After the slice, no symbol from the §1 list exists in
   `lib/`, `src/`, `test/` or `tools/` (grep is the proof); the four verbs fall through the common dispatcher to the
   existing unknown-verb refusal; an incoming `REMOTE_CMD` / `REMOTE_RESP` keeps exactly the 1b/5/8a behaviour
   (capability-owned v2 handlers; the un-owned type reaches the fail-closed internal guard) — nothing here touches
   `radmin_rx_owner`, `rx_remote_cmd_accept` or `rx_remote_resp_client`.
2. **The kept legacy state is inert.** `_admin_pubkey` / `_admin_counter_floor` / `_admin_provisioned`, `admin_load`
   and the three read accessors survive under `MR_FEAT_RADMIN_ACCEPT` with no writer except the boot restore and no
   reader at all (a control greps for a new reader). Slice 10 deletes them with the blob fields in one measured
   NV-version change. `sizeof(Node)` is unchanged on all three ABIs (the ABI probe is the control).
3. **Legacy-rejection proof survives the codec.** `test_remote_codec.cpp` §8 asserts, from a frozen legacy frame
   literal, that no v2 decoder accepts it under any outer type and that nothing retries a failed v2 open as legacy;
   the literal is recorded from the live sealer before deletion and the recording command is in the receipt.
4. **Authority artefacts move together.** Header, ruled table and generated inventory lose the same five rows in the
   same freeze; `tools/check_command_authority.py` passes with its selftests still RED; the inventory generator's
   profile matrix loses the legacy column and its tests lose only the two switch-separation cases.
5. **Nothing else changes.** Every surviving production path is byte-identical in behaviour: corpus 36/36
   byte-identical (the deleted code emits nothing on any corpus path), the six standing probes + deferred-actions at
   their pins except the re-derived legacy counts, the inbox CLIENT arm unchanged, gateway/mobile `Node` unchanged.

## 4. Documentation contract (the "durable protocol docs")

- **`docs/frames.md`** (wire-oriented: fields + byte offsets, no behaviour prose — CLAUDE.md map): replace the two
  `0xA0` / `0xA1` rows' text with "remote-admin v2 RPC body — see the REMOTE_CMD / REMOTE_RESP subsection", and add
  that subsection under "Inner layouts": the control byte (`bits 7..4` opcode, `bits 3..0` slot / sentinel `F`),
  the twelve opcode nibbles, and one byte table per body — authenticated execute (25-B envelope), open execute (9),
  bootstrap request (57), session control ACK / SAFE / FORCE (25), authenticated response (26), open response (10),
  bootstrap response (33), rollover result (34), terminal (`[code][detail…]`, codes `0x00..0x08`), admission result
  (28, R-RA-36) — each **verified against `lib/core/remote_codec.h`'s `kRemoteOverhead*` constants and
  `remote_codec.cpp`'s `remote_layout` / pack order (V1: the code, never the design's prose)**, plus the AAD and
  nonce-preimage field orders, the three KDF labels, and the mobile-wrapper carrier caps (`remote_body_cap`).
- **`docs/protocol.md`**: a new "## 15. Remote administration v2" section (behaviour, mechanisms, rationale — the
  "how it works" home), ~one page, covering: the two capabilities and the board rule (R-RA-17/26/27); the four
  stores and physical-USB roots; bootstrap / session / epoch / rollover; admission (R-RA-35/36) and the terminal
  domain incl. `action_busy` and `scheduled`; deferred actions and the activation budget (R-RA-20/23/37/38/40);
  the 36 refused-by-design rows (R-RA-41); the controller: target book, credentials, exact-byte retry, one resend
  edge and ACK-debt cadence (R-RA-47/48), the wrapper carrier and why the wrapper is mandatory (B413), `-a` and the
  three observations (R-RA-49, B278 §9.2), local USB/BLE delivery and the retained-result rule; what was deleted
  and why (this slice). Link each mechanism to the design section; state the source symbols it lives in.
- **Console help / manual:** `firmware_help.h` loses the four entries (structural pin); no manual doc lists them
  (verified: only historical review/handover documents mention `rcmd`, and history is not rewritten).
- **Companion contract:** the three edits in §1; the `remote` / `remote_carrier` sections are already the contract.
- **Bench:** the 9.9 block is rewritten as "REMOVED in Slice 9" (history kept visible); **Part 57e** is written:
  on a static/gateway board and on a mobile, `help` lists none of `rcmd` / `password` / `unlock` / `lock`; each of
  `rcmd 1 status`, `password x`, `unlock x`, `lock` answers exactly `> parse error` on USB and
  `{"err":"parse","msg":"unknown_cmd"}` over BLE; a v2 `remote` round trip (Part 57c step 1) still works.
- **Historical cross-references** (`docs/2026-07-04-codebase-review-findings.md`, `docs/2026-07-31-agent-handover.md`,
  the register's closed rows): untouched — frozen history, per the repository's rule.

## 5. Allocation (predicted; measured at the gate)

`sizeof(Node)` unchanged on all three ABIs (no field moves; the gate re-pins nothing). Gateway RAM **down** by
`sizeof(meshroute::Identity)` + `g_admin_unlocked` + `g_admin_tx_ctr` + `g_remote_action` + `g_remote_action_at`
(+ alignment); mobile RAM down by the two unconditional `g_remote_action*` globals (+ alignment); flash down on both
(the whole `firmware_remote.cpp`, the KDF, the TLV encoders, the sealed codec). The coder measures and attributes
both deltas by section and symbol; **any RAM or flash INCREASE is STOP-1.** `TimerWheel` and the expiry scan are
untouched. No wire change (R-RA-9: the join/beacon `wire_version` is not this slice's).

## 6. Fence

Production, delete: `src/firmware_remote.{h,cpp}`, `lib/core/admin_auth.{h,cpp}`, `lib/console/console_binary.{h,cpp}`.
Production, edit: `lib/core/node.h` (two gate lines, two method deletions in both arms, comment at `:~1795`),
`lib/core/node_mac.cpp` (two function bodies), `lib/core/node_mac_rx.cpp` (comments only), `lib/core/node.cpp` and
`lib/core/node_carriers.h` (one comment each), `lib/core/mr_features.h` (the four legacy-switch blocks),
`src/fw_main.cpp`, `src/fw_context.h`, `src/firmware_commands.cpp`, `src/firmware_config.{h,cpp}`,
`src/firmware_help.h`, `src/firmware_command_authority.h`, `platformio.ini` (comment). Tests, delete:
`test/test_admin_auth.cpp`, `test/test_console_binary.cpp`. Tests, edit: `test/test_remote_codec.cpp` (§8 literal),
`test/test_node_r3.cpp` (the two legacy cases at `:~6404–6480`: the "REMOTE_CMD no longer stages / legacy response
counted and dropped" assertions are kept as v2-guard proofs re-expressed through `enqueue_data(type=…)`; the
`send_remote_*` calls go), `test/test_command_authority.cpp`, `test/test_node_hashlocate.cpp:~2697` (comment).
Tools, edit: `tools/check_command_authority.py`, `tools/gen_command_inventory.py`, `tools/test_gen_command_inventory.py`,
`tools/probe_console_sink/{probe_main.cpp,ownership.py,structural.py}`, `tools/probe_inbox_verbs/{probe_main.cpp,
transcript_main.cpp}`, `tools/probe_features/{probe_main.cpp,run.sh,ownership.py}`, `tools/probe_ui_model_mutations.py`
(retarget the one control that names the switch; no battery is deleted), `tools/meshroute_lab.py` (per R1). Docs:
`docs/frames.md`, `docs/protocol.md`, `ios-companion/INBOX_SYNC_CONTRACT.md`, `docs/2026-07-31-bench-test-script.md`
(the 9.9 block + Part 57e), the authority table doc and the regenerated inventory doc. **P7:** the two removed
`lib/` TUs are not in `lora-universal-simulator/CMakeLists.txt` (verified) — no simulator edit; if the coder finds any
other user of a removed symbol not listed here, that is a STOP-1 with the grep, not a silent widening. **OUT:** every
file under the v2 owners named in §2.

## 7. Required proofs (every retained decision keeps its control; every deletion is proven by absence)

| Surface | Proof |
| --- | --- |
| Absence | a grep control over `lib/ src/ test/ tools/` for every §1 symbol returns zero; the build of all envs has no `-Wundef`/unused warning from a dangling reader (census at pins) |
| Refusal | native: `dispatch("rcmd 1 status")`, `password x`, `unlock x`, `lock` return unknown-verb; the inbox CLIENT/ACCEPT arms and console-sink probes show the exact USB and BLE refusal lines; help omits the four (structural) |
| v2 untouched | the whole remote-admin native family passes unchanged (session, executor, actions, client, carrier chain); `radmin_rx_owner` matrix and both owned handlers byte-identical (features probe + `ownership.py` re-derived) |
| Legacy rejection | `test_remote_codec.cpp` §8 with the frozen literal; a control that flips one literal byte still rejects; a control that removes the case is RED (the reference's freeze check) |
| Kept state inert | grep control: no reader of `admin_pubkey()` / `admin_provisioned()` / `admin_counter_floor()` remains; `admin_load` is called once (boot); ABI probe: Node 235248/122176/157344 exact |
| Authority | header / table / inventory agree (202 rows), selftests RED, no `legacy` class or family anywhere |
| Allocation | both boards' RAM and flash decrease and are attributed by symbol; xiao one-off reported |
| Docs | every `frames.md` byte table is checked by QA against `remote_codec.h` constants and the pack order; `protocol.md` §15 names the source symbol for each mechanism; the contract's three lines corrected |

## 8. Owner rulings — RULED 2026-09-18 (owner, verbatim: "R1 approved as recommended, record it as R-RA-50")

- **R1 — RULED R-RA-50:** delete `cmd_rcmd` and its sub-parser from `tools/meshroute_lab.py` now (the sub-command
  was already dead: it waits for the `[rcmd <from>]` reply 8a removed); a lab `remote` helper (target-book label +
  credential + the `> remote <id16> …` lines) is a separate tooling item after Part 57c has run on metal.

Everything else is ruled by the design (§17, item 9) and R-RA-6/13/27; no allocation ruling is needed for a
decrease. Nothing is HOLD: the coder source-validates this revision and implements.

## 9. Gate and landing

Full gate on both sides (`lib/core` and `lib/console` change): native wrapper and binary; extended reference
(`--freeze-check --compare`, 94/94; the legacy literal joins the compared set); simulator rebuild and corpus
`--require-anchors` **predicted 36/36 byte-identical** — any stream delta is STOP; ABI probes (Node exact); the six
probes + deferred-actions + BLE-line, default and `--no-neg`, explicit inbox CLIENT arm; tools discovery; inventory
write/bare/check (**202**); authority + selftests; A0; literals; whitespace both repos; census six envs; deterministic
pair gateway then heltec_mobile + the one-off `xiao_mobile`; union S ∪ H from the 61/984 floor (no battery deleted;
re-anchored patterns named in the receipt); the exact `PIN re-synced? YES` line. **STOP:** any surviving reference to a
deleted symbol; any edit inside a v2 owner file beyond a comment; a RAM or flash increase; a Node move; a stream
delta; a new verb or a changed refusal text; a vacuous control left behind. Receipt:
`docs/superpowers/evidence/2026-09-18-radmin-slice9.md`. On PASS QA lands the register (§0; B392's Part 57b and the
57e reservation), design §19.1 row 9, bench Part 57e, tracker, MEMORY, and hands Slice 10 its base.
