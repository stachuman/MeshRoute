# T1000-E (mobile-only) firmware variant — feasibility note

*2026-07-14. Assessment only — not scheduled.*

**Verdict: feasible and well-bounded.** Mobile-only is the right scope.

## MCU — free
The SenseCAP T1000-E is an **nRF52840** (811 KB flash / 235 KB RAM, variant `Seeed_T1000-E`, SoftDevice s140 7.3.0) — the same MCU MeshRoute already ships on `xiao_sx1262` and `gateway`. So BLE, LittleFS/QSPI inbox, fault-log, USB-CDC, and light-sleep already run on this silicon, and the mobile role is just a build flag (`-DMR_PROFILE_MOBILE`, the `xiao_mobile` pattern).

## Radio — the one real piece of work
The T1000-E uses the **Semtech LR1110**, not the SX1262 (MeshRoute's only PHY today). The architecture keeps this bounded rather than a rewrite:

- `lib/hal/iradio.h` is a clean ~12-method seam; `DeviceHal` uses the radio only through it (native tests run on a `MockRadio`). The chip-specific code is a single TU — `Sx1262Radio : IRadio` (`lib/hal/device_radio.h`, 340 lines).
- MeshRoute already vendors MeshCore's `CustomSX1262.h`. MeshCore has the exact parallel to vendor the same way: `CustomLR1110.h` + `CustomLR1110Wrapper.h` + `LR11x0Reset.h`, plus a **working** T1000-E config (pin map, RF-switch table, DIO3-TCXO @ 1.6 V, RX-boosted-gain).
- SF/BW/CR/freq are standard-LoRa RadioLib calls that the LR1110 class supports with the same shape.

### Scope (4 steps)
1. Vendor the 3 LR1110 headers (mirror the SX1262 vendoring). *small*
2. Write `Lr1110Radio : IRadio` — parallel to `Sx1262Radio`: LR1110 init/reset, RF-switch table, TCXO, RX-boosted-gain, LR11x0 IRQ constants, LR1110 CAD/RSSI for `channel_busy`. *medium — the bulk; needs the physical board to nail RF-switch/TCXO/CAD.*
3. Add the `t1000e_mobile` env + the `Seeed_T1000-E` nRF52 variant + pin map (all present in MeshCore's `variants/t1000-e/`) + `-DMR_PROFILE_MOBILE`. *small*
4. Branch `device_hal.cpp` to instantiate `Lr1110Radio` under the T1000-E env. *small*

## Interop
LR1110 and SX1262 both use standard LoRa modulation, so a T1000-E talks to the existing SX1262 fleet on the same freq/SF/BW/CR — no frame or protocol change; the airtime model is chip-agnostic. (Bench-verify sync-word/preamble; MeshCore uses the same defaults.)

## Effort / risks
Scaffolding (env/variant/vendor/wire-up) is a few hours of known patterns. The LR1110 driver plus on-hardware bring-up is ~1–3 days, de-risked by MeshCore's working `CustomLR1110` and proven T1000-E config. Watch: RadioLib 7.6 LR1110 LoRa maturity; `CustomLR1110` possibly pulling GNSS/WiFi deps (ignore for LoRa-only — confirm it compiles standalone); the vendored-file Serial-in-error-path caveat (same as `CustomSX1262`).


======================================================

  # Autonomous mobile tracker — T1000-E primary, Heltec WiFi LoRa 32 V4 variant

  Date: 2026-07-14
  Status: design proposal for discussion
  Scope: MR_PROFILE_MOBILE only. Stationary nodes and gateways are out of scope.

  ## 1. Goal

  MeshRoute gains an autonomous mobile-tracker profile that remains a normal member of the existing mobile team while operating
  usefully without a connected phone.

  The device can:

  - acquire its own position;
  - share positions within its mobile team when team location sharing is enabled;
  - retain and relay recent teammate positions;
  - monitor an uploaded route or geofence locally;
  - warn the wearer when off-route or outside the allowed area;
  - indicate that a DM arrived while the phone is disconnected.

  The SenseCAP T1000-E is the primary product-like target. The Heltec WiFi LoRa 32 V4 provides the same protocol and autonomy
  behavior as a development-board variant.

  ## 2. Existing foundation

  This design builds on functionality already implemented:

  - mobile-node role and mobile registration;
  - mobile-team provisioning through team_id;
  - team-local IDs and the separate team routing plane;
  - full team routing BCNs;
  - multi-hop routing through mobile team members;
  - team-scoped channel messages ignored by the static mesh;
  - stable sender identity through key_hash32;
  - the existing six-byte pack_loc6/unpack_loc6 location codec;
  - BLE/console configuration and companion known-node infrastructure.

  This does not introduce another mesh role. Both hardware variants remain mobile nodes and never act as stationary
  infrastructure.

  ## 3. Product decisions

  1. Location sharing is a team setting named team_share_location.
  2. It defaults to OFF.
  3. The companion provisions the setting independently onto each team member.
  4. No received radio packet may enable location sharing. Remote policy synchronization would require a future authenticated
     team-configuration mechanism.

  5. Route and geofence monitoring are independent of location sharing. They continue when:
      - team sharing is disabled;
      - BLE is disconnected;
      - the phone is absent;
      - no mesh peers are reachable.

  6. No valid location fix means no location transmission.
  7. Existing loc_in_dm behavior remains independent. Enabling team sharing does not automatically attach location to DMs.
  8. Team position propagation is bounded soft state. It is not a reliable tracking log and does not guarantee delivery of every
     sample.

  ## 4. User-visible behavior

  ### 4.1 Team tracking

  When team_share_location is enabled, every team member:

  1. obtains fixes from its onboard GNSS receiver or a recent phone-fed fallback;
  2. publishes its latest valid position in its team BCN;
  3. learns recent positions from matching-team BCNs;
  4. re-advertises fresh teammate positions so they can cross a multi-hop team;
  5. exposes the resulting position cache to the companion map.

  When the setting is disabled, the node:

  - does not originate a team position;
  - does not relay teammate positions;
  - does not expose received team positions to the UI;
  - may still run local navigation.

  If devices have inconsistent team settings, enabled devices may still transmit over the air. Disabled devices will neither
  display nor propagate those records. The companion should expose the local setting of each connected device so such
  inconsistencies can be found.

  ### 4.2 Autonomous route monitoring

  Before a hike, the companion can upload an activity profile containing:

  - an optional route polyline;
  - an off-route corridor, for example 100 metres;
  - an optional allowed-area geofence;
  - alarm enablement and repetition settings.

  The first version supports:

  - one active route;
  - at most 128 simplified route points;
  - one circular geofence or a polygon of at most 32 points;
  - independently enabled route and geofence checks;
  - transactional upload with profile ID, version and CRC.

  The companion imports and simplifies GPX data. Firmware does not parse GPX or render maps.

  ### 4.3 Local indications

   Event               T1000-E                                                Heltec V4
  ━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   New DM              Short buzzer and LED pattern; unread state retained    LED pattern and OLED unread indicator; optional
                                                                              external buzzer
  ──────────────────  ─────────────────────────────────────────────────────  ─────────────────────────────────────────────────────
   Off route           Repeating buzzer and LED until acknowledged            Persistent OLED warning and repeating LED; optional
                                                                              external buzzer
  ──────────────────  ─────────────────────────────────────────────────────  ─────────────────────────────────────────────────────
   Outside geofence    Distinct repeating buzzer/LED pattern                  Persistent OLED warning and repeating LED
  ──────────────────  ─────────────────────────────────────────────────────  ─────────────────────────────────────────────────────
   GNSS unavailable    Low-priority status indication                         OLED warning with fix age
  ──────────────────  ─────────────────────────────────────────────────────  ─────────────────────────────────────────────────────
   Low battery         Periodic low-priority indication                       OLED warning and LED

  A short button press silences the current indication. It does not clear the underlying route or geofence condition.

  A DM produces a one-shot notification and retained unread state. It does not repeat like a navigation alarm.

  ## 5. Common architecture

  Board-specific GNSS and alert drivers sit below a shared mobile-services layer:

  Onboard/external GNSS ---> ILocationProvider ----+
  Phone-fed fallback ------>                      |
                                                  v
                                       MobileLocationService
                                        |        |         |
                                        |        |         +--> NavigationMonitor
                                        |        |                |
                                        |        |                v
                                        |        |          IAlertOutput
                                        |        |
                                        |        +--> BLE status
                                        |
                                        +--> own team-position record
                                                  |
  Team BCN RX ------> TeamPositionCache <---------+
                           |
                           +--> bounded BCN re-advertisement
                           +--> companion query/push

  Suggested board-neutral interfaces:

  struct LocationFix {
      int32_t  lat_e7;
      int32_t  lon_e7;
      uint32_t age_ms;
      uint16_t accuracy_m;
      uint8_t  source;
      bool     valid;
  };

  class ILocationProvider {
  public:
      virtual void begin() = 0;
      virtual void set_power_mode(LocationPowerMode mode) = 0;
      virtual void poll(uint64_t now_ms) = 0;
      virtual bool latest(LocationFix& out) const = 0;
  };

  class IAlertOutput {
  public:
      virtual void signal(AlertKind kind, bool active) = 0;
      virtual void acknowledge() = 0;
      virtual void tick(uint64_t now_ms) = 0;
  };

  These components should live in a small platform-neutral lib/mobile/ area. fw_main.cpp constructs and pumps the board-specific
  implementations.

  The mesh Node should receive only validated, compact location data. UART, NMEA, OLED and buzzer details must not enter the
  protocol core.

  ## 6. Configuration and persistence

  Team policy:

  team_share_location = off | on
  team_pos_moving_s    = 60
  team_pos_idle_s      = 300
  team_pos_distance_m  = 50
  team_pos_max_age_s   = 120

  Only team_share_location needs to be configurable in the initial version. The cadence values may begin as compiled defaults and
  later move into the companion’s advanced team settings.

  Activity profile:

  activity_enabled
  activity_id
  activity_version
  activity_crc
  route_point_count
  route_points
  route_corridor_m
  geofence_kind
  geofence_geometry
  alarm_repeat_s

  Changing teams clears the teammate-position cache but does not delete the local activity profile.

  Leaving a team disables team sharing because team_id == 0, while local route monitoring may continue.

  ## 7. Team-position BCN extension

  Reserve BCN extension type 6 for team positions. Types 1–5 are already assigned, with type 5 carrying team_id.

  TEAM_POSITION TLV

  type       = 6
  body_len   = 14

  byte 0..3   subject_hash32, little-endian
  byte 4..9   location using existing loc6 encoding
  byte 10..11 fix_age_s, little-endian
  byte 12     flags
                b0 moving
                b1 phone-fed fix; 0 means onboard GNSS
                b2 reduced-quality fix
                b3..b7 reserved
  byte 13     battery_pct, 0..100; 255 means unknown

  The record occupies 15 bytes including its type/length byte, which fits the existing four-bit TLV body-length limit.

  subject_hash32 is required because a node may relay another member’s position. The enclosing beacon sender is therefore not
  necessarily the position subject.

  ### 7.1 Acceptance rules

  A position record is accepted only when:

  - the same BCN carries a type-5 team ID;
  - that team ID matches the receiver’s non-zero team_id;
  - the receiver is a mobile team member;
  - team_share_location is enabled;
  - the record is structurally valid and not expired.

  The originator uses its own stable key_hash32 as subject_hash32.

  The receiver converts fix_age_s into an estimated local fix time. It replaces a cached record only when the incoming record
  represents a newer fix. This prevents an older relayed position from overwriting a recent direct one.

  Unknown flag bits are ignored. Older firmware skips the unknown type-6 TLV.

  Static nodes and lone mobile nodes neither originate nor consume type 6.

  ## 8. Multi-hop position propagation

  A position carried only in its owner’s BCN is inherently one-hop. Team-wide sharing therefore requires bounded position gossip.

  Each team member maintains a fixed-size TeamPositionCache, initially proposed as 16 entries, keyed by subject_hash32.

  Rules:

  - At most one position TLV is included in each BCN.
  - A newly changed local position has priority.
  - Otherwise, fresh teammate records rotate fairly.
  - A received position is considered for a normal future BCN.
  - Receiving a position never triggers an immediate relay cascade.
  - Relayed records retain their original age.
  - Relaying never refreshes the record’s lifetime.
  - Records expire after a proposed 15-minute TTL.
  - Meaningful local movement may request a triggered BCN.
  - Existing BCN throttling, LBT, anti-spam and duty accounting remain authoritative.

  This provides eventual multi-hop propagation without using reliable DATA frames or producing a flood for each GNSS fix.

  ## 9. Position and GNSS cadence

  Initial adaptive policy:

   State                                GNSS sampling                                   Position publication
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   Active route and moving              Every 10–15 s                      Every 60 s or after 50 m movement
  ───────────────────────────────  ───────────────────  ─────────────────────────────────────────────────────
   Active route and stationary          Every 30–60 s                                            Every 5 min
  ───────────────────────────────  ───────────────────  ─────────────────────────────────────────────────────
   No activity, sharing enabled            Every 60 s                                            Every 5 min
  ───────────────────────────────  ───────────────────  ─────────────────────────────────────────────────────
   No activity, sharing disabled    GNSS sleeping/off                                                   None
  ───────────────────────────────  ───────────────────  ─────────────────────────────────────────────────────
   Active alarm                         Every 10–15 s    One rate-limited triggered BCN, then normal cadence

  These are field-tuning defaults, not battery-life guarantees.

  A fix older than team_pos_max_age_s is not originated. The companion may retain an older last-known position, but it must
  display its age.

  Phone-fed positions are fallback inputs and carry their own age. They are subject to the same maximum-age rules.

  ## 10. Navigation monitor

  The monitor evaluates only usable fixes. A usable fix is:

  - valid;
  - younger than the configured limit;
  - within an accuracy ceiling, initially 50 metres.

  An unusable fix creates a separate GNSS unavailable state. It must never be interpreted as an off-route or outside-geofence
  condition.

  For each usable fix:

  1. calculate the shortest distance to the route polyline;
  2. compare it with route_corridor_m;
  3. evaluate the active geofence;
  4. apply dwell and hysteresis;
  5. update the local alarm state.

  Initial anti-flap behavior:

  - enter an alarm after three consecutive outside fixes or 30 seconds outside;
  - clear after two consecutive inside fixes;
  - for a 100 m route corridor, clear only after returning within 80 m;
  - treat lack of fix separately from being outside.

  Geometry should be implemented in platform-neutral code with native tests covering:

  - route segment endpoints;
  - degenerate segments;
  - polygon boundaries;
  - the antimeridian;
  - high latitudes;
  - poor-accuracy fixes.

  Route and geofence alarms remain local in version 1. They are not automatically transmitted to teammates. A later
  share_safety_alerts team policy could add this explicitly.

  ## 11. Privacy and trust boundary

  team_id is a routing discriminator, not a secret or authorization key.

  The proposed position BCN is plaintext. Nearby radio observers can read it. Under the current beacon trust model, an attacker
  may also spoof a team ID or subject identity.

  The companion must therefore describe this setting as radio-visible team sharing, not encrypted team location.

  Confidential and authenticated team tracking requires a future team key and an AEAD-protected position record with replay
  protection. That work is separate from this proposal.

  ## 12. Companion behavior

  The companion owns:

  - editing team policy;
  - importing and simplifying routes;
  - activity-profile upload;
  - rendering maps;
  - long-term position history;
  - presenting alarms and stale-state information.

  Firmware owns:

  - GNSS acquisition;
  - current fix validation;
  - bounded teammate cache;
  - route/geofence evaluation;
  - local hardware alarms.

  Required companion additions:

  - Share member locations under Team Settings;
  - clear plaintext-radio privacy explanation;
  - activity/route import and configuration;
  - transactional activity upload and verification;
  - team map showing position age, battery and fix source;
  - stale styling for old positions;
  - device status showing GNSS state, active activity, route distance, geofence state and alarm;
  - unread-DM indication.

  Suggested firmware surface:

  team_positions
  team_position {...}
  nav_status {...}

  activity begin ...
  activity chunk ...
  activity commit ...
  activity clear

  Large route uploads should use a bounded binary/chunked transport rather than quoted console text.

  Team positions should update the existing known-node/NodeEntity model keyed by key_hash32, not create a separate companion
  identity store.

  ## 13. T1000-E hardware profile

  ### 13.1 Hardware

  The T1000-E provides:

  - nRF52840;
  - LR1110 LoRa radio;
  - integrated MediaTek AG3335 UART GNSS;
  - 700 mAh rechargeable battery;
  - battery measurement;
  - button;
  - green LED;
  - PWM buzzer;
  - accelerometer;
  - temperature and light sensors;
  - integrated antennas and IP65 enclosure.

  The autonomous position source is the AG3335 receiver. It does not depend on the LR1110 cloud-assisted GNSS scanner or an
  Internet position solver. See the official T1000-E datasheet
  (https://files.seeedstudio.com/products/SenseCAP/SenseCAP_Tracker_T1000_Datasheet.pdf).

  ### 13.2 Required MeshRoute work

  1. Add the T1000-E nRF52 variant and t1000e_mobile environment.
  2. Vendor the LR1110 RadioLib/MeshCore helpers.
  3. Implement Lr1110Radio : IRadio parallel to Sx1262Radio.
  4. Construct the LR1110 radio stack conditionally in fw_main.cpp. The current concrete CustomSX1262, Sx1262Radio and DeviceHal
     objects are instantiated there, not in device_hal.cpp.

  5. Bring up:
      - RF-switch table;
      - DIO3 TCXO at 1.6 V;
      - RX boosted gain;
      - async receive and transmit;
      - DIO IRQ behavior;
      - SF/BW/CR retuning;
      - RSSI/LBT;
      - MeshRoute sync word.

  6. Implement the AG3335 UART provider and power/reset/sleep sequence.
  7. Implement battery measurement, button handling, LED patterns and a non-blocking buzzer driver.
  8. Optionally use the accelerometer for stationary detection after GNSS correctness is established.

  The T1000-E is the acceptance-reference device for the requirement: phone disconnected, route monitoring still active.

  ## 14. Heltec WiFi LoRa 32 V4 variant

  ### 14.1 Hardware

  The V4 provides:

  - ESP32-S3R2;
  - 16 MB external flash and 2 MB PSRAM;
  - SX1262 with the V4 RF front end;
  - OLED;
  - user button and LED;
  - battery connector and measurement;
  - solar input;
  - powered SH1.25 eight-pin GNSS connector.

  It does not include the GNSS receiver itself and has no onboard buzzer or haptic actuator. These differences are documented by
  Heltec’s V3/V4 comparison (https://docs.heltec.org/en/node/esp32/wifi_lora_32/index.html).

  The mobile tracker variant therefore requires:

  - a selected compatible 3.3 V UART NMEA GNSS module;
  - the correct connector cable;
  - suitable GNSS antenna placement;
  - battery and enclosure;
  - optionally an external buzzer or haptic actuator.

  ### 14.2 Required MeshRoute work

  1. Add a distinct heltec_v4_mobile environment. The existing heltec_mobile environment targets the V3 board and must not be
     silently reused.

  2. Reuse Sx1262Radio, but add V4-specific board support.
  3. Implement the proven V4 RF-front-end control:
      - PA/LNA mode switching;
      - DIO2 RF-switch behavior;
      - 1.8 V TCXO;
      - current limit;
      - RX register patch;
      - conservative output-power mapping.

  4. Respect regional EIRP limits. The high-power board capability does not authorize transmitting at maximum power.
  5. Implement external GNSS UART and power control.

     The local MeshCore reference uses:

     GNSS RX       GPIO 38
     GNSS TX       GPIO 39
     GNSS reset    GPIO 42
     GNSS enable   GPIO 34

     These pins must be verified against the exact V4 revision and selected GNSS module.

  6. Implement OLED pages for:
      - team/link state;
      - GNSS fix and age;
      - current route distance;
      - active navigation alarm;
      - battery;
      - unread DM.

  7. Use OLED and LED as the baseline alert output.
  8. Support an optional configured GPIO for an external active buzzer or haptic actuator.
  9. Validate light-sleep behavior with radio DIO1, GNSS UART, BLE, button and the external RF front end.

  The Heltec V4 is a good development and rich-UI variant, but it is not equivalent to the sealed T1000-E until the GNSS module,
  battery, enclosure and alert hardware are selected.

  ## 15. Delivery slices

  ### Slice A — team-position protocol

  - Type-6 codec.
  - Team-position cache.
  - Same-team acceptance rules.
  - One-record-per-BCN rotation.
  - Multi-hop age and expiry handling.
  - Persisted team_share_location, default OFF.
  - Companion snapshot and update events.
  - Native and simulation tests.

  ### Slice B — autonomy core

  - ILocationProvider.
  - MobileLocationService.
  - Transactional activity store.
  - Route and geofence geometry.
  - Dwell and hysteresis state machine.
  - IAlertOutput.
  - DM-arrival hardware event.
  - Native tests.

  ### Slice C — T1000-E

  - LR1110 radio port.
  - T1000-E environment and pin map.
  - AG3335 GNSS.
  - Power, battery, button, LED and buzzer.
  - Cross-radio interoperability testing.

  ### Slice D — Heltec V4

  - V4 environment and RF-front-end support.
  - Selected external GNSS module.
  - OLED/button/LED UI.
  - Optional buzzer output.
  - Power and sleep validation.

  ### Slice E — companion

  - Team sharing settings.
  - Activity and GPX preparation.
  - Transactional upload.
  - Team map.
  - Navigation and alarm status.
  - Hardware bench validation with both targets.

  ## 16. Verification gates

  ### Protocol

  - Type-6 codec round-trip and malformed-input tests.
  - Sharing OFF produces no position TLV.
  - No valid fix produces no position TLV.
  - Static, lone-mobile and wrong-team nodes do not consume or relay positions.
  - Existing static and non-team scenarios remain byte-identical.
  - Three-node line A—B—C proves A’s position reaches C.
  - The relayed record becomes older at B and expires normally.
  - Multiple subjects rotate without starvation.
  - No BCN carries more than one position record.
  - Triggered BCNs still obey duty, LBT and beacon throttling.

  ### Navigation

  - Route-segment and geofence geometry tests.
  - Poor/no fix does not produce a false route violation.
  - Dwell and hysteresis prevent alarm chatter.
  - Monitoring works with BLE disconnected.
  - Monitoring works with sharing disabled.
  - Interrupted upload leaves the previous committed activity intact.

  ### T1000-E

  - LR1110-to-SX1262 operation at every supported SF/BW/CR.
  - SF=5 is treated as a separate PHY bring-up gate because it is already known not to work reliably.
  - Continuous RX, async TX, retuning, LBT and sleep/wake.
  - Outdoor cold and warm GNSS fixes.
  - DM and navigation alert distinction.
  - Button acknowledgement.
  - Measured battery behavior under moving, stationary and inactive profiles.

  ### Heltec V4

  - PA/LNA mode and output-power validation.
  - External GNSS cold/warm fix and power switching.
  - OLED, LED, button and optional buzzer.
  - Radio/GNSS/BLE wake behavior.
  - Measured battery behavior on the assembled tracker.

  ## 17. Non-goals for version 1

  - Stationary-node position broadcasting.
  - Turn-by-turn navigation.
  - Map rendering on the tracker.
  - GPX parsing in firmware.
  - Guaranteed delivery of every position.
  - Long-term location history stored on the tracker.
  - Automatic sharing of route/geofence alarms.
  - Remote activation of location sharing.
  - Claims that team positions are encrypted or authenticated.
  - Indoor Wi-Fi/BLE positioning.
  - Accelerometer dead reckoning.
  - Automatic SOS escalation.

  ## 18. Open decisions

  1. Select one supported Heltec V4 GNSS module and cable.
  2. Decide whether Heltec V4 must have an external buzzer/haptic device for the first tracker release.
  3. Confirm the proposed 16-member cache and 15-minute expiry.
  4. Decide whether version 1 needs polygon geofences or whether a circular geofence plus route corridor is sufficient.
  5. Decide whether plaintext radio-visible team tracking is acceptable for field use. If confidentiality is required, a team-key
     design must precede the position BCN implementation.

