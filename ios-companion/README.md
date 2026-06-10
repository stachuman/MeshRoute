# MeshRoute iOS Companion

A native iOS app that is the human face of a MeshRoute node: connect over BLE to read
received messages (DM threads + channel feeds), send DMs + channel posts, view node status,
and (later) update firmware OTA. The node is **name-agnostic** — it knows short ids +
`key_hash32`, never names — so **the app owns the contact/identity map** and the durable archive.

Design: `../docs/superpowers/specs/2026-06-10-ios-companion-app-design.md`.
Firmware-side contract: `../docs/superpowers/specs/2026-06-10-ble-companion-ota-inbox-design.md`.

## Layout

```
ios-companion/
├── MeshRouteKit/                 Swift package — pure, headless-testable (`swift test`)
│   ├── Sources/MeshRouteWire/    the wire contract (no Foundation UI deps)
│   │   ├── Command.swift           app→node LINE-ASCII command encoder
│   │   ├── Inbound.swift           node→app JSON push decoder (Codable)
│   │   ├── KeyHash.swift           key_hash32 (hex8 ↔ u32, flexible Codable)
│   │   └── LineAccumulator.swift   reassemble BLE chunks into '\n'-delimited lines
│   └── Sources/MeshRouteCore/    domain + transport seam
│       ├── Models.swift            Contact, ThreadKey, ChatMessage, ContactBook
│       ├── ConversationStore.swift dedup-by-(origin,ctr) + delivery-state logic
│       ├── NodeLink.swift          the transport protocol (raw lines + state)
│       ├── NodeSession.swift       encode/decode on top of any NodeLink
│       └── MockNodeLink.swift      a fake node that speaks the real contract
└── MeshRouteCompanion/           the SwiftUI app (CoreBluetooth + SwiftData) — TODO
```

The split mirrors the firmware's `lib/core` (native-testable) vs device-HAL discipline: all
protocol + domain logic is proven with `swift test` on macOS; CoreBluetooth / SwiftData / SwiftUI
live only in the app target.

## The BLE contract (chosen on review: line-ASCII commands + JSON pushes)

- **Transport:** the node's Nordic UART Service-compatible `bleuart` GATT (RX write, TX notify).
- **App → node:** line-ASCII console verbs, exactly what `console_parse.cpp` / `fw_main.cpp`
  accept (`send 2 hi`, `sendhash 8a3f1c02 hi`, `send_channel 3 gm`, `resolve <hex> [hard]`, `cfg`…).
- **Node → app:** newline-delimited JSON, exactly what `console_json.cpp` emits
  (`{"ack":"queued","ctr":5,"qd":0}`, `{"ev":"msg_recv","origin":2,"ctr":7,"body":"hi"}`, …).

> **Firmware gap (tracked):** today's firmware emits *human text* over USB-CDC and has no BLE
> companion yet — the JSON writers in `console_json.cpp` are unused. To talk to a real node the
> firmware needs (1) a NUS `bleuart` transport and (2) `write_push`/`write_ack` wired into the push
> drain. Until then the app runs end-to-end against `MockNodeLink`.

## Build & test

```sh
cd MeshRouteKit
swift test          # headless — protocol codec + domain logic, no simulator needed
```
