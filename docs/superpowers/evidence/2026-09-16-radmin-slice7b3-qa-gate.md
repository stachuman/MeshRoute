<!-- Independent QA gate: Claude (QA-gate role); coder: Codex; brief: revision 5 (Claude refresh of Codex revisions 1–4) -->
# Slice 7b-3 — deferred actions — independent QA gate — 2026-09-16

**Verdict: INDEPENDENT QA PASS.** The 7b-3 implementation, frozen by the coder and committed by the owner as
**`6086152`** (`7b-3`), reproduces every figure in the coder receipt
([`2026-09-13-radmin-slice7b3.md`](2026-09-13-radmin-slice7b3.md), section "Revision 5 / R-RA-41 resume —
implementation freeze") when re-executed by QA. The R-RA-40 allocation lands exactly (+80 B, Node 230976 /
157344 / 117912, gateway linked RAM +80, mobile unchanged). **B389, B390 and B401 close.** B392 stays open for
its metal and controller obligations. With R-RA-41 the disruptive remote-admin arc is complete.

## 1. State gated

| Item | Value |
| --- | --- |
| MeshRoute | owner commit `6086152b97b5934971d231a345b9939d5f2db1e9`, clean tree; the coder's freeze inventory (1636 entries, `…-r5-coder-freeze/inputs-at-freeze-before-receipt.json`) matches the commit for every entry except the receipt file itself, written after the inventory |
| Simulator | `06746a97de5764415d6fcef10b97bca90569b9c7`, clean; `lus` md5 `34140fc1` → `d6b58ce4` after a 36-action rebuild (session, Node and MAC RX objects recompiled) |
| Brief | revision 5 with B402 folded, SHA-256 `f1d38f86…`, re-pinned after the R-RA-41 wording change |

**Production diff read in full** (`lib/core/remote_session.{h,cpp}`, `node.{h,cpp}`, `node_mac_rx.cpp`,
`src/firmware_remote_actions.{h,cpp}` new, `firmware_remote_executor.h`, `firmware_commands.{h,cpp}`,
`fw_main.cpp`, `platformio.ini`). The lifecycle matches the ruled contract:

- `DeferredActionRecord` 40/8 with canonical offsets (identity u64/u64/u32/u8/u8, clock u64/u32, opaque
  `kind`/`backend`, `phase`, `trigger`, explicit reserved bytes); `TranscriptHeader` 24 → 32 with its own
  `activation_ms`; two diagnostic bytes; `RemoteSessionState` 8824 → 8904, all `static_assert`ed. Core never
  interprets the plan bytes; the one checked conversion (`remote_action_pack/unpack`, underlying-type
  `static_assert`, range-checked both ways, corrupt ⇒ `none`) is firmware's (B402 rule).
- Reservation before completion (`remote_action_reserve`: executing seen row, capturing transcript, ≥ 5-byte
  terminal capacity, non-zero kind and delay; occupied row ⇒ `action_busy`). Completion freezes the delay into
  the header and moves the row to `prepared`; a non-`scheduled` completion after reservation clears the row.
  The terminal plaintext is `[0x01][delay LE32]` built from the header only, so replay after consumption is
  byte-identical.
- Arming only on the sender's first checked ownership (`queued`/`parked`) of the terminal frame, once; the
  deadline joins the shared earliest-deadline scan; `due` leaves the scan, so no zero-delay re-arm. Deadline
  or an authenticated ACK for the same slot/epoch/request/source marks `due`; a premature ACK is refused
  without releasing the promise; ordinary ACK release of the transcript is unchanged.
- The main loop is the sole consumer: `remote_action_service_once` runs at the top of `mesh_service_once`,
  before the halt block, takes the row with the live clock (so `due` is detected even when the halted block
  stops the timer), clears the resident and transfer copies, then applies through P1's typed effects with a
  non-forwarding sink and a call-scoped observer. Rollover sees preparing/armed/due as `executing`; slot and
  root invalidation clear only an unarmed row. The 36 R-RA-41 rows reach `remote_action_admit` as
  `unsupported` and return the retained typed `refused`; no disruptive handler is called under a remote context.
- B401's include comment is back on the `fw_context.h` line.

## 2. Instruments QA executed

| Instrument | QA result | Coder receipt |
| --- | --- | --- |
| Native, wrapper then binary | **2931 / 189998 / 0 / 0 skipped** | same |
| Extended reference | 94/94, old 89 identical, 5 comparator controls RED | same |
| Corpus `--require-anchors` | **36/36 validated, 36/36 anchors**, s18 `32afbf11` / 269517 / 0, zero radmin events | same |
| ABI probes | Node **230976 / 117912 / 157344** (R-RA-40 pins); 218 checks 9/9 RED; B278 42 measurements 6/6 RED | same |
| console-sink | 6 profiles, 720 checks, structural 83, BLE guard 905, 149 controls, 0 unusable; `--no-neg` PASS | same |
| inbox-verbs | ACCEPT **1374** / 60 controls; CLIENT 384 / 45; explicit CLIENT arm 384 / 45; `--no-neg` PASS | same |
| firmware-UI | 223 controls 0 unusable, coverage 703/840; `--no-neg` bare PASS (B350) | same |
| custody-USB / BLE-line / features | 27 / 10 · 40 / 8 · 9 cells **122** checks **62** controls + **43** ownership; all `--no-neg` PASS | same |
| **deferred-actions probe, extended** | P1 arm 150/151/158/158 (39 transcripts each); remote arm **416/518/534/464**; radio arm **3160/3485/3689/3695**; **40 controls compile and fail an executed assertion**; `--no-neg` PASS (`probe_deferred_actions-results.json`) | same |
| tools unittest discovery | **351 OK, 0 skipped** | same |
| inventory write / bare / check | byte-identical, **204 rows** | same |
| authority + selftests; A0; DataType literals; whitespace | PASS; 6 RED; rc 0; rc 0; both repos clean | same |
| warning census, six envs | 173/178/177/177/182/182 at pins, zero `-Wswitch`; RAM **+80 in every ACCEPT cell** (gateway_heltec 239068→239148, gateway_heltec_v4 239340→239420, heltec_v3 214284→214364, heltec_v4 214556→214636), both mobile cells unchanged, +1 object everywhere | same |
| deterministic board pair | gateway **RAM 203956 → 204036 (+80) / flash 568220 → 574752 (+6532) / 286 objects**; mobile **RAM 207756 (0) / flash 1373576 → 1373604 (+28) / 330 objects** | same |

## 3. Mutation union S ∪ H

H = the 53 P1 batteries; S = the changed configured batteries plus the three new ones. QA ran all **56**:
**880 configured / 879 RED / 1 known unusable (sliceBmac M04, B342) / 0 vacuous**, every worker baseline
**2931 / 189998 / 0**, staged fenced sources restored byte-identical. New batteries: `radmin73action` 35,
`radmin73node` 5, `radmin73convert` 10, all RED; `radmin7rx` 16 (13 + the three checked-ownership controls);
`radmin5rx` 14 with X09 still effective after the expiry-arm change. Per battery: `union/summary.json`;
all logs: `union/all-56-battery-logs.tar.gz`.

## 4. Findings

- **B389** (allocation): implemented exactly as R-RA-40 rules, layouts asserted, Node pins measured, gateway
  linked RAM +80 entirely `g_node`, mobile Node and RAM unchanged → **CLOSED**.
- **B390** (one promise / typed conflict / rollover / release): proven by the new core batteries, the probe's
  remote and radio arms and the native cases → **CLOSED**.
- **B401** (displaced include comment): fixed in this slice's `fw_main.cpp` touch, source readers rerun → **CLOSED**.
- **B392** (prep-restart lockout): the schedulable path, the lockout and the local-restart recovery are
  implemented and proven on host fakes; stays **OPEN** for the Part 57b metal observation and the 8a warning.
- No new finding. Next free stays **B403**.

## 5. Not independently reproduced (D3)

The coder's symbol-level flash attribution (gateway `.text` +6528 by symbol-extent union, `.data` +4 / `.bss`
+76) and its per-function stack frames. QA reproduced the section, object, RAM and flash totals on its own
builds; the coder's pristine ELFs and `.su` files stay in its freeze archive. Part 57b (real reboot, halt,
DFU, OTA, erase, fault effects, radio timing) is metal and needs the 8b controller carrier to drive it.

## 6. Verdict and landings

**PASS.** Landed by QA: this file + `…-slice7b3-qa/`; register B389/B390/B401 closed, B392 updated, §0
rewritten; design §19.1 row 7b-3 → PASS and item 7 bullet; brief §9 PASS block; bench Part 57b →
software-complete / metal-pending on 8b; tracker and MEMORY pointers; ledger note. **The disruptive
remote-admin arc is complete** (12 deferred-action rows; 36 rows refused by design under R-RA-41).
**Next:** the paired 8a+8c brief (controller state/crypto + local USB/BLE delivery, one gate), then 8b, 9, 10.
