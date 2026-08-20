# MeshRoute User Manual

> Status: Skeleton. Chapters are reviewed incrementally against current source and metal-test evidence before they are marked complete.

This manual is for people who install, configure, and operate MeshRoute nodes. New users can begin with a real-world goal; experienced users can open an individual task or reference chapter directly.

## Start here

1. [Choose the path that matches your goal](00-choose-your-path.md).
2. Follow the matching guided journey from preparation through a working message test.
3. Use the task chapters for detailed procedures and the command reference for exact syntax.

## Guided journeys

| I want to... | Journey | Documentation status |
| --- | --- | --- |
| Use a mobile device on an existing network | [Mobile on an existing network](journeys/mobile-on-existing-network.md) | Skeleton |
| Install or add a fixed network node | [Install a static node](journeys/install-static-node.md) | Skeleton |
| Create or join a group | [Create or join a team](journeys/create-or-join-team.md) | Skeleton |
| Prepare several people and devices for a trip | [Prepare a hiking group](journeys/hiking-group.md) | Skeleton |

Journey pages provide an end-to-end sequence and link to the canonical task instructions. They do not duplicate command syntax.

## Task chapters

| Chapter | Scope | Documentation status |
| --- | --- | --- |
| [Choose your path](00-choose-your-path.md) | Starting situations and guided journeys | Skeleton |
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
