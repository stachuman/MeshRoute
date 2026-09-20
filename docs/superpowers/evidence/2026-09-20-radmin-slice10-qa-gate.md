<!-- QA/Author: Claude; production coder: Codex; owner rules and commits -->
# Slice 10 — the standalone main-NV cleanup (R-RA-6) — independent QA gate — 2026-09-20

**Verdict: INDEPENDENT QA PASS.** The Slice 10 implementation, frozen by the coder on 2026-09-19 (brief revision 3,
SHA-256 `5eab4af5…`), reproduces every number the coder reported, on the stock tools, from a tree that matched the
coder's freeze inventory file-for-file. The remote-admin v2 arc is **software-complete**: every replacement bullet of
design §17 is executed, including the last one — the three legacy `/mrcfg` fields, their `Node` mirrors and the boot
`admin_load` are gone in one measured NV-version change (v25 → v26, 280 → 240 bytes), with every re-pin measured and
attributed. The four remote-admin stores are proven untouched by the reseed path on the real ESP32 NV arm. **Bench
Part 57f (the on-metal migration) is NOT RUN and stays the owner's.** One tool residue is owed (B434, a one-literal
pin re-sync in the mutation harness); one new low tool finding is registered (B435). No production defect was found.

## 1. State gated

- MeshRoute HEAD `ab3d9c5` (owner commit "slice 8" = the production/test/doc half of the freeze over base `4ad9c34`)
  plus the seven uncommitted instrument files (`tools/probe_board_abi.py`, `tools/probe_console_sink/{negctl.py,run.sh,structural.py}`,
  `tools/probe_features/ownership.py`, `tools/probe_inbox_verbs/{probe_main.cpp,run.sh}`) = the coder's freeze:
  all 28 files QA checked (the 21 candidate files vs base + 7 controls such as `platformio.ini`, `firmware_config.cpp`,
  `node.cpp`, the mutation harness, the two discovery tests, `BASELINE.md`) match `…-r3-freeze/freeze-inputs.json`
  byte-for-byte; the candidate-vs-base file set is exactly the brief §5 fence (no `platformio.ini`, no
  `firmware_config.cpp`, no new TU, no simulator edit). Simulator `6585649`, clean. Tree unchanged after the gate.
- **Production diff read in full** (`device_nv.h`, `firmware_config.h`, `fw_main.cpp`, `node.h`): the three fields
  and the v20 comment leave; `kVersion` = `kVersionMinLoad` = 26; asserts 240 / 8 / 236; the inline
  `nv_boot_report_console(Print&, bool)` prints `mrnv::kVersion`, never a literal; `fw_main` captures the load bool
  once, applies the loaded config, then makes the one report call with `mrcon`, before the ACCEPT store gate; the
  `admin_load` call and both `Node` blocks (accessors, stubs, fields) are gone; the two history comments are factual.
  Tests and instruments read in full: the custody literal, the two service-test sentinel retargets, the NV test's
  v26/floor/old-size cases, the ABI pins, the census entries (base ten sites / five ACCEPT → eight / three, QA-parsed),
  S84 + its three controls, the inbox A7 re-fixture + A7-9..A7-19 + A10-C1, the runner pins with comment lines.
- Layout measured natively by QA against the frozen headers: `Blob` 240 / align 8 / `remote_action_activation_ms`
  @236 (`intro_attach` 162, `team_ch_pub` 163, `team_key_team_id` 228); `Node` 235208.

## 2. Instruments QA executed (stock tools; `chain.sh` + `chain_tail.sh`)

| Instrument | Result |
| --- | --- |
| Native wrapper, then the binary | **2950 cases / 195770 assertions / 0 failed / 0 skipped** |
| Extended reference (7b-3-0) | strict 94/94, old-89 identity 89/89, 5/5 comparator controls RED |
| Slice-9 reference | 94 unchanged arrays + 1 frozen 40-byte legacy frame, 3/3 legacy controls RED |
| Simulator rebuild (`node.h` changed) | 34 build actions, `lus` md5 `b3f5f0e7` → `18075be7` |
| Corpus `--require-anchors` | **36/36 streams, 36/36 anchors**; s18 `32afbf11` / 269517 events / 0 assertion failures — byte-identical, as predicted |
| Board ABI / B278 row ABI | Node **235208 / 122176 / 157304**, 290 checks, 9/9 controls RED; 42 measurements, 6/6 controls RED |
| Console sink (default + `--no-neg`) | 6 profiles / 720 / **84 structural** / 905 BLE / 6 ownership / 3 / **152 controls**, 0 unusable — S84 present, N1–N3 RED |
| Inbox verbs (both arms, `--no-neg`, explicit CLIENT) | **ACCEPT 1394 checks / 61 controls; CLIENT 477 / 69**, 0 failed, 0 unusable — A7-9..A7-19 execute the real ESP32 NV arm: a 280-byte v25 image refused, both schema goldens from the real formatter, the four store slots byte-identical, one write for the one save; A10-C1 (wrong slot) RED |
| Firmware UI / custody USB / BLE line | 404 / 839 checks, 223 controls; 27 / 10; 55 / 12 — all PASS at unchanged pins |
| Feature matrix (default + `--no-neg`) | 9 cells / 112 checks / 58 controls, ownership subset 43, 0 unusable — the eight-site `node.h` census accepted |
| Deferred actions (default + `--no-neg`) | 150 / 151 / 158 / 158 checks, 39 transcripts each, 40 controls RED |
| Board-UI supplemental `--no-neg` | exactly W49 / W51 / W54 fail (B418, pre-existing); 23/23 structural, 56/59 wiring, 171 controls RED |
| Absence (slice-10 symbols) | 5 non-comment grep hits, all non-executable: the `node.h` size-assert's history comment, the `kVersion` comment, S34/S45 negative regexes ×3 |
| Absence (slice-9 regression) | 5 hits, all non-executable (structural.py negative regexes ×3, generator prose ×1, `node.h` history comment ×1) |
| Inventory `--check` FIRST (B433 ordering), discovery, `--write`, bare | check PASS **197**; **349 tests / OK / 0 skipped** (688 s); write produced no diff; bare PASS |
| Authority + selftests / A0 / data-type literals / whitespace | PASS; 6/6 selftests RED; PASS; PASS (21 values); both repos clean |
| Warning census | **171 / 175 / 175 / 175 / 179 / 179**, 0 `-Wswitch` — no re-pin, as predicted |
| Stock board pair (gateway then heltec_mobile, jobs=2) | **gateway 203740 RAM / 572240 flash (285 objects); heltec_mobile 211724 / 1394704 (329)** — the coder's numbers exactly |
| XIAO one-off | **176556 RAM / 699548 flash** (RAM −40, flash unchanged) |
| RAM attribution vs the Slice 9 pair (same base tree, `nm -S`, incl. weak objects) | gateway: `g_node` 157344 → 157304 and `mrnv::save(...)::cur` 280 → 240 = **−80**; heltec_mobile: `cur` only = **−40**; no other RAM symbol moves |
| Part 57f | present in the bench script, metal-pending, with the exact boot lines |

Deltas vs the Slice 9 baseline: gateway RAM −80 / flash +16; heltec_mobile RAM −40 / flash +184; XIAO RAM −40 /
flash 0. Every RAM byte is attributed by symbol; the flash increase is the one boot line and its call (brief §4).
**Run note (D3):** the harness stopped the first chain run at the census after 48 minutes; every section before it
had already printed its result. The census, the board pair and XIAO were re-run to completion in a detached tail on
the identical tree (`git status` unchanged, the same seven modified instrument files). Nothing was inherited from an
interrupted section.

## 3. Mutation union S ∪ H (`mut.sh`, rsync stage `/home/staszek/mr-s10-qa-stage`, `--workers=3`)

**61 batteries / 983 RED / 1 known unusable (B342, `sliceBmac` M04) / 984 configured / 0 vacuous.** Every worker
derived its own clean baseline 2950 / 195770 / 0; the stage hashes are identical before and after. Honest sequence:
the first pass (60 batteries, run concurrently with the full chain) reported 914 RED / 2 unusable — the extra one
being `radmin5session` S15 labelled "does not compile / did not run"; `teamkeyring` (interrupted with the chain) then
ran to 68 / 0 in the detached tail; a solo re-run of `radmin5session` on the same stage gave **25 RED / 0 unusable**
(S15 RED), matching the Slice 9 gate and the coder's freeze. The transient is not reproduced solo and its target
file (`remote_session.cpp`) is untouched by Slice 10; registered as B435. The harness printed its B217 advisory
banner on every battery (its own `PIN_ASSERTS` literal is stale at 195768; results derive from the tree) — B434.

## 4. Findings

- **B430 / B431 / B432 — CLOSED at this PASS.** The three brief-fence omissions (console runner pins; the custody
  Node pin; the feature-guard census) are landed exactly as folded; QA re-derived each (84/152; 235208; 8 sites).
- **B433 — CLOSED (verified).** QA's chain runs `--check` before discovery; the tracked inventory equals fresh
  generation on the frozen tree; discovery 349 OK.
- **B434 — OPEN / TOOL, one literal owed.** `tools/probe_ui_model_mutations.py` `PIN_CASES, PIN_ASSERTS = 2950, 195768`
  must become `2950, 195770` with its comment line (the Slice 3/4/5/9 precedent re-synced it in-slice; revision 3
  fenced the harness only for a moved pattern — QA's omission of the same literal-pin class as B430–B432). Non-gating
  by the B217 ruling; gate for the re-sync = a union re-run with no banner + tools discovery.
- **B435 — OPEN / TOOL, low (new).** The harness reports a mutant that failed to build and one that did not run under
  one label and keeps no per-entry compiler output (scratch removed on exit unless `MR_MUT_KEEP_SCRATCH=1`), so a
  transient under load costs a re-run to attribute. Pair with B434 for the next tool dispatch.
- B418 (board-UI W49/W51/W54) re-verified pre-existing; B392 / B404 / B415 unchanged.

## 5. Not independently reproduced (D3)

Bench **Part 57f** (real flash, the unchanged `nv_load_stamped` reseed, both board ABIs) and the earlier metal
parts 57b–57e: metal is the owner's. Nothing in the software gate was skipped.

## 6. Verdict and landings

**PASS.** Landed by QA: this file + `…-slice10-qa/` (chain + tail + union scripts and logs, RAM symbol lists, the
native layout measure); register §0 rewritten (arc closing note), B430–B433 closed in place, B434 set OPEN/TOOL,
B435 registered; design header + item 10 + §19.1 row 10; the rulings ledger's arc-complete note; tracker; MEMORY.
**Owner:** commit the seven instrument files and the QA landings (the simulator repo is unchanged); Part 57f on
metal when convenient. **Next:** no further slice — the remote-admin v2 arc is software-complete. Remaining: the
owner's metal backlog (Parts 54 / 55a / 55b / 56 / 57a–57f / 58 / 59 / 61 / 62 / 63) and the B434 + B435 tool
dispatch (one literal + a harness label split; no product change).
