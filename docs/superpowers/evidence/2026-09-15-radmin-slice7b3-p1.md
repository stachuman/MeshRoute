<!-- Coder source-validation: OpenAI Codex, assigned by owner 2026-09-15 -->
# Slice 7b-3-P1 — coder receipt — 2026-09-15

**Current implementation/gate checkpoint: §§5–11. Sections 1–4 preserve the earlier preflight STOP and are historical.**

## 1. Source-validation disposition: STOP-1, before implementation

The owner assigned this session the P1 coder task, authorized the existing uncommitted QA preparation,
required typed build-support reporting without local changes, and removed commits as progress gates.
Independent QA still follows a complete implementation freeze; the behavior brief then refreshes at that
QA-passed frozen base. R-RA-40 is recorded and binding for later behavior, with zero resident state in P1.
No further allocation permission is requested here.

Two preflight discrepancies were reported before any repository edit:

1. **Base identity:** the brief and dispatch name `ac5f9a592065d08e7cc8c06ef395e79891d41b34`; actual HEAD is
   **`c591721c2e09bb4e7583f49e458da9e5155cf822`**. Its sole intervening commit changes 60 documentation paths;
   production/test/tool/profile/source inputs remain identical to ac5f9a5. The seven uncommitted files below
   are accepted as permitted preparation, not treated as unexplained dirty work. Nevertheless the specified
   commit pin differs. Owner instruction §5 and the roles document's September-15 override expressly retain
   mismatch→STOP. No reset, checkout repair or silent re-pin was performed. QA can reconcile the brief to the
   observed hash plus this complete inventory without requiring another commit.
2. **B399 — factory-reset anchor wording:** brief §1 says “warnings precede factory_erase/reboot”. At
   `src/firmware_commands.cpp:1083–1086`, the exact order is both inbox wipes → conditional inbox warning →
   NV factory_erase → conditional NV warning → reboot. The NV warning follows the operation it reports.
   Proposed comment-only fold-in: “Both inbox wipes run independently; their conditional warning precedes
   NV factory_erase. The conditional NV-failure warning follows that call. Reboot follows both checks.”
   Existing behavior must stay unchanged; no production correction or owner policy decision is proposed.

Implementation/full-gate dispatch remains stopped pending that base/brief reconciliation. These findings
are source-validation issues, not a requirement to wait for an owner commit. The current report is not a
P1 freeze, P1 PASS or authority for 7b-3 behavior implementation.

## 2. Exact preparation inputs and preservation boundary

Consumed P1 revision-1 brief SHA-256:
`1443c6821dea395b5db1ab46fe7ef127eaaefd997b71d0098b64a6ffba58d3bf`.
All **1,459** original tracked paths are inventoried, including directory symlinks (1,456 regular/resolved-file
paths plus three directory-link paths); there were no untracked or staged inputs at preflight start.
The complete initial manifest SHA-256 is
`12b6c7f126682bd948821bed0e086107f3d12e885459d3f3ede955b9f53515c8`.

| Permitted uncommitted preparation | SHA-256 at source-validation start |
| --- | --- |
| MEMORY.md | 1e8bb349c766a69fe30180c804c6936a3ac28e2d52b8f29037c2a7eb39f32be4 |
| docs/2026-07-30-open-bug-register.md | ea28f021a372154e40fa9d9477897552316f7b3ee65e52e57fdcce3024ec438f |
| docs/2026-09-02-agent-roles.md | fd60c3ccf3179ddbf9ba0d0a2a2bd4064e7541796fe322b665feba1867184dc7 |
| docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md | cbce67054db4caa0c2207dd2c21416481398969279875b7dc3b67d1f563f877e |
| docs/superpowers/plans/2026-09-13-radmin-slice7b3-deferred-actions.md | 4a7af9b19e723598470fa5b9b054403c63f7e88d44ffaffd7bb7aa717c31b397 |
| docs/superpowers/plans/2026-09-15-radmin-slice7b3-p1-simple-action-preparation.md | 1443c6821dea395b5db1ab46fe7ef127eaaefd997b71d0098b64a6ffba58d3bf |
| docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md | 309d576a7e485afcc9d8189a85202993687ff53309f41e60ada02303ab9daf9f |

Simulator HEAD **06746a97de5764415d6fcef10b97bca90569b9c7**, clean; all **285** paths inventoried. Original
preparation diff and manifests are retained in the [artifact directory](2026-09-15-radmin-slice7b3-p1-preflight/README.md).
This landing adds the receipt/artifacts and M1's B399/current-dispatch bookkeeping only. It preserves the
briefs, owner rulings and source; final hashes distinguish those new reporting documents from the input set.

## 3. Source anchors and the owner's build-support fold-in

| Current source (unchanged from ac5f9a5) | Verified consequence |
| --- | --- |
| firmware_commands.cpp:1071–1089 | Exact seven-byte confirm after leading spaces; no effect-only helper. B399 records precise warning/erase/reboot order. |
| firmware_config_parse.h:562–566 | Existing parse_confirm_token matches the stated whitespace/token predicate; adds explicit null rejection. Reuse only with local behavior preserved. |
| firmware_commands.cpp:1096–1105; fw_main.cpp:1827–1852 | Sleep off-prefix/otherwise-on grammar and flag/output always execute locally; MR_NO_POWERSAVE removes the sleep policy. Future typed admission reports unsupported in that build without skipping/changing the local handler. |
| fw_main.cpp:395–416 | Debug gate precedes prefix grammar. Fault uses NRF52_PLATFORM or MRFAULT_ESP32; absent backend still prints the existing fault text and unsupported line locally. Typed support must say unsupported without changing those local bytes/effects. |
| fw_main.cpp:293–298, :320–354, :423–445 | Local wrappers retain their global/caller sink choices, reset marks/delays, two prep wipes and halt/output order. No complete typed sink/outcome contract exists. |
| device_ota.cpp:92–105; fw_main.cpp:335–354 | ota_start is idempotent; local ESP OTA toggles stop/start. The absent outer OTA backend has no effect/output in do_ota. Typed admission must expose absent backend while preserving the local no-op. |
| firmware_remote_executor.h:41–43/:87–90; firmware_commands.cpp:1666–1673 | Shared disruptive predicate is enforced in both executor and common command seam. Both remain refusing throughout P1. |

The three support cases are explicit owner instructions, not new refusals on local USB/BLE. P1's future
remote consumer needs the typed support result; P1 itself grants no remote execution. Device-fault macro/ISR
ownership remains single-TU. The 36 R-RA-39 refusal rows remain outside this preparation implementation.

## 4. Fresh focused proof and what has not run

I extracted the actual factory-reset function (source SHA-256
`a91d78f79b931a35e83692d7390dd999bc457ae428e112753912f832629ab57e`) and compiled it with Print/inbox/NV/reset
fakes. **Eight erase-result combinations PASS**, comparing complete output bytes and operation order.
A private reorder control compiles and fails an executed assertion: `ORDER/BYTES assertion failed, case 0`.
Commands/generated TUs/hashes/logs are retained. This runs an extracted production function, not the whole
firmware_commands TU/router and not real flash or reboot. It proves the narrow B399 wording correction only.

No native suite, corpus, board/ABI gate, probes/tools sweep, census or mutation union ran this turn. No
implementation has been written, so prior QA totals are not a P1 result. No PIN re-sync is claimed:

`PIN re-synced? NO — preflight STOP-1; no implementation or full P1 gate.`

The required YES line belongs in the later completed full-gate freeze and must contain independently derived
counts. After the brief/base is reconciled, P1 source-validation resumes; then implementation/full gate,
independent QA, and behavior reissue at the frozen QA-passed input set. Commits may occur at the owner's
chosen time and are not a prerequisite.

## 5. Resumed source-validation and actual base

The original §§1–4 above are historical preflight evidence. The owner reaffirmed that commits are not
progress gates. The documentation-only successor **c591721c2e09bb4e7583f49e458da9e5155cf822** was reconciled
without a commit: all production, test, tool, profile and simulator-anchor inputs matched ac5f9a5 before
implementation. The complete dirty preparation was copied into isolated snapshots, including untracked
preflight artifacts. The original seven preparation documents remain identified in §2; their subsequent
changes are distinguished from implementation inputs in the manifests.

P1 revision 2 corrected B399's warning-order sentence and incorporated the owner's build-support fold-in.
Its complete consumed input manifest contains **1477 paths**: `inputs-base.json`, SHA-256
`ab1dba43f563a0ab49a97285a35fdbacae56d1a08f8bd78a948cd3130257d7d2`. Consumed revision-2 brief
SHA-256: `2c3d551f0b2110dfd2dfc671b6fc08fc65eed3d9436ebbe1b100afbbd0eaa4be`. Revision 3 subsequently
clarifies B400's necessary source-reader dependency, without a new policy ruling or a production behavior
change. The final brief hash is recorded in the complete freeze inventory. Simulator HEAD remains
**06746a97de5764415d6fcef10b97bca90569b9c7**, clean and unchanged.

Source-validation confirmed the original factory confirmation and sleep-prefix grammar, debug-first crash
admission, reset/fault/OTA macro ownership, OTA entry-versus-toggle distinction, reset marking/delays,
independent inbox wipes, NV-warning order and both remote disruptive refusal guards. The original factory
function's eight-way proof is retained. No production edit preceded the original STOP report; resumption
followed the documented base/sentence reconciliation. No commit was required or made.

## 6. Implemented C1 boundary

- `firmware_action_effects.h` owns bounded value types and pure admission. It reuses the existing exact
  `parse_confirm_token`; plans contain only kind/backend choices. Admission explicitly reports absent
  reset/fault/OTA backends and disabled power saving. Local handlers preserve their old unsupported-build
  behavior rather than using the new support result to refuse additional local actions.
- `firmware_commands.cpp` routes immediate local factory reset and sleep through shared typed effects.
  Factory reset still attempts both inbox erases, prints the conditional inbox warning, attempts NV erase,
  prints its conditional warning, then resets. A call-scoped observer reports the combined erase outcome
  before a non-returning reset. Partial failure never suppresses a later effect.
- `fw_main.cpp` retains the hardware-owned reboot, prep, crash and OTA effects. Crash apply consumes the
  admitted kind/backend without parsing text or reading debug again. Prep retains learned-state clearing,
  both erases, halt and truthful output order. `device_fault.h` remains single-TU.
- OTA startup has one implementation taking an explicit `Print&`; its existing no-argument API delegates
  to it. Local OTA still toggles active→stop; typed OTA apply uses idempotent entry. Upload/verification,
  stop and reboot-hook policy are unchanged. Local wrappers retain their original sinks.
- New native grammar/support cases and the action runtime probe accompany the seams. Existing inbox
  fixtures acquire optional observation hooks and the necessary effect stubs for their standing dispatch
  proof. The source inventory follows actual predicate ownership and caller hops; authority policy is
  unchanged.

There is **no resident action state, timer, queue, cached sink, Node field or remote admission change**.
The entire `dispatch()` tail is byte-identical to the base; `firmware_remote_executor.h`, all `lib/core`,
`platformio.ini`, codecs/reference literals and simulation anchors are unchanged. Both remote disruptive
guards continue refusing. All 36 R-RA-39 refusal rows remain refused. R-RA-40's +80-byte behavior allocation
is not consumed by P1.

## 7. Executed local-equivalence and typed-effect proof

`tools/probe_deferred_actions/run.sh` reuses the existing inbox build recipe. It compiles the **whole real
firmware_commands.cpp and firmware_inbox.cpp** with their real core/storage dependencies, and uniquely
extracts/compiles the actual board-owned action functions and OTA startup/stop/accessors. Reset, delay,
fault and DFU registers are labelled host primitives. Flush observation forwards to the supplied sink;
a linker wrapper records and calls the real `Node::clear_learned_state`. The actual hang loop is escaped
by a host alarm. This is not a whole-fw_main host build or a physical reset/flash/OTA qualification.

The whole command TU uses the existing ESP-style host platform. The four variants apply to the extracted
board owners: absent backend, nRF, ESP, and ESP with power saving compiled out. Separate real board builds
and compiler stack reports cover the actual nRF/ESP translation units.

| Executed variant | Original local checks | Final checks | Local transcripts |
| --- | ---: | ---: | ---: |
| Absent backend | 114 | 150 | 39 byte-identical |
| nRF | 114 | 151 | 39 byte-identical |
| ESP | 117 | 158 | 39 byte-identical |
| ESP, power saving disabled | 117 | 158 | 39 byte-identical |

All **156 complete local output/effect transcripts** compare byte for byte. They cover confirmation refusals,
eight inbox/NV failure combinations and warning order, sleep tails, prep failure/halt order, debug refusals,
crash prefixes, reset marks/flushes, OTA toggle/failure and safely escaped non-returning operations.
The native additions separately cover exact token/length/space boundaries and owned-plan lifetime.
Typed executed assertions cover supplied sinks, combined pre-reset erase outcomes, frozen debug admission,
backend support, OTA idempotence, startup failure and reset-hook installation.

The controlled action run and `--no-neg` both meet the four positive pins. **19 controls compile and fail
executed assertions**, including skipped/reordered effects, incorrect grammar/debug/backend handling,
warning suppression, sink escape, entry replaced by toggle, missing flush/reset hook and removed hang loop.
Four source-reader checks reject absent/duplicate owners and ignore comment/string shadows. Pins and their
arithmetic are explicit in the runner. Every watched source hash is preserved.

## 8. Coder full-gate results

The gates run in isolated complete snapshots with `MR_LUS_SRC` explicitly set. The native/mutation snapshot
contains all uncommitted implementation inputs; subsequent reader-only fixes have no native or board code
dependency. All **218 native build inputs** and all **53 selected mutation target files** remain hash-identical
to the union's input freeze. The reader retry uses a fresh complete snapshot of the corrected instruments.
Failed original source-reader runs are retained alongside the corrected runs.

Fresh native base: **2912 cases /184461 assertions /0 failures /0 skips**. Fresh final wrapper and actual
binary: **2916 /184587 /0 /0**. Four new cases add 126 assertions; no prior case is removed.

`PIN re-synced? YES — 2912/184461 + 4/126 = 2916/184587 (0 failed, 0 skipped).`

| Instrument | Executed result |
| --- | --- |
| Independent reference | 94/94 strict; old 89 identical; five comparator controls RED; separate named-array one-byte corruption refused |
| Fresh normal/gateway simulator builds | Both variants compiled/linked in separate base/final build directories; verbose provenance retained |
| Corpus | Both manifests validated; canonical comparator PASS and independent actual-byte comparison **36/36 identical** |
| Live s18 anchor | md5 **32afbf11e43b4bf9d0bd470ad502ba0a**, **269517 events /0 assertion failures** |
| Default board ABI | **191 checks; 9/9 controls RED; zero unusable** |
| B278 correlation-row ABI | **42 measurements; 6/6 controls RED** |
| Console sink, default and --no-neg | Six profiles, **720 runtime /83 structural /905 BLE /6 ownership**, 3 ownership controls; controlled default **149 controls /0 unusable** |
| Inbox verbs, default and --no-neg | ACCEPT **1363 checks /60 controls**; CLIENT **384 /45**; explicit CLIENT controlled run also passed |
| Firmware UI, default and --no-neg | All runner pins met; controlled default **223 controls /0 unusable** |
| Custody USB, default and --no-neg | **27 checks /10 controls**, zero unusable |
| BLE line, default and --no-neg | **40 checks /8 controls**, zero unusable |
| Features, default and --no-neg | **9 cells /120 checks /59 controls**, zero unusable |
| New action probe | Four positive pins above, controlled **19 assertion RED**, source-reader checks **4**, --no-neg also passed |
| Inventory | Write/bare/check PASS; **204/204 semantic rows unchanged**, actual owning-function/source provenance updated |
| Authority | Checker and all **six selftests** PASS |
| Tools/census/final integrity | Completion recorded in §11 below |

The union selectors are independently derived: changed configured source set **S = {actionadmit}**;
**H = all 52 historical batteries**, including the reused confirmation-token dependency `sliceDtoken` and
B391's repaired X09. **S ∪ H = 53 batteries /827 configured controls**. Executed result:
**826 assertion RED /1 known unusable B342 /0 vacuous**. All 143 worker baselines are 2916/184587/0;
every restoration hash matches, and every RED reports one source match and a positive assertion-failure
count. B342's M04 compiles but remains green; it is not counted RED. The orchestration initially stopped
on that tool's exit 1, verified the exact known exception, and resumed the remaining batteries without
rerunning or relabelling completed results. X09 again matches once, compiles and fails its assertion.

## 9. Resource measurements and attribution

Deterministic base/final board pairs ran **gateway then heltec_mobile**, sequentially, at the same private
paths and fixed build identity. Pristine manifests, ELFs, payloads, sections and symbol inventories are in
`measurements.tar.xz`; none were patched for attribution.

| Target | Node bytes | RAM, base → final | Flash, base → final | Object count |
| --- | ---: | ---: | ---: | ---: |
| Native | 230896, unchanged | n/a | n/a | n/a |
| Gateway | 157264, unchanged | 203956 → 203956 (**0**) | 570588 → 568220 (**−2368**) | 285 → 285 |
| Mobile | 117912, unchanged | 207756 → 207756 (**0**) | 1372992 → 1373576 (**+584**) | 329 → 329 |

Only the linked text section grows/shrinks in size. Gateway text is 569604→567236: its defined-symbol
extent union changes by −2370 and uncovered bytes by +2, exactly −2368. Mobile text is 1052122→1052706:
its union changes by +562 and uncovered bytes by +22, exactly +584. The attribution records all changed
symbols rather than calling small differences noise. Extraction changes compiler inlining and retained
weak functions as well as adding the typed entries; for example gateway SegmentedInboxStore::append is
6448→3740 and begin is 4012→3024. Those are compiler output changes in shared inline code, not edits to
core source. “Uncovered bytes” is a measured extent complement, not a claim that every byte is padding.

The real compilers also compiled all three owning TUs before/after with their derived environment flags
plus `-fstack-usage`. Objects, exact commands, warnings and `.su` files are retained. No firmware flags
were edited. Representative **individual compiler frames**, in bytes:

| Function/path | Gateway base → final | Mobile base → final |
| --- | ---: | ---: |
| dispatch | 304 → 312 | 320 → 320 |
| mesh_service_once | 792 → 760 | 688 → 704 |
| setup | 1128 → 1120 | 1088 → 1088 |
| Local prep owner → typed prep effect | 144 → 152 | 32 → 48 |
| do_reboot → typed reboot effect | 8 → 32 | 32 → 48 |
| New factory effect | 152 | 64 |
| New sleep effect | 24 | 48 |
| New crash effect | 32 | 48 |
| New OTA effect | 24 | 48 |
| OTA startup, original → supplied-sink implementation | not compiled | 96 → 96 |

These are compiler frame measurements, **not a sum or whole-task stack high-water**: wrappers/inlining,
callee frames, interrupt nesting and callbacks affect live depth. The reports price the new transient
frames without pretending that zero resident RAM implies zero stack cost. Hardware qualification remains
separate. Type-size probes on all three ABIs measure ActionPlan **2**, ActionAdmission **3**, ActionSupport
**4**, and ActionObserver **16 native /8 gateway /8 mobile**. No resident instance of those types is added.

## 10. Findings, failed attempts and limits

**B399:** the revised brief states the real warning order; both the original narrow reproduction and the
final complete local-equivalence proof retain it. Independent closure remains QA's responsibility.

**B400:** the first console/inventory runs correctly refused three now-empty parser surfaces. The old
source map produced no sites in handle_sleep, handle_factory_reset or handle_crashtest; five inventory
rows had moved. This was reported STOP-1 before generator edits. Review of the authorized C1 scope showed
that following those real predicates is required source-provenance maintenance, so work resumed without
another permission request or owner ruling. Revision 3 makes that dependency explicit. The generator now
follows both typed action predicates and the existing library confirmation predicate, checks every caller
hop and preserves enclosing feature gates. All original command files retain full-file coverage; only the
named confirmation predicate is selected from the generic configuration-value parser. Empty, missing,
duplicate and disconnected-source refusals remain effective. Eight new reader tests exercise the new
boundaries; full semantic comparison preserves all 204 rows. The original failed discovery ran only 325
tests, with four failures and eight errors (including class setup failures); it is not a smaller passing
suite. Corrected full discovery runs **351 = 343 + 8** tests, zero failures/errors/skips. Authority policy is byte-identical.

Retained iteration failures are not successful gate evidence:

- The first new native compile lacked `<initializer_list>`; the explicit include fixed it before the fresh
  successful native run and mutation input freeze.
- Action-probe development encountered an import-path failure, a generated-source marker error, a macro
  argument compile failure, and a buffered stdout/stderr interleaving that hid an assertion marker. The
  final runner uses unbuffered transcript output and requires a real assertion failure for every control.
  An OTA failure check was tightened to the exact outer failure line so the lower-level warning could not
  satisfy it accidentally. All final controls compile and are RED.
- The first independent corruption attempt altered a non-reference literal, so the reference comparator
  correctly stayed green. The corrected control flips exactly one byte of the named independent array
  `kRefBody_resp_terminal_auth_code08`; it exits 1 with a byte-zero mismatch.
- One private staging copy attempted to copy a file onto itself. Subsequent byte/hash verification showed
  that every production file actually built in the board snapshot equals the final source; no stale build
  was accepted. The complete deterministic base/final measurements are retained.
- The first new empty-predicate reader test used the wrong source argument name and refused its own
  zero-match control. It was corrected against source and all eight reader tests then passed.
- The first mutation aggregation assumed three workers for every battery. Some batteries have fewer than
  three entries; the corrected audit derives each actual worker count, totaling 143. Raw logs are unchanged.

Known B312/B315/B350/B359/B364 limits remain separate. In particular B350's firmware-UI --no-neg run prints
bare PASS with zero controls; it is recorded only as the required positive-only run, never a substitute
for its 223-control default gate. The stale optional B359 extra-pins overlay was not used. The ordinary
native binary was run; no whole-suite XML pass is claimed across B364's fixture over-read. Host reset,
watchdog, flash and OTA fakes do not establish real hardware behavior. P1 introduces no new metal behavior,
so no new bench behavior is claimed. R-RA-40 and the 36 retained refusals are unchanged.

## 11. Frozen coder handoff — 2026-09-15

**CODER GATE PASS. Implementation and evidence are uncommitted and frozen for independent QA.**
All required final instruments passed, with only the previously named B342 unusable control. This is not
an independent QA verdict. No production implementation of 7b-3 behavior was started.

Final completions:

- Full tools discovery: **351 tests /0 failures /0 errors /0 skips**, with a measured real ELF under the
  private `.pio-measure` tree. Its intentional negative-control output and the existing Python
  ResourceWarning remain in the unedited log; the suite exits 0 and reports OK.
- A0 and DataType literal checks PASS. Both repositories' whitespace checks PASS. Simulator source/status
  remain unchanged. Production sources match the measured board/native/probe inputs; all core/profile/
  executor/codec/anchor inputs are unchanged. The whole dispatcher tail still matches the base byte for byte.
- Six-environment census PASS: gateway_heltec **173**, gateway_heltec_v4 **178**, heltec_mobile **177**,
  heltec_v3 **177**, heltec_v4 **182**, heltec_v4_mobile **182** warnings, exactly their pins; **zero -Wswitch**.
  This is the census's explicit six-environment exception. The normal board pair remains gateway/mobile only.

HEAD is **c591721c2e09bb4e7583f49e458da9e5155cf822**. The complete final tracked/untracked SHA-256 inventory
(including the final brief, this receipt and all evidence artifacts) is
[`freeze.json`](2026-09-15-radmin-slice7b3-p1-coder/freeze.json). Its only self-exclusion is the inventory
file itself; ignored build caches and Git internals are not source inputs. The original preparation and
per-instrument input manifests are retained separately. This makes the base a commit plus the entire
frozen input set, including new/uncommitted implementation, not HEAD alone.

[Artifact index and replay details](2026-09-15-radmin-slice7b3-p1-coder/README.md) link the command/result
records, pristine board/stack archive, both complete validated corpora, every mutation log, probe sources
and controls, source-restoration evidence, retained failed attempts, and the complete uncommitted source
patch/overlay. Artifacts have their own SHA-256 inventory. The private working archive remains at
`/tmp/mr-codex-s7b3p1-gaz2dlhn` for direct inspection; durable evidence is in the repository artifact directory.

**Next:** QA independently reruns every required instrument against this complete freeze and issues its
own PASS/HOLD. After independent P1 PASS, QA refreshes the separate 7b-3 revision-4 behavior brief at this
frozen base/API shape. That behavior implementation must use exactly R-RA-40's one 40-byte row, four u32
transcript details and two diagnostic bytes, +80 total, Node 230976 native /157344 gateway, mobile unchanged.
Extra owned state remains STOP-1 for measurement; the 36 refusals stay refused. No step waits for a commit.
B394/B399/B400 remain pending independent QA closure. Owner rulings, commits and metal verification remain
with the owner.
