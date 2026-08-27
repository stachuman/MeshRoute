# MeshRoute tracker

Last refreshed: **2026-08-17**

This file records only project-level status. Implementation detail belongs in the linked specification or plan;
individual defects belong in `docs/2026-07-30-open-bug-register.md`.

## Marked as implemented
- `2026-07-31-onboard-oled-ui-design.md` / `2026-07-31-onboard-oled-ui-phase-a.md` — UI-14, TX-completion T3 and
  the status/navigation redesign are implemented. Finish the B196 sleep correction and QG, then run refreshed R1-R6
  plus Parts 19-25 metal qualification before UI-15/UI-16 provisioning.
  2026-07-31-onboard-oled-ui-design.md / 2026-07-31-onboard-oled-ui-phase-a.md — Phase A implementation complete and metal-tested; subsequent UI-15/UI-16/UI-17 and
  preset work are maintained in their dedicated specifications. Remaining defects and optional hardware qualification are tracked separately.

- `2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md` — port and qualify Heltec V4 hardware.


## Ongoing
  1. B159 contradicts a design premise. The design says existing DATA dedup prevents repeated delivery (docs/superpowers/specs/2026-08-23-internal-data-and-custody-
     outcome-design.md:876), while B159 records that dedup can expire inside the retry horizon. Either fix B159 first or explicitly design custody-record idempotence
     without relying on transport dedup.

  2. B134 needs an explicit product ruling. Custody outcomes can be implemented with the existing store abstraction, but on Heltec they will remain RAM-only and
     disappear on reboot. If persistent custody history on Heltec is required now, B134 must precede custody Slice C/G. Otherwise record that limitation and defer
     B134.

-  fault reason and internal data : `2026-08-23-internal-data-and-custody-outcome-design.md`

## Backlog — priority order
- GPS: 2026-08-25-heltec-v4-mobile-l76k-gnss-and-automatic-location-design.md - to be reviewed

- 2026-08-07-mobile-home-attachment-reliability-design.md — core S0–S5 mostly landed. Resume with B151 late-home/
  auto-OFF scenarios, finish S6 product integration and then evaluate narrowed B178 proactive roaming. B184 and
  B186b remain separate adjacent follow-ups.

-  reviewed and ready - `2026-08-23-remote-admin-independent-rpc-design.md` — remote admin v2
  
- 2026-08-08-hybrid-rts-flight-identity-design.md — core S1–S6 landed. B251's home-counter boundary and B161's
  canonical typed-answer origin passed combined QG and are closed. Re-evaluate B157/B153 next. B112 remains separately
  open and does not block B251. Fix B166 NAV under-reservation later;
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

  B250 — finish and land the roster-grant return-context fix.

  Bookkeeping closure — close B209, B207, B196, B164 and B193 from their existing
  QG/metal evidence; no further implementation expected.

  B206 → B138 → B205 — restore trustworthy build/measurement gates before the next
  large core and wire transition.

  B20/B21 → B159 → B35 — resolve silent-loss, duplicate-delivery and plane-correctness
  defects. B159 is a prerequisite for the custody design unless that design adds its
  own explicit idempotence mechanism.

  Re-evaluate B157 → re-evaluate B153 — B251/B161 are closed; finish the hybrid-RTS disposition before changing the
  DATA namespace and terminal-custody paths.

  2026-08-23-internal-data-and-custody-outcome-design.md — next major arc. Finalize
  the review, settle B159 and B134 dependencies, unpark B59, then implement slices A–H
  with separate gates and attribution.

  B151 → B178 → B186b — resume mobile-home reliability after the custody arc.

  B134 — move before custody Slice C/G if Heltec custody records must survive reboot;
  otherwise retain as product backlog. Then O4 and remaining product/UI work.



### Hardware backlog

10. `2026-07-14-t1000e-feasibility.md` — revisit T1000E feasibility after the current Heltec work.

## Done — implementation

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
