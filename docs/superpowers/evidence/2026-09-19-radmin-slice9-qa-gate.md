<!-- Independent QA gate: Claude (QA-gate role); coder: Codex; brief: revision 5 (Claude) -->
# Slice 9 — legacy protocol deletion and the durable protocol docs — independent QA gate — 2026-09-19

**Verdict: INDEPENDENT QA PASS.** The Slice 9 implementation, frozen by the coder on 2026-09-19 and **uncommitted** on
top of HEAD `84edd3e` (owner commit `part 8`), reproduces every figure in the coder receipt
([`2026-09-18-radmin-slice9.md`](2026-09-18-radmin-slice9.md) §9–§11) when re-executed by QA with the stock tools.
The whole legacy remote-management mechanism is gone with no executable residue; the corpus is byte-identical;
`sizeof(Node)` is unchanged on all three ABIs; RAM and flash are DOWN on both boards; the warning census sits at its
derived lower pins; the inventory has 197 rows and the three authority artefacts agree. **B425 and B428 close; B429
(an author typo) is corrected here.** B418 stays open as pre-existing tool drift. Slice 10 (the atomic NV cleanup)
is the last slice of the arc.

## 1. State gated

| Item | Value |
| --- | --- |
| MeshRoute | HEAD `84edd3e` + the coder's uncommitted implementation, including eight tracked deletions; the coder's `frozen-primary-inputs.json` (361 records) matches the tree: 353 files by hash, 8 deletion markers absent on disk |
| Simulator | `6585649`, clean — neither removed `lib/` TU was in its source list (P7 on the removal side, verified) |
| Brief | revision 5, SHA-256 `6eb070f1…`, unchanged since the coder's revision-5 source-validation PASS |
| Union staging | rsync copy `/home/staszek/mr-s9-qa-stage`, fenced sources hash-identical before and after |

**Production diff read in full.** Deleted: `src/firmware_remote.{h,cpp}`, `lib/core/admin_auth.{h,cpp}`,
`lib/console/console_binary.{h,cpp}`, `test/test_admin_auth.cpp`, `test/test_console_binary.cpp`. Edited exactly as the
brief pins: `lib/core/node.h` (the legacy block re-gated on `MR_FEAT_RADMIN_ACCEPT`, `admin_set_pubkey` and
`admin_counter_check_advance` removed from both arms, `send_remote_*` declarations gone), `lib/core/node_mac.cpp` (the
two `app_dm=false` senders gone), `lib/core/mr_features.h` (the mobile override, the default and the agreement
`#error` gone), `src/fw_main.cpp` / `src/fw_context.h` (the admin globals, the legacy deferred action and its loop
consumer, the BLE `rcmd` arm, the usings/includes, `fw_wdt_feed`), `src/firmware_commands.cpp` /
`src/firmware_config.{h,cpp}` / `src/firmware_help.h` (the four verbs and `handle_password`),
`src/firmware_command_authority.h` (six rows and `CommandClass::legacy`), `platformio.ini` (comments),
`test/test_remote_codec.cpp` (§8 keeps its v2-rejection loop over the frozen 40-byte `kRefLegacySealed`),
`test/test_node_r3.cpp` (typed origination through the existing `test_do_send_typed` seam), tools per §6, and the
documentation: `docs/frames.md`'s new REMOTE_CMD / REMOTE_RESP subsection — its twelve opcode pairs, nine envelope
tables (25 / 9 / 57 / 25 / 26 / 10 / 33 / 34 / 28) and terminal codes `00..08` **agree with `remote_codec.h`'s
`kRemoteOverhead*` constants and `kRemoteTerminalMax`** (QA compared each number), `docs/protocol.md` §15 (with the
"twelve = eleven distinct + the deleted alias" precision), the companion contract (Asks 1/3 superseded), the bench
(9.9 REMOVED, Part 57e written) and the paired §B87 warning record.

## 2. Instruments QA executed

| Instrument | QA result | Coder receipt |
| --- | --- | --- |
| Native, wrapper then binary | **2950 / 195768 / 0 / 0 skipped** (−20 cases / −174 assertions: the two deleted test files + the retired cases) | same |
| Extended reference (7b-3-0) | 94/94 strict, old 89 identical, comparator controls RED | same |
| Slice-9 reference (new) | **PASS: 94 unchanged arrays + 1 frozen legacy frame**; selftest controls RED | same |
| Simulator + corpus | stock `lus` rebuilt (`b3f5f0e7`); **36/36 validated, 36/36 anchors**, s18 `32afbf11`/269517/0 | same |
| ABI probes | Node **235248 / 122176 / 157344** unchanged; 290 checks 9/9 RED; B278 42 measurements 6/6 RED | same |
| console-sink | 6 profiles, 720 checks, structural 83, BLE guard 905, 149 controls, 0 unusable; ownership at the re-derived pins; `--no-neg` PASS | same |
| inbox-verbs | ACCEPT 1374 / 60; CLIENT 457 / 68; explicit CLIENT arm 457 / 68; `--no-neg` PASS | same |
| firmware-UI | 223 controls, coverage 703/840; `--no-neg` PASS | same |
| custody-USB / BLE-line / features | 27 / 10 · 55 / 12 · 9 cells **112** checks **58** controls + 43 ownership (B428's 87/15 arithmetic); all `--no-neg` PASS | same |
| deferred-actions | P1 150/151/158/158; remote 416/518/534/464; radio 3160/3485/3689/3695; 40 controls RED; **scheduled-scope census 11** (B423); `--no-neg` PASS | same |
| supplemental `probe_board_ui --no-neg` | wiring 56/59, structural 23/23, 171 controls; failures **exactly W49 / W51 / W54** (B418; `CFG_NOTIFY_SITES=6`) | same |
| absence (executable uses of every deleted symbol) | **zero**: 44 raw / 32 non-comment hits, all classified — 27 are the live 7a global `g_remote_action_activation_ms` matched by the `g_remote_action` stem, 3 are `structural.py` S34/S45's retained forbidden-symbol regexes, 1 is the `node.h` layout ledger's trailing comment, 1 is a historical docstring in `gen_command_inventory.py` (`absence_noncomment.txt`) | same (scoped audit) |
| tools discovery | **349 OK, 0 skipped** (351 − the two retired switch-separation cases) | same |
| inventory write / bare / check | PASS, **197 rows** (208 − 11) | same |
| authority + selftests; A0; literals; whitespace | PASS at 197 with no `legacy` class; 6 RED; rc 0; rc 0; both repos clean | same |
| warning census | **171 / 175 / 175 / 175 / 179 / 179** at the re-derived pins, zero `-Wswitch`; objects 329/330 (−3 per env) | same |
| deterministic board pair (stock `measure_board.py`, no wrapper, eight deletion markers in the manifests) | gateway **RAM 204036 → 203820 (−216) / flash 575008 → 572224 (−2784) / 285 objects**; mobile **RAM 211772 → 211764 (−8) / flash 1395276 → 1394520 (−756) / 329 objects** | same |
| one-off `xiao_mobile` | RAM 176596 / flash 699548 (−8 / −1040) | same |

## 3. Mutation union S ∪ H

All **61** batteries of the 8b floor, re-anchored where the deletion moved a pattern: **984 configured / 983 RED /
1 known unusable (sliceBmac M04, B342) / 0 vacuous**, every worker baseline **2950 / 195768 / 0**, staged fenced
sources restored byte-identical. Per battery: `union/summary.json`; all logs: `union/all-61-battery-logs.tar.gz`.

## 4. Findings

- **B425** (board tool vs unstaged tracked deletions): the stock `measure_board.py` measured the frozen tree with its
  eight deletions recorded as manifest markers; its unit controls (deletion arm removed, missing input accepted, marker
  omitted) are RED → **CLOSED**.
- **B428** (feature-test pins 87 / 15): reproduced through the real ownership instrument (112 checks / 58 controls) →
  **CLOSED**.
- **B429** (author typo, brief rev 5 §1 "the two V4 envs"): the native-USB `-Wcpp` removal applies to all three
  V4-family environments (`heltec_v4`, `heltec_v4_mobile`, `gateway_heltec_v4`, each −3); the table was right, the prose
  wrong; corrected in the brief's §10 → **CLOSED**.
- **B418** (board-UI probe W49/W51/W54): unchanged, pre-existing, verified again on this tree → stays **OPEN / TOOL**.
- **B392** (Part 57b metal), **B404**, **B415** parked/open as before. No new finding. Next free stays **B430**.

## 5. Not independently reproduced (D3)

The coder's per-symbol flash attribution (gateway `.text` −2784 with −2046 in named symbols; mobile `.flash.text` −484
and `.flash.rodata` −272) — QA reproduced the section, object, RAM and flash totals. The coder's warning-multiset A/B
per environment — QA reproduced the six final counts and the zero `-Wswitch`. Part 57e is the owner's bench.

## 6. Verdict and landings

**PASS.** Landed by QA: this file + `…-slice9-qa/`; register B425/B428/B429 closed, B418 re-verified, §0 rewritten;
design header + item 9 + §19.1 row → PASS, and the "twelve policy rows" precision in §13 and §19 item 7; brief §10
PASS block; bench Part 57e → software-complete / metal-pending; tracker; MEMORY; ledger R-RA-50 completion note.
**Owner:** commit the MeshRoute freeze (the simulator repo is unchanged). **Next:** the Slice 10 brief — the standalone
main-NV cleanup (R-RA-6): drop `admin_pubkey` / `admin_counter_floor` / `admin_provisioned` from `/mrcfg`, the three
`Node` mirrors and the boot `admin_load`, one measured NV-version change, Part 57f.
