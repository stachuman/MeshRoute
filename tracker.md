# MeshRoute tracker

Last refreshed: **2026-09-06**

This file records only project-level status. Implementation detail belongs in the linked specification or plan;
individual defects belong in `docs/2026-07-30-open-bug-register.md`.

## Marked as implemented
- `2026-07-31-onboard-oled-ui-design.md` / `2026-07-31-onboard-oled-ui-phase-a.md` — Phase A is implemented and
  metal-tested. UI-15/UI-16/UI-17 and configurable presets subsequently landed under their dedicated specs;
  remaining defects and optional hardware checks are tracked separately.

- `2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md` — V4 implementation slices landed; remaining
  hardware qualification is tracked separately.

- `2026-08-23-internal-data-and-custody-outcome-design.md` — slices A0–G are implemented and QG-passed; B59 is
  fully closed after bench Part 53 passed on metal on 2026-09-01. Optional Slice H (`DM UNCERTAIN` user-send
  presentation) remains deliberately parked; adjacent findings stay in the bug register.

- `2026-09-01-b278-mobile-custody-feedback-design.md` — the mobile/static custody extension on top of that v1
  arc, **tracked separately from it**: slices S0–S5 are implemented and QG-passed and B278 is
  **SOFTWARE-COMPLETE / METAL-PENDING**. It closes only when bench **Part 54** passes on real radios. It adds
  no custody generator: team-plane, cross-layer and hosted-last-mile custody generation stay out of scope,
  and [[B112]] remains open and separate.


## Backlog — priority order
- GPS: 2026-08-25-heltec-v4-mobile-l76k-gnss-and-automatic-location-design.md - to be reviewed

- 2026-08-07-mobile-home-attachment-reliability-design.md — core S0–S5 mostly landed. Resume with B151 late-home/
  auto-OFF scenarios, finish S6 product integration and then evaluate narrowed B178 proactive roaming. B184 and
  B186b remain separate adjacent follow-ups.

-  **DESIGN PASS 2026-09-04** - `2026-08-23-remote-admin-independent-rpc-design.md` — remote admin v2;
  implementation authority, with every §19 slice still requiring its own Quality-Agent-passed brief. The
  pre-feature implementation is committed through 0c (`b942c37`). **Slice 1 software-complete / QA-passed
  2026-09-06**, committed at `5d2c00e`: the capability scaffold is inert on both ruled boards and all 36
  streams. B304's header half is closed (ini sibling remains); B305 is closed; B306 records the stale
  `node_mac.cpp` citation. **Slice 1b software-complete / QA-passed 2026-09-06**, owner closure commit `cc35137`:
  strict receive owners, no legacy widening; native 2615/111354/0, mutation union 99/99 RED, 36/36 byte-identical
  streams after both simulator variants rebuilt, RAM unchanged and flash gateway +16 / heltec_mobile −8 fully
  attributed. Evidence: `docs/superpowers/evidence/2026-09-06-radmin-slice1b.md`. B307 closed; B311 registers the
  non-UTF-8 mutation-worker failure, and B286 carries the new disk-pressure measurement. B310 remains parked.
  **Slice 2 software-complete / implementation QA PASS 2026-09-06, no fold-ins:** native 2640/115288/0,
  independent reference 87/87, mutation union 66+5 = 71/71 RED, 0 unusable, 36/36 byte-identical anchored
  streams. Both simulator archives compile the codec; the executable and both ruled board ELFs remain
  byte-identical, with no runtime codec consumer. Six probes/ABI/inventory/tools312/census/checkers pass.
  Evidence `docs/superpowers/evidence/2026-09-06-radmin-slice2.md` and code are committed at `f2735f7`;
  Author closure is committed at `231e1be`. Simulator `8688884` contains the one-line source-list addition.
  Measured dispatch bases were `9ea4947` / `fd3295d`; all delivered file hashes are unchanged.
  B308/B309 closed; B312 codec half complete, real entropy adapter still open. B314 naming
  hazard avoided/closed, B315 output-path validation open, B316 duplicate folded into open B286 with rsync
  citation corrected to line 10114. B311/B313 remain open; B310 remains parked.
  **Slice 3 preparation:** QA pre-check plus R-RA-29 are committed at `7299eb9`. The Author brief
  `docs/superpowers/plans/2026-09-06-radmin-slice3-target-stores.md` is **DRAFT — sole QA HOLD fold-in applied,
  changed sections awaiting confirmation**, pinned to
  that existing base and unchanged simulator `8688884`. It fixes the fingerprint, target-only USB surface,
  no-resident-state records/services, explicit invalid recovery and honest flash/RNG limits; no remote
  execution. Part 55a is drafted, not run. B317 records self-heal erasure; B318 is closed by QA's ledger
  corrections and accepted router/guard gate split. B319 tracks the explicitly fenced inventory ACCEPT
  profile column, its fixtures and re-derived per-profile ownership counts. **Next:** QA confirms the
  changed brief sections, owner Author-preparation
  commit, Author explicit base repin, then QA dispatch. No implementation is claimed for Slice 3.
  Existing hardware debts remain separate; no metal is added by 1/1b/2. Earlier 0c documentation residue
  (B298 closure / Part 60 landing) is not discharged by the Slice 1/1b landings. B292 and B283 remain separate.
  **Slice 4 advance draft, 2026-09-06:** owner reports Slice 3 running (the preparation status above is
  historical). Pre-check plus R-RA-30 authorize drafting ahead, not implementation overlap. Brief
  `docs/superpowers/plans/2026-09-06-radmin-slice4-controller-stores.md` is NON-DISPATCHABLE; base and
  delivered anchors/pins wait for Slice 3 QA PASS and the owner's closure commit. Author §6.3–6.7
  decisions cover the 64-byte row/2056-byte client scratch, admin-target pages, real mobile router arm,
  literal CLIENT profile/census, exact regen warning and Parts 55b/56 (draft, not run). R-RA-30's
  xiao_mobile capture is one-off, not an expanded ruled pair. B320 is closed by QA's in-place profile
  correction. Preliminary gate otherwise PASS pending closure; sole B321 layering fold-in applied:
  direct monocypher.h include and decoder-local crypto_wipe scope guard, no NV/team-keyring include
  or guard relocation. B321's implementation remains open, including attributed flash cost. Next:
  resolve closure checklist/base, then final QA re-gate. No dispatch authorization or software PASS.
  
- 2026-08-08-hybrid-rts-flight-identity-design.md — core S1–S6 landed. B251's home-counter boundary and B161's
  canonical typed-answer origin passed combined QG and are closed. The final current-tree audit closes B157, and the
  owner's one-time acceptance of B182's 757/1041 closes B153 without establishing a permanent floor. B163 is
  superseded, not a live dependency; B252 retains the 44 unresolved-destination measurement debt. B112 remains
  separately open and does not block B251. Fix B166 NAV under-reservation later;
  treat B158 as a separate MeshRoute-native jitter redesign.

- 2026-08-05-channel-app-code-draft.md — design-only and unimplemented. Refresh against the new DATA-type namespace
  (DATA_TYPE_APP_MESSAGE=0x05), settle the transport, authentication, lifetime, plaintext and DM-scope rulings, then
  implement envelope/codec/storage → B118 tracker → OLED/companion actions

- `2026-08-03-multi-gateway-explicit-layer-path-routing-design.md` — extend explicit cross-layer routing beyond
   the currently limited path.
- `2026-07-26-companion-v1-feature-roadmap.md` — audit roadmap coverage, then plan missing companion work,
   especially remote administration and map/location UX.

-  `2026-08-01-full-firmware-source-review-vectors.md` — perform the systematic firmware review.

## Bugs - suggested order

- **NEXT QUEUE:** (1) bench **Part 54** on metal — the single item that closes B278; (2) owner reports
  **Slice 3 running**: QA gates the coder's report, then Author landings / owner closure; (3) resolve the
  advance Slice 4 draft's closure anchors/base and QA-gate it before dispatch. No dirty-input exception,
  reused Slice 2 base or overlapping implementation. Parts 55a/55b/56 remain draft until gated transcripts.
  Bench Part 58 closes
  B208's product residue independently. [[B280]], [[B281]], [[B282]], [[B283]], [[B292]] and [[B112]] remain
  separately tracked and do not block Part 54.

- B35 — resolve channel self-skip plane correctness separately; it does not block the custody arc.

- B252 measurement debt — later classify the 44 unresolved logical destinations under B182 authority; preserve the
  `1/11/44/3` fail-loud baseline and do not fold this non-blocking work into a routing/protocol change.

- B151 → B178 → B186b — resume mobile-home reliability after the custody arc.



### Hardware backlog

10. `2026-07-14-t1000e-feasibility.md` — revisit T1000E feasibility after the current Heltec work.

## Done — implementation

- `2026-08-27-b206-b138-deterministic-board-measurement-design.md`,
  `2026-08-27-b253-untracked-build-provenance-design.md` and B205 — gate-reliability arc complete and QG-closed;
  deterministic board-delta evidence is certified. B246 remains a separate next-step guard.

-  2026-08-20-status-screen-redesign-note - change of initial screen content

- `2026-07-26-team-encrypted-channel-design.md` — implemented; metal regression remains in the bench checklist.
- `2026-07-27-cts-len6-cr2-design.md` — implemented.
- `2026-07-29-peer-address-book-design.md` — implemented.
- `2026-07-30-channel-crypt-and-location-privacy-design.md` — implemented.
- `2026-07-31-node-role-model-design.md` — implemented.
- `2026-08-01-id-to-hash-resolution-design.md` — implemented.
- `2026-08-13-tx-completion-path-design.md` — T1/T2 implemented and QG-approved; metal evidence is part of the
  active Heltec run.
- `2026-08-14-t3-app-ui-send-aired-spec.md` — implemented and QG-approved; metal evidence is part of the active
  Heltec run.
- `2026-08-15-heltec-mobile-status-navigation-ui-design.md` — CHROME-1…4 implemented; Parts 24-25 metal evidence is
  part of the active Heltec run.

## Working references — not backlog items

- `docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md` — current R1-R6 Heltec execution guide.
- `docs/2026-07-31-bench-test-script.md` — general firmware bench checklist.
- `docs/2026-08-11-mobile-home-metal-test-guide.md` — mobile-home-specific hardware scenarios.
- `docs/2026-07-30-open-bug-register.md` — continuous defect register.
