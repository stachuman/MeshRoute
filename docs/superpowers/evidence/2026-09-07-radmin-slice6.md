<!-- Coder: OpenAI Codex -->
# Remote-admin v2 Slice 6 — pre-implementation STOP report, 2026-09-07

**Status: STOP before implementation/base measurements.** This is the coder's source preflight required
by the brief's opening paragraph, not a software report, QA verdict or completed implementation. No
production, tests, tools, inventory, ruling, QA brief/ledger, maintained register or simulator file changed.
The only coder-created file is this report. Proposed findings below are for QA's maintained-register
landing under the owner's new roles; the coder does not alter classifications or repair the brief.

## 1. Observed inputs and dispatch prerequisites

- MeshRoute: /home/staszek/MeshRoute, HEAD d226189831420440b2a089e846114ebfbafb1042.
  Initial status contained only the untracked Slice 6 brief. Thus the brief's requirement for an empty
  status was not satisfied; no measured-start baseline was taken.
- Brief: docs/superpowers/plans/2026-09-07-radmin-slice6-dispatcher-authority.md, SHA256
  30ab6f4c7d2309598fbdd7add62d0191c9d8f4c6bdbe51fc9cd55a74358118d2 (344 lines inspected completely).
- Simulator: /home/staszek/lora-universal-simulator, HEAD
  06746a97de5764415d6fcef10b97bca90569b9c7, clean; CMakeLists.txt diff empty. Its Slice 5 commit exists.
  Brief :29–32 still pins 8688884 plus a pending uncommitted change. QA must bind the actual commit;
  the coder may not silently repin or measure around a placeholder (STOP-1).
- Owner has assigned Codex implementation and the other agent specifications/briefs, independent
  implementation Quality Gate and documentation landings; owner retains rulings/commits. The brief :6
  still says model: opus, and docs/2026-09-02-agent-roles.md:8–23 still describes the old division.
  QA should reconcile these preparation documents and their dispatch status with the already-made
  owner decision. This is not a request for a new owner ruling or permission to self-author Slice 6.
- After QA resolves the source disagreements below, the preparation package and a clean measured-start
  arrangement need explicit final base bindings. No implicit dirty-document exception is assumed.

## 2. Findings proposed to QA (M1; next-free number checked as B343)

### B343 — the seam-only validator misses BLE's earlier executing handlers

Brief §§4.1/5 limits validation to exec_console_line and exec_command; fw_main may only build contexts
and name the USB buffer bound. This cannot establish its whole-BLE embedded-NUL refusal promise.
At src/fw_main.cpp:543–547, ble_dispatch_line calls handle_cfg_set directly and returns before the
fallback seam; :563–565 likewise directly execute inbox handlers, and :514–516 calls handle_rcmd.
A source-path witness is a byte span spelling cfg set name X followed by NUL and a suffix: the prefix
matches :543 and handle_cfg_set's name arm (firmware_config.cpp:272–279) uses strlen on the value.
Nothing in the proposed seam validator reaches that earlier call. This is source-derived, NOT an
executed reproduction or a claim of an external exploit.

**Required author resolution:** explicitly place the same validator before every BLE command-owning
branch, define refusal precedence/output, and authorize/prove that actual entry wiring. Preserve valid
local bytes and the existing transport guards. A direct exec_console_line probe alone cannot prove
this BLE path. Expanding fw_main's fence or its instrument is QA's brief decision, not the coder's.

### B344 — semantic authority key collides with legacy surface classification

Brief :179–183 and :218–221 require remote_encode/remote_exec inventory rows to be legacy. But
:198–205 keys the normalized table/header/checker solely by (verb, subverb), one class per key.
The live inventory has status / bare on dispatch (:55), ble_dispatch_line (:219) and remote_encode
(:231); routes likewise occurs at :53/:218/:230, and duty at :26/:207/:228. The ordinary semantic
status/routes rows must be open, while those same keys on remote_encode are required to be legacy.
No one-class table keyed as specified can satisfy both obligations.

**Required author resolution:** specify a source-bound distinction between inventory surface
eligibility and semantic command authority, including inheritance/checker rules and refusal controls.
Do not choose one of the conflicting classes, silently drop historical surfaces or invent an owner
classification. This is a STOP-2 representation conflict, not an objection to R-RA-32/33's policy.

### B345 — peers normalization prediction does not follow the prescribed edits

Inventory :44–46 contains all plus two bare dispatch rows; :213–214 contains two bare BLE rows.
Brief :211–217 removes the dispatch level-guard row but RETAINS the BLE refusal row with a discriminator.
Those described peers edits remove one row and relabel another: from 204, that predicts 203, not 202.
If another row is meant to disappear, the author must name it and preserve its semantic/transport
coverage. Also bind the newly introduced <args> refusal discriminator to the normalized authority
representation; it is not a new executable subcommand to feed to the native lookup test.

**Required author resolution:** correct the predicted arithmetic or specify the second justified
removal, with exact before/after row mapping and controls. No generator was run or edited by this
preflight; 203 is the inference from the written operations, not a measured regenerated inventory.

## 3. Handoff and verification status

STOP-1 covers the dispatch inputs and source/brief disagreements; B344 additionally invokes STOP-2.
QA authors the corrections and records these findings in the maintained register; the owner commits
preparation. Codex resumes against the corrected, explicitly pinned clean-start contract.
Native, corpus, probes, mutations, boards and tool sweep were NOT run. No Slice 5 figure has been
relabelled as a coder-derived Slice 6 baseline, and no software PASS or PIN re-sync is claimed.

## 4. Revision 2 follow-up preflight — clean inputs; new authority conflict (2026-09-07)

The original preflight record above is preserved. B343–B345's author fold-ins and the new roles are
present. Measured-start input checks, before this report append:

- MeshRoute HEAD f948c2558bcdb03934a3fc73b8c6ee7f61f88772, clean. The successor diff against d1a2906
  names only the brief; it contains the dispatch pin and the requested B346/six-selftests wording fixes.
- Simulator HEAD 06746a97de5764415d6fcef10b97bca90569b9c7, clean. MESHROUTE_DIR in build/CMakeCache.txt
  points to /home/staszek/lora-universal-simulator/../MeshRoute, the intended checkout.
- Current brief SHA256 c964a0731b4fbe79d756ecc2d6e619c898b82221ebdc0b11b65759803c89f180.

**STOP-2 before baseline runs or implementation: proposed B346 — acl list test classification contradicts
the owner's remote-authority ruling.** This emerged while reading the complete ruled classification
for its normalized transcription, not from a runtime measurement.

| Authority | Required behavior |
| --- | --- |
| R-RA-33, rulings ledger :743–753, judgment call 8 | acl list and admin-id show are owner when remote, physical when local |
| Ruled classification proposal :113–117 | acl list/add/set/remove admit remote owner; acl reset and admin-id generate/rotate/reset remain physical; admin-id show is owner |
| Slice 6 revision 2 brief :331 | the native table test must classify acl list as physical |
| Same brief :343–344 | the real-seam probe must refuse acl list at ALL three remote authorities |

Those test expectations cannot coexist with the ruled semantic table. R-RA-29's existing local BLE
whole-family refusal does not prohibit the separately ruled remote-owner access; the brief itself
keeps local contexts off the authority table and preserves that BLE guard.

**Required author correction, not a new product ruling:** align the table examples and probe expectations
with R-RA-33. Use acl list as owner-only remotely (open/operator refused, owner admitted); a genuinely
physical operation such as acl reset confirm can retain the all-remote-refused control. Check the
sibling admin-id show and owner ACL mutation rows against the same approved table. Do not widen local
BLE or implement future remote-only execution semantics outside the slice's fence.

QA owns the brief correction and maintained-register landing for B346. The coder has not silently
chosen between the conflicting obligations, changed production/tests/tools, committed, or run a software
gate. Only this evidence append changed the tree. Baseline collection and implementation remain pending
the corrected dispatch input contract.
