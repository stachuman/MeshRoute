# MeshRoute User Manual

> Status: Skeleton. Chapters are reviewed incrementally against current source and metal-test evidence before they are marked complete.

This manual is for people who install, configure, and operate MeshRoute nodes. Start with the quick-start path, or open an individual chapter when you need a specific task.

## Quick-start path

1. [Choose a build, flash a node, and verify first boot](01-getting-started.md).
2. [Connect over USB or BLE](02-connections.md).
3. [Create or join a static network](03-static-networks.md).
4. [Send a direct or channel message](06-messaging.md).
5. [Handle received messages](07-inbox.md).

## Chapters

| Chapter | Scope | Documentation status |
| --- | --- | --- |
| [Getting started](01-getting-started.md) | Build selection, flashing, and first boot | Skeleton |
| [Connections](02-connections.md) | USB console and BLE | Skeleton |
| [Static networks](03-static-networks.md) | Create, join, verify, and leave | Skeleton |
| [Mobile operation](04-mobile-operation.md) | Registration, gateways, and roaming | Skeleton |
| [Teams](05-teams.md) | Team lifecycle and key handling | Skeleton |
| [Messaging](06-messaging.md) | Direct and channel messages | Skeleton |
| [Inbox](07-inbox.md) | Reading, marking, and deleting messages | Skeleton |
| [Heltec OLED](08-heltec-oled.md) | Controls and implemented screens | Skeleton; UI-15 provisioning is planned |
| [Configuration](09-configuration.md) | Settings, persistence, reset, and restart | Skeleton |
| [Diagnostics and recovery](10-diagnostics.md) | Status, faults, recovery, and common errors | Skeleton |
| [Command reference](command-reference.md) | Concise, source-verified command catalogue | Skeleton |

## Availability labels

The completed manual will use these labels consistently:

- **Available**: implemented behavior verified against current production source.
- **Board-specific**: available only on the named hardware.
- **Build-specific**: available only when the named firmware role or feature is built.
- **Planned**: not currently presented as usable behavior.
- **Pending verification**: excluded from operating instructions until its source and, where necessary, metal behavior have been checked.

UI-15 on-device provisioning is **planned**. It must not be treated as an available OLED workflow until its implementation and metal validation are complete.

Unresolved documentation questions are kept in [review notes](review-notes.md), separate from user instructions.
