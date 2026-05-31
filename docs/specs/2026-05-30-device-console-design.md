# Device console — host/test interface over USB-CDC (→ BLE) — design

**Date:** 2026-05-30  **Status:** PROPOSAL — awaiting review, no code written.

The console is how a human or a host script drives a running node on real hardware:
type/scripted **line commands in**, **NDJSON events out**, over a swappable transport
(USB-CDC now, BLE-NUS later). It is the on-metal twin of the sim's `FirmwareNode`
console role, and it realizes the "device serial-BLE" backend that `command.h:6` and
`2026-05-30-command-interface-design.md` already anticipated. It sits **above** the H0
device `Hal` (it consumes the Node + the Hal's `emit`/`log`) and changes **nothing** in
`lib/core`.

## 0. The cut

The firmware already exposes the two halves of a console seam, both transport-agnostic:
- **Commands in:** `Node::on_command(const Command&) → CmdResult` (`node.cpp:915`); the
  backend parses its wire INTO a `Command` POD (`command.h`) — `lib/core` never sees a byte.
- **Events out:** the `Push` ring drained via `Node::next_push()` (functional:
  `msg_recv`/`send_acked`/`send_failed`, `command.h:51`) **and** `Hal::emit`/`Hal::log`
  (diagnostic; 22 structured event types).

The console is the backend code that wires those to real I/O: *parse line → `Command` →
`on_command`* inbound, and *drain `Push` (+ optionally `emit`/`log`) → JSON → transport*
outbound. The protocol is untouched; the transport is a pluggable adapter.

## 1. Decisions (resolved in brainstorming 2026-05-30)

- **D1 — Client:** both human + scripted. One format serves hand bring-up (in
  `pio device monitor`) and an automated host harness.
- **D2 — Channels:** functional `Push` **always on**; diagnostic `emit`/`log` behind a
  runtime `verbose` toggle (keeps the stream — and BLE bandwidth — lean).
- **D3 — Wire format:** human-readable **line commands in, NDJSON out** (one object per
  line). The JSON-out reuses the sim's event vocabulary.
- **D4 — Transport:** a thin `IConsoleTransport` seam now; `UsbCdcTransport` is impl #1,
  `BleNusTransport` drops in later untouched. Also makes the console core host-testable.
- **D5 — Config:** one binary on every board; configured at runtime via `cfg` console
  commands. The stable long id (`key_hash32`) auto-derives from the nRF52840 FICR
  DEVICEID so boards are distinct with zero config; `cfg key …` can override it.
- **D6 — Sharing (A2):** the parser + JSON writer are a **shared, heap-free core**
  (`lib/console/`) used by BOTH the device driver and the sim's `FirmwareNode`, so the
  grammar/schema physically cannot drift between backends.
- **D7 — `start` semantics:** `start` enters **operating** state with a **layer-entry
  mode** (`existing` default / `join` / `found`), not merely "start beaconing".

## 2. Module layout & dependencies

```
lib/console/                 # SHARED core — heap-free, -fno-exceptions, bounded buffers,
                             #   C++17-includable (hal.h discipline). NO Hal / NO transport / NO json-lib.
  console_parse.{h,cpp}      #   parse_command(line,len, Command&, ParseErr&) -> bool          // the `send` line -> Command
                             #   parse_cfg(line,len, NodeConfig&, uint8_t& node_id, uint32_t& key) -> CfgResult  // a `cfg k v` line
  console_json.{h,cpp}       #   write_ack(buf,cap, CmdResult)         -> size_t
                             #   write_push(buf,cap, Push)             -> size_t
                             #   write_event(buf,cap, type, EventField*, n) -> size_t
                             #   write_log/ write_err/ write_ready/ write_status  (all bounded char buffers)

lib/hal/  (device-only)      # the driver that binds the shared core to real I/O
  iconsole_transport.h       #   bool read_line(char* buf, size_t cap, size_t& len)  // non-blocking poll
                             #   void write(const char* buf, size_t len)
  console_device.{h,cpp}     #   ConsoleDevice: owns the line buffer + verbose flag + lifecycle state;
                             #   pump(): transport -> core.parse -> cfg/on_command/lifecycle ; drain Push ; route emit/log
  usb_cdc_transport.{h,cpp}  #   IConsoleTransport over USB-CDC (Serial). BleNusTransport is a later sibling.
```

**Dependency direction (one-way):** `lib/core` ← `lib/console` ← { `lib/hal` device driver +
transport | sim `FirmwareNode` }. `lib/console` depends only on existing `lib/core` types
(`command.h`, `NodeConfig` at `node.h:28`, `EventField` at `hal.h`); `lib/core` never depends
on `lib/console`. The shared core carries `hal.h`'s constraints (heap-free, no exceptions, no
`std::string`/json, C++17-includable) because the C++17 sim consumes it.

The **shared** surface the sim reuses is `parse_command` + the `write_*` writers. The control
verbs (`cfg`/`start`/`verbose`/`status`) are dispatched by `ConsoleDevice` on the first token —
`start`'s mode and `verbose`'s flag are trivial inline token parses, and `cfg` lines go to
`parse_cfg`. The sim configures nodes from its scenario JSON, not `cfg` lines, so it never
exercises the control verbs.

## 3. Command grammar (line in → action)

MVP verbs only; an unknown verb or malformed line returns an error and the Node is untouched.

| Line | Maps to | Notes |
|---|---|---|
| `cfg <key> <val>` | accumulate into `NodeConfig` / `node_id` / `key` / start-mode | **config state only** |
| `start [existing\|join\|found]` | construct `Node(id,key)` + `on_init(cfg)` + enter operating | default `existing`; `join`/`found` stubbed (§5) |
| `send <dst> <body…>` | `Command{kind=send, u.send.dst_id=dst, flags=E2E}` + `body` → `on_command` | body = rest of line, verbatim, ≤235 B |
| `verbose <on\|off>` | toggle diagnostic `emit`/`log` forwarding | functional `Push` always on |
| `status` | `write_status` (id, key, cfg, lifecycle state) | allowed in any state |

**`cfg` keys** (curated subset of `NodeConfig` + identity): `id` (short `node_id`),
`key` (hex32 `key_hash32` override; default = FICR-derived), `routing_sf`, `data_sf`,
`gateway` (`0|1` → `is_gateway`), `beacon_period_ms`, `leaf_id`.

**`send` flags:** MVP sets `E2E` (`0x08`) so `send_acked`/`send_failed` are observable;
`dst` is the short id (`dst_id`); addressing by `key_hash32` (`dst_hash`) and the
`PRIORITY` (`0x02`) flag are deferred (a later flag-token syntax). `send_layer`/
`send_channel`/`join` `CmdKind`s have **no grammar yet** (they map to `err_unsupported`).

## 4. Output JSON schema (NDJSON — one object per line)

The host parser branches on the leading key (`ack` / `ev` / `log` / `err`).

```jsonc
// command acks + lifecycle (CmdResult.code → ack name; all 7 CmdCode values):
{"ack":"queued","ctr":7,"qd":1}                       // queued: ctr = message id, qd = queue_depth
{"ack":"err_unknown_dst","ctr":0,"qd":0}              // err_unknown_dst|err_too_large|err_no_gateway|
                                                      //   err_priority_capped|err_no_binding|err_unsupported
{"ack":"cfg","key":"routing_sf","val":7}
{"ack":"verbose","on":true}
{"ev":"ready","id":3,"key":"a1b2c3d4","leaf_id":0,"mode":"existing","gateway":false,"routing_sf":7,"data_sf":12}
{"ev":"boot","hwid":"e1f2…","build":1}                // emitted at power-on, before any Node exists

// functional Push (always on) — PushKind → ev:
{"ev":"msg_recv","origin":3,"ctr":7,"body":"hello"}   // body = JSON-escaped text from Push.body[0..body_len]
{"ev":"send_acked","dst":5,"ctr":7}
{"ev":"send_failed","dst":5,"ctr":7}

// diagnostics (verbose on) — emit type + its EventField k/v, typed:
{"ev":"cts_rx","from":5,"snr":7.2}                    // i64→number, f64→number, str→string, boolean→bool
{"log":"…"}                                            // Hal::log line

// errors — never enter the Node, never abort:
{"err":"parse","msg":"expected: send <dst> <body>"}
{"err":"unknown_cmd","cmd":"foo"}
{"err":"not_started"}        // send before start
{"err":"already_started"}    // cfg after start
{"err":"line_too_long"}      // line exceeded the buffer; discarded to next newline
{"err":"cfg","msg":"unknown key: foo"}
```

Because the serializer is shared (A2), **this schema is also the sim's** — the same host
parser/asserts run against both backends. `body` is JSON-escaped text for MVP; an
arbitrary-binary `body_hex` field is a future addition (not now).

## 5. Lifecycle & states

1. **boot** — console up on the transport; no Node yet. Emits `{"ev":"boot",…}` so the
   client sees it's alive + the hardware id. Accepts `cfg`/`status`/`verbose`.
2. **config** — accumulate `cfg …`. `send` here → `{"err":"not_started"}`.
3. **`start [mode]`** — resolve identity (`node_id` from `cfg id`; `key_hash32` from
   `cfg key` else FICR DEVICEID), `Node(id,key)` + `on_init(cfg)`, enter operating in the
   layer-entry mode, emit `ready`. Transition → operating.
   - `existing` (default) — operate in the configured layer (`cfg leaf_id`; R1 single-layer
     = 0). **Implemented today.**
   - `join` — discover + join an existing layer (address-assign). **Forward-looking**:
     `{"err":"not_yet"}` until the join R-iteration (mirrors the `CmdKind::join` stub).
   - `found` — bootstrap a new layer as its first node. **Forward-looking** likewise.
4. **operating** — `send` works; `Push` drained; `emit`/`log` forwarded if `verbose`.
   `cfg` → `{"err":"already_started"}` (config is immutable post-start in MVP). `status`/
   `verbose` still work.

## 6. Data flow (the pump)

`ConsoleDevice::pump()` is called from the device `fw_main` `loop()`, alongside the existing
H-track timer/RX pump:
1. `transport.read_line(buf,cap,len)` → on a full line: `handle_line(buf,len)` → core parse →
   apply `cfg` / `on_command` / lifecycle → `transport.write` the ack or err.
2. drain `node.next_push(p)` while true → `write_push` → `transport.write` (functional, always).
3. device `Hal::emit`/`log` → forwarded into the `ConsoleDevice` sink, gated by `verbose` →
   `write_event`/`write_log` → `transport.write`.

The device `Hal` forwarding (step 3) is the only coupling to H0: the device `Hal`'s `emit`/`log`
(H0 §3 maps them to USB-CDC) call into the `ConsoleDevice` sink instead of formatting JSON
themselves — keeping one serializer (the shared core).

## 7. Error handling

The console **never throws or aborts** (`-fno-exceptions`); every failure is a JSON `err`
line, so malformed input cannot brick the node. A line longer than the buffer →
`{"err":"line_too_long"}` and bytes are discarded to the next newline (the line buffer is
sized for `send <dst> ` + a 235-byte body + slack, ≈288 B). Parse/`cfg` failures →
`{"err":"parse"|"cfg",…}` with the Node never invoked. Valid commands the Node rejects
surface as `{"ack":"err_*"}` carrying the `CmdCode`.

## 8. A2 — sim adoption

`FirmwareNode` adopts the shared core: `onCommand` (its own parser today at
`FirmwareNode.cpp:74`) calls `console::parse_command(...)`, and its push/event emission calls
`console::write_*` into a buffer it then writes to its NDJSON stream. The sim keeps
`nlohmann`/`std::string` for its **other** duties (scenario config-in, telemetry plumbing) —
only the command/event **wire** goes through the shared core. A golden test pins
device-emitted JSON == sim-emitted JSON for representative events (trivially equal given one
serializer, but the test locks it against future edits).

## 9. Identity on device

`Node(Hal&, uint8_t node_id, uint32_t key_hash32, name)` fixes id + long-id at construction;
`NodeConfig` is applied via `on_init`. So:
- **`key_hash32`** (stable long id) — default derives from the nRF52840 **FICR DEVICEID**
  (`NRF_FICR->DEVICEID[0/1]` folded 64→32, e.g. FNV-1a) so every board is distinct with no
  config; `cfg key <hex32>` overrides it for deterministic / sim-parity runs.
- **`node_id`** (short id) — from `cfg id <n>`; if unset, the proposed default derives it from
  the long id so an unconfigured board still has *a* short id (see Q14.4).

Both are resolved at `start`, when the Node is constructed.

## 10. Testing

- **Native unit tests** (host) run the shared core against a `FakeTransport` + a real `Node`
  + a stub `Hal`: feed `cfg`/`start`/`send` lines → assert the `Command` built and the JSON
  emitted; feed a `Push` → assert `msg_recv`/`send_acked`/`send_failed` JSON; round-trip
  parse↔serialize; lifecycle/error transitions. This is the A2 payoff — the wire is fully
  host-verified without hardware.
- **Golden parity test** — device-core JSON == sim-core JSON for a fixed event set.
- **On-metal** — the 2-device beacon + DM exchange (the H3 success criterion) is driven and
  observed through the console: configure each board over USB, `start`, `send`, assert
  `msg_recv` on the peer and `send_acked` on the sender.

## 11. Transport roadmap

- **USB-CDC now** (`UsbCdcTransport`, impl #1).
- **BLE-NUS later** — same shared core + `ConsoleDevice`, a new `IConsoleTransport`; the S140
  softdevice is already in the device build (`boards/seeed-xiao-afruitnrf52-nrf52840.json`).
  Client = a phone NUS terminal (nRF Connect/Toolbox) or a desktop bridge.
- **WiFi** — ESP32 target only (`heltec_v3`); the nRF52840 has no WiFi. A TCP/WebSocket
  transport is an ESP32-S3 capability, never the primary board.

## 12. Files

- `lib/console/console_parse.{h,cpp}` (+ native test), `lib/console/console_json.{h,cpp}`
  (+ native test) — the shared core.
- `lib/hal/iconsole_transport.h`, `lib/hal/console_device.{h,cpp}` (+ native test against
  `FakeTransport`), `lib/hal/usb_cdc_transport.{h,cpp}` (device-only TU).
- `src/fw_main.cpp` — add the `ConsoleDevice` construction + `pump()` in `loop()`, and route
  the device `Hal`'s `emit`/`log` into the console sink.
- `lora-universal-simulator/orchestrator/runtime/FirmwareNode.{cpp,h}` — adopt
  `console::parse_command` + `console::write_*` (A2); add the golden parity test.
- `platformio.ini` — `native` env compiles `lib/console` (+ its tests); `xiao_sx1262` compiles
  the device driver + `UsbCdcTransport`.

## 13. Scope / non-goals

**In:** the shared `lib/console` core (parser + bounded JSON writer), `IConsoleTransport` +
`UsbCdcTransport`, the `ConsoleDevice` driver, the `cfg`/`start`/`send`/`verbose`/`status`
grammar + JSON schema, the sim `FirmwareNode` adoption (A2), native tests.

**Out (now):** NV/flash persistence of config (config is per-boot over the console — H0 defers
flash); the BLE/WiFi transport **impls** (the seam only); the `join`/`found` layer procedures
(stubbed); arbitrary-binary `body_hex`; the `send_layer`/`send_channel` command grammar; any
change to `lib/core`.

## 14. Resolved decisions (2026-05-30)

1. **Sim adoption timing** — build the shared core + device path + native tests **first**; land
   the `FirmwareNode` A2 adoption as an **immediate follow-up**. It refactors working sim code
   and avoids colliding with the parallel H-track agent (active in `lib/hal`/`fw_main`). This is
   the Phase A / B / C split in §15.
2. **`verbose` granularity** — one **global** on/off for MVP (no per-type filter).
3. **`status` depth** — identity + config + lifecycle **only** for MVP; a `dump routes` /
   neighbor command is a later addition.
4. **`node_id` default** — when `cfg id` is unset, **derive** a short id from the long id so a
   bare board still runs.
5. **Line editing** — **raw** line read (no backspace/echo handling); rely on the host terminal
   (`pio device monitor` echoes locally; the scripted client doesn't need it).

## 15. Build phases & coordination with the parallel H-track

- **Phase A — `lib/console` core + native tests (parallel-safe, now).** Greenfield directory;
  depends only on shipped `lib/core` (`command.h`, `NodeConfig`). Collides with nothing the
  H-track has open. The bulk of the logic + the full A2 payoff, host-tested without hardware.
- **Phase B — device driver + `fw_main` pump (gated on H3).** `console_device` +
  `usb_cdc_transport` + the `loop()` pump + routing the device `Hal`'s `emit`/`log` into the
  console sink. Lives in `lib/hal`/`fw_main` (the agent's active area) and needs the device
  `Hal` facade (H3). Coordinate: agent owns `fw_main`; the pump addition is a small, delimited edit.
- **Phase C — A2 sim adoption (coordinate, low-risk).** `FirmwareNode` adopts `parse_command` +
  `write_*`. Sim repo; do it when the agent isn't editing `FirmwareNode`.

Sync mechanism (separate Claude instances, coordinating via the user + repo): ownership split —
agent owns `lib/hal`+`fw_main`, console work owns `lib/console`; Phase A on its own branch; one
heads-up to the agent to leave a device-`Hal` `emit`/`log` → console-sink hook for Phase B.
