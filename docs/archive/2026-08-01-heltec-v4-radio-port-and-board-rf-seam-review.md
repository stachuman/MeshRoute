> ## ⛔ OBSOLETE — ADDRESSED AND ARCHIVED 2026-08-01
>
> **Every finding was actioned in revision 2 of the spec** (`docs/superpowers/specs/2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md`). Kept as the audit trail for why it changed — do not action again.
>
> | § | finding | where it landed |
> |---|---|---|
> | 1 | FEM has no init / fail-closed lifecycle (P0) | §3.1 `begin()` + §3.2 `_fem_ready` + **§3.4 fail-closed** (no TX, visible reason, and `Sx1262Radio::begin()`'s bool folded into `g_radio_ok` — R1) |
> | 2 | `LORA_TX_POWER` cannot mean chip drive *and* antenna output (P0) | **§4.2** — the `MR_DEFAULT_OUTPUT_DBM` split, aliased so identity boards are unchanged |
> | 3 | lookup/clamp contract incomplete (P0) | §3.1 `BoardRfDrive{valid,chip_dbm}` + `min/max_output_dbm()`; **§4.1** totality table; **§4.3** runtime clamps at console *and* radio, plus boot validation of persisted NV |
> | 4 | "antenna dBm" conflates conducted vs EIRP (P0) | **§4** — contract named **conducted at the antenna connector**; EIRP/ERP explicitly out of scope with a "must not be described as compliant" rule; §8 demotes second-node RSSI to a relative check |
> | 5 | calibration coverage must gate R4 | **§7 R4 entry criteria** — per-revision table, band coverage, refuse-or-cap when unknown; out-of-band `cfg set freq` refused for the V4 env |
> | 6 | `sleep_mode()` would break light sleep | **§6** — renamed `power_off()`, deep-only, **never called from `board_sleep_until()`**; deep-sleep current removed from R3's gate |
> | 7 | no V4 board manifest exists | **§5** — verified (pinned platform has V3, not V4); R2 must list every asset and build **from a clean checkout** |
> | 8 | native/s18 gates do not exercise production sequencing | **§8** — the "s18 proves the seam inert" claim is **retracted**; per-level table of what each gate actually proves; on-target instrumented trace is the real coverage |
> | 9 | two-vs-three call sites | **§2** — "three placements" stated once and used consistently; the ambiguity itself recorded as the reason ③ gets omitted |
> | 10 | semantic rename incomplete | **§9** — the declaration list, plus the `output_dbm` / `chip_dbm` lexical split |
>
> **Owner ruling this review prompted (2026-08-01):** `tx_power` means **desired conducted dBm at the antenna connector**; **EIRP is a separate policy layer**, out of scope for the board seam.
>
> §11's "already sound" items were retained unchanged: composition over subclassing, the `begin()` RX-arm exception, the non-constant FEM gain, the V4-only reset/register deltas, and keeping LR1110 in its own spec.
>
> Findings were **not** entered in `docs/2026-07-30-open-bug-register.md`: that register is for implemented code (owner ruling 2026-08-01), and this reviewed an unimplemented spec. The one item about existing code — `fw_main.cpp` discarding `g_iradio.begin()`'s return — is folded into **R1** rather than registered, since R1 is where the value first carries meaning.

---

# Heltec V4 radio port + board-RF seam — review findings

*2026-08-01. Standalone review of `docs/superpowers/specs/2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md`. The reviewed design is not modified by this document.*

## 0. Outcome

The proposed composition seam is the right size and is preferable to a V4-specific `Sx1262Radio` subclass. The design also correctly identifies the initial `begin()` receive arm, the non-linear FEM gain, the V4-only reset/register deltas, and the need to keep the LR1110 port separate.

The spec is not ready to dispatch, however. Four issues are safety or bring-up blockers:

1. `IBoardRf` has no initialization/failure contract, so the V4 FEM detection is never wired into boot;
2. `LORA_TX_POWER` would simultaneously mean desired antenna output and direct SX1262 chip drive;
3. the lookup contract has no defined lower bound or full-domain coverage, and the radio-side snippet does not enforce the board output range;
4. “antenna dBm” and the proposed RSSI bench method are not precise enough to support the regulatory claim.

Several plan/gate details also need correction: the project lacks the V4 board manifest/Arduino variant, the sleep hook does not match the current light-sleep behavior, and the proposed native/s18 tests do not exercise the production hook placements.

## 1. The FEM has no initialization or fail-closed lifecycle

**Severity: P0 — the V4 can reach RX/TX with an uninitialized FEM object.**

The interface contains only `tx_mode()`, `rx_mode()`, and `sleep_mode()` (`design.md:50-62`). R3 says to wire a V4 implementation into the radio constructor (`design.md:182`), but never specifies who initializes it or when.

That initialization is not a trivial constructor. The referenced MeshCore implementation:

- powers the FEM LDO on GPIO 7;
- releases RTC holds;
- waits for cold-start and input settling;
- samples GPIO 2 once to detect GC1109 versus KCT8103L;
- configures different control pins and establishes an initial state.

See `/home/staszek/MeshCore/variants/heltec_v4/LoRaFEMControl.cpp:6-41`.

MeshRoute initializes RadioLib at `src/fw_main.cpp:572-579`, then performs the initial continuous receive at `:737-745`. Merely storing a pointer in the global `Sx1262Radio` does not invoke the FEM initialization before either operation. An uninitialized `fem_type` makes the referenced mode functions no-ops.

### Proposed solution

Add an explicit fail-capable lifecycle to the seam:

```cpp
struct IBoardRf {
    virtual ~IBoardRf() = default;
    virtual bool begin() = 0;       // detect/configure and establish a safe RX state
    virtual void tx_mode() = 0;
    virtual void rx_mode() = 0;
    virtual void power_off() {}     // deep power-off only; see §6
};
```

`Sx1262Radio::begin()` should call `_fem->begin()` before the initial `rx_mode()`/`startReceive()`. It must remember readiness and refuse `start_transmit()` if initialization failed. The setup caller must combine the return with `g_radio_ok`; it currently ignores `g_iradio.begin()`'s boolean result (`src/fw_main.cpp:744`).

The V4 implementation must fail closed if detection/configuration cannot establish a known type. On failure: no TX, visible boot/status reason, and the FEM put in its safest available state. Add a cold boot, warm reset, and sleep-wake lifecycle matrix to R3.

## 2. `LORA_TX_POWER` cannot carry both chip-drive and antenna-output semantics

**Severity: P0 — the new fleet-wide meaning is not actually applied at every use of the constant.**

The design rules that `tx_power` means desired output at the antenna (`design.md:114-118`) and translates it immediately before each `setOutputPower()` (`:145-152`). But `LORA_TX_POWER` is also consumed directly by the vendored RadioLib initialization:

- `CustomSX1262::std_init()` passes `LORA_TX_POWER` straight to `SX1262::begin()` as chip power (`lib/meshcore/src/helpers/radiolib/CustomSX1262.h:45-49`);
- `g_tx_power` initializes from the same macro (`src/fw_main.cpp:189`);
- the `leave` reset seeds NV from the same macro (`src/firmware_config.cpp:1169-1177`).

On V4, leaving `LORA_TX_POWER=10` keeps RadioLib's chip initialization safe but makes the new desired-output default 10 dBm, which the two-point table cannot even represent. Changing it to the intended desired output of 22 makes `std_init()` configure 22 dBm chip drive—the high-output operating point the design is trying not to select accidentally.

### Proposed solution

Split the compile-time concepts while preserving aliases for existing boards:

```cpp
#ifndef MR_DEFAULT_OUTPUT_DBM
#define MR_DEFAULT_OUTPUT_DBM LORA_TX_POWER
#endif
```

- `LORA_TX_POWER` remains the direct, safe SX1262 initialization value required by the vendored header (V4: 10 unless measurement rules otherwise).
- `MR_DEFAULT_OUTPUT_DBM` becomes the operator/NV/HAL default (V4: the approved conducted-output default, likely 22 after §4 is clarified).
- `g_tx_power`, fresh-blob seeding, `leave`, status text, and config comments use `MR_DEFAULT_OUTPUT_DBM` semantics.

The per-frame path must still translate every desired-output request before `setOutputPower()`. Document that the RadioLib boot value is chip drive and is never the operator setting on a gain board.

## 3. The lookup and clamp contract is incomplete

**Severity: P0 — low requests and stale/out-of-range configuration have no safe defined behavior.**

The referenced vendor material supplies only two points: chip 10 → output 22 and chip 22 → output 28 (`design.md:120-129`). The algorithm selects the highest table entry whose output does not exceed the request (`:133-143`). For every request below the lowest measured output, no entry qualifies. The spec does not define whether that case refuses, bypasses the PA, or accidentally selects a point that over-delivers.

The design calls for two clamps (`design.md:154`), but the radio-side snippet clamps only the derived chip value (`:147-151`). The requested output is checked only at the console through compile flags (`:156-165`). That is not a sufficient safety boundary:

- persisted NV can come from an older firmware or another policy;
- non-console producers can supply a per-frame power override;
- a revision-dependent ceiling cannot be represented by one build-time flag;
- `IBoardRf::max_output_dbm()` is not used by the shown radio path.

### Proposed solution

Make the board translation a total, validated capability rather than an `int8_t` function that cannot report failure:

```cpp
struct BoardRfDrive { bool valid; int8_t chip_dbm; };
virtual int8_t min_output_dbm() const = 0;
virtual int8_t max_output_dbm() const = 0;
virtual BoardRfDrive drive_for_output(int8_t wanted_output_dbm) const = 0;
```

Requirements:

- the allowed request interval must be fully covered by measured table data;
- requests below/above it refuse loudly rather than select an over-power point;
- the table is sorted by chip drive and measured output is monotonic non-decreasing;
- selection uses a measurement/uncertainty guard, not merely a mean value equal to the limit;
- the returned chip drive is still clamped/asserted to SX1262 −9..22;
- the radio path enforces the board min/max as the final backstop, even after console validation;
- boot validates persisted `tx_power` against the detected board and reports/remediates an invalid value before sends begin.

Prefer one runtime source of truth. `firmware_config.cpp` already includes `fw_context.h` and can query `g_iradio`; exposing detected min/max through `Sx1262Radio` avoids duplicating a runtime board capability in `MR_TX_*` flags. If flags are retained, define constants once and use them in both the override and a compile-time assertion; do not maintain two independent numbers.

Add pure tests for: below minimum, exact points, between points, maximum, above maximum, non-monotonic table rejection, and both detected FEM revisions.

## 4. “Antenna dBm” and the calibration method need a precise measurement contract

**Severity: P0 policy/safety gap — the table cannot be called regulatory without defining what is measured.**

The spec equates “what leaves the antenna” with the number regulations limit (`design.md:114-118`). That phrase conflates at least two different quantities:

- **conducted power at the RF connector/feed point**;
- **radiated ERP/EIRP**, which also depends on antenna gain, cable loss, orientation, installation, frequency, and measurement geometry.

A board-only chip-drive table can define conducted output. It cannot define EIRP for a replaceable external antenna. If the product meaning is EIRP, antenna/install information must be part of the configuration and compliance calculation; it is not a property of `IBoardRf` alone.

The R3/R4 gates allow “a power meter, or a calibrated second node's RSSI at fixed distance” (`design.md:182`, `:187`). A second node at a fixed distance is useful for relative TX/RX sanity, but RSSI is not an absolute power calibration unless the complete link—receiver calibration, path loss, antennas, cable loss, polarization, reflections, and uncertainty—has itself been calibrated. It should not populate a table described as regulatorily meaningful.

### Proposed solution

Choose and name one contract explicitly. The practical board-layer choice is **desired conducted dBm at the antenna connector**. Then:

- calibrate with a suitable RF power meter/spectrum analyzer and rated attenuation/coupling, or a documented calibrated OTA setup;
- state measurement uncertainty and retain margin below any configured ceiling;
- treat ERP/EIRP and regional compliance as a separate policy using antenna gain/loss;
- keep second-node RSSI as a relative functional check only.

If the intended contract remains EIRP, this design needs a broader regional/antenna policy and cannot claim the change is confined to the board seam.

## 5. Calibration coverage must be a prerequisite, not an optional open question

**Severity: P1 — one table may be unsafe for the other FEM revision or frequency band.**

The implementation detects two different FEMs at runtime (`design.md:89-96`), but whether their power curves differ remains open (`:204-210`). The metal gate says “per silicon revision if both are to hand” (`:197-202`). That is insufficient for a translation that promises never to exceed the request.

Power behavior also varies with frequency, supply, temperature, unit tolerance, and the board's RF matching. MeshRoute currently accepts `cfg set freq` across 100..1000 MHz (`src/firmware_config.cpp:154-156`); a V4 FEM/antenna calibration measured at one frequency cannot justify that whole range.

### Proposed solution

Before enabling R4 on a detected revision:

- require a table for that revision, or use a documented conservative fallback with a deliberately restricted output set;
- measure across the supported hardware band and use worst-case-safe values/margin;
- define the V4 environment's supported frequency interval and reject out-of-band configuration for that hardware;
- refuse TX or cap to an explicitly safe bring-up point when revision/table/band is unknown.

R3 switching can land before full calibration, but an uncalibrated R4 must remain a non-release/bench configuration. Reword Q6 as an R4 entry criterion.

## 6. The sleep hook does not match MeshRoute's current sleep behavior

**Severity: P1 — using `sleep_mode()` on the existing sleep path would make the node deaf.**

MeshRoute uses ESP32 **light sleep while the radio remains in continuous RX**, with DIO1 as a wake source (`src/fw_main.cpp:919-947`, `:1343-1358`). The referenced MeshCore deep-sleep path does not power the FEM off; it explicitly holds the FEM in RX mode so DIO1 can wake the MCU (`HeltecV4Board.cpp:35-57`, `LoRaFEMControl.cpp:85-103`).

The proposed `sleep_mode()` means “FEM off for deep sleep” (`design.md:57-62`), but R3 does not say where it is called and the metal gate asks for deep-sleep current (`:182`, `:197-202`) even though this tree has no V4 deep-sleep/power-off path.

### Proposed solution

- Rename the current optional method to `power_off()` or `deep_sleep_off()` so it cannot be mistaken for light sleep.
- Do not call it from `board_sleep_until()`; light sleep must retain RX mode and GPIO state.
- If deep sleep with radio wake is later added, give it a distinct `prepare_rx_wake_sleep()` lifecycle that configures RTC holds exactly as required.
- Remove deep-sleep current from R3's acceptance gate, or add the missing deep-sleep feature as an explicit separate slice.
- Keep the warm/deep-reset GPIO-hold question, but attach it to that future slice rather than blocking the current light-sleep port.

## 7. The V4 PlatformIO environment needs board assets or an explicit base-board strategy

**Severity: P1 implementation blocker — `board = heltec_v4` is not available in the pinned platform.**

MeshCore supplies its own `boards/heltec_v4.json` and `variants/heltec_v4/pins_arduino.h`. MeshRoute has neither, and the installed/pinned Espressif platform contains V3 manifests but no `heltec_v4` manifest. R2 currently says only “envs + pins” (`design.md:181`), which is not enough to reproduce the 16 MB flash, 2 MB PSRAM, USB, partition, and Arduino-variant settings.

### Proposed solution

Choose one approach and list every file in R2:

1. add a project-local V4 board JSON plus its Arduino variant and configure the variants directory; or
2. extend a supported generic ESP32-S3 board and spell out flash/PSRAM/partition/USB properties plus every board pin used by this project.

Do not assume MeshCore's project-local board definition exists in the pinned PlatformIO package. Add a clean-environment build check so success does not depend on a developer's unrelated local MeshCore checkout.

## 8. The proposed native and s18 gates do not exercise the production sequencing

**Severity: P1 verification gap.**

The spec correctly notes that `Sx1262Radio` is device-only, then proposes a native “test double of the sequence” (`design.md:189-194`). Unless production sequencing is extracted and shared, such a test exercises the test double's order, not the actual calls around RadioLib.

Likewise, s18 does not use the Arduino/RadioLib `Sx1262Radio` path. An exact s18 digest proves that the core simulation stayed unchanged; it does not prove that a null FEM is byte-identical on device or that hooks were inserted correctly (`design.md:180`, `:195`). Adding a pointer and branches also means the board binary itself will not be byte-identical.

### Proposed solution

- Keep s18 exact as the “no core behavior changed” gate, but describe it honestly as orthogonal to the device seam.
- Either extract a small production-used sequencing component that can be driven with a fake radio/FEM natively, or make hook ordering an instrumented on-target test.
- Add null-FEM V3 and XIAO TX/RX smoke checks, including start failure and watchdog abort.
- On V4, record mode transitions and assert no `startTransmit` occurs outside TX mode and no `startReceive` outside RX mode.
- Keep the source enumeration (`startReceive`/`startTransmit` grep) as a review check, not as a substitute for runtime coverage.

## 9. Call-site count is internally inconsistent

**Severity: P2 editorial defect with a metal-failure consequence.**

The spec correctly concludes there are three mode-call placements (`design.md:40-44`) and lists all three (`:75-77`), but §3.2 says “two call sites” (`:67`) and R1 again says “the two `Sx1262Radio` call sites” (`:180`). This is exactly the wording that can cause the initial `begin()` RX hook to be omitted.

Change every occurrence to **three placements: TX arm, common RX re-arm, initial RX arm**. Distinguish two method categories (`tx_mode`, `rx_mode`) from three code placements if that was the intended meaning.

## 10. Complete the semantic rename

**Severity: P2 documentation/API consistency.**

R4 changes the meaning of persisted/operator `tx_power`, but several current declarations still say SX1262/chip dBm, including `src/device_nv.h:48`, `lib/hal/iradio.h:30-31`, `lib/core/hal.h:29`, `src/fw_main.cpp:189`, and console/help/status text. The spec names only the validator.

Add these comments and user-facing strings to R4's file list. Keep the low-level variable passed to RadioLib explicitly named `chip_dbm`; keep the value above the board translator explicitly named `output_dbm`. This prevents the two meanings from reconverging later.

## 11. Findings that are already sound

- Composition is preferable to subclassing here; it keeps the proven RX/TX sequence in one implementation.
- The initial `begin()` receive arm really is the exception to `arm_rx()` and must be covered.
- A constant FEM-gain offset is unsafe; a conservative measured mapping is the correct shape once its domain and uncertainty are defined.
- V4 reset GPIO 12 and the V4-only register patch are correctly isolated.
- T1000-E/LR1110 should remain in its own design and slice; it shares `IRadio`, not this FEM lifecycle.

## 12. Recommended revision order

1. Add the FEM initialization/readiness lifecycle and exact boot ordering (§1).
2. Split direct chip-init power from desired output power (§2).
3. Define a total, runtime-enforced power capability with min/max/error semantics (§3).
4. Choose conducted output versus ERP/EIRP and replace the RSSI calibration claim (§4).
5. Make revision/band calibration an R4 prerequisite (§5).
6. Separate light-sleep RX retention from future deep-sleep/power-off behavior (§6).
7. Specify all V4 PlatformIO board assets (§7).
8. Replace the self-testing sequence double and vacuous s18 claim with production-path gates (§8).
9. Apply the call-count and semantic-name corrections (§9-§10).

## 13. Minimum acceptance additions

- FEM `begin()` occurs exactly once before the initial RX arm; failed/unknown detection makes TX impossible and visible.
- Cold boot, warm reset, light-sleep wake, and any future deep-sleep wake each establish the correct FEM state.
- `LORA_TX_POWER` is proven to remain direct chip drive while the persisted/operator default is desired conducted output.
- every accepted desired-output value maps to a measured conservative point; below/above-range values refuse.
- old/out-of-range NV cannot bypass the runtime board limit.
- both FEM revisions use measured or explicitly conservative tables, selected by the detected type.
- calibration is valid over the allowed hardware frequency band and out-of-band `cfg set freq` is refused.
- calibrated power measurement uses an absolute RF method with stated uncertainty; second-node RSSI remains relative-only.
- a clean checkout can build both V4 environments without files from the MeshCore checkout.
- null-FEM current boards pass on-target TX/RX/error/abort smoke checks; s18 remains exact as a core-only gate.
- all three production placements are instrumented: TX arm, common RX re-arm, and initial RX arm.
