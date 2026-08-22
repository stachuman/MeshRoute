# Manual Review Notes

> Internal documentation-review companion. This file records uncertainties and evidence gaps; it is not operating guidance.

Resolved facts belong in the relevant manual chapter. Developer rationale remains in the existing engineering documentation rather than being copied into the user manual.

## Open items

| ID | Chapter | Question or uncertainty | Source checked | Metal evidence | Resolution |
| --- | --- | --- | --- | --- | --- |
| MAN-001 | Getting started | Confirm the supported board/build matrix before publishing it. | Pending | Pending | Open |
| MAN-002 | Connections | Establish the current scope of BLE metal validation before describing it as verified. | Pending | Pending | Open |
| MAN-003 | Heltec OLED | Recheck the boundary between existing OLED behavior and unfinished UI-15 work when drafting the chapter. | Pending | Pending | Open; UI-15 remains planned |
| MAN-004 | Choose your path | Confirm a concise user-facing explanation of device role, network participation, mobile attachment, and team membership. | Pending | Pending | Open |
| MAN-005 | Hiking group | Establish which field topologies are both implemented and metal-tested before recommending a group setup. | Pending | Pending | Open |
| MAN-006 | Command reference | The built-in help omits `joinprofile`, the `control_sf` alias, `l1_bw`, and `l1_cr`; its `rcmd` wording is broader than the target allow-list. The manual inventory follows the handlers. Any firmware-help correction is a separate task. | `firmware_commands.cpp`, `firmware_config.cpp`, `firmware_remote.cpp` checked 2026-08-21 | Pending | Open |
| MAN-007 | Command reference | BLE specializes some replies as JSON, streams other commands through the text fallback, and refuses `help`/`?` plus every argument-bearing `peers` form (including `peers all`). Audit each command's BLE output before describing the transport as uniformly JSON. | `fw_main.cpp` checked 2026-08-21 | Pending | Open |
| MAN-008 | Configuration | The common `cfg set` handler accepts the 15 dual-layer topology keys even though built-in help labels them gateway-only. Confirm the supported guidance for issuing them on a normal build. | `firmware_config.cpp` checked 2026-08-21 | Pending | Open |
| MAN-009 | Command reference | Decide whether host-side client subcommands belong in this reference or in the Connections chapter. They are intentionally outside the first node-command inventory. | Node command paths checked 2026-08-21 | Pending | Open |

## Resolved items

| ID | Resolution | Evidence |
| --- | --- | --- |
| _None yet_ |  |  |

## Review checkpoints

- Skeleton and terminology
- User mental model, journey selection, and journey completion criteria
- Getting started and connections
- Static network and configuration workflows
- Mobile and team workflows
- Messaging and inbox workflows
- OLED behavior, diagnostics, recovery, and command reference
