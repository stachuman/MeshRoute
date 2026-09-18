<!-- Independent QA gate: Claude (QA-gate role); coder: Codex; brief: revision 4 (Claude) -->
# Slices 8a+8c (paired) — mobile controller core + local USB/BLE delivery — independent QA gate — 2026-09-18

**Verdict: INDEPENDENT QA PASS.** The paired 8a+8c implementation, frozen by the coder on 2026-09-18 and
**uncommitted** on top of HEAD `e3a5fa0` (base `6086152`), reproduces every figure in the coder receipt
([`2026-09-17-radmin-slice8ac.md`](2026-09-17-radmin-slice8ac.md) §7) when re-executed by QA. The R-RA-45
allocation lands exactly (block 4512 B, Node 235248 native / 122176 mobile / 157344 gateway; gateway RAM and
Node unchanged — the control; mobile RAM +4016). **B292 and B312 close.** Two findings are registered and closed:
**B406** (my brief's fence omitted the simulator source list — fixed by QA with one CMake line, rule P7) and
**B407** (the coder's pre-freeze self-review corrections). B392 stays open for its Part 57b metal observation only;
its 8a warning is landed. **Owner action: commit the MeshRoute freeze AND the one-line change in the simulator repo.**

## 1. State gated

| Item | Value |
| --- | --- |
| MeshRoute | HEAD `e3a5fa0` (`Doc update`) + the coder's uncommitted implementation; the coder's freeze inventory (`…-r4-implementation/freeze-inputs.json`, 2726 entries) matched the tree entry for entry before the gate |
| Simulator | `06746a9` + **one uncommitted line** in `CMakeLists.txt` naming `lib/core/remote_client.cpp` (B406, §4); `lus` md5 `d6b58ce4` → `60f42a1a` |
| Brief | revision 4, SHA-256 `0d45d7b7…`, unchanged since the coder's preflight PASS |
| Landed by QA before the chain | the four R-RA-42 rows (`remote`, `remote-ack`, `remote-result`, `remote-retry` = `controller_local`) in the ruled authority table, so the three-artefact checker sees header, table and inventory agree |

**Production diff read in full** (`lib/core/remote_client.{h,cpp}` new, `node.{h,cpp}`, `node_mac.cpp`,
`node_mac_rx.cpp`, `src/firmware_remote_client.{h,cpp}` new, `firmware_commands.{h,cpp}`,
`firmware_command_authority.h`, `firmware_help.h`, `device_ble.h`, `device_rng.h`, `fw_main.cpp`,
`platformio.ini`, `ios-companion/INBOX_SYNC_CONTRACT.md`). The mechanics match the ruled contract:

- `RemoteClientState` is the R-RA-45 block: 4 pending rows (352 B, `local_transport` tag, **no sink pointer**),
  4 session-cache entries (144), 2 assemblies (32), 8 chunks (210), 2 retained results (32), 8 ACK-debt rows (88),
  six saturating u16 counters; `static_assert(sizeof == 4512 && alignof == 8)`; `#if MR_FEAT_RADMIN_CLIENT` on
  the `Node` member. The legacy `Node::RemoteInbound` slot, `take_remote_inbound`, `remote_inbound_stage` and the
  `fw_main` `[rcmd]` drain are gone (only historical comments mention them); `dispatch()` now carries a
  `CommandTransport`.
- The controller seals through the Slice 2 codec against `derive_base`'s mirror; my rerun of the coder's
  independent hashlib+libsodium reference (`controller-reference-qa-run.json`) reproduces the six vectors the
  native suite pins (`test/test_remote_client.cpp:148–151`: base, session key, sealed request). Entropy is
  `mrrng::fill_checked` bound as `client_entropy` (`firmware_commands.cpp:490`); a failed draw refuses the request
  (B312). The only board carrier is `DeviceRemoteCarrier`, whose `submit_request`/`submit_ack` return
  `RemoteClientSend::unavailable` — nothing reaches the radio until 8b, and the corpus carries zero radmin events.
- Local delivery is `RemoteClientDelivery`: USB `> remote <id16> out <body>`, `> remote <id16> <result>` with
  `activation_ms=`/`detail=` when present, `> remote <id16> retained`, `> remote err <reason>`; BLE
  `remote_output`/`remote_terminal`/`remote_retained`/`remote_warning` events through `console::write_event` into a
  245-byte buffer, so one event fits one notification. `device_ble.h` routes the direct dispatch reply through the
  existing chunked `tx_line` (`:223`), the one bounded transport write (R-RA-43). The prep-restart warning sentence
  is in `firmware_remote_client.cpp:131` verbatim (R-RA-38 / B392).

## 2. Instruments QA executed

| Instrument | QA result | Coder receipt |
| --- | --- | --- |
| Native, wrapper then binary | **2947 / 193734 / 0 / 0 skipped** | same |
| Extended reference (`--freeze-check --compare`) | 94/94 strict, old 89 identical, 4 comparator controls RED; controller vectors reproduced 6/6 | same |
| Corpus `--require-anchors` (after the B406 line) | **36/36 validated, 36/36 anchors**, s18 `32afbf11` / 269517 / 0, zero radmin events | same |
| ABI probes | Node **235248 / 122176 / 157344** (R-RA-45 pins); **290 checks 9/9 RED**; B278 42 measurements 6/6 RED | same |
| console-sink | 6 profiles, 720 checks, structural 83, BLE guard 905, 149 controls, 0 unusable; `--no-neg` PASS | same |
| inbox-verbs | ACCEPT **1374** / 60; CLIENT **439** / 64; explicit CLIENT arm 439 / 64; `--no-neg` PASS | same |
| firmware-UI | 223 controls 0 unusable, coverage 703/840; `--no-neg` bare PASS (B350) | same |
| custody-USB / BLE-line / features | 27 / 10 · **55 / 12** (D1 "complete 245..256-byte direct reply survives" under four chunkings; the N6 control reddens D1) · 9 cells 121 checks 62 controls + 43 ownership; all `--no-neg` PASS | same |
| deferred-actions probe | P1 arm 150/151/158/158; remote 416/518/534/464; radio 3160/3485/3689/3695; **40 controls RED**; `--no-neg` PASS (`probe_deferred_actions-results.json`) | same |
| tools unittest discovery | **351 OK, 0 skipped** | same |
| inventory write / bare / check | byte-identical, **208 rows** (+4 verbs) | same |
| authority + selftests; A0; DataType literals; whitespace | PASS after the four rows; 6 RED; rc 0; rc 0; both repos clean | same |
| warning census, six envs | 173/178/177/177/182/182 at pins, zero `-Wswitch`; **RAM unchanged in every ACCEPT cell** (239148 / 239420 / 214364 / 214636), heltec_mobile 207756 → 211772, heltec_v4_mobile 208028 → 212044 | same |
| deterministic board pair | gateway **RAM 204036 (0) / flash 574752 → 574896 (+144) / 288 objects**; mobile **RAM 207756 → 211772 (+4016) / flash 1373604 → 1392832 (+19228) / 332 objects** | same |
| one-off `xiao_mobile` (reported, not gated) | RAM 176604 / flash 696940 | same |

## 3. Mutation union S ∪ H

H = the 56 7b-3 batteries; S = the changed batteries plus three new ones (`radmin8client` 29, `radmin8verbs` 8,
`radmin8rng` 1). QA ran all **59** from the rsync staging tree: **918 configured / 917 RED / 1 known unusable
(sliceBmac M04, B342) / 0 vacuous**, every worker baseline **2947 / 193734 / 0**, staged fenced sources restored
byte-identical (`mut_stage_hashes*.txt`). `radmin7rx` 16 and `radmin5rx` 14 keep their controls effective after
the client-intake change. Per battery: `union/summary.json`; all logs: `union/all-59-battery-logs.tar.gz`.

## 4. Findings

- **B292** (256-byte direct BLE reply written as one 244-byte notification): the direct reply now goes through
  the one chunked `tx_line` writer; the BLE-line probe's D1 control shows a 245..256-byte reply surviving under
  four chunkings and the N6 control reddens it → **CLOSED** (R-RA-43).
- **B312** (HAL entropy cannot report failure): `mrrng::fill_checked` is bound as the controller's `RemoteEntropyFn`
  on every backend and a failed draw refuses the request without publishing an ID; `radmin8rng` RED, native cases
  pass → **CLOSED**.
- **B406 — NEW, CLOSED (QA's own fence omission).** The brief's OUT list forbade simulator edits, but a new
  `lib/core` TU must be named in the simulator's explicit CMake source list; the stock `lus` failed to link the
  frozen tree (`undefined reference to meshroute::remote_client_*`). The coder respected the fence, disclosed the
  gap (receipt §6) and gated its corpus with an archived build-only `target_sources` hook outside the checkout;
  that is not a durable integration. QA added the one line (same shape as the Slice 2/5 lines), rebuilt and re-ran:
  36/36 byte-identical. **Rule P7** lands in CLAUDE.md/AGENTS.md: a brief that adds a `lib/core` TU fences the
  simulator source list in the same slice; a brief that removes a symbol fences every user (B405).
- **B407 — NEW, CLOSED in-slice.** The coder's receipt §7.3 records two recovery defects found and fixed in its
  self-review before the freeze (explicit `remote-result show` not adopting the caller's transport / assembly
  fallback dropping the completed local acceptance; a full ACK-debt table refusing session controls and session
  controls accepting execution-terminal responses), with compiled controls C25–C29 RED. Registered per M1.
- **B392**: the 8a pre-submission warning is implemented verbatim and gated here; stays **OPEN** for the Part 57b
  metal observation only (needs the 8b carrier).
- Next free finding: **B408**.

## 5. Not independently reproduced (D3)

The coder's symbol-level attribution of the mobile RAM delta (Node +4264, legacy `ri` −245, padding −3 = +4016) and
of the flash deltas; its 264-check Python characterization of the eight canonical low-order peers (the production
refusals are covered by the native suite QA ran); its per-function stack frames. QA reproduced the section, object,
RAM and flash totals on its own builds and re-executed the controller reference. No on-air, physical-BLE, flash-wear
or reset-delivery claim is made: **Part 57d** is reserved and metal-pending on 8b.

## 6. Verdict and landings

**PASS.** Landed by QA: this file + `…-slice8ac-qa/`; the four R-RA-42 table rows; the simulator source-list
line; register B292/B312 closed, B406/B407 registered and closed, B392 updated, §0 rewritten (next free B408);
design header + §19.1 row + item 8 bullets; brief §10 PASS block; bench Part 57d reserved; rule P7 in
CLAUDE.md/AGENTS.md + roles-doc addendum; tracker and MEMORY pointers; ledger note.
**Owner:** commit the MeshRoute freeze and `lora-universal-simulator/CMakeLists.txt`.
**Next:** the 8b brief (the only product controller carrier; needs [[B112]] fixed first), then 9, then 10.
