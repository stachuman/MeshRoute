# Getting Started

> Status: Skeleton. The Heltec V4 build/profile inventory is source-verified; flashing steps and observed output
> still require chapter and metal review.

## What you will accomplish

Choose the appropriate firmware build, flash one node, and verify that it starts successfully.

## Choose a board and firmware role

### Heltec WiFi LoRa 32 V4.2/V4.3

MeshRoute provides three firmware builds for the original high-power 863–928 MHz Heltec V4.2/V4.3. Choose the role
before building or flashing:

| Firmware environment | Choose it for | Important difference |
| --- | --- | --- |
| `heltec_v4` | A fixed, single-layer node or a full-feature development image | Keeps the full feature set, including remote management. |
| `heltec_v4_mobile` | A device carried by one person | Uses the dedicated mobile profile; remote management is not included. |
| `gateway_heltec_v4` | A fixed node bridging two network layers | Uses the dedicated dual-layer gateway profile and does not run the mobile-member or team planes. |

All three use the same board support and automatically distinguish V4.2 from V4.3 at boot. They are not compatible
aliases for V4 R8, the low-power/no-FEM board, or the 433–510 MHz model. Attach the correct antenna or a rated dummy
load before powering firmware that may transmit.

The `heltec_v4_mobile` name does not mean GNSS is available. L76K GNSS support is planned separately and is not part
of these builds yet.

**Pending verification:** these build choices are implemented and compile-tested, but the V4 operating instructions
remain unpublished until the V4.2/V4.3 metal checklist is complete.

<!-- Add the source-verified selection table for the other supported board families here. -->

## Prepare the flashing tools

<!-- Add supported host prerequisites and installation checks here. -->

## Build and flash

<!-- Add one verified procedure per supported board family here. -->

## Verify first boot

<!-- Add exact, current boot evidence and the first safe interaction here. -->

## Common flashing problems

<!-- Add only reproduced or source-supported recovery steps here. -->

## Next step

Continue to [Connections](02-connections.md), then return to your selected journey in [Choose Your Path](00-choose-your-path.md).
