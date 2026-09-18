<!-- Independent QA gate: Claude (QA-gate role); coder: Codex; brief: revision 6 (Claude) -->
# Slice 8b — the mobile controller carrier — independent QA gate — 2026-09-18

**Verdict: INDEPENDENT QA PASS.** The 8b implementation, frozen by the coder on 2026-09-18 and **uncommitted** on top of
HEAD `c07b77f` (owner commit `8`), reproduces every figure in the coder receipt
([`2026-09-18-radmin-slice8b.md`](2026-09-18-radmin-slice8b.md) §11) when re-executed by QA. R-RA-46..49 are implemented
as ruled; the corpus is byte-identical on the stock simulator; no resident byte moved (`carrier_ctr` sits in the
pending row's former padding, block 4512 on all three ABIs); gateway and mobile RAM are unchanged. **B408 and B414
close.** B418 (a pre-existing board-UI probe drift) stays open and is verified pre-existing by QA on a scratch export
of the base. **With 8b the remote-admin product path is complete end to end in software:** Parts 57b and 57d flip from
"needs 8b" to runnable, and the new Part 57c is the carrier's own metal check. Slices 9 and 10 remain.

## 1. State gated

| Item | Value |
| --- | --- |
| MeshRoute | HEAD `c07b77f` + the coder's uncommitted implementation; the coder's `frozen-primary-inputs.json` (361 production/test/tool entries) matches the tree entry for entry |
| Simulator | `6585649`, clean — the stock source list already names every core TU 8b touches (P7 satisfied; no CMake change) |
| Brief | revision 6, SHA-256 `875c3160…`, unchanged since the coder's revision-6 preflight PASS |
| Union staging | rsync copy `/home/staszek/mr-s8b-qa-stage`, fenced sources hash-identical before and after |

**Production diff read in full** (`lib/core/node_hashlocate.cpp` — exactly the trailing defaulted `via_home` parameter
and the one `!via_home &&` conjunct on the authoritative-binding arm; `lib/core/node.h` — `NodeRadminClientCarrier`,
`remote_client_submit`, `remote_client_correlation_free()` re-pointed to the E2E ring (B408); `lib/core/node_mac_rx.cpp`
— the carrier beside `radmin_send_response` with the pre-check order registered → TX-queue → E2E ring →
`send_by_hash(…, via_home=true)` / `delegate_send_layer`, admission mapped from `SendDispatch`, and the R-RA-49 E2E-ACK
arm for admitted / replay / already-acknowledged verdicts; `lib/core/remote_client.{h,cpp}` — `carrier_ctr` at offset
118 with a `static_assert`, `RemoteClientSend::correlation_full`, execute-only `-a` in start/send/accounting, one
resend at `hop_count ? gateway_send_giveup_ms : e2e_ack_deadline_ms`, ACK debt at `cascade_requeue_base_ms` doubling to
the cap for `cascade_requeue_max` retries then dormant with one re-attempt per new request, the two pure matchers;
`src/fw_main.cpp` — one call after the `if (mrble::connected())` fanout block inside the push-drain loop, with a
call-scoped `LineSink`, the lookup and `&g_node`; `src/firmware_commands.cpp` — the stub deleted, the Node carrier
bound; `src/firmware_remote_client.{h,cpp}` — parser refusal of `-a` on the rollover forms, the observer with the
same-layer-only binding veto through `remote_client_bind_lookup`, the shared carrier-line formatter, the
`radmin_client_ack_debt` status field; `ios-companion/INBOX_SYNC_CONTRACT.md` — the `remote_carrier` section).

## 2. Instruments QA executed

| Instrument | QA result | Coder receipt |
| --- | --- | --- |
| Native, wrapper then binary | **2970 / 195942 / 0 / 0 skipped** | same |
| Extended reference | 94/94 strict, old 89 identical, comparator controls RED | same |
| Simulator + corpus | stock `lus` rebuilt (`d6b58ce4`-lineage → `0f9ac5ef`); **36/36 validated, 36/36 anchors**, s18 `32afbf11`/269517/0, zero radmin events — byte-identical as predicted | same |
| ABI probes | Node **235248 / 122176 / 157344** unchanged; block 4512; 290 checks 9/9 RED; B278 42 measurements 6/6 RED | same |
| console-sink (B414 repair) | 6 profiles, 720 checks, structural 83, BLE guard 905, 149 controls, 0 unusable; `--no-neg` PASS | same |
| inbox-verbs | ACCEPT 1374 / 60; CLIENT **457 / 68** (now compiling the real carrier and observer); explicit CLIENT arm 457 / 68; `--no-neg` PASS | same |
| firmware-UI | 223 controls, coverage 703/840; `--no-neg` PASS | same |
| custody-USB / BLE-line / features | 27 / 10 (+ the observer's structural checks) · 55 / 12 · 9 cells 121 / 62 + 43 ownership; all `--no-neg` PASS | same |
| deferred-actions | P1 150/151/158/158; remote 416/518/534/464; radio 3160/3485/3689/3695; 40 controls RED; `--no-neg` PASS | same |
| tools discovery | **351 OK, 0 skipped** | same |
| inventory write / bare / check | PASS; **no semantic row changed** — 76 rows moved by `file:line` anchors only (QA compared the diff with anchors stripped) | same |
| authority + selftests; A0; literals; whitespace | PASS; 6 RED; rc 0; rc 0; both repos clean | same |
| warning census | 173/178/177/177/182/182 at pins, zero `-Wswitch`; every RAM cell unchanged | same |
| deterministic board pair | gateway **RAM 204036 (0) / flash 574896 → 575008 (+112) / 288 objects**; mobile **RAM 211772 (0) / flash 1392832 → 1395276 (+2444) / 332 objects** | same |
| one-off `xiao_mobile` | RAM 176604 / flash 700588 | same |
| supplemental `probe_board_ui --no-neg` (B418) | W49/W51/W54 FAIL, wiring 57/60, structural 23/23, 176 controls — **identical on a scratch export of `c07b77f`**: pre-existing, not 8b's | same (base run) |

## 3. Mutation union S ∪ H

H = the 59 8a+8c batteries; S adds `radmin8brx` (18) and `radmin8node` (1) and extends `radmin8client` (57) and
`radmin8verbs` (27). QA ran all **61** fresh from the staging copy: **984 configured / 983 RED / 1 known unusable
(sliceBmac M04, B342) / 0 vacuous**, every worker baseline **2970 / 195942 / 0**, staged fenced sources restored
byte-identical. The three controls the coder repaired mid-slice (`radmin8client` C39/C43, `radmin8brx` X09), the B413
control (`via_home` forced false) and the B416 controls V23–V27 all fired in this fresh run. Per battery:
`union/summary.json`; all logs: `union/all-61-battery-logs.tar.gz`.

## 4. Findings

- **B408** (the `-a` bound read the home-side ring): `remote_client_correlation_free()` now counts free
  `_pending_e2e_acks` rows and the carrier pre-checks `e2e_ack_ring_full()` before an `-a` submit, reporting the typed
  `correlation_full`; both halves controlled → **CLOSED**.
- **B414** (console-sink S45 rejected the call-scoped carrier binding): the allow-list recognises exactly
  `meshroute::NodeRadminClientCarrier carrier(g_node);`, S-C45 stays RED, both probe modes and tools discovery pass
  under QA → **CLOSED**.
- **B392**: the 8a warning was landed at 8a+8c; with the carrier live, **Part 57b is now runnable** — stays OPEN for
  that metal observation only.
- **B418**: reproduced by QA on the candidate and on a scratch export of the base; pre-existing board-UI probe drift,
  outside the six-probe chain and the 8b fence → stays **OPEN / TOOL**, not 8b's.
- **B415** stays parked (pre-existing E2E-ring wildcard ambiguity); the observer's matcher inherits it as documented.
- No new finding. Next free stays **B420**.

## 5. Not independently reproduced (D3)

The coder's per-function flash attribution (gateway `.text` +112 with `send_by_hash` +40 and the admission ACK +56;
mobile `.flash.text` +2284 / `.flash.rodata` +160) — QA reproduced the section, object, RAM and flash totals on its own
builds. The coder's separate `cmp` of every stream against a freshly rebuilt base corpus — QA's anchor validation is the
same byte-identity per scenario. Anything on metal: **Part 57c** (this slice), 57b and 57d are the owner's bench.

## 6. Verdict and landings

**PASS.** Landed by QA: this file + `…-slice8b-qa/`; register B408/B414 closed, B392 → Part 57b runnable, B418 verified
pre-existing, §0 rewritten; design header + item 8b + §19.1 row → PASS; brief §10 PASS block; bench **Part 57c** written
and Parts 57b/57d flipped to RUNNABLE; tracker; MEMORY; ledger completion notes for R-RA-46..49.
**Owner:** commit the MeshRoute freeze (the simulator repo is unchanged). **Next:** Slice 9 (legacy protocol deletion +
durable protocol docs), then Slice 10 (main-NV cleanup, standalone per R-RA-6); the metal backlog gains Part 57c and the
now-runnable 57b/57d.
