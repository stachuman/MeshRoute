<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Heltec V4 mobile L76K GNSS and producer-controlled sealed location — design specification

First written 2026-08-25 against MeshRoute
`1a897ffe4d755ecc9996215bdbcce5ef1e95f834` plus the owner's uncommitted UI/V4 documentation work.

**Status: FIRST REVIEW DRAFT. The settled owner rulings are recorded below. D1 and D2 are settled; the specification
is ready for final review and implementation planning.**

This is a follow-on to
[`2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md`](./2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md).
That specification deliberately excludes GNSS. This one starts only after its V4-4 slice has produced the
`heltec_v4_mobile` profile; it does not widen the radio-port slice while that work is in progress.

---

## 0. Authority and owner rulings

### 0.1 Settled owner rulings

The following are requirements, not proposals:

1. GNSS is supported only on a **mobile** Heltec V4.2/V4.3 using the external **Heltec L76K** module on the dedicated
   eight-pin connector.
2. Heltec V3 gets no GNSS support in this slice.
3. A static node never operates the GNSS receiver. Static location remains manually configured through the existing
   serial/BLE command path.
4. A valid mobile GNSS fix remains usable for **15 minutes**. At age `>= 900000 ms` it is unavailable.
5. GNSS fixes are runtime state. They are not written to `/mrid`, `/mrcfg`, the inbox, or another durable record.
6. A plaintext DM or plaintext channel post never carries location.
7. The existing `send ... -l` and `send_channel ... -l` forms remain the explicit attach-location request. Omitting
   `-l` means **do not attach location**, even when GPS is ON and a fresh fix exists.
8. The simple on-device Heltec mobile send path defaults to sharing for the complete eligible set enumerated in
   section 4.5: when GPS is ON and a fresh fix is available, it adds `-l` to the ruled encrypted app DM or
   team-key-sealed, team-only channel post. With GPS OFF or no fresh fix, it omits `-l` and the ordinary message still
   sends.
9. Serial/BLE controllers and richer companions such as a LilyGo T-Deck or iOS app own their per-message policy.
   They request location by including `-l` and suppress it by omitting `-l`; firmware must not rewrite that choice.
10. Configuration intent is not the authority. The carrier that will actually be emitted is the authority: direct
   `DATA_FLAG_CRYPTED`, `DATA_TYPE_SEALED_RELAY`, or a team-key-sealed channel post.
11. A global channel, a plaintext/keyless team post, and a mixed team+global post never carry location. Location does
    not force encryption and is never copied into a clear fallback.
12. No fresh location is an ordinary condition for the simple Heltec default: the on-device producer omits `-l` and
    continues without location. Once any producer includes `-l`, however, the request is strict and refuses
    `no_location` when no usable location exists.
13. If a message fits without the requested location but does not fit with it, the entire send is refused. Location
    is never silently stripped to make the text fit. The user receives a distinct
    `location_too_large` result.
14. On the simple Heltec mobile, the user controls its default sharing behavior by switching the GNSS module **ON or
    OFF**. There is no second Heltec `share_location` preference. This does not override commands supplied by an
    advanced companion: GPS ON makes a fresh fix available, while that producer's presence or absence of `-l`
    decides whether a particular message requests it.

### 0.2 D1 — settled: cross-layer and delegated encrypted DMs

**Source finding:** MeshRoute has two encrypted app-DM carriers, not one:

- a same-layer `DATA_FLAG_CRYPTED` frame, whose existing sealed plaintext can already carry `DATA_FLAG_LOCATION`;
- `DATA_TYPE_SEALED_RELAY`, used for delegated and cross-layer encrypted DMs.

The current SEALED_RELAY builder hard-codes no location, and its receiver has no format bit from which to discover
one. Silently excluding this carrier would make the same explicit `-l` request succeed or fail solely because
routing selected another layer. It would also make the simple Heltec default route-dependent.

**Owner ruling: include SEALED_RELAY and do not bump `wire_version`.** Section 8.4 reuses the existing encrypted
relay `origin` byte, which the builder always writes as zero and the receiver deliberately ignores, as
`relay_flags`. It reuses the existing `DATA_FLAG_LOCATION` bit and `pack_loc6`; no new flag bit, frame type, clear
header byte or always-present encrypted byte is added. An unlocated relay remains byte-identical and a located relay
grows only by the existing six-byte location.

MeshRoute is still under development, so the owner explicitly rules that this change does not require a wire-version
bump. The relay-format work remains isolated in GPS-1 for attribution, and its wire documentation and tests change
in that slice.

### 0.3 D2 — settled: persisted OFF default and first-slice sleep policy

The user-facing GNSS power setting needs a default and a reboot policy. Separately, the current ESP32-S3 idle path
light-sleeps for up to one second with no UART wake source. Entering that sleep while 9600-baud NMEA is arriving can
lose the sentence. Espressif's UART wake also deliberately loses the triggering characters, while the L76K 1PPS pin
is not documented as a complete NMEA-delivery handshake. Neither mechanism should be declared reliable before a
metal test proves it.

**Owner ruling:** `gps_enabled` defaults **OFF**, is persisted, and takes effect live. When ON,
the L76K stays in Full-on mode and MeshRoute inhibits MCU light sleep. When OFF, the rail is disabled and existing
sleep behavior resumes exactly. This is privacy-safe and correctness-first, but GPS ON has an explicit battery cost.
A later, separately measured power slice may use repeated NMEA UART wake, PPS wake, or L76K Standby; it must not be
folded into initial acquisition correctness.

---

## 1. Goals and exclusions

### 1.1 Goals

This design delivers:

- non-blocking L76K power, reset, UART and NMEA service on `heltec_v4_mobile`;
- a truthful fresh/stale/unavailable location authority;
- one persisted live GNSS ON/OFF control reachable through USB serial and BLE-NUS;
- a simple Heltec on-device default for the explicitly enumerated sealed app traffic;
- explicit per-message location control for serial/BLE and richer companion producers through the retained `-l`;
- route-independent honoring of that request by every supported encrypted app-DM and sealed team-only carrier;
- no location on plaintext or internal traffic;
- exact size preflight and a distinct `location_too_large` result;
- console and BLE/JSON diagnostics sufficient to tell OFF, acquisition, a fresh fix, stale fix and no NMEA apart;
- host-testable parsing and policy logic plus focused V4 metal acceptance.

### 1.2 Explicitly out of scope

- GNSS on Heltec V3, XIAO, gateways or static Heltec V4 profiles;
- phone-fed mobile location or choosing between phone and receiver;
- GPS time discipline, RTC setting, altitude, speed, course, geofencing, tracks or waypoints;
- changing the LoRa radio/FEM design or the V4 revision detector;
- a new OLED screen, string, menu row or gesture for GNSS control;
- location in plaintext, global channels, mixed team+global posts, beacons or route frames;
- location in internal traffic such as acknowledgements, key grants, remote administration or custody reports;
- simulator-generated GNSS fixes or GNSS timing events;
- deep sleep;
- low-power UART/PPS wake or L76K Standby until a separate metal-proven slice;
- changing peer-location retention. An authenticated received position continues through the existing Push and
  RAM-only peer-location cache.

---

## 2. Present source state, verified 2026-08-25

### 2.1 Board/profile state

There is no Heltec V4 environment or GNSS driver in the current tree. The approved V4 port specification creates
`heltec_v4`, `heltec_v4_mobile` and `gateway_heltec_v4`; this work targets only the mobile profile and must not create
a parallel board port.

No TinyGPS, MicroNMEA or L76K parser is present. `fw_main.cpp` currently owns the service-loop ordering and delegates
feature work to `firmware_*` modules. The GNSS implementation follows that same boundary rather than growing feature
logic in `fw_main.cpp`.

### 2.2 Existing location authority

MeshRoute currently carries location as `NodeConfig::lat_e7/lon_e7`, mirrored by `g_lat_e7/g_lon_e7` on firmware.
`(0,0)` is the existing “unset” sentinel. Static coordinates live in `/mrid`; `cfg set lat` and `cfg set lon` both
persist and update the live Node.

The coordinate pair alone is not a sufficient future runtime authority: `(0,0)` is a real GNSS position, and expiry
is a fact distinct from two numbers. This design therefore adds a runtime-only `NodeConfig::location_available` fact.
Existing static/manual producers seed it from the current non-zero sentinel; GNSS sets it from fix validity and age.
It is not another coordinate source and is never persisted as a fix.

GPS-2 owns this `NodeConfig::location_available` addition. Because `NodeConfig` is embedded in `Node`, GPS-2 is a
deliberate lib/core layout slice even if the bool happens to occupy padding on one ABI. Before editing, record the
current native sizes; after editing, update permanent `sizeof(NodeConfig)` and `sizeof(Node)` tripwires with measured
old→new figures and the field/alignment arithmetic, then obtain the named board RAM deltas. Never infer a board ABI
from native alignment and never move the field solely to hide its honest cost.

The OLED status/team projections already consume the effective coordinates. This design feeds those same values;
it does not create a second set of coordinates for the UI or MAC.

### 2.3 Existing send behavior

Location is currently explicit per message:

- `send ... -l` asks a direct/same-layer sealed DM to attach the existing 6-byte `pack_loc6` position;
- `send_channel ... -t -l -e` asks a sealed team post to carry location;
- a location request without a usable seal is refused `unsealable`;
- a location request without coordinates is refused `no_location`;
- `send_layer -l` and delegated/cross-layer location are refused because SEALED_RELAY cannot describe location.

The actual encryption decision already lives in the core, after configuration, key availability, team/global scope
and route/carrier selection are known. The simple Heltec convenience belongs in its existing typed OLED send
adapter, which conditionally composes `-l`. Adding the flag in the console/BLE dispatcher or inferring it globally
inside the core would overwrite an advanced producer's deliberate omission and would restore the “configuration
says encrypted” bug class. The producer supplies the per-message `-l` intent; the core validates that intent only
after it knows the actual carrier.

### 2.4 Existing wire carriers and size surface

- Direct sealed DM plaintext is `[origin][source_hash?][loc6?][body]` inside the existing pairwise AEAD envelope.
- Sealed team-channel plaintext is `[flags][source_hash if located][loc6 if located][text]` inside the existing team
  AEAD envelope.
- SEALED_RELAY body is `[seal_ctr 2 LE][seed8 8][ciphertext||tag]`; the decrypted plaintext currently has no inner
  flags byte and its open path hard-codes SOURCE_HASH-only parsing.

`pack_loc6` is the single wire codec and remains so. A second coordinate encoder is forbidden.

### 2.5 Existing errors and transports

Synchronous sends use `CmdCode::err_too_large`; asynchronous failures use `SendFailReason::too_large`. USB renders
the latter in `fw_main.cpp`, and BLE/JSON maps both enums in `console_json.cpp`. A distinct requested-location
overflow must be added to all of these surfaces, not represented only by a log line.

### 2.6 Existing sleep behavior

After the boot grace and without an attached host/BLE session, ESP32-S3 idle service calls
`esp_light_sleep_start()`. DIO1, a bounded timer and the temporarily armed UI button are wake sources. There is no
UART wake. This is safe for a headless radio today and is not safe to assume for a continuous NMEA byte stream.

---

## 3. Hardware and dependency contract

### 3.1 Supported hardware

The physical target is exactly:

- Heltec WiFi LoRa 32 V4.2 high-power plus Heltec L76K GNSS module;
- Heltec WiFi LoRa 32 V4.3 high-power plus the same module;
- the dedicated 1.25 mm eight-pin connector and its supplied cable.

The V4.2 and V4.3 schematics agree on the GNSS nets:

| purpose | ESP32-S3 GPIO | direction at MeshRoute MCU | electrical rule |
|---|---:|---|---|
| L76K RX / `RX_GPS` | 38 | TX from MCU to module | UART1 TX |
| L76K TX / `TX_GPS` | 39 | RX into MCU from module | UART1 RX |
| L76K `WAKE_UP` | 40 | output-capable | active LOW; unused in first slice |
| L76K 1PPS | 41 | input | 100 ms HIGH pulse; diagnostic only in first slice |
| L76K `RESET_N` | 42 | output | active LOW; hold LOW at least 10 ms for recovery reset |
| switched GNSS rail `VGNSS_Ctrl` | 34 | output | active LOW enables the high-side switch |

The direction names are module-side names. Arduino `Serial1.begin(..., rxPin, txPin)` must therefore receive
`rxPin=39` and `txPin=38`; swapping the macro names while preserving their labels is a silent no-data failure.

### 3.2 L76K operating contract

The first version uses the receiver's documented power-on defaults:

- 9600 baud, 8 data bits, no parity, 1 stop bit;
- NMEA 0183 4.1;
- 1 Hz update;
- RMC and GGA among the default sentences;
- Full-on acquisition/tracking after power-on.

No PMTK command is sent: L76K uses the PCAS command family, not PMTK. No PCAS reconfiguration is needed for this
slice. The driver never assumes that a one-second boot probe proves presence; cold acquisition is documented around
30 seconds without assistance, and NMEA startup/detection must remain recoverable.

Normal power-on does not pulse reset. Configure reset inactive, enable the rail, wait at least 250 ms without
blocking, then accept UART input. A recovery reset holds `RESET_N` low for at least 10 ms and again waits for the
post-reset valid-UART interval.

On power-off, stop UART service and place MCU TX/RX in high-impedance states before disabling the rail so GPIO38
cannot back-power an unpowered module.

### 3.3 Parser dependency

Use `stevemarple/MicroNMEA` pinned exactly to release `2.0.6`, and include it only in `heltec_v4_mobile`. It is the
compact, allocation-free parser already used by the pinned Heltec V4 reference implementation and accepts the GN
talker variants emitted by multi-constellation receivers.

Wrap the library behind a small pure `firmware_gnss_parse` adapter. Production code consumes only the adapter's
typed result; tests feed recorded/checksummed NMEA through the same adapter. Do not let MicroNMEA types escape into
`fw_main.cpp`, `NodeConfig`, console JSON or board code.

PlatformIO must use an exact version, not a caret range. Record the upstream license/provenance with the dependency.

### 3.4 Primary sources

- [Heltec L76 GNSS module product page](https://heltec.org/project/l76-gnss-module/)
- [Heltec V4.2 schematic](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/Schematic/WiFi_LoRa_32_V4.2.pdf)
- [Heltec V4.3 schematic](https://resource.heltec.cn/download/WiFi_LoRa_32_V4/Schematic/HTIT-WB32LAF_V4.3.pdf)
- [Quectel L76K hardware design](https://resource.heltec.cn/download/Heltec%20Capsule%20Sensor%20V3/Quectel_L76K_V1.1-1.pdf)
- [ESP32-S3 sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html)
- [Pinned MeshCore Heltec V4 reference](https://github.com/meshcore-dev/MeshCore/tree/0679dbeffc504d562d2f09eb072fdc223f8ffc2a/variants/heltec_v4)

---

## 4. Product semantics

### 4.1 Role and profile gate

GNSS hardware is active only when all three conditions hold:

1. the build has `MR_FEAT_GNSS` (`heltec_v4_mobile` only);
2. the live Node role is mobile;
3. persisted `gps_enabled` is true.

A static role forces the rail off even in the mobile-capable image. A role transition from mobile to static powers
GNSS down and removes the runtime fix before any later command can originate. A non-GNSS profile returns a loud
`unsupported` result for `cfg set gps`; it never accepts a dead setting.

`cfg set lat/lon` retains existing behavior on static and non-GNSS builds. On an active V4 GNSS-mobile role it is
refused as `managed_by_gnss` rather than saying “saved” for coordinates the GNSS authority will ignore. Existing
`/mrid` coordinates are left intact but are not loaded as a mobile fix; reflashing or reconfiguring the device as a
static node can use them again.

### 4.2 ON/OFF is the simple Heltec default-sharing control

There is exactly one user setting:

```text
cfg set gps 1
cfg set gps 0
```

BLE-NUS sends the same command line through the same handler. USB receives the text result; BLE receives either the
fresh `cfg` object on success or a structured error on refusal/save failure. No separate location-sharing toggle is
added to Node, NV, JSON or OLED.

ON means “power and acquire; let the simple on-device Heltec producer request a location when a fresh fix exists.”
It does not mean “a fix exists,” and it does not force location onto a command received from a companion. OFF means:

- stop acquisition and NMEA parsing;
- power down the GNSS rail;
- clear the effective mobile location immediately;
- make the simple Heltec default omit `-l`;
- make an explicit `-l` send fail `no_location`;
- restore the pre-GNSS idle-sleep permission.

The setting takes effect for a message when that message's sealed carrier is constructed. A payload already sealed
and queued retains its authenticated bytes if GPS is switched off later; OFF is not a destructive queue purge.

### 4.3 Valid fix and freshness

A fix becomes usable only after a checksum-valid RMC-active or GGA-valid sentence supplies a syntactically valid
latitude and longitude inside `[-90,+90]` and `[-180,+180]` degrees. Convert MicroNMEA's degree representation to
signed E7 with checked arithmetic before publishing.

Each accepted fix atomically publishes latitude, longitude, `location_available=true` and `last_fix_ms`.
Invalid/no-fix sentences do not erase a recent valid fix and do not refresh its age. The usable predicate is exactly:

```text
gps_enabled && mobile_role && have_valid_fix && now_ms - last_fix_ms < 900000
```

At age `>= 900000 ms`, set `location_available=false` and clear `g_lat_e7/g_lon_e7` plus the live Node coordinates
before processing another command. Status may continue to report that a prior fix became stale, but neither the old
coordinates nor a “last known” fallback may reach a send.

The explicit availability fact lets a checksum-valid GNSS fix at exactly `(0,0)` remain usable. The legacy static
`/mrid` format still treats `(0,0)` as unset; changing that durable manual-location format is outside this slice.

Power-on and reboot begin with no fix even if `/mrid` contains coordinates. No fix timestamp or coordinate is restored
from flash.

### 4.4 Static location

A static node continues to use its manually persisted `/mrid` coordinates. They do not expire because they describe
a fixed deployment. At load and after each manual coordinate update, the existing sentinel seeds
`location_available = (lat_e7 != 0 || lon_e7 != 0)`. A serial/BLE producer may request those coordinates with
`-l`; the static firmware never adds the flag automatically. Clearing both makes location unavailable.

Static nodes never initialize UART1, toggle GNSS power/reset/wake pins, parse NMEA or expose GPS as enabled.

### 4.5 Producer choice and core intent

The core has only two per-message intents:

| command intent | producer spelling | no location | plaintext carrier | sealed carrier with location |
|---|---|---|---|---|
| omit | no `-l` | send without location | send without location | send without location |
| required | `-l` | refuse `no_location` | refuse `unsealable` | attach location |

The simple Heltec on-device producer applies an opportunistic policy **before** issuing the command. The complete
eligible set in the present OLED is:

- the fixed emergency team-channel post: include `-l` when the effective fix is fresh, otherwise preserve its
  existing safety rule and send the alarm without `-l`;
- a canned team-only channel post: its command already requests `-e`; include `-l` when the effective fix is fresh,
  otherwise issue the same `-e` post without location (the core still refuses if no team key can seal it);
- a canned team-plane DM: include `-l` only when the effective fix is fresh **and** the live `e2e_dm` setting requests
  encryption; when `e2e_dm` is off, omit `-l` and preserve the existing plaintext send.

The K7 team-key grant, invite/reqpubkey operations, provisioning actions and every other internal/control command are
not eligible. A future on-device producer must be classified explicitly in this list rather than inheriting the
default by virtue of passing through `exec_command`. This convenience is not a third core intent and is not applied
to serial/BLE input. A T-Deck, iOS app or other advanced companion may expose a global, per-conversation or
per-message control, but its on-wire command still reduces to presence or absence of `-l`.

There is no additional force-omit flag because omission of `-l` already means exactly that. GPS OFF controls the
simple Heltec default and makes an explicit `-l` unavailable; it does not erase a companion's ability to express the
request and receive the truthful `no_location` refusal.

Take one immutable `LocationSnapshot {available, lat_e7, lon_e7}` at carrier-construction time. Encryption choice,
size preflight and packing all consume that snapshot. Re-reading mutable coordinates between preflight and packing
could accept one size and seal another position.

### 4.6 Message matrix

| outgoing application traffic | can honor `-l`? | reason |
|---|---:|---|
| direct pairwise `DATA_FLAG_CRYPTED` DM | yes | pairwise authenticated/confidential |
| delegated/cross-layer `DATA_TYPE_SEALED_RELAY` DM | yes | end-to-end sealed despite plaintext outer routing envelope |
| plaintext DM | refuse | eavesdroppable; `-l` must not force sealing |
| team-only channel sealed with held team key | yes | team authenticated/confidential |
| keyless/plaintext team channel | refuse | not sealed |
| global channel | refuse | no global content key exists |
| mixed team+global post | refuse | global copy defeats confidentiality; existing explicit encrypted form is refused |
| E2E/link ACK, INTRO, key grant, remote-admin, custody/internal report | never requested | protocol/internal traffic, not an app message |

Without `-l`, every row emits no location. With `-l`, the carrier—not `e2e_dm`, `team_channel_crypt`, `-e`, team
membership or key presence by itself—selects the result.

---

## 5. Firmware architecture

### 5.1 Feature seam

Add a feature-neutral GNSS API, following the existing UI seam shape:

```cpp
void mr_gnss_init(bool enabled, bool mobile_role);
void mr_gnss_tick(uint64_t now_ms);
bool mr_gnss_set_enabled(bool enabled, bool mobile_role);
bool mr_gnss_allows_sleep();
GnssStatus mr_gnss_status(uint64_t now_ms);
```

The non-GNSS inline implementation is inert: init/tick do nothing, enabling returns unsupported, sleep permission is
true and status says unsupported. `fw_main.cpp` calls the hooks unconditionally and contains no parser, pin,
freshness or privacy decision.

The exact signatures may be adjusted to match surrounding idiom, but these ownership boundaries are mandatory.

### 5.2 Module boundaries

`src/firmware_gnss.{h,cpp}` owns:

- the power/acquisition/fix state machine;
- freshness and publication into the existing effective coordinates;
- the `gps_enabled` live setting;
- MicroNMEA adapter consumption;
- the typed diagnostic snapshot;
- the sleep-inhibit policy.

The existing OLED send adapter owns the simple Heltec default. It reads the published effective-location fact and,
for the DM arm, the live `e2e_dm` fact, then conditionally includes `-l` in the command line; the present emergency
path already demonstrates that composition seam. It must not intercept or rewrite a line received from USB or BLE.
Thus all producers still converge on the one command/core path, but a companion's deliberate omission of `-l`
survives intact.

`variants/heltec_v4/board_gnss.{h,cpp}` owns only:

- GPIO34/38/39/40/41/42 electrical operations;
- `Serial1` begin/end/read/available;
- non-blocking power/reset deadlines;
- any later board wake primitive.

Core location policy and exact message fitting stay under `lib/core`; board code never constructs a `Command`,
`TxItem`, frame or JSON object. Console formatting stays in the existing command/JSON modules.

### 5.3 One effective coordinate path

The GNSS module publishes through one helper that updates together:

- `g_lat_e7/g_lon_e7`;
- `g_node.mutable_config().lat_e7/lon_e7`;
- `g_node.mutable_config().location_available`.

The clear path uses the same helper with unavailable/zero values. Do not write the availability fact or either pair
at independent call sites. The helper performs no persistence. Existing OLED fix projection reads this explicit fact
rather than re-deriving GNSS validity from `(0,0)`.

### 5.4 Feature/profile wiring

`heltec_v4_mobile` adds:

- `MR_FEAT_GNSS=1`;
- `firmware_gnss.cpp` and the V4 board adapter to its source filter;
- exact MicroNMEA 2.0.6 dependency;
- compile-time pin/polarity checks or one V4 board-traits authority.

`heltec_v4`, `gateway_heltec_v4`, every V3/XIAO profile, native and the simulator do not link Arduino GNSS code.
Pure parser/policy units remain native-testable through headers or platform-neutral sources.

---

## 6. GNSS state machine

### 6.1 States

The typed diagnostic state is:

```text
unsupported -> off -> powering -> acquiring -> fix
                                      |          |
                                      +-> no_data+-> stale
```

- `unsupported`: feature absent or current role cannot use GNSS;
- `off`: supported mobile profile, user setting OFF, rail disabled;
- `powering`: rail enabled, non-blocking startup interval not complete;
- `acquiring`: checksum-valid NMEA has been observed, no valid fresh position yet;
- `no_data`: ON but no checksum-valid sentence has arrived within a bounded diagnostic interval;
- `fix`: effective position available and younger than 15 minutes;
- `stale`: a prior position crossed 15 minutes and has been removed from the effective location.

`no_data` is not a permanent detection verdict. The driver keeps accepting input and can transition to acquiring/fix
if a module is attached late or begins talking after a cold start. Missing/broken GNSS never blocks boot, radio RX,
BLE, console or LoRa sends without location.

### 6.2 Service behavior

Initialization and every transition are non-blocking. A service pass:

1. completes any elapsed power/reset deadline;
2. drains at most a bounded number of UART bytes;
3. feeds those bytes to the one parser;
4. accepts at most complete checksum-valid fix sentences;
5. expires the effective location before command processing can use it;
6. updates diagnostic ages/counters.

No `delay`, blocking `read`, wait-for-fix loop or one-time presence probe is allowed. If more UART data remains, the
next main-loop pass continues immediately.

### 6.3 Parser evidence

Native fixtures must include real L76K-shaped sentences covering:

- valid `GNRMC` and `GNGGA` fixes in all latitude/longitude hemispheres;
- no-fix RMC/GGA;
- bad checksum;
- truncated and overlong sentence recovery;
- noise before `$` and recovery at the next sentence;
- degree-to-E7 boundaries and range rejection;
- repeated valid fixes updating age;
- invalid sentences not refreshing or erasing the retained fresh fix.

Tests assert the adapter result, not MicroNMEA internals.

### 6.4 Sleep rule for the first slice

Under settled D2, `mr_gnss_allows_sleep()` is false for the whole interval in which GNSS is enabled on a mobile. It is
true in every other state. The existing sleep gate becomes the conjunction of UI permission and GNSS permission;
neither module knows the other exists.

Do not add UART wake, PPS wake or a second call to `esp_light_sleep_start()` in this slice. A future optimization must
prove complete sentence recovery, coexistence with DIO1 and button wake, fix freshness under movement, truthful wake
counters, and no interrupt storm before it may relax the gate.

---

## 7. Configuration, persistence and diagnostics

### 7.1 Durable setting

Append one `gps_enabled` byte to `/mrcfg`'s `mrnv::Blob` and bump its NV version once from the value current when the
slice lands. Do not reuse an unrelated pad or the removed `loc_in_dm` concept: this bit controls physical receiver
power, not “attach this coordinate regardless of confidentiality.”

The seed/default follows D2. `cfg set gps 0|1` uses the existing load/stamp/save ritual, applies live only after the
write succeeds, and leaves both persisted and live state unchanged on an NV failure. Its handler returns a typed
result instead of making BLE infer success from a dump: USB renders the result as text; BLE returns the fresh `cfg`
object on success and a structured `bad_value`, `unsupported`, `managed_by_gnss`, or `nv_save_failed` error otherwise.
The setting is not added to the one-button OLED `ConfigService` in this slice.

No coordinate or timestamp is added to any durable record. A fix must cause zero flash writes.

### 7.2 Text diagnostics

USB `status` includes one concise line, for example:

```text
  gps       = off
  gps       = acquiring nmea_age=1s
  gps       = fix age=4s sats=9
  gps       = stale age=901s
  gps       = no_data
```

Satellite count is diagnostic only and may be omitted when the current accepted sentence does not carry it. Do not
print a stale coordinate as though it were usable. `cfg` reports the persisted/effective `gps_enabled` setting.

### 7.3 BLE/JSON diagnostics

The structured status surface gains additive fields sufficient to express the same facts:

```json
{"gps_supported":true,"gps_enabled":true,"gps_state":"fix","gps_fix_age_s":4,"gps_sats":9}
```

Omit optional age/satellite fields when unknown; never encode unknown as zero-age or zero satellites if that would
claim a measurement. The common `status` and `cfg` schemas always emit `gps_supported` and `gps_enabled`; unsupported
profiles emit `false` and status emits `gps_state:"unsupported"`. This lets a companion distinguish old firmware,
unsupported hardware and an OFF supported receiver without board-name heuristics. Exact JSON is pinned natively.

### 7.4 Configuration refusal surfaces

The typed `cfg set` result must preserve one token across its transport renderers. In particular, the two new
refusals are not folded into `bad_value`, and BLE must not answer a failed write with a fresh `cfg` object that looks
like success:

| condition | typed token | USB text | BLE/JSON command result | asynchronous send failure |
|---|---|---|---|---|
| `cfg set gps` on an unsupported build/role | `unsupported` | `> cfg err unsupported` | `{"err":"cfg","msg":"unsupported"}` | not applicable; no send started |
| `cfg set lat/lon` while V4-mobile GNSS owns location | `managed_by_gnss` | `> cfg err managed_by_gnss` | `{"err":"cfg","msg":"managed_by_gnss"}` | not applicable; no send started |

The existing `bad_value` and `nv_save_failed` outcomes use the same USB token-first and BLE
`{"err":"cfg","msg":"<token>"}` shape. Success alone returns the fresh `cfg` object over BLE. One typed result and
one token mapper feed both transports; USB text must not be scraped to decide the JSON result.

### 7.5 Send-size failures

Append, without renumbering existing values:

- `CmdCode::err_location_too_large`;
- `SendFailReason::location_too_large`.

The result means exactly: the same message and carrier fit without location, `-l` was present for this attempt
(including when the simple Heltec producer added it), and adding the location made it not fit. It must not be used
for a message which fails even without location.

Required renderings:

- USB: `message plus location too large - shorten the message`;
- JSON command result: `err_location_too_large`;
- JSON send failure: `location_too_large`.

No frame is queued, no counter is burned where the current path can preflight before minting, and no partial/clear
copy is emitted.

---

## 8. Core location-request design

### 8.1 Central policy

Add one core helper that accepts:

- app/internal classification;
- the explicit per-message location-request bit;
- actual sealed/plain carrier decision;
- the immutable location snapshot;
- the carrier's exact no-location and located fit results.

It returns a typed decision: omit, attach, `no_location`, `unsealable`, `too_large`, or
`location_too_large`. Direct DM, SEALED_RELAY and channel origination use this helper rather than restating the matrix.

`NodeConfig::location_available` answers only whether a location can be supplied; it is never permission to attach
one. Configured static coordinates and a fresh V4-mobile GNSS fix can both satisfy a requested `-l`. A missing `-l`
returns `omit` before location availability can affect the carrier. GPS OFF and fix expiry clear availability, so a
present `-l` receives the existing strict `no_location` result.

### 8.2 Ordering

For every carrier, the order is:

1. retain the producer's presence or absence of `-l` through any parked/deferred state;
2. resolve whether this attempt will actually be sealed;
3. prove the body fits without location;
4. when `-l` is absent, construct the no-location carrier without consulting location state;
5. when `-l` is present, snapshot location once and apply the strict availability/sealing checks;
6. prove the located body fits;
7. construct the sealed carrier from that same snapshot;
8. only then queue/air through the existing machinery.

Checking configured encryption before key/carrier resolution is forbidden. Packing with location, seeing a generic
failure, and retrying without it is also forbidden: that is the silent omission this design exists to prevent.

### 8.3 Direct and channel size identities

The existing 6-byte `pack_loc6` representation is unchanged.

For a normal direct sealed DM in the current 255-byte frame, location reduces the exact body cap by 6 bytes: from
214 to 208 bytes. Do not continue relying on the generic 239-byte command cap; direct seal preflight must use the
actual carrier formula. *(Landed 2026-08-28 by §B20/B21: the formula authority is `data_inner_cap()` / `data_frame_len()` in `lib/core/frame_codec.h` — the packer's own arithmetic, now enforced at the seal preflight; GPS-2 consumes it rather than restating a cap.)*

For a sealed team channel, the existing inner helper remains the authority. A located post adds 10 bytes relative
to a text-only post: 4-byte authenticated source hash plus 6-byte location. Current exact text caps are 173 bytes
without location and 163 with it.

Tests must pin both sides of every boundary and prove the distinction:

- no-location cap succeeds;
- located cap succeeds;
- first byte above located cap is `location_too_large` when the no-location form still fits;
- first byte above no-location cap is ordinary `too_large`.

### 8.4 SEALED_RELAY inner extension — settled D1

Keep the visible SEALED_RELAY body envelope unchanged:

```text
[seal_ctr 2 LE][seed8 8][ciphertext || tag16]
```

Change the authenticated decrypted plaintext from:

```text
[origin=0 1][source_hash 4][body]
```

to:

```text
[relay_flags 1][source_hash 4][loc6 if relay_flags & DATA_FLAG_LOCATION][body]
```

This does not append a byte. It changes the meaning of the existing first sealed byte only for
`DATA_TYPE_SEALED_RELAY`. The builder currently always supplies `origin=0` and `e2e_open_relay` deliberately ignores
the recovered origin, so this byte is already authenticated and semantically free in this carrier. Reuse the
existing `DATA_FLAG_LOCATION` value (`0x08`) as its only allowed bit; a receiver rejects every other set bit. Zero is
the ordinary unlocated form and remains byte-identical.

Reuse `e2e_seal_inner` rather than fork crypto:

- on seal, pass `relay_flags` through the helper's existing `origin` argument and include its existing
  `DATA_FLAG_LOCATION` input flag only for a located relay; the helper then writes the one existing `pack_loc6`
  representation after source hash;
- on open, call `e2e_open_inner` with SOURCE_HASH only, so it returns the first sealed byte separately and leaves an
  optional location at the beginning of the recovered body;
- validate the recovered byte as `relay_flags`; when LOCATION is set, require and unpack the first six body bytes,
  strip them, and return the remaining message plus `has_location/lat/lon` to the common receive path.

An unlocated SEALED_RELAY has zero size change. A located relay grows by exactly six encrypted bytes. Exact fit
depends on the cross-layer path length and delegated wrapper, so use the existing packers as the one length authority
rather than a single hand-written maximum.

An old reader would ignore the nonzero former-origin byte and deliver a located relay's six location bytes as a
message prefix. The owner explicitly accepts that development-build incompatibility and rules **no `wire_version`
bump**. Update `docs/frames.md` and `docs/protocol.md` in GPS-1, reflash the test fleet consistently, and keep the
layout change isolated so its behavior remains attributable.

No new DATA type, outer flag bit, nonce rule, clear sender field, routing behavior or encryption algorithm is added.

### 8.5 Receive behavior

All three encrypted carriers feed the existing receive facts:

- `Push::has_location/lat_e7/lon_e7`;
- authenticated peer-location telemetry;
- RAM-only `peer_loc_set` retention;
- existing OLED/team distance consumers.

A SEALED_RELAY location is authenticated exactly like its body and source hash. A malformed/unknown inner flag or
short location is a hard drop, never a fallback to delivering raw bytes as text.

---

## 9. Tests and mutation controls

### 9.1 Native GNSS policy/parser tests

Required cases include:

- ON/OFF power-state transitions and idempotence;
- OFF clears effective coordinates immediately;
- static role cannot power GNSS;
- late module/NMEA recovery after `no_data`;
- fix valid at age `899999 ms` and unavailable at `900000 ms`;
- invalid/no-fix NMEA does not refresh age;
- no durable write on any fix/update/expiry;
- a failed persisted ON/OFF write does not change live power;
- unsupported `cfg set gps` and GNSS-owned `cfg set lat/lon` produce their exact USB and BLE/JSON refusal tokens,
  never a success-shaped fresh `cfg` response;
- non-GNSS stub stays inert and permits sleep;
- GNSS ON denies sleep under the D2 first-slice policy, OFF restores it.

### 9.2 Core carrier matrix

Drive ordinary and boundary-size messages through public commands for:

- direct plaintext without `-l`, with a location available: no location on wire;
- direct sealed with `-l` and a location available: location present and decodes within `pack_loc6` tolerance;
- direct sealed without `-l`, with a location available: sends without location;
- explicit `-l` without location: `no_location`;
- each of the three enumerated Heltec producers is pinned independently: emergency and canned team channel add `-l`
  with a fresh fix; canned DM adds it only with both a fresh fix and live `e2e_dm`; GPS OFF/no-fix omits it;
- K7 grant and every other internal/provisioning producer remain location-free with GPS ON and a fresh fix;
- otherwise-identical serial/BLE commands with and without `-l` preserve that exact choice;
- team keyless/plaintext, team sealed, global and mixed scope, both with and without `-l` where meaningful;
- same-layer, delegated and cross-layer SEALED_RELAY with `-l`;
- SEALED_RELAY KATs proving an unlocated relay remains byte-identical and a located relay reuses the former-origin
  byte as `DATA_FLAG_LOCATION`, adding exactly one `pack_loc6` and no other byte;
- internal encrypted/control carriers: never location-bearing;
- async parked/deferred `-l` send where location expires before carrier construction: refuse rather than strip it;
- switching GPS OFF after a carrier is already sealed: queued bytes remain unchanged, later Heltec-default carriers
  omit location and later explicit `-l` commands refuse `no_location`.

### 9.3 Required mutation controls

At minimum, automated controls must go RED for:

1. attaching location based on `e2e_dm`, `team_channel_crypt` or `-e` rather than the actual emitted carrier;
2. attaching location when `-l` is absent;
3. adding `-l` to a serial/BLE command that omitted it;
4. allowing any plaintext/global copy to carry location;
5. changing `< 900000` to `<= 900000`;
6. refreshing `last_fix_ms` on an invalid/no-fix sentence;
7. omitting the immediate clear on GPS OFF;
8. changing `location_too_large` back to ordinary `too_large`;
9. silently retrying a failed located pack without location;
10. dropping SEALED_RELAY's authenticated LOCATION bit or parsing its loc bytes as text;
11. accepting unknown SEALED_RELAY inner flag bits;
12. using a new relay bit or appending a separate relay-flags byte instead of reusing `DATA_FLAG_LOCATION` in the
    existing former-origin byte;
13. mapping `unsupported` or `managed_by_gnss` to a generic/success-shaped configuration response on either transport;
14. adding `-l` to the K7 grant/internal path, or failing to add it to any one enumerated eligible OLED producer;
15. permitting light sleep while GPS ON under the approved first-slice policy.

### 9.4 Durable wiring evidence

Add a focused source/host probe that pins the only production bridge:

- `mr_gnss_tick` executes before command dispatch, before `mr_ui_tick` can call `build_snapshot`, and before the
  sleep decision;
- fix expiry therefore happens before a same-pass send can snapshot location;
- the same ordering prevents STATUS from rendering a just-expired coordinate for one extra UI frame;
- the sleep gate consults both `mr_ui_allows_sleep()` and `mr_gnss_allows_sleep()`;
- status consumes the typed GNSS snapshot, not a re-derived coordinate guess;
- `heltec_v4_mobile` alone links the real board adapter and parser dependency.

Controls must fail for deletion, ordering after command processing, ordering after the UI snapshot, bypassing the
sleep gate and compiling the real adapter into a static/gateway profile.

---

## 10. Gate and metal acceptance

### 10.1 Automated gate per implementation slice

Read the current anchors from `simulation/BASELINE.md` at execution time. The completion gate is:

- native wrapper plus the real native test binary;
- exact s18 keystone MD5/event/failure tuple;
- warning census with zero new warning and zero `-Wswitch`;
- exactly two essential board environments, sequentially: `heltec_v4_mobile` (the real GNSS feature and Xtensa/team
  arm) and `gateway` (the non-GNSS stub plus ARM/team-off arm); do not pre-authorize a third board build;
- existing board/UI probes plus the GNSS wiring/parser probe;
- `git diff --check`;
- `sizeof(Node)` and `sizeof(NodeConfig)` pinned; GPS-2 records measured old→new arithmetic and the RAM delta on both
  named board environments.

The simulator has no Device GNSS source and must not synthesize fixes or aired-location events. The expected s18
result is exact identity; if any stream moves, investigate rather than blessing a GPS explanation.

### 10.2 V4.2 and V4.3 metal checklist

Run on both physical revisions with the same L76K module/cable where practical:

**Expected transient during sequential reflashing:** if a newly flashed sender emits a located SEALED_RELAY before
its peer has been reflashed, the old peer may display the six packed location bytes as a message prefix. This is the
accepted no-wire-version-bump skew mode, not a new codec defect. Record it if observed, then reflash the peer; GPS-4
qualification starts only after both ends run the new format and must then show clean text plus decoded location.

1. Boot GPS OFF: rail is off, UART pins do not back-power the module, radio/OLED/button behavior is unchanged and
   `slept=` continues increasing.
2. Enable through USB, then through BLE: save succeeds, rail enables active LOW, no boot-loop/blocking delay occurs.
3. Outdoors with clear sky, capture raw diagnostic sentence/counter evidence and acquire a fix; verify E7 coordinate
   against an independent receiver.
4. Reboot with the approved persisted setting; confirm the setting restores but no old fix does.
5. Remove the GNSS module while ON: status becomes no-data/stale as appropriate, LoRa continues, no panic occurs;
   reconnect and prove recovery without reflashing.
6. At exactly the 15-minute boundary after the last valid fix, confirm the effective coordinate disappears and the
   next simple Heltec-default encrypted message carries no location; an explicit `-l` command reports no-location.
7. Send plaintext DM/channel traffic with a fresh fix and inspect decoded frames: zero location.
8. Send same-layer encrypted DM, SEALED_RELAY encrypted DM and team-key-sealed team channel with `-l`: each carries
   the same fresh position within codec tolerance. Repeat without `-l`: none carries a position despite GPS ON.
9. Exercise both size outcomes: ordinary `too_large` and `location_too_large`; confirm no frame airs for either.
10. Disable GPS: rail turns off, effective location clears immediately, later simple Heltec-default messages omit
    it, and explicit `-l` reports no-location.
11. Under D2's first-slice policy, confirm `slept=` stops increasing only while GPS is ON and resumes after OFF; record
    current draw in both states so the cost is visible.
12. During continuous GNSS parsing, receive LoRa and complete RTS/CTS/DATA exchanges; no missed radio IRQ, watchdog,
    long loop stall or OLED page regression.
13. Preserve `firmware.elf`, version banner, board revision, module revision and the serial transcript.

The bench script receives only the parts no automated gate can reach: rail polarity/current, physical UART, outdoor
fix, both-board coexistence and measured sleep/current behavior.

---

## 11. Implementation slices

Each slice is independently reviewable. Do not combine the wire-format change, core request policy and device driver
in one diff.

| slice | kind | content | completion |
|---|---|---|---|
| GPS-0 | design | owner review of this specification; D1/D2 settled | specification dispatchable |
| GPS-1 | wire/core | settled D1: reuse sealed relay origin as authenticated `DATA_FLAG_LOCATION`, receive outputs, frames/protocol docs; explicitly no wire-version bump | relay native/dual-layer cases green; unlocated relay byte-identical; no GNSS or Heltec default policy |
| GPS-2 | core feature/layout | `NodeConfig::location_available`; central omit/required request policy; exact preflight; appended error enums/renderers; direct/channel/relay matrix | native + s18 + two named boards green; deliberate old→new `sizeof(NodeConfig)`/`sizeof(Node)` arithmetic and both RAM deltas recorded; configured test/static coordinates only |
| GPS-3 | device feature | `gps_enabled` NV field/version, parser adapter, firmware/board GNSS modules, V4-mobile profile dependency, simple Heltec `-l` default, diagnostics and sleep inhibit | automated gate green; no low-power wake optimization |
| GPS-4 | metal acceptance | V4.2/V4.3 bench script evidence, expected flash-skew observation, and documentation corrections | both peers on the new format; physical checklist recorded; feature ready |
| GPS-5 | later optimization | UART/PPS/Standby power work, only after a separate design and metal proof | GPS ON can sleep without sentence/fix/radio regressions |

GPS-1 isolates the relay-layout attribution but, by explicit owner ruling, does not bump `wire_version`. GPS-3 still
carries the unrelated and required NV-version change; persistent layout must migrate correctly on a reflash without
a full erase.

---

## 12. Completion invariants

The feature is complete only when all of these statements are true:

1. GPS OFF is a physical power-off and disables the simple Heltec mobile's default location sharing.
2. GPS ON never claims a fix until a valid sentence establishes one.
3. A 15-minute-old mobile fix can never be attached.
4. No fix update or expiry writes flash.
5. No plaintext or global carrier contains a position.
6. Every supported encrypted app-DM carrier, including SEALED_RELAY, honors the same explicit `-l` request.
7. A sealed team-only channel honors the same request; a keyless/global/mixed post refuses it.
8. Omitting `-l` always omits location, including on GPS-ON mobile and manually located static nodes.
9. The three enumerated Heltec app producers apply their ruled defaults, while K7/internal producers remain excluded
   and serial/BLE commands are never rewritten.
10. Internal/control traffic is never location-bearing.
11. A requested location is never silently removed to make a message fit.
12. `location_too_large` is distinct and visible over USB and BLE/JSON.
13. V3, static/gateway V4 and XIAO behavior remain inert with respect to GNSS hardware.
14. Radio service and UI remain responsive while GNSS is acquiring and tracking.
15. The first implementation makes its GPS-ON power cost explicit; no unproved wake mechanism is presented as
    reliable.
16. `unsupported` and `managed_by_gnss` remain distinct, typed and truthful on both USB and BLE/JSON.
17. GPS-2 records the measured `NodeConfig`/`Node` layout arithmetic and the RAM delta on exactly the two named board
    environments.
