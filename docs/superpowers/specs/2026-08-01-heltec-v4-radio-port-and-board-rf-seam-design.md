<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Heltec V4.2/V4.3 radio port and board-RF seam — design specification

First written 2026-08-01. Revision 3 refreshed 2026-08-25 against MeshRoute
e9008794d0ebf4910872b518564f33dee647bb86, the current pinned PlatformIO framework, Heltec's current V4
documentation, and MeshCore main at 0679dbeffc504d562d2f09eb072fdc223f8ffc2a.

**Status: OWNER-APPROVED AND DISPATCHABLE (2026-08-25).**

Revision 3 replaces Revision 2 in place. It keeps the approved generic board-RF seam and conducted-output contract,
but corrects the source drift and closes omissions found by the 2026-08-25 audit:

- the target is exactly the original high-power Heltec V4.2 and V4.3, one runtime-detected image;
- Heltec V4 R8, low-power/no-FEM boards, TFT, GNSS and deep sleep are excluded;
- the current TX path has split completion collection and pumping; that ordering must remain untouched;
- initial RX must reuse the one RX-arm authority instead of creating a third FEM placement;
- a one-bit default-level read cannot prove that a FEM exists, so ambiguous/no-FEM detection must fail closed;
- V4 uses native USB CDC, not V3's CP2102/UART0 path;
- Heltec documents the V4 panel as SSD1315, although the SSD1306-compatible page driver is the proven reference;
- the V3 board canvas is already almost entirely reusable and must not be copied into a second 488-line board port;
- the project needs its own board JSON and Arduino variant because the pinned platform has neither;
- all three V4 profiles, the warning census, the real-board UI probe and source provenance were absent from Revision 2;
- a safe fixed-output first image can be built before a full RF calibration table exists.

The purpose is practical: the approved slices in section 11 lead to a buildable and bench-safe heltec_v4 image
without waiting for the later full power-calibration slice.

---

## 0. Authority and decisions

### 0.1 Carried owner rulings

These decisions were approved on 2026-08-01 and remain authoritative:

1. The V4 port introduces one reusable board-RF front-end seam, not a Heltec-only branch threaded through the MAC.
2. The user-facing tx_power value means desired conducted dBm at the antenna connector.
3. EIRP/ERP policy is outside this seam. Antenna gain and installation loss are not board properties.
4. Existing boards retain identity power mapping: requested conducted dBm equals SX1262 chip drive.
5. Front-end initialization or mode failure is visible and refuses transmission. It never silently bypasses the FEM.
6. Nothing in lib/core, the wire format, Node layout, timers or simulator behavior changes for this port.

### 0.2 Owner rulings approved 2026-08-25

The owner approved all three first-build decisions on 2026-08-25. No policy decision remains before implementation.

**D1 — board population confirmation, not a policy choice.**

**Owner ruling:** both boards are the **high-power / 28 dBm HTIT-WB32LAF population**. A frequency label alone would
not have been sufficient: Heltec's V4.2 datasheet also lists a low-power HTIT-WB32LAF-N-HF population covering the
same 863–928 MHz range. The implementation additionally proves an external FEM bias at boot and fails closed if none
is present. This specification does not support any -N low-power/no-FEM population or any 433–510 MHz population.
The high-power product supports both the 868 and 915 MHz ranges, and MeshRoute's present 869.4625 MHz plan is inside
it.

**D2 — V4.3 receive LNA default.**

**Owner ruling: LNA enabled by default.** MeshRoute is a reachability-oriented hiking/safety system and should prefer
receive sensitivity. V4.3 can instead bypass its LNA for lower current and potentially better strong-signal behavior.
Heltec's current product page says MeshCore 1.15 enables it, while the pinned current MeshCore source initializes
lna_enabled=false; neither conflicting external default should silently choose MeshRoute's policy.
The first implementation exposes the selected state in status but does not add a new command or NV field. A runtime
setting is a later, separately designed feature.

**D3 — first-image transmit power.**

**Owner ruling:** support exactly **22 dBm nominal conducted output**, mapped to SX1262 chip drive 10 dBm, and refuse
every other requested output. This is the conservative published reference point and permits a safe first bench
image today. Do not expose 28 dBm until each board revision has a measured table. The status and boot banner must say
that 22 dBm is nominal/reference-derived until measured locally; it must not claim regulatory compliance.

No owner choice remains for board assets, native testing, profile names or the common UI boundary; those are resolved
below from the present tree.

---

## 1. Exact target and exclusions

One build target, BOARD_HELTEC_V4, supports:

| physical board | detected FEM | runtime label |
|---|---|---|
| Heltec WiFi LoRa 32 V4.2 high-power | GC1109 | gc1109 |
| Heltec WiFi LoRa 32 V4.3 high-power | KCT8103L | kct8103l |

One binary performs the detection at each boot. The compile-time board name remains heltec_v4; boot and status add
the detected FEM. This avoids pretending that two separately maintained images are necessary.

Explicitly excluded from this design:

- Heltec V4 R8: different ESP32-S3R8/PSRAM and peripheral pinout;
- low-power or no-FEM V4 boards, including the high-frequency HTIT-WB32LAF-N-HF population;
- 433–510 MHz operation;
- TFT expansion displays;
- GNSS input or location propagation;
- deep sleep and FEM power-off;
- a V4.3 LNA console/BLE command or persisted setting;
- 28 dBm operation and a general multi-point power table;
- changes to app pushes, UI screens, protocol behavior, wire format, NV layout or simulator outcomes.

If an excluded or electrically ambiguous board reaches this image, detection must refuse the RF path rather than
guessing a supported FEM.

---

## 2. Present source state, verified 2026-08-25

### 2.1 Radio lifecycle

Sx1262Radio in lib/hal/device_radio.h owns the real RadioLib calls:

| authority | current behavior |
|---|---|
| begin | registers DIO1 and directly calls startReceive once |
| start_transmit | standby, applies per-frame PHY and power, then calls startTransmit |
| arm_rx | the other eight startReceive paths funnel through this helper |
| poll_tx_done | finishTransmit, restore listening SF, arm_rx |
| abort_tx | standby, restore listening SF, arm_rx |
| set_rx_sf/freq/bw/cr and poll_rx | each ends in arm_rx |

There are exactly two underlying RadioLib call expressions today: one startTransmit and two textual startReceive
expressions, the second being begin's bypass of arm_rx. Revision 3 removes that bypass: begin initializes the FEM and
then calls the same arm_rx helper used everywhere else. The durable invariant becomes:

- exactly one production startReceive expression, inside arm_rx;
- exactly one production startTransmit expression, immediately after a successful TX-mode transition.

This is stronger and simpler than Revision 2's three-placement rule.

### 2.2 TX-completion ordering

DeviceHal no longer has Revision 2's combined service_tx. It now has:

1. collect_tx_completion before the Node timer drain;
2. exhaustive outcome-ring delivery to Node::on_tx_complete;
3. timer drain;
4. pump_tx after timers.

That order is pinned by W21 and is unrelated to FEM switching. The V4 change occurs below IRadio, inside
Sx1262Radio. It must not merge, move, wrap or otherwise disturb collect_tx_completion, the exhaustive outcome drain,
or pump_tx.

A failed FEM transition or unsupported output returns TxResult::radio_error from start_transmit. The existing
DeviceHal path already counts the failed arm, creates a failed TxOutcome, and delivers it through the same ring.
No new retry policy belongs here.

### 2.3 Boot truth

fw_main currently:

1. runs CustomSX1262::std_init;
2. prints radio OK/INIT FAILED and assigns g_radio_ok from that result alone;
3. loads the operating configuration;
4. applies frequency/SF/BW/CR/sync word;
5. calls g_iradio.begin but discards its result.

Therefore g_radio_ok can claim success when initial RX or the future FEM initialization failed. The V4 slice must
make the result truthful without overloading one flag with two different facts:

- g_radio_ok is hardware readiness: chip initialization, FEM initialization and initial RX arm all succeeded;
- RF-configuration validity is separate: the requested frequency and output are both supported;
- the user-facing rfok value is their conjunction.

This separation is load-bearing. apply_radio_live currently uses g_radio_ok to decide whether a corrective retune
may touch the radio. Marking hardware failed merely because a persisted setting is unsupported would prevent the
operator from repairing that setting live. Boot/status prints hardware, configuration and their combined result
after all three have been evaluated; a valid cfg set recomputes configuration validity without requiring a reboot.

CustomSX1262::std_init does not put a packet on air. It is safe to initialize the board FEM immediately after it and
before the first startReceive. No V4 target may call startTransmit until the FEM is ready.

### 2.4 Power state

The common build flag LORA_TX_POWER is currently both:

- direct SX1262 chip drive consumed by the vendored CustomSX1262::std_init; and
- the default/persisted user value seeded into g_tx_power, the NV fallback and leave.

Those meanings coincide on existing boards and diverge on V4. The console currently accepts -9 through 22 for every
board. The split in section 7 is therefore mandatory, not cleanup.

### 2.5 UI and sleep

The feature UI in src/firmware_ui.cpp and firmware_ui_model.h is board-independent. The V3 canvas in
variants/heltec_v3/board_ui.cpp contains the U8g2 page adapter, panel power, battery ADC, button polling and
light-sleep button wake. Only its hardware traits differ on V4.

MeshRoute uses light sleep while the SX1262 remains in continuous RX. ESP-IDF preserves digital peripheral state
across light sleep, so the FEM stays in RX mode. The V4 port must not copy MeshCore's deep-sleep GPIO-hold sequence
into board_sleep_until. Deep sleep is absent here, and powering down the FEM would make DIO1 radio wake impossible.

### 2.6 Platform and profiles

The pinned pioarduino platform provides heltec_wifi_lora_32_V3 but no original-V4 board definition. Its builder does
support a project-local build.variants_dir. The repository must therefore carry the V4 assets.

V3 currently has three profiles:

- heltec_v3;
- heltec_mobile;
- gateway_heltec.

V4 mirrors them as:

- heltec_v4;
- heltec_v4_mobile;
- gateway_heltec_v4.

The first flashable milestone is heltec_v4. The two derived profiles add no board behavior but must exist before the
variant is called complete.

---

## 3. Hardware facts and board traits

The following is the target table for the original V4.2/V4.3 family:

| function | V4.2 | V4.3 | shared implementation |
|---|---:|---:|---|
| MCU / memory | ESP32-S3R2, 16 MB flash, 2 MB QSPI PSRAM | same | board JSON |
| LoRa NSS / DIO1 / RESET / BUSY | 8 / 14 / 12 / 13 | same | build flags |
| LoRa SCLK / MISO / MOSI | 9 / 11 / 10 | same | build flags |
| FEM LDO | GPIO7, HIGH | same | V4 RF driver |
| FEM discriminator/control | GPIO2 external pull-down | GPIO2 external pull-up | V4 RF driver |
| second FEM control | GPIO46 | GPIO5 | V4 RF driver |
| button | GPIO0, active LOW | same | common Heltec canvas |
| OLED | SSD1315-compatible 128x64 at 0x3C | same | common page canvas |
| OLED SDA / SCL / RESET | 17 / 18 / 21 | same | traits |
| Vext | GPIO36, active HIGH | same | traits |
| battery ADC / divider control | GPIO1 / GPIO37, control active HIGH | same | traits |
| battery multiplier | 5.42 reference value | same | traits |
| USB console | native ESP32-S3 CDC/JTAG | same | board JSON and existing console |

Heltec names the controller SSD1315; the current MeshCore OLED port and the compatible U8g2 command path use an
SSD1306-class driver. Compatibility is an on-metal acceptance item, not a reason to duplicate the renderer.

There is a documented-source conflict on Vext polarity: current pinned MeshCore declares GPIO36 active HIGH, while
the V4.2 Heltec datasheet prose says VextCtrl must be LOW to use VE. The first build follows the current MeshCore
reference (HIGH), but the trait is provisional until panel ACK and current are measured on both boards. If V4.2
and V4.3 differ, derive the runtime level from the already detected FEM kind; do not choose one revision silently.

GPIO0 remains a boot strap. Holding the UI button through reset can enter the ROM downloader, exactly as on V3.

---

## 4. Source boundary: keep the board-specific part small

### 4.1 Generic RF seam

Add lib/hal/iboard_rf.h containing:

- BoardRfKind: none, gc1109, kct8103l, unknown;
- BoardRfDrive: valid plus chip_dbm;
- IBoardRf with begin, tx_mode, rx_mode, kind, LNA-state diagnostics, frequency_supported and drive_for_output.

The three mode methods report success. A failure never proceeds into the corresponding RadioLib operation.
No deep-sleep or power_off method is added: there is no caller and speculative API is not a seam.

Add a tiny provider header following mr_ui's optional-capability pattern:

- V4 builds declare a board_rf_instance provider and compile its implementation;
- all other builds inline to a null pointer;
- fw_main constructs Sx1262Radio with that pointer and contains no Heltec pin logic.

Sx1262Radio owns the sequencing because it owns every RadioLib transition. The front end is composed with it; it is
not a subclass and is not added to lib/core or Hal.

### 4.2 V4-specific RF implementation

Only variants/heltec_v4/board_rf.cpp knows:

- GPIO7, GPIO2, GPIO46 and GPIO5;
- the GC1109 and KCT8103L mode truth tables;
- the V4.3 LNA default;
- the V4 frequency envelope and first-build power mapping;
- revision labels used by diagnostics.

Do not copy MeshCore's class into MeshRoute. Use it as the behavioral reference and write the bounded implementation
against MeshRoute's interface, with project authorship and tests.

### 4.3 Shared Heltec OLED canvas

Do not create variants/heltec_v4/board_ui.cpp by copying the 488-line V3 file.

First make a behavior-preserving refactor:

- move the common board_ui.h/cpp implementation to variants/heltec_common;
- replace hard-coded OLED, Vext and ADC behavior with required compile-time traits;
- keep the current V3 trait values byte-for-byte equivalent;
- retain one display-independent canvas API for firmware_ui.cpp.

Required traits include OLED pins/address, Vext pin/active level, button pin, ADC input/control, ADC polarity strategy
and multiplier. V3 retains its existing runtime ADC-control polarity probe. V4 uses the documented fixed active-HIGH
control. Missing traits are compile errors, never defaults.

The real-board UI probe must compile the same common source twice and prove both trait sets:

- V3: present pin values, Vext LOW, ADC polarity probing;
- V4: OLED 17/18/21, Vext HIGH, ADC control LOW at rest and HIGH only while sampling;
- both: one-page-per-service paint, panel ACK check, blanking, button polling, wake arm/disarm and no frame-policy
  leakage into the board adapter.

All screens, strings, gestures and UI state remain unchanged.

### 4.4 Board assets

Vendor the exact content of these assets from MeshCore commit
0679dbeffc504d562d2f09eb072fdc223f8ffc2a:

- boards/heltec_v4.json;
- upstream variants/heltec_v4/pins_arduino.h into
  arduino_variants/heltec_v4/pins_arduino.h in this repository.

The JSON specifies ESP32-S3R2, 16 MB flash, 2 MB QSPI PSRAM, default_16MB.csv, USB CDC on boot and USB mode 1.
Set board_build.variants_dir = arduino_variants in the V4 PlatformIO environment so a clean checkout resolves the
vendored pins_arduino.h.

This directory split is deliberate. The pinned ESP32 builder compiles every source file under the selected Arduino
variant as FrameworkArduinoVariant. MeshRoute's variants/heltec_v4 directory contains application-owned board_rf.cpp
and must remain outside that implicit framework build; it is compiled exactly once by build_src_filter. Do not place
either board_rf.cpp or the common canvas under arduino_variants.

Extend tools/vendor_meshcore.sh and lib/meshcore/NOTICE so future re-syncs include these two assets and retain exact
provenance. Do not require a sibling /home/staszek/MeshCore checkout at build time.

---

## 5. FEM detection and mode truth

### 5.1 Detection

MeshCore's current reference reads GPIO2 once with no MCU pull:

- LOW means GC1109/V4.2;
- HIGH means KCT8103L/V4.3.

That is sufficient to choose between two known populated boards, but it cannot distinguish a supported external pull
from a floating/no-FEM GPIO. Revision 3 strengthens the first-build detector:

1. power the FEM LDO on GPIO7 and wait at least the reference cold-start interval;
2. release any stale hold on the relevant pins;
3. sample GPIO2 after INPUT_PULLUP settling;
4. sample it again after INPUT_PULLDOWN settling;
5. both LOW means an external pull-down and selects GC1109;
6. both HIGH means an external pull-up and selects KCT8103L;
7. disagreement or unstable repeated samples means unknown/no-FEM and fails initialization.

This reuses the same externally-held-versus-floating classification already proven in the V3 battery-control path.
The GPIO sequence remains board-specific; the pure classification should have one shared/tested helper if extraction
is cheaper than duplicating its truth table.

The two-pull method is an entry check on both physical boards. If either real FEM's bias is too weak to dominate an
ESP32 internal pull, stop and revise the detector from measurement; do not silently fall back to a single guessed
read.

### 5.2 Mode tables

GC1109/V4.2:

| state | GPIO7 LDO | GPIO2 EN | GPIO46 TX_EN |
|---|---:|---:|---:|
| RX | HIGH | HIGH | LOW |
| TX | HIGH | HIGH | HIGH |

KCT8103L/V4.3:

| state | GPIO7 LDO | GPIO2 CSD | GPIO5 CTX |
|---|---:|---:|---:|
| RX, LNA enabled | HIGH | HIGH | LOW |
| RX, LNA bypass | HIGH | HIGH | HIGH |
| TX | HIGH | HIGH | HIGH |

The apparent equality of V4.3 TX and RX-bypass pin levels does not remove the lifecycle hook. The driver still owns
the semantic transition, and a later LNA setting must not require surgery in Sx1262Radio.

### 5.3 Fail closed

If detection, initialization or a requested mode fails:

- no startReceive/startTransmit follows that failed transition;
- start_transmit returns TxResult::radio_error;
- the existing DeviceHal failed-arm outcome path remains authoritative;
- g_radio_ok is false for a boot-time failure;
- boot and status expose fem=unknown and a counted RF-mode failure;
- no bypass, assumed revision or lower-power substitution occurs.

The detector cannot prove an arbitrary counterfeit or electrically damaged board is genuine. Its guarantee is
narrow and honest: it rejects the observable floating/unstable case and identifies the two known boards on the bench.

---

## 6. Exact radio sequencing

Sx1262Radio receives an optional IBoardRf pointer.

### 6.1 Boot

1. CustomSX1262::std_init initializes the chip at the safe V4 chip-drive default; it emits no packet.
2. Firmware validates and applies the loaded frequency through the Sx1262Radio frequency authority. An invalid
   value leaves the safe build-default carrier in hardware, marks frequency invalid and cannot be used for TX.
3. Sx1262Radio::begin registers DIO1.
4. If a front end exists, begin initializes and detects it.
5. begin calls arm_rx; it does not call RadioLib startReceive directly.
6. arm_rx first establishes FEM RX mode, then calls the sole startReceive expression.
7. The combined chip/FEM/RX result becomes g_radio_ok. RF-configuration validity and the composite rfok result are
   printed with it; they are not folded back into g_radio_ok.

### 6.2 Transmit

1. Before touching the radio, require FEM readiness and a valid current frequency, translate the desired conducted
   output to chip drive, and refuse any unsupported state. A preflight refusal leaves continuous RX untouched.
2. standby;
3. apply SF/BW/CR/preamble and the translated chip drive;
4. clear the shared DIO1 edge;
5. establish FEM TX mode;
6. call the sole startTransmit expression.

If TX-mode or startTransmit fails, call arm_rx to restore the FEM and continuous RX before returning radio_error.

### 6.3 Completion and recovery

poll_tx_done and abort_tx retain their current order and both finish through arm_rx. Retunes and successful RX reads
also retain their current order and finish through arm_rx. No caller sets FEM mode directly.

The current collect_tx_completion/outcome/timer/pump order in fw_main is unchanged.

---

## 7. Honest output power and frequency

### 7.1 Split chip drive from requested output

Introduce MR_DEFAULT_OUTPUT_DBM:

    #ifndef MR_DEFAULT_OUTPUT_DBM
    #define MR_DEFAULT_OUTPUT_DBM LORA_TX_POWER
    #endif

Existing boards define nothing new and retain identical values.

For heltec_v4:

- remove the inherited common LORA_TX_POWER=22 flag before defining LORA_TX_POWER=10;
- LORA_TX_POWER is only the safe SX1262 chip-drive value consumed by vendored std_init;
- MR_DEFAULT_OUTPUT_DBM=22 is the fresh-NV, fallback, leave and user-visible default;
- g_tx_power and TxParams power carry desired conducted output;
- only the translated chip_dbm reaches RadioLib setOutputPower.

Update the comments and names at the NV field, Hal/IRadio boundary, fw_main state, config/help/status and leave path.
No NV layout or version bump is required: existing boards retain the same numeric meaning and no V4 MeshRoute target
previously existed.

### 7.2 First-build power capability

Per approved D3, the first V4 driver exposes the singleton supported set:

| requested nominal conducted output | SX1262 chip drive |
|---:|---:|
| 22 dBm | 10 dBm |

Its min and max are both 22. The console accepts only 22 on this board. A persisted other value is not rewritten:
boot reports RF configuration as invalid, the composite rfok result is false, and every send is refused until the
owner saves 22. g_radio_ok continues to report the independently established hardware/RX state. Existing boards
continue accepting -9 through 22.

The later calibration slice may replace this singleton with separate monotonic tables for GC1109 and KCT8103L.
Selection rounds down and never exceeds the requested conducted output. 28 dBm remains unavailable until measured.

### 7.3 Frequency envelope

Per confirmed D1, the V4 high-power capability accepts 863 through 928 MHz. The local cfg/create/join inputs must query
one frequency predicate rather than each restating a range.

The radio remains the final backstop because remote/adopted configuration is another producer. Add one concrete
Sx1262Radio frequency-apply helper used by both boot/grouped live configuration and set_rx_freq. It owns the current
frequency-valid flag; there must be no direct setFrequency bypass in fw_main or firmware_config:

- an unsupported retune leaves the last valid RF setting, marks RF configuration invalid and counts the refusal;
- validation precedes standby, so that refusal also leaves continuous RX armed on the last valid carrier;
- start_transmit refuses while RF configuration is invalid;
- a later valid retune clears that condition immediately, without a reboot;
- status exposes the refusal count and validity.

The current global 100–1000 MHz parser may remain for other boards. A V4 must never transmit outside its declared
hardware envelope merely because the generic parser accepted it.

---

## 8. Platform environment

The heltec_v4 environment uses the same pinned pioarduino platform, C++ mode, RadioLib and U8g2 versions as V3, plus
the project-local V4 board definition.

Required V4-only values:

- BOARD_HELTEC_V4;
- MR_BOARD_RF_FRONTEND=1;
- board heltec_v4 and board_build.variants_dir = arduino_variants;
- LoRa NSS 8, DIO1 14, RESET 12, BUSY 13;
- SPI SCLK 9, MISO 11, MOSI 10;
- FEM GPIOs 7, 2, 46 and 5;
- DIO2 RF switch, 1.8 V TCXO, 140 mA current limit, boosted RX and the V4 register 0x8B5 patch;
- MR_FEAT_OLED=1 and the common Heltec canvas include/source;
- OLED 17/18/21, Vext 36 active HIGH, button 0, VBAT 1, ADC control 37 fixed active HIGH;
- safe chip init 10 and default nominal output 22;
- the D2 LNA default.

The source filter adds only the V4 RF TU and shared canvas; firmware_ui.cpp remains the one feature renderer.

Add board_name's explicit heltec_v4 arm. All other production BOARD_HELTEC_V3 occurrences are already ESP32
architecture OR-chains and therefore reach V4 through ARDUINO_ARCH_ESP32; do not add another board macro to those
chains merely for visual symmetry.

The V4 board JSON selects native USB CDC. Update console_sink.h's V3-only transport census and verify the existing
whole-line, non-blocking sink against HWCDC::availableForWrite on metal. Do not fork a V4 console implementation.

The default_16MB partition table has two 0x640000 OTA app slots and remains compatible with device_ota.cpp.

---

## 9. Diagnostics

Boot and USB status must make the physical result observable:

- board=heltec_v4;
- fem=gc1109, kct8103l or unknown;
- lna=on, bypass or n/a;
- radiohw=0/1, rfcfg=0/1 and their conjunction rfok=0/1;
- rfmodefail count;
- rfbandfail count;
- requested nominal output and selected chip drive.

Do not report V4.2/V4.3 from a compile-time name. The runtime FEM result is the authority.

No OLED screen or string is added in this slice. No BLE/JSON contract extension is required for the first build;
USB serial is the explicit bring-up diagnostic. A later diagnostics-contract slice may add the fields to structured
status.

---

## 10. Verification

### 10.1 Automated

Add a host probe that compiles the real production Sx1262Radio header against counting Arduino/RadioLib/FEM fakes.
It must prove:

- FEM begin precedes the initial arm;
- begin reaches the sole arm_rx authority;
- rx_mode precedes every startReceive;
- power translation and tx_mode precede startTransmit;
- failed translation or tx_mode emits no startTransmit;
- failed startTransmit restores RX;
- completion, abort, every retune and packet drain restore RX;
- null-FEM ordering and return behavior remain valid for existing boards;
- the T3 collect/outcome/timer/pump order is untouched.

Mutation controls must turn the probe red for deleting or reversing each critical transition, bypassing arm_rx at
boot, or using requested output directly as chip drive.

Pure tests cover:

- external-bias classification, including floating/unstable;
- both FEM mode truth tables;
- singleton output mapping and refusal;
- V4 frequency boundaries;
- invalid-to-valid frequency and output correction restores rfcfg/rfok without changing hardware-failure truth;
- all enum-to-name switches.

Extend tools/probe_board_ui/run.sh to exercise both common-canvas trait sets. Keep the firmware UI probe focused on
board-independent screens; it need not duplicate every case for a second identical canvas.

The warning census derives OLED environments. Adding V4 profiles without expectations must fail. Measure and pin all
six OLED environments, with zero -Wswitch.

Run:

- native suite and its binary;
- current exact s18 keystone from simulation/BASELINE.md;
- tools/probe_board_ui/run.sh;
- tools/probe_firmware_ui/run.sh;
- the new radio sequencing probe and mutations;
- tools/warning_census.sh;
- every board environment sequentially, including all three V4 profiles;
- git diff --check.

Confirm no change to sizeof(Node), TxOutcome, wire/NV layout, timers, corpus anchors or simulator outcomes. Record
per-board RAM and flash, especially the new V4 values and the null-FEM cost on existing boards.

### 10.2 Metal — both boards

Add the irreducible checks to docs/2026-07-31-bench-test-script.md with exact expected lines.

Before every TX test, attach the correct antenna or a rated dummy load.

1. Clean-flash and ordinary-flash heltec_v4 onto V4.2 and V4.3.
2. Confirm native USB upload, monitor reconnect, command input and complete output lines.
3. Confirm board=heltec_v4 and the correct unique FEM label on each board.
4. Confirm an induced ambiguous/no-FEM detector arm refuses RF and reports unknown.
5. Receive a frame before this board has transmitted anything; this proves the initial RX transition.
6. Exchange frames V4.2 to V4.3 and V4.3 to V4.2; verify TxDone, outcome delivery and restored RX.
7. Exercise a forced startTransmit failure and watchdog abort; verify RX recovers.
8. With no external Vext load attached, verify the GPIO36 level that powers the panel on each revision by panel ACK
   and current; reconcile the MeshCore-HIGH / Heltec-datasheet-LOW conflict before pinning the trait.
9. Let the OLED page frame complete; test blanking, short button wake and battery plausibility on both boards.
10. While light-sleeping, receive a LoRa frame and prove the DIO1 wake counter increments and the frame is delivered.
11. On V4.3, measure receive behavior and current with the approved D2 LNA state and compare against bypass; record
    the evidence and revisit D2 only if metal contradicts the expected reachability tradeoff.
12. At the first-build 10 dBm chip drive, measure conducted output if suitable RF equipment is available. Until then,
    label 22 dBm nominal/reference-derived.
13. Preserve each firmware.elf and version banner with the result.

A second-node RSSI comparison is useful for function and relative sensitivity. It is not an absolute power
calibration.

---

## 11. Implementation slices

Each slice is independently reviewable and obeys C1.

| slice | kind | content | completion |
|---|---|---|---|
| V4-0 | design | D1–D3 approved and recorded here on 2026-08-25 | complete; specification dispatchable |
| V4-1 | refactor | move V3 canvas to heltec_common; required traits; dual board-ui probe; V3 byte/behavior proof | every existing gate green; no V4 env |
| V4-2 | RF-seam feature | IBoardRf/provider; Sx1262Radio single arm_rx authority; null FEM on every existing board; truthful initial-arm failure; production-header probe | existing boards green; no V4 env |
| V4-3 | V4 feature | board assets and provenance, heltec_v4 env, V4 RF driver/detection, fixed power and band enforcement, truthful diagnostics, V4 UI traits | heltec_v4 builds and passes both-board base metal checklist |
| V4-4 | profile expansion | heltec_v4_mobile and gateway_heltec_v4; census and user/developer docs | all profiles build and census is pinned |
| V4-5 | later RF feature | measured per-revision power tables and any runtime LNA control | separate reviewed design and RF calibration |

V4-3 is the first image the owner can flash today. There is deliberately no intermediate V4 environment whose binary
can transmit without the FEM driver.

---

## 12. Documentation obligations

With implementation:

- update platformio.ini's environment index and comments;
- update docs/firmware-dev-guide.md with build/upload commands and native-USB recovery;
- update tools/vendor_meshcore.sh and lib/meshcore/NOTICE for V4 assets;
- update console_sink.h's transport facts;
- add the metal residue above to the maintained bench script;
- update warning-census expectations only from measured clean builds;
- record the completed V4 support and residual calibrated-power/LNA work in the maintained project state.

Do not change protocol.md, frames.md, the wire version, simulator corpus or user-visible OLED behavior for this port.

---

## 13. Source authority

Verified references for Revision 3:

- Heltec original-V4 product and V4.3 notes:
  https://heltec.org/project/wifi-lora-32-v4/
- Heltec V4.2 datasheet, including 433–510 / 863–928 populations, SSD1315 and memory:
  https://resource.heltec.cn/download/WiFi_LoRa_32_V4/datasheet/WiFi_LoRa_32_V4.2.0.pdf
- Heltec current board pin/FEM definitions:
  https://github.com/HelTecAutomation/Heltec_ESP32/blob/master/src/driver/board-config.h
- MeshCore V4 assets and FEM reference, pinned for this design:
  https://github.com/meshcore-dev/MeshCore/tree/0679dbeffc504d562d2f09eb072fdc223f8ffc2a/variants/heltec_v4
- ESP-IDF ESP32-S3 light-sleep state and wake behavior:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html

These are implementation references, not dependencies except for the explicitly vendored board JSON and
pins_arduino.h. Re-verify them before a later V4 R8 port; R8 is not a compatible alias for this target.
