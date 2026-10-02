# MeshRoute tracker

Last refreshed: **2026-09-28**

This file records only project-level status. Implementation detail belongs in the linked specification or plan;
individual defects belong in `docs/2026-07-30-open-bug-register.md`.

## Marked as implemented

- [2026-07-31-onboard-oled-ui-design.md](docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md) / [2026-07-31-onboard-oled-ui-phase-a.md](docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md) — Phase A is implemented and
  metal-tested. UI-15/UI-16/UI-17 and configurable presets subsequently landed under their dedicated specs;
  remaining defects and optional hardware checks are tracked separately.

- [2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md](docs/superpowers/specs/2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md) — V4 implementation slices landed; remaining
  hardware qualification is tracked separately.

- [2026-08-23-internal-data-and-custody-outcome-design.md](docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md) — slices A0–G are implemented and QG-passed; B59 is
  fully closed after bench Part 53 passed on metal on 2026-09-01. Optional Slice H (`DM UNCERTAIN` user-send
  presentation) remains deliberately parked; adjacent findings stay in the bug register.

- [2026-09-01-b278-mobile-custody-feedback-design.md](docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md) — the mobile/static custody extension on top of that v1
  arc, **tracked separately from it**: slices S0–S5 are implemented and QG-passed and B278 is
  **SOFTWARE-COMPLETE / METAL-PENDING**. Its former Part 54 obligations now live in the metal plan's
  [CUSTODY-02/03](docs/2026-09-20-metal-test-plan.md#custody-02). It adds
  no custody generator: team-plane, cross-layer and hosted-last-mile custody generation stay out of scope,
  and [[B112]] remains open and separate.

- **Remote admin v2 is software-complete**, including Slice 10 and the B434/B435/B436 harness follow-up.
  Current dispatch and per-slice gates live in [register §0](docs/2026-07-30-open-bug-register.md#0-current-remote-admin-dispatch--2026-09-18)
  and [design §19.1](docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md#191-per-slice-gate-ownership);
  remaining hardware qualification is in the [metal plan](docs/2026-09-20-metal-test-plan.md#radmin-01).

## Backlog — priority order

- [Standalone mobile Home/editor design, revision 2.24](docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md#13-proposed-implementation-packages-for-qa-briefs--not-a-frozen-slice-list) — package status and next dispatch live there and in [register §0](docs/2026-07-30-open-bug-register.md#0-current-remote-admin-dispatch--2026-09-18); current: W0 — [independent QA receipt](docs/superpowers/evidence/2026-09-30-standalone-mobile-home-w0-qa.md); next: the owner-ruled B478/B487/B488 tool package, with B490 — [brief](docs/superpowers/plans/2026-09-30-b478-b487-b488-tool-repairs.md) revision 4 for QA's scoped re-review — the owner folded B492's F07 fix in (2026-10-02) after the [independent QA HOLD](docs/superpowers/evidence/2026-10-01-b478-b487-b488-qa.md) on B491 (invalid-ledger refusal) and B492 (F07 count instability) — then W7 after the repair gate. W0 physical persistence is [USB-BLE-03](docs/2026-09-20-metal-test-plan.md#usb-ble-03), still OWED; B489 is a separate stack-evidence follow-up. See the status authorities for closures and remaining work. Commits are not progress gates.

- [Heltec V4 L76K GNSS/location design](docs/superpowers/specs/2026-08-25-heltec-v4-mobile-l76k-gnss-and-automatic-location-design.md) — first-review draft; ready for final review and implementation planning.

- [2026-08-07-mobile-home-attachment-reliability-design.md](docs/superpowers/specs/2026-08-07-mobile-home-attachment-reliability-design.md) — core S0–S5 mostly landed. Resume with B151 late-home/
  auto-OFF scenarios, finish S6 product integration and then evaluate narrowed B178 proactive roaming. B184 and
  B186b remain separate adjacent follow-ups.

- [2026-08-08-hybrid-rts-flight-identity-design.md](docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md) — core S1–S6 landed. B251's home-counter boundary and B161's
  canonical typed-answer origin passed combined QG and are closed. The final current-tree audit closes B157, and the
  owner's one-time acceptance of B182's 757/1041 closes B153 without establishing a permanent floor. B163 is
  superseded, not a live dependency; B252 retains the 44 unresolved-destination measurement debt. B112 remains
  separately open and does not block B251. Fix B166 NAV under-reservation later;
  treat B158 as a separate MeshRoute-native jitter redesign.

- [2026-08-05-channel-app-code-draft.md](docs/superpowers/specs/2026-08-05-channel-app-code-draft.md) — design-only and unimplemented. Refresh against the new DATA-type namespace
  (DATA_TYPE_APP_MESSAGE=0x05), settle the transport, authentication, lifetime, plaintext and DM-scope rulings, then
  implement envelope/codec/storage → B118 tracker → OLED/companion actions

- [2026-08-03-multi-gateway-explicit-layer-path-routing-design.md](docs/superpowers/specs/2026-08-03-multi-gateway-explicit-layer-path-routing-design.md) — extend explicit cross-layer routing beyond
   the currently limited path.
- [2026-07-26-companion-v1-feature-roadmap.md](docs/superpowers/specs/2026-07-26-companion-v1-feature-roadmap.md) — audit roadmap coverage, then plan missing companion work,
   especially remote administration and map/location UX.

- [2026-08-01-full-firmware-source-review-vectors.md](docs/2026-08-01-full-firmware-source-review-vectors.md) — perform the systematic firmware review.

## Bugs - suggested order

The standalone Home/editor design is REVIEWED; implementation proceeds through separately checked packages.
The current package status and dispatch are linked above. Existing backlog priorities remain unchanged.

- **NEXT QUEUE:** owner metal qualification follows [the maintained plan](docs/2026-09-20-metal-test-plan.md): CUSTODY-02/03 retains B278's closure obligation; RADMIN and USB-BLE carry the completed remote-admin arc's physical residue. Current per-board results live only in that plan.
  USB-BLE-01 (former Part 58) closes B208's product residue independently. [[B280]], [[B281]], [[B282]], [[B283]], [[B112]]
  remain separately tracked and do not block B278's metal qualification.

- B35 — resolve channel self-skip plane correctness separately; it does not block the custody arc.

- B252 measurement debt — later classify the 44 unresolved logical destinations under B182 authority; preserve the
  `1/11/44/3` fail-loud baseline and do not fold this non-blocking work into a routing/protocol change.

- B151 → B178 → B186b — resume mobile-home reliability after the custody arc.


### Hardware backlog

- [2026-07-14-t1000e-feasibility.md](docs/2026-07-14-t1000e-feasibility.md) — revisit T1000E feasibility after the current Heltec work.

## Done — implementation

- [2026-08-27-b206-b138-deterministic-board-measurement-design.md](docs/superpowers/specs/2026-08-27-b206-b138-deterministic-board-measurement-design.md),
  [2026-08-27-b253-untracked-build-provenance-design.md](docs/superpowers/specs/2026-08-27-b253-untracked-build-provenance-design.md) and B205 — gate-reliability arc complete and QG-closed;
  deterministic board-delta evidence is certified. B246 is also closed; its standing ABI guard is
  `tools/probe_board_abi.py`.

- [2026-08-20-status-screen-redesign-note](docs/superpowers/plans/2026-08-20-status-screen-redesign-note.md) — initial screen content updated.

- [2026-07-26-team-encrypted-channel-design.md](docs/superpowers/specs/2026-07-26-team-encrypted-channel-design.md) — implemented; current metal qualification is in the maintained metal plan.
- [2026-07-27-cts-len6-cr2-design.md](docs/superpowers/specs/2026-07-27-cts-len6-cr2-design.md) — implemented.
- [2026-07-29-peer-address-book-design.md](docs/superpowers/specs/2026-07-29-peer-address-book-design.md) — implemented.
- [2026-07-30-channel-crypt-and-location-privacy-design.md](docs/superpowers/specs/2026-07-30-channel-crypt-and-location-privacy-design.md) — implemented.
- [2026-07-31-node-role-model-design.md](docs/superpowers/specs/2026-07-31-node-role-model-design.md) — implemented.
- [2026-08-01-id-to-hash-resolution-design.md](docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md) — implemented.
- [2026-08-13-tx-completion-path-design.md](docs/superpowers/specs/2026-08-13-tx-completion-path-design.md) — T1/T2 implemented and QG-approved; current physical qualification is DM-03/UI-15 in the metal plan.
- [2026-08-14-t3-app-ui-send-aired-spec.md](docs/superpowers/specs/2026-08-14-t3-app-ui-send-aired-spec.md) — implemented and QG-approved; current physical qualification is DM-03/UI-15 in the metal plan.
- [2026-08-15-heltec-mobile-status-navigation-ui-design.md](docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md) — CHROME-1…4 implemented; former Parts 24–25 map to UI/POWER scenarios in the metal plan.

## Working references — not backlog items

- [Metal test plan](docs/2026-09-20-metal-test-plan.md) — current scenario procedures and the single hardware-result table; the [disposition map](docs/2026-09-20-metal-test-triage.md) maps old Part/guide IDs to preserved archives.
- [Bug register](docs/2026-07-30-open-bug-register.md) — continuous defect register.
- [Evidence retention and recovery](docs/superpowers/evidence/README.md) — current raw-output policy and recovery of retired run files.
