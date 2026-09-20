# Manual Review Notes

> Internal documentation-review companion. This file records uncertainties and evidence gaps; it is not operating guidance.

Resolved facts belong in the relevant manual chapter. Developer rationale remains in the existing engineering documentation rather than being copied into the user manual.

## Tracked items

| ID | Chapter | Question or uncertainty | Source checked | Metal evidence | Resolution |
| --- | --- | --- | --- | --- | --- |
| MAN-001 | Getting started | Confirm the complete supported board/build matrix before publishing it. The V4.2/V4.3 base, mobile, and gateway environment bindings are source-audited; their operating status remains pending metal. | `platformio.ini`, `lib/core/mr_features.h`, and V4 RF constraints checked 2026-08-26 | Pending for V4.2/V4.3 | Open; V4 rows drafted, not marked Available |
| MAN-002 | Connections | Establish the current scope of BLE metal validation before describing it as verified. | Pending | Pending | Open |
| MAN-004 | Choose your path | Confirm a concise user-facing explanation of device role, network participation, mobile attachment, and team membership. | Pending | Pending | Open |
| MAN-005 | Hiking group | Establish which field topologies are both implemented and metal-tested before recommending a group setup. | Pending | Pending | Open |
| MAN-006 | Command reference | Historical 2026-08-31 concern: help omitted `joinprofile`, `-K`, `control_sf`, `l1_bw`, `l1_cr` details and overstated the `rcmd` allow-list. Superseded: help is intentionally a bare per-build name index plus the manual pointer; topics and the legacy `rcmd` family are removed. | Current help, dispatch, parser, generated inventory and authority checks at `d11b5a9`, 2026-09-20 | No new metal claim | Resolved by current reference; B437 records the stale manual landing |
| MAN-007 | Command reference | BLE specializes some replies as JSON and streams other handlers as text. Current reference documents target/controller guards, the remote event contract and the special `cfg set` readback behavior. Do not describe the whole transport as uniformly JSON; remaining older-family output details still need review. | `fw_main.cpp`, `device_ble.h`, `firmware_remote_client.h/.cpp` at `d11b5a9`, 2026-09-20 | Pending | Open for remaining per-command detail/metal review |
| MAN-008 | Configuration | The common `cfg set` handler accepts the 15 dual-layer topology keys. Confirm the supported guidance for issuing them on a normal build. The older observation that help labels their use is superseded: current help has no descriptions. | `firmware_config.cpp` and `firmware_help.h` at `d11b5a9`, 2026-09-20 | Pending | Open |
| MAN-009 | Command reference | Decide whether host-side client subcommands belong in this reference or in the Connections chapter. They are intentionally outside the first node-command inventory. | Node command paths checked 2026-08-21 | Pending | Open |

## Resolved items

| ID | Resolution | Evidence |
| --- | --- | --- |
| MAN-003 | UI-15 is implemented and metal-qualified. The manual must describe its stored-profile static join and team-create workflows as available, not planned. | `firmware_ui_model.h`, `firmware_ui_prov.h`, `firmware_ui_join.h`, and the completed 2026-08-20 UI-15 metal walkthrough checked 2026-08-31 |

## Review checkpoints

- Skeleton and terminology
- User mental model, journey selection, and journey completion criteria
- Getting started and connections
- Static network and configuration workflows
- Mobile and team workflows
- Messaging and inbox workflows
- OLED behavior, diagnostics, recovery, and command reference
