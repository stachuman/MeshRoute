<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Heltec V4 radio port + the board-RF seam — design spec

*2026-08-01. Owner-approved scope: the V4 port is the vehicle for a **board-RF seam** (option (b) of the 2026-08-01 source-separation discussion) rather than a one-off. Line references code-verified today; **re-verify before acting (V1/V2)** — this tree moves several times a day.*

**Status: SPEC'D, NOT DISPATCHED.**

*Revision 2 (2026-08-01) — rewritten against the review archived at `docs/archive/2026-08-01-heltec-v4-radio-port-and-board-rf-seam-review.md`. Material changes: a fail-closed FEM lifecycle; the `LORA_TX_POWER` macro split (it is consumed by a **vendored** file as chip drive); a total, failure-reporting power capability with runtime clamps; **conducted-at-connector** named as the power contract with EIRP explicitly out of scope; calibration coverage as an R4 entry criterion; `sleep_mode` → `power_off` with a "never called on the light-sleep path" rule; the V4 board assets; honest gates; and the two-vs-three call-site inconsistency fixed.*

**Why it exists:** `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §10.2 named the V4 radio port a Phase B prerequisite (B-1/B-2) but deliberately did not contain it — a PA/LNA switching path and a transmit-power semantics change do not belong inside a display feature (C1).

---

## 0. The problem

The Heltec V4 has an **external front-end module** (PA + LNA) that must be switched between transmit and receive on **every frame**, and initialised — including a runtime silicon-revision detect — before either. MeshCore drives this from a per-board `Board` class (`~/MeshCore/variants/heltec_v4/`). **Our tree has no such hook**: `Sx1262Radio` drives RadioLib directly (`lib/hal/device_radio.h`), so nothing between the MAC and the silicon can run per-TX board logic. Without it a V4 transmits through a **bypassed PA with the LNA still in circuit** — the radio appears to work, badly, in a way no test in our corpus can see.

## 1. What is already right — do not rebuild it

| already solved | by | evidence |
|---|---|---|
| protocol ⟂ device | `lib/core` purity | **1** board `#if` in 43 files (`frame_trace.h:12`, a debug trace); the other grep hits are comments, incl. `hal.h:8` and `node.h:4` stating the rule |
| a **different radio chip** (LR1110 / T1000-E) | **`IRadio` itself** | a virtual interface; `Lr1110Radio : IRadio` is a new TU, not a refactor. It shares `IRadio`, **not** this FEM lifecycle — keep it in its own spec and slice |
| **optional** board capability, zero cost when absent | **`IRadio`'s own precedent** | `set_rx_freq` (`iradio.h:51`), `set_rx_bw`/`set_rx_cr` (`:57-58`) are non-pure virtuals with no-op defaults |
| per-board pins | `platformio.ini` build flags | already how NSS/DIO1/BUSY/SCLK reach `Module(...)` |
| a board-capability TU behind an interface | `lib/hal/mr_ui.h` + `src/board_ui.cpp` | inline no-ops when the feature is off, so call sites stay unconditional |

⇒ **The missing axis is per-TX board RF control.** One seam, not an architecture.

## 2. Present state of the TX/RX path (verified)

`Sx1262Radio` (`lib/hal/device_radio.h:69`) holds `CustomSX1262& _radio`:

| site | what it does | needs |
|---|---|---|
| `begin()` `:75-78` | ISR registration, then **`_radio.startReceive()` DIRECTLY** | FEM `begin()`, then **RX mode** |
| `start_transmit` `:137-157` | `standby()` → set SF/BW/CR/power/preamble → `startTransmit()` | **TX mode** before `startTransmit`; the `radio_error` path (`:150-154`) already returns via `arm_rx()` |
| `poll_tx_done` `:162-173` | TxDone → `finishTransmit()` → restore `_rx_sf` → `arm_rx()` | (covered by `arm_rx`) |
| `abort_tx` `:178-187` | watchdog → `standby()` → restore SF → `arm_rx()` | (covered by `arm_rx`) |
| `arm_rx()` `:324` | `startReceive()` + failure counter — **8 call sites**: `:152`, `:170`, `:186`, `:196`, `:208`, `:221`, `:229`, `:296` | **RX mode** |

★★ **THREE production placements — say "three" everywhere.** `arm_rx()`'s own comment (`:321`) claims *"every RX re-arm routes through here"*; that is true of **re**-arms, but the **initial** arm in `begin():77` predates it and bypasses it. A FEM hooked only into `arm_rx()` leaves the V4 booting with the front end **undefined until the first TX or first received packet** — intermittent, board-specific, invisible to every automated gate.

**The three placements are:** ① `tx_mode()` before `startTransmit` · ② `rx_mode()` inside `arm_rx()` · ③ FEM `begin()` + `rx_mode()` in `Sx1262Radio::begin()` before the initial `startReceive()`.

*(Two methods, three placements — an earlier revision of this spec said "two call sites" in two places while concluding three, which is exactly the wording that gets ③ omitted.)*

## 3. Design — `IBoardRf`, composed, with a fail-closed lifecycle

### 3.1 The interface

`lib/hal/iboardrf.h`:

```cpp
// The per-TX board RF seam. A board with an external front end (PA/LNA) implements this; every other board supplies
// nothing and the radio holds a null pointer. NOT part of IRadio: a front end is a property of the BOARD, not of the
// radio chip, and one SX1262 driver serves boards with and without one.
struct BoardRfDrive { bool valid; int8_t chip_dbm; };   // valid=false => the request cannot be met SAFELY -> refuse

struct IBoardRf {
    virtual ~IBoardRf() = default;

    // Detect/configure the front end and leave it in a safe RX state. Called ONCE, before the initial startReceive().
    // false = the board could not establish a KNOWN configuration -> the radio must refuse to transmit (§3.4).
    virtual bool begin() = 0;

    virtual void tx_mode() = 0;
    virtual void rx_mode() = 0;

    // DEEP power-off only (front end to µA). ⚠ NEVER called on the light-sleep path — see §6.
    virtual void power_off() {}

    // The board's conducted-output envelope AT THE ANTENNA CONNECTOR (§4). Defaults = the SX1262's own range, i.e.
    // the identity case for a board with no front end.
    virtual int8_t min_output_dbm() const { return -9; }
    virtual int8_t max_output_dbm() const { return 22; }

    // Map a DESIRED conducted output to the chip drive that achieves it WITHOUT EXCEEDING it. Total over int8_t:
    // every input either yields a measured-safe drive or {valid=false}. Default = identity (no front end).
    virtual BoardRfDrive drive_for_output(int8_t want_output_dbm) const {
        return { want_output_dbm >= min_output_dbm() && want_output_dbm <= max_output_dbm(), want_output_dbm };
    }
};
```

### 3.2 Wiring

`Sx1262Radio` gains one nullable member, a readiness flag, and the three placements of §2:

```cpp
    explicit Sx1262Radio(CustomSX1262& radio, IBoardRf* fem = nullptr) : _radio(radio), _fem(fem) {}
    IBoardRf* _fem = nullptr;
    bool      _fem_ready = false;
```

- `begin()`: `if (_fem) { _fem_ready = _fem->begin(); if (_fem_ready) _fem->rx_mode(); }` **before** the initial `startReceive()`; return false if the FEM failed, so the caller can surface it.
- `arm_rx()`: `if (_fem && _fem_ready) _fem->rx_mode();` before `startReceive()`.
- `start_transmit`: **refuse if the FEM is present but not ready** (§3.4), else `_fem->tx_mode()` immediately before `_radio.startTransmit(...)` — after the `standby()` + param block, so a refused arm still recovers through the existing `arm_rx()`.

**Implementer discipline:** grep the TU for `startReceive` and `startTransmit` and prove every occurrence is preceded by a mode call. Three today; a fourth added without one is the regression this seam invites. ⚠ This is a **review** check, not a substitute for the runtime coverage in §8.

**Why composition, not a `Sx1262FemRadio` subclass or virtuals on `IRadio`:** a subclass would have to re-override `start_transmit`, `poll_tx_done`, `abort_tx` **and** reach `arm_rx()` (private), duplicating the exact sequencing M11/H6 took several fixes to get right — the U1 rot. Hooks on `IRadio` would push a board concern into the radio-chip contract, which the LR1110 driver would then inherit in places it may not need.

### 3.3 The V4 implementation

`src/board_rf_heltec_v4.cpp`, compiled only under the V4 env. `begin()` performs, in MeshCore's order (`~/MeshCore/variants/heltec_v4/LoRaFEMControl.cpp:6-41`): power the FEM LDO (`P_LORA_PA_POWER=7`), release RTC holds, wait for cold-start settling, **sample GPIO 2 once** — pull-down ⇒ **GC1109 (V4.2)**, pull-up ⇒ **KCT8103L (V4.3)** — then configure that type's pins (GC1109 `EN=2`/`TX_EN=46`; KCT8103L `CSD=2`/`CTX=5`) and establish RX.

- ⚠ **Do not edit vendored files.** MeshCore is a reference, not a dependency: read the method, implement it in our TU under our own author header.
- ⚠ The detect reads a pin that is also a FEM control line. Sample **once**, cache the type, never re-probe.
- ⚠ Cold boot, warm reset and light-sleep wake must each leave a correct state — R3 carries that matrix (§8).

### 3.4 Fail-closed

If `_fem` is present and `begin()` returned false, the type is unknown and **no drive value can be trusted**:

- `start_transmit` returns `TxResult::radio_error` — **no TX at all**;
- the failure is **visible**: a boot line and a `status` field, not a silent mute;
- the front end is left in its safest reachable state;
- `Sx1262Radio::begin()`'s bool must be **combined into `g_radio_ok`** by the caller. ⚠ Today `fw_main.cpp` ignores that return value — fixing that is part of R1, since a seam whose failure is discarded is not fail-closed.

## 4. `tx_power` = **desired conducted dBm at the antenna connector**

★ **Ruled by the owner 2026-08-01**, and refined the same day: the contract is **conducted power at the antenna connector** — a board property we can measure and bound.

⚠ **EIRP / ERP is explicitly OUT OF SCOPE of this seam.** What regulations limit is radiated power, which adds antenna gain and cable loss to the conducted figure. Those are installation properties, not board properties, and `IBoardRf` cannot know them. A future region/antenna policy layer may sit **above** this contract and compute a conducted ceiling from an EIRP limit; this spec neither provides nor claims it. **Nothing in this document may be described as making the node regulatorily compliant** — it makes the *conducted* number honest, which is the prerequisite for such a policy.

★ **The migration is a no-op for every board that exists today.** With no front end, conducted output at the connector **is** chip drive (we do not model trace/connector loss), so `tx_power=22` keeps its meaning and no NV migration is needed. Only V4 — which has no env yet — gets a non-identity map.

### 4.1 The translation is not a constant offset

MeshCore's table gives V4 two points: chip **10 → 22 dBm** and **22 → 28 dBm** — gain **+12** then **+6**. The PA **compresses**, the same pattern the Station G2 rows annotate as *"1 dB compression point"*. A single gain constant calibrated at the high point would make a request for 22 dBm compute chip 16, which on a curve with +12 dB small-signal gain radiates well over the request. **Silent over-delivery.**

⇒ a **monotonic measured table** `(chip_dbm → measured conducted dBm)`, and `drive_for_output()` selects **the highest chip entry whose measured output does not exceed the request**.

**Total by construction:**

| request | result |
|---|---|
| below `min_output_dbm()` — no entry qualifies | `{valid=false}` → **refuse loudly**. Never "bypass the PA" or fall to a point that over-delivers |
| between measured points | the lower bracketing entry (round **down**) |
| at a measured point | that entry |
| above `max_output_dbm()` | `{valid=false}` → refuse |

Table invariants, asserted at construction: sorted by chip drive, measured output **monotonic non-decreasing**, and every value inside `[min_output_dbm, max_output_dbm]` covered. Selection carries a **margin below the ceiling** rather than treating a measured mean equal to the limit as acceptable.

### 4.2 ★★ `LORA_TX_POWER` must be split — it is consumed as CHIP DRIVE by a vendored file

**Verified:** `LORA_TX_POWER` is passed straight into `SX1262::begin(...)` as chip power at `lib/meshcore/src/helpers/radiolib/CustomSX1262.h:45` **and** `:49` (the TCXO-retry path) — a **vendored file we may not edit**. It also seeds `g_tx_power` (`src/fw_main.cpp:189`), the v2-blob fallback (`:622`) and the `leave` NV reset (`src/firmware_config.cpp:1176`).

One macro therefore cannot carry both meanings. On V4: leave it at 10 and the chip init is safe but the operator default becomes "10 dBm conducted", which the table **cannot represent** (its minimum is 22); set it to 22 and `std_init()` boots the chip at 22 — the 28 dBm high-output point this design exists to avoid.

**Split, preserving today's behaviour by aliasing:**

```cpp
#ifndef MR_DEFAULT_OUTPUT_DBM
#define MR_DEFAULT_OUTPUT_DBM LORA_TX_POWER   // identity boards: the two meanings coincide
#endif
```

- **`LORA_TX_POWER` stays direct chip drive** for the vendored init — on V4 the safe bring-up value (10 unless measurement rules otherwise).
- **`MR_DEFAULT_OUTPUT_DBM`** becomes the operator/NV/HAL default in conducted dBm; `g_tx_power`, fresh-blob seeding, `leave`, and status/help text move to it.
- Document at both sites that the RadioLib boot value is chip drive and is **never** the operator setting on a gain board.

### 4.3 Clamps — runtime, not build-time

Two enforcement points, both required:

1. **Console** (`src/firmware_config.cpp:142`) validates the requested conducted value against the **detected board's** `min/max_output_dbm()`, so the operator is refused loudly with the reason.
2. **Radio**, as the final backstop, because the console is not the only producer:

```cpp
        if (pw > -100) {
            const BoardRfDrive d = _fem ? _fem->drive_for_output(pw) : BoardRfDrive{ true, pw };
            if (!d.valid) return TxResult::radio_error;          // refuse; never substitute a nearby point
            _radio.setOutputPower(clamp_sx1262(d.chip_dbm));     // and still clamp to the chip's own -9..22
        }
```

⚠ **Prefer one runtime source of truth over `MR_TX_*` build flags.** Persisted NV can predate a policy change, a revision-dependent ceiling cannot be expressed by one compile-time number, and two independent constants drift. `src/firmware_config.cpp` already includes `fw_context.h` and can reach `g_iradio`; expose the detected envelope through `Sx1262Radio`. If a build flag is kept for the console, define it **once** and assert it equals the runtime value in the V4 TU.

**Boot validation:** a persisted `tx_power` outside the detected board's envelope must be reported and remediated **before any send**, not silently used.

## 5. Other V4 deltas

- **LoRa reset pin**: V4 uses **GPIO 12**; our `heltec_v3` env sets `LORA_PIN_RST=RADIOLIB_NC` (`platformio.ini:218`). Build-flag difference only.
- **`SX126X_REGISTER_PATCH=1`** (register 0x8B5, "improved RX") is set for V4 in MeshCore and absent from all our envs. Enable **for V4 only** — it is an unmeasured per-board RX tuning claim; adopting it fleet-wide needs an A/B on metal, not a copy.
- ★ **Board assets (R2 blocker, verified):** the pinned platform ships `heltec_wifi_lora_32_V3.json` but **no V4 manifest**, and our `boards/` holds only nRF52 assets — so `board = heltec_v4` fails. MeshCore supplies its own `boards/heltec_v4.json` + `variants/heltec_v4/pins_arduino.h`. **Choose one and list every file in R2:** (a) add a project-local V4 board JSON plus its Arduino variant and point the variants dir at it, or (b) extend a supported generic ESP32-S3 board and spell out flash (16 MB), PSRAM (2 MB), partitions, USB-CDC and every pin. **A clean checkout must build both V4 envs without any file from a local MeshCore clone.**

## 6. Sleep — `power_off()` is NOT for our sleep path

**Verified** (`src/fw_main.cpp:918-947`): MeshRoute uses **light sleep with the radio in continuous RX**, woken by DIO1 RxDone (`esp_sleep_enable_ext1_wakeup` on `LORA_PIN_DIO1`) or the next-timer deadline. The header comment says it outright: *"The radio stays in continuous RX, so a DIO1 RxDone (an incoming frame) wakes us."*

⇒ **powering the front end down there would make the node deaf** — the LNA leaves circuit while we depend on RxDone to wake. Rules:

- the optional method is named **`power_off()`**, deep-power-off only, and **is not called from `board_sleep_until()`**;
- light sleep retains RX mode and GPIO state; nothing in R1-R4 calls `power_off()` at all;
- if deep sleep with radio wake is added later it needs its own lifecycle (RTC holds configured exactly as the FEM requires) — **a separate slice**, and the warm/deep-reset GPIO-hold question belongs to it;
- **deep-sleep current is therefore NOT an R3 acceptance criterion** (this tree has no V4 deep-sleep path to measure).

## 7. Slices

| # | slice | gate |
|---|---|---|
| **R1** | `lib/hal/iboardrf.h`; the three placements; `_fem = nullptr` everywhere; `_fem_ready` + fail-closed TX; **`Sx1262Radio::begin()`'s bool folded into `g_radio_ok`** | s18 EXACT *(core-only — see §8)* + all board envs + **on-target null-FEM smoke on V3 and XIAO**: TX, RX, arm-failure, watchdog abort |
| **R2** | V4 board assets (§5) + `heltec_v4` / `heltec_v4_mobile` envs + pins + register patch. **No FEM yet** | both V4 envs build **from a clean checkout**; ⚠ **do not transmit on real V4 hardware between R2 and R3** |
| **R3** | `src/board_rf_heltec_v4.cpp` — revision detect, `begin`/`tx_mode`/`rx_mode`, fail-closed; wire into the V4 ctor | on-target: the cold-boot / warm-reset / light-sleep-wake matrix; instrumented mode-transition trace (§8) |
| **R4** | `tx_power` = conducted dBm: the `MR_DEFAULT_OUTPUT_DBM` split, `drive_for_output`/min/max, runtime clamps, boot validation, the measured table, the §10 renames | s18 EXACT + board envs + **the calibration below, which is an ENTRY criterion** |

**R4 entry criteria — not open questions:**

- a **measured** table for **each detected revision**, or a documented conservative fallback with a deliberately restricted output set for the uncalibrated one;
- measurement across the **supported frequency band**, worst-case-safe. ⚠ `cfg set freq` currently accepts 100..1000 MHz (`src/firmware_config.cpp`), which no single-frequency calibration can justify — **the V4 env must declare its supported interval and refuse out-of-band configuration**;
- unknown revision, missing table or out-of-band frequency ⇒ **refuse TX, or cap to an explicitly safe bring-up point**.

R3 switching may land before full calibration, but **an uncalibrated R4 is a bench configuration, never a release one**.

## 8. Test strategy — and an honest account of what each gate proves

⚠ **s18 does NOT exercise this seam.** `Sx1262Radio` is device-only (it pulls RadioLib); s18 is the simulator. An exact digest proves **the core is unchanged** — a necessary, orthogonal check. It says nothing about hook placement, and the board binary will **not** be byte-identical once a pointer and branches are added. An earlier revision claimed s18 exactness was "the whole proof the seam is inert"; **that was wrong** and is retracted.

**What actually covers the seam:**

| level | coverage |
|---|---|
| **native** | ordering logic, *if* a small sequencing component used by production is extracted and driven with a fake radio + fake FEM. ⚠ A test double that merely replays an invented order tests itself — either share the production path or drop this level and rely on the instrumented target test |
| **on-target, null FEM (V3 + XIAO)** | TX, RX, `start_transmit` arm-failure, watchdog abort — proves R1 did not disturb boards without a front end |
| **on-target, V4** | an instrumented mode-transition trace asserting **no `startTransmit` outside TX mode** and **no `startReceive` outside RX mode**, across boot, normal traffic, arm failure and abort |
| **review** | the `startReceive`/`startTransmit` grep of §3.2 — a check, not a gate |

**Metal (M2 — add to `docs/2026-07-31-bench-test-script.md`)**, none of it reachable by any automated gate:

1. **The R4 calibration**: conducted output at each table point, measured with an **RF power meter / spectrum analyser** with rated attenuation and a **stated uncertainty**, per revision, across the declared band. ⚠ **Second-node RSSI is a relative functional check only** — it cannot calibrate absolute power unless the whole link is itself calibrated, and an earlier revision of this spec wrongly offered it as an alternative.
2. RX sensitivity sanity: the V4 hears a distant node the V3 also hears (LNA genuinely in circuit).
3. **Both silicon revisions**, if both are to hand — the detect is a runtime branch no build can prove.
4. FEM-init failure behaviour: force a failed detect and confirm **no TX**, visible reason.

## 9. Naming — finish the rename in R4

`tx_power`'s meaning changes, so the declarations that still say "SX1262 / chip dBm" must change with it: `src/device_nv.h:48`, `lib/hal/iradio.h:30-31`, `lib/core/hal.h:29`, `src/fw_main.cpp:189`, and the console help/status strings. **Keep the two meanings lexically distinct**: the value above the board translator is `output_dbm`; the value handed to RadioLib is `chip_dbm`. That is what stops them reconverging later.

## 10. Open questions for the reviewer

1. **§4 EIRP boundary.** Conducted-at-connector is ruled and specified. Is a region/antenna policy layer *wanted* on top, and if so does it belong to the address-book/config arc rather than the radio? This spec deliberately stops at the connector.
2. **§8 native level.** Extracting a production-shared sequencing component from `Sx1262Radio` may cost more than it returns, given the on-target trace covers the same property. My inclination is to **skip the native level and rely on the instrumented target test**, but a second opinion is worth having before someone builds a self-testing double.
3. **§5 board assets.** (a) project-local V4 JSON + Arduino variant, or (b) generic ESP32-S3 base plus explicit properties? (a) matches MeshCore and is reproducible; (b) avoids carrying vendor manifests. No strong preference — but it must be decided *in R2*, not discovered during it.
4. **§7 band declaration.** What *is* the V4's supported frequency interval for calibration purposes? Needs the hardware's matching network, not a guess.
5. **§4.2** — is `LORA_TX_POWER=10` the right bring-up value for V4's vendored `std_init()`, or should it be lower until R4's table exists?
