# Standalone mobile Home and messaging — specification-author handoff

**2026-09-22 · Ready for specification work only.** The owner requests a fresh agent to develop the existing [design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md). This handoff supplies context and an authoring assignment; it is neither an approved product specification nor an implementation dispatch. Source observations below were checked for this handoff; tests and hardware were not run.

## 1. Your assignment and authority

Act as **specification author**. Bring the existing standalone-mobile design to a source-validated, concrete form that the owner and an independent reviewer can assess, and from which QA can later author implementation briefs. Improve that canonical document in place; do not create a competing product design. Preserve its owner agreements, identify proposed choices explicitly, and retain useful decision history without leaving obsolete statements presented as current.

The product is a handheld group communicator usable without an iOS companion: primarily hiking and event/festival coordination. “Home” here means the **OLED landing screen**, not the mobile's routing home node. The work concerns identity, setup discoverability, everyday team/person messaging and the shared text editor. It is not a mobile-home attachment or routing redesign.

The owner has authorized independent inspection and documentation work. Proceed without asking for access, permission to read/edit the design, or a commit. Ask the owner only for unresolved product/resource choices that materially change the contract, after presenting a concrete recommendation and tradeoff. Author proposals are not owner rulings. A separate context reviews your specification; a later coder source-validates and implements an approved brief; QA independently gates that implementation. The owner commits and performs physical qualification. Historical agent names in the roles document do not change this assignment.

**Authorized outputs:** the existing design; a compact source-audit/decision record under `docs/superpowers/evidence/` if useful; measured new findings in the maintained bug register; concise tracker/MEMORY pointers where their current statements actually change. Add or revise metal-plan obligations only where the proposed behavior requires physical evidence, marked pending and clearly distinguished from today's executable procedure. Keep proposal-only procedures in the design until their implementation contract is approved.

**Stage boundary:** do not implement production, test, tool, board-profile, simulator or gate-pin changes; do not begin a full implementation gate. Read those files to make the design truthful. Small disposable source/layout calculations may support an explicitly labelled measurement; they are not a software PASS. Do not stage, commit, reset, clean, restore or discard existing work. Do not run cleanup as part of this assignment.

## 2. Starting state and preservation

| Item | State observed for this handoff |
| --- | --- |
| Repository | `/home/staszek/MeshRoute` |
| MeshRoute HEAD | `20357578700270d7315dcf86bc3bb5725d8024cb` (`repository cleanup`) |
| Existing tracked deletion | `B164.md` — preserve the deletion |
| Existing modified documents | `docs/2026-07-30-open-bug-register.md`, `tracker.md` — preserve their B439/tracker refresh |
| Existing untracked paths before this handoff | None reported by `git status --short` |
| Simulator | `/home/staszek/lora-universal-simulator`, `6585649ea5a780f0542b2931853a667be56a5b2b`, clean; read-only for this task |
| Input inventory | [SHA-256 manifest](../evidence/2026-09-22-standalone-mobile-home-handoff-inputs.json): preparation inputs and pre-existing dirty state; excludes these new handoff artifacts |
| Product design status | DRAFT, last updated 2026-09-07; detailed review still pending |
| Relevant live finding | B335 / HOME-A2 remains OPEN; B334 / HOME-A1 is a closed correction of the discussion premise |

First read the working tree and report its exact state. Verify the manifest and distinguish subsequent owner work from unexpected input changes. Do not silently measure an old export. This is a preparation snapshot, not a frozen implementation dispatch: reconcile a changed base before relying on its facts. Under P4, relocated line numbers alone are not STOP-1; a semantic disagreement or an unexplained frozen-input mismatch is. Use `file:symbol` anchors, adding current line numbers as navigation hints.

**Commits do not block progress.** An eventual uncommitted implementation base is the last commit plus a SHA-256 inventory of its frozen inputs. Any isolated snapshot must include tracked edits, untracked inputs and deletions, not merely `git archive HEAD`. Coordinate before any builds or mutation runs so they cannot overlap coder edits.

The design's references to an active remote-admin Slice 4/5 coder and its old `a0ff994`/`1677b44` source snapshots are historical. Remote-admin software and its harness follow-up are now completed; physical obligations remain in the metal plan. Refresh the design's current-state and sequencing language. Do not reopen that arc or copy its allocations, count floors or gate figures into this project.

The September cleanup deliberately moved large historical logs/ELFs/streams out of the live tree. Their absence is not missing implementation or invalid evidence. See [evidence policy](../evidence/README.md) and the [recovery catalog](../../archive/2026-09-20-generated-evidence.md). New raw output belongs in ignored `artifacts/` or a named external archive; retain compact receipts and input hashes, not another copy of every build log.

## 3. Required reading

Read in this order, then follow the relevant source symbols:

1. Root `AGENTS.md`, [CODE_GUIDELINES](../../CODE_GUIDELINES.md), [agent roles and later owner amendments](../../2026-09-02-agent-roles.md), root `MEMORY.md` and `tracker.md`. Apply V1/V2, U1/U2/U3, C1/C4, D3/D4, P4–P7 and M1–M3. Later owner rules supersede old prose: specifically, the guideline about fleet reflashing is superseded by M3; MeshRoute is unshipped and wire changes are free, with separate attribution still required by C4.
2. The **entire** [standalone-mobile design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md), especially agreement boundaries §§1/4.1, multiline/manual sending §§6.1–6.2, preview §§7.1–7.3 and earlier-authority table §9.
3. [Bug register](../../2026-07-30-open-bug-register.md): current dispatch, B334/B335, B118, B191, B246, B286, B418 and any newer related findings. At this snapshot the live next-free number is **B440**; re-read before allocating. Chronological prose contains older numbers/statuses; use current row dispositions.
4. [UI-17 navigation/status/team design](../specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md), particularly geometry, interruption, resources and **§9 R-1 through R-7**; [UI-10/11 preset catalog](../specs/2026-08-25-ui10-11-preset-catalog-spec.md), especially OQ-A, canonical bytes, generation binding, failed saves and emergency exceptions; [UI-16 nearby onboarding](../specs/2026-08-22-ui16-nearby-onboarding-spec.md), especially identity, scope and join/key flows.
5. [Current metal plan](../../2026-09-20-metal-test-plan.md), especially UI-01–UI-19, TEAM, BOOT and relevant power/NV rows, plus its [disposition map](../../2026-09-20-metal-test-triage.md). The old `docs/2026-07-31-bench-test-script.md` and companion guides are navigation stubs/archive records, not the maintained plan.
6. `simulation/BASELINE.md`, `docs/firmware-dev-guide.md`, `docs/protocol.md` and `docs/frames.md` as needed to derive later validation and transport limits. Read the actual codecs/parsers before trusting a diagram or a quoted maximum.

The available bench is two Heltec V3s, a XIAO ESP32-S3, a Heltec V4 and a XIAO nRF52840/SX1262 node. Verify each relevant build profile: the stock OLED handheld path and implemented nRF52 BLE path are not an assumed single OLED-plus-BLE image. No current physical PASS is inherited from old builds.

## 4. Settled direction versus decisions still open

Preserve these agreements; do not ask the owner to choose them again:

- Standalone use, visible device name, discoverable naming/join/create, and dynamic no-team versus in-team Home.
- Ordinary team sending already exists. **No hardware failure was reported** behind the earlier assumption that only emergency sends worked. Improve discovery and composition; reuse the normal send path. B334 is not an implementation defect to fix.
- Owner, 2026-09-06: **“This limit of 17 chars is wrong - messages should use multilines.”** The 17-byte preset cap is a current implementation fact, not the future target. Preserve **`Return to base now` (18 ASCII bytes)** in full; substituting `Return to base` is not the remedy.
- Owner, 2026-09-07: **“Proposal makes sense, grouping should be equal (we can't consider english specific statistic of letter use) and limited to bare minimum (letter, numbers, limited punctaction).”** Use one shared editor for naming and one-off ordinary team/person messages, with fixed, equally sized character groups and a minimal repertoire. Short advances, double chooses; long holds remain emergency gestures. No frequency layout, prediction or learned ordering.
- Explicit review before Save/Send; bounded RAM drafts; no write to NV for every character; cancellation cannot save/send. One-off manual composition does not rewrite a preset or use the emergency repeat path.
- Retain full-message bytes through review and submission. Changes to names, membership or the roster cannot change the action/destination under the user's finger.

These remain **proposals requiring detailed design/review**, not inherited permission:

| Decision | Existing candidate / work required |
| --- | --- |
| Home states and navigation | No-team setup versus in-team communication; choose exact actions, rail behavior, back paths, identity/detail path, strings and geometry |
| Alphabet and editor | Candidate: `ABCDEF`, `GHIJKL`, `MNOPQR`, `STUVWX`, `YZ0123`, `456789`, `SPACE . , ? ! -` — seven groups of six, 42 unique characters; uppercase, group size and layout are not ruled |
| Text capacities/default phrases | Derive separate name/manual/preset byte bounds from real transport, parser, memory and storage constraints; choose vocabulary and migration/default policy |
| Incoming team preview | Recommended by the draft, not owner-approved; specify eligibility, identity, ordering, retirement, Open/Hide and counts before asking for a decision |
| Direct-message preview | Separate privacy/priority decision; not implicitly included in team preview approval |
| Branding/splash | Moving the Home logo to a roughly one-second boot-only splash is a candidate; exact timing and navigation changes remain open |
| Implementation grouping | Existing work-package list is a suggestion, not a frozen slice sequence, resource grant or file fence |

Record each unresolved choice with a recommended option, a concrete alternative, its user-visible consequence and its memory/protocol cost when relevant. Keep agreed requirements, proposed mechanisms, measured facts and pending owner rulings visibly distinct.

## 5. Current source map — starting facts, not a substitute for your audit

All paths below are repository-relative. Re-resolve symbols; inspect their callers and tests before designing a change.

| Area | Executable anchors | What matters |
| --- | --- | --- |
| Input classification | `src/firmware_ui_input.h`: `InputCfg`, `InputFsm::update`, `InputFsm::active`; `firmware_ui_model.h`: `UiModel::on_gesture` | Current debounce/double/arm/fire defaults are 25/350/800/3500 ms. Short waits for the double window. Emergency handling precedes ordinary navigation; consume the waking gesture. Reuse classified input |
| Navigation/state | `src/firmware_ui_model.h`: `Screen`, `UiState`, `UiModel`, `advance_or_next`, `next_screen` | Current cycle: Status, Team, Inbox, Send, Settings. Design every entry/return/confirmation path, including existing modals |
| Rendering and copies | `src/firmware_ui.cpp`: `build_snapshot`, `draw_status_screen`, `draw_compose`, `draw_frame`, `s_frame_state`, `s_frame_snap`; model `UiSnapshot` | Frame rendering uses frozen copies. A field's size is not its total image cost: account for model/frame-state copies, frozen snapshot and transient snapshot/stack lifetime |
| Current Home geometry | `src/firmware_ui_status.h`: `kStatusNarrowCols`, `kStatusWideCols`, `ui_status_me`; renderer/chrome/icons | Logo takes upper-row space: 14 columns there, 19 below. Current ME display is a local ID, not the human name. Do not assume a sixth body row fits |
| Name persistence | `src/firmware_config.cpp`: `handle_cfg_set` name arm; `src/device_nv.h`: `IdBlob`, `save_id`; `lib/core/node.cpp`: `Node::effective_name`; `node.h`: `_name` | Existing name storage is 32 bytes. Console name handling clamps to the record width and updates live name only after save succeeds. Existing default is `MeshRoute node: 0x<hash>`, not the draft's proposed short fallback |
| Setup services | Model `provision_rows`/`settings_rows`; `src/firmware_ui_{prov,nearby,invite,join}.h`; `firmware_provisioning_service.h`, `firmware_join_service.h`, `firmware_join_profiles.h`, `firmware_team_keyring.h` | Reuse existing create/join/invite/saved-key services and explicit confirmations. There is no current Settings name editor |
| Existing ordinary sends | Model `SendKind`, `SendReq`, `queue_send`; `src/firmware_ui_send.h`: `send_gate_of`, `ui_compose_send_line`, `ui_perform_send`; `firmware_ui.cpp`: `ui_exec`, send adapter | `channel_canned` is distinct from emergency. `SendReq` carries kind, peer ID, slot and generation, **not owned free text**. Real dispatch uses the shared executor. Its command buffer is currently `kSendLineCap = 96`; expansion must audit the whole path |
| Persistent presets | `src/device_nv.h`: `UiPresetSlot`, `UiPresetBlob`, `kUiPresetTextMax`, `kUiPresetVersion`, `ui_preset_blob_state`; `firmware_ui_presets.h`: `validate_preset_text`, `PresetCatalog`, `kPresetDefaults` | 17 slots: one emergency, eight DM, eight channel. Each text max is 17 bytes; slot size 21, blob 372 by current static assertions. `/mrui` is version 1 with exact-version acceptance; a bump alone rejects old records, it does not migrate them |
| Preset grammar | `src/firmware_ui_presets.h`: `validate_preset_text`; `firmware_ui_preset_verbs.h` | Current printable-ASCII grammar excludes quote/backslash, CR/LF and all-space text. Separate display wrapping from a change to accepted payload grammar/escaping |
| Current phrase behavior | `kPresetDefaults`; preset catalog commit/generation helpers; `send_gate_of` | Mandatory emergency phrase plus ordinary defaults including `Got your message` and `All good`. Preserve custom/disabled slots, location flags, canonical bytes and stale-generation rejection |
| Inbox paging/read semantics | Model `kDetailCols`, `kDetailBodyRows`, `kDetailPageMs`, `FrameGate::on_page`; `firmware_ui.cpp`: inbox callbacks, `ui_open_inbox_detail` | Detail already pages 19 × 2 body characters on a 2000-ms cadence. List-frame read-watermarks are not per-message acknowledgements. Audit reuse of detail formatting and exact-record opening |
| Inbox identity/provenance | `lib/core/inbox.h`: `InboxEntry`, `Inbox::pull`, `inbox_record_is_internal`; `inbox.cpp`: `Inbox::record_channel` | Channel records retain team/channel/opened-encryption data but record `sender_hash = 0`. A current roster label for an old local ID is not stable sender provenance. Callback body storage is borrowed |
| Receive/emergency distinction | `src/firmware_ui_send.h` receive handling; `firmware_ui.cpp`: `mr_ui_on_push`; model reply/wake methods | Channel wake uses opened encryption (`pu.enc`); distress reply has additional scope/state guards. Preview eligibility, wake and emergency reply are distinct decisions |
| Actual byte admission | `lib/console/console_line.h`, `console_parse.cpp`; command dispatch; `lib/core/node_channel.cpp`, `protocol_constants.h`, `frame_codec.*` and DM sender/crypto callers | Derive DM and channel limits separately, including optional location/sealing and console overhead. A DM maximum is not a channel maximum |
| Profiles/board integration | `platformio.ini`, `src/device_ble.h`, `variants/heltec_common/board_ui.cpp`, traits and feature gates | Verify OLED/profile inclusion, loop/input/power behavior and transport availability on each applicable target |

The ordinary path is also present in `test/test_firmware_ui_model.cpp` and exact command-byte cases in `test/test_firmware_ui_send.cpp`. This handoff inspected those sources; it does **not** report a new passing test run.

## 6. Required design work

### A. Identity and setup

Specify name display, truncation indicator/full-name view, unset-name fallback and the rename flow. Distinguish a presentation abbreviation from a second persisted identity. Treat bytes and characters separately. Existing names can contain characters the proposed minimal editor cannot create; preserve them or explicitly refuse an unsupported edit without silently uppercasing, transliterating or deleting text.

Trace the existing identity save path before proposing a reusable firmware service. Define success, failed save, cancel and interruption outcomes; preserve identity seed, position and membership. Save must succeed before the UI claims the live name changed. A console's existing truncation is not permission to clip an editor input silently.

Define Home for no team, membership without a key, missing/pending local ID, ready-to-send, and relevant fault/pending-restart states. Membership, possession of the content key and local-ID acquisition are separate. Reuse create/nearby-join/invite/saved-key workflows; no hidden scanning, PHY retuning or authorization shortcut. Explain the six-hex nearby fingerprint versus full team identity display. Back navigation must not pretend to undo an already acknowledged create/join operation.

### B. Shared editor and one-button interaction

Specify the complete state machine: caller entry, group selection, character selection, insertion, cursor movement, backspace, Done, review, final Save/Send, Cancel and return. Equal character-group sizes mean no undersized last group, duplicates or dummy selectable padding. Editor controls are separate from the alphabet. All required text/actions must fit measured OLED geometry and glyph availability.

Include transition/priority tables for short, double, emergency hold, timeout/blank, wake, receive, team/recipient change and backend failure. Respect the classifier's actual double-click timing. Do not claim typing speed without measurement or change timing silently. Retain drafts through blanking; consume wake; receive must not steal navigation. Emergency pre-emption cannot later auto-submit the interrupted draft: revalidate and require fresh confirmation.

Specify one bounded draft's owner, lifetime and caller-specific byte capacity, including queued submission. A pointer into an expired editor/callback buffer is insufficient. Do not encode manual text as a fake preset slot/generation. Define empty/all-space/full-buffer behavior, cursor limits and how an unsupported existing name can be reviewed without loss. No per-character persistent writes.

### C. Ordinary messages, multiline text and catalog migration

Keep preset selection as the fast path and offer manual ordinary DM/team composition even when no ordinary preset is enabled, subject to the real send admission. One shared normal submission service is preferred; trace current checks and preserve them instead of bypassing the preset gate. Bind review to destination/team, exact bytes, location policy and, for presets, slot/generation. On changing context, invalidate or re-confirm; never retarget silently.

Produce a capacity derivation table covering DM versus team channel, sealed/location variants, wire/crypto overhead, parser and command-string limits, C terminators, persisted record size and editor/UI copies. Show equations and source symbols. The existing 96-byte command composer is one constraint, not an approved future maximum. The new bound must allow the 18-byte example but must not be an arbitrary large constant borrowed from one transport arm.

Separate **presentation wrapping** from authored newline bytes. Define word wrapping, long words, page boundaries/cadence/navigation, full review and exact transmitted bytes. Rendering must neither insert payload newlines nor discard/duplicate bytes. Page advance must not send. Decide deliberately whether this phase adds authored paragraph breaks; that also affects grammar, escaping and controls.

Design `/mrui` version/layout migration explicitly, separately from main `/mrcfg` v26. Preserve valid custom phrases, disabled slots, location flags, canonical padding and generation safety. Do not overwrite valid user records with new defaults. Define missing, invalid, old-valid, short/torn, I/O-error and failed-save/migration outcomes against actual backends; explain recovery and when the upgraded record becomes authoritative. Merely increasing `kUiPresetVersion` is insufficient. Include mandatory emergency-slot behavior and the impact of longer text on emergency rendering/tracking.

Define user-visible outcome wording according to evidence: admission/queueing, actual airtime, relay/custody result, any supported endpoint evidence and human reply are different. Do not invent whole-team delivery acknowledgements or claim “everyone received.” Preserve emergency repeat/hold behavior and current ordinary/emergency tracker separation.

### D. Optional Home preview

Develop a reviewable recommendation without treating it as approved. If included, resolve every condition in the existing design §§7.1–7.3:

- Eligibility: readable, opened sealed posts for the current nonzero team and intended UI channel. Exclude internal, foreign-team, wrong-channel and cleartext traffic. Do not equate shared-team authentication with an individual identity or organizer role.
- Provenance: retained channel records lack a stable sender hash. Use an honest origin label; do not retrospectively identify an old sender from today's roster/local ID. An app-level acknowledgement/provenance protocol is outside this scope unless separately ruled.
- Lifetime: bind to record kind/epoch/sequence and team, copy borrowed data safely, freeze visible content/metadata during selection and frame rendering, and handle deletion/eviction as gone rather than opening a different record.
- Attention: specify startup/history eligibility, multiple arrivals, offer ordering/retirement, blank/wake, team changes and explicit access to other messages. Reuse bounded Inbox storage; price any retirement metadata rather than creating a second inbox.
- Actions: Open must target that exact record. Hide, render and wake must not acknowledge/delete/mark-read or send. Preserve current list-watermark semantics unless a separately explicit change is approved. Audit B191's four-per-kind browse limit so a selected preview remains reachable.
- Interruption: preview is not a distress confirmation and does not extend panel-on time by itself. No keyword privilege for `Return to base now` or other message text. Direct-message preview remains a separate decision.

### E. Splash, chrome and retained contracts

If recommending a splash, make it boot-only, nonblocking, dismissible with consumed input, and compatible with radio service, emergency input and visible boot faults. No splash on ordinary display wake. Show exact geometry for Home/editor/review/attention with longest strings, selected markers, badges and Back paths. A drawing is not proof that a sixth row fits.

Explicitly map all changed and retained earlier rulings:

| Earlier authority | Required treatment |
| --- | --- |
| UI-17 R-1 | Preserve compose/detail across blank; consume wake; retain emergency exception |
| UI-17 R-2 | Preserve Team ordering; no sorting change hidden in Home work |
| UI-17 R-3 | Preserve configuration badge/Settings text split and visible restart requirement; new generic unsaved/conflict Home text needs an explicit revision |
| UI-17 R-4/R-5 | Account for body/rail mapping, parent Back paths and terminal acknowledgement by either press; name any proposed reversal |
| UI-17 R-6/R-7 | Preserve accepted chatty receive power behavior/no rate limiter and existing wake scope; preview is not permission to change either |
| Preset OQ-A | Its single-row 17-byte rationale is superseded for this redesign by the owner's multiline direction; retain other catalog invariants unless explicitly revised |
| UI-16 | Reuse provisioning authorization, scope, confirmations and actual saved-key/grant behavior |
| B118 / B191 | App-ack direction and browse-depth limit remain separately recorded; evaluate dependencies without silently expanding into their implementation |

## 7. Resources, reachability and later validation

Give a resource model before requesting an allocation. Distinguish a declaration's size, resident object multiplicity, transient stack and final linked RAM/flash. Audit `UiState` in both model and frame copies, the frozen `UiSnapshot`, snapshot construction/return, editor ownership, catalog copies, preview metadata and call-path stack overlap. Measure native, Xtensa and ARM layouts when needed; don't infer board padding from native or describe a compile-only size as linked RAM. B246's adopted `tools/probe_board_abi.py` is the standing guard. This handoff grants no new byte budget and imports no remote-admin allocation.

State feature/profile boundaries and whether `lib/core`, wire or persistent layout changes are actually necessary. Predict corpus inertia only when source scope supports it; prediction is not a result. Wire changes are allowed if needed, with separate attribution under C4. Keep a pure preparatory refactor separate from feature behavior (C1). Plan complete later fences from all users, including tests and source-reading instruments (P7); adding a core translation unit requires its simulator source-list entry in that later authorized slice.

Create an acceptance matrix mapping each contract to a concrete automated seam, distinguishing existing proof, required new proof and hardware residue. Candidate instruments to inspect:

- Native tests: `test/test_firmware_ui_{input,model,status,chrome,send,presets,preset_verbs,team,geo,nearby,invite,prov,join}.cpp`; provisioning/join/keyring tests; `test/test_device_nv.cpp`; Inbox and both storage-backend tests. Expand brace names when checking paths.
- Real renderer/dispatch: `tools/probe_firmware_ui/run.sh`; board integration: `tools/probe_board_ui/run.sh`. Pure formatter/model tests alone do not prove that production rendering and command submission use the new logic.
- ABI/resources: `tools/probe_board_abi.py`, board measurement tool and profile builds. Re-derive pins and linked deltas for the eventual candidate, not from old UI or remote-admin receipts.
- Mutation coverage: `tools/probe_ui_model_mutations.py`. Derive both changed-source and dependency/historical selectors and justify the needed union. Controls must compile, match their intended boundary and fail for the intended reason; do not count VACUOUS/unusable controls as RED.
- Full implementation validation later follows the current rules and approved slice fence. Native means running the binary as well as the PIO wrapper; corpus/keystone comes from current `simulation/BASELINE.md`; required board builds are sequential. Reconcile later slice-specific board/census scope with the roles amendments rather than inventing a new gate here.

**Known instrument issues:** B418 records pre-existing board-UI W49/W51/W54 source-reader drift. Its historical counts are not a fresh reproduction in this handoff. Audit the present readers and explicitly plan the narrow instrument repair if this redesign depends on them; do not misattribute them to the feature, quietly weaken them or repair them during documentation work. B286 remains recorded for mutation-worker copy amplification: any staging workaround must preserve all real tracked/untracked inputs and deletions. Missing historical raw output is recoverable through the catalog, not grounds for skipping an instrument.

Minimum planned automated cases include:

| Contract | Cases and meaningful controls |
| --- | --- |
| Editor | Exact repertoire/equal groups/unique reachability; group/character wrap; Back/cursor/delete; empty/full/cap-plus-one; unsupported text; Done versus final Save/Send; cancel with zero effects |
| Interruption | Short/double boundary; blank/wake during every edit/review; receive without navigation theft; emergency pre-emption; no delayed automatic send |
| Name save | Exact bytes, full width, failure, no seed/position/team loss; unchanged live identity after failed save |
| Multiline/send | 17/18/actual-cap/cap-plus-one, page boundaries/long words, location variants, full review equals actual submitted bytes, no clipping and no send on page advance |
| Context/lifetime | Stale preset generation, changed team/recipient/roster, duplicate names, queued manual draft after editor exits; ordinary path never touches preset NV or emergency repetition |
| Catalog upgrade | Valid v1 custom/disabled/location data, fresh store, invalid/truncated/I/O failure, migration save failure, exact canonical bytes and emergency slot regressions |
| Preview if adopted | Wrong scope/encryption/internal records, old local-ID reassignment, deletion/eviction/epoch change, arrivals during selection, exact Open, Hide/read/wake separation, no reconstructed individual provenance |
| Production reachability | Drive real input/model/render/dispatcher seams and actual existing executor/admission; label synthetic backend fault injection explicitly |

Physical obligations belong in the **current scenario plan**: OLED readability, actual button timing/discovery, name/message entry with correction, two-device setup and identifying the intended peer, real ordinary radio traffic and truthful outcomes, interruption/blanking, power cost under traffic, flash/power-loss behavior and splash/service coexistence as applicable. Map onto existing scenarios first. Freeze exact expected panel/console strings with the eventual implementation brief; do not claim illustrative wireframes are current output or mark any metal row PASS.

## 8. Work sequence and completion criteria

1. **Reconstruct:** read the required documents, inventory the current tree and report facts before proposals (P1). State which parts of the existing design are agreements, historical facts and open mechanisms.
2. **Audit:** refresh every source claim by symbol. Trace complete name-save, preset/manual-submit, receive/Inbox, input/render and provisioning paths. Make a compact discrepancy/dependency list; register genuinely new findings with evidence (M1), avoiding duplicates. A design question alone is not a reproduced bug.
3. **Specify:** revise the canonical design with concrete states, navigation/priority tables, measured-layout wireframes, ownership/lifetime, capacities/migration, error behavior and resource pricing. Link a compact audit rather than pasting raw logs. Explicitly name earlier rulings any proposal changes.
4. **Resolve choices:** give the owner a short numbered decision list with recommendations and consequences. Continue independent source work while awaiting necessary choices. Do not infer approval from silence or label unmeasured sizes as granted allocations.
5. **Prepare review:** propose cohesive dependency-ordered implementation packages with the anticipated surfaces and gate obligations, respecting C1/C4/P6/P7. These are a plan for later QA briefs, not permission to start coding. Before an implementation brief is dispatched, obtain the required QA pre-check and independent review.
6. **Validate documentation:** check links/anchors, contradictions, byte counts, examples, scope and preservation of existing work. Report exactly what you inspected/calculated/ran and what remains unverified. Re-inventory the final preparation inputs. Keep the design DRAFT/owner-review-pending wherever decisions remain; never self-award independent QA PASS.

The handoff back to the owner should contain: the updated design link; a concise current-source/gap summary; settled versus pending decisions; resource and compatibility consequences; proposed implementation order; and the acceptance/metal matrix. Include explicit limits on any measurement. Update tracker/MEMORY with short pointers rather than a second status narrative (P5).

**B335 remains open after specification work.** Its closure requires the approved source-derived design, implemented capacity/migration/full-text behavior and independent QA, including actual-send and failure cases. No code, tool fix, gate verdict, hardware result or owner ruling may be implied merely because the new document is complete.

## 9. Suggested first response from the fresh agent

Report the current commit and dirty preparation inputs; confirm the spec-author scope; summarize the settled multiline/shared-editor requirements and the stale source context being refreshed. Then perform the source audit and develop concrete recommendations. Do not start by asking for already-granted permission, offering to implement, or requiring a clean tree/commit.
