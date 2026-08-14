# MeshRoute durable decisions

- **B197/B198 UI sleep/wake (design pending review, 2026-08-14):** an OLED build may light-sleep only after active-low
  button GPIO wake is armed and the panel is blanked, `InputFsm` is inactive, and `FrameGate` has no open logical
  frame. Non-OLED builds return `true`; retain DIO1/timer wake and one-page-per-service-pass rendering. See
  `docs/superpowers/specs/2026-08-14-b197-b198-ui-sleep-wake-design.md`.
