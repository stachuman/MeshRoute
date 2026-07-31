# On-device OLED + one-button UI for a team mobile — design spec

*2026-07-31. From the owner's screen draft, refined in design dialogue. Fills the `mr_ui` seam that `2026-07-12-firmware-feature-split.md` slice 4 left empty.*

*Status: DRAFT, not implemented. Line references verified against the working tree at time of writing; board pins recovered from MeshCore's working V3 and V4 ports.*

*Phased: **Phase A ships the UI on Heltec V3**; **Phase B ports to V4** and adds GPS. Phase B's radio port and GPS driver are named here but specified elsewhere — see §10.2, §10.3, §13.*

---

## 0. Purpose

A team mobile with a display and one button must let a hiker **raise an alarm and know it was heard** without a phone. That ruling (owner, 2026-07-31) orders every trade-off below: the emergency path gets a dedicated gesture, an explicit confirm, honest delivery reporting and bounded auto-retry; browsing screens are deliberately thin.

The device remains fully usable with the companion app — this UI adds a degraded-mode surface, it does not replace the app or the console.

## 1. Scope

**In:** screen framework, one-button gesture input, status bar, team roster, merged inbox, two canned non-emergency messages, the emergency send path, battery indicator (incl. a new ESP32-S3 battery reader), screen blanking.

**Out (owner-scoped):**

| deferred | why |
|---|---|
| Editable settings screen (BLE on/off, OTA, GPS enable) | the companion app and console already cover it; one-button editing is expensive to build and rarely used. Screen-off timeout becomes a build constant. |
| Separate channel-message screen | merged with the DM inbox; two inbox screens double the cycle for the same interaction. |

**Phased by board (owner ruling 2026-07-31): Phase A targets Heltec V3, Phase B adds V4.** V3 first because its panel and button are identical to V4's while it has no front-end module, so the display and input work can land without the radio port §10.2 describes. See §13.

**GPS / distance-to-teammate: out for Phase A, IN for Phase B (V4).** No GPS driver exists anywhere in the tree today and `lat`/`lon` are typed by hand via `cfg set lat`/`lon` into `/mrid`, so Phase A's TEAM screen shows last-heard age and signal quality only — both already in the core. Phase B adds the GPS driver and the distance column (§10.3).

⚠ **Honest caveat on the evidence.** MeshCore's V4 config defines GPS pins *and* sets `ENV_INCLUDE_GPS=1`, which is what prompted reviving this. But its **V3** config also defines GPS pins (RX 47, TX 48, EN 26) without enabling GPS — so pin definitions alone do not prove an on-board module, and my earlier reading overstated the case. Practically this changes little: the driver work (UART NMEA on defined pins) is identical whether the receiver is on-board or attached to headers. What it changes is whether the *user* must attach a module, which needs confirming against the physical board before Phase B is planned.

**Explicit non-goal:** per-message read/unread. The existing cursor model (`Inbox::read_cursor()` / `mark_read()`, `inbox.h:69-70, 116`) is kept as-is, per the owner's draft.

## 2. Architecture

The `mr_ui` seam already defines the boundary and is already wired unconditionally — `mr_ui_init()` at `fw_main.cpp:795`, `mr_ui_on_push()` at `:1066`, `mr_ui_tick()` at `:1208`, with inline no-ops when `MR_FEAT_OLED=0` (`lib/hal/mr_ui.h`). This spec fills that seam; it does not change it.

Four units:

| unit | responsibility | depends on | tested |
|---|---|---|---|
| `src/firmware_ui_input.h` — pure header | classify `(level, now_ms)` samples → `short` / `double` / `long-arm-progress` / `long-fire` / `cancel` | nothing | **native** |
| `src/firmware_ui_model.h` — pure header | reduce `(Gesture, UiSnapshot) → UiState` (screen, cursors, emergency phase, dirty flag) | nothing | **native** |
| `src/firmware_ui.cpp` | build `UiSnapshot` from `g_node` / `mrble` / `Inbox` each tick; drive the model; call render primitives | core accessors, model | on-target |
| `src/board_ui.cpp` | board port: panel driver, button GPIO, battery ADC, per-board pin table | Arduino, display lib | on-target |

**Hard boundary:** `firmware_ui.cpp` never touches GPIO or I²C; `board_ui.cpp` never knows what a screen *is*. This honours U3 (feature logic lives in a `firmware_*` module; board glue stays board glue) and keeps `board_ui.cpp` as the board seam it already claims to be.

**Why two pure headers.** `[env:native]` sets `test_build_src = no` but adds `-I src` (`platformio.ini:83`), so a pure header in `src/` is reachable from native tests — the established precedent is `firmware_config_parse.h` driven by `test_firmware_config_parse.cpp`. Gesture timing and emergency-state transitions are miserable to debug on hardware and trivial to test on host; both belong on the right side of that line.

`UiSnapshot` is plain data — unread counts, ages in ms, a bounded array of teammate rows, `batt_mv`, `ble_connected`, last send outcome. The model never sees `g_node`, so a native test can drive "3 teammates, one heard 4 h ago, emergency armed, `no_relay` returned" with no radio and no Arduino.

## 3. Screens and gestures

### 3.1 Cycle

| slot | screen | gate |
|---|---|---|
| 1 | STATUS | `MR_FEAT_OLED` |
| 2 | TEAM | `MR_FEAT_OLED && MR_FEAT_TEAM` |
| 3 | INBOX (DM + CH merged) | `MR_FEAT_OLED` |
| 4 | SEND "Got your message" | `MR_FEAT_OLED && MR_FEAT_TEAM` |
| 5 | SEND "All good" | `MR_FEAT_OLED && MR_FEAT_TEAM` |

Slots 4–5 give each canned message its own screen rather than a select-then-send submode. That keeps `double` meaning exactly one thing everywhere and removes a nested mode from a one-button device. It scales badly past ~3 messages, which is acceptable.

On a non-team build the cycle is STATUS → INBOX.

**"I'm in danger" is deliberately absent from slots 4–5.** It is reachable only by long-press (§4). Two routes to the same dangerous action invite accidental alarms, and the navigational one would be the route nobody can use under stress.

### 3.2 Gestures

| gesture | meaning |
|---|---|
| short | next screen (wraps) |
| double | context action — INBOX: next entry · TEAM: next teammate · SEND: **send this message** · STATUS: none |
| long | arm emergency, from **any** screen |

The owner's draft had `short` as both screen-select and list-scroll; `double`-as-cursor resolves that. The draft's separate menu screen is dropped — short-press cycling *is* the menu.

### 3.3 Layout

A persistent 8 px status bar on every screen, so "is anything wrong" never requires cycling:

```
+---------------------------------------+
| DM3 CH12  T4/5  BLE*  84%             |  <- 6x8, always
+---------------------------------------+
|  screen body: 4 lines @ 6x8,          |
|  or 2 lines @ 8x16 (emergency)        |
+---------------------------------------+
```

STATUS becomes the detail view: ages spelled out ("DM 3, newest 1h05"), **our own team local id** (so the wearer can tell a teammate how to address them), team id, registration state, BLE mode, battery mV.

TEAM shows one row per teammate: name (or `0x<hash>`, or the bare team id), last-heard age, signal quality, hops. **Phase B adds a distance column on V4**, rendered only when both our fix and the peer's location are known and fresh — omitted, never estimated (§10.3).

## 4. Emergency state machine

```
IDLE --hold 800ms--> ARMING
  ARMING: "RELEASE TO CANCEL / EMERGENCY IN 3..2..1" drawn while held
    release < 3.5s ------> CANCELLED (brief toast) -> IDLE
    held through 3.5s ---> FIRING
FIRING: post the encrypted team channel message "I'm in danger" (+ location if a fix exists)
    send_blocked{next_ms}          -> BLOCKED   (show "retry in Ns", auto-retry at next_ms)
    channel_sent{relayed=true}     -> PICKED UP
    channel_sent{relayed=false}    -> NOT HEARD (auto-retry x3 w/ backoff) -> NOT HEARD (sticky)
any state: inbound channel msg from a teammate -> REPLY: <name> <text>
sticky until acknowledged (double)
```

Two constraints the draft could not have anticipated, both verified:

- **`send_blocked` must be handled.** `channel_min_interval_ms = 10000` (`protocol_constants.h:377`) means a second emergency post inside 10 s is refused **pre-TX** with `send_blocked{min_interval, next_ms}` (`command.h:103, 177-180`). A safety UI that displayed "sent" there would be lying. Show the countdown; auto-retry at `next_ms`.
- **Delivery evidence is weaker than "delivered", and the wording must say so.** Team channel messages carry no end-to-end ack — there is no `DATA_FLAG_E2E_ACK_REQ` anywhere in `node_channel.cpp`. The only signal is `PushKind::channel_sent` (`command.h:105`, `node_channel.cpp:403-416`): `relayed=true` means a neighbour was overheard re-flooding the post. Render that as **`PICKED UP`**, never `DELIVERED`. True human confirmation arrives only as a teammate's reply — which is precisely why "Got your message" earns slot 4.

**Auto-retry bound:** 3 attempts, backoff respecting `next_ms` when blocked; then a sticky `NOT HEARD` that the user can re-fire with `double`. Unbounded retry is not acceptable — it would burn the duty budget the rest of the team needs to answer.

### 4.1 ★ Location on the distress call (owner ruling 2026-07-31)

Per `2026-07-30-channel-crypt-and-location-privacy-design.md` §2.2.1, an encrypted channel post carries **text and location together** — the sealed inner becomes a flags byte (`bit0` text, `bit1` location) with `pack_loc6`, not the either/or the earlier T-K2 draft had. **The distress call includes location when one is available.** This supersedes the recommendation in §14 Q3 to omit it.

The exact invocation is `send_channel <ch> "I'm in danger" -t -l -e`.

⚠ **The UI must attach `-l` conditionally, and this is a safety-critical detail, not a nicety.** That spec's matrix refuses `-t -l` outright when there is no fix (`lat_e7 == 0 && lon_e7 == 0`) with `no_location`. A UI that always sent `-l` would therefore turn "no fix" into **no alarm at all** — the worst possible failure for this feature. So:

| condition | command |
|---|---|
| `lat_e7 != 0 \|\| lon_e7 != 0` | `send_channel <ch> "I'm in danger" -t -l -e` |
| no fix | `send_channel <ch> "I'm in danger" -t -e` |

Both forms are explicitly `-e`, which also satisfies the "must actually be sealed" half of the O6 ruling without depending on `team_channel_crypt`'s default.

**Phase A caveat, stated once and then accepted.** On V3 there is no GPS, so the only coordinate is whatever was typed via `cfg set lat`/`lon` — potentially hours stale for a walking hiker, and a stale position in a rescue context can send help to the wrong place. I raised this and the owner ruled to include it when available; the ruling stands and the design follows it. Phase B's live fix removes the concern entirely. The canned non-emergency messages ("Got your message", "All good") do **not** carry location — only the distress call does.

## 5. Paint and power policy

**This is a correctness constraint, not a nicety.** A full 128×64 SSD1306 frame is 1024 bytes; at 400 kHz I²C that is roughly **25 ms of blocking bus time**. The MAC's CTS→DATA gap is `cts_to_data_gap_ms = 5` (`protocol_constants.h:127`) and measured turnarounds are ~5–8 ms (`protocol_constants.h:331-334`). A full-frame repaint is therefore long enough to break an in-flight RTS/CTS/DATA exchange.

Three rules:

1. **Paint only when the MAC is idle** — no pending TX/RX, `!g_iradio.tx_busy()`, `g_hal.txq_depth() == 0`. `fw_main.cpp:1274` already computes this predicate to decide it may sleep; reuse it rather than inventing a second one (U1).
2. **Paint only when the model reports dirty**, throttled to ≤2 Hz, as `mr_ui.h` already instructs.
3. **Chunk the frame across ticks** — 8 pages of 128 B ≈ 3 ms each, so no single tick holds the bus. This is why §8 recommends a page-buffered driver rather than a full-frame one.

Emergency states override rule 2's throttle but **not** rule 1's idle check: sending is more important than drawing, and the send is what the screen is about.

**Power:** the panel blanks after `MR_UI_BLANK_MS` (build constant, proposed 15000) of no input. Any press wakes it and **the waking press is consumed** — it must not actuate, or a wake becomes an accidental screen change. Emergency states hold the panel on for at most 120 s, after which it blanks with state retained; the next press restores the emergency screen, not the cycle.

## 6. Data sources

Every field, and where it comes from:

| field | source |
|---|---|
| unread DM / CH counts | **counted UI-locally in `mr_ui_on_push`**, cleared when the INBOX screen is viewed. ⚠ Corrected 2026-07-31 during planning: the original `dm_newest_seq() - read_cursor()` here was not implementable — `Inbox` (`inbox.h:111-116`) has no read-cursor getter (`read_cursor()` is on `InboxStore`), and `firmware_inbox.h:11` states that verbs use `g_node.inbox()`, not the `g_inbox_*` stores. Counting locally needs no new core API. Consequence: counts are **session-scoped** and reset on reboot, which for a glanceable bar arguably reads better as "since you last looked". |
| "newest received Nm ago" | **stamped by the UI in `mr_ui_on_push`**, not scanned from the store — `msg_recv` / `channel_recv` already fire there (`fw_main.cpp:1066`). Avoids a store scan on the service path. Consequence: after a reboot the age reads `—` until the first push, while counts stay correct (the store persists, the stamp does not). Accepted; the alternative is an `InboxEntry.rx_time_ms` (`inbox.h:38`) lookup via `read_since`, which is a scan we do not need. |
| team member count / roster | `rt_team_count()`, `rt_team_at(i)` (`node.h:420-421`) |
| per-member last heard / quality / hops | `RtCandidate.last_seen_ms`, `.score` (Q4 dB), `.hops` (`node_carriers.h:265-272`) |
| member name | `peer_name_find(key_hash32, …)` (`node.h:622`) via `team_key_of_id` (`node.h:129`); falls back to `0x<hash>` then to the bare team id |
| our own team id | `team_local_id()` (`node.h:258`) |
| BLE connected | `mrble::connected()` (`fw_main.cpp:1195`) |
| BLE mode / period / pin | `g_ble_mode`, `g_ble_period_min` (`fw_context.h:75-76`) |
| battery | **new** — see §7 |
| emergency outcome | `PushKind::channel_sent` / `send_blocked` via `mr_ui_on_push` |

## 7. Battery reader (new work)

The only battery reader in the tree is nRF52-only: `#if defined(NRF52_PLATFORM) && defined(PIN_VBAT) && !defined(MR_NO_BATT)` (`firmware_commands.cpp:299-304`, method at `:709`). Both Heltec boards are ESP32-S3, so `batt_mv` is unavailable on either today, and `console_json.h:126` records the project rule: an unavailable reading is **omitted, never faked**. The status bar must render `--` rather than a plausible wrong percentage.

Add an ESP32-S3 reader behind the same shape (a board-gated function returning millivolts, `<0` = unavailable). Both methods come from MeshCore — the same provenance `firmware_commands.cpp:709` used for the nRF52 reader ("the authoritative MeshCore XiaoNrf52Board method").

Pins and formula are **identical** on V3 and V4: `ADC_CTRL` 37, `VBAT_READ` 1, 10-bit resolution, mean of 8 samples, `mv = 5.42 * (3.3/1024.0) * raw * 1000`. Two things differ:

**V3 — polarity is auto-detected** (`HeltecV3Board::begin()`), because boards past rev 3.2 inverted it:

```
pinMode(PIN_ADC_CTRL, INPUT);
adc_active_state = !digitalRead(PIN_ADC_CTRL);   // probe the idle level, then invert
pinMode(PIN_ADC_CTRL, OUTPUT);
digitalWrite(PIN_ADC_CTRL, !adc_active_state);   // park inactive
```
Read: drive `adc_active_state`, sample, drive `!adc_active_state`. **No settling delay.** Do not hardcode LOW — the auto-detect exists because the hardware genuinely varies within "V3".

**V4 — fixed ACTIVE=HIGH, plus a settling delay:**

```
digitalWrite(PIN_ADC_CTRL, HIGH);
delay(10);                        // ⚠ see below
... sample ...
digitalWrite(PIN_ADC_CTRL, LOW);
```

⚠ **V4's `delay(10)` is the same hazard class as a full-frame repaint.** Ten milliseconds of blocking wait against `cts_to_data_gap_ms = 5` will break an in-flight exchange. The Phase B port must not copy it verbatim: either sample only under the §5 rule 1 MAC-idle predicate, or restructure as a small state machine across ticks (enable → return; sample on a later tick → disable). Battery is a once-per-N-seconds reading; there is no reason for it to ever block. Phase A (V3) has no delay to remove, which is one more reason it lands first.

Verify against a multimeter on the cell before trusting the constant on either board — the discipline the nRF52 comment demands, and the divider is a per-revision property.

## 8. Dependencies

The tree has **no display library** — `board_ui.cpp` is deliberately driver-free so the Heltec build links today. Phase A adds one, and V4 reuses it unchanged (same panel, same pins).

**Recommended: U8g2 in page-buffer mode** (`U8G2_SSD1306_128X64_NONAME_1_HW_I2C`). Two reasons, both structural rather than aesthetic: the 1-page mode uses a **128 B** buffer instead of 1024 B, and its natural draw loop is exactly the 8-page chunking §5 rule 3 requires — the constraint and the library's grain agree. Alternative is Adafruit_SSD1306 + GFX + BusIO (three deps, full 1024 B buffer, no page mode).

**Pin the version exactly, never a caret.** The RadioLib ruling (`platformio.ini:234` and its note, on this very env) exists because a caret let different checkouts resolve different versions and silently skewed board RAM/Flash baselines.

## 9. Feature gating

- `MR_FEAT_OLED` (board capability) gates the UI TUs. Default 0 (`mr_features.h`); set to 1 on `heltec_v3` (`platformio.ini:215`), inherited by `heltec_mobile`.
- `MR_FEAT_OLED && MR_FEAT_TEAM` gates slots 2, 4, 5 and the team rows of the status bar.

**Deviation from the owner's draft, approved 2026-07-31:** the draft said "gate it behind `MR_FEAT_TEAM`". Composing the two gates instead means a static OLED board still gets a status screen and an inbox rather than a blank panel, at no cost. `MR_FEAT_TEAM` alone would have conflated a board capability with a protocol plane.

**Phase A target env: `heltec_mobile`** (`platformio.ini:372`) — `heltec_v3` plus `MR_PROFILE_MOBILE`, which sets `MR_FEAT_REMOTE_MGMT=0` and leaves `MR_FEAT_TEAM=1`. It already inherits `MR_FEAT_OLED=1` from `heltec_v3`, so no env change is needed to start.

Phase B adds a `heltec_v4` env (and a `heltec_v4_mobile` extending it) once the radio port of §10.2 exists. GPS gets its own feature flag rather than riding `MR_FEAT_OLED` — a GPS is not a display, and a future non-display tracker would want one without the other.

## 10. Board port table

All values recovered from MeshCore's working ports — `~/MeshCore/variants/heltec_v3/` and `~/MeshCore/variants/heltec_v4/` — not from datasheet reading.

### 10.1 V3 vs V4

| item | Heltec V3 (Phase A) | Heltec V4 (Phase B) | same? |
|---|---|---|---|
| MCU | ESP32-S3 (`esp32-s3-devkitc-1`) | ESP32-S3, 16 MB flash, 2 MB PSRAM | ~ |
| panel | SSD1306 @ 0x3C, SDA **17**, SCL **18** | SSD1306 @ 0x3C, SDA **17**, SCL **18** | ✅ |
| panel reset | **21** per our own `board_ui.cpp:14` note — MeshCore's V3 variant defines no `PIN_OLED_RESET`; **confirm on hardware** | **21** (`PIN_OLED_RESET`, explicit) | ✅ (pending V3 confirmation) |
| **user button** | **GPIO 0** | **GPIO 0** | ✅ |
| battery pins | `ADC_CTRL` **37**, `VBAT_READ` **1** | `ADC_CTRL` **37**, `VBAT_READ` **1** | ✅ |
| battery formula | `5.42 * (3.3/1024) * mean8(raw)` | identical | ✅ |
| **battery ctrl polarity** | **auto-detected at boot** (`adc_active_state = !digitalRead(pin)` as INPUT) — boards >3.2 differ; nominal ACTIVE=LOW | **fixed ACTIVE=HIGH** | ❌ |
| **battery settling delay** | none | **`delay(10)`** | ❌ |
| peripheral power | `VEXT_EN` **36**, polarity implicit (default) | `VEXT_EN` **36**, explicitly **ACTIVE=HIGH** | ❌ |
| LoRa SPI | NSS 8, DIO1 14, BUSY 13, SCLK 9, MISO 11, MOSI 10 | identical | ✅ |
| **LoRa reset** | **`RADIOLIB_NC`** (matches our `platformio.ini:218`) | **GPIO 12** | ❌ |
| TX LED | 35 | 35 | ✅ |
| **front-end module** | **none** | **PA + LNA, switched every TX** | ❌ |
| **`LORA_TX_POWER`** | **22** = 22 dBm at the SX1262 | **10** = **22 dBm output** | ❌ |
| RX register patch | not set | `SX126X_REGISTER_PATCH=1` (reg 0x8B5) | ❌ |
| GPS pins | RX 47, TX 48, EN 26 — defined, **GPS not enabled** | RX 38, TX 39, RESET 42, EN 34 — **`ENV_INCLUDE_GPS=1`** | ❌ |

**What this means for the plan:** the panel, the button and the battery *pins and formula* are common to both boards, so the UI layer and its render/input code port unchanged. The differences are confined to two places — a handful of **board-port details** (battery control polarity and settling, Vext polarity) that live entirely inside `board_ui.cpp`, and the **radio domain** (FEM, reset pin, tx_power semantics, register patch), which the UI never touches. That is exactly why Phase A on V3 is cheap and Phase B is a real port: Phase B's cost is radio work, not UI work.

⚠ **GPIO 0 is the ESP32-S3 boot strap pin, on both boards.** Holding the user button across a reset enters serial-download mode. Since long-press is the emergency gesture, a user holding the button while the node brownouts or resets gets a bricked-looking device instead of an alarm. This is a hardware behaviour to document in user-facing text and to weigh when choosing the arm duration — not something the UI layer can fix.

### 10.2 ⚠ V4 is a radio port, not a pin table — Phase B PREREQUISITE, out of scope here

MeshCore's V4 variant carries a `LoRaFEMControl` class and calls it on **every transmission** (`HeltecV4Board::onBeforeTransmit` / `onAfterTransmit`). The V4 has an external front-end module — PA plus LNA — that must be switched between TX and RX modes. Our tree has **no such hook**: `DeviceRadio::start_transmit` and `poll_tx_done` (`lib/hal/device_radio.h:137, 162`) drive RadioLib directly with no board callback.

Four consequences, none of which belong in a UI spec:

1. **FEM switching is mandatory.** Without it the V4 transmits through a bypassed PA with the LNA still in circuit. The natural insertion points exist (`start_transmit` before `startTransmit`; `poll_tx_done` and `tx_timeout_recover` before `arm_rx`) but the abstraction does not.
2. **Two board revisions behind one name.** `LoRaFEMControl::init()` auto-detects the FEM at runtime by reading GPIO 2's default pull level — pull-down ⇒ GC1109 (V4.2), pull-up ⇒ KCT8103L (V4.3) — and the two need different pin sequences. "Heltec V4" is not one target.
3. **★ `tx_power` changes meaning, and the range becomes unsafe.** MeshCore's FAQ §7.7 states for the V4: an in-app setting of **10 dBm yields 22 dBm output**, and **22 dBm yields 28 dBm**. Our `cfg set tx_power` validates −9..22 (`firmware_config.cpp:142`) and documents it as SX1262 dBm. On a V4 that same range silently means up to **28 dBm actual**, past what most EU868 sub-bands permit and into the territory MeshCore's own table prefixes with a hardware-damage warning. The V4 port must either offset the setting or clamp the range per board — a compliance decision for the owner, not an implementation detail.
4. `SX126X_REGISTER_PATCH=1` (register 0x8B5, "improved RX") is set for V4 and is absent from our envs.

**Ruling (owner, 2026-07-31): the V4 radio port is its own spec, and Phase A proceeds on V3 meanwhile.** Folding a PA/LNA switching path and a transmit-power semantics change into a display feature would violate C1 outright, and the tx_power item is a regulatory question that deserves its own decision record. Nothing in Phase A depends on it, because V3 has no FEM.

### 10.3 GPS and distance — Phase B only

Phase B adds, in this order:

1. **A UART NMEA driver** behind a board-gated seam, on V4's `PIN_GPS_RX 38` / `PIN_GPS_TX 39`, with `PIN_GPS_EN 34` (active LOW) and `PIN_GPS_RESET 42` (active LOW) for power control. It must not block: NMEA arrives continuously and the parser has to be fed incrementally from the service loop, never with a blocking read — the same discipline §5 imposes on the panel and §7 on the battery.
2. **Feed the fix into the existing location plumbing** rather than inventing a parallel one (U1): `lat_e7` / `lon_e7` already exist in `NodeConfig` and in the identity record, and a DM location piggyback already exists. A GPS fix should write the same fields `cfg set lat/lon` writes, so every existing consumer works unchanged.
3. **Distance column on the TEAM screen**, shown only when both our fix and the peer's location are known and fresh; otherwise the column is omitted, never estimated. This follows the project rule already recorded for battery (`console_json.h:126`): an unavailable reading is omitted, never faked. A wrong distance in a safety context is worse than no distance.
4. **Location in the emergency message** becomes viable and should be revisited then — it is §14 question 3, which Phase A cannot answer because Phase A has no fix worth sending.

Peer location exchange is the open dependency: distance needs teammates' coordinates, and how those propagate on the team plane (beacon TLV, DM piggyback, or channel message) is not settled. That question belongs to the Phase B spec, not this one.

## 11. Flash / RAM budget and D2

- RAM: `UiSnapshot` + `UiState` + a bounded teammate array (cap 16, matching `cap_team_liveness`) + a 128 B page buffer. Expected low hundreds of bytes; measure, don't estimate.
- Flash: U8g2 with **two** fonts selected (6×8, 8×16). Do not link the full font set.
- These units live in `src/`, not `lib/core`, so **`sizeof(Node)` is unchanged** and the `node.h:1976` assert is untouched. That is a deliberate architectural consequence: no UI state belongs in the protocol engine.
- Per D2/the 3-env board rule, gate on `gateway` + `xiao_sx1262` + `xiao_esp32s3`, plus `heltec_v3` and `heltec_mobile` since those are the envs Phase A changes. Record the flash/RAM delta for the Heltec envs. ESP32-S3 flash is not the constraint here (V3 devkit part; V4 is 16 MB) — the reason to record it is the RadioLib-pinning lesson about baselines drifting unnoticed, not headroom anxiety.

## 12. Test plan

**Native (the valuable half):**

| test | drives |
|---|---|
| `test_firmware_ui_input.cpp` | gesture classifier: short vs double window, double vs two shorts, long-arm progress ticks, release-before-fire cancel, bounce rejection, the consumed wake press |
| `test_firmware_ui_model.cpp` | screen cycle incl. the non-team compile-out, cursor advance and wrap on TEAM/INBOX, the full emergency machine (arm → cancel, arm → fire → `blocked` → retry-at-`next_ms` → `picked up`; and → `no_relay` → 3 retries → sticky), dirty-flag correctness, blanking and emergency-hold timing |

Both are pure and table-driven; no Arduino, no radio, no display.

**On-target checklist** (Phase A: `heltec_mobile` = Heltec **V3**, bench):

1. Emergency from each screen, in the dark, with gloves — reaches FIRING without reading the panel.
2. Release at 3.0 s cancels; release at 3.6 s fires.
3. Two nodes: fire with the second powered off → `NOT HEARD` after 3 retries. Power the second on → fire again → `PICKED UP`. Reply from the second → `REPLY` shown.
4. Fire twice inside 10 s → second shows `BLOCKED` with a live countdown, then auto-fires.
5. **Paint-vs-radio:** run the s18-style DM load while cycling screens continuously; confirm no CTS timeout regression. This is the check that §5 rule 1 exists for, and the one most likely to fail.
6. Blank/wake: waking press does not change screen.

**Gate:** the standing D1 gate applies — native, s18 md5 exact (this work is `src/`-only, so it is inert by construction and the md5 must not move), and the board envs per §11.

## 13. Slices

Slices are named `UI-n` deliberately: bare `U1`/`U3` would collide with the CLAUDE.md working-rule IDs (U1 = reuse-before-writing, U3 = feature logic in a `firmware_*` module), both of which this spec cites.

### Phase A — Heltec V3 (this spec)

| # | slice | gate |
|---|---|---|
| UI-1 | `firmware_ui_input.h` + native test | native |
| UI-2 | `firmware_ui_model.h` + native test (screens, cursors, no emergency) | native |
| UI-3 | board port: display driver, page-chunked paint, blanking; renders STATUS + INBOX | on-target (V3) |
| UI-4 | button GPIO 0 wiring into the classifier; the cycle becomes live | on-target (V3) |
| UI-5 | TEAM screen + canned-message slots (`MR_FEAT_TEAM`) | on-target (V3) |
| UI-6 | emergency machine end-to-end incl. `send_blocked`/retry | native + on-target |
| UI-7 | V3 battery reader (auto-detected ADC_CTRL polarity, no delay) | on-target, multimeter-verified |

UI-1 and UI-2 are pure and can start immediately — no hardware question blocks them. UI-3 is blocked only on the display-library choice (§8). Every V3 pin UI-3/UI-4/UI-7 needs is now known (§10.1), except the panel reset pin, which wants a hardware confirmation.

### Phase B — Heltec V4 (separate specs, not this one)

| # | work | owning spec |
|---|---|---|
| B-1 | V4 radio port: FEM TX/RX switching, runtime GC1109-vs-KCT8103L detection, LoRa RST 12, `SX126X_REGISTER_PATCH` | **new spec** (§10.2) |
| B-2 | `tx_power` semantics and per-board clamp — a compliance decision | **new spec / owner decision** (§10.2 item 3) |
| B-3 | V4 board port for the UI: battery polarity fixed HIGH, the `delay(10)` restructured, Vext ACTIVE=HIGH | this spec's §7 + §10.1, applied to a new env |
| B-4 | GPS driver + peer-location exchange + TEAM distance column | **new spec** (§10.3) |

B-1 and B-2 must land before any V4 hardware is trusted on air. B-3 is small once B-1 exists. B-4 is the largest and depends on a decision this spec does not make (how peer locations propagate on the team plane).

## 14. Open questions for the reviewer

1. ~~Which board is the real target?~~ **Resolved 2026-07-31.** Pins for both boards recovered from MeshCore (§10.1). **Phase A on V3, Phase B on V4**, with the V4 radio port and GPS as separate specs (§10.2, §10.3). Residual, small: the **V3 panel reset pin** is 21 per our own `board_ui.cpp:14` note, but MeshCore's V3 variant defines no `PIN_OLED_RESET` — worth one hardware confirmation before UI-3, since a wrong reset pin shows up as a dead panel and is easy to misdiagnose as a driver problem.
2. **§4 — is 3.5 s the right arm time?** Long enough to prevent pocket-fires, short enough not to feel broken. It is a guess and wants a bench opinion, not a code review.
3. ~~Should the emergency message include location?~~ **RESOLVED 2026-07-31 by owner ruling: yes, when available** — see §4.1. I had recommended omitting it in Phase A on staleness grounds; the owner ruled otherwise and the design now follows that. The residual implementation risk is the conditional `-l` (§4.1): getting it wrong converts "no fix" into "no alarm", so it belongs in the Task 8 bench matrix, not in a code review.
4. **§5 — is the idle-paint rule sufficient?** It prevents a paint from *starting* mid-exchange, but a paint already in progress when an RTS lands still holds the bus for one page (~3 ms). That should be inside the RX window slop, but it is an assumption a reviewer familiar with the metal RX path should confirm.
5. **§3.1 — two canned messages, or three?** The draft listed three, one of which became the long-press path. If a third benign message is wanted ("moving on", "waiting here"), it is one more slot — but each slot lengthens the cycle for every other interaction.
