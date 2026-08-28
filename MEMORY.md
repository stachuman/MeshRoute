# MeshRoute durable decisions

- **Deterministic board measurement (B138/B206 closed after independent QG, 2026-08-28):** build identity has one device-TU authority. Actionable
  RAM/flash comparisons use `tools/measure_board.py` with fixed epoch/revision, the same checkout and stable
  `.pio-measure/` build paths, one runner lock, exact source/toolchain/wrapper manifests, and two matching clean arms
  per measured ABI; ordinary `.pio/` must remain untouched. The lock does not cover source-mutating batteries, which
  remain operationally exclusive. Manifests must bind CC, CXX and LINK independently and an external literal test
  inventory must redden every omitted comparison. A normal `pio run` size line is informational. B253 is the next
  separately reviewed slice (untracked-source provenance + fail-loud Git); B246 board-ABI struct visibility remains
  separate.
- **Hybrid RTS closure authority (B157/B153, 2026-08-27):** B157 is closed on complete flight identity plus the
  restored exact implicit-forward credit. B163 has no separate implementation: B182 supersedes its time-windowed
  alias proposal with configured-logical identity plus separate wire correlation and fail-loud ambiguity. The old
  732/733 absolute delivery floor was frozen before B182 and is not a current gate. The owner accepts 757/1041 once
  to close B153; this is not a permanent floor. Current comparison baseline: raw 760; s06 110/148, s07 84/207, s22
  8/8, s27 15/15; residue census `1/11/44/3`; the 44 unresolved logical destinations are B252 measurement debt.
  Future movement in delivery, decisive rows, airtime, duplicates or residue needs explicit attribution. See
  `docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md`.
- **Hosted-mobile counter boundary (B251, 2026-08-27; closed, independent QG passed):** a qualifying plaintext
  static/global transit keeps the mobile counter on the mobile-home hop and forwards once under a newly allocated home
  counter. Direct by-id transit admits the outward queue and any correlation before its hop ACK. Hash-wrapper transit
  reserves correlation before that ACK, then treats `SendDispatch` as the sole outward admission authority: queued
  activates, parked retains the reservation until admission, and refused releases it without origin evidence. The
  broader B112 first-hop-ACK contract remains open. First-hop loop identity includes the verified hosted-mobile hash;
  reverse E2E-ACK correlation also binds the return peer and layer, never evicts a live row, and translates back to the
  mobile counter. Team-plane, ordinary static and CRYPTED traffic are unchanged. See
  `docs/superpowers/specs/2026-08-27-b251-home-counter-translation-design.md`.
- **B197/B198 UI sleep/wake (design pending review, 2026-08-14):** an OLED build may light-sleep only after active-low
  button GPIO wake is armed and the panel is blanked, `InputFsm` is inactive, and `FrameGate` has no open logical
  frame. Non-OLED builds return `true`; retain DIO1/timer wake and one-page-per-service-pass rendering. See
  `docs/superpowers/specs/2026-08-14-b197-b198-ui-sleep-wake-design.md`.
- **Heltec V4 port (V4-4, 2026-08-26; metal validation pending):** `heltec_v4`, `heltec_v4_mobile` and
  `gateway_heltec_v4` support original
  high-power V4.2/V4.3 in 863–928 MHz, detects GC1109/KCT8103L at runtime with a fail-closed two-pull check, enables
  the V4.3 receive LNA, and supports nominal conducted 22 dBm via SX1262 drive 10 dBm. The two derived images add
  only the existing mobile or gateway role flags. Metal proves the shared OLED Vext contract is GPIO36 active LOW on
  both V4.2 and V4.3; the provisional HIGH level fails the V4.3 panel ACK. V4 R8/low-power/433–510, GNSS, calibrated
  power tables and runtime LNA control remain outside this slice. See
  `docs/superpowers/specs/2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md`.
- **Roster grant return context (B250, 2026-08-26):** the reused N5/N6 grant chain carries one private, explicit
  caller authority. Invitation-origin navigation stays unchanged; TEAM-roster exits restore the entered roster by
  saved TEAM-local identity through the existing B64 follow/refuse authority. A matching roster-origin pubkey opens
  the existing REJECT-default confirmation. Missing callers fail closed, and unrelated provisioning close or
  emergency pre-emption retires the context; no parent is inferred from window, screen or sentinel fields. See
  `docs/superpowers/specs/2026-08-26-b250-roster-grant-return-context-design.md`.
