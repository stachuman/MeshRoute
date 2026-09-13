<!-- Independent QA/Author: OpenAI Codex, replacing Claude; production coder: separate Codex session -->
# Remote-admin Slice 7b-2 — independent QA gate — 2026-09-09

**Verdict: PASS — Slice 7b-2 software-complete, uncommitted at `564f460`, 2026-09-09.**
B388's scoped return is independently verified and closed in §8. Exactly three comment lines changed;
all executable/instrument inputs retain the completed full gate's attribution. B378/B379 now close with
that gate and the resolved comment requirement; B387 remains closed. No production repair by QA, owner
ruling or commit. The original HOLD finding and its measurements remain below as history.

## 1. Original full-gate frozen state and independence

- MeshRoute base/HEAD: `564f460a3b755a104f146da70457e5c8c68e99b9`.
- Simulator: `06746a97de5764415d6fcef10b97bca90569b9c7`, clean before/after; no simulator edits.
- Consumed brief: revision 4 of `2026-09-08-radmin-slice7b2-session-open-status.md`, SHA-256
  `0920bb419cf5f33fbedb8fabfa2c9dc9b91c81744727fd69cb21b16253a7200e`.
- Coder receipt: `2026-09-08-radmin-slice7b2.md` §10, SHA-256
  `3872539fb44f8bb57710e64237cd038bb00613377ce3bbe810589c7c405ff7e5`, unchanged by QA.
- Complete input inventory: **1087** tracked/nonignored-untracked inputs, including all dirty QA preparation,
  implementation and untracked reissue evidence. All **1086** non-receipt inputs match the coder's frozen
  manifest (`984bebeb6a063b9d23187f8e86e8ce0d69cc188da73efbc21b2309a4a6138eb0`). QA independently checks
  regular-file bytes/modes and all four symlink targets. A HEAD-only checkout would omit this implementation.
- Actual coder scope is **20** production/test/instrument paths, plus generated inventory and receipt
  (**22 coder paths**). `changed-from-base.txt` also records the preserved QA preparation. No new production,
  native-test or tool file. Codec, literals, `fw_main.cpp`, authority/NV schema and simulator anchors unchanged.

Every measurement here was executed by this QA session. Private root:
`/tmp/mr-qa-s7b2-gate-0phs0ag4`. `frozen/` preserves all inputs; `gate/` runs probes/tools/census;
`mutations/` supplies disposable, worker-owned mutation copies; `measure/` builds exact committed base,
then the final 20-file source overlay at the **same** private paths. No build or mutation touched the shared
checkout. Baseline measurement used identical documentation and fixed board identity. Final simulator uses
a **fresh** build directory, avoiding B385. Normal board environments run gateway then heltec_mobile,
sequentially with jobs 1; census waits until these finish before its own pinned six-environment sequence.

The [retained evidence directory](2026-09-09-radmin-slice7b2-qa/README.md) contains the consumed brief,
input inventory, commands/exits, raw gate logs, mutation selectors/per-battery logs, focused reproductions,
board manifests and read-only artifact attribution. Large pristine board ELFs/payloads remain at the recorded
private paths with hashes. These hashes identify QA's builds; coder artifacts were not substituted.

## 2. Original required correction and scoped return — closed by §8

**B388 — `lib/core/node_mac_rx.cpp:2273–2275`:** the comment claims refused inputs “changed nothing” and
re-arm “to exactly what was already armed.” Current intake increments the appropriate scalar counter;
open intake also releases expired staging/capture pairs **before** its peer/rate refusal decision
(`remote_session.cpp`, `cmd_open_execute`). The unconditional `radmin_expiry_arm()` correctly scans the
remaining rows. The behavior is consistent with the brief; its comment is not.

QA's standalone **15-check** `b388.cpp` reuses established provisioning and calls the real codec, intake and
earliest-deadline scan. Admit source 11 at 1000 ms and source 22 at 2000 ms. At 301000 ms, a new source-22
request is refused as `open_peer_bound`, while source 11's expired pair is wiped; inbound refusal **0→1**,
source 22's complete retained row/capture unchanged, earliest deadline **301000→302000**. This is a real
pure-session reproduction, not a claim of executing the Node timer/radio. The Node caller is verified in source.

Coder correction: describe recomputing the earliest deadline from **current** rows after every classified
intake, including refusals; open intake may have expired an older row before refusing. Remove the promise of
unchanged state/deadline. Preserve behavior and append the new frozen source/report hashes. No new brief,
owner ruling, counter, timer or production repair by QA. Return review is the changed comment and complete
input-preservation comparison; with executable/instrument inputs unchanged, these completed measurements
remain attributable. Any semantic or instrument change requires its appropriate renewed checks.

## 3. Native and real firmware boundary

Fresh `pio test -e native` followed by actual `./.pio/build/native/program`:

| State | Cases | Assertions | Failed | Skipped |
| --- | ---: | ---: | ---: | ---: |
| Committed codec base | 2888 | 172264 | 0 | 0 |
| Frozen 7b-2 | 2909 | 174485 | 0 | 0 |

**PIN re-synced? YES — 2888 + 21 = 2909 cases; 172264 + 2216 new-case assertions + 5 existing-case assertions = 174485.**
Separately executed, filtered XML for the three affected remote test files gives **51/1359→72/3580**;
2 executor +12 Node +7 pure-session cases. Existing full-TX executor adds one assertion; N11's zero-source
boundary adds four. The unaffected remainder is **2837 cases / 170905 assertions**, with no old case removed.
Whole-suite XML is not used over B364; the ordinary executed binary is the complete-suite evidence.

Source review and executed native cases verify:

- Reserved CONTROL admission without borrowing seen capacity; main-loop prepare before epoch install;
  safe live counts, busy **2→1** retry, force abandonment, one eight-byte draw, zero/current draw refusal,
  old/fresh session distinction, no rollback after checked send refusal and bootstrap recovery.
- B379 real full→other-slot rotation→same-key request execution; mutable refusals use ADMISSION_RESULT,
  acknowledged retry uses its exact one-byte protocol-error domain. Unauthenticated failures remain silent.
- B386 real force-producer release with two other-slot survivors: only allowed scheduling ranks compact;
  identity, route, owned output, nonzero replay cursor/pending frame and relative order survive.
- Owned open input/output, exact authority, three target-wide starts per rolling 300000 ms across requesters,
  original deadline/cooldown, no authenticated/bootstrap borrowing, and earliest-row expiry. Empty, exact 1648,
  bound+1, binary and carrier cut-point outputs are separately exercised through the counting fake executor.
- Checked queued/parked ownership, raw-zero success, full queue pacing, original route, same-layer and depth
  2/3/4 return flights, nonzero cursor preservation/recovery on labelled synthetic codec/send failure.
- Five counters: exclusive named deltas, once-per-event accounting, saturation, preserved across ordinary
  invalidation, cleared only by whole-state teardown. Absent source counts inbound once; present zero reaches
  admission then the explicit sender refusal, preserving the specified open/bootstrap/control lifetimes.
- Existing B374/B375 immutable authenticated transcripts, B369 expiry/ACK, ACL actor/private-DM boundaries,
  B372 medium and B376/S22 route discrimination remain exercised.

Native fake execution is **not** real handler proof. The inbox probe compiles actual `firmware_commands.cpp`
and `firmware_inbox.cpp`, drives the real seam/radio, and compares open `status`/`routes` with local output at
one execution snapshot. It checks exact argument-free authority, malformed/privileged refusals, no NV write,
no USB/BLE leak, restored scope, all five distinct/zero/saturated fields and CLIENT absence. Its real-bound
control flight records the eight-byte epoch draw. Paused authenticated/open execution and injected faults are
explicit synthetic fixtures; none qualifies physical entropy or natural handler/RX interleaving.

## 4. Full instruments and mutation union

All controlled probes and both ABI probes pass with **zero new unusable controls**. Each probe's `--no-neg`
execution also succeeds; these diagnostic runs are not substituted for the controlled gate.

| Instrument | Independent measured outcome |
| --- | --- |
| Board ABI | 191 checks; 9/9 controls RED |
| B278 correlation-row ABI | 42 measurements; 6/6 controls RED |
| Console sink | 6 profiles; 720 checks; 83 structural; 905 BLE guard; 6 ownership/3 ownership controls; 149 controls RED |
| Inbox verbs ACCEPT | 1363 checks; 60 controls RED |
| Inbox verbs CLIENT | 384 checks; 45 controls RED |
| Firmware UI | 223 controls RED; controlled real-source rows pass |
| Custody USB | 27 checks; 10 controls RED |
| BLE line | 40 checks; 8 controls RED |
| Features | 9 cells /120 checks; 59 controls RED |
| Full tools discovery | 343 tests, OK, zero skips; independent measured real ELF available |
| Inventory | write, bare and check pass; 204 semantic rows unchanged |
| Authority | default and six selftests pass |
| A0 / DataType literals | both checkers pass |
| Whitespace | both repositories pass |
| Warning census | all six pinned environments pass; see retained raw census log |

`MR_LUS_SRC` explicitly selects the unchanged simulator for private probes/tools. The real-ELF tool test reads
QA's independently measured pristine base gateway artifact under private `.pio-measure/`; no missing-ELF skip.
The tools log also repeats the known unclosed `/dev/null` ResourceWarning; its actual result is OK, not a skipped test.
B350 remains: firmware-UI's `--no-neg` wording is imperfect; only its controlled run qualifies the gate.

QA derives both selectors from the actual harness/source:
**S = 17 changed-source batteries; H = 47 mandatory historical/dependency batteries;
S∩H = 15; S∪H = 49.** All **773** configured patterns match their real target exactly once. Firmware TUs
are guarded by executed real-source probes, not counted as native mutation targets.

**772 RED + 1 known unusable B342.** QA used four workers where the battery has four entries, fewer otherwise.
Every one of **164 worker baselines** independently executes **2909/174485/0**. Each RED has a positive
assertion failure and one pattern match; every worker restores its source by hash. Every battery confirms all
**56 configured target files** unchanged in the parent tree; the final SHA-256 source map also matches.
`sliceBmac` M04 alone stays green (B342), yields the expected nonzero battery exit, and is never counted RED.
The complete names, S/H reasons, pattern counts, sources/hashes, outcomes and log hashes are retained in
`union/selectors.json` and `union-audit.json`; this is not a shorthand subset or a coder-derived total.

The RED increase is independently **712 +36 session/open/control +15 Node RX/sender +9 executor =772**.
Committed codec coverage remains 103 RED. The real probe controls separately attack service/sink bindings,
all five status values, scalar-only ownership and ACCEPT/CLIENT gating.

## 5. Simulator and immutable codec reference

Both fresh Release/Ninja graphs execute **70 actions /64 compiler actions**, including normal and gateway
compilation of `node.cpp`, `node_mac_rx.cpp` and `remote_session.cpp`. **30/64 objects** differ: the session
and Node implementations plus Node-header/layout dependents in both namespaces. Other source inputs are
verified against base/freeze. Executable SHA-256:

- Base: `3826d36ee5b07fdaf2eb4836bfcf81240cb4ace18151fce4e66fe693670e186c`.
- Final: `862241173963b3c30a501849dea9e5e6184227575225aa8e263f0f5c48d1170a`.

Both corpora pass **36/36** live anchors, zero assertion failures and independent manifest validation.
Canonical comparison exits **1 solely because `lus_sha256` differs**; that refusal is retained, manifests
unmodified. QA then compares every actual stream byte and independently hashes each stream against both
validated manifests: **36/36 byte-identical**. BASELINE SHA-256
`71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`;
s18 **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**, zero failures.
No remote traffic in the corpus is promoted to proof of the new remote behavior.

The unchanged independent reference runs under `/home/staszek/mr-slice2-ref/bin/python`:
**87/87 old literals; 89/89 strict arrays; 25 positive and 1718 valid-tag invalid tuples generated; four comparator controls RED**.
A disposable copy corrupting the first new admission byte `0x53→0x52` is rejected with exit 1 and the exact
first-byte mismatch. Production codec/literal source is untouched. This preserves 7b-2-0's reference boundary.

## 6. Deterministic boards and allocation attribution

| Board | RAM base→final | Flash base→final | Object count | Node base→final /alignment |
| --- | ---: | ---: | ---: | --- |
| gateway | 198980→203956 (**+4976**) | 562748→570588 (**+7840**) | 285→285 | 152288→157264 /8 |
| heltec_mobile | 207756→207756 (**0**) | 1372992→1372992 (**0**) | 329→329 | 117912→117912 /8 |

Native Node **225920→230896**, alignment 8. Real host/ARM/Xtensa state assertions measure **8824/8**, +4976;
three `OpenCapture` 1658/2 rows use the approved 1648-byte payloads, aggregate 4974, two new u16 counters,
with existing tail padding accounted. No additional resident pool/timer/adapter. The scalar snapshot contains
five u16 values; status does not copy secret-bearing state. Mobile instantiates none of this target state.

Read-only ELF analysis preserves **28/28 original measurement files**. Gateway `.bss`197972→202948 is
entirely `g_node` +4976; `.data`976 and `.noinit`32 retain size; heap reserve decreases 4976. All **48** changed
`.data` words resolve to the same symbol/offset or literal at relocated addresses; `.ARM.exidx` retains its
same absolute target/unwind entry. `.text`561764→569604 is exactly **+7840**: **+7644 unioned sized-symbol
bytes +196 uncovered string/padding/unsized bytes**. Raw named-symbol sums overcount by 2 because the new
OpenSink D1/D2 destructors alias. The uncovered range includes exactly the five new status-label strings
(143 printable bytes); no removed printable string. Full 189 changed-symbol rows are retained, including
receive+1778, service clone+782, control prepare+688/check+620, next-open+438 and Node control+396.
Small unchanged-Node-method codegen changes follow shifted member offsets and LTO; they are not extra RAM.

All allocated mobile sections and the full symbol table are identical. Raw ELF debug metadata differs;
firmware payload differs at **65 bytes only**, within its embedded ELF digest, XOR checksum and SHA trailer.
QA verifies both checksums and that exact allowed-offset set. Thus linked mobile code/RAM/flash is unchanged;
no raw ELF or complete payload byte-identity claim is made.

Normalized board-warning messages are identical base/final: gateway **18631** each (three pre-existing
vendored CustomLFS reorder warnings, no switch warnings), mobile **177** each (no reorder/switch warnings).
Strip ANSI colors and source line numbers only; retain actual diagnostics. The independent census counts
all warnings in its six OLED environments: **173/178/177/177/182/182**, all exact pins, all zero `-Wswitch`.
Those are separate builds from the gateway/mobile comparison pair; raw census rows identify each environment.

## 7. Findings, attempts and preservation

- **B387 CLOSED:** final bare console pin assignments parse through the unchanged actual strict reader.
  QA restores each offending inline comment only in disposable runner copies; **2/2** reader controls fail
  with the named missing pin. Full tools discovery passes 343/zero skips. This independently closes the coder's
  corrected finding; the earlier coder 322/import-error attempt remains in its receipt. B377 recurred, so QA
  adds D5 and its guideline detail: strict-reader numeric pins stay bare, derivations precede them, run full
  discovery after runner edits. Reader/assertions/counts are not weakened.
- **B388 was the sole HOLD item:** the comment correction in §2 is independently closed by §8. B378/B379
  close with the completed implementation gate and this scoped return; their original measurements remain above.
- QA setup initially compared symlinks using a different hash encoding; after reading the coder's actual
  `SYMLINK:` representation all 1086 match. No real input mismatch. The initial stop is retained.
- Read-only artifact analysis initially omitted zero-size linker symbols and stopped at `.data+424`;
  adding exact `__HeapBase`/`__end__` resolution proves the pointer relocation. No ELF changed.
- Initial warning-message comparison retained ANSI prefixes and produced false message deltas. The corrected
  normalizer strips ANSI/line numbers; both counters are identical. Both analyses remain available.
- Corpus identity refusal, reference corruption and known B342 nonzero exit are intentional, explicitly
  classified outcomes. No new unusable instrument, skipped tools test or production correction is hidden.

The final preservation audit checks all frozen shared inputs before the QA documentation landing, private
source restoration, simulator HEAD/cleanliness, unchanged coder receipt and the original board artifacts.
QA modifies only AGENTS/CODE_GUIDELINES discipline, maintained register/MEMORY/design/brief status,
settled design counter/allocation/admission wording and new QA evidence. No source/test/tool/codec/inventory correction, reset, clean, commit or simulator edit.
Deferred actions stay 7b-3; controller/carrier and metal open/control round trips stay 8b. No new bench part,
entropy qualification or owner ruling is inferred. The scoped return and final closure follow in §8.
The next 7b-3 brief will use the owner's actual committed result.

## 8. B388 scoped return — independent PASS and final closure

**2026-09-09: PASS.** QA reads the actual `node_mac_rx.cpp:2272–2274` correction against
`ReceiveCompletion`, `cmd_open_execute` and the shared scan/caller. It now correctly describes recomputing
from current rows after classified intake, with possible counter changes and expiry before refusal.
No runtime, timer, counter or admission behavior changes.

QA independently reconstructs its own prior post-landing state from the original input inventory, six
recorded QA documentation updates, QA-report hash and all **152** retained artifact files. A fresh live
inventory has **1240** MeshRoute inputs and **285** simulator inputs. Exactly **two** MeshRoute paths differ:
the RX comment and the append-only coder receipt §11. All other **1238** preserve bytes, modes and symlink
targets; both repository HEADs match §1, simulator stays clean, and both whitespace checks pass. The coder's
complete before/after manifests independently match these reconstructed/live inventories.

The RX source remains **3345 lines**. Only lines **2272, 2273, 2274** differ; each is a complete `//` comment
with no line continuation. Substituting the old three lines reconstructs every original byte. The entire
old coder receipt is an unchanged prefix of the final receipt. Frozen SHA-256:

| Input | Before → after |
| --- | --- |
| RX source | `24addc50e6fe1a5a72e92e8e74c73697ae70d0cf4cce8045392dc75fd7108894` → `4c1bee50d31298ba9ffcb0290ea936ae6a6ae309c5e52f8c847e043d74685eb6` |
| Coder receipt | `3872539fb44f8bb57710e64237cd038bb00613377ce3bbe810589c7c405ff7e5` → `994ef94bdc1298b4d11853094bba8e3c672721bd19a3c9f3b744e369646d8a84` |

QA copies the **complete current tracked/untracked state** into a new private snapshot at
`/tmp/mr-qa-b388-return-kwseqbsg/snapshot`, freshly compiles Monocypher and the real session/codec/identity/
DM-crypto sources with the retained `b388.cpp`, then executes it: **15 checks PASS**, exit 0. Refused intake
increments inbound **0→1**, wipes the expired earlier pair, preserves the surviving row/capture and moves
the earliest deadline **301000→302000**. No existing archive/executable is reused for this focused run.
This remains a pure-session/scan proof, not Node timer/radio execution. The coder's earlier omitted-DM-crypto
link failure remains disclosed in its §11; QA's fresh compile/run has no failed attempt.

**The full native/corpus/boards/probes/mutations/tools/census chain was not repeated for this return.**
Section 2 explicitly scopes the review to the comment and complete preservation proof. The unchanged
executable/instrument inputs retain §3–§6's independently measured **2909/174485/0**, **36/36 byte identity**,
**343 tools /zero skips**, **772 RED +known B342**, all ABI/probes/checkers/census, gateway **+4976 RAM /
+7840 flash** and unchanged mobile linked state. These are the prior full-gate results, not newly measured
numbers from the comment return.

[Retained scoped-return evidence](2026-09-09-radmin-slice7b2-b388-qa/README.md) includes the current input
inventories, reconstructed preservation audit, exact comment diff, prior QA report, fresh commands/logs,
source snapshot identity and post-landing integrity. The original full-gate artifact directory is unchanged.
QA updates only this report, register, MEMORY, brief/design status and a labelled closure note in the ruling
ledger; owner wording remains untouched. **B388, B378 and B379 close in place; B387 stays closed.**
No new finding or owner decision. Source, tests, tools, inventory and the final coder receipt are preserved.

**Next:** owner commit; then QA/Author prepares the separate 7b-3 deferred-action brief against that actual
hash for coder source-validation. This closure does not start 7b-3 implementation. Controller/carrier
open/control round trips and on-air flood/recovery remain 8b's metal gate; no new bench part now. B312/B315/
B342/B350/B359/B364 and other standing unrelated limits retain their existing dispositions.
