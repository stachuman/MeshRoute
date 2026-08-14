# MeshRoute tracker

Last refreshed: **2026-08-14**

This file records only project-level status. Implementation detail belongs in the linked specification or plan;
individual defects belong in `docs/2026-07-30-open-bug-register.md`.

## Ongoing

- `2026-07-31-onboard-oled-ui-design.md` / `2026-07-31-onboard-oled-ui-phase-a.md` — implementation through
  UI-14 and TX-completion T3 passed QG. Run the refreshed R1-R6 Heltec metal qualification, fix confirmed findings,
  then continue with UI-15/UI-16 provisioning.

## Backlog — priority order

1. `2026-08-07-mobile-home-attachment-reliability-design.md` — resume after OLED metal testing with narrowed B178
   proactive roaming and S6; add B151/B184 scenarios only if still needed.
2. `2026-08-08-hybrid-rts-flight-identity-design.md` — resume with B161, close B153/B157, then address B158 jitter
   and B166 NAV pricing.
3. `2026-08-05-channel-app-code-draft.md` — define stable channel application codes, then emergency message and
   response types.
4. `2026-08-03-multi-gateway-explicit-layer-path-routing-design.md` — extend explicit cross-layer routing beyond
   the currently limited path.
5. `2026-07-26-companion-v1-feature-roadmap.md` — audit roadmap coverage, then plan missing companion work,
   especially remote administration and map/location UX.
6. `2026-08-05-b59-custody-failure-notice-design.md` — notify the sender when an intermediate node cannot continue
   forwarding; do not change routing as part of this slice.
7. `2026-07-26-remote-admin-challenge-response-design.md` — implement authenticated remote administration.
8. `2026-08-01-full-firmware-source-review-vectors.md` — perform the systematic firmware review.

### Hardware backlog

9. `2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md` — port and qualify Heltec V4 hardware.
10. `2026-07-14-t1000e-feasibility.md` — revisit T1000E feasibility after the current Heltec work.

## Done — implementation

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

## Working references — not backlog items

- `docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md` — current R1-R6 Heltec execution guide.
- `docs/2026-07-31-bench-test-script.md` — general firmware bench checklist.
- `docs/2026-08-11-mobile-home-metal-test-guide.md` — mobile-home-specific hardware scenarios.
- `docs/2026-07-30-open-bug-register.md` — continuous defect register.
